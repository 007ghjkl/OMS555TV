#include "testing/TestEngine.h"

#include <QVariantMap>
#include <QMap>
#include <QPointer>

#include <algorithm>
#include <limits>
#include <variant>

namespace oms555tv::testing {
namespace {

TestError makeError(TestErrorCode code, QString diagnostic)
{
    return {code, std::move(diagnostic), std::nullopt};
}

QString retryName(communication::ErrorCategory category)
{
    switch (category) {
    case communication::ErrorCategory::Timeout: return QStringLiteral("timeout");
    case communication::ErrorCategory::Connection: return QStringLiteral("connection");
    case communication::ErrorCategory::Serial: return QStringLiteral("serial");
    case communication::ErrorCategory::Crc: return QStringLiteral("crc");
    case communication::ErrorCategory::Protocol: return QStringLiteral("protocol");
    default: return {};
    }
}

std::optional<RetryError> retryError(communication::ErrorCategory category)
{
    switch (category) {
    case communication::ErrorCategory::Timeout: return RetryError::Timeout;
    case communication::ErrorCategory::Connection: return RetryError::Connection;
    case communication::ErrorCategory::Serial: return RetryError::Serial;
    case communication::ErrorCategory::Crc: return RetryError::Crc;
    case communication::ErrorCategory::Protocol: return RetryError::Protocol;
    default: return std::nullopt;
    }
}

} // namespace

TestEngine::TestEngine(app::AppStateController &appState,
                       communication::IModbusClient &client,
                       TestResultManager &results,
                       logging::SessionLogService *sessionLog,
                       Clock clock,
                       TestHandlerRegistry registry,
                       QObject *parent)
    : QObject(parent)
    , appState_(appState)
    , client_(client)
    , results_(results)
    , sessionLog_(sessionLog)
    , clock_(clock ? std::move(clock) : [] { return QDateTime::currentDateTimeUtc(); })
    , registry_(std::move(registry))
    , ownedScheduler_(std::make_unique<monitor::QtMonitorScheduler>())
    , scheduler_(ownedScheduler_.get())
{
    qRegisterMetaType<TestRunId>();
    qRegisterMetaType<TestEngineState>();
    qRegisterMetaType<TestSuiteResult>();
    connect(&client_, &communication::IModbusClient::requestCompleted,
            this, &TestEngine::handleRequestCompleted);
    connect(&appState_, &app::AppStateController::commandCompleted,
            this, &TestEngine::handleAppCommandCompleted);
}

TestEngine::TestEngine(app::AppStateController &appState,
                       communication::IModbusClient &client,
                       TestResultManager &results,
                       logging::SessionLogService *sessionLog,
                       monitor::IMonitorScheduler &scheduler,
                       TestHandlerRegistry registry,
                       QObject *parent)
    : TestEngine(appState, client, results, sessionLog,
                 [&scheduler] { return scheduler.utcNow(); },
                 std::move(registry), parent)
{
    scheduler_ = &scheduler;
    ownedScheduler_.reset();
}

TestEngine::~TestEngine()
{
    if (state_ == TestEngineState::Idle && !guidedProbe_) {
        return;
    }
    disconnect(&client_, nullptr, this, nullptr);
    disconnect(&appState_, nullptr, this, nullptr);
    cancelDelay();
    if (currentRequestId_) {
        (void)client_.cancelRequest(*currentRequestId_);
    }
    if (guidedProbe_) {
        (void)client_.cancelRequest(guidedProbe_->requestId);
    }
    if (appState_.state() == app::AppState::Testing
        && client_.activeOwner() == communication::CommunicationOwner::Testing) {
        (void)appState_.stopTesting();
    }
}

TestEngineState TestEngine::state() const noexcept { return state_; }

std::optional<TestRunId> TestEngine::activeRunId() const noexcept
{
    return run_ ? std::optional<TestRunId>(run_->runId) : std::nullopt;
}

bool TestEngine::guidedProbeActive() const noexcept { return guidedProbe_.has_value(); }

monitor::IMonitorScheduler &TestEngine::scheduler() const noexcept { return *scheduler_; }

logging::SessionLogService *TestEngine::sessionLog() const noexcept { return sessionLog_; }

QDateTime TestEngine::nowUtc() const { return clock_().toUTC(); }

std::optional<TestRunId> TestEngine::allocateRunId() noexcept
{
    if (nextRunId_ == 0) {
        return std::nullopt;
    }
    const TestRunId result{nextRunId_};
    nextRunId_ = nextRunId_ == std::numeric_limits<quint64>::max() ? 0 : nextRunId_ + 1;
    return result;
}

TestRunSubmission TestEngine::reject(TestErrorCode code, QString diagnostic) const
{
    return {std::nullopt, makeError(code, std::move(diagnostic))};
}

void TestEngine::setState(TestEngineState state)
{
    if (state_ == state) {
        return;
    }
    state_ = state;
    emit stateChanged(state_);
}

TestRunSubmission TestEngine::runSuite(const TestSuite &suite,
                                       const QSet<QString> &selectedCaseIds)
{
    if (state_ != TestEngineState::Idle || run_ || guidedProbe_) {
        return reject(TestErrorCode::AlreadyRunning, QStringLiteral("已有测试运行"));
    }
    if (appState_.state() != app::AppState::Testing
        || client_.connectionState() != communication::ConnectionState::Connected
        || client_.activeOwner() != communication::CommunicationOwner::Testing) {
        return reject(TestErrorCode::InvalidState,
                      QStringLiteral("只有 TESTING 状态和 Testing owner 可以执行测试"));
    }
    QSet<QString> known;
    for (const auto &testCase : suite.cases) {
        known.insert(testCase.id);
    }
    for (const auto &id : selectedCaseIds) {
        if (!known.contains(id)) {
            return reject(TestErrorCode::UnknownCaseId,
                          QStringLiteral("选择了不存在的用例：%1").arg(id));
        }
    }
    const auto id = allocateRunId();
    if (!id) {
        return reject(TestErrorCode::InvariantViolation, QStringLiteral("运行 ID 已耗尽"));
    }

    run_.emplace();
    run_->runId = *id;
    run_->suite = std::make_shared<const TestSuite>(suite);
    run_->status = TestStatus::Running;
    run_->startedUtc = nowUtc();
    run_->sessionId = sessionLog_ ? sessionLog_->sessionId() : QString{};
    runStartedMonotonic_ = scheduler_->monotonicNow();
    selected_ = selectedCaseIds;
    currentCaseIndex_ = -1;
    nextAttemptSequence_ = 1;
    loggingErrorRecorded_ = false;
    for (const auto &testCase : suite.cases) {
        TestCaseResult result;
        result.caseId = testCase.id;
        result.declaredType = testCase.declaredType;
        result.type = testCase.type;
        result.expected = testCase.expected;
        result.sessionId = run_->sessionId;
        result.evidenceRetention.description = QStringLiteral("完整保留全部 attempt");
        if (!testCase.enabled) {
            result.status = TestStatus::Skipped;
            result.skipReason = TestSkipReason::Disabled;
        } else if (!selected_.isEmpty() && !selected_.contains(testCase.id)) {
            result.status = TestStatus::Skipped;
            result.skipReason = TestSkipReason::NotSelected;
        }
        run_->cases.append(std::move(result));
    }
    setState(TestEngineState::Running);
    appendLog(QStringLiteral("suite_started"), QStringLiteral("测试套件开始"));
    publish();
    startNextCase();
    return {*id, std::nullopt};
}

GuidedProbeSubmission TestEngine::submitGuidedProbe(
    TestRunId runId,
    const QString &caseId,
    const QString &stepId,
    const TestRequest &request,
    std::chrono::milliseconds timeout)
{
    const auto rejectProbe = [](TestErrorCode code, QString diagnostic,
                                std::optional<communication::CommunicationError> error = std::nullopt) {
        return GuidedProbeSubmission{std::nullopt,
                                     TestError{code, std::move(diagnostic), std::move(error)}};
    };
    if (state_ != TestEngineState::Idle || run_ || guidedProbe_) {
        return rejectProbe(TestErrorCode::AlreadyRunning,
                           QStringLiteral("测试引擎已有活动执行器"));
    }
    if (appState_.state() != app::AppState::Testing
        || client_.connectionState() != communication::ConnectionState::Connected
        || client_.activeOwner() != communication::CommunicationOwner::Testing) {
        return rejectProbe(TestErrorCode::InvalidState,
                           QStringLiteral("引导探测要求 TESTING 状态和 Testing owner"));
    }
    if (runId.value == 0 || caseId.isEmpty() || stepId.isEmpty()
        || request.function != ModbusFunction::ReadHoldingRegisters) {
        return rejectProbe(TestErrorCode::InvariantViolation,
                           QStringLiteral("引导探测上下文或请求无效"));
    }

    communication::RequestOptions options;
    options.owner = communication::CommunicationOwner::Testing;
    options.responseTimeout = std::max(std::chrono::milliseconds(1), timeout);
    options.correlationId = QStringLiteral("guided/%1/%2/%3")
        .arg(runId.value).arg(caseId, stepId);
    const auto submission = client_.readHoldingRegisters(
        request.address, request.count, options);
    if (!submission.accepted()) {
        return rejectProbe(TestErrorCode::RequestRejected,
                           QStringLiteral("通信层拒绝引导探测"), submission.rejection);
    }
    guidedProbe_ = GuidedProbeContext{runId, caseId, stepId, *submission.requestId};
    return {*submission.requestId, std::nullopt};
}

bool TestEngine::cancelGuidedProbe(communication::RequestId requestId)
{
    if (!guidedProbe_ || guidedProbe_->requestId != requestId) {
        return false;
    }
    return client_.cancelRequest(requestId).isAccepted;
}

TestRunSubmission TestEngine::runCase(const TestSuite &suite, const QString &caseId)
{
    bool found = false;
    for (const auto &testCase : suite.cases) {
        found = found || testCase.id == caseId;
    }
    if (!found) {
        return reject(TestErrorCode::UnknownCaseId,
                      QStringLiteral("用例不存在：%1").arg(caseId));
    }
    return runSuite(suite, QSet<QString>{caseId});
}

bool TestEngine::skipCase(const QString &caseId)
{
    if (!run_ || state_ != TestEngineState::Running) {
        return false;
    }
    for (qsizetype index = 0; index < run_->cases.size(); ++index) {
        auto &result = run_->cases[index];
        if (result.caseId != caseId || result.status != TestStatus::NotRun
            || index == currentCaseIndex_) {
            continue;
        }
        result.status = TestStatus::Skipped;
        result.skipReason = TestSkipReason::UserSelected;
        publish();
        return true;
    }
    return false;
}

bool TestEngine::abort()
{
    if (!run_ || (state_ != TestEngineState::Running
                  && state_ != TestEngineState::Aborting)) {
        return false;
    }
    if (state_ == TestEngineState::Aborting) {
        return false;
    }
    run_->aborted = true;
    setState(TestEngineState::Aborting);
    if (scheduledDelay_) {
        cancelDelay();
        applyDecision(handler_->handleCommunicationError(
            makeError(TestErrorCode::Aborted, QStringLiteral("用户中止测试")),
            false, handlerContext()));
        return true;
    }
    if (currentRequestId_ && currentStep_ && !currentStep_->cleanup) {
        (void)client_.cancelRequest(*currentRequestId_);
        return true;
    }
    if (handler_ && currentStep_ && !currentStep_->cleanup) {
        applyDecision(handler_->handleCommunicationError(
            makeError(TestErrorCode::Aborted, QStringLiteral("用户中止测试")),
            false, handlerContext()));
    } else if (!currentRequestId_) {
        markRemainingAborted();
        beginRelease();
    }
    return true;
}

void TestEngine::startNextCase()
{
    if (!run_) {
        return;
    }
    if (state_ == TestEngineState::Aborting) {
        markRemainingAborted();
        beginRelease();
        return;
    }
    do {
        ++currentCaseIndex_;
    } while (currentCaseIndex_ < run_->cases.size()
             && run_->cases[currentCaseIndex_].status != TestStatus::NotRun);
    if (currentCaseIndex_ >= run_->cases.size()) {
        beginRelease();
        return;
    }

    auto &result = run_->cases[currentCaseIndex_];
    const auto &testCase = run_->suite->cases[currentCaseIndex_];
    handler_ = registry_.create(testCase);
    result.status = TestStatus::Running;
    result.startedUtc = nowUtc();
    caseStartedMonotonic_ = scheduler_->monotonicNow();
    currentLogicalStepStartedUtc_ = {};
    currentLogicalAttemptSequences_.clear();
    stabilityRetention_ = {};
    if (testCase.type == TestCaseType::Stability) {
        stabilityRetention_.limit = testCase.stability.evidenceSampleLimit;
        stabilityRetention_.maximumStarts = static_cast<quint64>(
            (testCase.stability.duration.count() + testCase.stability.interval.count() - 1)
            / testCase.stability.interval.count());
        stabilityRetention_.failureCapacity = std::max(2, stabilityRetention_.limit / 2);
        stabilityRetention_.sampleCapacity = std::max(
            1, stabilityRetention_.limit - stabilityRetention_.failureCapacity - 2);
        stabilityRetention_.samplingStride = std::max<quint64>(
            1, (stabilityRetention_.maximumStarts
                + static_cast<quint64>(stabilityRetention_.sampleCapacity) - 1)
                / static_cast<quint64>(stabilityRetention_.sampleCapacity));
        result.evidenceRetention.policy =
            TestEvidenceRetentionPolicy::BoundedRepresentative;
        result.evidenceRetention.configuredLimit = stabilityRetention_.limit;
        result.evidenceRetention.description =
            QStringLiteral("首条、均匀抽样、失败窗口与末条；完整事务见 SessionLog JSONL");
    }
    emit caseStarted(result.caseId);
    appendLog(QStringLiteral("case_started"), QStringLiteral("测试用例开始"), &result);
    publish();
    if (!handler_) {
        TestHandlerDecision decision;
        decision.finished = true;
        decision.status = TestStatus::Error;
        decision.error = makeError(TestErrorCode::HandlerNotFound,
                                   QStringLiteral("没有注册的测试处理器"));
        completeCurrentCase(std::move(decision));
        return;
    }
    applyDecision(handler_->start(handlerContext()));
}

std::chrono::milliseconds TestEngine::caseElapsed() const
{
    if (!run_ || currentCaseIndex_ < 0 || currentCaseIndex_ >= run_->cases.size()) {
        return std::chrono::milliseconds(0);
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::max(std::chrono::nanoseconds::zero(),
                 scheduler_->monotonicNow() - caseStartedMonotonic_));
}

TestHandlerContext TestEngine::handlerContext() const
{
    TestHandlerContext context;
    context.monotonicNow = scheduler_->monotonicNow();
    context.caseElapsed = std::max(std::chrono::nanoseconds::zero(),
                                   context.monotonicNow - caseStartedMonotonic_);
    context.utcNow = nowUtc();
    return context;
}

void TestEngine::submitStep(TestHandlerStep step, bool retry, QString retryReason)
{
    if (!run_ || !handler_ || currentCaseIndex_ < 0) {
        return;
    }
    const auto &testCase = run_->suite->cases[currentCaseIndex_];
    const auto remaining = testCase.timeout.testCase - caseElapsed();
    if (remaining <= std::chrono::milliseconds::zero() && !step.cleanup) {
        applyDecision(handler_->handleCommunicationError(
            makeError(TestErrorCode::CaseTimeout, QStringLiteral("用例总预算已耗尽")),
            false, handlerContext()));
        return;
    }
    const auto timeout = step.cleanup
        ? testCase.timeout.request
        : std::min(testCase.timeout.request, remaining);
    communication::RequestOptions options;
    options.owner = communication::CommunicationOwner::Testing;
    options.responseTimeout = std::max(std::chrono::milliseconds(1), timeout);
    options.correlationId = QStringLiteral("test/%1/%2/%3/%4")
        .arg(run_->runId.value).arg(testCase.id)
        .arg(testStepPurposeName(step.purpose)).arg(stepAttempt_ + 1);
    if (!step.logicalStepId.isEmpty()) {
        options.correlationId += QStringLiteral("/%1/%2")
            .arg(step.repetition).arg(step.logicalStepId);
    }

    if (!retry) {
        currentLogicalStepStartedUtc_ = nowUtc();
        currentLogicalStepStartedMonotonic_ = scheduler_->monotonicNow();
        currentLogicalAttemptSequences_.clear();
    }

    communication::RequestSubmission submission;
    if (step.kind == TestStepKind::Read) {
        submission = client_.readHoldingRegisters(step.address, step.count, options);
    } else {
        submission = client_.writeSingleRegister(step.address, step.rawValue, options);
    }
    if (!submission.accepted()) {
        TestError error{TestErrorCode::RequestRejected,
                        QStringLiteral("通信层拒绝测试请求"), submission.rejection};
        applyDecision(handler_->handleCommunicationError(std::move(error), false,
                                                         handlerContext()));
        return;
    }
    currentStep_ = step;
    currentRequestId_ = submission.requestId;
    handler_->requestAccepted(step, handlerContext());
    currentAttemptStarted_ = nowUtc();
    currentRetryReason_ = retry ? std::move(retryReason) : QString{};
    ++stepAttempt_;
    emit stepStarted(testCase.id, step.purpose, *submission.requestId, stepAttempt_,
                     step.logicalStepId, step.logicalStepIndex, step.repetition);
}

void TestEngine::scheduleDelay(const std::chrono::nanoseconds delay)
{
    cancelDelay();
    const quint64 generation = ++delayGeneration_;
    const QPointer<TestEngine> self(this);
    scheduledDelay_ = scheduler_->scheduleAfter(delay, this, [self, generation] {
        if (!self || !self->scheduledDelay_ || self->delayGeneration_ != generation
            || !self->run_ || !self->handler_
            || self->state_ != TestEngineState::Running) {
            return;
        }
        self->scheduledDelay_.reset();
        self->applyDecision(self->handler_->resume(self->handlerContext()));
    });
}

void TestEngine::cancelDelay() noexcept
{
    ++delayGeneration_;
    if (scheduledDelay_ && scheduler_) {
        scheduler_->cancel(*scheduledDelay_);
    }
    scheduledDelay_.reset();
}

void TestEngine::handleRequestCompleted(
    const communication::ModbusRequestResult &result)
{
    if (guidedProbe_ && result.requestId == guidedProbe_->requestId) {
        const auto context = *guidedProbe_;
        guidedProbe_.reset();
        emit guidedProbeCompleted(context.runId, context.caseId, context.stepId, result);
        return;
    }
    if (!run_ || !currentRequestId_ || result.requestId != *currentRequestId_
        || currentCaseIndex_ < 0 || !handler_ || !currentStep_) {
        return;
    }
    auto &caseResult = run_->cases[currentCaseIndex_];
    TestRequestAttemptResult attempt;
    attempt.sequence = nextAttemptSequence_++;
    attempt.purpose = currentStep_->purpose;
    attempt.stepAttempt = stepAttempt_;
    attempt.retry = stepAttempt_ > 1;
    attempt.retryReason = currentRetryReason_;
    attempt.logicalStepId = currentStep_->logicalStepId;
    attempt.logicalStepIndex = currentStep_->logicalStepIndex;
    attempt.repetition = currentStep_->repetition;
    attempt.requestId = result.requestId;
    attempt.startedUtc = result.evidence.enqueuedUtc.isValid()
        ? result.evidence.enqueuedUtc : currentAttemptStarted_;
    attempt.finishedUtc = result.evidence.completedUtc.isValid()
        ? result.evidence.completedUtc : nowUtc();
    attempt.duration = std::chrono::milliseconds(
        std::max<qint64>(0, attempt.startedUtc.msecsTo(attempt.finishedUtc)));
    attempt.requestResult = result;
    appendLog(QStringLiteral("request_attempt"), QStringLiteral("测试请求完成"),
              &caseResult, &attempt);
    currentLogicalAttemptSequences_.append(attempt.sequence);
    retainAttempt(std::move(attempt), result);
    currentRequestId_.reset();
    currentRetryReason_.clear();

    if (state_ == TestEngineState::Aborting && !currentStep_->cleanup) {
        applyDecision(handler_->handleCommunicationError(
            makeError(TestErrorCode::Aborted, QStringLiteral("用户中止测试")),
            true, handlerContext()));
        return;
    }
    if (result.error
        && result.error->category != communication::ErrorCategory::RemoteException) {
        if (!currentStep_->cleanup && shouldRetry(*result.error)) {
            const QString reason = retryName(result.error->category);
            submitStep(*currentStep_, true, reason);
            return;
        }
        const bool budgetExpired = caseElapsed()
            >= run_->suite->cases[currentCaseIndex_].timeout.testCase;
        applyDecision(handler_->handleCommunicationError(
            requestError(budgetExpired ? TestErrorCode::CaseTimeout
                                       : TestErrorCode::RequestFailed,
                         result,
                         budgetExpired ? QStringLiteral("用例总预算已耗尽")
                                       : QStringLiteral("Modbus 请求失败")),
            true, handlerContext()));
        return;
    }
    applyDecision(handler_->handleResult(result, handlerContext()));
}

bool TestEngine::shouldRetry(const communication::CommunicationError &error) const
{
    if (!run_ || currentCaseIndex_ < 0 || currentStep_->cleanup
        || state_ == TestEngineState::Aborting) {
        return false;
    }
    const auto &policy = currentStep_->retryOverride
        ? *currentStep_->retryOverride : run_->suite->cases[currentCaseIndex_].retry;
    const auto mapped = retryError(error.category);
    return mapped && stepAttempt_ <= policy.maxRetries
        && policy.onErrors.contains(*mapped)
        && caseElapsed() < run_->suite->cases[currentCaseIndex_].timeout.testCase;
}

TestError TestEngine::requestError(TestErrorCode code,
                                   const communication::ModbusRequestResult &result,
                                   QString diagnostic) const
{
    return {code, std::move(diagnostic), result.error};
}

void TestEngine::applyDecision(TestHandlerDecision decision)
{
    if (!run_) {
        return;
    }

    if (decision.completedStep) {
        recordCompletedStep(std::move(*decision.completedStep));
    }

    const int actionCount = (decision.nextStep ? 1 : 0)
        + (decision.delay ? 1 : 0) + (decision.finished ? 1 : 0);
    if (actionCount != 1) {
        decision.nextStep.reset();
        decision.delay.reset();
        decision.finished = true;
        decision.status = TestStatus::Error;
        decision.error = makeError(
            TestErrorCode::InvariantViolation,
            QStringLiteral("处理器必须且只能产生下一步骤、等待或完成中的一种动作"));
    }

    if (decision.nextStep) {
        currentStep_ = *decision.nextStep;
        stepAttempt_ = 0;
        submitStep(*decision.nextStep);
        publish();
        return;
    }

    if (decision.delay) {
        currentStep_.reset();
        stepAttempt_ = 0;
        const auto &testCase = run_->suite->cases[currentCaseIndex_];
        const auto context = handlerContext();
        const auto budget = std::chrono::duration_cast<std::chrono::nanoseconds>(
            testCase.timeout.testCase);
        const auto remaining = budget - context.caseElapsed;
        if (remaining <= std::chrono::nanoseconds::zero()
            || *decision.delay > remaining) {
            applyDecision(handler_->handleCommunicationError(
                makeError(TestErrorCode::CaseTimeout,
                          QStringLiteral("等待将耗尽用例总预算")),
                false, context));
            return;
        }
        scheduleDelay(*decision.delay);
        publish();
        return;
    }

    completeCurrentCase(std::move(decision));
}

void TestEngine::recordCompletedStep(TestCompositeStepResult step)
{
    if (!run_ || currentCaseIndex_ < 0
        || currentCaseIndex_ >= run_->cases.size()) {
        return;
    }
    const auto context = handlerContext();
    const auto startedMonotonic = currentLogicalStepStartedUtc_.isValid()
        ? currentLogicalStepStartedMonotonic_ : context.monotonicNow;
    step.startedUtc = currentLogicalStepStartedUtc_.isValid()
        ? currentLogicalStepStartedUtc_ : context.utcNow;
    step.finishedUtc = context.utcNow;
    step.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::max(std::chrono::nanoseconds::zero(),
                 context.monotonicNow - startedMonotonic));
    step.attemptSequences = currentLogicalAttemptSequences_;
    run_->cases[currentCaseIndex_].steps.append(std::move(step));
    currentLogicalStepStartedUtc_ = {};
    currentLogicalStepStartedMonotonic_ = std::chrono::nanoseconds::zero();
    currentLogicalAttemptSequences_.clear();
}

void TestEngine::retainAttempt(
    TestRequestAttemptResult attempt,
    const communication::ModbusRequestResult &requestResult)
{
    if (!run_ || currentCaseIndex_ < 0
        || currentCaseIndex_ >= run_->cases.size()) {
        return;
    }
    auto &caseResult = run_->cases[currentCaseIndex_];
    auto &summary = caseResult.evidenceRetention;
    ++summary.totalAttempts;
    const bool failed = !requestResult.success.has_value();
    if (failed) {
        ++summary.totalFailures;
    }

    if (summary.policy != TestEvidenceRetentionPolicy::BoundedRepresentative) {
        caseResult.attempts.append(std::move(attempt));
        summary.retainedAttempts = static_cast<quint64>(caseResult.attempts.size());
        summary.droppedAttempts = 0;
        summary.retainedFailures = summary.totalFailures;
        return;
    }

    if (stabilityRetention_.head.isEmpty()) {
        stabilityRetention_.head.append(attempt);
    }
    if (failed) {
        if (stabilityRetention_.failures.size() < stabilityRetention_.failureCapacity) {
            stabilityRetention_.failures.append(attempt);
        } else if (!stabilityRetention_.failures.isEmpty()) {
            stabilityRetention_.failures.last() = attempt;
        }
    }
    const quint64 ordinal = summary.totalAttempts;
    if (ordinal % stabilityRetention_.samplingStride == 0
        && stabilityRetention_.samples.size() < stabilityRetention_.sampleCapacity) {
        stabilityRetention_.samples.append(attempt);
    }
    stabilityRetention_.tail = {std::move(attempt)};
    refreshStabilityRetention();
}

void TestEngine::refreshStabilityRetention()
{
    if (!run_ || currentCaseIndex_ < 0
        || currentCaseIndex_ >= run_->cases.size()) {
        return;
    }
    auto &caseResult = run_->cases[currentCaseIndex_];
    QMap<quint64, TestRequestAttemptResult> retained;
    const auto merge = [&retained](const QVector<TestRequestAttemptResult> &bucket) {
        for (const auto &attempt : bucket) {
            retained.insert(attempt.sequence, attempt);
        }
    };
    merge(stabilityRetention_.head);
    merge(stabilityRetention_.samples);
    merge(stabilityRetention_.failures);
    merge(stabilityRetention_.tail);
    caseResult.attempts = retained.values();

    auto &summary = caseResult.evidenceRetention;
    summary.retainedAttempts = static_cast<quint64>(caseResult.attempts.size());
    summary.droppedAttempts = summary.totalAttempts - summary.retainedAttempts;
    summary.retainedFailures = 0;
    for (const auto &attempt : caseResult.attempts) {
        if (!attempt.requestResult.success) {
            ++summary.retainedFailures;
        }
    }
}

void TestEngine::completeCurrentCase(TestHandlerDecision decision)
{
    if (!run_ || currentCaseIndex_ < 0 || currentCaseIndex_ >= run_->cases.size()) {
        return;
    }
    cancelDelay();
    auto &result = run_->cases[currentCaseIndex_];
    result.status = decision.status;
    result.actual = std::move(decision.actual);
    if (result.actual
        && result.actual->type == ActualResultType::StabilitySummary) {
        result.stability = result.actual->stability;
    }
    result.assertion = std::move(decision.assertion);
    result.error = std::move(decision.error);
    result.cleanupError = std::move(decision.cleanupError);
    result.finishedUtc = nowUtc();
    result.duration = caseElapsed();
    currentRequestId_.reset();
    currentRetryReason_.clear();
    currentStep_.reset();
    currentLogicalStepStartedUtc_ = {};
    currentLogicalStepStartedMonotonic_ = std::chrono::nanoseconds::zero();
    currentLogicalAttemptSequences_.clear();
    handler_.reset();
    appendLog(QStringLiteral("case_finished"), QStringLiteral("测试用例结束"), &result);
    emit caseFinished(result.caseId, result.status);
    publish();
    if (state_ == TestEngineState::Aborting) {
        markRemainingAborted();
        beginRelease();
    } else {
        startNextCase();
    }
}

void TestEngine::markRemainingAborted()
{
    if (!run_) {
        return;
    }
    for (qsizetype index = std::max<qsizetype>(0, currentCaseIndex_ + 1);
         index < run_->cases.size(); ++index) {
        auto &result = run_->cases[index];
        if (result.status == TestStatus::NotRun) {
            result.status = TestStatus::Skipped;
            result.skipReason = TestSkipReason::Aborted;
        }
    }
    publish();
}

void TestEngine::beginRelease()
{
    if (!run_ || state_ == TestEngineState::Releasing) {
        return;
    }
    setState(TestEngineState::Releasing);
    const auto submission = appState_.stopTesting();
    if (!submission.accepted()) {
        run_->auxiliaryErrors.append({TestErrorCode::StopTestingRejected,
                                      QStringLiteral("AppStateController 拒绝释放 Testing owner"),
                                      submission.rejection
                                          ? submission.rejection->communicationError
                                          : std::nullopt});
        finalizeRun();
        return;
    }
    stopOperationId_ = submission.operationId;
}

void TestEngine::handleAppCommandCompleted(const app::AppCommandResult &result)
{
    if (!run_ || state_ != TestEngineState::Releasing || !stopOperationId_
        || !(result.operationId == *stopOperationId_)
        || result.command != app::AppCommand::StopTesting) {
        return;
    }
    stopOperationId_.reset();
    if (!result.succeeded || appState_.state() != app::AppState::ConnectedIdle
        || client_.activeOwner() != communication::CommunicationOwner::None) {
        TestError error = makeError(TestErrorCode::StateMismatch,
                                    QStringLiteral("Testing owner 释放后的应用状态不一致"));
        if (result.error && result.error->communicationError) {
            error.communicationError = result.error->communicationError;
        }
        run_->auxiliaryErrors.append(std::move(error));
    }
    finalizeRun();
}

void TestEngine::finalizeRun()
{
    if (!run_) {
        return;
    }
    run_->status = TestStatus::Pass;
    for (const auto &testCase : run_->cases) {
        if (testCase.status == TestStatus::Error) {
            run_->status = TestStatus::Error;
            break;
        }
        if (testCase.status == TestStatus::Fail) {
            run_->status = TestStatus::Fail;
        }
    }
    for (const auto &error : run_->auxiliaryErrors) {
        if (error.code != TestErrorCode::LoggingFailed) {
            run_->status = TestStatus::Error;
        }
    }
    cancelDelay();
    run_->finishedUtc = nowUtc();
    run_->duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::max(std::chrono::nanoseconds::zero(),
                 scheduler_->monotonicNow() - runStartedMonotonic_));
    appendLog(QStringLiteral("suite_finished"), QStringLiteral("测试套件结束"));
    publish(true);
    const TestSuiteResult completed = *run_;
    run_.reset();
    handler_.reset();
    currentStep_.reset();
    currentRequestId_.reset();
    currentCaseIndex_ = -1;
    selected_.clear();
    setState(TestEngineState::Idle);
    emit runCompleted(completed);
}

void TestEngine::publish(bool terminal)
{
    if (run_) {
        results_.publish(*run_, terminal);
    }
}

void TestEngine::appendLog(QString event, QString message,
                           const TestCaseResult *testCase,
                           const TestRequestAttemptResult *attempt)
{
    if (!sessionLog_ || !run_) {
        return;
    }
    logging::LogEntry entry;
    entry.timestamp = nowUtc();
    entry.level = logging::LogLevel::Test;
    entry.module = QStringLiteral("testing");
    entry.event = std::move(event);
    entry.message = std::move(message);
    entry.metadata.insert(QStringLiteral("run_id"),
                          static_cast<qulonglong>(run_->runId.value));
    entry.metadata.insert(QStringLiteral("suite_id"), run_->suite->id);
    entry.metadata.insert(QStringLiteral("suite_status"), testStatusName(run_->status));
    if (testCase) {
        entry.metadata.insert(QStringLiteral("case_id"), testCase->caseId);
        entry.metadata.insert(QStringLiteral("status"), testStatusName(testCase->status));
        entry.metadata.insert(QStringLiteral("retention_policy"),
                              testEvidenceRetentionPolicyName(
                                  testCase->evidenceRetention.policy));
        entry.metadata.insert(QStringLiteral("attempt_total"),
                              static_cast<qulonglong>(
                                  testCase->evidenceRetention.totalAttempts));
        entry.metadata.insert(QStringLiteral("attempt_retained"),
                              static_cast<qulonglong>(
                                  testCase->evidenceRetention.retainedAttempts));
        entry.metadata.insert(QStringLiteral("attempt_dropped"),
                              static_cast<qulonglong>(
                                  testCase->evidenceRetention.droppedAttempts));
        if (testCase->stability) {
            entry.metadata.insert(QStringLiteral("stability_success"),
                                  static_cast<qulonglong>(
                                      testCase->stability->successes));
            entry.metadata.insert(QStringLiteral("stability_failure"),
                                  static_cast<qulonglong>(
                                      testCase->stability->failures));
            entry.metadata.insert(QStringLiteral("stability_timeout"),
                                  static_cast<qulonglong>(
                                      testCase->stability->timeouts));
            entry.metadata.insert(QStringLiteral("stability_missing_rtt"),
                                  static_cast<qulonglong>(
                                      testCase->stability->missingRttSamples));
        }
    }
    if (attempt) {
        entry.requestId = attempt->requestId.value;
        entry.metadata.insert(QStringLiteral("step"), testStepPurposeName(attempt->purpose));
        entry.metadata.insert(QStringLiteral("attempt"), attempt->stepAttempt);
        entry.metadata.insert(QStringLiteral("logical_step_id"), attempt->logicalStepId);
        entry.metadata.insert(QStringLiteral("logical_step_index"),
                              static_cast<qlonglong>(attempt->logicalStepIndex));
        entry.metadata.insert(QStringLiteral("repetition"), attempt->repetition);
        entry.metadata.insert(QStringLiteral("attempt_sequence"),
                              static_cast<qulonglong>(attempt->sequence));
    }
    sessionLog_->append(std::move(entry));
    if (!loggingErrorRecorded_ && sessionLog_->lastError()) {
        loggingErrorRecorded_ = true;
        run_->auxiliaryErrors.append({TestErrorCode::LoggingFailed,
                                      sessionLog_->lastError()->diagnostic,
                                      std::nullopt});
    }
}

} // namespace oms555tv::testing

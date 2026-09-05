#include "testing/TestEngine.h"

#include <QVariantMap>

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
{
    qRegisterMetaType<TestRunId>();
    qRegisterMetaType<TestEngineState>();
    qRegisterMetaType<TestSuiteResult>();
    connect(&client_, &communication::IModbusClient::requestCompleted,
            this, &TestEngine::handleRequestCompleted);
    connect(&appState_, &app::AppStateController::commandCompleted,
            this, &TestEngine::handleAppCommandCompleted);
}

TestEngine::~TestEngine()
{
    if (state_ == TestEngineState::Idle) {
        return;
    }
    disconnect(&client_, nullptr, this, nullptr);
    disconnect(&appState_, nullptr, this, nullptr);
    if (currentRequestId_) {
        (void)client_.cancelRequest(*currentRequestId_);
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
    if (state_ != TestEngineState::Idle || run_) {
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
    if (currentRequestId_ && currentStep_ && !currentStep_->cleanup) {
        (void)client_.cancelRequest(*currentRequestId_);
        return true;
    }
    if (handler_ && currentStep_ && !currentStep_->cleanup) {
        applyDecision(handler_->handleCommunicationError(
            makeError(TestErrorCode::Aborted, QStringLiteral("用户中止测试")), false));
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
    applyDecision(handler_->start());
}

std::chrono::milliseconds TestEngine::caseElapsed() const
{
    if (!run_ || currentCaseIndex_ < 0 || currentCaseIndex_ >= run_->cases.size()) {
        return std::chrono::milliseconds(0);
    }
    const auto elapsed = run_->cases[currentCaseIndex_].startedUtc.msecsTo(nowUtc());
    return std::chrono::milliseconds(std::max<qint64>(0, elapsed));
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
            makeError(TestErrorCode::CaseTimeout, QStringLiteral("用例总预算已耗尽")), false));
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

    communication::RequestSubmission submission;
    if (step.kind == TestStepKind::Read) {
        submission = client_.readHoldingRegisters(step.address, step.count, options);
    } else {
        submission = client_.writeSingleRegister(step.address, step.rawValue, options);
    }
    if (!submission.accepted()) {
        TestError error{TestErrorCode::RequestRejected,
                        QStringLiteral("通信层拒绝测试请求"), submission.rejection};
        applyDecision(handler_->handleCommunicationError(std::move(error), false));
        return;
    }
    currentStep_ = step;
    currentRequestId_ = submission.requestId;
    handler_->requestAccepted(step);
    currentAttemptStarted_ = nowUtc();
    currentRetryReason_ = retry ? std::move(retryReason) : QString{};
    ++stepAttempt_;
    emit stepStarted(testCase.id, step.purpose, *submission.requestId, stepAttempt_);
}

void TestEngine::handleRequestCompleted(
    const communication::ModbusRequestResult &result)
{
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
    attempt.requestId = result.requestId;
    attempt.startedUtc = result.evidence.enqueuedUtc.isValid()
        ? result.evidence.enqueuedUtc : currentAttemptStarted_;
    attempt.finishedUtc = result.evidence.completedUtc.isValid()
        ? result.evidence.completedUtc : nowUtc();
    attempt.duration = std::chrono::milliseconds(
        std::max<qint64>(0, attempt.startedUtc.msecsTo(attempt.finishedUtc)));
    attempt.requestResult = result;
    caseResult.attempts.append(attempt);
    appendLog(QStringLiteral("request_attempt"), QStringLiteral("测试请求完成"),
              &caseResult, &caseResult.attempts.back());
    currentRequestId_.reset();
    currentRetryReason_.clear();

    if (state_ == TestEngineState::Aborting && !currentStep_->cleanup) {
        applyDecision(handler_->handleCommunicationError(
            makeError(TestErrorCode::Aborted, QStringLiteral("用户中止测试")), true));
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
            true));
        return;
    }
    applyDecision(handler_->handleResult(result));
}

bool TestEngine::shouldRetry(const communication::CommunicationError &error) const
{
    if (!run_ || currentCaseIndex_ < 0 || currentStep_->cleanup
        || state_ == TestEngineState::Aborting) {
        return false;
    }
    const auto &policy = run_->suite->cases[currentCaseIndex_].retry;
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
    if (decision.nextStep) {
        currentStep_ = *decision.nextStep;
        stepAttempt_ = 0;
        submitStep(*decision.nextStep);
        publish();
        return;
    }
    if (!decision.finished) {
        decision.finished = true;
        decision.status = TestStatus::Error;
        decision.error = makeError(TestErrorCode::InvariantViolation,
                                   QStringLiteral("处理器既未完成也未产生下一步骤"));
    }
    completeCurrentCase(std::move(decision));
}

void TestEngine::completeCurrentCase(TestHandlerDecision decision)
{
    if (!run_ || currentCaseIndex_ < 0 || currentCaseIndex_ >= run_->cases.size()) {
        return;
    }
    auto &result = run_->cases[currentCaseIndex_];
    result.status = decision.status;
    result.actual = std::move(decision.actual);
    result.assertion = std::move(decision.assertion);
    result.error = std::move(decision.error);
    result.cleanupError = std::move(decision.cleanupError);
    result.finishedUtc = nowUtc();
    result.duration = std::chrono::milliseconds(
        std::max<qint64>(0, result.startedUtc.msecsTo(result.finishedUtc)));
    currentRequestId_.reset();
    currentRetryReason_.clear();
    currentStep_.reset();
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
    run_->finishedUtc = nowUtc();
    run_->duration = std::chrono::milliseconds(
        std::max<qint64>(0, run_->startedUtc.msecsTo(run_->finishedUtc)));
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
    }
    if (attempt) {
        entry.requestId = attempt->requestId.value;
        entry.metadata.insert(QStringLiteral("step"), testStepPurposeName(attempt->purpose));
        entry.metadata.insert(QStringLiteral("attempt"), attempt->stepAttempt);
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

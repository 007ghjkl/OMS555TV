#include "testing/GuidedTestCoordinator.h"

#include <QPointer>
#include <QUuid>

#include <algorithm>

namespace oms555tv::testing {
namespace {

TestError makeError(TestErrorCode code, QString diagnostic,
                    std::optional<communication::CommunicationError> communicationError = std::nullopt)
{
    return {code, std::move(diagnostic), std::move(communicationError)};
}

TestStatus statusFor(GuidedTerminalReason reason)
{
    switch (reason) {
    case GuidedTerminalReason::Pass: return TestStatus::Pass;
    case GuidedTerminalReason::OutageNotDetected:
    case GuidedTerminalReason::RecoveryTimeout: return TestStatus::Fail;
    case GuidedTerminalReason::OperatorCancelled: return TestStatus::Skipped;
    case GuidedTerminalReason::OperatorTimeout:
    case GuidedTerminalReason::FatalCommunicationError:
    case GuidedTerminalReason::UserAborted:
    case GuidedTerminalReason::InternalError: return TestStatus::Error;
    case GuidedTerminalReason::None: return TestStatus::NotRun;
    }
    return TestStatus::Error;
}

quint64 monotonicMilliseconds(const monitor::IMonitorScheduler &scheduler)
{
    return static_cast<quint64>(std::max<qint64>(0,
        std::chrono::duration_cast<std::chrono::milliseconds>(
            scheduler.monotonicNow()).count()));
}

} // namespace

GuidedTestCoordinator::GuidedTestCoordinator(
    app::AppStateController &appState,
    TestEngine &engine,
    TestResultManager &results,
    logging::SessionLogService *sessionLog,
    monitor::IMonitorScheduler &scheduler,
    QObject *parent)
    : QObject(parent)
    , appState_(appState)
    , engine_(engine)
    , results_(results)
    , sessionLog_(sessionLog)
    , scheduler_(scheduler)
{
    connect(&engine_, &TestEngine::guidedProbeCompleted,
            this, &GuidedTestCoordinator::handleProbeCompleted);
    connect(&appState_, &app::AppStateController::commandCompleted,
            this, &GuidedTestCoordinator::handleAppCommandCompleted);
}

GuidedTestCoordinator::~GuidedTestCoordinator()
{
    invalidateStage();
    if (appState_.state() == app::AppState::Testing) {
        (void)appState_.stopTesting();
    }
}

GuidedCoordinatorState GuidedTestCoordinator::coordinatorState() const noexcept
{
    return coordinatorState_;
}

bool GuidedTestCoordinator::active() const noexcept
{
    return coordinatorState_ != GuidedCoordinatorState::Idle;
}

GuidedExecutionView GuidedTestCoordinator::view() const
{
    auto result = view_;
    if (active() && stageDeadlineMonotonic_ > std::chrono::nanoseconds::zero()) {
        result.remaining = remainingTo(stageDeadlineMonotonic_);
    }
    return result;
}

QString GuidedTestCoordinator::lastError() const { return lastError_; }

bool GuidedTestCoordinator::runSuite(const TestSuite &suite,
                                     const QSet<QString> &selectedCaseIds)
{
    if (active() || engine_.state() != TestEngineState::Idle
        || engine_.guidedProbeActive()) {
        lastError_ = QStringLiteral("已有引导或自动测试运行");
        return false;
    }
    if (suite.schemaVersion != testSuiteSchemaVersionV3
        || appState_.state() != app::AppState::Testing) {
        lastError_ = QStringLiteral("引导套件要求 Schema v3 和 TESTING 状态");
        return false;
    }
    QSet<QString> known;
    for (const auto &testCase : suite.cases) known.insert(testCase.id);
    for (const auto &id : selectedCaseIds) {
        if (!known.contains(id)) {
            lastError_ = QStringLiteral("选择了不存在的用例：%1").arg(id);
            return false;
        }
    }
    if (nextRunId_ == 0) {
        lastError_ = QStringLiteral("引导运行 ID 已耗尽");
        return false;
    }

    run_.emplace();
    run_->runId = TestRunId{nextRunId_++};
    run_->suite = std::make_shared<const TestSuite>(suite);
    run_->status = TestStatus::Running;
    run_->startedUtc = scheduler_.utcNow();
    run_->sessionId = sessionLog_ ? sessionLog_->sessionId() : QString{};
    selected_ = selectedCaseIds;
    currentCaseIndex_ = -1;
    currentStepIndex_ = -1;
    nextAttemptSequence_ = 1;
    loggingErrorRecorded_ = false;
    lastError_.clear();
    runStartedMonotonic_ = scheduler_.monotonicNow();
    for (const auto &testCase : suite.cases) {
        TestCaseResult result;
        result.caseId = testCase.id;
        result.declaredType = testCase.declaredType;
        result.type = testCase.type;
        result.expected = testCase.expected;
        result.sessionId = run_->sessionId;
        result.evidenceRetention.description = QStringLiteral("完整保留全部引导探测");
        if (!testCase.enabled) {
            result.status = TestStatus::Skipped;
            result.skipReason = TestSkipReason::Disabled;
        } else if (!selected_.isEmpty() && !selected_.contains(testCase.id)) {
            result.status = TestStatus::Skipped;
            result.skipReason = TestSkipReason::NotSelected;
        }
        run_->cases.append(std::move(result));
    }
    coordinatorState_ = GuidedCoordinatorState::Running;
    view_ = {};
    publish();
    startNextCase();
    return true;
}

GuidedActionRejection GuidedTestCoordinator::submitAction(
    const GuidedActionCommand &command)
{
    const auto rejection = validateGuidedAction(actionContext_, command);
    if (rejection != GuidedActionRejection::None) return rejection;

    actionContext_.tokenConsumed = true;
    cancelScheduledTask();
    auto *caseResult = currentCaseResult();
    if (!caseResult || !caseResult->guidedRecovery
        || caseResult->guidedRecovery->operatorActions.isEmpty()) {
        finishCurrentCase(GuidedTerminalReason::InternalError,
                          makeError(TestErrorCode::InvariantViolation,
                                    QStringLiteral("人工动作记录不存在")));
        return GuidedActionRejection::None;
    }
    auto &record = caseResult->guidedRecovery->operatorActions.last();
    record.action = command.action;
    record.actionUtc = scheduler_.utcNow();
    record.waitDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::max(std::chrono::nanoseconds::zero(),
                 scheduler_.monotonicNow() - stageStartedMonotonic_));
    record.note = command.note;

    QVariantMap metadata{{QStringLiteral("step_id"), command.stepId},
                         {QStringLiteral("token"), command.oneTimeToken},
                         {QStringLiteral("action"), guidedOperatorActionName(command.action)}};
    if (command.action == GuidedOperatorAction::Cancel) {
        appendLog(QStringLiteral("operator_cancelled"),
                  QStringLiteral("操作员取消引导测试"), metadata);
        finishCurrentCase(GuidedTerminalReason::OperatorCancelled);
        return GuidedActionRejection::None;
    }

    appendLog(QStringLiteral("operator_confirmed"),
              QStringLiteral("操作员确认人工步骤完成"), metadata);
    if (view_.state == GuidedRunState::WaitingForDisconnectConfirmation) {
        beginObservation(1);
    } else if (view_.state == GuidedRunState::WaitingForReconnectConfirmation) {
        reconnectConfirmedMs_ = monotonicMilliseconds(scheduler_);
        beginObservation(3);
    } else {
        finishCurrentCase(GuidedTerminalReason::InternalError,
                          makeError(TestErrorCode::InvariantViolation,
                                    QStringLiteral("确认动作对应状态无效")));
    }
    return GuidedActionRejection::None;
}

bool GuidedTestCoordinator::abort()
{
    if (coordinatorState_ != GuidedCoordinatorState::Running || !run_) return false;
    finishCurrentCase(GuidedTerminalReason::UserAborted,
                      makeError(TestErrorCode::Aborted, QStringLiteral("用户中止引导测试")));
    return true;
}

void GuidedTestCoordinator::startNextCase()
{
    if (!run_ || coordinatorState_ != GuidedCoordinatorState::Running) return;
    do { ++currentCaseIndex_; }
    while (currentCaseIndex_ < run_->cases.size()
           && run_->cases[currentCaseIndex_].status != TestStatus::NotRun);
    if (currentCaseIndex_ >= run_->cases.size()) {
        beginRelease();
        return;
    }
    const auto *testCase = currentCase();
    auto *result = currentCaseResult();
    if (!testCase || !result || testCase->type != TestCaseType::GuidedRecovery
        || testCase->guidedRecovery.steps.size() != guidedRecoveryStepCount) {
        finishCurrentCase(GuidedTerminalReason::InternalError,
                          makeError(TestErrorCode::InvariantViolation,
                                    QStringLiteral("引导用例结构不符合 v3 固定四步骤契约")));
        return;
    }
    result->status = TestStatus::Running;
    result->startedUtc = scheduler_.utcNow();
    result->guidedRecovery = GuidedRecoveryCaseResult{};
    caseStartedMonotonic_ = scheduler_.monotonicNow();
    reconnectConfirmedMs_.reset();
    firstRecoveryMs_.reset();
    stableRecoveryMs_.reset();
    view_.recoveryTiming.reset();
    view_.restorationReminder.clear();
    showPrompt(0);
}

void GuidedTestCoordinator::showPrompt(qsizetype stepIndex)
{
    const auto *testCase = currentCase();
    auto *result = currentCaseResult();
    if (!testCase || !result || !result->guidedRecovery
        || stepIndex < 0 || stepIndex >= testCase->guidedRecovery.steps.size()
        || !testCase->guidedRecovery.steps[stepIndex].operatorStep) {
        finishCurrentCase(GuidedTerminalReason::InternalError,
                          makeError(TestErrorCode::InvariantViolation,
                                    QStringLiteral("人工提示步骤不存在")));
        return;
    }
    invalidateStage();
    currentStepIndex_ = stepIndex;
    const auto &step = testCase->guidedRecovery.steps[stepIndex];
    const auto &prompt = *step.operatorStep;
    view_.visible = true;
    view_.state = prompt.purpose == GuidedPromptPurpose::DisconnectRs485
        ? GuidedRunState::WaitingForDisconnectConfirmation
        : GuidedRunState::WaitingForReconnectConfirmation;
    view_.terminalReason = GuidedTerminalReason::None;
    view_.runId = run_->runId;
    view_.caseId = testCase->id;
    view_.stepId = step.id;
    view_.token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    view_.title = prompt.title;
    view_.instruction = prompt.instruction;
    view_.safetyNotice = prompt.safetyNotice;
    view_.allowedActions = prompt.allowedActions;
    view_.consecutiveMatches = 0;
    view_.requiredConsecutiveMatches = 0;
    view_.currentRequestId.reset();
    stageStartedMonotonic_ = scheduler_.monotonicNow();
    const auto timeout = std::min(prompt.waitTimeout, caseRemaining());
    actionContext_ = {run_->runId.value, testCase->id, step.id, view_.token,
                      prompt.allowedActions, true, false};
    GuidedOperatorActionRecord record;
    record.runId = run_->runId;
    record.caseId = testCase->id;
    record.stepId = step.id;
    record.oneTimeToken = view_.token;
    record.promptShownUtc = scheduler_.utcNow();
    result->guidedRecovery->operatorActions.append(std::move(record));
    appendLog(QStringLiteral("prompt_shown"), QStringLiteral("显示人工操作提示"),
              {{QStringLiteral("step_id"), step.id},
               {QStringLiteral("token"), view_.token},
               {QStringLiteral("state"), guidedRunStateName(view_.state)}});
    scheduleStageDeadline(timeout);
    publish();
    emit viewChanged();
}

void GuidedTestCoordinator::beginObservation(qsizetype stepIndex)
{
    const auto *testCase = currentCase();
    auto *result = currentCaseResult();
    if (!testCase || !result || !result->guidedRecovery
        || stepIndex < 0 || stepIndex >= testCase->guidedRecovery.steps.size()
        || !testCase->guidedRecovery.steps[stepIndex].observationStep) {
        finishCurrentCase(GuidedTerminalReason::InternalError,
                          makeError(TestErrorCode::InvariantViolation,
                                    QStringLiteral("观察步骤不存在")));
        return;
    }
    invalidateStage();
    currentStepIndex_ = stepIndex;
    const auto &step = testCase->guidedRecovery.steps[stepIndex];
    const auto &observation = *step.observationStep;
    view_.state = observation.target == GuidedObservationTarget::ConsecutiveResponseTimeouts
        ? GuidedRunState::ObservingOutage : GuidedRunState::ObservingRecovery;
    view_.stepId = step.id;
    view_.token.clear();
    view_.title = observation.target == GuidedObservationTarget::ConsecutiveResponseTimeouts
        ? QStringLiteral("正在观察通信中断") : QStringLiteral("正在观察通信恢复");
    view_.instruction.clear();
    view_.safetyNotice.clear();
    view_.allowedActions.clear();
    view_.consecutiveMatches = 0;
    view_.requiredConsecutiveMatches = observation.consecutiveMatches;
    view_.currentRequestId.reset();
    actionContext_ = {};
    stageStartedMonotonic_ = scheduler_.monotonicNow();
    GuidedObservationResult observationResult;
    observationResult.runId = run_->runId;
    observationResult.caseId = testCase->id;
    observationResult.stepId = step.id;
    observationResult.target = observation.target;
    observationResult.startedUtc = scheduler_.utcNow();
    observationResult.requiredConsecutiveMatches = observation.consecutiveMatches;
    result->guidedRecovery->observations.append(std::move(observationResult));
    appendLog(QStringLiteral("observation_started"), QStringLiteral("开始引导自动观察"),
              {{QStringLiteral("step_id"), step.id},
               {QStringLiteral("target"), guidedObservationTargetName(observation.target)}});
    scheduleStageDeadline(std::min(observation.deadline, caseRemaining()));
    publish();
    emit viewChanged();
    submitProbe();
}

void GuidedTestCoordinator::submitProbe()
{
    const auto *testCase = currentCase();
    if (!testCase || currentStepIndex_ < 0
        || currentStepIndex_ >= testCase->guidedRecovery.steps.size()) return;
    const auto &step = testCase->guidedRecovery.steps[currentStepIndex_];
    if (!step.observationStep || currentProbe_) return;
    const auto remaining = std::min(remainingTo(stageDeadlineMonotonic_), caseRemaining());
    if (remaining <= std::chrono::milliseconds::zero()) {
        finishObservation(GuidedObservationOutcome::DeadlineExpired,
                          view_.state == GuidedRunState::ObservingOutage
                              ? GuidedTerminalReason::OutageNotDetected
                              : GuidedTerminalReason::RecoveryTimeout);
        return;
    }
    const auto timeout = std::max(std::chrono::milliseconds(1),
                                  std::min(testCase->timeout.request, remaining));
    const auto submission = engine_.submitGuidedProbe(
        run_->runId, testCase->id, step.id, step.observationStep->probe, timeout);
    if (!submission.accepted()) {
        finishObservation(GuidedObservationOutcome::FatalCommunicationError,
                          GuidedTerminalReason::FatalCommunicationError,
                          submission.rejection);
        return;
    }
    currentProbe_ = submission.requestId;
    view_.currentRequestId = submission.requestId;
    emit viewChanged();
}

void GuidedTestCoordinator::handleProbeCompleted(
    TestRunId runId, const QString &caseId, const QString &stepId,
    const communication::ModbusRequestResult &result)
{
    if (!run_ || coordinatorState_ != GuidedCoordinatorState::Running
        || runId.value != run_->runId.value || caseId != view_.caseId
        || stepId != view_.stepId || !currentProbe_
        || result.requestId != *currentProbe_) return;
    currentProbe_.reset();
    view_.currentRequestId.reset();
    auto *caseResult = currentCaseResult();
    const auto *testCase = currentCase();
    if (!caseResult || !caseResult->guidedRecovery || !testCase
        || caseResult->guidedRecovery->observations.isEmpty()) return;
    auto &observationResult = caseResult->guidedRecovery->observations.last();
    const auto &observation = *testCase->guidedRecovery.steps[currentStepIndex_].observationStep;

    TestRequestAttemptResult attempt;
    attempt.sequence = nextAttemptSequence_++;
    attempt.purpose = TestStepPurpose::Read;
    attempt.stepAttempt = 1;
    attempt.logicalStepId = stepId;
    attempt.logicalStepIndex = currentStepIndex_;
    attempt.requestId = result.requestId;
    attempt.startedUtc = result.evidence.enqueuedUtc;
    attempt.finishedUtc = result.evidence.completedUtc.isValid()
        ? result.evidence.completedUtc : scheduler_.utcNow();
    attempt.duration = std::chrono::milliseconds(std::max<qint64>(
        0, attempt.startedUtc.msecsTo(attempt.finishedUtc)));
    attempt.requestResult = result;
    observationResult.probes.append(attempt);
    caseResult->attempts.append(attempt);
    ++caseResult->evidenceRetention.totalAttempts;
    if (!result.success) ++caseResult->evidenceRetention.totalFailures;
    caseResult->evidenceRetention.retainedAttempts =
        caseResult->evidenceRetention.totalAttempts;
    caseResult->evidenceRetention.retainedFailures =
        caseResult->evidenceRetention.totalFailures;
    appendLog(QStringLiteral("request_attempt"), QStringLiteral("引导探测完成"),
              {{QStringLiteral("step_id"), stepId},
               {QStringLiteral("attempt_sequence"),
                static_cast<qulonglong>(attempt.sequence)}}, result.requestId);

    const auto classification = classifyGuidedProbe(
        observation.target, result, observation.businessAssertion);
    if (classification == GuidedProbeClassification::FatalCommunicationError) {
        finishObservation(GuidedObservationOutcome::FatalCommunicationError,
                          GuidedTerminalReason::FatalCommunicationError,
                          makeError(TestErrorCode::RequestFailed,
                                    QStringLiteral("引导探测发生致命通信错误"), result.error));
        return;
    }
    if (classification == GuidedProbeClassification::Cancelled) {
        finishObservation(GuidedObservationOutcome::Aborted,
                          GuidedTerminalReason::UserAborted,
                          makeError(TestErrorCode::Aborted,
                                    QStringLiteral("引导探测被取消"), result.error));
        return;
    }
    if (classification == GuidedProbeClassification::Match) {
        ++view_.consecutiveMatches;
        observationResult.achievedConsecutiveMatches = view_.consecutiveMatches;
        if (!observationResult.firstMatchingRequestId) {
            observationResult.firstMatchingRequestId = result.requestId;
        }
        if (view_.state == GuidedRunState::ObservingRecovery && !firstRecoveryMs_) {
            firstRecoveryMs_ = monotonicMilliseconds(scheduler_);
        }
        if (view_.consecutiveMatches >= observation.consecutiveMatches) {
            observationResult.stableMatchingRequestId = result.requestId;
            if (view_.state == GuidedRunState::ObservingRecovery) {
                stableRecoveryMs_ = monotonicMilliseconds(scheduler_);
            }
            finishObservation(GuidedObservationOutcome::Matched);
            return;
        }
    } else {
        view_.consecutiveMatches = 0;
        observationResult.achievedConsecutiveMatches = 0;
    }
    publish();
    emit viewChanged();
    scheduleNextProbe(observation.interval);
}

void GuidedTestCoordinator::handleAppCommandCompleted(
    const app::AppCommandResult &result)
{
    if (!releaseOperationId_ || !(result.operationId == *releaseOperationId_)
        || result.command != app::AppCommand::StopTesting) return;
    releaseOperationId_.reset();
    if (!result.succeeded || appState_.state() != app::AppState::ConnectedIdle) {
        run_->auxiliaryErrors.append(makeError(
            TestErrorCode::CleanupFailed, QStringLiteral("释放 Testing owner 失败"),
            result.error ? result.error->communicationError : std::nullopt));
    }
    finalizeRun();
}

void GuidedTestCoordinator::scheduleStageDeadline(std::chrono::milliseconds duration)
{
    if (deadlineTask_) scheduler_.cancel(*deadlineTask_);
    deadlineTask_.reset();
    duration = std::max(std::chrono::milliseconds(1), duration);
    stageDeadlineMonotonic_ = scheduler_.monotonicNow() + duration;
    const quint64 generation = stageGeneration_;
    const QPointer<GuidedTestCoordinator> self(this);
    deadlineTask_ = scheduler_.scheduleAfter(duration, this, [self, generation] {
        if (!self || self->stageGeneration_ != generation
            || self->coordinatorState_ != GuidedCoordinatorState::Running) return;
        self->deadlineTask_.reset();
        if (self->view_.state == GuidedRunState::WaitingForDisconnectConfirmation
            || self->view_.state == GuidedRunState::WaitingForReconnectConfirmation) {
            self->finishCurrentCase(
                GuidedTerminalReason::OperatorTimeout,
                makeError(TestErrorCode::CaseTimeout, QStringLiteral("等待操作员确认超时")));
        } else if (self->view_.state == GuidedRunState::ObservingOutage) {
            self->finishObservation(GuidedObservationOutcome::DeadlineExpired,
                                    GuidedTerminalReason::OutageNotDetected);
        } else if (self->view_.state == GuidedRunState::ObservingRecovery) {
            self->finishObservation(GuidedObservationOutcome::DeadlineExpired,
                                    GuidedTerminalReason::RecoveryTimeout);
        }
    });
}

void GuidedTestCoordinator::scheduleNextProbe(std::chrono::milliseconds interval)
{
    if (nextProbeTask_) scheduler_.cancel(*nextProbeTask_);
    nextProbeTask_.reset();
    const auto remaining = remainingTo(stageDeadlineMonotonic_);
    if (remaining <= interval) {
        return;
    }
    const quint64 generation = stageGeneration_;
    const QPointer<GuidedTestCoordinator> self(this);
    nextProbeTask_ = scheduler_.scheduleAfter(interval, this, [self, generation] {
        if (!self || self->stageGeneration_ != generation
            || self->coordinatorState_ != GuidedCoordinatorState::Running) return;
        self->nextProbeTask_.reset();
        self->submitProbe();
    });
}

void GuidedTestCoordinator::cancelScheduledTask() noexcept
{
    if (deadlineTask_) scheduler_.cancel(*deadlineTask_);
    if (nextProbeTask_) scheduler_.cancel(*nextProbeTask_);
    deadlineTask_.reset();
    nextProbeTask_.reset();
}

void GuidedTestCoordinator::invalidateStage() noexcept
{
    ++stageGeneration_;
    cancelScheduledTask();
    actionContext_.awaitingAction = false;
    if (currentProbe_) {
        const auto requestId = *currentProbe_;
        currentProbe_.reset();
        view_.currentRequestId.reset();
        (void)engine_.cancelGuidedProbe(requestId);
    }
    stageDeadlineMonotonic_ = std::chrono::nanoseconds::zero();
}

void GuidedTestCoordinator::finishObservation(
    GuidedObservationOutcome outcome, GuidedTerminalReason failureReason,
    std::optional<TestError> error)
{
    auto *caseResult = currentCaseResult();
    if (!caseResult || !caseResult->guidedRecovery
        || caseResult->guidedRecovery->observations.isEmpty()) {
        finishCurrentCase(GuidedTerminalReason::InternalError,
                          makeError(TestErrorCode::InvariantViolation,
                                    QStringLiteral("观察结果不存在")));
        return;
    }
    invalidateStage();
    auto &observation = caseResult->guidedRecovery->observations.last();
    observation.outcome = outcome;
    observation.finishedUtc = scheduler_.utcNow();
    observation.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::max(std::chrono::nanoseconds::zero(),
                 scheduler_.monotonicNow() - stageStartedMonotonic_));
    observation.error = error;
    appendLog(QStringLiteral("observation_finished"), QStringLiteral("引导自动观察结束"),
              {{QStringLiteral("step_id"), observation.stepId},
               {QStringLiteral("target"), guidedObservationTargetName(observation.target)},
               {QStringLiteral("outcome"), static_cast<int>(outcome)},
               {QStringLiteral("consecutive"), observation.achievedConsecutiveMatches}});
    if (outcome != GuidedObservationOutcome::Matched) {
        finishCurrentCase(failureReason == GuidedTerminalReason::None
                              ? GuidedTerminalReason::InternalError : failureReason,
                          std::move(error));
        return;
    }
    if (observation.target == GuidedObservationTarget::ConsecutiveResponseTimeouts) {
        showPrompt(2);
        return;
    }
    const auto timing = calculateRecoveryTiming(
        {reconnectConfirmedMs_.value_or(0), firstRecoveryMs_, stableRecoveryMs_});
    if (!timing.succeeded()) {
        finishCurrentCase(GuidedTerminalReason::InternalError,
                          makeError(TestErrorCode::InvariantViolation,
                                    QStringLiteral("恢复耗时计算失败")));
        return;
    }
    caseResult->guidedRecovery->recoveryTiming = timing.timing;
    view_.recoveryTiming = timing.timing;
    finishCurrentCase(GuidedTerminalReason::Pass);
}

void GuidedTestCoordinator::finishCurrentCase(
    GuidedTerminalReason reason, std::optional<TestError> error)
{
    if (!run_ || coordinatorState_ != GuidedCoordinatorState::Running) return;
    invalidateStage();
    auto *result = currentCaseResult();
    if (!result) {
        beginRelease();
        return;
    }
    if (!result->guidedRecovery) result->guidedRecovery = GuidedRecoveryCaseResult{};
    result->guidedRecovery->finalState = GuidedRunState::Finished;
    result->guidedRecovery->terminalReason = reason;
    result->guidedRecovery->physicalLinkRestored = reason == GuidedTerminalReason::Pass;
    result->guidedRecovery->recoveryInstructionRequired = reason != GuidedTerminalReason::Pass;
    result->guidedRecovery->recoveryInstruction =
        reason == GuidedTerminalReason::Pass ? QString{} : recoveryInstruction();
    result->status = statusFor(reason);
    result->error = std::move(error);
    result->finishedUtc = scheduler_.utcNow();
    result->duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::max(std::chrono::nanoseconds::zero(),
                 scheduler_.monotonicNow() - caseStartedMonotonic_));
    view_.state = GuidedRunState::Finished;
    view_.terminalReason = reason;
    view_.token.clear();
    view_.allowedActions.clear();
    view_.remaining = std::chrono::milliseconds(0);
    view_.currentRequestId.reset();
    view_.restorationReminder = result->guidedRecovery->recoveryInstruction;
    appendLog(QStringLiteral("guided_case_finished"), QStringLiteral("引导用例结束"),
              {{QStringLiteral("status"), testStatusName(result->status)},
               {QStringLiteral("reason"), guidedTerminalReasonName(reason)}});
    publish();
    emit viewChanged();
    if (reason == GuidedTerminalReason::Pass) {
        startNextCase();
        return;
    }
    for (qsizetype index = currentCaseIndex_ + 1; index < run_->cases.size(); ++index) {
        if (run_->cases[index].status == TestStatus::NotRun) {
            run_->cases[index].status = TestStatus::Skipped;
            run_->cases[index].skipReason = TestSkipReason::Aborted;
        }
    }
    beginRelease();
}

void GuidedTestCoordinator::beginRelease()
{
    if (!run_ || coordinatorState_ == GuidedCoordinatorState::Releasing) return;
    invalidateStage();
    coordinatorState_ = GuidedCoordinatorState::Releasing;
    emit viewChanged();
    const auto submission = appState_.stopTesting();
    if (!submission.accepted()) {
        run_->auxiliaryErrors.append(makeError(
            TestErrorCode::StopTestingRejected,
            QStringLiteral("AppStateController 拒绝释放 Testing owner"),
            submission.rejection ? submission.rejection->communicationError : std::nullopt));
        finalizeRun();
        return;
    }
    releaseOperationId_ = submission.operationId;
}

void GuidedTestCoordinator::finalizeRun()
{
    if (!run_) return;
    run_->status = TestStatus::Pass;
    bool anyPass = false;
    bool anySkipped = false;
    for (const auto &testCase : run_->cases) {
        anyPass = anyPass || testCase.status == TestStatus::Pass;
        anySkipped = anySkipped || testCase.status == TestStatus::Skipped;
        if (testCase.status == TestStatus::Error) {
            run_->status = TestStatus::Error;
            break;
        }
        if (testCase.status == TestStatus::Fail) run_->status = TestStatus::Fail;
    }
    for (const auto &error : run_->auxiliaryErrors) {
        if (error.code != TestErrorCode::LoggingFailed) run_->status = TestStatus::Error;
    }
    if (!anyPass && anySkipped && run_->status == TestStatus::Pass) {
        run_->status = TestStatus::Skipped;
    }
    run_->finishedUtc = scheduler_.utcNow();
    run_->duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::max(std::chrono::nanoseconds::zero(),
                 scheduler_.monotonicNow() - runStartedMonotonic_));
    publish(true);
    const auto completed = *run_;
    run_.reset();
    selected_.clear();
    currentCaseIndex_ = -1;
    currentStepIndex_ = -1;
    releaseOperationId_.reset();
    coordinatorState_ = GuidedCoordinatorState::Idle;
    emit viewChanged();
    emit runCompleted(completed);
}

void GuidedTestCoordinator::publish(bool terminal)
{
    if (run_) results_.publish(*run_, terminal);
}

void GuidedTestCoordinator::appendLog(
    QString event, QString message, const QVariantMap &metadata,
    std::optional<communication::RequestId> requestId)
{
    if (!sessionLog_ || !run_) return;
    logging::LogEntry entry;
    entry.timestamp = scheduler_.utcNow();
    entry.level = logging::LogLevel::Test;
    entry.module = QStringLiteral("testing");
    entry.event = std::move(event);
    entry.message = std::move(message);
    entry.requestId = requestId ? std::optional<quint64>(requestId->value) : std::nullopt;
    entry.metadata = metadata;
    entry.metadata.insert(QStringLiteral("run_id"),
                          static_cast<qulonglong>(run_->runId.value));
    entry.metadata.insert(QStringLiteral("suite_id"), run_->suite->id);
    if (const auto *testCase = currentCase()) {
        entry.metadata.insert(QStringLiteral("case_id"), testCase->id);
    }
    sessionLog_->append(std::move(entry));
    if (!loggingErrorRecorded_ && sessionLog_->lastError()) {
        loggingErrorRecorded_ = true;
        run_->auxiliaryErrors.append(makeError(TestErrorCode::LoggingFailed,
                                                sessionLog_->lastError()->diagnostic));
    }
}

TestCaseResult *GuidedTestCoordinator::currentCaseResult()
{
    return run_ && currentCaseIndex_ >= 0 && currentCaseIndex_ < run_->cases.size()
        ? &run_->cases[currentCaseIndex_] : nullptr;
}

const TestCase *GuidedTestCoordinator::currentCase() const
{
    return run_ && run_->suite && currentCaseIndex_ >= 0
        && currentCaseIndex_ < run_->suite->cases.size()
        ? &run_->suite->cases[currentCaseIndex_] : nullptr;
}

std::chrono::milliseconds GuidedTestCoordinator::remainingTo(
    std::chrono::nanoseconds deadline) const noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::max(std::chrono::nanoseconds::zero(), deadline - scheduler_.monotonicNow()));
}

std::chrono::milliseconds GuidedTestCoordinator::caseRemaining() const noexcept
{
    const auto *testCase = currentCase();
    if (!testCase) return std::chrono::milliseconds(0);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::max(std::chrono::nanoseconds::zero(),
                 scheduler_.monotonicNow() - caseStartedMonotonic_));
    return std::max(std::chrono::milliseconds::zero(),
                    testCase->timeout.testCase - elapsed);
}

QString GuidedTestCoordinator::recoveryInstruction() const
{
    const auto *testCase = currentCase();
    if (!testCase) return QStringLiteral("请检查并恢复 RS485 A/B 接线后再继续操作。");
    if (currentStepIndex_ >= 0
        && currentStepIndex_ < testCase->guidedRecovery.steps.size()) {
        const auto &current = testCase->guidedRecovery.steps[currentStepIndex_];
        if (current.operatorStep
            && !current.operatorStep->cancelRecoveryInstruction.isEmpty()) {
            return current.operatorStep->cancelRecoveryInstruction;
        }
    }
    for (auto iterator = testCase->guidedRecovery.steps.crbegin();
         iterator != testCase->guidedRecovery.steps.crend(); ++iterator) {
        if (iterator->operatorStep
            && !iterator->operatorStep->cancelRecoveryInstruction.isEmpty()) {
            return iterator->operatorStep->cancelRecoveryInstruction;
        }
    }
    return QStringLiteral("请检查并恢复 RS485 A/B 接线后再继续操作。");
}

QString guidedCoordinatorStateName(GuidedCoordinatorState state)
{
    switch (state) {
    case GuidedCoordinatorState::Idle: return QStringLiteral("IDLE");
    case GuidedCoordinatorState::Running: return QStringLiteral("RUNNING");
    case GuidedCoordinatorState::Releasing: return QStringLiteral("RELEASING");
    }
    return QStringLiteral("UNKNOWN");
}

} // namespace oms555tv::testing

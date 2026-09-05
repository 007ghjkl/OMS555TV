#include "testing/GuidedTestModel.h"
#include "testing/TestAssertions.h"

#include <limits>

namespace oms555tv::testing {

namespace {

bool isCancellation(const communication::ErrorCode code) noexcept
{
    return code == communication::ErrorCode::CancelledByCaller
        || code == communication::ErrorCode::CancelledByHandoff
        || code == communication::ErrorCode::CancelledByClose;
}

} // namespace

GuidedActionRejection validateGuidedAction(const GuidedActionContext &context,
                                           const GuidedActionCommand &command) noexcept
{
    if (!context.awaitingAction) return GuidedActionRejection::NotAwaitingAction;
    if (context.tokenConsumed) return GuidedActionRejection::TokenAlreadyConsumed;
    if (command.runId != context.runId) return GuidedActionRejection::RunMismatch;
    if (command.caseId != context.caseId) return GuidedActionRejection::CaseMismatch;
    if (command.stepId != context.stepId) return GuidedActionRejection::StepMismatch;
    if (command.oneTimeToken != context.oneTimeToken || command.oneTimeToken.isEmpty()) {
        return GuidedActionRejection::TokenMismatch;
    }
    if (!context.allowedActions.contains(command.action)) {
        return GuidedActionRejection::ActionNotAllowed;
    }
    return GuidedActionRejection::None;
}

RecoveryTimingResult calculateRecoveryTiming(const RecoveryTimingInput &input) noexcept
{
    if (!input.firstSuccessfulResponseMonotonicMs) {
        return {{}, RecoveryTimingError::MissingFirstSuccess};
    }
    if (!input.stableRecoveryMonotonicMs) {
        return {{}, RecoveryTimingError::MissingStableSuccess};
    }
    const quint64 first = *input.firstSuccessfulResponseMonotonicMs;
    const quint64 stable = *input.stableRecoveryMonotonicMs;
    if (first < input.reconnectConfirmedMonotonicMs) {
        return {{}, RecoveryTimingError::FirstSuccessBeforeConfirmation};
    }
    if (stable < first) {
        return {{}, RecoveryTimingError::StableSuccessBeforeFirstSuccess};
    }
    const quint64 firstDuration = first - input.reconnectConfirmedMonotonicMs;
    const quint64 stableDuration = stable - input.reconnectConfirmedMonotonicMs;
    const auto maximum = static_cast<quint64>(std::numeric_limits<qint64>::max());
    if (firstDuration > maximum || stableDuration > maximum) {
        return {{}, RecoveryTimingError::DurationOverflow};
    }
    return {RecoveryTiming{std::chrono::milliseconds(static_cast<qint64>(firstDuration)),
                           std::chrono::milliseconds(static_cast<qint64>(stableDuration))},
            RecoveryTimingError::None};
}

int guidedTerminalPriority(const GuidedTerminalReason reason) noexcept
{
    switch (reason) {
    case GuidedTerminalReason::None: return 0;
    case GuidedTerminalReason::Pass: return 1;
    case GuidedTerminalReason::RecoveryTimeout: return 2;
    case GuidedTerminalReason::OutageNotDetected: return 3;
    case GuidedTerminalReason::OperatorTimeout: return 4;
    case GuidedTerminalReason::FatalCommunicationError: return 5;
    case GuidedTerminalReason::OperatorCancelled: return 6;
    case GuidedTerminalReason::UserAborted: return 7;
    case GuidedTerminalReason::InternalError: return 8;
    }
    return 8;
}

GuidedProbeClassification classifyGuidedProbe(
    const GuidedObservationTarget target,
    const communication::ModbusRequestResult &result,
    const std::optional<ExpectedAssertion> &businessAssertion)
{
    if (result.error) {
        if (isCancellation(result.error->code)) return GuidedProbeClassification::Cancelled;
        if (result.error->code == communication::ErrorCode::ResponseTimeout) {
            return target == GuidedObservationTarget::ConsecutiveResponseTimeouts
                ? GuidedProbeClassification::Match : GuidedProbeClassification::NonMatch;
        }
        return GuidedProbeClassification::FatalCommunicationError;
    }

    if (target == GuidedObservationTarget::ConsecutiveResponseTimeouts) {
        return GuidedProbeClassification::NonMatch;
    }
    if (result.state != communication::RequestState::Succeeded || !result.success
        || !std::holds_alternative<communication::ReadHoldingRegistersResult>(*result.success)) {
        return GuidedProbeClassification::FatalCommunicationError;
    }
    if (!businessAssertion) return GuidedProbeClassification::Match;
    const auto &read = std::get<communication::ReadHoldingRegistersResult>(*result.success);
    return evaluateAssertion(*businessAssertion, ActualResult::registers(read.values)).passed()
        ? GuidedProbeClassification::Match : GuidedProbeClassification::NonMatch;
}

GuidedTerminalReason selectGuidedTerminalReason(
    const QVector<GuidedTerminalReason> &candidates) noexcept
{
    GuidedTerminalReason selected = GuidedTerminalReason::None;
    for (const auto candidate : candidates) {
        if (guidedTerminalPriority(candidate) > guidedTerminalPriority(selected)) {
            selected = candidate;
        }
    }
    return selected;
}

QString guidedRunStateName(const GuidedRunState state)
{
    switch (state) {
    case GuidedRunState::Idle: return QStringLiteral("IDLE");
    case GuidedRunState::WaitingForDisconnectConfirmation:
        return QStringLiteral("WAITING_FOR_DISCONNECT_CONFIRMATION");
    case GuidedRunState::ObservingOutage: return QStringLiteral("OBSERVING_OUTAGE");
    case GuidedRunState::WaitingForReconnectConfirmation:
        return QStringLiteral("WAITING_FOR_RECONNECT_CONFIRMATION");
    case GuidedRunState::ObservingRecovery: return QStringLiteral("OBSERVING_RECOVERY");
    case GuidedRunState::Finished: return QStringLiteral("FINISHED");
    }
    return {};
}

QString guidedTerminalReasonName(const GuidedTerminalReason reason)
{
    switch (reason) {
    case GuidedTerminalReason::None: return QStringLiteral("none");
    case GuidedTerminalReason::Pass: return QStringLiteral("pass");
    case GuidedTerminalReason::OutageNotDetected: return QStringLiteral("outage_not_detected");
    case GuidedTerminalReason::RecoveryTimeout: return QStringLiteral("recovery_timeout");
    case GuidedTerminalReason::OperatorTimeout: return QStringLiteral("operator_timeout");
    case GuidedTerminalReason::FatalCommunicationError:
        return QStringLiteral("fatal_communication_error");
    case GuidedTerminalReason::OperatorCancelled: return QStringLiteral("operator_cancelled");
    case GuidedTerminalReason::UserAborted: return QStringLiteral("user_aborted");
    case GuidedTerminalReason::InternalError: return QStringLiteral("internal_error");
    }
    return {};
}

QString guidedActionRejectionName(const GuidedActionRejection rejection)
{
    switch (rejection) {
    case GuidedActionRejection::None: return QStringLiteral("none");
    case GuidedActionRejection::NotAwaitingAction: return QStringLiteral("not_awaiting_action");
    case GuidedActionRejection::RunMismatch: return QStringLiteral("run_mismatch");
    case GuidedActionRejection::CaseMismatch: return QStringLiteral("case_mismatch");
    case GuidedActionRejection::StepMismatch: return QStringLiteral("step_mismatch");
    case GuidedActionRejection::TokenMismatch: return QStringLiteral("token_mismatch");
    case GuidedActionRejection::TokenAlreadyConsumed:
        return QStringLiteral("token_already_consumed");
    case GuidedActionRejection::ActionNotAllowed: return QStringLiteral("action_not_allowed");
    }
    return {};
}

} // namespace oms555tv::testing

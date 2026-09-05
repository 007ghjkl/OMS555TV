#pragma once

#include "testing/TestCaseTypes.h"
#include "communication/CommunicationTypes.h"

#include <QString>
#include <QVector>

#include <chrono>
#include <optional>

namespace oms555tv::testing {

enum class GuidedRunState {
    Idle,
    WaitingForDisconnectConfirmation,
    ObservingOutage,
    WaitingForReconnectConfirmation,
    ObservingRecovery,
    Finished,
};

enum class GuidedTerminalReason {
    None,
    Pass,
    OutageNotDetected,
    RecoveryTimeout,
    OperatorTimeout,
    FatalCommunicationError,
    OperatorCancelled,
    UserAborted,
    InternalError,
};

enum class GuidedActionRejection {
    None,
    NotAwaitingAction,
    RunMismatch,
    CaseMismatch,
    StepMismatch,
    TokenMismatch,
    TokenAlreadyConsumed,
    ActionNotAllowed,
};

enum class RecoveryTimingError {
    None,
    MissingFirstSuccess,
    MissingStableSuccess,
    FirstSuccessBeforeConfirmation,
    StableSuccessBeforeFirstSuccess,
    DurationOverflow,
};

enum class GuidedProbeClassification {
    Match,
    NonMatch,
    Cancelled,
    FatalCommunicationError,
};

struct GuidedActionContext {
    quint64 runId = 0;
    QString caseId;
    QString stepId;
    QString oneTimeToken;
    QVector<GuidedOperatorAction> allowedActions;
    bool awaitingAction = false;
    bool tokenConsumed = false;
};

struct GuidedActionCommand {
    quint64 runId = 0;
    QString caseId;
    QString stepId;
    QString oneTimeToken;
    GuidedOperatorAction action = GuidedOperatorAction::Confirm;
    QString note;
};

struct RecoveryTimingInput {
    quint64 reconnectConfirmedMonotonicMs = 0;
    std::optional<quint64> firstSuccessfulResponseMonotonicMs;
    std::optional<quint64> stableRecoveryMonotonicMs;
};

struct RecoveryTiming {
    std::chrono::milliseconds toFirstSuccessfulResponse{0};
    std::chrono::milliseconds toStableRecovery{0};
};

struct RecoveryTimingResult {
    std::optional<RecoveryTiming> timing;
    RecoveryTimingError error = RecoveryTimingError::None;
    [[nodiscard]] bool succeeded() const noexcept { return timing.has_value(); }
};

[[nodiscard]] GuidedActionRejection validateGuidedAction(
    const GuidedActionContext &context, const GuidedActionCommand &command) noexcept;
[[nodiscard]] RecoveryTimingResult calculateRecoveryTiming(
    const RecoveryTimingInput &input) noexcept;
[[nodiscard]] GuidedTerminalReason selectGuidedTerminalReason(
    const QVector<GuidedTerminalReason> &candidates) noexcept;
[[nodiscard]] int guidedTerminalPriority(GuidedTerminalReason reason) noexcept;
[[nodiscard]] GuidedProbeClassification classifyGuidedProbe(
    GuidedObservationTarget target,
    const communication::ModbusRequestResult &result,
    const std::optional<ExpectedAssertion> &businessAssertion = std::nullopt);

[[nodiscard]] QString guidedRunStateName(GuidedRunState state);
[[nodiscard]] QString guidedTerminalReasonName(GuidedTerminalReason reason);
[[nodiscard]] QString guidedActionRejectionName(GuidedActionRejection rejection);

} // namespace oms555tv::testing

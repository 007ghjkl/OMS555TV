#pragma once

#include "communication/CommunicationTypes.h"
#include "testing/TestAssertions.h"
#include "testing/GuidedTestModel.h"

#include <QDateTime>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QVector>

#include <chrono>
#include <memory>
#include <optional>

namespace oms555tv::testing {

struct TestRunId { quint64 value = 0; };

enum class TestEngineState { Idle, Running, Aborting, Releasing };
enum class TestStepKind { Read, Write };
enum class TestStepPurpose {
    Read,
    Write,
    ReadBeforeWrite,
    VerifyReadback,
    RestoreOriginal,
    SequenceStep,
    ConsistencySample,
    StabilityIteration,
};
enum class TestSkipReason { Disabled, NotSelected, UserSelected, Aborted };
enum class TestEvidenceRetentionPolicy { Complete, BoundedRepresentative };

enum class TestErrorCode {
    InvalidState,
    AlreadyRunning,
    UnknownCaseId,
    HandlerNotFound,
    RequestRejected,
    RequestFailed,
    CaseTimeout,
    Aborted,
    CleanupFailed,
    StopTestingRejected,
    StateMismatch,
    LoggingFailed,
    InvariantViolation,
};

struct TestError {
    TestErrorCode code = TestErrorCode::InvariantViolation;
    QString diagnostic;
    std::optional<communication::CommunicationError> communicationError;
};

struct TestRunSubmission {
    std::optional<TestRunId> runId;
    std::optional<TestError> rejection;
    [[nodiscard]] bool accepted() const noexcept { return runId.has_value(); }
};

struct GuidedProbeSubmission {
    std::optional<communication::RequestId> requestId;
    std::optional<TestError> rejection;
    [[nodiscard]] bool accepted() const noexcept { return requestId.has_value(); }
};

struct TestRequestAttemptResult {
    quint64 sequence = 0;
    TestStepPurpose purpose = TestStepPurpose::Read;
    int stepAttempt = 1;
    bool retry = false;
    QString retryReason;
    QString logicalStepId;
    qsizetype logicalStepIndex = -1;
    int repetition = 0;
    communication::RequestId requestId;
    QDateTime startedUtc;
    QDateTime finishedUtc;
    std::chrono::milliseconds duration{0};
    communication::ModbusRequestResult requestResult;
};

struct TestCompositeStepResult {
    QString stepId;
    qsizetype stepIndex = -1;
    int repetition = 0;
    TestStatus status = TestStatus::NotRun;
    ExpectedAssertion expected;
    std::optional<ActualResult> actual;
    std::optional<AssertionResult> assertion;
    std::optional<TestError> error;
    QDateTime startedUtc;
    QDateTime finishedUtc;
    std::chrono::milliseconds duration{0};
    QVector<quint64> attemptSequences;
};

struct TestEvidenceRetentionSummary {
    TestEvidenceRetentionPolicy policy = TestEvidenceRetentionPolicy::Complete;
    quint64 totalAttempts = 0;
    quint64 retainedAttempts = 0;
    quint64 droppedAttempts = 0;
    quint64 totalFailures = 0;
    quint64 retainedFailures = 0;
    int configuredLimit = 0;
    QString description;
};

struct GuidedOperatorActionRecord {
    TestRunId runId;
    QString caseId;
    QString stepId;
    QString oneTimeToken;
    QDateTime promptShownUtc;
    std::optional<GuidedOperatorAction> action;
    std::optional<QDateTime> actionUtc;
    std::chrono::milliseconds waitDuration{0};
    QString note;
};

enum class GuidedObservationOutcome {
    Pending,
    Matched,
    DeadlineExpired,
    FatalCommunicationError,
    Aborted,
};

struct GuidedObservationResult {
    TestRunId runId;
    QString caseId;
    QString stepId;
    GuidedObservationTarget target = GuidedObservationTarget::ConsecutiveResponseTimeouts;
    GuidedObservationOutcome outcome = GuidedObservationOutcome::Pending;
    QDateTime startedUtc;
    QDateTime finishedUtc;
    std::chrono::milliseconds duration{0};
    int requiredConsecutiveMatches = 1;
    int achievedConsecutiveMatches = 0;
    std::optional<communication::RequestId> firstMatchingRequestId;
    std::optional<communication::RequestId> stableMatchingRequestId;
    QVector<TestRequestAttemptResult> probes;
    std::optional<TestError> error;
};

struct GuidedRecoveryCaseResult {
    GuidedRunState finalState = GuidedRunState::Idle;
    GuidedTerminalReason terminalReason = GuidedTerminalReason::None;
    QVector<GuidedOperatorActionRecord> operatorActions;
    QVector<GuidedObservationResult> observations;
    std::optional<RecoveryTiming> recoveryTiming;
    bool physicalLinkRestored = false;
    bool recoveryInstructionRequired = false;
    QString recoveryInstruction;
};

struct TestCaseResult {
    QString caseId;
    TestCaseType declaredType = TestCaseType::ReadRegisters;
    TestCaseType type = TestCaseType::ReadRegisters;
    TestStatus status = TestStatus::NotRun;
    std::optional<TestSkipReason> skipReason;
    ExpectedAssertion expected;
    std::optional<ActualResult> actual;
    std::optional<AssertionResult> assertion;
    std::optional<TestError> error;
    std::optional<TestError> cleanupError;
    QDateTime startedUtc;
    QDateTime finishedUtc;
    std::chrono::milliseconds duration{0};
    QVector<TestRequestAttemptResult> attempts;
    QVector<TestCompositeStepResult> steps;
    std::optional<StabilityStatistics> stability;
    std::optional<GuidedRecoveryCaseResult> guidedRecovery;
    TestEvidenceRetentionSummary evidenceRetention;
    QString sessionId;
};

struct TestSuiteResult {
    TestRunId runId;
    std::shared_ptr<const TestSuite> suite;
    TestStatus status = TestStatus::NotRun;
    bool aborted = false;
    QDateTime startedUtc;
    QDateTime finishedUtc;
    std::chrono::milliseconds duration{0};
    QVector<TestCaseResult> cases;
    QVector<TestError> auxiliaryErrors;
    QString sessionId;
};

[[nodiscard]] QString testEngineStateName(TestEngineState state);
[[nodiscard]] QString testStepPurposeName(TestStepPurpose purpose);
[[nodiscard]] QString testSkipReasonName(TestSkipReason reason);
[[nodiscard]] QString testErrorCodeName(TestErrorCode code);
[[nodiscard]] QString testEvidenceRetentionPolicyName(TestEvidenceRetentionPolicy policy);

} // namespace oms555tv::testing

Q_DECLARE_METATYPE(oms555tv::testing::TestRunId)
Q_DECLARE_METATYPE(oms555tv::testing::TestEngineState)
Q_DECLARE_METATYPE(oms555tv::testing::TestSuiteResult)

#pragma once

#include "communication/CommunicationTypes.h"
#include "testing/TestAssertions.h"

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
enum class TestStepPurpose { Read, Write, ReadBeforeWrite, VerifyReadback, RestoreOriginal };
enum class TestSkipReason { Disabled, NotSelected, UserSelected, Aborted };

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

struct TestRequestAttemptResult {
    quint64 sequence = 0;
    TestStepPurpose purpose = TestStepPurpose::Read;
    int stepAttempt = 1;
    bool retry = false;
    QString retryReason;
    communication::RequestId requestId;
    QDateTime startedUtc;
    QDateTime finishedUtc;
    std::chrono::milliseconds duration{0};
    communication::ModbusRequestResult requestResult;
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
};

[[nodiscard]] QString testEngineStateName(TestEngineState state);
[[nodiscard]] QString testStepPurposeName(TestStepPurpose purpose);
[[nodiscard]] QString testSkipReasonName(TestSkipReason reason);
[[nodiscard]] QString testErrorCodeName(TestErrorCode code);

} // namespace oms555tv::testing

Q_DECLARE_METATYPE(oms555tv::testing::TestRunId)
Q_DECLARE_METATYPE(oms555tv::testing::TestEngineState)
Q_DECLARE_METATYPE(oms555tv::testing::TestSuiteResult)

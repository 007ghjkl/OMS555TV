#pragma once

#include "device/RegisterMap.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <chrono>
#include <limits>
#include <optional>

namespace oms555tv::testing {

inline constexpr int testSuiteSchemaVersion = 1;
inline constexpr int testSuiteSchemaVersionV2 = 2;
inline constexpr int defaultRequestTimeoutMs = 500;
inline constexpr int maximumRequestTimeoutMs = 60000;
inline constexpr int maximumCaseTimeoutMs = 300000;
inline constexpr int maximumRetryCount = 3;
inline constexpr int maximumReadRegisterCount = 125;
inline constexpr int maximumSequenceSteps = 64;
inline constexpr int maximumSequenceRepeatCount = 100;
inline constexpr int maximumStepDelayMs = 60000;
inline constexpr int maximumCompositeCaseTimeoutMs = 3600000;
inline constexpr int minimumStabilityDurationMs = 600000;
inline constexpr int maximumStabilityDurationMs = 86400000;
inline constexpr int minimumStabilityIntervalMs = 10;
inline constexpr int maximumStabilityIntervalMs = 60000;
inline constexpr int maximumStabilityCaseTimeoutMs = 86460000;
inline constexpr int maximumConsistencySamples = 16;
inline constexpr int defaultEvidenceSampleLimit = 64;
inline constexpr int minimumEvidenceSampleLimit = 8;
inline constexpr int maximumEvidenceSampleLimit = 256;
inline constexpr int failureRateScalePpm = 1000000;

enum class TestStatus { NotRun, Running, Pass, Fail, Skipped, Error };

enum class TestCaseType {
    ReadRegister,
    ReadRegisters,
    WriteRegister,
    WriteAndVerify,
    ExpectException,
    Sequence,
    ExpectTimeout,
    Consistency,
    Stability,
};

enum class ModbusFunction : quint8 {
    ReadHoldingRegisters = 0x03,
    WriteSingleRegister = 0x06,
};

enum class AssertionType {
    Equals,
    Range,
    RegisterSequence,
    BitMask,
    ModbusException,
    Elements,
    ResponseTimeout,
    UInt32,
    StabilitySummary,
};

enum class ValueRepresentation { UInt16, Int16 };

enum class ExecutionEnvironment { Both, RealRs485, Fake };

enum class SequenceFailurePolicy { StopOnFailure, ContinueOnFailure };

enum class FaultInjection { None, NoResponse };

enum class UInt32Comparison { Range, NonDecreasing, StrictlyIncreasing };

enum class UInt32WordOrder { LowWordFirst };

enum class RetryError { Timeout, Connection, Serial, Crc, Protocol };

struct TimeoutPolicy {
    std::chrono::milliseconds request{defaultRequestTimeoutMs};
    std::chrono::milliseconds testCase{defaultRequestTimeoutMs};
};

struct RetryPolicy {
    int maxRetries = 0;
    QVector<RetryError> onErrors;
};

struct TestRequest {
    ModbusFunction function = ModbusFunction::ReadHoldingRegisters;
    device::PduAddress address{0};
    quint16 count = 1;
    quint16 rawValue = 0;
    bool readBeforeWrite = false;
    bool restoreOriginal = false;
};

struct ElementAssertion {
    qsizetype index = 0;
    AssertionType type = AssertionType::Equals;
    ValueRepresentation representation = ValueRepresentation::UInt16;
    qint64 value = 0;
    qint64 minimum = 0;
    qint64 maximum = 0;
    quint16 mask = 0;
    int decimalPlaces = 0;
    QString unit;
};

struct UInt32Assertion {
    UInt32WordOrder wordOrder = UInt32WordOrder::LowWordFirst;
    UInt32Comparison comparison = UInt32Comparison::Range;
    quint64 minimum = 0;
    quint64 maximum = std::numeric_limits<quint32>::max();
    int decimalPlaces = 0;
    QString unit;
};

struct StabilityExpectation {
    quint64 minimumTotal = 0;
    quint64 minimumSuccesses = 0;
    quint64 maximumFailures = 0;
    quint64 maximumTimeouts = 0;
    quint32 maximumFailureRatePpm = 0;
    bool requireValidRttSamples = true;
    qint64 minimumRttMs = 0;
    qint64 maximumAverageRttMs = maximumRequestTimeoutMs;
    qint64 maximumRttMs = maximumRequestTimeoutMs;
};

struct ExpectedAssertion {
    AssertionType type = AssertionType::Equals;
    ValueRepresentation representation = ValueRepresentation::UInt16;
    qint64 value = 0;
    qint64 minimum = 0;
    qint64 maximum = 0;
    QVector<qint64> values;
    quint16 mask = 0;
    quint8 exceptionCode = 0;
    QString unit;
    QVector<ElementAssertion> elements;
    UInt32Assertion uint32;
    StabilityExpectation stability;
};

struct SequenceStep {
    QString id;
    TestCaseType declaredType = TestCaseType::ReadRegisters;
    TestCaseType type = TestCaseType::ReadRegisters;
    std::chrono::milliseconds delayBefore{0};
    TestRequest request;
    ExpectedAssertion expected;
    RetryPolicy retry;
    FaultInjection fault = FaultInjection::None;
};

struct SequencePolicy {
    int repeatCount = 1;
    SequenceFailurePolicy failurePolicy = SequenceFailurePolicy::StopOnFailure;
    QVector<SequenceStep> steps;
};

struct ConsistencyPolicy {
    int sampleCount = 1;
    std::chrono::milliseconds interval{0};
};

struct StabilityPolicy {
    std::chrono::milliseconds duration{minimumStabilityDurationMs};
    std::chrono::milliseconds interval{1000};
    quint64 minimumSuccesses = 1;
    quint32 allowedFailureRatePpm = 0;
    int evidenceSampleLimit = defaultEvidenceSampleLimit;
};

struct TestCase {
    QString id;
    QString name;
    QString category;
    QString description;
    ExecutionEnvironment environment = ExecutionEnvironment::Both;
    TestCaseType declaredType = TestCaseType::ReadRegisters;
    // read_register 成功加载后也规范化为 ReadRegisters。
    TestCaseType type = TestCaseType::ReadRegisters;
    bool enabled = true;
    QStringList tags;
    TestRequest request;
    ExpectedAssertion expected;
    TimeoutPolicy timeout;
    RetryPolicy retry;
    FaultInjection fault = FaultInjection::None;
    SequencePolicy sequence;
    ConsistencyPolicy consistency;
    StabilityPolicy stability;
};

struct TestSuite {
    int schemaVersion = testSuiteSchemaVersion;
    QString id;
    QString name;
    QString description;
    QStringList tags;
    QMap<QString, QString> metadata;
    QVector<TestCase> cases;
};

enum class ConfigErrorCode {
    InvalidUtf8,
    JsonSyntax,
    RootNotObject,
    UnknownSchemaVersion,
    MissingField,
    UnknownField,
    WrongType,
    EmptyString,
    InvalidIdentifier,
    OutOfRange,
    UnknownCaseType,
    UnknownAssertionType,
    DuplicateId,
    DuplicateTag,
    AddressRangeOverflow,
    InvalidCombination,
    DuplicateIndex,
    BudgetOverflow,
    StatisticsConflict,
};

struct ConfigError {
    ConfigErrorCode code = ConfigErrorCode::JsonSyntax;
    QString path;
    QString suiteId;
    QString caseId;
    QString diagnostic;
};

struct LoadResult {
    std::optional<TestSuite> suite;
    QVector<ConfigError> errors;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return suite.has_value() && errors.isEmpty();
    }
};

[[nodiscard]] QString testStatusName(TestStatus status);
[[nodiscard]] QString testCaseTypeName(TestCaseType type);
[[nodiscard]] QString assertionTypeName(AssertionType type);
[[nodiscard]] QString configErrorCodeName(ConfigErrorCode code);
[[nodiscard]] QString executionEnvironmentName(ExecutionEnvironment environment);

} // namespace oms555tv::testing

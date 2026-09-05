#pragma once

#include "testing/TestCaseTypes.h"

#include <optional>

namespace oms555tv::testing {

enum class ActualResultType {
    RegisterValues,
    RegisterSamples,
    ModbusException,
    ResponseTimeout,
    CommunicationError,
    StabilitySummary,
};

struct StabilityStatistics {
    quint64 total = 0;
    quint64 successes = 0;
    quint64 failures = 0;
    quint64 timeouts = 0;
    quint64 validRttSamples = 0;
    std::optional<qint64> minimumRttMs;
    std::optional<qint64> averageRttMs;
    std::optional<qint64> maximumRttMs;
};

struct ScaledInteger {
    qint64 numerator = 0;
    qint64 denominator = 1;
};

struct ActualResult {
    ActualResultType type = ActualResultType::RegisterValues;
    QVector<quint16> registerValues;
    QVector<QVector<quint16>> registerSamples;
    std::optional<quint8> exceptionCode;
    std::optional<StabilityStatistics> stability;

    [[nodiscard]] static ActualResult registers(QVector<quint16> values);
    [[nodiscard]] static ActualResult samples(QVector<QVector<quint16>> values);
    [[nodiscard]] static ActualResult exception(quint8 code);
    [[nodiscard]] static ActualResult responseTimeout();
    [[nodiscard]] static ActualResult communicationError();
    [[nodiscard]] static ActualResult stabilitySummary(StabilityStatistics value);
};

enum class AssertionDifferenceCode {
    ActualTypeMismatch,
    ValueMismatch,
    OutOfRange,
    SequenceLengthMismatch,
    SequenceValueMismatch,
    BitMaskMismatch,
    ExceptionCodeMismatch,
    ElementCountMismatch,
    UInt32WordCountMismatch,
    UInt32OutOfRange,
    UInt32MonotonicityMismatch,
    StabilityInvariantMismatch,
    StabilityCountMismatch,
    FailureRateExceeded,
    RttMissing,
    RttOutOfRange,
};

struct AssertionDifference {
    AssertionDifferenceCode code = AssertionDifferenceCode::ValueMismatch;
    std::optional<qsizetype> index;
    std::optional<qint64> expected;
    std::optional<qint64> expectedMinimum;
    std::optional<qint64> expectedMaximum;
    std::optional<qint64> actual;
    std::optional<quint16> actualRaw;
    std::optional<qint64> scaledDenominator;
    QString unit;
    QString reason;
};

struct AssertionResult {
    TestStatus status = TestStatus::Fail;
    AssertionType type = AssertionType::Equals;
    QString expectedSummary;
    QString actualSummary;
    QVector<qint64> expectedValues;
    QVector<ElementAssertion> expectedElements;
    std::optional<UInt32Assertion> expectedUInt32;
    std::optional<StabilityExpectation> expectedStability;
    QVector<qint64> actualValues;
    QVector<quint16> actualRawValues;
    QVector<QVector<quint16>> actualRawSamples;
    QVector<ScaledInteger> actualScaledValues;
    std::optional<quint8> expectedExceptionCode;
    std::optional<quint8> actualExceptionCode;
    QString unit;
    QVector<AssertionDifference> differences;
    std::optional<StabilityStatistics> stability;

    [[nodiscard]] bool passed() const noexcept { return status == TestStatus::Pass; }
};

[[nodiscard]] AssertionResult evaluateAssertion(const ExpectedAssertion &expected,
                                                const ActualResult &actual);

} // namespace oms555tv::testing

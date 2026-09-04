#pragma once

#include "testing/TestCaseTypes.h"

#include <optional>

namespace oms555tv::testing {

enum class ActualResultType { RegisterValues, ModbusException };

struct ActualResult {
    ActualResultType type = ActualResultType::RegisterValues;
    QVector<quint16> registerValues;
    std::optional<quint8> exceptionCode;

    [[nodiscard]] static ActualResult registers(QVector<quint16> values);
    [[nodiscard]] static ActualResult exception(quint8 code);
};

enum class AssertionDifferenceCode {
    ActualTypeMismatch,
    ValueMismatch,
    OutOfRange,
    SequenceLengthMismatch,
    SequenceValueMismatch,
    BitMaskMismatch,
    ExceptionCodeMismatch,
};

struct AssertionDifference {
    AssertionDifferenceCode code = AssertionDifferenceCode::ValueMismatch;
    std::optional<qsizetype> index;
    std::optional<qint64> expected;
    std::optional<qint64> expectedMinimum;
    std::optional<qint64> expectedMaximum;
    std::optional<qint64> actual;
    std::optional<quint16> actualRaw;
    QString unit;
    QString reason;
};

struct AssertionResult {
    TestStatus status = TestStatus::Fail;
    AssertionType type = AssertionType::Equals;
    QString expectedSummary;
    QString actualSummary;
    QVector<qint64> expectedValues;
    QVector<qint64> actualValues;
    QVector<quint16> actualRawValues;
    std::optional<quint8> expectedExceptionCode;
    std::optional<quint8> actualExceptionCode;
    QString unit;
    QVector<AssertionDifference> differences;

    [[nodiscard]] bool passed() const noexcept { return status == TestStatus::Pass; }
};

[[nodiscard]] AssertionResult evaluateAssertion(const ExpectedAssertion &expected,
                                                const ActualResult &actual);

} // namespace oms555tv::testing

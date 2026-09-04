#include "testing/TestAssertions.h"

#include <QStringList>

#include <utility>

namespace oms555tv::testing {
namespace {

qint64 interpreted(const quint16 raw, const ValueRepresentation representation)
{
    return representation == ValueRepresentation::Int16
        ? static_cast<qint16>(raw)
        : raw;
}

QString numberList(const QVector<qint64> &values)
{
    QStringList parts;
    parts.reserve(values.size());
    for (const qint64 value : values) {
        parts.append(QString::number(value));
    }
    return QStringLiteral("[%1]").arg(parts.join(QStringLiteral(", ")));
}

AssertionDifference difference(const AssertionDifferenceCode code,
                               const QString &reason,
                               const QString &unit)
{
    AssertionDifference result;
    result.code = code;
    result.reason = reason;
    result.unit = unit;
    return result;
}

void populateRegisterActual(AssertionResult &result,
                            const ActualResult &actual,
                            const ValueRepresentation representation)
{
    result.actualRawValues = actual.registerValues;
    result.actualValues.reserve(actual.registerValues.size());
    for (const quint16 raw : actual.registerValues) {
        result.actualValues.append(interpreted(raw, representation));
    }
    result.actualSummary = numberList(result.actualValues);
}

void populateRegisterExpected(AssertionResult &result,
                              const ExpectedAssertion &expected)
{
    switch (expected.type) {
    case AssertionType::Equals:
        result.expectedValues = {expected.value};
        result.expectedSummary = QString::number(expected.value);
        break;
    case AssertionType::Range:
        result.expectedValues = {expected.minimum, expected.maximum};
        result.expectedSummary = QStringLiteral("[%1, %2]")
            .arg(expected.minimum).arg(expected.maximum);
        break;
    case AssertionType::RegisterSequence:
        result.expectedValues = expected.values;
        result.expectedSummary = numberList(expected.values);
        break;
    case AssertionType::BitMask:
        result.expectedValues = {expected.mask, expected.value};
        result.expectedSummary = QStringLiteral("(actual & 0x%1) == 0x%2")
            .arg(expected.mask, 4, 16, QLatin1Char('0'))
            .arg(expected.value, 4, 16, QLatin1Char('0'));
        break;
    case AssertionType::ModbusException:
        break;
    }
}

} // namespace

ActualResult ActualResult::registers(QVector<quint16> values)
{
    ActualResult result;
    result.type = ActualResultType::RegisterValues;
    result.registerValues = std::move(values);
    return result;
}

ActualResult ActualResult::exception(const quint8 code)
{
    ActualResult result;
    result.type = ActualResultType::ModbusException;
    result.exceptionCode = code;
    return result;
}

AssertionResult evaluateAssertion(const ExpectedAssertion &expected,
                                  const ActualResult &actual)
{
    AssertionResult result;
    result.type = expected.type;
    result.unit = expected.unit;

    if (expected.type == AssertionType::ModbusException) {
        result.expectedExceptionCode = expected.exceptionCode;
        result.expectedSummary = QStringLiteral("Modbus exception 0x%1")
            .arg(expected.exceptionCode, 2, 16, QLatin1Char('0'));
        if (actual.type != ActualResultType::ModbusException
            || !actual.exceptionCode.has_value()) {
            populateRegisterActual(result, actual, ValueRepresentation::UInt16);
            result.differences.append(difference(
                AssertionDifferenceCode::ActualTypeMismatch,
                QStringLiteral("期望 Modbus 异常，实际为正常寄存器响应"), {}));
            return result;
        }
        result.actualExceptionCode = actual.exceptionCode;
        result.actualSummary = QStringLiteral("Modbus exception 0x%1")
            .arg(*actual.exceptionCode, 2, 16, QLatin1Char('0'));
        if (*actual.exceptionCode != expected.exceptionCode) {
            auto item = difference(AssertionDifferenceCode::ExceptionCodeMismatch,
                                   QStringLiteral("Modbus 异常码不匹配"), {});
            item.expected = expected.exceptionCode;
            item.actual = *actual.exceptionCode;
            result.differences.append(item);
            return result;
        }
        result.status = TestStatus::Pass;
        return result;
    }

    populateRegisterExpected(result, expected);
    if (actual.type != ActualResultType::RegisterValues) {
        result.actualExceptionCode = actual.exceptionCode;
        result.actualSummary = actual.exceptionCode.has_value()
            ? QStringLiteral("Modbus exception 0x%1")
                  .arg(*actual.exceptionCode, 2, 16, QLatin1Char('0'))
            : QStringLiteral("Modbus exception");
        result.differences.append(difference(
            AssertionDifferenceCode::ActualTypeMismatch,
            QStringLiteral("期望寄存器数据，实际为 Modbus 异常"), expected.unit));
        return result;
    }

    populateRegisterActual(result, actual, expected.representation);

    switch (expected.type) {
    case AssertionType::Equals: {
        if (result.actualValues.size() != 1) {
            auto item = difference(AssertionDifferenceCode::SequenceLengthMismatch,
                                   QStringLiteral("相等断言要求恰好一个寄存器"), expected.unit);
            item.expected = 1;
            item.actual = result.actualValues.size();
            result.differences.append(item);
        } else if (result.actualValues.front() != expected.value) {
            auto item = difference(AssertionDifferenceCode::ValueMismatch,
                                   QStringLiteral("寄存器值与期望不相等"), expected.unit);
            item.index = 0;
            item.expected = expected.value;
            item.actual = result.actualValues.front();
            item.actualRaw = result.actualRawValues.front();
            result.differences.append(item);
        }
        break;
    }
    case AssertionType::Range: {
        if (result.actualValues.size() != 1) {
            auto item = difference(AssertionDifferenceCode::SequenceLengthMismatch,
                                   QStringLiteral("范围断言要求恰好一个寄存器"), expected.unit);
            item.expected = 1;
            item.actual = result.actualValues.size();
            result.differences.append(item);
        } else if (result.actualValues.front() < expected.minimum
                   || result.actualValues.front() > expected.maximum) {
            auto item = difference(AssertionDifferenceCode::OutOfRange,
                                   QStringLiteral("寄存器值超出期望范围"), expected.unit);
            item.index = 0;
            item.expectedMinimum = expected.minimum;
            item.expectedMaximum = expected.maximum;
            item.actual = result.actualValues.front();
            item.actualRaw = result.actualRawValues.front();
            result.differences.append(item);
        }
        break;
    }
    case AssertionType::RegisterSequence: {
        if (result.actualValues.size() != expected.values.size()) {
            auto item = difference(AssertionDifferenceCode::SequenceLengthMismatch,
                                   QStringLiteral("寄存器序列长度不匹配"), expected.unit);
            item.expected = expected.values.size();
            item.actual = result.actualValues.size();
            result.differences.append(item);
            break;
        }
        for (qsizetype index = 0; index < expected.values.size(); ++index) {
            if (result.actualValues[index] == expected.values[index]) {
                continue;
            }
            auto item = difference(AssertionDifferenceCode::SequenceValueMismatch,
                                   QStringLiteral("寄存器序列元素不匹配"), expected.unit);
            item.index = index;
            item.expected = expected.values[index];
            item.actual = result.actualValues[index];
            item.actualRaw = result.actualRawValues[index];
            result.differences.append(item);
        }
        break;
    }
    case AssertionType::BitMask: {
        if (actual.registerValues.size() != 1) {
            auto item = difference(AssertionDifferenceCode::SequenceLengthMismatch,
                                   QStringLiteral("位掩码断言要求恰好一个寄存器"), expected.unit);
            item.expected = 1;
            item.actual = actual.registerValues.size();
            result.differences.append(item);
        } else if ((actual.registerValues.front() & expected.mask) != expected.value) {
            auto item = difference(AssertionDifferenceCode::BitMaskMismatch,
                                   QStringLiteral("寄存器掩码结果与期望不匹配"), expected.unit);
            item.index = 0;
            item.expected = expected.value;
            item.actual = actual.registerValues.front() & expected.mask;
            item.actualRaw = actual.registerValues.front();
            result.differences.append(item);
        }
        break;
    }
    case AssertionType::ModbusException:
        break;
    }

    if (result.differences.isEmpty()) {
        result.status = TestStatus::Pass;
    }
    return result;
}

} // namespace oms555tv::testing

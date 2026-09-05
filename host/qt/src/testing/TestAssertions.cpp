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

qint64 decimalDenominator(const int decimalPlaces)
{
    qint64 result = 1;
    for (int index = 0; index < decimalPlaces; ++index) result *= 10;
    return result;
}

QString scaledNumber(const qint64 numerator, const int decimalPlaces)
{
    if (decimalPlaces == 0) return QString::number(numerator);
    const qint64 denominator = decimalDenominator(decimalPlaces);
    const quint64 absolute = numerator < 0
        ? static_cast<quint64>(-(numerator + 1)) + 1U
        : static_cast<quint64>(numerator);
    const QString sign = numerator < 0 ? QStringLiteral("-") : QString{};
    return QStringLiteral("%1%2.%3")
        .arg(sign)
        .arg(absolute / static_cast<quint64>(denominator))
        .arg(absolute % static_cast<quint64>(denominator), decimalPlaces, 10,
             QLatin1Char('0'));
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
    case AssertionType::Elements:
    case AssertionType::ResponseTimeout:
    case AssertionType::UInt32:
    case AssertionType::StabilitySummary:
        break;
    }
}

AssertionResult evaluateElements(const ExpectedAssertion &expected,
                                 const ActualResult &actual)
{
    AssertionResult result;
    result.type = AssertionType::Elements;
    result.expectedElements = expected.elements;
    result.expectedSummary = QStringLiteral("%1 个逐元素断言").arg(expected.elements.size());
    if (actual.type != ActualResultType::RegisterValues) {
        result.differences.append(difference(AssertionDifferenceCode::ActualTypeMismatch,
                                             QStringLiteral("期望寄存器数据"), {}));
        return result;
    }
    result.actualRawValues = actual.registerValues;
    if (actual.registerValues.size() != expected.elements.size()) {
        auto item = difference(AssertionDifferenceCode::ElementCountMismatch,
                               QStringLiteral("逐元素断言数量与实际寄存器数量不一致"), {});
        item.expected = expected.elements.size();
        item.actual = actual.registerValues.size();
        result.differences.append(item);
        return result;
    }

    QStringList actualParts;
    for (const ElementAssertion &element : expected.elements) {
        if (element.index < 0 || element.index >= actual.registerValues.size()) {
            auto item = difference(AssertionDifferenceCode::ElementCountMismatch,
                                   QStringLiteral("逐元素断言索引超出实际寄存器范围"),
                                   element.unit);
            item.index = element.index;
            result.differences.append(item);
            continue;
        }
        const quint16 raw = actual.registerValues[element.index];
        const qint64 value = interpreted(raw, element.representation);
        const qint64 denominator = decimalDenominator(element.decimalPlaces);
        result.actualValues.append(value);
        result.actualScaledValues.append({value, denominator});
        actualParts.append(QStringLiteral("%1=%2%3")
                               .arg(element.index)
                               .arg(scaledNumber(value, element.decimalPlaces))
                               .arg(element.unit));
        if (element.type == AssertionType::Equals && value != element.value) {
            auto item = difference(AssertionDifferenceCode::ValueMismatch,
                                   QStringLiteral("逐元素值与期望不相等"), element.unit);
            item.index = element.index;
            item.expected = element.value;
            item.actual = value;
            item.actualRaw = raw;
            item.scaledDenominator = denominator;
            result.differences.append(item);
        } else if (element.type == AssertionType::Range
                   && (value < element.minimum || value > element.maximum)) {
            auto item = difference(AssertionDifferenceCode::OutOfRange,
                                   QStringLiteral("逐元素值超出期望范围"), element.unit);
            item.index = element.index;
            item.expectedMinimum = element.minimum;
            item.expectedMaximum = element.maximum;
            item.actual = value;
            item.actualRaw = raw;
            item.scaledDenominator = denominator;
            result.differences.append(item);
        } else if (element.type == AssertionType::BitMask
                   && (raw & element.mask) != element.value) {
            auto item = difference(AssertionDifferenceCode::BitMaskMismatch,
                                   QStringLiteral("逐元素掩码结果与期望不匹配"), element.unit);
            item.index = element.index;
            item.expected = element.value;
            item.actual = raw & element.mask;
            item.actualRaw = raw;
            item.scaledDenominator = 1;
            result.differences.append(item);
        }
    }
    result.actualSummary = QStringLiteral("[%1]").arg(actualParts.join(QStringLiteral(", ")));
    if (result.differences.isEmpty()) result.status = TestStatus::Pass;
    return result;
}

quint32 combineLowWordFirst(const QVector<quint16> &words)
{
    return static_cast<quint32>(words[0])
        | (static_cast<quint32>(words[1]) << 16U);
}

AssertionResult evaluateUInt32(const ExpectedAssertion &expected,
                               const ActualResult &actual)
{
    AssertionResult result;
    result.type = AssertionType::UInt32;
    result.expectedUInt32 = expected.uint32;
    result.unit = expected.uint32.unit;
    result.expectedSummary = QStringLiteral("uint32 [%1, %2]")
        .arg(expected.uint32.minimum).arg(expected.uint32.maximum);
    QVector<QVector<quint16>> samples;
    if (actual.type == ActualResultType::RegisterValues) {
        samples.append(actual.registerValues);
    } else if (actual.type == ActualResultType::RegisterSamples) {
        samples = actual.registerSamples;
    } else {
        result.differences.append(difference(AssertionDifferenceCode::ActualTypeMismatch,
                                             QStringLiteral("期望 uint32 寄存器样本"),
                                             expected.uint32.unit));
        return result;
    }
    result.actualRawSamples = samples;
    QStringList actualParts;
    const qint64 denominator = decimalDenominator(expected.uint32.decimalPlaces);
    for (qsizetype index = 0; index < samples.size(); ++index) {
        if (samples[index].size() != 2) {
            auto item = difference(AssertionDifferenceCode::UInt32WordCountMismatch,
                                   QStringLiteral("uint32 样本必须恰好包含两个寄存器"),
                                   expected.uint32.unit);
            item.index = index;
            item.expected = 2;
            item.actual = samples[index].size();
            result.differences.append(item);
            continue;
        }
        const quint32 value = combineLowWordFirst(samples[index]);
        result.actualValues.append(static_cast<qint64>(value));
        result.actualScaledValues.append({static_cast<qint64>(value), denominator});
        actualParts.append(scaledNumber(value, expected.uint32.decimalPlaces));
        if (value < expected.uint32.minimum || value > expected.uint32.maximum) {
            auto item = difference(AssertionDifferenceCode::UInt32OutOfRange,
                                   QStringLiteral("uint32 组合值超出期望范围"),
                                   expected.uint32.unit);
            item.index = index;
            item.expectedMinimum = static_cast<qint64>(expected.uint32.minimum);
            item.expectedMaximum = static_cast<qint64>(expected.uint32.maximum);
            item.actual = static_cast<qint64>(value);
            item.scaledDenominator = denominator;
            result.differences.append(item);
        }
    }
    result.actualSummary = QStringLiteral("[%1] %2")
        .arg(actualParts.join(QStringLiteral(", ")), expected.uint32.unit);
    if (result.differences.isEmpty()
        && expected.uint32.comparison != UInt32Comparison::Range) {
        for (qsizetype index = 1; index < result.actualValues.size(); ++index) {
            const bool ordered = expected.uint32.comparison == UInt32Comparison::NonDecreasing
                ? result.actualValues[index] >= result.actualValues[index - 1]
                : result.actualValues[index] > result.actualValues[index - 1];
            if (ordered) continue;
            auto item = difference(AssertionDifferenceCode::UInt32MonotonicityMismatch,
                                   QStringLiteral("uint32 样本不满足声明的单调性"),
                                   expected.uint32.unit);
            item.index = index;
            item.expected = result.actualValues[index - 1];
            item.actual = result.actualValues[index];
            result.differences.append(item);
            break;
        }
    }
    if (result.differences.isEmpty()) result.status = TestStatus::Pass;
    return result;
}

quint64 allowedFailures(const quint64 total, const quint32 thresholdPpm)
{
    const quint64 scale = failureRateScalePpm;
    return (total / scale) * thresholdPpm
        + ((total % scale) * thresholdPpm) / scale;
}

AssertionResult evaluateStability(const ExpectedAssertion &expected,
                                  const ActualResult &actual)
{
    AssertionResult result;
    result.type = AssertionType::StabilitySummary;
    result.expectedStability = expected.stability;
    result.expectedSummary = QStringLiteral("total>=%1 success>=%2 failure_rate<=%3ppm")
        .arg(expected.stability.minimumTotal)
        .arg(expected.stability.minimumSuccesses)
        .arg(expected.stability.maximumFailureRatePpm);
    if (actual.type != ActualResultType::StabilitySummary || !actual.stability.has_value()) {
        result.differences.append(difference(AssertionDifferenceCode::ActualTypeMismatch,
                                             QStringLiteral("期望稳定性汇总"), {}));
        return result;
    }
    const StabilityStatistics &stats = *actual.stability;
    result.stability = stats;
    result.actualSummary = QStringLiteral("total=%1 success=%2 failure=%3 timeout=%4")
        .arg(stats.total).arg(stats.successes).arg(stats.failures).arg(stats.timeouts);
    if (stats.total != stats.successes + stats.failures + stats.timeouts) {
        result.differences.append(difference(AssertionDifferenceCode::StabilityInvariantMismatch,
                                             QStringLiteral("total 不等于 success+failure+timeout"), {}));
    }
    if (stats.total != stats.validRttSamples + stats.missingRttSamples) {
        result.differences.append(difference(AssertionDifferenceCode::StabilityInvariantMismatch,
                                             QStringLiteral("total 不等于有效 RTT+缺失 RTT"), {}));
    }
    auto addCountDifference = [&result](const QString &reason,
                                        const quint64 expectedValue,
                                        const quint64 actualValue) {
        auto item = difference(AssertionDifferenceCode::StabilityCountMismatch, reason, {});
        item.expected = static_cast<qint64>(expectedValue);
        item.actual = static_cast<qint64>(actualValue);
        result.differences.append(item);
    };
    if (stats.total < expected.stability.minimumTotal) {
        addCountDifference(QStringLiteral("总请求数低于下限"),
                           expected.stability.minimumTotal, stats.total);
    }
    if (stats.successes < expected.stability.minimumSuccesses) {
        addCountDifference(QStringLiteral("成功数低于下限"),
                           expected.stability.minimumSuccesses, stats.successes);
    }
    if (stats.failures > expected.stability.maximumFailures) {
        addCountDifference(QStringLiteral("失败数超过上限"),
                           expected.stability.maximumFailures, stats.failures);
    }
    if (stats.timeouts > expected.stability.maximumTimeouts) {
        addCountDifference(QStringLiteral("超时数超过上限"),
                           expected.stability.maximumTimeouts, stats.timeouts);
    }
    const quint64 failed = stats.failures + stats.timeouts;
    if (stats.total == 0 || failed > allowedFailures(stats.total,
                                                     expected.stability.maximumFailureRatePpm)) {
        auto item = difference(AssertionDifferenceCode::FailureRateExceeded,
                               QStringLiteral("失败率超过 ppm 上限"), QStringLiteral("ppm"));
        item.expected = expected.stability.maximumFailureRatePpm;
        item.actual = stats.total == 0
            ? failureRateScalePpm
            : static_cast<qint64>((failed * failureRateScalePpm) / stats.total);
        result.differences.append(item);
    }
    const bool hasAllRtt = stats.validRttSamples > 0 && stats.minimumRttMs.has_value()
        && stats.averageRttMs.has_value() && stats.maximumRttMs.has_value();
    if (expected.stability.requireValidRttSamples && !hasAllRtt) {
        result.differences.append(difference(AssertionDifferenceCode::RttMissing,
                                             QStringLiteral("缺少有效 RTT 样本"),
                                             QStringLiteral("ms")));
    } else if (hasAllRtt) {
        if (*stats.minimumRttMs > *stats.averageRttMs
            || *stats.averageRttMs > *stats.maximumRttMs) {
            result.differences.append(difference(AssertionDifferenceCode::StabilityInvariantMismatch,
                                                 QStringLiteral("RTT min/avg/max 顺序非法"),
                                                 QStringLiteral("ms")));
        }
        if (*stats.minimumRttMs < expected.stability.minimumRttMs
            || *stats.averageRttMs > expected.stability.maximumAverageRttMs
            || *stats.maximumRttMs > expected.stability.maximumRttMs) {
            result.differences.append(difference(AssertionDifferenceCode::RttOutOfRange,
                                                 QStringLiteral("RTT 汇总超出期望边界"),
                                                 QStringLiteral("ms")));
        }
    }
    if (result.differences.isEmpty()) result.status = TestStatus::Pass;
    return result;
}

} // namespace

ActualResult ActualResult::registers(QVector<quint16> values)
{
    ActualResult result;
    result.type = ActualResultType::RegisterValues;
    result.registerValues = std::move(values);
    return result;
}

ActualResult ActualResult::samples(QVector<QVector<quint16>> values)
{
    ActualResult result;
    result.type = ActualResultType::RegisterSamples;
    result.registerSamples = std::move(values);
    return result;
}

ActualResult ActualResult::exception(const quint8 code)
{
    ActualResult result;
    result.type = ActualResultType::ModbusException;
    result.exceptionCode = code;
    return result;
}

ActualResult ActualResult::responseTimeout()
{
    ActualResult result;
    result.type = ActualResultType::ResponseTimeout;
    return result;
}

ActualResult ActualResult::communicationError()
{
    ActualResult result;
    result.type = ActualResultType::CommunicationError;
    return result;
}

ActualResult ActualResult::stabilitySummary(StabilityStatistics value)
{
    ActualResult result;
    result.type = ActualResultType::StabilitySummary;
    result.stability = std::move(value);
    return result;
}

AssertionResult evaluateAssertion(const ExpectedAssertion &expected,
                                  const ActualResult &actual)
{
    AssertionResult result;
    result.type = expected.type;
    result.unit = expected.unit;

    if (expected.type == AssertionType::Elements) {
        return evaluateElements(expected, actual);
    }
    if (expected.type == AssertionType::UInt32) {
        return evaluateUInt32(expected, actual);
    }
    if (expected.type == AssertionType::StabilitySummary) {
        return evaluateStability(expected, actual);
    }
    if (expected.type == AssertionType::ResponseTimeout) {
        result.expectedSummary = QStringLiteral("ResponseTimeout");
        if (actual.type == ActualResultType::ResponseTimeout) {
            result.actualSummary = QStringLiteral("ResponseTimeout");
            result.status = TestStatus::Pass;
            return result;
        }
        result.actualSummary = actual.type == ActualResultType::CommunicationError
            ? QStringLiteral("OtherCommunicationError") : QStringLiteral("NonTimeoutResponse");
        result.status = actual.type == ActualResultType::CommunicationError
            ? TestStatus::Error : TestStatus::Fail;
        result.differences.append(difference(AssertionDifferenceCode::ActualTypeMismatch,
                                             QStringLiteral("实际结果不是目标 ResponseTimeout"), {}));
        return result;
    }

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
    case AssertionType::Elements:
    case AssertionType::ResponseTimeout:
    case AssertionType::UInt32:
    case AssertionType::StabilitySummary:
        break;
    }

    if (result.differences.isEmpty()) {
        result.status = TestStatus::Pass;
    }
    return result;
}

} // namespace oms555tv::testing

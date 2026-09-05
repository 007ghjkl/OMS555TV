#include "testing/TestCaseLoaderV2.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <initializer_list>
#include <limits>
#include <optional>
#include <utility>

namespace oms555tv::testing {
namespace {

constexpr qsizetype maximumSuiteCases = 1000;
constexpr qsizetype maximumTags = 32;
constexpr qsizetype maximumMetadataEntries = 32;
constexpr quint64 maximumStabilitySamples =
    (maximumStabilityDurationMs + minimumStabilityIntervalMs - 1)
    / minimumStabilityIntervalMs;

QString childPath(const QString &path, const QString &field)
{
    return path + QLatin1Char('/') + field;
}

class Parser final
{
public:
    explicit Parser(QVector<ConfigError> &errors)
        : errors_(errors)
    {
    }

    void setSuiteId(QString id) { suiteId_ = std::move(id); }
    void setCaseId(QString id) { caseId_ = std::move(id); }
    void clearCaseId() { caseId_.clear(); }

    void error(const ConfigErrorCode code, const QString &path, const QString &diagnostic)
    {
        errors_.append({code, path, suiteId_, caseId_, diagnostic});
    }

    bool allowedFields(const QJsonObject &object,
                       const QString &path,
                       std::initializer_list<QString> allowed)
    {
        const QSet<QString> allowedSet(allowed.begin(), allowed.end());
        bool valid = true;
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (allowedSet.contains(it.key())) continue;
            error(ConfigErrorCode::UnknownField, childPath(path, it.key()),
                  QStringLiteral("不支持字段“%1”").arg(it.key()));
            valid = false;
        }
        return valid;
    }

    std::optional<QString> requiredString(const QJsonObject &object,
                                          const QString &field,
                                          const QString &path,
                                          const qsizetype maximumLength,
                                          const bool identifier = false)
    {
        if (!object.contains(field)) {
            error(ConfigErrorCode::MissingField, childPath(path, field),
                  QStringLiteral("缺少必填字符串字段“%1”").arg(field));
            return std::nullopt;
        }
        return stringValue(object.value(field), childPath(path, field), maximumLength,
                           false, identifier);
    }

    std::optional<QString> optionalString(const QJsonObject &object,
                                          const QString &field,
                                          const QString &path,
                                          const qsizetype maximumLength)
    {
        if (!object.contains(field)) return QString{};
        return stringValue(object.value(field), childPath(path, field), maximumLength,
                           true, false);
    }

    std::optional<qint64> requiredInteger(const QJsonObject &object,
                                          const QString &field,
                                          const QString &path,
                                          const qint64 minimum,
                                          const qint64 maximum)
    {
        if (!object.contains(field)) {
            error(ConfigErrorCode::MissingField, childPath(path, field),
                  QStringLiteral("缺少必填整数字段“%1”").arg(field));
            return std::nullopt;
        }
        return integerValue(object.value(field), childPath(path, field), minimum, maximum);
    }

    std::optional<qint64> optionalInteger(const QJsonObject &object,
                                          const QString &field,
                                          const QString &path,
                                          const qint64 minimum,
                                          const qint64 maximum,
                                          const qint64 defaultValue)
    {
        if (!object.contains(field)) return defaultValue;
        return integerValue(object.value(field), childPath(path, field), minimum, maximum);
    }

    std::optional<bool> requiredBoolean(const QJsonObject &object,
                                        const QString &field,
                                        const QString &path)
    {
        if (!object.contains(field)) {
            error(ConfigErrorCode::MissingField, childPath(path, field),
                  QStringLiteral("缺少必填布尔字段“%1”").arg(field));
            return std::nullopt;
        }
        if (!object.value(field).isBool()) {
            error(ConfigErrorCode::WrongType, childPath(path, field),
                  QStringLiteral("字段“%1”必须为布尔值").arg(field));
            return std::nullopt;
        }
        return object.value(field).toBool();
    }

    std::optional<QJsonObject> requiredObject(const QJsonObject &object,
                                              const QString &field,
                                              const QString &path)
    {
        if (!object.contains(field)) {
            error(ConfigErrorCode::MissingField, childPath(path, field),
                  QStringLiteral("缺少必填对象字段“%1”").arg(field));
            return std::nullopt;
        }
        if (!object.value(field).isObject()) {
            error(ConfigErrorCode::WrongType, childPath(path, field),
                  QStringLiteral("字段“%1”必须为对象").arg(field));
            return std::nullopt;
        }
        return object.value(field).toObject();
    }

    std::optional<QStringList> tags(const QJsonObject &object,
                                    const QString &field,
                                    const QString &path)
    {
        if (!object.contains(field)) return QStringList{};
        const QString fieldPath = childPath(path, field);
        if (!object.value(field).isArray()) {
            error(ConfigErrorCode::WrongType, fieldPath, QStringLiteral("标签必须为数组"));
            return std::nullopt;
        }
        const QJsonArray array = object.value(field).toArray();
        if (array.size() > maximumTags) {
            error(ConfigErrorCode::OutOfRange, fieldPath,
                  QStringLiteral("标签数量不得超过 32"));
            return std::nullopt;
        }
        QStringList result;
        QSet<QString> seen;
        bool valid = true;
        for (qsizetype index = 0; index < array.size(); ++index) {
            const QString itemPath = childPath(fieldPath, QString::number(index));
            const auto tag = stringValue(array.at(index), itemPath, 32, false, false);
            if (!tag) {
                valid = false;
            } else if (seen.contains(*tag)) {
                error(ConfigErrorCode::DuplicateTag, itemPath,
                      QStringLiteral("标签“%1”重复").arg(*tag));
                valid = false;
            } else {
                seen.insert(*tag);
                result.append(*tag);
            }
        }
        return valid ? std::optional<QStringList>(result) : std::nullopt;
    }

    std::optional<qint64> integerValue(const QJsonValue &value,
                                       const QString &path,
                                       const qint64 minimum,
                                       const qint64 maximum)
    {
        if (!value.isDouble()) {
            error(ConfigErrorCode::WrongType, path, QStringLiteral("字段必须为整数"));
            return std::nullopt;
        }
        const double number = value.toDouble();
        if (!std::isfinite(number) || std::floor(number) != number) {
            error(ConfigErrorCode::WrongType, path, QStringLiteral("字段必须为整数"));
            return std::nullopt;
        }
        if (number < static_cast<double>(minimum)
            || number > static_cast<double>(maximum)) {
            error(ConfigErrorCode::OutOfRange, path,
                  QStringLiteral("整数超出允许范围 %1..%2").arg(minimum).arg(maximum));
            return std::nullopt;
        }
        return static_cast<qint64>(number);
    }

private:
    std::optional<QString> stringValue(const QJsonValue &value,
                                       const QString &path,
                                       const qsizetype maximumLength,
                                       const bool allowEmpty,
                                       const bool identifier)
    {
        if (!value.isString()) {
            error(ConfigErrorCode::WrongType, path, QStringLiteral("字段必须为字符串"));
            return std::nullopt;
        }
        const QString result = value.toString().trimmed();
        if (!allowEmpty && result.isEmpty()) {
            error(ConfigErrorCode::EmptyString, path, QStringLiteral("字符串不得为空"));
            return std::nullopt;
        }
        if (result.size() > maximumLength) {
            error(ConfigErrorCode::OutOfRange, path,
                  QStringLiteral("字符串长度不得超过 %1").arg(maximumLength));
            return std::nullopt;
        }
        static const QRegularExpression idPattern(
            QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
        if (identifier && !idPattern.match(result).hasMatch()) {
            error(ConfigErrorCode::InvalidIdentifier, path,
                  QStringLiteral("标识符仅允许字母、数字、点、下划线和连字符，且首字符必须为字母或数字"));
            return std::nullopt;
        }
        return result;
    }

    QVector<ConfigError> &errors_;
    QString suiteId_;
    QString caseId_;
};

bool addressRangeValid(Parser &parser,
                       const QString &path,
                       const qint64 address,
                       const qint64 count)
{
    const quint32 end = static_cast<quint32>(address)
        + static_cast<quint32>(count) - 1U;
    if (end <= std::numeric_limits<quint16>::max()) return true;
    parser.error(ConfigErrorCode::AddressRangeOverflow, path,
                 QStringLiteral("PDU 地址范围末端超过 65535"));
    return false;
}

std::optional<ExecutionEnvironment> parseEnvironment(Parser &parser,
                                                      const QJsonObject &object,
                                                      const QString &path)
{
    const auto name = parser.requiredString(object, QStringLiteral("environment"), path, 16);
    if (!name) return std::nullopt;
    if (*name == QStringLiteral("both")) return ExecutionEnvironment::Both;
    if (*name == QStringLiteral("real_rs485")) return ExecutionEnvironment::RealRs485;
    if (*name == QStringLiteral("fake")) return ExecutionEnvironment::Fake;
    parser.error(ConfigErrorCode::OutOfRange, childPath(path, QStringLiteral("environment")),
                 QStringLiteral("environment 仅支持 both、real_rs485 或 fake"));
    return std::nullopt;
}

std::optional<TestCaseType> parseCaseType(Parser &parser,
                                          const QJsonObject &object,
                                          const QString &path)
{
    const auto name = parser.requiredString(object, QStringLiteral("type"), path, 32);
    if (!name) return std::nullopt;
    if (*name == QStringLiteral("read_register")) return TestCaseType::ReadRegister;
    if (*name == QStringLiteral("read_registers")) return TestCaseType::ReadRegisters;
    if (*name == QStringLiteral("write_register")) return TestCaseType::WriteRegister;
    if (*name == QStringLiteral("write_and_verify")) return TestCaseType::WriteAndVerify;
    if (*name == QStringLiteral("expect_exception")) return TestCaseType::ExpectException;
    if (*name == QStringLiteral("sequence")) return TestCaseType::Sequence;
    if (*name == QStringLiteral("expect_timeout")) return TestCaseType::ExpectTimeout;
    if (*name == QStringLiteral("consistency")) return TestCaseType::Consistency;
    if (*name == QStringLiteral("stability")) return TestCaseType::Stability;
    parser.error(ConfigErrorCode::UnknownCaseType, childPath(path, QStringLiteral("type")),
                 QStringLiteral("未知用例类型“%1”").arg(*name));
    return std::nullopt;
}

std::optional<TestRequest> parseRequest(Parser &parser,
                                        const QJsonObject &object,
                                        const QString &path,
                                        const TestCaseType type)
{
    TestRequest request;
    if (type == TestCaseType::ReadRegister || type == TestCaseType::ReadRegisters
        || type == TestCaseType::Consistency || type == TestCaseType::Stability
        || type == TestCaseType::ExpectTimeout) {
        parser.allowedFields(object, path, {QStringLiteral("function"),
                                            QStringLiteral("address"),
                                            QStringLiteral("count")});
        const auto function = parser.requiredInteger(object, QStringLiteral("function"), path, 3, 3);
        const auto address = parser.requiredInteger(object, QStringLiteral("address"), path, 0, 65535);
        const qint64 minimumCount = 1;
        const qint64 maximumCount = type == TestCaseType::ReadRegister ? 1
            : type == TestCaseType::Consistency ? 2 : maximumReadRegisterCount;
        const auto count = parser.requiredInteger(object, QStringLiteral("count"), path,
                                                   minimumCount, maximumCount);
        if (!function || !address || !count
            || !addressRangeValid(parser, path, *address, *count)) return std::nullopt;
        if (type == TestCaseType::Consistency && *count != 2) {
            parser.error(ConfigErrorCode::InvalidCombination,
                         childPath(path, QStringLiteral("count")),
                         QStringLiteral("consistency 必须读取恰好两个寄存器"));
            return std::nullopt;
        }
        request.function = ModbusFunction::ReadHoldingRegisters;
        request.address = device::PduAddress(static_cast<quint16>(*address));
        request.count = static_cast<quint16>(*count);
        return request;
    }

    if (type == TestCaseType::WriteRegister) {
        parser.allowedFields(object, path, {QStringLiteral("function"),
                                            QStringLiteral("address"),
                                            QStringLiteral("value")});
        const auto function = parser.requiredInteger(object, QStringLiteral("function"), path, 6, 6);
        const auto address = parser.requiredInteger(object, QStringLiteral("address"), path, 0, 65535);
        const auto value = parser.requiredInteger(object, QStringLiteral("value"), path, 0, 65535);
        if (!function || !address || !value) return std::nullopt;
        request.function = ModbusFunction::WriteSingleRegister;
        request.address = device::PduAddress(static_cast<quint16>(*address));
        request.rawValue = static_cast<quint16>(*value);
        return request;
    }

    if (type == TestCaseType::WriteAndVerify) {
        parser.allowedFields(object, path, {QStringLiteral("function"),
                                            QStringLiteral("verify_function"),
                                            QStringLiteral("address"),
                                            QStringLiteral("value"),
                                            QStringLiteral("read_before_write"),
                                            QStringLiteral("restore_original")});
        const auto function = parser.requiredInteger(object, QStringLiteral("function"), path, 6, 6);
        const auto verify = parser.requiredInteger(object, QStringLiteral("verify_function"), path, 3, 3);
        const auto address = parser.requiredInteger(object, QStringLiteral("address"), path, 0, 65535);
        const auto value = parser.requiredInteger(object, QStringLiteral("value"), path, 0, 65535);
        const auto preRead = parser.requiredBoolean(object, QStringLiteral("read_before_write"), path);
        const auto restore = parser.requiredBoolean(object, QStringLiteral("restore_original"), path);
        if (!function || !verify || !address || !value || !preRead || !restore) return std::nullopt;
        if (*restore && !*preRead) {
            parser.error(ConfigErrorCode::InvalidCombination,
                         childPath(path, QStringLiteral("restore_original")),
                         QStringLiteral("恢复原值要求 read_before_write=true"));
            return std::nullopt;
        }
        request.function = ModbusFunction::WriteSingleRegister;
        request.address = device::PduAddress(static_cast<quint16>(*address));
        request.rawValue = static_cast<quint16>(*value);
        request.count = 1;
        request.readBeforeWrite = *preRead;
        request.restoreOriginal = *restore;
        return request;
    }

    if (type != TestCaseType::ExpectException) return std::nullopt;
    const auto function = parser.requiredInteger(object, QStringLiteral("function"), path, 3, 6);
    if (!function) return std::nullopt;
    if (*function == 3) {
        parser.allowedFields(object, path, {QStringLiteral("function"),
                                            QStringLiteral("address"),
                                            QStringLiteral("count")});
        const auto address = parser.requiredInteger(object, QStringLiteral("address"), path, 0, 65535);
        const auto count = parser.requiredInteger(object, QStringLiteral("count"), path, 1,
                                                   maximumReadRegisterCount);
        if (!address || !count) return std::nullopt;
        // expect_exception 允许请求本身跨越实现地址，但 PDU 16 位范围仍不能溢出。
        if (!addressRangeValid(parser, path, *address, *count)) return std::nullopt;
        request.function = ModbusFunction::ReadHoldingRegisters;
        request.address = device::PduAddress(static_cast<quint16>(*address));
        request.count = static_cast<quint16>(*count);
        return request;
    }
    if (*function != 6) {
        parser.error(ConfigErrorCode::OutOfRange, childPath(path, QStringLiteral("function")),
                     QStringLiteral("expect_exception 仅支持功能码 3 或 6"));
        return std::nullopt;
    }
    parser.allowedFields(object, path, {QStringLiteral("function"),
                                        QStringLiteral("address"),
                                        QStringLiteral("value")});
    const auto address = parser.requiredInteger(object, QStringLiteral("address"), path, 0, 65535);
    const auto value = parser.requiredInteger(object, QStringLiteral("value"), path, 0, 65535);
    if (!address || !value) return std::nullopt;
    request.function = ModbusFunction::WriteSingleRegister;
    request.address = device::PduAddress(static_cast<quint16>(*address));
    request.rawValue = static_cast<quint16>(*value);
    return request;
}

std::optional<ValueRepresentation> parseRepresentation(Parser &parser,
                                                        const QJsonObject &object,
                                                        const QString &path)
{
    if (!object.contains(QStringLiteral("representation"))) return ValueRepresentation::UInt16;
    const auto name = parser.requiredString(object, QStringLiteral("representation"), path, 16);
    if (!name) return std::nullopt;
    if (*name == QStringLiteral("uint16")) return ValueRepresentation::UInt16;
    if (*name == QStringLiteral("int16")) return ValueRepresentation::Int16;
    parser.error(ConfigErrorCode::OutOfRange,
                 childPath(path, QStringLiteral("representation")),
                 QStringLiteral("representation 仅支持 uint16 或 int16"));
    return std::nullopt;
}

std::optional<ElementAssertion> parseElement(Parser &parser,
                                             const QJsonValue &value,
                                             const QString &path)
{
    if (!value.isObject()) {
        parser.error(ConfigErrorCode::WrongType, path, QStringLiteral("逐元素断言必须为对象"));
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const auto index = parser.requiredInteger(object, QStringLiteral("index"), path, 0,
                                               maximumReadRegisterCount - 1);
    const auto type = parser.requiredString(object, QStringLiteral("type"), path, 16);
    if (!index || !type) return std::nullopt;

    ElementAssertion result;
    result.index = static_cast<qsizetype>(*index);
    const auto unit = parser.optionalString(object, QStringLiteral("unit"), path, 16);
    const auto decimalPlaces = parser.optionalInteger(object, QStringLiteral("decimal_places"),
                                                       path, 0, 6, 0);
    if (!unit || !decimalPlaces) return std::nullopt;
    result.unit = *unit;
    result.decimalPlaces = static_cast<int>(*decimalPlaces);

    if (*type == QStringLiteral("bitmask")) {
        parser.allowedFields(object, path, {QStringLiteral("index"), QStringLiteral("type"),
                                            QStringLiteral("mask"), QStringLiteral("value"),
                                            QStringLiteral("decimal_places"), QStringLiteral("unit")});
        const auto mask = parser.requiredInteger(object, QStringLiteral("mask"), path, 0, 65535);
        const auto expected = parser.requiredInteger(object, QStringLiteral("value"), path, 0, 65535);
        if (!mask || !expected) return std::nullopt;
        if (*decimalPlaces != 0) {
            parser.error(ConfigErrorCode::InvalidCombination,
                         childPath(path, QStringLiteral("decimal_places")),
                         QStringLiteral("bitmask 的 decimal_places 必须为 0"));
            return std::nullopt;
        }
        if ((static_cast<quint16>(*expected) & ~static_cast<quint16>(*mask)) != 0) {
            parser.error(ConfigErrorCode::InvalidCombination,
                         childPath(path, QStringLiteral("value")),
                         QStringLiteral("bitmask 期望值不得包含 mask 之外的位"));
            return std::nullopt;
        }
        result.type = AssertionType::BitMask;
        result.mask = static_cast<quint16>(*mask);
        result.value = *expected;
        return result;
    }

    const auto representation = parseRepresentation(parser, object, path);
    if (!representation) return std::nullopt;
    result.representation = *representation;
    const qint64 minimum = *representation == ValueRepresentation::Int16 ? -32768 : 0;
    const qint64 maximum = *representation == ValueRepresentation::Int16 ? 32767 : 65535;
    if (*type == QStringLiteral("equals")) {
        parser.allowedFields(object, path, {QStringLiteral("index"), QStringLiteral("type"),
                                            QStringLiteral("value"), QStringLiteral("representation"),
                                            QStringLiteral("decimal_places"), QStringLiteral("unit")});
        const auto expected = parser.requiredInteger(object, QStringLiteral("value"), path,
                                                      minimum, maximum);
        if (!expected) return std::nullopt;
        result.type = AssertionType::Equals;
        result.value = *expected;
        return result;
    }
    if (*type == QStringLiteral("range")) {
        parser.allowedFields(object, path, {QStringLiteral("index"), QStringLiteral("type"),
                                            QStringLiteral("min"), QStringLiteral("max"),
                                            QStringLiteral("representation"),
                                            QStringLiteral("decimal_places"), QStringLiteral("unit")});
        const auto minValue = parser.requiredInteger(object, QStringLiteral("min"), path,
                                                      minimum, maximum);
        const auto maxValue = parser.requiredInteger(object, QStringLiteral("max"), path,
                                                      minimum, maximum);
        if (!minValue || !maxValue) return std::nullopt;
        if (*minValue > *maxValue) {
            parser.error(ConfigErrorCode::InvalidCombination, childPath(path, QStringLiteral("max")),
                         QStringLiteral("范围上限不得小于下限"));
            return std::nullopt;
        }
        result.type = AssertionType::Range;
        result.minimum = *minValue;
        result.maximum = *maxValue;
        return result;
    }
    parser.error(ConfigErrorCode::UnknownAssertionType, childPath(path, QStringLiteral("type")),
                 QStringLiteral("逐元素断言只支持 equals、range 或 bitmask"));
    return std::nullopt;
}

std::optional<ExpectedAssertion> parseExpected(Parser &parser,
                                               const QJsonObject &object,
                                               const QString &path)
{
    const auto typeName = parser.requiredString(object, QStringLiteral("type"), path, 32);
    if (!typeName) return std::nullopt;
    ExpectedAssertion result;

    if (*typeName == QStringLiteral("modbus_exception")) {
        parser.allowedFields(object, path, {QStringLiteral("type"), QStringLiteral("code")});
        const auto code = parser.requiredInteger(object, QStringLiteral("code"), path, 1, 255);
        if (!code) return std::nullopt;
        result.type = AssertionType::ModbusException;
        result.exceptionCode = static_cast<quint8>(*code);
        return result;
    }
    if (*typeName == QStringLiteral("response_timeout")) {
        parser.allowedFields(object, path, {QStringLiteral("type"), QStringLiteral("source")});
        const auto source = parser.requiredString(object, QStringLiteral("source"), path, 32);
        if (!source) return std::nullopt;
        if (*source != QStringLiteral("deterministic_no_response")) {
            parser.error(ConfigErrorCode::InvalidCombination,
                         childPath(path, QStringLiteral("source")),
                         QStringLiteral("response_timeout 仅接受 deterministic_no_response"));
            return std::nullopt;
        }
        result.type = AssertionType::ResponseTimeout;
        return result;
    }
    if (*typeName == QStringLiteral("elements")) {
        parser.allowedFields(object, path, {QStringLiteral("type"), QStringLiteral("items")});
        if (!object.contains(QStringLiteral("items"))) {
            parser.error(ConfigErrorCode::MissingField, childPath(path, QStringLiteral("items")),
                         QStringLiteral("elements 缺少 items"));
            return std::nullopt;
        }
        if (!object.value(QStringLiteral("items")).isArray()) {
            parser.error(ConfigErrorCode::WrongType, childPath(path, QStringLiteral("items")),
                         QStringLiteral("items 必须为数组"));
            return std::nullopt;
        }
        const QJsonArray items = object.value(QStringLiteral("items")).toArray();
        if (items.isEmpty() || items.size() > maximumReadRegisterCount) {
            parser.error(ConfigErrorCode::OutOfRange, childPath(path, QStringLiteral("items")),
                         QStringLiteral("items 数量必须为 1..125"));
            return std::nullopt;
        }
        QSet<qsizetype> indexes;
        for (qsizetype i = 0; i < items.size(); ++i) {
            const auto item = parseElement(parser, items.at(i),
                childPath(childPath(path, QStringLiteral("items")), QString::number(i)));
            if (!item) return std::nullopt;
            if (indexes.contains(item->index)) {
                parser.error(ConfigErrorCode::DuplicateIndex,
                             childPath(childPath(childPath(path, QStringLiteral("items")),
                                                 QString::number(i)), QStringLiteral("index")),
                             QStringLiteral("逐元素断言索引重复"));
                return std::nullopt;
            }
            indexes.insert(item->index);
            result.elements.append(*item);
        }
        result.type = AssertionType::Elements;
        return result;
    }
    if (*typeName == QStringLiteral("uint32")) {
        parser.allowedFields(object, path, {QStringLiteral("type"), QStringLiteral("word_order"),
                                            QStringLiteral("comparison"), QStringLiteral("min"),
                                            QStringLiteral("max"), QStringLiteral("decimal_places"),
                                            QStringLiteral("unit")});
        const auto wordOrder = parser.requiredString(object, QStringLiteral("word_order"), path, 32);
        const auto comparison = parser.requiredString(object, QStringLiteral("comparison"), path, 32);
        const auto minimum = parser.requiredInteger(object, QStringLiteral("min"), path, 0,
                                                     std::numeric_limits<quint32>::max());
        const auto maximum = parser.requiredInteger(object, QStringLiteral("max"), path, 0,
                                                     std::numeric_limits<quint32>::max());
        const auto decimalPlaces = parser.optionalInteger(object, QStringLiteral("decimal_places"),
                                                           path, 0, 6, 0);
        const auto unit = parser.optionalString(object, QStringLiteral("unit"), path, 16);
        if (!wordOrder || !comparison || !minimum || !maximum || !decimalPlaces || !unit) {
            return std::nullopt;
        }
        if (*wordOrder != QStringLiteral("low_word_first")) {
            parser.error(ConfigErrorCode::InvalidCombination,
                         childPath(path, QStringLiteral("word_order")),
                         QStringLiteral("uint32 只接受 low_word_first"));
            return std::nullopt;
        }
        if (*minimum > *maximum) {
            parser.error(ConfigErrorCode::InvalidCombination, childPath(path, QStringLiteral("max")),
                         QStringLiteral("uint32 范围上限不得小于下限"));
            return std::nullopt;
        }
        if (*comparison == QStringLiteral("range")) {
            result.uint32.comparison = UInt32Comparison::Range;
        } else if (*comparison == QStringLiteral("non_decreasing")) {
            result.uint32.comparison = UInt32Comparison::NonDecreasing;
        } else if (*comparison == QStringLiteral("strictly_increasing")) {
            result.uint32.comparison = UInt32Comparison::StrictlyIncreasing;
        } else {
            parser.error(ConfigErrorCode::InvalidCombination,
                         childPath(path, QStringLiteral("comparison")),
                         QStringLiteral("未知 uint32 comparison"));
            return std::nullopt;
        }
        result.type = AssertionType::UInt32;
        result.uint32.minimum = static_cast<quint64>(*minimum);
        result.uint32.maximum = static_cast<quint64>(*maximum);
        result.uint32.decimalPlaces = static_cast<int>(*decimalPlaces);
        result.uint32.unit = *unit;
        return result;
    }
    if (*typeName == QStringLiteral("stability_summary")) {
        parser.allowedFields(object, path, {QStringLiteral("type"), QStringLiteral("min_total"),
                                            QStringLiteral("min_successes"), QStringLiteral("max_failures"),
                                            QStringLiteral("max_timeouts"),
                                            QStringLiteral("max_failure_rate_ppm"),
                                            QStringLiteral("rtt_ms")});
        const auto minTotal = parser.requiredInteger(object, QStringLiteral("min_total"), path, 1,
                                                      maximumStabilitySamples);
        const auto minSuccesses = parser.requiredInteger(object, QStringLiteral("min_successes"), path, 1,
                                                          maximumStabilitySamples);
        const auto maxFailures = parser.requiredInteger(object, QStringLiteral("max_failures"), path, 0,
                                                         maximumStabilitySamples);
        const auto maxTimeouts = parser.requiredInteger(object, QStringLiteral("max_timeouts"), path, 0,
                                                         maximumStabilitySamples);
        const auto maxRate = parser.requiredInteger(object, QStringLiteral("max_failure_rate_ppm"),
                                                     path, 0, failureRateScalePpm);
        const auto rttObject = parser.requiredObject(object, QStringLiteral("rtt_ms"), path);
        if (!minTotal || !minSuccesses || !maxFailures || !maxTimeouts || !maxRate || !rttObject) {
            return std::nullopt;
        }
        const QString rttPath = childPath(path, QStringLiteral("rtt_ms"));
        parser.allowedFields(*rttObject, rttPath,
                             {QStringLiteral("require_valid_samples"),
                              QStringLiteral("minimum_sample"),
                              QStringLiteral("maximum_average"),
                              QStringLiteral("maximum_sample")});
        const auto requireRtt = parser.requiredBoolean(*rttObject,
                                                       QStringLiteral("require_valid_samples"), rttPath);
        const auto minimumRtt = parser.requiredInteger(*rttObject, QStringLiteral("minimum_sample"),
                                                       rttPath, 0, maximumRequestTimeoutMs);
        const auto maximumAverage = parser.requiredInteger(*rttObject, QStringLiteral("maximum_average"),
                                                           rttPath, 0, maximumRequestTimeoutMs);
        const auto maximumRtt = parser.requiredInteger(*rttObject, QStringLiteral("maximum_sample"),
                                                       rttPath, 0, maximumRequestTimeoutMs);
        if (!requireRtt || !minimumRtt || !maximumAverage || !maximumRtt) return std::nullopt;
        if (*minSuccesses > *minTotal) {
            parser.error(ConfigErrorCode::StatisticsConflict,
                         childPath(path, QStringLiteral("min_successes")),
                         QStringLiteral("min_successes 不得大于 min_total"));
            return std::nullopt;
        }
        if (*minimumRtt > *maximumAverage || *maximumAverage > *maximumRtt) {
            parser.error(ConfigErrorCode::StatisticsConflict, rttPath,
                         QStringLiteral("RTT 边界必须满足 minimum_sample <= maximum_average <= maximum_sample"));
            return std::nullopt;
        }
        result.type = AssertionType::StabilitySummary;
        result.stability.minimumTotal = static_cast<quint64>(*minTotal);
        result.stability.minimumSuccesses = static_cast<quint64>(*minSuccesses);
        result.stability.maximumFailures = static_cast<quint64>(*maxFailures);
        result.stability.maximumTimeouts = static_cast<quint64>(*maxTimeouts);
        result.stability.maximumFailureRatePpm = static_cast<quint32>(*maxRate);
        result.stability.requireValidRttSamples = *requireRtt;
        result.stability.minimumRttMs = *minimumRtt;
        result.stability.maximumAverageRttMs = *maximumAverage;
        result.stability.maximumRttMs = *maximumRtt;
        return result;
    }

    const auto representation = parseRepresentation(parser, object, path);
    if (!representation) return std::nullopt;
    result.representation = *representation;
    const qint64 minimum = *representation == ValueRepresentation::Int16 ? -32768 : 0;
    const qint64 maximum = *representation == ValueRepresentation::Int16 ? 32767 : 65535;
    const auto unit = parser.optionalString(object, QStringLiteral("unit"), path, 16);
    if (!unit) return std::nullopt;
    result.unit = *unit;
    if (*typeName == QStringLiteral("equals")) {
        parser.allowedFields(object, path, {QStringLiteral("type"), QStringLiteral("value"),
                                            QStringLiteral("representation"), QStringLiteral("unit")});
        const auto expected = parser.requiredInteger(object, QStringLiteral("value"), path,
                                                      minimum, maximum);
        if (!expected) return std::nullopt;
        result.type = AssertionType::Equals;
        result.value = *expected;
        return result;
    }
    if (*typeName == QStringLiteral("range")) {
        parser.allowedFields(object, path, {QStringLiteral("type"), QStringLiteral("min"),
                                            QStringLiteral("max"), QStringLiteral("representation"),
                                            QStringLiteral("unit")});
        const auto minValue = parser.requiredInteger(object, QStringLiteral("min"), path,
                                                      minimum, maximum);
        const auto maxValue = parser.requiredInteger(object, QStringLiteral("max"), path,
                                                      minimum, maximum);
        if (!minValue || !maxValue) return std::nullopt;
        if (*minValue > *maxValue) {
            parser.error(ConfigErrorCode::InvalidCombination, childPath(path, QStringLiteral("max")),
                         QStringLiteral("范围上限不得小于下限"));
            return std::nullopt;
        }
        result.type = AssertionType::Range;
        result.minimum = *minValue;
        result.maximum = *maxValue;
        return result;
    }
    if (*typeName == QStringLiteral("register_sequence")) {
        parser.allowedFields(object, path, {QStringLiteral("type"), QStringLiteral("values"),
                                            QStringLiteral("representation"), QStringLiteral("unit")});
        if (!object.contains(QStringLiteral("values"))
            || !object.value(QStringLiteral("values")).isArray()) {
            parser.error(object.contains(QStringLiteral("values")) ? ConfigErrorCode::WrongType
                                                                    : ConfigErrorCode::MissingField,
                         childPath(path, QStringLiteral("values")),
                         QStringLiteral("values 必须为数组"));
            return std::nullopt;
        }
        const QJsonArray values = object.value(QStringLiteral("values")).toArray();
        if (values.isEmpty() || values.size() > maximumReadRegisterCount) {
            parser.error(ConfigErrorCode::OutOfRange, childPath(path, QStringLiteral("values")),
                         QStringLiteral("寄存器序列长度必须为 1..125"));
            return std::nullopt;
        }
        for (qsizetype i = 0; i < values.size(); ++i) {
            const auto expected = parser.integerValue(values.at(i),
                childPath(childPath(path, QStringLiteral("values")), QString::number(i)),
                minimum, maximum);
            if (!expected) return std::nullopt;
            result.values.append(*expected);
        }
        result.type = AssertionType::RegisterSequence;
        return result;
    }
    if (*typeName == QStringLiteral("bitmask")) {
        parser.allowedFields(object, path, {QStringLiteral("type"), QStringLiteral("mask"),
                                            QStringLiteral("value"), QStringLiteral("unit")});
        if (object.contains(QStringLiteral("representation"))) {
            parser.error(ConfigErrorCode::UnknownField,
                         childPath(path, QStringLiteral("representation")),
                         QStringLiteral("bitmask 不支持 representation"));
            return std::nullopt;
        }
        const auto mask = parser.requiredInteger(object, QStringLiteral("mask"), path, 0, 65535);
        const auto expected = parser.requiredInteger(object, QStringLiteral("value"), path, 0, 65535);
        if (!mask || !expected) return std::nullopt;
        if ((static_cast<quint16>(*expected) & ~static_cast<quint16>(*mask)) != 0) {
            parser.error(ConfigErrorCode::InvalidCombination, childPath(path, QStringLiteral("value")),
                         QStringLiteral("bitmask 期望值不得包含 mask 之外的位"));
            return std::nullopt;
        }
        result.type = AssertionType::BitMask;
        result.representation = ValueRepresentation::UInt16;
        result.mask = static_cast<quint16>(*mask);
        result.value = *expected;
        return result;
    }
    parser.error(ConfigErrorCode::UnknownAssertionType, childPath(path, QStringLiteral("type")),
                 QStringLiteral("未知断言类型“%1”").arg(*typeName));
    return std::nullopt;
}

std::optional<RetryPolicy> parseRetry(Parser &parser,
                                      const QJsonObject &container,
                                      const QString &path)
{
    if (!container.contains(QStringLiteral("retry"))) return RetryPolicy{};
    const QString retryPath = childPath(path, QStringLiteral("retry"));
    const auto retryObject = parser.requiredObject(container, QStringLiteral("retry"), path);
    if (!retryObject) return std::nullopt;
    parser.allowedFields(*retryObject, retryPath,
                         {QStringLiteral("max_retries"), QStringLiteral("on_errors")});
    const auto maximum = parser.requiredInteger(*retryObject, QStringLiteral("max_retries"),
                                                retryPath, 1, maximumRetryCount);
    if (!retryObject->contains(QStringLiteral("on_errors"))
        || !retryObject->value(QStringLiteral("on_errors")).isArray()) {
        parser.error(retryObject->contains(QStringLiteral("on_errors"))
                         ? ConfigErrorCode::WrongType : ConfigErrorCode::MissingField,
                     childPath(retryPath, QStringLiteral("on_errors")),
                     QStringLiteral("on_errors 必须为数组"));
        return std::nullopt;
    }
    const QJsonArray array = retryObject->value(QStringLiteral("on_errors")).toArray();
    if (array.isEmpty() || array.size() > 5) {
        parser.error(ConfigErrorCode::OutOfRange,
                     childPath(retryPath, QStringLiteral("on_errors")),
                     QStringLiteral("on_errors 必须包含 1..5 项"));
        return std::nullopt;
    }
    RetryPolicy result;
    if (maximum) result.maxRetries = static_cast<int>(*maximum);
    QSet<QString> seen;
    for (qsizetype i = 0; i < array.size(); ++i) {
        const QString itemPath = childPath(childPath(retryPath, QStringLiteral("on_errors")),
                                           QString::number(i));
        if (!array.at(i).isString()) {
            parser.error(ConfigErrorCode::WrongType, itemPath,
                         QStringLiteral("重试错误类型必须为字符串"));
            return std::nullopt;
        }
        const QString name = array.at(i).toString().trimmed();
        if (seen.contains(name)) {
            parser.error(ConfigErrorCode::InvalidCombination, itemPath,
                         QStringLiteral("重试错误类型重复"));
            return std::nullopt;
        }
        seen.insert(name);
        if (name == QStringLiteral("timeout")) result.onErrors.append(RetryError::Timeout);
        else if (name == QStringLiteral("connection")) result.onErrors.append(RetryError::Connection);
        else if (name == QStringLiteral("serial")) result.onErrors.append(RetryError::Serial);
        else if (name == QStringLiteral("crc")) result.onErrors.append(RetryError::Crc);
        else if (name == QStringLiteral("protocol")) result.onErrors.append(RetryError::Protocol);
        else {
            parser.error(ConfigErrorCode::OutOfRange, itemPath,
                         QStringLiteral("未知重试错误类型“%1”").arg(name));
            return std::nullopt;
        }
    }
    return maximum ? std::optional<RetryPolicy>(result) : std::nullopt;
}

std::optional<TimeoutPolicy> parseTimeout(Parser &parser,
                                          const QJsonObject &container,
                                          const QString &path,
                                          const bool requireCase,
                                          const qint64 maximumCase)
{
    const QString timeoutPath = childPath(path, QStringLiteral("timeout"));
    if (!container.contains(QStringLiteral("timeout"))) {
        if (requireCase) {
            parser.error(ConfigErrorCode::MissingField, timeoutPath,
                         QStringLiteral("复合用例必须显式配置 timeout"));
            return std::nullopt;
        }
        return TimeoutPolicy{};
    }
    const auto timeoutObject = parser.requiredObject(container, QStringLiteral("timeout"), path);
    if (!timeoutObject) return std::nullopt;
    parser.allowedFields(*timeoutObject, timeoutPath,
                         {QStringLiteral("request_ms"), QStringLiteral("case_ms")});
    const auto request = parser.requiredInteger(*timeoutObject, QStringLiteral("request_ms"),
                                                timeoutPath, 1, maximumRequestTimeoutMs);
    std::optional<qint64> caseMs;
    if (timeoutObject->contains(QStringLiteral("case_ms"))) {
        caseMs = parser.requiredInteger(*timeoutObject, QStringLiteral("case_ms"), timeoutPath,
                                        1, maximumCase);
    } else if (requireCase) {
        parser.error(ConfigErrorCode::MissingField, childPath(timeoutPath, QStringLiteral("case_ms")),
                     QStringLiteral("复合用例必须显式配置 case_ms"));
    } else if (request) {
        caseMs = request;
    }
    if (!request || !caseMs) return std::nullopt;
    if (*caseMs < *request) {
        parser.error(ConfigErrorCode::InvalidCombination,
                     childPath(timeoutPath, QStringLiteral("case_ms")),
                     QStringLiteral("用例总预算不得小于单请求超时"));
        return std::nullopt;
    }
    return TimeoutPolicy{std::chrono::milliseconds(*request),
                         std::chrono::milliseconds(*caseMs)};
}

std::optional<FaultInjection> parseFault(Parser &parser,
                                         const QJsonObject &container,
                                         const QString &path)
{
    if (!container.contains(QStringLiteral("fault"))) return FaultInjection::None;
    const auto name = parser.requiredString(container, QStringLiteral("fault"), path, 32);
    if (!name) return std::nullopt;
    if (*name == QStringLiteral("no_response")) return FaultInjection::NoResponse;
    parser.error(ConfigErrorCode::InvalidCombination, childPath(path, QStringLiteral("fault")),
                 QStringLiteral("fault 仅支持 no_response"));
    return std::nullopt;
}

bool validateOperation(Parser &parser,
                       const QString &path,
                       const TestCaseType type,
                       const TestRequest &request,
                       const ExpectedAssertion &expected)
{
    const QString expectedPath = childPath(path, QStringLiteral("expected"));
    if (type == TestCaseType::ExpectException) {
        if (expected.type == AssertionType::ModbusException) return true;
        parser.error(ConfigErrorCode::InvalidCombination, expectedPath,
                     QStringLiteral("expect_exception 只接受 modbus_exception 断言"));
        return false;
    }
    if (type == TestCaseType::ExpectTimeout) {
        if (expected.type == AssertionType::ResponseTimeout) return true;
        parser.error(ConfigErrorCode::InvalidCombination, expectedPath,
                     QStringLiteral("expect_timeout 只接受 response_timeout 断言"));
        return false;
    }
    if (type == TestCaseType::Consistency) {
        if (expected.type == AssertionType::UInt32 && request.count == 2) return true;
        parser.error(ConfigErrorCode::InvalidCombination, expectedPath,
                     QStringLiteral("consistency 要求两个寄存器和 uint32 断言"));
        return false;
    }
    if (type == TestCaseType::Stability) {
        if (expected.type == AssertionType::StabilitySummary) return true;
        parser.error(ConfigErrorCode::InvalidCombination, expectedPath,
                     QStringLiteral("stability 只接受 stability_summary 断言"));
        return false;
    }
    if (expected.type == AssertionType::ModbusException
        || expected.type == AssertionType::ResponseTimeout
        || expected.type == AssertionType::UInt32
        || expected.type == AssertionType::StabilitySummary) {
        parser.error(ConfigErrorCode::InvalidCombination, expectedPath,
                     QStringLiteral("断言类型与用例类型不匹配"));
        return false;
    }
    if (type == TestCaseType::WriteRegister || type == TestCaseType::WriteAndVerify) {
        if (expected.type == AssertionType::Equals
            && expected.representation == ValueRepresentation::UInt16
            && expected.value == request.rawValue) return true;
        parser.error(ConfigErrorCode::InvalidCombination, expectedPath,
                     QStringLiteral("写用例要求 uint16 equals 且期望值等于写入原始值"));
        return false;
    }
    if (expected.type == AssertionType::Elements) {
        if (expected.elements.size() != request.count) {
            parser.error(ConfigErrorCode::InvalidCombination,
                         childPath(expectedPath, QStringLiteral("items")),
                         QStringLiteral("逐元素断言数量必须等于读取数量"));
            return false;
        }
        QSet<qsizetype> indexes;
        for (const auto &item : expected.elements) indexes.insert(item.index);
        for (qsizetype index = 0; index < request.count; ++index) {
            if (!indexes.contains(index)) {
                parser.error(ConfigErrorCode::InvalidCombination,
                             childPath(expectedPath, QStringLiteral("items")),
                             QStringLiteral("逐元素断言必须完整覆盖 0..count-1"));
                return false;
            }
        }
        return true;
    }
    if (request.count > 1 && expected.type != AssertionType::RegisterSequence) {
        parser.error(ConfigErrorCode::InvalidCombination, expectedPath,
                     QStringLiteral("多寄存器读取只接受 register_sequence 或 elements 断言"));
        return false;
    }
    if (expected.type == AssertionType::RegisterSequence
        && expected.values.size() != request.count) {
        parser.error(ConfigErrorCode::InvalidCombination,
                     childPath(expectedPath, QStringLiteral("values")),
                     QStringLiteral("期望序列长度必须等于读取数量"));
        return false;
    }
    return true;
}

std::optional<SequenceStep> parseSequenceStep(Parser &parser,
                                              const QJsonValue &value,
                                              const QString &path,
                                              const ExecutionEnvironment environment)
{
    if (!value.isObject()) {
        parser.error(ConfigErrorCode::WrongType, path, QStringLiteral("sequence step 必须为对象"));
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    parser.allowedFields(object, path, {QStringLiteral("id"), QStringLiteral("type"),
                                        QStringLiteral("delay_before_ms"), QStringLiteral("request"),
                                        QStringLiteral("expected"), QStringLiteral("retry"),
                                        QStringLiteral("fault")});
    const auto id = parser.requiredString(object, QStringLiteral("id"), path, 64, true);
    const auto type = parseCaseType(parser, object, path);
    const auto delay = parser.optionalInteger(object, QStringLiteral("delay_before_ms"), path,
                                              0, maximumStepDelayMs, 0);
    const auto requestObject = parser.requiredObject(object, QStringLiteral("request"), path);
    const auto expectedObject = parser.requiredObject(object, QStringLiteral("expected"), path);
    const auto retry = parseRetry(parser, object, path);
    const auto fault = parseFault(parser, object, path);
    if (!id || !type || !delay || !requestObject || !expectedObject || !retry || !fault) {
        return std::nullopt;
    }
    if (*type != TestCaseType::ReadRegister && *type != TestCaseType::ReadRegisters
        && *type != TestCaseType::ExpectException && *type != TestCaseType::ExpectTimeout) {
        parser.error(ConfigErrorCode::InvalidCombination, childPath(path, QStringLiteral("type")),
                     QStringLiteral("sequence step 只支持读取、期望异常或预期超时"));
        return std::nullopt;
    }
    const auto request = parseRequest(parser, *requestObject,
                                      childPath(path, QStringLiteral("request")), *type);
    const auto expected = parseExpected(parser, *expectedObject,
                                        childPath(path, QStringLiteral("expected")));
    if (!request || !expected || !validateOperation(parser, path, *type, *request, *expected)) {
        return std::nullopt;
    }
    if (*type == TestCaseType::ExpectTimeout) {
        if (environment != ExecutionEnvironment::Fake || *fault != FaultInjection::NoResponse
            || retry->maxRetries != 0) {
            parser.error(ConfigErrorCode::InvalidCombination, path,
                         QStringLiteral("sequence 预期超时步骤要求 fake、no_response 且无 retry"));
            return std::nullopt;
        }
    } else if (*fault != FaultInjection::None) {
        parser.error(ConfigErrorCode::InvalidCombination, childPath(path, QStringLiteral("fault")),
                     QStringLiteral("仅 expect_timeout 可配置 fault"));
        return std::nullopt;
    }
    SequenceStep result;
    result.id = *id;
    result.declaredType = *type;
    result.type = *type == TestCaseType::ReadRegister ? TestCaseType::ReadRegisters : *type;
    result.delayBefore = std::chrono::milliseconds(*delay);
    result.request = *request;
    result.expected = *expected;
    result.retry = *retry;
    result.fault = *fault;
    return result;
}

std::optional<QMap<QString, QString>> parseMetadata(Parser &parser,
                                                    const QJsonObject &root)
{
    if (!root.contains(QStringLiteral("metadata"))) return QMap<QString, QString>{};
    const QString path = QStringLiteral("/metadata");
    if (!root.value(QStringLiteral("metadata")).isObject()) {
        parser.error(ConfigErrorCode::WrongType, path, QStringLiteral("metadata 必须为对象"));
        return std::nullopt;
    }
    const QJsonObject object = root.value(QStringLiteral("metadata")).toObject();
    if (object.size() > maximumMetadataEntries) {
        parser.error(ConfigErrorCode::OutOfRange, path,
                     QStringLiteral("metadata 项数不得超过 32"));
        return std::nullopt;
    }
    QMap<QString, QString> result;
    static const QRegularExpression keyPattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
    bool valid = true;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString itemPath = childPath(path, it.key());
        if (!keyPattern.match(it.key()).hasMatch()) {
            parser.error(ConfigErrorCode::InvalidIdentifier, itemPath,
                         QStringLiteral("metadata 键不是合法标识符"));
            valid = false;
        } else if (!it.value().isString()) {
            parser.error(ConfigErrorCode::WrongType, itemPath,
                         QStringLiteral("metadata 值必须为字符串"));
            valid = false;
        } else {
            const QString text = it.value().toString().trimmed();
            if (text.size() > 256) {
                parser.error(ConfigErrorCode::OutOfRange, itemPath,
                             QStringLiteral("metadata 值长度不得超过 256"));
                valid = false;
            } else {
                result.insert(it.key(), text);
            }
        }
    }
    return valid ? std::optional<QMap<QString, QString>>(result) : std::nullopt;
}

std::optional<TestCase> parseCase(Parser &parser,
                                  const QJsonValue &value,
                                  const QString &path)
{
    if (!value.isObject()) {
        parser.error(ConfigErrorCode::WrongType, path, QStringLiteral("用例必须为对象"));
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    TestCase result;
    const auto id = parser.requiredString(object, QStringLiteral("id"), path, 64, true);
    if (id) {
        result.id = *id;
        parser.setCaseId(*id);
    }
    const auto name = parser.requiredString(object, QStringLiteral("name"), path, 128);
    const auto category = parser.requiredString(object, QStringLiteral("category"), path, 64);
    const auto description = parser.optionalString(object, QStringLiteral("description"), path, 1024);
    const auto environment = parseEnvironment(parser, object, path);
    const auto type = parseCaseType(parser, object, path);
    const auto tags = parser.tags(object, QStringLiteral("tags"), path);
    if (object.contains(QStringLiteral("enabled"))) {
        if (!object.value(QStringLiteral("enabled")).isBool()) {
            parser.error(ConfigErrorCode::WrongType, childPath(path, QStringLiteral("enabled")),
                         QStringLiteral("enabled 必须为布尔值"));
        } else {
            result.enabled = object.value(QStringLiteral("enabled")).toBool();
        }
    }
    if (!id || !name || !category || !description || !environment || !type || !tags) {
        return std::nullopt;
    }
    result.name = *name;
    result.category = *category;
    result.description = *description;
    result.environment = *environment;
    result.declaredType = *type;
    result.type = *type == TestCaseType::ReadRegister ? TestCaseType::ReadRegisters : *type;
    result.tags = *tags;

    if (*type == TestCaseType::Sequence) {
        parser.allowedFields(object, path, {QStringLiteral("id"), QStringLiteral("name"),
            QStringLiteral("category"), QStringLiteral("description"), QStringLiteral("environment"),
            QStringLiteral("type"), QStringLiteral("enabled"), QStringLiteral("tags"),
            QStringLiteral("repeat_count"), QStringLiteral("failure_policy"),
            QStringLiteral("timeout"), QStringLiteral("steps")});
        const auto repeat = parser.optionalInteger(object, QStringLiteral("repeat_count"), path,
                                                   1, maximumSequenceRepeatCount, 1);
        QString policyName = QStringLiteral("stop_on_failure");
        if (object.contains(QStringLiteral("failure_policy"))) {
            const auto parsed = parser.requiredString(object, QStringLiteral("failure_policy"), path, 32);
            if (!parsed) return std::nullopt;
            policyName = *parsed;
        }
        const auto timeout = parseTimeout(parser, object, path, true,
                                          maximumCompositeCaseTimeoutMs);
        if (!repeat || !timeout) return std::nullopt;
        if (policyName == QStringLiteral("stop_on_failure")) {
            result.sequence.failurePolicy = SequenceFailurePolicy::StopOnFailure;
        } else if (policyName == QStringLiteral("continue_on_failure")) {
            result.sequence.failurePolicy = SequenceFailurePolicy::ContinueOnFailure;
        } else {
            parser.error(ConfigErrorCode::InvalidCombination,
                         childPath(path, QStringLiteral("failure_policy")),
                         QStringLiteral("未知 sequence failure_policy"));
            return std::nullopt;
        }
        if (!object.contains(QStringLiteral("steps"))
            || !object.value(QStringLiteral("steps")).isArray()) {
            parser.error(object.contains(QStringLiteral("steps")) ? ConfigErrorCode::WrongType
                                                                   : ConfigErrorCode::MissingField,
                         childPath(path, QStringLiteral("steps")),
                         QStringLiteral("steps 必须为数组"));
            return std::nullopt;
        }
        const QJsonArray steps = object.value(QStringLiteral("steps")).toArray();
        if (steps.isEmpty() || steps.size() > maximumSequenceSteps) {
            parser.error(ConfigErrorCode::OutOfRange, childPath(path, QStringLiteral("steps")),
                         QStringLiteral("sequence 步骤数必须为 1..64"));
            return std::nullopt;
        }
        QSet<QString> ids;
        for (qsizetype i = 0; i < steps.size(); ++i) {
            const QString stepPath = childPath(childPath(path, QStringLiteral("steps")),
                                               QString::number(i));
            const auto step = parseSequenceStep(parser, steps.at(i), stepPath, *environment);
            if (!step) return std::nullopt;
            if (ids.contains(step->id)) {
                parser.error(ConfigErrorCode::DuplicateId,
                             childPath(stepPath, QStringLiteral("id")),
                             QStringLiteral("sequence step ID 重复"));
                return std::nullopt;
            }
            ids.insert(step->id);
            result.sequence.steps.append(*step);
        }
        result.sequence.repeatCount = static_cast<int>(*repeat);
        result.timeout = *timeout;
        return result;
    }

    parser.allowedFields(object, path, {QStringLiteral("id"), QStringLiteral("name"),
        QStringLiteral("category"), QStringLiteral("description"), QStringLiteral("environment"),
        QStringLiteral("type"), QStringLiteral("enabled"), QStringLiteral("tags"),
        QStringLiteral("request"), QStringLiteral("expected"), QStringLiteral("timeout"),
        QStringLiteral("retry"), QStringLiteral("fault"), QStringLiteral("consistency"),
        QStringLiteral("stability")});
    const auto requestObject = parser.requiredObject(object, QStringLiteral("request"), path);
    const auto expectedObject = parser.requiredObject(object, QStringLiteral("expected"), path);
    const bool composite = *type == TestCaseType::WriteAndVerify
        || *type == TestCaseType::ExpectTimeout || *type == TestCaseType::Consistency
        || *type == TestCaseType::Stability;
    const qint64 maximumCase = *type == TestCaseType::Stability
        ? maximumStabilityCaseTimeoutMs
        : composite ? maximumCompositeCaseTimeoutMs : maximumCaseTimeoutMs;
    const auto timeout = parseTimeout(parser, object, path, composite, maximumCase);
    const auto retry = parseRetry(parser, object, path);
    const auto fault = parseFault(parser, object, path);
    if (!requestObject || !expectedObject || !timeout || !retry || !fault) return std::nullopt;
    const auto request = parseRequest(parser, *requestObject,
                                      childPath(path, QStringLiteral("request")), *type);
    const auto expected = parseExpected(parser, *expectedObject,
                                        childPath(path, QStringLiteral("expected")));
    if (!request || !expected || !validateOperation(parser, path, *type, *request, *expected)) {
        return std::nullopt;
    }
    result.request = *request;
    result.expected = *expected;
    result.timeout = *timeout;
    result.retry = *retry;
    result.fault = *fault;

    if (*type == TestCaseType::ExpectTimeout) {
        if (*environment != ExecutionEnvironment::Fake || *fault != FaultInjection::NoResponse
            || retry->maxRetries != 0) {
            parser.error(ConfigErrorCode::InvalidCombination, path,
                         QStringLiteral("expect_timeout 要求 fake、no_response 且无 retry"));
            return std::nullopt;
        }
    } else if (*fault != FaultInjection::None) {
        parser.error(ConfigErrorCode::InvalidCombination, childPath(path, QStringLiteral("fault")),
                     QStringLiteral("仅 expect_timeout 可配置 fault"));
        return std::nullopt;
    }

    if (*type == TestCaseType::Consistency) {
        const auto consistencyObject = parser.requiredObject(object, QStringLiteral("consistency"), path);
        if (!consistencyObject) return std::nullopt;
        const QString consistencyPath = childPath(path, QStringLiteral("consistency"));
        parser.allowedFields(*consistencyObject, consistencyPath,
                             {QStringLiteral("sample_count"), QStringLiteral("interval_ms")});
        const auto samples = parser.requiredInteger(*consistencyObject, QStringLiteral("sample_count"),
                                                    consistencyPath, 1, maximumConsistencySamples);
        const auto interval = parser.requiredInteger(*consistencyObject, QStringLiteral("interval_ms"),
                                                     consistencyPath, 0, maximumStepDelayMs);
        if (!samples || !interval) return std::nullopt;
        if (expected->uint32.comparison != UInt32Comparison::Range && *samples < 2) {
            parser.error(ConfigErrorCode::InvalidCombination,
                         childPath(consistencyPath, QStringLiteral("sample_count")),
                         QStringLiteral("单调性断言至少需要两个样本"));
            return std::nullopt;
        }
        const qint64 attempts = retry->maxRetries + 1;
        const qint64 needed = *samples * timeout->request.count() * attempts
            + (*samples - 1) * *interval;
        if (timeout->testCase.count() < needed) {
            parser.error(ConfigErrorCode::BudgetOverflow,
                         childPath(childPath(path, QStringLiteral("timeout")),
                                   QStringLiteral("case_ms")),
                         QStringLiteral("consistency case_ms 小于理论请求与间隔预算"));
            return std::nullopt;
        }
        result.consistency.sampleCount = static_cast<int>(*samples);
        result.consistency.interval = std::chrono::milliseconds(*interval);
    } else if (object.contains(QStringLiteral("consistency"))) {
        parser.error(ConfigErrorCode::InvalidCombination,
                     childPath(path, QStringLiteral("consistency")),
                     QStringLiteral("仅 consistency 用例可配置 consistency"));
        return std::nullopt;
    }

    if (*type == TestCaseType::Stability) {
        if (retry->maxRetries != 0) {
            parser.error(ConfigErrorCode::InvalidCombination, childPath(path, QStringLiteral("retry")),
                         QStringLiteral("stability 不允许单请求 retry"));
            return std::nullopt;
        }
        const auto stabilityObject = parser.requiredObject(object, QStringLiteral("stability"), path);
        if (!stabilityObject) return std::nullopt;
        const QString stabilityPath = childPath(path, QStringLiteral("stability"));
        parser.allowedFields(*stabilityObject, stabilityPath,
                             {QStringLiteral("duration_ms"), QStringLiteral("interval_ms"),
                              QStringLiteral("minimum_successes"),
                              QStringLiteral("allowed_failure_rate_ppm"),
                              QStringLiteral("evidence_sample_limit")});
        const auto duration = parser.requiredInteger(*stabilityObject, QStringLiteral("duration_ms"),
                                                     stabilityPath, minimumStabilityDurationMs,
                                                     maximumStabilityDurationMs);
        const auto interval = parser.requiredInteger(*stabilityObject, QStringLiteral("interval_ms"),
                                                     stabilityPath, minimumStabilityIntervalMs,
                                                     maximumStabilityIntervalMs);
        const auto minimumSuccesses = parser.requiredInteger(*stabilityObject,
            QStringLiteral("minimum_successes"), stabilityPath, 1, maximumStabilitySamples);
        const auto allowedRate = parser.requiredInteger(*stabilityObject,
            QStringLiteral("allowed_failure_rate_ppm"), stabilityPath, 0, failureRateScalePpm);
        const auto evidenceLimit = parser.optionalInteger(*stabilityObject,
            QStringLiteral("evidence_sample_limit"), stabilityPath,
            minimumEvidenceSampleLimit, maximumEvidenceSampleLimit, defaultEvidenceSampleLimit);
        if (!duration || !interval || !minimumSuccesses || !allowedRate || !evidenceLimit) {
            return std::nullopt;
        }
        const qint64 maximumStarts = (*duration + *interval - 1) / *interval;
        if (*minimumSuccesses > maximumStarts
            || static_cast<qint64>(expected->stability.minimumTotal) > maximumStarts
            || static_cast<qint64>(expected->stability.minimumSuccesses) > maximumStarts) {
            parser.error(ConfigErrorCode::StatisticsConflict, stabilityPath,
                         QStringLiteral("最低样本数超过 duration/interval 的理论最大启动数"));
            return std::nullopt;
        }
        if (timeout->testCase.count() < *duration + timeout->request.count()) {
            parser.error(ConfigErrorCode::BudgetOverflow,
                         childPath(childPath(path, QStringLiteral("timeout")),
                                   QStringLiteral("case_ms")),
                         QStringLiteral("stability case_ms 必须覆盖 duration 与最后请求超时"));
            return std::nullopt;
        }
        if (expected->stability.minimumSuccesses != static_cast<quint64>(*minimumSuccesses)
            || expected->stability.maximumFailureRatePpm != static_cast<quint32>(*allowedRate)) {
            parser.error(ConfigErrorCode::StatisticsConflict,
                         childPath(path, QStringLiteral("expected")),
                         QStringLiteral("稳定性配置与汇总期望的最低成功数/失败率必须一致"));
            return std::nullopt;
        }
        result.stability.duration = std::chrono::milliseconds(*duration);
        result.stability.interval = std::chrono::milliseconds(*interval);
        result.stability.minimumSuccesses = static_cast<quint64>(*minimumSuccesses);
        result.stability.allowedFailureRatePpm = static_cast<quint32>(*allowedRate);
        result.stability.evidenceSampleLimit = static_cast<int>(*evidenceLimit);
    } else if (object.contains(QStringLiteral("stability"))) {
        parser.error(ConfigErrorCode::InvalidCombination,
                     childPath(path, QStringLiteral("stability")),
                     QStringLiteral("仅 stability 用例可配置 stability"));
        return std::nullopt;
    }

    return result;
}

} // namespace

LoadResult loadTestSuiteV2(const QJsonObject &root)
{
    LoadResult result;
    Parser parser(result.errors);
    const auto id = parser.requiredString(root, QStringLiteral("id"), QString{}, 64, true);
    if (id) parser.setSuiteId(*id);
    parser.allowedFields(root, QString{}, {QStringLiteral("schema_version"),
                                           QStringLiteral("id"),
                                           QStringLiteral("name"),
                                           QStringLiteral("description"),
                                           QStringLiteral("tags"),
                                           QStringLiteral("metadata"),
                                           QStringLiteral("cases")});
    const auto version = parser.requiredInteger(root, QStringLiteral("schema_version"), QString{},
                                                testSuiteSchemaVersionV2,
                                                testSuiteSchemaVersionV2);
    const auto name = parser.requiredString(root, QStringLiteral("name"), QString{}, 128);
    const auto description = parser.optionalString(root, QStringLiteral("description"), QString{}, 1024);
    const auto tags = parser.tags(root, QStringLiteral("tags"), QString{});
    const auto metadata = parseMetadata(parser, root);

    QVector<TestCase> cases;
    if (!root.contains(QStringLiteral("cases"))) {
        parser.error(ConfigErrorCode::MissingField, QStringLiteral("/cases"),
                     QStringLiteral("缺少 cases 数组"));
    } else if (!root.value(QStringLiteral("cases")).isArray()) {
        parser.error(ConfigErrorCode::WrongType, QStringLiteral("/cases"),
                     QStringLiteral("cases 必须为数组"));
    } else {
        const QJsonArray array = root.value(QStringLiteral("cases")).toArray();
        if (array.size() > maximumSuiteCases) {
            parser.error(ConfigErrorCode::OutOfRange, QStringLiteral("/cases"),
                         QStringLiteral("用例数量不得超过 1000"));
        } else {
            QSet<QString> ids;
            for (qsizetype index = 0; index < array.size(); ++index) {
                parser.clearCaseId();
                const QString path = QStringLiteral("/cases/%1").arg(index);
                const auto testCase = parseCase(parser, array.at(index), path);
                if (!testCase) continue;
                if (ids.contains(testCase->id)) {
                    parser.setCaseId(testCase->id);
                    parser.error(ConfigErrorCode::DuplicateId, childPath(path, QStringLiteral("id")),
                                 QStringLiteral("用例 ID“%1”重复").arg(testCase->id));
                    continue;
                }
                ids.insert(testCase->id);
                cases.append(*testCase);
            }
        }
    }

    if (!result.errors.isEmpty() || !version || !id || !name || !description
        || !tags || !metadata) return result;
    TestSuite suite;
    suite.schemaVersion = testSuiteSchemaVersionV2;
    suite.id = *id;
    suite.name = *name;
    suite.description = *description;
    suite.tags = *tags;
    suite.metadata = *metadata;
    suite.cases = std::move(cases);
    result.suite = std::move(suite);
    return result;
}

} // namespace oms555tv::testing

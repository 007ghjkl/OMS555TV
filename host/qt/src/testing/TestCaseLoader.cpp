#include "testing/TestCaseLoader.h"
#include "testing/TestCaseLoaderV2.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
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
        QSet<QString> allowedSet(allowed.begin(), allowed.end());
        bool valid = true;
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (allowedSet.contains(it.key())) {
                continue;
            }
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
        if (!object.contains(field)) {
            return QString{};
        }
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
        if (!object.contains(field)) {
            return QStringList{};
        }
        const QString fieldPath = childPath(path, field);
        if (!object.value(field).isArray()) {
            error(ConfigErrorCode::WrongType, fieldPath,
                  QStringLiteral("标签必须为数组"));
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
            if (!tag.has_value()) {
                valid = false;
                continue;
            }
            if (seen.contains(*tag)) {
                error(ConfigErrorCode::DuplicateTag, itemPath,
                      QStringLiteral("标签“%1”重复").arg(*tag));
                valid = false;
                continue;
            }
            seen.insert(*tag);
            result.append(*tag);
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

std::optional<TestCaseType> parseCaseType(Parser &parser,
                                          const QJsonObject &object,
                                          const QString &path)
{
    const auto value = parser.requiredString(object, QStringLiteral("type"), path, 32);
    if (!value.has_value()) {
        return std::nullopt;
    }
    if (*value == QStringLiteral("read_register")) return TestCaseType::ReadRegister;
    if (*value == QStringLiteral("read_registers")) return TestCaseType::ReadRegisters;
    if (*value == QStringLiteral("write_register")) return TestCaseType::WriteRegister;
    if (*value == QStringLiteral("write_and_verify")) return TestCaseType::WriteAndVerify;
    if (*value == QStringLiteral("expect_exception")) return TestCaseType::ExpectException;
    parser.error(ConfigErrorCode::UnknownCaseType, childPath(path, QStringLiteral("type")),
                 QStringLiteral("未知用例类型“%1”").arg(*value));
    return std::nullopt;
}

bool addressRangeValid(Parser &parser,
                       const QString &path,
                       const qint64 address,
                       const qint64 count)
{
    const quint32 end = static_cast<quint32>(address)
        + static_cast<quint32>(count) - 1U;
    if (end <= std::numeric_limits<quint16>::max()) {
        return true;
    }
    parser.error(ConfigErrorCode::AddressRangeOverflow, path,
                 QStringLiteral("PDU 地址范围末端超过 65535"));
    return false;
}

std::optional<TestRequest> parseRequest(Parser &parser,
                                        const QJsonObject &object,
                                        const QString &path,
                                        const TestCaseType type)
{
    TestRequest request;
    if (type == TestCaseType::ReadRegister || type == TestCaseType::ReadRegisters) {
        parser.allowedFields(object, path, {QStringLiteral("function"),
                                            QStringLiteral("address"),
                                            QStringLiteral("count")});
        const auto function = parser.requiredInteger(object, QStringLiteral("function"), path, 3, 3);
        const auto address = parser.requiredInteger(object, QStringLiteral("address"), path, 0, 65535);
        const qint64 minimumCount = type == TestCaseType::ReadRegister ? 1 : 1;
        const qint64 maximumCount = type == TestCaseType::ReadRegister ? 1 : maximumReadRegisterCount;
        const auto count = parser.requiredInteger(object, QStringLiteral("count"), path,
                                                   minimumCount, maximumCount);
        if (!function || !address || !count
            || !addressRangeValid(parser, path, *address, *count)) {
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
        if (!function || !verify || !address || !value || !preRead || !restore) {
            return std::nullopt;
        }
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

    const auto function = parser.requiredInteger(object, QStringLiteral("function"), path, 3, 6);
    if (!function.has_value()) {
        parser.allowedFields(object, path, {QStringLiteral("function"),
                                            QStringLiteral("address"),
                                            QStringLiteral("count"),
                                            QStringLiteral("value")});
        return std::nullopt;
    }
    if (*function == 3) {
        parser.allowedFields(object, path, {QStringLiteral("function"),
                                            QStringLiteral("address"),
                                            QStringLiteral("count")});
        const auto address = parser.requiredInteger(object, QStringLiteral("address"), path, 0, 65535);
        const auto count = parser.requiredInteger(object, QStringLiteral("count"), path, 1,
                                                   maximumReadRegisterCount);
        if (!address || !count || !addressRangeValid(parser, path, *address, *count)) {
            return std::nullopt;
        }
        request.function = ModbusFunction::ReadHoldingRegisters;
        request.address = device::PduAddress(static_cast<quint16>(*address));
        request.count = static_cast<quint16>(*count);
        return request;
    }
    if (*function == 6) {
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
    parser.error(ConfigErrorCode::OutOfRange, childPath(path, QStringLiteral("function")),
                 QStringLiteral("expect_exception 仅支持功能码 3 或 6"));
    return std::nullopt;
}

std::optional<ValueRepresentation> parseRepresentation(Parser &parser,
                                                        const QJsonObject &object,
                                                        const QString &path)
{
    if (!object.contains(QStringLiteral("representation"))) {
        return ValueRepresentation::UInt16;
    }
    const auto value = parser.requiredString(object, QStringLiteral("representation"), path, 16);
    if (!value) return std::nullopt;
    if (*value == QStringLiteral("uint16")) return ValueRepresentation::UInt16;
    if (*value == QStringLiteral("int16")) return ValueRepresentation::Int16;
    parser.error(ConfigErrorCode::OutOfRange,
                 childPath(path, QStringLiteral("representation")),
                 QStringLiteral("representation 仅支持 uint16 或 int16"));
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

    const auto representation = parseRepresentation(parser, object, path);
    if (!representation) return std::nullopt;
    result.representation = *representation;
    const qint64 minimum = *representation == ValueRepresentation::Int16 ? -32768 : 0;
    const qint64 maximum = *representation == ValueRepresentation::Int16 ? 32767 : 65535;
    const auto unit = parser.optionalString(object, QStringLiteral("unit"), path, 16);
    if (!unit) return std::nullopt;
    result.unit = *unit;

    if (*typeName == QStringLiteral("equals")) {
        parser.allowedFields(object, path, {QStringLiteral("type"),
                                            QStringLiteral("value"),
                                            QStringLiteral("representation"),
                                            QStringLiteral("unit")});
        const auto value = parser.requiredInteger(object, QStringLiteral("value"), path, minimum, maximum);
        if (!value) return std::nullopt;
        result.type = AssertionType::Equals;
        result.value = *value;
        return result;
    }
    if (*typeName == QStringLiteral("range")) {
        parser.allowedFields(object, path, {QStringLiteral("type"),
                                            QStringLiteral("min"),
                                            QStringLiteral("max"),
                                            QStringLiteral("representation"),
                                            QStringLiteral("unit")});
        const auto minValue = parser.requiredInteger(object, QStringLiteral("min"), path, minimum, maximum);
        const auto maxValue = parser.requiredInteger(object, QStringLiteral("max"), path, minimum, maximum);
        if (!minValue || !maxValue) return std::nullopt;
        if (*minValue > *maxValue) {
            parser.error(ConfigErrorCode::InvalidCombination,
                         childPath(path, QStringLiteral("max")),
                         QStringLiteral("范围上限不得小于下限"));
            return std::nullopt;
        }
        result.type = AssertionType::Range;
        result.minimum = *minValue;
        result.maximum = *maxValue;
        return result;
    }
    if (*typeName == QStringLiteral("register_sequence")) {
        parser.allowedFields(object, path, {QStringLiteral("type"),
                                            QStringLiteral("values"),
                                            QStringLiteral("representation"),
                                            QStringLiteral("unit")});
        if (!object.contains(QStringLiteral("values"))) {
            parser.error(ConfigErrorCode::MissingField, childPath(path, QStringLiteral("values")),
                         QStringLiteral("缺少寄存器序列"));
            return std::nullopt;
        }
        if (!object.value(QStringLiteral("values")).isArray()) {
            parser.error(ConfigErrorCode::WrongType, childPath(path, QStringLiteral("values")),
                         QStringLiteral("values 必须为数组"));
            return std::nullopt;
        }
        const QJsonArray values = object.value(QStringLiteral("values")).toArray();
        if (values.isEmpty() || values.size() > maximumReadRegisterCount) {
            parser.error(ConfigErrorCode::OutOfRange, childPath(path, QStringLiteral("values")),
                         QStringLiteral("寄存器序列长度必须为 1..125"));
            return std::nullopt;
        }
        for (qsizetype index = 0; index < values.size(); ++index) {
            const auto value = parser.integerValue(values.at(index),
                childPath(childPath(path, QStringLiteral("values")), QString::number(index)),
                minimum, maximum);
            if (!value) return std::nullopt;
            result.values.append(*value);
        }
        result.type = AssertionType::RegisterSequence;
        return result;
    }
    if (*typeName == QStringLiteral("bitmask")) {
        parser.allowedFields(object, path, {QStringLiteral("type"),
                                            QStringLiteral("mask"),
                                            QStringLiteral("value"),
                                            QStringLiteral("unit")});
        if (object.contains(QStringLiteral("representation"))) {
            parser.error(ConfigErrorCode::UnknownField,
                         childPath(path, QStringLiteral("representation")),
                         QStringLiteral("bitmask 不支持 representation"));
            return std::nullopt;
        }
        const auto mask = parser.requiredInteger(object, QStringLiteral("mask"), path, 0, 65535);
        const auto value = parser.requiredInteger(object, QStringLiteral("value"), path, 0, 65535);
        if (!mask || !value) return std::nullopt;
        if ((static_cast<quint16>(*value) & ~static_cast<quint16>(*mask)) != 0) {
            parser.error(ConfigErrorCode::InvalidCombination,
                         childPath(path, QStringLiteral("value")),
                         QStringLiteral("bitmask 期望值不得包含 mask 之外的位"));
            return std::nullopt;
        }
        result.type = AssertionType::BitMask;
        result.representation = ValueRepresentation::UInt16;
        result.mask = static_cast<quint16>(*mask);
        result.value = *value;
        return result;
    }

    parser.error(ConfigErrorCode::UnknownAssertionType,
                 childPath(path, QStringLiteral("type")),
                 QStringLiteral("未知断言类型“%1”").arg(*typeName));
    return std::nullopt;
}

std::optional<TimeoutPolicy> parseTimeout(Parser &parser,
                                          const QJsonObject &testCase,
                                          const QString &path,
                                          const TestCaseType type)
{
    const QString timeoutPath = childPath(path, QStringLiteral("timeout"));
    if (!testCase.contains(QStringLiteral("timeout"))) {
        if (type == TestCaseType::WriteAndVerify) {
            parser.error(ConfigErrorCode::MissingField, timeoutPath,
                         QStringLiteral("write_and_verify 必须显式配置 timeout"));
            return std::nullopt;
        }
        return TimeoutPolicy{};
    }
    if (!testCase.value(QStringLiteral("timeout")).isObject()) {
        parser.error(ConfigErrorCode::WrongType, timeoutPath,
                     QStringLiteral("timeout 必须为对象"));
        return std::nullopt;
    }
    const QJsonObject object = testCase.value(QStringLiteral("timeout")).toObject();
    parser.allowedFields(object, timeoutPath, {QStringLiteral("request_ms"),
                                               QStringLiteral("case_ms")});
    const auto request = parser.requiredInteger(object, QStringLiteral("request_ms"), timeoutPath,
                                                1, maximumRequestTimeoutMs);
    std::optional<qint64> testCaseMs;
    if (object.contains(QStringLiteral("case_ms"))) {
        testCaseMs = parser.requiredInteger(object, QStringLiteral("case_ms"), timeoutPath,
                                            1, maximumCaseTimeoutMs);
    } else if (type == TestCaseType::WriteAndVerify) {
        parser.error(ConfigErrorCode::MissingField, childPath(timeoutPath, QStringLiteral("case_ms")),
                     QStringLiteral("write_and_verify 必须显式配置 case_ms"));
    } else if (request) {
        testCaseMs = request;
    }
    if (!request || !testCaseMs) return std::nullopt;
    if (*testCaseMs < *request) {
        parser.error(ConfigErrorCode::InvalidCombination,
                     childPath(timeoutPath, QStringLiteral("case_ms")),
                     QStringLiteral("用例总预算不得小于单请求超时"));
        return std::nullopt;
    }
    return TimeoutPolicy{std::chrono::milliseconds(*request),
                         std::chrono::milliseconds(*testCaseMs)};
}

std::optional<RetryPolicy> parseRetry(Parser &parser,
                                      const QJsonObject &testCase,
                                      const QString &path)
{
    if (!testCase.contains(QStringLiteral("retry"))) return RetryPolicy{};
    const QString retryPath = childPath(path, QStringLiteral("retry"));
    if (!testCase.value(QStringLiteral("retry")).isObject()) {
        parser.error(ConfigErrorCode::WrongType, retryPath,
                     QStringLiteral("retry 必须为对象"));
        return std::nullopt;
    }
    const QJsonObject object = testCase.value(QStringLiteral("retry")).toObject();
    parser.allowedFields(object, retryPath, {QStringLiteral("max_retries"),
                                             QStringLiteral("on_errors")});
    const auto maximum = parser.requiredInteger(object, QStringLiteral("max_retries"), retryPath,
                                                1, maximumRetryCount);
    if (!object.contains(QStringLiteral("on_errors"))) {
        parser.error(ConfigErrorCode::MissingField, childPath(retryPath, QStringLiteral("on_errors")),
                     QStringLiteral("retry 缺少 on_errors"));
        return std::nullopt;
    }
    if (!object.value(QStringLiteral("on_errors")).isArray()) {
        parser.error(ConfigErrorCode::WrongType, childPath(retryPath, QStringLiteral("on_errors")),
                     QStringLiteral("on_errors 必须为数组"));
        return std::nullopt;
    }
    const QJsonArray array = object.value(QStringLiteral("on_errors")).toArray();
    if (array.isEmpty() || array.size() > 5) {
        parser.error(ConfigErrorCode::OutOfRange, childPath(retryPath, QStringLiteral("on_errors")),
                     QStringLiteral("on_errors 必须包含 1..5 项"));
        return std::nullopt;
    }
    RetryPolicy result;
    if (maximum) result.maxRetries = static_cast<int>(*maximum);
    QSet<QString> seen;
    for (qsizetype index = 0; index < array.size(); ++index) {
        const QString itemPath = childPath(childPath(retryPath, QStringLiteral("on_errors")),
                                           QString::number(index));
        if (!array.at(index).isString()) {
            parser.error(ConfigErrorCode::WrongType, itemPath,
                         QStringLiteral("重试错误类型必须为字符串"));
            return std::nullopt;
        }
        const QString name = array.at(index).toString().trimmed();
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

bool validateCombination(Parser &parser,
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
    if (expected.type == AssertionType::ModbusException) {
        parser.error(ConfigErrorCode::InvalidCombination, expectedPath,
                     QStringLiteral("仅 expect_exception 可使用 modbus_exception 断言"));
        return false;
    }
    if (type == TestCaseType::WriteRegister || type == TestCaseType::WriteAndVerify) {
        if (expected.type != AssertionType::Equals
            || expected.representation != ValueRepresentation::UInt16
            || expected.value != request.rawValue) {
            parser.error(ConfigErrorCode::InvalidCombination, expectedPath,
                         QStringLiteral("写用例要求 uint16 equals 且期望值等于写入原始值"));
            return false;
        }
        return true;
    }
    if (request.count > 1 && expected.type != AssertionType::RegisterSequence) {
        parser.error(ConfigErrorCode::InvalidCombination, expectedPath,
                     QStringLiteral("多寄存器读取只接受 register_sequence 断言"));
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
    parser.allowedFields(object, path, {QStringLiteral("id"),
                                        QStringLiteral("name"),
                                        QStringLiteral("category"),
                                        QStringLiteral("description"),
                                        QStringLiteral("type"),
                                        QStringLiteral("enabled"),
                                        QStringLiteral("tags"),
                                        QStringLiteral("request"),
                                        QStringLiteral("expected"),
                                        QStringLiteral("timeout"),
                                        QStringLiteral("retry")});
    const auto name = parser.requiredString(object, QStringLiteral("name"), path, 128);
    const auto category = parser.requiredString(object, QStringLiteral("category"), path, 64);
    const auto description = parser.optionalString(object, QStringLiteral("description"), path, 1024);
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
    if (!name || !category || !description || !type || !tags) return std::nullopt;
    result.name = *name;
    result.category = *category;
    result.description = *description;
    result.declaredType = *type;
    result.type = *type == TestCaseType::ReadRegister ? TestCaseType::ReadRegisters : *type;
    result.tags = *tags;

    const auto requestObject = parser.requiredObject(object, QStringLiteral("request"), path);
    const auto expectedObject = parser.requiredObject(object, QStringLiteral("expected"), path);
    const auto timeout = parseTimeout(parser, object, path, *type);
    const auto retry = parseRetry(parser, object, path);
    if (!requestObject || !expectedObject || !timeout || !retry) return std::nullopt;
    const auto request = parseRequest(parser, *requestObject,
                                      childPath(path, QStringLiteral("request")), *type);
    const auto expected = parseExpected(parser, *expectedObject,
                                        childPath(path, QStringLiteral("expected")));
    if (!request || !expected
        || !validateCombination(parser, path, *type, *request, *expected)) {
        return std::nullopt;
    }
    result.request = *request;
    result.expected = *expected;
    result.timeout = *timeout;
    result.retry = *retry;
    return id ? std::optional<TestCase>(result) : std::nullopt;
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
            continue;
        }
        if (!it.value().isString()) {
            parser.error(ConfigErrorCode::WrongType, itemPath,
                         QStringLiteral("metadata 值必须为字符串"));
            valid = false;
            continue;
        }
        const QString text = it.value().toString().trimmed();
        if (text.size() > 256) {
            parser.error(ConfigErrorCode::OutOfRange, itemPath,
                         QStringLiteral("metadata 值长度不得超过 256"));
            valid = false;
            continue;
        }
        result.insert(it.key(), text);
    }
    return valid ? std::optional<QMap<QString, QString>>(result) : std::nullopt;
}

} // namespace

LoadResult TestCaseLoader::load(const QByteArray &utf8Json)
{
    LoadResult result;
    if (utf8Json.startsWith(QByteArray::fromHex("efbbbf"))) {
        result.errors.append({ConfigErrorCode::InvalidUtf8, QStringLiteral("/"), {}, {},
                              QStringLiteral("JSON 不允许 UTF-8 BOM")});
        return result;
    }
    const QString decoded = QString::fromUtf8(utf8Json);
    if (decoded.toUtf8() != utf8Json) {
        result.errors.append({ConfigErrorCode::InvalidUtf8, QStringLiteral("/"), {}, {},
                              QStringLiteral("JSON 包含非法 UTF-8 字节")});
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(utf8Json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        result.errors.append({ConfigErrorCode::JsonSyntax, QStringLiteral("/"), {}, {},
                              QStringLiteral("JSON 语法错误（字节 %1）：%2")
                                  .arg(parseError.offset).arg(parseError.errorString())});
        return result;
    }
    if (!document.isObject()) {
        result.errors.append({ConfigErrorCode::RootNotObject, QStringLiteral("/"), {}, {},
                              QStringLiteral("JSON 顶层必须为对象")});
        return result;
    }

    Parser parser(result.errors);
    const QJsonObject root = document.object();
    const QJsonValue rawVersion = root.value(QStringLiteral("schema_version"));
    if (rawVersion.isDouble() && std::floor(rawVersion.toDouble()) == rawVersion.toDouble()
        && rawVersion.toInt() == testSuiteSchemaVersionV2) {
        return loadTestSuiteV2(root);
    }
    const auto id = parser.requiredString(root, QStringLiteral("id"), QString{}, 64, true);
    if (id) parser.setSuiteId(*id);
    parser.allowedFields(root, QString{}, {QStringLiteral("schema_version"),
                                           QStringLiteral("id"),
                                           QStringLiteral("name"),
                                           QStringLiteral("description"),
                                           QStringLiteral("tags"),
                                           QStringLiteral("metadata"),
                                           QStringLiteral("cases")});
    const auto version = parser.requiredInteger(root, QStringLiteral("schema_version"),
                                                QString{},
                                                std::numeric_limits<int>::min(),
                                                std::numeric_limits<int>::max());
    if (version && *version != testSuiteSchemaVersion) {
        parser.error(ConfigErrorCode::UnknownSchemaVersion, QStringLiteral("/schema_version"),
                     QStringLiteral("不支持 schema_version=%1").arg(*version));
    }
    const auto name = parser.requiredString(root, QStringLiteral("name"), QString{}, 128);
    const auto description = parser.optionalString(root, QStringLiteral("description"),
                                                   QString{}, 1024);
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
        || !tags || !metadata) {
        return result;
    }
    TestSuite suite;
    suite.schemaVersion = static_cast<int>(*version);
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

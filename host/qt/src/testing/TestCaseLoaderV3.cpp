#include "testing/TestCaseLoaderV3.h"

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

QString childPath(const QString &path, const QString &field)
{
    return path + QLatin1Char('/') + field;
}

class Parser final
{
public:
    explicit Parser(QVector<ConfigError> &errors) : errors_(errors) {}

    void setSuiteId(QString id) { suiteId_ = std::move(id); }
    void setCaseId(QString id) { caseId_ = std::move(id); }
    void clearCaseId() { caseId_.clear(); }

    void error(ConfigErrorCode code, const QString &path, const QString &diagnostic)
    {
        errors_.append({code, path, suiteId_, caseId_, diagnostic});
    }

    bool allowedFields(const QJsonObject &object, const QString &path,
                       std::initializer_list<QString> allowed)
    {
        const QSet<QString> names(allowed.begin(), allowed.end());
        bool valid = true;
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (names.contains(it.key())) continue;
            error(ConfigErrorCode::UnknownField, childPath(path, it.key()),
                  QStringLiteral("不支持字段“%1”").arg(it.key()));
            valid = false;
        }
        return valid;
    }

    std::optional<QString> requiredString(const QJsonObject &object, const QString &field,
                                          const QString &path, qsizetype maximumLength,
                                          bool identifier = false)
    {
        if (!object.contains(field)) {
            error(ConfigErrorCode::MissingField, childPath(path, field),
                  QStringLiteral("缺少必填字符串字段“%1”").arg(field));
            return std::nullopt;
        }
        return stringValue(object.value(field), childPath(path, field), maximumLength,
                           false, identifier);
    }

    std::optional<QString> optionalString(const QJsonObject &object, const QString &field,
                                          const QString &path, qsizetype maximumLength)
    {
        if (!object.contains(field)) return QString{};
        return stringValue(object.value(field), childPath(path, field), maximumLength, true, false);
    }

    std::optional<qint64> requiredInteger(const QJsonObject &object, const QString &field,
                                          const QString &path, qint64 minimum, qint64 maximum)
    {
        if (!object.contains(field)) {
            error(ConfigErrorCode::MissingField, childPath(path, field),
                  QStringLiteral("缺少必填整数字段“%1”").arg(field));
            return std::nullopt;
        }
        const auto value = object.value(field);
        if (!value.isDouble() || !std::isfinite(value.toDouble())
            || std::floor(value.toDouble()) != value.toDouble()) {
            error(ConfigErrorCode::WrongType, childPath(path, field),
                  QStringLiteral("字段“%1”必须为整数").arg(field));
            return std::nullopt;
        }
        const double number = value.toDouble();
        if (number < static_cast<double>(minimum) || number > static_cast<double>(maximum)) {
            error(ConfigErrorCode::OutOfRange, childPath(path, field),
                  QStringLiteral("整数超出允许范围 %1..%2").arg(minimum).arg(maximum));
            return std::nullopt;
        }
        return static_cast<qint64>(number);
    }

    std::optional<QJsonObject> requiredObject(const QJsonObject &object, const QString &field,
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

    std::optional<QStringList> tags(const QJsonObject &object, const QString &path)
    {
        if (!object.contains(QStringLiteral("tags"))) return QStringList{};
        const QString tagsPath = childPath(path, QStringLiteral("tags"));
        if (!object.value(QStringLiteral("tags")).isArray()) {
            error(ConfigErrorCode::WrongType, tagsPath, QStringLiteral("tags 必须为数组"));
            return std::nullopt;
        }
        const auto array = object.value(QStringLiteral("tags")).toArray();
        if (array.size() > maximumTags) {
            error(ConfigErrorCode::OutOfRange, tagsPath, QStringLiteral("标签数量不得超过 32"));
            return std::nullopt;
        }
        QStringList result;
        QSet<QString> seen;
        bool valid = true;
        for (qsizetype i = 0; i < array.size(); ++i) {
            const auto text = stringValue(array.at(i), childPath(tagsPath, QString::number(i)),
                                          32, false, false);
            if (!text) {
                valid = false;
            } else if (seen.contains(*text)) {
                error(ConfigErrorCode::DuplicateTag, childPath(tagsPath, QString::number(i)),
                      QStringLiteral("标签重复"));
                valid = false;
            } else {
                seen.insert(*text);
                result.append(*text);
            }
        }
        return valid ? std::optional<QStringList>(result) : std::nullopt;
    }

private:
    std::optional<QString> stringValue(const QJsonValue &value, const QString &path,
                                       qsizetype maximumLength, bool allowEmpty, bool identifier)
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
            error(ConfigErrorCode::InvalidIdentifier, path, QStringLiteral("标识符格式无效"));
            return std::nullopt;
        }
        return result;
    }

    QVector<ConfigError> &errors_;
    QString suiteId_;
    QString caseId_;
};

std::optional<ExecutionEnvironment> parseEnvironment(Parser &parser, const QJsonObject &object,
                                                      const QString &path)
{
    const auto value = parser.requiredString(object, QStringLiteral("environment"), path, 16);
    if (!value) return std::nullopt;
    if (*value == QStringLiteral("real_rs485")) return ExecutionEnvironment::RealRs485;
    if (*value == QStringLiteral("fake")) return ExecutionEnvironment::Fake;
    parser.error(ConfigErrorCode::InvalidCombination, childPath(path, QStringLiteral("environment")),
                 QStringLiteral("guided_recovery 仅支持 real_rs485 或 fake"));
    return std::nullopt;
}

std::optional<TestRequest> parseReadProbe(Parser &parser, const QJsonObject &object,
                                          const QString &path)
{
    parser.allowedFields(object, path, {QStringLiteral("function"), QStringLiteral("address"),
                                        QStringLiteral("count")});
    const auto function = parser.requiredInteger(object, QStringLiteral("function"), path, 3, 3);
    const auto address = parser.requiredInteger(object, QStringLiteral("address"), path, 0, 65535);
    const auto count = parser.requiredInteger(object, QStringLiteral("count"), path, 1,
                                              maximumReadRegisterCount);
    if (!function || !address || !count) return std::nullopt;
    const quint32 end = static_cast<quint32>(*address) + static_cast<quint32>(*count) - 1U;
    if (end > std::numeric_limits<quint16>::max()) {
        parser.error(ConfigErrorCode::AddressRangeOverflow, path,
                     QStringLiteral("探测地址范围末端超过 65535"));
        return std::nullopt;
    }
    TestRequest request;
    request.function = ModbusFunction::ReadHoldingRegisters;
    request.address = device::PduAddress(static_cast<quint16>(*address));
    request.count = static_cast<quint16>(*count);
    return request;
}

std::optional<ValueRepresentation> parseRepresentation(Parser &parser, const QJsonObject &object,
                                                        const QString &path)
{
    if (!object.contains(QStringLiteral("representation"))) return ValueRepresentation::UInt16;
    const auto value = parser.requiredString(object, QStringLiteral("representation"), path, 16);
    if (!value) return std::nullopt;
    if (*value == QStringLiteral("uint16")) return ValueRepresentation::UInt16;
    if (*value == QStringLiteral("int16")) return ValueRepresentation::Int16;
    parser.error(ConfigErrorCode::OutOfRange, childPath(path, QStringLiteral("representation")),
                 QStringLiteral("representation 仅支持 uint16 或 int16"));
    return std::nullopt;
}

std::optional<ExpectedAssertion> parseBusinessAssertion(Parser &parser,
                                                        const QJsonObject &object,
                                                        const QString &path,
                                                        quint16 probeCount)
{
    const auto type = parser.requiredString(object, QStringLiteral("type"), path, 32);
    if (!type) return std::nullopt;
    ExpectedAssertion result;
    if (*type == QStringLiteral("equals") || *type == QStringLiteral("range")) {
        if (probeCount != 1) {
            parser.error(ConfigErrorCode::InvalidCombination, path,
                         QStringLiteral("标量业务断言要求 probe.count=1"));
            return std::nullopt;
        }
        const auto representation = parseRepresentation(parser, object, path);
        if (!representation) return std::nullopt;
        result.representation = *representation;
        const qint64 minimum = *representation == ValueRepresentation::Int16 ? -32768 : 0;
        const qint64 maximum = *representation == ValueRepresentation::Int16 ? 32767 : 65535;
        if (*type == QStringLiteral("equals")) {
            parser.allowedFields(object, path, {QStringLiteral("type"), QStringLiteral("value"),
                                                QStringLiteral("representation")});
            const auto value = parser.requiredInteger(object, QStringLiteral("value"), path,
                                                      minimum, maximum);
            if (!value) return std::nullopt;
            result.type = AssertionType::Equals;
            result.value = *value;
            return result;
        }
        parser.allowedFields(object, path, {QStringLiteral("type"), QStringLiteral("min"),
                                            QStringLiteral("max"), QStringLiteral("representation")});
        const auto min = parser.requiredInteger(object, QStringLiteral("min"), path, minimum, maximum);
        const auto max = parser.requiredInteger(object, QStringLiteral("max"), path, minimum, maximum);
        if (!min || !max) return std::nullopt;
        if (*min > *max) {
            parser.error(ConfigErrorCode::InvalidCombination,
                         childPath(path, QStringLiteral("max")), QStringLiteral("min 不得大于 max"));
            return std::nullopt;
        }
        result.type = AssertionType::Range;
        result.minimum = *min;
        result.maximum = *max;
        return result;
    }
    parser.error(ConfigErrorCode::UnknownAssertionType, childPath(path, QStringLiteral("type")),
                 QStringLiteral("恢复业务断言仅支持 equals 或 range"));
    return std::nullopt;
}

std::optional<QVector<GuidedOperatorAction>> parseActions(Parser &parser,
                                                          const QJsonObject &object,
                                                          const QString &path)
{
    const QString actionsPath = childPath(path, QStringLiteral("allowed_actions"));
    if (!object.contains(QStringLiteral("allowed_actions"))) {
        parser.error(ConfigErrorCode::MissingField, actionsPath,
                     QStringLiteral("缺少 allowed_actions"));
        return std::nullopt;
    }
    if (!object.value(QStringLiteral("allowed_actions")).isArray()) {
        parser.error(ConfigErrorCode::WrongType, actionsPath,
                     QStringLiteral("allowed_actions 必须为数组"));
        return std::nullopt;
    }
    const auto array = object.value(QStringLiteral("allowed_actions")).toArray();
    QVector<GuidedOperatorAction> actions;
    QSet<QString> seen;
    bool valid = array.size() == 2;
    if (!valid) {
        parser.error(ConfigErrorCode::InvalidCombination, actionsPath,
                     QStringLiteral("allowed_actions 必须且只能包含 confirm、cancel"));
    }
    for (qsizetype i = 0; i < array.size(); ++i) {
        const QString itemPath = childPath(actionsPath, QString::number(i));
        if (!array.at(i).isString()) {
            parser.error(ConfigErrorCode::WrongType, itemPath, QStringLiteral("动作必须为字符串"));
            valid = false;
            continue;
        }
        const QString name = array.at(i).toString();
        if (seen.contains(name)) {
            parser.error(ConfigErrorCode::DuplicateId, itemPath, QStringLiteral("动作重复"));
            valid = false;
            continue;
        }
        seen.insert(name);
        if (name == QStringLiteral("confirm")) actions.append(GuidedOperatorAction::Confirm);
        else if (name == QStringLiteral("cancel")) actions.append(GuidedOperatorAction::Cancel);
        else {
            parser.error(ConfigErrorCode::UnknownAction, itemPath,
                         QStringLiteral("未知人工动作“%1”").arg(name));
            valid = false;
        }
    }
    if (!seen.contains(QStringLiteral("confirm")) || !seen.contains(QStringLiteral("cancel"))) {
        valid = false;
    }
    return valid ? std::optional<QVector<GuidedOperatorAction>>(actions) : std::nullopt;
}

std::optional<GuidedStep> parseOperatorStep(Parser &parser, const QJsonObject &object,
                                            const QString &path)
{
    parser.allowedFields(object, path, {QStringLiteral("id"), QStringLiteral("type"),
        QStringLiteral("purpose"), QStringLiteral("title"), QStringLiteral("instruction"),
        QStringLiteral("safety_notice"), QStringLiteral("allowed_actions"),
        QStringLiteral("wait_timeout_ms"), QStringLiteral("cancel_recovery_instruction")});
    const auto id = parser.requiredString(object, QStringLiteral("id"), path, 64, true);
    const auto purpose = parser.requiredString(object, QStringLiteral("purpose"), path, 32);
    const auto title = parser.requiredString(object, QStringLiteral("title"), path, 128);
    const auto instruction = parser.requiredString(object, QStringLiteral("instruction"), path, 1024);
    const auto safety = parser.requiredString(object, QStringLiteral("safety_notice"), path, 512);
    const auto actions = parseActions(parser, object, path);
    const auto wait = parser.requiredInteger(object, QStringLiteral("wait_timeout_ms"), path,
                                             minimumOperatorWaitMs, maximumOperatorWaitMs);
    const auto recovery = parser.requiredString(object, QStringLiteral("cancel_recovery_instruction"),
                                                path, 1024);
    if (!id || !purpose || !title || !instruction || !safety || !actions || !wait || !recovery) {
        return std::nullopt;
    }
    GuidedPromptPurpose parsedPurpose;
    if (*purpose == QStringLiteral("disconnect_rs485")) {
        parsedPurpose = GuidedPromptPurpose::DisconnectRs485;
    } else if (*purpose == QStringLiteral("reconnect_rs485")) {
        parsedPurpose = GuidedPromptPurpose::ReconnectRs485;
    } else {
        parser.error(ConfigErrorCode::InvalidCombination, childPath(path, QStringLiteral("purpose")),
                     QStringLiteral("未知人工提示目的"));
        return std::nullopt;
    }
    GuidedOperatorStep policy;
    policy.purpose = parsedPurpose;
    policy.title = *title;
    policy.instruction = *instruction;
    policy.safetyNotice = *safety;
    policy.allowedActions = *actions;
    policy.waitTimeout = std::chrono::milliseconds(*wait);
    policy.cancelRecoveryInstruction = *recovery;
    GuidedStep step;
    step.id = *id;
    step.type = GuidedStepType::OperatorPrompt;
    step.operatorStep = std::move(policy);
    return step;
}

std::optional<GuidedStep> parseObservationStep(Parser &parser, const QJsonObject &object,
                                               const QString &path, GuidedStepType type,
                                               qint64 requestTimeoutMs)
{
    parser.allowedFields(object, path, {QStringLiteral("id"), QStringLiteral("type"),
        QStringLiteral("probe"), QStringLiteral("interval_ms"), QStringLiteral("deadline_ms"),
        QStringLiteral("consecutive_matches"), QStringLiteral("business_assertion")});
    const auto id = parser.requiredString(object, QStringLiteral("id"), path, 64, true);
    const auto probeObject = parser.requiredObject(object, QStringLiteral("probe"), path);
    const auto interval = parser.requiredInteger(object, QStringLiteral("interval_ms"), path,
                                                 minimumObservationIntervalMs,
                                                 maximumObservationIntervalMs);
    const auto deadline = parser.requiredInteger(object, QStringLiteral("deadline_ms"), path,
                                                 minimumObservationDeadlineMs,
                                                 maximumObservationDeadlineMs);
    const auto consecutive = parser.requiredInteger(object, QStringLiteral("consecutive_matches"), path,
                                                    1, maximumObservationConsecutiveMatches);
    if (!id || !probeObject || !interval || !deadline || !consecutive) return std::nullopt;
    const auto probe = parseReadProbe(parser, *probeObject, childPath(path, QStringLiteral("probe")));
    if (!probe) return std::nullopt;
    if (*deadline < requestTimeoutMs || *interval > *deadline) {
        parser.error(ConfigErrorCode::InvalidCombination, childPath(path, QStringLiteral("deadline_ms")),
                     QStringLiteral("deadline 必须覆盖 request timeout 且不得小于 interval"));
        return std::nullopt;
    }
    const qint64 maximumStarts = (*deadline + *interval - 1) / *interval;
    if (*consecutive > maximumStarts) {
        parser.error(ConfigErrorCode::InvalidCombination,
                     childPath(path, QStringLiteral("consecutive_matches")),
                     QStringLiteral("连续命中数超过 deadline/interval 的理论启动数"));
        return std::nullopt;
    }
    GuidedObservationStep policy;
    policy.target = type == GuidedStepType::ObserveOutage
        ? GuidedObservationTarget::ConsecutiveResponseTimeouts
        : GuidedObservationTarget::ConsecutiveValidResponses;
    policy.probe = *probe;
    policy.interval = std::chrono::milliseconds(*interval);
    policy.deadline = std::chrono::milliseconds(*deadline);
    policy.consecutiveMatches = static_cast<int>(*consecutive);
    if (object.contains(QStringLiteral("business_assertion"))) {
        if (type == GuidedStepType::ObserveOutage) {
            parser.error(ConfigErrorCode::InvalidCombination,
                         childPath(path, QStringLiteral("business_assertion")),
                         QStringLiteral("中断观察不得配置业务断言"));
            return std::nullopt;
        }
        const auto assertionObject = parser.requiredObject(object,
            QStringLiteral("business_assertion"), path);
        if (!assertionObject) return std::nullopt;
        const auto assertion = parseBusinessAssertion(parser, *assertionObject,
            childPath(path, QStringLiteral("business_assertion")), probe->count);
        if (!assertion) return std::nullopt;
        policy.businessAssertion = *assertion;
    }
    GuidedStep step;
    step.id = *id;
    step.type = type;
    step.observationStep = std::move(policy);
    return step;
}

std::optional<QMap<QString, QString>> parseMetadata(Parser &parser, const QJsonObject &root)
{
    if (!root.contains(QStringLiteral("metadata"))) return QMap<QString, QString>{};
    const QString path = QStringLiteral("/metadata");
    if (!root.value(QStringLiteral("metadata")).isObject()) {
        parser.error(ConfigErrorCode::WrongType, path, QStringLiteral("metadata 必须为对象"));
        return std::nullopt;
    }
    const auto object = root.value(QStringLiteral("metadata")).toObject();
    if (object.size() > maximumMetadataEntries) {
        parser.error(ConfigErrorCode::OutOfRange, path, QStringLiteral("metadata 项数不得超过 32"));
        return std::nullopt;
    }
    static const QRegularExpression keyPattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
    QMap<QString, QString> result;
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

std::optional<TestCase> parseCase(Parser &parser, const QJsonValue &value, const QString &path)
{
    if (!value.isObject()) {
        parser.error(ConfigErrorCode::WrongType, path, QStringLiteral("用例必须为对象"));
        return std::nullopt;
    }
    const auto object = value.toObject();
    parser.allowedFields(object, path, {QStringLiteral("id"), QStringLiteral("name"),
        QStringLiteral("category"), QStringLiteral("description"), QStringLiteral("environment"),
        QStringLiteral("type"), QStringLiteral("enabled"), QStringLiteral("tags"),
        QStringLiteral("timeout"), QStringLiteral("steps")});
    const auto id = parser.requiredString(object, QStringLiteral("id"), path, 64, true);
    if (id) parser.setCaseId(*id);
    const auto name = parser.requiredString(object, QStringLiteral("name"), path, 128);
    const auto category = parser.requiredString(object, QStringLiteral("category"), path, 64);
    const auto description = parser.optionalString(object, QStringLiteral("description"), path, 1024);
    const auto environment = parseEnvironment(parser, object, path);
    const auto type = parser.requiredString(object, QStringLiteral("type"), path, 32);
    const auto tags = parser.tags(object, path);
    bool enabled = true;
    if (object.contains(QStringLiteral("enabled"))) {
        if (!object.value(QStringLiteral("enabled")).isBool()) {
            parser.error(ConfigErrorCode::WrongType, childPath(path, QStringLiteral("enabled")),
                         QStringLiteral("enabled 必须为布尔值"));
            return std::nullopt;
        }
        enabled = object.value(QStringLiteral("enabled")).toBool();
    }
    if (!id || !name || !category || !description || !environment || !type || !tags) {
        return std::nullopt;
    }
    if (*type != QStringLiteral("guided_recovery")) {
        parser.error(ConfigErrorCode::UnknownCaseType, childPath(path, QStringLiteral("type")),
                     QStringLiteral("Schema v3 仅接受 guided_recovery 用例"));
        return std::nullopt;
    }
    const auto timeoutObject = parser.requiredObject(object, QStringLiteral("timeout"), path);
    if (!timeoutObject) return std::nullopt;
    const QString timeoutPath = childPath(path, QStringLiteral("timeout"));
    parser.allowedFields(*timeoutObject, timeoutPath,
                         {QStringLiteral("request_ms"), QStringLiteral("case_ms")});
    const auto requestMs = parser.requiredInteger(*timeoutObject, QStringLiteral("request_ms"),
                                                  timeoutPath, 1, maximumRequestTimeoutMs);
    const auto caseMs = parser.requiredInteger(*timeoutObject, QStringLiteral("case_ms"),
                                               timeoutPath, 1, maximumGuidedCaseTimeoutMs);
    if (!requestMs || !caseMs) return std::nullopt;
    if (*caseMs < *requestMs) {
        parser.error(ConfigErrorCode::InvalidCombination,
                     childPath(timeoutPath, QStringLiteral("case_ms")),
                     QStringLiteral("case_ms 不得小于 request_ms"));
        return std::nullopt;
    }
    if (!object.contains(QStringLiteral("steps"))
        || !object.value(QStringLiteral("steps")).isArray()) {
        parser.error(object.contains(QStringLiteral("steps")) ? ConfigErrorCode::WrongType
                                                               : ConfigErrorCode::MissingField,
                     childPath(path, QStringLiteral("steps")), QStringLiteral("steps 必须为数组"));
        return std::nullopt;
    }
    const auto array = object.value(QStringLiteral("steps")).toArray();
    if (array.size() != guidedRecoveryStepCount || array.size() > maximumGuidedSteps) {
        parser.error(ConfigErrorCode::InvalidCombination, childPath(path, QStringLiteral("steps")),
                     QStringLiteral("guided_recovery 必须包含固定四个步骤"));
        return std::nullopt;
    }
    QVector<GuidedStep> steps;
    QSet<QString> ids;
    qint64 boundedBudget = 0;
    for (qsizetype i = 0; i < array.size(); ++i) {
        const QString stepPath = childPath(childPath(path, QStringLiteral("steps")),
                                           QString::number(i));
        if (!array.at(i).isObject()) {
            parser.error(ConfigErrorCode::WrongType, stepPath, QStringLiteral("步骤必须为对象"));
            return std::nullopt;
        }
        const auto stepObject = array.at(i).toObject();
        const auto stepTypeName = parser.requiredString(stepObject, QStringLiteral("type"),
                                                        stepPath, 32);
        if (!stepTypeName) return std::nullopt;
        std::optional<GuidedStep> step;
        if (*stepTypeName == QStringLiteral("operator_prompt")) {
            step = parseOperatorStep(parser, stepObject, stepPath);
        } else if (*stepTypeName == QStringLiteral("observe_outage")) {
            step = parseObservationStep(parser, stepObject, stepPath,
                                        GuidedStepType::ObserveOutage, *requestMs);
        } else if (*stepTypeName == QStringLiteral("observe_recovery")) {
            step = parseObservationStep(parser, stepObject, stepPath,
                                        GuidedStepType::ObserveRecovery, *requestMs);
        } else {
            parser.error(ConfigErrorCode::UnknownStepType,
                         childPath(stepPath, QStringLiteral("type")),
                         QStringLiteral("未知引导步骤类型“%1”").arg(*stepTypeName));
            return std::nullopt;
        }
        if (!step) return std::nullopt;
        if (ids.contains(step->id)) {
            parser.error(ConfigErrorCode::DuplicateId, childPath(stepPath, QStringLiteral("id")),
                         QStringLiteral("guided step ID 重复"));
            return std::nullopt;
        }
        ids.insert(step->id);
        boundedBudget += step->operatorStep
            ? step->operatorStep->waitTimeout.count() : step->observationStep->deadline.count();
        steps.append(*step);
    }
    const bool orderValid = steps[0].type == GuidedStepType::OperatorPrompt
        && steps[0].operatorStep->purpose == GuidedPromptPurpose::DisconnectRs485
        && steps[1].type == GuidedStepType::ObserveOutage
        && steps[2].type == GuidedStepType::OperatorPrompt
        && steps[2].operatorStep->purpose == GuidedPromptPurpose::ReconnectRs485
        && steps[3].type == GuidedStepType::ObserveRecovery;
    if (!orderValid) {
        parser.error(ConfigErrorCode::InvalidCombination, childPath(path, QStringLiteral("steps")),
                     QStringLiteral("步骤顺序必须为断线提示、中断观察、重连提示、恢复观察"));
        return std::nullopt;
    }
    if (*caseMs < boundedBudget) {
        parser.error(ConfigErrorCode::BudgetOverflow,
                     childPath(timeoutPath, QStringLiteral("case_ms")),
                     QStringLiteral("case_ms 小于人工等待与观察 deadline 总预算"));
        return std::nullopt;
    }
    TestCase result;
    result.id = *id;
    result.name = *name;
    result.category = *category;
    result.description = *description;
    result.environment = *environment;
    result.declaredType = TestCaseType::GuidedRecovery;
    result.type = TestCaseType::GuidedRecovery;
    result.enabled = enabled;
    result.tags = *tags;
    result.timeout = {std::chrono::milliseconds(*requestMs), std::chrono::milliseconds(*caseMs)};
    result.guidedRecovery.steps = std::move(steps);
    return result;
}

} // namespace

LoadResult loadTestSuiteV3(const QJsonObject &root)
{
    LoadResult result;
    Parser parser(result.errors);
    const auto id = parser.requiredString(root, QStringLiteral("id"), QString{}, 64, true);
    if (id) parser.setSuiteId(*id);
    parser.allowedFields(root, QString{}, {QStringLiteral("schema_version"), QStringLiteral("id"),
        QStringLiteral("name"), QStringLiteral("description"), QStringLiteral("tags"),
        QStringLiteral("metadata"), QStringLiteral("cases")});
    const auto version = parser.requiredInteger(root, QStringLiteral("schema_version"), QString{},
                                                testSuiteSchemaVersionV3, testSuiteSchemaVersionV3);
    const auto name = parser.requiredString(root, QStringLiteral("name"), QString{}, 128);
    const auto description = parser.optionalString(root, QStringLiteral("description"), QString{}, 1024);
    const auto tags = parser.tags(root, QString{});
    const auto metadata = parseMetadata(parser, root);
    QVector<TestCase> cases;
    if (!root.contains(QStringLiteral("cases"))) {
        parser.error(ConfigErrorCode::MissingField, QStringLiteral("/cases"),
                     QStringLiteral("缺少 cases 数组"));
    } else if (!root.value(QStringLiteral("cases")).isArray()) {
        parser.error(ConfigErrorCode::WrongType, QStringLiteral("/cases"),
                     QStringLiteral("cases 必须为数组"));
    } else {
        const auto array = root.value(QStringLiteral("cases")).toArray();
        if (array.isEmpty() || array.size() > maximumSuiteCases) {
            parser.error(ConfigErrorCode::OutOfRange, QStringLiteral("/cases"),
                         QStringLiteral("v3 用例数量必须为 1..1000"));
        } else {
            QSet<QString> ids;
            for (qsizetype i = 0; i < array.size(); ++i) {
                parser.clearCaseId();
                const QString path = QStringLiteral("/cases/%1").arg(i);
                const auto testCase = parseCase(parser, array.at(i), path);
                if (!testCase) continue;
                if (ids.contains(testCase->id)) {
                    parser.setCaseId(testCase->id);
                    parser.error(ConfigErrorCode::DuplicateId, childPath(path, QStringLiteral("id")),
                                 QStringLiteral("用例 ID 重复"));
                    continue;
                }
                ids.insert(testCase->id);
                cases.append(*testCase);
            }
        }
    }
    if (!result.errors.isEmpty() || !version || !id || !name || !description || !tags || !metadata) {
        return result;
    }
    TestSuite suite;
    suite.schemaVersion = testSuiteSchemaVersionV3;
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

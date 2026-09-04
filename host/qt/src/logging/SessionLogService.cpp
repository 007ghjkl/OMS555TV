#include "logging/SessionLogService.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <algorithm>

namespace oms555tv::logging {
namespace {

QString defaultOutputRoot()
{
    return QDir::current().filePath(QStringLiteral("output/logs"));
}

QJsonObject metadataObject(const QVariantMap &metadata)
{
    return QJsonObject::fromVariantMap(metadata);
}

} // namespace

SessionLogService::SessionLogService(
    diagnostics::CommunicationDiagnosticsModel &diagnostics,
    QString outputRoot,
    qsizetype memoryCapacity,
    qint64 maximumFileBytes,
    QObject *parent)
    : QObject(parent)
    , outputRoot_(outputRoot.isEmpty() ? defaultOutputRoot() : std::move(outputRoot))
    , memoryCapacity_(std::max<qsizetype>(1, memoryCapacity))
    , maximumFileBytes_(std::max<qint64>(1024, maximumFileBytes))
{
    qRegisterMetaType<SessionLogError>();
    connect(&diagnostics, &diagnostics::CommunicationDiagnosticsModel::recordAdded,
            this, &SessionLogService::appendDiagnostic);
}

SessionLogService::~SessionLogService()
{
    if (file_.isOpen()) {
        file_.flush();
        file_.close();
    }
}

bool SessionLogService::active() const noexcept { return active_; }
QString SessionLogService::sessionId() const { return sessionId_; }
QString SessionLogService::currentFilePath() const { return currentFilePath_; }
const QVector<LogEntry> &SessionLogService::entries() const noexcept { return entries_; }
const std::optional<SessionLogError> &SessionLogService::lastError() const noexcept
{
    return lastError_;
}

SessionLogResult SessionLogService::startSession(const QVariantMap &metadata)
{
    if (active_) {
        SessionLogError error{SessionLogErrorCode::AlreadyActive,
                              QStringLiteral("已有活动会话")};
        recordError(error);
        return {false, sessionId_, currentFilePath_, error};
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString dateDirectory = QDir(outputRoot_).filePath(
        now.date().toString(QStringLiteral("yyyy-MM-dd")));
    if (!QDir().mkpath(dateDirectory)) {
        SessionLogError error{SessionLogErrorCode::CreateDirectoryFailed,
                              QStringLiteral("无法创建日志目录")};
        recordError(error);
        return {false, {}, {}, error};
    }

    sessionId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    baseStem_ = QDir(dateDirectory).filePath(
        QStringLiteral("session-%1-%2")
            .arg(now.toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")), sessionId_));
    part_ = 0;
    active_ = true;
    fileWritingEnabled_ = true;
    lastError_.reset();
    if (!openPart(part_)) {
        active_ = false;
        fileWritingEnabled_ = false;
        sessionId_.clear();
        return {false, {}, {}, lastError_};
    }

    LogEntry start{now, LogLevel::Info, QStringLiteral("session"),
                   QStringLiteral("会话开始")};
    start.event = QStringLiteral("session_start");
    start.metadata = metadata;
    append(std::move(start));
    emit sessionStateChanged(true);
    return {true, sessionId_, currentFilePath_, std::nullopt};
}

SessionLogResult SessionLogService::endSession()
{
    if (!active_) {
        SessionLogError error{SessionLogErrorCode::NotActive,
                              QStringLiteral("当前没有活动会话")};
        recordError(error);
        return {false, {}, currentFilePath_, error};
    }
    const QString completedSessionId = sessionId_;
    std::optional<SessionLogError> endError;
    LogEntry end{QDateTime::currentDateTimeUtc(), LogLevel::Info,
                 QStringLiteral("session"), QStringLiteral("会话结束")};
    end.event = QStringLiteral("session_end");
    append(std::move(end));
    if (!fileWritingEnabled_ && lastError_) {
        endError = lastError_;
    }
    if (file_.isOpen()) {
        if (!file_.flush()) {
            recordError({SessionLogErrorCode::FlushFailed,
                         QStringLiteral("结束会话时刷新日志失败")});
            endError = lastError_;
        }
        file_.close();
    }
    active_ = false;
    fileWritingEnabled_ = false;
    emit sessionStateChanged(false);
    return {!endError.has_value(), completedSessionId, currentFilePath_, endError};
}

void SessionLogService::append(LogEntry entry)
{
    if (!entry.timestamp.isValid()) {
        entry.timestamp = QDateTime::currentDateTimeUtc();
    }
    if (entries_.size() >= memoryCapacity_) {
        entries_.remove(0, entries_.size() - memoryCapacity_ + 1);
    }
    entries_.push_back(entry);
    emit entriesChanged();

    if (!active_ || !fileWritingEnabled_) {
        return;
    }
    const QByteArray line = serialize(entry) + '\n';
    if (!rotateIfNeeded(line.size())) {
        return;
    }
    if (file_.write(line) != line.size()) {
        recordError({SessionLogErrorCode::WriteFailed,
                     QStringLiteral("写入会话日志失败")});
        fileWritingEnabled_ = false;
        return;
    }
    if (!file_.flush()) {
        recordError({SessionLogErrorCode::FlushFailed,
                     QStringLiteral("刷新会话日志失败")});
        fileWritingEnabled_ = false;
    }
}

void SessionLogService::clearMemory()
{
    if (entries_.isEmpty()) {
        return;
    }
    entries_.clear();
    emit entriesChanged();
}

void SessionLogService::appendDiagnostic(
    const diagnostics::DiagnosticRecord &record)
{
    const auto &result = record.result;
    LogEntry entry{result.evidence.completedUtc, record.level,
                   QStringLiteral("communication"),
                   QStringLiteral("Modbus 请求 %1")
                       .arg(diagnostics::requestStateName(result.state))};
    entry.event = QStringLiteral("request_completed");
    entry.requestId = result.requestId.value;
    if (result.error) {
        entry.errorCode = static_cast<int>(result.error->code);
    }
    entry.tx = result.evidence.txAdu;
    entry.rx = result.evidence.rxAdu;
    entry.metadata.insert(QStringLiteral("owner"),
                          diagnostics::ownerName(result.evidence.owner));
    entry.metadata.insert(QStringLiteral("function_code"),
                          static_cast<int>(result.evidence.functionCode));
    if (result.evidence.rtt) {
        entry.metadata.insert(QStringLiteral("rtt_ns"),
                              static_cast<qlonglong>(result.evidence.rtt->count()));
    }
    append(std::move(entry));
}

QByteArray SessionLogService::serialize(const LogEntry &entry) const
{
    QJsonObject object;
    object.insert(QStringLiteral("schema_version"), 1);
    object.insert(QStringLiteral("session_id"), sessionId_);
    object.insert(QStringLiteral("timestamp_utc"),
                  entry.timestamp.toUTC().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("level"), logLevelName(entry.level));
    object.insert(QStringLiteral("module"), entry.module);
    object.insert(QStringLiteral("event"), entry.event);
    object.insert(QStringLiteral("message"), entry.message);
    if (entry.requestId) {
        object.insert(QStringLiteral("request_id"),
                      static_cast<qint64>(*entry.requestId));
    }
    if (entry.errorCode) {
        object.insert(QStringLiteral("error_code"), *entry.errorCode);
    }
    if (!entry.tx.isEmpty()) {
        object.insert(QStringLiteral("tx"), diagnostics::byteArrayHex(entry.tx));
    }
    if (!entry.rx.isEmpty()) {
        object.insert(QStringLiteral("rx"), diagnostics::byteArrayHex(entry.rx));
    }
    if (!entry.metadata.isEmpty()) {
        object.insert(QStringLiteral("metadata"), metadataObject(entry.metadata));
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool SessionLogService::openPart(int part)
{
    currentFilePath_ = part == 0
        ? baseStem_ + QStringLiteral(".jsonl")
        : baseStem_ + QStringLiteral(".part%1.jsonl").arg(part, 3, 10, QLatin1Char('0'));
    file_.setFileName(currentFilePath_);
    if (!file_.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        recordError({SessionLogErrorCode::OpenFailed,
                     QStringLiteral("无法创建会话日志文件：%1").arg(file_.errorString())});
        return false;
    }
    return true;
}

bool SessionLogService::rotateIfNeeded(qint64 nextBytes)
{
    if (file_.size() + nextBytes <= maximumFileBytes_) {
        return true;
    }
    if (part_ >= 99) {
        recordError({SessionLogErrorCode::RetentionLimitReached,
                     QStringLiteral("会话日志已达到 100 个分片上限")});
        fileWritingEnabled_ = false;
        return false;
    }
    file_.flush();
    file_.close();
    ++part_;
    if (!openPart(part_)) {
        fileWritingEnabled_ = false;
        return false;
    }
    return true;
}

void SessionLogService::recordError(SessionLogError error)
{
    lastError_ = std::move(error);
    emit fileError(*lastError_);
}

} // namespace oms555tv::logging

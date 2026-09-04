#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVariantMap>

#include <optional>

namespace oms555tv::logging {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
    Test,
};

struct LogEntry {
    QDateTime timestamp;
    LogLevel level = LogLevel::Info;
    QString module;
    QString message;
    QString event;
    std::optional<quint64> requestId;
    std::optional<int> errorCode;
    QByteArray tx;
    QByteArray rx;
    QVariantMap metadata;
};

QString logLevelName(LogLevel level);
QString formatLogEntry(const LogEntry &entry);

} // namespace oms555tv::logging

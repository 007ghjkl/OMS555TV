#pragma once

#include <QDateTime>
#include <QString>

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
};

QString logLevelName(LogLevel level);
QString formatLogEntry(const LogEntry &entry);

} // namespace oms555tv::logging

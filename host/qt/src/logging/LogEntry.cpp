#include "logging/LogEntry.h"

namespace oms555tv::logging {

QString logLevelName(const LogLevel level)
{
    switch (level) {
    case LogLevel::Debug:
        return QStringLiteral("DEBUG");
    case LogLevel::Info:
        return QStringLiteral("INFO");
    case LogLevel::Warning:
        return QStringLiteral("WARNING");
    case LogLevel::Error:
        return QStringLiteral("ERROR");
    case LogLevel::Test:
        return QStringLiteral("TEST");
    }

    return QStringLiteral("UNKNOWN");
}

QString formatLogEntry(const LogEntry &entry)
{
    return QStringLiteral("[%1] [%2] [%3] %4")
        .arg(entry.timestamp.toUTC().toString(Qt::ISODateWithMs),
             logLevelName(entry.level),
             entry.module,
             entry.message);
}

} // namespace oms555tv::logging

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
        return QStringLiteral("WARN");
    case LogLevel::Error:
        return QStringLiteral("ERROR");
    case LogLevel::Test:
        return QStringLiteral("TEST");
    }

    return QStringLiteral("UNKNOWN");
}

QString formatLogEntry(const LogEntry &entry)
{
    QString result = QStringLiteral("[%1] [%2] [%3] %4")
        .arg(entry.timestamp.toUTC().toString(Qt::ISODateWithMs),
             logLevelName(entry.level),
             entry.module,
             entry.message);
    if (!entry.event.isEmpty()) {
        result += QStringLiteral(" event=%1").arg(entry.event);
    }
    if (entry.requestId) {
        result += QStringLiteral(" request_id=%1").arg(*entry.requestId);
    }
    if (entry.errorCode) {
        result += QStringLiteral(" error_code=%1").arg(*entry.errorCode);
    }
    return result;
}

} // namespace oms555tv::logging

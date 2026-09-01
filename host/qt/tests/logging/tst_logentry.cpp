#include "logging/LogEntry.h"

#include <QDate>
#include <QTest>
#include <QTime>

using oms555tv::logging::LogEntry;
using oms555tv::logging::LogLevel;
using oms555tv::logging::formatLogEntry;
using oms555tv::logging::logLevelName;

class LogEntryTest final : public QObject
{
    Q_OBJECT

private slots:
    void levelNamesAreStable()
    {
        QCOMPARE(logLevelName(LogLevel::Debug), QStringLiteral("DEBUG"));
        QCOMPARE(logLevelName(LogLevel::Info), QStringLiteral("INFO"));
        QCOMPARE(logLevelName(LogLevel::Warning), QStringLiteral("WARNING"));
        QCOMPARE(logLevelName(LogLevel::Error), QStringLiteral("ERROR"));
        QCOMPARE(logLevelName(LogLevel::Test), QStringLiteral("TEST"));
    }

    void entryContainsRequiredFields()
    {
        const LogEntry entry{
            QDateTime(QDate(2026, 9, 1), QTime(12, 0), Qt::UTC),
            LogLevel::Info,
            QStringLiteral("bootstrap"),
            QStringLiteral("ready"),
        };

        QCOMPARE(formatLogEntry(entry),
                 QStringLiteral("[2026-09-01T12:00:00.000Z] [INFO] [bootstrap] ready"));
    }
};

QTEST_APPLESS_MAIN(LogEntryTest)

#include "tst_logentry.moc"

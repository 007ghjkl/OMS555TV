#include "communication/FakeModbusClient.h"
#include "diagnostics/CommunicationDiagnostics.h"
#include "logging/SessionLogService.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace oms555tv;

class SessionLogTest final : public QObject
{
    Q_OBJECT

private slots:
    void lifecycleWritesReloadableJsonLinesAndKeepsMemoryBounded()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto scheduler = std::make_shared<communication::ManualScheduler>();
        communication::FakeModbusClient client(scheduler);
        diagnostics::CommunicationDiagnosticsModel diagnostics(client);
        logging::SessionLogService logger(diagnostics, directory.path(), 3, 4096);

        const auto started = logger.startSession({{QStringLiteral("device"),
                                                   QStringLiteral("fake")}});
        QVERIFY(started.succeeded);
        QVERIFY(logger.active());
        QVERIFY(started.filePath.contains(QStringLiteral("session-")));
        QVERIFY(started.filePath.contains(
            QDateTime::currentDateTimeUtc().date().toString(QStringLiteral("yyyy-MM-dd"))));
        QVERIFY(started.filePath.endsWith(QStringLiteral(".jsonl")));

        for (int index = 0; index < 4; ++index) {
            logging::LogEntry entry{QDateTime::currentDateTimeUtc(),
                                    logging::LogLevel::Test,
                                    QStringLiteral("test"),
                                    QStringLiteral("entry %1").arg(index)};
            entry.event = QStringLiteral("test_event");
            entry.requestId = static_cast<quint64>(index + 1);
            entry.tx = QByteArray::fromHex("0103");
            if (index == 0) {
                entry.errorCode = 77;
                entry.rx = QByteArray::fromHex("018302");
                entry.metadata.insert(QStringLiteral("case"), QStringLiteral("optional"));
            }
            logger.append(std::move(entry));
        }
        QCOMPARE(logger.entries().size(), 3);
        const auto ended = logger.endSession();
        QVERIFY(ended.succeeded);
        QVERIFY(!logger.active());

        QFile file(started.filePath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto lines = file.readAll().split('\n');
        int parsed = 0;
        bool sawStart = false;
        bool sawEnd = false;
        bool sawOptionalFields = false;
        for (const auto &line : lines) {
            if (line.isEmpty()) {
                continue;
            }
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(line, &error);
            QCOMPARE(error.error, QJsonParseError::NoError);
            QCOMPARE(document.object().value(QStringLiteral("schema_version")).toInt(), 1);
            sawStart |= document.object().value(QStringLiteral("event"))
                == QStringLiteral("session_start");
            sawEnd |= document.object().value(QStringLiteral("event"))
                == QStringLiteral("session_end");
            const auto object = document.object();
            if (object.value(QStringLiteral("request_id")).toInteger() == 1) {
                QCOMPARE(object.value(QStringLiteral("error_code")).toInt(), 77);
                QCOMPARE(object.value(QStringLiteral("tx")).toString(),
                         QStringLiteral("01 03"));
                QCOMPARE(object.value(QStringLiteral("rx")).toString(),
                         QStringLiteral("01 83 02"));
                QCOMPARE(object.value(QStringLiteral("metadata")).toObject()
                             .value(QStringLiteral("case")).toString(),
                         QStringLiteral("optional"));
                sawOptionalFields = true;
            }
            ++parsed;
        }
        QCOMPARE(parsed, 6);
        QVERIFY(sawStart);
        QVERIFY(sawEnd);
        QVERIFY(sawOptionalFields);
    }

    void invalidOutputPathReturnsStructuredErrorWithoutStoppingMemoryLog()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString filePath = directory.filePath(QStringLiteral("not-a-directory"));
        QFile blocker(filePath);
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();

        auto scheduler = std::make_shared<communication::ManualScheduler>();
        communication::FakeModbusClient client(scheduler);
        diagnostics::CommunicationDiagnosticsModel diagnostics(client);
        logging::SessionLogService logger(diagnostics, filePath, 2);
        const auto started = logger.startSession();
        QVERIFY(!started.succeeded);
        QCOMPARE(started.error->code,
                 logging::SessionLogErrorCode::CreateDirectoryFailed);
        logger.append({QDateTime::currentDateTimeUtc(), logging::LogLevel::Error,
                       QStringLiteral("test"), QStringLiteral("still alive")});
        QCOMPARE(logger.entries().size(), 1);
    }

    void duplicateStartDoesNotMakeSuccessfulEndFail()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto scheduler = std::make_shared<communication::ManualScheduler>();
        communication::FakeModbusClient client(scheduler);
        diagnostics::CommunicationDiagnosticsModel diagnostics(client);
        logging::SessionLogService logger(diagnostics, directory.path());

        QVERIFY(logger.startSession().succeeded);
        const auto duplicate = logger.startSession();
        QVERIFY(!duplicate.succeeded);
        QCOMPARE(duplicate.error->code, logging::SessionLogErrorCode::AlreadyActive);

        const auto ended = logger.endSession();
        QVERIFY(ended.succeeded);
        QVERIFY(!ended.error.has_value());
    }
};

QTEST_APPLESS_MAIN(SessionLogTest)

#include "tst_sessionlog.moc"

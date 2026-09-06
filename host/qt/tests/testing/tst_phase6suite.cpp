#include "app/AppStateController.h"
#include "communication/FakeModbusClient.h"
#include "diagnostics/CommunicationDiagnostics.h"
#include "logging/SessionLogService.h"
#include "monitor/MonitorService.h"
#include "testing/TestCaseLoader.h"
#include "testing/TestEngine.h"
#include "../monitor/ManualMonitorScheduler.h"

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>

using namespace oms555tv;

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

testing::LoadResult loadSuite(const QString &path)
{
    return testing::TestCaseLoader::load(readFile(path));
}

communication::ModbusConnectionConfig connectionConfig()
{
    communication::ModbusConnectionConfig config;
    config.serial.portName = QStringLiteral("FAKE");
    return config;
}

communication::FakeOutcome readSuccess(QVector<quint16> values)
{
    communication::FakeOutcome result;
    result.kind = communication::FakeOutcomeKind::ReadSuccess;
    result.readValues = std::move(values);
    return result;
}

communication::FakeOutcome outcome(communication::FakeOutcomeKind kind,
                                   quint8 exceptionCode = 0)
{
    communication::FakeOutcome result;
    result.kind = kind;
    result.exceptionCode = exceptionCode;
    return result;
}

communication::FakeStep readStep(quint16 address, quint16 count,
                                 communication::FakeOutcome result,
                                 int timeoutMs = 500)
{
    return {{communication::CommunicationOwner::Testing,
             communication::ReadRequestDescriptor{device::PduAddress(address), count},
             std::chrono::milliseconds(timeoutMs)},
            std::move(result), {}, {}, {}};
}

communication::FakeStep writeStep(quint16 address, quint16 raw,
                                  communication::FakeOutcome result,
                                  int timeoutMs = 500)
{
    return {{communication::CommunicationOwner::Testing,
             communication::WriteRequestDescriptor{device::PduAddress(address), raw},
             std::chrono::milliseconds(timeoutMs)},
            std::move(result), {}, {}, {}};
}

void enqueueWriteAndRestore(communication::FakeModbusClient &client,
                            quint16 address, quint16 original, quint16 written)
{
    client.enqueueStep(readStep(address, 1, readSuccess({original})));
    client.enqueueStep(writeStep(address, written,
                                 outcome(communication::FakeOutcomeKind::WriteSuccess)));
    client.enqueueStep(readStep(address, 1, readSuccess({written})));
    client.enqueueStep(writeStep(address, original,
                                 outcome(communication::FakeOutcomeKind::WriteSuccess)));
}

void enqueueMainPassScript(communication::FakeModbusClient &client)
{
    client.enqueueStep(readStep(0, 1, readSuccess({250})));                  // F001
    client.enqueueStep(readStep(0, 4, readSuccess({250, 260, 270, 280})));   // F002
    client.enqueueStep(readStep(9, 4, readSuccess({600, 610, 620, 400})));   // F003
    enqueueWriteAndRestore(client, 9, 600, 650);                             // F004
    client.enqueueStep(readStep(39, 2, readSuccess({0, 2})));                // F005
    client.enqueueStep(readStep(4, 1, readSuccess({1200})));                 // F006
    client.enqueueStep(readStep(19, 2, readSuccess({0, 33})));               // F007
    client.enqueueStep(readStep(30, 2, readSuccess({100, 0})));              // F008
    client.enqueueStep(readStep(29, 1, readSuccess({0})));                   // P001
    enqueueWriteAndRestore(client, 10, 610, 650);                            // P002
    client.enqueueStep(readStep(50000, 1,
        outcome(communication::FakeOutcomeKind::RemoteException, 2)));       // P004
    for (int repetition = 0; repetition < 5; ++repetition) {                 // P007
        client.enqueueStep(readStep(39, 1, readSuccess({0})));
        client.enqueueStep(readStep(40, 1, readSuccess({2})));
        client.enqueueStep(readStep(29, 1, readSuccess({0})));
    }
    enqueueWriteAndRestore(client, 11, 600, 65136);                          // B003
    enqueueWriteAndRestore(client, 12, 400, 800);                            // B004
    client.enqueueStep(writeStep(9, 801,
        outcome(communication::FakeOutcomeKind::RemoteException, 3)));       // B005
    client.enqueueStep(readStep(40, 2,
        outcome(communication::FakeOutcomeKind::RemoteException, 2)));       // B006
    client.enqueueStep(readStep(30, 2, readSuccess({120, 0})));              // D002
    client.enqueueStep(readStep(30, 2, readSuccess({120, 0})));              // D003
    client.enqueueStep(readStep(30, 2, readSuccess({121, 0})));
    client.enqueueStep(readStep(30, 2, readSuccess({122, 0})));
    client.enqueueStep(readStep(50000, 1,
        outcome(communication::FakeOutcomeKind::RemoteException, 2)));       // R-AUTO
    client.enqueueStep(readStep(39, 2, readSuccess({0, 2})));
    for (int iteration = 0; iteration < 600; ++iteration) {                  // S001
        auto step = readStep(0, 5, readSuccess({250, 260, 270, 280, 1200}));
        step.virtualDelay = std::chrono::milliseconds(1);
        client.enqueueStep(std::move(step));
    }
}

testing::TestSuite onlyCase(const testing::TestSuite &source, const QString &id)
{
    testing::TestSuite suite = source;
    suite.id += QStringLiteral("-") + id;
    suite.cases.clear();
    for (const auto &testCase : source.cases) {
        if (testCase.id == id) suite.cases.append(testCase);
    }
    return suite;
}

struct Fixture {
    const QDateTime epoch = QDateTime::fromMSecsSinceEpoch(0, Qt::UTC);
    QTemporaryDir output;
    std::shared_ptr<communication::ManualScheduler> scheduler
        = std::make_shared<communication::ManualScheduler>();
    communication::FakeModbusClient client{scheduler, epoch};
    monitor::test::ManualMonitorScheduler monitorScheduler{scheduler, epoch};
    monitor::MonitorService monitorService{client, monitorScheduler};
    app::AppStateController controller{client, monitorService};
    diagnostics::CommunicationDiagnosticsModel diagnostics{client};
    logging::SessionLogService log{diagnostics, output.path()};
    testing::TestResultManager results;
    testing::TestEngine engine{controller, client, results, &log, monitorScheduler};

    void enterTesting()
    {
        QVERIFY(output.isValid());
        QVERIFY(controller.connectDevice(connectionConfig()).accepted());
        scheduler->runUntilIdle();
        QVERIFY(controller.startTesting().accepted());
        scheduler->runUntilIdle();
        QCOMPARE(controller.state(), app::AppState::Testing);
    }

    const testing::TestSuiteResult &finished() const
    {
        return *results.history().back();
    }
};

QSet<QString> mainIds()
{
    return {QStringLiteral("TC-F001"), QStringLiteral("TC-F002"),
            QStringLiteral("TC-F003"), QStringLiteral("TC-F004"),
            QStringLiteral("TC-F005"), QStringLiteral("TC-F006"),
            QStringLiteral("TC-F007"), QStringLiteral("TC-F008"),
            QStringLiteral("TC-P001"), QStringLiteral("TC-P002"),
            QStringLiteral("TC-P004"), QStringLiteral("TC-P007"),
            QStringLiteral("TC-B003"), QStringLiteral("TC-B004"),
            QStringLiteral("TC-B005"), QStringLiteral("TC-B006"),
            QStringLiteral("TC-D002"), QStringLiteral("TC-D003"),
            QStringLiteral("TC-R-AUTO-001"), QStringLiteral("TC-S001")};
}

QSet<QString> fakeIds()
{
    return {QStringLiteral("TC-P006"), QStringLiteral("TC-B001"),
            QStringLiteral("TC-B002"), QStringLiteral("TC-D001")};
}

QSet<QString> tableIds(const QString &markdown)
{
    QSet<QString> result;
    const QRegularExpression pattern(
        QStringLiteral("^\\| (TC-[A-Z0-9-]+) \\|"),
        QRegularExpression::MultilineOption);
    auto matches = pattern.globalMatch(markdown);
    while (matches.hasNext()) result.insert(matches.next().captured(1));
    return result;
}

qsizetype tableIdRowCount(const QString &markdown)
{
    const QRegularExpression pattern(
        QStringLiteral("^\\| (TC-[A-Z0-9-]+) \\|"),
        QRegularExpression::MultilineOption);
    qsizetype count = 0;
    auto matches = pattern.globalMatch(markdown);
    while (matches.hasNext()) {
        (void)matches.next();
        ++count;
    }
    return count;
}

} // namespace

class Phase6SuiteTest final : public QObject
{
    Q_OBJECT

private slots:
    void formalCatalogIsCompleteUniqueSafeAndTraceable()
    {
        const auto main = loadSuite(QStringLiteral(OMS555TV_PHASE6_RS485_SUITE));
        const auto fake = loadSuite(QStringLiteral(OMS555TV_PHASE6_FAKE_SUITE));
        for (const auto &error : main.errors) {
            qDebug() << testing::configErrorCodeName(error.code)
                     << error.path << error.diagnostic;
        }
        for (const auto &error : fake.errors) {
            qDebug() << testing::configErrorCodeName(error.code)
                     << error.path << error.diagnostic;
        }
        QVERIFY(main.succeeded());
        QVERIFY(fake.succeeded());
        QCOMPARE(main.suite->schemaVersion, testing::testSuiteSchemaVersionV2);
        QCOMPARE(fake.suite->schemaVersion, testing::testSuiteSchemaVersionV2);
        QCOMPARE(main.suite->cases.size(), 20);
        QCOMPARE(fake.suite->cases.size(), 4);

        QSet<QString> actualMain;
        QSet<QString> names;
        QSet<QString> descriptions;
        QMap<QString, int> categoryCounts;
        for (const auto &testCase : main.suite->cases) {
            QVERIFY(testCase.enabled);
            QVERIFY(!testCase.description.trimmed().isEmpty());
            QVERIFY(!testCase.tags.isEmpty());
            QVERIFY(testCase.environment != testing::ExecutionEnvironment::Fake);
            actualMain.insert(testCase.id);
            names.insert(testCase.name);
            descriptions.insert(testCase.description);
            ++categoryCounts[testCase.category];
            if (testCase.declaredType == testing::TestCaseType::WriteAndVerify) {
                QVERIFY(testCase.request.readBeforeWrite);
                QVERIFY(testCase.request.restoreOriginal);
            }
            if (testCase.request.function
                == testing::ModbusFunction::WriteSingleRegister) {
                QVERIFY(testCase.declaredType == testing::TestCaseType::WriteAndVerify
                        || testCase.declaredType == testing::TestCaseType::ExpectException);
            }
        }
        QCOMPARE(actualMain, mainIds());
        QCOMPARE(names.size(), 20);
        QCOMPARE(descriptions.size(), 20);
        QVERIFY(categoryCounts[QStringLiteral("functional")] >= 8);
        QVERIFY(categoryCounts[QStringLiteral("protocol")] >= 4);
        QVERIFY(categoryCounts[QStringLiteral("boundary")] >= 4);
        QVERIFY(categoryCounts[QStringLiteral("consistency")] >= 2);
        QVERIFY(categoryCounts[QStringLiteral("recovery")] >= 1);
        QVERIFY(categoryCounts[QStringLiteral("stability")] >= 1);

        QSet<QString> actualFake;
        for (const auto &testCase : fake.suite->cases) {
            QVERIFY(testCase.enabled);
            QCOMPARE(testCase.environment, testing::ExecutionEnvironment::Fake);
            actualFake.insert(testCase.id);
        }
        QCOMPARE(actualFake, fakeIds());
        QSet<QString> all = actualMain;
        all.unite(actualFake);
        QCOMPARE(all.size(), 24);

        const auto stability = std::find_if(
            main.suite->cases.cbegin(), main.suite->cases.cend(), [](const auto &testCase) {
                return testCase.id == QStringLiteral("TC-S001");
            });
        QVERIFY(stability != main.suite->cases.cend());
        QVERIFY(stability->stability.duration >= std::chrono::minutes(10));
        QCOMPARE(stability->environment, testing::ExecutionEnvironment::RealRs485);

        const QString coverage = QString::fromUtf8(readFile(
            QStringLiteral(OMS555TV_PHASE6_COVERAGE_MATRIX)));
        const QString catalog = QString::fromUtf8(readFile(
            QStringLiteral(OMS555TV_PHASE6_TEST_CASES)));
        QVERIFY(!coverage.isEmpty());
        QVERIFY(!catalog.isEmpty());
        const qsizetype phase7Section = catalog.indexOf(QStringLiteral("## 4. Phase 7"));
        QVERIFY(phase7Section > 0);
        const QString phase6Catalog = catalog.left(phase7Section);
        QCOMPARE(tableIdRowCount(coverage), qsizetype(24));
        QCOMPARE(tableIdRowCount(phase6Catalog), qsizetype(24));
        QCOMPARE(tableIds(coverage), all);
        QCOMPARE(tableIds(phase6Catalog), all);
    }

    void executesAllFormalCasesWithFakeAndVirtualTenMinutes()
    {
        const auto loaded = loadSuite(QStringLiteral(OMS555TV_PHASE6_RS485_SUITE));
        QVERIFY(loaded.succeeded());
        Fixture fixture;
        const auto session = fixture.log.startSession(
            {{QStringLiteral("suite"), QStringLiteral("phase6-rs485-full")}});
        QVERIFY(session.succeeded);
        fixture.enterTesting();
        enqueueMainPassScript(fixture.client);
        QVERIFY(fixture.engine.runSuite(*loaded.suite).accepted());
        fixture.scheduler->runUntilIdle();

        const auto &result = fixture.finished();
        QCOMPARE(result.status, testing::TestStatus::Pass);
        QCOMPARE(result.cases.size(), 20);
        QCOMPARE(result.sessionId, session.sessionId);
        for (const auto &testCase : result.cases) {
            QCOMPARE(testCase.status, testing::TestStatus::Pass);
            QCOMPARE(testCase.sessionId, session.sessionId);
        }
        QCOMPARE(result.cases[11].steps.size(), 15); // P007: 3 steps × 5 repetitions
        QCOMPARE(result.cases[18].steps.size(), 2);  // R-AUTO
        const auto &stability = result.cases[19];
        QVERIFY(stability.stability.has_value());
        QCOMPARE(stability.stability->total, quint64(600));
        QCOMPARE(stability.stability->successes, quint64(600));
        QCOMPARE(stability.stability->failures, quint64(0));
        QCOMPARE(stability.stability->timeouts, quint64(0));
        QCOMPARE(stability.stability->validRttSamples, quint64(600));
        QCOMPARE(stability.evidenceRetention.totalAttempts, quint64(600));
        QVERIFY(stability.attempts.size() <= 64);
        QCOMPARE(stability.evidenceRetention.droppedAttempts,
                 quint64(600 - stability.attempts.size()));

        for (const int index : {3, 9, 12, 13}) {
            const auto &writeResult = result.cases[index];
            QCOMPARE(writeResult.attempts.size(), 4);
            QCOMPARE(writeResult.attempts.back().purpose,
                     testing::TestStepPurpose::RestoreOriginal);
            QCOMPARE(writeResult.attempts.back().requestResult.state,
                     communication::RequestState::Succeeded);
        }
        QCOMPARE(fixture.diagnostics.records().size(), 648);
        QSet<quint64> requestIds;
        for (const auto &record : fixture.diagnostics.records()) {
            QVERIFY(record.result.requestId.value > 0);
            QVERIFY(!requestIds.contains(record.result.requestId.value));
            requestIds.insert(record.result.requestId.value);
        }
        QCOMPARE(requestIds.size(), 648);
        QVERIFY(fixture.client.scriptConsumed());
        QVERIFY(!fixture.client.hasNonTerminalRequests());
        QCOMPARE(fixture.controller.state(), app::AppState::ConnectedIdle);
        QCOMPARE(fixture.client.activeOwner(), communication::CommunicationOwner::None);

        QVERIFY(fixture.log.endSession().succeeded);
        const QByteArray jsonl = readFile(session.filePath);
        QCOMPARE(jsonl.count("\"event\":\"request_attempt\""), 648);
    }

    void executesDeterministicFakeBoundarySuite()
    {
        const auto loaded = loadSuite(QStringLiteral(OMS555TV_PHASE6_FAKE_SUITE));
        QVERIFY(loaded.succeeded());
        Fixture fixture;
        fixture.enterTesting();
        fixture.client.enqueueStep(readStep(0, 1,
            outcome(communication::FakeOutcomeKind::Timeout), 100));
        fixture.client.enqueueStep(readStep(0, 1, readSuccess({0x8000})));
        fixture.client.enqueueStep(readStep(0, 1, readSuccess({0x7fff})));
        fixture.client.enqueueStep(readStep(0, 1, readSuccess({0xff9c})));
        QVERIFY(fixture.engine.runSuite(*loaded.suite).accepted());
        fixture.scheduler->runUntilIdle();
        QCOMPARE(fixture.finished().status, testing::TestStatus::Pass);
        for (const auto &testCase : fixture.finished().cases) {
            QCOMPARE(testCase.status, testing::TestStatus::Pass);
        }
        QVERIFY(fixture.client.scriptConsumed());
    }

    void formalCasesCoverFailErrorCleanupFailureAndAbort()
    {
        const auto loaded = loadSuite(QStringLiteral(OMS555TV_PHASE6_RS485_SUITE));
        QVERIFY(loaded.succeeded());
        {
            Fixture fixture;
            fixture.enterTesting();
            fixture.client.enqueueStep(readStep(0, 1, readSuccess({2000})));
            QVERIFY(fixture.engine.runSuite(
                onlyCase(*loaded.suite, QStringLiteral("TC-F001"))).accepted());
            fixture.scheduler->runUntilIdle();
            QCOMPARE(fixture.finished().cases.front().status, testing::TestStatus::Fail);
        }
        {
            Fixture fixture;
            fixture.enterTesting();
            fixture.client.enqueueStep(readStep(29, 1,
                outcome(communication::FakeOutcomeKind::SerialError)));
            QVERIFY(fixture.engine.runSuite(
                onlyCase(*loaded.suite, QStringLiteral("TC-P001"))).accepted());
            fixture.scheduler->runUntilIdle();
            QCOMPARE(fixture.finished().cases.front().status, testing::TestStatus::Error);
        }
        {
            Fixture fixture;
            fixture.enterTesting();
            fixture.client.enqueueStep(readStep(11, 1, readSuccess({600})));
            fixture.client.enqueueStep(writeStep(11, 65136,
                outcome(communication::FakeOutcomeKind::WriteSuccess)));
            fixture.client.enqueueStep(readStep(11, 1, readSuccess({65136})));
            fixture.client.enqueueStep(writeStep(11, 600,
                outcome(communication::FakeOutcomeKind::SerialError)));
            QVERIFY(fixture.engine.runSuite(
                onlyCase(*loaded.suite, QStringLiteral("TC-B003"))).accepted());
            fixture.scheduler->runUntilIdle();
            const auto &result = fixture.finished().cases.front();
            QCOMPARE(result.status, testing::TestStatus::Error);
            QVERIFY(result.cleanupError.has_value());
            QCOMPARE(result.cleanupError->code, testing::TestErrorCode::CleanupFailed);
        }
        {
            Fixture fixture;
            fixture.enterTesting();
            fixture.client.enqueueStep(readStep(0, 5,
                outcome(communication::FakeOutcomeKind::Pending)));
            QVERIFY(fixture.engine.runSuite(
                onlyCase(*loaded.suite, QStringLiteral("TC-S001"))).accepted());
            fixture.scheduler->runUntilIdle();
            QVERIFY(fixture.engine.abort());
            fixture.scheduler->runUntilIdle();
            const auto &result = fixture.finished().cases.front();
            QCOMPARE(result.status, testing::TestStatus::Error);
            QCOMPARE(result.error->code, testing::TestErrorCode::Aborted);
            QCOMPARE(fixture.controller.state(), app::AppState::ConnectedIdle);
        }
    }
};

QTEST_MAIN(Phase6SuiteTest)

#include "tst_phase6suite.moc"

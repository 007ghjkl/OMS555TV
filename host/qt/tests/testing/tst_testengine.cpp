#include "app/AppStateController.h"
#include "communication/FakeModbusClient.h"
#include "diagnostics/CommunicationDiagnostics.h"
#include "logging/SessionLogService.h"
#include "monitor/MonitorService.h"
#include "testing/TestEngine.h"
#include "../monitor/ManualMonitorScheduler.h"

#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace oms555tv;

namespace {

communication::ModbusConnectionConfig connectionConfig()
{
    communication::ModbusConnectionConfig config;
    config.serial.portName = QStringLiteral("FAKE");
    return config;
}

communication::FakeOutcome readSuccess(QVector<quint16> values)
{
    communication::FakeOutcome outcome;
    outcome.kind = communication::FakeOutcomeKind::ReadSuccess;
    outcome.readValues = std::move(values);
    return outcome;
}

communication::FakeOutcome outcome(communication::FakeOutcomeKind kind,
                                   quint8 exceptionCode = 0)
{
    communication::FakeOutcome value;
    value.kind = kind;
    value.exceptionCode = exceptionCode;
    return value;
}

communication::FakeStep readStep(device::PduAddress address, quint16 count,
                                 communication::FakeOutcome result,
                                 std::chrono::milliseconds timeout =
                                     std::chrono::milliseconds(500))
{
    return {{communication::CommunicationOwner::Testing,
             communication::ReadRequestDescriptor{address, count}, timeout},
            std::move(result), {}, {}, {}};
}

communication::FakeStep writeStep(device::PduAddress address, quint16 raw,
                                  communication::FakeOutcome result,
                                  std::chrono::milliseconds timeout =
                                      std::chrono::milliseconds(500))
{
    return {{communication::CommunicationOwner::Testing,
             communication::WriteRequestDescriptor{address, raw}, timeout},
            std::move(result), {}, {}, {}};
}

testing::TestCase readCase(QString id, quint16 address, qint64 expected)
{
    testing::TestCase testCase;
    testCase.id = std::move(id);
    testCase.name = testCase.id;
    testCase.category = QStringLiteral("test");
    testCase.type = testing::TestCaseType::ReadRegisters;
    testCase.declaredType = testing::TestCaseType::ReadRegister;
    testCase.request.address = device::PduAddress(address);
    testCase.request.count = 1;
    testCase.expected.type = testing::AssertionType::Equals;
    testCase.expected.value = expected;
    return testCase;
}

testing::TestCase writeCase(QString id, quint16 address, quint16 raw)
{
    auto testCase = readCase(std::move(id), address, raw);
    testCase.type = testing::TestCaseType::WriteRegister;
    testCase.declaredType = testCase.type;
    testCase.request.function = testing::ModbusFunction::WriteSingleRegister;
    testCase.request.rawValue = raw;
    return testCase;
}

testing::TestCase exceptionCase(QString id, quint16 address, quint8 code)
{
    auto testCase = readCase(std::move(id), address, 0);
    testCase.type = testing::TestCaseType::ExpectException;
    testCase.declaredType = testCase.type;
    testCase.expected.type = testing::AssertionType::ModbusException;
    testCase.expected.exceptionCode = code;
    return testCase;
}

testing::TestCase writeVerifyCase(QString id, quint16 address, quint16 raw,
                                  bool restore = true)
{
    auto testCase = writeCase(std::move(id), address, raw);
    testCase.type = testing::TestCaseType::WriteAndVerify;
    testCase.declaredType = testCase.type;
    testCase.request.readBeforeWrite = true;
    testCase.request.restoreOriginal = restore;
    testCase.timeout.testCase = std::chrono::milliseconds(2000);
    return testCase;
}

testing::TestSuite suiteOf(std::initializer_list<testing::TestCase> cases)
{
    testing::TestSuite suite;
    suite.id = QStringLiteral("suite");
    suite.name = QStringLiteral("套件");
    for (const auto &testCase : cases) {
        suite.cases.append(testCase);
    }
    return suite;
}

struct Fixture {
    const QDateTime epoch = QDateTime::fromMSecsSinceEpoch(0, Qt::UTC);
    std::shared_ptr<communication::ManualScheduler> scheduler
        = std::make_shared<communication::ManualScheduler>();
    communication::FakeModbusClient client{scheduler, epoch};
    monitor::test::ManualMonitorScheduler monitorScheduler{scheduler};
    monitor::MonitorService monitorService{client, monitorScheduler};
    app::AppStateController controller{client, monitorService};
    diagnostics::CommunicationDiagnosticsModel diagnostics{client};
    logging::SessionLogService log{diagnostics};
    testing::TestResultManager results;
    testing::TestEngine engine{
        controller, client, results, &log,
        [this] {
            return epoch.addMSecs(
                std::chrono::duration_cast<std::chrono::milliseconds>(scheduler->now())
                    .count());
        }};

    void enterTesting()
    {
        QVERIFY(controller.connectDevice(connectionConfig()).accepted());
        scheduler->runUntilIdle();
        QVERIFY(controller.startTesting().accepted());
        scheduler->runUntilIdle();
        QCOMPARE(controller.state(), app::AppState::Testing);
        QCOMPARE(client.activeOwner(), communication::CommunicationOwner::Testing);
    }

    const testing::TestSuiteResult &finished() const
    {
        return *results.history().back();
    }
};

} // namespace

class TestEngineTest final : public QObject
{
    Q_OBJECT

private slots:
    void requiresTestingOwnerAndEmptySuiteReleasesIt()
    {
        Fixture fixture;
        const auto suite = suiteOf({});
        const auto rejected = fixture.engine.runSuite(suite);
        QVERIFY(!rejected.accepted());
        QCOMPARE(rejected.rejection->code, testing::TestErrorCode::InvalidState);

        fixture.enterTesting();
        QVERIFY(fixture.engine.runSuite(suite).accepted());
        fixture.scheduler->runUntilIdle();
        QCOMPARE(fixture.finished().status, testing::TestStatus::Pass);
        QCOMPARE(fixture.controller.state(), app::AppState::ConnectedIdle);
        QCOMPARE(fixture.client.activeOwner(), communication::CommunicationOwner::None);
    }

    void executesAllBasicHandlersAndPreservesEvidence()
    {
        Fixture fixture;
        fixture.enterTesting();
        const auto suite = suiteOf({readCase(QStringLiteral("read"), 0, 123),
                                    writeCase(QStringLiteral("write"), 9, 600),
                                    exceptionCase(QStringLiteral("exception"), 65535, 2),
                                    writeVerifyCase(QStringLiteral("verify"), 10, 650)});
        fixture.client.enqueueStep(readStep(device::PduAddress(0), 1, readSuccess({123})));
        fixture.client.enqueueStep(writeStep(device::PduAddress(9), 600,
                                             outcome(communication::FakeOutcomeKind::WriteSuccess)));
        fixture.client.enqueueStep(readStep(device::PduAddress(65535), 1,
                                             outcome(communication::FakeOutcomeKind::RemoteException,
                                                     2)));
        fixture.client.enqueueStep(readStep(device::PduAddress(10), 1, readSuccess({610})));
        fixture.client.enqueueStep(writeStep(device::PduAddress(10), 650,
                                             outcome(communication::FakeOutcomeKind::WriteSuccess)));
        fixture.client.enqueueStep(readStep(device::PduAddress(10), 1, readSuccess({650})));
        fixture.client.enqueueStep(writeStep(device::PduAddress(10), 610,
                                             outcome(communication::FakeOutcomeKind::WriteSuccess)));

        QVERIFY(fixture.engine.runSuite(suite).accepted());
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished();
        QCOMPARE(result.status, testing::TestStatus::Pass);
        QCOMPARE(result.cases.size(), 4);
        for (const auto &testCase : result.cases) {
            QCOMPARE(testCase.status, testing::TestStatus::Pass);
        }
        QCOMPARE(result.cases[3].attempts.size(), 4);
        QCOMPARE(result.cases[3].attempts[2].purpose,
                 testing::TestStepPurpose::VerifyReadback);
        QCOMPARE(result.cases[3].attempts[3].purpose,
                 testing::TestStepPurpose::RestoreOriginal);
        for (const auto &testCase : result.cases) {
            for (const auto &attempt : testCase.attempts) {
                QVERIFY(attempt.requestId.value > 0);
                QVERIFY(!attempt.requestResult.evidence.txAdu.isEmpty());
                QVERIFY(!attempt.requestResult.evidence.rxAdu.isEmpty());
            }
        }
        QVERIFY(fixture.client.scriptConsumed());
    }

    void distinguishesAssertionFailureAndCommunicationError()
    {
        Fixture fixture;
        fixture.enterTesting();
        const auto suite = suiteOf({readCase(QStringLiteral("fail"), 0, 5),
                                    readCase(QStringLiteral("error"), 1, 6)});
        fixture.client.enqueueStep(readStep(device::PduAddress(0), 1, readSuccess({4})));
        fixture.client.enqueueStep(readStep(device::PduAddress(1), 1,
                                             outcome(communication::FakeOutcomeKind::SerialError)));
        QVERIFY(fixture.engine.runSuite(suite).accepted());
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished();
        QCOMPARE(result.status, testing::TestStatus::Error);
        QCOMPARE(result.cases[0].status, testing::TestStatus::Fail);
        QVERIFY(!result.cases[0].assertion->differences.isEmpty());
        QCOMPARE(result.cases[1].status, testing::TestStatus::Error);
        QCOMPARE(result.cases[1].error->code, testing::TestErrorCode::RequestFailed);
    }

    void everyBasicHandlerHasFailPath()
    {
        Fixture fixture;
        fixture.enterTesting();
        const auto suite = suiteOf({readCase(QStringLiteral("read"), 0, 10),
                                    writeCase(QStringLiteral("write"), 9, 600),
                                    exceptionCase(QStringLiteral("exception"), 1, 2),
                                    writeVerifyCase(QStringLiteral("verify"), 10, 650)});
        fixture.client.enqueueStep(readStep(device::PduAddress(0), 1, readSuccess({11})));
        fixture.client.enqueueStep(writeStep(device::PduAddress(9), 600,
                                             outcome(communication::FakeOutcomeKind::RemoteException,
                                                     3)));
        fixture.client.enqueueStep(readStep(device::PduAddress(1), 1, readSuccess({1})));
        fixture.client.enqueueStep(readStep(device::PduAddress(10), 1, readSuccess({610})));
        fixture.client.enqueueStep(writeStep(device::PduAddress(10), 650,
                                             outcome(communication::FakeOutcomeKind::WriteSuccess)));
        fixture.client.enqueueStep(readStep(device::PduAddress(10), 1, readSuccess({651})));
        fixture.client.enqueueStep(writeStep(device::PduAddress(10), 610,
                                             outcome(communication::FakeOutcomeKind::WriteSuccess)));
        QVERIFY(fixture.engine.runSuite(suite).accepted());
        fixture.scheduler->runUntilIdle();
        QCOMPARE(fixture.finished().status, testing::TestStatus::Fail);
        for (const auto &testCase : fixture.finished().cases) {
            QCOMPARE(testCase.status, testing::TestStatus::Fail);
            QVERIFY(testCase.assertion.has_value());
        }
    }

    void everyBasicHandlerHasCommunicationErrorPath()
    {
        Fixture fixture;
        fixture.enterTesting();
        const auto suite = suiteOf({readCase(QStringLiteral("read"), 0, 10),
                                    writeCase(QStringLiteral("write"), 9, 600),
                                    exceptionCase(QStringLiteral("exception"), 1, 2),
                                    writeVerifyCase(QStringLiteral("verify"), 10, 650)});
        fixture.client.enqueueStep(readStep(device::PduAddress(0), 1,
                                             outcome(communication::FakeOutcomeKind::SerialError)));
        fixture.client.enqueueStep(writeStep(device::PduAddress(9), 600,
                                              outcome(communication::FakeOutcomeKind::SerialError)));
        fixture.client.enqueueStep(readStep(device::PduAddress(1), 1,
                                             outcome(communication::FakeOutcomeKind::SerialError)));
        fixture.client.enqueueStep(readStep(device::PduAddress(10), 1,
                                             outcome(communication::FakeOutcomeKind::SerialError)));
        QVERIFY(fixture.engine.runSuite(suite).accepted());
        fixture.scheduler->runUntilIdle();
        QCOMPARE(fixture.finished().status, testing::TestStatus::Error);
        for (const auto &testCase : fixture.finished().cases) {
            QCOMPARE(testCase.status, testing::TestStatus::Error);
            QVERIFY(testCase.error.has_value());
        }
    }

    void retriesEligibleErrorWithNewRequestId()
    {
        Fixture fixture;
        fixture.enterTesting();
        auto testCase = readCase(QStringLiteral("retry"), 0, 7);
        testCase.timeout.request = std::chrono::milliseconds(100);
        testCase.timeout.testCase = std::chrono::milliseconds(500);
        testCase.retry.maxRetries = 1;
        testCase.retry.onErrors = {testing::RetryError::Timeout};
        fixture.client.enqueueStep(readStep(device::PduAddress(0), 1,
                                             outcome(communication::FakeOutcomeKind::Timeout),
                                             std::chrono::milliseconds(100)));
        fixture.client.enqueueStep(readStep(device::PduAddress(0), 1, readSuccess({7}),
                                             std::chrono::milliseconds(100)));
        QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished().cases.front();
        QCOMPARE(result.status, testing::TestStatus::Pass);
        QCOMPARE(result.attempts.size(), 2);
        QVERIFY(result.attempts[0].requestId != result.attempts[1].requestId);
        QVERIFY(result.attempts[1].retry);
        QCOMPARE(result.attempts[1].retryReason, QStringLiteral("timeout"));
    }

    void caseBudgetStopsFurtherRetries()
    {
        Fixture fixture;
        fixture.enterTesting();
        auto testCase = readCase(QStringLiteral("budget"), 0, 7);
        testCase.timeout.request = std::chrono::milliseconds(100);
        testCase.timeout.testCase = std::chrono::milliseconds(100);
        testCase.retry.maxRetries = 3;
        testCase.retry.onErrors = {testing::RetryError::Timeout};
        fixture.client.enqueueStep(readStep(device::PduAddress(0), 1,
                                             outcome(communication::FakeOutcomeKind::Timeout),
                                             std::chrono::milliseconds(100)));
        QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished().cases.front();
        QCOMPARE(result.status, testing::TestStatus::Error);
        QCOMPARE(result.error->code, testing::TestErrorCode::CaseTimeout);
        QCOMPARE(result.attempts.size(), 1);
    }

    void abortCancelsCurrentAndSkipsRemaining()
    {
        Fixture fixture;
        fixture.enterTesting();
        fixture.client.enqueueStep(readStep(
            device::PduAddress(0), 1, outcome(communication::FakeOutcomeKind::Pending)));
        QVERIFY(fixture.engine.runSuite(suiteOf({
            readCase(QStringLiteral("current"), 0, 1),
            readCase(QStringLiteral("remaining"), 1, 2)})).accepted());
        const auto duplicate = fixture.engine.runSuite(suiteOf({}));
        QVERIFY(!duplicate.accepted());
        QCOMPARE(duplicate.rejection->code, testing::TestErrorCode::AlreadyRunning);
        fixture.scheduler->runUntilIdle();
        QVERIFY(fixture.client.hasNonTerminalRequests());
        communication::ModbusRequestResult late;
        late.requestId = communication::RequestId{999};
        late.descriptor = communication::ReadRequestDescriptor{device::PduAddress(0), 1};
        late.state = communication::RequestState::Succeeded;
        late.evidence.requestId = late.requestId;
        late.evidence.completedUtc = fixture.epoch;
        fixture.client.requestCompleted(late);
        QVERIFY(fixture.engine.abort());
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished();
        QVERIFY(result.aborted);
        QCOMPARE(result.cases[0].status, testing::TestStatus::Error);
        QCOMPARE(result.cases[0].error->code, testing::TestErrorCode::Aborted);
        QCOMPARE(result.cases[0].attempts.size(), 1);
        QVERIFY(result.cases[0].attempts.front().requestId != late.requestId);
        QCOMPARE(result.cases[1].status, testing::TestStatus::Skipped);
        QCOMPARE(*result.cases[1].skipReason, testing::TestSkipReason::Aborted);
        QCOMPARE(fixture.controller.state(), app::AppState::ConnectedIdle);
    }

    void abortDuringCompositeStillRestoresOriginal()
    {
        Fixture fixture;
        fixture.enterTesting();
        const auto testCase = writeVerifyCase(QStringLiteral("abort-restore"), 9, 650);
        fixture.client.enqueueStep(readStep(device::PduAddress(9), 1, readSuccess({600})));
        fixture.client.enqueueStep(writeStep(device::PduAddress(9), 650,
                                             outcome(communication::FakeOutcomeKind::WriteSuccess)));
        fixture.client.enqueueStep(readStep(
            device::PduAddress(9), 1, outcome(communication::FakeOutcomeKind::Pending)));
        fixture.client.enqueueStep(writeStep(device::PduAddress(9), 600,
                                             outcome(communication::FakeOutcomeKind::WriteSuccess)));
        QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
        fixture.scheduler->runUntilIdle();
        QVERIFY(fixture.engine.abort());
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished().cases.front();
        QCOMPARE(result.status, testing::TestStatus::Error);
        QCOMPARE(result.error->code, testing::TestErrorCode::Aborted);
        QCOMPARE(result.attempts.size(), 4);
        QCOMPARE(result.attempts.back().purpose,
                 testing::TestStepPurpose::RestoreOriginal);
        QCOMPARE(result.attempts.back().requestResult.state,
                 communication::RequestState::Succeeded);
    }

    void compositeWriteFailureStillRestoresOriginal()
    {
        Fixture fixture;
        fixture.enterTesting();
        const auto testCase = writeVerifyCase(QStringLiteral("write-failure"), 9, 650);
        fixture.client.enqueueStep(readStep(device::PduAddress(9), 1, readSuccess({600})));
        fixture.client.enqueueStep(writeStep(device::PduAddress(9), 650,
                                             outcome(communication::FakeOutcomeKind::SerialError)));
        fixture.client.enqueueStep(writeStep(device::PduAddress(9), 600,
                                             outcome(communication::FakeOutcomeKind::WriteSuccess)));
        QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished().cases.front();
        QCOMPARE(result.status, testing::TestStatus::Error);
        QCOMPARE(result.error->code, testing::TestErrorCode::RequestFailed);
        QCOMPARE(result.attempts.size(), 3);
        QCOMPARE(result.attempts.back().purpose,
                 testing::TestStepPurpose::RestoreOriginal);
        QCOMPARE(result.attempts.back().requestResult.state,
                 communication::RequestState::Succeeded);
    }

    void restoreFailureOverridesAssertionFailure()
    {
        Fixture fixture;
        fixture.enterTesting();
        const auto testCase = writeVerifyCase(QStringLiteral("cleanup"), 9, 650);
        fixture.client.enqueueStep(readStep(device::PduAddress(9), 1, readSuccess({600})));
        fixture.client.enqueueStep(writeStep(device::PduAddress(9), 650,
                                             outcome(communication::FakeOutcomeKind::WriteSuccess)));
        fixture.client.enqueueStep(readStep(device::PduAddress(9), 1, readSuccess({651})));
        fixture.client.enqueueStep(writeStep(device::PduAddress(9), 600,
                                             outcome(communication::FakeOutcomeKind::SerialError)));
        QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished().cases.front();
        QCOMPARE(result.status, testing::TestStatus::Error);
        QVERIFY(result.assertion.has_value());
        QCOMPARE(result.assertion->status, testing::TestStatus::Fail);
        QVERIFY(result.cleanupError.has_value());
        QCOMPARE(result.cleanupError->code, testing::TestErrorCode::CleanupFailed);
    }

    void selectionSkipLoggingAndSnapshotsAreStable()
    {
        Fixture fixture;
        fixture.enterTesting();
        auto disabled = readCase(QStringLiteral("disabled"), 0, 1);
        disabled.enabled = false;
        const auto suite = suiteOf({disabled,
                                    readCase(QStringLiteral("selected"), 1, 2),
                                    readCase(QStringLiteral("not-selected"), 2, 3)});
        fixture.client.enqueueStep(readStep(device::PduAddress(1), 1, readSuccess({2})));
        QVERIFY(fixture.engine.runCase(suite, QStringLiteral("selected")).accepted());
        const auto runningSnapshot = fixture.results.snapshot();
        QVERIFY(runningSnapshot);
        QCOMPARE(runningSnapshot->status, testing::TestStatus::Running);
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished();
        QCOMPARE(result.cases[0].skipReason, testing::TestSkipReason::Disabled);
        QCOMPARE(result.cases[1].status, testing::TestStatus::Pass);
        QCOMPARE(result.cases[2].skipReason, testing::TestSkipReason::NotSelected);
        QCOMPARE(runningSnapshot->status, testing::TestStatus::Running);

        bool foundAttemptLog = false;
        for (const auto &entry : fixture.log.entries()) {
            if (entry.level == logging::LogLevel::Test
                && entry.event == QStringLiteral("request_attempt")) {
                foundAttemptLog = true;
                QCOMPARE(entry.requestId, result.cases[1].attempts.front().requestId.value);
                QCOMPARE(entry.metadata.value(QStringLiteral("case_id")).toString(),
                         QStringLiteral("selected"));
            }
        }
        QVERIFY(foundAttemptLog);
    }

    void explicitSkipKeepsJsonOrderAndDoesNotSubmitRequest()
    {
        Fixture fixture;
        fixture.enterTesting();
        const auto suite = suiteOf({readCase(QStringLiteral("first"), 0, 1),
                                    readCase(QStringLiteral("skip"), 1, 2),
                                    readCase(QStringLiteral("last"), 2, 3)});
        fixture.client.enqueueStep(readStep(device::PduAddress(0), 1, readSuccess({1})));
        fixture.client.enqueueStep(readStep(device::PduAddress(2), 1, readSuccess({3})));
        QVERIFY(fixture.engine.runSuite(suite).accepted());
        QVERIFY(fixture.engine.skipCase(QStringLiteral("skip")));
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished();
        QCOMPARE(result.cases[0].caseId, QStringLiteral("first"));
        QCOMPARE(result.cases[1].caseId, QStringLiteral("skip"));
        QCOMPARE(result.cases[2].caseId, QStringLiteral("last"));
        QCOMPARE(result.cases[1].status, testing::TestStatus::Skipped);
        QCOMPARE(result.cases[1].skipReason, testing::TestSkipReason::UserSelected);
        QCOMPARE(result.cases[1].attempts.size(), 0);
        QVERIFY(fixture.client.scriptConsumed());
    }

    void preexistingSessionLogErrorIsAuxiliaryOnly()
    {
        Fixture fixture;
        fixture.enterTesting();
        QVERIFY(!fixture.log.endSession().succeeded);
        fixture.client.enqueueStep(readStep(device::PduAddress(0), 1, readSuccess({1})));
        QVERIFY(fixture.engine.runSuite(
            suiteOf({readCase(QStringLiteral("logged"), 0, 1)})).accepted());
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished();
        QCOMPARE(result.status, testing::TestStatus::Pass);
        QCOMPARE(result.auxiliaryErrors.size(), 1);
        QCOMPARE(result.auxiliaryErrors.front().code,
                 testing::TestErrorCode::LoggingFailed);
    }

    void abortQueuedRequestCompletesExactlyOnce()
    {
        Fixture fixture;
        fixture.enterTesting();
        fixture.client.enqueueStep(readStep(device::PduAddress(0), 1, readSuccess({1})));
        QVERIFY(fixture.engine.runSuite(
            suiteOf({readCase(QStringLiteral("queued"), 0, 1)})).accepted());
        QVERIFY(fixture.engine.abort());
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished().cases.front();
        QCOMPARE(result.status, testing::TestStatus::Error);
        QCOMPARE(result.attempts.size(), 1);
        QCOMPARE(result.attempts.front().requestResult.state,
                 communication::RequestState::Cancelled);
        QVERIFY(!fixture.client.hasNonTerminalRequests());
        QCOMPARE(fixture.controller.state(), app::AppState::ConnectedIdle);
    }

    void destructionCancelsRequestAndReleasesOwner()
    {
        const QDateTime epoch = QDateTime::fromMSecsSinceEpoch(0, Qt::UTC);
        auto scheduler = std::make_shared<communication::ManualScheduler>();
        communication::FakeModbusClient client(scheduler, epoch);
        monitor::test::ManualMonitorScheduler monitorScheduler(scheduler);
        monitor::MonitorService monitorService(client, monitorScheduler);
        app::AppStateController controller(client, monitorService);
        testing::TestResultManager results;
        QVERIFY(controller.connectDevice(connectionConfig()).accepted());
        scheduler->runUntilIdle();
        QVERIFY(controller.startTesting().accepted());
        scheduler->runUntilIdle();
        client.enqueueStep(readStep(
            device::PduAddress(0), 1, outcome(communication::FakeOutcomeKind::Pending)));
        auto engine = std::make_unique<testing::TestEngine>(
            controller, client, results, nullptr,
            [scheduler, epoch] {
                return epoch.addMSecs(
                    std::chrono::duration_cast<std::chrono::milliseconds>(scheduler->now())
                        .count());
            });
        QVERIFY(engine->runSuite(
            suiteOf({readCase(QStringLiteral("destroy"), 0, 1)})).accepted());
        scheduler->runUntilIdle();
        QVERIFY(client.hasNonTerminalRequests());
        engine.reset();
        scheduler->runUntilIdle();
        QVERIFY(!client.hasNonTerminalRequests());
        QCOMPARE(client.activeOwner(), communication::CommunicationOwner::None);
        QCOMPARE(controller.state(), app::AppState::ConnectedIdle);
    }
};

QTEST_APPLESS_MAIN(TestEngineTest)

#include "tst_testengine.moc"

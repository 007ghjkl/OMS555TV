#include "app/AppStateController.h"
#include "communication/FakeModbusClient.h"
#include "diagnostics/CommunicationDiagnostics.h"
#include "logging/SessionLogService.h"
#include "monitor/MonitorService.h"
#include "testing/TestEngine.h"
#include "../monitor/ManualMonitorScheduler.h"

#include <QSignalSpy>
#include <QFile>
#include <QTemporaryDir>
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

testing::SequenceStep sequenceReadStep(QString id, quint16 address, qint64 expected,
                                       std::chrono::milliseconds delay = {})
{
    testing::SequenceStep step;
    step.id = std::move(id);
    step.type = testing::TestCaseType::ReadRegisters;
    step.declaredType = testing::TestCaseType::ReadRegister;
    step.delayBefore = delay;
    step.request.address = device::PduAddress(address);
    step.expected.type = testing::AssertionType::Equals;
    step.expected.value = expected;
    return step;
}

testing::TestCase sequenceCase(QString id,
                               QVector<testing::SequenceStep> steps,
                               testing::SequenceFailurePolicy failurePolicy,
                               int repeatCount = 1)
{
    testing::TestCase testCase;
    testCase.id = std::move(id);
    testCase.name = testCase.id;
    testCase.category = QStringLiteral("test");
    testCase.type = testing::TestCaseType::Sequence;
    testCase.declaredType = testCase.type;
    testCase.sequence.steps = std::move(steps);
    testCase.sequence.failurePolicy = failurePolicy;
    testCase.sequence.repeatCount = repeatCount;
    testCase.timeout.testCase = std::chrono::seconds(10);
    return testCase;
}

testing::TestCase timeoutCase(QString id, quint16 address)
{
    auto testCase = readCase(std::move(id), address, 0);
    testCase.type = testing::TestCaseType::ExpectTimeout;
    testCase.declaredType = testCase.type;
    testCase.environment = testing::ExecutionEnvironment::Fake;
    testCase.fault = testing::FaultInjection::NoResponse;
    testCase.expected.type = testing::AssertionType::ResponseTimeout;
    testCase.timeout.request = std::chrono::milliseconds(100);
    testCase.timeout.testCase = std::chrono::milliseconds(500);
    return testCase;
}

testing::TestCase consistencyCase(QString id, int sampleCount,
                                  std::chrono::milliseconds interval)
{
    auto testCase = readCase(std::move(id), 20, 0);
    testCase.type = testing::TestCaseType::Consistency;
    testCase.declaredType = testCase.type;
    testCase.request.count = 2;
    testCase.consistency.sampleCount = sampleCount;
    testCase.consistency.interval = interval;
    testCase.expected.type = testing::AssertionType::UInt32;
    testCase.expected.uint32.comparison = testing::UInt32Comparison::NonDecreasing;
    testCase.timeout.testCase = std::chrono::seconds(5);
    return testCase;
}

testing::TestCase stabilityCase(QString id,
                                std::chrono::milliseconds duration,
                                std::chrono::milliseconds interval,
                                quint64 expectedSamples,
                                int evidenceLimit = testing::defaultEvidenceSampleLimit)
{
    auto testCase = readCase(std::move(id), 30, 1);
    testCase.type = testing::TestCaseType::Stability;
    testCase.declaredType = testCase.type;
    testCase.stability.duration = duration;
    testCase.stability.interval = interval;
    testCase.stability.minimumSuccesses = expectedSamples;
    testCase.stability.allowedFailureRatePpm = 0;
    testCase.stability.evidenceSampleLimit = evidenceLimit;
    testCase.expected.type = testing::AssertionType::StabilitySummary;
    testCase.expected.stability.minimumTotal = expectedSamples;
    testCase.expected.stability.minimumSuccesses = expectedSamples;
    testCase.expected.stability.maximumFailures = 0;
    testCase.expected.stability.maximumTimeouts = 0;
    testCase.expected.stability.maximumFailureRatePpm = 0;
    testCase.expected.stability.requireValidRttSamples = true;
    testCase.expected.stability.maximumAverageRttMs = 500;
    testCase.expected.stability.maximumRttMs = 500;
    testCase.timeout.request = std::chrono::milliseconds(500);
    testCase.timeout.testCase = duration + std::chrono::seconds(1);
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
    QTemporaryDir output;
    std::shared_ptr<communication::ManualScheduler> scheduler
        = std::make_shared<communication::ManualScheduler>();
    communication::FakeModbusClient client{scheduler, epoch};
    monitor::test::ManualMonitorScheduler monitorScheduler{scheduler};
    monitor::MonitorService monitorService{client, monitorScheduler};
    app::AppStateController controller{client, monitorService};
    diagnostics::CommunicationDiagnosticsModel diagnostics{client};
    logging::SessionLogService log{diagnostics, output.path()};
    testing::TestResultManager results;
    testing::TestEngine engine{
        controller, client, results, &log, monitorScheduler};

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

    void sequenceRunsInOrderWithDelayRepeatAndImmutableSteps()
    {
        Fixture fixture;
        fixture.enterTesting();
        const auto testCase = sequenceCase(
            QStringLiteral("sequence"),
            {sequenceReadStep(QStringLiteral("first"), 0, 10),
             sequenceReadStep(QStringLiteral("second"), 1, 20,
                              std::chrono::milliseconds(50))},
            testing::SequenceFailurePolicy::StopOnFailure, 2);
        for (int repetition = 0; repetition < 2; ++repetition) {
            fixture.client.enqueueStep(
                readStep(device::PduAddress(0), 1, readSuccess({10})));
            fixture.client.enqueueStep(
                readStep(device::PduAddress(1), 1, readSuccess({20})));
        }

        QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished().cases.front();
        QCOMPARE(result.status, testing::TestStatus::Pass);
        QCOMPARE(result.steps.size(), 4);
        QCOMPARE(result.attempts.size(), 4);
        QCOMPARE(result.duration, std::chrono::milliseconds(100));
        for (qsizetype index = 0; index < result.steps.size(); ++index) {
            QCOMPARE(result.steps[index].status, testing::TestStatus::Pass);
            QCOMPARE(result.steps[index].stepIndex, index % 2);
            QCOMPARE(result.steps[index].repetition, static_cast<int>(index / 2));
            QCOMPARE(result.steps[index].attemptSequences.size(), 1);
            QCOMPARE(result.steps[index].attemptSequences.front(),
                     result.attempts[index].sequence);
        }
        QCOMPARE(result.steps[1].stepId, QStringLiteral("second"));
        QCOMPARE(result.attempts[1].logicalStepId, QStringLiteral("second"));
        QVERIFY(fixture.client.scriptConsumed());
    }

    void sequenceAppliesContinueStopCommunicationAndBudgetPolicies()
    {
        {
            Fixture fixture;
            fixture.enterTesting();
            const auto testCase = sequenceCase(
                QStringLiteral("continue"),
                {sequenceReadStep(QStringLiteral("bad"), 0, 1),
                 sequenceReadStep(QStringLiteral("good"), 1, 2)},
                testing::SequenceFailurePolicy::ContinueOnFailure);
            fixture.client.enqueueStep(
                readStep(device::PduAddress(0), 1, readSuccess({9})));
            fixture.client.enqueueStep(
                readStep(device::PduAddress(1), 1, readSuccess({2})));
            QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
            fixture.scheduler->runUntilIdle();
            const auto &result = fixture.finished().cases.front();
            QCOMPARE(result.status, testing::TestStatus::Fail);
            QCOMPARE(result.steps.size(), 2);
            QCOMPARE(result.steps[0].status, testing::TestStatus::Fail);
            QCOMPARE(result.steps[1].status, testing::TestStatus::Pass);
        }
        {
            Fixture fixture;
            fixture.enterTesting();
            const auto testCase = sequenceCase(
                QStringLiteral("stop"),
                {sequenceReadStep(QStringLiteral("bad"), 0, 1),
                 sequenceReadStep(QStringLiteral("unused"), 1, 2)},
                testing::SequenceFailurePolicy::StopOnFailure);
            fixture.client.enqueueStep(
                readStep(device::PduAddress(0), 1, readSuccess({9})));
            QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
            fixture.scheduler->runUntilIdle();
            const auto &result = fixture.finished().cases.front();
            QCOMPARE(result.status, testing::TestStatus::Fail);
            QCOMPARE(result.steps.size(), 1);
            QVERIFY(fixture.client.scriptConsumed());
        }
        {
            Fixture fixture;
            fixture.enterTesting();
            const auto testCase = sequenceCase(
                QStringLiteral("error"),
                {sequenceReadStep(QStringLiteral("serial"), 0, 1)},
                testing::SequenceFailurePolicy::StopOnFailure);
            fixture.client.enqueueStep(readStep(
                device::PduAddress(0), 1,
                outcome(communication::FakeOutcomeKind::SerialError)));
            QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
            fixture.scheduler->runUntilIdle();
            const auto &result = fixture.finished().cases.front();
            QCOMPARE(result.status, testing::TestStatus::Error);
            QCOMPARE(result.steps.front().error->code,
                     testing::TestErrorCode::RequestFailed);
        }
        {
            Fixture fixture;
            fixture.enterTesting();
            auto delayed = sequenceReadStep(QStringLiteral("late"), 0, 1,
                                            std::chrono::milliseconds(101));
            auto testCase = sequenceCase(
                QStringLiteral("budget"), {delayed},
                testing::SequenceFailurePolicy::StopOnFailure);
            testCase.timeout.testCase = std::chrono::milliseconds(100);
            QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
            fixture.scheduler->runUntilIdle();
            const auto &result = fixture.finished().cases.front();
            QCOMPARE(result.status, testing::TestStatus::Error);
            QCOMPARE(result.error->code, testing::TestErrorCode::CaseTimeout);
            QCOMPARE(result.attempts.size(), 0);
        }
    }

    void sequenceRetryRemainsInsideLogicalStep()
    {
        Fixture fixture;
        fixture.enterTesting();
        auto retried = sequenceReadStep(QStringLiteral("retried"), 0, 1);
        retried.retry.maxRetries = 1;
        retried.retry.onErrors = {testing::RetryError::Timeout};
        const auto testCase = sequenceCase(
            QStringLiteral("sequence-retry"),
            {retried, sequenceReadStep(QStringLiteral("next"), 1, 2)},
            testing::SequenceFailurePolicy::StopOnFailure);
        fixture.client.enqueueStep(readStep(
            device::PduAddress(0), 1,
            outcome(communication::FakeOutcomeKind::Timeout)));
        fixture.client.enqueueStep(
            readStep(device::PduAddress(0), 1, readSuccess({1})));
        fixture.client.enqueueStep(
            readStep(device::PduAddress(1), 1, readSuccess({2})));
        QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished().cases.front();
        QCOMPARE(result.status, testing::TestStatus::Pass);
        QCOMPARE(result.steps.size(), 2);
        QCOMPARE(result.steps.front().attemptSequences.size(), 2);
        QCOMPARE(result.attempts.size(), 3);
        QVERIFY(result.attempts[1].retry);
        QCOMPARE(result.attempts[0].logicalStepId, QStringLiteral("retried"));
        QCOMPARE(result.attempts[1].logicalStepId, QStringLiteral("retried"));
        QCOMPARE(result.attempts[2].logicalStepId, QStringLiteral("next"));
    }

    void sequenceExpectedTimeoutAndAbortDelayAreBounded()
    {
        {
            Fixture fixture;
            fixture.enterTesting();
            testing::SequenceStep timeoutStep;
            timeoutStep.id = QStringLiteral("expected-timeout");
            timeoutStep.type = testing::TestCaseType::ExpectTimeout;
            timeoutStep.declaredType = timeoutStep.type;
            timeoutStep.request.address = device::PduAddress(4);
            timeoutStep.expected.type = testing::AssertionType::ResponseTimeout;
            auto testCase = sequenceCase(
                QStringLiteral("sequence-timeout"), {timeoutStep},
                testing::SequenceFailurePolicy::StopOnFailure);
            testCase.timeout.request = std::chrono::milliseconds(100);
            fixture.client.enqueueStep(readStep(
                device::PduAddress(4), 1,
                outcome(communication::FakeOutcomeKind::Timeout),
                std::chrono::milliseconds(100)));
            QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
            fixture.scheduler->runUntilIdle();
            const auto &result = fixture.finished().cases.front();
            QCOMPARE(result.status, testing::TestStatus::Pass);
            QCOMPARE(result.steps.front().status, testing::TestStatus::Pass);
            QCOMPARE(result.steps.front().actual->type,
                     testing::ActualResultType::ResponseTimeout);
        }
        {
            Fixture fixture;
            fixture.enterTesting();
            const auto testCase = sequenceCase(
                QStringLiteral("abort-delay"),
                {sequenceReadStep(QStringLiteral("never-started"), 0, 1,
                                  std::chrono::seconds(1))},
                testing::SequenceFailurePolicy::StopOnFailure);
            QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
            QVERIFY(fixture.engine.abort());
            fixture.scheduler->runUntilIdle();
            const auto &result = fixture.finished().cases.front();
            QCOMPARE(result.status, testing::TestStatus::Error);
            QCOMPARE(result.error->code, testing::TestErrorCode::Aborted);
            QCOMPARE(result.attempts.size(), 0);
            QCOMPARE(fixture.controller.state(), app::AppState::ConnectedIdle);
            QVERIFY(fixture.client.scriptConsumed());
        }
    }

    void expectedTimeoutUsesExactStructuredClassification()
    {
        Fixture fixture;
        fixture.enterTesting();
        const auto suite = suiteOf({timeoutCase(QStringLiteral("timeout"), 0),
                                    timeoutCase(QStringLiteral("normal"), 1),
                                    timeoutCase(QStringLiteral("remote"), 2),
                                    timeoutCase(QStringLiteral("serial"), 3)});
        fixture.client.enqueueStep(readStep(
            device::PduAddress(0), 1,
            outcome(communication::FakeOutcomeKind::Timeout),
            std::chrono::milliseconds(100)));
        fixture.client.enqueueStep(readStep(
            device::PduAddress(1), 1, readSuccess({7}),
            std::chrono::milliseconds(100)));
        fixture.client.enqueueStep(readStep(
            device::PduAddress(2), 1,
            outcome(communication::FakeOutcomeKind::RemoteException, 2),
            std::chrono::milliseconds(100)));
        fixture.client.enqueueStep(readStep(
            device::PduAddress(3), 1,
            outcome(communication::FakeOutcomeKind::SerialError),
            std::chrono::milliseconds(100)));

        QVERIFY(fixture.engine.runSuite(suite).accepted());
        fixture.scheduler->runUntilIdle();
        const auto &cases = fixture.finished().cases;
        QCOMPARE(cases[0].status, testing::TestStatus::Pass);
        QCOMPARE(cases[1].status, testing::TestStatus::Fail);
        QCOMPARE(cases[2].status, testing::TestStatus::Fail);
        QCOMPARE(cases[3].status, testing::TestStatus::Error);
        QCOMPARE(cases[3].error->communicationError->category,
                 communication::ErrorCategory::Serial);
    }

    void consistencyCollectsUInt32SamplesAcrossVirtualIntervals()
    {
        Fixture fixture;
        fixture.enterTesting();
        const auto testCase = consistencyCase(
            QStringLiteral("consistent"), 3, std::chrono::milliseconds(25));
        fixture.client.enqueueStep(
            readStep(device::PduAddress(20), 2, readSuccess({1, 0})));
        fixture.client.enqueueStep(
            readStep(device::PduAddress(20), 2, readSuccess({2, 0})));
        fixture.client.enqueueStep(
            readStep(device::PduAddress(20), 2, readSuccess({2, 0})));
        QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished().cases.front();
        QCOMPARE(result.status, testing::TestStatus::Pass);
        QCOMPARE(result.actual->registerSamples.size(), 3);
        QCOMPARE(result.duration, std::chrono::milliseconds(50));
        QCOMPARE(result.attempts.size(), 3);
    }

    void stabilityAggregatesFailuresTimeoutsRttAndBoundsEvidence()
    {
        Fixture fixture;
        fixture.enterTesting();
        auto testCase = stabilityCase(
            QStringLiteral("stability"), std::chrono::milliseconds(1000),
            std::chrono::milliseconds(100), 8, 8);
        testCase.expected.stability.minimumTotal = 10;
        testCase.expected.stability.minimumSuccesses = 8;
        testCase.expected.stability.maximumFailures = 1;
        testCase.expected.stability.maximumTimeouts = 1;
        testCase.expected.stability.maximumFailureRatePpm = 200000;
        testCase.stability.allowedFailureRatePpm = 200000;
        for (int index = 0; index < 10; ++index) {
            communication::FakeStep step;
            if (index == 3) {
                step = readStep(device::PduAddress(30), 1,
                                outcome(communication::FakeOutcomeKind::Timeout));
                step.virtualDelay = std::chrono::milliseconds(5);
            } else if (index == 7) {
                step = readStep(device::PduAddress(30), 1,
                                outcome(communication::FakeOutcomeKind::CrcMismatch));
                step.virtualDelay = std::chrono::milliseconds(5);
            } else {
                step = readStep(device::PduAddress(30), 1, readSuccess({1}));
                step.virtualDelay = std::chrono::milliseconds(5);
            }
            fixture.client.enqueueStep(std::move(step));
        }
        QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished().cases.front();
        QCOMPARE(result.status, testing::TestStatus::Pass);
        QVERIFY(result.stability.has_value());
        QCOMPARE(result.stability->total, quint64(10));
        QCOMPARE(result.stability->successes, quint64(8));
        QCOMPARE(result.stability->failures, quint64(1));
        QCOMPARE(result.stability->timeouts, quint64(1));
        QCOMPARE(result.stability->validRttSamples, quint64(10));
        QCOMPARE(result.stability->missingRttSamples, quint64(0));
        QCOMPARE(result.evidenceRetention.totalAttempts, quint64(10));
        QVERIFY(result.attempts.size() <= 8);
        QVERIFY(result.evidenceRetention.droppedAttempts > 0);
        QCOMPARE(result.attempts.front().sequence, quint64(1));
        QCOMPARE(result.attempts.back().sequence, quint64(10));
        QCOMPARE(result.duration, std::chrono::milliseconds(1000));
    }

    void stabilityOverrunDoesNotBackfillAndAllowsLastCompletion()
    {
        Fixture fixture;
        fixture.enterTesting();
        auto testCase = stabilityCase(
            QStringLiteral("overrun"), std::chrono::minutes(10),
            std::chrono::seconds(50), 11, 8);
        testCase.timeout.request = std::chrono::seconds(60);
        testCase.timeout.testCase = std::chrono::minutes(11);
        testCase.expected.stability.maximumAverageRttMs = 60000;
        testCase.expected.stability.maximumRttMs = 60000;
        for (int index = 0; index < 11; ++index) {
            auto step = readStep(device::PduAddress(30), 1, readSuccess({1}),
                                 std::chrono::seconds(60));
            step.virtualDelay = std::chrono::seconds(55);
            fixture.client.enqueueStep(std::move(step));
        }
        QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished().cases.front();
        QVERIFY2(result.status == testing::TestStatus::Pass,
                 qPrintable(result.error ? result.error->diagnostic
                                         : QStringLiteral("无错误详情")));
        QCOMPARE(result.stability->total, quint64(11));
        QCOMPARE(result.duration, std::chrono::minutes(10) + std::chrono::seconds(5));
        QVERIFY(fixture.client.scriptConsumed());
    }

    void stabilityTracksZeroValidRttWithoutUsingZeroSamples()
    {
        Fixture fixture;
        fixture.enterTesting();
        auto testCase = stabilityCase(
            QStringLiteral("missing-rtt"), std::chrono::minutes(10),
            std::chrono::minutes(1), 10, 8);
        testCase.expected.stability.requireValidRttSamples = false;
        for (int index = 0; index < 10; ++index) {
            auto step = readStep(device::PduAddress(30), 1, readSuccess({1}));
            step.omitRtt = true;
            fixture.client.enqueueStep(std::move(step));
        }
        QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
        fixture.scheduler->runUntilIdle();
        const auto &stats = *fixture.finished().cases.front().stability;
        QCOMPARE(stats.total, quint64(10));
        QCOMPARE(stats.validRttSamples, quint64(0));
        QCOMPARE(stats.missingRttSamples, quint64(10));
        QVERIFY(!stats.minimumRttMs.has_value());
        QVERIFY(!stats.averageRttMs.has_value());
        QVERIFY(!stats.maximumRttMs.has_value());
        QCOMPARE(fixture.finished().cases.front().status, testing::TestStatus::Pass);
    }

    void stabilityJsonlKeepsAllAttemptsAndSessionAssociation()
    {
        Fixture fixture;
        QVERIFY(fixture.output.isValid());
        const auto session = fixture.log.startSession();
        QVERIFY(session.succeeded);
        fixture.enterTesting();
        const auto testCase = stabilityCase(
            QStringLiteral("logged-stability"), std::chrono::minutes(10),
            std::chrono::minutes(1), 10, 8);
        for (int index = 0; index < 10; ++index) {
            fixture.client.enqueueStep(
                readStep(device::PduAddress(30), 1, readSuccess({1})));
        }
        QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished();
        QCOMPARE(result.sessionId, session.sessionId);
        QCOMPARE(result.cases.front().sessionId, session.sessionId);
        QVERIFY(result.cases.front().attempts.size() <= 8);
        QVERIFY(fixture.log.endSession().succeeded);

        QFile file(session.filePath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray jsonl = file.readAll();
        QCOMPARE(jsonl.count("\"event\":\"request_attempt\""), 10);
        QCOMPARE(jsonl.count("\"event\":\"request_completed\""), 10);
    }

    void stabilityVirtualLongRuns_data()
    {
        QTest::addColumn<qint64>("durationMs");
        QTest::addColumn<quint64>("sampleCount");
        QTest::newRow("10min") << qint64(600000) << quint64(10);
        QTest::newRow("1h") << qint64(3600000) << quint64(60);
        QTest::newRow("8h") << qint64(28800000) << quint64(480);
        QTest::newRow("24h") << qint64(86400000) << quint64(1440);
    }

    void stabilityVirtualLongRuns()
    {
        QFETCH(qint64, durationMs);
        QFETCH(quint64, sampleCount);
        Fixture fixture;
        fixture.enterTesting();
        const auto testCase = stabilityCase(
            QStringLiteral("long"), std::chrono::milliseconds(durationMs),
            std::chrono::minutes(1), sampleCount, 8);
        for (quint64 index = 0; index < sampleCount; ++index) {
            auto step = readStep(device::PduAddress(30), 1, readSuccess({1}));
            step.virtualDelay = std::chrono::milliseconds(1);
            fixture.client.enqueueStep(std::move(step));
        }
        QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished().cases.front();
        QCOMPARE(result.status, testing::TestStatus::Pass);
        QCOMPARE(result.stability->total, sampleCount);
        QCOMPARE(result.duration, std::chrono::milliseconds(durationMs));
        QVERIFY(result.attempts.size() <= 8);
        QCOMPARE(result.evidenceRetention.totalAttempts, sampleCount);
        QCOMPARE(result.evidenceRetention.retainedAttempts,
                 static_cast<quint64>(result.attempts.size()));
        QCOMPARE(result.evidenceRetention.droppedAttempts,
                 sampleCount - static_cast<quint64>(result.attempts.size()));
        QVERIFY(fixture.client.scriptConsumed());
    }

    void abortDuringStabilityDelayStartsNoFurtherRequest()
    {
        Fixture fixture;
        fixture.enterTesting();
        const auto testCase = stabilityCase(
            QStringLiteral("abort-stability"), std::chrono::minutes(10),
            std::chrono::minutes(1), 10, 8);
        auto first = readStep(device::PduAddress(30), 1, readSuccess({1}));
        first.virtualDelay = std::chrono::milliseconds(1);
        fixture.client.enqueueStep(std::move(first));
        QVERIFY(fixture.engine.runSuite(suiteOf({testCase})).accepted());
        fixture.scheduler->advanceBy(std::chrono::nanoseconds::zero());
        fixture.scheduler->advanceBy(std::chrono::milliseconds(1));
        QVERIFY(fixture.engine.abort());
        fixture.scheduler->runUntilIdle();
        const auto &result = fixture.finished().cases.front();
        QCOMPARE(result.status, testing::TestStatus::Error);
        QCOMPARE(result.error->code, testing::TestErrorCode::Aborted);
        QCOMPARE(result.stability->total, quint64(1));
        QCOMPARE(result.evidenceRetention.totalAttempts, quint64(1));
        QCOMPARE(fixture.controller.state(), app::AppState::ConnectedIdle);
        QVERIFY(fixture.client.scriptConsumed());
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

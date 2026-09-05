#include "app/AppStateController.h"
#include "communication/FakeModbusClient.h"
#include "diagnostics/CommunicationDiagnostics.h"
#include "logging/SessionLogService.h"
#include "monitor/MonitorService.h"
#include "testing/GuidedTestCoordinator.h"
#include "testing/TestCaseLoader.h"
#include "testing/TestEngine.h"
#include "testing/TestResultManager.h"
#include "../monitor/ManualMonitorScheduler.h"

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

communication::FakeOutcome outcome(communication::FakeOutcomeKind kind,
                                   QVector<quint16> values = {})
{
    communication::FakeOutcome result;
    result.kind = kind;
    result.readValues = std::move(values);
    return result;
}

communication::FakeStep probeStep(communication::FakeOutcome result,
                                  int timeoutMs = 500,
                                  int delayMs = 0)
{
    return {{communication::CommunicationOwner::Testing,
             communication::ReadRequestDescriptor{device::PduAddress(16), 1},
             std::chrono::milliseconds(timeoutMs)},
            std::move(result), std::chrono::milliseconds(delayMs), {}, {}, {}};
}

testing::TestSuite loadSuite()
{
    QFile file(QStringLiteral(OMS555TV_GUIDED_V3_FIXTURE));
    if (!file.open(QIODevice::ReadOnly)) return {};
    auto loaded = testing::TestCaseLoader::load(file.readAll());
    return loaded.suite.value_or(testing::TestSuite{});
}

struct Rig {
    QTemporaryDir output;
    std::shared_ptr<communication::ManualScheduler> clock =
        std::make_shared<communication::ManualScheduler>();
    communication::FakeModbusClient client{clock};
    monitor::test::ManualMonitorScheduler scheduler{clock};
    monitor::MonitorService monitor{client, scheduler};
    app::AppStateController appState{client, monitor};
    diagnostics::CommunicationDiagnosticsModel diagnostics{client};
    logging::SessionLogService log{diagnostics, output.path()};
    testing::TestResultManager results;
    testing::TestEngine engine{appState, client, results, &log, scheduler};
    testing::GuidedTestCoordinator coordinator{
        appState, engine, results, &log, scheduler};

    void enterTesting()
    {
        QVERIFY(output.isValid());
        QVERIFY(log.startSession({{QStringLiteral("test"), QStringLiteral("guided")}})
                    .succeeded);
        QVERIFY(appState.connectDevice(connectionConfig()).accepted());
        clock->advanceBy(std::chrono::milliseconds(0));
        QVERIFY(appState.startTesting().accepted());
        clock->advanceBy(std::chrono::milliseconds(0));
        QCOMPARE(appState.state(), app::AppState::Testing);
    }

    testing::GuidedActionCommand action(testing::GuidedOperatorAction value,
                                        QString token = {}) const
    {
        const auto view = coordinator.view();
        return {view.runId.value, view.caseId, view.stepId,
                token.isEmpty() ? view.token : std::move(token), value, {}};
    }
};

} // namespace

class GuidedTestCoordinatorTest final : public QObject
{
    Q_OBJECT

private slots:
    void successKeepsOwnerAndPublishesEvidenceAndLogs()
    {
        Rig rig;
        rig.enterTesting();
        auto suite = loadSuite();
        QCOMPARE(suite.schemaVersion, testing::testSuiteSchemaVersionV3);
        for (int index = 0; index < 3; ++index) {
            rig.client.enqueueStep(probeStep(outcome(
                communication::FakeOutcomeKind::Timeout)));
        }
        for (int index = 0; index < 3; ++index) {
            rig.client.enqueueStep(probeStep(outcome(
                communication::FakeOutcomeKind::ReadSuccess, {42})));
        }

        QVERIFY(rig.coordinator.runSuite(suite));
        QCOMPARE(rig.coordinator.view().state,
                 testing::GuidedRunState::WaitingForDisconnectConfirmation);
        QCOMPARE(rig.client.activeOwner(), communication::CommunicationOwner::Testing);
        QVERIFY(!rig.client.hasNonTerminalRequests());

        const auto firstToken = rig.coordinator.view().token;
        auto wrong = rig.action(testing::GuidedOperatorAction::Confirm,
                                QStringLiteral("stale-token"));
        QCOMPARE(rig.coordinator.submitAction(wrong),
                 testing::GuidedActionRejection::TokenMismatch);
        QCOMPARE(rig.coordinator.view().token, firstToken);
        QCOMPARE(rig.coordinator.submitAction(
                     rig.action(testing::GuidedOperatorAction::Confirm)),
                 testing::GuidedActionRejection::None);
        QCOMPARE(rig.coordinator.submitAction(
                     {1, QStringLiteral("TC-G001"), QStringLiteral("disconnect"),
                      firstToken, testing::GuidedOperatorAction::Confirm, {}}),
                 testing::GuidedActionRejection::NotAwaitingAction);

        rig.clock->advanceBy(std::chrono::milliseconds(500));
        rig.clock->advanceBy(std::chrono::milliseconds(500));
        rig.clock->advanceBy(std::chrono::milliseconds(500));
        rig.clock->advanceBy(std::chrono::milliseconds(500));
        rig.clock->advanceBy(std::chrono::milliseconds(500));
        rig.clock->advanceBy(std::chrono::milliseconds(500));
        QCOMPARE(rig.coordinator.view().state,
                 testing::GuidedRunState::WaitingForReconnectConfirmation);
        QCOMPARE(rig.client.activeOwner(), communication::CommunicationOwner::Testing);

        QCOMPARE(rig.coordinator.submitAction(
                     rig.action(testing::GuidedOperatorAction::Confirm)),
                 testing::GuidedActionRejection::None);
        rig.clock->advanceBy(std::chrono::milliseconds(0));
        rig.clock->advanceBy(std::chrono::milliseconds(500));
        rig.clock->advanceBy(std::chrono::milliseconds(500));
        QCOMPARE(rig.appState.state(), app::AppState::ConnectedIdle);
        QVERIFY(!rig.coordinator.active());
        QVERIFY(rig.client.scriptConsumed());

        const auto snapshot = rig.results.snapshot();
        QVERIFY(snapshot);
        QCOMPARE(snapshot->status, testing::TestStatus::Pass);
        QCOMPARE(snapshot->cases.front().status, testing::TestStatus::Pass);
        QVERIFY(snapshot->cases.front().guidedRecovery);
        const auto &guided = *snapshot->cases.front().guidedRecovery;
        QCOMPARE(guided.terminalReason, testing::GuidedTerminalReason::Pass);
        QVERIFY(guided.physicalLinkRestored);
        QCOMPARE(guided.operatorActions.size(), 2);
        QCOMPARE(guided.observations.size(), 2);
        QCOMPARE(snapshot->cases.front().attempts.size(), 6);
        QVERIFY(guided.recoveryTiming);
        QCOMPARE(guided.recoveryTiming->toFirstSuccessfulResponse.count(), 0);
        QCOMPARE(guided.recoveryTiming->toStableRecovery.count(), 1000);
        for (const auto &attempt : snapshot->cases.front().attempts) {
            QVERIFY(attempt.requestId.value > 0);
            QVERIFY(!attempt.requestResult.evidence.txAdu.isEmpty());
            QVERIFY(attempt.requestResult.evidence.rtt.has_value());
        }

        QSet<QString> events;
        QSet<quint64> loggedRequests;
        for (const auto &entry : rig.log.entries()) {
            if (entry.level != logging::LogLevel::Test) continue;
            events.insert(entry.event);
            if (entry.requestId) loggedRequests.insert(*entry.requestId);
        }
        for (const auto &event : {QStringLiteral("prompt_shown"),
                                  QStringLiteral("operator_confirmed"),
                                  QStringLiteral("observation_started"),
                                  QStringLiteral("observation_finished"),
                                  QStringLiteral("guided_case_finished")}) {
            QVERIFY(events.contains(event));
        }
        QCOMPARE(loggedRequests.size(), 6);
    }

    void cancellationConsumesTokenAndRequiresRestoration()
    {
        Rig rig;
        rig.enterTesting();
        const auto suite = loadSuite();
        QVERIFY(rig.coordinator.runSuite(suite));
        const auto command = rig.action(testing::GuidedOperatorAction::Cancel);
        QCOMPARE(rig.coordinator.submitAction(command),
                 testing::GuidedActionRejection::None);
        rig.clock->advanceBy(std::chrono::milliseconds(0));
        QCOMPARE(rig.coordinator.submitAction(command),
                 testing::GuidedActionRejection::NotAwaitingAction);
        QCOMPARE(rig.appState.state(), app::AppState::ConnectedIdle);
        const auto snapshot = rig.results.snapshot();
        QCOMPARE(snapshot->status, testing::TestStatus::Skipped);
        QCOMPARE(snapshot->cases.front().status, testing::TestStatus::Skipped);
        QVERIFY(snapshot->cases.front().guidedRecovery->recoveryInstructionRequired);
        QVERIFY(!snapshot->cases.front().guidedRecovery->physicalLinkRestored);
        QVERIFY(!rig.coordinator.view().restorationReminder.isEmpty());
        QVERIFY(!rig.client.hasNonTerminalRequests());
    }

    void deadlinesAndFatalErrorsProduceStableTerminalStates()
    {
        {
            Rig rig;
            rig.enterTesting();
            auto suite = loadSuite();
            suite.cases[0].guidedRecovery.steps[0].operatorStep->waitTimeout =
                std::chrono::milliseconds(1000);
            QVERIFY(rig.coordinator.runSuite(suite));
            rig.clock->advanceBy(std::chrono::milliseconds(1000));
            QCOMPARE(rig.results.snapshot()->cases.front().guidedRecovery->terminalReason,
                     testing::GuidedTerminalReason::OperatorTimeout);
            QCOMPARE(rig.results.snapshot()->status, testing::TestStatus::Error);
            QCOMPARE(rig.appState.state(), app::AppState::ConnectedIdle);
        }
        {
            Rig rig;
            rig.enterTesting();
            auto suite = loadSuite();
            auto &outage = *suite.cases[0].guidedRecovery.steps[1].observationStep;
            outage.deadline = std::chrono::milliseconds(100);
            outage.interval = std::chrono::milliseconds(50);
            rig.client.enqueueStep(probeStep(
                outcome(communication::FakeOutcomeKind::ReadSuccess, {1}), 100));
            QVERIFY(rig.coordinator.runSuite(suite));
            QCOMPARE(rig.coordinator.submitAction(
                         rig.action(testing::GuidedOperatorAction::Confirm)),
                     testing::GuidedActionRejection::None);
            rig.clock->advanceBy(std::chrono::milliseconds(100));
            QCOMPARE(rig.results.snapshot()->cases.front().guidedRecovery->terminalReason,
                     testing::GuidedTerminalReason::OutageNotDetected);
            QCOMPARE(rig.results.snapshot()->status, testing::TestStatus::Fail);
        }
        {
            Rig rig;
            rig.enterTesting();
            rig.client.enqueueStep(probeStep(
                outcome(communication::FakeOutcomeKind::CrcMismatch)));
            const auto suite = loadSuite();
            QVERIFY(rig.coordinator.runSuite(suite));
            QCOMPARE(rig.coordinator.submitAction(
                         rig.action(testing::GuidedOperatorAction::Confirm)),
                     testing::GuidedActionRejection::None);
            rig.clock->advanceBy(std::chrono::milliseconds(0));
            QCOMPARE(rig.results.snapshot()->cases.front().guidedRecovery->terminalReason,
                     testing::GuidedTerminalReason::FatalCommunicationError);
            QCOMPARE(rig.results.snapshot()->status, testing::TestStatus::Error);
        }
    }

    void abortCancelsPendingProbeAndIgnoresLateCompletion()
    {
        Rig rig;
        rig.enterTesting();
        rig.client.enqueueStep(probeStep(
            outcome(communication::FakeOutcomeKind::Pending)));
        const auto suite = loadSuite();
        QVERIFY(rig.coordinator.runSuite(suite));
        QCOMPARE(rig.coordinator.submitAction(
                     rig.action(testing::GuidedOperatorAction::Confirm)),
                 testing::GuidedActionRejection::None);
        QVERIFY(rig.client.hasNonTerminalRequests());
        QVERIFY(rig.coordinator.abort());
        rig.clock->advanceBy(std::chrono::milliseconds(0));
        QVERIFY(!rig.client.hasNonTerminalRequests());
        QCOMPARE(rig.results.snapshot()->cases.front().guidedRecovery->terminalReason,
                 testing::GuidedTerminalReason::UserAborted);
        QCOMPARE(rig.appState.state(), app::AppState::ConnectedIdle);
    }

    void wrongBusinessResponseResetsRecoveryAndEndsAsTimeout()
    {
        Rig rig;
        rig.enterTesting();
        auto suite = loadSuite();
        auto &outage = *suite.cases[0].guidedRecovery.steps[1].observationStep;
        outage.consecutiveMatches = 1;
        outage.deadline = std::chrono::milliseconds(100);
        outage.interval = std::chrono::milliseconds(50);
        auto &recovery = *suite.cases[0].guidedRecovery.steps[3].observationStep;
        recovery.consecutiveMatches = 2;
        recovery.deadline = std::chrono::milliseconds(100);
        recovery.interval = std::chrono::milliseconds(50);
        recovery.businessAssertion->type = testing::AssertionType::Equals;
        recovery.businessAssertion->value = 7;
        rig.client.enqueueStep(probeStep(
            outcome(communication::FakeOutcomeKind::Timeout), 100, 1));
        rig.client.enqueueStep(probeStep(
            outcome(communication::FakeOutcomeKind::ReadSuccess, {42}), 100));

        QVERIFY(rig.coordinator.runSuite(suite));
        QCOMPARE(rig.coordinator.submitAction(
                     rig.action(testing::GuidedOperatorAction::Confirm)),
                 testing::GuidedActionRejection::None);
        rig.clock->advanceBy(std::chrono::milliseconds(0));
        rig.clock->advanceBy(std::chrono::milliseconds(1));
        QCOMPARE(rig.coordinator.view().state,
                 testing::GuidedRunState::WaitingForReconnectConfirmation);
        QCOMPARE(rig.coordinator.submitAction(
                     rig.action(testing::GuidedOperatorAction::Confirm)),
                 testing::GuidedActionRejection::None);
        rig.clock->advanceBy(std::chrono::milliseconds(0));
        QCOMPARE(rig.coordinator.view().consecutiveMatches, 0);
        rig.clock->advanceBy(std::chrono::milliseconds(100));

        const auto snapshot = rig.results.snapshot();
        QCOMPARE(snapshot->status, testing::TestStatus::Fail);
        QCOMPARE(snapshot->cases.front().guidedRecovery->terminalReason,
                 testing::GuidedTerminalReason::RecoveryTimeout);
        QCOMPARE(snapshot->cases.front().guidedRecovery->observations.last()
                     .achievedConsecutiveMatches, 0);
        QVERIFY(!snapshot->cases.front().guidedRecovery->recoveryTiming);
        QCOMPARE(rig.appState.state(), app::AppState::ConnectedIdle);
    }
};

QTEST_GUILESS_MAIN(GuidedTestCoordinatorTest)
#include "tst_guidedtestcoordinator.moc"

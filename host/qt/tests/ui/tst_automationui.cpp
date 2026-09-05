#include "app/AppStateController.h"
#include "app/MainWindow.h"
#include "communication/FakeModbusClient.h"
#include "configuration/ConfigurationService.h"
#include "diagnostics/CommunicationDiagnostics.h"
#include "logging/SessionLogService.h"
#include "monitor/MonitorService.h"
#include "testing/TestAutomationController.h"
#include "testing/TestEngine.h"
#include "testing/TestResultManager.h"
#include "ui/MonitoringViewModel.h"
#include "../monitor/ManualMonitorScheduler.h"

#include <QCheckBox>
#include <QFile>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace oms555tv;

namespace {

class EmptyPortCatalog final : public ui::ISerialPortCatalog
{
public:
    QVector<ui::SerialPortEntry> availablePorts() const override { return {}; }
};

communication::ModbusConnectionConfig connectionConfig()
{
    communication::ModbusConnectionConfig config;
    config.serial.portName = QStringLiteral("FAKE");
    return config;
}

communication::FakeStep readStep(
    communication::CommunicationOwner owner,
    quint16 address,
    communication::FakeOutcome outcome,
    int timeoutMs = 500)
{
    return {{owner,
             communication::ReadRequestDescriptor{device::PduAddress(address), 1},
             std::chrono::milliseconds(timeoutMs)},
            std::move(outcome), {}, {}, {}};
}

communication::FakeStep readRegistersStep(
    communication::CommunicationOwner owner,
    quint16 address,
    quint16 count,
    communication::FakeOutcome outcome,
    int timeoutMs = 500)
{
    return {{owner,
             communication::ReadRequestDescriptor{device::PduAddress(address), count},
             std::chrono::milliseconds(timeoutMs)},
            std::move(outcome), {}, {}, {}};
}

communication::FakeStep monitorReadStep(communication::FakeOutcome outcome)
{
    return {{communication::CommunicationOwner::Monitor,
             communication::ReadRequestDescriptor{device::PduAddress(0), 5},
             std::chrono::milliseconds(500)},
            std::move(outcome), {}, {}, {}};
}

communication::FakeOutcome readSuccess(quint16 value)
{
    communication::FakeOutcome outcome;
    outcome.kind = communication::FakeOutcomeKind::ReadSuccess;
    outcome.readValues = {value};
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

QString writeFile(QTemporaryDir &directory, const QString &name, const QByteArray &contents)
{
    const QString path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) return {};
    file.close();
    return path;
}

QByteArray statusSuite()
{
    return R"JSON({
  "schema_version": 1,
  "id": "ui-status",
  "name": "UI 状态套件",
  "cases": [
    {"id":"pass","name":"通过","category":"functional","type":"read_register","request":{"function":3,"address":0,"count":1},"expected":{"type":"range","min":0,"max":100}},
    {"id":"fail","name":"断言失败","category":"functional","type":"read_register","request":{"function":3,"address":1,"count":1},"expected":{"type":"equals","value":5}},
    {"id":"error","name":"通信错误","category":"functional","type":"read_register","request":{"function":3,"address":2,"count":1},"expected":{"type":"equals","value":7},"timeout":{"request_ms":10}},
    {"id":"disabled","name":"禁用","category":"functional","type":"read_register","enabled":false,"request":{"function":3,"address":3,"count":1},"expected":{"type":"equals","value":8}}
  ]
})JSON";
}

QByteArray selectionSuite()
{
    return R"JSON({
  "schema_version": 1,
  "id": "ui-selection",
  "name": "UI 选择套件",
  "cases": [
    {"id":"one","name":"第一条","category":"functional","type":"read_register","request":{"function":3,"address":0,"count":1},"expected":{"type":"equals","value":1}},
    {"id":"two","name":"第二条","category":"functional","type":"read_register","request":{"function":3,"address":1,"count":1},"expected":{"type":"equals","value":2}},
    {"id":"three","name":"第三条","category":"functional","type":"read_register","request":{"function":3,"address":2,"count":1},"expected":{"type":"equals","value":3}}
  ]
})JSON";
}

QByteArray guidedSuite()
{
    return R"JSON({
  "schema_version": 3,
  "id": "ui-guided",
  "name": "UI 引导套件",
  "cases": [{
    "id": "guided", "name": "RS485 引导恢复", "category": "recovery",
    "environment": "fake", "type": "guided_recovery",
    "timeout": {"request_ms": 50, "case_ms": 5000},
    "steps": [
      {"id":"disconnect","type":"operator_prompt","purpose":"disconnect_rs485",
       "title":"断开 A/B","instruction":"断开开发板侧 A/B 后确认。","safety_notice":"不要触碰电源端子。",
       "allowed_actions":["confirm","cancel"],"wait_timeout_ms":1000,
       "cancel_recovery_instruction":"按原极性恢复 A/B。"},
      {"id":"outage","type":"observe_outage","probe":{"function":3,"address":16,"count":1},
       "interval_ms":50,"deadline_ms":100,"consecutive_matches":1},
      {"id":"reconnect","type":"operator_prompt","purpose":"reconnect_rs485",
       "title":"恢复 A/B","instruction":"按原极性接回 A/B 后确认。","safety_notice":"确认 A/B 极性。",
       "allowed_actions":["confirm","cancel"],"wait_timeout_ms":1000,
       "cancel_recovery_instruction":"检查并恢复正确 A/B 接线。"},
      {"id":"recovery","type":"observe_recovery","probe":{"function":3,"address":16,"count":1},
       "interval_ms":50,"deadline_ms":100,"consecutive_matches":1,
       "business_assertion":{"type":"equals","value":42}}
    ]
  }]
})JSON";
}

struct UiRig {
    QTemporaryDir output;
    std::shared_ptr<communication::ManualScheduler> scheduler =
        std::make_shared<communication::ManualScheduler>();
    communication::FakeModbusClient client{scheduler};
    monitor::test::ManualMonitorScheduler monitorScheduler{scheduler};
    monitor::MonitorService monitorService{client, monitorScheduler};
    app::AppStateController appState{client, monitorService};
    EmptyPortCatalog ports;
    ui::MonitoringViewModel monitoring{appState, monitorService, ports};
    configuration::ConfigurationService configuration{appState, client};
    diagnostics::CommunicationDiagnosticsModel diagnostics{client};
    logging::SessionLogService sessionLog{diagnostics, output.path()};
    testing::TestResultManager results;
    testing::TestEngine engine{
        appState, client, results, &sessionLog, monitorScheduler};
    testing::TestAutomationController automation{appState, engine, results};
    MainWindow window{monitoring, configuration, diagnostics, sessionLog, automation};

    UiRig()
    {
        QVERIFY(output.isValid());
        window.show();
        QCoreApplication::processEvents();
    }

    void connectDevice()
    {
        QVERIFY(appState.connectDevice(connectionConfig()).accepted());
        scheduler->runUntilIdle();
        QCOMPARE(appState.state(), app::AppState::ConnectedIdle);
    }

    void load(const QString &path, int expectedRows)
    {
        auto *pathEdit = window.findChild<QLineEdit *>(QStringLiteral("testSuitePath"));
        auto *loadButton = window.findChild<QPushButton *>(QStringLiteral("loadTestSuiteButton"));
        QVERIFY(pathEdit && loadButton);
        pathEdit->setText(path);
        loadButton->click();
        QTRY_COMPARE_WITH_TIMEOUT(automation.state(), testing::TestAutomationState::Idle, 2000);
        auto *table = window.findChild<QTableWidget *>(QStringLiteral("testCaseTable"));
        QVERIFY(table);
        QCOMPARE(table->rowCount(), expectedRows);
    }

    int rowFor(const QString &id) const
    {
        auto *table = window.findChild<QTableWidget *>(QStringLiteral("testCaseTable"));
        for (int row = 0; table && row < table->rowCount(); ++row) {
            if (table->item(row, 0)->text() == id) return row;
        }
        return -1;
    }
};

template<typename Widget>
Widget *widget(MainWindow &window, const char *name)
{
    auto *result = window.findChild<Widget *>(QString::fromLatin1(name));
    Q_ASSERT(result != nullptr);
    return result;
}

} // namespace

class AutomationUiTest final : public QObject
{
    Q_OBJECT

private slots:
    void loadsRealSuiteAndShowsStructuredInvalidPath()
    {
        UiRig rig;
        rig.load(QStringLiteral(OMS555TV_PHASE5_SUITE), 8);
        QVERIFY(rig.automation.suite());
        QCOMPARE(rig.automation.suite()->id, QStringLiteral("phase5-smoke"));
        QVERIFY(widget<QLabel>(rig.window, "testSuiteSummaryLabel")->text()
                    .contains(QStringLiteral("8 条用例")));

        const QString invalid = writeFile(
            rig.output, QStringLiteral("invalid.json"),
            QByteArrayLiteral("{\"schema_version\":1,\"id\":\"bad\",\"name\":\"bad\",\"cases\":[{}]}"));
        QVERIFY(!invalid.isEmpty());
        rig.load(invalid, 0);
        const QString errors = widget<QPlainTextEdit>(rig.window, "testLoadErrors")->toPlainText();
        QVERIFY(errors.contains(QStringLiteral("MissingField")));
        QVERIFY(errors.contains(QStringLiteral("/cases/0/id")));
    }

    void rendersPassFailErrorSkippedAndEvidenceConsistency()
    {
        UiRig rig;
        const QString path = writeFile(rig.output, QStringLiteral("status.json"), statusSuite());
        QVERIFY(!path.isEmpty());
        rig.load(path, 4);
        rig.connectDevice();
        rig.client.enqueueStep(readStep(communication::CommunicationOwner::Testing, 0,
                                        readSuccess(42)));
        rig.client.enqueueStep(readStep(communication::CommunicationOwner::Testing, 1,
                                        readSuccess(6)));
        rig.client.enqueueStep(readStep(communication::CommunicationOwner::Testing, 2,
                                        outcome(communication::FakeOutcomeKind::Timeout), 10));
        QVERIFY(rig.sessionLog.startSession({{QStringLiteral("test"), QStringLiteral("ui")}}).succeeded);
        widget<QPushButton>(rig.window, "runAllTestsButton")->click();
        QVERIFY(!widget<QPushButton>(rig.window, "disconnectButton")->isEnabled());
        QVERIFY(!widget<QPushButton>(rig.window, "writeThresholdsButton")->isEnabled());
        rig.scheduler->runUntilIdle();

        const auto snapshot = rig.results.snapshot();
        QVERIFY(snapshot);
        QCOMPARE(snapshot->status, testing::TestStatus::Error);
        QCOMPARE(snapshot->cases[0].status, testing::TestStatus::Pass);
        QCOMPARE(snapshot->cases[1].status, testing::TestStatus::Fail);
        QCOMPARE(snapshot->cases[2].status, testing::TestStatus::Error);
        QCOMPARE(snapshot->cases[3].status, testing::TestStatus::Skipped);
        auto *table = widget<QTableWidget>(rig.window, "testCaseTable");
        QCOMPARE(table->item(0, 4)->text(), QStringLiteral("PASS"));
        QCOMPARE(table->item(1, 4)->text(), QStringLiteral("FAIL"));
        QCOMPARE(table->item(2, 4)->text(), QStringLiteral("ERROR"));
        QCOMPARE(table->item(3, 4)->text(), QStringLiteral("SKIPPED"));
        QVERIFY(widget<QLabel>(rig.window, "testStatisticsLabel")->text()
                    .contains(QStringLiteral("PASS 1 / FAIL 1 / ERROR 1 / SKIPPED 1")));

        QCOMPARE(rig.diagnostics.records().size(), 3);
        int requestLogCount = 0;
        QSet<quint64> logIds;
        for (const auto &entry : rig.sessionLog.entries()) {
            if (entry.module == QStringLiteral("testing")
                && entry.event == QStringLiteral("request_attempt") && entry.requestId) {
                ++requestLogCount;
                logIds.insert(*entry.requestId);
            }
        }
        QCOMPARE(requestLogCount, 3);
        for (int index = 0; index < 3; ++index) {
            QCOMPARE(snapshot->cases[index].attempts.size(), 1);
            const auto id = snapshot->cases[index].attempts.front().requestId.value;
            QVERIFY(logIds.contains(id));
            QCOMPARE(rig.diagnostics.records()[index].result.requestId.value, id);
        }
        table->selectRow(0);
        QVERIFY(widget<QPlainTextEdit>(rig.window, "testCaseDetails")->toPlainText()
                    .contains(QStringLiteral("RequestId=")));
    }

    void executeSelectedThenSkipAndAbortRemainDistinct()
    {
        UiRig rig;
        const QString path = writeFile(rig.output, QStringLiteral("selection.json"), selectionSuite());
        QVERIFY(!path.isEmpty());
        rig.load(path, 3);
        rig.connectDevice();
        auto *table = widget<QTableWidget>(rig.window, "testCaseTable");
        table->selectRow(1);
        rig.client.enqueueStep(readStep(communication::CommunicationOwner::Testing, 1,
                                        readSuccess(2)));
        widget<QPushButton>(rig.window, "runSelectedTestsButton")->click();
        rig.scheduler->runUntilIdle();
        QVERIFY(!rig.automation.busy());
        QCOMPARE(rig.appState.state(), app::AppState::ConnectedIdle);
        const auto selectedSnapshot = rig.results.snapshot();
        QVERIFY(selectedSnapshot);
        QCOMPARE(selectedSnapshot->cases.size(), 3);
        QCOMPARE(selectedSnapshot->cases[0].skipReason,
                 std::optional<testing::TestSkipReason>(testing::TestSkipReason::NotSelected));
        QCOMPARE(selectedSnapshot->cases[1].status, testing::TestStatus::Pass);

        rig.load(path, 3);
        rig.client.enqueueStep(readStep(communication::CommunicationOwner::Testing, 0,
                                        outcome(communication::FakeOutcomeKind::Pending)));
        widget<QPushButton>(rig.window, "runAllTestsButton")->click();
        rig.scheduler->runUntilIdle();
        QCOMPARE(rig.automation.state(), testing::TestAutomationState::Running);
        QVERIFY(widget<QLabel>(rig.window, "testCurrentStepLabel")->text()
                    .contains(QStringLiteral("RequestId=")));
        table->selectRow(1);
        widget<QPushButton>(rig.window, "skipTestButton")->click();
        widget<QPushButton>(rig.window, "abortTestsButton")->click();
        rig.scheduler->runUntilIdle();
        const auto snapshot = rig.results.snapshot();
        QVERIFY(snapshot);
        QCOMPARE(snapshot->cases.size(), 3);
        QCOMPARE(snapshot->cases[0].status, testing::TestStatus::Error);
        QCOMPARE(snapshot->cases[0].error->code, testing::TestErrorCode::Aborted);
        QCOMPARE(snapshot->cases[1].skipReason,
                 std::optional<testing::TestSkipReason>(testing::TestSkipReason::UserSelected));
        QCOMPARE(snapshot->cases[2].skipReason,
                 std::optional<testing::TestSkipReason>(testing::TestSkipReason::Aborted));
        QCOMPARE(rig.appState.state(), app::AppState::ConnectedIdle);
        QCOMPARE(rig.client.activeOwner(), communication::CommunicationOwner::None);
    }

    void monitoringHandoffAndExplicitResumeUseStateMachine()
    {
        UiRig rig;
        const QString path = writeFile(rig.output, QStringLiteral("selection.json"), selectionSuite());
        QVERIFY(!path.isEmpty());
        rig.load(path, 3);
        rig.connectDevice();
        monitor::MonitorConfig config;
        config.targetPeriod = std::chrono::milliseconds(1000);
        rig.client.enqueueStep(monitorReadStep(
            outcome(communication::FakeOutcomeKind::Pending)));
        QVERIFY(rig.appState.startMonitoring(config).accepted());
        rig.scheduler->runUntilIdle();
        QCOMPARE(rig.appState.state(), app::AppState::Monitoring);
        rig.automation.setResumeMonitoring(true);
        rig.client.enqueueStep(readStep(communication::CommunicationOwner::Testing, 0,
                                        readSuccess(1)));
        rig.client.enqueueStep(monitorReadStep(
            outcome(communication::FakeOutcomeKind::Pending)));
        auto *table = widget<QTableWidget>(rig.window, "testCaseTable");
        table->selectRow(0);
        widget<QPushButton>(rig.window, "runSelectedTestsButton")->click();
        QVERIFY(rig.automation.busy());
        QVERIFY(!widget<QPushButton>(rig.window, "stopMonitoringButton")->isEnabled());
        rig.scheduler->runUntilIdle();
        QCOMPARE(rig.results.snapshot()->cases[0].status, testing::TestStatus::Pass);
        QCOMPARE(rig.appState.state(), app::AppState::Monitoring);
        QCOMPARE(rig.client.activeOwner(), communication::CommunicationOwner::Monitor);
        QVERIFY(!rig.automation.busy());

        QVERIFY(rig.appState.stopMonitoring().accepted());
        rig.scheduler->runUntilIdle();
        QCOMPARE(rig.appState.state(), app::AppState::ConnectedIdle);
    }

    void formalV2SuiteShowsCompositeEvidenceAndAbortableStabilityProgress()
    {
        UiRig rig;
        rig.load(QStringLiteral(OMS555TV_PHASE6_RS485_SUITE), 20);
        rig.connectDevice();
        auto *table = widget<QTableWidget>(rig.window, "testCaseTable");

        const int recoveryRow = rig.rowFor(QStringLiteral("TC-R-AUTO-001"));
        QVERIFY(recoveryRow >= 0);
        table->selectRow(recoveryRow);
        rig.client.enqueueStep(readStep(
            communication::CommunicationOwner::Testing, 50000,
            outcome(communication::FakeOutcomeKind::RemoteException, 2)));
        rig.client.enqueueStep(readRegistersStep(
            communication::CommunicationOwner::Testing, 39, 2,
            [] {
                communication::FakeOutcome value;
                value.kind = communication::FakeOutcomeKind::ReadSuccess;
                value.readValues = {0, 2};
                return value;
            }()));
        widget<QPushButton>(rig.window, "runSelectedTestsButton")->click();
        rig.scheduler->runUntilIdle();
        table->selectRow(recoveryRow);
        const QString recoveryDetails =
            widget<QPlainTextEdit>(rig.window, "testCaseDetails")->toPlainText();
        QVERIFY(recoveryDetails.contains(QStringLiteral("复合步骤（2）")));
        QVERIFY(recoveryDetails.contains(QStringLiteral("expected-address-error")));
        QVERIFY(recoveryDetails.contains(QStringLiteral("legal-read-after-error")));
        QVERIFY(recoveryDetails.contains(QStringLiteral("证据保留摘要")));
        QVERIFY(recoveryDetails.contains(QStringLiteral("Session ID")));

        rig.load(QStringLiteral(OMS555TV_PHASE6_RS485_SUITE), 20);
        const int sequenceRow = rig.rowFor(QStringLiteral("TC-P007"));
        QVERIFY(sequenceRow >= 0);
        table->selectRow(sequenceRow);
        rig.client.enqueueStep(readStep(
            communication::CommunicationOwner::Testing, 39,
            outcome(communication::FakeOutcomeKind::Pending)));
        widget<QPushButton>(rig.window, "runSelectedTestsButton")->click();
        rig.scheduler->runUntilIdle();
        const QString sequenceProgress =
            widget<QLabel>(rig.window, "testCurrentStepLabel")->text();
        QVERIFY(sequenceProgress.contains(QStringLiteral("复合步骤=major（1/3）")));
        QVERIFY(sequenceProgress.contains(QStringLiteral("重复=1/5")));
        widget<QPushButton>(rig.window, "abortTestsButton")->click();
        rig.scheduler->runUntilIdle();

        rig.load(QStringLiteral(OMS555TV_PHASE6_RS485_SUITE), 20);
        const int stabilityRow = rig.rowFor(QStringLiteral("TC-S001"));
        QVERIFY(stabilityRow >= 0);
        table->selectRow(stabilityRow);
        rig.client.enqueueStep(readRegistersStep(
            communication::CommunicationOwner::Testing, 0, 5,
            outcome(communication::FakeOutcomeKind::Pending)));
        widget<QPushButton>(rig.window, "runSelectedTestsButton")->click();
        rig.scheduler->runUntilIdle();
        QVERIFY(widget<QPushButton>(rig.window, "abortTestsButton")->isEnabled());
        QVERIFY(widget<QLabel>(rig.window, "testCurrentStepLabel")->text()
                    .contains(QStringLiteral("稳定性迭代=1/600")));
        QCoreApplication::processEvents();
        widget<QPushButton>(rig.window, "abortTestsButton")->click();
        rig.scheduler->runUntilIdle();
        table->selectRow(stabilityRow);
        const QString stabilityDetails =
            widget<QPlainTextEdit>(rig.window, "testCaseDetails")->toPlainText();
        QVERIFY(stabilityDetails.contains(QStringLiteral("稳定性聚合统计")));
        QVERIFY(stabilityDetails.contains(QStringLiteral("证据保留摘要")));
        QCOMPARE(rig.appState.state(), app::AppState::ConnectedIdle);
        QCOMPARE(rig.client.activeOwner(), communication::CommunicationOwner::None);
    }

    void guidedSuccessRendersPromptProgressTimingAndGates()
    {
        UiRig rig;
        const QString path = writeFile(rig.output, QStringLiteral("guided.json"),
                                       guidedSuite());
        QVERIFY(!path.isEmpty());
        rig.load(path, 1);
        rig.connectDevice();
        auto outageStep = readStep(
            communication::CommunicationOwner::Testing, 16,
            outcome(communication::FakeOutcomeKind::Timeout), 50);
        outageStep.virtualDelay = std::chrono::milliseconds(1);
        rig.client.enqueueStep(std::move(outageStep));
        rig.client.enqueueStep(readStep(
            communication::CommunicationOwner::Testing, 16, readSuccess(42), 50));

        widget<QPushButton>(rig.window, "runAllTestsButton")->click();
        rig.scheduler->advanceBy(std::chrono::milliseconds(0));
        auto *panel = widget<QGroupBox>(rig.window, "guidedPanel");
        QVERIFY(!panel->isHidden());
        QVERIFY(widget<QLabel>(rig.window, "guidedPromptTitleLabel")->text()
                    .contains(QStringLiteral("断开 A/B")));
        QVERIFY(widget<QLabel>(rig.window, "guidedInstructionLabel")->text()
                    .contains(QStringLiteral("开发板侧")));
        QVERIFY(widget<QLabel>(rig.window, "guidedSafetyLabel")->text()
                    .contains(QStringLiteral("电源端子")));
        QVERIFY(widget<QLabel>(rig.window, "guidedCountdownLabel")->text()
                    .contains(QStringLiteral("1000 ms")));
        QVERIFY(widget<QPushButton>(rig.window, "guidedConfirmButton")->isEnabled());
        QVERIFY(widget<QPushButton>(rig.window, "guidedCancelButton")->isEnabled());
        QVERIFY(!widget<QPushButton>(rig.window, "disconnectButton")->isEnabled());
        QVERIFY(!widget<QPushButton>(rig.window, "writeThresholdsButton")->isEnabled());
        QVERIFY(!widget<QPushButton>(rig.window, "clearDiagnosticsButton")->isEnabled());

        widget<QPushButton>(rig.window, "guidedConfirmButton")->click();
        QVERIFY(!widget<QPushButton>(rig.window, "guidedConfirmButton")->isEnabled());
        QVERIFY(widget<QLabel>(rig.window, "guidedObservationProgressLabel")->text()
                    .contains(QStringLiteral("0/1")));
        rig.scheduler->advanceBy(std::chrono::milliseconds(0));
        rig.scheduler->advanceBy(std::chrono::milliseconds(1));
        const auto reconnectTitle = widget<QLabel>(
            rig.window, "guidedPromptTitleLabel")->text();
        QVERIFY2(reconnectTitle.contains(QStringLiteral("恢复 A/B")),
                 qPrintable(QStringLiteral("title=%1 state=%2 fake=%3")
                                .arg(reconnectTitle,
                                     testing::guidedRunStateName(
                                         rig.automation.guidedView().state),
                                     rig.client.verificationError())
                                + QStringLiteral(" reason=%1")
                                      .arg(testing::guidedTerminalReasonName(
                                          rig.automation.guidedView().terminalReason))));
        widget<QPushButton>(rig.window, "guidedConfirmButton")->click();
        rig.scheduler->advanceBy(std::chrono::milliseconds(0));

        QCOMPARE(rig.automation.state(), testing::TestAutomationState::Idle);
        QCOMPARE(rig.results.snapshot()->status, testing::TestStatus::Pass);
        QVERIFY(widget<QLabel>(rig.window, "guidedPromptTitleLabel")->text()
                    .contains(QStringLiteral("FINISHED")));
        QVERIFY(widget<QLabel>(rig.window, "guidedRecoveryTimingLabel")->text()
                    .contains(QStringLiteral("首次响应 0 ms")));
        auto *table = widget<QTableWidget>(rig.window, "testCaseTable");
        table->selectRow(0);
        const auto details = widget<QPlainTextEdit>(
            rig.window, "testCaseDetails")->toPlainText();
        QVERIFY(details.contains(QStringLiteral("引导式恢复结果")));
        QVERIFY(details.contains(QStringLiteral("RequestId=")));
        QVERIFY(details.contains(QStringLiteral("TX=")));
        QVERIFY(details.contains(QStringLiteral("物理链路已观察恢复：是")));
    }

    void guidedFailureAndCancellationShowSafeRestorationReminder()
    {
        UiRig rig;
        const QString path = writeFile(rig.output, QStringLiteral("guided.json"),
                                       guidedSuite());
        QVERIFY(!path.isEmpty());
        rig.load(path, 1);
        rig.connectDevice();
        rig.client.enqueueStep(readStep(
            communication::CommunicationOwner::Testing, 16, readSuccess(42), 50));
        widget<QPushButton>(rig.window, "runAllTestsButton")->click();
        rig.scheduler->advanceBy(std::chrono::milliseconds(0));
        widget<QPushButton>(rig.window, "guidedConfirmButton")->click();
        rig.scheduler->advanceBy(std::chrono::milliseconds(100));
        QCOMPARE(rig.results.snapshot()->status, testing::TestStatus::Fail);
        QVERIFY(widget<QLabel>(rig.window, "guidedRestorationReminderLabel")->text()
                    .contains(QStringLiteral("软件尚未观察到稳定恢复")));
        QCOMPARE(widget<QTableWidget>(rig.window, "testCaseTable")
                     ->item(0, 4)->text(), QStringLiteral("FAIL"));

        rig.load(path, 1);
        widget<QPushButton>(rig.window, "runAllTestsButton")->click();
        rig.scheduler->advanceBy(std::chrono::milliseconds(0));
        widget<QPushButton>(rig.window, "guidedCancelButton")->click();
        rig.scheduler->advanceBy(std::chrono::milliseconds(0));
        QCOMPARE(rig.results.snapshot()->status, testing::TestStatus::Skipped);
        QCOMPARE(widget<QTableWidget>(rig.window, "testCaseTable")
                     ->item(0, 4)->text(), QStringLiteral("SKIPPED"));
        const auto reminder = widget<QLabel>(
            rig.window, "guidedRestorationReminderLabel")->text();
        QVERIFY(reminder.contains(QStringLiteral("恢复")));
        QVERIFY(reminder.contains(QStringLiteral("A/B")));
        QVERIFY(!widget<QLabel>(rig.window, "guidedRestorationReminderLabel")->text()
                     .contains(QStringLiteral("已安全恢复")));
        QCOMPARE(rig.appState.state(), app::AppState::ConnectedIdle);
        QCOMPARE(rig.client.activeOwner(), communication::CommunicationOwner::None);
    }
};

QTEST_MAIN(AutomationUiTest)

#include "tst_automationui.moc"

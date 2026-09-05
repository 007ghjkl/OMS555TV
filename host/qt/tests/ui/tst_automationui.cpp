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

communication::FakeOutcome outcome(communication::FakeOutcomeKind kind)
{
    communication::FakeOutcome value;
    value.kind = kind;
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
    testing::TestEngine engine{appState, client, results, &sessionLog,
        [this] { return QDateTime::fromMSecsSinceEpoch(
            std::chrono::duration_cast<std::chrono::milliseconds>(scheduler->now()).count(),
            Qt::UTC); }};
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
};

QTEST_MAIN(AutomationUiTest)

#include "tst_automationui.moc"

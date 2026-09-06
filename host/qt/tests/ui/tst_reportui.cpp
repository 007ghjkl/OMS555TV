#include "app/AppStateController.h"
#include "app/MainWindow.h"
#include "communication/FakeModbusClient.h"
#include "configuration/ConfigurationService.h"
#include "diagnostics/CommunicationDiagnostics.h"
#include "logging/SessionLogService.h"
#include "monitor/MonitorService.h"
#include "report/ReportExportController.h"
#include "testing/TestAutomationController.h"
#include "testing/TestEngine.h"
#include "testing/TestResultManager.h"
#include "ui/MonitoringViewModel.h"
#include "../monitor/ManualMonitorScheduler.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
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
    config.serial.portName = QStringLiteral("COM-FIXTURE");
    config.serverAddress = 7;
    config.defaultResponseTimeout = std::chrono::milliseconds(321);
    return config;
}

QByteArray suite(const QString &id, const QString &name, const int expected)
{
    return QStringLiteral(R"JSON({
  "schema_version": 1,
  "id": "%1",
  "name": "%2",
  "description": "报告 UI <fixture>",
  "tags": ["phase8"],
  "metadata": {
    "device_model": "NUCLEO-F411RE",
    "test_bench": "Fake 安全台架",
    "environment_description": "离线 & offscreen"
  },
  "cases": [{
    "id":"read","name":"读取 <值>","category":"functional",
    "type":"read_register","request":{"function":3,"address":0,"count":1},
    "expected":{"type":"equals","value":%3}
  }]
})JSON").arg(id, name).arg(expected).toUtf8();
}

communication::FakeStep readStep(const quint16 value,
                                 communication::FakeOutcomeKind kind =
                                     communication::FakeOutcomeKind::ReadSuccess)
{
    communication::FakeOutcome outcome;
    outcome.kind = kind;
    outcome.readValues = {value};
    return {{communication::CommunicationOwner::Testing,
             communication::ReadRequestDescriptor{device::PduAddress(0), 1},
             std::chrono::milliseconds(500)},
            std::move(outcome), {}, {}, {}};
}

QString writeFile(QTemporaryDir &directory, const QString &name,
                  const QByteArray &contents)
{
    const QString path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) {
        return {};
    }
    file.close();
    return path;
}

template<typename Widget>
Widget *widget(MainWindow &window, const char *name)
{
    auto *value = window.findChild<Widget *>(QString::fromLatin1(name));
    Q_ASSERT(value != nullptr);
    return value;
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
    logging::SessionLogService sessionLog{diagnostics, output.filePath("logs")};
    testing::TestResultManager results;
    testing::TestEngine engine{
        appState, client, results, &sessionLog, monitorScheduler};
    testing::TestAutomationController automation{appState, engine, results};
    report::ReportExportController reportExport{
        automation, sessionLog, [] {
            report::ReportRuntimeContext context;
            context.connectionConfig = connectionConfig();
            context.applicationVersion = QStringLiteral("0.1.0-test");
            context.sourceRevision = QStringLiteral("task026-fixture");
            context.displayTimeZone = QTimeZone(QByteArrayLiteral("Asia/Shanghai"));
            return context;
        }};
    MainWindow window{monitoring, configuration, diagnostics, sessionLog,
                      automation, reportExport};

    UiRig()
    {
        QVERIFY(output.isValid());
        window.show();
        QCoreApplication::processEvents();
    }

    void connectDevice()
    {
        if (appState.state() == app::AppState::ConnectedIdle) return;
        QVERIFY(appState.connectDevice(connectionConfig()).accepted());
        scheduler->runUntilIdle();
        QCOMPARE(appState.state(), app::AppState::ConnectedIdle);
    }

    void load(const QString &path)
    {
        widget<QLineEdit>(window, "testSuitePath")->setText(path);
        widget<QPushButton>(window, "loadTestSuiteButton")->click();
        QTRY_COMPARE_WITH_TIMEOUT(automation.state(), testing::TestAutomationState::Idle,
                                  2000);
        QVERIFY(automation.suite());
    }

    void runSuccessful(const quint16 value, const bool withSession = true)
    {
        connectDevice();
        if (withSession) {
            QVERIFY(sessionLog.startSession({{QStringLiteral("fixture"), true}}).succeeded);
        }
        client.enqueueStep(readStep(value));
        widget<QPushButton>(window, "runAllTestsButton")->click();
        scheduler->runUntilIdle();
        QTRY_COMPARE(automation.state(), testing::TestAutomationState::Idle);
        QVERIFY(results.snapshot());
        QCOMPARE(results.snapshot()->status, testing::TestStatus::Pass);
        if (withSession) QVERIFY(sessionLog.endSession().succeeded);
        QCoreApplication::processEvents();
    }
};

} // namespace

class ReportUiTest final : public QObject
{
    Q_OBJECT

private slots:
    void gatesMetadataAndExportsAsynchronouslyWithoutOverwrite()
    {
        UiRig rig;
        QVERIFY(widget<QLabel>(rig.window, "reportGateLabel")->text()
                    .contains(QStringLiteral("NoCompletedResult")));
        QVERIFY(!widget<QPushButton>(rig.window, "generateHtmlReportButton")->isEnabled());

        report::ReportExportRequest noResult;
        noResult.operatorMetadata.tester = QStringLiteral("测试员");
        noResult.outputDirectory = rig.output.path();
        noResult.fileName = QStringLiteral("none.html");
        QVERIFY(!rig.reportExport.exportHtml(noResult));
        QCOMPARE(rig.reportExport.lastOutcome().errors.front().code,
                 report::ReportErrorCode::NoCompletedResult);

        const QString path = writeFile(rig.output, QStringLiteral("one.json"),
                                       suite(QStringLiteral("report-one"),
                                             QStringLiteral("第一套件"), 11));
        QVERIFY(!path.isEmpty());
        rig.load(path);
        rig.runSuccessful(11);

        QVERIFY(rig.reportExport.preview());
        QCOMPARE(rig.reportExport.preview()->suiteId, QStringLiteral("report-one"));
        QVERIFY(widget<QLabel>(rig.window, "reportRunLabel")->text()
                    .contains(QStringLiteral("终态=PASS")));
        QVERIFY(widget<QLabel>(rig.window, "reportConfiguredMetadataLabel")->text()
                    .contains(QStringLiteral("NUCLEO-F411RE [suite-configured]")));
        QVERIFY(widget<QLabel>(rig.window, "reportGateLabel")->text()
                    .contains(QStringLiteral("MissingRequiredMetadata")));

        report::ReportExportRequest missingTester;
        missingTester.operatorMetadata.tester = QStringLiteral("   ");
        missingTester.outputDirectory = rig.output.path();
        missingTester.fileName = QStringLiteral("missing-tester.html");
        QVERIFY(!rig.reportExport.exportHtml(missingTester));
        QCOMPARE(rig.reportExport.lastOutcome().errors.front().code,
                 report::ReportErrorCode::MissingRequiredMetadata);

        auto *tester = widget<QLineEdit>(rig.window, "reportTesterEdit");
        tester->setText(QStringLiteral("测试员 <甲> & 乙"));
        QVERIFY(widget<QPushButton>(rig.window, "generateHtmlReportButton")->isEnabled());
        auto *directory = widget<QLineEdit>(rig.window, "reportOutputDirectoryEdit");
        directory->setText(rig.output.path());
        auto *fileName = widget<QLineEdit>(rig.window, "reportFileNameEdit");
        fileName->setText(QStringLiteral("ui-export.html"));

        report::ReportExportRequest invalidName;
        invalidName.operatorMetadata.tester = tester->text();
        invalidName.outputDirectory = directory->text();
        invalidName.fileName = QStringLiteral("../escape.html");
        QVERIFY(!rig.reportExport.exportHtml(invalidName));
        QCOMPARE(rig.reportExport.lastOutcome().errors.front().code,
                 report::ReportErrorCode::InvalidOutputPath);

        widget<QPushButton>(rig.window, "generateHtmlReportButton")->click();
        QCOMPARE(rig.reportExport.state(), report::ReportExportState::Exporting);
        QVERIFY(!widget<QPushButton>(rig.window, "generateHtmlReportButton")->isEnabled());
        report::ReportExportRequest duplicate;
        duplicate.operatorMetadata.tester = tester->text();
        duplicate.outputDirectory = directory->text();
        duplicate.fileName = fileName->text();
        QVERIFY(!rig.reportExport.exportHtml(duplicate));
        QCOMPARE(rig.reportExport.lastOutcome().errors.front().code,
                 report::ReportErrorCode::ExportInProgress);
        QTRY_COMPARE_WITH_TIMEOUT(rig.reportExport.state(),
                                  report::ReportExportState::Succeeded, 5000);
        QVERIFY(rig.reportExport.lastOutcome().succeeded());
        const QString reportPath = *rig.reportExport.lastOutcome().filePath;
        QVERIFY(QFileInfo::exists(reportPath));
        QVERIFY(QFileInfo(reportPath).size() > 1000);
        QCOMPARE(resultsStatus(rig), testing::TestStatus::Pass);

        QFile html(reportPath);
        QVERIFY(html.open(QIODevice::ReadOnly));
        const QByteArray contents = html.readAll();
        QVERIFY(contents.contains("测试员 &lt;甲&gt; &amp; 乙"));
        QVERIFY(contents.contains("COM-FIXTURE"));
        QVERIFY(contents.contains("SessionLog 与证据边界"));
        QVERIFY(contents.contains("SHA-256"));
        QVERIFY(!contents.contains("<script"));

        QVERIFY(rig.reportExport.exportHtml(duplicate));
        QTRY_COMPARE_WITH_TIMEOUT(rig.reportExport.state(),
                                  report::ReportExportState::Failed, 5000);
        QCOMPARE(rig.reportExport.lastOutcome().errors.front().code,
                 report::ReportErrorCode::TargetAlreadyExists);
        QCOMPARE(resultsStatus(rig), testing::TestStatus::Pass);

        duplicate.outputDirectory = rig.output.filePath("missing-directory");
        duplicate.fileName = QStringLiteral("invalid.html");
        QVERIFY(rig.reportExport.exportHtml(duplicate));
        QTRY_COMPARE_WITH_TIMEOUT(rig.reportExport.state(),
                                  report::ReportExportState::Failed, 5000);
        QCOMPARE(rig.reportExport.lastOutcome().errors.front().code,
                 report::ReportErrorCode::InvalidOutputPath);
        QVERIFY(!QFileInfo::exists(rig.output.filePath("missing-directory/invalid.html")));
    }

    void runningGateAndLatestCompletedResultReplacement()
    {
        UiRig rig;
        const QString first = writeFile(rig.output, QStringLiteral("first.json"),
            suite(QStringLiteral("first-suite"), QStringLiteral("第一套件"), 1));
        const QString second = writeFile(rig.output, QStringLiteral("second.json"),
            suite(QStringLiteral("second-suite"), QStringLiteral("第二套件"), 2));
        rig.load(first);
        rig.runSuccessful(1, false);
        const quint64 firstRun = rig.reportExport.preview()->runId;
        QCOMPARE(rig.reportExport.preview()->suiteId, QStringLiteral("first-suite"));

        rig.load(second);
        QCOMPARE(rig.reportExport.preview()->suiteId, QStringLiteral("first-suite"));
        QCOMPARE(rig.reportExport.preview()->runId, firstRun);

        communication::FakeOutcome pending;
        pending.kind = communication::FakeOutcomeKind::Pending;
        rig.client.enqueueStep({
            {communication::CommunicationOwner::Testing,
             communication::ReadRequestDescriptor{device::PduAddress(0), 1},
             std::chrono::milliseconds(500)}, pending, {}, {}, {}});
        widget<QPushButton>(rig.window, "runAllTestsButton")->click();
        rig.scheduler->runUntilIdle();
        QCOMPARE(rig.automation.state(), testing::TestAutomationState::Running);
        QVERIFY(widget<QLabel>(rig.window, "reportGateLabel")->text()
                    .contains(QStringLiteral("TestRunInProgress")));
        QVERIFY(!rig.reportExport.canExport());
        report::ReportExportRequest running;
        running.operatorMetadata.tester = QStringLiteral("测试员");
        running.outputDirectory = rig.output.path();
        running.fileName = QStringLiteral("running.html");
        QVERIFY(!rig.reportExport.exportHtml(running));
        QCOMPARE(rig.reportExport.lastOutcome().errors.front().code,
                 report::ReportErrorCode::TestRunInProgress);
        widget<QPushButton>(rig.window, "abortTestsButton")->click();
        rig.scheduler->runUntilIdle();
        QTRY_COMPARE(rig.automation.state(), testing::TestAutomationState::Idle);
        QCOMPARE(rig.reportExport.preview()->suiteId, QStringLiteral("second-suite"));
        QVERIFY(rig.reportExport.preview()->runId > firstRun);
        QCOMPARE(rig.reportExport.preview()->status, testing::TestStatus::Error);
    }

private:
    static testing::TestStatus resultsStatus(const UiRig &rig)
    {
        return rig.results.snapshot()->status;
    }
};

QTEST_MAIN(ReportUiTest)

#include "tst_reportui.moc"

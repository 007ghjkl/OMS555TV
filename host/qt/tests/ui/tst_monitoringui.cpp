#include "app/MainWindow.h"
#include "app/AppStateController.h"
#include "communication/FakeModbusClient.h"
#include "monitor/MonitorService.h"
#include "../monitor/ManualMonitorScheduler.h"
#include "ui/MonitoringViewModel.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTest>

#include <memory>

using namespace oms555tv;

namespace {

class FakeSerialPortCatalog final : public ui::ISerialPortCatalog
{
public:
    QVector<ui::SerialPortEntry> entries;

    [[nodiscard]] QVector<ui::SerialPortEntry> availablePorts() const override
    {
        return entries;
    }
};

ui::SerialPortEntry port(QString name, QString description)
{
    ui::SerialPortEntry entry;
    entry.portName = std::move(name);
    entry.description = std::move(description);
    return entry;
}

QVector<quint16> validValues(qsizetype blockIndex)
{
    switch (blockIndex) {
    case 0:
        return {251, 262, 273, 234, 1234};
    case 1:
        return {600, 610, 620, 400};
    case 2:
        return {0x0001, 0x0021};
    case 3:
        return {2, 1234, 0};
    case 4:
        return {0, 2};
    default:
        return {};
    }
}

communication::FakeStep stepForBlock(
    qsizetype blockIndex,
    communication::FakeOutcome outcome,
    std::chrono::milliseconds delay = std::chrono::milliseconds::zero())
{
    const auto block = device::deviceSnapshotReadBlocks[static_cast<std::size_t>(blockIndex)];
    return {{communication::CommunicationOwner::Monitor,
             communication::ReadRequestDescriptor{block.startAddress, block.count},
             std::chrono::milliseconds(500), {}},
            std::move(outcome), delay, {}, {}};
}

void enqueueSuccessfulBatch(communication::FakeModbusClient &client,
                            std::chrono::milliseconds delay =
                                std::chrono::milliseconds::zero())
{
    for (qsizetype index = 0;
         index < static_cast<qsizetype>(device::deviceSnapshotReadBlocks.size());
         ++index) {
        communication::FakeOutcome outcome;
        outcome.kind = communication::FakeOutcomeKind::ReadSuccess;
        outcome.readValues = validValues(index);
        client.enqueueStep(stepForBlock(index, std::move(outcome), delay));
    }
}

void enqueueTimeout(communication::FakeModbusClient &client)
{
    communication::FakeOutcome outcome;
    outcome.kind = communication::FakeOutcomeKind::Timeout;
    client.enqueueStep(stepForBlock(0, std::move(outcome)));
}

void enqueuePending(communication::FakeModbusClient &client)
{
    communication::FakeOutcome outcome;
    outcome.kind = communication::FakeOutcomeKind::Pending;
    client.enqueueStep(stepForBlock(0, std::move(outcome)));
}

struct UiRig {
    std::shared_ptr<communication::ManualScheduler> scheduler =
        std::make_shared<communication::ManualScheduler>();
    communication::FakeModbusClient client{scheduler};
    monitor::test::ManualMonitorScheduler monitorScheduler{scheduler};
    monitor::MonitorService monitorService{client, monitorScheduler};
    app::AppStateController controller{client, monitorService};
    FakeSerialPortCatalog catalog;
    ui::MonitoringViewModel viewModel{controller, monitorService, catalog};
    MainWindow window{viewModel};

    explicit UiRig(QVector<ui::SerialPortEntry> ports)
    {
        catalog.entries = std::move(ports);
        viewModel.refreshPorts();
        window.show();
        QCoreApplication::processEvents();
    }

    void connectDevice()
    {
        viewModel.connectDevice();
        scheduler->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(controller.state(), app::AppState::ConnectedIdle);
    }

    void startMonitoring()
    {
        viewModel.startMonitoring();
        scheduler->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(controller.state(), app::AppState::Monitoring);
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

class MonitoringUiTest final : public QObject
{
    Q_OBJECT

private slots:
    void portEnumerationAndInitialGateAreExplicit()
    {
        UiRig rig({port(QStringLiteral("COM3"), QStringLiteral("ST-LINK VCP")),
                   port(QStringLiteral("COM6"), QStringLiteral("USB-RS485"))});
        auto *portCombo = widget<QComboBox>(rig.window, "portCombo");
        QCOMPARE(portCombo->count(), 2);
        QCOMPARE(portCombo->currentIndex(), -1);
        QVERIFY(!widget<QPushButton>(rig.window, "connectButton")->isEnabled());
        QVERIFY(!widget<QPushButton>(rig.window, "startMonitoringButton")->isEnabled());
        QCOMPARE(widget<QLabel>(rig.window, "freshnessLabel")->text(),
                 QStringLiteral("无有效快照"));
        QCOMPARE(widget<QLabel>(rig.window, "ambientTemperatureLabel")->text(),
                 QStringLiteral("--（模拟源）"));
        QVERIFY(widget<QLabel>(rig.window, "serialParamsLabel")->text()
                    .contains(QStringLiteral("115200")));

        rig.viewModel.setSelectedPortName(QStringLiteral("COM6"));
        QVERIFY(widget<QPushButton>(rig.window, "connectButton")->isEnabled());
        QCOMPARE(portCombo->currentData().toString(), QStringLiteral("COM6"));
    }

    void snapshotFormattingButtonsAndStoppedStaleness()
    {
        UiRig rig({port(QStringLiteral("COM6"), QStringLiteral("USB-RS485"))});
        auto *connectButton = widget<QPushButton>(rig.window, "connectButton");
        connectButton->click();
        QVERIFY(!widget<QComboBox>(rig.window, "portCombo")->isEnabled());
        QVERIFY(!connectButton->isEnabled());
        rig.scheduler->advanceBy(std::chrono::nanoseconds::zero());
        QVERIFY(widget<QPushButton>(rig.window, "startMonitoringButton")->isEnabled());
        QVERIFY(widget<QPushButton>(rig.window, "disconnectButton")->isEnabled());

        enqueueSuccessfulBatch(rig.client);
        rig.viewModel.setTargetPeriodMs(100);
        rig.startMonitoring();
        QCOMPARE(widget<QLabel>(rig.window, "phaseATemperatureLabel")->text(),
                 QStringLiteral("25.1 ℃"));
        QCOMPARE(widget<QLabel>(rig.window, "phaseBTemperatureLabel")->text(),
                 QStringLiteral("26.2 ℃"));
        QCOMPARE(widget<QLabel>(rig.window, "phaseCTemperatureLabel")->text(),
                 QStringLiteral("27.3 ℃"));
        QVERIFY(widget<QLabel>(rig.window, "ambientTemperatureLabel")->text()
                    .contains(QStringLiteral("模拟源，状态位已确认")));
        QCOMPARE(widget<QLabel>(rig.window, "lightMillivoltsLabel")->text(),
                 QStringLiteral("1234 mV"));
        QCOMPARE(widget<QLabel>(rig.window, "phaseAAlarmLabel")->text(),
                 QStringLiteral("高温告警"));
        QCOMPARE(widget<QLabel>(rig.window, "phaseBAlarmLabel")->text(),
                 QStringLiteral("正常"));
        QVERIFY(widget<QLabel>(rig.window, "deviceStatusLabel")->text()
                    .contains(QStringLiteral("A相传感器正常")));
        QVERIFY(widget<QLabel>(rig.window, "deviceStatusLabel")->text()
                    .contains(QStringLiteral("状态字 0x0021")));
        QCOMPARE(widget<QLabel>(rig.window, "firmwareVersionLabel")->text(),
                 QStringLiteral("0.2"));
        QVERIFY(widget<QLabel>(rig.window, "uptimeLabel")->text()
                    .contains(QStringLiteral("1234 s")));
        QCOMPARE(widget<QLabel>(rig.window, "freshnessLabel")->text(),
                 QStringLiteral("实时（完整快照）"));
        QCOMPARE(widget<QLabel>(rig.window, "requestsLabel")->text(),
                 QStringLiteral("5"));
        QCOMPARE(widget<QLabel>(rig.window, "successRateLabel")->text(),
                 QStringLiteral("100.00 %"));
        QVERIFY(!widget<QComboBox>(rig.window, "periodCombo")->isEnabled());

        widget<QPushButton>(rig.window, "stopMonitoringButton")->click();
        rig.scheduler->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(rig.controller.state(), app::AppState::ConnectedIdle);
        QVERIFY(widget<QLabel>(rig.window, "freshnessLabel")->text()
                    .contains(QStringLiteral("陈旧（当前未监控）")));
        widget<QPushButton>(rig.window, "disconnectButton")->click();
        rig.scheduler->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(rig.controller.state(), app::AppState::Disconnected);
        QVERIFY(widget<QComboBox>(rig.window, "portCombo")->isEnabled());
    }

    void offlineRetainsSnapshotAndNextCompleteBatchRecovers()
    {
        UiRig rig({port(QStringLiteral("COM6"), QStringLiteral("USB-RS485"))});
        rig.connectDevice();
        rig.viewModel.setTargetPeriodMs(100);
        enqueueSuccessfulBatch(rig.client);
        enqueueTimeout(rig.client);
        enqueueTimeout(rig.client);
        enqueueTimeout(rig.client);
        enqueueSuccessfulBatch(rig.client, std::chrono::milliseconds(1));
        rig.startMonitoring();
        QCOMPARE(rig.monitorService.health(), monitor::DeviceHealth::Online);
        const QString successfulTime = widget<QLabel>(
            rig.window, "lastSuccessfulLabel")->text();

        rig.scheduler->advanceBy(std::chrono::milliseconds(100));
        rig.scheduler->advanceBy(std::chrono::milliseconds(500));
        QCOMPARE(rig.monitorService.health(), monitor::DeviceHealth::Degraded);
        QVERIFY(widget<QLabel>(rig.window, "freshnessLabel")->text()
                    .contains(QStringLiteral("通信退化")));
        QCOMPARE(widget<QLabel>(rig.window, "lastSuccessfulLabel")->text(),
                 successfulTime);
        rig.scheduler->advanceBy(std::chrono::milliseconds(500));
        rig.scheduler->advanceBy(std::chrono::milliseconds(500));
        QCOMPARE(rig.monitorService.health(), monitor::DeviceHealth::Offline);
        QVERIFY(widget<QLabel>(rig.window, "freshnessLabel")->text()
                    .contains(QStringLiteral("设备离线")));
        QCOMPARE(widget<QLabel>(rig.window, "phaseATemperatureLabel")->text(),
                 QStringLiteral("25.1 ℃"));
        QCOMPARE(widget<QLabel>(rig.window, "timedOutLabel")->text(),
                 QStringLiteral("3"));

        for (int index = 0; index < 5; ++index) {
            rig.scheduler->advanceBy(std::chrono::milliseconds(1));
        }
        QCOMPARE(rig.monitorService.health(), monitor::DeviceHealth::Online);
        QCOMPARE(widget<QLabel>(rig.window, "freshnessLabel")->text(),
                 QStringLiteral("实时（完整快照）"));
        QCOMPARE(widget<QLabel>(rig.window, "lastErrorLabel")->text(),
                 QStringLiteral("无"));
    }

    void overrunAndEffectivePeriodAreTruthfullyDisplayed()
    {
        UiRig rig({port(QStringLiteral("COM6"), QStringLiteral("USB-RS485"))});
        rig.connectDevice();
        rig.viewModel.setTargetPeriodMs(100);
        enqueueSuccessfulBatch(rig.client, std::chrono::milliseconds(30));
        enqueueSuccessfulBatch(rig.client, std::chrono::milliseconds(30));
        enqueuePending(rig.client);
        rig.viewModel.startMonitoring();
        rig.scheduler->advanceBy(std::chrono::nanoseconds::zero());
        for (int index = 0; index < 10; ++index) {
            rig.scheduler->advanceBy(std::chrono::milliseconds(30));
        }
        QCOMPARE(rig.monitorService.statistics().overruns, quint64(2));
        QCOMPARE(widget<QLabel>(rig.window, "effectivePeriodLabel")->text(),
                 QStringLiteral("150.0 ms"));
        QCOMPARE(widget<QLabel>(rig.window, "overrunLabel")->text(),
                 QStringLiteral("是（累计 2）"));
        QCOMPARE(rig.monitorService.statistics().requests, quint64(11));
        QVERIFY(rig.client.hasNonTerminalRequests());
        rig.viewModel.stopMonitoring();
        rig.scheduler->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(rig.controller.state(), app::AppState::ConnectedIdle);
    }

    void failedConnectEntersErrorAndExplicitRecoveryRestoresGate()
    {
        UiRig rig({port(QStringLiteral("COM6"), QStringLiteral("USB-RS485"))});
        communication::CommunicationError error;
        error.category = communication::ErrorCategory::Connection;
        error.code = communication::ErrorCode::OpenFailed;
        error.diagnostic = QStringLiteral("测试打开失败");
        rig.client.failNextOpen(error);
        rig.viewModel.connectDevice();
        rig.scheduler->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(rig.controller.state(), app::AppState::Error);
        QVERIFY(widget<QPushButton>(rig.window, "recoverButton")->isEnabled());
        QVERIFY(widget<QLabel>(rig.window, "lastErrorLabel")->text()
                    .contains(QStringLiteral("串口打开失败")));
        QVERIFY(!widget<QPushButton>(rig.window, "connectButton")->isEnabled());

        widget<QPushButton>(rig.window, "recoverButton")->click();
        QCOMPARE(rig.controller.state(), app::AppState::Disconnected);
        QVERIFY(widget<QPushButton>(rig.window, "connectButton")->isEnabled());
    }
};

QTEST_MAIN(MonitoringUiTest)

#include "tst_monitoringui.moc"

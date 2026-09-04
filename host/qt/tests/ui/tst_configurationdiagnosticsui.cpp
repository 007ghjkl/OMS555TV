#include "app/AppStateController.h"
#include "app/MainWindow.h"
#include "communication/FakeModbusClient.h"
#include "configuration/ConfigurationService.h"
#include "diagnostics/CommunicationDiagnostics.h"
#include "logging/SessionLogService.h"
#include "monitor/MonitorService.h"
#include "ui/MonitoringViewModel.h"
#include "../monitor/ManualMonitorScheduler.h"

#include <QDoubleSpinBox>
#include <QFileInfo>
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

communication::ModbusConnectionConfig config()
{
    communication::ModbusConnectionConfig value;
    value.serial.portName = QStringLiteral("FAKE");
    return value;
}

communication::FakeOutcome readSuccess(QVector<quint16> values)
{
    communication::FakeOutcome outcome;
    outcome.kind = communication::FakeOutcomeKind::ReadSuccess;
    outcome.readValues = std::move(values);
    return outcome;
}

communication::FakeStep readStep(quint16 address, quint16 count,
                                 communication::FakeOutcome outcome)
{
    return {{communication::CommunicationOwner::ManualDebug,
             communication::ReadRequestDescriptor{device::PduAddress(address), count},
             std::chrono::milliseconds(500)}, std::move(outcome), {}, {}, {}};
}

communication::FakeStep writeStep(quint16 address, quint16 raw)
{
    communication::FakeOutcome outcome;
    outcome.kind = communication::FakeOutcomeKind::WriteSuccess;
    return {{communication::CommunicationOwner::ManualDebug,
             communication::WriteRequestDescriptor{device::PduAddress(address), raw},
             std::chrono::milliseconds(500)}, outcome, {}, {}, {}};
}

} // namespace

class ConfigurationDiagnosticsUiTest final : public QObject
{
    Q_OBJECT

private slots:
    void completeWorkflowUpdatesPagesAndGatesMonitoring()
    {
        QTemporaryDir output;
        QVERIFY(output.isValid());
        auto scheduler = std::make_shared<communication::ManualScheduler>();
        communication::FakeModbusClient client(scheduler);
        monitor::test::ManualMonitorScheduler monitorScheduler(scheduler);
        monitor::MonitorService monitorService(client, monitorScheduler);
        app::AppStateController controller(client, monitorService);
        EmptyPortCatalog ports;
        ui::MonitoringViewModel viewModel(controller, monitorService, ports);
        configuration::ConfigurationService configuration(controller, client);
        diagnostics::CommunicationDiagnosticsModel diagnostics(client);
        logging::SessionLogService logger(diagnostics, output.path());
        MainWindow window(viewModel, configuration, diagnostics, logger);

        auto *readButton = window.findChild<QPushButton *>(QStringLiteral("readThresholdsButton"));
        auto *writeButton = window.findChild<QPushButton *>(QStringLiteral("writeThresholdsButton"));
        auto *startSession = window.findChild<QPushButton *>(QStringLiteral("startSessionButton"));
        auto *endSession = window.findChild<QPushButton *>(QStringLiteral("endSessionButton"));
        auto *resultView = window.findChild<QPlainTextEdit *>(QStringLiteral("configurationResult"));
        auto *table = window.findChild<QTableWidget *>(QStringLiteral("diagnosticTable"));
        auto *requestFilter = window.findChild<QLineEdit *>(QStringLiteral("diagnosticRequestFilter"));
        QVERIFY(readButton && writeButton && startSession && endSession
                && resultView && table && requestFilter);
        QVERIFY(!readButton->isEnabled());

        QVERIFY(controller.connectDevice(config()).accepted());
        scheduler->runUntilIdle();
        QVERIFY(readButton->isEnabled());
        QVERIFY(writeButton->isEnabled());
        startSession->click();
        QVERIFY(logger.active());

        client.enqueueStep(readStep(9, 4, readSuccess({600, 610, 620, 400})));
        const std::array<quint16, 4> raw{650, 660, 670, 450};
        for (int index = 0; index < 4; ++index) {
            client.enqueueStep(writeStep(static_cast<quint16>(9 + index), raw[index]));
            client.enqueueStep(readStep(static_cast<quint16>(9 + index), 1,
                                        readSuccess({raw[index]})));
            auto *spin = window.findChild<QDoubleSpinBox *>(
                QStringLiteral("thresholdSpin%1").arg(index));
            QVERIFY(spin);
            spin->setValue(static_cast<double>(raw[index]) / 10.0);
        }
        writeButton->click();
        scheduler->runUntilIdle();
        QVERIFY(configuration.lastResult()->succeeded);
        QVERIFY(resultView->toPlainText().contains(QStringLiteral("成功")));
        QCOMPARE(table->rowCount(), 9);
        QVERIFY(logger.entries().size() >= 10);

        requestFilter->setText(QStringLiteral("1"));
        QCOMPARE(table->rowCount(), 1);
        requestFilter->clear();
        QCOMPARE(table->rowCount(), 9);
        endSession->click();
        QVERIFY(!logger.active());
        QVERIFY(QFileInfo::exists(logger.currentFilePath()));

        monitor::MonitorConfig monitorConfig;
        monitorConfig.targetPeriod = std::chrono::milliseconds(1000);
        QVERIFY(controller.startMonitoring(monitorConfig).accepted());
        scheduler->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(controller.state(), app::AppState::Monitoring);
        QVERIFY(!writeButton->isEnabled());
        QVERIFY(!readButton->isEnabled());
    }
};

QTEST_MAIN(ConfigurationDiagnosticsUiTest)

#include "tst_configurationdiagnosticsui.moc"

#include "app/MainWindow.h"
#include "app/AppStateController.h"
#include "communication/QSerialPortModbusClient.h"
#include "monitor/MonitorScheduler.h"
#include "monitor/MonitorService.h"
#include "ui/MonitoringViewModel.h"
#include "ui/SerialPortCatalog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("OMS555TV Host"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    oms555tv::communication::QSerialPortModbusClient client;
    oms555tv::monitor::QtMonitorScheduler scheduler;
    oms555tv::monitor::MonitorService monitorService(client, scheduler);
    oms555tv::app::AppStateController controller(client, monitorService);
    oms555tv::ui::QtSerialPortCatalog serialPorts;
    oms555tv::ui::MonitoringViewModel viewModel(controller, monitorService,
                                                serialPorts);
    MainWindow window(viewModel);
    window.show();

    if (application.arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(0, &application, &QCoreApplication::quit);
    }

    return application.exec();
}

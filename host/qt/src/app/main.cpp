#include "app/MainWindow.h"
#include "app/AppStateController.h"
#include "communication/QSerialPortModbusClient.h"
#include "configuration/ConfigurationService.h"
#include "diagnostics/CommunicationDiagnostics.h"
#include "logging/SessionLogService.h"
#include "monitor/MonitorScheduler.h"
#include "monitor/MonitorService.h"
#include "testing/TestAutomationController.h"
#include "testing/TestEngine.h"
#include "testing/TestResultManager.h"
#include "ui/MonitoringViewModel.h"
#include "ui/SerialPortCatalog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QTimer>

#include <utility>

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
    oms555tv::configuration::ConfigurationService configuration(controller, client);
    oms555tv::diagnostics::CommunicationDiagnosticsModel diagnostics(client);
    oms555tv::logging::SessionLogService sessionLog(diagnostics);
    oms555tv::testing::TestResultManager testResults;
    oms555tv::testing::TestEngine testEngine(
        controller, client, testResults, &sessionLog, scheduler);
    oms555tv::testing::TestAutomationController testAutomation(
        controller, testEngine, testResults);
    QObject::connect(&controller, &oms555tv::app::AppStateController::stateChanged,
                     &sessionLog, [&sessionLog](oms555tv::app::AppState state) {
        oms555tv::logging::LogEntry entry{
            QDateTime::currentDateTimeUtc(), oms555tv::logging::LogLevel::Info,
            QStringLiteral("app"),
            QStringLiteral("应用状态变更为 %1").arg(static_cast<int>(state))};
        entry.event = QStringLiteral("state_changed");
        sessionLog.append(std::move(entry));
    });
    QObject::connect(
        &configuration,
        &oms555tv::configuration::ConfigurationService::operationCompleted,
        &sessionLog,
        [&sessionLog](const oms555tv::configuration::ConfigurationOperationResult &result) {
            oms555tv::logging::LogEntry entry{
                QDateTime::currentDateTimeUtc(),
                result.succeeded ? oms555tv::logging::LogLevel::Info
                                 : oms555tv::logging::LogLevel::Warning,
                QStringLiteral("configuration"),
                QStringLiteral("配置操作 %1 %2")
                    .arg(result.operationId.value)
                    .arg(result.succeeded ? QStringLiteral("成功")
                                          : QStringLiteral("失败或取消"))};
            entry.event = QStringLiteral("operation_completed");
            entry.metadata.insert(QStringLiteral("operation_id"),
                                  static_cast<qulonglong>(result.operationId.value));
            entry.metadata.insert(QStringLiteral("succeeded"), result.succeeded);
            entry.metadata.insert(QStringLiteral("cancelled"), result.cancelled);
            sessionLog.append(std::move(entry));
        });
    MainWindow window(viewModel, configuration, diagnostics, sessionLog, testAutomation);
    window.show();

    if (application.arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(0, &application, &QCoreApplication::quit);
    }

    return application.exec();
}

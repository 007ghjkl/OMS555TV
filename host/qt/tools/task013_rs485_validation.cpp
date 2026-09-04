#include "app/AppStateController.h"
#include "communication/QSerialPortModbusClient.h"
#include "configuration/ConfigurationService.h"
#include "diagnostics/CommunicationDiagnostics.h"
#include "logging/SessionLogService.h"
#include "monitor/MonitorScheduler.h"
#include "monitor/MonitorService.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>

#include <array>
#include <optional>

using namespace oms555tv;

namespace {

std::array<double, 4> toValues(const device::AlarmThresholds &thresholds)
{
    return {thresholds.phaseA.celsius(), thresholds.phaseB.celsius(),
            thresholds.phaseC.celsius(), thresholds.ambient.celsius()};
}

bool equalThresholds(const device::AlarmThresholds &left,
                     const device::AlarmThresholds &right)
{
    return left.phaseA.deciCelsius == right.phaseA.deciCelsius
        && left.phaseB.deciCelsius == right.phaseB.deciCelsius
        && left.phaseC.deciCelsius == right.phaseC.deciCelsius
        && left.ambient.deciCelsius == right.ambient.deciCelsius;
}

QString thresholdText(const device::AlarmThresholds &value)
{
    return QStringLiteral("A=%1, B=%2, C=%3, ambient=%4 ℃")
        .arg(value.phaseA.celsius(), 0, 'f', 1)
        .arg(value.phaseB.celsius(), 0, 'f', 1)
        .arg(value.phaseC.celsius(), 0, 'f', 1)
        .arg(value.ambient.celsius(), 0, 'f', 1);
}

class ValidationRunner final : public QObject
{
public:
    ValidationRunner(QString portName, int timeoutMs, QObject *parent = nullptr)
        : QObject(parent)
        , portName_(std::move(portName))
        , timeoutMs_(timeoutMs)
        , monitorService_(client_, monitorScheduler_)
        , controller_(client_, monitorService_)
        , configuration_(controller_, client_)
        , diagnostics_(client_)
        , sessionLog_(diagnostics_)
    {
        connect(&controller_, &app::AppStateController::commandCompleted,
                this, [this](const app::AppCommandResult &result) {
            if (!result.succeeded) {
                fail(QStringLiteral("应用命令失败：%1").arg(static_cast<int>(result.command)));
                return;
            }
            if (result.command == app::AppCommand::Connect) {
                stage_ = Stage::ReadingBefore;
                if (!configuration_.readThresholds().accepted()) {
                    fail(QStringLiteral("初始阈值读取未被接受"));
                }
            } else if (result.command == app::AppCommand::Disconnect) {
                finish();
            }
        });
        connect(&configuration_, &configuration::ConfigurationService::operationCompleted,
                this, [this](const configuration::ConfigurationOperationResult &result) {
            handleConfigurationResult(result);
        });
        watchdog_.setSingleShot(true);
        watchdog_.setInterval(60000);
        connect(&watchdog_, &QTimer::timeout, this, [this] {
            fail(QStringLiteral("验收流程超过 60 秒"));
        });
    }

    void start()
    {
        const auto logResult = sessionLog_.startSession({
            {QStringLiteral("task"), QStringLiteral("TASK-013")},
            {QStringLiteral("port"), portName_},
            {QStringLiteral("baud"), 115200},
            {QStringLiteral("slave_id"), 1},
            {QStringLiteral("timeout_ms"), timeoutMs_},
        });
        if (!logResult.succeeded) {
            fail(QStringLiteral("无法创建验收会话日志：%1")
                     .arg(logResult.error->diagnostic));
            return;
        }
        QTextStream(stdout) << "SESSION_LOG=" << logResult.filePath << Qt::endl;
        watchdog_.start();
        communication::ModbusConnectionConfig config;
        config.serial.portName = portName_;
        config.serverAddress = 1;
        config.defaultResponseTimeout = std::chrono::milliseconds(timeoutMs_);
        if (!controller_.connectDevice(config).accepted()) {
            fail(QStringLiteral("连接命令未被接受"));
        }
    }

private:
    enum class Stage { Connecting, ReadingBefore, WritingTest, Restoring, FinalRead, Disconnecting };

    void handleConfigurationResult(
        const configuration::ConfigurationOperationResult &result)
    {
        if (stage_ == Stage::ReadingBefore) {
            if (!result.succeeded || !result.verifiedThresholds) {
                fail(QStringLiteral("读取测试前阈值失败"));
                return;
            }
            before_ = *result.verifiedThresholds;
            QTextStream(stdout) << "BEFORE=" << thresholdText(*before_) << Qt::endl;
            auto testValues = toValues(*before_);
            for (double &value : testValues) {
                value += value <= 79.8 ? 0.1 : -0.1;
            }
            stage_ = Stage::WritingTest;
            if (!configuration_.writeThresholds(testValues).accepted()) {
                fail(QStringLiteral("测试值写入未被接受"));
            }
            return;
        }

        if (stage_ == Stage::WritingTest) {
            testWriteSucceeded_ = result.succeeded;
            if (result.verifiedThresholds) {
                QTextStream(stdout) << "TEST_READBACK="
                                    << thresholdText(*result.verifiedThresholds) << Qt::endl;
            }
            restore();
            return;
        }

        if (stage_ == Stage::Restoring) {
            restoreSucceeded_ = result.succeeded;
            stage_ = Stage::FinalRead;
            if (!configuration_.readThresholds().accepted()) {
                fail(QStringLiteral("最终回读未被接受"));
            }
            return;
        }

        if (stage_ == Stage::FinalRead) {
            finalReadSucceeded_ = result.succeeded && result.verifiedThresholds
                && before_ && equalThresholds(*result.verifiedThresholds, *before_);
            if (result.verifiedThresholds) {
                QTextStream(stdout) << "FINAL="
                                    << thresholdText(*result.verifiedThresholds) << Qt::endl;
            }
            disconnect();
        }
    }

    void restore()
    {
        if (!before_) {
            fail(QStringLiteral("缺少测试前值，无法恢复"));
            return;
        }
        stage_ = Stage::Restoring;
        if (!configuration_.writeThresholds(toValues(*before_)).accepted()) {
            fail(QStringLiteral("恢复写入未被接受"));
        }
    }

    void disconnect()
    {
        stage_ = Stage::Disconnecting;
        if (!controller_.disconnectDevice().accepted()) {
            fail(QStringLiteral("断开命令未被接受"));
        }
    }

    void fail(const QString &message)
    {
        failed_ = true;
        QTextStream(stderr) << "ERROR=" << message << Qt::endl;
        logging::LogEntry entry{QDateTime::currentDateTimeUtc(), logging::LogLevel::Error,
                                QStringLiteral("validation"), message};
        entry.event = QStringLiteral("validation_error");
        sessionLog_.append(std::move(entry));
        if (configuration_.busy()) {
            (void)configuration_.cancel();
            return;
        }
        if (controller_.state() == app::AppState::ConnectedIdle) {
            disconnect();
        } else {
            finish();
        }
    }

    void finish()
    {
        watchdog_.stop();
        const bool passed = !failed_ && testWriteSucceeded_ && restoreSucceeded_
            && finalReadSucceeded_ && client_.activeOwner() == communication::CommunicationOwner::None;
        logging::LogEntry entry{QDateTime::currentDateTimeUtc(),
                                passed ? logging::LogLevel::Test : logging::LogLevel::Error,
                                QStringLiteral("validation"),
                                passed ? QStringLiteral("TASK-013 RS485 验收通过")
                                       : QStringLiteral("TASK-013 RS485 验收失败")};
        entry.event = QStringLiteral("validation_complete");
        sessionLog_.append(std::move(entry));
        const QString logPath = sessionLog_.currentFilePath();
        sessionLog_.endSession();
        QTextStream(stdout) << "REQUESTS=" << diagnostics_.records().size() << Qt::endl
                            << "RESTORE=" << (restoreSucceeded_ ? "PASS" : "FAIL") << Qt::endl
                            << "FINAL_READ=" << (finalReadSucceeded_ ? "PASS" : "FAIL") << Qt::endl
                            << "RESULT=" << (passed ? "PASS" : "FAIL") << Qt::endl
                            << "LOG=" << logPath << Qt::endl;
        QCoreApplication::exit(passed ? 0 : 2);
    }

    QString portName_;
    int timeoutMs_ = 500;
    communication::QSerialPortModbusClient client_;
    monitor::QtMonitorScheduler monitorScheduler_;
    monitor::MonitorService monitorService_;
    app::AppStateController controller_;
    configuration::ConfigurationService configuration_;
    diagnostics::CommunicationDiagnosticsModel diagnostics_;
    logging::SessionLogService sessionLog_;
    QTimer watchdog_;
    Stage stage_ = Stage::Connecting;
    std::optional<device::AlarmThresholds> before_;
    bool failed_ = false;
    bool testWriteSucceeded_ = false;
    bool restoreSucceeded_ = false;
    bool finalReadSucceeded_ = false;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("TASK-013 RS485 Validation"));
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption portOption({QStringLiteral("p"), QStringLiteral("port")},
                                  QStringLiteral("USB-RS485 串口"),
                                  QStringLiteral("port"));
    QCommandLineOption timeoutOption(QStringLiteral("timeout-ms"),
                                     QStringLiteral("响应超时，毫秒"),
                                     QStringLiteral("ms"), QStringLiteral("500"));
    parser.addOption(portOption);
    parser.addOption(timeoutOption);
    parser.process(application);
    if (!parser.isSet(portOption)) {
        QTextStream(stderr) << "必须指定 --port" << Qt::endl;
        return 1;
    }
    bool timeoutOk = false;
    const int timeoutMs = parser.value(timeoutOption).toInt(&timeoutOk);
    if (!timeoutOk || timeoutMs < 1 || timeoutMs > 60000) {
        QTextStream(stderr) << "--timeout-ms 必须为 1..60000" << Qt::endl;
        return 1;
    }
    ValidationRunner runner(parser.value(portOption), timeoutMs);
    QTimer::singleShot(0, &runner, [&runner] { runner.start(); });
    return application.exec();
}

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

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QSet>
#include <QTextStream>
#include <QTimer>

#include <optional>

using namespace oms555tv;

namespace {

QString thresholdText(const device::AlarmThresholds &value)
{
    return QStringLiteral("A=%1, B=%2, C=%3, ambient=%4 ℃")
        .arg(value.phaseA.celsius(), 0, 'f', 1)
        .arg(value.phaseB.celsius(), 0, 'f', 1)
        .arg(value.phaseC.celsius(), 0, 'f', 1)
        .arg(value.ambient.celsius(), 0, 'f', 1);
}

bool equalThresholds(const device::AlarmThresholds &left,
                     const device::AlarmThresholds &right)
{
    return left.phaseA.deciCelsius == right.phaseA.deciCelsius
        && left.phaseB.deciCelsius == right.phaseB.deciCelsius
        && left.phaseC.deciCelsius == right.phaseC.deciCelsius
        && left.ambient.deciCelsius == right.ambient.deciCelsius;
}

class ValidationRunner final : public QObject
{
public:
    ValidationRunner(QString portName, QString suitePath, int timeoutMs,
                     QObject *parent = nullptr)
        : QObject(parent)
        , portName_(std::move(portName))
        , suitePath_(std::move(suitePath))
        , timeoutMs_(timeoutMs)
        , monitorService_(client_, monitorScheduler_)
        , appState_(client_, monitorService_)
        , configuration_(appState_, client_)
        , diagnostics_(client_)
        , sessionLog_(diagnostics_)
        , engine_(appState_, client_, results_, &sessionLog_)
        , automation_(appState_, engine_, results_)
    {
        connect(&automation_, &testing::TestAutomationController::suiteChanged,
                this, [this] { connectDeviceAfterLoad(); });
        connect(&automation_, &testing::TestAutomationController::workflowFinished,
                this, [this] { handleWorkflowFinished(); });
        connect(&appState_, &app::AppStateController::commandCompleted,
                this, [this](const app::AppCommandResult &result) {
            if (result.command == app::AppCommand::Connect) {
                if (!result.succeeded) {
                    fail(QStringLiteral("连接命令失败"));
                    return;
                }
                stage_ = Stage::ReadingBaseline;
                if (!configuration_.readThresholds().accepted()) {
                    fail(QStringLiteral("测试前阈值读取未被接受"));
                }
            } else if (result.command == app::AppCommand::Disconnect) {
                if (!result.succeeded) failed_ = true;
                finish();
            }
        });
        connect(&configuration_, &configuration::ConfigurationService::operationCompleted,
                this, [this](const configuration::ConfigurationOperationResult &result) {
            handleThresholdResult(result);
        });
        watchdog_.setSingleShot(true);
        watchdog_.setInterval(60000);
        connect(&watchdog_, &QTimer::timeout, this, [this] {
            fail(QStringLiteral("验收流程超过 60 秒 watchdog"));
        });
    }

    void start()
    {
        const auto log = sessionLog_.startSession({
            {QStringLiteral("task"), QStringLiteral("TASK-016")},
            {QStringLiteral("host_version"), QCoreApplication::applicationVersion()},
            {QStringLiteral("firmware_expected"), QStringLiteral("0.2")},
            {QStringLiteral("hardware"), QStringLiteral("NUCLEO-F411RE / USART1 RS485")},
            {QStringLiteral("port"), portName_},
            {QStringLiteral("baud"), 115200},
            {QStringLiteral("slave_id"), 1},
            {QStringLiteral("timeout_ms"), timeoutMs_},
            {QStringLiteral("suite"), suitePath_},
        });
        if (!log.succeeded) {
            fail(QStringLiteral("无法创建会话日志：%1").arg(log.error->diagnostic));
            return;
        }
        logPath_ = log.filePath;
        QTextStream(stdout) << "SESSION_LOG=" << logPath_ << Qt::endl;
        watchdog_.start();
        stage_ = Stage::Loading;
        if (!automation_.loadSuiteFile(suitePath_)) {
            fail(QStringLiteral("套件加载命令被拒绝"));
        }
    }

private:
    enum class Stage { Starting, Loading, Connecting, ReadingBaseline, Running,
                       ReadingFinal, Disconnecting, Finished };

    void connectDeviceAfterLoad()
    {
        if (stage_ != Stage::Loading) return;
        if (!automation_.suite()) {
            QStringList errors;
            for (const auto &error : automation_.loadErrors()) {
                errors << QStringLiteral("%1 %2：%3")
                    .arg(testing::configErrorCodeName(error.code), error.path,
                         error.diagnostic);
            }
            fail(automation_.lastError().isEmpty()
                ? QStringLiteral("套件加载失败：%1").arg(errors.join(QStringLiteral("；")))
                : automation_.lastError());
            return;
        }
        QTextStream(stdout) << "SUITE=" << automation_.suite()->id
                            << " CASES=" << automation_.suite()->cases.size() << Qt::endl;
        stage_ = Stage::Connecting;
        communication::ModbusConnectionConfig config;
        config.serial.portName = portName_;
        config.serverAddress = 1;
        config.defaultResponseTimeout = std::chrono::milliseconds(timeoutMs_);
        if (!appState_.connectDevice(config).accepted()) {
            fail(QStringLiteral("连接命令未被接受"));
        }
    }

    void handleThresholdResult(
        const configuration::ConfigurationOperationResult &result)
    {
        if (stage_ == Stage::ReadingBaseline) {
            if (!result.succeeded || !result.verifiedThresholds) {
                fail(QStringLiteral("无法建立测试前阈值基线"));
                return;
            }
            baseline_ = *result.verifiedThresholds;
            QTextStream(stdout) << "BEFORE=" << thresholdText(*baseline_) << Qt::endl;
            stage_ = Stage::Running;
            monitor::MonitorConfig config;
            config.requestTimeout = std::chrono::milliseconds(timeoutMs_);
            if (!automation_.runAll(config)) {
                fail(QStringLiteral("自动化运行未被接受：%1").arg(automation_.lastError()));
            }
            return;
        }
        if (stage_ == Stage::ReadingFinal) {
            finalReadPassed_ = result.succeeded && result.verifiedThresholds && baseline_
                && equalThresholds(*result.verifiedThresholds, *baseline_);
            if (result.verifiedThresholds) {
                QTextStream(stdout) << "FINAL=" << thresholdText(*result.verifiedThresholds)
                                    << Qt::endl;
            }
            if (!finalReadPassed_) {
                failed_ = true;
                QTextStream(stderr) << "ERROR=最终阈值与测试前基线不一致" << Qt::endl;
            }
            disconnectDevice();
        }
    }

    void handleWorkflowFinished()
    {
        if (stage_ != Stage::Running) return;
        const auto snapshot = results_.snapshot();
        suitePassed_ = snapshot && snapshot->status == testing::TestStatus::Pass
            && snapshot->cases.size() >= 5;
        if (snapshot) {
            QSet<quint64> resultIds;
            for (const auto &testCase : snapshot->cases) {
                QString actual = testCase.assertion
                    ? testCase.assertion->actualSummary
                    : testCase.error ? testCase.error->diagnostic : QStringLiteral("--");
                QTextStream(stdout) << "CASE=" << testCase.caseId
                                    << " STATUS=" << testing::testStatusName(testCase.status)
                                    << " DURATION_MS=" << testCase.duration.count()
                                    << " ACTUAL=" << actual << Qt::endl;
                for (const auto &attempt : testCase.attempts) {
                    resultIds.insert(attempt.requestId.value);
                    const auto &evidence = attempt.requestResult.evidence;
                    QTextStream(stdout) << "ATTEMPT=" << attempt.sequence
                                        << " REQUEST_ID=" << attempt.requestId.value
                                        << " STEP=" << testing::testStepPurposeName(attempt.purpose)
                                        << " TX=" << diagnostics::byteArrayHex(evidence.txAdu)
                                        << " RX=" << diagnostics::byteArrayHex(evidence.rxAdu)
                                        << Qt::endl;
                    if (evidence.txAdu.isEmpty() || evidence.rxAdu.isEmpty()) evidencePassed_ = false;
                }
            }
            QSet<quint64> diagnosticIds;
            for (const auto &record : diagnostics_.records()) {
                if (record.result.evidence.owner == communication::CommunicationOwner::Testing) {
                    diagnosticIds.insert(record.result.requestId.value);
                }
            }
            QSet<quint64> logIds;
            for (const auto &entry : sessionLog_.entries()) {
                if (entry.module == QStringLiteral("testing")
                    && entry.event == QStringLiteral("request_attempt") && entry.requestId) {
                    logIds.insert(*entry.requestId);
                }
            }
            evidencePassed_ = evidencePassed_ && !resultIds.isEmpty()
                && resultIds == diagnosticIds && resultIds == logIds;
            QTextStream(stdout) << "RESULT_REQUEST_IDS=" << resultIds.size()
                                << " DIAGNOSTIC_REQUEST_IDS=" << diagnosticIds.size()
                                << " TEST_LOG_REQUEST_IDS=" << logIds.size() << Qt::endl;
        }
        if (!suitePassed_ || !evidencePassed_ || !automation_.lastError().isEmpty()) {
            failed_ = true;
            QTextStream(stderr) << "ERROR=套件状态或 RequestId 证据核对失败："
                                << automation_.lastError() << Qt::endl;
        }
        stage_ = Stage::ReadingFinal;
        if (!configuration_.readThresholds().accepted()) {
            fail(QStringLiteral("最终阈值读取未被接受"));
        }
    }

    void disconnectDevice()
    {
        stage_ = Stage::Disconnecting;
        if (!appState_.disconnectDevice().accepted()) {
            fail(QStringLiteral("断开连接命令未被接受"));
        }
    }

    void fail(const QString &diagnostic)
    {
        if (stage_ == Stage::Finished) return;
        failed_ = true;
        QTextStream(stderr) << "ERROR=" << diagnostic << Qt::endl;
        if (automation_.state() == testing::TestAutomationState::Running) {
            (void)automation_.abort();
            return;
        }
        if (configuration_.busy()) {
            (void)configuration_.cancel();
            return;
        }
        if (appState_.state() == app::AppState::ConnectedIdle) {
            disconnectDevice();
            return;
        }
        finish();
    }

    void finish()
    {
        if (stage_ == Stage::Finished) return;
        stage_ = Stage::Finished;
        watchdog_.stop();
        const bool passed = !failed_ && suitePassed_ && evidencePassed_
            && finalReadPassed_
            && client_.activeOwner() == communication::CommunicationOwner::None
            && appState_.state() == app::AppState::Disconnected;
        logging::LogEntry entry{QDateTime::currentDateTimeUtc(),
                                passed ? logging::LogLevel::Test : logging::LogLevel::Error,
                                QStringLiteral("validation"),
                                passed ? QStringLiteral("TASK-016 RS485 验收通过")
                                       : QStringLiteral("TASK-016 RS485 验收失败")};
        entry.event = QStringLiteral("validation_complete");
        sessionLog_.append(std::move(entry));
        sessionLog_.endSession();
        QTextStream(stdout) << "SUITE_RESULT=" << (suitePassed_ ? "PASS" : "FAIL") << Qt::endl
                            << "EVIDENCE=" << (evidencePassed_ ? "PASS" : "FAIL") << Qt::endl
                            << "THRESHOLD_RESTORE=" << (finalReadPassed_ ? "PASS" : "FAIL") << Qt::endl
                            << "LOG=" << logPath_ << Qt::endl
                            << "RESULT=" << (passed ? "PASS" : "FAIL") << Qt::endl;
        QCoreApplication::exit(passed ? 0 : 2);
    }

    QString portName_;
    QString suitePath_;
    int timeoutMs_ = 500;
    communication::QSerialPortModbusClient client_;
    monitor::QtMonitorScheduler monitorScheduler_;
    monitor::MonitorService monitorService_;
    app::AppStateController appState_;
    configuration::ConfigurationService configuration_;
    diagnostics::CommunicationDiagnosticsModel diagnostics_;
    logging::SessionLogService sessionLog_;
    testing::TestResultManager results_;
    testing::TestEngine engine_;
    testing::TestAutomationController automation_;
    QTimer watchdog_;
    Stage stage_ = Stage::Starting;
    QString logPath_;
    std::optional<device::AlarmThresholds> baseline_;
    bool failed_ = false;
    bool suitePassed_ = false;
    bool evidencePassed_ = true;
    bool finalReadPassed_ = false;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("TASK-016 RS485 Validation"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption portOption({QStringLiteral("p"), QStringLiteral("port")},
                                  QStringLiteral("USB-RS485 串口"), QStringLiteral("port"));
    QCommandLineOption suiteOption(QStringLiteral("suite"), QStringLiteral("JSON 测试套件"),
                                   QStringLiteral("path"));
    QCommandLineOption timeoutOption(QStringLiteral("timeout-ms"),
                                     QStringLiteral("响应超时，毫秒"),
                                     QStringLiteral("ms"), QStringLiteral("500"));
    parser.addOption(portOption);
    parser.addOption(suiteOption);
    parser.addOption(timeoutOption);
    parser.process(application);
    if (!parser.isSet(portOption) || !parser.isSet(suiteOption)) {
        QTextStream(stderr) << "必须指定 --port 和 --suite" << Qt::endl;
        return 1;
    }
    bool timeoutOk = false;
    const int timeoutMs = parser.value(timeoutOption).toInt(&timeoutOk);
    if (!timeoutOk || timeoutMs < 1 || timeoutMs > 60000) {
        QTextStream(stderr) << "--timeout-ms 必须为 1..60000" << Qt::endl;
        return 1;
    }
    ValidationRunner runner(parser.value(portOption), parser.value(suiteOption), timeoutMs);
    QTimer::singleShot(0, &runner, [&runner] { runner.start(); });
    return application.exec();
}

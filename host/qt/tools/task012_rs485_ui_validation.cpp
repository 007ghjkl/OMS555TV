#include "app/AppStateController.h"
#include "app/MainWindow.h"
#include "communication/QSerialPortModbusClient.h"
#include "monitor/MonitorScheduler.h"
#include "monitor/MonitorService.h"
#include "ui/MonitoringViewModel.h"
#include "ui/SerialPortCatalog.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <optional>

using namespace oms555tv;

namespace {

QString healthName(monitor::DeviceHealth health)
{
    switch (health) {
    case monitor::DeviceHealth::Unknown:
        return QStringLiteral("Unknown");
    case monitor::DeviceHealth::Online:
        return QStringLiteral("Online");
    case monitor::DeviceHealth::Degraded:
        return QStringLiteral("Degraded");
    case monitor::DeviceHealth::Offline:
        return QStringLiteral("Offline");
    }
    return QStringLiteral("Unknown");
}

QString requestStateName(communication::RequestState state)
{
    switch (state) {
    case communication::RequestState::Queued:
        return QStringLiteral("Queued");
    case communication::RequestState::InFlight:
        return QStringLiteral("InFlight");
    case communication::RequestState::Succeeded:
        return QStringLiteral("Succeeded");
    case communication::RequestState::Failed:
        return QStringLiteral("Failed");
    case communication::RequestState::Cancelled:
        return QStringLiteral("Cancelled");
    }
    return QStringLiteral("Unknown");
}

QString milliseconds(std::optional<std::chrono::nanoseconds> value,
                     int decimals = 3)
{
    if (!value) {
        return QStringLiteral("NA");
    }
    return QString::number(
        std::chrono::duration<double, std::milli>(*value).count(), 'f', decimals);
}

class ValidationRun final : public QObject
{
public:
    ValidationRun(QApplication &application,
                  ui::MonitoringViewModel &viewModel,
                  app::AppStateController &controller,
                  monitor::MonitorService &monitorService,
                  const QString &port,
                  int durationSeconds,
                  int targetPeriodMs,
                  int timeoutMs,
                  bool recoveryValidation,
                  QString benchNotes,
                  QFile &logFile)
        : application_(application)
        , viewModel_(viewModel)
        , controller_(controller)
        , monitorService_(monitorService)
        , port_(port)
        , durationSeconds_(durationSeconds)
        , targetPeriodMs_(targetPeriodMs)
        , timeoutMs_(timeoutMs)
        , recoveryValidation_(recoveryValidation)
        , benchNotes_(std::move(benchNotes))
        , logFile_(logFile)
        , log_(&logFile_)
    {
        heartbeatTimer_.setInterval(100);
        deadlineTimer_.setSingleShot(true);
        watchdogTimer_.setSingleShot(true);
        portRetryTimer_.setInterval(250);
        connect(&heartbeatTimer_, &QTimer::timeout, this, [this] { heartbeat(); });
        connect(&deadlineTimer_, &QTimer::timeout, this, [this] { requestStop(); });
        connect(&watchdogTimer_, &QTimer::timeout, this, [this] {
            finish(false, QStringLiteral("验证流程看门狗超时"));
        });
        connect(&portRetryTimer_, &QTimer::timeout, this, [this] {
            retryExplicitReconnect();
        });
        connect(&controller_, &app::AppStateController::stateChanged,
                this, [this](app::AppState state) { handleState(state); });
        connect(&controller_, &app::AppStateController::commandCompleted,
                this, [this](const app::AppCommandResult &result) {
            if (!result.succeeded) {
                finish(false, QStringLiteral("控制命令失败：%1")
                                  .arg(static_cast<int>(result.command)));
            }
        });
        connect(&monitorService_, &monitor::MonitorService::batchCompleted,
                this, [this](const monitor::PollBatchResult &batch) {
            recordBatch(batch);
        });
        connect(&monitorService_, &monitor::MonitorService::healthChanged,
                this, [this](monitor::DeviceHealth health) {
            if (health == monitor::DeviceHealth::Degraded) {
                everDegraded_ = true;
            } else if (health == monitor::DeviceHealth::Offline) {
                everOffline_ = true;
            }
            event(QStringLiteral("HEALTH"), healthName(health));
            if (recoveryValidation_ && phase_ == Phase::AwaitingDisruption
                && health == monitor::DeviceHealth::Offline) {
                disruptionObserved_ = true;
                const auto &uiState = viewModel_.state();
                event(QStringLiteral("UI_OFFLINE_STATE"),
                      QStringLiteral("app=%1;health=%2;freshness=%3;error=%4")
                          .arg(uiState.appStateText, uiState.deviceHealthText,
                               uiState.dataFreshnessText, uiState.lastErrorText));
                phase_ = Phase::Recovering;
                event(QStringLiteral("COMMAND"),
                      QStringLiteral("explicit-disconnect-after-offline"));
                viewModel_.disconnectDevice();
            }
        });
        connect(&monitorService_, &monitor::MonitorService::snapshotPublished,
                this, [this](const monitor::MonitoringSnapshot &snapshot) {
            const quint32 uptime = snapshot.snapshot.diagnostics.uptimeSeconds;
            if (!initialUptime_) {
                initialUptime_ = uptime;
            }
            finalUptime_ = uptime;
            finalSnapshot_ = snapshot;
        });
    }

    void start()
    {
        log_ << "META\tstarted_utc\t"
             << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << '\n';
        log_ << "META\thost_version\t" << QCoreApplication::applicationVersion() << '\n';
        log_ << "META\tqt_version\t" << QString::fromLatin1(qVersion()) << '\n';
        log_ << "META\tselected_port\t" << port_ << '\n';
        log_ << "META\tserial\t115200 8N1 flow=None slave=1 timeout_ms="
             << timeoutMs_ << '\n';
        log_ << "META\ttarget_period_ms\t" << targetPeriodMs_ << '\n';
        log_ << "META\trequested_duration_s\t" << durationSeconds_ << '\n';
        log_ << "META\trecovery_validation\t" << (recoveryValidation_ ? 1 : 0)
             << '\n';
        log_ << "META\tbench_notes\t" << benchNotes_ << '\n';
        const auto &ports = viewModel_.state().ports;
        for (const auto &entry : ports) {
            log_ << "PORT\t" << entry.portName << '\t' << entry.description << '\t'
                 << entry.manufacturer << '\t' << entry.serialNumber << '\t'
                 << (entry.vendorIdentifier
                         ? QString::number(*entry.vendorIdentifier, 16)
                         : QStringLiteral("NA"))
                 << '\t'
                 << (entry.productIdentifier
                         ? QString::number(*entry.productIdentifier, 16)
                         : QStringLiteral("NA")) << '\n';
        }
        logFile_.flush();

        const auto selected = std::find_if(ports.cbegin(), ports.cend(), [this](const auto &entry) {
            return entry.portName.compare(port_, Qt::CaseInsensitive) == 0;
        });
        if (selected == ports.cend()) {
            finish(false, QStringLiteral("指定串口不在运行时枚举结果中"));
            return;
        }
        viewModel_.setSelectedPortName(selected->portName);
        viewModel_.setSlaveAddress(1);
        viewModel_.setResponseTimeoutMs(timeoutMs_);
        viewModel_.setTargetPeriodMs(targetPeriodMs_);
        phase_ = Phase::Connecting;
        event(QStringLiteral("COMMAND"), QStringLiteral("connect"));
        viewModel_.connectDevice();
        watchdogTimer_.start(recoveryValidation_ ? 120000
                                                 : (durationSeconds_ + 30) * 1000);
    }

private:
    enum class Phase {
        Initial,
        Connecting,
        Monitoring,
        AwaitingDisruption,
        Recovering,
        Reconnecting,
        PostRecovery,
        Stopping,
        Disconnecting,
        Done
    };

    void handleState(app::AppState state)
    {
        event(QStringLiteral("APP_STATE"), QString::number(static_cast<int>(state)));
        if (phase_ == Phase::Connecting && state == app::AppState::ConnectedIdle) {
            event(QStringLiteral("COMMAND"), QStringLiteral("start-monitoring"));
            viewModel_.startMonitoring();
            return;
        }
        if (phase_ == Phase::Connecting && state == app::AppState::Monitoring) {
            phase_ = Phase::Monitoring;
            monitoringElapsed_.start();
            lastHeartbeatMs_ = 0;
            heartbeatTimer_.start();
            if (!recoveryValidation_) {
                deadlineTimer_.start(durationSeconds_ * 1000);
            }
            event(QStringLiteral("MONITORING_STARTED"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
            return;
        }
        if (phase_ == Phase::Reconnecting && state == app::AppState::ConnectedIdle) {
            event(QStringLiteral("COMMAND"), QStringLiteral("start-monitoring-after-reconnect"));
            viewModel_.startMonitoring();
            return;
        }
        if (phase_ == Phase::Reconnecting && state == app::AppState::Monitoring) {
            phase_ = Phase::PostRecovery;
            reconnected_ = true;
            postRecoveryStartBatchCount_ = monitorService_.statistics().batchesSucceeded;
            event(QStringLiteral("POST_RECOVERY_MONITORING"),
                  QStringLiteral("explicit reconnect complete"));
            return;
        }
        if (phase_ == Phase::Recovering && state == app::AppState::Disconnected) {
            explicitRecoveryCompleted_ = true;
            event(QStringLiteral("EXPLICIT_RECOVER_COMPLETE"),
                  QStringLiteral("application returned to Disconnected"));
            portRetryTimer_.start();
            retryExplicitReconnect();
            return;
        }
        if (phase_ == Phase::Stopping && state == app::AppState::ConnectedIdle) {
            phase_ = Phase::Disconnecting;
            event(QStringLiteral("COMMAND"), QStringLiteral("disconnect"));
            viewModel_.disconnectDevice();
            return;
        }
        if (phase_ == Phase::Disconnecting && state == app::AppState::Disconnected) {
            const auto stats = monitorService_.statistics();
            const bool uptimeAdvanced = initialUptime_ && finalUptime_
                && *finalUptime_ > *initialUptime_;
            const bool passed = recoveryValidation_
                ? recoveryReady_ && disruptionObserved_ && explicitRecoveryCompleted_
                    && reconnected_ && postRecoveryBatchesCompleted_
                    && finalSnapshot_.has_value() && maximumHeartbeatDelayMs_ < 1000
                : monitoredDurationMs_ >= durationSeconds_ * 1000LL
                    && stats.batchesSucceeded > 0 && stats.failed == 0
                    && stats.timedOut == 0 && !everDegraded_ && !everOffline_
                    && finalSnapshot_.has_value() && uptimeAdvanced
                    && maximumHeartbeatDelayMs_ < 1000;
            finish(passed, passed ? QStringLiteral("验收条件全部满足")
                                  : QStringLiteral("最终验收条件未全部满足"));
            return;
        }
        if (state == app::AppState::Error && recoveryValidation_
            && (phase_ == Phase::AwaitingDisruption || phase_ == Phase::Monitoring)) {
            disruptionObserved_ = true;
            const auto &uiState = viewModel_.state();
            event(QStringLiteral("UI_ERROR_STATE"),
                  QStringLiteral("app=%1;connection=%2;freshness=%3;error=%4")
                      .arg(uiState.appStateText, uiState.connectionStateText,
                           uiState.dataFreshnessText, uiState.lastErrorText));
            phase_ = Phase::Recovering;
            QTimer::singleShot(250, this, [this] {
                event(QStringLiteral("COMMAND"), QStringLiteral("explicit-recover"));
                viewModel_.recover();
            });
            return;
        }
        if (state == app::AppState::Error) {
            finish(false, QStringLiteral("应用进入 Error 状态"));
        }
    }

    void retryExplicitReconnect()
    {
        if (phase_ != Phase::Recovering || controller_.state() != app::AppState::Disconnected) {
            return;
        }
        viewModel_.refreshPorts();
        const auto &ports = viewModel_.state().ports;
        const auto selected = std::find_if(ports.cbegin(), ports.cend(), [this](const auto &entry) {
            return entry.portName.compare(port_, Qt::CaseInsensitive) == 0;
        });
        if (selected == ports.cend()) {
            return;
        }
        portRetryTimer_.stop();
        viewModel_.setSelectedPortName(selected->portName);
        viewModel_.setSlaveAddress(1);
        viewModel_.setResponseTimeoutMs(timeoutMs_);
        viewModel_.setTargetPeriodMs(targetPeriodMs_);
        phase_ = Phase::Reconnecting;
        event(QStringLiteral("COMMAND"), QStringLiteral("explicit-reconnect"));
        viewModel_.connectDevice();
    }

    void heartbeat()
    {
        const qint64 now = monitoringElapsed_.elapsed();
        if (lastHeartbeatMs_ != 0) {
            maximumHeartbeatDelayMs_ = std::max(
                maximumHeartbeatDelayMs_, std::max<qint64>(0, now - lastHeartbeatMs_ - 100));
        }
        lastHeartbeatMs_ = now;
        ++heartbeatCount_;
        if (heartbeatCount_ % 10 == 0) {
            log_ << "HEARTBEAT\t"
                 << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
                 << '\t' << now << '\t' << maximumHeartbeatDelayMs_ << '\n';
            logFile_.flush();
        }
    }

    void requestStop()
    {
        if (phase_ != Phase::Monitoring) {
            return;
        }
        const qint64 requiredDurationMs = static_cast<qint64>(durationSeconds_) * 1000;
        const qint64 elapsedMs = monitoringElapsed_.elapsed();
        if (elapsedMs < requiredDurationMs) {
            deadlineTimer_.start(static_cast<int>(requiredDurationMs - elapsedMs));
            return;
        }
        monitoredDurationMs_ = elapsedMs;
        preStopHealth_ = monitorService_.health();
        phase_ = Phase::Stopping;
        deadlineTimer_.stop();
        heartbeatTimer_.stop();
        event(QStringLiteral("COMMAND"), QStringLiteral("stop-monitoring"));
        viewModel_.stopMonitoring();
    }

    void recordBatch(const monitor::PollBatchResult &batch)
    {
        const auto &stats = monitorService_.statistics();
        for (const auto &request : batch.requestResults) {
            log_ << "TXN\t" << batch.batchId.value << '\t' << request.requestId.value
                 << '\t' << requestStateName(request.state) << '\t'
                 << milliseconds(request.evidence.rtt) << '\t'
                 << QString::fromLatin1(request.evidence.txAdu.toHex()) << '\t'
                 << QString::fromLatin1(request.evidence.rxAdu.toHex()) << '\t'
                 << (request.error
                         ? QString::number(static_cast<int>(request.error->code))
                         : QStringLiteral("NA")) << '\n';
        }
        log_ << "BATCH\t"
             << batch.completedUtc.toString(Qt::ISODateWithMs) << '\t'
             << batch.batchId.value << '\t' << (batch.complete ? 1 : 0) << '\t'
             << (batch.stopped ? 1 : 0) << '\t'
             << healthName(monitorService_.health()) << '\t'
             << milliseconds(batch.effectiveInterval, 1) << '\t'
             << milliseconds(batch.duration, 1) << '\t'
             << (batch.overrun ? 1 : 0) << '\t'
             << stats.requests << '\t' << stats.succeeded << '\t' << stats.failed
             << '\t' << stats.timedOut << '\t' << milliseconds(stats.recentRtt)
             << '\t'
             << (finalUptime_ ? QString::number(*finalUptime_) : QStringLiteral("NA"))
             << '\n';
        logFile_.flush();

        if (recoveryValidation_ && phase_ == Phase::Monitoring
            && stats.batchesSucceeded >= 5) {
            phase_ = Phase::AwaitingDisruption;
            recoveryReady_ = true;
            event(QStringLiteral("RECOVERY_READY"),
                  QStringLiteral("disable the selected serial device now"));
        } else if (recoveryValidation_ && phase_ == Phase::PostRecovery
                   && stats.batchesSucceeded >= postRecoveryStartBatchCount_ + 10) {
            postRecoveryBatchesCompleted_ = true;
            monitoredDurationMs_ = monitoringElapsed_.elapsed();
            preStopHealth_ = monitorService_.health();
            phase_ = Phase::Stopping;
            heartbeatTimer_.stop();
            event(QStringLiteral("COMMAND"), QStringLiteral("stop-monitoring-after-recovery"));
            viewModel_.stopMonitoring();
        }
    }

    void event(const QString &kind, const QString &value)
    {
        log_ << "EVENT\t" << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
             << '\t' << kind << '\t' << value << '\n';
        logFile_.flush();
    }

    void finish(bool passed, const QString &reason)
    {
        if (phase_ == Phase::Done) {
            return;
        }
        phase_ = Phase::Done;
        heartbeatTimer_.stop();
        deadlineTimer_.stop();
        watchdogTimer_.stop();
        portRetryTimer_.stop();
        const auto &stats = monitorService_.statistics();
        log_ << "SUMMARY\tended_utc\t"
             << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << '\n';
        log_ << "SUMMARY\tmonitored_duration_ms\t" << monitoredDurationMs_ << '\n';
        log_ << "SUMMARY\trequests\t" << stats.requests << '\n';
        log_ << "SUMMARY\tsucceeded\t" << stats.succeeded << '\n';
        log_ << "SUMMARY\tfailed\t" << stats.failed << '\n';
        log_ << "SUMMARY\ttimed_out\t" << stats.timedOut << '\n';
        log_ << "SUMMARY\tbatches_started\t" << stats.batchesStarted << '\n';
        log_ << "SUMMARY\tbatches_succeeded\t" << stats.batchesSucceeded << '\n';
        log_ << "SUMMARY\tbatches_failed\t" << stats.batchesFailed << '\n';
        log_ << "SUMMARY\toverruns\t" << stats.overruns << '\n';
        log_ << "SUMMARY\trtt_min_ms\t" << milliseconds(stats.minimumRtt) << '\n';
        log_ << "SUMMARY\trtt_avg_ms\t"
             << (stats.rttSamples == 0
                     ? QStringLiteral("NA")
                     : milliseconds(std::chrono::nanoseconds(
                           stats.totalRttNanoseconds / stats.rttSamples))) << '\n';
        log_ << "SUMMARY\trtt_max_ms\t" << milliseconds(stats.maximumRtt) << '\n';
        log_ << "SUMMARY\tpre_stop_health\t" << healthName(preStopHealth_) << '\n';
        log_ << "SUMMARY\tever_degraded\t" << (everDegraded_ ? 1 : 0) << '\n';
        log_ << "SUMMARY\tever_offline\t" << (everOffline_ ? 1 : 0) << '\n';
        log_ << "SUMMARY\tinitial_uptime_s\t"
             << (initialUptime_ ? QString::number(*initialUptime_) : QStringLiteral("NA"))
             << '\n';
        log_ << "SUMMARY\tfinal_uptime_s\t"
             << (finalUptime_ ? QString::number(*finalUptime_) : QStringLiteral("NA"))
             << '\n';
        log_ << "SUMMARY\tui_heartbeat_max_delay_ms\t" << maximumHeartbeatDelayMs_ << '\n';
        log_ << "SUMMARY\trecovery_ready\t" << (recoveryReady_ ? 1 : 0) << '\n';
        log_ << "SUMMARY\tdisruption_observed\t" << (disruptionObserved_ ? 1 : 0)
             << '\n';
        log_ << "SUMMARY\texplicit_recovery_completed\t"
             << (explicitRecoveryCompleted_ ? 1 : 0) << '\n';
        log_ << "SUMMARY\treconnected\t" << (reconnected_ ? 1 : 0) << '\n';
        log_ << "SUMMARY\tpost_recovery_batches_completed\t"
             << (postRecoveryBatchesCompleted_ ? 1 : 0) << '\n';
        if (finalSnapshot_) {
            const auto &snapshot = finalSnapshot_->snapshot;
            log_ << "SUMMARY\tfirmware\t" << snapshot.firmwareVersion.major << '.'
                 << snapshot.firmwareVersion.minor << '\n';
            log_ << "SUMMARY\tdevice_status_hex\t"
                 << QString::number(snapshot.status.device.raw, 16) << '\n';
            log_ << "SUMMARY\talarm_status_hex\t"
                 << QString::number(snapshot.status.alarms.raw, 16) << '\n';
            log_ << "SUMMARY\ttemperatures_c\t"
                 << snapshot.measurements.phaseATemperature.celsius() << ','
                 << snapshot.measurements.phaseBTemperature.celsius() << ','
                 << snapshot.measurements.phaseCTemperature.celsius() << ','
                 << snapshot.measurements.ambientTemperature.celsius() << '\n';
            log_ << "SUMMARY\tlight_mv\t" << snapshot.measurements.lightMillivolts << '\n';
        }
        log_ << "RESULT\t" << (passed ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
             << '\t' << reason << '\n';
        log_.flush();
        logFile_.flush();
        application_.exit(passed ? 0 : 4);
    }

    QApplication &application_;
    ui::MonitoringViewModel &viewModel_;
    app::AppStateController &controller_;
    monitor::MonitorService &monitorService_;
    QString port_;
    int durationSeconds_ = 0;
    int targetPeriodMs_ = 0;
    int timeoutMs_ = 0;
    bool recoveryValidation_ = false;
    QString benchNotes_;
    QFile &logFile_;
    QTextStream log_;
    Phase phase_ = Phase::Initial;
    QTimer heartbeatTimer_;
    QTimer deadlineTimer_;
    QTimer watchdogTimer_;
    QTimer portRetryTimer_;
    QElapsedTimer monitoringElapsed_;
    qint64 lastHeartbeatMs_ = 0;
    qint64 maximumHeartbeatDelayMs_ = 0;
    qint64 monitoredDurationMs_ = 0;
    quint64 heartbeatCount_ = 0;
    bool everDegraded_ = false;
    bool everOffline_ = false;
    bool recoveryReady_ = false;
    bool disruptionObserved_ = false;
    bool explicitRecoveryCompleted_ = false;
    bool reconnected_ = false;
    bool postRecoveryBatchesCompleted_ = false;
    quint64 postRecoveryStartBatchCount_ = 0;
    monitor::DeviceHealth preStopHealth_ = monitor::DeviceHealth::Unknown;
    std::optional<quint32> initialUptime_;
    std::optional<quint32> finalUptime_;
    std::optional<monitor::MonitoringSnapshot> finalSnapshot_;
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    QCoreApplication::setApplicationName(QStringLiteral("TASK-012 RS485 UI 验证"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("port"), QStringLiteral("运行时枚举中的串口名。"),
                      QStringLiteral("name")});
    parser.addOption({QStringLiteral("duration-seconds"), QStringLiteral("监控时长（秒）。"),
                      QStringLiteral("seconds"), QStringLiteral("1800")});
    parser.addOption({QStringLiteral("period-ms"), QStringLiteral("目标周期（100/500/1000/2000）。"),
                      QStringLiteral("milliseconds"), QStringLiteral("500")});
    parser.addOption({QStringLiteral("timeout-ms"), QStringLiteral("单请求超时（毫秒）。"),
                      QStringLiteral("milliseconds"), QStringLiteral("500")});
    parser.addOption({QStringLiteral("log"), QStringLiteral("原始记录输出路径。"),
                      QStringLiteral("path")});
    parser.addOption({QStringLiteral("bench-notes"), QStringLiteral("台架说明。"),
                      QStringLiteral("text"),
                      QStringLiteral("约20 cm安全低压点对点；自动换向TTL-RS485；DTECH USB-RS485")});
    parser.addOption({QStringLiteral("recovery-validation"),
                      QStringLiteral("运行受控串口消失与显式恢复验证。")});
    parser.process(application);

    bool durationOk = false;
    bool periodOk = false;
    bool timeoutOk = false;
    const int duration = parser.value(QStringLiteral("duration-seconds")).toInt(&durationOk);
    const int period = parser.value(QStringLiteral("period-ms")).toInt(&periodOk);
    const int timeout = parser.value(QStringLiteral("timeout-ms")).toInt(&timeoutOk);
    const QString port = parser.value(QStringLiteral("port")).trimmed();
    const QString logPath = parser.value(QStringLiteral("log")).trimmed();
    const bool allowedPeriod = period == 100 || period == 500
        || period == 1000 || period == 2000;
    if (!durationOk || duration < 1 || !periodOk || !allowedPeriod
        || !timeoutOk || timeout < 1 || timeout > 60000
        || port.isEmpty() || logPath.isEmpty()) {
        QTextStream(stderr) << "参数无效：必须指定 --port 和 --log，时长为正数，"
                               "周期为 100/500/1000/2000 ms，超时为 1..60000 ms。\n";
        return 2;
    }
    const QFileInfo logInfo(logPath);
    if (!QDir().mkpath(logInfo.absolutePath())) {
        QTextStream(stderr) << "无法创建日志目录。\n";
        return 2;
    }
    QFile logFile(logInfo.absoluteFilePath());
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream(stderr) << "无法创建原始记录：" << logFile.errorString() << '\n';
        return 2;
    }

    communication::QSerialPortModbusClient client;
    monitor::QtMonitorScheduler scheduler;
    monitor::MonitorService monitorService(client, scheduler);
    app::AppStateController controller(client, monitorService);
    ui::QtSerialPortCatalog serialPorts;
    ui::MonitoringViewModel viewModel(controller, monitorService, serialPorts);
    MainWindow window(viewModel);
    window.show();

    ValidationRun run(application, viewModel, controller, monitorService, port,
                      duration, period, timeout,
                      parser.isSet(QStringLiteral("recovery-validation")),
                      parser.value(QStringLiteral("bench-notes")), logFile);
    QTimer::singleShot(0, &run, [&run] { run.start(); });
    return application.exec();
}

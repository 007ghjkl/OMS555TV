#include "ui/MonitoringViewModel.h"

#include <QStringList>

#include <algorithm>
#include <chrono>

namespace oms555tv::ui {
namespace {

QString appStateText(app::AppState state)
{
    switch (state) {
    case app::AppState::Disconnected:
        return QStringLiteral("未连接");
    case app::AppState::ConnectedIdle:
        return QStringLiteral("已连接（空闲）");
    case app::AppState::Monitoring:
        return QStringLiteral("实时监控中");
    case app::AppState::Testing:
        return QStringLiteral("测试模式");
    case app::AppState::Stopping:
        return QStringLiteral("正在停止");
    case app::AppState::Error:
        return QStringLiteral("错误");
    }
    return QStringLiteral("未知");
}

QString connectionStateText(communication::ConnectionState state)
{
    switch (state) {
    case communication::ConnectionState::Disconnected:
        return QStringLiteral("串口已关闭");
    case communication::ConnectionState::Opening:
        return QStringLiteral("正在打开串口");
    case communication::ConnectionState::Connected:
        return QStringLiteral("串口已连接");
    case communication::ConnectionState::Closing:
        return QStringLiteral("正在关闭串口");
    case communication::ConnectionState::Faulted:
        return QStringLiteral("串口故障");
    }
    return QStringLiteral("串口状态未知");
}

QString healthText(monitor::DeviceHealth health)
{
    switch (health) {
    case monitor::DeviceHealth::Unknown:
        return QStringLiteral("未知");
    case monitor::DeviceHealth::Online:
        return QStringLiteral("在线");
    case monitor::DeviceHealth::Degraded:
        return QStringLiteral("退化（连续通信失败）");
    case monitor::DeviceHealth::Offline:
        return QStringLiteral("离线");
    }
    return QStringLiteral("未知");
}

QString formatTemperature(const device::Temperature &temperature)
{
    return QStringLiteral("%1 ℃").arg(temperature.celsius(), 0, 'f', 1);
}

QString formatMilliseconds(std::chrono::nanoseconds value, int decimals)
{
    return QStringLiteral("%1 ms").arg(
        std::chrono::duration<double, std::milli>(value).count(), 0, 'f', decimals);
}

QString formatUptime(quint32 seconds)
{
    const quint32 hours = seconds / 3600U;
    const quint32 minutes = (seconds % 3600U) / 60U;
    const quint32 remaining = seconds % 60U;
    return QStringLiteral("%1 s（%2:%3:%4）")
        .arg(seconds)
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(remaining, 2, 10, QLatin1Char('0'));
}

QString alarmText(bool alarm)
{
    return alarm ? QStringLiteral("高温告警") : QStringLiteral("正常");
}

QString communicationErrorName(communication::ErrorCode code)
{
    switch (code) {
    case communication::ErrorCode::ResponseTimeout:
        return QStringLiteral("响应超时");
    case communication::ErrorCode::OpenFailed:
        return QStringLiteral("串口打开失败");
    case communication::ErrorCode::PermissionError:
        return QStringLiteral("串口权限错误");
    case communication::ErrorCode::ResourceError:
        return QStringLiteral("串口资源错误");
    case communication::ErrorCode::ResponseCrcMismatch:
        return QStringLiteral("响应 CRC 错误");
    case communication::ErrorCode::ModbusIllegalDataAddress:
        return QStringLiteral("Modbus 非法地址");
    case communication::ErrorCode::ModbusIllegalDataValue:
        return QStringLiteral("Modbus 非法数据值");
    case communication::ErrorCode::CancelledByCaller:
    case communication::ErrorCode::CancelledByClose:
    case communication::ErrorCode::CancelledByHandoff:
        return QStringLiteral("请求已取消");
    default:
        return QStringLiteral("通信错误 %1").arg(static_cast<int>(code));
    }
}

} // namespace

MonitoringViewModel::MonitoringViewModel(app::AppStateController &controller,
                                         monitor::MonitorService &monitorService,
                                         ISerialPortCatalog &serialPorts,
                                         QObject *parent)
    : QObject(parent)
    , controller_(controller)
    , monitorService_(monitorService)
    , serialPorts_(serialPorts)
{
    connect(&controller_, &app::AppStateController::stateChanged,
            this, [this] { publish(); });
    connect(&controller_, &app::AppStateController::commandCompleted,
            this, [this](const app::AppCommandResult &result) {
        if (result.succeeded) {
            lastError_.clear();
        } else if (result.error) {
            lastError_ = appErrorText(*result.error);
        }
        publish();
    });
    connect(&controller_, &app::AppStateController::errorOccurred,
            this, &MonitoringViewModel::handleAppError);
    connect(&monitorService_, &monitor::MonitorService::snapshotPublished,
            this, [this](const monitor::MonitoringSnapshot &snapshot) {
        lastSnapshot_ = snapshot;
        lastError_.clear();
        publish();
    });
    connect(&monitorService_, &monitor::MonitorService::batchCompleted,
            this, [this](const monitor::PollBatchResult &batch) {
        lastBatch_ = batch;
        publish();
    });
    connect(&monitorService_, &monitor::MonitorService::healthChanged,
            this, [this] { publish(); });
    connect(&monitorService_, &monitor::MonitorService::statisticsChanged,
            this, [this] { publish(); });
    connect(&monitorService_, &monitor::MonitorService::errorOccurred,
            this, &MonitoringViewModel::handleMonitorError);
    refreshPorts();
}

const MonitoringUiState &MonitoringViewModel::state() const noexcept
{
    return state_;
}

void MonitoringViewModel::refreshPorts()
{
    if (!state_.refreshPortsEnabled && !state_.ports.isEmpty()) {
        return;
    }
    const QString previous = state_.selectedPortName;
    state_.ports = serialPorts_.availablePorts();
    state_.selectedPortName.clear();
    const auto selected = std::find_if(state_.ports.cbegin(), state_.ports.cend(),
                                       [&previous](const auto &entry) {
        return entry.portName.compare(previous, Qt::CaseInsensitive) == 0;
    });
    if (selected != state_.ports.cend()) {
        state_.selectedPortName = selected->portName;
    } else if (state_.ports.size() == 1) {
        state_.selectedPortName = state_.ports.front().portName;
    }
    publish();
}

void MonitoringViewModel::setSelectedPortName(const QString &portName)
{
    if (!state_.connectionFieldsEnabled) {
        return;
    }
    const auto found = std::find_if(state_.ports.cbegin(), state_.ports.cend(),
                                    [&portName](const auto &entry) {
        return entry.portName.compare(portName, Qt::CaseInsensitive) == 0;
    });
    if (found != state_.ports.cend()) {
        state_.selectedPortName = found->portName;
        publish();
    }
}

void MonitoringViewModel::setSlaveAddress(int address)
{
    if (state_.connectionFieldsEnabled && address >= 1 && address <= 247) {
        state_.slaveAddress = address;
        publish();
    }
}

void MonitoringViewModel::setResponseTimeoutMs(int timeoutMs)
{
    if (state_.connectionFieldsEnabled && timeoutMs >= 1 && timeoutMs <= 60000) {
        state_.responseTimeoutMs = timeoutMs;
        publish();
    }
}

void MonitoringViewModel::setTargetPeriodMs(int periodMs)
{
    static constexpr int allowed[] = {100, 500, 1000, 2000};
    if (state_.targetPeriodEnabled
        && std::find(std::begin(allowed), std::end(allowed), periodMs)
            != std::end(allowed)) {
        state_.targetPeriodMs = periodMs;
        publish();
    }
}

void MonitoringViewModel::connectDevice()
{
    if (!state_.connectEnabled) {
        return;
    }
    communication::ModbusConnectionConfig config;
    config.serial.portName = state_.selectedPortName;
    config.serial.baudRate = 115200;
    config.serial.dataBits = 8;
    config.serial.parity = communication::SerialParity::None;
    config.serial.stopBits = communication::SerialStopBits::One;
    config.serial.flowControl = communication::SerialFlowControl::None;
    config.serverAddress = static_cast<quint8>(state_.slaveAddress);
    config.defaultResponseTimeout = std::chrono::milliseconds(state_.responseTimeoutMs);
    config.maxPendingRequests = 64;
    acceptSubmission(controller_.connectDevice(config));
}

void MonitoringViewModel::disconnectDevice()
{
    if (state_.disconnectEnabled) {
        acceptSubmission(controller_.disconnectDevice());
    }
}

void MonitoringViewModel::startMonitoring()
{
    if (!state_.startMonitoringEnabled) {
        return;
    }
    monitor::MonitorConfig config;
    config.targetPeriod = std::chrono::milliseconds(state_.targetPeriodMs);
    config.requestTimeout = std::chrono::milliseconds(state_.responseTimeoutMs);
    acceptSubmission(controller_.startMonitoring(config));
}

void MonitoringViewModel::stopMonitoring()
{
    if (state_.stopMonitoringEnabled) {
        acceptSubmission(controller_.stopMonitoring());
    }
}

void MonitoringViewModel::recover()
{
    if (state_.recoverEnabled) {
        acceptSubmission(controller_.recover());
    }
}

void MonitoringViewModel::acceptSubmission(const app::AppCommandSubmission &submission)
{
    if (submission.rejection) {
        lastError_ = appErrorText(*submission.rejection);
    }
    publish();
}

void MonitoringViewModel::handleAppError(const app::AppError &error)
{
    lastError_ = appErrorText(error);
    publish();
}

void MonitoringViewModel::handleMonitorError(const monitor::MonitorError &error)
{
    if (error.code != monitor::MonitorErrorCode::Stopped) {
        lastError_ = monitorErrorText(error);
        publish();
    }
}

QString MonitoringViewModel::appErrorText(const app::AppError &error) const
{
    QString text = QStringLiteral("应用错误 %1").arg(static_cast<int>(error.code));
    if (error.communicationError) {
        text = communicationErrorName(error.communicationError->code);
    } else if (error.monitorError) {
        text = monitorErrorText(*error.monitorError);
    }
    if (!error.diagnostic.trimmed().isEmpty()) {
        text += QStringLiteral("：%1").arg(error.diagnostic.trimmed());
    } else if (error.communicationError
               && !error.communicationError->diagnostic.trimmed().isEmpty()) {
        text += QStringLiteral("：%1").arg(
            error.communicationError->diagnostic.trimmed());
    }
    return text;
}

QString MonitoringViewModel::monitorErrorText(const monitor::MonitorError &error) const
{
    QString text;
    if (error.communicationError) {
        text = communicationErrorName(error.communicationError->code);
    } else {
        switch (error.code) {
        case monitor::MonitorErrorCode::RequestRejected:
            text = QStringLiteral("监控请求被拒绝");
            break;
        case monitor::MonitorErrorCode::RequestFailed:
            text = QStringLiteral("监控请求失败");
            break;
        case monitor::MonitorErrorCode::ResultMismatch:
            text = QStringLiteral("监控结果不匹配");
            break;
        case monitor::MonitorErrorCode::DecodeFailed:
            text = QStringLiteral("完整快照解码失败");
            break;
        case monitor::MonitorErrorCode::BackendUnavailable:
            text = QStringLiteral("监控后端不可用");
            break;
        default:
            text = QStringLiteral("监控错误 %1").arg(static_cast<int>(error.code));
            break;
        }
    }
    if (error.batchId) {
        text += QStringLiteral("（批次 %1）").arg(error.batchId->value);
    }
    if (!error.diagnostic.trimmed().isEmpty()) {
        text += QStringLiteral("：%1").arg(error.diagnostic.trimmed());
    }
    return text;
}

void MonitoringViewModel::rebuildState()
{
    state_.appState = controller_.state();
    state_.health = monitorService_.health();
    state_.statistics = monitorService_.statistics();
    const bool busy = controller_.commandInProgress();
    const bool disconnected = state_.appState == app::AppState::Disconnected;
    const bool idle = state_.appState == app::AppState::ConnectedIdle;
    const bool monitoring = state_.appState == app::AppState::Monitoring;
    state_.connectionFieldsEnabled = disconnected && !busy;
    state_.refreshPortsEnabled = state_.connectionFieldsEnabled;
    state_.targetPeriodEnabled = (disconnected || idle) && !busy;
    state_.connectEnabled = disconnected && !busy
        && !state_.selectedPortName.trimmed().isEmpty();
    state_.disconnectEnabled = (idle || monitoring) && !busy;
    state_.startMonitoringEnabled = idle && !busy;
    state_.stopMonitoringEnabled = monitoring && !busy;
    state_.recoverEnabled = state_.appState == app::AppState::Error && !busy;

    state_.appStateText = appStateText(state_.appState);
    state_.connectionStateText = connectionStateText(controller_.connectionState());
    state_.deviceHealthText = healthText(state_.health);
    state_.lastErrorText = lastError_.isEmpty() ? QStringLiteral("无") : lastError_;

    const QString placeholder = QStringLiteral("--");
    state_.phaseATemperatureText = placeholder;
    state_.phaseBTemperatureText = placeholder;
    state_.phaseCTemperatureText = placeholder;
    state_.ambientTemperatureText = QStringLiteral("--（模拟源）");
    state_.lightMillivoltsText = placeholder;
    state_.phaseAAlarmText = placeholder;
    state_.phaseBAlarmText = placeholder;
    state_.phaseCAlarmText = placeholder;
    state_.ambientAlarmText = placeholder;
    state_.deviceStatusText = QStringLiteral("无有效状态字");
    state_.firmwareVersionText = placeholder;
    state_.uptimeText = placeholder;
    state_.lastSuccessfulText = QStringLiteral("无");

    if (!lastSnapshot_) {
        state_.dataFreshnessText = QStringLiteral("无有效快照");
    } else {
        const auto &snapshot = lastSnapshot_->snapshot;
        state_.phaseATemperatureText = formatTemperature(
            snapshot.measurements.phaseATemperature);
        state_.phaseBTemperatureText = formatTemperature(
            snapshot.measurements.phaseBTemperature);
        state_.phaseCTemperatureText = formatTemperature(
            snapshot.measurements.phaseCTemperature);
        state_.ambientTemperatureText = QStringLiteral("%1（模拟源%2）")
            .arg(formatTemperature(snapshot.measurements.ambientTemperature),
                 snapshot.status.device.ambientSimulated()
                     ? QStringLiteral("，状态位已确认")
                     : QStringLiteral("，状态位未置位"));
        state_.lightMillivoltsText = QStringLiteral("%1 mV")
            .arg(snapshot.measurements.lightMillivolts);
        state_.phaseAAlarmText = alarmText(snapshot.status.alarms.phaseATemperatureHigh());
        state_.phaseBAlarmText = alarmText(snapshot.status.alarms.phaseBTemperatureHigh());
        state_.phaseCAlarmText = alarmText(snapshot.status.alarms.phaseCTemperatureHigh());
        state_.ambientAlarmText = alarmText(snapshot.status.alarms.ambientTemperatureHigh());
        QStringList deviceParts;
        deviceParts << (snapshot.status.device.running()
                            ? QStringLiteral("设备运行")
                            : QStringLiteral("设备未运行"));
        deviceParts << (snapshot.status.device.phaseASensorFault()
                            ? QStringLiteral("A相传感器故障")
                            : QStringLiteral("A相传感器正常"));
        deviceParts << (snapshot.status.device.phaseBSensorFault()
                            ? QStringLiteral("B相传感器故障")
                            : QStringLiteral("B相传感器正常"));
        deviceParts << (snapshot.status.device.phaseCSensorFault()
                            ? QStringLiteral("C相传感器故障")
                            : QStringLiteral("C相传感器正常"));
        deviceParts << (snapshot.status.device.lightAdcFault()
                            ? QStringLiteral("光敏 ADC 故障")
                            : QStringLiteral("光敏 ADC 正常"));
        deviceParts << (snapshot.status.device.ambientSimulated()
                            ? QStringLiteral("环境温度为模拟源")
                            : QStringLiteral("环境模拟源状态位未置位"));
        if (snapshot.status.device.reservedBits() != 0) {
            deviceParts << QStringLiteral("存在未知状态位");
        }
        state_.deviceStatusText = QStringLiteral("%1；状态字 0x%2")
            .arg(deviceParts.join(QStringLiteral("；")),
                 QString::number(snapshot.status.device.raw, 16)
                     .rightJustified(4, QLatin1Char('0')).toUpper());
        state_.firmwareVersionText = QStringLiteral("%1.%2")
            .arg(snapshot.firmwareVersion.major)
            .arg(snapshot.firmwareVersion.minor);
        state_.uptimeText = formatUptime(snapshot.diagnostics.uptimeSeconds);
        state_.lastSuccessfulText = lastSnapshot_->lastSuccessfulUtc.toLocalTime()
            .toString(Qt::ISODateWithMs);

        if (monitoring && state_.health == monitor::DeviceHealth::Online) {
            state_.dataFreshnessText = QStringLiteral("实时（完整快照）");
        } else if (state_.health == monitor::DeviceHealth::Degraded) {
            state_.dataFreshnessText = QStringLiteral("陈旧（通信退化，保留最后成功快照）");
        } else if (state_.health == monitor::DeviceHealth::Offline) {
            state_.dataFreshnessText = QStringLiteral("陈旧（设备离线，保留最后成功快照）");
        } else if (monitoring) {
            state_.dataFreshnessText = QStringLiteral("陈旧（等待新的完整快照）");
        } else {
            state_.dataFreshnessText = QStringLiteral("陈旧（当前未监控）");
        }
    }

    const auto &stats = state_.statistics;
    state_.requestsText = QString::number(stats.requests);
    state_.succeededText = QString::number(stats.succeeded);
    state_.failedText = QString::number(stats.failed);
    state_.timedOutText = QString::number(stats.timedOut);
    state_.successRateText = stats.requests == 0
        ? placeholder
        : QStringLiteral("%1 %").arg(
              static_cast<double>(stats.succeeded) * 100.0
                  / static_cast<double>(stats.requests),
              0, 'f', 2);
    if (stats.rttSamples == 0 || !stats.recentRtt
        || !stats.minimumRtt || !stats.maximumRtt) {
        state_.rttText = placeholder;
    } else {
        const auto average = std::chrono::nanoseconds(
            stats.totalRttNanoseconds / stats.rttSamples);
        state_.rttText = QStringLiteral("最近 %1 / 最小 %2 / 平均 %3 / 最大 %4")
            .arg(formatMilliseconds(*stats.recentRtt, 3),
                 formatMilliseconds(*stats.minimumRtt, 3),
                 formatMilliseconds(average, 3),
                 formatMilliseconds(*stats.maximumRtt, 3));
    }
    if (lastBatch_ && lastBatch_->effectiveInterval) {
        state_.effectivePeriodText = formatMilliseconds(
            *lastBatch_->effectiveInterval, 1);
    } else {
        state_.effectivePeriodText = placeholder;
    }
    if (!lastBatch_) {
        state_.overrunText = QStringLiteral("尚无批次");
    } else {
        state_.overrunText = QStringLiteral("%1（累计 %2）")
            .arg(lastBatch_->overrun ? QStringLiteral("是") : QStringLiteral("否"))
            .arg(stats.overruns);
    }
}

void MonitoringViewModel::publish()
{
    rebuildState();
    emit stateChanged();
}

} // namespace oms555tv::ui

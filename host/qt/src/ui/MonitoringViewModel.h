#pragma once

#include "app/AppStateController.h"
#include "monitor/MonitorService.h"
#include "ui/SerialPortCatalog.h"

#include <QObject>

#include <optional>

namespace oms555tv::ui {

struct MonitoringUiState {
    QVector<SerialPortEntry> ports;
    QString selectedPortName;
    int slaveAddress = 1;
    int responseTimeoutMs = 500;
    int targetPeriodMs = 1000;

    app::AppState appState = app::AppState::Disconnected;
    monitor::DeviceHealth health = monitor::DeviceHealth::Unknown;
    monitor::CommunicationStatistics statistics;

    bool connectionFieldsEnabled = true;
    bool refreshPortsEnabled = true;
    bool targetPeriodEnabled = true;
    bool connectEnabled = false;
    bool disconnectEnabled = false;
    bool startMonitoringEnabled = false;
    bool stopMonitoringEnabled = false;
    bool recoverEnabled = false;

    QString appStateText;
    QString connectionStateText;
    QString deviceHealthText;
    QString dataFreshnessText;
    QString lastSuccessfulText;
    QString lastErrorText;

    QString phaseATemperatureText;
    QString phaseBTemperatureText;
    QString phaseCTemperatureText;
    QString ambientTemperatureText;
    QString lightMillivoltsText;
    QString phaseAAlarmText;
    QString phaseBAlarmText;
    QString phaseCAlarmText;
    QString ambientAlarmText;
    QString deviceStatusText;
    QString firmwareVersionText;
    QString uptimeText;

    QString requestsText;
    QString succeededText;
    QString failedText;
    QString timedOutText;
    QString successRateText;
    QString rttText;
    QString effectivePeriodText;
    QString overrunText;
};

class MonitoringViewModel final : public QObject
{
    Q_OBJECT

public:
    MonitoringViewModel(app::AppStateController &controller,
                        monitor::MonitorService &monitorService,
                        ISerialPortCatalog &serialPorts,
                        QObject *parent = nullptr);

    [[nodiscard]] const MonitoringUiState &state() const noexcept;

    void refreshPorts();
    void setSelectedPortName(const QString &portName);
    void setSlaveAddress(int address);
    void setResponseTimeoutMs(int timeoutMs);
    void setTargetPeriodMs(int periodMs);

    void connectDevice();
    void disconnectDevice();
    void startMonitoring();
    void stopMonitoring();
    void recover();

signals:
    void stateChanged();

private:
    void rebuildState();
    void publish();
    void acceptSubmission(const app::AppCommandSubmission &submission);
    void handleAppError(const app::AppError &error);
    void handleMonitorError(const monitor::MonitorError &error);
    [[nodiscard]] QString appErrorText(const app::AppError &error) const;
    [[nodiscard]] QString monitorErrorText(const monitor::MonitorError &error) const;

    app::AppStateController &controller_;
    monitor::MonitorService &monitorService_;
    ISerialPortCatalog &serialPorts_;
    MonitoringUiState state_;
    std::optional<monitor::MonitoringSnapshot> lastSnapshot_;
    std::optional<monitor::PollBatchResult> lastBatch_;
    QString lastError_;
};

} // namespace oms555tv::ui

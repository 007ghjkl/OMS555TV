#pragma once

#include "app/AppStateTypes.h"
#include "communication/IModbusClient.h"
#include "monitor/MonitorService.h"

#include <QObject>

#include <optional>

namespace oms555tv::app {

class AppStateController final : public QObject
{
    Q_OBJECT

public:
    AppStateController(communication::IModbusClient &client,
                       monitor::MonitorService &monitorService,
                       QObject *parent = nullptr);

    [[nodiscard]] AppState state() const noexcept;
    [[nodiscard]] bool commandInProgress() const noexcept;
    [[nodiscard]] communication::ConnectionState connectionState() const noexcept;

    AppCommandSubmission connectDevice(const communication::ModbusConnectionConfig &config);
    AppCommandSubmission disconnectDevice();
    AppCommandSubmission startMonitoring(const monitor::MonitorConfig &config);
    AppCommandSubmission stopMonitoring();
    AppCommandSubmission startTesting();
    AppCommandSubmission stopTesting();
    AppCommandSubmission recover();

signals:
    void stateChanged(oms555tv::app::AppState state);
    void commandCompleted(const oms555tv::app::AppCommandResult &result);
    void errorOccurred(const oms555tv::app::AppError &error);

private:
    enum class Stage {
        WaitingOpen,
        WaitingClose,
        WaitingAcquireMonitor,
        WaitingMonitorStop,
        WaitingReleaseMonitor,
        WaitingAcquireTesting,
        WaitingReleaseTesting,
        WaitingRollbackMonitorRelease,
    };

    struct PendingCommand {
        AppOperationId id;
        AppCommand command = AppCommand::Connect;
        Stage stage = Stage::WaitingOpen;
        std::optional<communication::OperationId> communicationOperationId;
        std::optional<monitor::MonitorConfig> monitorConfig;
        std::optional<AppError> deferredError;
    };

    [[nodiscard]] AppCommandSubmission reject(AppCommand command,
                                              AppErrorCode code,
                                              QString diagnostic = {}) const;
    [[nodiscard]] std::optional<AppOperationId> allocateOperationId() noexcept;
    void setState(AppState state);
    void handleControlCompleted(const communication::ControlResult &result);
    void handleConnectionStateChanged(communication::ConnectionState state);
    void handleMonitorStopped();
    void handleMonitorError(const monitor::MonitorError &error);
    void releaseMonitorOwner();
    void closeBackend();
    void completePendingSuccess(AppState finalState);
    void failPending(AppError error, bool enterErrorState = true);
    void enterUnexpectedError(AppError error);
    [[nodiscard]] AppError communicationFailure(
        AppErrorCode code,
        const communication::ControlResult &result,
        QString diagnostic = {}) const;

    communication::IModbusClient &client_;
    monitor::MonitorService &monitorService_;
    AppState state_ = AppState::Disconnected;
    quint64 nextOperationId_ = 1;
    std::optional<PendingCommand> pending_;
};

} // namespace oms555tv::app

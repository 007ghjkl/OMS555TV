#pragma once

#include "app/AppStateController.h"
#include "communication/IModbusClient.h"
#include "configuration/ConfigurationTypes.h"

#include <QObject>

#include <array>
#include <optional>

namespace oms555tv::configuration {

class ConfigurationService final : public QObject
{
    Q_OBJECT

public:
    ConfigurationService(app::AppStateController &controller,
                         communication::IModbusClient &client,
                         QObject *parent = nullptr);

    [[nodiscard]] ConfigurationState state() const noexcept;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] const std::optional<device::AlarmThresholds> &thresholds() const noexcept;
    [[nodiscard]] const std::optional<ConfigurationOperationResult> &lastResult() const noexcept;

    ConfigurationSubmission readThresholds();
    ConfigurationSubmission writeThresholds(const ThresholdValues &values);
    [[nodiscard]] bool cancel();

signals:
    void stateChanged(oms555tv::configuration::ConfigurationState state);
    void thresholdsChanged(const oms555tv::device::AlarmThresholds &thresholds);
    void operationCompleted(
        const oms555tv::configuration::ConfigurationOperationResult &result);
    void errorOccurred(const oms555tv::configuration::ConfigurationError &error);

private:
    struct PendingOperation {
        ConfigurationOperationResult result;
        std::optional<communication::OperationId> controlOperationId;
        std::optional<communication::RequestId> requestId;
        std::array<std::optional<device::RegisterWrite>, 4> writes{};
        QVector<quint16> beforeRaw;
        int itemIndex = 0;
        bool cancelRequested = false;
    };

    [[nodiscard]] ConfigurationSubmission begin(ConfigurationCommand command,
                                                const ThresholdValues *values);
    [[nodiscard]] std::optional<ConfigurationError> validateReady() const;
    [[nodiscard]] std::optional<ConfigurationOperationId> allocateId() noexcept;
    void setState(ConfigurationState state);
    void handleControlCompleted(const communication::ControlResult &result);
    void handleRequestCompleted(const communication::ModbusRequestResult &result);
    void submitInitialRead();
    void submitWrite();
    void submitReadback();
    void advanceItem();
    void beginRelease();
    void completeAfterRelease();
    void completeWithError(ConfigurationError error);
    void markRemainingCancelled();
    [[nodiscard]] ConfigurationError requestFailure(
        ConfigurationErrorCode code,
        const communication::ModbusRequestResult &result,
        std::optional<device::TemperatureChannel> channel = std::nullopt) const;
    [[nodiscard]] static device::Temperature thresholdForChannel(
        const device::AlarmThresholds &thresholds,
        device::TemperatureChannel channel);
    static void setThresholdForChannel(device::AlarmThresholds &thresholds,
                                       device::TemperatureChannel channel,
                                       device::Temperature value);

    app::AppStateController &controller_;
    communication::IModbusClient &client_;
    ConfigurationState state_ = ConfigurationState::Idle;
    quint64 nextOperationId_ = 1;
    std::optional<PendingOperation> pending_;
    std::optional<device::AlarmThresholds> thresholds_;
    std::optional<ConfigurationOperationResult> lastResult_;
};

} // namespace oms555tv::configuration

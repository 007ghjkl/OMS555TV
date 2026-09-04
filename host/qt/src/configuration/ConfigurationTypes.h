#pragma once

#include "communication/CommunicationTypes.h"
#include "device/DeviceTypes.h"

#include <QMetaType>
#include <QVector>

#include <array>
#include <optional>

namespace oms555tv::configuration {

struct ConfigurationOperationId {
    quint64 value = 0;
};

[[nodiscard]] constexpr bool operator==(ConfigurationOperationId left,
                                        ConfigurationOperationId right) noexcept
{
    return left.value == right.value;
}

enum class ConfigurationState {
    Idle,
    AcquiringOwner,
    ReadingCurrent,
    Writing,
    Verifying,
    Cancelling,
    ReleasingOwner,
};

enum class ConfigurationCommand { ReadThresholds, WriteThresholds };

enum class ConfigurationItemStatus {
    Pending,
    Succeeded,
    WriteFailed,
    ReadbackFailed,
    ReadbackMismatch,
    Cancelled,
};

enum class ConfigurationErrorCode {
    InvalidState,
    Busy,
    InvalidInput,
    IdExhausted,
    OwnerRejected,
    InitialReadRejected,
    InitialReadFailed,
    WriteRejected,
    WriteFailed,
    ReadbackRejected,
    ReadbackFailed,
    ReadbackMismatch,
    Cancelled,
    ReleaseFailed,
    ResultMismatch,
    InvariantViolation,
};

struct ConfigurationError {
    ConfigurationErrorCode code = ConfigurationErrorCode::InvariantViolation;
    std::optional<device::TemperatureChannel> channel;
    std::optional<device::Temperature> expected;
    std::optional<device::Temperature> actual;
    std::optional<communication::RequestId> writeRequestId;
    std::optional<communication::RequestId> readbackRequestId;
    std::optional<communication::CommunicationError> communicationError;
    std::optional<device::CodecError> codecError;
    QString diagnostic;
};

struct ConfigurationItemResult {
    device::TemperatureChannel channel = device::TemperatureChannel::PhaseA;
    ConfigurationItemStatus status = ConfigurationItemStatus::Pending;
    device::Temperature before;
    device::Temperature expected;
    std::optional<device::Temperature> actual;
    std::optional<communication::RequestId> writeRequestId;
    std::optional<communication::RequestId> readbackRequestId;
    std::optional<ConfigurationError> error;
};

struct ConfigurationOperationResult {
    ConfigurationOperationId operationId;
    ConfigurationCommand command = ConfigurationCommand::ReadThresholds;
    bool succeeded = false;
    bool cancelled = false;
    std::optional<device::AlarmThresholds> before;
    std::optional<device::AlarmThresholds> verifiedThresholds;
    QVector<ConfigurationItemResult> items;
    std::optional<ConfigurationError> error;
};

struct ConfigurationSubmission {
    std::optional<ConfigurationOperationId> operationId;
    std::optional<ConfigurationError> rejection;

    [[nodiscard]] bool accepted() const noexcept { return operationId.has_value(); }
};

using ThresholdValues = std::array<double, 4>;

} // namespace oms555tv::configuration

Q_DECLARE_METATYPE(oms555tv::configuration::ConfigurationState)
Q_DECLARE_METATYPE(oms555tv::configuration::ConfigurationOperationResult)

#pragma once

#include "device/RegisterMap.h"

#include <QVector>

#include <optional>
#include <variant>

namespace oms555tv::device {

enum class TemperatureChannel {
    PhaseA,
    PhaseB,
    PhaseC,
    Ambient,
};

struct Temperature {
    qint16 deciCelsius = 0;

    [[nodiscard]] double celsius() const noexcept
    {
        return static_cast<double>(deciCelsius) / 10.0;
    }
};

struct Measurements {
    Temperature phaseATemperature;
    Temperature phaseBTemperature;
    Temperature phaseCTemperature;
    Temperature ambientTemperature;
    quint16 lightMillivolts = 0;
};

struct AlarmThresholds {
    Temperature phaseA;
    Temperature phaseB;
    Temperature phaseC;
    Temperature ambient;
};

struct AlarmStatusWord {
    static constexpr quint16 phaseATemperatureHighMask = 1U << 0U;
    static constexpr quint16 phaseBTemperatureHighMask = 1U << 1U;
    static constexpr quint16 phaseCTemperatureHighMask = 1U << 2U;
    static constexpr quint16 ambientTemperatureHighMask = 1U << 3U;
    static constexpr quint16 knownMask = phaseATemperatureHighMask
        | phaseBTemperatureHighMask | phaseCTemperatureHighMask
        | ambientTemperatureHighMask;

    quint16 raw = 0;

    [[nodiscard]] bool phaseATemperatureHigh() const noexcept
    {
        return (raw & phaseATemperatureHighMask) != 0;
    }

    [[nodiscard]] bool phaseBTemperatureHigh() const noexcept
    {
        return (raw & phaseBTemperatureHighMask) != 0;
    }

    [[nodiscard]] bool phaseCTemperatureHigh() const noexcept
    {
        return (raw & phaseCTemperatureHighMask) != 0;
    }

    [[nodiscard]] bool ambientTemperatureHigh() const noexcept
    {
        return (raw & ambientTemperatureHighMask) != 0;
    }

    [[nodiscard]] quint16 reservedBits() const noexcept
    {
        return static_cast<quint16>(raw & static_cast<quint16>(~knownMask));
    }
};

struct DeviceStatusWord {
    static constexpr quint16 runningMask = 1U << 0U;
    static constexpr quint16 phaseASensorFaultMask = 1U << 1U;
    static constexpr quint16 phaseBSensorFaultMask = 1U << 2U;
    static constexpr quint16 phaseCSensorFaultMask = 1U << 3U;
    static constexpr quint16 lightAdcFaultMask = 1U << 4U;
    static constexpr quint16 ambientSimulatedMask = 1U << 5U;
    static constexpr quint16 knownMask = runningMask | phaseASensorFaultMask
        | phaseBSensorFaultMask | phaseCSensorFaultMask | lightAdcFaultMask
        | ambientSimulatedMask;

    quint16 raw = 0;

    [[nodiscard]] bool running() const noexcept
    {
        return (raw & runningMask) != 0;
    }

    [[nodiscard]] bool phaseASensorFault() const noexcept
    {
        return (raw & phaseASensorFaultMask) != 0;
    }

    [[nodiscard]] bool phaseBSensorFault() const noexcept
    {
        return (raw & phaseBSensorFaultMask) != 0;
    }

    [[nodiscard]] bool phaseCSensorFault() const noexcept
    {
        return (raw & phaseCSensorFaultMask) != 0;
    }

    [[nodiscard]] bool lightAdcFault() const noexcept
    {
        return (raw & lightAdcFaultMask) != 0;
    }

    [[nodiscard]] bool ambientSimulated() const noexcept
    {
        return (raw & ambientSimulatedMask) != 0;
    }

    [[nodiscard]] quint16 reservedBits() const noexcept
    {
        return static_cast<quint16>(raw & static_cast<quint16>(~knownMask));
    }
};

struct Status {
    AlarmStatusWord alarms;
    DeviceStatusWord device;
};

struct Diagnostics {
    quint16 communicationErrorCount = 0;
    quint32 uptimeSeconds = 0;
};

struct FirmwareVersion {
    quint16 major = 0;
    quint16 minor = 0;
};

struct DeviceSnapshot {
    Measurements measurements;
    AlarmThresholds thresholds;
    Status status;
    Diagnostics diagnostics;
    FirmwareVersion firmwareVersion;
};

struct RegisterBlock {
    PduAddress startAddress;
    QVector<quint16> values;
};

struct RegisterWrite {
    PduAddress address;
    quint16 rawValue = 0;
    Temperature value;
};

enum class DeviceField {
    None,
    PhaseATemperature,
    PhaseBTemperature,
    PhaseCTemperature,
    AmbientTemperature,
    LightMillivolts,
    PhaseAThreshold,
    PhaseBThreshold,
    PhaseCThreshold,
    AmbientThreshold,
};

enum class CodecErrorCode {
    AddressMismatch,
    RegisterCountMismatch,
    MissingReadBlock,
    DuplicateReadBlock,
    UnknownReadBlock,
    ValueOutOfRange,
    InvalidPrecision,
    NonFiniteValue,
    InvalidChannel,
};

struct CodecError {
    CodecErrorCode code;
    DeviceField field = DeviceField::None;
    std::optional<PduAddress> actualAddress;
    std::optional<PduAddress> expectedAddress;
    std::optional<qsizetype> actualRegisterCount;
    std::optional<qsizetype> expectedRegisterCount;
    std::optional<double> actualValue;
    std::optional<double> minimumValue;
    std::optional<double> maximumValue;
};

template<typename T>
using CodecResult = std::variant<T, CodecError>;

} // namespace oms555tv::device

#pragma once

#include <QtGlobal>

#include <array>

namespace oms555tv::device {

// 表示 Modbus PDU 中的 0-based 地址，不表示 40001 风格文档编号。
class PduAddress final
{
public:
    explicit constexpr PduAddress(const quint16 value) noexcept
        : value_(value)
    {
    }

    [[nodiscard]] constexpr quint16 value() const noexcept
    {
        return value_;
    }

private:
    quint16 value_;
};

[[nodiscard]] constexpr bool operator==(const PduAddress left, const PduAddress right) noexcept
{
    return left.value() == right.value();
}

[[nodiscard]] constexpr bool operator!=(const PduAddress left, const PduAddress right) noexcept
{
    return !(left == right);
}

[[nodiscard]] constexpr bool operator<(const PduAddress left, const PduAddress right) noexcept
{
    return left.value() < right.value();
}

enum class HoldingRegister : quint16 {
    PhaseATemperature = 0,
    PhaseBTemperature = 1,
    PhaseCTemperature = 2,
    AmbientTemperature = 3,
    LightMillivolts = 4,
    PhaseAThreshold = 9,
    PhaseBThreshold = 10,
    PhaseCThreshold = 11,
    AmbientThreshold = 12,
    AlarmStatus = 19,
    DeviceStatus = 20,
    CommunicationErrorCount = 29,
    UptimeLow = 30,
    UptimeHigh = 31,
    FirmwareMajor = 39,
    FirmwareMinor = 40,
};

[[nodiscard]] constexpr PduAddress pduAddress(const HoldingRegister reg) noexcept
{
    return PduAddress(static_cast<quint16>(reg));
}

[[nodiscard]] constexpr quint32 documentationNumber(const PduAddress address) noexcept
{
    return 40001U + address.value();
}

struct ReadBlockDefinition {
    PduAddress startAddress;
    quint16 count;

    [[nodiscard]] constexpr PduAddress endAddress() const noexcept
    {
        return PduAddress(static_cast<quint16>(startAddress.value() + count - 1U));
    }
};

// 读块避开寄存器表中的保留地址，可直接交给后续通信层生成 0x03 请求。
inline constexpr ReadBlockDefinition measurementsReadBlock{PduAddress(0), 5};
inline constexpr ReadBlockDefinition thresholdsReadBlock{PduAddress(9), 4};
inline constexpr ReadBlockDefinition statusReadBlock{PduAddress(19), 2};
inline constexpr ReadBlockDefinition diagnosticsReadBlock{PduAddress(29), 3};
inline constexpr ReadBlockDefinition versionReadBlock{PduAddress(39), 2};

inline constexpr std::array<ReadBlockDefinition, 5> deviceSnapshotReadBlocks{
    measurementsReadBlock,
    thresholdsReadBlock,
    statusReadBlock,
    diagnosticsReadBlock,
    versionReadBlock,
};

} // namespace oms555tv::device

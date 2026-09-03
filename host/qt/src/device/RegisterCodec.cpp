#include "device/RegisterCodec.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace oms555tv::device {
namespace {

constexpr qint32 minimumTemperature = -400;
constexpr qint32 maximumTemperature = 800;
constexpr quint16 maximumLightMillivolts = 3300;
constexpr double precisionTolerance = 1e-9;

std::optional<CodecError> validateBlock(const RegisterBlock &block,
                                        const ReadBlockDefinition definition)
{
    if (block.startAddress != definition.startAddress) {
        return CodecError{
            CodecErrorCode::AddressMismatch,
            DeviceField::None,
            block.startAddress,
            definition.startAddress,
        };
    }

    if (block.values.size() != definition.count) {
        CodecError error{CodecErrorCode::RegisterCountMismatch};
        error.actualAddress = block.startAddress;
        error.expectedAddress = definition.startAddress;
        error.actualRegisterCount = block.values.size();
        error.expectedRegisterCount = definition.count;
        return error;
    }

    return std::nullopt;
}

CodecResult<Temperature> decodeTemperature(const quint16 raw, const DeviceField field)
{
    const qint32 signedValue = raw <= 0x7FFFU
        ? static_cast<qint32>(raw)
        : static_cast<qint32>(raw) - 0x10000;

    if (signedValue < minimumTemperature || signedValue > maximumTemperature) {
        CodecError error{CodecErrorCode::ValueOutOfRange};
        error.field = field;
        error.actualValue = signedValue;
        error.minimumValue = minimumTemperature;
        error.maximumValue = maximumTemperature;
        return error;
    }

    return Temperature{static_cast<qint16>(signedValue)};
}

template<typename T>
const CodecError *errorFrom(const CodecResult<T> &result)
{
    return std::get_if<CodecError>(&result);
}

std::optional<std::size_t> readBlockIndex(const PduAddress address)
{
    for (std::size_t index = 0; index < deviceSnapshotReadBlocks.size(); ++index) {
        if (deviceSnapshotReadBlocks[index].startAddress == address) {
            return index;
        }
    }
    return std::nullopt;
}

CodecError blockIdentityError(const CodecErrorCode code, const PduAddress address)
{
    CodecError error{code};
    if (code != CodecErrorCode::MissingReadBlock) {
        error.actualAddress = address;
    }
    if (code == CodecErrorCode::MissingReadBlock
        || code == CodecErrorCode::DuplicateReadBlock) {
        error.expectedAddress = address;
    }
    return error;
}

std::optional<PduAddress> thresholdAddress(const TemperatureChannel channel)
{
    switch (channel) {
    case TemperatureChannel::PhaseA:
        return pduAddress(HoldingRegister::PhaseAThreshold);
    case TemperatureChannel::PhaseB:
        return pduAddress(HoldingRegister::PhaseBThreshold);
    case TemperatureChannel::PhaseC:
        return pduAddress(HoldingRegister::PhaseCThreshold);
    case TemperatureChannel::Ambient:
        return pduAddress(HoldingRegister::AmbientThreshold);
    }

    return std::nullopt;
}

DeviceField thresholdField(const TemperatureChannel channel)
{
    switch (channel) {
    case TemperatureChannel::PhaseA:
        return DeviceField::PhaseAThreshold;
    case TemperatureChannel::PhaseB:
        return DeviceField::PhaseBThreshold;
    case TemperatureChannel::PhaseC:
        return DeviceField::PhaseCThreshold;
    case TemperatureChannel::Ambient:
        return DeviceField::AmbientThreshold;
    }

    return DeviceField::None;
}

} // namespace

CodecResult<Measurements> decodeMeasurements(const RegisterBlock &block)
{
    if (const auto error = validateBlock(block, measurementsReadBlock)) {
        return *error;
    }

    const auto phaseA = decodeTemperature(block.values[0], DeviceField::PhaseATemperature);
    if (const auto *error = errorFrom(phaseA)) {
        return *error;
    }
    const auto phaseB = decodeTemperature(block.values[1], DeviceField::PhaseBTemperature);
    if (const auto *error = errorFrom(phaseB)) {
        return *error;
    }
    const auto phaseC = decodeTemperature(block.values[2], DeviceField::PhaseCTemperature);
    if (const auto *error = errorFrom(phaseC)) {
        return *error;
    }
    const auto ambient = decodeTemperature(block.values[3], DeviceField::AmbientTemperature);
    if (const auto *error = errorFrom(ambient)) {
        return *error;
    }

    const quint16 lightMillivolts = block.values[4];
    if (lightMillivolts > maximumLightMillivolts) {
        CodecError error{CodecErrorCode::ValueOutOfRange};
        error.field = DeviceField::LightMillivolts;
        error.actualValue = lightMillivolts;
        error.minimumValue = 0;
        error.maximumValue = maximumLightMillivolts;
        return error;
    }

    return Measurements{
        std::get<Temperature>(phaseA),
        std::get<Temperature>(phaseB),
        std::get<Temperature>(phaseC),
        std::get<Temperature>(ambient),
        lightMillivolts,
    };
}

CodecResult<AlarmThresholds> decodeThresholds(const RegisterBlock &block)
{
    if (const auto error = validateBlock(block, thresholdsReadBlock)) {
        return *error;
    }

    const auto phaseA = decodeTemperature(block.values[0], DeviceField::PhaseAThreshold);
    if (const auto *error = errorFrom(phaseA)) {
        return *error;
    }
    const auto phaseB = decodeTemperature(block.values[1], DeviceField::PhaseBThreshold);
    if (const auto *error = errorFrom(phaseB)) {
        return *error;
    }
    const auto phaseC = decodeTemperature(block.values[2], DeviceField::PhaseCThreshold);
    if (const auto *error = errorFrom(phaseC)) {
        return *error;
    }
    const auto ambient = decodeTemperature(block.values[3], DeviceField::AmbientThreshold);
    if (const auto *error = errorFrom(ambient)) {
        return *error;
    }

    return AlarmThresholds{
        std::get<Temperature>(phaseA),
        std::get<Temperature>(phaseB),
        std::get<Temperature>(phaseC),
        std::get<Temperature>(ambient),
    };
}

CodecResult<Status> decodeStatus(const RegisterBlock &block)
{
    if (const auto error = validateBlock(block, statusReadBlock)) {
        return *error;
    }

    return Status{AlarmStatusWord{block.values[0]}, DeviceStatusWord{block.values[1]}};
}

CodecResult<Diagnostics> decodeDiagnostics(const RegisterBlock &block)
{
    if (const auto error = validateBlock(block, diagnosticsReadBlock)) {
        return *error;
    }

    const quint32 lowWord = block.values[1];
    const quint32 highWord = block.values[2];
    return Diagnostics{block.values[0], lowWord | (highWord << 16U)};
}

CodecResult<FirmwareVersion> decodeFirmwareVersion(const RegisterBlock &block)
{
    if (const auto error = validateBlock(block, versionReadBlock)) {
        return *error;
    }

    return FirmwareVersion{block.values[0], block.values[1]};
}

CodecResult<DeviceSnapshot> decodeDeviceSnapshot(const QVector<RegisterBlock> &blocks)
{
    std::array<const RegisterBlock *, deviceSnapshotReadBlocks.size()> found{};
    std::array<bool, deviceSnapshotReadBlocks.size()> duplicated{};
    std::optional<PduAddress> smallestUnknown;

    for (const auto &block : blocks) {
        const auto index = readBlockIndex(block.startAddress);
        if (!index) {
            if (!smallestUnknown || block.startAddress < *smallestUnknown) {
                smallestUnknown = block.startAddress;
            }
            continue;
        }

        if (found[*index] == nullptr) {
            found[*index] = &block;
        } else {
            duplicated[*index] = true;
        }
    }

    if (smallestUnknown) {
        return blockIdentityError(CodecErrorCode::UnknownReadBlock, *smallestUnknown);
    }
    for (std::size_t index = 0; index < duplicated.size(); ++index) {
        if (duplicated[index]) {
            return blockIdentityError(CodecErrorCode::DuplicateReadBlock,
                                      deviceSnapshotReadBlocks[index].startAddress);
        }
    }
    for (std::size_t index = 0; index < found.size(); ++index) {
        if (found[index] == nullptr) {
            return blockIdentityError(CodecErrorCode::MissingReadBlock,
                                      deviceSnapshotReadBlocks[index].startAddress);
        }
    }

    const auto measurements = decodeMeasurements(*found[0]);
    if (const auto *error = errorFrom(measurements)) {
        return *error;
    }
    const auto thresholds = decodeThresholds(*found[1]);
    if (const auto *error = errorFrom(thresholds)) {
        return *error;
    }
    const auto status = decodeStatus(*found[2]);
    if (const auto *error = errorFrom(status)) {
        return *error;
    }
    const auto diagnostics = decodeDiagnostics(*found[3]);
    if (const auto *error = errorFrom(diagnostics)) {
        return *error;
    }
    const auto firmwareVersion = decodeFirmwareVersion(*found[4]);
    if (const auto *error = errorFrom(firmwareVersion)) {
        return *error;
    }

    return DeviceSnapshot{
        std::get<Measurements>(measurements),
        std::get<AlarmThresholds>(thresholds),
        std::get<Status>(status),
        std::get<Diagnostics>(diagnostics),
        std::get<FirmwareVersion>(firmwareVersion),
    };
}

CodecResult<RegisterWrite> encodeAlarmThreshold(const TemperatureChannel channel,
                                                const double celsius)
{
    const auto address = thresholdAddress(channel);
    if (!address) {
        CodecError error{CodecErrorCode::InvalidChannel};
        error.actualValue = static_cast<int>(channel);
        return error;
    }

    const DeviceField field = thresholdField(channel);
    if (!std::isfinite(celsius)) {
        CodecError error{CodecErrorCode::NonFiniteValue};
        error.field = field;
        error.actualValue = celsius;
        return error;
    }

    const double scaled = celsius * 10.0;
    if (!std::isfinite(scaled)) {
        CodecError error{CodecErrorCode::ValueOutOfRange};
        error.field = field;
        error.actualValue = celsius;
        error.minimumValue = -40.0;
        error.maximumValue = 80.0;
        return error;
    }

    const double rounded = std::round(scaled);
    if (std::abs(scaled - rounded) > precisionTolerance) {
        CodecError error{CodecErrorCode::InvalidPrecision};
        error.field = field;
        error.actualValue = celsius;
        return error;
    }
    if (rounded < minimumTemperature || rounded > maximumTemperature) {
        CodecError error{CodecErrorCode::ValueOutOfRange};
        error.field = field;
        error.actualValue = celsius;
        error.minimumValue = -40.0;
        error.maximumValue = 80.0;
        return error;
    }

    const auto deciCelsius = static_cast<qint16>(rounded);
    const quint16 rawValue = deciCelsius < 0
        ? static_cast<quint16>(0x10000 + static_cast<qint32>(deciCelsius))
        : static_cast<quint16>(deciCelsius);
    return RegisterWrite{*address, rawValue, Temperature{deciCelsius}};
}

} // namespace oms555tv::device

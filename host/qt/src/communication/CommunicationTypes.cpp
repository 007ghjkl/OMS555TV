#include "communication/CommunicationTypes.h"

namespace oms555tv::communication {
namespace {

CommunicationError invalidParameter(ErrorCode code, QString diagnostic = {})
{
    CommunicationError error;
    error.category = ErrorCategory::InvalidParameter;
    error.code = code;
    error.diagnostic = std::move(diagnostic);
    return error;
}

bool timeoutIsValid(std::chrono::milliseconds timeout) noexcept
{
    return timeout >= std::chrono::milliseconds(1)
        && timeout <= std::chrono::milliseconds(60000);
}

} // namespace

std::optional<CommunicationError> validateConfig(const ModbusConnectionConfig &config)
{
    if (config.serial.portName.trimmed().isEmpty()) {
        return invalidParameter(ErrorCode::EmptyPortName);
    }
    const bool validParity = config.serial.parity == SerialParity::None
        || config.serial.parity == SerialParity::Even
        || config.serial.parity == SerialParity::Odd;
    const bool validStopBits = config.serial.stopBits == SerialStopBits::One
        || config.serial.stopBits == SerialStopBits::Two;
    if (config.serial.baudRate <= 0 || config.serial.dataBits != 8
        || !validParity || !validStopBits
        || config.serial.flowControl != SerialFlowControl::None) {
        return invalidParameter(ErrorCode::UnsupportedConfiguration);
    }
    if (config.serverAddress < 1 || config.serverAddress > 247) {
        return invalidParameter(ErrorCode::InvalidServerAddress);
    }
    if (!timeoutIsValid(config.defaultResponseTimeout)) {
        return invalidParameter(ErrorCode::InvalidTimeout);
    }
    if (config.maxPendingRequests < 1 || config.maxPendingRequests > 1024) {
        return invalidParameter(ErrorCode::UnsupportedConfiguration);
    }
    return std::nullopt;
}

std::optional<CommunicationError> validateReadRequest(device::PduAddress startAddress,
                                                       quint16 count)
{
    if (count < 1 || count > 125) {
        return invalidParameter(ErrorCode::InvalidQuantity);
    }
    const quint32 end = static_cast<quint32>(startAddress.value())
        + static_cast<quint32>(count) - 1U;
    if (end > 0xFFFFU) {
        return invalidParameter(ErrorCode::AddressRangeOverflow);
    }
    return std::nullopt;
}

std::optional<CommunicationError> validateRequestOptions(const RequestOptions &options)
{
    if (options.responseTimeout.has_value() && !timeoutIsValid(*options.responseTimeout)) {
        return invalidParameter(ErrorCode::InvalidTimeout);
    }
    return std::nullopt;
}

ErrorCode remoteExceptionErrorCode(quint8 exceptionCode) noexcept
{
    switch (exceptionCode) {
    case 0x01:
        return ErrorCode::ModbusIllegalFunction;
    case 0x02:
        return ErrorCode::ModbusIllegalDataAddress;
    case 0x03:
        return ErrorCode::ModbusIllegalDataValue;
    default:
        return ErrorCode::ModbusUnknownException;
    }
}

} // namespace oms555tv::communication

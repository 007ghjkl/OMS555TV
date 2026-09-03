#include "communication/RtuCodec.h"

#include <type_traits>

namespace oms555tv::communication {
namespace {

quint8 byteAt(const QByteArray &bytes, qsizetype index) noexcept
{
    return static_cast<quint8>(bytes.at(index));
}

void appendBigEndian(QByteArray &bytes, quint16 value)
{
    bytes.append(static_cast<char>((value >> 8U) & 0xFFU));
    bytes.append(static_cast<char>(value & 0xFFU));
}

quint16 readBigEndian(const QByteArray &bytes, qsizetype offset) noexcept
{
    return static_cast<quint16>((static_cast<quint16>(byteAt(bytes, offset)) << 8U)
                                | byteAt(bytes, offset + 1));
}

quint16 readLittleEndian(const QByteArray &bytes, qsizetype offset) noexcept
{
    return static_cast<quint16>(byteAt(bytes, offset)
                                | (static_cast<quint16>(byteAt(bytes, offset + 1)) << 8U));
}

quint8 functionCode(const RequestDescriptor &descriptor) noexcept
{
    return std::holds_alternative<ReadRequestDescriptor>(descriptor)
        ? readHoldingRegistersFunction
        : writeSingleRegisterFunction;
}

RtuDecodeResult protocolError(ErrorCode code,
                              qsizetype expected = -1,
                              qsizetype actual = -1)
{
    RtuDecodeResult result;
    result.status = RtuDecodeStatus::Error;
    CommunicationError error;
    error.category = ErrorCategory::Protocol;
    error.code = code;
    if (expected >= 0) {
        error.expectedLength = expected;
    }
    if (actual >= 0) {
        error.actualLength = actual;
    }
    result.error = std::move(error);
    return result;
}

RtuDecodeResult incompleteResult(const QByteArray &candidate, bool frameComplete)
{
    if (!frameComplete) {
        return {};
    }
    if (candidate.size() < 2) {
        RtuDecodeResult result;
        result.status = RtuDecodeStatus::Error;
        CommunicationError error;
        error.category = ErrorCategory::Crc;
        error.code = ErrorCode::ResponseCrcMissing;
        error.actualLength = candidate.size();
        result.error = std::move(error);
        return result;
    }
    return protocolError(ErrorCode::MalformedResponse, -1, candidate.size());
}

} // namespace

quint16 modbusCrc16(const QByteArray &bytes) noexcept
{
    quint16 crc = 0xFFFFU;
    for (const char rawByte : bytes) {
        crc ^= static_cast<quint8>(rawByte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0
                ? static_cast<quint16>((crc >> 1U) ^ 0xA001U)
                : static_cast<quint16>(crc >> 1U);
        }
    }
    return crc;
}

QByteArray appendModbusCrc(QByteArray bytes)
{
    const quint16 crc = modbusCrc16(bytes);
    bytes.append(static_cast<char>(crc & 0xFFU));
    bytes.append(static_cast<char>((crc >> 8U) & 0xFFU));
    return bytes;
}

bool hasValidModbusCrc(const QByteArray &adu) noexcept
{
    if (adu.size() < 2) {
        return false;
    }
    const QByteArray payload = adu.first(adu.size() - 2);
    return readLittleEndian(adu, adu.size() - 2) == modbusCrc16(payload);
}

QByteArray encodeRequestAdu(quint8 serverAddress, const RequestDescriptor &descriptor)
{
    QByteArray bytes;
    bytes.reserve(8);
    bytes.append(static_cast<char>(serverAddress));
    std::visit(
        [&bytes](const auto &request) {
            using T = std::decay_t<decltype(request)>;
            if constexpr (std::is_same_v<T, ReadRequestDescriptor>) {
                bytes.append(static_cast<char>(readHoldingRegistersFunction));
                appendBigEndian(bytes, request.startAddress.value());
                appendBigEndian(bytes, request.count);
            } else {
                bytes.append(static_cast<char>(writeSingleRegisterFunction));
                appendBigEndian(bytes, request.address.value());
                appendBigEndian(bytes, request.rawValue);
            }
        },
        descriptor);
    return appendModbusCrc(std::move(bytes));
}

RtuDecodeResult decodeResponseAdu(quint8 serverAddress,
                                  const RequestDescriptor &descriptor,
                                  const QByteArray &candidate,
                                  bool frameComplete)
{
    if (candidate.size() < 2) {
        return incompleteResult(candidate, frameComplete);
    }

    const quint8 expectedFunction = functionCode(descriptor);
    const quint8 actualFunction = byteAt(candidate, 1);
    qsizetype expectedLength = -1;
    if (actualFunction == static_cast<quint8>(expectedFunction | 0x80U)) {
        expectedLength = 5;
    } else if (actualFunction == readHoldingRegistersFunction) {
        if (candidate.size() < 3) {
            return incompleteResult(candidate, frameComplete);
        }
        expectedLength = 5 + byteAt(candidate, 2);
    } else if (actualFunction == writeSingleRegisterFunction) {
        expectedLength = 8;
    } else if (frameComplete) {
        expectedLength = candidate.size();
    } else {
        return {};
    }

    if (candidate.size() < expectedLength) {
        return incompleteResult(candidate, frameComplete);
    }
    if (candidate.size() > expectedLength) {
        return protocolError(ErrorCode::UnexpectedTrailingBytes,
                             expectedLength,
                             candidate.size());
    }
    if (candidate.size() < 4) {
        return incompleteResult(candidate, true);
    }

    RtuDecodeResult result;
    result.receivedCrc = readLittleEndian(candidate, candidate.size() - 2);
    result.calculatedCrc = modbusCrc16(candidate.first(candidate.size() - 2));
    result.crcStatus = *result.receivedCrc == *result.calculatedCrc
        ? CrcStatus::Valid
        : CrcStatus::Invalid;
    if (result.crcStatus == CrcStatus::Invalid) {
        result.status = RtuDecodeStatus::Error;
        CommunicationError error;
        error.category = ErrorCategory::Crc;
        error.code = ErrorCode::ResponseCrcMismatch;
        result.error = std::move(error);
        return result;
    }

    if (byteAt(candidate, 0) != serverAddress) {
        result.status = RtuDecodeStatus::Error;
        CommunicationError error;
        error.category = ErrorCategory::Protocol;
        error.code = ErrorCode::WrongServerAddress;
        error.expectedValue = serverAddress;
        error.actualValue = byteAt(candidate, 0);
        result.error = std::move(error);
        return result;
    }
    if (actualFunction == static_cast<quint8>(expectedFunction | 0x80U)) {
        if (candidate.size() != 5) {
            return protocolError(ErrorCode::MalformedResponse, 5, candidate.size());
        }
        const quint8 exceptionCode = byteAt(candidate, 2);
        result.status = RtuDecodeStatus::Error;
        CommunicationError error;
        error.category = ErrorCategory::RemoteException;
        error.code = remoteExceptionErrorCode(exceptionCode);
        error.functionCode = expectedFunction;
        error.exceptionCode = exceptionCode;
        result.error = std::move(error);
        return result;
    }
    if (actualFunction != expectedFunction) {
        result.status = RtuDecodeStatus::Error;
        CommunicationError error;
        error.category = ErrorCategory::Protocol;
        error.code = ErrorCode::UnexpectedFunction;
        error.expectedValue = expectedFunction;
        error.actualValue = actualFunction;
        result.error = std::move(error);
        return result;
    }

    if (const auto *read = std::get_if<ReadRequestDescriptor>(&descriptor)) {
        const qsizetype expectedByteCount = static_cast<qsizetype>(read->count) * 2;
        const qsizetype actualByteCount = byteAt(candidate, 2);
        if (actualByteCount != expectedByteCount) {
            result.status = RtuDecodeStatus::Error;
            CommunicationError error;
            error.category = ErrorCategory::Protocol;
            error.code = ErrorCode::InvalidByteCount;
            error.expectedLength = expectedByteCount;
            error.actualLength = actualByteCount;
            result.error = std::move(error);
            return result;
        }
        result.readValues.reserve(read->count);
        for (quint16 index = 0; index < read->count; ++index) {
            result.readValues.append(readBigEndian(candidate, 3 + index * 2));
        }
    } else {
        const auto &write = std::get<WriteRequestDescriptor>(descriptor);
        const quint16 echoedAddress = readBigEndian(candidate, 2);
        const quint16 echoedValue = readBigEndian(candidate, 4);
        if (echoedAddress != write.address.value() || echoedValue != write.rawValue) {
            result.status = RtuDecodeStatus::Error;
            CommunicationError error;
            error.category = ErrorCategory::Protocol;
            error.code = ErrorCode::WriteEchoMismatch;
            error.address = write.address;
            error.expectedValue = write.rawValue;
            error.actualValue = echoedValue;
            result.error = std::move(error);
            return result;
        }
    }

    result.status = RtuDecodeStatus::Success;
    return result;
}

} // namespace oms555tv::communication

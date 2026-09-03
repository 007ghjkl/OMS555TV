#pragma once

#include "communication/CommunicationTypes.h"

namespace oms555tv::communication {

inline constexpr quint8 readHoldingRegistersFunction = 0x03;
inline constexpr quint8 writeSingleRegisterFunction = 0x06;

enum class RtuDecodeStatus { NeedMoreData, Success, Error };

struct RtuDecodeResult {
    RtuDecodeStatus status = RtuDecodeStatus::NeedMoreData;
    QVector<quint16> readValues;
    std::optional<CommunicationError> error;
    CrcStatus crcStatus = CrcStatus::NotAvailable;
    std::optional<quint16> receivedCrc;
    std::optional<quint16> calculatedCrc;
};

[[nodiscard]] quint16 modbusCrc16(const QByteArray &bytes) noexcept;
[[nodiscard]] QByteArray appendModbusCrc(QByteArray bytes);
[[nodiscard]] bool hasValidModbusCrc(const QByteArray &adu) noexcept;

[[nodiscard]] QByteArray encodeRequestAdu(quint8 serverAddress,
                                          const RequestDescriptor &descriptor);

// frameComplete=false 用于分片流；true 表示静默期已经界定当前候选帧。
[[nodiscard]] RtuDecodeResult decodeResponseAdu(quint8 serverAddress,
                                                const RequestDescriptor &descriptor,
                                                const QByteArray &candidate,
                                                bool frameComplete = false);

} // namespace oms555tv::communication

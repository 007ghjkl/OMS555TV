#pragma once

#include "device/RegisterMap.h"

#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>

#include <chrono>
#include <limits>
#include <optional>
#include <variant>

namespace oms555tv::communication {

struct OperationId {
    quint64 value = 0;
};

struct RequestId {
    quint64 value = 0;
};

template<typename Id>
class MonotonicIdGenerator final
{
public:
    explicit constexpr MonotonicIdGenerator(quint64 firstValue = 1) noexcept
        : nextValue_(firstValue)
    {
    }

    [[nodiscard]] std::optional<Id> next() noexcept
    {
        if (nextValue_ == 0) {
            return std::nullopt;
        }
        const Id result{nextValue_};
        nextValue_ = nextValue_ == std::numeric_limits<quint64>::max()
            ? 0
            : nextValue_ + 1;
        return result;
    }

private:
    quint64 nextValue_;
};

[[nodiscard]] constexpr bool operator==(OperationId left, OperationId right) noexcept
{
    return left.value == right.value;
}

[[nodiscard]] constexpr bool operator==(RequestId left, RequestId right) noexcept
{
    return left.value == right.value;
}

[[nodiscard]] constexpr bool operator!=(OperationId left, OperationId right) noexcept
{
    return !(left == right);
}

[[nodiscard]] constexpr bool operator!=(RequestId left, RequestId right) noexcept
{
    return !(left == right);
}

enum class SerialParity { None, Even, Odd };
enum class SerialStopBits { One, Two };
enum class SerialFlowControl { None };

struct SerialPortConfig {
    QString portName;
    qint32 baudRate = 115200;
    quint8 dataBits = 8;
    SerialParity parity = SerialParity::None;
    SerialStopBits stopBits = SerialStopBits::One;
    SerialFlowControl flowControl = SerialFlowControl::None;
};

struct ModbusConnectionConfig {
    SerialPortConfig serial;
    quint8 serverAddress = 1;
    std::chrono::milliseconds defaultResponseTimeout{500};
    qsizetype maxPendingRequests = 64;
};

enum class ConnectionState { Disconnected, Opening, Connected, Closing, Faulted };
enum class RequestState { Queued, InFlight, Succeeded, Failed, Cancelled };
enum class CommunicationOwner { None, Monitor, Testing, ManualDebug };
enum class CloseMode { CancelAll, FinishInFlight };
enum class HandoffMode { FinishInFlight, CancelInFlight };
enum class CrcStatus { NotAvailable, Valid, Invalid };

struct RequestOptions {
    CommunicationOwner owner = CommunicationOwner::None;
    std::optional<std::chrono::milliseconds> responseTimeout;
    QString correlationId;
};

struct ReadRequestDescriptor {
    device::PduAddress startAddress{0};
    quint16 count = 0;
};

struct WriteRequestDescriptor {
    device::PduAddress address{0};
    quint16 rawValue = 0;
};

using RequestDescriptor = std::variant<ReadRequestDescriptor, WriteRequestDescriptor>;

struct RtuTransactionEvidence {
    RequestId requestId;
    CommunicationOwner owner = CommunicationOwner::None;
    quint8 serverAddress = 0;
    quint8 functionCode = 0;
    QByteArray txAdu;
    QByteArray rxAdu;
    CrcStatus txCrcStatus = CrcStatus::NotAvailable;
    CrcStatus rxCrcStatus = CrcStatus::NotAvailable;
    std::optional<quint16> receivedRxCrc;
    std::optional<quint16> calculatedRxCrc;
    QDateTime enqueuedUtc;
    std::optional<QDateTime> txStartedUtc;
    std::optional<QDateTime> txAcceptedUtc;
    std::optional<QDateTime> firstRxUtc;
    QDateTime completedUtc;
    std::optional<std::chrono::nanoseconds> queueDelay;
    std::optional<std::chrono::nanoseconds> rtt;
};

enum class ErrorCategory {
    InvalidParameter,
    Connection,
    Serial,
    Queue,
    Ownership,
    Timeout,
    Cancelled,
    Crc,
    Protocol,
    RemoteException,
    InternalState,
};

enum class ErrorCode {
    EmptyPortName,
    UnsupportedConfiguration,
    InvalidServerAddress,
    InvalidTimeout,
    InvalidQuantity,
    AddressRangeOverflow,
    NotConnected,
    AlreadyOpen,
    OpenFailed,
    ClosedByPeer,
    Closing,
    SerialConfigurationRejected,
    SerialReadFailed,
    SerialWriteFailed,
    PartialWrite,
    ResourceError,
    PermissionError,
    QueueFull,
    NotAcceptingRequests,
    OwnerMismatch,
    OwnershipTransitionInProgress,
    ResponseTimeout,
    CancelledByCaller,
    CancelledByHandoff,
    CancelledByClose,
    ResponseCrcMismatch,
    ResponseCrcMissing,
    WrongServerAddress,
    UnexpectedFunction,
    InvalidByteCount,
    MalformedResponse,
    WriteEchoMismatch,
    UnexpectedTrailingBytes,
    UnsolicitedData,
    ModbusIllegalFunction,
    ModbusIllegalDataAddress,
    ModbusIllegalDataValue,
    ModbusUnknownException,
    InvalidState,
    IdExhausted,
    InvariantViolation,
    WorkerUnavailable,
    RequestNotFoundOrCompleted,
    FakeScriptMismatch,
};

struct CommunicationError {
    ErrorCategory category = ErrorCategory::InternalState;
    ErrorCode code = ErrorCode::InvariantViolation;
    std::optional<OperationId> operationId;
    std::optional<RequestId> requestId;
    ConnectionState connectionState = ConnectionState::Disconnected;
    std::optional<RequestState> requestState;
    CommunicationOwner owner = CommunicationOwner::None;
    std::optional<int> serialError;
    std::optional<quint8> functionCode;
    std::optional<quint8> exceptionCode;
    std::optional<device::PduAddress> address;
    std::optional<qsizetype> expectedLength;
    std::optional<qsizetype> actualLength;
    std::optional<quint16> expectedValue;
    std::optional<quint16> actualValue;
    std::optional<RtuTransactionEvidence> evidence;
    QString diagnostic;
};

struct ControlSubmission {
    std::optional<OperationId> operationId;
    std::optional<CommunicationError> rejection;

    [[nodiscard]] bool accepted() const noexcept { return operationId.has_value(); }
};

struct RequestSubmission {
    std::optional<RequestId> requestId;
    std::optional<CommunicationError> rejection;

    [[nodiscard]] bool accepted() const noexcept { return requestId.has_value(); }
};

struct CommandAcceptance {
    bool isAccepted = false;
    std::optional<CommunicationError> rejection;
};

enum class ControlKind { Open, Close, AcquireOwnership, ReleaseOwnership };

struct ControlResult {
    OperationId operationId;
    ControlKind kind = ControlKind::Open;
    bool succeeded = false;
    std::optional<CommunicationError> error;
};

struct OwnershipResult {
    OperationId operationId;
    CommunicationOwner previousOwner = CommunicationOwner::None;
    CommunicationOwner owner = CommunicationOwner::None;
    bool succeeded = false;
    std::optional<CommunicationError> error;
};

struct ReadHoldingRegistersResult {
    RequestId requestId;
    device::PduAddress startAddress{0};
    QVector<quint16> values;
    RtuTransactionEvidence evidence;
};

struct WriteSingleRegisterResult {
    RequestId requestId;
    device::PduAddress address{0};
    quint16 rawValue = 0;
    RtuTransactionEvidence evidence;
};

using SuccessfulRequestResult = std::variant<ReadHoldingRegistersResult,
                                             WriteSingleRegisterResult>;

struct ModbusRequestResult {
    RequestId requestId;
    RequestState state = RequestState::Failed;
    RequestDescriptor descriptor;
    std::optional<SuccessfulRequestResult> success;
    std::optional<CommunicationError> error;
    RtuTransactionEvidence evidence;
};

[[nodiscard]] std::optional<CommunicationError> validateConfig(
    const ModbusConnectionConfig &config);
[[nodiscard]] std::optional<CommunicationError> validateReadRequest(
    device::PduAddress startAddress, quint16 count);
[[nodiscard]] std::optional<CommunicationError> validateRequestOptions(
    const RequestOptions &options);
[[nodiscard]] ErrorCode remoteExceptionErrorCode(quint8 exceptionCode) noexcept;

} // namespace oms555tv::communication

Q_DECLARE_METATYPE(oms555tv::communication::OperationId)
Q_DECLARE_METATYPE(oms555tv::communication::RequestId)
Q_DECLARE_METATYPE(oms555tv::communication::ConnectionState)
Q_DECLARE_METATYPE(oms555tv::communication::RequestState)
Q_DECLARE_METATYPE(oms555tv::communication::CommunicationOwner)
Q_DECLARE_METATYPE(oms555tv::communication::ControlResult)
Q_DECLARE_METATYPE(oms555tv::communication::OwnershipResult)
Q_DECLARE_METATYPE(oms555tv::communication::ModbusRequestResult)

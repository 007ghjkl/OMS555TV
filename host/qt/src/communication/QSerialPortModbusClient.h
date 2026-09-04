#pragma once

#include "communication/CommunicationWorker.h"
#include "communication/IModbusClient.h"

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QThread>

#include <optional>

namespace oms555tv::communication {

// 应用线程 Facade。所有串口、计时器和协议状态只存在于 workerThread_。
class QSerialPortModbusClient final : public IModbusClient
{
    Q_OBJECT

public:
    explicit QSerialPortModbusClient(QObject *parent = nullptr);
    explicit QSerialPortModbusClient(SerialTransportFactory transportFactory,
                                    QObject *parent = nullptr);
    ~QSerialPortModbusClient() override;

    [[nodiscard]] ConnectionState connectionState() const noexcept override;
    [[nodiscard]] CommunicationOwner activeOwner() const noexcept override;

    ControlSubmission open(const ModbusConnectionConfig &config) override;
    ControlSubmission close(CloseMode mode = CloseMode::CancelAll) override;
    ControlSubmission acquireOwnership(CommunicationOwner owner,
                                       HandoffMode mode) override;
    ControlSubmission releaseOwnership(CommunicationOwner owner,
                                       HandoffMode mode) override;
    RequestSubmission readHoldingRegisters(device::PduAddress startAddress,
                                           quint16 count,
                                           const RequestOptions &options) override;
    RequestSubmission writeSingleRegister(device::PduAddress address,
                                          quint16 rawValue,
                                          const RequestOptions &options) override;
    CommandAcceptance cancelRequest(RequestId requestId) override;

private slots:
    void onWorkerConnectionStateChanged(ConnectionState state);
    void onWorkerControlCompleted(const ControlResult &result);
    void onWorkerOwnershipChanged(const OwnershipResult &result);
    void onWorkerRequestStateChanged(RequestId id, RequestState state);
    void onWorkerRequestCompleted(const ModbusRequestResult &result);

private:
    [[nodiscard]] bool inFacadeThread() const noexcept;
    [[nodiscard]] CommunicationError makeError(ErrorCategory category,
                                               ErrorCode code) const;
    [[nodiscard]] ControlSubmission rejectControl(ErrorCategory category,
                                                  ErrorCode code) const;
    [[nodiscard]] RequestSubmission rejectRequest(ErrorCategory category,
                                                  ErrorCode code) const;
    [[nodiscard]] std::optional<OperationId> nextOperationId();
    [[nodiscard]] std::optional<RequestId> nextRequestId();
    RequestSubmission submit(RequestDescriptor descriptor,
                             const RequestOptions &options);
    void setConnectionState(ConnectionState state);

    QThread workerThread_;
    QPointer<CommunicationWorker> worker_;
    ConnectionState state_ = ConnectionState::Disconnected;
    CommunicationOwner owner_ = CommunicationOwner::None;
    ModbusConnectionConfig config_;
    MonotonicIdGenerator<OperationId> operationIds_;
    MonotonicIdGenerator<RequestId> requestIds_;
    QHash<quint64, RequestState> requests_;
    QSet<quint64> cancellationRequested_;
    qsizetype queuedCount_ = 0;
    bool ownershipTransition_ = false;
    std::optional<ControlKind> pendingOwnershipControlKind_;
    bool acceptingRequests_ = false;
};

} // namespace oms555tv::communication

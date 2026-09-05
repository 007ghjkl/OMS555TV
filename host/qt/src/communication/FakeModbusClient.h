#pragma once

#include "communication/IModbusClient.h"
#include "communication/ManualScheduler.h"

#include <QDateTime>

#include <deque>
#include <memory>
#include <optional>

namespace oms555tv::communication {

struct RequestMatcher {
    CommunicationOwner owner = CommunicationOwner::None;
    RequestDescriptor descriptor;
    std::chrono::milliseconds responseTimeout{500};
    // 空字符串表示不校验；非空时用于验证上层批次/用例关联语义。
    QString correlationId;
};

enum class FakeOutcomeKind {
    ReadSuccess,
    WriteSuccess,
    RemoteException,
    Timeout,
    CrcMismatch,
    ProtocolError,
    SerialError,
    Pending,
};

struct FakeOutcome {
    FakeOutcomeKind kind = FakeOutcomeKind::Pending;
    QVector<quint16> readValues;
    quint8 exceptionCode = 0;
    ErrorCode serialError = ErrorCode::SerialReadFailed;
};

struct FakeStep {
    RequestMatcher expectedRequest;
    FakeOutcome outcome;
    std::chrono::milliseconds virtualDelay{0};
    QByteArray expectedTxAdu;
    QByteArray rxAdu;
    bool omitRtt = false;
};

class FakeModbusClient final : public IModbusClient
{
    Q_OBJECT

public:
    explicit FakeModbusClient(std::shared_ptr<ManualScheduler> scheduler,
                              QDateTime utcEpoch = QDateTime::fromMSecsSinceEpoch(0, Qt::UTC),
                              QObject *parent = nullptr);

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

    void enqueueStep(FakeStep step);
    // 仅供确定性上层状态测试：下一次已接受的 open 在异步完成阶段失败。
    void failNextOpen(CommunicationError error);
    [[nodiscard]] bool scriptConsumed() const noexcept;
    [[nodiscard]] bool hasNonTerminalRequests() const noexcept;
    [[nodiscard]] QString verificationError() const;

private:
    struct PendingRequest {
        RequestId id;
        RequestDescriptor descriptor;
        RequestOptions options;
        std::chrono::milliseconds timeout{0};
        std::chrono::nanoseconds enqueuedAt{0};
        std::optional<std::chrono::nanoseconds> txAcceptedAt;
        RtuTransactionEvidence evidence;
    };

    struct OwnershipTransition {
        OperationId operationId;
        CommunicationOwner previousOwner = CommunicationOwner::None;
        CommunicationOwner targetOwner = CommunicationOwner::None;
        HandoffMode mode = HandoffMode::FinishInFlight;
    };

    struct CloseTransition {
        OperationId operationId;
        CloseMode mode = CloseMode::CancelAll;
    };

    [[nodiscard]] QDateTime utcNow() const;
    [[nodiscard]] ControlSubmission rejectControl(ErrorCategory category,
                                                  ErrorCode code) const;
    [[nodiscard]] RequestSubmission rejectRequest(ErrorCategory category,
                                                  ErrorCode code) const;
    [[nodiscard]] OperationId allocateOperationId();
    [[nodiscard]] RequestId allocateRequestId();
    [[nodiscard]] bool matcherMatches(const RequestMatcher &matcher,
                                      const PendingRequest &request) const;
    RequestSubmission submit(RequestDescriptor descriptor,
                             const RequestOptions &options);
    void startNext();
    void executeStep(FakeStep step);
    void completeActiveSuccess(const QVector<quint16> &readValues,
                               const QByteArray &rxAdu);
    void completeActiveError(CommunicationError error,
                             const QByteArray &rxAdu = {});
    void completeActiveCancelled(ErrorCode code);
    void completeQueuedCancelled(PendingRequest request, ErrorCode code);
    void finalize(PendingRequest request,
                  RequestState state,
                  std::optional<SuccessfulRequestResult> success,
                  std::optional<CommunicationError> error);
    void cancelQueuedByOwner(CommunicationOwner owner, ErrorCode code);
    void maybeCompleteControlTransition();
    void scheduleOwnershipCompletion(OperationId operationId,
                                     CommunicationOwner previous,
                                     CommunicationOwner target);
    void finishClose();

    std::shared_ptr<ManualScheduler> scheduler_;
    QDateTime utcEpoch_;
    ConnectionState state_ = ConnectionState::Disconnected;
    CommunicationOwner owner_ = CommunicationOwner::None;
    ModbusConnectionConfig config_;
    MonotonicIdGenerator<OperationId> operationIds_;
    MonotonicIdGenerator<RequestId> requestIds_;
    std::deque<FakeStep> script_;
    std::deque<PendingRequest> queue_;
    std::optional<PendingRequest> active_;
    std::optional<OwnershipTransition> ownershipTransition_;
    std::optional<CloseTransition> closeTransition_;
    std::optional<CommunicationError> nextOpenFailure_;
};

} // namespace oms555tv::communication

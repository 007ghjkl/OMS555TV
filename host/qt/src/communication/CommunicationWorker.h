#pragma once

#include "communication/SerialTransport.h"

#include <QElapsedTimer>
#include <QTimer>

#include <chrono>
#include <deque>
#include <optional>

namespace oms555tv::communication {

struct WorkerRequest {
    RequestId id;
    RequestDescriptor descriptor;
    RequestOptions options;
    std::chrono::milliseconds timeout{500};
    std::chrono::steady_clock::time_point enqueuedAt;
    std::optional<std::chrono::steady_clock::time_point> txAcceptedAt;
    RtuTransactionEvidence evidence;
};

class CommunicationWorker final : public QObject
{
    Q_OBJECT

public:
    explicit CommunicationWorker(SerialTransportFactory transportFactory,
                                 QObject *parent = nullptr);

public slots:
    void initialize();
    void open(OperationId operationId, ModbusConnectionConfig config);
    void close(OperationId operationId, CloseMode mode);
    void acquireOwnership(OperationId operationId,
                          CommunicationOwner owner,
                          HandoffMode mode);
    void releaseOwnership(OperationId operationId,
                          CommunicationOwner owner,
                          HandoffMode mode);
    void enqueue(WorkerRequest request);
    void cancel(RequestId requestId);
    void shutdown();

signals:
    void connectionStateChanged(oms555tv::communication::ConnectionState state);
    void controlCompleted(const oms555tv::communication::ControlResult &result);
    void ownershipChanged(const oms555tv::communication::OwnershipResult &result);
    void requestStateChanged(oms555tv::communication::RequestId id,
                             oms555tv::communication::RequestState state);
    void requestCompleted(const oms555tv::communication::ModbusRequestResult &result);
    void stopped();

private slots:
    void onReadyRead();
    void onSerialError(int serialError);
    void onResponseTimeout();
    void onSilenceElapsed();

private:
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

    [[nodiscard]] bool inWorkerThread() const noexcept;
    [[nodiscard]] int silenceIntervalMs() const noexcept;
    [[nodiscard]] CommunicationError makeError(ErrorCategory category,
                                               ErrorCode code) const;
    [[nodiscard]] ErrorCode serialErrorCode(int serialError) const noexcept;
    void startNextWhenSilent();
    void startNext();
    void evaluateCandidate(bool frameComplete);
    void completeActiveSuccess(const QVector<quint16> &readValues);
    void completeActiveError(CommunicationError error);
    void completeActiveCancelled(ErrorCode code);
    void completeQueuedCancelled(WorkerRequest request, ErrorCode code);
    void finalize(WorkerRequest request,
                  RequestState state,
                  std::optional<SuccessfulRequestResult> success,
                  std::optional<CommunicationError> error);
    void cancelQueuedByOwner(CommunicationOwner owner, ErrorCode code);
    void failAllQueued(const CommunicationError &error);
    void maybeCompleteTransition();
    void finishOwnershipTransition();
    void finishClose();
    void markRxActivity();

    SerialTransportFactory transportFactory_;
    SerialTransport *transport_ = nullptr;
    QTimer *responseTimer_ = nullptr;
    QTimer *silenceTimer_ = nullptr;
    ConnectionState state_ = ConnectionState::Disconnected;
    CommunicationOwner owner_ = CommunicationOwner::None;
    ModbusConnectionConfig config_;
    std::deque<WorkerRequest> queue_;
    std::optional<WorkerRequest> active_;
    std::optional<OwnershipTransition> ownershipTransition_;
    std::optional<CloseTransition> closeTransition_;
    QElapsedTimer lastRxTimer_;
    bool resynchronizing_ = false;
    bool initialized_ = false;
    bool shuttingDown_ = false;
};

} // namespace oms555tv::communication

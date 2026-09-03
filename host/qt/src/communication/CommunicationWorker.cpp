#include "communication/CommunicationWorker.h"

#include "communication/RtuCodec.h"

#include <QSerialPort>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace oms555tv::communication {
namespace {

quint8 descriptorFunction(const RequestDescriptor &descriptor) noexcept
{
    return std::holds_alternative<ReadRequestDescriptor>(descriptor)
        ? readHoldingRegistersFunction
        : writeSingleRegisterFunction;
}

} // namespace

CommunicationWorker::CommunicationWorker(SerialTransportFactory transportFactory,
                                         QObject *parent)
    : QObject(parent)
    , transportFactory_(std::move(transportFactory))
{
}

bool CommunicationWorker::inWorkerThread() const noexcept
{
    return QThread::currentThread() == thread();
}

void CommunicationWorker::initialize()
{
    Q_ASSERT(inWorkerThread());
    if (initialized_) {
        return;
    }
    transport_ = transportFactory_ ? transportFactory_() : nullptr;
    if (transport_ == nullptr) {
        return;
    }
    transport_->setParent(this);
    responseTimer_ = new QTimer(this);
    responseTimer_->setSingleShot(true);
    silenceTimer_ = new QTimer(this);
    silenceTimer_->setSingleShot(true);
    connect(transport_, &SerialTransport::readyRead,
            this, &CommunicationWorker::onReadyRead);
    connect(transport_, &SerialTransport::errorOccurred,
            this, &CommunicationWorker::onSerialError);
    connect(responseTimer_, &QTimer::timeout,
            this, &CommunicationWorker::onResponseTimeout);
    connect(silenceTimer_, &QTimer::timeout,
            this, &CommunicationWorker::onSilenceElapsed);
    initialized_ = true;
}

CommunicationError CommunicationWorker::makeError(ErrorCategory category,
                                                   ErrorCode code) const
{
    CommunicationError error;
    error.category = category;
    error.code = code;
    error.connectionState = state_;
    error.owner = owner_;
    return error;
}

ErrorCode CommunicationWorker::serialErrorCode(int serialError) const noexcept
{
    if (serialError == static_cast<int>(QSerialPort::PermissionError)) {
        return ErrorCode::PermissionError;
    }
    if (serialError == static_cast<int>(QSerialPort::ResourceError)
        || serialError == static_cast<int>(QSerialPort::DeviceNotFoundError)) {
        return ErrorCode::ResourceError;
    }
    return ErrorCode::SerialReadFailed;
}

void CommunicationWorker::open(OperationId operationId,
                               ModbusConnectionConfig config)
{
    Q_ASSERT(inWorkerThread());
    if (!initialized_) {
        initialize();
    }
    ControlResult result{operationId, ControlKind::Open, false, std::nullopt};
    if (transport_ == nullptr) {
        auto error = makeError(ErrorCategory::InternalState, ErrorCode::WorkerUnavailable);
        error.operationId = operationId;
        result.error = std::move(error);
        state_ = ConnectionState::Faulted;
        emit connectionStateChanged(state_);
        emit controlCompleted(result);
        return;
    }
    config_ = std::move(config);
    state_ = ConnectionState::Opening;
    if (!transport_->configure(config_.serial)) {
        auto error = makeError(ErrorCategory::Connection,
                               ErrorCode::SerialConfigurationRejected);
        error.operationId = operationId;
        error.serialError = transport_->errorCode();
        error.diagnostic = transport_->errorString();
        result.error = std::move(error);
        state_ = ConnectionState::Faulted;
        emit connectionStateChanged(state_);
        emit controlCompleted(result);
        return;
    }
    if (!transport_->open()) {
        auto error = makeError(ErrorCategory::Connection, ErrorCode::OpenFailed);
        error.operationId = operationId;
        error.serialError = transport_->errorCode();
        error.diagnostic = transport_->errorString();
        result.error = std::move(error);
        transport_->close();
        state_ = ConnectionState::Faulted;
        emit connectionStateChanged(state_);
        emit controlCompleted(result);
        return;
    }

    (void)transport_->readAll();
    queue_.clear();
    active_.reset();
    owner_ = CommunicationOwner::None;
    // 打开后也先观察一个 RTU 静默期，首个请求不得紧贴端口打开动作发送。
    resynchronizing_ = true;
    lastRxTimer_.start();
    silenceTimer_->start(silenceIntervalMs());
    state_ = ConnectionState::Connected;
    result.succeeded = true;
    emit connectionStateChanged(state_);
    emit controlCompleted(result);
}

void CommunicationWorker::close(OperationId operationId, CloseMode mode)
{
    Q_ASSERT(inWorkerThread());
    closeTransition_ = CloseTransition{operationId, mode};
    state_ = ConnectionState::Closing;
    emit connectionStateChanged(state_);
    while (!queue_.empty()) {
        WorkerRequest request = std::move(queue_.front());
        queue_.pop_front();
        completeQueuedCancelled(std::move(request), ErrorCode::CancelledByClose);
    }
    if (active_ && mode == CloseMode::CancelAll) {
        completeActiveCancelled(ErrorCode::CancelledByClose);
    }
    maybeCompleteTransition();
}

void CommunicationWorker::acquireOwnership(OperationId operationId,
                                            CommunicationOwner owner,
                                            HandoffMode mode)
{
    Q_ASSERT(inWorkerThread());
    if (owner_ == CommunicationOwner::None || owner_ == owner) {
        const CommunicationOwner previous = owner_;
        owner_ = owner;
        emit ownershipChanged({operationId, previous, owner_, true, std::nullopt});
        return;
    }
    ownershipTransition_ = OwnershipTransition{operationId, owner_, owner, mode};
    cancelQueuedByOwner(owner_, ErrorCode::CancelledByHandoff);
    if (active_ && active_->options.owner == owner_
        && mode == HandoffMode::CancelInFlight) {
        completeActiveCancelled(ErrorCode::CancelledByHandoff);
    }
    maybeCompleteTransition();
}

void CommunicationWorker::releaseOwnership(OperationId operationId,
                                            CommunicationOwner owner,
                                            HandoffMode mode)
{
    Q_ASSERT(inWorkerThread());
    ownershipTransition_ = OwnershipTransition{
        operationId, owner, CommunicationOwner::None, mode};
    cancelQueuedByOwner(owner, ErrorCode::CancelledByHandoff);
    if (active_ && active_->options.owner == owner
        && mode == HandoffMode::CancelInFlight) {
        completeActiveCancelled(ErrorCode::CancelledByHandoff);
    }
    maybeCompleteTransition();
}

void CommunicationWorker::enqueue(WorkerRequest request)
{
    Q_ASSERT(inWorkerThread());
    if (state_ != ConnectionState::Connected || ownershipTransition_ || closeTransition_) {
        auto error = makeError(ErrorCategory::Queue, ErrorCode::NotAcceptingRequests);
        error.requestId = request.id;
        finalize(std::move(request), RequestState::Failed, std::nullopt, std::move(error));
        return;
    }
    queue_.push_back(std::move(request));
    startNextWhenSilent();
}

int CommunicationWorker::silenceIntervalMs() const noexcept
{
    if (config_.serial.baudRate > 19200) {
        return 2;
    }
    const int parityBits = config_.serial.parity == SerialParity::None ? 0 : 1;
    const int stopBits = config_.serial.stopBits == SerialStopBits::Two ? 2 : 1;
    const int bitsPerCharacter = 1 + config_.serial.dataBits + parityBits + stopBits;
    const double milliseconds = 3500.0 * bitsPerCharacter
        / static_cast<double>(std::max(1, config_.serial.baudRate));
    return std::max(1, static_cast<int>(std::ceil(milliseconds)));
}

void CommunicationWorker::startNextWhenSilent()
{
    if (active_ || queue_.empty() || state_ != ConnectionState::Connected
        || ownershipTransition_ || closeTransition_) {
        return;
    }
    if (!lastRxTimer_.isValid()) {
        resynchronizing_ = false;
        startNext();
        return;
    }
    const int remaining = silenceIntervalMs() - static_cast<int>(lastRxTimer_.elapsed());
    if (remaining <= 0) {
        resynchronizing_ = false;
        startNext();
    } else {
        silenceTimer_->start(remaining);
    }
}

void CommunicationWorker::startNext()
{
    if (active_ || queue_.empty() || state_ != ConnectionState::Connected
        || ownershipTransition_ || closeTransition_ || resynchronizing_) {
        return;
    }
    active_ = std::move(queue_.front());
    queue_.pop_front();
    active_->evidence.txStartedUtc = QDateTime::currentDateTimeUtc();
    active_->evidence.queueDelay = std::chrono::steady_clock::now() - active_->enqueuedAt;
    active_->evidence.txAdu = encodeRequestAdu(config_.serverAddress,
                                               active_->descriptor);
    active_->evidence.txCrcStatus = CrcStatus::Valid;
    emit requestStateChanged(active_->id, RequestState::InFlight);

    const qint64 accepted = transport_->write(active_->evidence.txAdu);
    if (accepted < 0) {
        auto error = makeError(ErrorCategory::Serial, ErrorCode::SerialWriteFailed);
        error.serialError = transport_->errorCode();
        error.diagnostic = transport_->errorString();
        completeActiveError(std::move(error));
        return;
    }
    if (accepted != active_->evidence.txAdu.size()) {
        auto error = makeError(ErrorCategory::Serial, ErrorCode::PartialWrite);
        error.expectedLength = active_->evidence.txAdu.size();
        error.actualLength = accepted;
        error.serialError = transport_->errorCode();
        error.diagnostic = transport_->errorString();
        completeActiveError(std::move(error));
        return;
    }
    active_->txAcceptedAt = std::chrono::steady_clock::now();
    active_->evidence.txAcceptedUtc = QDateTime::currentDateTimeUtc();
    responseTimer_->start(static_cast<int>(active_->timeout.count()));
}

void CommunicationWorker::markRxActivity()
{
    lastRxTimer_.restart();
    silenceTimer_->start(silenceIntervalMs());
}

void CommunicationWorker::onReadyRead()
{
    Q_ASSERT(inWorkerThread());
    const QByteArray bytes = transport_->readAll();
    if (bytes.isEmpty()) {
        return;
    }
    markRxActivity();
    if (!active_ || resynchronizing_) {
        return;
    }
    if (!active_->evidence.firstRxUtc.has_value()) {
        active_->evidence.firstRxUtc = QDateTime::currentDateTimeUtc();
    }
    active_->evidence.rxAdu.append(bytes);
    evaluateCandidate(false);
}

void CommunicationWorker::evaluateCandidate(bool frameComplete)
{
    if (!active_) {
        return;
    }
    const auto decoded = decodeResponseAdu(config_.serverAddress,
                                           active_->descriptor,
                                           active_->evidence.rxAdu,
                                           frameComplete);
    if (decoded.status == RtuDecodeStatus::NeedMoreData) {
        return;
    }
    if (frameComplete && decoded.status == RtuDecodeStatus::Error
        && decoded.error.has_value()
        && (decoded.error->code == ErrorCode::MalformedResponse
            || decoded.error->code == ErrorCode::ResponseCrcMissing)) {
        // 已知响应的短分片仍可能继续到达；由响应超时给出最终结论。
        return;
    }
    active_->evidence.rxCrcStatus = decoded.crcStatus;
    active_->evidence.receivedRxCrc = decoded.receivedCrc;
    active_->evidence.calculatedRxCrc = decoded.calculatedCrc;
    if (decoded.status == RtuDecodeStatus::Success) {
        completeActiveSuccess(decoded.readValues);
    } else {
        completeActiveError(*decoded.error);
    }
}

void CommunicationWorker::completeActiveSuccess(const QVector<quint16> &readValues)
{
    if (!active_) {
        return;
    }
    WorkerRequest request = std::move(*active_);
    active_.reset();
    responseTimer_->stop();
    std::optional<SuccessfulRequestResult> success;
    if (const auto *read = std::get_if<ReadRequestDescriptor>(&request.descriptor)) {
        success = ReadHoldingRegistersResult{
            request.id, read->startAddress, readValues, request.evidence};
    } else {
        const auto &write = std::get<WriteRequestDescriptor>(request.descriptor);
        success = WriteSingleRegisterResult{
            request.id, write.address, write.rawValue, request.evidence};
    }
    finalize(std::move(request), RequestState::Succeeded, std::move(success), std::nullopt);
    maybeCompleteTransition();
    startNextWhenSilent();
}

void CommunicationWorker::completeActiveError(CommunicationError error)
{
    if (!active_) {
        return;
    }
    WorkerRequest request = std::move(*active_);
    active_.reset();
    responseTimer_->stop();
    resynchronizing_ = true;
    lastRxTimer_.restart();
    silenceTimer_->start(silenceIntervalMs());
    finalize(std::move(request), RequestState::Failed, std::nullopt, std::move(error));
    maybeCompleteTransition();
}

void CommunicationWorker::completeActiveCancelled(ErrorCode code)
{
    if (!active_) {
        return;
    }
    WorkerRequest request = std::move(*active_);
    active_.reset();
    responseTimer_->stop();
    resynchronizing_ = true;
    lastRxTimer_.restart();
    silenceTimer_->start(silenceIntervalMs());
    auto error = makeError(ErrorCategory::Cancelled, code);
    finalize(std::move(request), RequestState::Cancelled, std::nullopt, std::move(error));
    maybeCompleteTransition();
}

void CommunicationWorker::completeQueuedCancelled(WorkerRequest request, ErrorCode code)
{
    auto error = makeError(ErrorCategory::Cancelled, code);
    finalize(std::move(request), RequestState::Cancelled, std::nullopt, std::move(error));
}

void CommunicationWorker::finalize(WorkerRequest request,
                                   RequestState state,
                                   std::optional<SuccessfulRequestResult> success,
                                   std::optional<CommunicationError> error)
{
    request.evidence.completedUtc = QDateTime::currentDateTimeUtc();
    if (request.txAcceptedAt.has_value()) {
        request.evidence.rtt = std::chrono::steady_clock::now() - *request.txAcceptedAt;
    }
    if (success.has_value()) {
        std::visit([&request](auto &value) { value.evidence = request.evidence; }, *success);
    }
    if (error.has_value()) {
        error->requestId = request.id;
        error->requestState = state;
        error->connectionState = state_;
        error->owner = request.options.owner;
        error->functionCode = descriptorFunction(request.descriptor);
        error->evidence = request.evidence;
    }
    emit requestStateChanged(request.id, state);
    emit requestCompleted({request.id,
                           state,
                           request.descriptor,
                           std::move(success),
                           std::move(error),
                           request.evidence});
}

void CommunicationWorker::cancel(RequestId requestId)
{
    Q_ASSERT(inWorkerThread());
    const auto queued = std::find_if(queue_.begin(), queue_.end(),
                                     [requestId](const WorkerRequest &request) {
                                         return request.id == requestId;
                                     });
    if (queued != queue_.end()) {
        WorkerRequest request = std::move(*queued);
        queue_.erase(queued);
        completeQueuedCancelled(std::move(request), ErrorCode::CancelledByCaller);
        return;
    }
    if (active_ && active_->id == requestId) {
        completeActiveCancelled(ErrorCode::CancelledByCaller);
    }
}

void CommunicationWorker::cancelQueuedByOwner(CommunicationOwner owner, ErrorCode code)
{
    auto iterator = queue_.begin();
    while (iterator != queue_.end()) {
        if (iterator->options.owner == owner) {
            WorkerRequest request = std::move(*iterator);
            iterator = queue_.erase(iterator);
            completeQueuedCancelled(std::move(request), code);
        } else {
            ++iterator;
        }
    }
}

void CommunicationWorker::failAllQueued(const CommunicationError &prototype)
{
    while (!queue_.empty()) {
        WorkerRequest request = std::move(queue_.front());
        queue_.pop_front();
        finalize(std::move(request), RequestState::Failed, std::nullopt, prototype);
    }
}

void CommunicationWorker::onResponseTimeout()
{
    if (!active_) {
        return;
    }
    auto error = makeError(ErrorCategory::Timeout, ErrorCode::ResponseTimeout);
    completeActiveError(std::move(error));
}

void CommunicationWorker::onSilenceElapsed()
{
    if (active_ && !resynchronizing_) {
        evaluateCandidate(true);
        return;
    }
    resynchronizing_ = false;
    maybeCompleteTransition();
    startNextWhenSilent();
}

void CommunicationWorker::onSerialError(int serialError)
{
    if (serialError == static_cast<int>(QSerialPort::NoError)
        || state_ == ConnectionState::Opening
        || state_ == ConnectionState::Closing
        || shuttingDown_) {
        return;
    }
    state_ = ConnectionState::Faulted;
    auto error = makeError(ErrorCategory::Serial, serialErrorCode(serialError));
    error.serialError = serialError;
    error.diagnostic = transport_ ? transport_->errorString() : QString{};
    if (active_) {
        completeActiveError(error);
    }
    failAllQueued(error);
    if (ownershipTransition_) {
        const OwnershipTransition transition = *ownershipTransition_;
        ownershipTransition_.reset();
        CommunicationError transitionError = error;
        transitionError.operationId = transition.operationId;
        emit ownershipChanged({transition.operationId,
                               transition.previousOwner,
                               CommunicationOwner::None,
                               false,
                               std::move(transitionError)});
    }
    if (transport_) {
        transport_->close();
    }
    owner_ = CommunicationOwner::None;
    emit connectionStateChanged(state_);
}

void CommunicationWorker::maybeCompleteTransition()
{
    if (active_ || resynchronizing_) {
        return;
    }
    if (state_ == ConnectionState::Faulted) {
        return;
    }
    if (closeTransition_) {
        finishClose();
        return;
    }
    if (ownershipTransition_) {
        finishOwnershipTransition();
    }
}

void CommunicationWorker::finishOwnershipTransition()
{
    const OwnershipTransition transition = *ownershipTransition_;
    ownershipTransition_.reset();
    owner_ = transition.targetOwner;
    emit ownershipChanged({transition.operationId,
                           transition.previousOwner,
                           transition.targetOwner,
                           true,
                           std::nullopt});
    startNextWhenSilent();
}

void CommunicationWorker::finishClose()
{
    const CloseTransition transition = *closeTransition_;
    closeTransition_.reset();
    responseTimer_->stop();
    silenceTimer_->stop();
    if (transport_) {
        (void)transport_->readAll();
        transport_->close();
    }
    owner_ = CommunicationOwner::None;
    resynchronizing_ = false;
    lastRxTimer_.invalidate();
    state_ = ConnectionState::Disconnected;
    emit connectionStateChanged(state_);
    emit controlCompleted({transition.operationId,
                           ControlKind::Close,
                           true,
                           std::nullopt});
}

void CommunicationWorker::shutdown()
{
    Q_ASSERT(inWorkerThread());
    if (shuttingDown_) {
        return;
    }
    shuttingDown_ = true;
    if (responseTimer_) {
        responseTimer_->stop();
    }
    if (silenceTimer_) {
        silenceTimer_->stop();
    }
    queue_.clear();
    active_.reset();
    if (transport_) {
        transport_->close();
    }
    state_ = ConnectionState::Disconnected;
    owner_ = CommunicationOwner::None;
    emit stopped();
}

} // namespace oms555tv::communication

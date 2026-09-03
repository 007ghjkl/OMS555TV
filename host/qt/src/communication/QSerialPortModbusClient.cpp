#include "communication/QSerialPortModbusClient.h"

#include "communication/QSerialPortTransport.h"
#include "communication/RtuCodec.h"

#include <QMetaObject>
#include <QThread>

#include <chrono>
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

QSerialPortModbusClient::QSerialPortModbusClient(QObject *parent)
    : QSerialPortModbusClient([] { return new QSerialPortTransport; }, parent)
{
}

QSerialPortModbusClient::QSerialPortModbusClient(
    SerialTransportFactory transportFactory,
    QObject *parent)
    : IModbusClient(parent)
{
    qRegisterMetaType<ControlResult>();
    qRegisterMetaType<OwnershipResult>();
    qRegisterMetaType<ModbusRequestResult>();

    auto *worker = new CommunicationWorker(std::move(transportFactory));
    worker_ = worker;
    worker->moveToThread(&workerThread_);
    connect(&workerThread_, &QThread::started,
            worker, &CommunicationWorker::initialize);
    connect(worker, &CommunicationWorker::connectionStateChanged,
            this, &QSerialPortModbusClient::onWorkerConnectionStateChanged,
            Qt::QueuedConnection);
    connect(worker, &CommunicationWorker::controlCompleted,
            this, &QSerialPortModbusClient::onWorkerControlCompleted,
            Qt::QueuedConnection);
    connect(worker, &CommunicationWorker::ownershipChanged,
            this, &QSerialPortModbusClient::onWorkerOwnershipChanged,
            Qt::QueuedConnection);
    connect(worker, &CommunicationWorker::requestStateChanged,
            this, &QSerialPortModbusClient::onWorkerRequestStateChanged,
            Qt::QueuedConnection);
    connect(worker, &CommunicationWorker::requestCompleted,
            this, &QSerialPortModbusClient::onWorkerRequestCompleted,
            Qt::QueuedConnection);
    connect(worker, &CommunicationWorker::stopped,
            &workerThread_, &QThread::quit,
            Qt::DirectConnection);
    connect(&workerThread_, &QThread::finished,
            worker, &QObject::deleteLater);
    workerThread_.setObjectName(QStringLiteral("OMS555TV Modbus communication"));
    workerThread_.start();
}

QSerialPortModbusClient::~QSerialPortModbusClient()
{
    acceptingRequests_ = false;
    if (worker_ && workerThread_.isRunning()) {
        QMetaObject::invokeMethod(worker_, &CommunicationWorker::shutdown,
                                  Qt::BlockingQueuedConnection);
    }
    workerThread_.quit();
    if (!workerThread_.wait(3000)) {
        // 禁止 terminate()；最终进程退出时由 Qt 完成剩余回收。
        workerThread_.requestInterruption();
        workerThread_.quit();
        (void)workerThread_.wait(3000);
    }
}

bool QSerialPortModbusClient::inFacadeThread() const noexcept
{
    return QThread::currentThread() == thread();
}

ConnectionState QSerialPortModbusClient::connectionState() const noexcept
{
    return state_;
}

CommunicationOwner QSerialPortModbusClient::activeOwner() const noexcept
{
    return owner_;
}

CommunicationError QSerialPortModbusClient::makeError(ErrorCategory category,
                                                      ErrorCode code) const
{
    CommunicationError error;
    error.category = category;
    error.code = code;
    error.connectionState = state_;
    error.owner = owner_;
    return error;
}

ControlSubmission QSerialPortModbusClient::rejectControl(ErrorCategory category,
                                                         ErrorCode code) const
{
    return {std::nullopt, makeError(category, code)};
}

RequestSubmission QSerialPortModbusClient::rejectRequest(ErrorCategory category,
                                                         ErrorCode code) const
{
    return {std::nullopt, makeError(category, code)};
}

std::optional<OperationId> QSerialPortModbusClient::nextOperationId()
{
    return operationIds_.next();
}

std::optional<RequestId> QSerialPortModbusClient::nextRequestId()
{
    return requestIds_.next();
}

void QSerialPortModbusClient::setConnectionState(ConnectionState state)
{
    if (state_ == state) {
        return;
    }
    state_ = state;
    emit connectionStateChanged(state_);
}

ControlSubmission QSerialPortModbusClient::open(const ModbusConnectionConfig &config)
{
    if (!inFacadeThread() || !worker_) {
        return rejectControl(ErrorCategory::InternalState, ErrorCode::WorkerUnavailable);
    }
    if (const auto error = validateConfig(config)) {
        CommunicationError rejection = *error;
        rejection.connectionState = state_;
        rejection.owner = owner_;
        return {std::nullopt, std::move(rejection)};
    }
    if (state_ != ConnectionState::Disconnected) {
        return state_ == ConnectionState::Connected
            ? rejectControl(ErrorCategory::Connection, ErrorCode::AlreadyOpen)
            : rejectControl(ErrorCategory::InternalState, ErrorCode::InvalidState);
    }
    const auto id = nextOperationId();
    if (!id) {
        return rejectControl(ErrorCategory::InternalState, ErrorCode::IdExhausted);
    }
    config_ = config;
    owner_ = CommunicationOwner::None;
    acceptingRequests_ = false;
    setConnectionState(ConnectionState::Opening);
    QMetaObject::invokeMethod(worker_,
                              [worker = worker_, operationId = *id, config] {
                                  if (worker) {
                                      worker->open(operationId, config);
                                  }
                              },
                              Qt::QueuedConnection);
    return {*id, std::nullopt};
}

ControlSubmission QSerialPortModbusClient::close(CloseMode mode)
{
    if (!inFacadeThread() || !worker_) {
        return rejectControl(ErrorCategory::InternalState, ErrorCode::WorkerUnavailable);
    }
    if (state_ != ConnectionState::Connected && state_ != ConnectionState::Faulted) {
        return rejectControl(ErrorCategory::InternalState, ErrorCode::InvalidState);
    }
    if (ownershipTransition_ || state_ == ConnectionState::Closing) {
        return rejectControl(ErrorCategory::Queue, ErrorCode::NotAcceptingRequests);
    }
    const auto id = nextOperationId();
    if (!id) {
        return rejectControl(ErrorCategory::InternalState, ErrorCode::IdExhausted);
    }
    acceptingRequests_ = false;
    setConnectionState(ConnectionState::Closing);
    QMetaObject::invokeMethod(worker_,
                              [worker = worker_, operationId = *id, mode] {
                                  if (worker) {
                                      worker->close(operationId, mode);
                                  }
                              },
                              Qt::QueuedConnection);
    return {*id, std::nullopt};
}

ControlSubmission QSerialPortModbusClient::acquireOwnership(CommunicationOwner owner,
                                                            HandoffMode mode)
{
    if (!inFacadeThread() || !worker_) {
        return rejectControl(ErrorCategory::InternalState, ErrorCode::WorkerUnavailable);
    }
    if (state_ != ConnectionState::Connected) {
        return rejectControl(ErrorCategory::Connection, ErrorCode::NotConnected);
    }
    if (owner == CommunicationOwner::None) {
        return rejectControl(ErrorCategory::Ownership, ErrorCode::OwnerMismatch);
    }
    if (ownershipTransition_) {
        return rejectControl(ErrorCategory::Ownership,
                             ErrorCode::OwnershipTransitionInProgress);
    }
    const auto id = nextOperationId();
    if (!id) {
        return rejectControl(ErrorCategory::InternalState, ErrorCode::IdExhausted);
    }
    ownershipTransition_ = true;
    acceptingRequests_ = false;
    QMetaObject::invokeMethod(worker_,
                              [worker = worker_, operationId = *id, owner, mode] {
                                  if (worker) {
                                      worker->acquireOwnership(operationId, owner, mode);
                                  }
                              },
                              Qt::QueuedConnection);
    return {*id, std::nullopt};
}

ControlSubmission QSerialPortModbusClient::releaseOwnership(CommunicationOwner owner,
                                                            HandoffMode mode)
{
    if (!inFacadeThread() || !worker_) {
        return rejectControl(ErrorCategory::InternalState, ErrorCode::WorkerUnavailable);
    }
    if (state_ != ConnectionState::Connected) {
        return rejectControl(ErrorCategory::Connection, ErrorCode::NotConnected);
    }
    if (owner == CommunicationOwner::None || owner != owner_) {
        return rejectControl(ErrorCategory::Ownership, ErrorCode::OwnerMismatch);
    }
    if (ownershipTransition_) {
        return rejectControl(ErrorCategory::Ownership,
                             ErrorCode::OwnershipTransitionInProgress);
    }
    const auto id = nextOperationId();
    if (!id) {
        return rejectControl(ErrorCategory::InternalState, ErrorCode::IdExhausted);
    }
    ownershipTransition_ = true;
    acceptingRequests_ = false;
    QMetaObject::invokeMethod(worker_,
                              [worker = worker_, operationId = *id, owner, mode] {
                                  if (worker) {
                                      worker->releaseOwnership(operationId, owner, mode);
                                  }
                              },
                              Qt::QueuedConnection);
    return {*id, std::nullopt};
}

RequestSubmission QSerialPortModbusClient::readHoldingRegisters(
    device::PduAddress startAddress,
    quint16 count,
    const RequestOptions &options)
{
    if (const auto error = validateReadRequest(startAddress, count)) {
        CommunicationError rejection = *error;
        rejection.connectionState = state_;
        rejection.owner = owner_;
        return {std::nullopt, std::move(rejection)};
    }
    return submit(ReadRequestDescriptor{startAddress, count}, options);
}

RequestSubmission QSerialPortModbusClient::writeSingleRegister(
    device::PduAddress address,
    quint16 rawValue,
    const RequestOptions &options)
{
    return submit(WriteRequestDescriptor{address, rawValue}, options);
}

RequestSubmission QSerialPortModbusClient::submit(RequestDescriptor descriptor,
                                                  const RequestOptions &options)
{
    if (!inFacadeThread() || !worker_) {
        return rejectRequest(ErrorCategory::InternalState, ErrorCode::WorkerUnavailable);
    }
    if (const auto error = validateRequestOptions(options)) {
        CommunicationError rejection = *error;
        rejection.connectionState = state_;
        rejection.owner = owner_;
        return {std::nullopt, std::move(rejection)};
    }
    if (state_ != ConnectionState::Connected) {
        return rejectRequest(ErrorCategory::Connection,
                             state_ == ConnectionState::Closing
                                 ? ErrorCode::Closing
                                 : ErrorCode::NotConnected);
    }
    if (!acceptingRequests_ || ownershipTransition_) {
        return rejectRequest(ErrorCategory::Queue, ErrorCode::NotAcceptingRequests);
    }
    if (owner_ == CommunicationOwner::None || options.owner != owner_) {
        return rejectRequest(ErrorCategory::Ownership, ErrorCode::OwnerMismatch);
    }
    if (queuedCount_ >= config_.maxPendingRequests) {
        return rejectRequest(ErrorCategory::Queue, ErrorCode::QueueFull);
    }
    const auto id = nextRequestId();
    if (!id) {
        return rejectRequest(ErrorCategory::InternalState, ErrorCode::IdExhausted);
    }
    const auto timeout = options.responseTimeout.value_or(config_.defaultResponseTimeout);
    RtuTransactionEvidence evidence;
    evidence.requestId = *id;
    evidence.owner = options.owner;
    evidence.serverAddress = config_.serverAddress;
    evidence.functionCode = descriptorFunction(descriptor);
    evidence.enqueuedUtc = QDateTime::currentDateTimeUtc();
    WorkerRequest request{*id,
                          descriptor,
                          options,
                          timeout,
                          std::chrono::steady_clock::now(),
                          std::nullopt,
                          evidence};
    requests_.insert(id->value, RequestState::Queued);
    ++queuedCount_;
    emit requestStateChanged(*id, RequestState::Queued);
    QMetaObject::invokeMethod(worker_,
                              [worker = worker_, request = std::move(request)]() mutable {
                                  if (worker) {
                                      worker->enqueue(std::move(request));
                                  }
                              },
                              Qt::QueuedConnection);
    return {*id, std::nullopt};
}

CommandAcceptance QSerialPortModbusClient::cancelRequest(RequestId requestId)
{
    if (!inFacadeThread() || !worker_) {
        return {false, makeError(ErrorCategory::InternalState,
                                 ErrorCode::WorkerUnavailable)};
    }
    if (!requests_.contains(requestId.value)
        || cancellationRequested_.contains(requestId.value)) {
        return {false, makeError(ErrorCategory::InternalState,
                                 ErrorCode::RequestNotFoundOrCompleted)};
    }
    cancellationRequested_.insert(requestId.value);
    QMetaObject::invokeMethod(worker_,
                              [worker = worker_, requestId] {
                                  if (worker) {
                                      worker->cancel(requestId);
                                  }
                              },
                              Qt::QueuedConnection);
    return {true, std::nullopt};
}

void QSerialPortModbusClient::onWorkerConnectionStateChanged(ConnectionState state)
{
    setConnectionState(state);
    if (state == ConnectionState::Connected) {
        acceptingRequests_ = owner_ != CommunicationOwner::None;
    } else if (state == ConnectionState::Faulted
               || state == ConnectionState::Disconnected) {
        acceptingRequests_ = false;
        owner_ = CommunicationOwner::None;
        ownershipTransition_ = false;
    }
}

void QSerialPortModbusClient::onWorkerControlCompleted(const ControlResult &result)
{
    if (result.kind == ControlKind::Open && result.succeeded) {
        acceptingRequests_ = false;
    }
    if (result.kind == ControlKind::Close && result.succeeded) {
        owner_ = CommunicationOwner::None;
        acceptingRequests_ = false;
        ownershipTransition_ = false;
    }
    emit controlCompleted(result);
}

void QSerialPortModbusClient::onWorkerOwnershipChanged(const OwnershipResult &result)
{
    ownershipTransition_ = false;
    if (result.succeeded) {
        owner_ = result.owner;
    }
    acceptingRequests_ = result.succeeded
        && owner_ != CommunicationOwner::None
        && state_ == ConnectionState::Connected;
    emit ownershipChanged(result);
}

void QSerialPortModbusClient::onWorkerRequestStateChanged(RequestId id,
                                                          RequestState state)
{
    const auto iterator = requests_.find(id.value);
    if (iterator == requests_.end()) {
        return;
    }
    if (*iterator == RequestState::Queued && state != RequestState::Queued) {
        --queuedCount_;
    }
    *iterator = state;
    emit requestStateChanged(id, state);
}

void QSerialPortModbusClient::onWorkerRequestCompleted(
    const ModbusRequestResult &result)
{
    const auto iterator = requests_.find(result.requestId.value);
    if (iterator == requests_.end()) {
        return;
    }
    if (*iterator == RequestState::Queued) {
        --queuedCount_;
    }
    requests_.erase(iterator);
    cancellationRequested_.remove(result.requestId.value);
    emit requestCompleted(result);
}

} // namespace oms555tv::communication

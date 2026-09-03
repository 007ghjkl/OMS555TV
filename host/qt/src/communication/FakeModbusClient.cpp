#include "communication/FakeModbusClient.h"

#include "communication/RtuCodec.h"

#include <QPointer>

#include <algorithm>
#include <type_traits>
#include <utility>

namespace oms555tv::communication {
namespace {

CommunicationError makeError(ErrorCategory category,
                             ErrorCode code,
                             ConnectionState state,
                             CommunicationOwner owner)
{
    CommunicationError error;
    error.category = category;
    error.code = code;
    error.connectionState = state;
    error.owner = owner;
    return error;
}

QByteArray readResponse(quint8 serverAddress, const QVector<quint16> &values)
{
    QByteArray response;
    response.append(static_cast<char>(serverAddress));
    response.append(static_cast<char>(readHoldingRegistersFunction));
    response.append(static_cast<char>(values.size() * 2));
    for (const quint16 value : values) {
        response.append(static_cast<char>((value >> 8U) & 0xFFU));
        response.append(static_cast<char>(value & 0xFFU));
    }
    return appendModbusCrc(std::move(response));
}

QByteArray exceptionResponse(quint8 serverAddress, quint8 function, quint8 exceptionCode)
{
    QByteArray response;
    response.append(static_cast<char>(serverAddress));
    response.append(static_cast<char>(function | 0x80U));
    response.append(static_cast<char>(exceptionCode));
    return appendModbusCrc(std::move(response));
}

quint8 descriptorFunction(const RequestDescriptor &descriptor) noexcept
{
    return std::holds_alternative<ReadRequestDescriptor>(descriptor)
        ? readHoldingRegistersFunction
        : writeSingleRegisterFunction;
}

bool descriptorsEqual(const RequestDescriptor &left, const RequestDescriptor &right) noexcept
{
    if (left.index() != right.index()) {
        return false;
    }
    if (const auto *leftRead = std::get_if<ReadRequestDescriptor>(&left)) {
        const auto &rightRead = std::get<ReadRequestDescriptor>(right);
        return leftRead->startAddress == rightRead.startAddress
            && leftRead->count == rightRead.count;
    }
    const auto &leftWrite = std::get<WriteRequestDescriptor>(left);
    const auto &rightWrite = std::get<WriteRequestDescriptor>(right);
    return leftWrite.address == rightWrite.address
        && leftWrite.rawValue == rightWrite.rawValue;
}

} // namespace

FakeModbusClient::FakeModbusClient(std::shared_ptr<ManualScheduler> scheduler,
                                   QDateTime utcEpoch,
                                   QObject *parent)
    : IModbusClient(parent)
    , scheduler_(std::move(scheduler))
    , utcEpoch_(std::move(utcEpoch))
{
    Q_ASSERT(scheduler_);
}

ConnectionState FakeModbusClient::connectionState() const noexcept
{
    return state_;
}

CommunicationOwner FakeModbusClient::activeOwner() const noexcept
{
    return owner_;
}

QDateTime FakeModbusClient::utcNow() const
{
    return utcEpoch_.addMSecs(
        std::chrono::duration_cast<std::chrono::milliseconds>(scheduler_->now()).count());
}

ControlSubmission FakeModbusClient::rejectControl(ErrorCategory category, ErrorCode code) const
{
    return {std::nullopt, makeError(category, code, state_, owner_)};
}

RequestSubmission FakeModbusClient::rejectRequest(ErrorCategory category, ErrorCode code) const
{
    return {std::nullopt, makeError(category, code, state_, owner_)};
}

OperationId FakeModbusClient::allocateOperationId()
{
    return operationIds_.next().value_or(OperationId{});
}

RequestId FakeModbusClient::allocateRequestId()
{
    return requestIds_.next().value_or(RequestId{});
}

ControlSubmission FakeModbusClient::open(const ModbusConnectionConfig &config)
{
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
    const OperationId id = allocateOperationId();
    if (id.value == 0) {
        return rejectControl(ErrorCategory::InternalState, ErrorCode::IdExhausted);
    }
    config_ = config;
    state_ = ConnectionState::Opening;
    emit connectionStateChanged(state_);
    QPointer<FakeModbusClient> self(this);
    (void)scheduler_->scheduleAfter(std::chrono::nanoseconds::zero(), [self, id] {
        if (!self) {
            return;
        }
        self->state_ = ConnectionState::Connected;
        self->owner_ = CommunicationOwner::None;
        emit self->connectionStateChanged(self->state_);
        emit self->controlCompleted({id, ControlKind::Open, true, std::nullopt});
    });
    return {id, std::nullopt};
}

ControlSubmission FakeModbusClient::close(CloseMode mode)
{
    if (state_ != ConnectionState::Connected && state_ != ConnectionState::Faulted) {
        return rejectControl(ErrorCategory::InternalState, ErrorCode::InvalidState);
    }
    if (ownershipTransition_ || closeTransition_) {
        return rejectControl(ErrorCategory::Queue, ErrorCode::NotAcceptingRequests);
    }
    const OperationId id = allocateOperationId();
    if (id.value == 0) {
        return rejectControl(ErrorCategory::InternalState, ErrorCode::IdExhausted);
    }
    state_ = ConnectionState::Closing;
    closeTransition_ = CloseTransition{id, mode};
    emit connectionStateChanged(state_);

    while (!queue_.empty()) {
        PendingRequest request = std::move(queue_.front());
        queue_.pop_front();
        completeQueuedCancelled(std::move(request), ErrorCode::CancelledByClose);
    }
    if (active_ && mode == CloseMode::CancelAll) {
        completeActiveCancelled(ErrorCode::CancelledByClose);
    }
    maybeCompleteControlTransition();
    return {id, std::nullopt};
}

ControlSubmission FakeModbusClient::acquireOwnership(CommunicationOwner owner,
                                                      HandoffMode mode)
{
    if (state_ != ConnectionState::Connected) {
        return rejectControl(ErrorCategory::Connection, ErrorCode::NotConnected);
    }
    if (owner == CommunicationOwner::None) {
        return rejectControl(ErrorCategory::Ownership, ErrorCode::OwnerMismatch);
    }
    if (ownershipTransition_ || closeTransition_) {
        return rejectControl(ErrorCategory::Ownership,
                             ErrorCode::OwnershipTransitionInProgress);
    }
    const OperationId id = allocateOperationId();
    if (id.value == 0) {
        return rejectControl(ErrorCategory::InternalState, ErrorCode::IdExhausted);
    }
    if (owner_ == CommunicationOwner::None || owner_ == owner) {
        scheduleOwnershipCompletion(id, owner_, owner);
        return {id, std::nullopt};
    }

    ownershipTransition_ = OwnershipTransition{id, owner_, owner, mode};
    cancelQueuedByOwner(owner_, ErrorCode::CancelledByHandoff);
    if (active_ && active_->options.owner == owner_
        && mode == HandoffMode::CancelInFlight) {
        completeActiveCancelled(ErrorCode::CancelledByHandoff);
    }
    maybeCompleteControlTransition();
    return {id, std::nullopt};
}

ControlSubmission FakeModbusClient::releaseOwnership(CommunicationOwner owner,
                                                      HandoffMode mode)
{
    if (state_ != ConnectionState::Connected) {
        return rejectControl(ErrorCategory::Connection, ErrorCode::NotConnected);
    }
    if (owner == CommunicationOwner::None || owner != owner_) {
        return rejectControl(ErrorCategory::Ownership, ErrorCode::OwnerMismatch);
    }
    if (ownershipTransition_ || closeTransition_) {
        return rejectControl(ErrorCategory::Ownership,
                             ErrorCode::OwnershipTransitionInProgress);
    }
    const OperationId id = allocateOperationId();
    if (id.value == 0) {
        return rejectControl(ErrorCategory::InternalState, ErrorCode::IdExhausted);
    }
    ownershipTransition_ = OwnershipTransition{id, owner_, CommunicationOwner::None, mode};
    cancelQueuedByOwner(owner_, ErrorCode::CancelledByHandoff);
    if (active_ && mode == HandoffMode::CancelInFlight) {
        completeActiveCancelled(ErrorCode::CancelledByHandoff);
    }
    maybeCompleteControlTransition();
    return {id, std::nullopt};
}

RequestSubmission FakeModbusClient::readHoldingRegisters(device::PduAddress startAddress,
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

RequestSubmission FakeModbusClient::writeSingleRegister(device::PduAddress address,
                                                         quint16 rawValue,
                                                         const RequestOptions &options)
{
    return submit(WriteRequestDescriptor{address, rawValue}, options);
}

RequestSubmission FakeModbusClient::submit(RequestDescriptor descriptor,
                                            const RequestOptions &options)
{
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
    if (ownershipTransition_ || closeTransition_) {
        return rejectRequest(ErrorCategory::Queue, ErrorCode::NotAcceptingRequests);
    }
    if (options.owner != owner_ || owner_ == CommunicationOwner::None) {
        return rejectRequest(ErrorCategory::Ownership, ErrorCode::OwnerMismatch);
    }
    if (static_cast<qsizetype>(queue_.size()) >= config_.maxPendingRequests) {
        return rejectRequest(ErrorCategory::Queue, ErrorCode::QueueFull);
    }
    const RequestId id = allocateRequestId();
    if (id.value == 0) {
        return rejectRequest(ErrorCategory::InternalState, ErrorCode::IdExhausted);
    }

    const auto timeout = options.responseTimeout.value_or(config_.defaultResponseTimeout);
    RtuTransactionEvidence evidence;
    evidence.requestId = id;
    evidence.owner = options.owner;
    evidence.serverAddress = config_.serverAddress;
    evidence.functionCode = descriptorFunction(descriptor);
    evidence.enqueuedUtc = utcNow();
    queue_.push_back({id,
                      std::move(descriptor),
                      options,
                      timeout,
                      scheduler_->now(),
                      std::nullopt,
                      std::move(evidence)});
    emit requestStateChanged(id, RequestState::Queued);
    QPointer<FakeModbusClient> self(this);
    (void)scheduler_->scheduleAfter(std::chrono::nanoseconds::zero(), [self] {
        if (self) {
            self->startNext();
        }
    });
    return {id, std::nullopt};
}

bool FakeModbusClient::matcherMatches(const RequestMatcher &matcher,
                                      const PendingRequest &request) const
{
    return matcher.owner == request.options.owner
        && matcher.responseTimeout == request.timeout
        && descriptorsEqual(matcher.descriptor, request.descriptor);
}

void FakeModbusClient::startNext()
{
    if (active_ || queue_.empty() || state_ != ConnectionState::Connected
        || ownershipTransition_ || closeTransition_) {
        return;
    }
    active_ = std::move(queue_.front());
    queue_.pop_front();
    active_->evidence.txStartedUtc = utcNow();
    active_->evidence.txAdu = encodeRequestAdu(config_.serverAddress, active_->descriptor);
    active_->evidence.txCrcStatus = CrcStatus::Valid;
    active_->txAcceptedAt = scheduler_->now();
    active_->evidence.txAcceptedUtc = utcNow();
    active_->evidence.queueDelay = scheduler_->now() - active_->enqueuedAt;
    emit requestStateChanged(active_->id, RequestState::InFlight);

    if (script_.empty()) {
        completeActiveError(makeError(ErrorCategory::InternalState,
                                      ErrorCode::FakeScriptMismatch,
                                      state_,
                                      owner_));
        return;
    }
    FakeStep step = std::move(script_.front());
    script_.pop_front();
    if (!matcherMatches(step.expectedRequest, *active_)
        || (!step.expectedTxAdu.isEmpty()
            && step.expectedTxAdu != active_->evidence.txAdu)) {
        completeActiveError(makeError(ErrorCategory::InternalState,
                                      ErrorCode::FakeScriptMismatch,
                                      state_,
                                      owner_));
        return;
    }
    executeStep(std::move(step));
}

void FakeModbusClient::executeStep(FakeStep step)
{
    if (!active_ || step.outcome.kind == FakeOutcomeKind::Pending) {
        return;
    }
    const bool isRead = std::holds_alternative<ReadRequestDescriptor>(active_->descriptor);
    if ((step.outcome.kind == FakeOutcomeKind::ReadSuccess && !isRead)
        || (step.outcome.kind == FakeOutcomeKind::WriteSuccess && isRead)) {
        completeActiveError(makeError(ErrorCategory::InternalState,
                                      ErrorCode::FakeScriptMismatch,
                                      state_, owner_));
        return;
    }
    const RequestId expectedId = active_->id;
    auto delay = std::chrono::duration_cast<std::chrono::nanoseconds>(step.virtualDelay);
    if (step.outcome.kind == FakeOutcomeKind::Timeout && delay.count() == 0) {
        delay = std::chrono::duration_cast<std::chrono::nanoseconds>(active_->timeout);
    }
    QPointer<FakeModbusClient> self(this);
    (void)scheduler_->scheduleAfter(delay, [self, expectedId, step = std::move(step)]() mutable {
        if (!self || !self->active_ || self->active_->id != expectedId) {
            return;
        }
        QByteArray response = std::move(step.rxAdu);
        const quint8 server = self->config_.serverAddress;
        const quint8 function = descriptorFunction(self->active_->descriptor);
        const auto scriptMismatch = [&self] {
            self->completeActiveError(makeError(ErrorCategory::InternalState,
                                                ErrorCode::FakeScriptMismatch,
                                                self->state_, self->owner_));
        };
        switch (step.outcome.kind) {
        case FakeOutcomeKind::ReadSuccess:
            if (response.isEmpty()) {
                response = readResponse(server, step.outcome.readValues);
            }
            self->completeActiveSuccess(step.outcome.readValues, response);
            break;
        case FakeOutcomeKind::WriteSuccess:
            if (response.isEmpty()) {
                response = self->active_->evidence.txAdu;
            }
            self->completeActiveSuccess({}, response);
            break;
        case FakeOutcomeKind::RemoteException:
            if (response.isEmpty()) {
                response = exceptionResponse(server, function, step.outcome.exceptionCode);
            }
            if (const auto decoded = decodeResponseAdu(
                    server, self->active_->descriptor, response, true);
                decoded.error
                && decoded.error->category == ErrorCategory::RemoteException) {
                self->completeActiveError(*decoded.error, response);
            } else {
                scriptMismatch();
            }
            break;
        case FakeOutcomeKind::Timeout:
            self->completeActiveError(makeError(ErrorCategory::Timeout,
                                                ErrorCode::ResponseTimeout,
                                                self->state_, self->owner_),
                                      response);
            break;
        case FakeOutcomeKind::CrcMismatch:
            if (response.isEmpty()) {
                if (const auto *read = std::get_if<ReadRequestDescriptor>(
                        &self->active_->descriptor)) {
                    QVector<quint16> values(read->count, 0);
                    response = readResponse(server, values);
                } else {
                    response = self->active_->evidence.txAdu;
                }
                response[response.size() - 1] = static_cast<char>(
                    static_cast<quint8>(response.back()) ^ 0x01U);
            }
            if (const auto decoded = decodeResponseAdu(
                    server, self->active_->descriptor, response, true);
                decoded.error && decoded.error->category == ErrorCategory::Crc) {
                self->completeActiveError(*decoded.error, response);
            } else {
                scriptMismatch();
            }
            break;
        case FakeOutcomeKind::ProtocolError:
            if (response.isEmpty()) {
                if (const auto *read = std::get_if<ReadRequestDescriptor>(
                        &self->active_->descriptor)) {
                    QVector<quint16> values(read->count, 0);
                    response = readResponse(static_cast<quint8>(server + 1U), values);
                } else {
                    response = self->active_->evidence.txAdu;
                    response[0] = static_cast<char>(server + 1U);
                    response = appendModbusCrc(response.first(response.size() - 2));
                }
            }
            if (const auto decoded = decodeResponseAdu(
                    server, self->active_->descriptor, response, true);
                decoded.error && decoded.error->category == ErrorCategory::Protocol) {
                self->completeActiveError(*decoded.error, response);
            } else {
                scriptMismatch();
            }
            break;
        case FakeOutcomeKind::SerialError:
            self->completeActiveError(makeError(ErrorCategory::Serial,
                                                step.outcome.serialError,
                                                self->state_, self->owner_),
                                      response);
            break;
        case FakeOutcomeKind::Pending:
            break;
        }
    });
}

void FakeModbusClient::completeActiveSuccess(const QVector<quint16> &readValues,
                                              const QByteArray &rxAdu)
{
    if (!active_) {
        return;
    }
    const auto decoded = decodeResponseAdu(config_.serverAddress,
                                           active_->descriptor,
                                           rxAdu,
                                           true);
    if (decoded.status != RtuDecodeStatus::Success) {
        completeActiveError(decoded.error.value_or(
                                makeError(ErrorCategory::InternalState,
                                          ErrorCode::FakeScriptMismatch,
                                          state_, owner_)),
                            rxAdu);
        return;
    }
    PendingRequest request = std::move(*active_);
    active_.reset();
    request.evidence.rxAdu = rxAdu;
    request.evidence.firstRxUtc = utcNow();
    request.evidence.rxCrcStatus = decoded.crcStatus;
    request.evidence.receivedRxCrc = decoded.receivedCrc;
    request.evidence.calculatedRxCrc = decoded.calculatedCrc;

    std::optional<SuccessfulRequestResult> success;
    if (const auto *read = std::get_if<ReadRequestDescriptor>(&request.descriptor)) {
        success = ReadHoldingRegistersResult{
            request.id,
            read->startAddress,
            decoded.readValues.isEmpty() ? readValues : decoded.readValues,
            request.evidence,
        };
    } else {
        const auto &write = std::get<WriteRequestDescriptor>(request.descriptor);
        success = WriteSingleRegisterResult{
            request.id, write.address, write.rawValue, request.evidence};
    }
    finalize(std::move(request), RequestState::Succeeded, std::move(success), std::nullopt);
    maybeCompleteControlTransition();
    startNext();
}

void FakeModbusClient::completeActiveError(CommunicationError error,
                                            const QByteArray &rxAdu)
{
    if (!active_) {
        return;
    }
    PendingRequest request = std::move(*active_);
    active_.reset();
    request.evidence.rxAdu = rxAdu;
    if (!rxAdu.isEmpty()) {
        request.evidence.firstRxUtc = utcNow();
    }
    const auto decoded = decodeResponseAdu(config_.serverAddress,
                                           request.descriptor,
                                           rxAdu,
                                           true);
    request.evidence.rxCrcStatus = decoded.crcStatus;
    request.evidence.receivedRxCrc = decoded.receivedCrc;
    request.evidence.calculatedRxCrc = decoded.calculatedCrc;
    finalize(std::move(request), RequestState::Failed, std::nullopt, std::move(error));
    maybeCompleteControlTransition();
    startNext();
}

void FakeModbusClient::completeActiveCancelled(ErrorCode code)
{
    if (!active_) {
        return;
    }
    PendingRequest request = std::move(*active_);
    active_.reset();
    auto error = makeError(ErrorCategory::Cancelled, code, state_, owner_);
    finalize(std::move(request), RequestState::Cancelled, std::nullopt, std::move(error));
    maybeCompleteControlTransition();
    startNext();
}

void FakeModbusClient::completeQueuedCancelled(PendingRequest request, ErrorCode code)
{
    auto error = makeError(ErrorCategory::Cancelled, code, state_, owner_);
    finalize(std::move(request), RequestState::Cancelled, std::nullopt, std::move(error));
}

void FakeModbusClient::finalize(PendingRequest request,
                                RequestState state,
                                std::optional<SuccessfulRequestResult> success,
                                std::optional<CommunicationError> error)
{
    request.evidence.completedUtc = utcNow();
    if (request.txAcceptedAt) {
        request.evidence.rtt = scheduler_->now() - *request.txAcceptedAt;
    }
    if (success) {
        std::visit([&request](auto &value) { value.evidence = request.evidence; }, *success);
    }
    if (error) {
        error->requestId = request.id;
        error->requestState = state;
        error->connectionState = state_;
        error->owner = request.options.owner;
        error->functionCode = descriptorFunction(request.descriptor);
        error->evidence = request.evidence;
    }
    ModbusRequestResult result{
        request.id,
        state,
        request.descriptor,
        std::move(success),
        std::move(error),
        request.evidence,
    };
    emit requestStateChanged(request.id, state);
    emit requestCompleted(result);
}

void FakeModbusClient::cancelQueuedByOwner(CommunicationOwner owner, ErrorCode code)
{
    for (auto iterator = queue_.begin(); iterator != queue_.end();) {
        if (iterator->options.owner != owner) {
            ++iterator;
            continue;
        }
        PendingRequest request = std::move(*iterator);
        iterator = queue_.erase(iterator);
        completeQueuedCancelled(std::move(request), code);
    }
}

CommandAcceptance FakeModbusClient::cancelRequest(RequestId requestId)
{
    for (auto iterator = queue_.begin(); iterator != queue_.end(); ++iterator) {
        if (iterator->id == requestId) {
            PendingRequest request = std::move(*iterator);
            queue_.erase(iterator);
            completeQueuedCancelled(std::move(request), ErrorCode::CancelledByCaller);
            return {true, std::nullopt};
        }
    }
    if (active_ && active_->id == requestId) {
        completeActiveCancelled(ErrorCode::CancelledByCaller);
        return {true, std::nullopt};
    }
    return {false,
            makeError(ErrorCategory::InternalState,
                      ErrorCode::RequestNotFoundOrCompleted,
                      state_, owner_)};
}

void FakeModbusClient::maybeCompleteControlTransition()
{
    if (active_) {
        return;
    }
    if (closeTransition_) {
        finishClose();
        return;
    }
    if (ownershipTransition_) {
        const auto transition = *ownershipTransition_;
        ownershipTransition_.reset();
        scheduleOwnershipCompletion(transition.operationId,
                                    transition.previousOwner,
                                    transition.targetOwner);
    }
}

void FakeModbusClient::scheduleOwnershipCompletion(OperationId operationId,
                                                    CommunicationOwner previous,
                                                    CommunicationOwner target)
{
    QPointer<FakeModbusClient> self(this);
    (void)scheduler_->scheduleAfter(std::chrono::nanoseconds::zero(),
                                   [self, operationId, previous, target] {
        if (!self) {
            return;
        }
        self->owner_ = target;
        emit self->ownershipChanged(
            {operationId, previous, target, true, std::nullopt});
        emit self->controlCompleted(
            {operationId,
             target == CommunicationOwner::None
                 ? ControlKind::ReleaseOwnership
                 : ControlKind::AcquireOwnership,
             true,
             std::nullopt});
        self->startNext();
    });
}

void FakeModbusClient::finishClose()
{
    if (!closeTransition_ || active_) {
        return;
    }
    const OperationId id = closeTransition_->operationId;
    closeTransition_.reset();
    QPointer<FakeModbusClient> self(this);
    (void)scheduler_->scheduleAfter(std::chrono::nanoseconds::zero(), [self, id] {
        if (!self) {
            return;
        }
        self->owner_ = CommunicationOwner::None;
        self->state_ = ConnectionState::Disconnected;
        emit self->connectionStateChanged(self->state_);
        emit self->controlCompleted({id, ControlKind::Close, true, std::nullopt});
    });
}

void FakeModbusClient::enqueueStep(FakeStep step)
{
    script_.push_back(std::move(step));
}

bool FakeModbusClient::scriptConsumed() const noexcept
{
    return script_.empty();
}

bool FakeModbusClient::hasNonTerminalRequests() const noexcept
{
    return active_.has_value() || !queue_.empty();
}

QString FakeModbusClient::verificationError() const
{
    if (!script_.empty()) {
        return QStringLiteral("Fake 脚本仍有 %1 个步骤未消费").arg(script_.size());
    }
    if (active_) {
        return QStringLiteral("Fake 仍有在途请求 %1").arg(active_->id.value);
    }
    if (!queue_.empty()) {
        return QStringLiteral("Fake 仍有 %1 个排队请求").arg(queue_.size());
    }
    return {};
}

} // namespace oms555tv::communication

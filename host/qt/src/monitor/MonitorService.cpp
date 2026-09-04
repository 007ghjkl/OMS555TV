#include "monitor/MonitorService.h"

#include "device/RegisterCodec.h"
#include "device/RegisterMap.h"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace oms555tv::monitor {
namespace {

MonitorError basicError(MonitorErrorCode code, QString diagnostic = {})
{
    MonitorError error;
    error.code = code;
    error.diagnostic = std::move(diagnostic);
    return error;
}

} // namespace

MonitorService::MonitorService(communication::IModbusClient &client,
                               IMonitorScheduler &scheduler,
                               QObject *parent)
    : QObject(parent)
    , client_(client)
    , scheduler_(scheduler)
{
    qRegisterMetaType<PollBatchId>();
    qRegisterMetaType<DeviceHealth>();
    qRegisterMetaType<MonitorError>();
    qRegisterMetaType<CommunicationStatistics>();
    qRegisterMetaType<MonitoringSnapshot>();
    qRegisterMetaType<PollBatchResult>();
    connect(&client_, &communication::IModbusClient::requestCompleted,
            this, &MonitorService::handleRequestCompleted);
}

bool MonitorService::isRunning() const noexcept
{
    return running_;
}

bool MonitorService::isStopping() const noexcept
{
    return stopping_;
}

DeviceHealth MonitorService::health() const noexcept
{
    return health_;
}

const CommunicationStatistics &MonitorService::statistics() const noexcept
{
    return statistics_;
}

const std::optional<MonitoringSnapshot> &MonitorService::lastSnapshot() const noexcept
{
    return lastSnapshot_;
}

MonitorCommandAcceptance MonitorService::start(const MonitorConfig &config)
{
    if (running_ || stopping_) {
        return {false, basicError(MonitorErrorCode::AlreadyRunning)};
    }
    if (const auto error = validateMonitorConfig(config)) {
        return {false, error};
    }
    if (client_.connectionState() != communication::ConnectionState::Connected) {
        return {false, basicError(MonitorErrorCode::BackendUnavailable)};
    }
    if (client_.activeOwner() != communication::CommunicationOwner::Monitor) {
        return {false, basicError(MonitorErrorCode::OwnerMismatch)};
    }

    config_ = config;
    running_ = true;
    stopping_ = false;
    ++generation_;
    if (generation_ == 0) {
        ++generation_;
    }
    previousBatchStart_.reset();
    setHealth(DeviceHealth::Unknown);
    scheduleNextBatch(std::chrono::nanoseconds::zero());
    return {true, std::nullopt};
}

MonitorCommandAcceptance MonitorService::stop()
{
    if (!running_ || stopping_) {
        return {false, basicError(MonitorErrorCode::NotRunning)};
    }

    running_ = false;
    stopping_ = true;
    ++generation_;
    if (scheduledTask_) {
        scheduler_.cancel(*scheduledTask_);
        scheduledTask_.reset();
    }

    if (!activeBatch_) {
        finishStop();
        return {true, std::nullopt};
    }
    if (!activeBatch_->activeRequestId) {
        finalizeStoppedBatch();
        finishStop();
        return {true, std::nullopt};
    }

    const communication::RequestId requestId = *activeBatch_->activeRequestId;
    const auto acceptance = client_.cancelRequest(requestId);
    if (!acceptance.isAccepted && activeBatch_
        && activeBatch_->activeRequestId == requestId) {
        MonitorError error = basicError(
            MonitorErrorCode::Stopped,
            QStringLiteral("停止时请求已不再可取消"));
        error.batchId = activeBatch_->id;
        error.requestId = requestId;
        error.communicationError = acceptance.rejection;
        finalizeStoppedBatch();
        finishStop();
    }
    return {true, std::nullopt};
}

void MonitorService::abortBackend()
{
    running_ = false;
    stopping_ = false;
    ++generation_;
    if (scheduledTask_) {
        scheduler_.cancel(*scheduledTask_);
        scheduledTask_.reset();
    }
    if (activeBatch_) {
        MonitorError error = basicError(MonitorErrorCode::BackendUnavailable);
        error.batchId = activeBatch_->id;
        error.blockIndex = activeBatch_->nextBlockIndex;
        finalizeFailedBatch(std::move(error));
    }
    setHealth(DeviceHealth::Offline);
}

void MonitorService::scheduleNextBatch(std::chrono::nanoseconds delay)
{
    if (!running_ || stopping_) {
        return;
    }
    const quint64 generation = generation_;
    scheduledTask_ = scheduler_.scheduleAfter(delay, this, [this, generation] {
        scheduledTask_.reset();
        beginBatch(generation);
    });
}

void MonitorService::beginBatch(quint64 generation)
{
    if (!running_ || stopping_ || generation != generation_ || activeBatch_) {
        return;
    }
    if (client_.connectionState() != communication::ConnectionState::Connected
        || client_.activeOwner() != communication::CommunicationOwner::Monitor) {
        MonitorError error = basicError(MonitorErrorCode::BackendUnavailable);
        emit errorOccurred(error);
        abortBackend();
        return;
    }
    if (nextBatchId_ == 0) {
        MonitorError error = basicError(MonitorErrorCode::InvariantViolation,
                                        QStringLiteral("轮询批次 ID 已耗尽"));
        emit errorOccurred(error);
        abortBackend();
        return;
    }

    const auto now = scheduler_.monotonicNow();
    ActiveBatch batch;
    batch.id = PollBatchId{nextBatchId_++};
    batch.startedUtc = scheduler_.utcNow();
    batch.startedMonotonic = now;
    if (previousBatchStart_) {
        batch.effectiveInterval = now - *previousBatchStart_;
        statistics_.recentEffectiveInterval = batch.effectiveInterval;
    }
    previousBatchStart_ = now;
    activeBatch_ = std::move(batch);
    detail::saturatingIncrement(statistics_.batchesStarted);
    emit statisticsChanged(statistics_);
    submitNextBlock();
}

void MonitorService::submitNextBlock()
{
    if (!activeBatch_ || stopping_ || !running_) {
        return;
    }
    if (activeBatch_->nextBlockIndex < 0
        || activeBatch_->nextBlockIndex
            >= static_cast<qsizetype>(device::deviceSnapshotReadBlocks.size())) {
        finalizeFailedBatch(basicError(MonitorErrorCode::InvariantViolation));
        return;
    }

    const qsizetype index = activeBatch_->nextBlockIndex;
    const auto definition = device::deviceSnapshotReadBlocks[static_cast<std::size_t>(index)];
    communication::RequestOptions options;
    options.owner = communication::CommunicationOwner::Monitor;
    options.responseTimeout = config_.requestTimeout;
    options.correlationId = QStringLiteral("monitor/%1").arg(activeBatch_->id.value);
    const auto submission = client_.readHoldingRegisters(
        definition.startAddress, definition.count, options);
    if (!submission.accepted()) {
        MonitorError error = basicError(MonitorErrorCode::RequestRejected);
        error.batchId = activeBatch_->id;
        error.blockIndex = index;
        error.blockAddress = definition.startAddress;
        error.communicationError = submission.rejection;
        finalizeFailedBatch(std::move(error));
        return;
    }

    activeBatch_->activeRequestId = *submission.requestId;
    activeBatch_->requestIds.append(*submission.requestId);
    detail::saturatingIncrement(statistics_.requests);
    emit statisticsChanged(statistics_);
}

void MonitorService::handleRequestCompleted(
    const communication::ModbusRequestResult &result)
{
    if (!activeBatch_ || !activeBatch_->activeRequestId
        || result.requestId != *activeBatch_->activeRequestId) {
        return;
    }

    activeBatch_->requestResults.append(result);
    activeBatch_->activeRequestId.reset();
    updateRequestStatistics(result);

    if (stopping_) {
        finalizeStoppedBatch();
        finishStop();
        return;
    }
    if (!running_) {
        return;
    }

    const qsizetype index = activeBatch_->nextBlockIndex;
    const auto definition = device::deviceSnapshotReadBlocks[static_cast<std::size_t>(index)];
    if (result.state != communication::RequestState::Succeeded || !result.success) {
        MonitorError error = basicError(MonitorErrorCode::RequestFailed);
        error.batchId = activeBatch_->id;
        error.blockIndex = index;
        error.blockAddress = definition.startAddress;
        error.requestId = result.requestId;
        error.communicationError = result.error;
        finalizeFailedBatch(std::move(error));
        return;
    }

    const auto *descriptor = std::get_if<communication::ReadRequestDescriptor>(
        &result.descriptor);
    const auto *success = std::get_if<communication::ReadHoldingRegistersResult>(
        &*result.success);
    if (descriptor == nullptr || success == nullptr
        || descriptor->startAddress != definition.startAddress
        || descriptor->count != definition.count
        || success->startAddress != definition.startAddress
        || success->values.size() != definition.count) {
        MonitorError error = basicError(MonitorErrorCode::ResultMismatch);
        error.batchId = activeBatch_->id;
        error.blockIndex = index;
        error.blockAddress = definition.startAddress;
        error.requestId = result.requestId;
        finalizeFailedBatch(std::move(error));
        return;
    }

    activeBatch_->blocks.append(device::RegisterBlock{
        definition.startAddress,
        success->values,
    });
    ++activeBatch_->nextBlockIndex;
    if (activeBatch_->nextBlockIndex
        < static_cast<qsizetype>(device::deviceSnapshotReadBlocks.size())) {
        submitNextBlock();
        return;
    }

    const auto decoded = device::decodeDeviceSnapshot(activeBatch_->blocks);
    if (const auto *codecError = std::get_if<device::CodecError>(&decoded)) {
        MonitorError error = basicError(MonitorErrorCode::DecodeFailed);
        error.batchId = activeBatch_->id;
        error.codecError = *codecError;
        finalizeFailedBatch(std::move(error));
        return;
    }
    finalizeSuccessfulBatch(std::get<device::DeviceSnapshot>(decoded));
}

void MonitorService::updateRequestStatistics(
    const communication::ModbusRequestResult &result)
{
    if (result.state == communication::RequestState::Succeeded) {
        detail::saturatingIncrement(statistics_.succeeded);
        statistics_.consecutiveRequestFailures = 0;
    } else if (result.state == communication::RequestState::Cancelled) {
        detail::saturatingIncrement(statistics_.cancelled);
        if (!stopping_) {
            detail::saturatingIncrement(statistics_.failed);
            detail::saturatingIncrement(statistics_.consecutiveRequestFailures);
        }
    } else {
        detail::saturatingIncrement(statistics_.failed);
        detail::saturatingIncrement(statistics_.consecutiveRequestFailures);
        if (result.error
            && result.error->category == communication::ErrorCategory::Timeout) {
            detail::saturatingIncrement(statistics_.timedOut);
        }
    }

    if (result.evidence.rtt) {
        const auto rtt = std::max(*result.evidence.rtt,
                                  std::chrono::nanoseconds::zero());
        statistics_.recentRtt = rtt;
        if (!statistics_.minimumRtt || rtt < *statistics_.minimumRtt) {
            statistics_.minimumRtt = rtt;
        }
        if (!statistics_.maximumRtt || rtt > *statistics_.maximumRtt) {
            statistics_.maximumRtt = rtt;
        }
        detail::saturatingIncrement(statistics_.rttSamples);
        detail::saturatingAdd(statistics_.totalRttNanoseconds,
                              static_cast<quint64>(rtt.count()));
    }
    emit statisticsChanged(statistics_);
}

PollBatchResult MonitorService::takeBatchResult()
{
    PollBatchResult result;
    if (!activeBatch_) {
        return result;
    }
    const auto now = scheduler_.monotonicNow();
    result.batchId = activeBatch_->id;
    result.startedUtc = activeBatch_->startedUtc;
    result.completedUtc = scheduler_.utcNow();
    result.requestIds = activeBatch_->requestIds;
    result.requestResults = activeBatch_->requestResults;
    result.duration = now - activeBatch_->startedMonotonic;
    result.effectiveInterval = activeBatch_->effectiveInterval;
    activeBatch_.reset();
    return result;
}

void MonitorService::finalizeSuccessfulBatch(device::DeviceSnapshot snapshot)
{
    PollBatchResult result = takeBatchResult();
    result.complete = true;
    result.snapshot = snapshot;
    result.overrun = result.duration
        >= std::chrono::duration_cast<std::chrono::nanoseconds>(config_.targetPeriod);

    detail::saturatingIncrement(statistics_.batchesSucceeded);
    statistics_.consecutiveBatchFailures = 0;
    statistics_.recentBatchDuration = result.duration;
    if (result.overrun) {
        detail::saturatingIncrement(statistics_.overruns);
    }

    MonitoringSnapshot published;
    published.batchId = result.batchId;
    published.startedUtc = result.startedUtc;
    published.completedUtc = result.completedUtc;
    published.lastSuccessfulUtc = result.completedUtc;
    published.requestIds = result.requestIds;
    published.snapshot = std::move(snapshot);
    lastSnapshot_ = published;
    setHealth(DeviceHealth::Online);
    emit statisticsChanged(statistics_);
    emit batchCompleted(result);
    emit snapshotPublished(*lastSnapshot_);
    scheduleAfterCompletedBatch(result.duration);
}

void MonitorService::finalizeFailedBatch(MonitorError error)
{
    PollBatchResult result = takeBatchResult();
    if (error.batchId == std::nullopt && result.batchId.value != 0) {
        error.batchId = result.batchId;
    }
    result.error = error;
    result.overrun = result.duration
        >= std::chrono::duration_cast<std::chrono::nanoseconds>(config_.targetPeriod);

    detail::saturatingIncrement(statistics_.batchesFailed);
    detail::saturatingIncrement(statistics_.consecutiveBatchFailures);
    statistics_.recentBatchDuration = result.duration;
    if (result.overrun) {
        detail::saturatingIncrement(statistics_.overruns);
    }
    setHealth(statistics_.consecutiveBatchFailures >= 3
                  ? DeviceHealth::Offline
                  : DeviceHealth::Degraded);
    emit statisticsChanged(statistics_);
    emit errorOccurred(error);
    emit batchCompleted(result);
    scheduleAfterCompletedBatch(result.duration);
}

void MonitorService::finalizeStoppedBatch()
{
    if (!activeBatch_) {
        return;
    }
    PollBatchResult result = takeBatchResult();
    result.stopped = true;
    MonitorError error = basicError(MonitorErrorCode::Stopped);
    error.batchId = result.batchId;
    result.error = error;
    emit batchCompleted(result);
}

void MonitorService::scheduleAfterCompletedBatch(std::chrono::nanoseconds duration)
{
    if (!running_ || stopping_) {
        return;
    }
    const auto target = std::chrono::duration_cast<std::chrono::nanoseconds>(
        config_.targetPeriod);
    scheduleNextBatch(duration < target ? target - duration
                                        : std::chrono::nanoseconds::zero());
}

void MonitorService::setHealth(DeviceHealth health)
{
    if (health_ == health) {
        return;
    }
    health_ = health;
    emit healthChanged(health_);
}

void MonitorService::finishStop()
{
    if (!stopping_) {
        return;
    }
    stopping_ = false;
    setHealth(DeviceHealth::Unknown);
    emit stopped();
}

} // namespace oms555tv::monitor

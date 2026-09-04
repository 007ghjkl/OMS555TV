#include "configuration/ConfigurationService.h"

#include "device/RegisterCodec.h"
#include "device/RegisterMap.h"

#include <limits>
#include <variant>

namespace oms555tv::configuration {
namespace {

constexpr std::array<device::TemperatureChannel, 4> channels{
    device::TemperatureChannel::PhaseA,
    device::TemperatureChannel::PhaseB,
    device::TemperatureChannel::PhaseC,
    device::TemperatureChannel::Ambient,
};

ConfigurationError makeError(ConfigurationErrorCode code, QString diagnostic = {})
{
    ConfigurationError error;
    error.code = code;
    error.diagnostic = std::move(diagnostic);
    return error;
}

bool isSuccessful(const communication::ModbusRequestResult &result)
{
    return result.state == communication::RequestState::Succeeded
        && result.success.has_value() && !result.error.has_value();
}

} // namespace

ConfigurationService::ConfigurationService(app::AppStateController &controller,
                                           communication::IModbusClient &client,
                                           QObject *parent)
    : QObject(parent)
    , controller_(controller)
    , client_(client)
{
    qRegisterMetaType<ConfigurationState>();
    qRegisterMetaType<ConfigurationOperationResult>();
    connect(&client_, &communication::IModbusClient::controlCompleted,
            this, &ConfigurationService::handleControlCompleted);
    connect(&client_, &communication::IModbusClient::requestCompleted,
            this, &ConfigurationService::handleRequestCompleted);
}

ConfigurationState ConfigurationService::state() const noexcept
{
    return state_;
}

bool ConfigurationService::busy() const noexcept
{
    return pending_.has_value();
}

const std::optional<device::AlarmThresholds> &ConfigurationService::thresholds() const noexcept
{
    return thresholds_;
}

const std::optional<ConfigurationOperationResult> &ConfigurationService::lastResult() const noexcept
{
    return lastResult_;
}

std::optional<ConfigurationError> ConfigurationService::validateReady() const
{
    if (pending_) {
        return makeError(ConfigurationErrorCode::Busy, QStringLiteral("配置操作正在进行"));
    }
    if (controller_.state() != app::AppState::ConnectedIdle
        || controller_.commandInProgress()
        || client_.connectionState() != communication::ConnectionState::Connected
        || client_.activeOwner() != communication::CommunicationOwner::None) {
        return makeError(ConfigurationErrorCode::InvalidState,
                         QStringLiteral("仅允许在已连接空闲且无通信 owner 时配置"));
    }
    return std::nullopt;
}

std::optional<ConfigurationOperationId> ConfigurationService::allocateId() noexcept
{
    if (nextOperationId_ == 0) {
        return std::nullopt;
    }
    const ConfigurationOperationId id{nextOperationId_};
    nextOperationId_ = nextOperationId_ == std::numeric_limits<quint64>::max()
        ? 0 : nextOperationId_ + 1;
    return id;
}

ConfigurationSubmission ConfigurationService::readThresholds()
{
    return begin(ConfigurationCommand::ReadThresholds, nullptr);
}

ConfigurationSubmission ConfigurationService::writeThresholds(const ThresholdValues &values)
{
    return begin(ConfigurationCommand::WriteThresholds, &values);
}

ConfigurationSubmission ConfigurationService::begin(ConfigurationCommand command,
                                                     const ThresholdValues *values)
{
    if (const auto error = validateReady()) {
        return {std::nullopt, error};
    }

    PendingOperation operation;
    operation.result.command = command;
    if (command == ConfigurationCommand::WriteThresholds) {
        if (!values) {
            return {std::nullopt, makeError(ConfigurationErrorCode::InvariantViolation)};
        }
        for (int index = 0; index < static_cast<int>(channels.size()); ++index) {
            const auto encoded = device::encodeAlarmThreshold(channels[index], (*values)[index]);
            if (const auto *codecError = std::get_if<device::CodecError>(&encoded)) {
                ConfigurationError error = makeError(
                    ConfigurationErrorCode::InvalidInput,
                    QStringLiteral("阈值输入未通过范围或精度校验"));
                error.channel = channels[index];
                error.codecError = *codecError;
                return {std::nullopt, std::move(error)};
            }
            operation.writes[index] = std::get<device::RegisterWrite>(encoded);
        }
    }

    const auto id = allocateId();
    if (!id) {
        return {std::nullopt, makeError(ConfigurationErrorCode::IdExhausted)};
    }
    operation.result.operationId = *id;

    const auto acquire = client_.acquireOwnership(
        communication::CommunicationOwner::ManualDebug,
        communication::HandoffMode::FinishInFlight);
    if (!acquire.accepted()) {
        ConfigurationError error = makeError(ConfigurationErrorCode::OwnerRejected);
        error.communicationError = acquire.rejection;
        return {std::nullopt, std::move(error)};
    }
    operation.controlOperationId = acquire.operationId;
    pending_ = std::move(operation);
    setState(ConfigurationState::AcquiringOwner);
    return {*id, std::nullopt};
}

bool ConfigurationService::cancel()
{
    if (!pending_ || state_ == ConfigurationState::ReleasingOwner) {
        return false;
    }
    const bool wasAcquiring = state_ == ConfigurationState::AcquiringOwner;
    pending_->cancelRequested = true;
    setState(ConfigurationState::Cancelling);
    if (pending_->requestId) {
        const auto acceptance = client_.cancelRequest(*pending_->requestId);
        if (!acceptance.isAccepted) {
            pending_->requestId.reset();
            markRemainingCancelled();
            beginRelease();
        }
    } else if (!wasAcquiring) {
        markRemainingCancelled();
        beginRelease();
    }
    return true;
}

void ConfigurationService::setState(ConfigurationState state)
{
    if (state_ == state) {
        return;
    }
    state_ = state;
    emit stateChanged(state_);
}

void ConfigurationService::handleControlCompleted(
    const communication::ControlResult &result)
{
    if (!pending_ || !pending_->controlOperationId
        || result.operationId != *pending_->controlOperationId) {
        return;
    }
    pending_->controlOperationId.reset();

    if (state_ == ConfigurationState::AcquiringOwner
        || state_ == ConfigurationState::Cancelling) {
        if (!result.succeeded
            || client_.activeOwner() != communication::CommunicationOwner::ManualDebug) {
            if (pending_->cancelRequested
                && client_.activeOwner() == communication::CommunicationOwner::None) {
                markRemainingCancelled();
                completeAfterRelease();
                return;
            }
            ConfigurationError error = makeError(ConfigurationErrorCode::OwnerRejected);
            error.communicationError = result.error;
            if (client_.activeOwner() == communication::CommunicationOwner::ManualDebug) {
                pending_->result.error = error;
                pending_->result.succeeded = false;
                emit errorOccurred(error);
                beginRelease();
            } else {
                completeWithError(std::move(error));
            }
            return;
        }
        if (pending_->cancelRequested) {
            markRemainingCancelled();
            beginRelease();
        } else {
            submitInitialRead();
        }
        return;
    }

    if (state_ == ConfigurationState::ReleasingOwner) {
        if (!result.succeeded
            || client_.activeOwner() != communication::CommunicationOwner::None) {
            ConfigurationError error = makeError(
                ConfigurationErrorCode::ReleaseFailed,
                QStringLiteral("ManualDebug owner 释放失败"));
            error.communicationError = result.error;
            pending_->result.error = error;
            pending_->result.succeeded = false;
            emit errorOccurred(error);
        }
        completeAfterRelease();
    }
}

void ConfigurationService::submitInitialRead()
{
    if (!pending_) {
        return;
    }
    communication::RequestOptions options;
    options.owner = communication::CommunicationOwner::ManualDebug;
    options.correlationId = QStringLiteral("configuration/%1/initial-read")
        .arg(pending_->result.operationId.value);
    const auto submission = client_.readHoldingRegisters(
        device::thresholdsReadBlock.startAddress,
        device::thresholdsReadBlock.count,
        options);
    if (!submission.accepted()) {
        ConfigurationError error = makeError(ConfigurationErrorCode::InitialReadRejected);
        error.communicationError = submission.rejection;
        pending_->result.error = error;
        emit errorOccurred(error);
        beginRelease();
        return;
    }
    pending_->requestId = submission.requestId;
    setState(ConfigurationState::ReadingCurrent);
}

void ConfigurationService::handleRequestCompleted(
    const communication::ModbusRequestResult &result)
{
    if (!pending_ || !pending_->requestId || result.requestId != *pending_->requestId) {
        return;
    }
    pending_->requestId.reset();

    if (pending_->cancelRequested) {
        markRemainingCancelled();
        beginRelease();
        return;
    }

    if (state_ == ConfigurationState::ReadingCurrent) {
        if (!isSuccessful(result)
            || !std::holds_alternative<communication::ReadHoldingRegistersResult>(
                *result.success)) {
            ConfigurationError error = requestFailure(
                ConfigurationErrorCode::InitialReadFailed, result);
            pending_->result.error = error;
            emit errorOccurred(error);
            beginRelease();
            return;
        }
        const auto &read = std::get<communication::ReadHoldingRegistersResult>(*result.success);
        if (read.startAddress != device::thresholdsReadBlock.startAddress
            || read.values.size() != device::thresholdsReadBlock.count) {
            ConfigurationError error = makeError(ConfigurationErrorCode::ResultMismatch);
            pending_->result.error = error;
            emit errorOccurred(error);
            beginRelease();
            return;
        }
        const device::RegisterBlock block{read.startAddress, read.values};
        const auto decoded = device::decodeThresholds(block);
        if (const auto *codecError = std::get_if<device::CodecError>(&decoded)) {
            ConfigurationError error = makeError(ConfigurationErrorCode::InitialReadFailed);
            error.codecError = *codecError;
            pending_->result.error = error;
            emit errorOccurred(error);
            beginRelease();
            return;
        }
        const auto before = std::get<device::AlarmThresholds>(decoded);
        pending_->beforeRaw = read.values;
        pending_->result.before = before;
        pending_->result.verifiedThresholds = before;
        thresholds_ = before;
        emit thresholdsChanged(*thresholds_);

        if (pending_->result.command == ConfigurationCommand::ReadThresholds) {
            pending_->result.succeeded = true;
            beginRelease();
            return;
        }
        pending_->result.items.clear();
        for (int index = 0; index < static_cast<int>(channels.size()); ++index) {
            pending_->result.items.push_back(ConfigurationItemResult{
                channels[index], ConfigurationItemStatus::Pending,
                thresholdForChannel(before, channels[index]),
                pending_->writes[index]->value,
            });
        }
        pending_->itemIndex = 0;
        submitWrite();
        return;
    }

    if (pending_->itemIndex < 0
        || pending_->itemIndex >= pending_->result.items.size()) {
        ConfigurationError error = makeError(ConfigurationErrorCode::InvariantViolation);
        pending_->result.error = error;
        pending_->result.succeeded = false;
        emit errorOccurred(error);
        beginRelease();
        return;
    }
    auto &item = pending_->result.items[pending_->itemIndex];

    if (state_ == ConfigurationState::Writing) {
        item.writeRequestId = result.requestId;
        if (!isSuccessful(result)
            || !std::holds_alternative<communication::WriteSingleRegisterResult>(
                *result.success)) {
            item.status = ConfigurationItemStatus::WriteFailed;
            item.error = requestFailure(ConfigurationErrorCode::WriteFailed,
                                        result, item.channel);
            item.error->writeRequestId = result.requestId;
            advanceItem();
            return;
        }
        const auto &write = std::get<communication::WriteSingleRegisterResult>(*result.success);
        const auto &expected = *pending_->writes[pending_->itemIndex];
        if (write.address != expected.address || write.rawValue != expected.rawValue) {
            item.status = ConfigurationItemStatus::WriteFailed;
            item.error = makeError(ConfigurationErrorCode::ResultMismatch);
            item.error->channel = item.channel;
            item.error->writeRequestId = result.requestId;
            advanceItem();
            return;
        }
        submitReadback();
        return;
    }

    if (state_ == ConfigurationState::Verifying) {
        item.readbackRequestId = result.requestId;
        if (!isSuccessful(result)
            || !std::holds_alternative<communication::ReadHoldingRegistersResult>(
                *result.success)) {
            item.status = ConfigurationItemStatus::ReadbackFailed;
            item.error = requestFailure(ConfigurationErrorCode::ReadbackFailed,
                                        result, item.channel);
            item.error->writeRequestId = item.writeRequestId;
            item.error->readbackRequestId = result.requestId;
            advanceItem();
            return;
        }
        const auto &read = std::get<communication::ReadHoldingRegistersResult>(*result.success);
        const auto &expected = *pending_->writes[pending_->itemIndex];
        if (read.startAddress != expected.address || read.values.size() != 1) {
            item.status = ConfigurationItemStatus::ReadbackFailed;
            item.error = makeError(ConfigurationErrorCode::ResultMismatch);
            item.error->channel = item.channel;
            item.error->readbackRequestId = result.requestId;
            advanceItem();
            return;
        }
        QVector<quint16> candidate = pending_->beforeRaw;
        candidate[pending_->itemIndex] = read.values.front();
        const auto decoded = device::decodeThresholds(
            {device::thresholdsReadBlock.startAddress, candidate});
        if (const auto *codecError = std::get_if<device::CodecError>(&decoded)) {
            item.status = ConfigurationItemStatus::ReadbackFailed;
            item.error = makeError(ConfigurationErrorCode::ReadbackFailed);
            item.error->channel = item.channel;
            item.error->codecError = *codecError;
            advanceItem();
            return;
        }
        const auto actual = thresholdForChannel(
            std::get<device::AlarmThresholds>(decoded), item.channel);
        item.actual = actual;
        if (actual.deciCelsius != item.expected.deciCelsius) {
            item.status = ConfigurationItemStatus::ReadbackMismatch;
            item.error = makeError(ConfigurationErrorCode::ReadbackMismatch);
            item.error->channel = item.channel;
            item.error->expected = item.expected;
            item.error->actual = actual;
            item.error->writeRequestId = item.writeRequestId;
            item.error->readbackRequestId = item.readbackRequestId;
        } else {
            item.status = ConfigurationItemStatus::Succeeded;
            setThresholdForChannel(*pending_->result.verifiedThresholds,
                                   item.channel, actual);
            thresholds_ = pending_->result.verifiedThresholds;
            emit thresholdsChanged(*thresholds_);
        }
        advanceItem();
    }
}

void ConfigurationService::submitWrite()
{
    if (!pending_ || pending_->itemIndex >= static_cast<int>(channels.size())) {
        beginRelease();
        return;
    }
    const auto &write = *pending_->writes[pending_->itemIndex];
    communication::RequestOptions options;
    options.owner = communication::CommunicationOwner::ManualDebug;
    options.correlationId = QStringLiteral("configuration/%1/%2/write")
        .arg(pending_->result.operationId.value).arg(pending_->itemIndex);
    const auto submission = client_.writeSingleRegister(write.address,
                                                        write.rawValue, options);
    if (!submission.accepted()) {
        auto &item = pending_->result.items[pending_->itemIndex];
        item.status = ConfigurationItemStatus::WriteFailed;
        item.error = makeError(ConfigurationErrorCode::WriteRejected);
        item.error->channel = item.channel;
        item.error->communicationError = submission.rejection;
        advanceItem();
        return;
    }
    pending_->requestId = submission.requestId;
    setState(ConfigurationState::Writing);
}

void ConfigurationService::submitReadback()
{
    if (!pending_) {
        return;
    }
    const auto &write = *pending_->writes[pending_->itemIndex];
    communication::RequestOptions options;
    options.owner = communication::CommunicationOwner::ManualDebug;
    options.correlationId = QStringLiteral("configuration/%1/%2/readback")
        .arg(pending_->result.operationId.value).arg(pending_->itemIndex);
    const auto submission = client_.readHoldingRegisters(write.address, 1, options);
    if (!submission.accepted()) {
        auto &item = pending_->result.items[pending_->itemIndex];
        item.status = ConfigurationItemStatus::ReadbackFailed;
        item.error = makeError(ConfigurationErrorCode::ReadbackRejected);
        item.error->channel = item.channel;
        item.error->writeRequestId = item.writeRequestId;
        item.error->communicationError = submission.rejection;
        advanceItem();
        return;
    }
    pending_->requestId = submission.requestId;
    setState(ConfigurationState::Verifying);
}

void ConfigurationService::advanceItem()
{
    if (!pending_) {
        return;
    }
    ++pending_->itemIndex;
    if (pending_->cancelRequested) {
        markRemainingCancelled();
        beginRelease();
    } else if (pending_->itemIndex < static_cast<int>(channels.size())) {
        submitWrite();
    } else {
        pending_->result.succeeded = true;
        for (const auto &item : pending_->result.items) {
            if (item.status != ConfigurationItemStatus::Succeeded) {
                pending_->result.succeeded = false;
                break;
            }
        }
        beginRelease();
    }
}

void ConfigurationService::markRemainingCancelled()
{
    if (!pending_) {
        return;
    }
    pending_->result.cancelled = true;
    pending_->result.succeeded = false;
    if (pending_->result.command == ConfigurationCommand::WriteThresholds) {
        for (int index = pending_->itemIndex;
             index < pending_->result.items.size(); ++index) {
            if (pending_->result.items[index].status == ConfigurationItemStatus::Pending) {
                pending_->result.items[index].status = ConfigurationItemStatus::Cancelled;
            }
        }
    }
    pending_->result.error = makeError(ConfigurationErrorCode::Cancelled);
}

void ConfigurationService::beginRelease()
{
    if (!pending_) {
        return;
    }
    pending_->requestId.reset();
    if (client_.activeOwner() != communication::CommunicationOwner::ManualDebug) {
        if (client_.activeOwner() == communication::CommunicationOwner::None) {
            completeAfterRelease();
        } else {
            completeWithError(makeError(ConfigurationErrorCode::ReleaseFailed));
        }
        return;
    }
    const auto release = client_.releaseOwnership(
        communication::CommunicationOwner::ManualDebug,
        communication::HandoffMode::CancelInFlight);
    if (!release.accepted()) {
        ConfigurationError error = makeError(ConfigurationErrorCode::ReleaseFailed);
        error.communicationError = release.rejection;
        pending_->result.error = error;
        pending_->result.succeeded = false;
        emit errorOccurred(error);
        completeAfterRelease();
        return;
    }
    pending_->controlOperationId = release.operationId;
    setState(ConfigurationState::ReleasingOwner);
}

void ConfigurationService::completeAfterRelease()
{
    if (!pending_) {
        return;
    }
    lastResult_ = pending_->result;
    pending_.reset();
    setState(ConfigurationState::Idle);
    emit operationCompleted(*lastResult_);
}

void ConfigurationService::completeWithError(ConfigurationError error)
{
    if (!pending_) {
        return;
    }
    pending_->result.succeeded = false;
    pending_->result.error = error;
    lastResult_ = pending_->result;
    pending_.reset();
    setState(ConfigurationState::Idle);
    emit errorOccurred(error);
    emit operationCompleted(*lastResult_);
}

ConfigurationError ConfigurationService::requestFailure(
    ConfigurationErrorCode code,
    const communication::ModbusRequestResult &result,
    std::optional<device::TemperatureChannel> channel) const
{
    ConfigurationError error = makeError(code);
    error.channel = channel;
    error.communicationError = result.error;
    return error;
}

device::Temperature ConfigurationService::thresholdForChannel(
    const device::AlarmThresholds &thresholds,
    device::TemperatureChannel channel)
{
    switch (channel) {
    case device::TemperatureChannel::PhaseA: return thresholds.phaseA;
    case device::TemperatureChannel::PhaseB: return thresholds.phaseB;
    case device::TemperatureChannel::PhaseC: return thresholds.phaseC;
    case device::TemperatureChannel::Ambient: return thresholds.ambient;
    }
    return {};
}

void ConfigurationService::setThresholdForChannel(
    device::AlarmThresholds &thresholds,
    device::TemperatureChannel channel,
    device::Temperature value)
{
    switch (channel) {
    case device::TemperatureChannel::PhaseA: thresholds.phaseA = value; break;
    case device::TemperatureChannel::PhaseB: thresholds.phaseB = value; break;
    case device::TemperatureChannel::PhaseC: thresholds.phaseC = value; break;
    case device::TemperatureChannel::Ambient: thresholds.ambient = value; break;
    }
}

} // namespace oms555tv::configuration

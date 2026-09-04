#include "app/AppStateController.h"

#include <limits>
#include <utility>

namespace oms555tv::app {
namespace {

AppError makeError(AppErrorCode code,
                   AppCommand command,
                   AppState state,
                   QString diagnostic = {})
{
    AppError error;
    error.code = code;
    error.command = command;
    error.state = state;
    error.diagnostic = std::move(diagnostic);
    return error;
}

} // namespace

AppStateController::AppStateController(communication::IModbusClient &client,
                                       monitor::MonitorService &monitorService,
                                       QObject *parent)
    : QObject(parent)
    , client_(client)
    , monitorService_(monitorService)
{
    qRegisterMetaType<AppOperationId>();
    qRegisterMetaType<AppState>();
    qRegisterMetaType<AppCommand>();
    qRegisterMetaType<AppError>();
    qRegisterMetaType<AppCommandResult>();
    connect(&client_, &communication::IModbusClient::controlCompleted,
            this, &AppStateController::handleControlCompleted);
    connect(&client_, &communication::IModbusClient::connectionStateChanged,
            this, &AppStateController::handleConnectionStateChanged);
    connect(&monitorService_, &monitor::MonitorService::stopped,
            this, &AppStateController::handleMonitorStopped);
    connect(&monitorService_, &monitor::MonitorService::errorOccurred,
            this, &AppStateController::handleMonitorError);

    if (client_.connectionState() == communication::ConnectionState::Connected
        && client_.activeOwner() == communication::CommunicationOwner::None) {
        state_ = AppState::ConnectedIdle;
    } else if (client_.connectionState() != communication::ConnectionState::Disconnected) {
        state_ = AppState::Error;
    }
}

AppState AppStateController::state() const noexcept
{
    return state_;
}

bool AppStateController::commandInProgress() const noexcept
{
    return pending_.has_value();
}

communication::ConnectionState AppStateController::connectionState() const noexcept
{
    return client_.connectionState();
}

AppCommandSubmission AppStateController::reject(AppCommand command,
                                                AppErrorCode code,
                                                QString diagnostic) const
{
    return {std::nullopt, makeError(code, command, state_, std::move(diagnostic))};
}

std::optional<AppOperationId> AppStateController::allocateOperationId() noexcept
{
    if (nextOperationId_ == 0) {
        return std::nullopt;
    }
    const AppOperationId id{nextOperationId_};
    nextOperationId_ = nextOperationId_ == std::numeric_limits<quint64>::max()
        ? 0
        : nextOperationId_ + 1;
    return id;
}

AppCommandSubmission AppStateController::connectDevice(
    const communication::ModbusConnectionConfig &config)
{
    if (pending_) {
        return reject(AppCommand::Connect, AppErrorCode::CommandInProgress);
    }
    if (state_ != AppState::Disconnected
        || client_.connectionState() != communication::ConnectionState::Disconnected) {
        return reject(AppCommand::Connect, AppErrorCode::InvalidState);
    }
    const auto appId = allocateOperationId();
    if (!appId) {
        return reject(AppCommand::Connect, AppErrorCode::IdExhausted);
    }
    const auto submission = client_.open(config);
    if (!submission.accepted()) {
        AppError error = makeError(AppErrorCode::BackendRejected,
                                   AppCommand::Connect, state_);
        error.communicationError = submission.rejection;
        return {std::nullopt, std::move(error)};
    }
    pending_ = PendingCommand{*appId, AppCommand::Connect, Stage::WaitingOpen,
                              submission.operationId};
    return {*appId, std::nullopt};
}

AppCommandSubmission AppStateController::disconnectDevice()
{
    if (pending_) {
        return reject(AppCommand::Disconnect, AppErrorCode::CommandInProgress);
    }
    if (state_ != AppState::ConnectedIdle && state_ != AppState::Monitoring
        && state_ != AppState::Testing) {
        return reject(AppCommand::Disconnect, AppErrorCode::InvalidState);
    }
    const auto appId = allocateOperationId();
    if (!appId) {
        return reject(AppCommand::Disconnect, AppErrorCode::IdExhausted);
    }

    if (state_ == AppState::Monitoring) {
        pending_ = PendingCommand{*appId, AppCommand::Disconnect,
                                  Stage::WaitingMonitorStop};
        setState(AppState::Stopping);
        const auto acceptance = monitorService_.stop();
        if (!acceptance.accepted && pending_) {
            AppError error = makeError(AppErrorCode::MonitorRejected,
                                       AppCommand::Disconnect, state_);
            error.monitorError = acceptance.rejection;
            failPending(std::move(error));
        }
        return {*appId, std::nullopt};
    }

    setState(AppState::Stopping);
    if (state_ == AppState::Stopping
        && client_.activeOwner() == communication::CommunicationOwner::Testing) {
        const auto release = client_.releaseOwnership(
            communication::CommunicationOwner::Testing,
            communication::HandoffMode::CancelInFlight);
        if (!release.accepted()) {
            AppError error = makeError(AppErrorCode::BackendRejected,
                                       AppCommand::Disconnect, state_);
            error.communicationError = release.rejection;
            setState(AppState::Testing);
            return {std::nullopt, std::move(error)};
        }
        pending_ = PendingCommand{*appId, AppCommand::Disconnect,
                                  Stage::WaitingReleaseTesting,
                                  release.operationId};
        return {*appId, std::nullopt};
    }

    const auto close = client_.close(communication::CloseMode::CancelAll);
    if (!close.accepted()) {
        AppError error = makeError(AppErrorCode::BackendRejected,
                                   AppCommand::Disconnect, state_);
        error.communicationError = close.rejection;
        setState(AppState::ConnectedIdle);
        return {std::nullopt, std::move(error)};
    }
    pending_ = PendingCommand{*appId, AppCommand::Disconnect,
                              Stage::WaitingClose, close.operationId};
    return {*appId, std::nullopt};
}

AppCommandSubmission AppStateController::startMonitoring(
    const monitor::MonitorConfig &config)
{
    if (pending_) {
        return reject(AppCommand::StartMonitoring, AppErrorCode::CommandInProgress);
    }
    if (state_ != AppState::ConnectedIdle
        || client_.connectionState() != communication::ConnectionState::Connected
        || client_.activeOwner() != communication::CommunicationOwner::None) {
        return reject(AppCommand::StartMonitoring, AppErrorCode::InvalidState);
    }
    if (const auto monitorError = monitor::validateMonitorConfig(config)) {
        AppError error = makeError(AppErrorCode::MonitorRejected,
                                   AppCommand::StartMonitoring, state_);
        error.monitorError = monitorError;
        return {std::nullopt, std::move(error)};
    }
    const auto appId = allocateOperationId();
    if (!appId) {
        return reject(AppCommand::StartMonitoring, AppErrorCode::IdExhausted);
    }
    const auto acquire = client_.acquireOwnership(
        communication::CommunicationOwner::Monitor,
        communication::HandoffMode::FinishInFlight);
    if (!acquire.accepted()) {
        AppError error = makeError(AppErrorCode::BackendRejected,
                                   AppCommand::StartMonitoring, state_);
        error.communicationError = acquire.rejection;
        return {std::nullopt, std::move(error)};
    }
    PendingCommand pending{*appId, AppCommand::StartMonitoring,
                           Stage::WaitingAcquireMonitor, acquire.operationId};
    pending.monitorConfig = config;
    pending_ = std::move(pending);
    return {*appId, std::nullopt};
}

AppCommandSubmission AppStateController::stopMonitoring()
{
    if (pending_) {
        return reject(AppCommand::StopMonitoring, AppErrorCode::CommandInProgress);
    }
    if (state_ != AppState::Monitoring) {
        return reject(AppCommand::StopMonitoring, AppErrorCode::InvalidState);
    }
    const auto appId = allocateOperationId();
    if (!appId) {
        return reject(AppCommand::StopMonitoring, AppErrorCode::IdExhausted);
    }
    pending_ = PendingCommand{*appId, AppCommand::StopMonitoring,
                              Stage::WaitingMonitorStop};
    setState(AppState::Stopping);
    const auto acceptance = monitorService_.stop();
    if (!acceptance.accepted && pending_) {
        AppError error = makeError(AppErrorCode::MonitorRejected,
                                   AppCommand::StopMonitoring, state_);
        error.monitorError = acceptance.rejection;
        failPending(std::move(error));
    }
    return {*appId, std::nullopt};
}

AppCommandSubmission AppStateController::startTesting()
{
    if (pending_) {
        return reject(AppCommand::StartTesting, AppErrorCode::CommandInProgress);
    }
    if (state_ != AppState::ConnectedIdle
        || client_.connectionState() != communication::ConnectionState::Connected
        || client_.activeOwner() != communication::CommunicationOwner::None) {
        return reject(AppCommand::StartTesting, AppErrorCode::InvalidState);
    }
    const auto appId = allocateOperationId();
    if (!appId) {
        return reject(AppCommand::StartTesting, AppErrorCode::IdExhausted);
    }
    const auto acquire = client_.acquireOwnership(
        communication::CommunicationOwner::Testing,
        communication::HandoffMode::FinishInFlight);
    if (!acquire.accepted()) {
        AppError error = makeError(AppErrorCode::BackendRejected,
                                   AppCommand::StartTesting, state_);
        error.communicationError = acquire.rejection;
        return {std::nullopt, std::move(error)};
    }
    pending_ = PendingCommand{*appId, AppCommand::StartTesting,
                              Stage::WaitingAcquireTesting,
                              acquire.operationId};
    return {*appId, std::nullopt};
}

AppCommandSubmission AppStateController::stopTesting()
{
    if (pending_) {
        return reject(AppCommand::StopTesting, AppErrorCode::CommandInProgress);
    }
    if (state_ != AppState::Testing
        || client_.activeOwner() != communication::CommunicationOwner::Testing) {
        return reject(AppCommand::StopTesting, AppErrorCode::InvalidState);
    }
    const auto appId = allocateOperationId();
    if (!appId) {
        return reject(AppCommand::StopTesting, AppErrorCode::IdExhausted);
    }
    setState(AppState::Stopping);
    const auto release = client_.releaseOwnership(
        communication::CommunicationOwner::Testing,
        communication::HandoffMode::CancelInFlight);
    if (!release.accepted()) {
        AppError error = makeError(AppErrorCode::BackendRejected,
                                   AppCommand::StopTesting, state_);
        error.communicationError = release.rejection;
        setState(AppState::Testing);
        return {std::nullopt, std::move(error)};
    }
    pending_ = PendingCommand{*appId, AppCommand::StopTesting,
                              Stage::WaitingReleaseTesting,
                              release.operationId};
    return {*appId, std::nullopt};
}

AppCommandSubmission AppStateController::recover()
{
    if (pending_) {
        return reject(AppCommand::Recover, AppErrorCode::CommandInProgress);
    }
    if (state_ != AppState::Error) {
        return reject(AppCommand::Recover, AppErrorCode::InvalidState);
    }
    const auto appId = allocateOperationId();
    if (!appId) {
        return reject(AppCommand::Recover, AppErrorCode::IdExhausted);
    }
    if (client_.connectionState() == communication::ConnectionState::Disconnected) {
        setState(AppState::Disconnected);
        emit commandCompleted({*appId, AppCommand::Recover, true, std::nullopt});
        return {*appId, std::nullopt};
    }
    const auto close = client_.close(communication::CloseMode::CancelAll);
    if (!close.accepted()) {
        AppError error = makeError(AppErrorCode::BackendRejected,
                                   AppCommand::Recover, state_);
        error.communicationError = close.rejection;
        return {std::nullopt, std::move(error)};
    }
    pending_ = PendingCommand{*appId, AppCommand::Recover,
                              Stage::WaitingClose, close.operationId};
    setState(AppState::Stopping);
    return {*appId, std::nullopt};
}

void AppStateController::setState(AppState state)
{
    if (state_ == state) {
        return;
    }
    state_ = state;
    emit stateChanged(state_);
}

void AppStateController::handleControlCompleted(
    const communication::ControlResult &result)
{
    if (!pending_ || !pending_->communicationOperationId
        || result.operationId != *pending_->communicationOperationId) {
        return;
    }
    if (!result.succeeded) {
        failPending(communicationFailure(AppErrorCode::BackendOperationFailed, result));
        return;
    }

    switch (pending_->stage) {
    case Stage::WaitingOpen:
        if (client_.connectionState() != communication::ConnectionState::Connected
            || client_.activeOwner() != communication::CommunicationOwner::None) {
            failPending(communicationFailure(AppErrorCode::BackendStateMismatch, result));
            return;
        }
        completePendingSuccess(AppState::ConnectedIdle);
        break;
    case Stage::WaitingClose:
        if (client_.connectionState() != communication::ConnectionState::Disconnected) {
            failPending(communicationFailure(AppErrorCode::BackendStateMismatch, result));
            return;
        }
        completePendingSuccess(AppState::Disconnected);
        break;
    case Stage::WaitingAcquireMonitor: {
        if (client_.activeOwner() != communication::CommunicationOwner::Monitor
            || !pending_->monitorConfig) {
            failPending(communicationFailure(AppErrorCode::BackendStateMismatch, result));
            return;
        }
        const auto acceptance = monitorService_.start(*pending_->monitorConfig);
        if (!acceptance.accepted) {
            AppError error = makeError(AppErrorCode::MonitorRejected,
                                       pending_->command, state_);
            error.monitorError = acceptance.rejection;
            const auto release = client_.releaseOwnership(
                communication::CommunicationOwner::Monitor,
                communication::HandoffMode::CancelInFlight);
            if (!release.accepted()) {
                failPending(std::move(error));
                return;
            }
            pending_->stage = Stage::WaitingRollbackMonitorRelease;
            pending_->communicationOperationId = release.operationId;
            pending_->deferredError = std::move(error);
            return;
        }
        completePendingSuccess(AppState::Monitoring);
        break;
    }
    case Stage::WaitingReleaseMonitor:
        if (client_.activeOwner() != communication::CommunicationOwner::None) {
            failPending(communicationFailure(AppErrorCode::BackendStateMismatch, result));
            return;
        }
        if (pending_->command == AppCommand::Disconnect) {
            closeBackend();
        } else {
            completePendingSuccess(AppState::ConnectedIdle);
        }
        break;
    case Stage::WaitingAcquireTesting:
        if (client_.activeOwner() != communication::CommunicationOwner::Testing) {
            failPending(communicationFailure(AppErrorCode::BackendStateMismatch, result));
            return;
        }
        completePendingSuccess(AppState::Testing);
        break;
    case Stage::WaitingReleaseTesting:
        if (client_.activeOwner() != communication::CommunicationOwner::None) {
            failPending(communicationFailure(AppErrorCode::BackendStateMismatch, result));
            return;
        }
        if (pending_->command == AppCommand::Disconnect) {
            closeBackend();
        } else {
            completePendingSuccess(AppState::ConnectedIdle);
        }
        break;
    case Stage::WaitingRollbackMonitorRelease: {
        AppError error = pending_->deferredError.value_or(
            makeError(AppErrorCode::InvariantViolation, pending_->command, state_));
        failPending(std::move(error));
        break;
    }
    case Stage::WaitingMonitorStop:
        failPending(communicationFailure(AppErrorCode::InvariantViolation, result));
        break;
    }
}

void AppStateController::handleConnectionStateChanged(
    communication::ConnectionState connectionState)
{
    if (connectionState == communication::ConnectionState::Faulted) {
        monitorService_.abortBackend();
        AppCommand command = pending_ ? pending_->command : AppCommand::Disconnect;
        AppError error = makeError(AppErrorCode::BackendStateMismatch, command, state_,
                                   QStringLiteral("通信后端进入 Faulted"));
        enterUnexpectedError(std::move(error));
        return;
    }
    if (connectionState == communication::ConnectionState::Disconnected) {
        if (state_ == AppState::Disconnected
            || (pending_ && pending_->stage == Stage::WaitingClose)) {
            return;
        }
        monitorService_.abortBackend();
        AppCommand command = pending_ ? pending_->command : AppCommand::Disconnect;
        AppError error = makeError(AppErrorCode::BackendStateMismatch, command, state_,
                                   QStringLiteral("通信后端意外断开"));
        enterUnexpectedError(std::move(error));
        return;
    }
    if (connectionState == communication::ConnectionState::Connected
        && state_ == AppState::Disconnected && !pending_) {
        AppError error = makeError(AppErrorCode::BackendStateMismatch,
                                   AppCommand::Connect, state_,
                                   QStringLiteral("通信后端在无连接命令时变为 Connected"));
        enterUnexpectedError(std::move(error));
    }
}

void AppStateController::handleMonitorStopped()
{
    if (!pending_ || pending_->stage != Stage::WaitingMonitorStop) {
        return;
    }
    releaseMonitorOwner();
}

void AppStateController::handleMonitorError(const monitor::MonitorError &monitorError)
{
    if (state_ != AppState::Monitoring
        || (monitorError.code != monitor::MonitorErrorCode::BackendUnavailable
            && monitorError.code != monitor::MonitorErrorCode::OwnerMismatch
            && monitorError.code != monitor::MonitorErrorCode::InvariantViolation)) {
        return;
    }
    monitorService_.abortBackend();
    AppError error = makeError(AppErrorCode::BackendStateMismatch,
                               AppCommand::StopMonitoring, state_,
                               QStringLiteral("监控核心检测到不可恢复的不变量错误"));
    error.monitorError = monitorError;
    enterUnexpectedError(std::move(error));
}

void AppStateController::releaseMonitorOwner()
{
    if (!pending_) {
        return;
    }
    const auto release = client_.releaseOwnership(
        communication::CommunicationOwner::Monitor,
        communication::HandoffMode::FinishInFlight);
    if (!release.accepted()) {
        AppError error = makeError(AppErrorCode::BackendRejected,
                                   pending_->command, state_);
        error.communicationError = release.rejection;
        failPending(std::move(error));
        return;
    }
    pending_->stage = Stage::WaitingReleaseMonitor;
    pending_->communicationOperationId = release.operationId;
}

void AppStateController::closeBackend()
{
    if (!pending_) {
        return;
    }
    const auto close = client_.close(communication::CloseMode::CancelAll);
    if (!close.accepted()) {
        AppError error = makeError(AppErrorCode::BackendRejected,
                                   pending_->command, state_);
        error.communicationError = close.rejection;
        failPending(std::move(error));
        return;
    }
    pending_->stage = Stage::WaitingClose;
    pending_->communicationOperationId = close.operationId;
}

void AppStateController::completePendingSuccess(AppState finalState)
{
    if (!pending_) {
        return;
    }
    const AppCommandResult result{pending_->id, pending_->command, true, std::nullopt};
    pending_.reset();
    setState(finalState);
    emit commandCompleted(result);
}

void AppStateController::failPending(AppError error, bool enterErrorState)
{
    if (!pending_) {
        if (enterErrorState) {
            setState(AppState::Error);
            emit errorOccurred(error);
        }
        return;
    }
    error.command = pending_->command;
    error.state = state_;
    const AppCommandResult result{pending_->id, pending_->command, false, error};
    pending_.reset();
    if (enterErrorState) {
        setState(AppState::Error);
    }
    emit errorOccurred(error);
    emit commandCompleted(result);
}

void AppStateController::enterUnexpectedError(AppError error)
{
    if (pending_) {
        failPending(std::move(error));
        return;
    }
    error.state = state_;
    setState(AppState::Error);
    emit errorOccurred(error);
}

AppError AppStateController::communicationFailure(
    AppErrorCode code,
    const communication::ControlResult &result,
    QString diagnostic) const
{
    const AppCommand command = pending_ ? pending_->command : AppCommand::Disconnect;
    AppError error = makeError(code, command, state_, std::move(diagnostic));
    error.communicationError = result.error;
    return error;
}

} // namespace oms555tv::app

#pragma once

#include "communication/CommunicationTypes.h"
#include "monitor/MonitorTypes.h"

#include <QMetaType>
#include <QString>

#include <optional>

namespace oms555tv::app {

struct AppOperationId {
    quint64 value = 0;
};

[[nodiscard]] constexpr bool operator==(AppOperationId left,
                                        AppOperationId right) noexcept
{
    return left.value == right.value;
}

enum class AppState {
    Disconnected,
    ConnectedIdle,
    Monitoring,
    Testing,
    Stopping,
    Error,
};

enum class AppCommand {
    Connect,
    Disconnect,
    StartMonitoring,
    StopMonitoring,
    StartTesting,
    StopTesting,
    Recover,
};

enum class AppErrorCode {
    InvalidState,
    CommandInProgress,
    BackendRejected,
    BackendOperationFailed,
    BackendStateMismatch,
    MonitorRejected,
    IdExhausted,
    InvariantViolation,
};

struct AppError {
    AppErrorCode code = AppErrorCode::InvariantViolation;
    AppCommand command = AppCommand::Connect;
    AppState state = AppState::Disconnected;
    std::optional<communication::CommunicationError> communicationError;
    std::optional<monitor::MonitorError> monitorError;
    QString diagnostic;
};

struct AppCommandSubmission {
    std::optional<AppOperationId> operationId;
    std::optional<AppError> rejection;

    [[nodiscard]] bool accepted() const noexcept { return operationId.has_value(); }
};

struct AppCommandResult {
    AppOperationId operationId;
    AppCommand command = AppCommand::Connect;
    bool succeeded = false;
    std::optional<AppError> error;
};

} // namespace oms555tv::app

Q_DECLARE_METATYPE(oms555tv::app::AppOperationId)
Q_DECLARE_METATYPE(oms555tv::app::AppState)
Q_DECLARE_METATYPE(oms555tv::app::AppCommand)
Q_DECLARE_METATYPE(oms555tv::app::AppError)
Q_DECLARE_METATYPE(oms555tv::app::AppCommandResult)

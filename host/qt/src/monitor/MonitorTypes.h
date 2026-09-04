#pragma once

#include "communication/CommunicationTypes.h"
#include "device/DeviceTypes.h"

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>

#include <chrono>
#include <limits>
#include <optional>

namespace oms555tv::monitor {

struct PollBatchId {
    quint64 value = 0;
};

[[nodiscard]] constexpr bool operator==(PollBatchId left, PollBatchId right) noexcept
{
    return left.value == right.value;
}

[[nodiscard]] constexpr bool operator!=(PollBatchId left, PollBatchId right) noexcept
{
    return !(left == right);
}

enum class DeviceHealth { Unknown, Online, Degraded, Offline };

enum class MonitorErrorCode {
    InvalidPeriod,
    InvalidRequestTimeout,
    AlreadyRunning,
    NotRunning,
    BackendUnavailable,
    OwnerMismatch,
    RequestRejected,
    RequestFailed,
    ResultMismatch,
    DecodeFailed,
    InvariantViolation,
    Stopped,
};

struct MonitorError {
    MonitorErrorCode code = MonitorErrorCode::InvariantViolation;
    std::optional<PollBatchId> batchId;
    std::optional<qsizetype> blockIndex;
    std::optional<device::PduAddress> blockAddress;
    std::optional<communication::RequestId> requestId;
    std::optional<communication::CommunicationError> communicationError;
    std::optional<device::CodecError> codecError;
    QString diagnostic;
};

struct MonitorConfig {
    std::chrono::milliseconds targetPeriod{1000};
    std::optional<std::chrono::milliseconds> requestTimeout;
};

struct CommunicationStatistics {
    quint64 requests = 0;
    quint64 succeeded = 0;
    quint64 failed = 0;
    quint64 timedOut = 0;
    quint64 cancelled = 0;
    quint64 consecutiveRequestFailures = 0;

    quint64 rttSamples = 0;
    quint64 totalRttNanoseconds = 0;
    std::optional<std::chrono::nanoseconds> recentRtt;
    std::optional<std::chrono::nanoseconds> minimumRtt;
    std::optional<std::chrono::nanoseconds> maximumRtt;

    quint64 batchesStarted = 0;
    quint64 batchesSucceeded = 0;
    quint64 batchesFailed = 0;
    quint64 consecutiveBatchFailures = 0;
    quint64 overruns = 0;
    std::optional<std::chrono::nanoseconds> recentBatchDuration;
    std::optional<std::chrono::nanoseconds> recentEffectiveInterval;
};

struct MonitoringSnapshot {
    PollBatchId batchId;
    QDateTime startedUtc;
    QDateTime completedUtc;
    QDateTime lastSuccessfulUtc;
    QVector<communication::RequestId> requestIds;
    device::DeviceSnapshot snapshot;
};

struct PollBatchResult {
    PollBatchId batchId;
    QDateTime startedUtc;
    QDateTime completedUtc;
    QVector<communication::RequestId> requestIds;
    QVector<communication::ModbusRequestResult> requestResults;
    bool complete = false;
    bool stopped = false;
    bool overrun = false;
    std::chrono::nanoseconds duration{0};
    std::optional<std::chrono::nanoseconds> effectiveInterval;
    std::optional<device::DeviceSnapshot> snapshot;
    std::optional<MonitorError> error;
};

struct MonitorCommandAcceptance {
    bool accepted = false;
    std::optional<MonitorError> rejection;
};

[[nodiscard]] std::optional<MonitorError> validateMonitorConfig(const MonitorConfig &config);

namespace detail {
void saturatingIncrement(quint64 &value) noexcept;
void saturatingAdd(quint64 &value, quint64 increment) noexcept;
} // namespace detail

} // namespace oms555tv::monitor

Q_DECLARE_METATYPE(oms555tv::monitor::PollBatchId)
Q_DECLARE_METATYPE(oms555tv::monitor::DeviceHealth)
Q_DECLARE_METATYPE(oms555tv::monitor::MonitorError)
Q_DECLARE_METATYPE(oms555tv::monitor::CommunicationStatistics)
Q_DECLARE_METATYPE(oms555tv::monitor::MonitoringSnapshot)
Q_DECLARE_METATYPE(oms555tv::monitor::PollBatchResult)

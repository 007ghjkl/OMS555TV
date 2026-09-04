#pragma once

#include "communication/IModbusClient.h"
#include "monitor/MonitorScheduler.h"
#include "monitor/MonitorTypes.h"

#include <QObject>

#include <optional>

namespace oms555tv::monitor {

class MonitorService final : public QObject
{
    Q_OBJECT

public:
    MonitorService(communication::IModbusClient &client,
                   IMonitorScheduler &scheduler,
                   QObject *parent = nullptr);

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] bool isStopping() const noexcept;
    [[nodiscard]] DeviceHealth health() const noexcept;
    [[nodiscard]] const CommunicationStatistics &statistics() const noexcept;
    [[nodiscard]] const std::optional<MonitoringSnapshot> &lastSnapshot() const noexcept;

    MonitorCommandAcceptance start(const MonitorConfig &config);
    MonitorCommandAcceptance stop();
    void abortBackend();

signals:
    void snapshotPublished(const oms555tv::monitor::MonitoringSnapshot &snapshot);
    void batchCompleted(const oms555tv::monitor::PollBatchResult &result);
    void healthChanged(oms555tv::monitor::DeviceHealth health);
    void statisticsChanged(const oms555tv::monitor::CommunicationStatistics &statistics);
    void errorOccurred(const oms555tv::monitor::MonitorError &error);
    void stopped();

private:
    struct ActiveBatch {
        PollBatchId id;
        QDateTime startedUtc;
        std::chrono::nanoseconds startedMonotonic{0};
        std::optional<std::chrono::nanoseconds> effectiveInterval;
        qsizetype nextBlockIndex = 0;
        std::optional<communication::RequestId> activeRequestId;
        QVector<device::RegisterBlock> blocks;
        QVector<communication::RequestId> requestIds;
        QVector<communication::ModbusRequestResult> requestResults;
    };

    void scheduleNextBatch(std::chrono::nanoseconds delay);
    void beginBatch(quint64 generation);
    void submitNextBlock();
    void handleRequestCompleted(const communication::ModbusRequestResult &result);
    void updateRequestStatistics(const communication::ModbusRequestResult &result);
    void finalizeSuccessfulBatch(device::DeviceSnapshot snapshot);
    void finalizeFailedBatch(MonitorError error);
    void finalizeStoppedBatch();
    [[nodiscard]] PollBatchResult takeBatchResult();
    void scheduleAfterCompletedBatch(std::chrono::nanoseconds duration);
    void setHealth(DeviceHealth health);
    void finishStop();

    communication::IModbusClient &client_;
    IMonitorScheduler &scheduler_;
    MonitorConfig config_;
    bool running_ = false;
    bool stopping_ = false;
    quint64 generation_ = 0;
    quint64 nextBatchId_ = 1;
    std::optional<IMonitorScheduler::TaskId> scheduledTask_;
    std::optional<std::chrono::nanoseconds> previousBatchStart_;
    std::optional<ActiveBatch> activeBatch_;
    DeviceHealth health_ = DeviceHealth::Unknown;
    CommunicationStatistics statistics_;
    std::optional<MonitoringSnapshot> lastSnapshot_;
};

} // namespace oms555tv::monitor

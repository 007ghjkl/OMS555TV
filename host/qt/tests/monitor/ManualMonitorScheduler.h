#pragma once

#include "communication/ManualScheduler.h"
#include "monitor/MonitorScheduler.h"

#include <QPointer>

#include <memory>
#include <utility>

namespace oms555tv::monitor::test {

class ManualMonitorScheduler final : public IMonitorScheduler
{
public:
    explicit ManualMonitorScheduler(
        std::shared_ptr<communication::ManualScheduler> scheduler,
        QDateTime utcEpoch = QDateTime::fromMSecsSinceEpoch(0, Qt::UTC))
        : scheduler_(std::move(scheduler))
        , utcEpoch_(std::move(utcEpoch))
    {
    }

    [[nodiscard]] std::chrono::nanoseconds monotonicNow() const noexcept override
    {
        return scheduler_->now();
    }

    [[nodiscard]] QDateTime utcNow() const override
    {
        return utcEpoch_.addMSecs(
            std::chrono::duration_cast<std::chrono::milliseconds>(scheduler_->now()).count());
    }

    [[nodiscard]] TaskId scheduleAfter(std::chrono::nanoseconds delay,
                                       QObject *context,
                                       std::function<void()> callback) override
    {
        const QPointer<QObject> guard(context);
        return scheduler_->scheduleAfter(delay,
                                         [guard, callback = std::move(callback)]() mutable {
            if (guard) {
                callback();
            }
        });
    }

    void cancel(TaskId taskId) noexcept override
    {
        scheduler_->cancel(taskId);
    }

private:
    std::shared_ptr<communication::ManualScheduler> scheduler_;
    QDateTime utcEpoch_;
};

} // namespace oms555tv::monitor::test

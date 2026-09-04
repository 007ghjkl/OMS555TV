#pragma once

#include <QDateTime>
#include <QObject>

#include <chrono>
#include <functional>
#include <memory>

namespace oms555tv::monitor {

class IMonitorScheduler
{
public:
    using TaskId = quint64;

    virtual ~IMonitorScheduler() = default;
    [[nodiscard]] virtual std::chrono::nanoseconds monotonicNow() const noexcept = 0;
    [[nodiscard]] virtual QDateTime utcNow() const = 0;
    [[nodiscard]] virtual TaskId scheduleAfter(std::chrono::nanoseconds delay,
                                               QObject *context,
                                               std::function<void()> callback) = 0;
    virtual void cancel(TaskId taskId) noexcept = 0;
};

class QtMonitorScheduler final : public QObject, public IMonitorScheduler
{
    Q_OBJECT

public:
    explicit QtMonitorScheduler(QObject *parent = nullptr);
    ~QtMonitorScheduler() override;

    [[nodiscard]] std::chrono::nanoseconds monotonicNow() const noexcept override;
    [[nodiscard]] QDateTime utcNow() const override;
    [[nodiscard]] TaskId scheduleAfter(std::chrono::nanoseconds delay,
                                       QObject *context,
                                       std::function<void()> callback) override;
    void cancel(TaskId taskId) noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace oms555tv::monitor

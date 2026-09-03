#pragma once

#include <QtGlobal>

#include <chrono>
#include <functional>
#include <vector>

namespace oms555tv::communication {

class ManualScheduler final
{
public:
    using TaskId = quint64;

    [[nodiscard]] std::chrono::nanoseconds now() const noexcept { return now_; }
    [[nodiscard]] TaskId scheduleAfter(std::chrono::nanoseconds delay,
                                       std::function<void()> callback);
    void cancel(TaskId taskId) noexcept;
    void advanceBy(std::chrono::nanoseconds duration);
    void runUntilIdle();
    [[nodiscard]] bool hasScheduledTasks() const noexcept;

private:
    struct ScheduledTask {
        TaskId id = 0;
        std::chrono::nanoseconds due{0};
        std::function<void()> callback;
        bool cancelled = false;
    };

    void runDueTasks();

    std::chrono::nanoseconds now_{0};
    TaskId nextId_ = 1;
    std::vector<ScheduledTask> tasks_;
};

} // namespace oms555tv::communication

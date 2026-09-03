#include "communication/ManualScheduler.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace oms555tv::communication {

ManualScheduler::TaskId ManualScheduler::scheduleAfter(
    std::chrono::nanoseconds delay,
    std::function<void()> callback)
{
    if (delay < std::chrono::nanoseconds::zero()) {
        delay = std::chrono::nanoseconds::zero();
    }
    const TaskId id = nextId_++;
    tasks_.push_back({id, now_ + delay, std::move(callback), false});
    return id;
}

void ManualScheduler::cancel(TaskId taskId) noexcept
{
    for (auto &task : tasks_) {
        if (task.id == taskId) {
            task.cancelled = true;
            return;
        }
    }
}

void ManualScheduler::advanceBy(std::chrono::nanoseconds duration)
{
    if (duration < std::chrono::nanoseconds::zero()) {
        return;
    }
    now_ += duration;
    runDueTasks();
}

void ManualScheduler::runUntilIdle()
{
    while (hasScheduledTasks()) {
        auto next = std::min_element(
            tasks_.cbegin(), tasks_.cend(), [](const auto &left, const auto &right) {
                if (left.cancelled != right.cancelled) {
                    return !left.cancelled;
                }
                return left.due < right.due
                    || (left.due == right.due && left.id < right.id);
            });
        if (next == tasks_.cend() || next->cancelled) {
            tasks_.clear();
            return;
        }
        now_ = std::max(now_, next->due);
        runDueTasks();
    }
}

bool ManualScheduler::hasScheduledTasks() const noexcept
{
    return std::any_of(tasks_.cbegin(), tasks_.cend(), [](const auto &task) {
        return !task.cancelled;
    });
}

void ManualScheduler::runDueTasks()
{
    for (;;) {
        auto next = tasks_.end();
        for (auto iterator = tasks_.begin(); iterator != tasks_.end(); ++iterator) {
            if (iterator->cancelled || iterator->due > now_) {
                continue;
            }
            if (next == tasks_.end() || iterator->due < next->due
                || (iterator->due == next->due && iterator->id < next->id)) {
                next = iterator;
            }
        }
        if (next == tasks_.end()) {
            tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(), [](const auto &task) {
                             return task.cancelled;
                         }),
                         tasks_.end());
            return;
        }
        auto callback = std::move(next->callback);
        tasks_.erase(next);
        callback();
    }
}

} // namespace oms555tv::communication

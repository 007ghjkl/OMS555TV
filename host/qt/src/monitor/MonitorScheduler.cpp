#include "monitor/MonitorScheduler.h"

#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <utility>

namespace oms555tv::monitor {

class QtMonitorScheduler::Impl final
{
public:
    QElapsedTimer elapsed;
    TaskId nextId = 1;
    QHash<TaskId, QTimer *> timers;
};

QtMonitorScheduler::QtMonitorScheduler(QObject *parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>())
{
    impl_->elapsed.start();
}

QtMonitorScheduler::~QtMonitorScheduler() = default;

std::chrono::nanoseconds QtMonitorScheduler::monotonicNow() const noexcept
{
    return std::chrono::nanoseconds(impl_->elapsed.nsecsElapsed());
}

QDateTime QtMonitorScheduler::utcNow() const
{
    return QDateTime::currentDateTimeUtc();
}

IMonitorScheduler::TaskId QtMonitorScheduler::scheduleAfter(
    std::chrono::nanoseconds delay,
    QObject *context,
    std::function<void()> callback)
{
    if (delay < std::chrono::nanoseconds::zero()) {
        delay = std::chrono::nanoseconds::zero();
    }
    TaskId id = impl_->nextId++;
    if (id == 0) {
        id = impl_->nextId++;
    }

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    // 稳定性测试按实际请求开始时刻计算下一周期。使用默认粗粒度定时器时，
    // 每周期的调度迟到会累计，10 分钟窗口可能达不到套件规定的最低样本数。
    timer->setTimerType(Qt::PreciseTimer);
    impl_->timers.insert(id, timer);
    const QPointer<QObject> guard(context);
    connect(timer, &QTimer::timeout, this,
            [this, id, guard, callback = std::move(callback)]() mutable {
        QTimer *finished = impl_->timers.take(id);
        if (finished != nullptr) {
            finished->deleteLater();
        }
        if (guard) {
            callback();
        }
    });

    const auto rounded = std::chrono::ceil<std::chrono::milliseconds>(delay);
    const qint64 milliseconds = std::clamp<qint64>(
        rounded.count(), 0, std::numeric_limits<int>::max());
    timer->start(static_cast<int>(milliseconds));
    return id;
}

void QtMonitorScheduler::cancel(TaskId taskId) noexcept
{
    QTimer *timer = impl_->timers.take(taskId);
    if (timer == nullptr) {
        return;
    }
    timer->stop();
    timer->deleteLater();
}

} // namespace oms555tv::monitor

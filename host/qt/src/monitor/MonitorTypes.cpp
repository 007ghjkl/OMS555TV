#include "monitor/MonitorTypes.h"

#include <algorithm>
#include <limits>

namespace oms555tv::monitor {

std::optional<MonitorError> validateMonitorConfig(const MonitorConfig &config)
{
    if (config.targetPeriod < std::chrono::milliseconds(10)
        || config.targetPeriod > std::chrono::milliseconds(60000)) {
        return MonitorError{MonitorErrorCode::InvalidPeriod};
    }
    if (config.requestTimeout
        && (*config.requestTimeout < std::chrono::milliseconds(1)
            || *config.requestTimeout > std::chrono::milliseconds(60000))) {
        return MonitorError{MonitorErrorCode::InvalidRequestTimeout};
    }
    return std::nullopt;
}

namespace detail {

void saturatingIncrement(quint64 &value) noexcept
{
    if (value != std::numeric_limits<quint64>::max()) {
        ++value;
    }
}

void saturatingAdd(quint64 &value, quint64 increment) noexcept
{
    const quint64 room = std::numeric_limits<quint64>::max() - value;
    value += std::min(room, increment);
}

} // namespace detail

} // namespace oms555tv::monitor

#include "alarm.h"

bool alarm_update(bool current,
                  int16_t temperature_deci_c,
                  int16_t threshold_deci_c)
{
    const int32_t temperature = temperature_deci_c;
    const int32_t threshold = threshold_deci_c;

    if (!current && temperature >= threshold) {
        return true;
    }

    if (current && temperature <= (threshold - ALARM_HYSTERESIS_DECI_C)) {
        return false;
    }

    return current;
}

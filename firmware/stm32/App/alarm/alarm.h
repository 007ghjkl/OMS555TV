#ifndef OMS555TV_ALARM_H
#define OMS555TV_ALARM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALARM_HYSTERESIS_DECI_C 50

bool alarm_update(bool current,
                  int16_t temperature_deci_c,
                  int16_t threshold_deci_c);

#ifdef __cplusplus
}
#endif

#endif

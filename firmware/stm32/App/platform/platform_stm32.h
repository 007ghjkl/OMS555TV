#ifndef OMS555TV_PLATFORM_STM32_H
#define OMS555TV_PLATFORM_STM32_H

#include <stdbool.h>

#include "modbus_platform.h"
#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

bool platform_stm32_create(Phase1Platform *platform);
bool platform_stm32_create_modbus(ModbusPlatform *platform);

#ifdef __cplusplus
}
#endif

#endif

#ifndef OMS555TV_APP_H
#define OMS555TV_APP_H

#include <stdbool.h>

#include "device_model.h"

#ifdef __cplusplus
extern "C" {
#endif

bool app_init(void);
void app_service(void);
const DeviceModel *app_device_model(void);

#ifdef __cplusplus
}
#endif

#endif

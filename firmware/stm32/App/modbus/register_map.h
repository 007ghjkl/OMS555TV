#ifndef OMS555TV_REGISTER_MAP_H
#define OMS555TV_REGISTER_MAP_H

#include <stdbool.h>
#include <stdint.h>

#include "device_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    REGISTER_WRITE_OK = 0,
    REGISTER_WRITE_ILLEGAL_ADDRESS,
    REGISTER_WRITE_ILLEGAL_VALUE
} RegisterWriteResult;

bool register_map_read_holding(const DeviceModel *model,
                               uint16_t start_address,
                               uint16_t quantity,
                               uint16_t *values);

RegisterWriteResult register_map_write_single(DeviceModel *model,
                                               uint16_t address,
                                               uint16_t raw_value);

#ifdef __cplusplus
}
#endif

#endif

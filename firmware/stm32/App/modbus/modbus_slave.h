#ifndef OMS555TV_MODBUS_SLAVE_H
#define OMS555TV_MODBUS_SLAVE_H

#include <stddef.h>
#include <stdint.h>

#include "device_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODBUS_SLAVE_ID 1u
#define MODBUS_READ_MAX_QUANTITY 125u

typedef enum {
    MODBUS_PROCESS_NO_RESPONSE = 0,
    MODBUS_PROCESS_RESPONSE_READY,
    MODBUS_PROCESS_COMMUNICATION_ERROR
} ModbusProcessResult;

typedef struct {
    ModbusProcessResult result;
    size_t response_length;
} ModbusProcessOutcome;

ModbusProcessOutcome modbus_slave_process_adu(
    DeviceModel *model,
    const uint8_t *request,
    size_t request_length,
    uint8_t *response,
    size_t response_capacity);

#ifdef __cplusplus
}
#endif

#endif

#ifndef OMS555TV_MODBUS_PLATFORM_H
#define OMS555TV_MODBUS_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MODBUS_PORT_EVENT_NONE = 0,
    MODBUS_PORT_EVENT_FRAME,
    MODBUS_PORT_EVENT_OVERFLOW,
    MODBUS_PORT_EVENT_UART_ERROR
} ModbusPortEventType;

typedef struct {
    ModbusPortEventType type;
    uint16_t uart_error_count;
} ModbusPortEvent;

typedef struct {
    void *context;
    bool (*start)(void *context);
    ModbusPortEvent (*poll)(void *context,
                            uint32_t now_ms,
                            uint8_t *frame,
                            size_t capacity,
                            size_t *frame_length);
    PlatformStatus (*transmit)(void *context,
                               const uint8_t *data,
                               size_t length);
} ModbusPlatform;

#ifdef __cplusplus
}
#endif

#endif

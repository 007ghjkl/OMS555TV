#ifndef OMS555TV_PLATFORM_H
#define OMS555TV_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#include "device_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLATFORM_OK = 0,
    PLATFORM_I2C_NOT_READY,
    PLATFORM_TIMEOUT,
    PLATFORM_BUS_ERROR,
    PLATFORM_INVALID_ARGUMENT,
    PLATFORM_IO_ERROR
} PlatformStatus;

typedef struct {
    void *context;
    PlatformStatus (*dht_recover)(void *context,
                                  TemperatureChannel channel);
    PlatformStatus (*dht_write)(void *context,
                                TemperatureChannel channel,
                                uint8_t address_7bit,
                                const uint8_t *data,
                                size_t length,
                                uint32_t timeout_ms);
    PlatformStatus (*dht_read)(void *context,
                               TemperatureChannel channel,
                               uint8_t address_7bit,
                               uint8_t *data,
                               size_t capacity,
                               size_t *received_length,
                               uint32_t timeout_ms);
    PlatformStatus (*adc_start)(void *context);
    PlatformStatus (*adc_read)(void *context,
                               uint16_t *raw,
                               uint32_t timeout_ms);
    PlatformStatus (*debug_write)(void *context,
                                  const uint8_t *data,
                                  size_t length);
    uint32_t (*millis)(void *context);
} Phase1Platform;

#ifdef __cplusplus
}
#endif

#endif

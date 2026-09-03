#ifndef OMS555TV_MODBUS_CRC_H
#define OMS555TV_MODBUS_CRC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t modbus_crc16(const uint8_t *data, size_t length);
bool modbus_crc16_valid(const uint8_t *frame, size_t length);

#ifdef __cplusplus
}
#endif

#endif

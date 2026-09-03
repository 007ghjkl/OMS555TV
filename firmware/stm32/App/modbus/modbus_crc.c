#include "modbus_crc.h"

#include <stdbool.h>

uint16_t modbus_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    size_t index;

    if (data == NULL && length != 0u) {
        return 0u;
    }

    for (index = 0u; index < length; ++index) {
        unsigned int bit;
        crc ^= data[index];
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 1u) != 0u
                ? (uint16_t)((crc >> 1u) ^ 0xA001u)
                : (uint16_t)(crc >> 1u);
        }
    }

    return crc;
}

bool modbus_crc16_valid(const uint8_t *frame, size_t length)
{
    uint16_t expected;
    uint16_t actual;

    if (frame == NULL || length < 4u) {
        return false;
    }

    expected = modbus_crc16(frame, length - 2u);
    actual = (uint16_t)(frame[length - 2u] |
                        ((uint16_t)frame[length - 1u] << 8u));
    return expected == actual;
}

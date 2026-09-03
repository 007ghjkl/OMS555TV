#include "dhtc12_codec.h"

#include <limits.h>
#include <string.h>

#define TEMPERATURE_MIN_DECI_C (-400)
#define TEMPERATURE_MAX_DECI_C 800

uint8_t dhtc12_crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0xFFu;
    size_t byte_index;

    if (data == NULL && length != 0u) {
        return 0u;
    }

    for (byte_index = 0u; byte_index < length; ++byte_index) {
        unsigned int bit;
        crc ^= data[byte_index];
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x80u) != 0u
                ? (uint8_t)((uint8_t)(crc << 1u) ^ 0x31u)
                : (uint8_t)(crc << 1u);
        }
    }

    return crc;
}

static int16_t convert_temperature(uint16_t raw)
{
    const int16_t st = (int16_t)raw;
    const int32_t scaled = (int32_t)st * 10;
    const int32_t delta = scaled >= 0
        ? (scaled + 128) / 256
        : -((-scaled + 128) / 256);
    const int32_t result = 400 + delta;

    if (result < INT16_MIN) {
        return INT16_MIN;
    }
    if (result > INT16_MAX) {
        return INT16_MAX;
    }
    return (int16_t)result;
}

AcquisitionError dhtc12_decode_frame(const uint8_t frame[DHTC12_FRAME_SIZE],
                                     Dhtc12Frame *decoded)
{
    if (frame == NULL || decoded == NULL) {
        return ACQ_ERROR_SHORT_FRAME;
    }

    memset(decoded, 0, sizeof(*decoded));
    decoded->temperature_raw =
        (uint16_t)(((uint16_t)frame[0] << 8u) | frame[1]);
    decoded->temperature_crc = frame[2];
    decoded->humidity_raw =
        (uint16_t)(((uint16_t)frame[3] << 8u) | frame[4]);
    decoded->humidity_crc = frame[5];
    decoded->temperature_crc_valid =
        dhtc12_crc8(frame, 2u) == decoded->temperature_crc;
    decoded->humidity_crc_valid =
        dhtc12_crc8(&frame[3], 2u) == decoded->humidity_crc;
    decoded->temperature_deci_c =
        convert_temperature(decoded->temperature_raw);

    if (!decoded->temperature_crc_valid) {
        return ACQ_ERROR_TEMP_CRC;
    }
    if (!decoded->humidity_crc_valid) {
        return ACQ_ERROR_HUMIDITY_CRC;
    }
    if (decoded->temperature_deci_c < TEMPERATURE_MIN_DECI_C ||
        decoded->temperature_deci_c > TEMPERATURE_MAX_DECI_C) {
        return ACQ_ERROR_TEMP_OUT_OF_RANGE;
    }

    return ACQ_ERROR_NONE;
}

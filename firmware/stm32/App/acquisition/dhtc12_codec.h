#ifndef OMS555TV_DHTC12_CODEC_H
#define OMS555TV_DHTC12_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DHTC12_FRAME_SIZE 6u

typedef struct {
    uint16_t temperature_raw;
    uint16_t humidity_raw;
    uint8_t temperature_crc;
    uint8_t humidity_crc;
    bool temperature_crc_valid;
    bool humidity_crc_valid;
    int16_t temperature_deci_c;
} Dhtc12Frame;

uint8_t dhtc12_crc8(const uint8_t *data, size_t length);

AcquisitionError dhtc12_decode_frame(const uint8_t frame[DHTC12_FRAME_SIZE],
                                     Dhtc12Frame *decoded);

#ifdef __cplusplus
}
#endif

#endif

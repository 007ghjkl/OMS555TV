#ifndef OMS555TV_ACQUISITION_H
#define OMS555TV_ACQUISITION_H

#include <stdbool.h>
#include <stdint.h>

#include "device_model.h"
#include "dhtc12_codec.h"
#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DHTC12_REAL_CHANNEL_COUNT 3u
#define ADC_BATCH_SAMPLE_COUNT 16u

typedef enum {
    ACQUISITION_EVENT_DHT = 0,
    ACQUISITION_EVENT_LIGHT
} AcquisitionEventType;

typedef struct {
    AcquisitionEventType type;
    uint32_t sequence;
    uint32_t timestamp_ms;
    AcquisitionError error;
    PlatformStatus platform_status;
    bool published;
    union {
        struct {
            TemperatureChannel channel;
            bool initialization;
            uint32_t conversion_elapsed_ms;
            bool frame_available;
            Dhtc12Frame frame;
        } dht;
        struct {
            uint16_t average_raw;
            uint16_t value_mv;
            uint8_t sample_count;
        } light;
    } data;
} AcquisitionEvent;

typedef void (*AcquisitionObserver)(void *context,
                                    const AcquisitionEvent *event,
                                    const DeviceModel *model);

typedef enum {
    DHT_STATE_INIT_RECOVER = 0,
    DHT_STATE_INIT_RESET,
    DHT_STATE_IDLE,
    DHT_STATE_WAIT_READY
} DhtAcquisitionState;

typedef struct {
    DhtAcquisitionState state;
    uint32_t next_trigger_ms;
    uint32_t triggered_at_ms;
    uint32_t next_read_ms;
} DhtChannelContext;

typedef enum {
    ADC_STATE_IDLE = 0,
    ADC_STATE_START_SAMPLE,
    ADC_STATE_WAIT_SAMPLE
} AdcAcquisitionState;

typedef struct {
    AdcAcquisitionState state;
    uint32_t next_batch_ms;
    uint32_t sum_raw;
    uint8_t sample_count;
} AdcAcquisitionContext;

typedef struct {
    Phase1Platform platform;
    AcquisitionObserver observer;
    void *observer_context;
    bool temperature_conversion_confirmed;
    DhtChannelContext dht[DHTC12_REAL_CHANNEL_COUNT];
    AdcAcquisitionContext adc;
    uint32_t event_sequence;
} AcquisitionContext;

bool acquisition_init(AcquisitionContext *context,
                      const Phase1Platform *platform,
                      AcquisitionObserver observer,
                      void *observer_context,
                      bool temperature_conversion_confirmed,
                      uint32_t now_ms);

void acquisition_service(AcquisitionContext *context,
                         DeviceModel *model,
                         uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif

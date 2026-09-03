#include "acquisition.h"

#include <stddef.h>
#include <string.h>

#define DHTC12_ADDRESS_7BIT 0x44u
#define DHTC12_SAMPLE_INTERVAL_MS 2000u
#define DHTC12_READY_POLL_MS 50u
#define DHTC12_CONVERSION_TIMEOUT_MS 250u
#define DHTC12_IO_TIMEOUT_MS 10u
#define ADC_BATCH_INTERVAL_MS 2000u
#define ADC_FIRST_BATCH_OFFSET_MS 75u
#define ADC_IO_TIMEOUT_MS 1u
#define ADC_MAX_RAW 4095u
#define ADC_REFERENCE_MV 3300u

static const uint32_t DHT_FIRST_OFFSETS_MS[DHTC12_REAL_CHANNEL_COUNT] = {
    0u, 25u, 50u
};

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static bool platform_complete(const Phase1Platform *platform)
{
    return platform != NULL &&
           platform->dht_recover != NULL &&
           platform->dht_write != NULL &&
           platform->dht_read != NULL &&
           platform->adc_start != NULL &&
           platform->adc_read != NULL;
}

static void emit_event(AcquisitionContext *context,
                       const AcquisitionEvent *event,
                       const DeviceModel *model)
{
    if (context->observer != NULL) {
        context->observer(context->observer_context, event, model);
    }
}

static AcquisitionEvent new_event(AcquisitionContext *context,
                                  AcquisitionEventType type,
                                  uint32_t now_ms)
{
    AcquisitionEvent event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.sequence = context->event_sequence++;
    event.timestamp_ms = now_ms;
    event.error = ACQ_ERROR_NONE;
    event.platform_status = PLATFORM_OK;
    return event;
}

static void fail_dht_cycle(AcquisitionContext *context,
                           DeviceModel *model,
                           TemperatureChannel channel,
                           uint32_t now_ms,
                           uint32_t conversion_elapsed_ms,
                           AcquisitionError error,
                           PlatformStatus platform_status,
                           bool crc_failure,
                           const Dhtc12Frame *frame)
{
    AcquisitionEvent event = new_event(context, ACQUISITION_EVENT_DHT, now_ms);

    device_model_fail_temperature(model, channel, error, crc_failure);
    event.error = error;
    event.platform_status = platform_status;
    event.data.dht.channel = channel;
    event.data.dht.conversion_elapsed_ms = conversion_elapsed_ms;
    event.data.dht.frame_available = frame != NULL;
    if (frame != NULL) {
        event.data.dht.frame = *frame;
    }
    emit_event(context, &event, model);
}

static void fail_dht_initialization(AcquisitionContext *context,
                                    DeviceModel *model,
                                    TemperatureChannel channel,
                                    DhtChannelContext *channel_context,
                                    uint32_t now_ms,
                                    AcquisitionError error,
                                    PlatformStatus platform_status)
{
    AcquisitionEvent event = new_event(context, ACQUISITION_EVENT_DHT, now_ms);

    device_model_fail_temperature(model, channel, error, false);
    event.error = error;
    event.platform_status = platform_status;
    event.data.dht.channel = channel;
    event.data.dht.initialization = true;
    channel_context->state = DHT_STATE_INIT_RECOVER;
    channel_context->next_trigger_ms =
        now_ms + DHTC12_SAMPLE_INTERVAL_MS;
    emit_event(context, &event, model);
}

static void publish_dht_initialization(AcquisitionContext *context,
                                       const DeviceModel *model,
                                       TemperatureChannel channel,
                                       uint32_t now_ms)
{
    AcquisitionEvent event = new_event(context, ACQUISITION_EVENT_DHT, now_ms);

    event.data.dht.channel = channel;
    event.data.dht.initialization = true;
    emit_event(context, &event, model);
}

static void publish_dht_cycle(AcquisitionContext *context,
                              DeviceModel *model,
                              TemperatureChannel channel,
                              uint32_t now_ms,
                              uint32_t conversion_elapsed_ms,
                              const Dhtc12Frame *frame)
{
    AcquisitionEvent event = new_event(context, ACQUISITION_EVENT_DHT, now_ms);

    event.data.dht.channel = channel;
    event.data.dht.conversion_elapsed_ms = conversion_elapsed_ms;
    event.data.dht.frame_available = true;
    event.data.dht.frame = *frame;
    event.published = device_model_publish_temperature(
        model, channel, frame->temperature_deci_c, now_ms);
    if (!event.published) {
        event.error = ACQ_ERROR_TEMP_OUT_OF_RANGE;
    }
    emit_event(context, &event, model);
}

static AcquisitionError read_error_from_platform(PlatformStatus status)
{
    return status == PLATFORM_TIMEOUT
        ? ACQ_ERROR_I2C_TIMEOUT
        : ACQ_ERROR_I2C_BUS;
}

static void service_dht_channel(AcquisitionContext *context,
                                DeviceModel *model,
                                TemperatureChannel channel,
                                uint32_t now_ms)
{
    static const uint8_t reset_command[2] = {0x30u, 0xA2u};
    static const uint8_t measure_command[2] = {0x2Cu, 0x10u};
    DhtChannelContext *channel_context = &context->dht[channel];

    if (channel_context->state == DHT_STATE_INIT_RECOVER) {
        PlatformStatus status;

        if (!time_reached(now_ms, channel_context->next_trigger_ms)) {
            return;
        }
        status = context->platform.dht_recover(
            context->platform.context, channel);
        if (status != PLATFORM_OK) {
            fail_dht_initialization(context,
                                    model,
                                    channel,
                                    channel_context,
                                    now_ms,
                                    ACQ_ERROR_I2C_RECOVERY,
                                    status);
            return;
        }
        channel_context->state = DHT_STATE_INIT_RESET;
        channel_context->next_trigger_ms = now_ms;
        return;
    }

    if (channel_context->state == DHT_STATE_INIT_RESET) {
        PlatformStatus status;

        if (!time_reached(now_ms, channel_context->next_trigger_ms)) {
            return;
        }
        status = context->platform.dht_write(
            context->platform.context,
            channel,
            DHTC12_ADDRESS_7BIT,
            reset_command,
            sizeof(reset_command),
            DHTC12_IO_TIMEOUT_MS);
        if (status != PLATFORM_OK) {
            fail_dht_initialization(context,
                                    model,
                                    channel,
                                    channel_context,
                                    now_ms,
                                    ACQ_ERROR_I2C_RESET,
                                    status);
            return;
        }
        channel_context->state = DHT_STATE_IDLE;
        channel_context->next_trigger_ms =
            now_ms + DHTC12_SAMPLE_INTERVAL_MS;
        publish_dht_initialization(context, model, channel, now_ms);
        return;
    }

    if (channel_context->state == DHT_STATE_IDLE) {
        PlatformStatus status;

        if (!time_reached(now_ms, channel_context->next_trigger_ms)) {
            return;
        }

        channel_context->next_trigger_ms =
            now_ms + DHTC12_SAMPLE_INTERVAL_MS;
        status = context->platform.dht_write(
            context->platform.context,
            channel,
            DHTC12_ADDRESS_7BIT,
            measure_command,
            sizeof(measure_command),
            DHTC12_IO_TIMEOUT_MS);
        if (status != PLATFORM_OK) {
            channel_context->state = DHT_STATE_INIT_RECOVER;
            fail_dht_cycle(context,
                           model,
                           channel,
                           now_ms,
                           0u,
                           ACQ_ERROR_I2C_TRIGGER,
                           status,
                           false,
                           NULL);
            return;
        }

        channel_context->triggered_at_ms = now_ms;
        channel_context->next_read_ms = now_ms + DHTC12_READY_POLL_MS;
        channel_context->state = DHT_STATE_WAIT_READY;
        return;
    }

    if (channel_context->state == DHT_STATE_WAIT_READY &&
        time_reached(now_ms, channel_context->next_read_ms)) {
        uint8_t raw_frame[DHTC12_FRAME_SIZE] = {0u};
        size_t received_length = 0u;
        Dhtc12Frame frame;
        AcquisitionError decode_error;
        const uint32_t conversion_elapsed_ms =
            now_ms - channel_context->triggered_at_ms;
        const PlatformStatus status = context->platform.dht_read(
            context->platform.context,
            channel,
            DHTC12_ADDRESS_7BIT,
            raw_frame,
            sizeof(raw_frame),
            &received_length,
            DHTC12_IO_TIMEOUT_MS);

        if (status == PLATFORM_I2C_NOT_READY) {
            if ((uint32_t)(now_ms - channel_context->triggered_at_ms) >=
                DHTC12_CONVERSION_TIMEOUT_MS) {
                channel_context->state = DHT_STATE_INIT_RECOVER;
                fail_dht_cycle(context,
                               model,
                               channel,
                               now_ms,
                               conversion_elapsed_ms,
                               ACQ_ERROR_I2C_TIMEOUT,
                               status,
                               false,
                               NULL);
            } else {
                channel_context->next_read_ms = now_ms + DHTC12_READY_POLL_MS;
            }
            return;
        }

        if (status != PLATFORM_OK) {
            channel_context->state = DHT_STATE_INIT_RECOVER;
            fail_dht_cycle(context,
                           model,
                           channel,
                           now_ms,
                           conversion_elapsed_ms,
                           read_error_from_platform(status),
                           status,
                           false,
                           NULL);
            return;
        }

        channel_context->state = DHT_STATE_IDLE;

        if (received_length != DHTC12_FRAME_SIZE) {
            fail_dht_cycle(context,
                           model,
                           channel,
                           now_ms,
                           conversion_elapsed_ms,
                           ACQ_ERROR_SHORT_FRAME,
                           status,
                           false,
                           NULL);
            return;
        }

        decode_error = dhtc12_decode_frame(raw_frame, &frame);
        if (decode_error != ACQ_ERROR_NONE) {
            fail_dht_cycle(context,
                           model,
                           channel,
                           now_ms,
                           conversion_elapsed_ms,
                           decode_error,
                           status,
                           decode_error == ACQ_ERROR_TEMP_CRC ||
                               decode_error == ACQ_ERROR_HUMIDITY_CRC,
                           &frame);
            return;
        }

        if (!context->temperature_conversion_confirmed) {
            fail_dht_cycle(context,
                           model,
                           channel,
                           now_ms,
                           conversion_elapsed_ms,
                           ACQ_ERROR_TEMP_CONVERSION_UNCONFIRMED,
                           status,
                           false,
                           &frame);
            return;
        }

        publish_dht_cycle(context,
                          model,
                          channel,
                          now_ms,
                          conversion_elapsed_ms,
                          &frame);
    }
}

static AcquisitionError adc_error_from_platform(PlatformStatus status,
                                                bool starting)
{
    if (starting) {
        return ACQ_ERROR_ADC_START;
    }
    return status == PLATFORM_TIMEOUT
        ? ACQ_ERROR_ADC_TIMEOUT
        : ACQ_ERROR_ADC_READ;
}

static void fail_adc_batch(AcquisitionContext *context,
                           DeviceModel *model,
                           uint32_t now_ms,
                           AcquisitionError error,
                           PlatformStatus platform_status)
{
    AcquisitionEvent event = new_event(context, ACQUISITION_EVENT_LIGHT, now_ms);

    device_model_fail_light(model, error);
    event.error = error;
    event.platform_status = platform_status;
    event.data.light.sample_count = context->adc.sample_count;
    emit_event(context, &event, model);
    context->adc.state = ADC_STATE_IDLE;
    context->adc.next_batch_ms = now_ms + ADC_BATCH_INTERVAL_MS;
    context->adc.sum_raw = 0u;
    context->adc.sample_count = 0u;
}

static void service_adc(AcquisitionContext *context,
                        DeviceModel *model,
                        uint32_t now_ms)
{
    if (context->adc.state == ADC_STATE_IDLE) {
        if (!time_reached(now_ms, context->adc.next_batch_ms)) {
            return;
        }
        context->adc.sum_raw = 0u;
        context->adc.sample_count = 0u;
        context->adc.next_batch_ms = now_ms + ADC_BATCH_INTERVAL_MS;
        context->adc.state = ADC_STATE_START_SAMPLE;
        return;
    }

    if (context->adc.state == ADC_STATE_START_SAMPLE) {
        const PlatformStatus status =
            context->platform.adc_start(context->platform.context);
        if (status != PLATFORM_OK) {
            fail_adc_batch(context,
                           model,
                           now_ms,
                           adc_error_from_platform(status, true),
                           status);
        } else {
            context->adc.state = ADC_STATE_WAIT_SAMPLE;
        }
        return;
    }

    if (context->adc.state == ADC_STATE_WAIT_SAMPLE) {
        uint16_t raw = 0u;
        const PlatformStatus status = context->platform.adc_read(
            context->platform.context, &raw, ADC_IO_TIMEOUT_MS);

        if (status != PLATFORM_OK) {
            fail_adc_batch(context,
                           model,
                           now_ms,
                           adc_error_from_platform(status, false),
                           status);
            return;
        }
        if (raw > ADC_MAX_RAW) {
            fail_adc_batch(context,
                           model,
                           now_ms,
                           ACQ_ERROR_ADC_OUT_OF_RANGE,
                           PLATFORM_INVALID_ARGUMENT);
            return;
        }

        context->adc.sum_raw += raw;
        ++context->adc.sample_count;
        if (context->adc.sample_count < ADC_BATCH_SAMPLE_COUNT) {
            context->adc.state = ADC_STATE_START_SAMPLE;
            return;
        }

        {
            const uint16_t average_raw = (uint16_t)(
                (context->adc.sum_raw + ADC_BATCH_SAMPLE_COUNT / 2u) /
                ADC_BATCH_SAMPLE_COUNT);
            const uint16_t value_mv = (uint16_t)(
                ((uint32_t)average_raw * ADC_REFERENCE_MV) / ADC_MAX_RAW);
            AcquisitionEvent event =
                new_event(context, ACQUISITION_EVENT_LIGHT, now_ms);

            event.data.light.average_raw = average_raw;
            event.data.light.value_mv = value_mv;
            event.data.light.sample_count = ADC_BATCH_SAMPLE_COUNT;
            event.published =
                device_model_publish_light(model, value_mv, now_ms);
            if (!event.published) {
                event.error = ACQ_ERROR_ADC_OUT_OF_RANGE;
            }
            emit_event(context, &event, model);
        }

        context->adc.state = ADC_STATE_IDLE;
        context->adc.sum_raw = 0u;
        context->adc.sample_count = 0u;
    }
}

bool acquisition_init(AcquisitionContext *context,
                      const Phase1Platform *platform,
                      AcquisitionObserver observer,
                      void *observer_context,
                      bool temperature_conversion_confirmed,
                      uint32_t now_ms)
{
    unsigned int channel;

    if (context == NULL || !platform_complete(platform)) {
        return false;
    }

    memset(context, 0, sizeof(*context));
    context->platform = *platform;
    context->observer = observer;
    context->observer_context = observer_context;
    context->temperature_conversion_confirmed =
        temperature_conversion_confirmed;
    for (channel = 0u; channel < DHTC12_REAL_CHANNEL_COUNT; ++channel) {
        context->dht[channel].state = DHT_STATE_INIT_RECOVER;
        context->dht[channel].next_trigger_ms =
            now_ms + DHT_FIRST_OFFSETS_MS[channel];
    }
    context->adc.state = ADC_STATE_IDLE;
    context->adc.next_batch_ms = now_ms + ADC_FIRST_BATCH_OFFSET_MS;
    return true;
}

void acquisition_service(AcquisitionContext *context,
                         DeviceModel *model,
                         uint32_t now_ms)
{
    unsigned int channel;

    if (context == NULL || model == NULL) {
        return;
    }

    for (channel = 0u; channel < DHTC12_REAL_CHANNEL_COUNT; ++channel) {
        service_dht_channel(
            context, model, (TemperatureChannel)channel, now_ms);
    }
    service_adc(context, model, now_ms);
}

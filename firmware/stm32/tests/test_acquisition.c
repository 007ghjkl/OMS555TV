#include "test_framework.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "acquisition.h"

typedef struct {
    uint32_t now_ms;
    unsigned int dht_recover_count[DHTC12_REAL_CHANNEL_COUNT];
    PlatformStatus dht_recover_status[DHTC12_REAL_CHANNEL_COUNT];
    unsigned int dht_reset_count[DHTC12_REAL_CHANNEL_COUNT];
    PlatformStatus dht_reset_status[DHTC12_REAL_CHANNEL_COUNT];
    unsigned int dht_measure_count[DHTC12_REAL_CHANNEL_COUNT];
    uint32_t dht_last_measure_ms[DHTC12_REAL_CHANNEL_COUNT];
    PlatformStatus dht_measure_status[DHTC12_REAL_CHANNEL_COUNT];
    PlatformStatus dht_read_status[DHTC12_REAL_CHANNEL_COUNT];
    uint8_t dht_frame[DHTC12_REAL_CHANNEL_COUNT][DHTC12_FRAME_SIZE];
    size_t dht_frame_length[DHTC12_REAL_CHANNEL_COUNT];
    unsigned int adc_start_count;
    PlatformStatus adc_start_status;
    PlatformStatus adc_read_status;
    uint16_t adc_values[ADC_BATCH_SAMPLE_COUNT];
    unsigned int adc_value_index;
    AcquisitionEvent last_event;
    unsigned int event_count;
    AcquisitionEvent last_dht_event[DHTC12_REAL_CHANNEL_COUNT];
    unsigned int dht_event_count[DHTC12_REAL_CHANNEL_COUNT];
} FakePlatform;

static PlatformStatus fake_dht_recover(void *context,
                                       TemperatureChannel channel)
{
    FakePlatform *fake = context;
    if (channel >= TEMP_CHANNEL_AMBIENT) {
        return PLATFORM_INVALID_ARGUMENT;
    }
    ++fake->dht_recover_count[channel];
    return fake->dht_recover_status[channel];
}

static PlatformStatus fake_dht_write(void *context,
                                     TemperatureChannel channel,
                                     uint8_t address_7bit,
                                     const uint8_t *data,
                                     size_t length,
                                     uint32_t timeout_ms)
{
    FakePlatform *fake = context;
    (void)timeout_ms;
    if (channel >= TEMP_CHANNEL_AMBIENT || address_7bit != 0x44u ||
        data == NULL || length != 2u) {
        return PLATFORM_INVALID_ARGUMENT;
    }
    if (data[0] == 0x30u && data[1] == 0xA2u) {
        ++fake->dht_reset_count[channel];
        return fake->dht_reset_status[channel];
    }
    if (data[0] == 0x2Cu && data[1] == 0x10u) {
        ++fake->dht_measure_count[channel];
        fake->dht_last_measure_ms[channel] = fake->now_ms;
        return fake->dht_measure_status[channel];
    }
    return PLATFORM_INVALID_ARGUMENT;
}

static PlatformStatus fake_dht_read(void *context,
                                    TemperatureChannel channel,
                                    uint8_t address_7bit,
                                    uint8_t *data,
                                    size_t capacity,
                                    size_t *received_length,
                                    uint32_t timeout_ms)
{
    FakePlatform *fake = context;
    const size_t length = fake->dht_frame_length[channel];
    (void)timeout_ms;
    if (channel >= TEMP_CHANNEL_AMBIENT || address_7bit != 0x44u ||
        data == NULL || received_length == NULL) {
        return PLATFORM_INVALID_ARGUMENT;
    }
    if (fake->dht_read_status[channel] != PLATFORM_OK) {
        *received_length = 0u;
        return fake->dht_read_status[channel];
    }
    if (capacity < length) {
        return PLATFORM_INVALID_ARGUMENT;
    }
    memcpy(data, fake->dht_frame[channel], length);
    *received_length = length;
    return PLATFORM_OK;
}

static PlatformStatus fake_adc_start(void *context)
{
    FakePlatform *fake = context;
    ++fake->adc_start_count;
    return fake->adc_start_status;
}

static PlatformStatus fake_adc_read(void *context,
                                    uint16_t *raw,
                                    uint32_t timeout_ms)
{
    FakePlatform *fake = context;
    (void)timeout_ms;
    if (raw == NULL) {
        return PLATFORM_INVALID_ARGUMENT;
    }
    if (fake->adc_read_status != PLATFORM_OK) {
        return fake->adc_read_status;
    }
    *raw = fake->adc_values[fake->adc_value_index % ADC_BATCH_SAMPLE_COUNT];
    ++fake->adc_value_index;
    return PLATFORM_OK;
}

static void observe_event(void *context,
                          const AcquisitionEvent *event,
                          const DeviceModel *model)
{
    FakePlatform *fake = context;
    (void)model;
    fake->last_event = *event;
    ++fake->event_count;
    if (event->type == ACQUISITION_EVENT_DHT &&
        event->data.dht.channel < TEMP_CHANNEL_AMBIENT) {
        fake->last_dht_event[event->data.dht.channel] = *event;
        ++fake->dht_event_count[event->data.dht.channel];
    }
}

static void set_valid_frame(uint8_t frame[DHTC12_FRAME_SIZE])
{
    static const uint8_t valid[DHTC12_FRAME_SIZE] = {
        0xF1u, 0x00u, 0x6Du, 0x12u, 0x34u, 0x37u
    };
    memcpy(frame, valid, sizeof(valid));
}

static void fake_init(FakePlatform *fake, Phase1Platform *platform)
{
    unsigned int channel;
    memset(fake, 0, sizeof(*fake));
    for (channel = 0u; channel < DHTC12_REAL_CHANNEL_COUNT; ++channel) {
        fake->dht_recover_status[channel] = PLATFORM_OK;
        fake->dht_reset_status[channel] = PLATFORM_OK;
        fake->dht_measure_status[channel] = PLATFORM_OK;
        fake->dht_read_status[channel] = PLATFORM_OK;
        fake->dht_frame_length[channel] = DHTC12_FRAME_SIZE;
        set_valid_frame(fake->dht_frame[channel]);
    }
    fake->adc_start_status = PLATFORM_OK;
    fake->adc_read_status = PLATFORM_OK;
    memset(platform, 0, sizeof(*platform));
    platform->context = fake;
    platform->dht_recover = fake_dht_recover;
    platform->dht_write = fake_dht_write;
    platform->dht_read = fake_dht_read;
    platform->adc_start = fake_adc_start;
    platform->adc_read = fake_adc_read;
}

static void service_at(AcquisitionContext *acquisition,
                       DeviceModel *model,
                       FakePlatform *fake,
                       uint32_t now_ms)
{
    fake->now_ms = now_ms;
    acquisition_service(acquisition, model, now_ms);
}

static void initialize_channel(AcquisitionContext *acquisition,
                               DeviceModel *model,
                               FakePlatform *fake,
                               uint32_t offset_ms)
{
    service_at(acquisition, model, fake, offset_ms);
    service_at(acquisition, model, fake, offset_ms + 1u);
}

static void dht_initialization_recovers_resets_and_waits(void)
{
    FakePlatform fake;
    Phase1Platform platform;
    AcquisitionContext acquisition;
    DeviceModel model;

    fake_init(&fake, &platform);
    device_model_init(&model);
    TEST_ASSERT_TRUE(acquisition_init(
        &acquisition, &platform, observe_event, &fake, true, 0u));

    service_at(&acquisition, &model, &fake, 0u);
    TEST_ASSERT_EQ(1, fake.dht_recover_count[TEMP_CHANNEL_A]);
    TEST_ASSERT_EQ(0, fake.dht_reset_count[TEMP_CHANNEL_A]);
    service_at(&acquisition, &model, &fake, 1u);
    TEST_ASSERT_EQ(1, fake.dht_reset_count[TEMP_CHANNEL_A]);
    TEST_ASSERT_TRUE(
        fake.last_dht_event[TEMP_CHANNEL_A].data.dht.initialization);
    TEST_ASSERT_EQ(ACQ_ERROR_NONE,
                   fake.last_dht_event[TEMP_CHANNEL_A].error);
    service_at(&acquisition, &model, &fake, 2000u);
    TEST_ASSERT_EQ(0, fake.dht_measure_count[TEMP_CHANNEL_A]);
    service_at(&acquisition, &model, &fake, 2001u);
    TEST_ASSERT_EQ(1, fake.dht_measure_count[TEMP_CHANNEL_A]);
}

static void dht_recovery_failure_prevents_reset(void)
{
    FakePlatform fake;
    Phase1Platform platform;
    AcquisitionContext acquisition;
    DeviceModel model;

    fake_init(&fake, &platform);
    fake.dht_recover_status[TEMP_CHANNEL_A] = PLATFORM_BUS_ERROR;
    device_model_init(&model);
    TEST_ASSERT_TRUE(acquisition_init(
        &acquisition, &platform, observe_event, &fake, true, 0u));

    service_at(&acquisition, &model, &fake, 0u);
    TEST_ASSERT_EQ(ACQ_ERROR_I2C_RECOVERY,
                   model.temperatures[TEMP_CHANNEL_A].last_error);
    TEST_ASSERT_EQ(0, fake.dht_reset_count[TEMP_CHANNEL_A]);
    TEST_ASSERT_EQ(0, fake.dht_measure_count[TEMP_CHANNEL_A]);
    TEST_ASSERT_TRUE(
        fake.last_dht_event[TEMP_CHANNEL_A].data.dht.initialization);
}

static void dht_reset_failure_restarts_recovery(void)
{
    FakePlatform fake;
    Phase1Platform platform;
    AcquisitionContext acquisition;
    DeviceModel model;

    fake_init(&fake, &platform);
    fake.dht_reset_status[TEMP_CHANNEL_A] = PLATFORM_BUS_ERROR;
    device_model_init(&model);
    TEST_ASSERT_TRUE(acquisition_init(
        &acquisition, &platform, observe_event, &fake, true, 0u));

    initialize_channel(&acquisition, &model, &fake, 0u);
    TEST_ASSERT_EQ(ACQ_ERROR_I2C_RESET,
                   model.temperatures[TEMP_CHANNEL_A].last_error);
    TEST_ASSERT_EQ(0, fake.dht_measure_count[TEMP_CHANNEL_A]);
    service_at(&acquisition, &model, &fake, 2001u);
    TEST_ASSERT_EQ(2, fake.dht_recover_count[TEMP_CHANNEL_A]);
}

static void dht_schedule_publishes_and_respects_interval(void)
{
    FakePlatform fake;
    Phase1Platform platform;
    AcquisitionContext acquisition;
    DeviceModel model;

    fake_init(&fake, &platform);
    device_model_init(&model);
    TEST_ASSERT_TRUE(acquisition_init(
        &acquisition, &platform, observe_event, &fake, true, 0u));

    initialize_channel(&acquisition, &model, &fake, 0u);
    TEST_ASSERT_EQ(1, fake.dht_recover_count[TEMP_CHANNEL_A]);
    TEST_ASSERT_EQ(1, fake.dht_reset_count[TEMP_CHANNEL_A]);
    service_at(&acquisition, &model, &fake, 2000u);
    TEST_ASSERT_EQ(0, fake.dht_measure_count[TEMP_CHANNEL_A]);
    service_at(&acquisition, &model, &fake, 2001u);
    TEST_ASSERT_EQ(1, fake.dht_measure_count[TEMP_CHANNEL_A]);
    service_at(&acquisition, &model, &fake, 2051u);
    TEST_ASSERT_TRUE(model.temperatures[TEMP_CHANNEL_A].valid);
    TEST_ASSERT_EQ(250, model.temperatures[TEMP_CHANNEL_A].value_deci_c);
    service_at(&acquisition, &model, &fake, 4000u);
    TEST_ASSERT_EQ(1, fake.dht_measure_count[TEMP_CHANNEL_A]);
    service_at(&acquisition, &model, &fake, 4001u);
    TEST_ASSERT_EQ(2, fake.dht_measure_count[TEMP_CHANNEL_A]);
}

static void unconfirmed_conversion_does_not_publish(void)
{
    FakePlatform fake;
    Phase1Platform platform;
    AcquisitionContext acquisition;
    DeviceModel model;

    fake_init(&fake, &platform);
    device_model_init(&model);
    TEST_ASSERT_TRUE(acquisition_init(
        &acquisition, &platform, observe_event, &fake, false, 0u));
    initialize_channel(&acquisition, &model, &fake, 0u);
    service_at(&acquisition, &model, &fake, 2001u);
    service_at(&acquisition, &model, &fake, 2051u);

    TEST_ASSERT_FALSE(model.temperatures[TEMP_CHANNEL_A].valid);
    TEST_ASSERT_EQ(ACQ_ERROR_TEMP_CONVERSION_UNCONFIRMED,
                   model.temperatures[TEMP_CHANNEL_A].last_error);
    TEST_ASSERT_TRUE(
        fake.last_dht_event[TEMP_CHANNEL_A].data.dht.frame_available);
    TEST_ASSERT_EQ(250,
                   fake.last_dht_event[TEMP_CHANNEL_A].data.dht.frame
                       .temperature_deci_c);
}

static void not_ready_times_out_at_deadline(void)
{
    FakePlatform fake;
    Phase1Platform platform;
    AcquisitionContext acquisition;
    DeviceModel model;

    fake_init(&fake, &platform);
    fake.dht_read_status[TEMP_CHANNEL_A] = PLATFORM_I2C_NOT_READY;
    device_model_init(&model);
    TEST_ASSERT_TRUE(acquisition_init(
        &acquisition, &platform, observe_event, &fake, true, 0u));
    initialize_channel(&acquisition, &model, &fake, 0u);
    service_at(&acquisition, &model, &fake, 2001u);
    service_at(&acquisition, &model, &fake, 2051u);
    TEST_ASSERT_EQ(1, fake.dht_event_count[TEMP_CHANNEL_A]);
    service_at(&acquisition, &model, &fake, 2251u);
    TEST_ASSERT_EQ(2, fake.dht_event_count[TEMP_CHANNEL_A]);
    TEST_ASSERT_EQ(ACQ_ERROR_I2C_TIMEOUT,
                   model.temperatures[TEMP_CHANNEL_A].last_error);
    service_at(&acquisition, &model, &fake, 4001u);
    TEST_ASSERT_EQ(2, fake.dht_recover_count[TEMP_CHANNEL_A]);
    TEST_ASSERT_EQ(1, fake.dht_measure_count[TEMP_CHANNEL_A]);
}

static void dht_read_bus_failure_restarts_recovery(void)
{
    FakePlatform fake;
    Phase1Platform platform;
    AcquisitionContext acquisition;
    DeviceModel model;

    fake_init(&fake, &platform);
    fake.dht_read_status[TEMP_CHANNEL_A] = PLATFORM_BUS_ERROR;
    device_model_init(&model);
    TEST_ASSERT_TRUE(acquisition_init(
        &acquisition, &platform, observe_event, &fake, true, 0u));
    initialize_channel(&acquisition, &model, &fake, 0u);

    service_at(&acquisition, &model, &fake, 2001u);
    service_at(&acquisition, &model, &fake, 2051u);
    TEST_ASSERT_EQ(ACQ_ERROR_I2C_BUS,
                   model.temperatures[TEMP_CHANNEL_A].last_error);
    service_at(&acquisition, &model, &fake, 4001u);
    TEST_ASSERT_EQ(2, fake.dht_recover_count[TEMP_CHANNEL_A]);
    TEST_ASSERT_EQ(1, fake.dht_measure_count[TEMP_CHANNEL_A]);
}

static void one_dht_failure_does_not_block_other_channels(void)
{
    FakePlatform fake;
    Phase1Platform platform;
    AcquisitionContext acquisition;
    DeviceModel model;

    fake_init(&fake, &platform);
    fake.dht_measure_status[TEMP_CHANNEL_A] = PLATFORM_BUS_ERROR;
    device_model_init(&model);
    TEST_ASSERT_TRUE(acquisition_init(
        &acquisition, &platform, observe_event, &fake, true, 0u));

    initialize_channel(&acquisition, &model, &fake, 0u);
    initialize_channel(&acquisition, &model, &fake, 25u);
    service_at(&acquisition, &model, &fake, 2001u);
    TEST_ASSERT_EQ(ACQ_ERROR_I2C_TRIGGER,
                   model.temperatures[TEMP_CHANNEL_A].last_error);
    service_at(&acquisition, &model, &fake, 2026u);
    service_at(&acquisition, &model, &fake, 2076u);

    TEST_ASSERT_TRUE(model.temperatures[TEMP_CHANNEL_B].valid);
    TEST_ASSERT_EQ(250, model.temperatures[TEMP_CHANNEL_B].value_deci_c);
    TEST_ASSERT_FALSE(model.temperatures[TEMP_CHANNEL_A].valid);
}

static void crc_fault_sets_on_third_cycle_and_success_recovers(void)
{
    FakePlatform fake;
    Phase1Platform platform;
    AcquisitionContext acquisition;
    DeviceModel model;
    unsigned int cycle;

    fake_init(&fake, &platform);
    fake.dht_frame[TEMP_CHANNEL_A][2] ^= 0x01u;
    device_model_init(&model);
    TEST_ASSERT_TRUE(acquisition_init(
        &acquisition, &platform, observe_event, &fake, true, 0u));

    initialize_channel(&acquisition, &model, &fake, 0u);
    for (cycle = 0u; cycle < 3u; ++cycle) {
        const uint32_t start = 2001u + cycle * 2000u;
        service_at(&acquisition, &model, &fake, start);
        service_at(&acquisition, &model, &fake, start + 50u);
    }
    TEST_ASSERT_TRUE((model.status_bits & STATUS_A_SENSOR_FAULT) != 0u);

    set_valid_frame(fake.dht_frame[TEMP_CHANNEL_A]);
    service_at(&acquisition, &model, &fake, 8001u);
    service_at(&acquisition, &model, &fake, 8051u);
    TEST_ASSERT_FALSE((model.status_bits & STATUS_A_SENSOR_FAULT) != 0u);
    TEST_ASSERT_TRUE(model.temperatures[TEMP_CHANNEL_A].valid);
}

static void adc_batch_averages_sixteen_samples(void)
{
    FakePlatform fake;
    Phase1Platform platform;
    AcquisitionContext acquisition;
    DeviceModel model;
    unsigned int index;

    fake_init(&fake, &platform);
    for (index = 0u; index < ADC_BATCH_SAMPLE_COUNT; ++index) {
        fake.adc_values[index] = (index & 1u) == 0u ? 0u : 4095u;
    }
    device_model_init(&model);
    TEST_ASSERT_TRUE(acquisition_init(
        &acquisition, &platform, observe_event, &fake, true, 0u));

    service_at(&acquisition, &model, &fake, 75u);
    for (index = 0u; index < (ADC_BATCH_SAMPLE_COUNT * 2u); ++index) {
        service_at(&acquisition, &model, &fake, 76u + index);
    }

    TEST_ASSERT_TRUE(model.light.valid);
    TEST_ASSERT_EQ(1650, model.light.value_mv);
    TEST_ASSERT_EQ(16, fake.adc_start_count);
}

static void adc_out_of_range_sets_fault(void)
{
    FakePlatform fake;
    Phase1Platform platform;
    AcquisitionContext acquisition;
    DeviceModel model;

    fake_init(&fake, &platform);
    fake.adc_values[0] = 4096u;
    device_model_init(&model);
    TEST_ASSERT_TRUE(acquisition_init(
        &acquisition, &platform, observe_event, &fake, true, 0u));
    service_at(&acquisition, &model, &fake, 75u);
    service_at(&acquisition, &model, &fake, 76u);
    service_at(&acquisition, &model, &fake, 77u);

    TEST_ASSERT_TRUE((model.status_bits & STATUS_LIGHT_ADC_FAULT) != 0u);
    TEST_ASSERT_EQ(ACQ_ERROR_ADC_OUT_OF_RANGE, model.light.last_error);
}

static void schedule_handles_millisecond_wrap(void)
{
    FakePlatform fake;
    Phase1Platform platform;
    AcquisitionContext acquisition;
    DeviceModel model;
    const uint32_t start = UINT32_MAX - 10u;

    fake_init(&fake, &platform);
    device_model_init(&model);
    TEST_ASSERT_TRUE(acquisition_init(
        &acquisition, &platform, observe_event, &fake, true, start));
    initialize_channel(&acquisition, &model, &fake, start);
    service_at(&acquisition, &model, &fake, 1989u);
    TEST_ASSERT_EQ(0, fake.dht_measure_count[TEMP_CHANNEL_A]);
    service_at(&acquisition, &model, &fake, 1990u);
    TEST_ASSERT_EQ(1, fake.dht_measure_count[TEMP_CHANNEL_A]);
}

void register_acquisition_tests(void)
{
    test_run("DHT initialization order and wait",
             dht_initialization_recovers_resets_and_waits);
    test_run("DHT recovery failure blocks reset",
             dht_recovery_failure_prevents_reset);
    test_run("DHT reset failure restarts recovery",
             dht_reset_failure_restarts_recovery);
    test_run("DHT schedule and interval",
             dht_schedule_publishes_and_respects_interval);
    test_run("DHT unconfirmed conversion gate",
             unconfirmed_conversion_does_not_publish);
    test_run("DHT ready timeout", not_ready_times_out_at_deadline);
    test_run("DHT read failure restarts recovery",
             dht_read_bus_failure_restarts_recovery);
    test_run("DHT channels remain independent",
             one_dht_failure_does_not_block_other_channels);
    test_run("DHT CRC fault recovery",
             crc_fault_sets_on_third_cycle_and_success_recovers);
    test_run("ADC sixteen sample average", adc_batch_averages_sixteen_samples);
    test_run("ADC out of range", adc_out_of_range_sets_fault);
    test_run("scheduler millisecond wrap", schedule_handles_millisecond_wrap);
}

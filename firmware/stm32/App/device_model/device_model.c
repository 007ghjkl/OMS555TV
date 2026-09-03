#include "device_model.h"

#include <stddef.h>
#include <string.h>

#include "alarm.h"

#define TEMPERATURE_MIN_DECI_C (-400)
#define TEMPERATURE_MAX_DECI_C 800
#define LIGHT_MAX_MV 3300u
#define FAILURE_LIMIT 3u

static uint16_t alarm_mask_for_channel(TemperatureChannel channel)
{
    static const uint16_t masks[TEMP_CHANNEL_COUNT] = {
        ALARM_A_PHASE_TEMP_HIGH,
        ALARM_B_PHASE_TEMP_HIGH,
        ALARM_C_PHASE_TEMP_HIGH,
        ALARM_AMBIENT_TEMP_HIGH
    };

    return channel < TEMP_CHANNEL_COUNT ? masks[channel] : 0u;
}

static uint16_t fault_mask_for_channel(TemperatureChannel channel)
{
    static const uint16_t masks[TEMP_CHANNEL_COUNT] = {
        STATUS_A_SENSOR_FAULT,
        STATUS_B_SENSOR_FAULT,
        STATUS_C_SENSOR_FAULT,
        0u
    };

    return channel < TEMP_CHANNEL_COUNT ? masks[channel] : 0u;
}

static uint8_t increment_saturated(uint8_t value)
{
    return value < FAILURE_LIMIT ? (uint8_t)(value + 1u) : FAILURE_LIMIT;
}

static void update_alarm(DeviceModel *model, TemperatureChannel channel)
{
    const uint16_t mask = alarm_mask_for_channel(channel);
    const TemperatureState *temperature = &model->temperatures[channel];
    const bool current = (model->alarm_bits & mask) != 0u;

    if (!temperature->valid) {
        return;
    }

    if (alarm_update(current,
                     temperature->value_deci_c,
                     model->thresholds_deci_c[channel])) {
        model->alarm_bits |= mask;
    } else {
        model->alarm_bits &= (uint16_t)~mask;
    }
}

void device_model_init(DeviceModel *model)
{
    if (model == NULL) {
        return;
    }

    memset(model, 0, sizeof(*model));
    model->thresholds_deci_c[TEMP_CHANNEL_A] = 600;
    model->thresholds_deci_c[TEMP_CHANNEL_B] = 600;
    model->thresholds_deci_c[TEMP_CHANNEL_C] = 600;
    model->thresholds_deci_c[TEMP_CHANNEL_AMBIENT] = 400;
    model->temperatures[TEMP_CHANNEL_AMBIENT].value_deci_c = 250;
    model->temperatures[TEMP_CHANNEL_AMBIENT].valid = true;
    model->temperatures[TEMP_CHANNEL_AMBIENT].last_error = ACQ_ERROR_NONE;
    model->status_bits = STATUS_AMBIENT_SIMULATED;
    model->firmware_version_major = 0u;
    model->firmware_version_minor = 2u;
    update_alarm(model, TEMP_CHANNEL_AMBIENT);
}

void device_model_set_running(DeviceModel *model, bool running)
{
    if (model == NULL) {
        return;
    }

    if (running) {
        model->status_bits |= STATUS_RUNNING;
    } else {
        model->status_bits &= (uint16_t)~STATUS_RUNNING;
    }

    model->status_bits |= STATUS_AMBIENT_SIMULATED;
}

bool device_model_set_threshold(DeviceModel *model,
                                TemperatureChannel channel,
                                int16_t threshold_deci_c)
{
    if (model == NULL || channel >= TEMP_CHANNEL_COUNT ||
        threshold_deci_c < TEMPERATURE_MIN_DECI_C ||
        threshold_deci_c > TEMPERATURE_MAX_DECI_C) {
        return false;
    }

    model->thresholds_deci_c[channel] = threshold_deci_c;
    update_alarm(model, channel);
    return true;
}

bool device_model_publish_temperature(DeviceModel *model,
                                      TemperatureChannel channel,
                                      int16_t value_deci_c,
                                      uint32_t now_ms)
{
    TemperatureState *state;
    const uint16_t fault_mask = fault_mask_for_channel(channel);

    if (model == NULL || channel >= TEMP_CHANNEL_COUNT ||
        value_deci_c < TEMPERATURE_MIN_DECI_C ||
        value_deci_c > TEMPERATURE_MAX_DECI_C) {
        return false;
    }

    state = &model->temperatures[channel];
    state->value_deci_c = value_deci_c;
    state->valid = true;
    state->updated_at_ms = now_ms;
    state->last_error = ACQ_ERROR_NONE;
    state->consecutive_crc_failures = 0u;
    state->consecutive_invalid_cycles = 0u;
    model->status_bits &= (uint16_t)~fault_mask;
    model->status_bits |= STATUS_AMBIENT_SIMULATED;
    update_alarm(model, channel);
    return true;
}

void device_model_fail_temperature(DeviceModel *model,
                                   TemperatureChannel channel,
                                   AcquisitionError error,
                                   bool crc_failure)
{
    TemperatureState *state;
    const uint16_t fault_mask = fault_mask_for_channel(channel);

    if (model == NULL || channel >= TEMP_CHANNEL_AMBIENT) {
        return;
    }

    state = &model->temperatures[channel];
    state->valid = false;
    state->last_error = error;
    state->consecutive_invalid_cycles =
        increment_saturated(state->consecutive_invalid_cycles);
    state->consecutive_crc_failures = crc_failure
        ? increment_saturated(state->consecutive_crc_failures)
        : 0u;

    if (state->consecutive_invalid_cycles >= FAILURE_LIMIT ||
        state->consecutive_crc_failures >= FAILURE_LIMIT) {
        model->status_bits |= fault_mask;
    }

    model->status_bits |= STATUS_AMBIENT_SIMULATED;
}

bool device_model_publish_light(DeviceModel *model,
                                uint16_t value_mv,
                                uint32_t now_ms)
{
    if (model == NULL || value_mv > LIGHT_MAX_MV) {
        return false;
    }

    model->light.value_mv = value_mv;
    model->light.valid = true;
    model->light.updated_at_ms = now_ms;
    model->light.last_error = ACQ_ERROR_NONE;
    model->status_bits &= (uint16_t)~STATUS_LIGHT_ADC_FAULT;
    model->status_bits |= STATUS_AMBIENT_SIMULATED;
    return true;
}

void device_model_fail_light(DeviceModel *model, AcquisitionError error)
{
    if (model == NULL) {
        return;
    }

    model->light.valid = false;
    model->light.last_error = error;
    model->status_bits |= STATUS_LIGHT_ADC_FAULT;
    model->status_bits |= STATUS_AMBIENT_SIMULATED;
}

void device_model_add_communication_errors(DeviceModel *model,
                                           uint16_t count)
{
    const uint32_t total = model != NULL
        ? (uint32_t)model->communication_error_count + count
        : 0u;

    if (model == NULL) {
        return;
    }
    model->communication_error_count = total > UINT16_MAX
        ? UINT16_MAX
        : (uint16_t)total;
}

const char *acquisition_error_name(AcquisitionError error)
{
    static const char *const names[] = {
        "NONE",
        "I2C_RECOVERY",
        "I2C_RESET",
        "I2C_TRIGGER",
        "I2C_TIMEOUT",
        "I2C_BUS",
        "SHORT_FRAME",
        "TEMP_CRC",
        "HUMIDITY_CRC",
        "TEMP_CONVERSION_UNCONFIRMED",
        "TEMP_OUT_OF_RANGE",
        "ADC_START",
        "ADC_TIMEOUT",
        "ADC_READ",
        "ADC_OUT_OF_RANGE"
    };

    return (unsigned int)error < (sizeof(names) / sizeof(names[0]))
        ? names[error]
        : "UNKNOWN";
}

const char *temperature_channel_name(TemperatureChannel channel)
{
    static const char *const names[TEMP_CHANNEL_COUNT] = {
        "A", "B", "C", "AMBIENT"
    };

    return channel < TEMP_CHANNEL_COUNT ? names[channel] : "UNKNOWN";
}

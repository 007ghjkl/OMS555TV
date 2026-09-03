#include "register_map.h"

#include <stddef.h>

enum {
    REGISTER_TEMP_A = 0u,
    REGISTER_TEMP_B = 1u,
    REGISTER_TEMP_C = 2u,
    REGISTER_TEMP_AMBIENT = 3u,
    REGISTER_LIGHT_MV = 4u,
    REGISTER_THRESHOLD_A = 9u,
    REGISTER_THRESHOLD_B = 10u,
    REGISTER_THRESHOLD_C = 11u,
    REGISTER_THRESHOLD_AMBIENT = 12u,
    REGISTER_ALARM_BITS = 19u,
    REGISTER_STATUS_BITS = 20u,
    REGISTER_COMMUNICATION_ERRORS = 29u,
    REGISTER_UPTIME_LOW = 30u,
    REGISTER_UPTIME_HIGH = 31u,
    REGISTER_FIRMWARE_MAJOR = 39u,
    REGISTER_FIRMWARE_MINOR = 40u
};

typedef struct {
    int16_t temperatures[TEMP_CHANNEL_COUNT];
    uint16_t light_mv;
    int16_t thresholds[TEMP_CHANNEL_COUNT];
    uint16_t alarm_bits;
    uint16_t status_bits;
    uint16_t communication_errors;
    uint32_t uptime_seconds;
    uint16_t firmware_major;
    uint16_t firmware_minor;
} RegisterSnapshot;

static bool address_is_readable(uint16_t address)
{
    return address <= REGISTER_LIGHT_MV ||
           (address >= REGISTER_THRESHOLD_A &&
            address <= REGISTER_THRESHOLD_AMBIENT) ||
           (address >= REGISTER_ALARM_BITS &&
            address <= REGISTER_STATUS_BITS) ||
           (address >= REGISTER_COMMUNICATION_ERRORS &&
            address <= REGISTER_UPTIME_HIGH) ||
           (address >= REGISTER_FIRMWARE_MAJOR &&
            address <= REGISTER_FIRMWARE_MINOR);
}

static RegisterSnapshot make_snapshot(const DeviceModel *model)
{
    RegisterSnapshot snapshot;
    unsigned int channel;

    for (channel = 0u; channel < TEMP_CHANNEL_COUNT; ++channel) {
        snapshot.temperatures[channel] =
            model->temperatures[channel].value_deci_c;
        snapshot.thresholds[channel] = model->thresholds_deci_c[channel];
    }
    snapshot.light_mv = model->light.value_mv;
    snapshot.alarm_bits = model->alarm_bits & 0x000Fu;
    snapshot.status_bits = model->status_bits & 0x003Fu;
    snapshot.communication_errors = model->communication_error_count;
    snapshot.uptime_seconds = model->uptime_seconds;
    snapshot.firmware_major = model->firmware_version_major;
    snapshot.firmware_minor = model->firmware_version_minor;
    return snapshot;
}

static uint16_t snapshot_value(const RegisterSnapshot *snapshot,
                               uint16_t address)
{
    if (address <= REGISTER_TEMP_AMBIENT) {
        return (uint16_t)snapshot->temperatures[address];
    }
    if (address == REGISTER_LIGHT_MV) {
        return snapshot->light_mv;
    }
    if (address >= REGISTER_THRESHOLD_A &&
        address <= REGISTER_THRESHOLD_AMBIENT) {
        return (uint16_t)snapshot->thresholds[
            address - REGISTER_THRESHOLD_A];
    }
    if (address == REGISTER_ALARM_BITS) {
        return snapshot->alarm_bits;
    }
    if (address == REGISTER_STATUS_BITS) {
        return snapshot->status_bits;
    }
    if (address == REGISTER_COMMUNICATION_ERRORS) {
        return snapshot->communication_errors;
    }
    if (address == REGISTER_UPTIME_LOW) {
        return (uint16_t)(snapshot->uptime_seconds & 0xFFFFu);
    }
    if (address == REGISTER_UPTIME_HIGH) {
        return (uint16_t)(snapshot->uptime_seconds >> 16u);
    }
    if (address == REGISTER_FIRMWARE_MAJOR) {
        return snapshot->firmware_major;
    }
    return snapshot->firmware_minor;
}

bool register_map_read_holding(const DeviceModel *model,
                               uint16_t start_address,
                               uint16_t quantity,
                               uint16_t *values)
{
    RegisterSnapshot snapshot;
    uint32_t end_address;
    uint16_t index;

    if (model == NULL || values == NULL || quantity == 0u) {
        return false;
    }
    end_address = (uint32_t)start_address + quantity;
    if (end_address > 0x10000u) {
        return false;
    }
    for (index = 0u; index < quantity; ++index) {
        if (!address_is_readable((uint16_t)(start_address + index))) {
            return false;
        }
    }

    snapshot = make_snapshot(model);
    for (index = 0u; index < quantity; ++index) {
        values[index] = snapshot_value(
            &snapshot, (uint16_t)(start_address + index));
    }
    return true;
}

RegisterWriteResult register_map_write_single(DeviceModel *model,
                                               uint16_t address,
                                               uint16_t raw_value)
{
    TemperatureChannel channel;
    int32_t signed_value;

    if (model == NULL || address < REGISTER_THRESHOLD_A ||
        address > REGISTER_THRESHOLD_AMBIENT) {
        return REGISTER_WRITE_ILLEGAL_ADDRESS;
    }

    signed_value = raw_value <= 0x7FFFu
        ? (int32_t)raw_value
        : (int32_t)raw_value - 0x10000L;
    if (signed_value < -400 || signed_value > 800) {
        return REGISTER_WRITE_ILLEGAL_VALUE;
    }

    channel = (TemperatureChannel)(address - REGISTER_THRESHOLD_A);
    return device_model_set_threshold(model, channel, (int16_t)signed_value)
        ? REGISTER_WRITE_OK
        : REGISTER_WRITE_ILLEGAL_VALUE;
}

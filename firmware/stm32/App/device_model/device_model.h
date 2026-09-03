#ifndef OMS555TV_DEVICE_MODEL_H
#define OMS555TV_DEVICE_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TEMP_CHANNEL_A = 0,
    TEMP_CHANNEL_B,
    TEMP_CHANNEL_C,
    TEMP_CHANNEL_AMBIENT,
    TEMP_CHANNEL_COUNT
} TemperatureChannel;

typedef enum {
    ACQ_ERROR_NONE = 0,
    ACQ_ERROR_I2C_RECOVERY,
    ACQ_ERROR_I2C_RESET,
    ACQ_ERROR_I2C_TRIGGER,
    ACQ_ERROR_I2C_TIMEOUT,
    ACQ_ERROR_I2C_BUS,
    ACQ_ERROR_SHORT_FRAME,
    ACQ_ERROR_TEMP_CRC,
    ACQ_ERROR_HUMIDITY_CRC,
    ACQ_ERROR_TEMP_CONVERSION_UNCONFIRMED,
    ACQ_ERROR_TEMP_OUT_OF_RANGE,
    ACQ_ERROR_ADC_START,
    ACQ_ERROR_ADC_TIMEOUT,
    ACQ_ERROR_ADC_READ,
    ACQ_ERROR_ADC_OUT_OF_RANGE
} AcquisitionError;

enum {
    ALARM_A_PHASE_TEMP_HIGH = (1u << 0),
    ALARM_B_PHASE_TEMP_HIGH = (1u << 1),
    ALARM_C_PHASE_TEMP_HIGH = (1u << 2),
    ALARM_AMBIENT_TEMP_HIGH = (1u << 3)
};

enum {
    STATUS_RUNNING = (1u << 0),
    STATUS_A_SENSOR_FAULT = (1u << 1),
    STATUS_B_SENSOR_FAULT = (1u << 2),
    STATUS_C_SENSOR_FAULT = (1u << 3),
    STATUS_LIGHT_ADC_FAULT = (1u << 4),
    STATUS_AMBIENT_SIMULATED = (1u << 5)
};

typedef struct {
    int16_t value_deci_c;
    bool valid;
    uint32_t updated_at_ms;
    AcquisitionError last_error;
    uint8_t consecutive_crc_failures;
    uint8_t consecutive_invalid_cycles;
} TemperatureState;

typedef struct {
    uint16_t value_mv;
    bool valid;
    uint32_t updated_at_ms;
    AcquisitionError last_error;
} LightState;

typedef struct {
    TemperatureState temperatures[TEMP_CHANNEL_COUNT];
    LightState light;
    int16_t thresholds_deci_c[TEMP_CHANNEL_COUNT];
    uint16_t alarm_bits;
    uint16_t status_bits;
    uint16_t communication_error_count;
    uint32_t uptime_seconds;
    uint16_t firmware_version_major;
    uint16_t firmware_version_minor;
} DeviceModel;

void device_model_init(DeviceModel *model);
void device_model_set_running(DeviceModel *model, bool running);

bool device_model_set_threshold(DeviceModel *model,
                                TemperatureChannel channel,
                                int16_t threshold_deci_c);

bool device_model_publish_temperature(DeviceModel *model,
                                      TemperatureChannel channel,
                                      int16_t value_deci_c,
                                      uint32_t now_ms);

void device_model_fail_temperature(DeviceModel *model,
                                   TemperatureChannel channel,
                                   AcquisitionError error,
                                   bool crc_failure);

bool device_model_publish_light(DeviceModel *model,
                                uint16_t value_mv,
                                uint32_t now_ms);

void device_model_fail_light(DeviceModel *model, AcquisitionError error);

void device_model_add_communication_errors(DeviceModel *model,
                                           uint16_t count);

const char *acquisition_error_name(AcquisitionError error);
const char *temperature_channel_name(TemperatureChannel channel);

#ifdef __cplusplus
}
#endif

#endif

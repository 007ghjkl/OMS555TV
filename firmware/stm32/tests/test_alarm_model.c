#include "test_framework.h"

#include "alarm.h"
#include "device_model.h"

static void alarm_obeys_threshold_and_hysteresis(void)
{
    bool current = false;

    current = alarm_update(current, 599, 600);
    TEST_ASSERT_FALSE(current);
    current = alarm_update(current, 600, 600);
    TEST_ASSERT_TRUE(current);
    current = alarm_update(current, 551, 600);
    TEST_ASSERT_TRUE(current);
    current = alarm_update(current, 550, 600);
    TEST_ASSERT_FALSE(current);
}

static void model_initializes_deterministically(void)
{
    DeviceModel model;

    device_model_init(&model);
    TEST_ASSERT_EQ(600, model.thresholds_deci_c[TEMP_CHANNEL_A]);
    TEST_ASSERT_EQ(400, model.thresholds_deci_c[TEMP_CHANNEL_AMBIENT]);
    TEST_ASSERT_EQ(250,
                   model.temperatures[TEMP_CHANNEL_AMBIENT].value_deci_c);
    TEST_ASSERT_TRUE(model.temperatures[TEMP_CHANNEL_AMBIENT].valid);
    TEST_ASSERT_EQ(STATUS_AMBIENT_SIMULATED, model.status_bits);
    TEST_ASSERT_EQ(0, model.alarm_bits);
    TEST_ASSERT_EQ(0, model.firmware_version_major);
    TEST_ASSERT_EQ(1, model.firmware_version_minor);
}

static void temperature_failure_sets_fault_on_third_cycle_and_recovers(void)
{
    DeviceModel model;

    device_model_init(&model);
    TEST_ASSERT_TRUE(device_model_publish_temperature(
        &model, TEMP_CHANNEL_A, 610, 100u));
    TEST_ASSERT_TRUE((model.alarm_bits & ALARM_A_PHASE_TEMP_HIGH) != 0u);

    device_model_fail_temperature(
        &model, TEMP_CHANNEL_A, ACQ_ERROR_TEMP_CRC, true);
    device_model_fail_temperature(
        &model, TEMP_CHANNEL_A, ACQ_ERROR_TEMP_CRC, true);
    TEST_ASSERT_FALSE((model.status_bits & STATUS_A_SENSOR_FAULT) != 0u);
    device_model_fail_temperature(
        &model, TEMP_CHANNEL_A, ACQ_ERROR_TEMP_CRC, true);
    TEST_ASSERT_TRUE((model.status_bits & STATUS_A_SENSOR_FAULT) != 0u);
    TEST_ASSERT_EQ(610, model.temperatures[TEMP_CHANNEL_A].value_deci_c);
    TEST_ASSERT_FALSE(model.temperatures[TEMP_CHANNEL_A].valid);
    TEST_ASSERT_TRUE((model.alarm_bits & ALARM_A_PHASE_TEMP_HIGH) != 0u);

    TEST_ASSERT_TRUE(device_model_publish_temperature(
        &model, TEMP_CHANNEL_A, 540, 7000u));
    TEST_ASSERT_FALSE((model.status_bits & STATUS_A_SENSOR_FAULT) != 0u);
    TEST_ASSERT_FALSE((model.alarm_bits & ALARM_A_PHASE_TEMP_HIGH) != 0u);
    TEST_ASSERT_EQ(0,
                   model.temperatures[TEMP_CHANNEL_A]
                       .consecutive_invalid_cycles);
}

static void non_crc_error_breaks_crc_sequence(void)
{
    DeviceModel model;

    device_model_init(&model);
    device_model_fail_temperature(
        &model, TEMP_CHANNEL_B, ACQ_ERROR_TEMP_CRC, true);
    device_model_fail_temperature(
        &model, TEMP_CHANNEL_B, ACQ_ERROR_I2C_BUS, false);
    TEST_ASSERT_EQ(0,
                   model.temperatures[TEMP_CHANNEL_B]
                       .consecutive_crc_failures);
    TEST_ASSERT_EQ(2,
                   model.temperatures[TEMP_CHANNEL_B]
                       .consecutive_invalid_cycles);
}

static void threshold_validation_and_immediate_recheck_work(void)
{
    DeviceModel model;

    device_model_init(&model);
    TEST_ASSERT_TRUE(device_model_publish_temperature(
        &model, TEMP_CHANNEL_C, 300, 1u));
    TEST_ASSERT_TRUE(
        device_model_set_threshold(&model, TEMP_CHANNEL_C, 300));
    TEST_ASSERT_TRUE((model.alarm_bits & ALARM_C_PHASE_TEMP_HIGH) != 0u);
    TEST_ASSERT_FALSE(
        device_model_set_threshold(&model, TEMP_CHANNEL_C, 801));
    TEST_ASSERT_EQ(300, model.thresholds_deci_c[TEMP_CHANNEL_C]);
}

static void light_fault_preserves_value_and_recovers(void)
{
    DeviceModel model;

    device_model_init(&model);
    TEST_ASSERT_TRUE(device_model_publish_light(&model, 1650u, 10u));
    device_model_fail_light(&model, ACQ_ERROR_ADC_TIMEOUT);
    TEST_ASSERT_EQ(1650, model.light.value_mv);
    TEST_ASSERT_FALSE(model.light.valid);
    TEST_ASSERT_TRUE((model.status_bits & STATUS_LIGHT_ADC_FAULT) != 0u);
    TEST_ASSERT_TRUE(device_model_publish_light(&model, 1700u, 20u));
    TEST_ASSERT_FALSE((model.status_bits & STATUS_LIGHT_ADC_FAULT) != 0u);
}

void register_alarm_model_tests(void)
{
    test_run("alarm threshold and hysteresis",
             alarm_obeys_threshold_and_hysteresis);
    test_run("model deterministic defaults", model_initializes_deterministically);
    test_run("temperature fault and recovery",
             temperature_failure_sets_fault_on_third_cycle_and_recovers);
    test_run("mixed error breaks CRC sequence",
             non_crc_error_breaks_crc_sequence);
    test_run("threshold validation", threshold_validation_and_immediate_recheck_work);
    test_run("light fault recovery", light_fault_preserves_value_and_recovers);
}

#include "test_framework.h"

#include <limits.h>

#include "register_map.h"

static void register_map_reads_all_defined_blocks(void)
{
    DeviceModel model;
    uint16_t values[5];

    device_model_init(&model);
    model.temperatures[TEMP_CHANNEL_A].value_deci_c = -123;
    model.temperatures[TEMP_CHANNEL_B].value_deci_c = 234;
    model.temperatures[TEMP_CHANNEL_C].value_deci_c = 345;
    model.light.value_mv = 1650u;
    model.alarm_bits = 0xFFF5u;
    model.status_bits = 0xFFE1u;
    model.communication_error_count = 77u;
    model.uptime_seconds = 0x1234ABCDu;
    model.firmware_version_major = 2u;
    model.firmware_version_minor = 7u;

    TEST_ASSERT_TRUE(register_map_read_holding(&model, 0u, 5u, values));
    TEST_ASSERT_EQ((uint16_t)-123, values[0]);
    TEST_ASSERT_EQ(234, values[1]);
    TEST_ASSERT_EQ(345, values[2]);
    TEST_ASSERT_EQ(250, values[3]);
    TEST_ASSERT_EQ(1650, values[4]);

    TEST_ASSERT_TRUE(register_map_read_holding(&model, 19u, 2u, values));
    TEST_ASSERT_EQ(0x0005, values[0]);
    TEST_ASSERT_EQ(0x0021, values[1]);
    TEST_ASSERT_TRUE(register_map_read_holding(&model, 29u, 3u, values));
    TEST_ASSERT_EQ(77, values[0]);
    TEST_ASSERT_EQ(0xABCD, values[1]);
    TEST_ASSERT_EQ(0x1234, values[2]);
    TEST_ASSERT_TRUE(register_map_read_holding(&model, 39u, 2u, values));
    TEST_ASSERT_EQ(2, values[0]);
    TEST_ASSERT_EQ(7, values[1]);
}

static void register_map_rejects_holes_and_overflow(void)
{
    DeviceModel model;
    uint16_t values[8] = {0u};

    device_model_init(&model);
    TEST_ASSERT_FALSE(register_map_read_holding(&model, 4u, 2u, values));
    TEST_ASSERT_FALSE(register_map_read_holding(&model, 12u, 8u, values));
    TEST_ASSERT_FALSE(register_map_read_holding(&model, UINT16_MAX, 2u, values));
    TEST_ASSERT_FALSE(register_map_read_holding(&model, 0u, 0u, values));
}

static void register_map_writes_thresholds_atomically(void)
{
    DeviceModel model;

    device_model_init(&model);
    TEST_ASSERT_TRUE(device_model_publish_temperature(
        &model, TEMP_CHANNEL_A, 600, 1u));
    TEST_ASSERT_EQ(REGISTER_WRITE_OK,
                   register_map_write_single(&model, 9u, 0xFE70u));
    TEST_ASSERT_EQ(-400, model.thresholds_deci_c[TEMP_CHANNEL_A]);
    TEST_ASSERT_TRUE((model.alarm_bits & ALARM_A_PHASE_TEMP_HIGH) != 0u);
    TEST_ASSERT_EQ(REGISTER_WRITE_OK,
                   register_map_write_single(&model, 12u, 800u));
    TEST_ASSERT_EQ(800,
                   model.thresholds_deci_c[TEMP_CHANNEL_AMBIENT]);
    TEST_ASSERT_EQ(REGISTER_WRITE_ILLEGAL_VALUE,
                   register_map_write_single(&model, 9u, 801u));
    TEST_ASSERT_EQ(-400, model.thresholds_deci_c[TEMP_CHANNEL_A]);
    TEST_ASSERT_EQ(REGISTER_WRITE_ILLEGAL_VALUE,
                   register_map_write_single(&model, 9u, 0xFE6Fu));
    TEST_ASSERT_EQ(REGISTER_WRITE_ILLEGAL_ADDRESS,
                   register_map_write_single(&model, 0u, 600u));
}

static void communication_error_count_saturates(void)
{
    DeviceModel model;

    device_model_init(&model);
    model.communication_error_count = 65534u;
    device_model_add_communication_errors(&model, 1u);
    TEST_ASSERT_EQ(65535, model.communication_error_count);
    device_model_add_communication_errors(&model, 10u);
    TEST_ASSERT_EQ(65535, model.communication_error_count);
}

void register_register_map_tests(void)
{
    test_run("register map defined blocks",
             register_map_reads_all_defined_blocks);
    test_run("register map holes", register_map_rejects_holes_and_overflow);
    test_run("register map threshold writes",
             register_map_writes_thresholds_atomically);
    test_run("communication errors saturate",
             communication_error_count_saturates);
}

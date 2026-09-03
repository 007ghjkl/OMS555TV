#include "test_framework.h"

#include <string.h>

#include "modbus_crc.h"
#include "modbus_rtu_rx.h"
#include "modbus_slave.h"

static size_t finish_request(uint8_t *frame, size_t data_length)
{
    const uint16_t crc = modbus_crc16(frame, data_length);
    frame[data_length] = (uint8_t)(crc & 0xFFu);
    frame[data_length + 1u] = (uint8_t)(crc >> 8u);
    return data_length + 2u;
}

static void assert_exception(const uint8_t *response,
                             const ModbusProcessOutcome *outcome,
                             uint8_t function,
                             uint8_t code)
{
    TEST_ASSERT_EQ(MODBUS_PROCESS_RESPONSE_READY, outcome->result);
    TEST_ASSERT_EQ(5, outcome->response_length);
    TEST_ASSERT_EQ(MODBUS_SLAVE_ID, response[0]);
    TEST_ASSERT_EQ((uint8_t)(function | 0x80u), response[1]);
    TEST_ASSERT_EQ(code, response[2]);
    TEST_ASSERT_TRUE(modbus_crc16_valid(response, outcome->response_length));
}

static void slave_reads_holding_registers_big_endian(void)
{
    DeviceModel model;
    uint8_t request[8] = {1u, 3u, 0u, 0u, 0u, 5u, 0u, 0u};
    uint8_t response[MODBUS_RTU_MAX_ADU] = {0u};
    ModbusProcessOutcome result;

    device_model_init(&model);
    model.temperatures[TEMP_CHANNEL_A].value_deci_c = -123;
    model.temperatures[TEMP_CHANNEL_B].value_deci_c = 234;
    model.temperatures[TEMP_CHANNEL_C].value_deci_c = 345;
    model.light.value_mv = 1650u;
    (void)finish_request(request, 6u);

    result = modbus_slave_process_adu(&model,
                                      request,
                                      sizeof(request),
                                      response,
                                      sizeof(response));
    TEST_ASSERT_EQ(MODBUS_PROCESS_RESPONSE_READY, result.result);
    TEST_ASSERT_EQ(15, result.response_length);
    TEST_ASSERT_EQ(10, response[2]);
    TEST_ASSERT_EQ(0xFF, response[3]);
    TEST_ASSERT_EQ(0x85, response[4]);
    TEST_ASSERT_EQ(0x00, response[5]);
    TEST_ASSERT_EQ(0xEA, response[6]);
    TEST_ASSERT_EQ(0x06, response[11]);
    TEST_ASSERT_EQ(0x72, response[12]);
    TEST_ASSERT_TRUE(modbus_crc16_valid(response, result.response_length));
}

static void slave_writes_and_echoes_threshold(void)
{
    DeviceModel model;
    uint8_t request[8] = {1u, 6u, 0u, 9u, 0x02u, 0x58u, 0u, 0u};
    uint8_t response[8] = {0u};
    ModbusProcessOutcome result;

    device_model_init(&model);
    (void)finish_request(request, 6u);
    result = modbus_slave_process_adu(&model,
                                      request,
                                      sizeof(request),
                                      response,
                                      sizeof(response));
    TEST_ASSERT_EQ(MODBUS_PROCESS_RESPONSE_READY, result.result);
    TEST_ASSERT_EQ(8, result.response_length);
    TEST_ASSERT_EQ(600, model.thresholds_deci_c[TEMP_CHANNEL_A]);
    TEST_ASSERT_TRUE(memcmp(request, response, sizeof(request)) == 0);
}

static void slave_returns_standard_exceptions(void)
{
    DeviceModel model;
    uint8_t request[10] = {0u};
    uint8_t response[8] = {0u};
    ModbusProcessOutcome result;
    size_t length;

    device_model_init(&model);
    request[0] = 1u;
    request[1] = 0x10u;
    length = finish_request(request, 2u);
    result = modbus_slave_process_adu(
        &model, request, length, response, sizeof(response));
    assert_exception(response, &result, 0x10u, 0x01u);

    memset(request, 0, sizeof(request));
    request[0] = 1u;
    request[1] = 3u;
    request[3] = 5u;
    request[5] = 1u;
    length = finish_request(request, 6u);
    result = modbus_slave_process_adu(
        &model, request, length, response, sizeof(response));
    assert_exception(response, &result, 3u, 0x02u);

    request[3] = 0u;
    request[5] = 0u;
    length = finish_request(request, 6u);
    result = modbus_slave_process_adu(
        &model, request, length, response, sizeof(response));
    assert_exception(response, &result, 3u, 0x03u);

    request[4] = 0u;
    request[5] = 126u;
    length = finish_request(request, 6u);
    result = modbus_slave_process_adu(
        &model, request, length, response, sizeof(response));
    assert_exception(response, &result, 3u, 0x03u);

    request[1] = 6u;
    request[3] = 9u;
    request[4] = 0x03u;
    request[5] = 0x21u;
    length = finish_request(request, 6u);
    result = modbus_slave_process_adu(
        &model, request, length, response, sizeof(response));
    assert_exception(response, &result, 6u, 0x03u);

    request[3] = 0u;
    request[4] = 0x02u;
    request[5] = 0x58u;
    length = finish_request(request, 6u);
    result = modbus_slave_process_adu(
        &model, request, length, response, sizeof(response));
    assert_exception(response, &result, 6u, 0x02u);
}

static void slave_silently_rejects_bad_frames(void)
{
    DeviceModel model;
    uint8_t request[8] = {1u, 3u, 0u, 0u, 0u, 1u, 0u, 0u};
    uint8_t response[8] = {0u};
    ModbusProcessOutcome result;

    device_model_init(&model);
    (void)finish_request(request, 6u);
    request[7] ^= 1u;
    result = modbus_slave_process_adu(&model,
                                      request,
                                      sizeof(request),
                                      response,
                                      sizeof(response));
    TEST_ASSERT_EQ(MODBUS_PROCESS_COMMUNICATION_ERROR, result.result);
    TEST_ASSERT_EQ(0, result.response_length);

    result = modbus_slave_process_adu(
        &model, request, 3u, response, sizeof(response));
    TEST_ASSERT_EQ(MODBUS_PROCESS_COMMUNICATION_ERROR, result.result);

    request[0] = 2u;
    result = modbus_slave_process_adu(&model,
                                      request,
                                      sizeof(request),
                                      response,
                                      sizeof(response));
    TEST_ASSERT_EQ(MODBUS_PROCESS_NO_RESPONSE, result.result);
    TEST_ASSERT_EQ(0, model.communication_error_count);
}

static void slave_rejects_extra_data_and_small_response_buffer(void)
{
    DeviceModel model;
    uint8_t request[9] = {1u, 3u, 0u, 0u, 0u, 1u, 0x99u, 0u, 0u};
    uint8_t response[8] = {0u};
    ModbusProcessOutcome result;
    size_t length;

    device_model_init(&model);
    length = finish_request(request, 7u);
    result = modbus_slave_process_adu(
        &model, request, length, response, sizeof(response));
    assert_exception(response, &result, 3u, 0x03u);

    request[6] = 0u;
    length = finish_request(request, 6u);
    result = modbus_slave_process_adu(
        &model, request, length, response, 2u);
    TEST_ASSERT_EQ(MODBUS_PROCESS_NO_RESPONSE, result.result);
    TEST_ASSERT_EQ(0, result.response_length);
}

void register_modbus_slave_tests(void)
{
    test_run("Modbus 0x03 register read",
             slave_reads_holding_registers_big_endian);
    test_run("Modbus 0x06 threshold write",
             slave_writes_and_echoes_threshold);
    test_run("Modbus standard exceptions",
             slave_returns_standard_exceptions);
    test_run("Modbus silent frame rejection",
             slave_silently_rejects_bad_frames);
    test_run("Modbus malformed and capacity",
             slave_rejects_extra_data_and_small_response_buffer);
}

#include "modbus_slave.h"

#include <stdbool.h>
#include <stddef.h>

#include "modbus_crc.h"
#include "modbus_rtu_rx.h"
#include "register_map.h"

#define MODBUS_FUNCTION_READ_HOLDING 0x03u
#define MODBUS_FUNCTION_WRITE_SINGLE 0x06u
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION 0x01u
#define MODBUS_EXCEPTION_ILLEGAL_ADDRESS 0x02u
#define MODBUS_EXCEPTION_ILLEGAL_VALUE 0x03u
#define MODBUS_REQUEST_LENGTH 8u
#define MODBUS_EXCEPTION_LENGTH 5u

static ModbusProcessOutcome outcome(ModbusProcessResult result,
                                    size_t response_length)
{
    ModbusProcessOutcome value;
    value.result = result;
    value.response_length = response_length;
    return value;
}

static uint16_t read_u16_be(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8u) | data[1]);
}

static void append_crc(uint8_t *frame, size_t length_without_crc)
{
    const uint16_t crc = modbus_crc16(frame, length_without_crc);
    frame[length_without_crc] = (uint8_t)(crc & 0xFFu);
    frame[length_without_crc + 1u] = (uint8_t)(crc >> 8u);
}

static ModbusProcessOutcome make_exception(uint8_t function,
                                           uint8_t exception_code,
                                           uint8_t *response,
                                           size_t response_capacity)
{
    if (response == NULL || response_capacity < MODBUS_EXCEPTION_LENGTH) {
        return outcome(MODBUS_PROCESS_NO_RESPONSE, 0u);
    }

    response[0] = MODBUS_SLAVE_ID;
    response[1] = (uint8_t)(function | 0x80u);
    response[2] = exception_code;
    append_crc(response, 3u);
    return outcome(MODBUS_PROCESS_RESPONSE_READY,
                   MODBUS_EXCEPTION_LENGTH);
}

static ModbusProcessOutcome process_read(DeviceModel *model,
                                         const uint8_t *request,
                                         uint8_t *response,
                                         size_t response_capacity)
{
    uint16_t values[MODBUS_READ_MAX_QUANTITY];
    const uint16_t start_address = read_u16_be(&request[2]);
    const uint16_t quantity = read_u16_be(&request[4]);
    const uint32_t end_address = (uint32_t)start_address + quantity;
    size_t response_length;
    uint16_t index;

    if (quantity == 0u || quantity > MODBUS_READ_MAX_QUANTITY) {
        return make_exception(request[1],
                              MODBUS_EXCEPTION_ILLEGAL_VALUE,
                              response,
                              response_capacity);
    }
    if (end_address > 0x10000u ||
        !register_map_read_holding(model,
                                   start_address,
                                   quantity,
                                   values)) {
        return make_exception(request[1],
                              MODBUS_EXCEPTION_ILLEGAL_ADDRESS,
                              response,
                              response_capacity);
    }

    response_length = 5u + (size_t)quantity * 2u;
    if (response == NULL || response_capacity < response_length) {
        return outcome(MODBUS_PROCESS_NO_RESPONSE, 0u);
    }
    response[0] = MODBUS_SLAVE_ID;
    response[1] = MODBUS_FUNCTION_READ_HOLDING;
    response[2] = (uint8_t)(quantity * 2u);
    for (index = 0u; index < quantity; ++index) {
        response[3u + (size_t)index * 2u] =
            (uint8_t)(values[index] >> 8u);
        response[4u + (size_t)index * 2u] =
            (uint8_t)(values[index] & 0xFFu);
    }
    append_crc(response, response_length - 2u);
    return outcome(MODBUS_PROCESS_RESPONSE_READY, response_length);
}

static ModbusProcessOutcome process_write(DeviceModel *model,
                                          const uint8_t *request,
                                          uint8_t *response,
                                          size_t response_capacity)
{
    const uint16_t address = read_u16_be(&request[2]);
    const uint16_t raw_value = read_u16_be(&request[4]);
    const RegisterWriteResult result =
        register_map_write_single(model, address, raw_value);
    size_t index;

    if (result == REGISTER_WRITE_ILLEGAL_ADDRESS) {
        return make_exception(request[1],
                              MODBUS_EXCEPTION_ILLEGAL_ADDRESS,
                              response,
                              response_capacity);
    }
    if (result == REGISTER_WRITE_ILLEGAL_VALUE) {
        return make_exception(request[1],
                              MODBUS_EXCEPTION_ILLEGAL_VALUE,
                              response,
                              response_capacity);
    }
    if (response == NULL || response_capacity < MODBUS_REQUEST_LENGTH) {
        return outcome(MODBUS_PROCESS_NO_RESPONSE, 0u);
    }
    for (index = 0u; index < 6u; ++index) {
        response[index] = request[index];
    }
    append_crc(response, 6u);
    return outcome(MODBUS_PROCESS_RESPONSE_READY, MODBUS_REQUEST_LENGTH);
}

ModbusProcessOutcome modbus_slave_process_adu(
    DeviceModel *model,
    const uint8_t *request,
    size_t request_length,
    uint8_t *response,
    size_t response_capacity)
{
    uint8_t function;

    if (model == NULL || request == NULL || request_length == 0u) {
        return outcome(MODBUS_PROCESS_NO_RESPONSE, 0u);
    }
    function = request_length >= 2u ? request[1] : 0u;
    if (request[0] != MODBUS_SLAVE_ID) {
        return outcome(MODBUS_PROCESS_NO_RESPONSE, 0u);
    }
    if (request_length < 4u || request_length > MODBUS_RTU_MAX_ADU) {
        return outcome(MODBUS_PROCESS_COMMUNICATION_ERROR, 0u);
    }
    if (!modbus_crc16_valid(request, request_length)) {
        return outcome(MODBUS_PROCESS_COMMUNICATION_ERROR, 0u);
    }
    if (function != MODBUS_FUNCTION_READ_HOLDING &&
        function != MODBUS_FUNCTION_WRITE_SINGLE) {
        return make_exception(function,
                              MODBUS_EXCEPTION_ILLEGAL_FUNCTION,
                              response,
                              response_capacity);
    }
    if (request_length < MODBUS_REQUEST_LENGTH) {
        return outcome(MODBUS_PROCESS_COMMUNICATION_ERROR, 0u);
    }
    if (request_length > MODBUS_REQUEST_LENGTH) {
        return make_exception(function,
                              MODBUS_EXCEPTION_ILLEGAL_VALUE,
                              response,
                              response_capacity);
    }

    return function == MODBUS_FUNCTION_READ_HOLDING
        ? process_read(model, request, response, response_capacity)
        : process_write(model, request, response, response_capacity);
}

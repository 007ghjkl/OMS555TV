#include "modbus_rtu_rx.h"

#include <stddef.h>

void modbus_rtu_rx_init(ModbusRtuReceiver *receiver)
{
    if (receiver == NULL) {
        return;
    }
    receiver->length = 0u;
    receiver->last_byte_ms = 0u;
    receiver->receiving = false;
    receiver->overflow = false;
}

void modbus_rtu_rx_discard(ModbusRtuReceiver *receiver)
{
    modbus_rtu_rx_init(receiver);
}

void modbus_rtu_rx_push_byte(ModbusRtuReceiver *receiver,
                             uint8_t byte,
                             uint32_t now_ms)
{
    if (receiver == NULL) {
        return;
    }

    if (receiver->length < MODBUS_RTU_MAX_ADU) {
        receiver->bytes[receiver->length] = byte;
        ++receiver->length;
    } else {
        receiver->overflow = true;
    }
    receiver->last_byte_ms = now_ms;
    receiver->receiving = true;
}

ModbusRxResult modbus_rtu_rx_poll(ModbusRtuReceiver *receiver,
                                  uint32_t now_ms,
                                  uint8_t *frame,
                                  size_t capacity,
                                  size_t *frame_length)
{
    size_t index;
    size_t length;

    if (receiver == NULL || frame == NULL || frame_length == NULL) {
        return MODBUS_RX_NONE;
    }
    *frame_length = 0u;
    if (!receiver->receiving ||
        (uint32_t)(now_ms - receiver->last_byte_ms) <
            MODBUS_RTU_FRAME_GAP_MS) {
        return MODBUS_RX_NONE;
    }

    length = receiver->length;
    if (receiver->overflow || capacity < length) {
        modbus_rtu_rx_init(receiver);
        return MODBUS_RX_OVERFLOW;
    }

    for (index = 0u; index < length; ++index) {
        frame[index] = receiver->bytes[index];
    }
    *frame_length = length;
    modbus_rtu_rx_init(receiver);
    return MODBUS_RX_FRAME_READY;
}

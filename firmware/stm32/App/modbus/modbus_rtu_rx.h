#ifndef OMS555TV_MODBUS_RTU_RX_H
#define OMS555TV_MODBUS_RTU_RX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODBUS_RTU_MAX_ADU 256u
#define MODBUS_RTU_FRAME_GAP_MS 3u

typedef enum {
    MODBUS_RX_NONE = 0,
    MODBUS_RX_FRAME_READY,
    MODBUS_RX_OVERFLOW
} ModbusRxResult;

typedef struct {
    volatile uint8_t bytes[MODBUS_RTU_MAX_ADU];
    volatile uint16_t length;
    volatile uint32_t last_byte_ms;
    volatile bool receiving;
    volatile bool overflow;
} ModbusRtuReceiver;

void modbus_rtu_rx_init(ModbusRtuReceiver *receiver);
void modbus_rtu_rx_discard(ModbusRtuReceiver *receiver);
void modbus_rtu_rx_push_byte(ModbusRtuReceiver *receiver,
                             uint8_t byte,
                             uint32_t now_ms);
ModbusRxResult modbus_rtu_rx_poll(ModbusRtuReceiver *receiver,
                                  uint32_t now_ms,
                                  uint8_t *frame,
                                  size_t capacity,
                                  size_t *frame_length);

#ifdef __cplusplus
}
#endif

#endif

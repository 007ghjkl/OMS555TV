#include "test_framework.h"

#include <limits.h>
#include <string.h>

#include "modbus_crc.h"
#include "modbus_rtu_rx.h"

static void modbus_crc_matches_standard_vector(void)
{
    static const uint8_t request[] = {0x01u, 0x03u, 0x00u,
                                      0x00u, 0x00u, 0x0Au};
    uint8_t frame[8];

    TEST_ASSERT_EQ(0xFFFF, modbus_crc16(NULL, 0u));
    TEST_ASSERT_EQ(0xCDC5, modbus_crc16(request, sizeof(request)));
    memcpy(frame, request, sizeof(request));
    frame[6] = 0xC5u;
    frame[7] = 0xCDu;
    TEST_ASSERT_TRUE(modbus_crc16_valid(frame, sizeof(frame)));
    frame[4] ^= 0x01u;
    TEST_ASSERT_FALSE(modbus_crc16_valid(frame, sizeof(frame)));
}

static void receiver_waits_for_silence_across_segments(void)
{
    ModbusRtuReceiver receiver;
    uint8_t frame[MODBUS_RTU_MAX_ADU];
    size_t length = 99u;

    modbus_rtu_rx_init(&receiver);
    modbus_rtu_rx_push_byte(&receiver, 0x01u, 10u);
    modbus_rtu_rx_push_byte(&receiver, 0x03u, 10u);
    TEST_ASSERT_EQ(MODBUS_RX_NONE,
                   modbus_rtu_rx_poll(&receiver,
                                      11u,
                                      frame,
                                      sizeof(frame),
                                      &length));
    modbus_rtu_rx_push_byte(&receiver, 0x00u, 11u);
    TEST_ASSERT_EQ(MODBUS_RX_NONE,
                   modbus_rtu_rx_poll(&receiver,
                                      12u,
                                      frame,
                                      sizeof(frame),
                                      &length));
    TEST_ASSERT_EQ(MODBUS_RX_NONE,
                   modbus_rtu_rx_poll(&receiver,
                                      13u,
                                      frame,
                                      sizeof(frame),
                                      &length));
    TEST_ASSERT_EQ(MODBUS_RX_FRAME_READY,
                   modbus_rtu_rx_poll(&receiver,
                                      14u,
                                      frame,
                                      sizeof(frame),
                                      &length));
    TEST_ASSERT_EQ(3, length);
    TEST_ASSERT_EQ(0x01, frame[0]);
    TEST_ASSERT_EQ(0x03, frame[1]);
    TEST_ASSERT_EQ(0x00, frame[2]);
}

static void receiver_handles_boundary_overflow_and_recovery(void)
{
    ModbusRtuReceiver receiver;
    uint8_t frame[MODBUS_RTU_MAX_ADU];
    size_t length = 0u;
    unsigned int index;

    modbus_rtu_rx_init(&receiver);
    for (index = 0u; index < MODBUS_RTU_MAX_ADU; ++index) {
        modbus_rtu_rx_push_byte(&receiver, (uint8_t)index, 20u);
    }
    TEST_ASSERT_EQ(MODBUS_RX_FRAME_READY,
                   modbus_rtu_rx_poll(&receiver,
                                      23u,
                                      frame,
                                      sizeof(frame),
                                      &length));
    TEST_ASSERT_EQ(MODBUS_RTU_MAX_ADU, length);

    for (index = 0u; index <= MODBUS_RTU_MAX_ADU; ++index) {
        modbus_rtu_rx_push_byte(&receiver, (uint8_t)index, 30u);
    }
    TEST_ASSERT_EQ(MODBUS_RX_OVERFLOW,
                   modbus_rtu_rx_poll(&receiver,
                                      33u,
                                      frame,
                                      sizeof(frame),
                                      &length));
    modbus_rtu_rx_push_byte(&receiver, 0x55u, 40u);
    TEST_ASSERT_EQ(MODBUS_RX_FRAME_READY,
                   modbus_rtu_rx_poll(&receiver,
                                      43u,
                                      frame,
                                      sizeof(frame),
                                      &length));
    TEST_ASSERT_EQ(1, length);
    TEST_ASSERT_EQ(0x55, frame[0]);
}

static void receiver_handles_tick_wrap_and_separate_frames(void)
{
    ModbusRtuReceiver receiver;
    uint8_t frame[8];
    size_t length = 0u;

    modbus_rtu_rx_init(&receiver);
    modbus_rtu_rx_push_byte(&receiver, 0x11u, UINT32_MAX);
    TEST_ASSERT_EQ(MODBUS_RX_FRAME_READY,
                   modbus_rtu_rx_poll(&receiver,
                                      2u,
                                      frame,
                                      sizeof(frame),
                                      &length));
    TEST_ASSERT_EQ(0x11, frame[0]);

    modbus_rtu_rx_push_byte(&receiver, 0x22u, 10u);
    TEST_ASSERT_EQ(MODBUS_RX_FRAME_READY,
                   modbus_rtu_rx_poll(&receiver,
                                      13u,
                                      frame,
                                      sizeof(frame),
                                      &length));
    TEST_ASSERT_EQ(0x22, frame[0]);
    TEST_ASSERT_EQ(MODBUS_RX_NONE,
                   modbus_rtu_rx_poll(&receiver,
                                      20u,
                                      frame,
                                      sizeof(frame),
                                      &length));
}

void register_modbus_crc_rx_tests(void)
{
    test_run("Modbus CRC16 vector", modbus_crc_matches_standard_vector);
    test_run("RTU receiver segmented silence",
             receiver_waits_for_silence_across_segments);
    test_run("RTU receiver overflow recovery",
             receiver_handles_boundary_overflow_and_recovery);
    test_run("RTU receiver wrap and frames",
             receiver_handles_tick_wrap_and_separate_frames);
}

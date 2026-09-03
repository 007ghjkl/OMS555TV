#include "test_framework.h"

#include "dhtc12_codec.h"

static void crc_matches_normative_vectors(void)
{
    static const uint8_t zero[] = {0x00u, 0x00u};
    static const uint8_t ones[] = {0xFFu, 0xFFu};
    static const uint8_t sample[] = {0x12u, 0x34u};
    static const uint8_t room[] = {0xF1u, 0x00u};

    TEST_ASSERT_EQ(0x81, dhtc12_crc8(zero, sizeof(zero)));
    TEST_ASSERT_EQ(0xAC, dhtc12_crc8(ones, sizeof(ones)));
    TEST_ASSERT_EQ(0x37, dhtc12_crc8(sample, sizeof(sample)));
    TEST_ASSERT_EQ(0x6D, dhtc12_crc8(room, sizeof(room)));
}

static void valid_frame_is_decoded(void)
{
    static const uint8_t frame[DHTC12_FRAME_SIZE] = {
        0xF1u, 0x00u, 0x6Du, 0x12u, 0x34u, 0x37u
    };
    Dhtc12Frame decoded;

    TEST_ASSERT_EQ(ACQ_ERROR_NONE, dhtc12_decode_frame(frame, &decoded));
    TEST_ASSERT_EQ(0xF100, decoded.temperature_raw);
    TEST_ASSERT_EQ(0x1234, decoded.humidity_raw);
    TEST_ASSERT_TRUE(decoded.temperature_crc_valid);
    TEST_ASSERT_TRUE(decoded.humidity_crc_valid);
    TEST_ASSERT_EQ(250, decoded.temperature_deci_c);
}

static void each_crc_is_checked_independently(void)
{
    uint8_t frame[DHTC12_FRAME_SIZE] = {
        0xF1u, 0x00u, 0x6Du, 0x12u, 0x34u, 0x37u
    };
    Dhtc12Frame decoded;

    frame[2] ^= 0x01u;
    TEST_ASSERT_EQ(ACQ_ERROR_TEMP_CRC,
                   dhtc12_decode_frame(frame, &decoded));
    frame[2] ^= 0x01u;
    frame[5] ^= 0x01u;
    TEST_ASSERT_EQ(ACQ_ERROR_HUMIDITY_CRC,
                   dhtc12_decode_frame(frame, &decoded));
}

static void out_of_range_temperature_is_rejected(void)
{
    uint8_t frame[DHTC12_FRAME_SIZE] = {
        0x7Fu, 0xFFu, 0x00u, 0x12u, 0x34u, 0x37u
    };
    Dhtc12Frame decoded;

    frame[2] = dhtc12_crc8(frame, 2u);
    TEST_ASSERT_EQ(ACQ_ERROR_TEMP_OUT_OF_RANGE,
                   dhtc12_decode_frame(frame, &decoded));
}

void register_codec_tests(void)
{
    test_run("DHTC12 CRC vectors", crc_matches_normative_vectors);
    test_run("DHTC12 valid frame", valid_frame_is_decoded);
    test_run("DHTC12 independent CRC fields", each_crc_is_checked_independently);
    test_run("DHTC12 temperature range", out_of_range_temperature_is_rejected);
}

#include <zephyr/ztest.h>

#include <protocols/common/crc16_modbus.h>

ZTEST(crc16_modbus, test_known_vector)
{
	static const uint8_t sample[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A };

	zassert_equal(crc16_modbus_compute(sample, sizeof(sample)), 0xCDC5U, NULL);
}

ZTEST_SUITE(crc16_modbus, NULL, NULL, NULL, NULL, NULL);

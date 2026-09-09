#include <assert.h>
#include <string.h>
#include <stdio.h>

#include <protocols/common/crc16_modbus.h>

static void crc16_modbus_test_known_vector(void)
{
	static const uint8_t sample[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A };

	assert((crc16_modbus_compute(sample, sizeof(sample))) == (0xCDC5U));
}



int main(void)
{
    crc16_modbus_test_known_vector();
    puts("test_crc16_modbus: PASS");
    return 0;
}

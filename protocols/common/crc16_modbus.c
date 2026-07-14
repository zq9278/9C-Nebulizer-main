#include "crc16_modbus.h"

uint16_t crc16_modbus_compute(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xFFFFU;

	for (size_t i = 0; i < len; ++i) {
		crc ^= data[i];
		for (uint8_t bit = 0; bit < 8U; ++bit) {
			if ((crc & 0x0001U) != 0U) {
				crc = (crc >> 1) ^ 0xA001U;
			} else {
				crc >>= 1;
			}
		}
	}

	return crc;
}

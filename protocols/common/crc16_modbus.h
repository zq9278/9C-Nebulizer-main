#ifndef PROTOCOLS_COMMON_CRC16_MODBUS_H_
#define PROTOCOLS_COMMON_CRC16_MODBUS_H_

#include <stddef.h>
#include <stdint.h>

uint16_t crc16_modbus_compute(const uint8_t *data, size_t len);

#endif /* PROTOCOLS_COMMON_CRC16_MODBUS_H_ */

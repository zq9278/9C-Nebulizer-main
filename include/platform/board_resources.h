#ifndef PLATFORM_BOARD_RESOURCES_H_
#define PLATFORM_BOARD_RESOURCES_H_

#include <platform/hardware.h>
#include <platform/board_ids.h>

struct board_triac_resource {
	struct board_gpio enable_gpio;
	struct board_gpio zcd_gpio;
	const struct board_device *counter_dev;
	uint32_t default_delay_us;
	uint32_t pulse_width_us;
};

struct board_resources {
	const struct board_device *host_uart;
	const struct board_device *debug_uart;
	const struct board_device *mist_uart;
	const struct board_device *adc_dev;
	const struct board_device *eeprom_i2c;
	struct board_pwm fan_pwm;
	struct board_gpio fan_sense;
	struct board_gpio liquid_level;
	struct board_gpio hall1;
	struct board_gpio hall2;
	struct board_gpio gx1832;
	struct board_gpio ee_wp;
	struct board_gpio otp_reset;
	struct board_triac_resource triac;
	struct board_adc ntc[BOARD_NTC_COUNT];
};

#endif /* PLATFORM_BOARD_RESOURCES_H_ */

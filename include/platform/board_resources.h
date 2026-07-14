#ifndef PLATFORM_BOARD_RESOURCES_H_
#define PLATFORM_BOARD_RESOURCES_H_

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>

enum board_ntc_id {
	BOARD_NTC_OUTLET1 = 0,
	BOARD_NTC_KETTLE,
	BOARD_NTC_OUTLET2,
	BOARD_NTC_SPARE,
	BOARD_NTC_COUNT,
};

struct board_triac_resource {
	struct gpio_dt_spec enable_gpio;
	struct gpio_dt_spec zcd_gpio;
	const struct device *counter_dev;
	uint32_t default_delay_us;
	uint32_t pulse_width_us;
};

struct board_resources {
	const struct device *host_uart;
	const struct device *debug_uart;
	const struct device *mist_uart;
	const struct device *adc_dev;
	const struct device *eeprom_i2c;
	struct pwm_dt_spec fan_pwm;
	struct gpio_dt_spec fan_sense;
	struct gpio_dt_spec liquid_level;
	struct gpio_dt_spec hall1;
	struct gpio_dt_spec hall2;
	struct gpio_dt_spec gx1832;
	struct gpio_dt_spec ee_wp;
	struct gpio_dt_spec otp_reset;
	struct board_triac_resource triac;
	struct adc_dt_spec ntc[BOARD_NTC_COUNT];
};

#endif /* PLATFORM_BOARD_RESOURCES_H_ */

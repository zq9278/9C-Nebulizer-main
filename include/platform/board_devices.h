#ifndef PLATFORM_BOARD_DEVICES_H_
#define PLATFORM_BOARD_DEVICES_H_

#include <stdbool.h>

#include <platform/board_resources.h>

int board_devices_init(void);
const struct board_resources *board_get_resources(void);
const struct device *board_get_host_uart(void);
const struct device *board_get_mist_uart(void);
const struct device *board_get_debug_uart(void);
const struct gpio_dt_spec *board_get_triac_en(void);
const struct gpio_dt_spec *board_get_zcd_in(void);
const struct gpio_dt_spec *board_get_otp_reset(void);
const struct pwm_dt_spec *board_get_fan1_pwm(void);
const struct adc_dt_spec *board_get_ntc(enum board_ntc_id id);
bool board_ntc_is_enabled(enum board_ntc_id id);

#endif /* PLATFORM_BOARD_DEVICES_H_ */

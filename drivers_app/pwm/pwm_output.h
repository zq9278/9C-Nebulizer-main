#ifndef DRIVERS_APP_PWM_PWM_OUTPUT_H_
#define DRIVERS_APP_PWM_PWM_OUTPUT_H_

#include <stdint.h>

#include <zephyr/drivers/pwm.h>

int pwm_output_init(const struct pwm_dt_spec *spec);
int pwm_output_set_percent(const struct pwm_dt_spec *spec, uint8_t percent);
int pwm_output_stop(const struct pwm_dt_spec *spec);

#endif /* DRIVERS_APP_PWM_PWM_OUTPUT_H_ */

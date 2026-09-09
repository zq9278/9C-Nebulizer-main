#ifndef DRIVERS_APP_PWM_PWM_OUTPUT_H_
#define DRIVERS_APP_PWM_PWM_OUTPUT_H_

#include <stdint.h>

#include <platform/hardware.h>

int pwm_output_init(const struct board_pwm *spec);
int pwm_output_set_percent(const struct board_pwm *spec, uint8_t percent);
int pwm_output_stop(const struct board_pwm *spec);

#endif /* DRIVERS_APP_PWM_PWM_OUTPUT_H_ */

#include "pwm_output.h"

#include <errno.h>
#include <nebulizer/app_config.h>

int pwm_output_init(const struct board_pwm *spec)
{
	if ((spec == NULL) || !board_device_ready(spec->dev)) {
		return -ENODEV;
	}

	return board_pwm_set(spec, spec->period, APP_FAN_PWM_INVERTED ? spec->period : 0U);
}

int pwm_output_set_percent(const struct board_pwm *spec, uint8_t percent)
{
	uint32_t pulse;

	if ((spec == NULL) || (percent > 100U)) {
		return -EINVAL;
	}

	pulse = ((uint64_t)spec->period * percent) / 100U;
	return board_pwm_set(spec, spec->period, pulse);
}

int pwm_output_stop(const struct board_pwm *spec)
{
	if (spec == NULL) {
		return -EINVAL;
	}

	return board_pwm_set(spec, spec->period, APP_FAN_PWM_INVERTED ? spec->period : 0U);
}

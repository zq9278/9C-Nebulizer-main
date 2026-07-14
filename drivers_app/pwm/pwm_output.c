#include "pwm_output.h"

#include <errno.h>

int pwm_output_init(const struct pwm_dt_spec *spec)
{
	if ((spec == NULL) || !device_is_ready(spec->dev)) {
		return -ENODEV;
	}

	return pwm_set_dt(spec, spec->period, 0U);
}

int pwm_output_set_percent(const struct pwm_dt_spec *spec, uint8_t percent)
{
	uint32_t pulse;

	if ((spec == NULL) || (percent > 100U)) {
		return -EINVAL;
	}

	pulse = ((uint64_t)spec->period * percent) / 100U;
	return pwm_set_dt(spec, spec->period, pulse);
}

int pwm_output_stop(const struct pwm_dt_spec *spec)
{
	if (spec == NULL) {
		return -EINVAL;
	}

	return pwm_set_dt(spec, spec->period, 0U);
}

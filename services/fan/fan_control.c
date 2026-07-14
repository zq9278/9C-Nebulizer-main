#include "fan_control.h"

#include <errno.h>

#include <nebulizer/app_config.h>
#include <drivers_app/pwm/pwm_output.h>
#include <platform/board_devices.h>

static const struct pwm_dt_spec *fan_pwm;

static uint8_t fan_apply_hw_polarity(uint8_t percent)
{
	if (IS_ENABLED(CONFIG_NEBULIZER_FAN_PWM_INVERTED)) {
		return 100U - percent;
	}

	return percent;
}

int fan_control_init(void)
{
	fan_pwm = board_get_fan1_pwm();
	if (pwm_output_init(fan_pwm) != 0) {
		return -ENODEV;
	}

	return fan_control_stop();
}

static uint8_t fan_percent_for_level(air_level_t level)
{
	switch (level) {
	case AIR_LEVEL_LOW:
		return FAN_LEVEL_LOW_PERCENT;
	case AIR_LEVEL_MID:
		return FAN_LEVEL_MID_PERCENT;
	case AIR_LEVEL_HIGH:
		return FAN_LEVEL_HIGH_PERCENT;
	case AIR_LEVEL_OFF:
	default:
		return 0U;
	}
}

int fan_control_set_level(air_level_t level)
{
	uint8_t percent = fan_percent_for_level(level);

	return pwm_output_set_percent(fan_pwm, fan_apply_hw_polarity(percent));
}

int fan_control_stop(void)
{
	return pwm_output_set_percent(fan_pwm, fan_apply_hw_polarity(0U));
}

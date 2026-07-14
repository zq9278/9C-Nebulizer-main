#include "input_gpio.h"

#include <errno.h>

int input_gpio_init(const struct gpio_dt_spec *spec, gpio_flags_t extra_flags)
{
	if ((spec == NULL) || !gpio_is_ready_dt(spec)) {
		return -ENODEV;
	}

	return gpio_pin_configure_dt(spec, GPIO_INPUT | extra_flags);
}

int input_gpio_read(const struct gpio_dt_spec *spec, bool *active)
{
	int ret;

	if ((spec == NULL) || (active == NULL)) {
		return -EINVAL;
	}

	ret = gpio_pin_get_dt(spec);
	if (ret < 0) {
		return ret;
	}

	*active = (ret != 0);
	return 0;
}

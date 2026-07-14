#include "output_gpio.h"

#include <errno.h>

int output_gpio_init(const struct gpio_dt_spec *spec, bool active)
{
	if ((spec == NULL) || !gpio_is_ready_dt(spec)) {
		return -ENODEV;
	}

	return gpio_pin_configure_dt(spec,
				     GPIO_OUTPUT | (active ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE));
}

int output_gpio_set(const struct gpio_dt_spec *spec, bool active)
{
	if ((spec == NULL) || !gpio_is_ready_dt(spec)) {
		return -ENODEV;
	}

	return gpio_pin_set_dt(spec, active ? 1 : 0);
}

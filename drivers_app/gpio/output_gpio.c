#include "output_gpio.h"

#include <errno.h>

int output_gpio_init(const struct board_gpio *spec, bool active)
{
	if ((spec == NULL) || !board_gpio_ready(spec)) {
		return -ENODEV;
	}

	return board_gpio_configure(spec,
				     GPIO_OUTPUT | (active ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE));
}

int output_gpio_set(const struct board_gpio *spec, bool active)
{
	if ((spec == NULL) || !board_gpio_ready(spec)) {
		return -ENODEV;
	}

	return board_gpio_set(spec, active ? 1 : 0);
}

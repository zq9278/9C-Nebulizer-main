#include "input_gpio.h"

#include <errno.h>

int input_gpio_init(const struct board_gpio *spec, gpio_flags_t extra_flags)
{
	if ((spec == NULL) || !board_gpio_ready(spec)) {
		return -ENODEV;
	}

	return board_gpio_configure(spec, GPIO_INPUT | extra_flags);
}

int input_gpio_read(const struct board_gpio *spec, bool *active)
{
	int ret;

	if ((spec == NULL) || (active == NULL)) {
		return -EINVAL;
	}

	ret = board_gpio_get(spec);
	if (ret < 0) {
		return ret;
	}

	*active = (ret != 0);
	return 0;
}

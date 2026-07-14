#ifndef DRIVERS_APP_GPIO_INPUT_GPIO_H_
#define DRIVERS_APP_GPIO_INPUT_GPIO_H_

#include <stdbool.h>

#include <zephyr/drivers/gpio.h>

int input_gpio_init(const struct gpio_dt_spec *spec, gpio_flags_t extra_flags);
int input_gpio_read(const struct gpio_dt_spec *spec, bool *active);

#endif /* DRIVERS_APP_GPIO_INPUT_GPIO_H_ */

#ifndef DRIVERS_APP_GPIO_OUTPUT_GPIO_H_
#define DRIVERS_APP_GPIO_OUTPUT_GPIO_H_

#include <stdbool.h>

#include <zephyr/drivers/gpio.h>

int output_gpio_init(const struct gpio_dt_spec *spec, bool active);
int output_gpio_set(const struct gpio_dt_spec *spec, bool active);

#endif /* DRIVERS_APP_GPIO_OUTPUT_GPIO_H_ */

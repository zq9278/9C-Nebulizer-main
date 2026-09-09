#ifndef DRIVERS_APP_GPIO_OUTPUT_GPIO_H_
#define DRIVERS_APP_GPIO_OUTPUT_GPIO_H_

#include <stdbool.h>

#include <platform/hardware.h>

int output_gpio_init(const struct board_gpio *spec, bool active);
int output_gpio_set(const struct board_gpio *spec, bool active);

#endif /* DRIVERS_APP_GPIO_OUTPUT_GPIO_H_ */

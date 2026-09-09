#ifndef DRIVERS_APP_GPIO_INPUT_GPIO_H_
#define DRIVERS_APP_GPIO_INPUT_GPIO_H_

#include <stdbool.h>

#include <platform/hardware.h>

int input_gpio_init(const struct board_gpio *spec, gpio_flags_t extra_flags);
int input_gpio_read(const struct board_gpio *spec, bool *active);

#endif /* DRIVERS_APP_GPIO_INPUT_GPIO_H_ */

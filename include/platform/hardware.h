#ifndef PLATFORM_HARDWARE_H
#define PLATFORM_HARDWARE_H
#include <platform/util.h>
#include "stm32g0xx_hal.h"
struct board_device { void *instance; bool ready; };
typedef uint32_t gpio_flags_t;
/* Application GPIO flags are separate from STM32 HAL GPIO mode values. */
#define GPIO_INPUT BIT(0)
#define GPIO_OUTPUT BIT(1)
#define GPIO_OUTPUT_ACTIVE (GPIO_OUTPUT | BIT(2))
#define GPIO_OUTPUT_INACTIVE GPIO_OUTPUT
#define GPIO_ACTIVE_LOW BIT(3)
#define GPIO_PULL_UP BIT(4)
#define GPIO_PULL_DOWN BIT(5)
#define GPIO_INT_EDGE_BOTH BIT(6)
struct board_gpio { const struct board_device *port; uint8_t pin; gpio_flags_t flags; };
struct board_pwm { const struct board_device *dev; uint32_t channel; uint32_t period; };
struct board_adc { const struct board_device *dev; uint32_t channel; uint8_t resolution; };
struct board_gpio_callback {
    void (*handler)(const struct board_device *, struct board_gpio_callback *, uint32_t);
    uint32_t pins;
};
bool board_device_ready(const struct board_device *dev);
bool board_gpio_ready(const struct board_gpio *gpio);
int board_gpio_configure(const struct board_gpio *gpio, gpio_flags_t flags);
int board_gpio_get(const struct board_gpio *gpio);
int board_gpio_set(const struct board_gpio *gpio, int active);
void board_gpio_callback_init(struct board_gpio_callback *cb,
    void (*handler)(const struct board_device *, struct board_gpio_callback *, uint32_t), uint32_t pins);
int board_gpio_callback_add(const struct board_device *dev, struct board_gpio_callback *cb);
int board_gpio_callback_remove(const struct board_device *dev, struct board_gpio_callback *cb);
int board_gpio_interrupt_configure(const struct board_gpio *gpio, uint32_t flags);
int board_pwm_set(const struct board_pwm *pwm, uint32_t period_ns, uint32_t pulse_ns);
int board_adc_read(const struct board_adc *adc, uint16_t *value);
int board_i2c_write(const struct board_device *dev, const uint8_t *data, size_t len, uint16_t address);
int board_i2c_write_read(const struct board_device *dev, uint16_t address,
    const uint8_t *prefix, size_t prefix_len, uint8_t *data, size_t len);
int board_hardware_init(void);
uint32_t board_time_us(void);
void board_emergency_off(void);
extern UART_HandleTypeDef board_host_uart, board_debug_uart, board_mist_uart;
#endif

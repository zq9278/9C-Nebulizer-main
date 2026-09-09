#include "board_devices.h"
#include <errno.h>
extern ADC_HandleTypeDef board_adc;
extern I2C_HandleTypeDef board_eeprom_i2c;
extern TIM_HandleTypeDef board_fan_timer;
static const struct board_device gpioa = {GPIOA, true}, gpiob = {GPIOB, true};
static const struct board_device host = {&board_host_uart, true}, debug = {&board_debug_uart, true}, mist = {&board_mist_uart, true};
static const struct board_device adc = {&board_adc, true}, i2c = {&board_eeprom_i2c, true}, pwm = {&board_fan_timer, true};
static const struct board_device timer = {TIM15, true};
static const struct board_resources board_res = {
    .host_uart = &host, .debug_uart = &debug, .mist_uart = &mist,
    .adc_dev = &adc, .eeprom_i2c = &i2c,
    .fan_pwm = {&pwm, TIM_CHANNEL_1, 100000U},
    .fan_sense = {&gpioa, 4, GPIO_PULL_UP},
    .liquid_level = {&gpiob, 5, GPIO_ACTIVE_LOW | GPIO_PULL_UP},
    .hall1 = {&gpioa, 11, GPIO_ACTIVE_LOW | GPIO_PULL_UP},
    .hall2 = {&gpioa, 12, GPIO_ACTIVE_LOW | GPIO_PULL_UP},
    .gx1832 = {&gpioa, 10, GPIO_PULL_DOWN},
    .ee_wp = {&gpiob, 15, 0}, .otp_reset = {&gpioa, 7, 0},
    .triac = {{&gpiob, 1, 0}, {&gpiob, 2, 0}, &timer, 4000, 3000},
    .ntc = {{&adc, ADC_CHANNEL_15, 12}, {&adc, ADC_CHANNEL_11, 12},
            {&adc, ADC_CHANNEL_16, 12}, {&adc, ADC_CHANNEL_5, 12}},
};
int board_devices_init(void)
{
    return SystemCoreClock == 64000000U && board_host_uart.Instance == USART1 &&
        board_mist_uart.Instance == USART4 && board_adc.Instance == ADC1 ? 0 : -ENODEV;
}
const struct board_resources *board_get_resources(void)
{
	return &board_res;
}

/* 以下 getter 用于给各业务服务提供最小必要的板级句柄。 */
const struct board_device *board_get_host_uart(void)
{
	return board_res.host_uart;
}

const struct board_device *board_get_mist_uart(void)
{
	return board_res.mist_uart;
}

const struct board_device *board_get_debug_uart(void)
{
	return board_res.debug_uart;
}

const struct board_gpio *board_get_triac_en(void)
{
	return &board_res.triac.enable_gpio;
}

const struct board_gpio *board_get_zcd_in(void)
{
	return &board_res.triac.zcd_gpio;
}

const struct board_gpio *board_get_otp_reset(void)
{
	return &board_res.otp_reset;
}

const struct board_pwm *board_get_fan1_pwm(void)
{
	return &board_res.fan_pwm;
}

const struct board_adc *board_get_ntc(enum board_ntc_id id)
{
	if ((unsigned)id >= BOARD_NTC_COUNT) {
		return NULL;
	}

	return &board_res.ntc[id];
}

bool board_ntc_is_enabled(enum board_ntc_id id)
{
	if ((unsigned)id >= BOARD_NTC_COUNT) {
		return false;
	}

	return true;
}

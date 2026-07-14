#include "board_devices.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(board_devices, CONFIG_NEBULIZER_LOG_LEVEL);

/*
 * 这里把设备树里的 alias / node label 映射成应用可用的板级资源。
 *
 * 这样上层业务代码就不需要知道具体的端口名和引脚号，
 * 只需要通过 board_get_xxx() 拿资源。
 */
#define HOST_UART_NODE      DT_ALIAS(host_uart)
#define DEBUG_UART_NODE     DT_ALIAS(debug_uart)
#define MIST_UART_NODE      DT_ALIAS(mist_uart)
#define FAN_PWM_NODE        DT_ALIAS(fan1_pwm)
#define FAN_SENSE_NODE      DT_ALIAS(fan1_sense)
#define LIQUID_LEVEL_NODE   DT_ALIAS(liquid_level)
#define HALL1_NODE          DT_ALIAS(hall1)
#define HALL2_NODE          DT_ALIAS(hall2)
#define GX1832_NODE         DT_ALIAS(gx1832)
#define EE_WP_NODE          DT_ALIAS(ee_wp)
#define OTP_RESET_NODE      DT_ALIAS(otp_reset)
#define TRIAC_NODE          DT_NODELABEL(triac0)
#define NTC1_NODE           DT_ALIAS(ntc1)
#define NTC2_NODE           DT_ALIAS(ntc2)
#define NTC3_NODE           DT_ALIAS(ntc3)
#define NTC4_NODE           DT_ALIAS(ntc4)

static struct board_resources board_res = {
	.host_uart = DEVICE_DT_GET(HOST_UART_NODE),
	.debug_uart = DEVICE_DT_GET(DEBUG_UART_NODE),
	.mist_uart = DEVICE_DT_GET(MIST_UART_NODE),
	.adc_dev = DEVICE_DT_GET(DT_IO_CHANNELS_CTLR(NTC1_NODE)),
	.eeprom_i2c = DEVICE_DT_GET(DT_NODELABEL(i2c2)),
	.fan_pwm = PWM_DT_SPEC_GET(FAN_PWM_NODE),
	.fan_sense = GPIO_DT_SPEC_GET(FAN_SENSE_NODE, gpios),
	.liquid_level = GPIO_DT_SPEC_GET(LIQUID_LEVEL_NODE, gpios),
	.hall1 = GPIO_DT_SPEC_GET(HALL1_NODE, gpios),
	.hall2 = GPIO_DT_SPEC_GET(HALL2_NODE, gpios),
	.gx1832 = GPIO_DT_SPEC_GET(GX1832_NODE, gpios),
	.ee_wp = GPIO_DT_SPEC_GET(EE_WP_NODE, gpios),
	.otp_reset = GPIO_DT_SPEC_GET(OTP_RESET_NODE, gpios),
	.triac = {
		.enable_gpio = GPIO_DT_SPEC_GET(TRIAC_NODE, enable_gpios),
		.zcd_gpio = GPIO_DT_SPEC_GET(TRIAC_NODE, zcd_gpios),
		.counter_dev = DEVICE_DT_GET(DT_PHANDLE(TRIAC_NODE, counter)),
		.default_delay_us = DT_PROP(TRIAC_NODE, default_delay_us),
		.pulse_width_us = DT_PROP(TRIAC_NODE, pulse_width_us),
	},
	.ntc = {
		ADC_DT_SPEC_GET(NTC1_NODE),
		ADC_DT_SPEC_GET(NTC2_NODE),
		ADC_DT_SPEC_GET(NTC3_NODE),
		ADC_DT_SPEC_GET(NTC4_NODE),
	},
};

static const bool ntc_enabled[BOARD_NTC_COUNT] = {
	[BOARD_NTC_OUTLET1] = true,
	[BOARD_NTC_KETTLE] = true,
	[BOARD_NTC_OUTLET2] = true,
	[BOARD_NTC_SPARE] = false,
};

/* 公共 UART 就绪检查函数，避免三份重复代码。 */
static int board_check_uart(const struct device *dev, const char *name)
{
	if (!device_is_ready(dev)) {
		LOG_ERR("%s is not ready", name);
		return -ENODEV;
	}

	return 0;
}

/*
 * 初始化并校验所有关键板级资源。
 *
 * 这里不是在“配置硬件参数”，而是在确认：
 * - 对应的 Zephyr device 已被正确生成
 * - 运行期要用到的设备都处于 ready 状态
 *
 * 如果这里失败，应用会在启动阶段尽早退出。
 */
int board_devices_init(void)
{
	int ret;

	ret = board_check_uart(board_res.host_uart, "host_uart");
	if (ret != 0) {
		return ret;
	}

	ret = board_check_uart(board_res.debug_uart, "debug_uart");
	if (ret != 0) {
		return ret;
	}

	ret = board_check_uart(board_res.mist_uart, "mist_uart");
	if (ret != 0) {
		return ret;
	}

	if (!device_is_ready(board_res.adc_dev)) {
		LOG_ERR("adc device is not ready");
		return -ENODEV;
	}

	if (!device_is_ready(board_res.fan_pwm.dev)) {
		LOG_ERR("fan pwm is not ready");
		return -ENODEV;
	}

	if (!device_is_ready(board_res.triac.enable_gpio.port) ||
	    !device_is_ready(board_res.triac.zcd_gpio.port)) {
		LOG_ERR("triac gpio is not ready");
		return -ENODEV;
	}

	if (!device_is_ready(board_res.triac.counter_dev)) {
		LOG_ERR("triac counter is not ready");
		return -ENODEV;
	}

	if (!device_is_ready(board_res.liquid_level.port) ||
	    !device_is_ready(board_res.hall1.port) ||
	    !device_is_ready(board_res.hall2.port) ||
	    !device_is_ready(board_res.gx1832.port) ||
	    !device_is_ready(board_res.fan_sense.port) ||
	    !device_is_ready(board_res.ee_wp.port) ||
	    !device_is_ready(board_res.otp_reset.port)) {
		LOG_ERR("one or more gpio inputs are not ready");
		return -ENODEV;
	}

	return 0;
}

/* 返回整份资源表，适合需要一次性读取多个板级对象的场景。 */
const struct board_resources *board_get_resources(void)
{
	return &board_res;
}

/* 以下 getter 用于给各业务服务提供最小必要的板级句柄。 */
const struct device *board_get_host_uart(void)
{
	return board_res.host_uart;
}

const struct device *board_get_mist_uart(void)
{
	return board_res.mist_uart;
}

const struct device *board_get_debug_uart(void)
{
	return board_res.debug_uart;
}

const struct gpio_dt_spec *board_get_triac_en(void)
{
	return &board_res.triac.enable_gpio;
}

const struct gpio_dt_spec *board_get_zcd_in(void)
{
	return &board_res.triac.zcd_gpio;
}

const struct gpio_dt_spec *board_get_otp_reset(void)
{
	return &board_res.otp_reset;
}

const struct pwm_dt_spec *board_get_fan1_pwm(void)
{
	return &board_res.fan_pwm;
}

const struct adc_dt_spec *board_get_ntc(enum board_ntc_id id)
{
	if (id >= BOARD_NTC_COUNT) {
		return NULL;
	}

	return &board_res.ntc[id];
}

bool board_ntc_is_enabled(enum board_ntc_id id)
{
	if (id >= BOARD_NTC_COUNT) {
		return false;
	}

	return ntc_enabled[id];
}

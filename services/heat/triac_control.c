#include "triac_control.h"

#include <errno.h>

#include <nebulizer/app_config.h>
#include <platform/board_devices.h>
#include <platform/hardware.h>
#include <platform/runtime.h>
#include <platform/log.h>

static const struct board_resources *res;
static struct board_gpio_callback zcd_callback;


static volatile bool triac_enabled;
static volatile uint16_t triac_output_permille;
static uint32_t triac_window_accumulator;
static uint32_t last_zcd_cycle;

#define TRIAC_ZCD_MIN_INTERVAL_US 5000U

static int triac_control_set_input_supply(bool enabled)
{
	/*
	 * 原理图中 MOC3063 输入 LED 上端由 OTP1 -> Q6 -> Q5(AO3401) 提供电源。
	 * 如果只拉 TRIAC_EN，而不打开这一路高边供电，Q2 只能下拉一个悬空节点，
	 * MOC3063 没有 LED 电流，自然不会触发主可控硅。
	 */
	if (enabled) {
		return board_gpio_configure(&res->otp_reset, GPIO_OUTPUT_ACTIVE);
	}

	return board_gpio_configure(&res->otp_reset, GPIO_INPUT);
}

static int triac_control_set_gate(bool enabled)
{
	return board_gpio_set(&res->triac.enable_gpio, enabled ? 1 : 0);
}

static void triac_control_reset_window_locked(void)
{
	triac_window_accumulator = 0U;
}

static bool triac_control_next_half_cycle_on_locked(void)
{
	bool slot_on;

	if (triac_output_permille == 0U) {
		triac_control_reset_window_locked();
		return false;
	}

	if (triac_output_permille >= 1000U) {
		return true;
	}

	/*
	 * MOC3063 是过零触发型光耦，可控硅一旦触发会保持到下一个交流过零点。
	 * 因此这里不能再用自由运行的 PWM 定时器，而是每次 ZCD_OUT 过零事件到来时，
	 * 直接按 permille 累加决定“这个半周是否触发”。这样不会把 0..1000 permille
	 * 先压缩成 0..100 档，100 permille 以下也能以更低触发频率输出。
	 *
	 * 例：zcd/s 约 200 时，50 permille 约 10 个触发半周/秒；
	 * 10 permille 约 2 个触发半周/秒；1 permille 约 0.2 个触发半周/秒。
	 */
	triac_window_accumulator += triac_output_permille;
	slot_on = triac_window_accumulator >= 1000U;
	if (slot_on) {
		triac_window_accumulator -= 1000U;
	}

	return slot_on;
}


static void triac_control_zcd_handler(const struct board_device *port,
				      struct board_gpio_callback *cb,
				      uint32_t pins)
{
	uint32_t key;
	bool gate_on;
	uint32_t now_cycle;
	uint32_t elapsed_us;

	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	key = runtime_irq_save();

	now_cycle = board_time_us();
	if (last_zcd_cycle != 0U) {
		elapsed_us = (uint32_t)(now_cycle - last_zcd_cycle);
		if (elapsed_us < TRIAC_ZCD_MIN_INTERVAL_US) {
			runtime_irq_restore(key);
			return;
		}
	}
	last_zcd_cycle = now_cycle;

	if (!triac_enabled || (triac_output_permille == 0U)) {
		triac_control_reset_window_locked();
		gate_on = false;
	} else {
		gate_on = triac_control_next_half_cycle_on_locked();
	}
	runtime_irq_restore(key);

	if (gate_on) {
		/*
		 * MOC3063 自带过零触发功能。ZCD_OUT 是由光耦整形出的“过零窗口”，
		 * 不是一个精确的数学零点；如果只打一小段脉冲，可能刚好落在
		 * MOC3063 尚未进入可触发区或已经错过触发区的位置。
		 *
		 * 因此对被选中的半周，直接保持 TRIAC_EN 为高，直到下一个有效
		 * 半周边界重新决策。这样 MOC3063 在自己的过零触发窗口内一定
		 * 能看到输入 LED 电流，行为更接近普通过零 SSR 的输入控制。
		 */
		(void)triac_control_set_gate(true);
	} else {
		(void)triac_control_set_gate(false);
	}
}

int triac_control_init(void)
{
	int ret;

	res = board_get_resources();
	if (res == NULL) {
		return -ENODEV;
	}

	ret = board_gpio_configure(&res->triac.enable_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		return ret;
	}

	ret = triac_control_set_input_supply(false);
	if (ret != 0) {
		return ret;
	}

	ret = board_gpio_configure(&res->triac.zcd_gpio, GPIO_INPUT);
	if (ret != 0) {
		return ret;
	}

	triac_enabled = false;
	triac_output_permille = 0U;
	last_zcd_cycle = 0U;
	triac_control_reset_window_locked();

	board_gpio_callback_init(&zcd_callback, triac_control_zcd_handler,
			   BIT(res->triac.zcd_gpio.pin));
	ret = board_gpio_callback_add(res->triac.zcd_gpio.port, &zcd_callback);
	if (ret != 0) {
		return ret;
	}

	ret = board_gpio_interrupt_configure(&res->triac.zcd_gpio, GPIO_INT_EDGE_BOTH);
	if (ret != 0) {
		(void)board_gpio_callback_remove(res->triac.zcd_gpio.port, &zcd_callback);
		return ret;
	}

	return triac_control_set_gate(false);
}

int triac_control_set_output_permille(uint16_t output_permille)
{
	uint32_t key;

	if (output_permille > 1000U) {
		return -EINVAL;
	}

	key = runtime_irq_save();
	if (triac_output_permille != output_permille) {
		triac_output_permille = output_permille;
		last_zcd_cycle = 0U;
		triac_control_reset_window_locked();
	}
	runtime_irq_restore(key);

	if (output_permille == 0U) {
		(void)triac_control_set_gate(false);
	}

	return 0;
}

int triac_control_set_enabled(bool enabled)
{
	uint32_t key;
	bool was_enabled;
	int ret;

	if (enabled) {
		ret = triac_control_set_input_supply(true);
		if (ret != 0) {
			return ret;
		}
	}

	key = runtime_irq_save();
	was_enabled = triac_enabled;
	triac_enabled = enabled;
	if (!enabled) {
		triac_output_permille = 0U;
		last_zcd_cycle = 0U;
		triac_control_reset_window_locked();
	}
	runtime_irq_restore(key);

	if (enabled && !was_enabled) {
		LOG_INF("triac enabled output=%u permille", triac_output_permille);
	}

	if (!enabled) {
		(void)triac_control_set_gate(false);
		(void)triac_control_set_input_supply(false);
		LOG_INF("triac disabled");
	}

	return 0;
}

int triac_control_stop(void)
{
	uint32_t key;

	key = runtime_irq_save();
	triac_enabled = false;
	triac_output_permille = 0U;
	last_zcd_cycle = 0U;
	triac_control_reset_window_locked();
	runtime_irq_restore(key);
	(void)triac_control_set_gate(false);
	(void)triac_control_set_input_supply(false);
	return 0;
}

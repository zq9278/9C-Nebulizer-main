#include "triac_control.h"

#include <errno.h>

#include <nebulizer/app_config.h>
#include <platform/board_devices.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(triac_control, CONFIG_NEBULIZER_LOG_LEVEL);

static const struct board_resources *res;
static struct gpio_callback zcd_callback;
static struct k_timer gate_pulse_timer;
static struct k_spinlock triac_lock;
static volatile bool triac_enabled;
static volatile uint16_t triac_output_permille;
static uint32_t triac_window_accumulator;
static uint32_t last_zcd_cycle;

#define TRIAC_MOC3063_MIN_PULSE_US 3000U
#define TRIAC_ZCD_MIN_INTERVAL_US 5000U

static int triac_control_set_input_supply(bool enabled)
{
	/*
	 * 原理图中 MOC3063 输入 LED 上端由 OTP1 -> Q6 -> Q5(AO3401) 提供电源。
	 * 如果只拉 TRIAC_EN，而不打开这一路高边供电，Q2 只能下拉一个悬空节点，
	 * MOC3063 没有 LED 电流，自然不会触发主可控硅。
	 */
	if (enabled) {
		return gpio_pin_configure_dt(&res->otp_reset, GPIO_OUTPUT_ACTIVE);
	}

	return gpio_pin_configure_dt(&res->otp_reset, GPIO_INPUT);
}

static int triac_control_set_gate(bool enabled)
{
	return gpio_pin_set_dt(&res->triac.enable_gpio, enabled ? 1 : 0);
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

static void triac_control_gate_pulse_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	(void)triac_control_set_gate(false);
}

static void triac_control_zcd_handler(const struct device *port,
				      struct gpio_callback *cb,
				      gpio_port_pins_t pins)
{
	k_spinlock_key_t key;
	bool gate_on;
	uint32_t pulse_width_us;
	uint32_t now_cycle;
	uint32_t elapsed_us;

	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	key = k_spin_lock(&triac_lock);

	now_cycle = k_cycle_get_32();
	if (last_zcd_cycle != 0U) {
		elapsed_us = k_cyc_to_us_floor32(now_cycle - last_zcd_cycle);
		if (elapsed_us < TRIAC_ZCD_MIN_INTERVAL_US) {
			k_spin_unlock(&triac_lock, key);
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
	k_spin_unlock(&triac_lock, key);

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
		ARG_UNUSED(pulse_width_us);
		k_timer_stop(&gate_pulse_timer);
		(void)triac_control_set_gate(true);
	} else {
		k_timer_stop(&gate_pulse_timer);
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

	ret = gpio_pin_configure_dt(&res->triac.enable_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		return ret;
	}

	ret = triac_control_set_input_supply(false);
	if (ret != 0) {
		return ret;
	}

	ret = gpio_pin_configure_dt(&res->triac.zcd_gpio, GPIO_INPUT);
	if (ret != 0) {
		return ret;
	}

	triac_enabled = false;
	triac_output_permille = 0U;
	last_zcd_cycle = 0U;
	triac_control_reset_window_locked();

	k_timer_init(&gate_pulse_timer, triac_control_gate_pulse_timer_handler, NULL);

	gpio_init_callback(&zcd_callback, triac_control_zcd_handler,
			   BIT(res->triac.zcd_gpio.pin));
	ret = gpio_add_callback(res->triac.zcd_gpio.port, &zcd_callback);
	if (ret != 0) {
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&res->triac.zcd_gpio, GPIO_INT_EDGE_BOTH);
	if (ret != 0) {
		(void)gpio_remove_callback(res->triac.zcd_gpio.port, &zcd_callback);
		return ret;
	}

	return triac_control_set_gate(false);
}

int triac_control_set_output_permille(uint16_t output_permille)
{
	k_spinlock_key_t key;

	if (output_permille > 1000U) {
		return -EINVAL;
	}

	key = k_spin_lock(&triac_lock);
	if (triac_output_permille != output_permille) {
		triac_output_permille = output_permille;
		last_zcd_cycle = 0U;
		triac_control_reset_window_locked();
	}
	k_spin_unlock(&triac_lock, key);

	if (output_permille == 0U) {
		(void)triac_control_set_gate(false);
	}

	return 0;
}

int triac_control_set_enabled(bool enabled)
{
	k_spinlock_key_t key;
	bool was_enabled;
	int ret;

	if (enabled) {
		ret = triac_control_set_input_supply(true);
		if (ret != 0) {
			return ret;
		}
	}

	key = k_spin_lock(&triac_lock);
	was_enabled = triac_enabled;
	triac_enabled = enabled;
	if (!enabled) {
		triac_output_permille = 0U;
		last_zcd_cycle = 0U;
		triac_control_reset_window_locked();
	}
	k_spin_unlock(&triac_lock, key);

	if (enabled && !was_enabled) {
		LOG_INF("triac enabled output=%u permille", triac_output_permille);
	}

	if (!enabled) {
		k_timer_stop(&gate_pulse_timer);
		(void)triac_control_set_gate(false);
		(void)triac_control_set_input_supply(false);
		LOG_INF("triac disabled");
	}

	return 0;
}

int triac_control_stop(void)
{
	k_spinlock_key_t key;

	key = k_spin_lock(&triac_lock);
	triac_enabled = false;
	triac_output_permille = 0U;
	last_zcd_cycle = 0U;
	triac_control_reset_window_locked();
	k_spin_unlock(&triac_lock, key);

	k_timer_stop(&gate_pulse_timer);
	(void)triac_control_set_gate(false);
	(void)triac_control_set_input_supply(false);
	return 0;
}

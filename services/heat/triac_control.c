#include "triac_control.h"

#include <errno.h>

#include <nebulizer/app_config.h>
#include <platform/board_devices.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(triac_control, CONFIG_NEBULIZER_LOG_LEVEL);

static const struct board_resources *res;
static struct k_timer triac_slice_timer;
static struct k_spinlock triac_lock;
static volatile bool triac_enabled;
static volatile uint16_t triac_output_permille;
static volatile uint8_t triac_power_level;
static uint16_t triac_window_slot;
static uint16_t triac_window_on_count;
static uint16_t triac_window_accumulator;

static int triac_control_set_gate(bool enabled)
{
	return gpio_pin_set_dt(&res->triac.enable_gpio, enabled ? 1 : 0);
}

static uint8_t triac_control_permille_to_level(uint16_t output_permille)
{
	if (output_permille >= 1000U) {
		return 100U;
	}

	return (uint8_t)((output_permille + 5U) / 10U);
}

static void triac_control_reset_window_locked(void)
{
	triac_window_slot = 0U;
	triac_window_on_count = 0U;
	triac_window_accumulator = 0U;
}

static bool triac_control_next_slot_on_locked(void)
{
	bool slot_on;

	if (triac_power_level == 0U) {
		triac_control_reset_window_locked();
		return false;
	}

	if (triac_power_level >= 100U) {
		triac_window_slot++;
		if (triac_window_slot >= APP_TRIAC_POWER_WINDOW_SLICES) {
			triac_window_slot = 0U;
		}
		triac_window_on_count = APP_TRIAC_POWER_WINDOW_SLICES;
		return true;
	}

	triac_window_accumulator += triac_power_level;
	slot_on = triac_window_accumulator >= APP_TRIAC_POWER_WINDOW_SLICES;
	if (slot_on) {
		triac_window_accumulator -= APP_TRIAC_POWER_WINDOW_SLICES;
		triac_window_on_count++;
	}

	triac_window_slot++;
	if (triac_window_slot >= APP_TRIAC_POWER_WINDOW_SLICES) {
		triac_window_slot = 0U;
		triac_window_on_count = 0U;
	}

	return slot_on;
}

static void triac_control_slice_timer_handler(struct k_timer *timer)
{
	k_spinlock_key_t key;
	bool gate_on;

	ARG_UNUSED(timer);

	key = k_spin_lock(&triac_lock);
	if (!triac_enabled || (triac_power_level == 0U)) {
		triac_control_reset_window_locked();
		gate_on = false;
	} else {
		gate_on = triac_control_next_slot_on_locked();
	}
	k_spin_unlock(&triac_lock, key);

	(void)triac_control_set_gate(gate_on);
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

	triac_enabled = false;
	triac_output_permille = 0U;
	triac_power_level = 0U;
	triac_control_reset_window_locked();

	k_timer_init(&triac_slice_timer, triac_control_slice_timer_handler, NULL);
	k_timer_start(&triac_slice_timer,
		      K_MSEC(APP_TRIAC_POWER_SLICE_MS),
		      K_MSEC(APP_TRIAC_POWER_SLICE_MS));

	return triac_control_set_gate(false);
}

int triac_control_set_output_permille(uint16_t output_permille)
{
	k_spinlock_key_t key;

	if (output_permille > 1000U) {
		return -EINVAL;
	}

	key = k_spin_lock(&triac_lock);
	triac_output_permille = output_permille;
	triac_power_level = triac_control_permille_to_level(output_permille);
	triac_control_reset_window_locked();
	k_spin_unlock(&triac_lock, key);

	if (output_permille == 0U) {
		(void)triac_control_set_gate(false);
	}

	return 0;
}

int triac_control_set_enabled(bool enabled)
{
	k_spinlock_key_t key;

	key = k_spin_lock(&triac_lock);
	triac_enabled = enabled;
	if (!enabled) {
		triac_output_permille = 0U;
		triac_power_level = 0U;
		triac_control_reset_window_locked();
	}
	k_spin_unlock(&triac_lock, key);

	if (!enabled) {
		(void)triac_control_set_gate(false);
	}

	return 0;
}

int triac_control_stop(void)
{
	k_spinlock_key_t key;

	key = k_spin_lock(&triac_lock);
	triac_enabled = false;
	triac_output_permille = 0U;
	triac_power_level = 0U;
	triac_control_reset_window_locked();
	k_spin_unlock(&triac_lock, key);

	(void)triac_control_set_gate(false);
	return 0;
}

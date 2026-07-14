#include "triac_timer.h"

#include <errno.h>

#include <drivers_app/gpio/output_gpio.h>
#include <platform/board_devices.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(triac_timer, CONFIG_NEBULIZER_LOG_LEVEL);

enum {
	TRIAC_CH_FIRE = 0,
	TRIAC_CH_PULSE_END = 1,
};

static const struct board_resources *res;
static struct counter_alarm_cfg fire_alarm;
static struct counter_alarm_cfg pulse_end_alarm;
static volatile bool triac_enabled;
static volatile bool triac_busy;

static uint32_t triac_us_to_ticks(uint32_t usec)
{
	uint32_t freq = counter_get_frequency(res->triac.counter_dev);

	return (uint32_t)(((uint64_t)freq * usec) / USEC_PER_SEC);
}

static void triac_pulse_end_cb(const struct device *dev, uint8_t chan_id,
			       uint32_t ticks, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(chan_id);
	ARG_UNUSED(ticks);
	ARG_UNUSED(user_data);

	output_gpio_set(&res->triac.enable_gpio, false);
	triac_busy = false;
}

static void triac_fire_cb(const struct device *dev, uint8_t chan_id,
			  uint32_t ticks, void *user_data)
{
	int ret;

	ARG_UNUSED(chan_id);
	ARG_UNUSED(ticks);
	ARG_UNUSED(user_data);

	if (!triac_enabled) {
		triac_busy = false;
		return;
	}

	output_gpio_set(&res->triac.enable_gpio, true);
	pulse_end_alarm.ticks = triac_us_to_ticks(res->triac.pulse_width_us);
	pulse_end_alarm.flags = 0U;
	ret = counter_set_channel_alarm(dev, TRIAC_CH_PULSE_END, &pulse_end_alarm);
	if (ret != 0) {
		LOG_ERR("triac pulse end alarm failed: %d", ret);
		output_gpio_set(&res->triac.enable_gpio, false);
		triac_busy = false;
	}
}

int triac_timer_init(void)
{
	int ret;

	res = board_get_resources();
	if (res == NULL) {
		return -ENODEV;
	}

	ret = output_gpio_init(&res->triac.enable_gpio, false);
	if (ret != 0) {
		return ret;
	}

	ret = counter_start(res->triac.counter_dev);
	if ((ret != 0) && (ret != -EALREADY)) {
		LOG_ERR("triac counter start failed: %d", ret);
		return ret;
	}

	fire_alarm.callback = triac_fire_cb;
	fire_alarm.user_data = NULL;
	fire_alarm.flags = 0U;

	pulse_end_alarm.callback = triac_pulse_end_cb;
	pulse_end_alarm.user_data = NULL;
	pulse_end_alarm.flags = 0U;

	return 0;
}

int triac_timer_fire(uint32_t delay_us)
{
	int ret;

	if (!triac_enabled) {
		return 0;
	}

	if (triac_busy) {
		return -EBUSY;
	}

	fire_alarm.ticks = triac_us_to_ticks(delay_us);
	triac_busy = true;
	ret = counter_set_channel_alarm(res->triac.counter_dev, TRIAC_CH_FIRE, &fire_alarm);
	if (ret != 0) {
		triac_busy = false;
		LOG_ERR("triac fire alarm failed: %d", ret);
	}

	return ret;
}

int triac_timer_set_enabled(bool enabled)
{
	triac_enabled = enabled;
	if (!enabled) {
		triac_timer_force_low();
	}

	return 0;
}

void triac_timer_force_low(void)
{
	if (res != NULL) {
		output_gpio_set(&res->triac.enable_gpio, false);
	}
	triac_busy = false;
}

#include <string.h>
#include "sensor_service.h"

#include <errno.h>
#include <stdlib.h>
#include <drivers_app/adc/adc_manager.h>
#include <drivers_app/adc/ntc_convert.h>
#include <drivers_app/adc/ntc_table.h>
#include <drivers_app/gpio/input_gpio.h>
#include <platform/board_devices.h>
#include <services/sensors/gx1832_service.h>
#include <services/sensors/sensor_snapshot.h>
#include <platform/runtime.h>
#include <platform/log.h>

static const struct board_resources *res;
static uint16_t ntc_filtered[BOARD_NTC_COUNT];
static uint32_t ntc_diag_last_log_ms;

int sensor_service_init(void)
{
	int ret;

	res = board_get_resources();
	if (res == NULL) {
		return -ENODEV;
	}

	ret = input_gpio_init(&res->liquid_level, 0U);
	if (ret != 0) {
		return ret;
	}

	ret = input_gpio_init(&res->hall1, 0U);
	if (ret != 0) {
		return ret;
	}

	ret = input_gpio_init(&res->hall2, 0U);
	if (ret != 0) {
		return ret;
	}

	ret = input_gpio_init(&res->gx1832, 0U);
	if (ret != 0) {
		return ret;
	}

	ret = input_gpio_init(&res->fan_sense, 0U);
	if (ret != 0) {
		return ret;
	}

	ret = adc_manager_init();
	if (ret != 0) {
		return ret;
	}

	size_t ntc_table_count = 0U;
	const struct ntc_table_entry *ntc_table = ntc_table_get_default(&ntc_table_count);
	if ((ntc_table != NULL) && (ntc_table_count > 0U)) {
		const struct ntc_table_entry *last = &ntc_table[ntc_table_count - 1U];
		LOG_INF("ntc table max %d.%d C resistance=%u ohm",
			last->temp_deci_c / 10,
			abs(last->temp_deci_c % 10),
			(unsigned int)(last->resistance_deciohms / 10U));
	}

	return sensor_snapshot_init();
}

static uint16_t sensor_service_filter(uint16_t prev, uint16_t raw)
{
	if (prev == 0U) {
		return raw;
	}

	return (uint16_t)(((uint32_t)prev * 3U + raw) / 4U);
}

int sensor_service_sample(sensor_snapshot_t *snapshot)
{
	struct adc_manager_sample sample = { 0 };
	int ret;

	if (snapshot == NULL) {
		return -EINVAL;
	}

	ret = adc_manager_sample_all(&sample);
	if (ret != 0) {
		return ret;
	}

	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->sample_uptime_ms = runtime_now_ms();
	snapshot->sequence = snapshot->sample_uptime_ms;
	snapshot->ntc_raw_max = sample.raw_max;

	for (size_t i = 0; i < BOARD_NTC_COUNT; ++i) {
		struct ntc_convert_result converted;

		snapshot->ntc_raw[i] = sample.raw[i];
		ntc_filtered[i] = sensor_service_filter(ntc_filtered[i], sample.raw[i]);
		converted = ntc_convert_from_raw_for_channel((enum board_ntc_id)i,
			ntc_filtered[i], sample.raw_max, 10000U);
		snapshot->ntc_deci_c[i] = converted.temp_deci_c;

		if (board_ntc_is_enabled((enum board_ntc_id)i)) {
			snapshot->ntc_open[i] = converted.open_circuit;
			snapshot->ntc_short[i] = converted.short_circuit;
		} else {
			snapshot->ntc_open[i] = false;
			snapshot->ntc_short[i] = false;
		}
	}

	if ((snapshot->ntc_deci_c[BOARD_NTC_KETTLE] >= 790) &&
	    ((snapshot->sample_uptime_ms - ntc_diag_last_log_ms) >= 3000U)) {
		ntc_diag_last_log_ms = snapshot->sample_uptime_ms;
		LOG_INF("kettle ntc diag temp=%d.%dC raw=%u filt=%u raw_max=%u",
			snapshot->ntc_deci_c[BOARD_NTC_KETTLE] / 10,
			abs(snapshot->ntc_deci_c[BOARD_NTC_KETTLE] % 10),
			snapshot->ntc_raw[BOARD_NTC_KETTLE],
			ntc_filtered[BOARD_NTC_KETTLE],
			snapshot->ntc_raw_max);
	}

	input_gpio_read(&res->liquid_level, &snapshot->liquid_present);
	input_gpio_read(&res->hall1, &snapshot->cover_closed);
	/* PA12/HALL2 is temporarily unused during bring-up. */
	input_gpio_read(&res->gx1832, &snapshot->gx1832_active);

	sensor_snapshot_publish(snapshot);
	return 0;
}

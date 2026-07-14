#include "adc_manager.h"

#include <errno.h>

#include <platform/board_devices.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(adc_manager, CONFIG_NEBULIZER_LOG_LEVEL);

static bool adc_ready;

int adc_manager_init(void)
{
	const struct board_resources *res = board_get_resources();

	for (size_t i = 0; i < BOARD_NTC_COUNT; ++i) {
		if (!board_ntc_is_enabled((enum board_ntc_id)i)) {
			continue;
		}

		if (!adc_is_ready_dt(&res->ntc[i])) {
			LOG_ERR("adc channel %u not ready", (unsigned int)i);
			return -ENODEV;
		}

		if (adc_channel_setup_dt(&res->ntc[i]) != 0) {
			LOG_ERR("adc setup failed for channel %u", (unsigned int)i);
			return -EIO;
		}
	}

	adc_ready = true;
	return 0;
}

int adc_manager_sample_all(struct adc_manager_sample *sample)
{
	const struct board_resources *res = board_get_resources();
	int16_t raw_buf;
	int ret;

	if ((!adc_ready) || (sample == NULL)) {
		return -EACCES;
	}

	for (size_t i = 0; i < BOARD_NTC_COUNT; ++i) {
		struct adc_sequence sequence = { 0 };

		if (!board_ntc_is_enabled((enum board_ntc_id)i)) {
			sample->raw[i] = 0U;
			continue;
		}

		raw_buf = 0;
		sequence.buffer = &raw_buf;
		sequence.buffer_size = sizeof(raw_buf);

		ret = adc_sequence_init_dt(&res->ntc[i], &sequence);
		if (ret != 0) {
			return ret;
		}

		ret = adc_read(res->ntc[i].dev, &sequence);
		if (ret != 0) {
			LOG_ERR("adc read failed for %u: %d", (unsigned int)i, ret);
			return ret;
		}

		sample->raw[i] = (uint16_t)raw_buf;
	}

	sample->raw_max = BIT(res->ntc[0].resolution) - 1U;
	return 0;
}

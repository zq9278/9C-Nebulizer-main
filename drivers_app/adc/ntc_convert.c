#include "ntc_convert.h"

#include <stddef.h>

#include "ntc_table.h"

static int16_t ntc_interp_temp(uint32_t resistance,
	const struct ntc_table_entry *table, size_t count)
{

	for (size_t i = 1; i < count; ++i) {
		const struct ntc_table_entry *upper = &table[i - 1];
		const struct ntc_table_entry *lower = &table[i];

		if ((resistance <= upper->resistance_deciohms) &&
		    (resistance >= lower->resistance_deciohms)) {
			int32_t dr = (int32_t)upper->resistance_deciohms - (int32_t)lower->resistance_deciohms;
			int32_t dt = (int32_t)upper->temp_deci_c - (int32_t)lower->temp_deci_c;
			int32_t offset = (int32_t)upper->resistance_deciohms - (int32_t)resistance;

			if (dr == 0) {
				return lower->temp_deci_c;
			}

			return (int16_t)(upper->temp_deci_c - ((offset * dt) / dr));
		}
	}

	if (resistance > table[0].resistance_deciohms) {
		return table[0].temp_deci_c;
	}

	return table[count - 1].temp_deci_c;
}

static struct ntc_convert_result ntc_convert_raw(uint16_t raw, uint16_t raw_max,
	uint32_t pullup_ohms, bool outlet)
{
	struct ntc_convert_result result = { 0 };
	uint32_t resistance;
	size_t count;
	const struct ntc_table_entry *table = outlet ? ntc_table_get_outlet_3435(&count) :
		ntc_table_get_default(&count);

	if (raw <= 8U) {
		result.short_circuit = true;
		result.temp_deci_c = 999;
		return result;
	}

	if (raw >= (raw_max - 8U)) {
		result.open_circuit = true;
		result.temp_deci_c = -999;
		return result;
	}

	/* Preserve the legacy channels' whole-ohm rounding; outlets retain 0.1 ohm. */
	resistance = outlet ? ((uint64_t)pullup_ohms * raw * 10U) / (raw_max - raw) :
		(((uint64_t)pullup_ohms * raw) / (raw_max - raw)) * 10U;
	result.temp_deci_c = ntc_interp_temp(resistance, table, count);
	return result;
}

struct ntc_convert_result ntc_convert_from_raw(uint16_t raw, uint16_t raw_max,
	uint32_t pullup_ohms)
{
	return ntc_convert_raw(raw, raw_max, pullup_ohms, false);
}

struct ntc_convert_result ntc_convert_from_raw_for_channel(enum board_ntc_id channel,
	uint16_t raw, uint16_t raw_max, uint32_t pullup_ohms)
{
	return ntc_convert_raw(raw, raw_max, pullup_ohms,
		channel == BOARD_NTC_OUTLET1 || channel == BOARD_NTC_OUTLET2);
}

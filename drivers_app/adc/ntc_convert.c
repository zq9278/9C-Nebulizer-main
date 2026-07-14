#include "ntc_convert.h"

#include <stddef.h>

#include "ntc_table.h"

static int16_t ntc_interp_temp(uint32_t resistance)
{
	size_t count;
	const struct ntc_table_entry *table = ntc_table_get_default(&count);

	for (size_t i = 1; i < count; ++i) {
		const struct ntc_table_entry *upper = &table[i - 1];
		const struct ntc_table_entry *lower = &table[i];

		if ((resistance <= upper->resistance_ohms) &&
		    (resistance >= lower->resistance_ohms)) {
			int32_t dr = (int32_t)upper->resistance_ohms - (int32_t)lower->resistance_ohms;
			int32_t dt = (int32_t)upper->temp_deci_c - (int32_t)lower->temp_deci_c;
			int32_t offset = (int32_t)upper->resistance_ohms - (int32_t)resistance;

			if (dr == 0) {
				return lower->temp_deci_c;
			}

			return (int16_t)(upper->temp_deci_c - ((offset * dt) / dr));
		}
	}

	if (resistance > table[0].resistance_ohms) {
		return table[0].temp_deci_c;
	}

	return table[count - 1].temp_deci_c;
}

struct ntc_convert_result ntc_convert_from_raw(uint16_t raw, uint16_t raw_max,
					       uint32_t pullup_ohms)
{
	struct ntc_convert_result result = { 0 };
	uint32_t resistance;

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

	resistance = ((uint64_t)pullup_ohms * raw) / (raw_max - raw);
	result.temp_deci_c = ntc_interp_temp(resistance);
	return result;
}

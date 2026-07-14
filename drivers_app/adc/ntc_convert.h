#ifndef DRIVERS_APP_ADC_NTC_CONVERT_H_
#define DRIVERS_APP_ADC_NTC_CONVERT_H_

#include <stdbool.h>
#include <stdint.h>

struct ntc_convert_result {
	int16_t temp_deci_c;
	bool open_circuit;
	bool short_circuit;
};

struct ntc_convert_result ntc_convert_from_raw(uint16_t raw, uint16_t raw_max,
					       uint32_t pullup_ohms);

#endif /* DRIVERS_APP_ADC_NTC_CONVERT_H_ */

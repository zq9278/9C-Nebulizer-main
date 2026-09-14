#ifndef DRIVERS_APP_ADC_NTC_TABLE_H_
#define DRIVERS_APP_ADC_NTC_TABLE_H_

#include <stddef.h>
#include <stdint.h>

struct ntc_table_entry {
	int16_t temp_deci_c;
	uint32_t resistance_deciohms;
};

const struct ntc_table_entry *ntc_table_get_default(size_t *count);
const struct ntc_table_entry *ntc_table_get_outlet_3435(size_t *count);

#endif /* DRIVERS_APP_ADC_NTC_TABLE_H_ */

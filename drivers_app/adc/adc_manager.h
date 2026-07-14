#ifndef DRIVERS_APP_ADC_ADC_MANAGER_H_
#define DRIVERS_APP_ADC_ADC_MANAGER_H_

#include <stdint.h>

#include <platform/board_resources.h>

struct adc_manager_sample {
	uint16_t raw[BOARD_NTC_COUNT];
	uint16_t raw_max;
};

int adc_manager_init(void);
int adc_manager_sample_all(struct adc_manager_sample *sample);

#endif /* DRIVERS_APP_ADC_ADC_MANAGER_H_ */

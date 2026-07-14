#ifndef SERVICES_HEAT_TRIAC_CONTROL_H_
#define SERVICES_HEAT_TRIAC_CONTROL_H_

#include <stdbool.h>
#include <stdint.h>

int triac_control_init(void);
int triac_control_set_output_permille(uint16_t output_permille);
int triac_control_set_enabled(bool enabled);
int triac_control_stop(void);

#endif /* SERVICES_HEAT_TRIAC_CONTROL_H_ */

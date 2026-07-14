#ifndef DRIVERS_APP_TIMER_TRIAC_TIMER_H_
#define DRIVERS_APP_TIMER_TRIAC_TIMER_H_

#include <stdbool.h>
#include <stdint.h>

int triac_timer_init(void);
int triac_timer_fire(uint32_t delay_us);
int triac_timer_set_enabled(bool enabled);
void triac_timer_force_low(void);

#endif /* DRIVERS_APP_TIMER_TRIAC_TIMER_H_ */

#ifndef SERVICES_FAN_FAN_CONTROL_H_
#define SERVICES_FAN_FAN_CONTROL_H_

#include <nebulizer/app_types.h>

int fan_control_init(void);
int fan_control_set_level(air_level_t level);
int fan_control_stop(void);

#endif /* SERVICES_FAN_FAN_CONTROL_H_ */

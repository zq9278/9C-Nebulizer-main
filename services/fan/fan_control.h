#ifndef SERVICES_FAN_FAN_CONTROL_H_
#define SERVICES_FAN_FAN_CONTROL_H_

#include <nebulizer/app_types.h>

int fan_control_init(void);
int fan_control_step(const telemetry_status_t *status);
int fan_control_set_level(air_level_t level);
int fan_control_stop(void);
int fan_control_set_pid(const pid_params_t *pid);
void fan_control_get_pid(pid_params_t *pid);
void fan_control_get_diag(fan_control_diag_t *diag);

#endif /* SERVICES_FAN_FAN_CONTROL_H_ */

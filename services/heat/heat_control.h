#ifndef SERVICES_HEAT_HEAT_CONTROL_H_
#define SERVICES_HEAT_HEAT_CONTROL_H_

#include <nebulizer/app_types.h>

int heat_control_init(void);
fault_code_t heat_control_step(const telemetry_status_t *status);
int heat_control_stop(void);
int heat_control_set_pid(const pid_params_t *pid);
void heat_control_get_pid(pid_params_t *pid);
void heat_control_get_diag(heat_control_diag_t *diag);
int heat_control_manual_set(uint16_t output_permille);
int heat_control_manual_stop(void);

#endif /* SERVICES_HEAT_HEAT_CONTROL_H_ */

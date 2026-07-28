#ifndef SERVICES_HEAT_HEAT_CONTROL_H_
#define SERVICES_HEAT_HEAT_CONTROL_H_

#include <nebulizer/app_types.h>

int heat_control_init(void);
fault_code_t heat_control_step(const telemetry_status_t *status);
int heat_control_stop(void);
int heat_control_set_pid(const pid_params_t *pid);
void heat_control_get_pid(pid_params_t *pid);
int heat_control_set_outlet_params(const outlet_control_params_t *params);
void heat_control_get_outlet_params(outlet_control_params_t *params);
int heat_control_set_kettle_target_override(const kettle_target_override_t *override);
void heat_control_get_kettle_target_override(kettle_target_override_t *override);
void heat_control_get_diag(heat_control_diag_t *diag);
int heat_control_manual_set(uint16_t output_permille);
int heat_control_manual_stop(void);

#endif /* SERVICES_HEAT_HEAT_CONTROL_H_ */

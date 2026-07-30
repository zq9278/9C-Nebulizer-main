#ifndef SERVICES_STORAGE_SETTINGS_DEFAULTS_H_
#define SERVICES_STORAGE_SETTINGS_DEFAULTS_H_

#include <nebulizer/app_types.h>

void settings_defaults_get(treatment_config_t *config);
void settings_defaults_get_pid(pid_params_t *pid);
void settings_defaults_get_preheat_pid(pid_params_t *pid);
void settings_defaults_get_fan_pid(pid_params_t *pid);

#endif /* SERVICES_STORAGE_SETTINGS_DEFAULTS_H_ */

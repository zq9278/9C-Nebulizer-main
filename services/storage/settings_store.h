#ifndef SERVICES_STORAGE_SETTINGS_STORE_H_
#define SERVICES_STORAGE_SETTINGS_STORE_H_

#include <nebulizer/app_types.h>

int settings_store_init(void);
int settings_store_load(treatment_config_t *config);
int settings_store_save(const treatment_config_t *config);
int settings_store_save_delayed(const treatment_config_t *config);
int settings_store_load_pid(pid_params_t *pid);
int settings_store_save_pid(const pid_params_t *pid);
int settings_store_save_pid_delayed(const pid_params_t *pid);
int settings_store_load_preheat_pid(pid_params_t *pid);
int settings_store_save_preheat_pid(const pid_params_t *pid);
int settings_store_save_preheat_pid_delayed(const pid_params_t *pid);
int settings_store_load_fan_pid(pid_params_t *pid);
int settings_store_save_fan_pid(const pid_params_t *pid);
int settings_store_save_fan_pid_delayed(const pid_params_t *pid);

#endif /* SERVICES_STORAGE_SETTINGS_STORE_H_ */

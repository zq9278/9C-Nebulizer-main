#include "settings_defaults.h"

#include <nebulizer/app_config.h>

void settings_defaults_get(treatment_config_t *config)
{
	config->mode = TREATMENT_MODE_HOT;
	config->target_temp_deci_c = APP_DEFAULT_TARGET_TEMP_DECI_C;
	config->duration_sec = APP_DEFAULT_TREATMENT_TIME_SEC;
	config->air_level = AIR_LEVEL_MID;
	config->mist_level = MIST_LEVEL_UI_MID;
}

void settings_defaults_get_pid(pid_params_t *pid)
{
	pid->kp_milli = APP_HEAT_PID_KP_DEFAULT_MILLI;
	pid->ki_milli = APP_HEAT_PID_KI_DEFAULT_MILLI;
	pid->kd_milli = APP_HEAT_PID_KD_DEFAULT_MILLI;
	pid->integral_limit_permille = APP_HEAT_PID_I_LIMIT_DEFAULT;
}

void settings_defaults_get_preheat_pid(pid_params_t *pid)
{
	pid->kp_milli = APP_HEAT_PREHEAT_PID_KP_DEFAULT_MILLI;
	pid->ki_milli = APP_HEAT_PREHEAT_PID_KI_DEFAULT_MILLI;
	pid->kd_milli = APP_HEAT_PREHEAT_PID_KD_DEFAULT_MILLI;
	pid->integral_limit_permille = APP_HEAT_PREHEAT_PID_I_LIMIT_DEFAULT;
}

void settings_defaults_get_fan_pid(pid_params_t *pid)
{
	pid->kp_milli = APP_FAN_PID_KP_DEFAULT_MILLI;
	pid->ki_milli = APP_FAN_PID_KI_DEFAULT_MILLI;
	pid->kd_milli = APP_FAN_PID_KD_DEFAULT_MILLI;
	pid->integral_limit_permille = APP_FAN_PID_I_LIMIT_DEFAULT_PERMILLE;
}

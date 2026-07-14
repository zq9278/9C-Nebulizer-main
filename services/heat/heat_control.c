#include "heat_control.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <nebulizer/app_config.h>
#include <platform/board_resources.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <services/heat/triac_control.h>

LOG_MODULE_REGISTER(heat_control, CONFIG_NEBULIZER_LOG_LEVEL);

struct heat_control_ctx {
	struct k_mutex lock;
	pid_params_t pid;
	heat_control_diag_t diag;
	int16_t last_error_deci_c;
	bool first_sample;
	bool kettle_heat_limited;
};

static struct heat_control_ctx heat_ctx;

static uint16_t heat_control_limit_near_target(uint16_t output_permille, int16_t error_deci_c)
{
	if (error_deci_c <= APP_HEAT_NEAR_TARGET_BAND1_DECI_C) {
		return MIN(output_permille, APP_HEAT_NEAR_TARGET_MAX1_PERMILLE);
	}

	if (error_deci_c <= APP_HEAT_NEAR_TARGET_BAND2_DECI_C) {
		return MIN(output_permille, APP_HEAT_NEAR_TARGET_MAX2_PERMILLE);
	}

	if (error_deci_c <= APP_HEAT_NEAR_TARGET_BAND3_DECI_C) {
		return MIN(output_permille, APP_HEAT_NEAR_TARGET_MAX3_PERMILLE);
	}

	return output_permille;
}

static void heat_control_reset_locked(void)
{
	memset(&heat_ctx.diag, 0, sizeof(heat_ctx.diag));
	heat_ctx.diag.output_delay_us = TRIAC_MAX_DELAY_US;
	heat_ctx.first_sample = true;
	heat_ctx.last_error_deci_c = 0;
}

static void heat_control_publish_idle_diag_locked(int16_t measured_temp, int16_t target_temp,
						      int16_t error_deci_c)
{
	heat_control_reset_locked();
	heat_ctx.diag.measured_temp_deci_c = measured_temp;
	heat_ctx.diag.target_temp_deci_c = target_temp;
	heat_ctx.diag.error_deci_c = error_deci_c;
}

static bool heat_control_pid_valid(const pid_params_t *pid)
{
	if (pid == NULL) {
		return false;
	}

	return (pid->kp_milli >= 0) && (pid->kp_milli <= APP_HEAT_PID_KP_MAX_MILLI) &&
	       (pid->ki_milli >= 0) && (pid->ki_milli <= APP_HEAT_PID_KI_MAX_MILLI) &&
	       (pid->kd_milli >= 0) && (pid->kd_milli <= APP_HEAT_PID_KD_MAX_MILLI) &&
	       (pid->integral_limit_permille >= 0) &&
	       (pid->integral_limit_permille <= APP_HEAT_PID_I_LIMIT_MAX);
}

int heat_control_init(void)
{
	k_mutex_init(&heat_ctx.lock);
	heat_ctx.pid.kp_milli = APP_HEAT_PID_KP_DEFAULT_MILLI;
	heat_ctx.pid.ki_milli = APP_HEAT_PID_KI_DEFAULT_MILLI;
	heat_ctx.pid.kd_milli = APP_HEAT_PID_KD_DEFAULT_MILLI;
	heat_ctx.pid.integral_limit_permille = APP_HEAT_PID_I_LIMIT_DEFAULT;
	heat_control_reset_locked();
	return triac_control_init();
}

fault_code_t heat_control_step(const telemetry_status_t *status)
{
	int16_t outlet_temp;
	int16_t kettle_temp;
	int16_t error_deci_c;
	int32_t i_limit_raw;
	int32_t output_raw;
	uint32_t output_permille;
	bool was_limited;

	if (status == NULL) {
		return FAULT_INIT_FAILED;
	}

	outlet_temp = status->sensors.ntc_deci_c[BOARD_NTC_OUTLET1];
	kettle_temp = status->sensors.ntc_deci_c[BOARD_NTC_KETTLE];

	if ((status->config.mode != TREATMENT_MODE_HOT) ||
	    (status->state != TREATMENT_STATE_RUNNING_HOT)) {
		heat_control_stop();
		return FAULT_NONE;
	}

	if (status->sensors.ntc_open[BOARD_NTC_OUTLET1] ||
	    status->sensors.ntc_open[BOARD_NTC_KETTLE]) {
		heat_control_stop();
		return FAULT_SENSOR_NTC_OPEN;
	}

	if (status->sensors.ntc_short[BOARD_NTC_OUTLET1] ||
	    status->sensors.ntc_short[BOARD_NTC_KETTLE]) {
		heat_control_stop();
		return FAULT_SENSOR_NTC_SHORT;
	}

	if (outlet_temp >= APP_OUTLET1_OVER_TEMP_FAULT_DECI_C) {
		heat_control_stop();
		return FAULT_OUTLET1_OVER_TEMP;
	}

	if (kettle_temp >= APP_KETTLE_OVER_TEMP_FAULT_DECI_C) {
		heat_control_stop();
		return FAULT_KETTLE_OVER_TEMP;
	}

	error_deci_c = (int16_t)status->config.target_temp_deci_c - outlet_temp;

	if (kettle_temp >= APP_KETTLE_HEAT_CUTOFF_DECI_C) {
		k_mutex_lock(&heat_ctx.lock, K_FOREVER);
		was_limited = heat_ctx.kettle_heat_limited;
		heat_ctx.kettle_heat_limited = true;
		heat_control_publish_idle_diag_locked(outlet_temp,
						      (int16_t)status->config.target_temp_deci_c,
						      error_deci_c);
		k_mutex_unlock(&heat_ctx.lock);

		if (!was_limited) {
			LOG_WRN("kettle temp limit active: kettle=%d target=%u cutoff=%u",
				kettle_temp,
				(unsigned int)status->config.target_temp_deci_c,
				(unsigned int)APP_KETTLE_HEAT_CUTOFF_DECI_C);
		}

		(void)triac_control_stop();
		return FAULT_NONE;
	}

	k_mutex_lock(&heat_ctx.lock, K_FOREVER);
	was_limited = heat_ctx.kettle_heat_limited;
	heat_ctx.kettle_heat_limited = false;
	k_mutex_unlock(&heat_ctx.lock);

	if (was_limited) {
		LOG_INF("kettle temp recovered below cutoff: kettle=%d cutoff=%u",
			kettle_temp, (unsigned int)APP_KETTLE_HEAT_CUTOFF_DECI_C);
	}

	k_mutex_lock(&heat_ctx.lock, K_FOREVER);
	heat_ctx.diag.enabled = true;
	heat_ctx.diag.measured_temp_deci_c = outlet_temp;
	heat_ctx.diag.target_temp_deci_c = (int16_t)status->config.target_temp_deci_c;
	heat_ctx.diag.error_deci_c = error_deci_c;

	if (error_deci_c <= 0) {
		heat_control_reset_locked();
		k_mutex_unlock(&heat_ctx.lock);
		heat_control_stop();
		return FAULT_NONE;
	}

	heat_ctx.diag.p_term_raw = heat_ctx.pid.kp_milli * error_deci_c;
	heat_ctx.diag.i_term_raw += (heat_ctx.pid.ki_milli * error_deci_c * APP_CONTROL_PERIOD_MS) / 100;
	i_limit_raw = heat_ctx.pid.integral_limit_permille * 1000;
	if (heat_ctx.diag.i_term_raw > i_limit_raw) {
		heat_ctx.diag.i_term_raw = i_limit_raw;
	}
	if (heat_ctx.diag.i_term_raw < -i_limit_raw) {
		heat_ctx.diag.i_term_raw = -i_limit_raw;
	}
	heat_ctx.diag.d_term_raw = 0;
	heat_ctx.last_error_deci_c = error_deci_c;
	heat_ctx.first_sample = false;

	output_raw = heat_ctx.diag.p_term_raw + heat_ctx.diag.i_term_raw + heat_ctx.diag.d_term_raw;
	if (output_raw < 0) {
		output_raw = 0;
	}

	output_permille = (uint32_t)(output_raw / 1000);
	heat_ctx.diag.saturated = output_permille >= 1000U;
	if (output_permille > 1000U) {
		output_permille = 1000U;
	}
	output_permille = heat_control_limit_near_target((uint16_t)output_permille, error_deci_c);
	heat_ctx.diag.output_permille = (uint16_t)output_permille;

	if (output_permille == 0U) {
		heat_control_reset_locked();
		k_mutex_unlock(&heat_ctx.lock);
		heat_control_stop();
		return FAULT_NONE;
	}

	heat_ctx.diag.output_delay_us = 0U;
	k_mutex_unlock(&heat_ctx.lock);

	triac_control_set_output_permille((uint16_t)output_permille);
	triac_control_set_enabled(true);
	return FAULT_NONE;
}

int heat_control_stop(void)
{
	k_mutex_lock(&heat_ctx.lock, K_FOREVER);
	heat_control_reset_locked();
	heat_ctx.kettle_heat_limited = false;
	k_mutex_unlock(&heat_ctx.lock);
	return triac_control_stop();
}

int heat_control_manual_set(uint16_t output_permille)
{
	if (output_permille > 1000U) {
		return -EINVAL;
	}

	if (output_permille == 0U) {
		return heat_control_manual_stop();
	}

	k_mutex_lock(&heat_ctx.lock, K_FOREVER);
	memset(&heat_ctx.diag, 0, sizeof(heat_ctx.diag));
	heat_ctx.diag.enabled = true;
	heat_ctx.diag.output_permille = output_permille;
	heat_ctx.diag.output_delay_us = 0U;
	k_mutex_unlock(&heat_ctx.lock);

	triac_control_set_output_permille(output_permille);
	return triac_control_set_enabled(true);
}

int heat_control_manual_stop(void)
{
	return heat_control_stop();
}

int heat_control_set_pid(const pid_params_t *pid)
{
	if (!heat_control_pid_valid(pid)) {
		return -EINVAL;
	}

	k_mutex_lock(&heat_ctx.lock, K_FOREVER);
	heat_ctx.pid = *pid;
	heat_ctx.first_sample = true;
	heat_ctx.last_error_deci_c = 0;
	heat_ctx.diag.i_term_raw = 0;
	k_mutex_unlock(&heat_ctx.lock);

	LOG_INF("heat pid updated kp=%d ki=%d kd=%d i_limit=%d",
		pid->kp_milli, pid->ki_milli, pid->kd_milli, pid->integral_limit_permille);
	return 0;
}

void heat_control_get_pid(pid_params_t *pid)
{
	if (pid == NULL) {
		return;
	}

	k_mutex_lock(&heat_ctx.lock, K_FOREVER);
	*pid = heat_ctx.pid;
	k_mutex_unlock(&heat_ctx.lock);
}

void heat_control_get_diag(heat_control_diag_t *diag)
{
	if (diag == NULL) {
		return;
	}

	k_mutex_lock(&heat_ctx.lock, K_FOREVER);
	*diag = heat_ctx.diag;
	k_mutex_unlock(&heat_ctx.lock);
}

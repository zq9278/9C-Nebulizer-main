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
	pid_params_t outlet_pid;
	pid_params_t preheat_pid;
	heat_control_diag_t diag;
	int16_t last_error_deci_c;
	uint32_t last_pid_update_ms;
	bool first_sample;
	bool pa5_heat_limited;
};

static struct heat_control_ctx heat_ctx;

static void heat_control_reset_pid_terms_locked(void)
{
	heat_ctx.diag.saturated = false;
	heat_ctx.diag.p_term_raw = 0;
	heat_ctx.diag.i_term_raw = 0;
	heat_ctx.diag.d_term_raw = 0;
	heat_ctx.diag.output_delay_us = TRIAC_MAX_DELAY_US;
	heat_ctx.diag.output_permille = 0U;
	heat_ctx.first_sample = true;
	heat_ctx.last_error_deci_c = 0;
	heat_ctx.last_pid_update_ms = 0U;
}

static void heat_control_prepare_phase_locked(heat_control_phase_t phase)
{
	if (heat_ctx.diag.phase == phase) {
		return;
	}

	memset(&heat_ctx.diag, 0, sizeof(heat_ctx.diag));
	heat_ctx.diag.phase = phase;
	heat_control_reset_pid_terms_locked();
}

static void heat_control_publish_idle_diag_locked(heat_control_phase_t phase,
					       int16_t measured_temp,
					       int16_t target_temp,
					       int16_t error_deci_c)
{
	heat_control_prepare_phase_locked(phase);
	heat_ctx.diag.enabled = false;
	heat_ctx.diag.measured_temp_deci_c = measured_temp;
	heat_ctx.diag.target_temp_deci_c = target_temp;
	heat_ctx.diag.error_deci_c = error_deci_c;
	heat_control_reset_pid_terms_locked();
}

static void heat_control_reset_locked(void)
{
	memset(&heat_ctx.diag, 0, sizeof(heat_ctx.diag));
	heat_ctx.diag.phase = HEAT_CONTROL_PHASE_IDLE;
	heat_ctx.diag.output_delay_us = TRIAC_MAX_DELAY_US;
	heat_ctx.first_sample = true;
	heat_ctx.last_error_deci_c = 0;
	heat_ctx.last_pid_update_ms = 0U;
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
	heat_ctx.outlet_pid.kp_milli = APP_HEAT_PID_KP_DEFAULT_MILLI;
	heat_ctx.outlet_pid.ki_milli = APP_HEAT_PID_KI_DEFAULT_MILLI;
	heat_ctx.outlet_pid.kd_milli = APP_HEAT_PID_KD_DEFAULT_MILLI;
	heat_ctx.outlet_pid.integral_limit_permille = APP_HEAT_PID_I_LIMIT_DEFAULT;
	heat_ctx.preheat_pid.kp_milli = APP_HEAT_PREHEAT_PID_KP_DEFAULT_MILLI;
	heat_ctx.preheat_pid.ki_milli = APP_HEAT_PREHEAT_PID_KI_DEFAULT_MILLI;
	heat_ctx.preheat_pid.kd_milli = APP_HEAT_PREHEAT_PID_KD_DEFAULT_MILLI;
	heat_ctx.preheat_pid.integral_limit_permille = APP_HEAT_PREHEAT_PID_I_LIMIT_DEFAULT;
	heat_control_reset_locked();
	return triac_control_init();
}

fault_code_t heat_control_step(const telemetry_status_t *status)
{
	const pid_params_t *active_pid;
	heat_control_phase_t phase;
	heat_control_phase_t previous_phase;
	int16_t outlet_temp;
	int16_t kettle_temp;
	int16_t overtemp_protect_temp;
	int16_t measured_temp;
	int16_t target_temp;
	int16_t error_deci_c;
	int16_t pid_error_delta_deci_c;
	int32_t i_limit_raw;
	int32_t output_raw;
	int32_t candidate_i_term_raw;
	int64_t integral_raw;
	uint32_t output_permille;
	uint32_t output_max_permille;
	uint32_t pid_period_ms;
	uint32_t now_ms;
	uint32_t elapsed_ms;
	uint16_t active_pa5_stop_deci_c;
	bool was_limited;
	bool phase_changed;

	if (status == NULL) {
		return FAULT_INIT_FAILED;
	}

	outlet_temp = status->sensors.ntc_deci_c[BOARD_NTC_OUTLET1];
	kettle_temp = status->sensors.ntc_deci_c[BOARD_NTC_KETTLE];
	overtemp_protect_temp = status->sensors.ntc_deci_c[BOARD_NTC_OUTLET2];

	if ((status->config.mode != TREATMENT_MODE_HOT) ||
	    (status->state != TREATMENT_STATE_RUNNING_HOT)) {
		heat_control_stop();
		return FAULT_NONE;
	}

	if (status->sensors.ntc_open[BOARD_NTC_OUTLET1] ||
	    status->sensors.ntc_open[BOARD_NTC_KETTLE] ||
	    status->sensors.ntc_open[BOARD_NTC_OUTLET2]) {
		heat_control_stop();
		return FAULT_SENSOR_NTC_OPEN;
	}

	if (status->sensors.ntc_short[BOARD_NTC_OUTLET1] ||
	    status->sensors.ntc_short[BOARD_NTC_KETTLE] ||
	    status->sensors.ntc_short[BOARD_NTC_OUTLET2]) {
		heat_control_stop();
		return FAULT_SENSOR_NTC_SHORT;
	}

	if (overtemp_protect_temp >= APP_OUTLET1_OVER_TEMP_FAULT_DECI_C) {
		heat_control_stop();
		return FAULT_OVER_TEMP;
	}

	now_ms = k_uptime_get_32();
	phase = (outlet_temp < APP_HEAT_FULL_POWER_BELOW_DECI_C) ?
		HEAT_CONTROL_PHASE_PB11_FULL_POWER : HEAT_CONTROL_PHASE_PB11_OUTLET;
	measured_temp = outlet_temp;
	if (phase == HEAT_CONTROL_PHASE_PB11_FULL_POWER) {
		target_temp = APP_HEAT_FULL_POWER_BELOW_DECI_C;
		active_pa5_stop_deci_c = APP_HEAT_PA5_STOP_DECI_C;
		output_max_permille = APP_HEAT_FULL_POWER_PERMILLE;
	} else {
		target_temp = (int16_t)status->config.target_temp_deci_c;
		active_pa5_stop_deci_c = APP_HEAT_PID_PA5_STOP_DECI_C;
		output_max_permille = APP_HEAT_PID_OUTPUT_MAX_PERMILLE;
	}
	error_deci_c = target_temp - measured_temp;

	k_mutex_lock(&heat_ctx.lock, K_FOREVER);
	previous_phase = heat_ctx.diag.phase;
	heat_control_prepare_phase_locked(phase);
	phase_changed = previous_phase != phase;
	k_mutex_unlock(&heat_ctx.lock);

	if (phase_changed) {
		LOG_INF("heat phase changed: PB11=%d threshold=%d phase=%u",
			outlet_temp, APP_HEAT_FULL_POWER_BELOW_DECI_C, (unsigned int)phase);
	}

	if (kettle_temp >= active_pa5_stop_deci_c) {
		k_mutex_lock(&heat_ctx.lock, K_FOREVER);
		was_limited = heat_ctx.pa5_heat_limited;
		heat_ctx.pa5_heat_limited = true;
		heat_control_publish_idle_diag_locked(phase, measured_temp, target_temp,
						      error_deci_c);
		k_mutex_unlock(&heat_ctx.lock);

		if (!was_limited) {
			LOG_WRN("PA5 heat cutoff active: temp=%d cutoff=%u phase=%u",
				kettle_temp, (unsigned int)active_pa5_stop_deci_c,
				(unsigned int)phase);
		}

		(void)triac_control_stop();
		return FAULT_NONE;
	}

	k_mutex_lock(&heat_ctx.lock, K_FOREVER);
	was_limited = heat_ctx.pa5_heat_limited;
	heat_ctx.pa5_heat_limited = false;
	k_mutex_unlock(&heat_ctx.lock);

	if (was_limited) {
		LOG_INF("PA5 recovered below heat cutoff: temp=%d cutoff=%u phase=%u",
			kettle_temp, (unsigned int)active_pa5_stop_deci_c,
			(unsigned int)phase);
	}

	k_mutex_lock(&heat_ctx.lock, K_FOREVER);
	heat_control_prepare_phase_locked(phase);
	heat_ctx.diag.enabled = true;
	heat_ctx.diag.measured_temp_deci_c = measured_temp;
	heat_ctx.diag.target_temp_deci_c = target_temp;
	heat_ctx.diag.error_deci_c = error_deci_c;

	if (phase == HEAT_CONTROL_PHASE_PB11_FULL_POWER) {
		heat_ctx.diag.saturated = false;
		heat_ctx.diag.p_term_raw = 0;
		heat_ctx.diag.i_term_raw = 0;
		heat_ctx.diag.d_term_raw = 0;
		heat_ctx.diag.output_permille = (uint16_t)output_max_permille;
		heat_ctx.diag.output_delay_us = 0U;
		k_mutex_unlock(&heat_ctx.lock);
		triac_control_set_output_permille((uint16_t)output_max_permille);
		triac_control_set_enabled(true);
		return FAULT_NONE;
	}

	/* PB11 达到出口目标后立即停热并清空 PID 积分。 */
	if (error_deci_c <= 0) {
		heat_control_publish_idle_diag_locked(phase, measured_temp, target_temp,
						      error_deci_c);
		k_mutex_unlock(&heat_ctx.lock);
		(void)triac_control_stop();
		return FAULT_NONE;
	}

	pid_period_ms = APP_HEAT_PID_PERIOD_MS;
	elapsed_ms = (heat_ctx.last_pid_update_ms == 0U) ?
		     pid_period_ms : (now_ms - heat_ctx.last_pid_update_ms);

	if (elapsed_ms < pid_period_ms) {
		output_raw = heat_ctx.diag.p_term_raw + heat_ctx.diag.i_term_raw +
			     heat_ctx.diag.d_term_raw;
		if (output_raw < 0) {
			output_raw = 0;
		}
		output_permille = (uint32_t)(output_raw / 1000);
		heat_ctx.diag.saturated = output_permille >= output_max_permille;
		if (output_permille > output_max_permille) {
			output_permille = output_max_permille;
		}
		heat_ctx.diag.output_permille = (uint16_t)output_permille;
		heat_ctx.diag.output_delay_us =
			(output_permille == 0U) ? TRIAC_MAX_DELAY_US : 0U;
		k_mutex_unlock(&heat_ctx.lock);
		if (output_permille == 0U) {
			(void)triac_control_stop();
		} else {
			triac_control_set_output_permille((uint16_t)output_permille);
			triac_control_set_enabled(true);
		}
		return FAULT_NONE;
	}
	heat_ctx.last_pid_update_ms = now_ms;
	active_pid = &heat_ctx.outlet_pid;

	heat_ctx.diag.p_term_raw = active_pid->kp_milli * error_deci_c;
	if (heat_ctx.first_sample) {
		heat_ctx.diag.d_term_raw = 0;
	} else {
		pid_error_delta_deci_c = error_deci_c - heat_ctx.last_error_deci_c;
		heat_ctx.diag.d_term_raw = active_pid->kd_milli * pid_error_delta_deci_c;
	}

	candidate_i_term_raw = heat_ctx.diag.i_term_raw;
	integral_raw = (int64_t)heat_ctx.diag.i_term_raw +
		       ((int64_t)active_pid->ki_milli * error_deci_c * elapsed_ms) / 100;
	i_limit_raw = active_pid->integral_limit_permille * 1000;
	if (integral_raw > i_limit_raw) {
		integral_raw = i_limit_raw;
	}
	if (integral_raw < -i_limit_raw) {
		integral_raw = -i_limit_raw;
	}
	candidate_i_term_raw = (int32_t)integral_raw;

	/* 正误差且输出已饱和时不接受新的积分，防止高延迟反馈造成 windup。 */
	output_raw = heat_ctx.diag.p_term_raw + candidate_i_term_raw +
		     heat_ctx.diag.d_term_raw;
	if ((output_raw <= (int32_t)(output_max_permille * 1000U)) ||
	    (candidate_i_term_raw <= heat_ctx.diag.i_term_raw)) {
		heat_ctx.diag.i_term_raw = candidate_i_term_raw;
	}

	heat_ctx.last_error_deci_c = error_deci_c;
	heat_ctx.first_sample = false;
	output_raw = heat_ctx.diag.p_term_raw + heat_ctx.diag.i_term_raw +
		     heat_ctx.diag.d_term_raw;
	if (output_raw < 0) {
		output_raw = 0;
	}

	output_permille = (uint32_t)(output_raw / 1000);
	heat_ctx.diag.saturated = output_permille >= output_max_permille;
	if (output_permille > output_max_permille) {
		output_permille = output_max_permille;
	}
	heat_ctx.diag.output_permille = (uint16_t)output_permille;

	if (output_permille == 0U) {
		heat_ctx.diag.output_delay_us = TRIAC_MAX_DELAY_US;
		k_mutex_unlock(&heat_ctx.lock);
		(void)triac_control_stop();
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
	heat_ctx.pa5_heat_limited = false;
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
	heat_ctx.diag.phase = HEAT_CONTROL_PHASE_IDLE;
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
	heat_ctx.outlet_pid = *pid;
	if (heat_ctx.diag.phase == HEAT_CONTROL_PHASE_PB11_OUTLET) {
		heat_control_reset_pid_terms_locked();
	}
	k_mutex_unlock(&heat_ctx.lock);

	LOG_INF("outlet pid updated kp=%d ki=%d kd=%d i_limit=%d",
		pid->kp_milli, pid->ki_milli, pid->kd_milli, pid->integral_limit_permille);
	return 0;
}

void heat_control_get_pid(pid_params_t *pid)
{
	if (pid == NULL) {
		return;
	}

	k_mutex_lock(&heat_ctx.lock, K_FOREVER);
	*pid = heat_ctx.outlet_pid;
	k_mutex_unlock(&heat_ctx.lock);
}

int heat_control_set_preheat_pid(const pid_params_t *pid)
{
	if (!heat_control_pid_valid(pid)) {
		return -EINVAL;
	}

	k_mutex_lock(&heat_ctx.lock, K_FOREVER);
	heat_ctx.preheat_pid = *pid;
	k_mutex_unlock(&heat_ctx.lock);

	LOG_INF("legacy preheat pid updated (not used by heat control) kp=%d ki=%d kd=%d i_limit=%d",
		pid->kp_milli, pid->ki_milli, pid->kd_milli, pid->integral_limit_permille);
	return 0;
}

void heat_control_get_preheat_pid(pid_params_t *pid)
{
	if (pid == NULL) {
		return;
	}

	k_mutex_lock(&heat_ctx.lock, K_FOREVER);
	*pid = heat_ctx.preheat_pid;
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

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
	outlet_control_params_t outlet;
	kettle_target_override_t kettle_target_override;
	heat_control_diag_t diag;
	int16_t last_error_deci_c;
	uint32_t last_pid_update_ms;
	bool first_sample;
	bool kettle_heat_limited;
};

static struct heat_control_ctx heat_ctx;

static int16_t heat_control_clamp_deci_c(int32_t value, int16_t min_value, int16_t max_value)
{
	if (value < min_value) {
		return min_value;
	}

	if (value > max_value) {
		return max_value;
	}

	return (int16_t)value;
}

static int16_t heat_control_air_offset_deci_c(air_level_t level)
{
	switch (level) {
	case AIR_LEVEL_LOW:
		return heat_ctx.outlet.air_low_offset_deci_c;
	case AIR_LEVEL_MID:
		return heat_ctx.outlet.air_mid_offset_deci_c;
	case AIR_LEVEL_HIGH:
		return heat_ctx.outlet.air_high_offset_deci_c;
	case AIR_LEVEL_OFF:
	default:
		return 0;
	}
}

static int16_t heat_control_mist_offset_deci_c(mist_level_t level)
{
	switch (level) {
	case MIST_LEVEL_UI_LOW:
		return heat_ctx.outlet.mist_low_offset_deci_c;
	case MIST_LEVEL_UI_MID:
		return heat_ctx.outlet.mist_mid_offset_deci_c;
	case MIST_LEVEL_UI_HIGH:
		return heat_ctx.outlet.mist_high_offset_deci_c;
	case MIST_LEVEL_UI_OFF:
	default:
		return 0;
	}
}

static int16_t heat_control_kettle_target_deci_c(const treatment_config_t *config)
{
	ARG_UNUSED(config);

	if (heat_ctx.kettle_target_override.enabled) {
		return heat_ctx.kettle_target_override.target_deci_c;
	}

	/*
	 * 自动模式先不再按“出雾口目标 + 风扇/雾化补偿”计算锅体目标。
	 * 锅体温度直接跟踪固定目标，便于把内层锅温 PID 单独调顺。
	 */
	return APP_HEAT_KETTLE_FIXED_TARGET_DECI_C;
}

static bool heat_control_outlet_params_valid(const outlet_control_params_t *params)
{
	if (params == NULL) {
		return false;
	}

	return (params->base_offset_deci_c >= 0) &&
	       (params->base_offset_deci_c <= APP_HEAT_KETTLE_BASE_OFFSET_MAX_DECI_C) &&
	       (params->target_margin_deci_c >= 0) &&
	       (params->target_margin_deci_c <= APP_HEAT_KETTLE_TARGET_MARGIN_MAX_DECI_C) &&
	       (params->air_low_offset_deci_c >= 0) &&
	       (params->air_low_offset_deci_c <= APP_HEAT_FEEDFORWARD_OFFSET_MAX_DECI_C) &&
	       (params->air_mid_offset_deci_c >= 0) &&
	       (params->air_mid_offset_deci_c <= APP_HEAT_FEEDFORWARD_OFFSET_MAX_DECI_C) &&
	       (params->air_high_offset_deci_c >= 0) &&
	       (params->air_high_offset_deci_c <= APP_HEAT_FEEDFORWARD_OFFSET_MAX_DECI_C) &&
	       (params->mist_low_offset_deci_c >= 0) &&
	       (params->mist_low_offset_deci_c <= APP_HEAT_FEEDFORWARD_OFFSET_MAX_DECI_C) &&
	       (params->mist_mid_offset_deci_c >= 0) &&
	       (params->mist_mid_offset_deci_c <= APP_HEAT_FEEDFORWARD_OFFSET_MAX_DECI_C) &&
	       (params->mist_high_offset_deci_c >= 0) &&
	       (params->mist_high_offset_deci_c <= APP_HEAT_FEEDFORWARD_OFFSET_MAX_DECI_C);
}

static void heat_control_reset_locked(void)
{
	memset(&heat_ctx.diag, 0, sizeof(heat_ctx.diag));
	heat_ctx.diag.output_delay_us = TRIAC_MAX_DELAY_US;
	heat_ctx.first_sample = true;
	heat_ctx.last_error_deci_c = 0;
	heat_ctx.last_pid_update_ms = 0U;
}

static void heat_control_publish_idle_diag_locked(int16_t measured_temp, int16_t target_temp,
						      int16_t error_deci_c,
						      int16_t kettle_temp,
						      int16_t kettle_target,
						      int16_t kettle_error)
{
	heat_control_reset_locked();
	heat_ctx.diag.measured_temp_deci_c = measured_temp;
	heat_ctx.diag.target_temp_deci_c = target_temp;
	heat_ctx.diag.error_deci_c = error_deci_c;
	heat_ctx.diag.kettle_temp_deci_c = kettle_temp;
	heat_ctx.diag.kettle_target_deci_c = kettle_target;
	heat_ctx.diag.kettle_error_deci_c = kettle_error;
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
	heat_ctx.outlet.base_offset_deci_c = APP_HEAT_KETTLE_BASE_OFFSET_DECI_C;
	heat_ctx.outlet.target_margin_deci_c = APP_HEAT_KETTLE_TARGET_MARGIN_DECI_C;
	heat_ctx.outlet.air_low_offset_deci_c = APP_HEAT_AIR_LOW_OFFSET_DECI_C;
	heat_ctx.outlet.air_mid_offset_deci_c = APP_HEAT_AIR_MID_OFFSET_DECI_C;
	heat_ctx.outlet.air_high_offset_deci_c = APP_HEAT_AIR_HIGH_OFFSET_DECI_C;
	heat_ctx.outlet.mist_low_offset_deci_c = APP_HEAT_MIST_LOW_OFFSET_DECI_C;
	heat_ctx.outlet.mist_mid_offset_deci_c = APP_HEAT_MIST_MID_OFFSET_DECI_C;
	heat_ctx.outlet.mist_high_offset_deci_c = APP_HEAT_MIST_HIGH_OFFSET_DECI_C;
	heat_ctx.kettle_target_override.enabled = false;
	heat_ctx.kettle_target_override.target_deci_c =
		APP_HEAT_KETTLE_FIXED_TARGET_DECI_C;
	heat_control_reset_locked();
	return triac_control_init();
}

fault_code_t heat_control_step(const telemetry_status_t *status)
{
	int16_t outlet_temp;
	int16_t kettle_temp;
	int16_t overtemp_protect_temp;
	int16_t outlet_error_deci_c;
	int16_t kettle_target_deci_c;
	int16_t kettle_error_deci_c;
	int16_t kettle_error_delta_deci_c;
	int32_t i_limit_raw;
	int32_t output_raw;
	uint32_t output_permille;
	uint32_t now_ms;
	uint32_t elapsed_ms;
	bool was_limited;
	bool kettle_target_override_enabled;

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

	if (kettle_temp >= APP_KETTLE_OVER_TEMP_FAULT_DECI_C) {
		heat_control_stop();
		return FAULT_KETTLE_OVER_TEMP;
	}

	outlet_error_deci_c = (int16_t)status->config.target_temp_deci_c - outlet_temp;
	kettle_target_deci_c = heat_control_kettle_target_deci_c(&status->config);
	kettle_error_deci_c = kettle_target_deci_c - kettle_temp;
	kettle_target_override_enabled = heat_ctx.kettle_target_override.enabled;

	if (kettle_temp >= APP_KETTLE_HEAT_CUTOFF_DECI_C) {
		k_mutex_lock(&heat_ctx.lock, K_FOREVER);
		was_limited = heat_ctx.kettle_heat_limited;
		heat_ctx.kettle_heat_limited = true;
		heat_control_publish_idle_diag_locked(outlet_temp,
						      (int16_t)status->config.target_temp_deci_c,
						      outlet_error_deci_c,
						      kettle_temp,
						      kettle_target_deci_c,
						      kettle_error_deci_c);
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
	heat_ctx.diag.error_deci_c = outlet_error_deci_c;
	heat_ctx.diag.kettle_temp_deci_c = kettle_temp;
	heat_ctx.diag.kettle_target_deci_c = kettle_target_deci_c;
	heat_ctx.diag.kettle_error_deci_c = kettle_error_deci_c;

	/*
	 * 出雾口已经达到目标时立即关断加热。锅体热惯性很大，继续加热只会把
	 * 后续过冲留给系统慢慢“还债”。这里同时清掉内层积分，避免再次启动时
	 * 带着上一轮的热量记忆。
	 */
	if ((!kettle_target_override_enabled) && (outlet_error_deci_c <= 0)) {
		heat_control_publish_idle_diag_locked(outlet_temp,
						      (int16_t)status->config.target_temp_deci_c,
						      outlet_error_deci_c,
						      kettle_temp,
						      kettle_target_deci_c,
						      kettle_error_deci_c);
		k_mutex_unlock(&heat_ctx.lock);
		(void)triac_control_stop();
		return FAULT_NONE;
	}

	/*
	 * 内层控制：用锅体温度追踪虚拟锅体目标。锅体温度比出雾口温度更接近
	 * 加热器本身，因此这个闭环更快、更稳定；出雾口慢变量只负责决定锅体
	 * 应该大约维持在什么温度。
	 */
	if (APP_HEAT_KETTLE_OVER_TARGET_STOP_ENABLE &&
	    (kettle_error_deci_c <= -APP_HEAT_KETTLE_OVER_TARGET_STOP_DECI_C)) {
		heat_control_publish_idle_diag_locked(outlet_temp,
						      (int16_t)status->config.target_temp_deci_c,
						      outlet_error_deci_c,
						      kettle_temp,
						      kettle_target_deci_c,
						      kettle_error_deci_c);
		k_mutex_unlock(&heat_ctx.lock);
		(void)triac_control_stop();
		return FAULT_NONE;
	}

	if (APP_HEAT_KETTLE_INNER_DEADBAND_ENABLE &&
	    (!kettle_target_override_enabled) &&
	    (kettle_error_deci_c <= APP_HEAT_KETTLE_INNER_DEADBAND_DECI_C)) {
		heat_ctx.diag.p_term_raw = 0;
		heat_ctx.diag.i_term_raw = 0;
		heat_ctx.diag.d_term_raw = 0;
		heat_ctx.diag.output_permille = 0;
		heat_ctx.diag.output_delay_us = TRIAC_MAX_DELAY_US;
		heat_ctx.diag.saturated = false;
		heat_ctx.first_sample = true;
		heat_ctx.last_error_deci_c = 0;
		k_mutex_unlock(&heat_ctx.lock);
		(void)triac_control_stop();
		return FAULT_NONE;
	}

	if (kettle_temp < APP_HEAT_KETTLE_FULL_POWER_BELOW_DECI_C) {
		/*
		 * 冷锅阶段温度反馈很慢，PID 很容易在低温区慢慢“爬坡”或积累无意义积分。
		 * 低于该阈值时直接满功率预热；一旦锅体进入有效控温区，再交回 PID 精细控制。
		 */
		heat_ctx.diag.p_term_raw = 0;
		heat_ctx.diag.i_term_raw = 0;
		heat_ctx.diag.d_term_raw = 0;
		heat_ctx.diag.output_permille = 1000U;
		heat_ctx.diag.output_delay_us = 0U;
		heat_ctx.diag.saturated = true;
		heat_ctx.first_sample = true;
		heat_ctx.last_error_deci_c = kettle_error_deci_c;
		heat_ctx.last_pid_update_ms = 0U;
		k_mutex_unlock(&heat_ctx.lock);
		triac_control_set_output_permille(1000U);
		triac_control_set_enabled(true);
		return FAULT_NONE;
	}

	now_ms = k_uptime_get_32();
	elapsed_ms = (heat_ctx.last_pid_update_ms == 0U) ?
		     APP_HEAT_PID_PERIOD_MS : (now_ms - heat_ctx.last_pid_update_ms);
	if (elapsed_ms < APP_HEAT_PID_PERIOD_MS) {
		/*
		 * 锅体温度 2~3 秒才会有明显变化，热 PID 没必要跟 20ms 控制任务同频运行。
		 * 这里保持上一次输出，只刷新遥测诊断温度，避免积分项在传感器还没反馈时
		 * 快速累加导致过冲。
		 */
		k_mutex_unlock(&heat_ctx.lock);
		return FAULT_NONE;
	}
	heat_ctx.last_pid_update_ms = now_ms;

	heat_ctx.diag.p_term_raw = heat_ctx.pid.kp_milli * kettle_error_deci_c;
	heat_ctx.diag.i_term_raw +=
		(heat_ctx.pid.ki_milli * kettle_error_deci_c * elapsed_ms) / 100;
	i_limit_raw = heat_ctx.pid.integral_limit_permille * 1000;
	if (heat_ctx.diag.i_term_raw > i_limit_raw) {
		heat_ctx.diag.i_term_raw = i_limit_raw;
	}
	if (heat_ctx.diag.i_term_raw < -i_limit_raw) {
		heat_ctx.diag.i_term_raw = -i_limit_raw;
	}

	if (heat_ctx.first_sample) {
		heat_ctx.diag.d_term_raw = 0;
	} else {
		kettle_error_delta_deci_c = kettle_error_deci_c - heat_ctx.last_error_deci_c;
		heat_ctx.diag.d_term_raw = heat_ctx.pid.kd_milli * kettle_error_delta_deci_c;
	}

	heat_ctx.last_error_deci_c = kettle_error_deci_c;
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
	heat_ctx.diag.output_permille = (uint16_t)output_permille;

	if (output_permille == 0U) {
		heat_control_publish_idle_diag_locked(outlet_temp,
						      (int16_t)status->config.target_temp_deci_c,
						      outlet_error_deci_c,
						      kettle_temp,
						      kettle_target_deci_c,
						      kettle_error_deci_c);
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

int heat_control_set_outlet_params(const outlet_control_params_t *params)
{
	if (!heat_control_outlet_params_valid(params)) {
		return -EINVAL;
	}

	k_mutex_lock(&heat_ctx.lock, K_FOREVER);
	heat_ctx.outlet = *params;
	k_mutex_unlock(&heat_ctx.lock);

	LOG_INF("outlet control updated base=%d margin=%d air=%d/%d/%d mist=%d/%d/%d",
		params->base_offset_deci_c,
		params->target_margin_deci_c,
		params->air_low_offset_deci_c,
		params->air_mid_offset_deci_c,
		params->air_high_offset_deci_c,
		params->mist_low_offset_deci_c,
		params->mist_mid_offset_deci_c,
		params->mist_high_offset_deci_c);
	return 0;
}

void heat_control_get_outlet_params(outlet_control_params_t *params)
{
	if (params == NULL) {
		return;
	}

	k_mutex_lock(&heat_ctx.lock, K_FOREVER);
	*params = heat_ctx.outlet;
	k_mutex_unlock(&heat_ctx.lock);
}

int heat_control_set_kettle_target_override(const kettle_target_override_t *override)
{
	if (override == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&heat_ctx.lock, K_FOREVER);
	heat_ctx.kettle_target_override = *override;
	heat_ctx.first_sample = true;
	heat_ctx.last_error_deci_c = 0;
	heat_ctx.diag.i_term_raw = 0;
	k_mutex_unlock(&heat_ctx.lock);

	LOG_INF("kettle target override %s target=%d",
		override->enabled ? "enabled" : "disabled",
		override->target_deci_c);
	return 0;
}

void heat_control_get_kettle_target_override(kettle_target_override_t *override)
{
	if (override == NULL) {
		return;
	}

	k_mutex_lock(&heat_ctx.lock, K_FOREVER);
	*override = heat_ctx.kettle_target_override;
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

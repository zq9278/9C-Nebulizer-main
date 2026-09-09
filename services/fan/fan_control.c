#include "fan_control.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <nebulizer/app_config.h>
#include <drivers_app/pwm/pwm_output.h>
#include <platform/board_devices.h>
#include <platform/board_resources.h>
#include <platform/runtime.h>
#include <platform/log.h>

static const struct board_pwm *fan_pwm;

static struct {
	StaticSemaphore_t lock_storage;
	SemaphoreHandle_t lock;
	pid_params_t pid;
	fan_control_diag_t diag;
	int16_t last_error_deci_c;
	uint32_t last_pid_update_ms;
	bool first_sample;
} fan_ctx;

static uint8_t fan_apply_hw_polarity(uint8_t percent)
{
	if (APP_FAN_PWM_INVERTED) {
		return 100U - percent;
	}

	return percent;
}

int fan_control_init(void)
{
	fan_ctx.lock = xSemaphoreCreateMutexStatic(&fan_ctx.lock_storage);
	configASSERT(fan_ctx.lock != NULL);
	memset(&fan_ctx.diag, 0, sizeof(fan_ctx.diag));
	fan_ctx.pid.kp_milli = APP_FAN_PID_KP_DEFAULT_MILLI;
	fan_ctx.pid.ki_milli = APP_FAN_PID_KI_DEFAULT_MILLI;
	fan_ctx.pid.kd_milli = APP_FAN_PID_KD_DEFAULT_MILLI;
	fan_ctx.pid.integral_limit_permille = APP_FAN_PID_I_LIMIT_DEFAULT_PERMILLE;
	fan_ctx.first_sample = true;

	fan_pwm = board_get_fan1_pwm();
	if (pwm_output_init(fan_pwm) != 0) {
		return -ENODEV;
	}

	return fan_control_stop();
}

static bool fan_pid_valid(const pid_params_t *pid)
{
	return (pid != NULL) &&
	       (pid->kp_milli >= 0) && (pid->kp_milli <= APP_FAN_PID_KP_MAX_MILLI) &&
	       (pid->ki_milli >= 0) && (pid->ki_milli <= APP_FAN_PID_KI_MAX_MILLI) &&
	       (pid->kd_milli >= 0) && (pid->kd_milli <= APP_FAN_PID_KD_MAX_MILLI) &&
	       (pid->integral_limit_permille >= 0) &&
	       (pid->integral_limit_permille <= (int32_t)APP_FAN_PID_I_LIMIT_MAX_PERMILLE);
}

static uint8_t fan_percent_for_level(air_level_t level)
{
	switch (level) {
	case AIR_LEVEL_LOW:
		return FAN_LEVEL_LOW_PERCENT;
	case AIR_LEVEL_MID:
		return FAN_LEVEL_MID_PERCENT;
	case AIR_LEVEL_HIGH:
		return FAN_LEVEL_HIGH_PERCENT;
	case AIR_LEVEL_OFF:
	default:
		return 0U;
	}
}

int fan_control_set_level(air_level_t level)
{
	uint8_t percent = fan_percent_for_level(level);

	xSemaphoreTake(fan_ctx.lock, portMAX_DELAY);
	memset(&fan_ctx.diag, 0, sizeof(fan_ctx.diag));
	fan_ctx.diag.base_percent = percent;
	fan_ctx.diag.output_percent = percent;
	fan_ctx.first_sample = true;
	fan_ctx.last_error_deci_c = 0;
	fan_ctx.last_pid_update_ms = 0U;
	xSemaphoreGive(fan_ctx.lock);

	return pwm_output_set_percent(fan_pwm, fan_apply_hw_polarity(percent));
}

int fan_control_step(const telemetry_status_t *status)
{
	uint8_t base_percent;
	uint8_t output_percent;
	uint16_t boost_permille;
	uint32_t calculated_boost_permille;
	int16_t measured_temp;
	int16_t target_temp;
	int16_t error_deci_c;
	int16_t error_delta_deci_c;
	int32_t output_raw;
	int32_t i_limit_raw;
	int64_t integral_raw;
	uint32_t now_ms;
	uint32_t elapsed_ms;

	if (status == NULL) {
		return -EINVAL;
	}

	base_percent = fan_percent_for_level(status->config.air_level);
	if (base_percent == 0U) {
		return fan_control_stop();
	}

	measured_temp = status->sensors.ntc_deci_c[BOARD_NTC_OUTLET1];
	target_temp = (int16_t)status->config.target_temp_deci_c;
	error_deci_c = measured_temp - target_temp;

	xSemaphoreTake(fan_ctx.lock, portMAX_DELAY);
	fan_ctx.diag.enabled = true;
	fan_ctx.diag.measured_temp_deci_c = measured_temp;
	fan_ctx.diag.target_temp_deci_c = target_temp;
	fan_ctx.diag.error_deci_c = error_deci_c;
	fan_ctx.diag.base_percent = base_percent;

	if (error_deci_c <= 0) {
		fan_ctx.diag.p_term_raw = 0;
		fan_ctx.diag.i_term_raw = 0;
		fan_ctx.diag.d_term_raw = 0;
		fan_ctx.diag.boost_permille = 0U;
		fan_ctx.diag.output_percent = base_percent;
		fan_ctx.diag.saturated = false;
		fan_ctx.first_sample = true;
		fan_ctx.last_error_deci_c = error_deci_c;
		fan_ctx.last_pid_update_ms = 0U;
		output_percent = base_percent;
		xSemaphoreGive(fan_ctx.lock);
		return pwm_output_set_percent(fan_pwm, fan_apply_hw_polarity(output_percent));
	}

	now_ms = runtime_now_ms();
	elapsed_ms = (fan_ctx.last_pid_update_ms == 0U) ?
		     APP_FAN_PID_PERIOD_MS : (now_ms - fan_ctx.last_pid_update_ms);
	if (elapsed_ms >= APP_FAN_PID_PERIOD_MS) {
		fan_ctx.last_pid_update_ms = now_ms;
		fan_ctx.diag.p_term_raw = fan_ctx.pid.kp_milli * error_deci_c;
		integral_raw = (int64_t)fan_ctx.diag.i_term_raw +
			       ((int64_t)fan_ctx.pid.ki_milli * error_deci_c * elapsed_ms) / 100;
		i_limit_raw = fan_ctx.pid.integral_limit_permille * 1000;
		if (integral_raw > i_limit_raw) {
			integral_raw = i_limit_raw;
		}
		if (integral_raw < 0) {
			integral_raw = 0;
		}
		fan_ctx.diag.i_term_raw = (int32_t)integral_raw;

		if (fan_ctx.first_sample) {
			fan_ctx.diag.d_term_raw = 0;
		} else {
			error_delta_deci_c = error_deci_c - fan_ctx.last_error_deci_c;
			fan_ctx.diag.d_term_raw = fan_ctx.pid.kd_milli * error_delta_deci_c;
		}
		fan_ctx.last_error_deci_c = error_deci_c;
		fan_ctx.first_sample = false;

		output_raw = fan_ctx.diag.p_term_raw + fan_ctx.diag.i_term_raw +
			     fan_ctx.diag.d_term_raw;
		if (output_raw < 0) {
			output_raw = 0;
		}
		calculated_boost_permille = (uint32_t)(output_raw / 1000);
		if (calculated_boost_permille > (APP_FAN_PID_MAX_BOOST_PERCENT * 10U)) {
			calculated_boost_permille = APP_FAN_PID_MAX_BOOST_PERCENT * 10U;
		}
		boost_permille = (uint16_t)calculated_boost_permille;
		fan_ctx.diag.boost_permille = boost_permille;
	}

	boost_permille = fan_ctx.diag.boost_permille;
	output_percent = base_percent + (uint8_t)((boost_permille + 5U) / 10U);
	if (output_percent > (base_percent + APP_FAN_PID_MAX_BOOST_PERCENT)) {
		output_percent = base_percent + APP_FAN_PID_MAX_BOOST_PERCENT;
	}
	if (output_percent > 100U) {
		output_percent = 100U;
	}
	fan_ctx.diag.output_percent = output_percent;
	fan_ctx.diag.saturated = boost_permille >= (APP_FAN_PID_MAX_BOOST_PERCENT * 10U);
	xSemaphoreGive(fan_ctx.lock);

	return pwm_output_set_percent(fan_pwm, fan_apply_hw_polarity(output_percent));
}

int fan_control_stop(void)
{
	xSemaphoreTake(fan_ctx.lock, portMAX_DELAY);
	memset(&fan_ctx.diag, 0, sizeof(fan_ctx.diag));
	fan_ctx.first_sample = true;
	fan_ctx.last_error_deci_c = 0;
	fan_ctx.last_pid_update_ms = 0U;
	xSemaphoreGive(fan_ctx.lock);
	return pwm_output_set_percent(fan_pwm, fan_apply_hw_polarity(0U));
}

int fan_control_set_pid(const pid_params_t *pid)
{
	if (!fan_pid_valid(pid)) {
		return -EINVAL;
	}

	xSemaphoreTake(fan_ctx.lock, portMAX_DELAY);
	fan_ctx.pid = *pid;
	fan_ctx.diag.p_term_raw = 0;
	fan_ctx.diag.i_term_raw = 0;
	fan_ctx.diag.d_term_raw = 0;
	fan_ctx.diag.boost_permille = 0U;
	fan_ctx.first_sample = true;
	fan_ctx.last_error_deci_c = 0;
	fan_ctx.last_pid_update_ms = 0U;
	xSemaphoreGive(fan_ctx.lock);
	LOG_INF("fan pid updated kp=%d ki=%d kd=%d i_limit=%d",
		(int)pid->kp_milli, (int)pid->ki_milli, (int)pid->kd_milli, (int)pid->integral_limit_permille);
	return 0;
}

void fan_control_get_pid(pid_params_t *pid)
{
	if (pid == NULL) {
		return;
	}

	xSemaphoreTake(fan_ctx.lock, portMAX_DELAY);
	*pid = fan_ctx.pid;
	xSemaphoreGive(fan_ctx.lock);
}

void fan_control_get_diag(fan_control_diag_t *diag)
{
	if (diag == NULL) {
		return;
	}

	xSemaphoreTake(fan_ctx.lock, portMAX_DELAY);
	*diag = fan_ctx.diag;
	xSemaphoreGive(fan_ctx.lock);
}

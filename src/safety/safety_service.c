#include "safety_service.h"

#include <stdlib.h>

#include <nebulizer/app_config.h>
#include <platform/board_resources.h>
#include <services/fan/fan_control.h>
#include <services/heat/heat_control.h>
#include <services/mist/mist_service.h>
#include <services/sensors/sensor_snapshot.h>
#include <src/app/app_context.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(safety_service, CONFIG_NEBULIZER_LOG_LEVEL);

/* 这些标志仅用于抑制重复日志，避免相同故障在周期轮询中刷屏。 */
static struct {
	uint8_t ntc_open_mask;
	uint8_t ntc_short_mask;
	bool mist_fault_logged;
	bool mist_offline_logged;
	bool kettle_overtemp_logged;
	bool outlet_overtemp_logged;
} safety_log_state;

/*
 * 盖子相关状态单独封装，是因为它需要：
 * - 原始采样值
 * - 防抖后的稳定值
 * - 变化标记
 * - 因盖子打开而触发的暂停锁存
 * - 因缺水而触发的暂停锁存
 */
static struct {
	struct k_mutex lock;
	struct k_work_delayable cover_debounce_work;
	bool cover_raw_closed;
	bool cover_raw_valid;
	bool cover_debounced_closed;
	bool cover_debounced_valid;
	bool cover_change_pending;
	bool cover_pause_latched;
	bool mist_low_water_pause_latched;
} safety_cover_state;

struct cover_state_snapshot {
	bool closed;
	bool changed;
	bool pause_latched;
};

/* 延时防抖回调：把原始值“提交”为稳定值。 */
static void safety_service_cover_debounce_work(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&safety_cover_state.lock, K_FOREVER);
	if (!safety_cover_state.cover_raw_valid) {
		k_mutex_unlock(&safety_cover_state.lock);
		return;
	}

	if ((!safety_cover_state.cover_debounced_valid) ||
	    (safety_cover_state.cover_debounced_closed != safety_cover_state.cover_raw_closed)) {
		safety_cover_state.cover_debounced_closed = safety_cover_state.cover_raw_closed;
		safety_cover_state.cover_debounced_valid = true;
		safety_cover_state.cover_change_pending = true;
	}
	k_mutex_unlock(&safety_cover_state.lock);
}

/* 输入原始盖子状态；如果检测到变化，则启动防抖计时。 */
static void safety_service_cover_sample_raw(bool cover_closed)
{
	k_mutex_lock(&safety_cover_state.lock, K_FOREVER);
	if (!safety_cover_state.cover_raw_valid) {
		safety_cover_state.cover_raw_closed = cover_closed;
		safety_cover_state.cover_raw_valid = true;
		safety_cover_state.cover_debounced_closed = cover_closed;
		safety_cover_state.cover_debounced_valid = true;
		k_mutex_unlock(&safety_cover_state.lock);
		return;
	}

	if (safety_cover_state.cover_raw_closed != cover_closed) {
		safety_cover_state.cover_raw_closed = cover_closed;
		(void)k_work_reschedule(&safety_cover_state.cover_debounce_work,
					 K_MSEC(APP_COVER_DEBOUNCE_MS));
	}
	k_mutex_unlock(&safety_cover_state.lock);
}

/* 读取一次当前防抖后的盖子状态快照，并消费 changed 标志。 */
static struct cover_state_snapshot safety_service_cover_snapshot(void)
{
	struct cover_state_snapshot snapshot;

	k_mutex_lock(&safety_cover_state.lock, K_FOREVER);
	snapshot.closed = safety_cover_state.cover_debounced_closed;
	snapshot.changed = safety_cover_state.cover_change_pending;
	snapshot.pause_latched = safety_cover_state.cover_pause_latched;
	safety_cover_state.cover_change_pending = false;
	k_mutex_unlock(&safety_cover_state.lock);

	return snapshot;
}

/* 设置“因为盖子打开而暂停”的锁存标志。 */
static void safety_service_cover_set_pause_latched(bool active)
{
	k_mutex_lock(&safety_cover_state.lock, K_FOREVER);
	safety_cover_state.cover_pause_latched = active;
	k_mutex_unlock(&safety_cover_state.lock);
}

/* 初始化互斥锁与防抖延时工作项。 */
int safety_service_init(void)
{
	k_mutex_init(&safety_cover_state.lock);
	k_work_init_delayable(&safety_cover_state.cover_debounce_work,
			      safety_service_cover_debounce_work);
	return 0;
}

/*
 * 进入安全态时的统一执行器收口函数。
 *
 * 这一步是“动作层”的安全保护：
 * - 停止加热
 * - 停止雾化
 * - 停止风机
 *
 * 如果 reason 不是 FAULT_NONE，还会顺手把故障码写入上下文。
 */
void safety_service_enter_safe_state(fault_code_t reason)
{
	heat_control_stop();
	mist_service_request_stop();
	fan_control_stop();
	if (reason == FAULT_NONE) {
		return;
	}

	app_context_set_fault(reason);
}

/*
 * 安全轮询主函数。
 *
 * 这里把所有安全检查集中起来，输出的是“安全结果”，而不是直接做状态迁移。
 * 调用者（当前是 ControlSupervisorTask）会把结果转成 APP_EVT_FAULT /
 * APP_EVT_PAUSE / APP_EVT_RESUME，再交给 AppTask 统一处理。
 */
struct safety_result safety_service_poll(void)
{
	struct safety_result result = { 0 };
	telemetry_status_t status;
	uint32_t now_ms = k_uptime_get_32();
	struct cover_state_snapshot cover;

	app_context_get_status(&status);
	safety_service_cover_sample_raw(status.sensors.cover_closed);
	cover = safety_service_cover_snapshot();

	if ((status.state == TREATMENT_STATE_BOOT) ||
	    (status.state == TREATMENT_STATE_DONE)) {
		safety_service_cover_set_pause_latched(false);
		return result;
	}

	if ((status.state == TREATMENT_STATE_READY) ||
	    (status.state == TREATMENT_STATE_FAULT)) {
		safety_service_cover_set_pause_latched(false);
		k_mutex_lock(&safety_cover_state.lock, K_FOREVER);
		safety_cover_state.mist_low_water_pause_latched = false;
		k_mutex_unlock(&safety_cover_state.lock);
	}

	if (!app_context_heartbeat_alive(now_ms) &&
	    ((status.state == TREATMENT_STATE_RUNNING_HOT) ||
	     (status.state == TREATMENT_STATE_RUNNING_COLD))) {
		LOG_WRN("host heartbeat timeout");
		safety_service_enter_safe_state(FAULT_NONE);
		result.pause_requested = true;
		return result;
	}

	/* Stainless pot water level interlock is temporarily disabled during bring-up. */

	if (status.sensors.ntc_deci_c[BOARD_NTC_KETTLE] >= APP_KETTLE_OVER_TEMP_FAULT_DECI_C) {
		if (!safety_log_state.kettle_overtemp_logged) {
			LOG_ERR("kettle over temp: %d.%dC",
				status.sensors.ntc_deci_c[BOARD_NTC_KETTLE] / 10,
				abs(status.sensors.ntc_deci_c[BOARD_NTC_KETTLE] % 10));
			safety_log_state.kettle_overtemp_logged = true;
		}

		if (status.fault == FAULT_KETTLE_OVER_TEMP) {
			safety_service_enter_safe_state(FAULT_KETTLE_OVER_TEMP);
			return result;
		}

		safety_service_enter_safe_state(FAULT_KETTLE_OVER_TEMP);
		result.fault = FAULT_KETTLE_OVER_TEMP;
		return result;
	}

	safety_log_state.kettle_overtemp_logged = false;

	if (status.sensors.ntc_deci_c[BOARD_NTC_OUTLET2] >= APP_OUTLET1_OVER_TEMP_FAULT_DECI_C) {
		if (!safety_log_state.outlet_overtemp_logged) {
			LOG_ERR("outlet over temp: %d.%dC",
				status.sensors.ntc_deci_c[BOARD_NTC_OUTLET2] / 10,
				abs(status.sensors.ntc_deci_c[BOARD_NTC_OUTLET2] % 10));
			safety_log_state.outlet_overtemp_logged = true;
		}

		if (status.fault == FAULT_OVER_TEMP) {
			safety_service_enter_safe_state(FAULT_OVER_TEMP);
			return result;
		}

		safety_service_enter_safe_state(FAULT_OVER_TEMP);
		result.fault = FAULT_OVER_TEMP;
		return result;
	}

	safety_log_state.outlet_overtemp_logged = false;

	if (cover.changed) {
		if (!cover.closed) {
			LOG_WRN("cover open");
			if ((status.state == TREATMENT_STATE_RUNNING_HOT) ||
			    (status.state == TREATMENT_STATE_RUNNING_COLD)) {
				safety_service_cover_set_pause_latched(true);
				safety_service_enter_safe_state(FAULT_NONE);
				result.pause_requested = true;
				return result;
			}
		} else {
			LOG_INF("cover closed");
		}
	}

	if (cover.pause_latched &&
	    cover.closed &&
	    (status.state == TREATMENT_STATE_PAUSED) &&
	    (status.fault == FAULT_NONE) &&
	    !status.mist.low_water &&
	    !status.mist.safety_locked &&
	    status.mist.online) {
		LOG_INF("cover closed, resuming treatment");
		safety_service_cover_set_pause_latched(false);
		result.resume_requested = true;
		return result;
	}

	static const enum board_ntc_id safety_ntcs[] = {
		BOARD_NTC_OUTLET1,
		BOARD_NTC_OUTLET2,
		BOARD_NTC_KETTLE,
	};

	for (size_t idx = 0; idx < ARRAY_SIZE(safety_ntcs); ++idx) {
		size_t i = safety_ntcs[idx];
		uint8_t bit = BIT(i);

		if (status.sensors.ntc_open[i]) {
			if ((safety_log_state.ntc_open_mask & bit) == 0U) {
				LOG_ERR("ntc open: %u", (unsigned int)i);
				safety_log_state.ntc_open_mask |= bit;
			}

			if (status.fault == FAULT_SENSOR_NTC_OPEN) {
				return result;
			}

			safety_service_enter_safe_state(FAULT_SENSOR_NTC_OPEN);
			result.fault = FAULT_SENSOR_NTC_OPEN;
			return result;
		}

		safety_log_state.ntc_open_mask &= ~bit;

		if (status.sensors.ntc_short[i]) {
			if ((safety_log_state.ntc_short_mask & bit) == 0U) {
				LOG_ERR("ntc short: %u", (unsigned int)i);
				safety_log_state.ntc_short_mask |= bit;
			}

			if (status.fault == FAULT_SENSOR_NTC_SHORT) {
				return result;
			}

			safety_service_enter_safe_state(FAULT_SENSOR_NTC_SHORT);
			result.fault = FAULT_SENSOR_NTC_SHORT;
			return result;
		}

		safety_log_state.ntc_short_mask &= ~bit;
	}

	if (status.mist.low_water) {
		k_mutex_lock(&safety_cover_state.lock, K_FOREVER);
		safety_cover_state.mist_low_water_pause_latched = true;
		k_mutex_unlock(&safety_cover_state.lock);

		if ((status.state == TREATMENT_STATE_RUNNING_HOT) ||
		    (status.state == TREATMENT_STATE_RUNNING_COLD)) {
			LOG_WRN("mist low water, pausing treatment");
			safety_service_enter_safe_state(FAULT_NONE);
			result.pause_requested = true;
			return result;
		}
	} else {
		bool mist_pause_latched;

		k_mutex_lock(&safety_cover_state.lock, K_FOREVER);
		mist_pause_latched = safety_cover_state.mist_low_water_pause_latched;
		if (mist_pause_latched &&
		    (status.state == TREATMENT_STATE_PAUSED) &&
		    (status.fault == FAULT_NONE) &&
		    status.mist.online &&
		    status.sensors.cover_closed) {
			safety_cover_state.mist_low_water_pause_latched = false;
			k_mutex_unlock(&safety_cover_state.lock);
			LOG_INF("mist water restored, resuming treatment");
			result.resume_requested = true;
			return result;
		}

		if ((status.state == TREATMENT_STATE_READY) ||
		    (status.state == TREATMENT_STATE_DONE) ||
		    (status.state == TREATMENT_STATE_FAULT)) {
			safety_cover_state.mist_low_water_pause_latched = false;
		}
		k_mutex_unlock(&safety_cover_state.lock);
	}

	if ((status.mist.fault_code != 0U) && !status.mist.low_water) {
		if (!safety_log_state.mist_fault_logged) {
			LOG_ERR("mist board fault: %u", status.mist.fault_code);
			safety_log_state.mist_fault_logged = true;
		}

		if (status.fault == FAULT_MIST_BOARD_FAULT) {
			return result;
		}

		safety_service_enter_safe_state(FAULT_MIST_BOARD_FAULT);
		result.fault = FAULT_MIST_BOARD_FAULT;
		return result;
	}

	safety_log_state.mist_fault_logged = false;

	if (((status.state == TREATMENT_STATE_READY) ||
	     (status.state == TREATMENT_STATE_CONFIGURING) ||
	     (status.state == TREATMENT_STATE_RUNNING_HOT) ||
	     (status.state == TREATMENT_STATE_RUNNING_COLD) ||
	     (status.state == TREATMENT_STATE_PAUSED)) &&
	    !status.mist.online &&
	    (status.mist.consecutive_failures > 0U)) {
		if (!safety_log_state.mist_offline_logged) {
			LOG_ERR("mist board offline");
			safety_log_state.mist_offline_logged = true;
		}

		if (status.fault == FAULT_MIST_BOARD_OFFLINE) {
			return result;
		}

		safety_service_enter_safe_state(FAULT_MIST_BOARD_OFFLINE);
		result.fault = FAULT_MIST_BOARD_OFFLINE;
		return result;
	}

	safety_log_state.mist_offline_logged = false;

	return result;
}

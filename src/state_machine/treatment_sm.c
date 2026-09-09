#include "treatment_sm.h"

#include <nebulizer/app_config.h>
#include <platform/board_ids.h>

/* 判断一个状态是否属于真正的治疗运行态。 */
static bool treatment_sm_running(treatment_state_t state)
{
	return (state == TREATMENT_STATE_PREHEATING) ||
	       (state == TREATMENT_STATE_RUNNING_HOT) ||
	       (state == TREATMENT_STATE_RUNNING_COLD);
}

static treatment_state_t treatment_sm_hot_start_state(const telemetry_status_t *status)
{
	return (status->sensors.ntc_deci_c[BOARD_NTC_OUTLET1] <
		APP_HEAT_FULL_POWER_BELOW_DECI_C) ?
		TREATMENT_STATE_PREHEATING : TREATMENT_STATE_RUNNING_HOT;
}

/*
 * 治疗状态机核心逻辑。
 *
 * 读这个函数时，你可以把它理解成一张代码化的状态转移表：
 * - 哪些事件在什么状态下有效
 * - 发生后应该切到哪个状态
 * - 哪些前置条件必须满足
 */
treatment_state_t treatment_sm_handle_event(treatment_state_t current,
					    const app_event_t *evt,
					    const telemetry_status_t *status,
					    bool *accepted)
{
	*accepted = true;

	switch (evt->type) {
	case APP_EVT_START:
		if ((current == TREATMENT_STATE_READY) || (current == TREATMENT_STATE_CONFIGURING) ||
		    (current == TREATMENT_STATE_DONE) || (current == TREATMENT_STATE_KEEP_WARM)) {
			if (!status->mist.online || (status->fault != FAULT_NONE) ||
			    status->mist.low_water || status->mist.safety_locked ||
			    !status->sensors.cover_closed) {
				*accepted = false;
				return current;
			}

			return (status->config.mode == TREATMENT_MODE_HOT) ?
				treatment_sm_hot_start_state(status) : TREATMENT_STATE_RUNNING_COLD;
		}
		break;

	case APP_EVT_STOP:
		if ((current != TREATMENT_STATE_BOOT) && (current != TREATMENT_STATE_SELF_TEST)) {
			return TREATMENT_STATE_READY;
		}
		break;

	case APP_EVT_PAUSE:
	case APP_EVT_HEARTBEAT_TIMEOUT:
		if (treatment_sm_running(current)) {
			return TREATMENT_STATE_PAUSED;
		}
		break;

	case APP_EVT_RESUME:
		if ((current == TREATMENT_STATE_PAUSED) &&
		    status->mist.online &&
		    (status->fault == FAULT_NONE) &&
		    !status->mist.low_water &&
		    !status->mist.safety_locked &&
		    status->sensors.cover_closed) {
			return (status->config.mode == TREATMENT_MODE_HOT) ?
				treatment_sm_hot_start_state(status) : TREATMENT_STATE_RUNNING_COLD;
		}
		break;

	case APP_EVT_PREHEAT_READY:
		if ((current == TREATMENT_STATE_PREHEATING) &&
		    (status->sensors.ntc_deci_c[BOARD_NTC_OUTLET1] >=
		     APP_HEAT_FULL_POWER_BELOW_DECI_C)) {
			return TREATMENT_STATE_RUNNING_HOT;
		}
		break;

	case APP_EVT_FAULT:
		return TREATMENT_STATE_FAULT;

	case APP_EVT_FAULT_CLEAR:
		if ((current == TREATMENT_STATE_FAULT) || (status->fault != FAULT_NONE)) {
			return TREATMENT_STATE_READY;
		}
		break;

	case APP_EVT_SET_CONFIG:
		if ((current == TREATMENT_STATE_READY) || (current == TREATMENT_STATE_CONFIGURING)) {
			return TREATMENT_STATE_CONFIGURING;
		}

		*accepted = false;
		return current;

	default:
		break;
	}

	*accepted = false;
	return current;
}

#ifndef SRC_STATE_MACHINE_TREATMENT_SM_H_
#define SRC_STATE_MACHINE_TREATMENT_SM_H_

#include <stdbool.h>

#include <nebulizer/app_events.h>
#include <nebulizer/app_types.h>

/*
 * 纯状态机接口。
 *
 * 输入：
 * - current: 当前状态
 * - evt: 本次事件
 * - status: 当前系统快照
 *
 * 输出：
 * - 返回下一个状态
 * - accepted 告诉调用者这次事件是否被接受
 *
 * 这个函数只做“状态判定”，不直接操作执行器。
 */
treatment_state_t treatment_sm_handle_event(treatment_state_t current,
					    const app_event_t *evt,
					    const telemetry_status_t *status,
					    bool *accepted);

#endif /* SRC_STATE_MACHINE_TREATMENT_SM_H_ */

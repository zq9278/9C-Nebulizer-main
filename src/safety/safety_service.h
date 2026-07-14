#ifndef SRC_SAFETY_SAFETY_SERVICE_H_
#define SRC_SAFETY_SAFETY_SERVICE_H_

#include <stdbool.h>

#include <nebulizer/fault_codes.h>

/* safety_service_poll() 的返回结果。 */
struct safety_result {
	bool pause_requested;
	bool resume_requested;
	fault_code_t fault;
};

/* 初始化安全服务内部的锁和防抖 work。 */
int safety_service_init(void);

/* 周期轮询安全条件，并返回本轮应触发的安全动作。 */
struct safety_result safety_service_poll(void);

/* 一旦决定进入安全态，统一关闭执行器并在必要时记录故障码。 */
void safety_service_enter_safe_state(fault_code_t reason);

#endif /* SRC_SAFETY_SAFETY_SERVICE_H_ */

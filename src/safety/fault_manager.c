#include "fault_manager.h"

#include <src/app/app_context.h>

/* 报告一个新故障；FAULT_NONE 不会被写入，避免误覆盖真实故障。 */
void fault_manager_raise(fault_code_t fault)
{
	if (fault != FAULT_NONE) {
		app_context_set_fault(fault);
	}
}

/* 读取当前故障。 */
fault_code_t fault_manager_current(void)
{
	return app_context_get_fault();
}

/* 清除故障，真正的约束逻辑由 app_context 决定。 */
bool fault_manager_clear(void)
{
	return app_context_clear_fault();
}

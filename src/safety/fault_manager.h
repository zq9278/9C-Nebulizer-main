#ifndef SRC_SAFETY_FAULT_MANAGER_H_
#define SRC_SAFETY_FAULT_MANAGER_H_

#include <stdbool.h>

#include <nebulizer/fault_codes.h>

/*
 * fault_manager 是对 app_context 中 fault 字段的轻量封装。
 *
 * 单独保留这个模块的好处是：
 * - 将来如果故障策略复杂化，不需要所有调用者都直接碰 app_context
 * - 代码语义更清楚：这里处理的是“故障管理”，不是普通状态读写
 */
void fault_manager_raise(fault_code_t fault);
fault_code_t fault_manager_current(void);
bool fault_manager_clear(void);

#endif /* SRC_SAFETY_FAULT_MANAGER_H_ */

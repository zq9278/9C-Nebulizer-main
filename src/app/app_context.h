#ifndef SRC_APP_APP_CONTEXT_H_
#define SRC_APP_APP_CONTEXT_H_

#include <stdbool.h>

#include <nebulizer/app_types.h>

/*
 * app_context 是整个应用的“全局状态仓库”。
 *
 * 设计思路：
 * - 各个线程不直接相互持有对方的内部状态
 * - 而是通过 app_context 交换“当前系统快照”
 * - 这样可以显著降低线程之间的耦合
 *
 * 这个模块适合你把它理解成一个带锁保护的共享内存对象。
 */
int app_context_init(void);

/* 读取完整系统快照。调用者会得到一份拷贝，而不是内部状态指针。 */
void app_context_get_status(telemetry_status_t *status);

/* 单独读取/写入治疗配置，供命令处理与配置恢复使用。 */
void app_context_get_config(treatment_config_t *config);
void app_context_set_config(const treatment_config_t *config);

/* 读写当前治疗状态。状态迁移逻辑本身不在这里，而在状态机和 AppTask 中。 */
void app_context_set_state(treatment_state_t state);
treatment_state_t app_context_get_state(void);

/* 周期采样、雾化板状态、热控诊断等运行数据的写入口。 */
void app_context_set_sensor_snapshot(const sensor_snapshot_t *snapshot);
void app_context_set_mist_status(const mist_board_status_t *status);
void app_context_set_heat_pid(const pid_params_t *pid);
void app_context_set_heat_diag(const heat_control_diag_t *diag);
void app_context_set_maintenance(const maintenance_control_t *maintenance);

/* 故障状态接口。fault_manager 只是薄封装，最终也会落到这里。 */
void app_context_set_fault(fault_code_t fault);
fault_code_t app_context_get_fault(void);
bool app_context_clear_fault(void);

/* 剩余治疗时间接口，由控制线程递减、由 AppTask/配置逻辑初始化。 */
void app_context_set_remaining_sec(uint32_t remaining_sec);
uint32_t app_context_get_remaining_sec(void);

/* 上位机心跳接口，用于超时暂停保护。 */
void app_context_heartbeat_touch(void);
bool app_context_heartbeat_alive(uint32_t now_ms);

#endif /* SRC_APP_APP_CONTEXT_H_ */

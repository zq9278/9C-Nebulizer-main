#include "app_context.h"

#include <string.h>

#include <nebulizer/app_config.h>
#include <services/storage/settings_defaults.h>
#include <zephyr/kernel.h>

/*
 * app_context_store 是共享状态的真实存储体。
 *
 * lock:
 *   保护所有状态字段，避免多个线程同时读写时产生竞态。
 * status:
 *   整个系统对外可见的统一快照。
 * heartbeat_deadline_ms:
 *   上位机心跳截止时间，用于超时保护。
 */
struct app_context_store {
	struct k_mutex lock;
	telemetry_status_t status;
	uint32_t heartbeat_deadline_ms;
};

static struct app_context_store app_ctx;

/* 初始化共享状态仓库，并写入默认治疗配置与初始状态。 */
int app_context_init(void)
{
	k_mutex_init(&app_ctx.lock);
	memset(&app_ctx.status, 0, sizeof(app_ctx.status));
	settings_defaults_get(&app_ctx.status.config);
	app_ctx.status.state = TREATMENT_STATE_BOOT;
	app_ctx.status.remaining_sec = app_ctx.status.config.duration_sec;
	app_ctx.status.heartbeat_ok = true;
	app_ctx.heartbeat_deadline_ms = k_uptime_get_32() + APP_HOST_HEARTBEAT_TIMEOUT_MS;
	return 0;
}

/* 读取完整快照。这里采用“整结构体拷贝”的方式，换取调用侧的简单性。 */
void app_context_get_status(telemetry_status_t *status)
{
	k_mutex_lock(&app_ctx.lock, K_FOREVER);
	*status = app_ctx.status;
	k_mutex_unlock(&app_ctx.lock);
}

/* 读取当前治疗配置。 */
void app_context_get_config(treatment_config_t *config)
{
	k_mutex_lock(&app_ctx.lock, K_FOREVER);
	*config = app_ctx.status.config;
	k_mutex_unlock(&app_ctx.lock);
}

/* 写入新的治疗配置。 */
void app_context_set_config(const treatment_config_t *config)
{
	k_mutex_lock(&app_ctx.lock, K_FOREVER);
	app_ctx.status.config = *config;
	k_mutex_unlock(&app_ctx.lock);
}

/* 设置当前治疗状态。状态机只决定“是什么状态”，真正状态值落在这里。 */
void app_context_set_state(treatment_state_t state)
{
	k_mutex_lock(&app_ctx.lock, K_FOREVER);
	app_ctx.status.state = state;
	k_mutex_unlock(&app_ctx.lock);
}

/* 读取当前治疗状态。 */
treatment_state_t app_context_get_state(void)
{
	treatment_state_t state;

	k_mutex_lock(&app_ctx.lock, K_FOREVER);
	state = app_ctx.status.state;
	k_mutex_unlock(&app_ctx.lock);

	return state;
}

/* 写入最新的传感器采样快照。 */
void app_context_set_sensor_snapshot(const sensor_snapshot_t *snapshot)
{
	k_mutex_lock(&app_ctx.lock, K_FOREVER);
	app_ctx.status.sensors = *snapshot;
	k_mutex_unlock(&app_ctx.lock);
}

/* 写入最新的雾化板状态快照。 */
void app_context_set_mist_status(const mist_board_status_t *status)
{
	k_mutex_lock(&app_ctx.lock, K_FOREVER);
	app_ctx.status.mist = *status;
	k_mutex_unlock(&app_ctx.lock);
}

/* 写入热控 PID 参数快照，便于上位机查询当前控制参数。 */
void app_context_set_heat_pid(const pid_params_t *pid)
{
	k_mutex_lock(&app_ctx.lock, K_FOREVER);
	app_ctx.status.heat_pid = *pid;
	k_mutex_unlock(&app_ctx.lock);
}

/* 写入热控诊断信息，例如当前误差、输出占空比等。 */
void app_context_set_heat_diag(const heat_control_diag_t *diag)
{
	k_mutex_lock(&app_ctx.lock, K_FOREVER);
	app_ctx.status.heat_diag = *diag;
	k_mutex_unlock(&app_ctx.lock);
}

/* 写入维护模式状态。 */
void app_context_set_maintenance(const maintenance_control_t *maintenance)
{
	k_mutex_lock(&app_ctx.lock, K_FOREVER);
	app_ctx.status.maintenance = *maintenance;
	k_mutex_unlock(&app_ctx.lock);
}

/* 写入当前故障码。 */
void app_context_set_fault(fault_code_t fault)
{
	k_mutex_lock(&app_ctx.lock, K_FOREVER);
	app_ctx.status.fault = fault;
	k_mutex_unlock(&app_ctx.lock);
}

/* 读取当前故障码。 */
fault_code_t app_context_get_fault(void)
{
	fault_code_t fault;

	k_mutex_lock(&app_ctx.lock, K_FOREVER);
	fault = app_ctx.status.fault;
	k_mutex_unlock(&app_ctx.lock);

	return fault;
}

/* 仅在当前已处于 FAULT 状态时清除故障码，避免误清。 */
bool app_context_clear_fault(void)
{
	bool cleared;

	k_mutex_lock(&app_ctx.lock, K_FOREVER);
	cleared = (app_ctx.status.state == TREATMENT_STATE_FAULT);
	if (cleared) {
		app_ctx.status.fault = FAULT_NONE;
	}
	k_mutex_unlock(&app_ctx.lock);

	return cleared;
}

/* 写入剩余治疗时间（秒）。 */
void app_context_set_remaining_sec(uint32_t remaining_sec)
{
	k_mutex_lock(&app_ctx.lock, K_FOREVER);
	app_ctx.status.remaining_sec = remaining_sec;
	k_mutex_unlock(&app_ctx.lock);
}

/* 读取剩余治疗时间（秒）。 */
uint32_t app_context_get_remaining_sec(void)
{
	uint32_t remaining;

	k_mutex_lock(&app_ctx.lock, K_FOREVER);
	remaining = app_ctx.status.remaining_sec;
	k_mutex_unlock(&app_ctx.lock);

	return remaining;
}

/* 上位机心跳到达时刷新超时时间，并标记心跳正常。 */
void app_context_heartbeat_touch(void)
{
	k_mutex_lock(&app_ctx.lock, K_FOREVER);
	app_ctx.heartbeat_deadline_ms = k_uptime_get_32() + APP_HOST_HEARTBEAT_TIMEOUT_MS;
	app_ctx.status.heartbeat_ok = true;
	k_mutex_unlock(&app_ctx.lock);
}

/*
 * 判断心跳是否仍在有效期内。
 *
 * 注意这里不仅返回 alive，还会同步更新 status.heartbeat_ok，
 * 这样上位机查询状态时也能看到“心跳是否正常”。
 */
bool app_context_heartbeat_alive(uint32_t now_ms)
{
	bool alive;

	k_mutex_lock(&app_ctx.lock, K_FOREVER);
	alive = (int32_t)(app_ctx.heartbeat_deadline_ms - now_ms) > 0;
	app_ctx.status.heartbeat_ok = alive;
	k_mutex_unlock(&app_ctx.lock);

	return alive;
}

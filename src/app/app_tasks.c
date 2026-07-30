#include "app_tasks.h"

#include <string.h>

#include <nebulizer/app_config.h>
#include <nebulizer/protocol_ids.h>
#include <services/communication/host_comm_rx.h>
#include <services/communication/host_comm_service.h>
#include <services/communication/host_comm_tx.h>
#include <services/fan/fan_control.h>
#include <services/heat/heat_control.h>
#include <services/mist/mist_service.h>
#include <services/sensors/sensor_service.h>
#include <services/storage/settings_store.h>
#include <src/app/app_context.h>
#include <src/app/app_events.h>
#include <src/safety/fault_manager.h>
#include <src/safety/safety_service.h>
#include <src/state_machine/treatment_sm.h>
#include <src/telemetry/telemetry_service.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_tasks, CONFIG_NEBULIZER_LOG_LEVEL);

#define HOST_ERR_STATE_REJECTED 0x40U
#define HOST_ERR_OTP_RESET_FAILED 0x41U
#define HOST_ERR_COVER_OPEN 0x42U
#define HOST_ERR_MIST_LOW_WATER 0x43U
#define HOST_ERR_MIST_OFFLINE 0x44U
#define HOST_ERR_MIST_SAFETY_LOCKED 0x45U

/*
 * 线程栈统一在这里静态定义，便于集中观察 RTOS 资源占用。
 *
 * Zephyr 常见做法是：
 * - 用 K_THREAD_STACK_DEFINE 预留栈空间
 * - 用 struct k_thread 保存线程控制块
 * - 在 app_tasks_start() 里再真正创建线程
 */
K_THREAD_STACK_DEFINE(app_task_stack, APP_TASK_STACK_SIZE);
K_THREAD_STACK_DEFINE(host_rx_stack, APP_COMM_STACK_SIZE);
K_THREAD_STACK_DEFINE(mist_stack, APP_COMM_STACK_SIZE);
K_THREAD_STACK_DEFINE(supervisor_stack, APP_CONTROL_STACK_SIZE);

static struct k_thread app_task_thread;
static struct k_thread host_rx_thread;
static struct k_thread mist_thread;
static struct k_thread supervisor_thread;
static maintenance_control_t maintenance_ctx;

static void app_maintenance_apply_outputs(void);
static void app_maintenance_stop_outputs(void);
static void app_publish_status_snapshot(void);
static void app_submit_simple_event(app_event_type_t type);
static void app_run_sensor_phase(void);
static void app_run_control_phase(void);
static void app_run_safety_phase(void);
static void app_run_telemetry_phase(void);

/*
 * 按小端格式解析 16 位整数。
 *
 * 通信协议里上位机发来的数据是字节流，业务层不能直接强转结构体，
 * 否则会受到对齐、端序、编译器布局等问题影响。这里显式按协议定义解析，
 * 既安全，也更容易定位通信问题。
 */
static bool app_parse_le16(const uint8_t *data, uint16_t len, uint16_t *value)
{
	if (len < 2U) {
		return false;
	}

	*value = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
	return true;
}

/* 与 app_parse_le16() 相同，只是这里解析的是 32 位有符号整数。 */
static bool app_parse_le32(const uint8_t *data, uint16_t len, int32_t *value)
{
	if (len < 4U) {
		return false;
	}

	*value = (int32_t)((uint32_t)data[0] |
			   ((uint32_t)data[1] << 8) |
			   ((uint32_t)data[2] << 16) |
			   ((uint32_t)data[3] << 24));
	return true;
}

/*
 * 解析一组 PID 参数。
 *
 * 上位机一次下发 4 个 int32：
 * - kp_milli
 * - ki_milli
 * - kd_milli
 * - integral_limit_permille
 *
 * 这里不直接假定负载一定完整，而是逐字段解析并做长度保护，
 * 这样协议版本变化或异常帧都不会把内存读穿。
 */
static bool app_parse_pid_params(const uint8_t *data, uint16_t len, pid_params_t *pid)
{
	if ((pid == NULL) || (len < 16U)) {
		return false;
	}

	return app_parse_le32(&data[0], len, &pid->kp_milli) &&
	       app_parse_le32(&data[4], len - 4U, &pid->ki_milli) &&
	       app_parse_le32(&data[8], len - 8U, &pid->kd_milli) &&
	       app_parse_le32(&data[12], len - 12U, &pid->integral_limit_permille);
}

/*
 * 统一执行状态迁移后的副作用。
 *
 * 状态机本身只负责回答“下一个状态应该是什么”，这里负责把这个状态真正落地：
 * - 更新 app_context 中的状态值
 * - 根据目标状态停机、清维护输出或进入安全态
 * - 发布最新状态快照
 *
 * 这样可以把“状态判定”和“状态落地动作”清晰拆开。
 */
static void app_apply_state_transition(treatment_state_t state)
{
	app_context_set_state(state);

	switch (state) {
	case TREATMENT_STATE_READY:
		maintenance_ctx.active = false;
		app_maintenance_stop_outputs();
		heat_control_stop();
		mist_service_request_stop();
		fan_control_stop();
		break;

	case TREATMENT_STATE_PAUSED:
		heat_control_stop();
		mist_service_request_stop();
		fan_control_stop();
		break;

	case TREATMENT_STATE_FAULT:
		maintenance_ctx.active = false;
		app_maintenance_stop_outputs();
		safety_service_enter_safe_state(app_context_get_fault());
		break;

	case TREATMENT_STATE_DONE:
		maintenance_ctx.active = false;
		app_maintenance_stop_outputs();
		safety_service_enter_safe_state(FAULT_NONE);
		break;

	default:
		break;
	}

	app_publish_status_snapshot();
}

/*
 * 统一发布运行状态快照。
 *
 * 当前系统对外的可见状态主要通过 host_comm_service 发给上位机。
 * 这里把“状态”和“运行态快照”一起发，确保状态迁移后上位机能尽快看到
 * 最新的治疗状态、剩余时间、传感器和雾化板摘要。
 */
static void app_publish_status_snapshot(void)
{
	telemetry_status_t status;

	app_context_get_status(&status);
	(void)host_comm_service_send_status(&status);
	(void)host_comm_service_send_runtime(&status);
}

/*
 * 维护模式下直接下发人工控制输出。
 *
 * 维护模式绕过正常治疗状态机，由维修或调试命令直接驱动风机、雾化和加热。
 * 为了避免维护逻辑散落在多个线程里，这里统一写入上下文并推动三个执行器。
 */
static void app_maintenance_apply_outputs(void)
{
	app_context_set_maintenance(&maintenance_ctx);
	fan_control_set_level(maintenance_ctx.fan_level);
	mist_service_set_desired(maintenance_ctx.mist_level != MIST_LEVEL_UI_OFF,
				 maintenance_ctx.mist_level);
	if (maintenance_ctx.heat_output_permille > 0U) {
		(void)heat_control_manual_set(maintenance_ctx.heat_output_permille);
	} else {
		(void)heat_control_manual_stop();
	}
}

/*
 * 退出维护模式或进入故障/就绪态时，统一关闭人工输出。
 *
 * 这里不仅停止三个执行器，也会把 maintenance_ctx 清零并回写 app_context，
 * 这样上位机和其他线程看到的维护状态始终一致。
 */
static void app_maintenance_stop_outputs(void)
{
	maintenance_ctx.fan_level = AIR_LEVEL_OFF;
	maintenance_ctx.mist_level = MIST_LEVEL_UI_OFF;
	maintenance_ctx.heat_output_permille = 0U;
	app_context_set_maintenance(&maintenance_ctx);
	fan_control_stop();
	mist_service_request_stop();
	heat_control_manual_stop();
}

/*
 * 提交一个不带负载的简单事件。
 *
 * ControlSupervisorTask 会把周期检测得到的“事实”转换成事件，
 * 例如治疗结束、暂停、恢复、故障等。状态迁移仍然只在 AppTask 中完成，
 * 这样状态机入口始终保持单一。
 */
static void app_submit_simple_event(app_event_type_t type)
{
	app_event_t evt = {
		.type = type,
	};

	if (app_event_submit(&evt) != 0) {
		LOG_WRN("event queue full, drop type=%d", (int)type);
	}
}

/*
 * 处理上位机业务命令。
 *
 * 这里是整个应用的“命令裁决中心”：
 * 1. 参数合法性校验
 * 2. 维护模式约束
 * 3. 运行前置条件检查（盖子、缺水、雾化板在线等）
 * 4. 是否需要进入状态机
 * 5. 配置写回和延迟保存
 *
 * 规则上尽量保持：
 * - 简单配置命令：直接生效并应答
 * - 影响状态的命令：交给状态机决定是否接受
 */
static void app_handle_host_command(const host_cmd_event_t *cmd)
{
	telemetry_status_t status;
	treatment_config_t config;
	pid_params_t pid;
	app_event_t sm_evt = { 0 };
	bool accepted = false;
	treatment_state_t next_state;
	uint16_t value16;
	uint8_t error = 0U;

	app_context_get_status(&status);
	config = status.config;

	switch (cmd->command_id) {
	case HOST_CMD_SET_MODE:
		if ((cmd->data_len < 1U) || (cmd->data[0] > TREATMENT_MODE_COLD)) {
			error = 1U;
			break;
		}

		config.mode = (treatment_mode_t)cmd->data[0];
		app_context_set_config(&config);
		app_context_set_remaining_sec(config.duration_sec);
		(void)settings_store_save_delayed(&config);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_SET_TARGET_TEMP:
		if (!app_parse_le16(cmd->data, cmd->data_len, &value16) ||
		    (value16 > APP_MAX_TARGET_TEMP_DECI_C)) {
			error = 2U;
			break;
		}

		config.target_temp_deci_c = value16;
		app_context_set_config(&config);
		(void)settings_store_save_delayed(&config);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_SET_TIME:
		if (!app_parse_le16(cmd->data, cmd->data_len, &value16) ||
		    (value16 < (APP_MIN_TREATMENT_TIME_SEC / 60U)) ||
		    (value16 > (APP_MAX_TREATMENT_TIME_SEC / 60U))) {
			error = 3U;
			break;
		}

		config.duration_sec = (uint16_t)(value16 * 60U);
		app_context_set_config(&config);
		app_context_set_remaining_sec(config.duration_sec);
		(void)settings_store_save_delayed(&config);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_SET_AIR_LEVEL:
		if ((cmd->data_len < 1U) || (cmd->data[0] > AIR_LEVEL_HIGH)) {
			error = 4U;
			break;
		}

		config.air_level = (air_level_t)cmd->data[0];
		app_context_set_config(&config);
		(void)settings_store_save_delayed(&config);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_SET_MIST_LEVEL:
		if ((cmd->data_len < 1U) || (cmd->data[0] > MIST_LEVEL_UI_HIGH)) {
			error = 5U;
			break;
		}

		config.mist_level = (mist_level_t)cmd->data[0];
		app_context_set_config(&config);
		(void)settings_store_save_delayed(&config);
		mist_service_set_desired(status.state == TREATMENT_STATE_RUNNING_HOT ||
					 status.state == TREATMENT_STATE_RUNNING_COLD,
					 config.mist_level);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_START:
		if (status.maintenance.active) {
			error = 11U;
			break;
		}
		if (!status.sensors.cover_closed) {
			error = HOST_ERR_COVER_OPEN;
			break;
		}
		if (!status.mist.online) {
			error = HOST_ERR_MIST_OFFLINE;
			break;
		}
		if (status.mist.low_water) {
			error = HOST_ERR_MIST_LOW_WATER;
			break;
		}
		if (status.mist.safety_locked) {
			error = HOST_ERR_MIST_SAFETY_LOCKED;
			break;
		}
		if (mist_service_trigger_otp_reset() != 0) {
			LOG_ERR("otp reset sequence failed before HOST_CMD_START");
			error = HOST_ERR_OTP_RESET_FAILED;
			break;
		}
		sm_evt.type = APP_EVT_START;
		break;

	case HOST_CMD_STOP:
		sm_evt.type = APP_EVT_STOP;
		break;

	case HOST_CMD_PAUSE:
		sm_evt.type = APP_EVT_PAUSE;
		break;

	case HOST_CMD_RESUME:
		if (status.maintenance.active) {
			error = 11U;
			break;
		}
		sm_evt.type = APP_EVT_RESUME;
		break;

	case HOST_CMD_GET_STATUS:
		host_comm_service_send_status(&status);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_GET_CONFIG:
		host_comm_service_send_config(&config);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_GET_PID:
		heat_control_get_pid(&pid);
		host_comm_service_send_pid(&pid);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_SET_PID:
		if (!app_parse_pid_params(cmd->data, cmd->data_len, &pid) ||
		    (heat_control_set_pid(&pid) != 0)) {
			error = 6U;
			break;
		}

		(void)settings_store_save_pid_delayed(&pid);
		app_context_set_heat_pid(&pid);
		host_comm_service_send_pid(&pid);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_GET_PREHEAT_PID:
		heat_control_get_preheat_pid(&pid);
		host_comm_service_send_preheat_pid(&pid);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_SET_PREHEAT_PID:
		if (!app_parse_pid_params(cmd->data, cmd->data_len, &pid) ||
		    (heat_control_set_preheat_pid(&pid) != 0)) {
			error = 13U;
			break;
		}

		(void)settings_store_save_preheat_pid_delayed(&pid);
		app_context_set_preheat_pid(&pid);
		host_comm_service_send_preheat_pid(&pid);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_GET_FAN_PID:
		fan_control_get_pid(&pid);
		host_comm_service_send_fan_pid(&pid);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_SET_FAN_PID:
		if (!app_parse_pid_params(cmd->data, cmd->data_len, &pid) ||
		    (fan_control_set_pid(&pid) != 0)) {
			error = 12U;
			break;
		}

		(void)settings_store_save_fan_pid_delayed(&pid);
		app_context_set_fan_pid(&pid);
		host_comm_service_send_fan_pid(&pid);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_GET_RUNTIME:
		host_comm_service_send_runtime(&status);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_ENTER_MAINTENANCE:
		if ((status.state != TREATMENT_STATE_READY) &&
		    (status.state != TREATMENT_STATE_PAUSED) &&
		    (status.state != TREATMENT_STATE_DONE)) {
			error = 7U;
			break;
		}
		if (status.fault != FAULT_NONE) {
			error = 7U;
			break;
		}

		maintenance_ctx.active = true;
		app_maintenance_apply_outputs();
		host_comm_service_send_maintenance(&maintenance_ctx);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_EXIT_MAINTENANCE:
		maintenance_ctx.active = false;
		app_maintenance_stop_outputs();
		host_comm_service_send_maintenance(&maintenance_ctx);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_MANUAL_SET_FAN:
		if ((!maintenance_ctx.active) || (cmd->data_len < 1U) ||
		    (cmd->data[0] > AIR_LEVEL_HIGH)) {
			error = 8U;
			break;
		}
		maintenance_ctx.fan_level = (air_level_t)cmd->data[0];
		app_maintenance_apply_outputs();
		host_comm_service_send_maintenance(&maintenance_ctx);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_MANUAL_SET_MIST:
		if ((!maintenance_ctx.active) || (cmd->data_len < 1U) ||
		    (cmd->data[0] > MIST_LEVEL_UI_HIGH)) {
			error = 9U;
			break;
		}
		maintenance_ctx.mist_level = (mist_level_t)cmd->data[0];
		app_maintenance_apply_outputs();
		host_comm_service_send_maintenance(&maintenance_ctx);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_MANUAL_SET_HEAT:
		if ((!maintenance_ctx.active) || !app_parse_le16(cmd->data, cmd->data_len, &value16) ||
		    (value16 > 1000U)) {
			error = 10U;
			break;
		}
		maintenance_ctx.heat_output_permille = value16;
		app_maintenance_apply_outputs();
		host_comm_service_send_maintenance(&maintenance_ctx);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_GET_MAINTENANCE:
		host_comm_service_send_maintenance(&maintenance_ctx);
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return;

	case HOST_CMD_CLEAR_FAULT:
		sm_evt.type = APP_EVT_FAULT_CLEAR;
		break;

	default:
		error = 0x7FU;
		break;
	}

	if (error != 0U) {
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, false, error);
		return;
	}

	app_context_get_status(&status);
	next_state = treatment_sm_handle_event(status.state, &sm_evt, &status, &accepted);
	if (!accepted) {
		host_comm_service_send_ack(cmd->frame_id, cmd->command_id, false, HOST_ERR_STATE_REJECTED);
		return;
	}

	if (sm_evt.type == APP_EVT_FAULT_CLEAR) {
		(void)fault_manager_clear();
	}

	if (sm_evt.type == APP_EVT_START) {
		app_context_set_remaining_sec(status.config.duration_sec);
	}

	app_apply_state_transition(next_state);
	host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
}

/*
 * AppTask 是唯一的状态机入口线程。
 *
 * 它不做周期采样和闭环控制，而是专注做三件事：
 * 1. 消费系统事件
 * 2. 调用状态机判定是否允许状态迁移
 * 3. 触发状态迁移后的收尾动作和状态上报
 *
 * 这种拆法的目的，是把“决策”和“执行”分开，避免多个线程同时改状态。
 */
static void app_task_entry(void *a, void *b, void *c)
{
	app_event_t evt;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	app_context_set_state(TREATMENT_STATE_SELF_TEST);
	k_msleep(100);
	app_context_set_state(TREATMENT_STATE_READY);

	while (true) {
		if (app_event_wait(&evt, K_FOREVER) != 0) {
			continue;
		}

		switch (evt.type) {
		case APP_EVT_HOST_CMD:
			app_handle_host_command(&evt.data.host_cmd);
			break;

		case APP_EVT_FAULT:
			fault_manager_raise(evt.data.fault);
			app_apply_state_transition(TREATMENT_STATE_FAULT);
			break;

		case APP_EVT_MIST_STATUS:
			app_publish_status_snapshot();
			break;

		case APP_EVT_TREATMENT_DONE:
			app_apply_state_transition(TREATMENT_STATE_DONE);
			break;

		case APP_EVT_HEARTBEAT_TIMEOUT:
		case APP_EVT_PAUSE:
		case APP_EVT_RESUME: {
			telemetry_status_t status;
			bool accepted;
			treatment_state_t next;

			app_context_get_status(&status);
			next = treatment_sm_handle_event(status.state, &evt, &status, &accepted);
			if (accepted) {
				app_apply_state_transition(next);
			}
			break;
		}

		default:
			break;
		}
	}
}

/*
 * 采样阶段：只负责从硬件服务中读取现场输入，并写回全局上下文。
 *
 * 这里不做控制决策，目的就是把“读输入”和“算输出”拆成两个阶段，
 * 让控制逻辑总是基于一份相对完整、刚更新过的输入快照运行。
 */
static void app_run_sensor_phase(void)
{
	sensor_snapshot_t snapshot;

	if (sensor_service_sample(&snapshot) == 0) {
		app_context_set_sensor_snapshot(&snapshot);
	}
}

/*
 * 控制阶段：根据当前状态驱动风机、雾化和加热。
 *
 * 这是系统闭环控制最核心的一段逻辑：
 * - 运行热疗模式时执行 PID 与 TRIAC 输出
 * - 运行冷疗模式时停止加热但保持风机/雾化
 * - 非运行态时关闭或释放执行器
 * - 倒计时结束时不直接改状态，而是投递 APP_EVT_TREATMENT_DONE 给 AppTask
 *
 * 这样做的好处是：周期任务只负责“发现治疗结束”，状态迁移仍由 AppTask 收口。
 */
static void app_run_control_phase(void)
{
	static uint32_t last_tick_ms;
	static uint32_t subsec_ms;
	telemetry_status_t status;
	fault_code_t heat_fault;
	heat_control_diag_t heat_diag;
	fan_control_diag_t fan_diag;
	uint32_t now_ms = k_uptime_get_32();
	uint32_t delta_ms = (last_tick_ms == 0U) ? APP_CONTROL_PERIOD_MS : (now_ms - last_tick_ms);

	last_tick_ms = now_ms;
	app_context_get_status(&status);

	if ((status.fault != FAULT_NONE) || (status.state == TREATMENT_STATE_FAULT)) {
		app_maintenance_stop_outputs();
		heat_control_get_diag(&heat_diag);
		app_context_set_heat_diag(&heat_diag);
		fan_control_get_diag(&fan_diag);
		app_context_set_fan_diag(&fan_diag);
		return;
	}

	if (status.maintenance.active) {
		app_maintenance_apply_outputs();
		heat_control_get_diag(&heat_diag);
		app_context_set_heat_diag(&heat_diag);
		fan_control_get_diag(&fan_diag);
		app_context_set_fan_diag(&fan_diag);
		return;
	}

	switch (status.state) {
	case TREATMENT_STATE_RUNNING_HOT:
		fan_control_step(&status);
		fan_control_get_diag(&fan_diag);
		app_context_set_fan_diag(&fan_diag);
		mist_service_set_desired(true, status.config.mist_level);
		heat_fault = heat_control_step(&status);
		heat_control_get_diag(&heat_diag);
		app_context_set_heat_diag(&heat_diag);
		if (heat_fault != FAULT_NONE) {
			app_event_t evt = {
				.type = APP_EVT_FAULT,
				.data.fault = heat_fault,
			};

			(void)app_event_submit(&evt);
		}
		subsec_ms += delta_ms;
		if (subsec_ms >= 1000U) {
			uint32_t remaining = app_context_get_remaining_sec();

			subsec_ms -= 1000U;
			if (remaining > 0U) {
				remaining--;
				app_context_set_remaining_sec(remaining);
			}
			if (remaining == 0U) {
				app_submit_simple_event(APP_EVT_TREATMENT_DONE);
			}
		}
		break;

	case TREATMENT_STATE_RUNNING_COLD:
		fan_control_set_level(status.config.air_level);
		fan_control_get_diag(&fan_diag);
		app_context_set_fan_diag(&fan_diag);
		mist_service_set_desired(true, status.config.mist_level);
		heat_control_stop();
		heat_control_get_diag(&heat_diag);
		app_context_set_heat_diag(&heat_diag);
		subsec_ms += delta_ms;
		if (subsec_ms >= 1000U) {
			uint32_t remaining = app_context_get_remaining_sec();

			subsec_ms -= 1000U;
			if (remaining > 0U) {
				remaining--;
				app_context_set_remaining_sec(remaining);
			}
			if (remaining == 0U) {
				app_submit_simple_event(APP_EVT_TREATMENT_DONE);
			}
		}
		break;

	default:
		subsec_ms = 0U;
		heat_control_stop();
		heat_control_get_diag(&heat_diag);
		app_context_set_heat_diag(&heat_diag);
		mist_service_set_desired(false, MIST_LEVEL_UI_OFF);
		if ((status.state == TREATMENT_STATE_READY) ||
		    (status.state == TREATMENT_STATE_DONE) ||
		    (status.state == TREATMENT_STATE_FAULT)) {
			fan_control_stop();
		}
		fan_control_get_diag(&fan_diag);
		app_context_set_fan_diag(&fan_diag);
		break;
	}
}

/*
 * 安全阶段：轮询安全服务，把结果转换成统一事件。
 *
 * 安全服务本身负责“判定”，这里负责“转发”：
 * - 故障 -> APP_EVT_FAULT
 * - 暂停 -> APP_EVT_PAUSE
 * - 恢复 -> APP_EVT_RESUME
 *
 * 这样状态变更仍回到 AppTask 中统一处理。
 */
static void app_run_safety_phase(void)
{
	struct safety_result result = safety_service_poll();

	if (result.fault != FAULT_NONE) {
		app_event_t evt = {
			.type = APP_EVT_FAULT,
			.data.fault = result.fault,
		};

		(void)app_event_submit(&evt);
	} else if (result.pause_requested) {
		app_submit_simple_event(APP_EVT_PAUSE);
	} else if (result.resume_requested) {
		app_submit_simple_event(APP_EVT_RESUME);
	}
}

/*
 * 遥测阶段：统一执行周期上报。
 *
 * 遥测不是硬实时动作，因此没必要单独占一个线程栈。
 * 合并进 supervisor 后可以继续保持 500ms 周期，但显著减少线程数量。
 */
static void app_run_telemetry_phase(void)
{
	telemetry_service_publish_now();
}

/*
 * 雾化板通信线程入口。
 *
 * 雾化板使用独立协议和处理循环，因此仍保留单独线程。
 * app_tasks 这一层只负责把它拉起，不介入它的内部通信细节。
 */
static void mist_task_entry(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	mist_service_process_task();
}

/*
 * ControlSupervisorTask 合并了采样、控制、安全和遥测四类周期任务。
 *
 * 设计思路：
 * - 用一个 10ms 基准循环驱动所有周期逻辑
 * - 10ms 跑安全轮询
 * - 20ms 跑采样和控制
 * - 500ms 跑遥测
 *
 * 这样既保留了 RTOS 下的周期任务组织方式，又减少了线程数、栈占用和
 * app_context 的锁竞争，更适合当前这种“小而精”的控制系统。
 */
static void control_supervisor_task_entry(void *a, void *b, void *c)
{
	uint32_t next_sensor_ms = 0U;
	uint32_t next_control_ms = 0U;
	uint32_t next_safety_ms = 0U;
	uint32_t next_telemetry_ms = 0U;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (true) {
		uint32_t now_ms = k_uptime_get_32();

		if ((int32_t)(now_ms - next_safety_ms) >= 0) {
			app_run_safety_phase();
			next_safety_ms = now_ms + APP_SAFETY_PERIOD_MS;
		}

		if ((int32_t)(now_ms - next_sensor_ms) >= 0) {
			app_run_sensor_phase();
			next_sensor_ms = now_ms + APP_SENSOR_PERIOD_MS;
		}

		if ((int32_t)(now_ms - next_control_ms) >= 0) {
			app_run_control_phase();
			next_control_ms = now_ms + APP_CONTROL_PERIOD_MS;
		}

		if ((int32_t)(now_ms - next_telemetry_ms) >= 0) {
			app_run_telemetry_phase();
			next_telemetry_ms = now_ms + APP_TELEMETRY_PERIOD_MS;
		}

		k_msleep(10);
	}
}

/*
 * 创建并命名所有应用线程。
 *
 * 这里的优先级大致体现了任务重要性：
 * - ControlSupervisor 优先级更高，保证周期控制及时执行
 * - AppTask 次之，负责状态机与事件裁决
 * - HostComm/MistComm 负责通信，实时性要求略低
 *
 * 线程名会出现在调试器、日志或线程分析工具中，建议始终保留。
 */
int app_tasks_start(void)
{
	k_thread_create(&app_task_thread, app_task_stack, K_THREAD_STACK_SIZEOF(app_task_stack),
			app_task_entry, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	k_thread_name_set(&app_task_thread, "AppTask");

	k_thread_create(&host_rx_thread, host_rx_stack, K_THREAD_STACK_SIZEOF(host_rx_stack),
			(k_thread_entry_t)host_comm_rx_task, NULL, NULL, NULL, 6, 0, K_NO_WAIT);
	k_thread_name_set(&host_rx_thread, "HostCommTask");

	k_thread_create(&mist_thread, mist_stack, K_THREAD_STACK_SIZEOF(mist_stack),
			mist_task_entry, NULL, NULL, NULL, 6, 0, K_NO_WAIT);
	k_thread_name_set(&mist_thread, "MistCommTask");

	k_thread_create(&supervisor_thread, supervisor_stack,
			K_THREAD_STACK_SIZEOF(supervisor_stack),
			control_supervisor_task_entry, NULL, NULL, NULL, 4, 0, K_NO_WAIT);
	k_thread_name_set(&supervisor_thread, "ControlSupervisor");

	return 0;
}

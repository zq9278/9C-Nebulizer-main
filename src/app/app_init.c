#include "app_init.h"

#include <platform/board_devices.h>
#include <services/communication/host_comm_service.h>
#include <services/fan/fan_control.h>
#include <services/heat/heat_control.h>
#include <services/mist/mist_service.h>
#include <services/sensors/sensor_service.h>
#include <services/storage/settings_store.h>
#include <src/app/app_context.h>
#include <src/platform/board_check.h>
#include <src/safety/safety_service.h>
#include <src/telemetry/telemetry_service.h>
#include <platform/log.h>

/*
 * 应用初始化顺序刻意保持为“上下文 -> 板级检查 -> 配置恢复 -> 外设服务 -> 安全/遥测”。
 *
 * 这样安排的原因是：
 * 1. app_context 要先可用，后续服务才能把默认值和恢复值写进去
 * 2. 关键板级设备未就绪时，应尽早失败，避免后续服务部分拉起
 * 3. 配置和 PID 参数需要在控制类服务完全运行前恢复
 * 4. 安全与遥测放在最后，确保它们看到的是完整初始化后的系统状态
 */
int app_init(void)
{
	int ret;
	treatment_config_t config;
	pid_params_t pid;
	pid_params_t preheat_pid;
	pid_params_t fan_pid;

	/* 初始化全局状态仓库，写入默认配置、默认状态和心跳截止时间。 */
	ret = app_context_init();
	if (ret != 0) {
		return ret;
	}

	/* 在启动早期检查关键板级资源，避免进入运行期后才发现设备缺失。 */
	ret = board_check_critical_devices();
	if (ret != 0) {
		return ret;
	}

	/* 初始化持久化配置存储。后续治疗参数和 PID 参数都依赖这里恢复。 */
	ret = settings_store_init();
	if (ret != 0) {
		return ret;
	}

	/* 恢复治疗配置；失败时保留 app_context_init() 中的默认配置。 */
	ret = settings_store_load(&config);
	if (ret == 0) {
		app_context_set_config(&config);
		app_context_set_remaining_sec(config.duration_sec);
	}

	/* 传感器服务先于控制服务初始化，保证控制回路一启动就能读到现场输入。 */
	ret = sensor_service_init();
	if (ret != 0) {
		LOG_ERR("sensor init failed: %d", ret);
		return ret;
	}

	/* 风机服务单独初始化，后续热疗、冷疗和维护模式都会复用。 */
	ret = fan_control_init();
	if (ret != 0) {
		LOG_ERR("fan init failed: %d", ret);
		return ret;
	}

	/* 同步并恢复 PB11 出口温度风扇 PID。 */
	fan_control_get_pid(&fan_pid);
	app_context_set_fan_pid(&fan_pid);
	ret = settings_store_load_fan_pid(&fan_pid);
	if (ret == 0) {
		ret = fan_control_set_pid(&fan_pid);
		if (ret != 0) {
			LOG_ERR("fan pid load failed: %d", ret);
			return ret;
		}
		app_context_set_fan_pid(&fan_pid);
	}

	/* 热控初始化会建立 PID 默认状态与 TRIAC 控制基础。 */
	ret = heat_control_init();
	if (ret != 0) {
		LOG_ERR("heat init failed: %d", ret);
		return ret;
	}

	/* 先把热控内部默认 PID 同步到全局上下文，作为后续恢复失败时的兜底值。 */
	heat_control_get_pid(&pid);
	app_context_set_heat_pid(&pid);

	/* 如果存储里有历史 PID 参数，则覆盖默认 PID 并同步回上下文。 */
	ret = settings_store_load_pid(&pid);
	if (ret == 0) {
		ret = heat_control_set_pid(&pid);
		if (ret != 0) {
			LOG_ERR("heat pid load failed: %d", ret);
			return ret;
		}

		app_context_set_heat_pid(&pid);
	}

	/* PB10 预热 PID 使用独立参数，不能与 PB11 出口 PID 共用积分和增益。 */
	heat_control_get_preheat_pid(&preheat_pid);
	app_context_set_preheat_pid(&preheat_pid);
	ret = settings_store_load_preheat_pid(&preheat_pid);
	if (ret == 0) {
		ret = heat_control_set_preheat_pid(&preheat_pid);
		if (ret != 0) {
			LOG_ERR("preheat pid load failed: %d", ret);
			return ret;
		}
		app_context_set_preheat_pid(&preheat_pid);
	}

	/* 雾化服务包含对独立雾化板的链路建立与状态机准备。 */
	ret = mist_service_init();
	if (ret != 0) {
		LOG_ERR("mist init failed: %d", ret);
		return ret;
	}

	/* 上位机通信服务准备好后，系统才具备对外收发命令和状态的能力。 */
	ret = host_comm_service_init();
	if (ret != 0) {
		LOG_ERR("host comm init failed: %d", ret);
		return ret;
	}

	/* 安全服务最后初始化，用于托管运行期的暂停、恢复和故障判定。 */
	ret = safety_service_init();
	if (ret != 0) {
		return ret;
	}

	/* 遥测服务放在最后，确保第一次上报看到的是完整可运行的系统快照。 */
	return telemetry_service_init();
}

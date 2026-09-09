#include "board_check.h"

#include <platform/board_devices.h>
#include <platform/log.h>

/*
 * 启动时执行关键设备检查。
 *
 * 这个模块本身很薄，它的职责主要是把“板级资源初始化/检查”与应用初始化顺序分开，
 * 让 app_init() 读起来更清楚。
 */
int board_check_critical_devices(void)
{
	int ret = board_devices_init();

	if (ret != 0) {
		LOG_ERR("board device check failed: %d", ret);
	} else {
		LOG_INF("board device check passed");
	}

	return ret;
}

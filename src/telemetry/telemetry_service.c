#include "telemetry_service.h"

#include <services/communication/host_comm_service.h>
#include <src/app/app_context.h>

/* 当前实现里没有专门的遥测上下文，因此初始化为空实现。 */
int telemetry_service_init(void)
{
	return 0;
}

/*
 * 立即发布一次状态遥测。
 *
 * 这里做的事很直接：
 * 1. 从 app_context 取当前完整快照
 * 2. 编码成 Host status 帧
 * 3. 送入 Host 发送链路
 */
int telemetry_service_publish_now(void)
{
	telemetry_status_t status;

	app_context_get_status(&status);
	return host_comm_service_send_status(&status);
}

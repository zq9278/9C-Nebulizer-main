#include "app_events.h"

#include <nebulizer/app_config.h>

/*
 * Zephyr 消息队列：
 * - 每个元素是一个完整的 app_event_t
 * - 队列长度由 APP_EVENT_QUEUE_LEN 决定
 * - 对齐单位为 4 字节
 *
 * 这里等价于“应用内部的邮箱”。
 */
K_MSGQ_DEFINE(app_event_msgq, sizeof(app_event_t), APP_EVENT_QUEUE_LEN, 4);

/* 非阻塞提交事件。若队列已满，则调用者会收到错误码。 */
int app_event_submit(const app_event_t *evt)
{
	return k_msgq_put(&app_event_msgq, evt, K_NO_WAIT);
}

/* 按调用者指定的超时等待下一条事件。 */
int app_event_wait(app_event_t *evt, k_timeout_t timeout)
{
	return k_msgq_get(&app_event_msgq, evt, timeout);
}

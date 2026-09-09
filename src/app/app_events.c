#include "app_events.h"

#include <nebulizer/app_config.h>


static StaticQueue_t app_event_msgq_control;
static uint8_t app_event_msgq_storage[APP_EVENT_QUEUE_LEN * sizeof(app_event_t)];
static QueueHandle_t app_event_msgq;

/* 非阻塞提交事件。若队列已满，则调用者会收到错误码。 */
int app_event_submit(const app_event_t *evt)
{
	return (xQueueSend(app_event_msgq, evt, 0) == pdPASS ? 0 : -EAGAIN);
}

/* 按调用者指定的超时等待下一条事件。 */
int app_event_wait(app_event_t *evt, TickType_t timeout)
{
	return (xQueueReceive(app_event_msgq, evt, timeout) == pdPASS ? 0 : -EAGAIN);
}

void app_events_init(void)
{
	app_event_msgq = xQueueCreateStatic(APP_EVENT_QUEUE_LEN, sizeof(app_event_t), app_event_msgq_storage, &app_event_msgq_control);
	configASSERT(app_event_msgq != NULL);
}

#ifndef SRC_APP_APP_EVENTS_H_
#define SRC_APP_APP_EVENTS_H_

#include <nebulizer/app_events.h>
#include <platform/runtime.h>

/*
 * app_event_msgq 是应用内部的主事件通道。
 *
 * 所有需要交给 AppTask 统一裁决的动作，最终都应变成一个 app_event_t：
 * - Host 命令
 * - 安全暂停/恢复
 * - 故障
 * - 治疗结束
 * - 雾化状态变化通知
 */
void app_events_init(void);
int app_event_submit(const app_event_t *evt);
int app_event_wait(app_event_t *evt, TickType_t timeout);

#endif /* SRC_APP_APP_EVENTS_H_ */

#include "host_comm_tx.h"

#include <errno.h>
#include <string.h>

#include <drivers_app/uart/uart_port.h>
#include <nebulizer/app_config.h>
#include <protocols/common/frame_codec.h>
#include <services/communication/host_comm_service.h>
#include <platform/runtime.h>
#include <platform/log.h>

struct host_tx_frame {
	uint8_t data[FRAME_CODEC_MAX_FRAME];
	size_t len;
};

static StaticQueue_t host_tx_msgq_control;
static uint8_t host_tx_msgq_storage[APP_HOST_TX_QUEUE_LEN * sizeof(struct host_tx_frame)];
static QueueHandle_t host_tx_msgq;

int host_comm_tx_init(void)
{
	host_tx_msgq = xQueueCreateStatic(APP_HOST_TX_QUEUE_LEN, sizeof(struct host_tx_frame), host_tx_msgq_storage, &host_tx_msgq_control);
	configASSERT(host_tx_msgq != NULL);
	return 0;
}

int host_comm_tx_enqueue(const uint8_t *data, size_t len)
{
	struct host_tx_frame frame = { 0 };

	if ((data == NULL) || (len > sizeof(frame.data))) {
		return -EINVAL;
	}

	memcpy(frame.data, data, len);
	frame.len = len;
	return (xQueueSend(host_tx_msgq, &frame, 0) == pdPASS ? 0 : -EAGAIN);
}

/*
 * 处理一帧待发送数据。
 *
 * Host TX 现在不再独立占用一个线程，而是由 HostCommTask 在主循环中顺手抽空处理。
 * 这样既保留了“发送队列解耦”的好处，又减少了一个线程栈和一次额外调度。
 */
int host_comm_tx_process_one(TickType_t timeout)
{
	struct host_tx_frame frame;

	if (xQueueReceive(host_tx_msgq, &frame, timeout) != pdPASS) {
		return -EAGAIN;
	}

	return host_comm_service_send_raw(frame.data, frame.len);
}

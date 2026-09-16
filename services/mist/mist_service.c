#include "mist_service.h"

#include <string.h>

#include <nebulizer/app_config.h>
#include <nebulizer/protocol_ids.h>
#include <protocols/mist/mist_commands.h>
#include <services/mist/mist_board_client.h>
#include <src/app/app_context.h>
#include <src/app/app_events.h>
#include <platform/runtime.h>
#include <platform/log.h>

struct mist_service_request {
	uint8_t cmd_id;
	uint8_t payload[8];
	uint16_t payload_len;
};

static StaticQueue_t mist_req_msgq_control;
static uint8_t mist_req_msgq_storage[APP_MIST_TX_QUEUE_LEN * sizeof(struct mist_service_request)];
static QueueHandle_t mist_req_msgq;
static StaticSemaphore_t mist_lock_storage;
static SemaphoreHandle_t mist_lock;

static struct {
	bool desired_running;
	mist_level_t desired_level;
	bool last_running;
	mist_level_t last_level;
	uint32_t last_status_poll_ms;
	mist_board_status_t last_reported_status;
	bool last_reported_status_valid;
} mist_ctx;

static bool mist_service_status_changed(const mist_board_status_t *lhs, const mist_board_status_t *rhs)
{
	return (lhs->online != rhs->online) ||
	       (lhs->low_water != rhs->low_water) ||
	       (lhs->safety_locked != rhs->safety_locked) ||
	       (lhs->desired_level != rhs->desired_level) ||
	       (lhs->actual_level != rhs->actual_level) ||
	       (lhs->running != rhs->running) ||
	       (lhs->fault_code != rhs->fault_code) ||
	       (lhs->last_seq != rhs->last_seq) ||
	       (lhs->consecutive_failures != rhs->consecutive_failures);
}

static int mist_service_queue(uint8_t cmd_id, const uint8_t *payload, uint16_t payload_len)
{
	struct mist_service_request req = { .cmd_id = cmd_id, .payload_len = payload_len };

	if ((payload != NULL) && (payload_len > 0U)) {
		memcpy(req.payload, payload, payload_len);
	}

	return (xQueueSend(mist_req_msgq, &req, 0) == pdPASS ? 0 : -EAGAIN);
}

int mist_service_init(void)
{
	mist_req_msgq = xQueueCreateStatic(APP_MIST_TX_QUEUE_LEN, sizeof(struct mist_service_request), mist_req_msgq_storage, &mist_req_msgq_control);
	configASSERT(mist_req_msgq != NULL);
	mist_lock = xSemaphoreCreateMutexStatic(&mist_lock_storage);
	configASSERT(mist_lock != NULL);
	memset(&mist_ctx, 0, sizeof(mist_ctx));
	return mist_board_client_init();
}

int mist_service_set_desired(bool running, mist_level_t level)
{
	uint8_t payload[2];
	uint16_t payload_len;
	int ret = 0;

	xSemaphoreTake(mist_lock, portMAX_DELAY);
	mist_ctx.desired_running = running;
	mist_ctx.desired_level = level;

	if (mist_ctx.last_level != level) {
		mist_commands_level_payload((uint8_t)level, payload, &payload_len);
		ret = mist_service_queue(MIST_CMD_SET_LEVEL, payload, payload_len);
		if (ret == 0) {
			mist_ctx.last_level = level;
		} else {
			/* Do not enqueue START ahead of its required SET_LEVEL. */
			xSemaphoreGive(mist_lock);
			return ret;
		}
	}

	if (running != mist_ctx.last_running) {
		ret = mist_service_queue(running ? MIST_CMD_START : MIST_CMD_STOP, NULL, 0U);
		if (ret == 0) {
			mist_ctx.last_running = running;
		}
	}

	xSemaphoreGive(mist_lock);
	return ret;
}

int mist_service_request_stop(void)
{
	int ret;

	xSemaphoreTake(mist_lock, portMAX_DELAY);
	mist_ctx.desired_running = false;
	if (!mist_ctx.last_running) {
		xSemaphoreGive(mist_lock);
		return 0;
	}

	ret = mist_service_queue(MIST_CMD_STOP, NULL, 0U);
	if (ret == 0) {
		mist_ctx.last_running = false;
	}

	xSemaphoreGive(mist_lock);
	return ret;
}

int mist_service_trigger_otp_reset(void)
{
	return mist_board_client_trigger_otp_reset();
}

static void mist_service_process_tx(void)
{
	struct mist_service_request req;
	/* Only this task consumes the queue. Keep the head until the client has
	 * accepted it; -EBUSY means an earlier command still awaits its reply. */
	if (xQueuePeek(mist_req_msgq, &req, 0) != pdPASS) {
		return;
	}
	struct mist_client_request client_req = {
		.cmd_id = req.cmd_id,
		.payload_len = req.payload_len,
	};
	memcpy(client_req.payload, req.payload, req.payload_len);
	int ret = mist_board_client_submit(&client_req);
	if (ret == 0) {
		(void)xQueueReceive(mist_req_msgq, &req, 0);
	} else if (ret != -EBUSY) {
		LOG_WRN("mist send deferred cmd=0x%02x error=%d", req.cmd_id, ret);
	}
}

void mist_service_process_task(void)
{
	mist_board_status_t status;

	while (true) {
		mist_board_client_process_rx(pdMS_TO_TICKS(20));
		mist_board_client_process_timeouts();

		mist_service_process_tx();

			if ((int32_t)(runtime_now_ms() - mist_ctx.last_status_poll_ms) >= 1000) {
				(void)mist_service_queue(MIST_CMD_GET_STATUS, NULL, 0U);
				mist_ctx.last_status_poll_ms = runtime_now_ms();
			}

			mist_board_client_get_status(&status);
			app_context_set_mist_status(&status);
			if (!mist_ctx.last_reported_status_valid ||
			    mist_service_status_changed(&status, &mist_ctx.last_reported_status)) {
				app_event_t evt = {
					.type = APP_EVT_MIST_STATUS,
					.data.mist_status = status,
				};

				(void)app_event_submit(&evt);
				mist_ctx.last_reported_status = status;
				mist_ctx.last_reported_status_valid = true;
			}
			vTaskDelay(pdMS_TO_TICKS(10));
		}
}

void mist_service_get_status(mist_board_status_t *status)
{
	mist_board_client_get_status(status);
}

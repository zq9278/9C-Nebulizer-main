#include "mist_service.h"

#include <string.h>

#include <nebulizer/app_config.h>
#include <nebulizer/protocol_ids.h>
#include <protocols/mist/mist_commands.h>
#include <services/mist/mist_board_client.h>
#include <src/app/app_context.h>
#include <src/app/app_events.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mist_service, CONFIG_NEBULIZER_LOG_LEVEL);

struct mist_service_request {
	uint8_t cmd_id;
	uint8_t payload[8];
	uint16_t payload_len;
};

K_MSGQ_DEFINE(mist_req_msgq, sizeof(struct mist_service_request), APP_MIST_TX_QUEUE_LEN, 4);

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

	return k_msgq_put(&mist_req_msgq, &req, K_NO_WAIT);
}

int mist_service_init(void)
{
	memset(&mist_ctx, 0, sizeof(mist_ctx));
	return mist_board_client_init();
}

int mist_service_set_desired(bool running, mist_level_t level)
{
	uint8_t payload[2];
	uint16_t payload_len;
	int ret;

	mist_ctx.desired_running = running;
	mist_ctx.desired_level = level;

	if (mist_ctx.last_level != level) {
		mist_commands_level_payload((uint8_t)level, payload, &payload_len);
		ret = mist_service_queue(MIST_CMD_SET_LEVEL, payload, payload_len);
		if (ret == 0) {
			mist_ctx.last_level = level;
		}
	}

	if (running != mist_ctx.last_running) {
		ret = mist_service_queue(running ? MIST_CMD_START : MIST_CMD_STOP, NULL, 0U);
		if (ret == 0) {
			mist_ctx.last_running = running;
		}
	}

	return 0;
}

int mist_service_request_stop(void)
{
	int ret;

	mist_ctx.desired_running = false;
	if (!mist_ctx.last_running) {
		return 0;
	}

	ret = mist_service_queue(MIST_CMD_STOP, NULL, 0U);
	if (ret == 0) {
		mist_ctx.last_running = false;
	}

	return ret;
}

int mist_service_trigger_otp_reset(void)
{
	return mist_board_client_trigger_otp_reset();
}

void mist_service_process_task(void)
{
	struct mist_service_request req;
	mist_board_status_t status;

	while (true) {
		mist_board_client_process_rx(K_MSEC(20));
		mist_board_client_process_timeouts();

		if (k_msgq_get(&mist_req_msgq, &req, K_NO_WAIT) == 0) {
			struct mist_client_request client_req = {
				.cmd_id = req.cmd_id,
				.payload_len = req.payload_len,
			};

			memcpy(client_req.payload, req.payload, req.payload_len);
			(void)mist_board_client_submit(&client_req);
		}

			if ((int32_t)(k_uptime_get_32() - mist_ctx.last_status_poll_ms) >= 1000) {
				(void)mist_service_queue(MIST_CMD_GET_STATUS, NULL, 0U);
				mist_ctx.last_status_poll_ms = k_uptime_get_32();
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
			k_msleep(10);
		}
}

void mist_service_get_status(mist_board_status_t *status)
{
	mist_board_client_get_status(status);
}

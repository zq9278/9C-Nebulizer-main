#include "mist_board_client.h"

#include <errno.h>
#include <string.h>

#include <drivers_app/uart/uart_port.h>
#include <nebulizer/app_config.h>
#include <nebulizer/protocol_ids.h>
#include <platform/board_devices.h>
#include <protocols/common/ring_frame_parser.h>
#include <protocols/mist/mist_protocol.h>
#include <platform/runtime.h>
#include <platform/log.h>

struct mist_pending_cmd {
	bool active;
	struct mist_client_request request;
	uint8_t seq;
	uint8_t retries_left;
	uint32_t deadline_ms;
};

static struct uart_port mist_uart_port;
static struct ring_frame_parser mist_parser;
static struct mist_pending_cmd pending;
static mist_board_status_t mist_status;
static const struct board_gpio *otp_reset_gpio;
static uint8_t mist_rx_storage[APP_MIST_RX_RING_SIZE];
static uint8_t next_seq = 1U;

static int mist_board_client_run_otp_reset_sequence(void)
{
	int ret;

	if ((otp_reset_gpio == NULL) || !board_device_ready(otp_reset_gpio->port)) {
		return -ENODEV;
	}

	ret = board_gpio_configure(otp_reset_gpio, GPIO_OUTPUT_ACTIVE);
	if (ret != 0) {
		LOG_ERR("otp reset gpio output configure failed: %d", ret);
		return ret;
	}

	vTaskDelay(pdMS_TO_TICKS(APP_OTP_RESET_HIGH_MS));

	ret = board_gpio_configure(otp_reset_gpio, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("otp reset gpio input configure failed: %d", ret);
		return ret;
	}

	return 0;
}

static int mist_board_client_send_now(const struct mist_client_request *req, uint8_t seq)
{
	uint8_t frame[FRAME_CODEC_MAX_FRAME];
	size_t frame_len;
	int ret;

	ret = mist_protocol_encode(seq, MIST_FRAME_TYPE_CMD, req->cmd_id, seq, req->payload,
				   req->payload_len, frame, sizeof(frame), &frame_len);
	if (ret != 0) {
		return ret;
	}

	return uart_port_send(&mist_uart_port, frame, frame_len, pdMS_TO_TICKS(50));
}

static void mist_board_client_set_online(bool online)
{
	mist_status.online = online;
	if (online) {
		mist_status.last_seen_ms = runtime_now_ms();
		mist_status.consecutive_failures = 0U;
	}
}

static void mist_board_client_update_fault(uint16_t fault_code)
{
	bool was_low_water = mist_status.low_water;

	mist_status.fault_code = fault_code;
	mist_status.low_water = (fault_code == MIST_FAULT_LOW_WATER);
	if (!was_low_water && mist_status.low_water) {
		LOG_WRN("mist board reported low water");
	} else if (was_low_water && !mist_status.low_water) {
		LOG_INF("mist board water restored");
	}

	if (fault_code == MIST_FAULT_NONE) {
		mist_status.safety_locked = false;
	}
}

static void mist_board_client_parser_cb(const struct frame_codec_frame *frame, void *user_data)
{
	struct mist_protocol_packet packet;

	ARG_UNUSED(user_data);

	if (mist_protocol_decode(frame, &packet) != 0) {
		LOG_WRN("mist frame decode failed");
		return;
	}

	mist_board_client_set_online(true);
	mist_status.last_seq = packet.seq;

	switch (frame->type) {
	case MIST_FRAME_TYPE_ACK:
		if (pending.active && (packet.seq == pending.seq)) {
			if (pending.request.cmd_id != MIST_CMD_GET_STATUS) {
				LOG_INF("mist ACK cmd=0x%02x seq=%u", pending.request.cmd_id, pending.seq);
			}
			pending.active = false;
		}
		break;

	case MIST_FRAME_TYPE_NACK:
		if (pending.active && (packet.seq == pending.seq)) {
			uint8_t error_code = (packet.data_len > 0U) ? packet.data[0] : 0U;
			LOG_WRN("mist NACK cmd=0x%02x seq=%u error=%u",
				pending.request.cmd_id, pending.seq, error_code);

			if (error_code == MIST_ERR_SAFETY_LOCKED) {
				LOG_WRN("mist board safety locked cmd=0x%02x", pending.request.cmd_id);
				mist_status.running = false;
				mist_status.actual_level = 0U;
				mist_status.safety_locked = true;
				mist_board_client_update_fault(MIST_FAULT_LOW_WATER);
			}
			pending.active = false;
		}
		break;

	case MIST_FRAME_TYPE_STATUS:
		if (packet.data_len >= 3U) {
			mist_status.running = (packet.data[0] != 0U);
			mist_status.actual_level = packet.data[1];
			mist_board_client_update_fault(packet.data[2]);
		}

		if (pending.active && (packet.seq == pending.seq)) {
			pending.active = false;
		}
		break;

	case MIST_FRAME_TYPE_FAULT:
		if (packet.data_len >= 2U) {
			mist_board_client_update_fault((uint16_t)packet.data[0] |
						       ((uint16_t)packet.data[1] << 8));
			if (mist_status.low_water) {
				mist_status.running = false;
				mist_status.actual_level = 0U;
				mist_status.safety_locked = true;
			}
		}
		pending.active = false;
		break;

	default:
		break;
	}
}

int mist_board_client_init(void)
{
	int ret;

	memset(&mist_status, 0, sizeof(mist_status));
	otp_reset_gpio = board_get_otp_reset();
	if (otp_reset_gpio == NULL) {
		return -ENODEV;
	}

	ret = mist_board_client_run_otp_reset_sequence();
	if (ret != 0) {
		return ret;
	}

	ret = uart_port_init(&mist_uart_port, board_get_mist_uart(), mist_rx_storage,
				     sizeof(mist_rx_storage), NULL, NULL);
	if (ret != 0) {
		return ret;
	}

	ring_frame_parser_init(&mist_parser, mist_board_client_parser_cb, NULL);
	return 0;
}

int mist_board_client_trigger_otp_reset(void)
{
	return mist_board_client_run_otp_reset_sequence();
}

int mist_board_client_submit(const struct mist_client_request *req)
{
	int ret;

	if (pending.active) {
		return -EBUSY;
	}

	pending.active = true;
	pending.request = *req;
	pending.seq = next_seq++;
	pending.retries_left = APP_MIST_CMD_RETRY_COUNT;
	pending.deadline_ms = runtime_now_ms() + APP_MIST_CMD_TIMEOUT_MS;
	mist_status.desired_level = (req->cmd_id == MIST_CMD_SET_LEVEL) ? req->payload[0] :
				    mist_status.desired_level;

	ret = mist_board_client_send_now(req, pending.seq);
	if (ret != 0) {
		pending.active = false;
		return ret;
	}
	if (req->cmd_id != MIST_CMD_GET_STATUS) {
		LOG_INF("mist TX cmd=0x%02x seq=%u level=%u", req->cmd_id,
			pending.seq, mist_status.desired_level);
	}

	return 0;
}

void mist_board_client_process_rx(TickType_t timeout)
{
	uint8_t buf[64];
	size_t rd;

	if (uart_port_wait_rx(&mist_uart_port, timeout) != 0) {
		return;
	}

	do {
		rd = uart_port_read(&mist_uart_port, buf, sizeof(buf));
		if (rd > 0U) {
			ring_frame_parser_feed(&mist_parser, buf, rd);
		}
	} while (rd > 0U);
}

void mist_board_client_process_timeouts(void)
{
	int ret;
	uint32_t now_ms = runtime_now_ms();

	if (!pending.active) {
		return;
	}

	if ((pending.deadline_ms != 0U) && ((int32_t)(pending.deadline_ms - now_ms) > 0)) {
		return;
	}

	if (pending.retries_left > 0U) {
		pending.retries_left--;
		pending.deadline_ms = now_ms + APP_MIST_CMD_TIMEOUT_MS;
		ret = mist_board_client_send_now(&pending.request, pending.seq);
		if (ret != 0) {
			LOG_WRN("mist resend failed: %d", ret);
		}
		return;
	}

	LOG_ERR("mist board timeout cmd=0x%02x", pending.request.cmd_id);
	pending.active = false;
	mist_status.online = false;
	mist_status.consecutive_failures++;
}

void mist_board_client_get_status(mist_board_status_t *status)
{
	*status = mist_status;
}

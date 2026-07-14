#include "host_comm_service.h"

#include <errno.h>
#include <string.h>

#include <drivers_app/uart/uart_port.h>
#include <nebulizer/app_config.h>
#include <nebulizer/protocol_ids.h>
#include <platform/board_devices.h>
#include <protocols/host/host_protocol.h>
#include <protocols/common/ring_frame_parser.h>
#include <services/communication/host_comm_tx.h>
#include <src/app/app_context.h>
#include <src/app/app_events.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(host_comm_service, CONFIG_NEBULIZER_LOG_LEVEL);

static struct uart_port host_uart_port;
static struct ring_frame_parser host_parser;
static uint8_t host_rx_storage[APP_HOST_RX_RING_SIZE];
static uint16_t status_frame_id = 0x8000U;

static void host_comm_service_parser_cb(const struct frame_codec_frame *frame, void *user_data)
{
	app_event_t evt;

	ARG_UNUSED(user_data);

	if (host_protocol_decode_frame(frame, &evt) != 0) {
		LOG_WRN("host protocol decode failed");
		return;
	}

	if (evt.data.host_cmd.command_id == HOST_CMD_HEARTBEAT) {
		app_context_heartbeat_touch();
		host_comm_service_send_ack(evt.data.host_cmd.frame_id, HOST_CMD_HEARTBEAT, true, 0U);
		return;
	}

	if (app_event_submit(&evt) != 0) {
		LOG_WRN("app event queue full");
	}
}

int host_comm_service_init(void)
{
	int ret;

	ret = uart_port_init(&host_uart_port, board_get_host_uart(), host_rx_storage,
			     sizeof(host_rx_storage), NULL, NULL);
	if (ret != 0) {
		return ret;
	}

	ring_frame_parser_init(&host_parser, host_comm_service_parser_cb, NULL);
	return host_comm_tx_init();
}

int host_comm_service_process_rx(k_timeout_t timeout)
{
	uint8_t buf[64];
	size_t rd;

	if (uart_port_wait_rx(&host_uart_port, timeout) != 0) {
		return -EAGAIN;
	}

	do {
		rd = uart_port_read(&host_uart_port, buf, sizeof(buf));
		if (rd > 0U) {
			ring_frame_parser_feed(&host_parser, buf, rd);
		}
	} while (rd > 0U);

	return 0;
}

int host_comm_service_send_ack(uint16_t frame_id, uint8_t cmd_id, bool ok, uint8_t error)
{
	uint8_t frame[FRAME_CODEC_MAX_FRAME];
	size_t frame_len;
	int ret;

	ret = host_protocol_encode_ack(frame_id, cmd_id, ok, error, frame, sizeof(frame), &frame_len);
	if (ret != 0) {
		return ret;
	}

	return host_comm_tx_enqueue(frame, frame_len);
}

int host_comm_service_send_status(const telemetry_status_t *status)
{
	uint8_t frame[FRAME_CODEC_MAX_FRAME];
	size_t frame_len;
	int ret;

	ret = host_protocol_encode_status(status_frame_id++, status, frame, sizeof(frame), &frame_len);
	if (ret != 0) {
		return ret;
	}

	return host_comm_tx_enqueue(frame, frame_len);
}

int host_comm_service_send_config(const treatment_config_t *config)
{
	uint8_t frame[FRAME_CODEC_MAX_FRAME];
	size_t frame_len;
	int ret;

	ret = host_protocol_encode_config(status_frame_id++, config, frame, sizeof(frame), &frame_len);
	if (ret != 0) {
		return ret;
	}

	return host_comm_tx_enqueue(frame, frame_len);
}

int host_comm_service_send_pid(const pid_params_t *pid)
{
	uint8_t frame[FRAME_CODEC_MAX_FRAME];
	size_t frame_len;
	int ret;

	ret = host_protocol_encode_pid(status_frame_id++, pid, frame, sizeof(frame), &frame_len);
	if (ret != 0) {
		return ret;
	}

	return host_comm_tx_enqueue(frame, frame_len);
}

int host_comm_service_send_runtime(const telemetry_status_t *status)
{
	uint8_t frame[FRAME_CODEC_MAX_FRAME];
	size_t frame_len;
	int ret;

	ret = host_protocol_encode_runtime(status_frame_id++, status, frame, sizeof(frame), &frame_len);
	if (ret != 0) {
		return ret;
	}

	return host_comm_tx_enqueue(frame, frame_len);
}

int host_comm_service_send_maintenance(const maintenance_control_t *maintenance)
{
	uint8_t frame[FRAME_CODEC_MAX_FRAME];
	size_t frame_len;
	int ret;

	ret = host_protocol_encode_maintenance(status_frame_id++, maintenance, frame, sizeof(frame),
					       &frame_len);
	if (ret != 0) {
		return ret;
	}

	return host_comm_tx_enqueue(frame, frame_len);
}

int host_comm_service_send_raw(const uint8_t *data, size_t len)
{
	return uart_port_send(&host_uart_port, data, len, K_MSEC(100));
}

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
#include <services/heat/heat_control.h>
#include <services/fan/fan_control.h>
#include <src/app/app_context.h>
#include <src/app/app_events.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(host_comm_service, CONFIG_NEBULIZER_LOG_LEVEL);

#define HOST_ERR_BUSY 0x7EU

static struct uart_port host_uart_port;
static struct ring_frame_parser host_parser;
static uint8_t host_rx_storage[APP_HOST_RX_RING_SIZE];
static uint16_t status_frame_id = 0x8000U;
static uint32_t event_queue_full_last_log_ms;

static bool host_comm_service_handle_readonly_cmd(const host_cmd_event_t *cmd)
{
	telemetry_status_t status;
	pid_params_t pid;

	if (cmd == NULL) {
		return false;
	}

	app_context_get_status(&status);

	switch (cmd->command_id) {
	case HOST_CMD_GET_STATUS:
		(void)host_comm_service_send_status(&status);
		(void)host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return true;

	case HOST_CMD_GET_CONFIG:
		(void)host_comm_service_send_config(&status.config);
		(void)host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return true;

	case HOST_CMD_GET_RUNTIME:
		(void)host_comm_service_send_runtime(&status);
		(void)host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return true;

	case HOST_CMD_GET_MAINTENANCE:
		(void)host_comm_service_send_maintenance(&status.maintenance);
		(void)host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return true;

	case HOST_CMD_GET_PID:
		heat_control_get_pid(&pid);
		(void)host_comm_service_send_pid(&pid);
		(void)host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return true;

	case HOST_CMD_GET_PREHEAT_PID:
		heat_control_get_preheat_pid(&pid);
		(void)host_comm_service_send_preheat_pid(&pid);
		(void)host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return true;

	case HOST_CMD_GET_FAN_PID:
		fan_control_get_pid(&pid);
		(void)host_comm_service_send_fan_pid(&pid);
		(void)host_comm_service_send_ack(cmd->frame_id, cmd->command_id, true, 0U);
		return true;

	default:
		return false;
	}
}

static void host_comm_service_log_event_queue_full(void)
{
	uint32_t now_ms = k_uptime_get_32();

	if ((now_ms - event_queue_full_last_log_ms) >= 1000U) {
		event_queue_full_last_log_ms = now_ms;
		LOG_WRN("app event queue full");
	}
}

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

	if (host_comm_service_handle_readonly_cmd(&evt.data.host_cmd)) {
		return;
	}

	if (app_event_submit(&evt) != 0) {
		host_comm_service_log_event_queue_full();
		(void)host_comm_service_send_ack(evt.data.host_cmd.frame_id,
						 evt.data.host_cmd.command_id,
						 false,
						 HOST_ERR_BUSY);
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

int host_comm_service_send_preheat_pid(const pid_params_t *pid)
{
	uint8_t frame[FRAME_CODEC_MAX_FRAME];
	size_t frame_len;
	int ret;

	ret = host_protocol_encode_preheat_pid(status_frame_id++, pid, frame, sizeof(frame),
					       &frame_len);
	if (ret != 0) {
		return ret;
	}

	return host_comm_tx_enqueue(frame, frame_len);
}

int host_comm_service_send_fan_pid(const pid_params_t *pid)
{
	uint8_t frame[FRAME_CODEC_MAX_FRAME];
	size_t frame_len;
	int ret;

	ret = host_protocol_encode_fan_pid(status_frame_id++, pid, frame, sizeof(frame), &frame_len);
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

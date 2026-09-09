#include "host_protocol.h"

#include <errno.h>
#include <string.h>

#include <nebulizer/protocol_ids.h>
#include <nebulizer/app_config.h>
#include <platform/board_resources.h>
#include <protocols/common/frame_codec.h>

static void host_put_le16(uint8_t *buf, uint16_t value)
{
	buf[0] = (uint8_t)(value & 0xFFU);
	buf[1] = (uint8_t)(value >> 8);
}

static void host_put_le32(uint8_t *buf, uint32_t value)
{
	buf[0] = (uint8_t)(value & 0xFFU);
	buf[1] = (uint8_t)((value >> 8) & 0xFFU);
	buf[2] = (uint8_t)((value >> 16) & 0xFFU);
	buf[3] = (uint8_t)((value >> 24) & 0xFFU);
}

int host_protocol_decode_frame(const struct frame_codec_frame *frame, app_event_t *evt)
{
	if ((frame == NULL) || (evt == NULL) || (frame->type != HOST_FRAME_TYPE_CMD) ||
	    (frame->payload_len < 1U)) {
		return -EINVAL;
	}

	memset(evt, 0, sizeof(*evt));
	evt->type = APP_EVT_HOST_CMD;
	evt->data.host_cmd.command_id = frame->payload[0];
	evt->data.host_cmd.frame_id = frame->frame_id;
	evt->data.host_cmd.data_len = frame->payload_len - 1U;
	if (evt->data.host_cmd.data_len > sizeof(evt->data.host_cmd.data)) {
		return -EMSGSIZE;
	}
	if (evt->data.host_cmd.data_len > 0U) {
		memcpy(evt->data.host_cmd.data, &frame->payload[1], evt->data.host_cmd.data_len);
	}

	return 0;
}

int host_protocol_encode_ack(uint16_t frame_id, uint8_t cmd_id, bool ok,
			     uint8_t error_code, uint8_t *out, size_t out_size,
			     size_t *encoded_len)
{
	uint8_t payload[4] = {
		cmd_id,
		ok ? 1U : 0U,
		error_code,
		0U,
	};

	return frame_codec_encode(frame_id, ok ? HOST_FRAME_TYPE_ACK : HOST_FRAME_TYPE_NACK,
				  payload, sizeof(payload), out, out_size, encoded_len);
}

int host_protocol_encode_status(uint16_t frame_id, const telemetry_status_t *status,
				uint8_t *out, size_t out_size, size_t *encoded_len)
{
	uint8_t payload[21];

	if (status == NULL) {
		return -EINVAL;
	}

	payload[0] = (uint8_t)status->state;
	payload[1] = (uint8_t)(status->remaining_sec & 0xFFU);
	payload[2] = (uint8_t)(status->remaining_sec >> 8);
	payload[3] = (uint8_t)(status->config.target_temp_deci_c & 0xFFU);
	payload[4] = (uint8_t)(status->config.target_temp_deci_c >> 8);
	payload[5] = (uint8_t)(status->sensors.ntc_deci_c[0] & 0xFFU);
	payload[6] = (uint8_t)(status->sensors.ntc_deci_c[0] >> 8);
	payload[7] = (uint8_t)(status->sensors.ntc_deci_c[BOARD_NTC_KETTLE] & 0xFFU);
	payload[8] = (uint8_t)(status->sensors.ntc_deci_c[BOARD_NTC_KETTLE] >> 8);
	payload[9] = (uint8_t)status->config.air_level;
	payload[10] = (uint8_t)status->config.mist_level;
	payload[11] = status->sensors.liquid_present ? 1U : 0U;
	payload[12] = status->sensors.cover_closed ? 1U : 0U;
	payload[13] = (uint8_t)status->fault;
	payload[14] = status->mist.online ? 1U : 0U;
	payload[15] = (uint8_t)(status->mist.fault_code & 0xFFU);
	payload[16] = (uint8_t)(status->mist.fault_code >> 8);
	payload[17] = status->mist.running ? 1U : 0U;
	payload[18] = status->heartbeat_ok ? 1U : 0U;
	payload[19] = (uint8_t)status->config.mode;
	payload[20] = status->config.keep_warm_enabled ? 1U : 0U;

	return frame_codec_encode(frame_id, HOST_FRAME_TYPE_STATUS, payload, sizeof(payload),
				  out, out_size, encoded_len);
}

int host_protocol_encode_config(uint16_t frame_id, const treatment_config_t *config,
				uint8_t *out, size_t out_size, size_t *encoded_len)
{
	uint8_t payload[8];

	if (config == NULL) {
		return -EINVAL;
	}

	payload[0] = (uint8_t)config->mode;
	host_put_le16(&payload[1], config->target_temp_deci_c);
	host_put_le16(&payload[3], config->duration_sec);
	payload[5] = (uint8_t)config->air_level;
	payload[6] = (uint8_t)config->mist_level;
	payload[7] = config->keep_warm_enabled ? 1U : 0U;

	return frame_codec_encode(frame_id, HOST_FRAME_TYPE_CONFIG, payload, sizeof(payload),
				  out, out_size, encoded_len);
}

int host_protocol_encode_pid(uint16_t frame_id, const pid_params_t *pid,
			     uint8_t *out, size_t out_size, size_t *encoded_len)
{
	uint8_t payload[20];

	if (pid == NULL) {
		return -EINVAL;
	}

	host_put_le32(&payload[0], (uint32_t)pid->kp_milli);
	host_put_le32(&payload[4], (uint32_t)pid->ki_milli);
	host_put_le32(&payload[8], (uint32_t)pid->kd_milli);
	host_put_le32(&payload[12], (uint32_t)pid->integral_limit_permille);
	host_put_le16(&payload[16], TRIAC_MIN_DELAY_US);
	host_put_le16(&payload[18], TRIAC_MAX_DELAY_US);

	return frame_codec_encode(frame_id, HOST_FRAME_TYPE_PID, payload, sizeof(payload),
				  out, out_size, encoded_len);
}

int host_protocol_encode_preheat_pid(uint16_t frame_id, const pid_params_t *pid,
				     uint8_t *out, size_t out_size, size_t *encoded_len)
{
	uint8_t payload[20];

	if (pid == NULL) {
		return -EINVAL;
	}

	host_put_le32(&payload[0], (uint32_t)pid->kp_milli);
	host_put_le32(&payload[4], (uint32_t)pid->ki_milli);
	host_put_le32(&payload[8], (uint32_t)pid->kd_milli);
	host_put_le32(&payload[12], (uint32_t)pid->integral_limit_permille);
	host_put_le16(&payload[16], APP_HEAT_FULL_POWER_BELOW_DECI_C);
	host_put_le16(&payload[18], APP_HEAT_FULL_POWER_PERMILLE);

	return frame_codec_encode(frame_id, HOST_FRAME_TYPE_PREHEAT_PID, payload,
				  sizeof(payload), out, out_size, encoded_len);
}

int host_protocol_encode_fan_pid(uint16_t frame_id, const pid_params_t *pid,
				 uint8_t *out, size_t out_size, size_t *encoded_len)
{
	uint8_t payload[20];

	if (pid == NULL) {
		return -EINVAL;
	}

	host_put_le32(&payload[0], (uint32_t)pid->kp_milli);
	host_put_le32(&payload[4], (uint32_t)pid->ki_milli);
	host_put_le32(&payload[8], (uint32_t)pid->kd_milli);
	host_put_le32(&payload[12], (uint32_t)pid->integral_limit_permille);
	payload[16] = APP_FAN_PID_MAX_BOOST_PERCENT;
	payload[17] = FAN_LEVEL_LOW_PERCENT;
	payload[18] = FAN_LEVEL_MID_PERCENT;
	payload[19] = FAN_LEVEL_HIGH_PERCENT;

	return frame_codec_encode(frame_id, HOST_FRAME_TYPE_FAN_PID, payload, sizeof(payload),
				  out, out_size, encoded_len);
}

int host_protocol_encode_runtime(uint16_t frame_id, const telemetry_status_t *status,
				 uint8_t *out, size_t out_size, size_t *encoded_len)
{
	uint8_t payload[92] = { 0 };
	size_t payload_len = 46U;
	const pid_params_t *runtime_pid;

	if (status == NULL) {
		return -EINVAL;
	}

	runtime_pid = &status->heat_pid;

	payload[0] = (uint8_t)status->state;
	host_put_le32(&payload[1], status->remaining_sec);
	host_put_le16(&payload[5], (uint16_t)status->fault);
	payload[7] = status->heartbeat_ok ? 1U : 0U;
	host_put_le16(&payload[8], (uint16_t)status->sensors.ntc_deci_c[0]);
	host_put_le16(&payload[10], (uint16_t)status->sensors.ntc_deci_c[1]);
	host_put_le16(&payload[12], (uint16_t)status->sensors.ntc_deci_c[2]);
	host_put_le16(&payload[14], (uint16_t)status->sensors.ntc_deci_c[3]);
	payload[16] = status->sensors.liquid_present ? 1U : 0U;
	payload[17] = status->sensors.cover_closed ? 1U : 0U;
	payload[18] = status->sensors.gx1832_active ? 1U : 0U;
	host_put_le16(&payload[19], status->sensors.fan_rpm);
	payload[21] = status->mist.online ? 1U : 0U;
	payload[22] = status->mist.running ? 1U : 0U;
	payload[23] = status->mist.desired_level;
	payload[24] = status->mist.actual_level;
	host_put_le16(&payload[25], status->mist.fault_code);
	payload[27] = status->heat_diag.enabled ? 1U : 0U;
	host_put_le16(&payload[28], status->heat_diag.output_delay_us);
	host_put_le16(&payload[30], status->heat_diag.output_permille);
	host_put_le16(&payload[32], (uint16_t)status->heat_diag.error_deci_c);
	host_put_le16(&payload[34], (uint16_t)status->heat_diag.measured_temp_deci_c);
	host_put_le16(&payload[36], (uint16_t)status->heat_diag.target_temp_deci_c);
	host_put_le32(&payload[38], status->sensors.sample_uptime_ms);
	payload[42] = status->maintenance.active ? 1U : 0U;
	payload[43] = (uint8_t)status->maintenance.fan_level;
	payload[44] = (uint8_t)status->maintenance.mist_level;
	payload[45] = (uint8_t)(status->maintenance.heat_output_permille / 10U);

	if (APP_HOST_RUNTIME_PID_PARAMS_ENABLE) {
		host_put_le32(&payload[46], (uint32_t)runtime_pid->kp_milli);
		host_put_le32(&payload[50], (uint32_t)runtime_pid->ki_milli);
		host_put_le32(&payload[54], (uint32_t)runtime_pid->kd_milli);
		host_put_le32(&payload[58], (uint32_t)runtime_pid->integral_limit_permille);
		host_put_le32(&payload[62], (uint32_t)status->heat_diag.i_term_raw);
		host_put_le16(&payload[66], status->sensors.ntc_raw[0]);
		host_put_le16(&payload[68], status->sensors.ntc_raw[1]);
		host_put_le16(&payload[70], status->sensors.ntc_raw[2]);
		host_put_le16(&payload[72], status->sensors.ntc_raw[3]);
		host_put_le16(&payload[74], status->sensors.ntc_raw_max);
		payload_len = 76U;
	}

	payload[76] = status->fan_diag.enabled ? 1U : 0U;
	payload[77] = status->fan_diag.saturated ? 1U : 0U;
	payload[78] = status->fan_diag.base_percent;
	payload[79] = status->fan_diag.output_percent;
	host_put_le16(&payload[80], status->fan_diag.boost_permille);
	host_put_le16(&payload[82], (uint16_t)status->fan_diag.error_deci_c);
	host_put_le16(&payload[84], (uint16_t)status->fan_diag.measured_temp_deci_c);
	host_put_le16(&payload[86], (uint16_t)status->fan_diag.target_temp_deci_c);
	host_put_le16(&payload[88], (uint16_t)(status->fan_diag.i_term_raw / 1000));
	payload[90] = (uint8_t)status->heat_diag.phase;
	payload[91] = status->config.keep_warm_enabled ? 1U : 0U;
	payload_len = 92U;

	return frame_codec_encode(frame_id, HOST_FRAME_TYPE_RUNTIME, payload, payload_len,
				  out, out_size, encoded_len);
}

int host_protocol_encode_maintenance(uint16_t frame_id, const maintenance_control_t *maintenance,
				     uint8_t *out, size_t out_size, size_t *encoded_len)
{
	uint8_t payload[5];

	if (maintenance == NULL) {
		return -EINVAL;
	}

	payload[0] = maintenance->active ? 1U : 0U;
	payload[1] = (uint8_t)maintenance->fan_level;
	payload[2] = (uint8_t)maintenance->mist_level;
	host_put_le16(&payload[3], maintenance->heat_output_permille);

	return frame_codec_encode(frame_id, HOST_FRAME_TYPE_MAINT, payload, sizeof(payload),
				  out, out_size, encoded_len);
}

int host_protocol_encode_treatment_event(uint16_t frame_id, uint8_t event_id,
					 const telemetry_status_t *status, uint8_t *out,
					 size_t out_size, size_t *encoded_len)
{
	uint8_t payload[12];

	if (status == NULL) {
		return -EINVAL;
	}

	payload[0] = event_id;
	payload[1] = (uint8_t)status->config.mode;
	payload[2] = (uint8_t)status->state;
	payload[3] = status->config.keep_warm_enabled ? 1U : 0U;
	host_put_le32(&payload[4], status->remaining_sec);
	host_put_le16(&payload[8],
		      (uint16_t)status->sensors.ntc_deci_c[BOARD_NTC_OUTLET1]);
	host_put_le16(&payload[10],
		      (uint16_t)status->sensors.ntc_deci_c[BOARD_NTC_KETTLE]);

	return frame_codec_encode(frame_id, HOST_FRAME_TYPE_EVENT, payload, sizeof(payload),
				  out, out_size, encoded_len);
}

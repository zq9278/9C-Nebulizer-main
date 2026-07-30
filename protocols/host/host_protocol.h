#ifndef PROTOCOLS_HOST_HOST_PROTOCOL_H_
#define PROTOCOLS_HOST_HOST_PROTOCOL_H_

#include <stddef.h>
#include <stdint.h>

#include <nebulizer/app_events.h>
#include <nebulizer/app_types.h>
#include <protocols/common/frame_codec.h>

int host_protocol_decode_frame(const struct frame_codec_frame *frame, app_event_t *evt);
int host_protocol_encode_ack(uint16_t frame_id, uint8_t cmd_id, bool ok,
			     uint8_t error_code, uint8_t *out, size_t out_size,
			     size_t *encoded_len);
int host_protocol_encode_status(uint16_t frame_id, const telemetry_status_t *status,
				uint8_t *out, size_t out_size, size_t *encoded_len);
int host_protocol_encode_config(uint16_t frame_id, const treatment_config_t *config,
				uint8_t *out, size_t out_size, size_t *encoded_len);
int host_protocol_encode_pid(uint16_t frame_id, const pid_params_t *pid,
			     uint8_t *out, size_t out_size, size_t *encoded_len);
int host_protocol_encode_preheat_pid(uint16_t frame_id, const pid_params_t *pid,
				     uint8_t *out, size_t out_size, size_t *encoded_len);
int host_protocol_encode_fan_pid(uint16_t frame_id, const pid_params_t *pid,
				 uint8_t *out, size_t out_size, size_t *encoded_len);
int host_protocol_encode_runtime(uint16_t frame_id, const telemetry_status_t *status,
				 uint8_t *out, size_t out_size, size_t *encoded_len);
int host_protocol_encode_maintenance(uint16_t frame_id, const maintenance_control_t *maintenance,
				     uint8_t *out, size_t out_size, size_t *encoded_len);

#endif /* PROTOCOLS_HOST_HOST_PROTOCOL_H_ */

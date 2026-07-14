#include "mist_protocol.h"

#include <errno.h>
#include <string.h>

#include <protocols/common/frame_codec.h>

int mist_protocol_encode(uint16_t frame_id, uint8_t frame_type, uint8_t cmd_id,
			 uint8_t seq, const uint8_t *payload, uint16_t payload_len,
			 uint8_t *out, size_t out_size, size_t *encoded_len)
{
	uint8_t frame_payload[18];

	if (payload_len > (sizeof(frame_payload) - 2U)) {
		return -EINVAL;
	}

	frame_payload[0] = cmd_id;
	frame_payload[1] = seq;
	if ((payload_len > 0U) && (payload != NULL)) {
		memcpy(&frame_payload[2], payload, payload_len);
	}

	return frame_codec_encode(frame_id, frame_type, frame_payload, payload_len + 2U,
				  out, out_size, encoded_len);
}

int mist_protocol_decode(const struct frame_codec_frame *frame,
			 struct mist_protocol_packet *packet)
{
	if ((frame == NULL) || (packet == NULL) || (frame->payload_len < 2U)) {
		return -EINVAL;
	}

	packet->cmd_id = frame->payload[0];
	packet->seq = frame->payload[1];
	packet->data_len = frame->payload_len - 2U;
	if (packet->data_len > 0U) {
		memcpy(packet->data, &frame->payload[2], packet->data_len);
	}

	return 0;
}

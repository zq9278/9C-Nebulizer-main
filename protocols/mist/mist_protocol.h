#ifndef PROTOCOLS_MIST_MIST_PROTOCOL_H_
#define PROTOCOLS_MIST_MIST_PROTOCOL_H_

#include <stddef.h>
#include <stdint.h>

#include <nebulizer/app_types.h>
#include <protocols/common/frame_codec.h>

struct mist_protocol_packet {
	uint8_t cmd_id;
	uint8_t seq;
	uint8_t data[16];
	uint16_t data_len;
};

int mist_protocol_encode(uint16_t frame_id, uint8_t frame_type, uint8_t cmd_id,
			 uint8_t seq, const uint8_t *payload, uint16_t payload_len,
			 uint8_t *out, size_t out_size, size_t *encoded_len);
int mist_protocol_decode(const struct frame_codec_frame *frame,
			 struct mist_protocol_packet *packet);

#endif /* PROTOCOLS_MIST_MIST_PROTOCOL_H_ */

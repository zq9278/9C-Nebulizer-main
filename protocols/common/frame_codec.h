#ifndef PROTOCOLS_COMMON_FRAME_CODEC_H_
#define PROTOCOLS_COMMON_FRAME_CODEC_H_

#include <stddef.h>
#include <stdint.h>

#define FRAME_CODEC_PREAMBLE0 0xAAU
#define FRAME_CODEC_PREAMBLE1 0x55U
#define FRAME_CODEC_TRAILER0  0x0DU
#define FRAME_CODEC_TRAILER1  0x0AU
#define FRAME_CODEC_MAX_PAYLOAD 128U
#define FRAME_CODEC_OVERHEAD  11U
#define FRAME_CODEC_MAX_FRAME (FRAME_CODEC_MAX_PAYLOAD + FRAME_CODEC_OVERHEAD)

struct frame_codec_frame {
	uint16_t frame_id;
	uint8_t type;
	uint16_t payload_len;
	uint8_t payload[FRAME_CODEC_MAX_PAYLOAD];
};

int frame_codec_encode(uint16_t frame_id, uint8_t type, const uint8_t *payload,
		       uint16_t payload_len, uint8_t *out, size_t out_size,
		       size_t *encoded_len);
int frame_codec_decode(const uint8_t *frame, size_t frame_len,
		       struct frame_codec_frame *decoded);

#endif /* PROTOCOLS_COMMON_FRAME_CODEC_H_ */

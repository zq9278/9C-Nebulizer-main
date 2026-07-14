#ifndef PROTOCOLS_COMMON_RING_FRAME_PARSER_H_
#define PROTOCOLS_COMMON_RING_FRAME_PARSER_H_

#include <stddef.h>
#include <stdint.h>

#include "frame_codec.h"

typedef void (*ring_frame_parser_cb_t)(const struct frame_codec_frame *frame,
				       void *user_data);

struct ring_frame_parser {
	uint8_t buffer[FRAME_CODEC_MAX_FRAME];
	size_t len;
	size_t expected_len;
	ring_frame_parser_cb_t callback;
	void *user_data;
};

void ring_frame_parser_init(struct ring_frame_parser *parser,
			    ring_frame_parser_cb_t callback, void *user_data);
void ring_frame_parser_feed(struct ring_frame_parser *parser,
			    const uint8_t *data, size_t len);

#endif /* PROTOCOLS_COMMON_RING_FRAME_PARSER_H_ */

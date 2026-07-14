#include "ring_frame_parser.h"

#include <string.h>

void ring_frame_parser_init(struct ring_frame_parser *parser,
			    ring_frame_parser_cb_t callback, void *user_data)
{
	parser->len = 0U;
	parser->expected_len = 0U;
	parser->callback = callback;
	parser->user_data = user_data;
}

static void ring_frame_parser_reset(struct ring_frame_parser *parser)
{
	parser->len = 0U;
	parser->expected_len = 0U;
}

void ring_frame_parser_feed(struct ring_frame_parser *parser,
			    const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; ++i) {
		uint8_t byte = data[i];

		if ((parser->len == 0U) && (byte != FRAME_CODEC_PREAMBLE0)) {
			continue;
		}

		if ((parser->len == 1U) && (byte != FRAME_CODEC_PREAMBLE1)) {
			parser->len = (byte == FRAME_CODEC_PREAMBLE0) ? 1U : 0U;
			parser->buffer[0] = FRAME_CODEC_PREAMBLE0;
			continue;
		}

		if (parser->len < sizeof(parser->buffer)) {
			parser->buffer[parser->len++] = byte;
		} else {
			ring_frame_parser_reset(parser);
			continue;
		}

		if (parser->len == 7U) {
			uint16_t payload_len = (uint16_t)parser->buffer[5] |
					     ((uint16_t)parser->buffer[6] << 8);
			if (payload_len > FRAME_CODEC_MAX_PAYLOAD) {
				ring_frame_parser_reset(parser);
				continue;
			}

			parser->expected_len = FRAME_CODEC_OVERHEAD + payload_len;
		}

		if ((parser->expected_len > 0U) && (parser->len == parser->expected_len)) {
			struct frame_codec_frame decoded;

			if ((frame_codec_decode(parser->buffer, parser->len, &decoded) == 0) &&
			    (parser->callback != NULL)) {
				parser->callback(&decoded, parser->user_data);
			}

			ring_frame_parser_reset(parser);
		}
	}
}

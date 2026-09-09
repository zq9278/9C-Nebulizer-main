#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <protocols/common/frame_codec.h>
#include <protocols/common/ring_frame_parser.h>

static unsigned received;
static void on_frame(const struct frame_codec_frame *frame, void *context)
{
    assert(context == &received);
    assert(frame->frame_id == 0x1234);
    assert(frame->payload_len == 3);
    assert(memcmp(frame->payload, "ABC", 3) == 0);
    received++;
}

int main(void)
{
    struct ring_frame_parser parser;
    uint8_t encoded[FRAME_CODEC_MAX_FRAME];
    size_t length;
    assert(frame_codec_encode(0x1234, 1, (const uint8_t *)"ABC", 3,
        encoded, sizeof(encoded), &length) == 0);
    ring_frame_parser_init(&parser, on_frame, &received);
    const uint8_t noise[] = {0, 0x55, 0xaa, 0xaa};
    ring_frame_parser_feed(&parser, noise, sizeof(noise));
    for (size_t i = 0; i < length; ++i) ring_frame_parser_feed(&parser, &encoded[i], 1);
    assert(received == 1);
    encoded[7] ^= 1; /* Corrupt payload; CRC must reject the whole frame. */
    ring_frame_parser_feed(&parser, encoded, length);
    assert(received == 1);
    encoded[7] ^= 1;
    ring_frame_parser_feed(&parser, encoded, 6);
    assert(received == 1);
    ring_frame_parser_feed(&parser, encoded + 6, length - 6);
    assert(received == 2);
    const uint8_t oversize[] = {0xaa, 0x55, 0, 0, 1, 0xff, 0xff};
    ring_frame_parser_feed(&parser, oversize, sizeof(oversize));
    ring_frame_parser_feed(&parser, encoded, length);
    assert(received == 3);
    puts("test_stream_parser: PASS (fragmentation, noise, CRC, oversize recovery)");
    return 0;
}

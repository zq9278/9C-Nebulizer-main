#include <assert.h>
#include <string.h>
#include <stdio.h>

#include <protocols/common/frame_codec.h>

static void frame_codec_test_encode_decode_roundtrip(void)
{
	uint8_t encoded[FRAME_CODEC_MAX_FRAME];
	size_t encoded_len;
	struct frame_codec_frame decoded;
	static const uint8_t payload[] = { 0x10, 0x11, 0x12 };

	assert((frame_codec_encode(0x1234, 0x01, payload, sizeof(payload),
				      encoded, sizeof(encoded), &encoded_len)) == 0);
	assert((frame_codec_decode(encoded, encoded_len, &decoded)) == 0);
	assert((decoded.frame_id) == (0x1234));
	assert((decoded.type) == (0x01));
	assert((decoded.payload_len) == (sizeof(payload)));
	assert(memcmp(decoded.payload, payload, sizeof(payload)) == 0);
}



int main(void)
{
    frame_codec_test_encode_decode_roundtrip();
    puts("test_frame_codec: PASS");
    return 0;
}

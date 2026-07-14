#include <zephyr/ztest.h>

#include <protocols/common/frame_codec.h>

ZTEST(frame_codec, test_encode_decode_roundtrip)
{
	uint8_t encoded[FRAME_CODEC_MAX_FRAME];
	size_t encoded_len;
	struct frame_codec_frame decoded;
	static const uint8_t payload[] = { 0x10, 0x11, 0x12 };

	zassert_ok(frame_codec_encode(0x1234, 0x01, payload, sizeof(payload),
				      encoded, sizeof(encoded), &encoded_len), NULL);
	zassert_ok(frame_codec_decode(encoded, encoded_len, &decoded), NULL);
	zassert_equal(decoded.frame_id, 0x1234, NULL);
	zassert_equal(decoded.type, 0x01, NULL);
	zassert_equal(decoded.payload_len, sizeof(payload), NULL);
	zassert_mem_equal(decoded.payload, payload, sizeof(payload), NULL);
}

ZTEST_SUITE(frame_codec, NULL, NULL, NULL, NULL, NULL);

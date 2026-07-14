#include "frame_codec.h"

#include <errno.h>
#include <string.h>

#include "crc16_modbus.h"

int frame_codec_encode(uint16_t frame_id, uint8_t type, const uint8_t *payload,
		       uint16_t payload_len, uint8_t *out, size_t out_size,
		       size_t *encoded_len)
{
	uint16_t crc;
	size_t total_len = FRAME_CODEC_OVERHEAD + payload_len;

	if ((out == NULL) || (encoded_len == NULL) || (payload_len > FRAME_CODEC_MAX_PAYLOAD)) {
		return -EINVAL;
	}

	if (out_size < total_len) {
		return -ENOSPC;
	}

	out[0] = FRAME_CODEC_PREAMBLE0;
	out[1] = FRAME_CODEC_PREAMBLE1;
	out[2] = (uint8_t)(frame_id & 0xFFU);
	out[3] = (uint8_t)(frame_id >> 8);
	out[4] = type;
	out[5] = (uint8_t)(payload_len & 0xFFU);
	out[6] = (uint8_t)(payload_len >> 8);

	if ((payload_len > 0U) && (payload != NULL)) {
		memcpy(&out[7], payload, payload_len);
	}

	/*
	 * CRC16-Modbus covers: frame_id(2) + type(1) + len(2) + payload(len).
	 * It excludes the AA 55 preamble and 0D 0A trailer.
	 */
	crc = crc16_modbus_compute(&out[2], 5U + payload_len);
	out[7U + payload_len] = (uint8_t)(crc & 0xFFU);
	out[8U + payload_len] = (uint8_t)(crc >> 8);
	out[9U + payload_len] = FRAME_CODEC_TRAILER0;
	out[10U + payload_len] = FRAME_CODEC_TRAILER1;

	*encoded_len = total_len;
	return 0;
}

int frame_codec_decode(const uint8_t *frame, size_t frame_len,
		       struct frame_codec_frame *decoded)
{
	uint16_t payload_len;
	uint16_t crc_actual;
	uint16_t crc_expected;

	if ((frame == NULL) || (decoded == NULL) || (frame_len < FRAME_CODEC_OVERHEAD)) {
		return -EINVAL;
	}

	if ((frame[0] != FRAME_CODEC_PREAMBLE0) || (frame[1] != FRAME_CODEC_PREAMBLE1)) {
		return -EBADMSG;
	}

	payload_len = (uint16_t)frame[5] | ((uint16_t)frame[6] << 8);
	if ((payload_len > FRAME_CODEC_MAX_PAYLOAD) ||
	    (frame_len != (size_t)(FRAME_CODEC_OVERHEAD + payload_len))) {
		return -EMSGSIZE;
	}

	if ((frame[frame_len - 2U] != FRAME_CODEC_TRAILER0) ||
	    (frame[frame_len - 1U] != FRAME_CODEC_TRAILER1)) {
		return -EBADMSG;
	}

	crc_actual = (uint16_t)frame[7U + payload_len] | ((uint16_t)frame[8U + payload_len] << 8);
	crc_expected = crc16_modbus_compute(&frame[2], 5U + payload_len);
	if (crc_actual != crc_expected) {
		return -EBADMSG;
	}

	decoded->frame_id = (uint16_t)frame[2] | ((uint16_t)frame[3] << 8);
	decoded->type = frame[4];
	decoded->payload_len = payload_len;
	if (payload_len > 0U) {
		memcpy(decoded->payload, &frame[7], payload_len);
	}

	return 0;
}

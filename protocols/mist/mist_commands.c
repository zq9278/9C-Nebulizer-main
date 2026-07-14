#include "mist_commands.h"

#include <errno.h>

int mist_commands_level_payload(uint8_t level, uint8_t *payload, uint16_t *payload_len)
{
	if ((payload == NULL) || (payload_len == NULL)) {
		return -EINVAL;
	}

	payload[0] = level;
	*payload_len = 1U;
	return 0;
}

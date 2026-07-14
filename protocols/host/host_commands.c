#include "host_commands.h"

#include <errno.h>
#include <string.h>

int host_commands_build_event(uint8_t cmd_id, uint16_t frame_id, const uint8_t *payload,
			      uint16_t payload_len, app_event_t *evt)
{
	if ((evt == NULL) || (payload_len > sizeof(evt->data.host_cmd.data))) {
		return -EINVAL;
	}

	memset(evt, 0, sizeof(*evt));
	evt->type = APP_EVT_HOST_CMD;
	evt->data.host_cmd.command_id = cmd_id;
	evt->data.host_cmd.frame_id = frame_id;
	evt->data.host_cmd.data_len = payload_len;
	if ((payload != NULL) && (payload_len > 0U)) {
		memcpy(evt->data.host_cmd.data, payload, payload_len);
	}

	return 0;
}

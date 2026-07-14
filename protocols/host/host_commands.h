#ifndef PROTOCOLS_HOST_HOST_COMMANDS_H_
#define PROTOCOLS_HOST_HOST_COMMANDS_H_

#include <nebulizer/app_events.h>

int host_commands_build_event(uint8_t cmd_id, uint16_t frame_id, const uint8_t *payload,
			      uint16_t payload_len, app_event_t *evt);

#endif /* PROTOCOLS_HOST_HOST_COMMANDS_H_ */

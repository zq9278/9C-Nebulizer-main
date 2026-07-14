#ifndef PROTOCOLS_MIST_MIST_COMMANDS_H_
#define PROTOCOLS_MIST_MIST_COMMANDS_H_

#include <stdint.h>

int mist_commands_level_payload(uint8_t level, uint8_t *payload, uint16_t *payload_len);

#endif /* PROTOCOLS_MIST_MIST_COMMANDS_H_ */

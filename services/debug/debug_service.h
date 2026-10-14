#ifndef NEBULIZER_DEBUG_SERVICE_H
#define NEBULIZER_DEBUG_SERVICE_H
#include <nebulizer/app_events.h>
#include <stddef.h>

/* Fixed-width little-endian SWD ABI. Never expose compiler-dependent structs. */
#define DEBUG_MAGIC 0x3144424eU /* NBD1 */
#define DEBUG_VERSION 1U
#define DEBUG_SNAPSHOT_SIZE 320U
#define DEBUG_EVENT_COUNT 16U
#define DEBUG_CMD_ACQUIRE 0xe0U
#define DEBUG_CMD_RELEASE 0xe1U
#define DEBUG_ERR_OWNER 0x70U
#define DEBUG_ERR_FORMAT 0x71U
#define DEBUG_ERR_SESSION 0x72U
#define DEBUG_LEASE_MS 3000U

typedef struct {
    uint32_t sequence, uptime_ms, type, code, state, remaining_sec;
    uint32_t outlet_deci_c, kettle_deci_c;
} debug_event_t;
typedef struct {
    uint32_t magic, version, size, capabilities;
    uint32_t snapshot_sequence, uptime_ms, owner_session, owner_alive;
    uint32_t request_sequence, request_session, command, payload_length;
    uint8_t payload[16];
    uint32_t heartbeat_session, heartbeat_counter;
    uint32_t response_sequence, response_session, response_command, response_error;
    uint32_t snapshot_length, event_sequence;
    uint8_t snapshot[DEBUG_SNAPSHOT_SIZE];
    debug_event_t events[DEBUG_EVENT_COUNT];
} debug_mailbox_t;
extern volatile debug_mailbox_t g_nebulizer_debug;

void debug_service_init(void);
void debug_service_publish(void); /* Control task, one snapshot writer */
bool debug_service_take(host_cmd_event_t *cmd); /* AppTask only */
void debug_service_complete(const host_cmd_event_t *cmd, uint8_t error);
bool debug_service_owned(void);
bool debug_service_is_owner(uint32_t session);
bool debug_service_alive(uint32_t now);
bool debug_service_expired(void);
uint8_t debug_service_acquire(uint32_t session);
void debug_service_release(void);
uint8_t debug_service_authorize(const host_cmd_event_t *cmd);
void debug_service_event(uint32_t type, uint32_t code, const telemetry_status_t *status);
#endif

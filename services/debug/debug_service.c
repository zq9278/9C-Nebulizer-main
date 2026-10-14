#include "debug_service.h"
#include <string.h>
#include <platform/runtime.h>
#include <protocols/host/host_protocol.h>
#include <nebulizer/protocol_ids.h>
#include <src/app/app_context.h>

_Static_assert(offsetof(debug_mailbox_t, payload) == 48, "debug ABI payload");
_Static_assert(offsetof(debug_mailbox_t, snapshot) == 96, "debug ABI snapshot");
_Static_assert(offsetof(debug_mailbox_t, events) == 416, "debug ABI events");
_Static_assert(sizeof(debug_mailbox_t) == 928, "debug ABI size");
volatile debug_mailbox_t g_nebulizer_debug;
static volatile uint32_t owner, heartbeat_seen, heartbeat_at;
static uint32_t consumed_sequence;
static bool lease_expired;

/* Cortex-M0+ has no data cache. Compiler + CPU barriers order publication
 * against the SWD bus master; volatile alone is not a transaction protocol. */
#ifdef DEBUG_SERVICE_TEST
#define BARRIER() __asm__ volatile("" ::: "memory")
#else
#define BARRIER() __DMB()
#endif

void debug_service_init(void)
{
    memset((void *)&g_nebulizer_debug, 0, sizeof(g_nebulizer_debug));
    owner = heartbeat_seen = heartbeat_at = consumed_sequence = 0;
    lease_expired = false;
    g_nebulizer_debug.version = DEBUG_VERSION;
    g_nebulizer_debug.size = sizeof(g_nebulizer_debug);
    g_nebulizer_debug.capabilities = 7U; /* snapshot, command mailbox, events */
    BARRIER();
    g_nebulizer_debug.magic = DEBUG_MAGIC;
}

bool debug_service_owned(void) { return owner != 0; }
bool debug_service_is_owner(uint32_t session) { return session && owner == session; }
bool debug_service_alive(uint32_t now)
{
#ifndef DEBUG_SERVICE_TEST
    uint32_t key = runtime_irq_save();
#endif
    /* A caller may have sampled now before a preempting task refreshed the
     * lease; a small negative delta is not an expiry (also handles tick wrap). */
    if (owner && (int32_t)(now - heartbeat_at) >= (int32_t)DEBUG_LEASE_MS) lease_expired = true;
    uint32_t counter = g_nebulizer_debug.heartbeat_counter;
    BARRIER();
    if (!lease_expired && owner && g_nebulizer_debug.heartbeat_session == owner && counter != heartbeat_seen) {
        heartbeat_seen = counter;
        heartbeat_at = now;
    }
    bool alive = owner && !lease_expired;
#ifndef DEBUG_SERVICE_TEST
    runtime_irq_restore(key);
#endif
    return alive;
}
bool debug_service_expired(void)
{
    return debug_service_owned() && !debug_service_alive(runtime_now_ms());
}
uint8_t debug_service_acquire(uint32_t session)
{
    if (!session || g_nebulizer_debug.heartbeat_session != session ||
        !g_nebulizer_debug.heartbeat_counter) return DEBUG_ERR_SESSION;
    if (owner && owner != session) return DEBUG_ERR_OWNER;
    if (owner && lease_expired) return DEBUG_ERR_SESSION;
    heartbeat_seen = g_nebulizer_debug.heartbeat_counter;
    heartbeat_at = runtime_now_ms();
    lease_expired = false;
    BARRIER();
    owner = session;
    return 0;
}
void debug_service_release(void) { owner = 0; }

static bool readonly_command(uint8_t cmd)
{
    return cmd == HOST_CMD_GET_STATUS || cmd == HOST_CMD_GET_CONFIG ||
        cmd == HOST_CMD_GET_RUNTIME || cmd == HOST_CMD_GET_PID ||
        cmd == HOST_CMD_GET_PREHEAT_PID || cmd == HOST_CMD_GET_FAN_PID ||
        cmd == HOST_CMD_GET_MAINTENANCE || cmd == HOST_CMD_HEARTBEAT;
}
uint8_t debug_service_authorize(const host_cmd_event_t *cmd)
{
    if (readonly_command(cmd->command_id)) return 0;
    /* Either control surface may always stop outputs or cancel keep warm. */
    if (cmd->command_id == HOST_CMD_STOP || cmd->command_id == HOST_CMD_PAUSE ||
        cmd->command_id == HOST_CMD_EXIT_MAINTENANCE ||
        (cmd->command_id == HOST_CMD_SET_KEEP_WARM && cmd->data_len == 1 && !cmd->data[0])) return 0;
    if (!cmd->debug_session) return owner ? DEBUG_ERR_OWNER : 0;
    return owner == cmd->debug_session && debug_service_alive(runtime_now_ms()) ? 0 : DEBUG_ERR_OWNER;
}

bool debug_service_take(host_cmd_event_t *cmd)
{
    uint32_t seq = g_nebulizer_debug.request_sequence;
    if (!seq || seq == consumed_sequence) return false;
    BARRIER();
    memset(cmd, 0, sizeof(*cmd));
    cmd->debug_sequence = seq;
    cmd->debug_session = g_nebulizer_debug.request_session;
    uint32_t id = g_nebulizer_debug.command, length = g_nebulizer_debug.payload_length;
    cmd->command_id = (uint8_t)id;
    cmd->data_len = (uint16_t)length;
    for (size_t i = 0; i < sizeof(cmd->data); ++i) cmd->data[i] = g_nebulizer_debug.payload[i];
    BARRIER();
    if (seq != g_nebulizer_debug.request_sequence) return false;
    consumed_sequence = seq;
    if (!cmd->debug_session || id > 255U || length > sizeof(cmd->data)) {
        debug_service_complete(cmd, DEBUG_ERR_FORMAT);
        return false;
    }
    return true;
}
void debug_service_complete(const host_cmd_event_t *cmd, uint8_t error)
{
    g_nebulizer_debug.response_sequence = 0;
    BARRIER();
    g_nebulizer_debug.response_session = cmd->debug_session;
    g_nebulizer_debug.response_command = cmd->command_id;
    g_nebulizer_debug.response_error = error;
    BARRIER();
    g_nebulizer_debug.response_sequence = cmd->debug_sequence;
}

void debug_service_publish(void)
{
    telemetry_status_t status;
    uint8_t bytes[DEBUG_SNAPSHOT_SIZE];
    size_t used = 0, count = 0;
    app_context_get_status(&status);
    bool alive = debug_service_alive(runtime_now_ms());
#define ENCODE(fn, value) do { \
    if (fn(0, value, bytes + used, sizeof(bytes) - used, &count) != 0) return; \
    used += count; \
} while (0)
    ENCODE(host_protocol_encode_status, &status);
    ENCODE(host_protocol_encode_config, &status.config);
    ENCODE(host_protocol_encode_pid, &status.heat_pid);
    ENCODE(host_protocol_encode_preheat_pid, &status.preheat_pid);
    ENCODE(host_protocol_encode_fan_pid, &status.fan_pid);
    ENCODE(host_protocol_encode_runtime, &status);
    ENCODE(host_protocol_encode_maintenance, &status.maintenance);
#undef ENCODE
    ++g_nebulizer_debug.snapshot_sequence;
    BARRIER();
    g_nebulizer_debug.uptime_ms = runtime_now_ms();
    g_nebulizer_debug.owner_session = owner;
    g_nebulizer_debug.owner_alive = alive;
    g_nebulizer_debug.snapshot_length = used;
    for (size_t i = 0; i < used; ++i) g_nebulizer_debug.snapshot[i] = bytes[i];
    BARRIER();
    ++g_nebulizer_debug.snapshot_sequence;
}

void debug_service_event(uint32_t type, uint32_t code, const telemetry_status_t *status)
{
    /* AppTask is the only event producer. Reader never changes ring indices. */
    uint32_t seq = g_nebulizer_debug.event_sequence + 1U;
    if (!seq) seq = 1;
    volatile debug_event_t *event = &g_nebulizer_debug.events[(seq - 1U) % DEBUG_EVENT_COUNT];
    event->sequence = 0;
    BARRIER();
    event->uptime_ms = runtime_now_ms();
    event->type = type;
    event->code = code;
    event->state = status->state;
    event->remaining_sec = status->remaining_sec;
    event->outlet_deci_c = (uint32_t)(int32_t)status->sensors.ntc_deci_c[0];
    event->kettle_deci_c = (uint32_t)(int32_t)status->sensors.ntc_deci_c[3];
    BARRIER();
    event->sequence = seq;
    BARRIER();
    g_nebulizer_debug.event_sequence = seq;
}

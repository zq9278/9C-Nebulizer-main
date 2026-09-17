#include <assert.h>
#include <stdio.h>
#define TEST_RUNTIME_CLOCK
#include <src/safety/safety_service.c>
#include <src/state_machine/treatment_sm.h>

uint32_t test_runtime_ms;
static telemetry_status_t current;
static bool heartbeat;
static unsigned stops;
void app_context_get_status(telemetry_status_t *s) { *s = current; }
void app_context_set_fault(fault_code_t f) { current.fault = f; }
bool app_context_heartbeat_alive(uint32_t ms) { (void)ms; return heartbeat; }
int heat_control_stop(void) { ++stops; return 0; }
int fan_control_stop(void) { return 0; }
int mist_service_request_stop(void) { return 0; }
void platform_log(const char *level, const char *format, ...) { (void)level; (void)format; }

static void reset(void)
{
    memset(&safety_cover_state, 0, sizeof(safety_cover_state));
    memset(&safety_log_state, 0, sizeof(safety_log_state));
    memset(&current, 0, sizeof(current));
    safety_service_init();
    current.state = TREATMENT_STATE_RUNNING_HOT;
    current.config.mode = TREATMENT_MODE_HOT;
    current.remaining_sec = 123;
    current.sensors.cover_closed = true;
    current.sensors.ntc_deci_c[BOARD_NTC_OUTLET1] = 320;
    current.mist.online = true;
    heartbeat = true;
    stops = 0;
    test_runtime_ms = 0;
    assert(!safety_service_poll().resume_requested);
}

static void cover(bool closed)
{
    current.sensors.cover_closed = closed;
    (void)safety_service_poll();
    test_runtime_ms += APP_COVER_DEBOUNCE_MS;
}

static void test_refill_retries_and_keeps_time(void)
{
    reset();
    current.mist.low_water = true;
    current.mist.safety_locked = true;
    assert(safety_service_poll().pause_requested);
    assert(stops > 0);
    current.state = TREATMENT_STATE_PAUSED;
    cover(false);
    current.mist.low_water = false;
    current.mist.safety_locked = false;
    assert(!safety_service_poll().resume_requested);
    cover(true);
    /* Simulate an event not being delivered/accepted: recovery must retry. */
    assert(safety_service_poll().resume_requested);
    assert(safety_service_poll().resume_requested);
    current.mist.safety_locked = true;
    assert(!safety_service_poll().resume_requested);
    current.mist.safety_locked = false;
    heartbeat = false;
    assert(!safety_service_poll().resume_requested);
    heartbeat = true;
    current.mist.online = false;
    assert(!safety_service_poll().resume_requested);
    current.mist.online = true;
    assert(safety_service_poll().resume_requested);
    app_event_t evt = { .type = APP_EVT_RESUME };
    bool accepted;
    current.state = treatment_sm_handle_event(current.state, &evt, &current, &accepted);
    assert(accepted && current.state == TREATMENT_STATE_RUNNING_HOT);
    assert(current.remaining_sec == 123);
    safety_service_acknowledge_resume();
    current.state = TREATMENT_STATE_PAUSED; /* A later manual pause must stay paused. */
    assert(!safety_service_poll().resume_requested);
}

static void test_cover_and_low_water_together(void)
{
    reset();
    cover(false);
    current.mist.low_water = true;
    assert(safety_service_poll().pause_requested);
    assert(safety_cover_state.mist_low_water_pause_latched);
    current.state = TREATMENT_STATE_PAUSED;
    cover(true);
    assert(!safety_service_poll().resume_requested);
    current.mist.low_water = false;
    assert(safety_service_poll().resume_requested);
    current.sensors.ntc_open[BOARD_NTC_KETTLE] = true;
    struct safety_result result = safety_service_poll();
    assert(!result.resume_requested && result.fault == FAULT_SENSOR_NTC_OPEN);
}

static void test_cover_alone_requires_manual_resume(void)
{
    reset();
    cover(false);
    assert(safety_service_poll().pause_requested);
    current.state = TREATMENT_STATE_PAUSED;
    cover(true);
    assert(!safety_service_poll().resume_requested);
    assert(!safety_service_poll().resume_requested);
    assert(safety_cover_state.cover_pause_latched);

    app_event_t evt = { .type = APP_EVT_RESUME };
    bool accepted;
    current.state = treatment_sm_handle_event(current.state, &evt, &current, &accepted);
    assert(accepted && current.state == TREATMENT_STATE_RUNNING_HOT);
    assert(current.remaining_sec == 123);
    safety_service_acknowledge_resume();
    assert(!safety_cover_state.cover_pause_latched);
}

int main(void)
{
    test_refill_retries_and_keeps_time();
    test_cover_and_low_water_together();
    test_cover_alone_requires_manual_resume();
    puts("test_safety_recovery: PASS (refill auto-resume, cover manual-resume, debounce, interlocks)");
    return 0;
}

/* Execute the real AppTask command path with simulated actuators and clock. */
#include <assert.h>
#include <stdio.h>
#include <platform/util.h>
#define TEST_RUNTIME_CLOCK
#define DEBUG_SERVICE_TEST
#include <platform/runtime.h>
typedef uint32_t StackType_t;
typedef unsigned StaticTask_t;
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
static inline uint32_t xTaskGetTickCount(void) { return test_runtime_ms; }
static inline void vTaskDelayUntil(TickType_t *last, TickType_t delay) { *last += delay; }
static inline void *xTaskCreateStatic(void (*fn)(void *), const char *name, size_t size,
    void *arg, unsigned priority, StackType_t *stack, StaticTask_t *control)
{ (void)fn; (void)name; (void)size; (void)arg; (void)priority; (void)stack; return control; }
#include <src/app/app_context.c>
#include <services/debug/debug_service.c>
#include <src/app/app_tasks.c>

uint32_t test_runtime_ms;
static unsigned serial_acks, serial_reads, saves, starts;
static uint8_t serial_error;
static uint16_t heat_output;
static pid_params_t actual_heat, actual_preheat, actual_fan;
static bool reset_drops_cover;
void platform_log(const char *level, const char *format, ...) { (void)level; (void)format; }
int host_comm_service_send_ack(uint16_t frame, uint8_t cmd, bool ok, uint8_t error)
{ (void)frame; (void)cmd; (void)ok; serial_acks++; serial_error=error; return 0; }
#define SEND(name,type) int host_comm_service_send_##name(const type *value) { (void)value; serial_reads++; return 0; }
SEND(status,telemetry_status_t) SEND(runtime,telemetry_status_t) SEND(config,treatment_config_t)
SEND(pid,pid_params_t) SEND(preheat_pid,pid_params_t) SEND(fan_pid,pid_params_t)
SEND(maintenance,maintenance_control_t)
int host_comm_service_send_treatment_event(uint8_t id,const telemetry_status_t *s) { (void)id;(void)s;return 0; }
int fan_control_set_level(air_level_t level) { (void)level;return 0; }
int fan_control_stop(void) {return 0;}
int mist_service_set_desired(bool run,mist_level_t level) { (void)run;(void)level;return 0; }
int mist_service_request_stop(void) { return 0; }
int mist_service_trigger_otp_reset(void)
{ starts++; if (reset_drops_cover) app_ctx.status.sensors.cover_closed=false; return 0; }
int heat_control_stop(void) { heat_output=0; return 0; }
int heat_control_manual_stop(void) { heat_output=0; return 0; }
int heat_control_manual_set(uint16_t output) { heat_output=output; return 0; }
#define PID(name,var) int name##_set_pid(const pid_params_t *p) {var=*p;return 0;} \
    void name##_get_pid(pid_params_t *p) {*p=var;}
PID(heat_control,actual_heat) PID(fan_control,actual_fan)
int heat_control_set_preheat_pid(const pid_params_t *p) { actual_preheat=*p; return 0; }
void heat_control_get_preheat_pid(pid_params_t *p) { *p=actual_preheat; }
int settings_store_save_delayed(const treatment_config_t *c) { (void)c;saves++;return 0; }
#define SAVE(name) int settings_store_save_##name##_delayed(const pid_params_t *p) {(void)p;saves++;return 0;}
SAVE(pid) SAVE(preheat_pid) SAVE(fan_pid)
void safety_service_enter_safe_state(fault_code_t f) { (void)f;heat_output=0; }
void safety_service_acknowledge_resume(void) {}
bool fault_manager_clear(void) { return app_context_clear_fault(); }
void fault_manager_raise(fault_code_t f) { app_context_set_fault(f); }
int app_event_submit(const app_event_t *e) { (void)e;return 0; }
int app_event_wait(app_event_t *e,TickType_t timeout) { (void)e;(void)timeout;return -EAGAIN; }
int sensor_service_sample(sensor_snapshot_t *s) { (void)s;return -EAGAIN; }
void heat_control_get_diag(heat_control_diag_t *d) { memset(d,0,sizeof(*d)); }
void fan_control_get_diag(fan_control_diag_t *d) { memset(d,0,sizeof(*d)); }
int fan_control_step(const telemetry_status_t *s) { (void)s;return 0; }
fault_code_t heat_control_step(const telemetry_status_t *s) { (void)s;return FAULT_NONE; }
fault_code_t heat_control_keep_warm_step(const telemetry_status_t *s) { (void)s;return FAULT_NONE; }
struct safety_result safety_service_poll(void) { return (struct safety_result){0}; }
int telemetry_service_publish_now(void) { return 0; }
void mist_service_process_task(void) {}
void host_comm_rx_task(void) {}

static void reset(void)
{
    test_runtime_ms=100;
    debug_service_init(); app_context_init();
    app_ctx.status.state=TREATMENT_STATE_READY;
    app_ctx.status.sensors.cover_closed=true;
    app_ctx.status.sensors.ntc_deci_c[0]=350;
    settings_defaults_get_pid(&app_ctx.status.heat_pid);
    settings_defaults_get_preheat_pid(&app_ctx.status.preheat_pid);
    settings_defaults_get_fan_pid(&app_ctx.status.fan_pid);
    app_ctx.status.mist.online=true;
    memset(&maintenance_ctx,0,sizeof(maintenance_ctx));
    serial_acks=serial_reads=saves=starts=heat_output=0;
    reset_drops_cover=false;
}
static void request(uint32_t seq,uint32_t session,uint32_t cmd,const void *payload,uint32_t length)
{
    g_nebulizer_debug.request_session=session;
    g_nebulizer_debug.command=cmd;
    g_nebulizer_debug.payload_length=length;
    memset((void *)g_nebulizer_debug.payload,0,16);
    if(payload && length<=16) memcpy((void *)g_nebulizer_debug.payload,payload,length);
    g_nebulizer_debug.request_sequence=seq;
    app_debug_poll();
}
static void acquire(void)
{
    g_nebulizer_debug.heartbeat_session=123;
    g_nebulizer_debug.heartbeat_counter=1;
    request(1,123,DEBUG_CMD_ACQUIRE,NULL,0);
    assert(g_nebulizer_debug.response_error==0 && debug_service_owned());
}
static void test_commands_and_routing(void)
{
    reset(); acquire();
    uint16_t temp=420;
    request(2,123,HOST_CMD_SET_TARGET_TEMP,&temp,2);
    assert(app_ctx.status.config.target_temp_deci_c==420 && saves==1);
    assert(!serial_acks);
    request(2,123,HOST_CMD_SET_TARGET_TEMP,&temp,2); assert(saves==1);
    host_cmd_event_t serial={.command_id=HOST_CMD_SET_TARGET_TEMP,.data={0x90,1},.data_len=2};
    app_handle_host_command(&serial); assert(serial_error==DEBUG_ERR_OWNER);
    request(3,456,HOST_CMD_SET_TARGET_TEMP,&temp,2); assert(g_nebulizer_debug.response_error==DEBUG_ERR_OWNER);
    request(4,123,HOST_CMD_GET_PID,NULL,0); assert(!serial_reads);
    request(5,123,HOST_CMD_START,NULL,0); assert(app_ctx.status.state==TREATMENT_STATE_RUNNING_HOT && starts==1);
    request(5,123,HOST_CMD_START,NULL,0); assert(starts==1);
    request(6,123,HOST_CMD_PAUSE,NULL,0); assert(app_ctx.status.state==TREATMENT_STATE_PAUSED);
    request(7,123,HOST_CMD_RESUME,NULL,0); assert(app_ctx.status.state==TREATMENT_STATE_RUNNING_HOT);
    serial.command_id=HOST_CMD_STOP; serial.data_len=0;
    app_handle_host_command(&serial); assert(!serial_error && app_ctx.status.state==TREATMENT_STATE_READY);
    request(8,123,DEBUG_CMD_RELEASE,NULL,0); assert(!debug_service_owned());
    serial.command_id=HOST_CMD_SET_TARGET_TEMP;serial.data_len=2;
    app_handle_host_command(&serial);assert(!serial_error && saves==2);
}
static void test_lease_and_maintenance(void)
{
    reset(); acquire();
    request(2,123,HOST_CMD_ENTER_MAINTENANCE,NULL,0);
    uint16_t output=250;request(3,123,HOST_CMD_MANUAL_SET_HEAT,&output,2);
    assert(heat_output==250 && maintenance_ctx.active);
    test_runtime_ms+=1000;g_nebulizer_debug.heartbeat_counter++;
    assert(debug_service_alive(test_runtime_ms));
    assert(debug_service_alive(test_runtime_ms-1)); /* preempted clock sample */
    test_runtime_ms+=DEBUG_LEASE_MS;
    assert(debug_service_expired());
    g_nebulizer_debug.heartbeat_counter++; /* late heartbeat cannot revive lease */
    assert(debug_service_expired());
    app_debug_poll();
    assert(!debug_service_owned() && !maintenance_ctx.active && !heat_output);
    app_run_control_phase();assert(!heat_output);
    request(4,123,HOST_CMD_MANUAL_SET_HEAT,&output,2);
    assert(g_nebulizer_debug.response_error==DEBUG_ERR_OWNER && !heat_output);
    reset(); acquire();request(2,123,HOST_CMD_START,NULL,0);
    test_runtime_ms+=DEBUG_LEASE_MS;app_debug_poll();
    assert(app_ctx.status.state==TREATMENT_STATE_PAUSED && debug_release_hold);
    reset();acquire();app_ctx.status.state=TREATMENT_STATE_KEEP_WARM;
    test_runtime_ms+=DEBUG_LEASE_MS;app_debug_poll();
    assert(app_ctx.status.state==TREATMENT_STATE_READY && !heat_output);
    reset();test_runtime_ms=UINT32_MAX-100;acquire();
    test_runtime_ms=100;g_nebulizer_debug.heartbeat_counter++;
    assert(debug_service_alive(test_runtime_ms));
}
static void test_all_parameter_commands(void)
{
    reset();acquire();
    uint32_t seq=1;
    const uint8_t reads[]={HOST_CMD_GET_STATUS,HOST_CMD_GET_CONFIG,HOST_CMD_GET_RUNTIME,
        HOST_CMD_GET_PID,HOST_CMD_GET_PREHEAT_PID,HOST_CMD_GET_FAN_PID,HOST_CMD_GET_MAINTENANCE,HOST_CMD_HEARTBEAT};
    for(size_t i=0;i<sizeof(reads);i++) {
        request(++seq,123,reads[i],NULL,0);assert(!g_nebulizer_debug.response_error);
    }
    assert(!serial_reads && !serial_acks);
    uint8_t one=1,two=2;
    uint16_t minutes=12;
    request(++seq,123,HOST_CMD_SET_MODE,&one,1);assert(app_ctx.status.config.mode==TREATMENT_MODE_COLD);
    request(++seq,123,HOST_CMD_SET_TIME,&minutes,2);assert(app_ctx.status.remaining_sec==720);
    request(++seq,123,HOST_CMD_SET_AIR_LEVEL,&two,1);assert(app_ctx.status.config.air_level==AIR_LEVEL_MID);
    request(++seq,123,HOST_CMD_SET_MIST_LEVEL,&one,1);assert(app_ctx.status.config.mist_level==MIST_LEVEL_UI_LOW);
    pid_params_t pid={1000,2,3,40};
    request(++seq,123,HOST_CMD_SET_PID,&pid,16);assert(actual_heat.kp_milli==1000 && app_ctx.status.heat_pid.ki_milli==2);
    request(++seq,123,HOST_CMD_SET_PREHEAT_PID,&pid,16);assert(actual_preheat.kd_milli==3);
    request(++seq,123,HOST_CMD_SET_FAN_PID,&pid,16);assert(actual_fan.integral_limit_permille==40);
    request(++seq,123,HOST_CMD_ENTER_MAINTENANCE,NULL,0);
    request(++seq,123,HOST_CMD_MANUAL_SET_FAN,&one,1);assert(maintenance_ctx.fan_level==AIR_LEVEL_LOW);
    request(++seq,123,HOST_CMD_MANUAL_SET_MIST,&two,1);assert(maintenance_ctx.mist_level==MIST_LEVEL_UI_MID);
    request(++seq,123,HOST_CMD_EXIT_MAINTENANCE,NULL,0);assert(!maintenance_ctx.active);
    request(++seq,123,HOST_CMD_SET_KEEP_WARM,&one,1);assert(app_ctx.status.state==TREATMENT_STATE_KEEP_WARM);
    one=0;request(++seq,123,HOST_CMD_SET_KEEP_WARM,&one,1);assert(app_ctx.status.state==TREATMENT_STATE_READY);
    app_ctx.status.fault=FAULT_OVER_TEMP;app_ctx.status.state=TREATMENT_STATE_FAULT;
    request(++seq,123,HOST_CMD_CLEAR_FAULT,NULL,0);assert(app_ctx.status.fault==FAULT_NONE && app_ctx.status.state==TREATMENT_STATE_READY);
    minutes=0;request(++seq,123,HOST_CMD_SET_TIME,&minutes,2);assert(g_nebulizer_debug.response_error==3);
    assert(saves==9);
}
static void test_invalid_and_snapshot(void)
{
    reset();request(1,123,0x110,NULL,0);assert(g_nebulizer_debug.response_error==DEBUG_ERR_FORMAT);
    request(2,123,HOST_CMD_SET_PID,NULL,65536);assert(g_nebulizer_debug.response_error==DEBUG_ERR_FORMAT);
    request(3,0,HOST_CMD_START,NULL,0);assert(!starts);
    request(4,456,DEBUG_CMD_RELEASE,NULL,0);assert(g_nebulizer_debug.response_error==DEBUG_ERR_OWNER);
    const treatment_state_t states[] = {TREATMENT_STATE_PREHEATING,
        TREATMENT_STATE_RUNNING_HOT,TREATMENT_STATE_RUNNING_COLD,TREATMENT_STATE_KEEP_WARM};
    for (unsigned i=0;i<sizeof(states)/sizeof(states[0]);++i) {
        reset();app_ctx.status.state=states[i];heat_output=123;
        acquire();assert(app_ctx.status.state==states[i] && heat_output==123);
        assert(debug_service_is_owner(123) && debug_service_alive(test_runtime_ms));
    }
    reset();maintenance_ctx.active=true;app_ctx.status.maintenance.active=true;heat_output=234;
    acquire();assert(maintenance_ctx.active && heat_output==234);
    reset();acquire();reset_drops_cover=true;request(2,123,HOST_CMD_START,NULL,0);
    assert(g_nebulizer_debug.response_error && app_ctx.status.state==TREATMENT_STATE_READY);
    debug_service_publish(); assert(g_nebulizer_debug.snapshot_sequence==2 && g_nebulizer_debug.snapshot_length==263);
    for(unsigned i=1;i<=20;i++) debug_service_event(1,i,&app_ctx.status);
    assert(g_nebulizer_debug.event_sequence==20 && g_nebulizer_debug.events[3].code==20);
    FILE *f=fopen("build/tests/debug_mailbox.bin","wb");assert(f);
    assert(fwrite((const void *)&g_nebulizer_debug,1,sizeof(g_nebulizer_debug),f)==sizeof(g_nebulizer_debug));fclose(f);
}
int main(void)
{
    test_commands_and_routing();test_lease_and_maintenance();test_all_parameter_commands();test_invalid_and_snapshot();
    puts("test_debug_service: PASS (real command routing, lease, release, duplicate, snapshot, events)");
    return 0;
}

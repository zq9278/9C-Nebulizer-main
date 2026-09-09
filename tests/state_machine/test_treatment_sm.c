#include <assert.h>
#include <string.h>
#include <stdio.h>

#include <nebulizer/app_config.h>
#include <platform/board_ids.h>
#include <src/state_machine/treatment_sm.h>

static telemetry_status_t startable_status(treatment_mode_t mode, int16_t outlet_deci_c)
{
	telemetry_status_t status = { 0 };

	status.state = TREATMENT_STATE_READY;
	status.config.mode = mode;
	status.mist.online = true;
	status.sensors.cover_closed = true;
	status.sensors.ntc_deci_c[BOARD_NTC_OUTLET1] = outlet_deci_c;
	return status;
}

static void treatment_sm_test_start_requires_mist_online(void)
{
	app_event_t evt = { .type = APP_EVT_START };
	telemetry_status_t status = { 0 };
	bool accepted = false;

	status.state = TREATMENT_STATE_READY;
	status.config.mode = TREATMENT_MODE_HOT;
	status.mist.online = false;

	assert((treatment_sm_handle_event(status.state, &evt, &status, &accepted)) == (TREATMENT_STATE_READY));
	assert(!(accepted));
}

static void treatment_sm_test_hot_start_below_threshold_enters_preheat(void)
{
	app_event_t evt = { .type = APP_EVT_START };
	telemetry_status_t status = startable_status(
		TREATMENT_MODE_HOT, APP_HEAT_FULL_POWER_BELOW_DECI_C - 1);
	bool accepted = false;

	assert((treatment_sm_handle_event(status.state, &evt, &status, &accepted)) == (TREATMENT_STATE_PREHEATING));
	assert(accepted);
}

static void treatment_sm_test_hot_start_at_threshold_enters_treatment(void)
{
	app_event_t evt = { .type = APP_EVT_START };
	telemetry_status_t status = startable_status(
		TREATMENT_MODE_HOT, APP_HEAT_FULL_POWER_BELOW_DECI_C);
	bool accepted = false;

	assert((treatment_sm_handle_event(status.state, &evt, &status, &accepted)) == (TREATMENT_STATE_RUNNING_HOT));
	assert(accepted);
}

static void treatment_sm_test_preheat_ready_enters_hot_treatment(void)
{
	app_event_t evt = { .type = APP_EVT_PREHEAT_READY };
	telemetry_status_t status = startable_status(
		TREATMENT_MODE_HOT, APP_HEAT_FULL_POWER_BELOW_DECI_C);
	bool accepted = false;

	status.state = TREATMENT_STATE_PREHEATING;
	assert((treatment_sm_handle_event(status.state, &evt, &status, &accepted)) == (TREATMENT_STATE_RUNNING_HOT));
	assert(accepted);
}

static void treatment_sm_test_cold_start_skips_preheat(void)
{
	app_event_t evt = { .type = APP_EVT_START };
	telemetry_status_t status = startable_status(TREATMENT_MODE_COLD, 0);
	bool accepted = false;

	assert((treatment_sm_handle_event(status.state, &evt, &status, &accepted)) == (TREATMENT_STATE_RUNNING_COLD));
	assert(accepted);
}



int main(void)
{
    treatment_sm_test_start_requires_mist_online();
    treatment_sm_test_hot_start_below_threshold_enters_preheat();
    treatment_sm_test_hot_start_at_threshold_enters_treatment();
    treatment_sm_test_preheat_ready_enters_hot_treatment();
    treatment_sm_test_cold_start_skips_preheat();
    puts("test_treatment_sm: PASS");
    return 0;
}

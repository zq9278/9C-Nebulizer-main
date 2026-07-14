#include <zephyr/ztest.h>

#include <src/state_machine/treatment_sm.h>

ZTEST(treatment_sm, test_start_requires_mist_online)
{
	app_event_t evt = { .type = APP_EVT_START };
	telemetry_status_t status = { 0 };
	bool accepted = false;

	status.state = TREATMENT_STATE_READY;
	status.config.mode = TREATMENT_MODE_HOT;
	status.mist.online = false;

	zassert_equal(treatment_sm_handle_event(status.state, &evt, &status, &accepted),
		      TREATMENT_STATE_READY, NULL);
	zassert_false(accepted, NULL);
}

ZTEST_SUITE(treatment_sm, NULL, NULL, NULL, NULL, NULL);

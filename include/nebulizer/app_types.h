#ifndef NEBULIZER_APP_TYPES_H_
#define NEBULIZER_APP_TYPES_H_

#include <stdbool.h>
#include <stdint.h>

#include <nebulizer/fault_codes.h>

typedef enum {
	TREATMENT_STATE_BOOT = 0,
	TREATMENT_STATE_SELF_TEST,
	TREATMENT_STATE_READY,
	TREATMENT_STATE_CONFIGURING,
	TREATMENT_STATE_RUNNING_HOT,
	TREATMENT_STATE_RUNNING_COLD,
	TREATMENT_STATE_PAUSED,
	TREATMENT_STATE_DONE,
	TREATMENT_STATE_FAULT,
} treatment_state_t;

typedef enum {
	TREATMENT_MODE_HOT = 0,
	TREATMENT_MODE_COLD,
} treatment_mode_t;

typedef enum {
	AIR_LEVEL_OFF = 0,
	AIR_LEVEL_LOW,
	AIR_LEVEL_MID,
	AIR_LEVEL_HIGH,
} air_level_t;

typedef enum {
	MIST_LEVEL_UI_OFF = 0,
	MIST_LEVEL_UI_LOW,
	MIST_LEVEL_UI_MID,
	MIST_LEVEL_UI_HIGH,
} mist_level_t;

typedef enum {
	ACTUATOR_DISABLED = 0,
	ACTUATOR_ENABLED,
} actuator_state_t;

typedef struct {
	uint32_t sequence;
	int16_t ntc_deci_c[4];
	uint16_t ntc_raw[4];
	uint16_t ntc_raw_max;
	bool ntc_open[4];
	bool ntc_short[4];
	bool liquid_present;
	bool cover_closed;
	bool gx1832_active;
	uint16_t fan_rpm;
	uint32_t sample_uptime_ms;
} sensor_snapshot_t;

typedef struct {
	treatment_mode_t mode;
	uint16_t target_temp_deci_c;
	uint16_t duration_sec;
	air_level_t air_level;
	mist_level_t mist_level;
} treatment_config_t;

typedef struct {
	int32_t kp_milli;
	int32_t ki_milli;
	int32_t kd_milli;
	int32_t integral_limit_permille;
} pid_params_t;

typedef struct {
	bool active;
	air_level_t fan_level;
	mist_level_t mist_level;
	uint16_t heat_output_permille;
} maintenance_control_t;

typedef enum {
	HEAT_CONTROL_PHASE_IDLE = 0,
	HEAT_CONTROL_PHASE_PB11_FULL_POWER,
	HEAT_CONTROL_PHASE_PB11_OUTLET,
} heat_control_phase_t;

typedef struct {
	bool enabled;
	bool saturated;
	heat_control_phase_t phase;
	int16_t measured_temp_deci_c;
	int16_t target_temp_deci_c;
	int16_t error_deci_c;
	int32_t p_term_raw;
	int32_t i_term_raw;
	int32_t d_term_raw;
	uint16_t output_delay_us;
	uint16_t output_permille;
} heat_control_diag_t;

typedef struct {
	bool enabled;
	bool saturated;
	int16_t measured_temp_deci_c;
	int16_t target_temp_deci_c;
	int16_t error_deci_c;
	int32_t p_term_raw;
	int32_t i_term_raw;
	int32_t d_term_raw;
	uint16_t boost_permille;
	uint8_t base_percent;
	uint8_t output_percent;
} fan_control_diag_t;

typedef struct {
	bool online;
	bool low_water;
	bool safety_locked;
	uint8_t desired_level;
	uint8_t actual_level;
	bool running;
	uint16_t fault_code;
	uint8_t last_seq;
	uint32_t last_seen_ms;
	uint32_t consecutive_failures;
} mist_board_status_t;

typedef struct {
	treatment_state_t state;
	treatment_config_t config;
	sensor_snapshot_t sensors;
	mist_board_status_t mist;
	pid_params_t preheat_pid;
	pid_params_t heat_pid;
	heat_control_diag_t heat_diag;
	pid_params_t fan_pid;
	fan_control_diag_t fan_diag;
	maintenance_control_t maintenance;
	fault_code_t fault;
	uint32_t remaining_sec;
	bool heartbeat_ok;
} telemetry_status_t;

#endif /* NEBULIZER_APP_TYPES_H_ */

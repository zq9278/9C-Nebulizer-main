#ifndef NEBULIZER_APP_EVENTS_H_
#define NEBULIZER_APP_EVENTS_H_

#include <stdbool.h>
#include <stdint.h>

#include <nebulizer/app_types.h>
#include <nebulizer/fault_codes.h>

typedef enum {
	APP_EVT_HOST_CMD = 0,
	APP_EVT_START,
	APP_EVT_STOP,
	APP_EVT_PAUSE,
	APP_EVT_RESUME,
	APP_EVT_TREATMENT_DONE,
	APP_EVT_SET_CONFIG,
	APP_EVT_SENSOR_UPDATE,
	APP_EVT_FAULT,
	APP_EVT_FAULT_CLEAR,
	APP_EVT_MIST_STATUS,
	APP_EVT_HEARTBEAT_TIMEOUT,
	APP_EVT_PREHEAT_READY,
} app_event_type_t;

typedef struct {
	uint8_t command_id;
	uint16_t frame_id;
	uint8_t data[16];
	uint16_t data_len;
} host_cmd_event_t;

typedef struct {
	bool has_mode;
	bool has_target_temp;
	bool has_duration;
	bool has_air_level;
	bool has_mist_level;
	treatment_config_t config;
} config_update_event_t;

typedef struct {
	app_event_type_t type;
	union {
		host_cmd_event_t host_cmd;
		config_update_event_t config_update;
		fault_code_t fault;
		mist_board_status_t mist_status;
		uint32_t sensor_sequence;
	} data;
} app_event_t;

#endif /* NEBULIZER_APP_EVENTS_H_ */

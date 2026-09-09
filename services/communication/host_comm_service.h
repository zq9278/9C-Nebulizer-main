#ifndef SERVICES_COMMUNICATION_HOST_COMM_SERVICE_H_
#define SERVICES_COMMUNICATION_HOST_COMM_SERVICE_H_

#include <stddef.h>
#include <stdint.h>

#include <nebulizer/app_types.h>
#include <platform/runtime.h>

int host_comm_service_init(void);
int host_comm_service_process_rx(TickType_t timeout);
int host_comm_service_send_ack(uint16_t frame_id, uint8_t cmd_id, bool ok, uint8_t error);
int host_comm_service_send_status(const telemetry_status_t *status);
int host_comm_service_send_config(const treatment_config_t *config);
int host_comm_service_send_pid(const pid_params_t *pid);
int host_comm_service_send_preheat_pid(const pid_params_t *pid);
int host_comm_service_send_fan_pid(const pid_params_t *pid);
int host_comm_service_send_runtime(const telemetry_status_t *status);
int host_comm_service_send_maintenance(const maintenance_control_t *maintenance);
int host_comm_service_send_treatment_event(uint8_t event_id,
					   const telemetry_status_t *status);
int host_comm_service_send_raw(const uint8_t *data, size_t len);

#endif /* SERVICES_COMMUNICATION_HOST_COMM_SERVICE_H_ */

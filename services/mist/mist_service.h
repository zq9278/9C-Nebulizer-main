#ifndef SERVICES_MIST_MIST_SERVICE_H_
#define SERVICES_MIST_MIST_SERVICE_H_

#include <nebulizer/app_types.h>

int mist_service_init(void);
int mist_service_set_desired(bool running, mist_level_t level);
int mist_service_request_stop(void);
int mist_service_trigger_otp_reset(void);
void mist_service_process_task(void);
void mist_service_get_status(mist_board_status_t *status);

#endif /* SERVICES_MIST_MIST_SERVICE_H_ */

#ifndef SERVICES_MIST_MIST_BOARD_CLIENT_H_
#define SERVICES_MIST_MIST_BOARD_CLIENT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nebulizer/app_types.h>
#include <zephyr/kernel.h>

struct mist_client_request {
	uint8_t cmd_id;
	uint8_t payload[8];
	uint16_t payload_len;
};

int mist_board_client_init(void);
int mist_board_client_submit(const struct mist_client_request *req);
int mist_board_client_trigger_otp_reset(void);
void mist_board_client_process_rx(k_timeout_t timeout);
void mist_board_client_process_timeouts(void);
void mist_board_client_get_status(mist_board_status_t *status);

#endif /* SERVICES_MIST_MIST_BOARD_CLIENT_H_ */

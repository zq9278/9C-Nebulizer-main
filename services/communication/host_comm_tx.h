#ifndef SERVICES_COMMUNICATION_HOST_COMM_TX_H_
#define SERVICES_COMMUNICATION_HOST_COMM_TX_H_

#include <stddef.h>
#include <stdint.h>
#include <platform/runtime.h>

int host_comm_tx_init(void);
int host_comm_tx_enqueue(const uint8_t *data, size_t len);
int host_comm_tx_process_one(TickType_t timeout);

#endif /* SERVICES_COMMUNICATION_HOST_COMM_TX_H_ */

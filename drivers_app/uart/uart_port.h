#ifndef DRIVERS_APP_UART_UART_PORT_H_
#define DRIVERS_APP_UART_UART_PORT_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include "uart_rx_ring.h"

typedef void (*uart_port_rx_notify_t)(void *user_data);

struct uart_port {
	const struct device *dev;
	struct uart_rx_ring rx_ring;
	struct k_sem rx_sem;
	struct k_sem tx_done;
	struct k_mutex tx_lock;
	uart_port_rx_notify_t notify;
	void *notify_user_data;
	const uint8_t *tx_buf;
	size_t tx_len;
	size_t tx_pos;
};

int uart_port_init(struct uart_port *port, const struct device *dev,
		   uint8_t *rx_storage, size_t rx_storage_size,
		   uart_port_rx_notify_t notify, void *notify_user_data);
int uart_port_send(struct uart_port *port, const uint8_t *data, size_t len,
		   k_timeout_t timeout);
size_t uart_port_read(struct uart_port *port, uint8_t *data, size_t len);
int uart_port_wait_rx(struct uart_port *port, k_timeout_t timeout);
void uart_port_isr(const struct device *dev, void *user_data);

#endif /* DRIVERS_APP_UART_UART_PORT_H_ */

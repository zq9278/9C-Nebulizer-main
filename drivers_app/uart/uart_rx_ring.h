#ifndef DRIVERS_APP_UART_UART_RX_RING_H_
#define DRIVERS_APP_UART_UART_RX_RING_H_

#include <stddef.h>
#include <stdint.h>

struct uart_rx_ring {
	volatile size_t head, tail;
	volatile size_t used;
	uint8_t *storage;
	size_t storage_size;
};

void uart_rx_ring_init(struct uart_rx_ring *ring, uint8_t *storage, size_t size);
size_t uart_rx_ring_put(struct uart_rx_ring *ring, const uint8_t *data, size_t len);
size_t uart_rx_ring_get(struct uart_rx_ring *ring, uint8_t *data, size_t len);
size_t uart_rx_ring_size_get(const struct uart_rx_ring *ring);

#endif /* DRIVERS_APP_UART_UART_RX_RING_H_ */

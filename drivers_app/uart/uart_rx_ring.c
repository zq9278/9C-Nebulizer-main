#include "uart_rx_ring.h"

void uart_rx_ring_init(struct uart_rx_ring *ring, uint8_t *storage, size_t size)
{
	ring->storage = storage;
	ring->storage_size = size;
	ring_buf_init(&ring->rb, size, storage);
}

size_t uart_rx_ring_put(struct uart_rx_ring *ring, const uint8_t *data, size_t len)
{
	return ring_buf_put(&ring->rb, data, len);
}

size_t uart_rx_ring_get(struct uart_rx_ring *ring, uint8_t *data, size_t len)
{
	return ring_buf_get(&ring->rb, data, len);
}

size_t uart_rx_ring_size_get(const struct uart_rx_ring *ring)
{
	return ring_buf_size_get((struct ring_buf *)&ring->rb);
}

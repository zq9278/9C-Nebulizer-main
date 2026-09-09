#include "uart_rx_ring.h"
#include <platform/runtime.h>
void uart_rx_ring_init(struct uart_rx_ring *ring, uint8_t *storage, size_t size)
{
    ring->storage = storage; ring->storage_size = size;
    ring->head = ring->tail = ring->used = 0;
}
size_t uart_rx_ring_put(struct uart_rx_ring *ring, const uint8_t *data, size_t len)
{
    size_t count = 0;
    while (count < len) {
        uint32_t key = runtime_irq_save();
        if (ring->used == ring->storage_size) { runtime_irq_restore(key); break; }
        ring->storage[ring->head] = data[count++];
        ring->head = (ring->head + 1) % ring->storage_size;
        ring->used++;
        runtime_irq_restore(key);
    }
    return count;
}
size_t uart_rx_ring_get(struct uart_rx_ring *ring, uint8_t *data, size_t len)
{
    size_t count = 0;
    while (count < len) {
        uint32_t key = runtime_irq_save();
        if (!ring->used) { runtime_irq_restore(key); break; }
        data[count++] = ring->storage[ring->tail];
        ring->tail = (ring->tail + 1) % ring->storage_size;
        ring->used--;
        runtime_irq_restore(key);
    }
    return count;
}
size_t uart_rx_ring_size_get(const struct uart_rx_ring *ring) { return ring->used; }

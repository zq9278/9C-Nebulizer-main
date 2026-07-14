#include "uart_port.h"

#include <errno.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(uart_port, CONFIG_NEBULIZER_LOG_LEVEL);

int uart_port_init(struct uart_port *port, const struct device *dev,
		   uint8_t *rx_storage, size_t rx_storage_size,
		   uart_port_rx_notify_t notify, void *notify_user_data)
{
	if ((port == NULL) || (dev == NULL) || (rx_storage == NULL)) {
		return -EINVAL;
	}

	port->dev = dev;
	port->notify = notify;
	port->notify_user_data = notify_user_data;
	port->tx_buf = NULL;
	port->tx_len = 0;
	port->tx_pos = 0;

	uart_rx_ring_init(&port->rx_ring, rx_storage, rx_storage_size);
	k_sem_init(&port->rx_sem, 0, UINT_MAX);
	k_sem_init(&port->tx_done, 0, 1);
	k_mutex_init(&port->tx_lock);

	uart_irq_callback_user_data_set(dev, uart_port_isr, port);
	uart_irq_rx_enable(dev);

	return 0;
}

int uart_port_send(struct uart_port *port, const uint8_t *data, size_t len,
		   k_timeout_t timeout)
{
	uint32_t tx_complete_deadline_ms;

	if ((port == NULL) || (data == NULL) || (len == 0U)) {
		return -EINVAL;
	}

	k_mutex_lock(&port->tx_lock, K_FOREVER);
	port->tx_buf = data;
	port->tx_len = len;
	port->tx_pos = 0U;
	k_sem_reset(&port->tx_done);

	uart_irq_tx_enable(port->dev);
	if (k_sem_take(&port->tx_done, timeout) != 0) {
		uart_irq_tx_disable(port->dev);
		port->tx_buf = NULL;
		port->tx_len = 0U;
		port->tx_pos = 0U;
		k_mutex_unlock(&port->tx_lock);
		LOG_WRN("uart tx timed out");
		return -ETIMEDOUT;
	}

	tx_complete_deadline_ms = k_uptime_get_32() + 20U;
	while (!uart_irq_tx_complete(port->dev)) {
		if ((int32_t)(tx_complete_deadline_ms - k_uptime_get_32()) <= 0) {
			LOG_WRN("uart tx complete wait timed out");
			break;
		}
		k_busy_wait(100);
	}

	k_mutex_unlock(&port->tx_lock);
	return 0;
}

size_t uart_port_read(struct uart_port *port, uint8_t *data, size_t len)
{
	return uart_rx_ring_get(&port->rx_ring, data, len);
}

int uart_port_wait_rx(struct uart_port *port, k_timeout_t timeout)
{
	return k_sem_take(&port->rx_sem, timeout);
}

void uart_port_isr(const struct device *dev, void *user_data)
{
	struct uart_port *port = user_data;

	if (!uart_irq_update(dev)) {
		return;
	}

	if (uart_irq_rx_ready(dev)) {
		uint8_t buf[32];
		int rx = uart_fifo_read(dev, buf, sizeof(buf));

		if (rx > 0) {
			uart_rx_ring_put(&port->rx_ring, buf, (size_t)rx);
			k_sem_give(&port->rx_sem);
			if (port->notify != NULL) {
				port->notify(port->notify_user_data);
			}
		}
	}

	if (uart_irq_tx_ready(dev) && (port->tx_buf != NULL)) {
		size_t remaining = port->tx_len - port->tx_pos;
		int sent = uart_fifo_fill(dev, &port->tx_buf[port->tx_pos], remaining);

		if (sent > 0) {
			port->tx_pos += (size_t)sent;
		}

		if (port->tx_pos >= port->tx_len) {
			uart_irq_tx_disable(dev);
			port->tx_buf = NULL;
			port->tx_len = 0U;
			port->tx_pos = 0U;
			k_sem_give(&port->tx_done);
		}
	}
}

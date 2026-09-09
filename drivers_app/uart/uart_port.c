#include "uart_port.h"
#include <limits.h>
static struct uart_port *host_port, *mist_port;
static USART_TypeDef *uart_regs(const struct uart_port *port)
{
    UART_HandleTypeDef *handle = port->dev->instance;
    return handle->Instance;
}
int uart_port_init(struct uart_port *port, const struct board_device *dev,
    uint8_t *storage, size_t size, uart_port_rx_notify_t notify, void *user_data)
{
    if (!port || !board_device_ready(dev) || !storage || !size) return -EINVAL;
    port->dev = dev;
    port->notify = notify;
    port->notify_user_data = user_data;
    port->tx_buf = NULL;
    port->tx_len = port->tx_pos = 0;
    uart_rx_ring_init(&port->rx_ring, storage, size);
    port->rx_sem = xSemaphoreCreateBinaryStatic(&port->rx_storage);
    port->tx_done = xSemaphoreCreateBinaryStatic(&port->tx_storage);
    port->tx_lock = xSemaphoreCreateMutexStatic(&port->lock_storage);
    configASSERT(port->rx_sem && port->tx_done && port->tx_lock);
    USART_TypeDef *uart = uart_regs(port);
    IRQn_Type irq;
    if (uart == USART1) { host_port = port; irq = USART1_IRQn; }
    else if (uart == USART4) { mist_port = port; irq = USART3_4_IRQn; }
    else return -EINVAL;
    uart->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    uart->CR1 |= USART_CR1_RXNEIE_RXFNEIE;
    uart->CR3 |= USART_CR3_EIE;
    HAL_NVIC_SetPriority(irq, 2, 0);
    HAL_NVIC_EnableIRQ(irq);
    return 0;
}
int uart_port_send(struct uart_port *port, const uint8_t *data, size_t len, TickType_t timeout)
{
    if (!port || !data || !len) return -EINVAL;
    TickType_t started = xTaskGetTickCount();
    if (xSemaphoreTake(port->tx_lock, timeout) != pdPASS) return -ETIMEDOUT;
    if (timeout != portMAX_DELAY) {
        TickType_t elapsed = xTaskGetTickCount() - started;
        timeout = elapsed < timeout ? timeout - elapsed : 0;
    }
    xSemaphoreTake(port->tx_done, 0);
    USART_TypeDef *uart = uart_regs(port);
    uint32_t key = runtime_irq_save();
    port->tx_buf = data;
    port->tx_len = len;
    port->tx_pos = 0;
    uart->ICR = USART_ICR_TCCF;
    uart->CR1 |= USART_CR1_TXEIE_TXFNFIE;
    runtime_irq_restore(key);
    BaseType_t ok = xSemaphoreTake(port->tx_done, timeout);
    key = runtime_irq_save();
    uart->CR1 &= ~(USART_CR1_TXEIE_TXFNFIE | USART_CR1_TCIE);
    port->tx_buf = NULL;
    port->tx_len = port->tx_pos = 0;
    runtime_irq_restore(key);
    xSemaphoreGive(port->tx_lock);
    return ok == pdPASS ? 0 : -ETIMEDOUT;
}
size_t uart_port_read(struct uart_port *port, uint8_t *data, size_t len)
{
    return uart_rx_ring_get(&port->rx_ring, data, len);
}
int uart_port_wait_rx(struct uart_port *port, TickType_t timeout)
{
    if (uart_rx_ring_size_get(&port->rx_ring)) return 0;
    return xSemaphoreTake(port->rx_sem, timeout) == pdPASS ? 0 : -EAGAIN;
}
static void uart_port_irq(struct uart_port *port)
{
    if (!port) return;
    USART_TypeDef *uart = uart_regs(port);
    BaseType_t wake = pdFALSE;
    uint32_t status = uart->ISR;
    if (status & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) {
        port->rx_errors++;
        uart->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    }
    if (status & USART_ISR_RXNE_RXFNE) {
        uint8_t byte = (uint8_t)uart->RDR;
        if (uart_rx_ring_put(&port->rx_ring, &byte, 1) != 1) port->rx_overflows++;
        xSemaphoreGiveFromISR(port->rx_sem, &wake);
        if (port->notify) port->notify(port->notify_user_data);
    }
    if ((status & USART_ISR_TXE_TXFNF) && (uart->CR1 & USART_CR1_TXEIE_TXFNFIE) && port->tx_buf) {
        uart->TDR = port->tx_buf[port->tx_pos++];
        if (port->tx_pos == port->tx_len) {
            uart->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
            uart->CR1 |= USART_CR1_TCIE;
        }
    }
    /* Read TC again after filling TDR; the old status may have contained TC. */
    if ((uart->ISR & USART_ISR_TC) && (uart->CR1 & USART_CR1_TCIE)) {
        uart->CR1 &= ~USART_CR1_TCIE;
        uart->ICR = USART_ICR_TCCF;
        port->tx_buf = NULL;
        xSemaphoreGiveFromISR(port->tx_done, &wake);
    }
    portYIELD_FROM_ISR(wake);
}
void USART1_IRQHandler(void) { uart_port_irq(host_port); }
void USART3_4_IRQHandler(void) { uart_port_irq(mist_port); }

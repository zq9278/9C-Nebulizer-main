#include "host_comm_rx.h"

#include <services/communication/host_comm_tx.h>
#include <services/communication/host_comm_service.h>
#include <zephyr/kernel.h>

/*
 * HostCommTask 同时负责 Host UART 的接收和发送。
 *
 * 运行策略是：
 * 1. 先用一个较短超时等待 RX，保证接收优先级
 * 2. 再顺手处理一帧 TX 队列
 *
 * 这样可以把原来拆开的 HostRxTask / HostTxTask 合并成一个通信线程，
 * 同时尽量避免 TX 长时间占用导致 RX 饥饿。
 */
void host_comm_rx_task(void)
{
	while (true) {
		(void)host_comm_service_process_rx(K_MSEC(10));
		(void)host_comm_tx_process_one(K_NO_WAIT);
	}
}

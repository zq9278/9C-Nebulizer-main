# FreeRTOS 固件结构与板上验证

目标为 STM32G070CBT6，Cortex-M0+，128 KiB Flash、36 KiB SRAM，HSI 经 PLL 得到 64 MHz。
使用原生 FreeRTOS API、STM32 HAL 和 CMSIS；依赖源代码保存在 `third_party/`，正常构建不访问网络。

## 任务与同步

所有任务、队列、互斥锁和信号量均静态分配，关闭动态内存分配。
优先级数值越大越高，栈配置以字节为单位，创建时换算成 `StackType_t` 数量。

| 任务 | 优先级 | 栈字节 | 职责 |
|---|---:|---:|---|
| Boot | 5 | 2048 | 调度器启动后初始化服务，完成后删除自身；静态内存保留 |
| Control | 4 | 1536 | 10 ms 固定基准；采样/控制 20 ms、安全检查 50 ms、遥测 500 ms |
| AppTask | 3 | 1792 | 等待事件队列，执行状态机和控制命令 |
| HostComm | 2 | 1792 | 主机串口接收、解析和发送 |
| MistComm | 2 | 1792 | 雾化板协议、超时和重试 |
| Settings | 1 | 1024 | 通知唤醒，合并 1 秒内的参数更改，写入 EEPROM |
| Log | 1 | 1024 | 消费日志队列并输出到 USART2 |
| Idle | 0 | 512 | FreeRTOS 空闲任务 |

事件队列 32 项，主机发送队列 16 项，雾化命令队列 8 项，日志队列 8 项。
UART RX 使用环形缓冲区和 `xSemaphoreGiveFromISR`；TX 在硬件 TC（最后停止位发送完毕）后唤醒任务，超时清除中断和发送指针。
共享配置、传感器快照和控制状态使用具有优先级继承的互斥锁。
盖子防抖由安全检查周期推进；EEPROM 写周期等待只阻塞 Settings 任务。
日志支持当前使用的整数、十六进制、字符串格式；队列满时丢弃日志，计数见 `platform_log_dropped`。

## 引脚与外设

| 功能 | 外设/引脚 | 配置 |
|---|---|---|
| 上位机 | USART1，PB6/PB7，AF0 | 115200，8N1 |
| 调试日志 | USART2，PA2/PA3，AF1 | 115200，8N1 |
| 雾化板 | USART4，PA0/PA1，AF4 | 115200，8N1 |
| 风扇 | TIM3_CH1，PA6，AF1 | 10 kHz，板级反相，100% 引脚占空比表示停机 |
| 风扇反馈 | PA4 | 上拉输入 |
| 液位 | PB5 | 上拉，低有效 |
| 盖子 Hall1 | PA11 | 上拉，低有效 |
| Hall2 | PA12 | 上拉，低有效；维持当前业务未使用状态 |
| GX1832 | PA10 | 下拉，高有效 |
| TRIAC gate | PB1 | 高有效 |
| 过零检测 | PB2/EXTI2 | 双边沿，5 ms 过滤 |
| OTP/光耦电源 | PA7 | 高输出使能，输入态释放 |
| EEPROM 写保护 | PB15 | 高电平保护 |
| EEPROM | I2C2，PB13/PB14，AF6 | 约 100 kHz；地址和地址宽度见 app_config.h |
| Outlet1 NTC | PB11 / ADC1_IN15 | 12 位 |
| Power stage NTC | PB10 / ADC1_IN11 | 12 位 |
| Outlet2 NTC | PB12 / ADC1_IN16 | 12 位 |
| Kettle NTC | PA5 / ADC1_IN5 | 12 位 |

PB11/PB12 使用 R25=10 kΩ、B25/85=3435 K 的附件标称曲线；PB10/PA5 保留原曲线，详见 [NTC 参数](NTC.md)。

ADC 使用固定通道序列、每次只选一个通道。通道 15/16 不能用四位通道编号的可配置序列模式。
TIM15 以 1 MHz 连续运行，通过溢出中断扩展为 32 位微秒时钟，用于过零间隔过滤。
过零和 TIM15 中断优先级为 0，UART 为 2，SysTick/PendSV 为最低优先级。
过零中断不调用 FreeRTOS；短临界区保存并恢复 PRIMASK。
SysTick 在启动前提供 HAL 超时，在调度启动后同时推进 HAL 和 FreeRTOS 时钟。

## 构建与验证

```sh
cmake --preset debug
cmake --build --preset debug
cmake --preset release
cmake --build --preset release
python tools/run_tests.py
python tools/verify_firmware.py build/release/nebulizer.elf
```

本机测试使用 GCC/Clang，可通过 `--cc` 或 `HOST_CC` 指定；Windows 也会查找 CLion 随附的 MinGW。
编译启用 `-Wall -Wextra -Werror`，输出 `.su` 栈使用信息、`.map` 链接映射和 ELF/HEX/BIN。
链接脚本预留 2 KiB 主栈/中断栈，并检查 RAM 边界。
栈溢出、HardFault 或启动失败会关闭加热与风扇并停在故障位置。
查看 `runtime_fault_file`、`runtime_fault_line` 和 `uxTaskGetStackHighWaterMark()` 可定位问题。

## 尚需板上确认

编译、协议及状态机测试不能证明实际外设时序和整机行为。首次上板需确认：

1. 启动日志、64 MHz 时钟、任务运行及各任务栈余量。
2. 三路 UART、连续收发、超时恢复及 `rx_errors`/`rx_overflows`。
3. 四路 NTC 与实际温度对应，特别是 PB11/PB12 的通道映射。
4. PA6 停机电平和 10 kHz 波形；PB2 过零及 PB1 半周分配逻辑。
5. 开盖、缺水、过温、通信丢失、停止指令的执行器动作。
6. EEPROM 地址/写保护及断电重启后的参数恢复；读取失败保留原有默认参数回退。
7. 预热、热疗、冷疗和保温转换，确认上位机协议行为保持一致。

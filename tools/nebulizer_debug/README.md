# Nebulizer SWD 调试工作台

独立于原串口上位机的新 PyQt6 桌面程序。通过 J-Link 或 DAPLink / SWD 访问固件 RAM 调试接口，
使用当前烧录固件对应的 ELF 定位接口，并逐段核对设备 Flash。
原 `tools/nebulizer_host_pyqt/` 和 `tools/run_host.ps1` 保留，继续可用于串口通信。

## 启动

### DAPLink / CMSIS-DAP

安装本目录 requirements.txt 中的更新依赖后，仍使用 `tools/run_debug_host.ps1` 启动。
在“探针类型”选择“DAPLink / CMSIS-DAP”，刷新探针并选择完整 ID（可包含字母），
选择设备当前烧录固件对应的 ELF，点击连接。接线为 SWDIO、SWCLK、GND，
按探针要求连接电压参考，设备自行供电，初始 SWD 速度建议 1000 kHz。
目标芯片和 J-Link DLL 字段仅用于 J-Link，在 DAPLink 模式下禁用。

DAPLink 后端使用 pyOCD 0.44 的通用 Cortex-M 目标，不需要安装 STM32 芯片 Pack。
采用 attach 模式，不烧录、不复位、不暂停、不自动解锁；断开时不自动恢复 CPU。
禁用缓存、项目配置和用户脚本，保留完整 ELF/Flash 校验和邮箱协议。
探针须提供 CMSIS-DAP 的 SWD 接口，虚拟串口并非此工具的连接接口。
关闭占用同一探针的 IDE/OpenOCD/pyOCD 会话。J-Link 支持继续保留。

新增后端尚未完成实板验收；首次接入需验证运行中连接、曲线与命令应答、
断线租约超时、错误 ELF 拒绝写入，以及 CPU 暂停后会话终止。

### J-Link

先构建并使用已有烧录工具将本次固件烧录到设备。上位机连接过程不烧录、不解锁、不复位、
不暂停或自动恢复 CPU。旧固件不包含调试接口，不能直接使用新上位机控制。

```powershell
cmake --preset debug
cmake --build --preset debug
python -m pip install -r tools/nebulizer_debug/requirements.txt
.\tools\run_debug_host.ps1
```

也可直接运行：

```sh
python tools/nebulizer_debug/app.py
```

安装 SEGGER J-Link Software and Documentation Pack。程序需要与 Python 位数对应的
J-Link 动态库；自动查找失败时，在界面填写 `JLink_x64.dll` 的完整路径。
Python 依赖为 PyQt6、pyelftools、pylink-square、pyOCD 和 pyserial。
SWD 调试无需串口；新增“固件升级（串口）”通过 USART1 通信，
详情见 [单 App 升级说明](../../docs/FirmwareUpgrade.md)。
升级后调试应选择与该升级包对应的 `build/upgrade-release/nebulizer.elf`。

1. 连接 J-Link 的 SWDIO、SWCLK、GND、目标电压参考；设备按原方式供电。
2. 关闭其他占用该探针的调试会话。当前实现由本程序独占探针，不提供与 IDE 共享连接。
3. 点击“刷新探针”，选择序列号，目标芯片默认 `STM32G070CB`，初始 SWD 速度 1000 kHz。
4. 选择此次烧录对应的 `build/debug/nebulizer.elf`（Release 固件应选择 Release ELF）。
5. 点击连接。程序只读连接，核对 ELF 中所有 Flash PT_LOAD 段，包括 RAM 初始化数据的 Flash 镜像。
6. 默认观察模式，可查看实时值、曲线、事件和参数，不发送心跳或自动修改设备参数。
7. 点击“接管控制”。支持预热、热雾/冷雾治疗、保温和维护中接管，保留当前状态与输出；接管后由调试心跳维持控制。另一调试会话仍不能抢占。此规则需要烧录更新后的固件，并选择对应 ELF。
8. 操作完成后点击“释放控制”或“断开”。释放会关闭维护输出、暂停运行中的治疗、退出当前保温，
   不清除 EEPROM 中的保温设置。下次重新上电仍按原有配置恢复。

连接失败不会降级到暂停 CPU 的内存访问模式。实际探针和 DLL 对后台访问的支持仍需实板确认。
不要在执行器运行中用另一个调试器暂停 CPU：CPU 停止时，软件心跳保护本身也无法执行。

## 功能

- 实时查看四路 NTC、ADC、治疗状态、故障、液位、盖子、GX1832、雾化状态和加热/风机诊断。
- 配置冷热模式、目标温度、1–60 分钟治疗时间、风量、雾量、持续保温。
- 开始、暂停、恢复、停止治疗、关闭保温、清除故障。
- 出口 PID、风机 PID、旧预热 PID 读写；旧预热 PID 明确标注为兼容参数，不参与当前温控。
- 进入/退出维护、手动设置风机/雾化档位与 0–1000‰ 加热输出。
- 参数 JSON 与旧上位机字段兼容；导入只修改编辑区，不自动发送。
- 参数应用串行等待每条命令结果，失败停止批次；成功后等待新的固件快照再同步编辑区。
- 单一工作台集中展示监控、参数、维护、曲线与故障日志，左右/上下分区可拖动调整。
- 温度、输出、误差曲线，横纵向缩放、两轴平移、系列选择，CSV 导出与连续采集。Ctrl+滚轮缩放纵轴，普通滚轮缩放横轴，双击恢复自动范围。温度通道显示端口和中文名称。
- 事件、故障和操作日志；调试事件环覆盖时提示丢失条数。
- 调试控制会话内，连续保温两小时后发送关闭保温命令。

保持原有行为的边界：

- 热雾保温开启时 STOP 返回保温；关闭保温使用 `SET_KEEP_WARM(0)`。
- 两小时计时仍在 PC，观察模式不执行计时控制，断开/重连后重新计时；不是固件永久期限。
- 参数 ACK 表示运行时应用成功，EEPROM 仍由固件延迟保存，ACK 不代表写入 EEPROM 已完成。
- 风机 RPM 当前固件没有采集实现，界面显示“未实现采集”。锅体液位联锁仍处于原有禁用状态。
- 硬件日志 USART2 保持原输出；本程序显示业务事件/故障/命令日志，不读取 USART2 文本日志。
- 固件 100 ms 发布快照，桌面约 100 ms 读取。曲线缓存最多 864,000 个样本；长期保存使用 CSV。
- 不做自动重连或危险命令自动重发。目标复位、CPU 暂停、快照停更或请求超时后结束会话，
  用户重新连接后先检查实际状态，再决定是否继续。

## 双通道与失联

USART1 帧格式、CRC、命令 ID、参数单位、串口配置保持兼容。串口屏继续收到状态与治疗事件。
新增请求来源仅为 MCU 内部字段，未加入串口线上的数据结构。

串口屏默认持有业务控制。调试端接管后，串口屏的读取和心跳仍可用，
大部分修改命令返回 NACK `0x70`；STOP、PAUSE、EXIT_MAINTENANCE、关闭保温仍允许两端发起。
调试命令应答进入 RAM 邮箱，不伪装成串口 ACK。

调试心跳和串口心跳独立。调试端观察时不会维持串口心跳；接管后，治疗心跳判据切换为调试租约。
最后一次调试心跳超过 3000 ms 即锁存失效，迟到心跳不能恢复旧租约。
Control 任务阻断输出，AppTask 清除维护、暂停治疗、退出保温并释放控制。
自动补水恢复的旧请求不会在释放后重新启动治疗，需要显式 START/RESUME。

## 固件调试 ABI v1

实现：`services/debug/debug_service.[ch]`。符号 `g_nebulizer_debug`，928 字节，4 字节对齐。
所有整数为小端固定宽度；不解析 MCU 私有结构体或依赖 C 枚举大小。

| 偏移 | 字段 | 所有者/含义 |
|---:|---|---|
| 0 | magic u32 | 固件，`0x3144424E` |
| 4 | version u32 | 固件，1 |
| 8 | size u32 | 固件，928 |
| 12 | capabilities u32 | 固件，bit0 快照、bit1 命令、bit2 事件 |
| 16 | snapshot_sequence u32 | 固件，奇数正在发布、偶数稳定 |
| 20 | uptime_ms u32 | 固件快照发布时间 |
| 24 | owner_session u32 | 0 串口控制；其他值为调试会话 |
| 28 | owner_alive u32 | 当前调试租约状态 |
| 32 | request_sequence u32 | PC 最后写入的提交字段；0 无请求 |
| 36 | request_session u32 | PC 随机会话号，非 0 |
| 40 | command u32 | 原 27 条命令，另加 E0 接管、E1 释放 |
| 44 | payload_length u32 | 0–16 |
| 48 | payload u8[16] | 与原命令负载相同 |
| 64 | heartbeat_session u32 | PC 当前心跳所属会话 |
| 68 | heartbeat_counter u32 | PC 500 ms 递增，最后写入 |
| 72 | response_sequence u32 | 固件最后写入的完成标志 |
| 76 | response_session u32 | 对应请求会话 |
| 80 | response_command u32 | 对应命令 |
| 84 | response_error u32 | 0 成功，其他为原错误码或调试错误 |
| 88 | snapshot_length u32 | 当前 263 字节 |
| 92 | event_sequence u32 | 最新事件序号 |
| 96 | snapshot u8[320] | 7 个完整原协议帧，含各自 CRC |
| 416 | events[16] | 每项 32 字节 |

快照包含 STATUS、CONFIG、PID、PREHEAT_PID、FAN_PID、RUNTIME、MAINT 帧。
读取 `snapshot_sequence` → 读取元数据和快照 → 再读取序号；只接受两次相同的非零偶数。

请求：先写 sequence=0，再写会话/命令/参数，最后通过对齐的 32 位写提交新的非零序号。
每次只允许一个未完成请求；原序号重复读取不会重复执行。同一探针仅一个 PC 会话写入，
不支持多个进程并发改写邮箱。请求无应答时结果未知，不能重新提交 START 等有副作用的命令。
结果读取前后检查 sequence，且必须匹配 session 和 command。

事件字段：`sequence, uptime_ms, type, code, state, remaining_sec, outlet_deci_c, kettle_deci_c`。
前六项 u32，温度为 i32。type=1 治疗事件、2 故障、3 调试释放。
每项先置 sequence=0，写字段，最后提交 sequence；事件头最后发布。
读取前后检查记录序号。环覆盖可检测，不能保证断开期间无限保留历史。

新增错误：`0x70` 控制权不属于当前会话，`0x71` 格式错误，`0x72` 会话无效。

## 验证

```powershell
cmake --build --preset debug
python tools/run_tests.py
python tests/debug/test_debug_host.py
python tests/debug/test_daplink_transport.py
python tools/verify_firmware.py build/debug/nebulizer.elf
```

C 测试运行实际 AppTask 命令路径及 RAM 接口，硬件输出和调度使用测试替身；
生成的二进制快照再由 Python 解码。Python 测试覆盖 Flash 不匹配、只读连接、提交顺序、
结果身份、CRC/撕裂快照、超时、目标复位/暂停、事件覆盖、批次失败、参数编辑及界面渲染。
截图输出到 `build/tests/debug_ui_*.png`。

这些软件测试不替代实板验收。上板需确认：串口屏单独运行、两端观察/接管/停止、
拔出 J-Link 后治疗暂停与维护停机、错误 ELF 拒绝写入、设备复位后旧会话失效、
参数掉电恢复，以及正常预热/热雾/冷雾/保温/开盖/缺水流程。

温度、输出和误差同时绘制在综合曲线中，使用相同数值刻度，标注各自 °C / % 单位；各曲线最新点显示圆点和当前值，可勾选隐藏。查看历史时不会把历史点标为最新值。参数数字框和下拉框不响应滚轮改值，仍可点击或键盘输入。

曲线缓存为 86.4 万组（100 ms 周期约 24 小时），满后覆盖最早的数据；横轴最大范围 24 小时。使用紧凑数值存储，12 个数值字段满容量约 79 MiB（不含分配器及界面开销）。每次绘图最多抽样 2,000 组以保持响应，宽范围下短暂尖峰可能未绘出，可放大查看；CSV 导出保留完整缓存。清除曲线按钮释放缓存并恢复跟随，不改变曲线勾选或停止 CSV 记录。

# Boot + 单 App 串口升级 v1

此版本不增加外部存储，Boot 经原上位机串口 USART1 接收固件，直接覆盖唯一 App。
升级中断后可重新完整升级；没有旧镜像备份，不提供自动回滚。
EEPROM 原有参数格式保持不变，不把升级包或状态写入 EEPROM。

## 构建与分区

```powershell
cmake --preset upgrade-release
cmake --build --preset upgrade-release
```

调试版本使用 `upgrade-debug`。原 `debug` / `release` 构建保持原单体布局。
升级版打包需要 Python；打包器仅使用标准库。

| 区域 | 起始地址 | 结束地址（含） | 容量 |
|---|---|---|---:|
| Boot | 0x08000000 | 0x08003FFF | 16 KiB |
| App | 0x08004000 | 0x0801F7FF | 110 KiB |
| 有效镜像记录 | 0x0801F800 | 0x0801FFFF | 2 KiB |

RAM 顶部 16 字节 `0x20008FF0..0x20008FFF` 专用于 App → Boot 的复位请求。
两个链接脚本均保留它，栈顶为 `0x20008FF0`；App 固定链接到 `0x08004000`。
Flash 页为 2 KiB，写入使用对齐的 64 位 doubleword；分块 120 字节，最后一块补 FF。
硬件约束参见 [ST RM0454](https://www.st.com/resource/en/reference_manual/rm0454-stm32g0b0-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)。

`build/upgrade-release/` 产物：

| 文件 | 用途 |
|---|---|
| nebulizer_factory.hex | 首次安装：含 Boot、App 和有效记录；优先使用 HEX |
| nebulizer_factory.bin | 同样的完整 128 KiB Flash 镜像，烧录基址 0x08000000 |
| nebulizer.nfu | 后续 USART1 升级包，只含 App 及包头 |
| nebulizer.elf / .hex / .bin | App 调试/分析文件，基址 0x08004000；单独烧入不生成有效记录 |
| nebulizer_boot.elf / .hex / .bin | Boot 调试文件，基址 0x08000000；串口不支持升级 Boot |

## 首次安装

旧单体固件没有本 Boot，不能仅通过新增串口命令迁移。
先用 DAPLink/ST-Link/J-Link 和现有烧录工具写入 `nebulizer_factory.hex`，校验后复位。
例如已有 STM32CubeProgrammer + ST-Link 时：

```powershell
STM32_Programmer_CLI -c port=SWD -w build/upgrade-release/nebulizer_factory.hex -v -rst
```

Boot 每次启动核对记录、App CRC32 和初始 SP/Reset 向量。有效则等待 1.5 秒进入 App；
收到合法 Boot 命令后保持升级模式。无有效镜像或 App 主动请求升级时，一直留在 Boot。
Boot 不运行治疗任务，PB1 不产生加热触发，PA6 输出风机停止电平，PA7 释放雾化板供电控制。
App 正常启动后仍按原 EEPROM 设置运行，包括原有保温配置。

Boot 写入接口只接受 App 和记录区地址。建议生产烧录时为 Boot 页 0..7 配置 WRP，
本代码不会自动改写 Option Bytes、解锁保护或擦除 Boot。

## 上位机升级

USART1 接线为 PB6 TX、PB7 RX、GND，115200 / 8N1。PC 使用匹配的串口电平接口。
升级期间该串口只能由一个上位机占用；原串口屏也不能同时向该总线发送控制帧。

安装上位机依赖，启动原程序：

```powershell
python -m pip install -r tools/nebulizer_debug/requirements.txt
.\tools\run_debug_host.ps1
```

在调试工作台先断开 SWD 会话，再点击“固件升级（串口）”。
原串口上位机的 **Firmware Update** 按钮也打开同一个窗口，并先关闭原串口轮询。
选择上位机通信 COM 口及 `.nfu`，点击“开始升级（覆盖旧 App）”。
升级器在打开串口前校验包头、长度、产品标识、链接地址、向量和 CRC32。
若设备已在 Boot，直接握手；否则发送 ENTER_BOOT，再以 Boot INFO 回包确认切换成功。
App 只在就绪、完成、暂停或故障状态接受 ENTER_BOOT，维护、治疗和保温中拒绝；
调试控制租约持有时也拒绝串口切换。请先停止对应业务并释放调试控制。

流程为 INFO → BEGIN → DATA（逐块等待应答）→ END → RUN。
BEGIN 擦除前使旧镜像记录失效；END 校验实际 Flash 后最后提交有效标记。
RUN 重启后，上位机用原 GET_STATUS 的匹配 ACK 确认 App 已恢复通信。
“固件安装并校验成功”和“App 启动确认”分别报告，不能把传输结束当作业务运行正常。
取消/断线后不继续发送数据，也不会自动重发整次升级。

可直接使用命令行：

```powershell
python tools/fw_update/client.py --port COM5 --firmware build/upgrade-release/nebulizer.nfu
```

加 `--no-run` 可在成功提交后停留于 Boot；之后复位才按正常窗口启动。

## 传输协议

延用原帧封装：`AA 55 + frame_id:u16 + type:u8 + len:u16 + payload + CRC16:u16 + 0D 0A`。
所有多字节字段小端；CRC16-Modbus 初值 FFFF，覆盖 frame_id 至 payload。
Boot 最大 payload 为 128 字节。请求 type=01，payload 第一字节为 command。
帧 ID 非零，只有一个待应答请求；超时重传必须使用相同 ID 和相同 payload。

| 命令 | ID | command 后的 payload | 所在阶段 |
|---|---:|---|---|
| ENTER_BOOT | F0 | 8 字节 ASCII `NBLBOOT1` | App |
| INFO | F1 | 无 | Boot |
| BEGIN | F2 | target:u32, app_base:u32, image_length:u32, image_crc32:u32 | Boot |
| DATA | F3 | offset:u32, data:1..120 字节 | Boot |
| END | F4 | image_length:u32, image_crc32:u32 | Boot |
| RUN | F5 | target:u32 | Boot |

固定 product/layout target=`0x39430001`，app_base=`0x08004000`，最大镜像 112640 字节。
ENTER_BOOT 使用原 ACK/NACK（02/03），错误 40 表示业务状态拒绝，70 表示控制权拒绝，
71 表示参数错误，7F 表示单体/旧固件不支持。cookie 只防止误操作，不是身份认证。

Boot 回包 type=0C，复用请求 frame_id，固定 32 字节 payload：

| 偏移 | 字段 | 类型 |
|---:|---|---|
| 0 | 请求 command | u8 |
| 1 | error，0 为成功 | u8 |
| 2 | 协议版本，1 | u16 |
| 4 | target，0x39430001 | u32 |
| 8 | app_base，0x08004000 | u32 |
| 12 | app_capacity，112640 | u32 |
| 16 | next_offset，下一个期待的写入偏移 | u32 |
| 20 | valid，0/1 | u32 |
| 24 | 当前记录/升级的 image_length | u32 |
| 28 | 当前记录/升级的 image_crc32 | u32 |

错误码：1 请求格式，2 产品不匹配，3 地址/容量/对齐，4 升级状态，5 分块顺序/重复内容冲突，
6 Flash 操作失败，7 完整镜像 CRC 错误，8 启动向量错误。
损坏或 CRC16 错误的线帧丢弃，不触发擦写；不支持的业务命令不会执行治疗。

BEGIN 重试：同一活动事务的 ID、长度和 CRC 相同则只返回当前进度，不再擦除。
新的 BEGIN（新 ID）会重新擦除并从偏移 0 开始。
DATA 必须连续写入、offset 对齐 8 字节，非末块长度是 8 的倍数。
ACK 丢失后，Boot 对已写块比较实际 Flash，一致则返回成功，不重复编程 ECC doubleword。
实际编程失败后该事务失效，必须发新 BEGIN 从头升级。
END 完成后可重发，只有长度/CRC 与已提交记录匹配才成功。
RUN 只在有效镜像且无活动写入时接受，上位机只发一次，丢失应答后检查 App 实际启动。
参考超时：INFO/DATA 1 秒，BEGIN 8 秒，END 5 秒；幂等请求最多尝试 3 次。

## 升级包与有效记录

`.nfu` 包头为 32 字节：magic=`NBLUPD1\0`（8 字节），后接 6 个 u32：
format_version=1、target、app_base、image_length、image_crc32、header_crc32。
header_crc32 覆盖前 28 字节，随后紧接 image_length 字节原始 App BIN。
CRC32 使用 IEEE 反射多项式 EDB88320，与 Python `zlib.crc32` 一致。
这是完整性校验，不提供固件签名认证或版本防降级。

有效记录位于最后一个 Flash 页的前 40 字节：
magic=314D424E、format_version、length、crc、app_base、target（各 u32）；
再接覆盖前 24 字节的 header_crc32、reserved=0；最后一个 doubleword 是
commit=C35A6996 及其反码 3CA59669，必须最后写入。
Boot 每次复位都重新校验实际 App CRC，不只检查标记。
Boot 对 App/记录区的受控数据读取加入 ECC 错误处理，检测到不可纠正的读取错误时
使校验失败并保留升级入口；Boot 代码自身的异常仍进入停止状态。
此硬件路径需要实板断电测试确认，软件模拟不等同于真实 Flash ECC 故障。

## 中断恢复与验证

BEGIN 前中断通常保留旧 App；进入 Boot 但尚未擦除时，复位可返回旧 App。
BEGIN 后、END 提交前中断，Boot 不会启动不完整 App；重连并重新发送完整 `.nfu`。
不提供跨掉电的分块续传。END 后镜像有效，RUN 丢失也可通过复位启动。
Boot 本身损坏仍需 SWD 烧录恢复。

```powershell
python tools/run_tests.py
python tests/boot/test_upgrade_host.py
python tests/debug/test_debug_host.py
python tests/debug/test_daplink_transport.py
python tools/verify_firmware.py build/upgrade-release/nebulizer.elf
python tools/verify_firmware.py build/upgrade-debug/nebulizer.elf
```

测试覆盖真实 C 升级核心、边界、完整镜像 CRC、ACK 丢失重传、ECC 不重复写入、
每次擦写调用中断后的恢复、App 切换状态限制及 Python 上位机到 C Boot 的完整协议传输。
当前未烧录实板。验收还需验证串口接线、升级期间输出关闭、正常启动、保护区不被擦除、
擦除/传输/提交时断电后重刷，以及升级后的温控、雾化和参数恢复。

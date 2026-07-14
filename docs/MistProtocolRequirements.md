# 雾化板串口协议说明

## 1. 文档用途

本文档用于指导“雾化板从机固件”实现串口协议，不是给主控板写代码用的抽象说明。

通信对端是当前 `9C_Nebulizer_main` Zephyr 主控工程。你的雾化板固件必须按本文档实现，才能与当前主控正常联调。

适用对象：

- 主控板：STM32G070，使用 `USART4`
- 雾化板：你要编写的独立从机 MCU

通信目标：

- 设置雾化档位
- 启动雾化
- 停止雾化
- 心跳保活
- 状态查询
- 故障上报

## 2. 串口参数

- 波特率：`115200`
- 数据格式：`8N1`
- 协议类型：二进制协议

雾化板必须做到：

- 持续接收并解析二进制帧
- 及时回包
- 正确处理主控超时后的重复重发

## 2.1 PA7 引脚初始化与发送前处理要求

除串口协议本身外，雾化板从机固件还必须按以下顺序处理 `PA7`：

- 开机初始化阶段：
  - 先将 `PA7` 配置为 `GPIO 输出`
  - 立即将 `PA7` 拉高
  - 保持高电平 `500 ms`
  - 然后再将 `PA7` 切换为 `输入状态`
- 每次发送“开始命令”之前：
  - 必须先将 `PA7` 配置为 `GPIO 输出`
  - 必须先将 `PA7` 拉高
  - 保持高电平 `500 ms`
  - 然后再将 `PA7` 切换回 `输入状态`
  - 只有完成上述处理后，才允许继续发送“开始命令”

这里的“开始命令”是指雾化板从机固件内部用于启动雾化执行链路的发送动作，不是主控下发的串口命令编号。也就是说：

- 主控下发 `MIST_CMD_START` 后
- 雾化板从机在真正向本板内部驱动链路发出“开始”动作前
- 必须先完成一次 `PA7: 输出拉高 -> 切回输入` 的前处理

实现建议：

- 将上述逻辑封装为独立函数，例如 `mist_pa7_prepare_for_start()`
- 在系统初始化流程中调用一次
- 在每次执行启动发送路径前再次调用一次
- 不要把 `PA7` 长时间保持为输出高电平，按要求完成前处理后应恢复为输入状态

如果后续硬件需要明确最小拉高保持时间，请以原理图或硬件设计要求补充一个确定的延时参数；在未明确前，固件实现中应预留清晰注释和可配置延时点。

## 3. 主控当前真实行为

当前主控固件会执行以下行为：

- 周期发送：
  - `MIST_CMD_PING`，约每 `1 s`
  - `MIST_CMD_GET_STATUS`，约每 `1 s`
- 控制时发送：
  - `MIST_CMD_SET_LEVEL`
  - `MIST_CMD_START`
  - `MIST_CMD_STOP`
- 超时处理：
  - 单条命令超时：`150 ms`
  - 超时后用相同 `seq` 重发
  - 重试次数配置为 `3`
- 系统联动：
  - 如果雾化板不回包，主控判定雾化板离线
  - 如果雾化板上报非 0 故障码，主控判定雾化板故障

关键兼容要求：

- `STOP` 必须幂等
- `PING` 必须始终应答
- `GET_STATUS` 必须始终应答
- 相同 `seq` 的重复命令不能重复做危险动作

## 4. 外层总帧格式

雾化协议复用主工程的公共帧格式：

```text
AA 55 | frame_id uint16 LE | type uint8 | len uint16 LE | payload | crc16_modbus uint16 LE | 0D 0A
```

字段含义：

- `AA 55`：帧头
- `frame_id`：2 字节，小端
- `type`：帧类型
- `len`：payload 长度，小端
- `payload`：协议载荷
- `crc16_modbus`：CRC16 Modbus
- `0D 0A`：帧尾

CRC 覆盖范围：

- 包含：
  - `frame_id`
  - `type`
  - `len`
  - `payload`
- 不包含：
  - `AA 55`
  - `0D 0A`

## 5. 内层雾化 payload 格式

在总帧的 `payload` 内部，雾化协议再包一层：

```text
payload[0] = cmd_id
payload[1] = seq
payload[2..] = data
```

因此雾化协议最小字段为：

- `cmd_id u8`
- `seq u8`
- `data[0..N-1]`

当前主控实现中：

- `frame_id == seq`

兼容要求：

- 雾化板应正确解析 `frame_id` 和 `seq`
- 回包必须原样回显 `seq`
- 推荐回包时也让 `frame_id = seq`

## 6. 帧类型定义

主控当前使用的雾化帧类型如下：

```text
MIST_FRAME_TYPE_CMD    = 0x31
MIST_FRAME_TYPE_ACK    = 0x32
MIST_FRAME_TYPE_NACK   = 0x33
MIST_FRAME_TYPE_STATUS = 0x34
MIST_FRAME_TYPE_FAULT  = 0x35
```

含义：

- `CMD`：主控发给雾化板的命令
- `ACK`：雾化板成功响应
- `NACK`：雾化板拒绝或执行失败
- `STATUS`：雾化板状态响应
- `FAULT`：雾化板故障上报

## 7. 命令定义

主控当前会发送这些命令：

```text
MIST_CMD_PING       = 0x40
MIST_CMD_SET_LEVEL  = 0x41
MIST_CMD_START      = 0x42
MIST_CMD_STOP       = 0x43
MIST_CMD_GET_STATUS = 0x44
```

### 7.1 `MIST_CMD_PING = 0x40`

作用：

- 主控探测雾化板在线状态

请求格式：

```text
type = CMD
payload = [0x40, seq]
```

响应格式：

```text
type = ACK
payload = [0x40, seq, 0x00]
```

要求：

- 必须始终应答
- 应尽快回包
- 不要求雾化正在运行

### 7.2 `MIST_CMD_SET_LEVEL = 0x41`

作用：

- 设置雾化输出档位

请求格式：

```text
type = CMD
payload = [0x41, seq, level]
```

允许的 `level` 取值：

- `0 = OFF`
- `1 = LOW`
- `2 = MID`
- `3 = HIGH`

成功响应：

```text
type = ACK
payload = [0x41, seq, 0x00]
```

失败响应示例：

```text
type = NACK
payload = [0x41, seq, error_code]
```

要求：

- 此命令只设置目标档位
- 不要求自动启动雾化
- 相同 `seq` 的重复包不能导致危险重复写入

### 7.3 `MIST_CMD_START = 0x42`

作用：

- 启动雾化输出

请求格式：

```text
type = CMD
payload = [0x42, seq]
```

成功响应：

```text
type = ACK
payload = [0x42, seq, 0x00]
```

失败响应：

```text
type = NACK
payload = [0x42, seq, error_code]
```

推荐行为：

- 当前档位为 `OFF` 时，返回 `NACK(INVALID_LEVEL)`
- 已经在运行时，再收到 `START` 直接返回 `ACK`
- 当前处于故障态时，返回 `NACK(BAD_STATE)` 或 `NACK(SAFETY_LOCKED)`
- 在执行实际启动动作前，必须先执行一次 `PA7` 前处理：
  - `PA7` 配置为输出
  - `PA7` 拉高
  - 保持高电平 `500 ms`
  - `PA7` 切换回输入
  - 然后再继续发送或触发本板内部“开始命令”

### 7.4 `MIST_CMD_STOP = 0x43`

作用：

- 停止雾化输出

请求格式：

```text
type = CMD
payload = [0x43, seq]
```

响应格式：

```text
type = ACK
payload = [0x43, seq, 0x00]
```

强制要求：

- 必须幂等
- 已经停止时也要安全返回
- 重复收到也要安全返回
- 同一条重发命令也要安全返回
- 应快速 ACK

这是最关键的兼容命令。

### 7.5 `MIST_CMD_GET_STATUS = 0x44`

作用：

- 查询雾化板当前运行状态

请求格式：

```text
type = CMD
payload = [0x44, seq]
```

响应格式：

```text
type = STATUS
payload = [0x44, seq, running, actual_level, fault_code_low8]
```

这里必须注意：当前主控只按这个最小格式解析状态。

## 8. ACK / NACK / STATUS / FAULT 载荷格式

### 8.1 ACK 格式

请使用如下格式：

```text
type = 0x32
payload[0] = cmd_id
payload[1] = seq
payload[2] = 0x00
```

说明：

- 当前主控主要依赖 `seq` 匹配
- 仍然建议回显 `cmd_id`
- 第 3 字节固定写 `0x00`

### 8.2 NACK 格式

请使用如下格式：

```text
type = 0x33
payload[0] = cmd_id
payload[1] = seq
payload[2] = error_code
```

推荐 `error_code` 定义：

- `0x01 = UNKNOWN_CMD`
- `0x02 = INVALID_LEN`
- `0x03 = INVALID_LEVEL`
- `0x04 = BUSY`
- `0x05 = NOT_READY`
- `0x06 = INTERNAL_ERROR`
- `0x07 = SAFETY_LOCKED`
- `0x08 = UNSUPPORTED`
- `0x09 = BAD_STATE`

注意：

- 不要对重复 `STOP` 返回 `NACK`

### 8.3 STATUS 格式

为了兼容当前主控，`STATUS` 必须按下面格式组织：

```text
type = 0x34
payload[0] = cmd_id        // 一般为 0x44
payload[1] = seq
payload[2] = running
payload[3] = actual_level
payload[4] = fault_code_low8
```

字段定义：

- `running`
  - `0 = 未运行`
  - `1 = 运行中`
- `actual_level`
  - `0 = OFF`
  - `1 = LOW`
  - `2 = MID`
  - `3 = HIGH`
- `fault_code_low8`
  - 当前故障码低 8 位

重要说明：

- 当前主控从 `STATUS` 中只读 1 字节故障码
- 如果你内部故障码是 16 位，`STATUS` 里放低 8 位即可
- 无故障时写 `0x00`

### 8.4 FAULT 格式

请使用如下格式：

```text
type = 0x35
payload[0] = cmd_id
payload[1] = seq
payload[2] = fault_code_low
payload[3] = fault_code_high
```

含义：

- 完整 16 位故障码，小端

推荐用法：

- 雾化板发生严重故障时，可以主动发送 `FAULT`
- 如果是异步主动上报，可固定使用：
  - `cmd_id = 0x44`
  - `seq = 0x00`

## 9. 雾化档位定义

主控内部使用的雾化档位定义如下：

```text
0 = OFF
1 = LOW
2 = MID
3 = HIGH
```

雾化板必须完全使用相同映射。

## 10. 推荐雾化板故障码

建议使用以下 16 位故障码：

```text
0x0000 = NONE
0x0001 = DRIVER_OVERCURRENT
0x0002 = DRIVER_OVERTEMP
0x0003 = ATOMIZER_OPEN
0x0004 = ATOMIZER_SHORT
0x0005 = STARTUP_FAILED
0x0006 = UNDERVOLTAGE
0x0007 = OVERVOLTAGE
0x0008 = MCU_INTERNAL
0x0009 = UNKNOWN
```

如果你已有内部故障表，也可以继续使用，但要满足：

- `STATUS` 返回低 8 位
- `FAULT` 返回完整 16 位

## 11. 重复包处理要求

这是强制要求。

主控命令超时后会使用相同 `seq` 重发。

雾化板必须：

- 识别重复的 `cmd_id + seq`
- 不重复执行危险动作
- 尽量重发与第一次一致的响应

推荐实现：

- 缓存：
  - `last_cmd_id`
  - `last_seq`
  - `last_response_frame`
- 如果收到相同的 `cmd_id + seq`，直接重发缓存响应

重点关注：

- `START`
- `STOP`
- `SET_LEVEL`

## 12. 时序要求

主控单条命令超时为：

- `150 ms`

因此雾化板应做到：

- 尽快解析命令
- 最好在 `20 ms` 内回包
- 不要故意阻塞到 `150 ms` 之后

否则主控会重发，并可能最终判定雾化板离线。

## 13. 主控对雾化板的业务期望

这些不是线协议字段，但会影响你从机实现。

### 13.1 在线门槛

主控只有在雾化板在线时才允许治疗进入运行态。

所以雾化板上电后必须尽快响应：

- `PING`
- `GET_STATUS`

### 13.2 STOP 是安全关键命令

主控会在这些场景强制发 `STOP`：

- 用户手动 STOP
- PAUSE
- DONE
- FAULT
- 安全停机

所以你的实现必须：

- 先快速关闭雾化输出
- 再快速 ACK
- 容忍重复 STOP

### 13.3 故障上报

如果雾化板通过 `STATUS` 或 `FAULT` 上报非 0 故障码：

- 主控会把系统判定为雾化板故障

### 13.4 离线判定

如果雾化板不回复：

- 主控会把系统判定为雾化板离线

## 14. 推荐雾化板内部状态机

建议内部最少包含以下状态：

- `IDLE`
- `READY`
- `RUNNING`
- `FAULT`

推荐行为：

- `IDLE/READY`
  - 接受 `PING`
  - 接受 `GET_STATUS`
  - 接受 `SET_LEVEL`
  - 接受 `START`
  - 接受 `STOP`
- `RUNNING`
  - 接受全部命令
- `FAULT`
  - 仍然接受 `PING`
  - 仍然接受 `GET_STATUS`
  - 仍然接受 `STOP`
  - 拒绝 `START`

## 15. 推荐兼容策略

如果当前首要目标是尽快和主控跑通，建议按如下策略实现：

- `PING` -> `ACK`
- `SET_LEVEL` -> `ACK` 或 `NACK(INVALID_LEVEL)`
- `START` -> `ACK` 或 `NACK`
- `STOP` -> 永远 `ACK`
- `GET_STATUS` -> `STATUS`
- 故障存在时：
  - `STATUS.fault_code_low8 != 0`
  - 可额外主动发送 `FAULT`

这套策略与当前主控兼容性最好。

## 16. 请求/响应示例

### 16.1 PING

请求：

```text
type = 0x31
payload = [0x40, seq]
```

响应：

```text
type = 0x32
payload = [0x40, seq, 0x00]
```

### 16.2 SET_LEVEL(MID)

请求：

```text
type = 0x31
payload = [0x41, seq, 0x02]
```

响应：

```text
type = 0x32
payload = [0x41, seq, 0x00]
```

### 16.3 GET_STATUS，运行中，中档，无故障

请求：

```text
type = 0x31
payload = [0x44, seq]
```

响应：

```text
type = 0x34
payload = [0x44, seq, 0x01, 0x02, 0x00]
```

含义：

- 运行中
- 当前档位 MID
- 无故障

### 16.4 GET_STATUS，已停止，故障码 `0x0003`

响应：

```text
type = 0x34
payload = [0x44, seq, 0x00, 0x00, 0x03]
```

可选再主动发一帧：

```text
type = 0x35
payload = [0x44, 0x00, 0x03, 0x00]
```

表示完整故障码 `0x0003`

## 17. 雾化板固件实现清单

你至少要实现以下内容：

- UART 收发
- 外层总帧编解码
- CRC16 Modbus
- 雾化内层 payload 编解码
- 命令处理：
  - `PING`
  - `SET_LEVEL`
  - `START`
  - `STOP`
  - `GET_STATUS`
- 响应构造：
  - `ACK`
  - `NACK`
  - `STATUS`
  - `FAULT`
- 基于 `cmd_id + seq` 的重复命令检测
- 幂等 `STOP`
- 能稳定响应主控周期轮询

## 18. 五条最关键兼容规则

如果只记住五条，就记住下面五条：

1. 外层帧必须是：
   - `AA55 + frame_id LE + type + len LE + payload + CRC16Modbus + 0D0A`
2. 雾化 payload 必须是：
   - `cmd_id + seq + data`
3. `GET_STATUS` 的响应必须是：
   - `STATUS`
   - 并至少带：
     - `running`
     - `actual_level`
     - `fault_code_low8`
4. `STOP` 必须永远安全、永远可重复、永远快速 ACK
5. 回包时间必须明显小于 `150 ms`

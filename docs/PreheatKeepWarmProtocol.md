# 热雾预热与治疗后保温协议

本文档提供给上位机软件工程师。所有多字节字段均为小端序；串口帧封装、CRC 和
`frame_id` 规则沿用 `HostProtocolReference.md`。

## 1. 上位机需要发送的命令

### 1.1 设置治疗模式（已有命令）

- 命令：`HOST_CMD_SET_MODE = 0x10`
- 数据：`mode u8`
  - `0`：热雾
  - `1`：冷雾
- 返回：`ACK` 或 `NACK`

### 1.2 设置持续保温状态（新增命令）

- 命令：`HOST_CMD_SET_KEEP_WARM = 0x15`
- 数据：`enable u8`
  - `0`：关闭保温
  - `1`：开启保温，空闲时立即维持 PA5 到固件设定温度（当前 65.0°C）
- 返回：`ACK` 或 `NACK`
- 非法值：返回 `NACK`，错误码 `14`
- 参数在命令成功后延迟约 1 秒写入 I2C EEPROM，掉电后继续有效。

该开关表示持续保温意图，不只是“治疗结束后的选项”：

- 开机从 EEPROM 恢复为 `1` 时，不依赖冷热模式，完成自检后立即进入 `KEEP_WARM`。
- 在 `READY / CONFIGURING / DONE` 状态收到 `1` 时立即进入 `KEEP_WARM`。
- 热雾治疗期间由治疗温控接管，治疗结束或手动停止后重新进入 `KEEP_WARM`。
- 收到 `0` 时停止保温并回到 `READY`。
- 切换冷雾时，上位机必须发送 `SET_KEEP_WARM(0)`；冷雾不执行保温。
- 上位机计划保温两小时，可在两小时到达时发送 `SET_KEEP_WARM(0)` 结束保温。

### 1.3 读取当前配置（已有命令，返回值扩展）

- 命令：`HOST_CMD_GET_CONFIG = 0x27`
- 数据：无
- 返回：先返回 `CONFIG` 帧，再返回 `ACK`

`CONFIG` 帧类型为 `0x06`，Payload 从 7 字节扩展为 8 字节：

| 偏移 | 字段 | 类型 | 说明 |
|---:|---|---|---|
| 0 | `mode` | `u8` | `0` 热雾，`1` 冷雾 |
| 1 | `target_temp_deci_c` | `u16 LE` | 出雾口目标温度，单位 0.1°C |
| 3 | `duration_sec` | `u16 LE` | 正式治疗时间，单位秒 |
| 5 | `air_level` | `u8` | 风量档位 |
| 6 | `mist_level` | `u8` | 雾量档位 |
| 7 | `keep_warm_enabled` | `u8` | `0` 关闭，`1` 开启 |

### 1.4 开始治疗（已有命令，语义调整）

- 命令：`HOST_CMD_START = 0x20`
- 数据：无
- 返回：`ACK` 表示本次启动请求已被下位机接受；它不再等同于“正式治疗已经开始”。

热雾模式下：

1. PB11 出雾口温度低于 30.0°C 时进入预热状态，不启动治疗倒计时。
2. PB11 达到 30.0°C 后进入正式热雾治疗，倒计时才从完整设定时间开始递减。
3. 上位机必须以 `EVENT/TREATMENT_STARTED` 作为正式开始依据，不应以 `START ACK`
   作为正式开始依据。

冷雾模式不需要预热，`START` 被接受后直接进入正式治疗并发送正式开始事件。

## 2. 下位机主动事件帧

- 帧类型：`HOST_FRAME_TYPE_EVENT = 0x05`
- Payload 长度：12 字节

| 偏移 | 字段 | 类型 | 说明 |
|---:|---|---|---|
| 0 | `event_id` | `u8` | 事件编号，见下表 |
| 1 | `mode` | `u8` | `0` 热雾，`1` 冷雾 |
| 2 | `state` | `u8` | 事件发生后的下位机状态 |
| 3 | `keep_warm_enabled` | `u8` | 当前保温开关 |
| 4 | `remaining_sec` | `u32 LE` | 剩余正式治疗时间 |
| 8 | `outlet_temp_deci_c` | `i16 LE` | PB11 出雾口温度，单位 0.1°C |
| 10 | `kettle_temp_deci_c` | `i16 LE` | PA5 锅体温度，单位 0.1°C |

事件编号：

| `event_id` | 名称 | 上位机处理建议 |
|---:|---|---|
| `0x01` | `PREHEAT_STARTED` | 显示“预热中”，不要启动治疗计时 |
| `0x02` | `TREATMENT_STARTED` | 显示“治疗中”，从此刻开始正式计时 |
| `0x03` | `TREATMENT_FINISHED` | 显示治疗完成，停止治疗计时 |
| `0x04` | `KEEP_WARM_STARTED` | 显示“保温中” |
| `0x05` | `KEEP_WARM_STOPPED` | 清除“保温中”状态 |

事件帧属于主动上报，不需要上位机回复 ACK。建议上位机同时继续解析周期 `STATUS`
帧，事件丢失时可通过状态字段恢复界面。

## 3. 新增状态值

原状态值 `0..8` 保持不变，新增状态只追加在末尾：

| 状态值 | 名称 | 含义 |
|---:|---|---|
| `9` | `PREHEATING` | 热雾预热，执行器运行但正式治疗倒计时不动 |
| `10` | `KEEP_WARM` | 持续保温开启，仅维持 PA5 锅体到固件设定温度 |

`STATUS` Payload 在原 20 字节末尾增加：

- 偏移 `20`：`keep_warm_enabled u8`

`RUNTIME` Payload 在原 91 字节末尾增加：

- 偏移 `91`：`keep_warm_enabled u8`

## 4. 推荐上位机调用顺序

开启热雾持续保温：

1. `SET_MODE(0)`
2. `SET_KEEP_WARM(1)`
3. 收到 `KEEP_WARM_STARTED` 后显示“保温中”；不开始治疗也会持续保温
4. 需要治疗时设置温度、时间、风量和雾量，然后发送 `START`
5. 收到 `START ACK` 后显示“启动已接受”
6. 收到 `PREHEAT_STARTED` 后显示“预热中”
7. 收到 `TREATMENT_STARTED` 后才显示“治疗中”并开始界面计时
8. 收到 `TREATMENT_FINISHED` 后显示“治疗完成”
9. 收到 `KEEP_WARM_STARTED` 后重新显示“保温中”
10. 保温两小时后发送 `SET_KEEP_WARM(0)`；收到 ACK/`KEEP_WARM_STOPPED` 后显示空闲

项目自带 PyQt 上位机已实现上述两小时一次性计时。计时从上位机首次收到/查询到
`KEEP_WARM` 状态开始；如果上位机断开或重启，计时会重新开始。需要在下位机断电、
上位机离线时也精确累计两小时，则还需另行把保温起始时间持久化到下位机。

冷雾：

1. `SET_MODE(1)`
2. 建议发送 `SET_KEEP_WARM(0)`
3. `START`
4. 收到 `TREATMENT_STARTED` 后开始界面计时
5. 收到 `TREATMENT_FINISHED` 后结束，不进入保温

热雾且保温开关为 `1` 时，`STOP (0x23)` 会停止预热/正式治疗并回到 `KEEP_WARM`。
真正结束保温必须发送 `SET_KEEP_WARM(0)`。

## 5. 下位机温控参数

- 热雾预热判定：PB11 `>= 30.0°C`
- 预热阶段：PB11 低于 30.0°C 时使用 100% 加热功率
- 预热风扇：至少低档运行（40%）；风量设为关闭时也保持低档，中/高档按所选档位运行。开盖、暂停和故障联锁优先停机，不修改保存的风量设置。
- 正式热疗：沿用 PB11 出口 PID，最终输出上限 20%
- 持续保温：以 PA5 为反馈，目标由 `APP_KEEP_WARM_TARGET_DECI_C` 设置，当前 65.0°C，比例控制输出上限 20%
- 保温阶段：风扇关闭、雾化关闭，仅锅体加热控制运行

## 6. EEPROM 默认硬件参数

当前固件按常见 24C02 参数访问板载 EEPROM：

- I2C2
- 7-bit 从地址：`0x50`
- 8-bit 字地址
- 单字节写入后等待 5 ms

如果实际 EEPROM 型号使用 16-bit 字地址，只需把
`APP_SETTINGS_EEPROM_ADDR_WIDTH_BYTES` 改为 `2`；如果器件地址不同，则修改
`APP_SETTINGS_EEPROM_I2C_ADDR`。

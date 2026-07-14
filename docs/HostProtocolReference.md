# Nebulizer 主控板与上位机通信协议说明

本文档基于当前工程源码整理，面向上位机开发、联调和维护人员，说明主控板与上位机之间的串口协议、帧格式、命令集合、数据单位、参数范围、读写规则与典型通信流程。

适用代码基线：

- `include/nebulizer/protocol_ids.h`
- `protocols/common/frame_codec.[ch]`
- `protocols/host/host_protocol.[ch]`
- `src/app/app_tasks.c`
- `include/nebulizer/app_config.h`
- `services/heat/heat_control.c`
- `services/storage/settings_defaults.c`
- `services/communication/host_comm_service.c`

## 1. 总览

主控板通过 `USART1` 与上位机通信。协议采用固定前导/尾随字节、长度字段和 `CRC16-Modbus` 校验，支持：

- 参数设置：模式、目标温度、治疗时间、风量、雾量、PID 参数
- 过程控制：启动、暂停、恢复、停止、清故障
- 状态查询：简版状态、完整运行时、配置、PID、维护模式
- 保活：心跳
- 维护模式：手动控制风扇、雾化、加热

串口链路约定：

- 物理接口：`USART1`
- 当前波特率：`115200`
- 推荐格式：`8N1`
- 主控日志默认走 `USART2`，上位机应直接打开 `USART1` 原始串口，不要当调试控制台使用

## 2. 帧格式

### 2.1 通用帧结构

所有主控-上位机帧都使用同一层编解码格式：

| 偏移 | 长度 | 字段 | 类型 | 说明 |
|---|---:|---|---|---|
| 0 | 1 | `preamble0` | `u8` | 固定 `0xAA` |
| 1 | 1 | `preamble1` | `u8` | 固定 `0x55` |
| 2 | 2 | `frame_id` | `u16 LE` | 帧号，小端 |
| 4 | 1 | `type` | `u8` | 帧类型 |
| 5 | 2 | `payload_len` | `u16 LE` | 负载长度 |
| 7 | N | `payload` | `byte[N]` | 负载内容 |
| 7 + N | 2 | `crc16` | `u16 LE` | `CRC16-Modbus` |
| 9 + N | 1 | `trailer0` | `u8` | 固定 `0x0D` |
| 10 + N | 1 | `trailer1` | `u8` | 固定 `0x0A` |

固定字节：

- 前导：`AA 55`
- 尾随：`0D 0A`
- 固定开销：`11` 字节
- 当前主控侧最大负载：`128` 字节

### 2.2 CRC 规则

`CRC16-Modbus` 覆盖以下字段：

- `frame_id[2]`
- `type[1]`
- `payload_len[2]`
- `payload[N]`

不包含：

- 前导 `AA 55`
- 尾随 `0D 0A`

### 2.3 字节序和有符号规则

- 所有多字节整数字段均为小端
- 温度字段通常为 `i16`，单位 `0.1°C`
- 时间字段通常为秒，`STATUS` 用 `u16`，`RUNTIME` 用 `u32`
- 功率通常用 `permille`，即 `0..1000` 表示 `0%..100%`

## 3. 帧类型

当前定义如下：

| 名称 | 值 | 方向 | 说明 |
|---|---:|---|---|
| `HOST_FRAME_TYPE_CMD` | `0x01` | 上位机 -> 主控 | 命令请求 |
| `HOST_FRAME_TYPE_ACK` | `0x02` | 主控 -> 上位机 | 成功确认 |
| `HOST_FRAME_TYPE_NACK` | `0x03` | 主控 -> 上位机 | 失败确认 |
| `HOST_FRAME_TYPE_STATUS` | `0x04` | 主控 -> 上位机 | 简版状态 |
| `HOST_FRAME_TYPE_EVENT` | `0x05` | 保留 | 当前工程未使用 |
| `HOST_FRAME_TYPE_CONFIG` | `0x06` | 主控 -> 上位机 | 配置快照 |
| `HOST_FRAME_TYPE_PID` | `0x07` | 主控 -> 上位机 | PID 参数 |
| `HOST_FRAME_TYPE_RUNTIME` | `0x08` | 主控 -> 上位机 | 完整运行时快照 |
| `HOST_FRAME_TYPE_MAINT` | `0x09` | 主控 -> 上位机 | 维护模式状态 |

## 4. `frame_id` 使用规则

`frame_id` 在当前实现中有两种来源：

- 上位机发送 `CMD` 时，自行递增生成
- 主控发送 `STATUS/CONFIG/PID/RUNTIME/MAINT` 时，使用主控内部自增帧号，起始值为 `0x8000`

这意味着：

- `ACK/NACK` 会回显上位机请求的 `frame_id`
- 数据帧不会回显请求的 `frame_id`
- 上位机不能用数据帧的 `frame_id` 去直接关联某个请求
- 查询命令的完成判定应以“收到目标数据帧 + 收到对应 ACK”为准

## 5. 请求负载格式

主机发送的 `CMD` 帧负载格式固定为：

| 偏移 | 长度 | 字段 | 说明 |
|---|---:|---|---|
| 0 | 1 | `command_id` | 命令号 |
| 1..N | 变长 | `data` | 命令参数 |

主控接收时只接受：

- `type == 0x01`
- `payload_len >= 1`

## 6. ACK / NACK 格式

主控返回确认帧时，负载固定 4 字节：

| 偏移 | 长度 | 字段 | 类型 | 说明 |
|---|---:|---|---|---|
| 0 | 1 | `command_id` | `u8` | 对应请求命令号 |
| 1 | 1 | `ok` | `u8` | `1=成功`，`0=失败` |
| 2 | 1 | `error_code` | `u8` | 错误码；成功时为 `0` |
| 3 | 1 | `reserved` | `u8` | 保留，当前固定 `0` |

其中：

- 成功时，帧类型是 `ACK (0x02)`
- 失败时，帧类型是 `NACK (0x03)`

## 7. 命令分类与命令清单

### 7.1 配置写入类

| 命令 | 值 | 请求负载 | 作用 | 成功返回 |
|---|---:|---|---|---|
| `HOST_CMD_SET_MODE` | `0x10` | `mode u8` | 设置治疗模式 | `ACK` |
| `HOST_CMD_SET_TARGET_TEMP` | `0x11` | `target_temp_deci_c u16 LE` | 设置目标温度 | `ACK` |
| `HOST_CMD_SET_TIME` | `0x12` | `time_min u16 LE` | 设置治疗时间，单位分钟 | `ACK` |
| `HOST_CMD_SET_AIR_LEVEL` | `0x13` | `air_level u8` | 设置风量档位 | `ACK` |
| `HOST_CMD_SET_MIST_LEVEL` | `0x14` | `mist_level u8` | 设置雾量档位 | `ACK` |
| `HOST_CMD_SET_PID` | `0x29` | `kp i32 LE + ki i32 LE + kd i32 LE + i_limit i32 LE` | 设置 PID 参数 | 先 `PID`，后 `ACK` |

### 7.2 运行控制类

| 命令 | 值 | 请求负载 | 作用 | 成功返回 |
|---|---:|---|---|---|
| `HOST_CMD_START` | `0x20` | 无 | 启动治疗 | `ACK` |
| `HOST_CMD_PAUSE` | `0x21` | 无 | 暂停治疗 | `ACK` |
| `HOST_CMD_RESUME` | `0x22` | 无 | 恢复治疗 | `ACK` |
| `HOST_CMD_STOP` | `0x23` | 无 | 停止治疗并回到 `READY` | `ACK` |
| `HOST_CMD_HEARTBEAT` | `0x25` | 无 | 心跳保活 | `ACK` |
| `HOST_CMD_CLEAR_FAULT` | `0x26` | 无 | 清除故障 | `ACK` |

### 7.3 查询类

| 命令 | 值 | 请求负载 | 作用 | 成功返回 |
|---|---:|---|---|---|
| `HOST_CMD_GET_STATUS` | `0x24` | 无 | 获取简版状态 | 先 `STATUS`，后 `ACK` |
| `HOST_CMD_GET_CONFIG` | `0x27` | 无 | 获取当前配置 | 先 `CONFIG`，后 `ACK` |
| `HOST_CMD_GET_PID` | `0x28` | 无 | 获取当前 PID | 先 `PID`，后 `ACK` |
| `HOST_CMD_GET_RUNTIME` | `0x2A` | 无 | 获取完整运行时 | 先 `RUNTIME`，后 `ACK` |
| `HOST_CMD_GET_MAINTENANCE` | `0x30` | 无 | 获取维护模式状态 | 先 `MAINT`，后 `ACK` |

### 7.4 维护模式类

| 命令 | 值 | 请求负载 | 作用 | 成功返回 |
|---|---:|---|---|---|
| `HOST_CMD_ENTER_MAINTENANCE` | `0x2B` | 无 | 进入维护模式 | 先 `MAINT`，后 `ACK` |
| `HOST_CMD_EXIT_MAINTENANCE` | `0x2C` | 无 | 退出维护模式 | 先 `MAINT`，后 `ACK` |
| `HOST_CMD_MANUAL_SET_FAN` | `0x2D` | `fan_level u8` | 维护模式手动风扇 | 先 `MAINT`，后 `ACK` |
| `HOST_CMD_MANUAL_SET_MIST` | `0x2E` | `mist_level u8` | 维护模式手动雾化 | 先 `MAINT`，后 `ACK` |
| `HOST_CMD_MANUAL_SET_HEAT` | `0x2F` | `heat_output_permille u16 LE` | 维护模式手动加热功率 | 先 `MAINT`，后 `ACK` |

## 8. 命令参数、默认值、范围与注意事项

### 8.1 模式 `mode`

定义：

- `0 = TREATMENT_MODE_HOT`
- `1 = TREATMENT_MODE_COLD`

默认值：

- `HOT`

校验规则：

- 大于 `1` 会返回错误码 `1`

注意事项：

- `mode` 只影响治疗逻辑
- 不是所有状态下都能立即触发状态变化；真正进入运行态仍需 `START`

### 8.2 目标温度 `target_temp_deci_c`

请求单位：

- `u16`
- 单位 `0.1°C`
- 例如 `42.0°C` 应发送 `420`，小端为 `A4 01`

默认值：

- `420`，即 `42.0°C`

当前上限：

- `APP_MAX_TARGET_TEMP_DECI_C = 450`
- 即 `45.0°C`

当前下限：

- 主控代码没有显式下限
- `0` 也会通过协议层校验
- 上位机应自行限制一个合理最小值，避免把目标温度设得过低

错误码：

- 超过 `45.0°C` 或负载长度不对时返回 `2`

### 8.3 治疗时间 `SET_TIME`

请求单位：

- 上位机发送 `u16` 分钟数
- 主控内部保存为秒：`duration_sec = value_min * 60`

默认值：

- `600` 秒，即 `10` 分钟

当前允许范围：

- `1..30` 分钟

错误码：

- 长度不对，或值不在 `1..30` 时返回 `3`

注意事项：

- `CONFIG` 帧中返回的是秒，不是分钟
- `STATUS` 和 `RUNTIME` 中剩余时间也都是秒

### 8.4 风量 `air_level`

枚举定义：

- `0 = OFF`
- `1 = LOW`
- `2 = MID`
- `3 = HIGH`

默认值：

- `MID`

错误码：

- 大于 `3` 返回 `4`

当前风机 PWM 标定：

- `LOW = 35%`
- `MID = 65%`
- `HIGH = 100%`

注意事项：

- 这是配置档位，不是实时 PWM 百分比
- 实时输出建议从 `RUNTIME` 里结合状态和 `fan_rpm` 一起看

### 8.5 雾量 `mist_level`

枚举定义：

- `0 = OFF`
- `1 = LOW`
- `2 = MID`
- `3 = HIGH`

默认值：

- `MID`

错误码：

- 大于 `3` 返回 `5`

注意事项：

- `SET_MIST_LEVEL` 在运行中会立即调用雾化板目标更新
- `STATUS` 返回的是配置档位，`RUNTIME` 会同时给出雾化板期望档位和实际档位

### 8.6 PID 参数

请求负载顺序：

| 偏移 | 字段 | 类型 | 单位 |
|---|---|---|---|
| 0 | `kp_milli` | `i32 LE` | 放大 1000 倍 |
| 4 | `ki_milli` | `i32 LE` | 放大 1000 倍 |
| 8 | `kd_milli` | `i32 LE` | 放大 1000 倍 |
| 12 | `integral_limit_permille` | `i32 LE` | `permille` |

默认值：

- `Kp = 12000`，显示时可按 `12.000`
- `Ki = 120`，显示时可按 `0.120`
- `Kd = 0`
- `Integral Limit = 450`

允许范围：

- `Kp: 0..120000`
- `Ki: 0..10000`
- `Kd: 0..5000`
- `Integral Limit: 0..1000`

错误码：

- 负载长度不足 `16` 字节，或任一参数超范围时返回 `6`

显示换算建议：

- 上位机显示 `Kp/Ki/Kd` 时，应使用 `value / 1000.0`
- `integral_limit_permille = 450` 可显示为 `45.0%`

注意事项：

- 当前 PID 都是非负数；负值会被拒绝
- `GET_PID` 返回的 `triac_min_delay_us`、`triac_max_delay_us` 是只读硬件边界，不支持通过协议写入

### 8.7 温控与功率限制相关配置

#### 8.7.1 目标温度附近的功率封顶

主控在 PID 输出之后，会再做一次“近目标温度限幅”，对最终加热功率做上限裁剪。

当前代码实际数值：

| 条件 | 温差阈值 | 最大输出 |
|---|---|---|
| `error <= APP_HEAT_NEAR_TARGET_BAND1_DECI_C` | `20`，即 `2.0°C` | `APP_HEAT_NEAR_TARGET_MAX1_PERCENT = 5%` |
| `error <= APP_HEAT_NEAR_TARGET_BAND2_DECI_C` | `30`，即 `3.0°C` | `APP_HEAT_NEAR_TARGET_MAX2_PERCENT = 20%` |
| `error <= APP_HEAT_NEAR_TARGET_BAND3_DECI_C` | `100`，即 `10.0°C` | `APP_HEAT_NEAR_TARGET_MAX3_PERCENT = 100%` |

实现含义：

- 这不是“把 PID 输出再乘一个百分比”
- 而是“把最终允许输出限制到某个上限”
- 例如 PID 算出 `600 permille`，但当前误差只剩 `2.0°C`，最终会被裁到 `50 permille`

重要注意：

- `include/nebulizer/app_config.h` 中这几项的注释文字目前与数值已经不一致
- 协议文档和上位机逻辑应以宏的实际数值为准，不要以注释为准

#### 8.7.2 锅体 65°C 加热切断

当前项目已增加“锅体达到或超过 `65.0°C` 时暂停加热”的逻辑：

- 阈值：`APP_KETTLE_HEAT_CUTOFF_DECI_C = 650`
- 行为：立即停止加热，PID 输出清零
- 恢复：锅体温度回到 `65.0°C` 以下后自动恢复 PID 加热
- 特点：这不是整机故障，不影响风扇、雾化和其他功能

#### 8.7.3 故障级过温保护

当前仍保留更高等级的故障阈值：

- 出口温度 `>= 46.0°C`：`FAULT_OUTLET1_OVER_TEMP`
- 锅体温度 `>= 120.0°C`：`FAULT_KETTLE_OVER_TEMP`

这两类属于故障保护，会进入整机故障流程。

### 8.8 维护模式参数

维护模式结构：

- `active`
- `fan_level`
- `mist_level`
- `heat_output_permille`

进入条件：

- 当前状态必须为 `READY / PAUSED / DONE`
- 当前无系统故障

手动写入范围：

- `MANUAL_SET_FAN`: `0..3`
- `MANUAL_SET_MIST`: `0..3`
- `MANUAL_SET_HEAT`: `0..1000 permille`

错误码：

- `ENTER_MAINTENANCE` 条件不满足：`7`
- 手动风扇非法或未处于维护模式：`8`
- 手动雾化非法或未处于维护模式：`9`
- 手动加热非法或未处于维护模式：`10`

注意事项：

- `RUNTIME` 帧中维护加热功率是 `u8 div10`，即需要再乘 `10`
- `MAINT` 帧中维护加热功率是完整 `u16 permille`

## 9. 状态机相关的命令接受条件

### 9.1 `START`

`START` 在以下条件全部满足时才会成功：

- 当前状态是 `READY / CONFIGURING / DONE`
- 雾化板在线
- 系统无故障
- 雾化板未报缺水
- 雾化板未锁定
- 盖子已关闭
- 未处于维护模式
- 启动前 OTP reset 成功

失败时常见错误码：

- `0x42` 盖子打开
- `0x43` 雾化板缺水
- `0x44` 雾化板离线
- `0x45` 雾化板安全锁定
- `0x41` 启动前 OTP reset 失败
- `0x40` 状态机拒绝

### 9.2 `RESUME`

`RESUME` 需要：

- 当前状态是 `PAUSED`
- 雾化板在线
- 系统无故障
- 雾化板无缺水
- 雾化板未锁定
- 盖子已关闭
- 未处于维护模式

如果维护模式激活，当前实现会直接返回错误码 `11`。

### 9.3 `STOP`

`STOP` 只要当前不在 `BOOT / SELF_TEST`，就会回到 `READY`。

### 9.4 `PAUSE`

`PAUSE` 只有在 `RUNNING_HOT / RUNNING_COLD` 中才会真正切到 `PAUSED`，否则状态机会拒绝。

### 9.5 `CLEAR_FAULT`

只有当前状态为 `FAULT` 时，`CLEAR_FAULT` 才会成功把系统带回 `READY`。

## 10. 查询类返回数据帧详解

### 10.1 `STATUS` 帧

用途：

- 周期性上报
- `GET_STATUS` 查询返回
- 状态切换、雾化状态变化时也可能附带主动发送

主控当前周期上报节奏：

- `telemetry_service` 每 `500 ms` 主动发送一次 `STATUS`

负载长度：

- 固定 `20` 字节

字段表：

| 偏移 | 字段 | 类型 | 单位 | 说明 |
|---|---|---|---|---|
| 0 | `state` | `u8` | - | `treatment_state_t` |
| 1 | `remaining_sec` | `u16 LE` | 秒 | 剩余治疗时间 |
| 3 | `target_temp_deci_c` | `u16 LE` | `0.1°C` | 目标温度 |
| 5 | `outlet_temp_deci_c` | `i16 LE` | `0.1°C` | 出口温度 |
| 7 | `kettle_temp_deci_c` | `i16 LE` | `0.1°C` | 锅体温度 |
| 9 | `air_level` | `u8` | 枚举 | 风量配置档位 |
| 10 | `mist_level` | `u8` | 枚举 | 雾量配置档位 |
| 11 | `liquid_present` | `u8` | bool | 液位检测 |
| 12 | `cover_closed` | `u8` | bool | 盖子闭合 |
| 13 | `fault_code` | `u8` | 枚举值 | 系统故障码低 8 位 |
| 14 | `mist_online` | `u8` | bool | 雾化板在线 |
| 15 | `mist_fault_code` | `u16 LE` | 枚举值 | 雾化板故障码 |
| 17 | `mist_running` | `u8` | bool | 雾化板运行状态 |
| 18 | `heartbeat_ok` | `u8` | bool | 心跳是否有效 |
| 19 | `mode` | `u8` | 枚举 | 热/冷模式 |

注意事项：

- `STATUS.fault_code` 当前是 `u8`，不是 `u16`
- 当前项目里的故障码枚举值都落在 `0..255`，因此暂时没有截断问题
- 若将来扩展故障码到 `>255`，上位机应优先以 `RUNTIME.fault_code` 为准

### 10.2 `CONFIG` 帧

负载长度：

- 固定 `7` 字节

字段表：

| 偏移 | 字段 | 类型 | 单位 | 说明 |
|---|---|---|---|---|
| 0 | `mode` | `u8` | 枚举 | 热/冷模式 |
| 1 | `target_temp_deci_c` | `u16 LE` | `0.1°C` | 目标温度 |
| 3 | `duration_sec` | `u16 LE` | 秒 | 治疗时长 |
| 5 | `air_level` | `u8` | 枚举 | 风量档位 |
| 6 | `mist_level` | `u8` | 枚举 | 雾量档位 |

### 10.3 `PID` 帧

负载长度：

- 固定 `20` 字节

字段表：

| 偏移 | 字段 | 类型 | 单位 | 说明 |
|---|---|---|---|---|
| 0 | `kp_milli` | `i32 LE` | x1000 | `Kp * 1000` |
| 4 | `ki_milli` | `i32 LE` | x1000 | `Ki * 1000` |
| 8 | `kd_milli` | `i32 LE` | x1000 | `Kd * 1000` |
| 12 | `integral_limit_permille` | `i32 LE` | permille | 积分限幅 |
| 16 | `triac_min_delay_us` | `u16 LE` | 微秒 | 只读 |
| 18 | `triac_max_delay_us` | `u16 LE` | 微秒 | 只读 |

### 10.4 `RUNTIME` 帧

用途：

- `GET_RUNTIME` 查询返回
- 某些状态变化时会与 `STATUS` 一起主动发送

负载长度：

- `46` 字节：基础版
- `66` 字节：附带 PID 运行调试参数

当前工程实际值：

- `APP_HOST_RUNTIME_PID_PARAMS_ENABLE = 1`
- 因此当前构建下实际发送 `66` 字节

基础字段表：

| 偏移 | 字段 | 类型 | 单位 | 说明 |
|---|---|---|---|---|
| 0 | `state` | `u8` | - | 当前状态 |
| 1 | `remaining_sec` | `u32 LE` | 秒 | 剩余时间 |
| 5 | `fault_code` | `u16 LE` | 枚举值 | 系统故障码 |
| 7 | `heartbeat_ok` | `u8` | bool | 心跳有效 |
| 8 | `ntc1` | `i16 LE` | `0.1°C` | 出口1 |
| 10 | `ntc2` | `i16 LE` | `0.1°C` | 锅体 |
| 12 | `ntc3` | `i16 LE` | `0.1°C` | 出口2 |
| 14 | `ntc4` | `i16 LE` | `0.1°C` | 备用 |
| 16 | `liquid_present` | `u8` | bool | 液位状态 |
| 17 | `cover_closed` | `u8` | bool | 盖子状态 |
| 18 | `gx1832_active` | `u8` | bool | GX1832 输入 |
| 19 | `fan_rpm` | `u16 LE` | RPM | 风扇转速 |
| 21 | `mist_online` | `u8` | bool | 雾化板在线 |
| 22 | `mist_running` | `u8` | bool | 雾化板运行 |
| 23 | `mist_desired_level` | `u8` | 枚举 | 雾化期望档位 |
| 24 | `mist_actual_level` | `u8` | 枚举 | 雾化实际档位 |
| 25 | `mist_fault_code` | `u16 LE` | 枚举值 | 雾化板故障码 |
| 27 | `heat_enabled` | `u8` | bool | 加热输出是否使能 |
| 28 | `heat_output_delay_us` | `u16 LE` | 微秒 | 当前 triac 延时 |
| 30 | `heat_output_permille` | `u16 LE` | permille | 当前加热输出 |
| 32 | `heat_error_deci_c` | `i16 LE` | `0.1°C` | 目标-测量 |
| 34 | `heat_measured_temp_deci_c` | `i16 LE` | `0.1°C` | PID 测量温度 |
| 36 | `heat_target_temp_deci_c` | `i16 LE` | `0.1°C` | PID 目标温度 |
| 38 | `sensor_sample_uptime_ms` | `u32 LE` | ms | 采样时刻 uptime |
| 42 | `maintenance_active` | `u8` | bool | 维护模式激活 |
| 43 | `maintenance_fan_level` | `u8` | 枚举 | 维护风扇档位 |
| 44 | `maintenance_mist_level` | `u8` | 枚举 | 维护雾量档位 |
| 45 | `maintenance_heat_permille_div10` | `u8` | `permille/10` | 需乘 `10` 才是完整值 |

附加 PID 调试字段：

| 偏移 | 字段 | 类型 | 单位 |
|---|---|---|---|
| 46 | `pid_kp_milli` | `i32 LE` | x1000 |
| 50 | `pid_ki_milli` | `i32 LE` | x1000 |
| 54 | `pid_kd_milli` | `i32 LE` | x1000 |
| 58 | `pid_integral_limit_permille` | `i32 LE` | permille |
| 62 | `pid_i_term_raw` | `i32 LE` | 内部原始量 |

### 10.5 `MAINT` 帧

负载长度：

- 固定 `5` 字节

字段表：

| 偏移 | 字段 | 类型 | 单位 | 说明 |
|---|---|---|---|---|
| 0 | `active` | `u8` | bool | 是否处于维护模式 |
| 1 | `fan_level` | `u8` | 枚举 | 手动风量档位 |
| 2 | `mist_level` | `u8` | 枚举 | 手动雾量档位 |
| 3 | `heat_output_permille` | `u16 LE` | permille | 手动加热功率 |

## 11. 错误码表

### 11.1 通用参数错误码

| 错误码 | 场景 |
|---|---|
| `1` | `SET_MODE` 参数非法 |
| `2` | `SET_TARGET_TEMP` 参数非法 |
| `3` | `SET_TIME` 参数非法 |
| `4` | `SET_AIR_LEVEL` 参数非法 |
| `5` | `SET_MIST_LEVEL` 参数非法 |
| `6` | `SET_PID` 负载非法或 PID 校验失败 |
| `7` | 维护模式不允许进入 |
| `8` | 手动风扇参数非法或维护模式未激活 |
| `9` | 手动雾化参数非法或维护模式未激活 |
| `10` | 手动加热参数非法或维护模式未激活 |
| `11` | `RESUME` 时维护模式仍激活 |
| `0x7F` | 未知命令 |

### 11.2 状态/运行条件错误码

| 错误码 | 含义 |
|---|---|
| `0x40` | 状态机拒绝当前命令 |
| `0x41` | `START` 前 OTP reset 失败 |
| `0x42` | 盖子打开，禁止启动 |
| `0x43` | 雾化板报缺水，禁止启动 |
| `0x44` | 雾化板离线，禁止启动 |
| `0x45` | 雾化板安全锁定，禁止启动 |

### 11.3 故障码

当前系统故障码枚举：

| 值 | 名称 |
|---|---|
| `0` | `FAULT_NONE` |
| `1` | `FAULT_INIT_FAILED` |
| `2` | `FAULT_ADC_INIT_FAILED` |
| `3` | `FAULT_UART_INIT_FAILED` |
| `4` | `FAULT_PWM_INIT_FAILED` |
| `5` | `FAULT_TIMER_INIT_FAILED` |
| `6` | `FAULT_SENSOR_NTC_OPEN` |
| `7` | `FAULT_SENSOR_NTC_SHORT` |
| `8` | `FAULT_OVER_TEMP` |
| `9` | `FAULT_LIQUID_EMPTY` |
| `10` | `FAULT_COVER_OPEN` |
| `11` | `FAULT_MIST_BOARD_OFFLINE` |
| `12` | `FAULT_MIST_BOARD_FAULT` |
| `13` | `FAULT_HOST_HEARTBEAT_TIMEOUT` |
| `14` | `FAULT_PROTOCOL_ERROR` |
| `15` | `FAULT_STORAGE_ERROR` |
| `16` | `FAULT_OUTLET1_OVER_TEMP` |
| `17` | `FAULT_KETTLE_OVER_TEMP` |

雾化板故障码当前重点值：

| 值 | 含义 |
|---|---|
| `0x0000` | 无故障 |
| `0x0009` | 缺水 |

## 12. 保活与主动上报机制

### 12.1 心跳

上位机应周期发送：

- `HOST_CMD_HEARTBEAT (0x25)`

当前超时阈值：

- `APP_HOST_HEARTBEAT_TIMEOUT_MS = 3000`

建议发送周期：

- `1000 ms`

实现细节：

- 心跳在通信服务层直接处理
- 收到后立即刷新 `heartbeat_deadline_ms`
- 随即返回 `ACK`
- 心跳不会进入业务事件队列

### 12.2 主动上报

当前实现中的主动上报主要有两种：

- 周期遥测：`STATUS` 每 `500 ms`
- 状态变化快照：状态切换、雾化状态变化时会发送 `STATUS` 和 `RUNTIME`

因此上位机不要假设所有数据都只来自查询命令。

## 13. 典型通信流程

### 13.1 心跳

请求：

```text
aa 55 01 00 01 01 00 25 90 2d 0d 0a
```

含义：

- `frame_id = 0x0001`
- `type = CMD`
- `payload = [0x25]`

响应：

```text
aa 55 01 00 02 04 00 25 01 00 00 7f e8 0d 0a
```

表示：

- 回显 `frame_id = 0x0001`
- `ACK`
- `command_id = 0x25`
- `ok = 1`
- `error_code = 0`

### 13.2 读取当前配置

请求 `GET_CONFIG`：

```text
aa 55 23 01 01 01 00 27 2a ae 0d 0a
```

主控返回配置帧：

```text
aa 55 00 80 06 07 00 00 a4 01 58 02 02 02 1d 0a 0d 0a
```

解析为：

- `frame_id = 0x8000`
- `type = CONFIG`
- `mode = 0`，热疗
- `target_temp = 0x01A4 = 420 = 42.0°C`
- `duration_sec = 0x0258 = 600 = 10 分钟`
- `air_level = 2`，中档
- `mist_level = 2`，中档

随后主控再返回请求 ACK：

```text
aa 55 23 01 02 04 00 27 01 00 00 3f 3d 0d 0a
```

注意：

- 数据帧 `frame_id = 0x8000`
- ACK 帧 `frame_id = 0x0123`
- 二者不会相同

### 13.3 设置目标温度为 `42.0°C`

请求：

```text
aa 55 01 10 01 03 00 11 a4 01 3e d2 0d 0a
```

解析：

- `frame_id = 0x1001`
- `command_id = 0x11`
- `payload = A4 01 = 420 = 42.0°C`

成功 ACK：

```text
aa 55 01 10 02 04 00 11 01 00 00 70 d4 0d 0a
```

### 13.4 启动时因开盖被拒绝

失败 `NACK`：

```text
aa 55 01 20 03 04 00 20 00 42 00 0c 9c 0d 0a
```

解析：

- `command_id = 0x20`，`START`
- `ok = 0`
- `error_code = 0x42`
- 含义：盖子打开，启动被阻止

## 14. 上位机实现建议

### 14.1 接收侧

- 维护一个环形缓冲区或字节缓存
- 支持粘包、拆包、半包
- CRC 错帧直接丢弃并继续运行
- 解析放在工作线程，不要阻塞 UI 线程

### 14.2 刷新策略

建议轮询策略：

- `HEARTBEAT`: `1000 ms`
- `GET_STATUS`: `500 ms`
- `GET_RUNTIME`: `500..1000 ms`
- `GET_CONFIG`: 建连后、配置写入后、或低频轮询
- `GET_PID`: 建连后、PID 修改后、或低频轮询
- `GET_MAINTENANCE`: 进入维护页后及维护写入后

### 14.3 数据换算

- 温度显示：`value / 10.0`
- `Kp/Ki/Kd` 显示：`value / 1000.0`
- `permille` 转百分比：`value / 10.0`
- `maintenance_heat_permille_div10` 要先乘 `10`

### 14.4 当前项目实现限制

- `settings_store` 目前只是 RAM cache，调用“保存”接口并不会真正写入持久化存储
- 因此协议层看起来支持在线保存配置和 PID，但掉电后不会保留
- 若上位机需要“永久保存”语义，需等主控存储层实现真正落盘

## 15. 附录：常用枚举表

### 15.1 状态 `treatment_state_t`

| 值 | 名称 |
|---|---|
| `0` | `BOOT` |
| `1` | `SELF_TEST` |
| `2` | `READY` |
| `3` | `CONFIGURING` |
| `4` | `RUNNING_HOT` |
| `5` | `RUNNING_COLD` |
| `6` | `PAUSED` |
| `7` | `DONE` |
| `8` | `FAULT` |

### 15.2 模式 `treatment_mode_t`

| 值 | 名称 |
|---|---|
| `0` | `HOT` |
| `1` | `COLD` |

### 15.3 风量 `air_level_t`

| 值 | 名称 |
|---|---|
| `0` | `OFF` |
| `1` | `LOW` |
| `2` | `MID` |
| `3` | `HIGH` |

### 15.4 雾量 `mist_level_t`

| 值 | 名称 |
|---|---|
| `0` | `OFF` |
| `1` | `LOW` |
| `2` | `MID` |
| `3` | `HIGH` |

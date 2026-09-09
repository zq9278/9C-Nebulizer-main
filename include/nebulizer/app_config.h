#ifndef NEBULIZER_APP_CONFIG_H_
#define NEBULIZER_APP_CONFIG_H_
#define APP_FAN_PWM_INVERTED 1
/* 默认治疗目标温度，单位 0.1C；420 表示 42.0C。 */
#define APP_DEFAULT_TARGET_TEMP_DECI_C          420

/* 允许用户设置的最高治疗目标温度，单位 0.1C；450 表示 45.0C。 */
#define APP_MAX_TARGET_TEMP_DECI_C              450

/* 出雾口 1 过温故障阈值，单位 0.1C；达到 48.0C 立即进入故障保护。 */
#define APP_OUTLET1_OVER_TEMP_FAULT_DECI_C      480

/* 默认治疗时长，单位秒；600 表示默认治疗 10 分钟。 */
#define APP_DEFAULT_TREATMENT_TIME_SEC          600

/* 允许设置的最短治疗时长，单位秒。 */
#define APP_MIN_TREATMENT_TIME_SEC              60

/* 允许设置的最长治疗时长，单位秒。 */
#define APP_MAX_TREATMENT_TIME_SEC              3600

/* 传感器采样任务周期，单位毫秒。 */
#define APP_SENSOR_PERIOD_MS                    20

/* 控制任务周期，单位毫秒；热控、风扇、雾化输出按这个节拍更新。 */
#define APP_CONTROL_PERIOD_MS                   20

/* PB11 Outlet PID 计算周期，单位毫秒；出口温度变化慢，积分不跟随 20ms 控制任务累加。 */
#define APP_HEAT_PID_PERIOD_MS                  2000U

/* 安全检查任务周期，单位毫秒；越小代表保护响应越快。 */
#define APP_SAFETY_PERIOD_MS                    50

/* 上位机遥测状态上报周期，单位毫秒。 */
#define APP_TELEMETRY_PERIOD_MS                 500

/* 雾化板命令应答超时时间，单位毫秒。 */
#define APP_MIST_CMD_TIMEOUT_MS                 150

/* 雾化板命令失败后的重试次数。 */
#define APP_MIST_CMD_RETRY_COUNT                3

/* OTP_RESET/PA7 高电平保持时间，单位毫秒。 */
#define APP_OTP_RESET_HIGH_MS                   1000

/* 上位机心跳超时时间，单位毫秒；超过该时间未收到心跳则认为通信异常。 */
#define APP_HOST_HEARTBEAT_TIMEOUT_MS           3000

/* 是否在 runtime 帧中附带 PID 调试参数；1 表示附带，0 表示不附带。 */
#define APP_HOST_RUNTIME_PID_PARAMS_ENABLE      1

/* 普通 GPIO 输入消抖时间，单位毫秒。 */
#define APP_GPIO_DEBOUNCE_MS                    20

/* 盖子开合独立消抖时间，单位毫秒；用于避免霍尔信号抖动导致反复暂停/恢复。 */
#define APP_COVER_DEBOUNCE_MS                   100

/* 参数变更后延迟保存时间，单位毫秒；用于减少频繁写入非易失存储。 */
#define APP_SETTINGS_SAVE_DELAY_MS              1000

/* 主控与上位机串口接收环形缓冲区大小，单位字节。 */
#define APP_HOST_RX_RING_SIZE                   512

/* 主控与雾化板串口接收环形缓冲区大小，单位字节。 */
#define APP_MIST_RX_RING_SIZE                   256

/* 应用事件队列深度；决定最多可缓存多少个待处理业务事件。 */
#define APP_EVENT_QUEUE_LEN                     32

/* 上位机发送队列深度；决定最多可缓存多少帧待发送给上位机的数据。 */
#define APP_HOST_TX_QUEUE_LEN                   16

/* 雾化板发送队列深度；决定最多可缓存多少条待发送给雾化板的命令。 */
#define APP_MIST_TX_QUEUE_LEN                   8

/* 通用业务任务栈大小，单位字节。 */
#define APP_TASK_STACK_SIZE                     1792

/* 通信任务栈大小，单位字节。 */
#define APP_COMM_STACK_SIZE                     1792

/* 传感器任务栈大小，单位字节。 */
#define APP_SENSOR_STACK_SIZE                   1536

/* 控制任务栈大小，单位字节。 */
#define APP_CONTROL_STACK_SIZE                  1536

/* 安全任务栈大小，单位字节。 */
#define APP_SAFETY_STACK_SIZE                   1536

/* 遥测任务栈大小，单位字节。 */
#define APP_TELEMETRY_STACK_SIZE                1536

/* EEPROM 延迟写入专用工作队列栈，避免写周期等待阻塞系统工作队列。 */
#define APP_STORAGE_STACK_SIZE                  1024

/* 风扇 PWM 周期，单位微秒；100 表示 10 kHz PWM。 */
#define FAN_PWM_PERIOD_USEC                     100U

/* 风扇低档占空比，单位百分比。 */
#define FAN_LEVEL_LOW_PERCENT                   40U

/* 风扇中档占空比，单位百分比。 */
#define FAN_LEVEL_MID_PERCENT                   60U

/* 风扇高档占空比，单位百分比。 */
#define FAN_LEVEL_HIGH_PERCENT                  80U

/* PB11 出口温度风扇 PID 周期；风扇只在档位基础值上向上调节。 */
#define APP_FAN_PID_PERIOD_MS                   1000U

/* 风扇 PID 最多在 LOW/MID/HIGH 基础风速上增加 10 个百分点。 */
#define APP_FAN_PID_MAX_BOOST_PERCENT           10U

/* 风扇 PID 默认参数；误差为 PB11 温度减去出口目标温度。 */
#define APP_FAN_PID_KP_DEFAULT_MILLI            20000//风扇pid
#define APP_FAN_PID_KI_DEFAULT_MILLI            0
#define APP_FAN_PID_KD_DEFAULT_MILLI            0
#define APP_FAN_PID_I_LIMIT_DEFAULT_PERMILLE    50

/* 允许上位机写入的风扇 PID 参数范围。 */
#define APP_FAN_PID_KP_MAX_MILLI                20000
#define APP_FAN_PID_KI_MAX_MILLI                5000
#define APP_FAN_PID_KD_MAX_MILLI                5000
#define APP_FAN_PID_I_LIMIT_MAX_PERMILLE        (APP_FAN_PID_MAX_BOOST_PERCENT * 10U)

#if (FAN_LEVEL_LOW_PERCENT > 100U) || (FAN_LEVEL_MID_PERCENT > 100U) || \
	(FAN_LEVEL_HIGH_PERCENT > 100U)
#error "fan base level must be in the range 0..100 percent"
#endif

/* TRIAC 最小触发延时，单位微秒；越小代表越接近全功率导通。 */
#define TRIAC_MIN_DELAY_US                      500U

/* TRIAC 最大触发延时，单位微秒；越大代表越接近最低功率。 */
#define TRIAC_MAX_DELAY_US                      8000U

/* TRIAC 触发脉冲宽度，单位微秒。 */
#define TRIAC_PULSE_WIDTH_US                    100U

/* TRIAC_EN 时间比例控制的最小时间片，单位毫秒。 */
#define APP_TRIAC_POWER_SLICE_MS                12U

/* TRIAC_EN 功率控制窗口包含的时间片数量；12ms * 100 = 1200ms 完整功率窗口。 */
#define APP_TRIAC_POWER_WINDOW_SLICES           100U

/* PB11 Outlet PID 默认 Kp，按 milli 放大保存；9000 表示 9.000。 */
#define APP_HEAT_PID_KP_DEFAULT_MILLI           9000

/* PB11 Outlet PID 默认 Ki，按 milli 放大保存；当前 0 表示先关闭积分。 */
#define APP_HEAT_PID_KI_DEFAULT_MILLI           0

/* PB11 Outlet PID 默认 Kd，按 milli 放大保存；0 表示默认不使用微分。 */
#define APP_HEAT_PID_KD_DEFAULT_MILLI           0

/* PB11 Outlet PID 默认积分限幅，单位 permille；400 表示积分项最多贡献 40% 输出。 */
#define APP_HEAT_PID_I_LIMIT_DEFAULT            400

/*
 * 兼容旧版上位机的预热 PID 参数；当前控制逻辑不再使用这组参数，
 * 预热和出口闭环都只读取 PB11。
 */
#define APP_HEAT_PREHEAT_PID_KP_DEFAULT_MILLI   1000
#define APP_HEAT_PREHEAT_PID_KI_DEFAULT_MILLI   0
#define APP_HEAT_PREHEAT_PID_KD_DEFAULT_MILLI   0
#define APP_HEAT_PREHEAT_PID_I_LIMIT_DEFAULT    0

/* PB11 低于 30.0C 时使用固定满功率；达到该温度后切换到 PB11 PID。预热 */
#define APP_HEAT_FULL_POWER_BELOW_DECI_C        300
#define APP_HEAT_FULL_POWER_PERMILLE            1000U

/* 持续保温：PA5 锅体目标 65.0C，使用比例控制且最大输出 20%。 */
#define APP_KEEP_WARM_TARGET_DECI_C              650
#define APP_KEEP_WARM_KP_MILLI                   9000
#define APP_KEEP_WARM_OUTPUT_MAX_PERMILLE        200U

/* I2C EEPROM 参数。默认按常见 24C02：7-bit 地址 0x50、8-bit 字地址。 */
#define APP_SETTINGS_EEPROM_I2C_ADDR             0x50U
#define APP_SETTINGS_EEPROM_ADDR_WIDTH_BYTES     1U
#define APP_SETTINGS_EEPROM_WRITE_CYCLE_MS       5U

/* A：PB11 加热 PID 阶段的最终输出上限，单位 permille；默认 200 表示 20%。 */
#define APP_HEAT_PID_OUTPUT_MAX_PERMILLE        200U

/* PA5 通用停热温度；两个加热阶段达到120.0C后都停热，但不报故障。 */
#define APP_HEAT_PA5_STOP_DECI_C                1200

#if (APP_HEAT_PID_OUTPUT_MAX_PERMILLE > 1000U)
#error "heat PID output limit must be in the range 0..1000 permille"
#endif

#if (APP_HEAT_FULL_POWER_PERMILLE > 1000U)
#error "heat full-power output must be in the range 0..1000 permille"
#endif

#if (APP_KEEP_WARM_OUTPUT_MAX_PERMILLE > 1000U)
#error "keep-warm output limit must be in the range 0..1000 permille"
#endif

#if (APP_SETTINGS_EEPROM_ADDR_WIDTH_BYTES != 1U) && \
	(APP_SETTINGS_EEPROM_ADDR_WIDTH_BYTES != 2U)
#error "EEPROM address width must be one or two bytes"
#endif

/* 允许上位机写入的最大 Kp，按 milli 放大保存。 */
#define APP_HEAT_PID_KP_MAX_MILLI               120000

/* 允许上位机写入的最大 Ki，按 milli 放大保存。 */
#define APP_HEAT_PID_KI_MAX_MILLI               10000

/* 允许上位机写入的最大 Kd，按 milli 放大保存。 */
#define APP_HEAT_PID_KD_MAX_MILLI               5000

/* 允许上位机写入的最大积分限幅，单位 permille。 */
#define APP_HEAT_PID_I_LIMIT_MAX                1000

#endif /* NEBULIZER_APP_CONFIG_H_ */

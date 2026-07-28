#ifndef NEBULIZER_APP_CONFIG_H_
#define NEBULIZER_APP_CONFIG_H_

/* 默认治疗目标温度，单位 0.1C；420 表示 42.0C。 */
#define APP_DEFAULT_TARGET_TEMP_DECI_C          420

/* 允许用户设置的最高治疗目标温度，单位 0.1C；450 表示 45.0C。 */
#define APP_MAX_TARGET_TEMP_DECI_C              450

/* 锅体临时停热温度，单位 0.1C；达到 65.0C 时停止加热，低于阈值后可自动恢复 PID。 */
#define APP_KETTLE_HEAT_CUTOFF_DECI_C           1200

/* 出雾口 1 过温故障阈值，单位 0.1C；达到 46.0C 立即进入故障保护。 */
#define APP_OUTLET1_OVER_TEMP_FAULT_DECI_C      460

/* 锅体严重过温故障阈值，单位 0.1C；达到 100.0C 立即进入故障保护。 */
#define APP_KETTLE_OVER_TEMP_FAULT_DECI_C       1000

/* 默认治疗时长，单位秒；600 表示默认治疗 10 分钟。 */
#define APP_DEFAULT_TREATMENT_TIME_SEC          600

/* 允许设置的最短治疗时长，单位秒。 */
#define APP_MIN_TREATMENT_TIME_SEC              60

/* 允许设置的最长治疗时长，单位秒。 */
#define APP_MAX_TREATMENT_TIME_SEC              1800

/* 传感器采样任务周期，单位毫秒。 */
#define APP_SENSOR_PERIOD_MS                    20

/* 控制任务周期，单位毫秒；热控、风扇、雾化输出按这个节拍更新。 */
#define APP_CONTROL_PERIOD_MS                   20

/* 锅体热 PID 计算周期，单位毫秒；锅体温度变化慢，积分不需要跟随 20ms 控制任务累加。 */
#define APP_HEAT_PID_PERIOD_MS                  2000U

/* 安全检查任务周期，单位毫秒；越小代表保护响应越快。 */
#define APP_SAFETY_PERIOD_MS                    10

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

/* 风扇 PWM 周期，单位微秒；100 表示 10 kHz PWM。 */
#define FAN_PWM_PERIOD_USEC                     100U

/* 风扇低档占空比，单位百分比。 */
#define FAN_LEVEL_LOW_PERCENT                   80U

/* 风扇中档占空比，单位百分比。 */
#define FAN_LEVEL_MID_PERCENT                   90U

/* 风扇高档占空比，单位百分比。 */
#define FAN_LEVEL_HIGH_PERCENT                  100U

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

/* 热控内层 PID 默认 Kp，按 milli 放大保存；12000 表示 12.000。 */
#define APP_HEAT_PID_KP_DEFAULT_MILLI           8000

/* 热控内层 PID 默认 Ki，按 milli 放大保存；120 表示 0.120。 */
#define APP_HEAT_PID_KI_DEFAULT_MILLI           0

/* 热控内层 PID 默认 Kd，按 milli 放大保存；0 表示默认不使用微分。 */
#define APP_HEAT_PID_KD_DEFAULT_MILLI           0

/* 热控内层 PID 默认积分限幅，单位 permille；450 表示积分项最多贡献 45% 输出。 */
#define APP_HEAT_PID_I_LIMIT_DEFAULT            40

/* 自动模式下直接使用的锅体目标温度，单位 0.1C；680 表示 68.0C。 */
#define APP_HEAT_KETTLE_FIXED_TARGET_DECI_C     680

/* 锅体虚拟目标基础补偿已停用，保留为上位机兼容字段，固定为 0。 */
#define APP_HEAT_KETTLE_BASE_OFFSET_DECI_C      0

/* 锅体虚拟目标距离临时停热阈值的安全余量，单位 0.1C。 */
#define APP_HEAT_KETTLE_TARGET_MARGIN_DECI_C    30

/* 锅体内层控制死区，单位 0.1C；误差小于该值时认为锅体目标已满足，可停止加热。 */
#define APP_HEAT_KETTLE_INNER_DEADBAND_ENABLE   0
#define APP_HEAT_KETTLE_INNER_DEADBAND_DECI_C   5

/* 锅体超过虚拟目标多少后强制停热，单位 0.1C；用于抑制锅体侧过冲。 */
#define APP_HEAT_KETTLE_OVER_TARGET_STOP_ENABLE 0
#define APP_HEAT_KETTLE_OVER_TARGET_STOP_DECI_C 10

/* 调试锅体内层 PID 时，允许手动覆盖 kettle 目标温度的最小值，单位 0.1C。 */
#define APP_HEAT_KETTLE_TARGET_OVERRIDE_MIN_DECI_C 350

/* 调试锅体内层 PID 时，允许手动覆盖 kettle 目标温度的最大值，单位 0.1C。 */
#define APP_HEAT_KETTLE_TARGET_OVERRIDE_MAX_DECI_C 650

/* 锅体低温全功率预热阈值，单位 0.1C；低于 40.0C 时跳过 PID，直接满功率加热。 */
#define APP_HEAT_KETTLE_FULL_POWER_BELOW_DECI_C 30

/* 低风量对锅体虚拟目标的前馈补偿，单位 0.1C。 */
#define APP_HEAT_AIR_LOW_OFFSET_DECI_C          10

/* 中风量对锅体虚拟目标的前馈补偿，单位 0.1C。 */
#define APP_HEAT_AIR_MID_OFFSET_DECI_C          25

/* 高风量对锅体虚拟目标的前馈补偿，单位 0.1C。 */
#define APP_HEAT_AIR_HIGH_OFFSET_DECI_C         40

/* 低雾量对锅体虚拟目标的前馈补偿，单位 0.1C。 */
#define APP_HEAT_MIST_LOW_OFFSET_DECI_C         10

/* 中雾量对锅体虚拟目标的前馈补偿，单位 0.1C。 */
#define APP_HEAT_MIST_MID_OFFSET_DECI_C         20

/* 高雾量对锅体虚拟目标的前馈补偿，单位 0.1C。 */
#define APP_HEAT_MIST_HIGH_OFFSET_DECI_C        30

/* 出雾口外环基础补偿允许的最大值，单位 0.1C。 */
#define APP_HEAT_KETTLE_BASE_OFFSET_MAX_DECI_C  200

/* 出雾口外环安全余量允许的最大值，单位 0.1C。 */
#define APP_HEAT_KETTLE_TARGET_MARGIN_MAX_DECI_C 100

/* 风量/雾量前馈补偿允许的最大值，单位 0.1C。 */
#define APP_HEAT_FEEDFORWARD_OFFSET_MAX_DECI_C  100

/* 允许上位机写入的最大 Kp，按 milli 放大保存。 */
#define APP_HEAT_PID_KP_MAX_MILLI               120000

/* 允许上位机写入的最大 Ki，按 milli 放大保存。 */
#define APP_HEAT_PID_KI_MAX_MILLI               10000

/* 允许上位机写入的最大 Kd，按 milli 放大保存。 */
#define APP_HEAT_PID_KD_MAX_MILLI               5000

/* 允许上位机写入的最大积分限幅，单位 permille。 */
#define APP_HEAT_PID_I_LIMIT_MAX                1000

#endif /* NEBULIZER_APP_CONFIG_H_ */

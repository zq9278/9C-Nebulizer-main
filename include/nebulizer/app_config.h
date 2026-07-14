#ifndef NEBULIZER_APP_CONFIG_H_
#define NEBULIZER_APP_CONFIG_H_

/* 默认治疗目标温度和上限，单位都是 0.1C */
#define APP_DEFAULT_TARGET_TEMP_DECI_C      420
#define APP_MAX_TARGET_TEMP_DECI_C          450
/* 安全保护温度阈值，单位都是 0.1C */
#define APP_KETTLE_HEAT_CUTOFF_DECI_C       650   // 锅体到 65.0C 时暂停加热，低于阈值自动恢复 PID
#define APP_OUTLET1_OVER_TEMP_FAULT_DECI_C  460   // 出口雾气过温保护
#define APP_KETTLE_OVER_TEMP_FAULT_DECI_C   1200  // 锅体过温保护

/* 默认治疗时长、最短和最长时长，单位秒 */
#define APP_DEFAULT_TREATMENT_TIME_SEC      600
#define APP_MIN_TREATMENT_TIME_SEC          60
#define APP_MAX_TREATMENT_TIME_SEC          1800

/* 业务线程、控制周期、告警周期等，单位毫秒 */
#define APP_SENSOR_PERIOD_MS                20
#define APP_CONTROL_PERIOD_MS               20
#define APP_SAFETY_PERIOD_MS                10
#define APP_TELEMETRY_PERIOD_MS             500
/* 雾化板命令超时和重试次数 */
#define APP_MIST_CMD_TIMEOUT_MS             150
#define APP_MIST_CMD_RETRY_COUNT            3
/* OTP_RESET/PA7 高电平保持时间，单位毫秒 */
#define APP_OTP_RESET_HIGH_MS               1000
/* 上位机心跳超时，单位毫秒 */
#define APP_HOST_HEARTBEAT_TIMEOUT_MS       3000
/* 是否把 PID 调试参数附加到 runtime 帧发送给上位机 */
#define APP_HOST_RUNTIME_PID_PARAMS_ENABLE  1
/* GPIO 抖动消隐和参数延迟保存时间，单位毫秒 */
#define APP_GPIO_DEBOUNCE_MS                20
/* 盖子开合恢复使用独立防抖，避免霍尔切换抖动导致反复暂停/恢复 */
#define APP_COVER_DEBOUNCE_MS               100
#define APP_SETTINGS_SAVE_DELAY_MS          1000

/* 主控和雾化板的接收环形缓冲区大小，单位字节 */
#define APP_HOST_RX_RING_SIZE               512
#define APP_MIST_RX_RING_SIZE               256

/* 事件队列和各通信队列深度 */
#define APP_EVENT_QUEUE_LEN                 32
#define APP_HOST_TX_QUEUE_LEN               16
#define APP_MIST_TX_QUEUE_LEN               8

/* 各任务线程栈大小，单位字节 */
#define APP_TASK_STACK_SIZE                 1792
#define APP_COMM_STACK_SIZE                 1792
#define APP_SENSOR_STACK_SIZE               1536
#define APP_CONTROL_STACK_SIZE              1536
#define APP_SAFETY_STACK_SIZE               1536
#define APP_TELEMETRY_STACK_SIZE            1536

/* 风机 PWM 周期和风量档位映射，周期单位微秒，档位单位百分比 */
#define FAN_PWM_PERIOD_USEC                 100U
#define FAN_LEVEL_LOW_PERCENT               65U
#define FAN_LEVEL_MID_PERCENT               80U
#define FAN_LEVEL_HIGH_PERCENT              100U

/* TRIAC 控制参数，单位微秒 */
#define TRIAC_MIN_DELAY_US                  500U
#define TRIAC_MAX_DELAY_US                  8000U
#define TRIAC_PULSE_WIDTH_US                100U
/* TRIAC_EN 时间比例控制参数：
 * 以 12ms 为最小逻辑时间片，100 个时间片组成 1200ms 的完整功率窗口。
 * power_level=0 表示 0/100 导通，power_level=100 表示 100/100 全导通。
 */
#define APP_TRIAC_POWER_SLICE_MS            12U
#define APP_TRIAC_POWER_WINDOW_SLICES       100U

/* PID 默认值，Kp/Ki/Kd 统一按 milli 放大后保存 */
#define APP_HEAT_PID_KP_DEFAULT_MILLI       12000
#define APP_HEAT_PID_KI_DEFAULT_MILLI       120
#define APP_HEAT_PID_KD_DEFAULT_MILLI       0
#define APP_HEAT_PID_I_LIMIT_DEFAULT        450

/* 接近目标温度时的最大功率限制，用于抑制过冲 */
#define APP_HEAT_NEAR_TARGET_BAND1_DECI_C   20    // 接近目标 2.0C 内，最多 5% 输出
#define APP_HEAT_NEAR_TARGET_BAND2_DECI_C   30    // 接近目标 5.0C 内，最多 20% 输出
#define APP_HEAT_NEAR_TARGET_BAND3_DECI_C   100    // 接近目标 10.0C 内，最多 100% 输出
#define APP_HEAT_NEAR_TARGET_MAX1_PERCENT   15U
#define APP_HEAT_NEAR_TARGET_MAX2_PERCENT   30U
#define APP_HEAT_NEAR_TARGET_MAX3_PERCENT   100U
#define APP_HEAT_NEAR_TARGET_MAX1_PERMILLE  (APP_HEAT_NEAR_TARGET_MAX1_PERCENT * 10U)
#define APP_HEAT_NEAR_TARGET_MAX2_PERMILLE  (APP_HEAT_NEAR_TARGET_MAX2_PERCENT * 10U)
#define APP_HEAT_NEAR_TARGET_MAX3_PERMILLE  (APP_HEAT_NEAR_TARGET_MAX3_PERCENT * 10U)

/* PID 参数允许的最大值，防止上位机或配置写入非法范围 */
#define APP_HEAT_PID_KP_MAX_MILLI           120000
#define APP_HEAT_PID_KI_MAX_MILLI           10000
#define APP_HEAT_PID_KD_MAX_MILLI           5000
#define APP_HEAT_PID_I_LIMIT_MAX            1000

#endif /* NEBULIZER_APP_CONFIG_H_ */

#ifndef SRC_TELEMETRY_TELEMETRY_SERVICE_H_
#define SRC_TELEMETRY_TELEMETRY_SERVICE_H_

/* 预留初始化接口，便于未来扩展遥测缓存或统计对象。 */
int telemetry_service_init(void);

/* 立即抓取当前系统快照并对外发送。 */
int telemetry_service_publish_now(void);

#endif /* SRC_TELEMETRY_TELEMETRY_SERVICE_H_ */

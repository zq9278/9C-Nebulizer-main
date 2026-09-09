#ifndef PLATFORM_LOG_H
#define PLATFORM_LOG_H
#include <platform/util.h>
void platform_log(const char *level, const char *format, ...) __attribute__((format(printf,2,3)));
void platform_log_init(void);
void platform_log_start(void);
#define LOG_ERR(...) platform_log("E", __VA_ARGS__)
#define LOG_WRN(...) platform_log("W", __VA_ARGS__)
#define LOG_INF(...) platform_log("I", __VA_ARGS__)
#define LOG_DBG(...) do {} while (0)
#endif

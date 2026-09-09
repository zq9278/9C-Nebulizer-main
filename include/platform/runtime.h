#ifndef PLATFORM_RUNTIME_H
#define PLATFORM_RUNTIME_H
#include <errno.h>
#include <platform/util.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "stm32g0xx.h"
static inline uint32_t runtime_now_ms(void) { return xTaskGetTickCount() * portTICK_PERIOD_MS; }
static inline uint32_t runtime_irq_save(void) { uint32_t key = __get_PRIMASK(); __disable_irq(); return key; }
static inline void runtime_irq_restore(uint32_t key) { __set_PRIMASK(key); }
void runtime_panic(const char *file, int line) __attribute__((noreturn));
#endif

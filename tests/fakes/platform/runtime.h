#ifndef TEST_RUNTIME_H
#define TEST_RUNTIME_H
/* Single-threaded queue/mutex model for service backpressure regression tests.
 * This does not emulate MCU interrupts or FreeRTOS scheduling. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef struct { uint8_t *data; size_t item_size, capacity, head, count; } StaticQueue_t;
typedef StaticQueue_t *QueueHandle_t;
typedef struct { bool locked; } StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
#define configASSERT(x) assert(x)
static inline QueueHandle_t xQueueCreateStatic(size_t count, size_t size, uint8_t *buffer, StaticQueue_t *queue)
{
    *queue = (StaticQueue_t){.data=buffer, .item_size=size, .capacity=count};
    return queue;
}
static inline BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t timeout)
{
    (void)timeout;
    if (q->count == q->capacity) return 0;
    memcpy(q->data + ((q->head + q->count) % q->capacity) * q->item_size, item, q->item_size);
    ++q->count;
    return pdPASS;
}
static inline BaseType_t xQueuePeek(QueueHandle_t q, void *item, TickType_t timeout)
{
    (void)timeout;
    if (!q->count) return 0;
    memcpy(item, q->data + q->head * q->item_size, q->item_size);
    return pdPASS;
}
static inline BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t timeout)
{
    if (!xQueuePeek(q, item, timeout)) return 0;
    q->head = (q->head + 1) % q->capacity;
    --q->count;
    return pdPASS;
}
static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *s)
{
    s->locked = false;
    return s;
}
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t timeout)
{
    (void)timeout;
    assert(!s->locked);
    s->locked = true;
    return pdPASS;
}
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    assert(s->locked);
    s->locked = false;
    return pdPASS;
}
#ifdef TEST_RUNTIME_CLOCK
extern uint32_t test_runtime_ms;
static inline uint32_t runtime_now_ms(void) { return test_runtime_ms; }
#else
static inline uint32_t runtime_now_ms(void) { return 0; }
#endif
static inline void vTaskDelay(TickType_t delay) { (void)delay; }
#endif

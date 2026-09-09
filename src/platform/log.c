#include <platform/log.h>
#include <platform/runtime.h>
#include <platform/hardware.h>
#include <stdarg.h>
#include <string.h>
struct log_record { char text[160]; };
static StaticQueue_t log_control;
static uint8_t log_storage[8 * sizeof(struct log_record)];
static QueueHandle_t log_queue;
static StaticTask_t log_task_control;
static StackType_t log_stack[256];
volatile uint32_t platform_log_dropped;
/* Only integer/string formats are used by firmware diagnostics. This bounded
 * formatter avoids libc FILE state, allocation and shared reentrancy state. */
static void log_format(char *out, size_t capacity, const char *format, va_list args)
{
    size_t used = 0;
#define PUT(c) do { if (used + 1 < capacity) out[used++] = (c); } while (0)
    while (*format) {
        if (*format != '%') { PUT(*format++); continue; }
        ++format;
        unsigned width = 0;
        char pad = ' ';
        if (*format == '0') { pad = '0'; ++format; }
        while (*format >= '0' && *format <= '9') width = MIN(16U, width * 10U + (unsigned)(*format++ - '0'));
        char spec = *format;
        if (!spec) break;
        ++format;
        if (spec == '%') { PUT('%'); continue; }
        if (spec == 's') {
            const char *text = va_arg(args, const char *);
            if (!text) text = "(null)";
            while (*text) { PUT(*text++); }
            continue;
        }
        if (spec == 'c') { PUT((char)va_arg(args, int)); continue; }
        if (spec != 'd' && spec != 'i' && spec != 'u' && spec != 'x' && spec != 'X') {
            PUT('?'); continue;
        }
        uint32_t value;
        bool negative = false;
        if (spec == 'd' || spec == 'i') {
            int signed_value = va_arg(args, int);
            negative = signed_value < 0;
            value = negative ? 0U - (uint32_t)signed_value : (uint32_t)signed_value;
        } else value = va_arg(args, unsigned int);
        unsigned base = (spec == 'x' || spec == 'X') ? 16U : 10U;
        const char *digits = spec == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
        char reverse[11];
        unsigned length = 0;
        do { reverse[length++] = digits[value % base]; value /= base; } while (value);
        unsigned total = length + (negative ? 1U : 0U);
        if (negative && pad == '0') PUT('-');
        while (total++ < width) PUT(pad);
        if (negative && pad != '0') PUT('-');
        while (length) PUT(reverse[--length]);
    }
    out[used] = '\0';
#undef PUT
}
void platform_log_init(void)
{
    log_queue = xQueueCreateStatic(8, sizeof(struct log_record), log_storage, &log_control);
    configASSERT(log_queue);
}
void platform_log(const char *level, const char *format, ...)
{
    struct log_record record;
    if (!log_queue || __get_IPSR()) return;
    record.text[0] = level[0];
    record.text[1] = ' ';
    va_list args;
    va_start(args, format);
    log_format(record.text + 2, sizeof(record.text) - 4, format, args);
    va_end(args);
    strcat(record.text, "\r\n");
    if (xQueueSend(log_queue, &record, 0) != pdPASS) platform_log_dropped++;
}
static void log_task(void *argument)
{
    (void)argument;
    struct log_record record;
    for (;;) {
        if (xQueueReceive(log_queue, &record, portMAX_DELAY) == pdPASS)
            (void)HAL_UART_Transmit(&board_debug_uart, (uint8_t *)record.text,
                (uint16_t)strlen(record.text), 50);
    }
}
void platform_log_start(void)
{
    configASSERT(xTaskCreateStatic(log_task, "Log", ARRAY_SIZE(log_stack), NULL, 1,
        log_stack, &log_task_control));
}

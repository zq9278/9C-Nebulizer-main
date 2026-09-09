#include <platform/runtime.h>
#include <platform/hardware.h>
#include <platform/log.h>
#include <src/app/app_init.h>
#include <src/app/app_tasks.h>
#include <src/app/app_events.h>
static StaticTask_t boot_control;
static StackType_t boot_stack[512];
static void boot_task(void *argument)
{
    (void)argument;
    platform_log_start();
    LOG_INF("9C Nebulizer FreeRTOS boot");
    app_events_init();
    int ret = app_init();
    if (ret != 0) runtime_panic("app_init", ret);
    configASSERT(app_tasks_start() == 0);
    vTaskDelete(NULL);
}
int main(void)
{
    if (board_hardware_init() != 0) runtime_panic("hardware_init", 0);
    platform_log_init();
    configASSERT(xTaskCreateStatic(boot_task, "Boot", ARRAY_SIZE(boot_stack), NULL, 5,
        boot_stack, &boot_control));
    vTaskStartScheduler();
    runtime_panic("scheduler", 0);
}

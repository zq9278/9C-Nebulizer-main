#include <platform/runtime.h>
#include <platform/hardware.h>
static StaticTask_t idle_control;
static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
volatile const char *runtime_fault_file;
volatile int runtime_fault_line;
void vApplicationGetIdleTaskMemory(StaticTask_t **control, StackType_t **stack, uint32_t *size)
{
    *control = &idle_control;
    *stack = idle_stack;
    *size = ARRAY_SIZE(idle_stack);
}
void runtime_panic(const char *file, int line)
{
    __disable_irq();
    board_emergency_off();
    runtime_fault_file = file;
    runtime_fault_line = line;
    for (;;) { __WFI(); }
}
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    runtime_panic(name, 0);
}
void HardFault_Handler(void) { runtime_panic("HardFault", 0); }
void NMI_Handler(void) { runtime_panic("NMI", 0); }
/* HAL keeps its 1 ms tick before scheduler startup, then shares SysTick. */
extern void xPortSysTickHandler(void);
void SysTick_Handler(void)
{
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) xPortSysTickHandler();
}

void _init(void) {}
void _fini(void) {}

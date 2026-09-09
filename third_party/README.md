# Pinned upstream dependencies

Sources required for an offline ARM GCC build are vendored here. Upstream files are
unmodified; project configuration lives in `include/`.

| Directory | Upstream | Version |
|---|---|---|
| FreeRTOS | https://github.com/FreeRTOS/FreeRTOS-Kernel | V11.2.0 |
| CMSIS | https://github.com/ARM-software/CMSIS_5 | 5.9.0 |
| STM32G0 | https://github.com/STMicroelectronics/cmsis-device-g0 | v1.4.4 |
| STM32G0_HAL | https://github.com/STMicroelectronics/stm32g0xx-hal-driver | v1.4.6 |

Each directory includes the upstream license. `python tools/vendor_dependencies.py`
restores selected files from these tags, if needed. The normal build uses local files.
FreeRTOS uses GCC ARM_CM0 with MPU disabled. The kernel target renames its SysTick
function so the platform can share the tick with HAL.

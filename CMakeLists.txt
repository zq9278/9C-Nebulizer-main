cmake_minimum_required(VERSION 3.22)
project(Nebulizer LANGUAGES C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_C_COMPILER MATCHES "arm-none-eabi")
  message(FATAL_ERROR "Use cmake --preset debug or release (STM32 Arm GCC toolchain)")
endif()
set(FREERTOS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/FreeRTOS)
set(HAL ${CMAKE_CURRENT_SOURCE_DIR}/third_party/STM32G0_HAL)
set(DEVICE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/STM32G0)
if(NOT EXISTS "${FREERTOS}/tasks.c")
  message(FATAL_ERROR "Missing dependencies: run python tools/vendor_dependencies.py")
endif()
add_library(mcu_options INTERFACE)
target_compile_options(mcu_options INTERFACE -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
  -ffunction-sections -fdata-sections -fno-common
  $<$<CONFIG:Debug>:-Og> $<$<CONFIG:Debug>:-g3>
  $<$<CONFIG:Release>:-Os> $<$<CONFIG:Release>:-g3>)
target_compile_definitions(mcu_options INTERFACE STM32G070xx USE_HAL_DRIVER)
target_include_directories(mcu_options INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(mcu_options SYSTEM INTERFACE
  ${FREERTOS}/include ${FREERTOS}/portable/GCC/ARM_CM0
  ${HAL}/Inc ${DEVICE}/Include
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/CMSIS/CMSIS/Core/Include)
add_library(freertos STATIC ${FREERTOS}/tasks.c ${FREERTOS}/queue.c ${FREERTOS}/list.c
  ${FREERTOS}/portable/GCC/ARM_CM0/port.c
  ${FREERTOS}/portable/GCC/ARM_CM0/portasm.c)
target_link_libraries(freertos PUBLIC mcu_options)
target_compile_definitions(freertos PRIVATE SysTick_Handler=xPortSysTickHandler)
set(HAL_MODULES hal hal_cortex hal_rcc hal_rcc_ex hal_pwr hal_pwr_ex hal_flash
  hal_flash_ex hal_gpio hal_dma hal_dma_ex hal_adc hal_adc_ex hal_i2c hal_i2c_ex
  hal_tim hal_tim_ex hal_uart hal_uart_ex)
foreach(module IN LISTS HAL_MODULES)
  list(APPEND HAL_SOURCES ${HAL}/Src/stm32g0xx_${module}.c)
endforeach()
add_library(stm32_hal STATIC ${HAL_SOURCES} ${DEVICE}/Source/Templates/system_stm32g0xx.c)
target_link_libraries(stm32_hal PUBLIC mcu_options)
add_executable(nebulizer
  src/main.c
  src/app/app_context.c
  src/app/app_events.c
  src/app/app_init.c
  src/app/app_tasks.c
  src/platform/board_devices.c
  src/platform/board_check.c
  src/state_machine/treatment_sm.c
  src/safety/fault_manager.c
  src/safety/safety_service.c
  src/telemetry/telemetry_service.c
  drivers_app/gpio/input_gpio.c
  drivers_app/gpio/output_gpio.c
  drivers_app/adc/adc_manager.c
  drivers_app/adc/ntc_table.c
  drivers_app/adc/ntc_convert.c
  drivers_app/uart/uart_port.c
  drivers_app/uart/uart_rx_ring.c
  drivers_app/pwm/pwm_output.c
  services/communication/host_comm_service.c
  services/communication/host_comm_rx.c
  services/communication/host_comm_tx.c
  services/mist/mist_service.c
  services/mist/mist_board_client.c
  services/sensors/sensor_service.c
  services/sensors/sensor_snapshot.c
  services/sensors/gx1832_service.c
  services/heat/heat_control.c
  services/heat/triac_control.c
  services/fan/fan_control.c
  services/storage/settings_defaults.c
  services/storage/settings_store.c
  protocols/common/crc16_modbus.c
  protocols/common/frame_codec.c
  protocols/common/ring_frame_parser.c
  protocols/host/host_protocol.c
  protocols/host/host_commands.c
  protocols/mist/mist_protocol.c
  protocols/mist/mist_commands.c
  src/platform/hardware.c
  src/platform/runtime.c
  src/platform/log.c
  ${DEVICE}/Source/Templates/gcc/startup_stm32g070xx.s
)
set_target_properties(nebulizer PROPERTIES SUFFIX ".elf")
target_include_directories(nebulizer PRIVATE include . src drivers_app services protocols)
target_compile_options(nebulizer PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter -fstack-usage)
target_link_libraries(nebulizer PRIVATE freertos stm32_hal m)
target_link_options(nebulizer PRIVATE -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
  -T${CMAKE_CURRENT_SOURCE_DIR}/cmake/stm32g070cb.ld
  -nostartfiles --specs=nano.specs --specs=nosys.specs
  -Wl,--gc-sections -Wl,--print-memory-usage -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/nebulizer.map)
set_property(TARGET nebulizer APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/stm32g070cb.ld)
add_custom_command(TARGET nebulizer POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:nebulizer> ${CMAKE_CURRENT_BINARY_DIR}/nebulizer.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:nebulizer> ${CMAKE_CURRENT_BINARY_DIR}/nebulizer.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:nebulizer>)

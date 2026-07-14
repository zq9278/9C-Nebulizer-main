cmake_minimum_required(VERSION 3.20.0)

if(NOT DEFINED BOARD AND NOT DEFINED CACHE{BOARD} AND NOT DEFINED ENV{BOARD})
  set(BOARD nebulizer_g070cbt6 CACHE STRING "Default Zephyr board for this project")
endif()

if(NOT DEFINED Python3_EXECUTABLE AND NOT DEFINED CACHE{Python3_EXECUTABLE})
  set(_nebulizer_python_candidates
      "$ENV{WEST_PYTHON}"
      "$ENV{USERPROFILE}/.zinstaller/.venv/Scripts/python.exe")
  foreach(_nebulizer_python_candidate IN LISTS _nebulizer_python_candidates)
    if(EXISTS "${_nebulizer_python_candidate}")
      set(Python3_EXECUTABLE "${_nebulizer_python_candidate}" CACHE FILEPATH
          "Preferred Python interpreter for Zephyr builds")
      break()
    endif()
  endforeach()
endif()

if(NOT DEFINED NO_BUILD_TYPE_WARNING AND NOT DEFINED CACHE{NO_BUILD_TYPE_WARNING})
  set(NO_BUILD_TYPE_WARNING ON CACHE BOOL "Suppress Zephyr Debug build-type warning")
endif()

set(_nebulizer_zephyr_hints
    "$ENV{ZEPHYR_BASE}"
    "D:/Technology_stack/Zephyr/deps/zephyr")
find_package(Zephyr REQUIRED HINTS ${_nebulizer_zephyr_hints})
project(9C_Nebulizer_main C)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/drivers_app
  ${CMAKE_CURRENT_SOURCE_DIR}/services
  ${CMAKE_CURRENT_SOURCE_DIR}/protocols
)

target_sources(app PRIVATE
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
  drivers_app/timer/triac_timer.c
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
)

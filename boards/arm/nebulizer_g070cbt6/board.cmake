# SPDX-License-Identifier: Apache-2.0

board_runner_args(stm32cubeprogrammer "--port=swd" "--reset-mode=hw")
board_runner_args(openocd "--cmd-pre-init=source [find interface/stlink.cfg]")
board_runner_args(openocd "--cmd-pre-init=transport select hla_swd")
board_runner_args(openocd "--cmd-pre-init=source [find target/stm32g0x.cfg]")
board_runner_args(openocd "--cmd-pre-init=adapter speed 1000")
board_runner_args(pyocd "--target=stm32g070cbtx")
board_runner_args(pyocd "--flash-opt=-O reset_type=hw")
board_runner_args(pyocd "--flash-opt=-O connect_mode=under-reset")
board_runner_args(jlink "--device=STM32G070CB" "--speed=4000")

include(${ZEPHYR_BASE}/boards/common/stm32cubeprogrammer.board.cmake)
include(${ZEPHYR_BASE}/boards/common/openocd-stm32.board.cmake)
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/stlink_gdbserver.board.cmake)

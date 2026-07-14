# 9C-Nebulizer-main

Zephyr RTOS project for the STM32G070CBT6 based ophthalmic nebulizer controller.

## CLion

CLion import, Zephyr SDK configuration, pristine build, artifact generation, west flash,
and debugger-based flashing are documented in `docs/CLion.md`.

## Host UI

USART1 host software architecture, runtime monitoring, control command set, PID online tuning,
and the Qt desktop scaffold are documented in `docs/HostUI.md`.

Two desktop implementations are provided:

- C++ / Qt: `tools/nebulizer_host_qt/`
- Python / PyQt6: `tools/nebulizer_host_pyqt/`

## Build

```sh
west build -b nebulizer_g070cbt6 .
```

If `west` is installed in a virtual environment, activate it first or invoke the full path.

## Flash

```sh
west flash
```

Default logging console is `USART2` on `PA2/PA3` at `115200`.

## Layout

- `boards/arm/nebulizer_g070cbt6/`: standalone Zephyr board definition
- `src/platform/` and `include/platform/`: DTS-backed board resource adapter
- `drivers_app/`: Zephyr driver wrappers without treatment logic
- `protocols/`: frame codec, CRC, parsers, host/mist payload codecs
- `services/`: business services
- `src/state_machine/`: treatment state machine
- `src/safety/`: fault latch and safety stop logic
- `src/app/`: static tasks, queues, init, event dispatch

# Nebulizer Host UI over USART1

## Scope

This host-side solution targets the current firmware layout in
`/Users/zq/project/Nebulizer/software/9C-Nebulizer-main` and communicates only
through `USART1` (`PB6/PB7`, `115200 8N1`).

The design goal is to let a maintenance engineer or manufacturing tool:

- monitor treatment state, fault state, temperatures, fan, mist board, cover, liquid level
- send all main treatment control commands
- read and write treatment parameters
- tune the independent PB10 preheat PID and PB11 outlet PID online
- tune the independent PB11 outlet-temperature fan PID online
- observe protocol ACK/NACK, heartbeat, and communication exceptions

## Recommended Desktop Layout

The repository now contains a standalone Qt desktop scaffold under
`tools/nebulizer_host_qt/`.

If you prefer Python delivery and faster GUI iteration, the repository also contains a
PyQt6 implementation under `tools/nebulizer_host_pyqt/`.

Recommended layout:

- Top connection bar:
  - serial port selector
  - connect/disconnect
  - connection state
- Left control pane:
  - treatment mode
  - target temperature
  - duration
  - air level
  - mist level
  - start / pause / resume / stop / clear fault
- Left Outlet PID Tuning pane:
  - `Kp`
  - `Ki`
  - `Kd`
  - integral limit
  - live PB11 temperature, outlet target, and PID error
  - phase-aware tuning hints
  - read/apply outlet PID parameters
- Left Preheat PID Tuning pane:
  - independent PB10 `Kp/Ki/Kd` and integral limit
  - fixed `55.0°C` preheat target and `80%` output limit
  - live active phase, PB10 error, output, and target
- Left Fan PID Tuning pane:
  - `Kp/Ki/Kd` and integral limit
  - LOW `40..50%`, MID `60..70%`, HIGH `80..90%` limits
  - live PB11 error, base output, PID boost, and final fan PWM
- Left maintenance pane:
  - enter/exit maintenance mode
  - manual fan level
  - manual mist level
  - manual heat output permille
  - one-click manual output apply
- Right runtime pane:
  - current state
  - fault code
  - remaining time
  - heartbeat state
  - outlet / kettle temperatures
  - fan rpm
  - mist online/running/level
  - liquid / cover state
  - heat output delay / output permille
- Bottom log pane:
  - TX command log
  - RX ACK/NACK log
  - parser / CRC / serial error log
- Extra tabs:
  - fault history
  - live temperature trend chart
  - parameter import/export workflow

## Communication Strategy

Transport:

- physical link: `USART1`
- baud rate: `115200`
- format: `8N1`
- host app should open the board as a raw serial port, not as a console

Frame format reused from firmware:

```text
AA 55 | frame_id u16 LE | type u8 | len u16 LE | payload | crc16_modbus | 0D 0A
```

CRC range:

- `frame_id(2) + type(1) + len(2) + payload(len)`
- excludes `AA 55`
- excludes `0D 0A`

The host app should:

- keep a receive ring/buffer on the PC side too
- parse sticky packets and half packets
- drop bad CRC frames and keep running
- never block the GUI thread on parsing

## Host Commands

Existing control commands on `USART1`:

- `HOST_CMD_SET_MODE`
- `HOST_CMD_SET_TARGET_TEMP`
- `HOST_CMD_SET_TIME`
- `HOST_CMD_SET_AIR_LEVEL`
- `HOST_CMD_SET_MIST_LEVEL`
- `HOST_CMD_START`
- `HOST_CMD_PAUSE`
- `HOST_CMD_RESUME`
- `HOST_CMD_STOP`
- `HOST_CMD_GET_STATUS`
- `HOST_CMD_HEARTBEAT`
- `HOST_CMD_CLEAR_FAULT`

New commands added for host software completeness:

- `HOST_CMD_GET_CONFIG`
- `HOST_CMD_GET_PID`
- `HOST_CMD_SET_PID`
- `HOST_CMD_GET_RUNTIME`
- `HOST_CMD_ENTER_MAINTENANCE`
- `HOST_CMD_EXIT_MAINTENANCE`
- `HOST_CMD_MANUAL_SET_FAN`
- `HOST_CMD_MANUAL_SET_MIST`
- `HOST_CMD_MANUAL_SET_HEAT`
- `HOST_CMD_GET_MAINTENANCE`
- `HOST_CMD_GET_FAN_PID`
- `HOST_CMD_SET_FAN_PID`
- `HOST_CMD_GET_PREHEAT_PID`
- `HOST_CMD_SET_PREHEAT_PID`

## Response Frames

Periodic state frame:

- type: `HOST_FRAME_TYPE_STATUS`
- sent by firmware telemetry every `500 ms`
- enough for dashboard overview

Config frame:

- type: `HOST_FRAME_TYPE_CONFIG`
- fields:
  - `mode u8`
  - `target_temp_deci_c u16`
  - `duration_sec u16`
  - `air_level u8`
  - `mist_level u8`

PID frame:

- type: `HOST_FRAME_TYPE_PID`
- fields:
  - `kp_milli i32`
  - `ki_milli i32`
  - `kd_milli i32`
  - `integral_limit_permille i32`
  - `triac_min_delay_us u16`
  - `triac_max_delay_us u16`

Runtime frame:

- type: `HOST_FRAME_TYPE_RUNTIME`
- fields:
  - `state u8`
  - `remaining_sec u32`
  - `fault_code u16`
  - `heartbeat_ok u8`
  - `ntc1..ntc4 i16`
  - `liquid_present u8`
  - `cover_closed u8`
  - `gx1832_active u8`
  - `fan_rpm u16`
  - `mist_online u8`
  - `mist_running u8`
  - `mist_desired_level u8`
  - `mist_actual_level u8`
  - `mist_fault_code u16`
  - `heat_enabled u8`
  - `heat_delay_us u16`
  - `heat_output_permille u16`
  - `heat_error_deci_c i16`
  - `heat_measured_temp_deci_c i16`
  - `heat_target_temp_deci_c i16`
  - `sensor_sample_uptime_ms u32`
  - `maintenance_active u8`
  - `maintenance_fan_level u8`
  - `maintenance_mist_level u8`
  - `maintenance_heat_permille_div10 u8`
  - fan PID enabled/saturated flags
  - fan PID base/final PWM percent
  - fan PID boost, PB11 error, measured temperature, target, and integral term
  - `heat_control_phase`: `0=IDLE`, `1=PB10 PREHEAT PID`, `2=PB11 OUTLET PID`

Maintenance frame:

- type: `HOST_FRAME_TYPE_MAINT`
- fields:
  - `active u8`
  - `fan_level u8`
  - `mist_level u8`
  - `heat_output_permille u16`

## Refresh Mechanism

Recommended refresh strategy for the PC app:

- heartbeat: every `1000 ms`
- `GET_STATUS`: every `500 ms`
- `GET_RUNTIME`: every `1000 ms`
- `GET_CONFIG`: every `3000 ms` or after a config write
- `GET_PID`: after connection or after outlet PID apply
- `GET_PREHEAT_PID`: after connection or after preheat PID apply
- `GET_MAINTENANCE`: on maintenance page entry and after manual output writes

This keeps the UI responsive without flooding `USART1`.

## Temperature Tuning Path

The host UI provides two sequential heater-control tuning paths. PB10 first regulates to
the fixed 55C preheat target. When PB11 is within 2C of the outlet target, control latches
to PB11 using `OutletTarget - PB11 OutletTemp`.

## PID Tuning Path

Current firmware exposes two independent fixed-point heater PID controllers. PB10 preheat
uses `55C - PB10`, defaults to P-only control, disables integral outside the final 5C band,
and applies saturation anti-windup. After the one-way handoff, PB11 owns heater output and
its output ceiling ramps to the configured limit over 10 seconds.

Host workflow:

1. Connect to `USART1`
2. Read both PIDs via `HOST_CMD_GET_PREHEAT_PID` and `HOST_CMD_GET_PID`
3. Display `Kp/Ki/Kd` as `value / 1000.0`
4. Let the engineer edit values online
5. Send `HOST_CMD_SET_PREHEAT_PID` or `HOST_CMD_SET_PID`
6. Wait for ACK and updated PID frame
7. Observe runtime frame values:
   - heat enable
   - output delay
   - PB11 measured temperature
   - outlet target and error
   - output power and integral contribution
   - output permille
   - temperature error

The fan PID is independent from the heater PID. It uses
`PB11 OutletTemp - OutletTarget`; only positive error can increase fan PWM, and the
increase is clamped to 10 percentage points above the selected LOW/MID/HIGH base.

## Exception Handling

The host app should explicitly handle:

- serial port open failure
- hot unplug / cable removal
- RX CRC mismatch
- unsupported frame type
- ACK timeout
- NACK with error code
- heartbeat loss
- board in `FAULT`
- mist board offline while mainboard is alive

Recommended UI behavior:

- serial failure:
  - disconnect UI state
  - stop periodic polling
  - keep log visible
- NACK:
  - show command name and error code
- heartbeat loss:
  - highlight connection state
  - stop sending unsafe maintenance commands until link recovers
- board fault:
  - show fault code persistently until `CLEAR_FAULT`
- maintenance mode:
  - deny treatment `START/RESUME` while active
  - highlight maintenance state in runtime pane
  - force operator to exit maintenance before normal treatment

## Implementation Notes

Repository implementation:

- Desktop app scaffold:
  - `tools/nebulizer_host_qt/`
- PyQt desktop app:
  - `tools/nebulizer_host_pyqt/`
- Custom trend widget:
  - `tools/nebulizer_host_qt/src/TrendWidget.*`
  - `tools/nebulizer_host_pyqt/trend_widget.py`
- Shared protocol IDs reused from firmware:
  - `include/nebulizer/protocol_ids.h`
- Firmware protocol encode/decode:
  - `protocols/host/`
- Firmware command handling:
  - `src/app/app_tasks.c`
- Firmware PID and diagnostics:
  - `services/heat/heat_control.c`
- Firmware maintenance control:
  - `src/app/app_tasks.c`

The host app is intentionally kept outside the Zephyr root build so
`west build -b nebulizer_g070cbt6 .` remains unchanged.

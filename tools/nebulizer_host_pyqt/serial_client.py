from __future__ import annotations

import re
import sys
from typing import Optional

import serial
import serial.tools.list_ports
from PyQt6.QtCore import QObject, QTimer, pyqtSignal

from models import (
    ConfigModel,
    FanPidModel,
    MaintenanceModel,
    PidModel,
    PreheatPidModel,
    RuntimeModel,
    StatusModel,
    TreatmentEventModel,
)
from protocol import HostCmd, HostFrameType, build_command, extract_frames, parse_ack

MIST_FAULT_LOW_WATER = 0x0009


def _le16(payload: bytes, offset: int) -> int:
    return int.from_bytes(payload[offset : offset + 2], "little", signed=False)


def _sle16(payload: bytes, offset: int) -> int:
    return int.from_bytes(payload[offset : offset + 2], "little", signed=True)


def _le32(payload: bytes, offset: int) -> int:
    return int.from_bytes(payload[offset : offset + 4], "little", signed=False)


def _sle32(payload: bytes, offset: int) -> int:
    return int.from_bytes(payload[offset : offset + 4], "little", signed=True)


class SerialClient(QObject):
    STATUS_REFRESH_MS = 500
    RUNTIME_REFRESH_MS = 500

    connection_changed = pyqtSignal(bool)
    log_message = pyqtSignal(str)
    ack_received = pyqtSignal(int, bool, int)
    status_updated = pyqtSignal(object)
    config_updated = pyqtSignal(object)
    pid_updated = pyqtSignal(object)
    preheat_pid_updated = pyqtSignal(object)
    fan_pid_updated = pyqtSignal(object)
    runtime_updated = pyqtSignal(object)
    maintenance_updated = pyqtSignal(object)
    treatment_event_received = pyqtSignal(object)

    def __init__(self) -> None:
        super().__init__()
        self._port: Optional[serial.Serial] = None
        self._rx = bytearray()
        self._frame_id = 1

        self._poll_timer = QTimer(self)
        self._poll_timer.setInterval(20)
        self._poll_timer.timeout.connect(self._poll_serial)

        self._heartbeat_timer = QTimer(self)
        self._heartbeat_timer.setInterval(1000)
        self._heartbeat_timer.timeout.connect(self.send_heartbeat)

        self._status_timer = QTimer(self)
        self._status_timer.setInterval(self.STATUS_REFRESH_MS)
        self._status_timer.timeout.connect(self.request_status)

        self._runtime_timer = QTimer(self)
        self._runtime_timer.setInterval(self.RUNTIME_REFRESH_MS)
        self._runtime_timer.timeout.connect(self.request_runtime)

    @staticmethod
    def list_ports() -> list[str]:
        devices: set[str] = set()
        for port in serial.tools.list_ports.comports():
            device = port.device
            if sys.platform == "darwin" and not device.startswith("/dev/cu."):
                continue
            devices.add(device)

        def sort_key(device: str) -> tuple[str, int]:
            match = re.fullmatch(r"COM(\d+)", device, re.IGNORECASE)
            if match:
                return ("COM", int(match.group(1)))
            return (device, -1)

        return sorted(devices, key=sort_key)

    def is_open(self) -> bool:
        return self._port is not None and self._port.is_open

    def open_port(self, device: str, baudrate: int = 115200) -> None:
        self.close_port()
        try:
            self._port = serial.Serial(device, baudrate=baudrate, timeout=0)
        except Exception as exc:
            self.log_message.emit(f"open {device} failed: {exc}")
            return

        self._rx.clear()
        self._poll_timer.start()
        self._heartbeat_timer.start()
        self._status_timer.start()
        self._runtime_timer.start()

        self.connection_changed.emit(True)
        self.log_message.emit(f"connected {device} @ {baudrate}")

        self.request_config()
        self.request_preheat_pid()
        self.request_pid()
        self.request_fan_pid()
        self.request_status()
        self.request_runtime()
        self.request_maintenance()

    def close_port(self) -> None:
        self._poll_timer.stop()
        self._heartbeat_timer.stop()
        self._status_timer.stop()
        self._runtime_timer.stop()

        if self._port is not None:
            try:
                self._port.close()
            except Exception:
                pass
            self._port = None
            self.log_message.emit("serial closed")

        self.connection_changed.emit(False)

    def _next_frame_id(self) -> int:
        value = self._frame_id
        self._frame_id = 1 if self._frame_id >= 0xFFFF else self._frame_id + 1
        return value

    def _send(self, cmd: int, payload: bytes = b"") -> None:
        if not self.is_open():
            self.log_message.emit("serial is not connected")
            return

        frame = build_command(self._next_frame_id(), cmd, payload)
        try:
            self._port.write(frame)
            self.log_message.emit(f"tx cmd=0x{cmd:02x} payload={payload.hex(' ')}")
        except Exception as exc:
            self.log_message.emit(f"tx failed cmd=0x{cmd:02x}: {exc}")

    def _poll_serial(self) -> None:
        if not self.is_open():
            return

        try:
            waiting = self._port.in_waiting
            if waiting:
                chunk = self._port.read(waiting)
                self._rx.extend(chunk)
                self.log_message.emit(f"rx bytes={len(chunk)}")
                extract_frames(self._rx, self._handle_frame, self._handle_error)
        except Exception as exc:
            self.log_message.emit(f"serial poll error: {exc}")
            self.close_port()

    def _handle_error(self, message: str) -> None:
        self.log_message.emit(f"rx parse error: {message}")

    def _handle_frame(self, frame) -> None:
        ack = parse_ack(frame)
        if ack is not None:
            self.ack_received.emit(ack.command_id, ack.ok, ack.error_code)
            self.log_message.emit(
                f"ack cmd=0x{ack.command_id:02x} ok={int(ack.ok)} err={ack.error_code}"
            )
            return

        payload = frame.payload

        if frame.frame_type == HostFrameType.STATUS and len(payload) >= 20:
            status = StatusModel(
                state=payload[0],
                remaining_sec=_le16(payload, 1),
                target_temp_deci_c=_le16(payload, 3),
                outlet_temp_deci_c=_sle16(payload, 5),
                kettle_temp_deci_c=_sle16(payload, 7),
                air_level=payload[9],
                mist_level=payload[10],
                liquid_present=bool(payload[11]),
                cover_closed=bool(payload[12]),
                fault_code=payload[13],
                mist_online=bool(payload[14]),
                mist_fault_code=_le16(payload, 15),
                mist_low_water=_le16(payload, 15) == MIST_FAULT_LOW_WATER,
                mist_running=bool(payload[17]),
                heartbeat_ok=bool(payload[18]),
                mode=payload[19],
                keep_warm_enabled=bool(payload[20]) if len(payload) >= 21 else False,
            )
            self.status_updated.emit(status)
            self.log_message.emit(
                "rx status "
                f"state={status.state} rem={status.remaining_sec}s "
                f"outlet={status.outlet_temp_deci_c / 10.0:.1f}C "
                f"kettle={status.kettle_temp_deci_c / 10.0:.1f}C "
                f"fault={status.fault_code}"
            )
            return

        if frame.frame_type == HostFrameType.CONFIG and len(payload) >= 7:
            config = ConfigModel(
                mode=payload[0],
                target_temp_deci_c=_le16(payload, 1),
                duration_sec=_le16(payload, 3),
                air_level=payload[5],
                mist_level=payload[6],
                keep_warm_enabled=bool(payload[7]) if len(payload) >= 8 else False,
            )
            self.config_updated.emit(config)
            self.log_message.emit(
                "rx config "
                f"mode={config.mode} target={config.target_temp_deci_c / 10.0:.1f}C "
                f"time={config.duration_sec}s air={config.air_level} mist={config.mist_level} "
                f"keep_warm={int(config.keep_warm_enabled)}"
            )
            return

        if frame.frame_type == HostFrameType.EVENT and len(payload) >= 12:
            event = TreatmentEventModel(
                event_id=payload[0],
                mode=payload[1],
                state=payload[2],
                keep_warm_enabled=bool(payload[3]),
                remaining_sec=_le32(payload, 4),
                outlet_temp_deci_c=_sle16(payload, 8),
                kettle_temp_deci_c=_sle16(payload, 10),
            )
            self.treatment_event_received.emit(event)
            self.log_message.emit(
                "rx treatment event "
                f"id=0x{event.event_id:02x} state={event.state} "
                f"remaining={event.remaining_sec}s"
            )
            return

        if frame.frame_type == HostFrameType.PID and len(payload) >= 20:
            pid = PidModel(
                kp_milli=_sle32(payload, 0),
                ki_milli=_sle32(payload, 4),
                kd_milli=_sle32(payload, 8),
                integral_limit_permille=_sle32(payload, 12),
                min_delay_us=_le16(payload, 16),
                max_delay_us=_le16(payload, 18),
            )
            self.pid_updated.emit(pid)
            self.log_message.emit(
                "rx pid "
                f"kp={pid.kp_milli / 1000.0:.3f} "
                f"ki={pid.ki_milli / 1000.0:.3f} "
                f"kd={pid.kd_milli / 1000.0:.3f} "
                f"ilim={pid.integral_limit_permille}"
            )
            return

        if frame.frame_type == HostFrameType.PREHEAT_PID and len(payload) >= 20:
            pid = PreheatPidModel(
                kp_milli=_sle32(payload, 0),
                ki_milli=_sle32(payload, 4),
                kd_milli=_sle32(payload, 8),
                integral_limit_permille=_sle32(payload, 12),
                target_temp_deci_c=_le16(payload, 16),
                output_max_permille=_le16(payload, 18),
            )
            self.preheat_pid_updated.emit(pid)
            self.log_message.emit(
                "rx preheat pid "
                f"kp={pid.kp_milli / 1000.0:.3f} "
                f"ki={pid.ki_milli / 1000.0:.3f} "
                f"kd={pid.kd_milli / 1000.0:.3f} "
                f"ilim={pid.integral_limit_permille} "
                f"target={pid.target_temp_deci_c / 10.0:.1f}C "
                f"max={pid.output_max_permille}"
            )
            return

        if frame.frame_type == HostFrameType.FAN_PID and len(payload) >= 20:
            pid = FanPidModel(
                kp_milli=_sle32(payload, 0),
                ki_milli=_sle32(payload, 4),
                kd_milli=_sle32(payload, 8),
                integral_limit_permille=_sle32(payload, 12),
                max_boost_percent=payload[16],
                low_base_percent=payload[17],
                mid_base_percent=payload[18],
                high_base_percent=payload[19],
            )
            self.fan_pid_updated.emit(pid)
            self.log_message.emit(
                "rx fan pid "
                f"kp={pid.kp_milli / 1000.0:.3f} "
                f"ki={pid.ki_milli / 1000.0:.3f} "
                f"kd={pid.kd_milli / 1000.0:.3f} "
                f"ilim={pid.integral_limit_permille} boost={pid.max_boost_percent}%"
            )
            return

        if frame.frame_type == HostFrameType.RUNTIME and len(payload) >= 46:
            runtime = RuntimeModel(
                state=payload[0],
                remaining_sec=_le32(payload, 1),
                fault_code=_le16(payload, 5),
                heartbeat_ok=bool(payload[7]),
                ntc_deci_c=[
                    _sle16(payload, 8),
                    _sle16(payload, 10),
                    _sle16(payload, 12),
                    _sle16(payload, 14),
                ],
                liquid_present=bool(payload[16]),
                cover_closed=bool(payload[17]),
                gx1832_active=bool(payload[18]),
                fan_rpm=_le16(payload, 19),
                mist_online=bool(payload[21]),
                mist_running=bool(payload[22]),
                mist_desired_level=payload[23],
                mist_actual_level=payload[24],
                mist_fault_code=_le16(payload, 25),
                mist_low_water=_le16(payload, 25) == MIST_FAULT_LOW_WATER,
                heat_enabled=bool(payload[27]),
                heat_delay_us=_le16(payload, 28),
                heat_output_permille=_le16(payload, 30),
                heat_error_deci_c=_sle16(payload, 32),
                measured_temp_deci_c=_sle16(payload, 34),
                target_temp_deci_c=_sle16(payload, 36),
                sample_uptime_ms=_le32(payload, 38),
                maintenance_active=bool(payload[42]),
                maintenance_fan_level=payload[43],
                maintenance_mist_level=payload[44],
                maintenance_heat_permille=payload[45] * 10,
            )
            if len(payload) >= 62:
                runtime.pid_kp_milli = _sle32(payload, 46)
                runtime.pid_ki_milli = _sle32(payload, 50)
                runtime.pid_kd_milli = _sle32(payload, 54)
                runtime.pid_integral_limit_permille = _sle32(payload, 58)
            if len(payload) >= 66:
                runtime.pid_i_term_raw = _sle32(payload, 62)
            if len(payload) >= 76:
                runtime.ntc_raw = [
                    _le16(payload, 66),
                    _le16(payload, 68),
                    _le16(payload, 70),
                    _le16(payload, 72),
                ]
                runtime.ntc_raw_max = _le16(payload, 74)
            if len(payload) >= 90:
                runtime.fan_pid_enabled = bool(payload[76])
                runtime.fan_pid_saturated = bool(payload[77])
                runtime.fan_pid_base_percent = payload[78]
                runtime.fan_pid_output_percent = payload[79]
                runtime.fan_pid_boost_permille = _le16(payload, 80)
                runtime.fan_pid_error_deci_c = _sle16(payload, 82)
                runtime.fan_pid_measured_temp_deci_c = _sle16(payload, 84)
                runtime.fan_pid_target_temp_deci_c = _sle16(payload, 86)
                runtime.fan_pid_i_term_permille = _sle16(payload, 88)
            if len(payload) >= 91:
                runtime.heat_control_phase = payload[90]
            if len(payload) >= 92:
                runtime.keep_warm_enabled = bool(payload[91])
            self.runtime_updated.emit(runtime)
            self.log_message.emit(
                "rx runtime "
                f"fan={runtime.fan_rpm} mist_online={int(runtime.mist_online)} "
                f"heat={'on' if runtime.heat_enabled else 'off'} "
                f"delay={runtime.heat_delay_us}us output={runtime.heat_output_permille} "
                f"kp={runtime.pid_kp_milli / 1000.0:.3f} "
                f"ki={runtime.pid_ki_milli / 1000.0:.3f} "
                f"kd={runtime.pid_kd_milli / 1000.0:.3f} "
                f"i_term={runtime.pid_i_term_raw / 1000.0:.1f} "
                f"outlet_pid={runtime.measured_temp_deci_c / 10.0:.1f}/"
                f"{runtime.target_temp_deci_c / 10.0:.1f}C "
                f"err={runtime.heat_error_deci_c / 10.0:.1f}C "
                f"kettle_raw={runtime.ntc_raw[3]}/{runtime.ntc_raw_max}"
                f" fan_pid={runtime.fan_pid_base_percent}+"
                f"{runtime.fan_pid_boost_permille / 10.0:.1f}%="
                f"{runtime.fan_pid_output_percent}%"
            )
            return

        if frame.frame_type == HostFrameType.MAINT and len(payload) >= 5:
            maintenance = MaintenanceModel(
                active=bool(payload[0]),
                fan_level=payload[1],
                mist_level=payload[2],
                heat_output_permille=_le16(payload, 3),
            )
            self.maintenance_updated.emit(maintenance)
            self.log_message.emit(
                "rx maintenance "
                f"active={int(maintenance.active)} fan={maintenance.fan_level} "
                f"mist={maintenance.mist_level} heat={maintenance.heat_output_permille}"
            )
            return

        self.log_message.emit(
            f"unhandled frame type=0x{frame.frame_type:02x} len={len(payload)}"
        )

    def request_status(self) -> None:
        self._send(HostCmd.GET_STATUS)

    def request_config(self) -> None:
        self._send(HostCmd.GET_CONFIG)

    def request_pid(self) -> None:
        self._send(HostCmd.GET_PID)

    def request_preheat_pid(self) -> None:
        self._send(HostCmd.GET_PREHEAT_PID)

    def request_fan_pid(self) -> None:
        self._send(HostCmd.GET_FAN_PID)

    def request_runtime(self) -> None:
        self._send(HostCmd.GET_RUNTIME)

    def request_maintenance(self) -> None:
        self._send(HostCmd.GET_MAINTENANCE)

    def send_heartbeat(self) -> None:
        self._send(HostCmd.HEARTBEAT)

    def send_start(self) -> None:
        self._send(HostCmd.START)

    def send_pause(self) -> None:
        self._send(HostCmd.PAUSE)

    def send_resume(self) -> None:
        self._send(HostCmd.RESUME)

    def send_stop(self) -> None:
        self._send(HostCmd.STOP)

    def clear_fault(self) -> None:
        self._send(HostCmd.CLEAR_FAULT)

    def set_mode(self, mode: int) -> None:
        self._send(HostCmd.SET_MODE, bytes([mode]))

    def set_target_temp(self, target_temp_deci_c: int) -> None:
        self._send(HostCmd.SET_TARGET_TEMP, target_temp_deci_c.to_bytes(2, "little"))

    def set_time_minutes(self, minutes: int) -> None:
        self._send(HostCmd.SET_TIME, minutes.to_bytes(2, "little"))

    def set_air_level(self, level: int) -> None:
        self._send(HostCmd.SET_AIR_LEVEL, bytes([level]))

    def set_mist_level(self, level: int) -> None:
        self._send(HostCmd.SET_MIST_LEVEL, bytes([level]))

    def set_keep_warm(self, enabled: bool) -> None:
        self._send(HostCmd.SET_KEEP_WARM, bytes([1 if enabled else 0]))

    def apply_pid(self, kp_milli: int, ki_milli: int, kd_milli: int, i_limit_permille: int) -> None:
        payload = (
            kp_milli.to_bytes(4, "little", signed=True)
            + ki_milli.to_bytes(4, "little", signed=True)
            + kd_milli.to_bytes(4, "little", signed=True)
            + i_limit_permille.to_bytes(4, "little", signed=True)
        )
        self._send(HostCmd.SET_PID, payload)

    def apply_preheat_pid(
        self, kp_milli: int, ki_milli: int, kd_milli: int, i_limit_permille: int
    ) -> None:
        payload = (
            kp_milli.to_bytes(4, "little", signed=True)
            + ki_milli.to_bytes(4, "little", signed=True)
            + kd_milli.to_bytes(4, "little", signed=True)
            + i_limit_permille.to_bytes(4, "little", signed=True)
        )
        self._send(HostCmd.SET_PREHEAT_PID, payload)

    def apply_fan_pid(
        self, kp_milli: int, ki_milli: int, kd_milli: int, i_limit_permille: int
    ) -> None:
        payload = (
            kp_milli.to_bytes(4, "little", signed=True)
            + ki_milli.to_bytes(4, "little", signed=True)
            + kd_milli.to_bytes(4, "little", signed=True)
            + i_limit_permille.to_bytes(4, "little", signed=True)
        )
        self._send(HostCmd.SET_FAN_PID, payload)

    def enter_maintenance(self) -> None:
        self._send(HostCmd.ENTER_MAINTENANCE)

    def exit_maintenance(self) -> None:
        self._send(HostCmd.EXIT_MAINTENANCE)

    def manual_set_fan(self, level: int) -> None:
        self._send(HostCmd.MANUAL_SET_FAN, bytes([level]))

    def manual_set_mist(self, level: int) -> None:
        self._send(HostCmd.MANUAL_SET_MIST, bytes([level]))

    def manual_set_heat(self, permille: int) -> None:
        self._send(HostCmd.MANUAL_SET_HEAT, permille.to_bytes(2, "little"))


if __name__ == "__main__":
    raise SystemExit(
        "serial_client.py is a library module. Run the GUI with: ./run.sh"
    )

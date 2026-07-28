from __future__ import annotations

from dataclasses import dataclass
from typing import Callable


PREAMBLE = b"\xAA\x55"
TRAILER = b"\x0D\x0A"
FRAME_OVERHEAD = 11
MAX_PAYLOAD = 240


class HostFrameType:
    CMD = 0x01
    ACK = 0x02
    NACK = 0x03
    STATUS = 0x04
    EVENT = 0x05
    CONFIG = 0x06
    PID = 0x07
    RUNTIME = 0x08
    MAINT = 0x09
    OUTLET_CTRL = 0x0A
    KETTLE_TARGET = 0x0B


class HostCmd:
    SET_MODE = 0x10
    SET_TARGET_TEMP = 0x11
    SET_TIME = 0x12
    SET_AIR_LEVEL = 0x13
    SET_MIST_LEVEL = 0x14
    START = 0x20
    PAUSE = 0x21
    RESUME = 0x22
    STOP = 0x23
    GET_STATUS = 0x24
    HEARTBEAT = 0x25
    CLEAR_FAULT = 0x26
    GET_CONFIG = 0x27
    GET_PID = 0x28
    SET_PID = 0x29
    GET_RUNTIME = 0x2A
    ENTER_MAINTENANCE = 0x2B
    EXIT_MAINTENANCE = 0x2C
    MANUAL_SET_FAN = 0x2D
    MANUAL_SET_MIST = 0x2E
    MANUAL_SET_HEAT = 0x2F
    GET_MAINTENANCE = 0x30
    GET_KETTLE_PID = 0x31
    SET_KETTLE_PID = 0x32
    GET_OUTLET_CONTROL = 0x33
    SET_OUTLET_CONTROL = 0x34
    GET_KETTLE_TARGET = 0x35
    SET_KETTLE_TARGET = 0x36


@dataclass
class DecodedFrame:
    frame_id: int
    frame_type: int
    payload: bytes


@dataclass
class AckFrame:
    command_id: int
    ok: bool
    error_code: int


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def build_command(frame_id: int, command_id: int, payload: bytes = b"") -> bytes:
    body = bytes([command_id]) + payload
    header = (
        PREAMBLE
        + frame_id.to_bytes(2, "little")
        + bytes([HostFrameType.CMD])
        + len(body).to_bytes(2, "little")
        + body
    )
    crc = crc16_modbus(header[2: 7 + len(body)])
    return header + crc.to_bytes(2, "little") + TRAILER


def extract_frames(
    rx_buffer: bytearray,
    on_frame: Callable[[DecodedFrame], None],
    on_error: Callable[[str], None],
) -> None:
    while len(rx_buffer) >= FRAME_OVERHEAD:
        start = rx_buffer.find(PREAMBLE)
        if start < 0:
            rx_buffer.clear()
            return
        if start > 0:
            del rx_buffer[:start]
        if len(rx_buffer) < FRAME_OVERHEAD:
            return
        payload_len = int.from_bytes(rx_buffer[5:7], "little")
        if payload_len > MAX_PAYLOAD:
            on_error("payload too large")
            del rx_buffer[0]
            continue
        total_len = FRAME_OVERHEAD + payload_len
        if len(rx_buffer) < total_len:
            return
        frame = bytes(rx_buffer[:total_len])
        if frame[-2:] != TRAILER:
            on_error("trailer mismatch")
            del rx_buffer[0]
            continue
        actual_crc = int.from_bytes(frame[7 + payload_len: 9 + payload_len], "little")
        expected_crc = crc16_modbus(frame[2: 7 + payload_len])
        if actual_crc != expected_crc:
            on_error("crc mismatch")
            del rx_buffer[:total_len]
            continue
        on_frame(
            DecodedFrame(
                frame_id=int.from_bytes(frame[2:4], "little"),
                frame_type=frame[4],
                payload=frame[7: 7 + payload_len],
            )
        )
        del rx_buffer[:total_len]


def parse_ack(frame: DecodedFrame) -> AckFrame | None:
    if frame.frame_type not in (HostFrameType.ACK, HostFrameType.NACK):
        return None
    if len(frame.payload) < 3:
        return None
    return AckFrame(frame.payload[0], bool(frame.payload[1]), frame.payload[2])

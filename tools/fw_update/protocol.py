"""Boot protocol v1. No dependencies on either desktop application's protocol module."""
import struct
import zlib
from dataclasses import dataclass

TARGET = 0x39430001
APP_BASE = 0x08004000
APP_MAX = 110 * 1024
META_BASE = 0x0801F800
RAM_END = 0x20008FF0
VERSION = 1
CHUNK = 120
ENTER, INFO, BEGIN, DATA, END, RUN = range(0xF0, 0xF6)
COOKIE = b'NBLBOOT1'
REPLY = 0x0C
PACKAGE_MAGIC = b'NBLUPD1\0'
ERRORS = {1: 'invalid request', 2: 'wrong product', 3: 'address/size/alignment invalid',
          4: 'invalid update state', 5: 'unexpected offset or conflicting retry',
          6: 'Flash failure; restart the entire update', 7: 'image CRC mismatch',
          8: 'invalid App vectors'}

def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8): crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc

def frame(sequence, kind, payload):
    if not 1 <= sequence <= 65535 or len(payload) > 128:
        raise ValueError('invalid frame sequence/length')
    body = struct.pack('<HBH', sequence, kind, len(payload)) + payload
    return b'\xaa\x55' + body + struct.pack('<H', crc16(body)) + b'\r\n'

class Decoder:
    def __init__(self): self.buffer = bytearray()
    def feed(self, data):
        self.buffer.extend(data)
        result = []
        while True:
            at = self.buffer.find(b'\xaa\x55')
            if at < 0:
                self.buffer[:] = b'\xaa' if self.buffer[-1:] == b'\xaa' else b''
                break
            del self.buffer[:at]
            if len(self.buffer) < 7: break
            seq, kind, length = struct.unpack_from('<HBH', self.buffer, 2)
            if length > 128:
                del self.buffer[0]; continue
            total = length + 11
            if len(self.buffer) < total: break
            packet = bytes(self.buffer[:total])
            if packet[-2:] != b'\r\n' or crc16(packet[2:7+length]) != struct.unpack_from('<H',packet,7+length)[0]:
                del self.buffer[0]; continue
            del self.buffer[:total]
            result.append((seq, kind, packet[7:7+length]))
        return result

@dataclass(frozen=True)
class BootReply:
    command: int
    error: int
    version: int
    target: int
    address: int
    capacity: int
    next_offset: int
    valid: int
    length: int
    crc: int

    @classmethod
    def parse(cls, payload):
        if len(payload) != 32: raise ValueError('invalid Boot reply size')
        reply = cls(*struct.unpack('<BBH7I', payload))
        if (reply.version, reply.target, reply.address, reply.capacity) != (VERSION,TARGET,APP_BASE,APP_MAX):
            raise ValueError('Boot product/protocol/layout mismatch; update blocked')
        if reply.valid not in (0, 1) or reply.next_offset > APP_MAX:
            raise ValueError('invalid Boot state')
        return reply

def validate_app(data):
    if not 192 <= len(data) <= APP_MAX: raise ValueError('App size exceeds partition or vectors incomplete')
    sp, pc = struct.unpack_from('<II', data)
    if not (0x20000000 < sp <= RAM_END and sp % 8 == 0 and pc & 1 and
            APP_BASE <= (pc & ~1) < APP_BASE + len(data)):
        raise ValueError('App vectors/address invalid; use upgrade-release/upgrade-debug package')

@dataclass(frozen=True)
class Firmware:
    data: bytes
    crc: int

    @classmethod
    def load(cls, path):
        blob = path.read_bytes()
        if len(blob) < 32: raise ValueError('truncated firmware package')
        magic, version, target, address, length, crc, header_crc = struct.unpack_from('<8s6I', blob)
        if (magic,version,target,address) != (PACKAGE_MAGIC,VERSION,TARGET,APP_BASE):
            raise ValueError('wrong firmware package product/address/version')
        if zlib.crc32(blob[:28]) != header_crc or len(blob) != 32 + length:
            raise ValueError('firmware package header/length damaged')
        data = blob[32:]
        validate_app(data)
        if zlib.crc32(data) != crc: raise ValueError('firmware package CRC mismatch')
        return cls(data,crc)

def package_bytes(data):
    validate_app(data)
    header = struct.pack('<8s5I',PACKAGE_MAGIC,VERSION,TARGET,APP_BASE,len(data),zlib.crc32(data))
    return header + struct.pack('<I',zlib.crc32(header)) + data

def metadata_bytes(data):
    validate_app(data)
    header = struct.pack('<6I',0x314D424E,VERSION,len(data),zlib.crc32(data),APP_BASE,TARGET)
    return header + struct.pack('<4I',zlib.crc32(header),0,0xC35A6996,0x3CA59669)

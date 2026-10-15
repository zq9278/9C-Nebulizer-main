"""Stop-and-wait uploader. Retries preserve sequence and payload exactly."""
import argparse
from pathlib import Path
import sys
import struct
import threading
import time
import secrets
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from fw_update.protocol import (Firmware, Decoder, BootReply, frame, COOKIE, TARGET,
    APP_BASE, CHUNK, ENTER, INFO, BEGIN, DATA, END, RUN, REPLY, ERRORS)

class UpdateCancelled(Exception): pass
class BootRejected(RuntimeError): pass

class Updater:
    def __init__(self, port, log=print, progress=lambda value: None, cancel=None, clock=time.monotonic):
        self.port, self.log, self.progress = port, log, progress
        self.cancel = cancel if cancel is not None else threading.Event()
        self.clock = clock
        self.decoder = Decoder()
        self.sequence = secrets.randbelow(65535)

    def check_cancel(self):
        if self.cancel.is_set():
            raise UpdateCancelled('Upgrade cancelled. If erase started, reconnect and send the complete package again.')

    def next_sequence(self):
        self.sequence = self.sequence % 65535 + 1
        return self.sequence

    def send(self, packet):
        if self.port.write(packet) != len(packet): raise IOError('short serial write; result unknown')
        self.port.flush()

    def read_frames(self):
        return self.decoder.feed(self.port.read(max(1, min(self.port.in_waiting, 512))))

    def request(self, command, payload=b'', timeout=1.0, attempts=3):
        seq = self.next_sequence()
        packet = frame(seq,1,bytes([command])+payload)
        for attempt in range(attempts):
            self.check_cancel()
            self.send(packet)
            deadline = self.clock() + timeout
            while self.clock() < deadline:
                self.check_cancel()
                for got_seq, kind, data in self.read_frames():
                    if got_seq == seq and kind == REPLY:
                        reply = BootReply.parse(data)
                        if reply.command != command: continue
                        if reply.error:
                            raise BootRejected(f'Boot 0x{command:02X}: '+ERRORS.get(reply.error,f'error {reply.error}'))
                        return reply
            self.log(f'0x{command:02X} response timeout ({attempt+1}/{attempts})')
        raise TimeoutError(f'Boot 0x{command:02X} timed out; stop and reconnect to check the device')

    def connect_boot(self):
        # An invalid/incomplete App is already in Boot and must not depend on App code.
        try:
            return self.request(INFO,timeout=.4,attempts=1)
        except TimeoutError:
            pass
        self.log('Requesting App to enter Boot (idle/paused/fault only; keep-warm and maintenance must be stopped)')
        seq = self.next_sequence()
        self.send(frame(seq,1,bytes([ENTER])+COOKIE))
        deadline = self.clock()+.5
        while self.clock()<deadline:
            self.check_cancel()
            for got_seq,kind,data in self.read_frames():
                if got_seq==seq and kind==3 and len(data)==4 and data[0]==ENTER:
                    raise BootRejected(f'App rejected ENTER_BOOT (0x{data[2]:02X}). '
                                       'Stop treatment/keep-warm/maintenance and release debug control; '
                                       'old standalone firmware needs first installation with a programmer.')
        # Boot may reset during an App ACK; an INFO reply is the authoritative handshake.
        return self.request(INFO,timeout=1,attempts=5)

    def update(self, firmware, run=True):
        self.progress(0)
        reply = self.connect_boot()
        self.log(f'Boot v{reply.version}; App valid={reply.valid}; capacity={reply.capacity} bytes')
        self.check_cancel()
        self.log('BEGIN: invalidating old App record and erasing the App partition')
        reply = self.request(BEGIN,struct.pack('<4I',TARGET,APP_BASE,len(firmware.data),firmware.crc),timeout=8)
        if reply.next_offset != 0 or reply.valid:
            raise RuntimeError('unexpected BEGIN state; transfer stopped')
        for offset in range(0,len(firmware.data),CHUNK):
            chunk = firmware.data[offset:offset+CHUNK]
            reply = self.request(DATA,struct.pack('<I',offset)+chunk,timeout=1)
            if reply.next_offset != offset+len(chunk) or reply.valid:
                raise RuntimeError('unexpected DATA offset/state; transfer stopped')
            self.progress(min(99,reply.next_offset*100//len(firmware.data)))
        reply = self.request(END,struct.pack('<II',len(firmware.data),firmware.crc),timeout=5)
        if not reply.valid or (reply.length,reply.crc)!=(len(firmware.data),firmware.crc):
            raise RuntimeError('Boot did not confirm the complete image')
        self.progress(100)
        self.log('Flash CRC verified; App validity record committed')
        if not run: return 'Firmware installed and verified; device remains in Boot until reset.'
        # Do not replay RUN after a reset. A missing reply is not evidence of failure.
        try: self.request(RUN,struct.pack('<I',TARGET),timeout=1,attempts=1)
        except TimeoutError: self.log('RUN reply missing; checking actual App startup')
        if self.verify_app_started():
            return 'Firmware installed and verified; App startup confirmed by USART1 status reply.'
        return 'Firmware installed and verified; App startup was not confirmed. Reconnect to inspect the device.'

    def verify_app_started(self):
        # Normal Boot startup window is 1.5s. A status response must echo a fresh sequence.
        deadline = self.clock()+5
        while self.clock()<deadline:
            self.check_cancel()
            seq = self.next_sequence()
            self.send(frame(seq,1,b'\x24'))
            until = min(deadline,self.clock()+.5)
            while self.clock()<until:
                for got_seq,kind,data in self.read_frames():
                    # Status frames have independently assigned IDs in App; ACK echoes ours.
                    if got_seq==seq and kind==2 and len(data)==4 and data[:3]==b'\x24\x01\x00':
                        return True
        return False

def main():
    parser = argparse.ArgumentParser(description='Nebulizer USART1 single-App update')
    parser.add_argument('--port',required=True)
    parser.add_argument('--firmware',type=Path,required=True)
    parser.add_argument('--no-run',action='store_true')
    args = parser.parse_args()
    firmware = Firmware.load(args.firmware)  # Validate before opening the device.
    import serial
    with serial.Serial(args.port,115200,timeout=.05,write_timeout=2) as port:
        print(Updater(port).update(firmware,run=not args.no_run))

if __name__=='__main__': main()

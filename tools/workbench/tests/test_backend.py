"""No-hardware service tests; exercise the real serial codec and snapshot decoder."""
from pathlib import Path
import sys
import struct
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/workbench/backend'))
import server
from fw_update.protocol import frame,Decoder
from protocol import decode_frames

class FakeSerial:
    def __init__(self):
        mailbox=(ROOT/'build/tests/debug_mailbox.bin').read_bytes();length=struct.unpack_from('<I',mailbox,88)[0]
        self.frames=decode_frames(mailbox[96:96+length]);self.rx=bytearray();self.closed=False;self.reject=None;self.commands=[]
    @property
    def in_waiting(self):return len(self.rx)
    def write(self,data):
        seq,kind,payload=Decoder().feed(data)[0];cmd=payload[0];self.commands.append(cmd)
        types={0x24:4,0x27:6,0x28:7,0x33:11,0x31:10,0x2A:8,0x30:9}
        if cmd in types:self.rx.extend(frame(1,types[cmd],self.frames[types[cmd]]))
        self.rx.extend(frame(seq,3 if cmd==self.reject else 2,bytes([cmd,0 if cmd==self.reject else 1,0x40 if cmd==self.reject else 0,0])))
        return len(data)
    def flush(self):pass
    def read(self,n):data=bytes(self.rx[:n]);del self.rx[:n];return data
    def close(self):self.closed=True

class BackendTests(unittest.TestCase):
    def setUp(self):self.events=[];self.event_patch=patch('server.event',side_effect=lambda *x:self.events.append(x));self.event_patch.start()
    def tearDown(self):self.event_patch.stop()
    def test_serial_decode_observe_and_control(self):
        port=FakeSerial()
        with patch('serial.Serial',return_value=port):session=server.SerialSession({'port':'SIM'})
        self.assertFalse(session.owned);snap=session.read_snapshot();self.assertIn('runtime',snap);self.assertEqual(len(snap['runtime']['temperatures']),4)
        self.assertNotIn(0x25,port.commands)
        session.owned=True;session.read_snapshot();self.assertIn(0x25,port.commands)
        port.reject=0x20
        with self.assertRaises(RuntimeError):session.submit(0x20,b'')
        port.reject=None;session.close();self.assertTrue(port.closed);self.assertEqual(port.commands[-3:],[0x2C,0x15,0x21])
    def test_exclusive_jobs_and_confirmation_before_hardware(self):
        service=server.Service();service.quit.set();service.thread.join(1)
        with patch('server.program_factory') as program:
            with self.assertRaises(ValueError):service.dispatch('factory',{'confirmed':False})
            service.busy=True
            with self.assertRaises(ValueError):service.dispatch('connect',{})
            with self.assertRaises(ValueError):service.dispatch('factory',{'confirmed':True})
            program.assert_not_called()
    def test_failure_blocks_remaining_batch(self):
        service=server.Service();service.quit.set();service.thread.join(1)
        port=FakeSerial()
        with patch('serial.Serial',return_value=port):service.session=server.SerialSession({'port':'SIM'})
        service.kind='serial';service.session.owned=True;port.reject=0x20
        with self.assertRaises(RuntimeError):service.dispatch('commands',{'commands':[{'command':0x20},{'command':0x14,'payload':[3]}]})
        self.assertNotIn(0x14,port.commands);port.reject=None;service.disconnect()

if __name__=='__main__':unittest.main(verbosity=2)

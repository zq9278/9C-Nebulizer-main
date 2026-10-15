"""Host/package checks and real C Boot protocol round trips. Run after run_tests.py."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import zlib
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
sys.path.insert(0,str(ROOT/'tools'))
from fw_update.protocol import (Firmware,Decoder,frame,package_bytes,metadata_bytes,
    BootReply,APP_BASE,APP_MAX,RAM_END,TARGET,INFO,ENTER,BEGIN,DATA,END,RUN,REPLY)
from fw_update.client import Updater,UpdateCancelled,BootRejected

class CBootPort:
    def __init__(self,drop=(),corrupt_data=False,app=False,reject_enter=False,cancel=None):
        env=os.environ.copy()
        if os.name=='nt':
            bins=sorted(Path('C:/Program Files/JetBrains').glob('CLion */bin/mingw/bin'))
            if bins:env['PATH']=str(bins[-1])+os.pathsep+env.get('PATH','')
        exe=ROOT/'build/tests'/('test_boot_update.exe' if os.name=='nt' else 'test_boot_update')
        self.process=subprocess.Popen([str(exe),'--emulate'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,env=env)
        self.buffer=bytearray();self.drop=set(drop);self.corrupt=corrupt_data
        self.app=app;self.reject=reject_enter;self.cancel=cancel;self.writes=[];self.time=0.
    @property
    def in_waiting(self):return len(self.buffer)
    def clock(self):self.time+=.01;return self.time
    def write(self,packet):
        seq,kind,payload=Decoder().feed(packet)[0];cmd=payload[0]
        self.writes.append((seq,cmd,payload))
        if self.app:
            if cmd==ENTER:
                self.buffer.extend(frame(seq,3 if self.reject else 2,bytes([ENTER,0 if self.reject else 1,0x40 if self.reject else 0,0])))
                if not self.reject:self.app=False
            return len(packet)
        wire=packet
        if self.corrupt and cmd==DATA:
            bad=bytearray(payload);bad[-1]^=1;wire=frame(seq,kind,bad)
            self.corrupt=False
        self.process.stdin.write(struct.pack('<H',len(wire))+wire);self.process.stdin.flush()
        prefix=self.process.stdout.read(2)
        if len(prefix)!=2:raise RuntimeError('C emulator failed')
        result=self.process.stdout.read(struct.unpack('<H',prefix)[0])
        if cmd in self.drop:self.drop.remove(cmd)
        else:self.buffer.extend(result)
        if self.cancel and cmd==DATA:self.cancel.set()
        return len(packet)
    def flush(self):pass
    def read(self,n):
        data=bytes(self.buffer[:n]);del self.buffer[:n];return data
    def close(self):
        self.process.stdin.close();self.process.wait(timeout=3);self.process.stdout.close()
    def __enter__(self):return self
    def __exit__(self,*args):self.close()

class UpgradeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.firmware=Firmware.load(ROOT/'build/upgrade-release/nebulizer.nfu')
    def test_package_and_factory_records(self):
        data=self.firmware.data
        factory=(ROOT/'build/upgrade-release/nebulizer_factory.bin').read_bytes()
        self.assertEqual(len(factory),128*1024)
        self.assertEqual(factory[0x4000:0x4000+len(data)],data)
        self.assertEqual(factory[0x1F800:0x1F828],metadata_bytes(data))
        self.assertEqual(zlib.crc32(data),self.firmware.crc)
        for line in (ROOT/'build/upgrade-release/nebulizer_factory.hex').read_text().splitlines():
            self.assertEqual(sum(bytes.fromhex(line[1:])) & 255,0)
    def test_package_rejects_corruption_and_old_address(self):
        blob=package_bytes(self.firmware.data)
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'test.nfu'
            for at in (0,12,20,28,40,len(blob)-1):
                bad=bytearray(blob);bad[at]^=1;path.write_bytes(bad)
                with self.assertRaises(ValueError):Firmware.load(path)
            path.write_bytes(blob[:-1])
            with self.assertRaises(ValueError):Firmware.load(path)
        old=(ROOT/'build/debug/nebulizer.bin').read_bytes()
        with self.assertRaises(ValueError):package_bytes(old)
    def test_fragment_noise_crc_and_reply_validation(self):
        decoder=Decoder();packet=frame(123,1,b'\xf1')
        self.assertEqual(decoder.feed(b'noise'+packet[:5]),[])
        self.assertEqual(decoder.feed(packet[5:]),[(123,1,b'\xf1')])
        bad=bytearray(packet);bad[-3]^=1
        self.assertEqual(decoder.feed(bad+packet),[(123,1,b'\xf1')])
        for target,address,capacity in ((0,APP_BASE,APP_MAX),(TARGET,0,APP_MAX),(TARGET,APP_BASE,0)):
            with self.assertRaises(ValueError):BootReply.parse(struct.pack('<BBH7I',INFO,0,1,target,address,capacity,0,0,0,0))
    def test_full_image_actual_c_protocol_and_lost_acks(self):
        with CBootPort(drop=(BEGIN,DATA,END,RUN)) as port:
            progress=[]
            result=Updater(port,log=lambda s:None,progress=progress.append,clock=port.clock).update(self.firmware)
            self.assertIn('startup confirmed',result)
            self.assertEqual(progress[-1],100)
            for cmd in (BEGIN,DATA,END):
                requests=[w for w in port.writes if w[1]==cmd]
                self.assertEqual(requests[0],requests[1])
            self.assertEqual(sum(w[1]==RUN for w in port.writes),1)
    def test_c_flash_crc_rejection_blocks_run(self):
        with CBootPort(corrupt_data=True) as port:
            with self.assertRaisesRegex(BootRejected,'CRC'):
                Updater(port,log=lambda s:None,clock=port.clock).update(self.firmware)
            self.assertFalse(any(w[1]==RUN for w in port.writes))
    def test_cancel_stops_additional_writes(self):
        cancel=threading.Event()
        with CBootPort(cancel=cancel) as port:
            with self.assertRaises(UpdateCancelled):
                Updater(port,log=lambda s:None,cancel=cancel,clock=port.clock).update(self.firmware)
            self.assertEqual(sum(w[1]==DATA for w in port.writes),1)
            self.assertFalse(any(w[1] in (END,RUN) for w in port.writes))
    def test_enter_from_app_and_rejected_active_state(self):
        with CBootPort(app=True) as port:
            reply=Updater(port,log=lambda s:None,clock=port.clock).connect_boot()
            self.assertEqual(reply.command,INFO)
            self.assertEqual(sum(w[1]==ENTER for w in port.writes),1)
        with CBootPort(app=True,reject_enter=True) as port:
            with self.assertRaisesRegex(BootRejected,'rejected'):
                Updater(port,log=lambda s:None,clock=port.clock).connect_boot()
            self.assertFalse(any(w[1]==BEGIN for w in port.writes))

class UpgradeGuiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from PyQt6.QtWidgets import QApplication
        from PyQt6.QtGui import QFontDatabase,QFont
        cls.app=QApplication.instance() or QApplication([])
        # The Windows offscreen platform has no system fonts by default.
        font=Path('C:/Windows/Fonts/msyh.ttc')
        if not QFontDatabase.families() and font.exists():
            QFontDatabase.addApplicationFont(str(font))
            cls.app.setFont(QFont('Microsoft YaHei',10))
    def test_worker_owns_port_and_dialog_finishes(self):
        from fw_update.gui import UpgradeDialog
        window=UpgradeDialog(port='SIMULATED')
        window.package.setText(str(ROOT/'build/upgrade-release/nebulizer.nfu'))
        with patch('serial.Serial',side_effect=lambda *a,**k:CBootPort()):
            window.begin()
            self.assertFalse(window.start.isEnabled())
            self.assertFalse(window.port.isEnabled())
            deadline=time.monotonic()+5
            while window.worker and time.monotonic()<deadline:
                self.app.processEvents();time.sleep(.005)
            if window.worker:
                window.cancel_update();window.worker.wait(3000);self.app.processEvents()
                self.fail('upgrade worker did not finish')
            self.assertTrue(window.start.isEnabled())
            self.assertEqual(window.progress.value(),100)
            self.assertIn('startup confirmed',window.logs.toPlainText())
        window.show();self.app.processEvents()
        window.grab().save(str(ROOT/'build/tests/firmware_upgrade_ui.png'))
        window.close()
    def test_invalid_package_never_opens_port(self):
        from fw_update.gui import UpgradeDialog
        window=UpgradeDialog(port='SIMULATED');window.package.setText('missing.nfu')
        with patch('serial.Serial') as port,patch('fw_update.gui.QMessageBox.warning') as warning:
            window.begin();port.assert_not_called();warning.assert_called_once()
            self.assertIsNone(window.worker)
        window.close()

if __name__=='__main__':unittest.main(verbosity=2)

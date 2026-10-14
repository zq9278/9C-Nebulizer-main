"""Run after tools/run_tests.py and a Debug firmware build. No hardware needed."""
import copy
import os
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/nebulizer_debug'))
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from protocol import ACQUIRE, RELEASE, decode_snapshot, validate_params
from transport import ElfImage, Session

class FakeLink:
    def __init__(self,image):
        self.base=image.address
        self.ram=bytearray((ROOT/'build/tests/debug_mailbox.bin').read_bytes())
        self.flash={a:bytearray(b) for a,b in image.segments}
        self.writes=[]
        self.paused=False
        self.short_write=False
        self.auto_ack=False
        self.advance=False
        self.reject=None
        self.closed=False
        struct.pack_into('<II',self.ram,24,0,0)
        self.ram[32:88]=bytes(56)
    def halted(self):return self.paused
    def close(self):self.closed=True
    def memory_read8(self,address,count):
        if address==self.base and self.advance:
            self.publish()
        if self.base<=address<self.base+len(self.ram):
            return list(self.ram[address-self.base:address-self.base+count])
        for base,blob in self.flash.items():
            if base<=address<base+len(blob):return list(blob[address-base:address-base+count])
        raise IOError('invalid address')
    def memory_write8(self,address,data):
        self.writes.append((address-self.base,bytes(data)))
        self.ram[address-self.base:address-self.base+len(data)]=bytes(data)
        return len(data)-(1 if self.short_write else 0)
    def memory_write32(self,address,data):
        result=self.memory_write8(address,struct.pack('<'+'I'*len(data),*data))
        if address-self.base==32 and data[0] and self.auto_ack:
            seq,session,cmd=struct.unpack_from('<III',self.ram,32)
            error=0x40 if cmd==self.reject else 0
            struct.pack_into('<4I',self.ram,72,seq,session,cmd,error)
            if not error and cmd==ACQUIRE:struct.pack_into('<II',self.ram,24,session,1)
            if not error and cmd==RELEASE:struct.pack_into('<II',self.ram,24,0,0)
        return result
    def publish(self):
        seq,uptime=struct.unpack_from('<II',self.ram,16)
        struct.pack_into('<II',self.ram,16,seq+2,uptime+100)

class HostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.image=ElfImage(ROOT/'build/debug/nebulizer.elf')
    def setUp(self):
        self.link=FakeLink(self.image); self.now=0.
        self.session=Session(self.link,self.image,clock=lambda:self.now)
    def ack(self,error=0):
        seq,cmd,_=self.session.pending
        struct.pack_into('<4I',self.link.ram,72,seq,self.session.session,cmd,error)
    def test_connect_observer_never_writes(self):
        self.session.heartbeat();self.session.read_snapshot();self.session.events()
        self.assertEqual(self.link.writes,[])
        with self.assertRaises(RuntimeError):self.session.submit(0x20)
        self.assertEqual(self.link.writes,[])
    def test_wrong_elf_flash_blocks_all_writes(self):
        self.link.flash[next(iter(self.link.flash))][0]^=1
        with self.assertRaisesRegex(ValueError,'Flash'):Session(self.link,self.image)
        self.assertEqual(self.link.writes,[])
    def test_padding_excluded_and_data_verified(self):
        for address in range(0x080000B8,0x080000C0):
            self.assertFalse(any(a<=address<a+len(b) for a,b in self.image.segments))
        address,blob=self.image.segments[-1]
        self.assertEqual(len(blob),20)
        self.link.flash[address][3]^=1
        with self.assertRaisesRegex(ValueError,f'0x{address+3:08X}'):
            Session(self.link,self.image)
        self.assertEqual(self.link.writes,[])
    def test_short_flash_read(self):
        with patch.object(self.link,'memory_read8',return_value=[]):
            with self.assertRaises(IOError):self.image.verify(self.link)
        self.assertEqual(self.link.writes,[])
    def test_word_write_requires_four_bytes(self):
        self.session.write_word(64,123)
        self.assertEqual(struct.unpack_from('<I',self.link.ram,64)[0],123)
        for count in (0,1,2,3):
            with patch.object(self.link,'memory_write32',return_value=count):
                with self.assertRaises(IOError):self.session.write_word(64,456)
    def test_commit_order_and_ack_identity(self):
        self.session.submit(ACQUIRE)
        self.assertEqual([offset for offset,data in self.link.writes],[64,68,32,36,32])
        self.assertEqual(len(self.link.writes[-1][1]),4)
        self.assertIsNone(self.session.response())
        self.ack();struct.pack_into('<I',self.link.ram,76,999)
        self.assertIsNone(self.session.response())
        self.ack();self.assertEqual(self.session.response(),(ACQUIRE,0));self.assertTrue(self.session.owned)
    def test_timeout_no_retransmit(self):
        self.session.submit(ACQUIRE); before=len(self.link.writes)
        self.now=6
        with self.assertRaises(TimeoutError):self.session.response()
        self.assertEqual(before,len(self.link.writes))
    def test_other_owner_heartbeat_untouched(self):
        struct.pack_into('<II',self.link.ram,24,999,1)
        with self.assertRaises(RuntimeError):self.session.submit(ACQUIRE)
        self.assertEqual(self.link.writes,[])
    def test_crc_and_torn_snapshot_rejected(self):
        self.link.ram[110]^=1
        with self.assertRaises(ValueError):self.session.read_snapshot()
        self.link.ram[110]^=1;struct.pack_into('<I',self.link.ram,16,3)
        with self.assertRaises(RuntimeError):self.session.read_snapshot()
    def test_stale_and_reset_and_halt(self):
        self.now=3
        with self.assertRaisesRegex(RuntimeError,'两秒'):self.session.read_snapshot()
        self.now=0;self.link.publish();self.session.read_snapshot()
        struct.pack_into('<I',self.link.ram,16,2)
        with self.assertRaisesRegex(RuntimeError,'重启'):self.session.read_snapshot()
        self.link.paused=True
        with self.assertRaisesRegex(RuntimeError,'暂停'):self.session.read_snapshot()
    def test_short_write_does_not_commit(self):
        self.session.owned=True;self.link.short_write=True
        with self.assertRaises(IOError):self.session.submit(0x20)
        self.assertEqual(self.link.writes[-1][0],32)
        self.assertEqual(self.link.writes[-1][1],bytes(4))
    def test_event_history_and_overrun(self):
        events,lost=self.session.events()
        self.assertEqual([e['sequence'] for e in events],list(range(5,21)))
        self.assertEqual(lost,0)
        self.session.event_cursor=1
        events,lost=self.session.events();self.assertEqual(lost,3);self.assertEqual(len(events),16)
    def test_c_encoded_snapshot(self):
        snap=self.session.snapshot
        self.assertEqual(snap['config']['target_temp_deci_c'],420)
        self.assertEqual(snap['runtime']['state'],2)
        self.assertEqual(snap['threshold'],300)
        self.assertEqual(snap['full_power'],1000)
    def test_batch_nack_stops_remaining_and_releases(self):
        import worker
        self.link.auto_ack=True;self.link.advance=True;self.link.reject=0x29
        w=worker.DeviceWorker(dict(elf=str(self.image.path),serial='123',device='STM32G070CB',speed=1000,dll=''))
        results=[]
        def done(token,ok,result):
            results.append((token,ok))
            if token=='acquire':w.batch('pid',[(0x29,bytes(16)),(0x20,b'')])
            else:w.stop_requested.set()
        w.completed.connect(done)
        w.batch('acquire',[(ACQUIRE,b'')])
        with patch.object(worker,'open_probe',return_value=self.link):w.run()
        self.assertEqual(results,[('acquire',True),('pid',False)])
        submitted=[struct.unpack_from('<I',data,4)[0] for off,data in self.link.writes if off==36]
        self.assertEqual(submitted,[ACQUIRE,0x29,RELEASE]);self.assertTrue(self.link.closed)

class GuiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from PyQt6.QtWidgets import QApplication
        from PyQt6.QtGui import QFontDatabase
        from app import STYLE
        cls.qt=QApplication.instance() or QApplication([]);cls.qt.setStyle('Fusion');cls.qt.setStyleSheet(STYLE)
        # Qt's Windows offscreen plugin does not enumerate system fonts.
        for font in ('C:/Windows/Fonts/msyh.ttc','C:/Windows/Fonts/segoeui.ttf'):
            if Path(font).exists():QFontDatabase.addApplicationFont(font)
    def setUp(self):
        from app import Window
        self.window=Window()
        data=(ROOT/'build/tests/debug_mailbox.bin').read_bytes()
        size=struct.unpack_from('<I',data,88)[0]
        self.snap=decode_snapshot(data[96:96+size]);self.snap.update(sequence=2,uptime_ms=100,owner=0,control=False)
    def tearDown(self):self.window.close()
    def test_dirty_edits_survive_poll_and_import_validation(self):
        self.window.on_snapshot(copy.deepcopy(self.snap))
        self.window.fields['duration_min'].setValue(25)
        self.window.on_snapshot(copy.deepcopy(self.snap))
        self.assertEqual(self.window.fields['duration_min'].value(),25)
        p=self.window.parameters();self.assertEqual(validate_params(p)['duration_min'],25)
        p['kp']=float('nan')
        with self.assertRaises(ValueError):validate_params(p)
    def test_read_only_monitor_has_no_implicit_command(self):
        self.snap['config'].update(mode=1,keep_warm_enabled=1)
        with patch.object(self.window,'submit') as submit:
            self.window.on_snapshot(self.snap);self.window.read_all();self.window.keep_warm_tick()
            submit.assert_not_called()
    def test_two_hour_timer_only_for_controlled_keep_warm(self):
        import time
        self.snap['control']=True;self.snap['runtime']['state']=10
        self.window.last=self.snap;self.window.warm_started=time.monotonic()-7201
        with patch.object(self.window,'submit') as submit:
            self.window.keep_warm_tick();self.window.keep_warm_tick()
            submit.assert_called_once_with('warm_timeout',[(0x15,b'\0')])
    def test_batch_edits_are_disabled_and_config_order(self):
        self.window.busy=True;self.window.refresh_enabled()
        self.assertFalse(self.window.fields['duration_min'].isEnabled())
        commands=self.window.config_commands()
        self.assertEqual(commands[0],(0x15,b'\0'))
        self.assertEqual([cmd for cmd,data in commands],[0x15,0x10,0x11,0x12,0x13,0x14,0x15])
    def test_render_tabs(self):
        import math
        for i in range(240):
            snap=copy.deepcopy(self.snap);snap['sequence']=2*i+2;snap['uptime_ms']=1000*i+100
            r=snap['runtime'];r['sample_ms']=snap['uptime_ms']
            r['temperatures']=[round(250+170*(1-math.exp(-i/60))),310,325,650]
            r['heat_target']=420;r['heat_error']=420-r['temperatures'][0];r['heat_output']=min(200,r['heat_error']*9)
            r['fan_output']=35
            self.window.on_snapshot(snap)
        self.window.show();self.qt.processEvents()
        self.assertEqual(len(self.window.panels),5)
        self.assertTrue(self.window.grab().save(str(ROOT/'build/tests/debug_dashboard.png')))
    def test_curve_selection_survives_restart(self):
        import tempfile
        from PyQt6.QtCore import QSettings
        from app import Window
        with tempfile.TemporaryDirectory() as folder:
            settings=QSettings(str(Path(folder)/'settings.ini'),QSettings.Format.IniFormat)
            with patch('app.QSettings',return_value=settings):
                first=Window()
                try:
                    self.assertTrue(all(cb.isChecked() for cb in first.series_checks.values()))
                    for name,cb in first.series_checks.items():cb.setChecked(name=='PB11')
                finally:first.close()
                second=Window()
                try:
                    self.assertTrue(second.series_checks['PB11'].isChecked())
                    self.assertEqual(second.trend.hidden,set(second.series_checks)-{'PB11'})
                    self.assertTrue(all(not cb.isChecked() for name,cb in second.series_checks.items() if name!='PB11'))
                finally:second.close()
    def test_wheel_does_not_change_parameters(self):
        from PyQt6.QtCore import QPoint, QPointF, Qt
        from PyQt6.QtGui import QWheelEvent
        widgets=[self.window.fields['duration_min'],self.window.pid_fields['heat'][0],self.window.mode,
                 self.window.manual_heat,self.window.manual_fan,self.window.speed]
        for widget in widgets:
            widget.setFocus()
            before=widget.currentIndex() if hasattr(widget,'currentIndex') else widget.value()
            event=QWheelEvent(QPointF(5,5),QPointF(5,5),QPoint(),QPoint(0,120),
                              Qt.MouseButton.NoButton,Qt.KeyboardModifier.NoModifier,
                              Qt.ScrollPhase.NoScrollPhase,False)
            self.qt.sendEvent(widget,event)
            after=widget.currentIndex() if hasattr(widget,'currentIndex') else widget.value()
            self.assertEqual(before,after)
    def test_curve_ring_overwrite_and_visible_range(self):
        from trend import SampleBuffer
        buf=SampleBuffer(5)
        for i in range(8):buf.append(dict(time=i,sample_ms=i*100,value=i))
        self.assertEqual([v['time'] for v in buf],[3,4,5,6,7])
        self.assertEqual([v['time'] for v in buf.visible(4,6)],[4,5,6])
        self.assertEqual([v['time'] for v in buf.visible(0,10,3)],[3,5,7])
        self.assertEqual(buf.visible(8,10),[])
        buf.clear();self.assertEqual(len(buf),0)
        buf.append(dict(time=9,sample_ms=900,value=9))
        self.assertEqual(buf[-1]['time'],9)
    def test_clear_curve_keeps_selection_and_resumes(self):
        self.window.on_snapshot(copy.deepcopy(self.snap))
        trend=self.window.trend
        self.assertEqual(trend.samples.maxlen,864000)
        trend.hidden.add('PB11');trend.offset=100;trend.zoom_y(.5)
        self.window.clear_trend()
        self.assertEqual(len(trend.samples),0)
        self.assertEqual(trend.offset,0)
        self.assertFalse(trend.y_ranges)
        self.assertIn('PB11',trend.hidden)
        self.window.on_snapshot(copy.deepcopy(self.snap))
        self.assertEqual(len(trend.samples),1)
    def test_vertical_zoom_and_reset(self):
        trend=self.window.trend
        trend.last_range=(20,40)
        trend.zoom_y(.5)
        self.assertEqual(trend.y_ranges[trend.group],(25,35))
        trend.zoom_y(2)
        self.assertEqual(trend.y_ranges[trend.group],(20,40))
        trend.reset_view();self.assertFalse(trend.y_ranges)

if __name__=='__main__':unittest.main(verbosity=2)

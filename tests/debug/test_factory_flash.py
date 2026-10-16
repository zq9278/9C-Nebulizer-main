"""Factory programming validation and probe mocks; never opens real hardware."""
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import MagicMock,patch
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tools/nebulizer_debug')]
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from factory import FactoryImage,program_factory,verify_image,check_device,FactoryDialog

class FactoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.image=FactoryImage(ROOT/'build/upgrade-release/nebulizer_factory.hex')
    def test_image_guards(self):
        from intelhex import IntelHex
        with self.assertRaises(ValueError):FactoryImage(ROOT/'build/upgrade-release/nebulizer.hex')
        for address in (0x08000004,0x08004004,0x08004100,0x0801F820):
            h=IntelHex(str(self.image.path));h[address]^=1
            with tempfile.TemporaryDirectory() as folder:
                p=Path(folder)/'bad.hex';h.write_hex_file(str(p))
                # Changing Boot's reset address within its legal region cannot be
                # authenticated without a known Boot hash. Corrupt it out of range.
                if address==0x08000004:
                    h[address+3]=0;h.write_hex_file(str(p))
                with self.assertRaises(ValueError):FactoryImage(p)
    def test_verify_all_flash_and_short_read(self):
        progress=[]
        verify_image(lambda a,n:self.image.data[a-0x08000000:a-0x08000000+n],self.image,progress.append)
        self.assertEqual(progress[-1],100)
        with self.assertRaises(IOError):verify_image(lambda a,n:b'',self.image)
    def test_device_guards(self):
        read=lambda a,n:b'\x80\x00'
        check_device(read,lambda a:0x460 if a==0x40015800 else 0xAA)
        with self.assertRaises(ValueError):check_device(read,lambda a:0)
        with self.assertRaises(ValueError):check_device(lambda a,n:b'\x40\x00',lambda a:0x460)
    def test_jlink_verify_before_reset_and_error_no_reset(self):
        link=MagicMock()
        def read(a,n):
            if a==0x1FFF75E0:return [128,0]
            return self.image.data[a-0x08000000:a-0x08000000+n]
        link.memory_read8.side_effect=read
        link.memory_read32.side_effect=lambda a,n:[0x460 if a==0x40015800 else 0xAA if a==0x40022020 else 0]
        link.memory_write32.return_value=4;link.exec_command.return_value=0
        options=dict(backend='jlink',serial='123',speed=1000,dll='')
        with patch('pylink.JLink',return_value=link):
            program_factory(options,self.image,lambda s:None,lambda n:None,True)
        link.flash.assert_called_once_with(self.image.data,0x08000000)
        link.reset.assert_called_once_with(halt=False);link.close.assert_called_once()
        link.reset.reset_mock();link.memory_read8.side_effect=lambda a,n:[128,0] if a==0x1FFF75E0 else [0]*n
        with patch('pylink.JLink',return_value=link),self.assertRaises(IOError):
            program_factory(options,self.image,lambda s:None,lambda n:None,True)
        link.reset.assert_not_called()
    def test_daplink_explicit_target_no_unlock_and_full_readback(self):
        session=MagicMock();target=session.target;probe=MagicMock(unique_id='DAP_1')
        target.read32.side_effect=lambda a:0x460 if a==0x40015800 else 0xAA if a==0x40022020 else 0
        target.read_memory_block8.side_effect=lambda a,n: [128,0] if a==0x1FFF75E0 else self.image.data[a-0x08000000:a-0x08000000+n]
        options=dict(backend='daplink',serial='DAP_1',speed=1000)
        with patch('pyocd.probe.cmsis_dap_probe.CMSISDAPProbe.get_all_connected_probes',return_value=[probe]), \
             patch('pyocd.core.session.Session.__init__',return_value=None) as init, \
             patch('pyocd.core.session.Session.open'), \
             patch('pyocd.core.session.Session.target',new_callable=lambda:property(lambda self:target)), \
             patch('pyocd.core.session.Session.close') as close, \
             patch('pyocd.flash.loader.FlashLoader') as loader:
            program_factory(options,self.image,lambda s:None,lambda n:None,True)
            settings=init.call_args.kwargs['options']
            self.assertFalse(settings['auto_unlock']);self.assertFalse(settings['resume_on_disconnect'])
            self.assertEqual(settings['target_override'],'stm32g070cbtx')
            loader.return_value.add_data.assert_called_once_with(0x08000000,self.image.data)
            self.assertEqual(loader.call_args.kwargs['chip_erase'],'sector')
            target.reset.assert_called_once();close.assert_called_once()
    def test_gui_requires_confirmation(self):
        from PyQt6.QtWidgets import QApplication
        app=QApplication.instance() or QApplication([])
        dialog=FactoryDialog(None,dict(backend='daplink',serial='SIM',speed=1000))
        with patch('factory.QMessageBox.warning') as warning,patch('factory.FactoryWorker') as worker:
            dialog.begin();warning.assert_called_once();worker.assert_not_called()
        self.assertFalse(dialog.reset.isChecked())
        dialog.close();app.processEvents()

if __name__=='__main__':unittest.main(verbosity=2)

"""Headless factory image validation and SWD programming adapters."""
from pathlib import Path
import hashlib
import struct
import sys
from fw_update.protocol import APP_BASE, META_BASE, RAM_END, validate_app, metadata_bytes

BASE, SIZE = 0x08000000, 128*1024

class FactoryImage:
    def __init__(self,path):
        from intelhex import IntelHex
        self.path=Path(path)
        hexfile=IntelHex(str(self.path))
        if hexfile.addresses()!=list(range(BASE,BASE+SIZE)):
            raise ValueError('需要完整 128 KiB 工厂 HEX（Boot + App + 有效记录），不能选择独立 App HEX')
        self.data=bytes(hexfile.tobinarray(start=BASE,size=SIZE))
        sp,pc=struct.unpack_from('<II',self.data)
        if sp!=RAM_END or not pc&1 or not BASE<=(pc&~1)<APP_BASE:
            raise ValueError('Boot 栈顶或 Reset 向量错误')
        at=META_BASE-BASE
        length=struct.unpack_from('<I',self.data,at+8)[0]
        if not 192<=length<=META_BASE-APP_BASE:raise ValueError('App 长度错误')
        app=self.data[APP_BASE-BASE:APP_BASE-BASE+length]
        validate_app(app)
        if self.data[at:at+40]!=metadata_bytes(app):raise ValueError('App CRC32 或工厂有效记录错误')
        if any(x!=255 for x in self.data[APP_BASE-BASE+length:at]+self.data[at+40:]):
            raise ValueError('工厂镜像保留区含非 FF 数据')
        self.app_length=length
        self.sha256=hashlib.sha256(self.data).hexdigest()

def verify_image(read,image,progress=lambda n:None):
    for at in range(0,SIZE,1024):
        expected=image.data[at:at+1024]
        actual=bytes(read(BASE+at,len(expected)))
        if actual!=expected:
            raise IOError(f'工厂镜像读回校验失败：0x{BASE+at:08X}；未复位运行')
        progress(70+(at+len(expected))*30//SIZE)

def check_device(read,read32):
    if read32(0x40015800)&0xFFF!=0x460:
        raise ValueError('芯片 DEV_ID 不属于 STM32G07x/G08x 系列，拒绝烧录')
    size=bytes(read(0x1FFF75E0,2))
    if size!=b'\x80\x00':raise ValueError('实际芯片 Flash 容量不是 128 KiB，拒绝烧录')
    option=read32(0x40022020)
    if option&255!=0xAA:
        raise ValueError('芯片读保护状态不允许烧录；本模块不自动解锁或修改 Option Bytes')

def safe_outputs(read32,write32):
    # Same pin levels as board_emergency_off(), with the core halted.
    write32(0x40021034,read32(0x40021034)|3)  # RCC IOPENR
    write32(0x50000418,1<<(1+16))           # PB1 triac off
    write32(0x50000400,(read32(0x50000400)&~(3<<2))|(1<<2))
    write32(0x50000000,read32(0x50000000)&~(3<<14))  # PA7 release
    write32(0x5000000C,read32(0x5000000C)&~(3<<14))
    write32(0x50000018,1<<6)                # PA6 inverted fan off
    write32(0x50000000,(read32(0x50000000)&~(3<<12))|(1<<12))

def program_factory(options,image,log,progress,run=True):
    if options['backend']=='daplink':
        from pyocd.core.session import Session
        from pyocd.probe.cmsis_dap_probe import CMSISDAPProbe
        from pyocd.flash.loader import FlashLoader
        class FactorySession(Session):
            def _load_user_script(self):pass
        probes=[p for p in CMSISDAPProbe.get_all_connected_probes() if p.unique_id==options['serial']]
        if len(probes)!=1:raise RuntimeError('找不到唯一的 DAPLink 探针，请刷新探针')
        pack_options={}
        if getattr(sys,'frozen',False):
            packs=list((Path(sys._MEIPASS)/'flash-packs').glob('*.pack'))
            if packs:pack_options['pack']=[str(p) for p in packs]
        session=FactorySession(probes[0],auto_open=False,options={
            'target_override':'stm32g070cbtx','connect_mode':'halt','no_config':True,
            'auto_unlock':False,'resume_on_disconnect':False,'frequency':options['speed']*1000,
            'dap_protocol':'swd','cache.enable_memory':False,'cache.read_code_from_elf':False,
            'pack.debug_sequences.enable':False,**pack_options})
        try:
            session.open();target=session.target
            target.halt();check_device(target.read_memory_block8,target.read32)
            safe_outputs(target.read32,target.write32);target.flush()
            log('DAPLink：擦写工厂镜像（含 Boot）')
            loader=FlashLoader(session,progress=lambda p:progress(int(p*70)),
                chip_erase='sector',smart_flash=False,trust_crc=False,no_reset=True)
            loader.add_data(BASE,image.data);loader.commit()
            target.halt()
            verify_image(target.read_memory_block8,image,progress)
            if run:target.reset()
        except Exception:
            try:session.target.halt()
            except Exception:pass
            raise
        finally:session.close()
    elif options['backend']=='jlink':
        import pylink
        library=pylink.library.Library(dllpath=options['dll']) if options.get('dll') else None
        link=pylink.JLink(lib=library)
        try:
            link.open(serial_no=int(options['serial']))
            for cmd in ('SetAllowFlashCache = 0','SetEnableMemCache = 0'):
                if link.exec_command(cmd)<0:raise RuntimeError('J-Link 不支持关闭缓存：'+cmd)
            link.set_tif(pylink.enums.JLinkInterfaces.SWD)
            link.connect('STM32G070CB',speed=options['speed']);link.halt()
            def read32(a):return link.memory_read32(a,1)[0]
            def write32(a,v):
                if link.memory_write32(a,[v])!=4:raise IOError('安全输出寄存器写入失败')
            check_device(link.memory_read8,read32)
            safe_outputs(read32,write32)
            log('J-Link：擦写工厂镜像（含 Boot）')
            link.flash(image.data,BASE);progress(70)
            link.halt();verify_image(link.memory_read8,image,progress)
            if run:link.reset(halt=False)
        except Exception:
            try:link.halt()
            except Exception:pass
            raise
        finally:link.close()
    else:raise ValueError('未知探针类型')
    log('完整 128 KiB 读回校验通过；'+('已复位运行' if run else 'CPU 保持暂停，需手动复位'))


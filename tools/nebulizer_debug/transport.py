"""ELF checked, background-only SWD access. No flash/reset/halt operations."""
from pathlib import Path
import hashlib
import secrets
import struct
import time

from protocol import MAGIC, VERSION, MAILBOX_SIZE, ACQUIRE, RELEASE, decode_snapshot

class ElfImage:
    def __init__(self, path):
        from elftools.elf.elffile import ELFFile
        self.path = Path(path)
        with self.path.open('rb') as stream:
            elf = ELFFile(stream)
            if elf.elfclass != 32 or not elf.little_endian or elf['e_machine'] != 'EM_ARM':
                raise ValueError('需要 STM32 的 32 位小端 ARM ELF')
            symtab = elf.get_section_by_name('.symtab')
            symbols = symtab.get_symbol_by_name('g_nebulizer_debug') if symtab else None
            if not symbols or len(symbols) != 1:
                raise ValueError('ELF 没有 g_nebulizer_debug；请使用包含调试接口的新固件')
            symbol = symbols[0]
            self.address = int(symbol['st_value'])
            if int(symbol['st_size']) != MAILBOX_SIZE or self.address % 4 or not (
                    0x20000000 <= self.address <= 0x20009000 - MAILBOX_SIZE):
                raise ValueError('ELF 调试区布局或地址不兼容')
            # Flash loaders may leave inter-section alignment padding erased.
            # Check allocated file-backed sections using their physical load addresses.
            self.segments = []
            for segment in elf.iter_segments():
                if segment['p_type'] != 'PT_LOAD' or not segment['p_filesz']:
                    continue
                for section in elf.iter_sections():
                    if (not section['sh_flags'] & 2 or not section['sh_size'] or
                            section['sh_type'] == 'SHT_NOBITS' or
                            not segment.section_in_segment(section)):
                        continue
                    address = int(segment['p_paddr'] + section['sh_addr'] - segment['p_vaddr'])
                    if 0x08000000 <= address < 0x08020000:
                        self.segments.append((address, section.data()))
            if not self.segments or any(a+len(b)>0x08020000 for a,b in self.segments):
                raise ValueError('ELF Flash 段无效')
        self.fingerprint = hashlib.sha256(self.path.read_bytes()).hexdigest()

    def verify(self, link):
        # Includes the .data initializer's Flash load image. No cached Flash reads.
        for address, expected in self.segments:
            for start in range(0, len(expected), 1024):
                chunk = expected[start:start+1024]
                actual = bytes(link.memory_read8(address+start, len(chunk)))
                if len(actual) != len(chunk):
                    raise IOError(f'Flash short read at 0x{address+start:08X}: '
                                  f'expected {len(chunk)}, got {len(actual)}; writes blocked')
                if actual != chunk:
                    offset = next(i for i, (a, b) in enumerate(zip(actual, chunk)) if a != b)
                    raise ValueError(f'ELF / Flash mismatch at 0x{address+start+offset:08X}: '
                                     f'ELF={chunk[offset]:02X}, Flash={actual[offset]:02X}; '
                                     f'writes blocked. Select the ELF used for flashing: {self.path}')


def open_jlink(serial, device, speed, dll=''):
    import pylink
    library = pylink.library.Library(dllpath=dll) if dll else None
    link = pylink.JLink(lib=library)
    try:
        link.open(serial_no=int(serial))
        # Abort on unsupported commands; never silently fall back to stop mode.
        for command in ('SetAllowStopMode = 0', 'SetAllowFlashCache = 0', 'SetEnableMemCache = 0'):
            if link.exec_command(command) < 0:
                raise RuntimeError('J-Link 不支持后台访问设置：'+command)
        link.set_tif(pylink.enums.JLinkInterfaces.SWD)
        link.connect(device, speed=int(speed))
        if link.halted():
            raise RuntimeError('CPU 已暂停，请在调试器中确认运行状态后重新连接')
        return link
    except Exception:
        link.close()
        raise

def list_probes(dll=''):
    import pylink
    library = pylink.library.Library(dllpath=dll) if dll else None
    link = pylink.JLink(lib=library)
    try:
        return [str(p.SerialNumber) for p in link.connected_emulators()]
    finally:
        link.close()

def list_daplink_probes():
    from pyocd.probe.cmsis_dap_probe import CMSISDAPProbe
    return [p.unique_id for p in CMSISDAPProbe.get_all_connected_probes()]


class DapLink:
    """Adapt uncached pyOCD target accesses to the mailbox transport contract."""
    def __init__(self, session):
        self.session = session
        self.target = session.target

    def halted(self):
        from pyocd.core.target import Target
        # WFI sleep is normal in an RTOS idle task; reset/lockup are not.
        return self.target.get_state() not in (Target.State.RUNNING, Target.State.SLEEPING)

    def memory_read8(self, address, count):
        return self.target.read_memory_block8(address, count)

    def memory_write8(self, address, data):
        self.target.write_memory_block8(address, data)
        self.target.flush()
        return len(data)

    def memory_write32(self, address, data):
        for index, value in enumerate(data):
            self.target.write32(address + index * 4, value)
        self.target.flush()
        return len(data) * 4

    def close(self):
        self.session.close()


def open_daplink(serial, speed):
    from pyocd.core.session import Session as PyOcdSession
    from pyocd.probe.cmsis_dap_probe import CMSISDAPProbe

    class MailboxSession(PyOcdSession):
        def _load_user_script(self):
            # Project-local pyocd scripts must not override the attach contract.
            pass

    probes = [p for p in CMSISDAPProbe.get_all_connected_probes()
              if p.unique_id == serial]
    if len(probes) != 1:
        raise RuntimeError('未找到唯一的 DAPLink / CMSIS-DAP 探针，请刷新探针')
    session = MailboxSession(probes[0], auto_open=False, options={
        'no_config': True, 'connect_mode': 'attach', 'auto_unlock': False,
        'resume_on_disconnect': False, 'frequency': int(speed) * 1000,
        'dap_protocol': 'swd', 'target_override': 'cortex_m',
        'cache.enable_memory': False, 'cache.enable_register': False,
        'cache.read_code_from_elf': False, 'pack.debug_sequences.enable': False,
    })
    try:
        # Generic Cortex-M avoids vendor init hooks; only memory access is needed.
        session.open()
        link = DapLink(session)
        if link.halted():
            raise RuntimeError('CPU 未处于运行状态；请确认设备运行后重新连接')
        return link
    except Exception:
        session.close()
        raise


def open_probe(options):
    backend = options.get('backend', 'jlink')
    if backend == 'daplink':
        return open_daplink(options['serial'], options['speed'])
    if backend == 'jlink':
        return open_jlink(options['serial'], options['device'], options['speed'], options['dll'])
    raise ValueError('未知探针类型：' + backend)


class Session:
    def __init__(self, link, image, clock=time.monotonic):
        self.link, self.image, self.clock = link, image, clock
        self.address = image.address
        self.session = secrets.randbelow(0xFFFFFFFE)+1
        self.pending = None
        self.owned = False
        self.counter = 0
        self.last_heartbeat = 0
        self.last_progress = clock()
        self.last_snapshot_seq = None
        self.last_uptime = None
        self.event_cursor = None
        self.snapshot = None
        image.verify(link)
        self.read_snapshot()
        owner = self.snapshot['owner']
        if owner:
            # Observe only; do not touch another session's heartbeat input.
            self.owned = False

    def read(self, offset, count):
        data = bytes(self.link.memory_read8(self.address+offset, count))
        if len(data) != count: raise IOError('探针返回短数据')
        return data

    def word(self, offset): return struct.unpack('<I', self.read(offset,4))[0]

    def write(self, offset, data):
        if self.link.memory_write8(self.address+offset, list(data)) != len(data):
            raise IOError('探针写入不完整；命令状态未知')

    def write_word(self, offset, value):
        # PyLink forwards JLINKARM_WriteMemEx byte count, even for 32-bit access.
        if self.link.memory_write32(self.address+offset, [value]) != 4:
            raise IOError('探针提交不完整；命令状态未知')

    def heartbeat(self, force=False):
        if not self.owned and not force: return
        if force or self.clock()-self.last_heartbeat >= .5:
            self.counter = (self.counter+1) & 0xFFFFFFFF or 1
            self.write_word(64, self.session)
            self.write_word(68, self.counter)
            self.last_heartbeat = self.clock()

    def read_snapshot(self):
        if self.link.halted(): raise RuntimeError('CPU 已暂停；调试会话终止，未自动恢复 CPU')
        for _ in range(5):
            header = struct.unpack('<8I', self.read(0,32))
            if header[:3] != (MAGIC,VERSION,MAILBOX_SIZE):
                raise RuntimeError('调试接口未就绪或固件已重启；请重新连接并校验 ELF')
            seq = header[4]
            if not seq or seq & 1: continue
            size = self.word(88)
            if size > 320: raise ValueError('非法快照长度')
            data = self.read(96,size)
            if self.word(16) != seq: continue
            if self.last_snapshot_seq is not None:
                delta = (seq-self.last_snapshot_seq) & 0xFFFFFFFF
                if delta > 0x80000000:
                    raise RuntimeError('检测到目标重启，旧会话已失效')
                if self.last_uptime is not None and ((header[5]-self.last_uptime)&0xFFFFFFFF) > 0x80000000:
                    raise RuntimeError('检测到目标时钟重置，请重新连接')
            if seq != self.last_snapshot_seq:
                self.last_progress = self.clock()
            elif self.clock()-self.last_progress > 2:
                raise RuntimeError('固件快照超过两秒未更新；数据已失效')
            result = decode_snapshot(data)
            result.update(uptime_ms=header[5], owner=header[6], owner_alive=bool(header[7]), sequence=seq)
            self.last_snapshot_seq, self.last_uptime = seq, header[5]
            self.snapshot = result
            return result
        raise RuntimeError('无法读取一致快照')

    def submit(self, cmd, payload=b''):
        if self.pending: raise RuntimeError('仍有未完成命令')
        self.read_snapshot()
        if len(payload)>16: raise ValueError('命令参数过长')
        if cmd == ACQUIRE:
            if self.snapshot['owner'] not in (0,self.session):
                raise RuntimeError('另一调试会话持有控制权')
            self.heartbeat(force=True)
        elif cmd == RELEASE:
            if not self.owned: raise RuntimeError('当前没有调试控制权')
        elif not self.owned:
            raise RuntimeError('当前处于观察模式，请先接管控制')
        seq = (self.word(32)+1)&0xFFFFFFFF or 1
        # Invalidate commit first, then publish payload, then aligned sequence.
        self.write_word(32,0)
        self.write(36, struct.pack('<III',self.session,cmd,len(payload))+payload.ljust(16,b'\0'))
        self.pending = (seq,cmd,self.clock())
        self.write_word(32,seq)

    def response(self):
        if not self.pending: return None
        seq, cmd, started = self.pending
        first = self.word(72)
        if first == seq:
            raw = self.read(72,16)
            if self.word(72) == first:
                got_seq, session, got_cmd, error = struct.unpack('<4I',raw)
                if (got_seq,session,got_cmd) == (seq,self.session,cmd):
                    self.pending = None
                    if error == 0 and cmd == ACQUIRE: self.owned = True
                    if error == 0 and cmd == RELEASE: self.owned = False
                    return cmd,error
        if self.clock()-started>5:
            raise TimeoutError('命令应答超时，执行结果未知；停止后续命令，请重新连接检查状态')
        return None

    def events(self):
        head = self.word(92)
        if self.event_cursor is None:
            self.event_cursor = max(0,head-16)
        delta = (head-self.event_cursor)&0xFFFFFFFF
        if delta>0x80000000: raise RuntimeError('事件序号已重置')
        lost = max(0,delta-16)
        cursor = (head-min(delta,16))&0xFFFFFFFF
        events = []
        while cursor != head:
            seq = (cursor+1)&0xFFFFFFFF or 1
            offset = 416+((seq-1)%16)*32
            raw = self.read(offset,32)
            if self.word(offset) != seq or struct.unpack_from('<I',raw)[0] != seq:
                break # overwritten or being committed; retry next poll
            vals = struct.unpack('<6I2i',raw)
            events.append(dict(zip(('sequence','uptime_ms','type','code','state','remaining_sec','outlet','kettle'),vals)))
            cursor = seq
        self.event_cursor = cursor
        return events,lost

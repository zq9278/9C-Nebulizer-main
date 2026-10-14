"""Single owner of probe calls; GUI never performs USB I/O."""
import queue
import threading
import time
from PyQt6.QtCore import QThread, pyqtSignal
from protocol import ACQUIRE, RELEASE, ERRORS, COMMANDS, STATES
from transport import ElfImage, Session, open_probe

class DeviceWorker(QThread):
    snapshot = pyqtSignal(object)
    event = pyqtSignal(object)
    log = pyqtSignal(str)
    connected = pyqtSignal(str)
    completed = pyqtSignal(str, bool, object)

    def __init__(self, options):
        super().__init__()
        self.options = options
        self.commands = queue.Queue(maxsize=16)
        self.stop_requested = threading.Event()

    def batch(self, token, commands):
        try: self.commands.put_nowait((token,list(commands)))
        except queue.Full: self.completed.emit(token,False,'命令队列已满')

    def run(self):
        link = None
        session = None
        active = None
        remaining = []
        refresh_after = None
        try:
            image = ElfImage(self.options['elf'])
            self.log.emit('正在连接 '+self.options.get('backend','jlink')+' 并逐段校验设备 Flash 与 ELF…')
            link = open_probe(self.options)
            session = Session(link,image)
            self.connected.emit(image.fingerprint)
            while not self.stop_requested.is_set():
                session.heartbeat()
                snap = session.read_snapshot()
                snap['our_session'] = session.session
                snap['control'] = session.owned and snap['owner'] == session.session and snap['owner_alive']
                self.snapshot.emit(snap)
                events,lost = session.events()
                if lost: self.log.emit(f'事件缓冲覆盖了 {lost} 条历史记录')
                for event in events: self.event.emit(event)
                if session.owned and snap['owner'] != session.session and refresh_after is None:
                    raise RuntimeError('调试控制权已失效，请重新连接')
                result = session.response()
                if result:
                    cmd,error = result
                    self.log.emit(f'{COMMANDS.get(cmd,hex(cmd))}：'+('成功' if not error else f'失败 0x{error:02X} '+ERRORS.get(error,'参数无效或状态不允许')))
                    if error:
                        self.completed.emit(active,False,f'命令 0x{cmd:02X} 被拒绝：0x{error:02X}')
                        active,remaining,refresh_after = None,[],None
                    elif not remaining:
                        # A newer firmware publication must follow the ACK before readback.
                        refresh_after = snap['sequence']
                if active and refresh_after is not None and snap['sequence'] != refresh_after:
                    self.completed.emit(active,True,snap)
                    active,refresh_after = None,None
                if active is None:
                    try: active,remaining = self.commands.get_nowait()
                    except queue.Empty: pass
                if active and remaining and not session.pending:
                    cmd,payload = remaining.pop(0)
                    self.log.emit(f'提交 {COMMANDS.get(cmd,hex(cmd))}')
                    session.submit(cmd,payload)
                self.stop_requested.wait(.1)
        except Exception as exc:
            self.log.emit(str(exc))
            if active: self.completed.emit(active,False,str(exc))
        finally:
            # Never terminate the thread inside a DLL call. Lease expiry is the
            # fallback if cable loss or an unknown pending result blocks release.
            if session and session.owned:
                try:
                    deadline = time.monotonic()+5.5
                    while session.pending and time.monotonic()<deadline:
                        session.read_snapshot()
                        session.heartbeat()
                        session.response()
                        time.sleep(.05)
                    if not session.pending:
                        session.submit(RELEASE)
                        while session.pending and time.monotonic()<deadline:
                            session.read_snapshot()
                            session.heartbeat()
                            session.response()
                            time.sleep(.05)
                        if session.pending: raise TimeoutError('释放应答超时')
                        self.log.emit('已释放调试控制，固件停止了调试输出')
                except Exception as exc:
                    self.log.emit(f'无法确认释放：{exc}；固件调试租约将在最后心跳后 3 秒失效')
            if link:
                try: link.close()
                except Exception as exc: self.log.emit(f'关闭探针失败：{exc}')

class ProbeWorker(QThread):
    found = pyqtSignal(object)
    failed = pyqtSignal(str)
    def __init__(self,dll,backend='jlink'):
        super().__init__()
        self.dll=dll
        self.backend=backend
    def run(self):
        from transport import list_probes, list_daplink_probes
        try: self.found.emit(list_daplink_probes() if self.backend == 'daplink' else list_probes(self.dll))
        except Exception as exc: self.failed.emit(str(exc))

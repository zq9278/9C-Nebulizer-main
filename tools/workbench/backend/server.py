"""JSON-lines RPC hardware service. No Qt; stdout is exclusively protocol data."""
import sys
from pathlib import Path
import json
import threading
import time
import struct
import contextlib
import secrets

if getattr(sys,'frozen',False):
    TOOLS=Path(sys._MEIPASS)/'tools'
else:TOOLS=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(TOOLS/'nebulizer_debug'),str(TOOLS)]
from transport import ElfImage,Session,open_probe,list_probes,list_daplink_probes
from protocol import decode_snapshot,validate_params,COMMANDS,ACQUIRE,RELEASE
from fw_update.protocol import Decoder,frame,Firmware
from fw_update.client import Updater
from device_io.factory import FactoryImage,program_factory

OUT=sys.stdout
write_lock=threading.Lock()
def send(value):
    with write_lock:OUT.write(json.dumps(value,ensure_ascii=False,allow_nan=False)+'\n');OUT.flush()
def event(name,data):send({'event':name,'data':data})

class SerialSession:
    def __init__(self,options):
        import serial
        self.link=serial.Serial(options['port'],115200,timeout=.03,write_timeout=2)
        self.decoder=Decoder();self.frames={};self.owned=False;self.sequence=secrets.randbelow(65534)+1
        self.last_poll=0.;self.last_heartbeat=0.;self.last_progress=time.monotonic();self.last_sample=None
        try:
            for cmd in (0x24,0x27,0x28,0x33,0x31,0x2A,0x30):self.submit(cmd,b'')
        except Exception:self.link.close();raise
    def read_frames(self):
        packets=self.decoder.feed(self.link.read(max(1,min(512,self.link.in_waiting))))
        for seq,kind,payload in packets:
            if kind in (4,6,7,8,9,10,11):self.frames[kind]=payload
            elif kind==5:event('device-event',{'type':1,'code':payload[0] if payload else 0,'raw':payload.hex()})
        return packets
    def submit(self,cmd,payload):
        self.sequence=self.sequence%65535+1
        packet=frame(self.sequence,1,bytes([cmd])+payload)
        if self.link.write(packet)!=len(packet):raise IOError('串口短写，命令结果未知')
        self.link.flush();deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            for seq,kind,data in self.read_frames():
                if seq==self.sequence and kind in (2,3) and len(data)>=3 and data[0]==cmd:
                    if kind==3 or not data[1] or data[2]:raise RuntimeError(f'命令 0x{cmd:02X} 被拒绝：0x{data[2]:02X}')
                    return
        raise TimeoutError('串口命令应答超时，未自动重发；执行结果未知，请重新连接检查状态')
    def read_snapshot(self):
        now=time.monotonic()
        if self.owned and now-self.last_heartbeat>=.5:self.submit(0x25,b'');self.last_heartbeat=now
        if now-self.last_poll>=.5:
            for cmd in (0x2A,0x30):self.submit(cmd,b'')
            self.last_poll=now
        self.read_frames()
        snap=decode_snapshot(b''.join(frame(1,k,self.frames[k]) for k in (4,6,7,8,9,10,11)))
        sample=snap['runtime']['sample_ms']
        if sample!=self.last_sample:self.last_sample=sample;self.last_progress=now
        if now-self.last_progress>2:raise RuntimeError('串口快照超过两秒未更新')
        snap.update(uptime_ms=sample,sequence=sample,owner=1 if self.owned else 0,owner_alive=self.owned,control=self.owned)
        return snap
    def close(self):
        try:
            if self.owned:
                for cmd,payload in ((0x2C,b''),(0x15,b'\0'),(0x21,b'')):self.submit(cmd,payload)
        finally:self.link.close()

class Service:
    def __init__(self):
        self.lock=threading.RLock();self.session=None;self.kind=None;self.busy=False
        self.cancel=threading.Event();self.quit=threading.Event();self.warm_since=None;self.warm_sent=False
        self.thread=threading.Thread(target=self.poll,daemon=True);self.thread.start()
    def log(self,message):event('log',{'message':str(message),'level':'info'})
    def snapshot(self):
        s=self.session.read_snapshot()
        if self.kind=='swd':s.update(control=self.session.owned and s['owner']==self.session.session and s['owner_alive'])
        if self.kind=='swd' and self.session.owned and s['owner']!=self.session.session:
            raise RuntimeError('调试控制权已失效，请重新连接')
        event('snapshot',s)
        if self.kind=='swd':
            events,lost=self.session.events()
            for e in events:event('device-event',e)
            if lost:self.log(f'事件缓冲覆盖 {lost} 条')
        # Session timer lives beside the transport, independent of rendered pages.
        active=s.get('control') and s['runtime']['state']==10
        if not active:self.warm_since=None;self.warm_sent=False
        elif self.warm_since is None:self.warm_since=time.monotonic()
        remaining=max(0,7200-int(time.monotonic()-self.warm_since)) if active else None
        event('warm-timer',remaining)
        if remaining==0 and not self.warm_sent:
            self.warm_sent=True;self.command(0x15,b'\0');self.log('PC 保温两小时到期，已关闭保温')
        return s
    def poll(self):
        while not self.quit.wait(.1):
            with self.lock:
                if not self.session:continue
                try:self.snapshot()
                except Exception as exc:
                    self.log(str(exc));self.disconnect();event('connection',{'connected':False,'error':str(exc)})
    def disconnect(self):
        with self.lock:
            if self.session:
                s=self.session
                try:
                    if self.kind=='swd' and s.owned:
                        # Unknown pending command results are never replayed.
                        deadline=time.monotonic()+5.5
                        while s.pending and time.monotonic()<deadline:s.heartbeat();s.read_snapshot();s.response();time.sleep(.05)
                        if not s.pending:
                            s.submit(RELEASE)
                            while s.pending and time.monotonic()<deadline:s.heartbeat();s.read_snapshot();s.response();time.sleep(.05)
                    if self.kind=='swd':s.link.close()
                    else:s.close()
                except Exception as exc:
                    self.log(f'断开时无法确认释放：{exc}；调试租约按固件超时处理')
                    try:s.link.close()
                    except Exception:pass
                finally:self.session=None;self.kind=None;self.warm_since=None
            event('connection',{'connected':False})
    def command(self,cmd,payload):
        if cmd not in COMMANDS:raise ValueError('不支持的调试命令')
        if self.kind=='serial':
            if cmd==ACQUIRE:self.session.owned=True;return
            if cmd==RELEASE:self.session.close();self.session=None;event('connection',{'connected':False});return
            if not self.session.owned and cmd not in (0x24,0x27,0x28,0x33,0x31,0x2A,0x30,0x25):raise ValueError('请先接管控制')
            self.session.submit(cmd,payload);return
        s=self.session;s.submit(cmd,payload);deadline=time.monotonic()+5.5
        while time.monotonic()<deadline:
            s.heartbeat();s.read_snapshot();result=s.response()
            if result:
                if result[1]:raise RuntimeError(f'命令 0x{cmd:02X} 被拒绝：0x{result[1]:02X}')
                seq=s.snapshot['sequence']
                while time.monotonic()<deadline:
                    s.heartbeat();fresh=s.read_snapshot()
                    if fresh['sequence']!=seq:return
                    time.sleep(.03)
                break
            time.sleep(.03)
        raise TimeoutError('命令完成/新快照超时，停止后续命令；结果未知')
    def dispatch(self,method,p):
        if method=='cancel':self.cancel.set();return True
        if method=='shutdown':self.quit.set();self.disconnect();return True
        with self.lock:
            if method=='status':return {'connected':bool(self.session),'busy':self.busy}
            if method=='disconnect':self.disconnect();return True
            if method=='ports':
                import serial.tools.list_ports
                return [x.device for x in serial.tools.list_ports.comports()]
            if method=='probes':
                if self.session or self.busy:raise ValueError('请先断开连接')
                return list_daplink_probes() if p['backend']=='daplink' else list_probes(p.get('dll',''))
            if method=='connect':
                if self.session or self.busy:raise ValueError('已有连接或作业')
                if p.get('mode')=='serial':self.session=SerialSession(p);self.kind='serial'
                else:
                    image=ElfImage(p['elf']);link=open_probe(p)
                    try:self.session=Session(link,image)
                    except Exception:link.close();raise
                    self.kind='swd'
                self.warm_since=None;self.warm_sent=False
                s=self.snapshot();event('connection',{'connected':True,'mode':self.kind});return s
            if method=='commands':
                if not self.session:raise ValueError('设备未连接')
                commands=p.get('commands',[])
                if not 1<=len(commands)<=32:raise ValueError('命令批次大小无效')
                try:
                    for item in commands:
                        cmd=item['command'];payload=bytes(item.get('payload',[]))
                        if type(cmd)!=int or len(payload)>16:raise ValueError('命令格式错误')
                        self.log(f'提交 {COMMANDS.get(cmd,hex(cmd))}');self.command(cmd,payload)
                        if not self.session:return None
                    if self.kind=='serial':
                        for cmd in (0x24,0x27,0x28,0x33,0x31,0x2A,0x30):self.session.submit(cmd,b'')
                except (TimeoutError,IOError):
                    self.disconnect();raise
                return self.snapshot()
            if method=='validate-params':return validate_params(p['params'])
            if method=='inspect-factory':
                i=FactoryImage(p['path']);return {'length':len(i.data),'appLength':i.app_length,'sha256':i.sha256}
            if method=='inspect-update':
                i=Firmware.load(Path(p['path']));return {'length':len(i.data),'crc':f'{i.crc:08X}'}
            if method not in ('factory','update'):raise ValueError('未知 RPC 方法')
            if self.session or self.busy:raise ValueError('请断开所有连接后执行烧录或升级')
            if p.get('confirmed') is not True:raise ValueError('缺少实际输出停止及安装确认')
            # Validate before claiming the hardware job or touching the probe/port.
            image=FactoryImage(p['path']) if method=='factory' else Firmware.load(Path(p['path']))
            self.busy=True;self.cancel.clear()
        try:
            event('job',{'running':True,'type':method})
            progress=lambda n:event('progress',{'type':method,'value':n})
            if method=='factory':
                program_factory(p,image,self.log,progress,bool(p.get('run')))
                return {'message':'工厂烧录及完整读回校验通过','elf':self.matching_elf(image)}
            import serial
            with serial.Serial(p['port'],115200,timeout=.05,write_timeout=2) as port:
                return {'message':Updater(port,self.log,progress,self.cancel).update(image,run=bool(p.get('run',True)))}
        finally:
            with self.lock:self.busy=False
            event('job',{'running':False,'type':method})
    @staticmethod
    def matching_elf(image):
        path=image.path.with_name('nebulizer.elf')
        try:
            elf=ElfImage(path)
            if all(image.data[a-0x08000000:a-0x08000000+len(b)]==b for a,b in elf.segments):return str(path)
        except Exception:pass
        return None

def main():
    # Vendor libraries may print diagnostics; preserve the JSON stream.
    with contextlib.redirect_stdout(sys.stderr):service=Service()
    def run(request):
        try:
            with contextlib.redirect_stdout(sys.stderr):result=service.dispatch(request['method'],request.get('params') or {})
            send({'id':request['id'],'result':result})
        except Exception as exc:send({'id':request.get('id'),'error':str(exc)})
    event('ready',{'protocol':1,'python':sys.version.split()[0]})
    for line in sys.stdin:
        try:
            if len(line)>1048576:raise ValueError('请求太长')
            request=json.loads(line)
            if not isinstance(request,dict) or type(request.get('id'))!=int:raise ValueError('请求格式错误')
            if request.get('method')=='shutdown':run(request);break
            threading.Thread(target=run,args=(request,),daemon=True).start()
        except Exception as exc:send({'id':None,'error':str(exc)})
    service.quit.set();service.disconnect()

if __name__=='__main__':main()

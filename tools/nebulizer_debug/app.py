"""Nebulizer SWD workbench. Launch with python tools/nebulizer_debug/app.py."""
import csv
import json
from pathlib import Path
import struct
import sys
import time

from PyQt6.QtCore import QSettings, QTimer, Qt
from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QFormLayout, QGridLayout, QGroupBox, QLineEdit, QComboBox, QPushButton, QLabel,
    QSpinBox, QDoubleSpinBox, QCheckBox, QPlainTextEdit, QFileDialog,
    QMessageBox, QScrollArea, QListWidget, QSplitter)
from protocol import ACQUIRE, RELEASE, STATES, EVENTS, validate_params
from worker import DeviceWorker, ProbeWorker
from trend import Trend, SERIES
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from fw_update.gui import UpgradeDialog

ROOT=Path(__file__).resolve().parents[2]
FAULTS=dict(enumerate(['正常','初始化失败','ADC 初始化失败','UART 初始化失败','PWM 初始化失败',
    '定时器初始化失败','NTC 断路','NTC 短路','过温','缺水','开盖','雾化板离线','雾化板故障',
    '主机心跳超时','协议错误','存储故障','出口过温','锅体过温']))
PHASES=['待机','PB11 全功率','PB11 出口 PID','PA5 保温']
STYLE='''
QMainWindow, QWidget { background: #152232; color: #e3edf5; font-family: "Microsoft YaHei UI"; font-size: 10pt; }
QGroupBox { border: 1px solid #36495f; border-radius: 6px; margin-top: 14px; padding-top: 12px; }
QGroupBox::title { subcontrol-origin: margin; left: 12px; color: #65d9c7; }
QPushButton { background: #29455d; border: 1px solid #42627a; padding: 7px 13px; border-radius: 4px; }
QPushButton:hover { background: #365c78; }
QPushButton:disabled { color: #738395; background: #1a2a3b; border-color: #273a4f; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QListWidget { background: #0f1b29; border: 1px solid #36495f; padding: 4px; }
QTabBar::tab { padding: 9px 16px; background: #1c3044; }
QTabBar::tab:selected { background: #31516a; color: #65d9c7; }
QLabel#status { color: #65d9c7; font-size: 13pt; padding: 6px; }
'''

class NoWheelSpinBox(QSpinBox):
    def wheelEvent(self,event): event.ignore()

class NoWheelDoubleSpinBox(QDoubleSpinBox):
    def wheelEvent(self,event): event.ignore()

class NoWheelComboBox(QComboBox):
    def wheelEvent(self,event): event.ignore()

class Window(QMainWindow):
    def __init__(self):
        super().__init__()
        self.settings=QSettings('Nebulizer','JLinkWorkbench')
        self.worker=None
        self.probe_worker=None
        self.last=None
        self.initial_read=False
        self.busy=False
        self.closing=False
        self.warm_started=None
        self.warm_sent=False
        self.last_fault=None
        self.csv_file=None
        self.csv_writer=None
        self.last_record_sequence=None
        self.controls=[]
        self.parameter_actions=[]
        self.fields={}
        self.pid_fields={}
        self.dirty=set()
        self.loading=False
        self.setWindowTitle('Nebulizer · SWD 调试工作台')
        self.resize(1600,1000)
        central=QWidget()
        layout=QVBoxLayout(central)
        layout.setContentsMargins(16,12,16,12)
        title=QLabel('NEBULIZER  /  调试工作台')
        title.setStyleSheet('font-size:20pt;font-weight:600;')
        layout.addWidget(title)
        connection=QGroupBox('设备连接 · J-Link / DAPLink / SWD')
        grid=QGridLayout(connection)
        self.backend=NoWheelComboBox()
        self.backend.addItem('J-Link','jlink'); self.backend.addItem('DAPLink / CMSIS-DAP','daplink')
        self.backend.setCurrentIndex(max(0,self.backend.findData(self.settings.value('backend','jlink'))))
        self.serial=NoWheelComboBox(); self.serial.setEditable(True); self.serial.setMinimumWidth(125)
        self.serial.setCurrentText(self.settings.value('serial',''))
        self.device=QLineEdit(self.settings.value('device','STM32G070CB'))
        self.speed=NoWheelSpinBox(); self.speed.setRange(100,4000); self.speed.setValue(int(self.settings.value('speed',1000)))
        self.elf=QLineEdit(self.settings.value('elf',str(ROOT/'build/debug/nebulizer.elf')))
        self.dll=QLineEdit(self.settings.value('dll','')); self.dll.setPlaceholderText('留空自动查找 J-Link DLL，可指定 JLink_x64.dll')
        self.scan_button=QPushButton('刷新探针'); self.scan_button.clicked.connect(self.scan)
        self.connect_button=QPushButton('连接'); self.connect_button.clicked.connect(self.toggle_connection)
        self.upgrade_button=QPushButton('固件升级（串口）'); self.upgrade_button.clicked.connect(self.open_upgrade)
        self.acquire_button=QPushButton('接管控制'); self.acquire_button.clicked.connect(lambda:self.submit('acquire',[(ACQUIRE,b'')]))
        self.release_button=QPushButton('释放控制'); self.release_button.clicked.connect(lambda:self.submit('release',[(RELEASE,b'')]))
        browse=QPushButton('选择 ELF'); browse.clicked.connect(self.choose_elf)
        for col,(label,widget) in enumerate((('探针序列号',self.serial),('目标芯片',self.device),('SWD kHz',self.speed))):
            grid.addWidget(QLabel(label),0,col*2); grid.addWidget(widget,0,col*2+1)
        grid.addWidget(self.scan_button,0,6); grid.addWidget(self.connect_button,0,7)
        grid.addWidget(QLabel('固件 ELF'),1,0); grid.addWidget(self.elf,1,1,1,5); grid.addWidget(browse,1,6)
        grid.addWidget(QLabel('J-Link DLL'),2,0); grid.addWidget(self.dll,2,1,1,5)
        grid.addWidget(self.acquire_button,1,7); grid.addWidget(self.release_button,2,7)
        grid.addWidget(QLabel('探针类型'),3,0); grid.addWidget(self.backend,3,1)
        self.backend.currentIndexChanged.connect(self.backend_changed)
        grid.addWidget(self.upgrade_button,3,6,1,2)
        self.connection=QLabel('未连接 · 连接后默认观察模式'); self.connection.setObjectName('status')
        layout.addWidget(connection); layout.addWidget(self.connection)
        self.panels={}
        self.build_runtime(); self.build_parameters(); self.build_maintenance(); self.build_trends(); self.build_logs()
        self.dashboard=QSplitter(Qt.Orientation.Horizontal)
        left=QWidget(); left_layout=QVBoxLayout(left)
        for name in ('运行监控','参数调试','维护控制'):
            left_layout.addWidget(self.panels[name])
        scroll=QScrollArea(); scroll.setWidgetResizable(True); scroll.setWidget(left)
        right=QSplitter(Qt.Orientation.Vertical)
        right.addWidget(self.panels['曲线与采集']); right.addWidget(self.panels['故障与日志'])
        right.setSizes([650,220])
        self.dashboard.addWidget(scroll); self.dashboard.addWidget(right)
        self.dashboard.setSizes([800,800]); layout.addWidget(self.dashboard,1)
        self.setCentralWidget(central)
        self.timer=QTimer(self); self.timer.setInterval(1000); self.timer.timeout.connect(self.keep_warm_tick); self.timer.start()
        self.refresh_enabled()

    def button(self,label,callback,control=True):
        b=QPushButton(label); b.clicked.connect(callback)
        if control: self.controls.append(b)
        return b

    def build_runtime(self):
        tab=QWidget(); layout=QVBoxLayout(tab)
        self.state=QLabel('等待连接'); self.state.setStyleSheet('font-size:18pt;')
        layout.addWidget(self.state)
        grid=QGridLayout(); self.values={}
        specs=[('remaining','剩余治疗时间'),('fault','故障'),('phase','加热阶段'),('heartbeat','当前控制端心跳'),
               ('pb11','PB11 出口温度'),('pb10','PB10 旧反馈温度'),('pb12','PB12 保护温度'),('pa5','PA5 锅体温度'),
               ('heat','加热输出'),('fan','风机输出'),('rpm','风机转速'),('mist','雾化板'),
               ('cover','磁铁盖'),('water','雾化板缺水'),('liquid','锅体液位（联锁未启用）'),('gx','GX1832'),
               ('adc','ADC 原始值'),('pid','出口 PID 反馈'),('fanpid','风机 PID 反馈'),('maintenance','维护模式')]
        for i,(key,title) in enumerate(specs):
            box=QWidget(); v=QVBoxLayout(box); v.setContentsMargins(3,2,3,2); v.setSpacing(2)
            caption=QLabel(title); caption.setStyleSheet('color:#65d9c7;font-size:9pt;'); v.addWidget(caption)
            label=QLabel('—'); label.setWordWrap(True); v.addWidget(label)
            self.values[key]=label; grid.addWidget(box,i//4,i%4)
        layout.addLayout(grid)
        row=QHBoxLayout()
        for text,cmd in (('开始治疗',0x20),('暂停',0x21),('恢复',0x22),('停止治疗',0x23),('关闭保温',0x15),('清除故障',0x26)):
            row.addWidget(self.button(text,lambda checked=False,c=cmd:self.treatment(c)))
        layout.addLayout(row)
        note=QLabel('热雾开启保温时，“停止治疗”会返回保温；结束加热请使用“关闭保温”。\n支持在预热、治疗、保温和维护中接管，接管时保持当前运行状态。')
        note.setWordWrap(True); layout.addWidget(note)
        self.warm_label=QLabel('保温计时未启动'); layout.addWidget(self.warm_label)
        layout.addStretch(); self.panels['运行监控']=tab

    def spin(self,minimum,maximum,value,decimals=None):
        b=NoWheelDoubleSpinBox() if decimals is not None else NoWheelSpinBox()
        b.setRange(minimum,maximum)
        if decimals is not None: b.setDecimals(decimals); b.setSingleStep(.01)
        b.setValue(value); return b

    def mark(self,group):
        if not self.loading:
            self.dirty.add(group); self.edit_status.setText('待应用：'+', '.join(sorted(self.dirty)))

    def build_parameters(self):
        page=QWidget(); layout=QVBoxLayout(page)
        row=QHBoxLayout()
        for label,fn in (('读取全部参数',self.read_all),('导入 JSON',self.import_params)):
            b=self.button(label,fn,False); self.parameter_actions.append(b); row.addWidget(b)
        row.addWidget(self.button('导出 JSON',self.export_params,False))
        row.addStretch(); layout.addLayout(row)
        self.edit_status=QLabel('尚未读取设备参数'); layout.addWidget(self.edit_status)
        columns=QGridLayout()
        config=QGroupBox('治疗参数'); form=QFormLayout(config)
        self.mode=NoWheelComboBox(); self.mode.addItems(['热雾','冷雾'])
        self.fields['mode']=self.mode
        self.fields['target_temp_deci_c']=self.spin(0,450,420)
        self.fields['duration_min']=self.spin(1,60,10)
        for key in ('air_level','mist_level'):
            combo=NoWheelComboBox(); combo.addItems(['关闭','低档','中档','高档']); combo.setCurrentIndex(2); self.fields[key]=combo
        self.keep_warm=QCheckBox('持续保温（PA5 65°C）'); self.fields['keep_warm_enabled']=self.keep_warm
        for key,label in (('mode','模式'),('target_temp_deci_c','目标温度 / 0.1°C'),('duration_min','时间 / 分钟'),('air_level','风量'),('mist_level','雾量'),('keep_warm_enabled','保温')):
            w=self.fields[key]; form.addRow(label,w)
            signal=w.currentIndexChanged if isinstance(w,QComboBox) else w.toggled if isinstance(w,QCheckBox) else w.valueChanged
            signal.connect(lambda *args:self.mark('config'))
        self.mode.currentIndexChanged.connect(self.mode_changed)
        form.addRow(self.button('应用治疗参数',self.apply_config))
        columns.addWidget(config,0,0)
        for group,title,defaults,limits in (
            ('heat','出口加热 PID（PB11）',(9,0,0,400),(120,10,5,1000)),
            ('fan','风机 PID（PB11 超温增速）',(20,0,0,50),(20,5,5,100)),
            ('preheat','旧预热 PID（仅兼容保存）',(1,0,0,0),(120,10,5,1000))):
            box=QGroupBox(title); f=QFormLayout(box); fields=[]
            for i,(key,value,limit) in enumerate(zip(('Kp','Ki','Kd','积分限幅 ‰'),defaults,limits)):
                w=self.spin(0,limit,value,3 if i<3 else None); fields.append(w); f.addRow(key,w)
                w.valueChanged.connect(lambda *args,g=group:self.mark(g))
            self.pid_fields[group]=fields
            b=self.button('读取',lambda checked=False,g=group:self.read_group(g),False)
            self.parameter_actions.append(b); f.addRow(b)
            f.addRow(self.button('应用',lambda checked=False,g=group:self.apply_pid(g)))
            hint={'heat':'当前固件：PID 周期 2 秒，输出上限 20%。',
                  'fan':'低/中/高档基础 30/35/40%，最多增加 10 个百分点。',
                  'preheat':'此参数不参与加热。PB11 低于 30°C 时使用固定全功率。'}[group]
            lab=QLabel(hint); lab.setWordWrap(True); f.addRow(lab); columns.addWidget(box, *{'heat':(0,1),'fan':(1,0),'preheat':(1,1)}[group])
        layout.addLayout(columns); layout.addStretch()
        self.panels['参数调试']=page

    def build_maintenance(self):
        tab=QWidget(); layout=QVBoxLayout(tab); box=QGroupBox('手动输出'); form=QFormLayout(box)
        self.manual_fan=NoWheelComboBox(); self.manual_mist=NoWheelComboBox()
        for w in (self.manual_fan,self.manual_mist): w.addItems(['关闭','低档','中档','高档'])
        self.manual_heat=self.spin(0,1000,0)
        form.addRow('风机档位',self.manual_fan); form.addRow('雾化档位',self.manual_mist); form.addRow('加热功率 / ‰',self.manual_heat)
        form.addRow(self.button('进入维护',lambda:self.submit('maintenance',[(0x2B,b'')])))
        form.addRow(self.button('应用手动输出',self.apply_manual))
        form.addRow(self.button('退出维护并停输出',lambda:self.submit('maintenance',[(0x2C,b'')])))
        form.addRow(self.button('读取维护状态',self.read_maintenance,False))
        layout.addWidget(box); layout.addWidget(QLabel('允许进入状态：就绪、暂停、治疗完成，且无故障。\n调试控制释放或心跳超时后，固件关闭维护输出。'))
        layout.addStretch(); self.panels['维护控制']=tab

    def build_trends(self):
        tab=QWidget(); layout=QVBoxLayout(tab); row=QGridLayout()
        row.addWidget(QLabel('温度 · 输出 · 误差'),0,0)
        self.trend=Trend()
        actions=(('横向＋',lambda:self.trend.zoom(.75)),('横向－',lambda:self.trend.zoom(4/3)),
                 ('纵向＋',lambda:self.trend.zoom_y(.75)),('纵向－',lambda:self.trend.zoom_y(4/3)),
                 ('恢复自动',self.trend.reset_view),('导出曲线 CSV',self.export_trend),('清除曲线',self.clear_trend))
        for i,(label,fn) in enumerate(actions):
            row.addWidget(self.button(label,fn,False),i//4+1,i%4)
        self.record_button=self.button('开始记录 CSV',self.toggle_record,False); row.addWidget(self.record_button,0,1,1,2)
        layout.addLayout(row); filters=QGridLayout()
        from trend import LABELS
        self.series_checks={}
        for i,name in enumerate(n for items in SERIES.values() for n,c in items):
            enabled=self.settings.value(f'curves/{name}',True,type=bool)
            cb=QCheckBox(LABELS.get(name,name)); cb.setChecked(enabled); filters.addWidget(cb,i//3,i%3)
            self.series_checks[name]=cb
            if not enabled:self.trend.hidden.add(name)
            cb.toggled.connect(lambda on,n=name:self.visible_series(n,on))
        layout.addLayout(filters); layout.addWidget(self.trend,1)
        self.cache_status=QLabel(); layout.addWidget(self.cache_status)
        layout.addWidget(QLabel('横轴：设备运行秒数 · 滚轮横向缩放 · Ctrl+滚轮纵向缩放 · 拖动平移两轴 · 双击恢复跟随。断开连接保留已有曲线。'))
        self.panels['曲线与采集']=tab

    def build_logs(self):
        tab=QWidget(); layout=QVBoxLayout(tab); split=QSplitter()
        self.history=QListWidget(); self.log_view=QPlainTextEdit(); self.log_view.setReadOnly(True); self.log_view.setMaximumBlockCount(10000)
        split.addWidget(self.history); split.addWidget(self.log_view); split.setStretchFactor(1,2); layout.addWidget(split)
        self.history.setToolTip('设备故障与事件'); self.log_view.setPlaceholderText('故障与操作日志')
        layout.addWidget(self.button('导出事件及操作日志',self.export_log,False)); self.panels['故障与日志']=tab

    def log(self,message): self.log_view.appendPlainText(time.strftime('%H:%M:%S')+'  '+message)
    def visible_series(self,name,on):
        self.settings.setValue(f'curves/{name}',bool(on))
        self.settings.sync()
        self.trend.hidden.discard(name) if on else self.trend.hidden.add(name)
        self.trend.update()

    def mode_changed(self):
        cold=self.mode.currentIndex()==1
        self.fields['target_temp_deci_c'].setEnabled(not cold); self.keep_warm.setEnabled(not cold)
        if cold: self.keep_warm.setChecked(False)

    def choose_elf(self):
        path,_=QFileDialog.getOpenFileName(self,'选择与设备固件匹配的 ELF',self.elf.text(),'ELF (*.elf)')
        if path:self.elf.setText(path)

    def backend_changed(self):
        self.serial.clear()
        self.refresh_enabled()

    def open_upgrade(self):
        if self.worker:
            QMessageBox.information(self,'固件升级','请先断开调试连接，释放设备控制权，再进行串口升级。')
            return
        UpgradeDialog(self).exec()

    def scan(self):
        if self.probe_worker and self.probe_worker.isRunning():return
        self.scan_button.setEnabled(False)
        self.probe_worker=ProbeWorker(self.dll.text().strip(),self.backend.currentData())
        self.probe_worker.found.connect(self.probes_found); self.probe_worker.failed.connect(self.log)
        self.probe_worker.finished.connect(self.refresh_enabled); self.probe_worker.start()

    def probes_found(self,probes):
        old=self.serial.currentText(); self.serial.clear(); self.serial.addItems(probes)
        if old in probes:self.serial.setCurrentText(old)
        self.log(f'发现 {len(probes)} 个 {self.backend.currentText()} 探针')

    def toggle_connection(self):
        if self.worker:
            self.worker.stop_requested.set(); self.connect_button.setEnabled(False)
            self.connection.setText('正在释放控制并断开…'); return
        serial=self.serial.currentText().strip()
        if not serial or (self.backend.currentData() == 'jlink' and not serial.isdigit()):
            QMessageBox.warning(self,'连接','请刷新探针或输入对应探针的完整序列号'); return
        options=dict(backend=self.backend.currentData(),serial=self.serial.currentText().strip(),device=self.device.text().strip(),
            speed=self.speed.value(),elf=self.elf.text().strip(),dll=self.dll.text().strip())
        for k,v in options.items():self.settings.setValue(k,v)
        self.last=None; self.initial_read=False; self.last_fault=None
        self.worker=DeviceWorker(options)
        self.worker.snapshot.connect(self.on_snapshot); self.worker.event.connect(self.on_event)
        self.worker.log.connect(self.log); self.worker.completed.connect(self.on_completed)
        self.worker.connected.connect(lambda digest:self.log('ELF 已与设备 Flash 完整核对；文件 SHA256 '+digest))
        self.worker.finished.connect(self.disconnected)
        self.connection.setText('连接和校验中…'); self.connect_button.setText('断开'); self.worker.start(); self.refresh_enabled()

    def disconnected(self):
        self.worker=None; self.last=None; self.busy=False; self.warm_started=None; self.warm_sent=False
        self.initial_read=False; self.connect_button.setText('连接'); self.connection.setText('已断开 · 显示值为最后一次采样')
        self.warm_label.setText('保温 PC 计时已停止'); self.refresh_enabled()
        if self.closing:self.close()

    def refresh_enabled(self):
        connected=self.worker is not None and self.last is not None
        control=connected and self.last.get('control',False)
        for b in self.controls:b.setEnabled(bool(control and not self.busy))
        self.acquire_button.setEnabled(bool(connected and not self.busy and not self.last['owner']))
        self.release_button.setEnabled(bool(control and not self.busy))
        self.scan_button.setEnabled(not self.worker and not (self.probe_worker and self.probe_worker.isRunning()))
        self.connect_button.setEnabled(not (self.worker and self.worker.stop_requested.is_set()) and
            not (self.probe_worker and self.probe_worker.isRunning()))
        for w in (self.serial,self.device,self.speed,self.elf,self.dll,self.backend):w.setEnabled(self.worker is None)
        scanning=bool(self.probe_worker and self.probe_worker.isRunning())
        self.backend.setEnabled(self.worker is None and not scanning)
        for w in (self.device,self.dll):w.setEnabled(self.worker is None and self.backend.currentData() == 'jlink')
        for w in list(self.fields.values())+[w for fields in self.pid_fields.values() for w in fields]+self.parameter_actions:
            w.setEnabled(not self.busy)
        for w in (self.manual_fan,self.manual_mist,self.manual_heat):w.setEnabled(not self.busy)
        if not self.busy:
            cold=self.mode.currentIndex()==1
            self.fields['target_temp_deci_c'].setEnabled(not cold); self.keep_warm.setEnabled(not cold)

    def submit(self,token,commands):
        if not self.worker or self.busy:return
        self.busy=True; self.refresh_enabled(); self.worker.batch(token,commands)

    def config_commands(self):
        p=self.parameters()
        validate_params(p)
        # Disable keep warm before changing to cold, avoiding an intermediate re-entry.
        return [(0x15,b'\0'),(0x10,bytes([p['mode']])),(0x11,struct.pack('<H',p['target_temp_deci_c'])),
                (0x12,struct.pack('<H',p['duration_min'])),(0x13,bytes([p['air_level']])),
                (0x14,bytes([p['mist_level']])),(0x15,bytes([p['keep_warm_enabled']]))]

    def apply_config(self):
        try:self.submit('config',self.config_commands())
        except Exception as exc:QMessageBox.warning(self,'参数',str(exc))

    def treatment(self,cmd):
        commands=[]
        if cmd==0x20 and 'config' in self.dirty:
            try:commands=self.config_commands()
            except Exception as exc:QMessageBox.warning(self,'参数',str(exc)); return
        commands.append((cmd,b'\0' if cmd==0x15 else b''))
        self.submit('start_config' if len(commands)>1 else 'treatment',commands)

    def apply_pid(self,group):
        values=[round(w.value()*1000) if i<3 else w.value() for i,w in enumerate(self.pid_fields[group])]
        self.submit(group,[({'heat':0x29,'fan':0x32,'preheat':0x34}[group],struct.pack('<4i',*values))])

    def apply_manual(self):
        self.submit('maintenance',[(0x2D,bytes([self.manual_fan.currentIndex()])),
            (0x2E,bytes([self.manual_mist.currentIndex()])),(0x2F,struct.pack('<H',self.manual_heat.value()))])

    def on_completed(self,token,ok,result):
        self.busy=False
        if ok:
            if token in ('config','start_config'):self.load_group('config',result)
            elif token in self.pid_fields:self.load_group(token,result)
            elif token=='maintenance':self.read_maintenance(result)
        else:
            self.log(str(result)+'；未执行批次中剩余命令，请检查设备实际值')
            QMessageBox.warning(self,'操作未完成',str(result))
        self.refresh_enabled()

    def read_all(self):
        if not self.last or self.busy:return
        for group in ('config','heat','fan','preheat'):self.load_group(group,self.last)
        self.read_maintenance()
        self.log('已读取设备快照中的全部参数')

    def read_group(self,group):
        if self.last and not self.busy:self.load_group(group,self.last)

    def load_group(self,group,snap):
        self.loading=True
        if group=='config':
            c=snap['config']; self.mode.setCurrentIndex(c['mode'])
            self.fields['target_temp_deci_c'].setValue(c['target_temp_deci_c'])
            self.fields['duration_min'].setValue(c['duration_sec']//60)
            for key in ('air_level','mist_level'):self.fields[key].setCurrentIndex(c[key])
            self.keep_warm.setChecked(bool(c['keep_warm_enabled']))
            self.mode_changed()
        else:
            p=snap['pids'][group]
            for i,key in enumerate(('kp_milli','ki_milli','kd_milli','integral_limit_permille')):
                self.pid_fields[group][i].setValue(p[key]/1000 if i<3 else p[key])
        self.loading=False; self.dirty.discard(group)
        self.edit_status.setText('待应用：'+', '.join(sorted(self.dirty)) if self.dirty else '参数已与设备读回同步；EEPROM 为固件延迟保存')

    def read_maintenance(self,snap=None):
        snap=snap if isinstance(snap,dict) else self.last
        if not snap:return
        m=snap['maintenance']; self.manual_fan.setCurrentIndex(m['fan']); self.manual_mist.setCurrentIndex(m['mist']); self.manual_heat.setValue(m['heat'])

    def on_snapshot(self,snap):
        self.last=snap; r=snap['runtime']; state=r['state']
        self.connection.setText(('调试控制中' if snap['control'] else '观察模式')+f' · 设备运行 {snap["uptime_ms"]/1000:.1f}s · ELF 已匹配')
        self.state.setText(STATES[state] if state<len(STATES) else f'未知状态 {state}')
        vals=dict(remaining=f'{r["remaining_sec"]//60:02d}:{r["remaining_sec"]%60:02d}',fault=f'{r["fault"]} '+FAULTS.get(r['fault'],''),
            phase=PHASES[r['phase']] if r['phase']<len(PHASES) else str(r['phase']),heartbeat='正常' if r['heartbeat'] else '超时',
            heat=f'{r["heat_output"]/10:.1f}%',fan=f'{r["fan_base"]}% + {r["fan_boost"]/10:.1f}% = {r["fan_output"]}%',
            rpm='未实现采集',mist=('在线' if r['mist_online'] else '离线')+f' / {"运行" if r["mist_running"] else "停止"} / 档位 {r["mist_actual"]} / 故障 {r["mist_fault"]}',
            cover='关闭' if r['cover'] else '打开',water='缺水' if r['mist_fault']==9 else '未报告缺水',
            liquid='有水' if r['liquid'] else '无水',gx='有效' if r['gx1832'] else '无效',adc=' / '.join(map(str,r['adc'])),
            pid=f'目标 {r["heat_target"]/10:.1f}°C · 误差 {r["heat_error"]/10:.1f}°C · I {r["heat_integral"]/10000:.1f}%',
            fanpid=f'误差 {r["fan_error"]/10:.1f}°C · I {r["fan_integral"]/10:.1f}%'+(' · 饱和' if r['fan_saturated'] else ''),
            maintenance='已进入' if snap['maintenance']['active'] else '未进入')
        vals.update({key:f'{v/10:.1f} °C' for key,v in zip(('pb11','pb10','pb12','pa5'),r['temperatures'])})
        for key,value in vals.items():self.values[key].setText(value)
        if r['fault'] and r['fault']!=self.last_fault:self.history_line(f'故障 {r["fault"]} · {self.state.text()}')
        self.last_fault=r['fault']
        if not self.initial_read:
            for group in ('config','heat','fan','preheat'):
                if group not in self.dirty:self.load_group(group,snap)
            self.read_maintenance(); self.initial_read=True
        self.trend.add(snap); self.record(snap)
        self.cache_status.setText(f'缓存 {len(self.trend.samples):,} / {self.trend.samples.maxlen:,} 组 · 最新采样 {r["sample_ms"]/1000:.1f}s')
        self.refresh_enabled()

    def history_line(self,text):
        self.history.insertItem(0,time.strftime('%H:%M:%S')+' '+text)
        while self.history.count()>500:self.history.takeItem(self.history.count()-1)

    def on_event(self,event):
        text=EVENTS.get(event['code'],str(event['code'])) if event['type']==1 else f'类型 {event["type"]} / 代码 {event["code"]}'
        self.history_line(text); self.log(f'设备事件 @{event["uptime_ms"]}ms：{text}')

    def keep_warm_tick(self):
        active=self.last and self.last.get('control') and self.last['runtime']['state']==10
        if not active:
            self.warm_started=None; self.warm_sent=False; self.warm_label.setText('保温 PC 计时未启动'); return
        if self.warm_started is None:self.warm_started=time.monotonic(); self.log('保温两小时计时开始（本次 PC 控制会话）')
        remaining=max(0,7200-int(time.monotonic()-self.warm_started))
        self.warm_label.setText(f'PC 保温计时：剩余 {remaining//60} 分 {remaining%60} 秒；断开连接后计时不继续')
        if remaining==0 and not self.warm_sent and not self.busy:
            self.warm_sent=True; self.submit('warm_timeout',[(0x15,b'\0')])

    def parameters(self):
        p={}
        for key,w in self.fields.items():
            p[key]=w.currentIndex() if isinstance(w,QComboBox) else w.isChecked() if isinstance(w,QCheckBox) else w.value()
        for group,prefix in (('heat',''),('fan','fan_'),('preheat','preheat_')):
            for key,w in zip(('kp','ki','kd','i_limit_permille'),self.pid_fields[group]):p[prefix+key]=w.value()
        return p

    def import_params(self):
        path,_=QFileDialog.getOpenFileName(self,'导入参数','','JSON (*.json)')
        if not path:return
        try:
            p=validate_params(json.loads(Path(path).read_text(encoding='utf-8')))
            for key,w in self.fields.items():
                if isinstance(w,QComboBox):w.setCurrentIndex(p[key])
                elif isinstance(w,QCheckBox):w.setChecked(p[key])
                else:w.setValue(p[key])
            for group,prefix in (('heat',''),('fan','fan_'),('preheat','preheat_')):
                for key,w in zip(('kp','ki','kd','i_limit_permille'),self.pid_fields[group]):w.setValue(p[prefix+key])
            for group in ('config','heat','fan','preheat'):self.mark(group)
            self.log('参数已导入编辑区，尚未发送到设备')
        except Exception as exc:QMessageBox.warning(self,'导入失败',str(exc))

    def export_params(self):
        path,_=QFileDialog.getSaveFileName(self,'导出编辑区参数','nebulizer_params.json','JSON (*.json)')
        if path:
            try:Path(path).write_text(json.dumps(self.parameters(),ensure_ascii=False,indent=2),encoding='utf-8')
            except Exception as exc:QMessageBox.warning(self,'导出失败',str(exc))

    def clear_trend(self):
        self.trend.clear()
        self.cache_status.setText('缓存已清空 · 等待下一次采样')
        self.log('已清除曲线缓存并恢复实时跟随；CSV 记录继续')

    def export_trend(self):
        path,_=QFileDialog.getSaveFileName(self,'导出曲线','nebulizer_trend.csv','CSV (*.csv)')
        if path and self.trend.samples:
            try:
                with open(path,'w',encoding='utf-8-sig',newline='') as fp:
                    writer=csv.DictWriter(fp,fieldnames=list(self.trend.samples[0])); writer.writeheader(); writer.writerows(self.trend.samples)
            except Exception as exc:QMessageBox.warning(self,'导出失败',str(exc))

    def toggle_record(self):
        if self.csv_file:
            self.csv_file.close(); self.csv_file=None; self.record_button.setText('开始记录 CSV'); return
        path,_=QFileDialog.getSaveFileName(self,'连续记录','nebulizer_record.csv','CSV (*.csv)')
        if not path:return
        try:
            self.csv_file=open(path,'w',encoding='utf-8-sig',newline=''); self.csv_writer=csv.writer(self.csv_file)
            self.csv_writer.writerow(['PC_time','uptime_ms','sample_ms','state','fault','PB11_出口温度_C','PB10_旧反馈温度_C','PB12_保护温度_C','PA5_锅体温度_C','heat_percent','fan_percent','target_C','error_C','integral_percent'])
            self.last_record_sequence=None; self.record_button.setText('停止记录 CSV')
        except Exception as exc:QMessageBox.warning(self,'记录失败',str(exc))

    def record(self,snap):
        if not self.csv_file or snap['sequence']==self.last_record_sequence:return
        try:
            r=snap['runtime']; self.csv_writer.writerow([time.strftime('%Y-%m-%d %H:%M:%S'),snap['uptime_ms'],r['sample_ms'],r['state'],r['fault'],
                *[t/10 for t in r['temperatures']],r['heat_output']/10,r['fan_output'],r['heat_target']/10,r['heat_error']/10,r['heat_integral']/10000])
            self.csv_file.flush(); self.last_record_sequence=snap['sequence']
        except Exception as exc:
            self.log('记录失败：'+str(exc)); self.csv_file.close(); self.csv_file=None; self.record_button.setText('开始记录 CSV')

    def export_log(self):
        path,_=QFileDialog.getSaveFileName(self,'导出日志','nebulizer_log.txt','Text (*.txt)')
        if path:
            try:Path(path).write_text(self.log_view.toPlainText(),encoding='utf-8')
            except Exception as exc:QMessageBox.warning(self,'导出失败',str(exc))

    def closeEvent(self,event):
        if self.worker:
            self.closing=True; self.worker.stop_requested.set(); self.connection.setText('正在释放设备控制…'); event.ignore(); return
        if self.probe_worker and self.probe_worker.isRunning():
            self.closing=True; self.probe_worker.finished.connect(self.close); event.ignore(); return
        if self.csv_file:self.csv_file.close()
        event.accept()

def main():
    app=QApplication(sys.argv); app.setStyle('Fusion'); app.setStyleSheet(STYLE)
    window=Window(); window.show(); return app.exec()

if __name__=='__main__':raise SystemExit(main())

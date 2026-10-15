"""Shared upgrade dialog for both Nebulizer workbenches."""
from pathlib import Path
import threading
from PyQt6.QtCore import QThread, pyqtSignal
from PyQt6.QtWidgets import (QDialog,QVBoxLayout,QHBoxLayout,QLabel,QComboBox,
    QLineEdit,QPushButton,QFileDialog,QProgressBar,QPlainTextEdit,QMessageBox)
from .protocol import Firmware
from .client import Updater, UpdateCancelled

class UpgradeWorker(QThread):
    message = pyqtSignal(str)
    progress = pyqtSignal(int)
    result = pyqtSignal(bool,str)
    def __init__(self, port, package):
        super().__init__()
        self.port, self.package = port, package
        self.cancel = threading.Event()
    def run(self):
        try:
            firmware = Firmware.load(Path(self.package))
            import serial
            with serial.Serial(self.port,115200,timeout=.05,write_timeout=2) as port:
                result = Updater(port,self.message.emit,self.progress.emit,self.cancel).update(firmware)
            self.result.emit(True,result)
        except UpdateCancelled as exc: self.result.emit(False,str(exc))
        except Exception as exc: self.result.emit(False,str(exc))

class UpgradeDialog(QDialog):
    def __init__(self,parent=None,port=''):
        super().__init__(parent)
        self.setWindowTitle('固件升级 · USART1 / 单 App')
        self.resize(760,470)
        self.worker=None; self.close_pending=False
        layout=QVBoxLayout(self)
        label=QLabel('升级将覆盖旧 App，无法自动回滚。请停止治疗、保温和维护，释放调试控制。\n'
                     '升级中断后保留 Boot，可重新发送完整升级包。首次安装需烧录 factory.hex。')
        label.setWordWrap(True); layout.addWidget(label)
        row=QHBoxLayout(); self.port=QComboBox(); self.port.setEditable(True)
        refresh=QPushButton('刷新串口'); refresh.clicked.connect(self.refresh_ports)
        self.refresh_button=refresh
        row.addWidget(QLabel('上位机串口（115200）')); row.addWidget(self.port,1); row.addWidget(refresh)
        layout.addLayout(row)
        row=QHBoxLayout(); self.package=QLineEdit(str(Path(__file__).resolve().parents[2]/'build/upgrade-release/nebulizer.nfu'))
        self.browse=QPushButton('选择升级包'); self.browse.clicked.connect(self.choose)
        row.addWidget(self.package,1); row.addWidget(self.browse); layout.addLayout(row)
        self.summary=QLabel('仅接受 .nfu 升级包；普通固件、Boot 和 factory 文件不能用于串口升级。')
        self.summary.setWordWrap(True); layout.addWidget(self.summary)
        self.progress=QProgressBar(); layout.addWidget(self.progress)
        self.logs=QPlainTextEdit(); self.logs.setReadOnly(True); layout.addWidget(self.logs,1)
        row=QHBoxLayout(); self.start=QPushButton('开始升级（覆盖旧 App）'); self.cancel=QPushButton('取消传输')
        self.cancel.setEnabled(False)
        self.start.clicked.connect(self.begin); self.cancel.clicked.connect(self.cancel_update)
        row.addWidget(self.start); row.addWidget(self.cancel); layout.addLayout(row)
        self.refresh_ports()
        if port: self.port.setCurrentText(port)
    def refresh_ports(self):
        import serial.tools.list_ports
        old=self.port.currentText(); self.port.clear()
        self.port.addItems(sorted(p.device for p in serial.tools.list_ports.comports()))
        if old:self.port.setCurrentText(old)
    def choose(self):
        path,_=QFileDialog.getOpenFileName(self,'选择单 App 升级包',self.package.text(),'Nebulizer firmware (*.nfu)')
        if path:self.package.setText(path)
    def begin(self):
        if self.worker:return
        try:
            firmware=Firmware.load(Path(self.package.text().strip()))
            if not self.port.currentText().strip():raise ValueError('请选择上位机通信串口')
        except Exception as exc:
            QMessageBox.warning(self,'升级包检查',str(exc));return
        self.summary.setText(f'App {len(firmware.data)} 字节 · CRC32 {firmware.crc:08X} · 地址 0x08004000')
        self.progress.setValue(0); self.logs.clear(); self.close_pending=False
        self.worker=UpgradeWorker(self.port.currentText().strip(),self.package.text().strip())
        self.worker.message.connect(self.logs.appendPlainText); self.worker.progress.connect(self.progress.setValue)
        self.worker.result.connect(self.show_result); self.worker.finished.connect(self.finished_update)
        for widget in (self.start,self.port,self.package,self.browse,self.refresh_button):widget.setEnabled(False)
        self.cancel.setEnabled(True); self.worker.start()
    def show_result(self,ok,message):
        self.logs.appendPlainText(('升级结果：' if ok else '传输停止：')+message)
    def cancel_update(self):
        if self.worker:self.worker.cancel.set();self.cancel.setEnabled(False)
    def finished_update(self):
        thread=self.worker; self.worker=None; thread.deleteLater()
        for widget in (self.start,self.port,self.package,self.browse,self.refresh_button):widget.setEnabled(True)
        self.cancel.setEnabled(False)
        if self.close_pending:self.close()
    def reject(self):
        if self.worker:self.close_pending=True;self.cancel_update()
        else:super().reject()
    def closeEvent(self,event):
        if self.worker:
            self.close_pending=True;self.cancel_update();event.ignore()
        else:super().closeEvent(event)

"""Explicit factory programming, separate from the non-halting debug transport."""
from pathlib import Path
from PyQt6.QtCore import QThread, pyqtSignal
from PyQt6.QtWidgets import (QDialog,QVBoxLayout,QHBoxLayout,QLabel,QLineEdit,
    QPushButton,QCheckBox,QFileDialog,QProgressBar,QPlainTextEdit,QMessageBox)
from fw_update.protocol import APP_BASE, META_BASE, RAM_END, validate_app, metadata_bytes

from device_io.factory import BASE, SIZE, FactoryImage, verify_image, check_device, safe_outputs, program_factory

class FactoryWorker(QThread):
    message=pyqtSignal(str)
    progress=pyqtSignal(int)
    result=pyqtSignal(bool,str)
    def __init__(self,options,image,run):
        super().__init__();self.options=options;self.image=image;self.run_after=run
    def run(self):
        try:
            program_factory(self.options,self.image,self.message.emit,self.progress.emit,self.run_after)
            self.result.emit(True,'工厂 HEX 烧录及校验完成')
        except Exception as exc:self.result.emit(False,f'烧录失败：{exc}；请保持负载停止并检查设备，未自动重试')

class FactoryDialog(QDialog):
    def __init__(self,parent,options):
        super().__init__(parent);self.options=options;self.worker=None;self.succeeded=False;self.image=None
        self.setWindowTitle('工厂 HEX 烧录 · Boot + App');self.resize(820,480)
        layout=QVBoxLayout(self)
        label=QLabel(f"探针：{options['backend']} / {options['serial']} · STM32G070CBT6 · SWD {options['speed']} kHz\n"
            '此操作覆盖整个内部 Flash（包含 Boot），并暂停 CPU。EEPROM 参数保留。\n'
            'DAPLink 首次使用需安装芯片包：python -m pyocd pack install stm32g070cbtx')
        label.setWordWrap(True);layout.addWidget(label)
        self.path=QLineEdit(str(Path(__file__).resolve().parents[2]/'build/upgrade-release/nebulizer_factory.hex'))
        self.browse=QPushButton('选择工厂 HEX');self.browse.clicked.connect(self.choose)
        row=QHBoxLayout();row.addWidget(self.path,1);row.addWidget(self.browse);layout.addLayout(row)
        self.confirm=QCheckBox('已停止实际治疗/加热/雾化，确认覆盖 Boot 和 App')
        self.reset=QCheckBox('校验成功后复位运行（App 将恢复原有 EEPROM 配置）')
        layout.addWidget(self.confirm);layout.addWidget(self.reset)
        self.progress=QProgressBar();layout.addWidget(self.progress)
        self.logs=QPlainTextEdit();self.logs.setReadOnly(True);layout.addWidget(self.logs,1)
        self.start=QPushButton('检查镜像并烧录');self.start.clicked.connect(self.begin);layout.addWidget(self.start)
    def choose(self):
        path,_=QFileDialog.getOpenFileName(self,'选择完整工厂 HEX',self.path.text(),'Intel HEX (*.hex)')
        if path:self.path.setText(path)
    def begin(self):
        if self.worker:return
        if not self.confirm.isChecked():
            QMessageBox.warning(self,'烧录前确认','请先停止实际输出，再勾选覆盖确认。');return
        try:image=FactoryImage(self.path.text().strip())
        except Exception as exc:QMessageBox.warning(self,'工厂镜像检查',str(exc));return
        self.image=image;self.succeeded=False
        self.logs.clear();self.logs.appendPlainText(f'App {image.app_length} 字节\n镜像 SHA256：{image.sha256}')
        self.progress.setValue(0)
        self.worker=FactoryWorker(dict(self.options),image,self.reset.isChecked())
        self.worker.message.connect(self.logs.appendPlainText);self.worker.progress.connect(self.progress.setValue)
        self.worker.result.connect(self.show_result);self.worker.finished.connect(self.finished_work)
        for w in (self.path,self.browse,self.confirm,self.reset,self.start):w.setEnabled(False)
        self.worker.start()
    def show_result(self,ok,message):self.succeeded=ok;self.logs.appendPlainText(message)
    def finished_work(self):
        thread=self.worker;self.worker=None;thread.deleteLater()
        for w in (self.path,self.browse,self.confirm,self.reset,self.start):w.setEnabled(True)
        self.confirm.setChecked(False)
    def reject(self):
        if not self.worker:super().reject()
    def closeEvent(self,event):
        if self.worker:event.ignore()
        else:super().closeEvent(event)

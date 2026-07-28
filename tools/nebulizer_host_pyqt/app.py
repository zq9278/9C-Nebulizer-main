from __future__ import annotations

import json
import sys
from dataclasses import asdict

from PyQt6.QtCore import QDateTime, Qt
from PyQt6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QDoubleSpinBox,
    QFileDialog,
    QFormLayout,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QListWidget,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QSlider,
    QSpinBox,
    QSplitter,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

from models import (
    ConfigModel,
    KettleTargetModel,
    MaintenanceModel,
    OutletControlModel,
    PidModel,
    RuntimeModel,
    StatusModel,
)
from serial_client import SerialClient
from trend_widget import TrendWidget


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.client = SerialClient()
        self.last_fault_code = 0
        self.last_status: StatusModel | None = None
        self.config_dirty = False
        self.pid_dirty = False
        self.outlet_dirty = False
        self._suppress_dirty_tracking = False
        self._config_skip_logged = False
        self._pid_skip_logged = False
        self._build_ui()
        self._bind_signals()
        self.refresh_ports()
        self._update_mode_dependent_controls()
        self._update_debug_channel()

    def _build_ui(self) -> None:
        central = QWidget()
        main_layout = QVBoxLayout(central)
        top_bar = QHBoxLayout()
        splitter = QSplitter()
        self.port_box = QComboBox()
        self.connect_button = QPushButton("Connect")
        refresh_button = QPushButton("Refresh Ports")
        read_all_button = QPushButton("Read All")
        import_button = QPushButton("Import Params")
        export_button = QPushButton("Export Params")
        self.connection_state = QLabel("Disconnected")
        top_bar.addWidget(QLabel("USART1 Port:"))
        top_bar.addWidget(self.port_box)
        top_bar.addWidget(refresh_button)
        top_bar.addWidget(read_all_button)
        top_bar.addWidget(self.connect_button)
        top_bar.addWidget(import_button)
        top_bar.addWidget(export_button)
        top_bar.addStretch(1)
        top_bar.addWidget(self.connection_state)

        left_pane = QWidget()
        left_layout = QVBoxLayout(left_pane)

        config_group = QGroupBox("Treatment Control")
        config_form = QFormLayout(config_group)
        self.mode_box = QComboBox()
        self.mode_box.addItem("HOT", 0)
        self.mode_box.addItem("COLD", 1)
        self.target_temp_box = QSpinBox()
        self.target_temp_box.setRange(350, 450)
        self.target_temp_box.setSuffix(" /10C")
        self.duration_box = QSpinBox()
        self.duration_box.setRange(1, 30)
        self.duration_box.setSuffix(" min")
        self.air_level_box = QComboBox()
        self.air_level_box.addItems(["OFF", "LOW", "MID", "HIGH"])
        self.mist_level_box = QComboBox()
        self.mist_level_box.addItems(["OFF", "LOW", "MID", "HIGH"])
        read_config_button = QPushButton("Read Config")
        apply_config_button = QPushButton("Apply Config")
        start_button = QPushButton("Start")
        pause_button = QPushButton("Pause")
        resume_button = QPushButton("Resume")
        stop_button = QPushButton("Stop")
        clear_fault_button = QPushButton("Clear Fault")
        button_grid = QGridLayout()
        button_grid.addWidget(start_button, 0, 0)
        button_grid.addWidget(pause_button, 0, 1)
        button_grid.addWidget(resume_button, 1, 0)
        button_grid.addWidget(stop_button, 1, 1)
        button_grid.addWidget(clear_fault_button, 2, 0, 1, 2)
        config_form.addRow("Mode", self.mode_box)
        config_form.addRow("Target Temp", self.target_temp_box)
        config_form.addRow("Duration", self.duration_box)
        config_form.addRow("Air Level", self.air_level_box)
        config_form.addRow("Mist Level", self.mist_level_box)
        config_form.addRow(read_config_button, apply_config_button)
        config_form.addRow(button_grid)

        debug_group = QGroupBox("Temperature Debug Channel")
        debug_form = QFormLayout(debug_group)
        self.debug_channel_box = QComboBox()
        self.debug_channel_box.addItem("Outlet Temp Loop", "outlet")
        self.debug_channel_box.addItem("Kettle PID Loop", "kettle")
        debug_form.addRow("Channel", self.debug_channel_box)

        outlet_group = QGroupBox("Outlet Temp Loop Debug")
        outlet_form = QFormLayout(outlet_group)
        self.outlet_base_box = QSpinBox()
        self.outlet_margin_box = QSpinBox()
        self.outlet_air_low_box = QSpinBox()
        self.outlet_air_mid_box = QSpinBox()
        self.outlet_air_high_box = QSpinBox()
        self.outlet_mist_low_box = QSpinBox()
        self.outlet_mist_mid_box = QSpinBox()
        self.outlet_mist_high_box = QSpinBox()
        for box in (
            self.outlet_base_box,
            self.outlet_margin_box,
            self.outlet_air_low_box,
            self.outlet_air_mid_box,
            self.outlet_air_high_box,
            self.outlet_mist_low_box,
            self.outlet_mist_mid_box,
            self.outlet_mist_high_box,
        ):
            box.setRange(0, 200)
            box.setSuffix(" /10C")
        self.outlet_margin_box.setRange(0, 100)
        for box in (
            self.outlet_air_low_box,
            self.outlet_air_mid_box,
            self.outlet_air_high_box,
            self.outlet_mist_low_box,
            self.outlet_mist_mid_box,
            self.outlet_mist_high_box,
        ):
            box.setRange(0, 100)
        self.outlet_base_box.setValue(140)
        self.outlet_margin_box.setValue(30)
        self.outlet_air_low_box.setValue(10)
        self.outlet_air_mid_box.setValue(25)
        self.outlet_air_high_box.setValue(40)
        self.outlet_mist_low_box.setValue(10)
        self.outlet_mist_mid_box.setValue(20)
        self.outlet_mist_high_box.setValue(30)
        read_outlet_button = QPushButton("Read Outlet Loop")
        apply_outlet_button = QPushButton("Apply Outlet Loop")
        outlet_form.addRow("Base Offset", self.outlet_base_box)
        outlet_form.addRow("Safety Margin", self.outlet_margin_box)
        outlet_form.addRow("Air Low FF", self.outlet_air_low_box)
        outlet_form.addRow("Air Mid FF", self.outlet_air_mid_box)
        outlet_form.addRow("Air High FF", self.outlet_air_high_box)
        outlet_form.addRow("Mist Low FF", self.outlet_mist_low_box)
        outlet_form.addRow("Mist Mid FF", self.outlet_mist_mid_box)
        outlet_form.addRow("Mist High FF", self.outlet_mist_high_box)
        outlet_form.addRow(read_outlet_button, apply_outlet_button)

        pid_group = QGroupBox("Kettle PID Debug")
        pid_form = QFormLayout(pid_group)
        self.kp_box = QDoubleSpinBox()
        self.ki_box = QDoubleSpinBox()
        self.kd_box = QDoubleSpinBox()
        self.i_limit_box = QSpinBox()
        self.kp_slider = QSlider(Qt.Orientation.Horizontal)
        self.ki_slider = QSlider(Qt.Orientation.Horizontal)
        self.kd_slider = QSlider(Qt.Orientation.Horizontal)
        self.i_limit_slider = QSlider(Qt.Orientation.Horizontal)
        for box in (self.kp_box, self.ki_box, self.kd_box):
            box.setDecimals(3)
            box.setRange(0.0, 120.0)
        self.ki_box.setRange(0.0, 2.0)
        self.kd_box.setRange(0.0, 5.0)
        self.i_limit_box.setRange(0, 1000)
        self.kp_box.setSingleStep(0.1)
        self.ki_box.setSingleStep(0.01)
        self.kd_box.setSingleStep(0.01)
        self.kp_slider.setRange(0, 12000)
        self.ki_slider.setRange(0, 200)
        self.kd_slider.setRange(0, 500)
        self.i_limit_slider.setRange(0, 1000)
        self.kp_box.setValue(12.0)
        self.ki_box.setValue(0.120)
        self.kd_box.setValue(0.0)
        self.i_limit_box.setValue(450)
        self.kp_slider.setValue(1200)
        self.ki_slider.setValue(12)
        self.kd_slider.setValue(0)
        self.i_limit_slider.setValue(450)
        self.kettle_pid_temp_label = QLabel("-")
        self.kettle_pid_target_label = QLabel("-")
        self.kettle_pid_error_label = QLabel("-")
        self.kettle_target_override_box = QCheckBox("Enable manual kettle target")
        self.kettle_target_box = QSpinBox()
        self.kettle_target_box.setRange(-32768, 32767)
        self.kettle_target_box.setSuffix(" /10C")
        self.kettle_target_box.setValue(580)
        read_pid_button = QPushButton("Read Kettle PID")
        apply_pid_button = QPushButton("Apply Kettle PID")
        read_kettle_target_button = QPushButton("Read Kettle Target")
        apply_kettle_target_button = QPushButton("Apply Kettle Target")
        pid_form.addRow("Kp", self._build_pid_row(self.kp_box, self.kp_slider))
        pid_form.addRow("Ki", self._build_pid_row(self.ki_box, self.ki_slider))
        pid_form.addRow("Kd", self._build_pid_row(self.kd_box, self.kd_slider))
        pid_form.addRow("I Limit", self._build_pid_row(self.i_limit_box, self.i_limit_slider))
        pid_form.addRow("Manual Kettle Target", self.kettle_target_override_box)
        pid_form.addRow("Kettle Target Set", self.kettle_target_box)
        pid_form.addRow("Kettle Temp", self.kettle_pid_temp_label)
        pid_form.addRow("Kettle Target", self.kettle_pid_target_label)
        pid_form.addRow("Kettle Error", self.kettle_pid_error_label)
        pid_form.addRow(read_pid_button, apply_pid_button)
        pid_form.addRow(read_kettle_target_button, apply_kettle_target_button)

        maintenance_group = QGroupBox("Maintenance Mode")
        maintenance_form = QFormLayout(maintenance_group)
        self.manual_fan_box = QComboBox()
        self.manual_fan_box.addItems(["OFF", "LOW", "MID", "HIGH"])
        self.manual_mist_box = QComboBox()
        self.manual_mist_box.addItems(["OFF", "LOW", "MID", "HIGH"])
        self.manual_heat_box = QSpinBox()
        self.manual_heat_box.setRange(0, 1000)
        self.manual_heat_box.setSuffix(" permille")
        enter_maint_button = QPushButton("Enter Maintenance")
        exit_maint_button = QPushButton("Exit Maintenance")
        apply_manual_button = QPushButton("Apply Manual Outputs")
        maintenance_form.addRow("Fan", self.manual_fan_box)
        maintenance_form.addRow("Mist", self.manual_mist_box)
        maintenance_form.addRow("Heat", self.manual_heat_box)
        maintenance_form.addRow(enter_maint_button)
        maintenance_form.addRow(apply_manual_button)
        maintenance_form.addRow(exit_maint_button)

        left_layout.addWidget(config_group)
        left_layout.addWidget(debug_group)
        left_layout.addWidget(outlet_group)
        left_layout.addWidget(pid_group)
        left_layout.addWidget(maintenance_group)
        left_layout.addStretch(1)

        right_pane = QWidget()
        right_layout = QVBoxLayout(right_pane)
        status_group = QGroupBox("Runtime Overview")
        status_form = QFormLayout(status_group)
        self.state_label = QLabel("-")
        self.fault_label = QLabel("-")
        self.remaining_label = QLabel("-")
        self.heartbeat_label = QLabel("-")
        self.outlet_temp_label = QLabel("-")
        self.reserved_temp_label = QLabel("-")
        self.overtemp_temp_label = QLabel("-")
        self.kettle_temp_label = QLabel("-")
        self.fan_label = QLabel("-")
        self.mist_label = QLabel("-")
        self.mist_fault_label = QLabel("-")
        self.mist_water_label = QLabel("-")
        self.stainless_level_label = QLabel("-")
        self.cover_label = QLabel("-")
        self.heat_label = QLabel("-")
        self.maintenance_label = QLabel("-")
        for name, label in [
            ("State", self.state_label),
            ("Fault", self.fault_label),
            ("Remaining", self.remaining_label),
            ("Heartbeat", self.heartbeat_label),
            ("PB11 NTC / Outlet Temp", self.outlet_temp_label),
            ("PB10 NTC / Reserved Temp", self.reserved_temp_label),
            ("PB12 NTC / Overtemp Protect Temp", self.overtemp_temp_label),
            ("PA5 NTC / Kettle Temp", self.kettle_temp_label),
            ("Fan RPM", self.fan_label),
            ("Mist", self.mist_label),
            ("Mist Fault Code", self.mist_fault_label),
            ("Mist Water Level", self.mist_water_label),
            ("Stainless Pot Level", self.stainless_level_label),
            ("Cover", self.cover_label),
            ("Heat Output", self.heat_label),
            ("Maintenance", self.maintenance_label),
        ]:
            status_form.addRow(name, label)

        tabs = QTabWidget()
        self.trend_widget = TrendWidget()
        trend_tab = QWidget()
        trend_layout = QVBoxLayout(trend_tab)
        trend_filter_layout = QHBoxLayout()
        self._trend_outlet_cb = QCheckBox("Outlet")
        self._trend_kettle_cb = QCheckBox("Kettle")
        self._trend_target_cb = QCheckBox("Target")
        self._trend_output_cb = QCheckBox("Power")
        self._trend_error_cb = QCheckBox("Error")
        self._trend_integral_cb = QCheckBox("Integral")
        self._zoom_in_button = QPushButton("Zoom +")
        self._zoom_out_button = QPushButton("Zoom -")
        self._reset_zoom_button = QPushButton("Reset View")
        for checkbox in (
            self._trend_outlet_cb,
            self._trend_kettle_cb,
            self._trend_target_cb,
            self._trend_output_cb,
            self._trend_error_cb,
            self._trend_integral_cb,
        ):
            checkbox.setChecked(True)
            trend_filter_layout.addWidget(checkbox)
        trend_filter_layout.addStretch(1)
        trend_filter_layout.addWidget(self._zoom_in_button)
        trend_filter_layout.addWidget(self._zoom_out_button)
        trend_filter_layout.addWidget(self._reset_zoom_button)
        trend_layout.addLayout(trend_filter_layout)
        trend_layout.addWidget(self.trend_widget)
        tabs.addTab(trend_tab, "Trends")

        self.fault_history = QListWidget()
        fault_tab = QWidget()
        fault_layout = QVBoxLayout(fault_tab)
        fault_layout.addWidget(self.fault_history)
        tabs.addTab(fault_tab, "Fault History")

        self.log_view = QPlainTextEdit()
        self.log_view.setReadOnly(True)
        log_tab = QWidget()
        log_layout = QVBoxLayout(log_tab)
        log_layout.addWidget(self.log_view)
        tabs.addTab(log_tab, "Protocol Log")

        right_layout.addWidget(status_group)
        right_layout.addWidget(tabs, 1)

        splitter.addWidget(left_pane)
        splitter.addWidget(right_pane)
        splitter.setStretchFactor(1, 1)
        main_layout.addLayout(top_bar)
        main_layout.addWidget(splitter, 1)
        self.setCentralWidget(central)
        self.setWindowTitle("Nebulizer Host PyQt - USART1")
        self.resize(1360, 860)

        self._refresh_button = refresh_button
        self._read_all_button = read_all_button
        self._import_button = import_button
        self._export_button = export_button
        self._read_config_button = read_config_button
        self._apply_config_button = apply_config_button
        self._outlet_group = outlet_group
        self._pid_group = pid_group
        self._read_outlet_button = read_outlet_button
        self._apply_outlet_button = apply_outlet_button
        self._start_button = start_button
        self._pause_button = pause_button
        self._resume_button = resume_button
        self._stop_button = stop_button
        self._clear_fault_button = clear_fault_button
        self._read_pid_button = read_pid_button
        self._apply_pid_button = apply_pid_button
        self._read_kettle_target_button = read_kettle_target_button
        self._apply_kettle_target_button = apply_kettle_target_button
        self._enter_maint_button = enter_maint_button
        self._exit_maint_button = exit_maint_button
        self._apply_manual_button = apply_manual_button

    def _bind_signals(self) -> None:
        self._refresh_button.clicked.connect(self.refresh_ports)
        self._read_all_button.clicked.connect(self.request_all)
        self.connect_button.clicked.connect(self.toggle_connection)
        self._import_button.clicked.connect(self.import_params)
        self._export_button.clicked.connect(self.export_params)
        self._read_config_button.clicked.connect(self.client.request_config)
        self._apply_config_button.clicked.connect(self.apply_config)
        self._read_outlet_button.clicked.connect(self.client.request_outlet_control)
        self._apply_outlet_button.clicked.connect(self.apply_outlet_control)
        self._read_pid_button.clicked.connect(self.client.request_kettle_pid)
        self._apply_pid_button.clicked.connect(self.apply_pid)
        self._read_kettle_target_button.clicked.connect(self.client.request_kettle_target)
        self._apply_kettle_target_button.clicked.connect(self.apply_kettle_target)
        self._enter_maint_button.clicked.connect(self.client.enter_maintenance)
        self._exit_maint_button.clicked.connect(self.client.exit_maintenance)
        self._apply_manual_button.clicked.connect(self.apply_manual_outputs)
        self._start_button.clicked.connect(self.send_start)
        self._pause_button.clicked.connect(self.client.send_pause)
        self._resume_button.clicked.connect(self.client.send_resume)
        self._stop_button.clicked.connect(self.client.send_stop)
        self._clear_fault_button.clicked.connect(self.client.clear_fault)
        self._trend_outlet_cb.toggled.connect(
            lambda checked: self.trend_widget.set_series_visible("outlet", checked)
        )
        self._trend_kettle_cb.toggled.connect(
            lambda checked: self.trend_widget.set_series_visible("kettle", checked)
        )
        self._trend_target_cb.toggled.connect(
            lambda checked: self.trend_widget.set_series_visible("target", checked)
        )
        self._trend_output_cb.toggled.connect(
            lambda checked: self.trend_widget.set_series_visible("output", checked)
        )
        self._trend_error_cb.toggled.connect(
            lambda checked: self.trend_widget.set_series_visible("error", checked)
        )
        self._trend_integral_cb.toggled.connect(
            lambda checked: self.trend_widget.set_series_visible("integral", checked)
        )
        self._zoom_in_button.clicked.connect(self.trend_widget.zoom_in)
        self._zoom_out_button.clicked.connect(self.trend_widget.zoom_out)
        self._reset_zoom_button.clicked.connect(self.trend_widget.reset_view)

        self.client.connection_changed.connect(self.update_connection_state)
        self.client.log_message.connect(self.append_log)
        self.client.ack_received.connect(self.handle_ack)
        self.client.status_updated.connect(self.update_status)
        self.client.config_updated.connect(self.update_config)
        self.client.outlet_control_updated.connect(self.update_outlet_control)
        self.client.kettle_target_updated.connect(self.update_kettle_target)
        self.client.pid_updated.connect(self.update_pid)
        self.client.runtime_updated.connect(self.update_runtime)
        self.client.maintenance_updated.connect(self.update_maintenance)

        self.mode_box.currentIndexChanged.connect(self._mark_config_dirty)
        self.mode_box.currentIndexChanged.connect(self._update_mode_dependent_controls)
        self.debug_channel_box.currentIndexChanged.connect(self._update_debug_channel)
        self.target_temp_box.valueChanged.connect(self._mark_config_dirty)
        self.duration_box.valueChanged.connect(self._mark_config_dirty)
        self.air_level_box.currentIndexChanged.connect(self._mark_config_dirty)
        self.mist_level_box.currentIndexChanged.connect(self._mark_config_dirty)
        self.kp_box.valueChanged.connect(self._mark_pid_dirty)
        self.ki_box.valueChanged.connect(self._mark_pid_dirty)
        self.kd_box.valueChanged.connect(self._mark_pid_dirty)
        self.i_limit_box.valueChanged.connect(self._mark_pid_dirty)
        for box in (
            self.outlet_base_box,
            self.outlet_margin_box,
            self.outlet_air_low_box,
            self.outlet_air_mid_box,
            self.outlet_air_high_box,
            self.outlet_mist_low_box,
            self.outlet_mist_mid_box,
            self.outlet_mist_high_box,
        ):
            box.valueChanged.connect(self._mark_outlet_dirty)
        self.kp_box.valueChanged.connect(lambda value: self._sync_float_slider(self.kp_slider, value, 100.0))
        self.ki_box.valueChanged.connect(lambda value: self._sync_float_slider(self.ki_slider, value, 100.0))
        self.kd_box.valueChanged.connect(lambda value: self._sync_float_slider(self.kd_slider, value, 100.0))
        self.i_limit_box.valueChanged.connect(lambda value: self._sync_int_slider(self.i_limit_slider, value))
        self.kp_slider.valueChanged.connect(lambda value: self._sync_slider_float_box(self.kp_box, value, 100.0))
        self.ki_slider.valueChanged.connect(lambda value: self._sync_slider_float_box(self.ki_box, value, 100.0))
        self.kd_slider.valueChanged.connect(lambda value: self._sync_slider_float_box(self.kd_box, value, 100.0))
        self.i_limit_slider.valueChanged.connect(lambda value: self._sync_slider_int_box(self.i_limit_box, value))

    def _build_pid_row(self, editor: QWidget, slider: QSlider) -> QWidget:
        row = QWidget()
        layout = QHBoxLayout(row)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(editor)
        layout.addWidget(slider, 1)
        return row

    def _sync_float_slider(self, slider: QSlider, value: float, scale: float) -> None:
        target = int(round(value * scale))
        if slider.value() == target:
            return
        slider.blockSignals(True)
        slider.setValue(target)
        slider.blockSignals(False)

    def _sync_int_slider(self, slider: QSlider, value: int) -> None:
        if slider.value() == value:
            return
        slider.blockSignals(True)
        slider.setValue(value)
        slider.blockSignals(False)

    def _sync_slider_float_box(self, box: QDoubleSpinBox, value: int, scale: float) -> None:
        target = value / scale
        if abs(box.value() - target) < 0.0005:
            return
        box.blockSignals(True)
        box.setValue(target)
        box.blockSignals(False)
        self._mark_pid_dirty()

    def _sync_slider_int_box(self, box: QSpinBox, value: int) -> None:
        if box.value() == value:
            return
        box.blockSignals(True)
        box.setValue(value)
        box.blockSignals(False)
        self._mark_pid_dirty()

    def refresh_ports(self) -> None:
        current = self.port_box.currentText()
        self.port_box.clear()
        self.port_box.addItems(SerialClient.list_ports())
        idx = self.port_box.findText(current)
        if idx >= 0:
            self.port_box.setCurrentIndex(idx)

    def toggle_connection(self) -> None:
        if self.client.is_open():
            self.client.close_port()
        else:
            self.client.open_port(self.port_box.currentText(), 115200)

    def request_all(self) -> None:
        self.client.request_config()
        self.client.request_outlet_control()
        self.client.request_kettle_pid()
        self.client.request_kettle_target()
        self.client.request_status()
        self.client.request_runtime()
        self.client.request_maintenance()

    def apply_config(self, refresh_from_device: bool = True) -> None:
        self.client.set_mode(self.mode_box.currentData())
        self.client.set_target_temp(self.target_temp_box.value())
        self.client.set_time_minutes(self.duration_box.value())
        self.client.set_air_level(self.air_level_box.currentIndex())
        self.client.set_mist_level(self.mist_level_box.currentIndex())
        self.config_dirty = False
        self._config_skip_logged = False
        if refresh_from_device:
            self.client.request_config()

    def send_start(self) -> None:
        if self.config_dirty:
            self.append_log("local config has unsaved edits, applying before START")
            self.apply_config(refresh_from_device=False)

        self.client.send_start()

    def _update_mode_dependent_controls(self) -> None:
        hot_mode = self.mode_box.currentData() == 0

        self.target_temp_box.setEnabled(hot_mode)
        self.kp_box.setEnabled(hot_mode)
        self.ki_box.setEnabled(hot_mode)
        self.kd_box.setEnabled(hot_mode)
        self.i_limit_box.setEnabled(hot_mode)
        self.kp_slider.setEnabled(hot_mode)
        self.ki_slider.setEnabled(hot_mode)
        self.kd_slider.setEnabled(hot_mode)
        self.i_limit_slider.setEnabled(hot_mode)
        self.kettle_target_override_box.setEnabled(hot_mode)
        self.kettle_target_box.setEnabled(hot_mode)
        self._read_pid_button.setEnabled(hot_mode)
        self._apply_pid_button.setEnabled(hot_mode)
        self._read_kettle_target_button.setEnabled(hot_mode)
        self._apply_kettle_target_button.setEnabled(hot_mode)
        self._read_outlet_button.setEnabled(hot_mode)
        self._apply_outlet_button.setEnabled(hot_mode)

    def _update_debug_channel(self) -> None:
        channel = self.debug_channel_box.currentData()
        self._outlet_group.setVisible(channel == "outlet")
        self._pid_group.setVisible(channel == "kettle")
        self.trend_widget.set_channel(channel)

    def apply_pid(self) -> None:
        self.client.apply_kettle_pid(
            int(self.kp_box.value() * 1000.0),
            int(self.ki_box.value() * 1000.0),
            int(self.kd_box.value() * 1000.0),
            self.i_limit_box.value(),
        )
        self.pid_dirty = False
        self._pid_skip_logged = False

    def apply_kettle_target(self) -> None:
        self.client.apply_kettle_target(
            self.kettle_target_override_box.isChecked(),
            self.kettle_target_box.value(),
        )

    def apply_outlet_control(self) -> None:
        self.client.apply_outlet_control(
            self.outlet_base_box.value(),
            self.outlet_margin_box.value(),
            self.outlet_air_low_box.value(),
            self.outlet_air_mid_box.value(),
            self.outlet_air_high_box.value(),
            self.outlet_mist_low_box.value(),
            self.outlet_mist_mid_box.value(),
            self.outlet_mist_high_box.value(),
        )
        self.outlet_dirty = False

    def apply_manual_outputs(self) -> None:
        self.client.manual_set_fan(self.manual_fan_box.currentIndex())
        self.client.manual_set_mist(self.manual_mist_box.currentIndex())
        self.client.manual_set_heat(self.manual_heat_box.value())

    def export_params(self) -> None:
        path, _ = QFileDialog.getSaveFileName(
            self, "Export Params", "nebulizer_params.json", "JSON Files (*.json)"
        )
        if not path:
            return
        payload = {
            "mode": self.mode_box.currentData(),
            "target_temp_deci_c": self.target_temp_box.value(),
            "duration_min": self.duration_box.value(),
            "air_level": self.air_level_box.currentIndex(),
            "mist_level": self.mist_level_box.currentIndex(),
            "kp": self.kp_box.value(),
            "ki": self.ki_box.value(),
            "kd": self.kd_box.value(),
            "i_limit_permille": self.i_limit_box.value(),
            "outlet_base_offset_deci_c": self.outlet_base_box.value(),
            "outlet_target_margin_deci_c": self.outlet_margin_box.value(),
            "outlet_air_low_offset_deci_c": self.outlet_air_low_box.value(),
            "outlet_air_mid_offset_deci_c": self.outlet_air_mid_box.value(),
            "outlet_air_high_offset_deci_c": self.outlet_air_high_box.value(),
            "outlet_mist_low_offset_deci_c": self.outlet_mist_low_box.value(),
            "outlet_mist_mid_offset_deci_c": self.outlet_mist_mid_box.value(),
            "outlet_mist_high_offset_deci_c": self.outlet_mist_high_box.value(),
        }
        with open(path, "w", encoding="utf-8") as fp:
            json.dump(payload, fp, ensure_ascii=False, indent=2)
        self.append_log(f"parameters exported: {path}")

    def import_params(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "Import Params", "", "JSON Files (*.json)")
        if not path:
            return
        try:
            with open(path, "r", encoding="utf-8") as fp:
                payload = json.load(fp)
        except Exception as exc:
            QMessageBox.warning(self, "Import Failed", str(exc))
            return
        self.mode_box.setCurrentIndex(self.mode_box.findData(payload.get("mode", 0)))
        self.target_temp_box.setValue(payload.get("target_temp_deci_c", self.target_temp_box.value()))
        self.duration_box.setValue(payload.get("duration_min", self.duration_box.value()))
        self.air_level_box.setCurrentIndex(payload.get("air_level", self.air_level_box.currentIndex()))
        self.mist_level_box.setCurrentIndex(payload.get("mist_level", self.mist_level_box.currentIndex()))
        self.kp_box.setValue(payload.get("kp", self.kp_box.value()))
        self.ki_box.setValue(payload.get("ki", self.ki_box.value()))
        self.kd_box.setValue(payload.get("kd", self.kd_box.value()))
        self.i_limit_box.setValue(payload.get("i_limit_permille", self.i_limit_box.value()))
        self.outlet_base_box.setValue(
            payload.get("outlet_base_offset_deci_c", self.outlet_base_box.value())
        )
        self.outlet_margin_box.setValue(
            payload.get("outlet_target_margin_deci_c", self.outlet_margin_box.value())
        )
        self.outlet_air_low_box.setValue(
            payload.get("outlet_air_low_offset_deci_c", self.outlet_air_low_box.value())
        )
        self.outlet_air_mid_box.setValue(
            payload.get("outlet_air_mid_offset_deci_c", self.outlet_air_mid_box.value())
        )
        self.outlet_air_high_box.setValue(
            payload.get("outlet_air_high_offset_deci_c", self.outlet_air_high_box.value())
        )
        self.outlet_mist_low_box.setValue(
            payload.get("outlet_mist_low_offset_deci_c", self.outlet_mist_low_box.value())
        )
        self.outlet_mist_mid_box.setValue(
            payload.get("outlet_mist_mid_offset_deci_c", self.outlet_mist_mid_box.value())
        )
        self.outlet_mist_high_box.setValue(
            payload.get("outlet_mist_high_offset_deci_c", self.outlet_mist_high_box.value())
        )
        self.config_dirty = True
        self.pid_dirty = True
        self.outlet_dirty = True
        self._config_skip_logged = False
        self._pid_skip_logged = False
        self.append_log(f"parameters imported: {path}")

    def append_log(self, message: str) -> None:
        stamp = QDateTime.currentDateTime().toString("HH:mm:ss")
        self.log_view.appendPlainText(f"[{stamp}] {message}")

    def update_status(self, status: StatusModel) -> None:
        self.last_status = status
        self.state_label.setText(self.state_to_string(status.state))
        self.fault_label.setText(self.fault_to_string(status.fault_code))
        self.remaining_label.setText(f"{status.remaining_sec} s")
        self.heartbeat_label.setText("YES" if status.heartbeat_ok else "NO")
        self.mist_label.setText(
            f"{'online' if status.mist_online else 'offline'} / {self.level_to_string(status.mist_level)}"
            + (" / LINK_FAULT" if status.fault_code == 11 else "")
            + (" / LOW_WATER" if status.mist_low_water else "")
        )
        self.mist_fault_label.setText(
            f"{status.mist_fault_code} LOW_WATER" if status.mist_low_water else str(status.mist_fault_code)
        )
        self.mist_water_label.setText("LOW_WATER" if status.mist_low_water else "NORMAL")
        self.stainless_level_label.setText("YES" if status.liquid_present else "NO")
        self.cover_label.setText("YES" if status.cover_closed else "NO")
        if status.fault_code and status.fault_code != self.last_fault_code:
            stamp = QDateTime.currentDateTime().toString("HH:mm:ss")
            self.fault_history.insertItem(
                0,
                f"{stamp} {self.fault_to_string(status.fault_code)} state={self.state_to_string(status.state)}",
            )
            while self.fault_history.count() > 200:
                self.fault_history.takeItem(self.fault_history.count() - 1)
        self.last_fault_code = status.fault_code

    def update_config(self, config: ConfigModel) -> None:
        if self.config_dirty:
            if not self._config_skip_logged:
                self.append_log("rx config ignored: local config has unsaved edits")
                self._config_skip_logged = True
            return

        self._suppress_dirty_tracking = True
        self.mode_box.setCurrentIndex(self.mode_box.findData(config.mode))
        self.target_temp_box.setValue(config.target_temp_deci_c)
        self.duration_box.setValue(config.duration_sec // 60)
        self.air_level_box.setCurrentIndex(config.air_level)
        self.mist_level_box.setCurrentIndex(config.mist_level)
        self._suppress_dirty_tracking = False
        self.config_dirty = False
        self._config_skip_logged = False

    def update_pid(self, pid: PidModel) -> None:
        if self.pid_dirty:
            if not self._pid_skip_logged:
                self.append_log("rx pid ignored: local PID has unsaved edits")
                self._pid_skip_logged = True
            return

        self._suppress_dirty_tracking = True
        self.kp_box.setValue(pid.kp_milli / 1000.0)
        self.ki_box.setValue(pid.ki_milli / 1000.0)
        self.kd_box.setValue(pid.kd_milli / 1000.0)
        self.i_limit_box.setValue(pid.integral_limit_permille)
        self.kp_slider.setValue(int(round(pid.kp_milli / 10)))
        self.ki_slider.setValue(int(round(pid.ki_milli / 10)))
        self.kd_slider.setValue(int(round(pid.kd_milli / 10)))
        self.i_limit_slider.setValue(pid.integral_limit_permille)
        self._suppress_dirty_tracking = False
        self.pid_dirty = False
        self._pid_skip_logged = False
        self.append_log("pid window updated")

    def update_outlet_control(self, outlet: OutletControlModel) -> None:
        if self.outlet_dirty:
            self.append_log("rx outlet control ignored: local outlet loop has unsaved edits")
            return

        self._suppress_dirty_tracking = True
        self.outlet_base_box.setValue(outlet.base_offset_deci_c)
        self.outlet_margin_box.setValue(outlet.target_margin_deci_c)
        self.outlet_air_low_box.setValue(outlet.air_low_offset_deci_c)
        self.outlet_air_mid_box.setValue(outlet.air_mid_offset_deci_c)
        self.outlet_air_high_box.setValue(outlet.air_high_offset_deci_c)
        self.outlet_mist_low_box.setValue(outlet.mist_low_offset_deci_c)
        self.outlet_mist_mid_box.setValue(outlet.mist_mid_offset_deci_c)
        self.outlet_mist_high_box.setValue(outlet.mist_high_offset_deci_c)
        self._suppress_dirty_tracking = False
        self.outlet_dirty = False
        self.append_log("outlet loop window updated")

    def update_kettle_target(self, kettle_target: KettleTargetModel) -> None:
        self.kettle_target_override_box.setChecked(kettle_target.enabled)
        self.kettle_target_box.setValue(kettle_target.target_deci_c)
        self.append_log("kettle target window updated")

    def update_runtime(self, runtime: RuntimeModel) -> None:
        self.outlet_temp_label.setText(f"{runtime.ntc_deci_c[0] / 10.0:.1f} C")
        self.reserved_temp_label.setText(f"{runtime.ntc_deci_c[1] / 10.0:.1f} C")
        self.overtemp_temp_label.setText(f"{runtime.ntc_deci_c[2] / 10.0:.1f} C")
        if runtime.ntc_raw_max > 0:
            self.kettle_temp_label.setText(
                f"{runtime.ntc_deci_c[3] / 10.0:.1f} C  raw={runtime.ntc_raw[3]}/{runtime.ntc_raw_max}"
            )
        else:
            self.kettle_temp_label.setText(f"{runtime.ntc_deci_c[3] / 10.0:.1f} C")
        self.fan_label.setText(str(runtime.fan_rpm))
        self.mist_fault_label.setText(
            f"{runtime.mist_fault_code} LOW_WATER" if runtime.mist_low_water else str(runtime.mist_fault_code)
        )
        self.mist_water_label.setText("LOW_WATER" if runtime.mist_low_water else "NORMAL")
        self.stainless_level_label.setText("YES" if runtime.liquid_present else "NO")
        self.heat_label.setText(
            f"{'on' if runtime.heat_enabled else 'off'}, burst, {runtime.heat_output_permille} permille"
        )
        self.kettle_pid_temp_label.setText(f"{runtime.kettle_pid_temp_deci_c / 10.0:.1f} C")
        self.kettle_pid_target_label.setText(f"{runtime.kettle_pid_target_deci_c / 10.0:.1f} C")
        self.kettle_pid_error_label.setText(f"{runtime.kettle_pid_error_deci_c / 10.0:.1f} C")
        self.maintenance_label.setText(
            f"{'on' if runtime.maintenance_active else 'off'} fan={self.level_to_string(runtime.maintenance_fan_level)} "
            f"mist={self.level_to_string(runtime.maintenance_mist_level)} heat={runtime.maintenance_heat_permille}"
        )
        self.trend_widget.add_sample(
            runtime.ntc_deci_c[0] / 10.0,
            runtime.ntc_deci_c[3] / 10.0,
            runtime.target_temp_deci_c / 10.0,
            runtime.heat_output_permille / 10.0,
            runtime.heat_error_deci_c / 10.0,
            runtime.pid_i_term_raw / 10000.0,
            runtime.kettle_pid_target_deci_c / 10.0,
            runtime.kettle_pid_error_deci_c / 10.0,
        )
        self.trend_widget.set_pid_summary(
            runtime.pid_kp_milli / 1000.0,
            runtime.pid_ki_milli / 1000.0,
            runtime.pid_kd_milli / 1000.0,
            runtime.pid_integral_limit_permille,
        )

    def update_maintenance(self, maintenance: MaintenanceModel) -> None:
        self.manual_fan_box.setCurrentIndex(maintenance.fan_level)
        self.manual_mist_box.setCurrentIndex(maintenance.mist_level)
        self.manual_heat_box.setValue(maintenance.heat_output_permille)

    def update_connection_state(self, connected: bool) -> None:
        self.connection_state.setText("Connected" if connected else "Disconnected")
        self.connect_button.setText("Disconnect" if connected else "Connect")
        if not connected:
            self.last_status = None
            self.trend_widget.clear_samples()
            self.config_dirty = False
            self.pid_dirty = False
            self.outlet_dirty = False
            self._config_skip_logged = False
            self._pid_skip_logged = False

    def _mark_config_dirty(self) -> None:
        if not self._suppress_dirty_tracking:
            self.config_dirty = True
            self._config_skip_logged = False

    def _mark_pid_dirty(self) -> None:
        if not self._suppress_dirty_tracking:
            self.pid_dirty = True
            self._pid_skip_logged = False

    def _mark_outlet_dirty(self) -> None:
        if not self._suppress_dirty_tracking:
            self.outlet_dirty = True

    def handle_ack(self, command_id: int, ok: bool, error_code: int) -> None:
        command_name = self.command_to_string(command_id)
        if ok:
            if command_id == 0x34:  # SET_OUTLET_CONTROL
                self.append_log(f"{command_name} applied")
                self.client.request_outlet_control()
                self.client.request_runtime()
                return

            if command_id in {0x29, 0x32}:  # SET_PID / SET_KETTLE_PID
                self.append_log(f"{command_name} applied")
                self.client.request_kettle_pid()
                self.client.request_runtime()
                return

            if command_id == 0x36:  # SET_KETTLE_TARGET
                self.append_log(f"{command_name} applied")
                self.client.request_kettle_target()
                self.client.request_runtime()
                return

            if command_id in {
                0x2B,  # ENTER_MAINTENANCE
                0x2C,  # EXIT_MAINTENANCE
                0x2D,  # MANUAL_SET_FAN
                0x2E,  # MANUAL_SET_MIST
                0x2F,  # MANUAL_SET_HEAT
            }:
                self.append_log(f"{command_name} applied")
                self.client.request_maintenance()
                self.client.request_runtime()
                self.client.request_status()
            return

        reason = self.ack_error_to_string(command_id, error_code)
        self.append_log(f"{command_name} rejected: {reason}")

        if command_id == 0x2B:
            state_text = self.state_label.text()
            fault_text = self.fault_label.text()
            QMessageBox.warning(
                self,
                "Maintenance Rejected",
                "维护模式没有进入。\n\n"
                f"原因: {reason}\n"
                f"当前状态: {state_text}\n"
                f"当前故障: {fault_text}\n\n"
                "当前固件只允许在 READY / PAUSED / DONE 且无故障时进入维护模式。",
            )
        elif command_id in {0x2D, 0x2E, 0x2F}:
            QMessageBox.warning(
                self,
                "Manual Output Rejected",
                "手动输出命令未执行。\n\n"
                f"命令: {command_name}\n"
                f"原因: {reason}\n\n"
                "请先确认已经成功进入维护模式。",
            )

    @staticmethod
    def state_to_string(state: int) -> str:
        names = {
            0: "BOOT",
            1: "SELF_TEST",
            2: "READY",
            3: "CONFIGURING",
            4: "RUNNING_HOT",
            5: "RUNNING_COLD",
            6: "PAUSED",
            7: "DONE",
            8: "FAULT",
        }
        return names.get(state, "UNKNOWN")

    @staticmethod
    def level_to_string(level: int) -> str:
        names = {0: "OFF", 1: "LOW", 2: "MID", 3: "HIGH"}
        return names.get(level, "UNKNOWN")

    @staticmethod
    def fault_to_string(fault: int) -> str:
        names = {
            0: "0 NONE",
            1: "1 INIT_FAILED",
            2: "2 ADC_INIT_FAILED",
            3: "3 UART_INIT_FAILED",
            4: "4 PWM_INIT_FAILED",
            5: "5 TIMER_INIT_FAILED",
            6: "6 SENSOR_NTC_OPEN",
            7: "7 SENSOR_NTC_SHORT",
            8: "8 OVER_TEMP",
            9: "9 LIQUID_EMPTY",
            10: "10 COVER_OPEN",
            11: "11 MIST_BOARD_OFFLINE",
            12: "12 MIST_BOARD_FAULT",
            13: "13 HOST_HEARTBEAT_TIMEOUT",
            14: "14 PROTOCOL_ERROR",
            15: "15 STORAGE_ERROR",
            16: "16 OUTLET1_OVER_TEMP",
            17: "17 KETTLE_OVER_TEMP",
        }
        return names.get(fault, f"{fault} UNKNOWN")

    @staticmethod
    def command_to_string(command_id: int) -> str:
        names = {
            0x10: "SET_MODE",
            0x11: "SET_TARGET_TEMP",
            0x12: "SET_TIME",
            0x13: "SET_AIR_LEVEL",
            0x14: "SET_MIST_LEVEL",
            0x20: "START",
            0x21: "PAUSE",
            0x22: "RESUME",
            0x23: "STOP",
            0x24: "GET_STATUS",
            0x25: "HEARTBEAT",
            0x26: "CLEAR_FAULT",
            0x27: "GET_CONFIG",
            0x28: "GET_PID",
            0x29: "SET_PID",
            0x2A: "GET_RUNTIME",
            0x2B: "ENTER_MAINTENANCE",
            0x2C: "EXIT_MAINTENANCE",
            0x2D: "MANUAL_SET_FAN",
            0x2E: "MANUAL_SET_MIST",
            0x2F: "MANUAL_SET_HEAT",
            0x30: "GET_MAINTENANCE",
            0x31: "GET_KETTLE_PID",
            0x32: "SET_KETTLE_PID",
            0x33: "GET_OUTLET_CONTROL",
            0x34: "SET_OUTLET_CONTROL",
            0x35: "GET_KETTLE_TARGET",
            0x36: "SET_KETTLE_TARGET",
        }
        return names.get(command_id, f"CMD_0x{command_id:02X}")

    def ack_error_to_string(self, command_id: int, error_code: int) -> str:
        generic = {
            1: "invalid mode",
            2: "invalid target temperature",
            3: "invalid treatment time",
            4: "invalid air level",
            5: "invalid mist level",
            6: "invalid PID payload or PID rejected",
            7: "maintenance not allowed in current state or while fault is active",
            8: "manual fan command invalid or maintenance mode is inactive",
            9: "manual mist command invalid or maintenance mode is inactive",
            10: "manual heat command invalid or maintenance mode is inactive",
            11: "resume blocked while maintenance mode is active",
            12: "outlet loop payload invalid or parameter out of range",
            13: "kettle target payload invalid",
            0x40: "state machine rejected this command in the current state",
            0x42: "cover is open, start is blocked",
            0x43: "mist board reports low water, start is blocked",
            0x44: "mist board is offline, start is blocked",
            0x45: "mist board is safety locked, start is blocked",
            0x7F: "unknown command",
        }

        if command_id == 0x2B and error_code == 7 and self.last_status is not None:
            return (
                "maintenance mode requires READY / PAUSED / DONE and no active fault; "
                f"current state={self.state_to_string(self.last_status.state)}, "
                f"fault={self.fault_to_string(self.last_status.fault_code)}"
            )

        return generic.get(error_code, f"error {error_code}")


def main() -> int:
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())

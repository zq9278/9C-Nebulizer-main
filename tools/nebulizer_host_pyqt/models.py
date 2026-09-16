from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class StatusModel:
    state: int = 0
    remaining_sec: int = 0
    target_temp_deci_c: int = 0
    outlet_temp_deci_c: int = 0
    kettle_temp_deci_c: int = 0
    air_level: int = 0
    mist_level: int = 0
    liquid_present: bool = False
    cover_closed: bool = False
    fault_code: int = 0
    mist_online: bool = False
    mist_fault_code: int = 0
    mist_low_water: bool = False
    mist_running: bool = False
    heartbeat_ok: bool = False
    mode: int = 0
    keep_warm_enabled: bool = False


@dataclass
class ConfigModel:
    mode: int = 0
    target_temp_deci_c: int = 420
    duration_sec: int = 600
    air_level: int = 2
    mist_level: int = 2
    keep_warm_enabled: bool = False


@dataclass
class TreatmentEventModel:
    event_id: int = 0
    mode: int = 0
    state: int = 0
    keep_warm_enabled: bool = False
    remaining_sec: int = 0
    outlet_temp_deci_c: int = 0
    kettle_temp_deci_c: int = 0


@dataclass
class PidModel:
    kp_milli: int = 10000
    ki_milli: int = 1
    kd_milli: int = 0
    integral_limit_permille: int = 400
    min_delay_us: int = 0
    max_delay_us: int = 8000


@dataclass
class PreheatPidModel:
    kp_milli: int = 2500
    ki_milli: int = 0
    kd_milli: int = 0
    integral_limit_permille: int = 0
    target_temp_deci_c: int = 550
    output_max_permille: int = 800


@dataclass
class FanPidModel:
    kp_milli: int = 1000
    ki_milli: int = 0
    kd_milli: int = 0
    integral_limit_permille: int = 50
    max_boost_percent: int = 10
    low_base_percent: int = 30
    mid_base_percent: int = 35
    high_base_percent: int = 40


@dataclass
class RuntimeModel:
    state: int = 0
    remaining_sec: int = 0
    fault_code: int = 0
    heartbeat_ok: bool = False
    ntc_deci_c: list[int] = field(default_factory=lambda: [0, 0, 0, 0])
    ntc_raw: list[int] = field(default_factory=lambda: [0, 0, 0, 0])
    ntc_raw_max: int = 0
    liquid_present: bool = False
    cover_closed: bool = False
    gx1832_active: bool = False
    fan_rpm: int = 0
    mist_online: bool = False
    mist_running: bool = False
    mist_desired_level: int = 0
    mist_actual_level: int = 0
    mist_fault_code: int = 0
    mist_low_water: bool = False
    heat_enabled: bool = False
    heat_delay_us: int = 0
    heat_output_permille: int = 0
    heat_error_deci_c: int = 0
    measured_temp_deci_c: int = 0
    target_temp_deci_c: int = 0
    sample_uptime_ms: int = 0
    maintenance_active: bool = False
    maintenance_fan_level: int = 0
    maintenance_mist_level: int = 0
    maintenance_heat_permille: int = 0
    pid_kp_milli: int = 0
    pid_ki_milli: int = 0
    pid_kd_milli: int = 0
    pid_integral_limit_permille: int = 0
    pid_i_term_raw: int = 0
    fan_pid_enabled: bool = False
    fan_pid_saturated: bool = False
    fan_pid_base_percent: int = 0
    fan_pid_output_percent: int = 0
    fan_pid_boost_permille: int = 0
    fan_pid_error_deci_c: int = 0
    fan_pid_measured_temp_deci_c: int = 0
    fan_pid_target_temp_deci_c: int = 0
    fan_pid_i_term_permille: int = 0
    heat_control_phase: int = 0
    keep_warm_enabled: bool = False


@dataclass
class MaintenanceModel:
    active: bool = False
    fan_level: int = 0
    mist_level: int = 0
    heat_output_permille: int = 0

import type { Channel, Sample } from "@workbench/core";
export interface Snapshot {
  uptime_ms: number;
  sequence: number;
  owner: number;
  owner_alive: boolean;
  control: boolean;
  config: {
    mode: number;
    target_temp_deci_c: number;
    duration_sec: number;
    air_level: number;
    mist_level: number;
    keep_warm_enabled: number;
  };
  pids: Record<
    "heat" | "fan" | "preheat",
    {
      kp_milli: number;
      ki_milli: number;
      kd_milli: number;
      integral_limit_permille: number;
    }
  >;
  runtime: {
    state: number;
    remaining_sec: number;
    fault: number;
    heartbeat: number;
    temperatures: number[];
    adc: number[];
    sample_ms: number;
    phase: number;
    heat_output: number;
    fan_output: number;
    fan_base: number;
    fan_boost: number;
    fan_rpm: number;
    mist_online: number;
    mist_running: number;
    mist_fault: number;
    mist_actual: number;
    cover: number;
    liquid: number;
    gx1832: number;
    heat_error: number;
    heat_measured: number;
    heat_target: number;
    heat_integral: number;
    fan_error: number;
    fan_integral: number;
    fan_saturated: number;
  };
  maintenance: { active: number; fan: number; mist: number; heat: number };
}
export const project = {
  id: "nebulizer",
  name: "9C Nebulizer",
  subtitle: "雾化器调试工作台",
  chip: "STM32G070CBT6",
  version: "1.0.0",
};
export const states = [
  "启动",
  "自检",
  "就绪",
  "配置中",
  "热雾治疗",
  "冷雾治疗",
  "已暂停",
  "治疗完成",
  "故障",
  "预热中",
  "保温中",
];
export const phases = ["待机", "PB11 全功率", "PB11 出口 PID", "PA5 保温"];
export const faults = [
  "正常",
  "初始化失败",
  "ADC 初始化失败",
  "UART 初始化失败",
  "PWM 初始化失败",
  "定时器初始化失败",
  "NTC 断路",
  "NTC 短路",
  "过温",
  "缺水",
  "开盖",
  "雾化板离线",
  "雾化板故障",
  "主机心跳超时",
  "协议错误",
  "存储故障",
  "出口过温",
  "锅体过温",
];
export const channels: Channel[] = [
  { id: "outlet", label: "PB11 · 出口温度", unit: "°C", color: "#3478ed" },
  { id: "kettle", label: "PA5 · 锅体温度", unit: "°C", color: "#ed9c46" },
  { id: "legacy", label: "PB10 · 旧反馈", unit: "°C", color: "#a88bdd" },
  { id: "protection", label: "PB12 · 保护温度", unit: "°C", color: "#57b6bb" },
  { id: "target", label: "出口目标", unit: "°C", color: "#ed7081" },
  { id: "heat", label: "加热输出", unit: "%", color: "#e1af32" },
  { id: "fan", label: "风机输出", unit: "%", color: "#48a58e" },
  { id: "error", label: "出口误差", unit: "°C", color: "#8292a7" },
  { id: "fanError", label: "风机误差", unit: "°C", color: "#7187cb" },
  { id: "heatI", label: "加热积分", unit: "%", color: "#bb7c69" },
  { id: "fanI", label: "风机积分", unit: "%", color: "#7fa1aa" },
];
export function toSample(s: Snapshot): Sample {
  const r = s.runtime;
  return {
    time: s.uptime_ms / 1000,
    values: {
      outlet: r.temperatures[0] / 10,
      legacy: r.temperatures[1] / 10,
      protection: r.temperatures[2] / 10,
      kettle: r.temperatures[3] / 10,
      target: r.heat_target / 10,
      heat: r.heat_output / 10,
      fan: r.fan_output,
      error: r.heat_error / 10,
      fanError: r.fan_error / 10,
      heatI: r.heat_integral / 10000,
      fanI: r.fan_integral / 10,
    },
  };
}
export interface Params {
  mode: number;
  target_temp_deci_c: number;
  duration_min: number;
  air_level: number;
  mist_level: number;
  keep_warm_enabled: boolean;
  [key: string]: number | boolean;
}
export const defaultParams: Params = {
  mode: 0,
  target_temp_deci_c: 420,
  duration_min: 10,
  air_level: 2,
  mist_level: 2,
  keep_warm_enabled: false,
  kp: 9,
  ki: 0,
  kd: 0,
  i_limit_permille: 400,
  fan_kp: 20,
  fan_ki: 0,
  fan_kd: 0,
  fan_i_limit_permille: 50,
  preheat_kp: 1,
  preheat_ki: 0,
  preheat_kd: 0,
  preheat_i_limit_permille: 0,
};
export const pidGroups = [
  {
    id: "heat",
    title: "出口加热 PID",
    prefix: "",
    cmd: 0x29,
    max: [120, 10, 5, 1000],
    hint: "PB11 反馈 · PID 周期 2 秒 · 输出上限 20%",
  },
  {
    id: "fan",
    title: "风机 PID",
    prefix: "fan_",
    cmd: 0x32,
    max: [20, 5, 5, 100],
    hint: "PB11 超温增速 · 基础档位之外最多增加 10%",
  },
  {
    id: "preheat",
    title: "旧预热 PID",
    prefix: "preheat_",
    cmd: 0x34,
    max: [120, 10, 5, 1000],
    hint: "仅兼容保存，不参与当前加热控制",
  },
] as const;
export function paramsFrom(s: Snapshot): Params {
  const c = s.config;
  const p: Params = {
    ...defaultParams,
    mode: c.mode,
    target_temp_deci_c: c.target_temp_deci_c,
    duration_min: Math.floor(c.duration_sec / 60),
    air_level: c.air_level,
    mist_level: c.mist_level,
    keep_warm_enabled: !!c.keep_warm_enabled,
  };
  for (const g of pidGroups) {
    const pid = s.pids[g.id];
    ["kp", "ki", "kd"].forEach(
      (k, i) =>
        (p[g.prefix + k] =
          [pid.kp_milli, pid.ki_milli, pid.kd_milli][i] / 1000),
    );
    p[g.prefix + "i_limit_permille"] = pid.integral_limit_permille;
  }
  return p;
}
export interface Command {
  command: number;
  payload: number[];
}
export const cmd = (command: number, payload: number[] = []): Command => ({
  command,
  payload,
});
export const u16 = (value: number) => [value & 255, (value >> 8) & 255];
export function configCommands(p: Params) {
  return [
    cmd(0x15, [0]),
    cmd(0x10, [p.mode]),
    cmd(0x11, u16(p.target_temp_deci_c)),
    cmd(0x12, u16(p.duration_min)),
    cmd(0x13, [p.air_level]),
    cmd(0x14, [p.mist_level]),
    cmd(0x15, [Number(p.keep_warm_enabled)]),
  ];
}
export function pidCommand(group: (typeof pidGroups)[number], p: Params) {
  const buf = new ArrayBuffer(16),
    view = new DataView(buf);
  ["kp", "ki", "kd", "i_limit_permille"].forEach((k, i) => {
    const n = Number(p[group.prefix + k]);
    if (!Number.isFinite(n) || n < 0 || n > group.max[i])
      throw Error("PID 参数超出范围");
    view.setInt32(i * 4, Math.round(n * (i < 3 ? 1000 : 1)), true);
  });
  return cmd(group.cmd, Array.from(new Uint8Array(buf)));
}

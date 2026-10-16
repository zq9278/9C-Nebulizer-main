import { useState } from "react";
import {
  Play,
  Pause,
  Square,
  ShieldCheck,
  Unlock,
  LockKeyhole,
  Thermometer,
  Wind,
  Droplets,
  Activity,
} from "lucide-react";
import { useWorkbench, batch, canControl } from "../../core/store";
import { cmd, states, phases, faults } from "../../projects/nebulizer";
import { Panel, Badge, errorMessage } from "../../components/ui";
import { useParameters } from "../parameters/model";
import { Trends } from "../trends/Trends";
export function ControlBar() {
  const s = useWorkbench(),
    m = useParameters(),
    [error, setError] = useState("");
  const control = canControl(s);
  async function act(fn: () => Promise<unknown>) {
    setError("");
    try {
      await fn();
    } catch (e) {
      setError(errorMessage(e));
    }
  }
  return (
    <Panel
      title="设备控制"
      description="接管前保持观察，命令串行执行并等待设备应答"
      actions={
        <Badge tone={control ? "green" : "neutral"}>
          {control ? "控制权已获取" : "观察模式"}
        </Badge>
      }
    >
      <div className="control-row">
        <button
          className="primary"
          disabled={
            !s.connected ||
            s.commandBusy ||
            !!s.snapshot?.control ||
            !!s.snapshot?.owner
          }
          onClick={() => act(() => batch([cmd(0xe0)]))}
        >
          <Unlock size={15} />
          接管控制
        </button>
        <button
          disabled={!control}
          onClick={() => act(() => batch([cmd(0xe1)]))}
        >
          <LockKeyhole size={15} />
          释放控制
        </button>
        <i className="divider" />
        <button disabled={!control} onClick={() => act(m.start)}>
          <Play size={15} />
          开始治疗
        </button>
        <button
          disabled={!control}
          onClick={() => act(() => batch([cmd(0x21)]))}
        >
          <Pause size={15} />
          暂停
        </button>
        <button
          disabled={!control}
          onClick={() => act(() => batch([cmd(0x22)]))}
        >
          <Play size={15} />
          恢复
        </button>
        <button
          disabled={!control}
          onClick={() => act(() => batch([cmd(0x23)]))}
        >
          <Square size={14} />
          停止治疗
        </button>
        <button
          disabled={!control}
          onClick={() => act(() => batch([cmd(0x15, [0])]))}
        >
          关闭保温
        </button>
        <button
          disabled={!control}
          onClick={() => act(() => batch([cmd(0x26)]))}
        >
          <ShieldCheck size={15} />
          清除故障
        </button>
      </div>
      <div className="panel-note">
        {error ? (
          <span className="error-text">{error}</span>
        ) : s.warm !== null ? (
          `本次 PC 会话保温剩余 ${Math.floor(s.warm / 60)} 分 ${s.warm % 60} 秒`
        ) : (
          "热雾开启保温时，“停止治疗”会返回保温；结束加热请使用“关闭保温”。PC 保温计时两小时，断开后不继续。"
        )}
      </div>
    </Panel>
  );
}
export function Monitor() {
  const s = useWorkbench(),
    r = s.snapshot?.runtime;
  const statCards = [
    {
      title: "出口温度",
      value: r ? (r.temperatures[0] / 10).toFixed(1) : "—",
      unit: "°C",
      pin: "PB11",
      icon: Thermometer,
      color: "blue",
    },
    {
      title: "锅体温度",
      value: r ? (r.temperatures[3] / 10).toFixed(1) : "—",
      unit: "°C",
      pin: "PA5",
      icon: Thermometer,
      color: "orange",
    },
    {
      title: "加热输出",
      value: r ? (r.heat_output / 10).toFixed(1) : "—",
      unit: "%",
      pin: "出口 PID",
      icon: Activity,
      color: "purple",
    },
    {
      title: "风机输出",
      value: r ? String(r.fan_output) : "—",
      unit: "%",
      pin: r ? `基础 ${r.fan_base}% + ${r.fan_boost / 10}%` : "风机 PID",
      icon: Wind,
      color: "green",
    },
  ];
  return (
    <div className="page-stack">
      <div className="stat-grid">
        {statCards.map((c) => (
          <div className={"stat-card " + c.color} key={c.title}>
            <div className="stat-label">
              {c.title}
              <c.icon size={17} />
            </div>
            <div className="stat-value">
              {c.value}
              <small>{c.unit}</small>
            </div>
            <div className="stat-pin">
              {c.pin}
              <span>
                {s.connected ? "实时采样" : r ? "最后一次采样" : "等待连接"}
              </span>
            </div>
          </div>
        ))}
      </div>
      <div className="overview-grid">
        <Trends compact />
        <Panel
          title="运行状态"
          description="设备实际反馈"
          actions={<Activity size={16} color="#8698ab" />}
        >
          <div className="state-hero">
            <span className={"state-dot " + (s.connected ? "on" : "")} />
            <div>
              <strong>
                {r ? states[r.state] || `未知状态 ${r.state}` : "等待连接"}
              </strong>
              <p>
                {r ? phases[r.phase] || "未知阶段" : "连接设备后读取运行状态"}
              </p>
            </div>
          </div>
          <div className="countdown">
            <span>剩余治疗时间</span>
            <strong>
              {r
                ? `${Math.floor(r.remaining_sec / 60)
                    .toString()
                    .padStart(
                      2,
                      "0",
                    )}:${(r.remaining_sec % 60).toString().padStart(2, "0")}`
                : "— : —"}
            </strong>
          </div>
          <dl className="status-list">
            <dt>设备故障</dt>
            <dd className={r?.fault ? "error-text" : ""}>
              {r ? faults[r.fault] || `故障 ${r.fault}` : "—"}
            </dd>
            <dt>控制端心跳</dt>
            <dd>{r ? (r.heartbeat ? "正常" : "超时") : "—"}</dd>
            <dt>雾化板</dt>
            <dd>
              {r
                ? (r.mist_online ? "在线" : "离线") +
                  " / " +
                  (r.mist_running ? "运行" : "停止")
                : "—"}
            </dd>
            <dt>雾化档位 / 故障</dt>
            <dd>{r ? `${r.mist_actual} / ${r.mist_fault}` : "—"}</dd>
            <dt>磁铁盖</dt>
            <dd>{r ? (r.cover ? "关闭" : "打开") : "—"}</dd>
            <dt>雾化板缺水</dt>
            <dd>{r ? (r.mist_fault === 9 ? "缺水" : "未报告缺水") : "—"}</dd>
            <dt>GX1832</dt>
            <dd>{r ? (r.gx1832 ? "有效" : "无效") : "—"}</dd>
            <dt>锅体液位</dt>
            <dd>{r ? (r.liquid ? "有水" : "无水") : "—"}</dd>
          </dl>
          <p className="panel-note">锅体液位联锁未启用 · 风机 RPM 未实现采集</p>
        </Panel>
      </div>
      <ControlBar />
      <Panel
        title="传感器与 PID 诊断"
        description="PB10 为旧反馈通道，PB12 为保护通道"
      >
        <div className="diagnostic-grid">
          {[
            ["PB10 温度", r ? `${r.temperatures[1] / 10} °C` : "—"],
            ["PB12 温度", r ? `${r.temperatures[2] / 10} °C` : "—"],
            ["ADC 原始值", r ? r.adc.join(" / ") : "—"],
            ["出口误差", r ? `${r.heat_error / 10} °C` : "—"],
            ["出口积分", r ? `${r.heat_integral / 10000} %` : "—"],
            [
              "风机积分",
              r
                ? `${r.fan_integral / 10} %${r.fan_saturated ? " · 饱和" : ""}`
                : "—",
            ],
          ].map(([a, b]) => (
            <div key={a}>
              <span>{a}</span>
              <strong>{b}</strong>
            </div>
          ))}
        </div>
      </Panel>
    </div>
  );
}

import { useEffect, useState } from "react";
import {
  Activity,
  SlidersHorizontal,
  Wrench,
  ChartNoAxesCombined,
  Upload,
  Layers,
  ScrollText,
  Box,
  ChevronRight,
  Radio,
  PanelLeftClose,
} from "lucide-react";
import { project } from "./projects/nebulizer";
import { init, useWorkbench } from "./core/store";
import {
  ConnectionPanel,
  useConnectionOptions,
} from "./modules/connection/ConnectionPanel";
import { Monitor } from "./modules/monitor/Monitor";
import { Parameters } from "./modules/parameters/Parameters";
import { ParametersProvider } from "./modules/parameters/model";
import { Maintenance } from "./modules/maintenance/Maintenance";
import { Trends } from "./modules/trends/Trends";
import { Installation } from "./modules/installation/Installation";
import { Logs } from "./modules/logs/Logs";
import { Panel, Badge } from "./components/ui";
const modules = [
  { id: "monitor", label: "运行监控", en: "Monitor", icon: Activity },
  {
    id: "parameters",
    label: "参数调试",
    en: "Parameters",
    icon: SlidersHorizontal,
  },
  { id: "maintenance", label: "维护控制", en: "Maintenance", icon: Wrench },
  {
    id: "trends",
    label: "曲线与采集",
    en: "Time series",
    icon: ChartNoAxesCombined,
  },
  { id: "update", label: "串口升级", en: "App update", icon: Upload },
  { id: "factory", label: "工厂烧录", en: "Factory flash", icon: Layers },
  { id: "logs", label: "事件与日志", en: "Event logs", icon: ScrollText },
  { id: "about", label: "项目与模块", en: "Workspace", icon: Box },
];
export default function App() {
  const s = useWorkbench(),
    [page, setPage] = useState("monitor"),
    [options, setOptions] = useConnectionOptions();
  useEffect(init, []);
  const current = modules.find((m) => m.id === page)!;
  return (
    <ParametersProvider>
      <div className="app-shell">
        <aside className="sidebar">
          <div className="brand">
            <span className="brand-mark">
              n<span>·</span>
            </span>
            <div>
              <strong>WORKBENCH</strong>
              <small>设备调试工作台</small>
            </div>
          </div>
          <div className="workspace-card">
            <span className="workspace-icon">
              <Box size={18} />
            </span>
            <div>
              <strong>{project.name}</strong>
              <small>{project.chip}</small>
            </div>
            <ChevronRight size={14} />
          </div>
          <div className="nav-caption">
            工作空间 <span>WORKSPACE</span>
          </div>
          <nav>
            {modules.map((m) => (
              <button
                key={m.id}
                data-testid={"nav-" + m.id}
                className={page === m.id ? "active" : ""}
                onClick={() => setPage(m.id)}
              >
                <m.icon size={18} />
                <span>{m.label}</span>
                {m.id === "logs" && s.events.length > 0 && (
                  <small>{s.events.length}</small>
                )}
                {page === m.id && <i />}
              </button>
            ))}
          </nav>
          <div className="sidebar-footer">
            <span className="status-orb" />
            本地硬件服务{" "}
            <Badge tone={s.ready ? "green" : "neutral"}>
              {s.ready ? "就绪" : "未就绪"}
            </Badge>
            <p>
              Electron · 模块化工作台
              <br />v{project.version}
            </p>
          </div>
        </aside>
        <div className="main-shell">
          <header className="topbar">
            <div className="breadcrumb">
              <span>{project.name}</span>
              <ChevronRight size={13} />
              <strong>{current.label}</strong>
            </div>
            <div className="topbar-status">
              <span
                className={"connection-dot " + (s.connected ? "connected" : "")}
              />
              {s.connected
                ? s.snapshot?.control
                  ? "设备控制中"
                  : "已连接 · 观察模式"
                : "设备未连接"}
              <i />
              <span className="project-chip">{project.chip}</span>
            </div>
          </header>
          <main>
            <div className="page-heading">
              <div>
                <div className="eyebrow">{current.en.toUpperCase()}</div>
                <h1>{current.label}</h1>
                <p>
                  {page === "monitor"
                    ? "查看设备实时状态、传感器数据与控制反馈"
                    : "独立功能模块 · " + project.subtitle}
                </p>
              </div>
              <div className="heading-meta">
                <Radio size={15} />
                {s.snapshot
                  ? `设备运行 ${(s.snapshot.uptime_ms / 1000).toFixed(1)} s`
                  : "等待设备数据"}
                {s.snapshot && !s.connected && <Badge>历史采样</Badge>}
              </div>
            </div>
            {s.error && <div className="error-banner">{s.error}</div>}
            <ConnectionPanel options={options} onChange={setOptions} />
            <div className="page-content">
              {page === "monitor" && <Monitor />}
              {page === "parameters" && <Parameters />}
              {page === "maintenance" && <Maintenance />}
              {page === "trends" && <Trends />}
              {page === "factory" && (
                <Installation
                  key="factory"
                  kind="factory"
                  options={options}
                  onElf={(elf) => setOptions({ ...options, elf })}
                />
              )}{" "}
              {page === "update" && (
                <Installation
                  key="update"
                  kind="update"
                  options={options}
                  onElf={() => {}}
                />
              )}
              {page === "logs" && <Logs />}
              {page === "about" && (
                <Panel
                  title="项目与可复用模块"
                  description="业务协议位于项目适配层，通用模块不依赖 STM32 或雾化器字段"
                >
                  <div className="module-map">
                    {[
                      [
                        "@workbench/core",
                        "采样环形缓冲、通道定义、CSV 与桌面 API 契约",
                      ],
                      [
                        "@workbench/charts",
                        "通用 Canvas 时序图：缩放、平移、多单位轴、尖峰保留",
                      ],
                      ["modules / connection", "连接配置与探针 / 串口选择"],
                      [
                        "modules / parameters",
                        "独立编辑区、JSON 参数、批次应答与设备读回",
                      ],
                      [
                        "modules / installation",
                        "镜像检查、确认、进度、取消与恢复提示",
                      ],
                      ["modules / logs", "事件筛选、搜索与 CSV 导出"],
                      [
                        "projects / nebulizer",
                        "9C 设备模型、命令构建、单位换算和通道映射",
                      ],
                      [
                        "backend / server.py",
                        "无 Qt 的硬件服务：SWD、串口、升级、烧录",
                      ],
                    ].map(([a, b]) => (
                      <div key={a}>
                        <code>{a}</code>
                        <p>{b}</p>
                      </div>
                    ))}
                  </div>
                  <p className="panel-note">
                    界面、构建及模拟测试不替代实机验收。工厂烧录覆盖
                    Boot，串口升级仅更新
                    App。曲线组件只消费通道与时序采样，可独立用于其他项目。
                  </p>
                </Panel>
              )}
            </div>
            <footer className="main-footer">
              <span>
                9C NEBULIZER · {s.mode === "serial" ? "USART1" : "SWD"}{" "}
                WORKSPACE
              </span>
              <span>数据以设备实际反馈为准</span>
            </footer>
          </main>
        </div>
      </div>
    </ParametersProvider>
  );
}

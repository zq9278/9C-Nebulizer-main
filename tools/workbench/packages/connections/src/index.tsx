import { useState } from "react";
import { Cable, RefreshCw, FolderOpen, PlugZap, Unplug } from "lucide-react";
import type { WorkbenchAPI } from "@workbench/core";
import { Panel, Field, Badge, errorMessage } from "@workbench/ui";
export interface ConnectionOptions {
  mode: "swd" | "serial";
  backend: "daplink" | "jlink";
  serial: string;
  device: string;
  speed: number;
  elf: string;
  dll: string;
  port: string;
}
const defaults: ConnectionOptions = {
  mode: "swd",
  backend: "daplink",
  serial: "",
  device: "",
  speed: 1000,
  elf: "",
  dll: "",
  port: "",
};
export function useConnectionOptions(
  projectId: string,
  initial: Partial<ConnectionOptions> = {},
) {
  const key = projectId + "/connection";
  const [value, setValue] = useState<ConnectionOptions>(() => {
    try {
      return {
        ...defaults,
        ...initial,
        ...JSON.parse(localStorage.getItem(key) || "{}"),
      };
    } catch {
      return { ...defaults, ...initial };
    }
  });
  return [
    value,
    (next: ConnectionOptions) => {
      setValue(next);
      localStorage.setItem(key, JSON.stringify(next));
    },
  ] as const;
}
export interface ConnectionPanelProps {
  options: ConnectionOptions;
  onChange: (v: ConnectionOptions) => void;
  state: {
    connected: boolean;
    connecting: boolean;
    ready: boolean;
    job: { running: boolean } | null;
  };
  api: Pick<WorkbenchAPI, "request" | "openFile">;
  onConnect: (options: ConnectionOptions) => Promise<unknown>;
  notes?: { swd: string; serial: string };
}
export function ConnectionPanel({
  options,
  onChange,
  state,
  api,
  onConnect,
  notes,
}: ConnectionPanelProps) {
  const request = api.request;
  const [probes, setProbes] = useState<string[]>([]),
    [ports, setPorts] = useState<string[]>([]),
    [scanning, setScanning] = useState(false),
    [error, setError] = useState("");
  const disabled =
    state.connected || state.connecting || !!state.job?.running || scanning;
  const set = (key: keyof ConnectionOptions, v: string | number) =>
    onChange({ ...options, [key]: v });
  async function scan() {
    setScanning(true);
    setError("");
    try {
      if (options.mode === "serial") {
        const found = await request<string[]>("ports");
        setPorts(found);
        if (found.length) set("port", found[0]);
      } else {
        const found = await request<string[]>("probes", {
          backend: options.backend,
          dll: options.dll,
        });
        setProbes(found);
        if (found.length) set("serial", found[0]);
        if (!found.length) setError("未检测到探针，请检查 USB 连接和驱动");
      }
    } catch (e) {
      setError(errorMessage(e));
    } finally {
      setScanning(false);
    }
  }
  async function choose(kind: "elf" | "dll") {
    const path = await api.openFile(kind);
    if (path) set(kind, path);
  }
  async function toggle() {
    setError("");
    try {
      if (state.connected) await request("disconnect");
      else {
        if (options.mode === "swd" && (!options.serial || !options.elf))
          throw Error("请选择探针和与设备匹配的 App ELF");
        if (options.mode === "serial" && !options.port)
          throw Error("请选择串口");
        await onConnect({ ...options });
      }
    } catch (e) {
      setError(errorMessage(e));
    }
  }
  return (
    <Panel
      title="设备连接"
      description="独占通信通道 · 默认观察模式"
      actions={
        <Badge tone={state.connected ? "green" : "neutral"}>
          {state.connected ? "已连接" : "未连接"}
        </Badge>
      }
    >
      <div className="connection-tabs">
        <button
          className={options.mode === "swd" ? "active" : ""}
          disabled={disabled}
          onClick={() => set("mode", "swd")}
        >
          <PlugZap size={14} /> SWD 调试
        </button>
        <button
          className={options.mode === "serial" ? "active" : ""}
          disabled={disabled}
          onClick={() => set("mode", "serial")}
        >
          <Cable size={14} /> 串口
        </button>
      </div>
      <div className="connection-grid">
        {options.mode === "swd" ? (
          <>
            <Field label="探针类型">
              <select
                disabled={disabled}
                value={options.backend}
                onChange={(e) => {
                  onChange({
                    ...options,
                    backend: e.target.value as "daplink" | "jlink",
                    serial: "",
                  });
                  setProbes([]);
                }}
              >
                <option value="daplink">DAPLink / CMSIS-DAP</option>
                <option value="jlink">SEGGER J-Link</option>
              </select>
            </Field>
            <Field label="探针序列号">
              <input
                list="probes"
                value={options.serial}
                disabled={disabled}
                placeholder="刷新探针或输入完整 ID"
                onChange={(e) => set("serial", e.target.value)}
              />
              <datalist id="probes">
                {probes.map((p) => (
                  <option key={p}>{p}</option>
                ))}
              </datalist>
            </Field>
            <Field label="SWD 速度 / kHz">
              <select
                disabled={disabled}
                value={options.speed}
                onChange={(e) => set("speed", Number(e.target.value))}
              >
                {[100, 400, 1000, 2000, 4000].map((n) => (
                  <option key={n}>{n}</option>
                ))}
              </select>
            </Field>
          </>
        ) : (
          <>
            <Field label="通信端口">
              <input
                list="ports"
                value={options.port}
                disabled={disabled}
                placeholder="COM3"
                onChange={(e) => set("port", e.target.value)}
              />
              <datalist id="ports">
                {ports.map((p) => (
                  <option key={p}>{p}</option>
                ))}
              </datalist>
            </Field>
            <Field label="波特率">
              <input value="115200 · 8N1" disabled readOnly />
            </Field>
          </>
        )}
        <div className="connection-buttons">
          <button disabled={disabled || !state.ready} onClick={scan}>
            <RefreshCw size={14} className={scanning ? "spin" : ""} />
            刷新{options.mode === "swd" ? "探针" : "串口"}
          </button>
          <button
            className={state.connected ? "" : "primary"}
            disabled={
              state.connecting ||
              scanning ||
              !!state.job?.running ||
              !state.ready
            }
            onClick={toggle}
          >
            {state.connected ? <Unplug size={15} /> : <PlugZap size={15} />}{" "}
            {state.connecting
              ? "连接校验中…"
              : state.connected
                ? "断开连接"
                : "连接设备"}
          </button>
        </div>
      </div>
      {options.mode === "swd" && (
        <>
          <div className="path-field">
            <label>App ELF</label>
            <input
              value={options.elf}
              disabled={disabled}
              placeholder="选择与当前设备固件匹配的 App ELF"
              onChange={(e) => set("elf", e.target.value)}
            />
            <button disabled={disabled} onClick={() => choose("elf")}>
              <FolderOpen size={14} />
              选择 ELF
            </button>
          </div>
          {options.backend === "jlink" && (
            <div className="path-field">
              <label>J-Link DLL</label>
              <input
                value={options.dll}
                disabled={disabled}
                placeholder="留空自动查找"
                onChange={(e) => set("dll", e.target.value)}
              />
              <button disabled={disabled} onClick={() => choose("dll")}>
                选择 DLL
              </button>
            </div>
          )}
        </>
      )}
      <div className="panel-note">
        {error ? (
          <span className="error-text">{error}</span>
        ) : options.mode === "swd" ? (
          notes?.swd || "SWD 连接，按项目适配器执行固件校验。"
        ) : (
          notes?.serial || "串口独占连接，请关闭其他占用串口的程序。"
        )}
      </div>
    </Panel>
  );
}

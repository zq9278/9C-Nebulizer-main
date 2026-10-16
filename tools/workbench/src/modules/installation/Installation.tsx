import { useState } from "react";
import {
  FolderOpen,
  ShieldCheck,
  Upload,
  CheckCircle2,
  TriangleAlert,
} from "lucide-react";
import { api, useWorkbench, request, log } from "../../core/store";
import { Panel, Field, Badge, Alert, errorMessage } from "../../components/ui";
import type { ConnectionOptions } from "../connection/ConnectionPanel";
export function Installation({
  kind,
  options,
  onElf,
}: {
  kind: "factory" | "update";
  options: ConnectionOptions;
  onElf: (elf: string) => void;
}) {
  const s = useWorkbench(),
    factory = kind === "factory";
  const [path, setPath] = useState(""),
    [port, setPort] = useState(options.port),
    [ports, setPorts] = useState<string[]>([]),
    [info, setInfo] = useState<any>(null),
    [confirmed, setConfirmed] = useState(false),
    [run, setRun] = useState(!factory),
    [working, setWorking] = useState(false),
    [result, setResult] = useState(""),
    [error, setError] = useState("");
  const busy = working || !!s.job?.running,
    disabled = busy || s.connected || s.connecting;
  async function inspect() {
    setError("");
    setInfo(null);
    try {
      if (!path) throw Error("请选择文件");
      const value = await request(
        factory ? "inspect-factory" : "inspect-update",
        { path },
      );
      setInfo(value);
    } catch (e) {
      setError(errorMessage(e));
    }
  }
  async function choose() {
    const p = await api.openFile(factory ? "hex" : "nfu");
    if (p) {
      setPath(p);
      setInfo(null);
      setConfirmed(false);
      setResult("");
    }
  }
  async function install() {
    setError("");
    setResult("");
    setWorking(true);
    try {
      if (!confirmed) throw Error("请先确认实际输出已停止及安装范围");
      if (factory && !options.serial) throw Error("请在连接面板刷新并选择探针");
      if (!factory && !port) throw Error("请选择上位机通信串口");
      const value = await request<{ message: string; elf?: string | null }>(
        kind,
        { ...options, path, port, confirmed, run },
      );
      setResult(value.message);
      log(value.message);
      if (value.elf) onElf(value.elf);
    } catch (e) {
      setError(errorMessage(e));
    } finally {
      setWorking(false);
      setConfirmed(false);
    }
  }
  return (
    <div className="page-stack">
      <Alert>
        {factory
          ? "工厂烧录覆盖 Boot + App + 有效记录。烧录会暂停 CPU，EEPROM 参数保留；不要选择独立 App HEX。"
          : "串口升级覆盖唯一 App，保留 Boot。无旧版本自动回滚，中断后需重新发送完整升级包。"}
      </Alert>
      {s.connected && (
        <div className="error-banner">
          请先断开设备连接并释放控制权，再执行安装。
        </div>
      )}
      <div className="installation-grid">
        <Panel
          title={factory ? "工厂 HEX 烧录" : "App 串口升级"}
          description={
            factory
              ? "完整镜像 · 128 KiB · SWD"
              : "USART1 · PB6 TX / PB7 RX · 115200 / 8N1"
          }
          actions={<Badge>{factory ? "FACTORY IMAGE" : "APP PACKAGE"}</Badge>}
        >
          {factory ? (
            <div className="target-summary">
              <strong>STM32G070CBT6</strong>
              <span>
                {options.backend === "daplink"
                  ? "DAPLink / CMSIS-DAP"
                  : "J-Link"}{" "}
                · {options.serial || "未选择探针"} · {options.speed} kHz
              </span>
            </div>
          ) : (
            <div className="form-grid">
              <Field label="升级串口">
                <input
                  list="upgrade-ports"
                  value={port}
                  disabled={disabled}
                  placeholder="COM3"
                  onChange={(e) => setPort(e.target.value)}
                />
                <datalist id="upgrade-ports">
                  {ports.map((p) => (
                    <option key={p}>{p}</option>
                  ))}
                </datalist>
              </Field>
              <div className="align-end">
                <button
                  disabled={disabled}
                  onClick={async () => {
                    try {
                      const found = await request<string[]>("ports");
                      setPorts(found);
                      if (found.length) setPort(found[0]);
                    } catch (e) {
                      setError(errorMessage(e));
                    }
                  }}
                >
                  刷新串口
                </button>
              </div>
            </div>
          )}
          <Field label={factory ? "完整工厂 HEX" : "App 升级包 / .nfu"}>
            <div className="input-group">
              <input
                disabled={disabled}
                value={path}
                placeholder={
                  factory ? "nebulizer_factory.hex" : "nebulizer.nfu"
                }
                onChange={(e) => {
                  setPath(e.target.value);
                  setInfo(null);
                  setConfirmed(false);
                }}
              />
              <button disabled={disabled} onClick={choose}>
                <FolderOpen size={15} />
                选择文件
              </button>
            </div>
          </Field>
          <button
            className="inspect-button"
            disabled={disabled || !path}
            onClick={inspect}
          >
            <ShieldCheck size={15} />
            检查镜像
          </button>
          {info && (
            <div className="image-info">
              <CheckCircle2 size={17} />
              <div>
                <strong>镜像格式与完整性检查通过</strong>
                <p>
                  App {Number(info.appLength || info.length).toLocaleString()}{" "}
                  字节
                </p>
                <code>
                  {info.sha256
                    ? "SHA256: " + info.sha256
                    : "CRC32: " + info.crc}
                </code>
              </div>
            </div>
          )}
          <div className="confirmation-box">
            <label className="check-row">
              <input
                type="checkbox"
                checked={confirmed}
                disabled={disabled}
                onChange={(e) => setConfirmed(e.target.checked)}
              />
              已停止实际治疗、加热和雾化，确认
              {factory ? "覆盖 Boot 和 App" : "覆盖 App"}
            </label>
            <label className="check-row">
              <input
                type="checkbox"
                checked={run}
                disabled={disabled}
                onChange={(e) => setRun(e.target.checked)}
              />
              校验成功后复位运行（恢复原 EEPROM 配置）
            </label>
          </div>
          <div className="progress-track">
            <i style={{ width: `${busy ? s.progress : result ? 100 : 0}%` }} />
          </div>
          <div className="progress-caption">
            <span>
              {busy
                ? "正在处理，请保持连接"
                : result
                  ? "操作已完成"
                  : "等待安装"}
            </span>
            <strong>{busy ? s.progress : result ? 100 : 0}%</strong>
          </div>
          <footer className="form-footer">
            {!factory && (
              <button
                disabled={!busy}
                onClick={() =>
                  request("cancel").catch((e) => setError(errorMessage(e)))
                }
              >
                取消传输
              </button>
            )}
            <button
              className="primary"
              disabled={disabled || !confirmed || !path}
              onClick={install}
            >
              <Upload size={15} />
              {busy ? "处理中…" : factory ? "烧录工厂镜像" : "开始升级"}
            </button>
          </footer>
          {error && <div className="error-banner">{error}</div>}
          {result && <div className="success-banner">{result}</div>}
        </Panel>
        <div className="page-stack">
          <Panel title="安装流程" description="每个阶段均等待设备实际结果">
            <ol className="step-list">
              {(factory
                ? [
                    "检查完整 HEX 与有效记录",
                    "连接专用芯片 Flash 算法",
                    "检查容量与保护状态",
                    "擦写 Boot + App + 记录",
                    "完整 128 KiB 读回校验",
                    "按选择复位或保持暂停",
                  ]
                : [
                    "校验升级包及硬件标识",
                    "切换 Boot 并完成 INFO 握手",
                    "使旧 App 失效并擦除",
                    "分块写入并等待应答",
                    "验证实际 Flash CRC32",
                    "提交记录并确认 App 启动",
                  ]
              ).map((step, i) => (
                <li key={step}>
                  <span>{String(i + 1).padStart(2, "0")}</span>
                  {step}
                </li>
              ))}
            </ol>
          </Panel>
          <Panel title="连接与恢复" description="安装作业与调试会话互斥">
            <div className="help-copy">
              {factory ? (
                <>
                  <p>DAPLink 首次烧录需要安装 STM32G070 算法：</p>
                  <code>python -m pyocd pack install stm32g070cbtx</code>
                  <p>
                    默认保持 CPU 暂停，需手动复位。烧录成功后，同目录匹配的 App
                    ELF 会自动填入连接面板。
                  </p>
                  <p>不自动解除读写保护，不修改 Option Bytes。</p>
                </>
              ) : (
                <>
                  <p>旧单体固件没有 Boot，首次需使用工厂 HEX。</p>
                  <p>
                    先退出维护、治疗和保温，并释放调试控制。不要同时连接串口屏或其他上位机。
                  </p>
                  <p>
                    中断后设备留在
                    Boot，可重新完整升级；结果未知时先检查设备状态，不盲目重复命令。
                  </p>
                </>
              )}
            </div>
          </Panel>
        </div>
      </div>
    </div>
  );
}

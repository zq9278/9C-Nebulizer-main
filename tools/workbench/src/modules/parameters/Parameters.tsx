import { useState } from "react";
import { Download, Upload, RefreshCw, Check } from "lucide-react";
import { api, useWorkbench, canControl, log } from "../../core/store";
import {
  Panel,
  Field,
  NumberInput,
  Badge,
  errorMessage,
} from "../../components/ui";
import { pidGroups } from "../../projects/nebulizer";
import { useParameters } from "./model";
export function Parameters() {
  const s = useWorkbench(),
    m = useParameters();
  const [error, setError] = useState("");
  async function act(fn: () => Promise<unknown>) {
    setError("");
    try {
      await fn();
    } catch (e) {
      setError(errorMessage(e));
    }
  }
  async function importFile() {
    const path = await api.openFile("json");
    if (path) {
      await m.importParams(JSON.parse(await api.readFile(path)));
      log("参数导入编辑区，尚未发送到设备");
    }
  }
  const disabled = s.commandBusy,
    controlled = canControl(s);
  return (
    <div className="page-stack">
      <Panel
        title="参数工作区"
        description="编辑值与实时快照分开保存，采样不会覆盖未应用的修改"
        actions={
          <>
            <button
              disabled={!s.connected || disabled}
              onClick={() => act(() => m.read())}
            >
              <RefreshCw size={14} />
              读取全部
            </button>
            <button disabled={disabled} onClick={() => act(importFile)}>
              <Upload size={14} />
              导入 JSON
            </button>
            <button
              onClick={() =>
                act(() =>
                  api.saveFile(
                    "nebulizer_params.json",
                    JSON.stringify(m.params, null, 2),
                  ),
                )
              }
            >
              <Download size={14} />
              导出 JSON
            </button>
          </>
        }
      >
        <div className="parameter-status">
          <Badge tone={m.dirty.length ? "amber" : "green"}>
            {m.dirty.length ? "待应用：" + m.dirty.join(" / ") : "编辑区已同步"}
          </Badge>
          <span>应用成功后读取实际值 · EEPROM 由固件延迟保存</span>
        </div>
        {error && <div className="error-text">{error}</div>}
      </Panel>
      <div className="parameter-grid">
        <Panel
          title="治疗参数"
          description="模式、温度、时长和输出档位"
          actions={
            <Badge tone={m.dirty.includes("config") ? "amber" : "neutral"}>
              {m.dirty.includes("config") ? "已修改" : "CONFIG"}
            </Badge>
          }
        >
          <div className="form-grid">
            <Field label="治疗模式">
              <select
                disabled={disabled}
                value={m.params.mode}
                onChange={(e) => m.edit("mode", Number(e.target.value))}
              >
                <option value={0}>热雾</option>
                <option value={1}>冷雾</option>
              </select>
            </Field>
            <Field label="目标温度 / °C">
              <NumberInput
                disabled={disabled || m.params.mode === 1}
                value={m.params.target_temp_deci_c / 10}
                min={0}
                max={45}
                step={0.1}
                onChange={(v) =>
                  m.edit("target_temp_deci_c", Math.round(v * 10))
                }
              />
            </Field>
            <Field label="治疗时长 / 分钟">
              <NumberInput
                disabled={disabled}
                value={m.params.duration_min}
                min={1}
                max={60}
                onChange={(v) => m.edit("duration_min", v)}
              />
            </Field>
            {(["air_level", "mist_level"] as const).map((k) => (
              <Field
                key={k}
                label={k === "air_level" ? "风量档位" : "雾量档位"}
              >
                <select
                  disabled={disabled}
                  value={m.params[k]}
                  onChange={(e) => m.edit(k, Number(e.target.value))}
                >
                  {["关闭", "低档", "中档", "高档"].map((v, i) => (
                    <option key={i} value={i}>
                      {v}
                    </option>
                  ))}
                </select>
              </Field>
            ))}
          </div>
          <label className="check-row">
            <input
              type="checkbox"
              disabled={disabled || m.params.mode === 1}
              checked={m.params.keep_warm_enabled}
              onChange={(e) => m.edit("keep_warm_enabled", e.target.checked)}
            />
            持续保温 · PA5 65°C
          </label>
          <footer className="form-footer">
            <button
              disabled={!s.connected || disabled}
              onClick={() => act(() => m.read("config"))}
            >
              读取设备参数
            </button>
            <button
              className="primary"
              disabled={!controlled}
              onClick={() => act(() => m.apply("config"))}
            >
              <Check size={14} />
              应用治疗参数
            </button>
          </footer>
        </Panel>
        {pidGroups.map((g) => (
          <Panel
            key={g.id}
            title={g.title}
            description={g.hint}
            actions={
              <Badge tone={m.dirty.includes(g.id) ? "amber" : "neutral"}>
                {m.dirty.includes(g.id) ? "已修改" : g.id.toUpperCase()}
              </Badge>
            }
          >
            <div className="form-grid">
              {["kp", "ki", "kd", "i_limit_permille"].map((key, i) => (
                <Field
                  key={key}
                  label={i === 3 ? "积分限幅 / ‰" : key.toUpperCase()}
                >
                  <NumberInput
                    disabled={disabled}
                    value={Number(m.params[g.prefix + key])}
                    max={g.max[i]}
                    step={i < 3 ? 0.001 : 1}
                    onChange={(v) => m.edit(g.prefix + key, v, g.id)}
                  />
                </Field>
              ))}
            </div>
            <footer className="form-footer">
              <button
                disabled={!s.connected || disabled}
                onClick={() => act(() => m.read(g.id))}
              >
                读取
              </button>
              <button
                className="primary"
                disabled={!controlled}
                onClick={() => act(() => m.apply(g.id))}
              >
                应用 PID
              </button>
            </footer>
          </Panel>
        ))}
      </div>
    </div>
  );
}

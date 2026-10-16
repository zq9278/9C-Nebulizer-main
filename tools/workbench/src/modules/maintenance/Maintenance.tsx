import { useEffect, useState } from "react";
import { useWorkbench, canControl, batch } from "../../core/store";
import { cmd, u16 } from "../../projects/nebulizer";
import {
  Panel,
  Field,
  NumberInput,
  Badge,
  Alert,
  errorMessage,
} from "../../components/ui";
export function Maintenance() {
  const s = useWorkbench(),
    [fan, setFan] = useState(0),
    [mist, setMist] = useState(0),
    [heat, setHeat] = useState(0),
    [error, setError] = useState("");
  const control = canControl(s),
    active = !!s.snapshot?.maintenance.active;
  async function act(commands: ReturnType<typeof cmd>[]) {
    setError("");
    try {
      const result = await batch(commands);
      if (result) {
        setFan(result.maintenance.fan);
        setMist(result.maintenance.mist);
        setHeat(result.maintenance.heat);
      }
    } catch (e) {
      setError(errorMessage(e));
    }
  }
  function read() {
    const m = s.snapshot?.maintenance;
    if (m) {
      setFan(m.fan);
      setMist(m.mist);
      setHeat(m.heat);
    }
  }
  return (
    <div className="page-stack">
      <Alert>
        维护模式可直接控制实际输出。仅就绪、暂停或治疗完成且无故障时允许进入。释放调试控制或租约超时后，固件关闭维护输出。
      </Alert>
      <Panel
        title="维护控制"
        description="手动输出不使用治疗参数"
        actions={
          <Badge tone={active ? "amber" : "neutral"}>
            {active ? "维护已进入" : "维护未进入"}
          </Badge>
        }
      >
        <div className="form-grid">
          {[
            ["风机档位", fan, setFan],
            ["雾化档位", mist, setMist],
          ].map(([label, value, setter]) => (
            <Field key={String(label)} label={String(label)}>
              <select
                value={Number(value)}
                disabled={s.commandBusy}
                onChange={(e) =>
                  (setter as (n: number) => void)(Number(e.target.value))
                }
              >
                {["关闭", "低档", "中档", "高档"].map((v, i) => (
                  <option key={i} value={i}>
                    {v}
                  </option>
                ))}
              </select>
            </Field>
          ))}
          <Field label="加热功率 / ‰">
            <NumberInput
              value={heat}
              disabled={s.commandBusy}
              min={0}
              max={1000}
              onChange={setHeat}
            />
          </Field>
        </div>
        <footer className="form-footer">
          <button disabled={!s.connected || s.commandBusy} onClick={read}>
            读取维护快照
          </button>
          <button
            disabled={!control || active}
            onClick={() => act([cmd(0x2b)])}
          >
            进入维护
          </button>
          <button
            className="primary"
            disabled={!control || !active}
            onClick={() =>
              act([cmd(0x2d, [fan]), cmd(0x2e, [mist]), cmd(0x2f, u16(heat))])
            }
          >
            应用手动输出
          </button>
          <button disabled={!control} onClick={() => act([cmd(0x2c)])}>
            退出维护并停输出
          </button>
        </footer>
        {error && <div className="error-text">{error}</div>}
      </Panel>
    </div>
  );
}

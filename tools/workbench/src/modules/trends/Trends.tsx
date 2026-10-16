import { useState } from "react";
import { Circle, Square } from "lucide-react";
import { TimeSeriesChart } from "@workbench/charts";
import { samplesCsv } from "@workbench/core";
import {
  api,
  useWorkbench,
  samples,
  clearSamples,
  setRecording,
  log,
} from "../../core/store";
import { channels } from "../../projects/nebulizer";
import { Panel, Badge, errorMessage } from "../../components/ui";
export function Trends({ compact = false }: { compact?: boolean }) {
  const s = useWorkbench();
  const [error, setError] = useState("");
  async function exportCsv() {
    try {
      await api.saveFile(
        "nebulizer_trend.csv",
        "\uFEFF" + samplesCsv(samples, channels),
      );
    } catch (e) {
      setError(errorMessage(e));
    }
  }
  async function record() {
    try {
      if (s.recording) {
        await api.recording("stop");
        setRecording(false);
        log("连续 CSV 记录已停止");
      } else {
        const result = await api.recording(
          "start",
          samplesCsv([], channels) + "\r\n",
        );
        if (result) {
          setRecording(true);
          log("连续 CSV 记录：" + result);
        }
      }
    } catch (e) {
      setError(errorMessage(e));
    }
  }
  return (
    <Panel
      title={compact ? "实时曲线" : "曲线与采集"}
      description="多通道时序数据 · 温度 / 输出 / 误差"
      className={compact ? "chart-panel compact" : "chart-panel"}
      actions={
        <>
          <Badge tone={s.connected ? "green" : "neutral"}>
            {s.connected ? "100 ms SWD / 500 ms UART" : "等待设备"}
          </Badge>
          <button className={s.recording ? "recording" : ""} onClick={record}>
            {s.recording ? <Square size={12} /> : <Circle size={12} />}{" "}
            {s.recording ? "停止记录" : "记录 CSV"}
          </button>
        </>
      }
    >
      {error && <div className="error-text">{error}</div>}
      <TimeSeriesChart
        buffer={samples}
        channels={channels}
        revision={s.revision}
        onExport={exportCsv}
        onClear={clearSamples}
        storageKey="nebulizer/channels"
      />
    </Panel>
  );
}

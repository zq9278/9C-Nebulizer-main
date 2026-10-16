import { useState } from "react";
import { Search, Download, Trash2 } from "lucide-react";
import { api, useWorkbench, clearLogs } from "../../core/store";
import { Panel, Badge, Empty, errorMessage } from "../../components/ui";
import { csvCell } from "@workbench/core";
export function Logs() {
  const s = useWorkbench(),
    [filter, setFilter] = useState(""),
    [kind, setKind] = useState("all"),
    [error, setError] = useState("");
  const rows = (kind === "event" ? s.events : s.logs).filter(
    (x) =>
      (kind === "all" || kind === "event" || x.level === kind) &&
      x.message.toLowerCase().includes(filter.toLowerCase()),
  );
  async function exportLogs() {
    try {
      await api.saveFile(
        "workbench_logs.csv",
        "\uFEFFtime,level,message\r\n" +
          s.logs
            .map((r) => [r.time, r.level, r.message].map(csvCell).join(","))
            .join("\r\n"),
      );
    } catch (e) {
      setError(errorMessage(e));
    }
  }
  return (
    <Panel
      title="事件与日志"
      description="设备反馈、操作应答与安装过程"
      actions={
        <>
          <Badge>{s.logs.length.toLocaleString()} 条</Badge>
          <button onClick={exportLogs}>
            <Download size={14} />
            导出 CSV
          </button>
          <button onClick={clearLogs}>
            <Trash2 size={14} />
            清空
          </button>
        </>
      }
    >
      <div className="log-filters">
        <div className="search-input">
          <Search size={15} />
          <input
            value={filter}
            onChange={(e) => setFilter(e.target.value)}
            placeholder="搜索日志内容…"
          />
        </div>
        <select value={kind} onChange={(e) => setKind(e.target.value)}>
          <option value="all">全部级别</option>
          <option value="error">错误</option>
          <option value="event">设备事件</option>
          <option value="info">操作信息</option>
        </select>
      </div>
      {error && <div className="error-text">{error}</div>}
      {!rows.length ? (
        <Empty
          title="暂无日志"
          description="连接设备或执行操作后，日志会显示在这里"
        />
      ) : (
        <div className="log-table">
          <div className="log-table-header">
            <span>时间</span>
            <span>级别</span>
            <span>内容</span>
          </div>
          {rows
            .slice(-2000)
            .reverse()
            .map((r, i) => (
              <div className="log-table-row" key={r.time + i}>
                <time>
                  {new Date(r.time).toLocaleTimeString("zh-CN", {
                    hour12: false,
                  })}
                </time>
                <Badge
                  tone={
                    r.level === "error"
                      ? "red"
                      : r.level === "event"
                        ? "green"
                        : "neutral"
                  }
                >
                  {r.level.toUpperCase()}
                </Badge>
                <p>{r.message}</p>
              </div>
            ))}
        </div>
      )}
    </Panel>
  );
}

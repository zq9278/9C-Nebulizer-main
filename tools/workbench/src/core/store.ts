import { useSyncExternalStore } from "react";
import {
  SampleBuffer,
  csvCell,
  type WorkbenchAPI,
  type LogEntry,
} from "@workbench/core";
import {
  toSample,
  channels,
  type Snapshot,
  type Command,
} from "../projects/nebulizer";
declare global {
  interface Window {
    workbench: WorkbenchAPI;
  }
}
export const api = window.workbench;
export const samples = new SampleBuffer();
export interface State {
  connected: boolean;
  connecting: boolean;
  commandBusy: boolean;
  mode: string;
  snapshot: Snapshot | null;
  revision: number;
  logs: LogEntry[];
  events: LogEntry[];
  progress: number;
  job: { running: boolean; type: string } | null;
  ready: boolean;
  error: string | null;
  recording: boolean;
  warm: number | null;
}
let state: State = {
  connected: false,
  connecting: false,
  commandBusy: false,
  mode: "swd",
  snapshot: null,
  revision: 0,
  logs: [],
  events: [],
  progress: 0,
  job: null,
  ready: false,
  error: null,
  recording: false,
  warm: null,
};
const listeners = new Set<() => void>();
let lastSample: number | null = null;
let previousTime: number | null = null;
let previousFault = 0;
let framePending = false,
  patch: Partial<State> = {},
  revisionCounter = 0;
function schedule(p: Partial<State>) {
  patch = { ...patch, ...p };
  if (!framePending) {
    framePending = true;
    requestAnimationFrame(() => {
      framePending = false;
      const next = patch;
      patch = {};
      update(next);
    });
  }
}
function update(p: Partial<State>) {
  state = { ...state, ...p };
  for (const f of listeners) f();
}
export function log(message: string, level: LogEntry["level"] = "info") {
  update({
    logs: [
      ...state.logs,
      { time: new Date().toISOString(), message, level },
    ].slice(-10000),
  });
}
export function useWorkbench() {
  return useSyncExternalStore(
    (f) => {
      listeners.add(f);
      return () => listeners.delete(f);
    },
    () => state,
  );
}
export function init() {
  if (!api) {
    update({ error: "请通过 Electron 启动；浏览器预览无法访问硬件" });
    return () => {};
  }
  return api.subscribe((name, data) => {
    if (name === "ready") {
      update({ ready: true, error: null });
      log("设备服务已就绪 · 协议 v" + data.protocol);
    }
    if (name === "snapshot") {
      const snap = data as Snapshot,
        sample = toSample(snap);
      if (lastSample !== snap.sequence) {
        // Start a new history on uptime reset; never binary-search across epochs.
        if (previousTime !== null && sample.time < previousTime) {
          samples.clear();
          log("设备时钟已重置，曲线开始新记录");
        }
        previousTime = sample.time;
        lastSample = snap.sequence;
        samples.push(sample);
        if (state.recording)
          api
            .recording(
              "append",
              [sample.time, ...channels.map((c) => sample.values[c.id])]
                .map(csvCell)
                .join(",") + "\r\n",
            )
            .catch((e) => {
              update({ recording: false });
              api.recording("stop").catch(() => {});
              log(String(e), "error");
            });
      }
      if (snap.runtime.fault && snap.runtime.fault !== previousFault) {
        const entry = {
          time: new Date().toISOString(),
          message: `设备故障 ${snap.runtime.fault}`,
          level: "event" as const,
        };
        update({ events: [entry, ...state.events].slice(0, 1000) });
      }
      previousFault = snap.runtime.fault;
      schedule({ snapshot: snap, revision: ++revisionCounter });
    }
    if (name === "connection") {
      update({
        connected: !!data.connected,
        connecting: false,
        mode: data.mode || state.mode,
      });
      if (!data.connected) {
        lastSample = null;
        if (data.error) log(data.error, "error");
      }
    }
    if (name === "log")
      log(data.message, data.level === "error" ? "error" : "info");
    if (name === "device-event") {
      const entry = {
        time: new Date().toISOString(),
        message: `${data.type === 1 ? "治疗事件" : "设备事件"} · 代码 ${data.code} ${data.uptime_ms !== undefined ? "@ " + data.uptime_ms + " ms" : ""}`,
        level: "event" as const,
      };
      update({ events: [entry, ...state.events].slice(0, 1000) });
      log(entry.message, "event");
    }
    if (name === "progress") update({ progress: data.value });
    if (name === "job") update({ job: data });
    if (name === "warm-timer") update({ warm: data });
    if (name === "backend-exit")
      update({
        ready: false,
        error: data.error,
        connected: false,
        connecting: false,
      });
  });
}
export async function request<T = any>(
  method: string,
  params: Record<string, unknown> = {},
): Promise<T> {
  if (!api) throw Error("硬件 API 不可用");
  try {
    return await api.request<T>(method, params);
  } catch (e) {
    log(e instanceof Error ? e.message : String(e), "error");
    throw e;
  }
}
export async function connect(options: Record<string, unknown>) {
  samples.clear();
  lastSample = null;
  previousTime = null;
  previousFault = 0;
  update({ connecting: true, snapshot: null, revision: ++revisionCounter });
  try {
    await request("connect", options);
  } finally {
    update({ connecting: false });
  }
}
export async function batch(commands: Command[]) {
  if (state.commandBusy) throw Error("上一批命令尚未完成");
  update({ commandBusy: true });
  try {
    return await request<Snapshot | null>("commands", { commands });
  } finally {
    update({ commandBusy: false });
  }
}
export function clearSamples() {
  samples.clear();
  update({ revision: ++revisionCounter });
}
export function clearLogs() {
  update({ logs: [], events: [] });
}
export function setRecording(value: boolean) {
  update({ recording: value });
}
export const canControl = (s: State) =>
  s.connected && !!s.snapshot?.control && !s.commandBusy;

/** UI-agnostic contracts. No device protocol, Electron, React, or Python imports. */
export interface Channel {
  id: string;
  label: string;
  unit: string;
  color: string;
}
export interface Sample {
  time: number;
  values: Record<string, number>;
}
export interface LogEntry {
  time: string;
  level: "info" | "error" | "event";
  message: string;
}
export interface WorkbenchAPI {
  request<T = unknown>(
    method: string,
    params?: Record<string, unknown>,
  ): Promise<T>;
  subscribe(listener: (event: string, data: any) => void): () => void;
  openFile(
    kind: "elf" | "hex" | "nfu" | "json" | "dll",
  ): Promise<string | null>;
  saveFile(name: string, content: string): Promise<string | null>;
  readFile(path: string): Promise<string>;
  recording(
    action: "start" | "stop" | "append",
    payload?: unknown,
  ): Promise<unknown>;
}
export class SampleBuffer {
  private slots: Array<Sample | undefined>;
  private head = 0;
  private count = 0;
  constructor(public readonly capacity = 864000) {
    if (!Number.isInteger(capacity) || capacity < 1)
      throw Error("invalid capacity");
    this.slots = new Array(capacity);
  }
  get length() {
    return this.count;
  }
  push(sample: Sample) {
    if (!Number.isFinite(sample.time)) return;
    this.slots[this.head] = sample;
    this.head = (this.head + 1) % this.capacity;
    this.count = Math.min(this.capacity, this.count + 1);
  }
  at(index: number) {
    if (index < 0 || index >= this.count) return undefined;
    return this.slots[
      (this.head - this.count + index + this.capacity) % this.capacity
    ];
  }
  clear() {
    this.slots = new Array(this.capacity);
    this.head = 0;
    this.count = 0;
  }
  *[Symbol.iterator]() {
    for (let i = 0; i < this.count; i++) yield this.at(i)!;
  }
  /** Min/max buckets preserve narrow spikes, without copying the full history. */
  visible(from: number, to: number, keys: string[], budget = 2400): Sample[] {
    let lo = 0,
      hi = this.count;
    while (lo < hi) {
      const m = (lo + hi) >> 1;
      if (this.at(m)!.time < from) lo = m + 1;
      else hi = m;
    }
    const start = Math.max(0, lo - 1);
    lo = start;
    hi = this.count;
    while (lo < hi) {
      const m = (lo + hi) >> 1;
      if (this.at(m)!.time <= to) lo = m + 1;
      else hi = m;
    }
    const end = Math.min(this.count, lo + 1);
    const step = Math.max(
      1,
      Math.ceil(((end - start) * Math.max(2, keys.length * 2)) / budget),
    );
    const indices = new Set<number>();
    for (let i = start; i < end; i += step) {
      indices.add(i);
      indices.add(Math.min(end - 1, i + step - 1));
      for (const key of keys) {
        let min = i,
          max = i;
        for (let j = i + 1; j < Math.min(end, i + step); j++) {
          if (this.at(j)!.values[key] < this.at(min)!.values[key]) min = j;
          if (this.at(j)!.values[key] > this.at(max)!.values[key]) max = j;
        }
        indices.add(min);
        indices.add(max);
      }
    }
    return [...indices].sort((a, b) => a - b).map((i) => this.at(i)!);
  }
}
export const csvCell = (value: unknown) => {
  const s = String(value ?? "");
  return /[",\n\r]/.test(s) ? '"' + s.replaceAll('"', '""') + '"' : s;
};
export function samplesCsv(samples: Iterable<Sample>, channels: Channel[]) {
  return [
    "time_s," +
      channels.map((c) => csvCell(`${c.label} (${c.unit})`)).join(","),
    ...[...samples].map((s) =>
      [s.time, ...channels.map((c) => s.values[c.id])].map(csvCell).join(","),
    ),
  ].join("\r\n");
}

import { useEffect, useRef, useState, useMemo } from "react";
import { SampleBuffer, type Channel } from "@workbench/core";
import "./chart.css";
export interface TimeSeriesChartProps {
  buffer: SampleBuffer;
  channels: Channel[];
  revision: number;
  onExport?: () => void;
  onClear?: () => void;
  storageKey?: string;
  emptyText?: string;
}
/** Generic time series viewer. Times in seconds, values in declared units. */
export function TimeSeriesChart({
  buffer,
  channels,
  revision,
  onExport,
  onClear,
  storageKey,
  emptyText = "等待采样数据",
}: TimeSeriesChartProps) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const [size, setSize] = useState([800, 450]);
  const [hidden, setHidden] = useState<string[]>(() => {
    try {
      return JSON.parse(
        localStorage.getItem(storageKey || "chart/visibility") || "[]",
      );
    } catch {
      return [];
    }
  });
  const [range, setRange] = useState<[number, number] | null>(null),
    [windowSec, setWindowSec] = useState(60);
  const [yView, setYView] = useState({ factor: 1, offset: 0 }),
    [hover, setHover] = useState<{ x: number; y: number } | null>(null);
  const dragging = useRef<{
    x: number;
    y: number;
    range: [number, number];
    offset: number;
  } | null>(null);
  const latest = buffer.at(buffer.length - 1),
    earliest = buffer.at(0);
  const endTime =
    windowSec >= 1800
      ? Math.floor(latest?.time ?? windowSec)
      : (latest?.time ?? windowSec);
  const current: [number, number] = range || [
    Math.max(0, endTime - windowSec),
    Math.max(windowSec, endTime),
  ];
  const visible = channels.filter((c) => !hidden.includes(c.id)),
    units = [...new Set(visible.map((c) => c.unit))];
  const pointRevision =
    current[1] - current[0] > 1800 ? Math.floor(revision / 10) : revision;
  const points = useMemo(
    () =>
      buffer.visible(
        current[0],
        current[1],
        visible.map((c) => c.id),
      ),
    [buffer, current[0], current[1], pointRevision, hidden, channels],
  );
  useEffect(() => {
    const el = canvas.current;
    if (!el) return;
    const observer = new ResizeObserver(([entry]) =>
      setSize([entry.contentRect.width, entry.contentRect.height]),
    );
    observer.observe(el);
    return () => observer.disconnect();
  }, []);
  useEffect(() => {
    if (storageKey) localStorage.setItem(storageKey, JSON.stringify(hidden));
  }, [hidden, storageKey]);
  function zoom(factor: number, anchor = 0.5) {
    const span = Math.max(
      0.5,
      Math.min(86400, (current[1] - current[0]) * factor),
    );
    const center = current[0] + (current[1] - current[0]) * anchor;
    setRange([center - span * anchor, center + span * (1 - anchor)]);
  }
  useEffect(() => {
    const el = canvas.current;
    if (!el) return;
    const wheel = (e: WheelEvent) => {
      e.preventDefault();
      if (e.ctrlKey)
        setYView((v) => ({
          ...v,
          factor: Math.max(
            0.02,
            Math.min(100, v.factor * (e.deltaY > 0 ? 1.2 : 1 / 1.2)),
          ),
        }));
      else
        zoom(
          e.deltaY > 0 ? 1.2 : 1 / 1.2,
          Math.max(0, Math.min(1, (e.offsetX - 64) / (size[0] - 128))),
        );
    };
    el.addEventListener("wheel", wheel, { passive: false });
    return () => el.removeEventListener("wheel", wheel);
  }, [current[0], current[1], size[0]]);
  useEffect(() => {
    const el = canvas.current;
    if (!el) return;
    const [width, height] = size,
      dpr = devicePixelRatio || 1;
    el.width = width * dpr;
    el.height = height * dpr;
    const ctx = el.getContext("2d")!;
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, width, height);
    const left = 64,
      right = units.length > 1 ? 70 : 24,
      top = 24,
      bottom = 38,
      w = width - left - right,
      h = height - top - bottom;
    const scales = new Map<string, [number, number]>();
    for (const unit of units) {
      const ids = visible.filter((c) => c.unit === unit).map((c) => c.id);
      let lo = Infinity,
        hi = -Infinity;
      for (const p of points)
        for (const id of ids) {
          const v = p.values[id];
          if (Number.isFinite(v)) {
            lo = Math.min(lo, v);
            hi = Math.max(hi, v);
          }
        }
      if (!Number.isFinite(lo)) {
        lo = 0;
        hi = 100;
      }
      let span = Math.max(hi - lo, 1) * 1.2 * yView.factor;
      const center = (lo + hi) / 2 + yView.offset * span;
      scales.set(unit, [center - span / 2, center + span / 2]);
    }
    const x = (time: number) =>
      left + ((time - current[0]) / (current[1] - current[0])) * w;
    ctx.font = '11px "Segoe UI", "Microsoft YaHei", sans-serif';
    ctx.lineWidth = 1;
    for (let i = 0; i <= 5; i++) {
      const y = top + (i * h) / 5;
      ctx.strokeStyle = "#e7edf3";
      ctx.beginPath();
      ctx.moveTo(left, y);
      ctx.lineTo(width - right, y);
      ctx.stroke();
      for (let axis = 0; axis < Math.min(2, units.length); axis++) {
        const [lo, hi] = scales.get(units[axis])!;
        ctx.fillStyle = axis ? "#8a6e3f" : "#6b7f90";
        ctx.textAlign = axis ? "left" : "right";
        ctx.fillText(
          (hi - ((hi - lo) * i) / 5).toFixed(1),
          axis ? width - right + 10 : left - 12,
          y + 4,
        );
      }
      ctx.fillStyle = "#8293a2";
      ctx.textAlign = "center";
      ctx.fillText(
        (current[0] + ((current[1] - current[0]) * i) / 5).toFixed(1) + "s",
        left + (w * i) / 5,
        height - 12,
      );
    }
    ctx.textAlign = "left";
    ctx.fillStyle = "#6b7f90";
    ctx.fillText(units[0] || "", 8, 14);
    ctx.textAlign = "right";
    ctx.fillText(units.slice(1).join(" / "), width - 10, 14);
    ctx.save();
    ctx.beginPath();
    ctx.rect(left, top, w, h);
    ctx.clip();
    for (const channel of visible) {
      const [lo, hi] = scales.get(channel.unit)!;
      ctx.strokeStyle = channel.color;
      ctx.lineWidth = 1.8;
      ctx.beginPath();
      let move = true;
      for (const point of points) {
        const value = point.values[channel.id];
        if (!Number.isFinite(value)) {
          move = true;
          continue;
        }
        const py = top + ((hi - value) / (hi - lo)) * h,
          px = x(point.time);
        if (move) {
          ctx.moveTo(px, py);
          move = false;
        } else ctx.lineTo(px, py);
      }
      ctx.stroke();
      const p = points[points.length - 1];
      if (p && Number.isFinite(p.values[channel.id])) {
        ctx.fillStyle = channel.color;
        ctx.beginPath();
        ctx.arc(
          x(p.time),
          top + ((hi - p.values[channel.id]) / (hi - lo)) * h,
          3,
          0,
          Math.PI * 2,
        );
        ctx.fill();
      }
    }
    if (hover) {
      ctx.strokeStyle = "#71899b";
      ctx.setLineDash([3, 4]);
      ctx.beginPath();
      ctx.moveTo(hover.x, top);
      ctx.lineTo(hover.x, top + h);
      ctx.stroke();
      ctx.setLineDash([]);
    }
    ctx.restore();
  }, [size, revision, hidden, range, windowSec, yView, hover, channels]);
  const hoverTime = hover
    ? current[0] +
      ((hover.x - 64) / (size[0] - 134)) * (current[1] - current[0])
    : null;
  let inspected = latest;
  if (hoverTime !== null && points.length)
    inspected = points.reduce((a, b) =>
      Math.abs(b.time - hoverTime) < Math.abs(a.time - hoverTime) ? b : a,
    );
  return (
    <div className="wb-chart">
      <div className="wb-chart-toolbar">
        <div className="wb-chart-presets">
          {[60, 300, 1800, 86400].map((s) => (
            <button
              className={!range && windowSec === s ? "selected" : ""}
              key={s}
              onClick={() => {
                setWindowSec(s);
                setRange(null);
              }}
            >
              {s < 3600 ? s / 60 + " 分钟" : "24 小时"}
            </button>
          ))}
        </div>
        <div className="wb-chart-actions">
          <button onClick={() => zoom(0.75)}>横轴＋</button>
          <button onClick={() => zoom(4 / 3)}>横轴－</button>
          <button
            onClick={() => setYView((v) => ({ ...v, factor: v.factor * 0.75 }))}
          >
            纵轴＋
          </button>
          <button
            onClick={() =>
              setYView((v) => ({ ...v, factor: (v.factor * 4) / 3 }))
            }
          >
            纵轴－
          </button>
          <button
            onClick={() => {
              setRange(null);
              setYView({ factor: 1, offset: 0 });
            }}
          >
            实时跟随
          </button>
          {onExport && <button onClick={onExport}>导出 CSV</button>}
          {onClear && (
            <button
              onClick={() => {
                onClear();
                setRange(null);
                setYView({ factor: 1, offset: 0 });
              }}
            >
              清空
            </button>
          )}
        </div>
      </div>
      <div className="wb-chart-legend">
        {channels.map((c) => (
          <button
            key={c.id}
            className={hidden.includes(c.id) ? "hidden-series" : ""}
            onClick={() =>
              setHidden((ids) =>
                ids.includes(c.id)
                  ? ids.filter((id) => id !== c.id)
                  : [...ids, c.id],
              )
            }
          >
            <i style={{ background: c.color }} />
            {c.label}
            <strong>
              {Number.isFinite(inspected?.values[c.id])
                ? inspected!.values[c.id].toFixed(1)
                : "—"}
              <small>{c.unit}</small>
            </strong>
          </button>
        ))}
      </div>
      <div className="wb-chart-plot">
        <canvas
          ref={canvas}
          onDoubleClick={() => {
            setRange(null);
            setYView({ factor: 1, offset: 0 });
          }}
          onPointerDown={(e) => {
            e.currentTarget.setPointerCapture(e.pointerId);
            dragging.current = {
              x: e.clientX,
              y: e.clientY,
              range: current,
              offset: yView.offset,
            };
          }}
          onPointerMove={(e) => {
            const rect = e.currentTarget.getBoundingClientRect();
            setHover({ x: e.clientX - rect.left, y: e.clientY - rect.top });
            const d = dragging.current;
            if (d) {
              const dx =
                ((e.clientX - d.x) / (size[0] - 134)) *
                (d.range[1] - d.range[0]);
              setRange([d.range[0] - dx, d.range[1] - dx]);
              setYView((v) => ({
                ...v,
                offset: d.offset + (e.clientY - d.y) / (size[1] - 62),
              }));
            }
          }}
          onPointerUp={() => {
            dragging.current = null;
          }}
          onPointerCancel={() => {
            dragging.current = null;
          }}
          onPointerLeave={() => {
            if (!dragging.current) setHover(null);
          }}
        />
        {!buffer.length && (
          <div className="wb-chart-empty">
            <span>∿</span>
            <strong>{emptyText}</strong>
            <small>连接设备后，采样数据会显示在这里</small>
          </div>
        )}
        {hover && buffer.length > 0 && (
          <div className="wb-chart-cursor">
            {inspected?.time.toFixed(2)} s · {range ? "历史视图" : "实时视图"}
          </div>
        )}
      </div>
      <div className="wb-chart-footer">
        <span className={range ? "" : "live"}>
          {range ? "已暂停跟随" : "● 实时跟随"} · 滚轮缩放时间 / Ctrl +
          滚轮缩放数值 / 拖动平移 / 双击复位
        </span>
        <span>
          {buffer.length.toLocaleString()} / {buffer.capacity.toLocaleString()}{" "}
          点
          {earliest && latest
            ? ` · ${(latest.time - earliest.time).toFixed(1)} s`
            : ""}
        </span>
      </div>
    </div>
  );
}

import { describe, it, expect } from "vitest";
import { SampleBuffer, samplesCsv } from "../packages/core/src/index";
import {
  configCommands,
  pidCommand,
  pidGroups,
  defaultParams,
} from "../src/projects/nebulizer";
describe("generic sample buffer", () => {
  it("retains ordered history when the ring wraps", () => {
    const b = new SampleBuffer(3);
    for (let i = 0; i < 5; i++) b.push({ time: i, values: { x: i } });
    expect([...b].map((s) => s.time)).toEqual([2, 3, 4]);
    expect(b.at(-1)).toBeUndefined();
    b.clear();
    expect(b.length).toBe(0);
  });
  it("preserves narrow spikes when downsampling", () => {
    const b = new SampleBuffer(10000);
    for (let i = 0; i < 10000; i++)
      b.push({
        time: i,
        values: { x: i === 4731 ? 999 : i === 6789 ? -999 : 0 },
      });
    const points = b.visible(0, 9999, ["x"], 100);
    expect(points.length).toBeLessThanOrEqual(110);
    expect(points.some((p) => p.values.x === 999)).toBe(true);
    expect(points.some((p) => p.values.x === -999)).toBe(true);
  });
  it("restricts samples to the viewport plus boundary neighbours", () => {
    const b = new SampleBuffer();
    for (let i = 0; i < 100; i++) b.push({ time: i, values: { x: i } });
    const values = b.visible(20, 25, ["x"]);
    expect(values[0].time).toBe(19);
    expect(values.at(-1)!.time).toBe(26);
  });
  it("exports declared channels and quotes CSV labels", () => {
    expect(
      samplesCsv(
        [{ time: 1, values: { x: 3 } }],
        [{ id: "x", label: "a,b", unit: "V", color: "red" }],
      ),
    ).toBe('time_s,"a,b (V)"\r\n1,3');
  });
});
describe("Nebulizer adapter", () => {
  it("disables keep-warm before applying a configuration", () => {
    const commands = configCommands({
      ...defaultParams,
      keep_warm_enabled: true,
    });
    expect(commands[0]).toEqual({ command: 0x15, payload: [0] });
    expect(commands.at(-1)).toEqual({ command: 0x15, payload: [1] });
    expect(commands.find((c) => c.command === 0x11)!.payload).toEqual([164, 1]);
  });
  it("encodes signed-width PID fields and validates limits", () => {
    const encoded = pidCommand(pidGroups[0], defaultParams);
    expect(encoded.command).toBe(0x29);
    expect(
      new DataView(Uint8Array.from(encoded.payload).buffer).getInt32(0, true),
    ).toBe(9000);
    expect(() =>
      pidCommand(pidGroups[1], { ...defaultParams, fan_kp: 21 }),
    ).toThrow();
  });
});

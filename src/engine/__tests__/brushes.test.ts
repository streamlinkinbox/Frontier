import { describe, it, expect } from "vitest";
import { sculptField, sampleField } from "../field";
import { makeBrushStamp, brushPlaneHit } from "../brush";
import { DEFAULT_SETTINGS, type Vec3, type Tool } from "../types";
const size = { x: 64, y: 32, z: 64 },
  h = 1.5;
const settings = { ...DEFAULT_SETTINGS, radius: 7, strength: 1, falloff: 0.4 };
function field(fn: (p: Vec3) => number) {
  const data = new Float32Array(size.x * size.y * size.z * 4);
  for (let z = 0; z < size.z; z++)
    for (let y = 0; y < size.y; y++)
      for (let x = 0; x < size.x; x++)
        data[((z * size.y + y) * size.x + x) * 4] = Math.max(
          -24,
          Math.min(
            32,
            fn([-48 + (x + 0.5) * h, -10 + (y + 0.5) * h, -48 + (z + 0.5) * h]),
          ),
        );
  return data;
}

describe("stroke-plane flatten and retained ridges", () => {
  it("converges to a locked horizontal plane across a drag, without overshooting it", () => {
    const data = field(
      (p) => p[1] - (8 + p[0] * 0.23 + Math.sin(p[2] * 0.4) * 0.4),
    );
    const before = data.slice(),
      stamp = makeBrushStamp([0, 8, 0]);
    for (let k = 0; k < 20; k++)
      for (const x of [-3, 0, 3])
        sculptField(data, size, [x, 8, 0], "flatten", settings, stamp);
    for (const x of [-3, 0, 3])
      for (const z of [-2, 0, 2])
        expect(Math.abs(sampleField(data, size, [x, 8, z]))).toBeLessThan(0.04);
    for (let z = 0; z < size.z; z++)
      for (let y = 0; y < size.y; y++)
        for (let x = 0; x < size.x; x++) {
          const i = ((z * size.y + y) * size.x + x) * 4,
            plane = -10 + (y + 0.5) * h - 8;
          if (data[i] !== before[i]) {
            expect(data[i]).toBeGreaterThanOrEqual(
              Math.min(before[i], plane) - 0.0001,
            );
            expect(data[i]).toBeLessThanOrEqual(
              Math.max(before[i], plane) + 0.0001,
            );
          }
        }
    expect(sampleField(data, size, [30, 8, 0])).toBe(
      sampleField(before, size, [30, 8, 0]),
    );
  });
  it("flattens a vertical face as a true 3D plane, not a heightmap", () => {
    const data = field((p) => -p[0] + (p[1] - 8) * 0.2);
    const stamp = makeBrushStamp([0, 8, 0], [-1, 0, 0], [0, 0, 1]);
    for (let k = 0; k < 24; k++)
      sculptField(data, size, [0, 8, 0], "flatten", settings, stamp);
    expect(Math.abs(sampleField(data, size, [0, 6, 0]))).toBeLessThan(0.04);
    expect(Math.abs(sampleField(data, size, [0, 10, 0]))).toBeLessThan(0.04);
    expect(brushPlaneHit([10, 8, 3], [-1, 0, 0], stamp)).toEqual([0, 8, 3]);
  });
  it("keeps the old moving-height effect separately as Ridges", () => {
    const source = field((p) => p[1] - 8),
      data = source.slice();
    sculptField(data, size, [0, 10, 0], "ridges", settings);
    expect(sampleField(data, size, [0, 9, 0])).toBeLessThan(
      sampleField(source, size, [0, 9, 0]),
    );
    expect(sampleField(data, size, [30, 8, 0])).toBe(
      sampleField(source, size, [30, 8, 0]),
    );
  });
});

describe("geometric feature stamps", () => {
  it("cuts branching cracks and a distinctly larger/deeper crevice", () => {
    const source = field((p) => p[1] - 10),
      crack = source.slice(),
      crevice = source.slice();
    const stamp = makeBrushStamp([0, 10, 0], [0, 1, 0], [1, 0, 0], 1);
    for (let k = 0; k < 12; k++)
      for (const [data, tool] of [
        [crack, "crack"],
        [crevice, "crevice"],
      ] as [Float32Array, Tool][])
        sculptField(
          data,
          size,
          [0, 10, 0],
          tool,
          { ...settings, brushDepth: 0.8 },
          stamp,
        );
    let cracks = 0,
      crevices = 0;
    for (let i = 0; i < source.length; i += 4) {
      if (source[i] < 0 && crack[i] > 0) cracks++;
      if (source[i] < 0 && crevice[i] > 0) crevices++;
      expect(crack[i]).toBeGreaterThanOrEqual(source[i]);
    }
    expect(cracks).toBeGreaterThan(8);
    expect(crevices).toBeGreaterThan(cracks * 1.7);
    expect(sampleField(crevice, size, [0, 6, 0])).toBeGreaterThan(
      sampleField(crack, size, [0, 6, 0]),
    );
  });
  it("adds a substantial bounded boulder, with an idempotent full-strength stamp", () => {
    const data = field((p) => p[1]),
      stamp = makeBrushStamp([0, 0, 0]);
    sculptField(data, size, [0, 0, 0], "boulder", settings, stamp);
    expect(sampleField(data, size, [0, 3, 0])).toBeLessThan(0);
    expect(sampleField(data, size, [0, 12, 0])).toBeGreaterThan(0);
    const first = data.slice();
    sculptField(data, size, [0, 0, 0], "boulder", settings, stamp);
    expect(data).toEqual(first);
    expect(data.every(Number.isFinite)).toBe(true);
  });
});

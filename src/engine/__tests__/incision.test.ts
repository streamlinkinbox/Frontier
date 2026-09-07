import { describe, it, expect } from "vitest";
import { computeFlux, evolveField, redistanceField } from "../simulation";
import { DEFAULT_SETTINGS, type Settings } from "../types";
const size = { x: 40, y: 20, z: 40 },
  h = 96 / size.x;
function fixture(kind: "slope" | "wall", shelter = false) {
  const data = new Float32Array(size.x * size.y * size.z * 4);
  for (let z = 0; z < size.z; z++)
    for (let y = 0; y < size.y; y++)
      for (let x = 0; x < size.x; x++) {
        const px = -48 + (x + 0.5) * h,
          py = -10 + (y + 0.5) * h;
        let d = kind === "wall" ? -px : (py - 10) * 0.55 - px * 0.835;
        if (shelter) d = Math.min(d, Math.abs(px + 9) - h * 0.6);
        data[((z * size.y + y) * size.x + x) * 4] = Math.max(
          -24,
          Math.min(32, d),
        );
      }
  return data;
}
function run(source: Float32Array, settings: Settings, count: number) {
  let data = source.slice(),
    out = source.slice();
  const flux = new Float32Array(data.length);
  for (let i = 0; i < count; i++) {
    computeFlux(data, size, flux, settings);
    evolveField(data, size, settings, flux, out);
    [data, out] = [out, data];
    if (i % 2 === 1) {
      redistanceField(data, size, out);
      [data, out] = [out, data];
    }
  }
  return data;
}
function crossing(data: Float32Array, y: number, z: number) {
  for (let x = 18; x < size.x - 4; x++) {
    const a = data[((z * size.y + y) * size.x + x) * 4],
      b = data[((z * size.y + y) * size.x + x + 1) * 4];
    if (a >= 0 && b < 0) return -48 + (x + 0.5 + a / (a - b)) * h;
  }
  return NaN;
}
const std = (a: number[]) => {
  const m = a.reduce((x, y) => x + y, 0) / a.length;
  return Math.sqrt(a.reduce((x, y) => x + (y - m) ** 2, 0) / a.length);
};

describe("actual zero-isosurface incision, not changed shading or distance values", () => {
  it("routed rain creates nonuniform channels on an otherwise uniform slope", () => {
    const source = fixture("slope");
    const s = {
      ...DEFAULT_SETTINGS,
      rainfall: 1,
      erosion: 1,
      thermal: 0,
      cohesion: 0.02,
      resistance: 0,
      sediment: 0.9,
      settling: 0.1,
      evaporation: 0.1,
      windErosion: 0,
    };
    const channels = run(source, { ...s, channeling: 1 }, 100),
      uniform = run(source, { ...s, channeling: 0 }, 100);
    const cuts: number[] = [],
      flat: number[] = [];
    for (let z = 8; z < 32; z++) {
      const before = crossing(source, 8, z);
      cuts.push(crossing(channels, 8, z) - before);
      flat.push(crossing(uniform, 8, z) - before);
    }
    expect(cuts.every(Number.isFinite)).toBe(true);
    expect(Math.max(...cuts)).toBeGreaterThan(0.2);
    expect(std(cuts)).toBeGreaterThan(0.04);
    expect(std(cuts)).toBeGreaterThan(std(flat) * 1.5);
  }, 15000);
  it("wind cuts separated bands on the exposed face and respects its direction and shelter", () => {
    const source = fixture("wall"),
      s = {
        ...DEFAULT_SETTINGS,
        rainfall: 0,
        erosion: 0,
        thermal: 0,
        resistance: 0,
        water: false,
        windErosion: 1,
        windDirection: 0,
        settling: 0.5,
      };
    const wind = run(source, s, 65),
      away = run(source, { ...s, windDirection: 180 }, 65),
      shieldSource = fixture("wall", true),
      shield = run(shieldSource, s, 10);
    const cuts: number[] = [];
    for (let y = 5; y < 15; y++)
      for (let z = 12; z < 28; z++)
        cuts.push(crossing(wind, y, z) - crossing(source, y, z));
    expect(Math.max(...cuts)).toBeGreaterThan(0.2);
    expect(std(cuts)).toBeGreaterThan(0.08);
    expect(
      Math.abs(crossing(away, 9, 20) - crossing(source, 9, 20)),
    ).toBeLessThan(0.05);
    expect(
      Math.abs(crossing(shield, 9, 20) - crossing(shieldSource, 9, 20)),
    ).toBeLessThan(0.08);
  }, 15000);
});

it("wind transports dry sediment downwind without creating particle mass", () => {
  const data = fixture("wall");
  for (let i = 0; i < data.length; i += 4) data[i] = 24;
  const i = ((20 * size.y + 10) * size.x + 20) * 4;
  data[i + 2] = 0.5;
  const out = data.slice(),
    flux = new Float32Array(data.length);
  evolveField(
    data,
    size,
    {
      ...DEFAULT_SETTINGS,
      rainfall: 0,
      erosion: 0,
      thermal: 0,
      windErosion: 1,
      windDirection: 0,
      settling: 0,
    },
    flux,
    out,
  );
  expect(out[i + 4 + 2]).toBeGreaterThan(0);
  expect(out[i - 4 + 2]).toBe(0);
  let sum = 0;
  for (let j = 2; j < out.length; j += 4) {
    sum += out[j];
    expect(out[j]).toBeGreaterThanOrEqual(0);
  }
  expect(sum).toBeCloseTo(0.5, 6);
});

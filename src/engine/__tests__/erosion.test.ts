import { describe, it, expect } from "vitest";
import {
  computeFlux,
  evolveField,
  computeTalusFlux,
  settleTalus,
} from "../simulation";
import { solidFraction, strataStrength, erodibility } from "../geology";
import { DEFAULT_SETTINGS, type Settings } from "../types";
import { validateSettings } from "../project";

const size = { x: 24, y: 12, z: 24 },
  h = 4;
const at = (x: number, y: number, z: number) =>
  ((z * size.y + y) * size.x + x) * 4;
const base: Settings = {
  ...DEFAULT_SETTINGS,
  rainfall: 0,
  evaporation: 0,
  erosion: 1,
  thermal: 0,
  resistance: 0,
  settling: 0,
  cohesion: 0.28,
};
function plane(angle = 0) {
  const data = new Float32Array(size.x * size.y * size.z * 4),
    r = (angle * Math.PI) / 180;
  for (let z = 0; z < size.z; z++)
    for (let y = 0; y < size.y; y++)
      for (let x = 0; x < size.x; x++)
        data[at(x, y, z)] =
          ((y - 4.35) * Math.cos(r) - (x - 12) * Math.sin(r)) * h;
  return data;
}
function mass(data: Float32Array) {
  let total = 0;
  for (let i = 0; i < data.length; i += 4)
    total += solidFraction(data[i], h) + data[i + 2];
  return total;
}
function evolve(
  data: Float32Array,
  settings = base,
  flux = new Float32Array((data.length / 4) * 3),
) {
  const out = data.slice();
  evolveField(data, size, settings, flux, out);
  return out;
}

describe("shear, local sediment budgets and roof exposure", () => {
  it("still water does not scour, but tangential runoff above critical shear does", () => {
    const data = plane(),
      flux = new Float32Array((data.length / 4) * 3);
    for (let z = 3; z < size.z - 3; z++)
      for (let y = 3; y < 7; y++)
        for (let x = 3; x < size.x - 3; x++) {
          data[at(x, y, z) + 1] = 0.4;
          flux[(at(x, y, z) / 4) * 3] = 0.045;
        }
    const center = at(12, 4, 12);
    expect(evolve(data)[center]).toBe(data[center]);
    const flowing = evolve(data, base, flux);
    expect(flowing[center]).toBeGreaterThan(data[center]);
    expect(flowing[center + 2]).toBeGreaterThan(0);
    expect(mass(flowing)).toBeCloseTo(mass(data), 4);
    const cohesive = evolve(data, { ...base, cohesion: 1 }, flux);
    expect(cohesive[center] - data[center]).toBeLessThan(
      flowing[center] - data[center],
    );
  });
  it("deposition spends LOCAL suspended mass and cannot borrow a neighbor's sediment", () => {
    const data = plane(),
      center = at(12, 4, 12);
    data[center + 4 + 2] = 0.5;
    expect(evolve(data, { ...base, erosion: 0, sediment: 0 })[center]).toBe(
      data[center],
    );
    data[center + 2] = 0.15;
    const out = evolve(data, { ...base, erosion: 0, sediment: 0 });
    expect(out[center]).toBeLessThan(data[center]);
    const gained =
      solidFraction(out[center], h) - solidFraction(data[center], h);
    expect(data[center + 2] - out[center + 2]).toBeCloseTo(gained, 6);
    expect(mass(out)).toBeCloseTo(mass(data), 4);
  });
  it("a one-voxel roof stops rain on the cave floor, but rain reaches its exposed top", () => {
    const data = plane();
    for (let z = 0; z < size.z; z++)
      for (let y = 0; y < size.y; y++)
        for (let x = 0; x < size.x; x++)
          data[at(x, y, z)] = Math.min(
            (y - 3.7) * h,
            Math.abs(y - 7) * h - h * 0.4,
          );
    const out = evolve(data, { ...base, rainfall: 1, erosion: 0 });
    expect(out[at(12, 4, 12) + 1]).toBe(0);
    expect(out[at(12, 8, 12) + 1]).toBeGreaterThan(0);
  });
  it("sediment settles down without losing mass or becoming negative", () => {
    const data = plane();
    for (let i = 0; i < data.length; i += 4) data[i] = 24;
    const center = at(12, 7, 12);
    data[center + 2] = 0.2;
    const out = evolve(data, { ...base, settling: 1, erosion: 0 });
    expect(out[center - size.x * 4 + 2]).toBeGreaterThan(0);
    let before = 0,
      after = 0;
    for (let i = 2; i < data.length; i += 4) {
      before += data[i];
      after += out[i];
      expect(out[i]).toBeGreaterThanOrEqual(0);
    }
    expect(after).toBeCloseTo(before, 6);
  });
  it("combines advective and settling donor budgets without manufacturing sediment", () => {
    const data = plane();
    for (let i = 0; i < data.length; i += 4) data[i] = 24;
    for (let z = 4; z < 18; z++)
      for (let y = 4; y < 8; y++)
        for (let x = 4; x < 18; x++) {
          const i = at(x, y, z);
          data[i + 1] = ((x * 13 + y * 7 + z * 3) % 11) / 12;
          data[i + 2] = data[i + 1] * 0.7;
        }
    const flux = new Float32Array((data.length / 4) * 3);
    computeFlux(data, size, flux);
    const out = evolve(data, { ...base, settling: 1, erosion: 0 }, flux);
    let before = 0,
      after = 0;
    for (let i = 2; i < data.length; i += 4) {
      before += data[i];
      after += out[i];
      expect(out[i]).toBeGreaterThanOrEqual(0);
    }
    expect(after).toBeCloseTo(before, 4);
  });
});

describe("gravity/repose limited talus, rather than global smoothing", () => {
  it("leaves a slope below the repose angle unchanged", () => {
    const data = plane(15),
      flux = new Float32Array(data.length),
      out = data.slice();
    computeTalusFlux(data, size, { ...base, thermal: 1, talusAngle: 34 }, flux);
    settleTalus(data, size, flux, out);
    expect(out).toEqual(data);
    expect(flux.every((v) => v === 0)).toBe(true);
  });
  it("moves material downhill and conserves the solid-volume proxy on steep slopes", () => {
    const data = plane(60),
      flux = new Float32Array(data.length),
      out = data.slice();
    computeTalusFlux(data, size, { ...base, thermal: 1, talusAngle: 34 }, flux);
    settleTalus(data, size, flux, out);
    expect(flux.some((v) => v > 0)).toBe(true);
    expect(mass(out)).toBeCloseTo(mass(data), 4);
    let before = 0,
      after = 0;
    for (let z = 0; z < size.z; z++)
      for (let y = 0; y < size.y; y++)
        for (let x = 0; x < size.x; x++) {
          const i = at(x, y, z);
          before += solidFraction(data[i], h) * y;
          after += solidFraction(out[i], h) * y;
          expect(out[i + 1]).toBe(data[i + 1]);
          expect(out[i + 2]).toBe(data[i + 2]);
        }
    expect(after).toBeLessThan(before);
    expect(out.every(Number.isFinite)).toBe(true);
  });
  it("uses the same hard/soft layer ordering as the visual material", () => {
    const bands = Array.from({ length: 40 }, (_, y) => ({
      s: strataStrength(5, y, 7),
      e: erodibility(5, y, 7, 1),
    }));
    const hard = bands.reduce((a, b) => (a.s > b.s ? a : b)),
      soft = bands.reduce((a, b) => (a.s < b.s ? a : b));
    expect(hard.e).toBeLessThan(soft.e * 0.5);
  });
});

it("imports older settings with defaults but rejects invalid new controls", () => {
  const legacy: any = { ...DEFAULT_SETTINGS };
  for (const key of ["cohesion", "settling", "talusAngle", "waterClarity"])
    delete legacy[key];
  expect(validateSettings(legacy)).toEqual(DEFAULT_SETTINGS);
  expect(() =>
    validateSettings({ ...DEFAULT_SETTINGS, talusAngle: NaN }),
  ).toThrow();
  expect(() =>
    validateSettings({ ...DEFAULT_SETTINGS, waterClarity: 2 }),
  ).toThrow();
});

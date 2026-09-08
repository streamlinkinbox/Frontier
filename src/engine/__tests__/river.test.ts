import { describe, it, expect } from "vitest";
import { riverAmplitude, riverSample, riverSlopeBound } from "../river";
import { validateSettings } from "../project";
import { DEFAULT_SETTINGS, type Vec3 } from "../types";
const correlation = (a: number[], b: number[]) => {
  const ma = a.reduce((x, y) => x + y) / a.length,
    mb = b.reduce((x, y) => x + y) / b.length;
  let ab = 0,
    aa = 0,
    bb = 0;
  for (let i = 0; i < a.length; i++) {
    ab += (a[i] - ma) * (b[i] - mb);
    aa += (a[i] - ma) ** 2;
    bb += (b[i] - mb) ** 2;
  }
  return ab / Math.sqrt(aa * bb);
};
describe("non-periodic river surface", () => {
  it("has consistent analytic normals, rather than unrelated sine normals", () => {
    const s = {
      ...DEFAULT_SETTINGS,
      wind: 0.78,
      waterCurrent: 1.1,
      waterDirection: 237,
    };
    for (const p of [
      [3.17, 2, 8.33],
      [-12.21, 2, 17.81],
      [30.45, 2, -21.27],
    ] as Vec3[]) {
      const h = riverSample(p, 3.7, s),
        epsilon = 0.0001;
      const dx =
        (riverSample([p[0] + epsilon, p[1], p[2]], 3.7, s)[0] -
          riverSample([p[0] - epsilon, p[1], p[2]], 3.7, s)[0]) /
        (2 * epsilon);
      const dz =
        (riverSample([p[0], p[1], p[2] + epsilon], 3.7, s)[0] -
          riverSample([p[0], p[1], p[2] - epsilon], 3.7, s)[0]) /
        (2 * epsilon);
      expect(h[1]).toBeCloseTo(dx, 5);
      expect(h[2]).toBeCloseTo(dz, 5);
    }
  });
  it("does not repeat when shifted by plausible small tile widths", () => {
    const s = { ...DEFAULT_SETTINGS, wind: 0.66 },
      base: number[] = [];
    for (let z = 0; z < 20; z++)
      for (let x = 0; x < 20; x++)
        base.push(riverSample([x * 0.53 - 5, 0, z * 0.61 - 6], 2, s)[0]);
    for (const offset of [1.8, 3.6, 7.2, 14.4]) {
      const shifted: number[] = [];
      for (let z = 0; z < 20; z++)
        for (let x = 0; x < 20; x++)
          shifted.push(
            riverSample([x * 0.53 - 5 + offset, 0, z * 0.61 - 6], 2, s)[0],
          );
      expect(Math.abs(correlation(base, shifted))).toBeLessThan(0.9);
    }
  });
  it("has bounded wave height/slope, directional motion, and a still zero-forcing surface", () => {
    const s = {
      ...DEFAULT_SETTINGS,
      wind: 1,
      waterCurrent: 2.5,
      waterRippleScale: 0.5,
      waterFlowMode: "directional" as const,
    };
    for (let i = 0; i < 100; i++) {
      const wave = riverSample([i * 0.73 - 30, 0, i * 0.29 - 18], i * 0.07, s);
      expect(Math.abs(wave[0])).toBeLessThanOrEqual(riverAmplitude(s));
      expect(Math.hypot(wave[1], wave[2])).toBeLessThan(riverSlopeBound(s));
    }
    expect(
      riverSample([2, 0, 3], 10, { ...s, wind: 0, waterCurrent: 0 }),
    ).toEqual([0, 0, 0]);
    expect(riverSample([2, 0, 3], 2, s)).not.toEqual(
      riverSample([2, 0, 3], 8, s),
    );
    expect(riverSample([2, 0, 3], 2, s)).not.toEqual(
      riverSample([2, 0, 3], 2, { ...s, waterDirection: 90 }),
    );
  });
});

it("loads old projects with flow defaults and validates new river controls", () => {
  const old: any = { ...DEFAULT_SETTINGS };
  delete old.waterCurrent;
  delete old.waterDirection;
  delete old.waterRippleScale;
  const loaded = validateSettings(old);
  expect(loaded.waterCurrent).toBe(DEFAULT_SETTINGS.waterCurrent);
  expect(loaded.waterDirection).toBe(270);
  expect(loaded.waterRippleScale).toBe(1.8);
  expect(() =>
    validateSettings({ ...DEFAULT_SETTINGS, waterCurrent: -1 }),
  ).toThrow();
  expect(() =>
    validateSettings({ ...DEFAULT_SETTINGS, waterRippleScale: 0 }),
  ).toThrow();
});

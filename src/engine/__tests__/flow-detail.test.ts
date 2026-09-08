import { describe, it, expect } from "vitest";
import { DEFAULT_SETTINGS, type Vec3 } from "../types";
import {
  canyonCenter,
  channelArc,
  channelPointAtArc,
  riverCoordinates,
} from "../flowRoute";
import { currentStreak, riverSample } from "../river";
import { layeredRock } from "../surfaceDetail";
import { materialMacroColor } from "../materials";

describe("downstream channel motion", () => {
  it("follows the bend tangent and can be reversed", () => {
    const s = { ...DEFAULT_SETTINGS, waterFlowMode: "channel" as const };
    const p: Vec3 = [canyonCenter(12, s.seed), 0, 12],
      a = riverCoordinates(p, s),
      b = riverCoordinates(p, { ...s, waterReverse: true });
    expect(a.across).toBeCloseTo(0, 9);
    expect(a.direction[1]).toBeLessThan(0);
    expect(b.direction[1]).toBeGreaterThan(0);
    expect(a.direction[0]).toBeCloseTo(-b.direction[0]);
    expect(Math.abs(a.direction[0])).toBeGreaterThan(0.05);
  });
  it("a marked feature travels the requested arc distance, rather than only oscillating", () => {
    const s = {
      ...DEFAULT_SETTINGS,
      waterCurrent: 1,
      waterFlowMode: "channel" as const,
    };
    for (const reverse of [false, true]) {
      const state = { ...s, waterReverse: reverse };
      for (const z of [-20, -5, 12, 28]) {
        const start: Vec3 = [canyonCenter(z, s.seed), 0, z],
          arc = channelArc(z, s.seed)[0];
        const end = channelPointAtArc(arc + (reverse ? 1 : -1) * 3, s.seed);
        expect((end[2] - z) * (reverse ? 1 : -1)).toBeGreaterThan(0);
        expect(currentStreak(start, 0, state)).toBeCloseTo(
          currentStreak(end, 3, state),
          6,
        );
        const before = riverCoordinates(start, state),
          after = riverCoordinates(end, state);
        expect(after.along - before.along).toBeCloseTo(3, 5);
      }
    }
  });
  it("channel coordinates retain a consistent height gradient around bends", () => {
    const p: Vec3 = [3.1, 0, 7.9],
      s = {
        ...DEFAULT_SETTINGS,
        waterFlowMode: "channel" as const,
        waterCurrent: 0.8,
      },
      e = 0.0001;
    const wave = riverSample(p, 2, s);
    const dx =
      (riverSample([p[0] + e, p[1], p[2]], 2, s)[0] -
        riverSample([p[0] - e, p[1], p[2]], 2, s)[0]) /
      (2 * e);
    const dz =
      (riverSample([p[0], p[1], p[2] + e], 2, s)[0] -
        riverSample([p[0], p[1], p[2] - e], 2, s)[0]) /
      (2 * e);
    expect(wave[1]).toBeCloseTo(dx, 5);
    expect(wave[2]).toBeCloseTo(dz, 5);
  });
});

describe("layered rock height and normal detail", () => {
  it("adds real height-gradient detail independently of base material color", () => {
    const s = { ...DEFAULT_SETTINGS },
      flat = { ...s, rockRelief: 0, rockLayerRelief: 0 },
      p: Vec3 = [3.71, 8.43, -4.12];
    expect(layeredRock(p, flat)).toEqual([0, 0, 0, 0]);
    expect(Math.hypot(...layeredRock(p, s).slice(1))).toBeGreaterThan(0.05);
    expect(materialMacroColor(p, [0, 1, 0], s)).toEqual(
      materialMacroColor(p, [0, 1, 0], flat),
    );
  });
  it("matches finite differences for the layered, warped noise", () => {
    const s = { ...DEFAULT_SETTINGS },
      e = 0.00001;
    for (const p of [
      [3.71, 8.43, -4.12],
      [-7.31, 14.93, 11.14],
      [15.4, 3.36, -9.72],
    ] as Vec3[]) {
      const sample = layeredRock(p, s);
      for (let axis = 0; axis < 3; axis++) {
        const a = [...p] as Vec3,
          b = [...p] as Vec3;
        a[axis] += e;
        b[axis] -= e;
        expect(sample[axis + 1]).toBeCloseTo(
          (layeredRock(a, s)[0] - layeredRock(b, s)[0]) / (2 * e),
          3,
        );
      }
    }
  });
  it("has distinct octaves/layers and filters unresolved detail", () => {
    const p: Vec3 = [3.71, 8.43, -4.12],
      s = DEFAULT_SETTINGS;
    expect(layeredRock(p, s)).not.toEqual(
      layeredRock(p, { ...s, rockOctaves: 1, rockLayerRelief: 0 }),
    );
    expect(layeredRock(p, s, 3)).toEqual([0, 0, 0, 0]);
  });
});

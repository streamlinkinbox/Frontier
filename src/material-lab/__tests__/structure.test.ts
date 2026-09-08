import { describe, expect, it } from "vitest";
import {
  DEFAULT_STONE,
  STONE_PRESETS,
  encodeStoneRecipe,
  fieldBounds,
  validateStoneRecipe,
} from "../model";
import { stoneBase, stoneField, type Point } from "../field";
import { bedBoundary, beddingDisplacement, poreShape } from "../structure";
import {
  MAX_FRACTURE_SEGMENTS,
  getFractureNetwork,
  fractureDistance,
  projectStone,
} from "../fractures";
const settings = { ...DEFAULT_STONE };
const slate = { ...STONE_PRESETS.find((p) => p.id === "slate")!.values };
const dot = (a: Point, b: Point) => a.reduce((n, v, i) => n + v * b[i], 0);

describe("broken sheets rather than repeating ribs", () => {
  it("has ordered, nonuniform layer boundaries", () => {
    const gaps = Array.from(
      { length: 50 },
      (_, i) => bedBoundary(i - 24, slate) - bedBoundary(i - 25, slate),
    );
    expect(Math.min(...gaps)).toBeGreaterThan(0.5);
    expect(Math.max(...gaps)).toBeLessThan(1.5);
    expect(Math.max(...gaps) - Math.min(...gaps)).toBeGreaterThan(0.4);
  });
  it("contains flat sheet interiors, not a sinusoidal profile", () => {
    const s = { ...slate, layerBreakup: 0 };
    let flat = 0;
    let changing = 0;
    for (let i = 0; i < 500; i++) {
      const p: Point = [0.02, -0.2 + i * 0.0008, 0.11],
        q: Point = [p[0], p[1] + 0.00005, p[2]];
      const difference = Math.abs(
        beddingDisplacement(p, s, 0) - beddingDisplacement(q, s, 0),
      );
      if (difference < 1e-12) flat++;
      if (difference > 1e-6) changing++;
    }
    expect(flat).toBeGreaterThan(150);
    expect(changing).toBeGreaterThan(20);
  });
  it("sheet breakup changes geometric offsets and stays continuous at joins", () => {
    let changed = 0;
    for (let i = 0; i < 160; i++) {
      const p: Point = [0.09, -0.15 + i * 0.002, 0.13];
      if (
        Math.abs(
          beddingDisplacement(p, { ...slate, layerBreakup: 0 }, 0) -
            beddingDisplacement(p, { ...slate, layerBreakup: 1 }, 0),
        ) > 0.0001
      )
        changed++;
      const a = beddingDisplacement(p, slate, 0),
        b = beddingDisplacement([p[0], p[1] + 1e-7, p[2]], slate, 0);
      expect(Math.abs(a - b)).toBeLessThan(0.00002);
    }
    expect(changed).toBeGreaterThan(60);
  });
});
describe("surface-following fracture graph", () => {
  it("is deterministic, capped and independent of palette/lighting/camera", () => {
    const a = getFractureNetwork(settings),
      b = getFractureNetwork({
        ...settings,
        palette: "fantasy-amethyst",
        sunElevation: 70,
        roughness: 0.3,
      });
    expect(a).toBe(b);
    expect(a.segments.length).toBeLessThanOrEqual(MAX_FRACTURE_SEGMENTS);
    expect(a.segments.length).toBeGreaterThan(30);
    expect(a.a).toHaveLength(MAX_FRACTURE_SEGMENTS * 4);
    expect(a.a.every(Number.isFinite)).toBe(true);
    expect(
      getFractureNetwork({ ...settings, seed: settings.seed + 1 }).a,
    ).not.toEqual(a.a);
    expect(getFractureNetwork({ ...settings, crackSpacing: 75 }).a).not.toEqual(
      getFractureNetwork({ ...settings, crackSpacing: 95 }).a,
    );
  });
  it("anchors every endpoint on the base and gives each slit an orthogonal local frame", () => {
    for (const p of getFractureNetwork(settings).segments) {
      expect(Math.abs(stoneBase(p.a, settings))).toBeLessThan(0.000002);
      expect(Math.abs(stoneBase(p.b, settings))).toBeLessThan(0.000002);
      const delta = p.b.map((v, i) => v - p.a[i]) as Point;
      expect(Math.hypot(...delta)).toBeGreaterThan(0.001);
      expect(Math.abs(dot(delta, p.normal))).toBeLessThan(1e-6);
      expect(Math.hypot(...p.normal)).toBeCloseTo(1, 5);
    }
  });
  it("branches share exact existing vertices and terminate with tapered tips", () => {
    const network = getFractureNetwork(settings),
      branches = network.segments.filter((s) => s.branch);
    expect(branches.length).toBeGreaterThan(0);
    const starts = branches.filter((_, i) => i % 2 === 0);
    for (const b of starts)
      expect(
        network.segments.some(
          (p) =>
            !p.branch &&
            p.parent === b.parent &&
            p.a.every((v, i) => v === b.a[i]),
        ),
      ).toBe(true);
    for (const b of branches) expect(b.widthB).toBeLessThan(b.widthA);
    expect(
      getFractureNetwork({ ...settings, crackBranching: 0 }).segments.every(
        (s) => !s.branch,
      ),
    ).toBe(true);
  });
  it("has irregular flared mouths, narrower depth profiles, and a local depth bound", () => {
    const s = { ...settings, crackChipping: 1 },
      segment = getFractureNetwork(s).segments[4];
    const mid = segment.a.map((v, i) => (v + segment.b[i]) * 0.5) as Point;
    const face = fractureDistance(mid, 0, s, 0),
      plain = fractureDistance(mid, 0, { ...s, crackChipping: 0 }, 0);
    expect(face).toBeLessThan(plain);
    const depth = s.crackDepth * 0.001 * segment.depth;
    const p = mid.map((v, i) => v - segment.normal[i] * depth * 0.9) as Point;
    const deep = fractureDistance(p, -depth * 0.9, s, 0);
    expect(deep).toBeGreaterThan(face);
    const far = mid.map((v, i) => v - segment.normal[i] * 0.08) as Point;
    expect(fractureDistance(far, -0.08, s, 0)).toBeGreaterThan(0);
  });
  it("modifies the already-detailed signed surface, not only the smooth form", () => {
    const root = getFractureNetwork(slate).segments[5];
    const n = root.normal;
    const center = root.a;
    const uncut = { ...slate, crackDepth: 0, porosity: 0 };
    let lo = -0.025,
      hi = 0.025;
    for (let i = 0; i < 25; i++) {
      const t = (lo + hi) * 0.5,
        p = center.map((v, k) => v + n[k] * t) as Point;
      if (stoneField(p, uncut, 0) > 0) hi = t;
      else lo = t;
    }
    const surface = center.map((v, k) => v + n[k] * (lo + hi) * 0.5) as Point;
    expect(Math.abs(stoneField(surface, uncut, 0))).toBeLessThan(0.00001);
    expect(stoneField(surface, { ...slate, porosity: 0 }, 0)).toBeGreaterThan(
      0.00001,
    );
  });
});
describe("organic vesicle geometry and compatibility", () => {
  const radius = (angle: number, irregularity: number) => {
    let lo = 0,
      hi = 2;
    const axes: Point = [0.5, 0.75, 1];
    for (let i = 0; i < 40; i++) {
      const r = (lo + hi) * 0.5,
        p: Point = [Math.cos(angle) * r, Math.sin(angle) * r, 0];
      if (poreShape(p, 1, axes, [0.7, 0.9], irregularity) > 0) hi = r;
      else lo = r;
    }
    return (lo + hi) * 0.5;
  };
  it("zero irregularity returns round pores; organic pores are not merely circles or centered ellipses", () => {
    const round = Array.from({ length: 24 }, (_, i) =>
      radius((i * Math.PI) / 12, 0),
    );
    for (const r of round) expect(r).toBeCloseTo(1, 8);
    const organic = Array.from({ length: 24 }, (_, i) =>
      radius((i * Math.PI) / 12, 1),
    );
    expect(Math.max(...organic) / Math.min(...organic)).toBeGreaterThan(1.25);
    expect(
      Math.max(
        ...organic.slice(0, 12).map((r, i) => Math.abs(r - organic[i + 12])),
      ),
    ).toBeGreaterThan(0.04);
  });
  it("migrates v1 recipes with defaults and validates the new structure controls", () => {
    const old: Record<string, unknown> = { ...settings };
    for (const key of [
      "layerBreakup",
      "crackBranching",
      "crackChipping",
      "poreIrregularity",
    ])
      delete old[key];
    const loaded = validateStoneRecipe({
      kind: "frontier-sdf-stone",
      version: 1,
      settings: old,
    });
    expect(loaded.poreIrregularity).toBe(DEFAULT_STONE.poreIrregularity);
    expect(loaded.seed).toBe(settings.seed);
    expect(JSON.parse(encodeStoneRecipe(loaded)).version).toBe(2);
    expect(() =>
      validateStoneRecipe({
        kind: "frontier-sdf-stone",
        version: 2,
        settings: old,
      }),
    ).toThrow();
    for (const key of [
      "layerBreakup",
      "crackBranching",
      "crackChipping",
      "poreIrregularity",
    ])
      expect(() =>
        validateStoneRecipe({
          kind: "frontier-sdf-stone",
          version: 2,
          settings: { ...settings, [key]: 2 },
        }),
      ).toThrow();
  });
  it("keeps component step bounds finite across every preset", () => {
    for (const p of STONE_PRESETS)
      for (const fp of [0, 0.0002, 0.001, 0.003]) {
        const b = fieldBounds(p.values, fp);
        for (const x of Object.values(b)) {
          expect(Number.isFinite(x)).toBe(true);
          expect(x).toBeGreaterThanOrEqual(1);
        }
        const point = projectStone([0.7, 0.3, 0.5], p.values);
        expect(Number.isFinite(stoneField(point, p.values, fp))).toBe(true);
      }
  });
});

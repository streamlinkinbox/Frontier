import { describe, it, expect } from "vitest";
import {
  DEFAULT_STONE,
  STONE_PRESETS,
  LAB_VIEWS,
  detailWeight,
  detailEnvelope,
  slopeBound,
  encodeStoneRecipe,
  validateStoneRecipe,
} from "../model";
import {
  stoneHash,
  stoneNoise,
  stoneBase,
  stoneField,
  stoneNormal,
  type Point,
} from "../field";
import { stoneFragment } from "../shader";
const s = { ...DEFAULT_STONE };
function surface(direction: Point, settings = s): Point {
  const length = Math.hypot(...direction),
    n = direction.map((v) => v / length) as Point;
  let previous = 0.48;
  for (let r = 0.48; r > 0; r -= 0.001) {
    const p = n.map((v) => v * r) as Point;
    if (stoneField(p, settings) <= 0) {
      let a = r,
        b = previous;
      for (let i = 0; i < 16; i++) {
        const m = (a + b) * 0.5;
        if (stoneField(n.map((v) => v * m) as Point, settings) > 0) b = m;
        else a = m;
      }
      return n.map((v) => v * (a + b) * 0.5) as Point;
    }
    previous = r;
  }
  throw new Error("No surface");
}
describe("geometric stone field", () => {
  it("uses deterministic bounded lattice noise", () => {
    for (let i = -40; i < 40; i++) {
      const h = stoneHash(i, 8, -9, 214);
      expect(h).toBe(stoneHash(i, 8, -9, 214));
      expect(h).toBeGreaterThanOrEqual(0);
      expect(h).toBeLessThanOrEqual(1);
      const n = stoneNoise([i * 0.37, 0.6, -0.13], s.seed);
      expect(n).toBeGreaterThanOrEqual(-1);
      expect(n).toBeLessThanOrEqual(1);
    }
    expect(stoneHash(1, 2, 3, 5)).not.toBe(stoneHash(1, 2, 3, 6));
  });
  it("keeps the single organic form bounded and clipped at its support plane", () => {
    for (const preset of STONE_PRESETS) {
      expect(stoneBase([0, 0, 0], preset.values)).toBeLessThan(0);
      for (const p of [
        [0.5, 0, 0],
        [-0.5, 0, 0],
        [0, 0.5, 0],
        [0, 0, -0.5],
        [0, -0.3, 0],
      ] as Point[])
        expect(stoneField(p, preset.values)).toBeGreaterThan(0);
    }
  });
  it("returns the exact smooth field when geometric layers are off", () => {
    const off = {
      ...s,
      chips: 0,
      bedding: 0,
      porosity: 0,
      grain: 0,
      crackDepth: 0,
    };
    for (let i = 0; i < 40; i++) {
      const p: [number, number, number] = [i * 0.013 - 0.25, 0.12, 0.14];
      expect(stoneField(p, off)).toBe(stoneBase(p, off));
      expect(stoneField(p, { ...s, detail: false })).toBe(stoneBase(p, s));
    }
  });
  it("changes actual ray intersections rather than only pigment or bump normals", () => {
    const base = { ...s, detail: false };
    let maximum = 0;
    for (let i = 0; i < 20; i++) {
      const dir: Point = [
        Math.cos(i * 2.399),
        0.15 + i * 0.025,
        Math.sin(i * 2.399),
      ];
      const a = surface(dir, base),
        b = surface(dir, s);
      maximum = Math.max(maximum, Math.hypot(...a.map((v, k) => v - b[k])));
    }
    expect(maximum).toBeGreaterThan(0.002);
  });
  it("palette, exposure and roughness never change the signed geometry", () => {
    const p: Point = [0.212, 0.12, 0.105];
    const a = stoneField(p, s, 0.0002),
      b = stoneField(
        p,
        {
          ...s,
          palette: "fantasy-amethyst",
          roughness: 0.4,
          exposure: 1.7,
          saturation: 0.2,
        },
        0.0002,
      );
    expect(a).toBe(b);
  });
  it("supplies finite geometric normals from the same field", () => {
    for (const preset of STONE_PRESETS) {
      const p = surface([1, 0.5, 0.7], preset.values);
      const n = stoneNormal(p, preset.values, 0.0002);
      expect(n.every(Number.isFinite)).toBe(true);
      expect(Math.hypot(...n)).toBeCloseTo(1, 10);
    }
  });
  it("filters only unresolved detail, and keeps finite conservative step bounds", () => {
    expect(detailWeight(0, 0.001)).toBe(1);
    expect(detailWeight(0.002, 0.001)).toBe(0);
    for (const preset of STONE_PRESETS) {
      expect(detailEnvelope(preset.values, 0)).toBeGreaterThanOrEqual(
        detailEnvelope(preset.values, 0.002),
      );
      expect(slopeBound(preset.values, 0)).toBeGreaterThan(1);
      expect(Number.isFinite(slopeBound(preset.values, 0.001))).toBe(true);
    }
    expect(detailEnvelope({ ...s, detail: false }, 0)).toBe(0);
  });
  it("defines no texture-driven geometry or normal map sampler", () => {
    expect(stoneFragment.match(/uniform sampler2D/g)).toHaveLength(1);
    expect(stoneFragment).toContain("uniform sampler2D uPalette");
    expect(stoneFragment).toContain("detailStone(p");
    expect(stoneFragment).not.toContain("sampler3D");
  });
});
describe("isolated stone recipes", () => {
  it("round trips every specimen and inspection view", () => {
    for (const preset of STONE_PRESETS)
      for (const view of LAB_VIEWS) {
        const settings = { ...preset.values, view };
        expect(
          validateStoneRecipe(JSON.parse(encodeStoneRecipe(settings))),
        ).toEqual(settings);
      }
  });
  it("rejects unsupported files, arbitrary assets and invalid values", () => {
    expect(() => validateStoneRecipe({})).toThrow();
    for (const patch of [
      { seed: 1.5 },
      { grain: Infinity },
      { crackWidth: 0 },
      { porosity: 2 },
      { palette: "custom" },
      { palette: "https://example.com/map.png" },
      { view: "unknown" },
      { quality: "infinite" },
      { detail: "true" },
      { form: null },
    ])
      expect(() =>
        validateStoneRecipe({
          kind: "frontier-sdf-stone",
          version: 1,
          settings: { ...s, ...patch },
        }),
      ).toThrow();
  });
  it("stores compact geometry parameters, not meshes, voxel grids or image maps", () => {
    const json = encodeStoneRecipe(s);
    expect(json.length).toBeLessThan(2000);
    expect(json).not.toContain("satmapDetailMap");
    expect(json).not.toContain("data:image");
  });
});

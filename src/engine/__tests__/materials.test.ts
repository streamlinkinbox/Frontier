import { describe, it, expect } from "vitest";
import {
  MATERIAL_PRESETS,
  colorToLinear,
  dielectricF0,
  featureVisibility,
  materialMacroColor,
} from "../materials";
import {
  dielectricFresnel,
  ggxBRDF,
  transmittance,
  waterRayBias,
  waterEdgeRadiance,
} from "../optics";
import { DEFAULT_SETTINGS } from "../types";
import { validateSettings } from "../project";
import { extractMesh, buildGLB } from "../mesh";

describe("researched dielectric material controls", () => {
  it("decodes authored sRGB colors into linear lighting values", () => {
    expect(colorToLinear("#808080")[0]).toBeCloseTo(0.21586, 4);
    expect(colorToLinear("#000000")).toEqual([0, 0, 0]);
    expect(colorToLinear("#ffffff")).toEqual([1, 1, 1]);
    expect(dielectricF0(1.5)).toBeCloseTo(0.04, 8);
  });
  it("has distinct authored grain structures, not only four tints", () => {
    const [sand, lime, granite, basalt] = MATERIAL_PRESETS;
    expect(granite.values.materialGrain).toBeGreaterThan(
      sand.values.materialGrain * 5,
    );
    expect(basalt.values.materialGrain).toBeLessThan(
      granite.values.materialGrain / 10,
    );
    expect(granite.values.materialBedding).toBe(0);
    expect(basalt.values.materialBedding).toBe(0);
    expect(lime.values.materialPorosity).toBeGreaterThan(
      granite.values.materialPorosity,
    );
    for (const preset of MATERIAL_PRESETS) {
      const s = { ...DEFAULT_SETTINGS, material: preset.id, ...preset.values };
      const color = materialMacroColor([3, 7, 11], [0, 1, 0], s);
      expect(
        color.every((c) => Number.isFinite(c) && c >= 0.008 && c <= 0.85),
      ).toBe(true);
    }
  });
  it("averages sub-pixel mineral grains rather than rendering oversize spots", () => {
    expect(featureVisibility(0.01, 0.0006)).toBe(0);
    expect(featureVisibility(0.00005, 0.006)).toBe(1);
  });
  it("keeps the dielectric BRDF reciprocal, finite and bounded in a rough white-furnace quadrature", () => {
    for (const roughness of [0.35, 0.65, 1])
      for (const noV of [0.15, 0.5, 1]) {
        let reflected = 0;
        const n = 120,
          m = 160;
        for (let y = 0; y < n; y++)
          for (let x = 0; x < m; x++) {
            const noL = (y + 0.5) / n,
              phi = ((x + 0.5) / m) * Math.PI * 2;
            const vl =
              Math.sqrt(1 - noV * noV) *
                Math.sqrt(1 - noL * noL) *
                Math.cos(phi) +
              noV * noL;
            const norm = Math.sqrt(2 + 2 * vl),
              noH = (noV + noL) / norm,
              voH = (1 + vl) / norm;
            const value = ggxBRDF(noV, noL, noH, voH, roughness, 0.04, 1);
            expect(Number.isFinite(value)).toBe(true);
            reflected += (value * noL * 2 * Math.PI) / (n * m);
          }
        expect(reflected).toBeLessThan(1.04);
        expect(reflected).toBeGreaterThan(0.1);
      }
    expect(ggxBRDF(0.4, 0.7, 0.8, 0.65, 0.5, 0.04, 0.4)).toBeCloseTo(
      ggxBRDF(0.7, 0.4, 0.8, 0.65, 0.5, 0.04, 0.4),
      10,
    );
  });
  it("round-trips optional new controls and validates authored values", () => {
    const old: any = { ...DEFAULT_SETTINGS };
    for (const key of Object.keys(old))
      if (key.startsWith("material")) delete old[key];
    expect(validateSettings(old).material).toBe("sandstone");
    expect(() =>
      validateSettings({ ...DEFAULT_SETTINGS, material: "chrome" }),
    ).toThrow();
    expect(() =>
      validateSettings({ ...DEFAULT_SETTINGS, materialColor: "url(secret)" }),
    ).toThrow();
    expect(() =>
      validateSettings({ ...DEFAULT_SETTINGS, materialGrain: 0 }),
    ).toThrow();
  });
});

describe("water at a solid boundary", () => {
  it("has no absorption at zero thickness and continuous thin-layer transmittance", () => {
    expect(transmittance(0, 3)).toBe(1);
    expect(transmittance(0.001, 0.375)).toBeGreaterThan(0.999);
    expect(transmittance(8, 0.375)).toBeLessThan(0.05);
  });
  it("cannot offset an entry ray through a nearby bank", () => {
    const clearance = 0.02;
    expect(clearance - 0.1).toBeLessThan(0); // Previous fixed offset started in solid rock.
    expect(clearance - waterRayBias(clearance)).toBeGreaterThan(0);
    expect(waterRayBias(0)).toBe(0);
  });
  it("keeps partial underwater coverage continuous instead of shortening the opaque path", () => {
    const opaque = waterEdgeRadiance(0.5, 0.5, 0, 6, 0.4, 0.3);
    expect(waterEdgeRadiance(0.5, 0.5, 0.0001, 6, 0.4, 0.3)).toBeCloseTo(
      opaque,
      3,
    );
    expect(opaque).toBeCloseTo(0.5 * Math.exp(-1.8), 8);
  });
  it("uses the correct air/water IOR and total internal reflection below water", () => {
    expect(dielectricFresnel(1, 1, 1.333)).toBeCloseTo(0.02037, 4);
    expect(dielectricFresnel(0.3, 1.333, 1)).toBe(1);
    expect(dielectricFresnel(0.3, 1, 1.333)).toBeLessThan(1);
  });
});

it("exports selected material roughness and dielectric IOR, not hard-coded sandstone", () => {
  const size = { x: 8, y: 8, z: 8 },
    data = new Float32Array(8 * 8 * 8 * 4);
  for (let z = 0; z < 8; z++)
    for (let y = 0; y < 8; y++)
      for (let x = 0; x < 8; x++)
        data[((z * 8 + y) * 8 + x) * 4] =
          Math.hypot(x - 3.5, y - 3.5, z - 3.5) - 2;
  const s = {
    ...DEFAULT_SETTINGS,
    material: "granite" as const,
    ...MATERIAL_PRESETS[2].values,
    materialRoughness: 0.32,
    materialIOR: 1.62,
  };
  const mesh = extractMesh(data, size, undefined, s),
    glb = buildGLB(mesh),
    view = new DataView(glb);
  const doc = JSON.parse(
    new TextDecoder().decode(new Uint8Array(glb, 20, view.getUint32(12, true))),
  );
  expect(doc.materials[0].name).toBe("Granite");
  expect(doc.materials[0].pbrMetallicRoughness.roughnessFactor).toBe(0.32);
  expect(doc.materials[0].pbrMetallicRoughness.metallicFactor).toBe(0);
  expect(doc.materials[0].extensions.KHR_materials_ior.ior).toBe(1.62);
});

import { describe, expect, it } from "vitest";
import { organicArch, rockStack, terrainSdf } from "../field";
import { DEFAULT_SETTINGS } from "../types";
import { validateSettings } from "../project";
const seed = 47021 % 8192;
function area(y: number, index = 0) {
  let n = 0;
  for (let x = -14; x < 14; x += 0.4)
    for (let z = -14; z < 14; z += 0.4)
      if (rockStack(x, y, z, 24, 5.5, index, seed) < 0) n++;
  return n;
}

describe("organic rock formations", () => {
  it("hoodoos have caprock wider than the weathered neck instead of tapering to a cone", () => {
    expect(area(22.2)).toBeGreaterThan(area(17.3) * 1.8);
    expect(area(24.0)).toBeGreaterThan(60);
  });
  it("produces different cross-sections for hoodoos, fins and broken stacks", () => {
    expect(Math.abs(area(12, 0) - area(12, 1))).toBeGreaterThan(15);
    expect(Math.abs(area(12, 1) - area(12, 2))).toBeGreaterThan(15);
  });
  it("retains a real open arch while breaking mirror symmetry", () => {
    expect(organicArch(0, 12, 0, seed)).toBeGreaterThan(1);
    expect(organicArch(0, 30, 0, seed)).toBeLessThan(0);
    let difference = 0;
    for (let y = 3; y < 34; y += 3)
      for (let x = 4; x < 32; x += 4)
        difference += Math.abs(
          organicArch(x, y, 0, seed) - organicArch(-x, y, 0, seed),
        );
    expect(difference).toBeGreaterThan(100);
  });
  it("keeps formations finite, bounded and deterministic for multiple seeds", () => {
    for (const preset of ["arches", "badlands"] as const)
      for (const s of [0, 17, 47021, 999999])
        for (let k = 0; k < 100; k++) {
          const p = [
            ((k * 13) % 96) - 48,
            ((k * 7) % 48) - 10,
            ((k * 19) % 96) - 48,
          ] as [number, number, number];
          const v = terrainSdf(...p, { preset, seed: s });
          expect(Number.isFinite(v)).toBe(true);
          expect(v).toBe(terrainSdf(...p, { preset, seed: s }));
        }
  });
  it("loads old saved projects without a detail slider and validates new detail values", () => {
    const old = { ...DEFAULT_SETTINGS } as Record<string, unknown>;
    delete old.detail;
    expect(validateSettings(old).detail).toBe(DEFAULT_SETTINGS.detail);
    expect(validateSettings({ ...DEFAULT_SETTINGS, detail: 1.4 }).detail).toBe(
      1.4,
    );
    expect(() =>
      validateSettings({ ...DEFAULT_SETTINGS, detail: NaN }),
    ).toThrow();
    expect(() =>
      validateSettings({ ...DEFAULT_SETTINGS, detail: 3 }),
    ).toThrow();
  });
});

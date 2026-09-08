import { describe, it, expect } from "vitest";
import originals from "../satmaps/library.json";
import {
  AUTHORED_RECIPES,
  SATMAP_FAMILIES,
  SATMAP_LIBRARY,
  SATELLITE_LIBRARY,
  filterSatmaps,
  getBuiltinSatmap,
  isSatmapId,
  paletteFromStops,
} from "../satmaps/catalog";
import { getSatmap, paletteGradient, samplePalette } from "../satmaps/satmap";
import { hexToBytes } from "../satmaps/pixels";
import { DEFAULT_SETTINGS, type Settings } from "../types";
import { validateSettings, encodeProject, decodeProject } from "../project";

describe("100-map terrain CLUT catalog", () => {
  it("ships 100 distinct named palettes, preserving the original four first", () => {
    expect(SATMAP_LIBRARY).toHaveLength(100);
    expect(AUTHORED_RECIPES).toHaveLength(96);
    for (const key of ["id", "name", "palette"] as const)
      expect(new Set(SATMAP_LIBRARY.map((a) => a[key])).size).toBe(100);
    expect(SATMAP_LIBRARY.slice(0, 4).map((a) => a.id)).toEqual(
      originals.map((a) => a.id),
    );
  });
  it("covers all requested environments with useful variety", () => {
    const counts = Object.fromEntries(
      SATMAP_FAMILIES.map((f) => [
        f.id,
        SATMAP_LIBRARY.filter((a) => a.family === f.id).length,
      ]),
    );
    expect(counts).toEqual({
      desert: 10,
      grassland: 8,
      forest: 10,
      alpine: 8,
      fantasy: 12,
      lake: 8,
      volcanic: 10,
      icelandic: 8,
      beach: 8,
      quarry: 8,
      wetland: 6,
      badlands: 4,
    });
  });
  it("retains byte-identical source palettes and detail", () => {
    expect(SATELLITE_LIBRARY).toHaveLength(4);
    for (const source of originals) {
      const a = getBuiltinSatmap(source.id)!;
      expect(a.palette).toBe(source.palette);
      expect(a.detail).toBe(source.detail);
      expect(a.source).toBe(source.source);
      expect(a.origin).toBe("satellite");
      expect(a.thumbnail).toBe(`satmaps/${source.id}.webp`);
    }
  });
  it("builds exact, correctly sized authored ramps with shared attributed detail", () => {
    for (const recipe of AUTHORED_RECIPES) {
      const a = getBuiltinSatmap(recipe.id)!;
      expect(a.palette).toBe(
        paletteFromStops(
          recipe.colors,
          "knots" in recipe ? recipe.knots : undefined,
        ),
      );
      expect(a.palette).toMatch(/^[0-9a-f]{1536}$/);
      expect(a.detail).toMatch(/^[0-9a-f]{8192}$/);
      const colors = recipe.colors.toLowerCase().split(" ");
      expect(a.palette.slice(0, 6)).toBe(colors[0]);
      expect(a.palette.slice(-6)).toBe(colors.at(-1));
      expect(a.detail).toBe(getBuiltinSatmap(a.detailId)!.detail);
      expect(a.detailSource).toMatch(/^https:\/\/www.usgs.gov\//);
      expect(a.source).toBeUndefined();
      expect(a.thumbnail).toBeUndefined();
      expect(a.credit).toContain("authored");
      expect(paletteGradient(a.palette)).not.toContain("undefined");
    }
    expect(SATMAP_LIBRARY.filter((a) => a.origin === "fantasy")).toHaveLength(
      12,
    );
  });
  it("interpolates authored stops in linear light and validates malformed recipes", () => {
    const ramp = paletteFromStops("000000 ffffff");
    expect(parseInt(ramp.slice(128 * 6, 128 * 6 + 2), 16)).toBe(188);
    for (const [colors, knots] of [
      ["fff bad", undefined],
      ["ff0000", undefined],
      ["ff0000 00ff00", [0, 0]],
      ["ff0000 00ff00", [0, NaN]],
      ["ff0000 00ff00 0000ff", [0, 0.4, 0.3]],
    ] as const)
      expect(() => paletteFromStops(colors, knots)).toThrow();
  });
  it("searches names and tags with case/diacritic folding and combined filters", () => {
    expect(
      filterSatmaps("  MÓSS  ", "forest", "authored").map((a) => a.id),
    ).toContain("forest-mosswood");
    expect(filterSatmaps("copper").map((a) => a.id)).toContain("quarry-copper");
    expect(filterSatmaps("", "all", "satellite")).toHaveLength(4);
    expect(filterSatmaps("", "all", "authored")).toHaveLength(96);
    expect(filterSatmaps("", "forest", "satellite")).toHaveLength(0);
    expect(filterSatmaps("no-such-map-123")).toHaveLength(0);
    expect(filterSatmaps("blue glacier", "alpine").map((a) => a.id)).toContain(
      "alpine-glacier",
    );
  });
  it("accepts every registered ID but rejects arbitrary strings and objects", () => {
    for (const a of SATMAP_LIBRARY) {
      expect(isSatmapId(a.id)).toBe(true);
      expect(
        validateSettings({ ...DEFAULT_SETTINGS, satmap: a.id }).satmap,
      ).toBe(a.id);
      expect(getSatmap({ ...DEFAULT_SETTINGS, satmap: a.id }).name).toBe(
        a.name,
      );
    }
    for (const id of [
      "missing-map",
      "__proto__",
      ["forest-temperate"],
      {},
      42,
      null,
    ]) {
      expect(isSatmapId(id)).toBe(false);
      expect(() =>
        validateSettings({ ...DEFAULT_SETTINGS, satmap: id }),
      ).toThrow();
    }
  });
  it("round-trips catalog IDs without embedding the 100-map library in a project", async () => {
    for (const satmap of [
      "forest-temperate",
      "fantasy-amethyst",
      "quarry-copper",
      "wetland-sedge",
    ] as const) {
      const settings = { ...DEFAULT_SETTINGS, satmap };
      const data = new Float32Array(16 * 8 * 16 * 4);
      const blob = encodeProject({
        version: 1,
        settings,
        data,
        size: { x: 16, y: 8, z: 16 },
        steps: 0,
        savedAt: new Date(0).toISOString(),
      });
      const project = decodeProject(await blob.arrayBuffer());
      expect(project.settings).toEqual(settings);
      expect(getSatmap(project.settings).palette).toBe(
        getBuiltinSatmap(satmap)!.palette,
      );
      expect(blob.size).toBeLessThan(data.byteLength + 8192);
    }
  });
  it("keeps custom palettes and missing-mode migration intact", () => {
    const custom: Settings = {
      ...DEFAULT_SETTINGS,
      satmap: "custom",
      satmapName: "My colors",
      satmapPalette: "00ff66".repeat(256),
      satmapDetailMap: "80".repeat(64 * 64),
    };
    expect(getSatmap(validateSettings(custom))).toMatchObject({
      name: "My colors",
      palette: custom.satmapPalette,
    });
    const old: Record<string, unknown> = { ...DEFAULT_SETTINGS };
    delete old.textureMode;
    expect(validateSettings(old).textureMode).toBe("legacy");
  });
  it("offers genuinely different forest, fantasy and mineral colors in the export sampler", () => {
    const color = (id: string) =>
      samplePalette(hexToBytes(getBuiltinSatmap(id)!.palette), 0.6, 1);
    const forest = color("forest-rainforest"),
      fantasy = color("fantasy-amethyst"),
      quarry = color("quarry-marble");
    expect(forest[1]).toBeGreaterThan(forest[0] * 1.4);
    expect(fantasy[2]).toBeGreaterThan(fantasy[1]);
    expect(quarry.every((v, i) => v > forest[i])).toBe(true);
  });
});

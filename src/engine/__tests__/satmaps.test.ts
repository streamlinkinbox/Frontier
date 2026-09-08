import { describe, it, expect } from "vitest";
import { readFile } from "node:fs/promises";
import sharp from "sharp";
import { DEFAULT_SETTINGS, SATMAP_VIEWS, type Settings } from "../types";
import { decodeProject, encodeProject, validateSettings } from "../project";
import { packUniforms, UNIFORM_FLOATS } from "../uniforms";
import { extractMesh, buildGLB } from "../mesh";
import {
  DETAIL_SIZE,
  PALETTE_SAMPLES,
  bytesToHex,
  detailMipmaps,
  extractDetail,
  extractPalette,
  hexToBytes,
  paletteRGBA,
} from "../satmaps/pixels";
import {
  SATMAP_LIBRARY,
  photoDetail,
  remapSatmap,
  samplePalette,
  terrainFlow,
  textureMask,
  type SatmapMasks,
} from "../satmaps/satmap";
import {
  buildTerrainMaps,
  curvatureAt,
  extractHeightSurface,
  occlusionAt,
} from "../satmaps/terrainMaps";

const settings = { ...DEFAULT_SETTINGS };
const neutral: SatmapMasks = {
  height: 0.1,
  slope: 0.2,
  curvature: -0.3,
  ao: 0.4,
  flow: 0,
  sediment: 0,
  detail: 0.6,
};
const weightsOff = {
  satmapHeight: 0,
  satmapSlope: 0,
  satmapCurvature: 0,
  satmapAO: 0,
  satmapFlow: 0,
  satmapSediment: 0,
  satmapDetail: 0,
};
const size = { x: 16, y: 8, z: 16 };
function plane(height = 2) {
  const data = new Float32Array(size.x * size.y * size.z * 4);
  for (let z = 0; z < size.z; z++)
    for (let y = 0; y < size.y; y++)
      for (let x = 0; x < size.x; x++)
        data[((z * size.y + y) * size.x + x) * 4] =
          -10 + ((y + 0.5) * 48) / size.y - height;
  return data;
}

describe("satellite-derived image data", () => {
  it("extracts ordered horizontal and vertical strips without sorting or losing endpoints", () => {
    const pixels = [255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255];
    const h = extractPalette(pixels, 3, 1, "strip"),
      v = extractPalette(pixels, 1, 3, "strip");
    expect(h).toBe(v);
    expect(h.length).toBe(1536);
    expect(h.slice(0, 6)).toBe("ff0000");
    expect(h.slice(-6)).toBe("0000ff");
  });
  it("orders photographs by luminance, excluding transparent pixels", () => {
    const p = [
      255, 255, 255, 255, 0, 0, 0, 255, 128, 128, 128, 255, 255, 0, 0, 0,
    ];
    const hex = extractPalette(p, 4, 1, "photo"),
      data = hexToBytes(hex);
    for (let i = 0; i < data.length; i += 3) {
      expect(data[i]).toBe(data[i + 1]);
      expect(data[i]).toBe(data[i + 2]);
    }
    expect(data[0]).toBeLessThan(data.at(-3)!);
    expect(() => extractPalette([1, 2, 3, 0], 1, 1, "photo")).toThrow(
      /transparent/,
    );
  });
  it("keeps flat image detail flat and creates a complete deterministic mip chain", () => {
    const detail = extractDetail([70, 80, 90, 255], 1, 1);
    expect(detail).toBe("80".repeat(DETAIL_SIZE ** 2));
    const mips = detailMipmaps(detail);
    expect(mips.map((m) => m.size)).toEqual([64, 32, 16, 8, 4, 2, 1]);
    expect([...mips.at(-1)!.data]).toEqual([128, 128, 128, 255]);
  });
  it("reproduces every shipped CLUT and detail map from its real satellite crop", async () => {
    expect(SATMAP_LIBRARY).toHaveLength(4);
    for (const asset of SATMAP_LIBRARY) {
      const file = await readFile(`public/satmaps/${asset.id}.webp`);
      const { data, info } = await sharp(file)
        .resize(256, 256, { fit: "fill" })
        .ensureAlpha()
        .raw()
        .toBuffer({ resolveWithObject: true });
      expect(extractPalette(data, info.width, info.height, "photo")).toBe(
        asset.palette,
      );
      expect(extractDetail(data, info.width, info.height)).toBe(asset.detail);
      expect(asset.source).toMatch(/^https:\/\/www.usgs.gov\//);
      expect(asset.license).toContain("Public domain");
      expect(new Set(hexToBytes(asset.detail)).size).toBeGreaterThan(64);
      expect(paletteRGBA(asset.palette).length).toBe(PALETTE_SAMPLES * 4);
    }
  });
  it("interpolates in linear RGB, not gamma-encoded sRGB", () => {
    const rgb = new Uint8Array(PALETTE_SAMPLES * 3);
    rgb.fill(255, 128 * 3);
    expect(samplePalette(rgb, 0.5, 1)).toEqual([0.5, 0.5, 0.5]);
    expect(bytesToHex(hexToBytes("ab12ff"))).toBe("ab12ff");
  });
  it("uses mirrored triplanar detail on vertical walls as well as flat ground", () => {
    const detail = hexToBytes(SATMAP_LIBRARY[0].detail);
    const p: [number, number, number] = [1.3, 8.2, -6.1];
    for (const n of [
      [1, 0, 0],
      [0, 1, 0],
      [0, -1, 0],
      [0, 0, 1],
    ] as [number, number, number][]) {
      const value = photoDetail(detail, p, n, 6);
      expect(value).toBeGreaterThanOrEqual(0);
      expect(value).toBeLessThanOrEqual(1);
      expect(
        photoDetail(detail, p.map((v) => v + 12) as typeof p, n, 6),
      ).toBeCloseTo(value, 10);
    }
  });
});

describe("terrain masks, not arbitrary shader noise", () => {
  it("has zero planar curvature and opposite convex/concave signs", () => {
    expect(curvatureAt((p) => p[1], [3, 0, 2], 0.8)).toBeCloseTo(0, 10);
    const sphere = (p: [number, number, number]) => Math.hypot(...p) - 5;
    expect(curvatureAt(sphere, [5, 0, 0], 1)).toBeGreaterThan(0.4);
    expect(curvatureAt((p) => -sphere(p), [5, 0, 0], 1)).toBeLessThan(-0.4);
  });
  it("has open-sky AO on a plane and occlusion under a roof", () => {
    expect(occlusionAt((p) => p[1], [0, 0, 0], [0, 1, 0])).toBe(1);
    expect(
      occlusionAt((p) => Math.min(p[1], 2 - p[1]), [0, 0, 0], [0, 1, 0]),
    ).toBeLessThan(0.7);
  });
  it("extracts the upper envelope and leaves empty columns invalid", () => {
    const surface = extractHeightSurface(plane(2), size);
    expect([...surface.heights].every((v) => Math.abs(v - 2) < 1e-5)).toBe(
      true,
    );
    expect(surface.valid.every((v) => v === 1)).toBe(true);
    const empty = extractHeightSurface(
      new Float32Array(size.x * size.y * size.z * 4).fill(1),
      size,
    );
    expect(empty.valid.some(Boolean)).toBe(false);
    expect(buildTerrainMaps(empty).range).toEqual([-10, 38]);
  });
  it("accumulates ALL upstream D8 contributions without cycles or fabricated flat-land flow", () => {
    const heights = new Float32Array(25);
    for (let y = 0; y < 5; y++)
      for (let x = 0; x < 5; x++) heights[y * 5 + x] = Math.hypot(x - 2, y - 2);
    const maps = buildTerrainMaps({
      width: 5,
      height: 5,
      heights,
      valid: new Uint8Array(25).fill(1),
    });
    expect(maps.accumulation[12]).toBe(25);
    expect(maps.pixels[12 * 4]).toBe(255);
    for (let i = 0; i < 25; i++)
      if (maps.receivers[i] >= 0)
        expect(heights[maps.receivers[i]]).toBeLessThan(heights[i]);
    let sinks = 0;
    maps.receivers.forEach((r, i) => {
      if (r === -1) sinks += maps.accumulation[i];
    });
    expect(sinks).toBe(25);
    const flat = buildTerrainMaps({
      width: 5,
      height: 5,
      heights: new Float32Array(25),
      valid: new Uint8Array(25).fill(1),
    });
    expect(flat.receivers.every((v) => v === -1)).toBe(true);
    expect(
      flat.pixels.filter((_, i) => i % 4 === 0).every((v) => v === 0),
    ).toBe(true);
  });
  it("does not project upper-surface catchments onto lower cave floors or undersides", () => {
    const maps = buildTerrainMaps(extractHeightSurface(plane(20), size));
    for (let i = 0; i < maps.pixels.length; i += 4) maps.pixels[i] = 255;
    expect(terrainFlow(maps, [0, 20, 0], [0, 1, 0], 1)).toBeCloseTo(1);
    expect(terrainFlow(maps, [0, 0, 0], [0, 1, 0], 1)).toBe(0);
    expect(terrainFlow(maps, [0, 20, 0], [0, -1, 0], 1)).toBe(0);
  });
  it("wires each mask weight and gives a finite neutral result with all weights off", () => {
    const s = { ...settings, ...weightsOff };
    expect(textureMask(neutral, s)).toBe(0.5);
    const cases = {
      satmapHeight: 0.1,
      satmapSlope: 0.8,
      satmapCurvature: 0.35,
      satmapAO: 0.4,
      satmapDetail: 0.6,
    };
    for (const [key, value] of Object.entries(cases))
      expect(textureMask(neutral, { ...s, [key]: 1 })).toBeCloseTo(value);
    expect(
      textureMask({ ...neutral, flow: 1 }, { ...s, satmapFlow: 1 }),
    ).toBeCloseTo(0.196);
    expect(
      textureMask({ ...neutral, sediment: 1 }, { ...s, satmapSediment: 1 }),
    ).toBeCloseTo(0.916);
  });
  it("clips, reverses and biases LUT coordinates without going out of range", () => {
    const s = {
      ...settings,
      satmapLow: 0.2,
      satmapHigh: 0.8,
      satmapContrast: 1,
    };
    expect(remapSatmap(0, s)).toBe(0.2);
    expect(remapSatmap(1, s)).toBeCloseTo(0.8);
    expect(remapSatmap(0, { ...s, satmapReverse: true })).toBeCloseTo(0.8);
    expect(remapSatmap(0.5, { ...s, satmapBias: 1 })).toBeGreaterThan(
      remapSatmap(0.5, s),
    );
  });
});

describe("persistence and real mesh export", () => {
  it("preserves pre-SatMap project appearance and validates every new control", () => {
    const old = { ...settings } as Record<string, unknown>;
    for (const key of Object.keys(old))
      if (key.startsWith("satmap") || key === "textureMode") delete old[key];
    expect(validateSettings(old).textureMode).toBe("legacy");
    expect(validateSettings(settings).textureMode).toBe("satmap");
    for (const patch of [
      { textureMode: "unrecognized" },
      { satmap: "gaea-proprietary" },
      { satmap: ["namib"] },
      { satmapPreview: "bad" },
      { satmapLow: 0.8, satmapHigh: 0.2 },
      { satmapSlope: NaN },
      { satmapScale: 0 },
      { satmapRelief: 81 },
      { satmapReverse: 1 },
      { satmap: "custom" },
      { satmapPalette: "url(https://external.example)" },
      { satmapDetailMap: "00" },
      { satmapName: "x".repeat(81) },
    ])
      expect(() => validateSettings({ ...settings, ...patch })).toThrow();
  });
  it("embeds imported palette + detail in portable .frontier files and round trips all previews", async () => {
    const s: Settings = {
      ...settings,
      satmap: "custom",
      satmapName: "Own image",
      satmapPalette: SATMAP_LIBRARY[2].palette,
      satmapDetailMap: SATMAP_LIBRARY[2].detail,
      satmapPreview: "curvature",
      satmapLow: 0.15,
      satmapHigh: 0.9,
      satmapReverse: true,
    };
    const encoded = encodeProject({
      version: 1,
      settings: s,
      size,
      steps: 0,
      savedAt: "",
      data: plane(),
    });
    const saved = decodeProject(await encoded.arrayBuffer());
    expect(saved.settings).toEqual(s);
    expect(saved.data).toEqual(plane());
    for (const view of SATMAP_VIEWS)
      expect(
        validateSettings({ ...s, satmapPreview: view }).satmapPreview,
      ).toBe(view);
  });
  it("packs matching backend uniforms without moving existing material/erosion offsets", () => {
    const packed = packUniforms(
      {
        settings,
        time: 0,
        width: 100,
        height: 100,
        eye: [0, 1, 2],
        forward: [0, 0, -1],
        up: [0, 1, 0],
        right: [1, 0, 0],
        tool: "orbit",
        brush: null,
        compare: false,
      },
      size,
      [2, 26],
    );
    expect(packed.length).toBe(UNIFORM_FLOATS);
    expect(packed[36]).toBeCloseTo(settings.rainfall);
    expect(packed[144]).toBe(1);
    expect(packed[161]).toBe(2);
    expect(packed[162]).toBe(26);
  });
  it("exports satellite colors (not legacy noise colors) with unchanged geometry", () => {
    const data = plane(2),
      original = data.slice();
    const a = extractMesh(data, size, undefined, settings);
    const b = extractMesh(data, size, undefined, {
      ...settings,
      satmap: "iceland",
    });
    expect(a.positions).toEqual(b.positions);
    expect(a.indices).toEqual(b.indices);
    expect(data).toEqual(original);
    expect(a.colors).not.toEqual(b.colors);
    expect(a.colors.every((v) => Number.isFinite(v) && v >= 0 && v <= 1)).toBe(
      true,
    );
    const glb = buildGLB(b),
      view = new DataView(glb);
    const doc = JSON.parse(
      new TextDecoder().decode(
        new Uint8Array(glb, 20, view.getUint32(12, true)),
      ),
    );
    expect(doc.materials[0].name).toContain("Volcanic coast");
    expect(doc.extras.color).toContain("Satellite CLUT");
    expect(doc.meshes[0].primitives[0].attributes.COLOR_0).toBe(2);
  });
});

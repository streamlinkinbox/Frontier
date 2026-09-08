import { SATMAP_LIBRARY, getBuiltinSatmap } from "./catalog";
export { SATMAP_LIBRARY, SATELLITE_LIBRARY } from "./catalog";
import { clamp, mix, smoothstep } from "../math";
import { srgbToLinear } from "../materials";
import { sampleField } from "../field";
import type { Settings, Vec3 } from "../types";
import { hexToBytes, DETAIL_SIZE, PALETTE_SAMPLES } from "./pixels";
import {
  curvatureAt,
  occlusionAt,
  sampleTerrainDistance,
  type TerrainMaps,
} from "./terrainMaps";
import type { VolumeSize } from "../types";

export function getSatmap(settings: Settings) {
  if (
    settings.satmap === "custom" &&
    settings.satmapPalette &&
    settings.satmapDetailMap
  )
    return {
      palette: settings.satmapPalette,
      detail: settings.satmapDetailMap,
      name: settings.satmapName || "Imported palette",
    };
  return getBuiltinSatmap(settings.satmap) ?? SATMAP_LIBRARY[0];
}
export function paletteGradient(palette: string): string {
  const stops: string[] = [];
  for (let i = 0; i <= 16; i++) {
    const offset = Math.round((i / 16) * (PALETTE_SAMPLES - 1)) * 6;
    stops.push(`#${palette.slice(offset, offset + 6)} ${(i * 100) / 16}%`);
  }
  return `linear-gradient(90deg, ${stops.join(",")})`;
}
export interface SatmapMasks {
  height: number;
  slope: number;
  curvature: number;
  ao: number;
  flow: number;
  sediment: number;
  detail: number;
}
/** Keep equations in sync with shaders/satmap.wgsl (pixel parity tested). */
export function textureMask(m: SatmapMasks, s: Settings): number {
  const total =
    s.satmapHeight +
    s.satmapSlope +
    s.satmapCurvature +
    s.satmapAO +
    s.satmapDetail;
  let value =
    total > 1e-5
      ? (m.height * s.satmapHeight +
          (1 - m.slope) * s.satmapSlope +
          (m.curvature * 0.5 + 0.5) * s.satmapCurvature +
          m.ao * s.satmapAO +
          m.detail * s.satmapDetail) /
        total
      : 0.5;
  value = mix(value, 0.1 + m.detail * 0.16, m.flow * s.satmapFlow);
  value = mix(value, 0.82 + m.detail * 0.16, m.sediment * s.satmapSediment);
  return clamp(value, 0, 1);
}
export function remapSatmap(value: number, s: Settings): number {
  let t = Math.pow(
    clamp((value - 0.5) * s.satmapContrast + 0.5, 0, 1),
    2 ** (-s.satmapBias * 2),
  );
  if (s.satmapReverse) t = 1 - t;
  return mix(s.satmapLow, s.satmapHigh, t);
}
export function samplePalette(
  palette: Uint8Array,
  t: number,
  saturation: number,
): Vec3 {
  const p = clamp(t, 0, 1) * (PALETTE_SAMPLES - 1),
    a = Math.floor(p),
    b = Math.min(a + 1, PALETTE_SAMPLES - 1);
  const color = [0, 1, 2].map((c) =>
    mix(
      srgbToLinear(palette[a * 3 + c] / 255),
      srgbToLinear(palette[b * 3 + c] / 255),
      p - a,
    ),
  ) as Vec3;
  const luma = color[0] * 0.2126 + color[1] * 0.7152 + color[2] * 0.0722;
  return color.map((c) => clamp(mix(luma, c, saturation), 0, 1)) as Vec3;
}
export function sampleBilinear(
  data: Uint8Array,
  width: number,
  height: number,
  channels: number,
  u: number,
  v: number,
  channel = 0,
): number {
  const x = u * width - 0.5,
    y = v * height - 0.5,
    ix = Math.floor(x),
    iy = Math.floor(y);
  const read = (a: number, b: number) =>
    data[
      (clamp(b, 0, height - 1) * width + clamp(a, 0, width - 1)) * channels +
        channel
    ] / 255;
  return mix(
    mix(read(ix, iy), read(ix + 1, iy), x - ix),
    mix(read(ix, iy + 1), read(ix + 1, iy + 1), x - ix),
    y - iy,
  );
}
const mirror = (v: number) => 1 - Math.abs((v / 2 - Math.floor(v / 2)) * 2 - 1);
export function photoDetail(
  detail: Uint8Array,
  p: Vec3,
  n: Vec3,
  scale: number,
): number {
  const w = n.map((v) => Math.abs(v) ** 4),
    total = Math.max(1e-6, w[0] + w[1] + w[2]);
  const q = p.map((v) => v / scale);
  const read = (u: number, v: number) =>
    sampleBilinear(detail, DETAIL_SIZE, DETAIL_SIZE, 1, mirror(u), mirror(v));
  return (
    (read(q[2] + 0.17, q[1] + 0.31) * w[0] +
      read(q[0], q[2]) * w[1] +
      read(q[0] + 0.43, q[1] + 0.71) * w[2]) /
    total
  );
}
export function terrainFlow(
  maps: TerrainMaps,
  p: Vec3,
  n: Vec3,
  cell: number,
): number {
  const u = (p[0] + 48) / 96,
    v = (p[2] + 48) / 96;
  const read = (c: number) =>
    sampleBilinear(maps.pixels, maps.width, maps.height, 4, u, v, c);
  const height = -10 + ((read(1) * 256 + read(2)) / 257) * 48;
  return (
    read(0) *
    read(3) *
    (1 - smoothstep(cell * 1.5, cell * 4, Math.abs(p[1] - height))) *
    smoothstep(-0.05, 0.65, n[1])
  );
}
/** Vertex-color export uses the same CLUT and data inputs as the viewport.
 * Micro-bump remains shader-only; AO here selects colors, not baked lighting. */
export function createSatmapSampler(
  data: Float32Array,
  size: VolumeSize,
  maps: TerrainMaps,
  settings: Settings,
) {
  const asset = getSatmap(settings),
    palette = hexToBytes(asset.palette),
    detail = hexToBytes(asset.detail);
  const sample = (p: Vec3) => sampleTerrainDistance(data, size, p),
    cell = 96 / size.x;
  return (p: Vec3, n: Vec3): Vec3 => {
    const q = p.map((v, i) => v + n[i] * cell * 0.22) as Vec3;
    const stateWater = sampleField(data, size, q, 1),
      displacement = sampleField(data, size, q, 3);
    const masks: SatmapMasks = {
      height: clamp(
        (p[1] - maps.range[0]) / (maps.range[1] - maps.range[0]),
        0,
        1,
      ),
      slope: 1 - clamp(n[1], 0, 1),
      curvature: settings.satmapCurvature
        ? curvatureAt(sample, p, Math.max(cell * 1.4, 0.8))
        : 0,
      ao: settings.satmapAO ? occlusionAt(sample, p, n) : 1,
      flow: Math.max(
        terrainFlow(maps, p, n, cell),
        clamp(stateWater * 3, 0, 1),
      ),
      sediment:
        clamp(-displacement / 0.65, 0, 1) * smoothstep(0.05, 0.75, n[1]),
      detail: photoDetail(detail, p, n, settings.satmapScale),
    };
    return samplePalette(
      palette,
      remapSatmap(textureMask(masks, settings), settings),
      settings.satmapSaturation,
    );
  };
}

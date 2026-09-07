import { noise } from "./field";
import { clamp, smoothstep } from "./math";
import type { Settings, VolumeSize } from "./types";

/** A small, seeded hydraulic roughness potential; it ROUTES water rather than
 * directly carving painted lines. It is coherent down a rock face, not a heightmap. */
export function runoffPotential(
  x: number,
  y: number,
  z: number,
  seed: number,
): number {
  return (
    noise(x * 0.46 + seed * 0.013, y * 0.035, z * 0.46) * 0.72 +
    noise(x * 0.18, y * 0.06 + 11.2, z * 0.18 + seed * 0.007) * 0.28
  );
}
export function windBand(
  x: number,
  y: number,
  z: number,
  dx: number,
  dz: number,
  seed: number,
): number {
  const along = x * dx + z * dz,
    cross = -x * dz + z * dx;
  const phase =
    y * 1.55 +
    noise(cross * 0.14 + seed * 0.003, y * 0.09, along * 0.025) * 1.65;
  return smoothstep(0.25, 0.85, 0.5 + 0.5 * Math.sin(phase));
}
export function windExposure(
  data: Float32Array,
  size: VolumeSize,
  x: number,
  y: number,
  z: number,
  nx: number,
  ny: number,
  nz: number,
  dx: number,
  dz: number,
): number {
  const h = 96 / size.x;
  let visibility = 1;
  for (let step = 0; step < 288; step++) {
    const distance = 1 + step * 0.75;
    const xx = Math.floor(x + nx * 0.8 - dx * distance + 0.5),
      yy = Math.floor(y + ny * 0.8 + 0.5),
      zz = Math.floor(z + nz * 0.8 - dz * distance + 0.5);
    if (
      xx < 0 ||
      xx >= size.x ||
      yy < 0 ||
      yy >= size.y ||
      zz < 0 ||
      zz >= size.z
    )
      break;
    visibility *= smoothstep(
      -h * 0.25,
      h * 0.25,
      data[((zz * size.y + yy) * size.x + xx) * 4],
    );
    if (visibility < 0.001) break;
  }
  return visibility;
}
export function windCutRate(
  p: [number, number, number],
  normal: [number, number, number],
  s: Settings,
  wet: number,
  exposure: number,
): number {
  const angle = (s.windDirection * Math.PI) / 180,
    dx = Math.cos(angle),
    dz = Math.sin(angle);
  const facing = Math.max(0, -normal[0] * dx - normal[2] * dz);
  const dry = 1 - clamp(wet * 3, 0, 1);
  const aboveWater = !s.water || p[1] > s.waterLevel + 0.25 ? 1 : 0;
  const grain = windBand(...p, dx, dz, s.seed);
  const height = Math.exp(-Math.max(p[1] + 2, 0) / 22);
  return (
    s.windErosion *
    s.windErosion *
    facing *
    exposure *
    dry *
    aboveWater *
    grain *
    height
  );
}

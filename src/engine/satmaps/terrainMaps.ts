import type { Vec3, VolumeSize } from "../types";
import { sampleField } from "../field";
import { clamp } from "../math";

export interface HeightSurface {
  width: number;
  height: number;
  heights: Float32Array;
  valid: Uint8Array;
}
export interface TerrainMaps extends HeightSurface {
  pixels: Uint8Array;
  range: [number, number];
  accumulation: Float32Array;
  receivers: Int32Array;
}
/** Upper envelope ONLY for drainage. The renderer still uses the full SDF for
 * normals, curvature, AO and actual sediment, including caves and overhangs. */
export function extractHeightSurface(
  data: Float32Array,
  size: VolumeSize,
): HeightSurface {
  const heights = new Float32Array(size.x * size.z).fill(-10);
  const valid = new Uint8Array(heights.length);
  const dy = 48 / size.y;
  for (let z = 0; z < size.z; z++)
    for (let x = 0; x < size.x; x++) {
      for (let y = size.y - 1; y >= 0; y--) {
        const i = ((z * size.y + y) * size.x + x) * 4;
        if (data[i] >= 0) continue;
        const j = z * size.x + x;
        if (y === size.y - 1) heights[j] = 38;
        else {
          const above = data[i + size.x * 4];
          const t = clamp(-data[i] / Math.max(above - data[i], 1e-6), 0, 1);
          heights[j] = -10 + (y + 0.5 + t) * dy;
        }
        valid[j] = 1;
        break;
      }
    }
  return { width: size.x, height: size.z, heights, valid };
}

/** Deterministic D8 steepest-descent contributing area. Descending height order
 * is a topological order, so ALL upstream contributions arrive in a single pass.
 * Flats / closed depressions remain sinks (no invented drainage through rock).
 * Unlike the animated river route, this map is derived from the edited volume. */
export function buildTerrainMaps(surface: HeightSurface): TerrainMaps {
  const { width, height, heights, valid } = surface;
  const n = width * height;
  const receivers = new Int32Array(n).fill(-1);
  const accumulation = new Float32Array(n);
  const order: number[] = [];
  let low = Infinity,
    high = -Infinity;
  for (let i = 0; i < n; i++) {
    if (!valid[i]) continue;
    order.push(i);
    accumulation[i] = 1;
    low = Math.min(low, heights[i]);
    high = Math.max(high, heights[i]);
    const x = i % width,
      y = Math.floor(i / width);
    let steepest = 1e-6;
    for (let dy = -1; dy <= 1; dy++)
      for (let dx = -1; dx <= 1; dx++) {
        if (
          (!dx && !dy) ||
          x + dx < 0 ||
          x + dx >= width ||
          y + dy < 0 ||
          y + dy >= height
        )
          continue;
        const j = (y + dy) * width + x + dx;
        if (!valid[j]) continue;
        const gradient =
          (heights[i] - heights[j]) /
          Math.hypot((dx * 96) / width, (dy * 96) / height);
        if (gradient > steepest) {
          steepest = gradient;
          receivers[i] = j;
        }
      }
  }
  order.sort((a, b) => heights[b] - heights[a] || a - b);
  for (const i of order)
    if (receivers[i] >= 0) accumulation[receivers[i]] += accumulation[i];
  const pixels = new Uint8Array(n * 4);
  const scale = Math.log2(Math.max(2, order.length));
  for (let i = 0; i < n; i++) {
    const encodedHeight = Math.round(
      clamp((heights[i] + 10) / 48, 0, 1) * 65535,
    );
    pixels[i * 4] = Math.round(
      clamp(Math.log2(Math.max(1, accumulation[i])) / scale, 0, 1) * 255,
    );
    pixels[i * 4 + 1] = encodedHeight >> 8;
    pixels[i * 4 + 2] = encodedHeight & 255;
    pixels[i * 4 + 3] = valid[i] ? 255 : 0;
  }
  const range: [number, number] = order.length
    ? [low, Math.max(low + 1, high)]
    : [-10, 38];
  return { ...surface, pixels, range, accumulation, receivers };
}

export function sampleTerrainDistance(
  data: Float32Array,
  size: VolumeSize,
  p: Vec3,
): number {
  const q = [
    Math.abs(p[0]) - 48,
    Math.abs(p[1] - 14) - 24,
    Math.abs(p[2]) - 48,
  ];
  const boundary =
    Math.hypot(Math.max(q[0], 0), Math.max(q[1], 0), Math.max(q[2], 0)) +
    Math.min(Math.max(...q), 0);
  return Math.max(sampleField(data, size, p), boundary);
}
/** Signed, scale-normalized Laplacian/gradient curvature proxy. Plane = 0,
 * convex = positive, concave = negative. Six SDF samples, no color noise. */
export function curvatureAt(
  sample: (p: Vec3) => number,
  p: Vec3,
  radius: number,
): number {
  let laplacian = -6 * sample(p);
  const gradient = [0, 0, 0];
  for (let axis = 0; axis < 3; axis++) {
    const a = [...p] as Vec3,
      b = [...p] as Vec3;
    a[axis] += radius;
    b[axis] -= radius;
    const hi = sample(a),
      lo = sample(b);
    laplacian += hi + lo;
    gradient[axis] = (hi - lo) / (2 * radius);
  }
  return clamp(
    (laplacian / (radius * Math.max(Math.hypot(...gradient), 0.15))) * 1.2,
    -1,
    1,
  );
}
export function occlusionAt(
  sample: (p: Vec3) => number,
  p: Vec3,
  normal: Vec3,
): number {
  let occlusion = 0,
    weight = 0.65;
  for (let i = 0; i < 5; i++) {
    const h = 0.6 + i * 1.45;
    occlusion +=
      Math.max(0, h - sample(p.map((v, a) => v + normal[a] * h) as Vec3)) *
      weight;
    weight *= 0.52;
  }
  return clamp(1 - occlusion * 0.32, 0.23, 1);
}

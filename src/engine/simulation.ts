import { clamp, smoothstep, mix } from "./math";
import { noise } from "./field";
import type { Settings, VolumeSize } from "./types";

/** Low-resolution CPU reference/fallback for the same 3D finite-volume solver.
 * Flux is stored on positive faces. Each shared face has exactly one signed
 * flow, so transport is conservative before rainfall, evaporation and drains.
 * The 1/6 donor limiter prevents a cell exporting more fluid than it contains.
 */
export function computeFlux(
  data: Float32Array,
  size: VolumeSize,
  flux: Float32Array,
): void {
  const { x: sx, y: sy, z: sz } = size,
    cell = 96 / sx;
  const offsets = [1, sx, sx * sy];
  for (let z = 0; z < sz; z++)
    for (let y = 0; y < sy; y++)
      for (let x = 0; x < sx; x++) {
        const i = (z * sy + y) * sx + x,
          a = i * 4;
        for (let axis = 0; axis < 3; axis++) {
          if ([x, y, z][axis] === [sx, sy, sz][axis] - 1) {
            flux[i * 3 + axis] = 0;
            continue;
          }
          const b = (i + offsets[axis]) * 4;
          const raw =
            (data[a + 1] - data[b + 1] - (axis === 1 ? 0.55 : 0)) * 0.3;
          flux[i * 3 + axis] =
            clamp(raw, -data[b + 1] / 6, data[a + 1] / 6) *
            Math.min(
              smoothstep(-0.12 * cell, 0.65 * cell, data[a]),
              smoothstep(-0.12 * cell, 0.65 * cell, data[b]),
            );
        }
      }
}
export function evolveField(
  data: Float32Array,
  size: VolumeSize,
  s: Settings,
  flux: Float32Array,
  out: Float32Array,
): void {
  const { x: sx, y: sy, z: sz } = size,
    cell = 96 / sx;
  const offsets = [4, sx * 4, sx * sy * 4];
  const concentration = (i: number) =>
    Math.min(data[i + 2] / Math.max(data[i + 1], 0.00001), 3);
  const sedimentFlux = (flow: number, a: number, b: number) =>
    flow * (flow >= 0 ? concentration(a) : concentration(b));
  out.set(data);
  for (let z = 2; z < sz - 2; z++)
    for (let y = 2; y < sy - 2; y++)
      for (let x = 2; x < sx - 2; x++) {
        const voxel = (z * sy + y) * sx + x,
          i = voxel * 4;
        let water = data[i + 1],
          sediment = data[i + 2],
          wet = water,
          carried = sediment,
          lap = -data[i],
          speed = 0;
        for (let axis = 0; axis < 3; axis++) {
          const off = offsets[axis],
            f = flux[voxel * 3 + axis],
            fm = flux[(voxel - off / 4) * 3 + axis];
          water += fm - f;
          sediment +=
            sedimentFlux(fm, i - off, i) - sedimentFlux(f, i, i + off);
          wet = Math.max(wet, data[i - off + 1], data[i + off + 1]);
          carried += data[i - off + 2] + data[i + off + 2];
          lap += (data[i - off] + data[i + off]) / 6;
          speed += (Math.abs(f) + Math.abs(fm)) ** 2;
        }
        const gx = data[i + 4] - data[i - 4],
          gy = data[i + sx * 4] - data[i - sx * 4],
          gz = data[i + sx * sy * 4] - data[i - sx * sy * 4];
        const ny = gy / (Math.hypot(gx, gy, gz) || 1);
        let rain = 0;
        if (data[i] > 0 && data[i] < cell * 1.7 && ny > 0.08) {
          let exposed = 1;
          for (let j = 1; j <= 18; j++)
            if (y + j * 4 < sy)
              exposed *= smoothstep(
                -cell * 0.2,
                cell,
                data[i + j * 4 * sx * 4],
              );
          rain = s.rainfall * 0.028 * ny * exposed;
        }
        water = Math.max(0, water + rain) * (1 - s.evaporation * 0.035);
        wet = Math.max(wet, water);
        const px = -48 + (x + 0.5) * cell,
          py = -10 + (y + 0.5) * cell,
          pz = -48 + (z + 0.5) * cell;
        const narrow =
          1 - smoothstep(cell * 0.6, cell * 2.3, Math.abs(data[i]));
        let delta = 0,
          hydraulic = 0;
        if (narrow > 0) {
          const band =
            Math.sin(
              py * 1.38 + noise(px * 0.055, py * 0.055, pz * 0.055) * 1.4,
            ) *
              0.5 +
            0.5;
          const hardness = mix(1, 0.18 + band * 0.72, s.resistance);
          const capacity =
            s.sediment * (Math.sqrt(speed) * 8 + rain * 6 + 0.012) * wet * 3;
          const detach =
            s.erosion * hardness * Math.max(0, capacity - carried / 7) * 0.62;
          const deposit = Math.max(0, carried / 7 - capacity) * 0.11;
          hydraulic = clamp(
            (detach - deposit) * narrow,
            -cell * 0.025,
            cell * 0.035,
          );
          delta =
            hydraulic +
            clamp(
              lap * s.thermal * 0.11 * hardness * narrow,
              -cell * 0.028,
              cell * 0.028,
            );
        }
        out[i] = clamp(data[i] + delta, -24, 32);
        out[i + 1] = Math.min(2, water);
        out[i + 2] = clamp(sediment + hydraulic * 0.3, 0, 2);
        out[i + 3] = clamp(data[i + 3] + delta, -6, 6);
      }
}
/** Godunov Eikonal relaxation of the scalar distance channel. Fluid, suspended
 * sediment and cumulative surface displacement are deliberately unchanged. */
export function redistanceField(
  data: Float32Array,
  size: VolumeSize,
  out: Float32Array,
): void {
  const { x: sx, y: sy, z: sz } = size,
    h = 96 / sx,
    offsets = [4, sx * 4, sx * sy * 4];
  out.set(data);
  for (let z = 2; z < sz - 2; z++)
    for (let y = 2; y < sy - 2; y++)
      for (let x = 2; x < sx - 2; x++) {
        const i = ((z * sy + y) * sx + x) * 4,
          d = data[i];
        if (Math.abs(d) >= h * 8) continue;
        let gradient = 0;
        for (const off of offsets) {
          const backward = (d - data[i - off]) / h,
            ahead = (data[i + off] - d) / h;
          const b = d >= 0 ? Math.max(backward, 0) : Math.min(backward, 0),
            f = d >= 0 ? Math.min(ahead, 0) : Math.max(ahead, 0);
          gradient += Math.max(b * b, f * f);
        }
        const sign = d / Math.sqrt(d * d + h * h),
          correction = clamp(
            0.2 * h * sign * (Math.sqrt(gradient) - 1),
            -0.45 * Math.abs(d),
            0.45 * Math.abs(d),
          );
        out[i] = d - correction;
      }
}

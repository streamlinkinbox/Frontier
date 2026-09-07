import { clamp, smoothstep } from "./math";
import { erodibility, solidFraction } from "./geology";
import type { Settings, VolumeSize } from "./types";

const permeability = (d: number, h: number) =>
  smoothstep(-0.45 * h, 0.65 * h, d);
const boundary = (
  x: number,
  y: number,
  z: number,
  sx: number,
  sy: number,
  sz: number,
) => x < 2 || y < 2 || z < 2 || x >= sx - 2 || y >= sy - 2 || z >= sz - 2;

/** Shared signed face fluxes; each donor exports at most one sixth per face. */
export function computeFlux(
  data: Float32Array,
  size: VolumeSize,
  flux: Float32Array,
): void {
  const { x: sx, y: sy, z: sz } = size,
    h = 96 / sx;
  const offsets = [1, sx, sx * sy];
  for (let z = 0; z < sz; z++)
    for (let y = 0; y < sy; y++)
      for (let x = 0; x < sx; x++) {
        const voxel = (z * sy + y) * sx + x,
          a = voxel * 4;
        for (let axis = 0; axis < 3; axis++) {
          if (
            (axis === 0 && x === sx - 1) ||
            (axis === 1 && y === sy - 1) ||
            (axis === 2 && z === sz - 1)
          ) {
            flux[voxel * 3 + axis] = 0;
            continue;
          }
          const b = (voxel + offsets[axis]) * 4;
          if (data[a + 1] === 0 && data[b + 1] === 0) {
            flux[voxel * 3 + axis] = 0;
            continue;
          }
          const raw =
            (data[a + 1] - data[b + 1] - (axis === 1 ? 0.55 : 0)) * 0.3;
          flux[voxel * 3 + axis] =
            clamp(raw, -data[b + 1] / 6, data[a + 1] / 6) *
            Math.min(permeability(data[a], h), permeability(data[b], h));
        }
      }
}

/** Transport, settling and a locally budgeted rock/sediment exchange. Thermal
 * material transport is a separate conservative pass, not Laplacian smoothing. */
export function evolveField(
  data: Float32Array,
  size: VolumeSize,
  s: Settings,
  flux: Float32Array,
  out: Float32Array,
): void {
  const { x: sx, y: sy, z: sz } = size,
    h = 96 / sx;
  const offsets = [4, sx * 4, sx * sy * 4];
  const settling = s.settling * 0.06;
  const concentration = (i: number) =>
    Math.min(data[i + 2] / Math.max(data[i + 1], 0.00001), 3);
  // Reserve part of each donor's sediment budget for gravitational settling.
  const sedimentFlux = (f: number, a: number, b: number, axis: number) => {
    const advected =
      (1 - settling) * f * (f >= 0 ? concentration(a) : concentration(b));
    return (
      advected -
      (axis === 1
        ? settling *
          data[b + 2] *
          Math.min(permeability(data[a], h), permeability(data[b], h))
        : 0)
    );
  };
  out.set(data);
  for (let z = 0; z < sz; z++)
    for (let y = 0; y < sy; y++)
      for (let x = 0; x < sx; x++) {
        const voxel = (z * sy + y) * sx + x,
          i = voxel * 4;
        if (boundary(x, y, z, sx, sy, sz)) {
          out[i + 1] = out[i + 2] = 0;
          continue;
        }
        let water = data[i + 1],
          sediment = data[i + 2],
          wet = water;
        const q = [0, 0, 0];
        for (let axis = 0; axis < 3; axis++) {
          const off = offsets[axis],
            f = flux[voxel * 3 + axis],
            fm = flux[(voxel - off / 4) * 3 + axis];
          water += fm - f;
          sediment +=
            sedimentFlux(fm, i - off, i, axis) -
            sedimentFlux(f, i, i + off, axis);
          wet = Math.max(wet, data[i - off + 1], data[i + off + 1]);
          q[axis] = (f + fm) * 0.5;
        }
        const gx = data[i + 4] - data[i - 4],
          gy = data[i + sx * 4] - data[i - sx * 4],
          gz = data[i + sx * sy * 4] - data[i - sx * sy * 4];
        const len = Math.hypot(gx, gy, gz) || 1,
          nx = gx / len,
          ny = gy / len,
          nz = gz / len;
        let rain = 0;
        if (data[i] > 0 && data[i] < h * 1.7 && ny > 0.08 && s.rainfall > 0) {
          let exposed = 1;
          // Every voxel in the vertical column: do not rain through a thin roof.
          for (let yy = y + 1; yy < sy && exposed > 0.001; yy++)
            exposed *= smoothstep(
              -h * 0.2,
              h * 0.3,
              data[((z * sy + yy) * sx + x) * 4],
            );
          rain = s.rainfall * 0.04 * ny * exposed;
        }
        water = Math.max(0, water + rain) * (1 - s.evaporation * 0.035);
        sediment = Math.max(0, sediment);
        wet = Math.max(wet, water);
        let delta = 0;
        const narrow = 1 - smoothstep(h * 0.75, h, Math.abs(data[i]));
        if (narrow > 0) {
          // Include flow just outside the rock, rather than treating water pushing
          // normally into an impermeable wall as scouring along its surface.
          const axis =
            Math.abs(nx) > Math.abs(ny) && Math.abs(nx) > Math.abs(nz)
              ? 0
              : Math.abs(ny) > Math.abs(nz)
                ? 1
                : 2;
          const sign = [nx, ny, nz][axis] >= 0 ? 1 : -1;
          const front = voxel + (sign * offsets[axis]) / 4;
          for (let a = 0; a < 3; a++) q[a] = (q[a] + flux[front * 3 + a]) * 0.5;
          const normalFlow = q[0] * nx + q[1] * ny + q[2] * nz;
          const tangential = Math.hypot(
            q[0] - nx * normalFlow,
            q[1] - ny * normalFlow,
            q[2] - nz * normalFlow,
          );
          const slope = Math.min(
            2.5,
            Math.sqrt(Math.max(0, 1 - ny * ny)) / Math.max(Math.abs(ny), 0.25),
          );
          const velocity = Math.min(3, tangential / Math.max(wet, 0.015));
          const px = -48 + (x + 0.5) * h,
            py = -10 + (y + 0.5) * h,
            pz = -48 + (z + 0.5) * h;
          const weak = erodibility(px, py, pz, s.resistance);
          const shear = wet * velocity * (0.5 + slope * 1.2) + rain * 0.6;
          const threshold = (0.001 + s.cohesion * 0.025) * (1.1 - weak * 0.7);
          const capacity =
            s.sediment * (wet * velocity * (1 + slope * 0.8) * 4 + rain * 0.7);
          const solid = solidFraction(data[i], h);
          const detach = Math.min(
            solid,
            Math.max(0, capacity - sediment),
            0.02,
            s.erosion * weak * Math.max(0, shear - threshold) * 0.4 * narrow,
          );
          const deposit = Math.min(
            1 - solid,
            sediment,
            0.02,
            Math.max(0, sediment - capacity) *
              (0.08 + s.settling * 0.35) *
              smoothstep(-0.15, 0.75, ny) *
              narrow,
          );
          const exchange = detach - deposit;
          delta = exchange * 2 * h;
          sediment += exchange;
        }
        out[i] = clamp(data[i] + delta, -24, 32);
        out[i + 1] = Math.min(2, water);
        out[i + 2] = Math.max(0, sediment);
        out[i + 3] = clamp(data[i + 3] + delta, -6, 6);
      }
}

/** Four DOWNWARD diagonal transfers per donor. Each donor and receiver grants
 * one quarter of its material/void budget per face, so no mass clipping is needed.
 * Loose deposited sediment is mobile; cemented strata creep more reluctantly. */
export function computeTalusFlux(
  data: Float32Array,
  size: VolumeSize,
  s: Settings,
  flux: Float32Array,
): void {
  const { x: sx, y: sy, z: sz } = size,
    h = 96 / sx;
  const offsets = [1 - sx, -1 - sx, sx * sy - sx, -sx * sy - sx];
  flux.fill(0);
  const repose = Math.cos((s.talusAngle * Math.PI) / 180);
  for (let z = 2; z < sz - 2; z++)
    for (let y = 3; y < sy - 2; y++)
      for (let x = 2; x < sx - 2; x++) {
        const voxel = (z * sy + y) * sx + x,
          i = voxel * 4,
          d = data[i];
        if (Math.abs(d) >= h || s.thermal <= 0) continue;
        const gx = data[i + 4] - data[i - 4],
          gy = data[i + sx * 4] - data[i - sx * 4],
          gz = data[i + sx * sy * 4] - data[i - sx * sy * 4];
        const length = Math.hypot(gx, gy, gz) || 1,
          ny = gy / length;
        const unstable = clamp((repose - ny) / repose, 0, 1);
        if (unstable <= 0) continue;
        const loose = clamp(-data[i + 3] / h, 0, 1);
        const weak = erodibility(
          -48 + (x + 0.5) * h,
          -10 + (y + 0.5) * h,
          -48 + (z + 0.5) * h,
          s.resistance,
        );
        const mobility = (0.7 - s.cohesion * 0.35) * weak * (1 - loose) + loose;
        const rate = Math.min(0.4, s.thermal * 0.45 * unstable * mobility);
        const solid = solidFraction(d, h);
        for (let dir = 0; dir < 4; dir++) {
          if (
            (dir === 0 && x >= sx - 3) ||
            (dir === 1 && x <= 2) ||
            (dir === 2 && z >= sz - 3) ||
            (dir === 3 && z <= 2)
          )
            continue;
          const destination = (voxel + offsets[dir]) * 4;
          const free = 1 - solidFraction(data[destination], h);
          flux[voxel * 4 + dir] = Math.min(solid, free) * 0.25 * rate;
        }
      }
}
export function settleTalus(
  data: Float32Array,
  size: VolumeSize,
  flux: Float32Array,
  out: Float32Array,
): void {
  const { x: sx, y: sy, z: sz } = size,
    h = 96 / sx,
    offsets = [1 - sx, -1 - sx, sx * sy - sx, -sx * sy - sx];
  out.set(data);
  for (let z = 2; z < sz - 2; z++)
    for (let y = 2; y < sy - 2; y++)
      for (let x = 2; x < sx - 2; x++) {
        const voxel = (z * sy + y) * sx + x,
          i = voxel * 4;
        let change = 0;
        for (let dir = 0; dir < 4; dir++)
          change +=
            flux[(voxel - offsets[dir]) * 4 + dir] - flux[voxel * 4 + dir];
        if (Math.abs(change) < 1e-12) continue;
        const solid = solidFraction(data[i], h);
        const next = clamp(solid + change, 0, 1);
        const distance = (0.5 - next) * 2 * h;
        out[i] = distance;
        out[i + 3] = clamp(data[i + 3] - change * 2 * h, -6, 6);
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

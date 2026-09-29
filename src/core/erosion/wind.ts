// ---------------------------------------------------------------------------
// Frontier core / wind (aeolian) erosion
// Directional sediment flux swept along the wind vector:
//   - windward faces and convex crests abrade (curvature-weighted)
//   - lee slopes and sheltered hollows accumulate (occlusion-weighted)
//   - flux saturates: carrying capacity limits how much a column can lose
// Produces yardangs, ventifacts-style asymmetric forms and dune lee slopes.
// ---------------------------------------------------------------------------

import { HeightField, cloneHeightField, curvatureAt } from '../heightfield';
import { ErodeProgress } from './hydraulic';

export interface WindParams {
  iterations: number;
  directionDeg: number;   // compass direction wind blows TOWARDS
  strength: number;       // 0..1
  abrasion: number;       // crest/windward cutting
  deposition: number;     // lee/shelter filling
  shelterRange: number;   // metres of upwind occlusion sampling
  turbulence: number;     // noise modulation of the flux
}

export const WIND_DEFAULTS: WindParams = {
  iterations: 18,
  directionDeg: 235,
  strength: 0.6,
  abrasion: 0.5,
  deposition: 0.45,
  shelterRange: 60,
  turbulence: 0.35,
};

export function windErosion(src: HeightField, p: WindParams, seed: number, onProgress?: ErodeProgress): HeightField {
  const hf = cloneHeightField(src);
  const { nx, nz, h, data } = hf;
  const rad = (p.directionDeg * Math.PI) / 180;
  const wx = Math.sin(rad), wz = Math.cos(rad);   // wind travel direction (x, z)
  const range = Math.max(2, Math.round(p.shelterRange / h));
  const work = new Float32Array(data);

  for (let iter = 0; iter < p.iterations; iter++) {
    const delta = new Float32Array(nx * nz);
    const flux = new Float32Array(nx * nz);

    // sweep columns in upwind -> downwind order
    const indices = sweepOrder(nx, nz, wx, wz);

    for (let o = 0; o < indices.length; o++) {
      const i = indices[o];
      const x = i % nx, z = (i / nx) | 0;

      // upwind occlusion: sample heights behind, measure sheltering
      let occ = 0;
      const hc = work[i];
      for (let s = 1; s <= range; s += 2) {
        const sx = Math.round(x - wx * s), sz = Math.round(z - wz * s);
        if (sx < 0 || sz < 0 || sx >= nx || sz >= nz) break;
        const dh = work[sz * nx + sx] - hc;
        const w = 1 - s / range;
        if (dh > 0) occ += (dh / (h * 6 + dh)) * w;
      }
      const shelter = Math.min(1, occ);

      // windward exposure from gradient vs wind
      const ix0 = Math.max(0, x - 1), ix1 = Math.min(nx - 1, x + 1);
      const iz0 = Math.max(0, z - 1), iz1 = Math.min(nz - 1, z + 1);
      const gx = (work[z * nx + ix1] - work[z * nx + ix0]) / ((ix1 - ix0) * h || 1);
      const gz = (work[iz1 * nx + x] - work[iz0 * nx + x]) / ((iz1 - iz0) * h || 1);
      const gl = Math.hypot(gx, gz) || 1e-6;
      const exposure = Math.max(0, (gx * wx + gz * wz) / gl); // facing upwind

      const curv = curvatureAt(hf2(work, hf), x, z);
      const convex = Math.max(0, Math.min(1, curv * h * h * 0.5 + 0.5 * Math.sign(curv)));

      const turb = 0.75 + 0.5 * pseudoNoise(x * 0.11, z * 0.11, seed + iter * 31) * p.turbulence;
      const carry = flux[i];
      const capacity = p.strength * turb * (1 - shelter) * (0.4 + 0.6 * exposure);

      let change = 0;
      if (carry < capacity) {
        // abrasion: cut convex + windward surfaces
        change -= (capacity - carry) * p.abrasion * (0.35 + 0.65 * convex) * h * 0.08;
        flux[i] = capacity;
      } else {
        const dep = (carry - capacity) * p.deposition * (0.3 + 0.7 * shelter);
        change += dep;
        flux[i] = carry - dep;
      }
      delta[i] += change;

      // advect remaining flux downwind
      const ax = x + Math.round(wx), az = z + Math.round(wz);
      if (ax >= 0 && az >= 0 && ax < nx && az < nz) flux[az * nx + ax] += flux[i];
    }

    for (let i = 0; i < work.length; i++) work[i] = Math.max(0, work[i] + delta[i]);
    onProgress?.((iter + 1) / p.iterations, `wind ${iter + 1}/${p.iterations}`);
  }

  hf.data = work;
  return hf;
}

function sweepOrder(nx: number, nz: number, wx: number, wz: number): Int32Array {
  const idx = new Int32Array(nx * nz);
  for (let i = 0; i < idx.length; i++) idx[i] = i;
  // key = projection onto wind direction (upwind first)
  const key = new Float64Array(nx * nz);
  for (let z = 0; z < nz; z++) for (let x = 0; x < nx; x++) key[z * nx + x] = x * wx + z * wz;
  const order = Array.from(idx).sort((a, b) => key[a] - key[b]);
  return Int32Array.from(order);
}

function hf2(work: Float32Array, proto: HeightField): HeightField {
  return { nx: proto.nx, nz: proto.nz, h: proto.h, data: work };
}

function pseudoNoise(x: number, y: number, seed: number): number {
  const s = Math.sin(x * 12.9898 + y * 78.233 + seed * 0.017) * 43758.5453;
  return (s - Math.floor(s)) * 2 - 1;
}

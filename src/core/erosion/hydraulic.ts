// ---------------------------------------------------------------------------
// Frontier core / hydraulic erosion  (rivers)
// Capacity-based stream-power routing on the D8 network:
//   Q  = rainfall * accumulation          discharge
//   C  = Kc * Q^m * S                     sediment transport capacity
//   erode where load < C (incision ~ stream power K * Q^m * S)
//   deposit where load > C (floodplains, fans)
// Erosion is splatted over a metre-radius kernel so channels are several
// voxels wide and actually *read* as rivers instead of 1px cracks.
// ---------------------------------------------------------------------------

import { HeightField, cloneHeightField } from '../heightfield';
import { computeFlow, fillPits, D8_DX, D8_DZ, D8_LEN } from './flow';

export interface HydraulicParams {
  iterations: number;
  rainfall: number;        // water per cell per iteration
  erodibility: number;     // K, stream power coefficient
  capacityKc: number;      // sediment capacity coefficient
  expM: number;            // discharge exponent (0.4 - 0.6 typical)
  depositRate: number;     // 0..1 fraction of excess load dropped per cell
  maxErodeStep: number;    // metres of bed change per pass (stability clamp)
  channelWidth: number;    // metres — splat radius so rivers are visible
  talusMix: number;        // 0..1 blend of talus relaxation after each pass
}

export const HYDRAULIC_DEFAULTS: HydraulicParams = {
  iterations: 24,
  rainfall: 1.0,
  erodibility: 0.55,
  capacityKc: 0.9,
  expM: 0.5,
  depositRate: 0.35,
  maxErodeStep: 0.6,
  channelWidth: 14,
  talusMix: 0.15,
};

export interface ErodeProgress { (frac: number, stage: string): void }

export function hydraulicErosion(src: HeightField, p: HydraulicParams, onProgress?: ErodeProgress): HeightField {
  const hf = cloneHeightField(src);
  const { nx, nz, h } = hf;
  const n = nx * nz;
  const work = new Float32Array(n);
  work.set(hf.data);

  const radius = Math.max(1, Math.round((p.channelWidth * 0.5) / h));
  // precompute splat kernel (cosine falloff)
  const kSize = radius * 2 + 1;
  const kernel = new Float32Array(kSize * kSize);
  let kSum = 0;
  for (let dz = -radius; dz <= radius; dz++) {
    for (let dx = -radius; dx <= radius; dx++) {
      const r = Math.hypot(dx, dz) / radius;
      const w = r <= 1 ? 0.5 * (1 + Math.cos(Math.PI * r)) : 0;
      kernel[(dz + radius) * kSize + (dx + radius)] = w;
      kSum += w;
    }
  }

  const sediment = new Float32Array(n);

  for (let iter = 0; iter < p.iterations; iter++) {
    const filled = fillPits({ ...hf, data: work });
    const flow = computeFlow({ ...hf, data: work }, filled);

    // water & sediment routed upstream -> downstream
    const water = new Float32Array(n);
    sediment.fill(0);
    for (let i = 0; i < n; i++) water[i] = p.rainfall;

    const delta = new Float32Array(n);

    for (let o = 0; o < n; o++) {
      const i = flow.order[o];
      const x = i % nx, z = (i / nx) | 0;
      const k = flow.dir[i];
      const Q = water[i];
      if (k < 0) continue;
      const j = (z + D8_DZ[k]) * nx + (x + D8_DX[k]);
      const S = Math.max(0, (work[i] - work[j]) / (D8_LEN[k] * h));
      const capacity = p.capacityKc * Math.pow(Q, p.expM) * S;

      if (sediment[i] < capacity) {
        // under-saturated: incise the bed (stream power)
        const erode = Math.min(
          (capacity - sediment[i]) * p.erodibility,
          p.maxErodeStep * S * (0.25 + 0.75 * Math.min(1, Q / 8)),
        );
        delta[i] -= erode;
        sediment[i] += erode;
      } else {
        // over-saturated: drop the excess
        const dep = (sediment[i] - capacity) * p.depositRate;
        delta[i] += dep;
        sediment[i] -= dep;
      }
      water[j] += Q;
      sediment[j] += sediment[i];
    }

    // splat deltas so channels have real width
    const applied = new Float32Array(n);
    for (let z = 0; z < nz; z++) {
      for (let x = 0; x < nx; x++) {
        const i = z * nx + x;
        const dv = delta[i];
        if (dv === 0) continue;
        const share = dv / kSum;
        for (let dz = -radius; dz <= radius; dz++) {
          const az = z + dz;
          if (az < 0 || az >= nz) continue;
          for (let dx = -radius; dx <= radius; dx++) {
            const ax = x + dx;
            if (ax < 0 || ax >= nx) continue;
            const w = kernel[(dz + radius) * kSize + (dx + radius)];
            if (w > 0) applied[az * nx + ax] += share * w;
          }
        }
      }
    }
    for (let i = 0; i < n; i++) work[i] = Math.max(0, work[i] + applied[i]);

    if (p.talusMix > 0) talusRelax(work, nx, nz, h, p.talusMix, 1);
    onProgress?.((iter + 1) / p.iterations, `hydraulic ${iter + 1}/${p.iterations}`);
  }

  hf.data = work;
  return hf;
}

/** angle-of-repose relaxation shared by hydraulic tail-pass and thermal node */
export function talusRelax(H: Float32Array, nx: number, nz: number, h: number, strength: number, passes: number, angleDeg = 34, hardness?: Float32Array): void {
  const tanA = Math.tan((angleDeg * Math.PI) / 180);
  for (let p = 0; p < passes; p++) {
    for (let z = 0; z < nz; z++) {
      for (let x = 0; x < nx; x++) {
        const i = z * nx + x;
        const hc = hardness ? hardness[i] : 1;
        for (let k = 0; k < 4; k++) {
          const ax = x + D8_DX[k * 2], az = z + D8_DZ[k * 2];
          if (ax < 0 || az < 0 || ax >= nx || az >= nz) continue;
          const j = az * nx + ax;
          const hn = hardness ? hardness[j] : 1;
          const drop = H[i] - H[j];
          const maxDrop = tanA * h;
          if (drop > maxDrop) {
            const move = (drop - maxDrop) * 0.25 * strength * (1 - hc * 0.85) * (hn < 0.5 ? 1.25 : 1);
            if (move > 0) { H[i] -= move; H[j] += move; }
          }
        }
      }
    }
  }
}

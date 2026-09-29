// ---------------------------------------------------------------------------
// Frontier core / alluvial deposition  (sedimentation: floodplains, fans, aprons)
// Routes the sediment load produced upstream and drops it where transport
// energy collapses: low slope + high accumulation (valley floors), and at the
// base of cliffs (talus aprons). Mass comes from a supplied load field or is
// synthesised from local relief so the solver stays self-contained.
// ---------------------------------------------------------------------------

import { HeightField, cloneHeightField } from '../heightfield';
import { computeFlow, fillPits, D8_DX, D8_DZ, D8_LEN } from './flow';
import { ErodeProgress } from './hydraulic';

export interface AlluvialParams {
  iterations: number;
  supply: number;          // sediment supplied per cell (relief-scaled)
  slopeThreshold: number;  // below this gradient load starts dropping
  fanStrength: number;     // extra deposition where accumulation is high
  maxDepositStep: number;  // metres per pass (stability)
  apron: number;           // cliff-base apron strength 0..1
}

export const ALLUVIAL_DEFAULTS: AlluvialParams = {
  iterations: 14,
  supply: 0.5,
  slopeThreshold: 0.18,
  fanStrength: 0.7,
  maxDepositStep: 0.35,
  apron: 0.6,
};

export function alluvialDeposition(src: HeightField, p: AlluvialParams, onProgress?: ErodeProgress): HeightField {
  const hf = cloneHeightField(src);
  const { nx, nz, h, data } = hf;
  const n = nx * nz;
  const work = new Float32Array(data);

  // local relief = height - mean of neighbourhood (cliff detector)
  const relief = new Float32Array(n);
  for (let z = 0; z < nz; z++) {
    for (let x = 0; x < nx; x++) {
      const i = z * nx + x;
      let sum = 0, cnt = 0;
      for (let dz = -3; dz <= 3; dz += 2) for (let dx = -3; dx <= 3; dx += 2) {
        const ax = x + dx, az = z + dz;
        if (ax < 0 || az < 0 || ax >= nx || az >= nz) continue;
        sum += work[az * nx + ax]; cnt++;
      }
      relief[i] = work[i] - sum / Math.max(1, cnt);
    }
  }

  for (let iter = 0; iter < p.iterations; iter++) {
    const filled = fillPits({ ...hf, data: work });
    const flow = computeFlow({ ...hf, data: work }, filled);
    const load = new Float32Array(n);
    const delta = new Float32Array(n);

    // supply: soft high-relief cells shed material into the network
    for (let i = 0; i < n; i++) {
      load[i] = p.supply * Math.max(0, relief[i]) / (h * 4 + 1);
    }

    for (let o = 0; o < n; o++) {
      const i = flow.order[o];
      const x = i % nx, z = (i / nx) | 0;
      const k = flow.dir[i];
      if (k < 0) continue;
      const j = (z + D8_DZ[k]) * nx + (x + D8_DX[k]);
      const S = Math.max(0, (work[i] - work[j]) / (D8_LEN[k] * h));
      const acc = Math.min(1, flow.acc[j] / 64);
      // transport energy: steep + confined keeps load moving
      const energy = S * (0.35 + 0.65 * acc);
      const retain = S < p.slopeThreshold
        ? Math.min(1, (p.slopeThreshold - S) / p.slopeThreshold) * (0.4 + p.fanStrength * acc)
        : 0;
      const dep = Math.min(load[i] * retain, p.maxDepositStep * energy + 0.02);
      if (dep > 0) {
        delta[i] += dep;
        load[i] -= dep;
      }
      load[j] += load[i];
    }

    // cliff-base aprons: spread relief-sourced debris to the lowest neighbour
    if (p.apron > 0) {
      for (let z = 0; z < nz; z++) {
        for (let x = 0; x < nx; x++) {
          const i = z * nx + x;
          const r = Math.max(0, relief[i]);
          if (r < h * 1.5) continue;
          let bj = -1, bh = Infinity;
          for (let k = 0; k < 8; k++) {
            const ax = x + D8_DX[k], az = z + D8_DZ[k];
            if (ax < 0 || az < 0 || ax >= nx || az >= nz) continue;
            const j = az * nx + ax;
            if (work[j] < bh) { bh = work[j]; bj = j; }
          }
          if (bj >= 0) {
            const move = r * 0.02 * p.apron;
            work[i] -= move; delta[bj] += move;
          }
        }
      }
    }

    for (let i = 0; i < n; i++) work[i] = Math.max(0, work[i] + delta[i]);
    onProgress?.((iter + 1) / p.iterations, `alluvial ${iter + 1}/${p.iterations}`);
  }

  hf.data = work;
  return hf;
}

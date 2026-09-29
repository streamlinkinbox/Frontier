// ---------------------------------------------------------------------------
// Frontier core / thermal erosion  (cliffs + talus)
// Angle-of-repose collapse modulated by a rock-hardness field: hard strata
// barely creep, so they stand up as cliff faces while soft layers back off
// into scree slopes. This is what turns noise blobs into mesa/butte/cliff
// morphology, and it is the sediment source for the alluvial solver.
// ---------------------------------------------------------------------------

import { HeightField, cloneHeightField } from '../heightfield';
import { ErodeProgress } from './hydraulic';

export interface ThermalParams {
  iterations: number;
  talusAngle: number;     // degrees, angle of repose of collapsed material
  strength: number;       // 0..1 how aggressively collapse redistributes
  creep: number;          // slow soil creep on gentle slopes
  hardnessContrast: number; // 0..1 how much the hardness mask protects rock
}

export const THERMAL_DEFAULTS: ThermalParams = {
  iterations: 30,
  talusAngle: 37,
  strength: 0.6,
  creep: 0.05,
  hardnessContrast: 0.8,
};

export function thermalErosion(
  src: HeightField,
  p: ThermalParams,
  hardness: Float32Array | null,
  onProgress?: ErodeProgress,
): HeightField {
  const hf = cloneHeightField(src);
  const { nx, nz, h, data } = hf;
  const tanA = Math.tan((p.talusAngle * Math.PI) / 180);
  const n = nx * nz;
  const hard = hardness ?? new Float32Array(n).fill(0.5);

  for (let iter = 0; iter < p.iterations; iter++) {
    for (let z = 0; z < nz; z++) {
      for (let x = 0; x < nx; x++) {
        const i = z * nx + x;
        const hc = hard[i];
        // protection: hard rock resists being undermined
        const protect = 1 - p.hardnessContrast * Math.max(0, hc * 2 - 1);
        for (let k = 0; k < 8; k++) {
          const dx = [1, 1, 0, -1, -1, -1, 0, 1][k];
          const dz = [0, 1, 1, 1, 0, -1, -1, -1][k];
          const len = (k % 2 === 1) ? Math.SQRT2 : 1;
          const ax = x + dx, az = z + dz;
          if (ax < 0 || az < 0 || ax >= nx || az >= nz) continue;
          const j = az * nx + ax;
          const drop = data[i] - data[j];
          const maxDrop = tanA * h * len;
          if (drop > maxDrop) {
            const hn = hard[j];
            // material moves only as fast as the *weaker* of the pair allows
            const mobility = Math.min(protect, 1 - p.hardnessContrast * Math.max(0, hn * 2 - 1));
            const move = (drop - maxDrop) * 0.125 * p.strength * mobility;
            if (move > 1e-6) { data[i] -= move; data[j] += move; }
          } else if (drop > 0 && p.creep > 0) {
            const move = drop * p.creep * 0.05 * protect;
            data[i] -= move; data[j] += move;
          }
        }
      }
    }
    onProgress?.((iter + 1) / p.iterations, `thermal ${iter + 1}/${p.iterations}`);
  }
  return hf;
}

/** horizontal strata hardness mask (sedimentary bands) in [0,1] */
export function strataHardness(hf: HeightField, bandHeight: number, contrast: number, seed: number): Float32Array {
  const n = hf.nx * hf.nz;
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const y = hf.data[i];
    const b = Math.sin((y / Math.max(1, bandHeight)) * Math.PI * 2 + seed);
    const s = 0.5 + 0.5 * Math.sign(b) * Math.pow(Math.abs(b), 0.6);
    out[i] = 0.5 + (s - 0.5) * contrast;
  }
  return out;
}

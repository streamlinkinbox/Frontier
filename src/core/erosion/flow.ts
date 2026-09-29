// ---------------------------------------------------------------------------
// Frontier core / flow routing
// Shared hydrology primitives: pit filling (Planchon-Darboux), D8 directions,
// topological (downstream) ordering and flow accumulation. Everything the
// hydraulic / alluvial solvers need to route water downhill without sinks.
// ---------------------------------------------------------------------------

import { HeightField } from '../heightfield';

export const D8_DX = [1, 1, 0, -1, -1, -1, 0, 1];
export const D8_DZ = [0, 1, 1, 1, 0, -1, -1, -1];
export const D8_LEN = [1, Math.SQRT2, 1, Math.SQRT2, 1, Math.SQRT2, 1, Math.SQRT2];

export interface FlowField {
  nx: number;
  nz: number;
  dir: Int8Array;       // D8 index of steepest descent, -1 = pit/outlet
  order: Int32Array;    // cells sorted upstream -> downstream
  acc: Float32Array;    // accumulated drainage area (cells)
}

/** Planchon-Darboux pit filling: raise sinks until water can escape. */
export function fillPits(hf: HeightField, epsilon = 1e-4): Float32Array {
  const { nx, nz, data } = hf;
  const z = new Float32Array(data);
  const n = nx * nz;
  // seed borders at their height, interior at +inf where needed
  for (let i = 0; i < n; i++) {
    const x = i % nx, zz = (i / nx) | 0;
    const border = x === 0 || zz === 0 || x === nx - 1 || zz === nz - 1;
    if (!border) z[i] = Math.max(z[i], data[i]);
  }
  let changed = true;
  let guard = 0;
  while (changed && guard++ < 4000) {
    changed = false;
    for (let pass = 0; pass < 2; pass++) {
      for (let i = 0; i < n; i++) {
        const idx = pass === 0 ? i : n - 1 - i;
        const x = idx % nx, zz = (idx / nx) | 0;
        if (z[idx] <= data[idx]) continue;
        let m = z[idx];
        for (let k = 0; k < 8; k++) {
          const ax = x + D8_DX[k], az = zz + D8_DZ[k];
          if (ax < 0 || az < 0 || ax >= nx || az >= nz) continue;
          const j = az * nx + ax;
          const cand = Math.max(z[j], data[idx]) + (z[j] <= data[idx] ? epsilon : 0);
          if (cand < m) m = cand;
        }
        if (m < z[idx] - 1e-9) { z[idx] = m; changed = true; }
      }
    }
  }
  return z;
}

/** D8 steepest descent + topological order + accumulation area. */
export function computeFlow(hf: HeightField, heights?: Float32Array): FlowField {
  const { nx, nz } = hf;
  const H = heights ?? hf.data;
  const n = nx * nz;
  const dir = new Int8Array(n).fill(-1);
  const indeg = new Int32Array(n);

  for (let z = 0; z < nz; z++) {
    for (let x = 0; x < nx; x++) {
      const i = z * nx + x;
      const hi = H[i];
      let best = -1, bestDrop = 0;
      for (let k = 0; k < 8; k++) {
        const ax = x + D8_DX[k], az = z + D8_DZ[k];
        if (ax < 0 || az < 0 || ax >= nx || az >= nz) continue;
        const j = az * nx + ax;
        const drop = (hi - H[j]) / D8_LEN[k];
        if (drop > bestDrop) { bestDrop = drop; best = k; }
      }
      dir[i] = best;
      if (best >= 0) indeg[(z + D8_DZ[best]) * nx + (x + D8_DX[best])]++;
    }
  }

  // Kahn topological sort upstream first
  const order = new Int32Array(n);
  const queue = new Int32Array(n);
  let qh = 0, qt = 0;
  for (let i = 0; i < n; i++) if (indeg[i] === 0) queue[qt++] = i;
  let oi = 0;
  while (qh < qt) {
    const i = queue[qh++];
    order[oi++] = i;
    const k = dir[i];
    if (k < 0) continue;
    const x = i % nx, z = (i / nx) | 0;
    const j = (z + D8_DZ[k]) * nx + (x + D8_DX[k]);
    if (--indeg[j] === 0) queue[qt++] = j;
  }
  // cycles (should not exist after pit fill) -> append remainder
  for (let i = 0; i < n; i++) if (indeg[i] > 0) order[oi++] = i;

  const acc = new Float32Array(n).fill(1);
  for (let o = 0; o < n; o++) {
    const i = order[o];
    const k = dir[i];
    if (k < 0) continue;
    const x = i % nx, z = (i / nx) | 0;
    acc[(z + D8_DZ[k]) * nx + (x + D8_DX[k])] += acc[i];
  }
  return { nx, nz, dir, order, acc };
}

/** normalised log accumulation in [0,1] — the "riveriness" mask */
export function accumulationMask(flow: FlowField): Float32Array {
  const n = flow.nx * flow.nz;
  const out = new Float32Array(n);
  let maxLog = 1;
  for (let i = 0; i < n; i++) maxLog = Math.max(maxLog, Math.log(1 + flow.acc[i]));
  for (let i = 0; i < n; i++) out[i] = Math.log(1 + flow.acc[i]) / maxLog;
  return out;
}

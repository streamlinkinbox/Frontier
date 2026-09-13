// ============================================================================
// Frontier SDF Terrain — Progressive field generation (DOM-free, Node-safe).
// Evaluates the compiled node-graph field into the SDF + material volumes in
// time-sliced chunks so the UI stays alive. Also bakes near-surface cavity.
// ============================================================================

import { clamp } from './noise.js';

const tick = () => new Promise((r) => setTimeout(r, 0));

export async function generateVolume(vol, fieldFn, opts = {}) {
  const { onProgress = null, token = null, keepEmitters = true } = opts;
  const N = vol.N, h = vol.worldSize / 2, vs = vol.voxel;
  const S = vol.sdf, B = vol.attrB;
  const savedEmit = keepEmitters ? vol.attrA.slice() : null;
  const out = { h: 0.5, s: 0.5, m: 0 };
  const isCancelled = () => token && token.cancelled;

  for (let k = 0; k < N; k++) {
    const z = (k + 0.5) * vs - h;
    for (let j = 0; j < N; j++) {
      const y = (j + 0.5) * vs - h;
      const row = (k * N + j) * N;
      for (let i = 0; i < N; i++) {
        const x = (i + 0.5) * vs - h;
        out.h = 0.5; out.s = 0.5; out.m = 0;
        const d = fieldFn(x, y, z, out);
        S[row + i] = d;
        const b4 = (row + i) * 4;
        B[b4] = clamp(out.h, 0, 1) * 255;
        B[b4 + 1] = clamp(out.s, 0, 1) * 255;
        B[b4 + 2] = 0;
        B[b4 + 3] = 0;
      }
    }
    if ((k & 7) === 7) {
      if (onProgress) onProgress((k + 1) / N * 0.85);
      if (isCancelled()) return false;
      await tick();
    }
  }

  // cavity bake: near-surface voxels only (concavity from 6-tap laplacian)
  const e = vs * 1.6;
  let done = 0;
  for (let k = 1; k < N - 1; k++) {
    for (let j = 1; j < N - 1; j++) {
      const row = (k * N + j) * N;
      for (let i = 1; i < N - 1; i++) {
        const id = row + i;
        const c = S[id];
        if (c > -vs * 5 || c < -vs * 26) continue; // solid near-surface band
        const lap = (S[id + 1] + S[id - 1] + S[id + N] + S[id - N] + S[id + N * N] + S[id - N * N] - 6 * c) / (e * e);
        const cav = clamp(lap * vs * 2.4, 0, 1);
        B[id * 4 + 2] = cav * 255;
        done++;
      }
    }
    if ((k & 15) === 15) {
      if (onProgress) onProgress(0.85 + (k / N) * 0.15);
      if (isCancelled()) return false;
      await tick();
    }
  }

  vol.attrA.fill(0);
  if (savedEmit) {
    // restore painted emitters so regen keeps your rain painting
    for (let n = 0; n < vol.n3; n++) vol.attrA[n * 4 + 3] = savedEmit[n * 4 + 3];
  }
  vol.carvedVolume = 0;
  vol.depositedVolume = 0;
  vol.generation++;
  vol.markAllDirty();
  if (onProgress) onProgress(1);
  return true;
}

// Quick low-res preview field for responsive slider drags: evaluates into a
// scratch volume at N/2 then upscales with trilinear filtering.
export async function generatePreview(vol, fieldFn, opts = {}) {
  const { onProgress = null, token = null } = opts;
  const N = vol.N, h = vol.worldSize / 2;
  const M = Math.max(24, N >> 1);
  const small = new Float32Array(M * M * M);
  const smallH = new Float32Array(M * M * M);
  const smallS = new Float32Array(M * M * M);
  const out = { h: 0.5, s: 0.5, m: 0 };
  const vs = vol.worldSize / M;
  for (let k = 0; k < M; k++) {
    const z = (k + 0.5) * vs - h;
    for (let j = 0; j < M; j++) {
      const y = (j + 0.5) * vs - h;
      for (let i = 0; i < M; i++) {
        const x = (i + 0.5) * vs - h;
        out.h = 0.5; out.s = 0.5; out.m = 0;
        small[(k * M + j) * M + i] = fieldFn(x, y, z, out);
        smallH[(k * M + j) * M + i] = out.h;
        smallS[(k * M + j) * M + i] = out.s;
      }
    }
    if ((k & 15) === 15) {
      if (token && token.cancelled) return false;
      await tick();
    }
  }
  // upscale
  const S = vol.sdf, B = vol.attrB;
  const f = M / N;
  for (let k = 0; k < N; k++) {
    const gz = clamp(k * f, 0, M - 1.001);
    const z0 = Math.floor(gz), fz = gz - z0;
    for (let j = 0; j < N; j++) {
      const gy = clamp(j * f, 0, M - 1.001);
      const y0 = Math.floor(gy), fy = gy - y0;
      for (let i = 0; i < N; i++) {
        const gx = clamp(i * f, 0, M - 1.001);
        const x0 = Math.floor(gx), fx = gx - x0;
        const id = (k * N + j) * N + i;
        const tri = (arr) => {
          const c000 = arr[(z0 * M + y0) * M + x0];
          const c100 = arr[(z0 * M + y0) * M + x0 + 1];
          const c010 = arr[(z0 * M + y0 + 1) * M + x0];
          const c110 = arr[(z0 * M + y0 + 1) * M + x0 + 1];
          const c001 = arr[((z0 + 1) * M + y0) * M + x0];
          const c101 = arr[((z0 + 1) * M + y0) * M + x0 + 1];
          const c011 = arr[((z0 + 1) * M + y0 + 1) * M + x0];
          const c111 = arr[((z0 + 1) * M + y0 + 1) * M + x0 + 1];
          const x00 = c000 + (c100 - c000) * fx, x10 = c010 + (c110 - c010) * fx;
          const x01 = c001 + (c101 - c001) * fx, x11 = c011 + (c111 - c011) * fx;
          return (x00 + (x10 - x00) * fy) * (1 - fz) + (x01 + (x11 - x01) * fy) * fz;
        };
        S[id] = tri(small);
        B[id * 4] = clamp(tri(smallH), 0, 1) * 255;
        B[id * 4 + 1] = clamp(tri(smallS), 0, 1) * 255;
      }
    }
    if ((k & 31) === 31) {
      if (onProgress) onProgress(k / N);
      if (token && token.cancelled) return false;
      await tick();
    }
  }
  vol.markAllDirty();
  if (onProgress) onProgress(1);
  return true;
}

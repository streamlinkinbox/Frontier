// ---------------------------------------------------------------------------
// Frontier core / re-initialisation
// After any operation that displaces the zero set (erosion write-back, booleans
// with non-distance inputs, warps) the field is no longer an exact distance
// function: |grad d| != 1, which is exactly what blurs cliffs and caves.
//
// We restore it with a Fast Sweeping Method solve of the eikonal equation
// |grad d| = 1, seeded from the interface voxels, keeping the sign field fixed.
// Only the narrow band is solved (saturated voxels are frozen), which keeps the
// cost O(band) and — because FSM is an exact distance transform with no
// smoothing kernel anywhere — sharp unions, cliff faces and cave walls stay
// razor sharp at any resolution the guard allows.
// ---------------------------------------------------------------------------

import { SDFVolume } from './volume';

const INF = 1e9;

/**
 * Re-establish |grad d| = 1 inside the narrow band.
 * `seedWidth` (voxels): how far around the zero set current values are trusted
 * as Dirichlet seeds. `sweeps`: number of full 8-ordering FSM passes.
 */
export function reinitBand(vol: SDFVolume, seedWidth = 1.5, sweeps = 1): void {
  const { nx, ny, nz, h, band, data } = vol;
  const n = data.length;
  const dist = new Float32Array(n);
  const sign = new Int8Array(n);
  const active = new Uint8Array(n);
  const seedDist = seedWidth * h;

  for (let i = 0; i < n; i++) {
    const v = data[i];
    const a = v < 0 ? -v : v;
    sign[i] = v < 0 ? -1 : 1;
    active[i] = a < band ? 1 : 0;
    dist[i] = a <= seedDist ? a : INF;
  }

  const nxny = nx * ny;
  for (let s = 0; s < sweeps; s++) {
    for (let order = 0; order < 8; order++) {
      const xAsc = (order & 1) === 0;
      const yAsc = (order & 2) === 0;
      const zAsc = (order & 4) === 0;
      for (let zk = 0; zk < nz; zk++) {
        const z = zAsc ? zk : nz - 1 - zk;
        for (let yk = 0; yk < ny; yk++) {
          const y = yAsc ? yk : ny - 1 - yk;
          const row = (z * ny + y) * nx;
          for (let xk = 0; xk < nx; xk++) {
            const x = xAsc ? xk : nx - 1 - xk;
            const i = row + x;
            if (active[i] === 0 || dist[i] === 0) continue;
            let mx = INF;
            if (x > 0 && dist[i - 1] < mx) mx = dist[i - 1];
            if (x < nx - 1 && dist[i + 1] < mx) mx = dist[i + 1];
            let my = INF;
            if (y > 0 && dist[i - nx] < my) my = dist[i - nx];
            if (y < ny - 1 && dist[i + nx] < my) my = dist[i + nx];
            let mz = INF;
            if (z > 0 && dist[i - nxny] < mz) mz = dist[i - nxny];
            if (z < nz - 1 && dist[i + nxny] < mz) mz = dist[i + nxny];
            const solved = godunov(mx, my, mz, h);
            if (solved < dist[i]) dist[i] = solved;
          }
        }
      }
    }
  }

  for (let i = 0; i < n; i++) {
    if (active[i] === 0) continue;
    let m = dist[i];
    if (m > band) m = band;
    data[i] = sign[i] * m;
  }
}

/** allocation-free upwind eikonal solver on 3 axes */
function godunov(ax: number, ay: number, az: number, h: number): number {
  // sort three values ascending without allocation
  let a = ax, b = ay, c = az;
  if (a > b) { const t = a; a = b; b = t; }
  if (b > c) { const t = b; b = c; c = t; }
  if (a > b) { const t = a; a = b; b = t; }
  let u = a + h;
  if (b < INF) {
    const d1 = b - a;
    const disc = 2 * h * h - d1 * d1;
    if (disc > 0) {
      const cand = (a + b + Math.sqrt(disc)) * 0.5;
      if (cand >= b && cand < u) u = cand;
    }
  }
  if (c < INF) {
    const s = a + b + c;
    const q = a * a + b * b + c * c - h * h;
    const disc = s * s - 3 * q;
    if (disc > 0) {
      const cand = (s + Math.sqrt(disc)) / 3;
      if (cand >= c && cand < u) u = cand;
    }
  }
  return u;
}

/**
 * Cheap per-column vertical re-distance: exact on slopes, over-estimates on
 * overhangs. Used as the seed field before reinitBand after height write-back.
 */
export function verticalRedistance(vol: SDFVolume, heights: Float32Array): void {
  const { nx, ny, nz, h, band } = vol;
  const d = vol.data;
  for (let z = 0; z < nz; z++) {
    for (let x = 0; x < nx; x++) {
      const Hgt = heights[z * nx + x];
      let col = (z * ny) * nx + x;
      for (let y = 0; y < ny; y++) {
        let v = (y * h) - Hgt;
        if (v > band) v = band;
        else if (v < -band) v = -band;
        d[col] = v;
        col += nx;
      }
    }
  }
}

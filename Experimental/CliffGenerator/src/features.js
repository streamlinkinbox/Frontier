// Drawn features: roads, rivers and lakes. Pure JS (runs in the worker and under Node).
//
//   rivers  — Catmull-Rom spline → meandered centre line → monotone downhill bed profile → channel
//             carved into the heightfield (cut only, with sloped banks). Carved once before
//             hydraulic erosion so droplets drain into it, and seeded with extra droplets along the
//             line so the water itself erodes the bed and tributary gullies; carved again at the
//             end so the final bed matches its profile. Produces a bed mask and a water level.
//   roads   — applied on the (refined) mesh grid after erosion: smoothed profile along the spline,
//             carriageway flattened, cut slopes above and fill embankments below.
//   lakes   — flood fill from a point up to a level: bed flattened to level − depth, water level set.
//
// Grid convention everywhere: world x = (i / (N-1) − 0.5) · size, same for z / j.

import { SimplexNoise } from './noise.js';

export const NO_WATER = -1e6;

// ---- splines ----------------------------------------------------------------------------------
export function sampleSpline(points, step) {
  if (!points || points.length === 0) return [];
  if (points.length === 1) return [[points[0][0], points[0][1], 0]];
  const P = [points[0], ...points, points[points.length - 1]];
  const out = [];
  let length = 0;
  let prev = null;
  for (let s = 1; s < P.length - 2; s++) {
    const p0 = P[s - 1], p1 = P[s], p2 = P[s + 1], p3 = P[s + 2];
    const segLen = Math.hypot(p2[0] - p1[0], p2[1] - p1[1]);
    const n = Math.max(1, Math.ceil(segLen / step));
    for (let k = 0; k < n; k++) {
      const t = k / n, t2 = t * t, t3 = t2 * t;
      const x = 0.5 * ((2 * p1[0]) + (-p0[0] + p2[0]) * t + (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t2 + (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0]) * t3);
      const z = 0.5 * ((2 * p1[1]) + (-p0[1] + p2[1]) * t + (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2 + (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t3);
      if (prev) length += Math.hypot(x - prev[0], z - prev[1]);
      out.push([x, z, length]);
      prev = [x, z];
    }
  }
  const last = points[points.length - 1];
  length += Math.hypot(last[0] - prev[0], last[1] - prev[1]);
  out.push([last[0], last[1], length]);
  return out;
}

export function splineLength(points) {
  const s = sampleSpline(points, 8);
  return s.length ? s[s.length - 1][2] : 0;
}

// Lateral sinuosity so a hand-drawn river does not run dead straight.
function meander(samples, amplitude, wavelength, seed) {
  if (amplitude <= 0 || samples.length < 3) return samples;
  const noise = new SimplexNoise(seed);
  return samples.map((s, i) => {
    const a = samples[Math.max(0, i - 1)], b = samples[Math.min(samples.length - 1, i + 1)];
    let tx = b[0] - a[0], tz = b[1] - a[1];
    const l = Math.hypot(tx, tz) || 1; tx /= l; tz /= l;
    const off = noise.fbm(s[2] / wavelength, 0.37, 2) * amplitude;
    return [s[0] - tz * off, s[1] + tx * off, s[2]];
  });
}

// ---- grid helpers -----------------------------------------------------------------------------
function gridOf(N, size) {
  const cell = size / (N - 1);
  return {
    N, size, cell,
    toGrid: (x, z) => [((x / size) + 0.5) * (N - 1), ((z / size) + 0.5) * (N - 1)],
    toWorld: (i, j) => [(i / (N - 1) - 0.5) * size, (j / (N - 1) - 0.5) * size],
  };
}
function bilinear(arr, N, gx, gz) {
  const i = Math.max(0, Math.min(N - 2, Math.floor(gx)));
  const j = Math.max(0, Math.min(N - 2, Math.floor(gz)));
  const u = Math.min(1, Math.max(0, gx - i)), v = Math.min(1, Math.max(0, gz - j));
  const idx = j * N + i;
  return arr[idx] * (1 - u) * (1 - v) + arr[idx + 1] * u * (1 - v) + arr[idx + N] * (1 - u) * v + arr[idx + N + 1] * u * v;
}
function smoothProfile(values, window) {
  if (window <= 1) return values.slice();
  const half = Math.floor(window / 2);
  const out = new Float64Array(values.length);
  for (let i = 0; i < values.length; i++) {
    let sum = 0, n = 0;
    for (let k = -half; k <= half; k++) {
      const j = i + k;
      if (j < 0 || j >= values.length) continue;
      sum += values[j]; n++;
    }
    out[i] = sum / n;
  }
  return out;
}

// For every cell within `radiusCells` of the polyline: nearest sample index and its distance [m].
function stampNearest(grid, samples, radiusCells) {
  const { N, toGrid, toWorld } = grid;
  const dist = new Float32Array(N * N).fill(Infinity);
  const near = new Int32Array(N * N).fill(-1);
  const touched = [];
  const R = Math.ceil(radiusCells);
  for (let k = 0; k < samples.length; k++) {
    const [sx, sz] = samples[k];
    const [gx, gz] = toGrid(sx, sz);
    const i0 = Math.max(0, Math.floor(gx) - R), i1 = Math.min(N - 1, Math.ceil(gx) + R);
    const j0 = Math.max(0, Math.floor(gz) - R), j1 = Math.min(N - 1, Math.ceil(gz) + R);
    for (let j = j0; j <= j1; j++) {
      for (let i = i0; i <= i1; i++) {
        const [wx, wz] = toWorld(i, j);
        const d = Math.hypot(wx - sx, wz - sz);
        const idx = j * N + i;
        if (d < dist[idx]) {
          if (dist[idx] === Infinity) touched.push(idx);
          dist[idx] = d; near[idx] = k;
        }
      }
    }
  }
  return { dist, near, touched };
}

// ---- rivers (worker, heightfield resolution) --------------------------------------------------
// opts: { width, depth, bankAngle, meander, waterDepth, water }  — all metres / degrees
export function carveRivers(height, N, size, rivers, opts, seed = 1) {
  const grid = gridOf(N, size);
  const riverMask = new Float32Array(N * N);
  const waterLevel = new Float32Array(N * N).fill(NO_WATER);
  const sources = [];
  if (!rivers || rivers.length === 0) return { riverMask, waterLevel, sources };

  const bankSlope = Math.tan((Math.max(10, Math.min(80, opts.bankAngle)) * Math.PI) / 180);
  rivers.forEach((river, r) => {
    if (!river.points || river.points.length < 2) return;
    const width = Math.max(grid.cell, river.width || opts.width);
    const depth = river.depth == null ? opts.depth : river.depth;
    const step = Math.max(0.5, grid.cell * 0.5);
    let samples = sampleSpline(river.points, step);
    samples = meander(samples, opts.meander * width * 1.5, width * 10, seed * 17 + r);
    // bed profile: terrain along the line, smoothed, then monotone downhill from the first point
    let raw = samples.map(([x, z]) => { const [gx, gz] = grid.toGrid(x, z); return bilinear(height, N, gx, gz); });
    let smooth = smoothProfile(raw, Math.round((width * 6) / step) | 1);
    // rivers flow from the higher end: reverse the line if it was drawn uphill
    const n10 = Math.max(1, Math.floor(samples.length * 0.1));
    const headH = smooth.slice(0, n10).reduce((a, b) => a + b, 0) / n10;
    const tailH = smooth.slice(-n10).reduce((a, b) => a + b, 0) / n10;
    if (tailH > headH) { samples.reverse(); raw.reverse(); smooth = Float64Array.from(smooth).reverse(); }
    const bed = new Float64Array(samples.length);
    let running = Infinity;
    for (let k = 0; k < samples.length; k++) { running = Math.min(running, smooth[k]); bed[k] = running - depth; }
    // gentle fall even on flat ground so the water reads as flowing
    for (let k = 1; k < samples.length; k++) bed[k] = Math.min(bed[k], bed[k - 1] - 0.0005 * (samples[k][2] - samples[k - 1][2]));

    // banks are shaped up to `maxBank` above the water; where the line crosses higher ground the
    // channel becomes a gorge with near-vertical walls instead of a valley-wide planar cut
    const maxBank = Math.max(1, opts.maxBank == null ? 30 : opts.maxBank);
    const radiusCells = Math.min(80, (width / 2 + maxBank / bankSlope) / grid.cell + 1);
    const { dist, near, touched } = stampNearest(grid, samples, radiusCells);
    const half = width / 2;
    for (const idx of touched) {
      const d = dist[idx], k = near[idx];
      // concave bed, then banks rising at the bank angle
      let target;
      if (d <= half) target = bed[k] + depth * 0.35 * (d / half) * (d / half);
      else {
        const rise = (d - half) * bankSlope;
        if (rise > maxBank) continue;
        target = bed[k] + depth * 0.35 + rise;
      }
      if (target < height[idx]) height[idx] = target;
      const m = 1 - smoothstep(half, half + width * 0.9, d);
      if (m > riverMask[idx]) riverMask[idx] = m;
      if (opts.water && d <= half + width * 1.5) {
        const wl = bed[k] + depth * 0.35 + opts.waterDepth;
        if (wl > waterLevel[idx]) waterLevel[idx] = wl;
      }
    }
    for (let k = 0; k < samples.length; k += 2) {
      const [gx, gz] = grid.toGrid(samples[k][0], samples[k][1]);
      if (gx > 1 && gz > 1 && gx < N - 2 && gz < N - 2) sources.push(gx, gz);
    }
  });
  return { riverMask, waterLevel, sources };
}

// ---- roads (mesh grid, after erosion) ---------------------------------------------------------
// opts: { width, shoulder, smoothing [m], cutAngle, fillAngle }
export function applyRoads(height, N, size, roads, opts) {
  const grid = gridOf(N, size);
  const roadMask = new Float32Array(N * N);
  if (!roads || roads.length === 0) return { roadMask };
  const cutSlope = Math.tan((opts.cutAngle * Math.PI) / 180);
  const fillSlope = Math.tan((opts.fillAngle * Math.PI) / 180);
  for (const road of roads) {
    if (!road.points || road.points.length < 2) continue;
    const width = Math.max(grid.cell * 0.75, road.width || opts.width);
    const step = Math.max(0.5, Math.min(grid.cell * 0.5, width * 0.25));
    const samples = sampleSpline(road.points, step);
    const raw = samples.map(([x, z]) => { const [gx, gz] = grid.toGrid(x, z); return bilinear(height, N, gx, gz); });
    const prof = smoothProfile(smoothProfile(raw, Math.round(opts.smoothing / step) | 1), Math.round(opts.smoothing / step / 2) | 1);
    const half = width / 2;
    const maxWork = 60;   // [m] tallest cut / fill we bother shaping
    const radiusCells = Math.min(40, (half + opts.shoulder + maxWork / fillSlope) / grid.cell);
    const { dist, near, touched } = stampNearest(grid, samples, radiusCells);
    for (const idx of touched) {
      const d = dist[idx], k = near[idx], p = prof[k];
      const h = height[idx];
      if (d <= half) {
        height[idx] = p;
      } else {
        const out = d - half;
        const hi = p + opts.shoulder * 0.02 + Math.max(0, out - opts.shoulder) * cutSlope;   // cut ceiling
        const lo = p - 0.1 - Math.max(0, out - opts.shoulder) * fillSlope;                    // fill floor
        if (h > hi) height[idx] = hi; else if (h < lo) height[idx] = lo;
      }
      const m = 1 - smoothstep(half - 0.5, half + 0.6, d);
      const shoulderMask = 0.45 * (1 - smoothstep(half + opts.shoulder * 0.5, half + opts.shoulder + 2, d));
      const v = Math.max(m, shoulderMask);
      if (v > roadMask[idx]) roadMask[idx] = v;
    }
  }
  return { roadMask };
}

// ---- lakes (mesh grid) ------------------------------------------------------------------------
// lakes: [{ x, z, level }], opts: { depth, water }
export function applyLakes(height, N, size, lakes, opts, waterLevel) {
  const grid = gridOf(N, size);
  const lakeMask = new Float32Array(N * N);
  if (!lakes || lakes.length === 0) return { lakeMask };
  const visited = new Uint8Array(N * N);
  const maxCells = Math.floor(N * N * 0.45);
  for (const lake of lakes) {
    const [gx, gz] = grid.toGrid(lake.x, lake.z);
    const si = Math.round(gx), sj = Math.round(gz);
    if (si < 0 || sj < 0 || si >= N || sj >= N) continue;
    const start = sj * N + si;
    if (height[start] >= lake.level) continue;
    const level = lake.level;
    const stack = [start];
    const cells = [];
    visited.fill(0);
    visited[start] = 1;
    while (stack.length && cells.length < maxCells) {
      const idx = stack.pop();
      cells.push(idx);
      const i = idx % N, j = (idx - i) / N;
      const nb = [idx - 1, idx + 1, idx - N, idx + N];
      const ok = [i > 0, i < N - 1, j > 0, j < N - 1];
      for (let q = 0; q < 4; q++) {
        if (!ok[q]) continue;
        const n = nb[q];
        if (visited[n] || height[n] >= level) continue;
        visited[n] = 1;
        stack.push(n);
      }
    }
    const bedTop = level - opts.depth;
    for (const idx of cells) {
      // flat silt bed, soft near the shore
      const depthHere = level - height[idx];
      const t = smoothstep(0, opts.depth * 0.6, depthHere);
      height[idx] = Math.min(height[idx], bedTop + (height[idx] - bedTop) * (1 - t));
      lakeMask[idx] = 1;
      if (opts.water && waterLevel && level > waterLevel[idx]) waterLevel[idx] = level;
    }
    // shoreline band in the mask (for silt / wet sand shading)
    for (const idx of cells) {
      const i = idx % N, j = (idx - i) / N;
      for (let dj = -2; dj <= 2; dj++) for (let di = -2; di <= 2; di++) {
        const ii = i + di, jj = j + dj;
        if (ii < 0 || jj < 0 || ii >= N || jj >= N) continue;
        const n = jj * N + ii;
        if (lakeMask[n] === 0) lakeMask[n] = Math.max(lakeMask[n], 0.5 - Math.hypot(di, dj) * 0.12);
      }
    }
  }
  return { lakeMask };
}

// Upsample a water-level map (max over neighbours so banks keep the level).
export function upsampleWaterLevel(src, N, k) {
  const M = (N - 1) * k + 1;
  const out = new Float32Array(M * M).fill(NO_WATER);
  for (let J = 0; J < M; J++) {
    const j = Math.min(N - 2, Math.floor(J / k));
    for (let I = 0; I < M; I++) {
      const i = Math.min(N - 2, Math.floor(I / k));
      const idx = j * N + i;
      out[J * M + I] = Math.max(src[idx], src[idx + 1], src[idx + N], src[idx + N + 1]);
    }
  }
  return out;
}

function smoothstep(a, b, v) {
  const t = Math.min(1, Math.max(0, (v - a) / (b - a)));
  return t * t * (3 - 2 * t);
}

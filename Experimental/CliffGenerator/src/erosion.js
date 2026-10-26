// Erosion passes.
//  • Hydraulic: Lagrangian droplet erosion (Beyer 2015). Each droplet runs downhill carrying
//    sediment; it cuts gullies where it accelerates and deposits fans where it slows. Hardness
//    scales cutting so resistant strata hold their faces.
//  • Thermal: talus-angle slumping. Material steeper than the repose angle slides to the
//    downhill neighbours, producing scree aprons beneath cliffs. Repose angle depends on hardness.
//
// All passes operate on normalised heights (metres / heightScale) so the tuned constants are
// independent of the chosen mountain height.

import { mulberry32 } from './noise.js';

export function hydraulicErosion(height, hardness, params, progress = () => {}) {
  const N = params.resolution;
  const scale = 1 / params.heightScale;
  const h = new Float32Array(N * N);
  for (let i = 0; i < N * N; i++) h[i] = height[i] * scale;

  const droplets = params.droplets;
  const inertia = params.inertia;
  const capacityFactor = params.sedimentCapacity;
  const minCapacity = 0.01;
  const erodeSpeed = params.erodeSpeed;
  const depositSpeed = params.depositSpeed;
  const evaporate = params.evaporation;
  const gravity = params.gravity;
  const maxLifetime = params.dropletLifetime;
  const radius = Math.max(1, Math.round(params.erosionRadius));
  const hardnessInfluence = params.hardnessInfluence;

  const flow = new Float32Array(N * N);
  const delta = new Float32Array(N * N);

  // Brush offsets / weights for distributed erosion.
  const brushX = [], brushY = [], brushW = [];
  {
    let sum = 0;
    for (let y = -radius; y <= radius; y++) {
      for (let x = -radius; x <= radius; x++) {
        const d2 = x * x + y * y;
        if (d2 > radius * radius) continue;
        const w = 1 - Math.sqrt(d2) / radius;
        brushX.push(x); brushY.push(y); brushW.push(w);
        sum += w;
      }
    }
    for (let i = 0; i < brushW.length; i++) brushW[i] /= sum;
  }

  const rand = mulberry32(params.seed * 31 + 9);
  const reportEvery = Math.max(1, Math.floor(droplets / 40));
  // River sources: a share of the droplets start on the drawn river lines carrying more water,
  // so the rivers cut their own beds and the slopes drain towards them.
  const sources = params.sources || [];
  const sourceCount = sources.length / 2;
  const sourceFraction = sourceCount > 0 ? Math.min(0.9, params.sourceFraction || 0) : 0;
  const sourceWater = params.sourceWater || 3;

  for (let d = 0; d < droplets; d++) {
    let px, py, water;
    if (sourceFraction > 0 && rand() < sourceFraction) {
      const k = Math.floor(rand() * sourceCount) * 2;
      px = sources[k] + (rand() - 0.5) * 2.5;
      py = sources[k + 1] + (rand() - 0.5) * 2.5;
      water = sourceWater;
    } else {
      px = rand() * (N - 1);
      py = rand() * (N - 1);
      water = 1;
    }
    let dx = 0, dy = 0;
    let speed = params.initialSpeed;
    let sediment = 0;

    for (let life = 0; life < maxLifetime; life++) {
      const nx = Math.floor(px), ny = Math.floor(py);
      if (nx < 0 || nx >= N - 1 || ny < 0 || ny >= N - 1) break;
      const idx = ny * N + nx;
      const u = px - nx, v = py - ny;

      // Bilinear height & gradient at droplet position.
      const h00 = h[idx], h10 = h[idx + 1], h01 = h[idx + N], h11 = h[idx + N + 1];
      const gx = (h10 - h00) * (1 - v) + (h11 - h01) * v;
      const gy = (h01 - h00) * (1 - u) + (h11 - h10) * u;
      const hOld = h00 * (1 - u) * (1 - v) + h10 * u * (1 - v) + h01 * (1 - u) * v + h11 * u * v;

      dx = dx * inertia - gx * (1 - inertia);
      dy = dy * inertia - gy * (1 - inertia);
      const len = Math.hypot(dx, dy);
      if (len < 1e-7) {
        const a = rand() * Math.PI * 2;
        dx = Math.cos(a); dy = Math.sin(a);
      } else {
        dx /= len; dy /= len;
      }
      px += dx; py += dy;

      if (px < 0 || px >= N - 1 || py < 0 || py >= N - 1) break;
      const mx = Math.floor(px), my = Math.floor(py);
      const midx = my * N + mx;
      const mu = px - mx, mv = py - my;
      const hNew = h[midx] * (1 - mu) * (1 - mv) + h[midx + 1] * mu * (1 - mv)
        + h[midx + N] * (1 - mu) * mv + h[midx + N + 1] * mu * mv;
      const dh = hNew - hOld;

      flow[idx] += water;

      const capacity = Math.max(-dh * speed * water * capacityFactor, minCapacity);

      if (sediment > capacity || dh > 0) {
        // Deposit: fill pits fully, otherwise a fraction of the surplus.
        const amount = dh > 0 ? Math.min(dh, sediment) : (sediment - capacity) * depositSpeed;
        sediment -= amount;
        h[idx] += amount * (1 - u) * (1 - v);
        h[idx + 1] += amount * u * (1 - v);
        h[idx + N] += amount * (1 - u) * v;
        h[idx + N + 1] += amount * u * v;
        delta[idx] += amount * (1 - u) * (1 - v);
        delta[idx + 1] += amount * u * (1 - v);
        delta[idx + N] += amount * (1 - u) * v;
        delta[idx + N + 1] += amount * u * v;
      } else {
        const resist = 1 - hardnessInfluence * hardness[idx];
        const amount = Math.min((capacity - sediment) * erodeSpeed * resist, -dh);
        for (let b = 0; b < brushX.length; b++) {
          const bx = nx + brushX[b], by = ny + brushY[b];
          if (bx < 0 || bx >= N || by < 0 || by >= N) continue;
          const bidx = by * N + bx;
          const w = amount * brushW[b] * (1 - hardnessInfluence * 0.6 * hardness[bidx]);
          const removed = w;
          h[bidx] -= removed;
          delta[bidx] -= removed;
          sediment += removed;
        }
      }

      speed = Math.sqrt(Math.max(0, speed * speed + dh * gravity * -1));
      water *= 1 - evaporate;
      if (water < 0.01) break;
    }
    if (d % reportEvery === 0) progress(d / droplets);
  }

  for (let i = 0; i < N * N; i++) height[i] = h[i] * params.heightScale;
  for (let i = 0; i < N * N; i++) delta[i] *= params.heightScale;
  return { flow, delta };
}

export function thermalErosion(height, hardness, params, progress = () => {}) {
  const N = params.resolution;
  const cell = params.worldSize / (N - 1);
  const iterations = params.thermalIterations;
  const rate = params.thermalRate;
  const softTan = Math.tan((params.talusSoft * Math.PI) / 180);
  const hardTan = Math.tan((params.talusHard * Math.PI) / 180);
  const moved = new Float32Array(N * N);
  const out = new Float32Array(N * N);
  const nbrs = [
    [-1, 0, 1], [1, 0, 1], [0, -1, 1], [0, 1, 1],
    [-1, -1, Math.SQRT2], [1, -1, Math.SQRT2], [-1, 1, Math.SQRT2], [1, 1, Math.SQRT2],
  ];
  const diffs = new Float32Array(8);

  for (let it = 0; it < iterations; it++) {
    out.fill(0);
    for (let j = 1; j < N - 1; j++) {
      for (let i = 1; i < N - 1; i++) {
        const idx = j * N + i;
        const hc = height[idx];
        const hard = hardness[idx];
        const talus = (softTan + (hardTan - softTan) * hard) * cell;
        let total = 0, maxDiff = 0;
        for (let n = 0; n < 8; n++) {
          const nb = nbrs[n];
          const d = hc - height[(j + nb[1]) * N + i + nb[0]];
          const thr = talus * nb[2];
          const ex = d > thr ? d - thr : 0;
          diffs[n] = ex;
          total += ex;
          if (ex > maxDiff) maxDiff = ex;
        }
        if (total <= 0) continue;
        const move = maxDiff * 0.5 * rate;
        for (let n = 0; n < 8; n++) {
          if (diffs[n] <= 0) continue;
          const nb = nbrs[n];
          const share = move * (diffs[n] / total);
          out[(j + nb[1]) * N + i + nb[0]] += share;
          out[idx] -= share;
        }
      }
    }
    for (let k = 0; k < N * N; k++) {
      height[k] += out[k];
      if (out[k] > 0) moved[k] += out[k];
    }
    progress((it + 1) / iterations);
  }
  return moved;
}

// Small separable blur used to settle noisy erosion deltas before they become shading inputs.
// Returns a new array; the input is left untouched.
export function blurField(src, N, passes = 1) {
  let a = new Float32Array(src);
  const b = new Float32Array(N * N);
  for (let p = 0; p < passes; p++) {
    for (let j = 0; j < N; j++) {
      for (let i = 0; i < N; i++) {
        const i0 = Math.max(0, i - 1), i1 = Math.min(N - 1, i + 1);
        b[j * N + i] = (a[j * N + i0] + 2 * a[j * N + i] + a[j * N + i1]) * 0.25;
      }
    }
    for (let j = 0; j < N; j++) {
      const j0 = Math.max(0, j - 1), j1 = Math.min(N - 1, j + 1);
      for (let i = 0; i < N; i++) {
        a[j * N + i] = (b[j0 * N + i] + 2 * b[j * N + i] + b[j1 * N + i]) * 0.25;
      }
    }
  }
  return a;
}

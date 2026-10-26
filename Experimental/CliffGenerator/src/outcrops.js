// Boulder outcrops: clusters of large, half-buried core-stones ("tors", "woolsacks", "encampments")
// unioned into the base relief BEFORE strata and erosion, so they are a landform the rest of the
// pipeline weathers — not props scattered on top of it.
//
// Each boulder is a superellipsoid (rounder than a box, squarer than an egg) with a plan rotation,
// a low-frequency bulge that breaks the symmetry, and a burial depth. Clusters sit on a jittered
// site grid filtered by slope and (optionally) by relative elevation so they crown ridges and
// hill tops the way real tors do.
//
// Returns the outcrop mask (1 inside a boulder footprint, soft rim) — used to suppress strata
// terracing, pin hardness, dry out deposits and switch off bedding undercuts in the 3D chunks.

import { SimplexNoise, mulberry32, smoothstep, clamp01, lerp } from './noise.js';

export function addOutcrops(height, params) {
  const N = params.resolution;
  const mask = new Float32Array(N * N);
  const density = clamp01(params.outcropDensity || 0);
  if (density <= 0) return mask;

  const size = params.worldSize;
  const cell = size / (N - 1);
  const rand = mulberry32((params.seed | 0) * 131 + 9);
  const bulge = new SimplexNoise((params.seed | 0) * 7 + 3);
  const base = Float32Array.from(height); // sample the ground before any boulder lands on it

  const R = Math.max(cell * 2, params.outcropSize || 16);
  const spacing = Math.max(R * 3, params.outcropSpacing || 220);
  const perCluster = Math.max(1, Math.round(params.outcropCount == null ? 5 : params.outcropCount));
  const aspect = params.outcropAspect == null ? 0.7 : params.outcropAspect;
  const bury = clamp01(params.outcropBury == null ? 0.4 : params.outcropBury);
  const ridgeBias = clamp01(params.outcropRidge == null ? 0.5 : params.outcropRidge);
  const slopeMax = Math.tan(((params.outcropSlopeMax == null ? 30 : params.outcropSlopeMax) * Math.PI) / 180);
  const weather = clamp01(params.outcropWeather == null ? 0.5 : params.outcropWeather);
  const spread = params.outcropSpread == null ? 1.6 : params.outcropSpread;

  const sample = (x, z) => {
    const u = clamp01(x / size) * (N - 1), w = clamp01(z / size) * (N - 1);
    const i = Math.min(N - 2, Math.floor(u)), j = Math.min(N - 2, Math.floor(w));
    const fu = u - i, fw = w - j;
    const a = base[j * N + i], b = base[j * N + i + 1], c = base[(j + 1) * N + i], d = base[(j + 1) * N + i + 1];
    return (a * (1 - fu) + b * fu) * (1 - fw) + (c * (1 - fu) + d * fu) * fw;
  };
  const slopeAt = (x, z, r) => {
    const dx = (sample(x + r, z) - sample(x - r, z)) / (2 * r), dz = (sample(x, z + r) - sample(x, z - r)) / (2 * r);
    return Math.hypot(dx, dz);
  };
  const reliefAt = (x, z, r) => {
    let m = 0;
    for (let k = 0; k < 8; k++) { const a = (k / 8) * Math.PI * 2; m += sample(x + Math.cos(a) * r, z + Math.sin(a) * r); }
    return sample(x, z) - m / 8;
  };

  const boulders = [];
  const margin = R * 2;
  const nx = Math.max(1, Math.floor(size / spacing));
  for (let gj = 0; gj < nx; gj++) {
    for (let gi = 0; gi < nx; gi++) {
      // deterministic draws per site so changing density only adds/removes clusters
      const r0 = rand(), r1 = rand(), r2 = rand(), r3 = rand();
      const cx = margin + ((gi + 0.15 + r0 * 0.7) / nx) * (size - 2 * margin);
      const cz = margin + ((gj + 0.15 + r1 * 0.7) / nx) * (size - 2 * margin);
      if (r2 > density) continue;
      if (slopeAt(cx, cz, R) > slopeMax) continue;
      if (ridgeBias > 0) {
        const rel = reliefAt(cx, cz, R * 4);
        const p = lerp(1, smoothstep(-R * 0.3, R * 0.8, rel), ridgeBias);
        if (r3 > p) continue;
      }
      const count = Math.max(1, Math.round(perCluster * (0.6 + rand() * 0.8)));
      const clusterR = R * spread * Math.sqrt(count) * 0.6;
      const lean = rand() * Math.PI; // a cluster shares a dominant joint direction
      for (let b = 0; b < count; b++) {
        // first boulder in the middle (the big one), the rest around it
        const ang = rand() * Math.PI * 2, rad = b === 0 ? 0 : clusterR * Math.sqrt(rand());
        const x = cx + Math.cos(ang) * rad, z = cz + Math.sin(ang) * rad;
        if (slopeAt(x, z, R * 0.5) > slopeMax * 1.5) continue;
        const scale = (b === 0 ? 1 : 0.45 + rand() * 0.6) * (0.8 + rand() * 0.4);
        const rx = R * scale * (0.8 + rand() * 0.5), rz = R * scale * (0.6 + rand() * 0.5);
        const ry = Math.max(rx, rz) * aspect * (0.75 + rand() * 0.5);
        const theta = lean + (rand() - 0.5) * 0.9;
        const p = 2 + (1 - weather) * 1.4 + rand() * 0.4; // superellipsoid exponent: 2 = egg, 3.4 = blocky
        const ground = sample(x, z);
        const cy = ground - ry * bury * (0.7 + rand() * 0.6);
        boulders.push({ x, z, rx, rz, ry, cy, cos: Math.cos(theta), sin: Math.sin(theta), p, seed: rand() * 100 });
      }
    }
  }

  for (const bd of boulders) {
    const reach = Math.max(bd.rx, bd.rz) * 1.25;
    const i0 = Math.max(0, Math.floor((bd.x - reach) / cell)), i1 = Math.min(N - 1, Math.ceil((bd.x + reach) / cell));
    const j0 = Math.max(0, Math.floor((bd.z - reach) / cell)), j1 = Math.min(N - 1, Math.ceil((bd.z + reach) / cell));
    const invP = 1 / bd.p;
    for (let j = j0; j <= j1; j++) {
      const z = j * cell - bd.z;
      for (let i = i0; i <= i1; i++) {
        const x = i * cell - bd.x;
        const u = x * bd.cos + z * bd.sin, w = -x * bd.sin + z * bd.cos;
        // low-frequency bulge so no two boulders are the same egg
        const n = bulge.fbm(u / bd.rx * 1.3 + bd.seed, w / bd.rz * 1.3 - bd.seed, 2, 2, 0.5);
        const grow = 1 + n * 0.16 * (0.5 + weather * 0.5);
        const d = Math.pow(Math.abs(u) / (bd.rx * grow), bd.p) + Math.pow(Math.abs(w) / (bd.rz * grow), bd.p);
        if (d >= 1) continue;
        const idx = j * N + i;
        const top = bd.cy + bd.ry * Math.pow(1 - d, invP) * (1 + n * 0.06);
        if (top > height[idx]) {
          height[idx] = top;
          const m = smoothstep(1, 0.75, d);
          if (m > mask[idx]) mask[idx] = m;
        }
      }
    }
  }
  return mask;
}

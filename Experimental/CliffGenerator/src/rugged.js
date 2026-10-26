// Rugged outcrops: lateral (XZ) push–pull of the steep ground.
//
// A heightfield can only move up and down, so every detail stage so far has been a Z operation
// and the cliff faces came out as smooth sheets that only the SDF stage and the shader roughen.
// Real cliff faces are pushed and pulled *sideways*: blocky noses stand proud of the face, recesses
// and chimneys are cut back into it, and the beds step in and out between them (Gaea does this with
// its Rugged / Outcrops / Rocky nodes — a sharp, slope-masked domain warp of the terrain).
//
// Here the face is moved along its own horizontal normal: for a steep cell the height (and the
// maps that ride on it) is re-sampled from a point displaced up- or down-slope by a blocky
// multi-octave noise, so the face advances where the noise is positive (a buttress) and retreats
// where it is negative (a recess). Flat ground is not displaced at all, so valleys, rivers and
// plains keep their shape; the mask is blurred so a whole buttress moves together instead of
// each cell on its own. A fraction of the same noise is also added vertically (ledges), which is
// what makes the blocks read as blocks rather than as waves in the contour lines.

import { SimplexNoise, GradientNoise3, smoothstep } from './noise.js';

function stair(v, steps, sharp) {
  // smooth staircase on v (any range): plateaus with short steep risers between them
  const s = v * steps;
  const f = s - Math.floor(s);
  const e = 0.5 - 0.5 * sharp;
  const r = smoothstep(e, 1 - e, f);
  return (Math.floor(s) + r) / steps;
}

function blockify(v, steps, sharp) {
  // v ≈ −1..1 → plateaus and risers over the same range
  const t = Math.min(1, Math.max(0, (v + 1) * 0.5));
  return stair(t, steps, sharp) * 2 - 1;
}

export function ruggedPushPull(height, N, size, opts, maps = []) {
  const amount = opts.amount || 0;
  if (amount <= 0) return null;
  const total = N * N;
  const cell = size / (N - 1);
  const L = Math.max(cell * 3, opts.scale || 70);
  const blocky = Math.min(1, Math.max(0, opts.blockiness == null ? 0.7 : opts.blockiness));
  const ledges = Math.max(0, opts.ledges == null ? 0.5 : opts.ledges);
  const tMin = Math.tan(((opts.slopeMin == null ? 28 : opts.slopeMin) * Math.PI) / 180);
  const tMax = Math.tan(((opts.slopeMax == null ? 50 : opts.slopeMax) * Math.PI) / 180);
  const n2 = new SimplexNoise((opts.seed || 1) * 29 + 5);
  const n3 = new GradientNoise3((opts.seed || 1) * 31 + 17);

  // slope mask (blurred so a buttress moves as one piece)
  let mask = new Float32Array(total);
  const gx = new Float32Array(total), gz = new Float32Array(total);
  for (let j = 0; j < N; j++) {
    const j0 = Math.max(0, j - 1), j1 = Math.min(N - 1, j + 1);
    for (let i = 0; i < N; i++) {
      const i0 = Math.max(0, i - 1), i1 = Math.min(N - 1, i + 1);
      const c = j * N + i;
      const dx = (height[j * N + i1] - height[j * N + i0]) / ((i1 - i0) * cell);
      const dz = (height[j1 * N + i] - height[j0 * N + i]) / ((j1 - j0) * cell);
      gx[c] = dx; gz[c] = dz;
      mask[c] = smoothstep(tMin, tMax, Math.hypot(dx, dz));
    }
  }
  const r = Math.max(1, Math.round(L * 0.25 / cell));
  for (let pass = 0; pass < 2; pass++) {
    const tmp = new Float32Array(total);
    for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) {
      let s = 0, n = 0;
      for (let dj = -r; dj <= r; dj++) {
        const jj = j + dj; if (jj < 0 || jj >= N) continue;
        for (let di = -r; di <= r; di++) { const ii = i + di; if (ii < 0 || ii >= N) continue; s += mask[jj * N + ii]; n++; }
      }
      tmp[j * N + i] = s / n;
    }
    mask = tmp;
  }

  // displacement field (metres, world XZ)
  const dxF = new Float32Array(total), dzF = new Float32Array(total), dyF = new Float32Array(total);
  const stepsY = Math.max(1, Math.round((opts.bands || 3)));
  for (let j = 0; j < N; j++) {
    const z = (j / (N - 1) - 0.5) * size;
    for (let i = 0; i < N; i++) {
      const c = j * N + i;
      const m = mask[c];
      if (m < 0.01) continue;
      const x = (i / (N - 1) - 0.5) * size;
      const y = height[c];
      // blocky noise: coarse blocks in 3-D (so the noses sit at particular levels of the face),
      // finer blocks on top, each octave turned into plateaus and risers
      let b = n3.fbm(x / L, y / (L * 0.55), z / L, 2, 2.1, 0.5) * 1.4;
      b = (1 - blocky) * b + blocky * blockify(b, stepsY, 0.75);
      let f = n2.fbm(x / (L * 0.33) + 7.3, z / (L * 0.33) - 2.1, 3, 2.2, 0.5) * 1.4;
      f = (1 - blocky) * f + blocky * blockify(f, 2, 0.7);
      const v = b * 0.72 + f * 0.38; // ≈ −1..1
      // along the horizontal normal of the face (downhill = outward)
      const gl = Math.hypot(gx[c], gz[c]) || 1;
      const nxh = -gx[c] / gl, nzh = -gz[c] / gl;
      const d = amount * m * v;
      dxF[c] = nxh * d; dzF[c] = nzh * d;
      dyF[c] = amount * ledges * m * v * 0.35;
    }
  }
  // smooth the displacement a touch so adjacent cells do not fold over each other
  for (const F of [dxF, dzF]) {
    const tmp = new Float32Array(total);
    for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) {
      let s = 0, n = 0;
      for (let dj = -1; dj <= 1; dj++) { const jj = j + dj; if (jj < 0 || jj >= N) continue;
        for (let di = -1; di <= 1; di++) { const ii = i + di; if (ii < 0 || ii >= N) continue; s += F[jj * N + ii]; n++; } }
      tmp[j * N + i] = s / n;
    }
    F.set(tmp);
  }

  // re-sample: the face at p takes the height that was at p − D (the face moves by +D)
  const srcH = height.slice();
  const srcMaps = maps.map((m) => m.slice());
  const sample = (src, fx, fz) => {
    const x0 = Math.floor(fx), z0 = Math.floor(fz);
    const tx = fx - x0, tz = fz - z0;
    const x1 = Math.min(N - 1, x0 + 1), z1 = Math.min(N - 1, z0 + 1);
    const a = src[z0 * N + x0], b = src[z0 * N + x1], c = src[z1 * N + x0], d = src[z1 * N + x1];
    return (a * (1 - tx) + b * tx) * (1 - tz) + (c * (1 - tx) + d * tx) * tz;
  };
  let moved = 0;
  for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) {
    const c = j * N + i;
    const dx = dxF[c], dz = dzF[c];
    if (dx === 0 && dz === 0 && dyF[c] === 0) continue;
    const fx = Math.min(N - 1, Math.max(0, i - dx / cell));
    const fz = Math.min(N - 1, Math.max(0, j - dz / cell));
    height[c] = sample(srcH, fx, fz) + dyF[c];
    for (let k = 0; k < maps.length; k++) maps[k][c] = sample(srcMaps[k], fx, fz);
    moved++;
  }
  return { mask, moved };
}

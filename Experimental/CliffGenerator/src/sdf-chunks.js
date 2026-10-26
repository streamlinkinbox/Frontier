// True-3D cliffs: local SDF chunks over the steep parts of the refined heightfield, seamless with the
// surrounding heightfield mesh by construction (see Frontier/Experimental/SdfCliffLab for the
// isolated proof). Pure JS — chunk meshing runs in sdf.worker.js. The heightfield stays the
// terrain's single source of truth; only the steep chunks selected here are replaced, and only
// the field inside them comes from the SDF volume work (src/sdf-cliff-volume.js).
//
//   field  a dense local volume (chunk footprint + halo) seeded with the heightfield base field —
//          y − h(x,z) where W = 0, the slope-normalised distance with the vertical-wall shear and
//          lean inside — then filled with the editor's own strata and weathered in the volume:
//          differential retreat of soft beds and joint slots, narrow-band front feedback, and
//          rounded-box detachment of joint-bounded blocks left unsupported
//   W      cliff weight: 1 on cells steeper than `sdfAngle`, fading to 0 over `sdfBlend` cells, 0 on
//          every face shared with a non-chunked cell and on the map border. The cropped volume is
//          blended back to plain y − h by W, so W = 0 columns are exact whatever the passes did
//   mesh   marching cubes on a voxel grid aligned with the heightfield cells; where W == 0 the
//          field is linear along every edge, so iso-crossings sit exactly on the heightfield's own
//          piecewise-linear border → watertight with the terrain mesh, which skips the covered quads

import { SimplexNoise, GradientNoise3, hash2, smoothstep, lerp, clamp01 } from './noise.js';
import { makeBedTable, bedAt, bedJitter, bedHardness } from './strata-model.js';
import { NO_WATER } from './features.js';
import { edgeTable, triTable } from './mc-tables.js';
import { makeDetail } from './detail.js';
import { CliffVolume, weatherCliffVolume, detachJointBlocks } from './sdf-cliff-volume.js';

const DX8 = [1, -1, 0, 0, 1, 1, -1, -1], DZ8 = [0, 0, 1, -1, 1, -1, 1, -1];

// Chunks are C×C heightfield cells; the last row / column may be narrower (grid cells are rarely a
// multiple of the chunk size). Chunk (ci, cj) covers cells [ci·C, min(ci·C + C, N−1)).
export function chunkCellsFor(N, want) {
  return Math.max(4, Math.min(64, Math.round(want || 16)));
}

// ---- selection on the refined field --------------------------------------------------------------
export function selectChunks(fine, v) {
  const { resolution: N, worldSize: size, height } = fine;
  const cell = size / (N - 1);
  const tanA = Math.tan(((v.sdfAngle || 58) * Math.PI) / 180);
  const r = Math.max(1, Math.min(6, Math.round(v.sdfBlend || 2)));
  const total = N * N;
  // chamfer distance (in cells) from cliff nodes, up to r
  const dist = new Uint8Array(total).fill(255);
  for (let j = 1; j < N - 1; j++) {
    for (let i = 1; i < N - 1; i++) {
      const idx = j * N + i;
      const gx = (height[idx + 1] - height[idx - 1]) / (2 * cell), gz = (height[idx + N] - height[idx - N]) / (2 * cell);
      if (gx * gx + gz * gz > tanA * tanA) dist[idx] = 0;
    }
  }
  for (let pass = 1; pass <= r; pass++) {
    for (let j = 1; j < N - 1; j++) {
      for (let i = 1; i < N - 1; i++) {
        const idx = j * N + i;
        if (dist[idx] !== 255) continue;
        for (let d = 0; d < 8; d++) {
          if (dist[idx + DX8[d] + DZ8[d] * N] === pass - 1) { dist[idx] = pass; break; }
        }
      }
    }
  }
  const W = new Float32Array(total);
  for (let idx = 0; idx < total; idx++) {
    const d = dist[idx];
    if (d === 255) continue;
    const i = idx % N, j = (idx - i) / N;
    if (i === 0 || j === 0 || i === N - 1 || j === N - 1) continue;
    W[idx] = smoothstep(0, 1, 1 - d / (r + 1));
  }

  const C = chunkCellsFor(N, v.sdfChunk || 16);
  const nc = Math.ceil((N - 1) / C);
  const candidates = [];
  for (let cj = 0; cj < nc; cj++) {
    for (let ci = 0; ci < nc; ci++) {
      const i0 = ci * C, j0 = cj * C, i1 = Math.min(N - 1, i0 + C), j1 = Math.min(N - 1, j0 + C);
      let sum = 0, hmin = Infinity, hmax = -Infinity, g2max = 0;
      for (let j = j0; j <= j1; j++) {
        for (let i = i0; i <= i1; i++) {
          const idx = j * N + i;
          sum += W[idx];
          const h = height[idx]; if (h < hmin) hmin = h; if (h > hmax) hmax = h;
          if (i > 0 && j > 0 && i < N - 1 && j < N - 1) {
            const gx = (height[idx + 1] - height[idx - 1]) / (2 * cell), gz = (height[idx + N] - height[idx - N]) / (2 * cell);
            const g2 = gx * gx + gz * gz; if (g2 > g2max) g2max = g2;
          }
        }
      }
      if (sum > 0) candidates.push({ ci, cj, i0, j0, cw: i1 - i0, ch: j1 - j0, hmin, hmax, sum, slopeF: Math.sqrt(1 + g2max) });
    }
  }
  candidates.sort((a, b) => b.sum - a.sum);
  const maxChunks = Math.max(1, Math.round(v.sdfMaxChunks || 600));
  const list = candidates.slice(0, maxChunks);
  const active = new Uint8Array(nc * nc);
  for (const c of list) active[c.cj * nc + c.ci] = 1;
  // W must be 0 on faces shared with inactive chunks: zero every node that touches a non-active chunk
  if (list.length < candidates.length) {
    for (let j = 0; j < N; j++) {
      for (let i = 0; i < N; i++) {
        const idx = j * N + i;
        if (W[idx] === 0) continue;
        const ci0 = Math.max(0, Math.floor((i - 1) / C)), ci1 = Math.min(nc - 1, Math.floor(i / C));
        const cj0 = Math.max(0, Math.floor((j - 1) / C)), cj1 = Math.min(nc - 1, Math.floor(j / C));
        let inactive = false;
        for (let cj = cj0; cj <= cj1 && !inactive; cj++) for (let ci = ci0; ci <= ci1; ci++) if (!active[cj * nc + ci]) { inactive = true; break; }
        if (inactive) W[idx] = 0;
      }
    }
  }
  // fade for the heightfield mesh displacement: 0 on/inside active footprints, 1 three cells away
  const fade = new Float32Array(total).fill(1);
  for (const c of list) {
    for (let j = c.j0; j <= c.j0 + c.ch; j++) for (let i = c.i0; i <= c.i0 + c.cw; i++) fade[j * N + i] = 0;
  }
  for (let pass = 1; pass <= 3; pass++) {
    const level = pass / 4;
    for (let j = 1; j < N - 1; j++) {
      for (let i = 1; i < N - 1; i++) {
        const idx = j * N + i;
        if (fade[idx] < 1) continue;
        for (let d = 0; d < 8; d++) {
          const f = fade[idx + DX8[d] + DZ8[d] * N];
          if (f < level - 1e-6) { fade[idx] = level; break; }
        }
      }
    }
  }
  fine.sdfWeight = W;
  fine.sdfFade = fade;
  return { active, nc, C, list, W, fade, candidates: candidates.length };
}

// ---- jobs for the worker (slices of every map, transferable) ---------------------------------------
export function packChunkJobs(fine, chunks, v) {
  const { resolution: N } = fine;
  const { C, list } = chunks;
  const names = ['height', 'hardness', 'deposit', 'flow', 'cavity', 'road', 'river', 'lake', 'waterLevel', 'waterPaint', 'sdfWeight', 'outcrop', 'crest', 'foot', 'gmax', 'gxs', 'gzs'];
  const jobs = [], transfer = [];
  const cell = fine.worldSize / (N - 1);
  const maxCarve = carveReach(v);
  const leanR = Math.max(2, Math.round(Math.min(80, (v.sdfLeanReach || 24)) / cell));
  const vertical = Math.max(0, v.sdfVertical || 0);
  if (((v.sdfLean || 0) > 0 || vertical > 0) && (!fine.crest || fine._crestR !== leanR)) {
    // local crest / foot of the face (separable max / min filters + a blur) for the lean
    const r = leanR;
    fine._crestR = r;
    const run = (src, pick) => {
      const a = new Float32Array(N * N), b = new Float32Array(N * N);
      for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) { let m = src[j * N + i]; for (let d = -r; d <= r; d++) { const ii = i + d; if (ii >= 0 && ii < N) m = pick(m, src[j * N + ii]); } a[j * N + i] = m; }
      for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) { let m = a[j * N + i]; for (let d = -r; d <= r; d++) { const jj = j + d; if (jj >= 0 && jj < N) m = pick(m, a[jj * N + i]); } b[j * N + i] = m; }
      // box blur so the lean does not step where the filter window changes
      for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) { let sum = 0, n = 0; for (let dj = -2; dj <= 2; dj++) { const jj = j + dj; if (jj < 0 || jj >= N) continue; for (let di = -2; di <= 2; di++) { const ii = i + di; if (ii < 0 || ii >= N) continue; sum += b[jj * N + ii]; n++; } } a[j * N + i] = sum / n; }
      return a;
    };
    fine.crest = run(fine.height, Math.max);
    fine.foot = run(fine.height, Math.min);
    // steepest gradient in the window (→ plan width of the face) and the smoothed uphill direction
    const H = fine.height, gm = new Float32Array(N * N), gx = new Float32Array(N * N), gz = new Float32Array(N * N);
    for (let j = 1; j < N - 1; j++) for (let i = 1; i < N - 1; i++) {
      const idx = j * N + i;
      const a = (H[idx + 1] - H[idx - 1]) / (2 * cell), b = (H[idx + N] - H[idx - N]) / (2 * cell);
      gx[idx] = a; gz[idx] = b; gm[idx] = Math.hypot(a, b);
    }
    fine.gmax = run(gm, Math.max);
    // the direction is weighted by the gradient magnitude so the face dictates it, not the plateau
    const dirBox = (src) => {
      const a = new Float32Array(N * N), b = new Float32Array(N * N);
      for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) { let sum = 0; for (let d = -r; d <= r; d++) { const ii = i + d; if (ii >= 0 && ii < N) sum += src[j * N + ii]; } a[j * N + i] = sum; }
      for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) { let sum = 0; for (let d = -r; d <= r; d++) { const jj = j + d; if (jj >= 0 && jj < N) sum += a[jj * N + i]; } b[j * N + i] = sum; }
      return b;
    };
    const wx = new Float32Array(N * N), wz = new Float32Array(N * N), g2 = new Float32Array(N * N);
    for (let i = 0; i < N * N; i++) { wx[i] = gx[i] * gm[i]; wz[i] = gz[i] * gm[i]; g2[i] = gm[i] * gm[i]; }
    fine.gxs = dirBox(wx); fine.gzs = dirBox(wz);
    // steepness-weighted mean gradient of the window: the face's own slope (the plateau and the
    // foot hardly count, the steepest cell does not dominate) → plan width of the face
    const sg = dirBox(gm), sg2 = dirBox(g2);
    // the plan width of the face lies between (crest − foot) / max slope and / mean slope: use the
    // geometric mean of the two (calibrated on escarpments: vertical = 1 → a vertical wall)
    const gmx = fine.gmax;
    for (let i = 0; i < N * N; i++) gmx[i] = sg[i] > 1e-6 ? Math.sqrt(gmx[i] * (sg2[i] / sg[i])) : gmx[i];
  }
  // slices are padded so the vertical-wall shear can sample the heightfield beyond the chunk, and
  // so the SDF volume's halo reads real neighbouring terrain instead of a clamped edge. The halo
  // is ~6 m (5–14 voxels); expressed in heightfield cells that is min(6 / cell, 14 / k), and the
  // final k is not known yet in auto mode, so bound it with k = 1 and add one cell for bilinear
  // support. At coarse cells the old 6-cell minimum still applies.
  const verticalPad = vertical > 0 ? Math.min(24, Math.ceil((Math.min(80, v.sdfLeanReach || 24) * Math.max(1, vertical) * 0.6) / cell) + 1) : 0;
  const haloPad = Math.min(16, Math.ceil(Math.min(6 / cell, 14)) + 1);
  const pad = Math.max(6, haloPad, verticalPad);
  for (const c of list) {
    const Sx = c.cw + 1 + 2 * pad, Sz = c.ch + 1 + 2 * pad;
    const maps = {};
    for (const name of names) {
      const src = fine[name];
      const out = new Float32Array(Sx * Sz);
      if (src) {
        for (let j = 0; j < Sz; j++) {
          const jj = Math.min(N - 1, Math.max(0, c.j0 + j - pad));
          for (let i = 0; i < Sx; i++) { const ii = Math.min(N - 1, Math.max(0, c.i0 + i - pad)); out[j * Sx + i] = src[jj * N + ii]; }
        }
      } else if (name === 'waterLevel' || name === 'waterPaint') out.fill(NO_WATER);
      maps[name] = out; transfer.push(out.buffer);
    }
    jobs.push({ ci: c.ci, cj: c.cj, i0: c.i0, j0: c.j0, cw: c.cw, ch: c.ch, pad, hmin: c.hmin, hmax: c.hmax, slopeF: c.slopeF, maps });
  }
  // voxel resolution: explicit, or the finest that fits the voxel budget (only the band around the
  // surface is polygonised, so the estimate is footprint × band thickness)
  let k = Math.max(1, Math.min(4, Math.round(v.sdfVoxel || 0)));
  if (!(v.sdfVoxel >= 1)) {
    let est = 0;
    for (const c of list) {
      let band = (maxCarve * Math.min(3, c.slopeF) * 2) / cell + 6;
      // a sheared (vertical) wall is polygonised over the whole foot…crest band of each column
      if (vertical > 0) band = Math.max(band, 0.6 * ((c.hmax - c.hmin) / cell) + 6);
      est += c.cw * c.ch * band;
    }
    est *= 1.35; // the volume halo and its weathering passes cost more than the cropped band
    const budget = Math.max(1e6, (v.sdfVoxelBudget || 24) * 1e6);
    k = est * 64 <= budget ? 4 : est * 27 <= budget ? 3 : est * 8 <= budget ? 2 : 1;
  }
  return { jobs, transfer, meta: { N, size: fine.worldSize, C, k, cell: cell / k } };
}

// largest distance the volume weathering can move the surface along the normal (bulk retreat of
// the softest, most jointed bed, plus lean, detail and a detached block's depth)
export function carveReach(v) {
  const undercut = Math.max(0, v.sdfUndercut == null ? 6 : v.sdfUndercut);
  const pits = v.sdfPits == null ? 0.3 : v.sdfPits, joints = v.sdfJoints == null ? 0.5 : v.sdfJoints;
  const detail = (v.detailRelief || 0) + (v.rockyAmount || 0) + (v.cragAmount || 0);
  const blocks = Math.max(0, v.sdfBlocks || 0);
  const blockDepth = joints > 0.03 && (blocks > 0 || undercut >= 3)
    ? Math.min(16, Math.max(2.5, (v.sdfBlockSize || 8) * 0.65)) + blocks * 0.45 : 0;
  return undercut * (1 + 0.65 * joints + 0.35 * pits) + 1.5 + detail
    + Math.max(0, v.sdfPushPull || 0) * 0.35 + Math.max(0, v.sdfLean || 0) + blockDepth;
}

// ---- strata hardness at a 3-D point (same model as heightfield.applyStrata) -----------------------
// column(x, z) caches everything that depends on the plan position; at(col, y) is cheap per voxel.
export function makeStrataSampler(params) {
  const size = params.worldSize;
  const dipRad = (params.strataDip * Math.PI) / 180, dirRad = (params.strataDipDirection * Math.PI) / 180;
  const gx = Math.tan(dipRad) * Math.cos(dirRad), gz = Math.tan(dipRad) * Math.sin(dirRad);
  const detail = new SimplexNoise(params.seed * 5 + 77);
  const contrast = params.hardnessContrast;
  const table = makeBedTable(params);
  const lateral = params.strataLateral == null ? 0.18 : params.strataLateral;
  function column(xw, zw) {
    const x = xw + size / 2, z = zw + size / 2;
    return { x, z, tilt: gx * x + gz * z, jitter: bedJitter(x, z, lateral), layers: new Map(), bed: {} };
  }
  // returns [hardness, fraction within bed (0 base → 1 top), bed thickness, bed index]
  function at(col, y) {
    const bed = bedAt(table, (y + col.tilt) / col.jitter, col.bed);
    const bi = bed.index;
    let lens = col.layers.get(bi);
    if (lens === undefined) {
      lens = 0.12 * detail.fbm(col.x * 0.006 + bi * 3.7, col.z * 0.006, 3);
      col.layers.set(bi, lens);
    }
    const hard = lerp(0.35, clamp01(bedHardness(bed) + lens), contrast);
    return [hard, bed.f, bed.thick, bi];
  }
  return { column, at, table, sample: (xw, zw, y) => at(column(xw, zw), y) };
}

// ---- marching cubes ------------------------------------------------------------------------------
const CORNER = [[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0], [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1]];
const EDGES = [[0, 1], [1, 2], [2, 3], [3, 0], [4, 5], [5, 6], [6, 7], [7, 4], [0, 4], [1, 5], [2, 6], [3, 7]];

export function makeChunkContext(v) {
  return {
    seed: (v.seed | 0) >>> 0,
    strata: makeStrataSampler(v),
    pocket: new GradientNoise3((v.seed || 1) * 41 + 9),
    joint: new SimplexNoise((v.seed || 1) * 23 + 5),
    joint3: new GradientNoise3((v.seed || 1) * 43 + 17),
    rough: new GradientNoise3((v.seed || 1) * 31 + 11),
  };
}

// ---- joint model (from the SDF terrain volume, re-based on this editor's strata) ------------------
// Three near-vertical fracture families. Spacing grows with the local bed thickness (the observed
// rule in sedimentary rock: thick beds, wide joints) blended with the editor's block-scale
// control, the phase is re-seeded per bed so joints terminate at bedding boundaries, the sets
// swing slowly across the massif, and persistence masks stop any joint running forever.
const JOINT_SETS = [
  { az: 22, spacingK: 0.95, persistence: 0.85, seed: 11 },
  { az: 109, spacingK: 1.30, persistence: 0.75, seed: 47 },
  { az: -52, spacingK: 2.10, persistence: 0.45, seed: 83 },
].map((s) => ({ ...s, nx: Math.cos(s.az * Math.PI / 180), nz: Math.sin(s.az * Math.PI / 180) }));

function jointProximity(ctx, x, y, z, bedIndex, thick, vox, ca, sa, blockScale) {
  let best = 0;
  for (const set of JOINT_SETS) {
    const nx = set.nx * ca - set.nz * sa, nz = set.nx * sa + set.nz * ca;
    const spacing = Math.min(140, Math.max(Math.max(1.1, vox * 1.6),
      (0.62 * thick + 0.38 * blockScale) * set.spacingK));
    const u = (x * nx + z * nz) / spacing + hash2(bedIndex * 3 + set.seed, bedIndex - set.seed, ctx.seed + 901) * 7.31;
    const frac = u - Math.floor(u);
    const dist = Math.min(frac, 1 - frac) * spacing;
    const halfWidth = Math.min(Math.max(vox * 0.75, vox * 2.4), Math.max(vox * 0.75, spacing * 0.09));
    if (dist > halfWidth * 2.4) continue;
    const strike = -x * nz + z * nx;
    const pn = 0.5 + 0.5 * ctx.joint3.fbm(strike / (spacing * 1.7), y / (spacing * 0.85), set.seed * 0.173, 2, 2.05, 0.55);
    const persist = set.persistence * smoothstep(0.30, 0.62, pn);
    const prox = (1 - smoothstep(0, halfWidth, dist)) * clamp01(persist * 1.65);
    if (prox > best) best = prox;
  }
  return best;
}

// ---- the chunk field: a dense local SDF volume around the chunk footprint -------------------------
// The volume is seeded with the heightfield's own base field (plain y - h where W = 0, the
// slope-normalised distance plus the vertical-wall shear and lean inside), filled with the
// editor's strata (hardness with bedding-plane weakness and intra-bed heterogeneity, bed
// thickness, the joint model above, a seepage gate), then weathered: an integrated bulk retreat
// for the deep differential undercut, a few narrow-band Hamilton-Jacobi passes for feedback,
// and rounded-box detachment of joint-bounded blocks left unsupported. Only the cropped
// interior is returned for marching cubes; the halo exists so those passes never see a chunk
// boundary, and the final blend back to y - h by W makes the heightfield seam exact.
export function buildChunkField(job, meta, v, ctx) {
  const { N, size } = meta;
  const pad = job.pad || 0;
  const Sx = job.cw + 1 + 2 * pad, Sz = job.ch + 1 + 2 * pad;
  const cell = size / (N - 1);
  const k = v.sdfVoxel >= 1 ? Math.max(1, Math.min(4, Math.round(v.sdfVoxel))) : (meta.k || 1);
  const vox = cell / k;
  const nx = job.cw * k, nz = job.ch * k;
  const x0 = (job.i0 / (N - 1) - 0.5) * size, z0 = (job.j0 / (N - 1) - 0.5) * size;
  const m = job.maps;

  // local bilinear sampler on the (padded) slice (u, w in cells from the chunk origin)
  const bil = (arr, u, w) => {
    u += pad; w += pad;
    const i = Math.min(Sx - 2, Math.max(0, Math.floor(u))), j = Math.min(Sz - 2, Math.max(0, Math.floor(w)));
    const fu = Math.min(1, Math.max(0, u - i)), fw = Math.min(1, Math.max(0, w - j));
    const a = arr[j * Sx + i], b = arr[j * Sx + i + 1], c = arr[(j + 1) * Sx + i], d = arr[(j + 1) * Sx + i + 1];
    return (a + (b - a) * fu) * (1 - fw) + (c + (d - c) * fu) * fw;
  };
  const grad = (u, w) => {
    u += pad; w += pad;
    const i = Math.min(Sx - 2, Math.max(0, Math.floor(u))), j = Math.min(Sz - 2, Math.max(0, Math.floor(w)));
    const fu = Math.min(1, Math.max(0, u - i)), fw = Math.min(1, Math.max(0, w - j));
    const h = m.height;
    const a = h[j * Sx + i], b = h[j * Sx + i + 1], c = h[(j + 1) * Sx + i], d = h[(j + 1) * Sx + i + 1];
    return [((b - a) * (1 - fw) + (d - c) * fw) / cell, ((c - a) * (1 - fu) + (d - b) * fu) / cell];
  };

  const undercut = Math.max(0, v.sdfUndercut == null ? 6 : v.sdfUndercut);
  const pocketScale = Math.max(1, v.sdfPockets || 12);
  const pits = v.sdfPits == null ? 0.3 : v.sdfPits;
  const joints = v.sdfJoints == null ? 0.5 : v.sdfJoints;
  // roughness wavelength is tied to the voxel size (≥ 5 voxels) so it cannot alias into stair-steps
  const roughAmp = (v.sdfRough == null ? 0.5 : v.sdfRough) * Math.min(1.2, vox * 0.3);
  const roughFreq = 1 / (vox * 5);
  const bedPower = v.sdfBedContrast == null ? 1 : v.sdfBedContrast;
  const band = Math.max(2, v.strataBand || 26);
  const hasOutcrop = !!m.outcrop;

  // the same rocky facets / crags as the heightfield mesh, evaluated in 3-D on the chunk faces
  // (feature size clamped to ≥ 2.5 voxels so blocks cannot alias into voxel steps)
  const detail = makeDetail({ worldSize: size, resolution: N }, { ...v, detailRelief: 0 }, { minScale: vox * 2.5, limit: 1e9 }); // fbm relief is already there as `rough`
  // 3D push–pull becomes differential panel relief: susceptible panels retreat a little further
  // so resistant joint-bounded panels stand proud — no outward noise displacement of the face
  const pushAmp = Math.max(0, v.sdfPushPull || 0);
  const pushScale = Math.max(vox * 3, v.sdfPushScale || 18);
  // lean: the face is cut back progressively from its crest to its foot, so the profile is | / —
  // vertical to overhanging — instead of the back-leaning slope a heightfield gives
  const leanAmp = Math.max(0, v.sdfLean || 0);
  const hasLean = leanAmp > 0 && !!m.crest && !!m.foot;
  // vertical wall: the face is sheared so its crest moves out and its foot moves in — at 1 the
  // contours of the face collapse onto one line (a vertical wall), above 1 the wall overhangs
  // (\\ /). A point at height y takes the heightfield from a plan position shifted uphill by
  // (t − ½)·W·vertical, t = (y − foot)/(crest − foot), W = plan width of the face in the heightfield
  const vertical = Math.max(0, v.sdfVertical || 0);
  const hasVertical = vertical > 0 && !!m.crest && !!m.foot && !!m.gmax && pad > 0;
  const vertMaxW = Math.min(80, v.sdfLeanReach || 24) * 1.2;
  const needCF = hasLean || hasVertical;
  // joint-bounded block detachment (the old analytic block wall is gone: blocks now come from
  // the joint field in the volume — `sdfBlocks` deepens/strengthens the detachment)
  const blockAmp = Math.max(0, v.sdfBlocks || 0);
  const blockSize = Math.max(2, v.sdfBlockSize || 8);
  const blockLoss = Math.min(1, Math.max(0, v.sdfBlockLoss == null ? 0.12 : v.sdfBlockLoss));
  const blockSwing = Math.max(0, Math.min(1, v.sdfBlockSwing == null ? 0.6 : v.sdfBlockSwing));
  const blockEnabled = joints > 0.03 && (blockAmp > 0 || undercut >= 3);
  const bump = roughAmp + detail.amp;
  const maxCarve = carveReach(v);
  const blockReach = blockEnabled ? Math.min(Math.max(blockSize * 0.7, vox * 3), vox * 7) : 0;
  const reachNormal = Math.max(maxCarve, blockReach, vox * 3);

  // everything that depends on the plan position only
  function column(x, z) {
    const u = (x - x0) / cell, wq = (z - z0) / cell;
    const h = bil(m.height, u, wq);
    const w = bil(m.sdfWeight, u, wq);
    const [gx, gz] = grad(u, wq);
    const slopeF = Math.sqrt(1 + gx * gx + gz * gz);
    const gl = Math.hypot(gx, gz);
    const steep = smoothstep(0.7, 1.7, gl);
    const oc = hasOutcrop ? bil(m.outcrop, u, wq) : 0;
    const deposit = m.deposit ? bil(m.deposit, u, wq) : 0;
    // the joint sets swing slowly across the massif: joint sets curve around a dome in the
    // real thing, and a single axis-aligned grid over every wall is what reads as a tiled texture
    const dang = blockSwing * 0.85 * ctx.joint.fbm(x * 0.0062, z * 0.0062, 1);
    const ca = Math.cos(dang), sa = Math.sin(dang);
    const crest = needCF ? bil(m.crest, u, wq) : 0, foot = needCF ? bil(m.foot, u, wq) : 0;
    let vW = 0, vdx = 0, vdz = 0;
    if (hasVertical && w > 1e-6) {
      const gm = bil(m.gmax, u, wq);
      const dxs = bil(m.gxs, u, wq), dzs = bil(m.gzs, u, wq);
      const dl = Math.hypot(dxs, dzs);
      if (gm > 0.5 && dl > 1e-6 && crest - foot > 2) {
        vW = Math.min(vertMaxW, (crest - foot) / gm) * vertical * w;
        vdx = dxs / dl; vdz = dzs / dl; // unit uphill direction
      }
    }
    return { x, z, h, w, slopeF, steep, oc, deposit, crest, foot, vW, vdx, vdz, ca, sa, _effSteep: steep,
      strata: w > 1e-6 && (steep > 0 || vW > 0) ? ctx.strata.column(x, z) : null };
  }

  // the unweathered base field: exactly y - h where W = 0, the slope-normalised distance (with
  // the vertical-wall shear and the lean folded in) where the chunk owns the surface
  function baseField(col, y) {
    let h = col.h, slopeF = col.slopeF, steep = col.steep;
    if (col.vW > 0) {
      const t = Math.min(1, Math.max(0, (y - col.foot) / Math.max(1, col.crest - col.foot)));
      // eased so the plateau behind the crest and the ground before the foot are not dragged
      const e = t * t * (3 - 2 * t);
      const shift = (e - 0.5) * col.vW;
      const u = (col.x + col.vdx * shift - x0) / cell, wq = (col.z + col.vdz * shift - z0) / cell;
      h = bil(m.height, u, wq);
      const [gx, gz] = grad(u, wq);
      const g = Math.hypot(gx, gz);
      slopeF = Math.sqrt(1 + g * g);
      steep = Math.max(steep, smoothstep(0.7, 1.7, g));
    }
    col._effSteep = steep;
    const plain = y - col.h;
    if (col.w <= 1e-6 || steep <= 0) return plain;
    let field = (y - h) / slopeF;
    if (hasLean) {
      const faceH = Math.max(4, col.crest - col.foot);
      const down = Math.min(1, Math.max(0, (col.crest - y) / faceH));
      // deepest just above the foot, eased out at the very bottom so the face meets the ground
      field += leanAmp * down * (1 - 0.5 * smoothstep(0.85, 1, down)) * (1 - col.oc) * steep;
    }
    return plain + (field - plain) * col.w;
  }

  // haloed volume bounds: at least ~6 m of halo (5–14 voxels) so the weathering passes and the
  // block-support probes never see the volume boundary from the cropped interior
  const halo = Math.max(5, Math.min(14, Math.ceil(6 / vox) || 5));
  const vx = nx + 2 * halo + 1, vz = nz + 2 * halo + 1;
  const xv0 = x0 - halo * vox, zv0 = z0 - halo * vox;
  const cols = new Array(vx * vz);
  let hmin = Infinity, hmax = -Infinity, fmin = Infinity, cmax = -Infinity, slopeMax = 1;
  for (let jz = 0; jz < vz; jz++) {
    for (let jx = 0; jx < vx; jx++) {
      const col = column(xv0 + jx * vox, zv0 + jz * vox);
      cols[jz * vx + jx] = col;
      if (col.h < hmin) hmin = col.h;
      if (col.h > hmax) hmax = col.h;
      if (col.slopeF > slopeMax) slopeMax = col.slopeF;
      if (hasVertical && col.w > 1e-6) {
        if (col.foot < fmin) fmin = col.foot;
        if (col.crest > cmax) cmax = col.crest;
      }
    }
  }
  // vertical range: a retreat d along the normal moves the surface up to d·|∇| vertically
  let yLo = Math.floor((hmin - reachNormal * Math.min(3, slopeMax) - 2 * vox) / vox) * vox;
  let yHi = Math.ceil((hmax + bump + 2 * vox) / vox) * vox;
  if (hasVertical && fmin < Infinity) {
    // the sheared wall spans foot…crest in every column of the face
    yLo = Math.min(yLo, Math.floor((fmin - 2 * vox) / vox) * vox);
    yHi = Math.max(yHi, Math.ceil((cmax + bump + 2 * vox) / vox) * vox);
  }
  const ny = Math.max(1, Math.round((yHi - yLo) / vox));
  const vy = ny + 1;
  const volume = new CliffVolume(vx, vy, vz, vox, [xv0, yLo, zv0]);

  // fill: base field + geology + integrated bulk retreat, in the surface band of each column
  for (let jz = 0; jz < vz; jz++) {
    for (let jx = 0; jx < vx; jx++) {
      const col = cols[jz * vx + jx];
      const c = jz * vx + jx;
      volume.weight[c] = col.w;
      volume.height[c] = col.h;
      volume.steep[c] = col.steep;
      volume.outcrop[c] = col.oc;
      let lo = Math.max(0, Math.floor((col.h - reachNormal * col.slopeF - 2 * vox - yLo) / vox) - 1);
      let hi = Math.min(ny, Math.ceil((col.h + bump + 2 * vox - yLo) / vox) + 1);
      if (col.vW > 0) {
        lo = Math.min(lo, Math.max(0, Math.floor((col.foot - 2 * vox - yLo) / vox) - 1));
        hi = Math.max(hi, Math.min(ny, Math.ceil((col.crest + bump + 2 * vox - yLo) / vox) + 1));
      }
      volume.bandLo[c] = lo; volume.bandHi[c] = hi;
      const x = col.x, z = col.z;
      for (let jy = 0; jy <= ny; jy++) {
        const y = yLo + jy * vox;
        const idx = (jz * vy + jy) * vx + jx;
        const plain = y - col.h;
        if (jy < lo || jy > hi) { volume.sdf[idx] = plain; continue; }
        const field = baseField(col, y);
        let retreat = 0;
        if (col.w > 1e-6 && col.strata) {
          const [hard0, f, thick, bi] = ctx.strata.at(col.strata, y);
          // seepage / alcove gate: coarse 3-D variation that only modulates how fast susceptible
          // rock retreats — it never carves hard rock by itself (that was the banding bug)
          const seep01 = clamp01(0.5 + 0.5 * ctx.pocket.fbm(x / pocketScale, y / (pocketScale * 0.6), z / pocketScale, 3, 2.1, 0.55));
          // bedding-plane weakness + mild intra-bed heterogeneity (from the SDF volume geology)
          const seamDist = Math.min(f, 1 - f) * thick;
          const seam = 1 - 0.28 * (1 - smoothstep(0, Math.max(vox * 0.8, 0.3), seamDist));
          let hard = clamp01(hard0 * seam * (0.94 + 0.12 * seep01));
          if (col.oc > 0) hard = lerp(hard, 0.93, col.oc * 0.8); // embedded boulders are massive rock
          const joint01 = jointProximity(ctx, x, y, z, bi, thick, vox, col.ca, col.sa, pushScale) * (1 - col.oc * 0.65);
          volume.bed[idx] = bi;
          volume.bedThick[idx] = thick;
          volume.hardness[idx] = Math.round(hard * 255);
          volume.joint[idx] = Math.round(joint01 * 255);
          volume.seep[idx] = Math.round(seep01 * 255);
          volume.sediment[idx] = clamp01(col.deposit * 0.9) * (1 - col.steep * 0.6)
            * smoothstep(vox * 1.5, -vox * 1.5, plain);
          // differential susceptibility: soft beds recede into a notch deepest just under the hard
          // bed above (seepage and frost work hardest there); thin partings only groove, thick
          // soft beds undercut deeply; joints open slots even in hard beds; pits are granular
          // disintegration of soft, seepage-wet rock only
          const softness = Math.pow(1 - smoothstep(0.3, 0.62, hard), bedPower);
          const notch = 0.3 + 0.7 * Math.sin(Math.PI * Math.pow(Math.min(1, Math.max(0, f)), 1.6));
          const thickF = Math.pow(Math.min(1, thick / (band * 0.5)), 0.5);
          const pocketGate = 0.36 + 0.64 * smoothstep(0.32, 0.72, seep01);
          const bedRate = softness * notch * thickF * pocketGate;
          const jointRate = 0.55 * joints * joint01 * joint01 * (0.25 + 0.75 * (1 - hard)) * (0.4 + 0.6 * pocketGate);
          const pitRate = 0.32 * pits * softness * smoothstep(0.68, 0.86, seep01);
          volume.susceptibility[idx] = Math.round(255 * clamp01(bedRate + jointRate + pitRate) * (1 - col.oc * 0.75));
          // integrated bulk retreat — most of the weathering depth, from the geology at the node;
          // the volume passes below add the front-interaction feedback on top
          retreat = undercut * (bedRate + jointRate + pitRate)
            + pushAmp * 0.18 * (0.25 + 0.75 * softness) * (0.35 + 0.65 * seep01);
          retreat = Math.min(reachNormal, retreat * (1 - col.oc * 0.75) * col._effSteep * col.w);
        }
        let roughOffset = 0;
        if (col.w > 1e-6 && col._effSteep > 0) {
          const rough = ctx.rough.fbm(x * roughFreq, y * roughFreq, z * roughFreq, 2, 2.1, 0.55) * roughAmp;
          const det = detail.amp > 0 ? detail.at(x, y, z, 1 / col.slopeF, volume.hardness[idx] / 255) : 0;
          roughOffset = (rough - det) * col._effSteep * col.w;
        }
        volume.sdf[idx] = col.w <= 1e-6 ? plain : field + retreat + roughOffset;
      }
    }
  }

  // narrow-band feedback passes (count capped by the halo so a shared chunk face stays exact)
  const weatherPasses = undercut > 0 ? Math.min(4, Math.max(0, halo - 2)) : 0;
  const relax = Math.min(undercut * 0.2, vox * 1.5);
  const weatherStats = weatherPasses > 0 && relax > 0
    ? weatherCliffVolume(volume, { iterations: weatherPasses, step: relax / weatherPasses, band: Math.max(2.2, vox * 2.4), topExposure: 0.1, sedimentProtect: 2.0 })
    : { iterations: 0, totalDelta: 0, active: 0 };

  // joint-bounded blocks the undercutting left unsupported drop out of the face
  const blockStats = blockEnabled
    ? detachJointBlocks(volume, {
        seed: ctx.seed,
        strength: blockAmp > 0 ? 0.55 + 0.45 * Math.min(1, blockAmp / 3) : 0.38,
        loss: blockLoss, blockSize, depth: blockAmp,
        maxBlocks: Math.max(4, Math.min(36, Math.round(6 + (nx * nz) / 220))),
        nodeBounds: { ix0: halo + 1, ix1: halo + nx - 2, iz0: halo + 1, iz1: halo + nz - 2 },
        interior: { minX: x0, maxX: x0 + nx * vox, minZ: z0, maxZ: z0 + nz * vox },
      })
    : { candidates: 0, detached: 0 };

  // crop the interior and blend back to the plain heightfield by W — at W = 0 the field is
  // exactly y - h whatever the volume passes did nearby, which is the seam invariant
  const sx = nx + 1, sy = ny + 1, sz = nz + 1;
  const vals = new Float32Array(sx * sy * sz);
  const bandLo = new Int32Array(sx * sz), bandHi = new Int32Array(sx * sz);
  for (let iz = 0; iz < sz; iz++) {
    for (let ix = 0; ix < sx; ix++) {
      const vc = (iz + halo) * vx + (ix + halo);
      const c = iz * sx + ix;
      bandLo[c] = volume.bandLo[vc]; bandHi[c] = volume.bandHi[vc];
      const w = volume.weight[vc], h = volume.height[vc];
      for (let iy = 0; iy < sy; iy++) {
        const plain = yLo + iy * vox - h;
        const vol = volume.sdf[((iz + halo) * vy + iy) * vx + (ix + halo)];
        vals[(iz * sy + iy) * sx + ix] = w <= 1e-6 ? plain : plain + (vol - plain) * w;
      }
    }
  }
  return { vals, bandLo, bandHi, nx, ny, nz, x0, yLo, z0, vox, cell, halo, volume,
    stats: { halo, weatherPasses: weatherStats.iterations, weatherDelta: weatherStats.totalDelta,
      blockCandidates: blockStats.candidates, blocksDetached: blockStats.detached } };
}

export function buildChunkGeometry(job, meta, v, ctx) {
  const field = buildChunkField(job, meta, v, ctx);
  const { vals, bandLo, bandHi, nx, ny, nz, x0, yLo, z0, vox, cell } = field;
  const pad = job.pad || 0;
  const Sx = job.cw + 1 + 2 * pad, Sz = job.ch + 1 + 2 * pad;
  const m = job.maps;
  const bil = (arr, u, w) => {
    u += pad; w += pad;
    const i = Math.min(Sx - 2, Math.max(0, Math.floor(u))), j = Math.min(Sz - 2, Math.max(0, Math.floor(w)));
    const fu = Math.min(1, Math.max(0, u - i)), fw = Math.min(1, Math.max(0, w - j));
    const a = arr[j * Sx + i], b = arr[j * Sx + i + 1], c = arr[(j + 1) * Sx + i], d = arr[(j + 1) * Sx + i + 1];
    return (a + (b - a) * fu) * (1 - fw) + (c + (d - c) * fu) * fw;
  };
  const sx = nx + 1, sy = ny + 1, sz = nz + 1;
  const idx = (ix, iy, iz) => (iz * sy + iy) * sx + ix;

  // welded output: one vertex per crossed grid edge (axis, ix, iy, iz)
  const positions = [], indices = [];
  const edgeIds = new Int32Array(3 * sx * sy * sz).fill(-1);
  const EDGE_AXIS = [0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2];
  const EDGE_OFF = [[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 0], [0, 0, 1], [1, 0, 1], [0, 1, 1], [0, 0, 1], [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]];
  const cv = new Float64Array(8), cp = new Float64Array(24);
  const vid = new Int32Array(12);
  for (let iz = 0; iz < nz; iz++) {
    for (let ix = 0; ix < nx; ix++) {
      const c00 = iz * sx + ix, c10 = c00 + 1, c01 = c00 + sx, c11 = c01 + 1;
      const iyLo = Math.max(0, Math.min(bandLo[c00], bandLo[c10], bandLo[c01], bandLo[c11]));
      const iyHi = Math.min(ny - 1, Math.max(bandHi[c00], bandHi[c10], bandHi[c01], bandHi[c11]));
      for (let iy = iyLo; iy <= iyHi; iy++) {
        let cube = 0;
        for (let c = 0; c < 8; c++) {
          const val = vals[idx(ix + CORNER[c][0], iy + CORNER[c][1], iz + CORNER[c][2])];
          cv[c] = val; if (val < 0) cube |= 1 << c;
          cp[c * 3] = x0 + (ix + CORNER[c][0]) * vox; cp[c * 3 + 1] = yLo + (iy + CORNER[c][1]) * vox; cp[c * 3 + 2] = z0 + (iz + CORNER[c][2]) * vox;
        }
        const edges = edgeTable[cube];
        if (edges === 0) continue;
        for (let e = 0; e < 12; e++) {
          if (!(edges & (1 << e))) continue;
          const off = EDGE_OFF[e];
          const key = (EDGE_AXIS[e] * sz + iz + off[2]) * sy * sx + (iy + off[1]) * sx + ix + off[0];
          let id = edgeIds[key];
          if (id < 0) {
            const a = EDGES[e][0], b = EDGES[e][1];
            const t = cv[a] / (cv[a] - cv[b]);
            id = positions.length / 3;
            positions.push(cp[a * 3] + (cp[b * 3] - cp[a * 3]) * t, cp[a * 3 + 1] + (cp[b * 3 + 1] - cp[a * 3 + 1]) * t, cp[a * 3 + 2] + (cp[b * 3 + 2] - cp[a * 3 + 2]) * t);
            edgeIds[key] = id;
          }
          vid[e] = id;
        }
        // field gradient over the cube → outward direction for the winding check
        const gx = (cv[1] + cv[2] + cv[5] + cv[6]) - (cv[0] + cv[3] + cv[4] + cv[7]);
        const gy = (cv[2] + cv[3] + cv[6] + cv[7]) - (cv[0] + cv[1] + cv[4] + cv[5]);
        const gz = (cv[4] + cv[5] + cv[6] + cv[7]) - (cv[0] + cv[1] + cv[2] + cv[3]);
        const base = cube * 16;
        for (let t = 0; triTable[base + t] !== -1; t += 3) {
          let ia = vid[triTable[base + t]], ib = vid[triTable[base + t + 1]], ic = vid[triTable[base + t + 2]];
          if (ia === ib || ib === ic || ia === ic) continue;
          const ax = positions[ia * 3], ay = positions[ia * 3 + 1], az = positions[ia * 3 + 2];
          const ux = positions[ib * 3] - ax, uy = positions[ib * 3 + 1] - ay, uz = positions[ib * 3 + 2] - az;
          const wx = positions[ic * 3] - ax, wy = positions[ic * 3 + 1] - ay, wz = positions[ic * 3 + 2] - az;
          const nx_ = uy * wz - uz * wy, ny_ = uz * wx - ux * wz, nz_ = ux * wy - uy * wx;
          if (nx_ * gx + ny_ * gy + nz_ * gz < 0) { const tmp = ib; ib = ic; ic = tmp; }
          indices.push(ia, ib, ic);
        }
      }
    }
  }

  const count = positions.length / 3;
  const pos = new Float32Array(positions);
  const normals = new Float32Array(count * 3);
  const aux = new Float32Array(count * 4), aux2 = new Float32Array(count * 4);
  const g = new Float64Array(3);
  const gAt = (ix, iy, iz) => {
    const xa = Math.max(0, ix - 1), xb = Math.min(nx, ix + 1), ya = Math.max(0, iy - 1), yb = Math.min(ny, iy + 1), za = Math.max(0, iz - 1), zb = Math.min(nz, iz + 1);
    g[0] = (vals[idx(xb, iy, iz)] - vals[idx(xa, iy, iz)]) / (xb - xa);
    g[1] = (vals[idx(ix, yb, iz)] - vals[idx(ix, ya, iz)]) / (yb - ya);
    g[2] = (vals[idx(ix, iy, zb)] - vals[idx(ix, iy, za)]) / (zb - za);
  };
  for (let n = 0; n < count; n++) {
    const x = pos[n * 3], y = pos[n * 3 + 1], z = pos[n * 3 + 2];
    const u = Math.min(nx - 1e-6, Math.max(0, (x - x0) / vox)), vv = Math.min(ny - 1e-6, Math.max(0, (y - yLo) / vox)), w = Math.min(nz - 1e-6, Math.max(0, (z - z0) / vox));
    const ix = Math.floor(u), iy = Math.floor(vv), iz = Math.floor(w), fx = u - ix, fy = vv - iy, fz = w - iz;
    let sx_ = 0, sy_ = 0, sz_ = 0;
    for (let c = 0; c < 8; c++) {
      const wgt = (CORNER[c][0] ? fx : 1 - fx) * (CORNER[c][1] ? fy : 1 - fy) * (CORNER[c][2] ? fz : 1 - fz);
      if (wgt === 0) continue;
      gAt(ix + CORNER[c][0], iy + CORNER[c][1], iz + CORNER[c][2]);
      sx_ += g[0] * wgt; sy_ += g[1] * wgt; sz_ += g[2] * wgt;
    }
    const l = Math.hypot(sx_, sy_, sz_) || 1;
    normals[n * 3] = sx_ / l; normals[n * 3 + 1] = sy_ / l; normals[n * 3 + 2] = sz_ / l;
    // surface channels sampled by XZ (the shader is world-space, no UVs)
    const cu = (x - x0) / cell, cw = (z - z0) / cell;
    aux[n * 4] = bil(m.deposit, cu, cw);
    aux[n * 4 + 1] = bil(m.flow, cu, cw);
    // hardness from the 3-D strata model so beds read correctly on the undercut faces
    aux[n * 4 + 2] = ctx.strata.sample(x, z, y)[0];
    aux[n * 4 + 3] = bil(m.cavity, cu, cw);
    aux2[n * 4] = bil(m.road, cu, cw);
    aux2[n * 4 + 1] = bil(m.river, cu, cw);
    aux2[n * 4 + 2] = bil(m.lake, cu, cw);
    aux2[n * 4 + 3] = m.waterPaint ? bil(m.waterPaint, cu, cw) : m.waterLevel[Math.min(Sz - 1, Math.max(0, Math.round(cw) + pad)) * Sx + Math.min(Sx - 1, Math.max(0, Math.round(cu) + pad))];
  }
  const index = count > 65535 ? new Uint32Array(indices) : new Uint16Array(indices);
  return { positions: pos, normals, aux, aux2, index, voxels: nx * ny * nz, triangles: indices.length / 3, sdfVolume: field.stats };
}

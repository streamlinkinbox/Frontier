// True-3D cliffs: local SDF chunks over the steep parts of the refined heightfield, seamless with the
// surrounding heightfield mesh by construction (see Frontier/Experimental/SdfCliffLab for the
// isolated proof). Pure JS — chunk meshing runs in sdf.worker.js.
//
//   field  f(p) = mix( y − h(x,z),  (y − h) / |∇|  + carve(p),  W(x,z) )
//   W      cliff weight: 1 on cells steeper than `sdfAngle`, fading to 0 over `sdfBlend` cells, 0 on
//          every face shared with a non-chunked cell and on the map border
//   carve  ≥ 0 removes rock along the normal: soft strata beds (same strata model as the heightfield,
//          evaluated at the 3-D point's elevation) are undercut by pocket noise, plus joints,
//          weathering pits and fine roughness — so hard beds are left as lips and overhangs
//   mesh   marching tetrahedra on a voxel grid aligned with the heightfield cells; where W == 0 the
//          field is linear along every edge, so iso-crossings sit exactly on the heightfield's own
//          piecewise-linear border → watertight with the terrain mesh, which skips the covered quads

import { SimplexNoise, GradientNoise3, hash2, smoothstep, lerp, clamp01 } from './noise.js';
import { makeBedTable, bedAt, bedJitter, bedHardness } from './strata-model.js';
import { NO_WATER } from './features.js';
import { edgeTable, triTable } from './mc-tables.js';
import { makeDetail } from './detail.js';

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
  // slices are padded so the vertical-wall shear can sample the heightfield beyond the chunk
  const pad = vertical > 0 ? Math.min(24, Math.ceil((Math.min(80, v.sdfLeanReach || 24) * Math.max(1, vertical) * 0.6) / cell) + 1) : 0;
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
    const budget = Math.max(1e6, (v.sdfVoxelBudget || 24) * 1e6);
    k = est * 64 <= budget ? 4 : est * 27 <= budget ? 3 : est * 8 <= budget ? 2 : 1;
  }
  return { jobs, transfer, meta: { N, size: fine.worldSize, C, k, cell: cell / k } };
}

// largest distance the carve can move the surface along the normal
export function carveReach(v) {
  const undercut = Math.max(0, v.sdfUndercut == null ? 6 : v.sdfUndercut);
  const pits = v.sdfPits == null ? 0.3 : v.sdfPits, joints = v.sdfJoints == null ? 0.5 : v.sdfJoints;
  const detail = (v.detailRelief || 0) + (v.rockyAmount || 0) + (v.cragAmount || 0);
  const blocks = Math.max(0, v.sdfBlocks || 0);
  return undercut * (1 + 0.5 * pits + 0.9 * joints) + 1.5 + detail + Math.max(0, v.sdfPushPull || 0) + Math.max(0, v.sdfLean || 0)
    + blocks * 1.5 + (blocks > 0 ? Math.min(v.sdfBlockSize || 8, 12) * 0.6 : 0);
}

function stair(v, steps, sharp) {
  const s = v * steps, f = s - Math.floor(s), e = 0.5 - 0.5 * sharp;
  return (Math.floor(s) + smoothstep(e, 1 - e, f)) / steps;
}
function blockify(v, steps, sharp) {
  const t = Math.min(1, Math.max(0, (v + 1) * 0.5));
  return stair(t, steps, sharp) * 2 - 1;
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
    rough: new GradientNoise3((v.seed || 1) * 31 + 11),
    push: new GradientNoise3((v.seed || 1) * 37 + 29),
  };
}

export function buildChunkGeometry(job, meta, v, ctx) {
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

  // the same rocky facets / crags / relief as the heightfield mesh, evaluated in 3-D on the carved
  // faces (feature size clamped to ≥ 2.5 voxels so blocks cannot alias into voxel steps)
  const detail = makeDetail({ worldSize: size, resolution: N }, { ...v, detailRelief: 0 }, { minScale: vox * 2.5, limit: 1e9 }); // fbm relief is already there as `rough`
  // true 3-D push–pull: blocky buttresses stand out of the face (negative carve = outward) and
  // recesses go back into it, varying with height so a block can overhang the recess below it
  const pushAmp = Math.max(0, v.sdfPushPull || 0);
  const pushL = Math.max(vox * 4, v.sdfPushScale || 18);
  const pushBlock = Math.min(1, Math.max(0, v.ruggedBlockiness == null ? 0.7 : v.ruggedBlockiness));
  // lean: the face is cut back progressively from its crest to its foot, so the profile is | / —
  // vertical to overhanging — instead of the back-leaning slope a heightfield gives
  const leanAmp = Math.max(0, v.sdfLean || 0);
  const hasLean = leanAmp > 0 && !!m.crest && !!m.foot;
  // vertical wall: the face is sheared so its crest moves out and its foot moves in — at 1 the
  // contours of the face collapse onto one line (a vertical wall), above 1 the wall overhangs
  // (\ /). A point at height y takes the heightfield from a plan position shifted uphill by
  // (t − ½)·W·vertical, t = (y − foot)/(crest − foot), W = plan width of the face in the heightfield
  const vertical = Math.max(0, v.sdfVertical || 0);
  const hasVertical = vertical > 0 && !!m.crest && !!m.foot && !!m.gmax && pad > 0;
  const vertMaxW = Math.min(80, v.sdfLeanReach || 24) * 1.2;
  const needCF = hasLean || hasVertical;
  // rock blocks: the face is a wall of discrete blocks — bed rows (strata) × two vertical joint
  // families (world-aligned planes, staggered and resized bed to bed like brickwork). Each block
  // stands proud of or sits back from the face by its own amount with a bevelled rim, every block
  // boundary is a crack groove, and some blocks have fallen out. A joint family parallel to the
  // face would only show as spurious lines, so its boundaries are blended out by the face direction.
  const blockAmp = Math.max(0, v.sdfBlocks || 0);
  const blockSize = Math.max(2, v.sdfBlockSize || 8);
  const blockLoss = Math.min(1, Math.max(0, v.sdfBlockLoss == null ? 0.12 : v.sdfBlockLoss));
  const blockMinL = vox * 3; // a block narrower than 3 voxels cannot be resolved
  const blockSwing = Math.max(0, Math.min(1, v.sdfBlockSwing == null ? 0.6 : v.sdfBlockSwing));
  const crackW = Math.max(vox * 1.2, blockSize * 0.07);
  // joint-set domains: real rock is broken into panels, each with its own joint spacing and its
  // own block grid (and the grids do not line up across a panel boundary). Without this the blocks
  // read as one wallpaper over every face in the world.
  const blkDom = Math.max(14, blockSize * 5);
  const bump = roughAmp + detail.amp + pushAmp + blockAmp;
  const maxCarve = carveReach(v);
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
    // visibility of the joint families on this face: planes x = const show when the face normal
    // has little x (the gradient points along z), and vice versa
    // the block lattice swings slowly across the massif: joint sets curve around a dome in the
    // real thing, and a single axis-aligned grid over every wall is what reads as a tiled texture
    const dang = blockSwing * 0.85 * ctx.joint.fbm(x * 0.0062, z * 0.0062, 1);
    const ca = Math.cos(dang), sa = Math.sin(dang);
    const grx = gx * ca - gz * sa, grz = gx * sa + gz * ca;
    const visX = gl > 1e-6 ? Math.abs(grz) / gl : 0.7, visZ = gl > 1e-6 ? Math.abs(grx) / gl : 0.7;
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
    // this column's joint-set domain: block size, block depth and the phase of the block grid
    const dgx = Math.floor(x / blkDom), dgz = Math.floor(z / blkDom);
    const d0 = hash2(dgx, dgz, ctx.seed + 337), d1 = hash2(dgx, dgz, ctx.seed + 431);
    return { x, z, h, w, slopeF, steep, oc, crest, foot, vW, vdx, vdz, visX, visZ, ca, sa, bc: null,
      bf: 0.6 + 0.95 * d0, jf: 0.62 + 0.66 * d1, bpx: (d1 - 0.5) * 1.1, bpz: (d0 - 0.5) * 1.1, strata: w > 1e-6 && (steep > 0 || vW > 0) ? ctx.strata.column(x, z) : null };
  }
  // per-column, per-bed constants: the joint lattice and the block lattice depend on the plan
  // position and the bed only (spacing, stagger and hashes), so a column of samples reuses them
  // for every voxel of that bed — the noise and hashing in them is the bulk of a carve sample
  function bedPlan(col, bi, thick) {
    const { x, z } = col;
    const spacing = Math.max(2, Math.min(30, thick * 0.9 * col.bf));
    const bc = {
      bi,
      jn: 0.5 + 0.5 * ctx.joint.fbm(x / spacing + bi * 3.1, z / spacing - bi * 1.7, 2),
      gone0: hash2(Math.floor(x / spacing + bi * 0.37) + bi * 131, Math.floor(z / spacing - bi * 0.61), ctx.seed) < 0.05 * joints ? 1 : 0,
    };
    if (blockAmp <= 0) return bc;
    // lattice of this bed: size and stagger from the bed index, so rows do not line up
    const hb0 = hash2(bi, 0, ctx.seed + 101), hb1 = hash2(bi, 1, ctx.seed + 101);
    const L = Math.max(blockMinL, Math.min(60, blockSize * col.bf * (0.7 + 0.6 * hb0) * Math.min(1.6, Math.max(0.6, thick / blockSize + 0.4))));
    const ex = (x - x0) * col.ca - (z - z0) * col.sa, ez = (x - x0) * col.sa + (z - z0) * col.ca;
    const qx = (ex + hb0 * L * 2.3) / L + col.bpx, qz = (ez + hb1 * L * 1.7) / L + col.bpz;
    const sX = Math.max(0.08, 1 - col.visX), sZ = Math.max(0.08, 1 - col.visZ);
    const cx = qx - 0.5, cz = qz - 0.5;
    const ix = Math.floor(cx), iz = Math.floor(cz);
    const fx = qx - Math.floor(qx), fz = qz - Math.floor(qz);
    Object.assign(bc, { ix, iz,
      bX: smoothstep(0.5 - 0.5 * sX, 0.5 + 0.5 * sX, cx - ix), bZ: smoothstep(0.5 - 0.5 * sZ, 0.5 + 0.5 * sZ, cz - iz),
      gbase: Math.min(L, thick, 4 * blockAmp + 2 * vox) * 0.7,
      tf0: col.crest - 1.5 * thick - 2 * vox, tf1: col.crest - 0.3 * thick,
      dPlan: Math.min(Math.min(fx, 1 - fx) * L + (1 - col.visX) * L, Math.min(fz, 1 - fz) * L + (1 - col.visZ) * L),
      r: Math.min(L, thick) * (0.15 + 0.35 * hash2(Math.floor(qx) + bi * 31, Math.floor(qz) - bi * 17, ctx.seed + 19)) + crackW,
      base: new Float64Array(4), S: new Float64Array(4), M: new Float64Array(4), lost: new Uint8Array(4) });
    for (let c = 0; c < 4; c++) {
      const i = ix + (c & 1), k = iz + (c >> 1);
      if (hash2(i + bi * 7919, k, ctx.seed + 7) < blockLoss * 0.5) { bc.lost[c] = 1; continue; } // fallen out
      const h2 = hash2(i - bi * 104729, k + 3, ctx.seed + 11), h3 = hash2(i * 3 + bi, k * 5 - bi, ctx.seed + 13);
      bc.base[c] = (h2 - 0.5) * 2 * blockAmp;
      // a slight tilt of the block face (not quite flush, like a slab that has shifted)
      bc.S[c] = (h3 - 0.5) * (fx - 0.5) * col.visX + (h2 * 7 % 1 - 0.5) * (fz - 0.5) * col.visZ;
      bc.M[c] = h3 * 5 % 1 - 0.5;
    }
    return bc;
  }
  function carve(col, y) {
    const { x, z } = col;
    const [hard, f, thick, bi] = ctx.strata.at(col.strata, y);
    let bc = col.bc;
    if (!bc || bc.bi !== bi) bc = col.bc = bedPlan(col, bi, thick);
    // soft beds recede into a notch that is deepest just under the hard bed above (where seepage
    // and frost work hardest); thin partings only make a shallow groove, thick soft beds a deep
    // undercut; hard beds keep their lip
    const softness = Math.pow(1 - smoothstep(0.3, 0.6, hard), bedPower);
    const notch = 0.3 + 0.7 * Math.sin(Math.PI * Math.pow(Math.min(1, Math.max(0, f)), 1.6));
    const thickF = Math.pow(Math.min(1, thick / (band * 0.5)), 0.5);
    let soft = softness * notch * thickF;
    const n = 0.5 + 0.5 * ctx.pocket.fbm(x / pocketScale, y / (pocketScale * 0.6), z / pocketScale, 3, 2.1, 0.55);
    const pocket = smoothstep(0.32, 0.7, n);
    // hard beds break along joint sets spaced like their thickness → blocky columns; soft beds
    // weather smooth. Each bed has its own joint pattern, and now and then a whole block is gone.
    const hardness = smoothstep(0.45, 0.7, hard);
    let joint = smoothstep(0.7, 0.82, bc.jn) * joints * hardness;
    const fallen = bc.gone0 ? smoothstep(0.04, 0.16, f) * smoothstep(0.98, 0.9, f) * hardness : 0;
    const pit = smoothstep(0.66, 0.82, n) * pits;
    const rough = ctx.rough.fbm(x * roughFreq, y * roughFreq, z * roughFreq, 2, 2.1, 0.55) * roughAmp;
    // embedded boulders are massive rock: no bedding undercuts, spheroidal weathering only
    if (col.oc > 0) { soft *= 1 - col.oc; joint *= 1 - col.oc * 0.5; }
    const det = detail.amp > 0 ? detail.at(x, y, z, 1 / col.slopeF, hard) : 0;
    let push = 0;
    if (pushAmp > 0) {
      const pn = ctx.push.fbm(x / pushL, y / (pushL * 0.55), z / pushL, 2, 2.1, 0.5) * 1.4;
      push = pushAmp * ((1 - pushBlock) * pn + pushBlock * blockify(pn, 3, 0.75)) * (1 - col.oc * 0.7);
    }
    let block = 0;
    if (blockAmp > 0) {
      // the lattice of a bed over one plan position is the same for every voxel of that bed, and
      // the hashing it needs is the costly part of a sample → cache it on the column
      let bc = col.bc;
      if (!bc || bc.bi !== bi) bc = col.bc = blockPlan(col, bi, thick);
      const f05 = f - 0.5;
      // no block falls out of the top bed right under the crest (it would leave the lip floating)
      const gone = needCF ? -bc.gbase * (1 - smoothstep(bc.tf0, bc.tf1, y)) : -bc.gbase;
      const P = (c) => bc.lost[c] ? gone : bc.base[c] + (bc.S[c] + bc.M[c] * f05) * 0.8 * blockAmp;
      const pa = P(0), pb = P(1);
      const p0 = pa + (pb - pa) * bc.bX, p1 = P(2) + (P(3) - P(2)) * bc.bX;
      const offs = p0 + (p1 - p0) * bc.bZ;
      // distance to the block's rim: visible joint planes and the bed boundaries
      const e = Math.min(bc.dPlan, Math.min(f, 1 - f) * thick);
      // bevelled rim: the block face is flat, its edges round off into the crack (some blocks are
      // crisp, some well rounded)
      const rim = smoothstep(0, bc.r, e);
      const crack = (1 - smoothstep(0, crackW, e)) * (0.6 * blockAmp + 0.8 * vox);
      // soft beds weather smooth; hard beds break into blocks (massive boulders: no jointing)
      const bh = (0.35 + 0.65 * smoothstep(0.35, 0.6, hard)) * (1 - col.oc);
      block = (crack - offs * (0.25 + 0.75 * rim)) * bh * col.jf;
    }
    let lean = 0;
    if (hasLean) {
      const faceH = Math.max(4, col.crest - col.foot);
      const down = Math.min(1, Math.max(0, (col.crest - y) / faceH));
      // deepest just above the foot, eased out at the very bottom so the face meets the ground
      lean = leanAmp * down * (1 - 0.5 * smoothstep(0.85, 1, down)) * (1 - col.oc);
    }
    const d = undercut * (soft * (0.3 + 0.7 * pocket) + joint * 0.6 + fallen * 0.9 + pit * 0.5 + col.oc * 0.12 * pocket) + rough - det - push + block + lean;
    return d * col.steep * col.w;
  }
  function fCol(col, y) {
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
    const plain = y - h;
    if (col.w <= 1e-6 || steep <= 0) return plain;
    // outside the band where carving can flip the sign the field is just the plain distance
    if (plain < -maxCarve * slopeF - 2 * vox || plain > bump + 2 * vox) return plain;
    const dist = plain / slopeF;
    return plain + (dist + carve(col, y) - plain) * col.w;
  }

  // vertical range: carving d along the normal moves the surface up to d·|∇| vertically
  const reach = maxCarve * Math.min(3, job.slopeF || 3);
  let yLo = Math.floor((job.hmin - reach - 2 * vox) / vox) * vox;
  let yHi = Math.ceil((job.hmax + bump + 2 * vox) / vox) * vox;
  if (hasVertical) {
    // the sheared wall spans foot…crest in every column of the face
    let fmin = Infinity, cmax = -Infinity;
    for (let j = pad; j < Sz - pad; j++) for (let i = pad; i < Sx - pad; i++) { const q = j * Sx + i; if (m.sdfWeight[q] > 1e-6) { if (m.foot[q] < fmin) fmin = m.foot[q]; if (m.crest[q] > cmax) cmax = m.crest[q]; } }
    if (fmin < Infinity) { yLo = Math.min(yLo, Math.floor((fmin - 2 * vox) / vox) * vox); yHi = Math.max(yHi, Math.ceil((cmax + bump + 2 * vox) / vox) * vox); }
  }
  const ny = Math.max(1, Math.round((yHi - yLo) / vox));
  const sx = nx + 1, sy = ny + 1, sz = nz + 1;
  // only the band around the surface is sampled / polygonised; nodes outside it are the plain
  // distance, whose sign is known from the column height
  const vals = new Float32Array(sx * sy * sz);
  const bandLo = new Int32Array(sx * sz), bandHi = new Int32Array(sx * sz);
  for (let jz = 0; jz < sz; jz++) {
    const z = z0 + jz * vox;
    for (let jx = 0; jx < sx; jx++) {
      const col = column(x0 + jx * vox, z);
      let lo = Math.max(0, Math.floor((col.h - maxCarve * col.slopeF - 2 * vox - yLo) / vox) - 1);
      let hi = Math.min(ny, Math.ceil((col.h + bump + 2 * vox - yLo) / vox) + 1);
      if (col.vW > 0) {
        lo = Math.min(lo, Math.max(0, Math.floor((col.foot - 2 * vox - yLo) / vox) - 1));
        hi = Math.max(hi, Math.min(ny, Math.ceil((col.crest + bump + 2 * vox - yLo) / vox) + 1));
      }
      bandLo[jz * sx + jx] = lo; bandHi[jz * sx + jx] = hi;
      const base = jz * sy * sx + jx;
      for (let jy = 0; jy < lo; jy++) vals[base + jy * sx] = yLo + jy * vox - col.h;
      for (let jy = lo; jy <= hi; jy++) vals[base + jy * sx] = fCol(col, yLo + jy * vox);
      for (let jy = hi + 1; jy < sy; jy++) vals[base + jy * sx] = yLo + jy * vox - col.h;
    }
  }
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
  return { positions: pos, normals, aux, aux2, index, voxels: nx * ny * nz, triangles: indices.length / 3 };
}

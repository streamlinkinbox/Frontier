// Simulated river network (Gaea-style "Rivers"): drainage of the eroded heightfield instead of
// hand-drawn channels. Pure JS — runs in the worker and under Node.
//
//   1. priority-flood depression filling (Barnes et al. 2014) with an epsilon gradient, seeded from
//      the map border and the sea, so every cell has a downhill path to an outlet
//   2. D8 steepest-descent flow directions on the filled surface and flow accumulation
//      (uniform rainfall + injected flow at drawn-river guides)
//   3. cells whose catchment exceeds a threshold are channel cells; width and depth scale with
//      catchment area; a monotone bed and water profile is built from the outlets upstream
//   4. channels are carved with a concave bed and sloped banks (chamfer distance from the nearest
//      channel cell, nearest-source semantics), braided bars on wide, flat reaches
//   5. depressions that are deep / large enough or fed by a channel become lakes at spill level
//
// Outputs masks and water levels in the same convention as features.js (NO_WATER where dry).

import { SimplexNoise } from './noise.js';
import { NO_WATER } from './features.js';

const smoothstep = (a, b, v) => { const t = Math.min(1, Math.max(0, (v - a) / (b - a))); return t * t * (3 - 2 * t); };

// Binary min-heap on (key, index) pairs stored in parallel typed arrays.
class Heap {
  constructor(capacity) { this.k = new Float64Array(capacity); this.v = new Int32Array(capacity); this.n = 0; }
  push(key, val) {
    let i = this.n++;
    const k = this.k, v = this.v;
    while (i > 0) { const p = (i - 1) >> 1; if (k[p] <= key) break; k[i] = k[p]; v[i] = v[p]; i = p; }
    k[i] = key; v[i] = val;
  }
  pop() {
    const k = this.k, v = this.v;
    const topV = v[0];
    const lastK = k[--this.n], lastV = v[this.n];
    let i = 0;
    for (;;) {
      let c = i * 2 + 1; if (c >= this.n) break;
      if (c + 1 < this.n && k[c + 1] < k[c]) c++;
      if (k[c] >= lastK) break;
      k[i] = k[c]; v[i] = v[c]; i = c;
    }
    k[i] = lastK; v[i] = lastV;
    return topV;
  }
  get size() { return this.n; }
}

const DX = [1, -1, 0, 0, 1, 1, -1, -1];
const DZ = [0, 0, 1, -1, 1, -1, 1, -1];
const DL = [1, 1, 1, 1, Math.SQRT2, Math.SQRT2, Math.SQRT2, Math.SQRT2];

// Priority flood: returns the filled surface and the processing order (increasing filled height).
export function fillDepressions(height, N, seaLevel = -Infinity, eps = 1e-4) {
  const filled = new Float32Array(N * N);
  const closed = new Uint8Array(N * N);
  const order = new Int32Array(N * N);
  const heap = new Heap(N * N + 8);
  for (let j = 0; j < N; j++) {
    for (let i = 0; i < N; i++) {
      const idx = j * N + i;
      const border = i === 0 || j === 0 || i === N - 1 || j === N - 1;
      if (border || height[idx] <= seaLevel) {
        filled[idx] = Math.max(height[idx], seaLevel);
        closed[idx] = 1; heap.push(filled[idx], idx);
      }
    }
  }
  let n = 0;
  while (heap.size) {
    const c = heap.pop();
    order[n++] = c;
    const ci = c % N, cj = (c - ci) / N;
    for (let d = 0; d < 8; d++) {
      const ni = ci + DX[d], nj = cj + DZ[d];
      if (ni < 0 || nj < 0 || ni >= N || nj >= N) continue;
      const nidx = nj * N + ni;
      if (closed[nidx]) continue;
      closed[nidx] = 1;
      filled[nidx] = Math.max(height[nidx], filled[c] + eps * DL[d]);
      heap.push(filled[nidx], nidx);
    }
  }
  return { filled, order };
}

// Box-smooth the heights of a cell set (separable 5×5, `passes` times; neighbours outside the set are read, not written).
function smoothBasin(height, N, cells, passes = 1, floor = -Infinity) {
  const mark = new Map();
  for (const c of cells) {
    const ci = c % N, cj = (c - ci) / N;
    if (ci < 2 || cj < 2 || ci >= N - 2 || cj >= N - 2) continue;
    mark.set(c, 0);
  }
  const idxs = Array.from(mark.keys());
  for (let p = 0; p < passes; p++) {
    for (const c of idxs) {
      const ci = c % N;
      let sum = 0, n = 0;
      for (let d = -2; d <= 2; d++) { const ii = ci + d; if (ii < 0 || ii >= N) continue; sum += height[c + d]; n++; }
      mark.set(c, sum / n);
    }
    for (const c of idxs) height[c] = Math.max(floor, mark.get(c));
    for (const c of idxs) {
      const ci = c % N, cj = (c - ci) / N;
      let sum = 0, n = 0;
      for (let d = -2; d <= 2; d++) { const jj = cj + d; if (jj < 0 || jj >= N) continue; sum += height[c + d * N]; n++; }
      mark.set(c, sum / n);
    }
    for (const c of idxs) height[c] = Math.max(floor, mark.get(c));
  }
}

// D8 downstream neighbour on the filled surface (-1 at outlets).
export function flowDirections(filled, N) {
  const down = new Int32Array(N * N).fill(-1);
  for (let j = 0; j < N; j++) {
    for (let i = 0; i < N; i++) {
      const idx = j * N + i;
      let best = -1, bestDrop = 0;
      for (let d = 0; d < 8; d++) {
        const ni = i + DX[d], nj = j + DZ[d];
        if (ni < 0 || nj < 0 || ni >= N || nj >= N) continue;
        const nidx = nj * N + ni;
        const drop = (filled[idx] - filled[nidx]) / DL[d];
        if (drop > bestDrop) { bestDrop = drop; best = nidx; }
      }
      down[idx] = best;
    }
  }
  return down;
}

// opts: { catchment [km²], widthScale [m per √km²], maxWidth [m], depthScale, bankAngle [°],
//         maxBank [m], waterDepth (fraction of depth), braiding, lakes (0/1), lakeMinArea [cells],
//         seaLevel, sources: [gx, gz, ...] (drawn river guides), guideFlow [km²] }
export function simulateRivers(height, N, size, opts, seed = 1) {
  const cell = size / (N - 1);
  const cellKm2 = (cell * cell) / 1e6;
  const total = N * N;
  const original = Float32Array.from(height);

  // a little low-frequency noise on the routing surface breaks the dead-straight D8 lines that
  // smooth slopes otherwise produce (the depressions are filled again afterwards)
  const routing = Float32Array.from(height);
  const wander = opts.wander == null ? 1.5 : Math.max(0, opts.wander);
  if (wander > 0) {
    const wn = new SimplexNoise(seed * 23 + 11);
    for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) {
      const x = i * cell, z = j * cell;
      routing[j * N + i] += wander * wn.fbm(x / 90, z / 90, 3, 2.1, 0.55);
    }
  }
  const { filled } = fillDepressions(routing, N, opts.seaLevel == null ? -Infinity : opts.seaLevel);
  const down = flowDirections(filled, N);
  // inside closed depressions water runs down the real floor to the lowest point (into the lake),
  // not in dead-straight lines across the filled flat; the sink hands its flow to the pour point
  {
    const downF = Int32Array.from(down);
    const inDep = (c) => filled[c] - routing[c] > 0.3;
    for (let j = 0; j < N; j++) {
      for (let i = 0; i < N; i++) {
        const c = j * N + i;
        if (!inDep(c)) continue;
        let best = -1, bestDrop = 0;
        for (let d = 0; d < 8; d++) {
          const ni = i + DX[d], nj = j + DZ[d];
          if (ni < 0 || nj < 0 || ni >= N || nj >= N) continue;
          const n = nj * N + ni;
          const drop = (routing[c] - routing[n]) / DL[d];
          if (drop > bestDrop) { bestDrop = drop; best = n; }
        }
        if (best >= 0) { down[c] = best; continue; }
        let p = c, guard = 0;
        while (p >= 0 && inDep(p) && guard++ < total) p = downF[p];
        down[c] = p;
      }
    }
  }
  // topological order of the drainage graph (upstream first)
  const order = new Int32Array(total);
  {
    const indeg = new Int32Array(total);
    for (let c = 0; c < total; c++) if (down[c] >= 0) indeg[down[c]]++;
    let head = 0, tail = 0;
    for (let c = 0; c < total; c++) if (indeg[c] === 0) order[tail++] = c;
    while (head < tail) {
      const c = order[head++];
      const d = down[c];
      if (d >= 0 && --indeg[d] === 0) order[tail++] = d;
    }
    if (tail < total) { // should not happen (acyclic); fall back to the remaining cells in any order
      const seen = new Uint8Array(total); for (let n = 0; n < tail; n++) seen[order[n]] = 1;
      for (let c = 0; c < total; c++) if (!seen[c]) order[tail++] = c;
    }
  }

  // accumulation (in cells), highest cells first
  const acc = new Float32Array(total).fill(1);
  if (opts.sources && opts.sources.length) {
    const guide = Math.max(0, opts.guideFlow || 0) / cellKm2;
    for (let s = 0; s < opts.sources.length; s += 2) {
      const gi = Math.round(opts.sources[s]), gj = Math.round(opts.sources[s + 1]);
      if (gi < 0 || gj < 0 || gi >= N || gj >= N) continue;
      acc[gj * N + gi] += s === 0 ? guide : guide * 0.03;
    }
  }
  for (let n = 0; n < total; n++) {
    const c = order[n];
    const d = down[c];
    if (d >= 0) acc[d] += acc[c];
  }

  const threshold = Math.max(4, (opts.catchment || 0.1) / cellKm2);
  const isRiver = new Uint8Array(total);
  const width = new Float32Array(total);
  const depth = new Float32Array(total);
  const bed = new Float32Array(total);
  const wl = new Float32Array(total);
  const waterFrac = opts.waterDepth == null ? 0.6 : opts.waterDepth;
  const riverMask = new Float32Array(total);
  const bankMask = new Float32Array(total); // valley sides beside the channels (for bank gullying)
  const waterLevel = new Float32Array(total).fill(NO_WATER);
  const lakeMask = new Float32Array(total);
  const stats = { riverCells: 0, lakeCells: 0, maxWidth: 0, maxKm2: 0 };

  // ---- lakes: depressions on the filled surface ---------------------------------------------
  const lakeLevel = new Float32Array(total).fill(NO_WATER);
  if (opts.lakes) {
    const seen = new Uint8Array(total);
    const stack = [];
    const minArea = Math.max(4, opts.lakeMinArea || 24);
    const sea = opts.seaLevel == null ? -Infinity : opts.seaLevel;
    // pools are judged on the real surface (the wander noise would turn every flat valley floor
    // into a field of 1 m puddles); the sea is not a lake
    const { filled: filledReal } = fillDepressions(height, N, sea);
    const isPool = (c) => filledReal[c] - height[c] >= 0.6 && filledReal[c] > sea + 0.01;
    const candidates = [];
    for (let s = 0; s < total; s++) {
      if (seen[s] || !isPool(s)) continue;
      // flood the connected depression
      stack.length = 0; stack.push(s); seen[s] = 1;
      const cells = []; let fed = false, deepest = 0;
      while (stack.length) {
        const c = stack.pop(); cells.push(c);
        if (acc[c] >= threshold * 0.5) fed = true;
        deepest = Math.max(deepest, filledReal[c] - height[c]);
        const ci = c % N, cj = (c - ci) / N;
        for (let d = 0; d < 4; d++) {
          const ni = ci + DX[d], nj = cj + DZ[d];
          if (ni < 0 || nj < 0 || ni >= N || nj >= N) continue;
          const nidx = nj * N + ni;
          if (seen[nidx] || !isPool(nidx)) continue;
          seen[nidx] = 1; stack.push(nidx);
        }
      }
      if (deepest < 1.5 || (cells.length < minArea && !fed)) continue;
      candidates.push({ cells, deepest, fed });
    }
    // biggest / deepest basins first, up to a share of the map — a landscape full of small
    // closed hollows (domes, moraine) must not turn into a staircase of ponds
    candidates.sort((a, b) => b.cells.length * b.deepest - a.cells.length * a.deepest);
    const cap = total * (opts.lakeMaxArea == null ? 0.06 : Math.max(0, Math.min(1, opts.lakeMaxArea)));
    let used = 0;
    const fill0 = opts.lakeFill == null ? 1 : Math.max(0, Math.min(1, opts.lakeFill));
    for (const lake of candidates) {
      let fill = fill0;
      if (used + lake.cells.length > cap) {
        // over the cap: unfed hollows stay dry, basins a river drains into keep a small pond at
        // the bottom where the river ends
        if (!lake.fed) continue;
        fill = fill0 * 0.35;
      }
      // partial fill: level between the deepest point and the spill level
      let minH = Infinity, spill = -Infinity;
      for (const c of lake.cells) { if (height[c] < minH) minH = height[c]; if (filledReal[c] > spill) spill = filledReal[c]; }
      // a lake floor is sediment and its shore is worked by waves: smooth the basin before the
      // shoreline is taken, so the outline is a clean curve and not the hummocky cell-by-cell
      // contour of the droplet deposits (the rim is left alone so the spill level holds)
      smoothBasin(height, N, lake.cells, 2);
      minH = Infinity; for (const c of lake.cells) if (height[c] < minH) minH = height[c];
      const level = minH + (spill - minH) * fill - 0.05;
      let n = 0; for (const c of lake.cells) if (height[c] < level) n++;
      used += n;
      // morphological opening of the water area: one- and two-cell inlets (rill notches running
      // into the lake) are not part of the lake outline
      const below = new Set();
      for (const c of lake.cells) if (height[c] < level) below.add(c);
      const core = [];
      for (const c of below) {
        const ci = c % N, cj = (c - ci) / N;
        if (ci < 1 || cj < 1 || ci >= N - 1 || cj >= N - 1) continue;
        let full = true;
        for (let dj = -1; dj <= 1 && full; dj++) for (let di = -1; di <= 1; di++) if (!below.has(c + dj * N + di)) { full = false; break; }
        if (full) core.push(c);
      }
      const keep = new Set();
      for (const c of core) for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) keep.add(c + dj * N + di);
      const inLake = [];
      for (const c of keep) {
        lakeLevel[c] = level;
        lakeMask[c] = 1;
        stats.lakeCells++;
        inLake.push(c);
      }
      // wave-worked shore: the rim just outside the water line is smoothed too (never below the
      // water, so the basin cannot spring a leak) — rill notches and single-cell spikes go
      if (inLake.length) {
        const rim = new Set();
        for (const c of inLake) {
          const ci = c % N, cj = (c - ci) / N;
          for (let dj = -2; dj <= 2; dj++) for (let di = -2; di <= 2; di++) {
            const ii = ci + di, jj = cj + dj;
            if (ii < 2 || jj < 2 || ii >= N - 2 || jj >= N - 2) continue;
            const n = jj * N + ii;
            if (lakeLevel[n] <= NO_WATER * 0.5) rim.add(n);
          }
        }
        smoothBasin(height, N, Array.from(rim), 1, level + 0.05);
      }
    }
  }

  const maxWidth = Math.max(cell * 2, opts.maxWidth || 120);
  const steep = new Float32Array(total), dry = new Uint8Array(total);
  const dryGrade = Math.tan(((opts.drySlope == null ? 12 : opts.drySlope) * Math.PI) / 180);
  const dryBig = opts.dryBig == null ? 1.5 : opts.dryBig; // [km²] rivers this big keep water on any grade
  let riverCells = 0;
  // outlets first so the downstream bed is known
  for (let n = total - 1; n >= 0; n--) {
    const c = order[n];
    if (acc[c] < threshold) continue;
    isRiver[c] = 1; riverCells++;
    const km2 = acc[c] * cellKm2;
    // steep reaches run narrow and shallow (torrents), gentle reaches spread out
    let dn = c, drop = 0, run = 0;
    for (let k = 0; k < 6 && down[dn] >= 0; k++) { const nx = down[dn]; drop += filled[dn] - filled[nx]; run += cell * ((nx - dn) % N === 0 || Math.abs(nx - dn) === 1 ? 1 : 1.414); dn = nx; }
    const grade = run > 0 ? drop / run : 0;
    const torrent = 1 / (1 + 6 * grade);
    let w = Math.min(maxWidth, Math.max(cell * 1.5, (opts.widthScale || 25) * Math.sqrt(km2) * torrent));
    let dep = Math.max(0.4, (opts.depthScale || 1) * Math.pow(w, 0.45) * (0.5 + 0.5 * torrent));
    // on steep ground the channel is a gully: a wide shallow V, not a slot
    const steepness = smoothstep(0.08, 0.35, grade);
    w *= 1 + 1.6 * steepness;
    dep *= 1 - 0.35 * steepness;
    const d = down[c];
    if (d >= 0 && isRiver[d] && acc[c] >= 0.5 * acc[d]) {
      // same stream: width / depth may only change gradually along it (a jump in depth made the
      // bed a staircase of flat treads)
      w = Math.max(width[d] * 0.92, Math.min(width[d] * 1.08, w));
      dep = Math.max(depth[d] * 0.92, Math.min(depth[d] * 1.08, dep));
    }
    width[c] = w; depth[c] = dep;
    steep[c] = steepness;
    // water only stands on gentle reaches (or in big rivers); steep gullies show the carved bed
    dry[c] = grade > dryGrade && km2 < dryBig ? 1 : 0;
    let b = filled[c] - dep;
    let l = b + dep * waterFrac;
    if (filled[c] - routing[c] > 0.3) {
      // inside a closed depression: the channel follows the real floor down to the lake (or across
      // the dry hollow) instead of hovering at the spill level
      b = Math.min(height[c], filled[c]) - dep * 0.5;
      l = lakeLevel[c] > NO_WATER * 0.5 ? Math.max(lakeLevel[c], b + dep * waterFrac) : b + dep * waterFrac;
    } else if (d >= 0 && isRiver[d]) {
      // the bed follows the surface (continuous); only the water is forced monotone
      l = Math.min(filled[c] - dep * 0.2, Math.max(b + dep * waterFrac, wl[d]));
      b = Math.min(b, l - dep * 0.25);
    }
    bed[c] = b; wl[c] = l;
  }

  // water that is flowing does not vanish on a steep reach and reappear below it: once a stream
  // is wet it stays wet downstream (dry = only the ephemeral headwater gullies)
  for (let n = 0; n < total; n++) {
    const c = order[n];
    if (!isRiver[c] || dry[c]) continue;
    const d = down[c];
    if (d >= 0 && isRiver[d]) dry[d] = 0;
  }

  for (let c = 0; c < total; c++) if (isRiver[c]) { stats.maxWidth = Math.max(stats.maxWidth, width[c]); stats.maxKm2 = Math.max(stats.maxKm2, acc[c] * cellKm2); }
  stats.riverCells = riverCells;

  // ---- carve channels: distance to the smoothed centreline --------------------------------------
  // The D8 path is a chain of cell centres (staircase at bends); the channel is cut around a
  // centreline smoothed along the stream (main donor ← cell → receivers), rasterised segment by
  // segment, so the banks are smooth curves and a meander is a meander, not a zigzag.
  const sea = opts.seaLevel == null ? -Infinity : opts.seaLevel;
  const bankSlope = Math.tan((Math.max(10, Math.min(80, opts.bankAngle || 35)) * Math.PI) / 180);
  const maxBank = Math.max(1, opts.maxBank == null ? 30 : opts.maxBank);
  const floodplain = Math.max(0, opts.floodplain || 0);
  const dist = new Float32Array(total).fill(Infinity);
  const src = new Int32Array(total).fill(-1);
  const segT = new Float32Array(total);
  const mainDonor = new Int32Array(total).fill(-1);
  for (let c = 0; c < total; c++) {
    if (!isRiver[c]) continue;
    const d = down[c];
    if (d >= 0 && isRiver[d] && (mainDonor[d] < 0 || acc[c] > acc[mainDonor[d]])) mainDonor[d] = c;
  }
  const px = new Float32Array(total), pz = new Float32Array(total);
  const SW = [0.08, 0.16, 0.26, 0.26, 0.16, 0.08]; // weights for −3..+3 (centre weight added below)
  for (let c = 0; c < total; c++) {
    if (!isRiver[c]) continue;
    let sx = (c % N) * 0.3, sz = ((c - (c % N)) / N) * 0.3, sw = 0.3;
    let u = c, dn = c;
    for (let k = 0; k < 3; k++) {
      const w = SW[k];
      if (u >= 0) { u = mainDonor[u]; if (u >= 0) { sx += (u % N) * w; sz += ((u - (u % N)) / N) * w; sw += w; } }
      // the walk stops where the stream joins a much bigger one, so a tributary's centreline
      // bends into the trunk instead of being dragged along it
      if (dn >= 0) { dn = down[dn]; if (dn >= 0 && isRiver[dn] && acc[dn] < acc[c] * 2.5) { sx += (dn % N) * w; sz += ((dn - (dn % N)) / N) * w; sw += w; } else dn = -1; }
    }
    px[c] = sx / sw; pz[c] = sz / sw;
  }
  // the nearest *wet* segment is tracked separately, so a dry gully joining a river can never
  // punch a hole in the water next to the junction
  const distWet = new Float32Array(total).fill(Infinity);
  const srcWet = new Int32Array(total).fill(-1);
  const segTWet = new Float32Array(total);
  const touched = [];
  const seen = new Uint8Array(total);
  for (let s0 = 0; s0 < total; s0++) {
    if (!isRiver[s0]) continue;
    const d0 = down[s0];
    const hasSeg = d0 >= 0 && isRiver[d0];
    const ax = px[s0], az = pz[s0], bx = hasSeg ? px[d0] : ax, bz = hasSeg ? pz[d0] : az;
    const reach = (width[s0] * 0.5 + width[s0] * 1.6 + maxBank / bankSlope + (floodplain > 0 ? width[s0] * 4 * floodplain : 0)) / cell + 1;
    const i0 = Math.max(0, Math.floor(Math.min(ax, bx) - reach)), i1 = Math.min(N - 1, Math.ceil(Math.max(ax, bx) + reach));
    const j0 = Math.max(0, Math.floor(Math.min(az, bz) - reach)), j1 = Math.min(N - 1, Math.ceil(Math.max(az, bz) + reach));
    const vx = bx - ax, vz = bz - az, vv = vx * vx + vz * vz;
    for (let j = j0; j <= j1; j++) {
      for (let i = i0; i <= i1; i++) {
        let t = vv > 1e-9 ? ((i - ax) * vx + (j - az) * vz) / vv : 0;
        t = Math.max(0, Math.min(1, t));
        const qx = ax + vx * t - i, qz = az + vz * t - j;
        const dd = Math.sqrt(qx * qx + qz * qz);
        if (dd > reach) continue;
        const c = j * N + i;
        // near-ties go to the bigger stream, so a tributary never owns the trunk's channel cells
        const cur = src[c];
        if (dd < dist[c] - 0.6 || (dd < dist[c] + 0.6 && (cur < 0 || acc[s0] > acc[cur]))) {
          dist[c] = dd; src[c] = s0; segT[c] = t;
          if (!seen[c]) { seen[c] = 1; touched.push(c); }
        }
        if (!dry[s0] && dd < distWet[c]) { distWet[c] = dd; srcWet[c] = s0; segTWet[c] = t; }
      }
    }
  }

  const braidNoise = new SimplexNoise(seed * 19 + 3);
  const braiding = opts.braiding || 0;
  for (const c of touched) {
    if (height[c] <= sea) continue; // the sea bed is not a river bank
    const s = src[c];
    const d = dist[c] * cell;
    const w = width[s], dep = depth[s];
    let half = w * 0.5;
    const ci = c % N, cj = (c - ci) / N;
    const x = (ci / (N - 1) - 0.5) * size, z = (cj / (N - 1) - 0.5) * size;
    // braided bars on wide, gentle reaches — and on alluvium (fans, floodplains, silted basins),
    // where a loaded river spreads into shifting channels and bars
    let bar = 0;
    const allu = opts.alluvium ? opts.alluvium[s] : 0;
    if (braiding > 0 && w > cell * 4) {
      const dn = down[s];
      const grad = dn >= 0 ? (bed[s] - bed[dn]) / (cell * 1.2) : 0;
      const gentle = 1 - smoothstep(0.003, 0.012, grad);
      const wide = smoothstep(cell * 6, cell * 14, w);
      const br = braiding * Math.max(gentle * wide, allu * (1 - smoothstep(0.01, 0.03, grad)) * smoothstep(cell * 4, cell * 9, w));
      if (br > 0) {
        half *= 1 + br * 0.5;
        const n = braidNoise.fbm(x / (w * 0.9), z / (w * 0.9), 3, 2.1, 0.55);
        const n2 = braidNoise.fbm(x / (w * 0.3) + 7, z / (w * 0.3) - 4, 2, 2, 0.5);
        bar = br * smoothstep(-0.05, 0.35, n + n2 * 0.35);
      }
    }
    // bed / water of the nearest channel cell, interpolated along the channel direction so the
    // profile is a continuous ramp instead of one flat tread per channel cell
    let bedHere = bed[s], waterHere = wl[s];
    const ds = down[s];
    if (ds >= 0 && isRiver[ds]) {
      const t = segT[c];
      bedHere += (bed[ds] - bed[s]) * t;
      waterHere += (wl[ds] - wl[s]) * t;
    }
    let target;
    if (d <= half) {
      const t = d / half;
      target = bedHere + dep * 0.35 * t * t;
      // bars rise to just above the water so they read as gravel islands
      if (bar > 0) target = Math.max(target, target + (waterHere + 0.35 - target) * bar);
    } else {
      const rise = (d - half) * bankSlope;
      target = height[c];
      if (rise <= maxBank) {
        target = bedHere + dep * 0.35 + rise;
        // the bank cut fades out towards its limit instead of leaving a wall
        const fade = smoothstep(maxBank * 0.45, maxBank, rise);
        if (target < height[c]) target += (height[c] - target) * fade;
      }
      // floodplain: a river of any size has levelled a valley floor beside it — ground within a
      // few channel widths that is not much higher than the water is planed down to a gently
      // rising flat (Gaea "river valley"); mountainsides above it are left alone
      if (floodplain > 0 && w > cell * 2.5) {
        const outer = half + w * 4 * floodplain;
        const plain = waterHere + 0.8 + (d - half) * 0.012;
        if (d < outer && height[c] > plain && height[c] - plain < maxBank * 2.5) {
          const f = (1 - smoothstep(outer * 0.55, outer, d)) * smoothstep(cell * 2.5, cell * 7, w) * (1 - smoothstep(maxBank * 1.5, maxBank * 2.5, height[c] - plain));
          target = Math.min(target, height[c] + (plain - height[c]) * f);
        }
      }
      if (target >= height[c]) {
        // nothing to cut here; the mask / water bookkeeping below still applies near the channel
        if (d > half + w * 0.9) continue;
      }
    }
    // lakes keep their floor
    if (lakeLevel[c] > NO_WATER * 0.5 && target < height[c]) target = Math.max(target, height[c] - dep * 0.3);
    if (target < height[c]) height[c] = target;
    else if (d <= half + w * 0.6 && filled[c] - routing[c] <= 0.3 && height[c] > sea && target - height[c] < dep * 1.5) {
      // the channel also has a floor on its downhill side (a bench on cross-slopes) — otherwise
      // the water would hang in the air over the lower ground; never built out over the sea,
      // a lake / hollow, or ground that is much lower (a drop-off)
      const fillW = (d <= half ? 1 : 1 - (d - half) / (w * 0.6)) * (1 - smoothstep(dep * 0.8, dep * 1.5, target - height[c]));
      height[c] += (target - height[c]) * fillW;
    }
    const m = 1 - smoothstep(half, half + w * 0.9, d);
    if (m > riverMask[c]) riverMask[c] = m;
    {
      const R = half + w * 1.6 + (maxBank / bankSlope) * 0.8;
      const b = smoothstep(half * 0.9, half + w * 0.5, d) * (1 - smoothstep(R * 0.5, R, d)) * (0.4 + 0.6 * smoothstep(cell * 2, cell * 6, w));
      if (b > bankMask[c]) bankMask[c] = b;
    }
    const sw = srcWet[c];
    if (sw >= 0 && distWet[c] * cell <= width[sw] * 0.5 * (half / (w * 0.5)) + cell) {
      let wh = wl[sw];
      const dw = down[sw];
      if (dw >= 0 && isRiver[dw]) wh += (wl[dw] - wl[sw]) * segTWet[c];
      if (wh > waterLevel[c]) waterLevel[c] = wh;
    }
  }

  // smooth the carved channel floor (residual treads where the chamfer stripes meet at bends)
  {
    const tmp = Float32Array.from(height);
    for (const c of touched) {
      if (riverMask[c] < 0.2 || height[c] <= sea) continue;
      const ci = c % N, cj = (c - ci) / N;
      if (ci < 1 || cj < 1 || ci >= N - 1 || cj >= N - 1) continue;
      let sum = 0, n = 0;
      for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) { const k = (cj + dj) * N + ci + di; if (riverMask[k] >= 0.2) { sum += height[k]; n++; } }
      if (n) tmp[c] = height[c] + (sum / n - height[c]) * Math.min(1, riverMask[c] * 1.5);
    }
    height.set(tmp);
  }

  // the water level was copied from the nearest channel cell, which on steep reaches gives a
  // staircase of plates; relax it so the sheet becomes one continuous sloped surface
  {
    const tmp = new Float32Array(total);
    for (let pass = 0; pass < 3; pass++) {
      tmp.set(waterLevel);
      for (const c of touched) {
        if (waterLevel[c] <= NO_WATER * 0.5) continue;
        const ci = c % N, cj = (c - ci) / N;
        let sum = 0, n = 0;
        for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) {
          const ii = ci + di, jj = cj + dj;
          if (ii < 0 || jj < 0 || ii >= N || jj >= N) continue;
          const v = waterLevel[jj * N + ii];
          if (v > NO_WATER * 0.5) { sum += v; n++; }
        }
        if (n) tmp[c] = sum / n;
      }
      waterLevel.set(tmp);
    }
  }

  // lakes: water at spill level over the depression (and a shore band in the mask)
  if (opts.lakes) {
    // shelving shore: ground just above the water line is eased down into a shallow beach
    const shoreW = Math.max(2, Math.round(12 / cell));
    const shoreLvl = new Float32Array(total).fill(NO_WATER);
    const shoreD = new Float32Array(total).fill(Infinity);
    const q = [];
    for (let c = 0; c < total; c++) if (lakeLevel[c] > NO_WATER * 0.5) { shoreD[c] = 0; shoreLvl[c] = lakeLevel[c]; q.push(c); }
    for (let h = 0; h < q.length; h++) {
      const c = q[h];
      if (shoreD[c] >= shoreW) continue;
      const ci = c % N, cj = (c - ci) / N;
      for (let d = 0; d < 4; d++) {
        const ni = ci + DX[d], nj = cj + DZ[d];
        if (ni < 0 || nj < 0 || ni >= N || nj >= N) continue;
        const n = nj * N + ni;
        if (shoreD[n] < Infinity) continue;
        shoreD[n] = shoreD[c] + 1; shoreLvl[n] = shoreLvl[c]; q.push(n);
      }
    }
    for (let c = 0; c < total; c++) {
      if (!(shoreD[c] > 0 && shoreD[c] < Infinity)) continue;
      const lvl = shoreLvl[c], rise = height[c] - lvl;
      if (rise <= 0 || rise > 6) continue;
      const t = shoreD[c] / shoreW; // 0 at the water line → 1 at the back of the beach
      const beach = lvl - 0.25 + rise * t * t;
      if (beach < height[c]) { height[c] = beach; lakeMask[c] = Math.max(lakeMask[c], 0.6 * (1 - t)); }
    }
    for (let c = 0; c < total; c++) {
      if (lakeLevel[c] <= NO_WATER * 0.5) continue;
      if (lakeLevel[c] > waterLevel[c]) waterLevel[c] = lakeLevel[c];
      // shallow silt bed just under the surface so the shore is not a knife edge
      const ci = c % N, cj = (c - ci) / N;
      for (let dj = -2; dj <= 2; dj++) for (let di = -2; di <= 2; di++) {
        const ii = ci + di, jj = cj + dj;
        if (ii < 0 || jj < 0 || ii >= N || jj >= N) continue;
        const n = jj * N + ii;
        if (lakeMask[n] === 0) lakeMask[n] = Math.max(lakeMask[n], 0.5 - Math.hypot(di, dj) * 0.12);
      }
    }
  }

  // log-scaled accumulation for shading (wet streaks, gully floors)
  let maxAcc = 1;
  for (let c = 0; c < total; c++) if (acc[c] > maxAcc) maxAcc = acc[c];
  const flow = new Float32Array(total);
  const lm = Math.log1p(maxAcc);
  for (let c = 0; c < total; c++) flow[c] = Math.log1p(acc[c]) / lm;

  let cut = 0;
  for (let c = 0; c < total; c++) cut += original[c] - height[c];
  stats.cutVolume = cut * cell * cell;
  return { riverMask, bankMask, waterLevel, lakeMask, flow, acc, filled, down, stats, debug: { bed, wl, width, depth, isRiver, dist, src } };
}

// Fluvial incision — the stream-power model that gives real terrain its dendritic valleys.
//
//   ∂h/∂t = U − K·A^m·S^n + D·∇²h   (+ deposition where the sediment flux exceeds capacity)
//
// Rivers cut down at a rate set by their drainage area A (how much water they carry) and their
// slope S, while the hillslopes diffuse towards them. Running it on the layered base relief
// re-shapes the whole surface into a self-consistent drainage network: branching valleys that
// deepen downstream, concave long profiles, ridges left between them. The simulated rivers
// later follow these valleys instead of being carved into an unrelated surface.
//
// Solver: Braun & Willett (2013) implicit scheme with n = 1. Cells are processed from the
// outlets upwards (increasing filled height = receiver before donor), so for each cell the
// receiver's new height is already known and the update is a closed form:
//
//   h_i' = (h_i + f·h_j') / (1 + f),   f = dt·K·A_i^m / dx_ij
//
// which is unconditionally stable — a few dozen large steps are enough. Depressions are
// filled (priority flood) every step so closed basins drain through their spill points.
// Hardness lowers K (resistant beds hold up knickpoints and benches), optional uplift keeps the
// relief from being planed flat, and a light diffusion rounds the interfluves.

import { fillDepressions, flowDirections } from './hydrology.js';
import { SimplexNoise } from './noise.js';

// opts: { strength 0..1, iterations, concavity (m), uplift 0..1 (fraction of relief re-added
//         over the run), diffusion 0..1, seaLevel, relief [m] }
export function fluvialErosion(height, hardness, N, size, opts, progress = () => {}) {
  const total = N * N;
  const cell = size / (N - 1);
  const iterations = Math.max(1, Math.round(opts.iterations || 30));
  const m = opts.concavity == null ? 0.5 : opts.concavity;
  const sea = opts.seaLevel == null ? -Infinity : opts.seaLevel;
  const strength = opts.strength == null ? 0.5 : opts.strength;
  if (strength <= 0) return null;

  // Erodibility: f = 1 at a 1 km² stream over one cell for strength 1 — a river that size moves
  // halfway to its receiver per step, hill-top rills (A ≈ one cell) barely move.
  const kBase = strength * Math.pow(1e6, -m) * cell;

  // Uplift pattern follows the initial relief (massifs rise, lowlands do not) so the mountains
  // keep their height while the valleys cut into them.
  let lo = Infinity, hi = -Infinity;
  for (let i = 0; i < total; i++) { const v = height[i]; if (v < lo) lo = v; if (v > hi) hi = v; }
  const relief = Math.max(1, hi - lo);
  const upliftTotal = (opts.uplift || 0) * relief * 0.12;
  const upliftMask = new Float32Array(total);
  for (let i = 0; i < total; i++) {
    const t = (height[i] - lo) / relief;
    upliftMask[i] = t * (upliftTotal / iterations);
  }
  // closed basins trap everything their rivers carry: their floors aggrade towards the spill
  // level (lake-floor sediment), so over the run most of them become flat valley floors drained
  // by a river, and only the youngest/deepest survive as lakes
  const fillRate = Math.min(1, (opts.basinFill == null ? 0.6 : opts.basinFill) * 0.12);

  // aggraded floors are not dead flat: the fill surface grades towards the spill point
  const floorGrade = 0.004 * cell;
  const diffusion = (opts.diffusion || 0) * 0.12; // explicit Laplacian weight per step (stable < 0.25)
  // sediment: what the rivers cut is carried downstream and dropped where the stream can no
  // longer carry it — transport capacity ∝ A^m·S, so fans form where slopes flatten, valley
  // floors and basins aggrade, deltas build where a channel enters a basin
  const deposition = Math.max(0, Math.min(1, opts.deposition == null ? 0.6 : opts.deposition));
  const capacityK = kBase * cell * cell * 40 * (opts.capacity == null ? 1 : opts.capacity); // m³ per step at A^m·S = 1
  const qs = new Float32Array(total);
  const eroded = new Float32Array(total);
  const sediment = new Float32Array(total); // total thickness deposited [m]
  const acc = new Float32Array(total);
  const tmp = new Float32Array(total);
  const seaMask = new Uint8Array(total);
  const incision = new Float32Array(total); // total metres cut (for the flow/sediment maps)
  const area = cell * cell;

  for (let it = 0; it < iterations; it++) {
    // 1. routing on the filled surface
    const { filled, order } = fillDepressions(height, N, sea, floorGrade);
    const down = flowDirections(filled, N);

    // 2. drainage area: donors are processed after receivers in `order`, so sum in reverse
    acc.fill(1);
    for (let k = total - 1; k >= 0; k--) {
      const c = order[k], d = down[c];
      if (d >= 0) acc[d] += acc[c];
    }

    // 3. uplift
    for (let i = 0; i < total; i++) if (height[i] > sea) height[i] += upliftMask[i];

    // 4. implicit incision, receiver before donor
    eroded.fill(0);
    for (let k = 0; k < total; k++) {
      const c = order[k];
      const d = down[c];
      if (d < 0) continue;
      const ci = c % N, di = d % N;
      const diag = (ci !== di) && (((c - ci) / N) !== ((d - di) / N));
      const dx = diag ? cell * Math.SQRT2 : cell;
      const a = acc[c] * area;
      const soft = 1.25 - 0.9 * hardness[c];          // hard beds: ~0.35 ×, soft: ~1.25 ×
      const f = kBase * soft * Math.pow(a, m) / dx;
      const h0 = height[c];
      const hr = height[d];
      if (h0 <= hr || filled[c] > h0 + 0.01) {
        // inside a filled depression: sediment settles, the floor rises towards the spill level
        if (fillRate > 0) height[c] = h0 + (filled[c] - h0) * fillRate;
        continue;
      }
      const h1 = (h0 + f * hr) / (1 + f);
      height[c] = h1;
      incision[c] += h0 - h1;
      eroded[c] = h0 - h1;
    }

    // 5. sediment transport and deposition, donors before receivers (reverse order)
    if (deposition > 0) {
      qs.fill(0);
      for (let k = total - 1; k >= 0; k--) {
        const c = order[k], d = down[c];
        let q = qs[c] + eroded[c] * area;
        if (d < 0 || q <= 0) continue;
        const ci = c % N, di = d % N;
        const diag = (ci !== di) && (((c - ci) / N) !== ((d - di) / N));
        const dx = diag ? cell * Math.SQRT2 : cell;
        const inBasin = filled[c] > height[c] + 0.01;
        const slopeC = inBasin ? 0 : Math.max(0, (height[c] - height[d]) / dx);
        const cap = capacityK * Math.pow(acc[c] * area, m) * slopeC;
        if (q > cap) {
          let dep = (q - cap) * deposition * (inBasin ? 1 : 0.5);
          // never bury the cell above its receiver's level + the local drop it needs to drain
          // (outside basins) nor above the water surface (inside)
          const roof = inBasin ? filled[c] - 0.02 : height[d] + Math.max(0.05, slopeC * dx * 0.6) + 0.5;
          const room = Math.max(0, roof - height[c]) * area;
          if (dep > room) dep = room;
          height[c] += dep / area;
          sediment[c] += dep / area;
          q -= dep;
        }
        qs[d] += q;
      }
    }

    // 6. hillslope diffusion (keeps interfluves rounded, prevents needle ridges)
    if (diffusion > 0) {
      for (let j = 1; j < N - 1; j++) {
        for (let i = 1; i < N - 1; i++) {
          const idx = j * N + i;
          const h = height[idx];
          const lap = height[idx - 1] + height[idx + 1] + height[idx - N] + height[idx + N] - 4 * h;
          // diffuse more where the ground is soft; resistant caprock keeps its edges
          tmp[idx] = h + lap * diffusion * (0.4 + 0.6 * (1 - hardness[idx]));
        }
      }
      for (let j = 1; j < N - 1; j++) for (let i = 1; i < N - 1; i++) { const idx = j * N + i; height[idx] = tmp[idx]; }
    }

    progress((it + 1) / iterations);
  }

  // final drainage area for the caller (flow map seed)
  const { filled, order } = fillDepressions(height, N, sea);
  const down = flowDirections(filled, N);
  acc.fill(1);
  for (let k = total - 1; k >= 0; k--) { const c = order[k], d = down[c]; if (d >= 0) acc[d] += acc[c]; }
  for (let i = 0; i < total; i++) seaMask[i] = height[i] <= sea ? 1 : 0;
  return { acc, incision, sediment, seaMask };
}

// Droplet erosion leaves the valley floors hummocky: sediment fans dam the channels into chains of
// shallow pits that would otherwise all become ponds. Pits are silted up to just below their spill
// level (so the river still finds its way through them); basins that are large and deep enough to
// be lakes are left alone.
export function fillShallowPits(height, N, seaLevel, maxDepth = 8, residual = 0.2, cell = 4, keepArea = 6400) {
  const total = N * N;
  const keepCells = Math.max(16, Math.round(keepArea / (cell * cell))); // basins at least this big (m²) and ≥ 1.5 m deep are lakes
  const { filled } = fillDepressions(height, N, seaLevel, 0.004 * cell);
  const seen = new Uint8Array(total);
  const stack = [];
  const cells = [];
  let filledCells = 0;
  for (let s = 0; s < total; s++) {
    if (seen[s] || filled[s] - height[s] <= 0.01) continue;
    stack.length = 0; cells.length = 0; stack.push(s); seen[s] = 1;
    let deepest = 0;
    while (stack.length) {
      const c = stack.pop(); cells.push(c);
      const d = filled[c] - height[c]; if (d > deepest) deepest = d;
      const ci = c % N, cj = (c - ci) / N;
      if (ci > 0 && !seen[c - 1] && filled[c - 1] - height[c - 1] > 0.01) { seen[c - 1] = 1; stack.push(c - 1); }
      if (ci < N - 1 && !seen[c + 1] && filled[c + 1] - height[c + 1] > 0.01) { seen[c + 1] = 1; stack.push(c + 1); }
      if (cj > 0 && !seen[c - N] && filled[c - N] - height[c - N] > 0.01) { seen[c - N] = 1; stack.push(c - N); }
      if (cj < N - 1 && !seen[c + N] && filled[c + N] - height[c + N] > 0.01) { seen[c + N] = 1; stack.push(c + N); }
    }
    // a real basin (big and deep enough for a lake) is kept; droplet pits — small however deep,
    // or shallow however big — are silted up
    if (cells.length >= keepCells && deepest >= 1.5) continue;
    if (deepest >= maxDepth && cells.length >= keepCells * 0.25) continue;
    for (const c of cells) { const t = filled[c] - residual; if (t > height[c]) { height[c] = t; filledCells++; } }
  }
  return filledCells;
}

// Rills / flow lines — the fine converging runoff channels that cover eroded slopes (the "flow"
// texture of Gaea's Erosion node). Same stream-power solver with a low area exponent, so even
// tiny catchments cut, no uplift / diffusion / basin fill, and a little noise on the routing
// surface so the lines wander instead of running dead straight along the 8 grid directions.
// Runs after the droplet erosion so the lines are not blurred away; returns the drainage area
// for the flow map (the wet lines).
export function rillErosion(height, hardness, N, size, opts, progress = () => {}) {
  const total = N * N;
  const cell = size / (N - 1);
  const iterations = Math.max(1, Math.round(opts.iterations || 6));
  const strength = opts.strength == null ? 0.5 : opts.strength;
  const sea = opts.seaLevel == null ? -Infinity : opts.seaLevel;
  const m = 0.3;
  const kBase = strength * Math.pow(1e4, -m) * cell; // f ≈ 0.36·strength for a 20-cell rill
  const area = cell * cell;
  const acc = new Float32Array(total);
  const routing = new Float32Array(total);
  const wn = new SimplexNoise((opts.seed || 1) * 37 + 19);
  const wander = new Float32Array(total);
  for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) wander[j * N + i] = 0.8 * wn.fbm(i * cell / 45, j * cell / 45, 2, 2.1, 0.5);
  let order = null, down = null;
  for (let it = 0; it <= iterations; it++) {
    for (let i = 0; i < total; i++) routing[i] = height[i] + wander[i];
    const fd = fillDepressions(routing, N, sea);
    order = fd.order; down = flowDirections(fd.filled, N);
    acc.fill(1);
    for (let k = total - 1; k >= 0; k--) { const c = order[k], d = down[c]; if (d >= 0) acc[d] += acc[c]; }
    if (it === iterations) break; // last pass only refreshes the drainage area
    for (let k = 0; k < total; k++) {
      const c = order[k], d = down[c];
      if (d < 0) continue;
      const h0 = height[c], hr = height[d];
      if (h0 <= hr + 0.02 || h0 <= sea) continue;
      const ci = c % N, di = d % N;
      const diag = (ci !== di) && (((c - ci) / N) !== ((d - di) / N));
      const dx = diag ? cell * Math.SQRT2 : cell;
      const soft = 1.25 - 0.9 * hardness[c];
      const f = kBase * soft * Math.pow(acc[c] * area, m) / dx;
      height[c] = (h0 + f * hr) / (1 + f);
    }
    progress((it + 1) / iterations);
  }
  return { acc };
}

// Bank gullying: soil erosion on the valley sides beside the simulated rivers. Runoff off the
// slopes converges on the channel and cuts small gullies into the soft bank material (rills at a
// finer scale and a stronger rate than the hillslope rills), masked to the banks and never below
// the river's water line, so the channel itself is left as the river simulation shaped it.
export function bankErosion(height, hardness, N, size, opts, progress = () => {}) {
  const total = N * N;
  const cell = size / (N - 1);
  const iterations = Math.max(1, Math.round(opts.iterations || 5));
  const strength = opts.strength == null ? 0.6 : opts.strength;
  const sea = opts.seaLevel == null ? -Infinity : opts.seaLevel;
  const mask = opts.mask, floorLvl = opts.floor;
  const m = 0.4;
  const kBase = strength * Math.pow(2e3, -m) * cell * 1.6;
  const area = cell * cell;
  const acc = new Float32Array(total);
  const routing = new Float32Array(total);
  const wn = new SimplexNoise((opts.seed || 1) * 53 + 7);
  const wander = new Float32Array(total);
  for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) wander[j * N + i] = 0.5 * wn.fbm(i * cell / 18, j * cell / 18, 2, 2.1, 0.5);
  for (let it = 0; it < iterations; it++) {
    for (let i = 0; i < total; i++) routing[i] = height[i] + wander[i] * mask[i];
    const fd = fillDepressions(routing, N, sea);
    const order = fd.order, down = flowDirections(fd.filled, N);
    acc.fill(1);
    for (let k = total - 1; k >= 0; k--) { const c = order[k], d = down[c]; if (d >= 0) acc[d] += acc[c]; }
    for (let k = 0; k < total; k++) {
      const c = order[k];
      if (mask[c] <= 0.01) continue;
      const d = down[c];
      if (d < 0) continue;
      const h0 = height[c], hr = height[d];
      if (h0 <= hr + 0.02 || h0 <= sea) continue;
      const ci = c % N, di = d % N;
      const diag = (ci !== di) && (((c - ci) / N) !== ((d - di) / N));
      const dx = diag ? cell * Math.SQRT2 : cell;
      const soft = 1.3 - 1.0 * hardness[c];
      const f = kBase * soft * mask[c] * Math.pow(acc[c] * area, m) / dx;
      let h = (h0 + f * hr) / (1 + f);
      if (floorLvl && floorLvl[c] > -1e5) h = Math.max(h, Math.min(h0, floorLvl[c] + 0.3));
      height[c] = h;
    }
    progress((it + 1) / iterations);
  }
}

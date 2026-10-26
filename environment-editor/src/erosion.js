// Erosion processes, ported from the CliffGenerator's heightfield versions
// (erosion.js / fluvial.js / hydrology.js there) to act on the SDF volume:
//
//  heightfield original                here on the SDF
//  ----------------------------------  ------------------------------------------------
//  droplet erosion (Beyer 2015)        droplets walk the iso-surface itself: project to
//                                      sdf=0, move along the tangential component of
//                                      gravity, erode/deposit with sphere stamps. Works
//                                      on cliff faces, under cornices, on cave floors.
//  thermal / talus slumping            cellular transfer of *loose* material only
//                                      (sediment field) to supported air neighbours
//                                      below — intact hard rock stands, scree slumps.
//  stream-power fluvial (Braun &
//  Willett drainage)                   drainage graph on the extracted top surface,
//                                      incision K·A^m·S^n / hardness applied back into
//                                      the volume as channel stamps; also produces the
//                                      flow / moisture maps the water & vegetation
//                                      phases consume.
//
//  ...plus the two processes that actually make sedimentary cliffs, which a
//  heightfield cannot express at all:
//  differential weathering — a level-set front velocity ∝ (1-hardness)^p ·
//  joint factor · face exposure: soft beds recede into undercuts, hard beds
//  are left standing as cornices. No noise is involved; the relief is the
//  geology, differentially removed.
//  block fall — joint-bounded hard blocks over an undercut detach, land below
//  as loose blocks (keeping their source bed), feeding the talus.

import { clamp, lerp, smoothstep, mulberry32 } from './noise.js';

const zero3 = () => [0, 0, 0];

function markDirtyNode(volume, ix, iy, iz) {
  const cx = clamp(Math.floor(ix / volume.chunk), 0, volume.chunksX - 1);
  const cy = clamp(Math.floor(iy / volume.chunk), 0, volume.chunksY - 1);
  const cz = clamp(Math.floor(iz / volume.chunk), 0, volume.chunksZ - 1);
  volume.dirty.add(volume.chunkKey(cx, cy, cz));
}

// ------------------------------------------------------ differential weathering
export async function differentialWeathering(volume, opts = {}, onProgress = null, yieldFn = null) {
  const iterations = Math.max(1, Math.round(opts.iterations ?? 16));
  const rate = opts.rate ?? 0.16;                    // metres of front retreat / iteration at hardness 0
  const power = opts.power ?? 1.6;
  const jointInfluence = opts.jointInfluence ?? 1.4;
  const topExposure = opts.topExposure ?? 0.12;      // flat tops weather slowly (soil-mantled)
  const sedimentProtect = opts.sedimentProtect ?? 2.0;
  const band = opts.band ?? 2.2;                     // narrow band around the surface [m]
  const redistanceEvery = Math.max(1, Math.round(opts.redistanceEvery ?? 5));
  const { nx, ny, nz, sdf } = volume;
  const delta = new Float32Array(sdf.length);
  const g = zero3();
  let totalDelta = 0;

  for (let iter = 0; iter < iterations; iter++) {
    delta.fill(0);
    for (let iz = 0; iz < nz; iz++) for (let iy = 0; iy < ny; iy++) for (let ix = 0; ix < nx; ix++) {
      const idx = (iz * ny + iy) * nx + ix;
      if (Math.abs(sdf[idx]) > band) continue;
      volume.nodeGradient(ix, iy, iz, g);
      const gl = Math.hypot(g[0], g[1], g[2]);
      if (gl < 1e-5) continue;
      const nX = g[0] / gl, nY = g[1] / gl, nZ = g[2] / gl;
      const face = Math.hypot(nX, nZ);                    // 1 on a vertical face, 0 on flat ground
      const under = clamp(-nY, 0, 1);                     // cave roofs / cornice undersides
      const exposure = clamp(Math.max(face, under * 0.85, topExposure), 0, 1);
      const hard = volume.hardness[idx] / 255;
      const joint = volume.joint[idx] / 255;
      const protect = 1 / (1 + volume.sediment[idx] * sedimentProtect);
      // Additive joint term: a fracture is weak even inside a hard bed, so
      // joints open into slots in the cornices instead of only accelerating
      // beds that were already soft. Capped for front (CFL) stability.
      const bedRate = Math.pow(1 - hard, power) * 0.75;
      const jointRate = 0.45 * jointInfluence * joint * joint * (0.25 + 0.75 * (1 - hard));
      const speed = Math.min(rate * 1.35, rate * (bedRate + jointRate) * exposure * protect);
      if (speed <= 1e-6) continue;
      delta[idx] = speed;
    }
    for (let iz = 0; iz < nz; iz++) for (let iy = 0; iy < ny; iy++) for (let ix = 0; ix < nx; ix++) {
      const idx = (iz * ny + iy) * nx + ix;
      const d = delta[idx];
      if (!d) continue;
      sdf[idx] += d;
      totalDelta += d;
      markDirtyNode(volume, ix, iy, iz);
    }
    if ((iter + 1) % redistanceEvery === 0 && iter + 1 < iterations) volume.redistance();
    if (onProgress) onProgress((iter + 1) / iterations);
    if (yieldFn) await yieldFn();
  }
  volume.redistance();
  return { iterations, totalDelta };
}

// ------------------------------------------------------ block fall
export async function blockFall(volume, opts = {}, onProgress = null, yieldFn = null) {
  const maxBlocks = Math.max(0, Math.round(opts.maxBlocks ?? 26));
  const strength = opts.strength ?? 0.8;
  const rng = mulberry32(((opts.seed ?? 7) | 0) * 40507 + volume.sdf.length);
  const { nx, ny, nz, sdf } = volume;
  const layer = nx * ny;
  const g = zero3();
  const occupied = new Set();
  const candidates = [];

  for (let iz = 1; iz < nz - 1; iz++) for (let iy = 2; iy < ny - 1; iy++) for (let ix = 1; ix < nx - 1; ix++) {
    const idx = (iz * ny + iy) * nx + ix;
    const s = sdf[idx];
    if (s >= 0 || s < -1.3) continue;
    const airNeighbour = sdf[idx - 1] > 0 || sdf[idx + 1] > 0 || sdf[idx - nx] > 0 || sdf[idx + nx] > 0 || sdf[idx - layer] > 0 || sdf[idx + layer] > 0;
    if (!airNeighbour) continue;
    const hard = volume.hardness[idx] / 255;
    const joint = volume.joint[idx] / 255;
    if (hard < 0.56 || joint < 0.30) continue;
    volume.nodeGradient(ix, iy, iz, g);
    const gl = Math.hypot(g[0], g[1], g[2]) || 1;
    const nX = g[0] / gl, nY = g[1] / gl, nZ = g[2] / gl;
    if (nY > 0.25) continue; // upward-facing surfaces do not shed blocks
    const px = volume.nodeX(ix), py = volume.nodeY(iy), pz = volume.nodeZ(iz);
    const hl = Math.hypot(nX, nZ) || 1;
    const below = volume.sampleSdf(px + (nX / hl) * 1.3, py - 2.3, pz + (nZ / hl) * 1.3);
    const belowCentre = volume.sampleSdf(px, py - 2.6, pz);
    if (below < 0.7 && belowCentre < 0.7) continue; // still supported by the bed below
    const cell = `${Math.round(px / 4)}:${Math.round(py / 3)}:${Math.round(pz / 4)}`;
    if (occupied.has(cell)) continue;
    const prob = strength * joint * (nY < -0.15 ? 1 : 0.55);
    if (rng() > prob) continue;
    occupied.add(cell);
    candidates.push({ p: [px, py, pz], n: [nX, nY, nZ], bed: volume.bed[idx], thick: volume.bedThick[idx] / 10 });
  }

  // shuffle and take the budget
  for (let i = candidates.length - 1; i > 0; i--) { const j = (rng() * (i + 1)) | 0;[candidates[i], candidates[j]] = [candidates[j], candidates[i]]; }
  const take = candidates.slice(0, maxBlocks);
  const vnorm = (v) => { const l = Math.hypot(v[0], v[1], v[2]) || 1; return [v[0] / l, v[1] / l, v[2] / l]; };
  const vcross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  let done = 0;
  for (const c of take) {
    // Joint-bounded block: bed thickness tall, about a joint-spacing wide,
    // roughly half a bed deep into the face. Removed and landed as boxes —
    // spheres left round pockmarks that read as Swiss cheese, not rockfall.
    const sizeScale = opts.sizeScale ?? 1;
    const depth = clamp(c.thick * 0.9, 1.4, 4.2) * sizeScale;
    const height = clamp(c.thick, 1.2, 5.0) * sizeScale;
    const width = clamp(c.thick * (1.1 + rng() * 0.9), 1.8, 6.5) * sizeScale;
    const half = [width / 2, height / 2, depth / 2];
    let tangent = vcross([0, 1, 0], c.n);
    tangent = Math.hypot(...tangent) < 0.15 ? [1, 0, 0] : vnorm(tangent);
    const upish = vnorm(vcross(c.n, tangent));
    const centre = [
      c.p[0] - c.n[0] * half[2] * 0.35,
      c.p[1] - c.n[1] * half[2] * 0.35,
      c.p[2] - c.n[2] * half[2] * 0.35,
    ];
    volume.brushRemoveBox(centre, half, [tangent, upish, c.n], 0.16);
    // land it below: march down from just outboard of the face to the first surface
    const hl = Math.hypot(c.n[0], c.n[2]);
    const hn = hl > 0.15 ? [c.n[0] / hl, 0, c.n[2] / hl] : [1, 0, 0];
    const lx = c.p[0] + hn[0] * (depth + 1.4), lz = c.p[2] + hn[2] * (depth + 1.4);
    let ly = null;
    for (let y = c.p[1] - 0.5; y > volume.origin[1] + 0.5; y -= 0.5) {
      if (volume.sampleSdf(lx, y, lz) < 0) { ly = y; break; }
    }
    if (ly != null) {
      const landedHalf = rng() < 0.5 ? [half[0], half[2], half[1]] : half.slice(); // some blocks tumble
      const landedTangent = [-hn[2], 0, hn[0]];
      volume.brushAddBox([lx, ly + landedHalf[1] * 0.55, lz], landedHalf, [landedTangent, [0, 1, 0], hn], 0.2, { sediment: 0.75, bed: c.bed });
    }
    done++;
    if (onProgress) onProgress(done / Math.max(1, take.length));
    if (yieldFn && (done & 3) === 0) await yieldFn();
  }
  if (done > 0) volume.redistance();
  return { candidates: candidates.length, fallen: done };
}

// ------------------------------------------------------ thermal (loose material only)
export async function thermalLoose(volume, opts = {}, onProgress = null, yieldFn = null) {
  const iterations = Math.max(1, Math.round(opts.iterations ?? 14));
  const reposeTan = Math.tan((opts.reposeDeg ?? 34) * Math.PI / 180);
  const amount = opts.amount ?? 0.32;
  const { nx, ny, nz, sdf, sediment } = volume;
  const layer = nx * ny;
  const delta = new Float32Array(sdf.length);
  const targetSed = new Float32Array(sdf.length);
  const rng = mulberry32(((opts.seed ?? 3) | 0) * 8191 + 5);
  let transfers = 0;

  for (let iter = 0; iter < iterations; iter++) {
    delta.fill(0); targetSed.fill(0);
    let moved = 0;
    for (let iz = 1; iz < nz - 1; iz++) for (let iy = 2; iy < ny; iy++) for (let ix = 1; ix < nx - 1; ix++) {
      const idx = (iz * ny + iy) * nx + ix;
      if (sediment[idx] < 0.2) continue;
      const s = sdf[idx];
      if (s > 0.1 || s < -1.5) continue;
      const surface = sdf[idx - 1] > 0 || sdf[idx + 1] > 0 || sdf[idx - nx] > 0 || sdf[idx + nx] > 0 || sdf[idx - layer] > 0 || sdf[idx + layer] > 0;
      if (!surface) continue;
      // best supported air neighbour one step down
      let best = -1, bestSlope = reposeTan;
      for (let dz = -1; dz <= 1; dz++) for (let dx = -1; dx <= 1; dx++) {
        if (!dx && !dz) continue;
        const t = ((iz + dz) * ny + (iy - 1)) * nx + (ix + dx);
        if (sdf[t] < 0.05) continue;                       // target must be air
        if (sdf[t - nx] > 0.2 && iy - 1 > 1) continue;     // and (roughly) supported
        const slope = 1 / Math.hypot(dx, dz);
        if (slope > bestSlope) { bestSlope = slope; best = t; }
      }
      if (best < 0) continue;
      if (rng() > clamp((bestSlope - reposeTan) * 2.2, 0.08, 1)) continue;
      delta[idx] += amount;
      delta[best] -= amount;
      targetSed[best] = Math.max(targetSed[best], sediment[idx] * 0.95);
      moved++;
    }
    for (let i = 0; i < sdf.length; i++) {
      if (!delta[i]) continue;
      sdf[i] += delta[i];
      if (targetSed[i] > 0) sediment[i] = Math.max(sediment[i], targetSed[i]);
      if (delta[i] > 0 && sdf[i] > -0.1) sediment[i] *= 0.5;
    }
    volume.markAllDirty(); // transfers are sparse but widespread; block is small
    transfers += moved;
    if (onProgress) onProgress((iter + 1) / iterations);
    if (yieldFn) await yieldFn();
    if (moved === 0) break;
  }
  return { transfers };
}

// ------------------------------------------------------ hydraulic droplets on the surface
export async function hydraulicDroplets(volume, opts = {}, onProgress = null, yieldFn = null) {
  const count = Math.max(1, Math.round(opts.droplets ?? 5000));
  const lifetime = Math.round(opts.lifetime ?? 70);
  const inertia = opts.inertia ?? 0.25;
  const capacityFactor = opts.capacityFactor ?? 0.5;
  const erodeSpeed = opts.erodeSpeed ?? 0.30;
  const depositSpeed = opts.depositSpeed ?? 0.35;
  const evaporation = opts.evaporation ?? 0.012;
  const gravity = opts.gravity ?? 3;
  const stampRadius = opts.radius ?? 1.4;
  const hardnessInfluence = opts.hardnessInfluence ?? 0.85;
  const rng = mulberry32(((opts.seed ?? 11) | 0) * 31337 + count);
  const center = opts.center ?? null, brushR = opts.brushRadius ?? 8;
  const { nx, nz, origin, spacing } = volume;

  const g = zero3(), n = zero3();
  let eroded = 0, deposited = 0, completed = 0;

  const project = (p) => {
    const s = volume.sampleSdf(p[0], p[1], p[2]);
    volume.gradientAt(p[0], p[1], p[2], g);
    const gl = Math.hypot(g[0], g[1], g[2]);
    if (gl < 1e-5) return false;
    n[0] = g[0] / gl; n[1] = g[1] / gl; n[2] = g[2] / gl;
    p[0] -= n[0] * s; p[1] -= n[1] * s; p[2] -= n[2] * s;
    return true;
  };
  const addFlow = (x, z, a) => {
    const ix = clamp(Math.round((x - origin[0]) / spacing), 0, nx - 1);
    const iz = clamp(Math.round((z - origin[2]) / spacing), 0, nz - 1);
    volume.flow[iz * nx + ix] += a;
  };

  for (let d = 0; d < count; d++) {
    let x, z;
    if (center) {
      const a = rng() * Math.PI * 2, rr = brushR * Math.sqrt(rng());
      x = center[0] + Math.cos(a) * rr; z = center[2] + Math.sin(a) * rr;
    } else {
      x = origin[0] + 3 + rng() * (volume.sizeX - 6);
      z = origin[2] + 3 + rng() * (volume.sizeZ - 6);
    }
    const crossings = volume.columnCrossings(x, z);
    if (!crossings.length) continue;
    // entry crossings (air → solid going down) are walkable floors: indices 0,2,4…
    let y = crossings[0];
    if (center && crossings.length > 2) {
      let bestScore = Infinity;
      for (let k = 0; k < crossings.length; k += 2) {
        const score = Math.abs(crossings[k] - center[1]);
        if (score < bestScore) { bestScore = score; y = crossings[k]; }
      }
    }
    const p = [x, y + 0.03, z];
    const dir = zero3();
    let water = 1, speed = 0.1, carried = 0, carriedBed = volume.nearestBed(x, y, z);
    if (!project(p)) continue;

    for (let life = 0; life < lifetime; life++) {
      // tangential gravity on the current surface
      volume.normalAt(p[0], p[1], p[2], n);
      let tx = n[0] * n[1], ty = -1 + n[1] * n[1], tz = n[2] * n[1];
      const slopeMag = Math.hypot(tx, ty, tz);
      if (slopeMag < 0.02) {
        if (carried > 0) {
          const dep = Math.min(carried, 0.06);
          volume.stampDelta(p, stampRadius, -dep, { sediment: 0.8, bed: carriedBed });
          deposited += dep;
        }
        break;
      }
      tx /= slopeMag; ty /= slopeMag; tz /= slopeMag;
      if (dir[0] === 0 && dir[1] === 0 && dir[2] === 0) { dir[0] = tx; dir[1] = ty; dir[2] = tz; }
      else {
        dir[0] = dir[0] * inertia + tx * (1 - inertia);
        dir[1] = dir[1] * inertia + ty * (1 - inertia);
        dir[2] = dir[2] * inertia + tz * (1 - inertia);
        const dl = Math.hypot(dir[0], dir[1], dir[2]) || 1;
        dir[0] /= dl; dir[1] /= dl; dir[2] /= dl;
      }
      const step = spacing * 0.85;
      const np = [p[0] + dir[0] * step, p[1] + dir[1] * step, p[2] + dir[2] * step];
      if (np[0] < origin[0] + 1 || np[2] < origin[2] + 1 || np[0] > origin[0] + volume.sizeX - 1 || np[2] > origin[2] + volume.sizeZ - 1 || np[1] < 0.5) break;
      const yBefore = p[1];
      if (!project(np)) break;
      const dh = np[1] - yBefore;
      speed = Math.sqrt(Math.max(0.01, speed * speed + Math.max(-dh, 0) * gravity));
      if (dh > 0) speed = Math.max(0.05, speed * 0.7);
      water *= (1 - evaporation);
      const capacity = Math.max(0.01, capacityFactor * speed * water * (0.15 + slopeMag));
      const hard = volume.sampleHardness(p[0], p[1], p[2]);
      const joint = volume.sampleJoint(p[0], p[1], p[2]);

      if (carried > capacity || dh > 0.02) {
        let amount = dh > 0 ? Math.min(carried, dh * 0.8 + 0.004) : (carried - capacity) * depositSpeed;
        amount = clamp(amount, 0, 0.06);
        if (amount > 0) {
          volume.stampDelta(p, stampRadius, -amount, { sediment: 0.8, bed: carriedBed });
          carried -= amount; deposited += amount;
        }
      } else {
        let amount = Math.min((capacity - carried) * erodeSpeed, 0.055);
        amount *= (1 - hard * hardnessInfluence) * (0.55 + 0.9 * joint);
        amount = clamp(amount, 0, 0.055);
        if (amount > 0) {
          volume.stampDelta(p, stampRadius, amount);
          carried += amount; eroded += amount;
          carriedBed = volume.nearestBed(p[0], p[1] - 0.4, p[2]);
        }
      }
      addFlow(p[0], p[2], 0.02 * water);
      p[0] = np[0]; p[1] = np[1]; p[2] = np[2];
    }
    completed++;
    if (onProgress && (d % 500) === 0) onProgress(d / count);
    if (yieldFn && (d % 800) === 799) await yieldFn();
  }
  if (onProgress) onProgress(1);
  return { droplets: completed, eroded, deposited };
}

// ------------------------------------------------------ drainage + fluvial incision
export function computeDrainage(volume, heights = null) {
  const nx = volume.nx, nz = volume.nz, n = nx * nz;
  const h = heights ?? volume.extractTopHeight();
  const filled = Float32Array.from(h);
  const closed = new Uint8Array(n);
  // binary min-heap of cell indices keyed by filled height (priority-flood)
  const heap = [];
  const push = (i) => { heap.push(i); let c = heap.length - 1; while (c > 0) { const p = (c - 1) >> 1; if (filled[heap[p]] <= filled[heap[c]]) break;[heap[p], heap[c]] = [heap[c], heap[p]]; c = p; } };
  const pop = () => { const top = heap[0]; const last = heap.pop(); if (heap.length) { heap[0] = last; let c = 0; for (;;) { let l = 2 * c + 1, r = l + 1, m = c; if (l < heap.length && filled[heap[l]] < filled[heap[m]]) m = l; if (r < heap.length && filled[heap[r]] < filled[heap[m]]) m = r; if (m === c) break;[heap[c], heap[m]] = [heap[m], heap[c]]; c = m; } } return top; };
  for (let iz = 0; iz < nz; iz++) for (let ix = 0; ix < nx; ix++) {
    if (ix === 0 || iz === 0 || ix === nx - 1 || iz === nz - 1) { const i = iz * nx + ix; closed[i] = 1; push(i); }
  }
  const DIRS = [[1, 0], [-1, 0], [0, 1], [0, -1], [1, 1], [1, -1], [-1, 1], [-1, -1]];
  while (heap.length) {
    const c = pop();
    const cx = c % nx, cz = (c / nx) | 0;
    for (const [dx, dz] of DIRS) {
      const jx = cx + dx, jz = cz + dz;
      if (jx < 0 || jz < 0 || jx >= nx || jz >= nz) continue;
      const j = jz * nx + jx;
      if (closed[j]) continue;
      closed[j] = 1;
      filled[j] = Math.max(h[j], filled[c] + 1e-4);
      push(j);
    }
  }
  const receiver = new Int32Array(n).fill(-1);
  const slopeTo = new Float32Array(n);
  for (let iz = 0; iz < nz; iz++) for (let ix = 0; ix < nx; ix++) {
    const i = iz * nx + ix;
    let best = 0, bestJ = -1;
    for (const [dx, dz] of DIRS) {
      const jx = ix + dx, jz = iz + dz;
      if (jx < 0 || jz < 0 || jx >= nx || jz >= nz) continue;
      const j = jz * nx + jx;
      const sl = (filled[i] - filled[j]) / (Math.hypot(dx, dz) * volume.spacing);
      if (sl > best) { best = sl; bestJ = j; }
    }
    receiver[i] = bestJ; slopeTo[i] = best;
  }
  const order = Array.from({ length: n }, (_, i) => i).sort((a, b) => filled[b] - filled[a]);
  const area = new Float32Array(n).fill(volume.spacing * volume.spacing);
  for (const i of order) { const r = receiver[i]; if (r >= 0) area[r] += area[i]; }
  return { heights: h, filled, receiver, slopeTo, area, nx, nz };
}

export function blur2D(src, nx, nz, radius = 2) {
  const out = new Float32Array(src.length);
  for (let iz = 0; iz < nz; iz++) for (let ix = 0; ix < nx; ix++) {
    let sum = 0, cnt = 0;
    for (let dz = -radius; dz <= radius; dz++) for (let dx = -radius; dx <= radius; dx++) {
      const jx = ix + dx, jz = iz + dz;
      if (jx < 0 || jz < 0 || jx >= nx || jz >= nz) continue;
      sum += src[jz * nx + jx]; cnt++;
    }
    out[iz * nx + ix] = sum / cnt;
  }
  return out;
}

export function updateFlowMaps(volume, drainage) {
  const { area, nx, nz } = drainage;
  let maxA = 1;
  for (let i = 0; i < area.length; i++) if (area[i] > maxA) maxA = area[i];
  const invLog = 1 / Math.log1p(maxA);
  const flow = new Float32Array(area.length);
  for (let i = 0; i < area.length; i++) flow[i] = Math.log1p(area[i]) * invLog;
  volume.flow.set(flow);
  const blurred = blur2D(flow, nx, nz, 2);
  for (let i = 0; i < flow.length; i++) volume.moisture[i] = clamp(flow[i] * 0.65 + blurred[i] * 0.55, 0, 1);
  return { maxArea: maxA };
}

export async function fluvialIncision(volume, opts = {}, onProgress = null, yieldFn = null) {
  const iterations = Math.max(1, Math.round(opts.iterations ?? 6));
  const strength = opts.strength ?? 0.09;
  const concavity = opts.concavity ?? 0.5;             // stream-power m
  const deposition = opts.deposition ?? 0.5;
  const minArea = opts.minArea ?? 35;                  // m² of drainage before a channel exists
  const hardnessInfluence = opts.hardnessInfluence ?? 0.85;
  const refArea = 250;
  let channelCells = 0, totalIncision = 0, drainage = null;

  for (let iter = 0; iter < iterations; iter++) {
    drainage = computeDrainage(volume);
    const { heights, receiver, slopeTo, area, nx, nz } = drainage;
    channelCells = 0;
    for (let iz = 1; iz < nz - 1; iz++) for (let ix = 1; ix < nx - 1; ix++) {
      const i = iz * nx + ix;
      if (receiver[i] < 0 || area[i] < minArea) continue;
      const x = volume.nodeX(ix), z = volume.nodeZ(iz), topY = heights[i];
      const hard = volume.sampleHardness(x, topY - 0.4, z);
      const inc = clamp(strength * Math.pow(area[i] / refArea, concavity) * Math.max(slopeTo[i], 0.001) * (1 - hard * hardnessInfluence), 0, 0.3);
      const width = clamp(0.8 + 0.10 * Math.sqrt(area[i]), 0.9, 3.0);
      if (inc >= 0.004) {
        volume.stampDelta([x, topY, z], width, inc);
        totalIncision += inc;
        channelCells++;
      } else if (slopeTo[i] < 0.03 && deposition > 0) {
        // slack water drops its load: fans and floodplain flats
        const dep = clamp(strength * deposition * 0.35 * (1 - slopeTo[i] / 0.03), 0, 0.08);
        if (dep > 0.002) volume.stampDelta([x, topY, z], width, -dep, { sediment: 0.9 });
      }
    }
    if (onProgress) onProgress((iter + 1) / iterations);
    if (yieldFn) await yieldFn();
  }
  drainage = computeDrainage(volume);
  const { maxArea } = updateFlowMaps(volume, drainage);
  computeSuitability(volume, drainage);
  volume.markAllDirty();
  return { iterations, channelCells, totalIncision, maxArea };
}

// ------------------------------------------------------ vegetation suitability (phase-3 input, computed now)
export function computeSuitability(volume, drainage = null) {
  const nx = volume.nx, nz = volume.nz;
  const h = drainage?.heights ?? volume.extractTopHeight();
  let suitable = 0;
  for (let iz = 0; iz < nz; iz++) for (let ix = 0; ix < nx; ix++) {
    const i = iz * nx + ix;
    const x0 = h[iz * nx + Math.max(0, ix - 1)], x1 = h[iz * nx + Math.min(nx - 1, ix + 1)];
    const z0 = h[Math.max(0, iz - 1) * nx + ix], z1 = h[Math.min(nz - 1, iz + 1) * nx + ix];
    const slope = Math.hypot((x1 - x0) / 2, (z1 - z0) / 2) / volume.spacing;
    const x = volume.nodeX(ix), z = volume.nodeZ(iz);
    const soil = 0.25 + 0.75 * clamp(volume.sampleSediment(x, h[i] - 0.3, z) * 1.4, 0, 1);
    const slopeF = 1 - smoothstep(0.32, 0.78, slope);                 // ~18°..38°
    const elevF = smoothstep(3, 9, h[i]) * (1 - smoothstep(50, 62, h[i]));
    let waterF = 0.25 + 0.75 * volume.moisture[i];
    waterF *= 1 - 0.55 * smoothstep(0.9, 1.0, volume.flow[i]);       // not in the channel thalweg itself
    const s = clamp(slopeF * elevF * soil * waterF * 1.6, 0, 1);
    volume.suitability[i] = s;
    if (s > 0.5) suitable++;
  }
  return { suitableFraction: suitable / (nx * nz) };
}

// ------------------------------------------------------ local brush erosion (weathering + droplets in a sphere)
export async function brushErode(volume, center, radius, opts = {}) {
  const strength = opts.strength ?? 0.6;
  const entries = [];
  const g = zero3();
  volume._nodesInSphere(center, radius, (idx, px, py, pz, d, ix, iy, iz) => {
    if (Math.abs(volume.sdf[idx]) > 2.2) return;
    volume.nodeGradient(ix, iy, iz, g);
    const gl = Math.hypot(g[0], g[1], g[2]);
    if (gl < 1e-5) return;
    const face = Math.hypot(g[0], g[2]) / gl;
    const under = clamp(-g[1] / gl, 0, 1);
    const exposure = clamp(Math.max(face, under * 0.85, 0.12), 0, 1);
    const hard = volume.hardness[idx] / 255, joint = volume.joint[idx] / 255;
    const fall = 0.5 + 0.5 * Math.cos(Math.PI * clamp(d / radius, 0, 1));
    const bedRate = Math.pow(1 - hard, 1.6) * 0.75;
    const jointRate = 0.45 * 1.4 * joint * joint * (0.25 + 0.75 * (1 - hard));
    const speed = Math.min(0.3 * strength, 0.22 * strength * (bedRate + jointRate) * exposure * fall * 1.8);
    if (speed > 1e-5) entries.push(idx, speed);
  });
  for (let k = 0; k < entries.length; k += 2) volume.sdf[entries[k]] += entries[k + 1];
  const drops = Math.round((opts.droplets ?? 260) * strength);
  let dropStats = null;
  if (drops > 0) dropStats = await hydraulicDroplets(volume, { ...opts, droplets: drops, center, brushRadius: radius });
  return { weatheredNodes: entries.length / 2, dropStats };
}

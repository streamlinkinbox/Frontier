// Procedural test world for the fluid demo: an ocean bay with a sand beach, a river running down
// from the hills (with boulder rapids), a dirt race loop crossing the river at two fords, puddles
// and a mud pit on the track. Pure CPU code – also used by the Node tests.
import { RiverPath } from '../src/RiverPath.js';

// ---------------------------------------------------------------------------------------------
// noise
function hash2(i, j) {
  let h = (i * 374761393 + j * 668265263) | 0;
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967295;
}
function vnoise(x, z) {
  const i = Math.floor(x), j = Math.floor(z);
  const fx = x - i, fz = z - j;
  const ux = fx * fx * (3 - 2 * fx), uz = fz * fz * (3 - 2 * fz);
  const a = hash2(i, j), b = hash2(i + 1, j), c = hash2(i, j + 1), d = hash2(i + 1, j + 1);
  return (a + (b - a) * ux + (c - a) * uz + (a - b - c + d) * ux * uz) * 2 - 1;
}
export function fbm(x, z, oct = 4) {
  let s = 0, a = 0.5, f = 1;
  for (let o = 0; o < oct; o++) { s += a * vnoise(x * f + o * 17.3, z * f - o * 9.1); f *= 2.03; a *= 0.5; }
  return s;
}
const smooth = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
const lerp = (a, b, t) => a + (b - a) * t;

export const SEA_LEVEL = 0;

// shoreline (z of the waterline at a given x) and the natural terrain
export function shoreZ(x) { return -42 + 5 * Math.sin(x * 0.035 + 0.6) + 3 * fbm(x * 0.02, 3.1, 3); }
export function naturalHeight(x, z) {
  const d = z - shoreZ(x);
  if (d < 0) {
    // sea floor ~1:7, with a sand bar that makes the swell break offshore
    let h = Math.max(-9, d * 0.15) + 0.35 * fbm(x * 0.05, z * 0.05, 3);
    h += 0.9 * Math.exp(-(((d + 24) / 5) ** 2));
    return h;
  }
  const beach = Math.min(d, 30) * 0.075;
  const hills = smooth(8, 80, d) * (3 + 4 * smooth(50, 130, d));
  return beach + hills + fbm(x * 0.018, z * 0.018, 5) * 3 * smooth(5, 45, d);
}

// ---------------------------------------------------------------------------------------------
export function buildScene({ res = 512, size = 160 } = {}) {
  const nx = res, ny = res, dx = size / res;
  const origin = [-size / 2, -size / 2];
  const N = nx * ny;
  const bed = new Float32Array(N);
  const cx = (i) => origin[0] + (i + 0.5) * dx, cz = (j) => origin[1] + (j + 0.5) * dx;
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) bed[j * nx + i] = naturalHeight(cx(i), cz(j));

  // ---- river -------------------------------------------------------------------------------
  const riverPts = [[-58, 74], [-44, 56], [-26, 44], [-12, 28], [-6, 10], [6, -4], [18, -18], [24, -32], [27, -56]];
  const fordS = [0.12, 0.872];
  const river = new RiverPath(riverPts, {
    width: (s) => 7 + 2.5 * Math.sin(s * 9) * 0.3 + 6 * Math.max(...fordS.map((f) => Math.exp(-(((s - f) / 0.035) ** 2)))) + 5 * smooth(0.9, 1.0, s),
    depth: 1.1,
    bankSlope: 0.42,
    elevation: [4.4, -1.4],
    // gentle upper reach, steep rapids (s 0.30-0.42), gentle lower reach, estuary
    profile: (s) => {
      if (s < 0.3) return lerp(0, 0.086, s / 0.3);
      if (s < 0.42) return lerp(0.086, 0.36, smooth(0, 1, (s - 0.3) / 0.12));
      if (s < 0.85) return lerp(0.36, 0.67, (s - 0.42) / 0.43);
      return lerp(0.67, 1.0, (s - 0.85) / 0.15);
    },
  });
  river.carve(bed, nx, ny, dx, origin, 40);

  // boulders in the rapids + a few on the beach / in the surf
  const rocks = [];
  const rng = mulberry(7);
  for (let r = 0; r < 9; r++) {
    const s = 0.29 + 0.15 * (r / 8) + 0.01 * rng();
    const { point, tangent } = river.pointAt(s);
    const off = (rng() * 2 - 1) * 2.4;
    rocks.push({ x: point[0] - tangent[1] * off, z: point[1] + tangent[0] * off, r: 0.7 + 0.6 * rng(), h: 0.55 + 0.5 * rng() });
  }
  for (const [x, z, r, h] of [[-30, -45, 1.4, 1.1], [-26, -47, 0.9, 0.7], [40, -40, 1.6, 1.2], [44, -44, 1.0, 0.9], [-60, -52, 2.0, 1.6], [62, -55, 1.8, 1.4]]) rocks.push({ x, z, r, h });
  for (const rk of rocks) stamp(bed, nx, ny, dx, origin, rk.x, rk.z, rk.r * 2.2, (d) => rk.h * Math.exp(-((d / rk.r) ** 2) * 1.6), 'add');

  // ---- race track ----------------------------------------------------------------------------
  const roadPts = [[-62, -26], [-30, -30], [0, -28], [30, -24], [56, -14], [64, 12], [52, 40], [24, 56], [-8, 60], [-36, 62], [-62, 44], [-70, 10]];
  const road = new RiverPath(roadPts, { closed: true, width: 9 });
  const roadHalf = 4.2, roadBlend = 3.0;
  // height profile along the track: smoothed terrain, dipping into the two fords
  const M = 600;
  const prof = new Float32Array(M);
  for (let m = 0; m < M; m++) { const p = road.pointAt(m / M).point; prof[m] = sampleBilinear(bed, nx, ny, dx, origin, p[0], p[1]); }
  const sm = new Float32Array(M);
  const win = 22;
  for (let m = 0; m < M; m++) { let s = 0, w = 0; for (let k = -win; k <= win; k++) { const g = Math.exp(-((k / (win * 0.5)) ** 2)); s += prof[(m + k + M) % M] * g; w += g; } sm[m] = s / w; }
  // ford crossings
  const fords = fordS.map((s) => {
    const p = river.pointAt(s).point;
    const n = road.nearest(p[0], p[1]);
    return { x: p[0], z: p[1], t: n.s, level: river.bedAt(s) + 0.5 };
  });
  for (const f of fords) {
    for (let m = 0; m < M; m++) {
      let dt = Math.abs(m / M - f.t); dt = Math.min(dt, 1 - dt);
      const dist = dt * road.length;
      const w = 1 - smooth(4, 26, dist);
      sm[m] = lerp(sm[m], f.level, w);
    }
  }
  // never bury the road below sea level near the beach
  for (let m = 0; m < M; m++) sm[m] = Math.max(sm[m], 0.45);
  const roadH = (t) => { const x = ((t % 1) + 1) % 1 * M; const i = Math.floor(x); return lerp(sm[i % M], sm[(i + 1) % M], x - i); };

  const mask = new Uint8Array(N * 4);
  const roadInfo = new Float32Array(N * 2);
  for (let j = 0; j < ny; j++) {
    for (let i = 0; i < nx; i++) {
      const k = j * nx + i;
      const n = road.nearest(cx(i), cz(j), roadHalf + roadBlend + 2);
      roadInfo[k * 2] = n.dist; roadInfo[k * 2 + 1] = n.s;
      if (!Number.isFinite(n.dist)) continue;
      const h = roadH(n.s);
      const w = 1 - smooth(roadHalf, roadHalf + roadBlend, n.dist);
      // cross-fall: slightly crowned road
      const crown = -0.012 * Math.max(0, n.dist - 1);
      bed[k] = lerp(bed[k], h + crown, w);
      mask[k * 4] = Math.round(255 * (1 - smooth(roadHalf - 0.6, roadHalf + 1.2, n.dist)));
    }
  }

  // ---- puddles & mud pit ----------------------------------------------------------------------
  const puddleT = [0.035, 0.085, 0.125, 0.205, 0.27, 0.6, 0.64, 0.74, 0.95];
  const puddles = puddleT.map((t, q) => {
    const { point, tangent } = road.pointAt(t);
    const off = (rng() * 2 - 1) * 1.6;
    return { x: point[0] - tangent[1] * off, z: point[1] + tangent[0] * off, r: 1.6 + rng() * 1.8, depth: 0.09 + rng() * 0.09, t, stretch: 1.0 + rng() * 0.8, ang: Math.atan2(tangent[1], tangent[0]) };
  });
  for (const p of puddles) {
    p.rim = roadH(p.t);
    stampEllipse(bed, nx, ny, dx, origin, p, (r) => -p.depth * (1 - r * r) ** 2);
  }
  const mudT = 0.395;
  const mp = road.pointAt(mudT);
  const mudPit = { x: mp.point[0], z: mp.point[1], r: 5.2, stretch: 1.8, depth: 0.42, ang: Math.atan2(mp.tangent[1], mp.tangent[0]), t: mudT };
  mudPit.rim = roadH(mudT);
  stampEllipse(bed, nx, ny, dx, origin, mudPit, (r) => -mudPit.depth * smooth(1.0, 0.45, r));

  // ---- materials (R road, G river bed / pebbles, B sand, A mud) --------------------------------
  for (let j = 0; j < ny; j++) {
    for (let i = 0; i < nx; i++) {
      const k = j * nx + i, x = cx(i), z = cz(j);
      const d = z - shoreZ(x);
      const sand = (1 - smooth(10, 20, d + 3 * fbm(x * 0.1, z * 0.1, 2))) * (1 - smooth(1.6, 2.6, bed[k]));
      mask[k * 4 + 2] = Math.round(255 * Math.max(sand, bed[k] < -0.2 ? 1 : 0));
      const rd = river.info[k * 2], rs = river.info[k * 2 + 1];
      if (Number.isFinite(rd)) mask[k * 4 + 1] = Math.round(255 * (1 - smooth(river.widthFn(rs) * 0.5, river.widthFn(rs) * 0.5 + 1.8, rd)));
      const md = ellipseR(mudPit, x, z);
      mask[k * 4 + 3] = Math.round(255 * (1 - smooth(1.0, 1.35, md)));
    }
  }

  // ---- initial water ----------------------------------------------------------------------------
  const depth = new Float32Array(N), mud = new Float32Array(N);
  floodFillSea(bed, depth, nx, ny, SEA_LEVEL);
  river.fill(depth, bed, nx, ny, 0.72);
  for (const p of puddles) fillBasin(bed, depth, null, nx, ny, dx, origin, p, p.rim - 0.008, 0);
  fillBasin(bed, depth, mud, nx, ny, dx, origin, mudPit, mudPit.rim - 0.035, 1);

  // initial river current along the channel (staggered u,v)
  const vel = new Float32Array(N * 2);
  for (let k = 0; k < N; k++) {
    if (depth[k] > 0.05 && Number.isFinite(river.info[k * 2]) && river.info[k * 2] < 6) {
      const { tangent } = river.pointAt(river.info[k * 2 + 1]);
      vel[k * 2] = tangent[0] * 0.8; vel[k * 2 + 1] = tangent[1] * 0.8;
    }
  }

  // ---- far terrain (outside the simulated domain) ----------------------------------------------
  const far = buildFarTerrain(origin, size);

  const startT = 0.16;
  const sp = road.pointAt(startT);
  const heightAt = (x, z) => sampleBilinear(bed, nx, ny, dx, origin, x, z, naturalHeight);
  const scenarios = {
    beach: spawnOnRoad(road, 0.16, heightAt),
    ford: spawnOnRoad(road, fords[1].t - 0.04, heightAt),
    mud: spawnOnRoad(road, mudT - 0.05, heightAt),
    rapids: spawnOnRoad(road, fords[0].t + 0.035, heightAt, Math.PI),
    puddles: spawnOnRoad(road, 0.575, heightAt),
  };
  return {
    nx, ny, dx, origin, size, bed, depth, mud, vel, mask,
    river, road, roadH, roadInfo, fords, puddles, mudPit, rocks,
    far, seaLevel: SEA_LEVEL,
    heightAt,
    materialAt(x, z) {
      const i = Math.floor((x - origin[0]) / dx), j = Math.floor((z - origin[1]) / dx);
      if (i < 0 || j < 0 || i >= nx || j >= ny) return { road: 0, river: 0, sand: 0, mud: 0 };
      const k = (j * nx + i) * 4;
      return { road: mask[k] / 255, river: mask[k + 1] / 255, sand: mask[k + 2] / 255, mud: mask[k + 3] / 255 };
    },
    spawn: { position: [sp.point[0], heightAt(sp.point[0], sp.point[1]) + 0.9, sp.point[1]], heading: Math.atan2(sp.tangent[0], sp.tangent[1]) },
    scenarios,
  };
}

function spawnOnRoad(road, t, heightAt, extraYaw = 0) {
  const { point, tangent } = road.pointAt(((t % 1) + 1) % 1);
  return { position: [point[0], heightAt(point[0], point[1]) + 0.9, point[1]], heading: Math.atan2(tangent[0], tangent[1]) + extraYaw };
}

function mulberry(a) {
  return () => { a |= 0; a = (a + 0x6d2b79f5) | 0; let t = Math.imul(a ^ (a >>> 15), 1 | a); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}

export function sampleBilinear(arr, nx, ny, dx, origin, x, z, outside = null) {
  const fx = (x - origin[0]) / dx - 0.5, fz = (z - origin[1]) / dx - 0.5;
  if (outside && (fx < -0.5 || fz < -0.5 || fx > nx - 0.5 || fz > ny - 0.5)) return outside(x, z);
  const i = Math.floor(fx), j = Math.floor(fz);
  const tx = fx - i, tz = fz - j;
  const g = (a, b) => arr[Math.min(ny - 1, Math.max(0, b)) * nx + Math.min(nx - 1, Math.max(0, a))];
  return lerp(lerp(g(i, j), g(i + 1, j), tx), lerp(g(i, j + 1), g(i + 1, j + 1), tx), tz);
}

function stamp(bed, nx, ny, dx, origin, x, z, radius, fn, mode) {
  const i0 = Math.max(0, Math.floor((x - radius - origin[0]) / dx)), i1 = Math.min(nx - 1, Math.ceil((x + radius - origin[0]) / dx));
  const j0 = Math.max(0, Math.floor((z - radius - origin[1]) / dx)), j1 = Math.min(ny - 1, Math.ceil((z + radius - origin[1]) / dx));
  for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) {
    const d = Math.hypot(origin[0] + (i + 0.5) * dx - x, origin[1] + (j + 0.5) * dx - z);
    if (d < radius) { const k = j * nx + i; bed[k] = mode === 'add' ? bed[k] + fn(d) : fn(d, bed[k]); }
  }
}

// normalised elliptical radius (1 = edge), ellipse elongated along `ang` by `stretch`
function ellipseR(e, x, z) {
  const c = Math.cos(e.ang), s = Math.sin(e.ang);
  const lx = (x - e.x) * c + (z - e.z) * s, lz = -(x - e.x) * s + (z - e.z) * c;
  return Math.hypot(lx / (e.r * e.stretch), lz / e.r);
}
function stampEllipse(bed, nx, ny, dx, origin, e, fn) {
  const R = e.r * e.stretch * 1.05;
  const i0 = Math.max(0, Math.floor((e.x - R - origin[0]) / dx)), i1 = Math.min(nx - 1, Math.ceil((e.x + R - origin[0]) / dx));
  const j0 = Math.max(0, Math.floor((e.z - R - origin[1]) / dx)), j1 = Math.min(ny - 1, Math.ceil((e.z + R - origin[1]) / dx));
  for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) {
    const r = ellipseR(e, origin[0] + (i + 0.5) * dx, origin[1] + (j + 0.5) * dx);
    if (r < 1) bed[j * nx + i] += fn(r);
  }
}
function fillBasin(bed, depth, mud, nx, ny, dx, origin, e, level, mudC) {
  const R = e.r * e.stretch * 1.3;
  const i0 = Math.max(0, Math.floor((e.x - R - origin[0]) / dx)), i1 = Math.min(nx - 1, Math.ceil((e.x + R - origin[0]) / dx));
  const j0 = Math.max(0, Math.floor((e.z - R - origin[1]) / dx)), j1 = Math.min(ny - 1, Math.ceil((e.z + R - origin[1]) / dx));
  for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) {
    const r = ellipseR(e, origin[0] + (i + 0.5) * dx, origin[1] + (j + 0.5) * dx);
    if (r < 1.25) {
      const k = j * nx + i;
      const h = level - bed[k];
      if (h > depth[k]) { depth[k] = h; if (mud) mud[k] = mudC; }
    }
  }
}
function floodFillSea(bed, depth, nx, ny, level) {
  const seen = new Uint8Array(nx * ny);
  const stack = [];
  for (let i = 0; i < nx; i++) if (bed[i] < level) { stack.push(i); seen[i] = 1; }
  while (stack.length) {
    const k = stack.pop();
    depth[k] = level - bed[k];
    const i = k % nx, j = (k / nx) | 0;
    const nb = [i > 0 ? k - 1 : -1, i < nx - 1 ? k + 1 : -1, j > 0 ? k - nx : -1, j < ny - 1 ? k + nx : -1];
    for (const n of nb) if (n >= 0 && !seen[n] && bed[n] < level) { seen[n] = 1; stack.push(n); }
  }
}

function buildFarTerrain(origin, size) {
  // coarse terrain ring around the domain, aligned so its inner edge meets the domain boundary
  const cell = 10;
  const ext = 1400;
  const n = Math.round((2 * ext) / cell);
  const x0 = origin[0] + size / 2 - ext, z0 = origin[1] + size / 2 - ext;
  const pos = new Float32Array((n + 1) * (n + 1) * 3);
  const h = (x, z) => naturalHeight(x, z) - 0.05;
  for (let j = 0; j <= n; j++) for (let i = 0; i <= n; i++) {
    const x = x0 + i * cell, z = z0 + j * cell, o = (j * (n + 1) + i) * 3;
    pos[o] = x; pos[o + 1] = h(x, z); pos[o + 2] = z;
  }
  const nrm = new Float32Array(pos.length);
  for (let j = 0; j <= n; j++) for (let i = 0; i <= n; i++) {
    const o = (j * (n + 1) + i) * 3;
    const x = pos[o], z = pos[o + 2];
    const gx = (h(x + 1, z) - h(x - 1, z)) / 2, gz = (h(x, z + 1) - h(x, z - 1)) / 2;
    const l = Math.hypot(gx, 1, gz);
    nrm[o] = -gx / l; nrm[o + 1] = 1 / l; nrm[o + 2] = -gz / l;
  }
  const masks = new Uint8Array((n + 1) * (n + 1) * 4);
  for (let j = 0; j <= n; j++) for (let i = 0; i <= n; i++) {
    const o = j * (n + 1) + i;
    const x = pos[o * 3], z = pos[o * 3 + 2];
    const d = z - shoreZ(x);
    masks[o * 4 + 2] = d < 18 || pos[o * 3 + 1] < 1.5 ? 255 : 0;
  }
  const idx = [];
  const inside = (x, z) => x >= origin[0] - 0.01 && x <= origin[0] + size + 0.01 && z >= origin[1] - 0.01 && z <= origin[1] + size + 0.01;
  for (let j = 0; j < n; j++) for (let i = 0; i < n; i++) {
    const xa = x0 + i * cell, za = z0 + j * cell;
    if (inside(xa, za) && inside(xa + cell, za + cell)) continue; // skip cells covered by the domain
    const a = j * (n + 1) + i, b = a + 1, c = a + n + 1, d = c + 1;
    idx.push(a, c, d, a, d, b);
  }
  return { positions: pos, normals: nrm, masks, indices: new Uint32Array(idx) };
}

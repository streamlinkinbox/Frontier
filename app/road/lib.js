/* ============================================================
   FRONTIER · ROAD EDITOR — math / sampling / deterministic RNG
   Pure ES module, no renderer dependencies (runs headless).
   ============================================================ */

export const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
export const lerp = (a, b, t) => a + (b - a) * t;
export const invLerp = (a, b, v) => b === a ? 0 : (v - a) / (b - a);
export const smoothstep = (e0, e1, x) => { const t = clamp(invLerp(e0, e1, x), 0, 1); return t * t * (3 - 2 * t); };
export const deg = r => r * 180 / Math.PI;
export const rad = d => d * Math.PI / 180;
export const round = (v, n = 3) => { const p = 10 ** n; return Math.round(v * p) / p; };
export const TAU = Math.PI * 2;

/** Deterministic 32-bit hash → [0,1). */
export function hash01(...nums) {
  let h = 2166136261 >>> 0;
  for (const n of nums) {
    let x = Math.imul((n * 100000) | 0, 2246822519) >>> 0;
    h ^= x; h = Math.imul(h, 3266489917) >>> 0;
  }
  return (h >>> 0) / 4294967296;
}

export class RNG {
  constructor(seed = 1) { this.seed = (seed >>> 0) || 1; this.s = this.seed; }
  reseed(seed) { this.seed = (seed >>> 0) || 1; this.s = this.seed; return this; }
  next() { let t = this.s += 0x6D2B79F5; t = Math.imul(t ^ t >>> 15, t | 1); t ^= t + Math.imul(t ^ t >>> 7, t | 61); return ((t ^ t >>> 14) >>> 0) / 4294967296; }
  range(a, b) { return a + (b - a) * this.next(); }
  int(a, b) { return Math.floor(this.range(a, b + 1 - 1e-9)); }
  chance(p) { return this.next() < p; }
  pick(list) { return list[Math.floor(this.next() * list.length) % list.length]; }
  sign() { return this.next() < .5 ? -1 : 1; }
  gauss(sigma = 1) { const u = 1 - this.next(), v = this.next(); return Math.sqrt(-2 * Math.log(u)) * Math.cos(TAU * v) * sigma; }
  shuffle(a) { for (let i = a.length - 1; i > 0; i--) { const j = Math.floor(this.next() * (i + 1));[a[i], a[j]] = [a[j], a[i]]; } return a; }
  /** stable-per-key random in [0,1) — for deterministic jitter that survives edits */
  static keyed(...k) { return hash01(...k); }
}

export const strSeed = str => { let h = 2166136261 >>> 0; for (let i = 0; i < str.length; i++) { h ^= str.charCodeAt(i); h = Math.imul(h, 16777619) >>> 0; } return h >>> 0; };

/* ------------------------------- 2D helpers (XZ plane) ------------------------------- */
export const sub = (a, b) => ({ x: a.x - b.x, z: a.z - b.z });
export const add = (a, b) => ({ x: a.x + b.x, z: a.z + b.z });
export const mul = (a, s) => ({ x: a.x * s, z: a.z * s });
export const dot = (a, b) => a.x * b.x + a.z * b.z;
export const cross = (a, b) => a.x * b.z - a.z * b.x;
export const len = a => Math.hypot(a.x, a.z);
export const dist = (a, b) => Math.hypot(a.x - b.x, a.z - b.z);
export const norm = a => { const l = Math.hypot(a.x, a.z) || 1; return { x: a.x / l, z: a.z / l }; };
export const perp = a => ({ x: -a.z, z: a.x });            // left of travel
export const angleOf = a => Math.atan2(a.x, -a.z);          // compass-ish
export const rotate = (a, r) => ({ x: a.x * Math.cos(r) - a.z * Math.sin(r), z: a.x * Math.sin(r) + a.z * Math.cos(r) });
export const at = (p, dir, d) => ({ x: p.x + dir.x * d, z: p.z + dir.z * d });
export const offset = (p, nrm, d) => ({ x: p.x + nrm.x * d, z: p.z + nrm.z * d });

export function snap(v, step) { return step > 0 ? Math.round(v / step) * step : v; }
export function snapAngle(p, from, stepDeg) {
  if (!stepDeg) return p;
  const d = Math.hypot(p.x - from.x, p.z - from.z);
  let a = Math.atan2(p.z - from.z, p.x - from.x);
  const s = rad(stepDeg);
  a = Math.round(a / s) * s;
  return { x: from.x + Math.cos(a) * d, z: from.z + Math.sin(a) * d };
}

/* ------------------------------- polyline cleanup / smoothing ------------------------------- */
export function dedupe(pts, eps = 0.05) {
  const out = [];
  for (const p of pts) {
    if (!out.length || dist(out[out.length - 1], p) > eps) out.push({ x: p.x, z: p.z, y: p.y ?? 0 });
  }
  if (out.length > 1 && dist(out[0], out[out.length - 1]) <= eps) out.pop();
  return out;
}

/** Chaikin corner cutting — keeps roads inside the control hull (no overshoot like Catmull-Rom). */
export function chaikin(pts, iters = 2, closed = false) {
  let p = pts;
  for (let k = 0; k < iters; k++) {
    const out = [];
    const n = p.length;
    if (n < 3) { out.push(...p); return out; }
    if (!closed) out.push(p[0]);
    const last = closed ? n : n - 1;
    for (let i = 0; i < last; i++) {
      const a = p[i], b = p[(i + 1) % n];
      out.push({ x: a.x * .75 + b.x * .25, z: a.z * .75 + b.z * .25, y: (a.y ?? 0) * .75 + (b.y ?? 0) * .25 });
      out.push({ x: a.x * .25 + b.x * .75, z: a.z * .25 + b.z * .75, y: (a.y ?? 0) * .25 + (b.y ?? 0) * .75 });
    }
    if (!closed) out.push(p[n - 1]);
    p = out;
  }
  return p;
}

export function polylineLength(pts, closed = false) {
  let L = 0;
  for (let i = 1; i < pts.length; i++) L += dist(pts[i - 1], pts[i]);
  if (closed && pts.length > 2) L += dist(pts[pts.length - 1], pts[0]);
  return L;
}

/**
 * Resample a smoothed polyline at fixed arc-length spacing.
 * → samples: {x,z,y, tx,tz (tangent), nx,nz (left normal), s (arc length)}
 */
export function resample(pts, step = 2, closed = false) {
  const src = pts;
  if (src.length < 2) return [];
  const path = src.slice();
  if (closed) path.push(src[0]);
  const segs = [];
  let total = 0;
  for (let i = 1; i < path.length; i++) {
    const d = dist(path[i - 1], path[i]);
    if (d > 1e-6) { segs.push({ a: path[i - 1], b: path[i], d, s0: total }); total += d; }
  }
  if (!total) return [];
  const count = Math.max(2, Math.round(total / step));
  const ds = total / count;
  const out = [];
  let si = 0, acc = 0;
  for (let i = 0; i <= count; i++) {
    const target = Math.min(i * ds, total - 1e-4);
    while (si < segs.length - 1 && target > segs[si].s0 + segs[si].d) si++;
    const s = segs[si];
    const t = clamp((target - s.s0) / s.d, 0, 1);
    out.push({
      x: lerp(s.a.x, s.b.x, t), z: lerp(s.a.z, s.b.z, t),
      y: lerp(s.a.y ?? 0, s.b.y ?? 0, t),
      tx: s.b.x - s.a.x, tz: s.b.z - s.a.z, s: target, _i: i, _acc: acc + t
    });
    acc += t;
  }
  // normalise tangents + left normals, smooth frame at joints
  for (let i = 0; i < out.length; i++) {
    const p = out[i], a = out[Math.max(0, i - 1)], b = out[Math.min(out.length - 1, i + 1)];
    const t = norm({ x: b.x - a.x, z: b.z - a.z });
    p.tx = t.x; p.tz = t.z; p.nx = -t.z; p.nz = t.x;
  }
  out.forEach(p => { delete p._i; delete p._acc; });
  return out;
}

/** World-space lookup of a point at arc position `t` (metres from start). */
export function sampleAt(samples, t) {
  if (!samples.length) return null;
  const last = samples[samples.length - 1].s;
  const tt = clamp(t, 0, last);
  let i = 0;
  while (i < samples.length - 2 && samples[i + 1].s < tt) i++;
  const a = samples[i], b = samples[i + 1] || a;
  const k = invLerp(a.s, b.s, tt);
  const x = lerp(a.x, b.x, k), z = lerp(a.z, b.z, k), y = lerp(a.y, b.y, k);
  return { x, z, y, tx: lerp(a.tx, b.tx, k), tz: lerp(a.tz, b.tz, k), nx: lerp(a.nx, b.nx, k), nz: lerp(a.nz, b.nz, k) };
}

export const bounds = pts => {
  let minx = Infinity, minz = Infinity, maxx = -Infinity, maxz = -Infinity;
  for (const p of pts) { if (p.x < minx) minx = p.x; if (p.z < minz) minz = p.z; if (p.x > maxx) maxx = p.x; if (p.z > maxz) maxz = p.z; }
  return { minx, minz, maxx, maxz, w: maxx - minx, h: maxz - minz, cx: (minx + maxx) / 2, cz: (minz + maxz) / 2 };
};

/* ------------------------------- polygon triangulation ------------------------------- */
const triArea = (a, b, c) => (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
const ptInTri = (p, a, b, c) => {
  const d1 = triArea(a, b, p), d2 = triArea(b, c, p), d3 = triArea(c, a, p);
  const neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
  return !(neg && pos);
};

/**
 * Ear clipping for simple polygons ( [{x,z}] , either winding ).
 * Returns triangle index triples; falls back to a centroid fan for degenerate input.
 */
export function triangulatePoly(pts) {
  const n = pts.length;
  if (n < 3) return [];
  let area = 0;
  for (let i = 0; i < n; i++) { const a = pts[i], b = pts[(i + 1) % n]; area += a.x * b.z - b.x * a.z; }
  const sign = area > 0 ? 1 : -1;                    // 1 = CCW in x/z space
  const idx = [];
  for (let i = 0; i < n; i++) {
    const a = pts[i], b = pts[(i + 1) % n], c = pts[(i + 2) % n];
    if (sign * triArea(a, b, c) > 1e-6) idx.push(i, i + 1, i + 2);   // already-ear on convex runs
  }
  const list = pts.map((_, i) => i);
  const tris = [];
  let guard = 0, prev = -1;
  while (list.length > 2 && guard++ < n * n + 64) {
    let clipped = false;
    for (let i = 0; i < list.length; i++) {
      const p0 = list[(i - 1 + list.length) % list.length], p1 = list[i], p2 = list[(i + 1) % list.length];
      const a = pts[p0], b = pts[p1], c = pts[p2];
      if (sign * triArea(a, b, c) <= 1e-4) continue;
      let ok = true;
      for (const j of list) {
        if (j === p0 || j === p1 || j === p2) continue;
        if (ptInTri(pts[j], a, b, c)) { ok = false; break; }
      }
      if (!ok) continue;
      tris.push(p0, p1, p2);
      list.splice(i, 1);
      clipped = true;
      break;
    }
    if (!clipped) {
      if (prev === list.length) break;
      prev = list.length;
      // fallback: fan from centroid of remainder
      const cx = list.reduce((s, i) => s + pts[i].x, 0) / list.length;
      const cz = list.reduce((s, i) => s + pts[i].z, 0) / list.length;
      const c0 = pts[list[0]];
      for (let i = 1; i < list.length - 1; i++) tris.push(list[0], list[i], list[i + 1]);
      list.length = 0;
      if (Math.abs(cx - c0.x) < 0 && Math.abs(cz - c0.z) < 0) break;
      break;
    }
  }
  if (tris.length < 3) { for (let i = 1; i < n - 1; i++) tris.push(0, i, i + 1); }
  return tris;
}

/** convex hull (Andrew monotone chain) in x/z */
export function hull(pts) {
  const p = pts.slice().sort((a, b) => a.x - b.x || a.z - b.z);
  if (p.length < 3) return p;
  const cr = (o, a, b) => (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x);
  const lo = [], up = [];
  for (const q of p) { while (lo.length >= 2 && cr(lo[lo.length - 2], lo[lo.length - 1], q) <= 0) lo.pop(); lo.push(q); }
  for (let i = p.length - 1; i >= 0; i--) { const q = p[i]; while (up.length >= 2 && cr(up[up.length - 2], up[up.length - 1], q) <= 0) up.pop(); up.push(q); }
  lo.pop(); up.pop();
  return lo.concat(up);
}

/* ------------------------------- buckets: geometry assembly without a renderer ------------------------------- */
export class Builder {
  /** position / normal / uv / color, grouped by a material key */
  constructor() { this.groups = new Map(); }
  group(key) {
    let g = this.groups.get(key);
    if (!g) { g = { key, pos: [], nrm: [], uv: [], col: [], idx: [], n: 0 }; this.groups.set(key, g); }
    return g;
  }
  /** push a triangle strip band: `rows` = arrays of {x,y,z} cross-section, `cols` = along-length */
  quad(key, a, b, c, d, uv, col) {
    const g = this.group(key);
    const ux1 = b.x - a.x, uy1 = b.y - a.y, uz1 = b.z - a.z;
    const ux2 = d.x - a.x, uy2 = d.y - a.y, uz2 = d.z - a.z;
    let nx = uy1 * uz2 - uz1 * uy2, ny = uz1 * ux2 - ux1 * uz2, nz = ux1 * uy2 - uy1 * ux2;
    const l = Math.hypot(nx, ny, nz) || 1; nx /= l; ny /= l; nz /= l;
    const base = g.n;
    const pts = [a, b, c, d];
    const uvs = uv || [[0, 0], [1, 0], [1, 1], [0, 1]];
    for (let i = 0; i < 4; i++) {
      g.pos.push(pts[i].x, pts[i].y, pts[i].z);
      g.nrm.push(nx, ny, nz);
      g.uv.push(uvs[i][0], uvs[i][1]);
      g.col.push(col ? col[0] : 1, col ? col[1] : 1, col ? col[2] : 1);
    }
    g.idx.push(base, base + 1, base + 2, base, base + 2, base + 3);
    g.n += 4;
    return g;
  }
  tri(key, a, b, c, col) {
    const g = this.group(key);
    const nx = (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y);
    const ny = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z);
    const nz = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    const l = Math.hypot(nx, ny, nz) || 1;
    const base = g.n;
    for (const p of [a, b, c]) { g.pos.push(p.x, p.y, p.z); g.nrm.push(nx / l, ny / l, nz / l); g.uv.push(p.u ?? 0, p.v ?? 0); g.col.push(col ? col[0] : 1, col ? col[1] : 1, col ? col[2] : 1); }
    g.idx.push(base, base + 1, base + 2);
    g.n += 3;
    return g;
  }
  polygon(key, pts2d, y0, lift = 0, col) {
    const tris = triangulatePoly(pts2d);
    for (let i = 0; i < tris.length; i += 3) {
      const mk = p => ({ x: p.x, y: y0 + lift, z: p.z, u: (p.x - 9999) / -1, v: 0 });
      const A = pts2d[tris[i]], B = pts2d[tris[i + 1]], C = pts2d[tris[i + 2]];
      const uvA = { x: A.x / 4, z: A.z / 4 }, uvB = { x: B.x / 4, z: B.z / 4 }, uvC = { x: C.x / 4, z: C.z / 4 };
      const pa = { x: A.x, y: y0 + lift, z: A.z }, pb = { x: B.x, y: y0 + lift, z: B.z }, pc = { x: C.x, y: y0 + lift, z: C.z };
      const g = this.group(key);
      const base = g.n;
      for (const [p, u] of [[pa, uvA], [pb, uvB], [pc, uvC]]) {
        g.pos.push(p.x, p.y, p.z); g.nrm.push(0, 1, 0); g.uv.push(u.x, u.z); g.col.push(col ? col[0] : 1, col ? col[1] : 1, col ? col[2] : 1);
      }
      g.idx.push(base, base + 1, base + 2); g.n += 3;
    }
  }
  get stats() {
    let tris = 0, verts = 0, groups = 0;
    for (const g of this.groups.values()) { tris += g.idx.length / 3; verts += g.n; groups++; }
    return { tris: Math.round(tris), verts, groups };
  }
}

/** instance record for prop scattering */
export function instance(list, proto, x, y, z, rotY = 0, scale = 1, extra) {
  list.push({ proto, x, y, z, rotY, sx: Array.isArray(scale) ? scale[0] : scale, sy: Array.isArray(scale) ? scale[1] : scale, sz: Array.isArray(scale) ? scale[2] : scale, ...extra });
}

export const fmtNum = (v, unit = '', d = 1) => `${(v ?? 0).toFixed(d)}${unit}`;
export const fmtInt = v => Math.round(v).toLocaleString('en-US');
export const fmtKm = m => m >= 1000 ? `${(m / 1000).toFixed(2)} km` : `${Math.round(m)} m`;

/** shallow structural clone that survives JSON (used for undo + serialisation) */
export const clone = o => JSON.parse(JSON.stringify(o));

/** id generator that is stable across reloads of one session */
let _id = 0;
export const uid = (prefix = 'id') => `${prefix}-${(++_id).toString(36)}${Date.now().toString(36).slice(-3)}`;

/** deterministic per-road jitter helper so edits don't reshuffle props */
export const jitter = (roadId, index, salt = 0) => hash01(strSeed(roadId) % 1000003, index * 131 + salt);

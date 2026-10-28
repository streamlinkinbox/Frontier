// Shared math / geometry helpers
import * as THREE from '../vendor/three.module.min.js';

export const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
export const lerp = (a, b, t) => a + (b - a) * t;
export const smooth = (e0, e1, x) => {
  const t = clamp((x - e0) / (e1 - e0), 0, 1);
  return t * t * (3 - 2 * t);
};
export const TAU = Math.PI * 2;

export function rng(seed) {
  let a = seed >>> 0;
  const f = () => {
    a |= 0; a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
  f.range = (lo, hi) => lo + (hi - lo) * f();
  f.pick = arr => arr[Math.floor(f() * arr.length)];
  return f;
}

// ---------- value noise ----------
function hash2(x, y) {
  let h = (x * 374761393 + y * 668265263) | 0;
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}
export function vnoise(x, y) {
  const xi = Math.floor(x), yi = Math.floor(y);
  const xf = x - xi, yf = y - yi;
  const u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
  const a = hash2(xi, yi), b = hash2(xi + 1, yi), c = hash2(xi, yi + 1), d = hash2(xi + 1, yi + 1);
  return lerp(lerp(a, b, u), lerp(c, d, u), v) * 2 - 1;
}
export function fbm(x, y, oct = 3) {
  let s = 0, amp = 1, f = 1, n = 0;
  for (let i = 0; i < oct; i++) { s += vnoise(x * f + i * 17.3, y * f - i * 9.1) * amp; n += amp; amp *= 0.5; f *= 2.03; }
  return s / n;
}

// Monotone cubic (PCHIP) through keyframes [[x,y],...]
export function pchip(keys) {
  const n = keys.length, xs = keys.map(k => k[0]), ys = keys.map(k => k[1]);
  const d = [], m = new Array(n).fill(0);
  for (let i = 0; i < n - 1; i++) d.push((ys[i + 1] - ys[i]) / (xs[i + 1] - xs[i]));
  m[0] = d[0]; m[n - 1] = d[n - 2];
  for (let i = 1; i < n - 1; i++) {
    if (d[i - 1] * d[i] <= 0) m[i] = 0;
    else { const w1 = 2 * (xs[i + 1] - xs[i]) + (xs[i] - xs[i - 1]), w2 = (xs[i + 1] - xs[i]) + 2 * (xs[i] - xs[i - 1]); m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i]); }
  }
  return x => {
    if (x <= xs[0]) return ys[0] + (x - xs[0]) * m[0] * 0; // clamp
    if (x >= xs[n - 1]) return ys[n - 1];
    let i = 0; while (x > xs[i + 1]) i++;
    const h = xs[i + 1] - xs[i], t = (x - xs[i]) / h, t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * ys[i] + (t3 - 2 * t2 + t) * h * m[i] + (-2 * t3 + 3 * t2) * ys[i + 1] + (t3 - t2) * h * m[i + 1];
  };
}

// distance from point to segment, returns {d, t}
export function segDist(px, pz, ax, az, bx, bz) {
  const dx = bx - ax, dz = bz - az;
  const L2 = dx * dx + dz * dz || 1e-9;
  let t = ((px - ax) * dx + (pz - az) * dz) / L2;
  t = clamp(t, 0, 1);
  const cx = ax + dx * t, cz = az + dz * t;
  return { d: Math.hypot(px - cx, pz - cz), t };
}

// ---------- geometry building ----------
const _c = new THREE.Color();
/** Convert geometry to non-indexed, paint it one colour, optionally transform. */
export function paint(geo, color, matrix) {
  let g = geo.index ? geo.toNonIndexed() : geo;
  if (matrix) g.applyMatrix4(matrix);
  g.deleteAttribute('uv');
  const n = g.attributes.position.count;
  const col = new Float32Array(n * 3);
  _c.set(color);
  for (let i = 0; i < n; i++) { col[i * 3] = _c.r; col[i * 3 + 1] = _c.g; col[i * 3 + 2] = _c.b; }
  g.setAttribute('color', new THREE.BufferAttribute(col, 3));
  if (!g.attributes.normal) g.computeVertexNormals();
  return g;
}

/** Merge non-indexed geometries that all have position/normal/color. */
export function merge(geos) {
  let total = 0;
  for (const g of geos) total += g.attributes.position.count;
  const pos = new Float32Array(total * 3), nor = new Float32Array(total * 3), col = new Float32Array(total * 3);
  let o = 0;
  for (const g of geos) {
    pos.set(g.attributes.position.array, o * 3);
    nor.set(g.attributes.normal.array, o * 3);
    col.set(g.attributes.color.array, o * 3);
    o += g.attributes.position.count;
  }
  const out = new THREE.BufferGeometry();
  out.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  out.setAttribute('normal', new THREE.BufferAttribute(nor, 3));
  out.setAttribute('color', new THREE.BufferAttribute(col, 3));
  out.computeBoundingSphere();
  return out;
}

const _m = new THREE.Matrix4(), _q = new THREE.Quaternion(), _e = new THREE.Euler(), _s = new THREE.Vector3(), _p = new THREE.Vector3();
/** Build a transform matrix: position, euler rotation (YXZ order by default), scale */
export function mat(x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0, sx = 1, sy = sx, sz = sx, order = 'YXZ') {
  _e.set(rx, ry, rz, order); _q.setFromEuler(_e); _p.set(x, y, z); _s.set(sx, sy, sz);
  return new THREE.Matrix4().compose(_p, _q, _s);
}

/** Geometry from raw triangle list [[ax,ay,az],[bx..],[cx..]] per face with colours.
 *  `ref(cx,cy,cz)` returns an interior reference point; any face whose normal
 *  points toward it gets its winding flipped so every normal faces outward. */
export function triGeo(tris, ref) {
  const n = tris.length;
  const pos = new Float32Array(n * 9), nor = new Float32Array(n * 9), col = new Float32Array(n * 9);
  const a = new THREE.Vector3(), b = new THREE.Vector3(), c = new THREE.Vector3(), e1 = new THREE.Vector3(), e2 = new THREE.Vector3(), nn = new THREE.Vector3();
  let flipped = 0;
  tris.forEach((t, i) => {
    a.fromArray(t[0]); b.fromArray(t[1]); c.fromArray(t[2]);
    e1.subVectors(b, a); e2.subVectors(c, a); nn.crossVectors(e1, e2);
    if (nn.lengthSq() < 1e-12) nn.set(0, 1, 0);
    nn.normalize();
    if (ref || t[4]) {
      const cx = (a.x + b.x + c.x) / 3, cy = (a.y + b.y + c.y) / 3, cz = (a.z + b.z + c.z) / 3;
      const r = t[4] || ref(cx, cy, cz);
      if (nn.x * (cx - r[0]) + nn.y * (cy - r[1]) + nn.z * (cz - r[2]) < 0) { const tmp = b.clone(); b.copy(c); c.copy(tmp); nn.negate(); flipped++; }
    }
    a.toArray(pos, i * 9); b.toArray(pos, i * 9 + 3); c.toArray(pos, i * 9 + 6);
    for (let k = 0; k < 3; k++) nn.toArray(nor, i * 9 + k * 3);
    _c.set(t[3]);
    for (let k = 0; k < 3; k++) { col[i * 9 + k * 3] = _c.r; col[i * 9 + k * 3 + 1] = _c.g; col[i * 9 + k * 3 + 2] = _c.b; }
  });
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  g.setAttribute('normal', new THREE.BufferAttribute(nor, 3));
  g.setAttribute('color', new THREE.BufferAttribute(col, 3));
  g.userData.flipped = flipped;
  return g;
}

/** Recompute flat normals for a non-indexed geometry (after transforms). */
export function flatNormals(g) { g.computeVertexNormals(); return g; }

/** Tint every vertex colour of a geometry by random per-face jitter (low-poly shading variation) */
export function jitterFaces(g, amount, r) {
  const col = g.attributes.color.array;
  for (let f = 0; f < col.length; f += 9) {
    const k = 1 + (r() * 2 - 1) * amount;
    for (let i = 0; i < 9; i++) col[f + i] *= k;
  }
  return g;
}

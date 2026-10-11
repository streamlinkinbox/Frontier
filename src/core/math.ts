/**
 * Frontier Road Studio — math core.
 * Vec2 (plan XZ), Vec3, Mat4 and curve helpers. No dependencies.
 */

export type V2 = [number, number];
export type V3 = [number, number, number];
export type V4 = [number, number, number, number];
export type Mat4 = Float32Array;

export const EPS = 1e-9;
export const TAU = Math.PI * 2;
export const DEG = Math.PI / 180;
export const RAD = 180 / Math.PI;

export const clamp = (v: number, a: number, b: number) => (v < a ? a : v > b ? b : v);
export const clamp01 = (v: number) => clamp(v, 0, 1);
export const lerp = (a: number, b: number, t: number) => a + (b - a) * t;
export const smoothstep = (t: number) => {
  const x = clamp01(t);
  return x * x * (3 - 2 * x);
};
export const smootherstep = (t: number) => {
  const x = clamp01(t);
  return x * x * x * (x * (x * 6 - 15) + 10);
};
export const invLerp = (a: number, b: number, v: number) => (b - a === 0 ? 0 : (v - a) / (b - a));
export const sign = (v: number) => (v < 0 ? -1 : v > 0 ? 1 : 0);
export const approx = (a: number, b: number, eps = 1e-6) => Math.abs(a - b) <= eps;

// ---------------------------------------------------------------- vec2 (XZ)

export const v2 = (x: number, z: number): V2 => [x, z];
export const v2add = (a: V2, b: V2): V2 => [a[0] + b[0], a[1] + b[1]];
export const v2sub = (a: V2, b: V2): V2 => [a[0] - b[0], a[1] - b[1]];
export const v2mul = (a: V2, s: number): V2 => [a[0] * s, a[1] * s];
export const v2dot = (a: V2, b: V2) => a[0] * b[0] + a[1] * b[1];
/** Plan-space cross (z component of the 3D cross). */
export const v2cross = (a: V2, b: V2) => a[0] * b[1] - a[1] * b[0];
export const v2len = (a: V2) => Math.hypot(a[0], a[1]);
export const v2dist = (a: V2, b: V2) => Math.hypot(a[0] - b[0], a[1] - b[1]);
export const v2dist2 = (a: V2, b: V2) => {
  const dx = a[0] - b[0];
  const dz = a[1] - b[1];
  return dx * dx + dz * dz;
};
export const v2norm = (a: V2): V2 => {
  const l = Math.hypot(a[0], a[1]);
  return l < EPS ? [1, 0] : [a[0] / l, a[1] / l];
};
export const v2neg = (a: V2): V2 => [-a[0], -a[1]];
/** Left-hand normal in XZ (rotate +90° about Y). */
export const v2left = (a: V2): V2 => [-a[1], a[0]];
export const v2right = (a: V2): V2 => [a[1], -a[0]];
export const v2rot = (a: V2, ang: number): V2 => {
  const c = Math.cos(ang);
  const s = Math.sin(ang);
  return [a[0] * c - a[1] * s, a[0] * s + a[1] * c];
};
export const v2lerp = (a: V2, b: V2, t: number): V2 => [lerp(a[0], b[0], t), lerp(a[1], b[1], t)];
/** Bearing in degrees, 0 = +Z (south/up-screen), clockwise positive. */
export const v2angle = (a: V2) => Math.atan2(a[0], a[1]);
export const angleDiff = (a: number, b: number) => {
  let d = (b - a) % TAU;
  if (d > Math.PI) d -= TAU;
  if (d < -Math.PI) d += TAU;
  return d;
};

// ---------------------------------------------------------------- vec3

export const v3 = (x: number, y: number, z: number): V3 => [x, y, z];
export const v3add = (a: V3, b: V3): V3 => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
export const v3sub = (a: V3, b: V3): V3 => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
export const v3mul = (a: V3, s: number): V3 => [a[0] * s, a[1] * s, a[2] * s];
export const v3dot = (a: V3, b: V3) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
export const v3cross = (a: V3, b: V3): V3 => [
  a[1] * b[2] - a[2] * b[1],
  a[2] * b[0] - a[0] * b[2],
  a[0] * b[1] - a[1] * b[0],
];
export const v3len = (a: V3) => Math.hypot(a[0], a[1], a[2]);
export const v3dist = (a: V3, b: V3) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
export const v3norm = (a: V3): V3 => {
  const l = v3len(a);
  return l < EPS ? [0, 1, 0] : [a[0] / l, a[1] / l, a[2] / l];
};
export const v3lerp = (a: V3, b: V3, t: number): V3 => [
  lerp(a[0], b[0], t),
  lerp(a[1], b[1], t),
  lerp(a[2], b[2], t),
];
export const v3neg = (a: V3): V3 => [-a[0], -a[1], -a[2]];
export const v3scale3 = (a: V3, b: V3): V3 => [a[0] * b[0], a[1] * b[1], a[2] * b[2]];

// ---------------------------------------------------------------- mat4 (column-major, WebGPU style)

export function m4identity(out: Mat4 = new Float32Array(16)): Mat4 {
  out.fill(0);
  out[0] = out[5] = out[10] = out[15] = 1;
  return out;
}

export function m4mul(a: Mat4, b: Mat4, out: Mat4 = new Float32Array(16)): Mat4 {
  for (let c = 0; c < 4; c++) {
    for (let r = 0; r < 4; r++) {
      let s = 0;
      for (let k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
      out[c * 4 + r] = s;
    }
  }
  return out;
}

export function m4fromTranslation(t: V3, out: Mat4 = new Float32Array(16)): Mat4 {
  m4identity(out);
  out[12] = t[0];
  out[13] = t[1];
  out[14] = t[2];
  return out;
}

export function m4fromTRS(t: V3, qy: number, s: number | V3, out: Mat4 = new Float32Array(16)): Mat4 {
  const c = Math.cos(qy);
  const sn = Math.sin(qy);
  const sx = typeof s === 'number' ? s : s[0];
  const sy = typeof s === 'number' ? s : s[1];
  const sz = typeof s === 'number' ? s : s[2];
  out.fill(0);
  out[0] = c * sx;
  out[1] = 0;
  out[2] = -sn * sx;
  out[3] = 0;
  out[4] = 0;
  out[5] = sy;
  out[6] = 0;
  out[7] = 0;
  out[8] = sn * sz;
  out[9] = 0;
  out[10] = c * sz;
  out[11] = 0;
  out[12] = t[0];
  out[13] = t[1];
  out[14] = t[2];
  out[15] = 1;
  return out;
}

/** Build a frame matrix from basis vectors (right=X, up=Y, forward=-Z convention used for props). */
export function m4fromBasis(right: V3, up: V3, fwd: V3, pos: V3, scale = 1, out: Mat4 = new Float32Array(16)): Mat4 {
  const b = v3neg(fwd);
  out[0] = right[0] * scale; out[1] = right[1] * scale; out[2] = right[2] * scale; out[3] = 0;
  out[4] = up[0] * scale; out[5] = up[1] * scale; out[6] = up[2] * scale; out[7] = 0;
  out[8] = b[0] * scale; out[9] = b[1] * scale; out[10] = b[2] * scale; out[11] = 0;
  out[12] = pos[0]; out[13] = pos[1]; out[14] = pos[2]; out[15] = 1;
  return out;
}

export function m4perspective(fovY: number, aspect: number, near: number, far: number, out: Mat4 = new Float32Array(16)): Mat4 {
  const f = 1 / Math.tan(fovY / 2);
  out.fill(0);
  out[0] = f / aspect;
  out[5] = f;
  out[11] = -1;
  const nf = 1 / (near - far);
  out[10] = far * nf;
  out[14] = far * near * nf;
  return out;
}

/** WebGPU NDC z in [0,1]. */
export function m4ortho(l: number, r: number, b: number, t: number, near: number, far: number, out: Mat4 = new Float32Array(16)): Mat4 {
  out.fill(0);
  out[0] = 2 / (r - l);
  out[5] = 2 / (t - b);
  out[10] = 1 / (near - far);
  out[12] = (r + l) / (l - r);
  out[13] = (t + b) / (b - t);
  out[14] = near / (near - far);
  out[15] = 1;
  return out;
}

export function m4lookAt(eye: V3, target: V3, up: V3, out: Mat4 = new Float32Array(16)): Mat4 {
  const z = v3norm(v3sub(eye, target));
  let x = v3cross(up, z);
  if (v3len(x) < 1e-6) x = v3cross([0, 0, 1], z);
  x = v3norm(x);
  const y = v3cross(z, x);
  out[0] = x[0]; out[1] = y[0]; out[2] = z[0]; out[3] = 0;
  out[4] = x[1]; out[5] = y[1]; out[6] = z[1]; out[7] = 0;
  out[8] = x[2]; out[9] = y[2]; out[10] = z[2]; out[11] = 0;
  out[12] = -v3dot(x, eye);
  out[13] = -v3dot(y, eye);
  out[14] = -v3dot(z, eye);
  out[15] = 1;
  return out;
}

export function m4invert(m: Mat4, out: Mat4 = new Float32Array(16)): Mat4 {
  const a00 = m[0], a01 = m[1], a02 = m[2], a03 = m[3];
  const a10 = m[4], a11 = m[5], a12 = m[6], a13 = m[7];
  const a20 = m[8], a21 = m[9], a22 = m[10], a23 = m[11];
  const a30 = m[12], a31 = m[13], a32 = m[14], a33 = m[15];
  const b00 = a00 * a11 - a01 * a10;
  const b01 = a00 * a12 - a02 * a10;
  const b02 = a00 * a13 - a03 * a10;
  const b03 = a01 * a12 - a02 * a11;
  const b04 = a01 * a13 - a03 * a11;
  const b05 = a02 * a13 - a03 * a12;
  const b06 = a20 * a31 - a21 * a30;
  const b07 = a20 * a32 - a22 * a30;
  const b08 = a20 * a33 - a23 * a30;
  const b09 = a21 * a32 - a22 * a31;
  const b10 = a21 * a33 - a23 * a31;
  const b11 = a22 * a33 - a23 * a32;
  let det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
  if (Math.abs(det) < 1e-12) return m4identity(out);
  det = 1 / det;
  out[0] = (a11 * b11 - a12 * b10 + a13 * b09) * det;
  out[1] = (a02 * b10 - a01 * b11 - a03 * b09) * det;
  out[2] = (a31 * b05 - a32 * b04 + a33 * b03) * det;
  out[3] = (a22 * b04 - a21 * b05 - a23 * b03) * det;
  out[4] = (a12 * b08 - a10 * b11 - a13 * b07) * det;
  out[5] = (a00 * b11 - a02 * b08 + a03 * b07) * det;
  out[6] = (a32 * b02 - a30 * b05 - a33 * b01) * det;
  out[7] = (a20 * b05 - a22 * b02 + a23 * b01) * det;
  out[8] = (a10 * b10 - a11 * b08 + a13 * b06) * det;
  out[9] = (a01 * b08 - a00 * b10 - a03 * b06) * det;
  out[10] = (a30 * b04 - a31 * b02 + a33 * b00) * det;
  out[11] = (a21 * b02 - a20 * b04 - a23 * b00) * det;
  out[12] = (a11 * b07 - a10 * b09 - a12 * b06) * det;
  out[13] = (a00 * b09 - a01 * b07 + a02 * b06) * det;
  out[14] = (a31 * b01 - a30 * b03 - a32 * b00) * det;
  out[15] = (a20 * b03 - a21 * b01 + a22 * b00) * det;
  return out;
}

export function m4transformPoint(m: Mat4, p: V3): V3 {
  const x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
  const y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
  const z = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
  const w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
  const iw = Math.abs(w) < 1e-12 ? 1 : 1 / w;
  return [x * iw, y * iw, z * iw];
}

export function m4transformDir(m: Mat4, p: V3): V3 {
  return [
    m[0] * p[0] + m[4] * p[1] + m[8] * p[2],
    m[1] * p[0] + m[5] * p[1] + m[9] * p[2],
    m[2] * p[0] + m[6] * p[1] + m[10] * p[2],
  ];
}

// ---------------------------------------------------------------- curves

/** Cubic Bézier point / derivative in any dimension-agnostic form (works for V2 and V3). */
export function cubicAt(p0: V2, p1: V2, p2: V2, p3: V2, t: number): V2;
export function cubicAt(p0: V3, p1: V3, p2: V3, p3: V3, t: number): V3;
export function cubicAt(p0: any, p1: any, p2: any, p3: any, t: number): any {
  const mt = 1 - t;
  const a = mt * mt * mt;
  const b = 3 * mt * mt * t;
  const c = 3 * mt * t * t;
  const d = t * t * t;
  const out = new Array(p0.length);
  for (let i = 0; i < p0.length; i++) out[i] = a * p0[i] + b * p1[i] + c * p2[i] + d * p3[i];
  return out;
}

export function cubicDeriv(p0: V3, p1: V3, p2: V3, p3: V3, t: number): V3 {
  const mt = 1 - t;
  const a = 3 * mt * mt;
  const b = 6 * mt * t;
  const c = 3 * t * t;
  return [
    a * (p1[0] - p0[0]) + b * (p2[0] - p1[0]) + c * (p3[0] - p2[0]),
    a * (p1[1] - p0[1]) + b * (p2[1] - p1[1]) + c * (p3[1] - p2[1]),
    a * (p1[2] - p0[2]) + b * (p2[2] - p1[2]) + c * (p3[2] - p2[2]),
  ];
}

export function cubicDeriv2(p0: V3, p1: V3, p2: V3, p3: V3, t: number): V3 {
  const mt = 1 - t;
  return [
    6 * mt * (p2[0] - 2 * p1[0] + p0[0]) + 6 * t * (p3[0] - 2 * p2[0] + p1[0]),
    6 * mt * (p2[1] - 2 * p1[1] + p0[1]) + 6 * t * (p3[1] - 2 * p2[1] + p1[1]),
    6 * mt * (p2[2] - 2 * p1[2] + p0[2]) + 6 * t * (p3[2] - 2 * p2[2] + p1[2]),
  ];
}

/** Split a cubic at t, returning the two sub-curves' control points. */
export function cubicSplit(p0: V3, p1: V3, p2: V3, p3: V3, t: number): [V3, V3, V3, V3, V3] {
  const a = v3lerp(p0, p1, t);
  const b = v3lerp(p1, p2, t);
  const c = v3lerp(p2, p3, t);
  const d = v3lerp(a, b, t);
  const e = v3lerp(b, c, t);
  const f = v3lerp(d, e, t);
  return [p0, a, d, f, e, c, p3] as unknown as [V3, V3, V3, V3, V3];
}

// ---------------------------------------------------------------- polygon helpers

export interface Seg2 { a: V2; b: V2 }

/** Intersect two infinite lines (point + direction). */
export function lineLine(p: V2, dp: V2, q: V2, dq: V2): { t: number; s: number; point: V2 } | null {
  const den = v2cross(dp, dq);
  if (Math.abs(den) < 1e-12) return null;
  const t = v2cross(v2sub(q, p), dq) / den;
  const s = v2cross(v2sub(p, q), dp) / -den;
  return { t, s, point: v2add(p, v2mul(dp, t)) };
}

/** Intersect two line segments. */
export function segSeg(a: Seg2, b: Seg2): { t: number; u: number; point: V2 } | null {
  const r = v2sub(a.b, a.a);
  const s = v2sub(b.b, b.a);
  const den = v2cross(r, s);
  if (Math.abs(den) < 1e-12) return null;
  const qp = v2sub(b.a, a.a);
  const t = v2cross(qp, s) / den;
  const u = v2cross(qp, r) / den;
  if (t < -1e-9 || t > 1 + 1e-9 || u < -1e-9 || u > 1 + 1e-9) return null;
  return { t, u, point: v2add(a.a, v2mul(r, t)) };
}

export function polygonArea(poly: V2[]): number {
  let a = 0;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    a += poly[j][0] * poly[i][1] - poly[i][0] * poly[j][1];
  }
  return a / 2;
}

export function polygonCentroid(poly: V2[]): V2 {
  let cx = 0, cz = 0, a = 0;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const f = poly[j][0] * poly[i][1] - poly[i][0] * poly[j][1];
    a += f;
    cx += (poly[j][0] + poly[i][0]) * f;
    cz += (poly[j][1] + poly[i][1]) * f;
  }
  a *= 0.5;
  if (Math.abs(a) < 1e-9) {
    let sx = 0, sz = 0;
    for (const p of poly) { sx += p[0]; sz += p[1]; }
    return [sx / poly.length, sz / poly.length];
  }
  return [cx / (6 * a), cz / (6 * a)];
}

export function pointInPolygon(p: V2, poly: V2[]): boolean {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const zi = poly[i][1], zj = poly[j][1];
    if (zi > p[1] !== zj > p[1] && p[0] < ((poly[j][0] - poly[i][0]) * (p[1] - zi)) / (zj - zi + EPS) + poly[i][0]) inside = !inside;
  }
  return inside;
}

/** Signed distance from point to a 2D segment. */
export function distToSeg(p: V2, a: V2, b: V2): number {
  const ab = v2sub(b, a);
  const l2 = v2dot(ab, ab);
  if (l2 < 1e-12) return v2dist(p, a);
  const t = clamp01(v2dot(v2sub(p, a), ab) / l2);
  return v2dist(p, v2add(a, v2mul(ab, t)));
}

/** Polyline (open) length in XZ. */
export function polylineLength(pts: V2[]): number {
  let l = 0;
  for (let i = 1; i < pts.length; i++) l += v2dist(pts[i - 1], pts[i]);
  return l;
}

/** Resample an open polyline at equal arc length into `count` points (inclusive of both ends). */
export function resamplePolyline(pts: V2[], count: number): V2[] {
  if (pts.length < 2 || count < 2) return pts.slice(0, Math.max(1, count));
  const lens: number[] = [0];
  for (let i = 1; i < pts.length; i++) lens.push(lens[i - 1] + v2dist(pts[i - 1], pts[i]));
  const total = lens[lens.length - 1];
  const out: V2[] = [];
  let seg = 0;
  for (let i = 0; i < count; i++) {
    const target = (total * i) / (count - 1);
    while (seg < lens.length - 2 && lens[seg + 1] < target) seg++;
    const t = invLerp(lens[seg], lens[seg + 1], target);
    out.push(v2lerp(pts[seg], pts[seg + 1], clamp01(t)));
  }
  return out;
}

/** Offset an open polyline to one side. Positive = left. Handles simple cases with round joins. */
export function offsetPolyline(pts: V2[], dist: number): V2[] {
  if (pts.length < 2) return pts.slice();
  const out: V2[] = [];
  for (let i = 0; i < pts.length; i++) {
    const prev = pts[Math.max(0, i - 1)];
    const next = pts[Math.min(pts.length - 1, i + 1)];
    const dirP = i > 0 ? v2norm(v2sub(pts[i], prev)) : v2norm(v2sub(next, pts[i]));
    const dirN = i < pts.length - 1 ? v2norm(v2sub(next, pts[i])) : dirP;
    const d = v2norm(v2add(dirP, dirN));
    if (v2len(d) < 1e-6) { out.push(pts[i]); continue; }
    const cosHalf = Math.max(0.2, v2dot(d, dirN));
    out.push(v2add(pts[i], v2mul(v2left(d), dist / cosHalf)));
  }
  return out;
}

// ---------------------------------------------------------------- misc

/** Deterministic RNG (mulberry32). */
export function makeRng(seed: number) {
  let a = seed >>> 0;
  return function rng() {
    a |= 0;
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export type Rng = () => number;

export const rngRange = (rng: Rng, a: number, b: number) => a + (b - a) * rng();
export const rngInt = (rng: Rng, a: number, b: number) => Math.floor(a + (b - a + 1) * rng());
export const rngPick = <T,>(rng: Rng, arr: T[]): T => arr[Math.min(arr.length - 1, Math.floor(rng() * arr.length))];

/** Approximate ASCII-safe formatting helpers used by UI + export. */
export function fmt(v: number, digits = 2): string {
  if (!Number.isFinite(v)) return '—';
  const s = v.toFixed(digits);
  return s.replace(/\.?0+$/, '') === '' ? '0' : s;
}
export const fmtM = (v: number) => `${fmt(v, v < 10 ? 2 : 1)} m`;

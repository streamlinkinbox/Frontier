// Minimal math library: vectors, quaternions, 4x4 matrices.
// Right-handed, column-major matrices (WebGL convention).

export interface V3 { x: number; y: number; z: number; }
export interface Quat { x: number; y: number; z: number; w: number; }

export const v3 = (x = 0, y = 0, z = 0): V3 => ({ x, y, z });
export const clone = (a: V3): V3 => ({ x: a.x, y: a.y, z: a.z });
export const add = (a: V3, b: V3): V3 => ({ x: a.x + b.x, y: a.y + b.y, z: a.z + b.z });
export const sub = (a: V3, b: V3): V3 => ({ x: a.x - b.x, y: a.y - b.y, z: a.z - b.z });
export const mul = (a: V3, s: number): V3 => ({ x: a.x * s, y: a.y * s, z: a.z * s });
export const mulv = (a: V3, b: V3): V3 => ({ x: a.x * b.x, y: a.y * b.y, z: a.z * b.z });
export const dot = (a: V3, b: V3): number => a.x * b.x + a.y * b.y + a.z * b.z;
export const cross = (a: V3, b: V3): V3 => ({
  x: a.y * b.z - a.z * b.y,
  y: a.z * b.x - a.x * b.z,
  z: a.x * b.y - a.y * b.x,
});
export const len = (a: V3): number => Math.sqrt(dot(a, a));
export const len2 = (a: V3): number => dot(a, a);
export const dist = (a: V3, b: V3): number => len(sub(a, b));
export const norm = (a: V3): V3 => {
  const l = len(a);
  return l > 1e-12 ? mul(a, 1 / l) : v3(0, 0, 0);
};
export const lerp3 = (a: V3, b: V3, t: number): V3 => ({
  x: a.x + (b.x - a.x) * t,
  y: a.y + (b.y - a.y) * t,
  z: a.z + (b.z - a.z) * t,
});
export const neg = (a: V3): V3 => ({ x: -a.x, y: -a.y, z: -a.z });
export const clamp = (x: number, a: number, b: number) => (x < a ? a : x > b ? b : x);
export const lerp = (a: number, b: number, t: number) => a + (b - a) * t;
export const smoothstep = (e0: number, e1: number, x: number) => {
  const t = clamp((x - e0) / (e1 - e0), 0, 1);
  return t * t * (3 - 2 * t);
};

/** Build an orthonormal basis around `n` (n assumed normalized). */
export function basis(n: V3): [V3, V3] {
  const a = Math.abs(n.x) > 0.9 ? v3(0, 1, 0) : v3(1, 0, 0);
  const t = norm(cross(a, n));
  const b = cross(n, t);
  return [t, b];
}

/** Rotate vector `v` about unit axis `k` by angle `a` (Rodrigues). */
export function rotAxis(v: V3, k: V3, a: number): V3 {
  const c = Math.cos(a), s = Math.sin(a);
  return add(add(mul(v, c), mul(cross(k, v), s)), mul(k, dot(k, v) * (1 - c)));
}

// ---------------------------------------------------------------- quaternions

export const quat = (x = 0, y = 0, z = 0, w = 1): Quat => ({ x, y, z, w });

export function qmul(a: Quat, b: Quat): Quat {
  return {
    w: a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    x: a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
    y: a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
    z: a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
  };
}

export function qnorm(q: Quat): Quat {
  const l = Math.hypot(q.x, q.y, q.z, q.w) || 1;
  return { x: q.x / l, y: q.y / l, z: q.z / l, w: q.w / l };
}

export function qFromAxis(axis: V3, angle: number): Quat {
  const h = angle * 0.5, s = Math.sin(h);
  const n = norm(axis);
  return { x: n.x * s, y: n.y * s, z: n.z * s, w: Math.cos(h) };
}

/** Integrate orientation by angular velocity w over dt. */
export function qIntegrate(q: Quat, w: V3, dt: number): Quat {
  const dq = qmul({ x: w.x * dt * 0.5, y: w.y * dt * 0.5, z: w.z * dt * 0.5, w: 0 }, q);
  return qnorm({ x: q.x + dq.x, y: q.y + dq.y, z: q.z + dq.z, w: q.w + dq.w });
}

export function qRotate(q: Quat, v: V3): V3 {
  // v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
  const u = v3(q.x, q.y, q.z);
  const t = cross(u, add(cross(u, v), mul(v, q.w)));
  return add(v, mul(t, 2));
}

export function qRotateInv(q: Quat, v: V3): V3 {
  return qRotate({ x: -q.x, y: -q.y, z: -q.z, w: q.w }, v);
}

/** 3x3 rotation matrix (column-major, 9 floats) from quaternion. */
export function qToMat3(q: Quat, out = new Float32Array(9)): Float32Array {
  const { x, y, z, w } = q;
  const x2 = x + x, y2 = y + y, z2 = z + z;
  const xx = x * x2, xy = x * y2, xz = x * z2;
  const yy = y * y2, yz = y * z2, zz = z * z2;
  const wx = w * x2, wy = w * y2, wz = w * z2;
  out[0] = 1 - (yy + zz); out[1] = xy + wz; out[2] = xz - wy;
  out[3] = xy - wz; out[4] = 1 - (xx + zz); out[5] = yz + wx;
  out[6] = xz + wy; out[7] = yz - wx; out[8] = 1 - (xx + yy);
  return out;
}

// ------------------------------------------------------------------ matrices

export type M4 = Float32Array;

export const m4 = (): M4 => new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);

export function m4mul(a: M4, b: M4, out: M4 = m4()): M4 {
  for (let c = 0; c < 4; c++) {
    const b0 = b[c * 4], b1 = b[c * 4 + 1], b2 = b[c * 4 + 2], b3 = b[c * 4 + 3];
    out[c * 4 + 0] = a[0] * b0 + a[4] * b1 + a[8] * b2 + a[12] * b3;
    out[c * 4 + 1] = a[1] * b0 + a[5] * b1 + a[9] * b2 + a[13] * b3;
    out[c * 4 + 2] = a[2] * b0 + a[6] * b1 + a[10] * b2 + a[14] * b3;
    out[c * 4 + 3] = a[3] * b0 + a[7] * b1 + a[11] * b2 + a[15] * b3;
  }
  return out;
}

export function m4Perspective(fovy: number, aspect: number, near: number, far: number): M4 {
  const f = 1 / Math.tan(fovy / 2);
  const o = m4();
  o[0] = f / aspect; o[5] = f; o[11] = -1; o[15] = 0;
  o[10] = (far + near) / (near - far);
  o[14] = (2 * far * near) / (near - far);
  return o;
}

export function m4Ortho(l: number, r: number, b: number, t: number, n: number, f: number): M4 {
  const o = m4();
  o[0] = 2 / (r - l); o[5] = 2 / (t - b); o[10] = -2 / (f - n);
  o[12] = -(r + l) / (r - l); o[13] = -(t + b) / (t - b); o[14] = -(f + n) / (f - n);
  return o;
}

export function m4LookAt(eye: V3, center: V3, up: V3): M4 {
  const z = norm(sub(eye, center));
  const x = norm(cross(up, z));
  const y = cross(z, x);
  const o = m4();
  o[0] = x.x; o[1] = y.x; o[2] = z.x; o[3] = 0;
  o[4] = x.y; o[5] = y.y; o[6] = z.y; o[7] = 0;
  o[8] = x.z; o[9] = y.z; o[10] = z.z; o[11] = 0;
  o[12] = -dot(x, eye); o[13] = -dot(y, eye); o[14] = -dot(z, eye); o[15] = 1;
  return o;
}

/** Compose translation + rotation (+ uniform scale) into a model matrix. */
export function m4Compose(p: V3, q: Quat, s = 1, out: M4 = m4()): M4 {
  const r = qToMat3(q);
  out[0] = r[0] * s; out[1] = r[1] * s; out[2] = r[2] * s; out[3] = 0;
  out[4] = r[3] * s; out[5] = r[4] * s; out[6] = r[5] * s; out[7] = 0;
  out[8] = r[6] * s; out[9] = r[7] * s; out[10] = r[8] * s; out[11] = 0;
  out[12] = p.x; out[13] = p.y; out[14] = p.z; out[15] = 1;
  return out;
}

export function m4Invert(m: M4, out: M4 = m4()): M4 {
  const a00 = m[0], a01 = m[1], a02 = m[2], a03 = m[3];
  const a10 = m[4], a11 = m[5], a12 = m[6], a13 = m[7];
  const a20 = m[8], a21 = m[9], a22 = m[10], a23 = m[11];
  const a30 = m[12], a31 = m[13], a32 = m[14], a33 = m[15];
  const b00 = a00 * a11 - a01 * a10, b01 = a00 * a12 - a02 * a10;
  const b02 = a00 * a13 - a03 * a10, b03 = a01 * a12 - a02 * a11;
  const b04 = a01 * a13 - a03 * a11, b05 = a02 * a13 - a03 * a12;
  const b06 = a20 * a31 - a21 * a30, b07 = a20 * a32 - a22 * a30;
  const b08 = a20 * a33 - a23 * a30, b09 = a21 * a32 - a22 * a31;
  const b10 = a21 * a33 - a23 * a31, b11 = a22 * a33 - a23 * a32;
  let det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
  if (!det) return out;
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

export function m4TransformPoint(m: M4, p: V3): V3 {
  const w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15] || 1;
  return {
    x: (m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12]) / w,
    y: (m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13]) / w,
    z: (m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]) / w,
  };
}

// --------------------------------------------------------------------- noise

/** Deterministic hash-based PRNG (mulberry32). */
export function rng(seed: number) {
  let a = seed >>> 0;
  return function () {
    a |= 0; a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function hash3(x: number, y: number, z: number): number {
  let h = Math.imul(x | 0, 374761393) ^ Math.imul(y | 0, 668265263) ^ Math.imul(z | 0, 2147483647);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}

const fade = (t: number) => t * t * t * (t * (t * 6 - 15) + 10);

/** Value noise in 3D, range [0,1]. Cheap and good enough for material grain. */
export function noise3(x: number, y: number, z: number): number {
  const xi = Math.floor(x), yi = Math.floor(y), zi = Math.floor(z);
  const xf = fade(x - xi), yf = fade(y - yi), zf = fade(z - zi);
  let r = 0;
  for (let k = 0; k < 2; k++)
    for (let j = 0; j < 2; j++)
      for (let i = 0; i < 2; i++) {
        const w = (i ? xf : 1 - xf) * (j ? yf : 1 - yf) * (k ? zf : 1 - zf);
        r += w * hash3(xi + i, yi + j, zi + k);
      }
  return r;
}

export function fbm3(x: number, y: number, z: number, octaves = 4): number {
  let s = 0, a = 0.5, f = 1, norm = 0;
  for (let i = 0; i < octaves; i++) {
    s += a * noise3(x * f, y * f, z * f);
    norm += a;
    f *= 2; a *= 0.5;
  }
  // Normalised so the mean is 0.5 — a biased fbm makes every crack spiral.
  return s / norm;
}

/** Gaussian sample (Box-Muller) from a uniform generator. */
export function gauss(rand: () => number): number {
  const u = Math.max(1e-9, rand());
  return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * rand());
}

/**
 * Weibull-distributed strength sample.
 * Brittle materials fail from the weakest flaw in the stressed volume;
 * the strength distribution is Weibull with modulus m (glass m~5, rock m~8-12).
 */
export function weibull(rand: () => number, scale: number, m: number): number {
  return scale * Math.pow(-Math.log(Math.max(1e-9, 1 - rand())), 1 / m);
}

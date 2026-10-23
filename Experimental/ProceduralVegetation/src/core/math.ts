/**
 * Minimal, allocation-conscious 3D math used by the generator and mesher.
 * Y is up throughout the project (matches glTF / three.js).
 */

export interface V3 {
  x: number;
  y: number;
  z: number;
}

export const DEG2RAD = Math.PI / 180;
export const RAD2DEG = 180 / Math.PI;
export const TAU = Math.PI * 2;

export const v3 = (x = 0, y = 0, z = 0): V3 => ({ x, y, z });
export const UP: Readonly<V3> = Object.freeze({ x: 0, y: 1, z: 0 });

export const clone = (a: V3): V3 => ({ x: a.x, y: a.y, z: a.z });
export const add = (a: V3, b: V3): V3 => ({ x: a.x + b.x, y: a.y + b.y, z: a.z + b.z });
export const sub = (a: V3, b: V3): V3 => ({ x: a.x - b.x, y: a.y - b.y, z: a.z - b.z });
export const scale = (a: V3, s: number): V3 => ({ x: a.x * s, y: a.y * s, z: a.z * s });
export const addScaled = (a: V3, b: V3, s: number): V3 => ({ x: a.x + b.x * s, y: a.y + b.y * s, z: a.z + b.z * s });
export const neg = (a: V3): V3 => ({ x: -a.x, y: -a.y, z: -a.z });
export const dot = (a: V3, b: V3): number => a.x * b.x + a.y * b.y + a.z * b.z;
export const cross = (a: V3, b: V3): V3 => ({
  x: a.y * b.z - a.z * b.y,
  y: a.z * b.x - a.x * b.z,
  z: a.x * b.y - a.y * b.x,
});
export const length = (a: V3): number => Math.sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
export const lengthSq = (a: V3): number => a.x * a.x + a.y * a.y + a.z * a.z;
export const distance = (a: V3, b: V3): number => length(sub(a, b));
export const lerp = (a: V3, b: V3, t: number): V3 => ({
  x: a.x + (b.x - a.x) * t,
  y: a.y + (b.y - a.y) * t,
  z: a.z + (b.z - a.z) * t,
});

export function normalize(a: V3): V3 {
  const l = length(a);
  if (l < 1e-12) return { x: 0, y: 1, z: 0 };
  return { x: a.x / l, y: a.y / l, z: a.z / l };
}

/** Rotate vector `v` around unit axis `k` by `angle` radians (Rodrigues, right handed). */
export function rotateAxis(v: V3, k: V3, angle: number): V3 {
  const c = Math.cos(angle);
  const s = Math.sin(angle);
  const kv = dot(k, v);
  const kxv = cross(k, v);
  return {
    x: v.x * c + kxv.x * s + k.x * kv * (1 - c),
    y: v.y * c + kxv.y * s + k.y * kv * (1 - c),
    z: v.z * c + kxv.z * s + k.z * kv * (1 - c),
  };
}

/** Any unit vector perpendicular to `d` (assumed unit). Deterministic. */
export function anyPerpendicular(d: V3): V3 {
  const ref = Math.abs(d.y) < 0.9 ? UP : { x: 1, y: 0, z: 0 };
  return normalize(cross(ref, d));
}

/** Remove the component of `v` along unit vector `d`. */
export function projectOnPlane(v: V3, d: V3): V3 {
  return addScaled(v, d, -dot(v, d));
}

/** Angle in radians between two vectors. */
export function angleBetween(a: V3, b: V3): number {
  const d = dot(a, b) / (length(a) * length(b) || 1);
  return Math.acos(Math.max(-1, Math.min(1, d)));
}

/**
 * Rotate unit vector `from` towards unit vector `to` by at most `maxAngle` radians.
 * Returns `to` if the angle between them is already smaller.
 */
export function rotateTowards(from: V3, to: V3, maxAngle: number): V3 {
  const ang = angleBetween(from, to);
  if (ang <= maxAngle) return clone(to);
  let axis = cross(from, to);
  if (lengthSq(axis) < 1e-12) axis = anyPerpendicular(from);
  return rotateAxis(from, normalize(axis), maxAngle);
}

export const clamp = (v: number, lo: number, hi: number): number => (v < lo ? lo : v > hi ? hi : v);
export const lerp1 = (a: number, b: number, t: number): number => a + (b - a) * t;

/** Wrap an angle to [-PI, PI). */
export function wrapAngle(a: number): number {
  a = (a + Math.PI) % TAU;
  if (a < 0) a += TAU;
  return a - Math.PI;
}

/** Positive modulo. */
export const mod = (a: number, n: number): number => ((a % n) + n) % n;

/** Declination from the vertical (Y) axis, in degrees. */
export function declinationDeg(d: V3): number {
  return Math.atan2(Math.sqrt(d.x * d.x + d.z * d.z), d.y) * RAD2DEG;
}

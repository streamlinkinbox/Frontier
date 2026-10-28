//============================================================================================================================================
//                                                                  VEC.JS
//============================================================================================================================================
// Minimal dependency-free vector maths. Every geometry module in RoadWorks works on plain {x,y,z} objects so the
// generators can be unit tested in Node without a WebGL context. Units are metres, Z is up.

export const vec = (x = 0, y = 0, z = 0) => ({ x, y, z });
export const clone = (a) => ({ x: a.x, y: a.y, z: a.z });
export const add = (a, b) => ({ x: a.x + b.x, y: a.y + b.y, z: a.z + b.z });
export const sub = (a, b) => ({ x: a.x - b.x, y: a.y - b.y, z: a.z - b.z });
export const mul = (a, s) => ({ x: a.x * s, y: a.y * s, z: a.z * s });
export const dot = (a, b) => a.x * b.x + a.y * b.y + a.z * b.z;
export const cross = (a, b) => ({
  x: a.y * b.z - a.z * b.y,
  y: a.z * b.x - a.x * b.z,
  z: a.x * b.y - a.y * b.x,
});
export const len = (a) => Math.hypot(a.x, a.y, a.z);
export const lenXY = (a) => Math.hypot(a.x, a.y);
export const dist = (a, b) => Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
export const distXY = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);

export function norm(a, fallback = { x: 1, y: 0, z: 0 }) {
  const l = len(a);
  if (l <= 1e-9) return clone(fallback);
  return { x: a.x / l, y: a.y / l, z: a.z / l };
}

export function normXY(a, fallback = { x: 1, y: 0, z: 0 }) {
  const l = Math.hypot(a.x, a.y);
  if (l <= 1e-9) return clone(fallback);
  return { x: a.x / l, y: a.y / l, z: 0 };
}

export const lerp = (a, b, t) => ({
  x: a.x + (b.x - a.x) * t,
  y: a.y + (b.y - a.y) * t,
  z: a.z + (b.z - a.z) * t,
});

export const scalarLerp = (a, b, t) => a + (b - a) * t;
export const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
export const smoothstep = (t) => t * t * (3 - 2 * t);

// Left-hand normal of a planar direction (rotate +90° about Z).
export const leftOf = (dir) => ({ x: -dir.y, y: dir.x, z: 0 });

export const addScaled = (p, dir, s) => ({ x: p.x + dir.x * s, y: p.y + dir.y * s, z: p.z + dir.z * s });

export function wrapAngle(a) {
  while (a > Math.PI) a -= Math.PI * 2;
  while (a < -Math.PI) a += Math.PI * 2;
  return a;
}

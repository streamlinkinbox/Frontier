import type { Vec3 } from "./types";
export const clamp = (n: number, a: number, b: number) =>
  Math.max(a, Math.min(b, n));
export const add = (a: Vec3, b: Vec3): Vec3 => [
  a[0] + b[0],
  a[1] + b[1],
  a[2] + b[2],
];
export const sub = (a: Vec3, b: Vec3): Vec3 => [
  a[0] - b[0],
  a[1] - b[1],
  a[2] - b[2],
];
export const scale = (a: Vec3, n: number): Vec3 => [
  a[0] * n,
  a[1] * n,
  a[2] * n,
];
export const dot = (a: Vec3, b: Vec3) =>
  a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
export const cross = (a: Vec3, b: Vec3): Vec3 => [
  a[1] * b[2] - a[2] * b[1],
  a[2] * b[0] - a[0] * b[2],
  a[0] * b[1] - a[1] * b[0],
];
export const normalize = (a: Vec3): Vec3 =>
  scale(a, 1 / (Math.hypot(...a) || 1));
export const mix = (a: number, b: number, t: number) => a + (b - a) * t;
export const smoothstep = (a: number, b: number, x: number) => {
  const t = clamp((x - a) / (b - a), 0, 1);
  return t * t * (3 - 2 * t);
};
export function rayBox(
  ro: Vec3,
  rd: Vec3,
  lo: Vec3,
  hi: Vec3,
): [number, number] | null {
  let near = -Infinity,
    far = Infinity;
  for (let i = 0; i < 3; i++) {
    if (Math.abs(rd[i]) < 1e-8) {
      if (ro[i] < lo[i] || ro[i] > hi[i]) return null;
      continue;
    }
    const a = (lo[i] - ro[i]) / rd[i],
      b = (hi[i] - ro[i]) / rd[i];
    near = Math.max(near, Math.min(a, b));
    far = Math.min(far, Math.max(a, b));
  }
  return near <= far && far >= 0 ? [Math.max(0, near), far] : null;
}
export function floatToHalf(value: number): number {
  floatView[0] = value;
  const bits = intView[0];
  const s = (bits >> 16) & 0x8000;
  let e = ((bits >> 23) & 255) - 127 + 15;
  let m = bits & 0x7fffff;
  if (e <= 0) {
    if (e < -10) return s;
    m = (m | 0x800000) >> (1 - e);
    return s + ((m + 0x1000) >> 13);
  }
  if (e >= 31) return s | 0x7bff;
  m += 0x1000;
  if (m & 0x800000) {
    m = 0;
    e++;
  }
  return s | (e << 10) | (m >> 13);
}
const floatView = new Float32Array(1),
  intView = new Uint32Array(floatView.buffer);
export function halfToFloat(h: number): number {
  const s = h & 0x8000 ? -1 : 1,
    e = (h >> 10) & 31,
    m = h & 1023;
  return e === 0
    ? s * 2 ** -14 * (m / 1024)
    : e === 31
      ? m
        ? NaN
        : s * Infinity
      : s * 2 ** (e - 15) * (1 + m / 1024);
}

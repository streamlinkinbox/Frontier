import { clamp, type StoneSettings } from "./model";
export type Point = [number, number, number];
export function stoneHash(
  x: number,
  y: number,
  z: number,
  seed: number,
): number {
  let a =
    (Math.imul(x, 73856093) ^
      Math.imul(y, 19349663) ^
      Math.imul(z, 83492791) ^
      Math.imul(seed, 2654435761)) >>>
    0;
  a = Math.imul(a ^ (a >>> 13), 1274126177) >>> 0;
  return ((a ^ (a >>> 16)) >>> 0) / 4294967295;
}
export function stoneNoise(p: Point, seed: number): number {
  const i = p.map(Math.floor),
    f = p.map((v, k) => v - i[k]),
    w = f.map((t) => t * t * (3 - 2 * t));
  let v = 0;
  for (let z = 0; z < 2; z++)
    for (let y = 0; y < 2; y++)
      for (let x = 0; x < 2; x++)
        v +=
          stoneHash(i[0] + x, i[1] + y, i[2] + z, seed) *
          (x ? w[0] : 1 - w[0]) *
          (y ? w[1] : 1 - w[1]) *
          (z ? w[2] : 1 - w[2]);
  return v * 2 - 1;
}
export const mul = (p: Point, k: number): Point => [
  p[0] * k,
  p[1] * k,
  p[2] * k,
];
export const shift = (p: Point, x: number, y: number, z: number): Point => [
  p[0] + x,
  p[1] + y,
  p[2] + z,
];
const smoothMax = (a: number, b: number, k: number) => {
  const h = clamp(0.5 + (0.5 * (a - b)) / k, 0, 1);
  return b * (1 - h) + a * h + k * h * (1 - h);
};
export function stoneBase(p: Point, s: StoneSettings): number {
  let d = (Math.hypot(p[0] / 0.3, p[1] / 0.245, p[2] / 0.27) - 1) * 0.245;
  const k = 0.004 + 0.025 * (1 - s.facets),
    cut = 0.286 - s.facets * 0.082;
  for (const n of [
    [0.78, 0.2, 0.59],
    [-0.8, 0.46, 0.39],
    [0.1, 0.82, -0.56],
    [-0.2, -0.68, -0.7],
  ]) {
    const l = Math.hypot(...n);
    d = smoothMax(d, (p[0] * n[0] + p[1] * n[1] + p[2] * n[2]) / l - cut, k);
  }
  d +=
    s.form *
    (0.016 * stoneNoise(mul(p, 4), s.seed) +
      0.007 * stoneNoise(shift(mul(p, 11), 13, 7, 3), s.seed));
  return Math.max(d, -p[1] - 0.235);
}

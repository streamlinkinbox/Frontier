import { clamp, detailWeight, type StoneSettings } from "./model";
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
const mul = (p: Point, k: number): Point => [p[0] * k, p[1] * k, p[2] * k];
const shift = (p: Point, x: number, y: number, z: number): Point => [
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
/** Same bounded implicit field as stone.glsl. Detail changes the zero surface;
 * no image height/normal data is sampled. Values are distance estimators, not
 * guaranteed exact Euclidean distances after displacement/CSG. Units: meters. */
export function stoneField(p: Point, s: StoneSettings, footprint = 0): number {
  const base = stoneBase(p, s);
  if (!s.detail) return base;
  const q: Point = [p[0] * 0.8 + p[2] * 0.6, p[1], -p[0] * 0.6 + p[2] * 0.8];
  const n = (f: number, o: Point = [0, 0, 0]) =>
    stoneNoise(shift(mul(q, f), ...o), s.seed);
  let d = base;
  d +=
    s.chips *
    0.001 *
    detailWeight(footprint, 0.018) *
    (0.6 * n(22) +
      0.28 * n(47, [11, 3, 5]) +
      0.12 * Math.abs(n(93, [3, 19, 7])));
  if (s.bedding > 0) {
    const a = (s.tilt * Math.PI) / 180,
      level =
        (p[1] * Math.cos(a) + p[0] * Math.sin(a)) / (s.spacing * 0.001) +
        stoneNoise(mul(p, 8), s.seed) * 0.32 +
        stoneNoise(shift(mul(p, 21), 3, 7, 1), s.seed) * 0.075;
    const phase = level * 6.2831853;
    const t = clamp(
        (stoneNoise(shift(mul(p, 13), 11, 2, 7), s.seed) + 0.3) / 0.8,
        0,
        1,
      ),
      mask = 0.12 + 0.88 * t * t * (3 - 2 * t);
    d +=
      s.bedding *
      0.001 *
      detailWeight(footprint, s.spacing * 0.0005) *
      mask *
      (0.55 * Math.sin(phase) + 0.18 * Math.sin(phase * 2));
  }
  if (s.grain > 0) {
    const f = 1000 / s.grainSize;
    d +=
      s.grain *
      0.001 *
      (detailWeight(footprint, s.grainSize * 0.001) * n(f, [4, 7, 1]) +
        0.35 *
          detailWeight(footprint, s.grainSize * 0.0005) *
          n(f * 2, [17, 3, 13]));
  }
  if (s.crackDepth > 0) {
    const spacing = s.crackSpacing * 0.001,
      width =
        s.crackWidth * 0.001 * detailWeight(footprint, s.crackWidth * 0.004);
    const u =
      (p[0] * 0.8 + p[2] * 0.6 + stoneNoise(mul(p, 13), s.seed) * 0.003) /
      spacing;
    const v =
      (p[0] * -0.28 +
        p[1] * 0.92 +
        p[2] * 0.27 +
        stoneNoise(shift(mul(p, 11), 3, 8, 5), s.seed) * 0.003) /
      (spacing * 1.37);
    const line = Math.min(
      Math.abs(u - Math.floor(u + 0.5)) * spacing,
      Math.abs(v - Math.floor(v + 0.5)) * spacing * 1.37,
    );
    const gap =
      stoneNoise(shift(mul(p, 15), 7, 3, 11), s.seed) * 0.003 - 0.0008;
    const slot = Math.max(line - width, gap, -base - s.crackDepth * 0.001);
    d = Math.max(d, -slot);
  }
  const size = s.poreSize * 0.001,
    rScale = detailWeight(footprint, size * 0.3);
  if (s.porosity > 0 && rScale > 0 && d < size * 0.21) {
    const cell = p.map((v) => Math.floor(v / size));
    let pores = 10;
    for (let z = 0; z < 2; z++)
      for (let y = 0; y < 2; y++)
        for (let x = 0; x < 2; x++) {
          const a = cell[0] + x,
            b = cell[1] + y,
            c = cell[2] + z;
          if (stoneHash(a, b, c, s.seed + 31) > s.porosity) continue;
          const random = stoneHash(a, b, c, s.seed + 53);
          const cx =
              (a + (stoneHash(a, b, c, s.seed + 71) - 0.5) * 0.12) * size,
            cy = (b + (stoneHash(a, b, c, s.seed + 97) - 0.5) * 0.12) * size,
            cz = (c + (stoneHash(a, b, c, s.seed + 113) - 0.5) * 0.12) * size;
          pores = Math.min(
            pores,
            Math.hypot(p[0] - cx, p[1] - cy, p[2] - cz) -
              size * (0.1 + 0.11 * random) * rScale,
          );
        }
    d = Math.max(d, -pores);
  }
  return Math.max(d, -p[1] - 0.235);
}
export function stoneNormal(p: Point, s: StoneSettings, footprint = 0): Point {
  const e = Math.max(0.000025, Math.min(0.0007, footprint * 0.22));
  const g = [0, 1, 2].map((axis) => {
    const a = [...p] as Point,
      b = [...p] as Point;
    a[axis] += e;
    b[axis] -= e;
    return stoneField(a, s, footprint) - stoneField(b, s, footprint);
  });
  const length = Math.hypot(...g) || 1;
  return g.map((v) => v / length) as Point;
}

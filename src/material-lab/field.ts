import { detailWeight, type StoneSettings } from "./model";
import { stoneBase, stoneNoise, mul, shift, type Point } from "./base";
import { fractureDistance, maxFractureMouth } from "./fractures";
import { beddingDisplacement, poreVoidDistance } from "./structure";
export { stoneBase, stoneHash, stoneNoise } from "./base";
export type { Point } from "./base";
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
  d += beddingDisplacement(p, s, footprint);
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
  if (s.crackDepth > 0 && d < maxFractureMouth(s, footprint))
    d = Math.max(d, -fractureDistance(p, d, s, footprint));
  if (s.porosity > 0 && d < s.poreSize * 0.00032)
    d = Math.max(d, -poreVoidDistance(p, s, footprint));
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

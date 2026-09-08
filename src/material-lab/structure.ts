import { clamp, detailWeight, type StoneSettings } from "./model";
import { stoneHash, stoneNoise, mul, shift, type Point } from "./base";
const mix = (a: number, b: number, t: number) => a * (1 - t) + b * t;
const smoothMin = (a: number, b: number, k: number) => {
  if (k <= 0) return Math.min(a, b);
  const h = clamp(0.5 + (0.5 * (b - a)) / k, 0, 1);
  return mix(b, a, h) - k * h * (1 - h);
};

/** Nonuniform ordered boundaries: spacing ranges from .52 to 1.48 mean units. */
export const bedBoundary = (i: number, s: StoneSettings) =>
  i + (stoneHash(i, 613, 73, s.seed) - 0.5) * 0.48;
function bedPlate(
  i: number,
  u: number,
  v: number,
  s: StoneSettings,
): [number, number] {
  const frequency = 1000 / (s.spacing * 4.2);
  const low = stoneNoise([u * frequency, v * frequency, i * 2.713 + 5], s.seed);
  const high = stoneNoise(
    [u * frequency * 2.13 + 9, v * frequency * 2.13 + 4, i * 1.379 + 11],
    s.seed,
  );
  const spall = clamp((high + 0.05) / 0.38, 0, 1);
  const h =
    (stoneHash(i, 619, 97, s.seed) - 0.5) * 0.7 +
    s.layerBreakup * (0.28 * low + 0.36 * spall);
  return [h, spall];
}
/** Piecewise sheet offsets with narrow blended joins, per-sheet spalling and
 * discontinuous-looking (but continuous-field) ledges. Not a sinusoidal rib. */
export function beddingDisplacement(
  p: Point,
  s: StoneSettings,
  fp: number,
): number {
  if (s.bedding <= 0) return 0;
  const angle = (s.tilt * Math.PI) / 180,
    c = Math.cos(angle),
    sn = Math.sin(angle);
  const level =
    (p[1] * c + p[0] * sn) / (s.spacing * 0.001) +
    stoneNoise(mul(p, 7), s.seed) * 0.35 +
    stoneNoise(shift(mul(p, 17), 3, 7, 1), s.seed) * 0.09;
  const u = p[0] * c - p[1] * sn,
    v = p[2];
  let layer = Math.floor(level);
  if (level < bedBoundary(layer, s)) layer--;
  else if (level >= bedBoundary(layer + 1, s)) layer++;
  const lo = bedBoundary(layer, s),
    hi = bedBoundary(layer + 1, s),
    bottom = level - lo,
    top = hi - level;
  let [height, spall] = bedPlate(layer, u, v, s);
  const lowerWidth = 0.06 + stoneHash(layer, 631, 101, s.seed) * 0.07,
    upperWidth = 0.06 + stoneHash(layer + 1, 631, 101, s.seed) * 0.07;
  if (bottom < lowerWidth) {
    const previous = bedPlate(layer - 1, u, v, s),
      w = clamp((bottom + lowerWidth) / (2 * lowerWidth), 0, 1);
    height = mix(previous[0], height, w);
    spall = mix(previous[1], spall, w);
  } else if (top < upperWidth) {
    const next = bedPlate(layer + 1, u, v, s),
      w = clamp((-top + upperWidth) / (2 * upperWidth), 0, 1);
    height = mix(height, next[0], w);
    spall = mix(spall, next[1], w);
  }
  const torn =
    Math.max(0, 1 - Math.min(bottom, top) / 0.22) *
    spall *
    s.layerBreakup *
    0.18;
  return (
    s.bedding * 0.001 * detailWeight(fp, s.spacing * 0.0005) * (height + torn)
  );
}

/** Rotated, anisotropic, joined and gently dented vesicle; irregularity=0 is the
 * original spherical primitive. Radius/centers/population remain seed stable. */
export function poreShape(
  local: Point,
  r: number,
  axes: Point,
  angles: [number, number],
  irregularity: number,
  wallWeight = 1,
): number {
  if (irregularity <= 0) return Math.hypot(...local) - r;
  const ct = Math.cos(angles[0]),
    st = Math.sin(angles[0]),
    cp = Math.cos(angles[1]),
    sp = Math.sin(angles[1]);
  const a: Point = [
    local[0] * ct - local[1] * st,
    local[0] * st + local[1] * ct,
    local[2],
  ];
  const q: Point = [a[0] * cp + a[2] * sp, a[1], -a[0] * sp + a[2] * cp];
  const shape = axes.map((v) => mix(1, v, irregularity)) as Point,
    minAxis = Math.min(...shape);
  const v = q.map((x, i) => x / shape[i]) as Point;
  const l2 = Math.hypot(...v),
    l4 = (v[0] ** 4 + v[1] ** 4 + v[2] ** 4) ** 0.25;
  const main = (mix(l2, l4, irregularity * 0.22) - r) * minAxis;
  const lobe = [
    (q[0] - r * 0.45 * irregularity) / shape[0],
    (q[1] - r * 0.18 * irregularity) / shape[1],
    (q[2] + r * 0.12 * irregularity) / shape[2],
  ];
  const secondary = (Math.hypot(...lobe) - r * 0.68) * minAxis;
  const wall =
    r *
    irregularity *
    0.055 *
    wallWeight *
    Math.sin((v[0] / r) * 6 + 0.3) *
    Math.sin((v[1] / r) * 5 + 0.2) *
    Math.sin((v[2] / r) * 4 + 1.1);
  return smoothMin(main, secondary, r * 0.18 * irregularity) + wall;
}
export function poreVoidDistance(
  p: Point,
  s: StoneSettings,
  fp: number,
): number {
  const size = s.poreSize * 0.001,
    visible = detailWeight(fp, size * 0.3);
  if (s.porosity <= 0 || visible <= 0) return 10;
  const cell = p.map((v) => Math.floor(v / size));
  let pores = 10;
  for (let z = 0; z < 2; z++)
    for (let y = 0; y < 2; y++)
      for (let x = 0; x < 2; x++) {
        const a = cell[0] + x,
          b = cell[1] + y,
          c = cell[2] + z;
        const hash = (offset: number) => stoneHash(a, b, c, s.seed + offset);
        if (hash(31) > s.porosity) continue;
        const r = size * (0.1 + 0.11 * hash(53)) * visible;
        const center: Point = [
          (a + (hash(71) - 0.5) * 0.12) * size,
          (b + (hash(97) - 0.5) * 0.12) * size,
          (c + (hash(113) - 0.5) * 0.12) * size,
        ];
        const local = p.map((v, i) => v - center[i]) as Point;
        if (Math.hypot(...local) > r * 1.6 + 0.002) {
          pores = Math.min(pores, Math.hypot(...local) - r * 1.35);
          continue;
        }
        pores = Math.min(
          pores,
          poreShape(
            local,
            r,
            [0.45 + 0.5 * hash(173), 0.58 + 0.38 * hash(191), 1],
            [hash(211) * 6.2831853, hash(229) * 6.2831853],
            s.poreIrregularity,
            detailWeight(fp, r * 0.9),
          ),
        );
      }
  return pores;
}

import {
  clamp,
  detailEnvelope,
  detailWeight,
  type StoneSettings,
} from "./model";
import {
  stoneBase,
  stoneHash,
  stoneNoise,
  mul,
  shift,
  type Point,
} from "./base";

export const MAX_FRACTURE_SEGMENTS = 56;
export interface FractureSegment {
  a: Point;
  b: Point;
  normal: Point;
  widthA: number;
  widthB: number;
  depth: number;
  parent: number;
  branch: boolean;
}
export interface FractureNetwork {
  key: string;
  segments: readonly FractureSegment[];
  a: Float32Array;
  b: Float32Array;
  normals: Float32Array;
}
const add = (a: Point, b: Point): Point => [
  a[0] + b[0],
  a[1] + b[1],
  a[2] + b[2],
];
const sub = (a: Point, b: Point): Point => [
  a[0] - b[0],
  a[1] - b[1],
  a[2] - b[2],
];
const dot = (a: Point, b: Point) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const norm = (a: Point): Point => mul(a, 1 / Math.max(1e-12, Math.hypot(...a)));
const cross = (a: Point, b: Point): Point => [
  a[1] * b[2] - a[2] * b[1],
  a[2] * b[0] - a[0] * b[2],
  a[0] * b[1] - a[1] * b[0],
];
const round = (a: Point): Point => a.map(Math.fround) as Point;
/** Stable outward radial projection onto the organic BASE. Never depends on
 * color, camera, surface-detail values or render resolution. */
export function projectStone(direction: Point, s: StoneSettings): Point {
  const n = norm(direction);
  let lo = 0,
    hi = 0.46;
  for (let i = 0; i < 24; i++) {
    const r = (lo + hi) * 0.5;
    if (stoneBase(mul(n, r), s) > 0) hi = r;
    else lo = r;
  }
  return round(mul(n, (lo + hi) * 0.5));
}
function baseNormal(p: Point, s: StoneSettings): Point {
  const e = 0.0003;
  return norm([
    stoneBase(shift(p, e, 0, 0), s) - stoneBase(shift(p, -e, 0, 0), s),
    stoneBase(shift(p, 0, e, 0), s) - stoneBase(shift(p, 0, -e, 0), s),
    stoneBase(shift(p, 0, 0, e), s) - stoneBase(shift(p, 0, 0, -e), s),
  ]);
}
const cache = new Map<string, FractureNetwork>();
export function getFractureNetwork(s: StoneSettings): FractureNetwork {
  const key = [s.seed, s.form, s.facets, s.crackSpacing, s.crackBranching].join(
    ":",
  );
  const cached = cache.get(key);
  if (cached) return cached;
  const segments: FractureSegment[] = [];
  const random = (id: number, channel: number) =>
    stoneHash(id, channel, 381, s.seed + 701);
  const emit = (
    a: Point,
    b: Point,
    wa: number,
    wb: number,
    parent: number,
    branch: boolean,
  ) => {
    if (segments.length >= MAX_FRACTURE_SEGMENTS) return;
    const tangent = norm(sub(b, a));
    let n = norm(add(baseNormal(a, s), baseNormal(b, s)));
    n = norm(sub(n, mul(tangent, dot(n, tangent))));
    segments.push({
      a: round(a),
      b: round(b),
      normal: round(n),
      widthA: Math.fround(wa),
      widthB: Math.fround(wb),
      depth: Math.fround(0.7 + random(segments.length, 33) * 0.35),
      parent,
      branch,
    });
  };
  const roots = clamp(Math.round(450 / s.crackSpacing), 3, 4);
  for (let root = 0; root < roots; root++) {
    const angle = 0.62 + root * 2.399963 + (random(root, 1) - 0.5) * 0.6;
    const outward = norm([
      Math.sin(angle),
      0.14 + random(root, 2) * 0.58,
      Math.cos(angle),
    ]);
    const east = norm(cross([0, 1, 0], outward)),
      north = cross(outward, east);
    const turn = (random(root, 3) - 0.5) * 2.3;
    const tangent = add(mul(east, Math.cos(turn)), mul(north, Math.sin(turn))),
      side = cross(outward, tangent);
    const reach =
      (0.53 + random(root, 4) * 0.26) * clamp(110 / s.crackSpacing, 0.65, 1.35);
    const points: Point[] = [],
      weights = Array.from(
        { length: 11 },
        (_, k) =>
          (0.035 +
            0.965 * Math.pow(Math.max(0, Math.sin((k / 10) * Math.PI)), 0.7)) *
          (0.78 + random(root, 7) * 0.35),
      );
    const phase = random(root, 8) * 6.2831853;
    for (let k = 0; k < 11; k++) {
      const u = (k - 5) / 5;
      const wobble =
        Math.sin(k * 0.47 + phase) * 0.065 +
        (random(root * 13 + k, 9) - 0.5) * 0.045;
      points.push(
        projectStone(
          add(add(outward, mul(tangent, u * reach)), mul(side, wobble)),
          s,
        ),
      );
    }
    for (let k = 0; k < 10; k++)
      emit(points[k], points[k + 1], weights[k], weights[k + 1], root, false);
    // Every fork starts at an EXISTING root vertex: real connected branching,
    // not a second unrelated set of dashed/periodic planes.
    if (s.crackBranching > 0)
      for (let fork = 0; fork < 2; fork++) {
        const node = fork * 4 + 3,
          start = points[node],
          normal = baseNormal(start, s);
        let along = norm(sub(points[node + 1], points[node]));
        along = norm(sub(along, mul(normal, dot(along, normal))));
        const across = cross(normal, along),
          bend =
            (0.52 + random(root * 3 + fork, 19) * 0.5) * (fork === 0 ? 1 : -1);
        const travel = add(
          mul(along, Math.cos(bend)),
          mul(across, Math.sin(bend)),
        );
        const radius = 0.22 + random(root * 3 + fork, 21) * 0.18;
        const mid = projectStone(
          add(norm(start), mul(travel, radius * 0.6)),
          s,
        );
        const end = projectStone(
          add(
            add(norm(start), mul(travel, radius)),
            mul(across, (random(root, 24) - 0.5) * 0.11),
          ),
          s,
        );
        const w =
          weights[node] *
          s.crackBranching *
          (0.38 + random(root * 3 + fork, 27) * 0.2);
        emit(start, mid, w, w * 0.55, root, true);
        emit(mid, end, w * 0.55, w * 0.035, root, true);
      }
  }
  const a = new Float32Array(MAX_FRACTURE_SEGMENTS * 4),
    b = new Float32Array(a.length),
    normals = new Float32Array(a.length);
  segments.forEach((s, i) => {
    a.set([...s.a, s.widthA], i * 4);
    b.set([...s.b, s.widthB], i * 4);
    normals.set([...s.normal, s.depth], i * 4);
  });
  const network = { key, segments, a, b, normals };
  if (cache.size >= 12) cache.delete(cache.keys().next().value!);
  cache.set(key, network);
  return network;
}
export function maxFractureMouth(s: StoneSettings, fp: number): number {
  return (
    s.crackWidth *
    0.001 *
    detailWeight(fp, s.crackWidth * 0.006) *
    (1 + 3.8 * s.crackChipping) *
    1.2
  );
}
/** Finite surface-following slit graph with tapered tips, V-shaped depth profile,
 * ragged, flared shoulders and a shell/local-support gate. The return value is
 * the signed distance ESTIMATOR of the void, for CSG subtraction. */
export function fractureDistance(
  p: Point,
  substrate: number,
  s: StoneSettings,
  fp: number,
): number {
  const width = s.crackWidth * 0.001 * detailWeight(fp, s.crackWidth * 0.006);
  if (s.crackDepth <= 0 || width <= 0) return 10;
  let noise: number | null = null;
  const envelope = detailEnvelope(s, fp),
    rockDepth = Math.max(0, -substrate),
    mouth = maxFractureMouth(s, fp);
  let cut = 10;
  for (const segment of getFractureNetwork(s).segments) {
    const delta = sub(segment.b, segment.a),
      depth = s.crackDepth * 0.001 * segment.depth;
    const support = depth + envelope + 0.01;
    const mid = mul(add(segment.a, segment.b), 0.5);
    if (
      delta.some(
        (v, i) =>
          Math.abs(p[i] - mid[i]) - Math.abs(v) * 0.5 > support + mouth * 1.1,
      )
    )
      continue;
    const len = Math.hypot(...delta),
      dir = mul(delta, 1 / len);
    if (noise === null)
      noise =
        0.55 * stoneNoise(shift(mul(p, 80), 11, 7, 3), s.seed) +
        0.2 * stoneNoise(shift(mul(p, 170), 3, 13, 7), s.seed);
    const rel = sub(p, segment.a),
      unclamped = dot(rel, dir),
      t = clamp(unclamped / len, 0, 1),
      center = add(segment.a, mul(delta, t));
    const q = sub(p, center),
      taper = segment.widthA * (1 - t) + segment.widthB * t;
    const nominal = width * taper,
      penetration = clamp(1 - rockDepth / Math.max(depth, 0.00001), 0, 1);
    const shoulder = clamp(
      1 - rockDepth / Math.max(depth * 0.8, width * 6),
      0,
      1,
    );
    const halfWidth =
      nominal *
      (0.2 +
        0.8 * penetration +
        s.crackChipping *
          (0.6 + 3.2 * clamp((0.6 - Math.abs(noise + 0.15)) * 2, 0, 1)) *
          shoulder *
          shoulder);
    const across =
      dot(q, cross(segment.normal, dir)) +
      noise * nominal * s.crackChipping * 0.45;
    const beyond = unclamped - clamp(unclamped, 0, len);
    const slit = Math.hypot(across, beyond) - halfWidth;
    cut = Math.min(
      cut,
      Math.max(
        slit,
        -substrate - depth,
        Math.abs(dot(q, segment.normal)) - support,
      ),
    );
  }
  return cut;
}

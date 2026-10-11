/**
 * Path primitives: line / cubic-Bézier / circular-arc pieces chained into a
 * corridor path with exact arc-length parameterisation, tangents and trimming.
 *
 * Roads are authored as node-to-node cubic splines; every derived geometry
 * (carriageway grid, kerb runs, corner fillets, junction mouths, guardrails,
 * lane centrelines, rail tracks) is a trimmed sub-path of a corridor path, so
 * they can never drift apart.
 */

import {
  clamp,
  cubicAt,
  cubicDeriv,
  cubicDeriv2,
  lerp,
  TAU,
  v2add,
  v2cross,
  v2dist,
  v2dot,
  v2mul,
  v2norm,
  v2sub,
  v2rot,
  type V2,
  type V3,
} from './math';

export interface LinePiece {
  kind: 'line';
  a: V3;
  b: V3;
}
export interface CubicPiece {
  kind: 'cubic';
  p0: V3;
  p1: V3;
  p2: V3;
  p3: V3;
}
export interface ArcPiece {
  kind: 'arc';
  centre: V3;
  radius: number;
  /** start angle in the XZ plane, radians, measured as atan2(dz, dx) */
  startAngle: number;
  sweep: number;
  /** the arc's plane is horizontal; elevation is interpolated linearly */
  y0: number;
  y1: number;
}
export type Piece = LinePiece | CubicPiece | ArcPiece;

export interface PathPoint {
  p: V3;
  t: V3; // unit tangent
  s: number;
  /** local curvature (1/m, signed: + = turning left when viewed from above) */
  k: number;
  /** index of the piece this point belongs to */
  piece: number;
}

const sub3 = (a: V3, b: V3): V3 => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];

function pieceLength(p: Piece): number {
  switch (p.kind) {
    case 'line':
      return Math.hypot(p.b[0] - p.a[0], p.b[1] - p.a[1], p.b[2] - p.a[2]);
    case 'arc':
      return Math.abs(p.sweep) * p.radius;
    case 'cubic':
      return cubicTable(p).s[cubicTable(p).s.length - 1];
  }
}

/**
 * Arc-length lookup table for a cubic piece. A cubic's Bézier parameter is not
 * proportional to arc length, so stations must be inverted through the table —
 * otherwise kerb offsets, mouths and markings all slide along the curve.
 */
interface CubicLookup {
  t: number[];
  s: number[];
}
const cubicTables = new WeakMap<CubicPiece, CubicLookup>();

function cubicTable(p: CubicPiece): CubicLookup {
  let cached = cubicTables.get(p);
  if (!cached) {
    const n = 32;
    const t: number[] = [0];
    const s: number[] = [0];
    let prev = p.p0;
    let acc = 0;
    for (let i = 1; i <= n; i++) {
      const tt = i / n;
      const pt = cubicAt(p.p0, p.p1, p.p2, p.p3, tt);
      acc += Math.hypot(pt[0] - prev[0], pt[1] - prev[1], pt[2] - prev[2]);
      prev = pt;
      t.push(tt);
      s.push(acc);
    }
    cached = { t, s };
    cubicTables.set(p, cached);
  }
  return cached;
}

/** Bezier parameter at a given arc length along a cubic piece. */
function cubicTAt(p: CubicPiece, local: number): number {
  const { t, s } = cubicTable(p);
  const total = s[s.length - 1];
  if (total <= 1e-12) return 0;
  const target = clamp(local, 0, total);
  let i = 0;
  let hi = s.length - 1;
  while (i < hi) {
    const mid = (i + hi + 1) >> 1;
    if (s[mid] <= target) i = mid;
    else hi = mid - 1;
  }
  const j = Math.min(i + 1, s.length - 1);
  const span = s[j] - s[i];
  const frac = span < 1e-12 ? 0 : (target - s[i]) / span;
  return clamp(lerp(t[i], t[j], frac), 0, 1);
}

function piecePoint(p: Piece, t: number): V3 {
  switch (p.kind) {
    case 'line':
      return [lerp(p.a[0], p.b[0], t), lerp(p.a[1], p.b[1], t), lerp(p.a[2], p.b[2], t)];
    case 'arc': {
      const ang = p.startAngle + p.sweep * t;
      return [p.centre[0] + Math.cos(ang) * p.radius, lerp(p.y0, p.y1, t), p.centre[2] + Math.sin(ang) * p.radius];
    }
    case 'cubic':
      return cubicAt(p.p0, p.p1, p.p2, p.p3, t);
  }
}

function pieceTangent(p: Piece, t: number): V3 {
  switch (p.kind) {
    case 'line': {
      const d = sub3(p.b, p.a);
      const l = Math.hypot(d[0], d[1], d[2]) || 1;
      return [d[0] / l, d[1] / l, d[2] / l];
    }
    case 'arc': {
      const ang = p.startAngle + p.sweep * t;
      const dir = p.sweep >= 0 ? 1 : -1;
      const dx = -Math.sin(ang) * dir;
      const dz = Math.cos(ang) * dir;
      const dy = (p.y1 - p.y0) / Math.max(1e-6, Math.abs(p.sweep) * p.radius);
      const l = Math.hypot(dx, dy, dz) || 1;
      return [dx / l, dy / l, dz / l];
    }
    case 'cubic': {
      const d = cubicDeriv(p.p0, p.p1, p.p2, p.p3, t);
      const l = Math.hypot(d[0], d[1], d[2]) || 1;
      return [d[0] / l, d[1] / l, d[2] / l];
    }
  }
}

function pieceCurvature(p: Piece, t: number): number {
  switch (p.kind) {
    case 'line':
      return 0;
    case 'arc':
      return (p.sweep >= 0 ? 1 : -1) / Math.max(1e-6, p.radius);
    case 'cubic': {
      const d = cubicDeriv(p.p0, p.p1, p.p2, p.p3, t);
      const dd = cubicDeriv2(p.p0, p.p1, p.p2, p.p3, t);
      const sp = Math.hypot(d[0], d[2]) || 1e-6;
      // signed planar curvature, + = turning left (x = d.x/ds, z = d.z/ds)
      const k = (d[2] * dd[0] - d[0] * dd[2]) / Math.pow(sp, 3);
      return -k;
    }
  }
}

export class Path {
  pieces: Piece[] = [];
  private lens: number[] = [];
  private total = -1;

  static fromLine(a: V3, b: V3): Path {
    const p = new Path();
    p.pieces = [{ kind: 'line', a, b }];
    return p;
  }

  static fromCubic(p0: V3, p1: V3, p2: V3, p3: V3): Path {
    const p = new Path();
    p.pieces = [{ kind: 'cubic', p0, p1, p2, p3 }];
    return p;
  }

  static fromArc(centre: V3, radius: number, startAngle: number, sweep: number, y0 = 0, y1 = 0): Path {
    const p = new Path();
    p.pieces = [{ kind: 'arc', centre, radius, startAngle, sweep, y0, y1 }];
    return p;
  }

  clone(): Path {
    const p = new Path();
    p.pieces = this.pieces.map((x) => ({ ...x })) as Piece[];
    return p;
  }

  /** Append another path (assumes the ends match). */
  append(other: Path): Path {
    this.pieces.push(...other.pieces.map((x) => ({ ...x })) as Piece[]);
    this.total = -1;
    return this;
  }

  prepend(other: Path): Path {
    this.pieces.unshift(...other.pieces.map((x) => ({ ...x })) as Piece[]);
    this.total = -1;
    return this;
  }

  get length(): number {
    if (this.total < 0) {
      this.lens = this.pieces.map(pieceLength);
      this.total = this.lens.reduce((a, b) => a + b, 0);
    }
    return this.total;
  }

  /** Locate a station: piece index + normalised t within it. */
  private locate(s: number): { index: number; t: number } {
    const len = this.length;
    const target = clamp(s, 0, len);
    let acc = 0;
    for (let i = 0; i < this.pieces.length; i++) {
      const l = this.lens[i];
      if (target <= acc + l + 1e-9) {
        const piece = this.pieces[i];
        const local = clamp(target - acc, 0, l);
        const t = piece.kind === 'cubic' ? cubicTAt(piece, local) : l < 1e-9 ? 0 : local / l;
        return { index: i, t: clamp(t, 0, 1) };
      }
      acc += l;
    }
    const last = this.pieces.length - 1;
    return { index: last, t: 1 };
  }

  pointAt(s: number): PathPoint {
    const { index, t } = this.locate(s);
    const piece = this.pieces[index];
    return {
      p: piecePoint(piece, t),
      t: pieceTangent(piece, t),
      s: clamp(s, 0, this.length),
      k: pieceCurvature(piece, t),
      piece: index,
    };
  }

  positionAt(s: number): V3 {
    const { index, t } = this.locate(s);
    return piecePoint(this.pieces[index], t);
  }

  tangentAt(s: number): V3 {
    const { index, t } = this.locate(s);
    return pieceTangent(this.pieces[index], t);
  }

  curvatureAt(s: number): number {
    const { index, t } = this.locate(s);
    return pieceCurvature(this.pieces[index], t);
  }

  /** Truncate to [from, to] in station space, splitting pieces exactly. */
  trimmed(from: number, to: number): Path {
    const a = clamp(Math.min(from, to), 0, this.length);
    const b = clamp(Math.max(from, to), 0, this.length);
    const out: Piece[] = [];
    let acc = 0;
    for (let i = 0; i < this.pieces.length; i++) {
      const piece = this.pieces[i];
      const l = this.lens[i] || pieceLength(piece);
      const s0 = acc;
      const s1 = acc + l;
      acc = s1;
      if (s1 <= a + 1e-9 || s0 >= b - 1e-9) continue;
      const t0 = l < 1e-9 ? 0 : clamp((Math.max(a, s0) - s0) / l, 0, 1);
      const t1 = l < 1e-9 ? 1 : clamp((Math.min(b, s1) - s0) / l, 0, 1);
      const seg = trimPiece(piece, t0, t1);
      if (seg) out.push(seg);
    }
    const path = new Path();
    path.pieces = out;
    return path;
  }

  /** Split at stations (ascending) into a list of sub-paths. */
  splitAt(stations: number[]): Path[] {
    const cuts = [...new Set(stations.map((s) => clamp(s, 0, this.length)))].sort((a, b) => a - b);
    const bounds = [0, ...cuts.filter((c) => c > 1e-6 && c < this.length - 1e-6), this.length];
    const out: Path[] = [];
    for (let i = 1; i < bounds.length; i++) out.push(this.trimmed(bounds[i - 1], bounds[i]));
    return out;
  }

  /** Dense polyline for plan rendering / picking. */
  polyline(step: number): V3[] {
    const n = Math.max(2, Math.ceil(this.length / Math.max(0.05, step)) + 1);
    const out: V3[] = [];
    for (let i = 0; i < n; i++) out.push(this.positionAt((this.length * i) / (n - 1)));
    return out;
  }

  /** Station list with adaptive density: finer where curvature is high. */
  adaptiveStations(base: number, minStep = 0.25): number[] {
    const out: number[] = [0];
    let s = 0;
    const len = this.length;
    while (s < len - 1e-6) {
      const k = Math.abs(this.curvatureAt(s + 0.5));
      // chord tolerance ~ 12 mm against the local radius
      const r = k > 1e-6 ? 1 / k : 1e6;
      const tolStep = Math.sqrt(8 * 0.012 * r);
      const step = clamp(Math.min(base, Math.max(minStep, tolStep)), minStep, base);
      s = Math.min(len, s + step);
      out.push(s);
    }
    if (out[out.length - 1] < len) out.push(len);
    return out;
  }

  /** Total plan length of the polyline (XZ only). */
  planLength(): number {
    return this.polyline(1).reduce((acc, p, i, arr) => (i === 0 ? 0 : acc + Math.hypot(p[0] - arr[i - 1][0], p[2] - arr[i - 1][2])), 0);
  }
}

/** Trim a piece to normalised [t0,t1] with exact geometry. */
export function trimPiece(piece: Piece, t0: number, t1: number): Piece | null {
  if (t1 - t0 < 1e-9) return null;
  switch (piece.kind) {
    case 'line': {
      const a = piecePoint(piece, t0) as V3;
      const b = piecePoint(piece, t1) as V3;
      return { kind: 'line', a, b };
    }
    case 'arc': {
      const startAngle = piece.startAngle + piece.sweep * t0;
      const sweep = piece.sweep * (t1 - t0);
      const y0 = lerp(piece.y0, piece.y1, t0);
      const y1 = lerp(piece.y0, piece.y1, t1);
      return { kind: 'arc', centre: piece.centre, radius: piece.radius, startAngle, sweep, y0, y1 };
    }
    case 'cubic': {
      const [p0, p1, , , , , p3] = cubicSplit(piece.p0, piece.p1, piece.p2, piece.p3, t1);
      if (t0 <= 1e-9) return { kind: 'cubic', p0: piece.p0, p1: piece.p1, p2: piece.p2, p3: piece.p3 };
      const [q0, q1, q2, q3] = cubicSplit(p0, p1, piece.p2, p3, t0 / Math.max(1e-9, t1));
      return { kind: 'cubic', p0: q0, p1: q1, p2: q2, p3: q3 };
    }
  }
}

/** Split a cubic at t → [a0..a3][b0..b3]. */
export function cubicSplitIntoTwo(p0: V3, p1: V3, p2: V3, p3: V3, t: number): [V3[], V3[]] {
  const a = lerpV(p0, p1, t);
  const b = lerpV(p1, p2, t);
  const c = lerpV(p2, p3, t);
  const d = lerpV(a, b, t);
  const e = lerpV(b, c, t);
  const f = lerpV(d, e, t);
  return [
    [p0, a, d, f],
    [f, e, c, p3],
  ];
}

function lerpV(a: V3, b: V3, t: number): V3 {
  return [lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t)];
}

export function cubicSplit(p0: V3, p1: V3, p2: V3, p3: V3, t: number): V3[] {
  const [a, b] = cubicSplitIntoTwo(p0, p1, p2, p3, t);
  return [...a, ...b.slice(1)];
}

// ---------------------------------------------------------------- offsets / intersections

export interface EdgeFrame {
  /** left/right kerb edge lines of a road arm, as 2D point + unit direction */
  point: V2;
  dir: V2;
}

/** Intersect two infinite lines defined by point+dir. Returns null when parallel. */
export function lineIntersect(a: EdgeFrame, b: EdgeFrame): { p: V2; ta: number; tb: number } | null {
  const den = v2cross(a.dir, b.dir);
  if (Math.abs(den) < 1e-9) return null;
  const dp = v2sub(b.point, a.point);
  const ta = v2cross(dp, b.dir) / den;
  const tb = v2cross(dp, a.dir) / den;
  return { p: v2add(a.point, v2mul(a.dir, ta)), ta, tb };
}

/**
 * Circular fillet between two rays meeting at a corner.
 * `a`/`b` are rays leaving the corner; returns the arc that is tangent to both.
 */
export function filletArc(
  corner: V2,
  dirA: V2,
  dirB: V2,
  radius: number,
  y0: number,
  y1: number,
): { centre: V2; startAngle: number; sweep: number; tA: number; tB: number; radius: number } | null {
  const da = v2norm(dirA);
  const db = v2norm(dirB);
  const cross = v2cross(da, db);
  const dot = clamp(v2dot(da, db), -1, 1);
  const theta = Math.acos(dot);
  if (Math.abs(cross) < 1e-7 || theta < 1e-3 || theta > Math.PI - 1e-3) return null;
  const half = theta / 2;
  const t = radius / Math.tan(half);
  const bis = v2norm(v2add(da, db));
  const dist = radius / Math.sin(half);
  const centre = v2add(corner, v2mul(bis, dist * (cross > 0 ? 1 : 1)));
  const ta = v2add(corner, v2mul(da, t));
  const tb = v2add(corner, v2mul(db, t));
  const a0 = Math.atan2(ta[1] - centre[1], ta[0] - centre[0]);
  let sweep = Math.atan2(tb[1] - centre[1], tb[0] - centre[0]) - a0;
  while (sweep > Math.PI) sweep -= TAU;
  while (sweep < -Math.PI) sweep += TAU;
  return { centre, startAngle: a0, sweep, tA: t, tB: t, radius };
}

/** Solve the tangent length available for a fillet given a desired radius. */
export const filletTangentLength = (radius: number, theta: number): number => radius / Math.tan(Math.max(1e-3, theta / 2));

/** 2D distance from a point to an infinite line (signed, + = left of dir). */
export function signedDistanceToLine(p: V2, point: V2, dir: V2): number {
  return v2cross(dir, v2sub(p, point));
}

export const distanceToLine = (p: V2, point: V2, dir: V2): number => Math.abs(signedDistanceToLine(p, point, dir));

/** Closest station on a polyline to a plan point; returns {s, dist, index}. */
export function projectToPolyline(p: V2, pts: V2[], cum: number[]): { s: number; dist: number; index: number } {
  let best = { s: 0, dist: Infinity, index: 0 };
  for (let i = 1; i < pts.length; i++) {
    const a = pts[i - 1];
    const b = pts[i];
    const ab = v2sub(b, a);
    const l2 = v2dot(ab, ab);
    const t = l2 < 1e-12 ? 0 : clamp(v2dot(v2sub(p, a), ab) / l2, 0, 1);
    const q = v2add(a, v2mul(ab, t));
    const d = v2dist(p, q);
    if (d < best.dist) best = { s: lerp(cum[i - 1], cum[i], t), dist: d, index: i };
  }
  return best;
}

/** Cumulative lengths of a polyline. */
export function cumulative(pts: V2[]): number[] {
  const out = [0];
  for (let i = 1; i < pts.length; i++) out.push(out[i - 1] + v2dist(pts[i - 1], pts[i]));
  return out;
}

/** Round a polyline corner with an arc: returns the trimmed distance and arc data. */
export function cornerArc(inDir: V2, outDir: V2, corner: V2, radius: number): { arc: ReturnType<typeof filletArc>; trimIn: number; trimOut: number } | null {
  const back = v2norm(v2sub(corner, v2add(corner, v2mul(inDir, 1)))); // -inDir
  const arc = filletArc(corner, back, outDir, radius, 0, 0);
  if (!arc) return null;
  return { arc, trimIn: arc.tA, trimOut: arc.tB };
}

export { pieceLength, piecePoint, pieceTangent, pieceCurvature };

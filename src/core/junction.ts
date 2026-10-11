/**
 * Junction solver and mesh builder.
 *
 * A junction is solved in five passes:
 *
 *  1. every arm gets a frame (outward direction, outward kerb-face offsets);
 *  2. for each arm we compute a *mouth floor*: the distance at which every other
 *     road's carriageway has cleared our centre line. The mouth can never be
 *     closer than that, which is what stops one road from being drawn over
 *     another;
 *  3. adjacent arms get a kerb return — an analytic line/arc/line fillet whose
 *     radius is bisected up until the tangency stations hit the mouth limit. When
 *     two arms are almost straight through (a T on a through road) there is no
 *     corner at all: the kerb simply continues and the two mouths line up;
 *  4. the carriageway inside the junction is filled with pure quads: an annulus
 *     from the boundary ring to an inner ring, then a quad fan into a centre
 *     quad. Ring vertices *are* the road mouth vertices, so the merge is exact —
 *     no cracks, no doubled sheets, no triangles;
 *  5. kerbs, footways, planting and verges wrap each corner as one band mesh
 *     whose rows follow the corner boundary, so kerbs stay continuous from arm to
 *     arm and pavement edges never cross.
 */

import {
  clamp,
  lerp,
  polygonCentroid,
  TAU,
  v2add,
  v2angle,
  v2cross,
  v2dist,
  v2dot,
  v2len,
  v2mul,
  v2norm,
  v2sub,
  type V2,
  type V3,
} from './math';
import { MeshBuilder, MAT_INDEX } from './mesh';
import { profileHeightAt, type CompiledProfile, type ProfileSample, type ZoneKind } from './profile';
import { KERB_PROFILES, type RoadClass, type QualityProfile, type SurfaceKind } from './model';
import { profileAtCorridorStation, type Arm, type EndState } from './spline';

export const CLASS_CORNER_RADIUS: Record<RoadClass, number> = {
  motorway: 30,
  expressway: 24,
  arterial: 15,
  boulevard: 12,
  avenue: 11,
  street: 9,
  local: 6,
  lane: 4.5,
  alley: 2.5,
  service: 5,
  ramp: 20,
  roundabout: 8,
  'parking-aisle': 4,
  cycleway: 3,
  footpath: 1.5,
  rail: 40,
  tram: 12,
};

/** angular gap above which two arms count as one continuous road (no kerb return) */
const OPEN_ANGLE = (148 * Math.PI) / 180;
/** arms more parallel than this never limit each other's mouths */
const PARALLEL_SIN = 0.35;
const MIN_MOUTH = 1.1;
const MAX_MOUTH = 60;
const MOUTH_CLEARANCE = 0.35;

export interface CornerBand {
  offset: number;
  y: number;
  mat: SurfaceKind;
  kind: ZoneKind;
  vertical: boolean;
}

export interface CornerPoint {
  p: V2;
  y: number;
  t: number;
  /** outward direction (kerb line normal) */
  outward: V2;
  /** true when this point is an arc sample rather than a straight kerb run */
  arc: boolean;
}

export interface CornerSolution {
  index: number;
  armA: number;
  armB: number;
  kind: 'fillet' | 'open';
  V: V2 | null;
  radius: number;
  centre: V2 | null;
  startAngle: number;
  sweep: number;
  tangencyA: number;
  tangencyB: number;
  /** kerb-face offset on arm A's right side (outward frame) */
  sideA: 'left' | 'right';
  sideB: 'left' | 'right';
  offsetA: number;
  offsetB: number;
  /** points strictly between the two mouth ends, in ring order */
  midPoints: CornerPoint[];
  bandsA: CornerBand[];
  bandsB: CornerBand[];
  feasible: boolean;
  note: string;
}

export interface JunctionArmSolved {
  arm: Arm;
  index: number;
  bearing: number;
  outDir: V2;
  rightDir: V2;
  kerb: { left: number; right: number };
  nodeStation: number;
  end: 'start' | 'end';
  /** outward station of the mouth on the left / right side of this arm */
  stationLeft: number;
  stationRight: number;
  /** analytic mouth floor from the other arms */
  floor: number;
  mouthOffsets: number[];
  mouthProfile: ProfileSample[];
  ringStart: number;
  ringEnd: number;
  roadOffset(outU: number): number;
  /** outward frame offset -> this arm's own profile offset */
  flipOffset(roadU: number): number;
  corridorStation(outward: number): number;
  /** outward unit normal of a side (away from the carriageway) */
  outwardNormal(side: 'left' | 'right'): V2;
}

export interface RingPoint {
  p: V2;
  y: number;
  arm: number;
  corner: number;
  roadOffset: number;
  kind: 'mouth' | 'corner';
}

export interface JunctionSolve {
  nodeId: string;
  nodeIndex: number;
  name: string;
  arms: JunctionArmSolved[];
  corners: CornerSolution[];
  ring: RingPoint[];
  centre: V2;
  centreY: number;
  diagnostics: string[];
  warnings: string[];
  area: number;
  signalised: boolean;
  endStates: Map<string, EndState>;
}

export function solveJunction(
  nodeIndex: number,
  armsIn: Arm[],
  quality: QualityProfile,
  cpByEdge: Map<string, CompiledProfile>,
): JunctionSolve {
  const node = armsIn[0].node;
  const diagnostics: string[] = [];
  const warnings: string[] = [];
  const solved: JunctionArmSolved[] = [];

  const sorted = armsIn
    .map((arm) => ({ arm, bearing: v2angle(arm.dirOut) }))
    .sort((a, b) => a.bearing - b.bearing)
    .map((x) => x.arm);

  for (let i = 0; i < sorted.length; i++) {
    const arm = sorted[i];
    const cp = cpByEdge.get(arm.edge.id) ?? arm.profile;
    const outDir = arm.dirOut;
    const rightDir: V2 = [outDir[1], -outDir[0]];
    // outward-frame kerb offsets: the kerb FACE line. The mouth (junction
    // carriageway) ends here; the kerb top, footway and verge are carried by the
    // corner sectors so the kerb face stays vertical all the way round.
    const kerbLeft = arm.flip > 0 ? cp.kerb.left : -cp.kerb.right;
    const kerbRight = arm.flip > 0 ? cp.kerb.right : -cp.kerb.left;
    const corridorStation = (outward: number): number =>
      arm.end === 'start' ? arm.nodeStation + outward : arm.nodeStation - outward;
    const roadOffset = (outU: number): number => (arm.flip > 0 ? outU : -outU);
    solved.push({
      arm,
      index: i,
      bearing: v2angle(outDir),
      outDir,
      rightDir,
      kerb: { left: kerbLeft, right: kerbRight },
      nodeStation: arm.nodeStation,
      end: arm.end,
      stationLeft: 0,
      stationRight: 0,
      floor: MIN_MOUTH,
      mouthOffsets: [],
      mouthProfile: [],
      ringStart: -1,
      ringEnd: -1,
      roadOffset,
      flipOffset: (roadU: number) => (arm.flip > 0 ? roadU : -roadU),
      corridorStation,
      outwardNormal: (side: 'left' | 'right') => (side === 'right' ? rightDir : ([-rightDir[0], -rightDir[1]] as V2)),
    });
  }
  const n = solved.length;

  // ---- frames -------------------------------------------------------------
  const localDir = (a: JunctionArmSolved, out: number): V2 => {
    const corridor = a.arm.corridor;
    const s = a.corridorStation(out);
    const s0 = clamp(s - 1, 0, corridor.path.length);
    const s1 = clamp(s + 1, 0, corridor.path.length);
    const p0 = corridor.path.positionAt(s0);
    const p1 = corridor.path.positionAt(s1);
    const d = v2norm([p1[0] - p0[0], p1[2] - p0[2]]);
    if (v2len(d) < 1e-6) return a.outDir;
    return v2dot(d, a.outDir) >= 0 ? d : v2mul(d, -1);
  };
  const kerbPoint = (a: JunctionArmSolved, out: number, side: 'left' | 'right'): V2 => {
    const off = side === 'left' ? a.kerb.left : a.kerb.right;
    const s = a.corridorStation(out);
    const p = a.arm.corridor.path.positionAt(s);
    const dir = localDir(a, out);
    const right: V2 = [dir[1], -dir[0]];
    return v2add([p[0], p[2]], v2mul(right, off));
  };
  const kerbLine = (a: JunctionArmSolved, probe: number, side: 'left' | 'right'): { p: V2; d: V2 } => ({
    p: kerbPoint(a, probe, side),
    d: localDir(a, probe),
  });

  // ---- mouth floors -------------------------------------------------------
  for (const a of solved) {
    let floor = MIN_MOUTH;
    const halfA = Math.max(Math.abs(a.kerb.left), Math.abs(a.kerb.right));
    for (const b of solved) {
      if (b === a) continue;
      const sin = Math.abs(Math.sin(b.bearing - a.bearing));
      const halfB = Math.max(Math.abs(b.kerb.left), Math.abs(b.kerb.right));
      if (sin < PARALLEL_SIN) {
        // near-parallel roads overlap for a long way; the join is handled by the
        // corridor merge instead of by a mouth
        const sameWay = Math.cos(b.bearing - a.bearing) > 0;
        if (!sameWay) continue;
        floor = Math.max(floor, Math.min(halfB, halfA) * 0.35 + MOUTH_CLEARANCE);
        continue;
      }
      floor = Math.max(floor, halfB / sin + MOUTH_CLEARANCE);
    }
    const cap = Math.max(MIN_MOUTH, Math.min(MAX_MOUTH, a.arm.corridor.length * 0.42));
    a.floor = clamp(floor, MIN_MOUTH, cap);
  }

  // ---- corners ------------------------------------------------------------
  const probeFor = (a: JunctionArmSolved): number => Math.max(3, Math.min(25, a.arm.corridor.length * 0.12));
  const corners: CornerSolution[] = [];

  for (let k = 0; k < n; k++) {
    const A = solved[k];
    const B = solved[(k + 1) % n];
    const pA = probeFor(A);
    const pB = probeFor(B);
    // Arms are sorted clockwise (bearing = atan2(x, z)), so walking the boundary
    // the corner between A and B is always formed by A's RIGHT kerb and B's LEFT
    // kerb. The two kerb lines are collinear only when the arms are roughly
    // opposite (a straight-through road), and that is the one case with no corner
    // at all: the kerb simply continues.
    let gap = B.bearing - A.bearing;
    while (gap < 0) gap += TAU;
    while (gap > TAU) gap -= TAU;
    const small = gap > Math.PI ? TAU - gap : gap;
    const sideA = 'right' as 'left' | 'right';
    const sideB = 'left' as 'left' | 'right';
    const lineA = kerbLine(A, pA, sideA);
    const lineB = kerbLine(B, pB, sideB);
    const den = v2cross(lineA.d, lineB.d);
    let V: V2 | null = null;
    if (Math.abs(den) > 1e-6) {
      const dp = v2sub(lineB.p, lineA.p);
      const t = v2cross(dp, lineB.d) / den;
      V = v2add(lineA.p, v2mul(lineA.d, t));
    }

    const rClassA = CLASS_CORNER_RADIUS[A.arm.edge.spec.class] ?? 8;
    const rClassB = CLASS_CORNER_RADIUS[B.arm.edge.spec.class] ?? 8;
    const base = node.junction.radius > 0 ? node.junction.radius : (rClassA + rClassB) / 2;
    const rMax = base * clamp(node.junction.radiusScale || 1, 0.1, 4);
    const capA = Math.max(MIN_MOUTH, Math.min(MAX_MOUTH, A.arm.corridor.length * 0.9));
    const capB = Math.max(MIN_MOUTH, Math.min(MAX_MOUTH, B.arm.corridor.length * 0.9));

    const offsetA = sideA === 'right' ? A.kerb.right : A.kerb.left;
    const offsetB = sideB === 'right' ? B.kerb.right : B.kerb.left;

    if (!V || small > OPEN_ANGLE) {
      // straight-through pair: the kerb continues, both mouths sit on the floor
      corners.push({
        index: k,
        armA: k,
        armB: (k + 1) % n,
        kind: 'open',
        V,
        radius: 0,
        centre: null,
        startAngle: 0,
        sweep: 0,
        tangencyA: 0,
        tangencyB: 0,
        sideA,
        sideB,
        offsetA,
        offsetB,
        midPoints: [],
        bandsA: [],
        bandsB: [],
        feasible: true,
        note: 'through road — kerb continues',
      });
      continue;
    }

    const theta = clamp(small, 0.04, Math.PI - 0.02);
    const half = theta / 2;
    const dirA = localDir(A, pA);
    const dirB = localDir(B, pB);
    const bisOut = v2norm(v2add(dirA, dirB));

    const evaluate = (r: number) => {
      const t = r / Math.tan(half);
      const taPoint = v2add(V, v2mul(dirA, t));
      const tbPoint = v2add(V, v2mul(dirB, t));
      const centre = v2add(V, v2mul(bisOut, r / Math.max(1e-6, Math.sin(half))));
      const a0 = Math.atan2(taPoint[1] - centre[1], taPoint[0] - centre[0]);
      let sweep = Math.atan2(tbPoint[1] - centre[1], tbPoint[0] - centre[0]) - a0;
      while (sweep > Math.PI) sweep -= TAU;
      while (sweep < -Math.PI) sweep += TAU;
      const taStation = v2dot(v2sub(taPoint, A.arm.pos), A.outDir);
      const tbStation = v2dot(v2sub(tbPoint, B.arm.pos), B.outDir);
      // the tangency must stay outward of the node: a kerb return that reaches
      // back across the junction would cut into the mouth of another arm
      const ok = taStation >= 0.2 && tbStation >= 0.2 && taStation <= capA && tbStation <= capB && r > 0;
      return { ok, taStation, tbStation, taPoint, tbPoint, centre, startAngle: a0, sweep, r };
    };

    let best = evaluate(0);
    let lo = 0;
    let hi = rMax;
    const sharpOk = best.taStation <= capA && best.tbStation <= capB;
    if (sharpOk && rMax > 0.05) {
      for (let i = 0; i < 26; i++) {
        const mid = (lo + hi) / 2;
        const ev = evaluate(mid);
        if (ev.ok) {
          lo = mid;
          best = ev;
        } else {
          hi = mid;
        }
      }
    }
    const radius = best.r;
    const tight = radius < 0.75 && small < (35 * Math.PI) / 180;
    const corner: CornerSolution = {
      index: k,
      armA: k,
      armB: (k + 1) % n,
      kind: 'fillet',
      V,
      radius,
      centre: radius > 1e-3 ? best.centre : null,
      startAngle: best.startAngle,
      sweep: best.sweep,
      tangencyA: best.taStation,
      tangencyB: best.tbStation,
      sideA,
      sideB,
      offsetA,
      offsetB,
      midPoints: [],
      bandsA: [],
      bandsB: [],
      feasible: !tight,
      note: tight ? 'kerb return below 0.75 m' : '',
    };
    if (tight) {
      warnings.push(
        `${node.name || 'Junction'}: ${Math.round((small * 180) / Math.PI)}° between arms ${k + 1} and ${k + 2} is too tight for a kerb return — widen the angle or shorten the arms.`,
      );
    }
    corners.push(corner);
  }

  // ---- mouth stations -----------------------------------------------------
  for (const a of solved) {
    a.stationLeft = a.floor;
    a.stationRight = a.floor;
  }
  for (const corner of corners) {
    if (corner.kind === 'open') continue;
    const A = solved[corner.armA];
    const B = solved[corner.armB];
    if (corner.sideA === 'right') A.stationRight = Math.max(A.stationRight, corner.tangencyA);
    else A.stationLeft = Math.max(A.stationLeft, corner.tangencyA);
    if (corner.sideB === 'left') B.stationLeft = Math.max(B.stationLeft, corner.tangencyB);
    else B.stationRight = Math.max(B.stationRight, corner.tangencyB);
  }

  // mouths must not cross each other: push conflicting arms outward a little
  const mouthSeg = (a: JunctionArmSolved): { a: V2; b: V2 } => ({
    a: kerbPoint(a, a.stationLeft, 'left'),
    b: kerbPoint(a, a.stationRight, 'right'),
  });
  for (let pass = 0; pass < 8; pass++) {
    let changed = false;
    for (let i = 0; i < n && !changed; i++) {
      for (let j = i + 1; j < n && !changed; j++) {
        const A = solved[i];
        const B = solved[j];
        if (segmentsCross(mouthSeg(A), mouthSeg(B))) {
          A.stationLeft += 0.8;
          A.stationRight += 0.8;
          B.stationLeft += 0.8;
          B.stationRight += 0.8;
          changed = true;
        }
      }
    }
    if (!changed) break;
  }

  // ---- end states ---------------------------------------------------------
  const endStates = new Map<string, EndState>();
  for (const a of solved) {
    const spec = a.arm.edge.spec;
    const outwardMouth = (a.stationLeft + a.stationRight) / 2;
    const radius = CLASS_CORNER_RADIUS[spec.class] ?? 8;
    const hasMedian = spec.median !== 'none' && spec.median !== 'painted';
    const taper = hasMedian ? clamp(outwardMouth - 0.4, 0, Math.max(4, Math.min(16, radius * 0.9))) : 0;
    endStates.set(`${a.arm.corridor.id}|${a.arm.end}`, {
      kind: 'junction',
      mouth: a.corridorStation(outwardMouth),
      nodeStation: a.nodeStation,
      medianTaper: taper,
      footwayTaper: 0,
      widthAdd: 0,
    });
  }

  // ---- mouth profiles -----------------------------------------------------
  for (const a of solved) {
    const cp = cpByEdge.get(a.arm.edge.id) ?? a.arm.profile;
    const mid = (a.stationLeft + a.stationRight) / 2;
    const state = endStates.get(`${a.arm.corridor.id}|${a.arm.end}`);
    const profile = profileAtCorridorStation(a.arm.corridor, cp, a.corridorStation(mid), state ? [state] : [], quality);
    a.mouthProfile = profile;
    const offsets: number[] = [];
    for (const smp of profile) {
      const u = a.flipOffset(smp.u);
      if (u >= a.kerb.left - 1e-6 && u <= a.kerb.right + 1e-6) {
        if (!offsets.some((o) => Math.abs(o - u) < 1e-6)) offsets.push(u);
      }
    }
    offsets.sort((x, y) => x - y);
    if (!offsets.length || Math.abs(offsets[0] - a.kerb.left) > 1e-6) offsets.unshift(a.kerb.left);
    if (Math.abs(offsets[offsets.length - 1] - a.kerb.right) > 1e-6) offsets.push(a.kerb.right);
    a.mouthOffsets = offsets;
    if (offsets.length < 3) {
      warnings.push(`${node.name || 'Junction'}: arm ${a.index + 1} produced a degenerate mouth.`);
    }
  }

  // ---- corner boundaries --------------------------------------------------
  const cornerBands = (a: JunctionArmSolved, side: 'left' | 'right', outwardStation: number, ends: EndState[]): CornerBand[] => {
    const cp = cpByEdge.get(a.arm.edge.id) ?? a.arm.profile;
    const s = a.corridorStation(outwardStation);
    const samples = profileAtCorridorStation(a.arm.corridor, cp, s, ends, quality);
    const kerbOut = side === 'right' ? a.kerb.right : a.kerb.left;
    const bands: CornerBand[] = [];
    for (const smp of samples) {
      const u = a.flipOffset(smp.u);
      if (side === 'right' && u < kerbOut - 1e-6) continue;
      if (side === 'left' && u > kerbOut + 1e-6) continue;
      const offset = side === 'right' ? u - kerbOut : kerbOut - u;
      bands.push({
        offset,
        y: smp.y + Math.tan(a.arm.corridor.bankAt(s)) * a.roadOffset(u) + a.arm.corridor.elevationAt(s),
        mat: smp.mat,
        kind: smp.kind,
        vertical: false,
      });
    }
    for (let i = 1; i < bands.length; i++) {
      if (Math.abs(bands[i].offset - bands[i - 1].offset) < 1e-6) {
        bands[i - 1].vertical = true;
        if (bands[i].y > bands[i - 1].y) bands[i - 1].mat = bands[i].mat;
      }
    }
    return bands;
  };

  for (const corner of corners) {
    const A = solved[corner.armA];
    const B = solved[corner.armB];
    const endA = endStates.get(`${A.arm.corridor.id}|${A.arm.end}`);
    const endB = endStates.get(`${B.arm.corridor.id}|${B.arm.end}`);
    const endsA = endA ? [endA] : [];
    const endsB = endB ? [endB] : [];
    const stationA = corner.sideA === 'right' ? A.stationRight : A.stationLeft;
    const stationB = corner.sideB === 'left' ? B.stationLeft : B.stationRight;
    corner.bandsA = cornerBands(A, corner.sideA, stationA, endsA);
    corner.bandsB = cornerBands(B, corner.sideB, stationB, endsB);

    // straight kerb run from the mouth end back to the tangency point
    const start = kerbPoint(A, stationA, corner.sideA);
    const end = kerbPoint(B, stationB, corner.sideB);
    const mid: CornerPoint[] = [];
    const dirAOut = A.outwardNormal(corner.sideA);
    const dirBOut = B.outwardNormal(corner.sideB);
    if (corner.kind === 'open') {
      // straight through: the two kerb runs are collinear, nothing to round
    } else {
      if (corner.tangencyA < stationA - 0.05) {
        const tp = kerbPoint(A, corner.tangencyA, corner.sideA);
        mid.push({ p: tp, y: 0, t: 0, outward: dirAOut, arc: false });
      }
      if (corner.centre && corner.radius > 1e-3) {
        const arcLen = Math.abs(corner.sweep) * corner.radius;
        const target = quality === 'cinematic' ? 0.3 : quality === 'high' ? 0.5 : quality === 'standard' ? 0.9 : 1.6;
        const steps = Math.max(4, Math.round(clamp(arcLen / target, 4, 72)));
        for (let i = 1; i < steps; i++) {
          const ang = corner.startAngle + (corner.sweep * i) / steps;
          const p: V2 = [corner.centre[0] + Math.cos(ang) * corner.radius, corner.centre[1] + Math.sin(ang) * corner.radius];
          const radial = v2norm(v2sub(p, corner.centre));
          mid.push({ p, y: 0, t: 0, outward: radial, arc: true });
        }
      } else if (corner.V) {
        const v = corner.V;
        if (v2dist(v, start) > 0.25 && v2dist(v, end) > 0.25) {
          mid.push({ p: v, y: 0, t: 0.5, outward: v2norm(v2add(dirAOut, dirBOut)), arc: false });
        }
      }
      if (corner.tangencyB < stationB - 0.05) {
        const tp = kerbPoint(B, corner.tangencyB, corner.sideB);
        mid.push({ p: tp, y: 0, t: 1, outward: dirBOut, arc: false });
      }
    }

    // heights + parameter along the boundary polyline
    const chain: Array<{ p: V2; outward: V2; arc: boolean }> = [
      { p: start, outward: dirAOut, arc: false },
      ...mid.map((m) => ({ p: m.p, outward: m.outward, arc: m.arc })),
      { p: end, outward: dirBOut, arc: false },
    ];
    const lengths: number[] = [0];
    for (let i = 1; i < chain.length; i++) lengths.push(lengths[i - 1] + v2dist(chain[i - 1].p, chain[i].p));
    const total = Math.max(1e-6, lengths[lengths.length - 1]);
    const yA0 = corner.bandsA[0]?.y ?? 0;
    const yB0 = corner.bandsB[0]?.y ?? 0;
    for (let i = 0; i < mid.length; i++) {
      const t = lengths[i + 1] / total;
      mid[i].t = t;
      mid[i].y = lerp(yA0, yB0, t);
    }
    corner.midPoints = mid;
    void total;
  }

  // ---- ring ---------------------------------------------------------------
  const ring: RingPoint[] = [];
  for (let i = 0; i < n; i++) {
    const a = solved[i];
    a.ringStart = ring.length;
    // mouth from the left kerb end to the right kerb end; corner k joins arm k's
    // right kerb to arm k+1's left kerb
    for (let c = 0; c < a.mouthOffsets.length; c++) {
      const u = a.mouthOffsets[c];
      const t = clamp((u - a.kerb.left) / Math.max(1e-6, a.kerb.right - a.kerb.left), 0, 1);
      const outward = lerp(a.stationLeft, a.stationRight, t);
      const state = endStates.get(`${a.arm.corridor.id}|${a.arm.end}`);
      const mv = mouthVertex(a, u, outward, cpByEdge, quality, state ? [state] : []);
      ring.push({ p: mv.p, y: mv.y, arm: i, corner: -1, roadOffset: a.roadOffset(u), kind: 'mouth' });
    }
    a.ringEnd = ring.length - 1;
    for (const mp of corners[i].midPoints) {
      ring.push({ p: mp.p, y: mp.y, arm: -1, corner: i, roadOffset: 0, kind: 'corner' });
    }
  }

  const centre = interiorPoint(ring.map((r) => r.p));
  const ys = ring.map((r) => r.y);
  const centreY = ys.reduce((x, y) => x + y, 0) / Math.max(1, ys.length);

  return {
    nodeId: node.id,
    nodeIndex,
    name: node.name,
    arms: solved,
    corners,
    ring,
    centre,
    centreY,
    diagnostics,
    warnings,
    area: Math.abs(ringArea(ring.map((r) => r.p))),
    signalised: node.junction.signalised,
    endStates,
  };
}

function heightOf(bands: CornerBand[], offset: number): number {
  if (!bands.length) return 0;
  if (offset <= bands[0].offset) return bands[0].y;
  for (let i = 1; i < bands.length; i++) {
    const a = bands[i - 1];
    const b = bands[i];
    if (offset <= b.offset + 1e-9) {
      if (Math.abs(b.offset - a.offset) < 1e-9) return Math.max(a.y, b.y);
      return lerp(a.y, b.y, clamp((offset - a.offset) / (b.offset - a.offset), 0, 1));
    }
  }
  return bands[bands.length - 1].y;
}

function ringArea(poly: V2[]): number {
  let a = 0;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) a += poly[j][0] * poly[i][1] - poly[i][0] * poly[j][1];
  return a / 2;
}

function segmentsCross(a: { a: V2; b: V2 }, b: { a: V2; b: V2 }): boolean {
  const d1 = v2sub(a.b, a.a);
  const d2 = v2sub(b.b, b.a);
  const den = v2cross(d1, d2);
  if (Math.abs(den) < 1e-9) return false;
  const qp = v2sub(b.a, a.a);
  const t = v2cross(qp, d2) / den;
  const u = v2cross(qp, d1) / den;
  const eps = 0.02;
  return t > eps && t < 1 - eps && u > eps && u < 1 - eps;
}

export interface MouthVertex {
  p: V2;
  y: number;
}

/** Position and height of a mouth vertex, at outward position `outward`. */
export function mouthVertex(
  a: JunctionArmSolved,
  outU: number,
  outward: number,
  cpByEdge: Map<string, CompiledProfile>,
  quality: QualityProfile,
  ends: EndState[] = [],
): MouthVertex {
  const corridor = a.arm.corridor;
  const s = a.corridorStation(outward);
  const p = corridor.path.positionAt(s);
  const s0 = clamp(s - 1, 0, corridor.path.length);
  const s1 = clamp(s + 1, 0, corridor.path.length);
  const p0 = corridor.path.positionAt(s0);
  const p1 = corridor.path.positionAt(s1);
  let dir = v2norm([p1[0] - p0[0], p1[2] - p0[2]]);
  if (v2len(dir) < 1e-6) dir = a.outDir;
  if (v2dot(dir, a.outDir) < 0) dir = v2mul(dir, -1);
  const right: V2 = [dir[1], -dir[0]];
  const pos = v2add([p[0], p[2]], v2mul(right, outU));
  const cp = cpByEdge.get(a.arm.edge.id) ?? a.arm.profile;
  const samples = profileAtCorridorStation(corridor, cp, s, ends, quality);
  const roadU = a.roadOffset(outU);
  const y = corridor.elevationAt(s) + profileHeightAt(samples, roadU) + Math.tan(corridor.bankAt(s)) * roadU;
  return { p: pos, y };
}

/** Robust interior point of a simple polygon. */
export function interiorPoint(poly: V2[]): V2 {
  let c = polygonCentroid(poly);
  for (let iter = 0; iter < 40; iter++) {
    let minDist = Infinity;
    let best: [V2, V2] | null = null;
    for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
      const d = distToSegment(c, poly[j], poly[i]);
      if (d < minDist) {
        minDist = d;
        best = [poly[j], poly[i]];
      }
    }
    if (minDist > 0.2) break;
    if (!best) break;
    const [a, b] = best;
    const mid = v2mul(v2add(a, b), 0.5);
    const away = v2norm(v2sub(c, mid));
    c = v2add(mid, v2mul(away, Math.max(0.3, minDist * 1.5)));
  }
  if (!pointInPoly(c, poly)) {
    const cent = polygonCentroid(poly);
    for (let k = 1; k <= 24; k++) {
      const test = v2add(cent, v2mul(v2sub(c, cent), k / 24));
      if (pointInPoly(test, poly)) return test;
    }
  }
  return c;
}

function distToSegment(p: V2, a: V2, b: V2): number {
  const ab = v2sub(b, a);
  const l2 = v2dot(ab, ab);
  const t = l2 < 1e-12 ? 0 : clamp(v2dot(v2sub(p, a), ab) / l2, 0, 1);
  return v2dist(p, v2add(a, v2mul(ab, t)));
}

function pointInPoly(p: V2, poly: V2[]): boolean {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const zi = poly[i][1];
    const zj = poly[j][1];
    if (zi > p[1] !== zj > p[1] && p[0] < ((poly[j][0] - poly[i][0]) * (p[1] - zi)) / (zj - zi + 1e-12) + poly[i][0]) inside = !inside;
  }
  return inside;
}

// ---------------------------------------------------------------- meshing

export interface JunctionMeshContext {
  builder: MeshBuilder;
  sheetQuads: Array<[V3, V3, V3, V3]>;
  junctionMat: number;
  quality: QualityProfile;
  matOf: (s: SurfaceKind) => number;
}

/** Build the junction carriageway surface (annulus + quad fan) — pure quads. */
export function buildJunctionSurface(solve: JunctionSolve, ctx: JunctionMeshContext): number {
  const { builder } = ctx;
  const ring = solve.ring;
  const n = ring.length;
  if (n < 6) return 0;
  const centre = solve.centre;
  const centreY = solve.centreY;
  const mat = ctx.junctionMat;
  const startVerts = builder.vertexCount;
  const startQuads = builder.quadCount;

  const cum: number[] = [0];
  for (let i = 1; i < n; i++) cum.push(cum[i - 1] + v2dist(ring[i - 1].p, ring[i].p));
  const totalLen = Math.max(1e-3, cum[n - 1] + v2dist(ring[n - 1].p, ring[0].p));

  const dists = ring.map((r) => v2dist(r.p, centre));
  const minDist = Math.max(0.5, Math.min(...dists));
  const inset = clamp(minDist * 0.36, 0.45, 7);

  const uv = (i: number, radial: number): [number, number] => [cum[i] * 0.25, radial * 0.25];
  const addRing = (p: V2, y: number, i: number, radial: number): number =>
    builder.addVertex({
      p: [p[0], y, p[1]],
      n: [0, 1, 0],
      uv: uv(i, radial),
      uv2: [p[0], p[1]],
      mat,
      tint: [1, 1, 1, 1],
    });

  const ringIdx: number[] = [];
  const innerIdx: number[] = [];
  const inner: Array<{ p: V2; y: number }> = [];
  for (let i = 0; i < n; i++) {
    const d = Math.max(1e-3, dists[i]);
    const k = clamp(1 - inset / d, 0.32, 0.93);
    const p = v2add(centre, v2mul(v2sub(ring[i].p, centre), k));
    const y = lerp(ring[i].y, centreY, 1 - k);
    inner.push({ p, y });
    ringIdx.push(addRing(ring[i].p, ring[i].y, i, 0));
    innerIdx.push(addRing(p, y, i, inset));
  }

  const radialRows = minDist > 9 ? 3 : minDist > 4.5 ? 2 : 1;
  let prevRow = ringIdx;
  for (let row = 1; row <= radialRows; row++) {
    const t = row / (radialRows + 1);
    const tt = Math.pow(t, 1.25);
    const rowIdx: number[] = [];
    for (let i = 0; i < n; i++) {
      const p: V2 = [lerp(ring[i].p[0], inner[i].p[0], tt), lerp(ring[i].p[1], inner[i].p[1], tt)];
      const y = lerp(ring[i].y, inner[i].y, tt);
      rowIdx.push(addRing(p, y, i, inset * tt));
    }
    for (let i = 0; i < n; i++) {
      const j = (i + 1) % n;
      builder.addQuad(prevRow[i], prevRow[j], rowIdx[j], rowIdx[i]);
      pushSheetIdx(ctx, prevRow[i], prevRow[j], rowIdx[j], rowIdx[i]);
    }
    prevRow = rowIdx;
  }
  for (let i = 0; i < n; i++) {
    const j = (i + 1) % n;
    builder.addQuad(prevRow[i], prevRow[j], innerIdx[j], innerIdx[i]);
    pushSheetIdx(ctx, prevRow[i], prevRow[j], innerIdx[j], innerIdx[i]);
  }

  // centre quad aligned with the dominant arm
  const dominant = solve.arms.reduce(
    (acc, a) => (a.arm.edge.spec.priority > acc.p ? { p: a.arm.edge.spec.priority, dir: a.outDir } : acc),
    { p: -1, dir: [1, 0] as V2 },
  ).dir;
  const axis: V2 = v2norm(dominant);
  const side: V2 = [axis[1], -axis[0]];
  const quadHalf = Math.max(0.35, Math.min(...inner.map((p) => v2dist(p.p, centre))) * 0.42);
  const quadCorners: Array<{ p: V2; y: number }> = [
    { p: v2add(centre, v2add(v2mul(axis, quadHalf), v2mul(side, quadHalf))), y: centreY },
    { p: v2add(centre, v2add(v2mul(axis, quadHalf), v2mul(side, -quadHalf))), y: centreY },
    { p: v2add(centre, v2add(v2mul(axis, -quadHalf), v2mul(side, -quadHalf))), y: centreY },
    { p: v2add(centre, v2add(v2mul(axis, -quadHalf), v2mul(side, quadHalf))), y: centreY },
  ];
  const quadIdx = quadCorners.map((c) => addRing(c.p, c.y, 0, v2dist(c.p, centre)));

  const baseAngle = Math.atan2(quadCorners[0].p[1] - centre[1], quadCorners[0].p[0] - centre[0]);
  const norm = (a: number): number => {
    let v = a - baseAngle;
    while (v < 0) v += TAU;
    while (v >= TAU) v -= TAU;
    return v;
  };
  const quadAng = quadCorners.map((c) => norm(Math.atan2(c.p[1] - centre[1], c.p[0] - centre[0])));
  const innerAng = inner.map((p) => norm(Math.atan2(p.p[1] - centre[1], p.p[0] - centre[0])));

  for (let s = 0; s < 4; s++) {
    const a0 = quadAng[s];
    const a1 = s === 3 ? TAU : quadAng[s + 1];
    const span = Math.max(1e-4, a1 - a0);
    const members: number[] = [];
    for (let i = 0; i < n; i++) {
      const v = innerAng[i];
      if (v >= a0 - 1e-9 && v < a1 - 1e-9) members.push(i);
    }
    members.sort((x, y) => innerAng[x] - innerAng[y]);
    members.unshift(-1);
    members.push(-2);
    const cols = members.length;
    if (cols < 2) continue;
    const edgePts: number[] = [];
    const innerPts: number[] = [];
    for (let c = 0; c < cols; c++) {
      const m = members[c];
      const ang = m === -1 ? a0 : m === -2 ? a1 : innerAng[m];
      const t = clamp((ang - a0) / span, 0, 1);
      const qa = quadCorners[s];
      const qb = quadCorners[(s + 1) % 4];
      const ep: V2 = [lerp(qa.p[0], qb.p[0], t), lerp(qa.p[1], qb.p[1], t)];
      const ey = lerp(qa.y, qb.y, t);
      edgePts.push(addRing(ep, ey, m >= 0 ? m : 0, v2dist(ep, centre)));
      if (m === -1) innerPts.push(quadIdx[s]);
      else if (m === -2) innerPts.push(quadIdx[(s + 1) % 4]);
      else innerPts.push(innerIdx[m]);
    }
    for (let c = 0; c + 1 < cols; c++) {
      builder.addQuad(edgePts[c], edgePts[c + 1], innerPts[c + 1], innerPts[c]);
      pushSheetIdx(ctx, edgePts[c], edgePts[c + 1], innerPts[c + 1], innerPts[c]);
    }
  }
  builder.addQuad(quadIdx[0], quadIdx[1], quadIdx[2], quadIdx[3]);
  pushSheetIdx(ctx, quadIdx[0], quadIdx[1], quadIdx[2], quadIdx[3]);

  computeSmoothNormals(builder, startVerts);
  void totalLen;
  return builder.quadCount - startQuads;
}

function pushSheetIdx(ctx: JunctionMeshContext, a: number, b: number, c: number, d: number): void {
  const va = ctx.builder.verts[a];
  const vb = ctx.builder.verts[b];
  const vc = ctx.builder.verts[c];
  const vd = ctx.builder.verts[d];
  ctx.sheetQuads.push([va.p, vb.p, vc.p, vd.p]);
}

/** Average face normals over a vertex range, oriented upward. */
export function computeSmoothNormals(builder: MeshBuilder, fromVertex: number): void {
  const n = builder.vertexCount;
  const acc = new Float32Array(Math.max(0, n - fromVertex) * 3);
  const add = (i: number, nx: number, ny: number, nz: number) => {
    const o = (i - fromVertex) * 3;
    if (o < 0 || o + 2 >= acc.length) return;
    acc[o] += nx;
    acc[o + 1] += ny;
    acc[o + 2] += nz;
  };
  for (let t = 0; t + 2 < builder.tris.length; t += 3) {
    const a = builder.tris[t];
    const b = builder.tris[t + 1];
    const c = builder.tris[t + 2];
    if (a < fromVertex || b < fromVertex || c < fromVertex) continue;
    const pa = builder.verts[a].p;
    const pb = builder.verts[b].p;
    const pc = builder.verts[c].p;
    const ux = pb[0] - pa[0];
    const uy = pb[1] - pa[1];
    const uz = pb[2] - pa[2];
    const vx = pc[0] - pa[0];
    const vy = pc[1] - pa[1];
    const vz = pc[2] - pa[2];
    const nx = uy * vz - uz * vy;
    const ny = uz * vx - ux * vz;
    const nz = ux * vy - uy * vx;
    add(a, nx, ny, nz);
    add(b, nx, ny, nz);
    add(c, nx, ny, nz);
  }
  for (let i = fromVertex; i < n; i++) {
    const o = (i - fromVertex) * 3;
    let nx = acc[o];
    let ny = acc[o + 1];
    let nz = acc[o + 2];
    const l = Math.hypot(nx, ny, nz);
    if (l < 1e-9) {
      nx = 0;
      ny = 1;
      nz = 0;
    } else {
      nx /= l;
      ny /= l;
      nz /= l;
    }
    if (ny < 0) {
      nx = -nx;
      ny = -ny;
      nz = -nz;
    }
    builder.verts[i].n = [nx, ny, nz];
  }
}

/**
 * Kerbs, footways, planting and verges wrapping a corner.
 *
 * Rows follow the corner boundary (mouth end → kerb run → arc → kerb run → mouth
 * end). Columns are the two arms' outer profile samples paired by index, so the
 * bands taper smoothly from one road's section into the other's and the kerb face
 * stays vertical around the whole return.
 */
export function buildCornerSectors(solve: JunctionSolve, ctx: JunctionMeshContext): number {
  const { builder } = ctx;
  const quads0 = builder.quadCount;

  for (const corner of solve.corners) {
    const A = solve.arms[corner.armA];
    const B = solve.arms[corner.armB];
    const bandA = corner.bandsA;
    const bandB = corner.bandsB;
    if (bandA.length < 2 && bandB.length < 2) continue;

    const start = kerbEnd(A, corner.sideA);
    const end = kerbEnd(B, corner.sideB);
    const chain: Array<{ p: V2; outward: V2; t: number }> = [
      { p: start.p, outward: A.outwardNormal(corner.sideA), t: 0 },
      ...corner.midPoints.map((m) => ({ p: m.p, outward: m.outward, t: m.t })),
      { p: end.p, outward: B.outwardNormal(corner.sideB), t: 1 },
    ];
    if (chain.length < 2) continue;

    // Columns are the merged outer-band offsets of both arms. Because the two
    // bands are measured outward from their own kerb face, offset 0 is the kerb
    // face on both sides: columns therefore morph smoothly across the corner
    // instead of twisting where the two sections differ.
    const offsets: number[] = [];
    for (const b of [...bandA, ...bandB]) {
      if (b.offset < -1e-6) continue;
      if (!offsets.some((o) => Math.abs(o - b.offset) < 1e-6)) offsets.push(b.offset);
    }
    offsets.sort((x, y) => x - y);
    const cols2 = offsets.length;
    if (cols2 < 2) continue;

    const grid: number[][] = [];
    for (let j = 0; j < chain.length; j++) {
      const row = chain[j];
      const rowIdx: number[] = [];
      for (const offset of offsets) {
        const y = lerp(heightOf(bandA, offset), heightOf(bandB, offset), row.t);
        const p: V2 = [row.p[0] + row.outward[0] * offset, row.p[1] + row.outward[1] * offset];
        const matKind = (row.t < 0.5 ? bandFor(bandA, offset) : bandFor(bandB, offset))?.mat ?? 'concrete';
        rowIdx.push(
          builder.addVertex({
            p: [p[0], y, p[1]],
            n: [0, 1, 0],
            uv: [offset, 0],
            uv2: [p[0], p[1]],
            mat: ctx.matOf(matKind),
            tint: [1, 1, 1, 1],
          }),
        );
      }
      grid.push(rowIdx);
    }

    for (let j = 0; j + 1 < chain.length; j++) {
      const rowA = chain[j];
      const rowB = chain[j + 1];
      const dTangent: V2 = v2sub(rowB.p, rowA.p);
      for (let c = 0; c + 1 < cols2; c++) {
        const o0 = offsets[c];
        const o1 = offsets[c + 1];
        const r = rowA.t;
        const outward: V2 = v2norm(v2add(v2mul(rowA.outward, 1 - r), v2mul(rowB.outward, r)));
        const vertical = Math.abs(o1 - o0) < 1e-6;
        const dr = o1 - o0;
        const dh = lerp(heightOf(bandA, o1) - heightOf(bandA, o0), heightOf(bandB, o1) - heightOf(bandB, o0), r);
        let n: V3;
        if (vertical) {
          n = [outward[0], 0, outward[1]];
        } else {
          const tr: V3 = [outward[0] * dr, dh, outward[1] * dr];
          const ta: V3 = [dTangent[0], 0, dTangent[1]];
          const nx = ta[1] * tr[2] - ta[2] * tr[1];
          const ny = ta[2] * tr[0] - ta[0] * tr[2];
          const nz = ta[0] * tr[1] - ta[1] * tr[0];
          const l = Math.hypot(nx, ny, nz) || 1;
          n = [nx / l, ny / l, nz / l];
          if (n[1] < 0) n = [-n[0], -n[1], -n[2]];
        }
        for (const ci of [c, c + 1]) {
          builder.verts[grid[j][ci]].n = n;
          builder.verts[grid[j + 1][ci]].n = n;
        }
        builder.addQuad(grid[j][c], grid[j][c + 1], grid[j + 1][c + 1], grid[j + 1][c]);
      }
    }
  }
  return builder.quadCount - quads0;
}

function bandFor(bands: CornerBand[], offset: number): CornerBand | null {
  let best: CornerBand | null = null;
  for (const b of bands) {
    if (b.offset <= offset + 1e-6) best = b;
  }
  return best ?? bands[0] ?? null;
}

function kerbEnd(a: JunctionArmSolved, side: 'left' | 'right'): { p: V2; y: number } {
  const station = side === 'right' ? a.stationRight : a.stationLeft;
  const s = a.corridorStation(station);
  const p = a.arm.corridor.path.positionAt(s);
  const s0 = clamp(s - 1, 0, a.arm.corridor.path.length);
  const s1 = clamp(s + 1, 0, a.arm.corridor.path.length);
  const p0 = a.arm.corridor.path.positionAt(s0);
  const p1 = a.arm.corridor.path.positionAt(s1);
  let dir = v2norm([p1[0] - p0[0], p1[2] - p0[2]]);
  if (v2len(dir) < 1e-6) dir = a.outDir;
  if (v2dot(dir, a.outDir) < 0) dir = v2mul(dir, -1);
  const right: V2 = [dir[1], -dir[0]];
  const off = side === 'right' ? a.kerb.right : a.kerb.left;
  const pos = v2add([p[0], p[2]], v2mul(right, off));
  return { p: pos, y: 0 };
}

export { MAT_INDEX };

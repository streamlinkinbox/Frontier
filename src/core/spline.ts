/**
 * Spline layer: node handles, corridor paths, vertical alignment, banking and
 * station sampling.
 *
 * Authoring model: nodes are knots, edges are cubic-Bézier segments. Geometry
 * groups edges into *corridors* — maximal chains joined at degree-2 nodes — so a
 * hand-drawn sequence becomes one continuous path with mitered frames and a C1
 * vertical alignment. Corridors are what every downstream builder consumes.
 */

import {
  clamp,
  lerp,
  smootherstep,
  v2add,
  v2angle,
  v2dist,
  v2len,
  v2mul,
  v2norm,
  v2sub,
  type V2,
  type V3,
} from './math';
import { Path } from './path';
import { compileProfile, profileAt, type CompiledProfile, type ProfileSample } from './profile';
import type { Edge, Id, QualityProfile, RoadDoc, RoadNode } from './model';

export interface EdgeEndHandles {
  /** control point offset at the `a` end (from node a into the edge interior) */
  a: V2;
  /** control point offset at the `b` end */
  b: V2;
  /** true when either end was auto-generated */
  auto: boolean;
  /** true when the tangent was rotated to satisfy the junction angle constraint */
  adjusted: boolean;
}

/** key = `${edgeId}|a` or `${edgeId}|b` */
export type HandleMap = Map<string, V2>;

/** how a corridor end is modified by the junction it runs into */
export interface EndState {
  kind: 'junction' | 'free';
  /** corridor station where the road mesh stops (the mouth) */
  mouth: number;
  /** corridor station of the node */
  nodeStation: number;
  /** length over which a raised median tapers to a nose before the mouth */
  medianTaper: number;
  /** extra footway width at the mouth */
  footwayTaper: number;
  /** carriageway widening at the mouth (turn pocket / taper) */
  widthAdd: number;
}

export interface SplineDiagnostic {
  level: 'info' | 'warn' | 'error';
  message: string;
  nodeId?: Id;
  edgeId?: Id;
  station?: number;
}

const AUTO_HANDLE = 0.42;
export const handleKey = (edgeId: Id, end: 'a' | 'b'): string => `${edgeId}|${end}`;

/**
 * Resolve per-edge-end control handles.
 *
 *  - degree 1 (dead end): straight along the chord
 *  - degree 2 (pass-through): Catmull-Rom tangent through both neighbours, so a
 *    hand-drawn chain becomes one G1 curve
 *  - degree >= 3 (junction): every arm leaves straight along its own chord so
 *    mouth cross-sections stay perpendicular to the approach
 *
 * Manual handles (edge.ha / edge.hb) always win. Junction arms are then checked
 * against the minimum-angle constraint and rotated away from each other when the
 * user's geometry would otherwise produce an unusable intersection.
 */
export function resolveHandles(doc: RoadDoc, diags: SplineDiagnostic[] = []): { handles: HandleMap; nodeTension: Map<Id, number> } {
  const incident = new Map<Id, Edge[]>();
  for (const e of doc.edges) {
    if (!incident.has(e.a)) incident.set(e.a, []);
    if (!incident.has(e.b)) incident.set(e.b, []);
    incident.get(e.a)!.push(e);
    incident.get(e.b)!.push(e);
  }
  const nodeById = new Map(doc.nodes.map((n) => [n.id, n]));
  const handles: HandleMap = new Map();
  const nodeTension = new Map<Id, number>();

  const chord = (fromId: Id, toId: Id): { dir: V2; dist: number } => {
    const a = nodeById.get(fromId)!;
    const b = nodeById.get(toId)!;
    const d = v2sub([b.x, b.z], [a.x, a.z]);
    const len = v2len(d);
    return { dir: len < 1e-6 ? [1, 0] : v2norm(d), dist: len };
  };

  // pass 1 — auto handles
  for (const edge of doc.edges) {
    const ab = chord(edge.a, edge.b);
    const ba: { dir: V2; dist: number } = { dir: v2mul(ab.dir, -1), dist: ab.dist };
    const endInfo = (endNodeId: Id, otherNodeId: Id, other: { dir: V2; dist: number }) => {
      const node = nodeById.get(endNodeId)!;
      const edges = incident.get(endNodeId) ?? [];
      const tension = clamp(node.tension, 0.05, 1);
      const neighbours = edges
        .map((e) => {
          const otherId = e.a === endNodeId ? e.b : e.a;
          if (otherId === endNodeId) return null;
          const c = chord(endNodeId, otherId);
          return { id: otherId, dir: c.dir, dist: c.dist, edge: e };
        })
        .filter((n): n is { id: Id; dir: V2; dist: number; edge: Edge } => !!n && n.dist > 1e-4);
      let dir = other.dir;
      if (neighbours.length === 2) {
        const [n1, n2] = neighbours;
        const through = v2sub(
          [nodeById.get(n2.id)!.x, nodeById.get(n2.id)!.z],
          [nodeById.get(n1.id)!.x, nodeById.get(n1.id)!.z],
        );
        if (v2len(through) > 1e-4) {
          const t = v2norm(through);
          // keep the tangent pointing along this edge
          dir = (t[0] * other.dir[0] + t[1] * other.dir[1]) >= 0 ? t : v2mul(t, -1);
        }
      }
      const mag = Math.min(other.dist * AUTO_HANDLE * tension, 70);
      return { dir, mag, tension, node, count: edges.length };
    };
    const aInfo = endInfo(edge.a, edge.b, ab);
    const bInfo = endInfo(edge.b, edge.a, ba);
    const ha = edge.ha && v2len(edge.ha) > 0.05 ? edge.ha : v2mul(aInfo.dir, aInfo.mag);
    const hb = edge.hb && v2len(edge.hb) > 0.05 ? edge.hb : v2mul(bInfo.dir, bInfo.mag);
    handles.set(handleKey(edge.id, 'a'), ha);
    handles.set(handleKey(edge.id, 'b'), hb);
    nodeTension.set(edge.a, aInfo.tension);
    nodeTension.set(edge.b, bInfo.tension);
  }

  // pass 2 — junction angle constraint: rotate the shorter arm's approach away
  // from its neighbour when the deflection is too tight for a usable mouth.
  for (const node of doc.nodes) {
    const edges = incident.get(node.id) ?? [];
    if (edges.length < 3) continue;
    const minAngle = (node.junction.minAngle || doc.settings.angleConstraint || 40) * (Math.PI / 180);
    const arms = edges.map((e) => {
      const otherId = e.a === node.id ? e.b : e.a;
      const c = chord(node.id, otherId);
      const flip = e.a === node.id ? 1 : -1;
      return { edge: e, dir: c.dir, dist: c.dist, flip, key: handleKey(e.id, flip === 1 ? 'a' : 'b') };
    });
    arms.sort((p, q) => v2angle(p.dir) - v2angle(q.dir));
    for (let i = 0; i < arms.length; i++) {
      const a = arms[i];
      const b = arms[(i + 1) % arms.length];
      let between = gapBetween(arms.map((x) => x.dir), i, (i + 1) % arms.length);
      if (between > Math.PI) between = Math.PI * 2 - between;
      if (between >= minAngle - 1e-4) continue;
      const deficit = minAngle - between;
      // rotate the shorter arm away from the longer one by the deficit
      const target = a.dist <= b.dist ? a : b;
      const sign = target === a ? -1 : 1;
      const ang = v2angle(target.dir) + sign * deficit * 0.5;
      const newDir: V2 = [Math.sin(ang), Math.cos(ang)];
      const mag = v2len(handles.get(target.key) ?? [0, 0]) || target.dist * 0.25;
      handles.set(target.key, v2mul(newDir, mag));
      target.dir = newDir;
      diags.push({
        level: 'warn',
        nodeId: node.id,
        edgeId: target.edge.id,
        message: `${target.edge.name || 'Road'}: junction angle widened to ${((between + deficit * 0.5) * (180 / Math.PI)).toFixed(0)}° (minimum ${(minAngle * 180 / Math.PI).toFixed(0)}°) so the intersection stays clean.`,
      });
    }
  }
  return { handles, nodeTension };
}

/** Angular gap (radians) going clockwise from arm i to arm j (plan bearings). */
function gapBetween(axes: V2[], i: number, j: number): number {
  const a = v2angle(axes[i]);
  const b = v2angle(axes[j]);
  let d = b - a;
  while (d < 0) d += Math.PI * 2;
  while (d > Math.PI * 2) d -= Math.PI * 2;
  return d;
}

// ---------------------------------------------------------------- corridors

export interface CorridorEnd {
  kind: 'junction' | 'free';
  nodeId: Id;
  station: number;
  /** station where the corridor mesh stops (junction mouth trim, or the free end) */
  trim: number;
  flipped: boolean;
}

export interface Corridor {
  id: Id;
  name: string;
  edges: Edge[];
  nodes: RoadNode[];
  flipped: boolean[];
  path: Path;
  nodeStations: number[];
  length: number;
  elevationAt: (s: number) => number;
  gradeAt: (s: number) => number;
  /** signed banking in radians (positive = right side raised) */
  bankAt: (s: number) => number;
  start: CorridorEnd;
  end: CorridorEnd;
  spec: Edge['spec'];
  layerId: Id | null;
  hidden: boolean;
}

export interface Arm {
  id: Id;
  node: RoadNode;
  edge: Edge;
  corridor: Corridor;
  end: 'start' | 'end';
  /** unit direction leaving the node, plan space */
  dirOut: V2;
  /** unit direction leaving the node, with grade */
  tangentOut: V3;
  profile: CompiledProfile;
  nodeStation: number;
  /** +1 when the corridor traverses the edge a→b */
  flip: 1 | -1;
  pos: V2;
  edgeEnd: 'a' | 'b';
}

export function degreeMap(doc: RoadDoc): Map<Id, Edge[]> {
  const m = new Map<Id, Edge[]>();
  for (const n of doc.nodes) m.set(n.id, []);
  for (const e of doc.edges) {
    if (m.has(e.a)) m.get(e.a)!.push(e);
    if (m.has(e.b)) m.get(e.b)!.push(e);
  }
  return m;
}

export function isJunctionNode(doc: RoadDoc, incident: Map<Id, Edge[]>, nodeId: Id): boolean {
  const node = doc.nodes.find((n) => n.id === nodeId);
  if (!node) return false;
  const edges = incident.get(nodeId) ?? [];
  if (edges.length >= 3) return true;
  return false;
}

/**
 * Group edges into corridors; a corridor continues through degree-2 nodes that
 * are not flagged as corners and whose incident edges share a road class.
 */
export function buildCorridors(
  doc: RoadDoc,
  quality: QualityProfile,
  diags: SplineDiagnostic[],
  handles: HandleMap,
): { corridors: Corridor[]; incident: Map<Id, Edge[]> } {
  const incident = degreeMap(doc);
  const nodeById = new Map(doc.nodes.map((n) => [n.id, n]));
  const used = new Set<Id>();
  const corridors: Corridor[] = [];

  const continues = (nodeId: Id): boolean => {
    const node = nodeById.get(nodeId);
    if (!node) return false;
    const edges = incident.get(nodeId) ?? [];
    if (edges.length !== 2) return false;
    if (node.corner || node.kind === 'junction' || node.kind === 'roundabout' || node.kind === 'dead-end' || node.kind === 'crossing') return false;
    const [e1, e2] = edges;
    if (e1.spec.class !== e2.spec.class) return false;
    if ((e1.rail === null) !== (e2.rail === null)) return false;
    if (e1.layerId !== e2.layerId) return false;
    return true;
  };

  const buildFrom = (startEdge: Edge, startNodeId: Id) => {
    if (used.has(startEdge.id)) return;
    const chainEdges: Edge[] = [];
    const chainNodes: RoadNode[] = [];
    const flipped: boolean[] = [];
    let nodeId = startNodeId;
    let edge: Edge | null = startEdge;
    while (edge && !used.has(edge.id)) {
      used.add(edge.id);
      const flippedNow = edge.a !== nodeId;
      const nextNodeId = flippedNow ? edge.a : edge.b;
      chainEdges.push(edge);
      chainNodes.push(nodeById.get(nodeId)!);
      flipped.push(flippedNow);
      nodeId = nextNodeId;
      if (!continues(nodeId)) break;
      edge = (incident.get(nodeId) ?? []).find((e) => !used.has(e.id)) ?? null;
    }
    chainNodes.push(nodeById.get(nodeId)!);
    corridors.push(makeCorridor(chainEdges, chainNodes, flipped, diags, handles));
  };

  for (const e of doc.edges) {
    if (used.has(e.id)) continue;
    for (const endNode of [e.a, e.b]) {
      if (!used.has(e.id) && !continues(endNode)) buildFrom(e, endNode);
    }
    if (!used.has(e.id)) buildFrom(e, e.a);
  }
  return { corridors, incident };
}

function makeCorridor(edges: Edge[], nodes: RoadNode[], flipped: boolean[], diags: SplineDiagnostic[], handles: HandleMap): Corridor {
  const path = new Path();
  const nodeStations: number[] = [];
  let station = 0;

  for (let i = 0; i < edges.length; i++) {
    const edge = edges[i];
    const a = nodes[i];
    const b = nodes[i + 1];
    const forward = !flipped[i];
    const p0: V2 = [a.x, a.z];
    const p3: V2 = [b.x, b.z];
    const outHandle = handles.get(handleKey(edge.id, forward ? 'a' : 'b')) ?? [0, 0];
    const inHandle = handles.get(handleKey(edge.id, forward ? 'b' : 'a')) ?? [0, 0];
    let c1 = v2add(p0, outHandle);
    let c2 = v2add(p3, inHandle);
    const chord = v2dist(p0, p3);
    const maxLen = Math.max(0.5, chord * 0.75);
    const h1 = v2sub(c1, p0);
    const h2 = v2sub(c2, p3);
    if (v2len(h1) > maxLen) c1 = v2add(p0, v2mul(v2norm(h1), maxLen));
    if (v2len(h2) > maxLen) c2 = v2add(p3, v2mul(v2norm(h2), maxLen));
    nodeStations.push(station);
    const c0: V3 = [p0[0], 0, p0[1]];
    const cc1: V3 = [c1[0], 0, c1[1]];
    const cc2: V3 = [c2[0], 0, c2[1]];
    const c3: V3 = [p3[0], 0, p3[1]];
    const seg = Path.fromCubic(c0, cc1, cc2, c3);
    station += seg.length;
    path.append(seg);
  }
  nodeStations.push(station);

  const ys = nodes.map((n) => n.y);
  const elevationAt = makeElevation(nodeStations, ys);
  for (let i = 0; i < edges.length; i++) {
    const grade = Math.abs((ys[i + 1] - ys[i]) / Math.max(1, nodeStations[i + 1] - nodeStations[i]));
    if (grade > 0.1) {
      diags.push({ level: 'warn', edgeId: edges[i].id, message: `${edges[i].name || 'Road'}: ${(grade * 100).toFixed(1)}% grade exceeds 10% — check node elevations.` });
    }
  }

  const spec = edges.reduce((acc, e) => (e.spec.priority > acc.priority ? e.spec : acc), edges[0].spec);
  return {
    id: `c_${edges[0].id}`,
    name: edges.length === 1 ? edges[0].name : `${edges[0].name || 'Road'} — corridor`,
    edges,
    nodes,
    flipped,
    path,
    nodeStations,
    length: station,
    elevationAt,
    gradeAt: (s: number) => (elevationAt(s + 0.5) - elevationAt(s - 0.5)),
    bankAt: () => 0,
    start: { kind: 'free', nodeId: nodes[0].id, station: 0, trim: 0, flipped: flipped[0] ?? false },
    end: { kind: 'free', nodeId: nodes[nodes.length - 1].id, station, trim: station, flipped: flipped[flipped.length - 1] ?? false },
    spec,
    layerId: edges[0].layerId,
    hidden: edges.every((e) => e.hidden),
  };
}

/** Monotone cubic Hermite over (station, elevation) keys: C1 grade, no overshoot. */
export function makeElevation(stations: number[], ys: number[]): (s: number) => number {
  const n = stations.length;
  if (n === 0) return () => 0;
  if (n === 1) return () => ys[0];
  const m: number[] = new Array(n).fill(0);
  for (let i = 1; i < n - 1; i++) {
    const d0 = (ys[i] - ys[i - 1]) / Math.max(1e-6, stations[i] - stations[i - 1]);
    const d1 = (ys[i + 1] - ys[i]) / Math.max(1e-6, stations[i + 1] - stations[i]);
    m[i] = d0 * d1 <= 0 ? 0 : clamp((d0 + d1) / 2, -0.12, 0.12);
  }
  return (s: number) => {
    const x = clamp(s, stations[0], stations[n - 1]);
    let i = 0;
    while (i < n - 2 && stations[i + 1] < x) i++;
    const h = Math.max(1e-6, stations[i + 1] - stations[i]);
    const t = clamp((x - stations[i]) / h, 0, 1);
    const t2 = t * t;
    const t3 = t2 * t;
    return (
      (2 * t3 - 3 * t2 + 1) * ys[i] +
      (t3 - 2 * t2 + t) * h * m[i] +
      (-2 * t3 + 3 * t2) * ys[i + 1] +
      (t3 - t2) * h * m[i + 1]
    );
  };
}

// ---------------------------------------------------------------- arms

export function buildArms(corridors: Corridor[]): Map<Id, Arm[]> {
  const byNode = new Map<Id, Arm[]>();
  for (const corridor of corridors) {
    const ends: Array<'start' | 'end'> = ['start', 'end'];
    for (const end of ends) {
      const nodeIndex = end === 'start' ? 0 : corridor.nodes.length - 1;
      const node = corridor.nodes[nodeIndex];
      const edgeIndex = end === 'start' ? 0 : corridor.edges.length - 1;
      const edge = corridor.edges[edgeIndex];
      const nodeStation = corridor.nodeStations[nodeIndex];
      const eps = clamp(corridor.length * 0.06, 0.5, 8);
      const sIn = end === 'start' ? Math.min(corridor.length, nodeStation + eps) : Math.max(0, nodeStation - eps);
      const pIn = corridor.path.positionAt(sIn);
      const pNode = corridor.path.positionAt(nodeStation);
      // outward = pointing away from the node, along the corridor
      const dir: V2 =
        end === 'start'
          ? v2sub([pIn[0], pIn[2]], [pNode[0], pNode[2]])
          : v2sub([pNode[0], pNode[2]], [pIn[0], pIn[2]]);
      const dirOut: V2 = v2len(dir) < 1e-6 ? [1, 0] : v2norm(dir);
      const grade =
        end === 'start'
          ? (corridor.elevationAt(sIn) - corridor.elevationAt(nodeStation)) / Math.max(1e-6, eps)
          : (corridor.elevationAt(nodeStation) - corridor.elevationAt(sIn)) / Math.max(1e-6, eps);
      const flip = corridor.flipped[edgeIndex] ? -1 : 1;
      const arm: Arm = {
        id: `arm_${edge.id}_${end}`,
        node,
        edge,
        corridor,
        end,
        dirOut,
        tangentOut: [dirOut[0], grade, dirOut[1]],
        profile: compileProfile(edge.spec),
        nodeStation,
        flip: flip as 1 | -1,
        pos: [node.x, node.z],
        edgeEnd: (flip === 1 ? 'a' : 'b') as 'a' | 'b',
      };
      if (!byNode.has(node.id)) byNode.set(node.id, []);
      byNode.get(node.id)!.push(arm);
    }
  }
  for (const list of byNode.values()) list.sort((a, b) => v2angle(a.dirOut) - v2angle(b.dirOut));
  return byNode;
}

/** Which corridor + edge index holds a given station (used by feature lookups). */
export function edgeAtStation(corridor: Corridor, s: number): { edge: Edge; index: number; local: number } {
  let i = 0;
  while (i < corridor.edges.length - 1 && corridor.nodeStations[i + 1] < s) i++;
  const start = corridor.nodeStations[i];
  const len = corridor.nodeStations[i + 1] - start;
  const local = corridor.flipped[i] ? len - (s - start) : s - start;
  return { edge: corridor.edges[i], index: i, local };
}

/** Superelevation with runout: banking follows curvature up to design speed and
 *  relaxes to zero approaching junctions. */
export function applyBanking(corridor: Corridor, junctionZones: Array<[number, number]>): void {
  const spec = corridor.spec;
  const v = Math.max(6, spec.speedLimit / 3.6);
  const g = 9.81;
  const maxE = Math.tan((spec.superElevationMax * Math.PI) / 180);
  const stations = corridor.path.adaptiveStations(Math.max(2, corridor.length / 30), 1);
  const raw: Array<{ s: number; e: number }> = [];
  for (const s of stations) {
    const k = corridor.path.curvatureAt(s);
    let e = clamp((-k * v * v) / g, -maxE, maxE);
    for (const [a, b] of junctionZones) {
      const d = s < a ? a - s : s > b ? s - b : 0;
      const runout = Math.abs(e) < 1e-5 ? 12 : Math.max(12, (Math.abs(e) / 0.005) * 1.2);
      e *= smootherstep(clamp(d / runout, 0, 1));
    }
    raw.push({ s, e });
  }
  for (let i = 1; i < raw.length; i++) {
    const ds = Math.max(1e-6, raw[i].s - raw[i - 1].s);
    const maxDelta = 0.004 * ds;
    const delta = raw[i].e - raw[i - 1].e;
    if (Math.abs(delta) > maxDelta) raw[i].e = raw[i - 1].e + Math.sign(delta) * maxDelta;
  }
  corridor.bankAt = (s: number) => {
    if (!raw.length) return 0;
    for (let i = 1; i < raw.length; i++) {
      if (raw[i].s >= s) {
        const t = clamp((s - raw[i - 1].s) / Math.max(1e-6, raw[i].s - raw[i - 1].s), 0, 1);
        return lerp(raw[i - 1].e, raw[i].e, t);
      }
    }
    return raw[raw.length - 1].e;
  };
}

// ---------------------------------------------------------------- profile modifiers

export function profileModsAt(corridor: Corridor, edge: Edge, stationInEdge: number, junctionTaper: number): Record<string, number> {
  const mods: Record<string, number> = {};
  const s = stationInEdge;
  for (const f of edge.features) {
    const half = Math.max(0.01, f.length / 2);
    const inside = s >= f.station - half && s <= f.station + half;
    const t = f.length > 0.01 ? clamp((s - (f.station - half)) / f.length, 0, 1) : 0.5;
    const ramp = Math.sin(clamp(t, 0, 1) * Math.PI);
    switch (f.kind) {
      case 'bus-bay':
      case 'bus-stop':
      case 'parking-bay':
      case 'loading-bay':
      case 'kerb-extension':
      case 'chicane': {
        if (!inside && f.kind !== 'kerb-extension') break;
        const w = f.width || 2.06;
        const key = f.side === 'left' ? 'widthAddLeft' : 'widthAddRight';
        mods[key] = (mods[key] ?? 0) + w * ramp;
        break;
      }
      case 'driveway':
      case 'field-entry':
      case 'crossing-zebra':
      case 'crossing-signal':
      case 'crossing-unsignalised':
      case 'crossing-raised': {
        if (!inside) break;
        const drop = 1;
        if (f.side === 'both' || f.side === 'left') mods.kerbDropLeft = Math.max(mods.kerbDropLeft ?? 0, drop);
        if (f.side === 'both' || f.side === 'right') mods.kerbDropRight = Math.max(mods.kerbDropRight ?? 0, drop);
        const fw = edge.spec.left.footway?.width ?? edge.spec.right.footway?.width ?? 1.2;
        mods.rampLength = Math.max(mods.rampLength ?? 1.2, Math.min(2.2, fw));
        break;
      }
      default:
        break;
    }
  }
  if (junctionTaper > 0) {
    mods.footwayAddLeft = (mods.footwayAddLeft ?? 0) + junctionTaper;
    mods.footwayAddRight = (mods.footwayAddRight ?? 0) + junctionTaper;
  }
  void corridor;
  return mods;
}

export function featureStationInCorridor(corridor: Corridor, edgeIndex: number, edgeStation: number): number {
  const start = corridor.nodeStations[edgeIndex];
  const len = corridor.nodeStations[edgeIndex + 1] - start;
  return corridor.flipped[edgeIndex] ? start + (len - edgeStation) : start + edgeStation;
}

/**
 * Sample the profile for a corridor station, resolving the owning edge, authored
 * features and the junction end states (median nose taper, walkway widening).
 */
export function profileAtCorridorStation(
  corridor: Corridor,
  cp: CompiledProfile,
  corridorStation: number,
  ends: EndState[] = [],
  _quality?: string,
): ProfileSample[] {
  const { edge, index, local } = edgeAtStation(corridor, corridorStation);
  const mods = profileModsAt(corridor, edge, local, 0) as Record<string, number>;
  for (const e of ends) {
    if (e.kind !== 'junction') continue;
    const d = Math.abs(corridorStation - e.mouth);
    if (e.medianTaper > 0 && d < e.medianTaper) {
      const base = cp.spec.medianWidth;
      const ramp = smootherstep(clamp(d / e.medianTaper, 0, 1));
      mods.medianWidth = Math.min(mods.medianWidth ?? base, base * ramp);
    }
    if (e.footwayTaper > 0 && d < 25) {
      const ramp = smootherstep(clamp(d / 25, 0, 1));
      mods.footwayAddLeft = (mods.footwayAddLeft ?? 0) + e.footwayTaper * ramp;
      mods.footwayAddRight = (mods.footwayAddRight ?? 0) + e.footwayTaper * ramp;
    }
    if (e.widthAdd > 0 && d < 40) {
      const ramp = smootherstep(clamp(d / 40, 0, 1));
      mods.widthAddRight = (mods.widthAddRight ?? 0) + e.widthAdd * ramp;
    }
  }
  void index;
  return profileAt(cp, mods as never);
}

/** Finished surface height at a corridor station and lateral offset (banked). */
export function surfaceHeight(corridor: Corridor, station: number, offset: number, profileY: number): number {
  return corridor.elevationAt(station) + profileY + Math.tan(corridor.bankAt(station)) * offset;
}

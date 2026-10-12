// Topology engine.
//
// Input : raw roads  [{ id, ctrl: [[x,z],...], profile, seed, spans: [{s, len, type}] }]
// Output: planar graph { nodes, edges, roads }
//
// Rules implemented here (no 3D booleans, no polygon booleans in the graph itself):
//  * Two road centrelines crossing at the same height  -> one shared junction node.
//    (+, T, Y, and | all reduce to the same rule: every contact point becomes a node
//     and every road is split into pieces between its contact nodes.)
//  * A road endpoint that lands on another road (within SNAP m) -> T/Y junction.
//  * A bridge span (grade separation) suppresses merging inside its interior, so the
//    crossing road passes underneath/over without sharing a node.
//  * Node degree 1 = end, 2 = pass-through, >=3 = junction.

import {
  sampleCatmull,
  withY,
  cumLen,
  pointAtS,
  sliceS,
  segIntersect,
  closestOnPolyline,
  smoothstep,
  clamp,
} from './geometry.js';
import { buildSection } from './profiles.js';

export const SNAP = 3.0; // endpoint-to-road snap distance (m)
export const CLUSTER = 2.5; // contacts closer than this become one node (m)
export const MIN_PIECE = 0.3; // discard pieces shorter than this (m)
export const SAMPLE_STEP = 2.5; // centreline sample spacing (m)

export const BRIDGE_TYPES = {
  girder: { label: 'Girder bridge', clearance: 6 },
  arch: { label: 'Stone arch viaduct', clearance: 9 },
  cable: { label: 'Cable-stayed bridge', clearance: 14 },
  viaduct: { label: 'Tall concrete viaduct', clearance: 12 },
  truss: { label: 'Steel truss bridge', clearance: 8 },
};

export function spanAt(road, s) {
  for (const sp of road.spans) if (s > sp.s0 && s < sp.s1) return sp;
  return null;
}

export function buildNetwork(rawRoads) {
  // 1. Sample every road into a dense polyline.
  const roads = [];
  for (const r of rawRoads) {
    if (!r.ctrl || r.ctrl.length < 2) continue;
    const pts = withY(sampleCatmull(r.ctrl, SAMPLE_STEP), 0);
    if (pts.length < 2) continue;
    const cum = cumLen(pts);
    const total = cum[cum.length - 1];
    if (total < 1) continue;
    const spans = (r.spans || [])
      .map((sp) => ({
        s0: clamp(sp.s - sp.len / 2, 0, total),
        s1: clamp(sp.s + sp.len / 2, 0, total),
        type: BRIDGE_TYPES[sp.type] ? sp.type : 'girder',
      }))
      .filter((sp) => sp.s1 - sp.s0 > 6)
      .sort((a, b) => a.s0 - b.s0);
    roads.push({
      idx: roads.length,
      id: r.id,
      profile: r.profile,
      seed: r.seed,
      props: r.props || {},
      pts,
      cum,
      total,
      spans,
      section: buildSection(r.profile, r.seed),
    });
  }

  // 2. Contact candidates: { x, z, refs: [{ r, s }] }
  const cands = [];
  const inSpanInterior = (road, s) => spanAt(road, s) !== null;

  // 2a. Proper crossings between segments (spatial hash).
  const CELL = 50;
  const grid = new Map();
  const segRef = [];
  for (const road of roads) {
    const { pts } = road;
    for (let i = 0; i < pts.length - 1; i++) {
      const gid = segRef.length;
      segRef.push({ r: road.idx, i });
      const x0 = Math.min(pts[i][0], pts[i + 1][0]);
      const x1 = Math.max(pts[i][0], pts[i + 1][0]);
      const z0 = Math.min(pts[i][1], pts[i + 1][1]);
      const z1 = Math.max(pts[i][1], pts[i + 1][1]);
      for (let cx = Math.floor(x0 / CELL); cx <= Math.floor(x1 / CELL); cx++) {
        for (let cz = Math.floor(z0 / CELL); cz <= Math.floor(z1 / CELL); cz++) {
          const key = cx + ',' + cz;
          let list = grid.get(key);
          if (!list) grid.set(key, (list = []));
          list.push(gid);
        }
      }
    }
  }
  const seen = new Set();
  for (const list of grid.values()) {
    for (let a = 0; a < list.length; a++) {
      for (let b = a + 1; b < list.length; b++) {
        const ga = list[a];
        const gb = list[b];
        const A = segRef[ga];
        const B = segRef[gb];
        if (A.r === B.r) continue;
        const key = ga < gb ? ga + ':' + gb : gb + ':' + ga;
        if (seen.has(key)) continue;
        seen.add(key);
        const ra = roads[A.r];
        const rb = roads[B.r];
        const p = ra.pts;
        const q = rb.pts;
        const hit = segIntersect(p[A.i][0], p[A.i][1], p[A.i + 1][0], p[A.i + 1][1], q[B.i][0], q[B.i][1], q[B.i + 1][0], q[B.i + 1][1]);
        if (!hit) continue;
        const sA = ra.cum[A.i] + Math.hypot(hit.x - p[A.i][0], hit.z - p[A.i][1]);
        const sB = rb.cum[B.i] + Math.hypot(hit.x - q[B.i][0], hit.z - q[B.i][1]);
        if (inSpanInterior(ra, sA) || inSpanInterior(rb, sB)) continue; // grade separated
        cands.push({ x: hit.x, z: hit.z, refs: [{ r: ra.idx, s: sA }, { r: rb.idx, s: sB }] });
      }
    }
  }

  // 2b. Road endpoints that land on another road (T / Y / end-to-end joins).
  for (const road of roads) {
    for (const end of [0, road.total]) {
      const P = end === 0 ? road.pts[0] : road.pts[road.pts.length - 1];
      if (inSpanInterior(road, end)) continue;
      let best = null;
      for (const other of roads) {
        if (other === road) continue;
        if (P[0] < minX(other) - SNAP || P[0] > maxX(other) + SNAP) continue;
        if (P[1] < minZ(other) - SNAP || P[1] > maxZ(other) + SNAP) continue;
        const c = closestOnPolyline(other.pts, other.cum, P[0], P[1]);
        if (c.d <= SNAP && !inSpanInterior(other, c.s) && (!best || c.d < best.c.d)) best = { other, c };
      }
      if (best) {
        cands.push({
          x: best.c.x,
          z: best.c.z,
          refs: [{ r: road.idx, s: end }, { r: best.other.idx, s: best.c.s }],
        });
      }
    }
  }

  // 2c. Bridge span boundaries become pass-through nodes (ground level).
  for (const road of roads) {
    for (const sp of road.spans) {
      for (const s of [sp.s0, sp.s1]) {
        const p = pointAtS(road.pts, road.cum, s);
        cands.push({ x: p[0], z: p[1], refs: [{ r: road.idx, s }], spanBoundary: true });
      }
    }
  }

  // 3. Cluster candidates into nodes (union-find on distance).
  const parent = cands.map((_, i) => i);
  const find = (i) => (parent[i] === i ? i : (parent[i] = find(parent[i])));
  for (let i = 0; i < cands.length; i++) {
    for (let j = i + 1; j < cands.length; j++) {
      if (Math.hypot(cands[i].x - cands[j].x, cands[i].z - cands[j].z) <= CLUSTER) {
        parent[find(i)] = find(j);
      }
    }
  }
  const groups = new Map();
  cands.forEach((c, i) => {
    const g = find(i);
    if (!groups.has(g)) groups.set(g, []);
    groups.get(g).push(c);
  });

  const nodes = [];
  const cuts = roads.map(() => []); // per road: { s, node }
  for (const members of groups.values()) {
    const x = members.reduce((a, m) => a + m.x, 0) / members.length;
    const z = members.reduce((a, m) => a + m.z, 0) / members.length;
    const id = nodes.length;
    nodes.push({ id, x, z, y: 0, edges: [], kind: 'end' });
    for (const m of members) for (const ref of m.refs) cuts[ref.r].push({ s: ref.s, node: id });
  }

  // 4. Road ends that have no node yet get their own end node.
  for (const road of roads) {
    for (const end of [0, road.total]) {
      const has = cuts[road.idx].some((c) => Math.abs(c.s - end) < 0.05);
      if (!has) {
        const P = end === 0 ? road.pts[0] : road.pts[road.pts.length - 1];
        const id = nodes.length;
        nodes.push({ id, x: P[0], z: P[1], y: 0, edges: [], kind: 'end' });
        cuts[road.idx].push({ s: end, node: id });
      }
    }
  }

  // 5. Split every road at its cuts and create edges.
  const edges = [];
  for (const road of roads) {
    const cs = cuts[road.idx].slice().sort((a, b) => a.s - b.s);
    const uniq = [];
    for (const c of cs) {
      if (!uniq.length || c.s - uniq[uniq.length - 1].s >= MIN_PIECE) uniq.push(c);
    }
    for (let k = 0; k < uniq.length - 1; k++) {
      const a = uniq[k];
      const b = uniq[k + 1];
      if (b.s - a.s < MIN_PIECE || a.node === b.node) continue;
      const span = spanAt(road, (a.s + b.s) / 2);
      const pts = sliceS(road.pts, road.cum, a.s, b.s);
      const pc = cumLen(pts);
      const L = pc[pc.length - 1] || 1;
      const na = nodes[a.node];
      const nb = nodes[b.node];
      const clearance = span ? BRIDGE_TYPES[span.type].clearance : 0;
      for (let v = 0; v < pts.length; v++) {
        if (span) {
          const sAbs = a.s + (pc[v] / L) * (b.s - a.s);
          const t = (sAbs - span.s0) / (span.s1 - span.s0);
          pts[v][2] = clearance * smoothstep(Math.min(t, 1 - t) / 0.25);
        } else {
          pts[v][2] = 0;
        }
      }
      pts[0] = [na.x, na.z, nodes[a.node].y];
      pts[pts.length - 1] = [nb.x, nb.z, nodes[b.node].y];
      const edge = {
        id: 'e' + edges.length,
        a: a.node,
        b: b.node,
        road: road.id,
        roadIdx: road.idx,
        profile: road.profile,
        seed: road.seed,
        section: road.section,
        bridge: span ? span.type : null,
        length: L,
        pts,
      };
      edges.push(edge);
      na.edges.push(edge.id);
      nb.edges.push(edge.id);
    }
  }

  // 6. Classify nodes and compact away empty ones.
  const remap = new Map();
  const finalNodes = [];
  for (const n of nodes) {
    if (n.edges.length === 0) continue;
    const deg = n.edges.length;
    n.kind = deg === 1 ? 'end' : deg === 2 ? 'pass' : 'junction';
    remap.set(n.id, finalNodes.length);
    n.id = finalNodes.length;
    finalNodes.push(n);
  }
  for (const e of edges) {
    e.a = remap.get(e.a);
    e.b = remap.get(e.b);
  }

  return {
    roads: roads.map((r) => ({ id: r.id, idx: r.idx, profile: r.profile, seed: r.seed, pts: r.pts, cum: r.cum, total: r.total, spans: r.spans, section: r.section })),
    nodes: finalNodes,
    edges,
    stats: {
      roads: roads.length,
      nodes: finalNodes.length,
      junctions: finalNodes.filter((n) => n.kind === 'junction').length,
      ends: finalNodes.filter((n) => n.kind === 'end').length,
      edges: edges.length,
      bridges: edges.filter((e) => e.bridge).length,
    },
  };
}

function minX(r) {
  return r._minX ?? (r._minX = Math.min(...r.pts.map((p) => p[0])));
}
function maxX(r) {
  return r._maxX ?? (r._maxX = Math.max(...r.pts.map((p) => p[0])));
}
function minZ(r) {
  return r._minZ ?? (r._minZ = Math.min(...r.pts.map((p) => p[1])));
}
function maxZ(r) {
  return r._maxZ ?? (r._maxZ = Math.max(...r.pts.map((p) => p[1])));
}

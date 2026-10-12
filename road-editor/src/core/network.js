// Road topology.
//
// Roads are only ever *connected* when they share the same level (same layer key).
// Every same-level crossing / T-contact / endpoint contact becomes a graph NODE, and each road
// is split into EDGES between nodes. The result is a clean planar graph: edges only touch at
// nodes, never at random points (no overlapping or accidentally intersecting roads).
//
// Things on a different level (bridges over roads, subways under roads, rail lines crossing
// roads) are grade-separated: they keep crossing in plan view, nothing is merged.
// Railways never merge; a rail crossing a ground road becomes a level crossing.
import { catmullRom, cumulative, polylineCrossings, closestOnPolyline, bbox, bboxOverlap, dist2 } from './geom.js';
import { DEFAULTS } from './presets.js';

export function layerKey(road) {
  if (road.kind === 'rail') return `rail:${road.id}`;
  switch (road.layer.mode) {
    case 'bridge': return `bridge:${road.layer.clearance ?? DEFAULTS.clearance}`;
    case 'subway': return 'subway';
    default: return 'ground';
  }
}

export const isMergeable = (road) => road.kind !== 'rail';

/** Build the planar road graph. Pure function: model -> { roads, edges, nodes, crossings, warnings }. */
export function buildNetwork(model, opts = {}) {
  const snap = opts.snap ?? DEFAULTS.snap;
  const spacing = opts.spacing ?? DEFAULTS.spacing;
  const warnings = [];

  // 1. Sample every road centreline.
  const roads = [];
  for (const road of model.roads) {
    if (road.points.length < 2) continue;
    const pts = catmullRom(road.points, spacing);
    if (pts.length < 2) continue;
    const s = cumulative(pts);
    roads.push({ road, pts, s, L: s[s.length - 1], key: layerKey(road), mergeable: isMergeable(road), bbox: bbox(pts) });
  }

  // 2. Collect entries: every endpoint, every same-level crossing, every T-contact.
  const entries = [];
  const addEntry = (ri, s, x, z, kind) => {
    const R = roads[ri];
    entries.push({ ri, s, x, z, kind, mergeable: R.mergeable, key: R.key, node: -1 });
  };
  roads.forEach((R, ri) => {
    addEntry(ri, 0, R.pts[0].x, R.pts[0].z, 'end');
    addEntry(ri, R.L, R.pts[R.pts.length - 1].x, R.pts[R.pts.length - 1].z, 'end');
  });

  const crossings = [];   // non-merged crossings (grade separation / level crossings), used for props
  const overlapPairs = [];
  for (let i = 0; i < roads.length; i++) {
    for (let j = i + 1; j < roads.length; j++) {
      const A = roads[i], B = roads[j];
      if (!bboxOverlap(A.bbox, B.bbox, snap)) continue;
      const sameLevel = A.key === B.key && A.mergeable && B.mergeable;

      if (sameLevel) {
        const hits = polylineCrossings(A.pts, A.s, B.pts, B.s);
        for (const h of hits) {
          addEntry(i, h.sA, h.x, h.z, 'cross');
          addEntry(j, h.sB, h.x, h.z, 'cross');
        }
        // Coincident (overlapping) stretches are a topology error: report them.
        const overlap = countCoincident(A, B, 0.6);
        if (overlap >= 4) {
          overlapPairs.push([A.road.id, B.road.id]);
          warnings.push(`Overlapping roads: "${A.road.name}" runs on top of "${B.road.name}". Remove one of them.`);
        }
      } else {
        const hits = polylineCrossings(A.pts, A.s, B.pts, B.s);
        for (const h of hits) {
          let kind = 'grade';
          if (A.road.kind === 'rail' || B.road.kind === 'rail') {
            const other = A.road.kind === 'rail' ? B : A;
            if (A.road.kind === 'rail' && B.road.kind === 'rail') kind = 'rail-rail';
            else kind = other.key === 'ground' ? 'level-crossing' : 'grade';
          }
          crossings.push({ kind, x: h.x, z: h.z, a: A.road.id, b: B.road.id, sA: h.sA, sB: h.sB, tA: tangentAt(A, h.sA), tB: tangentAt(B, h.sB) });
        }
      }

      // T-contacts: an endpoint of one same-level road lands on the body of the other.
      if (sameLevel) {
        for (const [P, Q, qi] of [[A, B, j], [B, A, i]]) {
          for (const end of [P.pts[0], P.pts[P.pts.length - 1]]) {
            const c = closestOnPolyline(Q.pts, Q.s, end);
            if (c.dist <= snap && c.s > 0.5 && c.s < Q.L - 0.5) addEntry(qi, c.s, c.x, c.z, 'T');
          }
        }
      }
    }
  }

  // 3. Cluster entries into nodes (union-find over a spatial hash).
  const parent = entries.map((_, k) => k);
  const find = (k) => { while (parent[k] !== k) { parent[k] = parent[parent[k]]; k = parent[k]; } return k; };
  const union = (a, b) => { a = find(a); b = find(b); if (a !== b) parent[a] = b; };
  const cell = snap;
  const grid = new Map();
  entries.forEach((e, k) => {
    const cx = Math.floor(e.x / cell), cz = Math.floor(e.z / cell);
    for (let dx = -1; dx <= 1; dx++) for (let dz = -1; dz <= 1; dz++) {
      const list = grid.get(`${cx + dx},${cz + dz}`);
      if (!list) continue;
      for (const o of list) {
        const E = entries[o];
        if (dist2(E, e) > snap) continue;
        if (!compatible(E, e)) continue;
        union(k, o);
      }
    }
    const key = `${cx},${cz}`;
    if (!grid.has(key)) grid.set(key, []);
    grid.get(key).push(k);
  });

  const clusters = new Map();
  entries.forEach((e, k) => {
    const r = find(k);
    if (!clusters.has(r)) clusters.set(r, []);
    clusters.get(r).push(e);
  });

  // 4. Nodes. Position: prefer geometric contacts (crossings / T) so edges stay on their roads.
  const nodes = [];
  for (const list of clusters.values()) {
    const geo = list.filter((e) => e.kind !== 'end');
    const src = geo.length ? geo : list;
    const x = src.reduce((a, e) => a + e.x, 0) / src.length;
    const z = src.reduce((a, e) => a + e.z, 0) / src.length;
    const id = nodes.length;
    nodes.push({ id, x, z, edges: [], y: 0 });
    for (const e of list) e.node = id;
  }

  // 5. Split each road between consecutive nodes -> edges.
  const edges = [];
  const perRoad = roads.map(() => []);
  entries.forEach((e) => perRoad[e.ri].push(e));
  roads.forEach((R, ri) => {
    const list = perRoad[ri].slice().sort((a, b) => a.s - b.s);
    for (let k = 0; k < list.length - 1; k++) {
      const e0 = list[k], e1 = list[k + 1];
      if (e1.s - e0.s < 0.5) continue; // coincident contacts on the same road
      const n0 = nodes[e0.node], n1 = nodes[e1.node];
      const pts = [{ x: n0.x, z: n0.z, s: e0.s }];
      for (let i = 0; i < R.pts.length; i++) {
        if (R.s[i] > e0.s + 1e-6 && R.s[i] < e1.s - 1e-6) pts.push({ x: R.pts[i].x, z: R.pts[i].z, s: R.s[i] });
      }
      pts.push({ x: n1.x, z: n1.z, s: e1.s });
      const id = edges.length;
      const edge = {
        id,
        roadId: R.road.id,
        road: R.road,
        roadL: R.L,
        key: R.key,
        a: n0.id,
        b: n1.id,
        pts: dedupePts(pts),
        s0: e0.s,
        s1: e1.s,
        length: e1.s - e0.s,
      };
      if (edge.pts.length < 2) continue;
      edges.push(edge);
      n0.edges.push({ edge: id, end: 'a' });
      n1.edges.push({ edge: id, end: 'b' });
    }
  });

  // Sanity: a node with a single road shows the road ends there (dead end).
  for (const n of nodes) n.degree = n.edges.length;

  return {
    roads: roads.map((R) => ({ id: R.road.id, road: R.road, pts: R.pts, s: R.s, L: R.L, key: R.key })),
    edges,
    nodes,
    crossings,
    warnings,
    stats: { roads: roads.length, edges: edges.length, nodes: nodes.length, junctions: nodes.filter((n) => n.degree >= 3).length, grade: crossings.length },
  };
}

function compatible(a, b) {
  if (!a.mergeable || !b.mergeable) return false;
  if (a.key === b.key) return true;
  // Endpoints of different levels may meet (a ramp joins a bridge to the ground).
  return a.kind === 'end' && b.kind === 'end';
}

function tangentAt(R, s) {
  let i = 0;
  while (i < R.s.length - 2 && R.s[i + 1] < s) i++;
  const a = R.pts[i], b = R.pts[i + 1];
  const L = Math.hypot(b.x - a.x, b.z - a.z) || 1;
  return { x: (b.x - a.x) / L, z: (b.z - a.z) / L };
}

/** How many samples of A lie on B (within tol). Used to detect overlapping roads. */
function countCoincident(A, B, tol) {
  let n = 0;
  const step = Math.max(1, Math.floor(A.pts.length / 40));
  const bb = B.bbox;
  for (let i = 0; i < A.pts.length; i += step) {
    const p = A.pts[i];
    if (p.x < bb.minX - tol || p.x > bb.maxX + tol || p.z < bb.minZ - tol || p.z > bb.maxZ + tol) continue;
    const c = closestOnPolyline(B.pts, B.s, p);
    if (c.dist < tol) n++;
  }
  return n;
}

function dedupePts(pts) {
  const out = [];
  for (const p of pts) {
    const q = out[out.length - 1];
    if (!q || dist2(q, p) > 0.02) out.push(p);
  }
  return out;
}

/** Given an edge and an s value on its road, which edge is that? (helper for props) */
export function edgeAtRoadS(net, roadId, s) {
  return net.edges.find((e) => e.roadId === roadId && s >= e.s0 - 1e-6 && s <= e.s1 + 1e-6) || null;
}

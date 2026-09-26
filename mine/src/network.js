// Spline network: junction NODES + tunnel EDGES (each edge is a centripetal
// Catmull-Rom spline through  nodeA -> control points -> nodeB).
// Everything downstream (cave mesh, road, grooves/tracks, supports, lamps,
// cart lanes, collision) is derived from this data, so moving any node or
// control point regenerates the whole mine consistently.
import * as THREE from 'three';
import { mulberry32 } from './noise.js';

export const DEFAULT_PARAMS = {
  seed: 7,
  roadHalfWidth: 4.0,     // m, half width of the paved road
  wallBulge: 0.35,        // m, how far walls bow outward at the spring line
  springHeight: 3.0,      // m, where the wall turns into the roof arch
  roofHeight: 5.8,        // m, crown of the arch
  laneOffset: 1.75,       // m, cart lane center from tunnel axis
  gauge: 1.0,             // m, rail gauge
  grooveDepth: 0.13,      // m
  ringSpacing: 0.75,      // m, distance between tunnel edge-loops
  wallSegs: 5,            // edge loops up each wall
  roofSegs: 12,           // edge loops across the arch (even)
  filletSegs: 3,          // k: each junction corner gets 2k segments
  rockNoise: 0.42,        // m, rock displacement amplitude
  bankMax: 0.10,          // rad, max road banking in curves
  supportSpacing: 7.0,    // m
  lampEvery: 2,           // lamp on every Nth support
  cartCount: 26,
};

export function createMaze(seed = 7, spacing = 58) {
  const rnd = mulberry32(seed);
  const cols = 5, rows = 4;
  const nodes = [];
  const id = (c, r) => r * cols + c;
  for (let r = 0; r < rows; r++) {
    for (let c = 0; c < cols; c++) {
      const jx = (rnd() - 0.5) * 16, jz = (rnd() - 0.5) * 16;
      const h = Math.sin(c * 1.3 + seed) * 3.2 + Math.cos(r * 1.7 + seed * 0.5) * 2.6;
      nodes.push({ id: id(c, r), p: [c * spacing + jx - (cols - 1) * spacing / 2, h, r * spacing + jz - (rows - 1) * spacing / 2] });
    }
  }
  const edges = [];
  const has = new Set();
  const addEdge = (a, b, cps) => {
    const key = Math.min(a, b) + '-' + Math.max(a, b);
    if (has.has(key)) return;
    has.add(key);
    edges.push({ id: edges.length, a, b, cps });
  };
  const wiggle = (a, b, n) => {
    const A = new THREE.Vector3(...nodes[a].p), B = new THREE.Vector3(...nodes[b].p);
    const d = B.clone().sub(A);
    const side = new THREE.Vector3(-d.z, 0, d.x).normalize();
    const cps = [];
    for (let i = 1; i <= n; i++) {
      const t = i / (n + 1);
      const p = A.clone().lerp(B, t);
      p.addScaledVector(side, (rnd() - 0.5) * 12);
      p.y += (rnd() - 0.5) * 3.6;
      cps.push([p.x, p.y, p.z]);
    }
    return cps;
  };

  // interior edges that may be removed to make it more maze-like
  const interiorEdges = [];
  for (let r = 0; r < rows; r++) {
    for (let c = 0; c < cols; c++) {
      if (c < cols - 1) {
        const interior = r > 0 && r < rows - 1 && c > 0 && c < cols - 2;
        (interior ? interiorEdges : []).push([id(c, r), id(c + 1, r)]);
      }
      if (r < rows - 1) {
        const interior = c > 0 && c < cols - 1 && r > 0 && r < rows - 2;
        (interior ? interiorEdges : []).push([id(c, r), id(c, r + 1)]);
      }
    }
  }
  const removed = new Set();
  const protectedKey = id(2, 0) + '-' + id(2, 1); // overpass edge stays
  const shuffled = interiorEdges.slice().sort(() => rnd() - 0.5);
  const degree = new Array(nodes.length).fill(4); // interior nodes start with 4
  for (const [a, b] of shuffled) {
    if (removed.size >= 2) break;
    const key = Math.min(a, b) + '-' + Math.max(a, b);
    if (key === protectedKey) continue;
    if (degree[a] <= 3 || degree[b] <= 3) continue; // every junction keeps >= 3 tunnels
    degree[a]--; degree[b]--;
    removed.add(key);
  }

  for (let r = 0; r < rows; r++) {
    for (let c = 0; c < cols; c++) {
      const pairs = [];
      if (c < cols - 1) pairs.push([id(c, r), id(c + 1, r)]);
      if (r < rows - 1) pairs.push([id(c, r), id(c, r + 1)]);
      for (const [a, b] of pairs) {
        const key = Math.min(a, b) + '-' + Math.max(a, b);
        if (removed.has(key)) continue;
        if (key === protectedKey) continue; // added below with a crest
        addEdge(a, b, wiggle(a, b, rnd() < 0.5 ? 1 : 2));
      }
    }
  }
  // corner nodes only have 2 grid neighbours -> give them a diagonal (criss-cross)
  const corners = [[0, 0, 1, 1], [cols - 1, 0, cols - 2, 1], [0, rows - 1, 1, rows - 2], [cols - 1, rows - 1, cols - 2, rows - 2]];
  for (const [c, r, c2, r2] of corners) addEdge(id(c, r), id(c2, r2), wiggle(id(c, r), id(c2, r2), 1));

  // --- OVERPASS: tunnel (2,0)-(2,1) crests high while a diagonal (1,0)->(3,1) dives under it
  const nA = nodes[id(2, 0)], nB = nodes[id(2, 1)];
  nA.p[1] = 1.5; nB.p[1] = 1.0;
  const mid = [(nA.p[0] + nB.p[0]) / 2, 5.0, (nA.p[2] + nB.p[2]) / 2];
  addEdge(id(2, 0), id(2, 1), [mid]);
  const d0 = nodes[id(1, 0)], d1 = nodes[id(3, 1)];
  d0.p[1] = 0; d1.p[1] = 0;
  const L = (t) => [d0.p[0] + (d1.p[0] - d0.p[0]) * t, d0.p[2] + (d1.p[2] - d0.p[2]) * t];
  const cross = [mid[0], -6.5, mid[2]];
  const q1 = L(0.22), q3 = L(0.78);
  addEdge(id(1, 0), id(3, 1), [
    [q1[0] - 4, -1.8, q1[1] + 6],
    cross,
    [q3[0] + 4, -1.8, q3[1] - 6],
  ]);

  return { nodes, edges };
}

// ---------- spline evaluation ----------
export class EdgeSpline {
  constructor(net, edge) {
    const raw = [net.nodes[edge.a].p, ...edge.cps, net.nodes[edge.b].p].map((p) => new THREE.Vector3(...p));
    // "approach" points: tunnels leave a junction straight and level for a few
    // metres, so hub floors meet the tunnels without kinks (no accidental ramps)
    const lead = (A, B) => {
      const d = new THREE.Vector3(B.x - A.x, 0, B.z - A.z);
      const len = d.length();
      const k = Math.min(13, len * 0.28);
      return new THREE.Vector3(A.x, A.y, A.z).addScaledVector(d.normalize(), k);
    };
    const A = raw[0], B = raw[raw.length - 1];
    const pts = [A, lead(A, raw[1]), ...raw.slice(1, -1), lead(B, raw[raw.length - 2]), B];
    this.curve = new THREE.CatmullRomCurve3(pts, false, 'centripetal');
    this.curve.arcLengthDivisions = Math.max(200, Math.ceil(pts.length * 160));
    this.length = this.curve.getLength();
  }
  // arclength-parametrised sample
  pointAt(s, target = new THREE.Vector3()) {
    const u = THREE.MathUtils.clamp(s / this.length, 0, 1);
    return this.curve.getPointAt(u, target);
  }
  tangentAt(s, target = new THREE.Vector3()) {
    const u = THREE.MathUtils.clamp(s / this.length, 0, 1);
    return this.curve.getTangentAt(u, target).normalize();
  }
  // signed horizontal curvature (for banking)
  curvatureAt(s) {
    const h = 2.0;
    const t0 = this.tangentAt(Math.max(0, s - h));
    const t1 = this.tangentAt(Math.min(this.length, s + h));
    const c = t0.x * t1.z - t0.z * t1.x; // cross y
    return c / (2 * h);
  }
}

export function cloneNet(net) {
  return JSON.parse(JSON.stringify(net));
}

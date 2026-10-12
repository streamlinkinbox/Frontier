// Planarize polylines in 2D: find all crossings, create shared nodes, split into edges.
import { segIntersect } from './math.js';

export function planarize(polys, { tol = 1.5, cell = 120 } = {}) {
  // segments
  const segs = [];
  polys.forEach((p, pi) => {
    for (let i = 0; i < p.pts.length - 1; i++) segs.push({ pi, i, a: p.pts[i], b: p.pts[i + 1] });
  });
  const grid = new Map();
  const key = (x, z) => x * 92837111 ^ z * 689287499;
  segs.forEach((s, si) => {
    const x0 = Math.floor(Math.min(s.a[0], s.b[0]) / cell), x1 = Math.floor(Math.max(s.a[0], s.b[0]) / cell);
    const z0 = Math.floor(Math.min(s.a[1], s.b[1]) / cell), z1 = Math.floor(Math.max(s.a[1], s.b[1]) / cell);
    for (let x = x0; x <= x1; x++) for (let z = z0; z <= z1; z++) { const k = key(x, z); let b = grid.get(k); if (!b) grid.set(k, b = []); b.push(si); }
  });
  const cuts = polys.map(p => p.pts.map(() => []));
  const tested = new Set();
  for (const bucket of grid.values()) {
    for (let x = 0; x < bucket.length; x++) for (let y = x + 1; y < bucket.length; y++) {
      const si = bucket[x], sj = bucket[y];
      const pk = si < sj ? si * 1e7 + sj : sj * 1e7 + si;
      if (tested.has(pk)) continue; tested.add(pk);
      const A = segs[si], B = segs[sj];
      if (A.pi === B.pi && Math.abs(A.i - B.i) <= 1) continue;
      const pa = polys[A.pi], pb = polys[B.pi];
      if (pa.layer !== pb.layer) continue;
      const hit = segIntersect(A.a[0], A.a[1], A.b[0], A.b[1], B.a[0], B.a[1], B.b[0], B.b[1], 1e-4);
      if (!hit) continue;
      cuts[A.pi][A.i].push(Math.min(1, Math.max(0, hit[0])));
      cuts[B.pi][B.i].push(Math.min(1, Math.max(0, hit[1])));
    }
  }
  // nodes with snapping
  const nodes = [], nodeGrid = new Map();
  const ncell = Math.max(tol * 4, 4);
  const getNode = (x, z) => {
    const gx = Math.floor(x / ncell), gz = Math.floor(z / ncell);
    for (let dx = -1; dx <= 1; dx++) for (let dz = -1; dz <= 1; dz++) {
      const b = nodeGrid.get(key(gx + dx, gz + dz));
      if (b) for (const n of b) if (Math.hypot(n.x - x, n.z - z) < tol) return n;
    }
    const n = { id: nodes.length, x, z, deg: 0 };
    nodes.push(n);
    const k = key(gx, gz); let b = nodeGrid.get(k); if (!b) nodeGrid.set(k, b = []); b.push(n);
    return n;
  };
  const edges = [];
  polys.forEach((p, pi) => {
    // ordered points with node flags
    const seq = [];
    for (let i = 0; i < p.pts.length; i++) {
      seq.push({ x: p.pts[i][0], z: p.pts[i][1], node: i === 0 || i === p.pts.length - 1 });
      if (i < p.pts.length - 1) {
        const cs = [...new Set(cuts[pi][i].map(t => Math.round(t * 1e6) / 1e6))].sort((a, b) => a - b);
        for (const t of cs) {
          if (t <= 1e-6) { seq[seq.length - 1].node = true; continue; }
          if (t >= 1 - 1e-6) { seq.push({ x: p.pts[i + 1][0], z: p.pts[i + 1][1], node: true, dupNext: true }); continue; }
          seq.push({ x: p.pts[i][0] + (p.pts[i + 1][0] - p.pts[i][0]) * t, z: p.pts[i][1] + (p.pts[i + 1][1] - p.pts[i][1]) * t, node: true });
        }
      }
    }
    // collapse dupNext markers onto the following vertex
    const clean = [];
    for (let k = 0; k < seq.length; k++) {
      const s = seq[k];
      if (s.dupNext) { if (seq[k + 1]) seq[k + 1].node = true; continue; }
      const last = clean[clean.length - 1];
      if (last && Math.hypot(last.x - s.x, last.z - s.z) < 1e-6) { last.node = last.node || s.node; continue; }
      clean.push(s);
    }
    let start = null, inner = [];
    for (const s of clean) {
      if (s.node) {
        const n = getNode(s.x, s.z);
        if (start && start !== n) {
          edges.push({ a: start.id, b: n.id, pts: inner.map(q => [q.x, q.z]), src: p });
        }
        if (!start || start !== n) { start = n; inner = []; }
      } else inner.push(s);
    }
  });
  // dedupe identical edges
  const seen = new Set(), out = [];
  for (const e of edges) {
    const k = e.a < e.b ? e.a + ':' + e.b : e.b + ':' + e.a;
    const k2 = k + ':' + (e.pts.length ? Math.round(e.pts[0][0]) + ',' + Math.round(e.pts[0][1]) : '');
    if (seen.has(k2)) continue; seen.add(k2);
    out.push(e);
  }
  for (const e of out) { nodes[e.a].deg++; nodes[e.b].deg++; }
  return { nodes, edges: out };
}

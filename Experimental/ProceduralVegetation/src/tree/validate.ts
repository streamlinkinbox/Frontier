/**
 * Topology validator.
 *
 * A production tree mesh must be a closed 2-manifold: every edge shared by
 * exactly two faces with opposite orientation, no isolated vertices, and for a
 * single closed surface of genus 0, V - E + F = 2. This module computes all
 * of that from the quad mesh so it can be asserted in tests and displayed in
 * the UI as a topology report.
 */

import { QuadMesh } from './mesh';

export interface TopologyReport {
  vertices: number;
  edges: number;
  faces: number;
  quads: number;
  tris: number;
  quadRatio: number;
  boundaryEdges: number;
  nonManifoldEdges: number;
  inconsistentEdges: number;
  isolatedVertices: number;
  eulerCharacteristic: number;
  components: number;
  /** Genus, assuming a closed connected surface (undefined otherwise). */
  genus?: number;
  closed: boolean;
  manifold: boolean;
  /** Number of vertices with valence other than 4 (poles). */
  poles: number;
  valenceHistogram: Record<number, number>;
  degenerateFaces: number;
  minEdgeLength: number;
}

export function validateTopology(mesh: QuadMesh): TopologyReport {
  const V = mesh.vertexCount;
  const edgeCount = new Map<number, number>();
  const edgeDir = new Map<number, number>(); // +1 for (a<b) direction, -1 otherwise
  const valence = new Uint32Array(V);
  const used = new Uint8Array(V);
  const pos = mesh.positions;
  let degenerate = 0;
  let minEdge = Infinity;

  const key = (a: number, b: number): number => (a < b ? a * V + b : b * V + a);

  const processFace = (ids: number[]): void => {
    const n = ids.length;
    // Degenerate check: any repeated index.
    for (let i = 0; i < n; i++) for (let j = i + 1; j < n; j++) if (ids[i] === ids[j]) degenerate++;
    for (let i = 0; i < n; i++) {
      const a = ids[i];
      const b = ids[(i + 1) % n];
      used[a] = 1;
      const k = key(a, b);
      const c = edgeCount.get(k) ?? 0;
      if (c === 0) {
        valence[a]++;
        valence[b]++;
      }
      edgeCount.set(k, c + 1);
      edgeDir.set(k, (edgeDir.get(k) ?? 0) + (a < b ? 1 : -1));
      const dx = pos[a * 3] - pos[b * 3];
      const dy = pos[a * 3 + 1] - pos[b * 3 + 1];
      const dz = pos[a * 3 + 2] - pos[b * 3 + 2];
      const l = Math.sqrt(dx * dx + dy * dy + dz * dz);
      if (l < minEdge) minEdge = l;
    }
  };

  const q = mesh.quads;
  for (let f = 0; f < q.length; f += 4) processFace([q[f], q[f + 1], q[f + 2], q[f + 3]]);
  const t = mesh.tris;
  for (let f = 0; f < t.length; f += 3) processFace([t[f], t[f + 1], t[f + 2]]);

  let boundary = 0;
  let nonManifold = 0;
  let inconsistent = 0;
  for (const [k, c] of edgeCount) {
    if (c === 1) boundary++;
    else if (c > 2) nonManifold++;
    else if (edgeDir.get(k) !== 0) inconsistent++;
  }

  let isolated = 0;
  for (let i = 0; i < V; i++) if (!used[i]) isolated++;

  const E = edgeCount.size;
  const F = mesh.quadCount + mesh.triCount;
  const chi = V - isolated - E + F;

  // Connected components via union-find on edges.
  const parent = new Int32Array(V);
  for (let i = 0; i < V; i++) parent[i] = i;
  const find = (x: number): number => {
    while (parent[x] !== x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
    }
    return x;
  };
  const union = (a: number, b: number): void => {
    const ra = find(a);
    const rb = find(b);
    if (ra !== rb) parent[ra] = rb;
  };
  for (let f = 0; f < q.length; f += 4) {
    union(q[f], q[f + 1]);
    union(q[f + 1], q[f + 2]);
    union(q[f + 2], q[f + 3]);
  }
  for (let f = 0; f < t.length; f += 3) {
    union(t[f], t[f + 1]);
    union(t[f + 1], t[f + 2]);
  }
  const roots = new Set<number>();
  for (let i = 0; i < V; i++) if (used[i]) roots.add(find(i));
  const components = roots.size;

  const hist: Record<number, number> = {};
  let poles = 0;
  for (let i = 0; i < V; i++) {
    if (!used[i]) continue;
    const v = valence[i];
    hist[v] = (hist[v] ?? 0) + 1;
    if (v !== 4) poles++;
  }

  const closed = boundary === 0;
  const manifold = nonManifold === 0 && inconsistent === 0 && degenerate === 0;
  const report: TopologyReport = {
    vertices: V,
    edges: E,
    faces: F,
    quads: mesh.quadCount,
    tris: mesh.triCount,
    quadRatio: F > 0 ? mesh.quadCount / F : 1,
    boundaryEdges: boundary,
    nonManifoldEdges: nonManifold,
    inconsistentEdges: inconsistent,
    isolatedVertices: isolated,
    eulerCharacteristic: chi,
    components,
    closed,
    manifold,
    poles,
    valenceHistogram: hist,
    degenerateFaces: degenerate,
    minEdgeLength: isFinite(minEdge) ? minEdge : 0,
  };
  if (closed && manifold && components === 1) report.genus = (2 - chi) / 2;
  return report;
}

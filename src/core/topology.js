// Topology helpers: quad-only edge extraction (for a true quad wireframe)
// and connectivity analysis (proves the network is ONE connected mesh).

/** Unique edges of the quad faces. Returns Float32Array of line positions. */
export function quadEdgesToLines(positions, quads) {
  const seen = new Set();
  const out = [];
  const push = (a, b) => {
    const key = a < b ? `${a}_${b}` : `${b}_${a}`;
    if (seen.has(key)) return;
    seen.add(key);
    out.push(
      positions[a * 3], positions[a * 3 + 1], positions[a * 3 + 2],
      positions[b * 3], positions[b * 3 + 1], positions[b * 3 + 2]
    );
  };
  for (const q of quads) {
    for (let i = 0; i < 4; i++) push(q[i], q[(i + 1) % 4]);
  }
  return new Float32Array(out);
}

/** Count connected components over the quad mesh (shared-vertex topology). */
export function connectedComponents(vertexCount, quads) {
  const parent = new Int32Array(vertexCount);
  for (let i = 0; i < vertexCount; i++) parent[i] = i;
  const find = (x) => {
    while (parent[x] !== x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
    }
    return x;
  };
  const union = (a, b) => {
    const ra = find(a);
    const rb = find(b);
    if (ra !== rb) parent[rb] = ra;
  };
  for (const q of quads) {
    union(q[0], q[1]);
    union(q[1], q[2]);
    union(q[2], q[3]);
    union(q[3], q[0]);
  }
  const roots = new Set();
  for (let i = 0; i < vertexCount; i++) roots.add(find(i));
  return roots.size;
}

/** Number of unique vertices (position-welded), to prove seams are shared. */
export function uniqueVertexCount(positions, vertexCount) {
  const seen = new Set();
  for (let i = 0; i < vertexCount; i++) {
    seen.add(
      `${Math.round(positions[i * 3] * 1e4)}_${Math.round(positions[i * 3 + 1] * 1e4)}_${Math.round(positions[i * 3 + 2] * 1e4)}`
    );
  }
  return seen.size;
}

/** Degenerate quads (repeated index or ~zero area). */
export function degenerateQuads(positions, quads) {
  let n = 0;
  for (const q of quads) {
    if (q[0] === q[1] || q[1] === q[2] || q[2] === q[3] || q[3] === q[0] || q[0] === q[2] || q[1] === q[3]) {
      n++;
      continue;
    }
    // area via cross of diagonals
    const ax = positions[q[2] * 3] - positions[q[0] * 3];
    const ay = positions[q[2] * 3 + 1] - positions[q[0] * 3 + 1];
    const az = positions[q[2] * 3 + 2] - positions[q[0] * 3 + 2];
    const bx = positions[q[3] * 3] - positions[q[1] * 3];
    const by = positions[q[3] * 3 + 1] - positions[q[1] * 3 + 1];
    const bz = positions[q[3] * 3 + 2] - positions[q[1] * 3 + 2];
    const cx = ay * bz - az * by;
    const cy = az * bx - ax * bz;
    const cz = ax * by - ay * bx;
    if (cx * cx + cy * cy + cz * cz < 1e-10) n++;
  }
  return n;
}

/**
 * Edge valence histogram: how many quads share each topological edge.
 * A crack-free surface has only 2-valence edges (interior) and 1-valence
 * edges (open borders like tunnel portals). Anything >=3 is a defect.
 */
export function edgeValence(quads) {
  const count = new Map();
  for (const q of quads) {
    for (let i = 0; i < 4; i++) {
      const a = q[i];
      const b = q[(i + 1) % 4];
      const key = a < b ? `${a}_${b}` : `${b}_${a}`;
      count.set(key, (count.get(key) || 0) + 1);
    }
  }
  const hist = {};
  for (const v of count.values()) hist[v] = (hist[v] || 0) + 1;
  return hist;
}

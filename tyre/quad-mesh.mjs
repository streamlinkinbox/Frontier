// Pure-quad tyre surface builder + topology validator.
//
// A tyre cross-section is an open polyline (bead → sidewall → shoulder → tread → shoulder → sidewall → bead).
// Revolving it around the axis gives a single structured grid of N rows × M columns:
//   * every face is a quad (no triangles, no T-junctions, no separate tread/sidewall seams)
//   * columns wrap around the circumference (periodic), rows run bead to bead
//   * the tread pattern is applied as a per-vertex displacement along the profile normal
//
// No dependencies. Works in browsers (ES module) and Node.

/**
 * @typedef {{x:number, r:number, nx:number, nr:number}} ProfilePoint
 *   x = axial position (mm), r = radial distance from the axis (mm),
 *   (nx, nr) = unit outward normal of the cross-section at that point.
 * @typedef {object} TyreQuadMesh
 * @property {number} rows      profile samples (N)
 * @property {number} cols      circumferential samples (M), columns wrap
 * @property {Float32Array} positions  3 floats per vertex, index = row*cols + col
 * @property {Float32Array} normals    3 floats per vertex
 * @property {Float32Array} uvs        2 floats per vertex (u = around, v = across)
 * @property {Uint32Array} quads       4 indices per face, counter-clockwise seen from outside
 */

const TAU = Math.PI * 2;

/**
 * Build the quad grid.
 * @param {object} o
 * @param {ProfilePoint[]} o.profile  open cross-section, bead to bead (>= 2 points)
 * @param {number} o.cols             circumferential resolution (>= 3)
 * @param {(u:number, v:number, row:number, p:ProfilePoint) => number} [o.displace]
 *        signed offset in mm along the profile normal (positive = outward). u∈[0,1), v∈[0,1].
 */
export function buildTyreQuadMesh({ profile, cols, displace = () => 0 }) {
  if (!Array.isArray(profile) || profile.length < 2) throw new Error('profile needs >= 2 points');
  if (!(cols >= 3)) throw new Error('cols must be >= 3');
  const rows = profile.length, M = cols | 0;
  const N = rows * M;
  const pos = new Float32Array(N * 3), uvs = new Float32Array(N * 2);

  for (let i = 0; i < rows; i++) {
    const p = profile[i];
    const v = i / (rows - 1);
    for (let j = 0; j < M; j++) {
      const u = j / M, th = u * TAU;
      const off = displace(u, v, i, p) || 0;
      const x = p.x + p.nx * off, r = p.r + p.nr * off;
      const k = i * M + j;
      pos[k * 3] = x;
      pos[k * 3 + 1] = r * Math.cos(th);
      pos[k * 3 + 2] = r * Math.sin(th);
      uvs[k * 2] = u;
      uvs[k * 2 + 1] = v;
    }
  }

  // One quad per (row band, column). Columns wrap, so there are no seam duplicates.
  const quads = new Uint32Array((rows - 1) * M * 4);
  let q = 0;
  for (let i = 0; i < rows - 1; i++) {
    for (let j = 0; j < M; j++) {
      const a = i * M + j, b = i * M + ((j + 1) % M);
      const c = (i + 1) * M + j, d = (i + 1) * M + ((j + 1) % M);
      // winding chosen so the face normal points along the profile normal (outward)
      quads[q++] = a; quads[q++] = b; quads[q++] = d; quads[q++] = c;
    }
  }

  const mesh = { rows, cols: M, positions: pos, normals: null, uvs, quads };
  mesh.normals = computeVertexNormals(pos, quads);
  return mesh;
}

/** Area-weighted vertex normals for a quad mesh. */
export function computeVertexNormals(pos, quads) {
  const n = new Float32Array(pos.length);
  for (let f = 0; f < quads.length; f += 4) {
    const i0 = quads[f] * 3, i1 = quads[f + 1] * 3, i2 = quads[f + 2] * 3, i3 = quads[f + 3] * 3;
    // split quad into two triangles (0,1,2) and (0,2,3); cross products are area-weighted
    accumTri(pos, n, i0, i1, i2);
    accumTri(pos, n, i0, i2, i3);
  }
  for (let i = 0; i < n.length; i += 3) {
    const l = Math.hypot(n[i], n[i + 1], n[i + 2]) || 1;
    n[i] /= l; n[i + 1] /= l; n[i + 2] /= l;
  }
  return n;
}

function accumTri(p, n, a, b, c) {
  const ux = p[b] - p[a], uy = p[b + 1] - p[a + 1], uz = p[b + 2] - p[a + 2];
  const vx = p[c] - p[a], vy = p[c + 1] - p[a + 1], vz = p[c + 2] - p[a + 2];
  const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
  for (const k of [a, b, c]) { n[k] += nx; n[k + 1] += ny; n[k + 2] += nz; }
}

/** Two triangles per quad, for renderers that only take triangles (e.g. three.js index buffers). */
export function quadsToTriangles(quads) {
  const tris = new Uint32Array((quads.length / 4) * 6);
  for (let f = 0, t = 0; f < quads.length; f += 4) {
    const a = quads[f], b = quads[f + 1], c = quads[f + 2], d = quads[f + 3];
    tris[t++] = a; tris[t++] = b; tris[t++] = c;
    tris[t++] = a; tris[t++] = c; tris[t++] = d;
  }
  return tris;
}

/**
 * Topology report for a quad mesh.
 * Checks: every face is a non-degenerate quad, no edge shared by >2 faces (non-manifold),
 * consistent orientation (each directed edge used at most once), boundary loops, Euler characteristic.
 * @returns {{ok:boolean, faces:number, vertices:number, edges:number, boundaryEdges:number,
 *            interiorEdges:number, nonManifoldEdges:number, orientationErrors:number,
 *            degenerateFaces:number, boundaryLoops:number, euler:number, genus:number|null,
 *            triangleOnly:boolean, problems:string[]}}
 */
export function validateQuadMesh(mesh) {
  const { quads, positions } = mesh;
  const V = positions.length / 3, F = quads.length / 4;
  const problems = [];
  const undirected = new Map(); // "min,max" -> count
  const directed = new Set();   // "a>b"
  let orientationErrors = 0, degenerate = 0, badIndex = 0, repeatedVerts = 0;

  for (let f = 0; f < quads.length; f += 4) {
    const v = [quads[f], quads[f + 1], quads[f + 2], quads[f + 3]];
    if (v.some(x => x >= V)) badIndex++;
    if (new Set(v).size !== 4) { repeatedVerts++; continue; }
    // degenerate if the quad's diagonal cross product is ~0
    if (quadArea(positions, v) < 1e-12) degenerate++;
    for (let e = 0; e < 4; e++) {
      const a = v[e], b = v[(e + 1) % 4];
      const key = a < b ? `${a},${b}` : `${b},${a}`;
      undirected.set(key, (undirected.get(key) || 0) + 1);
      const dk = `${a}>${b}`;
      if (directed.has(dk)) orientationErrors++;
      directed.add(dk);
    }
  }
  if (badIndex) problems.push(`${badIndex} faces reference missing vertices`);
  if (repeatedVerts) problems.push(`${repeatedVerts} faces repeat a vertex`);
  if (degenerate) problems.push(`${degenerate} degenerate (zero-area) faces`);

  let boundary = 0, interior = 0, nonManifold = 0;
  for (const c of undirected.values()) {
    if (c === 1) boundary++; else if (c === 2) interior++; else nonManifold++;
  }
  if (nonManifold) problems.push(`${nonManifold} non-manifold edges (shared by >2 faces)`);
  if (orientationErrors) problems.push(`${orientationErrors} edges with inconsistent winding`);

  const E = undirected.size;
  const euler = V - E + F;
  // Genus only meaningful for closed surfaces; a tyre cross-section with open beads has boundary.
  const genus = boundary === 0 ? (2 - euler) / 2 : null;
  const boundaryLoops = countBoundaryLoops(undirected, quads);

  return {
    ok: problems.length === 0,
    faces: F, vertices: V, edges: E,
    boundaryEdges: boundary, interiorEdges: interior, nonManifoldEdges: nonManifold,
    orientationErrors, degenerateFaces: degenerate, boundaryLoops, euler, genus,
    triangleOnly: false, problems,
  };
}

function quadArea(p, v) {
  const [a, b, c, d] = v.map(i => [p[i * 3], p[i * 3 + 1], p[i * 3 + 2]]);
  const cr = (u, w) => [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]];
  const s = (x, y) => [x[0] - y[0], x[1] - y[1], x[2] - y[2]];
  const n = cr(s(c, a), s(d, b));
  return Math.hypot(n[0], n[1], n[2]) * 0.5;
}

function countBoundaryLoops(undirected, quads) {
  // Walk boundary edges (count 1) into loops using the face directed edges.
  const next = new Map();
  for (let f = 0; f < quads.length; f += 4) {
    const v = [quads[f], quads[f + 1], quads[f + 2], quads[f + 3]];
    for (let e = 0; e < 4; e++) {
      const a = v[e], b = v[(e + 1) % 4];
      const key = a < b ? `${a},${b}` : `${b},${a}`;
      if (undirected.get(key) === 1) next.set(b, a); // reversed: boundary runs opposite to face winding
    }
  }
  let loops = 0; const seen = new Set();
  for (const start of next.keys()) {
    if (seen.has(start)) continue;
    loops++;
    let cur = start;
    while (cur !== undefined && !seen.has(cur)) { seen.add(cur); cur = next.get(cur); }
  }
  return loops;
}

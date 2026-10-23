/**
 * Quad-native mesh container used by the mesher, the validator and the exporters.
 *
 * Faces are stored as quads (4 indices) plus a small number of triangles (only
 * at the strip caps of stem tips). UVs are face-varying (per corner) so the
 * cylindrical seam does not require duplicated vertices. Everything else is
 * per vertex.
 */

export interface VertexWind {
  /** 0..1 normalised height, drives trunk sway. */
  height: number;
  /** 0..1 distance along the limb from its root, drives limb bending. */
  limb: number;
  /** 0..1 random phase of the limb. */
  phase: number;
  /** 0..1 fine-detail flutter weight (twigs and leaves). */
  detail: number;
}

export class QuadMesh {
  positions: number[] = [];
  /** Per-vertex: height, limb, phase, detail. */
  wind: number[] = [];
  /** Per-vertex limb pivot (root of the limb this vertex belongs to). */
  pivots: number[] = [];
  /** Per-vertex stem level (0 = trunk). */
  levels: number[] = [];
  /** Per-vertex junction flag (1 = belongs to a branch collar / crotch). */
  junction: number[] = [];

  quads: number[] = [];
  quadUVs: number[] = [];
  tris: number[] = [];
  triUVs: number[] = [];

  get vertexCount(): number {
    return this.positions.length / 3;
  }
  get quadCount(): number {
    return this.quads.length / 4;
  }
  get triCount(): number {
    return this.tris.length / 3;
  }

  addVertex(x: number, y: number, z: number, wind: VertexWind, pivot: { x: number; y: number; z: number }, level: number, junction = 0): number {
    const idx = this.positions.length / 3;
    this.positions.push(x, y, z);
    this.wind.push(wind.height, wind.limb, wind.phase, wind.detail);
    this.pivots.push(pivot.x, pivot.y, pivot.z);
    this.levels.push(level);
    this.junction.push(junction);
    return idx;
  }

  setPosition(i: number, x: number, y: number, z: number): void {
    this.positions[i * 3] = x;
    this.positions[i * 3 + 1] = y;
    this.positions[i * 3 + 2] = z;
  }

  /**
   * Emit a quad. `uv` holds 4 (u, v) pairs; u values are unwrapped relative to
   * the first corner so seam-crossing faces stay continuous.
   */
  addQuad(a: number, b: number, c: number, d: number, uv: number[]): void {
    if (a < 0 || b < 0 || c < 0 || d < 0) throw new Error(`addQuad: reference to a skipped vertex (${a}, ${b}, ${c}, ${d})`);
    if (a === b || b === c || c === d || d === a || a === c || b === d) {
      // Degenerate quad: emit as triangle(s) if possible.
      const ids = [a, b, c, d];
      const uniq = ids.filter((v, i) => ids.indexOf(v) === i);
      if (uniq.length === 3) {
        const uvs: number[] = [];
        for (const u of uniq) {
          const k = ids.indexOf(u);
          uvs.push(uv[k * 2], uv[k * 2 + 1]);
        }
        this.addTri(uniq[0], uniq[1], uniq[2], uvs);
      }
      return;
    }
    this.quads.push(a, b, c, d);
    const u0 = uv[0];
    this.quadUVs.push(u0, uv[1]);
    for (let k = 1; k < 4; k++) {
      let u = uv[k * 2];
      while (u - u0 > 0.5) u -= 1;
      while (u0 - u > 0.5) u += 1;
      this.quadUVs.push(u, uv[k * 2 + 1]);
    }
  }

  addTri(a: number, b: number, c: number, uv: number[]): void {
    if (a < 0 || b < 0 || c < 0) throw new Error(`addTri: reference to a skipped vertex (${a}, ${b}, ${c})`);
    if (a === b || b === c || c === a) return;
    this.tris.push(a, b, c);
    const u0 = uv[0];
    this.triUVs.push(u0, uv[1]);
    for (let k = 1; k < 3; k++) {
      let u = uv[k * 2];
      while (u - u0 > 0.5) u -= 1;
      while (u0 - u > 0.5) u += 1;
      this.triUVs.push(u, uv[k * 2 + 1]);
    }
  }

  /** Area-weighted smooth vertex normals (quads use the diagonal cross product). */
  computeNormals(): Float32Array {
    const n = this.vertexCount;
    const normals = new Float32Array(n * 3);
    const p = this.positions;
    const q = this.quads;
    for (let f = 0; f < q.length; f += 4) {
      const a = q[f] * 3;
      const b = q[f + 1] * 3;
      const c = q[f + 2] * 3;
      const d = q[f + 3] * 3;
      const e1x = p[c] - p[a];
      const e1y = p[c + 1] - p[a + 1];
      const e1z = p[c + 2] - p[a + 2];
      const e2x = p[d] - p[b];
      const e2y = p[d + 1] - p[b + 1];
      const e2z = p[d + 2] - p[b + 2];
      const nx = e1y * e2z - e1z * e2y;
      const ny = e1z * e2x - e1x * e2z;
      const nz = e1x * e2y - e1y * e2x;
      for (const v of [a, b, c, d]) {
        normals[v] += nx;
        normals[v + 1] += ny;
        normals[v + 2] += nz;
      }
    }
    const t = this.tris;
    for (let f = 0; f < t.length; f += 3) {
      const a = t[f] * 3;
      const b = t[f + 1] * 3;
      const c = t[f + 2] * 3;
      const e1x = p[b] - p[a];
      const e1y = p[b + 1] - p[a + 1];
      const e1z = p[b + 2] - p[a + 2];
      const e2x = p[c] - p[a];
      const e2y = p[c + 1] - p[a + 1];
      const e2z = p[c + 2] - p[a + 2];
      const nx = e1y * e2z - e1z * e2y;
      const ny = e1z * e2x - e1x * e2z;
      const nz = e1x * e2y - e1y * e2x;
      for (const v of [a, b, c]) {
        normals[v] += nx;
        normals[v + 1] += ny;
        normals[v + 2] += nz;
      }
    }
    for (let i = 0; i < n; i++) {
      const x = normals[i * 3];
      const y = normals[i * 3 + 1];
      const z = normals[i * 3 + 2];
      const l = Math.sqrt(x * x + y * y + z * z) || 1;
      normals[i * 3] = x / l;
      normals[i * 3 + 1] = y / l;
      normals[i * 3 + 2] = z / l;
    }
    return normals;
  }
}

/** Simple leaf card container: each leaf is one quad with its own 4 vertices. */
export class LeafMesh {
  positions: number[] = [];
  normals: number[] = [];
  uvs: number[] = [];
  wind: number[] = [];
  pivots: number[] = [];
  indices: number[] = [];

  /** Number of leaf cards (each card is a 5-vertex kite = 3 triangles). */
  get count(): number {
    return this.indices.length / 9;
  }
}

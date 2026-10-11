// Vertex/index builder. Every surface is emitted as QUADS (two triangles on the GPU)
// wherever the geometry allows it; triangles are only used for the innermost fan of a junction
// core and for a few tapered caps. The builder records topology statistics so the UI can
// report the real quad/triangle ratio and any folded or concave faces.
//
// Vertex layout (12 floats = 48 bytes):
//   pos.xyz, nrm.xyz, uv.xz (world metres, used for texture tiling), col.rgb, mat
// mat >= 0 : texture array layer (0 asphalt, 1 concrete, 2 pavers)
// mat -1   : vertex colour, lit
// mat -2   : emissive (lamps)
// mat -3   : procedural ground
// mat -4   : unlit overlay (gizmos, handles)

export const STRIDE_FLOATS = 12;

export class MeshBuilder {
  constructor() {
    this.data = [];
    this.indices = [];
    this.vertexCount = 0;
    this.stats = { quads: 0, tris: 0, degenerate: 0, concave: 0 };
  }

  vertex(p, n, col, mat) {
    const d = this.data;
    d.push(p[0], p[1], p[2], n[0], n[1], n[2], p[0], p[2], col[0], col[1], col[2], mat);
    return this.vertexCount++;
  }

  // Quad a,b,c,d in order around the face. Optional `expect` is the intended normal;
  // used for fold/concavity statistics and the default flat normal.
  quad(a0, b0, c0, d0, { col = [1, 1, 1], mat = -1, expect = null, normals = null } = {}) {
    // Face normal from the diagonals: robust for planar and mildly non-planar quads.
    let a = a0, b = b0, c = c0, d = d0;
    let nrm = normalize(cross([c[0] - a[0], c[1] - a[1], c[2] - a[2]], [d[0] - b[0], d[1] - b[1], d[2] - b[2]]));
    if (!nrm) { this.stats.degenerate++; return false; }
    const e = expect ? normalize(expect) : null;
    let nn = nrm;
    if (e && dot(nrm, e) < 0) {
      // Reverse winding so the face agrees with the intended normal.
      [b, d] = [d, b];
      nn = scale(nrm, -1);
    }
    if (!this._convexXZ(a, b, c, d, e)) this.stats.concave++;
    this.stats.quads++;
    const fallback = e || nn;
    const na = normals ? normals[0] : fallback;
    const nb = normals ? normals[1] : fallback;
    const nc = normals ? normals[2] : fallback;
    const nd = normals ? normals[3] : fallback;
    const i0 = this.vertex(a, na, col, mat);
    const i1 = this.vertex(b, nb, col, mat);
    const i2 = this.vertex(c, nc, col, mat);
    const i3 = this.vertex(d, nd, col, mat);
    this.indices.push(i0, i1, i2, i0, i2, i3);
    return true;
  }

  tri(a, b, c, { col = [1, 1, 1], mat = -1, expect = null, normals = null } = {}) {
    const fn = normalize(cross([b[0] - a[0], b[1] - a[1], b[2] - a[2]], [c[0] - a[0], c[1] - a[1], c[2] - a[2]]));
    if (!fn) { this.stats.degenerate++; return false; }
    let p = [a, b, c];
    let nn = fn;
    if (expect) {
      const e = normalize(expect);
      if (e && dot(fn, e) < 0) { p = [a, c, b]; nn = scale(fn, -1); }
    }
    this.stats.tris++;
    const n0 = normals ? normals[0] : nn;
    const n1 = normals ? normals[1] : nn;
    const n2 = normals ? normals[2] : nn;
    const i0 = this.vertex(p[0], n0, col, mat);
    const i1 = this.vertex(p[1], n1, col, mat);
    const i2 = this.vertex(p[2], n2, col, mat);
    this.indices.push(i0, i1, i2);
    return true;
  }

  _convexXZ(a, b, c, d, expect) {
    // Only meaningful for upward-facing quads: check the XZ projection is convex.
    if (!expect || Math.abs(expect[1]) < 0.7) return true;
    const pts = [a, b, c, d];
    let sign = 0;
    for (let i = 0; i < 4; i++) {
      const p0 = pts[i], p1 = pts[(i + 1) % 4], p2 = pts[(i + 2) % 4];
      const cz = (p1[0] - p0[0]) * (p2[2] - p1[2]) - (p1[2] - p0[2]) * (p2[0] - p1[0]);
      if (Math.abs(cz) < 1e-9) continue;
      const s = Math.sign(cz);
      if (sign === 0) sign = s;
      else if (s !== sign) return false;
    }
    return true;
  }

  merge(other) {
    const base = this.vertexCount;
    for (const v of other.data) this.data.push(v);
    for (const i of other.indices) this.indices.push(i + base);
    this.vertexCount += other.vertexCount;
    for (const k of Object.keys(this.stats)) this.stats[k] += other.stats[k] || 0;
    return this;
  }

  get triangleCount() { return this.indices.length / 3; }

  build() {
    return {
      vertices: new Float32Array(this.data),
      indices: this.indices.length > 65535 ? new Uint32Array(this.indices) : new Uint32Array(this.indices),
      vertexCount: this.vertexCount,
      indexCount: this.indices.length,
      stats: { ...this.stats },
    };
  }
}

export function cross(a, b) { return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]; }
export function dot(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
export function len(a) { return Math.hypot(a[0], a[1], a[2]); }
export function scale(a, s) { return [a[0] * s, a[1] * s, a[2] * s]; }
export function normalize(a) {
  const l = Math.hypot(a[0], a[1], a[2]);
  return l > 1e-12 ? [a[0] / l, a[1] / l, a[2] / l] : null;
}

// Quad-first mesh builder. Faces are stored as quads (4 indices) or triangles (3 indices) and only
// triangulated when uploaded to the GPU, so exports (OBJ) keep the authored quad topology.
import { add, sub, cross, norm, len, mul, dot } from "./math.js";

export class MeshBuilder {
  /**
   * @param {string} owner  id of the scene object that owns this geometry (picking / selection)
   */
  constructor(owner = "") {
    this.owner = owner;
    this.positions = []; // flat xyz
    this.uvs = []; // flat uv
    this.colors = []; // flat rgba (vertex mask channels: r=wear, g=ao, b=dirt/variation, a=blend)
    this.normals = null; // computed lazily (flat xyz)
    this.explicitNormals = new Map(); // vertex index -> normal (when authored)
    this.faces = []; // {m: materialIndex, i: [a,b,c,(d)], group}
    this.materials = []; // material names
    this._matIndex = new Map();
    this.vertexCount = 0;
  }
  material(name) {
    let i = this._matIndex.get(name);
    if (i === undefined) { i = this.materials.length; this.materials.push(name); this._matIndex.set(name, i); }
    return i;
  }
  vertex(p, uv = [0, 0], color = [0, 1, 0, 0], normal = null) {
    const i = this.vertexCount++;
    this.positions.push(p[0], p[1], p[2]);
    this.uvs.push(uv[0], uv[1]);
    this.colors.push(color[0], color[1], color[2], color[3]);
    if (normal) this.explicitNormals.set(i, normal);
    return i;
  }
  position(i) { const P = this.positions; return [P[i * 3], P[i * 3 + 1], P[i * 3 + 2]]; }
  setPosition(i, p) { const P = this.positions; P[i * 3] = p[0]; P[i * 3 + 1] = p[1]; P[i * 3 + 2] = p[2]; }
  quad(mat, a, b, c, d, group = this.owner) {
    if (a === b || b === c || c === d || d === a) { // degenerate → triangle (or skip)
      const u = [...new Set([a, b, c, d])];
      if (u.length === 3) this.faces.push({ m: mat, i: u, g: group });
      return;
    }
    this.faces.push({ m: mat, i: [a, b, c, d], g: group });
  }
  tri(mat, a, b, c, group = this.owner) { if (a !== b && b !== c && c !== a) this.faces.push({ m: mat, i: [a, b, c], g: group }); }
  /**
   * Connect consecutive rows of vertex indices with quads. rows[i] and rows[i+1] must have equal length.
   * Winding: with rows advancing along +forward and columns advancing along +right, faces point up (+Y).
   */
  grid(mat, rows, group = this.owner, flip = false) {
    for (let i = 0; i + 1 < rows.length; i++) {
      const r0 = rows[i], r1 = rows[i + 1];
      const n = Math.min(r0.length, r1.length);
      for (let j = 0; j + 1 < n; j++) {
        if (flip) this.quad(mat, r0[j], r0[j + 1], r1[j + 1], r1[j], group);
        else this.quad(mat, r0[j], r1[j], r1[j + 1], r0[j + 1], group);
      }
    }
  }
  /**
   * Orientation-safe grid: emits quads for a row/column lattice and flips the winding if the summed
   * Newell normal of the lattice points down. matOrFn: material index or (columnIndex) => material index.
   */
  gridAuto(matOrFn, rows, group = this.owner) {
    const P = this.positions;
    let ny = 0;
    const faces = [];
    for (let i = 0; i + 1 < rows.length; i++) {
      const r0 = rows[i], r1 = rows[i + 1];
      const n = Math.min(r0.length, r1.length);
      for (let j = 0; j + 1 < n; j++) {
        const q = [r0[j], r1[j], r1[j + 1], r0[j + 1]];
        // Newell y component
        let y = 0;
        for (let k = 0; k < 4; k++) { const a = q[k] * 3, b = q[(k + 1) % 4] * 3; y += (P[a + 2] - P[b + 2]) * (P[a] + P[b]); }
        ny += y;
        faces.push([j, q]);
      }
    }
    const flip = ny < 0;
    for (const [j, q] of faces) {
      const m = typeof matOrFn === "function" ? matOrFn(j) : matOrFn;
      if (flip) this.quad(m, q[0], q[3], q[2], q[1], group); else this.quad(m, q[0], q[1], q[2], q[3], group);
    }
    return !flip;
  }
  /**
   * Grid whose winding is chosen so the first non-degenerate quad's normal points along `ref`
   * (a vector, or a function (quadCentre) => vector, e.g. "away from the centroid").
   */
  gridFacing(matOrFn, rows, group = this.owner, ref = [0, 1, 0]) {
    const P = this.positions;
    let flip = false;
    outer: for (let i = 0; i + 1 < rows.length; i++) {
      const r0 = rows[i], r1 = rows[i + 1], n = Math.min(r0.length, r1.length);
      for (let j = 0; j + 1 < n; j++) {
        const q = [r0[j], r1[j], r1[j + 1], r0[j + 1]];
        let nx = 0, ny = 0, nz = 0, cx = 0, cy = 0, cz = 0;
        for (let k = 0; k < 4; k++) { const a = q[k] * 3, b = q[(k + 1) % 4] * 3; nx += (P[a + 1] - P[b + 1]) * (P[a + 2] + P[b + 2]); ny += (P[a + 2] - P[b + 2]) * (P[a] + P[b]); nz += (P[a] - P[b]) * (P[a + 1] + P[b + 1]); cx += P[a] / 4; cy += P[a + 1] / 4; cz += P[a + 2] / 4; }
        const l = Math.hypot(nx, ny, nz); if (l < 1e-10) continue;
        const r = typeof ref === "function" ? ref([cx, cy, cz]) : ref;
        flip = nx * r[0] + ny * r[1] + nz * r[2] < 0;
        break outer;
      }
    }
    for (let i = 0; i + 1 < rows.length; i++) {
      const r0 = rows[i], r1 = rows[i + 1], n = Math.min(r0.length, r1.length);
      for (let j = 0; j + 1 < n; j++) {
        const m = typeof matOrFn === "function" ? matOrFn(j) : matOrFn;
        if (flip) this.quad(m, r0[j], r0[j + 1], r1[j + 1], r1[j], group); else this.quad(m, r0[j], r1[j], r1[j + 1], r0[j + 1], group);
      }
    }
    return !flip;
  }
  /** Record vertex welds (from → to). Applied by applyWelds(). */
  weld(from, to) { (this.weldMap ||= new Map()).set(from, to); }
  applyWelds() {
    if (!this.weldMap) return;
    const resolve = (v) => { let g = 0; while (this.weldMap.has(v) && g++ < 16) v = this.weldMap.get(v); return v; };
    for (const f of this.faces) for (let k = 0; k < f.i.length; k++) f.i[k] = resolve(f.i[k]);
    this.weldMap = null;
    this.normals = null;
  }
  /** Drop unreferenced vertices (after welds). */
  compact() {
    const used = new Uint8Array(this.vertexCount);
    for (const f of this.faces) for (const v of f.i) used[v] = 1;
    const remap = new Int32Array(this.vertexCount);
    const P = [], U = [], C = [], EN = new Map();
    let n = 0;
    for (let i = 0; i < this.vertexCount; i++) {
      if (!used[i]) { remap[i] = -1; continue; }
      remap[i] = n++;
      P.push(this.positions[i * 3], this.positions[i * 3 + 1], this.positions[i * 3 + 2]);
      U.push(this.uvs[i * 2], this.uvs[i * 2 + 1]);
      C.push(this.colors[i * 4], this.colors[i * 4 + 1], this.colors[i * 4 + 2], this.colors[i * 4 + 3]);
      const en = this.explicitNormals.get(i); if (en) EN.set(remap[i], en);
    }
    for (const f of this.faces) for (let k = 0; k < f.i.length; k++) f.i[k] = remap[f.i[k]];
    this.positions = P; this.uvs = U; this.colors = C; this.explicitNormals = EN; this.vertexCount = n; this.normals = null;
    return this;
  }
  /** Fan a closed polygon of vertex indices (used only for props/caps — road surfaces use grids). */
  fan(mat, ring, group = this.owner, flip = false) {
    for (let i = 1; i + 1 < ring.length; i++) {
      if (flip) this.tri(mat, ring[0], ring[i + 1], ring[i], group);
      else this.tri(mat, ring[0], ring[i], ring[i + 1], group);
    }
  }
  /** Append another builder's geometry (materials remapped). */
  append(other, transform = null) {
    const base = this.vertexCount;
    const n = other.vertexCount;
    for (let i = 0; i < n; i++) {
      let p = other.position(i);
      if (transform) p = transform(p);
      this.positions.push(p[0], p[1], p[2]);
      this.uvs.push(other.uvs[i * 2], other.uvs[i * 2 + 1]);
      this.colors.push(other.colors[i * 4], other.colors[i * 4 + 1], other.colors[i * 4 + 2], other.colors[i * 4 + 3]);
      const en = other.explicitNormals.get(i);
      if (en) this.explicitNormals.set(base + i, en);
    }
    this.vertexCount += n;
    const matMap = other.materials.map((m) => this.material(m));
    for (const f of other.faces) this.faces.push({ m: matMap[f.m], i: f.i.map((k) => k + base), g: f.g || other.owner });
    this.normals = null;
    return this;
  }
  translate(d) { for (let i = 0; i < this.vertexCount; i++) { this.positions[i * 3] += d[0]; this.positions[i * 3 + 1] += d[1]; this.positions[i * 3 + 2] += d[2]; } return this; }
  transform(fn, dirFn = null) {
    for (let i = 0; i < this.vertexCount; i++) this.setPosition(i, fn(this.position(i)));
    if (dirFn) for (const [k, nrm] of this.explicitNormals) this.explicitNormals.set(k, norm(dirFn(nrm)));
    this.normals = null;
    return this;
  }
  bounds() {
    const b = { min: [Infinity, Infinity, Infinity], max: [-Infinity, -Infinity, -Infinity] };
    for (let i = 0; i < this.vertexCount; i++) for (let k = 0; k < 3; k++) { const v = this.positions[i * 3 + k]; if (v < b.min[k]) b.min[k] = v; if (v > b.max[k]) b.max[k] = v; }
    return b;
  }
  /** Area-weighted smooth normals. Vertices are only shared where the author wants smoothing. */
  computeNormals() {
    const N = new Float32Array(this.vertexCount * 3);
    const P = this.positions;
    for (const f of this.faces) {
      const idx = f.i;
      // Newell's method for the face normal (robust for non-planar quads)
      let nx = 0, ny = 0, nz = 0;
      for (let k = 0; k < idx.length; k++) {
        const a = idx[k] * 3, b = idx[(k + 1) % idx.length] * 3;
        nx += (P[a + 1] - P[b + 1]) * (P[a + 2] + P[b + 2]);
        ny += (P[a + 2] - P[b + 2]) * (P[a] + P[b]);
        nz += (P[a] - P[b]) * (P[a + 1] + P[b + 1]);
      }
      for (const v of idx) { N[v * 3] += nx; N[v * 3 + 1] += ny; N[v * 3 + 2] += nz; }
    }
    for (let i = 0; i < this.vertexCount; i++) {
      const en = this.explicitNormals.get(i);
      let x = N[i * 3], y = N[i * 3 + 1], z = N[i * 3 + 2];
      if (en) { x = en[0]; y = en[1]; z = en[2]; }
      const l = Math.hypot(x, y, z) || 1;
      N[i * 3] = x / l; N[i * 3 + 1] = y / l; N[i * 3 + 2] = z / l;
    }
    this.normals = N;
    return N;
  }
  stats() {
    let q = 0, t = 0;
    for (const f of this.faces) if (f.i.length === 4) q++; else t++;
    return { vertices: this.vertexCount, quads: q, triangles: t, quadRatio: q + t ? q / (q + t) : 1 };
  }
  /**
   * GPU upload format: one interleaved buffer per material.
   * layout: pos(3) normal(3) uv(2) color(4) = 12 floats
   */
  toGPU() {
    if (!this.normals) this.computeNormals();
    const byMat = new Map(); // key: material|owner-group → triangle index list
    for (const f of this.faces) {
      const key = f.m + "|" + (f.g || "");
      let arr = byMat.get(key);
      if (!arr) { arr = []; arr.m = f.m; arr.g = f.g || this.owner; byMat.set(key, arr); }
      const i = f.i;
      if (i.length === 4) {
        // split along the shorter diagonal for better shading on non-planar quads
        const a = this.position(i[0]), b = this.position(i[1]), c = this.position(i[2]), d = this.position(i[3]);
        const d1 = (a[0] - c[0]) ** 2 + (a[1] - c[1]) ** 2 + (a[2] - c[2]) ** 2;
        const d2 = (b[0] - d[0]) ** 2 + (b[1] - d[1]) ** 2 + (b[2] - d[2]) ** 2;
        if (d1 <= d2) arr.push(i[0], i[1], i[2], i[0], i[2], i[3]);
        else arr.push(i[1], i[2], i[3], i[1], i[3], i[0]);
      } else arr.push(i[0], i[1], i[2]);
    }
    const out = [];
    for (const tris of byMat.values()) {
      const m = tris.m;
      // compact vertices used by this material
      const remap = new Map();
      const verts = [];
      const indices = new Uint32Array(tris.length);
      for (let k = 0; k < tris.length; k++) {
        const v = tris[k];
        let r = remap.get(v);
        if (r === undefined) {
          r = verts.length / 12;
          remap.set(v, r);
          verts.push(this.positions[v * 3], this.positions[v * 3 + 1], this.positions[v * 3 + 2],
            this.normals[v * 3], this.normals[v * 3 + 1], this.normals[v * 3 + 2],
            this.uvs[v * 2], this.uvs[v * 2 + 1],
            this.colors[v * 4], this.colors[v * 4 + 1], this.colors[v * 4 + 2], this.colors[v * 4 + 3]);
        }
        indices[k] = r;
      }
      out.push({ material: this.materials[m], owner: tris.g || this.owner, vertices: new Float32Array(verts), indices, vertexCount: verts.length / 12 });
    }
    return out;
  }
  toOBJ(name = "mesh") {
    if (!this.normals) this.computeNormals();
    const L = [`o ${name}`];
    for (let i = 0; i < this.vertexCount; i++) L.push(`v ${this.positions[i * 3].toFixed(4)} ${this.positions[i * 3 + 1].toFixed(4)} ${this.positions[i * 3 + 2].toFixed(4)}`);
    for (let i = 0; i < this.vertexCount; i++) L.push(`vt ${this.uvs[i * 2].toFixed(4)} ${this.uvs[i * 2 + 1].toFixed(4)}`);
    for (let i = 0; i < this.vertexCount; i++) L.push(`vn ${this.normals[i * 3].toFixed(4)} ${this.normals[i * 3 + 1].toFixed(4)} ${this.normals[i * 3 + 2].toFixed(4)}`);
    let cur = -1;
    const sorted = [...this.faces].sort((a, b) => a.m - b.m);
    for (const f of sorted) {
      if (f.m !== cur) { cur = f.m; L.push(`usemtl ${this.materials[cur]}`); }
      L.push("f " + f.i.map((v) => `${v + 1}/${v + 1}/${v + 1}`).join(" "));
    }
    return L.join("\n");
  }
}

/** Utility: a vertex row along a cross-section. */
export function offsetPoint(frame, offset, height = 0) {
  return [frame.p[0] + frame.r[0] * offset, frame.p[1] + height, frame.p[2] + frame.r[2] * offset];
}

/* RoadForge — quad-based mesh builder. Every primitive is authored as quads; triangles are only derived for the GPU. */
(function (RF) {
  'use strict';
  const V = RF.V;
  const STRIDE = 12;
  const SURFACE_RE = /^(asphalt|paving|grass|paint|ballast|concrete_road|ground|curb|granite|tactile|gravel|median)/; // pos3 nrm3 uv2 aux4

  class MeshBuilder {
    constructor(material, opts = {}) {
      this.material = material;
      this.pos = []; this.nrm = []; this.uv = []; this.aux = [];
      this.quads = []; // flat, 4 indices per quad, CCW when seen from the front
      this.tris = []; // only used for genuinely triangular caps (kept at zero wherever possible)
      this.smooth = opts.smooth !== false;
      this.explicitNormals = false;
    }
    get vertexCount() { return this.pos.length / 3; }
    vertex(p, uv = [0, 0], aux = [0, 0, 0, 0], n = null) {
      this.pos.push(p[0], p[1], p[2]);
      this.nrm.push(n ? n[0] : 0, n ? n[1] : 0, n ? n[2] : 0);
      if (n) this.explicitNormals = true;
      this.uv.push(uv[0], uv[1]);
      this.aux.push(aux[0], aux[1], aux[2], aux[3]);
      return this.pos.length / 3 - 1;
    }
    quad(a, b, c, d) { this.quads.push(a, b, c, d); }
    tri(a, b, c) { this.tris.push(a, b, c); }
    /** Quads between two equal-length rows of vertex indices. flip reverses winding. */
    strip(rowA, rowB, flip = false) {
      const n = Math.min(rowA.length, rowB.length);
      for (let i = 0; i < n - 1; i++) {
        if (flip) this.quad(rowA[i], rowA[i + 1], rowB[i + 1], rowB[i]);
        else this.quad(rowA[i], rowB[i], rowB[i + 1], rowA[i + 1]);
      }
    }
    /** rows: array of rows of indices (grid). Quads with consistent winding. */
    grid(rows, flip = false) { for (let r = 0; r < rows.length - 1; r++) this.strip(rows[r], rows[r + 1], flip); }
    /** Position of a vertex (for later displacement). */
    getPos(i) { return [this.pos[i * 3], this.pos[i * 3 + 1], this.pos[i * 3 + 2]]; }
    setPos(i, p) { this.pos[i * 3] = p[0]; this.pos[i * 3 + 1] = p[1]; this.pos[i * 3 + 2] = p[2]; }
    setAux(i, k, v) { this.aux[i * 4 + k] = v; }
    /** Append another builder's geometry (same material). */
    append(other, transform = null) {
      const base = this.vertexCount;
      if (!transform) {
        for (let i = 0; i < other.pos.length; i++) this.pos.push(other.pos[i]);
        for (let i = 0; i < other.nrm.length; i++) this.nrm.push(other.nrm[i]);
      } else {
        const m = transform;
        for (let i = 0; i < other.pos.length; i += 3) { const p = RF.M4.transformPoint(m, [other.pos[i], other.pos[i + 1], other.pos[i + 2]]); this.pos.push(p[0], p[1], p[2]); }
        for (let i = 0; i < other.nrm.length; i += 3) { const n = RF.M4.transformDir(m, [other.nrm[i], other.nrm[i + 1], other.nrm[i + 2]]); this.nrm.push(n[0], n[1], n[2]); }
      }
      for (let i = 0; i < other.uv.length; i++) this.uv.push(other.uv[i]);
      for (let i = 0; i < other.aux.length; i++) this.aux.push(other.aux[i]);
      for (let i = 0; i < other.quads.length; i++) this.quads.push(other.quads[i] + base);
      for (let i = 0; i < other.tris.length; i++) this.tris.push(other.tris[i] + base);
      if (other.explicitNormals) this.explicitNormals = true;
    }
    computeNormals() {
      const n = this.vertexCount, acc = new Float32Array(n * 3), P = this.pos;
      const addFace = (ids) => {
        // Newell normal (works for non-planar quads)
        let nx = 0, ny = 0, nz = 0;
        for (let k = 0; k < ids.length; k++) {
          const a = ids[k] * 3, b = ids[(k + 1) % ids.length] * 3;
          nx += (P[a + 1] - P[b + 1]) * (P[a + 2] + P[b + 2]);
          ny += (P[a + 2] - P[b + 2]) * (P[a] + P[b]);
          nz += (P[a] - P[b]) * (P[a + 1] + P[b + 1]);
        }
        for (const id of ids) { acc[id * 3] += nx; acc[id * 3 + 1] += ny; acc[id * 3 + 2] += nz; }
      };
      for (let i = 0; i < this.quads.length; i += 4) addFace([this.quads[i], this.quads[i + 1], this.quads[i + 2], this.quads[i + 3]]);
      for (let i = 0; i < this.tris.length; i += 3) addFace([this.tris[i], this.tris[i + 1], this.tris[i + 2]]);
      for (let i = 0; i < n; i++) {
        const l = Math.hypot(acc[i * 3], acc[i * 3 + 1], acc[i * 3 + 2]) || 1;
        this.nrm[i * 3] = acc[i * 3] / l; this.nrm[i * 3 + 1] = acc[i * 3 + 1] / l; this.nrm[i * 3 + 2] = acc[i * 3 + 2] / l;
      }
    }
    isEmpty() { return this.quads.length === 0 && this.tris.length === 0; }
    /** Merge vertices sharing position (quantised) and aux flag, so patches become one connected quad mesh. */
    weld(eps = 1e-4) {
      const n = this.vertexCount, map = new Map(), remap = new Int32Array(n);
      const pos = [], nrm = [], uv = [], aux = [];
      const q = 1 / eps;
      for (let i = 0; i < n; i++) {
        const key = Math.round(this.pos[i * 3] * q) + ',' + Math.round(this.pos[i * 3 + 1] * q) + ',' + Math.round(this.pos[i * 3 + 2] * q) + ',' + this.aux[i * 4 + 3];
        let j = map.get(key);
        if (j === undefined) { j = pos.length / 3; map.set(key, j); pos.push(this.pos[i * 3], this.pos[i * 3 + 1], this.pos[i * 3 + 2]); nrm.push(this.nrm[i * 3], this.nrm[i * 3 + 1], this.nrm[i * 3 + 2]); uv.push(this.uv[i * 2], this.uv[i * 2 + 1]); aux.push(this.aux[i * 4], this.aux[i * 4 + 1], this.aux[i * 4 + 2], this.aux[i * 4 + 3]); }
        remap[i] = j;
      }
      this.pos = pos; this.nrm = nrm; this.uv = uv; this.aux = aux;
      const quads = [];
      for (let i = 0; i < this.quads.length; i += 4) {
        const a = remap[this.quads[i]], b = remap[this.quads[i + 1]], c = remap[this.quads[i + 2]], d = remap[this.quads[i + 3]];
        if (a === b || b === c || c === d || d === a) continue; // drop collapsed quads
        quads.push(a, b, c, d);
      }
      this.quads = quads;
      this.tris = Array.from(this.tris, (i) => remap[i]);
      return this;
    }
    /** Finalise into typed arrays for the GPU. */
    /** Surface materials are road-top layers that must face +Y: any face wound downward is flipped (safety net for strip orientation). */
    orientUp() {
      const P = this.pos;
      const faceY = (ids) => { let s = 0; for (let t = 0; t < ids.length; t++) { const a = ids[t] * 3, b = ids[(t + 1) % ids.length] * 3; s += (P[a + 2] - P[b + 2]) * (P[a] + P[b]); } return s; };
      // Newell: ny = Σ (z_i - z_j)(x_i + x_j); ny > 0 means the face normal points up
      let flipped = 0;
      for (let i = 0; i < this.quads.length; i += 4) {
        const q = [this.quads[i], this.quads[i + 1], this.quads[i + 2], this.quads[i + 3]];
        const ny = faceY(q);
        const ax = P[q[0] * 3] - P[q[2] * 3], ay = P[q[0] * 3 + 1] - P[q[2] * 3 + 1], az = P[q[0] * 3 + 2] - P[q[2] * 3 + 2];
        const bx = P[q[1] * 3] - P[q[3] * 3], by = P[q[1] * 3 + 1] - P[q[3] * 3 + 1], bz = P[q[1] * 3 + 2] - P[q[3] * 3 + 2];
        const area2 = Math.hypot(ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx);
        if (area2 > 1e-12 && ny < -0.3 * area2) { this.quads[i + 1] = q[3]; this.quads[i + 3] = q[1]; flipped++; }
      }
      for (let i = 0; i < this.tris.length; i += 3) {
        const t = [this.tris[i], this.tris[i + 1], this.tris[i + 2]];
        const ny = faceY(t);
        const ax = P[t[1] * 3] - P[t[0] * 3], ay = P[t[1] * 3 + 1] - P[t[0] * 3 + 1], az = P[t[1] * 3 + 2] - P[t[0] * 3 + 2];
        const bx = P[t[2] * 3] - P[t[0] * 3], by = P[t[2] * 3 + 1] - P[t[0] * 3 + 1], bz = P[t[2] * 3 + 2] - P[t[0] * 3 + 2];
        const area2 = Math.hypot(ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx);
        if (area2 > 1e-12 && ny < -0.3 * area2) { this.tris[i + 1] = t[2]; this.tris[i + 2] = t[1]; flipped++; }
      }
      return flipped;
    }
    build() {
      if (!this.explicitNormals && SURFACE_RE.test(this.material)) this.orientUp();
      if (!this.explicitNormals) this.computeNormals();
      const n = this.vertexCount;
      const data = new Float32Array(n * STRIDE);
      let minx = Infinity, miny = Infinity, minz = Infinity, maxx = -Infinity, maxy = -Infinity, maxz = -Infinity;
      for (let i = 0; i < n; i++) {
        const o = i * STRIDE;
        const x = this.pos[i * 3], y = this.pos[i * 3 + 1], z = this.pos[i * 3 + 2];
        data[o] = x; data[o + 1] = y; data[o + 2] = z;
        data[o + 3] = this.nrm[i * 3]; data[o + 4] = this.nrm[i * 3 + 1]; data[o + 5] = this.nrm[i * 3 + 2];
        data[o + 6] = this.uv[i * 2]; data[o + 7] = this.uv[i * 2 + 1];
        data[o + 8] = this.aux[i * 4]; data[o + 9] = this.aux[i * 4 + 1]; data[o + 10] = this.aux[i * 4 + 2]; data[o + 11] = this.aux[i * 4 + 3];
        if (x < minx) minx = x; if (y < miny) miny = y; if (z < minz) minz = z; if (x > maxx) maxx = x; if (y > maxy) maxy = y; if (z > maxz) maxz = z;
      }
      const triCount = this.quads.length / 2 + this.tris.length / 3;
      const idx = new Uint32Array(triCount * 3);
      let k = 0;
      for (let i = 0; i < this.quads.length; i += 4) {
        const a = this.quads[i], b = this.quads[i + 1], c = this.quads[i + 2], d = this.quads[i + 3];
        idx[k++] = a; idx[k++] = b; idx[k++] = c; idx[k++] = a; idx[k++] = c; idx[k++] = d;
      }
      for (let i = 0; i < this.tris.length; i++) idx[k++] = this.tris[i];
      return { material: this.material, vertices: data, indices: idx, quads: new Uint32Array(this.quads), tris: new Uint32Array(this.tris), vertexCount: n, quadCount: this.quads.length / 4, triCount: this.tris.length / 3, bounds: [minx, miny, minz, maxx, maxy, maxz] };
    }
  }

  /* ---------- Primitive helpers (all quads) ---------- */

  /** Revolve a 2D profile [[r,y],...] around Y into a lathe (quads). closedBottom/top add centre caps as quad fans via a tiny centre ring. */
  function lathe(b, profile, segments, opts = {}) {
    const { uvScale = 1, aux = [0, 0, 0, 0], transform = null, capTop = false, capBottom = false } = opts;
    const rows = [];
    const addRing = (r, y, v) => {
      const row = [];
      for (let i = 0; i <= segments; i++) {
        const a = (i / segments) * Math.PI * 2;
        let p = [Math.cos(a) * r, y, Math.sin(a) * r];
        if (transform) p = RF.M4.transformPoint(transform, p);
        row.push(b.vertex(p, [i / segments * uvScale, v], aux));
      }
      return row;
    };
    let total = 0; for (let i = 1; i < profile.length; i++) total += Math.hypot(profile[i][0] - profile[i - 1][0], profile[i][1] - profile[i - 1][1]);
    let v = 0;
    for (let i = 0; i < profile.length; i++) {
      if (i > 0) v += Math.hypot(profile[i][0] - profile[i - 1][0], profile[i][1] - profile[i - 1][1]) / (total || 1);
      rows.push(addRing(profile[i][0], profile[i][1], v * uvScale));
    }
    // winding: profile from bottom to top, outward normals
    for (let r = 0; r < rows.length - 1; r++) b.strip(rows[r], rows[r + 1], true);
    const cap = (ring, y, up) => {
      const r0 = profile[up ? profile.length - 1 : 0][0];
      const inner = addRing(r0 * 0.3, y, 0);
      const centre = addRing(0, y, 0);
      if (up) { b.strip(ring, inner, true); b.strip(inner, centre, true); }
      else { b.strip(ring, inner, false); b.strip(inner, centre, false); }
    };
    if (capTop) cap(rows[rows.length - 1], profile[profile.length - 1][1], true);
    if (capBottom) cap(rows[0], profile[0][1], false);
    return rows;
  }

  /** Box with optional bevel (chamfer), all quads. size [sx,sy,sz], centred at origin + offset. */
  function box(b, size, opts = {}) {
    const { bevel = 0, center = [0, 0, 0], transform = null, aux = [0, 0, 0, 0], uvScale = 1 } = opts;
    const hx = size[0] / 2, hy = size[1] / 2, hz = size[2] / 2;
    const T = (p) => { let q = [p[0] + center[0], p[1] + center[1], p[2] + center[2]]; return transform ? RF.M4.transformPoint(transform, q) : q; };
    if (bevel <= 0) {
      const faces = [
        [[-hx, -hy, hz], [hx, -hy, hz], [hx, hy, hz], [-hx, hy, hz]], // +z
        [[hx, -hy, -hz], [-hx, -hy, -hz], [-hx, hy, -hz], [hx, hy, -hz]], // -z
        [[hx, -hy, hz], [hx, -hy, -hz], [hx, hy, -hz], [hx, hy, hz]], // +x
        [[-hx, -hy, -hz], [-hx, -hy, hz], [-hx, hy, hz], [-hx, hy, -hz]], // -x
        [[-hx, hy, hz], [hx, hy, hz], [hx, hy, -hz], [-hx, hy, -hz]], // +y
        [[-hx, -hy, -hz], [hx, -hy, -hz], [hx, -hy, hz], [-hx, -hy, hz]], // -y
      ];
      const uvs = [[size[0], size[1]], [size[0], size[1]], [size[2], size[1]], [size[2], size[1]], [size[0], size[2]], [size[0], size[2]]];
      faces.forEach((f, k) => {
        const i = f.map((p, j) => b.vertex(T(p), [(j === 1 || j === 2 ? uvs[k][0] : 0) * uvScale, (j >= 2 ? uvs[k][1] : 0) * uvScale], aux));
        b.quad(i[0], i[1], i[2], i[3]);
      });
      return;
    }
    // Bevelled box: 26-patch (faces, edges, corners) all quads.
    const e = Math.min(bevel, hx, hy, hz) ;
    const xs = [-hx, -hx + e, hx - e, hx], ys = [-hy, -hy + e, hy - e, hy], zs = [-hz, -hz + e, hz - e, hz];
    // Build a rounded box by sampling superellipse-ish: use 3x3 grid per face with corner rounding via normalisation blend.
    const corner = (x, y, z) => {
      // clamp inner box then push out along the bevel direction
      const cx = RF.clamp(x, -hx + e, hx - e), cy = RF.clamp(y, -hy + e, hy - e), cz = RF.clamp(z, -hz + e, hz - e);
      const dx = x - cx, dy = y - cy, dz = z - cz; const l = Math.hypot(dx, dy, dz);
      if (l < 1e-9) return [x, y, z];
      return [cx + (dx / l) * e, cy + (dy / l) * e, cz + (dz / l) * e];
    };
    const faceGrid = (gen, u0, v0) => {
      const rows = [];
      for (let i = 0; i < 4; i++) { const row = []; for (let j = 0; j < 4; j++) { const [x, y, z] = gen(i, j); row.push(b.vertex(T(corner(x, y, z)), [u0[j] * uvScale, v0[i] * uvScale], aux)); } rows.push(row); }
      return rows;
    };
    const nx = xs.map((x) => x + hx), ny = ys.map((y) => y + hy), nz = zs.map((z) => z + hz);
    b.grid(faceGrid((i, j) => [xs[j], ys[i], hz], nx, ny));            // +z
    b.grid(faceGrid((i, j) => [xs[3 - j], ys[i], -hz], nx, ny));       // -z
    b.grid(faceGrid((i, j) => [hx, ys[i], zs[3 - j]], nz, ny));        // +x
    b.grid(faceGrid((i, j) => [-hx, ys[i], zs[j]], nz, ny));           // -x
    b.grid(faceGrid((i, j) => [xs[j], hy, zs[3 - i]], nx, nz));        // +y
    b.grid(faceGrid((i, j) => [xs[j], -hy, zs[i]], nx, nz));           // -y
    // edges: between adjacent faces — bevel strips
    const ring = (fn, u) => { const row = []; for (let j = 0; j < 4; j++) row.push(fn(j)); return row; };
    // We approximate edge/corner bevels by stitching the face border rows with extra strips generated from corner() of midpoints.
    // Simpler: generate 12 edge strips with 2 segments and 8 corner patches (2x2 quads) — using the same corner() projector.
    const edgeStrip = (pA, pB) => {
      // pA(j), pB(j): parametric 4-point rows on neighbouring faces; interpolate through corner() for a mid row.
      const rowA = [], rowM = [], rowB = [];
      for (let j = 0; j < 4; j++) {
        const a = pA(j), c = pB(j), m = [(a[0] + c[0]) / 2, (a[1] + c[1]) / 2, (a[2] + c[2]) / 2];
        rowA.push(b.vertex(T(corner(a[0], a[1], a[2])), [j * uvScale, 0], aux));
        rowM.push(b.vertex(T(corner(m[0], m[1], m[2])), [j * uvScale, 0.5 * uvScale], aux));
        rowB.push(b.vertex(T(corner(c[0], c[1], c[2])), [j * uvScale, uvScale], aux));
      }
      b.grid([rowA, rowM, rowB]);
    };
    // X-parallel edges (4)
    edgeStrip((j) => [xs[j], hy - e, hz], (j) => [xs[j], hy, hz - e]);
    edgeStrip((j) => [xs[3 - j], hy - e, -hz], (j) => [xs[3 - j], hy, -hz + e]);
    edgeStrip((j) => [xs[3 - j], -hy + e, hz], (j) => [xs[3 - j], -hy, hz - e]);
    edgeStrip((j) => [xs[j], -hy + e, -hz], (j) => [xs[j], -hy, -hz + e]);
    // Z-parallel edges (4)
    edgeStrip((j) => [hx, hy - e, zs[3 - j]], (j) => [hx - e, hy, zs[3 - j]]);
    edgeStrip((j) => [-hx, hy - e, zs[j]], (j) => [-hx + e, hy, zs[j]]);
    edgeStrip((j) => [hx, -hy + e, zs[j]], (j) => [hx - e, -hy, zs[j]]);
    edgeStrip((j) => [-hx, -hy + e, zs[3 - j]], (j) => [-hx + e, -hy, zs[3 - j]]);
    // Y-parallel edges (4)
    edgeStrip((j) => [hx - e, ys[j], hz], (j) => [hx, ys[j], hz - e]);
    edgeStrip((j) => [hx, ys[j], -hz + e], (j) => [hx - e, ys[j], -hz]);
    edgeStrip((j) => [-hx + e, ys[j], -hz], (j) => [-hx, ys[j], -hz + e]);
    edgeStrip((j) => [-hx, ys[j], hz - e], (j) => [-hx + e, ys[j], hz]);
    // Corners (8): 2x2 quad patches
    const cornerPatch = (sx, sy, sz) => {
      const rows = [];
      for (let i = 0; i < 3; i++) {
        const row = [];
        for (let j = 0; j < 3; j++) {
          // param over the spherical corner octant
          const u = (j / 2) * Math.PI / 2, v = (i / 2) * Math.PI / 2;
          const dx = Math.cos(v) * Math.cos(u), dy = Math.sin(v), dz = Math.cos(v) * Math.sin(u);
          const p = [sx * (hx - e + dx * e), sy * (hy - e + dy * e), sz * (hz - e + dz * e)];
          row.push(b.vertex(T(p), [j * 0.5 * uvScale, i * 0.5 * uvScale], aux));
        }
        rows.push(row);
      }
      b.grid(rows, (sx * sy * sz) < 0);
    };
    for (const sx of [-1, 1]) for (const sy of [-1, 1]) for (const sz of [-1, 1]) cornerPatch(sx, sy, sz);
  }

  /**
   * Sweep a closed/open 2D profile along a path of frames. frames: [{p, d, n, up?}]. profile: [[x(lateral), y(up)]].
   * uv.u along profile, uv.v along path (metres).
   */
  function sweep(b, frames, profile, opts = {}) {
    const { closed = false, aux = [0, 0, 0, 0], flip = false, uvScale = 1, auxFn = null } = opts;
    const rows = [];
    let v = 0;
    for (let i = 0; i < frames.length; i++) {
      const f = frames[i];
      if (i > 0) v += V.dist(frames[i - 1].p, f.p);
      const up = f.up || [0, 1, 0];
      const row = [];
      let u = 0;
      for (let j = 0; j < profile.length; j++) {
        if (j > 0) u += Math.hypot(profile[j][0] - profile[j - 1][0], profile[j][1] - profile[j - 1][1]);
        const p = V.add(V.add(f.p, V.mul(f.n, profile[j][0])), V.mul(up, profile[j][1]));
        row.push(b.vertex(p, [u * uvScale, v * uvScale], auxFn ? auxFn(i, j) : aux));
      }
      if (closed) row.push(row[0]);
      rows.push(row);
    }
    b.grid(rows, !flip);
    return rows;
  }

  /** Coons patch from four boundary curves (arrays of points). c0: bottom (u 0→1), c1: right (v 0→1), c2: top (u 0→1), c3: left (v 0→1). Returns rows of vertex indices. */
  function coons(b, c0, c1, c2, c3, opts = {}) {
    const nu = c0.length - 1, nv = c1.length - 1;
    const { uvFn = null, aux = [0, 0, 0, 0], auxFn = null, flip = false, posFn = null } = opts;
    const P00 = c0[0], P10 = c0[nu], P01 = c2[0], P11 = c2[nu];
    const rows = [];
    for (let j = 0; j <= nv; j++) {
      const v = j / nv; const row = [];
      for (let i = 0; i <= nu; i++) {
        const u = i / nu;
        let p;
        if (i === 0) p = c3[j]; else if (i === nu) p = c1[j]; else if (j === 0) p = c0[i]; else if (j === nv) p = c2[i];
        else {
          const L = V.add(V.mul(c0[i], 1 - v), V.mul(c2[i], v));
          const R = V.add(V.mul(c3[j], 1 - u), V.mul(c1[j], u));
          const B = V.add(V.add(V.mul(P00, (1 - u) * (1 - v)), V.mul(P10, u * (1 - v))), V.add(V.mul(P01, (1 - u) * v), V.mul(P11, u * v)));
          p = V.sub(V.add(L, R), B);
        }
        if (posFn) p = posFn(p, u, v);
        row.push(b.vertex(p, uvFn ? uvFn(p, u, v) : [p[0], p[2]], auxFn ? auxFn(p, u, v) : aux));
      }
      rows.push(row);
    }
    b.grid(rows, flip);
    return rows;
  }

  RF.MeshBuilder = MeshBuilder; RF.STRIDE = STRIDE;
  RF.prim = { lathe, box, sweep, coons };
})(globalThis.RF || (globalThis.RF = {}));

// RiverPath – carve a river channel along a spline into a heightfield, pre-fill it, and drive it
// with a real inflow source. The water then *flows* down the carved bed under gravity (no fake
// scrolling textures): meanders, rapids over rocks and pooling all come from the solver.

function catmull(p0, p1, p2, p3, t) {
  const t2 = t * t, t3 = t2 * t;
  return p1.map((_, k) => 0.5 * ((2 * p1[k]) + (-p0[k] + p2[k]) * t + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2 + (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3));
}

export class RiverPath {
  /**
   * @param {number[][]} points   control points [x, z] (world), source first
   * @param {object} o
   * @param {number|function} [o.width=8]        channel width (m), or fn(s01)
   * @param {number} [o.depth=1]                 channel depth below banks (m)
   * @param {number[]} [o.elevation]             [sourceBed, mouthBed] bed heights; default = terrain
   * @param {number} [o.bankSlope=0.45]          valley wall slope
   */
  constructor(points, o = {}) {
    this.ctrl = points;
    this.widthFn = typeof o.width === 'function' ? o.width : () => (o.width ?? 8);
    this.depth = o.depth ?? 1;
    this.elevation = o.elevation ?? null;
    this.bankSlope = o.bankSlope ?? 0.45;
    this.profile = o.profile ?? ((s) => s);   // remap of normalised length -> elevation fraction
    this.closed = o.closed ?? false;          // closed loops are handy for race tracks
    this._sample();
  }

  _sample() {
    const P = this.ctrl, pts = [], N = P.length;
    const at = (i) => (this.closed ? P[((i % N) + N) % N] : P[Math.min(N - 1, Math.max(0, i))]);
    const segs = this.closed ? N : N - 1;
    for (let i = 0; i < segs; i++) {
      const n = 24;
      for (let s = 0; s < n; s++) pts.push(catmull(at(i - 1), at(i), at(i + 1), at(i + 2), s / n));
    }
    pts.push(this.closed ? P[0] : P[N - 1]);
    this.pts = pts;
    this.cum = [0];
    for (let i = 1; i < pts.length; i++) this.cum.push(this.cum[i - 1] + Math.hypot(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]));
    this.length = this.cum[this.cum.length - 1];
    this._buildBuckets(8);
  }

  // uniform grid of segment buckets so nearest() is O(1) per query instead of O(segments)
  _buildBuckets(size) {
    const P = this.pts;
    let x0 = Infinity, z0 = Infinity, x1 = -Infinity, z1 = -Infinity;
    for (const p of P) { x0 = Math.min(x0, p[0]); z0 = Math.min(z0, p[1]); x1 = Math.max(x1, p[0]); z1 = Math.max(z1, p[1]); }
    this._bk = { size, x0, z0, w: Math.ceil((x1 - x0) / size) + 1, h: Math.ceil((z1 - z0) / size) + 1 };
    this._bk.cells = Array.from({ length: this._bk.w * this._bk.h }, () => []);
    for (let i = 0; i < P.length - 1; i++) {
      const a = P[i], b = P[i + 1];
      const i0 = Math.floor((Math.min(a[0], b[0]) - x0) / size), i1 = Math.floor((Math.max(a[0], b[0]) - x0) / size);
      const j0 = Math.floor((Math.min(a[1], b[1]) - z0) / size), j1 = Math.floor((Math.max(a[1], b[1]) - z0) / size);
      for (let j = j0; j <= j1; j++) for (let ii = i0; ii <= i1; ii++) this._bk.cells[j * this._bk.w + ii].push(i);
    }
  }

  /**
   * nearest point on the path within maxDist: { dist, s (0..1 along length), tangent [x,z], point [x,z] }
   * (dist = Infinity when nothing is within maxDist)
   */
  nearest(x, z, maxDist = Infinity) {
    let best = Infinity, bs = 0, bt = [1, 0], bp = [x, z];
    const P = this.pts, B = this._bk;
    const test = (i) => {
      const ax = P[i][0], az = P[i][1], bx = P[i + 1][0], bz = P[i + 1][1];
      const dx = bx - ax, dz = bz - az;
      const l2 = dx * dx + dz * dz || 1e-9;
      let t = ((x - ax) * dx + (z - az) * dz) / l2;
      t = Math.max(0, Math.min(1, t));
      const px = ax + dx * t, pz = az + dz * t;
      const d = (x - px) ** 2 + (z - pz) ** 2;
      if (d < best) {
        best = d;
        const l = Math.sqrt(l2);
        bs = (this.cum[i] + l * t) / this.length;
        bt = [dx / l, dz / l];
        bp = [px, pz];
      }
    };
    if (Number.isFinite(maxDist)) {
      const r = Math.ceil(maxDist / B.size);
      const ci = Math.floor((x - B.x0) / B.size), cj = Math.floor((z - B.z0) / B.size);
      const seen = new Set();
      for (let j = Math.max(0, cj - r); j <= Math.min(B.h - 1, cj + r); j++) {
        for (let i = Math.max(0, ci - r); i <= Math.min(B.w - 1, ci + r); i++) {
          for (const seg of B.cells[j * B.w + i]) if (!seen.has(seg)) { seen.add(seg); test(seg); }
        }
      }
      if (best > maxDist * maxDist) return { dist: Infinity, s: 0, tangent: bt, point: bp };
    } else {
      for (let i = 0; i < P.length - 1; i++) test(i);
    }
    return { dist: Math.sqrt(best), s: bs, tangent: bt, point: bp };
  }

  /** point and tangent at normalised arc position s (0..1) */
  pointAt(s) {
    const L = Math.max(0, Math.min(1, s)) * this.length;
    let i = 0;
    while (i < this.cum.length - 2 && this.cum[i + 1] < L) i++;
    const a = this.pts[i], b = this.pts[i + 1];
    const seg = this.cum[i + 1] - this.cum[i] || 1e-9;
    const t = (L - this.cum[i]) / seg;
    return { point: [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t], tangent: [(b[0] - a[0]) / seg, (b[1] - a[1]) / seg] };
  }

  /** bed elevation of the channel centre at normalised position s */
  bedAt(s) {
    const [a, b] = this.elevation;
    return a + (b - a) * this.profile(s);
  }

  /**
   * Carve the channel into a heightfield (in place). heightAt(i,j) layout: row-major nx*ny.
   * Returns per-cell {dist, s} info for later use (Float32Array pairs).
   */
  carve(heights, nx, ny, cellSize, origin, maxInfluence = 40) {
    if (!this.elevation) {
      const h = (p) => {
        const i = Math.min(nx - 1, Math.max(0, Math.floor((p[0] - origin[0]) / cellSize)));
        const j = Math.min(ny - 1, Math.max(0, Math.floor((p[1] - origin[1]) / cellSize)));
        return heights[j * nx + i];
      };
      this.elevation = [h(this.ctrl[0]) - this.depth, h(this.ctrl[this.ctrl.length - 1]) - this.depth];
    }
    const info = new Float32Array(nx * ny * 2);
    for (let j = 0; j < ny; j++) {
      for (let i = 0; i < nx; i++) {
        const x = origin[0] + (i + 0.5) * cellSize, z = origin[1] + (j + 0.5) * cellSize;
        const n = this.nearest(x, z, maxInfluence);
        const k = j * nx + i;
        info[k * 2] = n.dist; info[k * 2 + 1] = n.s;
        if (!Number.isFinite(n.dist)) continue;
        const half = this.widthFn(n.s) / 2;
        const bed = this.bedAt(n.s);
        let ch;
        if (n.dist < half) {
          const r = n.dist / half;
          ch = bed + this.depth * 0.35 * r * r * r * r;  // flat-ish bottom, rounded banks
        } else {
          ch = bed + this.depth * 0.35 + (n.dist - half) * this.bankSlope;
        }
        if (ch < heights[k]) heights[k] = ch;
      }
    }
    this.info = info;
    return info;
  }

  /** fill the channel to `depth` metres above the local bed (initial condition) */
  fill(depthOut, heights, nx, ny, fillDepth = this.depth * 0.75) {
    for (let k = 0; k < nx * ny; k++) {
      const dist = this.info[k * 2], s = this.info[k * 2 + 1];
      const half = this.widthFn(s) / 2;
      if (dist < half * 1.3) {
        const level = this.bedAt(s) + fillDepth;
        depthOut[k] = Math.max(depthOut[k], level - heights[k]);
      }
    }
  }

  /** add a real inflow at the river head to a FluidSim */
  addSource(sim, { flow = 3, speed = 1.0, mud = 0, radius } = {}) {
    const p0 = this.pts[0], p1 = this.pts[Math.min(6, this.pts.length - 1)];
    const d = [p1[0] - p0[0], p1[1] - p0[1]];
    return sim.addSource({ x: p0[0], z: p0[1], radius: radius ?? this.widthFn(0) * 0.45, flow, direction: d, speed, mud });
  }
}

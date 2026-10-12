// Road centreline: a C1 Hermite spline through control points, densely sampled (~1 m)
// with arclength, tangent frames and a smooth vertical profile. All mesh builders sample
// through frameAt(s), so every lane, curb and marking shares exactly the same frames.
import { smoother, clamp } from './math.js';

const STEP = 1.0;

export class Centerline {
  /**
   * @param {Array<{x:number,z:number,y?:number}>} pts control points (first/last = nodes)
   * @param {{t0?:[number,number], t1?:[number,number]}} opts optional forced end tangents (unit)
   */
  constructor(pts, opts = {}) {
    this.ctrl = pts;
    const n = pts.length;
    // Tangent directions at control points.
    const dirs = [];
    for (let i = 0; i < n; i++) {
      let dx, dz;
      if (i === 0) {
        if (opts.t0) [dx, dz] = opts.t0; else { dx = pts[1].x - pts[0].x; dz = pts[1].z - pts[0].z; }
      } else if (i === n - 1) {
        if (opts.t1) [dx, dz] = opts.t1; else { dx = pts[n - 1].x - pts[n - 2].x; dz = pts[n - 1].z - pts[n - 2].z; }
      } else {
        const ax = pts[i].x - pts[i - 1].x, az = pts[i].z - pts[i - 1].z;
        const bx = pts[i + 1].x - pts[i].x, bz = pts[i + 1].z - pts[i].z;
        const la = Math.hypot(ax, az) || 1, lb = Math.hypot(bx, bz) || 1;
        dx = ax / la + bx / lb; dz = az / la + bz / lb;
      }
      const l = Math.hypot(dx, dz) || 1;
      dirs.push([dx / l, dz / l]);
    }
    const X = [], Z = [], S = [], ctrlS = [0];
    let s = 0;
    X.push(pts[0].x); Z.push(pts[0].z); S.push(0);
    for (let i = 0; i < n - 1; i++) {
      const p0 = pts[i], p1 = pts[i + 1];
      const chord = Math.hypot(p1.x - p0.x, p1.z - p0.z);
      const m0x = dirs[i][0] * chord, m0z = dirs[i][1] * chord;
      const m1x = dirs[i + 1][0] * chord, m1z = dirs[i + 1][1] * chord;
      const k = Math.max(1, Math.ceil(chord / STEP));
      let px = p0.x, pz = p0.z;
      for (let j = 1; j <= k; j++) {
        const t = j / k, t2 = t * t, t3 = t2 * t;
        const h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
        const x = h00 * p0.x + h10 * m0x + h01 * p1.x + h11 * m1x;
        const z = h00 * p0.z + h10 * m0z + h01 * p1.z + h11 * m1z;
        s += Math.hypot(x - px, z - pz);
        X.push(x); Z.push(z); S.push(s);
        px = x; pz = z;
      }
      ctrlS.push(s);
    }
    this.X = Float64Array.from(X); this.Z = Float64Array.from(Z); this.S = Float64Array.from(S);
    this.length = s;
    this.ctrlS = ctrlS;
    const m = X.length;
    // Tangents by central difference, forced at ends.
    this.TX = new Float64Array(m); this.TZ = new Float64Array(m);
    for (let i = 0; i < m; i++) {
      let dx, dz;
      if (i === 0) [dx, dz] = dirs[0];
      else if (i === m - 1) [dx, dz] = dirs[n - 1];
      else { dx = X[i + 1] - X[i - 1]; dz = Z[i + 1] - Z[i - 1]; }
      const l = Math.hypot(dx, dz) || 1;
      this.TX[i] = dx / l; this.TZ[i] = dz / l;
    }
    // Vertical profile: smootherstep between control points that define y.
    const keys = [];
    pts.forEach((p, i) => { if (p.y !== undefined && p.y !== null) keys.push([ctrlS[i], p.y]); });
    if (!keys.length) keys.push([0, 0]);
    this.Y = new Float64Array(m);
    let k = 0;
    for (let i = 0; i < m; i++) {
      const si = this.S[i];
      while (k < keys.length - 2 && si > keys[k + 1][0]) k++;
      if (keys.length === 1 || si <= keys[0][0]) this.Y[i] = keys[0][1];
      else if (si >= keys[keys.length - 1][0]) this.Y[i] = keys[keys.length - 1][1];
      else {
        const [s0, y0] = keys[k], [s1, y1] = keys[k + 1];
        this.Y[i] = y0 + (y1 - y0) * smoother(clamp((si - s0) / Math.max(1e-6, s1 - s0), 0, 1));
      }
    }
    this.yMax = Math.max(...this.Y); this.yMin = Math.min(...this.Y);
  }

  index(s) {
    const S = this.S;
    if (s <= 0) return 0;
    if (s >= this.length) return S.length - 2;
    let lo = 0, hi = S.length - 1;
    while (hi - lo > 1) { const mid = (lo + hi) >> 1; if (S[mid] <= s) lo = mid; else hi = mid; }
    return lo;
  }

  frameAt(s, out = {}) {
    const i = this.index(s), S = this.S;
    const ds = S[i + 1] - S[i];
    const t = ds > 0 ? clamp((s - S[i]) / ds, 0, 1) : 0;
    out.s = s;
    out.x = this.X[i] + (this.X[i + 1] - this.X[i]) * t;
    out.z = this.Z[i] + (this.Z[i + 1] - this.Z[i]) * t;
    out.y = this.Y[i] + (this.Y[i + 1] - this.Y[i]) * t;
    let tx = this.TX[i] + (this.TX[i + 1] - this.TX[i]) * t, tz = this.TZ[i] + (this.TZ[i + 1] - this.TZ[i]) * t;
    const l = Math.hypot(tx, tz) || 1; tx /= l; tz /= l;
    out.tx = tx; out.tz = tz; out.nx = -tz; out.nz = tx;
    return out;
  }

  // Adaptive stations in [s0, s1]: dense on curves / vertical change, sparse on straights.
  stations(s0, s1, extra = [], maxStep = 14, maxAngle = 0.035) {
    const out = [s0];
    const S = this.S;
    let i0 = this.index(s0) + 1;
    let lastS = s0, lastF = this.frameAt(s0), lastSlope = null;
    for (let i = i0; i < S.length && S[i] < s1; i++) {
      const si = S[i];
      const tx = this.TX[i], tz = this.TZ[i];
      const dot = clamp(tx * lastF.tx + tz * lastF.tz, -1, 1);
      const ang = Math.acos(dot);
      const slope = i > 0 ? (this.Y[i] - this.Y[i - 1]) / Math.max(1e-6, S[i] - S[i - 1]) : 0;
      if (lastSlope === null) lastSlope = slope;
      if (ang > maxAngle || si - lastS > maxStep || Math.abs(slope - lastSlope) > 0.006) {
        if (si - lastS > 0.3) { out.push(si); lastS = si; lastF = this.frameAt(si); lastSlope = slope; }
      }
    }
    for (const e of extra) if (e > s0 + 0.05 && e < s1 - 0.05) out.push(e);
    out.push(s1);
    out.sort((a, b) => a - b);
    const dedup = [out[0]];
    for (let i = 1; i < out.length; i++) if (out[i] - dedup[dedup.length - 1] > 0.05) dedup.push(out[i]);
    if (dedup[dedup.length - 1] !== s1) dedup[dedup.length - 1] = s1;
    return dedup;
  }

  // Nearest point query (brute force over samples, used by tools).
  nearest(px, pz) {
    let best = Infinity, bi = 0;
    for (let i = 0; i < this.X.length; i++) {
      const d = (this.X[i] - px) ** 2 + (this.Z[i] - pz) ** 2;
      if (d < best) { best = d; bi = i; }
    }
    // refine on adjacent segments
    let res = { d: Math.sqrt(best), s: this.S[bi] };
    for (const j of [bi - 1, bi]) {
      if (j < 0 || j >= this.X.length - 1) continue;
      const ax = this.X[j], az = this.Z[j], bx = this.X[j + 1], bz = this.Z[j + 1];
      const dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
      const t = l2 > 0 ? clamp(((px - ax) * dx + (pz - az) * dz) / l2, 0, 1) : 0;
      const d = Math.hypot(px - ax - dx * t, pz - az - dz * t);
      if (d < res.d) res = { d, s: this.S[j] + (this.S[j + 1] - this.S[j]) * t };
    }
    return res;
  }
}

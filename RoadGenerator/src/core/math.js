// Frontier Road Generator — core math (vectors, matrices, curves)
// All world units are metres. Y is up. Right-handed. Road "right" = cross(dir, up).

export const EPS = 1e-9;
export const TAU = Math.PI * 2;
export const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
export const lerp = (a, b, t) => a + (b - a) * t;
export const smoothstep = (a, b, x) => { const t = clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); };
export const deg = (r) => (r * 180) / Math.PI;
export const rad = (d) => (d * Math.PI) / 180;
export const wrapAngle = (a) => { while (a > Math.PI) a -= TAU; while (a <= -Math.PI) a += TAU; return a; };
export const mod = (a, n) => ((a % n) + n) % n;

// ---------- vec3 (plain arrays [x,y,z]) ----------
export const v3 = (x = 0, y = 0, z = 0) => [x, y, z];
export const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
export const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
export const mul = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
export const madd = (a, b, s) => [a[0] + b[0] * s, a[1] + b[1] * s, a[2] + b[2] * s];
export const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
export const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
export const len = (a) => Math.hypot(a[0], a[1], a[2]);
export const dist = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
export const norm = (a) => { const l = len(a); return l > EPS ? [a[0] / l, a[1] / l, a[2] / l] : [0, 0, 0]; };
export const mix = (a, b, t) => [lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t)];
export const neg = (a) => [-a[0], -a[1], -a[2]];
export const eq = (a, b, e = 1e-6) => Math.abs(a[0] - b[0]) < e && Math.abs(a[1] - b[1]) < e && Math.abs(a[2] - b[2]) < e;

// Planar (XZ) helpers — the plan view of the road network.
export const lenXZ = (a) => Math.hypot(a[0], a[2]);
export const distXZ = (a, b) => Math.hypot(a[0] - b[0], a[2] - b[2]);
export const normXZ = (a) => { const l = Math.hypot(a[0], a[2]); return l > EPS ? [a[0] / l, 0, a[2] / l] : [0, 0, 0]; };
export const dotXZ = (a, b) => a[0] * b[0] + a[2] * b[2];
export const crossXZ = (a, b) => a[0] * b[2] - a[2] * b[0];
/** Right-hand normal of a plan direction (points to the driver's right when travelling along d). */
export const rightXZ = (d) => [-d[2], 0, d[0]];
export const angleXZ = (d) => Math.atan2(d[2], d[0]);
export const fromAngleXZ = (a) => [Math.cos(a), 0, Math.sin(a)];
export const rotXZ = (v, a) => { const c = Math.cos(a), s = Math.sin(a); return [v[0] * c - v[2] * s, v[1], v[0] * s + v[2] * c]; };

/** Intersection of plan lines p + s*d and q + t*e. Returns {s,t,point} or null if parallel. */
export function lineIntersectXZ(p, d, q, e) {
  const det = crossXZ(d, e);
  if (Math.abs(det) < 1e-9) return null;
  const w = sub(q, p);
  const s = crossXZ(w, e) / det;
  const t = crossXZ(w, d) / det;
  return { s, t, point: madd(p, d, s) };
}

export function polygonAreaXZ(pts) {
  let a = 0;
  for (let i = 0, n = pts.length; i < n; i++) {
    const p = pts[i], q = pts[(i + 1) % n];
    a += p[0] * q[2] - q[0] * p[2];
  }
  return a * 0.5;
}
export function pointInPolygonXZ(p, poly) {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const a = poly[i], b = poly[j];
    if ((a[2] > p[2]) !== (b[2] > p[2]) && p[0] < ((b[0] - a[0]) * (p[2] - a[2])) / (b[2] - a[2]) + a[0]) inside = !inside;
  }
  return inside;
}

// ---------- mat4 (column-major Float32Array, WebGPU convention) ----------
export const mat4 = {
  identity() { const m = new Float32Array(16); m[0] = m[5] = m[10] = m[15] = 1; return m; },
  multiply(a, b, out = new Float32Array(16)) {
    for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) {
      out[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
    }
    return out;
  },
  perspective(fovy, aspect, near, far) {
    const f = 1 / Math.tan(fovy / 2), m = new Float32Array(16);
    m[0] = f / aspect; m[5] = f; m[10] = far / (near - far); m[11] = -1; m[14] = (near * far) / (near - far);
    return m;
  },
  // Reverse-Z infinite perspective is overkill; use standard [0,1] depth.
  ortho(l, r, b, t, n, f) {
    const m = new Float32Array(16);
    m[0] = 2 / (r - l); m[5] = 2 / (t - b); m[10] = 1 / (n - f); m[12] = (l + r) / (l - r); m[13] = (t + b) / (b - t); m[14] = n / (n - f); m[15] = 1;
    return m;
  },
  lookAt(eye, target, up) {
    const z = norm(sub(eye, target)), x = norm(cross(up, z)), y = cross(z, x);
    const m = new Float32Array(16);
    m[0] = x[0]; m[1] = y[0]; m[2] = z[0]; m[4] = x[1]; m[5] = y[1]; m[6] = z[1]; m[8] = x[2]; m[9] = y[2]; m[10] = z[2];
    m[12] = -dot(x, eye); m[13] = -dot(y, eye); m[14] = -dot(z, eye); m[15] = 1;
    return m;
  },
  translation(t) { const m = mat4.identity(); m[12] = t[0]; m[13] = t[1]; m[14] = t[2]; return m; },
  scaling(s) { const m = mat4.identity(); m[0] = s[0]; m[5] = s[1]; m[10] = s[2]; return m; },
  rotationY(a) { const m = mat4.identity(), c = Math.cos(a), s = Math.sin(a); m[0] = c; m[2] = -s; m[8] = s; m[10] = c; return m; },
  /** Build a frame matrix from basis vectors and origin. */
  fromBasis(x, y, z, o) {
    const m = new Float32Array(16);
    m[0] = x[0]; m[1] = x[1]; m[2] = x[2]; m[4] = y[0]; m[5] = y[1]; m[6] = y[2]; m[8] = z[0]; m[9] = z[1]; m[10] = z[2];
    m[12] = o[0]; m[13] = o[1]; m[14] = o[2]; m[15] = 1;
    return m;
  },
  invert(m, out = new Float32Array(16)) {
    const a00 = m[0], a01 = m[1], a02 = m[2], a03 = m[3], a10 = m[4], a11 = m[5], a12 = m[6], a13 = m[7];
    const a20 = m[8], a21 = m[9], a22 = m[10], a23 = m[11], a30 = m[12], a31 = m[13], a32 = m[14], a33 = m[15];
    const b00 = a00 * a11 - a01 * a10, b01 = a00 * a12 - a02 * a10, b02 = a00 * a13 - a03 * a10, b03 = a01 * a12 - a02 * a11;
    const b04 = a01 * a13 - a03 * a11, b05 = a02 * a13 - a03 * a12, b06 = a20 * a31 - a21 * a30, b07 = a20 * a32 - a22 * a30;
    const b08 = a20 * a33 - a23 * a30, b09 = a21 * a32 - a22 * a31, b10 = a21 * a33 - a23 * a31, b11 = a22 * a33 - a23 * a32;
    let det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    if (!det) return null;
    det = 1 / det;
    out[0] = (a11 * b11 - a12 * b10 + a13 * b09) * det; out[1] = (a02 * b10 - a01 * b11 - a03 * b09) * det;
    out[2] = (a31 * b05 - a32 * b04 + a33 * b03) * det; out[3] = (a22 * b04 - a21 * b05 - a23 * b03) * det;
    out[4] = (a12 * b08 - a10 * b11 - a13 * b07) * det; out[5] = (a00 * b11 - a02 * b08 + a03 * b07) * det;
    out[6] = (a32 * b02 - a30 * b05 - a33 * b01) * det; out[7] = (a20 * b05 - a22 * b02 + a23 * b01) * det;
    out[8] = (a10 * b10 - a11 * b08 + a13 * b06) * det; out[9] = (a01 * b08 - a00 * b10 - a03 * b06) * det;
    out[10] = (a30 * b04 - a31 * b02 + a33 * b00) * det; out[11] = (a21 * b02 - a20 * b04 - a23 * b00) * det;
    out[12] = (a11 * b07 - a10 * b09 - a12 * b06) * det; out[13] = (a00 * b09 - a01 * b07 + a02 * b06) * det;
    out[14] = (a31 * b01 - a30 * b03 - a32 * b00) * det; out[15] = (a20 * b03 - a21 * b01 + a22 * b00) * det;
    return out;
  },
  transformPoint(m, p) {
    const x = p[0], y = p[1], z = p[2];
    const w = m[3] * x + m[7] * y + m[11] * z + m[15] || 1;
    return [(m[0] * x + m[4] * y + m[8] * z + m[12]) / w, (m[1] * x + m[5] * y + m[9] * z + m[13]) / w, (m[2] * x + m[6] * y + m[10] * z + m[14]) / w];
  },
  transformDir(m, d) {
    return [m[0] * d[0] + m[4] * d[1] + m[8] * d[2], m[1] * d[0] + m[5] * d[1] + m[9] * d[2], m[2] * d[0] + m[6] * d[1] + m[10] * d[2]];
  },
};

// ---------- deterministic random ----------
export function makeRng(seed = 1) {
  let s = (seed >>> 0) || 1;
  const next = () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 4294967296; };
  next.range = (a, b) => a + (b - a) * next();
  next.int = (a, b) => Math.floor(next.range(a, b + 1));
  next.pick = (arr) => arr[Math.floor(next() * arr.length)];
  next.chance = (p) => next() < p;
  return next;
}
export function hash2(x, y) { let h = (x * 374761393 + y * 668265263) | 0; h = (h ^ (h >>> 13)) * 1274126177; return ((h ^ (h >>> 16)) >>> 0) / 4294967296; }

// ---------- cubic Bézier ----------
export function bezierPoint(p0, p1, p2, p3, t) {
  const u = 1 - t, a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
  return [a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0], a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1], a * p0[2] + b * p1[2] + c * p2[2] + d * p3[2]];
}
export function bezierTangent(p0, p1, p2, p3, t) {
  const u = 1 - t, a = 3 * u * u, b = 6 * u * t, c = 3 * t * t;
  return [a * (p1[0] - p0[0]) + b * (p2[0] - p1[0]) + c * (p3[0] - p2[0]), a * (p1[1] - p0[1]) + b * (p2[1] - p1[1]) + c * (p3[1] - p2[1]), a * (p1[2] - p0[2]) + b * (p2[2] - p1[2]) + c * (p3[2] - p2[2])];
}

/**
 * Piecewise-cubic spline through `points` with explicit end tangents, Catmull-Rom interior tangents.
 * Arc-length parameterised with a dense lookup table so station queries are O(log n) and uniform.
 * tangents: tA (direction leaving the first point), tB (direction arriving at the last point). Lengths matter
 * (they are Hermite tangents scaled per segment).
 */
export class Spline {
  constructor(points, tA, tB, tension = 0.5) {
    this.points = points.map((p) => [p[0], p[1], p[2]]);
    this.segments = [];
    const n = points.length;
    if (n < 2) throw new Error("Spline needs ≥2 points");
    const chord = (i) => dist(points[i], points[i + 1]);
    // Hermite tangent at each knot. Interior knots: chord-scaled Catmull-Rom (no overshoot on unequal chords).
    // End knots: explicit tangents when given (magnitude = handle strength), otherwise the adjacent chord.
    const tangents = [];
    for (let i = 0; i < n; i++) {
      if (i === 0) tangents.push(tA && len(tA) > EPS ? [...tA] : sub(points[1], points[0]));
      else if (i === n - 1) tangents.push(tB && len(tB) > EPS ? [...tB] : sub(points[n - 1], points[n - 2]));
      else {
        const dir = norm(sub(points[i + 1], points[i - 1]));
        const m = Math.min(chord(i - 1), chord(i)) * 2 * tension;
        tangents.push(mul(dir, m));
      }
    }
    for (let i = 0; i < n - 1; i++) {
      const L = chord(i);
      let t0 = tangents[i], t1 = tangents[i + 1];
      // Cap handle length so a single segment cannot loop back on itself.
      const cap = L * 1.5;
      if (len(t0) > cap) t0 = mul(norm(t0), cap);
      if (len(t1) > cap) t1 = mul(norm(t1), cap);
      this.segments.push({ p0: points[i], p1: madd(points[i], t0, 1 / 3), p2: madd(points[i + 1], t1, -1 / 3), p3: points[i + 1] });
    }
    this._buildTable();
  }
  _buildTable() {
    // Adaptive sampling per segment: more samples where curvature is high.
    const table = []; // {s, seg, t}
    let s = 0;
    for (let i = 0; i < this.segments.length; i++) {
      const sg = this.segments[i];
      const approx = dist(sg.p0, sg.p1) + dist(sg.p1, sg.p2) + dist(sg.p2, sg.p3);
      const steps = clamp(Math.ceil(approx / 0.25), 8, 400);
      let prev = sg.p0;
      if (i === 0) table.push({ s: 0, seg: 0, t: 0 });
      for (let k = 1; k <= steps; k++) {
        const t = k / steps;
        const p = bezierPoint(sg.p0, sg.p1, sg.p2, sg.p3, t);
        s += dist(prev, p);
        prev = p;
        table.push({ s, seg: i, t });
      }
    }
    this.table = table;
    this.length = s;
  }
  /** Map station s → (segment, t) via lookup table with linear interpolation. */
  locate(s) {
    const T = this.table;
    s = clamp(s, 0, this.length);
    let lo = 0, hi = T.length - 1;
    while (hi - lo > 1) { const mid = (lo + hi) >> 1; if (T[mid].s <= s) lo = mid; else hi = mid; }
    const a = T[lo], b = T[hi];
    if (a.seg !== b.seg) return b.s - s < s - a.s ? { seg: b.seg, t: b.t } : { seg: a.seg, t: a.t };
    const f = b.s > a.s ? (s - a.s) / (b.s - a.s) : 0;
    return { seg: a.seg, t: a.t + (b.t - a.t) * f };
  }
  pointAt(s) { const { seg, t } = this.locate(s); const g = this.segments[seg]; return bezierPoint(g.p0, g.p1, g.p2, g.p3, t); }
  tangentAt(s) {
    const { seg, t } = this.locate(s); const g = this.segments[seg];
    let d = bezierTangent(g.p0, g.p1, g.p2, g.p3, t);
    if (len(d) < 1e-6) d = sub(g.p3, g.p0);
    return norm(d);
  }
  /** Frame: position, forward (3D unit), plan-right (unit, horizontal), grade. */
  frameAt(s) {
    const p = this.pointAt(s), d = this.tangentAt(s);
    const dxz = normXZ(d);
    const r = rightXZ(dxz);
    const grade = d[1] / Math.max(1e-6, Math.hypot(d[0], d[2]));
    // Plan curvature by finite difference of heading (signed; +ve = turning right)
    const h = 0.5;
    const dA = this.tangentAt(Math.max(0, s - h)), dB = this.tangentAt(Math.min(this.length, s + h));
    const k = wrapAngle(angleXZ(normXZ(dB)) - angleXZ(normXZ(dA))) / (Math.min(this.length, s + h) - Math.max(0, s - h) || 1);
    return { p, d, dxz, r, grade, curvature: k, s };
  }
  /** Closest station to a plan point (coarse table search + refinement). */
  closestStation(pt) {
    let best = 0, bd = Infinity;
    const T = this.table;
    for (let i = 0; i < T.length; i += 2) {
      const p = this.pointAt(T[i].s);
      const d = distXZ(p, pt);
      if (d < bd) { bd = d; best = T[i].s; }
    }
    let step = 0.5;
    for (let it = 0; it < 24; it++) {
      const a = clamp(best - step, 0, this.length), b = clamp(best + step, 0, this.length);
      const da = distXZ(this.pointAt(a), pt), db = distXZ(this.pointAt(b), pt);
      if (da < bd) { bd = da; best = a; } else if (db < bd) { bd = db; best = b; } else step *= 0.5;
    }
    return { s: best, distance: bd };
  }
}

/** Sample stations with max spacing `step` but add extra stations where curvature is high. Returns sorted array incl. 0 and L. */
export function adaptiveStations(spline, step = 2, maxAngle = rad(3), minStep = 0.35) {
  const out = [0];
  let s = 0;
  const L = spline.length;
  while (s < L - 1e-6) {
    const k = Math.abs(spline.frameAt(s).curvature);
    // chord step that keeps heading change under maxAngle
    let ds = k > 1e-5 ? Math.min(step, maxAngle / k) : step;
    ds = Math.max(minStep, ds);
    s = Math.min(L, s + ds);
    if (L - s < minStep * 0.6) s = L;
    out.push(s);
  }
  if (out[out.length - 1] < L) out.push(L);
  return out;
}

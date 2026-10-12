// Pure 2D/3D polyline utilities. Points are [x, z, y] (y optional, defaults 0).
// The ground plane is x/z; y is height.

export function makeRng(seed) {
  let a = seed >>> 0;
  return function rng() {
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export const lerp = (a, b, t) => a + (b - a) * t;
export const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
export const smoothstep = (t) => {
  t = clamp(t, 0, 1);
  return t * t * (3 - 2 * t);
};

// Uniform Catmull-Rom through 2D control points. Returns [x, z] samples ~step apart.
export function sampleCatmull(ctrl, step = 1) {
  const pts = [];
  const n = ctrl.length;
  if (n === 0) return pts;
  if (n === 1) return [[ctrl[0][0], ctrl[0][1]]];
  for (let i = 0; i < n - 1; i++) {
    const p0 = ctrl[Math.max(0, i - 1)];
    const p1 = ctrl[i];
    const p2 = ctrl[i + 1];
    const p3 = ctrl[Math.min(n - 1, i + 2)];
    const segLen = Math.hypot(p2[0] - p1[0], p2[1] - p1[1]);
    const steps = Math.max(1, Math.ceil(segLen / step));
    for (let k = 0; k < steps; k++) {
      const t = k / steps;
      const t2 = t * t;
      const t3 = t2 * t;
      const f = (a, b, c, d) =>
        0.5 *
        (2 * b + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (-a + 3 * b - 3 * c + d) * t3);
      pts.push([f(p0[0], p1[0], p2[0], p3[0]), f(p0[1], p1[1], p2[1], p3[1])]);
    }
  }
  const last = ctrl[n - 1];
  pts.push([last[0], last[1]]);
  return dedupe(pts);
}

export function dedupe(pts, eps = 1e-6) {
  const out = [];
  for (const p of pts) {
    const q = out[out.length - 1];
    if (!q || Math.hypot(p[0] - q[0], p[1] - q[1]) > eps) out.push(p);
  }
  return out;
}

export function withY(pts2d, y = 0) {
  return pts2d.map((p) => [p[0], p[1], p[2] ?? y]);
}

// Cumulative arc length in the xz plane.
export function cumLen(pts) {
  const cum = new Array(pts.length);
  cum[0] = 0;
  for (let i = 1; i < pts.length; i++) {
    cum[i] = cum[i - 1] + Math.hypot(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]);
  }
  return cum;
}

// Index i such that cum[i] <= s < cum[i+1].
export function findSeg(cum, s) {
  let lo = 0;
  let hi = cum.length - 1;
  while (lo < hi - 1) {
    const mid = (lo + hi) >> 1;
    if (cum[mid] <= s) lo = mid;
    else hi = mid;
  }
  return lo;
}

// Point (x, z, y) at arc length s.
export function pointAtS(pts, cum, s) {
  if (s <= 0) return pts[0].slice(0, 3).map((v, k) => (k === 2 ? v ?? 0 : v));
  const last = cum.length - 1;
  if (s >= cum[last]) return withY([pts[last]])[0];
  const i = findSeg(cum, s);
  const a = pts[i];
  const b = pts[i + 1];
  const seg = cum[i + 1] - cum[i] || 1;
  const t = (s - cum[i]) / seg;
  return [lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2] ?? 0, b[2] ?? 0, t)];
}

// Sub-polyline between arc lengths s0 and s1 (inclusive, interpolated ends).
export function sliceS(pts, cum, s0, s1) {
  const out = [pointAtS(pts, cum, s0)];
  let i = findSeg(cum, s0) + 1;
  for (; i < cum.length && cum[i] < s1; i++) {
    if (cum[i] > s0) out.push([pts[i][0], pts[i][1], pts[i][2] ?? 0]);
  }
  out.push(pointAtS(pts, cum, s1));
  return dedupe(out, 1e-4);
}

// Unit left normal (-tz, tx) at vertex i using neighbouring samples.
export function normalAt(pts, i) {
  const a = pts[Math.max(0, i - 1)];
  const b = pts[Math.min(pts.length - 1, i + 1)];
  let tx = b[0] - a[0];
  let tz = b[1] - a[1];
  const l = Math.hypot(tx, tz) || 1;
  tx /= l;
  tz /= l;
  return [-tz, tx, tx, tz]; // [nx, nz, tx, tz]
}

// Segment intersection in 2D. Returns {t, u, x, z} or null. Inclusive of endpoints.
export function segIntersect(ax, az, bx, bz, cx, cz, dx, dz) {
  const rx = bx - ax;
  const rz = bz - az;
  const sx = dx - cx;
  const sz = dz - cz;
  const den = rx * sz - rz * sx;
  if (Math.abs(den) < 1e-12) return null;
  const qpx = cx - ax;
  const qpz = cz - az;
  const t = (qpx * sz - qpz * sx) / den;
  const u = (qpx * rz - qpz * rx) / den;
  const eps = 1e-9;
  if (t < -eps || t > 1 + eps || u < -eps || u > 1 + eps) return null;
  return { t: clamp(t, 0, 1), u: clamp(u, 0, 1), x: ax + rx * t, z: az + rz * t };
}

// Closest point on polyline segment to p. Returns {d, s, x, z}, s relative to cum.
export function closestOnPolyline(pts, cum, px, pz) {
  let best = { d: Infinity, s: 0, x: 0, z: 0 };
  for (let i = 0; i < pts.length - 1; i++) {
    const a = pts[i];
    const b = pts[i + 1];
    const vx = b[0] - a[0];
    const vz = b[1] - a[1];
    const l2 = vx * vx + vz * vz || 1;
    const t = clamp(((px - a[0]) * vx + (pz - a[1]) * vz) / l2, 0, 1);
    const x = a[0] + vx * t;
    const z = a[1] + vz * t;
    const d = Math.hypot(px - x, pz - z);
    if (d < best.d) best = { d, s: cum[i] + Math.hypot(x - a[0], z - a[1]), x, z };
  }
  return best;
}

export function polygonArea(poly) {
  let a = 0;
  for (let i = 0; i < poly.length; i++) {
    const p = poly[i];
    const q = poly[(i + 1) % poly.length];
    a += p[0] * q[1] - q[0] * p[1];
  }
  return a / 2;
}

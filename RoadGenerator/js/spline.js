// Centripetal Catmull-Rom splines in the XZ ground plane.
// A curve is { pts: [[x,z]...], s: [arc length...], tan: [[tx,tz]...], length }.
// Interpolating splines (not Bezier handles) keep the artist's control points exactly on the road.

function dist2(a, b) {
  const dx = b[0] - a[0], dz = b[1] - a[1];
  return Math.hypot(dx, dz);
}

function crSegment(p0, p1, p2, p3, steps, out) {
  const alpha = 0.5;
  const eps = 1e-6;
  const t0 = 0;
  const t1 = t0 + Math.pow(Math.max(dist2(p0, p1), eps), alpha);
  const t2 = t1 + Math.pow(Math.max(dist2(p1, p2), eps), alpha);
  const t3 = t2 + Math.pow(Math.max(dist2(p2, p3), eps), alpha);
  const lerpP = (a, b, ta, tb, t) => {
    const d = tb - ta || eps;
    const w1 = (tb - t) / d, w2 = (t - ta) / d;
    return [a[0] * w1 + b[0] * w2, a[1] * w1 + b[1] * w2];
  };
  for (let i = 0; i < steps; i++) {
    const t = t1 + (t2 - t1) * (i / steps);
    const A1 = lerpP(p0, p1, t0, t1, t), A2 = lerpP(p1, p2, t1, t2, t), A3 = lerpP(p2, p3, t2, t3, t);
    const B1 = lerpP(A1, A2, t0, t2, t), B2 = lerpP(A2, A3, t1, t3, t);
    out.push(lerpP(B1, B2, t1, t2, t));
  }
}

// ctrl: array of {x,z} or [x,z]. step: target sample spacing in metres.
export function buildCurve(ctrlIn, step = 1.0) {
  const ctrl = ctrlIn.map(p => (Array.isArray(p) ? p : [p.x, p.z]));
  const n = ctrl.length;
  const pts = [];
  if (n < 2) return { pts: [], s: [], tan: [], length: 0 };
  for (let i = 0; i < n - 1; i++) {
    const p1 = ctrl[i], p2 = ctrl[i + 1];
    const p0 = i === 0 ? [2 * p1[0] - p2[0], 2 * p1[1] - p2[1]] : ctrl[i - 1];
    const p3 = i + 2 >= n ? [2 * p2[0] - p1[0], 2 * p2[1] - p1[1]] : ctrl[i + 2];
    const len = dist2(p1, p2);
    const steps = Math.max(2, Math.ceil(len / step));
    crSegment(p0, p1, p2, p3, steps, pts);
  }
  pts.push(ctrl[n - 1].slice());
  // Remove exact duplicates that would break tangents.
  const clean = [pts[0]];
  for (let i = 1; i < pts.length; i++) {
    if (dist2(clean[clean.length - 1], pts[i]) > 1e-6) clean.push(pts[i]);
  }
  const s = new Array(clean.length);
  s[0] = 0;
  for (let i = 1; i < clean.length; i++) s[i] = s[i - 1] + dist2(clean[i - 1], clean[i]);
  const tan = clean.map((p, i) => {
    const a = clean[Math.max(0, i - 1)], b = clean[Math.min(clean.length - 1, i + 1)];
    const dx = b[0] - a[0], dz = b[1] - a[1];
    const l = Math.hypot(dx, dz) || 1;
    return [dx / l, dz / l];
  });
  return { pts: clean, s, tan, length: s[s.length - 1] };
}

// Binary search for the sample interval that contains arc length `d`.
function findIndex(s, d) {
  let lo = 0, hi = s.length - 1;
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1;
    if (s[mid] <= d) lo = mid; else hi = mid;
  }
  return lo;
}

// Position + unit tangent on the curve at arc length d (clamped to [0, length]).
export function pointAt(curve, dIn) {
  const d = Math.min(Math.max(dIn, 0), curve.length);
  const i = findIndex(curve.s, d);
  const j = Math.min(i + 1, curve.pts.length - 1);
  const seg = curve.s[j] - curve.s[i] || 1;
  const t = (d - curve.s[i]) / seg;
  const a = curve.pts[i], b = curve.pts[j];
  const ta = curve.tan[i], tb = curve.tan[j];
  const tx = ta[0] + (tb[0] - ta[0]) * t, tz = ta[1] + (tb[1] - ta[1]) * t;
  const l = Math.hypot(tx, tz) || 1;
  return { x: a[0] + (b[0] - a[0]) * t, z: a[1] + (b[1] - a[1]) * t, tx: tx / l, tz: tz / l };
}

// Closest point on the curve to (x,z). Returns { s, x, z, dist }.
export function closestPoint(curve, x, z) {
  let best = { s: 0, x: curve.pts[0][0], z: curve.pts[0][1], dist: Infinity };
  for (let i = 0; i < curve.pts.length - 1; i++) {
    const a = curve.pts[i], b = curve.pts[i + 1];
    const dx = b[0] - a[0], dz = b[1] - a[1];
    const l2 = dx * dx + dz * dz || 1e-12;
    const t = Math.min(1, Math.max(0, ((x - a[0]) * dx + (z - a[1]) * dz) / l2));
    const px = a[0] + dx * t, pz = a[1] + dz * t;
    const d = Math.hypot(x - px, z - pz);
    if (d < best.dist) best = { s: curve.s[i] + (curve.s[i + 1] - curve.s[i]) * t, x: px, z: pz, dist: d };
  }
  return best;
}

// Uniformly spaced arc-length samples in [s0, s1], inclusive of both ends.
export function sampleRange(curve, s0, s1, maxStep = 1.0) {
  const span = Math.max(0, s1 - s0);
  const n = Math.max(1, Math.ceil(span / maxStep));
  const out = [];
  for (let k = 0; k <= n; k++) {
    const d = s0 + (span * k) / n;
    out.push({ s: d, ...pointAt(curve, d) });
  }
  return out;
}

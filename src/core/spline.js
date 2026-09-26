// Centripetal Catmull-Rom splines with arc-length resampling.
// Control points are {x, y, z}; y is elevation (the tunnel height varies).

import { add, dist, norm, scale, sub, v3 } from './vec.js';

function knotsFor(points) {
  // Centripetal parameterisation: t grows with sqrt(chord length).
  const t = [0];
  for (let i = 1; i < points.length; i++) {
    const d = Math.max(dist(points[i], points[i - 1]), 1e-4);
    t.push(t[i - 1] + Math.sqrt(d));
  }
  return t;
}

/** Evaluate the centripetal Catmull-Rom spline at global parameter t. */
export function evalSpline(points, knots, t) {
  const n = points.length;
  if (n === 0) return v3();
  if (n === 1) return { ...points[0] };
  if (t <= knots[0]) return { ...points[0] };
  if (t >= knots[n - 1]) return { ...points[n - 1] };

  let seg = 0;
  while (seg < n - 2 && t > knots[seg + 1]) seg++;

  const p1 = points[seg];
  const p2 = points[seg + 1];
  const p0 = points[Math.max(seg - 1, 0)];
  const p3 = points[Math.min(seg + 2, n - 1)];
  const t0 = knots[Math.max(seg - 1, 0)];
  const t1 = knots[seg];
  const t2 = knots[seg + 1];
  const t3 = knots[Math.min(seg + 2, n - 1)];

  // Non-uniform Catmull-Rom -> cubic Hermite on [t1, t2].
  const dt = t2 - t1;
  const m1 = scale(sub(p2, p0), Math.max(dt / Math.max(t2 - t0, 1e-6), 0));
  const m2 = scale(sub(p3, p1), Math.max(dt / Math.max(t3 - t1, 1e-6), 0));

  const s = (t - t1) / dt;
  const s2 = s * s;
  const s3 = s2 * s;
  const h00 = 2 * s3 - 3 * s2 + 1;
  const h10 = s3 - 2 * s2 + s;
  const h01 = -2 * s3 + 3 * s2;
  const h11 = s3 - s2;

  return v3(
    h00 * p1.x + h10 * m1.x + h01 * p2.x + h11 * m2.x,
    h00 * p1.y + h10 * m1.y + h01 * p2.y + h11 * m2.y,
    h00 * p1.z + h10 * m1.z + h01 * p2.z + h11 * m2.z
  );
}

/**
 * Dense sample of the spline: [{pos, tan, s}] with cumulative arc length s.
 * tan is the unit tangent (mostly horizontal; clamped so side vectors exist).
 */
export function sampleDense(points, perSeg = 24) {
  const n = points.length;
  if (n < 2) return [];
  const knots = knotsFor(points);
  const raw = [];
  const total = (n - 1) * perSeg;
  for (let i = 0; i <= total; i++) {
    const t = knots[0] + ((knots[n - 1] - knots[0]) * i) / total;
    raw.push(evalSpline(points, knots, t));
  }
  // Tangents from neighbours, arc-length cumulative.
  const out = [];
  let s = 0;
  for (let i = 0; i < raw.length; i++) {
    const prev = raw[Math.max(i - 1, 0)];
    const next = raw[Math.min(i + 1, raw.length - 1)];
    let tan = sub(next, prev);
    if (Math.hypot(tan.x, tan.z) < 1e-5) tan = { x: 0, y: 0, z: 1 };
    tan = norm(tan);
    // Guarantee a usable horizontal frame for flat roads.
    if (Math.hypot(tan.x, tan.z) < 0.15) {
      tan = norm({ x: tan.x, y: Math.sign(tan.y || 1) * 0.15, z: tan.z });
    }
    if (i > 0) s += dist(raw[i], raw[i - 1]);
    out.push({ pos: raw[i], tan, s });
  }
  return out;
}

/**
 * Resample a dense polyline at ~uniform arc-length spacing.
 * `extraS` = arc-length positions that MUST be stations (junction mouths etc.).
 * Returns [{pos, tan, s}] sorted by s.
 */
export function resample(dense, spacing, extraS = []) {
  if (dense.length < 2) return dense.map((d) => ({ ...d }));
  const totalS = dense[dense.length - 1].s;
  const targets = new Set();
  for (let s = 0; s <= totalS + 1e-6; s += spacing) targets.add(Math.min(s, totalS));
  targets.add(0);
  targets.add(totalS);
  for (const e of extraS) {
    if (e > 1e-4 && e < totalS - 1e-4) targets.add(e);
  }

  const sorted = [...targets].sort((a, b) => a - b);
  // Merge targets closer than epsilon.
  const merged = [];
  for (const t of sorted) {
    if (merged.length === 0 || t - merged[merged.length - 1] > 1e-3) merged.push(t);
    else if (Math.abs(t - totalS) < 1e-3) merged[merged.length - 1] = totalS;
  }

  const out = [];
  let j = 0;
  for (const target of merged) {
    while (j < dense.length - 2 && dense[j + 1].s < target) j++;
    const a = dense[j];
    const b = dense[j + 1];
    const span = b.s - a.s;
    const u = span > 1e-9 ? Math.min(Math.max((target - a.s) / span, 0), 1) : 0;
    out.push({
      pos: v3(
        a.pos.x + (b.pos.x - a.pos.x) * u,
        a.pos.y + (b.pos.y - a.pos.y) * u,
        a.pos.z + (b.pos.z - a.pos.z) * u
      ),
      tan: norm(v3(
        a.tan.x + (b.tan.x - a.tan.x) * u,
        a.tan.y + (b.tan.y - a.tan.y) * u,
        a.tan.z + (b.tan.z - a.tan.z) * u
      )),
      s: target,
    });
  }
  return out;
}

/** Closest point on a dense polyline to p (XZ metric), returns {s, pos, dist}. */
export function projectOnDense(dense, p) {
  let best = { s: 0, pos: dense[0]?.pos ?? v3(), dist: Infinity, seg: 0, u: 0 };
  for (let i = 0; i < dense.length - 1; i++) {
    const a = dense[i];
    const b = dense[i + 1];
    const abx = b.pos.x - a.pos.x;
    const abz = b.pos.z - a.pos.z;
    const apx = p.x - a.pos.x;
    const apz = p.z - a.pos.z;
    const ab2 = abx * abx + abz * abz;
    const u = ab2 > 1e-12 ? Math.min(Math.max((apx * abx + apz * abz) / ab2, 0), 1) : 0;
    const cx = a.pos.x + abx * u;
    const cz = a.pos.z + abz * u;
    const dx = p.x - cx;
    const dz = p.z - cz;
    const d2 = dx * dx + dz * dz;
    if (d2 < best.dist) {
      best = {
        s: a.s + (b.s - a.s) * u,
        pos: v3(cx, a.pos.y + (b.pos.y - a.pos.y) * u, cz),
        dist: Math.sqrt(d2),
        seg: i,
        u,
      };
    }
  }
  return best;
}

/** XZ segment intersection between dense[i0..i0+1] and dense2[i1..i1+1]. */
export function segIntersectXZ(a0, a1, b0, b1) {
  const d1x = a1.x - a0.x;
  const d1z = a1.z - a0.z;
  const d2x = b1.x - b0.x;
  const d2z = b1.z - b0.z;
  const den = d1x * d2z - d1z * d2x;
  if (Math.abs(den) < 1e-9) return null;
  const t = ((b0.x - a0.x) * d2z - (b0.z - a0.z) * d2x) / den;
  const u = ((b0.x - a0.x) * d1z - (b0.z - a0.z) * d1x) / den;
  if (t < 0 || t > 1 || u < 0 || u > 1) return null;
  return {
    t,
    u,
    x: a0.x + d1x * t,
    z: a0.z + d1z * t,
  };
}

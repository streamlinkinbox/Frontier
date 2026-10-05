//============================================================================================================================================
//                                                                SPLINE.JS
//============================================================================================================================================
// Centripetal Catmull-Rom corridor splines.
//
// The reference project exposed raw Bézier handles, which is where most of its curve breakage came from: handles could
// cross, produce cusps, invert tangents and tear the swept cross-section apart. RoadWorks instead stores only control
// points plus a single tension value and derives tangents with the centripetal parameterisation, which is provably
// free of cusps and self-intersections inside a span. Smoothness is therefore a property of the solver rather than
// something the user has to hand-tune.

import { clamp, dist, lerp, sub, vec } from './Vec.js?v=3';
import { dedupe, polylineLength } from './Polyline.js?v=3';

const ALPHA = 0.5; // centripetal

function tangentFor(p0, p1, p2, t0, t1, t2, tension) {
  const d1 = Math.max(t1 - t0, 1e-5);
  const d2 = Math.max(t2 - t1, 1e-5);
  // Non-uniform Catmull-Rom tangent (Barry & Goldman), scaled by (1 - tension).
  const s = 1 - clamp(tension, 0, 0.95);
  const ax = (p1.x - p0.x) / d1 - (p2.x - p0.x) / (d1 + d2) + (p2.x - p1.x) / d2;
  const ay = (p1.y - p0.y) / d1 - (p2.y - p0.y) / (d1 + d2) + (p2.y - p1.y) / d2;
  const az = (p1.z - p0.z) / d1 - (p2.z - p0.z) / (d1 + d2) + (p2.z - p1.z) / d2;
  return { x: ax * d2 * s, y: ay * d2 * s, z: az * d2 * s };
}

function hermite(p0, p1, m0, m1, t) {
  const t2 = t * t;
  const t3 = t2 * t;
  const h00 = 2 * t3 - 3 * t2 + 1;
  const h10 = t3 - 2 * t2 + t;
  const h01 = -2 * t3 + 3 * t2;
  const h11 = t3 - t2;
  return {
    x: h00 * p0.x + h10 * m0.x + h01 * p1.x + h11 * m1.x,
    y: h00 * p0.y + h10 * m0.y + h01 * p1.y + h11 * m1.y,
    z: h00 * p0.z + h10 * m0.z + h01 * p1.z + h11 * m1.z,
  };
}

// Samples a corridor spline into a dense polyline. `step` is the target spacing in metres; curvature adaptively
// densifies spans so tight turns stay smooth without exploding vertex counts on long straights.
export function sampleSpline(controlPoints, { closed = false, tension = 0, step = 2.0 } = {}) {
  const pts = dedupe(controlPoints, 1e-3);
  if (pts.length < 2) return pts.map((p) => ({ ...p }));
  if (pts.length === 2 && !closed) {
    const n = Math.max(2, Math.ceil(dist(pts[0], pts[1]) / step));
    const out = [];
    for (let i = 0; i <= n; i++) out.push(lerp(pts[0], pts[1], i / n));
    return out;
  }

  const n = pts.length;
  const spanCount = closed ? n : n - 1;
  const out = [];

  for (let i = 0; i < spanCount; i++) {
    const p0 = pts[closed ? (i - 1 + n) % n : Math.max(i - 1, 0)];
    const p1 = pts[i % n];
    const p2 = pts[(i + 1) % n];
    const p3 = pts[closed ? (i + 2) % n : Math.min(i + 2, n - 1)];

    const t0 = 0;
    const t1 = t0 + Math.pow(Math.max(dist(p0, p1), 1e-4), ALPHA);
    const t2 = t1 + Math.pow(Math.max(dist(p1, p2), 1e-4), ALPHA);
    const t3 = t2 + Math.pow(Math.max(dist(p2, p3), 1e-4), ALPHA);

    const m0 = tangentFor(p0, p1, p2, t0, t1, t2, tension);
    const m1 = tangentFor(p1, p2, p3, t1, t2, t3, tension);

    const chord = dist(p1, p2);
    const bow = Math.max(dist(p1, p0), dist(p2, p3), chord);
    const divisions = clamp(Math.ceil((chord + bow * 0.35) / step), 2, 160);

    for (let s = 0; s < divisions; s++) {
      out.push(hermite(p1, p2, m0, m1, s / divisions));
    }
  }

  if (closed) out.push({ ...out[0] });
  else out.push({ ...pts[n - 1] });

  return dedupe(out, 1e-4);
}

// Projects a world point onto the spline polyline, returning the closest sample index and distance — used for
// inserting new control points by clicking on a corridor.
export function closestOnPolyline(points, target) {
  let best = { index: 0, distance: Infinity, point: points[0], t: 0 };
  for (let i = 0; i < points.length - 1; i++) {
    const a = points[i];
    const b = points[i + 1];
    const d = sub(b, a);
    const lenSq = d.x * d.x + d.y * d.y + d.z * d.z;
    if (lenSq < 1e-9) continue;
    const t = clamp(((target.x - a.x) * d.x + (target.y - a.y) * d.y + (target.z - a.z) * d.z) / lenSq, 0, 1);
    const proj = lerp(a, b, t);
    const dd = Math.hypot(proj.x - target.x, proj.y - target.y, proj.z - target.z);
    if (dd < best.distance) best = { index: i, distance: dd, point: proj, t };
  }
  return best;
}

export function splineLength(controlPoints, options) {
  return polylineLength(sampleSpline(controlPoints, options));
}

export { vec };

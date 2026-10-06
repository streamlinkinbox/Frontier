//============================================================================================================================================
//                                                               POLYLINE.JS
//============================================================================================================================================
// Arc-length utilities shared by the corridor, junction and bridge generators: measuring, trimming, resampling and
// building stable Frenet-ish frames (tangent / left / up) that never flip on curved corridors.

import { add, addScaled, clamp, cross, dist, lerp, norm, sub, vec } from './Vec.js?v=11';

export function cumulativeLengths(points) {
  const out = [0];
  for (let i = 1; i < points.length; i++) out.push(out[i - 1] + dist(points[i], points[i - 1]));
  return out;
}

export function polylineLength(points) {
  if (!points || points.length < 2) return 0;
  const cum = cumulativeLengths(points);
  return cum[cum.length - 1];
}

export function pointOnPolyline(points, distance) {
  if (!points.length) return vec();
  if (points.length === 1) return { ...points[0] };
  const cum = cumulativeLengths(points);
  const total = cum[cum.length - 1];
  const d = clamp(distance, 0, total);
  for (let i = 0; i < points.length - 1; i++) {
    const span = cum[i + 1] - cum[i];
    if (span <= 1e-9) continue;
    if (d <= cum[i + 1] || i === points.length - 2) {
      return lerp(points[i], points[i + 1], clamp((d - cum[i]) / span, 0, 1));
    }
  }
  return { ...points[points.length - 1] };
}

// Removes `startTrim` metres from the head and `endTrim` metres from the tail, keeping interior vertices intact.
export function trimPolyline(points, startTrim, endTrim) {
  if (points.length < 2) return points.map((p) => ({ ...p }));
  const cum = cumulativeLengths(points);
  const total = cum[cum.length - 1];
  if (total <= 1e-6) return points.map((p) => ({ ...p }));

  let a = clamp(startTrim, 0, total);
  let b = clamp(total - endTrim, 0, total);
  if (b - a < total * 0.02) {
    // Degenerate trim (junction radii overlap): keep a sliver centred on the segment.
    const mid = total * 0.5;
    const half = Math.max(total * 0.01, 0.05);
    a = mid - half;
    b = mid + half;
  }

  const out = [pointOnPolyline(points, a)];
  for (let i = 0; i < points.length; i++) {
    if (cum[i] > a + 1e-6 && cum[i] < b - 1e-6) out.push({ ...points[i] });
  }
  out.push(pointOnPolyline(points, b));
  return dedupe(out, 1e-4);
}

export function resamplePolyline(points, segments) {
  const n = Math.max(1, segments | 0);
  if (points.length === 0) return [];
  if (points.length === 1) return new Array(n + 1).fill(0).map(() => ({ ...points[0] }));
  const total = polylineLength(points);
  const out = [];
  for (let i = 0; i <= n; i++) out.push(pointOnPolyline(points, (total * i) / n));
  return out;
}

export function dedupe(points, epsilon = 1e-3) {
  const out = [];
  for (const p of points) {
    if (out.length && dist(out[out.length - 1], p) <= epsilon) continue;
    out.push({ ...p });
  }
  return out;
}

// Tangent / left / up frame at a vertex. Interior vertices use the central difference so curb offsets stay smooth
// through curves instead of pinching at every sample (this is the fix for the "curbs break on curves" problem).
export function frameAt(points, index) {
  let tangent;
  if (points.length < 2) tangent = vec(1, 0, 0);
  else if (index <= 0) tangent = sub(points[1], points[0]);
  else if (index >= points.length - 1) tangent = sub(points[points.length - 1], points[points.length - 2]);
  else tangent = sub(points[index + 1], points[index - 1]);

  const t = norm(tangent, vec(1, 0, 0));
  let upRef = vec(0, 0, 1);
  if (Math.abs(t.z) > 0.98) upRef = vec(0, 1, 0);
  const left = norm(cross(upRef, t), vec(0, 1, 0));
  const up = norm(cross(t, left), vec(0, 0, 1));
  return { tangent: t, left, up };
}

// Miter-corrected offsets: on a curve the straight perpendicular offset of each sample under-shoots the true parallel
// curve, producing the gaps / overlaps seen in the reference project. Scaling by 1/cos(half turn angle) fixes it.
export function miterScale(points, index, maxScale = 2.5) {
  if (index <= 0 || index >= points.length - 1) return 1;
  const a = norm(sub(points[index], points[index - 1]), vec(1, 0, 0));
  const b = norm(sub(points[index + 1], points[index]), vec(1, 0, 0));
  const cosFull = clamp(a.x * b.x + a.y * b.y + a.z * b.z, -1, 1);
  const half = Math.acos(cosFull) * 0.5;
  const c = Math.cos(half);
  if (c < 1e-3) return maxScale;
  return Math.min(1 / c, maxScale);
}

export function offsetPoint(point, frame, lateral, vertical = 0, scale = 1) {
  return add(addScaled(point, frame.left, lateral * scale), { x: 0, y: 0, z: vertical });
}

export function tangentXY(points, index) {
  const f = frameAt(points, index);
  const l = Math.hypot(f.tangent.x, f.tangent.y);
  if (l < 1e-6) return { x: 1, y: 0 };
  return { x: f.tangent.x / l, y: f.tangent.y / l };
}

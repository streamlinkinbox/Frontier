//============================================================================================================================================
//                                                                 RAY.JS
//============================================================================================================================================
// The two closest-point solves the translate gizmo needs, as plain functions on plain {x,y,z} objects.
//
// They live outside Gizmo.js on purpose: Gizmo.js imports three.js, which means it cannot be unit-tested headlessly,
// and a sign error in here is invisible in review but very obvious in use — dragging the Z handle up used to move
// the point *down*, because the line parameter was negated on its way out.

const dot = (a, b) => a.x * b.x + a.y * b.y + a.z * b.z;

// Closest point to `ray` on the infinite line through `origin` with direction `dir` (unit length not required).
// Returns the line parameter s, such that the point is origin + dir * s. Null when the two lines are parallel.
export function closestLineParam(origin, dir, rayOrigin, rayDir) {
  const w0 = { x: origin.x - rayOrigin.x, y: origin.y - rayOrigin.y, z: origin.z - rayOrigin.z };
  const a = dot(dir, dir);
  const b = dot(dir, rayDir);
  const c = dot(rayDir, rayDir);
  const d = dot(dir, w0);
  const e = dot(rayDir, w0);
  const denom = a * c - b * b;
  if (Math.abs(denom) < 1e-6) return null;
  return (b * e - c * d) / denom;
}

// Convenience for the vertical (Z) gizmo axis: the world Z of the closest point on the vertical line through origin.
export function closestZOnVerticalLine(origin, rayOrigin, rayDir) {
  const s = closestLineParam(origin, { x: 0, y: 0, z: 1 }, rayOrigin, rayDir);
  return s === null ? null : origin.z + s;
}

import { cross, dot, normalize, scale, sub, add } from "./math";
import type { BrushStamp, Vec3 } from "./types";

export function makeBrushStamp(
  origin: Vec3,
  normal: Vec3 = [0, 1, 0],
  tangent: Vec3 = [1, 0, 0],
  seed = 1,
): BrushStamp {
  const n =
    Math.hypot(...normal) > 1e-6 ? normalize(normal) : ([0, 1, 0] as Vec3);
  let t = sub(tangent, scale(n, dot(tangent, n)));
  if (Math.hypot(...t) < 1e-6)
    t = cross(Math.abs(n[1]) < 0.9 ? [0, 1, 0] : [0, 0, 1], n);
  return {
    origin: [...origin],
    normal: n,
    tangent: normalize(t),
    previous: [...origin],
    seed,
  };
}
export function projectToBrushPlane(point: Vec3, stamp: BrushStamp): Vec3 {
  return sub(
    point,
    scale(stamp.normal, dot(sub(point, stamp.origin), stamp.normal)),
  );
}
export function brushPlaneHit(
  eye: Vec3,
  direction: Vec3,
  stamp: BrushStamp,
): Vec3 | null {
  const denominator = dot(direction, stamp.normal);
  if (Math.abs(denominator) < 0.035) return null;
  const t = dot(sub(stamp.origin, eye), stamp.normal) / denominator;
  return t > 0 && t < 600 ? add(eye, scale(direction, t)) : null;
}

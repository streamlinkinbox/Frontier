import { noise } from "./field";
import { clamp, mix, smoothstep } from "./math";

/** The material bands in the renderer and mechanical resistance in the solver
 * share this warped, gently dipping stratigraphy (see common.wgsl). */
export function strataCoordinate(x: number, y: number, z: number): number {
  return (
    y + x * 0.025 + z * 0.017 + noise(x * 0.047, y * 0.018, z * 0.047) * 0.85
  );
}
export function strataStrength(x: number, y: number, z: number): number {
  const layer = strataCoordinate(x, y, z);
  return (
    0.2 +
    0.75 *
      smoothstep(
        -0.5,
        0.65,
        Math.sin(layer * 1.05 + noise(x * 0.16, y * 0.06, z * 0.16) * 0.24),
      )
  );
}
export function erodibility(
  x: number,
  y: number,
  z: number,
  resistance: number,
): number {
  return mix(1, 1 - strataStrength(x, y, z) * 0.82, resistance);
}
/** A cut-cell material-volume proxy, not an exact mesh-volume integration. */
export function solidFraction(distance: number, cell: number): number {
  return clamp(0.5 - distance / (2 * cell), 0, 1);
}

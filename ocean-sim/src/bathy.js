import { PARAMS } from './config.js?v=5';

// ---------------------------------------------------------------------------
// Analytic bathymetry — MUST match the GLSL `bathymetry()` in glsl.js exactly.
// Returns water depth in meters (positive = water, negative = dry land).
//
// Layout: shoreline runs along Z near x = shoreX. Ocean occupies x < shoreX.
// A reef bar paralleling the shore (skewed by reefAngle) creates the surf zone.
// ---------------------------------------------------------------------------
export const BEACH_SLOPE = 0.055;
export const REEF_WIDTH = 28;

export function bathyJS(x, z) {
  const P = PARAMS;
  const shoreX = P.shoreX + Math.tan(P.shoreAngle * Math.PI / 180) * z;
  let d = (shoreX - x) * BEACH_SLOPE;
  d += 1.2 * Math.sin(x * 0.011 + 1.7) * Math.sin(z * 0.013 + 0.4);
  d += 0.5 * Math.sin(x * 0.043 + 0.3) * Math.sin(z * 0.037 + 2.1);
  const reefX = P.reefX + Math.tan(P.reefAngle * Math.PI / 180) * z;
  const q = (x - reefX) / REEF_WIDTH;
  const g = Math.exp(-q * q);
  const barD = Math.min(d, P.reefDepth + 0.4 * Math.sin(z * 0.05));
  d = d + (barD - d) * Math.min(g, 1);
  return Math.min(Math.max(d, -7), 42);
}

export function reefXAt(z) {
  const P = PARAMS;
  return P.reefX + Math.tan(P.reefAngle * Math.PI / 180) * z;
}

export function shoreXAt(z) {
  const P = PARAMS;
  return P.shoreX + Math.tan(P.shoreAngle * Math.PI / 180) * z;
}

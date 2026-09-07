import { clamp } from "./math";

/** Exact unpolarized dielectric Fresnel, including total internal reflection.
 * etaI/etaT explicitly distinguish air→water from water→air (PBRT §9.3). */
export function dielectricFresnel(
  cosine: number,
  etaI: number,
  etaT: number,
): number {
  const c = clamp(Math.abs(cosine), 0, 1),
    eta = etaI / etaT;
  const sin2 = eta * eta * (1 - c * c);
  if (sin2 >= 1) return 1;
  const ct = Math.sqrt(Math.max(0, 1 - sin2));
  const rs = (etaI * c - etaT * ct) / Math.max(1e-8, etaI * c + etaT * ct);
  const rp = (etaT * c - etaI * ct) / Math.max(1e-8, etaT * c + etaI * ct);
  return (rs * rs + rp * rp) * 0.5;
}
export function transmittance(distance: number, coefficient: number): number {
  return Math.exp(-Math.max(0, distance) * Math.max(0, coefficient));
}
/** An offset must not cross the known clearance to rock. The shader also
 * samples the candidate point and rejects it if it is inside the solid. */
export function waterRayBias(clearance: number): number {
  return Math.min(0.012, Math.max(0, clearance) * 0.15);
}
export function ggxBRDF(
  noV: number,
  noL: number,
  noH: number,
  voH: number,
  roughness: number,
  f0: number,
  albedo: number,
): number {
  const r = clamp(roughness, 0.12, 1),
    alpha = r * r,
    a2 = alpha * alpha;
  const d = a2 / (Math.PI * (noH * noH * (a2 - 1) + 1) ** 2);
  const visibility =
    0.5 /
    Math.max(
      1e-6,
      noL * Math.sqrt(noV * noV * (1 - a2) + a2) +
        noV * Math.sqrt(noL * noL * (1 - a2) + a2),
    );
  const f = f0 + (1 - f0) * (1 - voH) ** 5;
  const fv = f0 + (1 - f0) * (1 - noV) ** 5,
    fl = f0 + (1 - f0) * (1 - noL) ** 5;
  return (albedo * (1 - fv) * (1 - fl)) / Math.PI + d * visibility * f;
}

/** Partial water coverage mixes two already-attenuated optical paths. */
export function waterEdgeRadiance(
  opaque: number,
  surface: number,
  coverage: number,
  opaqueDistance: number,
  surfaceDistance: number,
  coefficient: number,
): number {
  const a = opaque * transmittance(opaqueDistance, coefficient),
    b = surface * transmittance(surfaceDistance, coefficient);
  return a + (b - a) * clamp(coverage, 0, 1);
}

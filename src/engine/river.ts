import { noise } from "./field";
import { gradientNoise } from "./noise";
import { followsChannel, riverCoordinates } from "./flowRoute";
import { smoothstep } from "./math";
import type { Settings, Vec3 } from "./types";
export function riverAmplitude(s: Settings): number {
  return 0.055 * s.wind * s.wind + 0.022 * Math.min(s.waterCurrent, 2);
}
export function riverSlopeBound(s: Settings): number {
  return (
    (riverAmplitude(s) * (13 / s.waterRippleScale + 4.5) + 0.005) *
    (followsChannel(s) ? 1.5 : 1)
  );
}
/** Height + world X/Z derivatives. No UV tiling, looping time, or periodic
 * crossing sine bands. The stationary low-frequency warp breaks the grid;
 * different advected octaves travel downstream at slightly different speeds. */
export function riverSample(
  p: Vec3,
  time: number,
  s: Settings,
  footprint = 0,
): Vec3 {
  const amplitude = riverAmplitude(s);
  if (amplitude === 0) return [0, 0, 0];
  const route = riverCoordinates(p, s);
  const along = route.along,
    across = route.across,
    scale = s.waterRippleScale;
  const evolution = (s.wind + s.waterCurrent * 0.5) * 0.025,
    speed = s.waterCurrent + s.wind * 0.08;
  const wa = gradientNoise(
    along * 0.075 + s.seed * 0.013,
    across * 0.075 + 11.4,
    time * evolution * 0.35 + 4.8,
  );
  const wb = gradientNoise(
    along * 0.075 + 3.1,
    across * 0.075 + 39.2,
    time * evolution * 0.29 + s.seed * 0.005,
  );
  const a = along + wa[0] * scale * 0.85,
    c = across + wb[0] * scale * 0.65;
  const jaa = 1 + wa[1] * 0.075 * scale * 0.85,
    jac = wa[2] * 0.075 * scale * 0.85;
  const jca = wb[1] * 0.075 * scale * 0.65,
    jcc = 1 + wb[2] * 0.075 * scale * 0.65;
  const a1 = a - time * speed,
    a2 = a - time * speed * 0.83,
    a3 = a - time * speed * 1.21;
  const n1 = gradientNoise(
    (a1 * 1.37) / scale,
    (c * 0.61) / scale,
    time * evolution,
  );
  const n2 = gradientNoise(
    ((a2 * 0.82 + c * 0.572) * 3.11) / scale + 11.3,
    ((-a2 * 0.572 + c * 0.82) * 1.17) / scale + 43.7,
    time * evolution * 0.87 + 17.31,
  );
  const n3 = gradientNoise(
    ((a3 * 0.37 - c * 0.929) * 6.43) / scale + 8.7,
    ((a3 * 0.929 + c * 0.37) * 2.71) / scale + 3.2,
    time * evolution * 1.41 + 4.7,
  );
  const w3 = 0.11 * (1 - smoothstep(0.15, 0.75, (footprint * 6.43) / scale));
  const height = amplitude * (n1[0] * 0.62 + n2[0] * 0.27 + n3[0] * w3);
  const ga =
    (n1[1] * 1.37 * 0.62 +
      (n2[1] * 0.82 * 3.11 - n2[2] * 0.572 * 1.17) * 0.27 +
      (n3[1] * 0.37 * 6.43 + n3[2] * 0.929 * 2.71) * w3) /
    scale;
  const gc =
    (n1[2] * 0.61 * 0.62 +
      (n2[1] * 0.572 * 3.11 + n2[2] * 0.82 * 1.17) * 0.27 +
      (-n3[1] * 0.929 * 6.43 + n3[2] * 0.37 * 2.71) * w3) /
    scale;
  const da = (ga * jaa + gc * jca) * amplitude,
    dc = (ga * jac + gc * jcc) * amplitude;
  return [
    height,
    da * route.du[0] + dc * route.dv[0],
    da * route.du[1] + dc * route.dv[1],
  ];
}

/** Visible foam/current tracers translate in arc-distance coordinates. */
export function currentStreak(p: Vec3, time: number, s: Settings): number {
  const route = riverCoordinates(p, s),
    travel = route.along - s.waterCurrent * time;
  const ribbon = noise(travel * 0.24, route.across * 1.45, s.seed * 0.005);
  const broken = noise(
    travel * 0.61 + 13.7,
    route.across * 0.8 + 4.1,
    s.seed * 0.003,
  );
  return (
    ((1 - smoothstep(0.035, 0.16, Math.abs(ribbon))) *
      smoothstep(-0.3, 0.5, broken) *
      s.waterStreaks *
      Math.min(s.waterCurrent, 1.5)) /
    1.5
  );
}

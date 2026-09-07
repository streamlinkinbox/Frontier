import { hash } from "./field";
import { smoothstep } from "./math";
import type { Settings, Vec3 } from "./types";
type NoiseSample = [number, number, number, number];

/** CPU reference for the same analytic, non-periodic noise used by the shader. */
function gradientNoise(x: number, y: number, z: number): NoiseSample {
  const ix = Math.floor(x),
    iy = Math.floor(y),
    iz = Math.floor(z),
    fx = x - ix,
    fy = y - iy,
    fz = z - iz;
  const wx = fx * fx * (3 - 2 * fx),
    wy = fy * fy * (3 - 2 * fy),
    wz = fz * fz * (3 - 2 * fz);
  const dx = 6 * fx * (1 - fx),
    dy = 6 * fy * (1 - fy),
    dz = 6 * fz * (1 - fz);
  const a = hash(ix, iy, iz),
    b = hash(ix + 1, iy, iz),
    c = hash(ix, iy + 1, iz),
    d = hash(ix + 1, iy + 1, iz);
  const e = hash(ix, iy, iz + 1),
    f = hash(ix + 1, iy, iz + 1),
    g = hash(ix, iy + 1, iz + 1),
    h = hash(ix + 1, iy + 1, iz + 1);
  const k1 = b - a,
    k2 = c - a,
    k3 = e - a,
    k4 = a - b - c + d,
    k5 = a - c - e + g,
    k6 = a - b - e + f,
    k7 = -a + b + c - d + e - f - g + h;
  return [
    (a +
      k1 * wx +
      k2 * wy +
      k3 * wz +
      k4 * wx * wy +
      k5 * wy * wz +
      k6 * wz * wx +
      k7 * wx * wy * wz) *
      2 -
      1,
    dx * (k1 + k4 * wy + k6 * wz + k7 * wy * wz) * 2,
    dy * (k2 + k4 * wx + k5 * wz + k7 * wx * wz) * 2,
    dz * (k3 + k5 * wy + k6 * wx + k7 * wx * wy) * 2,
  ];
}
export function riverAmplitude(s: Settings): number {
  return 0.055 * s.wind * s.wind + 0.022 * Math.min(s.waterCurrent, 2);
}
export function riverSlopeBound(s: Settings): number {
  return riverAmplitude(s) * (13 / s.waterRippleScale + 4.5) + 0.005;
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
  const angle = (s.waterDirection * Math.PI) / 180,
    dx = Math.cos(angle),
    dz = Math.sin(angle);
  const along = p[0] * dx + p[2] * dz,
    across = -p[0] * dz + p[2] * dx,
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
  return [height, da * dx - dc * dz, da * dz + dc * dx];
}

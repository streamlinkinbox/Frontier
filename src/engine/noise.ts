import { hash } from "./field";
type NoiseSample = [number, number, number, number];

/** CPU reference for the same analytic, non-periodic noise used by the shader. */
export function gradientNoise(x: number, y: number, z: number): NoiseSample {
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

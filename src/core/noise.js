// Deterministic, world-space value noise. Everything procedural in the
// generator keys off world position so two mesh pieces that meet at the same
// point always generate the exact same profile (watertight stitching).

function hash2(ix, iz, seed) {
  let h = Math.imul(ix | 0, 0x27d4eb2d) ^ Math.imul(iz | 0, 0x165667b1) ^ Math.imul(seed | 0, 0x9e3779b9);
  h = Math.imul(h ^ (h >>> 15), 0x85ebca6b);
  h = Math.imul(h ^ (h >>> 13), 0xc2b2ae35);
  h ^= h >>> 16;
  return (h >>> 0) / 4294967295; // 0..1
}

const smooth = (t) => t * t * (3 - 2 * t);

/** Value noise in [0, 1]. */
export function noise2(x, z, seed = 0) {
  const ix = Math.floor(x);
  const iz = Math.floor(z);
  const fx = x - ix;
  const fz = z - iz;
  const sx = smooth(fx);
  const sz = smooth(fz);
  const a = hash2(ix, iz, seed);
  const b = hash2(ix + 1, iz, seed);
  const c = hash2(ix, iz + 1, seed);
  const d = hash2(ix + 1, iz + 1, seed);
  return (
    a * (1 - sx) * (1 - sz) +
    b * sx * (1 - sz) +
    c * (1 - sx) * sz +
    d * sx * sz
  );
}

/** Signed noise in [-1, 1]. */
export function snoise2(x, z, seed = 0) {
  return noise2(x, z, seed) * 2 - 1;
}

/** Fractal noise in [0, 1]. */
export function fbm2(x, z, seed = 0, octaves = 3) {
  let amp = 0.5;
  let freq = 1;
  let sum = 0;
  let norm = 0;
  for (let i = 0; i < octaves; i++) {
    sum += amp * noise2(x * freq, z * freq, seed + i * 101);
    norm += amp;
    amp *= 0.5;
    freq *= 2.05;
  }
  return sum / norm;
}

/** Deterministic scalar from a single integer (per-tier variation etc.). */
export function hash1(i, seed = 0) {
  return hash2(i, 0x51ed, seed);
}

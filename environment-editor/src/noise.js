// Small deterministic noise / RNG helpers.
// IMPORTANT (anti-banding rule for this editor): noise here is used ONLY for
// large-scale, low-frequency domain variation — cliff-line meander in plan,
// stratigraphic folding, lateral bed pinch-out, joint persistence masks.
// It is never displaced along Y or slope-blended onto cliff faces; cliff
// relief comes from geology (beds + joints) acted on by erosion processes.

export const clamp = (x, a, b) => Math.min(b, Math.max(a, x));
export const lerp = (a, b, t) => a + (b - a) * t;
export const smoothstep = (a, b, x) => {
  const t = clamp((x - a) / (b - a), 0, 1);
  return t * t * (3 - 2 * t);
};
export const smin = (a, b, k) => {
  if (k <= 1e-6) return Math.min(a, b);
  const h = clamp(0.5 + 0.5 * (b - a) / k, 0, 1);
  return lerp(b, a, h) - k * h * (1 - h);
};
export const smax = (a, b, k) => -smin(-a, -b, k);

export function mulberry32(seed) {
  let a = seed >>> 0;
  return function next() {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function hash3(ix, iy, iz, seed = 0) {
  let h = (Math.imul(ix, 374761393) + Math.imul(iy, 668265263) + Math.imul(iz, 1274126177) + Math.imul(seed | 0, 2246822519)) | 0;
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}
const fade = (t) => t * t * (3 - 2 * t);

/** Trilinear value noise in [0,1]. */
export function value3(x, y, z, seed = 0) {
  const ix = Math.floor(x), iy = Math.floor(y), iz = Math.floor(z);
  const fx = fade(x - ix), fy = fade(y - iy), fz = fade(z - iz);
  const c = (dx, dy, dz) => hash3(ix + dx, iy + dy, iz + dz, seed);
  const x00 = lerp(c(0, 0, 0), c(1, 0, 0), fx);
  const x10 = lerp(c(0, 1, 0), c(1, 1, 0), fx);
  const x01 = lerp(c(0, 0, 1), c(1, 0, 1), fx);
  const x11 = lerp(c(0, 1, 1), c(1, 1, 1), fx);
  return lerp(lerp(x00, x10, fy), lerp(x01, x11, fy), fz);
}

export const value2 = (x, z, seed = 0) => value3(x, seed * 0.173, z, seed);

export function fbm3(x, y, z, octaves = 4, seed = 0) {
  let a = 0.5, s = 0, n = 0;
  for (let i = 0; i < octaves; i++) {
    s += a * value3(x, y, z, seed + i * 101);
    n += a;
    x = x * 2.03 + 17.1; y = y * 2.01 + 3.7; z = z * 1.97 + 9.3;
    a *= 0.5;
  }
  return s / n;
}
export const fbm2 = (x, z, octaves = 4, seed = 0) => fbm3(x, seed * 0.317, z, octaves, seed);

export function hexToRgb(hex) {
  // Colours are authored as sRGB hex but shading happens in linear space and
  // the renderer gamma-encodes at the end — decode here or everything washes out.
  const n = parseInt(hex.slice(1), 16);
  const dec = (v) => { const s = v / 255; return s <= 0.04045 ? s / 12.92 : Math.pow((s + 0.055) / 1.055, 2.4); };
  return [dec((n >> 16) & 255), dec((n >> 8) & 255), dec(n & 255)];
}

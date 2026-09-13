// ============================================================================
// Frontier SDF Terrain — Seeded procedural noise library (DOM-free, Node-safe)
// All generators are deterministic functions of (x, y, z, seed).
// Ranges: value/perlin/simplex basis ~ [-1, 1]; fbm/ridged/etc documented below.
// ============================================================================

// --- Seeded PRNG -------------------------------------------------------------
export function mulberry32(seed) {
  let a = seed >>> 0;
  return function () {
    a |= 0; a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// --- Integer lattice hash -> [0, 1) ------------------------------------------
export function hash3(ix, iy, iz, seed) {
  let h = (seed | 0) ^ 0x9e3779b9;
  h = Math.imul(h ^ Math.imul(ix | 0, 374761393), 668265263);
  h = Math.imul(h ^ Math.imul(iy | 0, 2246822519), 3266489917);
  h = Math.imul(h ^ Math.imul(iz | 0, 3266489917), 668265263);
  h ^= h >>> 13;
  h = Math.imul(h, 1274126177);
  h ^= h >>> 16;
  return (h >>> 0) / 4294967296;
}

export function hash1(n, seed) {
  let h = Math.imul((n | 0) ^ (seed | 0), 374761393);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  h ^= h >>> 16;
  return (h >>> 0) / 4294967296;
}

export const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
export const lerp = (a, b, t) => a + (b - a) * t;
export const smootherstep = (t) => t * t * t * (t * (t * 6 - 15) + 10);

// --- Value noise 3D, [-1, 1] ---------------------------------------------------
export function valueNoise3(x, y, z, seed) {
  const xi = Math.floor(x), yi = Math.floor(y), zi = Math.floor(z);
  const xf = x - xi, yf = y - yi, zf = z - zi;
  const u = smootherstep(xf), v = smootherstep(yf), w = smootherstep(zf);
  const c000 = hash3(xi, yi, zi, seed);
  const c100 = hash3(xi + 1, yi, zi, seed);
  const c010 = hash3(xi, yi + 1, zi, seed);
  const c110 = hash3(xi + 1, yi + 1, zi, seed);
  const c001 = hash3(xi, yi, zi + 1, seed);
  const c101 = hash3(xi + 1, yi, zi + 1, seed);
  const c011 = hash3(xi, yi + 1, zi + 1, seed);
  const c111 = hash3(xi + 1, yi + 1, zi + 1, seed);
  const x00 = lerp(c000, c100, u), x10 = lerp(c010, c110, u);
  const x01 = lerp(c001, c101, u), x11 = lerp(c011, c111, u);
  const y0 = lerp(x00, x10, v), y1 = lerp(x01, x11, v);
  return lerp(y0, y1, w) * 2 - 1;
}

// --- Improved Perlin 3D, ~[-1, 1] ----------------------------------------------
const _permCache = new Map();
export function permTable(seed) {
  let p = _permCache.get(seed);
  if (p) return p;
  const rand = mulberry32((seed ^ 0x51ed2703) >>> 0);
  const base = new Uint8Array(256);
  for (let i = 0; i < 256; i++) base[i] = i;
  for (let i = 255; i > 0; i--) {
    const j = (rand() * (i + 1)) | 0;
    const t = base[i]; base[i] = base[j]; base[j] = t;
  }
  p = new Uint8Array(512);
  for (let i = 0; i < 512; i++) p[i] = base[i & 255];
  if (_permCache.size > 64) _permCache.clear();
  _permCache.set(seed, p);
  return p;
}

const _G3 = [
  1, 1, 0, -1, 1, 0, 1, -1, 0, -1, -1, 0,
  1, 0, 1, -1, 0, 1, 1, 0, -1, -1, 0, -1,
  0, 1, 1, 0, -1, 1, 0, 1, -1, 0, -1, -1,
];

export function perlin3(x, y, z, seed) {
  const P = permTable(seed);
  const X = Math.floor(x) & 255, Y = Math.floor(y) & 255, Z = Math.floor(z) & 255;
  x -= Math.floor(x); y -= Math.floor(y); z -= Math.floor(z);
  const u = smootherstep(x), v = smootherstep(y), w = smootherstep(z);
  const A = P[X] + Y, AA = P[A] + Z, AB = P[A + 1] + Z;
  const B = P[X + 1] + Y, BA = P[B] + Z, BB = P[B + 1] + Z;
  const g = (h, a, b, c) => {
    const o = (h % 12) * 3;
    return _G3[o] * a + _G3[o + 1] * b + _G3[o + 2] * c;
  };
  return lerp(
    lerp(lerp(g(P[AA], x, y, z), g(P[BA], x - 1, y, z), u),
         lerp(g(P[AB], x, y - 1, z), g(P[BB], x - 1, y - 1, z), u), v),
    lerp(lerp(g(P[AA + 1], x, y, z - 1), g(P[BA + 1], x - 1, y, z - 1), u),
         lerp(g(P[AB + 1], x, y - 1, z - 1), g(P[BB + 1], x - 1, y - 1, z - 1), u), v),
    w) * 0.964921414; // ~normalize
}

// --- Simplex 3D (Gustavson), ~[-1, 1] -------------------------------------------
const _SIMPLEX_G = [
  [1, 1, 0], [-1, 1, 0], [1, -1, 0], [-1, -1, 0],
  [1, 0, 1], [-1, 0, 1], [1, 0, -1], [-1, 0, -1],
  [0, 1, 1], [0, -1, 1], [0, 1, -1], [0, -1, -1],
];

export function simplex3(x, y, z, seed) {
  const P = permTable(seed ^ 0x2b992dd5);
  const F = 1 / 3, G = 1 / 6;
  const s = (x + y + z) * F;
  const i = Math.floor(x + s), j = Math.floor(y + s), k = Math.floor(z + s);
  const t = (i + j + k) * G;
  const x0 = x - (i - t), y0 = y - (j - t), z0 = z - (k - t);
  let i1, j1, k1, i2, j2, k2;
  if (x0 >= y0) {
    if (y0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
    else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
    else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
  } else {
    if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
    else if (x0 < z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
    else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
  }
  const x1 = x0 - i1 + G, y1 = y0 - j1 + G, z1 = z0 - k1 + G;
  const x2 = x0 - i2 + 2 * G, y2 = y0 - j2 + 2 * G, z2 = z0 - k2 + 2 * G;
  const x3 = x0 - 1 + 3 * G, y3 = y0 - 1 + 3 * G, z3 = z0 - 1 + 3 * G;
  const ii = i & 255, jj = j & 255, kk = k & 255;
  let n = 0, t0 = 0.6 - x0 * x0 - y0 * y0 - z0 * z0;
  if (t0 > 0) { const g = _SIMPLEX_G[P[ii + P[jj + P[kk]]] % 12]; t0 *= t0; n += t0 * t0 * (g[0] * x0 + g[1] * y0 + g[2] * z0); }
  let t1 = 0.6 - x1 * x1 - y1 * y1 - z1 * z1;
  if (t1 > 0) { const g = _SIMPLEX_G[P[ii + i1 + P[jj + j1 + P[kk + k1]]] % 12]; t1 *= t1; n += t1 * t1 * (g[0] * x1 + g[1] * y1 + g[2] * z1); }
  let t2 = 0.6 - x2 * x2 - y2 * y2 - z2 * z2;
  if (t2 > 0) { const g = _SIMPLEX_G[P[ii + i2 + P[jj + j2 + P[kk + k2]]] % 12]; t2 *= t2; n += t2 * t2 * (g[0] * x2 + g[1] * y2 + g[2] * z2); }
  let t3 = 0.6 - x3 * x3 - y3 * y3 - z3 * z3;
  if (t3 > 0) { const g = _SIMPLEX_G[P[ii + 1 + P[jj + 1 + P[kk + 1]]] % 12]; t3 *= t3; n += t3 * t3 * (g[0] * x3 + g[1] * y3 + g[2] * z3); }
  return n * 32;
}

export const BASIS = { value: valueNoise3, perlin: perlin3, simplex: simplex3 };
export function basisNoise(kind, x, y, z, seed) {
  if (kind === 1) return perlin3(x, y, z, seed);
  if (kind === 2) return simplex3(x, y, z, seed);
  return valueNoise3(x, y, z, seed);
}

// --- Fractal stacks ------------------------------------------------------------
// fbm: ~[-1, 1]. ridged/billow: [0, ~1]. mountainMF (ridged multifractal): [0, ~1.2]
export function fbm3(x, y, z, oct, lac, gain, seed, basis) {
  let amp = 0.5, f = 1, sum = 0, norm = 0;
  for (let o = 0; o < oct; o++) {
    sum += basisNoise(basis, x * f, y * f, z * f, seed + o * 101) * amp;
    norm += amp;
    amp *= gain; f *= lac;
  }
  return norm > 0 ? sum / norm : 0;
}

export function ridged3(x, y, z, oct, lac, gain, offset, seed, basis) {
  let amp = 0.5, f = 1, sum = 0, norm = 0;
  for (let o = 0; o < oct; o++) {
    let n = offset - Math.abs(basisNoise(basis, x * f, y * f, z * f, seed + o * 131));
    n *= n;
    sum += n * amp; norm += amp;
    amp *= gain; f *= lac;
  }
  return norm > 0 ? sum / norm : 0; // [0, ~1] with offset~1
}

export function billow3(x, y, z, oct, lac, gain, seed, basis) {
  let amp = 0.5, f = 1, sum = 0, norm = 0;
  for (let o = 0; o < oct; o++) {
    let n = Math.abs(basisNoise(basis, x * f, y * f, z * f, seed + o * 137));
    sum += n * amp; norm += amp;
    amp *= gain; f *= lac;
  }
  return norm > 0 ? sum / norm : 0;
}

// Ridged multifractal ("mountain" noise, Musgrave-style): sharp crests,
// weight cascade makes high areas stay rugged. Returns ~[0, 1.1].
export function mountainMF3(x, y, z, oct, lac, H, offset, gain, seed, basis) {
  const expo = [];
  let f = 1;
  for (let o = 0; o < oct; o++) { expo.push(Math.pow(f, -H)); f *= lac; }
  f = 1;
  let weight = 1, sum = offset * 0.55, prevW = 1;
  for (let o = 0; o < oct; o++) {
    let n = offset - Math.abs(basisNoise(basis, x * f, y * f, z * f, seed + o * 173));
    n *= n;
    n *= prevW;
    prevW = clamp(n * gain, 0, 1);
    sum += n * expo[o] * weight;
    weight *= 0.92;
    f *= lac;
  }
  return sum * 0.62;
}

// --- Voronoi -------------------------------------------------------------------
export function voronoi3(x, y, z, seed, jitter, out) {
  const xi = Math.floor(x), yi = Math.floor(y), zi = Math.floor(z);
  let f1 = 8, f2 = 8;
  for (let dz = -1; dz <= 1; dz++)
    for (let dy = -1; dy <= 1; dy++)
      for (let dx = -1; dx <= 1; dx++) {
        const cx = xi + dx, cy = yi + dy, cz = zi + dz;
        const px = cx + (hash3(cx, cy, cz, seed) - 0.5) * 2 * jitter + 0.5 - 0.5 + 0.0;
        const py = cy + (hash3(cx, cy, cz, seed + 1) - 0.5) * 2 * jitter;
        const pz = cz + (hash3(cx, cy, cz, seed + 2) - 0.5) * 2 * jitter;
        // note: points live in cell space; +0.0 keeps them centered on jitter
        const ddx = px + 0.0 - x + 0.0, ddy = py - y, ddz = pz - z;
        // px already includes cell origin; recompute cleanly:
        const ox = cx + 0.5 + (hash3(cx, cy, cz, seed) - 0.5) * 2 * jitter - x;
        const oy = cy + 0.5 + (hash3(cx, cy, cz, seed + 1) - 0.5) * 2 * jitter - y;
        const oz = cz + 0.5 + (hash3(cx, cy, cz, seed + 2) - 0.5) * 2 * jitter - z;
        void ddx; void ddy; void ddz; void px; void py; void pz;
        const d = Math.sqrt(ox * ox + oy * oy + oz * oz);
        if (d < f1) { f2 = f1; f1 = d; } else if (d < f2) { f2 = d; }
      }
  if (out) { out.f1 = f1; out.f2 = f2; return out; }
  return f1;
}

// --- Domain warp ----------------------------------------------------------------
export function warpOffset(x, y, z, amp, freq, seed, basis, out) {
  const s = seed | 0;
  out.x = basisNoise(basis, x * freq + 5.2, y * freq + 1.3, z * freq + 2.8, s) * amp;
  out.y = basisNoise(basis, x * freq + 8.1, y * freq + 9.2, z * freq + 4.4, s + 7) * amp;
  out.z = basisNoise(basis, x * freq + 3.7, y * freq + 7.9, z * freq + 6.6, s + 13) * amp;
  return out;
}

// --- Shaping ----------------------------------------------------------------------
export function terrace(v, steps, sharpness) {
  if (steps <= 1) return v;
  const s = v * steps;
  const i = Math.floor(s), f = s - i;
  const t = clamp((f - 0.5) * sharpness + 0.5, 0, 1);
  return (i + smootherstep(clamp(t, 0, 1))) / steps;
}

// Strata coordinate: warped vertical bands in [0,1)
export function strataCoord(x, y, z, freq, warpAmp, warpFreq, seed, basis) {
  const w = basisNoise(basis, x * warpFreq, y * warpFreq * 0.35, z * warpFreq, seed + 41);
  const v = (y * freq + w * warpAmp) % 1;
  return v < 0 ? v + 1 : v;
}

// Rock hardness from strata position + noise: 0 (soft shale) .. 1 (hard granite)
export function rockHardness(x, y, z, seed, contrast) {
  const s = strataCoord(x, y, z, 9.0, 0.9, 2.2, seed, 1);
  const band = 0.5 + 0.5 * Math.sin(s * Math.PI * 2 * 3 + 1.2 * Math.sin(s * 12.9));
  const n = 0.5 + 0.5 * fbm3(x * 3.1, y * 3.1, z * 3.1, 3, 2.03, 0.5, seed + 77, 1);
  return clamp(lerp(0.5, band * 0.65 + n * 0.35, contrast), 0.02, 1);
}

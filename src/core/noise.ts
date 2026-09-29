// ---------------------------------------------------------------------------
// Frontier core / noise
// Deterministic, seedable noise generators (2D + 3D) used by generator nodes.
// All value ranges are normalised to roughly [-1, 1] unless stated otherwise.
// ---------------------------------------------------------------------------

export function hashInt(x: number, y: number, z: number, seed: number): number {
  let h = seed | 0;
  h = Math.imul(h ^ (x | 0), 0x27d4eb2d);
  h = Math.imul(h ^ (y | 0), 0x165667b1);
  h = Math.imul(h ^ (z | 0), 0x9e3779b1);
  h ^= h >>> 15;
  h = Math.imul(h, 0x85ebca6b);
  h ^= h >>> 13;
  return (h >>> 0) / 4294967295; // [0,1]
}

const fade = (t: number) => t * t * t * (t * (t * 6 - 15) + 10);
const lerp = (a: number, b: number, t: number) => a + (b - a) * t;

function grad2(hash: number, x: number, y: number): number {
  switch (hash & 7) {
    case 0: return x + y;
    case 1: return x - y;
    case 2: return -x + y;
    case 3: return -x - y;
    case 4: return x;
    case 5: return -x;
    case 6: return y;
    default: return -y;
  }
}

function grad3(hash: number, x: number, y: number, z: number): number {
  const h = hash & 15;
  const u = h < 8 ? x : y;
  const v = h < 4 ? y : h === 12 || h === 14 ? x : z;
  return ((h & 1) === 0 ? u : -u) + ((h & 2) === 0 ? v : -v);
}

// ------------------------------- value noise -------------------------------

export function valueNoise2(x: number, y: number, seed: number): number {
  const xi = Math.floor(x), yi = Math.floor(y);
  const xf = x - xi, yf = y - yi;
  const u = fade(xf), v = fade(yf);
  const a = hashInt(xi, yi, 0, seed);
  const b = hashInt(xi + 1, yi, 0, seed);
  const c = hashInt(xi, yi + 1, 0, seed);
  const d = hashInt(xi + 1, yi + 1, 0, seed);
  return lerp(lerp(a, b, u), lerp(c, d, u), v) * 2 - 1;
}

export function valueNoise3(x: number, y: number, z: number, seed: number): number {
  const xi = Math.floor(x), yi = Math.floor(y), zi = Math.floor(z);
  const xf = x - xi, yf = y - yi, zf = z - zi;
  const u = fade(xf), v = fade(yf), w = fade(zf);
  const n000 = hashInt(xi, yi, zi, seed);
  const n100 = hashInt(xi + 1, yi, zi, seed);
  const n010 = hashInt(xi, yi + 1, zi, seed);
  const n110 = hashInt(xi + 1, yi + 1, zi, seed);
  const n001 = hashInt(xi, yi, zi + 1, seed);
  const n101 = hashInt(xi + 1, yi, zi + 1, seed);
  const n011 = hashInt(xi, yi + 1, zi + 1, seed);
  const n111 = hashInt(xi + 1, yi + 1, zi + 1, seed);
  const x00 = lerp(n000, n100, u), x10 = lerp(n010, n110, u);
  const x01 = lerp(n001, n101, u), x11 = lerp(n011, n111, u);
  return lerp(lerp(x00, x10, v), lerp(x01, x11, v), w) * 2 - 1;
}

// ------------------------------- perlin ------------------------------------

export function perlin2(x: number, y: number, seed: number): number {
  const xi = Math.floor(x), yi = Math.floor(y);
  const xf = x - xi, yf = y - yi;
  const u = fade(xf), v = fade(yf);
  const h = (a: number, b: number) => (hashInt(xi + a, yi + b, 0, seed) * 4294967295) | 0;
  const n00 = grad2(h(0, 0), xf, yf);
  const n10 = grad2(h(1, 0), xf - 1, yf);
  const n01 = grad2(h(0, 1), xf, yf - 1);
  const n11 = grad2(h(1, 1), xf - 1, yf - 1);
  return lerp(lerp(n00, n10, u), lerp(n01, n11, u), v) * 0.7071 * 1.4;
}

export function perlin3(x: number, y: number, z: number, seed: number): number {
  const xi = Math.floor(x), yi = Math.floor(y), zi = Math.floor(z);
  const xf = x - xi, yf = y - yi, zf = z - zi;
  const u = fade(xf), v = fade(yf), w = fade(zf);
  const H = (a: number, b: number, c: number) => (hashInt(xi + a, yi + b, zi + c, seed) * 4294967295) | 0;
  const n000 = grad3(H(0, 0, 0), xf, yf, zf);
  const n100 = grad3(H(1, 0, 0), xf - 1, yf, zf);
  const n010 = grad3(H(0, 1, 0), xf, yf - 1, zf);
  const n110 = grad3(H(1, 1, 0), xf - 1, yf - 1, zf);
  const n001 = grad3(H(0, 0, 1), xf, yf, zf - 1);
  const n101 = grad3(H(1, 0, 1), xf - 1, yf, zf - 1);
  const n011 = grad3(H(0, 1, 1), xf, yf - 1, zf - 1);
  const n111 = grad3(H(1, 1, 1), xf - 1, yf - 1, zf - 1);
  const x00 = lerp(n000, n100, u), x10 = lerp(n010, n110, u);
  const x01 = lerp(n001, n101, u), x11 = lerp(n011, n111, u);
  return lerp(lerp(x00, x10, v), lerp(x01, x11, v), w) * 0.9;
}

// ------------------------------- simplex -----------------------------------

const F2 = 0.5 * (Math.sqrt(3) - 1);
const G2 = (3 - Math.sqrt(3)) / 6;
const F3 = 1 / 3;
const G3 = 1 / 6;

export function simplex2(xin: number, yin: number, seed: number): number {
  const s = (xin + yin) * F2;
  const i = Math.floor(xin + s), j = Math.floor(yin + s);
  const t = (i + j) * G2;
  const x0 = xin - (i - t), y0 = yin - (j - t);
  const i1 = x0 > y0 ? 1 : 0, j1 = x0 > y0 ? 0 : 1;
  const x1 = x0 - i1 + G2, y1 = y0 - j1 + G2;
  const x2 = x0 - 1 + 2 * G2, y2 = y0 - 1 + 2 * G2;
  const h = (a: number, b: number) => (hashInt(i + a, j + b, 0, seed) * 4294967295) | 0;
  let n = 0;
  let t0 = 0.5 - x0 * x0 - y0 * y0;
  if (t0 > 0) { t0 *= t0; n += t0 * t0 * grad2(h(0, 0), x0, y0); }
  let t1 = 0.5 - x1 * x1 - y1 * y1;
  if (t1 > 0) { t1 *= t1; n += t1 * t1 * grad2(h(i1, j1), x1, y1); }
  let t2 = 0.5 - x2 * x2 - y2 * y2;
  if (t2 > 0) { t2 *= t2; n += t2 * t2 * grad2(h(1, 1), x2, y2); }
  return 70 * n;
}

export function simplex3(xin: number, yin: number, zin: number, seed: number): number {
  const s = (xin + yin + zin) * F3;
  const i = Math.floor(xin + s), j = Math.floor(yin + s), k = Math.floor(zin + s);
  const t = (i + j + k) * G3;
  const x0 = xin - (i - t), y0 = yin - (j - t), z0 = zin - (k - t);
  let i1 = 0, j1 = 0, k1 = 0, i2 = 0, j2 = 0, k2 = 0;
  if (x0 >= y0) {
    if (y0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
    else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
    else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
  } else {
    if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
    else if (x0 < z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
    else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
  }
  const x1 = x0 - i1 + G3, y1 = y0 - j1 + G3, z1 = z0 - k1 + G3;
  const x2 = x0 - i2 + 2 * G3, y2 = y0 - j2 + 2 * G3, z2 = z0 - k2 + 2 * G3;
  const x3 = x0 - 1 + 3 * G3, y3 = y0 - 1 + 3 * G3, z3 = z0 - 1 + 3 * G3;
  const H = (a: number, b: number, c: number) => (hashInt(i + a, j + b, k + c, seed) * 4294967295) | 0;
  let n = 0;
  let t0 = 0.6 - x0 * x0 - y0 * y0 - z0 * z0;
  if (t0 > 0) { t0 *= t0; n += t0 * t0 * grad3(H(0, 0, 0), x0, y0, z0); }
  let t1 = 0.6 - x1 * x1 - y1 * y1 - z1 * z1;
  if (t1 > 0) { t1 *= t1; n += t1 * t1 * grad3(H(i1, j1, k1), x1, y1, z1); }
  let t2 = 0.6 - x2 * x2 - y2 * y2 - z2 * z2;
  if (t2 > 0) { t2 *= t2; n += t2 * t2 * grad3(H(i2, j2, k2), x2, y2, z2); }
  let t3 = 0.6 - x3 * x3 - y3 * y3 - z3 * z3;
  if (t3 > 0) { t3 *= t3; n += t3 * t3 * grad3(H(1, 1, 1), x3, y3, z3); }
  return 32 * n;
}

// ------------------------------- white -------------------------------------

export function whiteNoise2(ix: number, iy: number, seed: number): number {
  return hashInt(ix, iy, 7, seed) * 2 - 1;
}

// ------------------------------- fractal -----------------------------------

export interface FractalOpts {
  octaves: number;
  lacunarity: number;
  gain: number;
  frequency: number;
  seed: number;
}

export function fbm2(x: number, y: number, o: FractalOpts): number {
  let amp = 1, freq = o.frequency, sum = 0, norm = 0;
  for (let i = 0; i < o.octaves; i++) {
    sum += amp * simplex2(x * freq, y * freq, o.seed + i * 1013);
    norm += amp;
    amp *= o.gain;
    freq *= o.lacunarity;
  }
  return sum / norm;
}

/**
 * Ridged multifractal (Musgrave): sharp creases -> mountains / badlands.
 * Returns [0,1] with 1 = ridge crest.
 */
export function ridgedMultifractal2(x: number, y: number, o: FractalOpts, offset = 1.0, sharp = 2.0): number {
  let amp = 0.5, freq = o.frequency, sum = 0, weight = 1;
  for (let i = 0; i < o.octaves; i++) {
    let n = 1 - Math.abs(simplex2(x * freq, y * freq, o.seed + i * 733));
    n = Math.pow(n, sharp);
    n *= weight;
    weight = Math.min(1, Math.max(0, n * 2));
    sum += n * amp;
    amp *= o.gain;
    freq *= o.lacunarity;
    void offset;
  }
  return Math.min(1, sum * 1.6);
}

/**
 * Badlands: ridged multifractal terraced by a strata warp — mesa/gully look.
 */
export function badlands2(x: number, y: number, o: FractalOpts, terrace = 0.35): number {
  const warp = 0.35 * simplex2(x * o.frequency * 2.7, y * o.frequency * 2.7, o.seed + 991);
  let v = ridgedMultifractal2(x + warp, y - warp, o, 1, 2.4);
  const steps = Math.max(1, Math.round(1 / Math.max(0.02, terrace)));
  const t = Math.floor(v * steps) / steps;
  const f = v * steps - Math.floor(v * steps);
  v = t + terrace * (f * f * (3 - 2 * f)) * (1 / steps) * steps * 0.5 + (1 - terrace) * f / steps;
  return Math.min(1, v);
}

// ------------------------------- voronoi -----------------------------------

export interface VoronoiResult { f1: number; f2: number; id: number }

export function voronoi2(x: number, y: number, seed: number, jitter = 0.9): VoronoiResult {
  const xi = Math.floor(x), yi = Math.floor(y);
  let f1 = Infinity, f2 = Infinity, id = 0;
  for (let dy = -1; dy <= 1; dy++) {
    for (let dx = -1; dx <= 1; dx++) {
      const cx = xi + dx, cy = yi + dy;
      const px = cx + 0.5 + (hashInt(cx, cy, 1, seed) - 0.5) * jitter;
      const py = cy + 0.5 + (hashInt(cx, cy, 2, seed) - 0.5) * jitter;
      const d = Math.hypot(x - px, y - py);
      if (d < f1) { f2 = f1; f1 = d; id = hashInt(cx, cy, 3, seed) * 1000; }
      else if (d < f2) { f2 = d; }
    }
  }
  return { f1, f2, id };
}

/** 3D worley — used for cave tunnels / cellular carve. */
export function voronoi3(x: number, y: number, z: number, seed: number, jitter = 0.9): VoronoiResult {
  const xi = Math.floor(x), yi = Math.floor(y), zi = Math.floor(z);
  let f1 = Infinity, f2 = Infinity, id = 0;
  for (let dz = -1; dz <= 1; dz++) for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) {
    const cx = xi + dx, cy = yi + dy, cz = zi + dz;
    const px = cx + 0.5 + (hashInt(cx, cy, cz, seed) - 0.5) * jitter;
    const py = cy + 0.5 + (hashInt(cx, cy, cz, seed + 11) - 0.5) * jitter;
    const pz = cz + 0.5 + (hashInt(cx, cy, cz, seed + 23) - 0.5) * jitter;
    const d = Math.sqrt((x - px) ** 2 + (y - py) ** 2 + (z - pz) ** 2);
    if (d < f1) { f2 = f1; f1 = d; id = hashInt(cx, cy, cz, seed + 5) * 1000; }
    else if (d < f2) { f2 = d; }
  }
  return { f1, f2, id };
}

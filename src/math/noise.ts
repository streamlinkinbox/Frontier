// Deterministic PRNG and fast 2D/3D gradient noise + FBM utilities

export function mulberry32(seed: number): () => number {
  let a = seed >>> 0;
  return function () {
    a |= 0;
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const PERM = new Uint8Array(512);
const GRAD3: ReadonlyArray<[number, number, number]> = [
  [1, 1, 0], [-1, 1, 0], [1, -1, 0], [-1, -1, 0],
  [1, 0, 1], [-1, 0, 1], [1, 0, -1], [-1, 0, -1],
  [0, 1, 1], [0, -1, 1], [0, 1, -1], [0, -1, -1],
];

(function initNoiseTable() {
  const rng = mulberry32(0x5f3759df);
  const p = new Uint8Array(256);
  for (let i = 0; i < 256; i++) p[i] = i;
  for (let i = 255; i > 0; i--) {
    const j = Math.floor(rng() * (i + 1));
    const tmp = p[i];
    p[i] = p[j];
    p[j] = tmp;
  }
  for (let i = 0; i < 512; i++) {
    PERM[i] = p[i & 255];
  }
})();

function fade(t: number): number {
  return t * t * t * (t * (t * 6 - 15) + 10);
}

export function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * t;
}

export function clamp(v: number, min: number, max: number): number {
  return v < min ? min : v > max ? max : v;
}

export function smoothstep(edge0: number, edge1: number, x: number): number {
  const t = clamp((x - edge0) / (edge1 - edge0), 0, 1);
  return t * t * (3 - 2 * t);
}

export function noise2D(x: number, y: number): number {
  const X = Math.floor(x) & 255;
  const Y = Math.floor(y) & 255;
  const xf = x - Math.floor(x);
  const yf = y - Math.floor(y);
  const u = fade(xf);
  const v = fade(yf);

  const aa = PERM[PERM[X] + Y] % 12;
  const ab = PERM[PERM[X] + Y + 1] % 12;
  const ba = PERM[PERM[X + 1] + Y] % 12;
  const bb = PERM[PERM[X + 1] + Y + 1] % 12;

  const gAA = GRAD3[aa];
  const gBA = GRAD3[ba];
  const gAB = GRAD3[ab];
  const gBB = GRAD3[bb];

  const x1 = lerp(gAA[0] * xf + gAA[1] * yf, gBA[0] * (xf - 1) + gBA[1] * yf, u);
  const x2 = lerp(
    gAB[0] * xf + gAB[1] * (yf - 1),
    gBB[0] * (xf - 1) + gBB[1] * (yf - 1),
    u
  );
  return lerp(x1, x2, v);
}

export function noise3D(x: number, y: number, z: number): number {
  const X = Math.floor(x) & 255;
  const Y = Math.floor(y) & 255;
  const Z = Math.floor(z) & 255;
  const xf = x - Math.floor(x);
  const yf = y - Math.floor(y);
  const zf = z - Math.floor(z);

  const u = fade(xf);
  const v = fade(yf);
  const w = fade(zf);

  const A = PERM[X] + Y;
  const AA = PERM[A] + Z;
  const AB = PERM[A + 1] + Z;
  const B = PERM[X + 1] + Y;
  const BA = PERM[B] + Z;
  const BB = PERM[B + 1] + Z;

  const dot3 = (gi: number, dx: number, dy: number, dz: number) => {
    const g = GRAD3[gi % 12];
    return g[0] * dx + g[1] * dy + g[2] * dz;
  };

  return lerp(
    lerp(
      lerp(dot3(PERM[AA], xf, yf, zf), dot3(PERM[BA], xf - 1, yf, zf), u),
      lerp(dot3(PERM[AB], xf, yf - 1, zf), dot3(PERM[BB], xf - 1, yf - 1, zf), u),
      v
    ),
    lerp(
      lerp(dot3(PERM[AA + 1], xf, yf, zf - 1), dot3(PERM[BA + 1], xf - 1, yf, zf - 1), u),
      lerp(
        dot3(PERM[AB + 1], xf, yf - 1, zf - 1),
        dot3(PERM[BB + 1], xf - 1, yf - 1, zf - 1),
        u
      ),
      v
    ),
    w
  );
}

export function fbm2D(
  x: number,
  y: number,
  octaves = 4,
  lacunarity = 2.03,
  gain = 0.5
): number {
  let sum = 0;
  let amp = 0.5;
  let freq = 1;
  let norm = 0;
  for (let i = 0; i < octaves; i++) {
    sum += amp * noise2D(x * freq, y * freq);
    norm += amp;
    amp *= gain;
    freq *= lacunarity;
  }
  return sum / norm;
}

export function ridge2D(x: number, y: number, octaves = 3): number {
  let sum = 0;
  let amp = 0.5;
  let freq = 1;
  let norm = 0;
  for (let i = 0; i < octaves; i++) {
    const n = 1 - Math.abs(noise2D(x * freq, y * freq));
    sum += amp * n * n;
    norm += amp;
    amp *= 0.5;
    freq *= 2.07;
  }
  return sum / norm;
}

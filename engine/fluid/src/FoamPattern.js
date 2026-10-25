// FoamPattern – procedural, tileable foam micro-structure (no image assets), generated once on the CPU.
//
// The foam *distribution* comes from the simulated foam particles; this texture only supplies the
// sub-particle structure that each particle carries (Lagrangian texture advection, see FOAM_SPLAT).
// Every channel is histogram-equalised to a uniform distribution on [0, 1] (mean 0.5), so that
//   coverage = P(pattern < density)  equals the density on average – which keeps foam coverage
// predictable, lets the variance-preserving blend use a known mean, and makes distant foam
// (where the structure is sub-pixel) converge to the right average brightness.
//
//   R  marbling  – domain-warped fBm + ridged noise: ragged patch outlines and holes (~0.3–1 m)
//   G  raft lace – cell walls of two domain-warped Voronoi diagrams (~12 and ~30 cm cells):
//                  low values on the walls, so decaying foam opens up into a lace network
//   B  bubbles   – small cells (~3 cm) with bright rims: surface detail for shading
//   A  clumps    – soft low-frequency noise: varies foam thickness inside a patch

const TAU = Math.PI * 2;

function hash2i(x, y, seed) {
  let h = (Math.imul(x, 0x27d4eb2d) ^ Math.imul(y, 0x165667b1) ^ Math.imul(seed, 0x9e3779b1)) >>> 0;
  h = Math.imul(h ^ (h >>> 15), 0x85ebca6b) >>> 0;
  h = Math.imul(h ^ (h >>> 13), 0xc2b2ae35) >>> 0;
  return (h ^ (h >>> 16)) >>> 0;
}
const u01 = (h) => h / 4294967296;
const mod = (a, n) => ((a % n) + n) % n;

// 256 unit gradients (a table is much faster than cos/sin per lattice corner)
const GX = new Float32Array(256), GY = new Float32Array(256);
for (let i = 0; i < 256; i++) { GX[i] = Math.cos((i / 256) * TAU); GY[i] = Math.sin((i / 256) * TAU); }

/** Periodic gradient noise layer (period P cells) with a precomputed lattice: (x, y) -> ~[-1, 1]. */
function makeNoise(P, seed) {
  const gi = new Uint8Array(P * P);
  for (let y = 0; y < P; y++) for (let x = 0; x < P; x++) gi[y * P + x] = hash2i(x, y, seed) & 255;
  return (x, y) => {
    const fx0 = Math.floor(x), fy0 = Math.floor(y), fx = x - fx0, fy = y - fy0;
    const ix = mod(fx0, P), iy = mod(fy0, P), jx = ix + 1 === P ? 0 : ix + 1, jy = iy + 1 === P ? 0 : iy + 1;
    const ux = fx * fx * fx * (fx * (fx * 6 - 15) + 10), uy = fy * fy * fy * (fy * (fy * 6 - 15) + 10);
    const g00 = gi[iy * P + ix], g10 = gi[iy * P + jx], g01 = gi[jy * P + ix], g11 = gi[jy * P + jx];
    const a = GX[g00] * fx + GY[g00] * fy, b = GX[g10] * (fx - 1) + GY[g10] * fy;
    const c = GX[g01] * fx + GY[g01] * (fy - 1), d = GX[g11] * (fx - 1) + GY[g11] * (fy - 1);
    return (a + (b - a) * ux + (c - a) * uy + (a - b - c + d) * ux * uy) * 1.4;
  };
}
/** Periodic fBm (or ridged) built from precomputed layers: (x, y) -> value. */
function makeFbm(P, oct, seed, ridged = false) {
  const layers = [];
  for (let i = 0; i < oct; i++) layers.push(makeNoise(P << i, seed + i * 101));
  return (x, y) => {
    let s = 0, a = 0.5, n = 0, f = 1;
    for (let i = 0; i < oct; i++) {
      const v = layers[i](x * f, y * f);
      s += a * (ridged ? Math.abs(v) : v);
      n += a; f *= 2; a *= ridged ? 0.55 : 0.5;
    }
    return s / n;
  };
}
/** Periodic Voronoi with precomputed feature points: (x, y, out) -> out = [F1, F2] in cell units. */
function makeVoronoi(P, seed, jitter = 0.9) {
  const fp = new Float32Array(P * P * 2);
  for (let y = 0; y < P; y++) for (let x = 0; x < P; x++) {
    const h = hash2i(x, y, seed);
    fp[(y * P + x) * 2] = 0.5 + (u01(h) - 0.5) * jitter;
    fp[(y * P + x) * 2 + 1] = 0.5 + (u01(hash2i(h, 7, seed)) - 0.5) * jitter;
  }
  return (x, y, out) => {
    const ix = Math.floor(x), iy = Math.floor(y), fx = x - ix, fy = y - iy;
    let d1 = 9, d2 = 9;
    for (let j = -1; j <= 1; j++) {
      const cy = mod(iy + j, P);
      for (let i = -1; i <= 1; i++) {
        const k = (cy * P + mod(ix + i, P)) * 2;
        const dx = i + fp[k] - fx, dy = j + fp[k + 1] - fy;
        const d = dx * dx + dy * dy;
        if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) d2 = d;
      }
    }
    out[0] = Math.sqrt(d1); out[1] = Math.sqrt(d2);
    return out;
  };
}

/** Map values to their rank (uniform on [0, 1]) – a 4096-bin histogram equalisation. */
function equalise(src) {
  let lo = Infinity, hi = -Infinity;
  for (const v of src) { if (v < lo) lo = v; if (v > hi) hi = v; }
  const B = 4096, hist = new Float64Array(B + 1), sc = (B - 1) / Math.max(hi - lo, 1e-9);
  for (const v of src) hist[Math.floor((v - lo) * sc)]++;
  const cdf = new Float64Array(B + 1);
  let acc = 0;
  for (let b = 0; b < B; b++) { cdf[b] = (acc + hist[b] * 0.5) / src.length; acc += hist[b]; }
  const out = new Float32Array(src.length);
  for (let i = 0; i < src.length; i++) out[i] = cdf[Math.floor((src[i] - lo) * sc)];
  return out;
}

/**
 * Generate the RGBA8 pattern and its mip chain.
 * @param {number} size  power of two (default 512)
 * @returns {{size: number, levels: Uint8Array[]}}
 */
export function generateFoamPattern(size = 512, seed = 1337) {
  const n = size * size;
  const R = new Float32Array(n), G = new Float32Array(n), Bc = new Float32Array(n), A = new Float32Array(n);
  const warpX = makeFbm(3, 3, seed), warpY = makeFbm(3, 3, seed + 17);
  const ridgeN = makeFbm(4, 4, seed + 31, true), blobN = makeFbm(3, 4, seed + 47);
  const fineWarp = makeFbm(12, 2, seed + 53), thickN = makeFbm(6, 3, seed + 89), wallN = makeFbm(32, 2, seed + 83);
  const vor1 = makeVoronoi(9, seed + 61), vor2 = makeVoronoi(23, seed + 73), vorB = makeVoronoi(96, seed + 97, 1.0);
  const bubN = makeNoise(40, seed + 109), clumpN = makeFbm(5, 3, seed + 127);
  const c1 = [0, 0], c2 = [0, 0], cb = [0, 0];
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const u = x / size, v = y / size, k = y * size + x;
      // domain warp shared by the large-scale fields (organic, non-grid-aligned shapes)
      const wx = warpX(u * 3, v * 3), wy = warpY(u * 3 + 5.2, v * 3 + 1.3);
      const qu = u + wx * 0.07, qv = v + wy * 0.07;
      // R: marbling – ragged blobs with ridged veins
      R[k] = blobN(qu * 3, qv * 3) * 0.65 + ridgeN(qu * 4, qv * 4) * 0.55;
      // G: bubble-raft lace – distance to the nearest cell wall at two scales. The walls wiggle
      // (fine warp), and their thickness varies: bubble clumps in places, thin broken filaments
      // elsewhere – a uniform Voronoi web would look like cracked mud.
      const fw = fineWarp(u * 12, v * 12);
      const lu = u + wx * 0.06 + fw * 0.012, lv = v + wy * 0.06 - fw * 0.012;
      vor1(lu * 9, lv * 9, c1); vor2(lu * 23 + 0.37, lv * 23 + 0.71, c2);
      G[k] = Math.min(c1[1] - c1[0], (c2[1] - c2[0]) * 1.5 + 0.07) + thickN(u * 6, v * 6) * 0.2 + wallN(u * 32, v * 32) * 0.07;
      // B: fine bubbles – bright cell rims
      vorB(u * 96, v * 96, cb);
      Bc[k] = -(cb[1] - cb[0]) + bubN(u * 40, v * 40) * 0.12;
      // A: clumps
      A[k] = clumpN(u * 5, v * 5);
    }
  }
  const ch = [equalise(R), equalise(G), equalise(Bc), equalise(A)];
  const base = new Uint8Array(n * 4);
  for (let k = 0; k < n; k++) for (let c = 0; c < 4; c++) base[k * 4 + c] = Math.round(ch[c][k] * 255);
  // mip chain (box filter; the pattern tiles, so wrap-around is not needed for 2x2 boxes)
  const levels = [base];
  let s = size, prev = base;
  while (s > 1) {
    const h = s >> 1, next = new Uint8Array(h * h * 4);
    for (let y = 0; y < h; y++) for (let x = 0; x < h; x++) for (let c = 0; c < 4; c++) {
      const i0 = ((2 * y) * s + 2 * x) * 4 + c;
      next[(y * h + x) * 4 + c] = (prev[i0] + prev[i0 + 4] + prev[i0 + s * 4] + prev[i0 + s * 4 + 4] + 2) >> 2;
    }
    levels.push(next);
    prev = next; s = h;
  }
  return { size, levels };
}

/** Upload the pattern as a mipmapped rgba8unorm texture. */
export function createFoamPatternTexture(device, size = 512) {
  const { levels } = generateFoamPattern(size);
  const tex = device.createTexture({
    label: 'fluid.foamPattern', size: [size, size], format: 'rgba8unorm', mipLevelCount: levels.length,
    usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
  });
  let s = size;
  levels.forEach((data, mip) => {
    device.queue.writeTexture({ texture: tex, mipLevel: mip }, data, { bytesPerRow: s * 4 }, [s, s]);
    s = Math.max(1, s >> 1);
  });
  return tex;
}

// Deterministic 3D simplex noise (Gustavson / Perlin), seeded.
// Used for procedural cave generation (CPU side) and procedural anatomy variation.

export function mulberry32(seed) {
  let a = seed >>> 0;
  return function () {
    a |= 0; a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const GRAD3 = new Float32Array([
  1, 1, 0, -1, 1, 0, 1, -1, 0, -1, -1, 0,
  1, 0, 1, -1, 0, 1, 1, 0, -1, -1, 0, -1,
  0, 1, 1, 0, -1, 1, 0, 1, -1, 0, -1, -1,
]);

export class Simplex3 {
  constructor(seed = 1337) {
    const rnd = mulberry32(seed);
    const p = new Uint8Array(256);
    for (let i = 0; i < 256; i++) p[i] = i;
    for (let i = 255; i > 0; i--) {
      const j = Math.floor(rnd() * (i + 1));
      const t = p[i]; p[i] = p[j]; p[j] = t;
    }
    this.perm = new Uint8Array(512);
    this.permMod12 = new Uint8Array(512);
    for (let i = 0; i < 512; i++) {
      this.perm[i] = p[i & 255];
      this.permMod12[i] = this.perm[i] % 12;
    }
  }

  noise(xin, yin, zin) {
    const perm = this.perm, pm = this.permMod12;
    const F3 = 1 / 3, G3 = 1 / 6;
    const s = (xin + yin + zin) * F3;
    const i = Math.floor(xin + s), j = Math.floor(yin + s), k = Math.floor(zin + s);
    const t = (i + j + k) * G3;
    const x0 = xin - (i - t), y0 = yin - (j - t), z0 = zin - (k - t);
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
    const x1 = x0 - i1 + G3, y1 = y0 - j1 + G3, z1 = z0 - k1 + G3;
    const x2 = x0 - i2 + 2 * G3, y2 = y0 - j2 + 2 * G3, z2 = z0 - k2 + 2 * G3;
    const x3 = x0 - 1 + 0.5, y3 = y0 - 1 + 0.5, z3 = z0 - 1 + 0.5;
    const ii = i & 255, jj = j & 255, kk = k & 255;
    let n = 0, tt, g;
    tt = 0.6 - x0 * x0 - y0 * y0 - z0 * z0;
    if (tt > 0) { g = pm[ii + perm[jj + perm[kk]]] * 3; tt *= tt; n += tt * tt * (GRAD3[g] * x0 + GRAD3[g + 1] * y0 + GRAD3[g + 2] * z0); }
    tt = 0.6 - x1 * x1 - y1 * y1 - z1 * z1;
    if (tt > 0) { g = pm[ii + i1 + perm[jj + j1 + perm[kk + k1]]] * 3; tt *= tt; n += tt * tt * (GRAD3[g] * x1 + GRAD3[g + 1] * y1 + GRAD3[g + 2] * z1); }
    tt = 0.6 - x2 * x2 - y2 * y2 - z2 * z2;
    if (tt > 0) { g = pm[ii + i2 + perm[jj + j2 + perm[kk + k2]]] * 3; tt *= tt; n += tt * tt * (GRAD3[g] * x2 + GRAD3[g + 1] * y2 + GRAD3[g + 2] * z2); }
    tt = 0.6 - x3 * x3 - y3 * y3 - z3 * z3;
    if (tt > 0) { g = pm[ii + 1 + perm[jj + 1 + perm[kk + 1]]] * 3; tt *= tt; n += tt * tt * (GRAD3[g] * x3 + GRAD3[g + 1] * y3 + GRAD3[g + 2] * z3); }
    return 32 * n; // ~[-1, 1]
  }

  fbm(x, y, z, oct = 4, lac = 2.03, gain = 0.5) {
    let a = 1, f = 1, s = 0, n = 0;
    for (let o = 0; o < oct; o++) {
      s += a * this.noise(x * f + o * 17.3, y * f - o * 9.1, z * f + o * 4.7);
      n += a; a *= gain; f *= lac;
    }
    return s / n;
  }

  ridged(x, y, z, oct = 4, lac = 2.1, gain = 0.5) {
    let a = 1, f = 1, s = 0, n = 0;
    for (let o = 0; o < oct; o++) {
      const v = 1 - Math.abs(this.noise(x * f + o * 31.7, y * f + o * 3.3, z * f - o * 12.9));
      s += a * v * v; n += a; a *= gain; f *= lac;
    }
    return s / n;
  }
}

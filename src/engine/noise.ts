// Deterministic 2D & 3D Noise Library with Nyquist Wavelength Clamping
// Supports Simplex, Perlin, Value, Ridged Multifractal, Hybrid Multifractal, Cellular (Voronoi), and White Noise.

export class SeededNoise {
  private perm: Uint8Array;
  private permMod12: Uint8Array;

  private static readonly GRAD3: ReadonlyArray<[number, number, number]> = [
    [1, 1, 0], [-1, 1, 0], [1, -1, 0], [-1, -1, 0],
    [1, 0, 1], [-1, 0, 1], [1, 0, -1], [-1, 0, -1],
    [0, 1, 1], [0, -1, 1], [0, 1, -1], [0, -1, -1],
  ];

  constructor(seed = 1337) {
    this.perm = new Uint8Array(512);
    this.permMod12 = new Uint8Array(512);
    const p = new Uint8Array(256);
    for (let i = 0; i < 256; i++) p[i] = i;

    let s = (seed | 0) || 1;
    for (let i = 255; i > 0; i--) {
      s = (s * 1664525 + 1013904223) | 0;
      const j = (s >>> 0) % (i + 1);
      const tmp = p[i];
      p[i] = p[j];
      p[j] = tmp;
    }

    for (let i = 0; i < 512; i++) {
      this.perm[i] = p[i & 255];
      this.permMod12[i] = this.perm[i] % 12;
    }
  }

  private fade(t: number): number {
    return t * t * t * (t * (t * 6 - 15) + 10);
  }

  private lerp(a: number, b: number, t: number): number {
    return a + t * (b - a);
  }

  // 2D Simplex Noise in [-1, 1]
  public simplex2D(xin: number, yin: number): number {
    const F2 = 0.5 * (Math.sqrt(3.0) - 1.0);
    const G2 = (3.0 - Math.sqrt(3.0)) / 6.0;
    let n0 = 0, n1 = 0, n2 = 0;

    const s = (xin + yin) * F2;
    const i = Math.floor(xin + s);
    const j = Math.floor(yin + s);
    const t = (i + j) * G2;
    const X0 = i - t;
    const Y0 = j - t;
    const x0 = xin - X0;
    const y0 = yin - Y0;

    let i1: number, j1: number;
    if (x0 > y0) {
      i1 = 1; j1 = 0;
    } else {
      i1 = 0; j1 = 1;
    }

    const x1 = x0 - i1 + G2;
    const y1 = y0 - j1 + G2;
    const x2 = x0 - 1.0 + 2.0 * G2;
    const y2 = y0 - 1.0 + 2.0 * G2;

    const ii = i & 255;
    const jj = j & 255;
    const gi0 = this.permMod12[ii + this.perm[jj]];
    const gi1 = this.permMod12[ii + i1 + this.perm[jj + j1]];
    const gi2 = this.permMod12[ii + 1 + this.perm[jj + 1]];

    let t0 = 0.5 - x0 * x0 - y0 * y0;
    if (t0 >= 0) {
      t0 *= t0;
      const g = SeededNoise.GRAD3[gi0];
      n0 = t0 * t0 * (g[0] * x0 + g[1] * y0);
    }

    let t1 = 0.5 - x1 * x1 - y1 * y1;
    if (t1 >= 0) {
      t1 *= t1;
      const g = SeededNoise.GRAD3[gi1];
      n1 = t1 * t1 * (g[0] * x1 + g[1] * y1);
    }

    let t2 = 0.5 - x2 * x2 - y2 * y2;
    if (t2 >= 0) {
      t2 *= t2;
      const g = SeededNoise.GRAD3[gi2];
      n2 = t2 * t2 * (g[0] * x2 + g[1] * y2);
    }

    return 70.0 * (n0 + n1 + n2);
  }

  // 3D Simplex Noise in [-1, 1] — used for 3D SDF caves, overhangs, and volumetric strata
  public simplex3D(xin: number, yin: number, zin: number): number {
    const F3 = 1.0 / 3.0;
    const G3 = 1.0 / 6.0;
    let n0 = 0, n1 = 0, n2 = 0, n3 = 0;

    const s = (xin + yin + zin) * F3;
    const i = Math.floor(xin + s);
    const j = Math.floor(yin + s);
    const k = Math.floor(zin + s);
    const t = (i + j + k) * G3;
    const X0 = i - t;
    const Y0 = j - t;
    const Z0 = k - t;
    const x0 = xin - X0;
    const y0 = yin - Y0;
    const z0 = zin - Z0;

    let i1: number, j1: number, k1: number;
    let i2: number, j2: number, k2: number;

    if (x0 >= y0) {
      if (y0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
      else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
      else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
    } else {
      if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
      else if (x0 < z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
      else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
    }

    const x1 = x0 - i1 + G3;
    const y1 = y0 - j1 + G3;
    const z1 = z0 - k1 + G3;
    const x2 = x0 - i2 + 2.0 * G3;
    const y2 = y0 - j2 + 2.0 * G3;
    const z2 = z0 - k2 + 2.0 * G3;
    const x3 = x0 - 1.0 + 3.0 * G3;
    const y3 = y0 - 1.0 + 3.0 * G3;
    const z3 = z0 - 1.0 + 3.0 * G3;

    const ii = i & 255;
    const jj = j & 255;
    const kk = k & 255;
    const gi0 = this.permMod12[ii + this.perm[jj + this.perm[kk]]];
    const gi1 = this.permMod12[ii + i1 + this.perm[jj + j1 + this.perm[kk + k1]]];
    const gi2 = this.permMod12[ii + i2 + this.perm[jj + j2 + this.perm[kk + k2]]];
    const gi3 = this.permMod12[ii + 1 + this.perm[jj + 1 + this.perm[kk + 1]]];

    let t0 = 0.6 - x0 * x0 - y0 * y0 - z0 * z0;
    if (t0 >= 0) {
      t0 *= t0;
      const g = SeededNoise.GRAD3[gi0];
      n0 = t0 * t0 * (g[0] * x0 + g[1] * y0 + g[2] * z0);
    }
    let t1 = 0.6 - x1 * x1 - y1 * y1 - z1 * z1;
    if (t1 >= 0) {
      t1 *= t1;
      const g = SeededNoise.GRAD3[gi1];
      n1 = t1 * t1 * (g[0] * x1 + g[1] * y1 + g[2] * z1);
    }
    let t2 = 0.6 - x2 * x2 - y2 * y2 - z2 * z2;
    if (t2 >= 0) {
      t2 *= t2;
      const g = SeededNoise.GRAD3[gi2];
      n2 = t2 * t2 * (g[0] * x2 + g[1] * y2 + g[2] * z2);
    }
    let t3 = 0.6 - x3 * x3 - y3 * y3 - z3 * z3;
    if (t3 >= 0) {
      t3 *= t3;
      const g = SeededNoise.GRAD3[gi3];
      n3 = t3 * t3 * (g[0] * x3 + g[1] * y3 + g[2] * z3);
    }

    return 32.0 * (n0 + n1 + n2 + n3);
  }

  // 2D Classic Perlin Noise in [-1, 1]
  public perlin2D(x: number, y: number): number {
    const X = Math.floor(x) & 255;
    const Y = Math.floor(y) & 255;
    const xf = x - Math.floor(x);
    const yf = y - Math.floor(y);
    const u = this.fade(xf);
    const v = this.fade(yf);

    const aa = this.permMod12[X + this.perm[Y]];
    const ab = this.permMod12[X + this.perm[Y + 1]];
    const ba = this.permMod12[X + 1 + this.perm[Y]];
    const bb = this.permMod12[X + 1 + this.perm[Y + 1]];

    const gAA = SeededNoise.GRAD3[aa];
    const gBA = SeededNoise.GRAD3[ba];
    const gAB = SeededNoise.GRAD3[ab];
    const gBB = SeededNoise.GRAD3[bb];

    const x1 = this.lerp(gAA[0] * xf + gAA[1] * yf, gBA[0] * (xf - 1) + gBA[1] * yf, u);
    const x2 = this.lerp(gAB[0] * xf + gAB[1] * (yf - 1), gBB[0] * (xf - 1) + gBB[1] * (yf - 1), u);

    return Math.max(-1, Math.min(1, this.lerp(x1, x2, v) * 1.4142));
  }

  // 2D Value Noise in [-1, 1]
  public value2D(x: number, y: number): number {
    const X = Math.floor(x) & 255;
    const Y = Math.floor(y) & 255;
    const xf = x - Math.floor(x);
    const yf = y - Math.floor(y);
    const u = this.fade(xf);
    const v = this.fade(yf);

    const v00 = this.perm[X + this.perm[Y]] / 127.5 - 1.0;
    const v10 = this.perm[X + 1 + this.perm[Y]] / 127.5 - 1.0;
    const v01 = this.perm[X + this.perm[Y + 1]] / 127.5 - 1.0;
    const v11 = this.perm[X + 1 + this.perm[Y + 1]] / 127.5 - 1.0;

    return this.lerp(this.lerp(v00, v10, u), this.lerp(v01, v11, u), v);
  }

  // 2D Cellular (Voronoi) returning { f1, f2, edge: f2 - f1 } in [0, 1]
  public cellular2D(x: number, y: number, jitter = 0.85): { f1: number; f2: number; edge: number } {
    const xi = Math.floor(x);
    const yi = Math.floor(y);
    let d1 = 999.0;
    let d2 = 999.0;

    for (let dy = -1; dy <= 1; dy++) {
      for (let dx = -1; dx <= 1; dx++) {
        const cx = xi + dx;
        const cy = yi + dy;
        const h1 = this.perm[(cx & 255) + this.perm[cy & 255]] / 255.0;
        const h2 = this.perm[((cx * 17) & 255) + this.perm[(cy * 31) & 255]] / 255.0;
        const px = cx + 0.5 + (h1 - 0.5) * jitter;
        const py = cy + 0.5 + (h2 - 0.5) * jitter;
        const distSq = (x - px) * (x - px) + (y - py) * (y - py);
        if (distSq < d1) {
          d2 = d1;
          d1 = distSq;
        } else if (distSq < d2) {
          d2 = distSq;
        }
      }
    }
    const f1 = Math.min(1.0, Math.sqrt(d1));
    const f2 = Math.min(1.0, Math.sqrt(d2));
    return { f1, f2, edge: Math.min(1.0, f2 - f1) };
  }

  // White noise hash in [0, 1]
  public white2D(x: number, y: number): number {
    const n = Math.sin(x * 12.9898 + y * 78.233 + this.perm[0]) * 43758.5453;
    return n - Math.floor(n);
  }

  /**
   * Nyquist-Aware Ridged Multifractal (Musgrave style) for sharp mountain ridges & badlands.
   * Automatically attenuates octaves whose spatial wavelength is smaller than 2 * voxelSize
   * so low-resolution SDF volumes never alias or blur!
   */
  public ridgedMultifractal2D(
    worldX: number,
    worldZ: number,
    baseScale: number,
    octaves: number,
    lacunarity: number,
    gain: number,
    nyquistMinWavelength: number,
    ridgeSharpness = 1.35
  ): { value: number; clampedOctaves: number } {
    let sum = 0;
    let maxWeight = 0;
    let freq = 1.0 / Math.max(1.0, baseScale);
    let amp = 0.55;
    let weight = 1.0;
    let clampedOctaves = 0;

    // Require at least 3.2 voxels per ridge wavelength so ridges never alias into 1-voxel spikes
    const safeMinWavelength = nyquistMinWavelength * 1.65;

    for (let i = 0; i < octaves; i++) {
      const wavelength = 1.0 / freq;
      if (wavelength < safeMinWavelength * 0.85) {
        clampedOctaves++;
        break;
      }
      const nyquistWeight =
        wavelength < safeMinWavelength * 1.6
          ? Math.max(0, (wavelength - safeMinWavelength * 0.85) / (safeMinWavelength * 0.75))
          : 1.0;

      const raw = this.simplex2D(worldX * freq, worldZ * freq);
      // Smooth C1 hyperbolic ridge fold prevents thorny 1-voxel cusps
      const smoothAbs = Math.sqrt(raw * raw + 0.018) - 0.08;
      let n = Math.max(0.0, 1.0 - smoothAbs);
      n = Math.pow(n, Math.min(1.65, ridgeSharpness));
      n *= weight;
      weight = Math.max(0.15, Math.min(1.0, n * gain * 1.75));

      sum += n * amp * nyquistWeight;
      maxWeight += amp * nyquistWeight;
      freq *= lacunarity;
      amp *= 0.5;
    }

    return {
      value: maxWeight > 0 ? Math.min(1.0, Math.max(0.0, (sum / maxWeight) * 0.95)) : 0.0,
      clampedOctaves,
    };
  }

  /**
   * Nyquist-Aware FBM for Simplex / Perlin / Value generators
   */
  public fbm2D(
    worldX: number,
    worldZ: number,
    baseScale: number,
    octaves: number,
    lacunarity: number,
    persistence: number,
    nyquistMinWavelength: number,
    mode: 'simplex' | 'perlin' | 'value' = 'simplex'
  ): { value: number; clampedOctaves: number } {
    let sum = 0;
    let maxAmp = 0;
    let freq = 1.0 / Math.max(1.0, baseScale);
    let amp = 1.0;
    let clampedOctaves = 0;

    for (let i = 0; i < octaves; i++) {
      const wavelength = 1.0 / freq;
      if (wavelength < nyquistMinWavelength * 0.85) {
        clampedOctaves++;
        break;
      }
      const nyquistWeight =
        wavelength < nyquistMinWavelength * 1.5
          ? Math.max(0, (wavelength - nyquistMinWavelength * 0.85) / (nyquistMinWavelength * 0.65))
          : 1.0;

      let sample = 0;
      if (mode === 'simplex') sample = this.simplex2D(worldX * freq, worldZ * freq);
      else if (mode === 'perlin') sample = this.perlin2D(worldX * freq, worldZ * freq);
      else sample = this.value2D(worldX * freq, worldZ * freq);

      sum += (sample * 0.5 + 0.5) * amp * nyquistWeight;
      maxAmp += amp * nyquistWeight;
      freq *= lacunarity;
      amp *= persistence;
    }

    return {
      value: maxAmp > 0 ? sum / maxAmp : 0.5,
      clampedOctaves,
    };
  }
}

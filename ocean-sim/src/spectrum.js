import { G, NCOMP, PARAMS, LAB } from './config.js?v=5';
import { SWELL_N, SEA_N, CHOP_N } from './config.js?v=5';

// Cascade bands: [wavelengthMin, wavelengthMax, count]
const BANDS = [[60, 600, SWELL_N], [8, 60, SEA_N], [1, 8, CHOP_N]];
const SPREAD_SIGMA = [0.21, 0.44, 0.70];   // directional spread (rad, gaussian sigma)
const SPREAD_S = [20, 8, 4];               // cos^2s directional shape exponents
const STEEP = [0.8, 1.0, 1.25];            // per-cascade steepness targets
const GAMMA = 3.3;                         // JONSWAP peak enhancement

function mulberry32(seed) {
  return function () {
    seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// ---------------------------------------------------------------------------
// WaveField: JONSWAP spectral ocean (Hasselmann 1973) with fetch-limited
// growth laws, Donelan-style directional spreading and Tessendorf-style
// per-wave steepness. Components are packed for the GPU vertex sum, and the
// same math is evaluated on CPU (buoy, probes, cancellation meter) so the
// simulation is one consistent model, not just a picture.
// ---------------------------------------------------------------------------
export class WaveField {
  constructor() {
    this.jitterL = new Float32Array(NCOMP);
    this.delta = new Float32Array(NCOMP);
    this.phase = new Float32Array(NCOMP);
    this.k = new Float32Array(NCOMP);
    this.om = new Float32Array(NCOMP);
    this.amp = new Float32Array(NCOMP);
    this.Q = new Float32Array(NCOMP);
    this.dirX = new Float32Array(NCOMP);
    this.dirZ = new Float32Array(NCOMP);
    this.casc = new Float32Array(NCOMP);
    // GPU pack: texA = dir.x, dir.z, k, omega | texB = amp, Q, phase, cascade
    this.dataA = new Float32Array(NCOMP * 4);
    this.dataB = new Float32Array(NCOMP * 4);

    this.Hs = 1; this.Tp = 7; this.fp = 0.14; this.m0 = 0.06; this.swellK = 2.2;
    this.saturated = false;
    this.seed = 1337;
    this.rollSeed(this.seed);
    this.update();
  }

  rollSeed(s) {
    this.seed = s;
    const rnd = mulberry32(s);
    let i = 0;
    for (let c = 0; c < 3; c++) {
      const n = BANDS[c][2];
      for (let k = 0; k < n; k++, i++) {
        this.jitterL[i] = 0.85 + rnd() * 0.3;
        const g = (rnd() + rnd() + rnd() - 1.5) * 2; // ~N(0,1)
        this.delta[i] = g * SPREAD_SIGMA[c];
        this.phase[i] = rnd() * Math.PI * 2;
      }
    }
  }

  jonswap(f, fp, alpha) {
    if (f <= 0.004) return 0;
    const sigma = f <= fp ? 0.07 : 0.09;
    const d = (f - fp) / (sigma * fp);
    const r = Math.exp(-0.5 * d * d);
    const tail = Math.exp(-1.25 * Math.pow(fp / f, 4));
    return alpha * G * G * Math.pow(2 * Math.PI, -4) * Math.pow(f, -5) * tail * Math.pow(GAMMA, r);
  }

  // Recompute spectrum from live params. Wavelength jitter, direction offsets
  // and phases are seed-fixed, so wind/fetch edits morph smoothly (no popping).
  update() {
    const U = Math.max(PARAMS.wind, 0.3);
    const F = Math.max(PARAMS.fetch, 2) * 1000;
    const wd = PARAMS.windDir * Math.PI / 180;
    const chop = PARAMS.chop;
    const X = Math.min(Math.max(G * F / (U * U), 10), 30000);
    const alpha = 0.076 * Math.pow(X, -0.22);
    const Tp = Math.min(0.286 * Math.pow(X, 0.33), 7.54) * U / G;
    const fp = 1 / Tp;

    const fArr = new Float32Array(NCOMP);
    let i = 0;
    for (let c = 0; c < 3; c++) {
      const L0 = BANDS[c][0], L1 = BANDS[c][1], n = BANDS[c][2];
      const l0 = Math.log(L0), l1 = Math.log(L1);
      for (let k = 0; k < n; k++, i++) {
        const L = Math.exp(l0 + (l1 - l0) * (k + 0.5) / n) * this.jitterL[i];
        const kk = 2 * Math.PI / L;
        const f = Math.sqrt(G * kk) / (2 * Math.PI);
        fArr[i] = f;
        this.k[i] = kk;
        this.om[i] = 2 * Math.PI * f;
        this.casc[i] = c;
      }
    }

    const raw = new Float32Array(NCOMP);
    let m0 = 0;
    i = 0;
    for (let c = 0; c < 3; c++) {
      const n = BANDS[c][2];
      for (let k = 0; k < n; k++, i++) {
        const f = fArr[i];
        const i0 = k === 0 ? i : i - 1;
        const i1 = k === n - 1 ? i : i + 1;
        const df = Math.max(Math.abs(fArr[i1] - fArr[i0]) / 2, 1e-5);
        const S = this.jonswap(f, fp, alpha);
        const D = Math.pow(Math.max(Math.cos(this.delta[i] * 0.5), 0), 2 * SPREAD_S[c]);
        const a = Math.sqrt(Math.max(2 * S * df, 0)) * (0.25 + 0.75 * D);
        raw[i] = a;
        m0 += a * a / 2;
      }
    }

    // Normalize energy to the fetch-limited significant wave height Hs = 4*sqrt(m0)
    const fetchTerm = 0.0016 * Math.sqrt(X);
    this.saturated = fetchTerm >= 0.21; // fully developed: fetch slider is honestly idle
    const HsT = (U * U / G) * Math.min(fetchTerm, 0.21);
    const scale = HsT / Math.max(4 * Math.sqrt(m0), 1e-4);

    let swSum = 0, sw8 = 0;
    for (let j = 0; j < NCOMP; j++) {
      const A = raw[j] * scale;
      this.amp[j] = A;
      const c = this.casc[j];
      const n = BANDS[c][2];
      const sjFrac = (this.jitterL[j] * 7.31) % 1;
      const sj = (0.7 + 0.6 * sjFrac) * STEEP[c] * chop;
      let Q = sj / (this.k[j] * A * Math.sqrt(n) + 1e-6);
      Q = Math.min(Q, 0.9 / (this.k[j] * A + 1e-6)); // hard clamp: no Gerstner loops
      this.Q[j] = Q;
      const th = wd + this.delta[j];
      this.dirX[j] = Math.cos(th);
      this.dirZ[j] = Math.sin(th);
      this.dataA[j * 4] = this.dirX[j];
      this.dataA[j * 4 + 1] = this.dirZ[j];
      this.dataA[j * 4 + 2] = this.k[j];
      this.dataA[j * 4 + 3] = this.om[j];
      this.dataB[j * 4] = A;
      this.dataB[j * 4 + 1] = Q;
      this.dataB[j * 4 + 2] = this.phase[j];
      this.dataB[j * 4 + 3] = c;
      if (c === 0) { swSum += A; if (j < 8) sw8 += A; }
    }
    this.m0 = m0 * scale * scale;
    this.Hs = 4 * Math.sqrt(this.m0);
    this.Tp = Tp;
    this.fp = fp;
    this.swellK = swSum / Math.max(sw8, 1e-5);
  }

  labHeight(L, x, z, t, use) {
    if (!use || !L.on || L.amp <= 0) return 0;
    const dx = (x - LAB.x) / LAB.r, dz = (z - LAB.z) / LAB.r;
    const env = Math.exp(-(dx * dx + dz * dz) * 1.5);
    const k = 2 * Math.PI / Math.max(L.lambda, 1);
    const om = Math.sqrt(G * k);
    const th = L.dir * Math.PI / 180;
    return L.amp * env * Math.sin(k * (Math.cos(th) * x + Math.sin(th) * z) - om * t + L.phase * Math.PI / 180);
  }

  // Full analytic surface height, including the relief (vertical exaggeration)
  // factor so CPU probes stay glued to the rendered surface. (Deep-water part;
  // shoaling/breaking is a GPU shaping stage on top, see ocean vertex shader.)
  height(x, z, t, useA, useB) {
    let h = 0;
    for (let j = 0; j < NCOMP; j++) {
      h += this.amp[j] * Math.sin(this.k[j] * (this.dirX[j] * x + this.dirZ[j] * z) - this.om[j] * t + this.phase[j]);
    }
    if (useA === undefined) { useA = true; useB = true; }
    h += this.labHeight(PARAMS.labA, x, z, t, useA);
    h += this.labHeight(PARAMS.labB, x, z, t, useB);
    return h * PARAMS.relief;
  }

  grad(x, z, t) {
    const e = 1.5;
    const hx = this.height(x + e, z, t) - this.height(x - e, z, t);
    const hz = this.height(x, z + e, t) - this.height(x, z - e, t);
    return [hx / (2 * e), hz / (2 * e)];
  }

  // Measured crest-to-trough relief over the central field (includes relief
  // factor) — the honest number behind "are there waves".
  reliefAt(t) {
    let mn = 1e9, mx = -1e9;
    for (let j = 0; j < 12; j++) {
      for (let k = 0; k < 12; k++) {
        const h = this.height(-150 + j * (300 / 11), -150 + k * (300 / 11), t);
        if (h < mn) mn = h;
        if (h > mx) mx = h;
      }
    }
    return mx - mn;
  }

  // Interference factor over the lab disc: var(A+B) / (varA + varB).
  //  ~0 = destructive cancellation, ~1 = incoherent, ~2 = fully constructive.
  interference(t) {
    const n = 7, span = LAB.r * 1.5;
    let sA = 0, sB = 0, sAB = 0, mA = 0, mB = 0, mAB = 0, cnt = 0;
    for (let j = 0; j < n; j++) {
      for (let k = 0; k < n; k++) {
        const x = LAB.x + (j / (n - 1) - 0.5) * 2 * span;
        const z = LAB.z + (k / (n - 1) - 0.5) * 2 * span;
        const dx = (x - LAB.x) / LAB.r, dz = (z - LAB.z) / LAB.r;
        if (dx * dx + dz * dz > 1.2) continue;
        // lab-only fields (ocean cancels out of the ratio either way, but the
        // lab-only view is the clean demonstration)
        const a = this.labHeight(PARAMS.labA, x, z, t, true);
        const b = this.labHeight(PARAMS.labB, x, z, t, true);
        mA += a; mB += b; mAB += a + b;
        sA += a * a; sB += b * b; sAB += (a + b) * (a + b);
        cnt++;
      }
    }
    if (cnt === 0) return { ratio: 1, active: false };
    mA /= cnt; mB /= cnt; mAB /= cnt;
    const vA = Math.max(sA / cnt - mA * mA, 1e-9);
    const vB = Math.max(sB / cnt - mB * mB, 1e-9);
    const vAB = Math.max(sAB / cnt - mAB * mAB, 0);
    const active = PARAMS.labA.on || PARAMS.labB.on;
    return { ratio: vAB / (vA + vB), active };
  }

  spectrumCurve(f) {
    const U = Math.max(PARAMS.wind, 0.3);
    const F = Math.max(PARAMS.fetch, 2) * 1000;
    const X = Math.min(Math.max(G * F / (U * U), 10), 30000);
    const alpha = 0.076 * Math.pow(X, -0.22);
    const Tp = Math.min(0.286 * Math.pow(X, 0.33), 7.54) * U / G;
    return this.jonswap(f, 1 / Tp, alpha);
  }
}

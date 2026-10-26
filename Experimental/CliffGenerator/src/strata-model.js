// Stratigraphic column shared by the heightfield terracing, the erosion hardness, the true-3D
// cliff carving and the surface shader — so the beds you see are the beds that were carved.
//
// Instead of beds of one thickness with a hashed hardness, the column is a sequence of
// *packages*: thin-bedded packages (shales / siltstones, mostly soft, the odd hard ledge),
// massive packages (thick sandstone / limestone, mostly hard, the odd soft parting) and mixed
// ones, each bed with its own thickness (log-normal around the band thickness), hardness, tint
// and an optional gradational base (hardness fading downwards — cross-bedded sets that weather
// back into the bed below).
//
// Elevation goes through a lateral *jitter* (beds thicken and thin across the tile) which is a
// sum of long sines so JS and GLSL evaluate exactly the same thing.

import { mulberry32 } from './noise.js';

const Y_MIN = -1500, Y_MAX = 5000;

export function makeBedTable(params) {
  const band = Math.max(2, params.strataBand || 26);
  const variation = params.strataVariation == null ? 0.6 : Math.max(0, Math.min(1.5, params.strataVariation));
  const packaging = params.strataPackaging == null ? 0.6 : Math.max(0, Math.min(1, params.strataPackaging));
  const hardShare = params.strataHardShare == null ? 0.4 : Math.max(0, Math.min(1, params.strataHardShare));
  const rand = mulberry32(((params.seed | 0) * 977 + 13) >>> 0);
  const gauss = () => { // Box–Muller
    const u = Math.max(1e-6, rand()), w = rand();
    return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * w);
  };
  const tops = [], hard = [], thick = [], tint = [], grad = [], pkg = [];
  let y = Y_MIN, guard = 0;
  while (y < Y_MAX && guard++ < 4000) {
    const r = rand();
    const type = r < packaging * 0.45 ? 0 /* thin-bedded */ : r < packaging * 0.72 ? 1 /* massive */ : 2 /* mixed */;
    const n = 2 + Math.floor(rand() * 6);
    for (let k = 0; k < n && y < Y_MAX; k++) {
      let t = band * Math.exp(gauss() * 0.5 * variation) * (type === 0 ? 0.42 : type === 1 ? 1.45 : 1);
      t = Math.max(band * 0.12, Math.min(band * 4, t));
      let h;
      // soft beds are decidedly soft (≤ 0.3 → full notch / terrace tread), hard ones decidedly
      // hard: the contrast between them is what reads as stratification
      if (type === 0) h = rand() < 0.15 ? 0.62 + 0.3 * rand() : 0.02 + 0.24 * rand();
      else if (type === 1) h = rand() < 0.18 ? 0.08 + 0.22 * rand() : 0.72 + 0.28 * rand();
      else h = rand() < hardShare ? 0.66 + 0.34 * rand() : 0.03 + 0.27 * rand();
      y += t;
      tops.push(y); hard.push(h); thick.push(t); tint.push(rand()); grad.push(h > 0.5 && rand() < 0.4 ? 1 : 0); pkg.push(type);
    }
  }
  return {
    count: tops.length, base: Y_MIN, band,
    tops: Float32Array.from(tops), hard: Float32Array.from(hard), thick: Float32Array.from(thick),
    tint: Float32Array.from(tint), grad: Uint8Array.from(grad), pkg: Uint8Array.from(pkg),
  };
}

// bed containing tilted elevation t: index, fraction within the bed (0 base → 1 top), thickness
export function bedAt(table, t, out = {}) {
  const { tops, count } = table;
  let lo = 0, hi = count - 1;
  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if (t < tops[mid]) hi = mid; else lo = mid + 1;
  }
  const top = tops[lo], thick = table.thick[lo];
  out.index = lo;
  out.f = Math.max(0, Math.min(1, (t - (top - thick)) / thick));
  out.thick = thick;
  out.hard = table.hard[lo];
  out.tint = table.tint[lo];
  out.grad = table.grad[lo];
  return out;
}

// lateral thickness jitter — identical expression in the shader (bedJitter in surface-shader.js)
export function bedJitter(x, z, amount = 0.18) {
  const a = Math.sin(x * 0.0091 + 0.7 * Math.sin(z * 0.0063 + 1.3));
  const b = Math.cos(z * 0.0077 + 0.5 * Math.sin(x * 0.0052 + 0.4));
  return 1 + amount * (0.6 * a + 0.4 * b);
}

// hardness including the gradational base
export function bedHardness(bed) {
  return bed.grad ? bed.hard * (0.55 + 0.45 * bed.f) : bed.hard;
}

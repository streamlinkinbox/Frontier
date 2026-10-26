// Sedimentary geology volume — the thing that makes cliffs *structural*
// instead of noise-banded.
//
// Model, bottom-up (all fields are functions of 3D position, so a cave carved
// anywhere exposes the correct rock inside the mountain):
//
//  1. Bed stack. A seeded sequence of beds, each with thickness, hardness,
//     colour and type (hard sandstone / limestone, siltstone, soft shale).
//     Unequal, non-repeating thicknesses — real strata are not a sine wave.
//  2. Stratigraphic coordinate  s(p) = y + dip·(x,z) + fold(x,z) + fault.
//     Bed boundaries are surfaces of constant s, so beds tilt and fold in 3D
//     instead of being world-Y stripes (the classic banding bug).
//  3. Lateral pinch-out. Each bed's thickness is scaled by its own
//     low-frequency field, so beds lens out and swell over tens of metres —
//     no bed runs as an infinite uniform band across the whole cliff.
//  4. Bedding-plane weakness: hardness dips right at bed boundaries.
//  5. Joint sets. Three families of near-vertical fracture planes. Spacing
//     is proportional to the local bed thickness (the observed rule in
//     sedimentary rock: thick beds, wide joints), phase is re-seeded per bed
//     (joints terminate at bedding boundaries), and persistence masks stop
//     joints running forever. Erosion exploits joints; block-fall removes
//     joint-bounded blocks. That is where blocky cliff geometry comes from.

import { clamp, lerp, smoothstep, mulberry32, value2, value3, fbm2, hexToRgb } from './noise.js';

const BED_TYPES = [
  // weight, hardness range, thickness range [m], colours
  { name: 'sandstone', w: 0.38, hard: [0.72, 0.95], thick: [2.6, 6.5], colors: ['#a98f6f', '#bfa27e', '#9c7d5f'] },
  { name: 'siltstone', w: 0.27, hard: [0.42, 0.64], thick: [1.4, 3.8], colors: ['#7d7263', '#8a7c68', '#6f6555'] },
  { name: 'shale',     w: 0.24, hard: [0.10, 0.34], thick: [0.8, 2.6], colors: ['#595045', '#4f483f', '#65584a'] },
  { name: 'limestone', w: 0.11, hard: [0.80, 0.96], thick: [2.0, 4.6], colors: ['#bbb3a2', '#c4bcab', '#ada491'] },
];

// Facies transitions (Walther-style): what tends to be deposited on top of what.
// Independent random bed types produce unrealistic 30 m runs of one lithology
// and cliff heights with no soft bed at all; a Markov chain over facies gives
// the interbedded hard/soft rhythm differential weathering needs, while still
// allowing occasional massive units (self-transition > 0).
const TRANSITIONS = {
  shale:     [['siltstone', 0.52], ['sandstone', 0.16], ['shale', 0.20], ['limestone', 0.12]],
  siltstone: [['sandstone', 0.40], ['shale', 0.30], ['siltstone', 0.20], ['limestone', 0.10]],
  sandstone: [['shale', 0.36], ['siltstone', 0.34], ['limestone', 0.12], ['sandstone', 0.18]],
  limestone: [['shale', 0.34], ['siltstone', 0.30], ['sandstone', 0.20], ['limestone', 0.16]],
};

export class Geology {
  constructor(params) {
    this.params = params;
    const rng = mulberry32((params.seed | 0) * 7919 + 1013);
    this.beds = [];
    let top = -30; // stratigraphic metres; stack covers well past the volume
    let guard = 0;
    while (top < 110 && guard++ < 90) {
      const prev = this.beds[this.beds.length - 1];
      let type = BED_TYPES[0];
      if (!prev) {
        let r = rng();
        for (const t of BED_TYPES) { if ((r -= t.w) <= 0) { type = t; break; } }
      } else {
        let r = rng();
        for (const [name, w] of TRANSITIONS[prev.type]) {
          if ((r -= w) <= 0) { type = BED_TYPES.find((t) => t.name === name); break; }
        }
      }
      const thickness = lerp(type.thick[0], type.thick[1], Math.pow(rng(), 0.8)) * (params.bedScale ?? 1);
      this.beds.push({
        type: type.name,
        base: top,
        thickness,
        hardness: lerp(type.hard[0], type.hard[1], rng()),
        color: hexToRgb(type.colors[(rng() * type.colors.length) | 0]),
        lateralSeed: (rng() * 1e6) | 0,
        jointPhase: rng(),
      });
      top += thickness;
    }
    this.totalThickness = top + 30;
    const dipRad = (params.dip ?? 4) * Math.PI / 180;
    const dirRad = (params.dipDirection ?? 35) * Math.PI / 180;
    this.dipGrad = [Math.tan(dipRad) * Math.cos(dirRad), Math.tan(dipRad) * Math.sin(dirRad)];
    this.jointSets = [
      { az: 22, spacingK: 0.95, persistence: 0.85, seed: 11 },
      { az: 109, spacingK: 1.30, persistence: 0.75, seed: 47 },
      { az: -52, spacingK: 2.10, persistence: 0.45, seed: 83 },
    ].map((s) => ({ ...s, nx: Math.cos(s.az * Math.PI / 180), nz: Math.sin(s.az * Math.PI / 180) }));
  }

  /** Stratigraphic offset (everything except +y): dip + fold + fault. Column-constant. */
  stratOffset(x, z) {
    const p = this.params;
    let s = this.dipGrad[0] * x + this.dipGrad[1] * z;
    const fold = (p.fold ?? 2.5);
    if (fold > 0) s += fold * (fbm2(x * 0.022 + 3.1, z * 0.022 - 1.7, 3, p.seed) - 0.5) * 2
                      + fold * 0.24 * (fbm2(x * 0.075 - 6.2, z * 0.075 + 2.9, 2, p.seed + 5) - 0.5) * 2;
    const throw_ = p.faultThrow ?? 0;
    if (Math.abs(throw_) > 1e-6) {
      const line = (p.faultPos ?? -18) + 4 * (value2(z * 0.03, 8.8, p.seed + 9) - 0.5);
      if (x * 0.8 + z * 0.6 > line) s += throw_;
    }
    return s;
  }

  /** Bed boundary positions (in s) for one column, with lateral pinch-out applied. */
  columnBoundaries(x, z, out) {
    const lateral = this.params.lateral ?? 0.55;
    let acc = -30;
    for (let i = 0; i < this.beds.length; i++) {
      const b = this.beds[i];
      const scale = lateral <= 0 ? 1
        : clamp(1 + lateral * (value2(x * 0.042, z * 0.042, b.lateralSeed) - 0.5) * 2.4, 0.12, 2.3);
      acc += b.thickness * scale;
      out[i] = acc; // top of bed i, in stratigraphic s
    }
    return out;
  }

  bedIndexAt(s, boundaries) {
    let lo = 0, hi = this.beds.length - 1;
    while (lo < hi) { const mid = (lo + hi) >> 1; if (s <= boundaries[mid]) hi = mid; else lo = mid + 1; }
    return lo;
  }

  /** Full bed lookup at a world point (used sparingly: cave placement, HUD). */
  bedAtPoint(x, y, z) {
    const bounds = this.columnBoundaries(x, z, new Float64Array(this.beds.length));
    const s = y + this.stratOffset(x, z);
    const i = this.bedIndexAt(s, bounds);
    const topS = bounds[i], botS = i > 0 ? bounds[i - 1] : -30;
    return { index: i, bed: this.beds[i], localThickness: topS - botS, topY: topS - this.stratOffset(x, z), bottomY: botS - this.stratOffset(x, z) };
  }

  /** Find a soft, reasonably thick bed whose centre is near targetY at (x,z) — cave host. */
  findSoftBed(x, z, targetY) {
    const bounds = this.columnBoundaries(x, z, new Float64Array(this.beds.length));
    const off = this.stratOffset(x, z);
    let best = null, bestScore = Infinity;
    for (let i = 1; i < this.beds.length; i++) {
      const b = this.beds[i];
      const thick = bounds[i] - bounds[i - 1];
      if (b.hardness > 0.42 || thick < 1.4) continue;
      const centreY = (bounds[i] + bounds[i - 1]) / 2 - off;
      const score = Math.abs(centreY - targetY) + b.hardness * 4;
      if (score < bestScore) { bestScore = score; best = { index: i, bed: b, centreY, thickness: thick }; }
    }
    return best;
  }

  /** Joint proximity ∈ [0,1] at a point inside the given bed (1 = on a fracture plane). */
  jointAt(x, y, z, bedIndex, localThickness) {
    const p = this.params;
    const halfWidth = (p.jointWidth ?? 0.55);
    let best = 0;
    for (const set of this.jointSets) {
      const spacing = clamp(localThickness * set.spacingK * (p.jointSpacing ?? 1), 1.2, 11);
      const u = (x * set.nx + z * set.nz) / spacing + this.beds[bedIndex].jointPhase * 7.31 + set.seed;
      const frac = u - Math.floor(u);
      const dist = Math.min(frac, 1 - frac) * spacing;
      if (dist > halfWidth * 2.4) continue;
      // persistence: joints die out along strike and with height in the bed
      const v = -x * set.nz + z * set.nx;
      const persist = set.persistence * smoothstep(0.28, 0.60, value3(v * 0.075, y * 0.060, set.seed * 1.7, set.seed));
      const prox = (1 - smoothstep(0, halfWidth, dist)) * clamp(persist * 1.6, 0, 1);
      if (prox > best) best = prox;
    }
    return best;
  }
}

/**
 * Fill the volume's per-node geology fields: bed index, hardness (0..255),
 * joint proximity (0..255), local bed thickness (decimetres, clamped).
 * Async in slabs so the browser can show progress; Node tests just await it.
 */
export async function fillGeology(volume, geology, onProgress = null, yieldFn = null) {
  const { nx, ny, nz, spacing, origin } = volume;
  const bounds = new Float64Array(geology.beds.length);
  const seed = geology.params.seed;
  for (let iz = 0; iz < nz; iz++) {
    const z = origin[2] + iz * spacing;
    for (let ix = 0; ix < nx; ix++) {
      const x = origin[0] + ix * spacing;
      const off = geology.stratOffset(x, z);
      geology.columnBoundaries(x, z, bounds);
      for (let iy = 0; iy < ny; iy++) {
        const y = origin[1] + iy * spacing;
        const s = y + off;
        const bi = geology.bedIndexAt(s, bounds);
        const bed = geology.beds[bi];
        const topS = bounds[bi], botS = bi > 0 ? bounds[bi - 1] : -30;
        const localThick = topS - botS;
        // bedding-plane weakness + mild intra-bed heterogeneity (coarse 3D, not a texture)
        const seamDist = Math.min(s - botS, topS - s);
        const seam = 1 - 0.28 * (1 - smoothstep(0, 0.3, seamDist));
        const hetero = 0.93 + 0.14 * value3(x * 0.07, y * 0.07, z * 0.07, seed + bi * 13);
        const h = clamp(bed.hardness * seam * hetero, 0.03, 1);
        const idx = volume.index(ix, iy, iz);
        volume.bed[idx] = bi;
        volume.hardness[idx] = Math.round(h * 255);
        volume.joint[idx] = Math.round(geology.jointAt(x, y, z, bi, localThick) * 255);
        volume.bedThick[idx] = Math.round(clamp(localThick * 10, 1, 255));
      }
    }
    if (onProgress && (iz & 7) === 0) onProgress(iz / nz);
    if (yieldFn && (iz & 7) === 0) await yieldFn();
  }
  if (onProgress) onProgress(1);
}

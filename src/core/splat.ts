// ---------------------------------------------------------------------------
// Frontier core / splatmaps (Gaea-style texturing)
// Mask extractors turn geometry + hydrology into per-layer coverage masks,
// which are packed 4-per-RGBA-texture (splat sets). The viewport shader blends
// layer colours by these masks; the same buffers export as PNG splatmaps.
// ---------------------------------------------------------------------------

import { HeightField, slopeAt, curvatureAt } from './heightfield';
import { SDFVolume } from './volume';
import { FlowField } from './erosion/flow';

export interface MaskInputs {
  hf: HeightField;
  flow: FlowField | null;
  ground: SDFVolume | null;
}

export type MaskKind = 'height' | 'slope' | 'curvature' | 'flow' | 'cavity' | 'moisture' | 'strata' | 'exposure';

export interface MaskRange { lo: number; hi: number; feather: number }

const smoothstep = (e0: number, e1: number, x: number): number => {
  const t = Math.max(0, Math.min(1, (x - e0) / (e1 - e0 || 1e-6)));
  return t * t * (3 - 2 * t);
};

/** map a raw scalar field through a feathered [lo, hi] window into [0,1] */
export function applyRange(raw: Float32Array, range: MaskRange): Float32Array {
  const f = Math.max(0.001, range.feather);
  const out = new Float32Array(raw.length);
  for (let i = 0; i < raw.length; i++) {
    out[i] = smoothstep(range.lo - f, range.lo + f, raw[i]) * (1 - smoothstep(range.hi - f, range.hi + f, raw[i]));
    if (range.lo > range.hi) out[i] = 1 - out[i];
  }
  return out;
}

export function buildMask(kind: MaskKind, inp: MaskInputs, range: MaskRange, strataBand = 24): Float32Array {
  const { nx, nz } = inp.hf;
  const n = nx * nz;
  const raw = new Float32Array(n);
  const { hf } = inp;

  switch (kind) {
    case 'height':
      for (let i = 0; i < n; i++) raw[i] = hf.data[i];
      break;
    case 'slope':
      for (let z = 0; z < nz; z++) for (let x = 0; x < nx; x++) raw[z * nx + x] = slopeAt(hf, x, z);
      break;
    case 'curvature':
      for (let z = 0; z < nz; z++) for (let x = 0; x < nx; x++) raw[z * nx + x] = curvatureAt(hf, x, z) * hf.h * hf.h * 8;
      break;
    case 'flow': {
      const acc = inp.flow?.acc;
      if (acc) {
        let maxLog = 1;
        for (let i = 0; i < n; i++) maxLog = Math.max(maxLog, Math.log(1 + acc[i]));
        for (let i = 0; i < n; i++) raw[i] = Math.log(1 + acc[i]) / maxLog;
      }
      break;
    }
    case 'cavity': {
      // concavity from height laplacian, biased by SDF horizon proximity
      for (let z = 0; z < nz; z++) for (let x = 0; x < nx; x++) {
        raw[z * nx + x] = -curvatureAt(hf, x, z) * hf.h * hf.h * 8;
      }
      break;
    }
    case 'moisture': {
      const acc = inp.flow?.acc;
      if (acc) {
        let maxLog = 1;
        for (let i = 0; i < n; i++) maxLog = Math.max(maxLog, Math.log(1 + acc[i]));
        for (let i = 0; i < n; i++) {
          const valley = Math.max(0, -curvatureAt(hf, i % nx, (i / nx) | 0) * hf.h * hf.h * 6);
          raw[i] = (Math.log(1 + acc[i]) / maxLog) * 0.6 + Math.min(1, valley);
        }
      }
      break;
    }
    case 'strata':
      for (let i = 0; i < n; i++) raw[i] = Math.sin((hf.data[i] / Math.max(1, strataBand)) * Math.PI * 2);
      break;
    case 'exposure': {
      // sky exposure: how far the surface sits above local mean (ridge mask)
      for (let z = 0; z < nz; z++) for (let x = 0; x < nx; x++) {
        let sum = 0, cnt = 0;
        for (let dz = -6; dz <= 6; dz += 3) for (let dx = -6; dx <= 6; dx += 3) {
          const ax = x + dx, az = z + dz;
          if (ax < 0 || az < 0 || ax >= nx || az >= nz) continue;
          sum += hf.data[az * nx + ax]; cnt++;
        }
        raw[z * nx + x] = hf.data[z * nx + x] - sum / Math.max(1, cnt);
      }
      break;
    }
  }

  return applyRange(raw, range);
}

export interface SplatLayer {
  name: string;
  color: [number, number, number];
  roughness: number;
  mask: Float32Array;
}

export interface SplatSet {
  nx: number;
  nz: number;
  /** RGBA byte textures, 4 layers each */
  textures: Uint8Array[];
  layers: SplatLayer[];
}

export function packSplatmaps(layers: SplatLayer[], nx: number, nz: number): SplatSet {
  const textures: Uint8Array[] = [];
  for (let s = 0; s < layers.length; s += 4) {
    const group = layers.slice(s, s + 4);
    const tex = new Uint8Array(nx * nz * 4);
    for (let i = 0; i < nx * nz; i++) {
      // normalise weights within the group so the shader can blend directly
      let sum = 0;
      for (const l of group) sum += l.mask[i];
      for (let c = 0; c < 4; c++) {
        const m = group[c]?.mask[i] ?? 0;
        tex[i * 4 + c] = Math.round(255 * (sum > 0 ? m / sum : c === 0 ? 1 : 0));
      }
    }
    textures.push(tex);
  }
  return { nx, nz, textures, layers };
}

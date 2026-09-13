// ============================================================================
// Frontier SDF Terrain — Node graph: definitions, field compilation, presets.
//
// Field nodes compile to closures:  eval(x, y, z, out) -> d
//   out = { h: hardness 0..1, s: strata 0..1, m: material id 0..1 }
// Every operator is a TRUE 3D SDF operator (offsets, unions, smooth blends).
// Noise displaces the SDF iso-surface in 3D — never a 2D height lookup — so
// caves, arches and overhangs survive generation AND erosion.
//
// Erode nodes configure realtime simulation modules (see erosion.js).
// Material node configures the satellite-shader uniforms (see render.js).
// ============================================================================

import {
  fbm3, ridged3, billow3, mountainMF3, voronoi3, valueNoise3,
  warpOffset, terrace, strataCoord, rockHardness, clamp, lerp,
} from './noise.js';
import { smin, smax } from './sdf.js';

// ---------------------------------------------------------------------------
// Node definitions (UI schema + factories)
// ---------------------------------------------------------------------------
// param types: float, int, seed, choice, bool
let _uid = 1;
export const nid = () => 'n' + (_uid++);

export const NODE_DEFS = {
  // -- sources ----------------------------------------------------------------
  IslandMask: {
    title: 'Island Mask', cat: 'Source', color: '#4d80ff',
    desc: 'Rounded landmass falloff: radial mask + sea level. The base all features add to.',
    inputs: [], outputs: [{ id: 'f', label: 'SDF' }],
    params: [
      { id: 'radius', label: 'Radius', type: 'float', min: 0.2, max: 1.4, step: 0.01, def: 0.86 },
      { id: 'seaLevel', label: 'Sea level', type: 'float', min: -0.9, max: 0.4, step: 0.01, def: -0.28 },
      { id: 'edge', label: 'Edge softness', type: 'float', min: 0.02, max: 0.8, step: 0.01, def: 0.34 },
      { id: 'baseHard', label: 'Base hardness', type: 'float', min: 0.02, max: 1, step: 0.01, def: 0.55 },
    ],
    make(p) {
      return (x, y, z, out) => {
        const r = Math.sqrt(x * x * 0.9 + z * z * 1.1);
        // land rises above sea inside radius; SDF approx of a flattened dome
        const mask = clamp((p.radius - r) / p.edge, -1.5, 1.5);
        const domeH = 0.34 * clamp(mask, 0, 1);
        const d = (y - p.seaLevel - domeH * Math.exp(-r * r * 1.1)) - 0.5 * clamp(mask, -1, 0) * p.edge;
        out.h = p.baseHard; out.s = 0.5 + 0.2 * Math.sin(y * 22.0); out.m = 0.3;
        return d;
      };
    },
  },

  MountainMF: {
    title: 'Mountain Noise', cat: 'Noise', color: '#8a63ff',
    desc: 'Ridged multifractal massif. Displaces the SDF along its own gradient (true 3D).',
    inputs: [{ id: 'in', label: 'SDF in' }], outputs: [{ id: 'f', label: 'SDF' }],
    params: [
      { id: 'amp', label: 'Amplitude', type: 'float', min: 0, max: 1.2, step: 0.01, def: 0.52 },
      { id: 'freq', label: 'Frequency', type: 'float', min: 0.3, max: 12, step: 0.05, def: 2.1 },
      { id: 'oct', label: 'Octaves', type: 'int', min: 1, max: 9, step: 1, def: 6 },
      { id: 'rough', label: 'Roughness (H)', type: 'float', min: 0.2, max: 1.4, step: 0.01, def: 0.78 },
      { id: 'sharp', label: 'Crest gain', type: 'float', min: 0.5, max: 3, step: 0.01, def: 1.6 },
      { id: 'basis', label: 'Basis', type: 'choice', options: ['Value', 'Perlin', 'Simplex'], def: 1 },
      { id: 'seed', label: 'Seed', type: 'seed', def: 1337 },
      { id: 'warp', label: 'Domain warp', type: 'float', min: 0, max: 1, step: 0.01, def: 0.22 },
      { id: 'maskLow', label: 'Apply above', type: 'float', min: -1, max: 1, step: 0.01, def: -0.55 },
    ],
    make(p) {
      const w = { x: 0, y: 0, z: 0 };
      return (x, y, z, out, input) => {
        const dIn = input ? input(x, y, z, out) : y;
        warpOffset(x, y, z, p.warp * 0.55, p.freq * 0.5, p.seed, p.basis, w);
        const n = mountainMF3(
          (x + w.x) * p.freq, (y + w.y * 0.6) * p.freq, (z + w.z) * p.freq,
          p.oct, 2.08, p.rough, 0.95, p.sharp, p.seed, p.basis);
        const gate = clamp((dIn - p.maskLow) * 2.2 + 0.5, 0, 1);
        const disp = (n - 0.42) * p.amp * (0.25 + 0.75 * gate);
        // harden high ridges (exposed bedrock), soften valley fill
        out.h = clamp(out.h + (n - 0.45) * 0.5, 0.02, 1);
        return dIn - disp;
      };
    },
  },

  Ridged: {
    title: 'Ridged Noise', cat: 'Noise', color: '#8a63ff',
    desc: 'Classic ridged fBm for sharp alpine crests.',
    inputs: [{ id: 'in', label: 'SDF in' }], outputs: [{ id: 'f', label: 'SDF' }],
    params: [
      { id: 'amp', label: 'Amplitude', type: 'float', min: 0, max: 1.2, step: 0.01, def: 0.3 },
      { id: 'freq', label: 'Frequency', type: 'float', min: 0.3, max: 14, step: 0.05, def: 3.4 },
      { id: 'oct', label: 'Octaves', type: 'int', min: 1, max: 9, step: 1, def: 5 },
      { id: 'gain', label: 'Gain', type: 'float', min: 0.2, max: 0.9, step: 0.01, def: 0.52 },
      { id: 'offset', label: 'Offset', type: 'float', min: 0.4, max: 1.4, step: 0.01, def: 1.0 },
      { id: 'basis', label: 'Basis', type: 'choice', options: ['Value', 'Perlin', 'Simplex'], def: 1 },
      { id: 'seed', label: 'Seed', type: 'seed', def: 4242 },
      { id: 'warp', label: 'Domain warp', type: 'float', min: 0, max: 1, step: 0.01, def: 0.18 },
    ],
    make(p) {
      const w = { x: 0, y: 0, z: 0 };
      return (x, y, z, out, input) => {
        const dIn = input ? input(x, y, z, out) : y;
        warpOffset(x, y, z, p.warp * 0.5, p.freq * 0.45, p.seed, p.basis, w);
        const n = ridged3((x + w.x) * p.freq, (y + w.y * 0.5) * p.freq, (z + w.z) * p.freq,
          p.oct, 2.12, p.gain, p.offset, p.seed, p.basis);
        out.h = clamp(out.h + (n - 0.5) * 0.35, 0.02, 1);
        return dIn - (n - 0.45) * p.amp;
      };
    },
  },

  Fbm: {
    title: 'fBm Detail', cat: 'Noise', color: '#8a63ff',
    desc: 'Rolling multifrequency detail. Low amp = texture, high amp = hills.',
    inputs: [{ id: 'in', label: 'SDF in' }], outputs: [{ id: 'f', label: 'SDF' }],
    params: [
      { id: 'amp', label: 'Amplitude', type: 'float', min: 0, max: 0.8, step: 0.005, def: 0.12 },
      { id: 'freq', label: 'Frequency', type: 'float', min: 0.5, max: 24, step: 0.1, def: 6.0 },
      { id: 'oct', label: 'Octaves', type: 'int', min: 1, max: 8, step: 1, def: 4 },
      { id: 'gain', label: 'Gain', type: 'float', min: 0.2, max: 0.8, step: 0.01, def: 0.5 },
      { id: 'basis', label: 'Basis', type: 'choice', options: ['Value', 'Perlin', 'Simplex'], def: 2 },
      { id: 'seed', label: 'Seed', type: 'seed', def: 9001 },
    ],
    make(p) {
      return (x, y, z, out, input) => {
        const dIn = input ? input(x, y, z, out) : y;
        const n = fbm3(x * p.freq, y * p.freq, z * p.freq, p.oct, 2.02, p.gain, p.seed, p.basis);
        return dIn - n * p.amp;
      };
    },
  },

  Billow: {
    title: 'Billow Noise', cat: 'Noise', color: '#8a63ff',
    desc: 'Billowy abs-fBm: dunes, clouds-like rolling masses, badlands puffs.',
    inputs: [{ id: 'in', label: 'SDF in' }], outputs: [{ id: 'f', label: 'SDF' }],
    params: [
      { id: 'amp', label: 'Amplitude', type: 'float', min: 0, max: 0.8, step: 0.005, def: 0.16 },
      { id: 'freq', label: 'Frequency', type: 'float', min: 0.4, max: 16, step: 0.1, def: 3.0 },
      { id: 'oct', label: 'Octaves', type: 'int', min: 1, max: 8, step: 1, def: 4 },
      { id: 'basis', label: 'Basis', type: 'choice', options: ['Value', 'Perlin', 'Simplex'], def: 0 },
      { id: 'seed', label: 'Seed', type: 'seed', def: 5150 },
    ],
    make(p) {
      return (x, y, z, out, input) => {
        const dIn = input ? input(x, y, z, out) : y;
        const n = billow3(x * p.freq, y * p.freq * 0.55, z * p.freq, p.oct, 2.0, 0.55, p.seed, p.basis);
        return dIn - (n - 0.42) * p.amp;
      };
    },
  },

  Terrace: {
    title: 'Terrace', cat: 'Shape', color: '#2fbf71',
    desc: 'Quantize SDF iso-bands into strata benches / rice-terrace steps.',
    inputs: [{ id: 'in', label: 'SDF in' }], outputs: [{ id: 'f', label: 'SDF' }],
    params: [
      { id: 'steps', label: 'Steps', type: 'int', min: 2, max: 24, step: 1, def: 7 },
      { id: 'sharp', label: 'Sharpness', type: 'float', min: 0.5, max: 8, step: 0.1, def: 3.2 },
      { id: 'amt', label: 'Amount', type: 'float', min: 0, max: 1, step: 0.01, def: 0.65 },
      { id: 'band', label: 'Band width', type: 'float', min: 0.05, max: 1, step: 0.01, def: 0.5 },
    ],
    make(p) {
      return (x, y, z, out, input) => {
        const dIn = input ? input(x, y, z, out) : y;
        const near = clamp(1 - Math.abs(dIn) / p.band, 0, 1);
        if (near <= 0) return dIn;
        const t = terrace(clamp(0.5 - dIn / (p.band * 2), 0, 1), p.steps, p.sharp);
        const dT = (0.5 - t) * p.band * 2;
        return lerp(dIn, dT, p.amt * near);
      };
    },
  },

  DomainWarp: {
    title: 'Domain Warp', cat: 'Shape', color: '#2fbf71',
    desc: 'Warp evaluation space of the upstream field. Swirls ridges, bends strata.',
    inputs: [{ id: 'in', label: 'SDF in' }], outputs: [{ id: 'f', label: 'SDF' }],
    params: [
      { id: 'amp', label: 'Amplitude', type: 'float', min: 0, max: 0.8, step: 0.005, def: 0.16 },
      { id: 'freq', label: 'Frequency', type: 'float', min: 0.3, max: 10, step: 0.05, def: 1.6 },
      { id: 'basis', label: 'Basis', type: 'choice', options: ['Value', 'Perlin', 'Simplex'], def: 1 },
      { id: 'seed', label: 'Seed', type: 'seed', def: 777 },
    ],
    make(p) {
      const w = { x: 0, y: 0, z: 0 };
      return (x, y, z, out, input) => {
        if (!input) return y;
        warpOffset(x, y, z, p.amp, p.freq, p.seed, p.basis, w);
        return input(x + w.x, y + w.y, z + w.z, out);
      };
    },
  },

  VoronoiCrack: {
    title: 'Voronoi Fracture', cat: 'Shape', color: '#2fbf71',
    desc: 'Carve polygonal fracture/canyon networks (F2-F1 edges) into the SDF.',
    inputs: [{ id: 'in', label: 'SDF in' }], outputs: [{ id: 'f', label: 'SDF' }],
    params: [
      { id: 'depth', label: 'Depth', type: 'float', min: 0, max: 0.6, step: 0.005, def: 0.14 },
      { id: 'freq', label: 'Frequency', type: 'float', min: 0.5, max: 12, step: 0.1, def: 3.2 },
      { id: 'width', label: 'Crack width', type: 'float', min: 0.01, max: 0.4, step: 0.005, def: 0.09 },
      { id: 'jitter', label: 'Jitter', type: 'float', min: 0, max: 1, step: 0.01, def: 0.85 },
      { id: 'seed', label: 'Seed', type: 'seed', def: 31337 },
    ],
    make(p) {
      const vv = { f1: 0, f2: 0 };
      return (x, y, z, out, input) => {
        const dIn = input ? input(x, y, z, out) : y;
        voronoi3(x * p.freq, y * p.freq * 0.35, z * p.freq, p.seed, p.jitter, vv);
        const edge = clamp(1 - (vv.f2 - vv.f1) / p.width, 0, 1);
        const gate = clamp(1 - Math.abs(dIn) / 0.45, 0, 1);
        out.h = clamp(out.h - edge * 0.25 * gate, 0.02, 1); // fractured rock is weak
        return dIn + edge * edge * p.depth * gate;
      };
    },
  },

  CaveWorm: {
    title: 'Cave Worm', cat: 'Shape', color: '#2fbf71',
    desc: 'Subtract winding tunnels/caves. Proof we are volumetric, not a heightmap.',
    inputs: [{ id: 'in', label: 'SDF in' }], outputs: [{ id: 'f', label: 'SDF' }],
    params: [
      { id: 'radius', label: 'Radius', type: 'float', min: 0.01, max: 0.3, step: 0.005, def: 0.07 },
      { id: 'freq', label: 'Wind freq', type: 'float', min: 0.2, max: 6, step: 0.05, def: 1.4 },
      { id: 'level', label: 'Level (y)', type: 'float', min: -0.8, max: 0.5, step: 0.01, def: -0.12 },
      { id: 'seed', label: 'Seed', type: 'seed', def: 60606 },
      { id: 'count', label: 'Worms', type: 'int', min: 1, max: 4, step: 1, def: 2 },
    ],
    make(p) {
      return (x, y, z, out, input) => {
        const dIn = input ? input(x, y, z, out) : y;
        let d = dIn;
        for (let wI = 0; wI < p.count; wI++) {
          const s = p.seed + wI * 977;
          const wind = fbm3(x * p.freq + wI * 9.1, p.level * 3, z * p.freq, 3, 2.1, 0.5, s, 1);
          const wind2 = fbm3(x * p.freq, p.level * 3 + wI * 4.7, z * p.freq + 3.3, 3, 2.1, 0.5, s + 5, 1);
          // tunnel centerline snakes in x/z at ~level
          const ang = wind * 2.2 + wI * 2.4;
          const cxLine = Math.sin(ang * 0.7 + x * 0.4) * 0.5;
          const czLine = Math.cos(ang * 0.6 + z * 0.4) * 0.5;
          const cy = p.level + wind2 * 0.12;
          const dd = Math.sqrt((x - cxLine * 0.4 - wI * 0.22 + 0.2) ** 2 + (y - cy) ** 2 * 1.7 + (z - czLine * 0.4) ** 2);
          const tunnel = dd - p.radius * (0.75 + 0.5 * (0.5 + 0.5 * wind));
          d = smax(d, -(tunnel), 0.02);
        }
        return d;
      };
    },
  },

  SdfPrimitive: {
    title: 'SDF Primitive', cat: 'Shape', color: '#2fbf71',
    desc: 'Add or subtract analytic hero shapes (mesa, sea stack, arch cutter…).',
    inputs: [{ id: 'in', label: 'SDF in' }], outputs: [{ id: 'f', label: 'SDF' }],
    params: [
      { id: 'shape', label: 'Shape', type: 'choice', options: ['Sphere', 'Box', 'Capsule', 'Torus'], def: 0 },
      { id: 'mode', label: 'Mode', type: 'choice', options: ['Add', 'Subtract', 'S-Add', 'S-Subtract'], def: 0 },
      { id: 'px', label: 'Pos X', type: 'float', min: -1, max: 1, step: 0.01, def: 0.35 },
      { id: 'py', label: 'Pos Y', type: 'float', min: -1, max: 1, step: 0.01, def: 0.1 },
      { id: 'pz', label: 'Pos Z', type: 'float', min: -1, max: 1, step: 0.01, def: 0.2 },
      { id: 's0', label: 'Size 0', type: 'float', min: 0.02, max: 1, step: 0.01, def: 0.22 },
      { id: 's1', label: 'Size 1', type: 'float', min: 0.02, max: 1, step: 0.01, def: 0.12 },
      { id: 'polish', label: 'Smooth k', type: 'float', min: 0.005, max: 0.4, step: 0.005, def: 0.06 },
      { id: 'hard', label: 'Hardness', type: 'float', min: 0.02, max: 1, step: 0.01, def: 0.7 },
    ],
    make(p) {
      const prim = (x, y, z) => {
        const ox = x - p.px, oy = y - p.py, oz = z - p.pz;
        if (p.shape === 1) { // box
          const qx = Math.abs(ox) - p.s0, qy = Math.abs(oy) - p.s1, qz = Math.abs(oz) - p.s0;
          const ax = Math.max(qx, 0), ay = Math.max(qy, 0), az = Math.max(qz, 0);
          return Math.sqrt(ax * ax + ay * ay + az * az) + Math.min(Math.max(qx, Math.max(qy, qz)), 0);
        } else if (p.shape === 2) { // capsule along Y
          const py = clamp(oy, -p.s1, p.s1);
          return Math.sqrt(ox * ox + (oy - py) * (oy - py) + oz * oz) - p.s0;
        } else if (p.shape === 3) { // torus in XZ
          const qx = Math.sqrt(ox * ox + oz * oz) - p.s1;
          return Math.sqrt(qx * qx + oy * oy) - p.s0;
        }
        return Math.sqrt(ox * ox + oy * oy + oz * oz) - p.s0;
      };
      return (x, y, z, out, input) => {
        const dIn = input ? input(x, y, z, out) : y;
        const hIn = out.h, sIn = out.s;
        const dp = prim(x, y, z);
        let d;
        const m = p.mode;
        if (m === 0) d = Math.min(dIn, dp);
        else if (m === 1) d = Math.max(dIn, -dp);
        else if (m === 2) d = smin(dIn, dp, p.polish);
        else d = smax(dIn, -dp, p.polish);
        if ((m === 0 || m === 2) && dp < dIn) { out.h = p.hard; out.s = sIn; }
        else out.h = hIn;
        return d;
      };
    },
  },

  Combine: {
    title: 'Combine (A ○ B)', cat: 'Op', color: '#e0a63c',
    desc: 'Boolean SDF combine of two branches with optional smoothing.',
    inputs: [{ id: 'a', label: 'A' }, { id: 'b', label: 'B' }], outputs: [{ id: 'f', label: 'SDF' }],
    params: [
      { id: 'mode', label: 'Mode', type: 'choice', options: ['Union', 'Subtract A-B', 'Intersect', 'S-Union', 'S-Subtract'], def: 3 },
      { id: 'polish', label: 'Smooth k', type: 'float', min: 0.005, max: 0.4, step: 0.005, def: 0.05 },
    ],
    make(p, A, B) {
      return (x, y, z, out) => {
        const oA = { h: out.h, s: out.s, m: out.m }, oB = { h: out.h, s: out.s, m: out.m };
        const da = A ? A(x, y, z, oA) : 1e5;
        const db = B ? B(x, y, z, oB) : 1e5;
        let d;
        const m = p.mode;
        if (m === 0) d = Math.min(da, db);
        else if (m === 1) d = Math.max(da, -db);
        else if (m === 2) d = Math.max(da, db);
        else if (m === 3) d = smin(da, db, p.polish);
        else d = smax(da, -db, p.polish);
        // attributes follow the winning side
        if (m === 1 || m === 4 || m === 2) { out.h = oA.h; out.s = oA.s; out.m = oA.m; }
        else if (da <= db) { out.h = oA.h; out.s = oA.s; out.m = oA.m; }
        else { out.h = oB.h; out.s = oB.s; out.m = oB.m; }
        return d;
      };
    },
  },

  StrataBands: {
    title: 'Strata Bands', cat: 'Material', color: '#c86dd7',
    desc: 'Warped sedimentary layering: drives hardness + canyon striping in the shader.',
    inputs: [{ id: 'in', label: 'SDF in' }], outputs: [{ id: 'f', label: 'SDF' }],
    params: [
      { id: 'freq', label: 'Band freq', type: 'float', min: 1, max: 30, step: 0.5, def: 11 },
      { id: 'warp', label: 'Warp', type: 'float', min: 0, max: 2, step: 0.01, def: 0.7 },
      { id: 'contrast', label: 'Hard contrast', type: 'float', min: 0, max: 1.5, step: 0.01, def: 0.8 },
      { id: 'seed', label: 'Seed', type: 'seed', def: 2024 },
    ],
    make(p) {
      return (x, y, z, out, input) => {
        const dIn = input ? input(x, y, z, out) : y;
        const s = strataCoord(x, y, z, p.freq, p.warp, 2.4, p.seed, 1);
        out.s = s;
        out.h = rockHardness(x, y, z, p.seed, p.contrast);
        out.m = 0.5 + 0.5 * Math.sin(s * 6.2831);
        return dIn;
      };
    },
  },

  // -- emitters ------------------------------------------------------------------
  GlobalRain: {
    title: 'Rain: Global', cat: 'Erode', color: '#3cc8e0',
    desc: 'Uniform storm rain over the whole landmass.',
    inputs: [], outputs: [{ id: 'e', label: 'FX', fxtype: true }],
    params: [
      { id: 'rate', label: 'Rain rate', type: 'float', min: 0, max: 1, step: 0.01, def: 0.35 },
      { id: 'dropSize', label: 'Drop mass', type: 'float', min: 0.2, max: 3, step: 0.05, def: 1.0 },
    ],
    fx: 'rain',
  },
  PaintedRain: {
    title: 'Rain: Painted', cat: 'Erode', color: '#3cc8e0',
    desc: 'Rain falls where you paint the emitter mask with the brush.',
    inputs: [], outputs: [{ id: 'e', label: 'FX', fxtype: true }],
    params: [
      { id: 'rate', label: 'Rain rate', type: 'float', min: 0, max: 2, step: 0.01, def: 1.0 },
      { id: 'dropSize', label: 'Drop mass', type: 'float', min: 0.2, max: 3, step: 0.05, def: 1.0 },
    ],
    fx: 'paintedRain',
  },

  // -- erosion sims ----------------------------------------------------------------
  Hydraulic: {
    title: 'Erode: Hydraulic', cat: 'Erode', color: '#3cc8e0',
    desc: 'Ballistic rain → impact excavation → sediment transport → deposition. Pure SDF: carves craters, rills, gullies; mass-conserved.',
    inputs: [], outputs: [{ id: 'e', label: 'FX', fxtype: true }],
    params: [
      { id: 'enabled', label: 'Enabled', type: 'bool', def: true },
      { id: 'spawn', label: 'Spawn / frame', type: 'int', min: 50, max: 20000, step: 50, def: 2600 },
      { id: 'maxParts', label: 'Max particles', type: 'int', min: 1000, max: 120000, step: 1000, def: 42000 },
      { id: 'gravity', label: 'Gravity', type: 'float', min: 0.2, max: 6, step: 0.05, def: 2.2 },
      { id: 'excavate', label: 'Excavation', type: 'float', min: 0, max: 4, step: 0.05, def: 1.25 },
      { id: 'capacity', label: 'Transport cap.', type: 'float', min: 0, max: 4, step: 0.05, def: 1.0 },
      { id: 'deposit', label: 'Deposition', type: 'float', min: 0, max: 4, step: 0.05, def: 1.0 },
      { id: 'evap', label: 'Evaporation', type: 'float', min: 0, max: 1, step: 0.005, def: 0.06 },
      { id: 'infil', label: 'Infiltration', type: 'float', min: 0, max: 1, step: 0.005, def: 0.10 },
    ],
    fx: 'hydro',
  },
  Thermal: {
    title: 'Erode: Thermal', cat: 'Erode', color: '#e07b3c',
    desc: 'Talus relaxation WITHOUT blur: over-steep SDF voxels shed discrete mass downslope until angle of repose. Forms scree + talus cones.',
    inputs: [], outputs: [{ id: 'e', label: 'FX', fxtype: true }],
    params: [
      { id: 'enabled', label: 'Enabled', type: 'bool', def: true },
      { id: 'rate', label: 'Rate', type: 'float', min: 0, max: 3, step: 0.05, def: 0.8 },
      { id: 'repose', label: 'Repose angle°', type: 'float', min: 15, max: 75, step: 0.5, def: 34 },
      { id: 'budget', label: 'Voxels / frame', type: 'int', min: 500, max: 120000, step: 500, def: 26000 },
    ],
    fx: 'thermal',
  },
  Wind: {
    title: 'Erode: Aeolian', cat: 'Erode', color: '#b8e03c',
    desc: 'Wind abrasion + saltation + lee deposition with upwind sheltering. Yardangs, ventifacts, dune slip faces.',
    inputs: [], outputs: [{ id: 'e', label: 'FX', fxtype: true }],
    params: [
      { id: 'enabled', label: 'Enabled', type: 'bool', def: true },
      { id: 'speed', label: 'Wind speed', type: 'float', min: 0, max: 4, step: 0.05, def: 1.1 },
      { id: 'dir', label: 'Direction°', type: 'float', min: 0, max: 360, step: 1, def: 40 },
      { id: 'abrasion', label: 'Abrasion', type: 'float', min: 0, max: 3, step: 0.05, def: 0.9 },
      { id: 'gust', label: 'Turbulence', type: 'float', min: 0, max: 2, step: 0.05, def: 0.7 },
      { id: 'budget', label: 'Voxels / frame', type: 'int', min: 500, max: 120000, step: 500, def: 20000 },
    ],
    fx: 'wind',
  },
  Chemical: {
    title: 'Erode: Chemical', cat: 'Erode', color: '#7de06a',
    desc: 'Dissolution along wetness × concavity (karst) + evaporite precipitate on convex dry rock.',
    inputs: [], outputs: [{ id: 'e', label: 'FX', fxtype: true }],
    params: [
      { id: 'enabled', label: 'Enabled', type: 'bool', def: false },
      { id: 'rate', label: 'Dissolve rate', type: 'float', min: 0, max: 3, step: 0.05, def: 0.7 },
      { id: 'karst', label: 'Karst boost', type: 'float', min: 0, max: 3, step: 0.05, def: 1.2 },
      { id: 'budget', label: 'Voxels / frame', type: 'int', min: 500, max: 120000, step: 500, def: 20000 },
    ],
    fx: 'chem',
  },

  // -- material ---------------------------------------------------------------------
  SatShade: {
    title: 'SAT Shade', cat: 'Shade', color: '#d7c26d',
    desc: 'Procedural satellite-style surfacing: strata + biome + scree/snow + AO/curvature/flow masks. No downloaded textures.',
    inputs: [], outputs: [{ id: 'e', label: 'FX', fxtype: true }],
    params: [
      { id: 'palette', label: 'Biome', type: 'choice', options: ['Alpine', 'Canyon', 'Karst Jungle', 'Volcanic', 'Coastal'], def: 0 },
      { id: 'snow', label: 'Snowline', type: 'float', min: -1, max: 1.5, step: 0.01, def: 0.34 },
      { id: 'veg', label: 'Vegetation', type: 'float', min: 0, max: 1.5, step: 0.01, def: 0.8 },
      { id: 'strataAmt', label: 'Strata bands', type: 'float', min: 0, max: 1.5, step: 0.01, def: 0.7 },
      { id: 'aoAmt', label: 'AO strength', type: 'float', min: 0, max: 1.5, step: 0.01, def: 0.85 },
      { id: 'sunAz', label: 'Sun azimuth°', type: 'float', min: 0, max: 360, step: 1, def: 135 },
      { id: 'sunEl', label: 'Sun elev°', type: 'float', min: 2, max: 88, step: 0.5, def: 42 },
      { id: 'seaLevel', label: 'Sea level', type: 'float', min: -1.2, max: 0.6, step: 0.01, def: -0.28 },
      { id: 'showSea', label: 'Show sea', type: 'bool', def: true },
    ],
    fx: 'material',
  },

  Output: {
    title: 'Output', cat: 'IO', color: '#ffffff',
    desc: 'Terrain root. Connect the final SDF here.',
    inputs: [{ id: 'f', label: 'SDF' }], outputs: [],
    params: [],
    make() { return null; },
  },
};

export function defaultParams(type) {
  const def = NODE_DEFS[type];
  const p = {};
  for (const pr of def.params) p[pr.id] = pr.def;
  return p;
}

export function makeNode(type, x = 0, y = 0) {
  return { id: nid(), type, x, y, params: defaultParams(type) };
}

// ---------------------------------------------------------------------------
// Graph compilation
// ---------------------------------------------------------------------------
export function findInputWire(graph, toNode, toSocket) {
  return graph.wires.find((w) => w.to === toNode && w.toSock === toSocket);
}

export function buildFieldEval(graph) {
  const byId = new Map(graph.nodes.map((n) => [n.id, n]));
  const memo = new Map();
  const visiting = new Set();

  function inputEval(nodeId, sock) {
    const wire = findInputWire(graph, nodeId, sock);
    if (!wire) return null;
    return nodeEval(wire.from);
  }

  function nodeEval(nodeId) {
    if (memo.has(nodeId)) return memo.get(nodeId);
    if (visiting.has(nodeId)) throw new Error('Cycle in field graph at ' + nodeId);
    const node = byId.get(nodeId);
    if (!node) return null;
    const def = NODE_DEFS[node.type];
    if (!def || !def.make) { memo.set(nodeId, null); return null; }
    visiting.add(nodeId);
    let fn = null;
    try {
      if (node.type === 'Combine') {
        fn = def.make(node.params, inputEval(node.id, 'a'), inputEval(node.id, 'b'));
      } else if (def.inputs && def.inputs.length === 1 && def.inputs[0].id === 'in') {
        // single-input field op: pass upstream eval as `input`
        fn = def.make(node.params);
        const up = inputEval(node.id, 'in');
        const inner = fn;
        fn = (x, y, z, out) => inner(x, y, z, out, up);
      } else if (node.type === 'Output') {
        fn = inputEval(node.id, 'f');
      } else {
        fn = def.make(node.params); // source
      }
    } finally {
      visiting.delete(nodeId);
    }
    memo.set(nodeId, fn);
    return fn;
  }

  const outNode = graph.nodes.find((n) => n.type === 'Output');
  const fn = outNode ? nodeEval(outNode.id) : null;
  return fn || ((x, y, z, out) => { out.h = 0.5; out.s = 0.5; out.m = 0; return y; });
}

// Collect sim/material configuration from FX nodes in graph order.
export function collectFx(graph) {
  const fx = {
    rain: [], hydro: null, thermal: null, wind: null, chem: null, material: null,
  };
  for (const n of graph.nodes) {
    const def = NODE_DEFS[n.type];
    if (!def || !def.fx) continue;
    const p = { ...n.params };
    if (def.fx === 'rain' || def.fx === 'paintedRain') fx.rain.push({ kind: def.fx, params: p });
    else if (def.fx === 'hydro') fx.hydro = p;
    else if (def.fx === 'thermal') fx.thermal = p;
    else if (def.fx === 'wind') fx.wind = p;
    else if (def.fx === 'chem') fx.chem = p;
    else if (def.fx === 'material') fx.material = p;
  }
  if (!fx.material) fx.material = defaultParams('SatShade');
  return fx;
}

// ---------------------------------------------------------------------------
// Presets — full studio graphs
// ---------------------------------------------------------------------------
function chain(types, x0 = 60, y0 = 60, dx = 200) {
  const nodes = types.map((t, i) => {
    const n = makeNode(t, x0 + i * dx, y0);
    return n;
  });
  const wires = [];
  for (let i = 1; i < nodes.length; i++) {
    wires.push({ from: nodes[i - 1].id, fromSock: 'f', to: nodes[i].id, toSock: 'in' });
  }
  return { nodes, wires };
}

export function presetGraph(name) {
  _uid = 1;
  const g = { nodes: [], wires: [] };
  const addFxRow = (types, y) => {
    types.forEach((t, i) => {
      const n = makeNode(t, 60 + i * 200, y);
      g.nodes.push(n);
    });
  };

  if (name === 'alpine') {
    const c = chain(['IslandMask', 'MountainMF', 'Ridged', 'Fbm', 'StrataBands'], 60, 60);
    const out = makeNode('Output', 60 + 5 * 200, 60);
    c.nodes.push(out);
    c.wires.push({ from: c.nodes[4].id, fromSock: 'f', to: out.id, toSock: 'f' });
    Object.assign(c.nodes[1].params, { amp: 0.62, freq: 1.9, oct: 6, rough: 0.72, sharp: 1.7, seed: 1337 });
    Object.assign(c.nodes[2].params, { amp: 0.26, freq: 3.6, oct: 5, seed: 4242 });
    Object.assign(c.nodes[3].params, { amp: 0.07, freq: 9.0, oct: 4, seed: 9001 });
    g.nodes.push(...c.nodes); g.wires.push(...c.wires);
    addFxRow(['GlobalRain', 'PaintedRain', 'Hydraulic', 'Thermal', 'Wind'], 300);
    const sat = makeNode('SatShade', 60, 480);
    Object.assign(sat.params, { palette: 0, snow: 0.30, veg: 0.85, sunEl: 44 });
    g.nodes.push(sat);
  } else if (name === 'canyon') {
    const c = chain(['IslandMask', 'Fbm', 'Terrace', 'VoronoiCrack', 'StrataBands'], 60, 60);
    const out = makeNode('Output', 60 + 5 * 200, 60);
    c.nodes.push(out);
    c.wires.push({ from: c.nodes[4].id, fromSock: 'f', to: out.id, toSock: 'f' });
    Object.assign(c.nodes[0].params, { radius: 1.0, seaLevel: -0.5, edge: 0.5 });
    Object.assign(c.nodes[1].params, { amp: 0.35, freq: 1.6, oct: 5, seed: 1103 });
    Object.assign(c.nodes[2].params, { steps: 9, sharp: 4.2, amt: 0.8 });
    Object.assign(c.nodes[3].params, { depth: 0.22, freq: 2.6, width: 0.07, seed: 777 });
    Object.assign(c.nodes[4].params, { freq: 16, warp: 0.9, contrast: 1.0 });
    g.nodes.push(...c.nodes); g.wires.push(...c.wires);
    addFxRow(['GlobalRain', 'PaintedRain', 'Hydraulic', 'Thermal', 'Wind'], 300);
    g.nodes.find((n) => n.type === 'GlobalRain').params.rate = 0.12;
    g.nodes.find((n) => n.type === 'Wind').params.speed = 1.8;
    const sat = makeNode('SatShade', 60, 480);
    Object.assign(sat.params, { palette: 1, snow: 1.4, veg: 0.25, strataAmt: 1.2, sunEl: 38, seaLevel: -0.5 });
    g.nodes.push(sat);
  } else if (name === 'karst') {
    const c = chain(['IslandMask', 'Billow', 'VoronoiCrack', 'CaveWorm', 'StrataBands'], 60, 60);
    const out = makeNode('Output', 60 + 5 * 200, 60);
    c.nodes.push(out);
    c.wires.push({ from: c.nodes[4].id, fromSock: 'f', to: out.id, toSock: 'f' });
    Object.assign(c.nodes[1].params, { amp: 0.34, freq: 2.2, oct: 5, seed: 5150 });
    Object.assign(c.nodes[2].params, { depth: 0.2, freq: 3.4, width: 0.1, seed: 999 });
    Object.assign(c.nodes[3].params, { radius: 0.09, level: -0.1, count: 2 });
    g.nodes.push(...c.nodes); g.wires.push(...c.wires);
    addFxRow(['GlobalRain', 'PaintedRain', 'Hydraulic', 'Chemical', 'Thermal'], 300);
    g.nodes.find((n) => n.type === 'GlobalRain').params.rate = 0.55;
    g.nodes.find((n) => n.type === 'Chemical').params.enabled = true;
    const sat = makeNode('SatShade', 60, 480);
    Object.assign(sat.params, { palette: 2, snow: 1.4, veg: 1.25, sunEl: 55 });
    g.nodes.push(sat);
  } else if (name === 'volcanic') {
    // cone = SDF primitive + mountain noise combined
    const isl = makeNode('IslandMask', 60, 60);
    Object.assign(isl.params, { radius: 0.7, seaLevel: -0.3 });
    const cone = makeNode('SdfPrimitive', 260, -40);
    Object.assign(cone.params, { shape: 0, mode: 0, px: 0, py: -0.1, pz: 0, s0: 0.5, hard: 0.75 });
    const mtn = makeNode('MountainMF', 260, 160);
    Object.assign(mtn.params, { amp: 0.4, freq: 2.6, oct: 5, rough: 0.85, seed: 555 });
    const cmb = makeNode('Combine', 460, 60);
    const crater = makeNode('SdfPrimitive', 660, 60);
    Object.assign(crater.params, { shape: 0, mode: 3, px: 0, py: 0.32, pz: 0, s0: 0.16, polish: 0.05 });
    const det = makeNode('Fbm', 860, 60);
    Object.assign(det.params, { amp: 0.06, freq: 10, oct: 3 });
    const out = makeNode('Output', 1060, 60);
    g.nodes.push(isl, cone, mtn, cmb, crater, det, out);
    g.wires.push(
      { from: isl.id, fromSock: 'f', to: mtn.id, toSock: 'in' },
      { from: cone.id, fromSock: 'f', to: cmb.id, toSock: 'a' },
      { from: mtn.id, fromSock: 'f', to: cmb.id, toSock: 'b' },
      { from: cmb.id, fromSock: 'f', to: crater.id, toSock: 'in' },
      { from: crater.id, fromSock: 'f', to: det.id, toSock: 'in' },
      { from: det.id, fromSock: 'f', to: out.id, toSock: 'f' },
    );
    addFxRow(['GlobalRain', 'PaintedRain', 'Hydraulic', 'Thermal', 'Wind'], 300);
    const sat = makeNode('SatShade', 60, 480);
    Object.assign(sat.params, { palette: 3, snow: 1.4, veg: 0.45, sunEl: 36 });
    g.nodes.push(sat);
  } else { // coastal
    const c = chain(['IslandMask', 'Fbm', 'Billow', 'DomainWarp', 'StrataBands'], 60, 60);
    const out = makeNode('Output', 60 + 5 * 200, 60);
    c.nodes.push(out);
    c.wires.push({ from: c.nodes[4].id, fromSock: 'f', to: out.id, toSock: 'f' });
    Object.assign(c.nodes[0].params, { radius: 0.8, seaLevel: -0.22, edge: 0.42 });
    Object.assign(c.nodes[1].params, { amp: 0.16, freq: 2.4, oct: 5, seed: 21 });
    Object.assign(c.nodes[2].params, { amp: 0.14, freq: 3.4, oct: 4, seed: 22 });
    Object.assign(c.nodes[3].params, { amp: 0.2, freq: 1.4 });
    g.nodes.push(...c.nodes); g.wires.push(...c.wires);
    addFxRow(['GlobalRain', 'PaintedRain', 'Hydraulic', 'Thermal', 'Wind'], 300);
    const sat = makeNode('SatShade', 60, 480);
    Object.assign(sat.params, { palette: 4, snow: 1.4, veg: 1.0, sunEl: 50 });
    g.nodes.push(sat);
  }
  return g;
}

export const PRESETS = [
  { id: 'alpine', name: 'Alpine Ridge' },
  { id: 'canyon', name: 'Desert Canyon' },
  { id: 'karst', name: 'Karst Towers' },
  { id: 'volcanic', name: 'Volcanic Island' },
  { id: 'coastal', name: 'Coastal Cliffs' },
];

// Validate + sanitize an imported graph.
export function sanitizeGraph(g) {
  const nodes = [];
  for (const n of g.nodes || []) {
    if (!NODE_DEFS[n.type]) continue;
    const params = { ...defaultParams(n.type), ...(n.params || {}) };
    nodes.push({ id: String(n.id), type: n.type, x: +n.x || 0, y: +n.y || 0, params });
  }
  const ids = new Set(nodes.map((n) => n.id));
  const wires = [];
  for (const w of g.wires || []) {
    if (ids.has(w.from) && ids.has(w.to)) {
      wires.push({ from: String(w.from), fromSock: String(w.fromSock), to: String(w.to), toSock: String(w.toSock) });
    }
  }
  // fix uid counter
  let max = 0;
  for (const id of ids) {
    const m = /^n(\d+)$/.exec(id);
    if (m) max = Math.max(max, +m[1]);
  }
  _uid = max + 1;
  return { nodes, wires };
}

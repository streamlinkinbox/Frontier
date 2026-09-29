// ---------------------------------------------------------------------------
// Frontier worker / node evaluation
// Runs entirely off the main thread. Data types flowing between nodes:
//   world  : { domain }
//   height : HeightField
//   sdf    : SDFState { ground, void, smoothK }   (void stays separate!)
//   void   : SDFVolume (negative inside the void)
//   mask   : Float32Array nx*nz in [0,1]
//   layer  : { mask, color, roughness, name }
//   splat  : SplatSet
// ---------------------------------------------------------------------------

import { SDFVolume } from '../core/volume';
import { applyHeight, extractHeight, HeightField, makeHeightField } from '../core/heightfield';
import { reinitBand } from '../core/reinit';
import { meshVolume, MeshData } from '../core/mesh';
import { fbm2, perlin2, valueNoise2, simplex2, ridgedMultifractal2, badlands2, voronoi2, hashInt } from '../core/noise';
import { hydraulicErosion, HYDRAULIC_DEFAULTS } from '../core/erosion/hydraulic';
import { thermalErosion, strataHardness } from '../core/erosion/thermal';
import { alluvialDeposition } from '../core/erosion/alluvial';
import { windErosion } from '../core/erosion/wind';
import { computeFlow, fillPits, FlowField, accumulationMask } from '../core/erosion/flow';
import { buildCaveVolume, carvePrimitive } from '../core/caves';
import { buildMask, applyRange, packSplatmaps, MaskKind, SplatLayer, SplatSet } from '../core/splat';
import { Graph, GraphNode, ParamValue } from '../graph/types';
import { defFor } from '../graph/registry';
import { topoOrder } from '../graph/graph';

export interface SDFState {
  ground: SDFVolume;
  void: SDFVolume | null;
  smoothK: number;
}

export interface LayerData { mask: Float32Array; color: [number, number, number]; roughness: number; name: string }

export interface CookContext {
  domain: { size: [number, number, number]; res: number; seed: number };
  flowCache: WeakMap<Float32Array, FlowField>;
  cancelled: () => boolean;
  progress: (frac: number, stage: string) => void;
}

type Values = Record<string, unknown>;

const num = (p: Record<string, ParamValue>, k: string, d: number): number => {
  const v = p[k];
  return typeof v === 'number' ? v : d;
};
const str = (p: Record<string, ParamValue>, k: string, d: string): string => {
  const v = p[k];
  return typeof v === 'string' ? v : d;
};
const bool = (p: Record<string, ParamValue>, k: string, d: boolean): boolean => {
  const v = p[k];
  return typeof v === 'boolean' ? v : d;
};

function genHeight(kind: string, params: Record<string, ParamValue>, ctx: CookContext): HeightField {
  const { size, res, seed } = ctx.domain;
  const h = size[0] / res;
  const hf = makeHeightField(res, res, h);
  const base = num(params, 'base', 40);
  const amp = num(params, 'amplitude', 120);
  const scale = num(params, 'scale', 420);
  const oct = num(params, 'octaves', 5);
  const lac = num(params, 'lacunarity', 2.05);
  const gain = num(params, 'gain', 0.5);
  const sharp = num(params, 'sharpness', 2.2);
  const terrace = num(params, 'terrace', 0.4);
  const jitter = num(params, 'jitter', 0.9);
  const mode = str(params, 'mode', 'F2-F1');
  const so = num(params, 'seedOffset', 0);
  const s = seed + so * 977;
  const o = { octaves: oct, lacunarity: lac, gain, frequency: 1, seed: s };

  for (let z = 0; z < res; z++) {
    const wz = z * h;
    for (let x = 0; x < res; x++) {
      const wx = x * h;
      let v = 0;
      switch (kind) {
        case 'simplex': v = fbm2(wx, wz, { ...o, frequency: 1 / scale }); break;
        case 'perlin': {
          let a = 1, fq = 1 / scale, sum = 0, nrm = 0;
          for (let i = 0; i < oct; i++) { sum += a * perlin2(wx * fq, wz * fq, s + i * 1013); nrm += a; a *= gain; fq *= lac; }
          v = sum / nrm;
          break;
        }
        case 'value': {
          let a = 1, fq = 1 / scale, sum = 0, nrm = 0;
          for (let i = 0; i < oct; i++) { sum += a * valueNoise2(wx * fq, wz * fq, s + i * 1013); nrm += a; a *= gain; fq *= lac; }
          v = sum / nrm;
          break;
        }
        case 'white': v = hashInt(x, z, 3, s) * 2 - 1; break;
        case 'multifractal': v = ridgedMultifractal2(wx / scale, wz / scale, o, 1, sharp) * 2 - 1; break;
        case 'badlands': v = badlands2(wx / scale, wz / scale, o, terrace) * 2 - 1; break;
        case 'voronoi': {
          const c = voronoi2(wx / scale, wz / scale, s, jitter);
          v = mode === 'F1' ? 1 - Math.min(1, c.f1) : mode === 'F2' ? 1 - Math.min(1, c.f2) : Math.min(1, (c.f2 - c.f1) * 2);
          v = v * 2 - 1;
          break;
        }
      }
      hf.data[z * res + x] = base + amp * 0.5 * (v + (kind === 'multifractal' || kind === 'badlands' ? 1 : 0)) * (kind === 'multifractal' || kind === 'badlands' ? 0.5 : 1);
    }
  }
  return hf;
}

function flowFor(hf: HeightField, ctx: CookContext): FlowField {
  let f = ctx.flowCache.get(hf.data);
  if (!f) {
    const filled = fillPits(hf);
    f = computeFlow(hf, filled);
    ctx.flowCache.set(hf.data, f);
  }
  return f;
}

export function evalNode(
  node: GraphNode,
  inputs: Record<string, unknown>,
  ctx: CookContext,
): Values {
  const p = node.params;
  const def = defFor(node.type);
  void def;

  switch (node.type) {
    case 'start':
      return { World: { domain: ctx.domain } };

    case 'simplex':
    case 'perlin':
    case 'value':
    case 'white':
    case 'multifractal':
    case 'badlands':
    case 'voronoi':
      return { Height: genHeight(node.type, p, ctx) };

    case 'mix': {
      const A = inputs.A as HeightField | undefined;
      const B = inputs.B as HeightField | undefined;
      if (!A) return {};
      const out = makeHeightField(A.nx, A.nz, A.h);
      const op = str(p, 'op', 'mix');
      const w = num(p, 'weight', 0.5);
      for (let i = 0; i < out.data.length; i++) {
        const a = A.data[i];
        const b = B ? B.data[i] : 0;
        let v = a;
        if (op === 'add') v = a + b;
        else if (op === 'subtract') v = a - b;
        else if (op === 'multiply') v = a * b;
        else if (op === 'max') v = Math.max(a, b);
        else if (op === 'min') v = Math.min(a, b);
        else v = a * (1 - w) + b * w;
        out.data[i] = v;
      }
      return { Height: out };
    }

    case 'lift': {
      const hf = inputs.Height as HeightField | undefined;
      if (!hf) return {};
      const vol = SDFVolume.fromDomain(ctx.domain, ctx.domain.size[1] / ctx.domain.size[0]);
      applyHeight(vol, hf);
      return { SDF: { ground: vol, void: null, smoothK: 0 } as SDFState };
    }

    case 'cave': {
      const probe = SDFVolume.fromDomain(ctx.domain, ctx.domain.size[1] / ctx.domain.size[0]);
      const caves = buildCaveVolume(probe, {
        seed: ctx.domain.seed + num(p, 'seedOffset', 0) * 977,
        tunnelScale: num(p, 'tunnelScale', 90),
        tunnelRadius: num(p, 'tunnelRadius', 9),
        worminess: num(p, 'worminess', 0.55),
        depthBias: num(p, 'depthBias', 0.55),
        coverage: num(p, 'coverage', 0.5),
        verticality: num(p, 'verticality', 0.25),
      });
      return { Void: caves };
    }

    case 'carveSphere':
    case 'carveBox': {
      const [sx, sy, sz] = ctx.domain.size;
      const probe = SDFVolume.fromDomain(ctx.domain, ctx.domain.size[1] / ctx.domain.size[0]);
      if (node.type === 'carveSphere') {
        const r = num(p, 'r', 60);
        return { Void: carvePrimitive(probe, 'sphere', num(p, 'cx', 0.5) * sx, num(p, 'cy', 0.25) * sy, num(p, 'cz', 0.5) * sz, [r, r, r]) };
      }
      return {
        Void: carvePrimitive(probe, 'box', num(p, 'cx', 0.5) * sx, num(p, 'cy', 0.2) * sy, num(p, 'cz', 0.5) * sz,
          [num(p, 'hx', 80), num(p, 'hy', 20), num(p, 'hz', 80)]),
      };
    }

    case 'unionVoid': {
      const A = inputs.A as SDFVolume | undefined;
      const B = inputs.B as SDFVolume | undefined;
      if (!A) return {};
      if (!B) return { Void: A };
      const out = A.clone();
      for (let i = 0; i < out.data.length; i++) out.data[i] = Math.min(out.data[i], B.data[i]);
      return { Void: out };
    }

    case 'carve': {
      const state = inputs.SDF as SDFState | undefined;
      const v = inputs.Void as SDFVolume | undefined;
      if (!state) return {};
      const next: SDFState = { ground: state.ground, void: state.void ? state.void.clone() : null, smoothK: num(p, 'smooth', 0) };
      if (v) {
        if (next.void) {
          for (let i = 0; i < next.void.data.length; i++) next.void.data[i] = Math.min(next.void.data[i], v.data[i]);
        } else {
          next.void = v.clone();
        }
      }
      return { SDF: next };
    }

    case 'hydraulic':
    case 'thermal':
    case 'alluvial':
    case 'wind': {
      const state = inputs.SDF as SDFState | undefined;
      if (!state) return {};
      const ground = state.ground.clone();
      let hf = extractHeight(ground);
      const out: Values = {};

      if (node.type === 'hydraulic') {
        hf = hydraulicErosion(hf, {
          iterations: num(p, 'iterations', HYDRAULIC_DEFAULTS.iterations),
          rainfall: num(p, 'rainfall', 1),
          erodibility: num(p, 'erodibility', 0.55),
          capacityKc: num(p, 'capacityKc', 0.9),
          expM: num(p, 'expM', 0.5),
          depositRate: num(p, 'depositRate', 0.35),
          maxErodeStep: 0.6,
          channelWidth: num(p, 'channelWidth', 14),
          talusMix: num(p, 'talusMix', 0.15),
        }, (f, s) => ctx.progress(f, s));
        applyHeight(ground, hf);
        const flow = flowFor(hf, ctx);
        out.Flow = accumulationMask(flow);
      } else if (node.type === 'thermal') {
        let hardness: Float32Array | null = null;
        const maskIn = inputs.Hardness as Float32Array | undefined;
        if (maskIn) hardness = maskIn;
        else if (bool(p, 'useStrata', true)) hardness = strataHardness(hf, num(p, 'strataBand', 26), num(p, 'hardnessContrast', 0.8), ctx.domain.seed);
        hf = thermalErosion(hf, {
          iterations: num(p, 'iterations', 30),
          talusAngle: num(p, 'talusAngle', 37),
          strength: num(p, 'strength', 0.6),
          creep: num(p, 'creep', 0.05),
          hardnessContrast: num(p, 'hardnessContrast', 0.8),
        }, hardness, (f, s) => ctx.progress(f, s));
        applyHeight(ground, hf);
      } else if (node.type === 'alluvial') {
        hf = alluvialDeposition(hf, {
          iterations: num(p, 'iterations', 14),
          supply: num(p, 'supply', 0.5),
          slopeThreshold: num(p, 'slopeThreshold', 0.18),
          fanStrength: num(p, 'fanStrength', 0.7),
          maxDepositStep: 0.35,
          apron: num(p, 'apron', 0.6),
        }, (f, s) => ctx.progress(f, s));
        applyHeight(ground, hf);
      } else {
        hf = windErosion(hf, {
          iterations: num(p, 'iterations', 18),
          directionDeg: num(p, 'directionDeg', 235),
          strength: num(p, 'strength', 0.6),
          abrasion: num(p, 'abrasion', 0.5),
          deposition: num(p, 'deposition', 0.45),
          shelterRange: num(p, 'shelterRange', 60),
          turbulence: num(p, 'turbulence', 0.35),
        }, ctx.domain.seed, (f, s) => ctx.progress(f, s));
        applyHeight(ground, hf);
      }
      out.SDF = { ground, void: state.void, smoothK: state.smoothK } as SDFState;
      return out;
    }

    case 'mask': {
      const state = inputs.SDF as SDFState | undefined;
      if (!state) return {};
      const hf = extractHeight(state.ground);
      const kind = str(p, 'kind', 'slope') as MaskKind;
      const flowIn = inputs.Flow as Float32Array | undefined;
      const range = { lo: num(p, 'lo', 0.1), hi: num(p, 'hi', 0.6), feather: num(p, 'feather', 0.1) };
      if (kind === 'flow' && flowIn) return { Mask: applyRange(flowIn, range) };
      const flow = (kind === 'flow' || kind === 'moisture') ? flowFor(hf, ctx) : null;
      const mask = buildMask(kind, { hf, flow, ground: state.ground }, range, num(p, 'strataBand', 24));
      return { Mask: mask };
    }

    case 'layer': {
      const mask = inputs.Mask as Float32Array | undefined;
      if (!mask) return {};
      const col = (p.color ?? [0.5, 0.5, 0.5]) as [number, number, number];
      return {
        Layer: {
          mask,
          color: col,
          roughness: num(p, 'roughness', 0.9),
          name: str(p, 'name', 'Base'),
        } as LayerData,
      };
    }

    case 'splat': {
      const layers: SplatLayer[] = [];
      for (const port of ['Base', 'L2', 'L3', 'L4', 'L5', 'L6', 'L7', 'L8']) {
        const l = inputs[port] as LayerData | undefined;
        if (l) layers.push(l);
      }
      if (layers.length === 0) return {};
      const side = Math.round(Math.sqrt(layers[0].mask.length));
      const set = packSplatmaps(layers, side, side);
      return { Splat: set };
    }

    case 'output': {
      const state = inputs.SDF as SDFState | undefined;
      if (!state) return {};
      let combined = state.ground;
      if (state.void) {
        combined = state.ground.clone();
        const v = state.void.data;
        const k = state.smoothK;
        for (let i = 0; i < combined.data.length; i++) {
          const g = combined.data[i];
          const nv = -v[i];
          combined.data[i] = k > 0 ? smoothSub(g, nv, k) : Math.max(g, nv);
        }
        combined.saturate();
        if (k > 0) reinitBand(combined, 1.5, 1);
      }
      ctx.progress(0.9, 'meshing');
      const mesh = meshVolume(combined, 1 / num(p, 'uvScale', 256));
      return { __mesh: mesh, __splat: (inputs.Splat as SplatSet | undefined) ?? null, __state: state };
    }

    default:
      return {};
  }
}

function smoothSub(a: number, b: number, k: number): number {
  const h = Math.max(0, Math.min(1, 0.5 - 0.5 * (a + b) / k));
  return b - (a + b) * h + k * h * (1 - h);
}

export interface CookResult {
  mesh: MeshData | null;
  splat: SplatSet | null;
  layerColors: [number, number, number][];
  layerRough: number[];
  stats: { triangles: number; cookMs: number; bandVoxels: number; res: number; h: number };
  wireframe: boolean;
}

export function cookGraph(graph: Graph, progress: (f: number, s: string) => void, cancelled: () => boolean): CookResult | null {
  const t0 = performance.now();
  const order = topoOrder(graph);
  if (!order) return null;
  const ctx: CookContext = {
    domain: graph.domain,
    flowCache: new WeakMap(),
    cancelled,
    progress,
  };
  const values = new Map<string, Values>();
  let result: CookResult | null = null;
  const total = Math.max(1, order.length);

  for (let i = 0; i < order.length; i++) {
    if (cancelled()) return null;
    const node = order[i];
    progress(i / total, defFor(node.type).label);
    const inputs: Record<string, unknown> = {};
    if (!node.muted) {
      for (const e of graph.edges.filter((e) => e.to === node.id)) {
        const src = values.get(e.from);
        if (src && src[e.fromPort] !== undefined) inputs[e.toPort] = src[e.fromPort];
      }
    }
    const out = node.muted ? {} : evalNode(node, inputs, ctx);
    values.set(node.id, out);
    if (node.type === 'output' && out.__mesh) {
      const mesh = out.__mesh as MeshData;
      const splat = (out.__splat as SplatSet | null) ?? null;
      result = {
        mesh,
        splat,
        layerColors: splat ? splat.layers.map((l) => l.color) : [],
        layerRough: splat ? splat.layers.map((l) => l.roughness) : [],
        stats: {
          triangles: mesh.triangleCount,
          cookMs: performance.now() - t0,
          bandVoxels: (out.__state as SDFState).ground.bandVoxelCount(),
          res: graph.domain.res,
          h: graph.domain.size[0] / graph.domain.res,
        },
        wireframe: node.params.wireframe === true,
      };
    }
  }
  if (result) result.stats.cookMs = performance.now() - t0;
  return result;
}

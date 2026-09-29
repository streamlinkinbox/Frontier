// ---------------------------------------------------------------------------
// Frontier graph / node registry
// Metadata only — evaluation lives in the cook worker (src/worker/eval.ts).
// Every node that can blur the distance field declares resRequirement();
// the guard turns it into a hard block at cook time.
// ---------------------------------------------------------------------------

import { NodeDef, ParamValue } from './types';
import { ResolutionContext } from '../core/resolution';

const f = (key: string, label: string, def: number, min: number, max: number, step = 0.01, unit = ''): NodeDef['params'][number] =>
  ({ key, label, type: 'float', min, max, step, default: def, unit });
const i = (key: string, label: string, def: number, min: number, max: number, step = 1, unit = ''): NodeDef['params'][number] =>
  ({ key, label, type: 'int', min, max, step, default: def, unit });

const noiseParams = (amp = 120, scale = 420): NodeDef['params'] => [
  f('base', 'Base Level', 40, 0, 300, 1, 'm'),
  f('amplitude', 'Amplitude', amp, 0, 400, 1, 'm'),
  f('scale', 'Feature Size', scale, 8, 2000, 4, 'm'),
  i('octaves', 'Octaves', 5, 1, 9),
  f('lacunarity', 'Lacunarity', 2.05, 1.2, 4, 0.05),
  f('gain', 'Gain', 0.5, 0.1, 0.95, 0.01),
  i('seedOffset', 'Seed Offset', 0, 0, 9999),
];

/** finest octave wavelength in metres for a fractal param set */
function finestWavelength(p: Record<string, ParamValue>): number {
  const scale = Number(p.scale ?? 420);
  const oct = Number(p.octaves ?? 5);
  const lac = Number(p.lacunarity ?? 2);
  return scale / Math.pow(lac, Math.max(0, oct - 1));
}

const fractalRes = (label: string) => (p: Record<string, ParamValue>, _ctx: ResolutionContext) => ({
  featureMetres: finestWavelength(p),
  label: `${label} finest octave`,
});

export const NODE_DEFS: NodeDef[] = [
  // ------------------------------------------------------------- generators
  {
    type: 'start', category: 'Output', label: 'Start', subtitle: 'Entry Point', icon: '⬢',
    inputs: [], outputs: [{ name: 'World', type: 'world' }],
    params: [],
  },
  {
    type: 'simplex', category: 'Generators', label: 'Simplex Noise', subtitle: 'Continuous 2D/3D noise', icon: '〜',
    inputs: [{ name: 'World', type: 'world', optional: true }],
    outputs: [{ name: 'Height', type: 'height' }],
    params: noiseParams(), resRequirement: fractalRes('Simplex'),
  },
  {
    type: 'perlin', category: 'Generators', label: 'Perlin Noise', subtitle: 'Classic gradient noise', icon: '〜',
    inputs: [{ name: 'World', type: 'world', optional: true }],
    outputs: [{ name: 'Height', type: 'height' }],
    params: noiseParams(), resRequirement: fractalRes('Perlin'),
  },
  {
    type: 'value', category: 'Generators', label: 'Value Noise', subtitle: 'Smooth interpolated noise', icon: '〜',
    inputs: [{ name: 'World', type: 'world', optional: true }],
    outputs: [{ name: 'Height', type: 'height' }],
    params: noiseParams(), resRequirement: fractalRes('Value'),
  },
  {
    type: 'multifractal', category: 'Generators', label: 'MultiFractal', subtitle: 'Complex ridged noise', icon: '⛰',
    inputs: [{ name: 'World', type: 'world', optional: true }],
    outputs: [{ name: 'Height', type: 'height' }],
    params: [
      f('base', 'Base Level', 30, 0, 300, 1, 'm'),
      f('amplitude', 'Amplitude', 210, 0, 500, 1, 'm'),
      f('scale', 'Feature Size', 620, 16, 3000, 4, 'm'),
      i('octaves', 'Octaves', 6, 1, 9),
      f('lacunarity', 'Lacunarity', 2.2, 1.2, 4, 0.05),
      f('gain', 'Gain', 0.55, 0.1, 0.95, 0.01),
      f('sharpness', 'Ridge Sharpness', 2.2, 0.5, 5, 0.1),
      i('seedOffset', 'Seed Offset', 0, 0, 9999),
    ],
    resRequirement: fractalRes('MultiFractal'),
  },
  {
    type: 'badlands', category: 'Generators', label: 'Badlands', subtitle: 'Terraced ridged mesas', icon: '⛰',
    inputs: [{ name: 'World', type: 'world', optional: true }],
    outputs: [{ name: 'Height', type: 'height' }],
    params: [
      ...noiseParams(160, 480),
      f('terrace', 'Terracing', 0.4, 0, 1, 0.02),
    ],
    resRequirement: fractalRes('Badlands'),
  },
  {
    type: 'voronoi', category: 'Generators', label: 'Cellular (Voronoi)', subtitle: 'Distance-based cellular noise', icon: '◇',
    inputs: [{ name: 'World', type: 'world', optional: true }],
    outputs: [{ name: 'Height', type: 'height' }],
    params: [
      f('base', 'Base Level', 30, 0, 300, 1, 'm'),
      f('amplitude', 'Amplitude', 90, 0, 300, 1, 'm'),
      f('scale', 'Cell Size', 260, 16, 2000, 4, 'm'),
      f('jitter', 'Jitter', 0.9, 0, 1, 0.01),
      { key: 'mode', label: 'Mode', type: 'select', default: 'F2-F1', options: [{ value: 'F1', label: 'F1' }, { value: 'F2', label: 'F2' }, { value: 'F2-F1', label: 'F2 − F1 (edges)' }] },
      i('seedOffset', 'Seed Offset', 0, 0, 9999),
    ],
    resRequirement: (p) => ({ featureMetres: Number(p.scale ?? 260) * 0.25, label: 'Voronoi cell wall' }),
  },
  {
    type: 'white', category: 'Generators', label: 'White Noise', subtitle: 'Random static noise', icon: '∷',
    inputs: [{ name: 'World', type: 'world', optional: true }],
    outputs: [{ name: 'Height', type: 'height' }],
    params: [f('base', 'Base Level', 40, 0, 300, 1, 'm'), f('amplitude', 'Amplitude', 6, 0, 60, 0.5, 'm'), i('seedOffset', 'Seed Offset', 0, 0, 9999)],
    resRequirement: () => null, // white noise is per-voxel by definition
  },
  {
    type: 'mix', category: 'Generators', label: 'Combine', subtitle: 'Blend two height fields', icon: '±',
    inputs: [{ name: 'A', type: 'height' }, { name: 'B', type: 'height' }],
    outputs: [{ name: 'Height', type: 'height' }],
    params: [
      { key: 'op', label: 'Operation', type: 'select', default: 'mix', options: ['add', 'subtract', 'multiply', 'max', 'min', 'mix'].map((v) => ({ value: v, label: v })) },
      f('weight', 'Mix Weight', 0.5, 0, 1, 0.01),
    ],
  },

  // ---------------------------------------------------------------- SDF
  {
    type: 'lift', category: 'SDF', label: 'To SDF', subtitle: 'Heightfield → narrow-band SDF', icon: '▣',
    inputs: [{ name: 'Height', type: 'height' }],
    outputs: [{ name: 'SDF', type: 'sdf' }],
    params: [],
  },
  {
    type: 'cave', category: 'SDF', label: 'Cave System', subtitle: 'Worley tunnel network', icon: '⌬',
    inputs: [{ name: 'World', type: 'world', optional: true }],
    outputs: [{ name: 'Void', type: 'void' }],
    params: [
      f('tunnelScale', 'Tunnel Spacing', 90, 20, 400, 2, 'm'),
      f('tunnelRadius', 'Tunnel Radius', 9, 1, 60, 0.5, 'm'),
      f('worminess', 'Worminess', 0.55, 0, 1, 0.01),
      f('depthBias', 'Depth Bias', 0.55, 0, 1, 0.01),
      f('coverage', 'Coverage', 0.5, 0, 1, 0.01),
      f('verticality', 'Vertical Shafts', 0.25, 0, 1, 0.01),
      i('seedOffset', 'Seed Offset', 0, 0, 9999),
    ],
    resRequirement: (p) => ({ featureMetres: Number(p.tunnelRadius ?? 9) * 1.6, label: 'Cave tunnel wall' }),
  },
  {
    type: 'carveSphere', category: 'SDF', label: 'Carve Sphere', subtitle: 'Spherical void primitive', icon: '◯',
    inputs: [], outputs: [{ name: 'Void', type: 'void' }],
    params: [
      f('cx', 'Center X', 0.5, 0, 1, 0.01), f('cy', 'Center Y', 0.25, 0, 1, 0.01), f('cz', 'Center Z', 0.5, 0, 1, 0.01),
      f('r', 'Radius', 60, 2, 400, 1, 'm'),
    ],
    resRequirement: (p) => ({ featureMetres: Number(p.r ?? 60) * 0.5, label: 'Carve sphere' }),
  },
  {
    type: 'carveBox', category: 'SDF', label: 'Carve Box', subtitle: 'Box void primitive', icon: '▢',
    inputs: [], outputs: [{ name: 'Void', type: 'void' }],
    params: [
      f('cx', 'Center X', 0.5, 0, 1, 0.01), f('cy', 'Center Y', 0.2, 0, 1, 0.01), f('cz', 'Center Z', 0.5, 0, 1, 0.01),
      f('hx', 'Half X', 80, 2, 500, 1, 'm'), f('hy', 'Half Y', 20, 2, 200, 1, 'm'), f('hz', 'Half Z', 80, 2, 500, 1, 'm'),
    ],
    resRequirement: (p) => ({ featureMetres: Math.min(Number(p.hx ?? 80), Number(p.hy ?? 20), Number(p.hz ?? 80)) * 0.6, label: 'Carve box' }),
  },
  {
    type: 'unionVoid', category: 'SDF', label: 'Union Voids', subtitle: 'Merge cave volumes', icon: '⋃',
    inputs: [{ name: 'A', type: 'void' }, { name: 'B', type: 'void', optional: true }],
    outputs: [{ name: 'Void', type: 'void' }],
    params: [],
  },
  {
    type: 'carve', category: 'SDF', label: 'Carve', subtitle: 'Subtract voids from terrain SDF', icon: '✂',
    inputs: [{ name: 'SDF', type: 'sdf' }, { name: 'Void', type: 'void' }],
    outputs: [{ name: 'SDF', type: 'sdf' }],
    params: [f('smooth', 'Smooth Blend', 0, 0, 20, 0.5, 'm')],
  },

  // ------------------------------------------------------------- erosion
  {
    type: 'hydraulic', category: 'Erosion', label: 'Hydraulic Erosion', subtitle: 'Rivers · stream power + sediment', icon: '≋',
    inputs: [{ name: 'SDF', type: 'sdf' }],
    outputs: [{ name: 'SDF', type: 'sdf' }, { name: 'Flow', type: 'mask' }],
    params: [
      i('iterations', 'Iterations', 16, 1, 200),
      f('rainfall', 'Rainfall', 1.0, 0.05, 5, 0.05),
      f('erodibility', 'Erodibility K', 0.55, 0, 2, 0.01),
      f('capacityKc', 'Capacity Kc', 0.9, 0, 3, 0.01),
      f('expM', 'Discharge Exp m', 0.5, 0.1, 1, 0.01),
      f('depositRate', 'Deposition', 0.35, 0, 1, 0.01),
      f('channelWidth', 'Channel Width', 14, 2, 120, 1, 'm'),
      f('talusMix', 'Talus Mix', 0.15, 0, 1, 0.01),
    ],
    resRequirement: (p) => ({ featureMetres: Number(p.channelWidth ?? 14), label: 'River channel' }),
  },
  {
    type: 'thermal', category: 'Erosion', label: 'Thermal Erosion', subtitle: 'Talus collapse · cliffs & scree', icon: '⩗',
    inputs: [{ name: 'SDF', type: 'sdf' }, { name: 'Hardness', type: 'mask', optional: true }],
    outputs: [{ name: 'SDF', type: 'sdf' }],
    params: [
      i('iterations', 'Iterations', 20, 1, 300),
      f('talusAngle', 'Talus Angle', 37, 10, 80, 0.5, '°'),
      f('strength', 'Strength', 0.6, 0, 1, 0.01),
      f('creep', 'Soil Creep', 0.05, 0, 0.5, 0.005),
      f('hardnessContrast', 'Hardness Contrast', 0.8, 0, 1, 0.01),
      f('strataBand', 'Strata Band', 26, 4, 200, 1, 'm'),
      b('useStrata', 'Use Strata Hardness', true),
    ],
    resRequirement: () => ({ featureMetres: 12, label: 'Talus slope width' }),
  },
  {
    type: 'alluvial', category: 'Erosion', label: 'Alluvial Deposition', subtitle: 'Floodplains · fans · aprons', icon: '⋞',
    inputs: [{ name: 'SDF', type: 'sdf' }],
    outputs: [{ name: 'SDF', type: 'sdf' }],
    params: [
      i('iterations', 'Iterations', 10, 1, 120),
      f('supply', 'Sediment Supply', 0.5, 0, 3, 0.01),
      f('slopeThreshold', 'Slope Threshold', 0.18, 0.01, 1, 0.01),
      f('fanStrength', 'Fan Strength', 0.7, 0, 1, 0.01),
      f('apron', 'Cliff Apron', 0.6, 0, 1, 0.01),
    ],
    resRequirement: () => ({ featureMetres: 12, label: 'Alluvial fan' }),
  },
  {
    type: 'wind', category: 'Erosion', label: 'Wind Erosion', subtitle: 'Aeolian abrasion & dunes', icon: '≨',
    inputs: [{ name: 'SDF', type: 'sdf' }],
    outputs: [{ name: 'SDF', type: 'sdf' }],
    params: [
      i('iterations', 'Iterations', 10, 1, 120),
      f('directionDeg', 'Wind Direction', 235, 0, 360, 1, '°'),
      f('strength', 'Strength', 0.6, 0, 1, 0.01),
      f('abrasion', 'Abrasion', 0.5, 0, 1, 0.01),
      f('deposition', 'Deposition', 0.45, 0, 1, 0.01),
      f('shelterRange', 'Shelter Range', 60, 8, 400, 2, 'm'),
      f('turbulence', 'Turbulence', 0.35, 0, 1, 0.01),
    ],
    resRequirement: (p) => ({ featureMetres: Number(p.shelterRange ?? 60) * 0.3, label: 'Wind shelter wavelength' }),
  },

  // ----------------------------------------------------------- texturing
  {
    type: 'mask', category: 'Texturing', label: 'Mask', subtitle: 'Height / slope / flow / cavity…', icon: '◧',
    inputs: [{ name: 'SDF', type: 'sdf' }, { name: 'Flow', type: 'mask', optional: true }],
    outputs: [{ name: 'Mask', type: 'mask' }],
    params: [
      {
        key: 'kind', label: 'Source', type: 'select', default: 'slope',
        options: ['height', 'slope', 'curvature', 'flow', 'cavity', 'moisture', 'strata', 'exposure'].map((v) => ({ value: v, label: v })),
      },
      f('lo', 'Range Lo', 0.1, -500, 500, 0.01),
      f('hi', 'Range Hi', 0.6, -500, 500, 0.01),
      f('feather', 'Feather', 0.1, 0.001, 5, 0.005),
      f('strataBand', 'Strata Band', 24, 4, 200, 1, 'm'),
    ],
  },
  {
    type: 'layer', category: 'Texturing', label: 'Splat Layer', subtitle: 'Mask + material = layer', icon: '▤',
    inputs: [{ name: 'Mask', type: 'mask' }],
    outputs: [{ name: 'Layer', type: 'layer' }],
    params: [
      { key: 'name', label: 'Layer Name', type: 'select', default: 'Rock', options: ['Rock', 'Grass', 'Sand', 'Snow', 'Scree', 'Mud', 'Base'].map((v) => ({ value: v, label: v })) },
      { key: 'color', label: 'Color', type: 'color', default: [0.45, 0.42, 0.4] as ParamValue },
      f('roughness', 'Roughness', 0.9, 0, 1, 0.01),
    ],
  },
  {
    type: 'splat', category: 'Texturing', label: 'Splatmap', subtitle: 'Pack layers into RGBA sets', icon: '▦',
    inputs: [
      { name: 'Base', type: 'layer' },
      { name: 'L2', type: 'layer', optional: true },
      { name: 'L3', type: 'layer', optional: true },
      { name: 'L4', type: 'layer', optional: true },
      { name: 'L5', type: 'layer', optional: true },
      { name: 'L6', type: 'layer', optional: true },
      { name: 'L7', type: 'layer', optional: true },
      { name: 'L8', type: 'layer', optional: true },
    ],
    outputs: [{ name: 'Splat', type: 'splat' }],
    params: [],
  },

  // -------------------------------------------------------------- output
  {
    type: 'output', category: 'Output', label: 'Output', subtitle: 'Mesh + splatmaps to viewport', icon: '▶',
    inputs: [{ name: 'SDF', type: 'sdf' }, { name: 'Splat', type: 'splat', optional: true }],
    outputs: [],
    params: [b('wireframe', 'Wireframe', false), f('uvScale', 'Texture Scale', 256, 16, 2048, 8, 'm')],
  },
];

function b(key: string, label: string, def: boolean): NodeDef['params'][number] {
  return { key, label, type: 'bool', default: def };
}

export const DEF_BY_TYPE = new Map(NODE_DEFS.map((d) => [d.type, d]));

export function defFor(type: string): NodeDef {
  const d = DEF_BY_TYPE.get(type);
  if (!d) throw new Error(`unknown node type ${type}`);
  return d;
}

export function defaultParams(type: string): Record<string, ParamValue> {
  const out: Record<string, ParamValue> = {};
  for (const p of defFor(type).params) out[p.key] = JSON.parse(JSON.stringify(p.default));
  return out;
}

export const CATEGORIES: NodeDef['category'][] = ['Generators', 'SDF', 'Erosion', 'Texturing', 'Output'];

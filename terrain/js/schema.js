// Declarative schema: every layer type and its parameters. The inspector UI is generated from this,
// and the engine packs the same values into GPU uniforms (via `slot` for height layers).
const R = (k, label, min, max, def, o = {}) => ({ k, label, min, max, def, type: 'range', ...o });
const SEG = (k, label, options, def, o = {}) => ({ k, label, options, def, type: 'seg', ...o });
const BOOL = (k, label, def, o = {}) => ({ k, label, def, type: 'bool', ...o });
const COLOR = (k, label, def) => ({ k, label, def, type: 'color' });
const SEED = (k = 'seed', slot) => ({ k, label: 'Seed', min: 0, max: 9999, def: 1, type: 'seed', int: true, slot });
const BAND = (k, label, min, max, def, o = {}) => ({ k, label, min, max, def, type: 'band', ...o });

const m = { unit: 'm' };
const deg = { unit: '°' };

const GEN_COMMON = [
  R('scale', 'Scale', 40, 8000, 900, { ...m, log: true, slot: 0 }),
  R('amp', 'Height', 0, 1.5, 0.6, { slot: 1, step: 0.01 }),
  R('bias', 'Base', -1, 1, 0, { slot: 2, step: 0.01 }),
  R('octaves', 'Octaves', 1, 12, 7, { int: true, slot: 3 }),
  R('lac', 'Lacunarity', 1.5, 3, 2.0, { slot: 4, step: 0.01, adv: true }),
  R('gain', 'Roughness', 0.2, 0.8, 0.5, { slot: 5, step: 0.01 }),
  R('warp', 'Warp', 0, 1200, 0, { ...m, slot: 6 }),
  R('warpScale', 'Warp scale', 40, 6000, 900, { ...m, log: true, slot: 7, adv: true }),
  R('offX', 'Offset X', -50, 50, 0, { slot: 12, step: 0.01, adv: true }),
  R('offZ', 'Offset Z', -50, 50, 0, { slot: 13, step: 0.01, adv: true }),
  SEED('seed', 15),
];
const without = (arr, ...keys) => arr.filter((p) => !keys.includes(p.k));
const override = (arr, k, o) => arr.map((p) => (p.k === k ? { ...p, ...o } : p));

export const BLEND_MODES = [['Replace', 0], ['Add', 1], ['Subtract', 2], ['Multiply', 3], ['Max', 4], ['Min', 5]];
export const MASK_TYPES = [['None', 0], ['Height', 1], ['Slope', 2], ['Noise', 3]];

export const HEIGHT_TYPES = {
  // ---------------- generators
  fbm: { label: 'Perlin fBm', group: 'Generate', kind: 1, glyph: 'F', blend: 1, params: GEN_COMMON },
  ridged: { label: 'Ridged', group: 'Generate', kind: 2, glyph: 'R', blend: 1,
    params: [...GEN_COMMON.slice(0, 8), R('sharp', 'Sharpness', 0.5, 4, 2, { slot: 8, step: 0.01 }), ...GEN_COMMON.slice(8)] },
  erosive: { label: 'Eroded fBm', group: 'Generate', kind: 10, glyph: 'E', blend: 1,
    params: [...GEN_COMMON.slice(0, 8), R('strength', 'Slope damping', 0, 5, 1.5, { slot: 8, step: 0.01 }), ...GEN_COMMON.slice(8)] },
  billow: { label: 'Billow', group: 'Generate', kind: 3, glyph: 'B', blend: 1, params: GEN_COMMON },
  voronoi: { label: 'Voronoi', group: 'Generate', kind: 4, glyph: 'V', blend: 1,
    params: [
      SEG('mode', 'Mode', [['Domes', 0], ['Cracks', 1], ['Plateaus', 2]], 2, { slot: 8 }),
      ...without(GEN_COMMON, 'octaves', 'lac', 'gain').slice(0, 5),
      R('jitter', 'Jitter', 0, 1, 0.9, { slot: 9, step: 0.01 }),
      R('edge', 'Edge width', 0.01, 0.6, 0.08, { slot: 10, step: 0.005 }),
      ...GEN_COMMON.slice(8),
    ] },
  mountain: { label: 'Mountain', group: 'Generate', kind: 5, glyph: 'M', blend: 4,
    params: [
      R('cx', 'Center X', 0, 1, 0.5, { slot: 8, step: 0.005 }), R('cz', 'Center Z', 0, 1, 0.5, { slot: 9, step: 0.005 }),
      R('radius', 'Radius', 50, 6000, 900, { ...m, slot: 10, log: true }), R('profile', 'Profile', 0.3, 3, 1.2, { slot: 11, step: 0.01 }),
      R('rough', 'Ridges', 0, 1.5, 0.7, { slot: 14, step: 0.01 }),
      ...override(GEN_COMMON, 'amp', { def: 0.9 }),
    ] },
  dunes: { label: 'Dunes', group: 'Generate', kind: 6, glyph: 'D', blend: 1,
    params: [
      R('wind', 'Wind', 0, 360, 30, { ...deg, slot: 8 }), R('asym', 'Slip face', 0.3, 0.95, 0.78, { slot: 9, step: 0.01 }),
      R('crest', 'Crest sharp', 0.3, 4, 1.6, { slot: 10, step: 0.01 }), R('vary', 'Field variation', 0, 1, 0.6, { slot: 11, step: 0.01 }),
      ...override(without(GEN_COMMON, 'octaves', 'lac', 'gain'), 'scale', { def: 260, label: 'Wavelength' }),
    ] },
  island: { label: 'Island shape', group: 'Generate', kind: 7, glyph: 'I', blend: 3,
    params: [
      R('cx', 'Center X', 0, 1, 0.5, { slot: 8, step: 0.005 }), R('cz', 'Center Z', 0, 1, 0.5, { slot: 9, step: 0.005 }),
      R('radius', 'Radius', 50, 6000, 800, { ...m, slot: 10, log: true }), R('falloff', 'Falloff', 1, 2000, 380, { ...m, slot: 11, log: true }),
      R('noise', 'Coast noise', 0, 1.2, 0.4, { slot: 14, step: 0.01 }),
      ...override(override(GEN_COMMON, 'amp', { def: 1 }), 'octaves', { def: 5 }),
    ] },
  canyon: { label: 'Canyon', group: 'Generate', kind: 8, glyph: 'C', blend: 1,
    params: [
      R('width', 'Width', 0, 0.4, 0.06, { slot: 8, step: 0.002 }), R('wall', 'Wall softness', 0.002, 0.3, 0.04, { slot: 9, step: 0.002 }),
      ...override(override(GEN_COMMON, 'amp', { def: 0.25, label: 'Depth' }), 'octaves', { def: 5 }),
    ] },
  coast: { label: 'Coastline', group: 'Generate', kind: 9, glyph: 'S', blend: 1,
    params: [
      R('angle', 'Direction', 0, 360, 90, { ...deg, slot: 8 }), R('pos', 'Position', 0, 1, 0.45, { slot: 9, step: 0.005 }),
      R('width', 'Edge width', 1, 600, 40, { ...m, slot: 10, log: true }),
      ...override(override(GEN_COMMON, 'warp', { def: 260 }), 'octaves', { def: 6 }),
    ] },
  constant: { label: 'Constant', group: 'Generate', kind: 11, glyph: 'K', blend: 1,
    params: [R('bias', 'Level', -1, 1.5, 0.1, { slot: 2, step: 0.005 })] },

  // ---------------- filters
  warp: { label: 'Domain warp', group: 'Filter', kind: 20, glyph: 'W', filter: true,
    params: [R('scale', 'Scale', 40, 6000, 700, { ...m, log: true, slot: 0 }), R('strength', 'Strength', 0, 600, 90, { ...m, slot: 1 }),
      R('octaves', 'Octaves', 1, 8, 4, { int: true, slot: 3 }), SEED('seed', 15)] },
  terrace: { label: 'Terraces', group: 'Filter', kind: 21, glyph: 'T', filter: true,
    params: [R('steps', 'Steps', 1, 40, 9, { int: true, slot: 0 }), R('riser', 'Riser width', 0.01, 1, 0.18, { slot: 1, step: 0.005 }),
      R('jitter', 'Jitter', 0, 0.8, 0.15, { slot: 2, step: 0.005 }), R('jscale', 'Jitter scale', 40, 4000, 500, { ...m, log: true, slot: 3 }),
      R('offset', 'Offset', 0, 1, 0, { slot: 4, step: 0.01 }), SEED('seed', 15)] },
  curve: { label: 'Curve / levels', group: 'Filter', kind: 22, glyph: '∿', filter: true,
    params: [R('gamma', 'Gamma', 0.2, 4, 1, { slot: 0, step: 0.01 }), R('contrast', 'Contrast', 0, 3, 1, { slot: 1, step: 0.01 }),
      R('bias', 'Bias', -0.6, 0.6, 0, { slot: 2, step: 0.005 }), BOOL('invert', 'Invert', false, { slot: 3 }),
      R('cmin', 'Clamp min', -1, 1, -1, { slot: 4, step: 0.01 }), R('cmax', 'Clamp max', 0, 2, 2, { slot: 5, step: 0.01 }),
      R('mult', 'Multiply', 0, 3, 1, { slot: 6, step: 0.01 })] },
  smooth: { label: 'Smooth', group: 'Filter', glyph: '≈', filter: true, pass: 'smooth',
    params: [R('radius', 'Radius', 1, 200, 12, { ...m, log: true })] },

  // ---------------- erosion / simulation
  erosion: { label: 'Hydraulic erosion', group: 'Erosion', glyph: 'H', filter: true, pass: 'erosion', sim: true,
    params: [
      R('rain', 'Rain density', 0.05, 5, 1.2, { step: 0.01 }), R('lifetime', 'Droplet life', 8, 160, 48, { int: true }),
      R('inertia', 'Inertia', 0, 0.95, 0.3, { step: 0.01 }), R('capacity', 'Capacity', 0.5, 16, 5, { step: 0.1 }),
      R('erode', 'Erosion', 0, 1, 0.35, { step: 0.01 }), R('deposit', 'Deposition', 0, 1, 0.25, { step: 0.01 }),
      R('evaporate', 'Evaporation', 0.001, 0.1, 0.018, { step: 0.001 }), R('gravity', 'Gravity', 0.5, 12, 4, { step: 0.1 }),
      R('radius', 'Brush radius', 1, 40, 6, { ...m }), R('minSlope', 'Min slope', 0.001, 0.2, 0.01, { step: 0.001, adv: true }),
      R('batches', 'Passes', 1, 12, 4, { int: true, adv: true }), SEED('seed'),
    ] },
  thermal: { label: 'Thermal erosion', group: 'Erosion', glyph: 'θ', filter: true, pass: 'thermal', sim: true,
    params: [R('talus', 'Talus angle', 5, 80, 36, deg), R('strength', 'Strength', 0, 1, 0.7, { step: 0.01 }), R('hard', 'Cliff hardness', 0, 1, 0, { step: 0.01, hint: 'Faces far steeper than the talus angle resist collapse (caprock / bedrock walls)' }),
      R('iterations', 'Iterations', 1, 200, 40, { int: true })] },
  rivers: { label: 'Rivers (flow carve)', group: 'Erosion', glyph: '≋', filter: true, pass: 'rivers', sim: true,
    params: [R('depth', 'Depth', 0, 120, 18, m), R('threshold', 'Threshold', 0, 1, 0.42, { step: 0.005 }),
      R('width', 'Width', 1, 120, 10, { ...m, log: true })] },
};

export const VOLUME_TYPES = {
  strata: { label: 'Strata ledges', kind: 1, glyph: '≡',
    params: [R('amp', 'Depth', 0, 25, 4, { ...m, step: 0.1 }), R('scale', 'Layer spacing', 1, 80, 9, { ...m, step: 0.1 }),
      R('sharp', 'Lip sharpness', 0.03, 0.9, 0.18, { step: 0.01 }), R('warp', 'Warp', 0, 60, 6, m),
      R('tilt', 'Dip', -30, 30, 0, deg), R('hard', 'Hardness variation', 0, 1, 0.6, { step: 0.01 }), SEED()] },
  overhang: { label: 'Overhang', kind: 2, glyph: '⌐',
    params: [R('amp', 'Amount', 0, 40, 6, { ...m, step: 0.1 }), R('scale', 'Height span', 5, 300, 50, { ...m, log: true })] },
  roughness: { label: '3D roughness', kind: 3, glyph: '∴',
    params: [R('amp', 'Amount', 0, 20, 2.5, { ...m, step: 0.1 }), R('scale', 'Scale', 1, 120, 14, { ...m, log: true }),
      R('octaves', 'Octaves', 1, 6, 4, { int: true }), BOOL('ridged', 'Ridged', false), SEED()] },
  rills: { label: 'Rills / flutes', kind: 4, glyph: '‖',
    params: [R('amp', 'Depth', 0, 12, 1.6, { ...m, step: 0.1 }), R('scale', 'Spacing', 1, 60, 5, { ...m, log: true }),
      R('stretch', 'Vertical stretch', 1, 24, 7, { step: 0.1 }), R('flow', 'Follow flow map', 0, 1, 0.5, { step: 0.01 }), SEED()] },
  fractures: { label: 'Fractures', kind: 5, glyph: '⌗',
    params: [R('amp', 'Depth', 0, 10, 1.5, { ...m, step: 0.1 }), R('scale', 'Block size', 2, 80, 16, { ...m, log: true }),
      R('width', 'Crack width', 0, 1, 0.3, { step: 0.01 }), SEED()] },
  spires: { label: 'Spires', prim: 1, glyph: '♜',
    params: [R('count', 'Count', 1, 64, 16, { int: true }), R('radius', 'Radius', 4, 120, 20, { ...m }),
      R('height', 'Height', 10, 800, 170, { ...m }), R('hvar', 'Height variation', 0, 1, 0.45, { step: 0.01 }),
      R('taper', 'Taper', 0.1, 1.5, 0.55, { step: 0.01 }), R('noise', 'Erosion noise', 0, 1, 0.6, { step: 0.01 }),
      R('bulge', 'Banding', 0, 1, 0.3, { step: 0.01 }),
      R('cx', 'Area X', 0, 1, 0.5, { step: 0.005 }), R('cz', 'Area Z', 0, 1, 0.5, { step: 0.005 }),
      R('area', 'Area radius', 0.05, 0.75, 0.35, { step: 0.005 }), SEED()] },
  arches: { label: 'Arches', prim: 2, glyph: '∩',
    params: [R('count', 'Count', 1, 16, 3, { int: true }), R('span', 'Span', 20, 600, 140, { ...m }),
      R('height', 'Height', 10, 400, 80, { ...m }), R('thick', 'Thickness', 3, 80, 15, { ...m }),
      R('noise', 'Erosion noise', 0, 1, 0.5, { step: 0.01 }),
      R('cx', 'Area X', 0, 1, 0.5, { step: 0.005 }), R('cz', 'Area Z', 0, 1, 0.5, { step: 0.005 }),
      R('area', 'Area radius', 0.05, 0.75, 0.3, { step: 0.005 }), SEED()] },
};

export const MATERIAL_TYPES = {
  rock: { label: 'Rock', detail: 0, glyph: '◆' },
  sand: { label: 'Sand', detail: 1, glyph: '∵' },
  vegetation: { label: 'Vegetation', detail: 2, glyph: '♣' },
  snow: { label: 'Snow', detail: 3, glyph: '❄' },
  scree: { label: 'Scree', detail: 4, glyph: '⁘' },
  soil: { label: 'Soil', detail: 5, glyph: '●' },
};
export const MATERIAL_PARAMS = [
  { section: 'Appearance' },
  COLOR('color', 'Color', '#8a8178'), COLOR('color2', 'Variation', '#6d655d'),
  R('varScale', 'Variation scale', 1, 800, 60, { ...m, log: true }),
  R('rough', 'Roughness', 0.05, 1, 0.85, { step: 0.01 }),
  R('detailScale', 'Detail scale', 0.1, 40, 3, { ...m, log: true, step: 0.01 }),
  R('bump', 'Detail bump', 0, 2, 0.6, { step: 0.01 }),
  { section: 'Coverage' },
  R('opacity', 'Opacity', 0, 1, 1, { step: 0.01 }),
  BAND('height', 'Height', -0.5, 1.6, [-0.5, 1.6], { step: 0.005 }),
  R('hSoft', 'Height softness', 0, 0.3, 0.04, { step: 0.005 }),
  BAND('slope', 'Slope', 0, 90, [0, 90], { ...deg, step: 0.5 }),
  R('sSoft', 'Slope softness', 0, 30, 6, { ...deg, step: 0.5 }),
  { section: 'Map weights  (−  prefers low · +  prefers high)' },
  R('curv', 'Curvature', -1, 1, 0, { step: 0.01, bipolar: true }),
  R('prot', 'Protrusion', -1, 1, 0, { step: 0.01, bipolar: true }),
  R('ao', 'Occlusion', -1, 1, 0, { step: 0.01, bipolar: true }),
  R('flow', 'Flow', -1, 1, 0, { step: 0.01, bipolar: true }),
  R('depo', 'Deposition', -1, 1, 0, { step: 0.01, bipolar: true }),
  R('wear', 'Wear', -1, 1, 0, { step: 0.01, bipolar: true }),
  { section: 'Breakup' },
  R('breakup', 'Noise breakup', 0, 1, 0.25, { step: 0.01 }),
  R('breakupScale', 'Breakup scale', 1, 800, 40, { ...m, log: true }),
  R('contrast', 'Edge contrast', 1, 20, 3, { step: 0.1 }),
];

export const HYBRID_PARAMS = [
  SEG('mode', 'Representation', [['Heightmap', 0], ['Hybrid', 1], ['Voxel', 2]], 1),
  R('threshold', 'Stretch threshold', 20, 85, 52, { ...deg, step: 0.5, hint: 'Slope above which the heightfield is considered stretched and voxels take over' }),
  R('softness', 'Transition', 1, 25, 7, { ...deg, step: 0.5 }),
  R('margin', 'Voxel margin', 0, 80, 14, { ...m }),
  SEG('voxel', 'Voxel size', [['½×', 0.5], ['1×', 1], ['2×', 2], ['3×', 3]], 1, { hint: 'Voxel cell size relative to the heightmap texel' }),
  BAND('weight', 'Detail ramp', 0, 1, [0.08, 0.6], { step: 0.01 }),
  R('smoothK', 'Primitive blend', 0, 40, 8, { ...m }),
  BOOL('showRegions', 'Highlight voxel regions', false),
];

export const WORLD_SECTIONS = [
  { title: 'Resolution', key: 'world', params: [
    SEG('resolution', 'Heightfield', [['256', 256], ['512', 512], ['1K', 1024], ['2K', 2048]], 512),
    SEG('preview', 'While dragging', [['Full', 1], ['½', 2], ['¼', 4]], 2),
    R('size', 'World size', 400, 12000, 2048, { ...m, log: true }),
    R('heightScale', 'Height scale', 40, 3000, 600, { ...m, log: true }),
    SEED('seed'),
  ] },
  { title: 'Render', key: 'render', params: [
    SEG('scale', 'Render scale', [['½×', 0.5], ['¾×', 0.75], ['1×', 1], ['1.5×', 1.5]], 1),
    BOOL('msaa', 'MSAA 4×', true),
    SEG('shadows', 'Shadows', [['Off', 0], ['2K', 2048], ['4K', 4096]], 2048),
    BOOL('skirts', 'Diorama skirts', true),
  ] },
  { title: 'Sun & sky', key: 'env', params: [
    R('sunAz', 'Sun azimuth', 0, 360, 135, deg), R('sunEl', 'Sun elevation', 1, 89, 28, deg),
    R('sun', 'Sun intensity', 0, 10, 2.6, { step: 0.05 }), COLOR('sunColor', 'Sun color', '#fff1dc'),
    R('ambient', 'Sky light', 0, 3, 0.8, { step: 0.01 }),
    COLOR('zenith', 'Zenith', '#4a78c0'), COLOR('horizon', 'Horizon', '#b9cde0'),
    R('fog', 'Haze', 0, 1, 0.25, { step: 0.005 }), R('fogFall', 'Haze falloff', 0, 1, 0.3, { step: 0.01 }),
    R('exposure', 'Exposure', 0.1, 3, 1.0, { step: 0.01 }), R('saturation', 'Saturation', 0, 1.6, 0.9, { step: 0.01 }),
  ] },
  { title: 'Ocean', key: 'ocean', params: [
    BOOL('enabled', 'Ocean', true), BOOL('infinite', 'Extend to horizon', true),
    R('level', 'Sea level', -0.2, 1, 0.12, { step: 0.002, fmtHs: true }),
    COLOR('shallow', 'Shallow', '#3fb7b0'), COLOR('deep', 'Deep', '#06243a'),
    R('clarity', 'Clarity', 0.5, 60, 9, { ...m, log: true }),
    R('waves', 'Wave height', 0, 3, 0.55, { ...m, step: 0.01 }), R('wavelength', 'Wavelength', 4, 200, 36, { ...m, log: true }),
    R('foam', 'Foam', 0, 1.5, 0.8, { step: 0.01 }), R('wind', 'Wind', 0, 360, 40, deg),
  ] },
  { title: 'Analysis maps', key: 'maps', params: [
    R('aoRadius', 'Occlusion radius', 10, 600, 120, { ...m, log: true }),
    R('protRadius', 'Protrusion radius', 10, 600, 90, { ...m, log: true }),
    R('curvRadius', 'Curvature radius', 1, 60, 6, { ...m, log: true }),
    R('curvGain', 'Curvature gain', 1, 400, 40, { log: true }),
    R('flowGain', 'Flow gain', 0.2, 3, 1.2, { step: 0.01 }),
    R('wearScale', 'Wear scale', 0.05, 20, 1.5, { ...m, log: true }),
    R('depoScale', 'Deposition scale', 0.05, 20, 1.0, { ...m, log: true }),
  ] },
];

export const VIEW_MODES = [
  ['Lit', 0], ['Albedo', 11], ['Height', 1], ['Slope', 2], ['Curvature', 3], ['Protrusion', 4], ['Occlusion', 5],
  ['Flow', 6], ['Wear', 7], ['Deposition', 8], ['Hybrid mask', 9], ['Normals', 10],
];

// which state changes require which amount of work
export const LEVEL = { RENDER: 0, MESH: 1, DERIVE: 2, HEIGHT: 3 };

export function defaultsFor(params) {
  const o = {};
  for (const p of params) if (p.k) o[p.k] = Array.isArray(p.def) ? [...p.def] : p.def;
  return o;
}
let _uid = 1;
export const uid = () => `l${Date.now().toString(36)}${(_uid++).toString(36)}`;

export function makeHeightLayer(type, over = {}) {
  const T = HEIGHT_TYPES[type];
  return {
    id: uid(), type, name: T.label, enabled: true,
    blend: T.blend ?? 1, opacity: 1,
    mask: { type: 0, lo: 0, hi: 1, soft: 0.05, invert: false, scale: 600, seed: 1 },
    p: { ...defaultsFor(T.params), ...(over.p || {}) },
    ...Object.fromEntries(Object.entries(over).filter(([k]) => k !== 'p')),
  };
}
export function makeVolumeLayer(type, over = {}) {
  const T = VOLUME_TYPES[type];
  return { id: uid(), type, name: T.label, enabled: true, p: { ...defaultsFor(T.params), ...(over.p || {}) },
    ...Object.fromEntries(Object.entries(over).filter(([k]) => k !== 'p')) };
}
export function makeMaterial(type, over = {}) {
  const T = MATERIAL_TYPES[type];
  return { id: uid(), type, name: over.name || T.label, enabled: true, p: { ...defaultsFor(MATERIAL_PARAMS), ...(over.p || {}) } };
}

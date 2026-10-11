// Texture layer stacks. Each surface material (asphalt, concrete, pavers) owns an ordered stack
// of procedural layers. The GPU compositor evaluates the stack bottom-to-top into an albedo map
// and a surface map (height + roughness), then builds mipmaps. Every layer is periodic, so the
// result tiles seamlessly at `textureTile` metres.

export const LAYER_TYPES = [
  { id: 0, key: 'base', name: 'Base colour & noise', params: ['colA', 'colB', 'scale', 'amount', 'rough', 'height'] },
  { id: 1, key: 'grain', name: 'Aggregate grain', params: ['colA', 'colB', 'scale', 'density', 'amount', 'height'] },
  { id: 2, key: 'cracks', name: 'Cracks', params: ['colA', 'scale', 'density', 'a', 'b', 'amount', 'height'], labels: { a: 'Wobble', b: 'Width' } },
  { id: 3, key: 'potholes', name: 'Potholes', params: ['scale', 'density', 'a', 'amount', 'height', 'rough'], labels: { a: 'Radius', density: 'Chance' } },
  { id: 4, key: 'patches', name: 'Repair patches', params: ['colA', 'scale', 'density', 'a', 'amount', 'rough'], labels: { a: 'Edge fuzz', density: 'Chance' } },
  { id: 5, key: 'stains', name: 'Oil & stains', params: ['colA', 'scale', 'density', 'amount', 'rough'] },
  { id: 6, key: 'wear', name: 'Traffic wear', params: ['colA', 'scale', 'a', 'amount', 'rough'], labels: { a: 'Track frequency' } },
  { id: 7, key: 'puddles', name: 'Puddles (wet)', params: ['scale', 'density', 'amount'], labels: { density: 'Coverage' } },
  { id: 8, key: 'pavers', name: 'Interlocking pavers', params: ['colA', 'colB', 'scale', 'a', 'amount', 'height'], labels: { a: 'Joint width', amount: 'Tone variation' } },
  { id: 9, key: 'cobbles', name: 'Setts / cobbles', params: ['colA', 'colB', 'scale', 'a', 'amount', 'height'], labels: { a: 'Joint width', amount: 'Tone variation' } },
  { id: 10, key: 'joints', name: 'Slab joints', params: ['colA', 'scale', 'a', 'amount', 'height'], labels: { a: 'Joint width', amount: 'Joint darkness', scale: 'Slab size (m)' } },
  { id: 11, key: 'gravel', name: 'Gravel & spall', params: ['colA', 'colB', 'scale', 'density', 'amount', 'height'] },
  { id: 12, key: 'grime', name: 'Grime & dirt', params: ['colA', 'scale', 'density', 'amount', 'rough'] },
];

export const PARAM_SPEC = {
  colA: { label: 'Colour A', type: 'color' },
  colB: { label: 'Colour B', type: 'color' },
  scale: { label: 'Scale (m)', min: 0.02, max: 12, step: 0.01 },
  density: { label: 'Density', min: 0, max: 1, step: 0.01 },
  amount: { label: 'Strength', min: 0, max: 1, step: 0.01 },
  a: { label: 'Param A', min: 0, max: 1, step: 0.01 },
  b: { label: 'Param B', min: 0, max: 1, step: 0.01 },
  rough: { label: 'Roughness', min: 0, max: 1, step: 0.01 },
  height: { label: 'Height / depth', min: -1, max: 1, step: 0.01 },
};

const LAYER_DEFAULTS = {
  colA: '#808080', colB: '#404040', scale: 2, density: 0.4, amount: 0.5, a: 0.3, b: 0.3, rough: 0.8, height: 0,
};

let uidCounter = 1;

export function makeLayer(typeId, over = {}) {
  const type = LAYER_TYPES[typeId];
  uidCounter += 1;
  return {
    id: `L${uidCounter.toString(36)}${Date.now().toString(36).slice(-3)}`,
    type: typeId,
    name: over.name || type.name,
    enabled: over.enabled ?? true,
    blend: over.blend ?? 0,      // 0 normal, 1 multiply
    opacity: over.opacity ?? 1,
    seed: over.seed ?? Math.floor(Math.random() * 999),
    ...LAYER_DEFAULTS,
    ...over.values,
  };
}

const L = (type, values, extra = {}) => ({ type, values, ...extra });

export const DEFAULT_STACKS = {
  asphalt: [
    L(0, { colA: '#3b3d41', colB: '#25262a', scale: 2.5, amount: 0.55, rough: 0.86, height: 0.25 }, { name: 'Asphalt base' }),
    L(1, { colA: '#6e7177', colB: '#141518', scale: 0.06, density: 0.55, amount: 0.35, height: 0.12 }, { name: 'Aggregate grain' }),
    L(12, { colA: '#2d2a26', scale: 5, density: 0.45, amount: 0.22, rough: 0.9 }, { name: 'Road dirt' }),
    L(6, { colA: '#1e1f22', scale: 4, a: 0.6, amount: 0.28, rough: 0.6 }, { name: 'Tyre wear tracks' }),
    L(5, { colA: '#0d0d0e', scale: 1.5, density: 0.25, amount: 0.45, rough: 0.5 }, { name: 'Oil stains' }),
    L(2, { colA: '#0b0b0c', scale: 2.0, density: 0.22, a: 0.35, b: 0.25, amount: 0.5, height: -0.4 }, { name: 'Surface cracks' }),
    L(4, { colA: '#44474d', scale: 4, density: 0.35, a: 0.25, amount: 0.2, rough: 0.8 }, { name: 'Repair patches' }),
    L(3, { scale: 6, density: 0.12, a: 0.35, amount: 0.55, height: -0.6, rough: 0.95 }, { name: 'Potholes' }),
    L(7, { scale: 4, density: 0.0, amount: 0.4 }, { name: 'Puddles', enabled: false }),
  ],
  concrete: [
    L(0, { colA: '#bdbab3', colB: '#a4a19a', scale: 2, amount: 0.35, rough: 0.7, height: 0.15 }, { name: 'Concrete base' }),
    L(1, { colA: '#d7d4cd', colB: '#7b7872', scale: 0.05, density: 0.5, amount: 0.3, height: 0.1 }, { name: 'Aggregate speckle' }),
    L(10, { colA: '#7d7a74', scale: 2.0, a: 0.03, amount: 0.6, height: -0.4 }, { name: 'Slab joints' }),
    L(5, { colA: '#6d6a63', scale: 3, density: 0.35, amount: 0.3, rough: 0.8 }, { name: 'Staining' }),
    L(12, { colA: '#8c8577', scale: 4, density: 0.5, amount: 0.25, rough: 0.9 }, { name: 'Grime' }),
    L(2, { colA: '#58554f', scale: 3.5, density: 0.12, a: 0.3, b: 0.12, amount: 0.35, height: -0.2 }, { name: 'Hairline cracks' }),
  ],
  pavers: [
    L(0, { colA: '#8f7c66', colB: '#7a6a58', scale: 2.4, amount: 0.2, rough: 0.75, height: 0.1 }, { name: 'Bedding sand' }),
    L(8, { colA: '#b49c7f', colB: '#8e7a62', scale: 0.2, a: 0.07, amount: 0.35, height: -0.5 }, { name: 'Pavers 200 mm' }),
    L(12, { colA: '#5f564b', scale: 2, density: 0.4, amount: 0.25, rough: 0.9 }, { name: 'Dirt in joints' }),
    L(5, { colA: '#3b3429', scale: 1.2, density: 0.3, amount: 0.3, rough: 0.6 }, { name: 'Stains' }),
  ],
};

export function defaultStack(material) {
  return DEFAULT_STACKS[material].map(item => makeLayer(item.type, { name: item.name, enabled: item.enabled, values: item.values }));
}

export function hexToRgb(hex) {
  const h = hex.replace('#', '');
  const v = parseInt(h.length === 3 ? h.split('').map(c => c + c).join('') : h, 16);
  return [((v >> 16) & 255) / 255, ((v >> 8) & 255) / 255, (v & 255) / 255];
}

export const MAX_LAYERS = 16;
export const PARAM_FLOATS = 4 + MAX_LAYERS * 20;

// Packs a stack into the uniform buffer layout expected by the compute shader:
//   info(vec4: count, tile, 0, 0), then per layer 5 x vec4 (info, p0, p1, p2, p3).
export function packStack(stack, tileMetres) {
  const out = new Float32Array(PARAM_FLOATS);
  const list = stack.slice(0, MAX_LAYERS);
  out[0] = list.length;
  out[1] = tileMetres;
  list.forEach((l, i) => {
    const o = 4 + i * 20;
    const ca = hexToRgb(l.colA), cb = hexToRgb(l.colB);
    out.set([l.type, l.blend, l.opacity, l.enabled ? 1 : 0], o);
    out.set([l.scale, l.seed, l.density, l.amount], o + 4);
    out.set([ca[0], ca[1], ca[2], l.height], o + 8);
    out.set([cb[0], cb[1], cb[2], l.rough], o + 12);
    out.set([l.a, l.b, 0, 0], o + 16);
  });
  return out;
}

export function describeLayer(l) {
  const t = LAYER_TYPES[l.type];
  return { type: t, name: t.name };
}

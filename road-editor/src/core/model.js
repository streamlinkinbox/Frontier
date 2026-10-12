// The project model is plain JSON: roads (control points + lanes + layer) and props.
// Everything else (sampled centrelines, topology, meshes) is derived from it.
import { PRESETS, DEFAULTS } from './presets.js';
import { terrainY, WATER_Y } from './terrain.js';
import { mulberry32, hashString } from './rng.js';

let idCounter = 1;
export const newId = (prefix) => `${prefix}${Date.now().toString(36)}${(idCounter++).toString(36)}`;

export function createModel(name = 'Untitled city') {
  return { format: 'frontier-road-editor', version: 1, name, roads: [], props: [] };
}

export function lanesFromPreset(presetKey) {
  const p = PRESETS[presetKey];
  return p.lanes.map(([type, width, dir]) => ({ type, width, dir: dir || null }));
}

/**
 * Create a road. `layer` describes the vertical placement:
 *   { mode: 'ground' }                              normal road
 *   { mode: 'bridge', bridge: 'girder'|..., clearance }
 *   { mode: 'subway', depth }                       underground line
 */
export function makeRoad({ name, preset = 'residential', points, layer, lanes, kind, seed } = {}) {
  const p = PRESETS[preset] || PRESETS.residential;
  const id = newId('r');
  return {
    id,
    name: name || `${p.label} ${id.slice(-4)}`,
    preset,
    kind: kind || p.kind || 'road',
    points: points.map((q) => ({ x: round(q.x), z: round(q.z) })),
    lanes: lanes || lanesFromPreset(preset),
    layer: { mode: 'ground', bridge: 'girder', clearance: DEFAULTS.clearance, depth: DEFAULTS.depth, rampLength: DEFAULTS.rampLength, ...(layer || {}) },
    seed: seed ?? hashString(id) >>> 0,
    look: makeLook(seed ?? hashString(id)),
  };
}

/** Per-road randomness: tint, wear, dash rhythm. This is why no two roads look copy-pasted. */
export function makeLook(seed) {
  const r = mulberry32(seed >>> 0);
  return {
    tint: 0.9 + r() * 0.16,                 // asphalt darkness multiplier
    warmth: (r() - 0.5) * 0.08,             // slight warm/cool cast
    patches: Math.floor(r() * 8),           // repair patches
    cracks: Math.floor(r() * 12),           // cracks / wear streaks
    dashLength: 2.8 + r() * 2.2,            // dashed-marking rhythm (m)
    dashGap: 4 + r() * 4,
    arrowEvery: 18 + r() * 30,              // arrow spacing in driving lanes
  };
}

export function makeProp(kind, x, z, extra = {}) {
  return { id: newId('p'), kind, x: round(x), z: round(z), angle: 0, length: 0, width: 0, ...extra };
}

const round = (v) => Math.round(v * 100) / 100;

/** Ramp factor 0..1 for a point at arc length s along a road of length L (smoothstep). */
export function rampAt(road, s, L) {
  if (road.layer.mode === 'ground') return 0;
  const len = Math.max(4, road.layer.rampLength || DEFAULTS.rampLength);
  const t = Math.max(0, Math.min(1, Math.min(s, L - s) / len));
  return t * t * (3 - 2 * t);
}

/** Final world height of the road surface centre at (x,z) with arc length s. */
export function elevationAt(road, s, L, x, z) {
  const g = terrainY(x, z);
  const r = rampAt(road, s, L);
  switch (road.layer.mode) {
    case 'bridge': {
      const deck = Math.max(g, WATER_Y) + (road.layer.clearance ?? DEFAULTS.clearance);
      return g + (deck - g) * r;
    }
    case 'subway':
      return g - (road.layer.depth ?? DEFAULTS.depth) * r;
    default:
      return g;
  }
}

export function roadWidth(road) {
  return road.lanes.reduce((a, l) => a + l.width, 0);
}

export function cloneModel(m) {
  return JSON.parse(JSON.stringify(m));
}

export function serialize(model) {
  return JSON.stringify(model, null, 2);
}

export function parseModel(text) {
  const m = JSON.parse(text);
  if (!m || m.format !== 'frontier-road-editor' || !Array.isArray(m.roads)) throw new Error('Not a Frontier road-editor project');
  m.props ||= [];
  return m;
}

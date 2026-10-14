// Presets: each is a complete, editable state — a height stack, volumetric detail, surface material stack, world.
import { WORLD_SECTIONS, HYBRID_PARAMS, defaultsFor, makeHeightLayer as H, makeVolumeLayer as V, makeMaterial as Mt } from './schema.js';

export function defaultState() {
  const s = { view: 0, layers: [], volume: [], materials: [], camera: null };
  for (const sec of WORLD_SECTIONS) s[sec.key] = defaultsFor(sec.params);
  s.hybrid = defaultsFor(HYBRID_PARAMS);
  return s;
}
const merge = (dst, src) => { for (const k in src) dst[k] = src[k] && typeof src[k] === 'object' && !Array.isArray(src[k]) ? merge(dst[k] || {}, src[k]) : src[k]; return dst; };
const mask = (type, lo, hi, soft, o = {}) => ({ mask: { type, lo, hi, soft, invert: false, scale: 600, seed: 1, ...o } });

function build(o) {
  const s = defaultState();
  merge(s, o.state || {});
  s.layers = o.layers; s.volume = o.volume || []; s.materials = o.materials; s.camera = o.camera || null;
  return s;
}

// ------------------------------------------------------------------ shared material building blocks
const rock = (c = '#6e6862', c2 = '#4c4743', o = {}) => Mt('rock', { name: o.name || 'Bedrock', p: { color: c, color2: c2, varScale: 90, detailScale: 4, bump: 0.9, rough: 0.85, ...o.p } });

export const PRESETS = {
  'Alpine Range': () => build({
    state: { world: { heightScale: 900 }, env: { sunAz: 125, sunEl: 26, fog: 0.3, fogFall: 0.35 }, ocean: { enabled: false },
      hybrid: { threshold: 50, margin: 10 } },
    layers: [
      H('ridged', { name: 'Main ridges', p: { scale: 1500, amp: 0.62, octaves: 8, gain: 0.48, sharp: 2.2, warp: 220, warpScale: 1600, seed: 4 } }),
      H('mountain', { name: 'Massif', blend: 1, p: { cx: 0.52, cz: 0.45, radius: 900, profile: 1.4, rough: 0.6, amp: 0.4, scale: 700, seed: 2 } }),
      H('erosive', { name: 'Erosive detail', p: { scale: 520, amp: 0.12, bias: -0.06, octaves: 7, strength: 2, seed: 7 } }),
      H('erosion', { name: 'Hydraulic erosion', p: { rain: 1.6, lifetime: 64, erode: 0.4, capacity: 6, deposit: 0.25, radius: 6 } }),
      H('thermal', { name: 'Talus', p: { talus: 38, strength: 0.6, iterations: 30 } }),
      H('rivers', { name: 'Valley rivers', p: { depth: 10, threshold: 0.5, width: 12 } }),
    ],
    volume: [
      V('strata', { p: { amp: 2.5, scale: 7, sharp: 0.22, tilt: 9, warp: 8 } }),
      V('fractures', { p: { amp: 1.4, scale: 14, width: 0.25 } }),
      V('roughness', { p: { amp: 1.6, scale: 11, octaves: 4 } }),
    ],
    materials: [
      rock('#6d6965', '#4b4846'),
      Mt('scree', { name: 'Scree fans', p: { color: '#8b8781', color2: '#6f6b66', slope: [10, 42], depo: 0.85, curv: -0.3, breakup: 0.35, detailScale: 1.2, bump: 1 } }),
      Mt('vegetation', { name: 'Alpine meadow', p: { color: '#4b5a2c', color2: '#68693a', height: [-0.5, 0.42], hSoft: 0.08, slope: [0, 31], sSoft: 6, ao: 0.2, prot: -0.2, breakup: 0.4, varScale: 140 } }),
      Mt('soil', { name: 'Stream beds', p: { color: '#5a5045', color2: '#463e35', flow: 0.9, breakup: 0.2, slope: [0, 40] } }),
      Mt('snow', { name: 'Snow', p: { color: '#eef2f7', color2: '#cfd8e4', height: [0.6, 1.6], hSoft: 0.1, slope: [0, 46], sSoft: 8, curv: 0.35, ao: 0.25, breakup: 0.45, rough: 0.6, bump: 0.3 } }),
    ],
    camera: { target: [1024, 200, 1024], yaw: 0.85, pitch: 0.4, distance: 3300 },
  }),

  'Sea Cliffs': () => build({
    state: { world: { heightScale: 240, size: 1600 }, env: { sunAz: 200, sunEl: 24, fog: 0.22, horizon: '#c6d6e2' },
      ocean: { level: 0.1, waves: 0.8, wavelength: 42, foam: 1.1, clarity: 7, shallow: '#3aa7a2', deep: '#082a40' },
      hybrid: { threshold: 46, softness: 6, margin: 12, weight: [0.06, 0.5] } },
    layers: [
      H('fbm', { name: 'Upland', p: { scale: 900, amp: 0.5, bias: 0.45, octaves: 7, gain: 0.52, warp: 150, seed: 3 } }),
      H('ridged', { name: 'Headland ridges', blend: 1, p: { scale: 700, amp: 0.18, bias: -0.08, octaves: 6, sharp: 1.4, seed: 8 } }),
      H('coast', { name: 'Coastline', blend: 3, p: { angle: 100, pos: 0.4, width: 14, warp: 520, warpScale: 650, octaves: 7, seed: 5 } }),
      H('fbm', { name: 'Sea floor', blend: 4, p: { scale: 600, amp: 0.12, bias: -0.25, octaves: 4, seed: 9 } }),
      H('terrace', { name: 'Ledges', opacity: 0.35, p: { steps: 14, riser: 0.15, jitter: 0.2 }, ...mask(1, 0.35, 1.5, 0.05) }),
      H('erosion', { name: 'Rain erosion', p: { rain: 1.0, erode: 0.3, radius: 5 } }),
      H('thermal', { name: 'Collapse', p: { talus: 60, strength: 0.4, iterations: 10 } }),
    ],
    volume: [
      V('strata', { p: { amp: 3.5, scale: 6, sharp: 0.16, warp: 5, tilt: -6, hard: 0.7 } }),
      V('overhang', { p: { amp: 7, scale: 45 } }),
      V('roughness', { p: { amp: 1.8, scale: 9, octaves: 4, ridged: true } }),
      V('fractures', { p: { amp: 1.2, scale: 10, width: 0.3 } }),
    ],
    materials: [
      rock('#78716a', '#58524d', { p: { bump: 1.1 } }),
      Mt('sand', { name: 'Beach', p: { color: '#c9b791', color2: '#a99674', height: [-0.5, 0.135], hSoft: 0.015, slope: [0, 22], sSoft: 6 } }),
      Mt('scree', { name: 'Talus', p: { color: '#8d877e', color2: '#6a655e', depo: 0.9, slope: [8, 40], height: [0.05, 1.6] } }),
      Mt('vegetation', { name: 'Clifftop grass', p: { color: '#56682f', color2: '#787a44', slope: [0, 34], sSoft: 5, height: [0.2, 1.6], hSoft: 0.04, ao: 0.15, breakup: 0.35 } }),
      Mt('rock', { name: 'Wet tidal rock', p: { color: '#3a3836', color2: '#2a2928', height: [-0.5, 0.115], hSoft: 0.01, slope: [25, 90], rough: 0.35 } }),
    ],
    camera: { target: [800, 70, 820], yaw: -0.55, pitch: 0.32, distance: 1700 },
  }),

  'Stone Forest': () => build({
    state: { world: { heightScale: 300 }, env: { sunAz: 160, sunEl: 34, fog: 0.45, fogFall: 0.5, horizon: '#c9d3d6', zenith: '#6688aa' },
      ocean: { enabled: false }, hybrid: { threshold: 48, margin: 10, smoothK: 10 } },
    layers: [
      H('fbm', { name: 'Basin', p: { scale: 1400, amp: 0.25, bias: 0.1, octaves: 6, seed: 2 } }),
      H('billow', { name: 'Hummocks', p: { scale: 260, amp: 0.05, octaves: 5, seed: 6 } }),
      H('erosion', { name: 'Erosion', p: { rain: 1.0, erode: 0.3 } }),
      H('smooth', { name: 'Soften valleys', opacity: 0.5, p: { radius: 10 }, ...mask(2, 0, 20, 6) }),
    ],
    volume: [
      V('spires', { name: 'Karst pillars', p: { count: 46, radius: 22, height: 190, hvar: 0.5, taper: 0.6, noise: 0.7, bulge: 0.5, cx: 0.5, cz: 0.5, area: 0.38, seed: 3 } }),
      V('spires', { name: 'Tall towers', p: { count: 8, radius: 34, height: 330, hvar: 0.3, taper: 0.45, noise: 0.5, bulge: 0.3, cx: 0.55, cz: 0.45, area: 0.22, seed: 11 } }),
      V('rills', { p: { amp: 2.2, scale: 5, stretch: 9, flow: 0.2 } }),
      V('strata', { p: { amp: 1.6, scale: 8, sharp: 0.25 } }),
      V('roughness', { p: { amp: 1.4, scale: 8 } }),
    ],
    materials: [
      rock('#8b8780', '#5d5a55', { name: 'Limestone', p: { bump: 1.2, varScale: 30 } }),
      Mt('rock', { name: 'Dark streaks', p: { color: '#4b4a46', color2: '#353431', slope: [55, 90], ao: -0.6, flow: 0.4, breakup: 0.6, breakupScale: 12, opacity: 0.8 } }),
      Mt('vegetation', { name: 'Forest floor', p: { color: '#353d22', color2: '#4a4a2b', slope: [0, 38], sSoft: 6, breakup: 0.3, ao: 0.2 } }),
      Mt('vegetation', { name: 'Ledge moss', p: { color: '#3c4626', color2: '#545531', slope: [0, 50], curv: 0.6, prot: 0.4, opacity: 0.85, breakup: 0.5 } }),
    ],
    camera: { target: [1030, 110, 1040], yaw: 0.6, pitch: 0.26, distance: 1500 },
  }),

  'Desert Mesas': () => build({
    state: { world: { heightScale: 420 }, env: { sunAz: 250, sunEl: 30, sunColor: '#ffe7c7', horizon: '#e3cfb4', zenith: '#5a86c0', fog: 0.2 },
      ocean: { enabled: false }, hybrid: { threshold: 50, margin: 12 } },
    layers: [
      H('constant', { name: 'Desert floor', blend: 0, p: { bias: 0.07 } }),
      H('fbm', { name: 'Floor undulation', blend: 1, p: { scale: 1100, amp: 0.05, bias: 0, octaves: 5, seed: 21 } }),
      H('voronoi', { name: 'Mesa plateaus', blend: 4, p: { mode: 2, scale: 480, amp: 0.5, bias: 0.02, jitter: 1, edge: 0.03, warp: 140, warpScale: 600, seed: 8 }, ...mask(3, 0.52, 1.5, 0.025, { scale: 900, seed: 4 }) }),
      H('voronoi', { name: 'Buttes', blend: 4, p: { mode: 2, scale: 190, amp: 0.36, bias: 0.0, jitter: 1, edge: 0.04, warp: 60, warpScale: 300, seed: 17 }, ...mask(3, 0.68, 1.5, 0.02, { scale: 500, seed: 9 }) }),
      H('terrace', { name: 'Sandstone benches', p: { steps: 6, riser: 0.04, jitter: 0.2, jscale: 700 } }),
      H('dunes', { name: 'Sand ripples', p: { scale: 120, amp: 0.012, wind: 60 }, ...mask(1, -1, 0.12, 0.03) }),
      H('erosion', { name: 'Flash flood erosion', p: { rain: 0.7, erode: 0.35, deposit: 0.35, capacity: 6, radius: 3 } }),
      H('thermal', { name: 'Talus', p: { talus: 32, strength: 0.5, hard: 0.95, iterations: 24 } }),
    ],
    volume: [
      V('strata', { p: { amp: 3, scale: 6, sharp: 0.1, warp: 2, hard: 0.9 } }),
      V('overhang', { p: { amp: 2, scale: 30 } }),
      V('rills', { p: { amp: 1.2, scale: 4, stretch: 10, flow: 0.4 } }),
      V('roughness', { p: { amp: 0.6, scale: 7 } }),
    ],
    materials: [
      rock('#8a4a2c', '#6a3521', { name: 'Red sandstone', p: { varScale: 25, bump: 0.8 } }),
      Mt('rock', { name: 'Pale bands', p: { color: '#a9805c', color2: '#966a45', slope: [40, 90], breakup: 0.7, breakupScale: 45, opacity: 0.35, contrast: 3 } }),
      Mt('scree', { name: 'Talus aprons', p: { color: '#8a5c3e', color2: '#6f462d', depo: 0.9, slope: [8, 38] } }),
      Mt('sand', { name: 'Desert sand', p: { color: '#b98d5d', color2: '#a87c4d', slope: [0, 18], sSoft: 6, height: [-0.5, 0.13], hSoft: 0.03 } }),
      Mt('soil', { name: 'Desert varnish', p: { color: '#3f2a20', color2: '#2d1f18', slope: [55, 90], flow: 0.5, ao: -0.5, opacity: 0.5, breakup: 0.6, breakupScale: 15 } }),
    ],
    camera: { target: [1024, 90, 1024], yaw: 3.6, pitch: 0.36, distance: 2600 },
  }),

  'Dune Sea': () => build({
    state: { world: { heightScale: 160 }, env: { sunAz: 280, sunEl: 16, sunColor: '#ffdcb0', horizon: '#ecd3b0', zenith: '#6d95c9', fog: 0.25, exposure: 1.05 },
      ocean: { enabled: false }, hybrid: { mode: 0 } },
    layers: [
      H('fbm', { name: 'Erg swell', p: { scale: 1800, amp: 0.3, bias: 0.15, octaves: 4, seed: 2 } }),
      H('dunes', { name: 'Draa (mega dunes)', p: { scale: 520, amp: 0.55, wind: 35, asym: 0.8, crest: 2.2, vary: 0.7, warp: 120, seed: 4 } }),
      H('dunes', { name: 'Barchan ridges', p: { scale: 140, amp: 0.1, wind: 45, asym: 0.75, crest: 1.6, vary: 0.8, warp: 40, seed: 9 } }),
      H('thermal', { name: 'Angle of repose', p: { talus: 33, strength: 0.8, iterations: 40 } }),
      H('dunes', { name: 'Ripples', p: { scale: 22, amp: 0.004, wind: 50, asym: 0.6, vary: 0.4, seed: 2 } }),
    ],
    volume: [],
    materials: [
      Mt('sand', { name: 'Sand', p: { color: '#c09060', color2: '#ad7d4f', varScale: 300, bump: 0.5 } }),
      Mt('sand', { name: 'Crest sand', p: { color: '#cfa575', color2: '#c29866', curv: 0.6, prot: 0.4, breakup: 0.3, opacity: 0.8 } }),
      Mt('sand', { name: 'Trough', p: { color: '#a2724a', color2: '#93653f', curv: -0.5, prot: -0.6, opacity: 0.7 } }),
    ],
    camera: { target: [1024, 30, 1024], yaw: 1.1, pitch: 0.17, distance: 1300 },
  }),

  'Icelandic Highlands': () => build({
    state: { world: { heightScale: 520 }, env: { sunAz: 190, sunEl: 15, sunColor: '#ffe6cc', sun: 2.0, zenith: '#5a7591', horizon: '#c3cbd1', fog: 0.4, fogFall: 0.25, ambient: 1.1 },
      ocean: { level: 0.05, deep: '#0b2230', shallow: '#36706f', clarity: 4, waves: 0.9 }, hybrid: { threshold: 50 } },
    layers: [
      H('erosive', { name: 'Volcanic plateau', p: { scale: 1400, amp: 0.6, bias: 0.05, octaves: 8, strength: 2.6, gain: 0.52, seed: 6 } }),
      H('ridged', { name: 'Tuya ridges', blend: 4, p: { scale: 800, amp: 0.85, bias: -0.12, octaves: 7, sharp: 2.2, warp: 200, seed: 12 } }),
      H('terrace', { name: 'Lava flows', opacity: 0.4, p: { steps: 6, riser: 0.3, jitter: 0.3 } }),
      H('erosion', { name: 'Glacial erosion', p: { rain: 1.8, erode: 0.45, capacity: 8, radius: 8, lifetime: 70 } }),
      H('rivers', { name: 'Braided rivers', p: { depth: 8, threshold: 0.4, width: 20 } }),
      H('thermal', { name: 'Talus', p: { talus: 36, strength: 0.5, hard: 0.6, iterations: 20 } }),
    ],
    volume: [
      V('fractures', { name: 'Columnar basalt', p: { amp: 2.4, scale: 6, width: 0.2 } }),
      V('strata', { p: { amp: 2, scale: 12, sharp: 0.3 } }),
      V('roughness', { p: { amp: 1.2, scale: 10 } }),
    ],
    materials: [
      rock('#3d3b3a', '#2a2928', { name: 'Basalt', p: { bump: 1 } }),
      Mt('scree', { name: 'Black gravel', p: { color: '#2f2e2d', color2: '#454240', depo: 0.8, slope: [0, 35], breakup: 0.3 } }),
      Mt('vegetation', { name: 'Moss', p: { color: '#4f6136', color2: '#6a7046', slope: [0, 24], sSoft: 6, height: [-0.5, 0.32], hSoft: 0.08, ao: 0.5, prot: -0.5, curv: -0.3, breakup: 0.6, breakupScale: 70, varScale: 220 } }),
      Mt('soil', { name: 'Glacial silt', p: { color: '#7a776f', color2: '#5f5c56', flow: 0.9, slope: [0, 20] } }),
      Mt('snow', { name: 'Snow patches', p: { color: '#e8edf2', color2: '#cbd4dd', height: [0.55, 1.6], hSoft: 0.12, slope: [0, 40], ao: 0.6, curv: -0.3, breakup: 0.7, breakupScale: 90 } }),
    ],
    camera: { target: [1024, 150, 1024], yaw: 5.2, pitch: 0.34, distance: 2700 },
  }),

  'Tropical Island': () => build({
    state: { world: { heightScale: 520 }, env: { sunAz: 110, sunEl: 40, sun: 3.0, zenith: '#2f6fd0', horizon: '#bcd8ee', fog: 0.18 },
      ocean: { level: 0.08, shallow: '#2fd0c4', deep: '#04355e', clarity: 16, waves: 0.5, foam: 0.9 },
      hybrid: { threshold: 52 } },
    layers: [
      H('fbm', { name: 'Volcanic body', p: { scale: 900, amp: 0.35, bias: 0.25, octaves: 7, seed: 5 } }),
      H('mountain', { name: 'Peak', p: { cx: 0.47, cz: 0.5, radius: 600, profile: 1.15, rough: 0.6, amp: 0.75, scale: 500, seed: 3 } }),
      H('island', { name: 'Island mask', p: { cx: 0.5, cz: 0.5, radius: 700, falloff: 380, noise: 0.7, scale: 700, seed: 2 } }),
      H('constant', { name: 'Sea floor', blend: 4, p: { bias: -0.15 } }),
      H('erosion', { name: 'Tropical rain', p: { rain: 2, erode: 0.5, capacity: 7, radius: 5, lifetime: 64 } }),
      H('rivers', { name: 'Gullies', p: { depth: 12, threshold: 0.45, width: 8 } }),
    ],
    volume: [
      V('rills', { name: 'Pali flutes', p: { amp: 1.4, scale: 8, stretch: 10, flow: 0.6 } }),
      V('roughness', { p: { amp: 1.5, scale: 10 } }),
    ],
    materials: [
      rock('#5a534d', '#3e3a36', { name: 'Volcanic rock' }),
      Mt('vegetation', { name: 'Jungle', p: { color: '#22361a', color2: '#33481f', slope: [0, 58], sSoft: 8, height: [0.1, 1.6], hSoft: 0.02, breakup: 0.25, bump: 1.1, varScale: 80, ao: 0.1 } }),
      Mt('vegetation', { name: 'Grass', p: { color: '#4d6230', color2: '#66713e', slope: [0, 22], height: [0.1, 0.3], hSoft: 0.04, opacity: 0.8 } }),
      Mt('sand', { name: 'Coral beach', p: { color: '#eadcbc', color2: '#d8c7a0', height: [-0.5, 0.105], hSoft: 0.012, slope: [0, 28] } }),
      Mt('soil', { name: 'Red laterite', p: { color: '#8a4b2c', color2: '#6b3820', flow: 0.8, wear: 0.4, slope: [10, 60], opacity: 0.7 } }),
    ],
    camera: { target: [1024, 60, 1024], yaw: 0.4, pitch: 0.3, distance: 2200 },
  }),

  Canyonlands: () => build({
    state: { world: { heightScale: 360 }, env: { sunAz: 235, sunEl: 24, sunColor: '#ffe3bf', horizon: '#e6cdb0', zenith: '#4f7fc0', fog: 0.18 },
      ocean: { enabled: false }, hybrid: { threshold: 48, margin: 12 } },
    layers: [
      H('fbm', { name: 'Plateau', p: { scale: 1600, amp: 0.15, bias: 0.6, octaves: 5, seed: 4 } }),
      H('canyon', { name: 'Main canyon', blend: 1, p: { width: 0.1, wall: 0.03, amp: 0.45, scale: 1700, octaves: 5, warp: 240, seed: 7 } }),
      H('canyon', { name: 'Side canyons', blend: 1, p: { width: 0.04, wall: 0.03, amp: 0.16, scale: 650, octaves: 5, warp: 80, seed: 13 } }),
      H('terrace', { name: 'Benches', p: { steps: 8, riser: 0.1, jitter: 0.2 } }),
      H('erosion', { name: 'Erosion', p: { rain: 1.0, erode: 0.4, deposit: 0.3, radius: 4 } }),
      H('thermal', { name: 'Talus', p: { talus: 34, strength: 0.5, hard: 0.85, iterations: 18 } }),
    ],
    volume: [
      V('arches', { name: 'Natural arches', p: { count: 5, span: 150, height: 85, thick: 16, noise: 0.5, cx: 0.5, cz: 0.5, area: 0.33, seed: 4 } }),
      V('strata', { p: { amp: 3.2, scale: 5, sharp: 0.15, warp: 3, hard: 0.85 } }),
      V('overhang', { p: { amp: 3, scale: 25 } }),
      V('roughness', { p: { amp: 0.9, scale: 7 } }),
    ],
    materials: [
      rock('#97552f', '#763f22', { name: 'Navajo sandstone', p: { varScale: 25 } }),
      Mt('rock', { name: 'White rim', p: { color: '#b89a78', color2: '#a68663', height: [0.6, 0.66], hSoft: 0.015, slope: [25, 90], opacity: 0.6, breakup: 0.4 } }),
      Mt('scree', { name: 'Talus', p: { color: '#a46a45', color2: '#86512f', depo: 0.85, slope: [8, 38] } }),
      Mt('sand', { name: 'Wash sand', p: { color: '#b98e60', color2: '#a87d50', slope: [0, 14], flow: 0.4, height: [-0.5, 0.45] } }),
      Mt('vegetation', { name: 'Desert scrub', p: { color: '#6a6a3a', color2: '#53562e', slope: [0, 14], opacity: 0.45, breakup: 0.85, breakupScale: 10, ao: 0.4 } }),
    ],
    camera: { target: [1024, 170, 1024], yaw: 2.2, pitch: 0.4, distance: 2500 },
  }),
};

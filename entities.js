/* ============================================================
   Frontier Engine — Scene Graph + Entity Card Definitions
   Every entity owns a bespoke card set. Nothing is shared.
   ============================================================ */

const FOLDERS = [
  { id: 'world',    name: 'World & Atmosphere', icon: 'world' },
  { id: 'nature',   name: 'Natural Environment', icon: 'mountain' },
  { id: 'lights',   name: 'Local Lights',        icon: 'flame' },
  { id: 'cine',     name: 'Cameras & Cinematics',icon: 'film' },
  { id: 'actors',   name: 'Actors & Geometry',   icon: 'box' },
  { id: 'post',     name: 'Post Processing',     icon: 'aperture' }
];

const ENTITIES = [
  /* ================= 1. SUN ================= */
  {
    id: 'sun', folder: 'world', name: 'Directional Light', label: 'Sun',
    icon: 'sun', accent: '#ffb020', mobility: 'static', visible: true, locked: false,
    transform: { loc: [0, 0, 900], rot: [-58.2, 42.8, 0], scale: [1, 1, 1] },
    cards: [
      { kind: 'celestialGizmo', accent: '#3ddc97', title: 'Solar Vector Gizmo', icon: 'crosshair' },
      { kind: 'probePlate',     accent: '#f5a524', title: 'Direct Illuminance · Probes', icon: 'target' },
      { kind: 'spectrumBar',    accent: '#ff8a3d', title: 'Color Temperature · Blackbody', icon: 'thermometer',
        cfg: { key: 'sun.kelvin', mode: 'kelvin', value: 5600, min: 1800, max: 12000,
               stops: ['#4d1403','#a83410','#e0691f','#ffab4a','#fff1d6','#eaf2ff','#c3d8ff','#9dbcf5'] } },
      { kind: 'cascadeSplits',  accent: '#4cc9f0', title: 'Cascaded Shadow Maps', icon: 'layers' },
      { kind: 'diurnalTimeline',accent: '#5b9dff', title: 'Diurnal Solar Cycle', icon: 'clock' },
      { kind: 'rtBudgetGraph',  accent: '#a3e635', title: 'RT Shadow Frame Budget', icon: 'cpu' }
    ]
  },

  /* ================= 2. SKY ATMOSPHERE ================= */
  {
    id: 'sky', folder: 'world', name: 'Sky Atmosphere', label: 'Physically Based Sky',
    icon: 'cloud', accent: '#4cc9f0', mobility: 'static', visible: true, locked: false,
    transform: { loc: [0, 0, 0], rot: [0, 0, 0], scale: [1, 1, 1] },
    cards: [
      { kind: 'densityColumn',     accent: '#8b7cf6', title: 'Atmospheric Density Profile', icon: 'layers' },
      { kind: 'wavelengthCurves',  accent: '#3ddc97', title: 'Spectral Scattering Response', icon: 'activity' },
      { kind: 'skyGradient',   accent: '#4cc9f0', title: 'Zenith → Horizon Luminance', icon: 'droplet' },
      { kind: 'dialKnob',      accent: '#ffb020', title: 'Solar Disc & Limb Darkening', icon: 'sun',
        cfg: { key: 'sky.disc', label: 'Angular Diameter', unit: '°', min: 0.1, max: 4, step: 0.005, value: 0.533,
               sub: 'Astronomical mean · drives shadow penumbra softness' } }
    ]
  },

  /* ================= 3. HEIGHT FOG ================= */
  {
    id: 'fog', folder: 'world', name: 'Exponential Height Fog', label: 'Valley Fog Volume',
    icon: 'fog', accent: '#7f8ea3', mobility: 'movable', visible: true, locked: false,
    transform: { loc: [120, -480, 24], rot: [0, 0, 0], scale: [1, 1, 1] },
    cards: [
      { kind: 'falloffCurve', accent: '#7f8ea3', title: 'Height Density Falloff', icon: 'activity',
        cfg: { key: 'fog.fall', a: 240, xUnit: 'm', yUnit: 'ρ', xMax: 900, yMax: 1, yDp: 2,
               xLabel: 'Altitude above datum', yLabel: 'RELATIVE DENSITY', curve: 'exp' } },
      { kind: 'windPad',      accent: '#4cc9f0', title: 'Fog Drift Vector Field', icon: 'wind',
        cfg: { key: 'fog.wind', ax: 0.42, ay: -0.66, xLabel: 'Drift X', yLabel: 'Drift Y',
               readout: 'speed', speedUnit: 'm/s', speedScale: 14.2 } },
      { kind: 'spectrumBar',  accent: '#8b7cf6', title: 'Inscatter Luminance Tint', icon: 'eyeDropper',
        cfg: { key: 'fog.tint', mode: 'tint', value: 6800, min: 1500, max: 14000,
               stops: ['#3a2a1c','#8a5a2a','#d9d4cc','#bcd4ea','#8fb4e0','#6f9ad6'] } },
      { kind: 'sliderStack',  accent: '#3ddc97', title: 'Volumetric Integration', icon: 'sliders',
        cfg: { key: 'fog.vol', rows: [
          { k: 'dens',  label: 'Base Density',      min: 0,   max: 0.4, step: 0.002, v: 0.086, unit: '',  dp: 3 },
          { k: 'step',  label: 'Ray Step Size',     min: 0.5, max: 16,  step: 0.1,   v: 4.2,   unit: 'm', dp: 1 },
          { k: 'samples',label:'Max Steps / Ray',   min: 16,  max: 256, step: 8,     v: 96,    unit: '',  dp: 0 },
          { k: 'albedo',label: 'Single-Scatter Albedo', min: 0, max: 1, step: 0.01,  v: 0.72,  unit: '',  dp: 2 },
          { k: 'start', label: 'Fog Start Offset',  min: 0,   max: 400, step: 1,     v: 62,    unit: 'm', dp: 0 }
        ] } }
    ]
  },

  /* ================= 4. FOREST ================= */
  {
    id: 'forest', folder: 'nature', name: 'Foliage Instancer', label: 'Blackpine Forest',
    icon: 'tree', accent: '#3ddc97', mobility: 'static', visible: true, locked: false,
    transform: { loc: [-1840, 2260, 12], rot: [0, 0, 0], scale: [1, 1, 1] },
    cards: [
      { kind: 'densityGrid',  accent: '#3ddc97', title: 'Canopy Stem Density Matrix', icon: 'grid' },
      { kind: 'windPad',      accent: '#a3e635', title: 'Wind & Gust Field', icon: 'wind',
        cfg: { key: 'forest.wind', ax: -0.55, ay: 0.30, xLabel: 'Gust X', yLabel: 'Gust Z',
               readout: 'gust', speedUnit: 'm/s', speedScale: 22.5 } },
      { kind: 'layerStack',   accent: '#7f8ea3', title: 'Undergrowth Strata Coverage', icon: 'stack',
        cfg: { key: 'forest.strata', layers: [
          { name: 'Canopy · Blackpine',  w: 0.82, c: '#2f7d4f', meta: '18 inst/m²' },
          { name: 'Shrub · Heather',     w: 0.47, c: '#5aa05c', meta: '34 inst/m²' },
          { name: 'Fern Floor',          w: 0.63, c: '#86b45a', meta: '51 inst/m²' },
          { name: 'Moss & Leaf Litter',  w: 0.91, c: '#7a6b45', meta: '2 inst/m²' }
        ] } },
      { kind: 'lodPyramid',   accent: '#f5a524', title: 'Foliage LOD Chain', icon: 'triangle' },
      { kind: 'rtBudgetGraph',accent: '#4cc9f0', title: 'Instance Cull & Draw Budget', icon: 'cpu',
        cfg: { key: 'forest.cull', target: 2.40, unit: 'ms', label: 'GPU Cull Pass',
               series: [1.12,1.18,1.09,1.24,1.31,1.27,1.42,1.38,1.51,1.46,1.62,1.58,1.71,1.66,1.79,1.84,1.77,1.92,1.88,2.01] } }
    ]
  },

  /* ================= 5. OCEAN ================= */
  {
    id: 'ocean', folder: 'nature', name: 'Water Body · Ocean', label: 'Northern Reach',
    icon: 'waves', accent: '#2dd4bf', mobility: 'movable', visible: true, locked: false,
    transform: { loc: [0, 0, -140], rot: [0, 0, 0], scale: [1, 1, 1] },
    cards: [
      { kind: 'waveSpectrum', accent: '#2dd4bf', title: 'Gerstner Wave Spectrum', icon: 'activity' },
      { kind: 'polarRose',    accent: '#5b9dff', title: 'Directional Wave Rose', icon: 'target' },
      { kind: 'layerStack',   accent: '#0ea5e9', title: 'Depth Absorption Bands', icon: 'droplet',
        cfg: { key: 'ocean.depth', mode: 'depth', layers: [
          { name: 'Surface · Foam',       w: 0.94, c: '#9fd8e8', meta: '0 – 0.5 m' },
          { name: 'Shallow · Teal',       w: 0.68, c: '#2dd4bf', meta: '0.5 – 3 m' },
          { name: 'Mid · Cobalt',         w: 0.41, c: '#2563eb', meta: '3 – 12 m' },
          { name: 'Abyssal · Absorbed',   w: 0.14, c: '#0b1a33', meta: '12 m +' }
        ] } },
      { kind: 'causticsPlate',accent: '#4cc9f0', title: 'Subsurface Caustics', icon: 'sparkles' }
    ]
  },

  /* ================= 6. TERRAIN ================= */
  {
    id: 'terrain', folder: 'nature', name: 'Landscape', label: 'Highland Massif',
    icon: 'mountain', accent: '#a3825c', mobility: 'static', visible: true, locked: false,
    transform: { loc: [0, 0, 0], rot: [0, 0, 0], scale: [100, 100, 340] },
    cards: [
      { kind: 'heightProfile',accent: '#a3825c', title: 'Elevation Profile Spline', icon: 'mountain' },
      { kind: 'layerStack',   accent: '#3ddc97', title: 'Paint Layer Weights', icon: 'layers',
        cfg: { key: 'terrain.paint', mode: 'paint', layers: [
          { name: 'Alpine Grass', w: 0.58, c: '#6a9b45', meta: 'Layer 0' },
          { name: 'Wet Rock',     w: 0.74, c: '#5c5f66', meta: 'Layer 1' },
          { name: 'Scree Gravel', w: 0.33, c: '#8a8378', meta: 'Layer 2' },
          { name: 'Snow Cap',     w: 0.86, c: '#e6edf5', meta: 'Layer 3' }
        ] } },
      { kind: 'rtBudgetGraph',accent: '#f5a524', title: 'Virtual Texture Streaming', icon: 'cpu',
        cfg: { key: 'terrain.vt', target: 3.00, unit: 'MB/f', label: 'Page Upload',
               series: [1.4,1.9,1.6,2.3,2.1,2.8,2.4,3.1,2.6,3.4,2.9,3.6,3.1,2.7,3.3,2.8,3.5,3.0,2.6,3.2] } },
      { kind: 'statTiles',    accent: '#4cc9f0', title: 'Landscape Topology', icon: 'grid',
        cfg: { key: 'terrain.stat', tiles: [
          { k: 'Components',    v: '256',    u: '' },
          { k: 'Quads / Comp',  v: '63×63',  u: '' },
          { k: 'Total Tris',    v: '2.03',   u: 'M' },
          { k: 'Z Scale',       v: '340',    u: 'uu' },
          { k: 'Collision',     v: 'Per-Poly', u: '' },
          { k: 'Nanite',        v: 'Enabled', u: '' }
        ] } }
    ]
  },

  /* ================= 7. CAMPFIRE (POINT LIGHT) ================= */
  {
    id: 'campfire', folder: 'lights', name: 'Point Light', label: 'Campfire Embers',
    icon: 'flame', accent: '#fb923c', mobility: 'movable', visible: true, locked: false,
    transform: { loc: [412, -88, 96], rot: [0, 0, 0], scale: [1, 1, 1] },
    cards: [
      { kind: 'flickerTrace', accent: '#fb923c', title: 'Procedural Flame Flicker', icon: 'activity' },
      { kind: 'falloffCurve', accent: '#f5a524', title: 'Radial Attenuation Curve', icon: 'gauge',
        cfg: { key: 'fire.att', a: 640, xUnit: 'm', yUnit: 'cd', xMax: 1400, yMax: 1200, yDp: 0,
               xLabel: 'Distance from emitter', yLabel: 'CANDELA', curve: 'inv2' } },
      { kind: 'spectrumBar',  accent: '#ff6b3d', title: 'Flame Emission Spectrum', icon: 'thermometer',
        cfg: { key: 'fire.temp', mode: 'kelvin', value: 1850, min: 1200, max: 3600,
               stops: ['#5a1a06','#a83410','#e0631a','#ff9d3c','#ffc46b','#ffe6b0'] } },
      { kind: 'toggleList',   accent: '#3ddc97', title: 'Emitter Behaviour', icon: 'settings',
        cfg: { key: 'fire.tog', rows: [
          { k: 'shadow',   label: 'Cast Dynamic Shadows', v: true },
          { k: 'ies',      label: 'IES Photometric Profile', v: false },
          { k: 'ember',    label: 'Spawn Ember Particles', v: true },
          { k: 'audio',    label: 'Attenuated Audio Cue', v: true },
          { k: 'lightfn',  label: 'Affect Local Light Function', v: false }
        ] } }
    ]
  },

  /* ================= 8. WATCHTOWER (SPOT LIGHT) ================= */
  {
    id: 'watchtower', folder: 'lights', name: 'Spot Light', label: 'Watchtower Beacon',
    icon: 'spotlight', accent: '#f5e663', mobility: 'movable', visible: true, locked: false,
    transform: { loc: [-620, 1440, 1180], rot: [-38, 118, 0], scale: [1, 1, 1] },
    cards: [
      { kind: 'spotCone',     accent: '#f5e663', title: 'Cone & Penumbra Wedge', icon: 'triangle' },
      { kind: 'dialKnob',     accent: '#4cc9f0', title: 'Beam Intensity', icon: 'zap',
        cfg: { key: 'spot.int', label: 'Luminous Intensity', unit: 'cd', min: 500, max: 24000, step: 50, value: 9800,
               sub: 'Candela at 0° incidence' } },
      { kind: 'falloffCurve', accent: '#fb923c', title: 'Beam Throw Attenuation', icon: 'gauge',
        cfg: { key: 'spot.att', a: 2600, xUnit: 'm', yUnit: 'cd', xMax: 3200, yMax: 9800, yDp: 0,
               xLabel: 'Throw distance', yLabel: 'CANDELA', curve: 'inv2' } },
      { kind: 'toggleList',   accent: '#8b7cf6', title: 'Projection & Shadows', icon: 'settings',
        cfg: { key: 'spot.tog', rows: [
          { k: 'tex',      label: 'Light Projection Texture', v: true },
          { k: 'shadow',   label: 'Cast Shadows', v: true },
          { k: 'softedge', label: 'Soft Source Radius', v: true },
          { k: 'barn',     label: 'Barn Door Masking', v: false },
          { k: 'haze',     label: 'Contribute to Volumetric Haze', v: true }
        ] } }
    ]
  },

  /* ================= 9. HERO CAMERA ================= */
  {
    id: 'camera', folder: 'cine', name: 'Cine Camera Actor', label: 'Hero 35mm Prime',
    icon: 'camera', accent: '#c9d4e3', mobility: 'movable', visible: false, locked: false,
    transform: { loc: [860, -1240, 210], rot: [-6.4, 148.2, 0], scale: [1, 1, 1] },
    cards: [
      { kind: 'frustumPad',   accent: '#4cc9f0', title: 'Frustum & Field of View', icon: 'move' },
      { kind: 'irisAperture', accent: '#f5a524', title: 'Iris Blades & Exposure', icon: 'aperture' },
      { kind: 'keyframeTrack',accent: '#ff6b8a', title: 'Sequence Keyframe Track', icon: 'film' },
      { kind: 'sliderStack',  accent: '#c9d4e3', title: 'Lens & Sensor Optics', icon: 'sliders',
        cfg: { key: 'cam.opt', rows: [
          { k: 'focal',  label: 'Focal Length',     min: 12,  max: 200, step: 1,    v: 35,   unit: 'mm', dp: 0 },
          { k: 'focus',  label: 'Focus Distance',   min: 0.5, max: 120, step: 0.1,  v: 8.4,  unit: 'm',  dp: 1 },
          { k: 'dof',    label: 'Depth of Field',   min: 0,   max: 1,   step: 0.01, v: 0.62, unit: '',   dp: 2 },
          { k: 'shutter',label: 'Shutter Angle',    min: 20,  max: 360, step: 1,    v: 180,  unit: '°',  dp: 0 },
          { k: 'nd',     label: 'ND Filter',        min: 0,   max: 2.4, step: 0.3,  v: 0.9,  unit: 'EV', dp: 1 }
        ] } }
    ]
  },

  /* ================= 10. CITADEL KEEP (STATIC MESH) ================= */
  {
    id: 'citadel', folder: 'actors', name: 'Static Mesh Actor', label: 'Citadel Keep · LOD0',
    icon: 'castle', accent: '#a3825c', mobility: 'static', visible: true, locked: true,
    transform: { loc: [0, 0, 0], rot: [0, 0, 0], scale: [1, 1, 1] },
    cards: [
      { kind: 'lodPyramid',   accent: '#f5a524', title: 'Mesh LOD Chain', icon: 'triangle',
        cfg: { key: 'citadel.lod', mode: 'mesh', lods: [
          { name: 'LOD0', tris: 284610, screen: 1.00 },
          { name: 'LOD1', tris: 142180, screen: 0.50 },
          { name: 'LOD2', tris: 61440,  screen: 0.24 },
          { name: 'LOD3', tris: 18920,  screen: 0.10 },
          { name: 'Imposter', tris: 2,  screen: 0.02 }
        ] } },
      { kind: 'collisionShell',accent: '#4cc9f0', title: 'Collision Hull', icon: 'hexagon' },
      { kind: 'materialSlots', accent: '#8b7cf6', title: 'Material Slots', icon: 'layers' },
      { kind: 'statTiles',     accent: '#3ddc97', title: 'Geometry Report', icon: 'box',
        cfg: { key: 'citadel.stat', tiles: [
          { k: 'Triangles',   v: '284.6', u: 'k' },
          { k: 'Vertices',    v: '162.3', u: 'k' },
          { k: 'UV Channels', v: '3',     u: '' },
          { k: 'Lightmap Res',v: '2048',  u: 'px' },
          { k: 'Bounds Radius', v: '412', u: 'm' },
          { k: 'Nanite',      v: 'Disabled', u: '' }
        ] } }
    ]
  },

  /* ================= 11. KNIGHT (SKELETAL MESH) ================= */
  {
    id: 'knight', folder: 'actors', name: 'Skeletal Mesh Actor', label: 'Knight · Vanguard',
    icon: 'shield', accent: '#ff6b8a', mobility: 'movable', visible: true, locked: false,
    transform: { loc: [180, -60, 0], rot: [0, 212, 0], scale: [1, 1, 1] },
    cards: [
      { kind: 'skeletonRig',  accent: '#ff6b8a', title: 'Skeleton Rig', icon: 'bone' },
      { kind: 'animStrip',    accent: '#f5a524', title: 'Montage Frame Strip', icon: 'film' },
      { kind: 'dialKnob',     accent: '#4cc9f0', title: 'Capsule & Movement', icon: 'gauge',
        cfg: { key: 'knight.move', label: 'Max Walk Speed', unit: 'cm/s', min: 120, max: 900, step: 5, value: 420,
               sub: 'Ground locomotion · root motion driven' } },
      { kind: 'toggleList',   accent: '#8b7cf6', title: 'Physics & Animation', icon: 'settings',
        cfg: { key: 'knight.tog', rows: [
          { k: 'ragdoll', label: 'Ragdoll on Death', v: true },
          { k: 'cloth',   label: 'Cloth Sim · Cape', v: true },
          { k: 'ik',      label: 'Foot IK on Slopes', v: true },
          { k: 'rootmot', label: 'Root Motion Authority', v: false },
          { k: 'update',  label: 'Update Rate Optimisation', v: true }
        ] } }
    ]
  },

  /* ================= 12. POST PROCESS VOLUME ================= */
  {
    id: 'post', folder: 'post', name: 'Post Process Volume', label: 'ACES Cinematic Grade',
    icon: 'aperture', accent: '#a78bfa', mobility: 'static', visible: true, locked: false,
    transform: { loc: [0, 0, 0], rot: [0, 0, 0], scale: [1, 1, 1] },
    cards: [
      { kind: 'tonemapCurve', accent: '#a78bfa', title: 'ACES Filmic Tonemap Curve', icon: 'activity' },
      { kind: 'rgbHistogram', accent: '#ff6b8a', title: 'Live RGB Histogram', icon: 'activity' },
      { kind: 'dialKnob',     accent: '#ffb020', title: 'Bloom Convolution', icon: 'sparkles',
        cfg: { key: 'post.bloom', label: 'Bloom Intensity', unit: '', min: 0, max: 4, step: 0.01, value: 1.24,
               sub: '6 mip convolution · threshold 1.0' } },
      { kind: 'sliderStack',  accent: '#4cc9f0', title: 'Grade & Optics', icon: 'sliders',
        cfg: { key: 'post.grade', rows: [
          { k: 'exp',   label: 'Exposure Compensation', min: -3, max: 3,    step: 0.05, v: 0.35, unit: 'EV', dp: 2 },
          { k: 'con',   label: 'Contrast',              min: 0,  max: 2,    step: 0.01, v: 1.12, unit: '',   dp: 2 },
          { k: 'sat',   label: 'Saturation',            min: 0,  max: 2,    step: 0.01, v: 0.94, unit: '',   dp: 2 },
          { k: 'vig',   label: 'Vignette',              min: 0,  max: 1,    step: 0.01, v: 0.28, unit: '',   dp: 2 },
          { k: 'grain', label: 'Film Grain',            min: 0,  max: 1,    step: 0.01, v: 0.16, unit: '',   dp: 2 },
          { k: 'ca',    label: 'Chromatic Aberration',  min: 0,  max: 4,    step: 0.05, v: 0.60, unit: 'px', dp: 2 }
        ] } }
    ]
  }
];

/* ---- shared, mutable runtime state keyed by card `key` ---- */
const S = {
  sun: {
    az: 42.8, el: 58.2, lux: 120450, threshold: 78, kelvin: 5600, autoK: true,
    splits: [0.06, 0.24, 0.52], cascadeRes: 4096, angular: 0.533,
    time: 13.7, playing: false, speed: 60,
    rt: [0.31,0.34,0.29,0.38,0.42,0.36,0.44,0.41,0.47,0.39,0.43,0.38,0.45,0.40,0.36,0.42,0.37,0.41,0.35,0.38],
    rtTarget: 0.60
  },
  sky:   { disc: 0.533, haze: 0.42 },
  fog:   {}, forest: {}, ocean: {}, terrain: {},
  campfire: { flick: 1.0 }, watchtower: {}, camera: {},
  citadel: {}, knight: {}, post: {}
};

function ent(id) { return ENTITIES.find(e => e.id === id); }

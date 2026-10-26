// Frontier — Environment Editor (Phase 1: SDF terrain)
//
// One editor, one brush, three systems. This phase ships the terrain core:
//   • true SDF volume (overhangs, caves, arches), sedimentary geology fields
//   • unified 3D brush: add / carve / smooth / flatten / terrace / erode /
//     deposit / paint soil — the same brush object (centre, radius, strength,
//     falloff) drives Water (sources, channel) and Vegetation (density,
//     erase) when those outliner systems come online in Phases 2–3
//   • SDF-native erosion: differential weathering + block fall (the real
//     cliff-makers), surface droplet hydraulics, loose-material thermal,
//     stream-power fluvial on the extracted drainage
//   • outputs the later phases consume: extractBedForFluid() in FluidSim's
//     exact layout, flow/moisture maps, vegetation suitability map

import { SdfVolume, buildEscarpmentScene, cliffLineX, dimsForResolution } from './volume.js';
import { Geology, fillGeology } from './geology.js';
import {
  differentialWeathering, blockFall, thermalLoose, hydraulicDroplets,
  fluvialIncision, computeDrainage, updateFlowMaps, computeSuitability, brushErode,
  scatterOutcrops, carvePath,
} from './erosion.js';
import { buildChunkMesh, exportOBJ } from './mesher.js';
import { Renderer } from './renderer.js';
import { EditorCamera } from './camera.js';
import { EditorUI } from './ui.js';
import { clamp, hexToRgb } from './noise.js';

const nextFrame = () => new Promise((r) => requestAnimationFrame(r));

// ------------------------------------------------------------------ state
const S = {
  // SDF grid (stage: regen). resolution = cells across the 96 m patch.
  resolution: 96, preset: 'escarpment',
  // scene / geology (stage: regen | geology)
  seed: 428, landform: 'escarpment', plateauHeight: 40, plainHeight: 12, cliffX: -10, cliffWidth: 4.8,
  reliefFrequency: 1, reliefContrast: 0.65, warpStrength: 0.45, ridgeSharpness: 1,
  mesaRadius: 29, canyonWidth: 13, canyonDepth: 24,
  stackScale: 17, stackHeight: 16, duneHeight: 6, duneWavelength: 18, duneDirection: 30,
  cave: 1, caveRadius: 2.4, fallenBlocks: 6, openingErosion: 1,
  bedScale: 1, dip: 4, dipDirection: 35, lateral: 0.55, fold: 2.5, faultThrow: 0, faultPos: -18,
  jointWidth: 0.55, jointSpacing: 1,
  // brush + path tools
  tool: 'carve', brushRadius: 6, brushStrength: 0.65, brushSmooth: 1.2, brushSpacing: 0.35, brushFalloff: 1, showBrush: 1,
  pathWidth: 4.5, pathBankWidth: 2.5, pathDepth: 1.1, pathEmbankment: 0.18, pathFlatten: 0.9, pathSediment: 0.75, pathLevee: 0, pathMinSlopePct: 1.8,
  // rocks & outcrops
  rockCount: 48, rockSeed: 1234, rockScale: 1.7, rockStretch: 1.35, rockSharpness: 0.68, rockMaxSlope: 38,
  rockMinSpacing: 7, boulderShare: 0.38, rockCover: 0.34, preferHorizon: 0, horizonY: 24, horizonWidth: 5,
  // erosion
  weatherIter: 16, weatherRate: 0.16, weatherPower: 1.6, weatherJoints: 1.4,
  blockMax: 28, blockStrength: 0.85,
  droplets: 6000, dropletInertia: 0.25, dropletCapacity: 0.5, dropletErode: 0.30, dropletDeposit: 0.35, dropletEvap: 0.012, dropletLifetime: 70, hardnessInfluence: 0.85,
  thermalIter: 14, thermalRepose: 34,
  fluvialIter: 6, fluvialStrength: 0.09, fluvialConcavity: 0.5, fluvialDeposition: 0.5, fluvialMinArea: 35,
  // view + material (stage: live)
  debugMode: 0, sunAzimuth: 135, sunElevation: 42, sunIntensity: 1.45, caveFill: 0.38,
  fogDensity: 0.0016, sedimentStrength: 1, moistureDarken: 0.8, jointShade: 0.8, grain: 0.05, fov: 50,
  aoStrength: 0.85, exposure: 1, saturation: 1, strataContrast: 1, hemiStrength: 0.55,
  soilColor: '#5d4632', rockTint: '#ffffff', showWaterLevel: 0, waterLevel: 8,
  cameraBookmark: 'opening', flySpeed: 14,
};
const GEO_KEYS = ['seed', 'bedScale', 'dip', 'dipDirection', 'lateral', 'fold', 'faultThrow', 'faultPos', 'jointWidth', 'jointSpacing'];
const SCENE_KEYS = [
  'seed', 'landform', 'plateauHeight', 'plainHeight', 'cliffX', 'cliffWidth', 'reliefFrequency', 'reliefContrast',
  'warpStrength', 'ridgeSharpness', 'mesaRadius', 'canyonWidth', 'canyonDepth', 'stackScale', 'stackHeight',
  'duneHeight', 'duneWavelength', 'duneDirection', 'cave', 'caveRadius', 'fallenBlocks', ...GEO_KEYS,
];
const STRING_KEYS = new Set(['tool', 'landform', 'preset', 'soilColor', 'rockTint', 'cameraBookmark']);
const DEFAULT_STATE = { ...S };

const RESOLUTION_PRESETS = [[48, '48 · 2.00 m'], [72, '72 · 1.33 m'], [96, '96 · 1.00 m'], [120, '120 · 0.80 m'], [144, '144 · 0.67 m'], [168, '168 · 0.57 m'], [192, '192 · 0.50 m']];
function resolutionSummary(cells = S.resolution) {
  const d = dimsForResolution(cells);
  const nodes = d.nx * d.ny * d.nz;
  // sdf f32 + sediment f32 + hardness/joint/bed u8 (+ transient meshing copies)
  const mb = nodes * (4 + 4 + 1 + 1 + 1) / 1048576;
  return { dims: d, nodes, mb, text: `${d.nx}×${d.ny}×${d.nz} nodes · ${d.spacing.toFixed(2)} m voxels · ~${mb.toFixed(0)} MB fields` };
}

const PRESETS = {
  escarpment: { label: 'Sedimentary escarpment + cave', values: { landform: 'escarpment', seed: 428, plateauHeight: 40, plainHeight: 12, cliffX: -10, cliffWidth: 4.8, cave: 1, caveRadius: 2.4, fallenBlocks: 6, dip: 4, fold: 2.5, faultThrow: 0 } },
  coastal: { label: 'Coastal escarpment & sea stacks', values: { landform: 'coastal', seed: 612, plateauHeight: 38, plainHeight: 10, cliffX: -16, cliffWidth: 4.2, cave: 1, caveRadius: 2.6, fallenBlocks: 8, dip: 3, fold: 2, showWaterLevel: 1, waterLevel: 7 } },
  mesa: { label: 'Island mesa', values: { landform: 'mesa', seed: 204, plateauHeight: 42, plainHeight: 11, mesaRadius: 29, cliffWidth: 4.4, cave: 1, fallenBlocks: 8, dip: 2, fold: 1.6 } },
  canyon: { label: 'Incised canyon', values: { landform: 'canyon', seed: 733, plateauHeight: 43, plainHeight: 12, canyonWidth: 13, canyonDepth: 26, cave: 0, fallenBlocks: 4, dip: 5, fold: 2.2 } },
  arch: { label: 'Rock fin & through-arch', values: { landform: 'arch', seed: 318, plateauHeight: 39, plainHeight: 12, cliffX: -4, ridgeSharpness: 1.15, cave: 1, caveRadius: 2.8, fallenBlocks: 5, dip: 3, fold: 1.8 } },
  stacks: { label: 'Hoodoo / tower field', values: { landform: 'stacks', seed: 509, plateauHeight: 36, plainHeight: 11, stackScale: 16, stackHeight: 18, cave: 0, fallenBlocks: 3, bedScale: 0.82, dip: 1.5, fold: 1.2 } },
  dunes: { label: 'Dune field over bedrock', values: { landform: 'dunes', seed: 845, plateauHeight: 30, plainHeight: 12, duneHeight: 6.5, duneWavelength: 18, cave: 0, fallenBlocks: 0, openingErosion: 0, dip: 1 } },
  quarry: { label: 'Benched quarry / pit', values: { landform: 'quarry', seed: 156, plateauHeight: 34, plainHeight: 13, cave: 0, fallenBlocks: 2, dip: 2.5, fold: 0.8 } },
  faulted: { label: 'Faulted escarpment', values: { landform: 'escarpment', seed: 927, plateauHeight: 41, plainHeight: 11, cliffX: -8, cave: 1, faultThrow: 5.5, faultPos: -14, dip: 7, fold: 3.2, fallenBlocks: 9 } },
};

const TOOLS = [
  { id: 'navigate', label: 'Navigate', title: 'Camera only (V). LMB orbit / fly-look, MMB pan, wheel zoom.' },
  { id: 'add', label: 'Add rock', title: 'Smooth-union a rock sphere. Builds buttresses, arches, overhangs (1).' },
  { id: 'carve', label: 'Carve', title: 'Smooth-subtract a sphere. Digs caves, tunnels, notches (2).' },
  { id: 'smooth', label: 'Smooth', title: 'Relax the SDF inside the brush (3).' },
  { id: 'flatten', label: 'Flatten', title: 'Blend to the plane touched at stroke start (4).' },
  { id: 'terrace', label: 'Terrace', title: 'Blend to a horizontal bench at stroke-start height (5).' },
  { id: 'erode', label: 'Erode', title: 'Local differential weathering + surface droplets. Attacks soft beds & joints first (6).' },
  { id: 'deposit', label: 'Deposit', title: 'Add loose material (sediment). Slumps under Thermal, feeds vegetation soil (7).' },
  { id: 'soil', label: 'Paint soil', title: 'Paint soil depth without changing shape (8). Shift erases.' },
  { id: 'river', label: 'River path', title: 'Draw a polyline on the surface, then double-click / Enter to carve a downhill channel (9). Esc cancels.' },
  { id: 'road', label: 'Road path', title: 'Draw a polyline on the surface, then double-click / Enter to cut a level road bench (0). Esc cancels.' },
];
const TOOL_COLORS = {
  add: [0.55, 0.95, 0.55], deposit: [0.85, 0.7, 0.4], carve: [1, 0.45, 0.4], erode: [1, 0.75, 0.3],
  smooth: [0.6, 0.8, 1], flatten: [0.8, 0.8, 1], terrace: [0.8, 0.8, 1], soil: [0.65, 0.9, 0.45], navigate: [1, 1, 1],
  river: [0.3, 0.65, 1], road: [0.95, 0.8, 0.45],
};
const DEBUG_MODES = [[0, 'Shaded'], [1, 'Strata beds'], [2, 'Hardness'], [3, 'Joint sets'], [4, 'Moisture / flow'], [5, 'Sediment / soil'], [6, 'Veg. suitability'], [7, 'Normals'], [8, 'Baked SDF AO']];

// ------------------------------------------------------------------ runtime
let volume = null, geology = null, renderer = null, ui = null;
const camera = new EditorCamera();
const meshes = new Map();
let totalTriangles = 0;
let busy = false;
let hoverHit = null, hoverNdc = null, pointerOnCanvas = false;
let stroke = null, navDrag = null;
const keys = new Set();
const undoStack = [], redoStack = [];
let lastBedStats = null, lastFlowStats = null, lastSuitability = null, lastRockStats = null, lastPathStats = null;
let erosionLog = [];
let pathDraft = [];

function pickParams(keys) { const o = {}; for (const k of keys) o[k] = S[k]; return o; }

// ------------------------------------------------------------------ UI defs
const slider = (key, label, min, max, step, extra = {}) => ({ type: 'slider', key, label, min, max, step, ...extra });
const DEFS = {
  sections: [
    { label: 'World', ids: ['terrain', 'geology', 'features', 'erosion', 'water', 'vegetation'] },
    { label: 'Editor', ids: ['brush', 'material', 'view', 'export'] },
  ],
  tools: TOOLS,
  groups: {
    terrain: {
      title: 'Terrain SDF', sub: 'True 3D volume · 97×65×97 nodes', icon: 'mountain', eyebrow: 'World',
      intro: 'One signed-distance volume is the whole terrain — no heightfield underneath. Carve goes through the mountain; Add builds overhangs and arches. Resolution changes the actual voxel grid, not just the mesh.',
      cards: [
        { title: 'Patch & SDF resolution', icon: 'grid', meta: 'Generate to apply', controls: [
          { type: 'stats', stats: [
            { id: 'statRes', label: 'Grid (nodes)' }, { id: 'statVoxel', label: 'Voxel size' },
            { id: 'statTris', label: 'Triangles' }, { id: 'statChunks', label: 'Active chunks' },
            { id: 'statSolid', label: 'Rock volume' }, { id: 'statCave', label: 'Cave column crossings' },
          ]},
          { type: 'segmented', key: 'resolution', label: 'Resolution preset (cells across · voxel size)', options: RESOLUTION_PRESETS, stage: 'regen' },
          slider('resolution', 'SDF resolution — cells across 96 m', 48, 192, 24, { stage: 'regen', fmt: (v) => resolutionSummary(v).text, minLabel: '48 · fast / coarse', maxLabel: '192 · slow / fine' }),
          { type: 'html', html: '<div id="resolution-summary" class="readout"></div>' },
          { type: 'buttons', buttons: [
            { action: 'generate', label: 'Generate at this resolution', primary: true },
            { action: 'redistance', label: 'Redistance field' },
          ]},
        ], note: 'Vertical cells follow at the same voxel size over the 64 m height. At 48 cells the cave radius is raised to the smallest tunnel the grid can hold; at 168–192 expect several million nodes and a slower first mesh.' },
        { title: 'Presets', icon: 'layers', controls: [
          { type: 'select', key: 'preset', label: 'Terrain preset', options: Object.entries(PRESETS).map(([id, p]) => [id, p.label]) },
          { type: 'buttons', buttons: [
            { action: 'applyPreset', label: 'Apply preset + Generate', primary: true },
            { action: 'randomSeed', label: 'Randomize seed' },
          ]},
        ], note: 'Presets are starting points for the same geology + erosion pipeline, not different terrain systems.' },
        { title: 'Landform', icon: 'mountain', meta: 'Generate to apply', controls: [
          { type: 'select', key: 'landform', label: 'Landform', stage: 'regen', options: [
            ['escarpment', 'Escarpment (cuesta + plain)'], ['coastal', 'Coastal escarpment & sea stacks'],
            ['mesa', 'Island mesa'], ['canyon', 'Incised canyon'], ['arch', 'Rock fin (arch-ready)'],
            ['stacks', 'Hoodoo / tower field'], ['dunes', 'Dune field'], ['quarry', 'Benched quarry / pit'],
          ]},
          { type: 'number', key: 'seed', label: 'Seed', min: 1, max: 999999, step: 1, stage: 'regen' },
          slider('plateauHeight', 'High ground height', 24, 54, 0.5, { unit: 'm', stage: 'regen' }),
          slider('plainHeight', 'Low ground height', 4, 24, 0.5, { unit: 'm', stage: 'regen' }),
          slider('cliffX', 'Escarpment / fin position', -30, 10, 0.5, { unit: 'm', stage: 'regen' }),
          slider('cliffWidth', 'Cliff transition width', 2, 14, 0.2, { unit: 'm', stage: 'regen', minLabel: 'near-vertical', maxLabel: 'steep slope' }),
          slider('reliefFrequency', 'Relief frequency', 0.4, 2.2, 0.01, { stage: 'regen' }),
          slider('reliefContrast', 'Relief contrast', 0, 1.2, 0.01, { stage: 'regen' }),
          slider('warpStrength', 'Planform warp', 0, 1, 0.01, { stage: 'regen', minLabel: 'straight', maxLabel: 'sinuous' }),
          slider('ridgeSharpness', 'Ridge / face sharpness', 0.4, 2.2, 0.01, { stage: 'regen' }),
        ]},
        { title: 'Landform features', icon: 'mountain', meta: 'used by the matching landform', controls: [
          slider('mesaRadius', 'Mesa radius', 14, 42, 0.5, { unit: 'm', stage: 'regen' }),
          slider('canyonWidth', 'Canyon width', 5, 30, 0.5, { unit: 'm', stage: 'regen' }),
          slider('canyonDepth', 'Canyon depth', 6, 34, 0.5, { unit: 'm', stage: 'regen' }),
          slider('stackScale', 'Tower spacing (stacks)', 9, 30, 0.5, { unit: 'm', stage: 'regen' }),
          slider('stackHeight', 'Tower height', 4, 30, 0.5, { unit: 'm', stage: 'regen' }),
          slider('duneHeight', 'Dune height', 1, 12, 0.25, { unit: 'm', stage: 'regen' }),
          slider('duneWavelength', 'Dune wavelength', 8, 36, 0.5, { unit: 'm', stage: 'regen' }),
          slider('duneDirection', 'Dune travel direction', 0, 360, 1, { unit: '°', stage: 'regen' }),
        ]},
        { title: 'Cave, blocks & opening', icon: 'cave', controls: [
          { type: 'toggle', key: 'cave', label: 'Cave / arch tunnel where the landform hosts one', stage: 'regen' },
          slider('caveRadius', 'Cave radius', 1.4, 4, 0.1, { unit: 'm', stage: 'regen' }),
          slider('fallenBlocks', 'Fallen blocks at foot', 0, 18, 1, { stage: 'regen' }),
          { type: 'toggle', key: 'openingErosion', label: 'Erode opening scene', stage: 'regen' },
        ], note: 'The starting relief is only the first solid. Everything after — brush, weathering, droplets, block fall — edits the 3D field directly.' },
      ],
    },
    geology: {
      title: 'Geology', sub: 'Bed stack · dip/fold · joint sets', icon: 'strata', eyebrow: 'World',
      intro: 'The anti-banding system. Cliffs are hard beds left standing while soft beds and jointed rock are removed by erosion — structure first, texture never.',
      cards: [
        { title: 'Stratigraphy', icon: 'strata', meta: 'Rebuild fields to apply', controls: [
          slider('bedScale', 'Bed thickness scale', 0.5, 2, 0.05, { stage: 'geology' }),
          slider('dip', 'Dip', 0, 14, 0.25, { unit: '°', stage: 'geology' }),
          slider('dipDirection', 'Dip direction', 0, 360, 1, { unit: '°', stage: 'geology' }),
          slider('fold', 'Folding amplitude', 0, 6, 0.1, { unit: 'm', stage: 'geology' }),
          slider('lateral', 'Lateral pinch-out', 0, 0.9, 0.01, { stage: 'geology', minLabel: 'uniform beds', maxLabel: 'strong lensing' }),
          slider('faultThrow', 'Fault throw', -8, 8, 0.25, { unit: 'm', stage: 'geology' }),
          slider('faultPos', 'Fault position', -40, 40, 0.5, { unit: 'm', stage: 'geology' }),
        ], note: 'Beds live in stratigraphic coordinates s = y + dip + fold + fault offset, with per-bed lateral thickness variation. No bed is a world-Y stripe, and none runs forever.' },
        { title: 'Joint sets', icon: 'strata', controls: [
          slider('jointSpacing', 'Joint spacing scale', 0.5, 2.2, 0.05, { stage: 'geology' }),
          slider('jointWidth', 'Joint damaged-zone width', 0.1, 0.8, 0.01, { unit: 'm', stage: 'geology' }),
          { type: 'button', action: 'rebuildGeology', label: 'Rebuild geology fields (keep shape)', wide: true, primary: true },
          { type: 'html', html: '<p class="muted">Three vertical joint families. Spacing ∝ local bed thickness, phase resets at bedding boundaries, persistence masks terminate joints. View them with <b>View → Joint sets</b>; erode and they become slots, chimneys and block boundaries.</p>' },
        ]},
        { title: 'Bed stack', icon: 'layers', controls: [
          { type: 'html', html: '<div id="bed-stack" class="bed-stack">Generate to list beds…</div>' },
        ]},
      ],
    },
    features: {
      title: 'Features', sub: 'Rivers · roads · rocks & outcrops', icon: 'route', eyebrow: 'World',
      intro: 'Drawn and scattered features are real SDF edits: a river is a carved channel in the volume (and therefore in the fluid bed), a road is a cut-and-fill bench, and rocks are unions that erode with everything else.',
      cards: [
        { title: 'Draw a river or road', icon: 'route', controls: [
          { type: 'buttons', buttons: [
            { action: 'toolRiver', label: 'River path tool (9)', primary: true },
            { action: 'toolRoad', label: 'Road path tool (0)' },
          ]},
          slider('pathWidth', 'Channel / road width', 1.5, 14, 0.25, { unit: 'm' }),
          slider('pathBankWidth', 'Bank / shoulder width', 0, 8, 0.25, { unit: 'm' }),
          slider('pathDepth', 'River channel depth', 0.2, 4, 0.05, { unit: 'm' }),
          slider('pathMinSlopePct', 'River minimum fall', 0, 8, 0.1, { unit: '%', minLabel: 'follow terrain', maxLabel: 'force downhill' }),
          slider('pathEmbankment', 'Road embankment', -1, 2, 0.05, { unit: 'm' }),
          slider('pathFlatten', 'Profile flattening', 0.05, 1, 0.01),
          slider('pathSediment', 'Bed / surface sediment paint', 0, 1.5, 0.01),
          slider('pathLevee', 'Bank / levee height', 0, 1.5, 0.05, { unit: 'm' }),
          { type: 'stats', stats: [{ id: 'statPathPoints', label: 'Draft points' }, { id: 'statPathResult', label: 'Last path' }] },
          { type: 'buttons', buttons: [
            { action: 'finishPath', label: 'Finish path (Enter)', primary: true },
            { action: 'undoPathPoint', label: 'Remove last point' },
            { action: 'clearPath', label: 'Clear draft (Esc)' },
          ]},
        ], note: 'Click surface points to draft the polyline (the blue/orange line in the viewport), then double-click or press Enter. Rivers get a smoothed, forced-downhill long profile; roads cut and fill to a smoothed bench.' },
        { title: 'Rocks & outcrops', icon: 'rock', controls: [
          slider('rockCount', 'Rocks to scatter', 0, 160, 1),
          { type: 'number', key: 'rockSeed', label: 'Scatter seed', min: 1, max: 999999, step: 1 },
          slider('rockScale', 'Rock scale', 0.4, 4, 0.05, { unit: 'm' }),
          slider('rockStretch', 'Block stretch', 0.4, 3, 0.01),
          slider('rockSharpness', 'Edge sharpness', 0, 1, 0.01, { minLabel: 'rounded', maxLabel: 'joint-blocky' }),
          slider('boulderShare', 'Boulder share (vs outcrop blocks)', 0, 1, 0.01),
          slider('rockMaxSlope', 'Maximum slope to place on', 5, 75, 1, { unit: '°' }),
          slider('rockMinSpacing', 'Minimum spacing', 2, 20, 0.5, { unit: 'm' }),
          slider('rockCover', 'Sediment dusting on new rock', 0, 1, 0.01),
          { type: 'toggle', key: 'preferHorizon', label: 'Favour one stratigraphic horizon (elevation band)' },
          slider('horizonY', 'Horizon elevation', 4, 54, 0.5, { unit: 'm' }),
          slider('horizonWidth', 'Horizon band width', 1, 14, 0.25, { unit: 'm' }),
          { type: 'stats', stats: [{ id: 'statRocks', label: 'Last scatter placed' }, { id: 'statRockAttempts', label: 'Candidates tried' }] },
          { type: 'button', action: 'scatterRocks', label: 'Scatter rocks & outcrops into the SDF', wide: true, primary: true },
        ], note: 'Outcrops are oriented rounded boxes (flat bedding/joint faces); boulders are rounded unions. Both inherit the bed beneath them, and Ctrl+Z removes a scatter pass.' },
      ],
    },
    erosion: {
      title: 'Erosion', sub: 'Weathering · block fall · water · talus', icon: 'erode', eyebrow: 'World',
      intro: 'The CliffGenerator processes re-derived for a volume. Run the full suite, or drive each process separately and watch the cornices emerge.',
      cards: [
        { title: 'Full suite', icon: 'erode', controls: [
          { type: 'button', action: 'erodeSuite', label: 'Run erosion suite (weather → blocks → rain → talus → rivers)', wide: true, primary: true },
          { type: 'html', html: '<div id="erosion-log" class="log"></div>' },
        ]},
        { title: 'Differential weathering', icon: 'erode', meta: 'the cliff-maker', controls: [
          slider('weatherIter', 'Iterations', 1, 60, 1),
          slider('weatherRate', 'Max retreat / iteration', 0.02, 0.35, 0.005, { unit: 'm' }),
          slider('weatherPower', 'Hardness contrast', 0.5, 3, 0.05),
          slider('weatherJoints', 'Joint exploitation', 0, 3, 0.05),
          { type: 'button', action: 'weather', label: 'Weather cliff faces', wide: true },
        ], note: 'Level-set retreat ∝ (1 − hardness)^p · joints · exposure. Soft shale beds undercut; hard sandstone/limestone beds remain as cornices.' },
        { title: 'Block fall', icon: 'mountain', controls: [
          slider('blockMax', 'Max blocks / pass', 1, 80, 1),
          slider('blockStrength', 'Detachment likelihood', 0.05, 1, 0.01),
          { type: 'button', action: 'blockfall', label: 'Shed undercut blocks', wide: true },
        ], note: 'Hard, jointed, unsupported surface rock detaches in bed-thickness-sized blocks and lands at the foot as loose material — volume is moved, not deleted.' },
        { title: 'Hydraulic droplets (on the surface)', icon: 'waves', controls: [
          slider('droplets', 'Droplets', 500, 30000, 250),
          slider('dropletInertia', 'Inertia', 0, 0.8, 0.01),
          slider('dropletCapacity', 'Sediment capacity', 0.05, 1.5, 0.01),
          slider('dropletErode', 'Erode speed', 0.02, 0.8, 0.01),
          slider('dropletDeposit', 'Deposit speed', 0.02, 0.8, 0.01),
          slider('dropletEvap', 'Evaporation', 0, 0.06, 0.001),
          slider('dropletLifetime', 'Lifetime', 10, 140, 1),
          slider('hardnessInfluence', 'Hardness influence', 0, 1, 0.01),
          { type: 'button', action: 'droplets', label: 'Rain on the whole patch', wide: true },
        ], note: 'Beyer droplets projected onto the iso-surface: they run down cliff faces, along cave floors and off cornices — not just down a height grid.' },
        { title: 'Thermal / talus', icon: 'mountain', controls: [
          slider('thermalIter', 'Iterations', 1, 60, 1),
          slider('thermalRepose', 'Angle of repose', 24, 48, 0.5, { unit: '°' }),
          { type: 'button', action: 'thermal', label: 'Slump loose material', wide: true },
        ], note: 'Only loose sediment moves. Intact hard beds stand at 80° because they are rock, not because a global talus angle says so.' },
        { title: 'Fluvial incision (stream power)', icon: 'waves', controls: [
          slider('fluvialIter', 'Iterations', 1, 30, 1),
          slider('fluvialStrength', 'Incision K', 0.01, 0.4, 0.005),
          slider('fluvialConcavity', 'Concavity m', 0.2, 0.9, 0.01),
          slider('fluvialDeposition', 'Slack-water deposition', 0, 1, 0.01),
          slider('fluvialMinArea', 'Channel threshold', 5, 200, 1, { unit: 'm²' }),
          { type: 'button', action: 'fluvial', label: 'Incise drainage into volume', wide: true },
          { type: 'stats', stats: [{ id: 'statFlowMax', label: 'Max drainage' }, { id: 'statChannel', label: 'Channel cells' }] },
        ], note: 'Drainage is solved on the extracted top surface (priority-flood, D8 receivers, area accumulation); incision is stamped back into the SDF, so channels can undercut banks.' },
      ],
    },
    water: {
      title: 'Water', sub: '2.5D shallow water · real caustics', icon: 'waves', eyebrow: 'World', badge: 'Phase 2',
      intro: 'Phase 2 plugs in <b>engine/fluid</b> (FluidSim, Frontier water branch) unchanged. The coupling contract already works: the bed is extracted from this volume in exactly the layout <code>FluidSim.setBed()</code> consumes.',
      cards: [
        { title: 'Bed coupling (live now)', icon: 'waves', controls: [
          { type: 'stats', stats: [
            { id: 'statBedRes', label: 'Bed resolution' }, { id: 'statBedRange', label: 'Bed elevation range' },
            { id: 'statBedMean', label: 'Mean bed' }, { id: 'statWet', label: 'Drainage cells > 35 m²' },
          ]},
          { type: 'buttons', buttons: [
            { action: 'extractBed', label: 'Extract bed + flow', primary: true },
            { action: 'exportBed', label: 'Download bed JSON' },
          ]},
          { type: 'toggle', key: 'showWaterLevel', label: 'Show sea-level guide plane', stage: 'live' },
          slider('waterLevel', 'Sea level (guide)', 0, 40, 0.25, { unit: 'm', stage: 'live' }),
          { type: 'html', html: '<p class="muted">The bed follows the current SDF resolution (cells = grid cells, cellSize = voxel size). Instantiate the fluid grid with the exported nx × ny. <code>FluidRenderer.groundWGSL()</code> then gives the terrain shader true caustics, wetness and mud from the simulated surface — the guide plane above is only a visual level, not water.</p>' },
        ]},
        { title: 'Same brush, Phase 2', icon: 'brush', controls: [
          { type: 'html', html: '<ul class="spec-list"><li><b>Carve</b> on terrain re-extracts the bed under the brush; channels appear in the sim as you dig.</li><li><b>Deposit</b> becomes a water source (m³/s ∝ strength) via <code>addSource()</code>.</li><li><b>Erode</b> spawns rain over the brush disc.</li><li>Brush radius/strength/falloff are shared verbatim — one brush object for all systems.</li><li>Known 2.5D limit: water uses the <i>top</i> surface, so it will not flow through cave interiors. Cave drips remain droplet-erosion work.</li></ul>' },
        ]},
      ],
    },
    vegetation: {
      title: 'Vegetation', sub: 'Simulated placement · trees & grass', icon: 'sprout', eyebrow: 'World', badge: 'Phase 3',
      intro: 'Phase 3 brings in the Slate tree/grass generator. Placement is not scatter: suitability is computed from this terrain — slope from ∇SDF, soil from erosion deposition, moisture from drainage.',
      cards: [
        { title: 'Suitability (computed now)', icon: 'sprout', controls: [
          { type: 'stats', stats: [{ id: 'statSuit', label: 'Suitable columns' }, { id: 'statSuitMean', label: 'Mean suitability' }] },
          { type: 'buttons', buttons: [
            { action: 'computeSuitability', label: 'Recompute suitability', primary: true },
            { action: 'viewSuitability', label: 'View suitability' },
          ]},
          { type: 'html', html: '<p class="muted">s = slope · elevation · soil · moisture, with the channel thalweg excluded. Paint soil or run fluvial deposition and watch ground become plantable. Roots in the tree generator already grow against SDF obstacles; here the obstacle field is this volume.</p>' },
        ]},
        { title: 'Same brush, Phase 3', icon: 'brush', controls: [
          { type: 'html', html: '<ul class="spec-list"><li><b>Paint soil</b> raises suitability directly (works today — try the suitability view).</li><li><b>Add</b> paints tree/grass density, accepted only where suitability passes; density is re-simulated, not just stamped.</li><li><b>Carve</b> erases instances; <b>Erode</b> thins by re-running the suitability test.</li><li>Small patch keeps instance counts sane: trees from the parametric generator, grass from the blade mesher, wind data included.</li></ul>' },
        ]},
      ],
    },
    brush: {
      title: 'Brush', sub: 'One brush for terrain · water · vegetation', icon: 'brush', eyebrow: 'Editor',
      intro: 'A 3D sphere brush on the SDF surface. Picking sphere-traces the field, so the brush works on cave walls, arch undersides and cornice lips — anywhere you can see.',
      cards: [
        { title: 'Tool', icon: 'brush', controls: [
          { type: 'segmented', key: 'tool', options: TOOLS.map((t) => [t.id, t.label]) },
        ]},
        { title: 'Brush shape', icon: 'brush', controls: [
          slider('brushRadius', 'Radius', 1.5, 22, 0.25, { unit: 'm' }),
          slider('brushStrength', 'Strength', 0.05, 1, 0.01),
          slider('brushSmooth', 'CSG blend (edge softness)', 0, 4, 0.05, { unit: 'm' }),
          slider('brushFalloff', 'Falloff power (flatten / smooth / soil)', 0.25, 3, 0.01, { minLabel: 'wide & soft', maxLabel: 'tight centre' }),
          slider('brushSpacing', 'Stamp spacing', 0.1, 0.9, 0.01, { fmt: (v) => `${Math.round(v * 100)}% of radius` }),
          { type: 'toggle', key: 'showBrush', label: 'Show brush gizmo' },
          { type: 'html', html: '<p class="muted"><b>River (9)</b> and <b>Road (0)</b> are path tools on this same picking ray: click points, double-click / Enter to carve the ribbon, Esc to cancel. Their width/depth/banking live under <b>Features</b>.</p>' },
        ], note: 'Ctrl+wheel resizes, Shift+wheel changes strength, [ and ] nudge radius. Alt+LMB orbits while a brush tool is active.' },
        { title: 'Under the cursor', icon: 'eye', controls: [
          { type: 'html', html: '<div id="cursor-readout" class="readout">Hover the terrain…</div>' },
        ]},
      ],
    },
    material: {
      title: 'Material', sub: 'Rock & soil response · baked SDF AO', icon: 'palette', eyebrow: 'Editor',
      intro: 'Live shading controls for the baked volume fields. Nothing here invents relief: bed colour comes from the stratigraphy, AO is probed from the SDF itself at mesh time, and soil colour only appears where sediment actually lies.',
      cards: [
        { title: 'Rock & soil', icon: 'palette', controls: [
          { type: 'color', key: 'soilColor', label: 'Soil / sediment colour', stage: 'live' },
          { type: 'color', key: 'rockTint', label: 'Global rock tint', stage: 'live' },
          slider('strataContrast', 'Strata colour contrast', 0.4, 1.8, 0.01, { stage: 'live' }),
          slider('sedimentStrength', 'Soil colour strength', 0, 2, 0.01, { stage: 'live' }),
          slider('moistureDarken', 'Wet darkening', 0, 1.5, 0.01, { stage: 'live' }),
          slider('jointShade', 'Joint seam shading', 0, 1.5, 0.01, { stage: 'live' }),
          slider('grain', 'Matte grain (non-structural)', 0, 0.2, 0.005, { stage: 'live' }),
        ], note: 'Seam shading only darkens existing joint geometry. If a cliff looks striped with grain at 0, the geology — not a texture — is doing it.' },
        { title: 'Occlusion & grade', icon: 'eye', controls: [
          slider('aoStrength', 'Baked SDF AO strength', 0, 1.2, 0.01, { stage: 'live' }),
          slider('hemiStrength', 'Hemisphere (sky/ground) fill', 0, 1.2, 0.01, { stage: 'live' }),
          slider('exposure', 'Exposure', 0.3, 2.2, 0.01, { stage: 'live' }),
          slider('saturation', 'Saturation', 0, 1.8, 0.01, { stage: 'live' }),
          { type: 'buttons', buttons: [
            { action: 'viewAO', label: 'View baked AO' },
            { action: 'viewShaded', label: 'Back to shaded' },
          ]},
        ], note: 'AO is recomputed with the mesh whenever chunks remesh, so newly carved caves and cornices occlude correctly after the stroke.' },
      ],
    },
    view: {
      title: 'View', sub: 'Sun, debug fields, cave light', icon: 'eye', eyebrow: 'Editor',
      cards: [
        { title: 'Debug field', icon: 'eye', controls: [
          { type: 'select', key: 'debugMode', label: 'Viewport mode', options: DEBUG_MODES },
          { type: 'html', html: '<p class="muted">Hardness / joints / moisture / sediment are the actual simulation fields baked per-vertex — the same numbers erosion reads.</p>' },
        ]},
        { title: 'Lighting', icon: 'eye', controls: [
          slider('sunAzimuth', 'Sun azimuth', 0, 360, 1, { unit: '°', stage: 'live' }),
          slider('sunElevation', 'Sun elevation', 4, 85, 1, { unit: '°', stage: 'live' }),
          slider('sunIntensity', 'Sun intensity', 0.2, 3, 0.05, { stage: 'live' }),
          slider('caveFill', 'Cave fill light', 0, 1.5, 0.01, { stage: 'live' }),
          slider('fogDensity', 'Fog density', 0, 0.006, 0.0001, { stage: 'live', fmt: (v) => v.toFixed(4) }),
          slider('fov', 'Field of view', 30, 80, 1, { unit: '°', stage: 'live' }),
        ]},
        { title: 'Camera & guide level', icon: 'camera', controls: [
          { type: 'select', key: 'cameraBookmark', label: 'Camera bookmark', options: [
            ['opening', 'Opening escarpment view'], ['patch', 'Whole patch from SE'], ['cave', 'Cave mouth / arch'],
            ['cliffFoot', 'Cliff foot & talus'], ['plateau', 'High ground & channels'], ['top', 'Plan (top-down)'],
          ]},
          { type: 'button', action: 'gotoBookmark', label: 'Go to bookmark', wide: true, primary: true },
          slider('flySpeed', 'Fly speed', 2, 40, 0.5, { unit: 'm/s', stage: 'live' }),
          { type: 'toggle', key: 'showWaterLevel', label: 'Sea-level guide plane', stage: 'live' },
          slider('waterLevel', 'Guide level', 0, 40, 0.25, { unit: 'm', stage: 'live' }),
          { type: 'html', html: '<p class="muted">The guide is a wire level + submerged-bed tint for coastal/flood planning. Real water arrives with the Phase 2 sim; rock/soil colour controls live under <b>Material</b>.</p>' },
        ]},
      ],
    },
    export: {
      title: 'Project & Export', sub: 'Project · mesh · maps · fluid bed', icon: 'export', eyebrow: 'Editor',
      cards: [
        { title: 'Project', icon: 'layers', controls: [
          { type: 'buttons', buttons: [
            { action: 'exportProject', label: 'Save project JSON', primary: true },
            { action: 'importProject', label: 'Load project JSON…' },
            { action: 'exportParams', label: 'Parameters only' },
          ]},
          { type: 'html', html: '<p class="muted">A project stores every studio parameter (resolution, preset, landform, geology, erosion, material, camera settings). Loading re-applies them and regenerates deterministically from the seed — hand-sculpted strokes after generation are not stored; export the OBJ/SDF for those.</p>' },
        ]},
        { title: 'Terrain deliverables', icon: 'export', controls: [
          { type: 'buttons', buttons: [
            { action: 'exportObj', label: 'Terrain OBJ', primary: true },
            { action: 'exportSdf', label: 'Raw SDF (.f32 + header)' },
            { action: 'exportBed', label: 'FluidSim bed JSON' },
            { action: 'screenshot', label: 'Screenshot PNG' },
          ]},
          { type: 'html', html: '<p class="muted">OBJ is the watertight marching-tetrahedra soup with SDF-gradient normals. The bed JSON drops straight into <code>sim.setBed()</code> after <code>new Float32Array(json.heights)</code>. The raw SDF zip-free pair is a Float32 little-endian grid plus a JSON header describing dims/spacing/origin.</p>' },
        ]},
        { title: 'Field maps (PNG)', icon: 'grid', controls: [
          { type: 'buttons', buttons: [
            { action: 'exportHeightmap', label: 'Top heightmap' },
            { action: 'exportMoisture', label: 'Moisture / flow' },
            { action: 'exportSuitability', label: 'Veg. suitability' },
            { action: 'exportSedimentMap', label: 'Surface sediment' },
          ]},
          { type: 'html', html: '<p class="muted">Top-surface projections of the volume fields at node resolution — the same maps Phases 2–3 consume. Caves/overhangs live in the SDF/OBJ, not in these 2D projections.</p>' },
        ]},
        { title: 'Shortcuts', icon: 'eye', controls: [
          { type: 'html', html: '<ul class="spec-list"><li><b>LMB</b> brush · <b>Alt+LMB / RMB</b> orbit or fly-look · <b>MMB</b> pan · <b>wheel</b> zoom</li><li><b>G</b> orbit ⇄ fly · <b>WASD/QE</b> fly (Shift fast) · <b>F</b> frame · <b>C</b> cave mouth</li><li><b>1–8</b> brushes · <b>9</b> river · <b>0</b> road · <b>V</b> navigate · <b>[ ]</b> radius</li><li>Path tools: click points · <b>Enter / double-click</b> finish · <b>Backspace</b> last point · <b>Esc</b> cancel</li><li><b>Ctrl+Z / Ctrl+Shift+Z</b> undo/redo · <b>Ctrl+Enter</b> generate</li></ul>' },
        ]},
      ],
    },
  },
};

// ------------------------------------------------------------------ helpers
function download(name, blob) {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 4000);
}
function toast(msg) { ui?.showToast(msg); }
function logErosion(msg) {
  erosionLog = [msg, ...erosionLog].slice(0, 7);
  const elx = document.getElementById('erosion-log');
  if (elx) elx.innerHTML = erosionLog.map((l) => `<div>${l}</div>`).join('');
}
function pushUndo() {
  if (!volume) return;
  undoStack.push(volume.snapshot());
  if (undoStack.length > 8) undoStack.shift();
  redoStack.length = 0;
}
function updateResolutionReadout() {
  const host = document.getElementById('resolution-summary');
  if (!host) return;
  const req = resolutionSummary(S.resolution);
  const cur = volume ? `${volume.nx}×${volume.ny}×${volume.nz} · ${volume.spacing.toFixed(2)} m` : '—';
  host.innerHTML = `<div><span>Requested</span><b>${req.text}</b></div><div><span>Current volume</span><b>${cur}</b></div><div><span>Fluid bed at request</span><b>${req.dims.nx - 1}×${req.dims.nz - 1} cells</b></div>`;
}
function applyPresetValues(id) {
  const preset = PRESETS[id];
  if (!preset) return false;
  for (const k of [...SCENE_KEYS, 'openingErosion']) S[k] = DEFAULT_STATE[k];
  S.showWaterLevel = DEFAULT_STATE.showWaterLevel;
  Object.assign(S, preset.values);
  S.preset = id;
  syncRendererFromState();
  ui?.refreshInspector();
  updateResolutionReadout();
  return true;
}
function rebuildOverlays() {
  if (!renderer) return;
  const sets = [];
  if (pathDraft.length) {
    const lines = [];
    const lift = 0.35;
    for (let i = 1; i < pathDraft.length; i++) {
      const a = pathDraft[i - 1], b = pathDraft[i];
      lines.push(a[0], a[1] + lift, a[2], b[0], b[1] + lift, b[2]);
    }
    for (const p of pathDraft) {
      const s = 0.7, y = p[1] + lift;
      lines.push(p[0] - s, y, p[2], p[0] + s, y, p[2], p[0], y, p[2] - s, p[0], y, p[2] + s, p[0], y - s, p[2], p[0], y + s, p[2]);
    }
    if (lines.length) sets.push({ lines: new Float32Array(lines), color: TOOL_COLORS[S.tool] ?? [0.3, 0.65, 1] });
  }
  if (S.showWaterLevel) {
    const lines = [], y = S.waterLevel, m = 47;
    for (let v = -m; v <= m; v += 11.75) lines.push(-m, y, v, m, y, v, v, y, -m, v, y, m);
    sets.push({ lines: new Float32Array(lines), color: [0.25, 0.65, 1] });
  }
  renderer.overlaySets = sets;
  ui?.setStat('statPathPoints', String(pathDraft.length));
}
function gotoBookmark(name = S.cameraBookmark) {
  if (!volume) return;
  if (name === 'cave') return caveView();
  if (name === 'patch') camera.frame([0, 22, 0], 122, 0.85, 1.04);
  else if (name === 'top') camera.frame([0, 26, 0], 126, 0.85, 0.08);
  else if (name === 'cliffFoot') {
    const xc = cliffLineX(0, S), y = volume.topHeight(xc + 12, 4);
    camera.toFly([xc + 30, y + 6, 27], [xc, y + 9, 0]);
  } else if (name === 'plateau') {
    const xc = cliffLineX(0, S), y = volume.topHeight(xc - 22, 0);
    camera.toFly([xc - 5, y + 9, 34], [xc - 24, y - 1, 0]);
  } else framePatch();
  ui?.setCameraMode(camera.mode);
}
function downloadMapPNG(kind) {
  if (!volume) return;
  const nx = volume.nx, nz = volume.nz;
  const canvas = document.createElement('canvas');
  canvas.width = nx; canvas.height = nz;
  const ctx = canvas.getContext('2d');
  const img = ctx.createImageData(nx, nz);
  const top = volume.extractTopHeight();
  let mn = Infinity, mx = -Infinity;
  if (kind === 'height') for (const v of top) { if (v < mn) mn = v; if (v > mx) mx = v; }
  for (let iz = 0; iz < nz; iz++) for (let ix = 0; ix < nx; ix++) {
    const i = iz * nx + ix, o = i * 4;
    let r = 0, g = 0, b = 0;
    if (kind === 'height') { const v = (top[i] - mn) / Math.max(1e-6, mx - mn); r = g = b = Math.round(255 * v); }
    else if (kind === 'moisture') { const v = clamp(volume.moisture[i], 0, 1); r = 12; g = Math.round(35 + 120 * v); b = Math.round(35 + 220 * v); }
    else if (kind === 'suitability') { const v = clamp(volume.suitability[i], 0, 1); r = Math.round(20 + 60 * (1 - v)); g = Math.round(25 + 220 * v); b = Math.round(25 + 60 * v); }
    else { const v = clamp(volume.sampleSediment(volume.nodeX(ix), top[i] - 0.25, volume.nodeZ(iz)) / 1.5, 0, 1); r = Math.round(35 + 190 * v); g = Math.round(28 + 130 * v); b = Math.round(22 + 70 * v); }
    img.data[o] = r; img.data[o + 1] = g; img.data[o + 2] = b; img.data[o + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
  canvas.toBlob((blob) => { if (blob) download(`frontier-${kind}-map.png`, blob); }, 'image/png');
}
function projectPayload() {
  return {
    format: 'frontier-environment-project/v2',
    savedAt: new Date().toISOString(),
    note: 'Regenerates deterministically from params+seed. Hand strokes after generation are in the OBJ/SDF exports.',
    params: { ...S },
    camera: { mode: camera.mode, target: camera.target, radius: camera.radius, azimuth: camera.azimuth, polar: camera.polar, pos: camera.pos, yaw: camera.yaw, pitch: camera.pitch, flySpeed: camera.flySpeed },
    volume: volume ? { nx: volume.nx, ny: volume.ny, nz: volume.nz, spacing: volume.spacing, origin: volume.origin } : null,
  };
}
function importProjectFile() {
  const input = document.createElement('input');
  input.type = 'file'; input.accept = 'application/json,.json';
  input.addEventListener('change', async () => {
    const file = input.files?.[0];
    if (!file) return;
    try {
      const json = JSON.parse(await file.text());
      const params = json.params ?? json;
      if (!params || typeof params !== 'object') throw new Error('No params object found');
      for (const k of Object.keys(S)) if (Object.prototype.hasOwnProperty.call(params, k)) S[k] = coerce(k, params[k]);
      S.resolution = clamp(Math.round(S.resolution / 24) * 24, 48, 192);
      if (json.camera) Object.assign(camera, json.camera);
      syncRendererFromState();
      ui.select('terrain'); setTool(S.tool); ui.refreshInspector(); updateResolutionReadout();
      toast(`Project loaded: ${file.name} — regenerating…`);
      await generate(false);
    } catch (err) { console.error(err); toast(`Project load failed: ${err.message}`); }
  });
  input.click();
}
function refreshBedList() {
  const host = document.getElementById('bed-stack');
  if (!host || !geology) return;
  const beds = geology.beds;
  host.innerHTML = beds.slice(-16).reverse().map((b, i) => {
    const idx = beds.length - 1 - i;
    const c = b.color.map((v) => Math.round(Math.pow(v, 1 / 2.2) * 255));
    return `<div class="bed-row"><span class="bed-swatch" style="background:rgb(${c[0]},${c[1]},${c[2]})"></span><span>#${idx} ${b.type}</span><span>${b.thickness.toFixed(1)} m</span><span>H ${b.hardness.toFixed(2)}</span></div>`;
  }).join('') + `<div class="bed-note">${beds.length} beds total (top 16 shown, youngest first)</div>`;
}
function updateStats() {
  if (!ui || !volume) return;
  ui.setStat('statRes', `${volume.nx}×${volume.ny}×${volume.nz}`);
  ui.setStat('statVoxel', `${volume.spacing.toFixed(2)} m`);
  ui.setStat('statPathPoints', String(pathDraft.length));
  if (lastPathStats) ui.setStat('statPathResult', `${lastPathStats.length.toFixed(0)} m · ${lastPathStats.samples} stamps`);
  if (lastRockStats) { ui.setStat('statRocks', String(lastRockStats.placed)); ui.setStat('statRockAttempts', String(lastRockStats.attempts)); }
  ui.setPatchSummary(`SDF ${volume.nx}×${volume.ny}×${volume.nz} · ${volume.spacing.toFixed(2)} m voxels`);
  updateResolutionReadout();
  ui.setStat('statTris', totalTriangles.toLocaleString('en-ZA'));
  let active = 0;
  for (const m of meshes.values()) if (m.vertexCount > 0) active++;
  ui.setStat('statChunks', `${active} / ${volume.chunksX * volume.chunksY * volume.chunksZ}`);
  ui.setStat('statSolid', `${Math.round(volume.solidVolume()).toLocaleString('en-ZA')} m³`);
  const cave = volume.landmarks.cave;
  if (cave) {
    const cross = volume.columnCrossings(cave.chamber[0], cave.chamber[2]).length;
    ui.setStat('statCave', `${cross} (heightfield max: 1)`);
  }
  if (lastBedStats) {
    ui.setStat('statBedRes', `${lastBedStats.nx}×${lastBedStats.ny} @ ${lastBedStats.cellSize} m`);
    ui.setStat('statBedRange', `${lastBedStats.min.toFixed(1)} … ${lastBedStats.max.toFixed(1)} m`);
    ui.setStat('statBedMean', `${lastBedStats.mean.toFixed(1)} m`);
    ui.setStat('statWet', String(lastBedStats.drainageCells));
  }
  if (lastFlowStats) {
    ui.setStat('statFlowMax', `${Math.round(lastFlowStats.maxArea).toLocaleString('en-ZA')} m²`);
    ui.setStat('statChannel', String(lastFlowStats.channelCells ?? '—'));
  }
  if (lastSuitability) {
    ui.setStat('statSuit', `${(lastSuitability.suitableFraction * 100).toFixed(1)}%`);
    let sum = 0;
    for (let i = 0; i < volume.suitability.length; i++) sum += volume.suitability[i];
    ui.setStat('statSuitMean', (sum / volume.suitability.length).toFixed(3));
  }
  refreshBedList();
}

// ------------------------------------------------------------------ meshing
function remeshKeys(keys) {
  for (const key of keys) {
    const [cx, cy, cz] = volume.chunkCoords(key);
    const mesh = buildChunkMesh(volume, cx, cy, cz);
    const old = meshes.get(key);
    if (old) totalTriangles -= old.triangles;
    meshes.set(key, mesh);
    totalTriangles += mesh.triangles;
    renderer?.uploadChunk(key, mesh);
    volume.dirty.delete(key);
  }
  return keys;
}
function remeshDirty() {
  if (!volume || volume.dirty.size === 0) return [];
  return remeshKeys([...volume.dirty]);
}
async function remeshAllAsync(label = 'Meshing SDF chunks') {
  const keys = [...volume.dirty];
  let done = 0;
  for (const key of keys) {
    remeshKeys([key]);
    done++;
    if ((done & 11) === 0) {
      ui.setProgress(label, done / keys.length);
      ui.showLoading(true, `${label} — ${done}/${keys.length}`, done / keys.length);
      await nextFrame();
    }
  }
  updateStats();
}

// ------------------------------------------------------------------ generation / erosion runs
async function withBusy(label, fn) {
  if (busy) { toast('Busy — wait for the current pass to finish'); return; }
  busy = true;
  ui.setBusy(true, label);
  try { await fn(); }
  catch (err) { console.error(err); toast(`Error: ${err.message}`); }
  finally {
    busy = false;
    ui.setBusy(false);
    ui.setProgress(null);
    ui.showLoading(false);
  }
}

async function generate(initial = false) {
  await withBusy('Generating', async () => {
    undoStack.length = 0; redoStack.length = 0;
    pathDraft = []; lastRockStats = null; lastPathStats = null;
    S.resolution = clamp(Math.round(S.resolution / 24) * 24, 48, 192);
    const dims = dimsForResolution(S.resolution);
    ui.showLoading(true, `Filling geology volume (${dims.nx}×${dims.ny}×${dims.nz} nodes)…`, 0);
    volume = new SdfVolume(dims);
    DEFS.groups.terrain.sub = `True 3D volume · ${dims.nx}×${dims.ny}×${dims.nz} · ${dims.spacing.toFixed(2)} m`;
    ui.renderOutliner();
    rebuildOverlays();
    geology = new Geology(pickParams(GEO_KEYS));
    volume.geology = geology;
    await fillGeology(volume, geology, (f) => {
      ui.setProgress('Geology: beds, hardness, joints', f);
      ui.showLoading(true, 'Geology: beds, hardness, joints…', f * 0.3);
    }, nextFrame);
    await buildEscarpmentScene(volume, geology, pickParams(SCENE_KEYS), (phase, f) => {
      ui.setProgress(phase, f);
      ui.showLoading(true, phase, 0.3 + f * 0.3);
    }, nextFrame);

    if (S.openingErosion) {
      ui.setProgress('Opening erosion: differential weathering', 0);
      ui.showLoading(true, 'Weathering cliff faces (soft beds recede)…', 0.62);
      await differentialWeathering(volume, { iterations: 18, rate: 0.15, power: S.weatherPower, jointInfluence: S.weatherJoints, seed: S.seed }, (f) => ui.setProgress('Opening erosion: weathering', f), nextFrame);
      const bf = await blockFall(volume, { maxBlocks: 18, strength: S.blockStrength, seed: S.seed }, (f) => ui.setProgress('Opening erosion: block fall', f), nextFrame);
      logErosion(`Opening scene: ${bf.fallen} blocks shed from undercuts`);
      // weather the fresh scars and newly exposed beds before rain/talus settle them
      await differentialWeathering(volume, { iterations: 5, rate: 0.13, power: S.weatherPower, jointInfluence: S.weatherJoints, seed: S.seed }, (f) => ui.setProgress('Opening erosion: weathering scars', f), nextFrame);
      await hydraulicDroplets(volume, { droplets: 3500, seed: S.seed, hardnessInfluence: S.hardnessInfluence }, (f) => ui.setProgress('Opening erosion: rain', f), nextFrame);
      await thermalLoose(volume, { iterations: 10, reposeDeg: S.thermalRepose, seed: S.seed }, (f) => ui.setProgress('Opening erosion: talus', f), nextFrame);
      const fl = await fluvialIncision(volume, { iterations: 5, strength: S.fluvialStrength, concavity: S.fluvialConcavity, deposition: S.fluvialDeposition, minArea: S.fluvialMinArea }, (f) => ui.setProgress('Opening erosion: rivers', f), nextFrame);
      lastFlowStats = fl;
    } else {
      const dr = computeDrainage(volume);
      lastFlowStats = updateFlowMaps(volume, dr);
      computeSuitability(volume, dr);
    }
    lastSuitability = { suitableFraction: suitabilityFraction() };
    computeBedStats();
    volume.markAllDirty();
    meshes.clear(); totalTriangles = 0;
    renderer?.clearChunks();
    await remeshAllAsync('Meshing SDF chunks');
    ui.setSaveStatus('Generated', true);
    ui.showLoading(false);
    if (initial) framePatch();
    toast(S.cave && volume.landmarks.cave ? `${S.landform} ready at ${volume.spacing.toFixed(2)} m voxels — press C for the cave/arch` : `${S.landform} ready at ${volume.spacing.toFixed(2)} m voxels`);
  });
}

function suitabilityFraction() {
  let n = 0;
  for (let i = 0; i < volume.suitability.length; i++) if (volume.suitability[i] > 0.5) n++;
  return n / volume.suitability.length;
}
function computeBedStats() {
  const bed = volume.extractBedForFluid();
  let min = Infinity, max = -Infinity, sum = 0;
  for (let i = 0; i < bed.heights.length; i++) { const h = bed.heights[i]; if (h < min) min = h; if (h > max) max = h; sum += h; }
  const dr = computeDrainage(volume);
  let drainageCells = 0;
  for (let i = 0; i < dr.area.length; i++) if (dr.area[i] > 35) drainageCells++;
  lastBedStats = { ...bed, min, max, mean: sum / bed.heights.length, drainageCells };
  return lastBedStats;
}

async function runErosion(kind) {
  if (!volume) return;
  await withBusy('Eroding', async () => {
    pushUndo();
    const t0 = performance.now();
    const progress = (phase) => (f) => ui.setProgress(phase, f);
    const suite = async () => {
      const w = await differentialWeathering(volume, { iterations: S.weatherIter, rate: S.weatherRate, power: S.weatherPower, jointInfluence: S.weatherJoints, seed: S.seed }, progress('Differential weathering'), nextFrame);
      logErosion(`Weathering ×${w.iterations}: front advanced (Σδ ${w.totalDelta.toFixed(0)})`);
      const b = await blockFall(volume, { maxBlocks: S.blockMax, strength: S.blockStrength, seed: S.seed + ((Math.random() * 99) | 0) }, progress('Block fall'), nextFrame);
      logErosion(`Block fall: ${b.fallen}/${b.candidates} undercut blocks shed`);
      const d = await hydraulicDroplets(volume, { droplets: S.droplets, inertia: S.dropletInertia, capacityFactor: S.dropletCapacity, erodeSpeed: S.dropletErode, depositSpeed: S.dropletDeposit, evaporation: S.dropletEvap, lifetime: S.dropletLifetime, hardnessInfluence: S.hardnessInfluence, seed: S.seed }, progress('Surface droplets'), nextFrame);
      logErosion(`Droplets ×${d.droplets}: eroded ${d.eroded.toFixed(2)}, deposited ${d.deposited.toFixed(2)} (SDF units)`);
      const t = await thermalLoose(volume, { iterations: S.thermalIter, reposeDeg: S.thermalRepose, seed: S.seed }, progress('Thermal talus'), nextFrame);
      logErosion(`Thermal: ${t.transfers} loose-material transfers`);
      const f = await fluvialIncision(volume, { iterations: S.fluvialIter, strength: S.fluvialStrength, concavity: S.fluvialConcavity, deposition: S.fluvialDeposition, minArea: S.fluvialMinArea }, progress('Fluvial incision'), nextFrame);
      lastFlowStats = f;
      logErosion(`Fluvial: ${f.channelCells} channel cells, max drainage ${Math.round(f.maxArea)} m²`);
    };
    if (kind === 'suite') await suite();
    else if (kind === 'weather') { const w = await differentialWeathering(volume, { iterations: S.weatherIter, rate: S.weatherRate, power: S.weatherPower, jointInfluence: S.weatherJoints, seed: S.seed }, progress('Differential weathering'), nextFrame); logErosion(`Weathering ×${w.iterations}`); }
    else if (kind === 'blockfall') { const b = await blockFall(volume, { maxBlocks: S.blockMax, strength: S.blockStrength, seed: S.seed + ((Math.random() * 99) | 0) }, progress('Block fall'), nextFrame); logErosion(`Block fall: ${b.fallen}/${b.candidates}`); }
    else if (kind === 'droplets') { const d = await hydraulicDroplets(volume, { droplets: S.droplets, inertia: S.dropletInertia, capacityFactor: S.dropletCapacity, erodeSpeed: S.dropletErode, depositSpeed: S.dropletDeposit, evaporation: S.dropletEvap, lifetime: S.dropletLifetime, hardnessInfluence: S.hardnessInfluence, seed: S.seed }, progress('Surface droplets'), nextFrame); logErosion(`Droplets ×${d.droplets}`); }
    else if (kind === 'thermal') { const t = await thermalLoose(volume, { iterations: S.thermalIter, reposeDeg: S.thermalRepose, seed: S.seed }, progress('Thermal talus'), nextFrame); logErosion(`Thermal: ${t.transfers} transfers`); }
    else if (kind === 'fluvial') { const f = await fluvialIncision(volume, { iterations: S.fluvialIter, strength: S.fluvialStrength, concavity: S.fluvialConcavity, deposition: S.fluvialDeposition, minArea: S.fluvialMinArea }, progress('Fluvial incision'), nextFrame); lastFlowStats = f; logErosion(`Fluvial: ${f.channelCells} channel cells`); }
    // keep derived maps honest after shape change
    const dr = computeDrainage(volume);
    updateFlowMaps(volume, dr);
    computeSuitability(volume, dr);
    lastSuitability = { suitableFraction: suitabilityFraction() };
    volume.markAllDirty(); // moisture/suitability vertex data changed everywhere
    await remeshAllAsync('Remeshing eroded chunks');
    computeBedStats();
    updateStats();
    ui.setSaveStatus('Eroded — undo available', false);
    toast(`Erosion pass in ${((performance.now() - t0) / 1000).toFixed(1)} s`);
  });
}

// ------------------------------------------------------------------ camera actions
function framePatch() {
  camera.frame([-10, 21, -1], 74, 1.05, 1.13);
  ui.setCameraMode(camera.mode);
}
function caveView() {
  const cave = volume?.landmarks?.cave;
  if (!cave) { toast('No cave in this patch (enable it under Terrain SDF)'); return; }
  const [ex, ey, ez] = cave.entrance;
  camera.toFly([ex + 9, ey + 1.2, ez + 1.5], cave.chamber);
  camera.flySpeed = 7;
  ui.setCameraMode(camera.mode);
  toast('At the cave mouth — WASD to go inside, G for orbit');
}
function toggleCamera() {
  if (camera.mode === 'orbit') camera.toFly(); else camera.toOrbit();
  ui.setCameraMode(camera.mode);
  toast(camera.mode === 'fly' ? 'Fly camera: WASD/QE, RMB or drag to look' : 'Orbit camera');
}

// ------------------------------------------------------------------ brush
function brushCenterFor(tool, hit) {
  const r = S.brushRadius, n = hit.normal, p = hit.point;
  if (tool === 'add' || tool === 'deposit') return [p[0] - n[0] * r * 0.22, p[1] - n[1] * r * 0.22, p[2] - n[2] * r * 0.22];
  if (tool === 'carve') return [p[0] + n[0] * r * 0.22, p[1] + n[1] * r * 0.22, p[2] + n[2] * r * 0.22];
  return p.slice();
}
async function applyBrush(hit, erase = false) {
  const tool = S.tool;
  if (tool === 'navigate' || !hit) return;
  const center = brushCenterFor(tool, hit);
  const r = S.brushRadius, st = S.brushStrength;
  switch (tool) {
    case 'add': volume.brushAdd(center, r, st, S.brushSmooth); break;
    case 'deposit': volume.brushAdd(center, r, st, S.brushSmooth, { sediment: 0.95 }); break;
    case 'carve': volume.brushRemove(center, r, st, S.brushSmooth); break;
    case 'smooth': volume.brushSmooth(center, r, st, S.brushFalloff); break;
    case 'flatten': if (stroke?.plane) volume.brushFlatten(center, r, st, stroke.plane.point, stroke.plane.normal, S.brushFalloff); break;
    case 'terrace': if (stroke?.plane) volume.brushFlatten(center, r, st, stroke.plane.point, [0, 1, 0], S.brushFalloff); break;
    case 'soil': volume.brushPaintSediment(center, r, st * 0.5, erase, S.brushFalloff); break;
    case 'erode': await brushErode(volume, center, r, { strength: st, droplets: 60, hardnessInfluence: S.hardnessInfluence, seed: S.seed }); break;
  }
  const done = remeshDirty();
  if (stroke) for (const k of done) stroke.dirtyKeys.add(k);
}
function setTool(id) {
  const isPath = id === 'river' || id === 'road';
  if (!isPath && pathDraft.length) pathDraft = [];
  S.tool = id;
  if (isPath && ui?.selected !== 'features' && ui?.selected !== 'brush') ui.select('features');
  ui?.setTool(id);
  ui?.syncControl('tool');
  ui?.setHint(toolHint());
  rebuildOverlays();
}
function toolHint() {
  if (S.tool === 'river' || S.tool === 'road') return `${S.tool === 'river' ? 'River' : 'Road'} path: click surface points · double-click/Enter to carve · Backspace last point · Esc cancel (${pathDraft.length} point${pathDraft.length === 1 ? '' : 's'})`;
  const base = 'LMB brush · Alt+LMB orbit · MMB pan · wheel zoom · Ctrl+wheel radius';
  return S.tool === 'navigate' ? 'Navigate: LMB orbit/look · MMB pan · wheel zoom · G fly/orbit' : `${TOOLS.find((t) => t.id === S.tool)?.label}: ${base}`;
}
async function refreshDerivedAndRemesh(label) {
  const dr = computeDrainage(volume);
  updateFlowMaps(volume, dr);
  computeSuitability(volume, dr);
  lastSuitability = { suitableFraction: suitabilityFraction() };
  volume.markAllDirty();
  await remeshAllAsync(label);
  computeBedStats();
  updateStats();
}
async function finishPathDraft() {
  if (!volume) return;
  if (pathDraft.length < 2) { toast('Path needs at least two points — click the terrain to draft it'); return; }
  const mode = S.tool === 'road' ? 'road' : 'river';
  const points = pathDraft.map((p) => p.slice());
  await withBusy(mode === 'river' ? 'Carving river' : 'Building road', async () => {
    pushUndo();
    await nextFrame();
    lastPathStats = carvePath(volume, points, {
      mode, width: S.pathWidth, bankWidth: S.pathBankWidth, depth: S.pathDepth, embankment: S.pathEmbankment,
      flatten: S.pathFlatten, sediment: S.pathSediment, levee: S.pathLevee, minSlope: S.pathMinSlopePct / 100,
    });
    pathDraft = [];
    rebuildOverlays();
    await refreshDerivedAndRemesh(mode === 'river' ? 'Remeshing river channel' : 'Remeshing road bench');
    ui.setSaveStatus(`${mode === 'river' ? 'River' : 'Road'} carved — undo available`, false);
    toast(`${mode === 'river' ? 'River' : 'Road'}: ${lastPathStats.length.toFixed(0)} m long, ${lastPathStats.drop.toFixed(1)} m fall, ${lastPathStats.samples} stamps`);
  });
  ui.setHint(toolHint());
}
async function scatterRocksAction() {
  if (!volume) return;
  await withBusy('Scattering rocks', async () => {
    pushUndo();
    await nextFrame();
    lastRockStats = scatterOutcrops(volume, {
      count: S.rockCount, seed: S.rockSeed, rockScale: S.rockScale, rockStretch: S.rockStretch,
      rockSharpness: S.rockSharpness, boulderShare: S.boulderShare, maxSlopeDeg: S.rockMaxSlope,
      minSpacing: S.rockMinSpacing, cover: S.rockCover, preferHorizon: !!S.preferHorizon,
      horizonY: S.horizonY, horizonWidth: S.horizonWidth,
    });
    await refreshDerivedAndRemesh('Remeshing rocks & outcrops');
    ui.setSaveStatus('Rocks scattered — undo available', false);
    toast(`Placed ${lastRockStats.placed} rocks/outcrops (${lastRockStats.attempts} candidates tried)`);
  });
}

// ------------------------------------------------------------------ actions
async function onAction(action) {
  switch (action) {
    case 'generate': return generate(false);
    case 'applyPreset': if (applyPresetValues(S.preset)) return generate(false); return;
    case 'randomSeed': S.seed = 1 + Math.floor(Math.random() * 99998); ui.refreshInspector(); ui.setSaveStatus('Press Generate to apply seed', false); toast(`Seed ${S.seed}`); return;
    case 'toolRiver': setTool('river'); return;
    case 'toolRoad': setTool('road'); return;
    case 'finishPath': return finishPathDraft();
    case 'undoPathPoint': pathDraft.pop(); rebuildOverlays(); ui.setHint(toolHint()); updateStats(); return;
    case 'clearPath': pathDraft = []; rebuildOverlays(); ui.setHint(toolHint()); updateStats(); return;
    case 'scatterRocks': return scatterRocksAction();
    case 'viewAO': S.debugMode = 8; syncRendererFromState(); ui.syncControl('debugMode'); return;
    case 'viewShaded': S.debugMode = 0; syncRendererFromState(); ui.syncControl('debugMode'); return;
    case 'gotoBookmark': gotoBookmark(); return;
    case 'exportProject': download('frontier-environment-project.json', new Blob([JSON.stringify(projectPayload(), null, 2)], { type: 'application/json' })); return;
    case 'importProject': importProjectFile(); return;
    case 'exportHeightmap': downloadMapPNG('height'); return;
    case 'exportMoisture': downloadMapPNG('moisture'); return;
    case 'exportSuitability': downloadMapPNG('suitability'); return;
    case 'exportSedimentMap': downloadMapPNG('sediment'); return;
    case 'exportSdf': {
      if (!volume) return;
      const header = { format: 'frontier-sdf/v1', dtype: 'float32-le', order: 'index=(z*ny+y)*nx+x', sign: 'negative=solid, positive=air (metres)', nx: volume.nx, ny: volume.ny, nz: volume.nz, spacing: volume.spacing, origin: volume.origin };
      download('frontier-terrain-sdf.json', new Blob([JSON.stringify(header, null, 2)], { type: 'application/json' }));
      setTimeout(() => download('frontier-terrain.sdf.f32', new Blob([volume.sdf], { type: 'application/octet-stream' })), 350);
      toast('SDF header + raw Float32 grid downloading');
      return;
    }
    case 'frame': return framePatch();
    case 'cave': return caveView();
    case 'cameraMode': return toggleCamera();
    case 'screenshot': {
      renderer.render(camera, currentBrushGizmo());
      ui.canvas.toBlob((b) => { if (b) download('frontier-environment.png', b); }, 'image/png');
      return;
    }
    case 'redistance': return withBusy('Redistancing', async () => { pushUndo(); volume.redistance(); volume.markAllDirty(); await remeshAllAsync('Remeshing'); toast('Field redistanced'); });
    case 'rebuildGeology': return withBusy('Geology', async () => {
      geology = new Geology(pickParams(GEO_KEYS));
      volume.geology = geology;
      await fillGeology(volume, geology, (f) => ui.setProgress('Rebuilding geology fields', f), nextFrame);
      volume.markAllDirty();
      await remeshAllAsync('Remeshing with new geology');
      updateStats();
      toast('Geology rebuilt — shape kept, new beds/hardness/joints are live');
    });
    case 'weather': return runErosion('weather');
    case 'blockfall': return runErosion('blockfall');
    case 'droplets': return runErosion('droplets');
    case 'thermal': return runErosion('thermal');
    case 'fluvial': return runErosion('fluvial');
    case 'erodeSuite': return runErosion('suite');
    case 'extractBed': {
      const s = computeBedStats();
      const dr = computeDrainage(volume);
      lastFlowStats = { ...updateFlowMaps(volume, dr), channelCells: s.drainageCells };
      computeSuitability(volume, dr);
      lastSuitability = { suitableFraction: suitabilityFraction() };
      volume.markAllDirty();
      remeshDirty();
      updateStats();
      toast(`Bed extracted: ${s.nx}×${s.ny}, ${s.min.toFixed(1)}…${s.max.toFixed(1)} m — ready for FluidSim.setBed()`);
      return;
    }
    case 'computeSuitability': {
      const dr = computeDrainage(volume);
      updateFlowMaps(volume, dr);
      computeSuitability(volume, dr);
      lastSuitability = { suitableFraction: suitabilityFraction() };
      volume.markAllDirty(); remeshDirty(); updateStats();
      toast(`Suitability: ${(lastSuitability.suitableFraction * 100).toFixed(1)}% of columns plantable`);
      return;
    }
    case 'viewSuitability': S.debugMode = 6; renderer.settings.mode = 6; ui.syncControl('debugMode'); ui.select('view'); return;
    case 'exportObj': {
      toast('Building OBJ…');
      setTimeout(() => download('frontier-terrain.obj', new Blob([exportOBJ(meshes)], { type: 'text/plain' })), 30);
      return;
    }
    case 'exportBed': {
      const bed = volume.extractBedForFluid();
      const payload = {
        format: 'frontier-fluid-bed/v1', nx: bed.nx, ny: bed.ny, cellSize: bed.cellSize, origin: bed.origin,
        note: 'heights are row-major z*y+x cell-centred top-surface elevations from the SDF volume; pass new Float32Array(heights) to FluidSim.setBed()',
        heights: Array.from(bed.heights, (v) => Math.round(v * 1000) / 1000),
      };
      download('frontier-fluid-bed.json', new Blob([JSON.stringify(payload)], { type: 'application/json' }));
      return;
    }
    case 'exportParams': download('frontier-environment-params.json', new Blob([JSON.stringify(S, null, 2)], { type: 'application/json' })); return;
  }
}

// ------------------------------------------------------------------ pointer / keys
function bindPointer() {
  const canvas = ui.canvas;
  const setNdc = (e) => {
    const rect = canvas.getBoundingClientRect();
    hoverNdc = [((e.clientX - rect.left) / rect.width) * 2 - 1, 1 - ((e.clientY - rect.top) / rect.height) * 2];
  };
  canvas.addEventListener('contextmenu', (e) => e.preventDefault());
  canvas.addEventListener('pointerenter', () => { pointerOnCanvas = true; });
  canvas.addEventListener('pointerleave', () => { pointerOnCanvas = false; hoverHit = null; });
  canvas.addEventListener('pointermove', async (e) => {
    setNdc(e);
    if (navDrag) {
      const dx = e.clientX - navDrag.x, dy = e.clientY - navDrag.y;
      navDrag.x = e.clientX; navDrag.y = e.clientY;
      if (navDrag.mode === 'pan') camera.pan(dx, dy, canvas.clientHeight);
      else if (camera.mode === 'fly') camera.flyLook(dx, dy);
      else camera.orbit(dx, dy);
      return;
    }
    if (stroke && !busy) {
      const hit = pickAtPointer();
      if (hit) {
        const spacing = Math.max(0.4, S.brushRadius * S.brushSpacing);
        const last = stroke.lastPoint;
        const dist = Math.hypot(hit.point[0] - last[0], hit.point[1] - last[1], hit.point[2] - last[2]);
        if (dist >= spacing) {
          const steps = Math.min(6, Math.max(1, Math.floor(dist / spacing)));
          for (let k = 1; k <= steps; k++) {
            const t = k / steps;
            const mid = {
              point: [last[0] + (hit.point[0] - last[0]) * t, last[1] + (hit.point[1] - last[1]) * t, last[2] + (hit.point[2] - last[2]) * t],
              normal: hit.normal,
            };
            await applyBrush(mid, e.shiftKey);
          }
          stroke.lastPoint = hit.point.slice();
          stroke.stamps += steps;
        }
      }
    }
  });
  canvas.addEventListener('pointerdown', async (e) => {
    canvas.setPointerCapture(e.pointerId);
    setNdc(e);
    if (e.button === 1) { navDrag = { mode: 'pan', x: e.clientX, y: e.clientY }; e.preventDefault(); return; }
    if (e.button === 2) { navDrag = { mode: 'look', x: e.clientX, y: e.clientY }; return; }
    if (e.button !== 0) return;
    if (e.altKey || S.tool === 'navigate' || busy) { navDrag = { mode: 'look', x: e.clientX, y: e.clientY }; return; }
    const hit = pickAtPointer();
    if (!hit) { navDrag = { mode: 'look', x: e.clientX, y: e.clientY }; return; }
    if (S.tool === 'river' || S.tool === 'road') {
      const last = pathDraft[pathDraft.length - 1];
      if (!last || Math.hypot(hit.point[0] - last[0], hit.point[1] - last[1], hit.point[2] - last[2]) > 0.75) pathDraft.push(hit.point.slice());
      rebuildOverlays();
      ui.setHint(toolHint());
      updateStats();
      return;
    }
    pushUndo();
    stroke = {
      lastPoint: hit.point.slice(), stamps: 0, dirtyKeys: new Set(),
      plane: { point: hit.point.slice(), normal: hit.normal.slice() },
    };
    await applyBrush(hit, e.shiftKey);
    stroke.stamps++;
    ui.setSaveStatus('Unsaved edits', false);
  });
  const finishDrag = async (e) => {
    if (navDrag && (e.button === 1 || e.button === 2 || e.button === 0)) navDrag = null;
    if (stroke && e.button === 0) {
      const st = stroke;
      stroke = null;
      if ((S.tool === 'add' || S.tool === 'carve' || S.tool === 'flatten' || S.tool === 'terrace' || S.tool === 'deposit') && st.stamps > 10) {
        volume.redistance();
        for (const k of st.dirtyKeys) volume.dirty.add(k);
        remeshDirty();
      }
      updateStats();
    }
  };
  canvas.addEventListener('pointerup', finishDrag);
  canvas.addEventListener('pointercancel', finishDrag);
  canvas.addEventListener('dblclick', (e) => {
    if ((S.tool === 'river' || S.tool === 'road') && pathDraft.length >= 2) { e.preventDefault(); finishPathDraft(); }
  });
  canvas.addEventListener('wheel', (e) => {
    e.preventDefault();
    if (e.ctrlKey || e.metaKey) {
      S.brushRadius = clamp(S.brushRadius * (e.deltaY < 0 ? 1.12 : 0.89), 1.5, 22);
      ui.syncControl('brushRadius');
    } else if (e.shiftKey) {
      S.brushStrength = clamp(S.brushStrength * (e.deltaY < 0 ? 1.1 : 0.9), 0.05, 1);
      ui.syncControl('brushStrength');
    } else {
      camera.dolly(e.deltaY < 0 ? 0.9 : 1.11);
    }
  }, { passive: false });

  window.addEventListener('keydown', async (e) => {
    const tag = document.activeElement?.tagName;
    if (tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA') return;
    keys.add(e.code);
    if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') { e.preventDefault(); generate(false); return; }
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'z') {
      e.preventDefault();
      if (e.shiftKey) redo(); else undo();
      return;
    }
    if (e.code === 'Space' && pointerOnCanvas) e.preventDefault();
    if ((S.tool === 'river' || S.tool === 'road') && e.code === 'Enter') { e.preventDefault(); finishPathDraft(); return; }
    if (e.code === 'Escape' && pathDraft.length) { pathDraft = []; rebuildOverlays(); ui.setHint(toolHint()); updateStats(); return; }
    if (e.code === 'Backspace' && pathDraft.length) { e.preventDefault(); pathDraft.pop(); rebuildOverlays(); ui.setHint(toolHint()); updateStats(); return; }
    const toolKeys = { Digit1: 'add', Digit2: 'carve', Digit3: 'smooth', Digit4: 'flatten', Digit5: 'terrace', Digit6: 'erode', Digit7: 'deposit', Digit8: 'soil', Digit9: 'river', Digit0: 'road', KeyV: 'navigate' };
    if (toolKeys[e.code]) setTool(toolKeys[e.code]);
    else if (e.code === 'KeyF') framePatch();
    else if (e.code === 'KeyG') toggleCamera();
    else if (e.code === 'KeyC' && !e.ctrlKey && !e.metaKey) caveView();
    else if (e.code === 'BracketLeft') { S.brushRadius = clamp(S.brushRadius - 0.5, 1.5, 22); ui.syncControl('brushRadius'); }
    else if (e.code === 'BracketRight') { S.brushRadius = clamp(S.brushRadius + 0.5, 1.5, 22); ui.syncControl('brushRadius'); }
  });
  window.addEventListener('keyup', (e) => keys.delete(e.code));
  window.addEventListener('blur', () => keys.clear());
}
function pickAtPointer() {
  if (!volume || !hoverNdc) return null;
  const { origin, dir } = camera.ray(hoverNdc[0], hoverNdc[1], renderer?.aspect ?? 1.6);
  return volume.raycast(origin, dir, 900);
}
function undo() {
  if (!undoStack.length || busy) return;
  redoStack.push(volume.snapshot());
  volume.restore(undoStack.pop());
  afterRestore('Undo');
}
function redo() {
  if (!redoStack.length || busy) return;
  undoStack.push(volume.snapshot());
  volume.restore(redoStack.pop());
  afterRestore('Redo');
}
function afterRestore(label) {
  const dr = computeDrainage(volume);
  updateFlowMaps(volume, dr);
  computeSuitability(volume, dr);
  lastSuitability = { suitableFraction: suitabilityFraction() };
  remeshDirty();
  updateStats();
  toast(label);
}

// ------------------------------------------------------------------ HUD / loop
function currentBrushGizmo() {
  if (!S.showBrush || S.tool === 'navigate' || !hoverHit) return null;
  const isPath = S.tool === 'river' || S.tool === 'road';
  return { visible: true, center: brushCenterFor(S.tool, hoverHit), radius: isPath ? S.pathWidth / 2 : S.brushRadius, color: TOOL_COLORS[S.tool] ?? [1, 1, 1] };
}
let hudTimer = 0;
function updateHud(now) {
  if (now - hudTimer < 120) return;
  hudTimer = now;
  if (!hoverHit || !volume) {
    ui.setHud(`<span>${camera.mode === 'fly' ? 'FLY' : 'ORBIT'}</span><span>${totalTriangles.toLocaleString('en-ZA')} tris</span>`);
    return;
  }
  const p = hoverHit.point;
  const hard = volume.sampleHardness(p[0], p[1], p[2]);
  const joint = volume.sampleJoint(p[0], p[1], p[2]);
  const bed = geology?.beds[volume.nearestBed(p[0], p[1], p[2])];
  const crossings = volume.columnCrossings(p[0], p[2]).length;
  ui.setHud(`<span>${camera.mode === 'fly' ? 'FLY' : 'ORBIT'}</span><span>${p[0].toFixed(1)}, ${p[1].toFixed(1)}, ${p[2].toFixed(1)} m</span><span>${bed ? bed.type : 'rock'} · H ${hard.toFixed(2)} · joint ${joint.toFixed(2)}</span><span>${crossings} surface${crossings === 1 ? '' : 's'} in column</span>`);
  const ro = document.getElementById('cursor-readout');
  if (ro) ro.innerHTML = `
    <div><span>Position</span><b>${p[0].toFixed(1)}, ${p[1].toFixed(1)}, ${p[2].toFixed(1)} m</b></div>
    <div><span>Bed</span><b>${bed ? `${bed.type} · H ${bed.hardness.toFixed(2)} · ${bed.thickness.toFixed(1)} m` : '—'}</b></div>
    <div><span>Hardness / joint</span><b>${hard.toFixed(2)} / ${joint.toFixed(2)}</b></div>
    <div><span>Sediment</span><b>${volume.sampleSediment(p[0], p[1], p[2]).toFixed(2)}</b></div>
    <div><span>Column surfaces</span><b>${crossings}${crossings > 1 ? ' — overhang / cave below' : ''}</b></div>`;
}
let lastTime = performance.now(), lastPick = 0;
function loop(now) {
  requestAnimationFrame(loop);
  const dt = Math.min(0.05, (now - lastTime) / 1000);
  lastTime = now;
  if (!renderer?.device) return;
  camera.flyMove(keys, dt);
  if (pointerOnCanvas && !busy && now - lastPick > 33 && !stroke) {
    lastPick = now;
    hoverHit = pickAtPointer();
  }
  renderer.time = now / 1000;
  renderer.render(camera, currentBrushGizmo());
  updateHud(now);
}

// ------------------------------------------------------------------ value plumbing
function coerce(key, value) {
  if (STRING_KEYS.has(key)) return String(value);
  if (key === 'debugMode') return Number(value);
  return typeof S[key] === 'number' ? Number(value) : value;
}
function syncRendererFromState() {
  if (!renderer) return;
  Object.assign(renderer.settings, {
    mode: S.debugMode, sunAzimuth: S.sunAzimuth, sunElevation: S.sunElevation, sunIntensity: S.sunIntensity,
    caveFill: S.caveFill, fogDensity: S.fogDensity, sedimentStrength: S.sedimentStrength,
    moistureDarken: S.moistureDarken, jointShade: S.jointShade, grain: S.grain,
    aoStrength: S.aoStrength, exposure: S.exposure, saturation: S.saturation, strataContrast: S.strataContrast,
    hemiStrength: S.hemiStrength, soilColor: hexToRgb(S.soilColor), tintColor: hexToRgb(S.rockTint),
    waterLevel: S.showWaterLevel ? S.waterLevel : -100,
  });
  camera.fov = S.fov * Math.PI / 180;
  camera.flySpeed = S.flySpeed;
  rebuildOverlays();
}
function onSetValue(key, value, ctrl, committed) {
  S[key] = coerce(key, value);
  if (key === 'resolution') {
    S.resolution = clamp(Math.round(S.resolution / 24) * 24, 48, 192);
    ui.syncControl('resolution');
    updateResolutionReadout();
  }
  if (key === 'tool') { setTool(S.tool); return; }
  const stage = ctrl?.stage;
  if (stage === 'live' || key === 'debugMode') syncRendererFromState();
  else if (stage === 'regen') ui.setSaveStatus(key === 'resolution' ? 'Press Generate to rebuild the SDF grid' : 'Press Generate to apply landform', false);
  else if (stage === 'geology') ui.setSaveStatus('Rebuild geology (or Generate) to apply', false);
}

// ------------------------------------------------------------------ boot
async function boot() {
  ui = new EditorUI(document.getElementById('root'), DEFS, {
    getValue: (k) => S[k],
    setValue: onSetValue,
    onAction,
    onTool: setTool,
    onSelect: () => updateStats(),
    onInspectorRendered: () => { updateStats(); updateResolutionReadout(); const elx = document.getElementById('erosion-log'); if (elx && erosionLog.length) elx.innerHTML = erosionLog.map((l) => `<div>${l}</div>`).join(''); },
  });
  ui.select('terrain');
  setTool(S.tool);
  bindPointer();
  renderer = new Renderer(ui.canvas);
  syncRendererFromState();
  try {
    await renderer.init();
  } catch (err) {
    ui.showLoading(true, `WebGPU unavailable: ${err.message}`, 0);
    ui.setHint('WebGPU is required. Terrain logic tests can still run in Node: node scripts/check.mjs');
    console.error(err);
    return;
  }
  requestAnimationFrame(loop);
  await generate(true);
}

boot();

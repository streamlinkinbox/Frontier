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

import { SdfVolume, buildEscarpmentScene, cliffLineX } from './volume.js';
import { Geology, fillGeology } from './geology.js';
import {
  differentialWeathering, blockFall, thermalLoose, hydraulicDroplets,
  fluvialIncision, computeDrainage, updateFlowMaps, computeSuitability, brushErode,
} from './erosion.js';
import { buildChunkMesh, exportOBJ } from './mesher.js';
import { Renderer } from './renderer.js';
import { EditorCamera } from './camera.js';
import { EditorUI } from './ui.js';
import { clamp } from './noise.js';

const nextFrame = () => new Promise((r) => requestAnimationFrame(r));

// ------------------------------------------------------------------ state
const S = {
  // scene / geology (stage: regen | geology)
  seed: 428, plateauHeight: 40, plainHeight: 12, cliffX: -10, cliffWidth: 4.8,
  cave: 1, caveRadius: 2.4, fallenBlocks: 6, openingErosion: 1,
  bedScale: 1, dip: 4, dipDirection: 35, lateral: 0.55, fold: 2.5, faultThrow: 0, faultPos: -18,
  jointWidth: 0.55, jointSpacing: 1,
  // brush
  tool: 'carve', brushRadius: 6, brushStrength: 0.65, brushSmooth: 1.2, brushSpacing: 0.35, showBrush: 1,
  // erosion
  weatherIter: 16, weatherRate: 0.16, weatherPower: 1.6, weatherJoints: 1.4,
  blockMax: 28, blockStrength: 0.85,
  droplets: 6000, dropletInertia: 0.25, dropletCapacity: 0.5, dropletErode: 0.30, dropletDeposit: 0.35, dropletEvap: 0.012, dropletLifetime: 70, hardnessInfluence: 0.85,
  thermalIter: 14, thermalRepose: 34,
  fluvialIter: 6, fluvialStrength: 0.09, fluvialConcavity: 0.5, fluvialDeposition: 0.5, fluvialMinArea: 35,
  // view
  debugMode: 0, sunAzimuth: 135, sunElevation: 42, sunIntensity: 1.45, caveFill: 0.38,
  fogDensity: 0.0016, sedimentStrength: 1, moistureDarken: 0.8, jointShade: 0.8, grain: 0.05, fov: 50,
};
const GEO_KEYS = ['seed', 'bedScale', 'dip', 'dipDirection', 'lateral', 'fold', 'faultThrow', 'faultPos', 'jointWidth', 'jointSpacing'];
const SCENE_KEYS = ['seed', 'plateauHeight', 'plainHeight', 'cliffX', 'cliffWidth', 'cave', 'caveRadius', 'fallenBlocks', ...GEO_KEYS];

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
];
const TOOL_COLORS = {
  add: [0.55, 0.95, 0.55], deposit: [0.85, 0.7, 0.4], carve: [1, 0.45, 0.4], erode: [1, 0.75, 0.3],
  smooth: [0.6, 0.8, 1], flatten: [0.8, 0.8, 1], terrace: [0.8, 0.8, 1], soil: [0.65, 0.9, 0.45], navigate: [1, 1, 1],
};
const DEBUG_MODES = [[0, 'Shaded'], [1, 'Strata beds'], [2, 'Hardness'], [3, 'Joint sets'], [4, 'Moisture / flow'], [5, 'Sediment / soil'], [6, 'Veg. suitability'], [7, 'Normals']];

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
let lastBedStats = null, lastFlowStats = null, lastSuitability = null;
let erosionLog = [];

function pickParams(keys) { const o = {}; for (const k of keys) o[k] = S[k]; return o; }

// ------------------------------------------------------------------ UI defs
const slider = (key, label, min, max, step, extra = {}) => ({ type: 'slider', key, label, min, max, step, ...extra });
const DEFS = {
  sections: [
    { label: 'World', ids: ['terrain', 'geology', 'erosion', 'water', 'vegetation'] },
    { label: 'Editor', ids: ['brush', 'view', 'export'] },
  ],
  tools: TOOLS,
  groups: {
    terrain: {
      title: 'Terrain SDF', sub: 'True 3D volume · 97×65×97 nodes', icon: 'mountain', eyebrow: 'World',
      intro: 'One signed-distance volume is the whole terrain — no heightfield underneath. Carve goes through the mountain; Add builds overhangs and arches.',
      cards: [
        { title: 'Patch', icon: 'mountain', controls: [
          { type: 'stats', stats: [
            { id: 'statTris', label: 'Triangles' }, { id: 'statChunks', label: 'Active chunks' },
            { id: 'statSolid', label: 'Rock volume' }, { id: 'statCave', label: 'Cave column crossings' },
          ]},
        ]},
        { title: 'Landform seed', icon: 'mountain', meta: 'Generate to apply', controls: [
          slider('seed', 'Seed', 1, 999, 1, { stage: 'regen' }),
          slider('plateauHeight', 'Plateau height', 24, 54, 0.5, { unit: 'm', stage: 'regen' }),
          slider('plainHeight', 'Plain height', 4, 24, 0.5, { unit: 'm', stage: 'regen' }),
          slider('cliffX', 'Escarpment position', -30, 10, 0.5, { unit: 'm', stage: 'regen' }),
          slider('cliffWidth', 'Cliff transition width', 2, 14, 0.2, { unit: 'm', stage: 'regen', minLabel: 'near-vertical', maxLabel: 'steep slope' }),
          { type: 'toggle', key: 'cave', label: 'Through-cave in a soft bed', stage: 'regen' },
          slider('caveRadius', 'Cave radius', 1.4, 4, 0.1, { unit: 'm', stage: 'regen' }),
          slider('fallenBlocks', 'Fallen blocks at foot', 0, 18, 1, { stage: 'regen' }),
          { type: 'toggle', key: 'openingErosion', label: 'Erode opening scene', stage: 'regen' },
          { type: 'buttons', buttons: [
            { action: 'generate', label: 'Generate patch', primary: true },
            { action: 'redistance', label: 'Redistance field' },
          ]},
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
          { type: 'html', html: '<p class="muted">Cell-centred 96×96 bed, 1 m cells, same origin. <code>FluidRenderer.groundWGSL()</code> then gives the terrain shader true caustics, wetness and mud from the simulated surface — no animated caustic texture.</p>' },
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
          slider('brushSpacing', 'Stamp spacing', 0.1, 0.9, 0.01, { fmt: (v) => `${Math.round(v * 100)}% of radius` }),
          { type: 'toggle', key: 'showBrush', label: 'Show brush gizmo' },
        ], note: 'Ctrl+wheel resizes, Shift+wheel changes strength, [ and ] nudge radius. Alt+LMB orbits while a brush tool is active.' },
        { title: 'Under the cursor', icon: 'eye', controls: [
          { type: 'html', html: '<div id="cursor-readout" class="readout">Hover the terrain…</div>' },
        ]},
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
        { title: 'Surface response', icon: 'strata', controls: [
          slider('sedimentStrength', 'Soil colour strength', 0, 2, 0.01, { stage: 'live' }),
          slider('moistureDarken', 'Wet darkening', 0, 1.5, 0.01, { stage: 'live' }),
          slider('jointShade', 'Joint seam shading', 0, 1.5, 0.01, { stage: 'live' }),
          slider('grain', 'Matte grain (non-structural)', 0, 0.2, 0.005, { stage: 'live' }),
        ], note: 'Seam shading only darkens existing joint geometry. If a cliff looks striped with grain at 0, the geology — not a texture — is doing it.' },
      ],
    },
    export: {
      title: 'Export', sub: 'Mesh · fluid bed · parameters', icon: 'export', eyebrow: 'Editor',
      cards: [
        { title: 'Deliverables', icon: 'export', controls: [
          { type: 'buttons', buttons: [
            { action: 'exportObj', label: 'Terrain OBJ', primary: true },
            { action: 'exportBed', label: 'FluidSim bed JSON' },
            { action: 'exportParams', label: 'Parameters JSON' },
            { action: 'screenshot', label: 'Screenshot PNG' },
          ]},
          { type: 'html', html: '<p class="muted">OBJ is the watertight marching-tetrahedra soup with SDF-gradient normals. The bed JSON drops straight into <code>sim.setBed()</code> after <code>new Float32Array(json.heights)</code>.</p>' },
        ]},
        { title: 'Shortcuts', icon: 'eye', controls: [
          { type: 'html', html: '<ul class="spec-list"><li><b>LMB</b> brush · <b>Alt+LMB / RMB</b> orbit or fly-look · <b>MMB</b> pan · <b>wheel</b> zoom</li><li><b>G</b> orbit ⇄ fly · <b>WASD/QE</b> fly (Shift fast) · <b>F</b> frame · <b>C</b> cave mouth</li><li><b>1–8</b> brush tools · <b>V</b> navigate · <b>[ ]</b> radius</li><li><b>Ctrl+Z / Ctrl+Shift+Z</b> undo/redo · <b>Ctrl+Enter</b> generate</li></ul>' },
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
    ui.showLoading(true, 'Filling geology volume…', 0);
    volume = new SdfVolume();
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
    toast(S.cave ? 'Escarpment ready — press C to fly to the cave mouth' : 'Patch generated');
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
    case 'smooth': volume.brushSmooth(center, r, st); break;
    case 'flatten': if (stroke?.plane) volume.brushFlatten(center, r, st, stroke.plane.point, stroke.plane.normal); break;
    case 'terrace': if (stroke?.plane) volume.brushFlatten(center, r, st, stroke.plane.point, [0, 1, 0]); break;
    case 'soil': volume.brushPaintSediment(center, r, st * 0.5, erase); break;
    case 'erode': await brushErode(volume, center, r, { strength: st, droplets: 60, hardnessInfluence: S.hardnessInfluence, seed: S.seed }); break;
  }
  const done = remeshDirty();
  if (stroke) for (const k of done) stroke.dirtyKeys.add(k);
}
function setTool(id) {
  S.tool = id;
  ui.setTool(id);
  ui.syncControl('tool');
  ui.setHint(toolHint());
}
function toolHint() {
  const base = 'LMB brush · Alt+LMB orbit · MMB pan · wheel zoom · Ctrl+wheel radius';
  return S.tool === 'navigate' ? 'Navigate: LMB orbit/look · MMB pan · wheel zoom · G fly/orbit' : `${TOOLS.find((t) => t.id === S.tool)?.label}: ${base}`;
}

// ------------------------------------------------------------------ actions
async function onAction(action) {
  switch (action) {
    case 'generate': return generate(false);
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
    const toolKeys = { Digit1: 'add', Digit2: 'carve', Digit3: 'smooth', Digit4: 'flatten', Digit5: 'terrace', Digit6: 'erode', Digit7: 'deposit', Digit8: 'soil', KeyV: 'navigate' };
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
  return { visible: true, center: brushCenterFor(S.tool, hoverHit), radius: S.brushRadius, color: TOOL_COLORS[S.tool] ?? [1, 1, 1] };
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
  if (key === 'tool') return String(value);
  if (key === 'debugMode') return Number(value);
  return typeof S[key] === 'number' ? Number(value) : value;
}
function syncRendererFromState() {
  if (!renderer) return;
  Object.assign(renderer.settings, {
    mode: S.debugMode, sunAzimuth: S.sunAzimuth, sunElevation: S.sunElevation, sunIntensity: S.sunIntensity,
    caveFill: S.caveFill, fogDensity: S.fogDensity, sedimentStrength: S.sedimentStrength,
    moistureDarken: S.moistureDarken, jointShade: S.jointShade, grain: S.grain,
  });
  camera.fov = S.fov * Math.PI / 180;
}
function onSetValue(key, value, ctrl, committed) {
  S[key] = coerce(key, value);
  if (key === 'tool') { setTool(S.tool); return; }
  const stage = ctrl?.stage;
  if (stage === 'live' || key === 'debugMode') syncRendererFromState();
  else if (stage === 'regen') ui.setSaveStatus('Press Generate to apply landform', false);
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
    onInspectorRendered: () => { updateStats(); const elx = document.getElementById('erosion-log'); if (elx && erosionLog.length) elx.innerHTML = erosionLog.map((l) => `<div>${l}</div>`).join(''); },
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

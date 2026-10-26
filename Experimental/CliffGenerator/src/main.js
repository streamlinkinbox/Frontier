// Entry: wires the editor chrome to the heightfield worker and the Three.js viewport.
import { defaults, presets, groups, stageOf, palettes, paletteKeys } from './params.js';
import { Editor } from './ui.js';
import { CliffScene } from './scene.js';

const STORAGE_KEY = 'frontier-cliff-generator';
const SCHEMA = 23; // bump when parameter semantics change so stale saved values do not override new defaults

function readStored() {
  try { return JSON.parse(localStorage.getItem(STORAGE_KEY) || '{}'); } catch { return {}; }
}

const stored = readStored();
const storedValues = stored.schema === SCHEMA ? stored.values || {} : {};
const values = { ...defaults, ...presets['Alpine granite'], ...storedValues };
values.features = { roads: [], rivers: [], lakes: [], ...(storedValues.features || {}) };

function applyPalette(name) {
  const p = palettes[name];
  if (!p) return;
  values.palette = name;
  for (const k of paletteKeys) values[k] = p[k];
}

let scene = null;
let worker = null;
let generation = 0;
let rockTimer = 0;
let lastField = null;

const editor = new Editor(document.getElementById('root'), {
  values,
  onChange(key, val) {
    values[key] = val;
    if (key === 'palette') applyPalette(val);
    persist();
    if (key === 'showFeatureLines') { scene && scene.setFeatureOverlay(values, draft.type && draft.type !== 'lake' ? draft : null); return; }
    const stage = stageOf(key);
    if (stage === 'terrain') editor.setPending(true);
    else if (stage === 'rocks') scheduleRocks();
    else if (stage === 'mesh') scheduleMesh();
    else applyLive();
    // some live keys also affect rock placement (water) or need camera re-frame (nothing yet)
    if (key === 'seaLevel' || key === 'waterEnabled') scheduleRocks();
  },
  onPreset(name) {
    const features = values.features;
    Object.assign(values, defaults, presets[name]);
    values.features = features;
    applyPalette(values.palette);
    persist();
    applyLive();
    generate();
  },
  onGenerate: () => generate(),
  onReset(group) {
    for (const card of group.cards) for (const [key] of card.controls || []) values[key] = defaults[key];
    if (group.id === 'material') applyPalette(defaults.palette);
    persist();
    if (group.stage === 'terrain') editor.setPending(true);
    else if (group.stage === 'rocks') scheduleRocks();
    else if (group.stage === 'mesh') scheduleMesh();
    else applyLive();
  },
  onAction(action, data) {
    if (!scene) return;
    if (action === 'tool') return setTool(data);
    if (action === 'tool-finish') return finishDraft();
    if (action === 'tool-undo') { draft.points.pop(); return refreshDraft(); }
    if (action === 'tool-cancel') return setTool(null);
    if (action === 'feature-delete') {
      const [kind, id] = data.split(':');
      values.features[kind] = values.features[kind].filter((f) => String(f.id) !== id);
      return featuresChanged(kind);
    }
    if (action === 'feature-clear') {
      const hadRivers = values.features.rivers.length > 0;
      values.features = { roads: [], rivers: [], lakes: [] };
      return featuresChanged(hadRivers ? 'rivers' : 'roads');
    }
    if (action === 'lake-level') {
      const lake = values.features.lakes.find((l) => String(l.id) === String(data.id));
      if (lake) { lake.level = data.level; featuresChanged('lakes'); }
      return;
    }
    if (action === 'frame') scene.frameCamera(values);
    if (action === 'screenshot') scene.screenshot();
    if (action === 'export-obj') scene.exportOBJ(true);
    if (action === 'export-glb') scene.exportGLB(true);
    if (action === 'export-heightmap') scene.exportHeightmap();
    if (action === 'export-satmap') scene.exportSatmap(2048);
    if (action === 'export-masks') scene.exportMasks();
    if (action === 'export-normal') scene.exportNormalMap();
  },
});

function persist() {
  try { localStorage.setItem(STORAGE_KEY, JSON.stringify({ schema: SCHEMA, values })); } catch { /* private mode */ }
}

function applyLive() {
  if (!scene) return;
  scene.setSurface(values);
  scene.setSky(values);
  scene.setWater(values);
  scene.setDisplay(values);
}

function scheduleRocks() {
  clearTimeout(rockTimer);
  rockTimer = setTimeout(() => {
    if (!scene || !lastField) return;
    const t0 = performance.now();
    scene.setRocks(values);
    updateStats({ rockMs: performance.now() - t0 });
  }, 350);
}

let meshTimer = 0;
function scheduleMesh() {
  clearTimeout(meshTimer);
  meshTimer = setTimeout(() => {
    if (!scene || !lastField) return;
    scene.rebuildMesh(values);
    updateStats();
  }, 400);
}

const ROCK_CAT_NAMES = { talus: 'talus', face: 'bedrock', channel: 'channel lag', bar: 'bars', cap: 'caps', erratic: 'erratics', shore: 'shore' };
const statCache = {};
function updateStats(extra = {}) {
  Object.assign(statCache, extra);
  if (!lastField) return;
  editor.setStats({
    'Grid': `${lastField.resolution}²${scene.meshField && scene.meshField !== lastField ? ` → ${scene.meshField.resolution}²` : ''} · ${values.worldSize} m`,
    'Triangles': (scene.stats.triangles + scene.rockGroup.children.reduce((n, m) => n + (m.geometry.getAttribute('position').count / 3) * m.count, 0)).toLocaleString('en-US', { maximumFractionDigits: 0 }),
    'Rocks': (() => {
      const c = scene.rockCats;
      const cats = c ? ['talus', 'face', 'channel', 'bar', 'cap', 'erratic', 'shore'].filter((k) => c[k] > 0).map((k) => `${ROCK_CAT_NAMES[k]} ${c[k].toLocaleString('en-US')}`) : [];
      return `${scene.rockCount.toLocaleString('en-US')} + ${(scene.pebbleCount || 0).toLocaleString('en-US')} stones${cats.length ? ` · ${cats.join(' · ')}` : ''}`;
    })(),
    '3D cliffs': scene.sdfStats ? `${scene.sdfStats.done}/${scene.sdfStats.total} chunks · ${scene.sdfStats.voxel.toFixed(2)} m voxels · ${scene.sdfStats.triangles.toLocaleString('en-US')} tris · ${(scene.sdfStats.ms / 1000).toFixed(1)} s${scene.sdfStats.candidates > scene.sdfStats.total ? ` (${scene.sdfStats.candidates - scene.sdfStats.total} skipped by cap)` : ''}` : 'off',
    'Relief': `${lastField.stats.min.toFixed(0)} – ${lastField.stats.max.toFixed(0)} m`,
    'Heightfield': `${(lastField.stats.elapsedMs / 1000).toFixed(1)} s`,
  });
}

function ensureWorker() {
  if (worker) return worker;
  worker = new Worker(new URL('./generate.worker.js', import.meta.url), { type: 'module' });
  worker.onmessage = (event) => {
    const msg = event.data;
    if (msg.id !== generation) return;
    if (msg.type === 'progress') editor.setProgress(msg.phase, msg.fraction);
    if (msg.type === 'error') {
      editor.setProgress(null);
      console.error(msg.message);
      alert(`Generation failed:\n${msg.message}`);
    }
    if (msg.type === 'done') {
      lastField = msg.result;
      editor.setProgress('Building surface', 1);
      requestAnimationFrame(() => {
        scene.setField(lastField, values);
        scene.setSurface(values);
        scene.setDisplay(values);
        scene.setRocks(values);
        editor.setProgress(null);
        editor.setPending(false);
        updateStats();
      });
    }
  };
  worker.onerror = (e) => { editor.setProgress(null); console.error(e); alert(`Worker error: ${e.message}`); };
  return worker;
}

function generate() {
  generation += 1;
  editor.setProgress('Starting', 0);
  const params = { ...values };
  // Scale droplet count with resolution so erosion intensity is resolution-independent.
  params.droplets = Math.round(values.droplets * (values.resolution * values.resolution) / (512 * 512));
  ensureWorker().postMessage({ id: generation, params });
}

// ---- drawing roads / rivers / lakes ------------------------------------------------------------
const draft = { type: null, points: [] };
let nextFeatureId = Date.now() % 1000000;

function setTool(tool) {
  draft.type = tool;
  draft.points = [];
  editor.setTool(tool, 0);
  if (scene) scene.setFeatureOverlay(values, null);
  if (tool) editor.select('features');
}

function refreshDraft() {
  editor.setTool(draft.type, draft.points.length);
  if (scene) scene.setFeatureOverlay(values, draft.type && draft.type !== 'lake' ? draft : null);
}

function featuresChanged(kind) {
  persist();
  editor.setTool(draft.type, draft.points.length);
  if (kind === 'rivers') { editor.setPending(true); generate(); }
  else scheduleMesh();
  if (editor.selected === 'features') editor.renderInspector();
}

function finishDraft() {
  if (!draft.type || draft.type === 'lake') return setTool(null);
  if (draft.points.length >= 2) {
    const feature = { id: nextFeatureId++, points: draft.points.map((p) => [Math.round(p[0] * 10) / 10, Math.round(p[1] * 10) / 10]) };
    const kind = draft.type === 'road' ? 'roads' : 'rivers';
    values.features[kind].push(feature);
    const type = draft.type;
    draft.points = [];
    featuresChanged(kind);
    // stay in the tool so several roads / rivers can be drawn in a row
    draft.type = type;
    refreshDraft();
  } else {
    setTool(null);
  }
}

function canvasClick(event) {
  if (!draft.type || !scene || !lastField) return;
  const rect = editor.canvas.getBoundingClientRect();
  const nx = ((event.clientX - rect.left) / rect.width) * 2 - 1;
  const ny = -(((event.clientY - rect.top) / rect.height) * 2 - 1);
  const hit = scene.pickTerrain(nx, ny, values);
  if (!hit) return;
  if (draft.type === 'lake') {
    values.features.lakes.push({ id: nextFeatureId++, x: Math.round(hit.x * 10) / 10, z: Math.round(hit.z * 10) / 10, level: Math.round((hit.y + values.lakeLevelOffset) * 10) / 10 });
    featuresChanged('lakes');
    return;
  }
  draft.points.push([hit.x, hit.z]);
  refreshDraft();
}

function bindDrawing() {
  const canvas = editor.canvas;
  let down = null;
  canvas.addEventListener('pointerdown', (e) => { if (e.button === 0) down = { x: e.clientX, y: e.clientY, t: performance.now() }; });
  canvas.addEventListener('pointerup', (e) => {
    if (!down || e.button !== 0) return;
    const moved = Math.hypot(e.clientX - down.x, e.clientY - down.y);
    const quick = performance.now() - down.t < 600;
    down = null;
    if (moved < 5 && quick) canvasClick(e);
  });
  canvas.addEventListener('dblclick', (e) => { if (draft.type) { e.preventDefault(); finishDraft(); } });
  window.addEventListener('keydown', (e) => {
    if (!draft.type) return;
    const typing = /INPUT|TEXTAREA/.test(document.activeElement && document.activeElement.tagName);
    if (e.key === 'Escape') { e.preventDefault(); setTool(null); }
    else if (e.key === 'Enter' && !e.ctrlKey && !e.metaKey) { e.preventDefault(); finishDraft(); }
    else if (e.key === 'Backspace' && !typing) { e.preventDefault(); draft.points.pop(); refreshDraft(); }
  });
}

function boot() {
  try {
    scene = new CliffScene(editor.canvas);
    scene.onSdfProgress = () => updateStats();
  } catch (error) {
    document.querySelector('.canvas-wrap').innerHTML = `<div class="empty">WebGL 2 is required to run the cliff generator.<br>${error.message}</div>`;
    return;
  }
  applyLive();
  bindDrawing();
  generate();
}

boot();

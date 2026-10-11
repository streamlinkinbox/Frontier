// Panels: outliner (left), inspector and texture layer stack (right), toolbar and HUD.
// Panels are rebuilt from the project on structural changes; slider drags only update the model.

import { ROAD_CLASSES, BOLLARD_DESIGNS, sampleProject, createEmptyProject, indexNodes } from './model.js';
import { LAYER_TYPES, PARAM_SPEC, MAX_LAYERS, hexToRgb } from './layers.js';
import { MAX_VEHICLES, VEHICLE_TYPES } from './vehicles.js';

let A = null; // app reference, set by bind()
const $ = (id) => document.getElementById(id);
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const fmt = (v, d = 2) => (Number.isFinite(v) ? Number(v).toFixed(d) : '—');

const PROP_KINDS = {
  streetlight: 'Streetlight', bollard: 'Bollard', cone: 'Traffic cone', pothole: 'Pothole', barrier: 'Jersey barrier',
};

const TOOL_HINTS = {
  select: 'Click to select. Drag the gizmo to move an intersection with all its approaches. Drag yellow squares to reshape a road. Right-drag orbit, middle-drag or Shift+drag pan, wheel zooms.',
  road: 'Click an intersection or empty ground to start. Click to add curve points. Click an intersection to finish and chain the next road. Double-click empty ground to finish on a new intersection. Esc stops.',
  node: 'Click the ground to place an intersection. Its roads merge into one junction mesh when you connect them.',
  prop: 'Click the ground to place a {kind}.',
};

// ---------- binding -----------------------------------------------------------------------------
export function bind(app) {
  A = app;
  document.querySelectorAll('[data-tool]').forEach(b => b.addEventListener('click', () => { A.setTool(b.dataset.tool); }));
  document.querySelectorAll('[data-toggle]').forEach(b => b.addEventListener('click', () => {
    const k = b.dataset.toggle;
    const s = A.project.settings;
    s[k] = !s[k];
    if (k === 'markingsOn' || k === 'snap') A.requestRebuild();
    refreshToolbar(A);
    refreshInspector(A);
  }));
  document.querySelectorAll('[data-tab]').forEach(b => b.addEventListener('click', () => {
    A.tab = b.dataset.tab;
    refreshTabs();
    refreshInspector(A);
    refreshMaterials(A);
  }));
  $('new-road-class').addEventListener('change', e => { A.roadClass = e.target.value; });
  $('new-prop-kind').addEventListener('change', e => { A.propKind = e.target.value; A.setTool('prop'); });
  $('proj-name').addEventListener('change', e => { A.project.name = e.target.value; });
  $('btn-sample').addEventListener('click', () => { A.loadProject(sampleProject()); });
  $('btn-new').addEventListener('click', () => { A.loadProject(createEmptyProject()); });
  $('btn-save').addEventListener('click', saveProject);
  $('btn-load').addEventListener('click', () => $('file-load').click());
  $('file-load').addEventListener('change', loadFile);
  $('btn-frame').addEventListener('click', frameAll);
  $('btn-delete').addEventListener('click', () => A.deleteSelection());

  $('outliner').addEventListener('click', e => {
    const row = e.target.closest('[data-sel]');
    if (!row) return;
    A.selection = { type: row.dataset.sel, id: row.dataset.id };
    if (row.dataset.sel === 'road' || row.dataset.sel === 'node' || row.dataset.sel === 'prop') { refreshAll(A); }
  });

  // Inspector: one delegated handler for all fields.
  for (const id of ['panel-inspector', 'panel-materials']) {
    const el = $(id);
    el.addEventListener('input', onField);
    el.addEventListener('change', onField);
    el.addEventListener('click', onAction);
  }

  window.addEventListener('keydown', e => {
    const tag = (e.target.tagName || '').toLowerCase();
    if (tag === 'input' || tag === 'select' || tag === 'textarea') return;
    if (e.key === 'v' || e.key === 'V') A.setTool('select');
    else if (e.key === 'r' || e.key === 'R') A.setTool('road');
    else if (e.key === 'n' || e.key === 'N') A.setTool('node');
    else if (e.key === 'p' || e.key === 'P') A.setTool('prop');
    else if (e.key === 'Escape') { A.draft = null; A.setTool('select'); refreshAll(A); }
    else if (e.key === 'Delete' || e.key === 'Backspace') { A.deleteSelection(); }
    else if (e.key === 'f' || e.key === 'F') frameAll();
    else if (e.key === 'd' || e.key === 'D') { flipSelected(); }
  });
  refreshAll(A);
}

function flipSelected() { if (A.selection?.type === 'road') A.flipSelectedRoad(); }

function onField(e) {
  const el = e.target;
  if (el.dataset.act) return;
  const t = el.dataset.t;
  if (!t) return;
  let v;
  if (el.type === 'checkbox') v = el.checked;
  else if (el.dataset.kind === 'num') v = Number(el.value);
  else if (el.dataset.kind === 'text' || el.tagName === 'SELECT') v = el.value;
  else v = el.value;
  if (el.dataset.kind === 'num' && !Number.isFinite(v)) return;
  if (t === 'layer') {
    A.setValue('layer', null, el.dataset.k, v);
    const out = el.parentElement?.querySelector('output');
    if (out) out.textContent = fmtParam(el.dataset.k, v);
    if (e.type === 'change') refreshMaterials(A);
    return;
  }
  const id = el.dataset.id ?? A.selection?.id;
  if (t === 'settings') {
    A.setValue('settings', null, el.dataset.k, v);
    const out = el.parentElement?.querySelector('output');
    if (out) out.textContent = fmt(v, el.dataset.dp ? Number(el.dataset.dp) : 2);
    if (el.dataset.k === 'snap') refreshToolbar(A);
    if (e.type === 'change') refreshInspector(A);
    return;
  }
  A.setValue(t, id, el.dataset.k, v);
  const out = el.parentElement?.querySelector('output');
  if (out) out.textContent = fmt(v, el.dataset.dp ? Number(el.dataset.dp) : 2);
  if (e.type === 'change') {
    refreshOutliner(A);
    if (t === 'road' || t === 'prop' || t === 'node') refreshInspectorMetrics();
  }
}

function fmtParam(key, v) {
  const spec = PARAM_SPEC[key];
  return fmt(v, spec && spec.step >= 0.1 ? 1 : 2);
}

function onAction(e) {
  const b = e.target.closest('[data-act]');
  if (!b) return;
  const act = b.dataset.act;
  const idx = Number(b.dataset.idx);
  if (act === 'delete') A.deleteSelection();
  else if (act === 'flip') A.flipSelectedRoad();
  else if (act === 'select-layer') { A.layerSel = idx; refreshMaterials(A); }
  else if (act === 'toggle-layer') { const L = A.project.layers[A.materialSel][idx]; L.enabled = !L.enabled; A.renderer.markMaterialDirty(A.materialSel === 'asphalt' ? 0 : A.materialSel === 'concrete' ? 1 : 2); refreshMaterials(A); }
  else if (act === 'layer-up') A.moveLayer(idx, 1);
  else if (act === 'layer-down') A.moveLayer(idx, -1);
  else if (act === 'layer-del') A.removeLayer(idx);
  else if (act === 'layer-add') { const v = Number($('layer-type').value); A.addLayer(v); }
  else if (act === 'mat') { A.materialSel = b.dataset.mat; A.layerSel = 0; refreshMaterials(A); }
  else if (act === 'reset-mat') A.resetMaterial(A.materialSel);
  else if (act === 'focus') frameSelected();
  else if (act === 'sel-node') { A.selection = { type: 'node', id: b.dataset.id }; refreshAll(A); }
}

function frameAll() {
  const pts = A.project.nodes.map(n => [n.x, n.z]);
  if (!pts.length) return;
  const xs = pts.map(p => p[0]), zs = pts.map(p => p[1]);
  A.cam.target = [(Math.min(...xs) + Math.max(...xs)) / 2, 0, (Math.min(...zs) + Math.max(...zs)) / 2];
  A.cam.dist = Math.max(120, Math.hypot(Math.max(...xs) - Math.min(...xs), Math.max(...zs) - Math.min(...zs)) * 1.2);
}

function frameSelected() {
  const s = A.selection;
  if (!s) return frameAll();
  if (s.type === 'node') { const n = A.project.nodes.find(q => q.id === s.id); if (n) { A.cam.target = [n.x, 0, n.z]; A.cam.dist = 120; } }
  else if (s.type === 'prop') { const q = A.project.props.find(p => p.id === s.id); if (q) { A.cam.target = [q.x, 0, q.z]; A.cam.dist = 80; } }
  else if (s.type === 'road') {
    const r = A.project.roads.find(q => q.id === s.id);
    const a = r && A.project.nodes.find(n => n.id === r.a), b = r && A.project.nodes.find(n => n.id === r.b);
    if (a && b) { A.cam.target = [(a.x + b.x) / 2, 0, (a.z + b.z) / 2]; A.cam.dist = Math.max(90, Math.hypot(a.x - b.x, a.z - b.z) * 1.1); }
  }
}

function saveProject() {
  const data = JSON.stringify({ ...A.project, layers: A.project.layers }, null, 2);
  const blob = new Blob([data], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `${(A.project.name || 'road-network').replace(/[^\w-]+/g, '_')}.json`;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

async function loadFile(e) {
  const f = e.target.files && e.target.files[0];
  if (!f) return;
  try {
    const obj = JSON.parse(await f.text());
    A.loadProject(obj);
  } catch (err) {
    alert('Could not read that project file: ' + err.message);
  }
  e.target.value = '';
}

// ---------- refresh -----------------------------------------------------------------------------
export function refreshAll(app) {
  if (!app.project) return;
  refreshToolbar(app);
  refreshOutliner(app);
  refreshTabs();
  refreshInspector(app);
  refreshMaterials(app);
  refreshHud(app);
  const nm = $('proj-name');
  if (nm && document.activeElement !== nm) nm.value = app.project.name || '';
}

export function refreshToolbar(app) {
  if (!app.project) return;
  document.querySelectorAll('[data-tool]').forEach(b => b.classList.toggle('active', b.dataset.tool === app.tool));
  document.querySelectorAll('[data-toggle]').forEach(b => {
    const on = !!app.project.settings[b.dataset.toggle];
    b.classList.toggle('on', on);
    b.setAttribute('aria-pressed', on ? 'true' : 'false');
  });
  const rc = $('new-road-class');
  if (rc && rc.options.length === 0) {
    for (const [k, c] of Object.entries(ROAD_CLASSES)) rc.add(new Option(c.label, k));
  }
  if (rc) rc.value = app.roadClass;
  const pk = $('new-prop-kind');
  if (pk && pk.options.length === 0) for (const [k, l] of Object.entries(PROP_KINDS)) pk.add(new Option(l, k));
  if (pk) pk.value = app.propKind;
  let hint = TOOL_HINTS[app.tool] || '';
  hint = hint.replace('{kind}', PROP_KINDS[app.propKind] || 'prop');
  $('hud-hint').textContent = hint;
}

function refreshTabs() {
  document.querySelectorAll('[data-tab]').forEach(b => b.classList.toggle('active', b.dataset.tab === A.tab));
  $('panel-inspector').hidden = A.tab !== 'inspector';
  $('panel-materials').hidden = A.tab !== 'materials';
}

export function refreshOutliner(app) {
  if (!app.project) return;
  const p = app.project;
  const sel = app.selection;
  const deg = new Map();
  for (const n of p.nodes) deg.set(n.id, 0);
  for (const r of p.roads) { deg.set(r.a, (deg.get(r.a) || 0) + 1); deg.set(r.b, (deg.get(r.b) || 0) + 1); }
  const row = (type, id, label, meta) => `<button class="row${sel && sel.type === type && sel.id === id ? ' selected' : ''}" data-sel="${type}" data-id="${esc(id)}"><span class="row-accent"></span><span class="row-label">${esc(label)}</span><span class="row-meta">${esc(meta)}</span></button>`;
  const nodes = p.nodes.map(n => row('node', n.id, n.name || 'Intersection', `${deg.get(n.id) || 0}-way`)).join('') || '<div class="empty">No intersections</div>';
  const roads = p.roads.map(r => row('road', r.id, r.name || 'Road', `${ROAD_CLASSES[r.cls]?.label || r.cls}${r.hidden ? ' · hidden' : ''}`)).join('') || '<div class="empty">No roads</div>';
  const props = p.props.map(q => row('prop', q.id, PROP_KINDS[q.kind] || q.kind, q.design || '')).join('') || '<div class="empty">No props</div>';
  $('outliner').innerHTML = `
    <section><header><span>Intersections</span><em>${p.nodes.length}</em></header>${nodes}</section>
    <section><header><span>Roads</span><em>${p.roads.length}</em></header>${roads}</section>
    <section><header><span>Props</span><em>${p.props.length}</em></header>${props}</section>
    <section><header><span>Traffic</span><em>${p.settings.trafficOn ? (app.traffic.vehicles.length + ' / ' + MAX_VEHICLES) : 'off'}</em></header>
      <div class="kv"><span>Vehicles</span><b>${app.traffic.vehicles.length}</b></div>
      <div class="kv"><span>Junction transfers</span><b>${app.traffic.stats.transfers}</b></div>
    </section>`;
}

function ctrlRange(label, key, min, max, step, value, dp = 2, unit = '') {
  return `<div class="ctl"><label>${esc(label)}</label><div class="ctl-r"><input type="range" data-kind="num" data-k="${key}" min="${min}" max="${max}" step="${step}" value="${value}" data-dp="${dp}" style="--p:${((value - min) / (max - min) * 100).toFixed(1)}%"><output>${fmt(value, dp)}${unit}</output></div></div>`;
}
function ctrlNum(label, key, value, step, unit = '') {
  return `<div class="ctl"><label>${esc(label)}</label><div class="ctl-r"><input type="number" data-kind="num" data-k="${key}" step="${step}" value="${fmt(value, 2)}">${unit ? `<span class="unit">${unit}</span>` : ''}</div></div>`;
}
function ctrlSelect(label, key, options, value) {
  return `<div class="ctl"><label>${esc(label)}</label><select data-kind="text" data-k="${key}">${options.map(([v, l]) => `<option value="${esc(v)}"${v === value ? ' selected' : ''}>${esc(l)}</option>`).join('')}</select></div>`;
}
function ctrlToggle(label, key, value) {
  return `<label class="toggle-row"><span>${esc(label)}</span><input type="checkbox" data-k="${key}"${value ? ' checked' : ''}><i class="sw"></i></label>`;
}
function card(title, body, off = false) {
  return `<section class="card${off ? ' feature-disabled' : ''}"><h3>${esc(title)}</h3>${body}</section>`;
}

export function refreshInspector(app) {
  const el = $('panel-inspector');
  if (!el || !app.project) return;
  const p = app.project;
  const sel = app.selection;
  let html = '';
  if (!sel) {
    html = settingsHtml(p.settings);
  } else if (sel.type === 'node') {
    const n = p.nodes.find(q => q.id === sel.id);
    if (!n) { el.innerHTML = ''; return; }
    const deg = p.roads.filter(r => r.a === n.id || r.b === n.id).length;
    html = `
      ${card('Intersection', `
        <div class="ctl"><label>Name</label><input type="text" data-kind="text" data-t="node" data-id="${esc(n.id)}" data-k="name" value="${esc(n.name)}"></div>
        ${ctrlNum('X (m)', 'x', n.x, 0.5)}
        ${ctrlNum('Z (m)', 'z', n.z, 0.5)}
        ${ctrlRange('Scale', 'scale', 0.5, 2, 0.05, n.scale || 1, 2)}
        ${ctrlToggle('Signal controlled', 'signal', n.signal)}
        <div class="kv"><span>Approaches</span><b>${deg}</b></div>
        <div class="kv"><span>Junction mesh</span><b>${junctionState(n.id)}</b></div>
        <p class="note">Drag the gizmo in the viewport to move this intersection and its approaches together.</p>`)}
      <div class="footer"><button class="danger" data-act="delete">Delete intersection</button></div>`;
  } else if (sel.type === 'road') {
    const r = p.roads.find(q => q.id === sel.id);
    if (!r) { el.innerHTML = ''; return; }
    const info = app.net && app.net.roadInfo.get(r.id);
    const cls = Object.entries(ROAD_CLASSES).map(([k, c]) => [k, c.label]);
    html = `
      ${card('Road', `
        <div class="ctl"><label>Name</label><input type="text" data-kind="text" data-t="road" data-id="${esc(r.id)}" data-k="name" value="${esc(r.name)}"></div>
        ${ctrlSelect('Class', 'cls', cls, r.cls)}
        <div class="metrics"><div><span>Length</span><b>${info ? fmt(info.L, 0) : '—'} m</b></div><div><span>Ends</span><b>${esc(nodeName(r.a))} → ${esc(nodeName(r.b))}</b></div></div>
        <div class="footer inline"><button data-act="flip">Flip direction</button></div>`)}
      ${card('Carriageway', `
        ${ctrlRange('Lanes per direction', 'lanesPerDir', 1, 4, 1, r.lanesPerDir, 0)}
        ${ctrlRange('Lane width', 'laneWidth', 2.8, 4.2, 0.05, r.laneWidth, 2, ' m')}
        ${ctrlRange('Design speed', 'speed', 20, 120, 5, r.speed, 0, ' km/h')}
        ${ctrlSelect('Surface', 'surface', [['asphalt', 'Asphalt'], ['concrete', 'Concrete'], ['pavers', 'Pavers']], r.surface)}
        ${ctrlToggle('Median', 'median', r.median)}
        ${ctrlRange('Median width', 'medianWidth', 0, 8, 0.1, r.medianWidth, 1, ' m')}
        ${ctrlToggle('Lane markings', 'markings', r.markings)}
        ${ctrlToggle('Crosswalks at junctions', 'crosswalks', r.crosswalks)}
        ${ctrlToggle('Traffic', 'traffic', r.traffic)}`)}
      ${card('Pavement & curbs', `
        ${ctrlSelect('Sidewalks', 'sidewalk', [['both', 'Both sides'], ['left', 'Left only'], ['right', 'Right only'], ['none', 'None']], r.sidewalk)}
        ${ctrlRange('Sidewalk width', 'sidewalkWidth', 0, 5, 0.1, r.sidewalkWidth, 1, ' m')}
        ${ctrlSelect('Pavement material', 'sidewalkMat', [['concrete', 'Concrete'], ['asphalt', 'Asphalt'], ['pavers', 'Pavers']], r.sidewalkMat)}
        ${ctrlRange('Light spacing', 'lightSpacing', 15, 80, 1, r.lightSpacing, 0, ' m')}
        ${ctrlToggle('Guard rails', 'railings.on', r.railings.on)}`)}
      ${card('Hazards & works', `
        ${ctrlRange('Speed bumps', 'speedBumps', 0, 8, 1, r.speedBumps, 0)}
        ${ctrlRange('Potholes', 'potholes', 0, 12, 1, r.potholes, 0)}
        ${ctrlToggle('Bollards', 'bollards.on', r.bollards.on)}
        ${ctrlSelect('Bollard design', 'bollards.design', Object.entries(BOLLARD_DESIGNS).map(([k, v]) => [k, v]), r.bollards.design)}
        ${ctrlRange('Bollard spacing', 'bollards.spacing', 2, 12, 0.5, r.bollards.spacing, 1, ' m')}
        ${ctrlToggle('Construction / rebuilding', 'construction.on', r.construction.on)}
        ${ctrlRange('Works start', 'construction.from', 0, 1, 0.01, r.construction.from, 2)}
        ${ctrlRange('Works end', 'construction.to', 0, 1, 0.01, r.construction.to, 2)}
        ${ctrlSelect('Works side', 'construction.side', [['left', 'Left'], ['right', 'Right']], r.construction.side)}`)}
      <div class="footer"><button class="danger" data-act="delete">Delete road</button></div>`;
  } else if (sel.type === 'prop') {
    const q = p.props.find(x => x.id === sel.id);
    if (!q) { el.innerHTML = ''; return; }
    html = `
      ${card(PROP_KINDS[q.kind] || 'Prop', `
        ${ctrlSelect('Kind', 'kind', Object.entries(PROP_KINDS), q.kind)}
        ${ctrlNum('X (m)', 'x', q.x, 0.5)}
        ${ctrlNum('Z (m)', 'z', q.z, 0.5)}
        ${ctrlRange('Rotation', 'rot', 0, 6.28, 0.01, q.rot || 0, 2, ' rad')}
        ${ctrlRange('Scale', 'scale', 0.5, 2, 0.05, q.scale || 1, 2)}
        ${q.kind === 'bollard' ? ctrlSelect('Design', 'design', Object.entries(BOLLARD_DESIGNS).map(([k, v]) => [k, v]), q.design) : ''}`)}
      <div class="footer"><button class="danger" data-act="delete">Delete prop</button></div>`;
  }
  el.innerHTML = html;
  el.querySelectorAll('[data-k]').forEach(() => {});
  // Use the correct target/id for every control in the card
  el.querySelectorAll('input[data-k],select[data-k]').forEach(c => {
    if (!c.dataset.t) { c.dataset.t = sel ? sel.type : 'settings'; }
    if (sel && !c.dataset.id) c.dataset.id = sel.id;
  });
  // Settings controls
  if (!sel) el.querySelectorAll('[data-k]').forEach(c => { c.dataset.t = 'settings'; });
}

function nodeName(id) {
  const n = A.project.nodes.find(q => q.id === id);
  return n ? n.name : id;
}

function junctionState(id) {
  const rep = A.net && A.net.junctionReports.find(r => r.nodeId === id);
  if (!rep) return '—';
  if (rep.overlap) return 'Dense – review';
  if (rep.radialFallback) return 'Radial fallback';
  return `Clean · ${rep.quads} quads`;
}

function refreshInspectorMetrics() {
  // Metrics change only after rebuild; the HUD and outliner are updated there.
}

function settingsHtml(s) {
  const tiles = ['asphalt', 'concrete', 'pavers'].map(k => ctrlRange(`${k[0].toUpperCase()}${k.slice(1)} tile`, `textureTile.${k}`, 0.5, 24, 0.1, s.textureTile[k], 1, ' m')).join('');
  return `
    ${card('Scene', `
      <p class="note">Select an intersection, road or prop to edit it. This panel controls the whole scene.</p>
      ${ctrlRange('Sun azimuth', 'sunAzimuth', 0, 360, 1, s.sunAzimuth, 0, '°')}
      ${ctrlRange('Sun elevation', 'sunElevation', 5, 85, 1, s.sunElevation, 0, '°')}
      ${ctrlRange('Exposure', 'exposure', 0.4, 2, 0.01, s.exposure, 2)}
      ${ctrlRange('Wetness', 'wetness', 0, 1, 0.01, s.wetness, 2)}
      ${ctrlRange('Haze', 'fogDensity', 0, 0.004, 0.0001, s.fogDensity, 4)}
    `)}
    ${card('Traffic', `
      ${ctrlToggle('AI traffic', 'trafficOn', s.trafficOn)}
      ${ctrlRange(`Vehicles (max ${MAX_VEHICLES})`, 'trafficCount', 0, MAX_VEHICLES, 1, Math.min(MAX_VEHICLES, s.trafficCount), 0)}
      <p class="note">Right-hand traffic. Vehicles change approach through a curved path across the junction. Models: ${Object.values(VEHICLE_TYPES).map(v => v.label).join(', ')}.</p>
    `, !s.trafficOn)}
    ${card('Markings & pavement', `
      ${ctrlToggle('Lane markings', 'markingsOn', s.markingsOn)}
      ${ctrlToggle('Snap to 0.5 m', 'snap', s.snap)}
      ${tiles}
    `)}`;
}

// The settings panel needs the 'settings' target on every control; the generic pass above sets it.

export function refreshMaterials(app) {
  const el = $('panel-materials');
  if (!el || el.hidden) return;
  const mat = app.materialSel;
  const stack = app.project.layers[mat];
  const sel = Math.min(app.layerSel, Math.max(0, stack.length - 1));
  app.layerSel = sel;
  const L = stack[sel];
  const typeOptions = LAYER_TYPES.map(t => `<option value="${t.id}">${esc(t.name)}</option>`).join('');
  const matBtns = ['asphalt', 'concrete', 'pavers'].map(m => `<button class="seg${m === mat ? ' active' : ''}" data-act="mat" data-mat="${m}">${m[0].toUpperCase() + m.slice(1)}</button>`).join('');
  // Top of the stack is listed first, matching a layer panel; the compositor evaluates bottom-up.
  const list = stack.map((l, i) => ({ l, i })).reverse().map(({ l, i }) => `
    <div class="layer${i === sel ? ' selected' : ''}${l.enabled ? '' : ' off'}" data-act="select-layer" data-idx="${i}">
      <button class="eye" data-act="toggle-layer" data-idx="${i}" title="Toggle layer">${l.enabled ? '●' : '○'}</button>
      <span class="layer-name">${esc(l.name || LAYER_TYPES[l.type].name)}</span>
      <span class="layer-tools">
        <button data-act="layer-up" data-idx="${i}" title="Move up">▲</button>
        <button data-act="layer-down" data-idx="${i}" title="Move down">▼</button>
        <button data-act="layer-del" data-idx="${i}" title="Delete">✕</button>
      </span>
    </div>`).join('') || '<div class="empty">No layers</div>';
  let params = '<div class="empty">Select a layer</div>';
  if (L) {
    const t = LAYER_TYPES[L.type];
    const rows = t.params.map(k => {
      const spec = PARAM_SPEC[k];
      const label = (t.labels && t.labels[k]) || spec.label;
      if (spec.type === 'color') {
        return `<div class="ctl"><label>${esc(label)}</label><div class="ctl-r"><input type="color" data-kind="text" data-t="layer" data-k="${k}" value="${rgbHex(L[k])}"></div></div>`;
      }
      const v = L[k] ?? 0;
      return `<div class="ctl"><label>${esc(label)}</label><div class="ctl-r"><input type="range" data-kind="num" data-t="layer" data-k="${k}" min="${spec.min}" max="${spec.max}" step="${spec.step}" value="${v}" style="--p:${((v - spec.min) / (spec.max - spec.min) * 100).toFixed(1)}%"><output>${fmt(v, spec.step >= 0.1 ? 1 : 2)}</output></div></div>`;
    }).join('');
    params = `
      <div class="ctl"><label>Name</label><input type="text" data-kind="text" data-t="layer" data-k="name" value="${esc(L.name)}"></div>
      ${rows}
      <div class="ctl"><label>Blend</label><select data-kind="num" data-t="layer" data-k="blend"><option value="0"${L.blend === 0 ? ' selected' : ''}>Normal</option><option value="1"${L.blend === 1 ? ' selected' : ''}>Multiply</option></select></div>
      <div class="ctl"><label>Opacity</label><div class="ctl-r"><input type="range" data-kind="num" data-t="layer" data-k="opacity" min="0" max="1" step="0.01" value="${L.opacity}"><output>${fmt(L.opacity, 2)}</output></div></div>
      <div class="ctl"><label>Seed</label><div class="ctl-r"><input type="number" data-kind="num" data-t="layer" data-k="seed" step="1" value="${L.seed}"></div></div>`;
  }
  el.innerHTML = `
    <section class="card">
      <h3>Texture stack</h3>
      <div class="seg-row">${matBtns}</div>
      <p class="note">Each surface is a stack of procedural layers. Top layers paint over the layers beneath. The compositor writes an albedo and a height/roughness map at 1024² with full mip chains.</p>
      <div class="ctl"><label>Tile size</label><div class="ctl-r"><input type="range" data-kind="num" data-t="settings" data-k="textureTile.${mat}" min="0.5" max="24" step="0.1" value="${app.project.settings.textureTile[mat]}" style="--p:${((app.project.settings.textureTile[mat] - 0.5) / 23.5 * 100).toFixed(1)}%" data-dp="1"><output>${fmt(app.project.settings.textureTile[mat], 1)} m</output></div></div>
    </section>
    <section class="card layers-card">
      <h3>Layers <em>${stack.length}/${MAX_LAYERS}</em></h3>
      <div class="layer-list">${list}</div>
      <div class="add-row"><select id="layer-type">${typeOptions}</select><button data-act="layer-add">Add</button></div>
    </section>
    <section class="card">
      <h3>${esc(L ? (L.name || LAYER_TYPES[L.type].name) : 'Layer')}</h3>
      ${params}
    </section>
    <div class="footer inline"><button data-act="reset-mat">Reset ${esc(mat)} stack</button></div>`;
  // Layer-level controls target the layer; the tile size targets settings.
  el.querySelectorAll('[data-t="layer"]').forEach(c => c.dataset.t = 'layer');
  el.querySelectorAll('[data-t="settings"]').forEach(c => c.dataset.t = 'settings');
}

function rgbHex(c) {
  const [r, g, b] = hexToRgb(typeof c === 'string' ? c : '#808080');
  return '#' + [r, g, b].map(v => Math.round(v * 255).toString(16).padStart(2, '0')).join('');
}

export function onNetworkRebuilt(app) {
  refreshHud(app);
  refreshOutliner(app);
  if (app.selection && app.selection.type === 'node') refreshInspector(app);
}

export function refreshHud(app) {
  const r = app.renderer;
  const st = r && r.stats ? r.stats : { frameMs: 0, drawCalls: 0, triangles: 0 };
  const bs = app.buildStats || {};
  const overlaps = app.net ? app.net.junctionReports.filter(j => j.overlap).length : 0;
  const fps = st.frameMs > 0 ? (1000 / st.frameMs) : 0;
  $('hud-stats').innerHTML = `
    <span>FPS <b>${fps.toFixed(0)}</b></span>
    <span>Build <b>${fmt(app.lastBuildMs, 0)} ms</b></span>
    <span>Quads <b>${(bs.quads || 0).toLocaleString()}</b></span>
    <span>Tris <b>${(st.triangles || 0).toLocaleString()}</b></span>
    <span>Draws <b>${st.drawCalls || 0}</b></span>
    <span>Vehicles <b>${app.traffic.vehicles.length}/${MAX_VEHICLES}</b></span>
    <span>Junctions <b class="${overlaps ? 'warn' : ''}">${app.net ? app.net.junctionReports.length : 0}${overlaps ? ' · ' + overlaps + ' dense' : ''}</b></span>`;
}

export function showFatal(msg) {
  const f = $('fatal');
  f.hidden = false;
  f.querySelector('p').textContent = msg;
}

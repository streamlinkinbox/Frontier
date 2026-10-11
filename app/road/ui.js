/* ============================================================
   FRONTIER · ROAD EDITOR — UI controller
   Outliner · details inspector · construct menu · city generator ·
   draw / edit / paint / measure tools · undo · import & export.
   Chrome vocabulary follows Experimental/FrontierEditor (Project Zero).
   ============================================================ */
import * as THREE from '../vendor/three.module.js';
import { RoadScene, LAYERS, frameAtLocal } from './scene.js';
import { PROFILES, profileById, solveSection, SURFACES, CLASS_COLOR, levelAt } from './profiles.js';
import { PROP_SPECS } from './props.js';
import { generateCity, DEFAULT_CITY, CITY_PRESETS, applyPreset } from './generator.js';
import { validateNetwork, prepareRoad, framesOf } from './mesh.js';
import { clamp, dist, snap, snapAngle, strSeed } from './lib.js';

const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
const esc = s => String(s ?? '').replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const m2 = v => (Math.round(v * 10) / 10).toFixed(1);
const km = v => (v / 1000).toFixed(v > 999 ? 0 : 2);
const icon = (n, cls = 'i') => `<svg class="${cls}"><use href="#i-${n}"/></svg>`;
const SUF = { motorway: 'Motorway', freeway: 'Freeway', expressway: 'Expressway', ramp: 'Ramp', arterial: 'Avenue', boulevard: 'Boulevard', collector: 'Street', local: 'Street', residential: 'Street', shared: 'Woonerf', service: 'Lane', industrial: 'Road', park: 'Walk', cycle: 'Way', path: 'Path', bus: 'Route', busway: 'Busway', pedestrian: 'Mall', plaza: 'Plaza', parking: 'Lot', 'parking-lot': 'Lot', rail: 'Line', tram: 'Tramway', subway: 'Metro', tunnel: 'Tunnel', bridge: 'Bridge', viaduct: 'Viaduct', underpass: 'Underpass', culdesac: 'Close', 'cul-de-sac': 'Close', roundabout: 'Roundabout', rural: 'Road', gravel: 'Track', unknown: 'Road' };
const clsLabel = c => (c || 'unknown').replace(/(^|[-\s])\w/g, m => m.toUpperCase());
const roadLabel = r => r.name || `${clsLabel(r.cls)} ${SUF[r.cls] ? '' : ''}`.trim() || r.id;

/* ------------------------------- state ------------------------------- */
const state = {
  network: { roads: [], nodes: [], water: [], areas: [], blocks: [], massing: [], meta: {}, params: { ...DEFAULT_CITY } },
  params: { ...DEFAULT_CITY }, preset: 'river-capital',
  sel: null, hover: null, muted: new Set(), collapsed: new Set(),
  tool: 'select', activeProfile: 'art-ave-4', tab: 'details',
  draw: null, measure: null, paint: 'cushion', filter: '',
  undo: [], redo: [], dirty: false, busy: false, pendingTimer: 0, lastBuildMs: 0,
  snap: { grid: 1, node: 1, angle: 0 }
};
let scene;

/* ------------------------------- boot ------------------------------- */
const host = $('#gl');
scene = new RoadScene(host, {
  onFrame: h => {
    $('#stFps').textContent = Math.round(h.fps);
    $('#stCalls').textContent = h.calls;
    $('#stTris').textContent = h.tris > 9999 ? (h.tris / 1000).toFixed(0) + 'k' : h.tris;
    $('#stInst').textContent = scene.instanced.reduce((s, i) => s + i.count, 0);
    $('#stPos').textContent = `${h.pos.x.toFixed(0)}, ${h.pos.y.toFixed(0)}, ${h.pos.z.toFixed(0)}`;
    $('#camSpeed').textContent = h.speed.toFixed(h.speed < 10 ? 1 : 0);
    $('#stMode').textContent = scene.mode;
    const nowM = performance.now();
    if (state.miniDirty || nowM - (state.miniT || 0) > 220) { state.miniT = nowM; state.miniDirty = 0; drawMini(); }
  },
  onPick: hit => onSelect(hit),
  onRightClick: (hit, _s, e) => { if (state.tool === 'select') { } else e.preventDefault(); },
  onHover: hit => {
    const id = hit ? hit.id : null;
    if (id !== state.hover) { state.hover = id; scene.setHover(id); $$('.row[data-id]', $('#tree')).forEach(r => r.classList.toggle('hov', r.dataset.id === id)); }
    host.style.cursor = state.tool === 'draw' ? 'crosshair' : state.tool === 'erase' ? 'cell' : hit ? 'pointer' : 'default';
  },
  getFov: () => state.fov || 55,
  onKey: e => onKey(e),
  onSun: () => { },
  getPrevMode: () => state.prevCam || 'fly'
});
scene.init();
scene.flags.detail = 1;
if (typeof globalThis !== 'undefined') globalThis.__scene = scene;   // harness/diagnostics hook
try { scene.mode = 'fly'; } catch (e) { }

/* ------------------------------- build pipeline ------------------------------- */
function viewNetwork() {
  const n = state.network;
  if (!state.muted.size) return n;
  return { ...n, roads: n.roads.filter(r => !state.muted.has(r.id)), nodes: n.nodes.filter(nd => nd.arms.some(a => !state.muted.has(a.road))) };
}
function build({ immediate = true, reflow = true } = {}) {
  clearTimeout(state.pendingTimer);
  const run = () => {
    const t0 = performance.now();
    state.busy = true;
    measureLengths();
    $('#stBuild').innerHTML = `<span class="spin"></span> building…`;
    const stats = scene.setNetwork(viewNetwork());
    state.lastBuildMs = Math.round(performance.now() - t0);
    state.busy = false;
    $('#stBuild').innerHTML = `<b>${state.lastBuildMs}</b> ms build`;
    $('#stInst').textContent = scene.instanced.reduce((s, i) => s + i.count, 0);
    if (reflow) { renderTree(); renderDetails(); }
    if (state.validateAfterBuild) { state.validateAfterBuild = false; validateNow(); }
    $('#countRoads').textContent = state.network.roads.length;
    $('#footCount').textContent = state.network.roads.length + state.network.nodes.length + (state.network.areas?.length || 0) + (state.network.water?.length || 0);
    $('#vpTitle').textContent = `${state.preset} · seed ${state.params.seed}`;
    $('#stSeed').textContent = state.params.seed;
    return stats;
  };
  if (immediate) return run();
  $('#stBuild').innerHTML = `<span class="spin"></span> pending…`;
  state.pendingTimer = setTimeout(run, 150);
}
function validateNow() {
  const v = validateNetwork(viewNetwork());
  state.problems = v.problems;
  const ok = !v.problems.length;
  $('#stMsg').className = 's ' + (ok ? 'ok' : 'warn');
  $('#stMsg').innerHTML = icon(ok ? 'check' : 'alert', 'i sm') + (ok ? `${v.stats.roads} roads · network valid` : `${v.problems.length} issue${v.problems.length > 1 ? 's' : ''}`);
  $('#stMsg').title = v.problems.slice(0, 14).join('\n');
  return v;
}
const edit = (label, fn, { live = false } = {}) => {
  pushUndo();
  fn();
  markDirty();
  build({ immediate: !live });
};
function snapshot() {
  return JSON.stringify({ roads: state.network.roads, nodes: state.network.nodes, areas: state.network.areas, water: state.network.water, massing: state.network.massing, params: state.params });
}
function pushUndo() {
  state.undo.push(snapshot());
  if (state.undo.length > 40) state.undo.shift();
  state.redo.length = 0;
  $('#uolo').disabled = false; $('#uered').disabled = true;
}
function restore(json) {
  const o = JSON.parse(json);
  Object.assign(state.network, o);
  Object.assign(state.params, o.params || {});
  state.sel = null;
  scene.setSelection(null);
  build();
}
const undo = () => { const s = state.undo.pop(); if (!s) return toast('Nothing to undo'); state.redo.push(snapshot()); restore(s); toast('Undo', 'info'); };
const redo = () => { const s = state.redo.pop(); if (!s) return toast('Nothing to redo'); state.undo.push(snapshot()); restore(s); toast('Redo', 'info'); };
const markDirty = () => { state.dirty = true; $('#saveState').className = 'pill dirty'; $('#saveState span').textContent = 'Unsaved edits'; };
const markClean = (t = 'Autosaved') => { state.dirty = false; $('#saveState').className = 'pill'; $('#saveState span').textContent = t; };

/* ------------------------------- selection ------------------------------- */
function renderAreaDetails(body, area) {
  const poly = area.polygon || area.points || [];
  const water = !!area.points && !area.polygon;
  let A = 0;
  for (let i = 0, n = poly.length; i < n; i++) { const p = poly[i], q = poly[(i + 1) % n]; A += p.x * q.z - q.x * p.z; }
  A = Math.abs(A / 2);
  const per = poly.reduce((s, p, i) => s + dist(p, poly[(i + 1) % poly.length]), 0);
  const xs = poly.map(p => p.x), zs = poly.map(p => p.z);
  setHead(water ? 'Water body' : 'Open space', esc(area.id), `<span class="chip">${esc(area.kind || area.mat || (water ? 'river' : 'area'))}</span><span>${poly.length} points · ${A > 1e4 ? (A / 1e4).toFixed(1) + ' ha' : m2(A) + ' m²'}</span>`, water ? 'water' : 'tree', water ? '#74bdd4' : '#93c779');
  const c = document.createElement('div');
  c.className = 'card';
  const w = Math.max(1, Math.max(...xs) - Math.min(...xs)), d = Math.max(1, Math.max(...zs) - Math.min(...zs));
  c.innerHTML = `<div class="card-head"><span>${icon(water ? 'water' : 'map')}Outline</span><span class="act"><span class="chip">${m2(w)} × ${m2(d)} m</span></span></div>
    <div class="section-view">${areaSVG(poly, area, water)}</div>
    <div class="grid3" style="margin-top:14px">
      <div>${metric((A / 1e4).toFixed(2), 'ha', 'sm')}<p class="muted">footprint</p></div>
      <div>${metric(m2(per), 'm', 'sm')}<p class="muted">perimeter</p></div>
      <div>${metric(m2(area.width || 0), 'm', 'sm')}<p class="muted">${water ? 'channel width' : 'n/a'}</p></div>
    </div>`;
  body.appendChild(c);
  const g = document.createElement('div');
  g.className = 'card';
  g.innerHTML = `<div class="card-head"><span>${icon('sliders')}Editing</span></div>`;
  if (water) {
    g.appendChild(slider({ label: 'Channel width', min: 8, max: 140, step: 1, unit: 'm', value: area.width || 30, onInput: (v, done) => edit('water width', () => { area.width = v; }) }));
    g.appendChild(slider({ label: 'Water level', min: -4, max: 2, step: .05, unit: 'm', value: area.y ?? -1.35, onInput: (v, done) => edit('water y', () => { area.y = v; }) }));
    g.appendChild(slider({ label: 'Quay step', min: 4, max: 30, step: .5, unit: 'm', value: area.step || 9, onInput: (v, done) => edit('water step', () => { area.step = v; }) }));
    const tg = document.createElement('div'); tg.className = 'tog-grid';
    tg.appendChild(toggle({ label: 'Quay lamps', on: area.lamps !== false, onChange: v => edit('lamps', () => { area.lamps = v; }) }));
    tg.appendChild(toggle({ label: 'Bank trees', on: area.trees !== false, onChange: v => edit('trees', () => { area.trees = v; }) }));
    g.appendChild(tg);
  } else {
    g.appendChild(segmented({ label: 'Surface', options: ['grass', 'paver', 'grass-mown', 'gravel', 'tile', 'soil'].map(v => ({ label: clsLabel(v), value: v })), value: area.mat || 'grass', onChange: v => edit('area mat', () => { area.mat = v; }) }));
    g.appendChild(slider({ label: 'Scatter count', min: 0, max: 300, step: 1, value: area.scatter?.count || 0, onInput: (v, done) => edit('scatter', () => { (area.scatter ||= {}).count = v | 0; }) }));
    g.appendChild(slider({ label: 'Perimeter prop spacing', min: 0, max: 60, step: .5, unit: 'm', value: area.perimeterSpacing || 0, onInput: (v, done) => edit('perimeter', () => { area.perimeterSpacing = v; }) }));
    g.appendChild(slider({ label: 'Level', min: -.4, max: 3, step: .05, unit: 'm', value: area.y ?? 0, onInput: (v, done) => edit('area y', () => { area.y = v; }) }));
  }
  body.appendChild(g);
  body.appendChild(btnRow([
    { label: 'Frame', ic: 'target', onClick: () => { const cx = (Math.min(...xs) + Math.max(...xs)) / 2, cz = (Math.min(...zs) + Math.max(...zs)) / 2; scene.focusOn(cx, cz, area.y || 0, Math.max(w, d) * 1.1, Math.max(w, d) * .5); } },
    { label: 'Delete', ic: 'trash', cls: 'danger', onClick: () => { pushUndo(); for (const k of ['areas', 'water']) state.network[k] = (state.network[k] || []).filter(x => x !== area); state.sel = null; markDirty(); build(); } },
  ]));
}
function areaSVG(poly, area, water) {
  const xs = poly.map(p => p.x), zs = poly.map(p => p.z);
  const x0 = Math.min(...xs), x1 = Math.max(...xs), z0 = Math.min(...zs), z1 = Math.max(...zs);
  const sc = Math.min(290 / Math.max(1, x1 - x0), 66 / Math.max(1, z1 - z0));
  const pts = poly.map(p => `${(8 + (p.x - x0) * sc).toFixed(1)},${(4 + (p.z - z0) * sc).toFixed(1)}`).join(' ');
  return `<svg viewBox="0 0 306 74" preserveAspectRatio="xMidYMid meet">
    <polygon points="${pts}" fill="${water ? 'rgba(88,140,180,.35)' : 'rgba(120,170,110,.28)'}" stroke="${water ? '#74bdd4' : '#93c779'}" stroke-width="1" ${area.closed ? '' : 'stroke-dasharray="4 3"'}/>
  </svg>`;
}
function onSelect(hit) {
  if (state.tool === 'draw') { addDrawPoint(hit); return; }
  if (state.tool === 'measure') { measureClick(hit); return; }
  if (state.tool === 'paint') { paintAt(hit); return; }
  if (state.tool === 'erase') { if (hit) deleteHit(hit); return; }
  if (state.tool === 'point' || state.tool === 'elev') { grabHandle(hit); return; }
  if (!hit) { state.sel = null; scene.setSelection(null); scene.setHover(null); renderDetails(); renderTree(); return; }
  state.sel = hit.kind === 'node' ? { kind: 'node', id: hit.node.id } : { kind: 'road', id: hit.id };
  scene.setSelection(hit.id);
  renderTree(); renderDetails(); markMini();
}
function markMini() { state.miniDirty = 1; }
function selectById(id, { frame = false } = {}) {
  const kind = state.network.nodes.some(n => n.id === id) ? 'node'
    : (state.network.roads || []).some(r => r.id === id) ? 'road' : 'area';
  state.sel = { kind, id };
  scene.setSelection(id);
  renderTree(); renderDetails(); markMini();
  if (frame) { scene.frameSelection(); }
}
function selectedRoad() { return state.sel?.kind === 'road' ? state.network.roads.find(r => r.id === state.sel.id) : null; }
function selectedNode() { return state.sel?.kind === 'node' ? state.network.nodes.find(n => n.id === state.sel.id) : null; }

/* ------------------------------- tree ------------------------------- */
const GROUPS = [
  { label: 'Freeways & ring', test: c => ['freeway', 'motorway', 'expressway', 'ramp'].includes(c) },
  { label: 'Avenues & boulevards', test: c => ['arterial', 'boulevard'].includes(c) },
  { label: 'Collectors & local streets', test: c => ['collector', 'local', 'residential', 'shared', 'service', 'industrial', 'gravel', 'rural', 'cul-de-sac', 'culdesac'].includes(c) },
  { label: 'Bus & cycle lanes', test: c => ['bus', 'busway', 'cycle'].includes(c) },
  { label: 'Pedestrian & paving', test: c => ['pedestrian', 'plaza', 'park', 'path', 'parking-lot', 'parking'].includes(c) },
  { label: 'Rail · tram · metro', test: c => ['rail', 'tram', 'subway', 'tunnel'].includes(c) },
  { label: 'Structures', test: c => ['bridge', 'viaduct', 'underpass', 'roundabout'].includes(c) },
];
function roadLength(r) { prepareRoad(r); const f = framesOf(r); return f.length ? f[f.length - 1].s : 0; }
function renderTree() {
  const root = $('#tree');
  const q = state.filter.trim().toLowerCase();
  const match = r => !q || (r.name || '').toLowerCase().includes(q) || r.id.toLowerCase().includes(q) || (r.cls || '').includes(q) || (r.profile + '').toLowerCase().includes(q);
  let html = `<div class="group"><button class="group-label" data-g="layers">${icon('eye', 'i sm')}Layers &amp; modes<span class="n">${LAYERS.filter(l => scene.layers[l.key]).length}/${LAYERS.length}</span></button>
    <div class="rows">${LAYERS.map(l => `<div class="row" data-layer="${l.key}">
      <span class="ico">${icon(layerIcon(l.key), 'i sm')}</span><span class="nm">${esc(l.label)}</span>
      <button class="eye ${scene.layers[l.key] === false ? 'off' : ''}" data-eye="${l.key}">${icon(scene.layers[l.key] === false ? 'eye-off' : 'eye', 'i sm')}</button></div>`).join('')}</div></div>`;
  for (const g of GROUPS) {
    const list = state.network.roads.filter(r => g.test(r.cls) && match(r));
    if (!list.length) continue;
    const len = list.reduce((s, r) => s + r._len, 0);
    html += groupHtml(g.label, 'g' + g.label, list.map(r => rowHtml(r)).join(''), `${list.length} · ${km(len)} km`);
  }
  const other = state.network.roads.filter(r => !GROUPS.some(g => g.test(r.cls)) && match(r));
  if (other.length) html += groupHtml('Other', 'go', other.map(r => rowHtml(r)).join(''), `${other.length}`);
  const nodes = state.network.nodes.filter(n => !q || (n.name || '').toLowerCase().includes(q));
  if (nodes.length) html += groupHtml('Junctions', 'nodes', nodes.slice(0, 260).map(n => `
    <div class="row ${state.sel?.id === n.id ? 'on' : ''}" data-id="${esc(n.id)}" data-kind="node">
      <span class="ico">${icon(n.style === 'signals' ? 'activity' : n.style === 'roundabout' ? 'orbit' : 'crosshair', 'i sm')}</span>
      <span class="nm">${esc(n.name || n.id)}</span><span class="meta">${n.arms.length} arm${n.arms.length > 1 ? 's' : ''}</span>
    </div>`).join(''), `${state.network.nodes.length} nodes`);
  const areas = [...(state.network.areas || []), ...(state.network.water || [])].filter(a => !q || (a.id || '').toLowerCase().includes(q));
  if (areas.length) html += groupHtml('Open space &amp; water', 'areas', areas.map(a => `
    <div class="row" data-id="${esc(a.id)}" data-kind="area">
      <span class="ico">${icon(a.points && !a.polygon ? 'water' : (a.mat || '').includes('water') ? 'water' : (a.kind === 'park' ? 'tree' : 'map'), 'i sm')}</span>
      <span class="nm">${esc(a.id)}</span><span class="meta">${esc(a.kind || a.mat || 'area')}</span></div>`).join(''), `${areas.length}`);
  root.innerHTML = html;
  root.querySelectorAll('.row[data-id]').forEach(el => {
    el.addEventListener('click', () => selectById(el.dataset.id, { frame: el.dataset.kind === 'node' }));
    el.addEventListener('pointerenter', () => { scene.setHover(el.dataset.id); });
    el.addEventListener('pointerleave', () => scene.setHover(state.hover));
  });
  root.querySelectorAll('[data-eye]').forEach(b => b.addEventListener('click', e => {
    e.stopPropagation();
    const k = b.dataset.eye;
    scene.setLayer(k, scene.layers[k] === false);
    renderTree();
  }));
  root.querySelectorAll('[data-layer]').forEach(el => el.addEventListener('dblclick', () => { scene.setLayer(el.dataset.layer, scene.layers[el.dataset.layer] === false); renderTree(); }));
  root.querySelectorAll('[data-g]').forEach(b => b.addEventListener('click', () => {
    const k = b.dataset.g;
    if (state.collapsed.has(k)) state.collapsed.delete(k); else state.collapsed.add(k);
    b.nextElementSibling.style.display = state.collapsed.has(k) ? 'none' : '';
    const ic = b.querySelector('use'); if (ic) ic.setAttribute('href', state.collapsed.has(k) ? '#i-chev-right' : '#i-chev-down');
  }));
}
const layerIcon = k => ({ surfaces: 'box', kerb: 'barrier', paint: 'flag', furniture: 'sliders', lighting: 'lamp', greenery: 'tree', safety: 'cone', transit: 'bus', rail: 'train', signing: 'flag', water: 'water', massing: 'building', underground: 'tunnel', traffic: 'car' }[k] || 'layers');
function groupHtml(label, key, rows, meta) {
  const closed = state.collapsed.has(key);
  return `<div class="group"><button class="group-label">${icon(closed ? 'chev-right' : 'chev-down', 'i sm')}${label}<span class="n">${meta || ''}</span></button>
    <div class="rows" style="display:${closed ? 'none' : ''}">${rows}</div></div>`;
}
function rowHtml(r) {
  const c = '#' + new THREE.Color(CLASS_COLOR[r.cls] ?? .5).getHexString();
  return `<div class="row ${state.sel?.id === r.id ? 'on' : ''}" data-id="${esc(r.id)}" data-kind="road">
    <i class="swatch" style="background:${c}"></i>
    <span class="nm">${esc(roadLabel(r))}</span>
    <span class="meta">${m2(r._len)} m</span>
    <button class="eye ${state.muted.has(r.id) ? 'off' : ''}" data-mute="${esc(r.id)}" title="Show / hide">${icon(state.muted.has(r.id) ? 'eye-off' : 'eye', 'i sm')}</button></div>`;
}
/* length cache used by the tree + panels */
function measureLengths() {
  for (const r of state.network.roads) r._len = roadLength(r);
}

/* ------------------------------- inspector widgets ------------------------------- */
function card(title, ic, body, actions = '') {
  const el = document.createElement('div');
  el.className = 'card';
  el.innerHTML = `<div class="card-head"><span><span class="ic">${icon(ic)}</span>${title}</span><span class="act">${actions}</span></div><div class="bd"></div>`;
  const bd = $('.bd', el) || el;
  (Array.isArray(body) ? body : [body]).forEach(n => n && (typeof n === 'string' ? bd.insertAdjacentHTML('beforeend', n) : bd.appendChild(n)));
  return el;
}
function slider({ label, min, max, step = 1, value, unit = '', fmt, onInput, commit }) {
  const l = document.createElement('label');
  l.className = 'ctl';
  const show = v => (fmt ? fmt(v) : (+v).toFixed(step < 1 ? (step < .1 ? 2 : 1) : 0)) + (unit ? ' ' + unit : '');
  l.innerHTML = `<span>${esc(label)}<b>${esc(show(value))}</b></span><input type="range" min="${min}" max="${max}" step="${step}" value="${value}">`;
  const inp = $('input', l), out = $('b', l);
  const paint = v => { l.style.setProperty('--p', ((v - min) / (max - min) * 100) + '%'); out.textContent = show(v); };
  paint(+value);
  inp.addEventListener('input', () => { paint(+inp.value); onInput && onInput(+inp.value, false); });
  inp.addEventListener('change', () => { paint(+inp.value); (commit || onInput) && (commit ? commit(+inp.value) : onInput(+inp.value, true)); });
  l.setValue = v => { inp.value = v; paint(+v); };
  return l;
}
function toggle({ label, on, onChange, hint, ic }) {
  const b = document.createElement('button');
  b.className = 'tog'; b.setAttribute('aria-pressed', !!on);
  b.innerHTML = `<span style="display:flex;align-items:center;gap:8px">${ic ? icon(ic, 'i sm') : ''}${esc(label)}</span><span class="switch"><i></i></span>`;
  if (hint) b.title = hint;
  b.addEventListener('click', () => { const v = b.getAttribute('aria-pressed') !== 'true'; b.setAttribute('aria-pressed', v); onChange(v); });
  b.setOn = v => b.setAttribute('aria-pressed', !!v);
  return b;
}
function segmented({ label, options, value, onChange }) {
  const w = document.createElement('label');
  w.className = 'ctl';
  w.innerHTML = `<span>${esc(label)}<b></b></span><div class="pills"></div>`;
  const p = $('.pills', w);
  for (const o of options) {
    const b = document.createElement('button');
    b.textContent = o.label ?? o;
    b.dataset.v = o.value ?? o;
    if ((o.value ?? o) === value) b.setAttribute('aria-pressed', 'true');
    b.addEventListener('click', () => { [...p.children].forEach(c => c.setAttribute('aria-pressed', c === b)); onChange(b.dataset.v); });
    p.appendChild(b);
  }
  $('b', w).textContent = (options.find(o => (o.value ?? o) === value) || {}).label ?? value ?? '';
  return w;
}
function dropdown({ label, options, value, onChange }) {
  const w = document.createElement('label');
  w.className = 'ctl';
  w.innerHTML = `<span>${esc(label)}<b></b></span><select></select>`;
  const s = $('select', w);
  for (const o of options) s.insertAdjacentHTML('beforeend', `<option value="${esc(o.value ?? o)}" ${(o.value ?? o) === value ? 'selected' : ''}>${esc(o.label ?? o)}</option>`);
  s.addEventListener('change', () => onChange(s.value));
  return w;
}
function btnRow(items) {
  const d = document.createElement('div');
  d.className = 'rowbtns';
  for (const b of items) {
    const el = document.createElement('button');
    el.className = 'btn sm ' + (b.cls || 'ghost');
    el.innerHTML = (b.ic ? icon(b.ic, 'i sm') : '') + esc(b.label);
    if (b.hint) el.title = b.hint;
    el.addEventListener('click', b.onClick);
    d.appendChild(el);
  }
  return d;
}
const metric = (v, u, cls = '') => `<div class="metric ${cls}">${v}${u ? `<small>${u}</small>` : ''}</div>`;
const kv = (k, v) => `<div class="kv"><span>${k}</span><b>${v}</b></div>`;

/* ------------------------------- cross-section drawing ------------------------------- */
const surfHex = surf => {
  const s = SURFACES[surf] || SURFACES.asphalt;
  const t = s.tint || [.4, .4, .4];
  return '#' + new THREE.Color(t[0], t[1], t[2]).getHexString();
};
function sectionSVG(profile, w = 306, opts = {}) {
  const compact = !!opts.compact;
  const h = compact ? 34 : 104;
  const s = solveSection(profile);
  const span = Math.max(4, s.total);
  const pad = compact ? 1.5 : 5;
  const sc = (w - pad * 2) / span;
  const yTop = compact ? 9 : 46;
  const bot = h - (compact ? 4 : 16);
  const camber = (profile.crown ?? 2) / 100;
  const el = (off) => s.elements.find(e => off >= e.x0 - 1e-6 && off <= e.x1 + 1e-6);
  const topY = off => {
    const e = el(off) || s.elements[0];
    const lvl = levelAt(s, profile, off);
    const drop = Math.abs(off - s.crownX) * camber * (e && e.crownSign ? 1 : 0);
    return yTop - lvl * (compact ? 1.2 : 5.2) + drop * sc * (compact ? .4 : 1);
  };
  let out = '';
  for (const e of s.elements) {
    const x0 = pad + (e.x0 - s.start) * sc, x1 = pad + (e.x1 - s.start) * sc;
    const t0 = topY(e.x0 + 1e-4), t1 = topY(e.x1 - 1e-4);
    const fill = surfHex(e.surf);
    out += `<path d="M${x0.toFixed(1)},${bot} L${x0.toFixed(1)},${t0.toFixed(1)} L${x1.toFixed(1)},${t1.toFixed(1)} L${x1.toFixed(1)},${bot} Z" fill="${fill}" ${compact ? '' : `stroke="#00000055" stroke-width=".5"`}/>`;
    if (e.k === 'curb' || e.k === 'barrier' || e.k === 'wall' || e.k === 'fence')
      out += `<path d="M${x0.toFixed(1)},${t0.toFixed(1)} L${x1.toFixed(1)},${t1.toFixed(1)}" stroke="#c9c9c6" stroke-width="${compact ? .8 : 1.2}" fill="none" opacity=".8"/>`;
    if (!compact && (e.k === 'lane' || e.k === 'bus' || e.k === 'cycle' || e.k === 'parking')) {
      const cx = (x0 + x1) / 2, yy = (t0 + t1) / 2;
      const col = e.k === 'bus' ? '#f4c26a' : e.k === 'cycle' ? '#7fce9f' : e.k === 'parking' ? '#9fb6d8' : '#d6d6d2';
      out += `<path d="M${cx},${yy - 5} l-3,5 M${cx},${yy - 5} l3,5 M${cx},${yy - 5} v7" stroke="${col}" stroke-width="1" fill="none" opacity=".75" stroke-linecap="round"/>`;
    }
    if (!compact && e.dirNum && e.k === 'lane') {
      const cx = (x0 + x1) / 2, yy = (t0 + t1) / 2 + 12;
      out += `<path d="M${cx},${yy + (e.dirNum > 0 ? 4 : -4)} l0,-8 M${cx},${yy + (e.dirNum > 0 ? -4 : 4)} l-2.6,${e.dirNum > 0 ? 2.6 : -2.6} M${cx},${yy + (e.dirNum > 0 ? -4 : 4)} l2.6,${e.dirNum > 0 ? 2.6 : -2.6}" stroke="#ffffff" stroke-width="1.1" opacity=".5" fill="none"/>`;
    }
  }
  if (!compact) {
    const cx = pad + (s.crownX - s.start) * sc;
    out += `<path d="M${cx},${yTop - 12} v${bot - yTop + 12}" stroke="#6e8cc0" stroke-width=".7" stroke-dasharray="3 3" opacity=".55"/>`;
    out += `<path d="M${pad},${bot + 7} H${w - pad}" stroke="#3a3a3a" stroke-width="1"/>
      <path d="M${pad},${bot + 4} v6 M${w - pad},${bot + 4} v6" stroke="#575757" stroke-width="1"/>
      <text x="${w / 2}" y="${bot + 15}" fill="#8b8b8b" font-size="8" text-anchor="middle" font-family="ui-monospace,monospace">corridor ${m2(s.total)} m</text>`;
    const nFwd = s.elements.filter(e => e.k === 'lane' && e.dirNum > 0).length, nRev = s.elements.filter(e => e.k === 'lane' && e.dirNum < 0).length;
    out += `<text x="${pad}" y="${yTop - 14}" fill="#8f8f8f" font-size="8.5" font-family="Inter,sans-serif">${s.twoWay ? `${nRev} + ${nFwd} lanes · two-way` : `${nFwd + nRev} lanes · one-way`}</text>`;
    out += `<text x="${w - pad}" y="${yTop - 14}" fill="#6f6f6f" font-size="8.5" text-anchor="end" font-family="ui-monospace,monospace">${profile.speed || '—'} km/h</text>`;
  }
  return `<svg viewBox="0 0 ${w} ${h}" preserveAspectRatio="none">${out}</svg>`;
}
function elevSVG(road, w = 306, h = 84) {
  const f = framesOf(road);
  if (f.length < 2) return '<div class="empty">no alignment</div>';
  const L = f[f.length - 1].s;
  const deck = road.deckY || 0;
  const ys = f.map(p => p.y + deck);
  let min = Math.min(...ys, 0), max = Math.max(...ys, 0);
  if (max - min < 2) { max += 1; min -= 1; }
  const px = i => 6 + (f[i].s / L) * (w - 12);
  const py = v => (h - 14) - ((v - min) / (max - min)) * (h - 26);
  let d = `M${px(0)},${py(ys[0])}`;
  for (let i = 1; i < f.length; i++) d += ` L${px(i).toFixed(1)},${py(ys[i]).toFixed(1)}`;
  const zero = py(0);
  const tick = (sx, col, hh, y0 = 6) => `<path d="M${(6 + (sx / L) * (w - 12)).toFixed(1)},${y0} v${hh}" stroke="${col}" stroke-width="1" opacity=".55"/>`;
  const marks = (road.bumps || []).map(b => tick(b.s, '#e8b65f', h - 20)).join('');
  const cross = (road.crossingsManual || []).map(c => tick(c.s, '#8fb6ff', h - 20)).join('');
  const drv = (road.driveways || []).map(c => tick(c.s, '#7fce9f', 7, h - 13)).join('') + (road.stations || []).map(c => tick(c.s, '#c8a8f0', h - 20)).join('');
  return `<svg viewBox="0 0 ${w} ${h}">
    <path d="M6,${zero.toFixed(1)} H${w - 6}" stroke="#3a3a3a" stroke-width=".8" stroke-dasharray="4 3"/>
    <path d="${d} L${px(f.length - 1)},${h - 6} L${px(0)},${h - 6} Z" fill="#8fb6ff" opacity=".07"/>
    <path d="${d}" fill="none" stroke="#a8c4f0" stroke-width="1.4"/>
    ${marks}${cross}${drv}
    <text x="6" y="9" fill="#6f6f6f" font-size="7.5" font-family="ui-monospace,monospace">+${m2(max)} m</text>
    <text x="6" y="${h - 2}" fill="#6f6f6f" font-size="7.5" font-family="ui-monospace,monospace">${m2(min)} m · ${m2(L)} m run</text>
  </svg>`;
}

/* ------------------------------- details panel ------------------------------- */
function selectedArea() {
  if (state.sel?.kind !== 'area') return null;
  const id = state.sel.id;
  return (state.network.areas || []).find(a => a.id === id) || (state.network.water || []).find(w => w.id === id) || null;
}
function renderDetails() {
  const body = $('#detBody');
  body.innerHTML = '';
  const road = selectedRoad(), node = selectedNode(), area = selectedArea();
  if (road) { renderRoadDetails(body, road); return; }
  if (node) { renderNodeDetails(body, node); return; }
  if (area) { renderAreaDetails(body, area); return; }
  renderSceneSummary(body);
}
function setHead(kind, title, sub, ic, color) {
  $('#detKind').textContent = kind;
  $('#detTitle').innerHTML = title;
  $('#detSub').innerHTML = sub;
  const box = $('#detIcon');
  box.innerHTML = icon(ic, 'i lg');
  box.style.color = color || 'var(--accent)';
}
function editableName(el, onCommit) {
  el.contentEditable = 'true';
  el.spellcheck = false;
  el.addEventListener('keydown', e => { if (e.key === 'Enter') { e.preventDefault(); el.blur(); } e.stopPropagation(); });
  el.addEventListener('blur', () => onCommit(el.textContent.trim()));
}
function renderSceneSummary(body) {
  setHead('Scene summary', 'Nothing selected', '<span>Pick a road or junction · <kbd style="font:500 9.5px var(--mono)">A</kbd> to draw · <kbd style="font:500 9.5px var(--mono)">⇧A</kbd> construct</span>', 'layers');
  const n = state.network;
  const total = n.roads.reduce((s, r) => s + (r._len || 0), 0);
  const byCls = {};
  for (const r of n.roads) byCls[r.cls] = (byCls[r.cls] || 0) + 1;
  body.insertAdjacentHTML('beforeend', `<div class="card">
    <div class="eyebrow" style="margin-bottom:10px">Network</div>
    ${metric(km(total), 'km lane-free corridor')}
    <div class="grid3" style="margin-top:16px">
      <div><div class="metric sm">${n.roads.length}<small>roads</small></div><p class="muted">corridors</p></div>
      <div><div class="metric sm">${n.nodes.length}<small>nodes</small></div><p class="muted">junctions</p></div>
      <div><div class="metric sm">${scene.stats?.lanes || 0}<small>lanes</small></div><p class="muted">${(scene.stats?.instances || 0).toLocaleString()} instanced props</p></div>
    </div>
  </div>`);
  body.insertAdjacentHTML('beforeend', `<div class="card"><div class="card-head"><span>${icon('activity')}Composition</span><span class="act"><span class="chip">${(scene.stats?.tris || 0).toLocaleString()} tris</span></span></div>
    <div class="list">${Object.entries(byCls).sort((a, b) => b[1] - a[1]).map(([c, k]) => {
      const len = n.roads.filter(r => r.cls === c).reduce((s, r) => s + r._len, 0);
      return `<div class="li"><i style="width:7px;height:7px;border-radius:2px;background:#${new THREE.Color(CLASS_COLOR[c] ?? .5).getHexString()}"></i>
      <b>${clsLabel(c)}</b><span style="color:#6f6f6f">${k} · ${m2(len)} m</span>
      <button class="x" data-cls="${esc(c)}" title="Select all of this class">${icon('cursor', 'i sm')}</button></div>`;
    }).join('')}</div></div>`);
  const b = document.createElement('div');
  b.className = 'card';
  b.innerHTML = `<div class="card-head"><span>${icon('sliders')}Quick actions</span></div>`;
  b.appendChild(btnRow([
    { label: 'Frame network', ic: 'target', onClick: () => scene.frameAll() },
    { label: 'Plan view', ic: 'map', onClick: () => planView() },
    { label: 'Draw a road', ic: 'draw', cls: 'primary', onClick: () => setTool('draw') },
    { label: 'Night', ic: 'moon', onClick: () => setHour(21.5) },
    { label: 'Noon', ic: 'sun', onClick: () => setHour(13) },
  ]));
  body.appendChild(b);
  if (state.network.meta?.notes?.length) body.insertAdjacentHTML('beforeend',
    `<div class="gen-note">${icon('info', 'i sm')}<div>${state.network.meta.notes.map(esc).join(' · ')}</div></div>`);
  body.querySelectorAll('[data-cls]').forEach(x => x.addEventListener('click', () => {
    const c = x.dataset.cls;
    state.filter = ''; $('#treeSearch').value = '';
    toast(`Filter set to ${clsLabel(c)} in the outliner`);
  }));
}
function renderRoadDetails(body, road) {
  const prof = prepareRoad(road);
  const sec = road._section;
  const L = road._len || roadLength(road);
  const col = '#' + new THREE.Color(CLASS_COLOR[road.cls] ?? .5).getHexString();
  setHead('Road corridor', esc(roadLabel(road)), `<span class="chip" style="border-color:${col}55;color:${col}">${clsLabel(road.cls)}</span>
    <span class="chip">${esc(prof.id)}</span><span>${m2(L)} m · ${prof.label}</span>`, clsIcon(road.cls), col);
  const nameEl = $('#detTitle');
  editableName(nameEl, v => { if (v && v !== road.name) edit('name', () => { road.name = v; }); });

  /* headline metrics + section */
  const lanes = sec.elements.filter(e => e.k === 'lane').length;
  const cyc = sec.elements.filter(e => e.k === 'cycle').length, busl = sec.elements.filter(e => e.k === 'bus').length;
  const park = sec.elements.filter(e => e.k === 'parking').length;
  const c = document.createElement('div');
  c.className = 'card';
  c.innerHTML = `<div class="card-head"><span>${icon('box')}Assembly</span>
      <span class="act"><button class="btn sm ghost" data-tabjump="construct">${icon('sliders', 'i sm')}Swap profile</button></span></div>
    <div class="section-view">${sectionSVG(prof, 306)}<span class="cap">${m2(sec.total)} m corridor · crown ${prof.crown ?? 2}%</span></div>
    <div class="grid3" style="margin-top:14px">
      <div>${metric(lanes, 'lanes', 'sm')}<p class="muted">${sec.twoWay ? 'two-way' : 'one-way'}${road.oneWay ? ' · edited' : ''}</p></div>
      <div>${metric(m2(sec.total), 'm', 'sm')}<p class="muted">kerb to kerb</p></div>
      <div>${metric(prof.speed || '—', 'km/h', 'sm')}<p class="muted">design speed</p></div>
    </div>
    <div class="grid3" style="margin-top:12px">
      ${cyc ? `<div>${metric(cyc, '', 'sm')}<p class="muted">cycle lanes</p></div>` : ''}
      ${busl ? `<div>${metric(busl, '', 'sm')}<p class="muted">bus lanes</p></div>` : ''}
      ${park ? `<div>${metric(park, '', 'sm')}<p class="muted">parking bays</p></div>` : ''}
      ${!cyc && !busl && !park ? `<div class="muted" style="grid-column:1/-1">No priority or parking provision in this assembly.</div>` : ''}
    </div>`;
  body.appendChild(c);
  $('[data-tabjump]', c)?.addEventListener('click', () => openConstruct(road));

  /* lane count / geometry */
  const g = document.createElement('div');
  g.className = 'card';
  g.innerHTML = `<div class="card-head"><span>${icon('node')}Geometry</span>${prof._base || typeof road.profile === 'object' ? `<button class="btn sm ghost" data-reset>${icon('undo', 'i sm')}Reset edits</button>` : ''}</div>`;
  const bd = $('.bd', g) || g;
  const addLanes = d => edit('lanes', () => {
    const p = mutableProfile(road);
    const laneEls = p.elements.filter(e => e.k === 'lane');
    if (d > 0) {
      const proto = laneEls[laneEls.length - 1] || { w: 3.4, surf: 'asphalt' };
      const rev = laneEls[0] || proto;
      p.elements.splice(p.elements.indexOf(proto) + 1, 0, { k: 'lane', w: proto.w ?? 3.4, surf: proto.surf, dir: 'fwd' });
      p.elements.splice(p.elements.indexOf(rev), 0, { k: 'lane', w: rev.w ?? 3.4, surf: rev.surf, dir: 'rev' });
    } else if (laneEls.length > 1) {
      for (const e of [laneEls[laneEls.length - 1], laneEls[0]]) if (e && (laneEls.length - p.elements.filter(x => x.k === 'lane').length) >= 0) p.elements.splice(p.elements.indexOf(e), 1);
      while (p.elements.filter(e => e.k === 'lane').length > Math.max(1, laneEls.length + 2 * d)) {
        const arr = p.elements.filter(e => e.k === 'lane');
        p.elements.splice(p.elements.indexOf(arr[arr.length - 1]), 1);
        p.elements.splice(p.elements.indexOf(arr[0]), 1);
      }
    }
  });
  bd.insertAdjacentHTML('beforeend', `<div class="rowbtns" style="margin-bottom:4px">
      <button class="btn sm ghost" data-lm>${icon('minus', 'i sm')}−1 lane each way</button>
      <button class="btn sm ghost" data-lp>${icon('plus', 'i sm')}+1 lane each way</button></div>`);
  $('[data-lp]', bd).addEventListener('click', () => addLanes(1));
  $('[data-lm]', bd).addEventListener('click', () => addLanes(-1));
  bd.appendChild(slider({ label: 'Lane width', min: 2.7, max: 4.2, step: .05, value: (prof.elements.find(e => e.k === 'lane') || {}).w || 3.4, unit: 'm', onInput: (v, done) => editSet(road, done, p => { p.elements.forEach(e => { if (e.k === 'lane') e.w = v; }); }, 'lane width') }));
  bd.appendChild(slider({ label: 'Speed limit', min: 10, max: 130, step: 5, value: prof.speed || 50, unit: 'km/h', onInput: (v, done) => editSet(road, done, p => { p.speed = v; }, 'speed') }));
  bd.appendChild(slider({ label: 'Crossfall', min: 0, max: 8, step: .25, value: prof.crown ?? 2, unit: '%', onInput: (v, done) => editSet(road, done, p => { p.crown = v; }, 'crown') }));
  bd.appendChild(slider({ label: 'Kerb height', min: 0, max: .3, step: .01, value: (prof.elements.find(e => e.k === 'curb') || {}).h ?? .15, unit: 'm', onInput: (v, done) => editSet(road, done, p => { p.elements.forEach(e => { if (e.k === 'curb' || e.k === 'gutter') e.h = v; }); }, 'kerb') }));
  const swEl = prof.elements.find(e => e.k === 'sidewalk');
  if (swEl) bd.appendChild(slider({ label: 'Footway width', min: 1, max: 8, step: .1, value: swEl.w, unit: 'm', onInput: (v, done) => editSet(road, done, p => { p.elements.forEach(e => { if (e.k === 'sidewalk') e.w = v; }); }, 'footway') }));
  const medEl = prof.elements.find(e => e.k === 'median');
  if (medEl) bd.appendChild(slider({ label: 'Median width', min: .8, max: 12, step: .1, value: medEl.w, unit: 'm', onInput: (v, done) => editSet(road, done, p => { p.elements.forEach(e => { if (e.k === 'median') e.w = v; }); }, 'median') }));
  bd.appendChild(segmented({ label: 'One-way', options: [{ label: 'As designed', value: 'auto' }, { label: 'Forward', value: 'fwd' }, { label: 'Reverse', value: 'rev' }], value: road.oneWay ? (road.oneWay === 'rev' ? 'rev' : 'fwd') : 'auto', onChange: v => edit('oneway', () => { if (v === 'auto') { delete road.oneWay; } else { road.oneWay = v; const p = mutableProfile(road); p.elements.forEach(e => { if (['lane', 'bus', 'cycle', 'turn', 'parking', 'loading'].includes(e.k)) { e.dir = v === 'rev' ? 'rev' : 'fwd'; e.h = e.h; } }); } }) }));
  bd.appendChild(segmented({ label: 'Anchor', options: ['centre', 'left', 'right'].map(v => ({ label: v, value: v })), value: prof.anchor || 'centre', onChange: v => editSet(road, true, p => { p.anchor = v === 'centre' ? undefined : v; }, 'anchor') }));
  body.appendChild(g);
  $('[data-reset]', g)?.addEventListener('click', () => edit('reset profile', () => { road.profile = prof._base || prof.id; delete road.oneWay; }));

  /* surfaces */
  const s1 = document.createElement('div');
  s1.className = 'card';
  s1.innerHTML = `<div class="card-head"><span>${icon('box')}Surfaces</span><span class="act"><span class="chip">${esc((sec.elements.find(e => e.k === 'lane') || {}).surf || 'asphalt')}</span></span></div>`;
  s1.appendChild(swatches(SURFACE_CHOICES, (sec.elements.find(e => e.k === 'lane') || {}).surf || 'asphalt', v => editSet(road, true, p => { p.elements.forEach(e => { if (['lane', 'turn', 'bus', 'parking', 'shoulder', 'gutter'].includes(e.k)) e.surf = v; }); }, 'carriageway surf')));
  s1.insertAdjacentHTML('beforeend', `<p class="muted">Carriageway · applied to every traveled lane.</p>`);
  s1.appendChild(swatches(SURFACE_CHOICES, (sec.elements.find(e => e.k === 'sidewalk' || e.k === 'plaza') || {}).surf || 'paver', v => editSet(road, true, p => { p.elements.forEach(e => { if (['sidewalk', 'plaza', 'verge', 'path'].includes(e.k)) e.surf = v; }); }, 'footway surf')));
  s1.insertAdjacentHTML('beforeend', `<p class="muted">Paving · footways, shared surfaces and verges.</p>`);
  body.appendChild(s1);

  /* markings */
  const m = document.createElement('div');
  m.className = 'card';
  m.innerHTML = `<div class="card-head"><span>${icon('flag')}Markings</span><span class="act"><button class="btn sm ghost" data-mkall>${icon('check', 'i sm')}Standard</button></span></div>`;
  m.appendChild(segmented({ label: 'Centre line', options: [{ label: 'None', value: 'none' }, { label: 'Broken white', value: 'broken' }, { label: 'Double yellow', value: 'double-yellow' }, { label: 'Double white', value: 'double' }], value: prof.markings?.center || 'none', onChange: v => editSet(road, true, p => { p.markings.center = v; }, 'centre') }));
  m.appendChild(segmented({ label: 'Lane line', options: [{ label: 'None', value: 'none' }, { label: 'Broken', value: 'broken' }, { label: 'Solid', value: 'solid' }], value: prof.markings?.lane || 'broken', onChange: v => editSet(road, true, p => { p.markings.lane = v; }, 'lane') }));
  m.appendChild(segmented({ label: 'Edge / gutter line', options: [{ label: 'None', value: 'none' }, { label: 'Solid white', value: 'solid' }, { label: 'Broken', value: 'broken' }], value: prof.markings?.edge || 'solid', onChange: v => editSet(road, true, p => { p.markings.edge = v; }, 'edge') }));
  m.appendChild(segmented({ label: 'Crosswalks', options: [{ label: 'None', value: 'none' }, { label: 'Zebra', value: 'zebra' }, { label: 'Continental', value: 'continental' }, { label: 'Ladder', value: 'ladder' }], value: prof.markings?.crosswalk || 'none', onChange: v => editSet(road, true, p => { p.markings.crosswalk = v; }, 'crosswalk') }));
  const tg = document.createElement('div'); tg.className = 'tog-grid';
  const mkTogs = [
    ['arrows', 'Direction arrows'], ['words', 'Word marks (SLOW / BUS)'], ['cycleSymbols', 'Cycle symbols'],
    ['busLane', 'Bus lane word'], ['parkingStalls', 'Parking stalls'], ['rumble', 'Rumble strips'],
    ['tactile', 'Tactile paving'], ['giveWay', 'Give-way teeth'], ['chevrons', 'Chevrons'], ['hatch', 'Hatching'],
    ['manholes', 'Drainage & manholes'], ['redRoute', 'Red route (double red)']
  ];
  for (const [k, label] of mkTogs) {
    tg.appendChild(toggle({ label, on: prof.markings?.[k] === undefined ? ['arrows', 'words', 'cycleSymbols', 'busLane', 'parkingStalls', 'rumble', 'tactile'].includes(k) : prof.markings?.[k] !== false, onChange: v => editSet(road, true, p => { p.markings[k] = v; }, 'marking ' + k) }));
  }
  m.appendChild(tg);
  body.appendChild(m);
  $('[data-mkall]', m).addEventListener('click', () => edit('standard markings', () => { const p = mutableProfile(road); p.markings = { center: p.center || 'double', lane: 'broken', edge: 'solid', crosswalk: 'zebra', arrows: true, words: true, tactile: true, rumble: false, giveWay: true, manholes: true }; }));

  /* furniture */
  const f = document.createElement('div');
  f.className = 'card';
  f.innerHTML = `<div class="card-head"><span>${icon('lamp')}Street furniture</span><span class="act"><button class="btn sm ghost" data-noprops>${icon('x', 'i sm')}Clear</button></span></div>`;
  f.appendChild(segmented({ label: 'Lighting', options: [{ label: 'None', value: 'none' }, ...Object.entries(LAMPS).map(([k, v]) => ({ label: v, value: k }))], value: prof.props?.lights?.type || 'none', onChange: v => editSet(road, true, p => { if (v === 'none') delete p.props.lights; else (p.props ||= {}).lights = { ...(p.props?.lights || {}), type: v }; }, 'lights') }));
  if (prof.props?.lights) f.appendChild(slider({ label: 'Lamp spacing', min: 18, max: 90, step: 1, value: prof.props.lights.spacing || 32, unit: 'm', onInput: (v, done) => editSet(road, done, p => { p.props.lights.spacing = v; }, 'lamp spacing') }));
  f.appendChild(segmented({ label: 'Trees', options: [{ label: 'None', value: 'none' }, { label: 'Plane', value: 'plane' }, { label: 'Palm', value: 'palm' }, { label: 'Cherry', value: 'cherry' }, { label: 'Oak', value: 'oak' }, { label: 'Olive', value: 'olive' }], value: prof.props?.trees?.species || 'none', onChange: v => editSet(road, true, p => { if (v === 'none') delete p.props.trees; else (p.props ||= {}).trees = { spacing: p.props?.trees?.spacing ?? 9, side: p.props?.trees?.side ?? 'verge', species: v }; }, 'trees') }));
  if (prof.props?.trees) {
    f.appendChild(slider({ label: 'Tree spacing', min: 5, max: 24, step: .5, value: prof.props.trees.spacing || 9, unit: 'm', onInput: (v, done) => editSet(road, done, p => { p.props.trees.spacing = v; }, 'tree spacing') }));
    f.appendChild(segmented({ label: 'Tree position', options: ['verge', 'both', 'median'].map(v => ({ label: v, value: v })), value: prof.props.trees.side || 'verge', onChange: v => editSet(road, true, p => { p.props.trees.side = v; }, 'tree side') }));
  }
  const ft = document.createElement('div'); ft.className = 'tog-grid';
  for (const [k, label] of [['bollards', 'Bollards'], ['benches', 'Benches'], ['bins', 'Bins'], ['planters', 'Planters'], ['meters', 'Parking meters'], ['hydrants', 'Hydrants'], ['drains', 'Gullies'], ['signs', 'Signage'], ['busStops', 'Bus stops'], ['catenary', 'Tram wires']]) {
    ft.appendChild(toggle({ label, on: !!prof.props?.[k], onChange: v => editSet(road, true, p => { const pp = (p.props ||= {}); if (v) pp[k] = k === 'busStops' ? { spacing: 480 } : k === 'drains' ? { spacing: 40 } : true; else delete pp[k]; }, 'prop ' + k) }));
  }
  f.appendChild(ft);
  f.appendChild(segmented({ label: 'Guard rail', options: [{ label: 'None', value: 'none' }, { label: 'Beam', value: 'beam' }, { label: 'Wire rope', value: 'wire' }, { label: 'Stone', value: 'stone' }, { label: 'Timber', value: 'timber' }], value: prof.props?.guards?.type || 'none', onChange: v => editSet(road, true, p => { if (v === 'none') delete p.props.guards; else (p.props ||= {}).guards = { type: v, side: p.props?.guards?.side || 'both' }; }, 'guards') }));
  body.appendChild(f);
  $('[data-noprops]', f).addEventListener('click', () => editSet(road, true, p => { p.props = {}; }, 'cleared furniture'));

  /* elevation + structure */
  const e2 = document.createElement('div');
  e2.className = 'card';
  e2.innerHTML = `<div class="card-head"><span>${icon('activity')}Elevation &amp; structure</span><span class="act"><span class="chip">${(road.bumps || []).length + (road.crosstables || []).length} features</span></span></div>
    <div class="elev-view">${elevSVG(road)}</div>`;
  e2.appendChild(slider({ label: 'Deck / ground offset', min: -18, max: 26, step: .25, value: road.deckY || 0, unit: 'm', onInput: (v, done) => edit('deck', () => { road.deckY = v; }) }));
  e2.appendChild(slider({ label: 'Longitudinal grade at ends', min: -12, max: 12, step: .5, value: (road.points?.[0]?.y || 0) * 0 + (road.points?.[0]?.y || 0), unit: 'm', fmt: v => v.toFixed(2) + ' m rise', onInput: (v, done) => edit('grade', () => { if (!road.points?.length) return; road.points[road.points.length - 1].y = v; }) }));
  e2.appendChild(segmented({ label: 'Structure', options: [{ label: 'At grade', value: 'none' }, { label: 'Bridge', value: 'bridge' }, { label: 'Viaduct', value: 'viaduct' }, { label: 'Tunnel', value: 'tunnel' }, { label: 'Underpass', value: 'underpass' }, { label: 'Embankment', value: 'embankment' }, { label: 'Retaining wall', value: 'retaining' }], value: road.structure || prof.structure || 'none', onChange: v => edit('structure', () => { if (v === 'none') { delete road.structure; } else road.structure = v; }) }));
  body.appendChild(e2);

  /* features / calming */
  const fc = document.createElement('div');
  fc.className = 'card';
  fc.innerHTML = `<div class="card-head"><span>${icon('cone')}Calming, crossings &amp; access</span><span class="act"><button class="btn sm" data-paint>${icon('sliders', 'i sm')}Paint mode</button></span></div>`;
  const feats = [];
  for (const b of (road.bumps || [])) feats.push({ t: b.kind === 'table' ? 'Speed table' : b.kind === 'flat' ? 'Flat-topped table' : 'Speed cushions', s: b.s, arr: 'bumps', ref: b });
  for (const c of (road.crosstables || [])) feats.push({ t: 'Raised crossing table', s: c.s, arr: 'crosstables', ref: c });
  for (const x of (road.crossingsManual || [])) feats.push({ t: (x.style || 'zebra') + ' crossing', s: x.s, arr: 'crossingsManual', ref: x });
  for (const d of (road.driveways || [])) feats.push({ t: 'Dropped driveway · ' + (d.side < 0 ? 'left' : 'right'), s: d.s, arr: 'driveways', ref: d });
  for (const s of (road.stations || [])) feats.push({ t: 'Metro station · ' + (s.name || ''), s: s.s, arr: 'stations', ref: s });
  fc.insertAdjacentHTML('beforeend', feats.length
    ? `<div class="list">${feats.map((f, i) => `<div class="li">${icon(f.arr === 'bumps' ? 'cone' : f.arr === 'driveways' ? 'car' : f.arr === 'stations' ? 'train' : 'flag', 'i sm')}<b>${esc(f.t)}</b><span style="color:#6f6f6f">s ${m2(f.s)} m</span><button class="x" data-fi="${i}">${icon('x', 'i sm')}</button></div>`).join('')}</div>`
    : `<div class="empty" style="padding:14px">No local features on this corridor.</div>`);
  fc._feats = feats;
  body.appendChild(fc);
  $('[data-paint]', fc).addEventListener('click', () => setTool('paint'));
  fc.querySelectorAll('[data-fi]').forEach(b => b.addEventListener('click', () => {
    const f = fc._feats[+b.dataset.fi];
    edit('remove feature', () => { const arr = road[f.arr] || []; const i = arr.indexOf(f.ref); if (i >= 0) arr.splice(i, 1); });
  }));
  const addRow = btnRow([
    { label: 'Cushion', ic: 'cone', onClick: () => addFeature(road, 'bumps', { s: midS(road), kind: 'cushion' }) },
    { label: 'Speed table', ic: 'cone', onClick: () => addFeature(road, 'bumps', { s: midS(road), kind: 'table' }) },
    { label: 'Zebra', ic: 'flag', onClick: () => addFeature(road, 'crossingsManual', { s: midS(road), style: 'zebra' }) },
    { label: 'Driveway L/R', ic: 'car', onClick: () => { addFeature(road, 'driveways', { s: midS(road), side: -1 }); addFeature(road, 'driveways', { s: midS(road), side: 1 }, true); } },
  ]);
  fc.appendChild(addRow);

  /* ends / junctions */
  const ends = state.network.nodes.filter(n => n.arms.some(a => a.road === road.id));
  if (ends.length) {
    const j = document.createElement('div');
    j.className = 'card';
    j.innerHTML = `<div class="card-head"><span>${icon('crosshair')}End junctions</span></div>`;
    for (const nd of ends) {
      const w = document.createElement('div');
      w.innerHTML = `<div class="kv"><span>${esc(nd.name || nd.id)}</span><b>${nd.arms.length} arms</b></div>`;
      w.appendChild(segmented({ label: 'Control', options: ['uncontrolled', 'give-way', 'signals', 'roundabout'].map(v => ({ label: v.split('-').map(x => x[0].toUpperCase() + x.slice(1)).join(' '), value: v })), value: nd.style || 'uncontrolled', onChange: v => edit('junction style', () => { nd.style = v; }) }));
      j.appendChild(w);
    }
    body.appendChild(j);
  }
  /* danger row */
  body.appendChild(btnRow([
    { label: 'Duplicate', ic: 'copy', onClick: () => dupRoad(road) },
    { label: 'Split at mid', ic: 'scissors', onClick: () => splitRoad(road) },
    { label: 'Frame', ic: 'target', onClick: () => scene.frameSelection() },
    { label: 'Drive this road', ic: 'play', onClick: () => driveRoad(road) },
    { label: 'Delete', ic: 'trash', cls: 'danger', onClick: () => { pushUndo(); state.network.roads = state.network.roads.filter(r => r !== road); state.sel = null; scene.setSelection(null); markDirty(); build(); } },
  ]));
}
const midS = road => { const f = framesOf(road); return f.length ? f[f.length - 1].s / 2 : 0; };
function addFeature(road, arr, obj, quiet) {
  edit('add feature', () => { (road[arr] ||= []).push(obj); road[arr].sort((a, b) => a.s - b.s); }, );
}
function renderNodeDetails(body, node) {
  const col = '#b6c8e8';
  setHead('Junction', esc(node.name || node.id), `<span class="chip">${node.arms.length} arms</span><span>${node.style || 'uncontrolled'} · r ${m2(node.radius || 0)} m</span>`, 'crosshair', col);
  const c = document.createElement('div');
  c.className = 'card';
  c.innerHTML = `<div class="card-head"><span>${icon('activity')}Control</span></div>`;
  c.appendChild(segmented({ label: 'Type', options: ['uncontrolled', 'give-way', 'signals', 'roundabout'].map(v => ({ label: clsLabel(v), value: v })), value: node.style || 'uncontrolled', onChange: v => edit('junction type', () => { node.style = v; }) }));
  c.appendChild(slider({ label: 'Radius', min: 6, max: 40, step: .5, value: node.radius || 14, unit: 'm', onInput: (v, done) => edit('radius', () => { node.radius = v; }) }));
  c.appendChild(slider({ label: 'CornerRadius', min: 2, max: 24, step: .5, value: node.corner ?? 9, unit: 'm', onInput: (v, done) => edit('corner', () => { node.corner = v; }) }));
  const tg = document.createElement('div'); tg.className = 'tog-grid';
  tg.appendChild(toggle({ label: 'Pedestrian scramble', on: !!node.scramble, onChange: v => edit('scramble', () => { node.scramble = v; }) }));
  tg.appendChild(toggle({ label: 'Signals (red amber)', on: node.signals !== false, onChange: v => edit('signals', () => { node.signals = v; }) }));
  tg.appendChild(toggle({ label: 'Central island', on: !!node.island, onChange: v => edit('island', () => { node.island = v; }) }));
  c.appendChild(tg);
  body.appendChild(c);
  const arms = document.createElement('div');
  arms.className = 'card';
  arms.innerHTML = `<div class="card-head"><span>${icon('route')}Arms</span><span class="act"><span class="chip">${node.arms.length}</span></span></div>
    <div class="list">${node.arms.map((a, i) => {
      const r = state.network.roads.find(x => x.id === a.road);
      return `<div class="li" data-arm="${i}">${icon('route', 'i sm')}<b>${esc(r ? roadLabel(r) : a.road)}</b><span style="color:#6f6f6f">${a.end === 's' ? 'start' : 'end'} · ${m2(r?._len || 0)} m</span><button class="x" data-unarm="${i}">${icon('x', 'i sm')}</button></div>`;
    }).join('')}</div>`;
  arms.querySelectorAll('[data-unarm]').forEach(b => b.addEventListener('click', () => edit('unwire arm', () => { node.arms.splice(+b.dataset.unarm, 1); })));
  arms.querySelectorAll('[data-arm]').forEach(b => b.addEventListener('click', e => { if (e.target.closest('.x')) return; selectById(node.arms[+b.dataset.arm].road, { frame: true }); }));
  body.appendChild(arms);
  body.appendChild(btnRow([
    { label: 'Frame', ic: 'target', onClick: () => scene.focusOn(node.x, node.z, node.y || 0, 110, 55) },
    { label: 'Delete junction', ic: 'trash', cls: 'danger', onClick: () => { pushUndo(); state.network.nodes = state.network.nodes.filter(n => n !== node); state.network.roads.forEach(r => { r.nodeIds = (r.nodeIds || []).filter(i => i !== node.id); }); state.sel = null; markDirty(); build(); } },
  ]));
}
const clsIcon = c => ({ freeway: 'route', motorway: 'route', expressway: 'route', ramp: 'route', arterial: 'route', boulevard: 'route', collector: 'car', local: 'car', residential: 'car', shared: 'footprints', service: 'car', industrial: 'barrier', cycle: 'bike', path: 'footprints', pedestrian: 'footprints', plaza: 'building', park: 'tree', 'parking-lot': 'car', parking: 'car', bus: 'bus', busway: 'bus', rail: 'train', tram: 'train', subway: 'train', tunnel: 'tunnel', bridge: 'bridge', viaduct: 'bridge', underpass: 'tunnel', 'cul-de-sac': 'target', culdesac: 'target', roundabout: 'orbit', rural: 'flag', gravel: 'water' }[c] || 'box');
const LAMPS = { cobra: 'Cobra head', modern: 'Modern flat', heritage: 'Heritage lantern', twin: 'Twin (median)', cycle: 'Cycle-lane column', bollard: 'Bollard uplight', sphere: 'Globe', tunnel: 'Tunnel strip', bridge: 'Bridge deck', wall: 'Wall bracket' };
const SURFACE_CHOICES = [
  { v: 'asphalt', label: 'Asphalt' }, { v: 'asphalt-new', label: 'New asphalt' }, { v: 'asphalt-dark', label: 'Dark asphalt' },
  { v: 'concrete', label: 'Concrete' }, { v: 'concrete-raw', label: 'In-situ concrete' }, { v: 'cobble', label: 'Setts' },
  { v: 'granite', label: 'Granite' }, { v: 'brick', label: 'Brick' }, { v: 'paver', label: 'Pavers' },
  { v: 'paver-herring', label: 'Herringbone' }, { v: 'gravel', label: 'Gravel' }, { v: 'grass', label: 'Grass' },
  { v: 'grass-mown', label: 'Mown verge' }, { v: 'timber', label: 'Timber' }, { v: 'tile-white', label: 'Tile' },
  { v: 'asphalt-red', label: 'Red asphalt' }, { v: 'asphalt-green', label: 'Green asphalt' }, { v: 'metal', label: 'Steel' }, { v: 'water', label: 'Water' }
];
function swatches(choices, value, onPick) {
  const d = document.createElement('div');
  d.className = 'swatches';
  for (const c of choices) {
    const s = SURFACES[c.v];
    const b = document.createElement('button');
    b.className = 'sw';
    b.dataset.v = c.v;
    const t = s?.tint || [.4, .4, .4];
    const hex = new THREE.Color(t[0], t[1], t[2]);
    b.style.background = `linear-gradient(135deg,#${hex.clone().offsetHSL(0, .06, .06).getHexString()},#${hex.clone().offsetHSL(0, 0, -.12).getHexString()})`;
    b.title = c.label + (s ? ` · ${s.label}` : '');
    if (c.v === value) b.setAttribute('aria-pressed', 'true');
    b.addEventListener('click', () => { [...d.children].forEach(x => x.setAttribute('aria-pressed', x === b)); onPick(c.v); });
    d.appendChild(b);
  }
  return d;
}
/** per-road profile overrides: clone the shared profile onto the road, then mutate */
function mutableProfile(road) {
  if (typeof road.profile === 'string') road.profile = { ...profileById(road.profile), _base: road.profile, elements: profileById(road.profile).elements.map(e => ({ ...e })), markings: { ...profileById(road.profile).markings }, props: JSON.parse(JSON.stringify(profileById(road.profile).props || {})) };
  else if (!road.profile._base) road.profile._base = road.profile.id;
  if (!road.profile.markings) road.profile.markings = {};
  if (!road.profile.props) road.profile.props = {};
  return road.profile;
}
function editSet(road, done, fn, label) {
  if (!state._editing) { pushUndo(); state._editing = true; }
  fn(mutableProfile(road));
  if (done) state._editing = false;
  else clearTimeout(state._t), state._t = setTimeout(() => { state._editing = false; }, 700);
  markDirty();
  build({ immediate: !!done });
}

/* ------------------------------- construct tab ------------------------------- */
function openConstruct(road) {
  setTab('construct');
  state.constructTarget = road ? road.id : (state.sel?.kind === 'road' ? state.sel.id : null);
  renderConstruct();
}
function renderConstruct() {
  const body = $('#detBody');
  body.innerHTML = '';
  const target = state.constructTarget ? state.network.roads.find(r => r.id === state.constructTarget) : null;
  if (target) prepareRoad(target);
  body.insertAdjacentHTML('beforeend', `<div class="gen-note">${icon('info', 'i sm')}<div>${target
    ? `Applying a profile re-writes <b style="color:#dcdcdc;font-weight:500">${esc(roadLabel(target))}</b>. Per-road edits are kept in the scene file.`
    : 'Choose the profile the <b style="color:#dcdcdc;font-weight:500">draw tool</b> will lay down, or select a road first to re-profile it.'}</div></div>`);
  const search = document.createElement('div');
  search.className = 'search'; search.style.margin = '10px 0';
  search.innerHTML = `${icon('search', 'i sm')}<input placeholder="Search 43 assemblies — boulevard, bus, cobble, tram…"><kbd>⇧A</kbd>`;
  const inp = $('input', search);
  body.appendChild(search);
  const list = document.createElement('div');
  list.className = 'lib-grid';
  body.appendChild(list);
  const paintList = () => {
    const q = inp.value.trim().toLowerCase();
    list.innerHTML = '';
    const groups = {};
    for (const p of PROFILES) {
      const hay = (p.id + ' ' + p.label + ' ' + (p.tags || []).join(' ') + ' ' + p.cls).toLowerCase();
      if (q && !hay.includes(q) && !q.split(' ').every(t => hay.includes(t))) continue;
      (groups[p.group] ||= []).push(p);
    }
    for (const [gname, list2] of Object.entries(groups)) {
      list.insertAdjacentHTML('beforeend', `<div class="section-label"><span>${esc(gname)}</span><span>${list2.length}</span></div>`);
      for (const p of list2) {
        const sec = solveSection(p);
        const el = document.createElement('button');
        el.className = 'lib' + (target ? (target._profile?.id === p.id ? ' on' : '') : (state.activeProfile === p.id ? ' on' : ''));
        el.innerHTML = `<span class="thumb">${sectionSVG(p, 78, { compact: true })}</span>
          <span class="txt"><b>${esc(p.label)}</b><span>${m2(sec.total)} m · ${sec.elements.filter(e => e.k === 'lane').length} ln · ${p.speed || '—'} km/h</span></span>
          <span class="grp">${target ? 'apply' : 'active'}</span>`;
        el.addEventListener('click', () => {
          if (target) edit('profile', () => { target.profile = p.id; target.cls = p.cls; });
          else { state.activeProfile = p.id; $('#hudProfile').textContent = p.id; }
          renderConstruct(); renderTree();
          toast(target ? `${roadLabel(target)} → ${p.label}` : `Active profile: ${p.label}`);
        });
        list.appendChild(el);
      }
    }
    if (!list.children.length) list.innerHTML = `<div class="empty">Nothing matches “${esc(inp.value)}”.</div>`;
  };
  inp.addEventListener('input', paintList);
  paintList();
  body.appendChild(card('Prop palette', 'cone', [`<div class="pills">${Object.entries(PROP_SPECS).slice(0, 0).join('')}</div>`]));
}
function renderGenerate() {
  const body = $('#detBody');
  body.innerHTML = '';
  setHead('City generator', 'Parametric city', `<span class="chip">${esc(state.preset)}</span><span>seed ${state.params.seed} · grid ${state.params.blocksX}×${state.params.blocksZ}</span>`, 'graduation');
  const P = state.params;
  const set = (k, v, live = false) => { P[k] = v; markDirty(); if (k === 'seed') $('#stSeed').textContent = v; build({ immediate: !live }); };
  const presets = document.createElement('div');
  presets.className = 'card';
  presets.innerHTML = `<div class="card-head"><span>${icon('map')}Presets</span></div><div class="pills"></div>`;
  const pw = $('.pills', presets);
  for (const [k, v] of Object.entries(CITY_PRESETS)) {
    const b = document.createElement('button');
    b.textContent = v.label.split(' · ')[0];
    b.title = v.label;
    if (state.preset === k) b.setAttribute('aria-pressed', 'true');
    b.addEventListener('click', () => {
      state.preset = k;
      Object.assign(P, applyPreset(k, {}));
      [...pw.children].forEach(x => x.setAttribute('aria-pressed', x === b));
      renderGenerate(); build(); toast(`Preset · ${v.label}`);
    });
    presets.querySelector('.pills').appendChild(b);
  }
  body.appendChild(presets);

  const c = document.createElement('div');
  c.className = 'card';
  c.innerHTML = `<div class="card-head"><span>${icon('grid')}Grid</span><span class="act"><button class="btn sm" data-gen>${icon('play', 'i sm')}Generate</button></span></div>`;
  const bd = $('.bd', c) || c;
  const seed = document.createElement('div');
  seed.className = 'seed';
  seed.innerHTML = `<span style="font-size:10px;color:#9a9a9a;width:52px">Seed</span><input type="number" value="${P.seed}">
    <button class="btn sm ghost" data-dice title="Random seed">${icon('shuffle', 'i sm')}</button>`;
  $('input', seed).addEventListener('change', e => set('seed', +e.target.value | 0));
  $('[data-dice]', seed).addEventListener('click', () => { P.seed = 1000 + Math.floor(Math.random() * 99000); $('input', seed).value = P.seed; build(); });
  bd.appendChild(seed);
  bd.appendChild(slider({ label: 'Blocks X', min: 3, max: 12, value: P.blocksX, onInput: v => { P.blocksX = v; $('#stSeed').textContent = P.seed; }, commit: v => set('blocksX', v) }));
  bd.appendChild(slider({ label: 'Blocks Z', min: 3, max: 12, value: P.blocksZ, onInput: v => { P.blocksZ = v; }, commit: v => set('blocksZ', v) }));
  bd.appendChild(slider({ label: 'Block size', min: 60, max: 190, step: 2, unit: 'm', value: P.blockSize, onInput: v => P.blockSize = v, commit: v => set('blockSize', v) }));
  bd.appendChild(slider({ label: 'Avenue every n blocks', min: 2, max: 6, value: P.avenueEvery, onInput: v => P.avenueEvery = v, commit: v => set('avenueEvery', v) }));
  bd.appendChild(slider({ label: 'Meander', min: 0, max: 26, step: .5, unit: 'm', value: P.meander, onInput: v => P.meander = v, commit: v => set('meander', v) }));
  bd.appendChild(slider({ label: 'Grid rotation', min: -45, max: 45, step: 1, unit: '°', value: P.rotation, onInput: v => P.rotation = v, commit: v => set('rotation', v) }));
  bd.appendChild(slider({ label: 'Roundabout share', min: 0, max: 1, step: .05, value: P.roundabouts, fmt: v => Math.round(v * 100) + '%', onInput: v => P.roundabouts = v, commit: v => set('roundabouts', v) }));
  body.appendChild(c);
  $('[data-gen]', c).addEventListener('click', () => { build(); toast('City regenerated'); });

  const tg = document.createElement('div');
  tg.className = 'card';
  tg.innerHTML = `<div class="card-head"><span>${icon('bus')}Network provision</span></div>`;
  const tgrid = document.createElement('div'); tgrid.className = 'tog-grid';
  const TOGGLES = [['bus', 'Bus lanes'], ['parking', 'Parking bays'], ['ring', 'Ring road'], ['diag', 'Diagonals'], ['river', 'River'], ['rail', 'Railway'], ['greenway', 'Riverside walk'], ['oldTown', 'Old-town mall'], ['parks', 'Parks'], ['lots', 'Car parks'], ['massing', 'Massing'], ['marks', 'Road markings'], ['props', 'Street furniture']];
  for (const [k, label] of TOGGLES) {
    const val = k === 'marks' ? (P.markings > 0) : k === 'props' ? (P.props > 0) : k === 'parks' || k === 'lots' ? !!P[k] : !!P[k];
    tgrid.appendChild(toggle({ label, on: val, onChange: v => { if (k === 'marks') P.markings = v ? 1 : 0; else if (k === 'props') P.props = v ? 1 : 0; else if (k === 'parks') P.parks = v ? .16 : 0; else if (k === 'lots') P.lots = v ? 4 : 0; else P[k] = v; markDirty(); build(); } }));
  }
  tg.appendChild(tgrid);
  tg.appendChild(segmented({ label: 'Cycle network', options: ['none', 'arterials', 'all', 'grid'].map(v => ({ label: clsLabel(v), value: v })), value: P.cycle, onChange: v => set('cycle', v) }));
  tg.appendChild(slider({ label: 'Metro lines', min: 0, max: 4, value: P.subway, onInput: v => P.subway = v, commit: v => set('subway', v) }));
  tg.appendChild(slider({ label: 'Metro depth', min: -30, max: -5, step: 1, unit: 'm', value: P.subwayDepth, onInput: v => P.subwayDepth = v, commit: v => set('subwayDepth', v) }));
  tg.appendChild(slider({ label: 'River width', min: 18, max: 90, step: 2, unit: 'm', value: P.riverWidth, onInput: v => P.riverWidth = v, commit: v => set('riverWidth', v) }));
  tg.appendChild(slider({ label: 'Rail offset', min: -.8, max: .8, step: .05, value: P.railOffset, fmt: v => v.toFixed(2), onInput: v => P.railOffset = v, commit: v => set('railOffset', v) }));
  tg.appendChild(slider({ label: 'Massing height', min: .2, max: 2, step: .05, value: P.massingHeight, onInput: v => P.massingHeight = v, commit: v => set('massingHeight', v) }));
  tg.appendChild(slider({ label: 'Massing cover', min: .2, max: 1, step: .02, value: P.massingCover, onInput: v => P.massingCover = v, commit: v => set('massingCover', v) }));
  body.appendChild(tg);

  const sun = document.createElement('div');
  sun.className = 'card';
  sun.innerHTML = `<div class="card-head"><span>${icon('sun')}Sun &amp; sky</span><span class="act"><span class="chip" id="sunChip">${(scene.sunParams?.elevation ?? 38).toFixed(0)}°</span></span></div>`;
  const sp = scene.sunParams || { azimuth: 128, elevation: 38, exposure: 1.05 };
  sun.appendChild(slider({ label: 'Time of day', min: 0, max: 24, step: .25, value: hourFromSun(sp), unit: '', fmt: v => hhmm(v), onInput: v => setHour(v) }));
  sun.appendChild(slider({ label: 'Sun azimuth', min: 0, max: 360, step: 1, unit: '°', value: sp.azimuth, onInput: v => scene.setSun({ ...scene.sunParams, azimuth: v }) }));
  sun.appendChild(slider({ label: 'Exposure', min: .4, max: 2, step: .02, value: sp.exposure, onInput: v => scene.setSun({ ...scene.sunParams, exposure: v }) }));
  sun.appendChild(slider({ label: 'Field of view', min: 25, max: 105, step: 1, unit: '°', value: state.fov || 55, onInput: v => { state.fov = v; } }));
  body.appendChild(sun);
}
const hourFromSun = s => 6 + clamp(s.elevation, -12, 88) / 88 * 12;
const hhmm = v => String(Math.floor(v) % 24).padStart(2, '0') + ':' + String(Math.round((v % 1) * 60)).padStart(2, '0');
function setHour(h) {
  const t = ((h - 6) / 12);
  const elevation = Math.sin(clamp(t, 0, 1) * Math.PI) * 72 - (t < 0 || t > 1 ? 8 : 0);
  const azimuth = 70 + clamp(t, -.15, 1.15) * 220;
  scene.setSun({ azimuth, elevation: clamp(elevation, -10, 80), exposure: scene.sunParams?.exposure ?? 1.05 });
  $('#sunChip') && ($('#sunChip').textContent = `${Math.round(clamp(elevation, -10, 80))}° · ${hhmm(h)}`);
}
const TABS = { details: renderDetails, construct: renderConstruct, generate: renderGenerate };
function setTab(t) {
  state.tab = t;
  $$('#detTabs button').forEach(b => b.setAttribute('aria-pressed', b.dataset.tab === t));
  TABS[t]();
}

/* ------------------------------- tools ------------------------------- */
function setTool(t) {
  state.tool = t;
  $('#app').dataset.tool = t;
  $$('.tool[data-tool]').forEach(b => b.setAttribute('aria-pressed', b.dataset.tool === t));
  if (t !== 'draw') cancelDraw();
  if (t !== 'measure') clearMeasure();
  host.style.cursor = t === 'draw' || t === 'paint' ? 'crosshair' : t === 'erase' ? 'cell' : 'default';
  const labels = { select: 'Click a corridor to inspect it. Drag with RMB to look around.', draw: 'Click to place alignment points. Enter or double-click to finish, Esc to cancel.', point: 'Drag an alignment handle to re-shape the selected road.', elev: 'Drag a handle vertically to set height (grades, bridges, tunnels).', paint: 'Click the carriageway to stamp the active feature. Change it in the road inspector.', measure: 'Click two points to measure distance and bearing.', erase: 'Click a road or junction to delete it.' };
  $('#hint').innerHTML = `<span style="color:#c8c8c8">${labels[t]}</span>`;
  if (t === 'draw') $('#hudProfile').textContent = state.activeProfile;
}
function rayGround(nx, ny) {
  const { o, d } = scene._ray(nx, ny);
  let t = d.y < -1e-5 ? (0 - o.y) / d.y : 1e6;
  const hit = scene.pick(nx, ny);
  if (hit?.point) { const p = hit.point; return { x: p[0], z: p[2], y: p[1] - levelAtFor(hit.road, 0), snap: true }; }
  if (t > 4000) return null;
  return { x: o.x + d.x * t, z: o.z + d.z * t, y: 0 };
}
const levelAtFor = (road, off) => { if (!road) return 0; prepareRoad(road); return levelAt(road._section, road._profile, off) + (road.deckY || 0); };
function snapPoint(p, from) {
  let q = { ...p };
  if (state.snap.node) {
    let best = null;
    for (const n of state.network.nodes) { const d = dist(q, n); if (d < 16 && (!best || d < best.d)) best = { d, n }; }
    for (const r of state.network.roads) for (const pt of r.points) { const d = dist(q, pt); if (d < 6 && (!best || d < best.d)) best = { d, pt }; }
    if (best) return { x: best.n?.x ?? best.pt.x, z: best.n?.z ?? best.pt.z, y: best.n?.y ?? best.pt.y ?? 0, snapped: true };
  }
  if (state.snap.grid) { q.x = snap(q.x, 1); q.z = snap(q.z, 1); }
  if (state.snap.angle && from) { const s = snapAngle(q, from, 15); q = s; }
  return q;
}
function addDrawPoint() {
  const p = rayGround(scene.pointer.nx, scene.pointer.ny);
  if (!p) return toast('Point the cursor at the ground first', 'warn');
  const from = state.draw?.points.length ? state.draw.points[state.draw.points.length - 1] : null;
  const q = snapPoint({ x: p.x, z: p.z, y: state.draw?.points.length ? (state.draw.points[0].y || 0) : 0 }, from);
  if (!state.draw) state.draw = { points: [] };
  const pts = state.draw.points;
  if (pts.length) {
    const last = pts[pts.length - 1];
    if (dist(last, q) < 4) return;
  }
  pts.push({ x: q.x, z: q.z, y: q.y || 0 });
  scene.setGhost(pts, state.activeProfile);
  updateHud();
}
function updateHud() {
  const pts = state.draw?.points || [];
  let len = 0;
  for (let i = 1; i < pts.length; i++) len += dist(pts[i - 1], pts[i]);
  $('#hudLen').textContent = m2(len) + ' m';
  $('#hudPts').textContent = pts.length;
}
function cancelDraw() {
  if (!state.draw) return;
  state.draw = null;
  scene.setGhost(null);
  updateHud();
}
function commitDraw() {
  const pts = state.draw?.points;
  if (!pts || pts.length < 2) return toast('Need at least two points', 'warn');
  pushUndo();
  const prof = profileById(state.activeProfile) || PROFILES[0];
  const id = 'road-' + (state.network.roads.length + 1).toString(36) + '-' + Math.floor(Math.random() * 1e4).toString(36);
  const road = {
    id, name: `${clsLabel(prof.cls)} ${SUF[prof.cls] || 'Road'} ${state.network.roads.length + 1}`, cls: prof.cls,
    profile: prof.id, points: pts.map(p => ({ x: +p.x.toFixed(2), z: +p.z.toFixed(2), y: +(p.y || 0).toFixed(2) })),
    smooth: 2, source: 'drawn', bumps: [], crosstables: [], driveways: [], crossingsManual: [], crossings: [], noEndCrossings: false
  };
  state.network.roads.push(road);
  wireEnds(road);
  state.draw = null;
  scene.setGhost(null);
  markDirty();
  measureLengths();
  build();
  selectById(id, { frame: false });
  toast(`Drew ${road.name} · ${m2(road._len)} m`);
}
/** create or reuse nodes at both ends of a road and register arms */
function wireEnds(road) {
  const nodes = state.network.nodes;
  const findNear = (p, r = 12) => nodes.find(n => dist(n, p) < r);
  road.nodeIds = [];
  for (const [end, pt] of [['s', road.points[0]], ['e', road.points[road.points.length - 1]]]) {
    let n = findNear(pt);
    if (!n) {
      n = { id: 'j-' + (nodes.length + 1).toString(36) + Math.floor(Math.random() * 900 + 100), x: pt.x, z: pt.z, y: pt.y || 0, style: 'uncontrolled', arms: [], radius: 12 };
      nodes.push(n);
    }
    n.arms.push({ road: road.id, end, x: pt.x, z: pt.z });
    road.nodeIds.push(n.id);
  }
}
function grabHandle(hit) {
  const road = selectedRoad();
  if (!road) return toast('Select a road first, then drag its handles', 'warn');
  const pts = road.points;
  const { o, d } = scene._ray(scene.pointer.nx, scene.pointer.ny);
  const cam = scene.camera.position;
  let best = -1, bd = 1e9;
  const v = new THREE.Vector3();
  pts.forEach((p, i) => {
    v.set(p.x, (p.y || 0) + (road.deckY || 0), p.z).project(scene.camera);
    const sx = (v.x - scene.pointer.nx), sy = (v.y - scene.pointer.ny);
    const dd = Math.hypot(sx, sy);
    if (dd < bd) { bd = dd; best = i; }
  });
  if (best < 0 || bd > .09) return;
  const g = rayGround(scene.pointer.nx, scene.pointer.ny) || { x: pts[best].x, z: pts[best].z, y: pts[best].y };
  const start = { ...pts[best] };
  pushUndo();
  const rect = host.getBoundingClientRect();
  const move = e => {
    const nx = ((e.clientX - rect.left) / rect.width) * 2 - 1, ny = -((e.clientY - rect.top) / rect.height) * 2 + 1;
    const gg = rayGround(nx, ny); if (!gg) return;
    if (state.tool === 'elev') {
      const dy = -(e.clientY - (pts._lastY ?? e.clientY)) * .14;
      pts._lastY = e.clientY;
      pts[best].y = (pts[best].y || 0) + dy;
    } else {
      pts[best].x = snap(gg.x, state.snap.grid ? 1 : 0);
      pts[best].z = snap(gg.z, state.snap.grid ? 1 : 0);
    }
    scene.setGhost(null);
    renderOverlayLive(road);
  };
  const up = () => {
    removeEventListener('pointermove', move);
    removeEventListener('pointerup', up);
    delete pts._lastY;
    void start;
    markDirty();
    measureLengths();
    build();
  };
  addEventListener('pointermove', move);
  addEventListener('pointerup', up);
}
const renderOverlayLive = road => { scene.setSelection(road.id); };
function paintAt(hit) {
  if (!hit || hit.kind !== 'road') return toast('Point at a corridor to stamp the feature', 'warn');
  const road = hit.road;
  const f = state.paint;
  edit('paint', () => {
    if (f === 'cushion') (road.bumps ||= []).push({ s: hit.s, kind: 'cushion' });
    else if (f === 'table') (road.bumps ||= []).push({ s: hit.s, kind: 'table' });
    else if (f === 'flat') (road.bumps ||= []).push({ s: hit.s, kind: 'flat' });
    else if (f === 'xstable') (road.crosstables ||= []).push({ s: hit.s });
    else if (f === 'zebra') (road.crossingsManual ||= []).push({ s: hit.s, style: 'zebra' });
    else if (f === 'ladder') (road.crossingsManual ||= []).push({ s: hit.s, style: 'ladder' });
    else if (f === 'driveway-l') (road.driveways ||= []).push({ s: hit.s, side: -1 });
    else if (f === 'driveway-r') (road.driveways ||= []).push({ s: hit.s, side: 1 });
    else if (f === 'signal') { const n = nearestNode(hit.point); if (n) n.style = 'signals'; }
    else if (f === 'roundabout') { const n = nearestNode(hit.point); if (n) n.style = 'roundabout'; }
    else if (f === 'station') { const p = road.points; (road.stations ||= []).push({ s: hit.s, name: 'Station ' + ((road.stations?.length || 0) + 1), x: hit.point[0], z: hit.point[2] }); }
    for (const k of ['bumps', 'crosstables', 'crossingsManual', 'driveways', 'stations']) road[k]?.sort((a, b) => a.s - b.s);
  });
}
const nearestNode = p => p ? state.network.nodes.reduce((b, n) => { const d = Math.hypot(n.x - p[0], n.z - p[2]); return (!b || d < b.d) ? { d, n } : b; }, null)?.n : null;
function deleteHit(hit) {
  if (!hit) return;
  pushUndo();
  if (hit.kind === 'node') state.network.nodes = state.network.nodes.filter(n => n.id !== hit.node.id);
  else {
    const id = hit.id;
    state.network.roads = state.network.roads.filter(r => r.id !== id);
    state.network.nodes.forEach(n => n.arms = n.arms.filter(a => a.road !== id));
  }
  state.sel = null; scene.setSelection(null);
  markDirty(); measureLengths(); build();
}
function dupRoad(road) {
  pushUndo();
  const copy = JSON.parse(JSON.stringify(road));
  copy.id = road.id + '-copy';
  copy.name = roadLabel(road) + ' (copy)';
  const d = 14;
  prepareRoad(road);
  const f = framesOf(road);
  const n0 = f[0];
  copy.points = copy.points.map(p => ({ x: p.x - n0.nz * d, z: p.z + n0.nx * d, y: p.y || 0 }));
  state.network.roads.push(copy);
  wireEnds(copy);
  state._editing = false;
  markDirty(); measureLengths(); build();
  selectById(copy.id);
}
function splitRoad(road) {
  prepareRoad(road);
  const pts = road.points;
  if (pts.length < 3) return toast('Need at least 3 points to split', 'warn');
  pushUndo();
  const mid = Math.floor(pts.length / 2);
  const b = JSON.parse(JSON.stringify(road));
  b.id = road.id + '-b'; b.name = roadLabel(road) + ' (part 2)';
  b.points = pts.slice(mid);
  road.points = pts.slice(0, mid + 1);
  for (const k of ['bumps', 'crosstables', 'crossingsManual', 'driveways', 'stations']) {
    if (!road[k]) continue;
    const L = road._len;
    b[k] = road[k].filter(x => x.s > L * (mid / (pts.length - 1)));
    road[k] = road[k].filter(x => x.s <= L * (mid / (pts.length - 1)));
  }
  state.network.roads.push(b);
  state.network.nodes.forEach(n => n.arms = n.arms.filter(a => a.road !== road.id || a.end !== 'e'));
  wireEnds(road); wireEnds(b);
  markDirty(); measureLengths(); build();
  toast('Split into two corridors');
}
function joinEnds() {
  const road = selectedRoad();
  if (!road) return toast('Select a road to join', 'warn');
  const n = nearestNode([road.points[road.points.length - 1].x, 0, road.points[road.points.length - 1].z]);
  if (!n) return toast('No junction nearby', 'warn');
  const near = state.network.nodes.filter(x => x !== n && dist(x, n) < 22);
  if (!near.length) return toast('No second junction within 22 m', 'warn');
  pushUndo();
  for (const x of near) {
    for (const a of x.arms) { a.x = n.x; a.z = n.z; const r = state.network.roads.find(rr => rr.id === a.road); if (r) { const p = a.end === 's' ? r.points[0] : r.points[r.points.length - 1]; p.x = n.x; p.z = n.z; r.nodeIds = (r.nodeIds || []).filter(i => i !== x.id); if (!r.nodeIds.includes(n.id)) r.nodeIds.push(n.id); if (!n.arms.some(b => b.road === a.road && b.end === a.end)) n.arms.push({ road: a.road, end: a.end, x: n.x, z: n.z }); } }
    state.network.nodes = state.network.nodes.filter(y => y !== x);
  }
  markDirty(); measureLengths(); build();
  toast('Junctions merged');
}
function measureClick(hit) {
  const p = hit?.point ? { x: hit.point[0], y: hit.point[1], z: hit.point[2] } : rayGround(scene.pointer.nx, scene.pointer.ny);
  if (!p) return;
  if (!state.measure) state.measure = [p];
  else {
    state.measure.push(p);
    const [a, b] = state.measure;
    const d = Math.hypot(b.x - a.x, b.y - a.y, b.z - a.z);
    const brg = (Math.atan2(b.x - a.x, b.z - a.z) * 180 / Math.PI + 360) % 360;
    toast(`Measure · ${m2(d)} m · bearing ${brg.toFixed(0)}° · Δy ${m2(b.y - a.y)} m`);
    state.measureLabel = `${m2(d)} m @ ${brg.toFixed(0)}°`;
  }
  drawMeasure();
}
function drawMeasure() {
  for (const c of [...scene.groups.ghost.children]) { scene.groups.ghost.remove(c); c.geometry?.dispose?.(); c.material?.dispose?.(); }
  const m = state.measure;
  if (!m?.length) return;
  const pts = m.map(p => new THREE.Vector3(p.x, p.y + .4, p.z));
  if (pts.length > 1) {
    const g = new THREE.BufferGeometry().setFromPoints(pts);
    scene.groups.ghost.add(new THREE.Line(g, new THREE.LineBasicMaterial({ color: 0xe8b65f })));
  }
  for (const p of pts) {
    const s = new THREE.Mesh(new THREE.SphereGeometry(.55, 12, 8), new THREE.MeshBasicMaterial({ color: 0xe8b65f }));
    s.position.copy(p); scene.groups.ghost.add(s);
  }
  $('#hint').innerHTML = `<span style="color:#c8c8c8">Measure ${state.measureLabel || '— click two points'} · <kbd>Esc</kbd> reset</span>`;
}
const clearMeasure = () => { state.measure = null; state.measureLabel = null; for (const c of [...scene.groups.ghost.children]) { scene.groups.ghost.remove(c); c.geometry?.dispose?.(); c.material?.dispose?.(); } };

/* ------------------------------- camera / view ------------------------------- */
function planView() {
  let r = 220;
  for (const road of state.network.roads) for (const p of road.points) r = Math.max(r, Math.hypot(p.x, p.z) + 60);
  scene.cam.pos.set(0, r * 1.35, 0);
  scene.cam.yaw = 0; scene.cam.pitch = -1.52;
  scene.mode = 'fly';
  toast('Plan view');
}
function setCamMode(m) {
  state.prevCam = scene.mode;
  $$('.tabs [data-cam]').forEach(b => b.setAttribute('aria-pressed', b.dataset.cam === m));
  if (m === 'drive') {
    const road = selectedRoad() || state.network.roads.find(r => r.cls === 'arterial') || state.network.roads[0];
    if (!road) return toast('Nothing to drive yet', 'warn');
    scene.driveOn(road.id, 0);
    $('#app').dataset.mode = 'drive';
    $('#hint').innerHTML = `<span style="color:#c8c8c8">Driving <b>${esc(roadLabel(road))}</b> · <kbd>Esc</kbd> exit · <kbd>[</kbd><kbd>]</kbd> change corridor</span>`;
    state.driveRoad = road;
  } else {
    scene.stopDrive?.();
    scene.setMode(m);
    $('#app').dataset.mode = 'edit';
    setTool(state.tool);
  }
  $('#stMode').textContent = m;
}
function driveRoad(road) { state.sel = { kind: 'road', id: road.id }; setCamMode('drive'); }

/* ------------------------------- minimap ------------------------------- */
const mini = $('#mini'), mctx = mini.getContext('2d');
let mmBox = { x0: -400, x1: 400, z0: -400, z1: 400 };
function drawMini() {
  if (!state.network.roads.length) return;
  const W = mini.width, H = mini.height;
  let x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9;
  for (const r of state.network.roads) for (const p of r.points) { x0 = Math.min(x0, p.x); x1 = Math.max(x1, p.x); z0 = Math.min(z0, p.z); z1 = Math.max(z1, p.z); }
  const pad = 22;
  const sc = Math.min((W - pad * 2) / Math.max(1, x1 - x0), (H - pad * 2) / Math.max(1, z1 - z0));
  const cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
  mmBox = { x0, x1, z0, z1, sc, cx, cz, W, H };
  mctx.clearRect(0, 0, W, H);
  mctx.fillStyle = '#0e1116'; mctx.fillRect(0, 0, W, H);
  for (const w of state.network.water || []) {
    mctx.strokeStyle = 'rgba(76,124,168,.6)'; mctx.lineWidth = Math.max(2, (w.width || 20) * sc * .5); mctx.lineCap = 'round';
    mctx.beginPath();
    (w.points || []).forEach((p, i) => { const x = W / 2 + (p.x - cx) * sc, y = H / 2 + (p.z - cz) * sc; i ? mctx.lineTo(x, y) : mctx.moveTo(x, y); });
    mctx.stroke();
  }
  for (const a of state.network.areas || []) {
    if (!a.polygon?.length) continue;
    mctx.fillStyle = a.kind === 'park' ? 'rgba(98,132,84,.4)' : a.kind === 'plaza' ? 'rgba(150,140,120,.35)' : 'rgba(90,90,90,.25)';
    mctx.beginPath();
    a.polygon.forEach((p, i) => { const x = W / 2 + (p.x - cx) * sc, y = H / 2 + (p.z - cz) * sc; i ? mctx.lineTo(x, y) : mctx.moveTo(x, y); });
    mctx.closePath(); mctx.fill();
  }
  mctx.lineCap = 'round';
  for (const r of state.network.roads) {
    if (state.muted.has(r.id)) continue;
    const c = CLASS_COLOR[r.cls] ?? .5;
    const col = new THREE.Color(c);
    mctx.strokeStyle = `rgb(${(col.r * 255) | 0},${(col.g * 255) | 0},${(col.b * 255) | 0})`;
    mctx.lineWidth = ({ freeway: 2.6, motorway: 2.6, expressway: 2.4, arterial: 1.9, boulevard: 1.9, collector: 1.4, bus: 1.4, cycle: 1, path: .8, rail: 2, subway: 1.6 }[r.cls] || 1.1);
    if (state.sel?.id === r.id) { mctx.strokeStyle = '#9dc0ff'; mctx.lineWidth += 1; }
    if (state.hover === r.id) { mctx.strokeStyle = '#dfe9ff'; }
    mctx.beginPath();
    const f = r._frames?.length ? r._frames : r.points;
    f.forEach((p, i) => { const x = W / 2 + (p.x - cx) * sc, y = H / 2 + (p.z - cz) * sc; i ? mctx.lineTo(x, y) : mctx.moveTo(x, y); });
    mctx.stroke();
  }
  /* camera frustum */
  const cp = scene.camera.position;
  const px = W / 2 + (cp.x - cx) * sc, py = H / 2 + (cp.z - cz) * sc;
  const yaw = scene.cam.yaw;
  mctx.fillStyle = 'rgba(143,182,255,.18)'; mctx.strokeStyle = 'rgba(200,220,255,.75)'; mctx.lineWidth = 1;
  const fovr = (scene.camera.fov * Math.PI / 180), L = 90;
  mctx.beginPath();
  mctx.moveTo(px, py);
  const dir = a => [Math.sin(yaw + a), Math.cos(yaw + a)];
  const [ax, ay] = dir(-fovr), [bx, by] = dir(fovr);
  mctx.lineTo(px + ax * L, py + ay * L); mctx.lineTo(px + bx * L, py + by * L); mctx.closePath();
  mctx.fill(); mctx.stroke();
  mctx.fillStyle = '#e9f0ff';
  mctx.beginPath(); mctx.arc(px, py, 2.4, 0, 7); mctx.fill();
  $('#mmScale').textContent = Math.round(mini.width / sc) + ' m across';
}
mini.addEventListener('click', e => {
  const r = mini.getBoundingClientRect();
  const x = (e.clientX - r.left) / r.width * mini.width, y = (e.clientY - r.top) / r.height * mini.height;
  const wx = (x - mmBox.W / 2) / mmBox.sc + mmBox.cx, wz = (y - mmBox.H / 2) / mmBox.sc + mmBox.cz;
  const near = state.network.roads.reduce((b, rd) => { const p = rd.points[Math.floor(rd.points.length / 2)]; const d = Math.hypot(p.x - wx, p.z - wz); return !b || d < b.d ? { d, id: rd.id } : b; }, null);
  if (near) selectById(near.id, { frame: true });
  else scene.focusOn(wx, wz, 0, 120, 60);
});

/* ------------------------------- legend ------------------------------- */
function renderLegend() {
  const byCls = {};
  for (const r of state.network.roads) byCls[r.cls] = (byCls[r.cls] || 0) + 1;
  $('#legend').innerHTML = Object.entries(byCls).sort((a, b) => b[1] - a[1]).slice(0, 8).map(([c, n]) =>
    `<span class="lg"><i style="background:#${new THREE.Color(CLASS_COLOR[c] ?? .5).getHexString()}"></i>${clsLabel(c)} ${n}</span>`).join('');
}

/* ------------------------------- toasts / modal ------------------------------- */
let toastT = 0;
function toast(msg, kind = 'ok') {
  const el = document.createElement('div');
  el.className = 'toast ' + (kind === 'ok' ? '' : kind);
  el.innerHTML = `<span class="ic">${icon(kind === 'warn' || kind === 'bad' ? 'alert' : kind === 'info' ? 'info' : 'check', 'i sm')}</span><span>${msg}</span>`;
  $('#toasts').appendChild(el);
  setTimeout(() => { el.style.opacity = '0'; el.style.transform = 'translateX(10px)'; setTimeout(() => el.remove(), 240); }, 3200);
}
function sheet(html) {
  $('#sheet').innerHTML = html;
  $('#scrim').classList.add('on');
  $$('[data-close]', $('#sheet')).forEach(b => b.addEventListener('click', closeSheet));
}
const closeSheet = () => $('#scrim').classList.remove('on');
$('#scrim').addEventListener('click', e => { if (e.target === $('#scrim')) closeSheet(); });

/* ------------------------------- keyboard ------------------------------- */
function onKey(e) {
  if (e.target && /INPUT|TEXTAREA|SELECT/.test(e.target.tagName)) { if (e.key === 'Escape') e.target.blur(); return false; }
  const mod = e.ctrlKey || e.metaKey;
  if (mod && e.key.toLowerCase() === 'z') { e.preventDefault(); e.shiftKey ? redo() : undo(); return false; }
  if (mod && e.key.toLowerCase() === 's') { e.preventDefault(); saveLocal(); return false; }
  if (mod && e.key.toLowerCase() === 'b') { e.preventDefault(); build(); return false; }
  if (mod && e.key.toLowerCase() === 'p') { e.preventDefault(); capture(); return false; }
  if (mod && e.key.toLowerCase() === 'd') { e.preventDefault(); const r = selectedRoad(); if (r) dupRoad(r); return false; }
  if (e.key === 'Escape') { if ($('#scrim').classList.contains('on')) closeSheet(); else if (state.draw) cancelDraw(); else if (scene.mode === 'drive') setCamMode(state.prevCam || 'fly'); else { state.sel = null; scene.setSelection(null); renderTree(); renderDetails(); } return false; }
  if (e.shiftKey && e.key.toLowerCase() === 'a') { e.preventDefault(); openConstruct(selectedRoad()); setTab('construct'); return false; }
  if (e.key === '/') { e.preventDefault(); $('#treeSearch').focus(); return false; }
  if (e.key === 'Enter' && state.draw) { commitDraw(); return false; }
  if (e.key === 'Delete' || e.key === 'Backspace') { const r = selectedRoad(); const n = selectedNode(); if (r || n) { pushUndo(); if (r) { state.network.roads = state.network.roads.filter(x => x !== r); state.network.nodes.forEach(nd => nd.arms = nd.arms.filter(a => a.road !== r.id)); } else state.network.nodes = state.network.nodes.filter(x => x !== n); state.sel = null; scene.setSelection(null); markDirty(); measureLengths(); build(); } return false; }
  if (e.key === '?') { shortcuts(); return false; }
  switch (e.key) {
    case 'v': case 'V': setTool('select'); break;
    case 'r': case 'R': setTool('draw'); break;
    case 'e': if (!e.shiftKey) setTool('point'); break;
    case 'h': case 'H': setTool('elev'); break;
    case 't': case 'T': setTool('paint'); break;
    case 'm': case 'M': setTool('measure'); break;
    case '1': scene.setView('lit'); syncView(); break;
    case '2': scene.setView('unlit'); syncView(); break;
    case '3': scene.setView('id'); syncView(); break;
    case '4': scene.setView('wire'); syncView(); break;
    case 'g': case 'G': $('#fGrid').click(); break;
    case 'x': case 'X': $('#fXray').click(); break;
    case 'n': case 'N': setHour(((scene.sunParams && hourFromSun(scene.sunParams)) || 13) + 1.5); break;
    case '[': case ']': {
      const roads = state.network.roads; if (!roads.length) break;
      let i = roads.findIndex(r => r.id === state.driveRoad?.id);
      i = (i + (e.key === ']' ? 1 : roads.length - 1) + roads.length) % roads.length;
      state.driveRoad = roads[i]; scene.driveOn(roads[i].id, 0); selectById(roads[i].id); break;
    }
    case 'Tab': setTab(state.tab === 'details' ? 'construct' : state.tab === 'construct' ? 'generate' : 'details'); e.preventDefault(); break;
  }
}
const syncView = () => $$('#viewSeg button').forEach(b => b.setAttribute('aria-pressed', b.dataset.view === scene.view));
addEventListener('keydown', onKey);
$('#viewport').addEventListener('dblclick', () => { if (state.draw) commitDraw(); });

/* ------------------------------- chrome wiring ------------------------------- */
$$('.tool[data-tool]').forEach(b => b.addEventListener('click', () => setTool(b.dataset.tool)));
$$('#viewSeg button').forEach(b => b.addEventListener('click', () => { scene.setView(b.dataset.view); syncView(); }));
$$('#detTabs button').forEach(b => b.addEventListener('click', () => setTab(b.dataset.tab)));
$$('.tabs [data-cam]').forEach(b => b.addEventListener('click', () => setCamMode(b.dataset.cam)));
const flagBtn = (sel, key, after) => $(sel).addEventListener('click', () => {
  const v = $(sel).getAttribute('aria-pressed') !== 'true';
  $(sel).setAttribute('aria-pressed', v);
  if (key) scene.flags[key] = v;
  scene.applyView();
  after && after(v);
});
flagBtn('#fShadow', 'shadows');
flagBtn('#fGrid', 'grid');
flagBtn('#fMarks', 'marks');
flagBtn('#fProps', 'props');
flagBtn('#fTraffic', 'traffic');
flagBtn('#fMassing', 'massing', v => { scene.setLayer('massing', v); });
flagBtn('#fXray', 'xray');
$('#snapGrid').addEventListener('click', () => { state.snap.grid = state.snap.grid ? 0 : 1; $('#snapGrid').setAttribute('aria-pressed', !!state.snap.grid); });
$('#snapNode').addEventListener('click', () => { state.snap.node = state.snap.node ? 0 : 1; $('#snapNode').setAttribute('aria-pressed', !!state.snap.node); });
$('#snapAngle').addEventListener('click', () => { state.snap.angle = state.snap.angle ? 0 : 1; $('#snapAngle').setAttribute('aria-pressed', !!state.snap.angle); });
$('#uolo').addEventListener('click', undo);
$('#uered').addEventListener('click', redo);
$('#shot').addEventListener('click', capture);
$('#hudFinish').addEventListener('click', commitDraw);
$('#hudCancel').addEventListener('click', cancelDraw);
$('#treeSearch').addEventListener('input', e => { state.filter = e.target.value; renderTree(); });
$$('.menu > button').forEach(b => b.addEventListener('click', e => {
  e.stopPropagation();
  const m = b.parentElement;
  const was = m.classList.contains('open');
  $$('.menu').forEach(x => x.classList.remove('open'));
  if (!was) m.classList.add('open');
}));
addEventListener('pointerdown', e => { if (!e.target.closest('.menu')) $$('.menu').forEach(x => x.classList.remove('open')); });
$$('.menu [data-act], .menu [data-preset]').forEach(b => b.addEventListener('click', () => {
  $$('.menu').forEach(x => x.classList.remove('open'));
  const p = b.dataset.preset;
  if (p) { state.preset = p; Object.assign(state.params, applyPreset(p, {})); renderGenerate(); build({ immediate: true }); scene.frameAll(); return `regen ${p}`; }
  act(b.dataset.act);
}));
function act(a) {
  switch (a) {
    case 'new': pushUndo(); state.network = { roads: [], nodes: [], water: [], areas: [], blocks: [], massing: [], meta: {} }; state.sel = null; scene.setSelection(null); measureLengths(); build(); renderLegend(); toast('Empty scene · press R to draw', 'info'); break;
    case 'sample': pushUndo(); generate('river-capital'); break;
    case 'regen': generate(state.preset, { keepSeed: true }); break;
    case 'rebuild': measureLengths(); build(); toast(`Rebuilt in ${state.lastBuildMs} ms`, 'info'); break;
    case 'save': saveLocal(); break;
    case 'load': loadLocal(); break;
    case 'import': $('#fileIn').click(); break;
    case 'export': download(`frontier-network-${state.params.seed}.json`, JSON.stringify(serialize(), null, state.network.roads.length < 40 ? 2 : 0), 'application/json'); break;
    case 'manifest': download(`frontier-roads-${state.params.seed}.csv`, manifestCSV(), 'text/csv'); break;
    case 'shot': capture(); break;
    case 'undo': undo(); break;
    case 'redo': redo(); break;
    case 'dup': { const r = selectedRoad(); r ? dupRoad(r) : toast('Select a road first', 'warn'); break; }
    case 'split': { const r = selectedRoad(); r ? splitRoad(r) : toast('Select a road first', 'warn'); break; }
    case 'join': joinEnds(); break;
    case 'del': deleteHit(state.sel ? { kind: state.sel.kind, id: state.sel.id, node: selectedNode(), road: selectedRoad() } : null); break;
    case 'validate': { const v = validateNetwork(viewNetwork()); sheet(`<h2>Network validation</h2><p class="muted">${v.problems.length ? `${v.problems.length} problem${v.problems.length > 1 ? 's' : ''} found` : 'No problems found in ' + v.stats.roads + ' corridors.'}</p>${v.problems.length ? `<div class="list">${v.problems.slice(0, 60).map(p => `<div class="li">${icon('alert', 'i sm')}<b>${esc(p)}</b></div>`).join('')}</div>` : ''}<div style="display:flex;gap:8px;justify-content:flex-end;margin-top:18px"><button class="btn primary" data-close>Close</button></div>`); break; }
    case 'view-lit': scene.setView('lit'); syncView(); break;
    case 'view-unlit': scene.setView('unlit'); syncView(); break;
    case 'view-id': scene.setView('id'); syncView(); break;
    case 'view-wire': scene.setView('wire'); syncView(); break;
    case 'frame-sel': scene.frameSelection(); break;
    case 'frame-all': scene.frameAll(); break;
    case 'top': planView(); break;
    case 'lobby': window.location.href = 'index.html'; break;
    case 'shortcuts': shortcuts(); break;
    case 'about': about(); break;
    case 'new-road': setTool('draw'); break;
    case 'toggle-layers': setTab('details'); scene.setLayer('underground', true); toast('Layer visibility is in the outliner — click the eye icons', 'info'); break;
    case 'expand-all': state.collapsed.clear(); renderTree(); break;
    case 'collapse-all': GROUPS.forEach(g => state.collapsed.add('g' + g.label)); renderTree(); break;
  }
}
$$('[data-act]').forEach(b => { if (b.closest('.menu') || b.closest('#sheet')) return; b.addEventListener('click', () => act(b.dataset.act)); });
$('#fileIn').addEventListener('change', async e => {
  const f = e.target.files?.[0]; if (!f) return;
  try {
    const t = await f.text();
    pushUndo();
    const o = JSON.parse(t);
    state.network = { roads: o.roads || [], nodes: o.nodes || [], water: o.water || [], areas: o.areas || [], blocks: o.blocks || [], massing: o.massing || [], meta: o.meta || {}, params: o.params || state.params };
    Object.assign(state.params, o.params || {});
    state.sel = null; measureLengths(); build(); renderLegend();
    toast(`Imported ${state.network.roads.length} corridors`);
  } catch (err) { toast('Import failed: ' + err.message, 'bad'); }
  e.target.value = '';
});

/* ------------------------------- persistence ------------------------------- */
const KEY = 'frontier.road.editor.v1';
function serialize() {
  const clean = o => JSON.parse(JSON.stringify(o, (k, v) => k.startsWith('_') || k === 'cut' || k === 'drops' || k === 'avoid' ? undefined : v));
  return {
    format: 'frontier.road/1', params: state.params, preset: state.preset,
    roads: state.network.roads.map(r => clean({ ...r, _len: undefined, stops: undefined })),
    nodes: state.network.nodes.map(clean), water: (state.network.water || []).map(clean),
    areas: (state.network.areas || []).map(clean), massing: (state.network.massing || []).map(clean),
    meta: state.network.meta || {}
  };
}
function saveLocal() {
  try {
    localStorage.setItem(KEY, JSON.stringify(serialize()));
    markClean('Saved locally');
    toast(`Scene saved · ${state.network.roads.length} roads · ${state.network.nodes.length} junctions`);
  } catch (e) { toast('Save failed (quota?)', 'bad'); }
}
function loadLocal() {
  const s = localStorage.getItem(KEY);
  if (!s) return toast('No saved scene in this browser', 'warn');
  pushUndo();
  const o = JSON.parse(s);
  Object.assign(state.network, o);
  Object.assign(state.params, o.params || {});
  state.preset = o.preset || state.preset;
  state.sel = null; measureLengths(); build(); renderLegend();
  markClean('Restored');
  toast('Scene restored');
}
function download(name, text, type) {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([text], { type }));
  a.download = name; a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 4000);
  toast(`Exported ${name}`);
}
function manifestCSV() {
  const rows = [['id', 'name', 'class', 'profile', 'length_m', 'corridor_m', 'lanes', 'bus_lanes', 'cycle_lanes', 'parking', 'speed_kmh', 'structure', 'deck_y', 'features', 'junctions']];
  for (const r of state.network.roads) {
    const s = r._section || solveSection(profileById(typeof r.profile === 'string' ? r.profile : r.profile.id));
    rows.push([r.id, roadLabel(r), r.cls, typeof r.profile === 'string' ? r.profile : r.profile.id, (r._len || 0).toFixed(1), s.total.toFixed(2),
      s.elements.filter(e => e.k === 'lane').length, s.elements.filter(e => e.k === 'bus').length, s.elements.filter(e => e.k === 'cycle').length,
      s.elements.filter(e => e.k === 'parking').length, s.speed || '', r.structure || '', (r.deckY || 0).toFixed(2),
      (r.bumps || []).length + (r.crosstables || []).length + (r.crossingsManual || []).length + (r.driveways || []).length,
      (r.nodeIds || []).join(' ')]);
  }
  return rows.map(r => r.map(c => /[",\n]/.test(String(c)) ? `"${String(c).replace(/"/g, '""')}"` : c).join(',')).join('\n');
}
function capture() {
  const url = scene.screenshot();
  const a = document.createElement('a');
  a.href = url; a.download = `frontier-road-${Date.now()}.png`; a.click();
  toast('Viewport captured');
}
function shortcuts() {
  const K = [
    ['V', 'Select'], ['R', 'Draw road'], ['E', 'Edit alignment points'], ['H', 'Edit height profile'], ['T', 'Paint features'], ['M', 'Measure'],
    ['Del', 'Delete selection'], ['Enter', 'Finish drawing'], ['Esc', 'Cancel / deselect'], ['⇧A', 'Construct menu'],
    ['1 – 4', 'Lit · unlit · mat-ID · wireframe'], ['G / X', 'Grid · x-ray'], ['F', 'Frame selection'], ['Home', 'Frame network'],
    ['N', 'Advance time of day'], ['[ ]', 'Next / previous corridor (drive)'], ['Ctrl Z / ⇧Z', 'Undo / redo'], ['Ctrl S', 'Save scene'],
    ['Ctrl B', 'Rebuild meshes'], ['Ctrl P', 'Capture'], ['/', 'Filter outliner'], ['W A S D', 'Fly · ⇧ sprint · ⌥ slow'], ['Q E', 'Down · up'], ['RMB drag', 'Look around'], ['Wheel', 'Speed / zoom']
  ];
  sheet(`<h2>Keyboard &amp; mouse</h2><p class="muted">Unreal-style camera: hold the right mouse button to look, WASD to move. Every tool has a single-key shortcut.</p>
    <div class="kbd-grid">${K.map(([k, v]) => `<div class="kv"><span>${v}</span><kbd style="font:500 10px var(--mono);color:#cfcfcf;background:#222;border:1px solid #343434;border-radius:5px;padding:3px 6px">${k}</kbd></div>`).join('')}</div>
    <div style="display:flex;gap:8px;justify-content:flex-end;margin-top:20px"><button class="btn primary" data-close>Got it</button></div>`);
}
function about() {
  const counts = PROFILES.length;
  sheet(`<h2>Frontier · Road Editor prototype</h2>
    <p class="muted">A browser prototype of the engine's road authoring tools, modelled on the Project&nbsp;Zero <span style="color:#bfbfbf">FrontierEditor</span> chrome: outliner on the left, inspector on the right, viewport in the middle, semantic accents only on icons.</p>
    <div class="grid3" style="margin:16px 0 6px">
      <div>${metric(counts, 'profiles', 'sm')}<p class="muted">cross-sections from shared-space lanes to 2×3 freeways</p></div>
      <div>${metric(Object.keys(PROP_SPECS).length, 'props', 'sm')}<p class="muted">kerbs, bollards, rails, signals, shelters, works</p></div>
      <div>${metric((scene.stats?.instances || 0).toLocaleString(), 'instances', 'sm')}<p class="muted">draw calls ${scene.stats?.buckets ?? 0}+ · ${state.network.roads.length} corridors</p></div>
    </div>
    <p class="muted">Everything is generated procedurally at load — profiles, meshes, textures, markings, structures. Export the network as JSON or a road manifest to feed it to the engine's build step.</p>
    <div style="display:flex;gap:8px;justify-content:flex-end;margin-top:18px">
      <button class="btn ghost" data-close>Close</button>
      <button class="btn primary" data-act2="export">${icon('download', 'i sm')}Export network JSON</button></div>`);
  $('[data-act2]', $('#sheet')).addEventListener('click', () => { closeSheet(); act('export'); });
}

/* ------------------------------- generate + boot ------------------------------- */
function generate(preset = state.preset, { keepSeed = true, frame = true } = {}) {
  const t0 = performance.now();
  state.preset = preset;
  const params = applyPreset(preset, { ...state.params, ...(keepSeed ? {} : { seed: 1000 + Math.floor(Math.random() * 99000) }) });
  state.params = params;
  const net = generateCity(params);
  state.network = { ...net, massing: net.massing || [] };
  for (const r of state.network.roads) { try { prepareRoad(r); } catch (e) { } }
  state.sel = null; scene.setSelection(null);
  const stats = build();
  state.validateAfterBuild = true;
  renderLegend();
  if (frame) scene.frameAll();
  $('#bootMsg')?.classList.add('hide');
  toast(`Generated ${net.roads.length} corridors · ${net.nodes.length} junctions · ${Math.round(performance.now() - t0)} ms`);
  return stats;
}
/* paint features list in details when in paint mode */
const paintBar = () => {
  const feats = [['cushion', 'Speed cushions', 'cone'], ['table', 'Speed table', 'cone'], ['flat', 'Flat table', 'cone'], ['xstable', 'Raised crossing', 'bridge'], ['zebra', 'Zebra crossing', 'flag'], ['ladder', 'Ladder crossing', 'flag'], ['driveway-l', 'Driveway left', 'car'], ['driveway-r', 'Driveway right', 'car'], ['signal', 'Signals', 'activity'], ['roundabout', 'Roundabout', 'orbit'], ['station', 'Metro station', 'train']];
  const el = document.createElement('div');
  el.className = 'float';
  el.style.cssText = 'position:absolute;left:50%;transform:translateX(-50%);top:60px;padding:6px;display:none;flex-wrap:wrap;max-width:420px';
  for (const [v, label, ic] of feats) {
    const b = document.createElement('button');
    b.className = 'tool'; b.style.width = 'auto'; b.style.padding = '0 9px'; b.style.fontSize = '10.5px'; b.style.gap = '6px';
    b.innerHTML = icon(ic, 'i sm') + label;
    b.dataset.f = v;
    if (v === state.paint) b.setAttribute('aria-pressed', 'true');
    b.addEventListener('click', () => { state.paint = v; [...el.children].forEach(c => c.setAttribute('aria-pressed', c === b)); });
    el.appendChild(b);
  }
  $('#viewport').appendChild(el);
  el.id = 'paintBar';
  const obs = () => { el.style.display = state.tool === 'paint' ? 'flex' : 'none'; };
  state.paintObs = obs;
};
paintBar();
const _setTool = setTool;
setTool = t => { _setTool(t); state.paintObs?.(); };

/* menu actions defined after act() hoist safety */
generate(state.preset);
setTab('details');
setTool('select');
setTimeout(() => {
  const saved = localStorage.getItem(KEY);
  if (saved) toast('Press <kbd style="font:500 9.5px var(--mono)">Scene › Restore last save</kbd> to bring back your saved edits', 'info');
}, 1200);
if (!navigator.webdriver) console.info('%cFRONTIER ROAD EDITOR', 'font:600 11px/1 ui-monospace;color:#8fb6ff', '·', PROFILES.length, 'profiles ·', Object.keys(PROP_SPECS).length, 'prop prototypes');

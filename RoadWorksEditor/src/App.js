//============================================================================================================================================
//                                                                 APP.JS
//============================================================================================================================================
// RoadWorks Editor shell: document state, the outliner / inspector bindings, pointer tooling and the rebuild pump.

import * as THREE from 'three';
import { Viewport } from './Viewport.js';
import { buildNetwork, GROUP_NAMES } from './Network.js';
import { toObj } from './MeshSpec.js';
import { sampleSpline, closestOnPolyline } from './Spline.js';
import { ROAD_PRESETS, BRIDGE_TYPES, PIER_TYPES, RAILING_TYPES } from './Profiles.js';
import { BRIDGE_DEFAULTS } from './BridgeMesh.js';
import { GRAPH_DEFAULTS } from './Graph.js';

const $ = (id) => document.getElementById(id);
let uid = 0;
const nextId = (prefix) => `${prefix}${(++uid).toString().padStart(2, '0')}`;

// ── document ──────────────────────────────────────────────────────────────────────────────────────────────────────

function makeCorridor(name, points, extra = {}) {
  return {
    id: nextId('C'),
    name,
    preset: 'street',
    overrides: {},
    family: 'road',
    capMode: 'flat',
    closed: false,
    tension: 0,
    radiusBias: 0,
    visible: true,
    bridge: { ...BRIDGE_DEFAULTS },
    points: points.map(([x, y, z = 0]) => ({ x, y, z })),
    ...extra,
  };
}

function demoDocument() {
  uid = 0;
  const corridors = [
    makeCorridor('Harbour Avenue', [[-150, 0], [-60, 0], [0, 0], [70, 6], [150, 24]], { preset: 'avenue' }),
    makeCorridor('Mill Street', [[0, -120], [0, -40], [0, 0], [0, 55], [10, 120]], { preset: 'street' }),
    makeCorridor('Quay Lane', [[-150, -70], [-80, -58], [-20, -40], [0, -40], [60, -52], [130, -46]], { preset: 'narrow' }),
    makeCorridor('Dock Alley', [[-80, -58], [-78, 0]], { preset: 'alley' }),
    makeCorridor('Estuary Viaduct', [[-130, 95, 11], [-60, 86, 11], [10, 92, 11], [80, 104, 11], [150, 96, 11]], {
      preset: 'highway',
      family: 'bridge',
      capMode: 'flat',
      bridge: { ...BRIDGE_DEFAULTS, type: 'cablestay', pierType: 'hammerhead', pierSpacing: 42, railing: 'jersey', towerHeight: 26, deckThickness: 1.1 },
    }),
    makeCorridor('Mill Street Overpass', [[10, 120], [16, 150, 4], [20, 190, 11], [20, 230, 11]], {
      preset: 'street',
      family: 'bridge',
      bridge: { ...BRIDGE_DEFAULTS, type: 'arch', pierType: 'column', pierSpacing: 30, railing: 'parapet', archRise: 7 },
    }),
  ];
  return {
    name: 'Estuary crossing',
    corridors,
    settings: {
      sampleStep: 2.0,
      nodeMergeXY: 2.0,
      zMerge: 1.5,
      cornerScale: 1.0,
      markings: true,
      groundZ: 0,
    },
  };
}

const state = {
  doc: demoDocument(),
  selection: { corridorId: null, pointIndex: -1 },
  tool: 'select',
  draft: null,
  network: null,
  rebuildQueued: false,
  display: { overlay: true, markings: true, shadows: true, ground: true, mode: 'shaded' },
};

// ── boot ──────────────────────────────────────────────────────────────────────────────────────────────────────────

const canvas = $('SceneCanvas');
const viewport = new Viewport(canvas);

window.addEventListener('resize', () => viewport.resize());

// The boot sequence lives at the very bottom of this module: it touches `let` bindings declared further down, which
// are in their temporal dead zone until module evaluation reaches them.
function boot() {
  bindUi();
  rebuild(true);
  viewport.frame(networkBounds());
  loop();
}

function loop() {
  requestAnimationFrame(loop);
  viewport.render();
}

// ── rebuild pump ──────────────────────────────────────────────────────────────────────────────────────────────────

// Rebuilds are coalesced and rate-limited: dragging a control point re-solves the whole network, so we run at most
// one solve per `minInterval` and always finish with a trailing solve once the pointer settles.
let lastBuildAt = 0;
let trailingTimer = null;

function queueRebuild() {
  const minInterval = Math.max(60, (state.network?.stats.buildMs || 40) * 1.6);
  const since = performance.now() - lastBuildAt;
  clearTimeout(trailingTimer);
  if (since >= minInterval) {
    if (state.rebuildQueued) return;
    state.rebuildQueued = true;
    requestAnimationFrame(() => {
      state.rebuildQueued = false;
      rebuild();
    });
  } else {
    trailingTimer = setTimeout(() => rebuild(), minInterval - since);
  }
}

function rebuild(initial = false) {
  lastBuildAt = performance.now();
  setStatus('Building network', 'busy');
  let net;
  try {
    net = buildNetwork(visibleCorridors(), state.doc.settings);
  } catch (error) {
    console.error(error);
    showFailure(error.message || String(error));
    setStatus('Build failed', 'error');
    return;
  }
  hideFailure();
  state.network = net;
  viewport.setNetwork(net.groups);
  viewport.setMarkingsVisible(state.display.markings);
  viewport.setDisplayMode(state.display.mode);
  refreshOverlay();
  renderOutliner();
  renderDiagnostics();
  setStatus(`${net.stats.junctions} junction${net.stats.junctions === 1 ? '' : 's'} merged · ${net.stats.buildMs} ms`, 'ok');
  $('TriangleCount').textContent = `${net.stats.triangles.toLocaleString()} triangles`;
  if (initial) $('Loading').hidden = true;
}

function visibleCorridors() {
  const list = state.doc.corridors.filter((c) => c.visible !== false && c.points.length >= 2);
  if (state.draft && state.draft.points.length >= 2) list.push(state.draft);
  return list;
}

function refreshOverlay() {
  const corridors = [...state.doc.corridors];
  if (state.draft) corridors.push({ ...state.draft, draft: true });
  for (const c of corridors) {
    c.samples = c.points.length >= 2 ? sampleSpline(c.points, { closed: c.closed, tension: c.tension, step: state.doc.settings.sampleStep }) : [];
  }
  viewport.setOverlay(corridors, state.network?.graph, state.selection);
  const sel = selectedCorridor();
  if (sel && state.selection.pointIndex >= 0 && sel.points[state.selection.pointIndex]) {
    const p = sel.points[state.selection.pointIndex];
    viewport.gizmo.attach(new THREE.Vector3(p.x, p.y, p.z));
  } else {
    viewport.gizmo.detach();
  }
}

function selectedCorridor() {
  return state.doc.corridors.find((c) => c.id === state.selection.corridorId) || null;
}

function networkBounds() {
  const pts = state.doc.corridors.flatMap((c) => c.points);
  if (!pts.length) return null;
  const min = { x: Infinity, y: Infinity, z: Infinity };
  const max = { x: -Infinity, y: -Infinity, z: -Infinity };
  for (const p of pts) {
    min.x = Math.min(min.x, p.x);
    min.y = Math.min(min.y, p.y);
    min.z = Math.min(min.z, p.z);
    max.x = Math.max(max.x, p.x);
    max.y = Math.max(max.y, p.y);
    max.z = Math.max(max.z, p.z);
  }
  return { min, max };
}

// ── pointer tooling ───────────────────────────────────────────────────────────────────────────────────────────────

let drag = null;

canvas.addEventListener('pointerdown', (event) => {
  canvas.setPointerCapture(event.pointerId);
  const ray = viewport.updatePointer(event);

  if (event.button === 2 || event.button === 1 || event.altKey) {
    drag = { kind: 'pan', x: event.clientX, y: event.clientY };
    return;
  }

  if (state.tool === 'draw-road' || state.tool === 'draw-bridge') {
    const hit = viewport.pickGroundPoint(state.tool === 'draw-bridge' ? drawHeight() : 0);
    if (hit) {
      if (!state.draft) {
        state.draft = makeCorridor(
          state.tool === 'draw-bridge' ? `Bridge ${state.doc.corridors.length + 1}` : `Corridor ${state.doc.corridors.length + 1}`,
          [],
          state.tool === 'draw-bridge' ? { family: 'bridge', preset: 'street' } : {},
        );
      }
      state.draft.points.push(hit);
      refreshOverlay();
      if (state.draft.points.length >= 2) queueRebuild();
      setStatus(`Placing points · ${state.draft.points.length} placed · Enter to finish, Esc to cancel`, 'busy');
    }
    return;
  }

  const axis = viewport.gizmo.hitTest(ray);
  if (axis) {
    const corridor = selectedCorridor();
    const p = corridor?.points[state.selection.pointIndex];
    if (p) {
      viewport.gizmo.begin(axis, ray, new THREE.Vector3(p.x, p.y, p.z));
      drag = { kind: 'gizmo', corridor, index: state.selection.pointIndex };
      return;
    }
  }

  const handle = viewport.pickHandle();
  if (handle && state.doc.corridors.some((c) => c.id === handle.corridorId)) {
    select(handle.corridorId, handle.pointIndex);
    const corridor = selectedCorridor();
    const p = corridor.points[handle.pointIndex];
    viewport.gizmo.begin('xy', ray, new THREE.Vector3(p.x, p.y, p.z));
    drag = { kind: 'gizmo', corridor, index: handle.pointIndex };
    return;
  }

  // Click on a corridor ribbon inserts a control point there; otherwise orbit.
  const inserted = event.shiftKey ? tryInsertPoint() : null;
  if (!inserted) drag = { kind: 'orbit', x: event.clientX, y: event.clientY };
});

canvas.addEventListener('pointermove', (event) => {
  const ray = viewport.updatePointer(event);
  if (!drag) {
    const over = viewport.gizmo.hitTest(ray) || viewport.pickHandle();
    canvas.style.cursor = over ? 'grab' : state.tool.startsWith('draw') ? 'crosshair' : 'default';
    return;
  }
  if (drag.kind === 'orbit') {
    viewport.orbit(event.clientX - drag.x, event.clientY - drag.y);
    drag.x = event.clientX;
    drag.y = event.clientY;
  } else if (drag.kind === 'pan') {
    viewport.pan(event.clientX - drag.x, event.clientY - drag.y);
    drag.x = event.clientX;
    drag.y = event.clientY;
  } else if (drag.kind === 'gizmo') {
    const next = viewport.gizmo.update(ray);
    if (next) {
      const p = drag.corridor.points[drag.index];
      p.x = next.x;
      p.y = next.y;
      p.z = next.z;
      refreshOverlay();
      queueRebuild();
      renderInspectorValues();
    }
  }
});

canvas.addEventListener('pointerup', (event) => {
  canvas.releasePointerCapture(event.pointerId);
  if (drag?.kind === 'gizmo') viewport.gizmo.end();
  drag = null;
  canvas.style.cursor = 'default';
});

canvas.addEventListener('wheel', (event) => {
  event.preventDefault();
  viewport.zoom(event.deltaY);
}, { passive: false });

canvas.addEventListener('contextmenu', (e) => e.preventDefault());

canvas.addEventListener('dblclick', () => {
  if (state.tool.startsWith('draw')) finishDraft();
});

window.addEventListener('keydown', (event) => {
  if (event.target.matches('input, select, textarea')) return;
  if (event.key === 'f' || event.key === 'F') viewport.frame(networkBounds());
  if (event.key === 'Escape') cancelDraft();
  if (event.key === 'Enter') finishDraft();
  if (event.key === 'Delete' || event.key === 'Backspace') deleteSelectedPoint();
  if (event.key === '1') setTool('select');
  if (event.key === '2') setTool('draw-road');
  if (event.key === '3') setTool('draw-bridge');
});

function tryInsertPoint() {
  const hit = viewport.pickSurface();
  if (!hit) return false;
  let best = null;
  for (const corridor of state.doc.corridors) {
    if (corridor.points.length < 2) continue;
    const r = closestOnPolyline(corridor.points, hit);
    if (!best || r.distance < best.r.distance) best = { corridor, r };
  }
  if (!best || best.r.distance > 14) return false;
  best.corridor.points.splice(best.r.index + 1, 0, { ...best.r.point });
  select(best.corridor.id, best.r.index + 1);
  queueRebuild();
  setStatus('Control point inserted', 'ok');
  return true;
}

function deleteSelectedPoint() {
  const corridor = selectedCorridor();
  if (!corridor || state.selection.pointIndex < 0) return;
  if (corridor.points.length <= 2) {
    removeCorridor(corridor.id);
    return;
  }
  corridor.points.splice(state.selection.pointIndex, 1);
  state.selection.pointIndex = Math.max(0, state.selection.pointIndex - 1);
  queueRebuild();
}

function drawHeight() {
  const sel = selectedCorridor();
  return sel?.family === 'bridge' ? sel.points[0]?.z || 8 : 8;
}

function finishDraft() {
  if (!state.draft) return;
  if (state.draft.points.length >= 2) {
    state.doc.corridors.push(state.draft);
    select(state.draft.id, state.draft.points.length - 1);
  }
  state.draft = null;
  setTool('select');
  queueRebuild();
}

function cancelDraft() {
  if (state.draft) {
    state.draft = null;
    setTool('select');
    queueRebuild();
  }
}

function select(corridorId, pointIndex = -1) {
  state.selection = { corridorId, pointIndex };
  refreshOverlay();
  renderOutliner();
  renderInspector();
}

function removeCorridor(id) {
  state.doc.corridors = state.doc.corridors.filter((c) => c.id !== id);
  if (state.selection.corridorId === id) state.selection = { corridorId: null, pointIndex: -1 };
  queueRebuild();
  renderInspector();
}

function setTool(tool) {
  state.tool = tool;
  for (const [id, value] of [['ToolSelect', 'select'], ['ToolRoad', 'draw-road'], ['ToolBridge', 'draw-bridge']]) {
    $(id).classList.toggle('Active', state.tool === value);
  }
  canvas.style.cursor = tool.startsWith('draw') ? 'crosshair' : 'default';
  if (!tool.startsWith('draw')) state.draft = null;
  if (tool.startsWith('draw')) setStatus('Click on the ground to place control points · Enter to finish', 'busy');
}

// ── outliner ──────────────────────────────────────────────────────────────────────────────────────────────────────

function renderOutliner() {
  const list = $('CorridorList');
  list.innerHTML = '';
  for (const corridor of state.doc.corridors) {
    const row = document.createElement('div');
    row.className = 'OutlinerEntry CorridorRow' + (state.selection.corridorId === corridor.id ? ' Selected' : '');

    const icon = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    const use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
    use.setAttribute('href', corridor.family === 'bridge' ? '#BridgeIcon' : '#RoadIcon');
    icon.appendChild(use);

    const label = document.createElement('span');
    label.className = 'CorridorName';
    label.textContent = corridor.name;
    label.addEventListener('click', () => select(corridor.id, 0));

    const meta = document.createElement('small');
    meta.textContent = `${ROAD_PRESETS[corridor.preset]?.label || corridor.preset} · ${corridor.points.length} pts`;

    const vis = document.createElement('button');
    vis.className = 'Tile' + (corridor.visible === false ? ' Off' : '');
    vis.textContent = corridor.visible === false ? '○' : '●';
    vis.title = 'Toggle visibility';
    vis.addEventListener('click', (e) => {
      e.stopPropagation();
      corridor.visible = corridor.visible === false;
      queueRebuild();
    });

    row.append(icon, label, meta, vis);
    row.addEventListener('click', () => select(corridor.id, state.selection.corridorId === corridor.id ? state.selection.pointIndex : 0));
    list.appendChild(row);
  }

  const stats = state.network?.stats;
  $('CensusCorridors').textContent = state.doc.corridors.length;
  $('CensusJunctions').textContent = stats ? stats.junctions : 0;
  $('SelectionName').textContent = selectedCorridor()?.name || 'Nothing selected';
  $('SelectionDetail').textContent =
    state.selection.pointIndex >= 0 && selectedCorridor()
      ? `Control point ${state.selection.pointIndex + 1} of ${selectedCorridor().points.length}`
      : 'Click a corridor to edit it · Shift-click a ribbon to insert a point';
}

// ── inspector ─────────────────────────────────────────────────────────────────────────────────────────────────────

function control({ label, value, min, max, step, onInput, format }) {
  const wrap = document.createElement('div');
  wrap.className = 'Property';
  const lab = document.createElement('label');
  lab.className = 'FieldLabel';
  lab.textContent = label;
  const out = document.createElement('output');
  out.textContent = format ? format(value) : value;
  lab.appendChild(out);
  const input = document.createElement('input');
  input.type = 'range';
  input.min = min;
  input.max = max;
  input.step = step;
  input.value = value;
  const paint = () => {
    const pct = ((input.value - min) / (max - min)) * 100;
    input.style.setProperty('--Fill', `${pct}%`);
  };
  paint();
  input.addEventListener('input', () => {
    const v = parseFloat(input.value);
    out.textContent = format ? format(v) : v;
    paint();
    onInput(v);
  });
  wrap.append(lab, input);
  return wrap;
}

function selectField({ label, value, options, onChange }) {
  const wrap = document.createElement('div');
  wrap.className = 'Property';
  const lab = document.createElement('label');
  lab.className = 'FieldLabel';
  lab.textContent = label;
  const sel = document.createElement('select');
  for (const [key, text] of options) {
    const opt = document.createElement('option');
    opt.value = key;
    opt.textContent = text;
    if (key === value) opt.selected = true;
    sel.appendChild(opt);
  }
  sel.addEventListener('change', () => onChange(sel.value));
  wrap.append(lab, sel);
  return wrap;
}

function checkField({ label, value, onChange, note }) {
  const lab = document.createElement('label');
  lab.className = 'Check';
  const input = document.createElement('input');
  input.type = 'checkbox';
  input.checked = !!value;
  input.addEventListener('change', () => onChange(input.checked));
  lab.append(input, document.createTextNode(` ${label}`));
  if (note) {
    const s = document.createElement('span');
    s.className = 'Subtle';
    s.textContent = note;
    lab.appendChild(s);
  }
  return lab;
}

function section(title, open = true) {
  const d = document.createElement('details');
  d.open = open;
  const s = document.createElement('summary');
  s.textContent = title;
  d.appendChild(s);
  const body = document.createElement('div');
  body.className = 'ControlGroup';
  d.appendChild(body);
  return { element: d, body };
}

function renderInspector() {
  const host = $('InspectorBody');
  host.innerHTML = '';
  const corridor = selectedCorridor();

  const header = document.createElement('div');
  header.className = 'ObjectHeader';
  const icon = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  const use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
  use.setAttribute('href', corridor?.family === 'bridge' ? '#BridgeIcon' : '#RoadIcon');
  icon.appendChild(use);
  const text = document.createElement('div');
  text.innerHTML = `<b>${corridor ? corridor.name : 'Network'}</b><small>${corridor ? (corridor.family === 'bridge' ? 'Bridge corridor' : 'Road corridor') : 'No corridor selected'}</small>`;
  header.append(icon, text);
  host.appendChild(header);
  $('InspectorTitle').textContent = corridor ? corridor.name : 'Network';

  if (corridor) {
    const mark = () => queueRebuild();

    const corr = section('Corridor');
    const nameInput = document.createElement('input');
    nameInput.type = 'text';
    nameInput.value = corridor.name;
    nameInput.className = 'TextField';
    nameInput.addEventListener('input', () => {
      corridor.name = nameInput.value;
      renderOutliner();
      $('InspectorTitle').textContent = corridor.name;
    });
    corr.body.appendChild(labelled('Name', nameInput));
    corr.body.appendChild(
      selectField({
        label: 'Family',
        value: corridor.family,
        options: [['road', 'Road'], ['bridge', 'Bridge']],
        onChange: (v) => {
          corridor.family = v;
          renderInspector();
          mark();
        },
      }),
    );
    corr.body.appendChild(
      selectField({
        label: 'Profile preset',
        value: corridor.preset,
        options: Object.entries(ROAD_PRESETS).map(([k, v]) => [k, v.label]),
        onChange: (v) => {
          corridor.preset = v;
          corridor.overrides = {};
          renderInspector();
          mark();
        },
      }),
    );
    corr.body.appendChild(
      selectField({
        label: 'End caps',
        value: corridor.capMode,
        options: [['flat', 'Flat'], ['none', 'Open']],
        onChange: (v) => {
          corridor.capMode = v;
          mark();
        },
      }),
    );
    corr.body.appendChild(control({ label: 'Curve tension', value: corridor.tension, min: 0, max: 0.9, step: 0.05, format: (v) => v.toFixed(2), onInput: (v) => { corridor.tension = v; refreshOverlay(); mark(); } }));
    corr.body.appendChild(control({ label: 'Junction radius bias', value: corridor.radiusBias, min: 0, max: 12, step: 0.5, format: (v) => `${v.toFixed(1)} m`, onInput: (v) => { corridor.radiusBias = v; mark(); } }));
    corr.body.appendChild(checkField({ label: 'Closed loop', value: corridor.closed, onChange: (v) => { corridor.closed = v; refreshOverlay(); mark(); } }));
    host.appendChild(corr.element);

    const profile = { ...ROAD_PRESETS[corridor.preset], ...corridor.overrides };
    const xs = section('Cross-section');
    xs.body.appendChild(num('Road width', profile.roadWidth, 3, 32, 0.5, 'm', (v) => (corridor.overrides.roadWidth = v), mark));
    xs.body.appendChild(num('Lanes', profile.lanes, 1, 8, 1, '', (v) => (corridor.overrides.lanes = v), mark));
    xs.body.appendChild(num('Pavement left', profile.pavementLeft, 0, 8, 0.1, 'm', (v) => (corridor.overrides.pavementLeft = v), mark));
    xs.body.appendChild(num('Pavement right', profile.pavementRight, 0, 8, 0.1, 'm', (v) => (corridor.overrides.pavementRight = v), mark));
    xs.body.appendChild(num('Curb height', profile.curbHeight, 0, 0.6, 0.01, 'm', (v) => (corridor.overrides.curbHeight = v), mark));
    xs.body.appendChild(num('Curb width', profile.curbWidth, 0.05, 1.0, 0.01, 'm', (v) => (corridor.overrides.curbWidth = v), mark));
    xs.body.appendChild(num('Camber', corridor.overrides.crown ?? 0.02, 0, 0.08, 0.005, '', (v) => (corridor.overrides.crown = v), mark));
    host.appendChild(xs.element);

    if (corridor.family === 'bridge') {
      const b = corridor.bridge || (corridor.bridge = { ...BRIDGE_DEFAULTS });
      const deck = section('Bridge · deck');
      deck.body.appendChild(selectField({ label: 'Structure type', value: b.type, options: Object.entries(BRIDGE_TYPES).map(([k, v]) => [k, v.label]), onChange: (v) => { b.type = v; renderInspector(); mark(); } }));
      deck.body.appendChild(num('Deck thickness', b.deckThickness, 0.3, 2.5, 0.05, 'm', (v) => (b.deckThickness = v), mark));
      deck.body.appendChild(num('Soffit inset', b.soffitInset, 0, 3, 0.1, 'm', (v) => (b.soffitInset = v), mark));
      deck.body.appendChild(selectField({ label: 'Railing', value: b.railing, options: Object.entries(RAILING_TYPES), onChange: (v) => { b.railing = v; mark(); } }));
      deck.body.appendChild(num('Railing height', b.railHeight, 0.4, 1.8, 0.05, 'm', (v) => (b.railHeight = v), mark));
      host.appendChild(deck.element);

      const sup = section('Bridge · superstructure');
      if (b.type === 'beam' || b.type === 'box') {
        sup.body.appendChild(num('Girder count', b.girderCount, 1, 10, 1, '', (v) => (b.girderCount = v), mark));
        sup.body.appendChild(num('Girder depth', b.girderDepth, 0.4, 4, 0.1, 'm', (v) => (b.girderDepth = v), mark));
      }
      if (b.type === 'arch') sup.body.appendChild(num('Arch rise', b.archRise, 1, 30, 0.5, 'm', (v) => (b.archRise = v), mark));
      if (b.type === 'truss') sup.body.appendChild(num('Truss height', b.trussHeight, 1.5, 12, 0.25, 'm', (v) => (b.trussHeight = v), mark));
      if (b.type === 'suspension' || b.type === 'cablestay') {
        sup.body.appendChild(num('Tower height', b.towerHeight, 6, 70, 1, 'm', (v) => (b.towerHeight = v), mark));
        sup.body.appendChild(num('Cables per fan', b.cableCount, 2, 16, 1, '', (v) => (b.cableCount = v), mark));
      }
      if (!sup.body.children.length) {
        const p = document.createElement('p');
        p.className = 'Small';
        p.textContent = 'This structure type has no extra superstructure parameters.';
        sup.body.appendChild(p);
      }
      host.appendChild(sup.element);

      const sub = section('Bridge · substructure');
      sub.body.appendChild(selectField({ label: 'Pier family', value: b.pierType, options: Object.entries(PIER_TYPES), onChange: (v) => { b.pierType = v; mark(); } }));
      sub.body.appendChild(num('Pier spacing', b.pierSpacing, 10, 90, 1, 'm', (v) => (b.pierSpacing = v), mark));
      sub.body.appendChild(num('Pier width', b.pierWidth, 0.5, 4, 0.1, 'm', (v) => (b.pierWidth = v), mark));
      sub.body.appendChild(checkField({ label: 'Abutments', value: b.abutments, onChange: (v) => { b.abutments = v; mark(); } }));
      host.appendChild(sub.element);
    }

    const pts = section('Control points', false);
    const list = document.createElement('div');
    list.className = 'PointList';
    corridor.points.forEach((p, i) => {
      const row = document.createElement('button');
      row.className = 'PointRow' + (state.selection.pointIndex === i ? ' Active' : '');
      row.innerHTML = `<span class="Number">${(i + 1).toString().padStart(2, '0')}</span><span>${p.x.toFixed(1)}, ${p.y.toFixed(1)}</span><b>${p.z.toFixed(2)} m</b>`;
      row.addEventListener('click', () => select(corridor.id, i));
      list.appendChild(row);
    });
    pts.body.appendChild(list);
    if (state.selection.pointIndex >= 0) {
      const p = corridor.points[state.selection.pointIndex];
      pts.body.appendChild(num('Point elevation', p.z, -10, 60, 0.25, 'm', (v) => (p.z = v), () => { refreshOverlay(); queueRebuild(); }));
    }
    const del = document.createElement('button');
    del.textContent = 'Delete corridor';
    del.addEventListener('click', () => removeCorridor(corridor.id));
    pts.body.appendChild(del);
    host.appendChild(pts.element);
  }

  const net = section('Network solver');
  const s = state.doc.settings;
  net.body.appendChild(num('Sample step', s.sampleStep, 0.5, 6, 0.25, 'm', (v) => (s.sampleStep = v), queueRebuild));
  net.body.appendChild(num('Node merge radius', s.nodeMergeXY, 0.5, 8, 0.25, 'm', (v) => (s.nodeMergeXY = v), queueRebuild));
  net.body.appendChild(num('Grade separation', s.zMerge, 0.5, 8, 0.25, 'm', (v) => (s.zMerge = v), queueRebuild));
  net.body.appendChild(num('Junction radius scale', s.cornerScale, 0.4, 2.5, 0.05, '×', (v) => (s.cornerScale = v), queueRebuild));
  net.body.appendChild(num('Ground level', s.groundZ, -20, 20, 0.5, 'm', (v) => (s.groundZ = v), queueRebuild));
  net.body.appendChild(checkField({ label: 'Lane markings', value: s.markings, onChange: (v) => { s.markings = v; queueRebuild(); } }));
  host.appendChild(net.element);

  const disp = section('Display');
  disp.body.appendChild(control({ label: 'Light azimuth', value: viewport.sunAzimuth, min: -180, max: 180, step: 1, format: (v) => `${v}°`, onInput: (v) => viewport.setSunAzimuth(v) }));
  disp.body.appendChild(checkField({ label: 'Cast shadows', value: state.display.shadows, onChange: (v) => { state.display.shadows = v; viewport.setShadows(v); } }));
  disp.body.appendChild(checkField({ label: 'Ground plane', value: state.display.ground, onChange: (v) => { state.display.ground = v; viewport.setGroundVisible(v); } }));
  disp.body.appendChild(checkField({ label: 'Editing overlay', value: state.display.overlay, onChange: (v) => { state.display.overlay = v; viewport.setOverlayVisible(v); } }));
  disp.body.appendChild(checkField({ label: 'Show markings', value: state.display.markings, onChange: (v) => { state.display.markings = v; viewport.setMarkingsVisible(v); } }));
  host.appendChild(disp.element);

  const diag = section('Diagnostics');
  const metrics = document.createElement('div');
  metrics.className = 'Metrics';
  metrics.id = 'Metrics';
  diag.body.appendChild(metrics);
  const warn = document.createElement('p');
  warn.className = 'Small';
  warn.id = 'WarningNote';
  diag.body.appendChild(warn);
  host.appendChild(diag.element);
  renderDiagnostics();
}

function labelled(label, input) {
  const wrap = document.createElement('div');
  wrap.className = 'Property';
  const lab = document.createElement('label');
  lab.className = 'FieldLabel';
  lab.textContent = label;
  wrap.append(lab, input);
  return wrap;
}

function num(label, value, min, max, step, unit, apply, after) {
  return control({
    label,
    value,
    min,
    max,
    step,
    format: (v) => `${step < 1 ? v.toFixed(2) : v} ${unit}`.trim(),
    onInput: (v) => {
      apply(v);
      after?.();
    },
  });
}

function renderInspectorValues() {
  const corridor = selectedCorridor();
  if (!corridor) return;
  const detail = $('SelectionDetail');
  if (state.selection.pointIndex >= 0) {
    const p = corridor.points[state.selection.pointIndex];
    detail.textContent = `Point ${state.selection.pointIndex + 1} · ${p.x.toFixed(1)}, ${p.y.toFixed(1)}, ${p.z.toFixed(2)} m`;
  }
}

function renderDiagnostics() {
  const metrics = $('Metrics');
  if (!metrics || !state.network) return;
  const s = state.network.stats;
  metrics.innerHTML = '';
  const rows = [
    ['Corridors', s.corridors],
    ['Graph nodes', s.nodes],
    ['Corridor edges', s.edges],
    ['Merged junctions', s.junctions],
    ['Grade separations', s.gradeSeparations],
    ['Triangles', s.triangles.toLocaleString()],
    ['Build time', `${s.buildMs} ms`],
  ];
  for (const [k, v] of rows) {
    const a = document.createElement('span');
    a.textContent = k;
    const b = document.createElement('b');
    b.textContent = v;
    metrics.append(a, b);
  }
  const warn = $('WarningNote');
  if (warn) warn.textContent = s.warnings.length ? s.warnings.slice(0, 3).join(' · ') : '';
}

// ── chrome ────────────────────────────────────────────────────────────────────────────────────────────────────────

function setStatus(message, kind = 'ok') {
  $('Status').textContent = message;
  const dot = $('StatusDot');
  dot.style.background = kind === 'error' ? '#c2706b' : kind === 'busy' ? '#d6a665' : '#88a483';
}

function showFailure(message) {
  const el = $('Failure');
  el.textContent = `Build error — ${message}`;
  el.hidden = false;
}

function hideFailure() {
  $('Failure').hidden = true;
}

function bindUi() {
  $('ToolSelect').addEventListener('click', () => setTool('select'));
  $('ToolRoad').addEventListener('click', () => setTool('draw-road'));
  $('ToolBridge').addEventListener('click', () => setTool('draw-bridge'));

  $('ViewShaded').addEventListener('click', () => setMode('shaded'));
  $('ViewWire').addEventListener('click', () => setMode('wireframe'));
  $('ViewSurfaces').addEventListener('click', () => setMode('surfaces'));
  $('Frame').addEventListener('click', () => viewport.frame(networkBounds()));
  $('ViewTop').addEventListener('click', () => viewport.setView('top'));
  $('ViewIso').addEventListener('click', () => viewport.setView('iso'));

  $('AddRoad').addEventListener('click', () => setTool('draw-road'));
  $('AddBridge').addEventListener('click', () => setTool('draw-bridge'));

  $('Rebuild').addEventListener('click', () => rebuild());
  $('ExportObj').addEventListener('click', exportObj);
  $('SaveDoc').addEventListener('click', saveDocument);
  $('OpenDoc').addEventListener('click', () => $('DocFile').click());
  $('DocFile').addEventListener('change', loadDocument);
  $('NewDoc').addEventListener('click', () => {
    state.doc = demoDocument();
    state.selection = { corridorId: null, pointIndex: -1 };
    $('DocumentName').value = state.doc.name;
    rebuild();
    renderInspector();
    viewport.frame(networkBounds());
  });
  $('DocumentName').value = state.doc.name;
  $('DocumentName').addEventListener('input', (e) => (state.doc.name = e.target.value));

  renderInspector();
  setTool('select');
}

function setMode(mode) {
  state.display.mode = mode;
  viewport.setDisplayMode(mode);
  for (const [id, value] of [['ViewShaded', 'shaded'], ['ViewWire', 'wireframe'], ['ViewSurfaces', 'surfaces']]) {
    $(id).classList.toggle('Active', mode === value);
  }
}

function exportObj() {
  if (!state.network) return;
  const groups = GROUP_NAMES.map((name) => ({ name, spec: state.network.groups[name] }));
  download(`${slug(state.doc.name)}.obj`, toObj(groups), 'text/plain');
  setStatus('OBJ exported (Y-up, metres)', 'ok');
}

function saveDocument() {
  const payload = JSON.stringify({ version: 1, name: state.doc.name, settings: state.doc.settings, corridors: state.doc.corridors.map(stripRuntime) }, null, 2);
  download(`${slug(state.doc.name)}.roadworks.json`, payload, 'application/json');
  setStatus('Document saved', 'ok');
}

function stripRuntime(corridor) {
  const { samples, ...rest } = corridor;
  void samples;
  return rest;
}

async function loadDocument(event) {
  const file = event.target.files?.[0];
  if (!file) return;
  try {
    const data = JSON.parse(await file.text());
    state.doc = {
      name: data.name || 'Untitled network',
      settings: { ...GRAPH_DEFAULTS, ...state.doc.settings, ...(data.settings || {}) },
      corridors: (data.corridors || []).map((c) => ({ ...makeCorridor(c.name || 'Corridor', []), ...c })),
    };
    state.selection = { corridorId: null, pointIndex: -1 };
    $('DocumentName').value = state.doc.name;
    rebuild();
    renderInspector();
    viewport.frame(networkBounds());
    setStatus(`Loaded ${file.name}`, 'ok');
  } catch (error) {
    showFailure(`Could not read ${file.name}: ${error.message}`);
  }
  event.target.value = '';
}

function download(filename, text, mime) {
  const blob = new Blob([text], { type: mime });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 2000);
}

const slug = (s) => (s || 'network').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');

// ── boot ──────────────────────────────────────────────────────────────────────────────────────────────────────────

// Any failure here used to leave the viewport sitting behind the loading spinner forever; surface it instead.
window.addEventListener('error', (event) => reportFatal(event.error || event.message));
window.addEventListener('unhandledrejection', (event) => reportFatal(event.reason));

function reportFatal(error) {
  console.error(error);
  const loading = $('Loading');
  if (loading) loading.hidden = true;
  const failure = $('Failure');
  if (failure) {
    failure.textContent = `Startup error — ${error?.message || error}`;
    failure.hidden = false;
  }
  const status = $('Status');
  if (status) status.textContent = 'Startup failed — see the browser console';
}

try {
  boot();
} catch (error) {
  reportFatal(error);
}

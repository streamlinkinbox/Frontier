//============================================================================================================================================
//                                                                 APP.JS
//============================================================================================================================================
// RoadWorks Editor shell: document state, the outliner / inspector bindings, pointer tooling and the rebuild pump.

import * as THREE from 'three';
import { Viewport } from './Viewport.js?v=4';
import { buildNetwork } from './Network.js?v=4';
import { toObj } from './MeshSpec.js?v=4';
import { sampleSpline, closestOnPolyline } from './Spline.js?v=4';
import { ROAD_PRESETS, BRIDGE_TYPES, PIER_TYPES, RAILING_TYPES } from './Profiles.js?v=4';
import { BRIDGE_DEFAULTS } from './BridgeMesh.js?v=4';
import { GRAPH_DEFAULTS } from './Graph.js?v=4';
import { ROADBED_DEFAULTS } from './Roadbed.js?v=4';
import { GUARDRAIL_DEFAULTS, GUARDRAIL_TYPES } from './Guardrail.js?v=4';
import { SIGNAGE_DEFAULTS } from './Signs.js?v=4';
import { PAVING_PATTERNS } from './Textures.js?v=4';

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
    paving: 'concrete',
    pavingScale: 1,
    roadbed: { ...ROADBED_DEFAULTS },
    guardrail: { ...GUARDRAIL_DEFAULTS },
    bridge: { ...BRIDGE_DEFAULTS },
    points: points.map(([x, y, z = 0]) => ({ x, y, z })),
    ...extra,
  };
}

// The default document is a showcase, not an empty canvas: the estuary network demonstrates merged topology,
// paving, roadbeds, guardrails and signage, and the gallery to the north carries one span of every bridge family
// so you can look at all eleven without drawing anything.

const BRIDGE_GALLERY = [
  ['Beam Viaduct', 'beam', 'street', { pierSpacing: 30 }],
  ['Box Girder Span', 'box', 'avenue', { pierSpacing: 38, girderDepth: 1.6 }],
  ['Slab Crossing', 'slab', 'street', { pierSpacing: 26 }],
  ['Cantilever Reach', 'cantilever', 'avenue', { pierSpacing: 46, girderDepth: 1.4, pierType: 'wall' }],
  ['Deck Arch', 'arch', 'street', { archRise: 7, pierType: 'column' }],
  ['Bowstring Arch', 'tiedarch', 'street', { archRise: 8, cableCount: 9 }],
  ['Stone Viaduct', 'masonry', 'narrow', { pierSpacing: 22, pierWidth: 2.0 }],
  ['Warren Truss', 'truss', 'street', { trussHeight: 4.6 }],
  ['Pratt Through Truss', 'throughtruss', 'street', { trussHeight: 5.2 }],
  ['Suspension Span', 'suspension', 'avenue', { towerHeight: 24, cableCount: 10 }],
  ['Cable-stay Span', 'cablestay', 'avenue', { towerHeight: 26, railing: 'jersey' }],
];

function bridgeGallery() {
  const length = 104;
  const colGap = 164;
  const rowGap = 82;
  const x0 = -248;
  const y0 = 300;
  return BRIDGE_GALLERY.map(([name, type, preset, overrides], i) => {
    const col = i % 4;
    const row = Math.floor(i / 4);
    const x = x0 + col * colGap;
    const y = y0 + row * rowGap;
    return makeCorridor(name, [[x, y, 12], [x + length, y, 12]], {
      preset,
      family: 'bridge',
      bridge: { ...BRIDGE_DEFAULTS, type, pierType: 'column', railing: 'parapet', ...overrides },
    });
  });
}

function demoDocument() {
  uid = 0;
  const corridors = [
    makeCorridor('Harbour Avenue', [[-150, 0], [-60, 0], [0, 0], [70, 6], [150, 24]], {
      preset: 'avenue',
      paving: 'flagstone',
      guardrail: { ...GUARDRAIL_DEFAULTS, type: 'pedestrian', when: 'fill', fillTrigger: 2.5, height: 1.1 },
    }),
    makeCorridor('Mill Street', [[0, -176], [0, -120], [0, -40], [0, 0], [0, 55], [10, 120]], { preset: 'street', paving: 'concrete' }),
    makeCorridor('Quay Lane', [[-150, -70], [-80, -58], [-20, -40], [0, -40], [60, -52], [130, -46]], { preset: 'narrow', paving: 'brick' }),
    makeCorridor('Dock Alley', [[-80, -58], [-78, 0]], { preset: 'alley', paving: 'cobble' }),
    makeCorridor('Harbour Expressway', [[-170, -176], [-60, -176], [60, -176], [170, -170]], {
      preset: 'highway',
      paving: 'asphaltWalk',
      guardrail: { ...GUARDRAIL_DEFAULTS, type: 'thrie', when: 'always', offset: 0.45 },
    }),
    makeCorridor('Quarry Ramp', [[0, 55], [45, 62, 1.8], [95, 70, 4.4], [150, 74, 6.0]], {
      preset: 'street',
      paving: 'granite',
      guardrail: { ...GUARDRAIL_DEFAULTS, type: 'wbeam', when: 'fill', fillTrigger: 1.2 },
    }),
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
    ...bridgeGallery(),
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
      signage: SIGNAGE_DEFAULTS.signage,
      stopBars: true,
      crosswalks: true,
    },
  };
}

// Selection is multi: `points` and `junctions` are the real selection, and the singular fields mirror the primary
// member so everything that only cares about "what is being edited" keeps working.
function emptySelection() {
  return { corridorId: null, pointIndex: -1, junctionId: null, junctionAt: null, points: [], junctions: [], edgeId: null, corridorIds: [] };
}

const state = {
  doc: demoDocument(),
  selection: emptySelection(),
  tool: 'select',
  draft: null,
  network: null,
  rebuildQueued: false,
  display: { overlay: true, markings: true, shadows: true, ground: true, textures: true, labels: true, mode: 'shaded' },
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
  // Debug handle: used by tools/boot-test.mjs, and handy from the browser console.
  window.__roadworks = {
    state, viewport, rebuild, select, selectCorridor, selectPoint, selectJunction, clearSelection,
    collectJunctionGrabs, collectGrabs, selectionCentroid, setTool, networkBounds, frameTarget, edgeAtPoint,
  };
}

let lastFrameAt = 0;

function loop(now = 0) {
  requestAnimationFrame(loop);
  const dt = lastFrameAt ? Math.min((now - lastFrameAt) / 1000, 0.1) : 0.016;
  lastFrameAt = now;
  const input = flyInput();
  if (input) viewport.fly(input, dt);
  viewport.render();
}

// Unchanged corridors are appended from their cached meshes instead of being rebuilt; see Network.edgeKey.
const meshCache = new Map();

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
    net = buildNetwork(visibleCorridors(), state.doc.settings, meshCache);
  } catch (error) {
    console.error(error);
    showFailure(error.message || String(error));
    setStatus('Build failed', 'error');
    return;
  }
  hideFailure();
  state.network = net;
  resolveJunctionSelection(net.graph);
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
  refreshHighlight();
  refreshLabels(corridors);

  const centre = selectionCentroid();
  if (centre) viewport.gizmo.attach(new THREE.Vector3(centre.x, centre.y, centre.z));
  else viewport.gizmo.detach();
}

// The stretch of road between two junctions is a graph *edge*, so highlighting it means painting that edge's own
// trimmed cross-sections — exactly the geometry the solver built, not an approximation of it.
function refreshHighlight() {
  const net = state.network;
  if (!net) {
    viewport.setHighlight({});
    return;
  }
  const sel = state.selection;
  const strips = [];
  const discs = [];

  const edgeIds = sel.edgeId
    ? [sel.edgeId]
    : [...net.sectionsByEdge.keys()].filter((id) => sel.corridorIds.includes(net.graph.edges.get(id)?.sourceId));

  for (const edgeId of edgeIds) {
    const sections = net.sectionsByEdge.get(edgeId);
    const edge = net.graph.edges.get(edgeId);
    if (!sections || !edge) continue;
    const left = [];
    const right = [];
    for (const section of sections) {
      const f = section.frame.left;
      const l = edge.profile.leftTotalHalf * section.miter;
      const r = edge.profile.rightTotalHalf * section.miter;
      left.push({ x: section.base.x + f.x * l, y: section.base.y + f.y * l, z: section.base.z });
      right.push({ x: section.base.x - f.x * r, y: section.base.y - f.y * r, z: section.base.z });
    }
    strips.push({ left, right });
  }

  for (const entry of sel.junctions) {
    const node = net.graph.nodes.get(entry.id);
    if (node) discs.push({ co: node.co, radius: Math.max(node.cornerRadius, 3) });
  }

  viewport.setHighlight({ strips, discs });
}

// Street names are rebuilt only when something they depend on changes — a sprite carries a canvas each, and the
// overlay refreshes on every frame of a drag.
let labelSignature = '';

function refreshLabels(corridors) {
  const graph = state.network?.graph;
  const parts = corridors.map((c) => `${c.id}:${c.name}:${c.visible === false ? 0 : 1}`);
  parts.push(`sel:${state.selection.corridorIds.join(',')}`);
  if (graph) for (const node of graph.nodes.values()) if (node.degree >= 3) parts.push(`n:${node.id}`);
  const signature = parts.join('|');
  if (signature === labelSignature) return;
  labelSignature = signature;

  const labels = [];
  for (const corridor of corridors) {
    if (corridor.visible === false || !corridor.samples?.length || corridor.draft) continue;
    const mid = corridor.samples[Math.floor(corridor.samples.length / 2)];
    labels.push({ text: corridor.name, position: mid, accent: state.selection.corridorIds.includes(corridor.id) });
  }
  if (graph) {
    for (const node of graph.nodes.values()) {
      if (node.degree < 3) continue;
      labels.push({ text: junctionName(node), position: node.co, kind: 'junction' });
    }
  }
  viewport.setLabels(labels);
}

// Which stretch of road is under this world point? Used by the select tool so clicking the road picks the edge
// between two junctions rather than the whole corridor.
function edgeAtPoint(p) {
  const net = state.network;
  if (!net) return null;
  let best = null;
  for (const [edgeId, sections] of net.sectionsByEdge) {
    const edge = net.graph.edges.get(edgeId);
    if (!edge) continue;
    const reach = Math.max(edge.profile.leftTotalHalf, edge.profile.rightTotalHalf) + 2.5;
    for (const section of sections) {
      const d = Math.hypot(section.base.x - p.x, section.base.y - p.y);
      if (d <= reach && Math.abs(section.base.z - p.z) < 6 && (!best || d < best.distance)) {
        best = { edgeId, edge, distance: d };
      }
    }
  }
  return best;
}

// ── selection ──// ── selection ─────────────────────────────────────────────────────────────────────────────────────────────────────

const pointKey = (corridorId, index) => `${corridorId}:${index}`;

// Mirrors the primary member of the multi-selection onto the singular fields the inspector and outliner read.
function syncPrimary() {
  const sel = state.selection;
  const point = sel.points[sel.points.length - 1] || null;
  const junction = sel.junctions[sel.junctions.length - 1] || null;
  sel.corridorId = point ? point.corridorId : sel.corridorIds[0] || null;
  sel.pointIndex = point ? point.index : -1;
  sel.junctionId = junction ? junction.id : null;
  sel.junctionAt = junction ? { ...junction.at } : null;
}

function afterSelectionChange() {
  syncPrimary();
  refreshOverlay();
  renderOutliner();
  renderInspector();
}

function clearSelection() {
  state.selection = emptySelection();
  afterSelectionChange();
}

// Selecting a control point. `additive` (Shift) toggles it in or out of the selection instead of replacing it.
function selectPoint(corridorId, index, additive = false) {
  const sel = state.selection;
  if (!additive) {
    sel.points = [];
    sel.junctions = [];
    sel.corridorIds = [];
    sel.edgeId = null;
  }
  const key = pointKey(corridorId, index);
  const at = sel.points.findIndex((p) => pointKey(p.corridorId, p.index) === key);
  if (at >= 0) sel.points.splice(at, 1);
  else sel.points.push({ corridorId, index });
  if (!sel.corridorIds.includes(corridorId)) sel.corridorIds.push(corridorId);
  afterSelectionChange();
}

// Selecting a corridor — and, when the click landed on the road itself, the single stretch between two junctions.
function selectCorridor(corridorId, { edgeId = null, pointIndex = -1, additive = false } = {}) {
  const sel = state.selection;
  if (!additive) {
    sel.points = [];
    sel.junctions = [];
    sel.corridorIds = [];
  }
  if (!sel.corridorIds.includes(corridorId)) sel.corridorIds.push(corridorId);
  sel.edgeId = edgeId;
  if (pointIndex >= 0) sel.points.push({ corridorId, index: pointIndex });
  afterSelectionChange();
  const corridor = state.doc.corridors.find((c) => c.id === corridorId);
  if (corridor) {
    setStatus(edgeId ? `${corridor.name} · stretch between junctions selected` : `${corridor.name} selected`, 'ok');
  }
}

function selectionCentroid() {
  const sel = state.selection;
  const acc = { x: 0, y: 0, z: 0 };
  let n = 0;
  for (const j of sel.junctions) {
    acc.x += j.at.x;
    acc.y += j.at.y;
    acc.z += j.at.z;
    n++;
  }
  for (const entry of sel.points) {
    const p = pointAt(entry);
    if (!p) continue;
    acc.x += p.x;
    acc.y += p.y;
    acc.z += p.z;
    n++;
  }
  if (!n) return null;
  return { x: acc.x / n, y: acc.y / n, z: acc.z / n };
}

function pointAt(entry) {
  const corridor = state.doc.corridors.find((c) => c.id === entry.corridorId);
  return corridor?.points[entry.index] || null;
}

// Every point that a drag of the current selection must move. Junctions expand into all of their arm points (and
// insert one where a corridor merely passes through), so an intersection still travels as one rigid body. Grabs
// hold point *references*, not indices, because expanding a junction can splice new points into a corridor.
function collectGrabs() {
  const grabs = [];
  const seen = new Set();
  const push = (point) => {
    if (!point || seen.has(point)) return;
    seen.add(point);
    grabs.push({ point, start: { ...point } });
  };
  for (const entry of state.selection.junctions) {
    const node = state.network?.graph.nodes.get(entry.id);
    if (!node) continue;
    for (const grab of collectJunctionGrabs(node)) push(grab.corridor.points[grab.index]);
  }
  for (const entry of state.selection.points) push(pointAt(entry));
  return grabs;
}

// ── junction selection ────────────────────────────────────────────────────────────────────────────────────────────
// Node ids are derived from rounded coordinates, so they change the moment a junction moves. The selection is
// therefore anchored to a *position*, and re-bound to the nearest node after every rebuild.

function selectedJunction() {
  const id = state.selection.junctionId;
  if (!id || !state.network) return null;
  return state.network.graph.nodes.get(id) || null;
}

function resolveJunctionSelection(graph) {
  const sel = state.selection;
  if (!sel.junctions.length) return;
  const nearest = (anchor) => {
    let best = null;
    let bestDist = Infinity;
    for (const node of graph.nodes.values()) {
      if (node.degree < 2) continue;
      const d = Math.hypot(node.co.x - anchor.x, node.co.y - anchor.y, node.co.z - anchor.z);
      if (d < bestDist) {
        bestDist = d;
        best = node;
      }
    }
    return bestDist <= 8 ? best : null;
  };
  const next = [];
  for (const entry of sel.junctions) {
    const node = nearest(entry.at);
    if (node && !next.some((e) => e.id === node.id)) next.push({ id: node.id, at: { ...node.co } });
  }
  sel.junctions = next;
  syncPrimary();
}

function selectJunction(node, additive = false) {
  const sel = state.selection;
  if (!additive) {
    sel.points = [];
    sel.junctions = [];
    sel.corridorIds = [];
    sel.edgeId = null;
  }
  const at = sel.junctions.findIndex((j) => j.id === node.id);
  if (at >= 0) sel.junctions.splice(at, 1);
  else sel.junctions.push({ id: node.id, at: { ...node.co } });
  afterSelectionChange();
  const count = sel.junctions.length;
  setStatus(
    count > 1
      ? `${count} intersections selected · drag the gizmo to move them together`
      : `${junctionName(node)} · ${node.degree} arms · drag to move the whole intersection`,
    'ok',
  );
}

// An intersection has no name of its own, so it borrows the names of the streets that meet there.
function junctionName(node) {
  const names = [];
  for (const id of node.sources || []) {
    const corridor = state.doc.corridors.find((c) => c.id === id);
    if (corridor && !names.includes(corridor.name)) names.push(corridor.name);
  }
  if (!names.length) return 'Junction';
  return names.slice(0, 3).join(' × ');
}

// Collects every control point that belongs to a junction so the intersection moves as one rigid body. Corridors
// that merely pass through — a crossing with no control point of its own — get one inserted at the node first,
// otherwise the junction would tear itself apart the moment it moved.
function collectJunctionGrabs(node) {
  const settings = state.doc.settings;
  const radius = Math.max(node.cornerRadius * 1.25, (settings.nodeMergeXY || 2) * 2, 5);
  const zTol = Math.max((settings.zMerge || 1.5) * 1.5, 2.5);
  const grabs = [];

  for (const corridor of state.doc.corridors) {
    if (corridor.points.length < 2 || corridor.visible === false) continue;
    const near = [];
    corridor.points.forEach((p, index) => {
      if (Math.hypot(p.x - node.co.x, p.y - node.co.y) <= radius && Math.abs(p.z - node.co.z) <= zTol) near.push(index);
    });
    if (near.length) {
      for (const index of near) grabs.push({ corridor, index, start: { ...corridor.points[index] } });
      continue;
    }
    if (!node.sources.has(corridor.id)) continue;
    const hit = closestOnPolyline(corridor.points, node.co);
    if (!hit || hit.distance > radius + 10) continue;
    const inserted = { x: node.co.x, y: node.co.y, z: hit.point.z };
    corridor.points.splice(hit.index + 1, 0, inserted);
    grabs.push({ corridor, index: hit.index + 1, start: { ...inserted } });
  }
  return grabs;
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

// F frames the selection when there is one, the whole network otherwise — the standard DCC behaviour, and the
// quickest way to get from the bridge gallery back to the city.
function frameTarget() {
  const sel = state.selection;
  const pts = [];
  for (const entry of sel.points) {
    const p = pointAt(entry);
    if (p) pts.push(p);
  }
  for (const entry of sel.junctions) pts.push(entry.at);
  if (!pts.length && sel.corridorIds.length) {
    for (const id of sel.corridorIds) {
      const corridor = state.doc.corridors.find((c) => c.id === id);
      if (corridor) pts.push(...corridor.points);
    }
  }
  if (pts.length < 1) return networkBounds();
  const min = { x: Infinity, y: Infinity, z: Infinity };
  const max = { x: -Infinity, y: -Infinity, z: -Infinity };
  for (const p of pts) {
    min.x = Math.min(min.x, p.x - 12);
    min.y = Math.min(min.y, p.y - 12);
    min.z = Math.min(min.z, p.z - 6);
    max.x = Math.max(max.x, p.x + 12);
    max.y = Math.max(max.y, p.y + 12);
    max.z = Math.max(max.z, p.z + 6);
  }
  return { min, max };
}

// ── pointer tooling ───────────────────────────────────────────────────────────────────────────────────────────────

let drag = null;

canvas.addEventListener('pointerdown', (event) => {
  canvas.setPointerCapture(event.pointerId);
  const ray = viewport.updatePointer(event);

  // Unreal-style navigation: right button looks (WASD flies while it is held), middle / Alt pans.
  if (event.button === 2) {
    drag = { kind: 'fly', x: event.clientX, y: event.clientY };
    canvas.style.cursor = 'none';
    setStatus(`Flying · WASD + Q/E · ${Math.round(viewport.flySpeed)} m/s (wheel to trim)`, 'busy');
    return;
  }
  if (event.button === 1 || event.altKey || held.has(' ')) {
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

  // The gizmo always wins: it is drawn on top of everything and moves the whole current selection.
  const axis = viewport.gizmo.hitTest(ray);
  if (axis && beginMove(axis, ray)) return;

  const additive = event.shiftKey;

  // Junction mode: only intersections are pickable, so an arm's control point can never be grabbed by accident.
  if (state.tool === 'junction') {
    const hub = viewport.pickJunction();
    const node = hub && state.network?.graph.nodes.get(hub.junctionId);
    if (node) {
      selectJunction(node, additive);
      beginMove('xy', ray);
      return;
    }
    if (additive) return startMarquee(event, 'junctions');
    if (!additive) clearSelection();
    drag = { kind: 'orbit', x: event.clientX, y: event.clientY };
    return;
  }

  // Point mode: control points only.
  if (state.tool === 'point') {
    const handle = viewport.pickHandle();
    if (handle && state.doc.corridors.some((c) => c.id === handle.corridorId)) {
      selectPoint(handle.corridorId, handle.pointIndex, additive);
      beginMove('xy', ray);
      return;
    }
    if (additive) return startMarquee(event, 'points');
    clearSelection();
    drag = { kind: 'orbit', x: event.clientX, y: event.clientY };
    return;
  }

  // ── select mode ──
  // A junction hub outranks the individual control points sitting inside it: clicking the intersection should grab
  // the intersection, not one arm of it.
  const hub = viewport.pickJunction();
  if (hub) {
    const node = state.network?.graph.nodes.get(hub.junctionId);
    if (node) {
      selectJunction(node, additive);
      beginMove('xy', ray);
      return;
    }
  }

  const handle = viewport.pickHandle();
  if (handle && state.doc.corridors.some((c) => c.id === handle.corridorId)) {
    selectPoint(handle.corridorId, handle.pointIndex, additive);
    beginMove('xy', ray);
    return;
  }

  // Shift-click on a ribbon inserts a control point; a plain click selects the stretch of road that was hit.
  if (additive && tryInsertPoint()) return;
  const surface = viewport.pickSurface();
  const edge = surface ? edgeAtPoint(surface) : null;
  if (edge) {
    selectCorridor(edge.edge.sourceId, { edgeId: edge.edgeId });
    return;
  }
  clearSelection();
  drag = { kind: 'orbit', x: event.clientX, y: event.clientY };
});

// Starts a transform of the whole selection. Returns false when there is nothing to move.
function beginMove(axis, ray) {
  const origin = selectionCentroid();
  if (!origin) return false;
  const grabs = collectGrabs();
  if (!grabs.length) return false;
  viewport.gizmo.begin(axis, ray, new THREE.Vector3(origin.x, origin.y, origin.z));
  // Junction anchors have to travel with the drag: the solver renames nodes as they move, and the selection is
  // re-bound to the nearest node by position after every rebuild.
  const junctionStarts = state.selection.junctions.map((j) => ({ ...j.at }));
  drag = { kind: 'move', origin, grabs, junctionStarts };
  return true;
}

// Shift-drag in point / junction mode: a screen rectangle that adds everything inside it to the selection.
function startMarquee(event, kind) {
  const rect = canvas.getBoundingClientRect();
  drag = { kind: 'marquee', pick: kind, x0: event.clientX, y0: event.clientY, x: event.clientX, y: event.clientY, rect };
  paintMarquee();
}

function paintMarquee() {
  const el = $('Marquee');
  if (!el || !drag || drag.kind !== 'marquee') return;
  const left = Math.min(drag.x0, drag.x) - drag.rect.left;
  const top = Math.min(drag.y0, drag.y) - drag.rect.top;
  el.style.left = `${left}px`;
  el.style.top = `${top}px`;
  el.style.width = `${Math.abs(drag.x - drag.x0)}px`;
  el.style.height = `${Math.abs(drag.y - drag.y0)}px`;
  el.hidden = false;
}

function finishMarquee() {
  const el = $('Marquee');
  if (el) el.hidden = true;
  if (!drag || drag.kind !== 'marquee') return;
  const { rect } = drag;
  const ndc = (cx, cy) => ({ x: ((cx - rect.left) / rect.width) * 2 - 1, y: -((cy - rect.top) / rect.height) * 2 + 1 });
  const a = ndc(drag.x0, drag.y0);
  const b = ndc(drag.x, drag.y);
  const box = { x0: Math.min(a.x, b.x), x1: Math.max(a.x, b.x), y0: Math.min(a.y, b.y), y1: Math.max(a.y, b.y) };
  if (box.x1 - box.x0 < 0.004 && box.y1 - box.y0 < 0.004) return;

  const sel = state.selection;
  if (drag.pick === 'junctions') {
    for (const hit of viewport.junctionsInRect(box)) {
      const node = state.network?.graph.nodes.get(hit.junctionId);
      if (node && !sel.junctions.some((j) => j.id === node.id)) sel.junctions.push({ id: node.id, at: { ...node.co } });
    }
    setStatus(`${sel.junctions.length} intersection${sel.junctions.length === 1 ? '' : 's'} selected`, 'ok');
  } else {
    for (const hit of viewport.handlesInRect(box)) {
      if (sel.points.some((p) => p.corridorId === hit.corridorId && p.index === hit.pointIndex)) continue;
      sel.points.push({ corridorId: hit.corridorId, index: hit.pointIndex });
      if (!sel.corridorIds.includes(hit.corridorId)) sel.corridorIds.push(hit.corridorId);
    }
    setStatus(`${sel.points.length} control point${sel.points.length === 1 ? '' : 's'} selected`, 'ok');
  }
  afterSelectionChange();
}

canvas.addEventListener('pointermove', (event) => {
  const ray = viewport.updatePointer(event);
  if (!drag) {
    const over = viewport.gizmo.hitTest(ray) || (state.tool !== 'point' && viewport.pickJunction()) || (state.tool !== 'junction' && viewport.pickHandle());
    canvas.style.cursor = over ? 'grab' : state.tool.startsWith('draw') ? 'crosshair' : 'default';
    return;
  }
  if (drag.kind === 'fly') {
    viewport.look(event.clientX - drag.x, event.clientY - drag.y);
    drag.x = event.clientX;
    drag.y = event.clientY;
  } else if (drag.kind === 'marquee') {
    drag.x = event.clientX;
    drag.y = event.clientY;
    paintMarquee();
  } else if (drag.kind === 'move') {
    const next = viewport.gizmo.update(ray);
    if (next) {
      const dx = next.x - drag.origin.x;
      const dy = next.y - drag.origin.y;
      const dz = next.z - drag.origin.z;
      for (const grab of drag.grabs) {
        grab.point.x = grab.start.x + dx;
        grab.point.y = grab.start.y + dy;
        grab.point.z = grab.start.z + dz;
      }
      state.selection.junctions.forEach((entry, i) => {
        const start = drag.junctionStarts[i];
        if (start) entry.at = { x: start.x + dx, y: start.y + dy, z: start.z + dz };
      });
      drag.moved = { dx, dy, dz };
      refreshOverlay();
      queueRebuild();
      renderInspectorValues();
      setStatus(`Moved ${Math.hypot(dx, dy).toFixed(1)} m across · ${dz >= 0 ? '+' : ''}${dz.toFixed(2)} m up · ${drag.grabs.length} points`, 'busy');
    }
  } else if (drag.kind === 'orbit') {
    viewport.orbit(event.clientX - drag.x, event.clientY - drag.y);
    drag.x = event.clientX;
    drag.y = event.clientY;
  } else if (drag.kind === 'pan') {
    viewport.pan(event.clientX - drag.x, event.clientY - drag.y);
    drag.x = event.clientX;
    drag.y = event.clientY;
  }
});

canvas.addEventListener('pointerup', (event) => {
  canvas.releasePointerCapture(event.pointerId);
  if (drag?.kind === 'move') {
    viewport.gizmo.end();
    // The junction anchors travel with the drag, so the selection survives the re-solve that follows.
    if (drag.moved) {
      syncPrimary();
      renderInspector();
      setStatus('Selection moved · network re-solved', 'ok');
    }
  }
  if (drag?.kind === 'marquee') finishMarquee();
  drag = null;
  canvas.style.cursor = 'default';
});

canvas.addEventListener('wheel', (event) => {
  event.preventDefault();
  // While the right button is held the wheel trims flight speed, exactly as it does in an Unreal viewport.
  if (drag?.kind === 'fly') {
    setStatus(`Flight speed ${Math.round(viewport.adjustFlySpeed(event.deltaY))} m/s`, 'busy');
    return;
  }
  viewport.zoom(event.deltaY);
}, { passive: false });

canvas.addEventListener('contextmenu', (e) => e.preventDefault());

canvas.addEventListener('dblclick', () => {
  if (state.tool.startsWith('draw')) finishDraft();
});

// WASD / QE are held keys, tracked so the render loop can integrate them at frame rate.
const held = new Set();
const typing = (event) => !!event.target?.matches?.('input, select, textarea');

window.addEventListener('keyup', (event) => held.delete(event.key.toLowerCase()));
window.addEventListener('blur', () => held.clear());

function flyInput() {
  if (held.size === 0) return null;
  const axis = (a, b) => (held.has(a) ? 1 : 0) - (held.has(b) ? 1 : 0);
  const forward = axis('w', 's');
  const strafe = axis('d', 'a');
  const rise = axis('e', 'q');
  if (!forward && !strafe && !rise) return null;
  const boost = held.has('shift') ? 3.2 : held.has('control') ? 0.25 : 1;
  return { forward, strafe, rise, boost };
}

window.addEventListener('keydown', (event) => {
  if (typing(event)) return;
  const key = event.key.toLowerCase();
  if (key.length === 1 && 'wasdqe '.includes(key)) held.add(key);
  if (event.key === 'Shift') held.add('shift');
  if (event.key === 'Control') held.add('control');
  if (event.key === 'f' || event.key === 'F') viewport.frame(frameTarget());
  if (event.key === 'Escape') {
    if (state.draft) cancelDraft();
    else clearSelection();
  }
  if (event.key === 'Enter') finishDraft();
  if (event.key === 'Delete' || event.key === 'Backspace') deleteSelection();
  if (event.key === '1') setTool('select');
  if (event.key === '2') setTool('point');
  if (event.key === '3') setTool('junction');
  if (event.key === '4') setTool('draw-road');
  if (event.key === '5') setTool('draw-bridge');
  if (key === 'l') setLabelsVisible(!state.display.labels);
  if (key === 'a' && (event.ctrlKey || event.metaKey)) {
    event.preventDefault();
    selectAll();
  }
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

// Delete removes every selected control point (and any corridor left with fewer than two).
function deleteSelection() {
  const sel = state.selection;
  if (!sel.points.length) {
    if (sel.corridorIds.length && !sel.junctions.length) {
      for (const id of [...sel.corridorIds]) removeCorridor(id);
      clearSelection();
    }
    return;
  }
  // Remove the highest indices first so the earlier ones stay valid.
  const byCorridor = new Map();
  for (const entry of sel.points) {
    if (!byCorridor.has(entry.corridorId)) byCorridor.set(entry.corridorId, []);
    byCorridor.get(entry.corridorId).push(entry.index);
  }
  for (const [corridorId, indices] of byCorridor) {
    const corridor = state.doc.corridors.find((c) => c.id === corridorId);
    if (!corridor) continue;
    const sorted = [...new Set(indices)].sort((a, b) => b - a);
    if (corridor.points.length - sorted.length < 2) {
      removeCorridor(corridorId);
      continue;
    }
    for (const index of sorted) corridor.points.splice(index, 1);
  }
  clearSelection();
  queueRebuild();
  setStatus('Deleted', 'ok');
}

function selectAll() {
  const sel = state.selection;
  if (state.tool === 'junction') {
    sel.points = [];
    sel.junctions = [];
    for (const node of state.network?.graph.nodes.values() || []) {
      if (node.degree >= 2) sel.junctions.push({ id: node.id, at: { ...node.co } });
    }
    setStatus(`${sel.junctions.length} intersections selected`, 'ok');
  } else {
    sel.junctions = [];
    sel.points = [];
    sel.corridorIds = [];
    for (const corridor of state.doc.corridors) {
      if (corridor.visible === false) continue;
      sel.corridorIds.push(corridor.id);
      if (state.tool === 'point') corridor.points.forEach((p, index) => sel.points.push({ corridorId: corridor.id, index }));
    }
    setStatus(state.tool === 'point' ? `${sel.points.length} control points selected` : `${sel.corridorIds.length} corridors selected`, 'ok');
  }
  afterSelectionChange();
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

// Legacy entry point used by the outliner and the draw tool: select a corridor, optionally focusing one point.
function select(corridorId, pointIndex = -1) {
  selectCorridor(corridorId, { pointIndex });
}

function removeCorridor(id) {
  state.doc.corridors = state.doc.corridors.filter((c) => c.id !== id);
  if (state.selection.corridorId === id) state.selection = emptySelection();
  queueRebuild();
  renderInspector();
}

const TOOLS = [
  ['ToolSelect', 'select', 'Click a road or bridge to select the stretch between its junctions'],
  ['ToolPoint', 'point', 'Control points · click to pick, Shift-click or Shift-drag to multi-select, gizmo to move'],
  ['ToolJunction', 'junction', 'Intersections · click to pick, Shift-click or Shift-drag to multi-select, gizmo to move'],
  ['ToolRoad', 'draw-road', 'Click on the ground to place control points · Enter to finish'],
  ['ToolBridge', 'draw-bridge', 'Click on the ground to place control points · Enter to finish'],
];

function setTool(tool) {
  state.tool = tool;
  let hint = '';
  for (const [id, value, text] of TOOLS) {
    $(id)?.classList.toggle('Active', state.tool === value);
    if (state.tool === value) hint = text;
  }
  canvas.style.cursor = tool.startsWith('draw') ? 'crosshair' : 'default';
  if (!tool.startsWith('draw')) state.draft = null;
  // Switching between point and junction mode drops the selection that the other mode owns, so the gizmo can
  // never move something the current tool cannot see.
  if (tool === 'point') state.selection.junctions = [];
  if (tool === 'junction') state.selection.points = [];
  if (tool === 'point' || tool === 'junction') afterSelectionChange();
  setStatus(hint, tool.startsWith('draw') ? 'busy' : 'ok');
}

function setLabelsVisible(on) {
  state.display.labels = on;
  viewport.setLabelsVisible(on);
  $('ViewLabels')?.classList.toggle('Active', on);
  setStatus(on ? 'Street names shown' : 'Street names hidden', 'ok');
}

// ── outliner ──────────────────────────────────────────────────────────────────────────────────────────────────────

function renderOutliner() {
  const list = $('CorridorList');
  list.innerHTML = '';
  for (const corridor of state.doc.corridors) {
    const row = document.createElement('div');
    row.className = 'OutlinerEntry CorridorRow' + (state.selection.corridorIds.includes(corridor.id) ? ' Selected' : '');

    const icon = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    const use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
    use.setAttribute('href', corridor.family === 'bridge' ? '#BridgeIcon' : '#RoadIcon');
    icon.appendChild(use);

    const label = document.createElement('span');
    label.className = 'CorridorName';
    label.textContent = corridor.name;
    label.title = 'Double-click to rename';
    label.addEventListener('click', () => select(corridor.id));
    // Double-click to rename in place: naming streets should not mean a round trip through the inspector.
    label.addEventListener('dblclick', (e) => {
      e.stopPropagation();
      beginRename(corridor, label);
    });

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
    row.addEventListener('click', (e) => selectCorridor(corridor.id, { additive: e.shiftKey }));
    list.appendChild(row);
  }

  const stats = state.network?.stats;
  $('CensusCorridors').textContent = state.doc.corridors.length;
  $('CensusJunctions').textContent = stats ? stats.junctions : 0;
  const sel = state.selection;
  const junction = selectedJunction();
  $('SelectionName').textContent =
    sel.junctions.length > 1
      ? `${sel.junctions.length} intersections`
      : junction
        ? junctionName(junction)
        : sel.corridorIds.length > 1
          ? `${sel.corridorIds.length} corridors`
          : selectedCorridor()?.name || 'Nothing selected';
  $('SelectionDetail').textContent =
    sel.points.length > 1
      ? `${sel.points.length} control points across ${new Set(sel.points.map((p) => p.corridorId)).size} corridor(s)`
      : junction
        ? `${junction.degree} arms meet here`
        : sel.edgeId
          ? 'Stretch between junctions · drag a handle or switch to Points mode'
          : sel.pointIndex >= 0 && selectedCorridor()
            ? `Control point ${sel.pointIndex + 1} of ${selectedCorridor().points.length}`
            : 'Click a road to select the stretch between its junctions · double-click a name to rename it';
}

// Inline rename in the outliner. Enter commits, Escape reverts, blur commits.
function beginRename(corridor, label) {
  const input = document.createElement('input');
  input.className = 'RenameField';
  input.value = corridor.name;
  input.maxLength = 48;
  const before = corridor.name;
  label.replaceWith(input);
  input.focus();
  input.select();
  const commit = (keep) => {
    corridor.name = keep ? input.value.trim() || before : before;
    labelSignature = '';
    renderOutliner();
    renderInspector();
    refreshOverlay();
    if (keep) setStatus(`Renamed to “${corridor.name}”`, 'ok');
  };
  input.addEventListener('keydown', (e) => {
    e.stopPropagation();
    if (e.key === 'Enter') commit(true);
    if (e.key === 'Escape') commit(false);
  });
  input.addEventListener('blur', () => commit(true));
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
  const junction = selectedJunction();
  const sel = state.selection;

  if (sel.junctions.length > 1 || sel.points.length > 1) {
    renderMultiInspector(host);
    return;
  }

  if (junction) {
    renderJunctionInspector(host, junction);
    return;
  }

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

    // ── paving ──
    const pave = section('Paving');
    pave.body.appendChild(
      selectField({
        label: 'Pattern',
        value: corridor.paving || 'concrete',
        options: Object.entries(PAVING_PATTERNS).map(([k, v]) => [k, v.label]),
        onChange: (v) => { corridor.paving = v; mark(); },
      }),
    );
    const paveWidth = Math.max(profile.pavementLeft, profile.pavementRight);
    pave.body.appendChild(
      num('Paving width', paveWidth, 0, 8, 0.1, 'm', (v) => {
        corridor.overrides.pavementLeft = v;
        corridor.overrides.pavementRight = v;
      }, () => { mark(); }),
    );
    pave.body.appendChild(num('Paver scale', corridor.pavingScale ?? 1, 0.4, 3, 0.05, '×', (v) => (corridor.pavingScale = v), mark));
    const paveNote = document.createElement('p');
    paveNote.className = 'Small';
    paveNote.textContent = 'Patterns are drawn procedurally at runtime and tiled in metres, so pavers keep their real size around curves and through junction aprons.';
    pave.body.appendChild(paveNote);
    host.appendChild(pave.element);

    // ── what holds the road up ──
    if (corridor.family !== 'bridge') {
      const rb = corridor.roadbed || (corridor.roadbed = { ...ROADBED_DEFAULTS });
      const bed = section('Roadbed', false);
      bed.body.appendChild(
        selectField({
          label: 'Support',
          value: rb.mode,
          options: [['auto', 'Auto (fill → wall)'], ['embankment', 'Earth embankment'], ['wall', 'Retaining wall'], ['slab', 'Slab soffit'], ['none', 'None (floating)']],
          onChange: (v) => { rb.mode = v; renderInspector(); mark(); },
        }),
      );
      if (rb.mode === 'auto' || rb.mode === 'embankment') {
        bed.body.appendChild(num('Batter slope', rb.slope, 0.5, 4, 0.1, ': 1', (v) => (rb.slope = v), mark));
      }
      if (rb.mode === 'auto') bed.body.appendChild(num('Max fill before wall', rb.maxFill, 1, 20, 0.5, 'm', (v) => (rb.maxFill = v), mark));
      if (rb.mode === 'auto' || rb.mode === 'wall') bed.body.appendChild(num('Wall batter', rb.wallBatter, 0, 0.15, 0.005, '', (v) => (rb.wallBatter = v), mark));
      if (rb.mode === 'slab') bed.body.appendChild(num('Slab depth', rb.slabDepth, 0.2, 2, 0.05, 'm', (v) => (rb.slabDepth = v), mark));
      const bedNote = document.createElement('p');
      bedNote.className = 'Small';
      bedNote.textContent = 'Only built where the corridor sits above ground level, and it stops and restarts cleanly wherever the alignment crosses grade.';
      bed.body.appendChild(bedNote);
      host.appendChild(bed.element);

      // ── restraint system ──
      const gr = corridor.guardrail || (corridor.guardrail = { ...GUARDRAIL_DEFAULTS });
      const rail = section('Guardrail', gr.type !== 'none');
      rail.body.appendChild(
        selectField({
          label: 'System',
          value: gr.type,
          options: Object.entries(GUARDRAIL_TYPES),
          onChange: (v) => { gr.type = v; renderInspector(); mark(); },
        }),
      );
      if (gr.type !== 'none') {
        rail.body.appendChild(
          selectField({
            label: 'Where',
            value: gr.when,
            options: [['fill', 'Only on embankment'], ['always', 'Whole corridor']],
            onChange: (v) => { gr.when = v; renderInspector(); mark(); },
          }),
        );
        if (gr.when === 'fill') rail.body.appendChild(num('Fill before railing', gr.fillTrigger, 0.2, 10, 0.1, 'm', (v) => (gr.fillTrigger = v), mark));
        rail.body.appendChild(selectField({ label: 'Side', value: gr.side, options: [['both', 'Both sides'], ['left', 'Left only'], ['right', 'Right only']], onChange: (v) => { gr.side = v; mark(); } }));
        rail.body.appendChild(num('Inset from edge', gr.offset, 0, 3, 0.05, 'm', (v) => (gr.offset = v), mark));
        rail.body.appendChild(num('Height', gr.height, 0.45, 1.4, 0.01, 'm', (v) => (gr.height = v), mark));
        const note = document.createElement('p');
        note.className = 'Small';
        note.textContent = 'Steel and rope systems are swept as real corrugated or tubular sections on posts at their standard spacing; Jersey and parapet barriers are solid concrete.';
        rail.body.appendChild(note);
      }
      host.appendChild(rail.element);
    }

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
      if (b.type === 'beam' || b.type === 'box' || b.type === 'cantilever' || b.type === 'slab') {
        if (b.type === 'beam' || b.type === 'box') sup.body.appendChild(num('Girder count', b.girderCount, 1, 10, 1, '', (v) => (b.girderCount = v), mark));
        const depthLabel = b.type === 'cantilever' ? 'Midspan depth' : b.type === 'slab' ? 'Haunch depth' : 'Girder depth';
        sup.body.appendChild(num(depthLabel, b.girderDepth, 0.4, 4, 0.1, 'm', (v) => (b.girderDepth = v), mark));
      }
      if (b.type === 'arch' || b.type === 'tiedarch' || b.type === 'masonry') {
        sup.body.appendChild(num(b.type === 'tiedarch' ? 'Arch rise above deck' : 'Arch rise', b.archRise, 1, 30, 0.5, 'm', (v) => (b.archRise = v), mark));
      }
      if (b.type === 'tiedarch') sup.body.appendChild(num('Hanger count', b.cableCount, 2, 16, 1, '', (v) => (b.cableCount = v), mark));
      if (b.type === 'masonry') sup.body.appendChild(num('Bay span', b.pierSpacing, 10, 90, 1, 'm', (v) => (b.pierSpacing = v), mark));
      if (b.type === 'truss' || b.type === 'throughtruss') sup.body.appendChild(num('Truss height', b.trussHeight, 1.5, 12, 0.25, 'm', (v) => (b.trussHeight = v), mark));
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

  host.appendChild(networkSection().element);
  host.appendChild(displaySection().element);

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

function networkSection() {
  const net = section('Network solver');
  const s = state.doc.settings;
  net.body.appendChild(num('Sample step', s.sampleStep, 0.5, 6, 0.25, 'm', (v) => (s.sampleStep = v), queueRebuild));
  net.body.appendChild(num('Node merge radius', s.nodeMergeXY, 0.5, 8, 0.25, 'm', (v) => (s.nodeMergeXY = v), queueRebuild));
  net.body.appendChild(num('Grade separation', s.zMerge, 0.5, 8, 0.25, 'm', (v) => (s.zMerge = v), queueRebuild));
  net.body.appendChild(num('Junction radius scale', s.cornerScale, 0.4, 2.5, 0.05, '×', (v) => (s.cornerScale = v), queueRebuild));
  net.body.appendChild(num('Ground level', s.groundZ, -20, 20, 0.5, 'm', (v) => (s.groundZ = v), queueRebuild));
  net.body.appendChild(checkField({ label: 'Lane markings', value: s.markings, onChange: (v) => { s.markings = v; queueRebuild(); } }));
  net.body.appendChild(
    selectField({
      label: 'Junction signage',
      value: s.signage ?? 'stop',
      options: [['stop', 'Stop signs'], ['yield', 'Yield signs'], ['none', 'None']],
      onChange: (v) => { s.signage = v; queueRebuild(); },
    }),
  );
  net.body.appendChild(checkField({ label: 'Stop bars', value: s.stopBars !== false, onChange: (v) => { s.stopBars = v; queueRebuild(); } }));
  net.body.appendChild(checkField({ label: 'Zebra crossings', value: s.crosswalks !== false, onChange: (v) => { s.crosswalks = v; queueRebuild(); } }));
  return net;
}

function displaySection() {
  const disp = section('Display');
  disp.body.appendChild(control({ label: 'Light azimuth', value: viewport.sunAzimuth, min: -180, max: 180, step: 1, format: (v) => `${v}°`, onInput: (v) => viewport.setSunAzimuth(v) }));
  disp.body.appendChild(control({ label: 'Flight speed', value: viewport.flySpeed, min: 2, max: 200, step: 1, format: (v) => `${Math.round(v)} m/s`, onInput: (v) => (viewport.flySpeed = v) }));
  disp.body.appendChild(checkField({ label: 'Surface textures', value: state.display.textures, onChange: (v) => { state.display.textures = v; viewport.setTextured(v); } }));
  disp.body.appendChild(checkField({ label: 'Cast shadows', value: state.display.shadows, onChange: (v) => { state.display.shadows = v; viewport.setShadows(v); } }));
  disp.body.appendChild(checkField({ label: 'Ground plane', value: state.display.ground, onChange: (v) => { state.display.ground = v; viewport.setGroundVisible(v); } }));
  disp.body.appendChild(checkField({ label: 'Editing overlay', value: state.display.overlay, onChange: (v) => { state.display.overlay = v; viewport.setOverlayVisible(v); } }));
  disp.body.appendChild(checkField({ label: 'Show markings', value: state.display.markings, onChange: (v) => { state.display.markings = v; viewport.setMarkingsVisible(v); } }));
  disp.body.appendChild(checkField({ label: 'Street name labels', value: state.display.labels, onChange: (v) => setLabelsVisible(v) }));
  return disp;
}

// What a transform of several things at once needs: how many, where their centroid is, and a way to nudge them
// numerically as well as with the gizmo.
function renderMultiInspector(host) {
  const sel = state.selection;
  const kind = sel.junctions.length > 1 ? 'intersections' : 'control points';
  const count = sel.junctions.length > 1 ? sel.junctions.length : sel.points.length;

  const header = document.createElement('div');
  header.className = 'ObjectHeader';
  const icon = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  const use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
  use.setAttribute('href', '#NodeIcon');
  icon.appendChild(use);
  const text = document.createElement('div');
  text.innerHTML = `<b>${count} ${kind}</b><small>Multiple selection</small>`;
  header.append(icon, text);
  host.appendChild(header);
  $('InspectorTitle').textContent = `${count} selected`;

  const centre = selectionCentroid();
  const m = section('Transform');
  const metrics = document.createElement('div');
  metrics.className = 'Metrics';
  for (const [k, v] of [
    ['Items', count],
    ['Centroid', centre ? `${centre.x.toFixed(1)}, ${centre.y.toFixed(1)}` : '—'],
    ['Elevation', centre ? `${centre.z.toFixed(2)} m` : '—'],
    ['Corridors', new Set(sel.points.map((p) => p.corridorId)).size || sel.corridorIds.length],
  ]) {
    const a = document.createElement('span');
    a.textContent = k;
    const b = document.createElement('b');
    b.textContent = v;
    metrics.append(a, b);
  }
  m.body.appendChild(metrics);

  const nudge = (label, axis, amount) => {
    const button = document.createElement('button');
    button.textContent = label;
    button.addEventListener('click', () => {
      const grabs = collectGrabs();
      for (const grab of grabs) grab.point[axis] += amount;
      for (const entry of sel.junctions) entry.at[axis] += amount;
      refreshOverlay();
      queueRebuild();
      renderInspector();
      setStatus(`Nudged ${grabs.length} points ${amount > 0 ? '+' : ''}${amount} m in ${axis.toUpperCase()}`, 'ok');
    });
    return button;
  };
  const row = document.createElement('div');
  row.className = 'ButtonRow';
  row.append(nudge('Raise 1 m', 'z', 1), nudge('Lower 1 m', 'z', -1));
  m.body.appendChild(row);

  const note = document.createElement('p');
  note.className = 'Small';
  note.textContent =
    sel.junctions.length > 1
      ? 'Dragging the gizmo moves every selected intersection — and every arm of each one — as one rigid body.'
      : 'Dragging the gizmo moves all selected control points together. Delete removes them.';
  m.body.appendChild(note);

  const clear = document.createElement('button');
  clear.textContent = 'Clear selection';
  clear.addEventListener('click', () => clearSelection());
  m.body.appendChild(clear);
  host.appendChild(m.element);

  host.appendChild(networkSection().element);
  host.appendChild(displaySection().element);
}

// A junction has no document record of its own — it is an emergent property of the corridors that meet there — so
// its inspector reports the solve and offers the handful of knobs that actually belong to the node.
function renderJunctionInspector(host, node) {
  const header = document.createElement('div');
  header.className = 'ObjectHeader';
  const icon = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  const use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
  use.setAttribute('href', '#NodeIcon');
  icon.appendChild(use);
  const text = document.createElement('div');
  const name = junctionName(node);
  text.innerHTML = `<b>${name}</b><small>${node.degree} arms merged</small>`;
  header.append(icon, text);
  host.appendChild(header);
  $('InspectorTitle').textContent = name;

  const j = section('Junction');
  const metrics = document.createElement('div');
  metrics.className = 'Metrics';
  const arms = [...new Set(node.edgeIds.map((id) => state.network.graph.edges.get(id)?.name).filter(Boolean))];
  for (const [k, v] of [
    ['Position', `${node.co.x.toFixed(1)}, ${node.co.y.toFixed(1)}`],
    ['Elevation', `${node.co.z.toFixed(2)} m`],
    ['Corner radius', `${node.cornerRadius.toFixed(1)} m`],
    ['Arms', arms.join(', ') || node.degree],
  ]) {
    const a = document.createElement('span');
    a.textContent = k;
    const b = document.createElement('b');
    b.textContent = v;
    metrics.append(a, b);
  }
  j.body.appendChild(metrics);
  const note = document.createElement('p');
  note.className = 'Small';
  note.textContent = 'Drag the gizmo to move every arm of this intersection together. Corridors that only pass through gain a control point here so the crossing follows.';
  j.body.appendChild(note);
  const clear = document.createElement('button');
  clear.textContent = 'Deselect junction';
  clear.addEventListener('click', () => {
    state.selection = emptySelection();
    refreshOverlay();
    renderInspector();
  });
  j.body.appendChild(clear);
  host.appendChild(j.element);

  host.appendChild(networkSection().element);
  host.appendChild(displaySection().element);
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
    ['Cached corridors', `${s.cacheHits ?? 0} of ${s.edges}`],
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
  for (const [id, tool] of TOOLS.map(([id, tool]) => [id, tool])) {
    $(id)?.addEventListener('click', () => setTool(tool));
  }
  $('ViewLabels')?.addEventListener('click', () => setLabelsVisible(!state.display.labels));

  $('ViewShaded').addEventListener('click', () => setMode('shaded'));
  $('ViewWire').addEventListener('click', () => setMode('wireframe'));
  $('ViewSurfaces').addEventListener('click', () => setMode('surfaces'));
  $('Frame').addEventListener('click', () => viewport.frame(frameTarget()));
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
    state.selection = emptySelection();
    $('DocumentName').value = state.doc.name;
    rebuild();
    renderInspector();
    viewport.frame(networkBounds());
  });
  $('DocumentName').value = state.doc.name;
  $('DocumentName').addEventListener('input', (e) => (state.doc.name = e.target.value));

  renderInspector();
  setTool('select');
  setLabelsVisible(state.display.labels);
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
  const groups = Object.entries(state.network.groups).map(([name, spec]) => ({ name, spec }));
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
    state.selection = emptySelection();
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

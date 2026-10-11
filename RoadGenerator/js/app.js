// Frontier Road Generator: application controller.
// Owns the project, camera, picking, gizmo editing, tools, the rebuild pipeline and the frame loop.

import {
  sampleProject, createEmptyProject, makeRoad, makeNode, makeProp, ROAD_CLASSES, defaultSettings,
} from './model.js';
import { buildNetwork } from './roadgen.js';
import { Renderer } from './renderer.js';
import { Traffic, MAX_VEHICLES } from './vehicles.js';
import { defaultStack, MAX_LAYERS, makeLayer, LAYER_TYPES } from './layers.js';
import { mat4Multiply, mat4Perspective, mat4LookAt, mat4Invert } from './math.js';
import { OverlayBuilder } from './overlay.js';
import * as UI from './ui.js';

const DEG = Math.PI / 180;
const canvas = document.getElementById('view');

export const app = {
  project: null,
  renderer: null,
  traffic: new Traffic(),
  net: null,
  tool: 'select',
  propKind: 'streetlight',
  roadClass: 'collector',
  selection: null,        // { type: 'node'|'road'|'prop', id }
  draft: null,            // road being drawn: { from, mids: [] }
  drag: null,
  hoverNode: null,
  rebuildQueued: false,
  rebuildAt: 0,
  lastBuildMs: 0,
  materialSel: 'asphalt',
  layerSel: 0,
  tab: 'inspector',
  cam: { target: [0, 0, 0], yaw: 0.55, pitch: 0.8, dist: 300, vp: null, inv: null, eye: [0, 0, 0] },
  W: 1, H: 1,
  lastT: performance.now(),
  hudAt: 0,
  error: null,
};

// ---------- project lifecycle ----------------------------------------------------------------
export function ensureProject(p) {
  const base = createEmptyProject();
  const out = { ...base, ...p };
  out.settings = { ...defaultSettings(), ...(p.settings || {}), textureTile: { ...defaultSettings().textureTile, ...((p.settings || {}).textureTile || {}) } };
  out.nodes = (p.nodes || []).map(n => makeNode(n));
  out.roads = (p.roads || []).map(r => makeRoad(r));
  out.props = (p.props || []).map(q => makeProp(q));
  out.layers = {
    asphalt: p.layers?.asphalt || defaultStack('asphalt'),
    concrete: p.layers?.concrete || defaultStack('concrete'),
    pavers: p.layers?.pavers || defaultStack('pavers'),
  };
  return out;
}

export function loadProject(p) {
  app.project = ensureProject(p);
  app.selection = null;
  app.draft = null;
  app.drag = null;
  app.renderer.markMaterialDirty(0);
  app.renderer.markMaterialDirty(1);
  app.renderer.markMaterialDirty(2);
  requestRebuild();
  UI.refreshAll(app);
}

// ---------- rebuild -----------------------------------------------------------------------------
export function requestRebuild() { app.rebuildQueued = true; }

function rebuild() {
  const t0 = performance.now();
  const s = app.project.settings;
  const view = {
    ...app.project,
    roads: app.project.roads.map(r => (r.markings && s.markingsOn ? r : { ...r, markings: false })),
  };
  const net = buildNetwork(view);
  app.net = net;
  const main = net.main.build();
  const decal = net.decal.build();
  app.renderer.setMesh('main', main);
  app.renderer.setMesh('decal', decal);
  app.traffic.sync(net, app.project, s);
  app.rebuildQueued = false;
  app.rebuildAt = performance.now();
  app.lastBuildMs = app.rebuildAt - t0;
  app.buildStats = { quads: main.stats.quads, tris: main.stats.tris, concave: main.stats.concave, degenerate: main.stats.degenerate, decalTris: decal.stats.tris };
  UI.onNetworkRebuilt(app);
}

// ---------- structural edits --------------------------------------------------------------------
function uid(prefix) { return `${prefix}${Math.random().toString(36).slice(2, 8)}`; }

export function createNode(x, z) {
  const count = app.project.nodes.length + 1;
  const n = makeNode({ id: uid('n'), x, z, name: `Intersection ${count}` });
  app.project.nodes.push(n);
  return n;
}

function addRoad(a, b, mids) {
  if (a === b) return null;
  const count = app.project.roads.length + 1;
  const r = makeRoad({ id: uid('r'), a, b, cls: app.roadClass, mid: mids, name: `Road ${count}` });
  app.project.roads.push(r);
  return r;
}

export function deleteSelection() {
  const sel = app.selection;
  if (!sel) return;
  const p = app.project;
  if (sel.type === 'node') {
    p.roads = p.roads.filter(r => r.a !== sel.id && r.b !== sel.id);
    p.nodes = p.nodes.filter(n => n.id !== sel.id);
  } else if (sel.type === 'road') {
    p.roads = p.roads.filter(r => r.id !== sel.id);
  } else if (sel.type === 'prop') {
    p.props = p.props.filter(q => q.id !== sel.id);
  }
  app.selection = null;
  requestRebuild();
  UI.refreshAll(app);
}

export function flipSelectedRoad() {
  const r = findRoad(app.selection?.id);
  if (!r) return;
  [r.a, r.b] = [r.b, r.a];
  r.mid = r.mid.slice().reverse();
  requestRebuild();
  UI.refreshAll(app);
}

export function findNode(id) { return app.project.nodes.find(n => n.id === id); }
export function findRoad(id) { return app.project.roads.find(r => r.id === id); }
export function findProp(id) { return app.project.props.find(q => q.id === id); }

const CLASS_KEYS = ['lanesPerDir', 'laneWidth', 'speed', 'median', 'medianWidth', 'sidewalk', 'sidewalkWidth', 'lightSpacing'];

function setPath(obj, path, value) {
  const keys = path.split('.');
  let o = obj;
  for (let i = 0; i < keys.length - 1; i++) o = o[keys[i]];
  o[keys[keys.length - 1]] = value;
}

// Called by the inspector / materials panel.
export function setValue(target, id, key, value) {
  const p = app.project;
  if (target === 'node') {
    const n = findNode(id); if (!n) return;
    setPath(n, key, value);
    requestRebuild();
  } else if (target === 'road') {
    const r = findRoad(id); if (!r) return;
    setPath(r, key, value);
    if (key === 'cls') {
      const c = ROAD_CLASSES[value];
      for (const k of CLASS_KEYS) r[k] = c[k];
      r.railings.on = c.railings;
      UI.refreshInspector(app);
    }
    requestRebuild();
  } else if (target === 'prop') {
    const q = findProp(id); if (!q) return;
    setPath(q, key, value);
    requestRebuild();
  } else if (target === 'settings') {
    setPath(p.settings, key, value);
    if (key.startsWith('textureTile')) for (const i of [0, 1, 2]) app.renderer.markMaterialDirty(i);
    if (key === 'markingsOn') requestRebuild();
  } else if (target === 'layer') {
    const stack = p.layers[app.materialSel];
    const layer = stack[app.layerSel];
    if (!layer) return;
    setPath(layer, key, value);
    app.renderer.markMaterialDirty(matIndex(app.materialSel));
  }
}

export function matIndex(name) { return { asphalt: 0, concrete: 1, pavers: 2 }[name]; }

export function addLayer(typeId) {
  const stack = app.project.layers[app.materialSel];
  if (stack.length >= MAX_LAYERS) return;
  stack.push(makeLayer(typeId));
  app.layerSel = stack.length - 1;
  app.renderer.markMaterialDirty(matIndex(app.materialSel));
  UI.refreshMaterials(app);
}

export function removeLayer(index) {
  const stack = app.project.layers[app.materialSel];
  stack.splice(index, 1);
  app.layerSel = Math.max(0, Math.min(app.layerSel, stack.length - 1));
  app.renderer.markMaterialDirty(matIndex(app.materialSel));
  UI.refreshMaterials(app);
}

export function moveLayer(index, dir) {
  const stack = app.project.layers[app.materialSel];
  const j = index + dir;
  if (j < 0 || j >= stack.length) return;
  [stack[index], stack[j]] = [stack[j], stack[index]];
  app.layerSel = j;
  app.renderer.markMaterialDirty(matIndex(app.materialSel));
  UI.refreshMaterials(app);
}

export function resetMaterial(name) {
  app.project.layers[name] = defaultStack(name);
  app.layerSel = 0;
  app.renderer.markMaterialDirty(matIndex(name));
  UI.refreshMaterials(app);
}

// ---------- camera -----------------------------------------------------------------------------
function eyeOf(cam) {
  const cp = Math.cos(cam.pitch);
  return [cam.target[0] + cam.dist * cp * Math.sin(cam.yaw), cam.target[1] + cam.dist * Math.sin(cam.pitch), cam.target[2] + cam.dist * cp * Math.cos(cam.yaw)];
}

function updateCamera() {
  const cam = app.cam;
  cam.eye = eyeOf(cam);
  const view = mat4LookAt(cam.eye, cam.target, [0, 1, 0]);
  const proj = mat4Perspective(40 * DEG, Math.max(1, app.W) / Math.max(1, app.H), 0.5, 6000);
  cam.vp = mat4Multiply(proj, view);
  cam.inv = mat4Invert(cam.vp);
}

function screenOf(p) {
  const m = app.cam.vp;
  const x = p[0], y = p[1], z = p[2];
  const cx = m[0] * x + m[4] * y + m[8] * z + m[12];
  const cy = m[1] * x + m[5] * y + m[9] * z + m[13];
  const cw = m[3] * x + m[7] * y + m[11] * z + m[15];
  return [(cx / cw * 0.5 + 0.5) * app.W, (1 - (cy / cw * 0.5 + 0.5)) * app.H];
}

function unproject(sx, sy, zndc) {
  const inv = app.cam.inv;
  const x = sx / app.W * 2 - 1, y = 1 - sy / app.H * 2, z = zndc;
  const r = [0, 0, 0, 0];
  for (let row = 0; row < 4; row++) r[row] = inv[row] * x + inv[4 + row] * y + inv[8 + row] * z + inv[12 + row];
  return [r[0] / r[3], r[1] / r[3], r[2] / r[3]];
}

// Ground-plane point (x, z) under a screen position.
export function groundAt(sx, sy) {
  const a = unproject(sx, sy, 0), b = unproject(sx, sy, 1);
  const dy = b[1] - a[1];
  if (Math.abs(dy) < 1e-9) return null;
  const t = -a[1] / dy;
  return [a[0] + (b[0] - a[0]) * t, a[2] + (b[2] - a[2]) * t];
}

function snapXZ(g) {
  if (!app.project.settings.snap) return g;
  return [Math.round(g[0] * 2) / 2, Math.round(g[1] * 2) / 2];
}

const segDist = (p, a, b) => {
  const dx = b[0] - a[0], dy = b[1] - a[1];
  const l2 = dx * dx + dy * dy || 1;
  const t = Math.max(0, Math.min(1, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2));
  return Math.hypot(p[0] - (a[0] + dx * t), p[1] - (a[1] + dy * t));
};

// ---------- picking ----------------------------------------------------------------------------
function nodeAt(sx, sy, maxPx = 16) {
  let best = null, bd = maxPx;
  for (const n of app.project.nodes) {
    const s = screenOf([n.x, 0, n.z]);
    const d = Math.hypot(s[0] - sx, s[1] - sy);
    if (d < bd) { bd = d; best = n; }
  }
  return best;
}

function propAt(sx, sy) {
  let best = null, bd = 14;
  for (const q of app.project.props) {
    const s = screenOf([q.x, 0, q.z]);
    const d = Math.hypot(s[0] - sx, s[1] - sy);
    if (d < bd) { bd = d; best = q; }
  }
  return best;
}

function roadAt(sx, sy) {
  if (!app.net) return null;
  let best = null, bd = 9;
  for (const info of app.net.roadInfo.values()) {
    const pts = info.curve.pts;
    let prev = null;
    for (let i = 0; i < pts.length; i += 2) {
      const s = screenOf([pts[i][0], 0, pts[i][1]]);
      if (prev) {
        const d = segDist([sx, sy], prev, s);
        if (d < bd) { bd = d; best = info.road; }
      }
      prev = s;
    }
  }
  return best;
}

// Gizmo geometry for the selected intersection, in world units scaled to screen distance.
function gizmoSize(n) {
  const cam = app.cam;
  return Math.max(6, cam.dist * 0.11) * (n.scale || 1);
}

function gizmoHit(sx, sy) {
  const sel = app.selection;
  if (!sel || sel.type !== 'node') return null;
  const n = findNode(sel.id);
  if (!n) return null;
  const g = gizmoSize(n);
  const o = screenOf([n.x, 0, n.z]);
  if (Math.hypot(o[0] - sx, o[1] - sy) < 12) return 'free';
  const tx = screenOf([n.x + g, 0, n.z]);
  if (segDist([sx, sy], o, tx) < 9) return 'x';
  const tz = screenOf([n.x, 0, n.z + g]);
  if (segDist([sx, sy], o, tz) < 9) return 'z';
  return null;
}

function midHandleAt(sx, sy) {
  const sel = app.selection;
  if (!sel || sel.type !== 'road') return -1;
  const r = findRoad(sel.id);
  if (!r) return -1;
  for (let i = 0; i < r.mid.length; i++) {
    const s = screenOf([r.mid[i].x, 0, r.mid[i].z]);
    if (Math.hypot(s[0] - sx, s[1] - sy) < 10) return i;
  }
  return -1;
}

// ---------- pointer input ------------------------------------------------------------------------
function localXY(e) {
  const rect = canvas.getBoundingClientRect();
  return [e.clientX - rect.left, e.clientY - rect.top];
}

function onPointerDown(e) {
  const [x, y] = localXY(e);
  canvas.setPointerCapture(e.pointerId);
  if (e.button === 1 || e.button === 2 || (e.button === 0 && e.shiftKey)) {
    app.drag = { kind: e.shiftKey || e.button === 1 ? 'pan' : 'orbit', x, y, moved: true };
    return;
  }
  if (e.button !== 0) return;
  const tool = app.tool;
  if (tool === 'road') { roadToolClick(x, y, e.detail === 2); return; }
  if (tool === 'node') {
    const g = groundAt(x, y);
    if (!g) return;
    const hit = nodeAt(x, y);
    if (hit) { app.selection = { type: 'node', id: hit.id }; }
    else { const n = createNode(...snapXZ(g)); app.selection = { type: 'node', id: n.id }; requestRebuild(); }
    UI.refreshAll(app);
    setTool('select');
    return;
  }
  if (tool === 'prop') {
    const g = groundAt(x, y);
    if (!g) return;
    const q = makeProp({ id: uid('p'), kind: app.propKind, x: snapXZ(g)[0], z: snapXZ(g)[1], rot: 0, design: 'post' });
    app.project.props.push(q);
    app.selection = { type: 'prop', id: q.id };
    requestRebuild();
    UI.refreshAll(app);
    return;
  }
  // select tool
  const g = gizmoHit(x, y);
  if (g) {
    const n = findNode(app.selection.id);
    const connected = app.project.roads.filter(r => r.a === n.id || r.b === n.id);
    app.drag = {
      kind: 'gizmo', axis: g, start: groundAt(x, y), n0: { x: n.x, z: n.z },
      mids: connected.map(r => ({ road: r, mids: r.mid.map(p => ({ x: p.x, z: p.z })) })),
    };
    return;
  }
  const mi = midHandleAt(x, y);
  if (mi >= 0) {
    app.drag = { kind: 'mid', index: mi, road: findRoad(app.selection.id) };
    return;
  }
  const hitNode = nodeAt(x, y);
  if (hitNode) {
    app.selection = { type: 'node', id: hitNode.id };
    UI.refreshAll(app);
    return;
  }
  const hitRoad = roadAt(x, y);
  if (hitRoad) {
    app.selection = { type: 'road', id: hitRoad.id };
    UI.refreshAll(app);
    return;
  }
  const hitProp = propAt(x, y);
  if (hitProp) {
    app.selection = { type: 'prop', id: hitProp.id };
    app.drag = { kind: 'prop', id: hitProp.id, start: groundAt(x, y), p0: { x: hitProp.x, z: hitProp.z } };
    UI.refreshAll(app);
    return;
  }
  app.drag = { kind: 'pending', x, y, moved: false };
}

function onPointerMove(e) {
  const [x, y] = localXY(e);
  const d = app.drag;
  if (!d) {
    const hover = nodeAt(x, y);
    app.hoverNode = hover ? hover.id : null;
    canvas.style.cursor = app.tool === 'select' ? (gizmoHit(x, y) ? 'move' : hover ? 'pointer' : 'grab') : 'crosshair';
    return;
  }
  if (d.kind === 'orbit') {
    app.cam.yaw -= (x - d.x) * 0.006;
    app.cam.pitch = Math.max(0.12, Math.min(1.5, app.cam.pitch + (y - d.y) * 0.004));
    d.x = x; d.y = y;
  } else if (d.kind === 'pan') {
    const cam = app.cam;
    const s = cam.dist * 0.0016;
    const right = [Math.cos(cam.yaw), 0, -Math.sin(cam.yaw)];
    const fwd = [Math.sin(cam.yaw), 0, Math.cos(cam.yaw)];
    const dx = x - d.x, dy = y - d.y;
    for (let i = 0; i < 3; i++) cam.target[i] += -right[i] * dx * s - fwd[i] * dy * s;
    d.x = x; d.y = y;
  } else if (d.kind === 'pending') {
    if (Math.hypot(x - d.x, y - d.y) > 4) { app.drag = { kind: 'orbit', x, y }; }
  } else if (d.kind === 'gizmo') {
    const g = groundAt(x, y);
    if (!g || !d.start) return;
    const n = findNode(app.selection.id);
    if (!n) return;
    let dx = g[0] - d.start[0], dz = g[1] - d.start[1];
    if (d.axis === 'x') dz = 0;
    if (d.axis === 'z') dx = 0;
    let nx = d.n0.x + dx, nz = d.n0.z + dz;
    if (app.project.settings.snap) { nx = Math.round(nx * 2) / 2; nz = Math.round(nz * 2) / 2; }
    const ddx = nx - d.n0.x, ddz = nz - d.n0.z;
    n.x = nx; n.z = nz;
    // Carry nearby road control points so approaches bend with the intersection instead of kinking.
    for (const c of d.mids) {
      c.road.mid = c.mids.map(p => {
        const dist = Math.hypot(p.x - d.n0.x, p.z - d.n0.z);
        const w = Math.max(0, 1 - dist / 45);
        const f = w * w * (3 - 2 * w);
        return { x: p.x + ddx * f, z: p.z + ddz * f };
      });
    }
    requestRebuild();
  } else if (d.kind === 'mid') {
    const g = groundAt(x, y);
    if (!g || !d.road) return;
    const s = snapXZ(g);
    d.road.mid[d.index] = { x: s[0], z: s[1] };
    requestRebuild();
  } else if (d.kind === 'prop') {
    const g = groundAt(x, y);
    const q = findProp(d.id);
    if (!g || !q || !d.start) return;
    const s = snapXZ([d.p0.x + g[0] - d.start[0], d.p0.z + g[1] - d.start[1]]);
    q.x = s[0]; q.z = s[1];
    requestRebuild();
  }
}

function onPointerUp(e) {
  const d = app.drag;
  app.drag = null;
  if (d && d.kind === 'pending' && !d.moved) {
    app.selection = null;
    UI.refreshAll(app);
  }
  if (d && (d.kind === 'gizmo' || d.kind === 'mid' || d.kind === 'prop')) {
    UI.refreshAll(app);
  }
}

function onWheel(e) {
  e.preventDefault();
  const k = Math.exp(e.deltaY * 0.0012);
  app.cam.dist = Math.max(20, Math.min(2500, app.cam.dist * k));
}

// ---------- road tool --------------------------------------------------------------------------
function roadToolClick(x, y, dbl) {
  const g = groundAt(x, y);
  if (!g) return;
  const sp = snapXZ(g);
  const hit = nodeAt(x, y);
  if (!app.draft) {
    const from = hit ? hit.id : createNode(sp[0], sp[1]).id;
    app.draft = { from, mids: [] };
    app.selection = { type: 'node', id: from };
    requestRebuild();
    UI.refreshAll(app);
    return;
  }
  const dr = app.draft;
  if (hit) {
    if (hit.id === dr.from) return;
    const r = addRoad(dr.from, hit.id, dr.mids);
    app.draft = { from: hit.id, mids: [] };
    app.selection = { type: 'road', id: r.id };
  } else if (dbl) {
    // Double click ends the road on a new intersection; drop the duplicate waypoint from the first click.
    const last = dr.mids[dr.mids.length - 1];
    if (last && Math.hypot(last.x - sp[0], last.z - sp[1]) < 1.5) dr.mids.pop();
    const n = createNode(sp[0], sp[1]);
    const r = addRoad(dr.from, n.id, dr.mids);
    app.draft = { from: n.id, mids: [] };
    app.selection = { type: 'road', id: r.id };
  } else {
    dr.mids.push({ x: sp[0], z: sp[1] });
  }
  requestRebuild();
  UI.refreshAll(app);
}

export function setTool(t) {
  app.tool = t;
  app.draft = null;
  UI.refreshToolbar(app);
}

// ---------- overlay -----------------------------------------------------------------------------
function buildOverlay() {
  const ob = new OverlayBuilder();
  const p = app.project;
  const sel = app.selection;
  // Intersection markers
  for (const n of p.nodes) {
    const isSel = sel && sel.type === 'node' && sel.id === n.id;
    const hov = app.hoverNode === n.id;
    const rep = app.net && app.net.junctionReports.find(r => r.nodeId === n.id);
    const warn = rep && rep.overlap;
    if (isSel) ob.ring(n.x, n.z, 3.2, [1, 0.93, 0.35, 1]);
    else if (hov) ob.ring(n.x, n.z, 2.6, [1, 1, 1, 0.9]);
    else ob.ring(n.x, n.z, 1.8, [0.7, 0.75, 0.8, 0.55]);
    if (warn) ob.ring(n.x, n.z, 4.4, [1, 0.25, 0.2, 0.9]);
  }
  // Selected road: highlight + control handles
  if (sel && sel.type === 'road') {
    const r = findRoad(sel.id);
    const info = app.net && app.net.roadInfo.get(sel.id);
    if (r && info) {
      const pts = info.curve.pts;
      for (let i = 0; i < pts.length - 1; i += 3) {
        const a = pts[i], b = pts[Math.min(i + 3, pts.length - 1)];
        ob.band([a[0], a[1]], [b[0], b[1]], 0.5, [1, 0.93, 0.35, 0.8]);
      }
    }
    if (r) for (const m of r.mid) ob.square(m.x, m.z, 2.2, [1, 0.93, 0.35, 1]);
  }
  // Selected prop
  if (sel && sel.type === 'prop') {
    const q = findProp(sel.id);
    if (q) { ob.ring(q.x, q.z, 2.4, [1, 0.93, 0.35, 1]); }
  }
  // Intersection gizmo
  if (sel && sel.type === 'node') {
    const n = findNode(sel.id);
    if (n) {
      const g = gizmoSize(n);
      const dragAxis = app.drag && app.drag.kind === 'gizmo' ? app.drag.axis : null;
      const xc = dragAxis === 'x' ? [1, 0.6, 0.5, 1] : [0.95, 0.27, 0.27, 1];
      const zc = dragAxis === 'z' ? [0.55, 0.8, 1, 1] : [0.27, 0.5, 1, 1];
      ob.band([n.x, n.z], [n.x + g, n.z], g * 0.07, xc);
      ob.arrowhead([n.x + g, n.z], [1, 0], g * 0.2, xc);
      ob.band([n.x, n.z], [n.x, n.z + g], g * 0.07, zc);
      ob.arrowhead([n.x, n.z + g], [0, 1], g * 0.2, zc);
      ob.square(n.x, n.z, g * 0.16, [1, 1, 1, 1]);
    }
  }
  // Road draft preview
  if (app.draft) {
    const dr = app.draft;
    const from = findNode(dr.from);
    if (from) {
      let prev = [from.x, from.z];
      for (const m of dr.mids) { ob.band(prev, [m.x, m.z], 0.8, [0.4, 1, 0.6, 0.9]); prev = [m.x, m.z]; }
      const g = app.mouse ? groundAt(app.mouse[0], app.mouse[1]) : null;
      if (g) ob.band(prev, g, 0.8, [0.4, 1, 0.6, 0.45]);
    }
  }
  return ob.build();
}

// ---------- frame -------------------------------------------------------------------------------
function frame(now) {
  requestAnimationFrame(frame);
  const r = app.renderer;
  const dt = Math.min(0.05, (now - app.lastT) / 1000);
  app.lastT = now;
  const s = app.project.settings;

  if (app.rebuildQueued && (!app.drag || now - app.rebuildAt > 90)) rebuild();
  if (r.materialDirty.size) r.regenerateMaterials(app.project.layers, s.textureTile);

  if (s.trafficOn) {
    app.traffic.sync(app.net, app.project, s);
    app.traffic.step(dt);
    const b = app.traffic.build();
    r.setDynamic('vehicles', b.vertices, b.indices);
  } else {
    r.setDynamic('vehicles', new Float32Array(0), new Uint32Array(0));
  }

  updateCamera();
  const ov = buildOverlay();
  r.setOverlay(ov.tris, ov.lines);

  const sa = s.sunAzimuth * DEG, se = s.sunElevation * DEG;
  const sunDir = [Math.cos(se) * Math.sin(sa), Math.sin(se), Math.cos(se) * Math.cos(sa)];
  const wet = s.wetness;
  const sky = {
    zenith: [0.2 * (1 - wet * 0.4), 0.4 * (1 - wet * 0.3), 0.72 * (1 - wet * 0.2)],
    horizon: [0.66, 0.74, 0.82],
    ground: [0.26, 0.25, 0.23],
  };
  r.render({
    viewProj: app.cam.vp, eye: app.cam.eye, target: app.cam.target, time: now / 1000,
    sunDir, sunColor: [1.0, 0.93, 0.8].map(v => v * (0.6 + 0.4 * Math.max(0, Math.sin(se)))),
    sky, fogDensity: s.fogDensity, wetness: wet,
    tiles: s.textureTile, exposure: s.exposure, shadowExtent: 170,
  });

  if (now - app.hudAt > 250) {
    app.hudAt = now;
    UI.refreshHud(app);
  }
}

// Controller API used by the panels (ui.js).
Object.assign(app, {
  setTool, requestRebuild, loadProject, deleteSelection, flipSelectedRoad, setValue,
  addLayer, removeLayer, moveLayer, resetMaterial, createNode,
});

// ---------- boot --------------------------------------------------------------------------------
function resize() {
  const rect = canvas.getBoundingClientRect();
  app.W = Math.max(2, rect.width);
  app.H = Math.max(2, rect.height);
}

async function boot() {
  const { Renderer: R } = await import('./renderer.js');
  try {
    app.renderer = await R.create(canvas);
  } catch (err) {
    UI.showFatal(err.message || String(err));
    return;
  }
  resize();
  window.addEventListener('resize', () => { resize(); requestRebuild(); });
  canvas.addEventListener('pointerdown', onPointerDown);
  canvas.addEventListener('pointermove', (e) => { const [x, y] = localXY(e); app.mouse = [x, y]; onPointerMove(e); });
  canvas.addEventListener('pointerleave', () => { app.mouse = null; });
  window.addEventListener('pointerup', onPointerUp);
  canvas.addEventListener('wheel', onWheel, { passive: false });
  canvas.addEventListener('contextmenu', e => e.preventDefault());
  UI.bind(app);
  loadProject(sampleProject());
  requestAnimationFrame(frame);
}

boot();

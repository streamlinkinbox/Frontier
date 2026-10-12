// Editor state + tools. Owns the project model, the undo history, the derived network and the
// overlay (selection handles, draw preview, snap markers). Rendering of the world is delegated
// to render/world.js; this file only decides *what* changes.
import * as THREE from 'three';
import { createModel, makeRoad, makeProp, serialize, parseModel, roadWidth, elevationAt } from '../core/model.js';
import { buildNetwork, layerKey } from '../core/network.js';
import { buildWorld } from '../render/world.js';
import { catmullRom, closestOnPolyline } from '../core/geom.js';
import { terrainY } from '../core/terrain.js';
import { DEFAULTS, PRESETS } from '../core/presets.js';
import { generateCity } from '../core/city.js';

const STORAGE_KEY = 'frontier-road-editor:v1';

export class Editor {
  constructor({ viewer, ui }) {
    this.viewer = viewer;
    this.ui = ui;
    this.model = createModel();
    this.net = null;
    this.undoStack = [];
    this.redoStack = [];
    this.tool = 'select';           // select | draw | props | delete
    this.sel = null;                 // { type: 'road'|'prop', id, point?: index }
    this.draw = { points: [], preset: 'residential', layer: { mode: 'ground', bridge: 'girder', clearance: DEFAULTS.clearance, depth: DEFAULTS.depth, rampLength: DEFAULTS.rampLength } };
    this.propKind = 'bollard';
    this.cursor = null;
    this.snap = null;
    this.drag = null;
    this.overlay = new THREE.Group();
    viewer.overlay.add(this.overlay);
    this.warnings = [];
    this.rebuildPending = false;
    this.bindInput();
  }

  /* ----------------------------------------------------------- project */

  load(model, { remember = true } = {}) {
    this.model = parseModel(JSON.stringify(model));
    this.undoStack = [];
    this.redoStack = [];
    this.sel = null;
    this.draw.points = [];
    this.rebuild();
    if (remember) this.persist();
  }

  newProject() { this.load(createModel('Untitled city')); }

  generate(seed) {
    this.load(generateCity({ seed }));
    this.viewer.focus(0, 0, 0, 900);
    this.ui.toast(`Generated city #${seed}: ${this.model.roads.length} road parts`);
  }

  restore() {
    const text = localStorage.getItem(STORAGE_KEY);
    if (text) {
      try { this.load(JSON.parse(text), { remember: false }); return true; } catch { /* ignore */ }
    }
    return false;
  }

  persist() {
    try { localStorage.setItem(STORAGE_KEY, serialize(this.model)); } catch { /* quota */ }
  }

  /** Apply a model change as one undoable step. */
  mutate(fn) {
    this.undoStack.push(JSON.stringify(this.model));
    if (this.undoStack.length > 200) this.undoStack.shift();
    this.redoStack = [];
    fn();
    this.rebuild();
    this.persist();
  }

  undo() {
    if (!this.undoStack.length) return this.ui.toast('Nothing to undo');
    this.redoStack.push(JSON.stringify(this.model));
    this.model = JSON.parse(this.undoStack.pop());
    this.afterHistory();
  }

  redo() {
    if (!this.redoStack.length) return this.ui.toast('Nothing to redo');
    this.undoStack.push(JSON.stringify(this.model));
    this.model = JSON.parse(this.redoStack.pop());
    this.afterHistory();
  }

  afterHistory() {
    if (this.sel && !this.findSel()) this.sel = null;
    this.rebuild();
    this.persist();
  }

  rebuild() {
    if (this.rebuildPending) return;
    this.rebuildPending = true;
    requestAnimationFrame(() => {
      this.rebuildPending = false;
      const t0 = performance.now();
      this.net = buildNetwork(this.model);
      const buckets = buildWorld(this.model, this.net);
      this.viewer.setWorld(buckets);
      this.buildMs = Math.round(performance.now() - t0);
      this.warnings = this.net.warnings;
      this.ui.onModelChanged();
      this.refreshOverlay();
    });
  }

  /** Synchronous rebuild for tests / immediate needs. */
  rebuildNow() {
    this.net = buildNetwork(this.model);
    this.viewer.setWorld(buildWorld(this.model, this.net));
    this.warnings = this.net.warnings;
    this.refreshOverlay();
  }

  findSel() {
    if (!this.sel) return null;
    if (this.sel.type === 'road') return this.model.roads.find((r) => r.id === this.sel.id) || null;
    return this.model.props.find((p) => p.id === this.sel.id) || null;
  }

  selectedRoad() {
    return this.sel?.type === 'road' ? this.model.roads.find((r) => r.id === this.sel.id) || null : null;
  }

  selectedProp() {
    return this.sel?.type === 'prop' ? this.model.props.find((p) => p.id === this.sel.id) || null : null;
  }

  setTool(tool) {
    this.tool = tool;
    if (tool !== 'draw') this.draw.points = [];
    this.snap = null;
    this.ui.onToolChanged();
    this.refreshOverlay();
  }

  /* ----------------------------------------------------------- picking */

  /** Nearest road (centreline) to a point. Returns {road, net-root, c, dist} or null. */
  pickRoad(p, maxDist = 6) {
    if (!this.net) return null;
    let best = null;
    for (const R of this.net.roads) {
      const bb = R.bbox || (R.bbox = bboxOf(R.pts));
      if (p.x < bb.minX - maxDist - 20 || p.x > bb.maxX + maxDist + 20 || p.z < bb.minZ - maxDist - 20 || p.z > bb.maxZ + maxDist + 20) continue;
      const c = closestOnPolyline(R.pts, R.s, p);
      const elev = elevationAt(R.road, c.s, R.L, c.x, c.z);
      const width = roadWidth(R.road);
      const score = c.dist - Math.max(0, width / 2 - 1.5) - (p.y !== undefined ? 0.02 * Math.min(40, Math.abs(elev - p.y)) : 0);
      if (c.dist > maxDist + width / 2) continue;
      if (!best || score < best.score) best = { road: R.road, c, dist: c.dist, score, L: R.L, elev, width };
    }
    return best;
  }

  pickProp(p, maxDist = 3) {
    let best = null;
    for (const pr of this.model.props) {
      const d = Math.hypot(pr.x - p.x, pr.z - p.z);
      if (d < maxDist && (!best || d < best.d)) best = { prop: pr, d };
    }
    return best?.prop || null;
  }

  /** Control point of the selected road under a point. */
  pickHandle(p) {
    const road = this.selectedRoad();
    if (!road) return -1;
    let best = -1, bd = 4;
    road.points.forEach((q, i) => {
      const d = Math.hypot(q.x - p.x, q.z - p.z);
      if (d < bd) { bd = d; best = i; }
    });
    return best;
  }

  /* ----------------------------------------------------------- input */

  bindInput() {
    const el = this.viewer.renderer.domElement;
    el.addEventListener('pointerdown', (e) => {
      if (e.button !== 0) return;
      const p = this.viewer.pickAt(e.clientX, e.clientY);
      if (!p) return;
      this.onPrimary(p, e);
    });
    el.addEventListener('pointermove', (e) => {
      const p = this.viewer.pickAt(e.clientX, e.clientY);
      if (p) this.cursor = p;
      if (this.drag && p) this.onDragMove(p);
      this.ui.onCursor(p);
      if (this.tool === 'draw' || this.drag) this.refreshOverlay();
    });
    el.addEventListener('pointerup', () => this.onDragEnd());
    el.addEventListener('dblclick', (e) => {
      const p = this.viewer.pickAt(e.clientX, e.clientY);
      if (!p) return;
      if (this.tool === 'draw') this.finishDraw();
      else if (this.tool === 'select') this.insertPointAt(p);
    });
    window.addEventListener('keydown', (e) => this.onKey(e));
  }

  onPrimary(p, e) {
    if (this.tool === 'draw') {
      const s = this.snapPoint(p);
      this.draw.points.push({ x: s.x, z: s.z });
      this.refreshOverlay();
      this.ui.onDrawChanged();
      return;
    }
    if (this.tool === 'props') return this.placeProp(p);
    if (this.tool === 'delete') return this.deleteAt(p);

    // select tool
    const h = this.pickHandle(p);
    const road = this.selectedRoad();
    if (road && h >= 0) {
      this.drag = { kind: 'handle', index: h, before: JSON.stringify(this.model), moved: false };
      return;
    }
    const prop = this.pickProp(p);
    if (prop) {
      this.sel = { type: 'prop', id: prop.id };
      this.drag = { kind: 'prop', id: prop.id, before: JSON.stringify(this.model), moved: false };
      this.ui.onSelectionChanged();
      this.refreshOverlay();
      return;
    }
    const hit = this.pickRoad(p, 6);
    this.sel = hit ? { type: 'road', id: hit.road.id } : null;
    this.ui.onSelectionChanged();
    this.refreshOverlay();
  }

  onDragMove(p) {
    if (this.drag.kind === 'handle') {
      const road = this.selectedRoad();
      if (!road) return;
      const s = this.snapPoint(p, road.id);
      road.points[this.drag.index] = { x: round(s.x), z: round(s.z) };
      this.drag.moved = true;
      this.refreshOverlay();
    } else if (this.drag.kind === 'prop') {
      const prop = this.selectedProp();
      if (!prop) return;
      prop.x = round(p.x); prop.z = round(p.z);
      this.drag.moved = true;
      this.refreshOverlay();
    }
  }

  onDragEnd() {
    if (!this.drag) return;
    const d = this.drag;
    this.drag = null;
    if (!d.moved) return;
    this.undoStack.push(d.before);
    this.redoStack = [];
    this.rebuild();
    this.persist();
  }

  onKey(e) {
    if (e.target && ['INPUT', 'TEXTAREA', 'SELECT'].includes(e.target.tagName)) return;
    const ctrl = e.ctrlKey || e.metaKey;
    if (ctrl && e.code === 'KeyZ' && !e.shiftKey) { e.preventDefault(); return this.undo(); }
    if (ctrl && (e.code === 'KeyY' || (e.code === 'KeyZ' && e.shiftKey))) { e.preventDefault(); return this.redo(); }
    if (ctrl) return;
    switch (e.code) {
      case 'KeyV': return this.setTool('select');
      case 'KeyR': return this.setTool('draw');
      case 'KeyP': return this.setTool('props');
      case 'KeyX': return this.setTool('delete');
      case 'KeyG': return this.ui.toggleUnderground();
      case 'KeyF': { const r = this.selectedRoad(); if (r) { const c = r.points[Math.floor(r.points.length / 2)]; this.viewer.focus(c.x, c.z, 0, 140); } return; }
      case 'Enter': case 'NumpadEnter': if (this.tool === 'draw') this.finishDraw(); return;
      case 'Escape':
        if (this.tool === 'draw' && this.draw.points.length) { this.draw.points = []; this.refreshOverlay(); this.ui.onDrawChanged(); return; }
        this.sel = null; this.ui.onSelectionChanged(); this.refreshOverlay(); return;
      case 'Backspace':
        if (this.tool === 'draw' && this.draw.points.length) { this.draw.points.pop(); this.refreshOverlay(); this.ui.onDrawChanged(); return; }
        if (this.sel) { e.preventDefault(); return this.deleteSelection(); }
        return;
      case 'Delete':
        if (this.sel) return this.deleteSelection();
        return;
      default:
    }
  }

  /* ----------------------------------------------------------- tools */

  /** Snap to network nodes / road bodies (for drawing and dragging). */
  snapPoint(p, ignoreRoadId = null) {
    if (!this.net) return p;
    const key = layerKey({ kind: 'road', layer: this.draw.layer, id: 'tmp' });
    const nodeHit = this.net.nodes.filter((n) => n.degree > 0).reduce((best, n) => {
      const d = Math.hypot(n.x - p.x, n.z - p.z);
      return d < DEFAULTS.snap * 1.4 && (!best || d < best.d) ? { n, d } : best;
    }, null);
    if (nodeHit) {
      this.snap = { x: nodeHit.n.x, z: nodeHit.n.z, kind: 'node' };
      return this.snap;
    }
    let best = null;
    for (const R of this.net.roads) {
      if (R.key !== key || R.road.id === ignoreRoadId || R.road.kind === 'rail') continue;
      const c = closestOnPolyline(R.pts, R.s, p);
      if (c.dist < DEFAULTS.snap && (!best || c.dist < best.dist)) best = c;
    }
    if (best) { this.snap = { x: best.x, z: best.z, kind: 'road' }; return this.snap; }
    this.snap = null;
    return { x: p.x, z: p.z };
  }

  finishDraw() {
    if (this.draw.points.length < 2) return this.ui.toast('A road needs at least two points');
    const d = this.draw;
    const preset = d.preset;
    const road = makeRoad({ points: d.points, preset, layer: { ...d.layer }, kind: PRESETS[preset].kind });
    this.mutate(() => this.model.roads.push(road));
    this.sel = { type: 'road', id: road.id };
    this.draw.points = [];
    this.snap = null;
    this.ui.onDrawChanged();
    this.ui.onSelectionChanged();
    const w = this.net.warnings.filter((x) => x.includes(road.name));
    if (w.length) this.ui.toast(w[0]);
    this.refreshOverlay();
  }

  insertPointAt(p) {
    const hit = this.pickRoad(p, 5);
    const road = hit && hit.road;
    if (!road) return;
    this.sel = { type: 'road', id: road.id };
    const R = this.net.roads.find((r) => r.id === road.id);
    const c = closestOnPolyline(R.pts, R.s, p);
    // insert into the control polygon at the segment nearest to c (by control-point distance)
    let bestI = 0, bestD = Infinity;
    for (let i = 0; i < road.points.length - 1; i++) {
      const a = road.points[i], b = road.points[i + 1];
      const d = distSeg(c, a, b);
      if (d < bestD) { bestD = d; bestI = i; }
    }
    this.mutate(() => road.points.splice(bestI + 1, 0, { x: round(c.x), z: round(c.z) }));
    this.ui.onSelectionChanged();
  }

  placeProp(p) {
    const kind = this.propKind;
    const hit = this.pickRoad(p, 8);
    const needsRoad = kind !== 'streetlamp';
    if (needsRoad && !hit) return this.ui.toast('Click on a road to place this prop');
    const x = hit && needsRoad ? hit.c.x : p.x;
    const z = hit && needsRoad ? hit.c.z : p.z;
    const extra = {};
    if (hit) {
      extra.angle = Math.atan2(hit.c.tangent.z, hit.c.tangent.x);
      extra.width = round(hit.width);
    }
    if (kind === 'bollard') extra.length = 8;
    if (kind === 'speedbump') extra.width = hit ? round(hit.width) : 6;
    if (kind === 'streetlamp') extra.angle = 0;
    const prop = makeProp(kind, x, z, extra);
    if (kind === 'streetlamp' && hit) {
      // lamps stand at the kerb side of the road
      const side = Math.sign((p.x - hit.c.x) * -hit.c.tangent.z + (p.z - hit.c.z) * hit.c.tangent.x) || 1;
      const off = hit.width / 2 + 0.6;
      prop.x = round(hit.c.x - hit.c.tangent.z * side * off);
      prop.z = round(hit.c.z + hit.c.tangent.x * side * off);
    }
    this.mutate(() => this.model.props.push(prop));
    this.sel = { type: 'prop', id: prop.id };
    this.ui.onSelectionChanged();
    this.refreshOverlay();
  }

  deleteAt(p) {
    const prop = this.pickProp(p, 3);
    if (prop) return this.mutate(() => { this.model.props = this.model.props.filter((x) => x.id !== prop.id); });
    const hit = this.pickRoad(p, 6);
    if (hit) {
      this.mutate(() => { this.model.roads = this.model.roads.filter((r) => r.id !== hit.road.id); });
      if (this.sel?.id === hit.road.id) this.sel = null;
      this.ui.onSelectionChanged();
    }
  }

  deleteSelection() {
    if (!this.sel) return;
    const id = this.sel.id;
    if (this.sel.type === 'road') this.mutate(() => { this.model.roads = this.model.roads.filter((r) => r.id !== id); });
    else this.mutate(() => { this.model.props = this.model.props.filter((p) => p.id !== id); });
    this.sel = null;
    this.ui.onSelectionChanged();
    this.refreshOverlay();
  }

  focusSelection() {
    const r = this.selectedRoad();
    const p = this.selectedProp();
    if (r) { const c = r.points[0]; this.viewer.focus(c.x, c.z, 0, 160); }
    if (p) this.viewer.focus(p.x, p.z, 0, 60);
  }

  /* ----------------------------------------------------------- overlay */

  refreshOverlay() {
    const g = this.overlay;
    g.clear();
    const line = (pts, color, opacity = 1) => {
      const geo = new THREE.BufferGeometry().setFromPoints(pts.map((p) => new THREE.Vector3(p.x, p.y, p.z)));
      const mat = new THREE.LineBasicMaterial({ color, transparent: opacity < 1, opacity, depthTest: false });
      const l = new THREE.Line(geo, mat);
      l.renderOrder = 10;
      g.add(l);
    };
    const dot = (p, color, r = 1.2) => {
      const m = new THREE.Mesh(new THREE.SphereGeometry(r, 12, 8), new THREE.MeshBasicMaterial({ color, depthTest: false, transparent: true, opacity: 0.95 }));
      m.position.set(p.x, p.y, p.z);
      m.renderOrder = 11;
      g.add(m);
    };
    const groundY = (x, z) => terrainY(x, z);

    // Selected road: bright centreline + control handles.
    const road = this.selectedRoad();
    if (road && this.tool !== 'draw') {
      const R = this.net?.roads.find((r) => r.id === road.id);
      if (R) line(R.pts.map((p, i) => ({ x: p.x, y: elevationAt(road, R.s[i], R.L, p.x, p.z) + 0.7, z: p.z })), 0xffd34d);
      road.points.forEach((p, i) => dot({ x: p.x, y: groundY(p.x, p.z) + 1.2, z: p.z }, i === 0 || i === road.points.length - 1 ? 0x63ff9a : 0xffd34d, 1.6));
    }
    const prop = this.selectedProp();
    if (prop) {
      const col = 0x63ff9a;
      dot({ x: prop.x, y: groundY(prop.x, prop.z) + 1.5, z: prop.z }, col, 1.8);
    }

    // Draw preview.
    if (this.tool === 'draw') {
      const d = this.draw;
      const ctrl = d.points.slice();
      if (this.cursor) ctrl.push(this.snap ? { x: this.snap.x, z: this.snap.z } : { x: this.cursor.x, z: this.cursor.z });
      if (ctrl.length >= 2) {
        const pts = catmullRom(ctrl, 3).map((p) => ({ x: p.x, y: groundY(p.x, p.z) + 1.2, z: p.z }));
        line(pts, 0x4dd0ff);
      }
      d.points.forEach((p) => dot({ x: p.x, y: groundY(p.x, p.z) + 1.3, z: p.z }, 0x4dd0ff, 1.4));
      if (this.snap) dot({ x: this.snap.x, y: groundY(this.snap.x, this.snap.z) + 1.6, z: this.snap.z }, this.snap.kind === 'node' ? 0xff5ad1 : 0xffffff, 2.2);
    }
    // Cursor marker while dragging a handle.
    if (this.drag?.kind === 'handle' && this.cursor) dot({ x: this.cursor.x, y: groundY(this.cursor.x, this.cursor.z) + 1.4, z: this.cursor.z }, 0xff5ad1, 2);
  }

  /** Current topology stats for the UI. */
  stats() {
    return {
      roads: this.model.roads.length,
      props: this.model.props.length,
      ...(this.net ? this.net.stats : {}),
      buildMs: this.buildMs,
    };
  }
}

function bboxOf(pts) {
  let minX = Infinity, minZ = Infinity, maxX = -Infinity, maxZ = -Infinity;
  for (const p of pts) { minX = Math.min(minX, p.x); maxX = Math.max(maxX, p.x); minZ = Math.min(minZ, p.z); maxZ = Math.max(maxZ, p.z); }
  return { minX, minZ, maxX, maxZ };
}

function distSeg(p, a, b) {
  const dx = b.x - a.x, dz = b.z - a.z;
  const L2 = dx * dx + dz * dz || 1e-9;
  const t = Math.max(0, Math.min(1, ((p.x - a.x) * dx + (p.z - a.z) * dz) / L2));
  return Math.hypot(p.x - (a.x + dx * t), p.z - (a.z + dz * t));
}

const round = (v) => Math.round(v * 100) / 100;

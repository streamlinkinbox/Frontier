// Editor state + operations: selection, tools (select / draw / bulldoze / place), undo history,
// city generation and incremental rebuilds. React subscribes via useEditor().
import { generateCity, DEFAULT_GEN, splitEdgeAt } from '../road/cityGen.js';
import { RoadNetwork } from '../road/network.js';
import { NetworkBuilder } from '../road/builder.js';
import { Centerline } from '../road/centerline.js';
import { PROFILES, getProfile, layout, cloneProfile } from '../road/profiles.js';
import { segIntersect } from '../road/math.js';

const clone = o => (o == null ? null : JSON.parse(JSON.stringify(o)));

export class Editor {
  constructor() {
    this.state = {
      tool: 'select', drawProfile: 'arterial4', drawElevation: 0, snap: true, placeType: null,
      selection: null, hover: null, busy: null, progress: 0, view: 'lit', version: 0,
      settings: { markings: true, props: true, trees: true, shadows: true, hour: 15.5, markingStyle: 'us' },
      gen: { ...DEFAULT_GEN, seed: 1337, size: 4096 },
      library: false, cameraSpeed: 3, toast: null,
      canUndo: false, canRedo: false,
    };
    this.subs = new Set();
    this.undoStack = []; this.redoStack = [];
    this.draft = [];
    this.net = null; this.builder = null; this.world = null;
  }
  subscribe(fn) { this.subs.add(fn); return () => this.subs.delete(fn); }
  set(patch) { this.state = { ...this.state, ...patch }; for (const f of this.subs) f(this.state); }
  bump() { this.set({ version: this.state.version + 1 }); }
  toast(msg, kind = 'info') { this.set({ toast: { msg, kind, t: Date.now() } }); clearTimeout(this._tt); this._tt = setTimeout(() => this.set({ toast: null }), 3200); }

  attach(world) {
    this.world = world;
    world.controls.pickPoint = (x, y) => world.pickSurface(x, y) || world.planePoint(world.ray(x, y), 0);
    world.controls.onChange = () => { if (world.controls.speedIndex !== this.state.cameraSpeed) this.set({ cameraSpeed: world.controls.speedIndex }); };
  }

  // ------------------------------------------------------------------ generation / IO
  async generate(opts = this.state.gen) {
    this.set({ busy: 'Generating city layout', progress: 0.02, selection: null, hover: null });
    await new Promise(r => setTimeout(r, 30));
    const t0 = performance.now();
    const net = generateCity(opts);
    await this.adopt(net);
    this.toast(`Generated ${net.edges.size} segments in ${((performance.now() - t0) / 1000).toFixed(1)} s`);
  }
  async adopt(net) {
    this.net = net;
    this.undoStack = []; this.redoStack = [];
    this.builder = new NetworkBuilder(net, { markings: true, props: true, trees: true });
    await this.builder.buildAll((p, label) => this.set({ busy: label, progress: 0.1 + p * 0.8 }));
    this.set({ busy: 'Uploading geometry', progress: 0.95 });
    await new Promise(r => setTimeout(r, 20));
    this.world.setNetwork(net, this.builder);
    this.world.setSelection(null);
    this.set({ busy: null, progress: 1, canUndo: false, canRedo: false });
    this.bump();
  }
  exportJSON() { return JSON.stringify(this.net.toJSON()); }
  async importJSON(text) {
    const j = JSON.parse(text);
    if (j.format !== 'frontier-roads') throw new Error('Not a Frontier roads file');
    this.set({ busy: 'Loading', progress: 0.05, selection: null });
    await this.adopt(RoadNetwork.fromJSON(j));
  }

  // ------------------------------------------------------------------ history
  begin() {
    if (this.tx) return;
    const tx = this.tx = { nodes: new Map(), edges: new Map() };
    this.net.onTouch = (kind, id) => {
      const m = kind === 'node' ? tx.nodes : tx.edges;
      if (!m.has(id)) m.set(id, clone(kind === 'node' ? this.net.nodes.get(id) : this.net.edges.get(id)));
    };
  }
  affected(tx) {
    const nodes = new Set(tx.nodes.keys()), edges = new Set(tx.edges.keys());
    for (const [id, pre] of tx.edges) { if (pre) { nodes.add(pre.a); nodes.add(pre.b); } const cur = this.net.edges.get(id); if (cur) { nodes.add(cur.a); nodes.add(cur.b); } }
    return { nodes: [...nodes], edges: [...edges] };
  }
  commit(label) {
    const tx = this.tx; if (!tx) return;
    this.tx = null; this.net.onTouch = null;
    if (!tx.nodes.size && !tx.edges.size) return;
    const post = { nodes: new Map(), edges: new Map() };
    for (const id of tx.nodes.keys()) post.nodes.set(id, clone(this.net.nodes.get(id)));
    for (const id of tx.edges.keys()) post.edges.set(id, clone(this.net.edges.get(id)));
    this.undoStack.push({ label, pre: tx, post });
    if (this.undoStack.length > 100) this.undoStack.shift();
    this.redoStack = [];
    this.rebuild(this.affected(tx));
    this.set({ canUndo: true, canRedo: false });
  }
  rebuild({ nodes, edges }) {
    const res = this.builder.rebuild(nodes, edges);
    this.world.applyUpdate(res);
    this.refreshSelection();
    this.bump();
  }
  restore(state) {
    const net = this.net;
    for (const id of state.edges.keys()) net.removeEdge(id);
    for (const [id, n] of state.nodes) { if (!n) { if (net.nodes.has(id)) net.removeNode(id); } else if (net.nodes.has(id)) Object.assign(net.nodes.get(id), n); else net.addNode(n); }
    for (const [, e] of state.edges) if (e && net.nodes.has(e.a) && net.nodes.has(e.b)) net.addEdge(clone(e));
    net.invalidate([...net.edges.keys()].filter(id => state.edges.has(id)));
  }
  undo() {
    const h = this.undoStack.pop(); if (!h) return;
    this.restore(h.pre);
    this.redoStack.push(h);
    this.rebuild(this.affected({ nodes: h.pre.nodes, edges: new Map([...h.pre.edges, ...h.post.edges]) }));
    this.set({ canUndo: this.undoStack.length > 0, canRedo: true });
    this.toast('Undo · ' + h.label);
  }
  redo() {
    const h = this.redoStack.pop(); if (!h) return;
    this.restore(h.post);
    this.undoStack.push(h);
    this.rebuild(this.affected({ nodes: h.post.nodes, edges: new Map([...h.pre.edges, ...h.post.edges]) }));
    this.set({ canUndo: true, canRedo: this.redoStack.length > 0 });
    this.toast('Redo · ' + h.label);
  }
  op(label, fn) { this.begin(); let r; try { r = fn(); } finally { this.commit(label); } return r; }

  // ------------------------------------------------------------------ selection
  select(sel, { focus = false } = {}) {
    this.set({ selection: sel });
    this.world.setSelection(sel);
    if (focus && sel) this.focusSelection();
  }
  refreshSelection() {
    const s = this.state.selection; if (!s) return;
    const exists = s.type === 'edge' ? this.net.edges.has(s.id) : s.type === 'node' ? this.net.nodes.has(s.id) : s.type === 'street' ? s.ids.some(id => this.net.edges.has(id)) : s.type === 'rail' ? this.net.rails.has(s.id) : s.type === 'subway' ? this.net.subways.has(s.id) : true;
    if (!exists) this.select(null); else this.world.setSelection(s.type === 'street' ? { ...s, ids: s.ids.filter(id => this.net.edges.has(id)) } : s);
  }
  focusSelection() {
    const s = this.state.selection; if (!s) return;
    let x, y = 0, z, d = 90;
    if (s.type === 'node') { const n = this.net.nodes.get(s.id); x = n.x; z = n.z; y = n.y; d = 70; }
    else if (s.type === 'edge' || s.type === 'street') {
      const ids = s.type === 'edge' ? [s.id] : s.ids; let mnx = Infinity, mxx = -Infinity, mnz = Infinity, mxz = -Infinity;
      for (const id of ids) { const e = this.net.edges.get(id); if (!e) continue; const c = this.net.centerline(e); for (let i = 0; i < c.X.length; i += 4) { mnx = Math.min(mnx, c.X[i]); mxx = Math.max(mxx, c.X[i]); mnz = Math.min(mnz, c.Z[i]); mxz = Math.max(mxz, c.Z[i]); y = c.Y[i]; } }
      x = (mnx + mxx) / 2; z = (mnz + mxz) / 2; d = Math.max(60, Math.hypot(mxx - mnx, mxz - mnz) * 0.9);
    } else if (s.type === 'rail' || s.type === 'subway') {
      const L = (s.type === 'rail' ? this.net.rails : this.net.subways).get(s.id); const p = L.pts[Math.floor(L.pts.length / 2)]; x = p.x; z = p.z; d = 400; y = s.type === 'subway' ? -L.depth : 0;
    } else return;
    this.world.focus(x, y, z, d);
  }

  // Pick nearest element under a screen point
  pick(cx, cy) {
    const w = this.world, net = this.net;
    if (!net) return null;
    const p = w.pickSurface(cx, cy) || w.planePoint(w.ray(cx, cy), 0);
    if (!p) return null;
    const idx = this.builder.index;
    // nodes: inside junction area
    let bestN = null;
    for (const e of idx.query(p.x, p.z, 30)) for (const nid of [e.a, e.b]) {
      const n = net.nodes.get(nid), J = this.builder.junctions.get(nid);
      if (!J || J.kind === 'continuation') continue;
      const r = J.kind === 'none' || J.kind === 'flat' || J.kind === 'flush' ? 3 : Math.max(5, ...J.ends.map(x => x.setback * 0.85));
      const d = Math.hypot(n.x - p.x, n.z - p.z) + Math.abs(n.y - p.y) * 2;
      if (d < r && (!bestN || d < bestN.d)) bestN = { n, d };
    }
    if (bestN) return { type: 'node', id: bestN.n.id, p };
    const hit = idx.nearestEdge(p.x, p.z, 4, p.y);
    if (hit && hit.d < 2) return { type: 'edge', id: hit.edge.id, s: hit.s, p };
    // rails
    for (const r of net.rails.values()) {
      const rc = this.builder.railCl.get(r.id); if (!rc) continue;
      if (rc.nearest(p.x, p.z).d < 5) return { type: 'rail', id: r.id, p };
    }
    if (w.settings.view === 'underground') for (const s of net.subways.values()) {
      const c = new Centerline(s.pts.map(q => ({ x: q.x, z: q.z }))); if (c.nearest(p.x, p.z).d < 8) return { type: 'subway', id: s.id, p };
    }
    return { type: 'none', p };
  }

  // ------------------------------------------------------------------ operations
  deleteSelection() {
    const s = this.state.selection; if (!s) return;
    const ids = s.type === 'edge' ? [s.id] : s.type === 'street' ? s.ids : null;
    if (ids) this.op('Delete road', () => { for (const id of ids) this.removeEdgeClean(id); });
    else if (s.type === 'node') this.op('Delete junction', () => { const es = this.net.nodeEdges(s.id); for (const e of es) this.removeEdgeClean(e.id); if (this.net.nodes.has(s.id)) this.net.removeNode(s.id); });
    else return;
    this.select(null);
  }
  removeEdgeClean(id) {
    const e = this.net.edges.get(id); if (!e) return;
    this.net.removeEdge(id);
    for (const nid of [e.a, e.b]) {
      if (!this.net.nodes.has(nid)) continue;
      const es = this.net.nodeEdges(nid);
      if (es.length === 0) this.net.removeNode(nid);
      else if (es.length === 2) this.tryMerge(nid);
    }
  }
  // merge two edges at a degree-2 node when they share a profile and run smoothly
  tryMerge(nid) {
    const net = this.net, es = net.nodeEdges(nid);
    if (es.length !== 2 || !net.isContinuation(nid)) return;
    const [e1, e2] = es;
    if (e1.id === e2.id || e1.name !== e2.name) return;
    const n = net.nodes.get(nid);
    const p1 = e1.b === nid ? e1.pts : [...e1.pts].reverse(), a = e1.b === nid ? e1.a : e1.b;
    const p2 = e2.a === nid ? e2.pts : [...e2.pts].reverse(), b = e2.a === nid ? e2.b : e2.a;
    if (a === b) return;
    const L1 = net.centerline(e1).length;
    const f1 = e1.b === nid ? e1.features : e1.features.map(f => ({ ...f, s: L1 - f.s }));
    const f2 = (e2.a === nid ? e2.features : e2.features.map(f => ({ ...f, s: net.centerline(e2).length - f.s }))).map(f => ({ ...f, s: f.s + L1 }));
    // one-way direction must follow e1
    if (layout(getProfile(e1)).oneWay && (e1.b !== nid || e2.a !== nid)) return;
    net.removeEdge(e1.id); net.removeEdge(e2.id); net.removeNode(nid);
    net.addEdge({ a, b, profile: e1.profile, custom: e1.custom, name: e1.name, flags: e1.flags, pts: [...p1, { x: n.x, z: n.z, y: n.y }, ...p2], features: [...f1, ...f2].filter(f => f.type !== 'barrierGap') });
  }
  setEdgeProfile(ids, profileId) {
    this.op('Change road type', () => { for (const id of ids) { const e = this.net.edges.get(id); if (!e) continue; this.net.touchEdge(id); e.profile = profileId; e.custom = null; } });
  }
  setEdgeLanes(id, lanes) {
    const e = this.net.edges.get(id); if (!e) return;
    this.op('Edit lanes', () => { this.net.touchEdge(id); const base = cloneProfile(getProfile(e)); base.lanes = lanes; base.id = 'custom'; base.name = (getProfile(e).name || 'Road').replace(/ \(custom\)$/, '') + ' (custom)'; e.custom = base; });
  }
  resetEdgeLanes(id) { this.op('Reset lanes', () => { this.net.touchEdge(id); this.net.edges.get(id).custom = null; }); }
  setEdgeField(ids, k, v) { this.op('Edit road', () => { for (const id of ids) { const e = this.net.edges.get(id); if (!e) continue; this.net.touchEdge(id); e[k] = v; } }); }
  setNodeField(id, patch, label = 'Edit junction') {
    this.op(label, () => { this.net.touchNode(id); Object.assign(this.net.nodes.get(id), patch); for (const e of this.net.nodeEdges(id)) this.net.touchEdge(e.id); });
  }
  reverseEdge(id) {
    this.op('Reverse direction', () => {
      const e = this.net.edges.get(id); this.net.touchEdge(id);
      const L = this.net.centerline(e).length;
      [e.a, e.b] = [e.b, e.a]; e.pts = [...e.pts].reverse();
      e.features = e.features.map(f => ({ ...f, s: L - f.s, ...(f.side ? { side: -f.side } : {}), ...(f.s0 !== undefined ? { s0: L - f.s1, s1: L - f.s0 } : {}) }));
      this.net.adj.get(e.a); // adjacency sets are node->edge, unaffected by swap
    });
  }
  splitEdge(id, s) {
    let n;
    this.op('Split road', () => { const e = this.net.edges.get(id); n = splitEdgeAt(this.net, e, s ?? this.net.centerline(e).length / 2); });
    if (n) this.select({ type: 'node', id: n.id });
  }
  addFeature(id, f) { this.op('Add ' + f.type, () => { const e = this.net.edges.get(id); this.net.touchEdge(id); e.features = [...e.features, f].sort((a, b) => (a.s ?? a.s0) - (b.s ?? b.s0)); }); }
  removeFeature(id, i) { this.op('Remove feature', () => { const e = this.net.edges.get(id); this.net.touchEdge(id); e.features = e.features.filter((_, k) => k !== i); }); }
  updateFeature(id, i, patch) { this.op('Edit feature', () => { const e = this.net.edges.get(id); this.net.touchEdge(id); e.features = e.features.map((f, k) => (k === i ? { ...f, ...patch } : f)); }); }

  // ------------------------------------------------------------------ drag (node / control point)
  startDrag(kind, ref) { this.begin(); this.drag = { kind, ...ref }; if (kind === 'node') this.net.touchNode(ref.id); else this.net.touchEdge(ref.edge); }
  dragTo(p) {
    const d = this.drag; if (!d) return;
    const net = this.net;
    if (d.kind === 'node') {
      const n = net.nodes.get(d.id); n.x = p.x; n.z = p.z;
      for (const e of net.nodeEdges(d.id)) net.touchEdge(e.id);
      this.liveRebuild([d.id], []);
    } else {
      const e = net.edges.get(d.edge); const q = e.pts[d.ctrl]; q.x = p.x; q.z = p.z;
      net.touchEdge(e.id);
      this.liveRebuild([], [e.id]);
    }
  }
  liveRebuild(nodes, edges) {
    this._live = { nodes, edges };
    if (this._liveRaf) return;
    this._liveRaf = requestAnimationFrame(() => {
      this._liveRaf = null; const L = this._live;
      const res = this.builder.rebuild(L.nodes, L.edges);
      this.world.applyUpdate(res);
      this.world.setSelection(this.state.selection);
    });
  }
  endDrag() { if (!this.drag) return; const k = this.drag.kind; this.drag = null; this.commit(k === 'node' ? 'Move junction' : 'Bend road'); }

  // ------------------------------------------------------------------ draw tool
  snapPoint(cx, cy, forDraw = true) {
    const w = this.world, net = this.net, y0 = this.state.drawElevation;
    const surf = w.pickSurface(cx, cy);
    let p = w.planePoint(w.ray(cx, cy), y0) || surf;
    if (!p) return null;
    if (surf && (y0 === 0 || Math.abs(surf.y - y0) < 2.5)) p = surf;
    const idx = this.builder.index;
    let best = null;
    for (const e of idx.query(p.x, p.z, 25)) for (const nid of [e.a, e.b]) {
      const n = net.nodes.get(nid), d = Math.hypot(n.x - p.x, n.z - p.z);
      if (d < 9 && Math.abs(n.y - p.y) < 3 && (!best || d < best.d)) best = { d, n };
    }
    if (best) return { x: best.n.x, y: best.n.y, z: best.n.z, node: best.n.id, kind: 'node' };
    const hit = idx.nearestEdge(p.x, p.z, 3, p.y);
    if (hit && hit.d < 0.5) {
      const f = net.centerline(hit.edge).frameAt(hit.s);
      if (Math.abs(f.y - p.y) < 3) return { x: f.x, y: f.y, z: f.z, edge: hit.edge.id, s: hit.s, kind: 'edge' };
    }
    let x = p.x, z = p.z;
    if (this.state.snap && forDraw) {
      const last = this.draft[this.draft.length - 1];
      if (last && this.shift) {
        const a = Math.atan2(z - last.z, x - last.x), L = Math.hypot(x - last.x, z - last.z), st = Math.PI / 12;
        const q = Math.round(a / st) * st; x = last.x + Math.cos(q) * L; z = last.z + Math.sin(q) * L;
      } else { x = Math.round(x); z = Math.round(z); }
    }
    return { x, y: y0, z, kind: 'free' };
  }
  draftCurve(pts = this.draft) {
    if (pts.length < 2) return null;
    try { return new Centerline(pts.map(p => ({ x: p.x, z: p.z, y: p.y }))); } catch { return null; }
  }
  updateDraftPreview(cursor) {
    const pts = [...this.draft]; if (cursor) pts.push(cursor);
    const width = layout(PROFILES[this.state.drawProfile]).total;
    const curve = this.draftCurve(pts); if (curve) pts.curve = curve;
    const ok = !cursor || !this.draft.length || Math.hypot(cursor.x - this.draft[this.draft.length - 1].x, cursor.z - this.draft[this.draft.length - 1].z) > 6;
    this.world.setPreview(pts, width, 0x4fd8e0, ok);
    if (cursor) this.world.setHover({ type: 'point', x: cursor.x, y: cursor.y, z: cursor.z, r: cursor.kind === 'free' ? 1.4 : 3, color: cursor.kind === 'node' ? 0x22c55e : cursor.kind === 'edge' ? 0xf59e0b : 0xffffff });
  }
  draftClick(p) {
    const last = this.draft[this.draft.length - 1];
    if (last && Math.hypot(p.x - last.x, p.z - last.z) < 6) return;
    this.draft.push(p);
    if (this.draft.length >= 2 && p.kind !== 'free') { const end = this.commitDraft(); this.draft = end ? [end] : []; }
    this.updateDraftPreview(null);
  }
  cancelDraft() { this.draft = []; this.world.setPreview(null); this.world.setHover(null); }
  finishDraft() { if (this.draft.length >= 2) this.commitDraft(); this.cancelDraft(); }

  // Build nodes/edges for the drafted polyline, auto-creating junctions where it crosses
  // existing roads at the same level (grade-separated crossings become bridges).
  commitDraft() {
    const pts = this.draft; if (pts.length < 2) return null;
    const net = this.net, profile = this.state.drawProfile;
    const curve = this.draftCurve(pts); if (!curve || curve.length < 8) return null;
    let endRef = null;
    this.op('Draw road', () => {
      const resolve = (p) => {
        if (p.kind === 'node' && net.nodes.has(p.node)) return net.nodes.get(p.node);
        if (p.kind === 'edge' && net.edges.has(p.edge)) return splitEdgeAt(net, net.edges.get(p.edge), p.s);
        return net.addNode({ x: p.x, z: p.z, y: p.y });
      };
      const A = resolve(pts[0]);
      const B = resolve(pts[pts.length - 1]);
      // crossings with existing edges
      const hits = [];
      const N = curve.X.length;
      const cand = new Set();
      for (let i = 0; i < N; i += 5) for (const e of this.builder.index.query(curve.X[i], curve.Z[i], 20)) cand.add(e);
      for (const e of cand) {
        if (!net.edges.has(e.id) || [e.a, e.b].some(id => id === A.id || id === B.id)) continue;
        const c = net.centerline(e);
        for (let i = 0; i < N - 1; i++) for (let j = 0; j < c.X.length - 1; j++) {
          const r = segIntersect(curve.X[i], curve.Z[i], curve.X[i + 1], curve.Z[i + 1], c.X[j], c.Z[j], c.X[j + 1], c.Z[j + 1]);
          if (!r) continue;
          const sNew = curve.S[i] + (curve.S[i + 1] - curve.S[i]) * r[0], sOld = c.S[j] + (c.S[j + 1] - c.S[j]) * r[1];
          const yNew = curve.frameAt(sNew).y, yOld = c.frameAt(sOld).y;
          if (Math.abs(yNew - yOld) > 3.5) continue;                       // bridge / underpass
          if (sNew < 10 || sNew > curve.length - 10) continue;
          hits.push({ sNew, edge: e.id, sOld });
        }
      }
      hits.sort((a, b) => a.sNew - b.sNew);
      const chain = [{ node: A, s: 0 }];
      for (const h of hits) {
        if (h.sNew - chain[chain.length - 1].s < 14) continue;
        const e = net.edges.get(h.edge); if (!e) continue;
        const n = splitEdgeAt(net, e, h.sOld);
        chain.push({ node: n, s: h.sNew });
      }
      chain.push({ node: B, s: curve.length });
      const cs = curve.ctrlS;
      const name = this.autoName(profile);
      for (let k = 0; k < chain.length - 1; k++) {
        const s0 = chain[k].s, s1 = chain[k + 1].s;
        const inner = [];
        for (let i = 1; i < pts.length - 1; i++) if (cs[i] > s0 + 4 && cs[i] < s1 - 4) inner.push({ x: pts[i].x, z: pts[i].z, y: pts[i].y });
        if (chain[k].node.id === chain[k + 1].node.id) continue;
        net.addEdge({ a: chain[k].node.id, b: chain[k + 1].node.id, profile, pts: inner, name });
      }
      endRef = { x: B.x, y: B.y, z: B.z, node: B.id, kind: 'node' };
    });
    return endRef;
  }
  autoName(profile) {
    const p = PROFILES[profile];
    const suffix = { highway: 'Highway', arterial: 'Avenue', urban: 'Street', residential: 'Lane', rural: 'Road', active: 'Path' }[p.category] || 'Road';
    this._nameSeq = (this._nameSeq || 0) + 1;
    const names = ['Aurora', 'Beacon', 'Cypress', 'Delta', 'Ember', 'Falcon', 'Granite', 'Harbor', 'Iris', 'Juniper', 'Kestrel', 'Linden', 'Meridian', 'Nova', 'Orchard', 'Pioneer', 'Quarry', 'Ridge', 'Summit', 'Tidal'];
    return names[(this._nameSeq - 1) % names.length] + ' ' + suffix;
  }

  // ------------------------------------------------------------------ place tool (features)
  placeAt(hit) {
    const t = this.state.placeType; if (!t || !hit || hit.type !== 'edge') return;
    const e = this.net.edges.get(hit.id), c = this.net.centerline(e), s = hit.s;
    if (s < 8 || s > c.length - 8) { this.toast('Too close to a junction', 'warn'); return; }
    if (t === 'hump' || t === 'table') this.addFeature(e.id, { type: 'hump', s, kind: t });
    else if (t === 'crosswalk') this.addFeature(e.id, { type: 'crosswalk', s });
    else if (t === 'busstop') { const f = c.frameAt(s); const side = ((hit.p.x - f.x) * f.nx + (hit.p.z - f.z) * f.nz) >= 0 ? 1 : -1; this.addFeature(e.id, { type: 'busstop', s, side }); }
  }

  // ------------------------------------------------------------------ viewport input
  setTool(tool, extra = {}) {
    if (this.state.tool === 'draw' && tool !== 'draw') this.cancelDraft();
    this.world?.setHover(null);
    this.set({ tool, ...extra });
  }
  pointerDown(e) {
    if (!this.net || e.button !== 0 || e.altKey) return false;
    const t = this.state.tool;
    this.downAt = { x: e.clientX, y: e.clientY };
    if (t === 'select') {
      const sel = this.state.selection;
      if (sel && (sel.type === 'node' || sel.type === 'edge')) {
        // handles first
        const p = this.world.planePoint(this.world.ray(e.clientX, e.clientY), sel.type === 'node' ? this.net.nodes.get(sel.id).y : 0);
        if (p && sel.type === 'node') { const n = this.net.nodes.get(sel.id); if (Math.hypot(n.x - p.x, n.z - p.z) < 4.5) { this.startDrag('node', { id: n.id, y: n.y }); return true; } }
        if (p && sel.type === 'edge') {
          const ed = this.net.edges.get(sel.id);
          for (let i = 0; i < ed.pts.length; i++) { const q = ed.pts[i]; const pp = this.world.planePoint(this.world.ray(e.clientX, e.clientY), q.y ?? 0); if (pp && Math.hypot(q.x - pp.x, q.z - pp.z) < 3.5) { this.startDrag('ctrl', { edge: ed.id, ctrl: i, y: q.y ?? 0 }); return true; } }
          for (const nid of [ed.a, ed.b]) { const n = this.net.nodes.get(nid); const pp = this.world.planePoint(this.world.ray(e.clientX, e.clientY), n.y); if (pp && Math.hypot(n.x - pp.x, n.z - pp.z) < 3.5) { this.select({ type: 'node', id: nid }); this.startDrag('node', { id: nid, y: n.y }); return true; } }
        }
      }
      return false;
    }
    return false;
  }
  pointerMove(e) {
    if (!this.net) return;
    if (this.drag) {
      const p = this.world.planePoint(this.world.ray(e.clientX, e.clientY), this.drag.y || 0);
      if (p) { if (this.state.snap) { p.x = Math.round(p.x); p.z = Math.round(p.z); } this.dragTo(p); }
      return;
    }
    if (this.world.controls.mode) return;
    this.shift = e.shiftKey;
    const t = this.state.tool;
    if (t === 'draw') { const p = this.snapPoint(e.clientX, e.clientY); this.cursor = p; if (p) this.cursorPos = p; this.updateDraftPreview(p); return; }
    const h = this.pick(e.clientX, e.clientY);
    if (h?.p) this.cursorPos = h.p;
    const key = h ? h.type + ':' + h.id : '';
    if (t === 'place') {
      if (h?.type === 'edge') this.world.setHover({ type: 'point', x: h.p.x, y: h.p.y, z: h.p.z, r: 2.5, color: 0xf59e0b }); else this.world.setHover(null);
      return;
    }
    if (key !== this._hoverKey) {
      this._hoverKey = key;
      if (h && (h.type === 'edge' || h.type === 'node')) this.world.setHover(h); else this.world.setHover(null);
      this.set({ hover: h && h.type !== 'none' ? { type: h.type, id: h.id } : null });
    }
  }
  pointerUp(e) {
    if (!this.net || e.button !== 0) return;
    if (this.drag) { this.endDrag(); return; }
    if (!this.downAt || Math.hypot(e.clientX - this.downAt.x, e.clientY - this.downAt.y) > 5 || e.altKey) { this.downAt = null; return; }
    this.downAt = null;
    const t = this.state.tool;
    if (t === 'draw') { const p = this.snapPoint(e.clientX, e.clientY); if (p) this.draftClick(p); return; }
    const h = this.pick(e.clientX, e.clientY);
    if (t === 'select') {
      if (!h || h.type === 'none') this.select(null);
      else if ((e.ctrlKey || e.metaKey) && h.type === 'edge') this.select({ type: 'street', ids: [...new Set([...(this.state.selection?.type === 'street' ? this.state.selection.ids : this.state.selection?.type === 'edge' ? [this.state.selection.id] : []), h.id])], name: 'Multiple segments' });
      else this.select({ type: h.type, id: h.id });
    } else if (t === 'bulldoze') {
      if (h?.type === 'edge') this.op('Bulldoze road', () => this.removeEdgeClean(h.id));
      else if (h?.type === 'node') this.op('Bulldoze junction', () => { for (const ed of this.net.nodeEdges(h.id)) this.removeEdgeClean(ed.id); if (this.net.nodes.has(h.id)) this.net.removeNode(h.id); });
      this._hoverKey = null; this.world.setHover(null);
    } else if (t === 'place') this.placeAt(h);
  }
  doubleClick(e) {
    if (this.state.tool === 'draw') { this.finishDraft(); return; }
    if (this.state.tool === 'select') {
      const h = this.pick(e.clientX, e.clientY);
      if (h?.type === 'edge') {
        // insert a bend point
        const ed = this.net.edges.get(h.id), c = this.net.centerline(ed), f = c.frameAt(h.s);
        let at = 0; for (let i = 1; i < c.ctrlS.length - 1; i++) if (c.ctrlS[i] < h.s) at = i;
        this.op('Add bend point', () => { this.net.touchEdge(ed.id); ed.pts.splice(at, 0, { x: f.x, z: f.z, y: ed.pts.length || f.y ? f.y : undefined }); });
        this.select({ type: 'edge', id: ed.id });
      }
    }
  }
  keyDown(e) {
    const tag = document.activeElement?.tagName;
    if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') return;
    const k = e.key.toLowerCase(), mod = e.ctrlKey || e.metaKey;
    if (mod && k === 'z') { e.preventDefault(); e.shiftKey ? this.redo() : this.undo(); return; }
    if (mod && k === 'y') { e.preventDefault(); this.redo(); return; }
    if (mod) return;
    if (this.world?.controls.mode === 'look') return;
    if (k === 'v' || k === '1') this.setTool('select');
    else if (k === 'b' || k === '2') this.setTool('draw');
    else if (k === 'x' || k === '3') this.setTool('bulldoze');
    else if (k === 'f') this.focusSelection();
    else if (k === 'g') this.set({ snap: !this.state.snap });
    else if (k === 'escape') { if (this.state.tool === 'draw' && this.draft.length) this.cancelDraft(); else if (this.state.library) this.set({ library: false }); else if (this.state.tool !== 'select') this.setTool('select'); else this.select(null); }
    else if (k === 'enter') { if (this.state.tool === 'draw') this.finishDraft(); }
    else if (k === 'backspace' && this.state.tool === 'draw') { this.draft.pop(); this.updateDraftPreview(this.cursor); }
    else if (k === 'delete' || k === 'backspace') this.deleteSelection();
    else if (e.key === 'A' && e.shiftKey) this.set({ library: !this.state.library });
  }
}

export const editor = new Editor();

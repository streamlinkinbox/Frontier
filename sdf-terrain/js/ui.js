// ============================================================================
// Frontier SDF Terrain — Studio: node editor, inspector, viewport interaction.
// ============================================================================

import { NODE_DEFS, makeNode, defaultParams, buildFieldEval, collectFx, sanitizeGraph, presetGraph, PRESETS } from './nodes.js';
import { Volume } from './sdf.js';
import { ErosionSim, WindSim } from './erosion.js';
import { generateVolume, generatePreview } from './fieldgen.js';
import { Renderer, Camera } from './render.js';
import { surfaceNets, paintVertices, exportOBJ, exportPLY } from './mesh.js';

// ---------------------------------------------------------------------------
// Node editor (2D canvas)
// ---------------------------------------------------------------------------
const NW = 178, NH_HEAD = 30, NROW = 20;

function nodeHeight(n) {
  const def = NODE_DEFS[n.type];
  const rows = Math.max(def.inputs.length, def.outputs.length, 1);
  return NH_HEAD + rows * NROW + 8;
}
function inSockPos(n, i) { return { x: n.x, y: n.y + NH_HEAD + i * NROW + 6 }; }
function outSockPos(n, i) { return { x: n.x + NW, y: n.y + NH_HEAD + i * NROW + 6 }; }

export class NodeEditor {
  constructor(canvas, menuEl) {
    this.c = canvas;
    this.menu = menuEl;
    this.ctx = canvas.getContext('2d');
    this.graph = { nodes: [], wires: [] };
    this.sel = null;
    this.view = { ox: 30, oy: 30, k: 1 };
    this.dragNode = null; this.panning = null; this.wireDrag = null;
    this.onSelect = null; this.onGraphChange = null; this.onStatus = null;
    this._bind();
    this.resize();
  }
  resize() {
    const r = this.c.getBoundingClientRect();
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    this.c.width = Math.max(2, r.width * dpr);
    this.c.height = Math.max(2, r.height * dpr);
    this.draw();
  }
  setGraph(g) { this.graph = g; this.sel = null; this.draw(); }
  toScreen(x, y) { return { x: x * this.view.k + this.view.ox, y: y * this.view.k + this.view.oy }; }
  toWorld(sx, sy) {
    const r = this.c.getBoundingClientRect();
    return { x: ((sx - r.left) * (this.c.width / r.width)) / this.view.k - this.view.ox / this.view.k,
             y: ((sy - r.top) * (this.c.height / r.height)) / this.view.k - this.view.oy / this.view.k };
  }
  hitNode(w) {
    for (let i = this.graph.nodes.length - 1; i >= 0; i--) {
      const n = this.graph.nodes[i];
      if (w.x >= n.x && w.x <= n.x + NW && w.y >= n.y && w.y <= n.y + nodeHeight(n)) return n;
    }
    return null;
  }
  hitSocket(w, kind) {
    for (const n of this.graph.nodes) {
      const def = NODE_DEFS[n.type];
      if (kind !== 'out') {
        for (let i = 0; i < def.inputs.length; i++) {
          const p = inSockPos(n, i);
          if (Math.hypot(w.x - p.x, w.y - p.y) < 10) return { node: n, sock: def.inputs[i].id, inout: 'in', idx: i };
        }
      }
      if (kind !== 'in') {
        for (let i = 0; i < def.outputs.length; i++) {
          if (def.outputs[i].fxtype) continue; // FX ports are global, not wired
          const p = outSockPos(n, i);
          if (Math.hypot(w.x - p.x, w.y - p.y) < 10) return { node: n, sock: def.outputs[i].id, inout: 'out', idx: i };
        }
      }
    }
    return null;
  }

  _bind() {
    const c = this.c;
    c.addEventListener('contextmenu', (e) => e.preventDefault());
    c.addEventListener('wheel', (e) => {
      e.preventDefault();
      const w = this.toWorld(e.clientX, e.clientY);
      const k0 = this.view.k;
      this.view.k = Math.min(1.8, Math.max(0.35, k0 * (e.deltaY < 0 ? 1.1 : 0.9)));
      // zoom at cursor: keep world point under cursor
      const r = c.getBoundingClientRect();
      const sx = (e.clientX - r.left) * (c.width / r.width);
      const sy = (e.clientY - r.top) * (c.height / r.height);
      this.view.ox = sx - w.x * this.view.k;
      this.view.oy = sy - w.y * this.view.k;
      this.draw();
    }, { passive: false });
    c.addEventListener('pointerdown', (e) => {
      c.setPointerCapture(e.pointerId);
      this.hideMenu();
      const w = this.toWorld(e.clientX, e.clientY);
      if (e.button === 2 || e.button === 1) { this.panning = { x: e.clientX, y: e.clientY }; return; }
      const sock = this.hitSocket(w);
      if (sock && sock.inout === 'out') {
        this.wireDrag = { from: sock.node.id, fromSock: sock.sock, x: w.x, y: w.y };
        this.draw();
        return;
      }
      const n = this.hitNode(w);
      if (n) {
        this.sel = n.id;
        this.dragNode = { n, dx: w.x - n.x, dy: w.y - n.y, moved: false };
        if (this.onSelect) this.onSelect(n);
        this.draw();
      } else {
        this.sel = null;
        if (this.onSelect) this.onSelect(null);
        this.panning = { x: e.clientX, y: e.clientY };
        this.draw();
      }
    });
    c.addEventListener('pointermove', (e) => {
      const w = this.toWorld(e.clientX, e.clientY);
      if (this.wireDrag) {
        this.wireDrag.x = w.x; this.wireDrag.y = w.y;
        this.wireDrag.hover = this.hitSocket(w, 'in');
        this.draw();
      } else if (this.dragNode) {
        this.dragNode.n.x = Math.round(w.x - this.dragNode.dx);
        this.dragNode.n.y = Math.round(w.y - this.dragNode.dy);
        this.dragNode.moved = true;
        this.draw();
      } else if (this.panning) {
        const r = c.getBoundingClientRect();
        const sx = c.width / r.width, sy = c.height / r.height;
        this.view.ox += (e.clientX - this.panning.x) * sx;
        this.view.oy += (e.clientY - this.panning.y) * sy;
        this.panning = { x: e.clientX, y: e.clientY };
        this.draw();
      }
    });
    c.addEventListener('pointerup', (e) => {
      if (this.wireDrag) {
        const w = this.toWorld(e.clientX, e.clientY);
        const tgt = this.hitSocket(w, 'in');
        if (tgt && tgt.node.id !== this.wireDrag.from) {
          // replace existing wire into that socket
          this.graph.wires = this.graph.wires.filter((x) => !(x.to === tgt.node.id && x.toSock === tgt.sock));
          this.graph.wires.push({ from: this.wireDrag.from, fromSock: this.wireDrag.fromSock, to: tgt.node.id, toSock: tgt.sock });
          if (this.onGraphChange) this.onGraphChange('field');
        }
        this.wireDrag = null;
        this.draw();
      }
      this.dragNode = null; this.panning = null;
    });
    c.addEventListener('dblclick', (e) => {
      const w = this.toWorld(e.clientX, e.clientY);
      if (!this.hitNode(w)) this.showMenu(e.clientX, e.clientY, w.x, w.y);
    });
    window.addEventListener('keydown', (e) => {
      if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT') return;
      if ((e.key === 'Delete' || e.key === 'Backspace') && this.sel) {
        const n = this.graph.nodes.find((x) => x.id === this.sel);
        if (n && n.type !== 'Output') {
          this.graph.nodes = this.graph.nodes.filter((x) => x.id !== this.sel);
          this.graph.wires = this.graph.wires.filter((x) => x.from !== this.sel && x.to !== this.sel);
          const wasField = NODE_DEFS[n.type].make;
          this.sel = null;
          if (this.onSelect) this.onSelect(null);
          if (this.onGraphChange) this.onGraphChange(wasField ? 'field' : 'fx');
          this.draw();
        }
      }
      if (e.key === 'Escape') { this.wireDrag = null; this.hideMenu(); this.draw(); }
    });
  }

  showMenu(sx, sy, wx, wy) {
    const m = this.menu;
    m.innerHTML = '';
    const cats = {};
    for (const [type, def] of Object.entries(NODE_DEFS)) {
      if (type === 'Output') continue;
      (cats[def.cat] = cats[def.cat] || []).push([type, def]);
    }
    for (const [cat, list] of Object.entries(cats)) {
      const h = document.createElement('div');
      h.className = 'cat'; h.textContent = cat.toUpperCase();
      m.appendChild(h);
      for (const [type, def] of list) {
        const b = document.createElement('button');
        b.className = 'item';
        b.innerHTML = `<span class="dot" style="background:${def.color}"></span>${def.title}`;
        b.title = def.desc;
        b.onclick = () => {
          const n = makeNode(type, Math.round(wx - NW / 2), Math.round(wy - 20));
          this.graph.nodes.push(n);
          this.hideMenu();
          this.sel = n.id;
          if (this.onSelect) this.onSelect(n);
          if (this.onGraphChange) this.onGraphChange(def.make ? 'field' : 'fx');
          this.draw();
        };
        m.appendChild(b);
      }
    }
    m.hidden = false;
    m.style.left = Math.min(sx, window.innerWidth - 250) + 'px';
    m.style.top = Math.min(sy, window.innerHeight - 400) + 'px';
  }
  hideMenu() { this.menu.hidden = true; }

  draw() {
    const ctx = this.ctx, W = this.c.width, H = this.c.height;
    ctx.clearRect(0, 0, W, H);
    ctx.save();
    ctx.translate(this.view.ox, this.view.oy);
    ctx.scale(this.view.k, this.view.k);
    const byId = new Map(this.graph.nodes.map((n) => [n.id, n]));
    // wires
    for (const w of this.graph.wires) {
      const a = byId.get(w.from), b = byId.get(w.to);
      if (!a || !b) continue;
      const da = NODE_DEFS[a.type], db = NODE_DEFS[b.type];
      const ai = da.outputs.findIndex((o) => o.id === w.fromSock);
      const bi = db.inputs.findIndex((o) => o.id === w.toSock);
      if (ai < 0 || bi < 0) continue;
      const p0 = outSockPos(a, ai), p1 = inSockPos(b, bi);
      this._wire(p0, p1, '#5b97ff');
    }
    if (this.wireDrag) {
      const a = byId.get(this.wireDrag.from);
      if (a) {
        const p0 = outSockPos(a, 0);
        const ok = this.wireDrag.hover;
        this._wire(p0, { x: this.wireDrag.x, y: this.wireDrag.y }, ok ? '#7dff9a' : '#5b97ff', true);
      }
    }
    // nodes
    for (const n of this.graph.nodes) {
      const def = NODE_DEFS[n.type];
      const h = nodeHeight(n);
      const sel = n.id === this.sel;
      ctx.beginPath();
      ctx.roundRect(n.x, n.y, NW, h, 9);
      ctx.fillStyle = sel ? '#1a2230' : '#151a22';
      ctx.fill();
      ctx.lineWidth = sel ? 2.2 : 1.2;
      ctx.strokeStyle = sel ? '#5b97ff' : '#2a313b';
      ctx.stroke();
      // header
      ctx.beginPath();
      ctx.roundRect(n.x, n.y, NW, NH_HEAD, [9, 9, 0, 0]);
      ctx.fillStyle = def.color + '2e';
      ctx.fill();
      ctx.fillStyle = def.color;
      ctx.fillRect(n.x + 9, n.y + 9, 8, 8);
      ctx.fillStyle = '#e8ecf1';
      ctx.font = '600 12px Inter, system-ui, sans-serif';
      ctx.textBaseline = 'middle';
      ctx.fillText(def.title, n.x + 24, n.y + 14, NW - 30);
      // sockets
      ctx.font = '11px Inter, system-ui, sans-serif';
      def.inputs.forEach((s, i) => {
        const p = inSockPos(n, i);
        ctx.beginPath(); ctx.arc(p.x, p.y + 4, 4.5, 0, 7);
        ctx.fillStyle = '#0e1116'; ctx.fill();
        ctx.lineWidth = 1.6; ctx.strokeStyle = '#5b97ff'; ctx.stroke();
        ctx.fillStyle = '#9aa4b2';
        ctx.fillText(s.label, p.x + 9, p.y + 4);
      });
      def.outputs.forEach((s, i) => {
        const p = outSockPos(n, i);
        ctx.fillStyle = '#9aa4b2';
        ctx.textAlign = 'right';
        ctx.fillText(s.label, p.x - 9, p.y + 4);
        ctx.textAlign = 'left';
        ctx.beginPath(); ctx.arc(p.x, p.y + 4, 4.5, 0, 7);
        if (s.fxtype) { ctx.fillStyle = def.color; ctx.fill(); }
        else { ctx.fillStyle = '#0e1116'; ctx.fill(); ctx.lineWidth = 1.6; ctx.strokeStyle = '#5b97ff'; ctx.stroke(); }
      });
    }
    ctx.restore();
  }
  _wire(p0, p1, color, dashed = false) {
    const ctx = this.ctx;
    const dx = Math.max(40, Math.abs(p1.x - p0.x) * 0.5);
    ctx.beginPath();
    ctx.moveTo(p0.x, p0.y + 4);
    ctx.bezierCurveTo(p0.x + dx, p0.y + 4, p1.x - dx, p1.y + 4, p1.x, p1.y + 4);
    ctx.strokeStyle = color;
    ctx.lineWidth = 2;
    if (dashed) ctx.setLineDash([6, 5]);
    ctx.stroke();
    ctx.setLineDash([]);
  }
}

// ---------------------------------------------------------------------------
// Studio
// ---------------------------------------------------------------------------
const $ = (id) => document.getElementById(id);

export class Studio {
  constructor() {
    this.vol = new Volume(128, 2);
    this.sim = new ErosionSim(this.vol);
    this.cam = new Camera();
    this.renderer = new Renderer($('viewport'));
    this.editor = new NodeEditor($('node-canvas'), $('add-menu'));
    this.graph = presetGraph('alpine');
    this.fieldFn = null;
    this.fx = null;
    this.genToken = null;
    this.mode = 'orbit';
    this.paintKind = 'rain';
    this.brushSize = 0.12; this.brushFlow = 0.7;
    this.playing = true;
    this.simSpeed = 1;
    this.painting = false;
    this.hoverPick = null;
    this.msgTimer = 0;
    this.statTimer = 0;
    this.fpsAcc = 0; this.fpsN = 0; this.fpsT = 0;
  }

  log(msg, cls = '') {
    const el = $('log');
    const t = new Date().toTimeString().slice(0, 8);
    const div = document.createElement('div');
    if (cls) div.className = cls;
    div.innerHTML = `<span class="t">${t}</span> ${msg}`;
    el.appendChild(div);
    el.scrollTop = el.scrollHeight;
    while (el.children.length > 220) el.removeChild(el.firstChild);
  }
  status(msg) { $('status').textContent = msg; }

  async boot() {
    const err = this.renderer.init();
    if (err) {
      $('gl-error').hidden = false;
      $('gl-error').textContent = 'Renderer failed: ' + err;
      this.log('Renderer failed: ' + err, 'err');
      return;
    }
    // preset dropdown
    const ps = $('preset');
    for (const p of PRESETS) {
      const o = document.createElement('option');
      o.value = p.id; o.textContent = p.name;
      ps.appendChild(o);
    }
    ps.value = 'alpine';
    ps.onchange = () => this.loadPreset(ps.value);

    this.editor.setGraph(this.graph);
    this.editor.onSelect = (n) => this.showInspector(n);
    this.editor.onGraphChange = (kind) => {
      if (kind === 'field') this.regen();
      else this.applyFx();
    };
    $('btn-add').onclick = (e) => {
      const r = $('node-canvas').getBoundingClientRect();
      const w = this.editor.toWorld(r.left + r.width / 2, r.top + r.height / 2);
      this.editor.showMenu(e.clientX, e.clientY, w.x, w.y);
    };
    document.addEventListener('pointerdown', (e) => {
      if (!this.editor.menu.hidden && !this.editor.menu.contains(e.target) && e.target.id !== 'btn-add') {
        this.editor.hideMenu();
      }
    });

    this._bindTopbar();
    this._bindViewport();
    this._bindRight();
    this.applyFx();
    this.log('Frontier SDF Terrain Studio ready. 100% volumetric — no heightmaps.');
    await this.regen();
    requestAnimationFrame((t) => this.loop(t));
  }

  // -- top bar ------------------------------------------------------------------
  _bindTopbar() {
    $('btn-new').onclick = () => {
      const isl = makeNode('IslandMask', 60, 80);
      const fbm = makeNode('Fbm', 260, 80);
      const out = makeNode('Output', 460, 80);
      const hydro = makeNode('Hydraulic', 60, 280);
      const rain = makeNode('GlobalRain', 260, 280);
      const sat = makeNode('SatShade', 460, 280);
      this.graph = {
        nodes: [isl, fbm, out, hydro, rain, sat],
        wires: [
          { from: isl.id, fromSock: 'f', to: fbm.id, toSock: 'in' },
          { from: fbm.id, fromSock: 'f', to: out.id, toSock: 'f' },
        ],
      };
      this.editor.setGraph(this.graph);
      this.showInspector(null);
      this.regen();
      this.log('New graph.');
    };
    $('btn-export').onclick = () => {
      const blob = new Blob([JSON.stringify(this.graph, null, 1)], { type: 'application/json' });
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = 'terrain-graph.json';
      a.click();
      URL.revokeObjectURL(a.href);
      this.log('Graph exported.');
    };
    $('btn-import').onclick = () => $('file-import').click();
    $('file-import').onchange = (e) => {
      const f = e.target.files[0];
      if (!f) return;
      const rd = new FileReader();
      rd.onload = () => {
        try {
          this.graph = sanitizeGraph(JSON.parse(rd.result));
          this.editor.setGraph(this.graph);
          this.showInspector(null);
          this.regen();
          this.log(`Graph imported (${this.graph.nodes.length} nodes).`);
        } catch (err) { this.log('Import failed: ' + err.message, 'err'); }
      };
      rd.readAsText(f);
      e.target.value = '';
    };
    $('res').onchange = (e) => {
      const N = parseInt(e.target.value, 10);
      this.vol = new Volume(N, 2);
      this.sim = new ErosionSim(this.vol);
      this.applyFx();
      this.regen();
      this.log(`Volume resolution → ${N}³.`);
    };
    $('btn-regen').onclick = () => this.regen();
    $('btn-play').onclick = () => this.togglePlay();
    window.addEventListener('keydown', (e) => {
      if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT') return;
      if (e.code === 'Space') { e.preventDefault(); this.togglePlay(); }
      if (e.key === 'v' || e.key === 'V') this.setMode('orbit');
      if (e.key === 'b' || e.key === 'B') this.setMode('paint');
    });
    $('sim-speed').onchange = (e) => { this.simSpeed = parseFloat(e.target.value); };
    $('btn-bake').onclick = () => this.bakeAsync(10);
    $('btn-obj').onclick = () => this.exportMesh('obj');
    $('btn-ply').onclick = () => this.exportMesh('ply');
    $('btn-png').onclick = () => {
      const r = this.renderer;
      const prev = r.renderScale;
      r.renderScale = 1;
      r.render(this.cam, this.sim.hydro);
      const url = r.canvas.toDataURL('image/png');
      r.renderScale = prev;
      const a = document.createElement('a');
      a.href = url; a.download = 'frontier-terrain.png'; a.click();
      this.log('Viewport screenshot saved.');
    };
  }
  togglePlay() {
    this.playing = !this.playing;
    this.sim.running = this.playing;
    $('btn-play').textContent = this.playing ? '⏸ Pause' : '▶ Play';
  }

  // -- viewport --------------------------------------------------------------------
  _bindViewport() {
    const cv = $('viewport');
    cv.addEventListener('contextmenu', (e) => e.preventDefault());
    let orbiting = null, panning = null;
    cv.addEventListener('pointerdown', (e) => {
      cv.setPointerCapture(e.pointerId);
      if (this.mode === 'paint' && e.button === 0) {
        this.painting = true;
        this.paintAt(e);
      } else if (e.button === 2 || e.button === 1) {
        panning = { x: e.clientX, y: e.clientY };
      } else {
        orbiting = { x: e.clientX, y: e.clientY };
      }
    });
    cv.addEventListener('pointermove', (e) => {
      if (this.painting) { this.paintAt(e); return; }
      if (orbiting) {
        this.cam.yaw -= (e.clientX - orbiting.x) * 0.006;
        this.cam.pitch = Math.min(1.45, Math.max(-0.2, this.cam.pitch + (e.clientY - orbiting.y) * 0.006));
        orbiting = { x: e.clientX, y: e.clientY };
      } else if (panning) {
        const s = this.cam.dist * 0.0016;
        const b = this.cam.basis({});
        this.cam.target.x -= ((e.clientX - panning.x) * b.right.x - (e.clientY - panning.y) * b.up.x) * s;
        this.cam.target.y -= ((e.clientX - panning.x) * b.right.y - (e.clientY - panning.y) * b.up.y) * s;
        this.cam.target.z -= ((e.clientX - panning.x) * b.right.z - (e.clientY - panning.y) * b.up.z) * s;
        panning = { x: e.clientX, y: e.clientY };
      }
      if (this.mode === 'paint') this.hoverAt(e);
    });
    const up = () => { orbiting = null; panning = null; this.painting = false; };
    cv.addEventListener('pointerup', up);
    cv.addEventListener('pointerleave', () => { up(); this.hoverPick = null; this.renderer.brush.r = -1; });
    cv.addEventListener('wheel', (e) => {
      e.preventDefault();
      this.cam.dist = Math.min(7, Math.max(0.7, this.cam.dist * (e.deltaY > 0 ? 1.09 : 0.92)));
    }, { passive: false });

    $('mode-seg').querySelectorAll('button').forEach((b) => {
      b.onclick = () => this.setMode(b.dataset.mode);
    });
    $('paint-seg').querySelectorAll('button').forEach((b) => {
      b.onclick = () => {
        this.paintKind = b.dataset.paint;
        $('paint-seg').querySelectorAll('button').forEach((x) => x.classList.toggle('on', x === b));
      };
    });
    $('brush-size').oninput = (e) => { this.brushSize = parseFloat(e.target.value); };
    $('brush-flow').oninput = (e) => { this.brushFlow = parseFloat(e.target.value); };
    $('tgl-rain').onclick = (e) => {
      this.renderer.showRain = !this.renderer.showRain;
      e.target.classList.toggle('on', this.renderer.showRain);
    };
    $('tgl-emit').onclick = (e) => {
      this.renderer.showEmit = !this.renderer.showEmit;
      e.target.classList.toggle('on', this.renderer.showEmit);
    };
  }
  setMode(m) {
    this.mode = m;
    $('mode-seg').querySelectorAll('button').forEach((x) => x.classList.toggle('on', x.dataset.mode === m));
    $('paint-seg').hidden = m !== 'paint';
    $('brush-ctl').hidden = m !== 'paint';
    if (m !== 'paint') this.renderer.brush.r = -1;
    $('viewport').style.cursor = m === 'paint' ? 'crosshair' : 'grab';
  }

  pick(e) {
    const r = $('viewport').getBoundingClientRect();
    const nx = ((e.clientX - r.left) / r.width) * 2 - 1;
    const ny = -(((e.clientY - r.top) / r.height) * 2 - 1);
    const ray = this.cam.ray(nx, ny, r.width / r.height, {});
    return this.vol.raycast(ray.ox, ray.oy, ray.oz, ray.dx, ray.dy, ray.dz, 9, {});
  }
  hoverAt(e) {
    this.hoverPick = this.pick(e);
    if (this.hoverPick.hit) {
      const b = this.renderer.brush;
      b.x = this.hoverPick.x; b.y = this.hoverPick.y; b.z = this.hoverPick.z;
      b.r = this.brushSize;
    } else {
      this.renderer.brush.r = -1;
    }
  }
  paintAt(e) {
    const hit = this.pick(e);
    if (!hit.hit) return;
    const V = this.vol;
    // push slightly into the surface so the stamp overlaps solid voxels
    const g = { x: 0, y: 0, z: 0 };
    V.gradient(hit.x, hit.y, hit.z, g);
    const px = hit.x - g.x * V.voxel * 0.6;
    const py = hit.y - g.y * V.voxel * 0.6;
    const pz = hit.z - g.z * V.voxel * 0.6;
    if (this.paintKind === 'rain') V.paintAttr(V.attrA, 3, px, py, pz, this.brushSize, 1, this.brushFlow);
    else if (this.paintKind === 'erase') V.paintAttr(V.attrA, 3, px, py, pz, this.brushSize, 0, this.brushFlow);
    else if (this.paintKind === 'harden') V.paintAttr(V.attrB, 0, px, py, pz, this.brushSize, 1, this.brushFlow);
    else if (this.paintKind === 'soften') V.paintAttr(V.attrB, 0, px, py, pz, this.brushSize, 0.05, this.brushFlow);
    this.hoverAt(e);
  }

  // -- right panel -------------------------------------------------------------------
  _bindRight() {
    $('btn-clear-emit').onclick = () => {
      const A = this.vol.attrA;
      for (let n = 0; n < this.vol.n3; n++) A[n * 4 + 3] = 0;
      this.vol.markAllDirty();
      this.log('Rain paint cleared.');
    };
    $('btn-clear-stain').onclick = () => {
      const A = this.vol.attrA;
      for (let n = 0; n < this.vol.n3; n++) { A[n * 4] = 0; A[n * 4 + 1] = 0; A[n * 4 + 2] = 0; }
      this.vol.markAllDirty();
      this.log('Sediment / wetness stains washed (carving kept).');
    };
  }

  showInspector(node) {
    const el = $('inspector');
    el.innerHTML = '';
    if (!node) {
      el.innerHTML = `<div class="empty">Select a node to edit its parameters.<br /><br />Field nodes (Source / Noise / Shape / Op) regenerate the SDF volume.<br />Erode nodes drive the realtime simulation — no regen needed.</div>`;
      return;
    }
    const def = NODE_DEFS[node.type];
    const title = document.createElement('div');
    title.className = 'insp-title';
    title.innerHTML = `<span class="dot" style="background:${def.color}"></span>${def.title}`;
    el.appendChild(title);
    const desc = document.createElement('div');
    desc.className = 'insp-desc';
    desc.textContent = def.desc;
    el.appendChild(desc);
    const isField = !!def.make && node.type !== 'Output';
    for (const pr of def.params) {
      const row = document.createElement('div');
      row.className = 'prow';
      const lab = document.createElement('label');
      lab.textContent = pr.label;
      row.appendChild(lab);
      const ctl = document.createElement('div');
      const val = node.params[pr.id];
      if (pr.type === 'float' || pr.type === 'int') {
        const wrap = document.createElement('div');
        wrap.style.display = 'flex'; wrap.style.gap = '6px'; wrap.style.alignItems = 'center';
        const sl = document.createElement('input');
        sl.type = 'range';
        sl.min = pr.min; sl.max = pr.max; sl.step = pr.step; sl.value = val;
        sl.style.flex = '1'; sl.style.minWidth = '0';
        const num = document.createElement('input');
        num.className = 'val';
        num.type = 'number';
        num.min = pr.min; num.max = pr.max; num.step = pr.step; num.value = val;
        const commit = (v, final) => {
          let vv = pr.type === 'int' ? Math.round(v) : v;
          vv = Math.min(pr.max, Math.max(pr.min, vv));
          node.params[pr.id] = vv;
          sl.value = vv; num.value = vv;
          if (!isField) this.applyFx();
          else if (final) this.regen();
          else this.regenPreview();
        };
        sl.oninput = () => commit(parseFloat(sl.value), false);
        sl.onchange = () => commit(parseFloat(sl.value), true);
        num.onchange = () => commit(parseFloat(num.value), true);
        wrap.appendChild(sl); wrap.appendChild(num);
        // grid is 2-col; span both for slider rows
        row.style.gridTemplateColumns = '1fr';
        lab.style.marginBottom = '2px';
        ctl.appendChild(wrap);
      } else if (pr.type === 'seed') {
        const wrap = document.createElement('div');
        wrap.className = 'seedrow';
        const num = document.createElement('input');
        num.type = 'number'; num.step = '1'; num.value = val;
        num.style.width = '100%';
        num.onchange = () => { node.params[pr.id] = parseInt(num.value, 10) | 0; isField ? this.regen() : this.applyFx(); };
        const dice = document.createElement('button');
        dice.className = 'dice'; dice.textContent = '🎲'; dice.title = 'Randomize seed';
        dice.onclick = () => {
          node.params[pr.id] = (Math.random() * 1e9) | 0;
          num.value = node.params[pr.id];
          isField ? this.regen() : this.applyFx();
        };
        wrap.appendChild(num); wrap.appendChild(dice);
        row.style.gridTemplateColumns = '86px 1fr';
        ctl.appendChild(wrap);
      } else if (pr.type === 'choice') {
        const sel = document.createElement('select');
        pr.options.forEach((o, i) => {
          const op = document.createElement('option');
          op.value = i; op.textContent = o;
          sel.appendChild(op);
        });
        sel.value = val;
        sel.onchange = () => { node.params[pr.id] = parseInt(sel.value, 10); isField ? this.regen() : this.applyFx(); };
        row.style.gridTemplateColumns = '86px 1fr';
        ctl.appendChild(sel);
      } else if (pr.type === 'bool') {
        const cb = document.createElement('input');
        cb.type = 'checkbox'; cb.checked = !!val;
        cb.onchange = () => { node.params[pr.id] = cb.checked; isField ? this.regen() : this.applyFx(); };
        ctl.appendChild(cb);
      }
      row.appendChild(ctl);
      el.appendChild(row);
    }
  }

  // -- graph / generation ---------------------------------------------------------------
  loadPreset(id) {
    this.graph = presetGraph(id);
    this.editor.setGraph(this.graph);
    this.showInspector(null);
    this.applyFx();
    this.regen();
    this.log(`Preset “${PRESETS.find((p) => p.id === id).name}” loaded.`);
  }

  applyFx() {
    try {
      this.fx = collectFx(this.graph);
    } catch (e) {
      this.log('FX collect failed: ' + e.message, 'err');
      return;
    }
    this.sim.setFx(this.fx);
    this.renderer.setMaterial(this.fx.material);
  }

  progress(p, label) {
    $('progress').hidden = p === null;
    if (p !== null) {
      $('progress-fill').style.setProperty('--p', p.toFixed(3));
      $('progress-label').textContent = label || `${(p * 100).toFixed(0)}%`;
    }
  }

  compileField() {
    try {
      this.fieldFn = buildFieldEval(this.graph);
      return true;
    } catch (e) {
      this.log('Graph compile failed: ' + e.message, 'err');
      return false;
    }
  }

  // Fast low-res preview while dragging sliders (debounced, cancellable).
  regenPreview() {
    if (this._pvTimer) clearTimeout(this._pvTimer);
    this._pvTimer = setTimeout(async () => {
      if (this._regenBusy) { this._regenQueued = true; return; }
      if (!this.compileField()) return;
      if (this.genToken) this.genToken.cancelled = true;
      const token = { cancelled: false };
      this.genToken = token;
      const wasPlaying = this.playing;
      this.sim.running = false;
      this.progress(0, 'preview…');
      const ok = await generatePreview(this.vol, this.fieldFn, {
        onProgress: (p) => this.progress(p, 'preview…'),
        token,
      });
      if (ok && !token.cancelled) {
        this.renderer.bindVolume(this.vol);
        this.sim.onRegenerated();
      }
      this.progress(null);
      this.sim.running = wasPlaying && this.playing;
    }, 140);
  }

  async regen() {
    if (this._regenBusy) { this._regenQueued = true; return; }
    this._regenBusy = true;
    if (this._pvTimer) clearTimeout(this._pvTimer);
    if (this.genToken) this.genToken.cancelled = true;
    const token = { cancelled: false };
    this.genToken = token;
    if (!this.compileField()) { this._regenBusy = false; return; }
    this.applyFx();
    const wasPlaying = this.playing;
    this.sim.running = false;
    // Stage 1: instant preview so sliders feel alive…
    this.progress(0, 'preview…');
    const pvOk = await generatePreview(this.vol, this.fieldFn, {
      onProgress: (p) => { if (!token.cancelled) this.progress(p * 0.25, 'preview…'); },
      token,
    });
    if (pvOk && !token.cancelled) {
      this.renderer.bindVolume(this.vol);
      this.sim.onRegenerated();
    }
    // Stage 2: full-quality field.
    if (!token.cancelled) {
      this.progress(0.25, 'generating…');
      const ok = await generateVolume(this.vol, this.fieldFn, {
        onProgress: (p) => { if (!token.cancelled) this.progress(0.25 + p * 0.75, 'generating…'); },
        token,
      });
      if (ok && !token.cancelled) {
        this.renderer.bindVolume(this.vol);
        this.sim.onRegenerated();
        this.status(`Terrain ready · ${(this.vol.N ** 3 / 1e6).toFixed(1)}M voxels · gen #${this.vol.generation}`);
        this.log(`Terrain regenerated @${this.vol.N}³.`);
      }
    }
    this.progress(null);
    this.sim.running = wasPlaying && this.playing;
    this._regenBusy = false;
    if (this._regenQueued) { this._regenQueued = false; this.regen(); }
  }

  async bakeAsync(seconds) {
    if (this._baking) return;
    if (this._regenBusy) { this.log('Wait for terrain generation to finish first.', 'warn'); return; }
    this._baking = true;
    this.log(`Baking ${seconds} s of erosion…`);
    const dt = 1 / 30;
    const steps = Math.ceil(seconds / dt);
    const wasPlaying = this.playing;
    this.sim.running = true;
    for (let s = 0; s < steps; s++) {
      this.sim.step(dt);
      if ((s & 3) === 3) {
        this.progress(s / steps, `baking erosion… ${(this.sim.hydro.alive / 1000).toFixed(1)}k drops`);
        this.renderer.syncDirty(900);
        this.renderer.render(this.cam, this.sim.hydro);
        await new Promise((r) => setTimeout(r, 0));
      }
    }
    this.progress(null);
    this.sim.running = wasPlaying && this.playing;
    this._baking = false;
    this.log(`Bake done: carved ${this.vol.carvedVolume.toFixed(3)} / deposited ${this.vol.depositedVolume.toFixed(3)} world³.`);
  }

  async exportMesh(fmt) {
    this.progress(0.02, 'meshing…');
    this.status('Extracting surface (surface nets)…');
    await new Promise((r) => setTimeout(r, 30));
    try {
      const mesh = surfaceNets(this.vol);
      this.progress(0.6, 'painting verts…');
      await new Promise((r) => setTimeout(r, 10));
      const colors = paintVertices(this.vol, mesh, this.fx.material);
      const text = fmt === 'obj' ? exportOBJ(mesh, colors) : exportPLY(mesh, colors);
      const blob = new Blob([text], { type: 'text/plain' });
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = `frontier-terrain.${fmt}`;
      a.click();
      URL.revokeObjectURL(a.href);
      this.log(`Mesh exported: ${(mesh.positions.length / 3).toLocaleString()} verts, ${(mesh.indices.length / 3).toLocaleString()} tris (.${fmt}).`);
      this.status('Mesh exported.');
    } catch (e) {
      this.log('Mesh export failed: ' + e.message, 'err');
    }
    this.progress(null);
  }

  // -- main loop ----------------------------------------------------------------------------
  loop(t) {
    requestAnimationFrame((tt) => this.loop(tt));
    const dt = Math.min(0.1, this._lastT ? (t - this._lastT) / 1000 : 1 / 60);
    this._lastT = t;
    // fps + auto quality (raymarch cost scales with pixels)
    this.fpsAcc += dt; this.fpsN++;
    this._dtEma = this._dtEma ? this._dtEma * 0.95 + dt * 0.05 : dt;
    if (t - this.fpsT > 500) {
      const fps = this.fpsN / Math.max(1e-3, this.fpsAcc);
      $('fps').textContent = `${fps.toFixed(0)} fps`;
      this.fpsAcc = 0; this.fpsN = 0; this.fpsT = t;
      const r = this.renderer;
      if (!this._regenBusy && t > 8000) { // settle first
        if (this._dtEma > 0.045 && r.renderScale > 0.45) {
          r.renderScale = Math.max(0.45, r.renderScale - 0.15);
          this.log(`Auto quality → ${Math.round(r.renderScale * 100)}% (holding framerate).`);
        } else if (this._dtEma < 0.017 && r.renderScale < 0.8) {
          r.renderScale = Math.min(0.8, r.renderScale + 0.1);
        }
      }
    }
    if (this.playing && !this._regenBusy && !this._baking) {
      this.sim.step(dt * this.simSpeed);
    } else {
      this.sim.cache.update(1 / 5);
    }
    this.renderer.syncDirty(700);
    this.renderer.render(this.cam, this.sim.hydro);
    if (t - this.statTimer > 250) {
      this.statTimer = t;
      this.updateStats(dt);
    }
    if (this.editor.c.width === 2) this.editor.resize();
  }

  updateStats(dt) {
    const s = this.sim.stats;
    const V = this.vol;
    $('hud').textContent =
      `drops ${s.particles.toLocaleString()}   carved ${V.carvedVolume.toFixed(3)}   deposited ${V.depositedVolume.toFixed(3)}\n` +
      `hydro ${s.hydroMs.toFixed(1)}ms  therm ${s.thermalMs.toFixed(1)}ms  wind ${s.windMs.toFixed(1)}ms  chem ${s.chemMs.toFixed(1)}ms`;
    $('vol-stats').textContent =
      `${V.N}³ · dirty ${V.dirtyBricks.size} · surf ${(this.sim.cache.surface.length / 1000).toFixed(1)}k · emit ${(this.sim.cache.emitters.length / 1000).toFixed(1)}k`;
    const fx = this.fx || {};
    $('sim-stats').innerHTML =
      `drops alive      <b>${s.particles.toLocaleString()}</b>\n` +
      `hydro / therm    <b>${s.hydroMs.toFixed(1)} / ${s.thermalMs.toFixed(1)} ms</b>\n` +
      `wind / chem      <b>${s.windMs.toFixed(1)} / ${s.chemMs.toFixed(1)} ms</b>\n` +
      `carved / placed  <b>${V.carvedVolume.toFixed(3)} / ${V.depositedVolume.toFixed(3)}</b>\n` +
      `modules          <b>${['hydro', 'thermal', 'wind', 'chem'].filter((k) => fx[k] && fx[k].enabled !== false).join(' ') || '—'}</b>`;
  }
}

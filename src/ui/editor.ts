// ---------------------------------------------------------------------------
// Frontier UI / node editor canvas
// Pan/zoom dotted canvas, rounded node cards with IN/OUT port columns, bezier
// wires coloured by port type, selection context toolbar, library panel —
// laid out to match the reference screenshots.
// ---------------------------------------------------------------------------

import { Graph, GraphNode, GraphEdge, PORT_COLORS, PortType } from '../graph/types';
import { defFor, NODE_DEFS, CATEGORIES } from '../graph/registry';
import { makeNode, makeEdge, topoOrder } from '../graph/graph';

export interface GuardBadge { ok: boolean; reason: string; requiredRes: number }

export interface EditorApp {
  graph: Graph;
  selectedId: string | null;
  markDirty(structural: boolean): void;
  onNodeMoved(): void;
  onToolbar(act: string): void;
  flags: { wireframe: boolean; textured: boolean; autoCook: boolean };
  onSelect(id: string | null): void;
  badges: Map<string, GuardBadge>;
  onRaiseRes(res: number): void;
  cook(): void;
  toggleInspector(open?: boolean): void;
  inspectorOpen: boolean;
}

const NODE_W = 172;

export class NodeEditor {
  wrap: HTMLElement;
  layer: HTMLElement;
  svg: SVGSVGElement;
  wireGroup: SVGGElement;
  ctxBar: HTMLElement;
  library: HTMLElement;
  progress: HTMLElement;
  app: EditorApp;
  private nodeEls = new Map<string, HTMLElement>();
  private pan = { x: 40, y: 20 };
  private zoom = 0.62;
  private dragState:
    | { kind: 'pan'; sx: number; sy: number; px: number; py: number }
    | { kind: 'node'; id: string; sx: number; sy: number; nx: number; ny: number }
    | { kind: 'wire'; from: string; fromPort: string; type: PortType; x: number; y: number }
    | null = null;
  private tmpPath: SVGPathElement | null = null;
  private snapGrid = true;
  private libraryOpen = false;

  constructor(wrap: HTMLElement, app: EditorApp) {
    this.wrap = wrap;
    this.app = app;
    wrap.innerHTML = `
      <svg id="wires"><g></g></svg>
      <div id="nodes-layer"></div>
      <div id="ctx-toolbar">
        <button data-act="collapse" title="Collapse">−</button>
        <button data-act="mute" title="Mute / bypass">👁</button>
        <button data-act="frame" title="Frame node">⌖</button>
        <button data-act="lock" title="Lock position">🔒</button>
        <button data-act="rename" title="Rename">T</button>
        <button data-act="fit" title="Fit graph">🔍</button>
        <button data-act="delete" class="danger" title="Delete node">✕</button>
      </div>
      <div id="editor-toolbar">
        <div class="pill">
          <button class="primary" data-act="library" title="Node library">▦</button>
          <button data-act="inspector" title="Inspector">≡</button>
          <span class="div"></span>
          <button data-act="cook" title="Cook graph (Ctrl+Enter)">▷</button>
          <span class="div"></span>
          <button data-act="new" title="New graph">🗎</button>
          <button data-act="open" title="Open graph (.json)">📂</button>
          <button data-act="save" title="Save graph (.json)">💾</button>
          <button data-act="undo" title="Undo (Ctrl+Z)">↩</button>
          <button data-act="redo" title="Redo (Ctrl+Shift+Z)">↪</button>
          <span class="div"></span>
          <button data-act="wire" title="Wireframe">⬢</button>
          <button data-act="tex" title="Splatmap shading">🖵</button>
          <button data-act="snap" title="Snapshot PNG">📷</button>
          <button data-act="auto" title="Auto-cook on edit">⚙</button>
        </div>
      </div>
      <button id="settings-btn" title="Canvas settings">⚙</button>
      <div id="settings-panel">
        <div class="sp-section">VIEW</div>
        <div class="sp-row" data-act="canvas-controls"><span>Canvas Controls</span><span class="switch on"></span></div>
        <div class="sp-section" style="margin-top:12px">BACKGROUND</div>
        <div class="sp-row sel" data-bg="dotted"><span>Dotted Grid</span><span class="check">✓</span></div>
        <div class="sp-row" data-bg="blank"><span>Blank</span><span></span></div>
      </div>
      <div id="library-panel">
        <input id="library-search" placeholder="Search nodes..." />
        <div id="library-list"></div>
      </div>
      <div id="progress-pill">
        <div class="pp-top"><span id="pp-label">Built</span><span id="pp-pct">100%</span></div>
        <div class="pp-bar"><div class="pp-fill" id="pp-fill"></div></div>
      </div>
    `;
    this.layer = wrap.querySelector('#nodes-layer')!;
    this.svg = wrap.querySelector('#wires')!;
    this.wireGroup = this.svg.querySelector('g')!;
    this.ctxBar = wrap.querySelector('#ctx-toolbar')!;
    this.library = wrap.querySelector('#library-panel')!;
    this.progress = wrap.querySelector('#progress-pill')!;

    this.bindCanvas();
    this.bindChrome();
    this.buildLibrary('');
    this.render();
  }

  // ------------------------------------------------------------------ render
  render(): void {
    this.layer.innerHTML = '';
    this.nodeEls.clear();
    for (const n of this.app.graph.nodes) this.layer.appendChild(this.nodeEl(n));
    this.applyTransform();
    this.updateWires();
    this.updateBadges();
    this.updateSelection();
  }

  private nodeEl(n: GraphNode): HTMLElement {
    const def = defFor(n.type);
    const el = document.createElement('div');
    el.className = 'node';
    el.dataset.id = n.id;
    el.style.left = `${n.x}px`;
    el.style.top = `${n.y}px`;
    if (n.muted) el.classList.add('muted');
    if (n.collapsed) el.classList.add('collapsed');

    const inputs = def.inputs.map((p) =>
      `<div class="port in" data-port="${p.name}" data-dir="in" data-type="${p.type}">
         <span class="dot" style="background:${PORT_COLORS[p.type]}"></span><span>${p.name}</span>
       </div>`).join('');
    const outputs = def.outputs.map((p) =>
      `<div class="port out" data-port="${p.name}" data-dir="out" data-type="${p.type}">
         <span>${p.name}</span><span class="dot" style="background:${PORT_COLORS[p.type]}"></span>
       </div>`).join('');

    el.innerHTML = `
      <div class="node-head">
        <div class="node-ico">${def.icon}</div>
        <div>
          <div class="node-title">${n.rename ?? def.label}</div>
          <div class="node-sub">${def.subtitle}</div>
        </div>
      </div>
      <div class="node-body">
        <div class="port-col in"><div class="port-head">IN</div>${inputs}</div>
        <div class="port-col out"><div class="port-head">OUT</div>${outputs}</div>
      </div>
    `;
    this.nodeEls.set(n.id, el);
    return el;
  }

  refreshNode(id: string): void {
    const n = this.app.graph.nodes.find((x) => x.id === id);
    const old = this.nodeEls.get(id);
    if (!n || !old) return;
    const el = this.nodeEl(n);
    old.replaceWith(el);
    this.applyTransform();
    this.updateWires();
    this.updateSelection();
  }

  // ------------------------------------------------------------------ wires
  portAnchor(nodeId: string, port: string, dir: 'in' | 'out'): { x: number; y: number } | null {
    const el = this.nodeEls.get(nodeId);
    if (!el) return null;
    const dot = el.querySelector(`.port.${dir}[data-port="${port}"] .dot`) as HTMLElement | null;
    if (!dot) return null;
    const r = dot.getBoundingClientRect();
    const wr = this.wrap.getBoundingClientRect();
    return {
      x: (r.left + r.width / 2 - wr.left - this.pan.x) / this.zoom,
      y: (r.top + r.height / 2 - wr.top - this.pan.y) / this.zoom,
    };
  }

  updateWires(): void {
    const g = this.app.graph;
    this.wireGroup.innerHTML = '';
    this.wireGroup.setAttribute('transform', `translate(${this.pan.x},${this.pan.y}) scale(${this.zoom})`);
    for (const e of g.edges) {
      const fromNode = g.nodes.find((n) => n.id === e.from);
      const toNode = g.nodes.find((n) => n.id === e.to);
      if (!fromNode || !toNode) continue;
      const a = this.portAnchor(e.from, e.fromPort, 'out');
      const b = this.portAnchor(e.to, e.toPort, 'in');
      if (!a || !b) continue;
      const type = defFor(fromNode.type).outputs.find((p) => p.name === e.fromPort)?.type ?? 'height';
      this.wireGroup.appendChild(this.wirePath(a, b, PORT_COLORS[type], false));
    }
    if (this.dragState?.kind === 'wire' && this.tmpPath) this.wireGroup.appendChild(this.tmpPath);
  }

  private wirePath(a: { x: number; y: number }, b: { x: number; y: number }, color: string, tmp: boolean): SVGPathElement {
    const p = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    const dx = Math.max(30, Math.abs(b.x - a.x) * 0.45);
    p.setAttribute('d', `M ${a.x} ${a.y} C ${a.x + dx} ${a.y}, ${b.x - dx} ${b.y}, ${b.x} ${b.y}`);
    p.setAttribute('stroke', color);
    if (tmp) p.classList.add('tmp');
    return p;
  }

  updateBadges(): void {
    for (const [id, el] of this.nodeEls) {
      el.querySelector('.node-badge')?.remove();
      const badge = this.app.badges.get(id);
      if (badge && !badge.ok) {
        const b = document.createElement('div');
        b.className = 'node-badge';
        b.textContent = '!';
        b.title = badge.reason;
        el.appendChild(b);
      }
    }
  }

  updateSelection(): void {
    for (const [id, el] of this.nodeEls) el.classList.toggle('selected', id === this.app.selectedId);
    const sel = this.app.selectedId;
    if (!sel || !this.nodeEls.has(sel)) { this.ctxBar.style.display = 'none'; return; }
    const n = this.app.graph.nodes.find((x) => x.id === sel)!;
    const el = this.nodeEls.get(sel)!;
    const wr = this.wrap.getBoundingClientRect();
    const x = n.x * this.zoom + this.pan.x;
    const y = n.y * this.zoom + this.pan.y;
    this.ctxBar.style.display = 'flex';
    this.ctxBar.style.left = `${x + (NODE_W * this.zoom) / 2 - this.ctxBar.offsetWidth / 2}px`;
    this.ctxBar.style.top = `${y - 40}px`;
    this.ctxBar.querySelector('[data-act="mute"]')?.classList.toggle('on', !!n.muted);
    this.ctxBar.querySelector('[data-act="lock"]')?.classList.toggle('on', !!n.locked);
    this.ctxBar.querySelector('[data-act="collapse"]')?.classList.toggle('on', !!n.collapsed);
    void el; void wr;
  }

  // ------------------------------------------------------------------ canvas
  private applyTransform(): void {
    this.layer.style.transform = `translate(${this.pan.x}px,${this.pan.y}px) scale(${this.zoom})`;
    this.wrap.style.setProperty('--grid-size', `${26 * this.zoom}px`);
    this.wrap.style.setProperty('--grid-pos', `${this.pan.x}px ${this.pan.y}px`);
    this.updateWires();
    this.updateSelection();
  }

  setZoom(z: number, cx?: number, cy?: number): void {
    const wr = this.wrap.getBoundingClientRect();
    const px = cx ?? wr.width / 2, py = cy ?? wr.height / 2;
    const nx = px - ((px - this.pan.x) / this.zoom) * z;
    const ny = py - ((py - this.pan.y) / this.zoom) * z;
    this.zoom = Math.max(0.2, Math.min(2.4, z));
    this.pan.x = nx; this.pan.y = ny;
    this.applyTransform();
  }

  fitGraph(): void {
    const ns = this.app.graph.nodes;
    if (ns.length === 0) return;
    let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
    for (const n of ns) {
      minX = Math.min(minX, n.x); minY = Math.min(minY, n.y);
      maxX = Math.max(maxX, n.x + NODE_W); maxY = Math.max(maxY, n.y + 90);
    }
    const wr = this.wrap.getBoundingClientRect();
    const z = Math.min(1.1, Math.min((wr.width - 120) / (maxX - minX), (wr.height - 160) / (maxY - minY)));
    this.zoom = Math.max(0.2, z);
    this.pan.x = 60 - minX * this.zoom;
    this.pan.y = 80 - minY * this.zoom;
    this.applyTransform();
  }

  frameNode(id: string): void {
    const n = this.app.graph.nodes.find((x) => x.id === id);
    if (!n) return;
    const wr = this.wrap.getBoundingClientRect();
    this.zoom = Math.max(this.zoom, 0.8);
    this.pan.x = wr.width / 2 - (n.x + NODE_W / 2) * this.zoom;
    this.pan.y = wr.height / 2 - (n.y + 45) * this.zoom;
    this.applyTransform();
  }

  private bindCanvas(): void {
    this.wrap.addEventListener('wheel', (e) => {
      e.preventDefault();
      const wr = this.wrap.getBoundingClientRect();
      this.setZoom(this.zoom * Math.exp(-e.deltaY * 0.0012), e.clientX - wr.left, e.clientY - wr.top);
    }, { passive: false });

    this.wrap.addEventListener('mousedown', (e) => {
      const target = e.target as HTMLElement;
      const dot = target.closest('.port .dot') as HTMLElement | null;
      const nodeEl = target.closest('.node') as HTMLElement | null;

      if (dot) {
        const portEl = dot.closest('.port') as HTMLElement;
        const dir = portEl.dataset.dir as 'in' | 'out';
        const id = nodeEl!.dataset.id!;
        const port = portEl.dataset.port!;
        const type = portEl.dataset.type as PortType;
        if (dir === 'out') {
          this.dragState = { kind: 'wire', from: id, fromPort: port, type, x: 0, y: 0 };
          this.tmpPath = document.createElementNS('http://www.w3.org/2000/svg', 'path');
          this.tmpPath.classList.add('tmp');
          this.tmpPath.setAttribute('stroke', PORT_COLORS[type]);
        } else {
          // drag from input: remove existing wire and re-drag backwards
          const existing = this.app.graph.edges.find((ed) => ed.to === id && ed.toPort === port);
          if (existing) {
            const from = existing.from, fromPort = existing.fromPort;
            const ft = this.app.graph.nodes.find((n) => n.id === from);
            const type2 = ft ? defFor(ft.type).outputs.find((p) => p.name === fromPort)?.type ?? 'height' : 'height';
            this.app.graph.edges = this.app.graph.edges.filter((ed) => ed.id !== existing.id);
            this.dragState = { kind: 'wire', from, fromPort, type: type2, x: 0, y: 0 };
            this.tmpPath = document.createElementNS('http://www.w3.org/2000/svg', 'path');
            this.tmpPath.classList.add('tmp');
            this.tmpPath.setAttribute('stroke', PORT_COLORS[type2]);
            this.updateWires();
          }
        }
        e.preventDefault();
        return;
      }

      if (nodeEl) {
        const id = nodeEl.dataset.id!;
        this.app.onSelect(id);
        const n = this.app.graph.nodes.find((x) => x.id === id)!;
        const mt = e.target as HTMLElement;
        if (!n.locked && (mt.closest('.node-head') || mt.closest('.node-body'))) {
          this.dragState = { kind: 'node', id, sx: e.clientX, sy: e.clientY, nx: n.x, ny: n.y };
        }
        e.preventDefault();
        return;
      }

      // blank canvas: pan + deselect
      if (target.closest('#ctx-toolbar') || target.closest('#editor-toolbar') || target.closest('#settings-panel') || target.closest('#settings-btn') || target.closest('#library-panel')) return;
      this.app.onSelect(null);
      this.dragState = { kind: 'pan', sx: e.clientX, sy: e.clientY, px: this.pan.x, py: this.pan.y };
    });

    window.addEventListener('mousemove', (e) => {
      const st = this.dragState;
      if (!st) return;
      if (st.kind === 'pan') {
        this.pan.x = st.px + (e.clientX - st.sx);
        this.pan.y = st.py + (e.clientY - st.sy);
        this.applyTransform();
      } else if (st.kind === 'node') {
        const n = this.app.graph.nodes.find((x) => x.id === st.id);
        if (!n) return;
        let nx = st.nx + (e.clientX - st.sx) / this.zoom;
        let ny = st.ny + (e.clientY - st.sy) / this.zoom;
        if (this.snapGrid) { nx = Math.round(nx / 10) * 10; ny = Math.round(ny / 10) * 10; }
        n.x = nx; n.y = ny;
        const el = this.nodeEls.get(st.id);
        if (el) { el.style.left = `${nx}px`; el.style.top = `${ny}px`; }
        this.updateWires();
        this.updateSelection();
      } else if (st.kind === 'wire' && this.tmpPath) {
        const a = this.portAnchor(st.from, st.fromPort, 'out')!;
        const wr = this.wrap.getBoundingClientRect();
        const b = { x: (e.clientX - wr.left - this.pan.x) / this.zoom, y: (e.clientY - wr.top - this.pan.y) / this.zoom };
        const dx = Math.max(30, Math.abs(b.x - a.x) * 0.45);
        this.tmpPath.setAttribute('d', `M ${a.x} ${a.y} C ${a.x + dx} ${a.y}, ${b.x - dx} ${b.y}, ${b.x} ${b.y}`);
        this.updateWires();
      }
    });

    window.addEventListener('mouseup', (e) => {
      const st = this.dragState;
      this.dragState = null;
      this.tmpPath = null;
      if (!st) return;
      if (st.kind === 'node') { this.app.onNodeMoved(); return; }
      if (st.kind !== 'wire') return;
      const target = document.elementFromPoint(e.clientX, e.clientY) as HTMLElement | null;
      const dot = target?.closest('.port .dot') as HTMLElement | null;
      if (!dot) { this.updateWires(); return; }
      const portEl = dot.closest('.port') as HTMLElement;
      if (portEl.dataset.dir !== 'in') { this.updateWires(); return; }
      const toId = (dot.closest('.node') as HTMLElement).dataset.id!;
      const toPort = portEl.dataset.port!;
      const toType = portEl.dataset.type as PortType;
      if (toId === st.from || toType !== st.type) { this.updateWires(); return; }
      const g = this.app.graph;
      const candidate: GraphEdge = makeEdge(st.from, st.fromPort, toId, toPort);
      const edges = g.edges.filter((ed) => !(ed.to === toId && ed.toPort === toPort));
      const test: Graph = { ...g, edges: [...edges, candidate] };
      if (!topoOrder(test)) { this.updateWires(); return; }  // cycle guard
      g.edges = [...edges, candidate];
      this.app.markDirty(true);
      this.updateWires();
    });

    this.wrap.addEventListener('dblclick', (e) => {
      const target = e.target as HTMLElement;
      if (target.closest('.node') || target.closest('#editor-toolbar') || target.closest('#settings-panel') || target.closest('#settings-btn') || target.closest('#library-panel') || target.closest('#inspector')) return;
      this.setLibraryOpen(!this.libraryOpen);
    });
  }

  // ------------------------------------------------------------------ chrome
  private bindChrome(): void {
    this.wrap.querySelector('#editor-toolbar')!.addEventListener('click', (e) => {
      const btn = (e.target as HTMLElement).closest('button') as HTMLElement | null;
      if (!btn) return;
      const act = btn.dataset.act;
      if (act === 'library') this.setLibraryOpen(!this.libraryOpen);
      else if (act === 'inspector') this.app.toggleInspector();
      else if (act === 'cook') this.app.cook();
      else if (act) { this.app.onToolbar(act); this.syncToolbar(); }
    });

    this.ctxBar.addEventListener('click', (e) => {
      const btn = (e.target as HTMLElement).closest('button') as HTMLElement | null;
      if (!btn || !this.app.selectedId) return;
      const g = this.app.graph;
      const n = g.nodes.find((x) => x.id === this.app.selectedId);
      if (!n) return;
      switch (btn.dataset.act) {
        case 'collapse': n.collapsed = !n.collapsed; this.refreshNode(n.id); break;
        case 'mute': n.muted = !n.muted; this.refreshNode(n.id); this.app.markDirty(true); break;
        case 'lock': n.locked = !n.locked; this.updateSelection(); break;
        case 'frame': this.frameNode(n.id); break;
        case 'fit': this.fitGraph(); break;
        case 'rename': {
          const name = window.prompt('Rename node', n.rename ?? defFor(n.type).label);
          if (name) { n.rename = name; this.refreshNode(n.id); }
          break;
        }
        case 'delete':
          g.nodes = g.nodes.filter((x) => x.id !== n.id);
          g.edges = g.edges.filter((ed) => ed.from !== n.id && ed.to !== n.id);
          this.app.onSelect(null);
          this.app.markDirty(true);
          this.render();
          break;
      }
    });

    const settingsBtn = this.wrap.querySelector('#settings-btn')!;
    const settingsPanel = this.wrap.querySelector('#settings-panel')!;
    settingsBtn.addEventListener('click', () => settingsPanel.classList.toggle('open'));
    settingsPanel.addEventListener('click', (e) => {
      const row = (e.target as HTMLElement).closest('.sp-row') as HTMLElement | null;
      if (!row) return;
      if (row.dataset.act === 'canvas-controls') {
        const sw = row.querySelector('.switch')!;
        sw.classList.toggle('on');
        this.snapGrid = sw.classList.contains('on');
      } else if (row.dataset.bg) {
        settingsPanel.querySelectorAll('[data-bg]').forEach((r) => {
          const on = r === row;
          r.classList.toggle('sel', on);
          (r as HTMLElement).querySelector('span:last-child')!.textContent = on ? '✓' : '';
        });
        this.wrap.classList.toggle('dotted', row.dataset.bg === 'dotted');
      }
    });

    this.library.querySelector('#library-search')!.addEventListener('input', (e) => {
      this.buildLibrary((e.target as HTMLInputElement).value);
    });
  }

  syncToolbar(): void {
    const bar = this.wrap.querySelector('#editor-toolbar');
    if (!bar) return;
    bar.querySelector('[data-act="wire"]')?.classList.toggle('on', this.app.flags.wireframe);
    bar.querySelector('[data-act="tex"]')?.classList.toggle('on', this.app.flags.textured);
    bar.querySelector('[data-act="auto"]')?.classList.toggle('on', this.app.flags.autoCook);
  }

  setLibraryOpen(open: boolean): void {
    this.libraryOpen = open;
    this.library.classList.toggle('open', open);
  }

  private buildLibrary(filter: string): void {
    const list = this.library.querySelector('#library-list')!;
    list.innerHTML = '';
    const q = filter.trim().toLowerCase();
    const catIcons: Record<string, string> = { Generators: '✳', SDF: '▣', Erosion: '≋', Texturing: '▦', Output: '▶' };
    for (const cat of CATEGORIES) {
      const items = NODE_DEFS.filter((d) => d.category === cat && d.type !== 'start')
        .filter((d) => !q || d.label.toLowerCase().includes(q) || d.subtitle.toLowerCase().includes(q));
      if (items.length === 0) continue;
      const catEl = document.createElement('div');
      catEl.className = 'lib-cat open';
      catEl.innerHTML = `<span class="ci">${catIcons[cat]}</span><span>${cat}</span><span class="chev">▶</span>`;
      catEl.addEventListener('click', () => catEl.classList.toggle('open'));
      list.appendChild(catEl);
      const box = document.createElement('div');
      box.className = 'lib-items';
      catEl.addEventListener('click', () => { box.style.display = catEl.classList.contains('open') ? 'block' : 'none'; });
      for (const d of items) {
        const item = document.createElement('div');
        item.className = 'lib-item';
        item.innerHTML = `<div><div class="li-t">${d.label}</div><div class="li-s">${d.subtitle}</div></div><span class="plus">+</span>`;
        item.addEventListener('click', (e) => {
          e.stopPropagation();
          this.addNode(d.type, e);
        });
        box.appendChild(item);
      }
      list.appendChild(box);
    }
  }

  private addNode(type: string, e?: MouseEvent): void {
    const wr = this.wrap.getBoundingClientRect();
    const cx = e ? (e.clientX - wr.left - this.pan.x) / this.zoom : (wr.width / 2 - this.pan.x) / this.zoom;
    const cy = e ? (e.clientY - wr.top - this.pan.y) / this.zoom : (wr.height / 2 - this.pan.y) / this.zoom;
    const n = makeNode(type, Math.round(cx / 10) * 10, Math.round(cy / 10) * 10);
    this.app.graph.nodes.push(n);
    this.app.onSelect(n.id);
    this.app.markDirty(true);
    this.render();
  }

  // ---------------------------------------------------------------- progress
  setProgress(frac: number, stage: string): void {
    this.progress.classList.remove('error');
    this.progress.querySelector('#pp-fill')!.setAttribute('style', `width:${Math.round(frac * 100)}%`);
    this.progress.querySelector('#pp-pct')!.textContent = `${Math.round(frac * 100)}%`;
    this.progress.querySelector('#pp-label')!.textContent = frac >= 1 ? 'Built' : `Building · ${stage}`;
  }

  setError(message: string): void {
    this.progress.classList.add('error');
    this.progress.querySelector('#pp-label')!.textContent = 'Cook failed';
    this.progress.querySelector('#pp-pct')!.textContent = '!';
    void message;
  }
}

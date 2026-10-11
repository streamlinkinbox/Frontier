/* RoadForge — application shell: outliner, layer stack, inspector, tools, gizmo, environment, export. */
(function (RF) {
  'use strict';
  const V = RF.V, $ = (s, el = document) => el.querySelector(s), h = (tag, attrs = {}, ...kids) => {
    const el = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) { if (k === 'class') el.className = v; else if (k === 'html') el.innerHTML = v; else if (k.startsWith('on')) el.addEventListener(k.slice(2), v); else if (v != null) el.setAttribute(k, v); }
    for (const k of kids.flat()) if (k != null) el.append(k.nodeType ? k : document.createTextNode(String(k)));
    return el;
  };
  const I = {
    logo: '<svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M4 20 9 4h6l5 16"/><path d="M12 7v2M12 12v2M12 17v2"/></svg>',
    select: '<svg viewBox="0 0 24 24"><path d="M4 4l7 16 2.5-6.5L20 11z"/></svg>', road: '<svg viewBox="0 0 24 24"><path d="M5 20 9 4M19 20 15 4M12 7v2M12 12v2M12 17v2"/></svg>', rail: '<svg viewBox="0 0 24 24"><path d="M7 3v18M17 3v18M7 7h10M7 12h10M7 17h10"/></svg>',
    block: '<svg viewBox="0 0 24 24"><path d="M4 6h16v12H4zM4 10h16M8 6v12"/></svg>', pan: '<svg viewBox="0 0 24 24"><path d="M12 3v18M3 12h18M12 3l-3 3M12 3l3 3M12 21l-3-3M12 21l3-3M3 12l3-3M3 12l3 3M21 12l-3-3M21 12l-3 3"/></svg>',
    plan: '<svg viewBox="0 0 24 24"><path d="M4 4h16v16H4zM4 12h16M12 4v16"/></svg>', cube: '<svg viewBox="0 0 24 24"><path d="M12 3 3 8l9 5 9-5zM3 8v8l9 5 9-5V8M12 13v8"/></svg>', xray: '<svg viewBox="0 0 24 24"><path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/></svg>',
    wire: '<svg viewBox="0 0 24 24"><path d="M3 7h18M3 12h18M3 17h18M7 3v18M12 3v18M17 3v18"/></svg>', car: '<svg viewBox="0 0 24 24"><path d="M5 16l1.5-5h11L19 16M3 16h18v3H3zM7 19v1M17 19v1"/></svg>', undo: '<svg viewBox="0 0 24 24"><path d="M9 14 4 9l5-5"/><path d="M4 9h10a6 6 0 0 1 0 12h-3"/></svg>', redo: '<svg viewBox="0 0 24 24"><path d="m15 14 5-5-5-5"/><path d="M20 9H10a6 6 0 0 0 0 12h3"/></svg>',
    export: '<svg viewBox="0 0 24 24"><path d="M12 3v12M7 10l5 5 5-5M4 17v3h16v-3"/></svg>', help: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M9.5 9.5a2.5 2.5 0 1 1 3.5 2.3c-.7.4-1 1-1 1.7M12 17h.01"/></svg>', search: '<svg viewBox="0 0 24 24"><circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/></svg>',
    chev: '<svg class="chev" viewBox="0 0 24 24"><path d="m6 9 6 6 6-6"/></svg>', node: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="3"/><path d="M12 3v6M12 15v6M3 12h6M15 12h6"/></svg>', eye: '<svg viewBox="0 0 24 24"><path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/></svg>', eyeoff: '<svg viewBox="0 0 24 24"><path d="M3 3l18 18M10.5 10.6a2 2 0 0 0 2.8 2.8M9.9 5.2A10 10 0 0 1 12 5c6 0 10 7 10 7a17 17 0 0 1-3.2 3.9M6.6 6.6A17 17 0 0 0 2 12s4 7 10 7a9.8 9.8 0 0 0 4.3-1"/></svg>',
    x: '<svg viewBox="0 0 24 24"><path d="M6 6l12 12M18 6 6 18"/></svg>', plus: '<svg viewBox="0 0 24 24"><path d="M12 5v14M5 12h14"/></svg>', trash: '<svg viewBox="0 0 24 24"><path d="M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3"/></svg>', focus: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="3"/><path d="M4 9V5a1 1 0 0 1 1-1h4M20 9V5a1 1 0 0 0-1-1h-4M4 15v4a1 1 0 0 0 1 1h4M20 15v4a1 1 0 0 1-1 1h-4"/></svg>',
    layerNet: '<svg viewBox="0 0 24 24"><path d="M4 18 9 5h6l5 13M12 8v2M12 13v2"/></svg>', layerSurf: '<svg viewBox="0 0 24 24"><path d="M3 17l9 4 9-4M3 12l9 4 9-4M3 7l9 4 9-4-9-4z"/></svg>', layerFurn: '<svg viewBox="0 0 24 24"><path d="M12 21V8M12 8a4 4 0 1 0 0-8 4 4 0 0 0 0 8zM8 21h8"/></svg>', layerTraffic: '<svg viewBox="0 0 24 24"><path d="M5 16l1.5-5h11L19 16M3 16h18v3H3zM7 19v1M17 19v1"/></svg>', layerEnv: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M2 12h2M20 12h2M5 5l1.4 1.4M17.6 17.6 19 19M5 19l1.4-1.4M17.6 6.4 19 5"/></svg>',
    tpl: '<svg viewBox="0 0 24 24"><path d="M4 4h7v7H4zM13 4h7v7h-7zM4 13h7v7H4zM13 13h7v7h-7z"/></svg>', bb: '<svg viewBox="0 0 24 24"><path d="M3 5h18v10H3zM12 15v6M9 21h6"/></svg>', train: '<svg viewBox="0 0 24 24"><rect x="5" y="3" width="14" height="14" rx="3"/><path d="M5 11h14M9 21l1.5-4M15 21l-1.5-4M9 14h.01M15 14h.01"/></svg>',
  };
  const fmt = (n, d = 1) => (Math.abs(n) >= 1000 ? (n / 1000).toFixed(d) + 'k' : Number(n).toFixed(d).replace(/\.0$/, ''));
  const deepClone = (o) => JSON.parse(JSON.stringify(o));

  const App = {
    tool: 'select', toolState: null, selection: null, selectedLayer: null, history: [], future: [], view: { plan: false, xray: false, wire: false, traffic: true }, hidden: new Set(), search: '',
    needsRender: true, lastT: 0, dragging: null, env: null, rebuildTimer: 0,
    async init() {
      this.canvas = $('#gl');
      this.renderer = new RF.Renderer(this.canvas);
      try { await this.renderer.init(); } catch (e) { $('#nogpu').style.display = 'grid'; $('#nogpu-reason').textContent = e.message; console.error(e); return; }
      this.renderer.setAtlas(makeAtlas());
      this.scene = new RF.scene.Scene(this.renderer);
      this.cam = new RF.Camera();
      this.buildLeft(); this.buildRight(); this.buildTopbar();
      this.bindViewport();
      const saved = this.loadLocal();
      this.loadProject(saved || RF.templates.list.crossroads.make(), !saved);
      this.cam.focus([0, 0, 0], 160); this.cam.yaw = -2.2; this.cam.pitch = 0.62; this.cam.update();
      requestAnimationFrame((t) => this.frame(t));
      window.addEventListener('resize', () => { this.needsRender = true; });
      window.addEventListener('keydown', (e) => this.onKey(e));
    },
    /* ---------------- project / history ---------------- */
    get project() { return this.scene.project; },
    loadProject(p, autoIntersect = true) {
      if (autoIntersect) RF.scene.autoIntersect(p);
      this.scene.setProject(p);
      this.history = []; this.future = []; this.selection = null; this.selectedLayer = null;
      if (this.nameInput) this.nameInput.value = p.name || '';
      this.refreshAll(); this.saveLocal();
    },
    snapshot() { return JSON.stringify(this.project); },
    commit(label, opts = {}) {
      // opts: { remesh (default true), materialsOnly, intersect }
      this.history.push(this._pending || this._lastSnapshot || this.snapshot()); this._pending = null; if (this.history.length > 60) this.history.shift(); this.future = [];
      if (opts.intersect) RF.scene.autoIntersect(this.project);
      if (opts.materialsOnly) this.scene.refreshMaterials(); else if (opts.remesh !== false) this.scene.rebuild();
      this._lastSnapshot = this.snapshot();
      this.refreshAll(); this.saveLocal(); this.toast(label);
    },
    beginEdit() { this._pending = this.snapshot(); },
    undo() { if (!this.history.length) return; this.future.push(this.snapshot()); const s = this.history.pop(); this.scene.setProject(JSON.parse(s)); this._lastSnapshot = s; this.selection = null; this.refreshAll(); this.toast('Undo'); },
    redo() { if (!this.future.length) return; this.history.push(this.snapshot()); const s = this.future.pop(); this.scene.setProject(JSON.parse(s)); this._lastSnapshot = s; this.selection = null; this.refreshAll(); this.toast('Redo'); },
    saveLocal() { try { localStorage.setItem('roadforge.project', this.snapshot()); } catch (e) { /* ignore */ } },
    loadLocal() { try { const s = localStorage.getItem('roadforge.project'); if (!s) return null; const p = JSON.parse(s); if (!p.roads || !p.nodes) return null; return p; } catch (e) { return null; } },
    refreshAll() { this.renderOutliner(); this.renderLayers(); this.renderInspector(); this.updateHandles(); this.updateStats(); this.needsRender = true; },
    toast(msg) { const t = $('#toast'); t.textContent = msg; t.classList.add('show'); clearTimeout(this._toastT); this._toastT = setTimeout(() => t.classList.remove('show'), 1600); },

    /* ---------------- top bar ---------------- */
    buildTopbar() {
      const bar = $('#topbar'); bar.innerHTML = '';
      const name = h('input', { value: this.scene.project.name, title: 'Project name', onchange: (e) => { this.project.name = e.target.value; this.saveLocal(); } });
      this.nameInput = name;
      bar.append(h('div', { class: 'brand' }, h('div', { class: 'logo', html: I.logo }), h('div', {}, h('h1', {}, 'RoadForge'), h('div', { class: 'sub' }, 'WebGPU road network generator')), name));
      const tools = h('div', { class: 'tools' });
      const tb = (id, icon, label, key, fn) => { const b = h('button', { class: 'tbtn', 'data-tool': id, title: `${label} (${key})`, html: icon + `<span>${label}</span>`, onclick: fn }); tools.append(b); return b; };
      for (const [id, icon, label, key] of [['select', I.select, 'Select', 'V'], ['road', I.road, 'Road', 'R'], ['rail', I.rail, 'Rail', 'T'], ['block', I.block, 'Block', 'B'], ['pan', I.pan, 'Pan', 'H']]) tb(id, icon, label, key, () => this.setTool(id));
      bar.append(tools);
      const views = h('div', { class: 'tools' });
      this.viewBtns = {};
      const vb = (id, icon, label, key, fn) => { const b = h('button', { class: 'tbtn', 'data-view': id, title: `${label} (${key})`, html: icon + `<span>${label}</span>`, onclick: fn }); views.append(b); this.viewBtns[id] = b; return b; };
      vb('plan', I.plan, 'Plan', 'P', () => this.togglePlan());
      vb('xray', I.xray, 'X-ray', 'X', () => { this.view.xray = !this.view.xray; this.updateViewBtns(); });
      vb('wire', I.wire, 'Topology', 'W', () => { this.view.wire = !this.view.wire; this.updateViewBtns(); });
      vb('traffic', I.car, 'Traffic', 'C', () => { this.view.traffic = !this.view.traffic; this.scene.showTraffic = this.view.traffic; if (!this.view.traffic) { this.renderer.removeByPrefix('veh|'); } this.updateViewBtns(); });
      bar.append(views, h('div', { class: 'spacer' }));
      this.statsEl = h('div', { class: 'stats' }); bar.append(this.statsEl);
      const hist = h('div', { class: 'tools' });
      this.undoBtn = h('button', { class: 'tbtn', id: 'btn-undo', title: 'Undo (Ctrl+Z)', html: I.undo, onclick: () => this.undo() }); this.redoBtn = h('button', { class: 'tbtn', id: 'btn-redo', title: 'Redo (Ctrl+Shift+Z)', html: I.redo, onclick: () => this.redo() });
      hist.append(this.undoBtn, this.redoBtn);
      const exp = h('button', { class: 'tbtn', id: 'btn-export', html: I.export + '<span>Export</span>', onclick: (e) => this.exportMenu(e.currentTarget) });
      const help = h('button', { class: 'tbtn', id: 'btn-help', html: I.help, title: 'Help', onclick: () => this.showHelp() });
      bar.append(hist, h('div', { class: 'tools' }, exp, help));
      this.updateViewBtns(); this.setTool('select');
    },
    updateViewBtns() { for (const [k, b] of Object.entries(this.viewBtns)) b.classList.toggle('on', !!this.view[k]); this.needsRender = true; },
    updateStats() {
      const s = this.scene.stats; const net = this.scene.net;
      this.statsEl.innerHTML = `<span><b>${fmt(s.quads, 1)}</b> quads</span><span><b>${fmt(Math.max(0, s.tris), 1)}</b> tris</span><span><b>${s.draws}</b> draws</span><span><b>${net ? net.roads.size : 0}</b> roads · <b>${net ? [...net.nodes.values()].filter((n) => n.kind === 'junction').length : 0}</b> junctions</span>`;
      this.undoBtn.toggleAttribute('disabled', !this.history.length); this.redoBtn.toggleAttribute('disabled', !this.future.length);
    },
    setTool(t) {
      this.tool = t; this.toolState = null;
      for (const b of document.querySelectorAll('[data-tool]')) b.classList.toggle('on', b.dataset.tool === t);
      this.canvas.className = 'tool-' + t; this.renderer.removeByPrefix('draft|');
      this.renderHud(); if (this.inspector) this.renderInspector(); this.needsRender = true;
    },
    togglePlan() { this.view.plan = !this.view.plan; this.cam.mode = this.view.plan ? 'plan' : 'orbit'; this.cam.update(); this.updateViewBtns(); },
    exportMenu(anchor) {
      const r = anchor.getBoundingClientRect();
      const menu = h('div', { class: 'menu', style: `top:${r.bottom + 6}px;right:${window.innerWidth - r.right}px` },
        h('button', { html: I.export + 'Export OBJ (quads) + MTL', onclick: () => { this.download('roadforge.obj', this.scene.exportOBJ()); this.download('roadforge.mtl', this.scene.exportMTL()); close(); } }),
        h('button', { html: I.export + 'Export project JSON', onclick: () => { this.download((this.project.name || 'roadforge').replace(/\W+/g, '_') + '.json', this.snapshot()); close(); } }),
        h('button', { html: I.plus + 'Import project JSON…', onclick: () => { close(); const inp = h('input', { type: 'file', accept: '.json' }); inp.onchange = () => { const f = inp.files[0]; if (!f) return; f.text().then((t) => { try { this.loadProject(JSON.parse(t)); this.toast('Project loaded'); } catch (e) { this.toast('Invalid project file'); } }); }; inp.click(); } }),
        h('button', { html: I.trash + 'New empty project', onclick: () => { this.loadProject(RF.scene.emptyProject('Untitled network')); close(); } }));
      const close = () => { menu.remove(); document.removeEventListener('pointerdown', onDoc, true); };
      const onDoc = (e) => { if (!menu.contains(e.target)) close(); };
      document.body.append(menu); setTimeout(() => document.addEventListener('pointerdown', onDoc, true));
    },
    download(name, text) { const a = h('a', { href: URL.createObjectURL(new Blob([text], { type: 'text/plain' })), download: name }); document.body.append(a); a.click(); setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 500); },
    showHelp() {
      const bg = h('div', { class: 'modal-bg', onclick: (e) => { if (e.target === bg) bg.remove(); } });
      bg.append(h('div', { class: 'modal' }, h('button', { class: 'btn close', html: I.x, onclick: () => bg.remove() }), h('h2', {}, 'RoadForge'),
        h('p', {}, 'Draw a road network, then dress it with the layer stack on the right. Every intersection is solved into one merged quad mesh; nodes, roads and control points are editable with the gizmo.'),
        h('ul', {}, h('li', {}, h('b', {}, 'Navigate: '), 'right-drag or Alt+drag orbits, middle-drag / Shift+drag pans, wheel zooms, ', h('kbd', {}, 'F'), ' frames the selection, ', h('kbd', {}, 'P'), ' toggles plan view.'),
          h('li', {}, h('b', {}, 'Road tool '), h('kbd', {}, 'R'), ': click to place points (click an existing node or road to connect / split). Double-click or ', h('kbd', {}, 'Enter'), ' finishes with a free end, ', h('kbd', {}, 'Esc'), ' cancels. Crossing roads at grade are split and joined automatically.'),
          h('li', {}, h('b', {}, 'Select '), h('kbd', {}, 'V'), ': click a junction to move it (and every connected road) with the gizmo; click a road to edit its control points, profile, structure, damage and furniture.'),
          h('li', {}, h('b', {}, 'Rail '), h('kbd', {}, 'T'), ' and ', h('b', {}, 'Block '), h('kbd', {}, 'B'), ' tools draw railways (level crossings, bridges and subway tunnels are derived from height) and extra city block footprints — blocks are otherwise generated from the network and follow your junctions live.'),
          h('li', {}, h('b', {}, 'Elevation: '), 'drag the green gizmo axis or type Y in the inspector. Heights above 4.5 m become viaducts; below −0.6 m become retained cuttings or tunnels; crossing roads with a height difference pass over / under each other.'),
          h('li', {}, h('kbd', {}, 'Delete'), ' removes the selection, ', h('kbd', {}, 'Ctrl+Z'), ' / ', h('kbd', {}, 'Ctrl+Shift+Z'), ' undo / redo, ', h('kbd', {}, 'X'), ' X-ray, ', h('kbd', {}, 'W'), ' topology (quad wire) view.'))));
      document.body.append(bg);
    },

    /* ---------------- left: outliner & templates ---------------- */
    buildLeft() {
      const L = $('#left'); L.innerHTML = '';
      L.append(h('div', { class: 'ph' }, h('span', { class: 'title' }, 'Outliner'), h('span', { class: 'count', id: 'outliner-count' })));
      const search = h('div', { class: 'search', html: I.search }); const inp = h('input', { placeholder: 'Search network…', oninput: (e) => { this.search = e.target.value.toLowerCase(); this.renderOutliner(); } }); search.append(inp); L.append(search);
      this.outliner = h('div', { class: 'scroll' }); L.append(this.outliner);
      this.groupsOpen = { templates: true, nodes: true, roads: true, rails: true, blocks: true, diagnostics: true };
    },
    renderOutliner() {
      const O = this.outliner; O.innerHTML = ''; const P = this.project, net = this.scene.net; const q = this.search;
      const group = (key, title, rows, countText) => {
        const g = h('div', { class: 'group' + (this.groupsOpen[key] ? '' : ' closed') });
        g.append(h('div', { class: 'gh', onclick: () => { this.groupsOpen[key] = !this.groupsOpen[key]; g.classList.toggle('closed'); }, html: I.chev + `<span>${title}</span><span class="count">${countText ?? rows.length}</span>` }));
        const rs = h('div', { class: 'rows' }); for (const r of rows) rs.append(r); g.append(rs); O.append(g);
      };
      // templates
      group('templates', 'Templates', Object.entries(RF.templates.list).map(([id, t]) => h('div', { class: 'row tpl', 'data-template': id, onclick: () => { this.loadProject(t.make()); this.frameAll(); this.toast(`Loaded “${t.name}”`); } }, h('div', { class: 'ico', style: 'color:var(--accent-b)', html: I.tpl }), h('div', { class: 'name' }, t.name, h('div', { class: 'blurb' }, t.blurb)))));
      const sel = this.selection;
      const row = (type, id, icon, color, name, meta) => {
        const isSel = sel && sel.type === type && sel.id === id; const hidden = this.hidden.has(type + ':' + id);
        return h('div', { class: 'row' + (isSel ? ' sel' : ''), 'data-type': type, 'data-id': id, onclick: () => this.select({ type, id }), ondblclick: () => this.frameSelection() },
          h('div', { class: 'ico', style: `color:${color}`, html: icon }), h('div', { class: 'name' }, name), h('div', { class: 'meta' }, meta || ''),
          h('button', { class: 'eye' + (hidden ? ' off' : ''), html: hidden ? I.eyeoff : I.eye, title: 'Toggle visibility', onclick: (e) => { e.stopPropagation(); this.toggleHidden(type, id); } }));
      };
      const match = (s) => !q || (s || '').toLowerCase().includes(q);
      const nodes = Object.values(P.nodes).map((n) => { const rec = net && net.nodes.get(n.id); const kind = rec ? (rec.kind === 'junction' ? rec.type : rec.kind === 'end' ? 'End' : rec.kind === 'transition' ? 'Transition' : 'Node') : 'Node'; return { n, rec, kind }; }).filter((x) => x.rec && x.rec.kind !== 'end' && x.rec.kind !== 'pass' || q).filter((x) => match(x.n.name) || match(x.kind));
      group('nodes', 'Junctions', nodes.map((x) => row('node', x.n.id, I.node, 'var(--accent-a)', x.n.name && x.n.name !== 'Node' ? x.n.name : x.kind, x.kind !== (x.n.name || '') ? `${x.rec ? x.rec.arms.length : ''}-arm` : '')));
      const roads = Object.values(P.roads).filter((r) => match(r.name) || match(r.profile));
      group('roads', 'Roads', roads.map((r) => { const Rd = net && net.roads.get(r.id); return row('road', r.id, I.road, 'var(--accent-c)', r.name || r.id, Rd ? `${Rd.length.toFixed(0)} m` : ''); }));
      const rails = Object.values(P.rails || {}).filter((r) => match(r.name));
      if (rails.length || !q) group('rails', 'Railways', rails.map((r) => row('rail', r.id, I.train, 'var(--accent-e)', r.name || r.id, r.kind)));
      const blocks = [...Object.values(P.blocks || {}), ...Object.values(this.scene.autoBlocks || {})].filter((b) => match(b.name) || match(b.surface));
      if (blocks.length || !q) group('blocks', 'City blocks', blocks.map((b) => row('block', b.id, I.block, 'var(--accent-d)', b.name || b.id, (b.auto ? 'auto · ' : '') + b.surface)));
      const diags = this.scene.diagnostics || [];
      if (diags.length) group('diagnostics', 'Diagnostics', diags.map((d) => h('div', { class: 'diag ' + (d.level === 'error' ? 'err' : 'warn'), onclick: () => { if (d.owner) this.select(P.roads[d.owner] ? { type: 'road', id: d.owner } : { type: 'node', id: d.owner }); } }, d.message)));
      $('#outliner-count').textContent = `${Object.keys(P.roads).length} roads · ${Object.keys(P.nodes).length} nodes`;
    },
    toggleHidden(type, id) {
      const key = type + ':' + id; if (this.hidden.has(key)) this.hidden.delete(key); else this.hidden.add(key);
      const owner = type + ':' + id; for (const [iid, it] of this.renderer.items) if (iid.startsWith(owner + '|')) it.visible = !this.hidden.has(key);
      this.renderOutliner(); this.needsRender = true;
    },

    /* ---------------- right: layer stack + inspector ---------------- */
    buildRight() {
      const Ls = $('#layers'); Ls.innerHTML = '';
      Ls.append(h('div', { class: 'ph' }, h('span', { class: 'title' }, 'Layer stack'), h('span', { class: 'count' }, 'generate · texture · dress')));
      this.layerList = h('div', { class: 'scroll' }); Ls.append(this.layerList);
      const Ins = $('#inspector'); Ins.innerHTML = '';
      Ins.append(h('div', { class: 'ph' }, h('span', { class: 'title' }, 'Inspector'), h('span', { class: 'count', id: 'insp-sub' })));
      this.inspector = h('div', { class: 'scroll' }); Ins.append(this.inspector);
    },
    LAYERS: [
      { id: 'network', name: 'Network', sub: 'profiles · intersections · handedness', icon: 'layerNet', color: 'var(--accent-a)' },
      { id: 'surface', name: 'Surface', sub: 'asphalt · kerbs · paving · markings', icon: 'layerSurf', color: 'var(--accent-c)' },
      { id: 'furniture', name: 'Furniture', sub: 'lighting · signals · railings · signs', icon: 'layerFurn', color: 'var(--accent-b)' },
      { id: 'traffic', name: 'Traffic', sub: 'AI vehicles (max 25) · trains', icon: 'layerTraffic', color: 'var(--accent-d)' },
      { id: 'environment', name: 'Environment', sub: 'sun · sky · fog · exposure', icon: 'layerEnv', color: 'var(--accent-e)' },
    ],
    renderLayers() {
      const L = this.layerList; L.innerHTML = '';
      for (const ly of this.LAYERS) {
        const on = this.layerEnabled(ly.id);
        L.append(h('div', { class: 'layer' + (this.selectedLayer === ly.id ? ' sel' : ''), 'data-id': ly.id, onclick: () => { this.selectedLayer = ly.id; this.selection = null; this.renderLayers(); this.renderOutliner(); this.renderInspector(); this.updateHandles(); } },
          h('div', { class: 'ico', style: `color:${ly.color}`, html: I[ly.icon] }), h('div', { class: 'lname' }, ly.name, h('div', { class: 'lsub' }, ly.sub)),
          h('button', { class: 'toggle' + (on ? ' on' : ''), title: 'Enable layer', onclick: (e) => { e.stopPropagation(); this.setLayerEnabled(ly.id, !on); } })));
      }
    },
    layerEnabled(id) { const L = this.project.layers; if (id === 'network') return true; if (id === 'surface') return L.surface.enabled !== false; if (id === 'furniture') return L.furniture.enabled !== false; if (id === 'traffic') return L.traffic.enabled !== false; return L.environment.enabled !== false; },
    setLayerEnabled(id, on) {
      const L = this.project.layers;
      if (id === 'surface') { L.surface.enabled = on; this.commit(on ? 'Surface detail on' : 'Surface detail off', { materialsOnly: true }); }
      else if (id === 'furniture') { L.furniture.enabled = on; for (const k of ['streetlights', 'signals', 'signs', 'railings', 'guardrails', 'billboards', 'parkedCars']) L.furniture[k] = on ? (L.furniture['_' + k] ?? true) : (L.furniture['_' + k] = L.furniture[k], false); this.commit(on ? 'Furniture on' : 'Furniture off'); }
      else if (id === 'traffic') { L.traffic.enabled = on; this.commit(on ? 'Traffic on' : 'Traffic off'); }
      else if (id === 'environment') { L.environment.enabled = on; this.renderLayers(); this.needsRender = true; }
    },

    /* ---------------- inspector ---------------- */
    renderInspector() {
      const el = this.inspector; el.innerHTML = ''; const sub = $('#insp-sub');
      if (this.tool !== 'select' && !this.selection) { sub.textContent = 'tool'; el.append(this.toolCard()); return; }
      if (this.selection) { sub.textContent = this.selection.type; const card = this.objectCard(this.selection); if (card) { el.append(card); return; } }
      if (this.selectedLayer) { sub.textContent = 'layer'; el.append(this.layerCard(this.selectedLayer)); return; }
      sub.textContent = '';
      const s = this.scene.stats;
      el.append(h('div', { class: 'card' }, h('div', { class: 'eyebrow' }, 'Project'), h('div', { class: 'h1' }, this.project.name), h('div', { class: 'muted' }, `${Object.keys(this.project.roads).length} roads · ${Object.keys(this.project.nodes).length} nodes · ${Object.keys(this.project.rails || {}).length} railways · ${Object.keys(this.project.blocks || {}).length} blocks`),
        h('div', { class: 'section-label' }, 'Topology'), h('div', { class: 'metric' }, fmt(s.quads, 1)), h('div', { class: 'muted' }, `quads on screen · ${fmt(Math.max(0, s.tris))} triangles (block interiors only) · ${s.draws} draw calls`),
        h('div', { class: 'section-label' }, 'Workflow'), h('div', { class: 'hint' }, '1. Pick a template or draw roads with ', h('kbd', {}, 'R'), '. 2. Select junctions and roads to tune profiles, heights and damage. 3. Texture with the Surface layer, dress with Furniture, then enable Traffic.')));
    },
    toolCard() {
      const t = this.tool; const ts = this.toolState || {};
      const card = h('div', { class: 'card' });
      if (t === 'road') {
        this.roadDraft = this.roadDraft || { profile: 'street', y: 0 };
        card.append(h('div', { class: 'eyebrow' }, 'Road tool'), h('h2', {}, 'Draw a road'), h('div', { class: 'hint' }, 'Click to place points. Start or end on a node or road to connect. ', h('kbd', {}, 'Enter'), ' / double-click to finish, ', h('kbd', {}, 'Esc'), ' to cancel.'));
        card.append(h('div', { class: 'section-label' }, 'Profile'), this.selectField('Type', RF.PROFILE_KEYS.map((k) => [k, RF.PROFILES[k].name]), this.roadDraft.profile, (v) => { this.roadDraft.profile = v; }));
        card.append(this.numberField('Height of new points (m)', this.roadDraft.y, 0.5, (v) => { this.roadDraft.y = v; }));
        if (ts.pts && ts.pts.length) card.append(h('div', { class: 'muted', style: 'margin-top:8px' }, `${ts.pts.length} point(s) placed`));
      } else if (t === 'rail') {
        this.railDraft = this.railDraft || { kind: 'rail', y: 0, electrified: true };
        card.append(h('div', { class: 'eyebrow' }, 'Rail tool'), h('h2', {}, 'Draw a railway'), h('div', { class: 'hint' }, 'Click to place track points; ', h('kbd', {}, 'Enter'), ' finishes. Tracks at grade get level crossings, above 4.5 m a bridge, below −3 m a tunnel; Metro lines are always underground.'));
        card.append(h('div', { class: 'section-label' }, 'Line'), this.selectField('Kind', [['rail', 'Railway (surface)'], ['subway', 'Metro (subway)']], this.railDraft.kind, (v) => { this.railDraft.kind = v; this.railDraft.y = v === 'subway' ? -13 : 0; this.renderInspector(); }));
        card.append(this.numberField('Height of new points (m)', this.railDraft.y, 0.5, (v) => { this.railDraft.y = v; }), this.toggleField('Electrified (catenary)', this.railDraft.electrified, (v) => { this.railDraft.electrified = v; }));
      } else if (t === 'block') {
        this.blockDraft = this.blockDraft || { surface: 'paving', sidewalk: 3 };
        card.append(h('div', { class: 'eyebrow' }, 'Block tool'), h('h2', {}, 'Draw a city block'), h('div', { class: 'hint' }, 'Click the block corners (kerb line), ', h('kbd', {}, 'Enter'), ' closes the polygon. Blocks get a kerb, a footway ring and an interior surface — no buildings.'));
        card.append(h('div', { class: 'section-label' }, 'Surface'), this.selectField('Interior', [['paving', 'Paving'], ['grass', 'Grass / park'], ['gravel', 'Gravel'], ['concrete', 'Concrete']], this.blockDraft.surface, (v) => { this.blockDraft.surface = v; }), this.sliderField('Footway width', this.blockDraft.sidewalk, 1, 8, 0.5, (v) => { this.blockDraft.sidewalk = v; }, 'm'));
      } else if (t === 'pan') card.append(h('div', { class: 'eyebrow' }, 'Pan tool'), h('h2', {}, 'Navigate'), h('div', { class: 'hint' }, 'Drag to pan, wheel to zoom, right-drag to orbit. ', h('kbd', {}, 'P'), ' toggles the plan view.'));
      return card;
    },
    /* generic controls */
    toggleField(label, val, fn) { const t = h('button', { class: 'toggle' + (val ? ' on' : ''), onclick: () => { val = !val; t.classList.toggle('on', val); fn(val); } }); return h('div', { class: 'field' }, h('label', {}, label), t); },
    sliderField(label, val, min, max, step, fn, unit = '', opts = {}) {
      const out = h('span', { class: 'val' }, `${fmt(val, step < 0.1 ? 2 : 1)}${unit}`);
      const r = h('input', { type: 'range', min, max, step, value: val });
      let began = false;
      r.addEventListener('input', () => { const v = parseFloat(r.value); out.textContent = `${fmt(v, step < 0.1 ? 2 : 1)}${unit}`; if (!began) { began = true; if (opts.begin) opts.begin(); } fn(v, false); });
      r.addEventListener('change', () => { fn(parseFloat(r.value), true); began = false; });
      return h('div', { class: 'field col' }, h('div', { class: 'field' }, h('label', {}, label), out), r);
    },
    selectField(label, options, val, fn) { const s = h('select', { class: 'sel', onchange: () => fn(s.value) }); for (const [v, n] of options) s.append(h('option', { value: v, selected: v === val ? '' : null }, n)); return h('div', { class: 'field' }, h('label', {}, label), s); },
    numberField(label, val, step, fn, unit = '') { const i = h('input', { class: 'num', type: 'number', step, value: Number(val).toFixed(step < 1 ? 2 : 0), onchange: () => fn(parseFloat(i.value)) }); return h('div', { class: 'field' }, h('label', {}, label + (unit ? ` (${unit})` : '')), i); },
    textField(label, val, fn) { const i = h('input', { class: 'txt', value: val || '', onchange: () => fn(i.value) }); return h('div', { class: 'field col' }, h('label', {}, label), i); },
    colorField(label, hex, fn) { const i = h('input', { class: 'swatch', type: 'color', value: hex, oninput: () => fn(i.value, false), onchange: () => fn(i.value, true) }); return h('div', { class: 'field' }, h('label', {}, label), i); },
    pillsField(label, options, val, fn) { const wrap = h('div', { class: 'pills' }); for (const [v, n] of options) wrap.append(h('button', { class: 'pill' + (v === val ? ' on' : ''), onclick: () => fn(v) }, n)); return h('div', { class: 'field col' }, h('label', {}, label), wrap); },
    vecField(label, p, fn) { const mk = (i) => h('input', { class: 'num', type: 'number', step: 0.5, value: p[i].toFixed(2), onchange: (e) => { const q = p.slice(); q[i] = parseFloat(e.target.value); fn(q); } }); return h('div', { class: 'field col' }, h('label', {}, label), h('div', { class: 'vec' }, mk(0), mk(1), mk(2))); },

    layerCard(id) {
      const L = this.project.layers; const card = h('div', { class: 'card' }); const ly = this.LAYERS.find((l) => l.id === id);
      card.append(h('div', { class: 'obj-icon', style: `color:${ly.color}`, html: I[ly.icon] }), h('div', { class: 'eyebrow' }, 'Layer'), h('div', { class: 'h1' }, ly.name), h('div', { class: 'muted' }, ly.sub));
      const S = L.surface, F = L.furniture, T = L.traffic, E = L.environment;
      const mat = (label) => (v, done) => { if (done) this.commit(label, { materialsOnly: true }); else { this.scene.refreshMaterials(); this.needsRender = true; } };
      const remesh = (label) => (v, done) => { if (done !== false) this.commit(label); };
      if (id === 'network') {
        const P = this.project;
        card.append(h('div', { class: 'section-label' }, 'Rules'), this.pillsField('Driving side', [['right', 'Right-hand traffic'], ['left', 'Left-hand traffic']], P.settings.handedness, (v) => { P.settings.handedness = v; this.commit('Driving side'); }));
        card.append(this.pillsField('Marking standard', [['eu', 'European (Vienna)'], ['us', 'US (MUTCD)']], S.markingStd || 'eu', (v) => { S.markingStd = v; this.commit('Marking standard'); }));
        card.append(this.sliderField('Crossfall', S.crossfall * 100, 0, 4, 0.25, (v, d) => { S.crossfall = v / 100; if (d) this.commit('Crossfall'); }, '%'));
        card.append(h('div', { class: 'section-label' }, 'Intersections'), h('div', { class: 'hint' }, 'Junctions are solved from the kerb lines of every approach: facing edges are intersected, tangent-continuous fillets are fitted and each approach is trimmed back — acute merges receive long runouts instead of overlapping geometry. The surface is one welded all-quad patch.'),
          h('div', { class: 'btnrow' }, h('button', { class: 'btn', html: I.plus + 'Auto-join crossing roads', onclick: () => this.commit('Joined crossing roads', { intersect: true }) })));
        const AB = (P.settings.autoBlocks = P.settings.autoBlocks || { enabled: Object.keys(P.blocks || {}).length === 0, surface: 'mixed', depth: 120 });
        card.append(h('div', { class: 'section-label' }, 'City blocks'), this.toggleField('Generate blocks from the network', AB.enabled !== false, (v) => { AB.enabled = v; this.commit(v ? 'Auto blocks on' : 'Auto blocks off'); }));
        card.append(this.pillsField('Block surface', [['mixed', 'Mixed'], ['paving', 'Paved'], ['grass', 'Parks'], ['gravel', 'Gravel'], ['concrete', 'Concrete']], AB.surface || 'mixed', (v) => { AB.surface = v; this.commit('Block surface'); }));
        card.append(this.sliderField('Roadside block depth', AB.depth ?? 120, 30, 300, 5, (v, d) => { AB.depth = v; if (d) this.commit('Block depth'); }, ' m'));
        const hiddenN = Object.values(P.blockOverrides || {}).filter((o) => o.hidden).length;
        if (hiddenN) card.append(h('div', { class: 'btnrow' }, h('button', { class: 'btn', html: I.plus + `Restore ${hiddenN} hidden block${hiddenN === 1 ? '' : 's'}`, onclick: () => { for (const o of Object.values(P.blockOverrides)) delete o.hidden; this.commit('Restored blocks'); } })));
        card.append(h('div', { class: 'hint' }, 'Closed faces of the network become blocks bounded by the footway edges; corners on the outside of the network get roadside blocks of the depth above. Blocks are rebuilt live while you drag junctions. Draw extra blocks anywhere with the Block tool.'));
        card.append(h('div', { class: 'section-label' }, 'Profiles'), h('div', { class: 'hint' }, Object.values(RF.PROFILES).map((p) => p.name).join(' · ')));
      } else if (id === 'surface') {
        card.append(h('div', { class: 'section-label' }, 'Asphalt'), this.colorField('Tone', S.asphaltColor || '#161617', (v, d) => { S.asphaltColor = v; mat('Asphalt tone')(v, d); }));
        card.append(this.sliderField('Wear & polish', S.wear, 0, 1, 0.01, (v, d) => { S.wear = v; mat('Wear')(v, d); }), this.sliderField('Cracks', S.cracks, 0, 1, 0.01, (v, d) => { S.cracks = v; mat('Cracks')(v, d); }), this.sliderField('Patches & repairs', S.patches, 0, 1, 0.01, (v, d) => { S.patches = v; mat('Patches')(v, d); }), this.sliderField('Wetness', S.wetness, 0, 1, 0.01, (v, d) => { S.wetness = v; mat('Wetness')(v, d); }), this.sliderField('Aggregate scale', S.aggregateScale, 0.5, 2, 0.05, (v, d) => { S.aggregateScale = v; mat('Aggregate')(v, d); }));
        card.append(this.numberField('Noise seed', S.seed || 1, 1, (v) => { S.seed = v; this.commit('Seed', { materialsOnly: true }); }));
        card.append(h('div', { class: 'section-label' }, 'Markings'), this.sliderField('Paint wear', S.paintWear, 0, 1, 0.01, (v, d) => { S.paintWear = v; mat('Paint wear')(v, d); }), this.toggleField('Coloured bus / cycle lanes', S.colouredLanes !== false, (v) => { S.colouredLanes = v; this.commit('Coloured lanes'); }));
        card.append(h('div', { class: 'section-label' }, 'Kerbs & footways'), this.pillsField('Kerb', [['concrete', 'Precast concrete'], ['granite', 'Granite setts']], S.curbMaterial, (v) => { S.curbMaterial = v; this.commit('Kerb material'); }));
        card.append(this.selectField('Paving pattern', RF.materials.PAVING_PATTERNS.map((p) => [String(p.id), p.name]), String(S.pavingPattern), (v) => { S.pavingPattern = parseInt(v); this.commit('Paving pattern', { materialsOnly: true }); }), this.colorField('Paving tone', S.pavingColor || '#9c9384', (v, d) => { S.pavingColor = v; mat('Paving tone')(v, d); }), this.sliderField('Paving scale', S.pavingScale, 0.5, 2.5, 0.05, (v, d) => { S.pavingScale = v; mat('Paving scale')(v, d); }));
        card.append(this.pillsField('Raised median', [['paving', 'Paved'], ['grass', 'Planted']], S.medianSurface, (v) => { S.medianSurface = v; this.commit('Median surface'); }));
        card.append(h('div', { class: 'section-label' }, 'Metalwork'), this.sliderField('Rust / grime', S.rust, 0, 1, 0.01, (v, d) => { S.rust = v; mat('Rust')(v, d); }));
      } else if (id === 'furniture') {
        card.append(h('div', { class: 'section-label' }, 'Lighting'), this.toggleField('Street lighting', F.streetlights !== false, (v) => { F.streetlights = v; this.commit('Street lighting'); }), this.sliderField('Column spacing override', F.lightSpacing || 0, 0, 60, 1, (v, d) => { F.lightSpacing = v; if (d) this.commit('Light spacing'); }, ' m'));
        card.append(h('div', { class: 'section-label' }, 'Junctions'), this.toggleField('Traffic signals (robots)', F.signals !== false, (v) => { F.signals = v; this.commit('Signals'); }), this.toggleField('Priority signs', F.signs !== false, (v) => { F.signs = v; this.commit('Signs'); }), this.toggleField('Pedestrian railings', !!F.railings, (v) => { F.railings = v; this.commit('Railings'); }), this.toggleField('Corner bollards', !!F.cornerBollards, (v) => { F.cornerBollards = v; this.commit('Corner bollards'); }));
        card.append(h('div', { class: 'section-label' }, 'Roadside'), this.toggleField('Guardrails (W-beam)', F.guardrails !== false, (v) => { F.guardrails = v; this.commit('Guardrails'); }), this.toggleField('Motorway billboards', F.billboards !== false, (v) => { F.billboards = v; this.commit('Billboards'); }), this.toggleField('Parked cars in bays', F.parkedCars !== false, (v) => { F.parkedCars = v; this.commit('Parked cars'); }));
        card.append(h('div', { class: 'hint', style: 'margin-top:10px' }, 'Per-road bollard designs, lighting sides, guardrails and billboards are set in each road’s inspector.'));
      } else if (id === 'traffic') {
        card.append(h('div', { class: 'section-label' }, 'Vehicles'), this.toggleField('AI traffic', T.enabled !== false, (v) => { T.enabled = v; this.commit('Traffic'); }), this.sliderField('Vehicles (max 25)', T.count ?? 18, 0, 25, 1, (v, d) => { T.count = v; if (this.scene.traffic) this.scene.traffic.setCount(v); if (d) { this.saveLocal(); } }));
        card.append(h('div', { class: 'section-label' }, 'Trains'), this.toggleField('Trains on railways', T.trains !== false, (v) => { T.trains = v; this.commit('Trains'); }));
        card.append(h('div', { class: 'hint', style: 'margin-top:10px' }, 'Vehicles follow lane centrelines, use IDM car-following, obey the signal phases and give way at priority junctions (major road first, then priority to the right).'));
      } else if (id === 'environment') {
        const upd = () => { this.needsRender = true; this.saveLocal(); };
        card.append(h('div', { class: 'section-label' }, 'Sun'), this.sliderField('Time of day', E.timeOfDay, 0, 24, 0.1, (v) => { E.timeOfDay = v; upd(); }, ' h'), this.sliderField('Sun azimuth', E.azimuth, 0, 360, 1, (v) => { E.azimuth = v; upd(); }, '°'), this.sliderField('Overcast', E.cloud, 0, 1, 0.01, (v) => { E.cloud = v; upd(); }));
        card.append(h('div', { class: 'section-label' }, 'Atmosphere'), this.sliderField('Fog density', E.fog * 1000, 0, 3, 0.05, (v) => { E.fog = v / 1000; upd(); }), this.sliderField('Exposure', E.exposure, 0.3, 2.5, 0.05, (v) => { E.exposure = v; upd(); }));
        card.append(h('div', { class: 'btnrow' }, ...[['Dawn', 6.3], ['Noon', 12.5], ['Golden hour', 18.6], ['Night', 23]].map(([n, t]) => h('button', { class: 'btn', onclick: () => { E.timeOfDay = t; upd(); this.renderInspector(); } }, n))));
      }
      return card;
    },

    objectCard(sel) {
      const P = this.project, net = this.scene.net;
      if (sel.type === 'node') {
        const n = P.nodes[sel.id]; if (!n) return null; const rec = net.nodes.get(n.id);
        const card = h('div', { class: 'card' }, h('div', { class: 'obj-icon', style: 'color:var(--accent-a)', html: I.node }), h('div', { class: 'eyebrow' }, rec ? rec.type || rec.kind : 'Node'), h('div', { class: 'h1' }, n.name && n.name !== 'Node' ? n.name : (rec && rec.type) || 'Node'));
        card.append(this.textField('Name', n.name, (v) => { n.name = v; this.commit('Rename', { remesh: false }); }));
        card.append(h('div', { class: 'section-label' }, 'Position'), this.vecField('X · Y (height) · Z', n.p, (q) => { n.p = q; this.commit('Move junction'); }));
        if (rec && rec.arms.length >= 3) {
          card.append(h('div', { class: 'section-label' }, 'Control'), this.pillsField('Control', [['auto', 'Auto'], ['signals', 'Signals'], ['priority', 'Give way'], ['stop', 'Stop'], ['none', 'Uncontrolled']], n.control || 'auto', (v) => { n.control = v; this.commit('Junction control'); }));
        }
        if (rec && rec.arms.length >= 2) card.append(this.sliderField('Corner radius override', n.radius ?? 0, 0, 30, 0.5, (v, d) => { n.radius = v > 0 ? v : undefined; if (d) this.commit('Corner radius'); }, ' m'));
        if (rec) {
          card.append(h('div', { class: 'section-label' }, 'Approaches'), h('div', { class: 'sub-list' }, ...rec.arms.map((a) => h('div', { class: 'sub-item', onclick: () => this.select({ type: 'road', id: a.roadId }) }, h('span', {}, a.road.road.name || a.roadId), h('span', { class: 'params' }, `trim ${a.trim.toFixed(1)} m`)))));
          if (rec.corners && rec.corners.length) card.append(h('div', { class: 'muted', style: 'margin-top:8px' }, 'Corners: ' + rec.corners.map((c) => (c.straight ? 'straight' : `R ${c.radius.toFixed(1)} m`)).join(' · ')));
        }
        card.append(h('div', { class: 'btnrow' }, h('button', { class: 'btn', html: I.focus + 'Frame', onclick: () => this.frameSelection() }), h('button', { class: 'btn danger', html: I.trash + 'Delete junction & roads', onclick: () => this.deleteSelection() })));
        return card;
      }
      if (sel.type === 'road') {
        const r = P.roads[sel.id]; if (!r) return null; const Rd = net.roads.get(r.id); const prof = RF.profiles.effective(r);
        r.features = r.features || {}; r.props = r.props || {}; r.overrides = r.overrides || {};
        const card = h('div', { class: 'card' }, h('div', { class: 'obj-icon', style: 'color:var(--accent-c)', html: I.road }), h('div', { class: 'eyebrow' }, prof.name), h('div', { class: 'h1' }, r.name || r.id), h('div', { class: 'muted' }, Rd ? `${Rd.length.toFixed(1)} m · ${Rd.structure ? Rd.structure.spans.map((s) => s.kind).filter((v, i, a) => a.indexOf(v) === i).join(' / ') : ''}` : ''));
        card.append(this.textField('Name', r.name, (v) => { r.name = v; this.commit('Rename', { remesh: false }); }));
        card.append(h('div', { class: 'section-label' }, 'Profile'), this.selectField('Type', RF.PROFILE_KEYS.map((k) => [k, RF.PROFILES[k].name]), r.profile, (v) => { r.profile = v; r.overrides = {}; this.commit('Road profile'); }));
        const ov = (key, label, min, max, step, unit = '') => this.sliderField(label, prof[key], min, max, step, (v, d) => { r.overrides[key] = v; if (d) this.commit(label); }, unit);
        card.append(ov('laneWidth', 'Lane width', 2.5, 4.5, 0.05, ' m'), this.numberField('Lanes forward', prof.lanesF, 1, (v) => { r.overrides.lanesF = Math.max(0, v | 0); this.commit('Lanes'); }), this.numberField('Lanes backward', prof.lanesB, 1, (v) => { r.overrides.lanesB = Math.max(0, v | 0); this.commit('Lanes'); }));
        card.append(ov('sidewalkL', 'Footway left', 0, 8, 0.1, ' m'), ov('sidewalkR', 'Footway right', 0, 8, 0.1, ' m'), ov('parkingL', 'Parking bay left', 0, 2.5, 0.1, ' m'), ov('parkingR', 'Parking bay right', 0, 2.5, 0.1, ' m'), ov('median', 'Median width', 0, 8, 0.1, ' m'));
        card.append(this.sliderField('Bus lane (each direction)', prof.bus || 0, 0, 4, 0.1, (v, d) => { r.overrides.bus = v < 0.5 ? 0 : v; if (d) this.commit('Bus lane'); }, ' m'), this.sliderField('Cycle lane (each direction)', prof.cycle || 0, 0, 3, 0.1, (v, d) => { r.overrides.cycle = v < 0.5 ? 0 : v; if (d) this.commit('Cycle lane'); }, ' m'));
        card.append(this.pillsField('Median kind', [['none', 'None'], ['painted', 'Painted'], ['raised', 'Raised kerb'], ['barrier', 'Barrier']], prof.medianKind, (v) => { r.overrides.medianKind = v; if (v !== 'none' && !(prof.median > 0)) r.overrides.median = v === 'barrier' ? 1.2 : 2.0; this.commit('Median'); }));
        card.append(this.pillsField('Surface', [['asphalt', 'Asphalt'], ['concrete', 'Concrete (jointed)']], r.surface || 'asphalt', (v) => { r.surface = v; this.commit('Surface'); }));
        card.append(h('div', { class: 'section-label' }, 'Structure'), this.pillsField('Vertical structure', [['auto', 'Auto (from height)'], ['ground', 'At grade'], ['embankment', 'Embankment'], ['bridge', 'Bridge / viaduct'], ['retained', 'Retained cutting'], ['cut', 'Open cutting'], ['tunnel', 'Tunnel']], r.structure || 'auto', (v) => { r.structure = v; this.commit('Structure'); }));
        // control points
        card.append(h('div', { class: 'section-label' }, 'Control points'));
        const cps = r.pts || [];
        const list = h('div', { class: 'sub-list' });
        cps.forEach((p, i) => list.append(h('div', { class: 'sub-item' }, h('div', { class: 'params' }, `#${i + 1}`, ...[0, 1, 2].map((k) => h('input', { class: 'num', type: 'number', step: 0.5, value: p[k].toFixed(1), onchange: (e) => { p[k] = parseFloat(e.target.value); this.commit('Move control point'); } }))), h('button', { class: 'x', html: I.x, onclick: () => { cps.splice(i, 1); this.commit('Remove control point'); } }))));
        card.append(list, h('div', { class: 'btnrow' }, h('button', { class: 'btn', html: I.plus + 'Insert midpoint', onclick: () => { const A = P.nodes[r.a].p, B = P.nodes[r.b].p; const all = [A, ...cps, B]; let bi = 0, bl = -1; for (let i = 0; i < all.length - 1; i++) { const l = V.dist(all[i], all[i + 1]); if (l > bl) { bl = l; bi = i; } } r.pts = cps; cps.splice(bi, 0, V.lerp(all[bi], all[bi + 1], 0.5)); this.commit('Insert control point'); } }),
          h('button', { class: 'btn', html: I.plus + 'Raise 7 m (overpass)', onclick: () => { const A = P.nodes[r.a].p, B = P.nodes[r.b].p; r.pts = [V.lerp(A, B, 0.3), V.lerp(A, B, 0.42), V.lerp(A, B, 0.58), V.lerp(A, B, 0.7)].map((p, i) => [p[0], i === 1 || i === 2 ? 7 : 0, p[2]]); this.commit('Overpass'); } }),
          h('button', { class: 'btn', html: I.plus + 'Dip −7 m (underpass)', onclick: () => { const A = P.nodes[r.a].p, B = P.nodes[r.b].p; r.pts = [V.lerp(A, B, 0.3), V.lerp(A, B, 0.42), V.lerp(A, B, 0.58), V.lerp(A, B, 0.7)].map((p, i) => [p[0], i === 1 || i === 2 ? -7 : 0, p[2]]); this.commit('Underpass'); } })));
        // features
        const F = r.features; const L = Rd ? Rd.length : 100;
        const featList = (key, title, mk, render) => {
          F[key] = F[key] || [];
          card.append(h('div', { class: 'section-label' }, title));
          const ul = h('div', { class: 'sub-list' });
          F[key].forEach((f, i) => ul.append(h('div', { class: 'sub-item' }, h('div', { class: 'params' }, ...render(f)), h('button', { class: 'x', html: I.x, onclick: () => { F[key].splice(i, 1); this.commit('Remove ' + title.toLowerCase()); } }))));
          card.append(ul, h('div', { class: 'btnrow' }, h('button', { class: 'btn', html: I.plus + 'Add', onclick: () => { F[key].push(mk()); this.commit('Add ' + title.toLowerCase()); } })));
        };
        const num = (obj, k, step, label) => h('span', {}, label, h('input', { class: 'num', type: 'number', step, value: Number(obj[k]).toFixed(step < 1 ? 2 : 0), onchange: (e) => { obj[k] = parseFloat(e.target.value); this.commit('Edit feature'); } }));
        featList('potholes', 'Potholes', () => ({ s: Math.round(L / 2), t: (Math.random() - 0.5) * (prof.laneWidth * 2), r: 0.6, depth: 0.06 }), (f) => [num(f, 's', 1, 's'), num(f, 't', 0.1, 't'), num(f, 'r', 0.1, 'r'), num(f, 'depth', 0.01, 'd')]);
        featList('speedBumps', 'Speed tables', () => ({ s: Math.round(L / 2), w: 3.6, h: 0.075 }), (f) => [num(f, 's', 1, 's'), num(f, 'w', 0.1, 'w'), num(f, 'h', 0.005, 'h')]);
        featList('construction', 'Road works', () => ({ s0: Math.round(L * 0.35), s1: Math.round(L * 0.65), side: 1, width: 3.4 }), (f) => [num(f, 's0', 1, 'from'), num(f, 's1', 1, 'to'), num(f, 'side', 2, 'side ±1'), num(f, 'width', 0.1, 'w')]);
        // props
        const pr = r.props;
        card.append(h('div', { class: 'section-label' }, 'Furniture'));
        card.append(this.pillsField('Street lighting', [['auto', 'Layer default'], ['on', 'On'], ['off', 'Off']], pr.streetlights == null ? 'auto' : pr.streetlights ? 'on' : 'off', (v) => { pr.streetlights = v === 'auto' ? undefined : v === 'on'; this.commit('Lighting'); }));
        card.append(this.pillsField('Lighting side', [['both', 'Staggered both'], ['left', 'Left'], ['right', 'Right']], pr.lightsSide || 'both', (v) => { pr.lightsSide = v; this.commit('Lighting side'); }));
        card.append(this.pillsField('Bollards', [['none', 'None'], ['steel', 'Steel Ø114'], ['cast', 'Cast iron'], ['concrete', 'Concrete'], ['wood', 'Timber'], ['striped', 'Reflective']], pr.bollards || 'none', (v) => { pr.bollards = v; this.commit('Bollards'); }));
        card.append(this.pillsField('Guardrail', [['auto', 'Auto'], ['both', 'Both sides'], ['none', 'None']], pr.guardrail || 'auto', (v) => { pr.guardrail = v === 'auto' ? undefined : v; this.commit('Guardrail'); }));
        pr.billboards = pr.billboards || [];
        const bl = h('div', { class: 'sub-list' });
        pr.billboards.forEach((b, i) => bl.append(h('div', { class: 'sub-item' }, h('div', { class: 'params' }, num(b, 's', 1, 's'), num(b, 'side', 2, 'side ±1')), h('button', { class: 'x', html: I.x, onclick: () => { pr.billboards.splice(i, 1); this.commit('Remove billboard'); } }))));
        card.append(h('div', { class: 'field' }, h('label', {}, 'Billboards'), h('button', { class: 'btn', html: I.plus + 'Add', onclick: () => { pr.billboards.push({ s: Math.round(L / 2), side: pr.billboards.length % 2 ? -1 : 1 }); this.commit('Add billboard'); } })), bl);
        card.append(h('div', { class: 'btnrow' }, h('button', { class: 'btn', html: I.focus + 'Frame', onclick: () => this.frameSelection() }), h('button', { class: 'btn danger', html: I.trash + 'Delete road', onclick: () => this.deleteSelection() })));
        return card;
      }
      if (sel.type === 'rail') {
        const r = P.rails[sel.id]; if (!r) return null;
        const card = h('div', { class: 'card' }, h('div', { class: 'obj-icon', style: 'color:var(--accent-e)', html: I.train }), h('div', { class: 'eyebrow' }, r.kind === 'subway' ? 'Metro line' : 'Railway'), h('div', { class: 'h1' }, r.name || r.id));
        card.append(this.textField('Name', r.name, (v) => { r.name = v; this.commit('Rename', { remesh: false }); }));
        card.append(this.pillsField('Kind', [['rail', 'Railway'], ['subway', 'Metro (underground)']], r.kind, (v) => { r.kind = v; this.commit('Rail kind'); }), this.toggleField('Electrified', !!r.electrified, (v) => { r.electrified = v; this.commit('Catenary'); }));
        card.append(this.sliderField('Trains', r.trains ?? 1, 0, 3, 1, (v, d) => { r.trains = v; if (d) this.commit('Trains'); }), this.sliderField('Cars per train', r.cars ?? 4, 1, 8, 1, (v, d) => { r.cars = v; if (d) this.commit('Train length'); }));
        card.append(h('div', { class: 'section-label' }, 'Track points'));
        const ul = h('div', { class: 'sub-list' });
        r.pts.forEach((p, i) => ul.append(h('div', { class: 'sub-item' }, h('div', { class: 'params' }, `#${i + 1}`, ...[0, 1, 2].map((k) => h('input', { class: 'num', type: 'number', step: 0.5, value: p[k].toFixed(1), onchange: (e) => { p[k] = parseFloat(e.target.value); this.commit('Move track point'); } }))), h('button', { class: 'x', html: I.x, onclick: () => { if (r.pts.length > 2) { r.pts.splice(i, 1); this.commit('Remove track point'); } } }))));
        card.append(ul, h('div', { class: 'btnrow' }, h('button', { class: 'btn', html: I.focus + 'Frame', onclick: () => this.frameSelection() }), h('button', { class: 'btn danger', html: I.trash + 'Delete railway', onclick: () => this.deleteSelection() })));
        return card;
      }
      if (sel.type === 'block') {
        const b = this.scene.blockById(sel.id); if (!b) return null;
        const card = h('div', { class: 'card' }, h('div', { class: 'obj-icon', style: 'color:var(--accent-d)', html: I.block }), h('div', { class: 'eyebrow' }, b.auto ? 'City block · generated' : 'City block'), h('div', { class: 'h1' }, b.name || b.id));
        if (b.auto) {
          const ov = () => { P.blockOverrides = P.blockOverrides || {}; return (P.blockOverrides[b.id] = P.blockOverrides[b.id] || {}); };
          card.append(h('div', { class: 'hint' }, `Traced from the footway edges of ${b.roads.length} road${b.roads.length === 1 ? '' : 's'} — it follows the network live when junctions or roads move.`));
          card.append(this.textField('Name', b.name, (v) => { ov().name = v; b.name = v; this.commit('Rename', { remesh: false }); }));
          card.append(this.pillsField('Interior', [['paving', 'Paving'], ['grass', 'Grass / park'], ['gravel', 'Gravel'], ['concrete', 'Concrete']], b.surface, (v) => { ov().surface = v; this.commit('Block surface'); }));
          card.append(h('div', { class: 'muted', style: 'margin-top:8px' }, `${b.poly.length} corners · ${b.area.toFixed(0)} m²`));
          card.append(h('div', { class: 'btnrow' }, h('button', { class: 'btn', html: I.focus + 'Frame', onclick: () => this.frameSelection() }), h('button', { class: 'btn danger', html: I.trash + 'Hide block', onclick: () => this.deleteSelection() })));
          return card;
        }
        card.append(this.textField('Name', b.name, (v) => { b.name = v; this.commit('Rename', { remesh: false }); }));
        card.append(this.pillsField('Interior', [['paving', 'Paving'], ['grass', 'Grass / park'], ['gravel', 'Gravel'], ['concrete', 'Concrete']], b.surface, (v) => { b.surface = v; this.commit('Block surface'); }), this.sliderField('Footway width', b.sidewalk ?? 3, 1, 8, 0.5, (v, d) => { b.sidewalk = v; if (d) this.commit('Footway'); }, ' m'));
        card.append(h('div', { class: 'muted', style: 'margin-top:8px' }, `${b.poly.length} corners · ${Math.abs(RF.spline.polygonArea(b.poly.map((p) => [p[0], 0, p[1]]))).toFixed(0)} m²`));
        card.append(h('div', { class: 'btnrow' }, h('button', { class: 'btn', html: I.focus + 'Frame', onclick: () => this.frameSelection() }), h('button', { class: 'btn danger', html: I.trash + 'Delete block', onclick: () => this.deleteSelection() })));
        return card;
      }
      if (sel.type === 'billboard') { const r = P.roads[sel.id]; if (!r) return null; this.selection = { type: 'road', id: r.id }; return this.objectCard(this.selection); }
      return null;
    },

    /* ---------------- selection / handles ---------------- */
    select(sel) { this.selection = sel; this.selectedLayer = null; this.scene.setSelection(sel ? [sel.id] : []); this.renderOutliner(); this.renderLayers(); this.renderInspector(); this.updateHandles(); this.needsRender = true; },
    updateHandles() {
      this.scene.clearHandles(); this.cpHandles = [];
      const sel = this.selection; if (!sel) return;
      const scale = this.handleScale();
      if (sel.type === 'node') { const n = this.project.nodes[sel.id]; if (n) this.scene.setGizmo(n.p, { scale }); }
      else if (sel.type === 'road') { this.cpHandles = this.scene.setControlPointHandles([sel.id], scale); if (sel.cp != null && this.project.roads[sel.id]?.pts?.[sel.cp]) this.scene.setGizmo(this.project.roads[sel.id].pts[sel.cp], { scale }); }
      else if (sel.type === 'rail') { const r = this.project.rails[sel.id]; if (r) { this.cpHandles = r.pts.map((p, i) => ({ road: sel.id, index: i, p, r: 1.2 * scale, rail: true })); const b = new RF.MeshBuilder('gizmo'); for (const p of r.pts) RF.prim.lathe(b, [[0.001, -0.7 * scale], [0.7 * scale, 0], [0.001, 0.7 * scale]], 10, { transform: RF.M4.translation(p[0], p[1] + 0.3, p[2]) }); this.renderer.setItem({ id: 'cph|rail', mesh: b.build(), material: { type: 'flat', color: [0.45, 0.75, 0.85], pipeline: 'overlay' }, pipeline: 'overlay' }); if (sel.cp != null) this.scene.setGizmo(r.pts[sel.cp], { scale }); } }
      else if (sel.type === 'block') { const b = this.scene.blockById(sel.id); if (b) { const mb = new RF.MeshBuilder('gizmo'); const fr = [...b.poly, b.poly[0]].map((p) => ({ p: [p[0], (b.y ?? 0) + 0.3, p[1]], d: [1, 0, 0], n: [0, 0, 1], up: [0, 1, 0] })); for (let i = 0; i < fr.length - 1; i++) { const d = V.normXZ(V.sub(fr[i + 1].p, fr[i].p)); fr[i].d = d; fr[i].n = V.perp(d); } fr[fr.length - 1].d = fr[fr.length - 2].d; fr[fr.length - 1].n = fr[fr.length - 2].n; RF.prim.sweep(mb, fr, [[-0.15 * scale, 0], [0.15 * scale, 0]], {}); this.renderer.setItem({ id: 'cph|block', mesh: mb.build(), material: { type: 'flat', color: [0.84, 0.63, 0.47], pipeline: 'overlay' }, pipeline: 'overlay' }); } }
    },
    handleScale() { return RF.clamp(this.cam.distance / 60, 0.3, 12); },
    frameSelection() {
      const sel = this.selection; const P = this.project; if (!sel) return this.frameAll();
      if (sel.type === 'node') { const n = P.nodes[sel.id]; this.cam.focus(n.p, Math.max(40, this.cam.distance * 0.6)); }
      else if (sel.type === 'road') { const Rd = this.scene.net.roads.get(sel.id); if (Rd) { const m = Rd.samples[Math.floor(Rd.samples.length / 2)].p; this.cam.focus(m, RF.clamp(Rd.length * 0.9, 40, 600)); } }
      else if (sel.type === 'rail') { const r = P.rails[sel.id]; const m = r.pts[Math.floor(r.pts.length / 2)]; this.cam.focus([m[0], 0, m[2]], 200); }
      else if (sel.type === 'block') { const b = this.scene.blockById(sel.id); if (!b) return; const c = b.poly.reduce((a, p) => [a[0] + p[0] / b.poly.length, 0, a[2] + p[1] / b.poly.length], [0, 0, 0]); this.cam.focus(c, 160); }
      this.needsRender = true; this.updateHandles();
    },
    frameAll() {
      const pts = Object.values(this.project.nodes).map((n) => n.p); if (!pts.length) return;
      let minx = 1e9, maxx = -1e9, minz = 1e9, maxz = -1e9; for (const p of pts) { minx = Math.min(minx, p[0]); maxx = Math.max(maxx, p[0]); minz = Math.min(minz, p[2]); maxz = Math.max(maxz, p[2]); }
      this.cam.focus([(minx + maxx) / 2, 0, (minz + maxz) / 2], RF.clamp(Math.max(maxx - minx, maxz - minz) * 0.9, 60, 2500)); this.needsRender = true; this.updateHandles();
    },
    deleteSelection() {
      const sel = this.selection; if (!sel) return; const P = this.project;
      if (sel.type === 'node') { for (const r of Object.values(P.roads)) if (r.a === sel.id || r.b === sel.id) delete P.roads[r.id]; delete P.nodes[sel.id]; this.pruneNodes(); this.selection = null; this.commit('Deleted junction'); }
      else if (sel.type === 'road') { if (sel.cp != null && P.roads[sel.id]?.pts) { P.roads[sel.id].pts.splice(sel.cp, 1); sel.cp = null; this.commit('Removed control point'); return; } delete P.roads[sel.id]; this.pruneNodes(); this.selection = null; this.commit('Deleted road'); }
      else if (sel.type === 'rail') { delete P.rails[sel.id]; this.selection = null; this.commit('Deleted railway'); }
      else if (sel.type === 'block') { if (P.blocks[sel.id]) delete P.blocks[sel.id]; else { P.blockOverrides = P.blockOverrides || {}; P.blockOverrides[sel.id] = { ...(P.blockOverrides[sel.id] || {}), hidden: true }; } this.selection = null; this.commit(P.blocks[sel.id] ? 'Deleted block' : 'Hid block'); }
      this.scene.setSelection([]);
    },
    pruneNodes() { const P = this.project; const used = new Set(); for (const r of Object.values(P.roads)) { used.add(r.a); used.add(r.b); } for (const id of Object.keys(P.nodes)) if (!used.has(id)) delete P.nodes[id]; },

    /* ---------------- viewport interaction ---------------- */
    bindViewport() {
      const c = this.canvas;
      c.addEventListener('contextmenu', (e) => e.preventDefault());
      c.addEventListener('pointerdown', (e) => this.onDown(e));
      window.addEventListener('pointermove', (e) => this.onMove(e));
      window.addEventListener('pointerup', (e) => this.onUp(e));
      c.addEventListener('dblclick', (e) => this.onDouble(e));
      c.addEventListener('wheel', (e) => { e.preventDefault(); this.cam.zoom(Math.exp(e.deltaY * 0.0012)); this.needsRender = true; this.updateHandles(); }, { passive: false });
    },
    ndc(e) { const r = this.canvas.getBoundingClientRect(); return [((e.clientX - r.left) / r.width) * 2 - 1, -(((e.clientY - r.top) / r.height) * 2 - 1)]; },
    rayAt(e) { const [x, y] = this.ndc(e); return this.cam.ray(x, y); },
    onDown(e) {
      this.canvas.focus();
      const orbit = e.button === 2 || (e.button === 0 && e.altKey), pan = e.button === 1 || (e.button === 0 && (e.shiftKey || this.tool === 'pan'));
      if (orbit || pan) { this.dragging = { kind: orbit ? 'orbit' : 'pan', x: e.clientX, y: e.clientY }; this.canvas.classList.add('grabbing'); return; }
      if (e.button !== 0) return;
      const ray = this.rayAt(e);
      if (this.tool === 'select') {
        // gizmo?
        const gpos = this.gizmoPos();
        if (gpos) { const axis = this.scene.pickGizmo(ray, gpos, this.handleScale()); if (axis) { this.beginEdit(); this.dragging = { kind: 'gizmo', axis, start: V.clone(gpos), origin: this.gizmoOrigin(), ray0: ray, plane: axis === 'y' ? null : gpos[1] }; this.scene.setGizmo(gpos, { scale: this.handleScale(), active: axis }); return; } }
        const hit = this.scene.pick(ray, { controlPoints: this.cpHandles });
        if (!hit || hit.type === 'ground') { this.select(null); return; }
        if (hit.type === 'cp') { const sel = { type: this.cpHandles[0]?.rail ? 'rail' : 'road', id: hit.id, cp: hit.index }; this.select(sel); this.beginEdit(); this.dragging = { kind: 'cp', sel, plane: hit.point[1] }; return; }
        this.select({ type: hit.type, id: hit.id });
        if (hit.type === 'node') { this.beginEdit(); this.dragging = { kind: 'nodeDrag', id: hit.id, plane: this.project.nodes[hit.id].p[1], moved: false, down: [e.clientX, e.clientY] }; }
        return;
      }
      if (this.tool === 'road' || this.tool === 'rail' || this.tool === 'block') { this.placePoint(ray, e); }
    },
    gizmoPos() { const sel = this.selection; if (!sel) return null; if (sel.type === 'node') return this.project.nodes[sel.id]?.p; if (sel.type === 'road' && sel.cp != null) return this.project.roads[sel.id]?.pts?.[sel.cp]; if (sel.type === 'rail' && sel.cp != null) return this.project.rails[sel.id]?.pts?.[sel.cp]; return null; },
    gizmoOrigin() { return V.clone(this.gizmoPos()); },
    onMove(e) {
      const d = this.dragging; if (!d) { if (this.tool !== 'select' && this.toolState && this.toolState.pts.length) this.updateDraft(this.rayAt(e)); return; }
      if (d.kind === 'orbit') { this.cam.orbit(e.clientX - d.x, e.clientY - d.y); d.x = e.clientX; d.y = e.clientY; this.needsRender = true; this.updateHandles(); return; }
      if (d.kind === 'pan') { const r = this.canvas.getBoundingClientRect(); this.cam.pan(e.clientX - d.x, e.clientY - d.y, r.width, r.height); d.x = e.clientX; d.y = e.clientY; this.needsRender = true; this.updateHandles(); return; }
      const ray = this.rayAt(e);
      if (d.kind === 'gizmo') {
        const p = this.gizmoPos(); if (!p) return;
        if (d.axis === 'c') { const g = this.scene.groundPoint(ray, d.plane); const g0 = this.scene.groundPoint(d.ray0, d.plane); if (g && g0) { p[0] = d.origin[0] + g[0] - g0[0]; p[2] = d.origin[2] + g[2] - g0[2]; } }
        else { const ax = { x: [1, 0, 0], y: [0, 1, 0], z: [0, 0, 1] }[d.axis]; const t0 = closestAxisT(d.ray0, d.origin, ax), t1 = closestAxisT(ray, d.origin, ax); const k = d.axis === 'y' ? 1 : 0; const q = V.madd(d.origin, ax, t1 - t0); p[0] = q[0]; p[1] = q[1]; p[2] = q[2]; if (e.ctrlKey) p[k] = Math.round(p[k] * 2) / 2; }
        this.liveRebuild(); return;
      }
      if (d.kind === 'cp') { const g = this.scene.groundPoint(ray, d.plane); if (!g) return; const sel = d.sel; const arr = sel.type === 'rail' ? this.project.rails[sel.id].pts : this.project.roads[sel.id].pts; arr[sel.cp][0] = g[0]; arr[sel.cp][2] = g[2]; this.liveRebuild(); return; }
      if (d.kind === 'nodeDrag') { if (!d.moved && Math.hypot(e.clientX - d.down[0], e.clientY - d.down[1]) < 4) return; d.moved = true; const g = this.scene.groundPoint(ray, d.plane); if (!g) return; const n = this.project.nodes[d.id]; n.p[0] = g[0]; n.p[2] = g[2]; this.liveRebuild(); }
    },
    onUp(e) {
      const d = this.dragging; if (!d) return; this.dragging = null; this.canvas.classList.remove('grabbing');
      if (d.kind === 'gizmo' || d.kind === 'cp' || (d.kind === 'nodeDrag' && d.moved)) { clearTimeout(this.rebuildTimer); this.rebuildTimer = 0; this.commit(d.kind === 'cp' ? 'Moved control point' : 'Moved junction', { intersect: true }); }
      else if (d.kind === 'nodeDrag') this._pending = null;
      void e;
    },
    liveRebuild() {
      // throttle full rebuilds while dragging
      this.needsRender = true;
      const gp = this.gizmoPos(); if (gp && this.dragging && this.dragging.kind === 'gizmo') this.scene.setGizmo(gp, { scale: this.handleScale(), active: this.dragging.axis });
      if (this.rebuildTimer) return;
      this.rebuildTimer = setTimeout(() => { this.rebuildTimer = 0; const t0 = performance.now(); this.scene.rebuild(); this.updateStats(); this.updateHandles(); this.needsRender = true; this._lastRebuildMs = performance.now() - t0; }, Math.max(60, (this._lastRebuildMs || 60) * 1.2));
    },
    onDouble(e) {
      if (this.tool === 'road' || this.tool === 'rail' || this.tool === 'block') { this.finishDraft(); return; }
      if (this.tool === 'select' && this.selection && this.selection.type === 'road') {
        // insert a control point at the clicked station
        const ray = this.rayAt(e); const hit = this.scene.pick(ray); if (!hit || hit.type !== 'road' || hit.id !== this.selection.id) return;
        const r = this.project.roads[this.selection.id]; const Rd = this.scene.net.roads.get(r.id); const c = RF.spline.closestOnPolyline(Rd.dense, Rd.cum, hit.point);
        const A = this.project.nodes[r.a].p, B = this.project.nodes[r.b].p; const all = [A, ...(r.pts || []), B]; const st = all.map((p) => RF.spline.closestOnPolyline(Rd.dense, Rd.cum, p).s);
        let idx = 0; while (idx < st.length - 1 && st[idx + 1] < c.s) idx++;
        r.pts = r.pts || []; r.pts.splice(idx, 0, [c.p[0], c.p[1], c.p[2]]); this.commit('Insert control point'); this.selection = { type: 'road', id: r.id, cp: idx }; this.updateHandles();
      }
    },
    onKey(e) {
      if (e.key === 'Escape') { const m = document.querySelector('.modal-bg'); if (m) { m.remove(); return; } const menu = document.querySelector('.menu'); if (menu) { menu.remove(); return; } }
      if (e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA')) return;
      const k = e.key.toLowerCase();
      if ((e.ctrlKey || e.metaKey) && k === 'z') { e.preventDefault(); if (e.shiftKey) this.redo(); else this.undo(); return; }
      if ((e.ctrlKey || e.metaKey) && k === 'y') { e.preventDefault(); this.redo(); return; }
      if (k === 'escape') { if (this.toolState) { this.toolState = null; this.renderer.removeByPrefix('draft|'); this.renderInspector(); this.needsRender = true; } else this.select(null); return; }
      if (k === 'enter') { if (this.toolState) this.finishDraft(); return; }
      if (k === 'delete' || k === 'backspace') { this.deleteSelection(); return; }
      const map = { v: 'select', r: 'road', t: 'rail', b: 'block', h: 'pan' };
      if (map[k] && !e.ctrlKey) { this.setTool(map[k]); return; }
      if (k === 'p') this.togglePlan(); else if (k === 'x') { this.view.xray = !this.view.xray; this.updateViewBtns(); } else if (k === 'w') { this.view.wire = !this.view.wire; this.updateViewBtns(); } else if (k === 'f') this.frameSelection(); else if (k === 'c') { this.view.traffic = !this.view.traffic; this.scene.showTraffic = this.view.traffic; if (!this.view.traffic) this.renderer.removeByPrefix('veh|'); this.updateViewBtns(); }
    },

    /* ---------------- drawing tools ---------------- */
    placePoint(ray, e) {
      const P = this.project; const ts = this.toolState || (this.toolState = { pts: [], anchors: [] });
      void e;
      if (this.tool === 'road') {
        const hit = this.scene.pick(ray, { handleScale: 1.4 });
        let anchor = null, p = null;
        if (hit && hit.type === 'node') { anchor = { node: hit.id }; p = V.clone(P.nodes[hit.id].p); }
        else if (hit && hit.type === 'road' && hit.id) { anchor = { road: hit.id, point: hit.point }; p = [hit.point[0], hit.point[1], hit.point[2]]; }
        else { const g = this.scene.groundPoint(ray, this.roadDraft.y || 0) || (hit && hit.point); if (!g) return; p = [g[0], this.roadDraft.y || 0, g[2]]; }
        if (ts.pts.length && V.distXZ(p, ts.pts[ts.pts.length - 1]) < 2) return;
        ts.pts.push(p); ts.anchors.push(anchor);
        if (anchor && ts.pts.length > 1) { this.finishDraft(); return; }
        this.updateDraft(ray); this.renderInspector();
      } else if (this.tool === 'rail') {
        const y = this.railDraft.y || 0; const g = this.scene.groundPoint(ray, 0); if (!g) return; ts.pts.push([g[0], y, g[2]]); this.updateDraft(ray); this.renderInspector();
      } else if (this.tool === 'block') {
        const g = this.scene.groundPoint(ray, 0); if (!g) return; if (ts.pts.length > 2 && V.distXZ(g, ts.pts[0]) < 4) { this.finishDraft(); return; } ts.pts.push([g[0], 0, g[2]]); this.updateDraft(ray); this.renderInspector();
      }
    },
    updateDraft(ray) {
      const ts = this.toolState; if (!ts || !ts.pts.length) return;
      const g = this.scene.groundPoint(ray, this.tool === 'road' ? this.roadDraft.y || 0 : this.tool === 'rail' ? 0 : 0);
      const pts = g ? [...ts.pts, [g[0], this.tool === 'rail' ? this.railDraft.y || 0 : ts.pts[ts.pts.length - 1][1], g[2]]] : ts.pts;
      const b = new RF.MeshBuilder('gizmo'); const scale = this.handleScale();
      const line = this.tool === 'block' && pts.length > 2 ? [...pts, pts[0]] : pts;
      const smooth = this.tool === 'block' ? line : RF.spline.samplePolyline(line, null, null, 1.0);
      const fr = smooth.map((p, i) => { const n = smooth[Math.min(i + 1, smooth.length - 1)], pv = smooth[Math.max(i - 1, 0)]; const d = V.normXZ(V.sub(n, pv)); return { p: [p[0], p[1] + 0.25, p[2]], d: d[0] || d[2] ? d : [1, 0, 0], n: V.perp(d[0] || d[2] ? d : [1, 0, 0]), up: [0, 1, 0] }; });
      const hw = this.tool === 'road' ? RF.profiles.layout(RF.PROFILES[this.roadDraft.profile] || RF.PROFILES.street).hwR : this.tool === 'rail' ? 1.8 : 0.3;
      if (fr.length > 1) RF.prim.sweep(b, fr, [[-hw, 0], [hw, 0]], {});
      for (const p of ts.pts) RF.prim.lathe(b, [[0.001, -0.6 * scale], [0.6 * scale, 0], [0.001, 0.6 * scale]], 10, { transform: RF.M4.translation(p[0], p[1] + 0.3, p[2]) });
      const mesh = b.build(); if (mesh.indices.length) this.renderer.setItem({ id: 'draft|0', mesh, material: { type: 'flat', color: [1, 0.85, 0.4], alpha: 0.45, pipeline: 'overlay' }, pipeline: 'overlay' });
      this.needsRender = true;
    },
    finishDraft() {
      const ts = this.toolState; const P = this.project; this.toolState = null; this.renderer.removeByPrefix('draft|');
      if (!ts || ts.pts.length < 2) { this.renderInspector(); this.needsRender = true; return; }
      if (this.tool === 'road') {
        const resolve = (p, anchor) => {
          if (anchor && anchor.node) return anchor.node;
          if (anchor && anchor.road && P.roads[anchor.road]) { return this.splitRoadAt(anchor.road, anchor.point); }
          const id = RF.scene.uid('n'); P.nodes[id] = { id, p: p, control: 'auto', name: 'Node' }; return id;
        };
        const a = resolve(ts.pts[0], ts.anchors[0]), b = resolve(ts.pts[ts.pts.length - 1], ts.anchors[ts.anchors.length - 1]);
        if (a === b) { this.toast('Start and end are the same node'); this.renderInspector(); return; }
        const id = RF.scene.uid('r'); const prof = this.roadDraft.profile;
        P.roads[id] = { id, name: `${RF.PROFILES[prof].name.split(' (')[0]} ${Object.keys(P.roads).length + 1}`, a, b, profile: prof, pts: ts.pts.slice(1, -1), features: {}, props: {} };
        this.selection = { type: 'road', id }; this.commit('Road added', { intersect: true });
      } else if (this.tool === 'rail') {
        const id = RF.scene.uid('t'); P.rails = P.rails || {}; P.rails[id] = { id, name: this.railDraft.kind === 'subway' ? `Metro line ${Object.keys(P.rails).length + 1}` : `Railway ${Object.keys(P.rails).length + 1}`, kind: this.railDraft.kind, electrified: this.railDraft.electrified, trains: 1, cars: 4, pts: ts.pts };
        this.selection = { type: 'rail', id }; this.commit('Railway added');
      } else if (this.tool === 'block') {
        if (ts.pts.length < 3) return; const id = RF.scene.uid('b'); P.blocks = P.blocks || {}; P.blocks[id] = { id, name: `Block ${Object.keys(P.blocks).length + 1}`, poly: ts.pts.map((p) => [p[0], p[2]]), surface: this.blockDraft.surface, sidewalk: this.blockDraft.sidewalk };
        this.selection = { type: 'block', id }; this.commit('Block added');
      }
      this.scene.setSelection(this.selection ? [this.selection.id] : []);
    },
    /** Split an existing road at a world point, returning the new node id. */
    splitRoadAt(roadId, point) {
      const P = this.project; const r = P.roads[roadId]; const Rd = this.scene.net.roads.get(roadId);
      const c = RF.spline.closestOnPolyline(Rd.dense, Rd.cum, point);
      if (c.s < 8) return r.a; if (c.s > Rd.length - 8) return r.b;
      const id = RF.scene.uid('n'); P.nodes[id] = { id, p: [c.p[0], c.p[1], c.p[2]], control: 'auto', name: 'Node' };
      const stationOf = (p) => RF.spline.closestOnPolyline(Rd.dense, Rd.cum, p).s;
      const pts = r.pts || [];
      const r1 = { ...r, id: RF.scene.uid('r'), b: id, pts: pts.filter((p) => stationOf(p) < c.s - 3) }, r2 = { ...r, id: RF.scene.uid('r'), a: id, pts: pts.filter((p) => stationOf(p) > c.s + 3) };
      delete P.roads[roadId]; P.roads[r1.id] = r1; P.roads[r2.id] = r2;
      return id;
    },

    /* ---------------- HUD ---------------- */
    renderHud() {
      const bl = $('#hud-bl'); bl.innerHTML = '';
      const hints = { select: '<b>Select</b> click junction / road · drag gizmo · <kbd>F</kbd> frame · <kbd>Del</kbd> delete', road: '<b>Road</b> click to place · click node/road to connect · <kbd>Enter</kbd> finish · <kbd>Esc</kbd> cancel', rail: '<b>Rail</b> click to place track points · <kbd>Enter</kbd> finish', block: '<b>Block</b> click corners · <kbd>Enter</kbd> or click first corner to close', pan: '<b>Pan</b> drag · wheel zoom · right-drag orbit' };
      bl.append(h('div', { class: 'chip', html: hints[this.tool] }));
      bl.append(h('div', { class: 'chip', html: 'Right-drag orbit · Shift/middle-drag pan · wheel zoom · <kbd>P</kbd> plan · <kbd>W</kbd> topology · <kbd>X</kbd> x-ray' }));
    },

    /* ---------------- environment & frame loop ---------------- */
    environment() {
      const E = this.project.layers.environment; const t = E.timeOfDay, az = (E.azimuth * Math.PI) / 180;
      const el = Math.sin(((t - 6) / 12) * Math.PI) * (Math.PI / 2) * 0.95; // elevation: 0 at 6h/18h, max at 12h
      const cosE = Math.cos(el), sinE = Math.sin(el);
      const dir = V.norm([Math.sin(az) * cosE, Math.max(sinE, -0.3), Math.cos(az) * cosE]);
      const day = RF.smoothstep(-0.08, 0.12, sinE), night = 1 - RF.smoothstep(-0.2, 0.05, sinE);
      const warm = 1 - RF.smoothstep(0.05, 0.45, sinE);
      const cloud = E.cloud || 0;
      const sunColor = [1, RF.lerp(0.97, 0.55, warm), RF.lerp(0.92, 0.3, warm)].map((c) => RF.lerp(c, 0.9, cloud * 0.6));
      const zenith = [RF.lerp(0.22, 0.45, cloud), RF.lerp(0.42, 0.5, cloud), RF.lerp(0.85, 0.6, cloud)].map((c) => c * RF.lerp(0.03, 1, day));
      const horizon = [RF.lerp(0.7, 0.62, cloud), RF.lerp(0.78, 0.66, cloud), RF.lerp(0.9, 0.72, cloud)].map((c, i) => c * RF.lerp(0.04, 1, day) * (i === 2 ? 1 : RF.lerp(1, 1.15, warm * day)));
      return { sunDir: dir, sunColor, sunIntensity: 5.5 * day * RF.lerp(1, 0.35, cloud), night, zenith, horizon, ground: [0.3, 0.3, 0.28].map((c) => c * RF.lerp(0.05, 1, day)), exposure: E.exposure * RF.lerp(1, 1.6, night), fog: E.fog, planMode: this.view.plan, wetness: this.project.layers.surface.wetness };
    },
    frame(t) {
      // ?headless=N renders N frames then exposes a PNG for automated screenshots
      if (this._headless == null) { const q = new URLSearchParams(location.search); this._headless = q.has('headless') ? parseInt(q.get('headless')) || 3 : -1; if (q.has('template') && RF.templates.list[q.get('template')]) { this.loadProject(RF.templates.list[q.get('template')].make()); this.frameAll(); } if (q.has('cam')) { const [d, yaw, pitch, tx, ty, tz] = q.get('cam').split(',').map(parseFloat); this.cam.distance = d; this.cam.yaw = yaw; this.cam.pitch = pitch; this.cam.target = [tx, ty, tz]; this.cam.update(); } if (q.has('move')) { for (const m of q.getAll('move')) { const [type, id, xyz] = m.split(':'); const v = xyz.split(',').map(parseFloat); if (type === 'node' && this.project.nodes[id]) { this.project.nodes[id].p = v; this.commit('Moved (url)'); } } } if (q.has('plan')) this.togglePlan(); if (q.has('wire')) { this.view.wire = true; this.updateViewBtns(); } if (q.has('select')) { const [type, id] = q.get('select').split(':'); this.select({ type, id }); } }
      if (this._headless > 0 && this._frames >= this._headless) { if (!window.__done) { window.__png = this.canvas.toDataURL('image/png'); window.__done = true; } return; }
      this._frames = (this._frames || 0) + 1;
      requestAnimationFrame((tt) => this.frame(tt));
      const dt = Math.min(0.1, (t - this.lastT) / 1000 || 0.016); this.lastT = t;
      const r = this.canvas.getBoundingClientRect(); this.cam.aspect = r.width / Math.max(1, r.height); this.cam.update();
      const env = this.environment(); this.env = env;
      const animate = (this.view.traffic && this.scene.traffic && this.scene.traffic.target > 0) || this.scene.trains.length > 0;
      if (animate) this.scene.tick(dt, this.cam, env);
      if (!this.needsRender && !animate && this._headless < 0) return;
      this.needsRender = false;
      this.renderer.render(this.cam, env, { xray: this.view.xray, wire: this.view.wire });
      if ((this._statT = (this._statT || 0) + dt) > 1) { this._statT = 0; this.scene.updateStats(); this.updateStats(); }
    },
  };
  function closestAxisT(ray, origin, axis) {
    // parameter along axis closest to the ray
    const w0 = V.sub(ray.o, origin); const a = V.dot(ray.d, ray.d), b = V.dot(ray.d, axis), c = V.dot(axis, axis), d = V.dot(ray.d, w0), e = V.dot(axis, w0);
    const den = a * c - b * b; if (Math.abs(den) < 1e-9) return 0; return (a * e - b * d) / den;
  }

  /** Billboard / sign atlas: 4 × 4 tiles, 512 px each. Tiles 0–7 adverts, 8 stop, 9 give way, 10 roadworks, 11 crossbuck, 12 speed 50. */
  function makeAtlas() {
    const S = 2048, T = 512; const c = document.createElement('canvas'); c.width = S; c.height = S; const g = c.getContext('2d');
    const tile = (i, fn) => { g.save(); g.translate((i % 4) * T, Math.floor(i / 4) * T); g.beginPath(); g.rect(0, 0, T, T); g.clip(); fn(); g.restore(); };
    const ads = [['#1b2a49', '#f2c14e', 'NORTHWIND', 'electric · from 36 990'], ['#f4f1ea', '#1a1a1a', 'OPEN LATE', 'coffee roasters · 5th & main'], ['#8a1c2b', '#ffffff', 'SUMMER SALE', 'everything must go'], ['#0e3b2e', '#d9f2e6', 'BREATHE', 'city parks initiative'], ['#f26b1d', '#1a1a1a', 'FAST LANE', 'tyres fitted while you wait'], ['#1a1a1a', '#e8e8e8', 'JAZZ NIGHTS', 'every friday · the vault'], ['#2f6fed', '#ffffff', 'FIBRE 10G', 'now in your street'], ['#e8dcc4', '#3a2a1a', 'OLD TOWN MARKET', 'saturdays · 7am']];
    ads.forEach(([bg, fg, big, small], i) => tile(i, () => {
      // adverts are composed on a 3:1 (1536 × 512) canvas squeezed into the square tile; the 12 × 4 m face stretches it back
      const W = T * 3; g.scale(1 / 3, 1);
      const grd = g.createLinearGradient(0, 0, W, T); grd.addColorStop(0, bg); grd.addColorStop(1, shade(bg, -25)); g.fillStyle = grd; g.fillRect(0, 0, W, T);
      g.fillStyle = fg; g.globalAlpha = 0.12; for (let k = 0; k < 9; k++) { g.beginPath(); g.arc(120 + k * 160, 400 + (k % 2) * 50, 70 + (k % 3) * 25, 0, 7); g.fill(); } g.globalAlpha = 1;
      g.font = `700 ${big.length > 9 ? 150 : 190}px "DM Sans", sans-serif`; g.textBaseline = 'middle'; g.fillText(big, 80, 200);
      g.font = '400 56px "DM Sans", sans-serif'; g.fillText(small, 84, 345); g.fillRect(80, 400, 300, 6);
    }));
    tile(8, () => { g.fillStyle = '#b11a1a'; g.fillRect(0, 0, T, T); g.fillStyle = '#fff'; g.font = '700 150px "DM Sans", sans-serif'; g.textAlign = 'center'; g.textBaseline = 'middle'; g.fillText('STOP', T / 2, T / 2); g.lineWidth = 14; g.strokeStyle = '#fff'; g.strokeRect(20, 20, T - 40, T - 40); });
    tile(9, () => { g.fillStyle = '#fff'; g.fillRect(0, 0, T, T); g.fillStyle = '#c8271c'; g.beginPath(); g.moveTo(16, 60); g.lineTo(T - 16, 60); g.lineTo(T / 2, T - 30); g.closePath(); g.fill(); g.fillStyle = '#fff'; g.beginPath(); g.moveTo(90, 100); g.lineTo(T - 90, 100); g.lineTo(T / 2, T - 110); g.closePath(); g.fill(); });
    tile(10, () => { g.fillStyle = '#fff'; g.fillRect(0, 0, T, T); g.fillStyle = '#c8271c'; g.beginPath(); g.moveTo(16, T - 30); g.lineTo(T - 16, T - 30); g.lineTo(T / 2, 40); g.closePath(); g.fill(); g.fillStyle = '#fff'; g.beginPath(); g.moveTo(80, T - 60); g.lineTo(T - 80, T - 60); g.lineTo(T / 2, 100); g.closePath(); g.fill(); g.fillStyle = '#111'; g.fillRect(200, 260, 110, 20); g.beginPath(); g.arc(256, 215, 30, 0, 7); g.fill(); g.fillRect(170, 380, 170, 22); g.fillRect(240, 280, 30, 100); });
    tile(11, () => { g.fillStyle = '#fff'; g.fillRect(0, 0, T, T); g.fillStyle = '#c8271c'; g.font = '700 60px "DM Sans", sans-serif'; g.textAlign = 'center'; g.fillText('RAILROAD', T / 2, 200); g.fillText('CROSSING', T / 2, 330); });
    tile(12, () => { g.fillStyle = '#fff'; g.beginPath(); g.arc(T / 2, T / 2, T / 2 - 6, 0, 7); g.fill(); g.lineWidth = 60; g.strokeStyle = '#c8271c'; g.beginPath(); g.arc(T / 2, T / 2, T / 2 - 40, 0, 7); g.stroke(); g.fillStyle = '#111'; g.font = '700 200px "DM Sans", sans-serif'; g.textAlign = 'center'; g.textBaseline = 'middle'; g.fillText('50', T / 2, T / 2 + 10); });
    for (let i = 13; i < 16; i++) tile(i, () => { g.fillStyle = '#9ea2a6'; g.fillRect(0, 0, T, T); });
    return c;
  }
  function shade(hex, pct) { const n = parseInt(hex.slice(1), 16); const f = (v) => Math.max(0, Math.min(255, Math.round(v + (pct / 100) * 255))); return `rgb(${f(n >> 16)},${f((n >> 8) & 255)},${f(n & 255)})`; }

  window.RoadForge = App;
  window.addEventListener('DOMContentLoaded', () => App.init());
})(globalThis.RF || (globalThis.RF = {}));

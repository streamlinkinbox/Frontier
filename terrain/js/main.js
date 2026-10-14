import { Engine, halfToFloat } from './engine.js';
import { Camera } from './camera.js';
import { Inspector, h, segCtl } from './ui.js';
import { PRESETS, defaultState } from './presets.js';
import { LEVEL, VIEW_MODES, HEIGHT_TYPES, VOLUME_TYPES, MATERIAL_TYPES, MATERIAL_PARAMS, WORLD_SECTIONS, HYBRID_PARAMS, defaultsFor } from './schema.js';
import { encodePNG, download } from './png.js';

const STORE = 'frontier.terrain.v1';
const $ = (id) => document.getElementById(id);

const HYBRID_LEVEL = { mode: LEVEL.DERIVE, threshold: LEVEL.DERIVE, softness: LEVEL.DERIVE, margin: LEVEL.DERIVE, voxel: LEVEL.DERIVE, weight: LEVEL.MESH, smoothK: LEVEL.MESH, showRegions: LEVEL.RENDER };
const SECTION_LEVEL = { world: LEVEL.HEIGHT, render: LEVEL.RENDER, env: LEVEL.RENDER, ocean: LEVEL.RENDER, maps: LEVEL.DERIVE };

class App {
  constructor() {
    this.canvas = $('gfx');
    this.engine = new Engine(this.canvas);
    this.camera = new Camera();
    this.history = []; this.future = [];
    this.needsRender = true;
    this.presetName = '';
  }

  async start() {
    try { await this.engine.init(); } catch (e) { return this.fatal(e.message); }
    this.engine.camera = this.camera;
    this.engine.onStats = (s) => { this.updateStats(); this.needsRender = true; if (this.inspector.tab === 'volume') this.refreshHybridInfo(); };
    this.engine.onError = (m) => this.toast(m.split('\n')[0], true);
    this.camera.onChange = () => { this.needsRender = true; this.saveSoon(); };
    this.camera.attach(this.canvas, () => [this.state.world.size, this.state.world.heightScale]);

    let st = null;
    const hashPreset = decodeURIComponent(location.hash.slice(1));
    if (hashPreset && PRESETS[hashPreset]) { st = PRESETS[hashPreset](); this.presetName = hashPreset; }
    else {
      try { const saved = JSON.parse(localStorage.getItem(STORE)); if (saved?.state?.layers) { st = this.sanitize(saved.state); this.presetName = saved.preset || ''; } } catch {}
    }
    if (!st) { this.presetName = 'Alpine Range'; st = PRESETS[this.presetName](); }
    const q = new URLSearchParams(location.search);
    if (q.get('res')) st.world.resolution = +q.get('res');
    if (q.get('tab')) { const t = q.get('tab').toLowerCase(); this._tab = t === 'surface' ? 'materials' : t; }
    if (q.has('still')) this.still = true;           // render only on change (debug / slow GPUs)
    if (q.get('rs')) st.render.scale = +q.get('rs');
    if (q.has('nomsaa')) st.render.msaa = false;
    this.inspector = new Inspector($('insp-body'), this);
    if (this._tab) this.inspector.tab = this._tab;
    this.setState(st, true);
    this.committed = this.snapshot();
    this.buildChrome();
    this.loop();
  }

  sanitize(s) {
    // fill keys added in newer versions
    const d = defaultState();
    for (const sec of WORLD_SECTIONS) s[sec.key] = { ...d[sec.key], ...(s[sec.key] || {}) };
    s.hybrid = { ...defaultsFor(HYBRID_PARAMS), ...(s.hybrid || {}) };
    s.view ??= 0;
    // back-fill parameters added in newer versions; drop layers of unknown type
    const fill = (arr, TYPES, params) => (arr || []).filter((it) => TYPES[it.type]).map((it) => ({ ...it, p: { ...defaultsFor(params || TYPES[it.type].params), ...(it.p || {}) } }));
    s.layers = fill(s.layers, HEIGHT_TYPES);
    s.volume = fill(s.volume, VOLUME_TYPES);
    s.materials = fill(s.materials, MATERIAL_TYPES, MATERIAL_PARAMS);
    return s;
  }

  setState(st, applyCamera) {
    this.state = st;
    this.engine.setState(st);
    if (applyCamera) {
      if (st.camera) this.camera.set({ ...st.camera, target: [...st.camera.target] });
      else this.camera.frame(st.world.size, st.world.heightScale);
    }
    this.inspector.render();
    this.buildHud?.();
    $('presetName').textContent = this.presetName ? `Preset · ${this.presetName}` : 'Custom terrain';
    this.engine.request(LEVEL.HEIGHT);
    this.needsRender = true;
  }

  loadPreset(name) {
    this.presetName = name;
    const st = PRESETS[name]();
    st.world.resolution = this.state.world.resolution; // keep the user's quality setting
    st.render = { ...st.render, ...this.state.render };
    this.setState(st, true);
    this.commitHistory();
    this.save();
  }

  // ------------------------------------------------------------------ change routing
  levelFor(path, info) {
    if (info.level !== undefined) return info.level;
    const [root, key] = path.split('.');
    if (root === 'layers') return LEVEL.HEIGHT;
    if (root === 'materials' || root === 'view') return LEVEL.RENDER;
    if (root === 'hybrid') return HYBRID_LEVEL[key] ?? LEVEL.DERIVE;
    if (root === 'volume') {
      const sig = this.primSig();
      if (sig !== this._lastPrimSig) { this._lastPrimSig = sig; return LEVEL.HEIGHT; }
      if (info.item && VOLUME_TYPES[info.item.type]?.prim) return LEVEL.HEIGHT;
      if (info.structural || info.param === 'amp') return LEVEL.DERIVE;
      return LEVEL.MESH;
    }
    if (root === 'world' && key === 'preview') return -1;
    return SECTION_LEVEL[root] ?? LEVEL.HEIGHT;
  }
  primSig() { return JSON.stringify(this.state.volume.filter((v) => v.enabled && VOLUME_TYPES[v.type].prim).map((v) => v.p)); }

  changed(path, info = {}) {
    const lvl = this.levelFor(path, info);
    if (info.begin) { this.engine.beginDrag(); this._dragging = true; return; }
    if (lvl >= 0) this.engine.request(lvl);
    if (info.end) { this._dragging = false; this.engine.endDrag(); }
    if (path === 'env.sunAz' || path === 'env.sunEl' || path.startsWith('render.')) this.engine.shadowDirty = true;
    this.needsRender = true;
    if (info.commit) { this.commitHistory(); this.saveSoon(); }
  }

  // ------------------------------------------------------------------ history / persistence
  snapshot() { return JSON.stringify({ state: { ...this.state, camera: this.camera.toJSON() }, preset: this.presetName }); }
  // controls mutate state before notifying, so history stores the last *committed* snapshot
  commitHistory() {
    const now = this.snapshot();
    if (this.committed && this.committed !== now) {
      this.history.push(this.committed); if (this.history.length > 80) this.history.shift();
      this.future = [];
    }
    this.committed = now;
  }
  undo() { if (!this.history.length) return; this.future.push(this.snapshot()); this.restore(this.history.pop()); }
  redo() { if (!this.future.length) return; this.history.push(this.snapshot()); this.restore(this.future.pop()); }
  restore(s) { const o = JSON.parse(s); this.presetName = o.preset; this.setState(this.sanitize(o.state), false); this.committed = this.snapshot(); this.save(); }
  save() { try { localStorage.setItem(STORE, this.snapshot()); } catch {} }
  saveSoon() { clearTimeout(this._saveT); this._saveT = setTimeout(() => this.save(), 400); }

  // ------------------------------------------------------------------ chrome
  buildChrome() {
    const dd = $('presetDD');
    const menu = h('div', { class: 'menu' }, h('div', { class: 'menu-g' }, 'Presets'),
      Object.keys(PRESETS).map((n) => h('button', { class: 'menu-i', onclick: () => { dd.classList.remove('open'); this.loadPreset(n); } }, n)),
      h('div', { class: 'menu-g' }, 'File'),
      h('button', { class: 'menu-i', onclick: () => { dd.classList.remove('open'); this.saveJSON(); } }, 'Save preset…', h('small', {}, '.json')),
      h('button', { class: 'menu-i', onclick: () => { dd.classList.remove('open'); this.loadJSON(); } }, 'Open preset…', h('small', {}, '.json')));
    const btn = h('button', { class: 'hbtn sm' }, h('span', { class: 'hbtn-ic', html: '<svg viewBox="0 0 16 16"><path d="M5 6l3 3 3-3" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>' }), 'Presets');
    btn.addEventListener('click', () => dd.classList.toggle('open'));
    document.addEventListener('pointerdown', (e) => { if (!dd.contains(e.target)) dd.classList.remove('open'); }, true);
    dd.append(btn, menu);
    $('undoBtn').onclick = () => this.undo();
    $('redoBtn').onclick = () => this.redo();
    $('regenBtn').onclick = () => this.engine.request(LEVEL.HEIGHT);
    $('shotBtn').onclick = () => this.screenshot();
    window.addEventListener('keydown', (e) => {
      if (e.target.tagName === 'INPUT') return;
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'z') { e.preventDefault(); e.shiftKey ? this.redo() : this.undo(); }
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'y') { e.preventDefault(); this.redo(); }
    });
    const insp = $('inspector');
    const ro = new ResizeObserver(() => {
      this.engine.viewOffsetPx = window.innerWidth > 900 ? insp.offsetWidth + 12 : 0;
      this.needsRender = true;
    });
    ro.observe(this.canvas); ro.observe(insp);
    this.buildHud();
  }
  buildHud() {
    const top = $('hudTop');
    top.innerHTML = '';
    const vs = segCtl(VIEW_MODES, this.state.view, (v) => { this.state.view = v; this.changed('view', { commit: true }); }, { small: true });
    vs.classList.add('viewseg', 'glass');
    top.append(vs);
  }
  refreshHybridInfo() {
    const el = document.querySelector('.pill-info');
    if (el) el.textContent = this.inspector._hybridInfo();
  }
  updateStats() {
    const s = this.engine.stats, el = $('stats');
    el.classList.toggle('busy', this.engine.busy);
    if (!s || !s.N) return;
    const hy = this.state.hybrid.mode;
    el.innerHTML = '';
    el.append(h('span', { class: 'dot' }),
      h('span', {}, h('b', {}, `${s.N}²`), s.preview ? ' preview' : ' heightfield'),
      h('span', {}, h('b', {}, `${Math.round(s.genMs)}`), ' ms gen'),
      hy ? h('span', {}, h('b', {}, `${s.bricks}`), ' bricks · ', h('b', {}, `${(s.voxTris / 1000).toFixed(0)}k`), ' voxel tris', s.overflow ? ' (capped)' : '') : h('span', {}, 'heightmap only'),
      h('span', {}, h('b', {}, `${(s.hmTris / 1000).toFixed(0)}k`), ' grid tris'));
  }
  toast(msg, err = false) {
    const t = $('toast');
    t.textContent = msg; t.classList.toggle('err', err); t.classList.add('show');
    clearTimeout(this._toastT); this._toastT = setTimeout(() => t.classList.remove('show'), err ? 5000 : 2200);
  }
  fatal(msg) {
    document.body.append(h('div', { class: 'fatal' }, h('div', { class: 'panel-msg' }, h('h2', {}, 'WebGPU is required'),
      h('p', {}, msg), h('p', {}, 'Use a recent Chrome, Edge, or Safari 26+ (or Firefox 141+ on Windows), with hardware acceleration enabled.'))));
  }

  // ------------------------------------------------------------------ loop
  loop() {
    let last = performance.now(), wasBusy = false;
    const tick = (t) => {
      const dt = Math.min(0.1, (t - last) / 1000); last = t;
      if (this.camera.update(dt)) this.needsRender = true;
      const animate = !this.still && this.state.ocean.enabled && this.state.view === 0;
      if (this.needsRender || animate) { this.engine.render(dt); this.needsRender = false; }
      if (wasBusy !== this.engine.busy) { wasBusy = this.engine.busy; this.updateStats(); }
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  }

  // ------------------------------------------------------------------ export
  exportPanel() {
    const b = (label, fn, cls = 'btn') => h('button', { class: cls, onclick: async () => { try { await fn(); } catch (e) { console.error(e); this.toast(String(e.message || e), true); } } }, label);
    return [
      h('div', { class: 'export-grid' },
        b('Heightmap 16-bit', () => this.exportHeight()),
        b('Normal map', () => this.exportNormals()),
        b('Maps (curv·AO·flow·mask)', () => this.exportMaps()),
        b('Erosion (depo·wear)', () => this.exportErosion()),
        b('Mesh OBJ', () => this.exportOBJ()),
        b('Screenshot', () => this.screenshot())),
      h('p', { class: 'hint' }, 'Maps export at heightfield resolution. OBJ contains the heightfield grid (voxel regions cut out) plus the voxel surface — they meet seamlessly.'),
    ];
  }
  fname(ext, tag = '') { return `frontier-${(this.presetName || 'terrain').toLowerCase().replace(/\s+/g, '-')}${tag}.${ext}`; }
  async exportHeight() {
    await this.engine.whenIdle();
    const hgt = await this.engine.readHeight(), N = this.engine.N;
    let mn = Infinity, mx = -Infinity;
    for (const v of hgt) { if (v < mn) mn = v; if (v > mx) mx = v; }
    const px = new Uint16Array(N * N), k = 65535 / Math.max(mx - mn, 1e-6);
    for (let i = 0; i < N * N; i++) px[i] = Math.round((hgt[i] - mn) * k);
    download(await encodePNG(N, N, px), this.fname('png', '-height16'));
    const Hs = this.state.world.heightScale;
    this.toast(`Heightmap ${N}² · range ${(mn * Hs).toFixed(1)} … ${(mx * Hs).toFixed(1)} m`);
  }
  async readRGBA16(tex) {
    const raw = new Uint16Array(await this.engine.readTexture(tex, 8));
    const N = this.engine.N, out = new Float32Array(N * N * 4);
    for (let i = 0; i < out.length; i++) out[i] = halfToFloat(raw[i]);
    return out;
  }
  async exportNormals() {
    await this.engine.whenIdle();
    const f = await this.readRGBA16(this.engine.R.nTex), N = this.engine.N, px = new Uint8Array(N * N * 4);
    for (let i = 0; i < N * N; i++) {
      // tangent-space style: R = +X, G = +Z (up in image = -z), B = Y
      px[i * 4] = Math.round((f[i * 4] * 0.5 + 0.5) * 255); px[i * 4 + 1] = Math.round((-f[i * 4 + 2] * 0.5 + 0.5) * 255);
      px[i * 4 + 2] = Math.round((f[i * 4 + 1] * 0.5 + 0.5) * 255); px[i * 4 + 3] = 255;
    }
    download(await encodePNG(N, N, px), this.fname('png', '-normal'));
  }
  async exportMaps() {
    await this.engine.whenIdle();
    const f = await this.readRGBA16(this.engine.R.aTex), N = this.engine.N, px = new Uint8Array(N * N * 4);
    const c = (v) => Math.max(0, Math.min(255, Math.round(v * 255)));
    for (let i = 0; i < N * N; i++) { px[i * 4] = c(f[i * 4] * 0.5 + 0.5); px[i * 4 + 1] = c(f[i * 4 + 1]); px[i * 4 + 2] = c(f[i * 4 + 2]); px[i * 4 + 3] = c(f[i * 4 + 3]); }
    download(await encodePNG(N, N, px), this.fname('png', '-maps'));
    this.toast('R curvature · G occlusion · B flow · A voxel weight');
  }
  async exportErosion() {
    await this.engine.whenIdle();
    const f = await this.readRGBA16(this.engine.R.bTex), n = await this.readRGBA16(this.engine.R.nTex), N = this.engine.N, px = new Uint8Array(N * N * 4);
    const c = (v) => Math.max(0, Math.min(255, Math.round(v * 255)));
    for (let i = 0; i < N * N; i++) { px[i * 4] = c(f[i * 4]); px[i * 4 + 1] = c(f[i * 4 + 1]); px[i * 4 + 2] = c(n[i * 4 + 3] * 0.5 + 0.5); px[i * 4 + 3] = 255; }
    download(await encodePNG(N, N, px), this.fname('png', '-erosion'));
    this.toast('R deposition · G wear · B protrusion');
  }
  async exportOBJ() {
    await this.engine.whenIdle();
    this.toast('Building OBJ…');
    const e = this.engine, N = e.N, st = this.state;
    const hgt = await e.readHeight();
    const mv = new Float32Array(await e.readBuffer(e.R.mVox, N * N * 4));
    const vox = await e.readVoxelMesh();
    const M = Math.min(N, 1024), S = st.world.size, Hs = st.world.heightScale;
    // streamed writer: compact ~4 MB string chunks instead of millions of tiny strings (avoids OOM on big meshes)
    const parts = []; let buf = '';
    const w = (line) => { buf += line + '\n'; if (buf.length > 4e6) { parts.push(buf); buf = ''; } };
    const breathe = () => new Promise((r) => setTimeout(r, 0));
    const samp = (u, v) => { const x = Math.round(u * (N - 1)), y = Math.round(v * (N - 1)); return y * N + x; };
    const useMask = st.hybrid.mode > 0;

    // weld the voxel triangle soup (MC emits 3 unique vertices per triangle) with a typed-array hash grid
    const nv = vox.length / 8;
    let vPos = null, vNrm = null, vCount = 0, tri = null, triCount = 0;
    if (nv) {
      let cap = 1; while (cap < nv * 2) cap <<= 1;
      const keys = new Int32Array(cap * 3), slot = new Int32Array(cap).fill(-1), mask = cap - 1;
      const remap = new Int32Array(nv);
      vPos = new Float32Array(nv * 3); vNrm = new Float32Array(nv * 3);
      const q = 1 / 0.005; // 5 mm weld tolerance
      for (let k = 0; k < nv; k++) {
        const x = Math.round(vox[k * 8] * q), y = Math.round(vox[k * 8 + 1] * q), z = Math.round(vox[k * 8 + 2] * q);
        let hsh = (Math.imul(x, 73856093) ^ Math.imul(y, 19349663) ^ Math.imul(z, 83492791)) & mask;
        for (;;) {
          const id = slot[hsh];
          if (id < 0) {
            slot[hsh] = vCount; keys.set([x, y, z], hsh * 3);
            vPos.set([vox[k * 8], vox[k * 8 + 1], vox[k * 8 + 2]], vCount * 3);
            remap[k] = vCount++; break;
          }
          if (keys[hsh * 3] === x && keys[hsh * 3 + 1] === y && keys[hsh * 3 + 2] === z) { remap[k] = id; break; }
          hsh = (hsh + 1) & mask;
        }
        const o = remap[k] * 3;
        vNrm[o] += vox[k * 8 + 4]; vNrm[o + 1] += vox[k * 8 + 5]; vNrm[o + 2] += vox[k * 8 + 6];
      }
      tri = new Int32Array(nv);
      for (let t = 0; t < nv / 3; t++) {
        const a = remap[t * 3], b = remap[t * 3 + 1], c = remap[t * 3 + 2];
        if (a === b || b === c || a === c) continue; // degenerate after welding
        tri[triCount * 3] = a; tri[triCount * 3 + 1] = c; tri[triCount * 3 + 2] = b; triCount++;
      }
    }

    w(`# Frontier terrain — ${this.presetName || 'custom'}`);
    w(`# heightfield ${M}x${M} (${S} m, height scale ${Hs} m), voxel surface ${vCount} verts / ${triCount} tris`);
    w('o heightfield');
    for (let j = 0; j < M; j++) {
      for (let i = 0; i < M; i++) {
        const u = i / (M - 1), v = j / (M - 1);
        w(`v ${(u * S).toFixed(2)} ${(hgt[samp(u, v)] * Hs).toFixed(2)} ${(v * S).toFixed(2)}`);
      }
      if ((j & 63) === 63) await breathe();
    }
    for (let j = 0; j < M - 1; j++) {
      for (let i = 0; i < M - 1; i++) {
        const a = j * M + i + 1, b = a + 1, c = a + M, d = c + 1;
        if (useMask && mv[samp((i + 0.5) / (M - 1), (j + 0.5) / (M - 1))] >= 0.5) continue;
        w(`f ${a} ${c} ${b}`); w(`f ${b} ${c} ${d}`);
      }
      if ((j & 63) === 63) await breathe();
    }
    if (triCount) {
      w('o voxel_surface');
      const base = M * M;
      for (let k = 0; k < vCount; k++) {
        w(`v ${vPos[k * 3].toFixed(3)} ${vPos[k * 3 + 1].toFixed(3)} ${vPos[k * 3 + 2].toFixed(3)}`);
        if ((k & 0xffff) === 0xffff) await breathe();
      }
      for (let k = 0; k < vCount; k++) {
        const x = vNrm[k * 3], y = vNrm[k * 3 + 1], z = vNrm[k * 3 + 2], l = Math.hypot(x, y, z) || 1;
        w(`vn ${(x / l).toFixed(3)} ${(y / l).toFixed(3)} ${(z / l).toFixed(3)}`);
        if ((k & 0xffff) === 0xffff) await breathe();
      }
      for (let t = 0; t < triCount; t++) {
        const a = tri[t * 3] + 1, b = tri[t * 3 + 1] + 1, c = tri[t * 3 + 2] + 1;
        w(`f ${base + a}//${a} ${base + b}//${b} ${base + c}//${c}`);
        if ((t & 0xffff) === 0xffff) await breathe();
      }
    }
    parts.push(buf);
    download(new Blob(parts, { type: 'text/plain' }), this.fname('obj'));
    this.toast(`OBJ exported · ${(parts.reduce((s, p) => s + p.length, 0) / 1e6).toFixed(1)} MB`);
  }
  screenshot() {
    this.engine.render(0);
    this.canvas.toBlob((b) => b && download(b, this.fname('png', '-shot')), 'image/png');
  }
  saveJSON() { download(new Blob([JSON.stringify({ frontier: 1, preset: this.presetName, state: { ...this.state, camera: this.camera.toJSON() } }, null, 1)], { type: 'application/json' }), this.fname('json')); }
  loadJSON() {
    const inp = h('input', { type: 'file', accept: '.json,application/json' });
    inp.onchange = async () => {
      try {
        const o = JSON.parse(await inp.files[0].text());
        this.presetName = o.preset || ''; this.setState(this.sanitize(o.state), true); this.commitHistory(); this.save();
      } catch (e) { this.toast('Could not read preset: ' + e.message, true); }
    };
    inp.click();
  }
}

const app = new App();
window.frontier = app;
app.start();

// DOM interface: tool palette, road presets, layer options, inspector, topology panel.
import { PRESETS, PRESET_KEYS, LANE_TYPES, LANE_TYPE_KEYS, BRIDGE_TYPES, PROP_KINDS } from '../core/presets.js';
import { lanesFromPreset, makeLook, roadWidth } from '../core/model.js';
import { mulberry32 } from '../core/rng.js';

const $ = (id) => document.getElementById(id);
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

const TOOL_HINTS = {
  select: 'Click a road or prop to select it. Drag a green/yellow handle to reshape. Double-click a road to add a point.',
  draw: 'Click to add points (snaps to junctions and roads). Enter or double-click to finish. Backspace removes the last point.',
  props: 'Click a road to place the selected prop. Street lamps stand at the kerb nearest your click.',
  delete: 'Click a road or prop to delete it. Ctrl+Z restores it.',
};

export class UI {
  constructor() {
    this.editor = null;
    this.filterText = '';
    this.bindStatic();
  }

  attach(editor) {
    this.editor = editor;
    this.renderPalettes();
    this.onToolChanged();
    this.onDrawChanged();
    this.onSelectionChanged();
    this.onModelChanged();
  }

  bindStatic() {
    document.querySelectorAll('[data-tool]').forEach((b) => b.addEventListener('click', () => this.editor.setTool(b.dataset.tool)));
    document.querySelectorAll('#layerSeg [data-layer]').forEach((b) => b.addEventListener('click', () => {
      this.editor.draw.layer.mode = b.dataset.layer;
      this.onDrawChanged();
    }));
    $('drawBridge').innerHTML = Object.entries(BRIDGE_TYPES).map(([k, v]) => `<option value="${k}">${esc(v.label)}</option>`).join('');
    $('drawBridge').addEventListener('change', (e) => { this.editor.draw.layer.bridge = e.target.value; this.onDrawChanged(); });
    $('drawClear').addEventListener('change', (e) => { this.editor.draw.layer.clearance = clampNum(e.target.value, 4, 40, 9); this.onDrawChanged(); });
    $('drawDepth').addEventListener('change', (e) => { this.editor.draw.layer.depth = clampNum(e.target.value, 3, 60, 8); this.onDrawChanged(); });

    $('btnNew').addEventListener('click', () => { if (confirm('Start a new empty project? Unsaved changes stay in undo history.')) this.editor.newProject(); });
    $('btnSave').addEventListener('click', () => download(`${(this.editor.model.name || 'project').replace(/\W+/g, '-')}.frontier.json`, JSON.stringify(this.editor.model, null, 2)));
    $('btnOpen').addEventListener('click', () => $('fileOpen').click());
    $('fileOpen').addEventListener('change', async (e) => {
      const f = e.target.files[0];
      if (!f) return;
      try { this.editor.load(JSON.parse(await f.text())); this.toast(`Opened ${f.name}`); } catch (err) { this.toast(`Could not open: ${err.message}`); }
      e.target.value = '';
    });
    $('btnUndo').addEventListener('click', () => this.editor.undo());
    $('btnRedo').addEventListener('click', () => this.editor.redo());
    $('btnGen').addEventListener('click', () => this.editor.generate(clampNum($('seed').value, 1, 999999, 1)));
    $('btnXray').addEventListener('click', () => this.toggleUnderground());
    $('filter').addEventListener('input', (e) => { this.filterText = e.target.value.toLowerCase(); this.renderRoadList(); });
    $('roadList').addEventListener('click', (e) => {
      const row = e.target.closest('[data-road]');
      if (!row) return;
      this.editor.sel = { type: 'road', id: row.dataset.road };
      this.editor.focusSelection();
      this.editor.setTool('select');
      this.onSelectionChanged();
    });
    this.viewportEl = $('viewport');
  }

  toggleUnderground() {
    this._xray = !this._xray;
    this.editor.viewer.setUnderground(this._xray);
    $('btnXray').classList.toggle('on', this._xray);
    this.toast(this._xray ? 'Underground view on (subways visible)' : 'Underground view off');
  }

  renderPalettes() {
    $('presets').innerHTML = PRESET_KEYS.map((k) => {
      const p = PRESETS[k];
      const w = p.lanes.reduce((a, l) => a + l[1], 0).toFixed(1);
      const summary = [...new Set(p.lanes.map((l) => LANE_TYPES[l[0]].label.split(' ')[0]))].join(' · ');
      return `<button class="preset" data-preset="${k}"><b>${esc(p.label)}</b><span>${w} m · ${esc(summary)}</span></button>`;
    }).join('');
    $('presets').querySelectorAll('[data-preset]').forEach((b) => b.addEventListener('click', () => {
      this.editor.draw.preset = b.dataset.preset;
      this.editor.setTool('draw');
      this.onDrawChanged();
      this.toast(`Drawing: ${PRESETS[b.dataset.preset].label}`);
    }));
    $('propKinds').innerHTML = Object.entries(PROP_KINDS).map(([k, v]) => `<button class="prop" data-prop="${k}">${esc(v.label)}</button>`).join('');
    $('propKinds').querySelectorAll('[data-prop]').forEach((b) => b.addEventListener('click', () => {
      this.editor.propKind = b.dataset.prop;
      this.editor.setTool('props');
      this.onToolChanged();
    }));
  }

  onToolChanged() {
    const ed = this.editor;
    document.querySelectorAll('[data-tool]').forEach((b) => b.classList.toggle('on', b.dataset.tool === ed.tool));
    $('toolHint').textContent = TOOL_HINTS[ed.tool];
    $('modeBadge').textContent = { select: 'SELECT', draw: 'DRAW ROAD', props: 'PROPS', delete: 'DELETE' }[ed.tool];
    $('modeBadge').className = `badge ${ed.tool}`;
    $('propKinds').querySelectorAll('[data-prop]').forEach((b) => b.classList.toggle('on', b.dataset.prop === ed.propKind));
    $('drawCard').classList.toggle('dim', ed.tool !== 'draw');
    $('propCard').classList.toggle('dim', ed.tool !== 'props');
    this.updatePresetSel();
  }

  updatePresetSel() {
    const cur = this.editor.draw.preset;
    $('presets').querySelectorAll('[data-preset]').forEach((b) => b.classList.toggle('on', b.dataset.preset === cur));
  }

  onDrawChanged() {
    const d = this.editor.draw;
    document.querySelectorAll('#layerSeg [data-layer]').forEach((b) => b.classList.toggle('on', b.dataset.layer === d.layer.mode));
    $('bridgeRow').hidden = d.layer.mode !== 'bridge';
    $('clearRow').hidden = d.layer.mode !== 'bridge';
    $('depthRow').hidden = d.layer.mode !== 'subway';
    $('drawBridge').value = d.layer.bridge;
    $('drawClear').value = d.layer.clearance;
    $('drawDepth').value = d.layer.depth;
    this.updatePresetSel();
    if (this.editor.tool === 'draw') $('toolHint').textContent = `${TOOL_HINTS.draw}  Points: ${d.points.length}`;
    this.editor.refreshOverlay();
  }

  onSelectionChanged() {
    this.renderInspector();
    this.renderRoadList();
    this.editor.refreshOverlay();
  }

  onModelChanged() {
    const st = this.editor.stats();
    $('stats').innerHTML = [
      stat('roads', st.roads), stat('junctions', st.junctions ?? 0), stat('edges', st.edges ?? 0),
      stat('bridges', this.editor.model.roads.filter((r) => r.layer.mode === 'bridge').length),
      stat('props', st.props), stat('build', `${st.buildMs ?? 0} ms`),
    ].join('');
    $('roadCount').textContent = `(${this.editor.model.roads.length})`;
    this.renderTopology();
    this.renderRoadList();
    this.renderInspector();
  }

  onCursor(p) {
    $('cursor').textContent = p ? `x ${p.x.toFixed(1)}  z ${p.z.toFixed(1)}  h ${p.y.toFixed(1)} m` : '—';
  }

  renderTopology() {
    const net = this.editor.net;
    if (!net) return;
    const s = net.stats;
    $('topoKV').innerHTML = [
      ['Road parts', s.roads], ['Edges (between junctions)', s.edges], ['Nodes', s.nodes],
      ['Junctions (3+ arms)', s.junctions], ['Grade separations', s.grade],
      ['Level crossings', net.crossings.filter((c) => c.kind === 'level-crossing').length],
    ].map(([k, v]) => `<div><span>${esc(k)}</span><b>${v}</b></div>`).join('');
    $('warnings').innerHTML = net.warnings.length
      ? net.warnings.slice(0, 12).map((w) => `<div class="warn">${esc(w)}</div>`).join('') + (net.warnings.length > 12 ? `<div class="warn">…and ${net.warnings.length - 12} more</div>` : '')
      : '<div class="ok">Topology clean: no overlapping roads.</div>';
  }

  renderRoadList() {
    const ed = this.editor;
    const rows = ed.model.roads
      .filter((r) => !this.filterText || r.name.toLowerCase().includes(this.filterText) || r.preset.includes(this.filterText))
      .map((r) => {
        const tag = r.layer.mode === 'bridge' ? `bridge · ${BRIDGE_TYPES[r.layer.bridge]?.label.split(' ')[0] || ''}` : r.layer.mode === 'subway' ? 'subway' : r.kind === 'rail' ? 'railway' : 'ground';
        const on = ed.sel?.type === 'road' && ed.sel.id === r.id ? ' on' : '';
        return `<button class="row-item${on}" data-road="${r.id}"><span>${esc(r.name)}</span><small>${esc(tag)}</small></button>`;
      });
    $('roadList').innerHTML = rows.join('') || '<div class="empty">No roads yet. Press R and click to draw one, or Generate city.</div>';
  }

  renderInspector() {
    const ed = this.editor;
    const box = $('inspector');
    const road = ed.selectedRoad();
    const prop = ed.selectedProp();
    if (road) return this.renderRoadInspector(box, road);
    if (prop) return this.renderPropInspector(box, prop);
    box.innerHTML = `<h3>Inspector</h3><p class="hint">Nothing selected.</p>
      <p class="hint">Draw roads with <b>R</b>, pick a road type from the left panel, choose ground / bridge / subway, and click to place points.</p>`;
  }

  renderRoadInspector(box, road) {
    const ed = this.editor;
    const total = roadWidth(road).toFixed(1);
    const layerMode = road.layer.mode;
    const lanesHtml = road.lanes.map((l, i) => `
      <div class="lane" data-i="${i}" style="--c:${LANE_TYPES[l.type]?.color || '#333'}">
        <i></i>
        <select data-lane-type="${i}">${LANE_TYPE_KEYS.map((k) => `<option value="${k}" ${k === l.type ? 'selected' : ''}>${esc(LANE_TYPES[k].label)}</option>`).join('')}</select>
        <input type="number" step="0.1" min="0.1" max="20" value="${l.width}" data-lane-w="${i}" title="Width (m)" />
        <select data-lane-dir="${i}" title="Direction">
          <option value="" ${!l.dir ? 'selected' : ''}>—</option>
          <option value="fwd" ${l.dir === 'fwd' ? 'selected' : ''}>fwd</option>
          <option value="back" ${l.dir === 'back' ? 'selected' : ''}>back</option>
        </select>
        <button class="mini-btn" data-lane-up="${i}" title="Move left">◀</button>
        <button class="mini-btn" data-lane-down="${i}" title="Move right">▶</button>
        <button class="mini-btn danger" data-lane-del="${i}" title="Remove lane">✕</button>
      </div>`).join('');
    box.innerHTML = `
      <h3>Road</h3>
      <div class="row"><label>Name</label><input id="rName" value="${esc(road.name)}" /></div>
      <div class="row"><label>Road type</label>
        <select id="rPreset">${PRESET_KEYS.map((k) => `<option value="${k}" ${k === road.preset ? 'selected' : ''}>${esc(PRESETS[k].label)}</option>`).join('')}</select>
      </div>
      <div class="row"><label>Reset lanes to type</label><button class="btn small" id="rResetLanes">Apply type</button></div>
      <div class="row"><label>Level</label>
        <select id="rLayer">
          <option value="ground" ${layerMode === 'ground' ? 'selected' : ''}>Ground</option>
          <option value="bridge" ${layerMode === 'bridge' ? 'selected' : ''}>Bridge (elevated)</option>
          <option value="subway" ${layerMode === 'subway' ? 'selected' : ''}>Subway (underground)</option>
        </select>
      </div>
      ${layerMode === 'bridge' ? `
        <div class="row"><label>Bridge model</label>
          <select id="rBridge">${Object.entries(BRIDGE_TYPES).map(([k, v]) => `<option value="${k}" ${k === road.layer.bridge ? 'selected' : ''}>${esc(v.label)}</option>`).join('')}</select>
        </div>
        <div class="row"><label>Clearance (m)</label><input id="rClear" type="number" min="4" max="40" step="0.5" value="${road.layer.clearance}" /></div>` : ''}
      ${layerMode === 'subway' ? `<div class="row"><label>Depth (m)</label><input id="rDepth" type="number" min="3" max="60" step="0.5" value="${road.layer.depth}" /></div>` : ''}
      ${layerMode !== 'ground' ? `<div class="row"><label>Ramp length (m)</label><input id="rRamp" type="number" min="4" max="200" step="1" value="${road.layer.rampLength}" /></div>` : ''}
      <div class="sub">Lanes · ${total} m total</div>
      <div class="lanes">${lanesHtml}</div>
      <div class="row addlane">
        <select id="addType">${LANE_TYPE_KEYS.map((k) => `<option value="${k}">${esc(LANE_TYPES[k].label)}</option>`).join('')}</select>
        <input id="addW" type="number" step="0.1" value="3" />
        <button class="btn small" id="addLane">Add lane</button>
      </div>
      <div class="sub">Look</div>
      <div class="row"><label>Seed</label><span class="mono">${road.seed}</span></div>
      <div class="row"><label>Look</label><button class="btn small" id="rReroll">Re-roll wear & markings</button></div>
      <div class="row btns">
        <button class="btn small" id="rReverse">Reverse</button>
        <button class="btn small" id="rDup">Duplicate</button>
        <button class="btn small danger" id="rDelete">Delete road</button>
      </div>
      <p class="hint">${road.points.length} control points · ${esc(road.kind === 'rail' ? 'railway' : road.preset)} · merges with same-level roads at junctions.</p>`;
    this.bindRoadInspector(road);
  }

  bindRoadInspector(road) {
    const ed = this.editor;
    const m = (fn) => ed.mutate(() => fn(this.editor.selectedRoad()));
    const on = (id, ev, fn) => { const el = $(id); if (el) el.addEventListener(ev, fn); };
    on('rName', 'change', (e) => m((r) => { r.name = e.target.value.slice(0, 60); }));
    on('rPreset', 'change', (e) => m((r) => {
      r.preset = e.target.value;
      r.lanes = lanesFromPreset(r.preset);
      r.kind = PRESETS[r.preset].kind || 'road';
    }));
    on('rResetLanes', 'click', () => m((r) => { r.lanes = lanesFromPreset(r.preset); }));
    on('rLayer', 'change', (e) => m((r) => { r.layer.mode = e.target.value; }));
    on('rBridge', 'change', (e) => m((r) => { r.layer.bridge = e.target.value; }));
    on('rClear', 'change', (e) => m((r) => { r.layer.clearance = clampNum(e.target.value, 4, 40, 9); }));
    on('rDepth', 'change', (e) => m((r) => { r.layer.depth = clampNum(e.target.value, 3, 60, 8); }));
    on('rRamp', 'change', (e) => m((r) => { r.layer.rampLength = clampNum(e.target.value, 4, 200, 32); }));
    document.querySelectorAll('[data-lane-type]').forEach((el) => el.addEventListener('change', (e) => m((r) => { r.lanes[+el.dataset.laneType].type = e.target.value; })));
    document.querySelectorAll('[data-lane-w]').forEach((el) => el.addEventListener('change', (e) => m((r) => { r.lanes[+el.dataset.laneW].width = clampNum(e.target.value, 0.1, 20, 3); })));
    document.querySelectorAll('[data-lane-dir]').forEach((el) => el.addEventListener('change', (e) => m((r) => { r.lanes[+el.dataset.laneDir].dir = e.target.value || null; })));
    document.querySelectorAll('[data-lane-up]').forEach((el) => el.addEventListener('click', () => m((r) => swap(r.lanes, +el.dataset.laneUp, +el.dataset.laneUp - 1))));
    document.querySelectorAll('[data-lane-down]').forEach((el) => el.addEventListener('click', () => m((r) => swap(r.lanes, +el.dataset.laneDown, +el.dataset.laneDown + 1))));
    document.querySelectorAll('[data-lane-del]').forEach((el) => el.addEventListener('click', () => m((r) => { r.lanes.splice(+el.dataset.laneDel, 1); })));
    on('addLane', 'click', () => {
      const type = $('addType').value;
      const w = clampNum($('addW').value, 0.1, 20, 3);
      m((r) => { r.lanes.push({ type, width: w, dir: LANE_TYPES[type].parking ? null : (r.lanes.some((l) => l.dir === 'fwd') ? 'back' : 'fwd') }); });
    });
    on('rReroll', 'click', () => m((r) => { r.seed = (Math.random() * 2 ** 31) >>> 0; r.look = makeLook(r.seed); }));
    on('rReverse', 'click', () => m((r) => { r.points.reverse(); }));
    on('rDup', 'click', () => {
      const copy = JSON.parse(JSON.stringify(road));
      copy.id = `${road.id}-copy${Math.floor(Math.random() * 1e6)}`;
      copy.name = `${road.name} (copy)`;
      copy.points = copy.points.map((p) => ({ x: p.x + 12, z: p.z + 12 }));
      copy.seed = (Math.random() * 2 ** 31) >>> 0;
      copy.look = makeLook(copy.seed);
      ed.mutate(() => ed.model.roads.push(copy));
      ed.sel = { type: 'road', id: copy.id };
      this.onSelectionChanged();
    });
    on('rDelete', 'click', () => ed.deleteSelection());
  }

  renderPropInspector(box, prop) {
    box.innerHTML = `
      <h3>Prop · ${esc(PROP_KINDS[prop.kind]?.label || prop.kind)}</h3>
      <div class="row"><label>Kind</label>
        <select id="pKind">${Object.entries(PROP_KINDS).map(([k, v]) => `<option value="${k}" ${k === prop.kind ? 'selected' : ''}>${esc(v.label)}</option>`).join('')}</select>
      </div>
      ${prop.kind === 'bollard' ? `<div class="row"><label>Row length (m)</label><input id="pLen" type="number" min="1" max="200" step="0.5" value="${prop.length || 8}" /></div>` : ''}
      ${prop.kind === 'speedbump' ? `<div class="row"><label>Width (m)</label><input id="pWidth" type="number" min="2" max="30" step="0.5" value="${prop.width || 6}" /></div>` : ''}
      <div class="row"><label>Angle (°)</label><input id="pAng" type="number" step="5" value="${Math.round(((prop.angle || 0) * 180) / Math.PI)}" /></div>
      <div class="row"><label>Position</label><span class="mono">${prop.x.toFixed(1)}, ${prop.z.toFixed(1)}</span></div>
      <div class="row btns"><button class="btn small danger" id="pDelete">Delete prop</button></div>
      <p class="hint">Drag the prop with the Select tool to move it.</p>`;
    const ed = this.editor;
    const m = (fn) => ed.mutate(() => fn(ed.selectedProp()));
    const on = (id, ev, fn) => { const el = $(id); if (el) el.addEventListener(ev, fn); };
    on('pKind', 'change', (e) => m((p) => { p.kind = e.target.value; }));
    on('pLen', 'change', (e) => m((p) => { p.length = clampNum(e.target.value, 1, 200, 8); }));
    on('pWidth', 'change', (e) => m((p) => { p.width = clampNum(e.target.value, 2, 30, 6); }));
    on('pAng', 'change', (e) => m((p) => { p.angle = (Number(e.target.value) || 0) * Math.PI / 180; }));
    on('pDelete', 'click', () => ed.deleteSelection());
  }

  toast(msg) {
    const t = $('toast');
    t.textContent = msg;
    t.classList.add('show');
    clearTimeout(this._tt);
    this._tt = setTimeout(() => t.classList.remove('show'), 2600);
  }

  fps(v) { $('fps').textContent = `${v} fps`; }
}

function stat(label, value) {
  return `<div class="stat"><span>${esc(label)}</span><b>${esc(value)}</b></div>`;
}

function clampNum(v, lo, hi, dflt) {
  const n = Number(v);
  if (!Number.isFinite(n)) return dflt;
  return Math.max(lo, Math.min(hi, n));
}

function swap(arr, i, j) {
  if (j < 0 || j >= arr.length) return;
  [arr[i], arr[j]] = [arr[j], arr[i]];
}

function download(name, text) {
  const blob = new Blob([text], { type: 'application/json' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

export { mulberry32 };

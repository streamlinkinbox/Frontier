// Frontier Editor chrome: outliner (left) · viewport (centre) · inspector
// cards (right). Same visual language as CliffGenerator/style.css
// (DM Sans, near-black panels, 22px cards, pill toggles, hairline strokes)
// so the terrain/water/vegetation phases share one theme.

const ICONS = {
  layers: '<path d="m12 2 9 5-9 5-9-5 9-5Z"/><path d="m3 12 9 5 9-5"/><path d="m3 17 9 5 9-5"/>',
  mountain: '<path d="m8 3 4 8 5-5 5 15H2L8 3Z"/>',
  strata: '<path d="M3 6h18"/><path d="M4 12h16"/><path d="M3 18h18"/>',
  brush: '<path d="m14 5 5 5L9 20H4v-5L14 5Z"/><path d="m12 7 5 5"/>',
  erode: '<path d="M4 19c3-1 4-5 8-6 3-.8 4-3 8-4"/><path d="M4 8c2 2 5 1 7 3"/><path d="M18 17l2 2"/>',
  waves: '<path d="M2 8c2.5 0 2.5 2 5 2s2.5-2 5-2 2.5 2 5 2 2.5-2 5-2"/><path d="M2 15c2.5 0 2.5 2 5 2s2.5-2 5-2 2.5 2 5 2 2.5-2 5-2"/>',
  sprout: '<path d="M12 21v-8"/><path d="M12 13c0-4 3-7 8-7 0 5-3 8-8 7Z"/><path d="M12 11C12 7 9 4 4 4c0 5 3 8 8 7Z"/>',
  eye: '<path d="M2 12s3.5-6 10-6 10 6 10 6-3.5 6-10 6-10-6-10-6Z"/><circle cx="12" cy="12" r="3"/>',
  export: '<path d="M12 3v12"/><path d="m7 10 5 5 5-5"/><path d="M4 21h16"/>',
  play: '<path d="m7 4 13 8-13 8V4Z"/>',
  camera: '<path d="M4 8h3l2-3h6l2 3h3v12H4V8Z"/><circle cx="12" cy="13" r="3.5"/>',
  frame: '<path d="M4 9V4h5"/><path d="M20 9V4h-5"/><path d="M4 15v5h5"/><path d="M20 15v5h-5"/>',
  cave: '<path d="M4 21V11a8 8 0 0 1 16 0v10"/><path d="M9 21v-6a3 3 0 0 1 6 0v6"/>',
  move: '<path d="M12 3v18"/><path d="M3 12h18"/><path d="m8 7 4-4 4 4"/><path d="m8 17 4 4 4-4"/>',
  check: '<path d="m4 12 5 5L20 6"/>',
};
export const icon = (name, size = 15, sw = 1.7) =>
  `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="${sw}" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${ICONS[name] ?? ICONS.layers}</svg>`;

function el(tag, cls, html) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (html != null) e.innerHTML = html;
  return e;
}
const fmtVal = (ctrl, v) => {
  if (ctrl.fmt) return ctrl.fmt(v);
  const n = Number(v);
  const s = Number.isFinite(n) ? (Math.abs(n) >= 100 ? n.toFixed(0) : Math.abs(n) >= 10 ? n.toFixed(1) : n.toFixed(2)) : String(v);
  return ctrl.unit ? `${s} ${ctrl.unit}` : s;
};

export class EditorUI {
  constructor(root, defs, hooks) {
    this.root = root;
    this.defs = defs;               // { sections, groups: {id: {title, sub, icon, badge, cards:[]}}, tools: [] }
    this.hooks = hooks;             // { getValue, setValue, onAction, onTool, onSelect }
    this.selected = defs.sections[0].ids[0];
    this.tool = defs.tools[0]?.id;
    this.controls = new Map();      // key -> {input?, output?, ctrl, sync()}
    this.stats = new Map();
    this.buildShell();
    this.renderOutliner();
    this.renderInspector();
    this.setTool(this.tool);
  }

  buildShell() {
    this.root.innerHTML = '';
    const shell = el('main', 'shell');
    // ---- outliner
    const outliner = el('aside', 'outliner');
    outliner.append(
      el('div', 'brand', `<span class="brand-symbol">${icon('layers', 24, 1.5)}</span><span>frontier<span class="brand-dot">.</span></span><span class="version">ENV / 01</span>`),
      el('div', 'scene-label', 'WORKSPACE <span class="status-dot"></span>'),
      el('div', 'scene-title', '<span>Environment editor</span><span class="scene-extension">.scene</span>'),
      el('div', 'outliner-heading', `<h2>Outliner <span>${String(Object.keys(this.defs.groups).length).padStart(2, '0')}</span></h2>`),
    );
    this.tree = el('div', 'outliner-tree');
    outliner.append(this.tree);
    const bottom = el('div', 'outliner-bottom',
      `<div class="world-icon">${icon('mountain', 19)}</div><div><strong>Escarpment study</strong><span>SDF patch · 96 × 64 × 96 m</span></div><span class="little-dot"></span>`);
    outliner.append(bottom);

    // ---- viewport
    const viewport = el('section', 'viewport');
    const top = el('div', 'viewport-top');
    this.crumbs = el('div', 'crumbs');
    const progress = el('div', 'progress-wrap');
    this.progressPhase = el('span', 'progress-phase');
    const track = el('div', 'progress-track');
    this.progressBar = el('i');
    track.append(this.progressBar);
    progress.append(this.progressPhase, track);
    this.progressWrap = progress;
    const actions = el('div', 'viewport-actions');
    const mkBtn = (action, icn, title, label = '') => {
      const b = el('button', label ? 'action-button' : 'icon-button', `${icon(icn, 15)}${label ? `<span>${label}</span>` : ''}`);
      b.title = title; b.dataset.action = action;
      b.addEventListener('click', () => this.hooks.onAction(action));
      actions.append(b);
      return b;
    };
    mkBtn('frame', 'frame', 'Frame patch (F)');
    mkBtn('cave', 'cave', 'Fly to cave mouth (C)');
    this.cameraButton = mkBtn('cameraMode', 'move', 'Toggle orbit / fly (G)', 'Orbit');
    mkBtn('screenshot', 'camera', 'Screenshot');
    const gen = el('button', 'generate', `${icon('play', 12, 2)}<span>Generate</span>`);
    gen.title = 'Regenerate patch (Ctrl+Enter)';
    gen.addEventListener('click', () => this.hooks.onAction('generate'));
    actions.append(gen);
    this.generateButton = gen;
    top.append(this.crumbs, progress, actions);

    const wrap = el('div', 'canvas-wrap');
    this.canvas = document.createElement('canvas');
    this.canvas.id = 'viewport-canvas';
    wrap.append(this.canvas);
    this.hud = el('div', 'viewport-hud');
    this.hint = el('div', 'viewport-hint');
    this.loading = el('div', 'loading-overlay');
    this.loading.innerHTML = `<div class="loading-card"><div class="loading-title">${icon('mountain', 20)} Building SDF patch</div><div class="loading-phase">Starting…</div><div class="loading-track"><i></i></div></div>`;
    this.loadingPhase = this.loading.querySelector('.loading-phase');
    this.loadingBar = this.loading.querySelector('.loading-track i');
    wrap.append(this.hud, this.hint, this.loading);

    this.toolBar = el('div', 'tool-bar');
    for (const t of this.defs.tools) {
      const b = el('button', '', `<span>${t.label}</span>`);
      b.dataset.tool = t.id;
      b.title = t.title ?? t.label;
      b.addEventListener('click', () => this.hooks.onTool(t.id));
      this.toolBar.append(b);
    }
    viewport.append(top, wrap, this.toolBar);

    // ---- inspector
    const inspector = el('section', 'inspector');
    const itop = el('div', 'inspector-top');
    this.inspectorCrumbs = el('div', 'inspector-crumbs');
    this.saveStatus = el('div', 'save-status saved', '<span class="unsaved-dot"></span><span class="save-text">Ready</span>');
    itop.append(this.inspectorCrumbs, this.saveStatus);
    this.content = el('div', 'inspector-content');
    inspector.append(itop, this.content);

    shell.append(outliner, viewport, inspector);
    this.root.append(shell);
    this.toast = el('div', 'toast');
    document.body.append(this.toast);
    this.setCrumbs();
    this.setProgress(null);
  }

  setCrumbs() {
    const g = this.defs.groups[this.selected];
    this.crumbs.innerHTML = `${icon('mountain', 14)} Environment <span class="crumb-slash">/</span> <span class="crumb-current">${g?.title ?? ''}</span>`;
    this.inspectorCrumbs.innerHTML = `${icon(g?.icon ?? 'layers', 14)}<span>${g?.title ?? ''}</span>`;
  }

  renderOutliner() {
    this.tree.innerHTML = '';
    for (const section of this.defs.sections) {
      const group = el('div', 'group');
      group.append(el('div', 'group-label', `${section.label}<span class="group-count">${section.ids.length}</span>`));
      for (const id of section.ids) {
        const g = this.defs.groups[id];
        if (!g) continue;
        const row = el('div', 'tree-row' + (id === this.selected ? ' selected' : ''));
        const btn = el('button', 'object-button',
          `${icon(g.icon, 17)}<span><span class="obj-name">${g.title}${g.badge ? `<em class="phase-badge">${g.badge}</em>` : ''}</span><small>${g.sub}</small></span>${id === this.selected ? '<span class="selected-dot"></span>' : ''}`);
        btn.addEventListener('click', () => this.select(id));
        row.append(btn);
        group.append(row);
      }
      this.tree.append(group);
    }
  }

  select(id) {
    this.selected = id;
    this.renderOutliner();
    this.renderInspector();
    this.setCrumbs();
    this.hooks.onSelect?.(id);
  }

  renderInspector() {
    const g = this.defs.groups[this.selected];
    this.controls.clear();
    this.stats.clear();
    this.content.innerHTML = '';
    if (!g) return;
    const header = el('div', 'object-header');
    header.innerHTML = `
      <div class="object-title">
        <div class="object-icon">${icon(g.icon, 27, 1.4)}</div>
        <div><div class="eyebrow">${g.eyebrow ?? 'Environment'}</div><h1>${g.title}</h1></div>
      </div>
      ${g.badge ? `<span class="enabled-pill disabled"><span></span>${g.badge}</span>` : '<span class="enabled-pill"><span></span>Active</span>'}`;
    this.content.append(header);
    if (g.intro) this.content.append(el('p', 'intro', g.intro));

    for (const card of g.cards) {
      const cardEl = el('section', 'card');
      const heading = el('div', 'card-heading', `<span>${icon(card.icon ?? g.icon, 15)}${card.title}</span>${card.meta ? `<span class="card-meta">${card.meta}</span>` : ''}`);
      cardEl.append(heading);
      const body = el('div', 'card-body');
      for (const ctrl of card.controls) body.append(this.buildControl(ctrl));
      if (card.note) body.append(el('p', 'muted', card.note));
      cardEl.append(body);
      this.content.append(cardEl);
    }
    this.content.append(el('div', 'inspector-footer',
      '<span><span class="footer-dot"></span>Frontier Environment Editor</span><span>SDF volume<span class="footer-slash">/</span>WebGPU</span>'));
    this.hooks.onInspectorRendered?.(this.selected);
  }

  buildControl(ctrl) {
    const H = this.hooks;
    switch (ctrl.type) {
      case 'slider': {
        const wrap = el('div', 'ctl');
        const head = el('div', 'ctl-head');
        const out = el('output', '', fmtVal(ctrl, H.getValue(ctrl.key)));
        head.append(el('label', '', ctrl.label), out);
        const input = document.createElement('input');
        input.type = 'range'; input.min = ctrl.min; input.max = ctrl.max; input.step = ctrl.step ?? 'any';
        input.value = H.getValue(ctrl.key);
        const paint = () => {
          const p = ((Number(input.value) - ctrl.min) / (ctrl.max - ctrl.min)) * 100;
          input.style.setProperty('--progress', `${p}%`);
          out.textContent = fmtVal(ctrl, Number(input.value));
        };
        input.addEventListener('input', () => { paint(); H.setValue(ctrl.key, Number(input.value), ctrl, false); });
        input.addEventListener('change', () => H.setValue(ctrl.key, Number(input.value), ctrl, true));
        paint();
        wrap.append(head, input);
        if (ctrl.minLabel || ctrl.maxLabel) wrap.append(el('div', 'range-labels', `<span>${ctrl.minLabel ?? ''}</span><span>${ctrl.maxLabel ?? ''}</span>`));
        this.controls.set(ctrl.key, { ctrl, input, output: out, sync: () => { input.value = H.getValue(ctrl.key); paint(); } });
        return wrap;
      }
      case 'toggle': {
        const row = el('div', 'control-line');
        row.append(el('span', '', ctrl.label));
        const btn = el('button', 'toggle' + (H.getValue(ctrl.key) ? ' on' : ''), '<span></span>');
        btn.setAttribute('role', 'switch');
        btn.addEventListener('click', () => {
          const v = H.getValue(ctrl.key) ? 0 : 1;
          btn.classList.toggle('on', !!v);
          H.setValue(ctrl.key, v, ctrl, true);
        });
        row.append(btn);
        this.controls.set(ctrl.key, { ctrl, sync: () => btn.classList.toggle('on', !!H.getValue(ctrl.key)) });
        return row;
      }
      case 'select': {
        const wrap = el('div', 'ctl');
        const head = el('div', 'ctl-head'); head.append(el('label', '', ctrl.label));
        const sel = document.createElement('select');
        sel.className = 'select';
        for (const [value, label] of ctrl.options) {
          const o = document.createElement('option'); o.value = value; o.textContent = label; sel.append(o);
        }
        sel.value = H.getValue(ctrl.key);
        sel.addEventListener('change', () => H.setValue(ctrl.key, sel.value, ctrl, true));
        wrap.append(head, sel);
        this.controls.set(ctrl.key, { ctrl, input: sel, sync: () => { sel.value = H.getValue(ctrl.key); } });
        return wrap;
      }
      case 'segmented': {
        const wrap = el('div', 'ctl');
        if (ctrl.label) wrap.append(el('div', 'ctl-head', `<label>${ctrl.label}</label>`));
        const seg = el('div', 'seg');
        const buttons = new Map();
        for (const [value, label] of ctrl.options) {
          const b = el('button', String(H.getValue(ctrl.key)) === String(value) ? 'on' : '', label);
          b.addEventListener('click', () => { H.setValue(ctrl.key, value, ctrl, true); syncSeg(); });
          buttons.set(String(value), b);
          seg.append(b);
        }
        const syncSeg = () => { for (const [v, b] of buttons) b.classList.toggle('on', v === String(H.getValue(ctrl.key))); };
        wrap.append(seg);
        this.controls.set(ctrl.key, { ctrl, sync: syncSeg });
        return wrap;
      }
      case 'button': {
        const b = el('button', 'btn' + (ctrl.primary ? ' primary' : '') + (ctrl.wide ? ' wide' : ''), ctrl.label);
        b.addEventListener('click', () => H.onAction(ctrl.action));
        const wrap = el('div', 'btn-row'); wrap.append(b);
        return wrap;
      }
      case 'buttons': {
        const wrap = el('div', 'btn-row wrap');
        for (const bdef of ctrl.buttons) {
          const b = el('button', 'btn' + (bdef.primary ? ' primary' : ''), bdef.label);
          b.addEventListener('click', () => H.onAction(bdef.action));
          wrap.append(b);
        }
        return wrap;
      }
      case 'stat': {
        const wrap = el('div', 'stat');
        wrap.append(el('div', 'stat-k', ctrl.label));
        const val = el('div', 'stat-v', ctrl.value ?? '—');
        wrap.append(val);
        this.stats.set(ctrl.id, val);
        return wrap;
      }
      case 'stats': {
        const grid = el('div', 'stat-grid');
        for (const s of ctrl.stats) {
          const cell = el('div', 'stat');
          cell.append(el('div', 'stat-k', s.label));
          const val = el('div', 'stat-v', s.value ?? '—');
          cell.append(val);
          this.stats.set(s.id, val);
          grid.append(cell);
        }
        return grid;
      }
      case 'html':
        return el('div', 'rich', ctrl.html);
      default:
        return el('div', '', '');
    }
  }

  syncControl(key) { this.controls.get(key)?.sync?.(); }
  syncAllControls() { for (const c of this.controls.values()) c.sync?.(); }
  setStat(id, value) { const e = this.stats.get(id); if (e) e.textContent = value; }

  setTool(id) {
    this.tool = id;
    this.toolBar.querySelectorAll('[data-tool]').forEach((b) => b.classList.toggle('on', b.dataset.tool === id));
  }
  setCameraMode(mode) {
    if (this.cameraButton) this.cameraButton.querySelector('span').textContent = mode === 'fly' ? 'Fly' : 'Orbit';
  }
  setBusy(busy, label = 'Working…') {
    this.generateButton.disabled = busy;
    this.generateButton.querySelector('span').textContent = busy ? label : 'Generate';
  }
  setSaveStatus(text, saved = true) {
    this.saveStatus.querySelector('.save-text').textContent = text;
    this.saveStatus.classList.toggle('saved', saved);
  }
  setProgress(phase, fraction = 0) {
    const active = phase != null;
    this.progressWrap.classList.toggle('active', active);
    if (active) {
      this.progressPhase.textContent = phase;
      this.progressBar.style.width = `${Math.round(Math.min(1, Math.max(0, fraction)) * 100)}%`;
    }
  }
  showLoading(show, phase = '', fraction = 0) {
    this.loading.classList.toggle('show', show);
    if (show) {
      if (phase) this.loadingPhase.textContent = phase;
      this.loadingBar.style.width = `${Math.round(Math.min(1, Math.max(0, fraction)) * 100)}%`;
    }
  }
  setHud(html) { this.hud.innerHTML = html; }
  setHint(text) { this.hint.textContent = text; }
  showToast(message, ms = 2600) {
    this.toast.textContent = message;
    this.toast.classList.add('show');
    clearTimeout(this._toastTimer);
    this._toastTimer = setTimeout(() => this.toast.classList.remove('show'), ms);
  }
}

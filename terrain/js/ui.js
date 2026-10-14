// Inspector UI: schema-driven controls + layer stacks. No framework — small DOM helpers.
import { HEIGHT_TYPES, VOLUME_TYPES, MATERIAL_TYPES, MATERIAL_PARAMS, HYBRID_PARAMS, WORLD_SECTIONS, BLEND_MODES, MASK_TYPES,
  makeHeightLayer, makeVolumeLayer, makeMaterial, uid } from './schema.js';

export function h(tag, attrs = {}, ...kids) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === undefined || v === null || v === false) continue;
    if (k === 'class') el.className = v;
    else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
    else if (k.startsWith('on')) el.addEventListener(k.slice(2).toLowerCase(), v);
    else if (k === 'html') el.innerHTML = v;
    else el.setAttribute(k, v === true ? '' : v);
  }
  for (const c of kids.flat(Infinity)) if (c !== null && c !== undefined && c !== false) el.append(c.nodeType ? c : document.createTextNode(String(c)));
  return el;
}
const ICON = {
  plus: '<svg viewBox="0 0 16 16"><path d="M8 3v10M3 8h10" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>',
  dup: '<svg viewBox="0 0 16 16"><rect x="5" y="5" width="8" height="8" rx="2" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M3 10.5V4a1 1 0 0 1 1-1h6.5" fill="none" stroke="currentColor" stroke-width="1.4"/></svg>',
  del: '<svg viewBox="0 0 16 16"><path d="M4 4l8 8M12 4l-8 8" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/></svg>',
  grip: '<svg viewBox="0 0 16 16"><g fill="currentColor"><circle cx="6" cy="4" r="1.2"/><circle cx="10" cy="4" r="1.2"/><circle cx="6" cy="8" r="1.2"/><circle cx="10" cy="8" r="1.2"/><circle cx="6" cy="12" r="1.2"/><circle cx="10" cy="12" r="1.2"/></g></svg>',
  dice: '<svg viewBox="0 0 16 16"><rect x="2.5" y="2.5" width="11" height="11" rx="3" fill="none" stroke="currentColor" stroke-width="1.4"/><g fill="currentColor"><circle cx="5.7" cy="5.7" r="1.1"/><circle cx="10.3" cy="10.3" r="1.1"/><circle cx="8" cy="8" r="1.1"/></g></svg>',
  chev: '<svg viewBox="0 0 16 16"><path d="M5 6l3 3 3-3" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/></svg>',
};
const icon = (n) => h('span', { class: 'ic', html: ICON[n] });

// ------------------------------------------------------------------ value formatting / mapping
const decimals = (p) => (p.int ? 0 : p.step ? Math.max(0, Math.min(4, Math.ceil(-Math.log10(p.step) - 1e-9))) : (p.max - p.min) > 50 ? 0 : 2);
function fmt(p, v, ctx) {
  if (p.fmtHs && ctx?.Hs) return `${(v * ctx.Hs).toFixed(0)} m`;
  const d = decimals(p);
  let s = Number(v).toFixed(d);
  if (p.bipolar && v > 0) s = '+' + s;
  return p.unit ? `${s}${p.unit === '°' ? '°' : ' ' + p.unit}` : s;
}
const toT = (p, v) => (p.log ? Math.log(v / p.min) / Math.log(p.max / p.min) : (v - p.min) / (p.max - p.min));
const fromT = (p, t) => {
  t = Math.min(1, Math.max(0, t));
  let v = p.log ? p.min * Math.pow(p.max / p.min, t) : p.min + t * (p.max - p.min);
  if (p.int) v = Math.round(v);
  else if (p.step) v = Math.round(v / p.step) * p.step;
  else if (p.log) v = Number(v.toPrecision(3));
  return +v.toFixed(6);
};

// ------------------------------------------------------------------ controls
// cb(value, {dragging, commit})
export function rangeCtl(p, value, cb, ctx) {
  const fill = h('div', { class: 'fill' });
  const val = h('span', { class: 'val' });
  const el = h('div', { class: 'scrub' + (p.bipolar ? ' bipolar' : ''), title: p.hint || '' }, fill, h('span', { class: 'lbl' }, p.label), val);
  let v = value;
  const paint = () => {
    const t = Math.min(1, Math.max(0, toT(p, v)));
    if (p.bipolar) {
      const c = toT(p, 0);
      fill.style.left = `${Math.min(c, t) * 100}%`; fill.style.width = `${Math.abs(t - c) * 100}%`;
    } else { fill.style.left = '0'; fill.style.width = `${t * 100}%`; }
    val.textContent = fmt(p, v, ctx);
  };
  paint();
  let drag = null;
  el.addEventListener('pointerdown', (e) => {
    if (e.button !== 0 || el.classList.contains('editing')) return;
    el.setPointerCapture(e.pointerId);
    const r = el.getBoundingClientRect();
    drag = { x0: e.clientX, t0: toT(p, v), w: r.width, moved: false, left: r.left };
    el.classList.add('active');
  });
  el.addEventListener('pointermove', (e) => {
    if (!drag) return;
    const dx = e.clientX - drag.x0;
    if (!drag.moved && Math.abs(dx) < 3) return;
    if (!drag.moved) { drag.moved = true; cb(v, { begin: true }); }
    const fine = e.shiftKey ? 0.2 : 1;
    const nv = fromT(p, drag.t0 + (dx / drag.w) * fine);
    if (nv !== v) { v = nv; paint(); cb(v, { dragging: true }); }
  });
  const up = (e) => {
    if (!drag) return;
    el.classList.remove('active');
    if (!drag.moved) {
      // click: jump to position
      const t = (e.clientX - drag.left) / drag.w;
      v = fromT(p, t); paint(); cb(v, { commit: true });
    } else cb(v, { commit: true, end: true });
    drag = null;
  };
  el.addEventListener('pointerup', up);
  el.addEventListener('pointercancel', up);
  el.addEventListener('dblclick', () => {
    el.classList.add('editing');
    const inp = h('input', { class: 'scrub-edit', value: String(+Number(v).toFixed(4)) });
    el.append(inp); inp.focus(); inp.select();
    const done = (ok) => {
      if (ok) {
        let nv = parseFloat(inp.value);
        if (Number.isFinite(nv)) { if (p.int) nv = Math.round(nv); v = nv; paint(); cb(v, { commit: true }); }
      }
      inp.remove(); el.classList.remove('editing');
    };
    inp.addEventListener('keydown', (e) => { if (e.key === 'Enter') done(true); if (e.key === 'Escape') done(false); e.stopPropagation(); });
    inp.addEventListener('blur', () => done(true));
  });
  el.set = (nv) => { v = nv; paint(); };
  return el;
}

export function segCtl(options, value, cb, { small = false, label } = {}) {
  const wrap = h('div', { class: 'seg' + (small ? ' sm' : '') });
  const btns = options.map(([lab, val]) => {
    const b = h('button', { class: val === value ? 'active' : '', onclick: () => { btns.forEach((x) => x.classList.remove('active')); b.classList.add('active'); cb(val, { commit: true }); } }, lab);
    return b;
  });
  wrap.append(...btns);
  return label ? h('div', { class: 'row' }, h('span', { class: 'rlbl' }, label), wrap) : wrap;
}

export function switchCtl(value, cb) {
  const s = h('button', { class: 'switch', 'data-on': String(!!value), 'aria-pressed': String(!!value) }, h('i'));
  s.addEventListener('click', (e) => {
    e.stopPropagation();
    const on = s.getAttribute('data-on') !== 'true';
    s.setAttribute('data-on', String(on)); s.setAttribute('aria-pressed', String(on));
    cb(on, { commit: true });
  });
  return s;
}
const boolRow = (p, v, cb) => h('div', { class: 'row', title: p.hint || '' }, h('span', { class: 'rlbl' }, p.label), switchCtl(v, cb));

function colorRow(p, v, cb) {
  const inp = h('input', { type: 'color', value: v });
  const sw = h('label', { class: 'swatch', style: { background: v } }, inp);
  const hex = h('span', { class: 'hex' }, v.toUpperCase());
  inp.addEventListener('input', () => { sw.style.background = inp.value; hex.textContent = inp.value.toUpperCase(); cb(inp.value, { dragging: true }); });
  inp.addEventListener('change', () => cb(inp.value, { commit: true }));
  return h('div', { class: 'row' }, h('span', { class: 'rlbl' }, p.label), h('div', { class: 'colorf' }, hex, sw));
}

function seedRow(p, v, cb) {
  const num = h('span', { class: 'seedv' }, String(v));
  const set = (nv) => { num.textContent = String(nv); cb(nv, { commit: true }); };
  return h('div', { class: 'row' }, h('span', { class: 'rlbl' }, p.label),
    h('div', { class: 'seedf' },
      h('button', { class: 'mini', onclick: () => set(Math.max(0, +num.textContent - 1)) }, '‹'),
      num,
      h('button', { class: 'mini', onclick: () => set(+num.textContent + 1) }, '›'),
      h('button', { class: 'mini dice', title: 'Randomize', onclick: () => set(Math.floor(Math.random() * 9999)), html: ICON.dice })));
}

export function bandCtl(p, value, cb, ctx) {
  let [lo, hi] = value;
  const track = h('div', { class: 'band-track' });
  const sel = h('div', { class: 'band-sel' });
  const tl = h('div', { class: 'thumb' }), th = h('div', { class: 'thumb' });
  const val = h('span', { class: 'val' });
  track.append(sel, tl, th);
  const el = h('div', { class: 'band', title: p.hint || '' }, h('div', { class: 'band-head' }, h('span', { class: 'lbl' }, p.label), val), track);
  const paint = () => {
    const a = toT(p, lo) * 100, b = toT(p, hi) * 100;
    sel.style.left = `${a}%`; sel.style.width = `${b - a}%`;
    tl.style.left = `${a}%`; th.style.left = `${b}%`;
    const f = (x) => fmt({ ...p, unit: p.unit }, x, ctx);
    val.textContent = lo <= p.min && hi >= p.max ? 'Any' : `${f(lo)} – ${f(hi)}`;
  };
  paint();
  let drag = null;
  track.addEventListener('pointerdown', (e) => {
    track.setPointerCapture(e.pointerId);
    const r = track.getBoundingClientRect();
    const t = (e.clientX - r.left) / r.width;
    const which = Math.abs(t - toT(p, lo)) < Math.abs(t - toT(p, hi)) ? 0 : (Math.abs(t - toT(p, lo)) === Math.abs(t - toT(p, hi)) && t < toT(p, lo) ? 0 : 1);
    drag = { r, which };
    cb([lo, hi], { begin: true });
    move(e);
  });
  const move = (e) => {
    if (!drag) return;
    const v = fromT(p, (e.clientX - drag.r.left) / drag.r.width);
    if (drag.which === 0) lo = Math.min(v, hi); else hi = Math.max(v, lo);
    paint(); cb([lo, hi], { dragging: true });
  };
  track.addEventListener('pointermove', move);
  const up = () => { if (drag) { drag = null; cb([lo, hi], { commit: true, end: true }); } };
  track.addEventListener('pointerup', up); track.addEventListener('pointercancel', up);
  return el;
}

// Build a control for a schema param bound to obj[p.k]
export function control(p, obj, onChange, ctx) {
  const cb = (v, info) => { obj[p.k] = v; onChange(p, v, info); };
  switch (p.type) {
    case 'range': return rangeCtl(p, obj[p.k], cb, ctx);
    case 'seg': return segCtl(p.options, obj[p.k], cb, { label: p.label });
    case 'bool': return boolRow(p, obj[p.k], cb);
    case 'color': return colorRow(p, obj[p.k], cb);
    case 'seed': return seedRow(p, obj[p.k], cb);
    case 'band': return bandCtl(p, obj[p.k], cb, ctx);
  }
  return h('div');
}
export function paramList(params, obj, onChange, ctx) {
  const main = [], adv = [];
  let section = null;
  const out = [];
  for (const p of params) {
    if (p.section) { section = h('div', { class: 'sub' }, p.section); out.push(section); continue; }
    const c = control(p, obj, onChange, ctx);
    (p.adv ? adv : main).push(c);
    if (!p.adv) out.push(c);
  }
  if (adv.length) {
    const body = h('div', { class: 'adv-body' }, adv);
    const det = h('div', { class: 'adv' }, h('button', { class: 'adv-toggle', onclick: () => det.classList.toggle('open') }, 'Advanced', icon('chev')), body);
    out.push(det);
  }
  return out;
}

// ------------------------------------------------------------------ cards
export const card = (title, right, ...body) => h('section', { class: 'card' }, h('header', {}, h('h3', {}, title), right || null), h('div', { class: 'card-body' }, body));

// ------------------------------------------------------------------ layer stack
// items: array (index 0 = bottom). Displayed top-first.
export function layerStack({ items, selected, typeOf, badge, onSelect, onToggle, onReorder, onDelete, onDuplicate, onRename }) {
  const list = h('ol', { class: 'stack' });
  const n = items.length;
  for (let vi = 0; vi < n; vi++) {
    const i = n - 1 - vi;
    const it = items[i];
    const T = typeOf(it);
    const name = h('span', { class: 'lname' }, it.name);
    const row = h('li', { class: 'layer' + (it.id === selected ? ' sel' : '') + (it.enabled ? '' : ' off'), 'data-i': i, draggable: 'true' },
      h('span', { class: 'grip', html: ICON.grip }),
      h('span', { class: 'glyph' }, T.glyph || '•'),
      h('div', { class: 'lmeta' }, name, h('span', { class: 'ltype' }, T.label)),
      badge ? badge(it) : null,
      h('div', { class: 'lact' },
        h('button', { class: 'mini', title: 'Duplicate', html: ICON.dup, onclick: (e) => { e.stopPropagation(); onDuplicate(i); } }),
        h('button', { class: 'mini', title: 'Delete', html: ICON.del, onclick: (e) => { e.stopPropagation(); onDelete(i); } })),
      switchCtl(it.enabled, (on) => onToggle(i, on)));
    row.addEventListener('click', () => onSelect(it.id));
    name.addEventListener('dblclick', (e) => {
      e.stopPropagation();
      const inp = h('input', { class: 'rename', value: it.name });
      name.replaceWith(inp); inp.focus(); inp.select();
      const done = () => { onRename(i, inp.value.trim() || it.name); };
      inp.addEventListener('keydown', (ev) => { if (ev.key === 'Enter') inp.blur(); ev.stopPropagation(); });
      inp.addEventListener('blur', done);
    });
    row.addEventListener('dragstart', (e) => { e.dataTransfer.setData('text/plain', String(i)); row.classList.add('dragging'); });
    row.addEventListener('dragend', () => row.classList.remove('dragging'));
    row.addEventListener('dragover', (e) => {
      e.preventDefault();
      const r = row.getBoundingClientRect();
      row.classList.toggle('drop-above', e.clientY < r.top + r.height / 2);
      row.classList.toggle('drop-below', e.clientY >= r.top + r.height / 2);
    });
    row.addEventListener('dragleave', () => row.classList.remove('drop-above', 'drop-below'));
    row.addEventListener('drop', (e) => {
      e.preventDefault();
      const from = +e.dataTransfer.getData('text/plain');
      const above = row.classList.contains('drop-above');
      row.classList.remove('drop-above', 'drop-below');
      // display is reversed: "above" in the list == higher index
      let to = above ? i + 1 : i;
      if (from < to) to -= 1;
      if (from !== to) onReorder(from, to);
    });
    list.append(row);
  }
  if (!n) list.append(h('li', { class: 'empty' }, 'No layers yet'));
  return list;
}

export function addMenu(types, onPick) {
  const groups = {};
  for (const [k, T] of Object.entries(types)) (groups[T.group || 'Add'] ||= []).push([k, T]);
  const menu = h('div', { class: 'menu' });
  for (const [g, arr] of Object.entries(groups)) {
    menu.append(h('div', { class: 'menu-g' }, g));
    for (const [k, T] of arr) menu.append(h('button', { class: 'menu-i', onclick: () => { close(); onPick(k); } }, h('span', { class: 'glyph sm' }, T.glyph), T.label));
  }
  const btn = h('button', { class: 'hbtn' }, h('span', { class: 'hbtn-ic', html: ICON.plus }), 'Add');
  const wrap = h('div', { class: 'addwrap' }, btn, menu);
  const close = () => { wrap.classList.remove('open'); document.removeEventListener('pointerdown', outside, true); };
  const outside = (e) => { if (!wrap.contains(e.target)) close(); };
  btn.addEventListener('click', () => {
    if (wrap.classList.toggle('open')) document.addEventListener('pointerdown', outside, true); else close();
  });
  return wrap;
}

// ------------------------------------------------------------------ inspector
export class Inspector {
  constructor(root, app) {
    this.root = root; this.app = app;
    this.tab = 'terrain';
    this.sel = { layers: null, volume: null, materials: null };
  }
  get st() { return this.app.state; }

  render() {
    const st = this.st;
    const keepScroll = this.scroller ? this.scroller.scrollTop : 0;
    this.root.innerHTML = '';
    const tabs = segCtl([['Terrain', 'terrain'], ['Volume', 'volume'], ['Surface', 'materials'], ['World', 'world']], this.tab, (v) => { this.tab = v; this.render(); });
    tabs.classList.add('tabs');
    this.scroller = h('div', { class: 'scroll' });
    const body = this.scroller;
    if (this.tab === 'terrain') this._stackTab(body, 'layers', HEIGHT_TYPES, 'Height layers', 'Generators, filters and erosion run bottom → top on one heightfield. Erosion also shapes the voxel regions.');
    if (this.tab === 'volume') {
      body.append(card('Hybrid representation', h('span', { class: 'pill-info' }, this._hybridInfo()),
        paramList(HYBRID_PARAMS, st.hybrid, (p, v, info) => this.app.changed(`hybrid.${p.k}`, info))));
      this._stackTab(body, 'volume', VOLUME_TYPES, 'Volumetric detail', 'Applied only where the heightfield stretches (steep faces, cliffs, overhangs) — sampled from the same eroded maps.');
    }
    if (this.tab === 'materials') this._stackTab(body, 'materials', MATERIAL_TYPES, 'Surface layers', 'Textures stack bottom → top, driven by height, slope and the analysis maps.');
    if (this.tab === 'world') {
      for (const sec of WORLD_SECTIONS) {
        body.append(card(sec.title, null, paramList(sec.params, st[sec.key], (p, v, info) => this.app.changed(`${sec.key}.${p.k}`, info), { Hs: st.world.heightScale })));
      }
      body.append(card('Export', null, this.app.exportPanel()));
    }
    this.root.append(tabs, body);
    body.scrollTop = keepScroll;
  }

  _hybridInfo() {
    const s = this.app.engine?.stats;
    if (!s || this.st.hybrid.mode === 0) return 'Heightmap only';
    return `${s.bricks} bricks · ${(s.voxTris / 1000).toFixed(0)}k tris`;
  }

  _stackTab(body, key, TYPES, title, hint) {
    const st = this.st, items = st[key];
    if (!items.find((x) => x.id === this.sel[key])) this.sel[key] = items.length ? items[items.length - 1].id : null;
    const typeOf = (it) => TYPES[it.type];
    const mk = key === 'layers' ? makeHeightLayer : key === 'volume' ? makeVolumeLayer : makeMaterial;
    const commit = (lvl) => { this.app.changed(`${key}`, { commit: true, structural: true }); this.render(); };
    const badge = key === 'layers' ? (it) => {
      const T = HEIGHT_TYPES[it.type];
      if (T.filter) return it.mask.type ? h('span', { class: 'badge' }, 'mask') : null;
      return h('span', { class: 'badge' }, BLEND_MODES.find((b) => b[1] === it.blend)?.[0] || '');
    } : key === 'volume' ? (it) => (VOLUME_TYPES[it.type].prim ? h('span', { class: 'badge blue' }, 'SDF') : null) : (it) => h('span', { class: 'chip', style: { background: it.p.color } });
    const stack = layerStack({
      items, selected: this.sel[key], typeOf, badge,
      onSelect: (id) => { this.sel[key] = id; this.render(); },
      onToggle: (i, on) => { items[i].enabled = on; this.app.changed(key, { commit: true, structural: true, item: items[i] }); this.render(); },
      onReorder: (from, to) => { const [x] = items.splice(from, 1); items.splice(to, 0, x); commit(); },
      onDelete: (i) => { items.splice(i, 1); commit(); },
      onDuplicate: (i) => { const c = JSON.parse(JSON.stringify(items[i])); c.id = uid(); c.name += ' copy'; items.splice(i + 1, 0, c); this.sel[key] = c.id; commit(); },
      onRename: (i, name) => { items[i].name = name; this.app.changed(key, { commit: true, level: 0 }); this.render(); },
    });
    const add = addMenu(TYPES, (type) => {
      const it = mk(type);
      const si = items.findIndex((x) => x.id === this.sel[key]);
      items.splice(si >= 0 ? si + 1 : items.length, 0, it);
      this.sel[key] = it.id; commit();
    });
    body.append(card(title, add, h('p', { class: 'hint' }, hint), stack));
    const it = items.find((x) => x.id === this.sel[key]);
    if (it) body.append(this._props(key, it));
  }

  _props(key, it) {
    const ch = (path) => (p, v, info) => this.app.changed(path, { ...info, item: it, param: p.k });
    const kids = [];
    const ctx = { Hs: this.st.world.heightScale };
    if (key === 'layers') {
      const T = HEIGHT_TYPES[it.type];
      if (!T.filter) kids.push(segCtl(BLEND_MODES.slice(0, 6), it.blend, (v) => { it.blend = v; this.app.changed('layers', { commit: true, item: it }); this.render(); }, { label: 'Blend', small: true }));
      kids.push(rangeCtl({ label: 'Opacity', min: 0, max: 1, step: 0.01 }, it.opacity, (v, info) => { it.opacity = v; this.app.changed('layers', { ...info, item: it }); }));
      kids.push(...paramList(T.params, it.p, ch('layers'), ctx));
      // mask
      const m = it.mask;
      const mk = [segCtl(MASK_TYPES, m.type, (v) => {
        m.type = v;
        if (v === 2) Object.assign(m, { lo: 30, hi: 90, soft: 6 }); else if (v === 1) Object.assign(m, { lo: 0.3, hi: 1.5, soft: 0.05 }); else if (v === 3) Object.assign(m, { lo: 0.5, hi: 1.5, soft: 0.1 });
        this.app.changed('layers', { commit: true, item: it }); this.render();
      }, { small: true, label: 'Mask' })];
      if (m.type) {
        const slope = m.type === 2;
        const bp = slope ? { label: 'Range', min: 0, max: 90, unit: '°', step: 0.5 } : { label: 'Range', min: -0.5, max: 1.6, step: 0.005 };
        mk.push(bandCtl(bp, [m.lo, m.hi], (v, info) => { m.lo = v[0]; m.hi = v[1]; this.app.changed('layers', { ...info, item: it }); }, ctx));
        mk.push(rangeCtl(slope ? { label: 'Softness', min: 0.5, max: 30, unit: '°', step: 0.5 } : { label: 'Softness', min: 0.001, max: 0.4, step: 0.001 }, m.soft, (v, info) => { m.soft = v; this.app.changed('layers', { ...info, item: it }); }));
        if (m.type === 3) {
          mk.push(rangeCtl({ label: 'Noise scale', min: 20, max: 6000, log: true, unit: 'm' }, m.scale, (v, info) => { m.scale = v; this.app.changed('layers', { ...info, item: it }); }));
          mk.push(seedRow({ label: 'Mask seed' }, m.seed, (v) => { m.seed = v; this.app.changed('layers', { commit: true, item: it }); }));
        }
        mk.push(boolRow({ label: 'Invert mask' }, m.invert, (v) => { m.invert = v; this.app.changed('layers', { commit: true, item: it }); }));
      }
      kids.push(h('div', { class: 'sub' }, 'Mask'), ...mk);
    } else if (key === 'volume') {
      kids.push(...paramList(VOLUME_TYPES[it.type].params, it.p, ch('volume'), ctx));
    } else {
      kids.push(...paramList(MATERIAL_PARAMS, it.p, ch('materials'), ctx));
    }
    return card(it.name, h('span', { class: 'ptype' }, (key === 'layers' ? HEIGHT_TYPES : key === 'volume' ? VOLUME_TYPES : MATERIAL_TYPES)[it.type].label), kids);
  }
}

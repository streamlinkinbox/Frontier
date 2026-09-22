/* ============================================================
   Frontier Engine — Widget Group B
   (Nature · Lights · Camera · Actors · Post)
   ============================================================ */

function polar(cx, cy, r, deg) { const a = (deg - 90) * Math.PI / 180; return [cx + r * Math.cos(a), cy + r * Math.sin(a)]; }
function arcPath(cx, cy, r, a0, a1) {
  const p0 = polar(cx, cy, r, a0), p1 = polar(cx, cy, r, a1);
  const large = Math.abs(a1 - a0) > 180 ? 1 : 0, sweep = a1 > a0 ? 1 : 0;
  return 'M ' + p0[0].toFixed(2) + ' ' + p0[1].toFixed(2) + ' A ' + r + ' ' + r + ' 0 ' + large + ' ' + sweep + ' ' + p1[0].toFixed(2) + ' ' + p1[1].toFixed(2);
}

/* ============================================================
   DIAL KNOB — 270° rotary encoder
   ============================================================ */
WIDGETS.dialKnob = function (api) {
  const cfg = api.cfg, key = cfg.key;
  let v = getPath(key, cfg.value);
  const hero = api.hero({ num: fmt(v, cfg.step >= 1 ? 0 : 2), unit: cfg.unit, badge: 'ROTARY', sub: cfg.sub });
  const wrap = h('div', 'dial');
  wrap.innerHTML =
    '<svg viewBox="0 0 128 128" class="dial__svg">' +
      '<path class="dial__tr" d="' + arcPath(64, 64, 50, -135, 135) + '"/>' +
      '<path class="dial__vl" d="' + arcPath(64, 64, 50, -135, -135) + '"/>' +
      '<g class="dial__ticks"></g>' +
      '<circle class="dial__hub" cx="64" cy="64" r="33"/>' +
      '<line class="dial__ptr" x1="64" y1="28" x2="64" y2="18"/>' +
    '</svg>' +
    '<div class="dial__mid"><b></b><i></i></div>';
  api.body.appendChild(wrap);

  const ticks = wrap.querySelector('.dial__ticks');
  let tickSvg = '';
  for (let i = 0; i <= 20; i++) {
    const a = -135 + (270 * i / 20);
    const p0 = polar(64, 64, 58, a), p1 = polar(64, 64, i % 5 === 0 ? 63 : 61, a);
    tickSvg += '<line x1="' + p0[0].toFixed(1) + '" y1="' + p0[1].toFixed(1) + '" x2="' + p1[0].toFixed(1) + '" y2="' + p1[1].toFixed(1) + '" class="' + (i % 5 === 0 ? 'is-major' : '') + '"/>';
  }
  ticks.innerHTML = tickSvg;

  const vl = wrap.querySelector('.dial__vl'), ptr = wrap.querySelector('.dial__ptr');
  const midB = wrap.querySelector('.dial__mid b'), midI = wrap.querySelector('.dial__mid i');

  function paint() {
    const t = inv(v, cfg.min, cfg.max);
    vl.setAttribute('d', arcPath(64, 64, 50, -135, -135 + 270 * t));
    const a = -135 + 270 * t;
    const p0 = polar(64, 64, 37, a), p1 = polar(64, 64, 46, a);
    ptr.setAttribute('x1', p0[0].toFixed(2)); ptr.setAttribute('y1', p0[1].toFixed(2));
    ptr.setAttribute('x2', p1[0].toFixed(2)); ptr.setAttribute('y2', p1[1].toFixed(2));
    midB.textContent = fmt(v, cfg.step >= 1 ? 0 : 2);
    midI.textContent = cfg.unit || '';
    hero.set({ num: fmt(v, cfg.step >= 1 ? 0 : 2), unit: cfg.unit, badge: (t * 100).toFixed(0) + '% RANGE', sub: cfg.sub });
  }

  bindDrag(wrap, {
    onMove(p) {
      const r = p.rect;
      const dx = p.px - r.width / 2, dy = p.py - r.height / 2;
      let a = Math.atan2(dx, -dy) * 180 / Math.PI;         /* -180..180, 0 = up */
      if (a > 180 - 45) a = 135; if (a < -180 + 45) a = -135;
      const t = clamp((clamp(a, -135, 135) + 135) / 270, 0, 1);
      const nv = lerp(cfg.min, cfg.max, t);
      v = Math.round(nv / cfg.step) * cfg.step;
      setPath(key, v); paint();
    },
    onEnd: () => tick()
  });

  const s = slider(api.body, {
    label: cfg.label, min: cfg.min, max: cfg.max, step: cfg.step, value: v, unit: cfg.unit,
    dp: cfg.step >= 1 ? 0 : 2,
    onChange: nv => { v = nv; setPath(key, v); paint(); }
  });
  paint();
};

/* ============================================================
   WIND PAD — 2D vector field with animated streamlines
   ============================================================ */
WIDGETS.windPad = function (api) {
  const cfg = api.cfg;
  const st = ns0(cfg.key.split('.')[0]);
  const kk = cfg.key.split('.')[1];
  if (st[kk] == null) st[kk] = { x: cfg.ax, y: cfg.ay };
  const vec = st[kk];

  const speed = () => Math.hypot(vec.x, vec.y) * cfg.speedScale;
  const dir = () => (Math.atan2(vec.x, -vec.y) * 180 / Math.PI + 360) % 360;
  const hero = api.hero({ num: fmt(speed(), 1), unit: cfg.speedUnit, badge: cfg.readout.toUpperCase(),
                          sub: 'Bearing ' + fmt(dir(), 0) + '° · drag the vector head' });

  const plate = h('div', 'plate plate--pad');
  api.body.appendChild(plate);
  const cv = mkCanvas(plate, 196);
  const lines = Array.from({ length: 26 }, () => ({ s: Math.random(), o: Math.random(), l: rnd(0.08, 0.26) }));
  let t = 0;

  function draw(dt) {
    t += dt;
    const { ctx } = cv, w = cv.w(), hh = cv.h();
    ctx.clearRect(0, 0, w, hh);
    const cx = w / 2, cy = hh / 2, R = Math.min(w, hh) / 2 - 16;

    /* grid */
    ctx.strokeStyle = 'rgba(255,255,255,0.05)';
    for (let i = -3; i <= 3; i++) {
      ctx.beginPath(); ctx.moveTo(cx + i * R / 3, cy - R); ctx.lineTo(cx + i * R / 3, cy + R); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(cx - R, cy + i * R / 3); ctx.lineTo(cx + R, cy + i * R / 3); ctx.stroke();
    }
    for (let i = 1; i <= 3; i++) {
      ctx.beginPath(); ctx.arc(cx, cy, R * i / 3, 0, Math.PI * 2);
      ctx.strokeStyle = i === 3 ? 'rgba(255,255,255,0.13)' : 'rgba(255,255,255,0.06)'; ctx.stroke();
    }

    /* streamlines */
    const mag = Math.hypot(vec.x, vec.y);
    const ang = Math.atan2(vec.y, vec.x);
    lines.forEach((L, i) => {
      L.s += dt * (0.18 + mag * 0.9);
      if (L.s > 1.25) { L.s = -0.25; L.o = Math.random(); }
      const base = (L.o - 0.5) * 2 * R * 0.92;
      const px = -Math.cos(ang) * base, py = -Math.sin(ang) * base;
      const along = (L.s - 0.5) * 2 * R * 1.25;
      const x0 = cx + px + Math.cos(ang) * along, y0 = cy + py + Math.sin(ang) * along;
      const len = L.l * R * (0.4 + mag);
      ctx.beginPath();
      ctx.moveTo(x0, y0);
      ctx.lineTo(x0 + Math.cos(ang) * len, y0 + Math.sin(ang) * len);
      const fade = Math.sin(clamp(L.s / 1.25, 0, 1) * Math.PI);
      ctx.strokeStyle = 'rgba(255,255,255,' + (0.05 + fade * 0.30 * (0.3 + mag)).toFixed(3) + ')';
      ctx.lineWidth = 1.4; ctx.stroke(); ctx.lineWidth = 1;
    });

    /* vector */
    const hx = cx + vec.x * R, hy = cy + vec.y * R;
    ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(hx, hy);
    ctx.strokeStyle = api.card.accent; ctx.lineWidth = 2; ctx.stroke(); ctx.lineWidth = 1;
    const ah = 9, aa = Math.atan2(hy - cy, hx - cx);
    ctx.beginPath();
    ctx.moveTo(hx, hy);
    ctx.lineTo(hx - ah * Math.cos(aa - 0.42), hy - ah * Math.sin(aa - 0.42));
    ctx.lineTo(hx - ah * Math.cos(aa + 0.42), hy - ah * Math.sin(aa + 0.42));
    ctx.closePath(); ctx.fillStyle = api.card.accent; ctx.fill();
    ctx.beginPath(); ctx.arc(hx, hy, 5.5, 0, Math.PI * 2);
    ctx.strokeStyle = 'rgba(255,255,255,0.9)'; ctx.stroke();
    ctx.beginPath(); ctx.arc(cx, cy, 2.4, 0, Math.PI * 2); ctx.fillStyle = 'rgba(255,255,255,0.6)'; ctx.fill();

    ctx.font = '500 8.5px ui-monospace, SFMono-Regular, Menlo, monospace';
    ctx.fillStyle = 'rgba(255,255,255,0.3)'; ctx.textAlign = 'center';
    ctx.fillText(cfg.xLabel.toUpperCase(), w - 34, hh - 6);
    ctx.textAlign = 'left'; ctx.fillText(cfg.yLabel.toUpperCase(), 6, 12);
  }
  addLoop(draw); draw(0);

  bindDrag(cv.c, {
    onMove(p) {
      const w = cv.w(), hh = cv.h(), R = Math.min(w, hh) / 2 - 16;
      vec.x = clamp((p.px - w / 2) / R, -1, 1);
      vec.y = clamp((p.py - hh / 2) / R, -1, 1);
      hero.set({ num: fmt(speed(), 1), unit: cfg.speedUnit, badge: cfg.readout.toUpperCase(),
                 sub: 'Bearing ' + fmt(dir(), 0) + '° · drag the vector head' });
      rd.innerHTML =
        '<div class="rgrid__c"><span class="rgrid__k">' + cfg.xLabel.toUpperCase() + '</span><span class="rgrid__v">' + fmt(vec.x, 3) + '</span></div>' +
        '<div class="rgrid__c"><span class="rgrid__k">' + cfg.yLabel.toUpperCase() + '</span><span class="rgrid__v">' + fmt(vec.y, 3) + '</span></div>' +
        '<div class="rgrid__c"><span class="rgrid__k">MAGNITUDE</span><span class="rgrid__v">' + fmt(Math.hypot(vec.x, vec.y), 3) + '</span></div>' +
        '<div class="rgrid__c"><span class="rgrid__k">BEARING</span><span class="rgrid__v">' + fmt(dir(), 1) + '°</span></div>';
    },
    onEnd: () => tick()
  });
  const rd = api.readouts([]);
  rd.innerHTML =
    '<div class="rgrid__c"><span class="rgrid__k">' + cfg.xLabel.toUpperCase() + '</span><span class="rgrid__v">' + fmt(vec.x, 3) + '</span></div>' +
    '<div class="rgrid__c"><span class="rgrid__k">' + cfg.yLabel.toUpperCase() + '</span><span class="rgrid__v">' + fmt(vec.y, 3) + '</span></div>' +
    '<div class="rgrid__c"><span class="rgrid__k">MAGNITUDE</span><span class="rgrid__v">' + fmt(Math.hypot(vec.x, vec.y), 3) + '</span></div>' +
    '<div class="rgrid__c"><span class="rgrid__k">BEARING</span><span class="rgrid__v">' + fmt(dir(), 1) + '°</span></div>';
};

/* ============================================================
   SLIDER STACK — dense parameter bank
   ============================================================ */
WIDGETS.sliderStack = function (api) {
  const cfg = api.cfg;
  const st = ns0(cfg.key.split('.')[0]);
  const kk = cfg.key.split('.')[1];
  st[kk] = st[kk] || {};
  const first = cfg.rows[0];
  if (st[kk][first.k] == null) st[kk][first.k] = first.v;
  const syncHero = () => hero.set({
    num: fmt(st[kk][first.k], first.dp), unit: first.unit, badge: first.label.toUpperCase(),
    sub: cfg.rows.length + ' parameters · drag a slider or scrub the value inline'
  });
  const hero = api.hero({ num: '', unit: '', badge: '', sub: '' });
  cfg.rows.forEach(r => {
    if (st[kk][r.k] == null) st[kk][r.k] = r.v;
    slider(api.body, {
      label: r.label, min: r.min, max: r.max, step: r.step, value: st[kk][r.k], unit: r.unit, dp: r.dp,
      onChange: v => { st[kk][r.k] = v; if (r.k === first.k) syncHero(); }
    });
  });
  syncHero();
};

/* ============================================================
   TOGGLE LIST — feature switches
   ============================================================ */
WIDGETS.toggleList = function (api) {
  const cfg = api.cfg;
  const st = ns0(cfg.key.split('.')[0]);
  const kk = cfg.key.split('.')[1];
  st[kk] = st[kk] || {};
  cfg.rows.forEach(r => { if (st[kk][r.k] == null) st[kk][r.k] = r.v; });
  const on = () => cfg.rows.filter(r => st[kk][r.k]).length;
  const hero = api.hero({ num: String(on()), unit: '/ ' + cfg.rows.length, badge: 'ACTIVE', sub: 'Feature switches for this component' });
  cfg.rows.forEach(r => {
    switchRow(api.body, {
      label: r.label, value: st[kk][r.k],
      onChange: v => { st[kk][r.k] = v; hero.set({ num: String(on()), unit: '/ ' + cfg.rows.length, badge: 'ACTIVE', sub: 'Feature switches for this component' }); }
    });
  });
};

/* ============================================================
   STAT TILES — key/value report grid
   ============================================================ */
WIDGETS.statTiles = function (api) {
  const cfg = api.cfg;
  const t0 = cfg.tiles[0];
  const hero = api.hero({ num: t0.v, unit: t0.u, badge: t0.k.toUpperCase(), sub: cfg.tiles.length + ' fields · baked asset report, refreshes on reimport' });
  const g = h('div', 'tiles');
  g.innerHTML = cfg.tiles.map(t =>
    '<div class="tile"><span class="tile__v">' + t.v + '<i>' + (t.u || '') + '</i></span><span class="tile__k">' + t.k + '</span></div>'
  ).join('');
  api.body.appendChild(g);
};

/* ============================================================
   LAYER STACK — weighted strata / paint / depth bands
   ============================================================ */
WIDGETS.layerStack = function (api) {
  const cfg = api.cfg;
  const mode = cfg.mode || 'coverage';
  const st = ns0(cfg.key.split('.')[0]);
  const kk = cfg.key.split('.')[1];
  if (!st[kk]) st[kk] = cfg.layers.map(L => L.w);

  const cap = mode === 'depth' ? 'DEPTH BAND' : mode === 'paint' ? 'PAINT WEIGHT' : 'COVERAGE';
  const hero = api.hero({
    num: fmt(st[kk].reduce((a, b) => a + b, 0) / cfg.layers.length * 100, 0), unit: '% MEAN',
    badge: cap, sub: cfg.layers.length + ' ' + (mode === 'depth' ? 'absorption bands' : mode === 'paint' ? 'paint layers' : 'vegetation strata')
  });

  const list = h('div', 'layers');
  api.body.appendChild(list);
  const rows = [];

  cfg.layers.forEach((L, i) => {
    const row = h('div', 'lrow');
    row.style.setProperty('--c', L.c);
    row.innerHTML =
      '<span class="lrow__sw"></span>' +
      '<span class="lrow__n">' + L.name + '<i>' + (L.meta || '') + '</i></span>' +
      '<span class="lrow__v">0%</span>' +
      '<div class="lrow__tr"><div class="lrow__fl"></div><div class="lrow__kn"></div></div>';
    list.appendChild(row);
    const tr = row.querySelector('.lrow__tr'), fl = row.querySelector('.lrow__fl'),
          kn = row.querySelector('.lrow__kn'), vv = row.querySelector('.lrow__v');
    const paint = () => {
      const t = st[kk][i];
      fl.style.width = (t * 100) + '%'; kn.style.left = 'calc(' + (t * 100) + '% - 6px)';
      vv.textContent = fmt(t * 100, 0) + '%';
      const tot = st[kk].reduce((a, b) => a + b, 0) / cfg.layers.length;
      hero.set({ num: fmt(tot * 100, 0), unit: '% MEAN', badge: cap,
                 sub: cfg.layers.length + ' ' + (mode === 'depth' ? 'absorption bands' : mode === 'paint' ? 'paint layers' : 'vegetation strata') });
    };
    bindDrag(tr, { onMove: p => { st[kk][i] = clamp(p.x, 0, 1); paint(); }, onEnd: () => tick() });
    rows.push(paint); paint();
  });

  if (mode === 'depth') {
    slider(api.body, { label: 'Absorption Scale', min: 0.1, max: 4, step: 0.01, value: 1.20, unit: '', dp: 2 });
    slider(api.body, { label: 'Scattering Albedo', min: 0, max: 1, step: 0.01, value: 0.42, unit: '', dp: 2 });
  } else if (mode === 'paint') {
    switchRow(api.body, { label: 'Weight-Blended Layered Material', value: true });
    slider(api.body, { label: 'Layer Blend Sharpness', min: 0.05, max: 1, step: 0.01, value: 0.34, unit: '', dp: 2 });
  } else {
    switchRow(api.body, { label: 'Density Falloff by Slope', value: true });
    slider(api.body, { label: 'Rejection Threshold', min: 0, max: 1, step: 0.01, value: 0.22, unit: '', dp: 2 });
  }
};

/* ============================================================
   LOD PYRAMID — LOD chain bars with screen-size gates
   ============================================================ */
WIDGETS.lodPyramid = function (api) {
  const cfg = api.cfg || {};
  const mode = cfg.mode || 'foliage';
  const key = cfg.key || 'forest.lod';
  const st = ns0(key.split('.')[0]);
  const kk = key.split('.')[1];
  const DEF = mode === 'mesh'
    ? [{ name: 'LOD0', tris: 128400, screen: 1 }, { name: 'LOD1', tris: 52300, screen: 0.5 }, { name: 'LOD2', tris: 18400, screen: 0.24 }, { name: 'LOD3', tris: 4200, screen: 0.09 }]
    : [{ name: 'LOD0', tris: 4280, screen: 1 }, { name: 'LOD1', tris: 1960, screen: 0.45 }, { name: 'LOD2', tris: 720, screen: 0.18 }, { name: 'LOD3 · Billboard', tris: 2, screen: 0.05 }];
  if (!st[kk]) st[kk] = (cfg.lods || DEF).map(L => ({ ...L }));
  const lods = st[kk];

  const total = () => lods.reduce((a, b) => a + b.tris, 0);
  const hero = api.hero({ num: fmt(lods[0].tris, 0), unit: 'TRIS', badge: lods.length + ' LODS', sub: (mode === 'mesh' ? 'Static mesh' : 'Per-instance foliage') + ' LOD chain · auto screen-size gates' });

  const box = h('div', 'lods');
  api.body.appendChild(box);
  const bars = [];
  lods.forEach((L, i) => {
    const row = h('div', 'lod');
    row.innerHTML =
      '<div class="lod__bar">' +
        '<span class="lod__n">' + L.name + '</span>' +
        '<div class="lod__tr"><div class="lod__fl"></div></div>' +
        '<span class="lod__t"></span>' +
      '</div>';
    box.appendChild(row);
    bars.push({ row, fl: row.querySelector('.lod__fl'), t: row.querySelector('.lod__t'), L, i });
  });
  function paint() {
    const mx = Math.max(...lods.map(l => l.tris));
    bars.forEach((b, i) => {
      b.fl.style.width = clamp(b.L.tris / mx * 100, 2, 100) + '%';
      b.t.textContent = fmt(b.L.tris, 0);
      b.row.style.setProperty('--i', i);
    });
    hero.set({ num: fmt(lods[0].tris, 0), unit: 'TRIS', badge: lods.length + ' LODS',
               sub: 'Chain total ' + fmt(total(), 0) + ' · ' + fmt((1 - lods[lods.length - 1].tris / lods[0].tris) * 100, 2) + '% reduction vs LOD0' });
    rd.innerHTML = lods.map((L, i) =>
      '<div class="rgrid__c"><span class="rgrid__k">' + L.name.split(' ')[0].toUpperCase() + '</span><span class="rgrid__v">' + fmt(L.screen * 100, 0) + '%</span></div>').join('');
  }
  const rd = api.readouts([]);
  paint();

  bars.forEach(b => {
    slider(api.body, {
      label: b.L.name + ' Screen Size', min: 0.01, max: 1, step: 0.01, value: b.L.screen, unit: '', dp: 2,
      onChange: v => { b.L.screen = v; paint(); }
    });
  });
  switchRow(api.body, { label: mode === 'mesh' ? 'Auto-Compute LOD Distances' : 'Cast Shadows on All LODs', value: true, onChange: paint });
};

/* ============================================================
   DENSITY GRID — painted instancing density matrix
   ============================================================ */
WIDGETS.densityGrid = function (api) {
  const st = ns0('forest');
  if (!st.grid) {
    const C = 20, R = 12;
    st.grid = { C, R, d: [] };
    for (let i = 0; i < C * R; i++) {
      const x = (i % C) / C, y = ((i / C) | 0) / R;
      const n = (Math.sin(x * 7.1) * Math.cos(y * 5.3) + Math.sin((x + y) * 9.7) * 0.6) * 0.5 + 0.5;
      st.grid.d.push(clamp(n * 0.9 + hash01(i) * 0.25, 0, 1));
    }
  }
  const G = st.grid;
  st.dens = st.dens == null ? 0.62 : st.dens;
  st.brush = st.brush == null ? 0.24 : st.brush;

  const mean = () => G.d.reduce((a, b) => a + b, 0) / G.d.length * st.dens;
  const hero = api.hero({ num: fmt(mean() * 18, 1), unit: 'STEMS/m²', badge: '20 × 12 CELLS', sub: 'Paint density directly on the matrix · brush radius ' + fmt(st.brush * 100, 0) + '%' });
  const plate = h('div', 'plate plate--grid');
  api.body.appendChild(plate);
  const cv = mkCanvas(plate, 182);
  let painting = false;

  const seeds = Array.from({ length: G.C * G.R }, (_, i) => Array.from({ length: 9 }, (_, j) => [hash01(i * 31 + j), hash01(i * 17 + j * 7)]));

  function draw() {
    const { ctx } = cv, w = cv.w(), hh = cv.h();
    ctx.clearRect(0, 0, w, hh);
    const pad = 10, iw = w - pad * 2, ih = hh - pad * 2;
    const cw = iw / G.C, ch = ih / G.R;
    ctx.strokeStyle = 'rgba(255,255,255,0.045)';
    for (let x = 0; x <= G.C; x++) { ctx.beginPath(); ctx.moveTo(pad + x * cw, pad); ctx.lineTo(pad + x * cw, pad + ih); ctx.stroke(); }
    for (let y = 0; y <= G.R; y++) { ctx.beginPath(); ctx.moveTo(pad, pad + y * ch); ctx.lineTo(pad + iw, pad + y * ch); ctx.stroke(); }

    G.d.forEach((d, i) => {
      const cxp = i % G.C, cyp = (i / G.C) | 0;
      const eff = clamp(d * st.dens, 0, 1);
      const n = Math.round(eff * 9);
      const bx = pad + cxp * cw, by = pad + cyp * ch;
      ctx.fillStyle = 'rgba(61,220,151,' + (0.04 + eff * 0.16).toFixed(3) + ')';
      ctx.fillRect(bx + 1, by + 1, cw - 2, ch - 2);
      for (let j = 0; j < n; j++) {
        const s = seeds[i][j];
        ctx.beginPath();
        ctx.arc(bx + 2 + s[0] * (cw - 4), by + 2 + s[1] * (ch - 4), 0.9 + eff * 0.7, 0, Math.PI * 2);
        ctx.fillStyle = 'rgba(163,230,53,' + (0.35 + eff * 0.6).toFixed(2) + ')';
        ctx.fill();
      }
    });
    ctx.strokeStyle = 'rgba(255,255,255,0.12)'; ctx.strokeRect(pad, pad, iw, ih);
  }

  function apply(p, add) {
    const w = cv.w(), hh = cv.h(), pad = 10, iw = w - pad * 2, ih = hh - pad * 2;
    const cw = iw / G.C, ch = ih / G.R;
    const bx = (p.px - pad) / cw, by = (p.py - pad) / ch;
    const rad = st.brush * Math.min(G.C, G.R) * 1.6;
    let changed = false;
    G.d.forEach((d, i) => {
      const x = i % G.C, y = (i / G.C) | 0;
      const dist = Math.hypot(x + 0.5 - bx, y + 0.5 - by);
      if (dist < rad) {
        const f = 1 - dist / rad;
        G.d[i] = clamp(d + (add ? 1 : -1) * f * 0.28, 0, 1);
        changed = true;
      }
    });
    if (changed) {
      draw();
      hero.set({ num: fmt(mean() * 18, 1), unit: 'STEMS/m²', badge: '20 × 12 CELLS', sub: 'Paint density directly on the matrix · brush radius ' + fmt(st.brush * 100, 0) + '%' });
    }
  }
  bindDrag(cv.c, {
    onStart: p => { painting = true; apply(p, true); },
    onMove: p => { if (painting) apply(p, true); },
    onEnd: () => { painting = false; tick(); }
  });
  cv.c.addEventListener('contextmenu', e => e.preventDefault());
  draw();

  slider(api.body, { label: 'Global Density Multiplier', min: 0, max: 2, step: 0.01, value: st.dens, unit: '×', dp: 2, onChange: v => { st.dens = v; draw(); hero.set({ num: fmt(mean() * 18, 1), unit: 'STEMS/m²', badge: '20 × 12 CELLS', sub: 'Paint density directly on the matrix · brush radius ' + fmt(st.brush * 100, 0) + '%' }); } });
  slider(api.body, { label: 'Brush Radius', min: 0.05, max: 0.6, step: 0.01, value: st.brush, dp: 2, onChange: v => { st.brush = v; } });
  const act = h('div', 'chips');
  const b1 = h('button', 'chip', 'Cull Sparse Cells'); const b2 = h('button', 'chip', 'Reseed Noise');
  act.append(b1, b2); api.body.appendChild(act);
  b1.addEventListener('click', () => { G.d = G.d.map(d => d < 0.18 ? 0 : d); draw(); thunk(); });
  b2.addEventListener('click', () => { G.d = G.d.map(() => Math.random()); draw(); thunk(); });
};

/* ============================================================
   WAVE SPECTRUM — animated Gerstner component bars
   ============================================================ */
WIDGETS.waveSpectrum = function (api) {
  const st = ns0('ocean');
  if (!st.spec) st.spec = Array.from({ length: 28 }, (_, i) => ({ a: Math.exp(-i / 7) * rnd(0.6, 1.2), p: Math.random() * 6.28 }));
  st.wind = st.wind == null ? 11.4 : st.wind;
  st.chop = st.chop == null ? 0.62 : st.chop;

  const peak = () => Math.max(...st.spec.map((c, i) => c.a * (st.wind / 12) * (1 - i / 60)));
  const hero = api.hero({ num: fmt(st.wind, 1), unit: 'm/s WIND', badge: fmt(peak() * 1.6, 2) + ' m SWELL', sub: '28 Gerstner components · JONSWAP peak enhancement' });
  const plate = h('div', 'plate plate--graph');
  api.body.appendChild(plate);
  const cv = mkCanvas(plate, 156);
  let t = 0, hover = -1;

  function draw(dt) {
    t += dt;
    const { ctx } = cv, w = cv.w(), hh = cv.h();
    ctx.clearRect(0, 0, w, hh);
    const pad = { l: 12, r: 12, t: 16, b: 22 }, iw = w - pad.l - pad.r, ih = hh - pad.t - pad.b;
    ctx.strokeStyle = 'rgba(255,255,255,0.06)';
    ctx.beginPath(); ctx.moveTo(pad.l, pad.t + ih); ctx.lineTo(w - pad.r, pad.t + ih); ctx.stroke();

    const bw = iw / st.spec.length;
    st.spec.forEach((c, i) => {
      const amp = c.a * (st.wind / 12) * (1 - i / 60);
      const wob = Math.sin(t * (1.4 + i * 0.22) + c.p) * 0.16 * st.chop;
      const v = clamp(amp + wob, 0, 1.3);
      const bh = ih * clamp(v / 1.3, 0.01, 1);
      const x = pad.l + i * bw;
      const g = ctx.createLinearGradient(0, pad.t + ih - bh, 0, pad.t + ih);
      g.addColorStop(0, i === hover ? '#ffffff' : 'rgba(45,212,191,0.95)');
      g.addColorStop(1, 'rgba(45,212,191,0.10)');
      ctx.fillStyle = g;
      ctx.fillRect(x + 1, pad.t + ih - bh, Math.max(1.5, bw - 2.5), bh);
      ctx.fillStyle = i === hover ? '#fff' : 'rgba(255,255,255,0.5)';
      ctx.fillRect(x + 1, pad.t + ih - bh, Math.max(1.5, bw - 2.5), 1.4);
    });

    /* envelope */
    ctx.beginPath();
    st.spec.forEach((c, i) => {
      const amp = c.a * (st.wind / 12) * (1 - i / 60);
      const x = pad.l + i * bw + bw / 2, y = pad.t + ih * (1 - clamp(amp / 1.3, 0, 1));
      i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    });
    ctx.strokeStyle = 'rgba(255,255,255,0.30)'; ctx.setLineDash([4, 3]); ctx.lineWidth = 1.2; ctx.stroke();
    ctx.setLineDash([]); ctx.lineWidth = 1;

    ctx.font = '500 8.5px ui-monospace, SFMono-Regular, Menlo, monospace';
    ctx.fillStyle = 'rgba(255,255,255,0.28)'; ctx.textAlign = 'left';
    ctx.fillText('0.03 Hz', pad.l, hh - 6);
    ctx.textAlign = 'center'; ctx.fillText('WAVE FREQUENCY', pad.l + iw / 2, hh - 6);
    ctx.textAlign = 'right'; ctx.fillText('1.20 Hz', w - pad.r, hh - 6);
  }
  addLoop(draw); draw(0);

  bindDrag(cv.c, {
    onMove(p) {
      const w = cv.w(), pad = 12, iw = w - pad * 2, bw = iw / st.spec.length;
      const i = clamp(Math.floor((p.px - pad) / bw), 0, st.spec.length - 1);
      hover = i;
      const hgt = clamp(1 - (p.py - 16) / (cv.h() - 38), 0, 1.3);
      st.spec[i].a = clamp(hgt / (st.wind / 12) / (1 - i / 60), 0.02, 2);
      hero.set({ num: fmt(0.03 + i / st.spec.length * 1.17, 3), unit: 'Hz', badge: fmt(st.spec[i].a * 1.6, 2) + ' m', sub: 'Component ' + (i + 1) + ' of 28 · amplitude ' + fmt(st.spec[i].a, 3) });
    },
    onEnd() { hover = -1; hero.set({ num: fmt(st.wind, 1), unit: 'm/s WIND', badge: fmt(peak() * 1.6, 2) + ' m SWELL', sub: '28 Gerstner components · JONSWAP peak enhancement' }); }
  });

  slider(api.body, { label: 'Wind Speed', min: 0, max: 32, step: 0.1, value: st.wind, unit: 'm/s', dp: 1, onChange: v => { st.wind = v; hero.set({ num: fmt(v, 1), unit: 'm/s WIND', badge: fmt(peak() * 1.6, 2) + ' m SWELL', sub: '28 Gerstner components · JONSWAP peak enhancement' }); } });
  slider(api.body, { label: 'Choppiness', min: 0, max: 1.5, step: 0.01, value: st.chop, dp: 2, onChange: v => st.chop = v });
  slider(api.body, { label: 'Peak Enhancement γ', min: 1, max: 7, step: 0.1, value: 3.3, dp: 1 });
};

/* ============================================================
   POLAR ROSE — directional energy distribution
   ============================================================ */
WIDGETS.polarRose = function (api) {
  const st = ns0('ocean');
  st.dir = st.dir == null ? 214 : st.dir;
  st.spread = st.spread == null ? 0.38 : st.spread;
  if (!st.rose) st.rose = Array.from({ length: 48 }, (_, i) => 0.55 + hash01(i * 3.3) * 0.45);

  const hero = api.hero({ num: fmt(st.dir, 0), unit: '° BEARING', badge: 'SWELL ' + fmt(st.spread * 180, 0) + '°', sub: 'Directional wave energy rose · drag to rotate the dominant swell' });
  const plate = h('div', 'plate plate--pad');
  api.body.appendChild(plate);
  const cv = mkCanvas(plate, 200);
  let t = 0;

  function draw(dt) {
    t += dt;
    const { ctx } = cv, w = cv.w(), hh = cv.h();
    ctx.clearRect(0, 0, w, hh);
    const cx = w / 2, cy = hh / 2, R = Math.min(w, hh) / 2 - 20;

    for (let i = 1; i <= 4; i++) {
      ctx.beginPath(); ctx.arc(cx, cy, R * i / 4, 0, Math.PI * 2);
      ctx.strokeStyle = i === 4 ? 'rgba(255,255,255,0.14)' : 'rgba(255,255,255,0.06)'; ctx.stroke();
    }
    for (let a = 0; a < 360; a += 30) {
      const p0 = polar(cx, cy, 0, a), p1 = polar(cx, cy, R, a);
      ctx.beginPath(); ctx.moveTo(p0[0], p0[1]); ctx.lineTo(p1[0], p1[1]);
      ctx.strokeStyle = 'rgba(255,255,255,0.05)'; ctx.stroke();
    }
    ['N', 'E', 'S', 'W'].forEach((l, i) => {
      const p = polar(cx, cy, R + 12, i * 90);
      ctx.font = '600 9px ui-monospace, SFMono-Regular, Menlo, monospace';
      ctx.fillStyle = 'rgba(255,255,255,0.4)'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
      ctx.fillText(l, p[0], p[1]);
    });

    const N = st.rose.length;
    ctx.beginPath();
    for (let i = 0; i <= N; i++) {
      const idx = i % N;
      const a = idx / N * 360;
      const rel = Math.cos((a - st.dir) * Math.PI / 180);
      const lobe = Math.pow(clamp((rel + 1) / 2, 0, 1), 1 / Math.max(0.08, st.spread));
      const r = R * (0.18 + lobe * 0.82) * st.rose[idx] * (0.96 + Math.sin(t * 1.3 + idx) * 0.04);
      const p = polar(cx, cy, r, a);
      i ? ctx.lineTo(p[0], p[1]) : ctx.moveTo(p[0], p[1]);
    }
    ctx.closePath();
    const g = ctx.createRadialGradient(cx, cy, 0, cx, cy, R);
    g.addColorStop(0, 'rgba(91,157,255,0.10)'); g.addColorStop(1, 'rgba(91,157,255,0.42)');
    ctx.fillStyle = g; ctx.fill();
    ctx.strokeStyle = 'rgba(147,197,253,0.9)'; ctx.lineWidth = 1.6; ctx.stroke(); ctx.lineWidth = 1;

    const dp = polar(cx, cy, R * 1.02, st.dir);
    ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(dp[0], dp[1]);
    ctx.strokeStyle = '#fff'; ctx.setLineDash([3, 3]); ctx.stroke(); ctx.setLineDash([]);
    ctx.beginPath(); ctx.arc(dp[0], dp[1], 5, 0, Math.PI * 2);
    ctx.fillStyle = '#5b9dff'; ctx.shadowColor = '#5b9dff'; ctx.shadowBlur = 12; ctx.fill(); ctx.shadowBlur = 0;
  }
  addLoop(draw); draw(0);

  bindDrag(cv.c, {
    onMove(p) {
      const w = cv.w(), hh = cv.h();
      const dx = p.px - w / 2, dy = p.py - hh / 2;
      let a = Math.atan2(dx, -dy) * 180 / Math.PI; if (a < 0) a += 360;
      st.dir = Math.round(a);
      hero.set({ num: fmt(st.dir, 0), unit: '° BEARING', badge: 'SWELL ' + fmt(st.spread * 180, 0) + '°', sub: 'Directional wave energy rose · drag to rotate the dominant swell' });
    }, onEnd: () => tick()
  });
  slider(api.body, { label: 'Directional Spread', min: 0.08, max: 1, step: 0.01, value: st.spread, dp: 2, onChange: v => { st.spread = v; hero.set({ num: fmt(st.dir, 0), unit: '° BEARING', badge: 'SWELL ' + fmt(v * 180, 0) + '°', sub: 'Directional wave energy rose · drag to rotate the dominant swell' }); } });
  slider(api.body, { label: 'Fetch Duration', min: 1, max: 48, step: 1, value: 18, unit: 'h', dp: 0 });
};

/* ============================================================
   CAUSTICS PLATE — animated subsurface refraction
   ============================================================ */
WIDGETS.causticsPlate = function (api) {
  const st = ns0('ocean');
  st.caus = st.caus == null ? 0.72 : st.caus;
  st.refr = st.refr == null ? 1.333 : st.refr;
  const hero = api.hero({ num: fmt(st.caus * 100, 0), unit: '% CAUSTIC', badge: 'IOR ' + fmt(st.refr, 3), sub: 'Refracted focal network at 2 m depth · 64-sample projection' });
  const plate = h('div', 'plate plate--caust');
  api.body.appendChild(plate);
  const cv = mkCanvas(plate, 176);
  let t = 0;

  function draw(dt) {
    t += dt;
    const { ctx } = cv, w = cv.w(), hh = cv.h();
    ctx.clearRect(0, 0, w, hh);
    const g = ctx.createLinearGradient(0, 0, 0, hh);
    g.addColorStop(0, 'rgba(24,86,110,0.55)'); g.addColorStop(1, 'rgba(4,16,26,0.9)');
    ctx.fillStyle = g; ctx.fillRect(0, 0, w, hh);

    const step = 7;
    ctx.globalCompositeOperation = 'lighter';
    for (let y = 0; y < hh; y += step) {
      for (let x = 0; x < w; x += step) {
        const u = x / w * 5.2, v = y / hh * 5.2;
        const n = Math.sin(u * 1.7 + t * 0.9) * Math.cos(v * 1.3 - t * 0.7) +
                  Math.sin((u + v) * 2.1 + t * 1.3) * 0.7 +
                  Math.cos((u - v) * 1.9 - t * 0.5) * 0.5;
        const f = Math.pow(clamp(n / 2.2 + 0.5, 0, 1), 5.5 - st.caus * 3);
        if (f > 0.02) {
          ctx.fillStyle = 'rgba(140,225,240,' + (f * st.caus * 0.95).toFixed(3) + ')';
          ctx.fillRect(x, y, step + 0.6, step + 0.6);
        }
      }
    }
    ctx.globalCompositeOperation = 'source-over';

    /* depth ruler */
    ctx.font = '500 8.5px ui-monospace, SFMono-Regular, Menlo, monospace';
    ctx.fillStyle = 'rgba(255,255,255,0.34)'; ctx.textAlign = 'left';
    ['0.0 m', '1.0 m', '2.0 m'].forEach((l, i) => ctx.fillText(l, 8, 14 + i * (hh - 28) / 2));
    ctx.strokeStyle = 'rgba(255,255,255,0.14)';
    ctx.beginPath(); ctx.moveTo(4, 10); ctx.lineTo(4, hh - 10); ctx.stroke();
  }
  addLoop(draw); draw(0);

  slider(api.body, { label: 'Caustic Intensity', min: 0, max: 1.5, step: 0.01, value: st.caus, dp: 2, onChange: v => { st.caus = v; hero.set({ num: fmt(v * 100, 0), unit: '% CAUSTIC', badge: 'IOR ' + fmt(st.refr, 3), sub: 'Refracted focal network at 2 m depth · 64-sample projection' }); } });
  slider(api.body, { label: 'Index of Refraction', min: 1.2, max: 1.5, step: 0.001, value: st.refr, dp: 3, onChange: v => { st.refr = v; hero.set({ num: fmt(st.caus * 100, 0), unit: '% CAUSTIC', badge: 'IOR ' + fmt(v, 3), sub: 'Refracted focal network at 2 m depth · 64-sample projection' }); } });
  slider(api.body, { label: 'Surface Normal Strength', min: 0, max: 2, step: 0.01, value: 0.85, dp: 2 });
  switchRow(api.body, { label: 'Underwater Post Process', value: true });
};

/* ============================================================
   HEIGHT PROFILE — draggable elevation spline
   ============================================================ */
WIDGETS.heightProfile = function (api) {
  const st = ns0('terrain');
  if (!st.profile) st.profile = [0.18, 0.34, 0.28, 0.56, 0.78, 0.62, 0.94, 0.71, 0.44, 0.26, 0.12];
  const P = st.profile;
  const hero = api.hero({ num: fmt(Math.max(...P) * 1420, 0), unit: 'm PEAK', badge: P.length + ' KNOTS', sub: 'West → east elevation transect · drag any knot' });
  const plate = h('div', 'plate plate--prof');
  api.body.appendChild(plate);
  const cv = mkCanvas(plate, 168);
  let drag = -1, hover = -1;

  function catmull(pts, n) {
    const out = [];
    const ext = [pts[0], ...pts, pts[pts.length - 1]];
    for (let i = 0; i < ext.length - 3; i++) {
      const p0 = ext[i], p1 = ext[i + 1], p2 = ext[i + 2], p3 = ext[i + 3];
      for (let s = 0; s < n; s++) {
        const t = s / n, t2 = t * t, t3 = t2 * t;
        out.push(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3));
      }
    }
    out.push(pts[pts.length - 1]);
    return out;
  }

  function draw() {
    const { ctx } = cv, w = cv.w(), hh = cv.h();
    ctx.clearRect(0, 0, w, hh);
    const pad = { l: 34, r: 14, t: 14, b: 20 }, iw = w - pad.l - pad.r, ih = hh - pad.t - pad.b;

    for (let i = 0; i <= 4; i++) {
      const y = pad.t + ih * i / 4;
      ctx.strokeStyle = 'rgba(255,255,255,0.05)';
      ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(w - pad.r, y); ctx.stroke();
      ctx.fillStyle = 'rgba(255,255,255,0.28)'; ctx.textAlign = 'right';
      ctx.font = '500 8.5px ui-monospace, SFMono-Regular, Menlo, monospace';
      ctx.fillText(fmt(1420 * (1 - i / 4), 0) + 'm', pad.l - 6, y + 3);
    }
    /* sea level */
    const sy = pad.t + ih * (1 - 0.10);
    ctx.strokeStyle = 'rgba(45,212,191,0.45)'; ctx.setLineDash([4, 4]);
    ctx.beginPath(); ctx.moveTo(pad.l, sy); ctx.lineTo(w - pad.r, sy); ctx.stroke(); ctx.setLineDash([]);

    const curve = catmull(P, 14);
    ctx.beginPath();
    ctx.moveTo(pad.l, pad.t + ih);
    curve.forEach((v, i) => ctx.lineTo(pad.l + iw * i / (curve.length - 1), pad.t + ih * (1 - v)));
    ctx.lineTo(pad.l + iw, pad.t + ih); ctx.closePath();
    const g = ctx.createLinearGradient(0, pad.t, 0, pad.t + ih);
    g.addColorStop(0, 'rgba(163,130,92,0.62)'); g.addColorStop(0.55, 'rgba(90,74,56,0.35)'); g.addColorStop(1, 'rgba(30,28,26,0.12)');
    ctx.fillStyle = g; ctx.fill();

    ctx.beginPath();
    curve.forEach((v, i) => {
      const x = pad.l + iw * i / (curve.length - 1), y = pad.t + ih * (1 - v);
      i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    });
    ctx.strokeStyle = '#c9a277'; ctx.lineWidth = 1.9; ctx.stroke(); ctx.lineWidth = 1;

    P.forEach((v, i) => {
      const x = pad.l + iw * i / (P.length - 1), y = pad.t + ih * (1 - v);
      const on = i === drag || i === hover;
      ctx.beginPath(); ctx.arc(x, y, on ? 6 : 4, 0, Math.PI * 2);
      ctx.fillStyle = on ? '#fff' : '#c9a277'; ctx.fill();
      ctx.beginPath(); ctx.arc(x, y, 1.6, 0, Math.PI * 2); ctx.fillStyle = '#14100c'; ctx.fill();
    });
    ctx.strokeStyle = 'rgba(255,255,255,0.12)'; ctx.strokeRect(pad.l, pad.t, iw, ih);
    ctx.fillStyle = 'rgba(255,255,255,0.3)'; ctx.textAlign = 'center';
    ctx.fillText('TRANSECT · W → E · 8.4 km', pad.l + iw / 2, hh - 5);
  }

  bindDrag(cv.c, {
    onStart(p) {
      const w = cv.w(), pad = { l: 34, r: 14 }, iw = w - pad.l - pad.r;
      let best = 1e9; drag = -1;
      P.forEach((v, i) => {
        const x = pad.l + iw * i / (P.length - 1);
        const d = Math.abs(x - p.px); if (d < best) { best = d; drag = i; }
      });
      if (best > 22) drag = -1;
    },
    onMove(p) {
      const hh = cv.h(), pad = { t: 14, b: 20 }, ih = hh - pad.t - pad.b;
      hover = drag;
      if (drag >= 0) {
        P[drag] = clamp(1 - (p.py - pad.t) / ih, 0, 1);
        hero.set({ num: fmt(Math.max(...P) * 1420, 0), unit: 'm PEAK', badge: P.length + ' KNOTS', sub: 'West → east elevation transect · drag any knot' });
        draw();
      }
    },
    onEnd() { drag = -1; hover = -1; draw(); tick(); }
  });
  draw();
  const rd = api.readouts([]);
  function stats() {
    const mean = P.reduce((a, b) => a + b, 0) / P.length;
    const rough = P.reduce((a, b, i) => i ? a + Math.abs(b - P[i - 1]) : a, 0) / (P.length - 1);
    rd.innerHTML =
      '<div class="rgrid__c"><span class="rgrid__k">MEAN</span><span class="rgrid__v">' + fmt(mean * 1420, 0) + ' m</span></div>' +
      '<div class="rgrid__c"><span class="rgrid__k">RELIEF</span><span class="rgrid__v">' + fmt((Math.max(...P) - Math.min(...P)) * 1420, 0) + ' m</span></div>' +
      '<div class="rgrid__c"><span class="rgrid__k">ROUGHNESS</span><span class="rgrid__v">' + fmt(rough, 3) + '</span></div>' +
      '<div class="rgrid__c"><span class="rgrid__k">ABOVE SEA</span><span class="rgrid__v">' + P.filter(v => v > 0.1).length + '/' + P.length + '</span></div>';
  }
  stats();
  const act = h('div', 'chips');
  [['Smooth', () => P.forEach((v, i) => P[i] = i ? (P[i - 1] + v) / 2 : v)],
   ['Terrace', () => P.forEach((v, i) => P[i] = Math.round(v * 6) / 6)],
   ['Flatten', () => P.forEach((v, i) => P[i] = 0.32)]
  ].forEach(([lab, fn]) => {
    const b = h('button', 'chip', lab);
    b.addEventListener('click', () => { fn(); draw(); stats(); thunk(); });
    act.appendChild(b);
  });
  api.body.appendChild(act);
};

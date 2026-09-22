/* ============================================================
   Frontier Engine — Card Shell, UI Primitives, Widget Group A
   (Sun · Sky · Fog)
   ============================================================ */

const WIDGETS = {};

/* ---------- state path helpers ---------- */
function getPath(key, fallback) {
  const v = String(key).split('.').reduce((o, k) => (o == null ? undefined : o[k]), S);
  return v === undefined ? fallback : v;
}
function setPath(key, val) {
  const p = String(key).split('.');
  let o = S;
  for (let i = 0; i < p.length - 1; i++) { o[p[i]] = o[p[i]] || {}; o = o[p[i]]; }
  o[p[p.length - 1]] = val;
}

/* ============================================================
   CARD SHELL
   ============================================================ */
function buildCard(entity, card, host) {
  const root = h('article', 'card');
  root.style.setProperty('--accent', card.accent || '#7f8ea3');
  root.innerHTML =
    '<header class="card__hd">' +
      '<span class="pill"></span>' +
      '<span class="card__ico">' + icon(card.icon || 'sliders', 13) + '</span>' +
      '<h3 class="card__ttl">' + card.title + '</h3>' +
      '<button class="card__act" type="button" aria-label="Expand card">' + icon('arrowUpRight', 13) + '</button>' +
    '</header>';

  const body = h('div', 'card__bd');
  root.appendChild(body);
  host.appendChild(root);

  const api = {
    root, body, card, entity, cfg: card.cfg || {},
    /* big numeric readout */
    hero(o) {
      const w = h('div', 'hero');
      body.appendChild(w);
      const draw = (d) => {
        w.innerHTML =
          '<div class="hero__row">' +
            '<span class="hero__num">' + d.num + '</span>' +
            (d.unit ? '<span class="hero__unit">' + d.unit + '</span>' : '') +
            (d.badge ? '<span class="badge">' + d.badge + '</span>' : '') +
          '</div>' +
          (d.sub ? '<div class="hero__sub">' + d.sub + '</div>' : '');
      };
      draw(o || {});
      return { set: draw, el: w };
    },
    /* secondary inline readouts under the hero */
    readouts(pairs) {
      const r = h('div', 'rgrid');
      r.innerHTML = pairs.map(p =>
        '<div class="rgrid__c"><span class="rgrid__k">' + p[0] + '</span><span class="rgrid__v">' + p[1] + '</span></div>'
      ).join('');
      body.appendChild(r);
      return r;
    },
    /* collapsible drawer revealed by the ↗ button */
    more() {
      const m = h('div', 'card__more');
      m.hidden = true;
      root.appendChild(m);
      api._more = m;
      return m;
    },
    foot() {
      const f = h('div', 'card__ft');
      root.appendChild(f);
      return f;
    }
  };

  root.querySelector('.card__act').addEventListener('click', () => {
    if (!api._more) { root.classList.add('is-pulse'); setTimeout(() => root.classList.remove('is-pulse'), 420); click(1900, 0.02, 0.008); return; }
    api._more.hidden = !api._more.hidden;
    root.classList.toggle('is-open', !api._more.hidden);
    click(api._more.hidden ? 900 : 1500, 0.02, 0.008);
  });

  const fn = WIDGETS[card.kind];
  if (fn) fn(api);
  else body.appendChild(h('div', 'empty', 'No editor for <b>' + card.kind + '</b>'));
  return api;
}

/* ============================================================
   UI PRIMITIVES
   ============================================================ */

/* native range, heavily restyled — keeps the UI keyboard accessible */
function slider(host, o) {
  const dp = o.dp != null ? o.dp : 2;
  const row = h('div', 'srow');
  const top = h('div', 'srow__top');
  top.innerHTML =
    '<span class="srow__l">' + o.label + '</span>' +
    '<span class="srow__v"><b>' + fmt(o.value, dp) + '</b>' + (o.unit ? '<i>' + o.unit + '</i>' : '') + '</span>';
  const inp = h('input', 'srow__in');
  inp.type = 'range'; inp.min = o.min; inp.max = o.max; inp.step = o.step; inp.value = o.value;
  inp.setAttribute('aria-label', o.label);
  const paint = () => inp.style.setProperty('--p', (inv(+inp.value, +o.min, +o.max) * 100).toFixed(2) + '%');
  inp.addEventListener('input', () => {
    paint();
    top.querySelector('b').textContent = fmt(+inp.value, dp);
    o.onChange && o.onChange(+inp.value);
  });
  inp.addEventListener('change', () => tick());
  paint();
  row.append(top, inp);
  host.appendChild(row);
  return {
    input: inp,
    set(v, silent) {
      inp.value = v; paint();
      top.querySelector('b').textContent = fmt(v, dp);
      if (!silent) o.onChange && o.onChange(+v);
    }
  };
}

function chipRow(host, items, onPick, initial) {
  const r = h('div', 'chips');
  items.forEach((it, i) => {
    const b = h('button', 'chip' + ((initial == null ? i === 0 : i === initial) ? ' is-on' : ''), it.label);
    b.type = 'button';
    b.title = it.title || it.label;
    b.addEventListener('click', () => {
      r.querySelectorAll('.chip').forEach(c => c.classList.remove('is-on'));
      b.classList.add('is-on'); click(1500, 0.02, 0.01);
      onPick(it, i);
    });
    r.appendChild(b);
  });
  host.appendChild(r);
  return r;
}

function switchRow(host, o) {
  const row = h('div', 'swrow');
  row.innerHTML =
    '<span class="swrow__l">' + o.label + '</span>' +
    '<button class="sw' + (o.value ? ' is-on' : '') + '" type="button" role="switch" aria-checked="' + !!o.value + '">' +
      '<span class="sw__k"></span>' +
    '</button>';
  const btn = row.querySelector('.sw');
  btn.addEventListener('click', () => {
    o.value = !o.value;
    btn.classList.toggle('is-on', o.value);
    btn.setAttribute('aria-checked', String(o.value));
    click(o.value ? 1750 : 880, 0.03, 0.012);
    o.onChange && o.onChange(o.value);
  });
  host.appendChild(row);
  return row;
}

function labelled(host, text, cls) {
  const l = h('div', cls || 'seclabel', text);
  host.appendChild(l);
  return l;
}

/* thin key/value strip */
function kvStrip(host, pairs) {
  const s = h('div', 'kv');
  s.innerHTML = pairs.map(p => '<span class="kv__k">' + p[0] + '</span><span class="kv__v">' + p[1] + '</span>').join('');
  host.appendChild(s);
  return s;
}

/* ============================================================
   1 · CELESTIAL SUN GIZMO          (Directional Light — Sun)
   ============================================================ */
WIDGETS.celestialGizmo = function (api) {
  const st = S.sun, e = api.entity;
  const hero = api.hero({ num: fmt(st.el, 1), unit: '° ELEVATION', badge: fmt(st.az, 1) + '° AZ', sub: 'Celestial dome · drag the disc to re-aim the sun' });
  const plate = h('div', 'plate plate--dome');
  api.body.appendChild(plate);
  const cv = mkCanvas(plate, 224);

  const SQ = 0.52;
  let geo = { cx: 0, cy: 0, R: 1 };

  const project = () => {
    const w = cv.w(), hh = cv.h();
    const cx = w / 2, cy = hh * 0.795;
    const R = Math.min(w * 0.435, (hh * 0.795 - 26) / SQ * 0.98);
    geo = { cx, cy, R };
    const r = (90 - clamp(st.el, -12, 90)) / 102 * R;
    const a = st.az * Math.PI / 180;
    return { x: cx + r * Math.sin(a), y: cy - r * Math.cos(a) * SQ, r, R };
  };

  function draw() {
    const { ctx } = cv, w = cv.w(), hh = cv.h();
    const { cx, cy, R } = (project(), geo);
    ctx.clearRect(0, 0, w, hh);

    /* dome wash */
    const night = clamp(inv(0, 18, st.el), 0, 1);
    const g = ctx.createLinearGradient(0, 0, 0, hh);
    g.addColorStop(0, 'rgba(20,28,44,' + (0.35 + night * 0.5) + ')');
    g.addColorStop(1, 'rgba(8,10,16,0.9)');
    ctx.fillStyle = g;
    ctx.fillRect(0, 0, w, hh);

    /* altitude rings */
    ctx.lineWidth = 1;
    for (let i = 0; i <= 3; i++) {
      const rr = R * (i / 3);
      ctx.beginPath();
      ctx.ellipse(cx, cy, rr, rr * SQ, 0, 0, Math.PI * 2);
      ctx.strokeStyle = i === 3 ? 'rgba(255,255,255,0.18)' : 'rgba(255,255,255,0.07)';
      ctx.setLineDash(i === 3 ? [] : [3, 4]);
      ctx.stroke();
    }
    ctx.setLineDash([]);

    /* azimuth spokes */
    for (let a = 0; a < 360; a += 30) {
      const rad = a * Math.PI / 180;
      ctx.beginPath();
      ctx.moveTo(cx, cy);
      ctx.lineTo(cx + R * Math.sin(rad), cy - R * SQ * Math.cos(rad));
      ctx.strokeStyle = (a % 90 === 0) ? 'rgba(255,255,255,0.13)' : 'rgba(255,255,255,0.05)';
      ctx.stroke();
    }

    /* ground plane fill below horizon */
    ctx.beginPath();
    ctx.ellipse(cx, cy, R, R * SQ, 0, 0, Math.PI * 2);
    ctx.fillStyle = 'rgba(255,255,255,0.022)';
    ctx.fill();

    /* diurnal arc (east → zenith → west) */
    ctx.beginPath();
    ctx.strokeStyle = 'rgba(255,176,32,0.30)';
    ctx.lineWidth = 1.6;
    ctx.setLineDash([5, 4]);
    for (let a = 90; a <= 270; a += 2) {
      const rad = a * Math.PI / 180;
      const el = 62 * Math.sin((a - 90) * Math.PI / 180);
      const rr = (90 - clamp(el, -12, 90)) / 102 * R;
      const x = cx + rr * Math.sin(rad), y = cy - rr * Math.cos(rad) * SQ;
      a === 90 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    }
    ctx.stroke();
    ctx.setLineDash([]);
    ctx.lineWidth = 1;

    /* cardinal markers */
    const cards = [['N', 0], ['E', 90], ['S', 180], ['W', 270]];
    ctx.font = '600 10px ui-monospace, SFMono-Regular, Menlo, monospace';
    ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    cards.forEach(([lab, a]) => {
      const rad = a * Math.PI / 180;
      const x = cx + (R + 15) * Math.sin(rad), y = cy - (R + 15) * SQ * Math.cos(rad);
      ctx.fillStyle = 'rgba(255,255,255,0.42)';
      ctx.fillText(lab, x, y);
      ctx.beginPath();
      ctx.moveTo(cx + R * Math.sin(rad), cy - R * SQ * Math.cos(rad));
      ctx.lineTo(cx + (R + 6) * Math.sin(rad), cy - (R + 6) * SQ * Math.cos(rad));
      ctx.strokeStyle = 'rgba(255,255,255,0.22)';
      ctx.stroke();
    });

    /* sun disc */
    const p = project();
    const col = rgbCss(kelvinToRGB(st.kelvin));
    const glow = ctx.createRadialGradient(p.x, p.y, 0, p.x, p.y, 46);
    glow.addColorStop(0, 'rgba(255,196,96,0.55)');
    glow.addColorStop(0.4, 'rgba(255,150,50,0.14)');
    glow.addColorStop(1, 'rgba(255,150,50,0)');
    ctx.fillStyle = glow;
    ctx.beginPath(); ctx.arc(p.x, p.y, 46, 0, Math.PI * 2); ctx.fill();

    /* drop line to horizon */
    ctx.beginPath();
    ctx.setLineDash([2, 3]);
    ctx.moveTo(p.x, p.y); ctx.lineTo(p.x, cy);
    ctx.strokeStyle = 'rgba(255,255,255,0.28)'; ctx.stroke();
    ctx.setLineDash([]);
    ctx.beginPath(); ctx.arc(p.x, cy, 2.4, 0, Math.PI * 2);
    ctx.fillStyle = 'rgba(255,255,255,0.5)'; ctx.fill();

    ctx.beginPath(); ctx.arc(p.x, p.y, 9.5, 0, Math.PI * 2);
    ctx.fillStyle = col; ctx.shadowColor = col; ctx.shadowBlur = 22; ctx.fill();
    ctx.shadowBlur = 0;
    ctx.beginPath(); ctx.arc(p.x, p.y, 13.5, 0, Math.PI * 2);
    ctx.strokeStyle = 'rgba(255,255,255,0.55)'; ctx.lineWidth = 1.2; ctx.stroke();
    ctx.lineWidth = 1;

    /* reticle */
    ctx.strokeStyle = 'rgba(255,255,255,0.35)';
    ctx.beginPath();
    ctx.moveTo(p.x - 20, p.y); ctx.lineTo(p.x - 15, p.y);
    ctx.moveTo(p.x + 15, p.y); ctx.lineTo(p.x + 20, p.y);
    ctx.moveTo(p.x, p.y - 20); ctx.lineTo(p.x, p.y - 15);
    ctx.moveTo(p.x, p.y + 15); ctx.lineTo(p.x, p.y + 20);
    ctx.stroke();
  }

  function sync() {
    draw();
    hero.set({
      num: fmt(st.el, 1), unit: '° ELEVATION', badge: fmt(st.az, 1) + '° AZ',
      sub: 'Vector [' + fmt(Math.cos(st.el * Math.PI / 180) * Math.sin(st.az * Math.PI / 180), 3) + ', ' +
           fmt(Math.cos(st.el * Math.PI / 180) * Math.cos(st.az * Math.PI / 180), 3) + ', ' +
           fmt(Math.sin(st.el * Math.PI / 180), 3) + ']'
    });
    rd.innerHTML = rdPairs().map(p =>
      '<div class="rgrid__c"><span class="rgrid__k">' + p[0] + '</span><span class="rgrid__v">' + p[1] + '</span></div>').join('');
    /* keep transform + lux coherent across the app */
    e.transform.rot[0] = -Math.round(st.el * 10) / 10;
    e.transform.rot[1] = Math.round(st.az * 10) / 10;
    st.lux = Math.round(132000 * Math.max(0, Math.sin(clamp(st.el, -6, 90) * Math.PI / 180)) + 120);
    if (st.autoK !== false) st.kelvin = autoKelvin(st.el);
    if (window.__refreshTransform) window.__refreshTransform();
    if (window.__refreshDependents) window.__refreshDependents('sun');
  }

  function rdPairs() {
    const dirX = Math.cos(st.el * Math.PI / 180) * Math.sin(st.az * Math.PI / 180);
    const dirY = Math.cos(st.el * Math.PI / 180) * Math.cos(st.az * Math.PI / 180);
    const dirZ = Math.sin(st.el * Math.PI / 180);
    const phase = st.el > 25 ? 'High Sun' : st.el > 6 ? 'Golden Hour' : st.el > -2 ? 'Twilight' : 'Night';
    return [['DIR X', fmt(dirX, 3)], ['DIR Y', fmt(dirY, 3)], ['DIR Z', fmt(dirZ, 3)], ['PHASE', phase]];
  }
  const rd = api.readouts(rdPairs());

  bindDrag(cv.c, {
    onMove: (p) => {
      const { cx, cy, R } = geo;
      const dx = p.px - cx, dy = (p.py - cy) / SQ;
      let a = Math.atan2(dx, -dy) * 180 / Math.PI;
      if (a < 0) a += 360;
      const rr = Math.hypot(dx, dy);
      st.az = Math.round(a * 10) / 10;
      st.el = Math.round(clamp(90 - (rr / R) * 102, -12, 90) * 10) / 10;
      sync();
    }
  });

  chipRow(api.body, [
    { label: 'Solar Noon', el: 84, az: 180 },
    { label: 'Golden', el: 14, az: 252 },
    { label: 'Dawn', el: 2, az: 92 },
    { label: 'Dusk', el: -1, az: 268 }
  ], (it) => { st.el = it.el; st.az = it.az; thunk(); sync(); });

  sync();
  registerDep(() => sync());
};

/* ============================================================
   2 · PROBE PLATE — Illuminance + scattered photometric probes
   ============================================================ */
WIDGETS.probePlate = function (api) {
  const st = S.sun;
  const PROBES = [
    { n: 'Ground Plane',    x: 0.16, y: 0.74, m: 1.00 },
    { n: 'Foliage Canopy',  x: 0.31, y: 0.40, m: 0.68 },
    { n: 'Citadel Courtyard', x: 0.52, y: 0.62, m: 0.88 },
    { n: 'Cave Mouth',      x: 0.70, y: 0.83, m: 0.07 },
    { n: 'Cloud Top',       x: 0.83, y: 0.24, m: 1.24 },
    { n: 'Water Mirror',    x: 0.44, y: 0.20, m: 1.41 },
    { n: 'North Wall',      x: 0.09, y: 0.34, m: 0.33 },
    { n: 'Watchtower Deck', x: 0.93, y: 0.56, m: 1.05 }
  ];
  const hero = api.hero({ num: fmt(st.lux, 0), unit: 'lx', badge: 'DIRECT', sub: 'Solar illuminance at world origin · 8 calibrated probes' });
  const plate = h('div', 'plate plate--probe');
  api.body.appendChild(plate);
  const cv = mkCanvas(plate, 186);
  const tip = h('div', 'ptip'); plate.appendChild(tip);
  const cross = h('div', 'plate__reticle', icon('crosshair', 13)); plate.appendChild(cross);

  let hover = -1;

  function draw() {
    const { ctx } = cv, w = cv.w(), hh = cv.h();
    ctx.clearRect(0, 0, w, hh);
    const pad = 16;

    /* faint grid */
    ctx.strokeStyle = 'rgba(255,255,255,0.045)'; ctx.lineWidth = 1;
    for (let i = 1; i < 8; i++) {
      const x = pad + (w - pad * 2) * i / 8;
      ctx.beginPath(); ctx.moveTo(x, 12); ctx.lineTo(x, hh - 12); ctx.stroke();
    }
    for (let i = 1; i < 5; i++) {
      const y = 12 + (hh - 24) * i / 5;
      ctx.beginPath(); ctx.moveTo(pad, y); ctx.lineTo(w - pad, y); ctx.stroke();
    }

    /* threshold reference line */
    const ty = 12 + (hh - 24) * (1 - st.threshold / 100);
    ctx.strokeStyle = 'rgba(255,255,255,0.30)';
    ctx.setLineDash([6, 4]);
    ctx.beginPath(); ctx.moveTo(pad, ty); ctx.lineTo(w - pad, ty); ctx.stroke();
    ctx.setLineDash([]);

    /* exposure band above threshold */
    const band = ctx.createLinearGradient(0, 12, 0, ty);
    band.addColorStop(0, 'rgba(245,165,36,0.16)');
    band.addColorStop(1, 'rgba(245,165,36,0.01)');
    ctx.fillStyle = band;
    ctx.fillRect(pad, 12, w - pad * 2, Math.max(0, ty - 12));

    /* probes */
    PROBES.forEach((p, i) => {
      const x = pad + (w - pad * 2) * p.x;
      const y = 12 + (hh - 24) * p.y;
      const over = p.m * st.lux > (st.threshold / 100) * 145000;
      const hot = i === hover;
      ctx.beginPath(); ctx.arc(x, y, hot ? 5 : 3, 0, Math.PI * 2);
      ctx.fillStyle = over ? 'rgba(255,255,255,0.95)' : 'rgba(255,255,255,0.34)';
      ctx.fill();
      if (hot) {
        ctx.beginPath(); ctx.arc(x, y, 10, 0, Math.PI * 2);
        ctx.strokeStyle = 'rgba(245,165,36,0.85)'; ctx.lineWidth = 1.2; ctx.stroke();
      }
      ctx.beginPath();
      ctx.moveTo(x, y); ctx.lineTo(x, ty);
      ctx.strokeStyle = over ? 'rgba(245,165,36,0.22)' : 'rgba(255,255,255,0.07)';
      ctx.lineWidth = 1; ctx.stroke();
    });

    /* draggable threshold handle */
    const hx = pad + (w - pad * 2) * 0.5;
    ctx.beginPath(); ctx.arc(hx, ty, 6, 0, Math.PI * 2);
    ctx.fillStyle = '#ff4d5e'; ctx.shadowColor = '#ff4d5e'; ctx.shadowBlur = 14; ctx.fill(); ctx.shadowBlur = 0;
    ctx.beginPath(); ctx.arc(hx, ty, 2, 0, Math.PI * 2); ctx.fillStyle = '#fff'; ctx.fill();

    /* axis */
    ctx.font = '500 9px ui-monospace, SFMono-Regular, Menlo, monospace';
    ctx.fillStyle = 'rgba(255,255,255,0.32)'; ctx.textAlign = 'left'; ctx.textBaseline = 'middle';
    ctx.fillText('145k lx', 2, 14);
    ctx.fillText('0 lx', 2, hh - 14);
  }

  const sLux = slider(api.body, {
    label: 'Solar Intensity', min: 0, max: 145000, step: 250, value: st.lux, unit: 'lx', dp: 0,
    onChange: v => { st.lux = v; hero.set({ num: fmt(v, 0), unit: 'lx', badge: 'DIRECT', sub: heroSub(v) }); draw(); }
  });
  slider(api.body, {
    label: 'Probe Saturation Threshold', min: 4, max: 98, step: 0.5, value: st.threshold, unit: '%', dp: 1,
    onChange: v => { st.threshold = v; draw(); }
  });

  function heroSub(v) {
    const ev = Math.log2(v / 2.5);
    return 'EV₁₀₀ ' + fmt(ev, 2) + ' · ' + (v > 100000 ? 'blown highlight risk' : v > 20000 ? 'photopic range' : 'scotopic / low key');
  }

  function inspect(p, edit) {
    const w = cv.w(), hh = cv.h(), pad = 16;
    if (edit) st.threshold = clamp((1 - (p.py - 12) / (hh - 24)) * 100, 4, 98);
    hover = -1; let best = 1e9;
    PROBES.forEach((pr, i) => {
      const x = pad + (w - pad * 2) * pr.x, y = 12 + (hh - 24) * pr.y;
      const d = Math.hypot(x - p.px, y - p.py);
      if (d < best) { best = d; hover = i; }
    });
    if (best > 22) hover = -1;
    if (hover >= 0) {
      const pr = PROBES[hover];
      tip.hidden = false;
      tip.style.left = clamp(pr.x * 100, 8, 70) + '%';
      tip.style.top = (pr.y * 100 - 3) + '%';
      tip.innerHTML = '<b>' + pr.n + '</b><span>' + fmt(pr.m * st.lux, 0) + ' lx</span>';
    } else tip.hidden = true;
    draw();
  }
  const clearTip = () => { tip.hidden = true; hover = -1; draw(); };
  bindDrag(cv.c, { onMove: p => inspect(p, true), onHover: p => inspect(p, false), onEnd: clearTip, onLeave: clearTip });

  /* stay in step when the gizmo or the diurnal cycle moves the sun */
  registerDep(() => {
    sLux.set(st.lux, true);
    hero.set({ num: fmt(st.lux, 0), unit: 'lx', badge: 'DIRECT', sub: heroSub(st.lux) });
    draw();
  });
  draw();
};

/* ============================================================
   3 · SPECTRUM BAR — blackbody / tint ramp with bloom swatch
   ============================================================ */
WIDGETS.spectrumBar = function (api) {
  const cfg = api.cfg;
  const key = cfg.key;
  const mode = cfg.mode || 'kelvin';
  let value = getPath(key, cfg.value);
  const min = cfg.min, max = cfg.max;
  const stops = cfg.stops;

  const isKelvin = mode === 'kelvin';
  const css = isKelvin ? rgbCss(kelvinToRGB(value)) : cssFromStops(stops, inv(value, min, max));

  const hero = api.hero({
    num: fmt(value, 0), unit: 'K', badge: isKelvin ? kelvinName(value).toUpperCase() : 'TINT',
    sub: isKelvin ? 'Planckian locus approximation · Tanner Helland' : 'Inscatter luminance ramp'
  });

  const sw = h('div', 'swatch');
  sw.style.setProperty('--c', css);
  sw.innerHTML = '<span class="swatch__glow"></span><span class="swatch__cap">' + (isKelvin ? 'EMISSION' : 'SCATTER') + '</span>';
  api.body.appendChild(sw);

  const ramp = h('div', 'ramp');
  ramp.style.background = 'linear-gradient(90deg,' + stops.join(',') + ')';
  const handle = h('div', 'ramp__hd');
  handle.innerHTML = '<span></span>';
  ramp.appendChild(handle);
  api.body.appendChild(ramp);

  const rd = api.readouts([['CIE x', '—'], ['CIE y', '—'], ['sRGB', '—'], ['HEX', '—']]);

  function cssFromStops(arr, t) {
    const n = arr.length - 1;
    const i = clamp(Math.floor(t * n), 0, n - 1);
    const f = t * n - i;
    const a = hex2rgb(arr[i]), b = hex2rgb(arr[i + 1]);
    return 'rgb(' + Math.round(lerp(a[0], b[0], f)) + ',' + Math.round(lerp(a[1], b[1], f)) + ',' + Math.round(lerp(a[2], b[2], f)) + ')';
  }
  function hex2rgb(s) {
    const m = s.replace('#', '');
    return [parseInt(m.slice(0, 2), 16), parseInt(m.slice(2, 4), 16), parseInt(m.slice(4, 6), 16)];
  }

  function paint() {
    const t = inv(value, min, max);
    handle.style.left = (t * 100) + '%';
    const c = isKelvin ? rgbCss(kelvinToRGB(value)) : cssFromStops(stops, t);
    sw.style.setProperty('--c', c);
    const rgb = isKelvin ? kelvinToRGB(value) : cssFromStops(stops, t).match(/\d+/g).map(Number);
    const hex = '#' + rgb.map(v => ('0' + v.toString(16)).slice(-2)).join('').toUpperCase();
    const xy = isKelvin ? kelvinToXY(value) : [0.3127, 0.3290];
    rd.innerHTML =
      '<div class="rgrid__c"><span class="rgrid__k">CIE x</span><span class="rgrid__v">' + fmt(xy[0], 4) + '</span></div>' +
      '<div class="rgrid__c"><span class="rgrid__k">CIE y</span><span class="rgrid__v">' + fmt(xy[1], 4) + '</span></div>' +
      '<div class="rgrid__c"><span class="rgrid__k">sRGB</span><span class="rgrid__v">' + rgb.join(' ') + '</span></div>' +
      '<div class="rgrid__c"><span class="rgrid__k">HEX</span><span class="rgrid__v">' + hex + '</span></div>';
    hero.set({
      num: fmt(value, isKelvin ? 0 : 0), unit: isKelvin ? 'K' : '',
      badge: isKelvin ? kelvinName(value).toUpperCase() : 'TINT',
      sub: isKelvin ? 'Planckian locus approximation · Tanner Helland' : 'Inscatter luminance ramp'
    });
  }

  bindDrag(ramp, { onMove: p => { value = Math.round(lerp(min, max, clamp(p.x, 0, 1))); setPath(key, value); paint(); } , onEnd: () => tick() });

  const s = slider(api.body, {
    label: isKelvin ? 'Color Temperature' : 'Scatter Wavelength Bias',
    min, max, step: isKelvin ? 10 : 10, value, unit: isKelvin ? 'K' : '', dp: 0,
    onChange: v => { value = v; setPath(key, v); paint(); }
  });

  if (isKelvin) {
    chipRow(api.body, [
      { label: 'Ember 1800', v: 1800 }, { label: 'Tungsten 3200', v: 3200 },
      { label: 'Sun 5600', v: 5600 }, { label: 'Overcast 6500', v: 6500 }, { label: 'Shade 9500', v: 9500 }
    ], it => { value = it.v; setPath(key, it.v); s.set(it.v, true); paint(); thunk(); }, -1);
  }
  paint();

  if (key === 'sun.kelvin') {
    const sw = switchRow(api.body, {
      label: 'Link Temperature to Sun Elevation', value: S.sun.autoK !== false,
      onChange: v => { S.sun.autoK = v; if (v) { S.sun.kelvin = autoKelvin(S.sun.el); value = S.sun.kelvin; setPath(key, value); s.set(value, true); paint(); } }
    });
    sw.style.marginTop = '2px';
    registerDep(() => {
      const nv = S.sun.kelvin;
      if (nv == null) return;
      value = nv; setPath(key, nv); s.set(nv, true); paint();
    });
  }
};

/* ============================================================
   4 · CASCADE SPLITS — CSM partition handles
   ============================================================ */
WIDGETS.cascadeSplits = function (api) {
  const st = S.sun;
  const FAR = 600;
  const hero = api.hero({ num: '4', unit: 'CASCADES', badge: st.cascadeRes + ' px', sub: 'Practical split scheme · logarithmic / uniform blend λ = 0.72' });
  const bar = h('div', 'csm');
  api.body.appendChild(bar);

  const SEGS = ['#3ddc97', '#4cc9f0', '#5b9dff', '#8b7cf6'];
  const segEls = [];
  const handleEls = [];

  function ranges() {
    const b = [0, ...st.splits, 1];
    return b.slice(0, -1).map((s, i) => [s * FAR, b[i + 1] * FAR]);
  }

  function build() {
    bar.innerHTML = '';
    segEls.length = 0; handleEls.length = 0;
    ranges().forEach((r, i) => {
      const seg = h('div', 'csm__seg');
      seg.style.setProperty('--c', SEGS[i]);
      seg.innerHTML = '<span class="csm__i">C' + i + '</span><span class="csm__r">' + fmt(r[0], 0) + '–' + fmt(r[1], 0) + ' m</span>';
      bar.appendChild(seg); segEls.push(seg);
      if (i < 3) {
        const hd = h('div', 'csm__hd', '<span></span>');
        hd.title = 'Drag to rebalance cascade ' + i + ' / ' + (i + 1);
        bindDrag(hd, {
          onMove: p => {
            const br = bar.getBoundingClientRect();
            const nv = clamp((p.cx - br.left) / br.width, 0.01, 0.99);
            const lo = i === 0 ? 0.015 : st.splits[i - 1] + 0.015;
            const hi = i === 2 ? 0.985 : st.splits[i + 1] - 0.015;
            st.splits[i] = clamp(nv, lo, hi);
            layout();
          },
          onEnd: () => thunk()
        });
        bar.appendChild(hd); handleEls.push(hd);
      }
    });
  }
  function layout() {
    const b = [0, ...st.splits, 1];
    segEls.forEach((s, i) => {
      s.style.flex = ((b[i + 1] - b[i]) * 100).toFixed(3);
      const r = [b[i] * FAR, b[i + 1] * FAR];
      s.querySelector('.csm__r').textContent = fmt(r[0], 0) + '–' + fmt(r[1], 0) + ' m';
    });
    handleEls.forEach((hd, i) => {
      const s = segEls[i];
      hd.style.left = (s.offsetLeft + s.offsetWidth) + 'px';
    });
    rd.innerHTML = ranges().map((r, i) =>
      '<div class="rgrid__c"><span class="rgrid__k">CASCADE ' + i + '</span><span class="rgrid__v">' + fmt(r[1] - r[0], 0) + ' m</span></div>'
    ).join('');
  }
  const rd = api.readouts([]);
  build(); layout();

  slider(api.body, {
    label: 'Sun Angular Diameter', min: 0.1, max: 4, step: 0.005, value: st.angular, unit: '°', dp: 3,
    onChange: v => { st.angular = v; }
  });
  labelled(api.body, 'Shadow Map Resolution');
  chipRow(api.body, [
    { label: '1024', v: 1024 }, { label: '2048', v: 2048 }, { label: '4096', v: 4096 }, { label: '8192', v: 8192 }
  ], it => {
    st.cascadeRes = it.v;
    hero.set({ num: '4', unit: 'CASCADES', badge: it.v + ' px', sub: 'Practical split scheme · logarithmic / uniform blend λ = 0.72' });
    thunk();
  }, 2);

  const more = api.more();
  kvStrip(more, [['Filter', 'PCF 5×5'], ['λ Blend', '0.72'], ['Depth Bias', '20'], ['Slope Bias', '4.0'], ['Fade', 'Distance 0.80']]);
};

/* ============================================================
   5 · DIURNAL TIMELINE — 24 h solar cycle scrubber
   ============================================================ */
WIDGETS.diurnalTimeline = function (api) {
  const st = S.sun;
  const strip = h('div', 'dial-strip');
  strip.innerHTML = '<span class="dial-strip__grad"></span>';
  const head = h('div', 'dial-strip__hd');
  head.innerHTML = '<span></span>';
  strip.appendChild(head);
  api.body.appendChild(strip);

  const marks = h('div', 'dial-marks');
  marks.innerHTML = ['00:00', '03:00', '06:00', '09:00', '12:00', '15:00', '18:00', '21:00', '24:00']
    .map(t => '<span>' + t + '</span>').join('');
  api.body.appendChild(marks);

  const hero = api.hero({ num: '13:42', unit: 'SOLAR', badge: 'AFTERNOON', sub: 'Local apparent solar time · 42.8° azimuth' });

  function phaseName(t) {
    if (t < 4.5) return 'Deep Night';
    if (t < 6.2) return 'Astronomical Dawn';
    if (t < 7.4) return 'Golden Hour';
    if (t < 11) return 'Morning';
    if (t < 13.5) return 'Solar Noon';
    if (t < 16.5) return 'Afternoon';
    if (t < 18.4) return 'Golden Hour';
    if (t < 19.8) return 'Civil Dusk';
    return 'Night';
  }
  function hhmm(t) {
    const H = Math.floor(t) % 24, M = Math.floor((t - Math.floor(t)) * 60), Sec = Math.floor((((t - Math.floor(t)) * 60) - M) * 60);
    return ('0' + H).slice(-2) + ':' + ('0' + M).slice(-2) + ':' + ('0' + Sec).slice(-2);
  }
  function sync(fromTime) {
    head.style.left = (st.time / 24 * 100) + '%';
    if (fromTime !== false) {
      /* time → azimuth / elevation on a simple circular path */
      const a = (st.time - 6) / 12 * 180;             /* 06:00 east horizon → 18:00 west horizon */
      st.az = clamp(a, -12, 372) % 360;
      if (st.az < 0) st.az += 360;
      st.el = 62 * Math.sin(clamp(a, 0, 180) * Math.PI / 180) - (a < 0 || a > 180 ? 8 : 0);
      st.el = Math.round(clamp(st.el, -14, 90) * 10) / 10;
      if (st.autoK !== false) st.kelvin = autoKelvin(st.el);
      st.lux = Math.round(132000 * Math.max(0, Math.sin(clamp(st.el, -6, 90) * Math.PI / 180)) + 120);
    }
    hero.set({ num: hhmm(st.time).slice(0, 5), unit: 'SOLAR', badge: phaseName(st.time).toUpperCase(),
               sub: 'Elevation ' + fmt(st.el, 1) + '° · ' + fmt(st.lux, 0) + ' lx · ' + fmt(st.kelvin, 0) + ' K' });
    if (window.__refreshTransform) window.__refreshTransform();
    if (window.__refreshDependents) window.__refreshDependents('sun');
  }

  bindDrag(strip, {
    onMove: p => { st.time = clamp(p.x, 0, 0.9999) * 24; sync(); },
    onEnd: () => thunk()
  });

  const ctl = h('div', 'tlctl');
  const playBtn = h('button', 'btn btn--go', icon('play', 13) + '<span>Simulate Cycle</span>');
  ctl.appendChild(playBtn);
  api.body.appendChild(ctl);

  chipRow(ctl, [{ label: '1×', v: 1 }, { label: '10×', v: 10 }, { label: '60×', v: 60 }, { label: '300×', v: 300 }],
    it => { st.speed = it.v; }, 2);

  playBtn.addEventListener('click', () => {
    st.playing = !st.playing;
    playBtn.innerHTML = icon(st.playing ? 'pause' : 'play', 13) + '<span>' + (st.playing ? 'Pause Cycle' : 'Simulate Cycle') + '</span>';
    playBtn.classList.toggle('is-live', st.playing);
    thunk();
  });

  addLoop((dt) => {
    if (!st.playing) return;
    st.time = (st.time + dt * st.speed / 60) % 24;
    sync();
  });

  registerDep(() => sync(false));
  sync(true);
};

/* ============================================================
   6 · RT BUDGET GRAPH — stepped histogram + target polyline
   ============================================================ */
WIDGETS.rtBudgetGraph = function (api) {
  const cfg = api.cfg;
  let series, target, unit, label;
  if (cfg.key) {
    series = cfg.series.slice();
    target = cfg.target; unit = cfg.unit; label = cfg.label;
  } else {
    series = S.sun.rt.slice(); target = S.sun.rtTarget; unit = 'ms'; label = 'GPU Shadow Compute';
  }
  const cur = () => cfg.key ? series[series.length - 1] : S.sun.rt[S.sun.rt.length - 1];
  const hero = api.hero({
    num: fmt(cur(), 2), unit, badge: cur() < target ? 'WITHIN BUDGET' : 'OVER BUDGET',
    sub: label + ' · target < ' + fmt(target, 2) + ' ' + unit + ' over 20 frames'
  });

  const plate = h('div', 'plate plate--graph');
  api.body.appendChild(plate);
  const cv = mkCanvas(plate, 132);
  let hoverI = -1;

  function draw() {
    const { ctx } = cv, w = cv.w(), hh = cv.h();
    ctx.clearRect(0, 0, w, hh);
    const pad = { l: 30, r: 12, t: 14, b: 20 };
    const iw = w - pad.l - pad.r, ih = hh - pad.t - pad.b;
    const data = cfg.key ? series : S.sun.rt;
    const maxV = Math.max(target * 1.35, ...data) * 1.12;
    const bw = iw / data.length;

    /* grid + y axis */
    ctx.font = '500 8.5px ui-monospace, SFMono-Regular, Menlo, monospace';
    ctx.textAlign = 'right'; ctx.textBaseline = 'middle';
    for (let i = 0; i <= 3; i++) {
      const y = pad.t + ih * (1 - i / 3);
      ctx.strokeStyle = 'rgba(255,255,255,0.05)';
      ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(w - pad.r, y); ctx.stroke();
      ctx.fillStyle = 'rgba(255,255,255,0.30)';
      ctx.fillText(fmt(maxV * i / 3, 2), pad.l - 6, y);
    }

    /* stepped histogram */
    data.forEach((v, i) => {
      const bh = ih * (v / maxV);
      const x = pad.l + i * bw;
      const over = v > target;
      const g = ctx.createLinearGradient(0, pad.t + ih - bh, 0, pad.t + ih);
      g.addColorStop(0, over ? 'rgba(255,107,138,0.55)' : 'rgba(163,230,53,0.42)');
      g.addColorStop(1, over ? 'rgba(255,107,138,0.06)' : 'rgba(163,230,53,0.05)');
      ctx.fillStyle = i === hoverI ? 'rgba(255,255,255,0.34)' : g;
      ctx.fillRect(x + 1.5, pad.t + ih - bh, Math.max(1, bw - 3), bh);
      ctx.fillStyle = over ? 'rgba(255,107,138,0.85)' : 'rgba(163,230,53,0.8)';
      ctx.fillRect(x + 1.5, pad.t + ih - bh, Math.max(1, bw - 3), 1.6);
    });

    /* target budget line */
    const ty = pad.t + ih * (1 - target / maxV);
    ctx.strokeStyle = 'rgba(255,255,255,0.42)';
    ctx.setLineDash([5, 4]); ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(pad.l, ty); ctx.lineTo(w - pad.r, ty); ctx.stroke();
    ctx.setLineDash([]);
    ctx.fillStyle = 'rgba(255,255,255,0.55)'; ctx.textAlign = 'left';
    ctx.fillText('TARGET ' + fmt(target, 2), pad.l + 4, ty - 7);

    /* polyline */
    ctx.beginPath();
    data.forEach((v, i) => {
      const x = pad.l + i * bw + bw / 2, y = pad.t + ih * (1 - v / maxV);
      i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    });
    ctx.strokeStyle = 'rgba(255,255,255,0.30)'; ctx.lineWidth = 1.2; ctx.stroke();
    data.forEach((v, i) => {
      const x = pad.l + i * bw + bw / 2, y = pad.t + ih * (1 - v / maxV);
      ctx.beginPath(); ctx.arc(x, y, i === hoverI ? 3.4 : 1.7, 0, Math.PI * 2);
      ctx.fillStyle = i === hoverI ? '#fff' : 'rgba(255,255,255,0.4)'; ctx.fill();
    });

    ctx.fillStyle = 'rgba(255,255,255,0.28)'; ctx.textAlign = 'center';
    ctx.fillText('f-19', pad.l + 4, hh - 8);
    ctx.fillText('FRAME WINDOW', pad.l + iw / 2, hh - 8);
    ctx.fillText('now', w - pad.r - 6, hh - 8);
  }

  const resetHero = () => { hoverI = -1; draw(); hero.set({ num: fmt(cur(), 2), unit, badge: cur() < target ? 'WITHIN BUDGET' : 'OVER BUDGET', sub: label + ' · target < ' + fmt(target, 2) + ' ' + unit + ' over 20 frames' }); };
  bindDrag(cv.c, {
    onHover(p) {
      const w = cv.w(), pad = { l: 30, r: 12 };
      const iw = w - pad.l - pad.r;
      const data = cfg.key ? series : S.sun.rt;
      hoverI = clamp(Math.floor((p.px - pad.l) / (iw / data.length)), 0, data.length - 1);
      const v = data[hoverI];
      hero.set({ num: fmt(v, 2), unit, badge: v > target ? 'OVER BUDGET' : 'WITHIN BUDGET',
                 sub: 'Frame f-' + (data.length - 1 - hoverI) + ' · ' + label });
      draw();
    },
    onLeave: resetHero
  });

  if (!cfg.key) {
    labelled(api.body, 'Ray Traced Shadows');
    switchRow(api.body, { label: 'Inline RT Shadow Rays', value: true });
    switchRow(api.body, { label: 'SVGF Spatio-Temporal Denoiser', value: true, onChange: v => { S.sun.rtTarget = v ? 0.60 : 0.95; } });
    slider(api.body, { label: 'Rays Per Pixel', min: 0.25, max: 4, step: 0.25, value: 1, unit: 'rpp', dp: 2 });
  } else {
    kvStrip(api.body, [['BUDGET', '< ' + fmt(target, 2) + ' ' + unit], ['PEAK', fmt(Math.max(...series), 2) + ' ' + unit], ['MEAN', fmt(series.reduce((a, b) => a + b, 0) / series.length, 2) + ' ' + unit], ['VARIANCE', fmt(Math.sqrt(series.reduce((a, b) => a + (b - series.reduce((x, y) => x + y, 0) / series.length) ** 2, 0) / series.length), 3)]]);
  }
  draw();
};

/* ============================================================
   7 · DENSITY COLUMN — vertical atmosphere slice w/ drift
   ============================================================ */
WIDGETS.densityColumn = function (api) {
  const st = ns0('sky');
  const hero = api.hero({ num: fmt(st.haze, 2), unit: 'HAZE', badge: 'RAYLEIGH + MIE', sub: 'Exponential density vs altitude · 0 → 80 km' });
  const plate = h('div', 'plate plate--col');
  api.body.appendChild(plate);
  const cv = mkCanvas(plate, 232);
  const LAYERS = [
    { n: 'Mesosphere', km: 80, c: 'rgba(30,26,66,0.85)' },
    { n: 'Stratosphere', km: 50, c: 'rgba(38,52,110,0.8)' },
    { n: 'Upper Tropo.', km: 12, c: 'rgba(56,96,158,0.72)' },
    { n: 'Boundary Layer', km: 2, c: 'rgba(96,150,206,0.62)' },
    { n: 'Ground', km: 0, c: 'rgba(150,196,236,0.5)' }
  ];
  const parts = Array.from({ length: 150 }, (_, i) => ({
    x: Math.random(), y: Math.random(), s: rnd(0.4, 1.5), v: rnd(0.02, 0.16), a: rnd(0.15, 0.85)
  }));
  let t = 0;

  function draw(dt) {
    t += dt;
    const { ctx } = cv, w = cv.w(), hh = cv.h();
    ctx.clearRect(0, 0, w, hh);
    const pad = { l: 62, r: 12, t: 12, b: 16 };
    const iw = w - pad.l - pad.r, ih = hh - pad.t - pad.b;

    /* column background = density integral */
    for (let i = 0; i < ih; i++) {
      const alt = (1 - i / ih) * 80;
      const dens = Math.exp(-alt / (8.5 / (0.4 + st.haze)));
      ctx.fillStyle = 'rgba(90,150,220,' + (dens * 0.5).toFixed(4) + ')';
      ctx.fillRect(pad.l, pad.t + i, iw, 1.2);
    }

    /* layer separators */
    ctx.font = '500 8.5px ui-monospace, SFMono-Regular, Menlo, monospace';
    ctx.textBaseline = 'middle';
    LAYERS.forEach(L => {
      const y = pad.t + ih * (1 - L.km / 80);
      ctx.strokeStyle = 'rgba(255,255,255,0.10)';
      ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(w - pad.r, y); ctx.stroke();
      ctx.fillStyle = 'rgba(255,255,255,0.42)'; ctx.textAlign = 'right';
      ctx.fillText(L.n.toUpperCase(), pad.l - 8, y - 6);
      ctx.fillStyle = 'rgba(255,255,255,0.24)';
      ctx.fillText(L.km + ' km', pad.l - 8, y + 6);
    });

    /* drifting particulates */
    parts.forEach(p => {
      p.y -= p.v * dt * (0.4 + st.haze);
      p.x += Math.sin(t * 0.7 + p.y * 9) * dt * 0.02;
      if (p.y < 0) { p.y = 1; p.x = Math.random(); }
      if (p.x > 1) p.x -= 1; if (p.x < 0) p.x += 1;
      const x = pad.l + p.x * iw, y = pad.t + (1 - p.y) * ih;
      const alt = p.y * 80;
      const dens = Math.exp(-alt / (8.5 / (0.4 + st.haze)));
      ctx.beginPath(); ctx.arc(x, y, p.s * (0.7 + dens), 0, Math.PI * 2);
      ctx.fillStyle = 'rgba(255,255,255,' + (p.a * clamp(dens * 1.4, 0.06, 0.9)).toFixed(3) + ')';
      ctx.fill();
    });

    /* density curve overlay */
    ctx.beginPath();
    for (let i = 0; i <= ih; i += 2) {
      const alt = (1 - i / ih) * 80;
      const dens = Math.exp(-alt / (8.5 / (0.4 + st.haze)));
      const x = pad.l + iw * clamp(dens, 0, 1);
      i ? ctx.lineTo(x, pad.t + i) : ctx.moveTo(x, pad.t + i);
    }
    ctx.strokeStyle = 'rgba(139,124,246,0.9)'; ctx.lineWidth = 1.6; ctx.stroke();
    ctx.lineTo(pad.l, pad.t + ih); ctx.closePath();
    ctx.fillStyle = 'rgba(139,124,246,0.14)'; ctx.fill();
    ctx.lineWidth = 1;

    /* frame */
    ctx.strokeStyle = 'rgba(255,255,255,0.14)';
    ctx.strokeRect(pad.l, pad.t, iw, ih);
  }
  addLoop(draw);
  draw(0);

  slider(api.body, {
    label: 'Atmospheric Haze', min: 0, max: 1, step: 0.01, value: st.haze, unit: '', dp: 2,
    onChange: v => {
      st.haze = v;
      hero.set({ num: fmt(v, 2), unit: 'HAZE', badge: 'RAYLEIGH + MIE', sub: 'Exponential density vs altitude · 0 → 80 km' });
    }
  });
  slider(api.body, { label: 'Rayleigh Scattering', min: 0, max: 40, step: 0.1, value: 21.4, unit: '', dp: 1 });
  slider(api.body, { label: 'Mie Scattering', min: 0, max: 1.5, step: 0.01, value: 0.42, unit: '', dp: 2 });
  slider(api.body, { label: 'Ozone Absorption', min: 0, max: 2, step: 0.01, value: 0.65, unit: '', dp: 2 });
};
function ns0(id) { S[id] = S[id] || {}; return S[id]; }

/* ============================================================
   8 · WAVELENGTH CURVES — spectral response of scatter terms
   ============================================================ */
WIDGETS.wavelengthCurves = function (api) {
  const st = ns0('sky');
  st.rayleigh = st.rayleigh == null ? 1.00 : st.rayleigh;
  st.mie = st.mie == null ? 0.32 : st.mie;
  st.absorb = st.absorb == null ? 0.55 : st.absorb;

  const hero = api.hero({ num: fmt(st.rayleigh, 2), unit: 'βR', badge: 'λ⁻⁴', sub: 'Spectral extinction 380 → 780 nm · normalised response' });
  const plate = h('div', 'plate plate--graph');
  api.body.appendChild(plate);
  const cv = mkCanvas(plate, 158);
  let hoverNm = -1;

  function resp(nm, k) { return Math.pow(550 / nm, k); }

  function draw() {
    const { ctx } = cv, w = cv.w(), hh = cv.h();
    ctx.clearRect(0, 0, w, hh);
    const pad = { l: 32, r: 12, t: 14, b: 22 };
    const iw = w - pad.l - pad.r, ih = hh - pad.t - pad.b;

    /* visible spectrum strip along the bottom */
    for (let i = 0; i < iw; i++) {
      const nm = 380 + (i / iw) * 400;
      ctx.fillStyle = rgbCss(kelvinToRGB(nmToKelvin(nm)));
      ctx.globalAlpha = 0.55;
      ctx.fillRect(pad.l + i, hh - pad.b + 8, 1.2, 6);
    }
    ctx.globalAlpha = 1;

    ctx.font = '500 8.5px ui-monospace, SFMono-Regular, Menlo, monospace';
    ctx.fillStyle = 'rgba(255,255,255,0.28)'; ctx.textAlign = 'center';
    ['380', '480', '580', '680', '780'].forEach((t, i) => ctx.fillText(t, pad.l + iw * i / 4, hh - 4));

    const curves = [
      { c: '#ff6b8a', k: 4 * st.rayleigh, n: 'Rayleigh' },
      { c: '#4cc9f0', k: 1.3 * st.mie + 0.2, n: 'Mie' },
      { c: '#a3e635', k: -2.2 * st.absorb, n: 'Ozone' }
    ];
    for (let g = 0; g <= 3; g++) {
      const y = pad.t + ih * g / 3;
      ctx.strokeStyle = 'rgba(255,255,255,0.05)';
      ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(w - pad.r, y); ctx.stroke();
    }
    curves.forEach(C => {
      ctx.beginPath();
      for (let i = 0; i <= iw; i++) {
        const nm = 380 + (i / iw) * 400;
        const v = clamp(resp(nm, C.k), 0, 1);
        const y = pad.t + ih * (1 - v);
        i ? ctx.lineTo(pad.l + i, y) : ctx.moveTo(pad.l + i, y);
      }
      ctx.strokeStyle = C.c; ctx.lineWidth = 1.7; ctx.stroke();
      ctx.lineTo(pad.l + iw, pad.t + ih); ctx.lineTo(pad.l, pad.t + ih); ctx.closePath();
      ctx.globalAlpha = 0.09; ctx.fillStyle = C.c; ctx.fill(); ctx.globalAlpha = 1;
      ctx.lineWidth = 1;
    });

    if (hoverNm >= 0) {
      const x = pad.l + iw * ((hoverNm - 380) / 400);
      ctx.strokeStyle = 'rgba(255,255,255,0.4)'; ctx.setLineDash([3, 3]);
      ctx.beginPath(); ctx.moveTo(x, pad.t); ctx.lineTo(x, pad.t + ih); ctx.stroke(); ctx.setLineDash([]);
      ctx.fillStyle = '#fff'; ctx.textAlign = 'left';
      ctx.fillText(hoverNm + ' nm', pad.l + 4, pad.t + 8);
    }
    ctx.strokeStyle = 'rgba(255,255,255,0.12)';
    ctx.strokeRect(pad.l, pad.t, iw, ih);
  }
  function nmToKelvin(nm) { return clamp(1e7 / nm, 1500, 12000); }

  const resetWl = () => { hoverNm = -1; draw(); hero.set({ num: fmt(st.rayleigh, 2), unit: 'βR', badge: 'λ⁻⁴', sub: 'Spectral extinction 380 → 780 nm · normalised response' }); };
  bindDrag(cv.c, {
    onHover(p) {
      const w = cv.w(), pad = { l: 32, r: 12 };
      const iw = w - pad.l - pad.r;
      hoverNm = Math.round(clamp(380 + ((p.px - pad.l) / iw) * 400, 380, 780));
      const v = resp(hoverNm, 4 * st.rayleigh);
      hero.set({ num: String(hoverNm), unit: 'nm', badge: 'β ' + fmt(v, 3), sub: 'Rayleigh response ' + fmt(v * 100, 1) + '% of 550 nm reference' });
      draw();
    },
    onLeave: resetWl
  });

  const sR = slider(api.body, { label: 'Rayleigh Scale', min: 0, max: 3, step: 0.01, value: st.rayleigh, dp: 2, onChange: v => { st.rayleigh = v; hero.set({ num: fmt(v, 2), unit: 'βR', badge: 'λ⁻⁴', sub: 'Spectral extinction 380 → 780 nm · normalised response' }); draw(); } });
  slider(api.body, { label: 'Mie Scale', min: 0, max: 2, step: 0.01, value: st.mie, dp: 2, onChange: () => draw() });
  slider(api.body, { label: 'Ozone Absorption', min: 0, max: 2, step: 0.01, value: st.absorb, dp: 2, onChange: () => draw() });
  draw();
};

/* ============================================================
   9 · SKY GRADIENT — zenith→horizon ramp (kept distinct from column)
   ============================================================ */
WIDGETS.skyGradient = function (api) {
  const st = ns0('sky');
  st.zenith = st.zenith == null ? 0.18 : st.zenith;
  st.horizon = st.horizon == null ? 0.62 : st.horizon;

  const hero = api.hero({ num: fmt(st.horizon * 100, 0), unit: '% HORIZON', badge: 'MULTI-SCATTER', sub: 'Vertical luminance ramp · zenith → horizon → ground' });
  const plate = h('div', 'plate plate--sky');
  api.body.appendChild(plate);
  const cv = mkCanvas(plate, 168);
  let hoverY = -1;

  function skyAt(t) {
    /* t: 0 = zenith, 1 = horizon */
    const zen = [16, 42, 96], hor = [168, 196, 226], gnd = [26, 28, 34];
    if (t <= 1) return zen.map((v, i) => Math.round(lerp(v, hor[i], Math.pow(t, 1 / (0.6 + st.zenith * 2)))));
    const u = clamp((t - 1) / 0.35, 0, 1);
    return hor.map((v, i) => Math.round(lerp(v, gnd[i], u)));
  }

  function draw() {
    const { ctx } = cv, w = cv.w(), hh = cv.h();
    ctx.clearRect(0, 0, w, hh);
    const pad = 12;
    for (let i = 0; i < hh - pad * 2; i++) {
      const t = (i / (hh - pad * 2)) * 1.35;
      const c = skyAt(t);
      ctx.fillStyle = 'rgb(' + c.join(',') + ')';
      ctx.fillRect(pad, pad + i, w - pad * 2, 1.2);
    }
    /* sun disc marker follows S.sun.el */
    const el = S.sun ? S.sun.el : 45;
    const sy = pad + (hh - pad * 2) * clamp(1 - (el + 10) / 100, 0, 1);
    const sx = w * 0.62;
    const g = ctx.createRadialGradient(sx, sy, 0, sx, sy, 40);
    g.addColorStop(0, 'rgba(255,220,160,0.9)'); g.addColorStop(0.25, 'rgba(255,180,90,0.30)'); g.addColorStop(1, 'rgba(255,180,90,0)');
    ctx.fillStyle = g; ctx.beginPath(); ctx.arc(sx, sy, 40, 0, Math.PI * 2); ctx.fill();
    ctx.beginPath(); ctx.arc(sx, sy, 5, 0, Math.PI * 2); ctx.fillStyle = '#fff6e2'; ctx.fill();

    if (hoverY >= 0) {
      ctx.strokeStyle = 'rgba(255,255,255,0.55)'; ctx.setLineDash([4, 3]);
      ctx.beginPath(); ctx.moveTo(pad, hoverY); ctx.lineTo(w - pad, hoverY); ctx.stroke(); ctx.setLineDash([]);
    }
    ctx.strokeStyle = 'rgba(255,255,255,0.16)';
    ctx.strokeRect(pad, pad, w - pad * 2, hh - pad * 2);
    ctx.font = '500 8.5px ui-monospace, SFMono-Regular, Menlo, monospace';
    ctx.fillStyle = 'rgba(255,255,255,0.5)'; ctx.textAlign = 'left';
    ctx.fillText('ZENITH', pad + 6, pad + 12);
    ctx.fillText('HORIZON', pad + 6, hh - pad - 22);
    ctx.fillText('GROUND', pad + 6, hh - pad - 6);
  }

  const resetSky = () => { hoverY = -1; draw(); hero.set({ num: fmt(st.horizon * 100, 0), unit: '% HORIZON', badge: 'MULTI-SCATTER', sub: 'Vertical luminance ramp · zenith → horizon → ground' }); };
  bindDrag(cv.c, {
    onHover(p) {
      const hh = cv.h(), pad = 12;
      hoverY = clamp(p.py, pad, hh - pad);
      const t = (hoverY - pad) / (hh - pad * 2) * 1.35;
      const c = skyAt(t);
      const deg = 90 - t / 1.35 * 130;
      hero.set({ num: fmt(deg, 1), unit: '° ALT', badge: '#' + c.map(v => ('0' + v.toString(16)).slice(-2)).join('').toUpperCase(),
                 sub: 'rgb(' + c.join(', ') + ') at ' + fmt(deg, 1) + '° altitude' });
      draw();
    },
    onLeave: resetSky
  });

  slider(api.body, { label: 'Zenith Compression', min: 0, max: 1, step: 0.01, value: st.zenith, dp: 2, onChange: v => { st.zenith = v; draw(); } });
  slider(api.body, { label: 'Horizon Luminance', min: 0, max: 1, step: 0.01, value: st.horizon, dp: 2,
    onChange: v => { st.horizon = v; hero.set({ num: fmt(v * 100, 0), unit: '% HORIZON', badge: 'MULTI-SCATTER', sub: 'Vertical luminance ramp · zenith → horizon → ground' }); draw(); } });
  switchRow(api.body, { label: 'Multi-Scatter Approximation', value: true, onChange: draw });
  switchRow(api.body, { label: 'Apply Aerial Perspective', value: true });
  draw();
  registerDep(() => draw());
};

/* ============================================================
   10 · FALLOFF CURVE — attenuation / density decay
   ============================================================ */
WIDGETS.falloffCurve = function (api) {
  const cfg = api.cfg;
  const st = ns0(cfg.key.split('.')[0]);
  const kk = cfg.key.split('.')[1];
  st[kk + '_r'] = st[kk + '_r'] == null ? cfg.a : st[kk + '_r'];
  st[kk + '_p'] = st[kk + '_p'] == null ? 2.0 : st[kk + '_p'];

  const radius = () => st[kk + '_r'];
  const badge = () => cfg.curve === 'exp' ? 'EXPONENTIAL' : 'INVERSE SQUARE';
  const hero = api.hero({ num: fmt(radius(), 0), unit: cfg.xUnit, badge: badge(), sub: cfg.xLabel + ' · exponent ' + fmt(st[kk + '_p'], 2) });
  const plate = h('div', 'plate plate--graph');
  api.body.appendChild(plate);
  const cv = mkCanvas(plate, 150);
  let dragR = false;

  function f(x) {
    const r = radius();
    const u = clamp(x / cfg.xMax, 0, 1);
    const norm = clamp(x / Math.max(1, r), 0, 1);
    const v = cfg.curve === 'exp'
      ? Math.exp(-norm * st[kk + '_p'] * 2.4)
      : 1 / Math.pow(1 + (x / Math.max(1, r * 0.35)) * st[kk + '_p'], 2);
    return clamp(v, 0, 1);
  }

  function draw() {
    const { ctx } = cv, w = cv.w(), hh = cv.h();
    ctx.clearRect(0, 0, w, hh);
    const pad = { l: 34, r: 14, t: 14, b: 22 };
    const iw = w - pad.l - pad.r, ih = hh - pad.t - pad.b;

    const yMax = cfg.yMax != null ? cfg.yMax : cfg.a;
    const yDp = cfg.yDp != null ? cfg.yDp : (yMax < 10 ? 2 : 0);
    for (let i = 0; i <= 4; i++) {
      const y = pad.t + ih * i / 4;
      ctx.strokeStyle = 'rgba(255,255,255,0.05)';
      ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(w - pad.r, y); ctx.stroke();
      ctx.fillStyle = 'rgba(255,255,255,0.28)'; ctx.textAlign = 'right';
      ctx.font = '500 8.5px ui-monospace, SFMono-Regular, Menlo, monospace';
      ctx.fillText(fmt((1 - i / 4) * yMax, yDp), pad.l - 6, y + 3);
    }

    /* filled area */
    ctx.beginPath();
    ctx.moveTo(pad.l, pad.t + ih);
    for (let i = 0; i <= iw; i++) {
      const x = (i / iw) * cfg.xMax;
      ctx.lineTo(pad.l + i, pad.t + ih * (1 - f(x)));
    }
    ctx.lineTo(pad.l + iw, pad.t + ih); ctx.closePath();
    const g = ctx.createLinearGradient(0, pad.t, 0, pad.t + ih);
    g.addColorStop(0, 'rgba(255,255,255,0.30)'); g.addColorStop(1, 'rgba(255,255,255,0.02)');
    ctx.fillStyle = g; ctx.fill();

    /* curve */
    ctx.beginPath();
    for (let i = 0; i <= iw; i++) {
      const x = (i / iw) * cfg.xMax;
      const y = pad.t + ih * (1 - f(x));
      i ? ctx.lineTo(pad.l + i, y) : ctx.moveTo(pad.l + i, y);
    }
    ctx.strokeStyle = api.card.accent; ctx.lineWidth = 1.9; ctx.stroke(); ctx.lineWidth = 1;

    /* radius marker (draggable) */
    const rx = pad.l + iw * clamp(radius() / cfg.xMax, 0, 1);
    ctx.strokeStyle = 'rgba(255,255,255,0.45)'; ctx.setLineDash([4, 3]);
    ctx.beginPath(); ctx.moveTo(rx, pad.t); ctx.lineTo(rx, pad.t + ih); ctx.stroke(); ctx.setLineDash([]);
    ctx.beginPath(); ctx.arc(rx, pad.t + ih * (1 - f(radius())), dragR ? 6 : 4.5, 0, Math.PI * 2);
    ctx.fillStyle = api.card.accent; ctx.shadowColor = api.card.accent; ctx.shadowBlur = 12; ctx.fill(); ctx.shadowBlur = 0;
    ctx.beginPath(); ctx.arc(rx, pad.t + ih * (1 - f(radius())), 1.8, 0, Math.PI * 2); ctx.fillStyle = '#0b0d12'; ctx.fill();

    ctx.fillStyle = 'rgba(255,255,255,0.3)'; ctx.textAlign = 'center';
    ctx.fillText(cfg.xLabel.toUpperCase(), pad.l + iw / 2, hh - 6);
    ctx.textAlign = 'left'; ctx.fillText(cfg.yLabel.toUpperCase(), 4, pad.t - 4);
    ctx.strokeStyle = 'rgba(255,255,255,0.12)';
    ctx.strokeRect(pad.l, pad.t, iw, ih);
  }

  bindDrag(cv.c, {
    onStart: () => { dragR = true; },
    onMove(p) {
      const w = cv.w(), pad = { l: 34, r: 14 };
      const iw = w - pad.l - pad.r;
      const v = clamp((p.px - pad.l) / iw, 0, 1) * cfg.xMax;
      st[kk + '_r'] = Math.round(v);
      hero.set({ num: fmt(radius(), 0), unit: cfg.xUnit, badge: badge(),
                 sub: cfg.xLabel + ' · exponent ' + fmt(st[kk + '_p'], 2) });
      draw();
    },
    onEnd() { dragR = false; draw(); tick(); }
  });

  slider(api.body, {
    label: cfg.curve === 'exp' ? 'Falloff Exponent' : 'Distance Exponent',
    min: 0.5, max: 6, step: 0.05, value: st[kk + '_p'], dp: 2,
    onChange: v => { st[kk + '_p'] = v; draw(); }
  });
  draw();
};

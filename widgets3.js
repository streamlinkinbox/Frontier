/* ============================================================
   Frontier Engine — Widget Group C
   (Lights · Camera · Actors · Post)
   ============================================================ */

/* ============================================================
   FLICKER TRACE — live procedural emitter intensity scope
   ============================================================ */
WIDGETS.flickerTrace = function (api) {
  const st = ns0('campfire');
  st.freq = st.freq == null ? 3.2 : st.freq;
  st.chaos = st.chaos == null ? 0.58 : st.chaos;
  st.base = st.base == null ? 2400 : st.base;
  let live = true;

  const hero = api.hero({ num: fmt(st.base, 0), unit: 'lm', badge: 'LIVE', sub: 'Procedural noise-driven luminous flux · 240 samples/s' });
  const plate = h('div', 'plate plate--graph');
  api.body.appendChild(plate);
  const cv = mkCanvas(plate, 150);

  const N = 160;
  const buf = new Array(N).fill(0.5);
  let t = 0, peak = 0;

  function sample(x) {
    const a = Math.sin(x * st.freq) * 0.32;
    const b = Math.sin(x * st.freq * 2.7 + 1.3) * 0.20 * st.chaos;
    const c = (hash01(Math.floor(x * 9)) - 0.5) * 0.55 * st.chaos;
    const d = Math.sin(x * 0.6) * 0.10;
    return clamp(0.62 + a + b + c + d, 0.05, 1);
  }

  function draw(dt) {
    if (live) {
      t += dt;
      buf.push(sample(t)); buf.shift();
    }
    const { ctx } = cv, w = cv.w(), hh = cv.h();
    ctx.clearRect(0, 0, w, hh);
    const pad = { l: 34, r: 12, t: 14, b: 20 }, iw = w - pad.l - pad.r, ih = hh - pad.t - pad.b;

    for (let i = 0; i <= 4; i++) {
      const y = pad.t + ih * i / 4;
      ctx.strokeStyle = 'rgba(255,255,255,0.05)';
      ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(w - pad.r, y); ctx.stroke();
      ctx.fillStyle = 'rgba(255,255,255,0.28)'; ctx.textAlign = 'right';
      ctx.font = '500 8.5px ui-monospace, SFMono-Regular, Menlo, monospace';
      ctx.fillText(fmt(st.base * 2 * (1 - i / 4), 0), pad.l - 6, y + 3);
    }

    ctx.beginPath(); ctx.moveTo(pad.l, pad.t + ih);
    buf.forEach((v, i) => ctx.lineTo(pad.l + iw * i / (N - 1), pad.t + ih * (1 - v / 2)));
    ctx.lineTo(pad.l + iw, pad.t + ih); ctx.closePath();
    const g = ctx.createLinearGradient(0, pad.t, 0, pad.t + ih);
    g.addColorStop(0, 'rgba(251,146,60,0.55)'); g.addColorStop(1, 'rgba(251,146,60,0.03)');
    ctx.fillStyle = g; ctx.fill();

    ctx.beginPath();
    buf.forEach((v, i) => {
      const x = pad.l + iw * i / (N - 1), y = pad.t + ih * (1 - v / 2);
      i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    });
    ctx.strokeStyle = '#fb923c'; ctx.lineWidth = 1.8; ctx.stroke(); ctx.lineWidth = 1;

    const last = buf[N - 1];
    const lx = pad.l + iw, ly = pad.t + ih * (1 - last / 2);
    ctx.beginPath(); ctx.arc(lx, ly, 4.2, 0, Math.PI * 2);
    ctx.fillStyle = '#ffd7a8'; ctx.shadowColor = '#fb923c'; ctx.shadowBlur = 14; ctx.fill(); ctx.shadowBlur = 0;

    peak = Math.max(peak * 0.995, last);
    ctx.strokeStyle = 'rgba(255,255,255,0.35)'; ctx.setLineDash([4, 3]);
    const py = pad.t + ih * (1 - peak / 2);
    ctx.beginPath(); ctx.moveTo(pad.l, py); ctx.lineTo(w - pad.r, py); ctx.stroke(); ctx.setLineDash([]);
    ctx.fillStyle = 'rgba(255,255,255,0.45)'; ctx.textAlign = 'left';
    ctx.fillText('PEAK ' + fmt(st.base * 2 * peak, 0) + ' lm', pad.l + 4, py - 6);

    ctx.strokeStyle = 'rgba(255,255,255,0.12)'; ctx.strokeRect(pad.l, pad.t, iw, ih);
    ctx.fillStyle = 'rgba(255,255,255,0.28)'; ctx.textAlign = 'center';
    ctx.fillText('t − 4.0 s                                    now', pad.l + iw / 2, hh - 5);

    hero.set({ num: fmt(st.base * 2 * last, 0), unit: 'lm', badge: live ? 'LIVE' : 'PAUSED',
               sub: 'Mean ' + fmt(st.base, 0) + ' lm · chaos ' + fmt(st.chaos, 2) + ' · ' + fmt(st.freq, 1) + ' Hz' });
  }
  addLoop(draw); draw(0);

  const ctl = h('div', 'chips');
  const pb = h('button', 'chip is-on', icon('pause', 11) + ' Freeze Scope');
  pb.addEventListener('click', () => {
    live = !live;
    pb.classList.toggle('is-on', live);
    pb.innerHTML = icon(live ? 'pause' : 'play', 11) + ' ' + (live ? 'Freeze Scope' : 'Resume Scope');
    click(live ? 1700 : 800);
  });
  ctl.appendChild(pb);
  api.body.appendChild(ctl);

  slider(api.body, { label: 'Base Luminous Flux', min: 200, max: 8000, step: 50, value: st.base, unit: 'lm', dp: 0, onChange: v => { st.base = v; peak = 0; } });
  slider(api.body, { label: 'Flicker Frequency', min: 0.2, max: 12, step: 0.1, value: st.freq, unit: 'Hz', dp: 1, onChange: v => st.freq = v });
  slider(api.body, { label: 'Turbulence Chaos', min: 0, max: 1, step: 0.01, value: st.chaos, dp: 2, onChange: v => st.chaos = v });
};

/* ============================================================
   SPOT CONE — top-down beam wedge with inner/outer handles
   ============================================================ */
WIDGETS.spotCone = function (api) {
  const st = ns0('watchtower');
  st.inner = st.inner == null ? 24 : st.inner;
  st.outer = st.outer == null ? 41 : st.outer;
  st.range = st.range == null ? 2400 : st.range;

  const hero = api.hero({ num: fmt(st.outer * 2, 1), unit: '° CONE', badge: 'INNER ' + fmt(st.inner * 2, 1) + '°', sub: 'Top-down beam wedge · drag either edge handle' });
  const plate = h('div', 'plate plate--cone');
  api.body.appendChild(plate);
  const cv = mkCanvas(plate, 190);
  let dragH = null;

  function draw() {
    const { ctx } = cv, w = cv.w(), hh = cv.h();
    ctx.clearRect(0, 0, w, hh);
    const ox = w * 0.14, oy = hh / 2;
    const R = Math.min(w * 0.80, hh * 0.94);

    ctx.strokeStyle = 'rgba(255,255,255,0.05)';
    for (let i = 1; i <= 4; i++) {
      ctx.beginPath(); ctx.arc(ox, oy, R * i / 4, -Math.PI / 2.4, Math.PI / 2.4); ctx.stroke();
      ctx.fillStyle = 'rgba(255,255,255,0.24)'; ctx.textAlign = 'center';
      ctx.font = '500 8px ui-monospace, SFMono-Regular, Menlo, monospace';
      ctx.fillText(fmt(st.range * i / 4, 0) + 'm', ox + R * i / 4, oy + hh * 0.42);
    }

    const wedge = (a0, a1, col) => {
      ctx.beginPath(); ctx.moveTo(ox, oy);
      ctx.arc(ox, oy, R, a0 * Math.PI / 180, a1 * Math.PI / 180);
      ctx.closePath(); ctx.fillStyle = col; ctx.fill();
    };
    wedge(-st.outer, st.outer, 'rgba(245,230,99,0.09)');
    wedge(-st.inner, st.inner, 'rgba(245,230,99,0.22)');

    [-st.outer, st.outer].forEach(a => {
      const p = polar(ox, oy, R, a + 90);
      ctx.beginPath(); ctx.moveTo(ox, oy); ctx.lineTo(p[0], p[1]);
      ctx.strokeStyle = 'rgba(245,230,99,0.75)'; ctx.lineWidth = 1.4; ctx.stroke(); ctx.lineWidth = 1;
      ctx.beginPath(); ctx.arc(p[0], p[1], 5.4, 0, Math.PI * 2);
      ctx.fillStyle = dragH === a ? '#fff' : '#f5e663'; ctx.shadowColor = '#f5e663'; ctx.shadowBlur = 10; ctx.fill(); ctx.shadowBlur = 0;
    });
    [-st.inner, st.inner].forEach(a => {
      const p = polar(ox, oy, R * 0.62, a + 90);
      ctx.beginPath(); ctx.moveTo(ox, oy); ctx.lineTo(p[0], p[1]);
      ctx.strokeStyle = 'rgba(255,255,255,0.35)'; ctx.setLineDash([3, 3]); ctx.stroke(); ctx.setLineDash([]);
    });

    /* emitter */
    ctx.beginPath(); ctx.arc(ox, oy, 7, 0, Math.PI * 2);
    ctx.fillStyle = '#fff8d0'; ctx.shadowColor = '#f5e663'; ctx.shadowBlur = 18; ctx.fill(); ctx.shadowBlur = 0;
    ctx.strokeStyle = 'rgba(255,255,255,0.5)';
    ctx.beginPath(); ctx.arc(ox, oy, 12, 0, Math.PI * 2); ctx.stroke();

    ctx.fillStyle = 'rgba(255,255,255,0.4)'; ctx.textAlign = 'left';
    ctx.font = '500 8.5px ui-monospace, SFMono-Regular, Menlo, monospace';
    ctx.fillText('EMITTER', ox - 12, oy - 22);
  }

  bindDrag(cv.c, {
    onStart(p) {
      const w = cv.w(), hh = cv.h(), ox = w * 0.14, oy = hh / 2, R = Math.min(w * 0.80, hh * 0.94);
      const a = Math.atan2(p.py - oy, p.px - ox) * 180 / Math.PI;
      dragH = (Math.abs(Math.abs(a) - st.outer) < Math.abs(Math.abs(a) - st.inner)) ? 'outer' : 'inner';
    },
    onMove(p) {
      const w = cv.w(), hh = cv.h(), ox = w * 0.14, oy = hh / 2;
      const a = Math.abs(Math.atan2(p.py - oy, p.px - ox) * 180 / Math.PI);
      if (dragH === 'outer') { st.outer = clamp(a, st.inner + 1, 80); }
      else { st.inner = clamp(a, 1, st.outer - 1); }
      hero.set({ num: fmt(st.outer * 2, 1), unit: '° CONE', badge: 'INNER ' + fmt(st.inner * 2, 1) + '°', sub: 'Top-down beam wedge · drag either edge handle' });
      draw();
    },
    onEnd() { dragH = null; draw(); tick(); }
  });
  draw();

  const sR = slider(api.body, { label: 'Attenuation Range', min: 200, max: 6000, step: 20, value: st.range, unit: 'm', dp: 0, onChange: v => { st.range = v; draw(); } });
  slider(api.body, { label: 'Soft Source Radius', min: 0, max: 20, step: 0.1, value: 3.4, unit: 'cm', dp: 1 });
  const rd = api.readouts([]);
  rd.innerHTML =
    '<div class="rgrid__c"><span class="rgrid__k">SOLID ANGLE</span><span class="rgrid__v">' + fmt(2 * Math.PI * (1 - Math.cos(st.outer * Math.PI / 180)), 3) + ' sr</span></div>' +
    '<div class="rgrid__c"><span class="rgrid__k">PENUMBRA</span><span class="rgrid__v">' + fmt((st.outer - st.inner) / st.outer * 100, 1) + '%</span></div>' +
    '<div class="rgrid__c"><span class="rgrid__k">COVERAGE</span><span class="rgrid__v">' + fmt(Math.PI * Math.pow(st.range * Math.tan(st.outer * Math.PI / 180), 2), 0) + ' m²</span></div>';
};

/* ============================================================
   FRUSTUM PAD — top-down camera frustum
   ============================================================ */
WIDGETS.frustumPad = function (api) {
  const st = ns0('camera');
  st.fov = st.fov == null ? 54.4 : st.fov;
  if (st.keys == null) st.keys = [0.06, 0.24, 0.47, 0.68, 0.90];
  st.play = st.play == null ? 0.12 : st.play;

  const focal = () => 35;
  const hero = api.hero({ num: fmt(st.fov, 1), unit: '° HFOV', badge: focal() + ' mm', sub: 'Horizontal field of view · drag the frustum edges' });
  const plate = h('div', 'plate plate--cone');
  api.body.appendChild(plate);
  const cv = mkCanvas(plate, 186);

  function draw() {
    const { ctx } = cv, w = cv.w(), hh = cv.h();
    ctx.clearRect(0, 0, w, hh);
    const ox = w * 0.13, oy = hh / 2, R = Math.min(w * 0.82, hh * 0.96);
    const half = st.fov / 2;

    ctx.strokeStyle = 'rgba(255,255,255,0.05)';
    for (let i = 1; i <= 3; i++) { ctx.beginPath(); ctx.arc(ox, oy, R * i / 3, -1.1, 1.1); ctx.stroke(); }

    ctx.beginPath(); ctx.moveTo(ox, oy);
    ctx.arc(ox, oy, R, -half * Math.PI / 180, half * Math.PI / 180);
    ctx.closePath();
    const g = ctx.createLinearGradient(ox, oy, ox + R, oy);
    g.addColorStop(0, 'rgba(76,201,240,0.30)'); g.addColorStop(1, 'rgba(76,201,240,0.03)');
    ctx.fillStyle = g; ctx.fill();
    ctx.strokeStyle = 'rgba(76,201,240,0.8)'; ctx.lineWidth = 1.4; ctx.stroke(); ctx.lineWidth = 1;

    [-half, half].forEach(a => {
      const p = polar(ox, oy, R, a + 90);
      ctx.beginPath(); ctx.arc(p[0], p[1], 5, 0, Math.PI * 2);
      ctx.fillStyle = '#4cc9f0'; ctx.shadowColor = '#4cc9f0'; ctx.shadowBlur = 12; ctx.fill(); ctx.shadowBlur = 0;
    });

    /* near/far planes */
    [0.22, 1].forEach((f, i) => {
      const a0 = polar(ox, oy, R * f, -half + 90), a1 = polar(ox, oy, R * f, half + 90);
      ctx.beginPath(); ctx.moveTo(a0[0], a0[1]); ctx.lineTo(a1[0], a1[1]);
      ctx.strokeStyle = i ? 'rgba(255,255,255,0.30)' : 'rgba(255,255,255,0.5)';
      ctx.setLineDash(i ? [4, 3] : []); ctx.stroke(); ctx.setLineDash([]);
      ctx.fillStyle = 'rgba(255,255,255,0.4)'; ctx.textAlign = 'center';
      ctx.font = '500 8px ui-monospace, SFMono-Regular, Menlo, monospace';
      ctx.fillText(i ? 'FAR 10 km' : 'NEAR 10 cm', (a0[0] + a1[0]) / 2, (a0[1] + a1[1]) / 2 - 6);
    });

    /* playhead ray */
    const pa = -half + (half * 2) * st.play;
    const pp = polar(ox, oy, R, pa + 90);
    ctx.beginPath(); ctx.moveTo(ox, oy); ctx.lineTo(pp[0], pp[1]);
    ctx.strokeStyle = '#ff6b8a'; ctx.setLineDash([2, 3]); ctx.stroke(); ctx.setLineDash([]);

    ctx.beginPath(); ctx.moveTo(ox - 10, oy); ctx.lineTo(ox + 4, oy - 7); ctx.lineTo(ox + 4, oy + 7); ctx.closePath();
    ctx.fillStyle = '#e6edf5'; ctx.fill();
    ctx.fillStyle = 'rgba(255,255,255,0.45)'; ctx.textAlign = 'left';
    ctx.font = '500 8.5px ui-monospace, SFMono-Regular, Menlo, monospace';
    ctx.fillText('CAMERA', ox - 14, oy + 22);
  }

  bindDrag(cv.c, {
    onMove(p) {
      const w = cv.w(), hh = cv.h(), ox = w * 0.13, oy = hh / 2;
      const a = Math.abs(Math.atan2(p.py - oy, p.px - ox) * 180 / Math.PI);
      st.fov = clamp(a * 2, 8, 140);
      hero.set({ num: fmt(st.fov, 1), unit: '° HFOV', badge: fmt(24 / (2 * Math.tan(st.fov / 2 * Math.PI / 180)), 1) + ' mm EQ', sub: 'Horizontal field of view · drag the frustum edges' });
      draw();
    }, onEnd: () => tick()
  });
  draw();

  slider(api.body, { label: 'Horizontal FOV', min: 8, max: 140, step: 0.1, value: st.fov, unit: '°', dp: 1, onChange: v => { st.fov = v; draw(); hero.set({ num: fmt(v, 1), unit: '° HFOV', badge: fmt(24 / (2 * Math.tan(v / 2 * Math.PI / 180)), 1) + ' mm EQ', sub: 'Horizontal field of view · drag the frustum edges' }); } });
  const rd = api.readouts([]);
  rd.innerHTML =
    '<div class="rgrid__c"><span class="rgrid__k">VFOV</span><span class="rgrid__v">' + fmt(2 * Math.atan(Math.tan(st.fov / 2 * Math.PI / 180) * 9 / 16) * 180 / Math.PI, 1) + '°</span></div>' +
    '<div class="rgrid__c"><span class="rgrid__k">ASPECT</span><span class="rgrid__v">16:9</span></div>' +
    '<div class="rgrid__c"><span class="rgrid__k">SENSOR</span><span class="rgrid__v">24×13.5</span></div>' +
    '<div class="rgrid__c"><span class="rgrid__k">SAFE MARGIN</span><span class="rgrid__v">90%</span></div>';
};

/* ============================================================
   IRIS APERTURE — animated blade diaphragm
   ============================================================ */
WIDGETS.irisAperture = function (api) {
  const st = ns0('camera');
  st.fstop = st.fstop == null ? 2.8 : st.fstop;
  st.blades = st.blades == null ? 9 : st.blades;

  const hero = api.hero({ num: 'ƒ/' + fmt(st.fstop, 1), unit: '', badge: st.blades + ' BLADES', sub: 'Diaphragm opening · drag the ring to stop down' });
  const wrap = h('div', 'iris');
  wrap.innerHTML = '<svg viewBox="0 0 160 160" class="iris__svg">' +
    '<circle class="iris__bg" cx="80" cy="80" r="66"/>' +
    '<g class="iris__bl"></g>' +
    '<circle class="iris__ring" cx="80" cy="80" r="66"/>' +
    '<circle class="iris__glow" cx="80" cy="80" r="30"/>' +
    '</svg><div class="iris__mid"><b>ƒ/' + fmt(st.fstop, 1) + '</b><i>APERTURE</i></div>';
  api.body.appendChild(wrap);
  const g = wrap.querySelector('.iris__bl');
  const mid = wrap.querySelector('.iris__mid b');

  function openT() { return clamp(inv(st.fstop, 22, 1.2), 0, 1); }

  function paint() {
    const t = openT();
    const R = 54, r = 9 + t * 43;
    let out = '';
    for (let i = 0; i < st.blades; i++) {
      const a0 = (360 / st.blades) * i;
      const a1 = a0 + 360 / st.blades + 26;
      const p0 = polar(80, 80, R + 14, a0), p1 = polar(80, 80, R + 14, a1);
      const q0 = polar(80, 80, r, a0 + 12), q1 = polar(80, 80, r, a1 - 6);
      out += '<path d="M ' + p0[0].toFixed(1) + ' ' + p0[1].toFixed(1) +
             ' A ' + (R + 14) + ' ' + (R + 14) + ' 0 0 1 ' + p1[0].toFixed(1) + ' ' + p1[1].toFixed(1) +
             ' L ' + q1[0].toFixed(1) + ' ' + q1[1].toFixed(1) +
             ' L ' + q0[0].toFixed(1) + ' ' + q0[1].toFixed(1) + ' Z" fill="rgba(18,20,27,0.94)" stroke="rgba(255,255,255,0.10)"/>';
    }
    g.innerHTML = out;
    const glow = wrap.querySelector('.iris__glow');
    if (glow) { glow.setAttribute('r', (r * 0.92).toFixed(1)); glow.style.opacity = (0.18 + t * 0.5).toFixed(2); }
    mid.textContent = 'ƒ/' + fmt(st.fstop, 1);
    const ev = fmt(Math.log2((st.fstop * st.fstop) / (2.8 * 2.8)), 2);
    hero.set({ num: 'ƒ/' + fmt(st.fstop, 1), unit: '', badge: st.blades + ' BLADES · ' + (ev >= 0 ? '+' : '') + ev + ' EV',
               sub: 'Diaphragm opening ' + fmt(t * 100, 0) + '% · ' + fmt(Math.PI * r * r / 100, 2) + ' cm² effective' });
  }

  bindDrag(wrap, {
    onMove(p) {
      const t = clamp(1 - p.y, 0, 1);
      st.fstop = Math.round(lerp(22, 1.2, t) * 10) / 10;
      paint();
    }, onEnd: () => tick()
  });

  slider(api.body, { label: 'Aperture (f-stop)', min: 1.2, max: 22, step: 0.1, value: st.fstop, unit: 'ƒ', dp: 1, onChange: v => { st.fstop = v; paint(); } });
  chipRow(api.body, [{ label: '5', v: 5 }, { label: '6', v: 6 }, { label: '7', v: 7 }, { label: '9', v: 9 }, { label: '11', v: 11 }],
    it => { st.blades = it.v; paint(); thunk(); }, 3);
  labelled(api.body, 'Blade Count');
  paint();
};

/* ============================================================
   KEYFRAME TRACK — sequence timeline with keys
   ============================================================ */
WIDGETS.keyframeTrack = function (api) {
  const st = ns0('camera');
  if (st.keys == null) st.keys = [0.06, 0.24, 0.47, 0.68, 0.90];
  st.play = st.play == null ? 0.12 : st.play;
  st.dur = st.dur == null ? 12 : st.dur;
  st.running = false;

  const hero = api.hero({ num: fmt(st.play * st.dur, 2), unit: 's', badge: st.keys.length + ' KEYS', sub: 'Double-click the track to drop a key · drag the playhead' });
  const track = h('div', 'ktrack');
  track.innerHTML = '<div class="ktrack__rail"></div><div class="ktrack__ph"><span></span></div>';
  api.body.appendChild(track);
  const rail = track.querySelector('.ktrack__rail');
  const ph = track.querySelector('.ktrack__ph');

  function paintKeys() {
    rail.innerHTML = st.keys.map((k, i) =>
      '<button class="key" style="left:' + (k * 100) + '%" data-i="' + i + '" title="Key ' + (i + 1) + ' @ ' + fmt(k * st.dur, 2) + 's"><span></span></button>'
    ).join('');
    rail.querySelectorAll('.key').forEach(b => {
      b.addEventListener('pointerdown', ev => ev.stopPropagation());
      b.addEventListener('dblclick', ev => {
        ev.stopPropagation();
        const i = +b.dataset.i; st.keys.splice(i, 1); paintKeys(); thunk();
      });
    });
    hero.set({ num: fmt(st.play * st.dur, 2), unit: 's', badge: st.keys.length + ' KEYS', sub: 'Double-click a key to remove it · drag the playhead' });
  }
  function paint() { ph.style.left = (st.play * 100) + '%'; }

  bindDrag(track, {
    onMove(p) { st.play = clamp(p.x, 0, 1); paint(); },
    onEnd: () => tick()
  });
  track.addEventListener('dblclick', p => {
    const r = track.getBoundingClientRect();
    const v = clamp((p.clientX - r.left) / r.width, 0, 1);
    st.keys.push(v); st.keys.sort((a, b) => a - b); paintKeys(); click(1900, 0.03, 0.012);
  });
  paintKeys(); paint();

  const ctl = h('div', 'chips');
  const pb = h('button', 'chip', icon('play', 11) + ' Scrub');
  pb.addEventListener('click', () => {
    st.running = !st.running;
    pb.classList.toggle('is-on', st.running);
    pb.innerHTML = icon(st.running ? 'pause' : 'play', 11) + ' ' + (st.running ? 'Pause' : 'Scrub');
    thunk();
  });
  ctl.appendChild(pb);
  api.body.appendChild(ctl);

  addLoop(dt => {
    if (!st.running) return;
    st.play = (st.play + dt / st.dur) % 1;
    paint();
    hero.set({ num: fmt(st.play * st.dur, 2), unit: 's', badge: st.keys.length + ' KEYS', sub: 'Double-click a key to remove it · drag the playhead' });
  });

  slider(api.body, { label: 'Sequence Duration', min: 2, max: 60, step: 0.5, value: st.dur, unit: 's', dp: 1, onChange: v => { st.dur = v; paintKeys(); } });
  kvStrip(api.body, [['FPS', '24'], ['IN', fmt(st.keys[0] * st.dur, 2) + 's'], ['OUT', fmt(st.keys[st.keys.length - 1] * st.dur, 2) + 's'], ['INTERP', 'Cubic Auto']]);
};

/* ============================================================
   COLLISION SHELL — mesh silhouette vs hull
   ============================================================ */
WIDGETS.collisionShell = function (api) {
  const st = ns0('citadel');
  st.mode = st.mode == null ? 'hull' : st.mode;
  st.complexity = st.complexity == null ? 12 : st.complexity;

  const hero = api.hero({ num: String(st.complexity), unit: 'VERTS', badge: st.mode.toUpperCase(), sub: 'Convex decomposition overlay · 3 primitives, 0.4 m error' });
  const plate = h('div', 'plate plate--shell');
  api.body.appendChild(plate);
  const cv = mkCanvas(plate, 190);

  const SIL = [[0.16,0.92],[0.16,0.48],[0.26,0.48],[0.26,0.30],[0.34,0.30],[0.34,0.18],[0.42,0.10],[0.50,0.18],[0.50,0.30],[0.58,0.30],[0.58,0.48],[0.68,0.48],[0.68,0.36],[0.80,0.36],[0.80,0.92]];

  function hullFor(n) {
    const pts = SIL.map(p => p);
    const out = [];
    for (let i = 0; i < n; i++) {
      const a = (i / n) * Math.PI * 2 - Math.PI / 2;
      let best = null, bd = -1;
      pts.forEach(p => {
        const d = (p[0] - 0.5) * Math.cos(a) + (0.6 - p[1]) * Math.sin(a) * 1.6;
        if (d > bd) { bd = d; best = p; }
      });
      out.push(best);
    }
    return out;
  }

  function draw() {
    const { ctx } = cv, w = cv.w(), hh = cv.h();
    ctx.clearRect(0, 0, w, hh);
    const pad = 18;
    ctx.strokeStyle = 'rgba(255,255,255,0.045)';
    for (let i = 0; i <= 8; i++) {
      const x = pad + (w - pad * 2) * i / 8;
      ctx.beginPath(); ctx.moveTo(x, pad); ctx.lineTo(x, hh - pad); ctx.stroke();
    }
    for (let i = 0; i <= 5; i++) {
      const y = pad + (hh - pad * 2) * i / 5;
      ctx.beginPath(); ctx.moveTo(pad, y); ctx.lineTo(w - pad, y); ctx.stroke();
    }

    const X = p => pad + p[0] * (w - pad * 2);
    const Y = p => pad + p[1] * (hh - pad * 2);

    /* silhouette */
    ctx.beginPath();
    SIL.forEach((p, i) => i ? ctx.lineTo(X(p), Y(p)) : ctx.moveTo(X(p), Y(p)));
    ctx.closePath();
    ctx.fillStyle = 'rgba(163,130,92,0.20)'; ctx.fill();
    ctx.strokeStyle = 'rgba(201,162,119,0.85)'; ctx.lineWidth = 1.5; ctx.stroke(); ctx.lineWidth = 1;

    /* collision */
    if (st.mode !== 'none') {
      const pts = hullFor(st.complexity);
      ctx.beginPath();
      pts.forEach((p, i) => i ? ctx.lineTo(X(p), Y(p)) : ctx.moveTo(X(p), Y(p)));
      ctx.closePath();
      ctx.strokeStyle = st.mode === 'hull' ? 'rgba(76,201,240,0.95)' : 'rgba(255,107,138,0.9)';
      ctx.setLineDash(st.mode === 'simple' ? [5, 4] : []);
      ctx.lineWidth = 1.6; ctx.stroke(); ctx.setLineDash([]); ctx.lineWidth = 1;
      ctx.fillStyle = st.mode === 'hull' ? 'rgba(76,201,240,0.10)' : 'rgba(255,107,138,0.08)';
      ctx.fill();
      pts.forEach(p => {
        ctx.beginPath(); ctx.arc(X(p), Y(p), 2.6, 0, Math.PI * 2);
        ctx.fillStyle = st.mode === 'hull' ? '#4cc9f0' : '#ff6b8a'; ctx.fill();
      });
    }

    ctx.font = '500 8.5px ui-monospace, SFMono-Regular, Menlo, monospace';
    ctx.fillStyle = 'rgba(255,255,255,0.32)'; ctx.textAlign = 'left';
    ctx.fillText('FRONT ELEVATION · 82.4 m', pad, hh - 5);
    ctx.textAlign = 'right';
    ctx.fillText('BOUNDS Ø 412 m', w - pad, hh - 5);
  }

  chipRow(api.body, [
    { label: 'No Collision', v: 'none' }, { label: 'Convex Hull', v: 'hull' }, { label: 'Simple 2D', v: 'simple' }
  ], it => { st.mode = it.v; draw(); hero.set({ num: String(st.complexity), unit: 'VERTS', badge: it.v.toUpperCase(), sub: 'Convex decomposition overlay · 3 primitives, 0.4 m error' }); },
  st.mode === 'none' ? 0 : st.mode === 'hull' ? 1 : 2);

  slider(api.body, { label: 'Hull Vertex Budget', min: 6, max: 32, step: 1, value: st.complexity, unit: 'v', dp: 0, onChange: v => { st.complexity = v; draw(); hero.set({ num: String(v), unit: 'VERTS', badge: st.mode.toUpperCase(), sub: 'Convex decomposition overlay · 3 primitives, 0.4 m error' }); } });
  switchRow(api.body, { label: 'Complex as Simple Fallback', value: true });
  switchRow(api.body, { label: 'Generate Overlap Events', value: false });
  draw();
};

/* ============================================================
   MATERIAL SLOTS
   ============================================================ */
WIDGETS.materialSlots = function (api) {
  const st = ns0('citadel');
  const MATS = [
    { n: 'MI_Stone_Basalt',  c: '#5c5f66', id: 0, tris: '94.2k' },
    { n: 'MI_Mortar_Cracked',c: '#8a8378', id: 1, tris: '41.7k' },
    { n: 'MI_Timber_Oak',    c: '#7a5a38', id: 2, tris: '62.3k' },
    { n: 'MI_Roof_Slate',    c: '#3f4a56', id: 3, tris: '58.1k' },
    { n: 'MI_Metal_Iron',    c: '#9aa1ad', id: 4, tris: '18.6k' },
    { n: 'MI_Banner_Cloth',  c: '#8c2f3a', id: 5, tris: '9.7k' }
  ];
  if (st.mat == null) st.mat = 0;
  const hero = api.hero({ num: String(st.mat + 1).padStart(2, '0'), unit: '/ ' + MATS.length + ' SLOTS', badge: MATS[st.mat].tris + ' TRIS', sub: MATS[st.mat].n });

  const list = h('div', 'mats');
  api.body.appendChild(list);
  const rows = [];
  MATS.forEach((m, i) => {
    const r = h('button', 'mat' + (i === st.mat ? ' is-on' : ''));
    r.type = 'button';
    r.innerHTML =
      '<span class="mat__sw" style="background:' + m.c + '"></span>' +
      '<span class="mat__n">' + m.n + '</span>' +
      '<span class="mat__i">EL ' + m.id + '</span>' +
      '<span class="mat__t">' + m.tris + '</span>';
    r.addEventListener('click', () => {
      st.mat = i;
      rows.forEach(x => x.classList.remove('is-on')); r.classList.add('is-on');
      hero.set({ num: String(i + 1).padStart(2, '0'), unit: '/ ' + MATS.length + ' SLOTS', badge: m.tris + ' TRIS', sub: m.n });
      click(1600, 0.02, 0.01);
    });
    list.appendChild(r); rows.push(r);
  });

  const more = api.more();
  kvStrip(more, [['SHADING MODEL', 'Default Lit'], ['BLEND MODE', 'Opaque'], ['TWO-SIDED', 'No'], ['SHADER', 'M_Master_Arch'], ['LOD BIAS', '0']]);
};

/* ============================================================
   SKELETON RIG — interactive bone hierarchy
   ============================================================ */
WIDGETS.skeletonRig = function (api) {
  const st = ns0('knight');
  st.bone = st.bone == null ? 'spine_02' : st.bone;

  const BONES = [
    { n: 'pelvis',        x1: 80, y1: 108, x2: 80, y2: 108, d: 0 },
    { n: 'spine_01',      x1: 80, y1: 108, x2: 80, y2: 92 },
    { n: 'spine_02',      x1: 80, y1: 92,  x2: 80, y2: 74 },
    { n: 'spine_03',      x1: 80, y1: 74,  x2: 80, y2: 58 },
    { n: 'neck_01',       x1: 80, y1: 58,  x2: 80, y2: 46 },
    { n: 'head',          x1: 80, y1: 46,  x2: 80, y2: 30 },
    { n: 'clavicle_l',    x1: 80, y1: 58,  x2: 62, y2: 54 },
    { n: 'upperarm_l',    x1: 62, y1: 54,  x2: 50, y2: 76 },
    { n: 'lowerarm_l',    x1: 50, y1: 76,  x2: 42, y2: 98 },
    { n: 'hand_l',        x1: 42, y1: 98,  x2: 38, y2: 106 },
    { n: 'clavicle_r',    x1: 80, y1: 58,  x2: 98, y2: 54 },
    { n: 'upperarm_r',    x1: 98, y1: 54,  x2: 112, y2: 74 },
    { n: 'lowerarm_r',    x1: 112, y1: 74, x2: 124, y2: 94 },
    { n: 'hand_r',        x1: 124, y1: 94, x2: 130, y2: 102 },
    { n: 'thigh_l',       x1: 80, y1: 108, x2: 68, y2: 138 },
    { n: 'calf_l',        x1: 68, y1: 138, x2: 66, y2: 168 },
    { n: 'foot_l',        x1: 66, y1: 168, x2: 56, y2: 176 },
    { n: 'thigh_r',       x1: 80, y1: 108, x2: 92, y2: 138 },
    { n: 'calf_r',        x1: 92, y1: 138, x2: 94, y2: 168 },
    { n: 'foot_r',        x1: 94, y1: 168, x2: 104, y2: 176 }
  ];

  const sel = () => BONES.find(b => b.n === st.bone) || BONES[2];
  const idxOf = b => BONES.indexOf(b) + 1;
  const hero = api.hero({ num: String(idxOf(sel())).padStart(2, '0'), unit: '/ ' + BONES.length + ' BONES', badge: 'HIERARCHY', sub: 'Click a bone in the rig to inspect its transform and skin weights' });

  const wrap = h('div', 'rig');
  let paths = '';
  BONES.forEach(b => {
    paths += '<g class="rig__b" data-n="' + b.n + '">' +
      '<line x1="' + b.x1 + '" y1="' + b.y1 + '" x2="' + b.x2 + '" y2="' + b.y2 + '" class="rig__hit"/>' +
      '<line x1="' + b.x1 + '" y1="' + b.y1 + '" x2="' + b.x2 + '" y2="' + b.y2 + '" class="rig__ln"/>' +
      '<circle cx="' + b.x1 + '" cy="' + b.y1 + '" r="2.6" class="rig__j"/></g>';
  });
  wrap.innerHTML =
    '<div class="plate plate--rig"><svg viewBox="0 0 160 190" class="rig__svg">' + paths +
    '<circle cx="80" cy="24" r="10" class="rig__skull"/></svg></div>' +
    '<div class="rig__info"></div>';
  api.body.appendChild(wrap);
  const info = wrap.querySelector('.rig__info');

  function paint() {
    wrap.querySelectorAll('.rig__b').forEach(g => g.classList.toggle('is-sel', g.dataset.n === st.bone));
    const b = sel();
    const depth = b.n.split('_').length > 1 ? 2 : 1;
    const chain = [];
    let cur = b.n;
    const parent = { spine_01: 'pelvis', spine_02: 'spine_01', spine_03: 'spine_02', neck_01: 'spine_03', head: 'neck_01',
      clavicle_l: 'spine_03', clavicle_r: 'spine_03', upperarm_l: 'clavicle_l', lowerarm_l: 'upperarm_l', hand_l: 'lowerarm_l',
      upperarm_r: 'clavicle_r', lowerarm_r: 'upperarm_r', hand_r: 'lowerarm_r',
      thigh_l: 'pelvis', calf_l: 'thigh_l', foot_l: 'calf_l', thigh_r: 'pelvis', calf_r: 'thigh_r', foot_r: 'calf_r', pelvis: 'root' };
    while (parent[cur]) { chain.unshift(cur); cur = parent[cur]; }
    chain.unshift('root');
    const bi = BONES.indexOf(b);
    const seed = hash01(bi * 7.13);
    const lx = -40 + seed * 80, ly = -20 + hash01(bi * 3.7) * 40, lz = 60 + b.y1 * 0.9;
    info.innerHTML =
      '<div class="rig__n">' + icon('bone', 13) + '<b>' + b.n + '</b></div>' +
      '<div class="rig__chain">' + chain.map((c, i) => '<span>' + c + '</span>' + (i < chain.length - 1 ? '<i>' + icon('chevron', 9) + '</i>' : '')).join('') + '</div>' +
      '<div class="rig__kv"><span>WORLD LOC</span><b>' + fmt(lx, 1) + ', ' + fmt(ly, 1) + ', ' + fmt(lz, 1) + '</b></div>' +
      '<div class="rig__kv"><span>RENDER</span><b>' + (bi % 3 === 0 ? 'GPU Skin Cache' : 'Skinned') + '</b></div>' +
      '<div class="rig__kv"><span>INFLUENCES</span><b>' + (2 + (bi % 5)) + ' verts</b></div>';
    hero.set({ num: String(idxOf(b)).padStart(2, '0'), unit: '/ ' + BONES.length + ' BONES', badge: 'DEPTH ' + chain.length, sub: 'Selected bone · ' + b.n });
  }
  wrap.querySelectorAll('.rig__b').forEach(g => {
    g.addEventListener('click', () => { st.bone = g.dataset.n; paint(); click(1700, 0.02, 0.01); });
  });
  paint();

  slider(api.body, { label: 'Skin Weight Influence', min: 1, max: 8, step: 1, value: 4, unit: 'bones', dp: 0 });
  switchRow(api.body, { label: 'GPU Skin Cache', value: true });
  switchRow(api.body, { label: 'Retargeting: Humanoid', value: true });
};

/* ============================================================
   ANIM STRIP — montage frame strip
   ============================================================ */
WIDGETS.animStrip = function (api) {
  const st = ns0('knight');
  st.frame = st.frame == null ? 22 : st.frame;
  const CLIPS = [
    { n: 'Idle_Breath',   a: 0,  b: 40, c: '#5b9dff' },
    { n: 'Sprint_Fwd',    a: 40, b: 96, c: '#3ddc97' },
    { n: 'Attack_Combo1', a: 96, b: 152, c: '#ff6b8a' },
    { n: 'Block_Raise',   a: 152, b: 190, c: '#f5a524' }
  ];
  const TOTAL = 200;
  const clipAt = f => CLIPS.find(c => f >= c.a && f < c.b) || CLIPS[CLIPS.length - 1];
  const hero = api.hero({ num: String(st.frame), unit: 'FRAME', badge: clipAt(st.frame).n.toUpperCase(), sub: 'Montage AM_Knight_Combat · 200 frames @ 30 fps' });

  const CELLS = 20;
  const strip = h('div', 'strip');
  const cells = h('div', 'strip__cells');
  for (let i = 0; i < CELLS; i++) {
    const f = Math.round((i + 0.5) / CELLS * TOTAL);
    const cl = CLIPS.find(c => f >= c.a && f < c.b) || CLIPS[0];
    const t = (f - cl.a) / (cl.b - cl.a);
    const c = h('div', 'cell');
    c.style.setProperty('--c', cl.c);
    c.innerHTML = '<svg viewBox="0 0 24 34" class="cell__g"><g stroke="currentColor" fill="none" stroke-width="1.6" stroke-linecap="round">' +
      '<circle cx="12" cy="' + (6 + Math.sin(t * 6.28) * 1.6).toFixed(1) + '" r="2.6"/>' +
      '<path d="M12 ' + (9 + Math.sin(t * 6.28) * 1.6).toFixed(1) + ' L12 20"/>' +
      '<path d="M12 12 L' + (6 + Math.sin(t * 6.28 + 1) * 3).toFixed(1) + ' ' + (17 + Math.cos(t * 6.28) * 2).toFixed(1) + '"/>' +
      '<path d="M12 12 L' + (18 - Math.sin(t * 6.28 + 1) * 3).toFixed(1) + ' ' + (17 - Math.cos(t * 6.28) * 2).toFixed(1) + '"/>' +
      '<path d="M12 20 L' + (8 + Math.sin(t * 6.28) * 3).toFixed(1) + ' 30"/>' +
      '<path d="M12 20 L' + (16 - Math.sin(t * 6.28) * 3).toFixed(1) + ' 30"/>' +
      '</g></svg><span>' + f + '</span>';
    c.addEventListener('click', () => { st.frame = Math.min(f, TOTAL - 1); paint(); click(1800, 0.02, 0.01); });
    cells.appendChild(c);
  }
  const ph = h('div', 'strip__ph');
  strip.append(cells, ph);
  api.body.appendChild(strip);

  const legend = h('div', 'legend');
  legend.innerHTML = CLIPS.map(c =>
    '<span class="legend__i" style="--c:' + c.c + ';flex:' + (c.b - c.a) + '"><b></b>' + c.n + '</span>').join('');
  api.body.appendChild(legend);

  function paint() {
    ph.style.left = (st.frame / TOTAL * 100) + '%';
    const cl = clipAt(st.frame);
    hero.set({ num: String(st.frame), unit: 'FRAME', badge: cl.n.toUpperCase(), sub: 'Montage AM_Knight_Combat · 200 frames @ 30 fps · t = ' + fmt(st.frame / 30, 2) + ' s' });
    [...cells.children].forEach((c, i) => c.classList.toggle('is-on', Math.round((i + 0.5) / CELLS * TOTAL) === st.frame));
  }
  bindDrag(strip, { onMove(p) { st.frame = Math.round(clamp(p.x, 0, 1) * (TOTAL - 1)); paint(); }, onEnd: () => tick() });
  paint();

  const rd = api.readouts([]);
  rd.innerHTML =
    '<div class="rgrid__c"><span class="rgrid__k">BLEND IN</span><span class="rgrid__v">0.15 s</span></div>' +
    '<div class="rgrid__c"><span class="rgrid__k">BLEND OUT</span><span class="rgrid__v">0.25 s</span></div>' +
    '<div class="rgrid__c"><span class="rgrid__k">NOTIFIES</span><span class="rgrid__v">6</span></div>' +
    '<div class="rgrid__c"><span class="rgrid__k">ROOT MOTION</span><span class="rgrid__v">Off</span></div>';
};

/* ============================================================
   TONEMAP CURVE — draggable filmic response
   ============================================================ */
WIDGETS.tonemapCurve = function (api) {
  const st = ns0('post');
  if (!st.curve) st.curve = [0.02, 0.18, 0.42, 0.70, 0.90];
  const P = st.curve;
  const hero = api.hero({ num: fmt(P[3] * 100, 0), unit: '% SHOULDER', badge: 'ACES', sub: 'Input scene-linear → output display-referred · drag control points' });
  const plate = h('div', 'plate plate--graph');
  api.body.appendChild(plate);
  const cv = mkCanvas(plate, 172);
  let drag = -1;

  function evalAt(x) {
    const n = P.length - 1;
    const seg = [0, ...P, 1];
    const xs = seg.map((_, i) => i / (seg.length - 1));
    for (let i = 0; i < seg.length - 1; i++) {
      if (x >= xs[i] && x <= xs[i + 1]) {
        const t = (x - xs[i]) / (xs[i + 1] - xs[i]);
        const s = t * t * (3 - 2 * t);
        return lerp(seg[i], seg[i + 1], s);
      }
    }
    return 1;
  }

  function draw() {
    const { ctx } = cv, w = cv.w(), hh = cv.h();
    ctx.clearRect(0, 0, w, hh);
    const pad = { l: 30, r: 14, t: 14, b: 22 }, iw = w - pad.l - pad.r, ih = hh - pad.t - pad.b;

    for (let i = 0; i <= 4; i++) {
      const y = pad.t + ih * i / 4, x = pad.l + iw * i / 4;
      ctx.strokeStyle = 'rgba(255,255,255,0.05)';
      ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(w - pad.r, y); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(x, pad.t); ctx.lineTo(x, pad.t + ih); ctx.stroke();
    }
    /* identity */
    ctx.strokeStyle = 'rgba(255,255,255,0.16)'; ctx.setLineDash([3, 4]);
    ctx.beginPath(); ctx.moveTo(pad.l, pad.t + ih); ctx.lineTo(pad.l + iw, pad.t); ctx.stroke(); ctx.setLineDash([]);

    /* 18% grey reference */
    ctx.strokeStyle = 'rgba(255,255,255,0.22)';
    const gy = pad.t + ih * (1 - evalAt(0.18));
    ctx.beginPath(); ctx.moveTo(pad.l, gy); ctx.lineTo(w - pad.r, gy); ctx.stroke();
    ctx.fillStyle = 'rgba(255,255,255,0.4)'; ctx.textAlign = 'left';
    ctx.font = '500 8px ui-monospace, SFMono-Regular, Menlo, monospace';
    ctx.fillText('18% GREY → ' + fmt(evalAt(0.18) * 100, 1) + '%', pad.l + 4, gy - 5);

    ctx.beginPath();
    for (let i = 0; i <= iw; i++) {
      const x = i / iw, y = pad.t + ih * (1 - evalAt(x));
      i ? ctx.lineTo(pad.l + i, y) : ctx.moveTo(pad.l + i, y);
    }
    ctx.strokeStyle = '#a78bfa'; ctx.lineWidth = 2; ctx.stroke();
    ctx.lineTo(pad.l + iw, pad.t + ih); ctx.lineTo(pad.l, pad.t + ih); ctx.closePath();
    ctx.fillStyle = 'rgba(167,139,250,0.13)'; ctx.fill();
    ctx.lineWidth = 1;

    P.forEach((v, i) => {
      const x = pad.l + iw * (i + 1) / (P.length + 1), y = pad.t + ih * (1 - v);
      ctx.beginPath(); ctx.arc(x, y, drag === i ? 6.5 : 4.5, 0, Math.PI * 2);
      ctx.fillStyle = drag === i ? '#fff' : '#a78bfa'; ctx.shadowColor = '#a78bfa'; ctx.shadowBlur = drag === i ? 14 : 8; ctx.fill(); ctx.shadowBlur = 0;
    });

    ctx.strokeStyle = 'rgba(255,255,255,0.12)'; ctx.strokeRect(pad.l, pad.t, iw, ih);
    ctx.fillStyle = 'rgba(255,255,255,0.28)'; ctx.textAlign = 'center';
    ctx.fillText('SCENE LINEAR INPUT', pad.l + iw / 2, hh - 5);
    ctx.save(); ctx.translate(9, pad.t + ih / 2); ctx.rotate(-Math.PI / 2);
    ctx.fillText('DISPLAY OUTPUT', 0, 0); ctx.restore();
  }

  bindDrag(cv.c, {
    onStart(p) {
      const w = cv.w(), pad = { l: 30, r: 14 }, iw = w - pad.l - pad.r;
      let best = 1e9; drag = -1;
      P.forEach((v, i) => {
        const x = pad.l + iw * (i + 1) / (P.length + 1);
        const d = Math.abs(x - p.px); if (d < best) { best = d; drag = i; }
      });
      if (best > 26) drag = -1;
    },
    onMove(p) {
      if (drag < 0) return;
      const hh = cv.h(), pad = { t: 14, b: 22 }, ih = hh - pad.t - pad.b;
      const lo = drag === 0 ? 0 : P[drag - 1];
      const hi = drag === P.length - 1 ? 1 : P[drag + 1];
      P[drag] = clamp(clamp(1 - (p.py - pad.t) / ih, 0, 1), lo, hi);
      draw();
      hero.set({ num: fmt(P[3] * 100, 0), unit: '% SHOULDER', badge: 'ACES', sub: 'Toe ' + fmt(P[0] * 100, 0) + '% · knee ' + fmt(P[2] * 100, 0) + '% · clip ' + fmt(P[4] * 100, 0) + '%' });
    },
    onEnd() { drag = -1; draw(); tick(); }
  });
  draw();
  kvStrip(api.body, [['GAMUT', 'ACEScg AP1'], ['WHITE PT', '16.0'], ['CONTRAST', fmt((P[3] - P[1]) / 0.6, 2)], ['TOE', fmt(P[0], 3)]]);
};

/* ============================================================
   RGB HISTOGRAM — live three channel scope
   ============================================================ */
WIDGETS.rgbHistogram = function (api) {
  const st = ns0('post');
  st.exp = st.exp == null ? 1.0 : st.exp;
  const BINS = 64;
  const ch = [new Array(BINS).fill(0), new Array(BINS).fill(0), new Array(BINS).fill(0)];
  const smooth = [new Array(BINS).fill(0), new Array(BINS).fill(0), new Array(BINS).fill(0)];

  const hero = api.hero({ num: fmt(st.exp, 2), unit: 'EV', badge: '64 BINS', sub: 'GPU histogram compute · log-scaled exposure bins' });
  const plate = h('div', 'plate plate--graph');
  api.body.appendChild(plate);
  const cv = mkCanvas(plate, 148);
  let t = 0;

  function regen() {
    for (let c = 0; c < 3; c++) {
      const shift = [0.52, 0.47, 0.42][c] + st.exp * 0.09;
      const spread = [0.20, 0.19, 0.22][c];
      for (let i = 0; i < BINS; i++) {
        const x = i / BINS;
        let v = Math.exp(-Math.pow((x - shift) / spread, 2));
        v += Math.exp(-Math.pow((x - (shift + 0.34)) / (spread * 0.6), 2)) * 0.55;
        v += Math.exp(-Math.pow((x - 0.94) / 0.05, 2)) * 0.4;
        ch[c][i] = v;
      }
    }
  }
  regen();

  function draw(dt) {
    t += dt;
    for (let c = 0; c < 3; c++) for (let i = 0; i < BINS; i++) {
      const tgt = ch[c][i] * (1 + Math.sin(t * 1.6 + i * 0.35 + c) * 0.045);
      smooth[c][i] = lerp(smooth[c][i], tgt, clamp(dt * 7, 0, 1));
    }
    const { ctx } = cv, w = cv.w(), hh = cv.h();
    ctx.clearRect(0, 0, w, hh);
    const pad = { l: 12, r: 12, t: 12, b: 20 }, iw = w - pad.l - pad.r, ih = hh - pad.t - pad.b;
    const mx = Math.max(...smooth.flat(), 0.001);

    ctx.strokeStyle = 'rgba(255,255,255,0.06)';
    for (let i = 0; i <= 3; i++) {
      const y = pad.t + ih * i / 3;
      ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(w - pad.r, y); ctx.stroke();
    }

    const COLS = [['rgba(255,80,110,', '#ff506e'], ['rgba(80,255,140,', '#50ff8c'], ['rgba(90,150,255,', '#5a96ff']];
    ctx.globalCompositeOperation = 'lighter';
    COLS.forEach((C, c) => {
      ctx.beginPath();
      ctx.moveTo(pad.l, pad.t + ih);
      for (let i = 0; i < BINS; i++) {
        const x = pad.l + iw * i / (BINS - 1);
        const y = pad.t + ih * (1 - smooth[c][i] / mx);
        ctx.lineTo(x, y);
      }
      ctx.lineTo(pad.l + iw, pad.t + ih); ctx.closePath();
      const g = ctx.createLinearGradient(0, pad.t, 0, pad.t + ih);
      g.addColorStop(0, C[0] + '0.42)'); g.addColorStop(1, C[0] + '0.03)');
      ctx.fillStyle = g; ctx.fill();
      ctx.beginPath();
      for (let i = 0; i < BINS; i++) {
        const x = pad.l + iw * i / (BINS - 1);
        const y = pad.t + ih * (1 - smooth[c][i] / mx);
        i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
      }
      ctx.strokeStyle = C[0] + '0.85)'; ctx.lineWidth = 1.2; ctx.stroke();
    });
    ctx.globalCompositeOperation = 'source-over';
    ctx.lineWidth = 1;

    ctx.font = '500 8px ui-monospace, SFMono-Regular, Menlo, monospace';
    ctx.fillStyle = 'rgba(255,255,255,0.3)'; ctx.textAlign = 'center';
    ctx.fillText('0', pad.l, hh - 6);
    ctx.fillText('SHADOWS · MIDTONES · HIGHLIGHTS', pad.l + iw / 2, hh - 6);
    ctx.fillText('255', w - pad.r, hh - 6);
    ctx.strokeStyle = 'rgba(255,255,255,0.12)'; ctx.strokeRect(pad.l, pad.t, iw, ih);
  }
  addLoop(draw); draw(0);

  slider(api.body, { label: 'Exposure Bias', min: -3, max: 3, step: 0.05, value: st.exp, unit: 'EV', dp: 2,
    onChange: v => { st.exp = v; regen(); hero.set({ num: fmt(v, 2), unit: 'EV', badge: '64 BINS', sub: 'GPU histogram compute · log-scaled exposure bins' }); } });
  const rd = api.readouts([]);
  function stats() {
    const clipHi = ch.map(c => c.slice(-3).reduce((a, b) => a + b, 0) / c.reduce((a, b) => a + b, 0) * 100);
    rd.innerHTML =
      '<div class="rgrid__c"><span class="rgrid__k">CLIP R</span><span class="rgrid__v">' + fmt(clipHi[0], 2) + '%</span></div>' +
      '<div class="rgrid__c"><span class="rgrid__k">CLIP G</span><span class="rgrid__v">' + fmt(clipHi[1], 2) + '%</span></div>' +
      '<div class="rgrid__c"><span class="rgrid__k">CLIP B</span><span class="rgrid__v">' + fmt(clipHi[2], 2) + '%</span></div>' +
      '<div class="rgrid__c"><span class="rgrid__k">MEAN LUMA</span><span class="rgrid__v">' + fmt(0.42 + st.exp * 0.07, 3) + '</span></div>';
  }
  stats();
  switchRow(api.body, { label: 'Auto Exposure (Histogram)', value: true, onChange: stats });
  switchRow(api.body, { label: 'Highlight Clip Warning', value: false });
};

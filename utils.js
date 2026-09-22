/* ============================================================
   Frontier Engine — Shared Utilities
   ============================================================ */

const DPR = () => Math.min(2, window.devicePixelRatio || 1);

function clamp(v, a, b) { return v < a ? a : v > b ? b : v; }
function lerp(a, b, t) { return a + (b - a) * t; }
function inv(v, a, b) { return b === a ? 0 : (v - a) / (b - a); }
function rnd(a, b) { return a + Math.random() * (b - a); }

function fmt(v, dp = 0) {
  const n = Number(v);
  if (!isFinite(n)) return '0';
  return n.toLocaleString('en-US', { minimumFractionDigits: dp, maximumFractionDigits: dp });
}
function fmtSig(v) {
  const a = Math.abs(v);
  if (a >= 1000) return fmt(v, 0);
  if (a >= 100) return fmt(v, 1);
  if (a >= 10) return fmt(v, 2);
  return fmt(v, 3);
}

/* ---------- DOM ---------- */
function h(tag, cls, html) {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (html != null) n.innerHTML = html;
  return n;
}

/* ---------- RAF loop registry (cleared on every entity switch) ---------- */
const LOOPS = new Set();
let _raf = null, _last = 0;
function addLoop(fn) { LOOPS.add(fn); startRaf(); return () => LOOPS.delete(fn); }
function clearLoops() { LOOPS.clear(); }
function startRaf() {
  if (_raf != null) return;
  _last = performance.now();
  const step = (t) => {
    const dt = Math.min(0.064, (t - _last) / 1000); _last = t;
    LOOPS.forEach(f => { try { f(dt, t / 1000); } catch (e) { /* keep other loops alive */ } });
    _raf = requestAnimationFrame(step);
  };
  _raf = requestAnimationFrame(step);
}

/* ---------- Responsive canvas ---------- */
function mkCanvas(host, height) {
  const c = h('canvas', 'cv');
  c.style.height = height + 'px';
  host.appendChild(c);
  const ctx = c.getContext('2d');
  c._w = 300; c._h = height;
  const fit = () => {
    const r = c.getBoundingClientRect();
    const d = DPR();
    const w = Math.max(1, Math.round((r.width || 300) * d));
    const hh = Math.max(1, Math.round(height * d));
    if (c.width !== w || c.height !== hh) { c.width = w; c.height = hh; }
    ctx.setTransform(d, 0, 0, d, 0, 0);
    c._w = r.width || 300; c._h = height;
  };
  fit();
  if (window.ResizeObserver) new ResizeObserver(fit).observe(c);
  else window.addEventListener('resize', fit);
  return { c, ctx, fit, w: () => c._w, h: () => c._h };
}

/* ---------- Pointer drag ---------- */
function bindDrag(el, handlers) {
  let active = false;
  const pos = (e) => {
    const r = el.getBoundingClientRect();
    return {
      x: clamp((e.clientX - r.left) / Math.max(1, r.width), -0.4, 1.4),
      y: clamp((e.clientY - r.top) / Math.max(1, r.height), -0.4, 1.4),
      px: e.clientX - r.left, py: e.clientY - r.top, rect: r,
      cx: e.clientX, cy: e.clientY
    };
  };
  el.addEventListener('pointerdown', (e) => {
    active = true; el.classList.add('is-drag');
    try { el.setPointerCapture(e.pointerId); } catch (_) {}
    handlers.onStart && handlers.onStart(pos(e));
    handlers.onMove && handlers.onMove(pos(e));
    e.preventDefault();
  });
  el.addEventListener('pointermove', (e) => {
    if (active) handlers.onMove && handlers.onMove(pos(e));
    else handlers.onHover && handlers.onHover(pos(e));
  });
  el.addEventListener('pointerleave', () => { if (!active) handlers.onLeave && handlers.onLeave(); });
  const stop = (e) => {
    if (!active) return; active = false; el.classList.remove('is-drag');
    handlers.onEnd && handlers.onEnd(pos(e));
  };
  el.addEventListener('pointerup', stop);
  el.addEventListener('pointercancel', stop);
  return el;
}

/* ---------- Audio (subtle tactile feedback) ---------- */
let _ac = null, _muted = false;
function click(freq = 1180, dur = 0.028, gain = 0.014, type = 'triangle') {
  if (_muted) return;
  try {
    _ac = _ac || new (window.AudioContext || window.webkitAudioContext)();
    if (_ac.state === 'suspended') _ac.resume();
    const o = _ac.createOscillator(), g = _ac.createGain();
    o.type = type; o.frequency.value = freq;
    g.gain.setValueAtTime(0, _ac.currentTime);
    g.gain.linearRampToValueAtTime(gain, _ac.currentTime + 0.004);
    g.gain.exponentialRampToValueAtTime(0.0001, _ac.currentTime + dur);
    o.connect(g); g.connect(_ac.destination);
    o.start(); o.stop(_ac.currentTime + dur + 0.02);
  } catch (_) {}
}
const tick = () => click(2400, 0.012, 0.005, 'square');
const thunk = () => click(320, 0.06, 0.02, 'sine');

/* ---------- Blackbody / colour science ---------- */
function kelvinToRGB(kelvin) {
  const t = clamp(kelvin, 1000, 40000) / 100;
  let r, g, b;
  if (t <= 66) {
    r = 255;
    g = 99.4708025861 * Math.log(t) - 161.1195681661;
    b = t <= 19 ? 0 : 138.5177312231 * Math.log(t - 10) - 305.0447927307;
  } else {
    r = 329.698727446 * Math.pow(t - 60, -0.1332047592);
    g = 288.1221695283 * Math.pow(t - 60, -0.0755148492);
    b = 255;
  }
  return [clamp(r, 0, 255) | 0, clamp(g, 0, 255) | 0, clamp(b, 0, 255) | 0];
}
const rgbCss = (c) => 'rgb(' + c[0] + ',' + c[1] + ',' + c[2] + ')';

function srgbToLinear(c) { return c.map(v => { v /= 255; return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); }); }
function linearToXYZ(L) {
  return [
    L[0] * 0.4124 + L[1] * 0.3576 + L[2] * 0.1805,
    L[0] * 0.2126 + L[1] * 0.7152 + L[2] * 0.0722,
    L[0] * 0.0193 + L[1] * 0.1192 + L[2] * 0.9505
  ];
}
function kelvinToXY(k) {
  const L = srgbToLinear(kelvinToRGB(k));
  const X = linearToXYZ(L);
  const s = X[0] + X[1] + X[2];
  return s === 0 ? [0.3127, 0.3290] : [X[0] / s, X[1] / s];
}

/* Sun elevation (deg) → correlated colour temperature along the daylight locus.
   Warm at the horizon, ~6000 K overhead; saturates so noon never goes blue-white. */
function autoKelvin(elevation) {
  return Math.round(clamp(1900 + 4200 * (1 - Math.exp(-Math.max(0, elevation) / 22)), 1800, 6800));
}

function kelvinName(k) {
  if (k < 2000) return 'Ember / Candle';
  if (k < 3000) return 'Warm Tungsten';
  if (k < 3800) return 'Golden Hour';
  if (k < 4800) return 'Neutral White';
  if (k < 5800) return 'Daylight';
  if (k < 7000) return 'Cool Daylight';
  if (k < 9000) return 'Overcast Sky';
  if (k < 12000) return 'Open Shade';
  return 'Blue Hour';
}

/* ---------- deterministic pseudo random (stable across frames) ---------- */
function hash01(n) { const x = Math.sin(n * 127.1) * 43758.5453; return x - Math.floor(x); }

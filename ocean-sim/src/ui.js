import { PARAMS, PRESETS, BEAUFORT_WIND, BEAUFORT_NAMES, G } from './config.js?v=5';

function getPath(o, p) {
  return p.split('.').reduce((a, k) => a[k], o);
}
function setPath(o, p, v) {
  const ks = p.split('.');
  let t = o;
  for (let i = 0; i < ks.length - 1; i++) t = t[ks[i]];
  t[ks[ks.length - 1]] = v;
}

const FMTS = {
  int: (v) => `${Math.round(v)}`,
  f1: (v) => Number(v).toFixed(1),
  f2: (v) => Number(v).toFixed(2),
  deg: (v) => `${Math.round(v)}°`,
  hour: (v) => {
    const h = Math.floor(v), m = Math.floor((v - h) * 60);
    return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}`;
  },
  k: (v) => (v >= 1000 ? `${Math.round(v / 1000)}k` : `${Math.round(v)}`),
  beaufort: (v) => BEAUFORT_NAMES[Math.round(v)] || `${Math.round(v)}`,
};

export class UI {
  constructor(api) {
    this.api = api;
    this.inputs = [...document.querySelectorAll('[data-p]')];
    this.outputs = [...document.querySelectorAll('[data-o]')];

    for (const el of this.inputs) {
      const path = el.dataset.p;
      const evt = el.type === 'checkbox' || el.tagName === 'SELECT' ? 'change' : 'input';
      el.addEventListener(evt, () => this.setFromInput(el, path));
    }
    document.querySelectorAll('[data-action]').forEach((el) => {
      el.addEventListener('click', () => this.action(el.dataset.action, el.dataset.arg));
    });

    this.refresh();
    this.spec = document.getElementById('spec');
    this.sctx = this.spec.getContext('2d');
    this.drawSpectrum();
    this.slowTick();
    setInterval(() => this.slowTick(), 250);

    window.addEventListener('keydown', (e) => {
      if (e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT')) {
        if (e.code !== 'Space') return;
      }
      if (e.code === 'Space') {
        e.preventDefault();
        if (e.target && e.target.tagName === 'BUTTON') e.target.blur();
        this.action('pause');
      }
      const names = Object.keys(PRESETS);
      if (/^Digit[1-6]$/.test(e.code)) {
        const i = Number(e.code.slice(5)) - 1;
        if (names[i]) this.action('preset', names[i]);
      }
    });
  }

  setFromInput(el, path) {
    let v;
    if (el.type === 'checkbox') {
      const cur = getPath(PARAMS, path);
      v = el.checked ? (typeof cur === 'number' ? 1 : true) : (typeof cur === 'number' ? 0 : false);
    } else if (el.tagName === 'SELECT') {
      v = el.value;
    } else {
      v = parseFloat(el.value);
    }
    setPath(PARAMS, path, v);
    if (path === 'beaufort') {
      PARAMS.wind = BEAUFORT_WIND[Math.round(v)];
    }
    this.refreshOutputs();
    this.api.after(path);
  }

  refresh() {
    for (const el of this.inputs) {
      const v = getPath(PARAMS, el.dataset.p);
      if (el.type === 'checkbox') el.checked = !!v;
      else el.value = v;
    }
    this.refreshOutputs();
  }

  refreshOutputs() {
    for (const el of this.outputs) {
      const v = getPath(PARAMS, el.dataset.o);
      const f = FMTS[el.dataset.fmt || 'f1'] || FMTS.f1;
      el.textContent = f(v);
    }
  }

  action(name, arg) {
    const api = this.api;
    if (name === 'preset') api.preset(arg);
    else if (name === 'cam') api.setCam(arg);
    else if (name === 'pause') api.togglePause();
    else if (name === 'seed') api.reseed();
    else if (name === 'panel') document.body.classList.toggle('collapsed');
  }

  drawSpectrum() {
    const c = this.sctx, W = this.spec.width, H = this.spec.height;
    const wave = this.api.wave;
    c.clearRect(0, 0, W, H);
    c.fillStyle = 'rgba(10,18,28,0.6)';
    c.fillRect(0, 0, W, H);
    const fMin = 0.04, fMax = 1.4;
    const l0 = Math.log(fMin), l1 = Math.log(fMax);
    const X = (f) => 8 + ((Math.log(f) - l0) / (l1 - l0)) * (W - 16);
    const fOfL = (L) => Math.sqrt((G * 2 * Math.PI) / L) / (2 * Math.PI);
    const bands = [
      [fOfL(600), fOfL(60), 'rgba(53,224,255,0.10)'],
      [fOfL(60), fOfL(8), 'rgba(120,180,255,0.10)'],
      [fOfL(8), Math.min(fOfL(1), fMax), 'rgba(180,140,255,0.10)'],
    ];
    let smax = 1e-9;
    for (let i = 0; i <= 100; i++) {
      const f = fMin * Math.pow(fMax / fMin, i / 100);
      smax = Math.max(smax, wave.spectrumCurve(f));
    }
    const Y = (s) => H - 14 - (s / smax) * (H - 30);
    for (const [a, b, col] of bands) {
      c.fillStyle = col;
      c.fillRect(X(a), 10, Math.max(X(b) - X(a), 1), H - 24);
    }
    c.strokeStyle = 'rgba(255,255,255,0.08)';
    c.lineWidth = 1;
    c.beginPath(); c.moveTo(8, H - 14); c.lineTo(W - 8, H - 14); c.stroke();
    c.strokeStyle = 'rgba(255,200,90,0.55)';
    c.setLineDash([3, 3]);
    c.beginPath(); c.moveTo(X(wave.fp), 10); c.lineTo(X(wave.fp), H - 14); c.stroke();
    c.setLineDash([]);
    c.strokeStyle = '#6ee7ff';
    c.lineWidth = 1.6;
    c.beginPath();
    for (let i = 0; i <= 120; i++) {
      const f = fMin * Math.pow(fMax / fMin, i / 120);
      const x = X(f), y = Y(wave.spectrumCurve(f));
      if (i) c.lineTo(x, y); else c.moveTo(x, y);
    }
    c.stroke();
    c.fillStyle = 'rgba(255,255,255,0.55)';
    c.font = '9px system-ui';
    c.fillText('swell', X(0.09) - 12, H - 3);
    c.fillText('sea', X(0.3) - 6, H - 3);
    c.fillText('chop', X(0.85) - 9, H - 3);
    c.fillStyle = 'rgba(255,200,90,0.85)';
    c.fillText('fp', X(wave.fp) + 3, 18);
  }

  slowTick() {
    const api = this.api;
    document.getElementById('ro-fps').textContent = api.fps().toFixed(0);
    document.getElementById('ro-sea').textContent =
      `Hs ${api.wave.Hs.toFixed(2)} m · Tp ${api.wave.Tp.toFixed(1)} s · U ${PARAMS.wind.toFixed(1)} m/s` +
      (api.wave.saturated ? ' · fully developed' : ' · fetch-limited');
    const bh = api.buoyH();
    document.getElementById('ro-buoy').textContent = `${bh >= 0 ? '+' : ''}${bh.toFixed(2)} m`;
    const t = api.simTime();
    document.getElementById('ro-relief').textContent = `${api.wave.reliefAt(t).toFixed(1)} m`;
    const hh = String(Math.floor(t / 3600)).padStart(2, '0');
    const mm = String(Math.floor(t / 60) % 60).padStart(2, '0');
    const ss = String(Math.floor(t) % 60).padStart(2, '0');
    document.getElementById('ro-clock').textContent = `T+${hh}:${mm}:${ss}`;
    const m = api.wave.interference(t);
    const fill = document.getElementById('meter-fill');
    const lab = document.getElementById('meter-label');
    const r = Math.min(m.ratio, 2.2);
    fill.style.width = `${(r / 2.2 * 100).toFixed(1)}%`;
    let txt = 'lab idle — enable trains A / B', hue = 210;
    if (m.active) {
      if (m.ratio < 0.15) { txt = `DESTRUCTIVE · ${m.ratio.toFixed(3)} — waves cancel`; hue = 160; }
      else if (m.ratio < 0.6) { txt = `partial cancellation · ${m.ratio.toFixed(2)}`; hue = 190; }
      else if (m.ratio < 1.4) { txt = `incoherent mix · ${m.ratio.toFixed(2)}`; hue = 45; }
      else { txt = `CONSTRUCTIVE · ${m.ratio.toFixed(2)} — amplified`; hue = 8; }
    }
    lab.textContent = txt;
    fill.style.background = `hsl(${hue} 90% 55%)`;
  }
}

/* ============================================================================
   FRONTIER · SCRAPYARD BOWL — Motorball Circuit
   Alita-inspired night bowl track: high-banked concrete, hazard stripes,
   floodlights, grandstand, and 2 animated trap-gate rings with blade arms.
   Vanilla Three.js (r160, vendored) — no build step.
   ========================================================================== */

const $ = (id) => document.getElementById(id);
const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
const lerp = (a, b, t) => a + (b - a) * t;
const TAU = Math.PI * 2;
const wrapAngle = (a) => ((a + Math.PI) % TAU + TAU) % TAU - Math.PI;

let THREE = null;
try {
  THREE = await import('./vendor/three.module.min.js');
} catch (e1) {
  try {
    THREE = await import('https://cdn.jsdelivr.net/npm/three@0.160.0/build/three.module.min.js');
  } catch (e2) {
    THREE = await import('https://unpkg.com/three@0.160.0/build/three.module.min.js');
  }
}

/* ------------------------------------------------------------------ config */
const CONTROL = [
  [-81.0, -94.5], [81.0, -94.5], [202.5, -94.5], [263.3, -40.5], [263.3, 40.5],
  [202.5, 94.5], [81.0, 94.5], [-13.5, 94.5], [-81.0, 74.3], [-121.5, 94.5],
  [-202.5, 94.5], [-263.3, 40.5], [-263.3, -40.5], [-202.5, -94.5],
].map(([x, z]) => new THREE.Vector3(x, 0, z));

const HALF_W = 13;          // road half width (m) — 26 m wide bowl
const WALL_H = 7.5;         // bowl wall height (m)
const WALL_P = 2.6;         // bowl profile exponent (parabolic)
const RACE_LAPS = 3;
const TOP_SPEED = 76;       // m/s (~274 km/h)
const GATES = [
  { name: 'ALPHA', f: 0.44, speed: 0.85, phase: 0.0 },
  { name: 'BETA', f: 0.96, speed: -1.05, phase: 1.7 },
];
const AI_DEFS = [
  { name: 'VOLT-9', suit: 0xb33c12, visor: 0xff7a1a, base: 58, s0: -38, ph: 0.0 },
  { name: 'JACKAL', suit: 0x7a1e5e, visor: 0xff3df0, base: 60, s0: -64, ph: 2.1 },
  { name: 'MIRA-7', suit: 0x1e6b46, visor: 0x51ff9e, base: 56, s0: -90, ph: 4.2 },
];

const bowlY = (u) => WALL_H * Math.pow(Math.abs(u), WALL_P);
const bowlSlope = (u) => (WALL_H * WALL_P / HALF_W) * Math.pow(Math.abs(u), WALL_P - 1) * Math.sign(u || 1);

/* ------------------------------------------------------------------- audio */
const Audio8 = {
  ctx: null, master: null, muted: false,
  eng: null, engSub: null, engGain: null, engFilter: null,
  windGain: null, windFilter: null, scrapeGain: null, alarmOsc: null, alarmGain: null,
  init() {
    if (this.ctx) { this.ctx.resume(); return; }
    const C = window.AudioContext || window.webkitAudioContext;
    if (!C) return;
    const ctx = this.ctx = new C();
    this.master = ctx.createGain(); this.master.gain.value = 0.5;
    this.master.connect(ctx.destination);
    // engine: saw + sub sine through lowpass
    this.eng = ctx.createOscillator(); this.eng.type = 'sawtooth'; this.eng.frequency.value = 55;
    this.engSub = ctx.createOscillator(); this.engSub.type = 'sine'; this.engSub.frequency.value = 28;
    this.engFilter = ctx.createBiquadFilter(); this.engFilter.type = 'lowpass'; this.engFilter.frequency.value = 700;
    this.engGain = ctx.createGain(); this.engGain.gain.value = 0.0;
    this.eng.connect(this.engFilter); this.engSub.connect(this.engFilter);
    this.engFilter.connect(this.engGain); this.engGain.connect(this.master);
    this.eng.start(); this.engSub.start();
    // wind + scrape share a noise buffer
    const len = ctx.sampleRate * 2, buf = ctx.createBuffer(1, len, ctx.sampleRate);
    const d = buf.getChannelData(0);
    for (let i = 0; i < len; i++) d[i] = Math.random() * 2 - 1;
    const noise = ctx.createBufferSource(); noise.buffer = buf; noise.loop = true;
    this.windFilter = ctx.createBiquadFilter(); this.windFilter.type = 'bandpass'; this.windFilter.frequency.value = 500;
    this.windGain = ctx.createGain(); this.windGain.gain.value = 0;
    noise.connect(this.windFilter); this.windFilter.connect(this.windGain); this.windGain.connect(this.master);
    const noise2 = ctx.createBufferSource(); noise2.buffer = buf; noise2.loop = true; noise2.playbackRate.value = 0.7;
    const hp = ctx.createBiquadFilter(); hp.type = 'highpass'; hp.frequency.value = 2500;
    this.scrapeGain = ctx.createGain(); this.scrapeGain.gain.value = 0;
    noise2.connect(hp); hp.connect(this.scrapeGain); this.scrapeGain.connect(this.master);
    noise.start(); noise2.start();
    // trap alarm
    this.alarmOsc = ctx.createOscillator(); this.alarmOsc.type = 'square'; this.alarmOsc.frequency.value = 740;
    this.alarmGain = ctx.createGain(); this.alarmGain.gain.value = 0;
    this.alarmOsc.connect(this.alarmGain); this.alarmGain.connect(this.master);
    this.alarmOsc.start();
    this._noiseBuf = buf;
  },
  beep(freq, dur = 0.15, type = 'sine', vol = 0.25, when = 0) {
    if (!this.ctx || this.muted) return;
    const ctx = this.ctx, t = ctx.currentTime + when;
    const o = ctx.createOscillator(), g = ctx.createGain();
    o.type = type; o.frequency.value = freq;
    g.gain.setValueAtTime(vol, t); g.gain.exponentialRampToValueAtTime(0.001, t + dur);
    o.connect(g); g.connect(this.master); o.start(t); o.stop(t + dur + 0.02);
  },
  thud() {
    if (!this.ctx || this.muted) return;
    const ctx = this.ctx, t = ctx.currentTime;
    const o = ctx.createOscillator(), g = ctx.createGain();
    o.type = 'sine'; o.frequency.setValueAtTime(130, t); o.frequency.exponentialRampToValueAtTime(38, t + 0.3);
    g.gain.setValueAtTime(0.5, t); g.gain.exponentialRampToValueAtTime(0.001, t + 0.35);
    o.connect(g); g.connect(this.master); o.start(t); o.stop(t + 0.4);
    const n = ctx.createBufferSource(); n.buffer = this._noiseBuf;
    const ng = ctx.createGain(); ng.gain.setValueAtTime(0.4, t); ng.gain.exponentialRampToValueAtTime(0.001, t + 0.25);
    const f = ctx.createBiquadFilter(); f.type = 'lowpass'; f.frequency.value = 3000;
    n.connect(f); f.connect(ng); ng.connect(this.master); n.start(t); n.stop(t + 0.3);
  },
  whoosh() { this.beep(180, 0.35, 'sawtooth', 0.06); },
  update(v, throttle, scraping, alarmOn, t) {
    if (!this.ctx) return;
    const m = this.muted ? 0 : 1;
    const av = Math.abs(v);
    this.eng.frequency.value = 52 + av * 1.7;
    this.engSub.frequency.value = 26 + av * 0.85;
    this.engFilter.frequency.value = 500 + throttle * 900 + av * 6;
    this.engGain.gain.value = m * (0.028 + throttle * 0.05);
    this.windFilter.frequency.value = 350 + av * 9;
    this.windGain.gain.value = m * Math.pow(av / TOP_SPEED, 2) * 0.14;
    this.scrapeGain.gain.value = m * (scraping ? 0.10 : 0);
    this.alarmGain.gain.value = (m && alarmOn && (t % 0.5 < 0.25)) ? 0.05 : 0;
  },
  toggleMute() {
    this.muted = !this.muted;
    if (this.master) this.master.gain.value = this.muted ? 0 : 0.5;
    return this.muted;
  },
};

/* ----------------------------------------------------------------- renderer */
let renderer;
try {
  renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
} catch (e) {
  $('menu').classList.add('err');
  document.querySelector('#menu .card p.sub').textContent =
    'WebGL is unavailable in this browser — the Scrapyard Bowl needs GPU rendering.';
  throw e;
}
renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.12;
$('scene').appendChild(renderer.domElement);

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x05070c);
scene.fog = new THREE.Fog(0x05070c, 80, 540);
const camera = new THREE.PerspectiveCamera(64, window.innerWidth / window.innerHeight, 0.1, 2600);

scene.add(new THREE.AmbientLight(0x2a3850, 0.65));
scene.add(new THREE.HemisphereLight(0x3a5a8a, 0x0a0a0c, 0.55));
const moon = new THREE.DirectionalLight(0x8fb0ff, 0.4);
moon.position.set(220, 320, 120);
scene.add(moon);

/* ------------------------------------------------------- canvas textures */
function canvasTex(w, h, draw, srgb = true) {
  const c = document.createElement('canvas'); c.width = w; c.height = h;
  draw(c.getContext('2d'), w, h);
  const t = new THREE.CanvasTexture(c);
  if (srgb) t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = renderer.capabilities.getMaxAnisotropy();
  return t;
}
let seed = 7;
const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;

function chevron(ctx, cx, cy, s, color, lw) {
  ctx.strokeStyle = color; ctx.lineWidth = lw; ctx.lineCap = 'butt';
  ctx.beginPath(); ctx.moveTo(cx - s, cy + s * 0.7); ctx.lineTo(cx, cy - s * 0.7); ctx.lineTo(cx + s, cy + s * 0.7); ctx.stroke();
}

const roadTex = canvasTex(1024, 1024, (ctx, W, H) => {
  ctx.fillStyle = '#33393f'; ctx.fillRect(0, 0, W, H);
  for (let i = 0; i < 5200; i++) { // concrete noise
    const g = 40 + rnd() * 40;
    ctx.fillStyle = `rgba(${g},${g + 4},${g + 8},0.16)`;
    ctx.fillRect(rnd() * W, rnd() * H, 1 + rnd() * 3, 1 + rnd() * 3);
  }
  for (let i = 0; i < 70; i++) { // brushed streaks along travel
    ctx.strokeStyle = `rgba(255,255,255,${0.02 + rnd() * 0.04})`;
    ctx.lineWidth = 1 + rnd() * 2;
    const x = rnd() * W; ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x + (rnd() - 0.5) * 30, H); ctx.stroke();
  }
  for (let i = 0; i < 30; i++) { // tyre arcs — heavy near bowl walls
    const edge = rnd() < 0.5 ? W * 0.16 : W * 0.84;
    const x = edge + (rnd() - 0.5) * W * 0.22, y = rnd() * H;
    ctx.strokeStyle = `rgba(8,8,10,${0.16 + rnd() * 0.22})`;
    ctx.lineWidth = 5 + rnd() * 9;
    ctx.beginPath(); ctx.moveTo(x - 60, y - 120);
    ctx.quadraticCurveTo(x + (rnd() - 0.5) * 160, y, x + (rnd() - 0.5) * 60, y + 130); ctx.stroke();
  }
  ctx.fillStyle = 'rgba(120,180,255,0.20)'; // faint cyan guide dashes
  for (let y = 0; y < H; y += 128) ctx.fillRect(W / 2 - 3, y, 6, 64);
  ctx.fillStyle = 'rgba(232,238,245,0.85)'; // worn edge lines
  for (let y = 0; y < H; y += 64) {
    if (rnd() < 0.85) { ctx.fillRect(10, y, 6, 52); ctx.fillRect(W - 16, y, 6, 52); }
  }
  for (let y = 64; y < H; y += 128) { // yellow chevron bands (point +v = travel)
    chevron(ctx, 96, y, 30, 'rgba(255,196,0,0.9)', 13);
    chevron(ctx, W - 96, y, 30, 'rgba(255,196,0,0.9)', 13);
  }
});
roadTex.wrapS = roadTex.wrapT = THREE.RepeatWrapping;

const stripeTex = canvasTex(128, 128, (ctx, W, H) => {
  ctx.fillStyle = '#0f1114'; ctx.fillRect(0, 0, W, H);
  ctx.fillStyle = '#f2b705';
  for (let i = -H; i < W + H; i += 32) {
    ctx.beginPath(); ctx.moveTo(i, 0); ctx.lineTo(i + 16, 0); ctx.lineTo(i + 16 + H, H); ctx.lineTo(i + H, H);
    ctx.closePath(); ctx.fill();
  }
  for (let i = 0; i < 500; i++) {
    ctx.fillStyle = `rgba(0,0,0,${rnd() * 0.25})`; ctx.fillRect(rnd() * W, rnd() * H, 2, 2);
  }
});
stripeTex.wrapS = stripeTex.wrapT = THREE.RepeatWrapping;

const fenceTex = canvasTex(128, 128, (ctx, W, H) => {
  ctx.clearRect(0, 0, W, H);
  ctx.strokeStyle = 'rgba(170,180,192,0.55)'; ctx.lineWidth = 2;
  for (let i = -H; i < W + H; i += 16) {
    ctx.beginPath(); ctx.moveTo(i, 0); ctx.lineTo(i + H, H); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(i + H, 0); ctx.lineTo(i, H); ctx.stroke();
  }
});
fenceTex.wrapS = fenceTex.wrapT = THREE.RepeatWrapping;

const glowTex = canvasTex(128, 128, (ctx, W, H) => {
  const g = ctx.createRadialGradient(64, 64, 2, 64, 64, 64);
  g.addColorStop(0, 'rgba(255,255,255,1)'); g.addColorStop(0.25, 'rgba(255,255,255,0.5)');
  g.addColorStop(1, 'rgba(255,255,255,0)');
  ctx.fillStyle = g; ctx.fillRect(0, 0, W, H);
});

const checkerTex = canvasTex(256, 64, (ctx, W, H) => {
  const cw = W / 16, ch = H / 2;
  for (let x = 0; x < 16; x++) for (let y = 0; y < 2; y++) {
    ctx.fillStyle = (x + y) % 2 ? '#0c0e11' : '#e8edf3'; ctx.fillRect(x * cw, y * ch, cw, ch);
  }
});

const gateStripTex = canvasTex(256, 256, (ctx, W, H) => {
  ctx.fillStyle = 'rgba(20,22,26,0.85)'; ctx.fillRect(0, 0, W, H);
  for (let y = 32; y < H; y += 64)
    for (const x of [40, 128, 216]) chevron(ctx, x, y, 22, 'rgba(255,196,0,0.95)', 10);
  ctx.fillStyle = 'rgba(255,255,255,0.9)';
  for (let x = 16; x < W; x += 48) { ctx.fillRect(x, 4, 16, 6); ctx.fillRect(x, H - 10, 16, 6); }
});

function billboardTex(title, sub, accent, bg = '#0a0e14') {
  return canvasTex(512, 160, (ctx, W, H) => {
    ctx.fillStyle = bg; ctx.fillRect(0, 0, W, H);
    ctx.strokeStyle = accent; ctx.lineWidth = 6; ctx.strokeRect(8, 8, W - 16, H - 16);
    ctx.fillStyle = accent; ctx.font = '800 54px system-ui,sans-serif';
    ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    ctx.fillText(title, W / 2, H / 2 - 14);
    ctx.fillStyle = 'rgba(255,255,255,0.75)'; ctx.font = '600 22px system-ui,sans-serif';
    ctx.fillText(sub, W / 2, H / 2 + 36);
    ctx.fillStyle = 'rgba(0,0,0,0.18)';
    for (let y = 0; y < H; y += 4) ctx.fillRect(0, y, W, 1);
  });
}

const bannerTex = canvasTex(1024, 128, (ctx, W, H) => {
  ctx.fillStyle = '#1f5eff'; ctx.fillRect(0, 0, W, H);
  ctx.fillStyle = '#f2b705';
  for (let i = 0; i < 60; i += 30) {
    ctx.save(); ctx.translate(i, 0); ctx.transform(1, 0, -0.4, 1, 0, 0); ctx.fillRect(0, 0, 14, H); ctx.restore();
    ctx.save(); ctx.translate(W - i, 0); ctx.transform(1, 0, -0.4, 1, 0, 0); ctx.fillRect(0, 0, 14, H); ctx.restore();
  }
  ctx.fillStyle = '#fff'; ctx.font = '800 64px system-ui,sans-serif';
  ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
  ctx.fillText('★ SCRAPYARD BOWL ★', W / 2, H / 2 + 2);
});

/* ---------------------------------------------------------- track sampling */
const curve = new THREE.CatmullRomCurve3(CONTROL, true, 'centripetal');
const TRACK_LEN = curve.getLength();
const NS = 800;
const sPts = curve.getSpacedPoints(NS); // NS+1 pts, last == first
const sTan = [], sLeft = [], sHead = [];
{
  for (let i = 0; i < NS; i++) {
    const a = sPts[i], b = sPts[(i + 1) % NS];
    const t = new THREE.Vector3().subVectors(b, a); t.y = 0; t.normalize();
    sTan.push(t);
    sLeft.push(new THREE.Vector3(t.z, 0, -t.x));
  }
  let prev = Math.atan2(sTan[0].x, sTan[0].z);
  sHead.push(prev);
  for (let i = 1; i < NS; i++) {
    const h = Math.atan2(sTan[i].x, sTan[i].z);
    prev += wrapAngle(h - prev);
    sHead.push(prev);
  }
}
const DS = TRACK_LEN / NS;
const wrapS = (s) => ((s % TRACK_LEN) + TRACK_LEN) % TRACK_LEN;
const wrapDist = (a, b) => ((a - b + TRACK_LEN * 1.5) % TRACK_LEN) - TRACK_LEN * 0.5;
const _sp = { pos: new THREE.Vector3(), tan: new THREE.Vector3(), left: new THREE.Vector3(), head: 0, curv: 0 };
function sampleAt(s) {
  s = wrapS(s);
  const f = (s / TRACK_LEN) * NS, i0 = Math.floor(f) % NS, i1 = (i0 + 1) % NS, fr = f - Math.floor(f);
  _sp.pos.lerpVectors(sPts[i0], sPts[i1], fr);
  _sp.tan.lerpVectors(sTan[i0], sTan[i1], fr).normalize();
  _sp.left.lerpVectors(sLeft[i0], sLeft[i1], fr).normalize();
  _sp.head = lerp(sHead[i0], sHead[i0] + wrapAngle(sHead[i1] - sHead[i0]), fr);
  const ip = (i0 + NS - 1) % NS, in_ = (i0 + 2) % NS;
  _sp.curv = wrapAngle(sHead[in_] - sHead[ip]) / (3 * DS);
  return _sp;
}
function roadPoint(s, u, out = new THREE.Vector3()) {
  const sm = sampleAt(s);
  out.copy(sm.pos).addScaledVector(sm.left, u * HALF_W);
  out.y = bowlY(u);
  return out;
}
const loopCenter = new THREE.Vector3();
sPts.forEach((p) => loopCenter.add(p));
loopCenter.multiplyScalar(1 / sPts.length); loopCenter.y = 0;

/* ------------------------------------------------------------ road meshes */
const roadMat = new THREE.MeshStandardMaterial({ map: roadTex, roughness: 0.94, metalness: 0.04 });
{
  const SEG = 720, AC = 30;
  const pos = [], uv = [], idx = [];
  for (let i = 0; i <= SEG; i++) {
    const s = (i / SEG) * TRACK_LEN;
    for (let j = 0; j <= AC; j++) {
      const u = (j / AC) * 2 - 1;
      const p = roadPoint(s, u);
      pos.push(p.x, p.y + 0.02, p.z);
      uv.push(j / AC, s / 40);
    }
  }
  for (let i = 0; i < SEG; i++) for (let j = 0; j < AC; j++) {
    const a = i * (AC + 1) + j, b = a + AC + 1;
    idx.push(a, b, a + 1, b, b + 1, a + 1);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setIndex(idx); g.computeVertexNormals();
  scene.add(new THREE.Mesh(g, roadMat));
}
function sideRibbon(uSide, yBot, yTop, outTop, vScale, mat) {
  const SEG = 360, pos = [], uvA = [], idx = [];
  for (let i = 0; i <= SEG; i++) {
    const s = (i / SEG) * TRACK_LEN;
    const sm = sampleAt(s);
    const bx = sm.pos.x + sm.left.x * uSide * HALF_W, bz = sm.pos.z + sm.left.z * uSide * HALF_W;
    const tx = bx + sm.left.x * uSide * outTop, tz = bz + sm.left.z * uSide * outTop;
    pos.push(bx, bowlY(uSide) + yBot, bz, tx, bowlY(uSide) + yTop, tz);
    uvA.push(0, s / vScale, 1, s / vScale);
  }
  for (let i = 0; i < SEG; i++) {
    const a = i * 2;
    idx.push(a, a + 2, a + 1, a + 1, a + 2, a + 3);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uvA, 2));
  g.setIndex(idx); g.computeVertexNormals();
  const m = new THREE.Mesh(g, mat);
  scene.add(m); return m;
}
const rimMat = new THREE.MeshStandardMaterial({ map: stripeTex, roughness: 0.8, metalness: 0.15, side: THREE.DoubleSide });
sideRibbon(1, 0.0, 1.25, 0.55, 6, rimMat);
sideRibbon(-1, 0.0, 1.25, 0.55, 6, rimMat);
const railMat = new THREE.MeshStandardMaterial({ color: 0x16191e, roughness: 0.6, metalness: 0.6, side: THREE.DoubleSide });
sideRibbon(1, 1.25, 1.45, 0.6, 6, railMat);
sideRibbon(-1, 1.25, 1.45, 0.6, 6, railMat);
const fenceMat = new THREE.MeshStandardMaterial({
  map: fenceTex, transparent: true, roughness: 0.5, metalness: 0.7,
  side: THREE.DoubleSide, depthWrite: false, color: 0x9aa4b0,
});
sideRibbon(1, 1.45, 4.6, 1.1, 2.6, fenceMat);
sideRibbon(-1, 1.45, 4.6, 1.1, 2.6, fenceMat);
{ // fence posts
  const step = 9, count = Math.floor(TRACK_LEN / step);
  const geo = new THREE.CylinderGeometry(0.07, 0.07, 3.4, 6);
  const mat = new THREE.MeshStandardMaterial({ color: 0x23272e, roughness: 0.6, metalness: 0.7 });
  const inst = new THREE.InstancedMesh(geo, mat, count * 2);
  const m4 = new THREE.Matrix4();
  let k = 0;
  for (let i = 0; i < count; i++) {
    const s = i * step;
    for (const sd of [1, -1]) {
      const sm = sampleAt(s);
      const x = sm.pos.x + sm.left.x * sd * (HALF_W + 0.85);
      const z = sm.pos.z + sm.left.z * sd * (HALF_W + 0.85);
      m4.makeTranslation(x, WALL_H + 2.8, z);
      inst.setMatrixAt(k++, m4);
    }
  }
  inst.instanceMatrix.needsUpdate = true;
  scene.add(inst);
}
function ribbonPatch(s0, s1, u0, u1, mat, yOff = 0.06) {
  const NSG = 6, NUG = 12, pos = [], uvA = [], idx = [];
  for (let i = 0; i <= NSG; i++) {
    const s = lerp(s0, s1, i / NSG);
    for (let j = 0; j <= NUG; j++) {
      const u = lerp(u0, u1, j / NUG);
      const p = roadPoint(s, u);
      pos.push(p.x, p.y + yOff, p.z);
      uvA.push(j / NUG, i / NSG);
    }
  }
  for (let i = 0; i < NSG; i++) for (let j = 0; j < NUG; j++) {
    const a = i * (NUG + 1) + j, b = a + NUG + 1;
    idx.push(a, b, a + 1, b, b + 1, a + 1);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uvA, 2));
  g.setIndex(idx); g.computeVertexNormals();
  const m = new THREE.Mesh(g, mat);
  m.renderOrder = 2; scene.add(m); return m;
}
ribbonPatch(-1.6, 1.6, -0.82, 0.82,
  new THREE.MeshBasicMaterial({ map: checkerTex, polygonOffset: true, polygonOffsetFactor: -2 }));

/* -------------------------------------------------------- ground and sky */
{
  const g = new THREE.Mesh(
    new THREE.PlaneGeometry(1600, 1600),
    new THREE.MeshStandardMaterial({ color: 0x0a0c10, roughness: 1, metalness: 0 }));
  g.rotation.x = -Math.PI / 2; g.position.y = -0.6; scene.add(g);
}
{ // gradient sky dome
  const skyMat = new THREE.ShaderMaterial({
    side: THREE.BackSide, depthWrite: false, fog: false,
    uniforms: {},
    vertexShader: `varying vec3 vP; void main(){ vP=position; gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0); }`,
    fragmentShader: `varying vec3 vP;
      void main(){
        float h = normalize(vP).y;
        vec3 top = vec3(0.008,0.012,0.035);
        vec3 mid = vec3(0.035,0.07,0.13);
        vec3 hor = vec3(0.10,0.20,0.32);
        vec3 c = mix(hor, mid, smoothstep(0.0,0.25,h));
        c = mix(c, top, smoothstep(0.2,0.7,h));
        c += vec3(0.10,0.05,0.0) * pow(max(0.0,1.0-abs(h+0.02)*9.0), 2.0);
        gl_FragColor = vec4(c,1.0);
      }`,
  });
  scene.add(new THREE.Mesh(new THREE.SphereGeometry(1100, 24, 16), skyMat));
}
{ // stars
  const n = 900, p = new Float32Array(n * 3);
  for (let i = 0; i < n; i++) {
    const a = rnd() * TAU, e = 0.08 + rnd() * 1.2, r = 1000;
    p[i * 3] = Math.cos(a) * Math.cos(e) * r;
    p[i * 3 + 1] = Math.sin(e) * r;
    p[i * 3 + 2] = Math.sin(a) * Math.cos(e) * r;
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(p, 3));
  scene.add(new THREE.Points(g, new THREE.PointsMaterial({
    color: 0xbdd0ee, size: 1.7, sizeAttenuation: false, fog: false, transparent: true, opacity: 0.8 })));
}
const zalem = new THREE.Group(); // sky-city ring over the scrapyard
{
  const mat = new THREE.MeshStandardMaterial({
    color: 0x1a2436, emissive: 0x8fb4ff, emissiveIntensity: 1.4, roughness: 0.4, metalness: 0.6, fog: false });
  zalem.add(new THREE.Mesh(new THREE.TorusGeometry(120, 5, 10, 72), mat));
  const mat2 = mat.clone(); mat2.emissiveIntensity = 0.7;
  const t2 = new THREE.Mesh(new THREE.TorusGeometry(88, 2.5, 8, 64), mat2);
  zalem.add(t2);
  const disc = new THREE.Mesh(new THREE.CircleGeometry(46, 40),
    new THREE.MeshBasicMaterial({ color: 0x24406b, fog: false }));
  zalem.add(disc);
  zalem.rotation.x = Math.PI / 2 - 0.12;
  zalem.position.set(-160, 300, -420);
  scene.add(zalem);
  const cable = new THREE.Mesh(new THREE.CylinderGeometry(0.9, 0.9, 320, 6), // elevator tether hangs vertical
    new THREE.MeshBasicMaterial({ color: 0x5f7ca8, transparent: true, opacity: 0.35, fog: false }));
  cable.position.set(-160, 140, -420);
  scene.add(cable);
}
const searchlights = [];
for (const [x, z, ph] of [[-190, 150, 0], [200, 150, 2.4]]) {
  const grp = new THREE.Group(); grp.position.set(x, 0, z);
  const base = new THREE.Mesh(new THREE.BoxGeometry(4, 2, 4),
    new THREE.MeshStandardMaterial({ color: 0x1c2129, roughness: 0.7, metalness: 0.5 }));
  base.position.y = 1; grp.add(base);
  const cone = new THREE.Mesh(new THREE.ConeGeometry(20, 190, 18, 1, true),
    new THREE.MeshBasicMaterial({ color: 0x9fc0ff, transparent: true, opacity: 0.055,
      blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide, fog: false }));
  cone.position.y = 95; grp.add(cone);
  grp.userData = { cone, ph };
  scene.add(grp); searchlights.push(grp);
}

/* --------------------------------------------------------------- skyline */
const beacons = [];
{
  const geo = new THREE.BoxGeometry(1, 1, 1);
  const mat = new THREE.MeshStandardMaterial({ color: 0x0c1017, roughness: 1 });
  const COUNT = 140;
  const inst = new THREE.InstancedMesh(geo, mat, COUNT);
  const m4 = new THREE.Matrix4(), q = new THREE.Quaternion(), sc = new THREE.Vector3(), pv = new THREE.Vector3();
  const up = new THREE.Vector3(0, 1, 0);
  for (let i = 0; i < COUNT; i++) {
    const a = (i / COUNT) * TAU + rnd() * 0.05, r = 400 + rnd() * 170;
    const w = 14 + rnd() * 30, h = 18 + rnd() * 95, dd = 14 + rnd() * 30;
    pv.set(Math.cos(a) * r, h / 2 - 1, Math.sin(a) * r);
    q.setFromAxisAngle(up, rnd() * TAU);
    sc.set(w, h, dd);
    m4.compose(pv, q, sc);
    inst.setMatrixAt(i, m4);
    if (h > 82 && beacons.length < 12) {
      const s = new THREE.Sprite(new THREE.SpriteMaterial({
        map: glowTex, color: 0xff2d22, transparent: true, opacity: 0.9,
        blending: THREE.AdditiveBlending, depthWrite: false }));
      s.position.set(pv.x, h + 2, pv.z); s.scale.set(7, 7, 1);
      s.userData.ph = rnd() * TAU;
      scene.add(s); beacons.push(s);
    }
  }
  inst.instanceMatrix.needsUpdate = true;
  scene.add(inst);
}

/* ------------------------------------------------------------ floodlights */
{
  const poleMat = new THREE.MeshStandardMaterial({ color: 0x23272e, roughness: 0.6, metalness: 0.7 });
  const headMat = new THREE.MeshStandardMaterial({ color: 0x14171c, roughness: 0.6, metalness: 0.6 });
  const panelMat = new THREE.MeshBasicMaterial({ color: 0xeaf2ff });
  for (let i = 0; i < 8; i++) {
    const s = (i / 8 + 1 / 16) * TRACK_LEN;
    const sm = sampleAt(s);
    const out = new THREE.Vector3().subVectors(sm.pos, loopCenter); out.y = 0; out.normalize();
    const base = sm.pos.clone().addScaledVector(out, 27); base.y = 0;
    const grp = new THREE.Group(); grp.position.copy(base);
    const pole = new THREE.Mesh(new THREE.CylinderGeometry(0.5, 0.8, 24, 8), poleMat);
    pole.position.y = 12; grp.add(pole);
    const head = new THREE.Group(); head.position.y = 24.5; grp.add(head);
    const bar = new THREE.Mesh(new THREE.BoxGeometry(7.5, 1, 0.8), headMat); head.add(bar);
    for (let px = 0; px < 3; px++) for (let py = 0; py < 2; py++) {
      const panel = new THREE.Mesh(new THREE.PlaneGeometry(1.9, 1.1), panelMat);
      panel.position.set((px - 1) * 2.3, py * 1.5 - 0.4, 0.5);
      head.add(panel);
    }
    const glow = new THREE.Sprite(new THREE.SpriteMaterial({ map: glowTex, color: 0xcfe0ff,
      transparent: true, opacity: 0.55, blending: THREE.AdditiveBlending, depthWrite: false }));
    glow.scale.set(26, 16, 1); glow.position.y = 24.5; grp.add(glow);
    grp.lookAt(sm.pos.x, 24.5, sm.pos.z);
    scene.add(grp);
    if (i % 2 === 0) {
      const spot = new THREE.SpotLight(0xd8e6ff, 90, 230, 0.55, 0.65, 1);
      spot.position.copy(base).add(new THREE.Vector3(0, 24.5, 0));
      spot.target.position.copy(sm.pos);
     
      scene.add(spot); scene.add(spot.target);
    }
  }
}

/* ------------------------------------------------------------ grandstand */
let crowd = null;
{
  const gpos = new THREE.Vector3(40, 0, -138);
  const stand = new THREE.Group(); stand.position.copy(gpos); scene.add(stand);
  const structMat = new THREE.MeshStandardMaterial({ color: 0x1a1e25, roughness: 0.65, metalness: 0.55 });
  const roofMat = new THREE.MeshStandardMaterial({ color: 0x11141a, roughness: 0.85, metalness: 0.3, side: THREE.DoubleSide });
  for (let x = -130; x <= 130; x += 26) {
    const col = new THREE.Mesh(new THREE.BoxGeometry(1.6, 22, 1.6), structMat);
    col.position.set(x, 11, -6); stand.add(col);
    const col2 = col.clone(); col2.position.z = 10; stand.add(col2);
  }
  for (let t = 0; t < 4; t++) {
    const step = new THREE.Mesh(new THREE.BoxGeometry(264, 1.2, 4.4), structMat);
    step.position.set(0, 6 + t * 3.1, -4 + t * 4.4); stand.add(step);
  }
  const roof = new THREE.Mesh(new THREE.PlaneGeometry(272, 34), roofMat);
  roof.rotation.x = -Math.PI / 2 + 0.18; roof.position.set(0, 23.5, 2); stand.add(roof);
  const strip = new THREE.Mesh(new THREE.PlaneGeometry(272, 0.8),
    new THREE.MeshBasicMaterial({ color: 0xbfd4ff }));
  strip.position.set(0, 22.2, -13.4); strip.rotation.x = 0.12; stand.add(strip);
  {
    const n = 1600, p = new Float32Array(n * 3), c = new Float32Array(n * 3);
    const pal = [[0.9,0.9,0.95],[1,0.77,0.1],[0.3,0.5,1],[1,0.3,0.3],[0.3,0.9,0.6]];
    for (let i = 0; i < n; i++) {
      p[i * 3] = (rnd() - 0.5) * 258;
      const t = Math.floor(rnd() * 4);
      p[i * 3 + 1] = 7.2 + t * 3.1; p[i * 3 + 2] = -4 + t * 4.4 + (rnd() - 0.5) * 3;
      const cc = pal[Math.floor(rnd() * pal.length)];
      c[i * 3] = cc[0]; c[i * 3 + 1] = cc[1]; c[i * 3 + 2] = cc[2];
    }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(p, 3));
    g.setAttribute('color', new THREE.BufferAttribute(c, 3));
    crowd = new THREE.Points(g, new THREE.PointsMaterial({ size: 0.55, vertexColors: true }));
    stand.add(crowd);
  }
}

/* ------------------------------------------------------ start gantry */
const gantryLights = [];
{
  const sm = sampleAt(0);
  const grp = new THREE.Group();
  grp.position.copy(sm.pos);
  grp.lookAt(sm.pos.x + sm.tan.x * 10, 0, sm.pos.z + sm.tan.z * 10);
  const mat = new THREE.MeshStandardMaterial({ color: 0x1d2129, roughness: 0.6, metalness: 0.6 });
  for (const sd of [1, -1]) {
    const tower = new THREE.Mesh(new THREE.BoxGeometry(1.4, 12.5, 1.4), mat);
    tower.position.set(sd * (HALF_W + 2.2), 6.25, 0); grp.add(tower);
    for (let b = 0; b < 3; b++) {
      const brace = new THREE.Mesh(new THREE.BoxGeometry(0.5, 5.4, 0.5), mat);
      brace.position.set(sd * (HALF_W + 2.2), 3 + b * 3, 0);
      brace.rotation.z = sd * (b % 2 ? 0.5 : -0.5); grp.add(brace);
    }
  }
  const beam = new THREE.Mesh(new THREE.BoxGeometry((HALF_W + 2.2) * 2 + 1.4, 1.2, 1.2), mat);
  beam.position.y = 12.4; grp.add(beam);
  const banner = new THREE.Mesh(new THREE.PlaneGeometry(30, 3.4),
    new THREE.MeshBasicMaterial({ map: bannerTex, side: THREE.DoubleSide }));
  banner.position.y = 10.1; grp.add(banner);
  for (let i = 0; i < 5; i++) {
    const lamp = new THREE.Mesh(new THREE.SphereGeometry(0.42, 12, 10),
      new THREE.MeshBasicMaterial({ color: 0x330a08 }));
    lamp.position.set((i - 2) * 1.6, 7.9, 0); grp.add(lamp);
    gantryLights.push(lamp);
  }
  scene.add(grp);
}
function setGantry(k, green = false) {
  gantryLights.forEach((l, i) => {
    l.material.color.setHex(i < k ? (green ? 0x2dff7a : 0xff2d22) : 0x330a08);
  });
}

/* -------------------------------------------------- billboards + infield */
{
  const boards = [
    ['MOTORBALL', 'SCRAP FACTORY LEAGUE · ROUND 07', '#ffc400'],
    ['SCRAPYARD BOWL', 'RIDE THE BANKING · SURVIVE THE TRAPS', '#4d80ff'],
    ['FRONTIER GP', 'NIGHT SESSION · LIVE', '#ff5a4e'],
    ['ZALEM AWAITS', 'NEXT ELEVATOR 03:12 · WINNERS RIDE UP', '#51e6ff'],
  ];
  const postMat = new THREE.MeshStandardMaterial({ color: 0x1c2129, roughness: 0.7, metalness: 0.5 });
  boards.forEach(([title, sub, accent], i) => {
    const x = i < 2 ? 60 - i * 120 : -40 + i * 60, z = i % 2 ? 34 : -34;
    const post = new THREE.Mesh(new THREE.CylinderGeometry(0.6, 0.8, 9, 8), postMat);
    post.position.set(x, 4.5, z); scene.add(post);
    const b = new THREE.Mesh(new THREE.PlaneGeometry(26, 8),
      new THREE.MeshBasicMaterial({ map: billboardTex(title, sub, accent), side: THREE.DoubleSide }));
    b.position.set(x, 12.5, z); b.rotation.y = (i * 1.7) % Math.PI; scene.add(b);
  });
  const geo = new THREE.BoxGeometry(6, 2.6, 2.4);
  const mat = new THREE.MeshStandardMaterial({ roughness: 0.75, metalness: 0.35 });
  const COUNT = 30;
  const inst = new THREE.InstancedMesh(geo, mat, COUNT);
  const m4 = new THREE.Matrix4(), q = new THREE.Quaternion(), up = new THREE.Vector3(0, 1, 0);
  const pal = [0x7a4a22, 0x5a5f66, 0x3f6b4f, 0x74522a, 0x4a3f66].map((h) => new THREE.Color(h));
  for (let i = 0; i < COUNT; i++) {
    const inField = i < 22;
    const x = inField ? (rnd() - 0.5) * 430 : 300 + rnd() * 60;
    const z = inField ? (rnd() - 0.5) * 128 : (rnd() - 0.5) * 200;
    q.setFromAxisAngle(up, rnd() * TAU);
    m4.compose(new THREE.Vector3(x, 0.7, z), q, new THREE.Vector3(1, 1, 1));
    inst.setMatrixAt(i, m4);
    inst.setColorAt(i, pal[i % pal.length]);
  }
  inst.instanceMatrix.needsUpdate = true;
  if (inst.instanceColor) inst.instanceColor.needsUpdate = true;
  scene.add(inst);
}

/* ------------------------------------------------------------- trap gates */
const tmpV = new THREE.Vector3(), tmpV2 = new THREE.Vector3(), tmpV3 = new THREE.Vector3();
const tmpC = new THREE.Color();
const gunmetal = new THREE.MeshStandardMaterial({ color: 0x4b4f55, roughness: 0.5, metalness: 0.85 });
const rustMat = new THREE.MeshStandardMaterial({ color: 0x6b4a2e, roughness: 0.72, metalness: 0.6 });
const darkSteel = new THREE.MeshStandardMaterial({ color: 0x23262c, roughness: 0.55, metalness: 0.8 });
const bladeMat = new THREE.MeshStandardMaterial({ color: 0x2b2e34, roughness: 0.35, metalness: 0.95 });

function buildTrapGate(def) {
  const sg = def.f * TRACK_LEN;
  const sm = sampleAt(sg);
  const grp = new THREE.Group();
  grp.position.set(sm.pos.x, 0, sm.pos.z);
  grp.lookAt(sm.pos.x + sm.tan.x * 10, 0, sm.pos.z + sm.tan.z * 10);
  const R = 16, CY = 10.5;
  const ring = new THREE.Mesh(new THREE.TorusGeometry(R, 1.6, 12, 40), gunmetal);
  ring.position.y = CY; grp.add(ring);
  for (let i = 0; i < 10; i++) { // rusted cladding segments
    const a = (i / 10) * TAU;
    const seg = new THREE.Mesh(new THREE.BoxGeometry(3.4, 4.6, 4.4), i % 3 ? gunmetal : rustMat);
    seg.position.set(Math.cos(a) * R, CY + Math.sin(a) * R, 0);
    seg.rotation.z = a + Math.PI / 2; grp.add(seg);
  }
  for (const sd of [1, -1]) { // side machinery + legs + hydraulics
    const leg = new THREE.Mesh(new THREE.BoxGeometry(3.4, 13, 4.6), darkSteel);
    leg.position.set(sd * (R + 1.2), 4.5, 0); grp.add(leg);
    const housing = new THREE.Mesh(new THREE.BoxGeometry(4.6, 6.5, 5.4), rustMat);
    housing.position.set(sd * (R + 1.2), 12.5, 0); grp.add(housing);
    const ram = new THREE.Mesh(new THREE.CylinderGeometry(0.5, 0.5, 9, 8), bladeMat);
    ram.position.set(sd * (R - 2.5), 13, 0); ram.rotation.z = sd * 0.5; grp.add(ram);
    const drum = new THREE.Mesh(new THREE.CylinderGeometry(1.5, 1.5, 2.4, 12), gunmetal);
    drum.rotation.x = Math.PI / 2; drum.position.set(sd * (R - 1), CY, 2.8); grp.add(drum);
  }
  const motor = new THREE.Mesh(new THREE.BoxGeometry(6.5, 3.4, 5), darkSteel);
  motor.position.set(0, CY + R + 2.4, 0); grp.add(motor);
  for (const sx of [-2, 0.5]) {
    const stack = new THREE.Mesh(new THREE.CylinderGeometry(0.5, 0.65, 4.5, 8), rustMat);
    stack.position.set(sx, CY + R + 6, 0); grp.add(stack);
  }
  const hub = new THREE.Mesh(new THREE.CylinderGeometry(1.3, 1.3, 3.4, 12), gunmetal);
  hub.rotation.x = Math.PI / 2; hub.position.y = CY; grp.add(hub);
  const arms = [];
  for (let k = 0; k < 3; k++) {
    const arm = new THREE.Group(); arm.position.y = CY;
    const bar = new THREE.Mesh(new THREE.BoxGeometry(0.75, 8.5, 0.75), bladeMat);
    bar.position.y = 4.25; arm.add(bar);
    const tip = new THREE.Mesh(new THREE.ConeGeometry(0.6, 2.0, 8), bladeMat);
    tip.position.y = 9.4; arm.add(tip);
    for (const so of [-1.4, 1.4]) {
      const spike = new THREE.Mesh(new THREE.ConeGeometry(0.28, 1.4, 6), bladeMat);
      spike.position.set(so * 0.4, 6.4, so * 0.25); spike.rotation.x = so * 0.35; arm.add(spike);
    }
    const tipLamp = new THREE.Mesh(new THREE.SphereGeometry(0.22, 8, 8),
      new THREE.MeshBasicMaterial({ color: 0xff2d22 }));
    tipLamp.position.y = 8.4; arm.add(tipLamp);
    grp.add(arm); arms.push(arm);
  }
  const beaconMat = new THREE.MeshBasicMaterial({ color: 0x2dff7a });
  for (const [bx, by] of [[0, CY + R + 0.6], [R + 1.2, 16.2], [-(R + 1.2), 16.2]]) {
    const b = new THREE.Mesh(new THREE.SphereGeometry(0.5, 10, 8), beaconMat);
    b.position.set(bx, by, 2.6); grp.add(b);
    const b2 = b.clone(); b2.position.z = -2.6; grp.add(b2);
  }
  const warnLight = new THREE.PointLight(0xff2d22, 0, 60, 1);
  warnLight.position.set(0, CY + 4, 0); grp.add(warnLight);
  scene.add(grp);
  ribbonPatch(sg - 9, sg + 9, -0.8, 0.8,
    new THREE.MeshBasicMaterial({ map: gateStripTex, transparent: true, opacity: 0.92,
      depthWrite: false, polygonOffset: true, polygonOffsetFactor: -2 }));
  const stripGeo = new THREE.BoxGeometry(0.7, 0.22, 0.7);
  const stripMat = new THREE.MeshBasicMaterial({ color: 0xffe9a8 });
  for (const sd of [1, -1]) for (let i = 0; i < 8; i++) {
    const lamp = new THREE.Mesh(stripGeo, stripMat);
    roadPoint(sg - 8 + (i / 7) * 16, sd * 0.93, tmpV);
    lamp.position.set(tmpV.x, tmpV.y + 0.15, tmpV.z);
    scene.add(lamp);
  }
  return { def, sg, group: grp, arms, beaconMat, warnLight, cooldown: 0, whooshed: false };
}
const gates = GATES.map(buildTrapGate);

function gateAngles(g, t) {
  const out = [];
  for (let k = 0; k < 3; k++) out.push(g.def.phase + g.def.speed * t + k * (TAU / 3));
  return out;
}
function gateState(g, t) {
  const angs = gateAngles(g, t);
  let blocked = false, closing = false;
  for (const a of angs) {
    const d = Math.abs(wrapAngle(a + Math.PI / 2));
    if (d < 0.42) blocked = true; else if (d < 1.15) closing = true;
  }
  return { blocked, closing };
}

/* ---------------------------------------------------------------- skaters */
function makeSkater(suit, visor, accent) {
  const grp = new THREE.Group();
  const suitMat = new THREE.MeshStandardMaterial({ color: suit, roughness: 0.5, metalness: 0.4 });
  const darkMat = new THREE.MeshStandardMaterial({ color: 0x14161a, roughness: 0.6, metalness: 0.5 });
  const deck = new THREE.Mesh(new THREE.BoxGeometry(0.52, 0.12, 1.5), darkMat);
  deck.position.y = 0.2; grp.add(deck);
  const wheels = [];
  const wheelGeo = new THREE.CylinderGeometry(0.09, 0.09, 0.1, 10);
  wheelGeo.rotateZ(Math.PI / 2);
  for (const sx of [-0.22, 0.22]) {
    const pod = new THREE.Mesh(new THREE.BoxGeometry(0.18, 0.14, 0.72), darkMat);
    pod.position.set(sx, 0.14, 0); grp.add(pod);
    for (const sz of [-0.24, 0, 0.24]) {
      const w = new THREE.Mesh(wheelGeo, bladeMat);
      w.position.set(sx, 0.09, sz); grp.add(w); wheels.push(w);
    }
  }
  for (const sx of [-0.22, 0.22]) {
    const leg = new THREE.Mesh(new THREE.BoxGeometry(0.16, 0.52, 0.2), suitMat);
    leg.position.set(sx, 0.48, -0.05); leg.rotation.x = 0.25; grp.add(leg);
  }
  const torso = new THREE.Mesh(new THREE.BoxGeometry(0.5, 0.62, 0.3), suitMat);
  torso.position.set(0, 0.98, 0.08); torso.rotation.x = 0.38; grp.add(torso);
  const stripe = new THREE.Mesh(new THREE.BoxGeometry(0.52, 0.07, 0.32),
    new THREE.MeshBasicMaterial({ color: accent }));
  stripe.position.set(0, 1.12, -0.03); stripe.rotation.x = 0.38; grp.add(stripe);
  const armGeo = new THREE.BoxGeometry(0.13, 0.56, 0.16);
  const armL = new THREE.Mesh(armGeo, suitMat); armL.position.set(-0.34, 0.95, 0.1); grp.add(armL);
  const armR = new THREE.Mesh(armGeo, suitMat); armR.position.set(0.34, 0.95, 0.1); grp.add(armR);
  const head = new THREE.Mesh(new THREE.SphereGeometry(0.16, 12, 10), darkMat);
  head.position.set(0, 1.42, 0.3); grp.add(head);
  const visorM = new THREE.Mesh(new THREE.BoxGeometry(0.2, 0.08, 0.06),
    new THREE.MeshBasicMaterial({ color: visor }));
  visorM.position.set(0, 1.44, 0.44); grp.add(visorM);
  grp.rotation.order = 'YXZ';
  scene.add(grp);
  return { group: grp, wheels, torso, armL, armR, head };
}
const player = {
  rig: makeSkater(0x1f5eff, 0x9fd0ff, 0x9fd0ff),
  s: -15, u: 0, v: 0, steerVis: 0, spin: 0, scraping: false,
};
{
  const hl = new THREE.SpotLight(0xcfe4ff, 50, 110, 0.5, 0.5, 1);
  hl.position.set(0, 1.3, 0.6);
  hl.target.position.set(0, 0.4, 26);
  player.rig.group.add(hl); player.rig.group.add(hl.target);
  const lampM = new THREE.Mesh(new THREE.BoxGeometry(0.24, 0.1, 0.06),
    new THREE.MeshBasicMaterial({ color: 0xd8e9ff }));
  lampM.position.set(0, 1.05, 0.32); player.rig.group.add(lampM);
}
const ais = AI_DEFS.map((d) => ({
  def: d, rig: makeSkater(d.suit, d.visor, d.visor),
  s: d.s0, prevS: d.s0, u: 0, v: d.base, lap: 0, steerVis: 0,
}));
function setTransform(s, u, rig, yawExtra, leanExtra, t, rollDist) {
  const sm = sampleAt(s);
  tmpV3.copy(sm.pos).addScaledVector(sm.left, u * HALF_W);
  const y = bowlY(u);
  rig.group.position.set(tmpV3.x, y + 0.03, tmpV3.z);
  rig.group.rotation.y = sm.head + yawExtra;
  const slope = Math.atan(bowlSlope(u));
  rig.group.rotation.z = slope * 0.92 + leanExtra;
  rig.group.rotation.x = 0;
  for (const w of rig.wheels) w.rotation.x += rollDist / 0.09;
  rig.torso.position.y = 0.98 + Math.sin(t * 27) * 0.008;
  rig.armL.rotation.x = -0.5 - yawExtra * 2.2;
  rig.armR.rotation.x = -0.5 + yawExtra * 2.2;
  return sm;
}


/* ----------------------------------------------------------------- sparks */
const SPARKS = 420;
const sparkPos = new Float32Array(SPARKS * 3);
const sparkCol = new Float32Array(SPARKS * 3);
const sparkVel = new Float32Array(SPARKS * 3);
const sparkLife = new Float32Array(SPARKS);
for (let i = 0; i < SPARKS; i++) sparkPos[i * 3 + 1] = -999;
const sparkGeo = new THREE.BufferGeometry();
sparkGeo.setAttribute('position', new THREE.BufferAttribute(sparkPos, 3).setUsage(THREE.DynamicDrawUsage));
sparkGeo.setAttribute('color', new THREE.BufferAttribute(sparkCol, 3).setUsage(THREE.DynamicDrawUsage));
scene.add(new THREE.Points(sparkGeo, new THREE.PointsMaterial({ size: 0.4, vertexColors: true,
  transparent: true, opacity: 0.95, depthWrite: false, blending: THREE.AdditiveBlending })));
let sparkHead = 0;
function spawnSparks(origin, n, colorHex, speed = 7, up = 4) {
  tmpC.setHex(colorHex);
  for (let k = 0; k < n; k++) {
    const i = sparkHead; sparkHead = (sparkHead + 1) % SPARKS;
    sparkPos[i * 3] = origin.x; sparkPos[i * 3 + 1] = origin.y + 0.3; sparkPos[i * 3 + 2] = origin.z;
    sparkVel[i * 3] = (Math.random() - 0.5) * speed * 2;
    sparkVel[i * 3 + 1] = Math.random() * up;
    sparkVel[i * 3 + 2] = (Math.random() - 0.5) * speed * 2;
    sparkCol[i * 3] = tmpC.r; sparkCol[i * 3 + 1] = tmpC.g; sparkCol[i * 3 + 2] = tmpC.b;
    sparkLife[i] = 0.4 + Math.random() * 0.5;
  }
}
function updateSparks(dt) {
  for (let i = 0; i < SPARKS; i++) {
    if (sparkLife[i] <= 0) continue;
    sparkLife[i] -= dt;
    if (sparkLife[i] <= 0) { sparkPos[i * 3 + 1] = -999; continue; }
    sparkVel[i * 3 + 1] -= 12 * dt;
    sparkPos[i * 3] += sparkVel[i * 3] * dt;
    sparkPos[i * 3 + 1] += sparkVel[i * 3 + 1] * dt;
    sparkPos[i * 3 + 2] += sparkVel[i * 3 + 2] * dt;
  }
  sparkGeo.attributes.position.needsUpdate = true;
  sparkGeo.attributes.color.needsUpdate = true;
}

/* ------------------------------------------------------------------ input */
const input = { fwd: false, back: false, left: false, right: false };
const KEYMAP = { KeyW: 'fwd', ArrowUp: 'fwd', KeyS: 'back', ArrowDown: 'back', KeyA: 'left', ArrowLeft: 'left', KeyD: 'right', ArrowRight: 'right' };
addEventListener('keydown', (e) => {
  if (KEYMAP[e.code]) { input[KEYMAP[e.code]] = true; e.preventDefault(); }
  else if (e.code === 'Digit1') setCam('chase');
  else if (e.code === 'Digit2') setCam('visor');
  else if (e.code === 'Digit3') setCam('track');
  else if (e.code === 'Digit4') setCam('orbit');
  else if (e.code === 'KeyC') cycleCam();
  else if (e.code === 'KeyR') resetRide();
  else if (e.code === 'KeyP') pauseGame(!paused);
  else if (e.code === 'KeyM') { const m = Audio8.toggleMute(); banner(m ? 'SOUND OFF' : 'SOUND ON', true); }
  else if (e.code === 'Enter' && phase === 'menu') $('ride').click();
});
addEventListener('keyup', (e) => { if (KEYMAP[e.code]) input[KEYMAP[e.code]] = false; });
let dragging = false, lastPX = 0, lastPY = 0, lastDragT = -9;
renderer.domElement.addEventListener('pointerdown', (e) => {
  dragging = true; lastPX = e.clientX; lastPY = e.clientY;
  renderer.domElement.setPointerCapture(e.pointerId);
});
addEventListener('pointermove', (e) => {
  if (!dragging) return;
  const dx = e.clientX - lastPX, dy = e.clientY - lastPY;
  lastPX = e.clientX; lastPY = e.clientY; lastDragT = tNow;
  if (camMode === 'chase') {
    chaseYaw = clamp(chaseYaw - dx * 0.005, -1.2, 1.2);
    chasePitch = clamp(chasePitch + dy * 0.003, -0.25, 0.5);
  } else if (camMode === 'orbit') {
    orbitYaw -= dx * 0.008; orbitPitch = clamp(orbitPitch + dy * 0.006, -0.1, 1.2);
  }
});
addEventListener('pointerup', () => { dragging = false; });
addEventListener('wheel', (e) => {
  if (camMode === 'chase') chaseDist = clamp(chaseDist + Math.sign(e.deltaY) * 1.2, 6, 17);
  else if (camMode === 'orbit') orbitDist = clamp(orbitDist + Math.sign(e.deltaY) * 2.5, 6, 60);
}, { passive: true });
function bindTouch(id, key) {
  const el = $(id);
  const on = (e) => { e.preventDefault(); input[key] = true; };
  const off = (e) => { e.preventDefault(); input[key] = false; };
  el.addEventListener('touchstart', on, { passive: false });
  el.addEventListener('touchend', off, { passive: false });
  el.addEventListener('touchcancel', off, { passive: false });
}
bindTouch('tG', 'fwd'); bindTouch('tB', 'back'); bindTouch('tL', 'left'); bindTouch('tR', 'right');
$('tcam').addEventListener('click', () => cycleCam());
if ('ontouchstart' in window) document.body.classList.add('touchmode');

/* ----------------------------------------------------------------- cameras */
let camMode = 'chase';
let chaseYaw = 0, chasePitch = 0.12, chaseDist = 10.5;
let orbitYaw = 0.6, orbitPitch = 0.35, orbitDist = 22;
const camPos = new THREE.Vector3(0, 30, -140), camLook = new THREE.Vector3();
let camSnap = true, shakeT = 0, trackCamIdx = -1, trackCamHold = 0;
const CAMS = ['chase', 'visor', 'track', 'orbit'];
function setCam(m) {
  camMode = m; camSnap = true;
  document.body.dataset.cam = m === 'visor' ? 'visor' : (m === 'track' ? 'track' : 'chase');
  const hide = m === 'visor'; // hide bodywork only — keep the headlight burning
  player.rig.group.children.forEach((o) => { if (!o.isLight) o.visible = !hide; });
}
function cycleCam() { setCam(CAMS[(CAMS.indexOf(camMode) + 1) % CAMS.length]); }
const bcams = [];
for (let k = 0; k < 6; k++) {
  const s = (k / 6) * TRACK_LEN;
  const sm = sampleAt(s);
  const out = new THREE.Vector3().subVectors(sm.pos, loopCenter); out.y = 0; out.normalize();
  bcams.push(new THREE.Vector3().copy(sm.pos).addScaledVector(out, 30).add(new THREE.Vector3(0, 17, 0)));
}
const camFwd = new THREE.Vector3(), camUp = new THREE.Vector3(0, 1, 0), camTmp = new THREE.Vector3();
function updateCamera(dt) {
  const pp = player.rig.group.position;
  const sm = sampleAt(player.s);
  const v = Math.abs(player.v);
  let fov = 64;
  if (camMode === 'chase') {
    if (tNow - lastDragT > 2.5) { chaseYaw *= Math.exp(-dt * 1.5); chasePitch += (0.12 - chasePitch) * dt; }
    const yaw = sm.head + Math.PI + chaseYaw;
    const dist = chaseDist + v * 0.045, h = 3.4 + v * 0.01 + chasePitch * 8;
    camTmp.set(pp.x + Math.sin(yaw) * dist, pp.y + h, pp.z + Math.cos(yaw) * dist);
    const k = camSnap ? 1 : 1 - Math.exp(-dt * 5.5);
    camPos.lerp(camTmp, k);
    camFwd.set(pp.x - Math.sin(yaw) * 9, pp.y + 1.4, pp.z - Math.cos(yaw) * 9);
    camLook.lerp(camFwd, camSnap ? 1 : 1 - Math.exp(-dt * 8));
    fov = Math.min(80, 62 + v * 0.14);
  } else if (camMode === 'visor') {
    camPos.set(pp.x + sm.tan.x * 0.4, pp.y + 1.62, pp.z + sm.tan.z * 0.4);
    camLook.set(pp.x + sm.tan.x * 30, pp.y + 1.1, pp.z + sm.tan.z * 30);
    fov = Math.min(82, 70 + v * 0.1);
  } else if (camMode === 'track') {
    const region = Math.floor(wrapS(player.s) / TRACK_LEN * 6) % 6;
    trackCamHold -= dt;
    if ((region !== trackCamIdx && trackCamHold <= 0) || camSnap) {
      trackCamIdx = region; trackCamHold = 3; camSnap = true;
    }
    camPos.lerp(bcams[trackCamIdx], camSnap ? 1 : 1 - Math.exp(-dt * 3));
    camLook.lerp(camTmp.set(pp.x, pp.y + 1.2, pp.z), camSnap ? 1 : 1 - Math.exp(-dt * 6));
    $('camtag').textContent = 'TRACKSIDE · CAM ' + (trackCamIdx + 1);
    fov = 50;
  } else {
    const cx = pp.x + Math.sin(orbitYaw) * Math.cos(orbitPitch) * orbitDist;
    const cz = pp.z + Math.cos(orbitYaw) * Math.cos(orbitPitch) * orbitDist;
    camTmp.set(cx, pp.y + Math.sin(orbitPitch) * orbitDist + 1.5, cz);
    camPos.lerp(camTmp, camSnap ? 1 : 1 - Math.exp(-dt * 6));
    camLook.lerp(camTmp.set(pp.x, pp.y + 1, pp.z), camSnap ? 1 : 1 - Math.exp(-dt * 8));
    fov = 55;
  }
  camSnap = false;
  camera.position.copy(camPos);
  if (shakeT > 0.003) {
    camera.position.x += (Math.random() - 0.5) * shakeT * 1.2;
    camera.position.y += (Math.random() - 0.5) * shakeT * 0.9;
    shakeT *= Math.exp(-dt * 4);
  }
  camera.up.set(0, 1, 0);
  camera.lookAt(camLook);
  if (camMode === 'visor') camera.rotation.z += player.rig.group.rotation.z * 0.55;
  if (Math.abs(camera.fov - fov) > 0.1) { camera.fov = fov; camera.updateProjectionMatrix(); }
}
/* ---------------------------------------------------------- race state */
let phase = 'menu', paused = false, tNow = 0;
let raceT = 0, lapStart = 0, laps = [], finished = false, lastGate = -21;
let countT = 0, countNum = 0, goShown = false, wrongWayT = 0, wrongShown = false;
let standTimer = 0, alarmOn = false, bannerT = null;

function fmt(t) {
  t = Math.max(0, t);
  const m = Math.floor(t / 60), s = Math.floor(t % 60), d = Math.floor((t % 1) * 10);
  return m + ':' + String(s).padStart(2, '0') + '.' + d;
}
function banner(text, cold = false) {
  $('bannertext').textContent = text;
  $('banner').classList.toggle('cold', cold);
  $('banner').classList.add('show');
  clearTimeout(bannerT);
  bannerT = setTimeout(hideBanner, 2300);
}
function hideBanner() { $('banner').classList.remove('show'); }
function flash() {
  const f = $('flash');
  f.style.opacity = '1';
  setTimeout(() => { f.style.opacity = '0'; }, 140);
}
function showCount(txt, go = false) {
  $('count').innerHTML = '<b class="' + (go ? 'go ' : '') + 'pop">' + txt + '</b>';
}
function resetRace() {
  player.s = -15; player.u = 0; player.v = 0; player.spin = 0; player.steerVis = 0;
  ais.forEach((ai) => { ai.s = ai.def.s0; ai.prevS = ai.s; ai.v = 0; ai.lap = 0; ai.u = 0; });
  laps = []; lapStart = 0; raceT = 0; finished = false; lastGate = -21;
  wrongWayT = 0; wrongShown = false;
  setGantry(0); hideBanner();
  $('finish').classList.remove('show');
}
function startCountdown() {
  resetRace();
  phase = 'count'; countT = 3.6; countNum = 0; goShown = false; camSnap = true;
}
function updateCountdown(dt) {
  countT -= dt;
  if (countT > 0.6) {
    const num = Math.ceil(countT - 0.6);
    if (num !== countNum && num >= 1 && num <= 3) {
      countNum = num; showCount(String(num)); Audio8.beep(440, 0.18, 'sine', 0.3);
      setGantry(5 - num);
    }
  } else if (!goShown) {
    goShown = true; showCount('GO', true); Audio8.beep(880, 0.5, 'sine', 0.3);
    setGantry(5, true); phase = 'race'; raceT = 0; lapStart = 0;
    setTimeout(() => setGantry(0), 2500);
  }
}
function onLap() {
  const t = raceT - lapStart;
  lapStart = raceT;
  if (finished) { banner('LAP ' + fmt(t) + ' · FREE RIDE', true); return; }
  laps.push(t);
  if (laps.length >= RACE_LAPS) { finishRace(); return; }
  banner('LAP ' + (laps.length + 1) + ' · LAST ' + fmt(t), true);
  Audio8.beep(660, 0.2, 'sine', 0.2);
}
function finishRace() {
  finished = true;
  Audio8.beep(880, 0.15); Audio8.beep(1100, 0.3, 'sine', 0.25, 0.15);
  let bi = 0;
  laps.forEach((t, i) => { if (t < laps[bi]) bi = i; });
  const tb = $('finishtable'); tb.innerHTML = '';
  laps.forEach((t, i) => {
    const tr = document.createElement('tr');
    if (i === bi) tr.className = 'best';
    tr.innerHTML = '<td>LAP ' + (i + 1) + (i === bi ? ' ★' : '') + '</td><td>' + fmt(t) + '</td>';
    tb.appendChild(tr);
  });
  $('finishbest').textContent = 'BEST ' + fmt(laps[bi]);
  $('finish').classList.add('show');
}
function resetRide() {
  if (phase === 'menu') return;
  if (finished || $('finish').classList.contains('show')) { startCountdown(); return; }
  player.s = lastGate + 6; player.u = 0; player.v = 12; player.spin = 0;
  banner('RESET · BACK ON TRACK', true);
}
function pauseGame(on) {
  if (phase !== 'race' && phase !== 'count') return;
  paused = on;
  if (Audio8.ctx) { if (on) Audio8.ctx.suspend(); else Audio8.ctx.resume(); }
  banner('PAUSED · PRESS P', true);
  if (!on) hideBanner();
}
function trapHit(g) {
  player.v *= 0.32; player.spin = TAU; shakeT = 1; flash();
  spawnSparks(player.rig.group.position, 42, 0xff5030, 12, 7);
  banner('TRAP HIT — GATE ' + g.def.name, false);
  Audio8.thud(); g.cooldown = 2.5;
}

/* ------------------------------------------------------------ player */
function updatePlayer(dt) {
  const P = player, locked = phase !== 'race';
  if (!locked) {
    if (input.fwd) P.v += (30 - P.v * 0.28) * 1.6 * dt;
    if (input.back) P.v -= (P.v > 1 ? 55 : 18) * dt;
    P.v -= P.v * 0.06 * dt;
    if (!input.fwd && !input.back) {
      const sgn = Math.sign(P.v);
      P.v -= sgn * 3 * dt;
      if (Math.sign(P.v) !== sgn) P.v = 0;
    }
    P.v = clamp(P.v, -12, TOP_SPEED);
  } else { P.v = 0; }
  const steerInput = locked ? 0 : ((input.left ? 1 : 0) - (input.right ? 1 : 0));
  P.u += steerInput * 0.95 * dt;
  P.steerVis += (steerInput - P.steerVis) * (1 - Math.exp(-dt * 10));
  const sm0 = sampleAt(P.s);
  const grip = 1 - 0.55 * Math.min(1, Math.abs(P.u));
  P.u -= sm0.curv * P.v * Math.abs(P.v) * 0.0026 * grip * dt;
  P.scraping = false;
  if (P.u > 0.965 || P.u < -0.965) {
    const sd = P.u > 0 ? 1 : -1;
    P.u = sd * 0.965;
    if (P.v > 8) {
      P.v -= P.v * 0.8 * dt;
      roadPoint(P.s, sd * 0.97, tmpV); tmpV.y += 0.4;
      spawnSparks(tmpV, 2, 0xffc400, 5, 3);
      P.scraping = true;
      shakeT = Math.max(shakeT, 0.18);
    }
  }
  P.s += P.v * dt;
  if (P.s >= TRACK_LEN) { P.s -= TRACK_LEN; if (!locked) onLap(); }
  else if (P.s < -TRACK_LEN * 0.5) P.s += TRACK_LEN;
  P.spin = Math.max(0, P.spin - dt * 7);
  for (const g of gates) {
    g.cooldown = Math.max(0, g.cooldown - dt);
    const d = wrapDist(P.s, g.sg);
    if (Math.abs(d) < 4 && P.v > 0) lastGate = g.sg;
    const st = gateState(g, tNow);
    if (Math.abs(d) < 7 && st.blocked && g.cooldown <= 0 && Math.abs(P.u) < 0.92 && P.v > 5 && !locked) {
      trapHit(g);
    } else if (Math.abs(d) < 7 && !st.blocked && !g.whooshed && P.v > 5) {
      const angs = gateAngles(g, tNow);
      if (angs.some((a) => Math.abs(wrapAngle(a + Math.PI / 2)) < 0.9)) {
        Audio8.whoosh(); g.whooshed = true;
      }
    }
    if (Math.abs(d) > 30) g.whooshed = false;
  }
  const tip = -P.steerVis * 0.2;
  setTransform(P.s, P.u, P.rig, P.steerVis * 0.16 + P.spin, tip, tNow, P.v * dt);
  if (P.v < -1.5) wrongWayT += dt; else { wrongWayT = 0; wrongShown = false; }
  if (wrongWayT > 0.8 && !wrongShown) { wrongShown = true; banner('WRONG WAY', false); }
  $('speed').textContent = Math.round(Math.abs(P.v) * 3.6);
  $('motorfill').style.width = (Math.abs(P.v) / TOP_SPEED * 100).toFixed(1) + '%';
  $('lapTime').textContent = fmt(phase === 'race' ? raceT - lapStart : 0);
  $('lapNum').textContent = Math.min(laps.length + 1, RACE_LAPS);
  const best = laps.length ? Math.min(...laps) : 0;
  $('bestTime').textContent = best ? fmt(best) : '--:--.-';
  let bestG = null, bestD = 1e9;
  for (const g of gates) {
    const d = wrapDist(g.sg, P.s);
    if (d > -14 && d < 110 && d < bestD) { bestG = g; bestD = d; }
  }
  const pill = $('trap');
  alarmOn = false;
  if (!bestG || locked) { pill.classList.remove('show', 'warn', 'danger'); }
  else {
    const st = gateState(bestG, tNow);
    pill.classList.add('show');
    pill.classList.toggle('warn', st.closing && !st.blocked);
    pill.classList.toggle('danger', st.blocked);
    $('traptext').textContent = 'TRAP GATE ' + bestG.def.name + ' · ' +
      (st.blocked ? 'BLOCKED' : st.closing ? 'BLADES CYCLING' : 'OPEN') + ' · ' + Math.max(0, Math.round(bestD)) + 'M';
    alarmOn = (st.blocked || st.closing) && bestD < 95;
  }
}

/* ------------------------------------------------------------------ ai */
function updateAI(dt, racing) {
  for (const ai of ais) {
    const d = wrapDist(player.s, ai.s);
    let target = ai.def.base * (1 + 0.05 * Math.sin(tNow * 0.5 + ai.def.ph));
    target += clamp(d * 0.06, -9, 9);
    ai.v += clamp(target - ai.v, -20 * dt, 12 * dt);
    if (!racing) ai.v = Math.max(0, ai.v - 30 * dt);
    ai.prevS = ai.s; ai.s += ai.v * dt;
    if (ai.s >= TRACK_LEN) { ai.s -= TRACK_LEN; ai.lap++; }
    ai.u = 0.5 * Math.sin(tNow * 0.35 + ai.def.ph * 1.3);
    ai.steerVis = Math.cos(tNow * 0.35 + ai.def.ph * 1.3) * 0.5;
    setTransform(ai.s, ai.u, ai.rig, ai.steerVis * 0.25, -ai.steerVis * 0.15, tNow, ai.v * dt);
  }
}
function updateGatesVisual(t) {
  for (const g of gates) {
    const angs = gateAngles(g, t);
    for (let k = 0; k < 3; k++) g.arms[k].rotation.z = angs[k];
    const st = gateState(g, t);
    g.beaconMat.color.setHex(st.blocked ? 0xff2d22 : st.closing ? 0xffc400 : 0x2dff7a);
    g.warnLight.intensity = st.blocked ? 26 : st.closing ? 8 : 0;
  }
}

/* ---------------------------------------------------------- map + board */
const mapCtx = $('map').getContext('2d');
const mapB = (() => {
  let x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9;
  for (let i = 0; i < NS; i += 4) {
    const p = sPts[i];
    x0 = Math.min(x0, p.x); x1 = Math.max(x1, p.x);
    z0 = Math.min(z0, p.z); z1 = Math.max(z1, p.z);
  }
  return { x0, x1, z0, z1 };
})();
function mapXY(x, z, W, H, pad) {
  const sx = (W - 2 * pad) / (mapB.x1 - mapB.x0), sz = (H - 2 * pad) / (mapB.z1 - mapB.z0);
  const s = Math.min(sx, sz);
  return [pad + (x - mapB.x0) * s + ((W - 2 * pad) - (mapB.x1 - mapB.x0) * s) / 2,
          pad + (z - mapB.z0) * s + ((H - 2 * pad) - (mapB.z1 - mapB.z0) * s) / 2];
}
function drawMap() {
  const W = 216, H = 140;
  mapCtx.setTransform(2, 0, 0, 2, 0, 0);
  mapCtx.clearRect(0, 0, W, H);
  mapCtx.lineJoin = 'round'; mapCtx.lineCap = 'round';
  for (const [w, col] of [[9, '#232b36'], [5.5, '#3d4654']]) {
    mapCtx.strokeStyle = col; mapCtx.lineWidth = w; mapCtx.beginPath();
    for (let i = 0; i <= NS; i += 6) {
      const p = sPts[i % NS];
      const [x, y] = mapXY(p.x, p.z, W, H, 12);
      if (i === 0) mapCtx.moveTo(x, y); else mapCtx.lineTo(x, y);
    }
    mapCtx.closePath(); mapCtx.stroke();
  }
  const [sx, sy] = mapXY(sPts[0].x, sPts[0].z, W, H, 12);
  mapCtx.fillStyle = '#fff'; mapCtx.fillRect(sx - 3, sy - 3, 6, 6);
  for (const g of gates) {
    const p = roadPoint(g.sg, 0, tmpV2);
    const [x, y] = mapXY(p.x, p.z, W, H, 12);
    const st = gateState(g, tNow);
    mapCtx.fillStyle = st.blocked ? '#ff3b30' : st.closing ? '#ffc400' : '#34d17b';
    mapCtx.beginPath(); mapCtx.arc(x, y, 4, 0, TAU); mapCtx.fill();
  }
  for (const ai of ais) {
    const p = roadPoint(ai.s, ai.u, tmpV2);
    const [x, y] = mapXY(p.x, p.z, W, H, 12);
    mapCtx.fillStyle = '#' + ai.def.visor.toString(16).padStart(6, '0');
    mapCtx.beginPath(); mapCtx.arc(x, y, 3, 0, TAU); mapCtx.fill();
  }
  const pp = roadPoint(player.s, player.u, tmpV2);
  const [px, py] = mapXY(pp.x, pp.z, W, H, 12);
  const sm = sampleAt(player.s);
  mapCtx.strokeStyle = '#fff'; mapCtx.lineWidth = 2;
  mapCtx.beginPath(); mapCtx.moveTo(px, py);
  mapCtx.lineTo(px + sm.tan.x * 10, py + sm.tan.z * 10); mapCtx.stroke();
  mapCtx.fillStyle = '#fff';
  mapCtx.beginPath(); mapCtx.arc(px, py, 3.4, 0, TAU); mapCtx.fill();
}
function raceProg(lap, s) { return lap * TRACK_LEN + (s < 0 ? s + TRACK_LEN : s); }
function updateStandings() {
  const rows = [{ n: 'YOU', c: '#fff', prog: raceProg(laps.length, player.s), me: true }];
  ais.forEach((ai) => rows.push({ n: ai.def.name, c: '#' + ai.def.visor.toString(16).padStart(6, '0'), prog: raceProg(ai.lap, ai.s) }));
  rows.sort((a, b) => b.prog - a.prog);
  const lead = rows[0].prog;
  $('standrows').innerHTML = rows.map((r, i) => {
    const gap = i === 0 ? 'LEAD' : '+' + Math.round(lead - r.prog) + 'm';
    return '<div class="row' + (r.me ? ' me' : '') + '"><span class="sw" style="background:' + r.c + '"></span>' + (i + 1) + ' · ' + r.n + '<span class="gap">' + gap + '</span></div>';
  }).join('');
  const meIdx = rows.findIndex((r) => r.me);
  const sector = Math.floor(wrapS(player.s) / TRACK_LEN * 3) + 1;
  $('mapstat').textContent = 'SECTOR ' + sector + ' · P' + (meIdx + 1) + '/4';
}

/* ------------------------------------------------------------ main loop */
let lastT = performance.now();
function frame() {
  requestAnimationFrame(frame);
  const now = performance.now();
  const dt = Math.min((now - lastT) / 1000, 0.05);
  lastT = now;
  if (!paused) {
    tNow += dt;
    if (phase === 'count') updateCountdown(dt);
    if (phase === 'race') raceT += dt;
    updatePlayer(dt);
    updateAI(dt, phase === 'race');
    updateGatesVisual(tNow);
    updateSparks(dt);
    updateCamera(dt);
    zalem.position.y = 300 + Math.sin(tNow * 0.18) * 4; // sky-city hover
    for (const s of searchlights) {
      s.userData.cone.rotation.x = Math.sin(tNow * 0.4 + s.userData.ph) * 0.1;
      s.userData.cone.rotation.z = Math.cos(tNow * 0.3 + s.userData.ph) * 0.1;
    }
    for (const b of beacons) b.material.opacity = 0.35 + 0.55 * Math.abs(Math.sin(tNow * 2 + b.userData.ph));
    if (crowd) crowd.material.size = 0.55 + Math.sin(tNow * 7) * 0.06;
    drawMap();
    standTimer -= dt;
    if (standTimer <= 0) { updateStandings(); standTimer = 0.3; }
    Audio8.update(player.v, input.fwd ? 1 : 0, player.scraping, alarmOn, tNow);
  }
  renderer.render(scene, camera);
}

/* ------------------------------------------------------------------ boot */
addEventListener('resize', () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
});
document.addEventListener('visibilitychange', () => { if (document.hidden) pauseGame(true); });
$('ride').addEventListener('click', () => {
  Audio8.init();
  $('menu').style.display = 'none';
  startCountdown();
});
$('btnAgain').addEventListener('click', () => { $('finish').classList.remove('show'); startCountdown(); });
$('btnCruise').addEventListener('click', () => { $('finish').classList.remove('show'); });
$('specLen').textContent = (TRACK_LEN / 1000).toFixed(2) + ' km';
console.log('[motorball] track length m:', TRACK_LEN.toFixed(1));
resetRace();
setTransform(player.s, 0, player.rig, 0, 0, 0, 0);
ais.forEach((ai) => setTransform(ai.s, 0, ai.rig, 0, 0, 0, 0));
updateCamera(0.016);
updateStandings();
frame();

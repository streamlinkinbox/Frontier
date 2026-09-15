import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { CSS2DRenderer, CSS2DObject } from 'three/addons/renderers/CSS2DRenderer.js';
import { buildRex, LEG, HIP_LOCAL } from './dino.js';
import { createGait } from './gait.js';
import { solveLeg } from './ik.js';
import { TailSim } from './tail.js';
import { buildHydraulics } from './hydro.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';
import { makeBoneTextures, makeGroundTextures, makeContactShadow } from './textures.js';
import { TAIL_LENS } from './dino.js';

// ---------------- renderer / scene / camera ----------------
const container = document.getElementById('scene-container');
const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.15;
renderer.outputColorSpace = THREE.SRGBColorSpace;
container.appendChild(renderer.domElement);

const labelRenderer = new CSS2DRenderer();
labelRenderer.setSize(window.innerWidth, window.innerHeight);
labelRenderer.domElement.style.position = 'absolute';
labelRenderer.domElement.style.top = '0';
labelRenderer.domElement.style.left = '0';
labelRenderer.domElement.style.pointerEvents = 'none';
labelRenderer.domElement.style.zIndex = '5';
container.appendChild(labelRenderer.domElement);

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x05070d);
scene.fog = new THREE.Fog(0x05070d, 22, 60);

// image-based lighting (no external HDR asset needed)
{
  const pmrem = new THREE.PMREMGenerator(renderer);
  scene.environment = pmrem.fromScene(new RoomEnvironment(renderer), 0.04).texture;
  pmrem.dispose();
}
const boneTex = makeBoneTextures();
const groundTex = makeGroundTextures();

const camera = new THREE.PerspectiveCamera(50, window.innerWidth / window.innerHeight, 0.1, 250);
const CAM_START = new THREE.Vector3(18, 10, 22);
const CAM_HOME = new THREE.Vector3(12.5, 3.8, 3.5);
camera.position.copy(CAM_START);

const controls = new OrbitControls(camera, renderer.domElement);
controls.target.set(0, 2.6, 0.3);
controls.enableDamping = true;
controls.dampingFactor = 0.06;
controls.minDistance = 2;
controls.maxDistance = 40;
controls.maxPolarAngle = 1.5;
controls.enabled = false;

// ---------------- lights ----------------
scene.add(new THREE.HemisphereLight(0x8fb7ff, 0x1a1208, 0.42));
const key = new THREE.DirectionalLight(0xffe0b3, 1.5);
key.position.set(9, 15, 7);
key.castShadow = true;
key.shadow.mapSize.set(2048, 2048);
key.shadow.camera.left = -12; key.shadow.camera.right = 12;
key.shadow.camera.top = 12; key.shadow.camera.bottom = -12;
key.shadow.camera.near = 1; key.shadow.camera.far = 45;
key.shadow.bias = -0.0005;
scene.add(key);
const rim = new THREE.DirectionalLight(0x33ccff, 0.9);
rim.position.set(-9, 7, -11);
scene.add(rim);

// ---------------- environment: lab + treadmill ----------------
{
  const floor = new THREE.Mesh(
    new THREE.CircleGeometry(32, 64),
    new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.94, map: groundTex.map, envMapIntensity: 0.3 })
  );
  floor.rotation.x = -Math.PI / 2;
  floor.position.y = -0.06;
  floor.receiveShadow = true;
  scene.add(floor);
}
const grid = new THREE.GridHelper(60, 60, 0x1d3f4d, 0x12242d);
grid.position.y = -0.05;
scene.add(grid);

// treadmill deck
const beltGroup = new THREE.Group();
scene.add(beltGroup);
{
  const deckMat = new THREE.MeshStandardMaterial({ color: 0x11161f, roughness: 0.9 });
  const deck = new THREE.Mesh(new THREE.BoxGeometry(8, 0.1, 20), deckMat);
  deck.position.y = -0.05;
  deck.receiveShadow = true;
  beltGroup.add(deck);
  const railMat = new THREE.MeshStandardMaterial({ color: 0x2a3345, roughness: 0.4, metalness: 0.8 });
  for (const s of [1, -1]) {
    const rail = new THREE.Mesh(new THREE.BoxGeometry(0.3, 0.3, 20), railMat);
    rail.position.set(s * 4.3, 0.1, 0);
    rail.castShadow = true;
    beltGroup.add(rail);
    for (let i = -2; i <= 2; i++) {
      const post = new THREE.Mesh(new THREE.BoxGeometry(0.12, 0.9, 0.12), railMat);
      post.position.set(s * 4.3, 0.5, i * 4);
      beltGroup.add(post);
      const led = new THREE.Mesh(
        new THREE.BoxGeometry(0.14, 0.06, 0.14),
        new THREE.MeshBasicMaterial({ color: 0xffb454 })
      );
      led.position.set(s * 4.3, 0.98, i * 4);
      beltGroup.add(led);
    }
  }
}
const SLAT_SPAN = 20, SLAT_N = 30;
const slats = [];
{
  const mA = new THREE.MeshBasicMaterial({ color: 0x1c5a6e });
  const mB = new THREE.MeshBasicMaterial({ color: 0x123844 });
  const g = new THREE.BoxGeometry(7.4, 0.03, 0.3);
  for (let i = 0; i < SLAT_N; i++) {
    const s = new THREE.Mesh(g, i % 2 ? mA : mB);
    s.position.set(0, 0.015, -SLAT_SPAN / 2 + (i / SLAT_N) * SLAT_SPAN);
    beltGroup.add(s);
    slats.push(s);
  }
}

// soft contact shadow following the animal
const contact = new THREE.Mesh(
  new THREE.PlaneGeometry(8, 13),
  new THREE.MeshBasicMaterial({ map: makeContactShadow(), transparent: true, opacity: 0.55, depthWrite: false })
);
contact.rotation.x = -Math.PI / 2;
contact.position.set(0, 0.045, -0.5);
scene.add(contact);
// rocks / fossils / pillars / stars (set dressing)
{
  const rockMat = new THREE.MeshStandardMaterial({ color: 0x2a3242, roughness: 0.9, flatShading: true });
  for (let i = 0; i < 14; i++) {
    const a = (i / 14) * Math.PI * 2 + Math.random() * 0.4;
    const r = 17 + Math.random() * 10;
    const rock = new THREE.Mesh(new THREE.DodecahedronGeometry(0.6 + Math.random() * 1.8, 0), rockMat);
    rock.position.set(Math.cos(a) * r, 0.2 + Math.random() * 0.6, Math.sin(a) * r);
    rock.rotation.set(Math.random() * 3, Math.random() * 3, Math.random() * 3);
    rock.castShadow = true;
    scene.add(rock);
  }
  const fossilMat = new THREE.MeshStandardMaterial({ color: 0x8a7c5c, roughness: 0.85 });
  for (const [x, ry, z] of [[17, 0.6, 3], [-16, 1.2, -6], [7, 2.4, -17]]) {
    const rib = new THREE.Mesh(new THREE.TorusGeometry(1.8, 0.13, 8, 22, Math.PI * 1.2), fossilMat);
    rib.position.set(x, 0.4, z);
    rib.rotation.set(0.2, ry, 2.4);
    rib.castShadow = true;
    scene.add(rib);
  }
  const pilMat = new THREE.MeshStandardMaterial({ color: 0x141b28, roughness: 0.8 });
  const stripMat = new THREE.MeshBasicMaterial({ color: 0xffb454 });
  for (let i = 0; i < 4; i++) {
    const a = Math.PI / 4 + (i * Math.PI) / 2;
    const p = new THREE.Mesh(new THREE.CylinderGeometry(1.2, 1.5, 8, 8), pilMat);
    p.position.set(Math.cos(a) * 22, 4, Math.sin(a) * 22);
    p.castShadow = true;
    scene.add(p);
    const strip = new THREE.Mesh(new THREE.TorusGeometry(1.25, 0.05, 8, 24), stripMat);
    strip.position.set(Math.cos(a) * 22, 7.2, Math.sin(a) * 22);
    strip.rotation.x = Math.PI / 2;
    scene.add(strip);
  }
  const n = 500;
  const pos = new Float32Array(n * 3);
  for (let i = 0; i < n; i++) {
    const a = Math.random() * Math.PI * 2;
    const e = 0.12 + Math.random() * 1.2;
    const r = 70 + Math.random() * 20;
    pos[i * 3] = Math.cos(a) * Math.cos(e) * r;
    pos[i * 3 + 1] = Math.sin(e) * r;
    pos[i * 3 + 2] = Math.sin(a) * Math.cos(e) * r;
  }
  const gg = new THREE.BufferGeometry();
  gg.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  scene.add(new THREE.Points(gg, new THREE.PointsMaterial({
    color: 0xaac4e8, size: 0.6, transparent: true, opacity: 0.85, fog: false,
  })));
}

// ---------------- rex + systems ----------------
const rig = buildRex();
scene.add(rig.root);
const { J, mats, refs } = rig;

// fossil PBR: procedural bone textures + tuned reflections
for (const m of [mats.bone, mats.boneDark]) {
  m.map = boneTex.map;
  m.roughnessMap = boneTex.roughnessMap;
  m.bumpMap = boneTex.bumpMap;
  m.bumpScale = 0.35;
  m.roughness = 1.0;
  m.envMapIntensity = 0.35;
}
mats.tooth.map = boneTex.map;
mats.tooth.bumpMap = boneTex.bumpMap;
mats.tooth.bumpScale = 0.12;
mats.tooth.roughness = 0.45;
mats.tooth.envMapIntensity = 0.4;
mats.chrome.envMapIntensity = 1.2;
mats.sleeve.envMapIntensity = 0.9;
mats.metal.envMapIntensity = 0.8;
mats.darkMetal.envMapIntensity = 0.7;
mats.collar.envMapIntensity = 0.7;
const gait = createGait();
const tailSim = new TailSim(TAIL_LENS);
const hydro = buildHydraulics(scene, rig.hydroDefs, mats);

const tailParams = { stiff: 0.85, damp: 0.96, gravity: 9.8, enabled: true, walkBlend: 0 };

// bone labels
for (const s of rig.labelSpots) {
  const div = document.createElement('div');
  div.className = 'bone-tag';
  div.innerHTML = `<i></i>${s.text}`;
  const o = new CSS2DObject(div);
  o.position.set(s.at[0], s.at[1], s.at[2]);
  s.obj.add(o);
}

// ---------------- dust ----------------
function softTexture() {
  const c = document.createElement('canvas');
  c.width = c.height = 64;
  const ctx = c.getContext('2d');
  const g = ctx.createRadialGradient(32, 32, 2, 32, 32, 30);
  g.addColorStop(0, 'rgba(255,255,255,1)');
  g.addColorStop(0.5, 'rgba(255,255,255,0.4)');
  g.addColorStop(1, 'rgba(255,255,255,0)');
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, 64, 64);
  return new THREE.CanvasTexture(c);
}
const dustTex = softTexture();
const dustPool = [];
for (let i = 0; i < 40; i++) {
  const s = new THREE.Sprite(new THREE.SpriteMaterial({
    map: dustTex, color: 0xc9b896, transparent: true, opacity: 0, depthWrite: false,
  }));
  s.visible = false;
  scene.add(s);
  dustPool.push({ s, vel: new THREE.Vector3(), life: 0, max: 1, grow: 1 });
}
let dustIdx = 0;
function spawnDust(p, n = 6, spread = 1.4, up = 2.2) {
  for (let i = 0; i < n; i++) {
    const d = dustPool[dustIdx++ % dustPool.length];
    d.s.visible = true;
    d.s.position.set(p.x + (Math.random() - 0.5) * 0.6, Math.max(0.12, p.y), p.z + (Math.random() - 0.5) * 0.6);
    d.vel.set((Math.random() - 0.5) * spread * 2, Math.random() * up, (Math.random() - 0.5) * spread * 2 - 1);
    d.max = 0.5 + Math.random() * 0.5;
    d.life = d.max;
    d.grow = 2 + Math.random() * 2.5;
    const sc = 0.6 + Math.random() * 0.6;
    d.s.scale.set(sc, sc, 1);
    d.s.material.opacity = 0.7;
  }
}
function updateDust(dt) {
  for (const d of dustPool) {
    if (!d.s.visible) continue;
    d.life -= dt;
    if (d.life <= 0) { d.s.visible = false; continue; }
    d.s.position.addScaledVector(d.vel, dt);
    d.vel.y -= 1.6 * dt;
    d.vel.multiplyScalar(1 - 1.8 * dt);
    const k = d.life / d.max;
    d.s.material.opacity = 0.7 * k;
    const g = d.s.scale.x + d.grow * dt;
    d.s.scale.set(g, g, 1);
  }
}

// ---------------- audio: servos + steps ----------------
const sfx = {
  ctx: null, master: null, servoGain: null, enabled: true,
  unlock() {
    if (this.ctx) { if (this.ctx.state === 'suspended') this.ctx.resume(); return; }
    try {
      const AC = window.AudioContext || window.webkitAudioContext;
      this.ctx = new AC();
      this.master = this.ctx.createGain();
      this.master.gain.value = 0.35;
      this.master.connect(this.ctx.destination);
      // servo loop: filtered noise, gain driven by gait
      const len = this.ctx.sampleRate;
      const buf = this.ctx.createBuffer(1, len, this.ctx.sampleRate);
      const d = buf.getChannelData(0);
      for (let i = 0; i < len; i++) d[i] = Math.random() * 2 - 1;
      const src = this.ctx.createBufferSource();
      src.buffer = buf; src.loop = true;
      const f = this.ctx.createBiquadFilter();
      f.type = 'bandpass'; f.frequency.value = 320; f.Q.value = 2;
      this.servoGain = this.ctx.createGain();
      this.servoGain.gain.value = 0;
      src.connect(f); f.connect(this.servoGain); this.servoGain.connect(this.master);
      src.start();
      const osc = this.ctx.createOscillator();
      osc.type = 'sawtooth'; osc.frequency.value = 48;
      const lp = this.ctx.createBiquadFilter();
      lp.type = 'lowpass'; lp.frequency.value = 120;
      const og = this.ctx.createGain(); og.gain.value = 0.05;
      osc.connect(lp); lp.connect(og); og.connect(this.master);
      osc.start();
      this.noiseBuf = buf;
    } catch (e) { /* no audio */ }
  },
  servo(level) {
    if (!this.ctx || !this.enabled) return;
    const g = this.servoGain.gain;
    g.setTargetAtTime(level, this.ctx.currentTime, 0.1);
  },
  tone(freq, dur, type = 'sine', vol = 0.3, slideTo = null) {
    if (!this.ctx || !this.enabled) return;
    const t0 = this.ctx.currentTime;
    const o = this.ctx.createOscillator();
    const g = this.ctx.createGain();
    o.type = type;
    o.frequency.setValueAtTime(freq, t0);
    if (slideTo) o.frequency.exponentialRampToValueAtTime(Math.max(1, slideTo), t0 + dur);
    g.gain.setValueAtTime(vol, t0);
    g.gain.exponentialRampToValueAtTime(0.001, t0 + dur);
    o.connect(g); g.connect(this.master);
    o.start(t0); o.stop(t0 + dur + 0.05);
  },
  noise(dur, freq = 800, vol = 0.3) {
    if (!this.ctx || !this.enabled) return;
    const t0 = this.ctx.currentTime;
    const src = this.ctx.createBufferSource();
    src.buffer = this.noiseBuf; src.loop = true;
    const f = this.ctx.createBiquadFilter();
    f.type = 'lowpass'; f.frequency.value = freq;
    const g = this.ctx.createGain();
    g.gain.setValueAtTime(vol, t0);
    g.gain.exponentialRampToValueAtTime(0.001, t0 + dur);
    src.connect(f); f.connect(g); g.connect(this.master);
    src.start(t0); src.stop(t0 + dur + 0.05);
  },
  ui() { this.tone(700, 0.05, 'square', 0.06); },
  step(speedN) {
    this.tone(62, 0.28, 'sine', 0.35 + 0.3 * speedN, 28);
    this.noise(0.14, 320, 0.2 + 0.2 * speedN);
  },
};
window.addEventListener('pointerdown', () => sfx.unlock());
window.addEventListener('keydown', () => sfx.unlock());

// ---------------- UI ----------------
const $ = (id) => document.getElementById(id);
const pill = $('state-pill');
const flashEl = $('flash');
const modeBtns = [...document.querySelectorAll('[data-mode]')];

function showFlash(text, cls = '') {
  flashEl.textContent = text;
  flashEl.className = '';
  void flashEl.offsetWidth;
  flashEl.className = 'pop' + (cls ? ' ' + cls : '');
}
function setMode(m) {
  gait.setMode(m);
  sfx.ui();
  modeBtns.forEach((b) => b.classList.toggle('active', b.dataset.mode === m));
}
modeBtns.forEach((b) => b.addEventListener('click', () => setMode(b.dataset.mode)));

$('speed').addEventListener('input', (e) => {
  const v = +e.target.value / 100;
  gait.setSpeed(v);
  $('speed-val').textContent = v.toFixed(2) + ' m/s';
});
$('t-labels').addEventListener('change', (e) => {
  labelRenderer.domElement.style.display = e.target.checked ? '' : 'none';
});
$('t-hydro').addEventListener('change', (e) => { hydro.group.visible = e.target.checked; });
$('t-tail').addEventListener('change', (e) => { tailParams.enabled = e.target.checked; });
$('t-ghost').addEventListener('change', (e) => {
  const g = e.target.checked;
  for (const m of [mats.bone, mats.boneDark, mats.tooth]) {
    m.transparent = g;
    m.opacity = g ? 0.32 : 1;
    m.depthWrite = !g;
  }
});
$('t-sound').addEventListener('change', (e) => {
  sfx.enabled = e.target.checked;
  if (sfx.enabled) sfx.unlock();
  if (!sfx.enabled && sfx.ctx) sfx.servoGain.gain.value = 0;
});
$('t-grid').addEventListener('change', (e) => { grid.visible = e.target.checked; });
$('tail-stiff').addEventListener('input', (e) => {
  tailParams.stiff = +e.target.value / 100;
  $('tail-stiff-val').textContent = e.target.value + '%';
});
$('tail-damp').addEventListener('input', (e) => {
  tailParams.damp = +e.target.value / 100;
  $('tail-damp-val').textContent = e.target.value + '%';
});

// camera presets
const CAMS = {
  overview: { p: [12.5, 3.8, 3.5], t: [0, 2.4, 0] },
  skull:    { p: [3.6, 4.8, 7.8],  t: [0, 4.1, 3.2] },
  leg:      { p: [4.8, 1.7, 3.6],  t: [0.4, 1.5, 0.2] },
  tail:     { p: [-5.5, 3.6, -8.8], t: [0, 2.6, -2.8] },
};
let camTween = null;
function flyTo(name) {
  const c = CAMS[name];
  if (!c) return;
  sfx.ui();
  camTween = {
    t: 0, dur: 1.1,
    fp: camera.position.clone(), tp: new THREE.Vector3(...c.p),
    ft: controls.target.clone(), tt: new THREE.Vector3(...c.t),
  };
  document.querySelectorAll('[data-cam]').forEach((b) =>
    b.classList.toggle('active', b.dataset.cam === name));
}
document.querySelectorAll('[data-cam]').forEach((b) =>
  b.addEventListener('click', () => flyTo(b.dataset.cam)));
controls.addEventListener('start', () => { camTween = null; });

// panel toggle (mobile)
const panel = $('panel');
if (window.innerWidth > 860) panel.classList.add('open');
$('panel-toggle').addEventListener('click', () => panel.classList.toggle('open'));

// keyboard
window.addEventListener('keydown', (e) => {
  if (e.repeat) return;
  const k = e.key.toLowerCase();
  if (k === '1') setMode('idle');
  else if (k === '2') setMode('walk');
  else if (k === ' ') { e.preventDefault(); setMode(gait.mode === 'walk' ? 'idle' : 'walk'); }
});

// bone picker
const ray = new THREE.Raycaster();
const ptr = new THREE.Vector2();
let downAt = null;
renderer.domElement.addEventListener('pointerdown', (e) => { downAt = [e.clientX, e.clientY]; });
renderer.domElement.addEventListener('pointerup', (e) => {
  if (!downAt) return;
  const moved = Math.hypot(e.clientX - downAt[0], e.clientY - downAt[1]);
  downAt = null;
  if (moved > 6) return;
  ptr.set((e.clientX / window.innerWidth) * 2 - 1, -(e.clientY / window.innerHeight) * 2 + 1);
  ray.setFromCamera(ptr, camera);
  const hits = ray.intersectObjects(rig.root.children, true);
  for (const h of hits) {
    let o = h.object;
    while (o && !o.userData.info) o = o.parent;
    if (o && o.userData.info) {
      $('bone-name').textContent = o.userData.info.bone;
      $('bone-desc').textContent = o.userData.info.desc;
      $('bone-info').classList.add('lit');
      sfx.ui();
      return;
    }
  }
  $('bone-name').textContent = 'Click any bone';
  $('bone-desc').textContent = 'Anatomical notes appear here.';
  $('bone-info').classList.remove('lit');
});

// ---------------- telemetry ----------------
const phaseBar = $('phase-bar');
const stanceL = $('stance-L'), stanceR = $('stance-R');
const tSpeed = $('t-speed'), tFps = $('t-fps');
const pressBars = {
  jaw: [$('p-jaw-bar'), $('p-jaw')],
  legL: [$('p-legL-bar'), $('p-legL')],
  legR: [$('p-legR-bar'), $('p-legR')],
  tail: [$('p-tail-bar'), $('p-tail')],
  arm: [$('p-arm-bar'), $('p-arm')],
  neck: [$('p-neck-bar'), $('p-neck')],
};
let teleT = 0;
function updateTelemetry(dt, pose, pressures, fps) {
  teleT -= dt;
  if (teleT > 0) return;
  teleT = 0.15;
  phaseBar.style.width = (pose.phase * 100).toFixed(1) + '%';
  stanceL.className = 'dot ' + (pose.stanceL ? 'stance' : 'swing');
  stanceR.className = 'dot ' + (pose.stanceR ? 'stance' : 'swing');
  tSpeed.textContent = (gait.S.speed * pose.blend).toFixed(2) + ' m/s';
  tFps.textContent = fps.toFixed(0);
  for (const k in pressBars) {
    const ext = pressures[k] || 1;
    const pct = Math.min(1, Math.max(0.04, (ext - 0.9) / 0.2));
    pressBars[k][0].style.width = (pct * 100).toFixed(0) + '%';
    pressBars[k][1].textContent = (ext * 100).toFixed(0) + '%';
  }
  const label = pose.blend > 0.5 ? 'WALK' : pose.blend < 0.5 && gait.mode === 'walk' ? 'WALK' : 'IDLE';
  const stateLabel = gait.mode === 'walk' ? 'WALK' : 'IDLE';
  if (pill.textContent !== stateLabel) pill.textContent = stateLabel;
  void label;
}

// ---------------- main loop ----------------
const clock = new THREE.Clock();
let fpsEma = 60;
let introT = 0;
const INTRO_LEN = 2.4;
let booted = false;
const _pq = new THREE.Quaternion();
const _hipL = new THREE.Vector3(HIP_LOCAL.x, HIP_LOCAL.y, HIP_LOCAL.z);
const _hipR = new THREE.Vector3(-HIP_LOCAL.x, HIP_LOCAL.y, HIP_LOCAL.z);
const _basePos = new THREE.Vector3();
const _baseQuat = new THREE.Quaternion();
const _foot = new THREE.Vector3();
const easeInOut = (t) => (t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2);

function applyPose(pose) {
  J.pelvis.position.set(pose.pelvis.pos.x, pose.pelvis.pos.y, pose.pelvis.pos.z);
  J.pelvis.rotation.set(pose.pelvis.rot.x, pose.pelvis.rot.y, pose.pelvis.rot.z);
  J.spine.rotation.set(pose.spine.x, pose.spine.y, pose.spine.z);
  J.neck1.rotation.set(pose.neck1.x, pose.neck1.y, pose.neck1.z);
  J.neck2.rotation.set(pose.neck2.x, pose.neck2.y, pose.neck2.z);
  J.head.rotation.set(pose.head.x, pose.head.y, pose.head.z);
  J.jaw.rotation.set(pose.jaw, 0, 0);
  J.tailRoot.rotation.set(pose.tailRoot.x, pose.tailRoot.y, pose.tailRoot.z);
  J.shL.rotation.set(pose.shL.x, pose.shL.y, pose.shL.z);
  J.shR.rotation.set(pose.shR.x, pose.shR.y, pose.shR.z);
  J.elL.rotation.set(pose.elL, 0, 0);
  J.elR.rotation.set(pose.elR, 0, 0);
}

function tick() {
  requestAnimationFrame(tick);
  const dt = Math.min(clock.getDelta(), 0.05);
  const t = clock.elapsedTime;
  if (dt > 0) fpsEma += (1 / dt - fpsEma) * 0.05;

  // 1. gait
  const pose = gait.update(dt, t);
  applyPose(pose);

  // 2. leg IK (root is static identity: local == world)
  _pq.setFromEuler(J.pelvis.rotation);
  solveLeg({
    hip: J.hipL, knee: J.kneeL, ank: J.ankL, mtp: J.mtpL,
    L1: LEG.L1, L2: LEG.L2,
    pelvisPos: J.pelvis.position, pelvisQuat: _pq, hipLocal: _hipL,
    target: pose.legL.ankle, yaw: pose.legL.yaw,
    alpha: pose.legL.alpha, beta: pose.legL.beta, side: 1,
  });
  solveLeg({
    hip: J.hipR, knee: J.kneeR, ank: J.ankR, mtp: J.mtpR,
    L1: LEG.L1, L2: LEG.L2,
    pelvisPos: J.pelvis.position, pelvisQuat: _pq, hipLocal: _hipR,
    target: pose.legR.ankle, yaw: pose.legR.yaw,
    alpha: pose.legR.alpha, beta: pose.legR.beta, side: -1,
  });

  // 3. commit transforms, then world-space systems
  rig.root.updateMatrixWorld(true);

  // 4. tail physics
  J.tailRoot.getWorldPosition(_basePos);
  J.tailRoot.getWorldQuaternion(_baseQuat);
  tailParams.walkBlend = pose.blend;
  const nodes = tailSim.update(dt, _basePos, _baseQuat, pose.phase, t, tailParams);
  for (let i = 0; i < rig.tailSegs.length; i++) {
    rig.tailSegs[i].position.copy(nodes[i]);
    rig.tailSegs[i].lookAt(nodes[i + 1]);
  }
  rig.root.updateMatrixWorld(true);

  // 5. hydraulics
  const pressures = hydro.update();

  // 6. treadmill + events
  contact.position.x = J.pelvis.position.x;
  for (const s of slats) {
    s.position.z -= pose.belt * dt;
    if (s.position.z < -SLAT_SPAN / 2) s.position.z += SLAT_SPAN;
  }
  const speedN = (gait.S.speed - 0.4) / 1.8;
  for (const ev of pose.events) {
    const foot = ev.leg === 'L' ? refs.footL : refs.footR;
    foot.getWorldPosition(_foot);
    if (ev.type === 'strike') {
      spawnDust(_foot, 7, 1.6, 2.4);
      sfx.step(speedN);
    } else {
      spawnDust(_foot, 2, 1.0, 1.4);
    }
  }
  sfx.servo(gait.mode === 'walk' ? 0.05 + 0.1 * pose.blend * (0.5 + 0.5 * Math.sin(pose.phase * Math.PI * 4)) : 0.008);
  updateDust(dt);

  // glow pulse
  mats.eye.emissiveIntensity = 0.85 + 0.18 * Math.sin(t * 3);
  mats.core.emissiveIntensity = 0.75 + 0.2 * Math.sin(t * 4);
  if (refs.coreLight) refs.coreLight.intensity = 1.0 + 0.25 * Math.sin(t * 4);
  refs.headLight.intensity = 0.4 + 0.1 * Math.sin(t * 3 + 1);

  // intro glide
  if (introT < 1) {
    introT = Math.min(1, introT + dt / INTRO_LEN);
    camera.position.lerpVectors(CAM_START, CAM_HOME, easeInOut(introT));
    if (introT >= 1) {
      controls.enabled = true;
      showFlash('SYSTEM ONLINE');
    }
  }
  // camera preset tween
  if (camTween) {
    camTween.t = Math.min(1, camTween.t + dt / camTween.dur);
    const e = easeInOut(camTween.t);
    camera.position.lerpVectors(camTween.fp, camTween.tp, e);
    controls.target.lerpVectors(camTween.ft, camTween.tt, e);
    if (camTween.t >= 1) camTween = null;
  }
  controls.update();

  updateTelemetry(dt, pose, pressures, fpsEma);

  renderer.render(scene, camera);
  labelRenderer.render(scene, camera);

  if (!booted) {
    booted = true;
    window.__booted = true;
    document.getElementById('loader').classList.add('done');
  }
}

window.addEventListener('resize', () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
  labelRenderer.setSize(window.innerWidth, window.innerHeight);
});

tick();

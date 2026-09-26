import * as THREE from 'three';
import GUI from 'lil-gui';
import { EffectComposer } from 'three/examples/jsm/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/examples/jsm/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/examples/jsm/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/examples/jsm/postprocessing/OutputPass.js';
import { RoomEnvironment } from 'three/examples/jsm/environments/RoomEnvironment.js';

import { createMaze, DEFAULT_PARAMS, cloneNet } from './network.js';
import { buildMine } from './mineBuilder.js';
import { World } from './world.js';
import { CarPhysics, createCarMesh, syncCarMesh } from './car.js';
import { Traffic } from './traffic.js';
import { buildProps, disposeGroup } from './props.js';
import { buildTracks } from './tracks.js';
import { createMineMaterial } from './materials.js';
import { Editor } from './editor.js';
import { mineToOBJ, exportGLB, download } from './exporters.js';
import { mulberry32 } from './noise.js';

// ---------------------------------------------------------------- renderer
const canvas = document.getElementById('c');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 1.75));
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFShadowMap;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 0.95;

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x020101);
scene.fog = new THREE.FogExp2(0x0a0705, 0.0125);
const pmrem = new THREE.PMREMGenerator(renderer);
scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
scene.environmentIntensity = 0.06;
scene.add(new THREE.HemisphereLight(0x6a5a48, 0x0a0806, 0.35));

const camera = new THREE.PerspectiveCamera(72, window.innerWidth / window.innerHeight, 0.1, 900);
const composer = new EffectComposer(renderer);
composer.addPass(new RenderPass(scene, camera));
const bloom = new UnrealBloomPass(new THREE.Vector2(window.innerWidth, window.innerHeight), 0.5, 0.45, 0.9);
composer.addPass(bloom);
composer.addPass(new OutputPass());

// ---------------------------------------------------------------- state
const P = { ...DEFAULT_PARAMS };
let net = createMaze(P.seed);
let mine = null;
const world = new World();
const mineMat = createMineMaterial();
const mineMesh = new THREE.Mesh(new THREE.BufferGeometry(), mineMat);
mineMesh.name = 'FrontierMine';
mineMesh.receiveShadow = true;
scene.add(mineMesh);
let props = new THREE.Group();
scene.add(props);
let tracks = new THREE.Group();
scene.add(tracks);
const topoMat = new THREE.LineBasicMaterial({ color: 0x46d8ff, transparent: true, opacity: 0.55 });
let topoLines = null;
let showTopo = false;
const traffic = new Traffic(scene, world);

// dynamic light pool (nearest lamps to the car get a real light)
const POOL = 12;
const lightPool = [];
for (let i = 0; i < POOL; i++) {
  const l = new THREE.PointLight(0xffb866, 0, 26, 1.6);
  scene.add(l);
  lightPool.push(l);
}
const cartLights = [];
for (let i = 0; i < 3; i++) { const l = new THREE.PointLight(0xffd9a0, 0, 20, 1.6); scene.add(l); cartLights.push(l); }

// ---------------------------------------------------------------- car
const car = new CarPhysics(world);
const carMesh = createCarMesh();
scene.add(carMesh);
const headL = new THREE.SpotLight(0xfff1d6, 260, 90, 0.52, 0.45, 1.4);
headL.position.set(0, 0.35, 2.0);
headL.castShadow = true;
headL.shadow.mapSize.set(1024, 1024);
headL.shadow.camera.near = 0.5; headL.shadow.camera.far = 70;
headL.shadow.bias = -0.0004;
const headTarget = new THREE.Object3D(); headTarget.position.set(0, -1.2, 22);
carMesh.add(headL, headTarget); headL.target = headTarget;
const tailL = new THREE.PointLight(0xff2a10, 3, 7, 2); tailL.position.set(0, 0.3, -2.4); carMesh.add(tailL);
car.impactFn = (v) => { shake = Math.min(1, shake + v * 0.04); if (v > 9) flash(0.35); };

// ---------------------------------------------------------------- build / rebuild
let buildTimer = null, lastQuick = 0;
function spawnTrains() {
  traffic.spawn({ count: P.trainCount, speed: P.trainSpeed, wagons: P.maxWagons, seed: P.seed + 5 });
}
function rebuild(full = true) {
  mine = buildMine(net, P, { preview: !full });
  mineMesh.geometry.dispose();
  mineMesh.geometry = mine.geometry;
  if (topoLines) { scene.remove(topoLines); topoLines.children.forEach((c) => c.geometry.dispose()); topoLines = null; }
  if (showTopo) makeTopo();
  tracks.visible = full; // rails are rebuilt when the drag ends
  if (!full) return;
  world.setGeometry(mine.collisionGeometry);
  scene.remove(props); disposeGroup(props);
  props = buildProps(mine, P);
  scene.add(props);
  traffic.setMine(mine);   // lane graph + clearance-checked junction routes
  spawnTrains();
  scene.remove(tracks); disposeGroup(tracks);
  tracks = buildTracks(mine, P, world, traffic); // rails follow every lane + junction route
  scene.add(tracks);
  lamps = mine.lamps.map((l) => l.p);
  setupRace();
  drawMinimapBase();
  const conflicts = new Set();
  mine.conflicts.forEach((c) => { conflicts.add(c.a); conflicts.add(c.b); });
  if (editor.active) editor.rebuildHandles(conflicts);
  updateStats();
}
function scheduleRebuild(full) {
  clearTimeout(buildTimer);
  if (full) { buildTimer = setTimeout(() => rebuild(true), 10); return; }
  const now = performance.now();
  if (now - lastQuick > 140) { lastQuick = now; rebuild(false); }
  else buildTimer = setTimeout(() => { lastQuick = performance.now(); rebuild(false); }, 140);
}
function makeTopo() {
  // draw the QUAD edges (not the render triangulation) so the real topology is visible.
  // Roof edges go into a separate layer, hidden in the top-down editor view (x-ray).
  const q = mine.quads, pos = mine.geometry.attributes.position.array, nrm = mine.geometry.attributes.normal.array;
  const seen = new Set();
  const lower = [], roof = [];
  const push = (arr, a) => arr.push(pos[a * 3] + nrm[a * 3] * 0.02, pos[a * 3 + 1] + nrm[a * 3 + 1] * 0.02, pos[a * 3 + 2] + nrm[a * 3 + 2] * 0.02);
  for (let i = 0; i < q.length; i += 4) {
    for (let j = 0; j < 4; j++) {
      const a = q[i + j], b = q[i + ((j + 1) & 3)];
      const k = a < b ? a * 1e7 + b : b * 1e7 + a;
      if (seen.has(k)) continue;
      seen.add(k);
      const arr = nrm[a * 3 + 1] < -0.35 && nrm[b * 3 + 1] < -0.35 ? roof : lower;
      push(arr, a); push(arr, b);
    }
  }
  topoLines = new THREE.Group();
  for (const [arr, name] of [[lower, 'lower'], [roof, 'roof']]) {
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(arr, 3));
    const l = new THREE.LineSegments(g, topoMat); l.name = name;
    topoLines.add(l);
  }
  topoLines.visible = showTopo;
  syncTopoLayers();
  scene.add(topoLines);
}
function syncTopoLayers() { if (topoLines) topoLines.getObjectByName('roof').visible = !editor.active; }
function updateStats() {
  const s = mine.stats;
  document.getElementById('stats').textContent =
    `mine mesh: 1 continuous closed quad surface\n${s.verts.toLocaleString()} verts · ${s.quads.toLocaleString()} quads · build ${s.ms.toFixed(0)} ms` +
    (tracks.userData.stats ? `\ntrack: ${tracks.userData.stats.railKm.toFixed(1)} km of rail · ${tracks.userData.stats.routes} junction routes · ${tracks.userData.stats.sleepers.toLocaleString()} sleepers` : '') +
    (mine.warnings.length ? `\n⚠ ${mine.warnings.join('\n⚠ ')}` : '');
}

// ---------------------------------------------------------------- race
let checkpoints = [], cpIndex = 0, raceTime = 0, raceRunning = false, penalties = 0;
const gateGroup = new THREE.Group(); scene.add(gateGroup);
const gateMat = new THREE.MeshBasicMaterial({ color: 0xffc23a, transparent: true, opacity: 0.16, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide });
const ringMat = new THREE.MeshBasicMaterial({ color: 0xffd24a, transparent: true, opacity: 0.9, blending: THREE.AdditiveBlending, depthWrite: false });
const gateMesh = new THREE.Mesh(new THREE.CylinderGeometry(3.2, 3.2, 7, 32, 1, true), gateMat);
const gateRing = new THREE.Mesh(new THREE.TorusGeometry(3.2, 0.09, 8, 48), ringMat); gateRing.rotation.x = Math.PI / 2;
const gateRing2 = gateRing.clone(); gateRing2.position.y = 3.2;
gateGroup.add(gateMesh, gateRing, gateRing2);
const gateLight = new THREE.PointLight(0xffc23a, 40, 18, 1.6); gateGroup.add(gateLight); gateLight.position.y = 2;
let bestTime = Number(localStorage.getItem('frontier-mine-best-' + P.seed)) || null;

function setupRace() {
  const rnd = mulberry32(P.seed * 13 + 1);
  const hubs = mine.hubs.map((h) => h.center.clone());
  const order = hubs.map((_, i) => i).sort(() => rnd() - 0.5);
  checkpoints = order.slice(0, Math.min(8, hubs.length)).map((i) => hubs[i]);
  resetRace(true);
}
function resetRace(respawn) {
  cpIndex = 0; raceTime = 0; raceRunning = false; penalties = 0;
  if (respawn) spawnCar();
  placeGate();
}
function placeGate() {
  const p = checkpoints[cpIndex];
  gateGroup.visible = !!p;
  if (p) gateGroup.position.copy(p).add(new THREE.Vector3(0, 0.05, 0));
  gateMesh.position.y = 3.5;
}
function spawnCar() {
  const cl = mine.centerline;
  const e0 = cl.filter((c) => c.edge === 0);
  const c = e0[Math.floor(e0.length * 0.35)];
  car.reset(c.p.clone().addScaledVector(c.B, 1.0), c.T);
}
function resetCarToTrack() {
  let best = null, bd = Infinity;
  for (let i = 0; i < mine.centerline.length; i += 2) {
    const c = mine.centerline[i];
    const d = c.p.distanceToSquared(car.pos);
    if (d < bd) { bd = d; best = c; }
  }
  // junctions: also consider hub centers
  let hubBest = null;
  for (const h of mine.hubs) { const d = h.center.distanceToSquared(car.pos); if (d < bd) { bd = d; hubBest = h; } }
  const fwd = car.forward();
  if (hubBest) { car.reset(hubBest.center.clone().add(new THREE.Vector3(0, 1, 0)), fwd.setY(0).lengthSq() > 0.01 ? fwd : new THREE.Vector3(1, 0, 0)); return; }
  const dir = best.T.clone(); if (dir.dot(fwd) < 0) dir.negate();
  car.reset(best.p.clone().addScaledVector(best.B, 1.0), dir);
}
function fmt(t) { const m = Math.floor(t / 60), s = t - m * 60; return `${m}:${s.toFixed(2).padStart(5, '0')}`; }
let msgTimer = 0;
function message(html, dur = 1.6) { const m = document.getElementById('msg'); m.innerHTML = html; m.style.opacity = 1; msgTimer = dur; }
function flash(a) { const f = document.getElementById('flash'); f.style.transition = 'none'; f.style.opacity = a; requestAnimationFrame(() => { f.style.transition = 'opacity .45s'; f.style.opacity = 0; }); }

// ---------------------------------------------------------------- minimap
const mm = document.getElementById('minimap');
const mmCtx = mm.getContext('2d');
let mmBase = null;
const MM_SCALE = 0.62;
function drawMinimapBase() {
  // pre-render the whole network at scale into an offscreen canvas
  const bb = new THREE.Box3();
  mine.centerline.forEach((c) => bb.expandByPoint(c.p));
  const pad = 30;
  const w = Math.ceil((bb.max.x - bb.min.x + pad * 2) * MM_SCALE), h = Math.ceil((bb.max.z - bb.min.z + pad * 2) * MM_SCALE);
  mmBase = document.createElement('canvas'); mmBase.width = w; mmBase.height = h;
  mmBase.ox = bb.min.x - pad; mmBase.oz = bb.min.z - pad;
  const ctx = mmBase.getContext('2d');
  ctx.lineCap = 'round'; ctx.lineJoin = 'round';
  const toXY = (p) => [(p.x - mmBase.ox) * MM_SCALE, (p.z - mmBase.oz) * MM_SCALE];
  const edges = new Map();
  mine.centerline.forEach((c) => { if (!edges.has(c.edge)) edges.set(c.edge, []); edges.get(c.edge).push(c.p); });
  for (const [pass, width, col] of [[0, 7, 'rgba(255,170,80,0.22)'], [1, 3.2, 'rgba(255,215,160,0.85)']]) {
    ctx.strokeStyle = col; ctx.lineWidth = width;
    for (const [ei, pts] of edges) {
      ctx.beginPath();
      const hubA = mine.hubs.find((h) => h.id === net.edges[ei].a), hubB = mine.hubs.find((h) => h.id === net.edges[ei].b);
      const all = [hubA ? hubA.center : pts[0], ...pts, hubB ? hubB.center : pts[pts.length - 1]];
      all.forEach((p, i) => { const [x, y] = toXY(p); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); });
      ctx.stroke();
    }
  }
  ctx.fillStyle = 'rgba(255,200,120,0.9)';
  mine.hubs.forEach((h) => { const [x, y] = toXY(h.center); ctx.beginPath(); ctx.arc(x, y, 3.2, 0, 7); ctx.fill(); });
}
function drawMinimap() {
  if (!mmBase) return;
  const W = mm.width, H = mm.height;
  mmCtx.clearRect(0, 0, W, H);
  mmCtx.save();
  mmCtx.beginPath(); mmCtx.arc(W / 2, H / 2, W / 2 - 2, 0, 7); mmCtx.clip();
  mmCtx.translate(W / 2, H / 2);
  const f = car.forward();
  mmCtx.rotate(-Math.PI / 2 - Math.atan2(f.z, f.x)); // car heading points up
  const cx = (car.pos.x - mmBase.ox) * MM_SCALE, cy = (car.pos.z - mmBase.oz) * MM_SCALE;
  mmCtx.drawImage(mmBase, -cx, -cy);
  const toL = (p) => [(p.x - mmBase.ox) * MM_SCALE - cx, (p.z - mmBase.oz) * MM_SCALE - cy];
  mmCtx.fillStyle = '#ff4b2b';
  traffic.forEachCar((c) => { const [x, y] = toL(c.pos); mmCtx.fillRect(x - 2, y - 2, 4, 4); });
  if (checkpoints[cpIndex]) {
    const [x, y] = toL(checkpoints[cpIndex]);
    mmCtx.strokeStyle = '#ffd24a'; mmCtx.lineWidth = 2.5; mmCtx.beginPath(); mmCtx.arc(x, y, 7, 0, 7); mmCtx.stroke();
  }
  mmCtx.restore();
  // car arrow (always up)
  mmCtx.fillStyle = '#ffffff';
  mmCtx.beginPath(); mmCtx.moveTo(W / 2, H / 2 - 9); mmCtx.lineTo(W / 2 + 6, H / 2 + 7); mmCtx.lineTo(W / 2 - 6, H / 2 + 7); mmCtx.closePath(); mmCtx.fill();
}

// ---------------------------------------------------------------- input
const keys = new Set();
let camMode = 0;
window.addEventListener('keydown', (e) => {
  if (e.target && e.target.tagName === 'INPUT') return;
  keys.add(e.code);
  if (e.code === 'Tab') { e.preventDefault(); toggleEdit(); }
  if (e.code === 'KeyR' && !editor.active) resetCarToTrack();
  if (e.code === 'KeyC') camMode = (camMode + 1) % 3;
  if (e.code === 'KeyT') toggleTopo();
  if (e.code === 'KeyH') { const h = document.getElementById('help'); h.style.display = h.style.display === 'none' ? '' : 'none'; }
  if (e.code === 'Enter' && !editor.active) { resetRace(true); message('GO!', 1); }
  if (e.code === 'KeyM') toggleSound();
  if (['ArrowUp', 'ArrowDown', 'Space'].includes(e.code)) e.preventDefault();
  startAudio();
});
window.addEventListener('keyup', (e) => keys.delete(e.code));
function readInput() {
  const k = (c) => keys.has(c);
  const inp = {
    throttle: k('KeyW') || k('ArrowUp') ? 1 : 0,
    brake: k('KeyS') || k('ArrowDown') ? 1 : 0,
    steer: (k('KeyA') || k('ArrowLeft') ? 1 : 0) - (k('KeyD') || k('ArrowRight') ? 1 : 0),
    handbrake: k('Space'),
    boost: k('ShiftLeft') || k('ShiftRight'),
  };
  const gp = navigator.getGamepads ? [...navigator.getGamepads()].find((g) => g) : null;
  if (gp) {
    const ax = gp.axes[0] || 0;
    if (Math.abs(ax) > 0.12) inp.steer = -ax;
    if (gp.buttons[7]) inp.throttle = Math.max(inp.throttle, gp.buttons[7].value);
    if (gp.buttons[6]) inp.brake = Math.max(inp.brake, gp.buttons[6].value);
    if (gp.buttons[0] && gp.buttons[0].pressed) inp.handbrake = true;
    if (gp.buttons[1] && gp.buttons[1].pressed) inp.boost = true;
  }
  return inp;
}

// ---------------------------------------------------------------- audio (simple synthesized engine)
let audio = null, soundOn = true;
function startAudio() {
  if (audio || !soundOn) return;
  try {
    const ctx = new AudioContext();
    const o1 = ctx.createOscillator(), o2 = ctx.createOscillator();
    o1.type = 'sawtooth'; o2.type = 'square';
    const lp = ctx.createBiquadFilter(); lp.type = 'lowpass'; lp.frequency.value = 900;
    const g = ctx.createGain(); g.gain.value = 0.0;
    o1.connect(lp); o2.connect(lp); lp.connect(g); g.connect(ctx.destination);
    o1.start(); o2.start();
    audio = { ctx, o1, o2, g, lp };
  } catch (e) { audio = null; }
}
function toggleSound() { soundOn = !soundOn; if (audio) audio.g.gain.value = 0; }
function updateAudio(inp) {
  if (!audio) return;
  const sp = car.speed;
  const gear = Math.min(5, Math.floor(sp / 14));
  const rpm = 0.25 + ((sp - gear * 14) / 14) * 0.75;
  const f = 40 + rpm * 90 + gear * 6;
  audio.o1.frequency.setTargetAtTime(f, audio.ctx.currentTime, 0.05);
  audio.o2.frequency.setTargetAtTime(f * 0.5, audio.ctx.currentTime, 0.05);
  audio.lp.frequency.setTargetAtTime(500 + inp.throttle * 1400 + (car.boosting ? 1200 : 0), audio.ctx.currentTime, 0.08);
  audio.g.gain.setTargetAtTime(soundOn && !editor.active ? 0.035 + inp.throttle * 0.04 : 0, audio.ctx.currentTime, 0.1);
}

// ---------------------------------------------------------------- editor + GUI
const editor = new Editor({ scene, camera, dom: renderer.domElement, getNet: () => net, getParams: () => P, onChange: (full) => scheduleRebuild(full) });
const gui = new GUI({ title: 'Mine generator' });
gui.hide();
const actions = {
  regenerate: () => { net = createMaze(P.seed); rebuild(true); editor.rebuildHandles(); bestTime = Number(localStorage.getItem('frontier-mine-best-' + P.seed)) || null; },
  addCP: () => editor.addControlPoint() || message('<small>select a cyan control point first</small>', 1.5),
  delCP: () => editor.deleteControlPoint() || message('<small>select a cyan control point (edge keeps ≥1)</small>', 1.5),
  topology: () => toggleTopo(),
  exportOBJ: () => download(mineToOBJ(mine), `frontier_mine_seed${P.seed}.obj`),
  exportGLB: async () => download(await exportGLB([mineMesh, props, tracks]), `frontier_mine_seed${P.seed}.glb`),
  saveJSON: () => download(JSON.stringify({ params: P, net }, null, 1), `frontier_mine_seed${P.seed}.json`, 'application/json'),
  loadJSON: () => {
    const inp = document.createElement('input'); inp.type = 'file'; inp.accept = '.json';
    inp.onchange = async () => {
      const data = JSON.parse(await inp.files[0].text());
      Object.assign(P, data.params || {}); net = data.net; gui.controllersRecursive().forEach((c) => c.updateDisplay());
      rebuild(true); editor.rebuildHandles();
    };
    inp.click();
  },
  drive: () => toggleEdit(),
};
const fGen = gui.addFolder('Layout');
fGen.add(P, 'seed', 1, 999, 1).name('maze seed');
fGen.add(actions, 'regenerate').name('↻ regenerate maze');
fGen.add(actions, 'addCP').name('+ insert control point');
fGen.add(actions, 'delCP').name('− delete control point');
const fShape = gui.addFolder('Tunnel profile');
const rb = () => scheduleRebuild(true);
fShape.add(P, 'roadHalfWidth', 3.2, 6, 0.1).name('road half width').onFinishChange(rb);
fShape.add(P, 'springHeight', 2.4, 4.5, 0.1).name('wall height').onFinishChange(rb);
fShape.add(P, 'roofHeight', 4.6, 8, 0.1).name('roof crown').onFinishChange(rb);
fShape.add(P, 'wallBulge', 0, 1, 0.05).name('wall bulge').onFinishChange(rb);
fShape.add(P, 'grooveDepth', 0.12, 0.3, 0.01).name('track bed depth').onFinishChange(rb);
fShape.add(P, 'rockNoise', 0, 0.9, 0.02).name('rock noise').onFinishChange(rb);
fShape.add(P, 'bankMax', 0, 0.2, 0.01).name('max banking').onFinishChange(rb);
fShape.add(P, 'minCrestRadius', 15, 150, 1).name('min crest radius (jumps)').onFinishChange(rb);
fShape.add(P, 'maxGrade', 0.05, 0.35, 0.01).name('max grade').onFinishChange(rb);
const fTopo = gui.addFolder('Topology');
fTopo.add(P, 'ringSpacing', 0.4, 2, 0.05).name('edge-loop spacing').onFinishChange(rb);
fTopo.add(P, 'wallSegs', 2, 10, 1).name('wall loops').onFinishChange(rb);
fTopo.add(P, 'roofSegs', 4, 24, 2).name('roof loops (even)').onFinishChange(rb);
fTopo.add(P, 'filletSegs', 2, 8, 1).name('corner half-segs k').onFinishChange(rb);
fTopo.add(actions, 'topology').name('show quad wireframe [T]');
const fProps = gui.addFolder('Props & traffic');
fProps.add(P, 'supportSpacing', 3, 16, 0.5).name('support spacing').onFinishChange(rb);
fProps.add(P, 'lampEvery', 1, 6, 1).name('lamp every N').onFinishChange(rb);
const fTrains = gui.addFolder('Trains (traffic)');
fTrains.add(P, 'trainCount', 0, 40, 1).name('number of trains').onFinishChange(spawnTrains);
fTrains.add(P, 'trainSpeed', 0.2, 2.5, 0.05).name('train speed ×').onChange((v) => traffic.setSpeed(v));
fTrains.add(P, 'maxWagons', 1, 6, 1).name('max wagons / train').onFinishChange(spawnTrains);
fTrains.add({ respawn: () => { P.seed2 = (P.seed2 || 0) + 1; traffic.spawn({ count: P.trainCount, speed: P.trainSpeed, wagons: P.maxWagons, seed: P.seed + 5 + P.seed2 * 97 }); } }, 'respawn').name('↻ reshuffle trains');
const fIO = gui.addFolder('Export');
fIO.add(actions, 'exportOBJ').name('⤓ OBJ (quads)');
fIO.add(actions, 'exportGLB').name('⤓ GLB (mesh + tracks + props)');
fIO.add(actions, 'saveJSON').name('⤓ save splines JSON');
fIO.add(actions, 'loadJSON').name('⤒ load splines JSON');
gui.add(actions, 'drive').name('▶ back to driving [TAB]');

function toggleEdit() {
  const on = !editor.active;
  document.body.classList.toggle('editing', on);
  if (on) { gui.show(); editor.enable(true, car.pos.clone()); const cs = new Set(); mine.conflicts.forEach((c) => { cs.add(c.a); cs.add(c.b); }); editor.rebuildHandles(cs); }
  else { gui.hide(); editor.enable(false); if (!insideMine(car.pos)) resetCarToTrack(); }
  syncTopoLayers();
  document.getElementById('msg').style.opacity = 0; msgTimer = 0;
}
function toggleTopo() {
  showTopo = !showTopo;
  if (showTopo && !topoLines) makeTopo();
  if (topoLines) topoLines.visible = showTopo;
}
function insideMine(p) {
  const hit = world.raycast(p.clone().add(new THREE.Vector3(0, 0.5, 0)), new THREE.Vector3(0, -1, 0), 4);
  return !!hit && hit.normal.y > 0;
}

// ---------------------------------------------------------------- lights / camera helpers
let lamps = [];
let lightTimer = 0;
function assignLights() {
  const ahead = car.pos.clone().addScaledVector(car.forward(), 14);
  const sorted = lamps.map((p, i) => [p.distanceToSquared(ahead), i]).sort((a, b) => a[0] - b[0]);
  for (let i = 0; i < POOL; i++) {
    const s = sorted[i];
    const L = lightPool[i];
    if (!s) { L.intensity = 0; continue; }
    L.position.copy(lamps[s[1]]);
    L.intensity = 20;
  }
  const carts = traffic.lightSources().sort((a, b) => a.p.distanceToSquared(car.pos) - b.p.distanceToSquared(car.pos));
  cartLights.forEach((l, i) => { if (carts[i]) { l.position.copy(carts[i].p); l.intensity = 22; } else l.intensity = 0; });
}

let shake = 0;
const camPos = new THREE.Vector3(), camLook = new THREE.Vector3();
function updateCamera(dt) {
  const f = car.forward(), up = new THREE.Vector3(0, 1, 0);
  const flatF = f.clone().setY(f.y * 0.4).normalize();
  const sp = car.speed;
  let want, look;
  if (camMode === 0) {
    want = car.pos.clone().addScaledVector(flatF, -7.0 - sp * 0.03).addScaledVector(up, 2.4);
    look = car.pos.clone().addScaledVector(flatF, 6).addScaledVector(up, 0.9);
  } else if (camMode === 1) {
    want = car.pos.clone().addScaledVector(flatF, -4.2).addScaledVector(up, 1.7);
    look = car.pos.clone().addScaledVector(flatF, 10).addScaledVector(up, 0.8);
  } else {
    want = car.pos.clone().addScaledVector(f, 0.3).addScaledVector(car.up(), 0.85);
    look = car.pos.clone().addScaledVector(f, 20).addScaledVector(car.up(), 0.6);
  }
  // keep the camera inside the tunnel
  const origin = car.pos.clone().addScaledVector(up, 1.2);
  const dir = want.clone().sub(origin); const dl = dir.length(); dir.normalize();
  const hit = world.raycast(origin, dir, dl + 0.4);
  if (hit && camMode !== 2) want = origin.addScaledVector(dir, Math.max(0.5, hit.distance - 0.45));
  // teleport (reset / respawn): snap instead of flying through the rock
  if (camPos.distanceTo(want) > 25) { camPos.copy(want); camLook.copy(look); }
  const k = camMode === 2 ? 1 : 1 - Math.exp(-dt * 9);
  camPos.lerp(want, k); camLook.lerp(look, camMode === 2 ? 1 : 1 - Math.exp(-dt * 14));
  camera.position.copy(camPos);
  if (shake > 0) { camera.position.x += (Math.random() - 0.5) * shake * 0.4; camera.position.y += (Math.random() - 0.5) * shake * 0.4; shake = Math.max(0, shake - dt * 2.2); }
  if (car.boosting) { camera.position.x += (Math.random() - 0.5) * 0.04; camera.position.y += (Math.random() - 0.5) * 0.04; }
  camera.lookAt(camLook);
  const fov = 70 + Math.min(22, sp * 0.28) + (car.boosting ? 6 : 0);
  camera.fov += (fov - camera.fov) * Math.min(1, dt * 4);
  camera.updateProjectionMatrix();
}

// ---------------------------------------------------------------- cart collisions
const _q = new THREE.Quaternion();
function collideCarts() {
  traffic.forEachCar((c) => {
    if (c.pos.distanceToSquared(car.pos) > 14 * 14) return;
    _q.copy(c.mesh.quaternion).invert();
    const center = c.pos.clone().add(new THREE.Vector3(0, 0.95, 0));
    for (const s of car.spec.spheres) {
      const sw = new THREE.Vector3(s[0], s[1], s[2]).applyQuaternion(car.quat).add(car.pos);
      const local = sw.clone().sub(center).applyQuaternion(_q);
      const cl = new THREE.Vector3(
        THREE.MathUtils.clamp(local.x, -c.half.x, c.half.x),
        THREE.MathUtils.clamp(local.y, -c.half.y, c.half.y),
        THREE.MathUtils.clamp(local.z, -c.half.z, c.half.z));
      const d = local.clone().sub(cl);
      const dist = d.length();
      if (dist >= car.spec.sphereR) continue;
      let n;
      if (dist > 1e-4) n = d.divideScalar(dist).applyQuaternion(c.mesh.quaternion);
      else n = sw.clone().sub(center).setY(0).normalize();
      n.y = Math.max(n.y, 0); n.normalize();
      car.pos.addScaledVector(n, car.spec.sphereR - dist);
      const imp = car.resolveImpulse(sw, n, 0.35, 0.3, c.vel);
      if (imp > 4 && raceRunning && performance.now() - lastHit > 800) {
        lastHit = performance.now(); penalties += 2; message('CART HIT<small>+2.0 s penalty</small>', 1.2); flash(0.5); shake = 1;
      }
    }
  });
}
let lastHit = 0;

// ---------------------------------------------------------------- HUD
const $ = (id) => document.getElementById(id);
function updateHUD(dt) {
  $('speed').textContent = Math.round(car.speed * 3.6);
  $('boostFill').style.width = (car.boost * 100).toFixed(0) + '%';
  $('time').textContent = fmt(raceTime + penalties);
  $('cp').textContent = `${Math.min(cpIndex + 1, checkpoints.length)} / ${checkpoints.length}`;
  $('best').textContent = bestTime ? fmt(bestTime) : '--';
  const cp = checkpoints[cpIndex];
  if (cp) {
    const to = cp.clone().sub(car.pos); const dist = to.length();
    const f = car.forward();
    const ang = Math.atan2(f.x * to.z - f.z * to.x, f.x * to.x + f.z * to.z);
    $('arrow').style.transform = `rotate(${(ang * 180) / Math.PI}deg)`;
    $('cpDist').textContent = `${dist.toFixed(0)} m`;
  }
  // train proximity warning (ahead of the car, closing in)
  let warn = false;
  const f = car.forward();
  traffic.forEachCar((c) => {
    const d = c.pos.clone().sub(car.pos);
    const L = d.length();
    if (L < 32 && d.dot(f) > 0 && Math.abs(d.y) < 4) {
      const closing = -(c.vel.clone().sub(car.vel)).dot(d.normalize());
      if (closing > 6) warn = true;
    }
  });
  $('warn').style.opacity = warn ? (0.6 + 0.4 * Math.sin(performance.now() / 70)) : 0;
  if (msgTimer > 0) { msgTimer -= dt; if (msgTimer <= 0) $('msg').style.opacity = 0; }
}

function updateRace(dt, inp) {
  if (!raceRunning && cpIndex === 0 && raceTime === 0 && (inp.throttle > 0)) { raceRunning = true; msgTimer = Math.min(msgTimer, 0.3); }
  if (raceRunning) raceTime += dt;
  const cp = checkpoints[cpIndex];
  if (!cp) return;
  const d = new THREE.Vector2(car.pos.x - cp.x, car.pos.z - cp.z).length();
  if (d < 6.5 && Math.abs(car.pos.y - cp.y) < 5) {
    cpIndex++;
    if (cpIndex >= checkpoints.length) {
      raceRunning = false;
      const total = raceTime + penalties;
      const rec = !bestTime || total < bestTime;
      if (rec) { bestTime = total; localStorage.setItem('frontier-mine-best-' + P.seed, String(total)); }
      message(`FINISH ${fmt(total)}<small>${rec ? 'NEW BEST · ' : ''}ENTER to race again</small>`, 5);
      gateGroup.visible = false;
    } else {
      message(`CHECKPOINT ${cpIndex}<small>${fmt(raceTime + penalties)}</small>`, 1.1);
      placeGate();
    }
  }
}

// ---------------------------------------------------------------- loop
rebuild(true);
camPos.copy(car.pos).add(new THREE.Vector3(0, 3, -8));
message('MINE RUN<small>W to start · hit all 8 junction checkpoints · dodge the ore trains</small>', 4);

const clock = new THREE.Timer();
function frame() {
  clock.update(); const dt = Math.min(clock.getDelta(), 1 / 20);
  const inp = editor.active ? { throttle: 0, brake: 0, steer: 0, handbrake: true, boost: false } : readInput();
  if (!editor.active) {
    car.step(dt, inp);
    collideCarts();
    if (car.pos.y < -80) resetCarToTrack();
    updateRace(dt, inp);
  }
  traffic.update(dt);
  syncCarMesh(carMesh, car);
  carMesh.userData.tailMat.emissiveIntensity = inp.brake ? 6 : 2.2;
  lightTimer -= dt;
  if (lightTimer <= 0) { assignLights(); lightTimer = 0.12; }
  gateRing.rotation.z += dt; gateMesh.material.opacity = 0.12 + 0.06 * Math.sin(performance.now() / 200);
  if (editor.active) editor.update(); else updateCamera(dt);
  updateHUD(dt);
  updateAudio(inp);
  drawMinimap();
  composer.render();
  requestAnimationFrame(frame);
}
frame();

window.addEventListener('resize', () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
  composer.setSize(window.innerWidth, window.innerHeight);
});

// debug handle
window.frontier = { scene, editor, get mine() { return mine; }, get net() { return net; }, car, traffic, P, rebuild };

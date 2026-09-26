// D-Day: Atlantic Wall — game bootstrap, loop, camera, tide, HUD
import * as THREE from '../vendor/three.module.min.js';
import { clamp, lerp, smooth, rng, TAU } from './util.js';
import { Terrain, profileAt, X0, X1, Z0, Z1, NX, NZ, CELL } from './terrain.js';
import { buildProps } from './props.js';
import { buildCar, MAT } from './models.js';
import { Vehicle } from './vehicle.js';
import { Enemies } from './enemies.js';
import { FX } from './fx.js';
import { Audio } from './audio.js';
import { WALL_Z, START, GATE_HW, ROAD_HW } from './layout.js';

const $ = s => document.querySelector(s);
const DIFF = [
  { name: 'Recruit', bulletDmg: 1.2, tide: 0.82, planeGap: [32, 44] },
  { name: 'Veteran', bulletDmg: 2.0, tide: 1.0, planeGap: [20, 30] },
  { name: 'Legend', bulletDmg: 2.8, tide: 1.12, planeGap: [13, 20] },
];
const settings = { diff: 1, paint: '#56603a' };

// ---------------------------------------------------------------- renderer
const canvas = $('#gl');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 1.75));
renderer.setSize(innerWidth, innerHeight);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.05;
renderer.outputColorSpace = THREE.SRGBColorSpace;

const scene = new THREE.Scene();
const HORIZON = new THREE.Color('#c3cdd1');
scene.fog = new THREE.Fog(HORIZON, 140, 950);
scene.background = HORIZON;
const camera = new THREE.PerspectiveCamera(62, innerWidth / innerHeight, 0.3, 4000);

// lights
const hemi = new THREE.HemisphereLight(0xdfe8ef, 0x6b5f48, 1.25); scene.add(hemi);
const sun = new THREE.DirectionalLight(0xfff1dc, 2.6);
const SUN_DIR = new THREE.Vector3(0.45, 0.75, -0.5).normalize();
sun.castShadow = true;
sun.shadow.mapSize.set(2048, 2048);
Object.assign(sun.shadow.camera, { left: -70, right: 70, top: 70, bottom: -70, near: 1, far: 400 });
sun.shadow.bias = -0.0006; sun.shadow.normalBias = 0.6;
scene.add(sun, sun.target);

// sky dome
{
  const g = new THREE.SphereGeometry(3000, 24, 12);
  const m = new THREE.ShaderMaterial({
    side: THREE.BackSide, depthWrite: false, fog: false,
    uniforms: { top: { value: new THREE.Color('#6f90ab') }, hor: { value: HORIZON }, sunDir: { value: SUN_DIR } },
    vertexShader: 'varying vec3 vP; void main(){ vP = normalize(position); gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.); }',
    fragmentShader: `varying vec3 vP; uniform vec3 top; uniform vec3 hor; uniform vec3 sunDir;
      void main(){ float h = max(vP.y, 0.0); vec3 c = mix(hor, top, pow(h, 0.55));
        float s = max(dot(normalize(vP), sunDir), 0.0); c += vec3(1.0,0.9,0.7) * (pow(s, 600.0) * 1.5 + pow(s, 12.0) * 0.18);
        if (vP.y < 0.0) c = hor; gl_FragColor = vec4(c, 1.0); }`,
  });
  const sky = new THREE.Mesh(g, m); sky.renderOrder = -1; scene.add(sky);
  scene.userData.sky = sky;
}
// low-poly clouds
{
  const r = rng(77), geo = new THREE.IcosahedronGeometry(1, 0);
  const mat = new THREE.MeshStandardMaterial({ color: 0xf2f4f5, flatShading: true, roughness: 1, emissive: 0x6d747a, emissiveIntensity: 0.35 });
  const n = 70, im = new THREE.InstancedMesh(geo, mat, n);
  const m = new THREE.Matrix4();
  for (let i = 0; i < n; i++) {
    m.compose(new THREE.Vector3(r.range(-1400, 1400), r.range(230, 330), r.range(-1400, 2600)),
      new THREE.Quaternion().setFromEuler(new THREE.Euler(0, r() * TAU, 0)), new THREE.Vector3(r.range(40, 110), r.range(10, 22), r.range(25, 60)));
    im.setMatrixAt(i, m);
  }
  scene.add(im);
}

// ---------------------------------------------------------------- world
const T = new Terrain();
scene.add(T.buildMesh());
scene.add(T.buildRoadMeshes());
const world = buildProps(scene, T);

// water (low-poly animated waves with shallow tint + foam from the heightmap)
const hTex = (() => {
  const data = new Uint16Array(NX * NZ);
  for (let i = 0; i < NX * NZ; i++) data[i] = THREE.DataUtils.toHalfFloat(T.h[i]);
  const t = new THREE.DataTexture(data, NX, NZ, THREE.RedFormat, THREE.HalfFloatType);
  t.magFilter = t.minFilter = THREE.LinearFilter; t.needsUpdate = true; return t;
})();
const water = { level: -0.75, front: -6, mesh: null };
{
  const g = new THREE.PlaneGeometry(4200, 5200, 140, 170); g.rotateX(-Math.PI / 2); g.translate(0, 0, 200);
  const m = new THREE.ShaderMaterial({
    transparent: true,
    uniforms: {
      time: { value: 0 }, level: { value: water.level }, hmap: { value: hTex }, sunDir: { value: SUN_DIR },
      origin: { value: new THREE.Vector2(X0, Z0) }, size: { value: new THREE.Vector2(X1 - X0, Z1 - Z0) },
      fogColor: { value: scene.fog.color }, fogNear: { value: scene.fog.near }, fogFar: { value: scene.fog.far },
    },
    vertexShader: `uniform float time; uniform float level; varying vec3 vW; varying float vFog;
      void main(){ vec3 p = position; vec4 w = modelMatrix * vec4(p,1.);
        float wv = sin(w.x*0.045 + time*0.9)*0.28 + sin(w.z*0.06 - time*1.3 + w.x*0.01)*0.35 + sin((w.x+w.z)*0.11 + time*1.7)*0.12;
        w.y = level + wv; vW = w.xyz; vec4 mv = viewMatrix * w; vFog = -mv.z; gl_Position = projectionMatrix * mv; }`,
    fragmentShader: `uniform sampler2D hmap; uniform vec2 origin; uniform vec2 size; uniform float level; uniform float time; uniform vec3 sunDir;
      uniform vec3 fogColor; uniform float fogNear; uniform float fogFar; varying vec3 vW; varying float vFog;
      void main(){
        vec3 n = normalize(cross(dFdx(vW), dFdy(vW))); if (n.y < 0.0) n = -n;
        vec2 uv = (vW.xz - origin) / size; float h = -14.0;
        if (uv.x > 0.0 && uv.x < 1.0 && uv.y > 0.0 && uv.y < 1.0) h = texture2D(hmap, uv).r;
        float depth = vW.y - h;
        vec3 deep = vec3(0.10, 0.27, 0.34), shallow = vec3(0.30, 0.52, 0.52);
        vec3 c = mix(shallow, deep, smoothstep(0.0, 5.0, depth));
        float dif = max(dot(n, sunDir), 0.0);
        c *= 0.62 + 0.5 * dif;
        vec3 v = normalize(cameraPosition - vW); vec3 hv = normalize(v + sunDir);
        c += vec3(1.0,0.95,0.85) * pow(max(dot(n, hv), 0.0), 60.0) * 0.5;
        float foam = 1.0 - smoothstep(0.0, 0.55, depth);
        foam += (1.0 - smoothstep(0.0, 1.6, depth)) * step(0.72, fract(depth * 1.3 - time * 0.35)) * 0.5;
        c = mix(c, vec3(0.93, 0.95, 0.94), clamp(foam, 0.0, 1.0) * 0.85);
        float a = mix(0.55, 0.93, smoothstep(0.0, 2.5, depth));
        a = max(a, clamp(foam,0.0,1.0) * 0.9);
        c = mix(c, fogColor, smoothstep(fogNear, fogFar, vFog));
        gl_FragColor = vec4(c, a);
      }`,
  });
  water.mesh = new THREE.Mesh(g, m); water.mesh.frustumCulled = false; water.mesh.renderOrder = 2; scene.add(water.mesh);
}

const fx = new FX(scene, scene.fog);
const audio = new Audio();
const enemies = new Enemies(scene, T, world, fx, audio);

// ---------------------------------------------------------------- car
let carMesh = buildCar(settings.paint);
scene.add(carMesh);
const car = new Vehicle(carMesh, T, world);
car.reset(START.x, START.z, START.yaw);
console.log('[dday] car faces flipped by winding fix:', carMesh.userData.flipped);
function repaint(hex) {
  scene.remove(carMesh);
  carMesh = buildCar(hex);
  scene.add(carMesh); car.mesh = carMesh;
  car.update(0.0001, { throttle: 0, brake: 0, steer: 0 }, water.level);
}

// ---------------------------------------------------------------- game state
const game = {
  state: 'menu', time: 0, live: false, water, tideClock: 0, bulletDmg: 2, planeGap: [20, 30], tideMul: 1,
  hits: 0, cp: 0, shake: 0, camMode: 0, deathCause: '', countdown: 0, maxZ: 0, paused: false,
  explode,
};
const keys = {};
addEventListener('keydown', e => {
  keys[e.code] = true;
  if (['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Space'].includes(e.code)) e.preventDefault();
  if (e.code === 'KeyC') game.camMode = (game.camMode + 1) % 3;
  if (e.code === 'KeyM') audio.setMuted(!audio.muted);
  if ((e.code === 'KeyP' || e.code === 'Escape') && (game.state === 'live' || game.state === 'paused')) togglePause();
  if (e.code === 'KeyR' && game.state === 'live') respawn(true);
  if (e.code === 'Enter' && game.state === 'menu') startGame();
});
addEventListener('keyup', e => { keys[e.code] = false; });
const input = () => ({
  throttle: (keys.KeyW || keys.ArrowUp) ? 1 : 0,
  brake: (keys.KeyS || keys.ArrowDown) ? 1 : 0,
  steer: ((keys.KeyA || keys.ArrowLeft) ? 1 : 0) - ((keys.KeyD || keys.ArrowRight) ? 1 : 0),
  handbrake: !!keys.Space,
});

function tideSpeed(t) { return Math.min(3.2 + 0.022 * t, 8.5) * game.tideMul; }

function startGame() {
  audio.init();
  const d = DIFF[settings.diff];
  game.bulletDmg = d.bulletDmg; game.planeGap = d.planeGap; game.tideMul = d.tide;
  game.cp = 0; world.checkpoints.forEach((c, i) => { c.reached = i === 0; c.flag.userData.flagMat.color.set(i === 0 ? 0x5ee07a : 0x1f5eff); });
  restartRun(0);
  $('#menu').classList.add('hide');
}
function restartRun(cpIndex) {
  const cp = world.checkpoints[cpIndex];
  let yaw = START.yaw, x = cp.x, z = cp.z;
  if (cpIndex > 0) {
    const ri = T.roadInfo(x, z);
    if (ri.road) { const s = ri.road.samples[Math.min(ri.i + 4, ri.road.samples.length - 1)]; yaw = Math.atan2(s.tx, s.tz); x = s.x; z = s.z; }
  }
  car.reset(x, z, yaw);
  enemies.reset();
  if (cpIndex === 0) { game.time = 0; game.tideClock = 0; water.front = -24; game.hits = 0; }
  else { water.front = Math.min(water.front, z - 190); game.tideClock = Math.max(0, solveTideClock(water.front)); }
  water.level = profileAt(water.front);
  game.maxZ = z;
  game.state = 'countdown'; game.countdown = cpIndex === 0 ? 3.2 : 2.2; game.live = false;
  $('#end').classList.add('hide');
  $('#hud').classList.add('on');
  camSnap = true;
}
function solveTideClock(front) { // find tide clock value that corresponds to a given front position
  let z = -24, t = 0; while (z < front && t < 900) { z += tideSpeed(t) * 0.1; t += 0.1; } return t;
}
function respawn(manual) {
  if (manual) { car.damage(15, 'respawn'); if (car.hp <= 0) return die('respawn'); }
  const hp = car.hp;
  restartRun(game.cp);
  if (manual) car.hp = Math.max(30, hp);
}
function die(cause) {
  if (game.state !== 'live') return;
  game.state = 'dead'; game.live = false; game.deathCause = cause;
  const msgs = {
    mg: ['Cut down', 'The MG nests had you bracketed. Keep moving and use the ground to break their line of sight.'],
    bomb: ['Dive-bombed', 'A Stuka put a bomb on you. Watch for the red arrows, then brake hard or swerve when you hear the whistle.'],
    teller: ['Hit a Teller mine', 'Anti-tank mines sit in the sand and on the roads. Look for the dug-up rings.'],
    ap: ['Minefield', 'Those ACHTUNG MINEN signs were a warning. The grass is full of buried mines.'],
    stake: ['Hit a mined stake', 'Rommel\'s asparagus: every leaning log on the beach has a mine on top.'],
    tide: ['Swept away', 'The tide caught up with you. Keep moving. The water doesn\'t wait.'],
    wire: ['Tangled in wire', 'Barbed wire tears the car apart. Find the gaps.'],
    respawn: ['Out of repairs', 'Nothing left to patch up.'],
  };
  const [title, msg] = msgs[cause] || ['Knocked out', 'The car couldn\'t take any more damage.'];
  setTimeout(() => showEnd(title, msg, false), 1400);
}
function win() {
  game.state = 'won'; game.live = false;
  const t = game.time;
  const best = +localStorage.getItem('dday-best-' + settings.diff) || Infinity;
  if (t < best) localStorage.setItem('dday-best-' + settings.diff, t);
  alertMsg('THE WALL IS BREACHED', 'blue', 4000);
  setTimeout(() => showEnd('Wall breached', `You broke through the Atlantic Wall in ${fmt(t)} with ${Math.round(car.hp)}% hull left.${t < best ? ' New best time on ' + DIFF[settings.diff].name + '.' : ''}`, true), 1800);
}
function showEnd(title, msg, won) {
  $('#endTitle').textContent = title; $('#endMsg').textContent = msg;
  $('#endBrand').textContent = won ? 'VICTORY' : 'KIA';
  $('#endBrand').style.color = won ? '#1f5eff' : '#c0392b';
  $('#stTime').textContent = fmt(game.time);
  $('#stDist').textContent = Math.round(Math.max(0, game.maxZ)) + ' m';
  $('#stHits').textContent = game.hits;
  $('#retryCp').style.display = won || game.cp === 0 ? 'none' : '';
  $('#retryCp').textContent = `Retry from ${world.checkpoints[game.cp].label}`;
  $('#end').classList.remove('hide');
}
function togglePause() {
  if (game.state === 'live') { game.state = 'paused'; $('#pause').classList.remove('hide'); }
  else if (game.state === 'paused') { game.state = 'live'; $('#pause').classList.add('hide'); }
}

// explosions (mines, bombs)
function explode(x, y, z, o) {
  const dist = Math.hypot(car.pos.x - x, car.pos.z - z), dy = car.pos.y - y;
  const r = o.radius;
  fx.explosion(x, y, z, o.size, { water: o.water, dirt: z < 330 ? new THREE.Color('#bda775') : new THREE.Color('#6b5a44') });
  audio.boom(Math.hypot(camera.position.x - x, camera.position.z - z), o.size);
  const d3 = Math.hypot(dist, dy);
  game.shake = Math.max(game.shake, clamp(1.4 * o.size * (1 - d3 / 120), 0, 1.6));
  if (d3 < r && car.hp > 0) {
    const k = 1 - d3 / r;
    car.damage(o.dmg * (0.25 + 0.75 * k * k), o.cause);
    if (k > 0.35) car.launch(x, z, o.lift * k);
    flashVignette(1);
  }
  if (o.crater && !o.water) { T.deform(x, z, o.crater, o.crater * 0.35); craterDirty = true; }
  // chain reaction
  for (const m of world.mines.query(x, z, 5.5, [])) if (m.alive && Math.hypot(m.x - x, m.z - z) < 5.5 && Math.hypot(m.x - x, m.z - z) > 0.1) setTimeout(() => detonate(m, 'chain'), 120 + Math.random() * 200);
}
function detonate(m, cause) {
  if (!m.alive) return;
  m.alive = false; world.hideMine(m);
  const size = m.type === 'ap' ? 0.65 : 1.0;
  explode(m.x, m.y, m.z, { radius: m.type === 'ap' ? 5 : 7, dmg: m.dmg, size, lift: m.lift, crater: m.type === 'ap' ? 1.2 : 2.4, cause: m.type });
}
let craterDirty = false;

// ---------------------------------------------------------------- HUD helpers
const alertsEl = $('#alerts');
const activeAlerts = new Map();
function alertMsg(text, cls = '', ms = 2200, key = text) {
  let el = activeAlerts.get(key);
  if (!el) { el = document.createElement('div'); el.className = 'alert ' + cls; alertsEl.appendChild(el); activeAlerts.set(key, el); }
  el.textContent = text; clearTimeout(el._t);
  el._t = setTimeout(() => { el.remove(); activeAlerts.delete(key); }, ms);
}
let vigT = 0;
function flashVignette(v) { vigT = Math.max(vigT, v); }
const fmt = t => `${Math.floor(t / 60)}:${(t % 60).toFixed(1).padStart(4, '0')}`;

// progress checkpoints on the track
{
  const track = $('#track');
  world.checkpoints.forEach((c, i) => { if (i === 0) return; const d = document.createElement('div'); d.className = 'cp'; d.style.left = (c.z / WALL_Z * 100) + '%'; track.appendChild(d); c.el = d; });
}
// minimap background
const MAP_S = 1.0; // px per metre (on the offscreen map)
const mapBg = document.createElement('canvas');
{
  mapBg.width = Math.round((X1 - X0) * MAP_S); mapBg.height = Math.round((Z1 - Z0) * MAP_S);
  const g = mapBg.getContext('2d');
  const W = mapBg.width, Hh = mapBg.height;
  const img = g.createImageData(W, Hh);
  for (let py = 0; py < Hh; py++) {
    const z = Z1 - py / MAP_S;
    for (let px = 0; px < W; px++) {
      const x = X1 - px / MAP_S; // mirrored so screen-right matches the driver's right
      const h = T.heightAt(x, z);
      let c = z < 330 ? [196, 178, 132] : [98, 116, 64];
      if (h < -0.8) c = [40, 80, 100];
      const ax = Math.abs(x); if (ax > 176) c = [60, 64, 52];
      const shade = clamp(0.8 + (T.heightAt(x - 2, z) - h) * 0.25, 0.55, 1.2);
      const i = (py * W + px) * 4;
      img.data[i] = c[0] * shade; img.data[i + 1] = c[1] * shade; img.data[i + 2] = c[2] * shade; img.data[i + 3] = 255;
    }
  }
  g.putImageData(img, 0, 0);
  const P = (x, z) => [(X1 - x) * MAP_S, (Z1 - z) * MAP_S];
  // trenches
  g.strokeStyle = 'rgba(60,44,30,.95)'; g.lineWidth = 4;
  T.trenches.forEach(pts => { g.beginPath(); pts.forEach((p, i) => { const [a, b] = P(p[0], p[1]); i ? g.lineTo(a, b) : g.moveTo(a, b); }); g.stroke(); });
  g.strokeStyle = 'rgba(40,60,30,.95)'; g.lineWidth = 4;
  T.hedges.forEach(pts => { g.beginPath(); pts.forEach((p, i) => { const [a, b] = P(p[0], p[1]); i ? g.lineTo(a, b) : g.moveTo(a, b); }); g.stroke(); });
  // roads
  T.roads.forEach(r => {
    g.strokeStyle = 'rgba(30,26,20,.6)'; g.lineWidth = 11; g.lineCap = 'round'; g.lineJoin = 'round';
    g.beginPath(); r.samples.forEach((s, i) => { const [a, b] = P(s.x, s.z); i ? g.lineTo(a, b) : g.moveTo(a, b); }); g.stroke();
    g.strokeStyle = '#e9dfc4'; g.lineWidth = 6; g.stroke();
  });
  // wall
  g.fillStyle = '#d8d6ce'; const [wx0, wy] = P(250, WALL_Z + 12); g.fillRect(0, wy, W, 14 * MAP_S);
  g.fillStyle = '#1f5eff'; const [gx] = P(GATE_HW, 0); g.fillRect(gx, wy, GATE_HW * 2 * MAP_S, 14 * MAP_S);
}
const mm = $('#minimap'), mctx = mm.getContext('2d');
function drawMinimap() {
  const S = mm.width, view = 360, k = S / view; // metres shown
  mctx.save();
  mctx.clearRect(0, 0, S, S);
  const cx = (X1 - car.pos.x) * MAP_S, cy = (Z1 - car.pos.z) * MAP_S;
  mctx.drawImage(mapBg, cx - view / 2 * MAP_S, cy - view / 2 * MAP_S, view * MAP_S, view * MAP_S, 0, 0, S, S);
  const P = (x, z) => [S / 2 + (car.pos.x - x) * k, S / 2 - (z - car.pos.z) * k];
  // tide
  const [, ty] = P(0, water.front);
  if (ty < S) { mctx.fillStyle = 'rgba(30,110,170,.55)'; mctx.fillRect(0, Math.max(0, ty), S, S - Math.max(0, ty)); mctx.fillStyle = 'rgba(200,235,255,.9)'; mctx.fillRect(0, ty - 1, S, 2); }
  // turrets
  for (const t of world.turrets) {
    const [a, b] = P(t.pos.x, t.pos.z); if (a < -10 || a > S + 10 || b < -10 || b > S + 10) continue;
    mctx.fillStyle = t.alert > 0.5 ? '#ff3b30' : '#b8453a';
    mctx.fillRect(a - 5, b - 5, 10, 10);
    if (t.alert > 0.5) { mctx.strokeStyle = 'rgba(255,60,48,.5)'; mctx.lineWidth = 2; mctx.beginPath(); mctx.arc(a, b, 11 + Math.sin(game.time * 8) * 2, 0, TAU); mctx.stroke(); }
  }
  // crates & checkpoints
  for (const c of world.crates) if (c.alive) { const [a, b] = P(c.x, c.z); mctx.fillStyle = '#6dff8a'; mctx.fillRect(a - 2, b - 7, 4, 14); mctx.fillRect(a - 7, b - 2, 14, 4); }
  for (const c of world.checkpoints) { const [a, b] = P(c.x, c.z); mctx.fillStyle = c.reached ? '#5ee07a' : '#4d80ff'; mctx.beginPath(); mctx.arc(a, b, 6, 0, TAU); mctx.fill(); }
  // planes & bombs
  for (const p of enemies.planes) {
    const [a, b] = P(p.pos.x, p.pos.z);
    mctx.save(); mctx.translate(clamp(a, 8, S - 8), clamp(b, 8, S - 8)); mctx.rotate(-(p.heading) + Math.PI);
    mctx.fillStyle = '#ff5a4a'; mctx.beginPath(); mctx.moveTo(0, -12); mctx.lineTo(9, 9); mctx.lineTo(0, 4); mctx.lineTo(-9, 9); mctx.closePath(); mctx.fill(); mctx.restore();
  }
  for (const b of enemies.bombs) { const [a, c] = P(b.pos.x, b.pos.z); mctx.fillStyle = '#ffcc33'; mctx.beginPath(); mctx.arc(a, c, 4, 0, TAU); mctx.fill(); }
  // car
  mctx.translate(S / 2, S / 2); mctx.rotate(car.yaw);
  mctx.fillStyle = '#fff'; mctx.strokeStyle = '#1f5eff'; mctx.lineWidth = 3;
  mctx.beginPath(); mctx.moveTo(0, -13); mctx.lineTo(9, 10); mctx.lineTo(0, 5); mctx.lineTo(-9, 10); mctx.closePath(); mctx.stroke(); mctx.fill();
  mctx.restore();
}
// plane warning arrows
const pwEl = $('#planewarn');
const arrows = [];
function updatePlaneArrows() {
  while (arrows.length < enemies.planes.length) { const i = document.createElement('i'); pwEl.appendChild(i); arrows.push(i); }
  arrows.forEach((el, i) => {
    const p = enemies.planes[i];
    if (!p || p.dropped) { el.style.display = 'none'; return; }
    const v = p.pos.clone().project(camera);
    const onScreen = v.z < 1 && Math.abs(v.x) < 0.95 && Math.abs(v.y) < 0.9;
    if (onScreen) { el.style.display = 'none'; return; }
    let ang = Math.atan2(v.x, v.y); if (v.z > 1) ang += Math.PI;
    const rr = Math.min(innerWidth, innerHeight) * 0.36;
    el.style.display = 'block';
    el.style.transform = `translate(${Math.sin(ang) * rr - 12}px, ${-Math.cos(ang) * rr - 11}px) rotate(${ang}rad)`;
  });
}

// ---------------------------------------------------------------- camera
let camSnap = true;
const camPos = new THREE.Vector3(), camLook = new THREE.Vector3();
function updateCamera(dt) {
  const f = car.forward();
  const vf = car.vel.clone(); vf.y = 0;
  const lookDir = vf.length() > 3 && vf.dot(f) > 0 ? vf.normalize().lerp(f, 0.6).normalize() : f;
  const cfg = [[8.6, 3.3, 1.3], [15, 6.5, 1.6], [0.2, 1.25, 1.2]][game.camMode];
  let want, look;
  if (game.camMode === 2) {
    want = car.mesh.localToWorld(new THREE.Vector3(0, 1.28, 0.35));
    look = car.mesh.localToWorld(new THREE.Vector3(0, 1.1, 12));
  } else {
    want = car.pos.clone().addScaledVector(lookDir, -cfg[0]); want.y += cfg[1];
    look = car.pos.clone().addScaledVector(f, 4); look.y += cfg[2];
    want.y = Math.max(want.y, T.heightAt(want.x, want.z) + 1.0, water.level + 0.8);
  }
  const k = camSnap ? 1 : 1 - Math.exp(-(game.camMode === 2 ? 30 : 6) * dt);
  camPos.lerp(want, k); camLook.lerp(look, camSnap ? 1 : 1 - Math.exp(-10 * dt));
  camSnap = false;
  camera.position.copy(camPos);
  if (game.shake > 0) {
    const s = game.shake * 0.35;
    camera.position.x += (Math.random() - 0.5) * s; camera.position.y += (Math.random() - 0.5) * s; camera.position.z += (Math.random() - 0.5) * s;
    game.shake = Math.max(0, game.shake - dt * 2.2);
  }
  camera.lookAt(camLook);
  const tf = 60 + clamp(car.speed / 34, 0, 1) * 12;
  if (Math.abs(camera.fov - tf) > 0.1) { camera.fov = lerp(camera.fov, tf, 1 - Math.exp(-3 * dt)); camera.updateProjectionMatrix(); }
}
// menu flyover
let menuT = 0;
function menuCamera(dt) {
  menuT += dt * 0.05;
  const a = menuT;
  camera.position.set(Math.sin(a) * 90, 22 + Math.sin(a * 0.7) * 6, 60 + Math.cos(a) * 70);
  camera.lookAt(0, 4, 190);
}

// ---------------------------------------------------------------- loop
const clock = new THREE.Clock();
let emitT = 0, smokeT = 0, lastSpot = 0, hudT = 0;
function frame() {
  requestAnimationFrame(frame);
  let dt = Math.min(clock.getDelta(), 1 / 30);
  if (game.state === 'paused') { renderer.render(scene, camera); return; }
  const tNow = performance.now() / 1000;
  water.mesh.material.uniforms.time.value = tNow;

  if (game.state === 'countdown') {
    game.countdown -= dt;
    const n = Math.ceil(game.countdown - 0.2);
    $('#count').textContent = game.countdown > 0.2 ? (n > 0 ? n : 'GO') : '';
    if (game.countdown <= 0.2 && !game.live) { game.state = 'live'; game.live = true; alertMsg('REACH THE GATE IN THE ATLANTIC WALL', 'blue', 3000); }
  } else $('#count').textContent = '';

  const running = game.state === 'live' || game.state === 'dead' || game.state === 'won' || game.state === 'countdown';
  if (running) {
    if (game.state === 'live') {
      game.time += dt; game.tideClock += dt;
      water.front += tideSpeed(game.tideClock) * dt;
      water.level = profileAt(water.front);
    }
    const inp = game.state === 'live' ? input() : { throttle: 0, brake: game.state === 'won' ? 1 : 0, steer: 0 };
    const sub = 3;
    for (let i = 0; i < sub; i++) car.update(dt / sub, inp, water.level);
    game.maxZ = Math.max(game.maxZ, car.pos.z);

    // vehicle events
    for (const e of car.events) {
      if (e.type === 'damage') { game.lastCause = e.cause; if (e.cause === 'mg') game.hits++; if (e.v > 3) flashVignette(Math.min(1, e.v / 25)); }
      if (e.type === 'crash') { fx.spark(e.x, car.pos.y + 0.6, e.z, 8); audio.boom(30, 0.12); game.shake = Math.max(game.shake, Math.min(0.8, e.v / 20)); }
      if (e.type === 'trigger') detonate(e.mine, 'stake');
      if (e.type === 'land' && e.v > 5) { fx.dust(car.pos.x, car.pos.y, car.pos.z, new THREE.Color('#a89775'), 8, 1.2); game.shake = Math.max(game.shake, e.v / 30); }
    }
    car.events.length = 0;

    if (game.state === 'live') {
      // mines under the car
      if (car.pos.y - T.groundAt(car.pos.x, car.pos.z) < 0.9) {
        for (const m of world.mines.query(car.pos.x, car.pos.z, 3, [])) {
          if (m.alive && m.r > 0 && Math.hypot(m.x - car.pos.x, m.z - car.pos.z) < m.r) detonate(m, m.type);
        }
      }
      // crates
      for (const c of world.crates) if (c.alive && Math.hypot(c.x - car.pos.x, c.z - car.pos.z) < 3) {
        c.alive = false; c.mesh.visible = false; c.marker.visible = false;
        car.hp = Math.min(100, car.hp + 40); audio.pickup(); alertMsg('+40 HULL · FIELD REPAIR', 'green', 1800);
      }
      // checkpoints
      world.checkpoints.forEach((c, i) => {
        if (!c.reached && Math.hypot(c.x - car.pos.x, c.z - car.pos.z) < 16) {
          c.reached = true; game.cp = Math.max(game.cp, i); c.flag.userData.flagMat.color.set(0x5ee07a);
          if (c.el) c.el.classList.add('ok');
          alertMsg('CHECKPOINT · ' + c.label.toUpperCase(), 'blue', 2200); audio.pickup();
        }
      });
      // drowning
      if (car.inWater > 1.0) { car.damage(40 * dt, 'tide'); if (car.inWater > 1.6) car.damage(100, 'tide'); }
      if (car.inWater > 0.3) alertMsg('WATER RISING!', 'red', 600, 'water');
      // finish
      if (car.pos.z > WALL_Z - 3 && Math.abs(car.pos.x) < GATE_HW - 0.5) win();
      if (car.hp <= 0) die(game.lastCause || 'mg');
      // spotted warnings
      if (enemies.spotted > 0 && tNow - lastSpot > 3) { lastSpot = tNow; alertMsg(enemies.spotted > 1 ? `${enemies.spotted} MG NESTS FIRING` : 'MG NEST HAS YOU', 'red', 1800, 'mg'); }
      if (car.inWire) alertMsg('BARBED WIRE', 'red', 500, 'wire');
    }
    enemies.update(dt, car, game);
    audio.engine(car.speed, input().throttle, game.state !== 'menu');
    audio.surf(clamp(0.12 - (car.pos.z - water.front) / 2500, 0.01, 0.12));

    // emitters: dust, damage smoke, wreck smoke, splashes
    emitT -= dt;
    if (emitT <= 0) {
      emitT = 0.05;
      const f = car.forward(), sp = car.speed;
      if (car.grounded && sp > 5 && car.surface !== 'road') {
        const col = car.surface === 'wet' || car.surface === 'sand' ? new THREE.Color('#d8c597') : new THREE.Color('#8a7a5a');
        for (const s of [-0.8, 0.8]) fx.dust(car.pos.x - f.x * 1.5 + f.z * s, car.pos.y + 0.2, car.pos.z - f.z * 1.5 - f.x * s, col, 1, 0.5 + sp / 40, 0.6);
      }
      if (car.inWater > 0 && sp > 2) fx.splash(car.pos.x - f.x * 1.2, water.level, car.pos.z - f.z * 1.2, 2);
      if (car.hp < 55) fx.smokePuff(car.pos.x + f.x * 1.6, car.pos.y + 1, car.pos.z + f.z * 1.6, car.hp < 25 ? 0.12 : 0.55, 0.5, car.hp < 25);
    }
    smokeT -= dt;
    if (smokeT <= 0) {
      smokeT = 0.3;
      for (const s of world.smokers) if (Math.abs(s.z - car.pos.z) < 350) fx.smokePuff(s.x, s.y, s.z, 0.14, 1.3, Math.random() < 0.5);
    }
    // crates bob
    for (const c of world.crates) if (c.alive) { c.marker.position.y = c.y + 1.9 + Math.sin(tNow * 3 + c.x) * 0.2; c.marker.rotation.y = tNow * 2; }
    // flags wave
    for (const c of world.checkpoints) { const p = c.flag.userData.flag.geometry.attributes.position; for (let i = 0; i < p.count; i++) { const x = p.getX(i) + 1.2; p.setZ(i, Math.sin(tNow * 5 + x * 2.2) * 0.12 * x); } p.needsUpdate = true; }
    updateCamera(dt);
  } else {
    menuCamera(dt);
  }
  // boats bob with the tide
  for (const b of world.boats) {
    const y = b.ship ? -1.2 : water.level + 0.2 + Math.sin(tNow * 1.3 + b.phase) * 0.18;
    b.obj.position.y = b.main ? Math.max(y, water.level + 0.1) : y;
    if (!b.ship) b.obj.rotation.z = Math.sin(tNow * 1.1 + b.phase) * 0.04;
  }
  water.mesh.material.uniforms.level.value = water.level;
  if (craterDirty) { // refresh water heightmap sparsely
    craterDirty = false;
  }
  fx.update(dt);

  // shadows follow the camera focus
  const focus = game.state === 'menu' ? new THREE.Vector3(0, 0, 150) : car.pos;
  sun.target.position.copy(focus);
  sun.position.copy(focus).addScaledVector(SUN_DIR, 200);
  scene.userData.sky.position.copy(camera.position);

  // HUD
  hudT -= dt;
  if (game.state !== 'menu' && hudT <= 0) {
    hudT = 0.05;
    const hp = Math.round(car.hp);
    $('#hpTxt').textContent = hp + '%';
    const bar = $('#hpBar'); bar.style.width = hp + '%'; bar.classList.toggle('low', hp < 35);
    $('#spd').textContent = Math.round(car.speed * 3.6);
    const surfName = { road: 'DIRT ROAD', sand: 'SOFT SAND', wet: 'WET SAND · SLOW', grass: 'GRASS', mud: 'TRENCH MUD · SLOW' }[car.surface];
    const s = $('#surf'); s.textContent = car.inWire ? 'BARBED WIRE · TEARING HULL' : car.inWater > 0.05 ? 'WATER · ENGINE FLOODING' : surfName;
    s.classList.toggle('warn', car.inWire || car.inWater > 0.05 || car.surface === 'mud' || car.surface === 'wet');
    const toWall = Math.max(0, WALL_Z - car.pos.z);
    $('#distTxt').textContent = Math.round(toWall).toLocaleString() + ' m to the Wall';
    $('#meDot').style.left = clamp(car.pos.z / WALL_Z * 100, 0, 100) + '%';
    $('#tideBar').style.width = clamp(water.front / WALL_Z * 100, 0, 100) + '%';
    const gap = car.pos.z - water.front;
    const tt = $('#tideTxt');
    tt.innerHTML = gap < 70 ? `<span class="danger">TIDE ${Math.max(0, Math.round(gap))} m BEHIND YOU</span>` : `Tide ${Math.round(gap)} m behind`;
    let zone = 'Omaha Beach';
    if (car.pos.z > 330) zone = 'The Bluffs'; if (car.pos.z > 470) zone = 'Bocage'; if (car.pos.z > 700) zone = 'The Mounds';
    if (car.pos.z > 1080) zone = 'No Man\'s Land'; if (car.pos.z > 1470) zone = 'Dragon\'s Teeth';
    $('#zoneTxt').textContent = zone;
    $('#timer').textContent = fmt(game.time);
    $('#spotTxt').textContent = enemies.spotted ? `${enemies.spotted} MG TRACKING` : '';
    $('#spotTxt').style.color = enemies.spotted ? '#ff4a3d' : '';
    drawMinimap();
    updatePlaneArrows();
  }
  vigT = Math.max(0, vigT - dt * 2.5);
  $('#vig').style.opacity = Math.max(vigT, car.hp < 30 && game.state === 'live' ? 0.25 + Math.sin(tNow * 6) * 0.1 : 0);

  renderer.render(scene, camera);
}

enemies.onPlane = () => alertMsg('AIRCRAFT INBOUND', 'red', 2500, 'plane');

// ---------------------------------------------------------------- UI wiring
document.querySelectorAll('#diff button').forEach(b => b.onclick = () => {
  document.querySelectorAll('#diff button').forEach(x => x.classList.remove('on')); b.classList.add('on'); settings.diff = +b.dataset.v; showBest();
});
document.querySelectorAll('#paint button').forEach(b => b.onclick = () => {
  document.querySelectorAll('#paint button').forEach(x => x.classList.remove('on')); b.classList.add('on'); settings.paint = b.dataset.v; repaint(settings.paint);
});
function showBest() { const b = +localStorage.getItem('dday-best-' + settings.diff); $('#bestTxt').textContent = b ? `Best on ${DIFF[settings.diff].name}: ${fmt(b)}` : ''; }
showBest();
$('#startBtn').onclick = startGame;
$('#retryCp').onclick = () => { audio.init(); restartRun(game.cp); };
$('#restart').onclick = () => { audio.init(); startGame(); };
$('#toMenu').onclick = () => { $('#end').classList.add('hide'); $('#menu').classList.remove('hide'); $('#hud').classList.remove('on'); game.state = 'menu'; enemies.reset(); car.reset(START.x, START.z, START.yaw); water.front = -6; water.level = profileAt(-6); };
$('#resume').onclick = togglePause;
$('#pRestart').onclick = () => { $('#pause').classList.add('hide'); startGame(); };
addEventListener('resize', () => { renderer.setSize(innerWidth, innerHeight); camera.aspect = innerWidth / innerHeight; camera.updateProjectionMatrix(); });

$('#loading').classList.add('hide');
window.__dday = { snap: () => { camSnap = true; }, game, car, T, world, enemies, camera, scene, renderer, water };
frame();

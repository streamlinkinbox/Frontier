import { mat4 } from './math.js?v=17';
import { Car } from './car.js?v=17';
import { Smoke, SMOKE_DIMS, SMOKE_H, FAR_DIMS, FAR_H, MAX_CRATES } from './smoke.js?v=17';
import { Sand, SAND_MAX, SAND_GRID } from './sand.js?v=17';
import { TIER } from './tier.js?v=17';
import { Scene } from './scene.js?v=17';
import { Tornado, TORNADO_MAX } from './tornado.js?v=17';
import { Water, puddleAt, groundY } from './water.js?v=17';

const $ = (id) => document.getElementById(id);
const errors = [];
function fatal(msg) {
  console.error(msg);
  errors.push(String(msg));
  $('err').style.display = 'block';
  $('err').textContent = errors.slice(-6).join('\n\n');
  window.__simErrors = errors;
}
window.__simErrors = errors;

async function init() {
  if (!navigator.gpu) { fatal('WebGPU is not available in this browser.\nUse Chrome/Edge 113+ (or enable WebGPU), on a secure context (https or localhost).'); return; }
  const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
  if (!adapter) { fatal('No WebGPU adapter found (GPU blocklisted or drivers too old).'); return; }
  // RTX tier uses ~60 MB storage buffers: request the adapter's real limits instead of the conservative defaults
  const want = {};
  for (const k of ['maxStorageBufferBindingSize', 'maxBufferSize', 'maxComputeWorkgroupsPerDimension']) if (adapter.limits[k]) want[k] = adapter.limits[k];
  const device = await adapter.requestDevice({ label: 'frontier-sim', requiredLimits: want });
  device.lost.then((info) => fatal(`GPU device lost: ${info.reason} ${info.message}`));
  device.addEventListener('uncapturederror', (e) => fatal('WebGPU error: ' + e.error.message));
  let adapterName = '';
  try { const i = adapter.info || (await adapter.requestAdapterInfo?.()); adapterName = i ? `${i.vendor} ${i.architecture || ''} ${i.description || ''}`.trim() : ''; } catch { /* optional */ }

  const checkModule = (module, label) => {
    module.getCompilationInfo().then((info) => {
      for (const m of info.messages) if (m.type === 'error') fatal(`[${label}] WGSL ${m.lineNum}:${m.linePos} ${m.message}`);
    });
    return module;
  };

  const canvas = $('c');
  const ctx = canvas.getContext('webgpu');
  const format = navigator.gpu.getPreferredCanvasFormat();
  ctx.configure({ device, format, alphaMode: 'opaque' });

  // world
  // 3 puddles: clear, muddy, clear
  const puddles = [
    { x: 15, z: 9, r: 4.0, mud: 0, seed: 1.3 },     // front-right of the start
    { x: -17, z: 12, r: 4.8, mud: 1, seed: 4.0 },   // front-left (mud)
    { x: 0, z: 40, r: 4.4, mud: 0, seed: 6.7 },     // past the barriers
  ];
  const crates = [];
  let seed = 7;
  const rnd = () => ((seed = (seed * 16807) % 2147483647) / 2147483647);
  for (let i = 0; i < 40; i++) {
    const a = rnd() * Math.PI * 2, r = 14 + rnd() * 110;
    const s = 0.6 + rnd() * 1.1;
    crates.push({ x: Math.cos(a) * r, z: Math.sin(a) * r, yaw: rnd() * Math.PI, hx: s, hy: s, hz: s, color: [0.55 + rnd() * 0.15, 0.38, 0.2, 0], gas: true });
  }
  // a row of barriers to slide into
  for (let i = 0; i < 6; i++) crates.push({ x: -12 + i * 4.5, z: 22, yaw: 0, hx: 1.6, hy: 0.6, hz: 0.5, color: [0.9, 0.9, 0.9, 0] });

  // keep crates out of the puddles
  for (const c of crates) for (const p of puddles) { const d = Math.hypot(c.x - p.x, c.z - p.z); if (d < p.r * 1.5 + 2) { const k = (p.r * 1.5 + 2) / Math.max(d, 0.1); c.x = p.x + (c.x - p.x) * k; c.z = p.z + (c.z - p.z) * k; } }
  if (new URLSearchParams(location.search).has('boomtest')) { Object.assign(crates[0], { x: 1.0, z: 4.2, yaw: 0.3 }); if (new URLSearchParams(location.search).has('plumetest')) crates[0].z = 9; }
  device.pushErrorScope('validation');
  const car = new Car();
  if (new URLSearchParams(location.search).has('puddletest')) { car.x = 15; car.z = 0; }
  const scene = new Scene(device, format, checkModule, crates);
  const smoke = new Smoke(device, format, checkModule, { jacobi: TIER.jacobi });
  scene.setSmokeLight(smoke.buf.light);
  const smokeFar = new Smoke(device, format, checkModule, { dims: FAR_DIMS, h: FAR_H, jacobi: TIER.farJacobi, far: true });
  const sand = new Sand(device, format, checkModule);
  scene.puddles = puddles;
  const water = new Water(device, format, checkModule, puddles, scene.camUbo);
  const tornado = new Tornado(device, checkModule, sand.renderPipe, scene.camUbo);
  smoke.tornado = tornado; smokeFar.tornado = tornado; sand.tornado = tornado;
  if (new URLSearchParams(location.search).has('tornadotest')) { tornado.x = 3; tornado.z = 14; crates[1] && Object.assign(crates[1], { x: 6, z: 12 }); }
  const err = await device.popErrorScope();
  if (err) { fatal('Pipeline creation failed: ' + err.message); return; }

  let depthTex = null;
  function resize() {
    const dpr = Math.min(window.devicePixelRatio || 1, 1.5);
    const w = Math.max(1, Math.floor(canvas.clientWidth * dpr)), h = Math.max(1, Math.floor(canvas.clientHeight * dpr));
    if (depthTex && canvas.width === w && canvas.height === h) return;
    canvas.width = w; canvas.height = h;
    depthTex?.destroy();
    depthTex = device.createTexture({ size: [w, h], format: 'depth32float', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING });
    smoke.makeRenderBindGroup(scene.camUbo, depthTex.createView());
    smokeFar.makeRenderBindGroup(scene.camUbo, depthTex.createView());
    water.resize(w, h, depthTex);
  }
  sand.makeRenderBindGroup(scene.camUbo);

  // input
  const keys = {};
  const map = { KeyW: 'throttle', ArrowUp: 'throttle', KeyS: 'brake', ArrowDown: 'brake', KeyA: 'left', ArrowLeft: 'left', KeyD: 'right', ArrowRight: 'right', Space: 'handbrake' };
  addEventListener('keydown', (e) => { if (map[e.code]) { keys[map[e.code]] = true; e.preventDefault(); } if (e.code === 'KeyR') car.reset(); if (e.code === 'KeyH') $('help').classList.toggle('hide'); });
  addEventListener('keyup', (e) => { if (map[e.code]) keys[map[e.code]] = false; });
  addEventListener('blur', () => { for (const k in keys) keys[k] = false; });
  // touch/mouse buttons
  document.querySelectorAll('[data-k]').forEach((b) => {
    const k = b.dataset.k;
    const on = (e) => { keys[k] = true; e.preventDefault(); }, off = () => { keys[k] = false; };
    b.addEventListener('pointerdown', on); b.addEventListener('pointerup', off); b.addEventListener('pointerleave', off);
  });
  // orbit camera
  const CAM_MODES = ['chase', 'free', 'action', 'tornado'];
  let camMode = 'chase';
  const setCam = (m) => { camMode = m; camYaw = 0; document.querySelectorAll('[data-cam]').forEach((b) => b.classList.toggle('on', b.dataset.cam === m)); };
  addEventListener('keydown', (e) => { if (e.code === 'KeyC') setCam(CAM_MODES[(CAM_MODES.indexOf(camMode) + 1) % CAM_MODES.length]); });
  let camYaw = 0, camPitch = 0.32, camDist = 11, dragging = false, lastX = 0, lastY = 0, userOrbit = 0;
  canvas.addEventListener('pointerdown', (e) => { dragging = true; lastX = e.clientX; lastY = e.clientY; });
  addEventListener('pointerup', () => { dragging = false; });
  addEventListener('pointermove', (e) => {
    if (!dragging) return;
    camYaw -= (e.clientX - lastX) * 0.006; camPitch = Math.min(1.3, Math.max(0.05, camPitch + (e.clientY - lastY) * 0.004));
    lastX = e.clientX; lastY = e.clientY; userOrbit = 2.5;
  });
  canvas.addEventListener('wheel', (e) => { camDist = Math.min(40, Math.max(5, camDist * (1 + Math.sign(e.deltaY) * 0.1))); e.preventDefault(); }, { passive: false });
  // smoke settings panel
  const DEFAULTS = {
    emission: 0.8, opacity: 1.6, fade: 0.8, tint: 1.0, dust: 0.3, tyreSwirl: 1.0,
    blast: 1.0, fireGain: 1.0, sootOpacity: 2.2, sootLevel: 0.08, sootFade: 0.35, blastSwirl: 0.2,
    tornado: 1.0, tornadoDust: 1.2,
    tyrePush: 1.0, waterVisc: 1.0, waterDrag: 1.0,
    shadow: 1.4, groundShadow: 0.85, ambient: 1.0, brightness: 1.0, phase: 0.45, vorticity: 3.5, buoyancy: 1.8,
  };
  // grouped: each effect has its own look + lifetime; lighting/motion is shared physics
  const RANGES = {
    '#🛞 Tyre smoke': 0,
    emission: [0, 3, 0.05, 'Amount'], opacity: [0.2, 5, 0.05, 'Opacity'], fade: [0.1, 3, 0.05, 'Fade speed'],
    tint: [0.5, 1.2, 0.01, 'Grey level'], dust: [0, 1, 0.01, 'Sand-dust tint'], tyreSwirl: [0, 2, 0.05, 'Wrap around tyre'],
    '#💥 Explosion / fire': 0,
    blast: [0, 2, 0.05, 'Size (0=off)'], fireGain: [0, 3, 0.05, 'Fire brightness'], sootOpacity: [0.2, 6, 0.05, 'Soot opacity'],
    sootLevel: [0.01, 0.6, 0.01, 'Soot colour (dark→grey)'], sootFade: [0.05, 2, 0.05, 'Soot fade speed'], blastSwirl: [0, 1, 0.01, 'Small-scale swirl'],
    '#🌪️ Tornado': 0,
    tornado: [0, 2, 0.05, 'Strength (0=off)'], tornadoDust: [0, 4, 0.05, 'Dust amount'],
    '#💧 Puddles': 0,
    tyrePush: [0, 2, 0.05, 'Tyre spin → water'], waterVisc: [0.1, 5, 0.05, 'Viscosity (thickness)'], waterDrag: [0, 3, 0.05, 'Water drag on car'],
    '#☀️ Lighting & motion (all)': 0,
    shadow: [0, 4, 0.05, 'Self-shadow'], groundShadow: [0, 1, 0.01, 'Shadows cast on ground/car'], ambient: [0, 2, 0.05, 'Ambient / sky'], brightness: [0.3, 2, 0.05, 'Brightness'],
    phase: [0, 0.85, 0.01, 'Sun glow (fwd scatter)'], vorticity: [0, 14, 0.1, 'Curl / vorticity'], buoyancy: [0, 5, 0.05, 'Rise (buoyancy)'],
  };
  let settings = { ...DEFAULTS };
  try { Object.assign(settings, JSON.parse(localStorage.getItem('smokeSettings5') || '{}')); } catch { /* ignore */ }
  const sl = $('sliders');
  const build = () => {
    sl.innerHTML = '';
    for (const [k, spec] of Object.entries(RANGES)) {
      if (k.startsWith('#')) { const hd = document.createElement('div'); hd.className = 'grp'; hd.textContent = k.slice(1); sl.appendChild(hd); continue; }
      const [mn, mx, st, label] = spec;
      const row = document.createElement('label');
      row.innerHTML = `<span>${label}</span><input type="range" min="${mn}" max="${mx}" step="${st}" value="${settings[k]}"><output>${(+settings[k]).toFixed(2)}</output>`;
      const inp = row.querySelector('input'), out = row.querySelector('output');
      inp.addEventListener('input', () => { settings[k] = +inp.value; out.textContent = (+inp.value).toFixed(2); localStorage.setItem('smokeSettings5', JSON.stringify(settings)); });
      inp.addEventListener('keydown', (e) => e.stopPropagation());
      sl.appendChild(row);
    }
  };
  build();
  $('resetSmoke').onclick = () => { settings = { ...DEFAULTS }; localStorage.removeItem('smokeSettings5'); build(); };
  $('toggleSmoke').onclick = () => $('smokePanel').classList.toggle('collapsed');
  const forceSmoke = new URLSearchParams(location.search).has('smoketest');
  const autoInput = new URLSearchParams(location.search).has('demo');

  const lightDir = (() => { const v = [0.45, 0.8, 0.35]; const l = Math.hypot(...v); return v.map((x) => x / l); })();
  let last = performance.now(), acc = 0, frame = 0, fpsT = 0, fpsN = 0, fps = 0, camHeading = 0;
  const camPos = [0, 4, -10], camLook = [0, 1, 0];
  $('tier').textContent = `${TIER.name.toUpperCase()} tier · smoke ${SMOKE_DIMS.join("×")} @ ${(SMOKE_H * 100).toFixed(1)} cm · Jacobi ${smoke.jacobiIters} · far LOD ${FAR_DIMS.join('×')} @ ${FAR_H} m (30 Hz) · sand MLS-MPM ${SAND_MAX / 1024}k (${SAND_GRID.join('×')} grid)`;
  document.querySelectorAll('[data-tier]').forEach((b) => {
    b.classList.toggle('on', b.dataset.tier === TIER.name);
    b.onclick = () => {
      const u = new URL(location.href);
      u.searchParams.delete('lowres'); u.searchParams.delete('tier');
      if (b.dataset.tier === 'low') u.searchParams.set('lowres', ''); else u.searchParams.set('tier', b.dataset.tier);
      location.href = u.toString();
    };
  });
  document.querySelectorAll('[data-cam]').forEach((b) => { b.onclick = () => setCam(b.dataset.cam); });
  setCam('chase');
  const vram = ((smoke.bytes + smokeFar.bytes + sand.bytes + tornado.bytes + water.bytes) / 1048576).toFixed(0);

  // ---- gas-crate explosions: proximity fuse -> fireball + shock + debris ----
  const blasts = [], plumes = [], pending = [], debris = [];
  let shake = 0, lastType = ''; const flash = [0, 0, 0, 0];
  // explosion archetypes — each detonation picks one, then randomises everything around it
  const TYPES = [
    { name: 'fireball', fuel: [1.8, 2.6], radius: [1.3, 1.7], impulse: [5, 8], up: [1, 3], noise: [0.35, 0.6], stretch: [0.9, 1.2], secondaries: [0, 1], debris: [8, 12] },
    { name: 'sharp',    fuel: [0.9, 1.3], radius: [1.0, 1.3], impulse: [12, 16], up: [0, 2], noise: [0.2, 0.4], stretch: [0.8, 1.0], secondaries: [0, 1], debris: [16, 24] },
    { name: 'mushroom', fuel: [1.6, 2.2], radius: [1.1, 1.4], impulse: [6, 9], up: [7, 11], noise: [0.3, 0.5], stretch: [1.3, 1.7], secondaries: [0, 0], debris: [8, 12] },
    { name: 'chain',    fuel: [1.2, 1.6], radius: [0.9, 1.2], impulse: [7, 10], up: [2, 4], noise: [0.4, 0.7], stretch: [0.9, 1.3], secondaries: [2, 4], debris: [10, 16] },
  ];
  const R = ([a, b]) => a + Math.random() * (b - a);
  function pushBlast(x, y, z, T, k, scale = 1) {
    blasts.push({ x, y, z, age: 0, mode: 1, seed: Math.random() * 100,
      radius: R(T.radius) * scale * Math.sqrt(k), fuel: R(T.fuel) * k, impulse: R(T.impulse) * k * scale,
      up: R(T.up) * k, noise: R(T.noise), stretch: R(T.stretch) });
    flash[0] = x; flash[1] = y + 1; flash[2] = z; flash[3] = Math.max(flash[3], 10 * k * scale);
  }
  function detonate(c) {
    const k = settings.blast;
    c.exploded = true; c.respawn = 18; c.fuse = undefined; c.drawColor = null;
    const T = TYPES[Math.floor(Math.random() * TYPES.length)];
    const size = Math.max(c.hx, c.hz);
    pushBlast(c.x, c.hy, c.z, T, k, 0.8 + size * 0.35);
    // delayed secondary pops (gas pockets igniting) at random offsets
    const nSec = Math.round(R(T.secondaries));
    for (let i = 0; i < nSec; i++) {
      const a = Math.random() * Math.PI * 2, d = 1 + Math.random() * 2.2;
      pending.push({ t: 0.12 + Math.random() * 0.5 + i * 0.15, x: c.x + Math.cos(a) * d, y: 0.4 + Math.random() * 1.8, z: c.z + Math.sin(a) * d, T, k, scale: 0.5 + Math.random() * 0.4 });
    }
    // lingering burning wreck -> continuous plume
    plumes.push({ x: c.x, y: 0.35, z: c.z, age: 0, mode: 2, seed: Math.random() * 100, life: 10 + Math.random() * 8,
      radius: 0.5 + size * 0.35, fuel0: 1.4 + Math.random() * 1.4, fuel: 0, impulse: 0, up: 2 + Math.random() * 2.5, noise: 0, stretch: 1, w: size });
    sand.burst(c.x, c.z, Math.floor(R([1800, 3200]) * k), 6 + 5 * k);
    sand.blast = [c.x, 0.5, c.z, R([6, 10]) * k];
    const nDeb = Math.round(R(T.debris));
    for (let i = 0; i < nDeb; i++) {
      const a = Math.random() * Math.PI * 2, sp = (3 + Math.random() * 10) * k * (T.name === 'sharp' ? 1.4 : 1);
      debris.push({ x: c.x, y: c.hy, z: c.z, vx: Math.cos(a) * sp, vy: (3 + Math.random() * 10) * k, vz: Math.sin(a) * sp,
        s: size * (0.08 + Math.random() * 0.25), yaw: Math.random() * 6, rx: 0, rz: 0, wr: [(Math.random() - 0.5) * 18, (Math.random() - 0.5) * 18], life: 8,
        color: Math.random() < 0.35 ? [0.1, 0.08, 0.07, 0] : c.color });
    }
    const dx = car.x - c.x, dz = car.z - c.z, dist = Math.hypot(dx, dz) || 1;
    const f = Math.max(0, 1 - dist / 12) * k * (T.name === 'sharp' ? 1.4 : 1);
    car.vx += (dx / dist) * 16 * f; car.vz += (dz / dist) * 16 * f; car.w += (Math.random() - 0.5) * 5 * f;
    shake = Math.min(1.5, shake + 0.4 + 1.2 * f);
    lastType = T.name;
  }
  // ---- tornado forces on the car (2D pull + spin) and crates (lift, orbit, fling, fall) ----
  function updateTornadoForces(dt) {
    const s = settings.tornado;
    car.ext = null;
    if (s > 0) {
      const w = tornado.wind(car.x, 0.5, car.z);
      if (w.r < 40) {
        // drag the car toward the local wind velocity (pull-in + swirl); stronger close to the funnel
        const k = Math.min(1.2, 2.2 * Math.exp(-w.r / (w.rc * 3))) * s;
        car.ext = [(w.x - car.vx) * k, (w.z - car.vz) * k, (w.r < w.rc * 1.5 ? 2.5 * s : 0) - car.w * k * 0.3];
      }
    }
    for (const c of crates) {
      if (c.exploded) continue;
      c.y = c.y || 0; c.vy = c.vy || 0; c.vx = c.vx || 0; c.vz = c.vz || 0;
      const w = s > 0 ? tornado.wind(c.x, c.y + c.hy, c.z) : null;
      const mass = c.hx * c.hy * c.hz * 8;
      const grab = w ? Math.exp(-w.r / (w.rc * 1.6)) * s / Math.sqrt(mass) : 0;
      c.caught = grab > 0.12;
      if (c.caught || c.y > 0) {
        c.vx += (w ? (w.x - c.vx) * grab * 2.5 : 0) * dt;
        c.vz += (w ? (w.z - c.vz) * grab * 2.5 : 0) * dt;
        c.vy += ((w ? w.y * grab * 1.6 : 0) - 9.81) * dt;
        // near the top of the funnel the vortex weakens: fling outward
        if (w && c.y > tornado.height * 0.45 && Math.random() < dt * 0.8) { const a = Math.atan2(c.z - tornado.z, c.x - tornado.x); c.vx += Math.cos(a) * 12; c.vz += Math.sin(a) * 12; }
        c.x += c.vx * dt; c.z += c.vz * dt; c.y += c.vy * dt;
        c.spin = (c.spin || 0) + (Math.hypot(c.vx, c.vz) * 0.3 - (c.spin || 0)) * dt;
        c.yaw += c.spin * dt; c.rx = (c.rx || 0) + c.spin * 0.7 * dt * (c.y > 0.1 ? 1 : 0); c.rz = (c.rz || 0) + c.spin * 0.4 * dt * (c.y > 0.1 ? 1 : 0);
        if (c.y <= 0) {
          c.y = 0; if (c.vy < -6) shake = Math.min(1.5, shake + 0.15);
          c.vy = 0; c.vx *= Math.max(0, 1 - dt * 4); c.vz *= Math.max(0, 1 - dt * 4);
          c.rx = Math.round((c.rx || 0) / (Math.PI / 2)) * (Math.PI / 2); c.rz = Math.round((c.rz || 0) / (Math.PI / 2)) * (Math.PI / 2); // lands flat on a face
        }
      }
    }
  }
  function updateExplosions(dt) {
    for (const c of crates) {
      if (!c.gas) continue;
      if (c.exploded) { c.respawn -= dt; if (c.respawn <= 0 && Math.hypot(car.x - c.x, car.z - c.z) > 15) c.exploded = false; continue; }
      const gap = Math.hypot(car.x - c.x, car.z - c.z) - Math.max(c.hx, c.hz) - 1.2;
      if (c.fuse === undefined && gap < 3.0 && settings.blast > 0 && !c.caught && (c.y || 0) < 0.2) c.fuse = 0.45;
      if (c.fuse !== undefined) {
        c.fuse -= dt;
        c.drawColor = (Math.floor(c.fuse * 16) % 2) ? [1, 0.25, 0.1, 0] : c.color; // hissing gas warning blink
        if (c.fuse <= 0) detonate(c);
      }
    }
    for (let i = pending.length - 1; i >= 0; i--) {
      const p = pending[i]; p.t -= dt;
      if (p.t <= 0) { pushBlast(p.x, p.y, p.z, p.T, p.k, p.scale); shake = Math.min(1.5, shake + 0.25); pending.splice(i, 1); }
    }
    for (const b of blasts) b.age += 1 / 60;
    while (blasts.length && blasts[0].age > 0.1) blasts.shift();
    for (let i = plumes.length - 1; i >= 0; i--) {
      const p = plumes[i]; p.age += dt;
      const a = p.age / p.life; // ramp up, burn, then die down
      p.fuel = p.fuel0 * Math.min(1, p.age * 2) * Math.max(0, 1 - a * a) * settings.blast;
      if (a >= 1) plumes.splice(i, 1);
    }
    // sources sent to the GPU: fresh blasts first, then nearest plumes
    const pl = plumes.slice().sort((p, q) => ((p.x - car.x) ** 2 + (p.z - car.z) ** 2) - ((q.x - car.x) ** 2 + (q.z - car.z) ** 2));
    smoke.sources = [...blasts, ...pl].slice(0, 8);
    flash[3] *= Math.exp(-dt * 7);
    if (pl.length && flash[3] < 1.2) { // burning-wreck flicker light
      const p = pl[0];
      flash[0] = p.x; flash[1] = 0.9; flash[2] = p.z;
      flash[3] = Math.max(flash[3], (p.fuel / 2) * (0.8 + 0.4 * Math.random()));
    }

    for (let i = debris.length - 1; i >= 0; i--) {
      const b = debris[i];
      b.vy -= 9.81 * dt; b.x += b.vx * dt; b.y += b.vy * dt; b.z += b.vz * dt;
      b.rx += b.wr[0] * dt; b.rz += b.wr[1] * dt;
      if (b.y < b.s) { b.y = b.s; b.vy *= -0.3; b.vx *= 0.6; b.vz *= 0.6; b.wr[0] *= 0.6; b.wr[1] *= 0.6; }
      b.life -= dt;
      if (b.life <= 0) debris.splice(i, 1);
    }
  }

  // debug: read back smoke field stats  (await __smokeStats())
  window.__smokeStats = async () => {
    const n = smoke.n * 16, rb = device.createBuffer({ size: n, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    const e = device.createCommandEncoder(); e.copyBufferToBuffer(smoke.buf.denA, 0, rb, 0, n); device.queue.submit([e.finish()]);
    await rb.mapAsync(GPUMapMode.READ); const a = new Float32Array(rb.getMappedRange());
    const st = { maxD: 0, maxT: 0, maxFuel: 0, sumD: 0, nan: 0 };
    for (let i = 0; i < a.length; i += 4) { if (!Number.isFinite(a[i]) || !Number.isFinite(a[i + 1])) { st.nan++; continue; } st.maxD = Math.max(st.maxD, a[i]); st.maxT = Math.max(st.maxT, a[i + 1]); st.maxFuel = Math.max(st.maxFuel, a[i + 2]); st.sumD += a[i]; }
    rb.destroy(); return { ...st, sources: JSON.stringify((smoke.sources || []).map((s) => [s.mode, +s.fuel.toFixed(2), +s.x.toFixed(1), +s.z.toFixed(1)])), origin: smoke.origin };
  };
  function frameFn(now) {
    const dtReal = Math.min(0.05, (now - last) / 1000); last = now;
    resize();
    fpsT += dtReal; fpsN++;
    if (fpsT > 0.5) { fps = fpsN / fpsT; fpsT = 0; fpsN = 0; }

    const input = autoInput ? { throttle: true, left: Math.sin(now / 1400) > -0.2, right: false, handbrake: (now % 3000) < 900, brake: false } : keys;
    acc += dtReal;
    const PH = 1 / 120;
    tornado.update(dtReal, car, settings.tornado);
    updateTornadoForces(dtReal);
    const solids = crates.filter((c) => !c.exploded && (c.y || 0) < 1.2);
    while (acc >= PH) { car.step(PH, input, solids); acc -= PH; }
    // puddles: which wheels are in water -> grip, drag, no sand/smoke from wet tyres
    const wet = car.wheels.map((wh) => puddleAt(puddles, wh.pos[0], wh.pos[2]));
    // car sits on the dented terrain: wheel heights -> body height, pitch (nose down = +) and roll (+X side up = +)
    for (const wh of car.wheels) wh.gy = (wh.gy || 0) + (groundY(puddles, wh.pos[0], wh.pos[2]) - (wh.gy || 0)) * Math.min(1, dtReal * 20);
    { const W = car.wheels, g = (k) => W[k].gy || 0;
      car.bodyOff = (g(0) + g(1) + g(2) + g(3)) / 4;
      car.tPitch = ((g(2) + g(3)) - (g(0) + g(1))) / 2 / 2.6;
      car.tRoll = ((g(1) + g(3)) - (g(0) + g(2))) / 2 / 2.0; }
    let nWet = 0, mudWet = 0;
    car.wheels.forEach((wh, i) => {
      const pi = wet[i];
      if (pi >= 0) { nWet++; mudWet += puddles[pi].mud; wh.gripMul = puddles[pi].mud ? 0.55 : 0.75; wh.sand = 0; wh.smoke = 0; wh.mudTimer = puddles[pi].mud ? 3 : (wh.mudTimer || 0); }
      else { wh.gripMul = 1; wh.mudTimer = Math.max(0, (wh.mudTimer || 0) - dtReal); }
    });
    car.waterDrag = (nWet * 40 + mudWet * 90) * (settings.waterDrag ?? 1);
    updateExplosions(dtReal);

    // camera: chase + orbit
    const [fx, fz] = car.fwd();
    const vh = car.speed > 2 ? Math.atan2(car.vx, car.vz) : car.heading;
    const target = 0.7 * car.heading + 0.3 * vh;
    let dh = target - camHeading; dh = Math.atan2(Math.sin(dh), Math.cos(dh));
    camHeading += dh * Math.min(1, dtReal * 3);
    userOrbit = Math.max(0, userOrbit - dtReal);
    // camera modes: chase (auto-recentre), free (orbit stays where you leave it), action (frames the latest
    // explosion/burning wreck, else the tornado, with the car in the foreground), tornado (always faces the funnel)
    let look = [car.x + fx * 1.5, 1.0, car.z + fz * 1.5];
    let baseYaw = camHeading;
    let focus = null;
    if (camMode === 'action') {
      const src = blasts.length ? blasts[blasts.length - 1] : plumes.length ? plumes[plumes.length - 1] : settings.tornado > 0 ? tornado : null;
      if (src) focus = [src.x, src.z, src === tornado ? 6 : 3];
    } else if (camMode === 'tornado' && settings.tornado > 0) focus = [tornado.x, tornado.z, 7];
    if (focus) {
      baseYaw = Math.atan2(focus[0] - car.x, focus[1] - car.z);           // camera behind the car, facing the target
      const k = Math.min(0.65, 12 / Math.max(1, Math.hypot(focus[0] - car.x, focus[1] - car.z)) + 0.35);
      look = [car.x + (focus[0] - car.x) * k, focus[2] * k + 1, car.z + (focus[1] - car.z) * k];
    }
    if (camMode === 'chase' && userOrbit <= 0) camYaw *= 1 - Math.min(1, dtReal * 1.5);
    if (camMode === 'free') baseYaw = 0;                                   // world-fixed orbit
    const yaw = baseYaw + camYaw + Math.PI;
    const want = [car.x + Math.sin(yaw) * Math.cos(camPitch) * camDist, 1 + Math.sin(camPitch) * camDist, car.z + Math.cos(yaw) * Math.cos(camPitch) * camDist];
    for (let i = 0; i < 3; i++) camPos[i] += (want[i] - camPos[i]) * Math.min(1, dtReal * 6);
    shake *= Math.exp(-dtReal * 5);
    const shaken = camPos.map((v) => v + (Math.random() - 0.5) * shake);
    for (let i = 0; i < 3; i++) camLook[i] += (look[i] - camLook[i]) * Math.min(1, dtReal * 5);
    const view = mat4.lookAt(shaken, camLook, [0, 1, 0]);
    const proj = mat4.perspective(1.0, canvas.width / canvas.height, 0.1, 600);
    const vp = mat4.mul(proj, view);
    scene.smokeInfo = [smoke.origin[0], smoke.origin[1], smoke.origin[2], SMOKE_H, ...SMOKE_DIMS, settings.groundShadow];
    scene.writeCamera(vp, shaken, lightDir, canvas.width, canvas.height, frame, car, flash);
    scene.buildInstances(car, debris.concat(plumes.map((p) => ({ x: p.x, y: 0.12, z: p.z, s: p.w * 0.55, yaw: p.seed, rx: 0, rz: 0.05, color: [0.06, 0.05, 0.045, 0] }))));

    // simulation inputs
    const simDt = 1 / 60;
    if (forceSmoke) for (const wh of car.wheels) if (!wh.front) wh.smoke = 1;
    const emitters = car.wheels.map((wh) => {
      const tread = -wh.spinVel * car.wheelR;
      return {
        // emit just behind the contact patch (outside the solid tyre), opposite to the patch's sliding direction
        pos: (() => { const sx = wh.vel[0] + tread * wh.fwd[0], sz = wh.vel[2] + tread * wh.fwd[2], l = Math.hypot(sx, sz) || 1;
          return [wh.pos[0] + (sx / l) * 0.55, 0.2, wh.pos[2] + (sz / l) * 0.55]; })(),
        strength: wh.smoke * 1.6, radius: Math.max(0.45, SMOKE_H * 1.2),
        center: [wh.pos[0], car.wheelR, wh.pos[2]], wheelR: car.wheelR, spin: wh.spinVel,
        axis: [Math.cos(car.heading + (wh.front ? car.steer : 0)), 0, -Math.sin(car.heading + (wh.front ? car.steer : 0))],
        vel: [wh.vel[0] * 0.25 + tread * wh.fwd[0] * 0.3, 0.8, wh.vel[2] * 0.25 + tread * wh.fwd[2] * 0.3],
      };
    });
    const near = solids.map((c) => [c, (c.x - car.x) ** 2 + (c.z - car.z) ** 2]).sort((a, b) => a[1] - b[1]).slice(0, MAX_CRATES).map((a) => a[0]);
    smoke.update(simDt, car, emitters, near, settings, lightDir);
    // far LOD: same sources/tornado, no tyre emitters, every other frame with 2x dt; hands over to the fine grid inside its box
    smokeFar.sources = smoke.sources;
    smokeFar.hole = { ox: smoke.origin[0], oz: smoke.origin[2], sx: SMOKE_DIMS[0] * SMOKE_H, sz: SMOKE_DIMS[2] * SMOKE_H, height: SMOKE_DIMS[1] * SMOKE_H, fade: 2.5 };
    if (frame % 2 === 0) smokeFar.update(simDt * 2, car, [], near, settings, lightDir);
    sand.update(simDt, car);
    sand.spawn(car, dtReal);
    water.update(simDt, car, wet, settings);

    const enc = device.createCommandEncoder();
    sand.encode(enc);
    if (settings.tornado > 0) tornado.encode(enc);
    water.encode(enc);
    smoke.encode(enc);
    if (frame % 2 === 0) smokeFar.encode(enc);
    const view0 = ctx.getCurrentTexture().createView();
    const p1 = enc.beginRenderPass({
      colorAttachments: [{ view: view0, loadOp: 'clear', storeOp: 'store', clearValue: { r: 0.62, g: 0.72, b: 0.84, a: 1 } }],
      depthStencilAttachment: { view: depthTex.createView(), depthClearValue: 1, depthLoadOp: 'clear', depthStoreOp: 'store' },
    });
    scene.draw(p1);
    water.drawOpaque(p1);
    sand.draw(p1);
    if (settings.tornado > 0) tornado.draw(p1);
    p1.end();
    water.render(enc, view0);           // screen-space fluid over the opaque scene
    const p2 = enc.beginRenderPass({ colorAttachments: [{ view: view0, loadOp: 'load', storeOp: 'store' }] });
    smokeFar.draw(p2);
    smoke.draw(p2);
    p2.end();
    device.queue.submit([enc.finish()]);

    frame++;
    if (frame % 10 === 0) {
      const alive = sand.aliveEstimate(dtReal * 10);
      $('stats').innerHTML =
        `<b>${fps.toFixed(0)}</b> fps · ${(1000 / Math.max(fps, 1)).toFixed(1)} ms<br>` +
        `speed <b>${(car.speed * 3.6).toFixed(0)}</b> km/h<br>` +
        `sand particles ~<b>${alive}</b> / ${SAND_MAX}<br>` +
        `water MLS-MPM ${(water.count / 1000).toFixed(0)}k particles (live ${(water.simCount / 1000 || 0).toFixed(0)}k)<br>` +
        `sim VRAM ≈ <b>${vram} MB</b> / 1024<br>` +
        `<span class="dim">${adapterName}</span>`;
      $('drift').style.width = (car.drift * 100).toFixed(0) + '%';
      $('driftLbl').textContent = (car.drift > 0.05 ? 'DRIFT 💨' : 'grip') + (car.waterDrag > 0 ? ' · 💦 splash' : '') + (lastType ? ` · last blast: ${lastType}` : '') + (plumes.length ? ` · ${plumes.length} burning` : '') + (settings.tornado > 0 ? ` · 🌪️ ${Math.hypot(tornado.x - car.x, tornado.z - car.z).toFixed(0)} m` : '');
    } else if (frame % 10 === 1) {
      // aliveEstimate decrements in 10-frame batches
    }
    window.__simFrame = frame;
    requestAnimationFrame(frameFn);
  }
  requestAnimationFrame(frameFn);
}

init().catch((e) => fatal(e.stack || e));

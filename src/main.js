import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { Heightfield } from './heightfield.js';
import { SplashFluid } from './splash.js';
import { makeOcean, gerstnerHeight } from './ocean.js';

const view = document.getElementById('view');
const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(devicePixelRatio, 2)); view.appendChild(renderer.domElement);
const scene = new THREE.Scene(); scene.background = new THREE.Color(0x87aebf);
scene.fog = new THREE.Fog(0x87aebf, 400, 6000);
const camera = new THREE.PerspectiveCamera(55, 1, .1, 20000); camera.position.set(20, 15, 30);
const controls = new OrbitControls(camera, renderer.domElement); controls.maxPolarAngle = 1.48; controls.target.set(0, 0, 0);
scene.add(new THREE.HemisphereLight(0xcfe8ff, 0x1c3a46, 1.1));
const sun = new THREE.DirectionalLight(0xfff2dd, 2); sun.position.set(40, 60, 25); scene.add(sun);

// --- systems -------------------------------------------------------------
const hf = new Heightfield(160, 90);
const ocean = makeOcean(scene, hf);
const splash = new SplashFluid(scene, THREE, 7000);
const P = { amp: 1, chop: .55, foam: 1.2 };           // ocean-facing params
const surfaceY = (x, z) => gerstnerHeight(x, z, tNow, P.amp) + hf.heightAt(x, z);

// Impact registry: THE coupling point. Anything falling into the water
// (splash particle, body, rain, click) registers here once, then both
// systems react: 2.5D gets impulse+foam, volume may get crown spray.
const registry = []; let impactSeq = 0, sprayBudget = 0;
const logEl = document.getElementById('log');
function registerImpact(x, z, energy, radius, source = 'splash') {
  const e = { id: ++impactSeq, x, z, energy, radius, source, time: performance.now() };
  registry.push(e); if (registry.length > 300) registry.shift();
  hf.splat(x, z, radius, energy * 1.6, Math.min(1.2, energy * .5) * splash.params.foamAmount);
  if (source !== 'splash' && energy > .8) // bodies/rain throw a volumetric crown
    splash.spawn(x, surfaceY(x, z) + .3, z, 0, 2.2 + energy * 1.6, 0, Math.min(90, 14 * energy | 0), radius * 1.4);
  if (source !== 'splash' || impactSeq % 12 === 0) {
    const d = document.createElement('div');
    d.innerHTML = `<b>#${e.id} ${source}</b> · x ${x.toFixed(1)}, z ${z.toFixed(1)} · E ${energy.toFixed(2)}`;
    logEl.prepend(d); while (logEl.children.length > 14) logEl.lastChild.remove();
  }
  return e;
}
splash.onImpact = (x, z, e, r) => registerImpact(x, z, e, r, 'splash');

// falling bodies (rocks/orbs) — also splash sources when they breach
const bodies = [];
const bodyGeo = new THREE.SphereGeometry(.7, 20, 14);
function dropBody(x = (Math.random() - .5) * 30, z = (Math.random() - .5) * 30, big = 1) {
  const m = new THREE.Mesh(bodyGeo, new THREE.MeshStandardMaterial({ color: 0x36414a, roughness: .6 }));
  m.scale.setScalar(big); m.position.set(x, 13 + Math.random() * 6, z);
  scene.add(m); bodies.push({ m, vy: 0, big, splashed: false });
}

// --- sliders (Frontier inspector) ---------------------------------------
const SLIDERS = [
  ['Ocean · 2.5D', [
    ['amp', 'Wave height', 0, 2.5, .05, () => P.amp, v => { P.amp = v; ocean.uniforms.uAmp.value = v; }],
    ['chop', 'Gerstner chop', 0, 1.5, .05, () => P.chop, v => { P.chop = v; ocean.uniforms.uChop.value = v; }],
    ['wspeed', 'Ripple propagation', 8, 90, 1, () => hf.params.waveSpeed, v => hf.params.waveSpeed = v],
    ['damp', 'Ripple damping', .90, .999, .001, () => hf.params.damping, v => hf.params.damping = v],
    ['tile', 'Ripple tile size (m)', 40, 220, 5, () => hf.worldSize, v => { hf.worldSize = v; ocean.uniforms.uHfSize.value = v; document.getElementById('tileInfo').textContent = v + ' m'; }],
  ]],
  ['Volumetric splash · 3D grid', [
    ['grav', 'Gravity', -20, -2, .2, () => -splash.params.gravity, v => splash.params.gravity = -v],
    ['press', 'Pressure stiffness', 5, 140, 1, () => splash.params.pressure, v => splash.params.pressure = v],
    ['visc', 'Viscosity', 0, 1.5, .05, () => splash.params.viscosity, v => splash.params.viscosity = v],
    ['vort', 'Vorticity / curl', 0, 2, .05, () => splash.params.vorticity, v => splash.params.vorticity = v],
    ['psize', 'Droplet size', .4, 2.5, .05, () => splash.params.splashSize, v => splash.params.splashSize = v],
    ['cell', 'Grid cell size (m)', .25, 1.4, .05, () => splash.params.cellSize, v => splash.params.cellSize = v],
  ]],
  ['Foam & coupling', [
    ['foam', 'Foam amount', 0, 2.5, .05, () => splash.params.foamAmount, v => { splash.params.foamAmount = v; ocean.uniforms.uFoam.value = v; }],
    ['fdecay', 'Foam lifetime', .95, .999, .001, () => hf.params.foamDecay, v => hf.params.foamDecay = v],
    ['fspread', 'Foam spread', 0, .6, .02, () => hf.params.foamSpread, v => hf.params.foamSpread = v],
  ]],
];
const host = document.getElementById('sliders');
for (const [title, items] of SLIDERS) {
  const g = document.createElement('div'); g.className = 'group'; g.innerHTML = `<h3>${title.toUpperCase()}</h3>`;
  for (const [id, label, min, max, step, get, set] of items) {
    const d = document.createElement('div'); d.className = 'ctl';
    d.innerHTML = `<label>${label}<output>${get()}</output></label><input type="range" min="${min}" max="${max}" step="${step}" value="${get()}">`;
    const out = d.querySelector('output');
    d.querySelector('input').oninput = e => { set(+e.target.value); out.textContent = (+e.target.value).toFixed(step < .01 ? 3 : 2); };
    g.appendChild(d);
  } host.appendChild(g);
}
const presets = document.createElement('div'); presets.className = 'group';
presets.innerHTML = `<h3>PRESETS</h3><div class="row"><button data-p="calm">Calm</button><button data-p="storm">Storm</button><button data-p="impact">Impacts</button></div>`;
host.prepend(presets);
presets.onclick = e => { const p = e.target.dataset.p; if (!p) return;
  if (p === 'calm') Object.assign(P, { amp: .35 }); if (p === 'storm') Object.assign(P, { amp: 2.1 });
  ocean.uniforms.uAmp.value = P.amp;
  if (p === 'impact') for (let i = 0; i < 5; i++) setTimeout(() => dropBody(), i * 250); };

// --- interaction ----------------------------------------------------------
let raining = false;
document.getElementById('dropBtn').onclick = () => dropBody(controls.target.x, controls.target.z);
document.getElementById('waveBtn').onclick = () => { hf.splat(controls.target.x, controls.target.z, 7, 5, 1.2);
  splash.spawn(controls.target.x, surfaceY(controls.target.x, controls.target.z) + 1, controls.target.z, 0, 5, 0, 260, 7); };
document.getElementById('clearBtn').onclick = () => { hf.h.fill(0); hf.v.fill(0); hf.foam.fill(0); splash.count = 0; splash.foam = []; };
const rainBtn = document.getElementById('rainBtn');
rainBtn.onclick = () => { raining = !raining; rainBtn.classList.toggle('active', raining); };
const ray = new THREE.Raycaster(), ptr = new THREE.Vector2();
renderer.domElement.addEventListener('pointerdown', e => {
  ptr.set(e.offsetX / view.clientWidth * 2 - 1, -(e.offsetY / view.clientHeight) * 2 + 1); ray.setFromCamera(ptr, camera);
  const hit = ray.intersectObject(ocean.near)[0]; if (hit && e.shiftKey === false && e.button === 0 && e.altKey) dropBody(hit.point.x, hit.point.z);
});
renderer.domElement.title = 'Alt+click water: drop a body there';

// --- loop -----------------------------------------------------------------
let tNow = 0, last = performance.now(), frames = 0, fpsT = 0;
function resize() { const w = view.clientWidth, h = view.clientHeight; renderer.setSize(w, h); camera.aspect = w / h; camera.updateProjectionMatrix(); }
new ResizeObserver(resize).observe(view); resize();
dropBody(4, 2); dropBody(-7, -4, 1.5);
function frame(now) {
  requestAnimationFrame(frame);
  const dt = Math.min(.033, (now - last) / 1000 || .016); last = now; tNow += dt;
  hf.follow(controls.target.x, controls.target.z);           // ripple tile follows focus, texel-snapped
  ocean.follow(camera.position); ocean.uniforms.uTime.value = tNow; ocean.uniforms.uCam.value.copy(camera.position);
  hf.step(dt);
  splash.step(dt, surfaceY);
  for (let i = bodies.length - 1; i >= 0; i--) { const b = bodies[i]; b.vy += splash.params.gravity * dt;
    b.m.position.y += b.vy * dt; const sy = surfaceY(b.m.position.x, b.m.position.z);
    if (!b.splashed && b.m.position.y < sy + .4) { b.splashed = true;
      registerImpact(b.m.position.x, b.m.position.z, Math.abs(b.vy) * .35 * b.big, 1.2 * b.big, 'body'); }
    if (b.m.position.y < sy - 3) { scene.remove(b.m); bodies.splice(i, 1); } }
  if (raining && Math.random() < .7) { const x = controls.target.x + (Math.random() - .5) * 60, z = controls.target.z + (Math.random() - .5) * 60;
    registerImpact(x, z, .25 + Math.random() * .3, .5, 'rain'); }
  controls.update(); renderer.render(scene, camera);
  frames++; fpsT += dt;
  if (fpsT > .5) { document.getElementById('status').textContent =
      `${Math.round(frames / fpsT)} fps · ${splash.count} droplets · ${splash.foam.length} foam`;
    document.getElementById('hudStats').innerHTML =
      `Sparse splash grid: <strong>${splash.activeCells} active cells</strong> @ ${splash.params.cellSize.toFixed(2)} m (world-space, unbounded) · ` +
      `Ripple tile @ ${hf.center.x.toFixed(0)}, ${hf.center.y.toFixed(0)} · Impacts registered: <strong>${impactSeq}</strong> · Bodies: ${bodies.length}`;
    document.getElementById('cellCount').textContent = splash.activeCells + ' cells';
    document.getElementById('bodyCount').textContent = bodies.length;
    frames = 0; fpsT = 0; }
}
requestAnimationFrame(frame);

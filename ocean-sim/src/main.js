import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { PARAMS, PRESETS, CAMS, LAB, G, todPalette } from './config.js?v=5';
import { BEACH_SLOPE, REEF_WIDTH } from './bathy.js?v=5';
import { WaveField } from './spectrum.js?v=5';
import { Ocean } from './ocean.js?v=5';
import { Sky } from './sky.js?v=5';
import { Seabed } from './seabed.js?v=5';
import { SprayParticles } from './particles.js?v=5';
import { FoamSim } from './foam.js?v=5';
import { Props } from './props.js?v=5';
import { UI } from './ui.js?v=5';

// Build stamp — proves which code is actually running (console + subtitle).
const BUILD = 'v5-uniform-arrays';
console.log(`%cFRONTIER ocean-sim build ${BUILD}`, 'color:#35e0ff;font-weight:bold');
try {
  const sub = document.querySelector('.brand .sub');
  if (sub && !sub.textContent.includes('build')) sub.textContent += ` · build ${BUILD}`;
} catch (_) { /* headless */ }

// ---------------------------------------------------------------------------
// Renderer / scene / camera
// ---------------------------------------------------------------------------
const canvas = document.getElementById('scene');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
renderer.toneMapping = THREE.ACESFilmicToneMapping;

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(55, 1, 1.5, 20000);
camera.position.set(...CAMS.orbit.pos);

const controls = new OrbitControls(camera, renderer.domElement);
controls.target.set(...CAMS.orbit.tgt);
controls.enableDamping = true;
controls.dampingFactor = 0.06;
controls.maxPolarAngle = 1.535;
controls.minDistance = 6;
controls.maxDistance = 900;
controls.autoRotateSpeed = 0.6;

// Lights exist only for the standard-material props (buoy / spar).
const dirLight = new THREE.DirectionalLight(0xffffff, 1.5);
scene.add(dirLight);
scene.add(dirLight.target);
const hemi = new THREE.HemisphereLight(0xbfd9ff, 0x2a3547, 0.8);
scene.add(hemi);

// ---------------------------------------------------------------------------
// Simulation + shared uniforms (one dict, shared by reference across materials)
//
// The spectrum travels as flat vec4 uniform arrays (uSpecA/uSpecB) shared by
// reference: three re-uploads them every render, so spectrum regens flow to
// the GPU with no texture upload, no needsUpdate, no driver-sensitive path.
// ---------------------------------------------------------------------------
const wave = new WaveField();

const U = {
  uTime: { value: 0 },
  uSpecA: { value: wave.dataA },
  uSpecB: { value: wave.dataB },
  uCascadeAmp: { value: new THREE.Vector3(1, 1, 1) },
  uLabA0: { value: new THREE.Vector4() },
  uLabA1: { value: new THREE.Vector4() },
  uLabB0: { value: new THREE.Vector4() },
  uLabB1: { value: new THREE.Vector4() },
  uLabCenter: { value: new THREE.Vector2(LAB.x, LAB.z) },
  uLabRadius: { value: LAB.r },
  uSurfOn: { value: 1 },
  uShoalGain: { value: 1 },
  uBreakAmp: { value: 1 },
  uBarrel: { value: 1.6 },
  uPeelSpeed: { value: 7 },
  uPeelWidth: { value: 26 },
  uPeelOffset: { value: 160 },
  uFoldGain: { value: 1 },
  uRelief: { value: PARAMS.relief },
  uShoreX: { value: PARAMS.shoreX },
  uShoreAngle: { value: 0 },
  uBeachSlope: { value: BEACH_SLOPE },
  uReefX: { value: PARAMS.reefX },
  uReefAngle: { value: 0.2 },
  uReefDepth: { value: PARAMS.reefDepth },
  uReefWidth: { value: REEF_WIDTH },
  uBarEnable: { value: 1 },
  uDeepCol: { value: new THREE.Color(0x062a44) },
  uShallowCol: { value: new THREE.Color(0x1b8f8b) },
  uSSSColor: { value: new THREE.Color(0x40e8d8) },
  uSkyAmb: { value: new THREE.Color(0x5f6c80) },
  uWindVec: { value: new THREE.Vector2(0.3, 0.2) },
  uFoamAmt: { value: 1 },
  uWhitecap: { value: 1 },
  uSSS: { value: 1 },
  uMicroAmp: { value: 0.65 },
  uFogDensity: { value: 0.00042 },
  uFogColor: { value: new THREE.Color(0xbcd3e2) },
  uSunDir: { value: new THREE.Vector3(0, 1, 0) },
  uZenith: { value: new THREE.Color(0x1e5fc4) },
  uHorizon: { value: new THREE.Color(0xbfd9e8) },
  uGroundCol: { value: new THREE.Color(0x323c4e) },
  uSunColor: { value: new THREE.Color(0xfff4e0) },
  uCloudCover: { value: 0.45 },
  uPointScale: { value: 800 },
  uSprayAmt: { value: 1 },
  uSwellK: { value: wave.swellK },
  uDrift: { value: new THREE.Vector2(1, 0.3) },
  uSandDry: { value: new THREE.Color(0xd9c49a) },
  uSandWet: { value: new THREE.Color(0x9a8a6e) },
  uSandDeep: { value: new THREE.Color(0x1d3a4a) },
  uCausticCol: { value: new THREE.Color(0x66ffee) },
  uCaustic: { value: 1 },
};
scene.background = U.uFogColor.value;

// Foam simulation first: the ocean material samples its buffer.
const foamSim = new FoamSim(renderer, U);
U.uFoamTex = { value: foamSim.read.texture };

const ocean = new Ocean(U);
ocean.addTo(scene);
ocean.setQuality(PARAMS.quality);
const sky = new Sky(U);
sky.addTo(scene);
const seabed = new Seabed(U);
seabed.addTo(scene);
const spray = new SprayParticles(U);
spray.addTo(scene);
const props = new Props();
props.addTo(scene);

// ---------------------------------------------------------------------------
// Environment + uniforms sync
// ---------------------------------------------------------------------------
const pal = {};
function applyEnv() {
  todPalette(PARAMS.hour, pal);
  U.uZenith.value.copy(pal.zen);
  U.uHorizon.value.copy(pal.hor);
  U.uGroundCol.value.copy(pal.gnd);
  U.uSunColor.value.copy(pal.sun).multiplyScalar(Math.max(pal.sunI, 0.02));
  U.uSunDir.value.copy(pal.sunDir);
  U.uFogColor.value.copy(pal.fog);
  U.uFogDensity.value = 0.00042 * PARAMS.fog;
  U.uDeepCol.value.copy(pal.deep);
  U.uShallowCol.value.copy(pal.shal);
  U.uSSSColor.value.copy(pal.sss);
  U.uSkyAmb.value.copy(pal.hor).multiplyScalar(0.5);
  U.uCloudCover.value = PARAMS.cloud;
  U.uCaustic.value = pal.elev01;
  renderer.toneMappingExposure = PARAMS.exposure;
  dirLight.position.copy(pal.sunDir).multiplyScalar(500);
  dirLight.color.copy(pal.sun);
  dirLight.intensity = 0.5 + pal.sunI * 1.4;
  hemi.color.copy(pal.zen);
  hemi.groundColor.copy(pal.gnd);
  hemi.intensity = 0.45 + pal.sunI * 0.35;
}

function setLab(v0, v1, L) {
  const k = (2 * Math.PI) / Math.max(L.lambda, 1);
  const om = Math.sqrt(G * k);
  const th = (L.dir * Math.PI) / 180;
  v0.value.set(Math.cos(th), Math.sin(th), k, om);
  v1.value.set(L.amp, 0.25, (L.phase * Math.PI) / 180, L.on ? 1 : 0);
}

let simTime = 0;
function syncUniforms() {
  const P = PARAMS;
  U.uTime.value = simTime;
  U.uCascadeAmp.value.set(P.cascSwell, P.cascSea, P.cascChop);
  setLab(U.uLabA0, U.uLabA1, P.labA);
  setLab(U.uLabB0, U.uLabB1, P.labB);
  U.uSurfOn.value = P.surfOn;
  U.uShoalGain.value = P.shoalGain;
  U.uBreakAmp.value = P.breakAmp;
  U.uBarrel.value = P.barrel;
  U.uPeelSpeed.value = P.peelSpeed;
  U.uPeelWidth.value = P.peelWidth;
  U.uRelief.value = P.relief;
  U.uReefAngle.value = (P.reefAngle * Math.PI) / 180;
  U.uReefDepth.value = P.reefDepth;
  U.uReefX.value = P.reefX;
  U.uShoreX.value = P.shoreX;
  U.uShoreAngle.value = (P.shoreAngle * Math.PI) / 180;
  U.uFoamAmt.value = P.foamAmt;
  U.uWhitecap.value = P.whitecap;
  U.uSprayAmt.value = P.spray;
  U.uSSS.value = P.sss;
  U.uMicroAmp.value = P.micro;
  const wd = (P.windDir * Math.PI) / 180;
  const wmag = P.wind * 0.06;
  U.uWindVec.value.set(Math.cos(wd) * wmag, Math.sin(wd) * wmag);
  U.uDrift.value.set(Math.cos(wd) * P.wind * 0.12 + 0.25, Math.sin(wd) * P.wind * 0.12);
}

// ---------------------------------------------------------------------------
// UI wiring
// ---------------------------------------------------------------------------
let ui = null;
let regenTimer = 0;
function regen() {
  wave.update(); // writes wave.dataA/dataB in place; shared uniform arrays pick it up
  U.uSwellK.value = wave.swellK;
  // auto-ranged whitecaps across sea states and relief settings
  U.uFoldGain.value = (1.7 / (0.5 + wave.Hs)) / Math.max(PARAMS.relief, 0.2);
  if (ui) ui.drawSpectrum();
}
function scheduleRegen() {
  clearTimeout(regenTimer);
  regenTimer = setTimeout(regen, 120);
}

let geoTimer = 0;
function scheduleGeo() {
  clearTimeout(geoTimer);
  geoTimer = setTimeout(() => {
    seabed.rebuild();
    spray.rebuild();
  }, 150);
}

function setCam(name) {
  const c = CAMS[name];
  if (!c) return;
  camera.position.set(...c.pos);
  controls.target.set(...c.tgt);
  controls.update();
}

function applyPixelRatio() {
  const cap = PARAMS.quality === 'low' ? 1 : PARAMS.quality === 'medium' ? 1.5 : 2;
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, cap));
  onResize();
}

function togglePause() {
  PARAMS.paused = !PARAMS.paused;
  document.getElementById('btn-pause').textContent = PARAMS.paused ? '▶' : '⏸';
  document.getElementById('btn-pause').classList.toggle('paused', PARAMS.paused);
}

const REGEN_KEYS = ['beaufort', 'wind', 'fetch', 'windDir', 'chop'];
const ENV_KEYS = ['hour', 'cloud', 'fog', 'exposure'];
const GEO_KEYS = ['reefAngle', 'reefDepth', 'reefX', 'shoreX', 'shoreAngle'];
let fpsEMA = 60;

const api = {
  wave,
  after(path) {
    if (REGEN_KEYS.includes(path)) scheduleRegen();
    if (ENV_KEYS.includes(path)) applyEnv();
    if (path === 'particles') spray.setCount(PARAMS.particles);
    if (path === 'quality') { ocean.setQuality(PARAMS.quality); applyPixelRatio(); }
    if (path === 'wireframe') ocean.material.wireframe = !!PARAMS.wireframe;
    if (path === 'autorotate') controls.autoRotate = !!PARAMS.autorotate;
    if (GEO_KEYS.includes(path)) scheduleGeo();
    // relief rescales fold, so recenter the whitecap auto-gain
    if (path === 'relief') regen();
    // enabling a lab train flies the camera to the site so it never feels dead
    if (path === 'labA.on' && PARAMS.labA.on) setCam('lab');
    if (path === 'labB.on' && PARAMS.labB.on) setCam('lab');
  },
  preset(name) {
    const pr = PRESETS[name];
    if (!pr) return;
    Object.assign(PARAMS, pr.p);
    for (const k of ['a', 'b']) {
      const key = k === 'a' ? 'labA' : 'labB';
      const v = pr.lab[k];
      if (typeof v === 'boolean') PARAMS[key].on = v;
      else if (v) Object.assign(PARAMS[key], v);
    }
    ui.refresh();
    regen();
    applyEnv();
    spray.setCount(PARAMS.particles);
    setCam(pr.cam);
  },
  setCam,
  togglePause,
  reseed() {
    wave.rollSeed((Math.random() * 1e9) | 0);
    regen();
  },
  fps: () => fpsEMA,
  buoyH: () => props.buoyH,
  simTime: () => simTime,
};

applyEnv();
regen();
ui = new UI(api);
applyPixelRatio();

// ---------------------------------------------------------------------------
// Input: WASD / arrows / Q·E fly, resize, underwater state
// ---------------------------------------------------------------------------
const keys = new Set();
window.addEventListener('keydown', (e) => keys.add(e.code));
window.addEventListener('keyup', (e) => keys.delete(e.code));

const _fwd = new THREE.Vector3(), _rgt = new THREE.Vector3(), _mv = new THREE.Vector3();
const _up = new THREE.Vector3(0, 1, 0);
function fly(dt) {
  _fwd.subVectors(controls.target, camera.position);
  _fwd.y = 0;
  if (_fwd.lengthSq() < 1e-6) return;
  _fwd.normalize();
  _rgt.crossVectors(_fwd, _up).normalize();
  _mv.set(0, 0, 0);
  if (keys.has('KeyW') || keys.has('ArrowUp')) _mv.add(_fwd);
  if (keys.has('KeyS') || keys.has('ArrowDown')) _mv.sub(_fwd);
  if (keys.has('KeyD') || keys.has('ArrowRight')) _mv.add(_rgt);
  if (keys.has('KeyA') || keys.has('ArrowLeft')) _mv.sub(_rgt);
  if (keys.has('KeyE')) _mv.y += 1;
  if (keys.has('KeyQ')) _mv.y -= 1;
  if (_mv.lengthSq() === 0) return;
  _mv.normalize().multiplyScalar(dt * (keys.has('ShiftLeft') || keys.has('ShiftRight') ? 90 : 28));
  camera.position.add(_mv);
  controls.target.add(_mv);
}

function onResize() {
  const w = window.innerWidth, h = window.innerHeight;
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
  renderer.setSize(w, h, false);
  U.uPointScale.value = renderer.domElement.height / (2 * Math.tan((camera.fov * 0.5 * Math.PI) / 180));
}
window.addEventListener('resize', onResize);
onResize();

const uwDiv = document.getElementById('uw');

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
const clock = new THREE.Clock();
let firstFrame = true;

function animate() {
  requestAnimationFrame(animate);
  const dt = Math.min(clock.getDelta(), 0.05);
  if (dt > 0) fpsEMA += (1 / dt - fpsEMA) * 0.05;
  const dtSim = PARAMS.paused ? 0 : dt * PARAMS.timeScale;
  simTime += dtSim;

  fly(PARAMS.paused ? 0 : dt);
  controls.update();
  syncUniforms();
  foamSim.update(dtSim, U.uDrift.value);
  U.uFoamTex.value = foamSim.read.texture;
  sky.follow(camera);
  props.update(PARAMS.paused ? 0 : dt, simTime, wave);

  const camH = wave.height(camera.position.x, camera.position.z, simTime);
  uwDiv.classList.toggle('on', camera.position.y < camH + 0.3);

  renderer.render(scene, camera);
  if (firstFrame) {
    firstFrame = false;
    document.getElementById('loading').classList.add('done');
  }
}

animate();

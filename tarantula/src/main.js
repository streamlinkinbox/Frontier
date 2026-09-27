import * as THREE from 'three';
import { SurfaceWorld } from './world/SurfaceWorld.js';
import { Cave, SUN_DIR } from './world/Cave.js';
import { Atmosphere } from './world/Atmosphere.js';
import { Tarantula } from './spider/Tarantula.js';
import { createMaterials, MAX_SHELLS } from './spider/build.js';
import { furUniforms } from './spider/materials.js';
import { CameraRig } from './game/CameraRig.js';
import { Brain } from './game/Brain.js';
import { Post } from './post/Post.js';

const $ = (id) => document.getElementById(id);
const params = new URLSearchParams(location.search);
const errBox = $('err');
window.addEventListener('error', (e) => { errBox.textContent = String(e.message || e); });
window.addEventListener('unhandledrejection', (e) => { errBox.textContent = String(e.reason && e.reason.stack || e.reason); });

const QUALITY = {
  low: { shells: 8, pr: 0.75, sun: 2048, torchShadow: 0, bloom: false, hair: 0.55 },
  medium: { shells: 12, pr: 1, sun: 2048, torchShadow: 512, bloom: true, hair: 0.8 },
  high: { shells: 16, pr: 1.5, sun: 4096, torchShadow: 1024, bloom: true, hair: 1 },
  ultra: { shells: 24, pr: 2, sun: 4096, torchShadow: 2048, bloom: true, hair: 1.25 },
};
let qualityName = params.get('q') || 'high';

const setLoad = (p, msg) => { $('loadbar').style.width = `${Math.round(p * 100)}%`; if (msg) $('loadmsg').textContent = msg; };
const tick = () => new Promise((r) => requestAnimationFrame(() => setTimeout(r, 0)));

async function main() {
  // ------------------------------------------------------------------ renderer
  const renderer = new THREE.WebGLRenderer({ antialias: false, powerPreference: 'high-performance', preserveDrawingBuffer: params.has('manual') });
  renderer.setSize(innerWidth, innerHeight);
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  renderer.toneMapping = THREE.NeutralToneMapping;
  renderer.toneMappingExposure = 0.95;
  document.body.prepend(renderer.domElement);

  const scene = new THREE.Scene();
  scene.background = new THREE.Color(1.6, 1.85, 2.2); // sky seen through the shaft (HDR)
  scene.fog = new THREE.FogExp2(0x15120f, 0.0042);
  const camera = new THREE.PerspectiveCamera(42, innerWidth / innerHeight, 0.1, 700);

  // ------------------------------------------------------------------ world
  setLoad(0.02, 'Carving limestone cave (signed distance field + surface nets)…');
  const world = new SurfaceWorld();
  const cave = new Cave(scene, world);
  await cave.build((p, msg) => setLoad(0.02 + p * 0.5, msg));

  // ------------------------------------------------------------------ lights
  const center = new THREE.Vector3(0, 20, 5);
  const sun = new THREE.DirectionalLight(0xfff0d9, 4.2);
  sun.position.copy(center).addScaledVector(SUN_DIR, -220);
  sun.target.position.copy(center);
  sun.castShadow = true;
  Object.assign(sun.shadow.camera, { left: -120, right: 120, top: 120, bottom: -120, near: 60, far: 400 });
  sun.shadow.bias = -0.0002; sun.shadow.normalBias = 0.035; sun.shadow.radius = 2;
  scene.add(sun, sun.target);

  const hemi = new THREE.HemisphereLight(0x9fb4cc, 0x2c2219, 0.22);
  scene.add(hemi);

  const atmosphere = new Atmosphere(scene, world);
  const bounce = new THREE.PointLight(0xffc996, 650, 0, 2);
  bounce.position.copy(atmosphere.floorSpot).add(new THREE.Vector3(0, 9, 0));
  scene.add(bounce);
  const skyFill = new THREE.PointLight(0xa8c4ff, 260, 0, 2);
  skyFill.position.copy(atmosphere.floorSpot).add(new THREE.Vector3(4, 40, -6));
  scene.add(skyFill);

  const torch = new THREE.SpotLight(0xfff1e2, 300, 0, 0.55, 0.85, 2);
  torch.castShadow = true;
  torch.shadow.camera.near = 2; torch.shadow.camera.far = 250;
  torch.shadow.bias = -0.0004; torch.shadow.normalBias = 0.02;
  scene.add(torch, torch.target);

  // ------------------------------------------------------------------ tarantula
  setLoad(0.55, 'Growing setae (shell pile + guard hairs)…');
  await tick();
  const Q = QUALITY[qualityName] || QUALITY.high;
  const mats = createMaterials();
  const spider = new Tarantula(world, mats, { hairDensity: Q.hair });
  scene.add(spider.object);
  const spawn = cave.findSpawn();
  spider.placeAt(spawn.point, spawn.normal, new THREE.Vector3(-0.6, 0, 1).normalize());

  // urticating setae
  spider.actions.emitHairs = (tipBody, side, dt) => {
    const w = tipBody.clone().applyMatrix4(spider.body.matrixWorld);
    const back = spider.fwd.clone().negate().addScaledVector(spider.up, 0.3).normalize();
    atmosphere.emitHairs(w, back, spider.up, Math.ceil(dt * 700));
  };

  // ------------------------------------------------------------------ environment lighting from the cave itself
  setLoad(0.85, 'Capturing image-based lighting…');
  await tick();
  const pmrem = new THREE.PMREMGenerator(renderer);
  spider.object.visible = false; atmosphere.beam.visible = false;
  sun.shadow.mapSize.set(2048, 2048);
  renderer.render(scene, camera); // prime shadow map
  const envRT = pmrem.fromScene(scene, 0.02, 0.5, 600, { size: 256, position: new THREE.Vector3(4, 12, 0) });
  scene.environment = envRT.texture;
  scene.environmentIntensity = 0.75;
  spider.object.visible = true; atmosphere.beam.visible = true;

  // ------------------------------------------------------------------ camera / post
  const rig = new CameraRig(camera, renderer.domElement, world);
  rig.snap(spider.root.position, spider.up, spider.fwd);
  rig.yaw = Math.PI * 0.78;
  const post = new Post(renderer, scene, camera, { samples: 4, bloom: Q.bloom });
  const brain = new Brain(spider);

  let baseShells = 16, curShells = 0;
  function applyQuality(name) {
    qualityName = name;
    const q = QUALITY[name];
    baseShells = q.shells; curShells = 0;
    const pr = Math.min(window.devicePixelRatio || 1, q.pr);
    renderer.setPixelRatio(pr);
    post.bloom.enabled = q.bloom;
    const setMap = (light, size) => {
      if (light.shadow.mapSize.x === size) return;
      light.shadow.mapSize.set(size, size);
      if (light.shadow.map) { light.shadow.map.dispose(); light.shadow.map = null; }
    };
    setMap(sun, q.sun);
    torch.castShadow = q.torchShadow > 0;
    if (q.torchShadow) setMap(torch, q.torchShadow);
    resize();
    document.querySelectorAll('#quality button').forEach((b) => b.classList.toggle('on', b.dataset.v === name));
  }
  function resize() {
    const pr = renderer.getPixelRatio();
    renderer.setSize(innerWidth, innerHeight);
    camera.aspect = innerWidth / innerHeight; camera.updateProjectionMatrix();
    post.setSize(innerWidth, innerHeight, pr);
    const pw = 2 * Math.tan(THREE.MathUtils.degToRad(camera.fov / 2)) / (innerHeight * pr);
    furUniforms.uPixelWorld.value = pw;
    atmosphere.uniforms.uPixelWorld.value = pw;
  }
  addEventListener('resize', resize);
  applyQuality(qualityName);

  // ------------------------------------------------------------------ input & UI
  const keys = new Set();
  let mode = params.get('mode') === 'ai' ? 'ai' : 'player';
  let timeScale = 1, torchOn = true;
  const toast = (msg) => { const t = $('toast'); t.textContent = msg; t.classList.add('show'); clearTimeout(toast._t); toast._t = setTimeout(() => t.classList.remove('show'), 1400); };
  const setMode = (m) => {
    mode = m; if (m === 'player') brain.stop();
    document.querySelectorAll('#mode button').forEach((b) => b.classList.toggle('on', b.dataset.v === m));
    toast(m === 'ai' ? 'Autonomous behaviour' : 'Player control');
  };
  const setTime = (v) => { timeScale = v; document.querySelectorAll('#timescale button').forEach((b) => b.classList.toggle('on', +b.dataset.v === v)); };
  const setTorch = (on) => { torchOn = on; torch.visible = on; $('torch').classList.toggle('on', on); };
  const threat = (on) => { spider.actions.setThreat(on); $('a-threat').classList.toggle('pressed', on); };
  const strike = () => { spider.actions.trigger('strike'); };
  const flick = () => { spider.actions.trigger('flick'); };

  addEventListener('keydown', (e) => {
    if (e.repeat) { keys.add(e.code); return; }
    keys.add(e.code);
    switch (e.code) {
      case 'KeyQ': threat(true); break;
      case 'KeyE': case 'Space': strike(); e.preventDefault(); break;
      case 'KeyR': flick(); break;
      case 'Tab': setMode(mode === 'ai' ? 'player' : 'ai'); e.preventDefault(); break;
      case 'KeyG': setTime(timeScale === 1 ? 0.2 : 1); break;
      case 'KeyT': setTorch(!torchOn); break;
      case 'KeyC': rig.cinematic = !rig.cinematic; $('cine').classList.toggle('on', rig.cinematic); break;
      case 'KeyH': $('hud').classList.toggle('hidden'); break;
      case 'KeyP': setTime(timeScale === 0 ? 1 : 0); break;
    }
  });
  addEventListener('keyup', (e) => { keys.delete(e.code); if (e.code === 'KeyQ') threat(false); });
  addEventListener('blur', () => { keys.clear(); threat(false); });
  document.querySelectorAll('#mode button').forEach((b) => b.onclick = () => setMode(b.dataset.v));
  document.querySelectorAll('#quality button').forEach((b) => b.onclick = () => applyQuality(b.dataset.v));
  document.querySelectorAll('#timescale button').forEach((b) => b.onclick = () => setTime(+b.dataset.v));
  $('torch').onclick = () => setTorch(!torchOn);
  $('cine').onclick = () => { rig.cinematic = !rig.cinematic; $('cine').classList.toggle('on', rig.cinematic); };
  $('close').onclick = () => { const on = !$('close').classList.contains('on'); $('close').classList.toggle('on', on); rig.dist = on ? 7 : 20; };
  const ta = $('a-threat');
  ta.onpointerdown = () => threat(true); ta.onpointerup = ta.onpointerleave = () => threat(false);
  $('a-strike').onclick = strike; $('a-flick').onclick = flick;
  setMode(mode);
  if (params.has('nohud')) $('hud').classList.add('hidden');

  const ctrl = { dir: new THREE.Vector3(), speed: 0, run: false };
  function playerControl() {
    const { fwd, right } = rig.moveBasis(spider.up);
    let x = 0, y = 0;
    if (keys.has('KeyW') || keys.has('ArrowUp')) y += 1;
    if (keys.has('KeyS') || keys.has('ArrowDown')) y -= 1;
    if (keys.has('KeyD') || keys.has('ArrowRight')) x += 1;
    if (keys.has('KeyA') || keys.has('ArrowLeft')) x -= 1;
    const l = Math.hypot(x, y);
    if (l > 0) { ctrl.dir.copy(fwd).multiplyScalar(y / l).addScaledVector(right, x / l); ctrl.speed = 1; }
    else ctrl.speed = 0;
    ctrl.run = keys.has('ShiftLeft') || keys.has('ShiftRight');
    return ctrl;
  }

  // debug / automation hooks
  window.__game = { THREE, scene, camera, renderer, spider, rig, cave, world, brain, atmosphere, setMode, setTime, applyQuality, keys, ctrl, torch, sun };

  // ------------------------------------------------------------------ loop
  setLoad(1, 'Ready');
  $('loading').classList.add('done');
  const clock = new THREE.Clock();
  let simT = 0, fpsAcc = 0, fpsN = 0, fpsT = 0;
  const headTarget = new THREE.Vector3();
  const manual = params.has('manual');
  function frame(forcedDt) {
    const raw = forcedDt !== undefined ? forcedDt : Math.min(clock.getDelta(), 0.1);
    const dt = raw * timeScale;
    simT += dt;
    if (dt > 0) {
      const c = mode === 'ai' ? brain.update(dt) : playerControl();
      // sub-step the creature for stable gait at low frame rates
      const steps = Math.max(1, Math.ceil(dt / (1 / 90)));
      for (let i = 0; i < steps; i++) spider.update(dt / steps, c);
      atmosphere.update(dt, simT);
    }
    headTarget.copy(spider.root.position).addScaledVector(spider.up, 0.6);
    if (!window.__game.freezeCam) rig.update(raw, headTarget, spider.up, spider.fwd, Math.abs(spider.speed) > 0.5);
    // macro adaptation: more fur shells up close; the torch behaves like a camera fill light
    // whose output is metered to the subject (constant illuminance instead of blowing out at 3 cm)
    const camD = camera.position.distanceTo(headTarget);
    const wantShells = Math.min(MAX_SHELLS, 2 * Math.round(baseShells * THREE.MathUtils.clamp(14 / camD, 1, 2.5) / 2));
    if (wantShells !== curShells) { curShells = wantShells; spider.setShellCount(curShells); }
    torch.intensity = 300 * THREE.MathUtils.clamp((camD / 18) ** 2, 0.04, 3);
    torch.position.copy(camera.position).add(new THREE.Vector3(0, 1.5, 0).applyQuaternion(camera.quaternion));
    torch.target.position.copy(headTarget);
    post.render(raw, simT);
    fpsAcc += raw; fpsN++;
    if ((fpsT += raw) > 0.5) {
      $('stats').textContent = `${Math.round(fpsN / fpsAcc)} FPS · ${qualityName.toUpperCase()} · ${spider.speed.toFixed(1)} cm/s · ${surfaceName(spider.up)}`;
      fpsAcc = 0; fpsN = 0; fpsT = 0;
    }
    if (!manual) requestAnimationFrame(() => frame());
  }
  if (manual) {
    // automation: advance simulation without rendering, then render on demand
    window.__game.step = (seconds, fps = 60) => { for (let i = 0; i < Math.round(seconds * fps); i++) {
      const dt = 1 / fps; simT += dt * timeScale; const c = mode === 'ai' ? brain.update(dt) : playerControl();
      spider.update(dt * timeScale, c); atmosphere.update(dt * timeScale, simT);
      headTarget.copy(spider.root.position).addScaledVector(spider.up, 0.6);
      rig.update(dt, headTarget, spider.up, spider.fwd, Math.abs(spider.speed) > 0.5); } };
    window.__game.render = () => { frame(0); return true; };
    window.__game.ready = true;
  } else frame();
}

function surfaceName(up) {
  if (up.y > 0.7) return 'FLOOR';
  if (up.y < -0.6) return 'CEILING';
  return 'WALL';
}

main().catch((e) => { console.error(e); errBox.textContent = String(e && e.stack || e); });

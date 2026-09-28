import * as THREE from 'three';
import { World, terrainHeight } from './env/World.js';
import { Eagle } from './eagle/Eagle.js';
import { defaultPose, WING_FOLD, WING_GLIDE, leg } from './eagle/pose.js';
import { Post } from './post/Post.js';
import { featherUniforms } from './eagle/materials.js';

const $ = (id) => document.getElementById(id);
const params = new URLSearchParams(location.search);
window.addEventListener('error', (e) => { $('err').textContent = String(e.message || e); });
window.addEventListener('unhandledrejection', (e) => { $('err').textContent = String(e.reason && e.reason.stack || e.reason); });
const setLoad = (p, msg) => { $('loadbar').style.width = `${Math.round(p * 100)}%`; if (msg) $('loadmsg').textContent = msg; };
const tick = () => new Promise((r) => requestAnimationFrame(() => setTimeout(r, 0)));

async function main() {
  const renderer = new THREE.WebGLRenderer({ antialias: false, powerPreference: 'high-performance', preserveDrawingBuffer: params.has('manual') });
  renderer.setPixelRatio(Math.min(devicePixelRatio || 1, 1.5));
  renderer.setSize(innerWidth, innerHeight);
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  renderer.toneMapping = THREE.NeutralToneMapping;
  renderer.toneMappingExposure = 1.0;
  document.body.prepend(renderer.domElement);
  const scene = new THREE.Scene();
  scene.fog = new THREE.FogExp2(0xa9bccf, 0.00055);
  const camera = new THREE.PerspectiveCamera(35, innerWidth / innerHeight, 0.02, 30000);

  setLoad(0.05, 'Building lakeshore…'); await tick();
  const world = new World(scene, renderer);
  await world.buildEnvironment(renderer);
  setLoad(0.4, 'Growing plumage…'); await tick();
  const eagle = new Eagle({ quality: params.get('q') || 'high' });
  scene.add(eagle.object);
  const post = new Post(renderer, scene, camera, { samples: 4, bloom: false });
  post.finish.uniforms.uGrain.value = 0.018; post.finish.uniforms.uVignette.value = 0.22;
  const resize = () => { renderer.setSize(innerWidth, innerHeight); camera.aspect = innerWidth / innerHeight; camera.updateProjectionMatrix(); post.setSize(innerWidth, innerHeight, renderer.getPixelRatio()); };
  addEventListener('resize', resize); resize();

  const pose = defaultPose();
  const g0 = terrainHeight(0, 0);
  function perch() {
    pose.pos.set(0, g0, 0); pose.yaw = 0; pose.pitch = 0.62; pose.trunkOff.set(0, 0.31, 0.0);
    pose.wingL = { ...WING_FOLD }; pose.wingR = { ...WING_FOLD };
    pose.head.worldPos = new THREE.Vector3(0, g0 + 0.53, 0.14); pose.head.worldW = 1;
    pose.head.worldQ = new THREE.Quaternion().setFromEuler(new THREE.Euler(0.08, 0, 0)); pose.head.pitch = 0;
    pose.legL = leg({ world: new THREE.Vector3(0.075, g0 + 0.012, 0.035), fwd: new THREE.Vector3(0.1, 0, 1).normalize(), up: new THREE.Vector3(0, 1, 0) });
    pose.legR = leg({ world: new THREE.Vector3(-0.075, g0 + 0.012, 0.035), fwd: new THREE.Vector3(-0.1, 0, 1).normalize(), up: new THREE.Vector3(0, 1, 0) });
    pose.tail.spread = 0.05; pose.tail.pitch = -0.12;
  }
  function glide() {
    pose.pos.set(0, g0 + 1.2, 0); pose.pitch = 0.04; pose.trunkOff.set(0, 0, 0);
    pose.wingL = { ...WING_GLIDE }; pose.wingR = { ...WING_GLIDE };
    pose.head.worldPos = null; pose.head.worldW = 0; pose.head.worldQ = null;
    pose.head.pos.copy(eagle.neckSolver.fk(0, 0, 0, 0, 0, 0, new THREE.Vector3(), new THREE.Quaternion()));
    pose.legL = leg({ pos: new THREE.Vector3(0.045, -0.07, -0.16), femurPitch: 1.9, toeCurl: 1 });
    pose.legR = leg({ pos: new THREE.Vector3(0.045, -0.07, -0.16), femurPitch: 1.9, toeCurl: 1 });
    pose.tail.spread = 0.15; pose.tail.pitch = 0;
  }
  (params.get('pose') === 'glide' ? glide : perch)();

  const target = new THREE.Vector3();
  const cam = { dist: 2.2, elev: 0.15, azim: 0.6 };
  let freeze = false;
  function placeCam() {
    eagle.trunkWorld(target);
    if (freeze) return;
    camera.position.set(target.x + Math.sin(cam.azim) * Math.cos(cam.elev) * cam.dist, target.y + Math.sin(cam.elev) * cam.dist, target.z + Math.cos(cam.azim) * Math.cos(cam.elev) * cam.dist);
    camera.lookAt(target);
  }
  // drag to orbit
  let drag = null;
  renderer.domElement.addEventListener('pointerdown', (e) => { drag = [e.clientX, e.clientY]; });
  addEventListener('pointerup', () => { drag = null; });
  addEventListener('pointermove', (e) => { if (!drag) return; cam.azim -= (e.clientX - drag[0]) * 0.006; cam.elev = Math.max(-1.2, Math.min(1.4, cam.elev + (e.clientY - drag[1]) * 0.005)); drag = [e.clientX, e.clientY]; });
  addEventListener('wheel', (e) => { cam.dist = Math.max(0.3, Math.min(20, cam.dist * (1 + e.deltaY * 0.001))); });

  let t = 0;
  function frame(dt) {
    t += dt;
    featherUniforms.uTime.value = t;
    eagle.applyPose(pose);
    world.update(dt);
    placeCam();
    world.follow(target);
    post.render(dt, t);
  }
  window.__game = {
    THREE, eagle, pose, camera, world, cam, scene, renderer,
    setFreeze: (v) => { freeze = v; },
    render: () => { frame(1 / 60); return true; },
    perch, glide,
    ready: true,
  };
  $('loading').classList.add('done'); if (params.has('manual')) $('loading').style.display = 'none';
  if (!params.has('manual')) { const clock = new THREE.Clock(); const loop = () => { frame(Math.min(0.05, clock.getDelta())); requestAnimationFrame(loop); }; loop(); }
  else frame(0);
}
main().catch((e) => { console.error(e); $('err').textContent = String(e && e.stack || e); });

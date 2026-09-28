import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { Post } from './Post.js';
import { World } from './world/World.js';
import { Eagle } from './eagle/Eagle.js';
import { Animator } from './eagle/Animator.js';

const params = new URLSearchParams(location.search);
const quality = params.get('q') || 'high';

const renderer = new THREE.WebGLRenderer({ antialias: false, powerPreference: 'high-performance' });
renderer.setPixelRatio(Math.min(devicePixelRatio, quality === 'low' ? 1 : 2));
renderer.setSize(innerWidth, innerHeight);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.toneMapping = THREE.NeutralToneMapping;
renderer.toneMappingExposure = 1.0;
renderer.outputColorSpace = THREE.SRGBColorSpace;
document.body.appendChild(renderer.domElement);

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(38, innerWidth / innerHeight, 0.02, 6000);
camera.position.set(1.3, 0.75, 1.6);

const world = new World(renderer, scene);
const eagle = new Eagle({ quality });
scene.add(eagle.group);
const anim = new Animator(eagle, world);

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true; controls.dampingFactor = 0.08;
controls.minDistance = 0.25; controls.maxDistance = 25;
controls.target.set(0, 0.45, 0);

const post = new Post(renderer, scene, camera, { samples: 4, bloom: true });
post.bloom.strength = 0.08;
function resize() {
  camera.aspect = innerWidth / innerHeight; camera.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight);
  post.setSize(innerWidth, innerHeight, renderer.getPixelRatio());
}
addEventListener('resize', resize); resize();

// ---- camera follow: keep the orbit offset relative to the bird ----
const focus = new THREE.Vector3(), lastFocus = new THREE.Vector3();
function focusPoint(out) {
  eagle.rig.thorax.getWorldPosition(out);
  if (!anim.airborne) out.y = anim.pos.y + 0.42;
  return out;
}
anim.onCut = (air) => {
  anim.update(0);
  focusPoint(focus); lastFocus.copy(focus);
  const back = new THREE.Vector3(Math.sin(anim.heading), 0, Math.cos(anim.heading));
  const side = new THREE.Vector3(back.z, 0, -back.x);
  if (air) camera.position.copy(focus).addScaledVector(back, -3.2).addScaledVector(side, 1.8).add(new THREE.Vector3(0, 0.9, 0));
  else camera.position.copy(focus).addScaledVector(back, 1.6).addScaledVector(side, 1.2).add(new THREE.Vector3(0, 0.25, 0));
  controls.target.copy(focus);
};

// ---- UI ----
const stateEl = document.getElementById('state');
const labels = { idle: 'Idle — perched on a river gravel bar', walk: 'Walk — rocking, alternating gait', flap: 'Flapping flight — 2.8 Hz', glide: 'Gliding / soaring — flat "plank" wings' };
function setMode(m) {
  anim.setMode(m);
  document.querySelectorAll('#ui button[data-mode]').forEach((b) => b.classList.toggle('on', b.dataset.mode === m));
  stateEl.textContent = labels[m];
}
document.querySelectorAll('#ui button[data-mode]').forEach((b) => b.addEventListener('click', () => setMode(b.dataset.mode)));
document.querySelectorAll('#ui button[data-action]').forEach((b) => b.addEventListener('click', () => anim.trigger(b.dataset.action)));
addEventListener('keydown', (e) => {
  const map = { 1: 'idle', 2: 'walk', 5: 'flap', 6: 'glide' };
  if (map[e.key]) setMode(map[e.key]);
  if (e.key === '3') anim.trigger('screech');
  if (e.key === '4') anim.trigger('headturn');
});
const ts = document.getElementById('ts'), tsv = document.getElementById('tsv');
ts.addEventListener('input', () => { anim.timeScale = +ts.value; tsv.textContent = (+ts.value).toFixed(2) + '×'; });
if (params.has('nohud')) { document.getElementById('ui').style.display = 'none'; document.getElementById('hud').style.display = 'none'; }

// ---- loop ----
const clock = new THREE.Clock();
let manual = params.has('manual');
function frame(dt) {
  anim.update(dt);
  focusPoint(focus);
  const d = focus.clone().sub(lastFocus);
  if (!window.__freezeCam) { camera.position.add(d); controls.target.add(d); }
  lastFocus.copy(focus);
  world.follow(focus, anim.airborne ? 4 : 3);
  controls.update();
  post.render(dt, anim.t);
}
anim.update(0); focusPoint(focus); lastFocus.copy(focus); controls.target.copy(focus);
document.getElementById('loading').remove();
renderer.setAnimationLoop(() => { if (!manual) frame(clock.getDelta()); });

// hooks for tooling / headless renders
window.__game = { THREE, scene, camera, controls, renderer, eagle, anim, world, setMode, frame, post,
  step(n = 1, dt = 1 / 60) { for (let i = 0; i < n; i++) frame(dt); },
  cam(dist, elev, azim, fov = 38, target) {
    window.__freezeCam = true;
    const f = target || focusPoint(new THREE.Vector3());
    const h = anim.heading + azim;
    camera.fov = fov; camera.updateProjectionMatrix();
    camera.position.set(f.x + Math.sin(h) * Math.cos(elev) * dist, f.y + Math.sin(elev) * dist, f.z + Math.cos(h) * Math.cos(elev) * dist);
    controls.target.copy(f); camera.lookAt(f); controls.update();
  },
};

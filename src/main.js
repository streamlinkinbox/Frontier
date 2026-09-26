import * as THREE from 'three';
import { WORLD } from './layout.js';
import { buildTerrain } from './terrain.js';
import { buildOcean } from './ocean.js';
import { buildProps } from './props.js';
import { buildCar, buildTanks } from './vehicles.js';
import { buildFortress } from './fortress.js';
import { createGame } from './game.js';
import { hud } from './hud.js';

// ---------------------------------------------------------------------------
// D-DAY "Breakwater" — bootstrap, sky/lighting, input, main loop.
// ---------------------------------------------------------------------------

const canvas = document.getElementById('scene');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.02;
renderer.outputColorSpace = THREE.SRGBColorSpace;

const scene = new THREE.Scene();
scene.background = new THREE.Color('#b7cdd6');
scene.fog = new THREE.Fog('#b7cdd6', 180, 620);

const camera = new THREE.PerspectiveCamera(72, window.innerWidth / window.innerHeight, 0.1, 1200);
camera.position.set(WORLD.gateHalfWidth, 8, 60);

// --- sky dome (clean vertical gradient) ------------------------------------
{
  const skyGeo = new THREE.SphereGeometry(560, 18, 12);
  const colors = new Float32Array(skyGeo.attributes.position.count * 3);
  const top = new THREE.Color('#7fb2c9');
  const bottom = new THREE.Color('#dce7e4');
  const c = new THREE.Color();
  const pos = skyGeo.attributes.position;
  for (let i = 0; i < pos.count; i++) {
    const t = THREE.MathUtils.clamp((pos.getY(i) / 560) * 1.5 + 0.35, 0, 1);
    c.copy(bottom).lerp(top, t);
    colors[i * 3] = c.r; colors[i * 3 + 1] = c.g; colors[i * 3 + 2] = c.b;
  }
  skyGeo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
  const sky = new THREE.Mesh(skyGeo, new THREE.MeshBasicMaterial({ vertexColors: true, side: THREE.BackSide, fog: false }));
  scene.add(sky);
}

// --- lights ----------------------------------------------------------------
const hemi = new THREE.HemisphereLight('#cfe3ec', '#8a7a62', 0.85);
scene.add(hemi);

const sun = new THREE.DirectionalLight('#fff2dc', 1.6);
sun.position.set(-60, 90, -40);
sun.castShadow = true;
sun.shadow.mapSize.set(2048, 2048);
sun.shadow.camera.near = 10;
sun.shadow.camera.far = 320;
sun.shadow.camera.left = -130;
sun.shadow.camera.right = 130;
sun.shadow.camera.top = 130;
sun.shadow.camera.bottom = -130;
sun.shadow.bias = -0.0004;
scene.add(sun);
scene.add(sun.target);
sun.target.position.set(0, 0, 80);

const fill = new THREE.DirectionalLight('#bcd4e2', 0.32);
fill.position.set(80, 40, -80);
scene.add(fill);

// --- world -----------------------------------------------------------------
buildTerrain(scene);
const ocean = buildOcean(scene);
const { group: propsGroup, tankMines, apMines } = buildProps(scene);
const tanks = buildTanks(scene);
const car = buildCar();
scene.add(car);
const { sentries } = buildFortress(scene);

// --- game ------------------------------------------------------------------
const game = createGame({ scene, camera, ocean, sentries, tankMines, apMines, tanks, car });

// --- input -----------------------------------------------------------------
window.addEventListener('keydown', (e) => {
  if (e.repeat) return;
  game.keydown(e.code);
});
window.addEventListener('keyup', (e) => game.keyup(e.code));

document.addEventListener('mousemove', (e) => {
  if (document.pointerLockElement === canvas) game.mouse(e.movementX, e.movementY);
});

canvas.addEventListener('click', () => {
  if (game.state.playing && document.pointerLockElement !== canvas) {
    canvas.requestPointerLock();
  }
});

function startGame() {
  hud.els.menu.classList.add('hidden');
  hud.els.end.classList.add('hidden');
  game.start();
  canvas.requestPointerLock();
}

document.getElementById('start-btn').addEventListener('click', startGame);
document.getElementById('retry-btn').addEventListener('click', () => {
  window.location.reload();
});

window.addEventListener('resize', () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
});

// --- loop ------------------------------------------------------------------
const clock = new THREE.Clock();
renderer.setAnimationLoop(() => {
  const dt = Math.min(clock.getDelta(), 0.05);
  const t = clock.elapsedTime;
  ocean.update(t, dt);
  game.update(dt);
  renderer.render(scene, camera);
});

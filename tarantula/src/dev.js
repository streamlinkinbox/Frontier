import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { SurfaceWorld } from './world/SurfaceWorld.js';
import { Tarantula } from './spider/Tarantula.js';
import { createMaterials } from './spider/build.js';
import { furUniforms } from './spider/materials.js';
import { Post } from './post/Post.js';

const renderer = new THREE.WebGLRenderer({ antialias: false });
renderer.setPixelRatio(1);
renderer.setSize(innerWidth, innerHeight);
renderer.shadowMap.enabled = true; renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.toneMapping = THREE.AgXToneMapping; renderer.toneMappingExposure = 1.0;
document.body.appendChild(renderer.domElement);
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x202020);
const camera = new THREE.PerspectiveCamera(40, innerWidth / innerHeight, 0.05, 500);
const params = new URLSearchParams(location.search);
camera.position.set(+(params.get('cx') ?? 9), +(params.get('cy') ?? 7), +(params.get('cz') ?? 11));
const controls = new OrbitControls(camera, renderer.domElement);
controls.target.set(0, 0.8, -0.5);
const ground = new THREE.Mesh(new THREE.PlaneGeometry(200, 200, 10, 10).rotateX(-Math.PI / 2), new THREE.MeshStandardMaterial({ color: 0x6b5a48, roughness: 0.9 }));
ground.receiveShadow = true; scene.add(ground);
const wall = new THREE.Mesh(new THREE.BoxGeometry(40, 40, 2), new THREE.MeshStandardMaterial({ color: 0x5b4a38, roughness: 0.9 }));
wall.position.set(0, 20, 16); wall.receiveShadow = true; scene.add(wall);
const world = new SurfaceWorld(); world.add(ground); world.add(wall);
scene.add(new THREE.HemisphereLight(0xbfd4ff, 0x3a2a1a, 0.6));
const key = new THREE.DirectionalLight(0xfff1dd, 3.2); key.position.set(10, 18, 6); key.castShadow = true;
key.shadow.mapSize.set(2048, 2048); Object.assign(key.shadow.camera, { left: -12, right: 12, top: 12, bottom: -12, near: 1, far: 60 }); key.shadow.bias = -0.0004; key.shadow.normalBias = 0.02;
scene.add(key);
const rim = new THREE.DirectionalLight(0xbfd8ff, 1.5); rim.position.set(-8, 6, -12); scene.add(rim);
const mats = createMaterials();
const spider = new Tarantula(world, mats, { hairDensity: +(params.get('hair') ?? 1) });
scene.add(spider.object);
spider.placeAt(new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, 1, 0), new THREE.Vector3(0, 0, 1));
spider.setShellCount(+(params.get('shells') ?? 16));
const post = new Post(renderer, scene, camera);
function resize() { renderer.setSize(innerWidth, innerHeight); camera.aspect = innerWidth / innerHeight; camera.updateProjectionMatrix(); post.setSize(innerWidth, innerHeight, 1);
  furUniforms.uPixelWorld.value = 2 * Math.tan(THREE.MathUtils.degToRad(camera.fov / 2)) / (innerHeight * 1); }
resize(); addEventListener('resize', resize);
const ctrl = { dir: null, speed: 0, run: false };
window.__dev = { spider, camera, controls, ctrl, THREE, scene };
let last = performance.now(), t = 0;
const sim = +(params.get('sim') ?? 0);
if (params.get('walk')) { ctrl.dir = new THREE.Vector3(0, 0, 1); ctrl.speed = +params.get('walk'); }
for (let i = 0; i < sim; i++) { spider.update(1 / 60, ctrl); t += 1 / 60; }
if (params.get('act')) { const a = params.get('act'); if (a === 'threat') spider.actions.setThreat(true); else spider.actions.trigger(a);
  const at = +(params.get('at') ?? 1); for (let i = 0; i < at * 60; i++) { spider.update(1 / 60, ctrl); } }
if (params.get('tx')) controls.target.set(+params.get('tx'), +params.get('ty'), +params.get('tz'));
const fixed = params.get('fixed');
function loop() {
  const now = performance.now(); let dt = (now - last) / 1000; last = now;
  if (fixed) dt = +fixed;
  t += dt;
  if (!params.get('freeze')) spider.update(dt, ctrl);
  controls.update();
  post.render(dt, t);
  requestAnimationFrame(loop);
}
loop();

/*
 * Frontier Cliff Forge - browser app.
 *
 * Wires the procedural generator (src/build.js) into a three.js scene with a
 * real sun, sky environment, shadows and orbit controls, plus the parameter
 * UI and OBJ/GLB/PNG export.
 */
import * as THREE from '../vendor/three.module.js';
import { OrbitControls } from '../vendor/OrbitControls.js';
import { generateCliff, framingFor } from './build.js';
import { DEFAULT_PARAMS, PRESETS } from './palette.js';
import { exportOBJ, exportGLB, exportPNG } from './export.js';

/* ------------------------------------------------------------------ */
/* renderer / scene                                                    */
/* ------------------------------------------------------------------ */
const canvas = document.getElementById('view');
const renderer = new THREE.WebGLRenderer({
  canvas, antialias: true, powerPreference: 'high-performance',
});
renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.shadowMap.autoUpdate = false; // cliffs are static - rebuild on demand
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.05;

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(45, 1, 0.5, 3000);

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.07;
controls.rotateSpeed = 0.75;
controls.zoomSpeed = 0.9;
controls.maxPolarAngle = Math.PI * 0.495;
controls.minDistance = 12;
controls.maxDistance = 420;
controls.autoRotateSpeed = 0.45;

/* ------------------------------------------------------------------ */
/* sky + environment                                                   */
/* ------------------------------------------------------------------ */
const skyScene = new THREE.Scene();
let skyMesh = null;
let envRT = null;
const pmrem = new THREE.PMREMGenerator(renderer);
pmrem.compileEquirectangularShader();

function buildSky(preset) {
  const top = new THREE.Color(preset.sky.top);
  const hor = new THREE.Color(preset.sky.horizon);
  const R = 1400;
  const geo = new THREE.SphereGeometry(R, 32, 20);
  const pos = geo.attributes.position;
  const col = new Float32Array(pos.count * 3);
  const c = new THREE.Color();
  for (let i = 0; i < pos.count; i++) {
    const t = Math.pow(THREE.MathUtils.clamp(pos.getY(i) / R, 0, 1), 0.55);
    c.copy(hor).lerp(top, t);
    col[i * 3] = c.r; col[i * 3 + 1] = c.g; col[i * 3 + 2] = c.b;
  }
  geo.setAttribute('color', new THREE.BufferAttribute(col, 3));
  const mat = new THREE.MeshBasicMaterial({
    vertexColors: true, side: THREE.BackSide, fog: false, toneMapped: false, depthWrite: false,
  });
  const mesh = new THREE.Mesh(geo, mat);
  mesh.renderOrder = -1;
  if (skyMesh) {
    scene.remove(skyMesh);
    skyScene.remove(skyMesh);
    skyMesh.geometry.dispose();
    skyMesh.material.dispose();
  }
  skyMesh = mesh;
  scene.add(mesh);
  skyScene.add(mesh);
  if (envRT) envRT.dispose();
  envRT = pmrem.fromScene(skyScene, 0, 1, 2000);
  scene.environment = envRT.texture;
  scene.environmentIntensity = 0.55;
}

/* ------------------------------------------------------------------ */
/* lights                                                              */
/* ------------------------------------------------------------------ */
const sun = new THREE.DirectionalLight(0xffffff, 3);
sun.castShadow = true;
sun.shadow.mapSize.set(2048, 2048);
sun.shadow.bias = -0.0006;
sun.shadow.normalBias = 0.45;
scene.add(sun);
scene.add(sun.target);

const hemi = new THREE.HemisphereLight(0xbdd3ff, 0x6a5c46, 0.55);
scene.add(hemi);
const amb = new THREE.AmbientLight(0xffffff, 0.08);
scene.add(amb);

/* ------------------------------------------------------------------ */
/* meshes                                                              */
/* ------------------------------------------------------------------ */
const rockMat = new THREE.MeshStandardMaterial({
  vertexColors: true, roughness: 0.94, metalness: 0.0, dithering: true,
});
const groundMat = new THREE.MeshStandardMaterial({
  vertexColors: true, roughness: 0.97, metalness: 0.0, dithering: true,
});
const waterMat = new THREE.MeshStandardMaterial({
  color: 0x10181c, roughness: 0.07, metalness: 0.0,
  transparent: true, opacity: 0.9, envMapIntensity: 1.6,
});

let group = new THREE.Group();
scene.add(group);
let current = null;

function clearMeshes() {
  for (const child of [...group.children]) {
    group.remove(child);
    if (child.geometry) child.geometry.dispose();
  }
}

/* ------------------------------------------------------------------ */
/* generation                                                          */
/* ------------------------------------------------------------------ */
function readParams() {
  const p = { ...DEFAULT_PARAMS };
  p.preset = el('preset').value;
  p.seed = el('seed').value.trim() || 'quarry-01';
  for (const key of ['pitRadius', 'pitDepth', 'benchHeight', 'benchWidth', 'roughness',
    'bulge', 'edgeFray', 'bandThickness', 'strataContrast', 'rockDensity', 'rockScale']) {
    p[key] = parseFloat(el(key).value);
  }
  p.road = el('road').checked;
  p.puddle = el('puddle').checked;
  p.shadows = el('shadows').checked;
  p.autoRotate = el('autoRotate').checked;
  return p;
}

function regenerate(keepCamera = true) {
  const params = readParams();
  const preset = PRESETS[params.preset] || PRESETS.limestone;

  showLoading(true);
  // let the browser paint the loading state before the (synchronous) build
  requestAnimationFrame(() => {
    let result;
    try {
      result = generateCliff(params);
    } catch (err) {
      console.error(err);
      showLoading(false);
      toast('Generation failed: ' + err.message);
      return;
    }
    clearMeshes();

    const terrain = new THREE.Mesh(result.terrainGeo, groundMat);
    terrain.receiveShadow = true;
    terrain.castShadow = true;
    terrain.name = 'terrain';
    group.add(terrain);

    const cliff = new THREE.Mesh(result.cliffGeo, rockMat);
    cliff.receiveShadow = true;
    cliff.castShadow = true;
    cliff.name = 'cliff';
    group.add(cliff);

    if (result.puddle) {
      const g = new THREE.CircleGeometry(result.puddle.r, 48);
      g.rotateX(-Math.PI / 2);
      const w = new THREE.Mesh(g, waterMat);
      w.position.set(result.puddle.x, result.puddle.y, result.puddle.z);
      w.scale.z = 0.62;
      w.receiveShadow = true;
      w.name = 'sump';
      group.add(w);
    }

    // sun + sky
    const az = (preset.sun.azimuth * Math.PI) / 180;
    const ev = (preset.sun.elevation * Math.PI) / 180;
    const R = result.pit.radius * 2.4 + result.pit.nBench * result.pit.bh * 2;
    sun.position.set(
      Math.cos(ev) * Math.sin(az) * R,
      Math.sin(ev) * R,
      Math.cos(ev) * Math.cos(az) * R
    );
    sun.target.position.set(0, result.pit.yTop - result.pit.nBench * result.pit.bh * 0.45, 0);
    sun.color.set(preset.sun.color);
    sun.intensity = preset.sun.intensity;
    const sc = sun.shadow.camera;
    const S = result.pit.radius * 1.35 + 14;
    sc.left = -S; sc.right = S; sc.top = S; sc.bottom = -S;
    sc.near = Math.max(1, R - 150);
    sc.far = R + 170;
    sc.updateProjectionMatrix();
    hemi.color.set(preset.sky.top);
    hemi.groundColor.set(preset.ground.dirt);

    scene.fog = new THREE.Fog(new THREE.Color(preset.fog),
      result.pit.radius * 1.15, params.size * 2.35);
    buildSky(preset);

    renderer.shadowMap.enabled = params.shadows;
    sun.castShadow = params.shadows;
    rockMat.needsUpdate = true;
    groundMat.needsUpdate = true;
    controls.autoRotate = params.autoRotate;

    if (!keepCamera || !current) frameCamera(result, 'overview', false);
    current = result;

    // stats
    el('stat-benches').textContent = result.stats.benches;
    el('stat-benchh').textContent = result.stats.benchHeight.toFixed(1);
    el('stat-rocks').textContent = result.stats.rocks;
    el('stat-tris').textContent = formatNum(result.stats.totalTris);
    el('stat-ms').textContent = result.stats.ms + ' ms';

    showLoading(false);
    renderer.shadowMap.needsUpdate = true;
  });
}

/* ------------------------------------------------------------------ */
/* camera framing                                                      */
/* ------------------------------------------------------------------ */
let tween = null;
function frameCamera(result, kind = 'overview', animate = true) {
  const { pit } = result;
  const depth = pit.nBench * pit.bh;
  const y = pit.yTop - depth * 0.45;
  const target = new THREE.Vector3(0, y, 0);
  let pos;
  switch (kind) {
    case 'rim':
      pos = new THREE.Vector3(pit.radius * 0.95, pit.yTop + 6, pit.radius * 0.55);
      break;
    case 'face':
      pos = new THREE.Vector3(pit.radius * 0.62, pit.yTop - depth * 0.22, pit.radius * 1.05);
      break;
    case 'top':
      pos = new THREE.Vector3(0.01, pit.yTop + pit.radius * 1.9, 0.02);
      break;
    default:
      pos = new THREE.Vector3(
        pit.radius * 1.32, pit.yTop + depth * 0.62, pit.radius * 1.42);
  }
  if (!animate) {
    camera.position.copy(pos);
    controls.target.copy(target);
    controls.update();
    return;
  }
  tween = {
    t: 0, dur: 0.9,
    fromP: camera.position.clone(), toP: pos,
    fromT: controls.target.clone(), toT: target,
  };
}

/* ------------------------------------------------------------------ */
/* UI                                                                  */
/* ------------------------------------------------------------------ */
function el(id) { return document.getElementById(id); }
function formatNum(n) { return n.toLocaleString('en-US'); }

function showLoading(on) {
  el('loading').classList.toggle('on', on);
}

function syncLabels() {
  for (const id of ['pitRadius', 'pitDepth', 'benchHeight', 'benchWidth', 'roughness',
    'bulge', 'edgeFray', 'bandThickness', 'strataContrast', 'rockDensity', 'rockScale']) {
    const input = el(id);
    const out = el('v-' + id);
    if (input && out) out.textContent = formatValue(id, parseFloat(input.value));
  }
  const benchInfo = el('bench-info');
  if (benchInfo) {
    const p = readParams();
    const n = Math.max(1, Math.round(p.pitDepth / Math.max(2, p.benchHeight)));
    benchInfo.textContent = `${n} benches`;
  }
}

function formatValue(id, v) {
  if (id === 'rockDensity') return v.toFixed(2);
  if (id === 'strataContrast') return v.toFixed(2);
  if (id === 'roughness' || id === 'bulge' || id === 'edgeFray') return v.toFixed(2);
  if (id === 'rockScale') return v.toFixed(2);
  return v.toFixed(1);
}

function applyPreset(id) {
  const preset = PRESETS[id];
  if (!preset) return;
  for (const [k, v] of Object.entries(preset.params)) {
    const input = el(k);
    if (input) input.value = v;
  }
  syncLabels();
}

function randomize() {
  el('seed').value = 'q-' + Math.random().toString(36).slice(2, 8);
  const jitter = (id, amt) => {
    const input = el(id);
    const min = parseFloat(input.min), max = parseFloat(input.max);
    const v = clamp(parseFloat(input.value) + (Math.random() - 0.5) * amt, min, max);
    input.value = v;
  };
  jitter('pitRadius', 10);
  jitter('pitDepth', 8);
  jitter('benchHeight', 2);
  jitter('roughness', 0.3);
  jitter('edgeFray', 0.3);
  syncLabels();
  regenerate();
}

function clamp(v, a, b) { return v < a ? a : v > b ? b : v; }

/* ------------------------------------------------------------------ */
/* export                                                              */
/* ------------------------------------------------------------------ */
function exportGroup(name) {
  const g = new THREE.Group();
  g.name = 'cliff';
  for (const child of group.children) {
    const clone = new THREE.Mesh(child.geometry, child.material);
    clone.name = child.name;
    g.add(clone);
  }
  return g;
}

el('generate').addEventListener('click', () => regenerate());
el('randomize').addEventListener('click', randomize);
el('export-glb').addEventListener('click', async () => {
  const bytes = await exportGLB(exportGroup(), `cliff-${el('seed').value || 'quarry'}.glb`);
  toast(`GLB exported (${(bytes / 1048576).toFixed(1)} MB)`);
});
el('export-obj').addEventListener('click', () => {
  const geos = group.children.filter((c) => c.name !== 'sump').map((c) => c.geometry);
  const r = exportOBJ(geos, `cliff-${el('seed').value || 'quarry'}.obj`);
  toast(`OBJ exported (${formatNum(r.vertices)} verts, ${formatNum(r.faces)} faces)`);
});
el('save-png').addEventListener('click', () => {
  renderer.render(scene, camera);
  exportPNG(renderer, `cliff-${el('seed').value || 'quarry'}.png`);
  toast('PNG saved');
});

el('preset').addEventListener('change', (e) => { applyPreset(e.target.value); regenerate(); });
el('seed').addEventListener('change', () => regenerate());
for (const id of ['pitRadius', 'pitDepth', 'benchHeight', 'benchWidth', 'roughness',
  'bulge', 'edgeFray', 'bandThickness', 'strataContrast', 'rockDensity', 'rockScale']) {
  el(id).addEventListener('input', syncLabels);
  el(id).addEventListener('change', () => regenerate());
}
for (const id of ['road', 'puddle', 'shadows', 'autoRotate']) {
  el(id).addEventListener('change', () => regenerate());
}
for (const btn of document.querySelectorAll('[data-view]')) {
  btn.addEventListener('click', () => {
    if (!current) return;
    document.querySelectorAll('[data-view]').forEach((b) => b.classList.remove('on'));
    btn.classList.add('on');
    frameCamera(current, btn.dataset.view, true);
  });
}

window.addEventListener('keydown', (e) => {
  if (e.target.tagName === 'INPUT') return;
  if (e.key === 'g') regenerate();
  if (e.key === 'r') randomize();
  if (e.key === ' ') { e.preventDefault(); el('autoRotate').checked = !el('autoRotate').checked; regenerate(); }
  if (e.key === 'h') el('panel').classList.toggle('hidden');
});

let toastTimer = 0;
function toast(msg) {
  const t = el('toast');
  t.textContent = msg;
  t.classList.add('on');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.remove('on'), 2600);
}

/* ------------------------------------------------------------------ */
/* resize + loop                                                       */
/* ------------------------------------------------------------------ */
function resize() {
  const w = window.innerWidth, h = window.innerHeight;
  renderer.setSize(w, h, false);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
}
window.addEventListener('resize', resize);
resize();

const clock = new THREE.Clock();
function loop() {
  requestAnimationFrame(loop);
  const dt = clock.getDelta();
  if (tween) {
    tween.t += dt;
    const k = clamp(tween.t / tween.dur, 0, 1);
    const e = k < 0.5 ? 2 * k * k : 1 - Math.pow(-2 * k + 2, 2) / 2;
    camera.position.lerpVectors(tween.fromP, tween.toP, e);
    controls.target.lerpVectors(tween.fromT, tween.toT, e);
    if (k >= 1) tween = null;
  }
  controls.update();
  renderer.render(scene, camera);
}
loop();

/* ------------------------------------------------------------------ */
/* boot                                                                */
/* ------------------------------------------------------------------ */
el('preset').value = DEFAULT_PARAMS.preset;
applyPreset(DEFAULT_PARAMS.preset);
regenerate(false);

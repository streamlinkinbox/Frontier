import * as THREE from 'three';
import { FlyCamera } from './camera.js';
import { buildNetwork, BRIDGE_TYPES } from './network.js';
import { buildRoadScene } from './meshes.js';
import { PROFILES, PROFILE_IDS } from './profiles.js';
import { generateMap } from './city.js';
import { closestOnPolyline } from './geometry.js';

// ---------- renderer / scene ----------
const canvas = document.getElementById('view');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, logarithmicDepthBuffer: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.setSize(window.innerWidth, window.innerHeight, false);
renderer.setClearColor(0x9cc3e0);

const scene = new THREE.Scene();
scene.fog = new THREE.Fog(0x9cc3e0, 1500, 9000);
const camera = new THREE.PerspectiveCamera(60, window.innerWidth / window.innerHeight, 0.5, 30000);
const fly = new FlyCamera(camera, canvas);

scene.add(new THREE.HemisphereLight(0xcfe7ff, 0x3a4a2a, 1.05));
const sun = new THREE.DirectionalLight(0xffffff, 1.6);
sun.position.set(400, 800, 300);
scene.add(sun);

const roadMat = new THREE.MeshStandardMaterial({ vertexColors: true, side: THREE.DoubleSide, roughness: 0.92, metalness: 0 });
const markMat = new THREE.MeshStandardMaterial({ vertexColors: true, side: THREE.DoubleSide, roughness: 0.7, polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -2 });
const propMat = new THREE.MeshStandardMaterial({ vertexColors: true, side: THREE.DoubleSide, roughness: 0.8 });
const meshes = {
  road: new THREE.Mesh(new THREE.BufferGeometry(), roadMat),
  marks: new THREE.Mesh(new THREE.BufferGeometry(), markMat),
  props: new THREE.Mesh(new THREE.BufferGeometry(), propMat),
};
for (const m of Object.values(meshes)) scene.add(m);

const selLine = new THREE.Line(new THREE.BufferGeometry(), new THREE.LineBasicMaterial({ color: 0xffd23f }));
selLine.visible = false;
selLine.frustumCulled = false;
scene.add(selLine);
const drawLine = new THREE.Line(new THREE.BufferGeometry(), new THREE.LineBasicMaterial({ color: 0x6ea8fe }));
drawLine.frustumCulled = false;
scene.add(drawLine);
const drawMarks = new THREE.Points(new THREE.BufferGeometry(), new THREE.PointsMaterial({ color: 0x6ea8fe, size: 6, sizeAttenuation: false }));
scene.add(drawMarks);
const cursor = new THREE.Mesh(new THREE.RingGeometry(2.4, 3, 32).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.8, depthTest: false }));
cursor.renderOrder = 10;
scene.add(cursor);

// ---------- state ----------
const state = {
  roads: [],
  net: null,
  tool: 'select',
  selected: null,
  draw: [],
  dirty: true,
  history: [],
  mouse: null,
  profile: 'avenue',
  bridge: 'girder',
  spanLen: 46,
  seed: 42,
};
let nextId = 1;
const newId = () => 'road-u' + (nextId++).toString(36) + Math.floor(Math.random() * 1e4).toString(36);

function snapshot() {
  state.history.push(JSON.stringify(state.roads));
  if (state.history.length > 80) state.history.shift();
}
function commit(mutator) {
  snapshot();
  mutator();
  state.dirty = true;
}
function undo() {
  const prev = state.history.pop();
  if (prev === undefined) return status('Nothing to undo.');
  state.roads = JSON.parse(prev);
  if (!state.roads.find((r) => r.id === state.selected)) state.selected = null;
  state.dirty = true;
  status('Undone.');
}

// ---------- rebuild ----------
function rebuild() {
  const t0 = performance.now();
  state.net = buildNetwork(state.roads);
  const scenes = buildRoadScene(state.net, {});
  const swap = (key, geo) => {
    meshes[key].geometry.dispose();
    meshes[key].geometry = geo;
  };
  swap('road', scenes.road);
  swap('marks', scenes.marks);
  swap('props', scenes.props);
  state.dirty = false;
  const ms = Math.round(performance.now() - t0);
  const s = state.net.stats;
  const tris = scenes.stats.roadTris + scenes.stats.markTris + scenes.stats.propTris;
  renderStats(s, tris, ms);
  refreshSelection();
}

function renderStats(s, tris, ms) {
  const rows = [
    ['Roads', s.roads],
    ['Graph edges', s.edges],
    ['Junctions', s.junctions],
    ['Dead ends', s.ends],
    ['Bridge spans', s.bridges],
    ['Triangles', tris.toLocaleString()],
    ['Rebuild', ms + ' ms'],
  ];
  document.getElementById('stats').innerHTML = rows.map(([k, v]) => `<tr><td>${k}</td><td style="text-align:right">${v}</td></tr>`).join('');
}

// ---------- picking ----------
const raycaster = new THREE.Raycaster();
const ground = new THREE.Plane(new THREE.Vector3(0, 1, 0), 0);
function groundHit(clientX, clientY) {
  const r = canvas.getBoundingClientRect();
  const ndc = new THREE.Vector2(((clientX - r.left) / r.width) * 2 - 1, -((clientY - r.top) / r.height) * 2 + 1);
  raycaster.setFromCamera(ndc, camera);
  const p = new THREE.Vector3();
  return raycaster.ray.intersectPlane(ground, p) ? p : null;
}
// Nearest road (by centreline) within tolerance; returns {roadId, s, d} or null.
function pickRoad(x, z, maxD = 10) {
  if (!state.net) return null;
  let best = null;
  for (const r of state.net.roads) {
    const c = closestOnPolyline(r.pts, r.cum, x, z);
    if (c.d <= maxD && (!best || c.d < best.d)) best = { roadId: r.id, s: c.s, d: c.d };
  }
  return best;
}

// ---------- editing ops ----------
function addRoad(ctrl) {
  if (ctrl.length < 2) return;
  commit(() => state.roads.push({ id: newId(), ctrl, profile: state.profile, seed: Math.floor(Math.random() * 2 ** 31), spans: [], props: {} }));
  status(`Added road (${PROFILES[state.profile].label}).`);
}
function findRoad(id) {
  return state.roads.find((r) => r.id === id);
}
function setSelected(id) {
  state.selected = id;
  refreshSelection();
}

// ---------- tools ----------
const TOOLS = { select: 'Select', draw: 'Draw road', bridge: 'Bridge span', delete: 'Delete road' };
function setTool(t) {
  state.tool = t;
  state.draw = [];
  for (const b of document.querySelectorAll('[data-tool]')) b.classList.toggle('active', b.dataset.tool === t);
  status(`Tool: ${TOOLS[t]}`);
  updateDraw();
}

canvas.addEventListener('pointerdown', (e) => {
  if (e.button !== 0) return;
  pointerDown = { x: e.clientX, y: e.clientY };
});
let pointerDown = null;
canvas.addEventListener('pointerup', (e) => {
  if (e.button !== 0 || !pointerDown) return;
  const moved = Math.hypot(e.clientX - pointerDown.x, e.clientY - pointerDown.y);
  pointerDown = null;
  if (moved > 5) return;
  const p = groundHit(e.clientX, e.clientY);
  if (!p) return;
  onToolClick(p.x, p.z, e);
});
canvas.addEventListener('dblclick', (e) => {
  if (state.tool === 'draw') finishDraw();
});

function onToolClick(x, z, e) {
  if (state.tool === 'draw') {
    state.draw.push([x, z]);
    updateDraw();
    return;
  }
  const hit = pickRoad(x, z);
  if (state.tool === 'select') {
    setSelected(hit ? hit.roadId : null);
    if (!hit) status('Nothing selected.');
    return;
  }
  if (state.tool === 'delete') {
    if (!hit) return;
    commit(() => (state.roads = state.roads.filter((r) => r.id !== hit.roadId)));
    if (state.selected === hit.roadId) state.selected = null;
    status('Road deleted.');
    return;
  }
  if (state.tool === 'bridge') {
    if (!hit) return status('Click directly on a road.');
    const road = findRoad(hit.roadId);
    if (!road) return;
    if (e.shiftKey) {
      if (!road.spans?.length) return status('No spans on that road.');
      const nearest = road.spans.reduce((a, b) => (Math.abs(b.s - hit.s) < Math.abs(a.s - hit.s) ? b : a));
      commit(() => (road.spans = road.spans.filter((sp) => sp !== nearest)));
      return status('Span removed.');
    }
    commit(() => {
      road.spans = (road.spans || []).concat([{ s: hit.s, len: state.spanLen, type: state.bridge }]);
    });
    setSelected(road.id);
    status(`Added ${BRIDGE_TYPES[state.bridge].label} (${state.spanLen} m).`);
  }
}

function finishDraw() {
  if (state.draw.length >= 2) addRoad(state.draw.slice());
  state.draw = [];
  updateDraw();
}

function updateDraw() {
  const pts = state.draw;
  drawLine.geometry.dispose();
  drawLine.geometry = new THREE.BufferGeometry().setFromPoints(pts.map((p) => new THREE.Vector3(p[0], 0.6, p[1])));
  drawMarks.geometry.dispose();
  drawMarks.geometry = new THREE.BufferGeometry().setFromPoints(pts.map((p) => new THREE.Vector3(p[0], 0.8, p[1])));
  drawLine.visible = pts.length > 1;
  drawMarks.visible = pts.length > 0;
}

// ---------- inspector ----------
function refreshSelection() {
  const road = state.selected ? findRoad(state.selected) : null;
  const info = document.getElementById('selInfo');
  const actions = document.getElementById('selActions');
  if (!road) {
    info.textContent = 'Nothing selected.';
    actions.hidden = true;
    selLine.visible = false;
    return;
  }
  const nr = state.net?.roads.find((r) => r.id === road.id);
  actions.hidden = false;
  info.innerHTML = `<b>${road.id}</b><br/>${nr ? nr.section.label : PROFILES[road.profile]?.label || road.profile}<br/><span class="muted">${nr ? Math.round(nr.total) + ' m' : ''} · seed ${road.seed}</span>`;
  document.getElementById('selProfile').value = road.profile;
  if (nr) {
    const rows = nr.section.lanes
      .slice()
      .sort((a, b) => a.o0 - b.o0)
      .map((l) => `<tr><td>${l.type}</td><td>${l.side === 'L' ? 'L' : l.side === 'R' ? 'R' : 'C'}</td><td style="text-align:right">${(l.o1 - l.o0).toFixed(2)} m</td></tr>`);
    document.getElementById('lanes').innerHTML = '<tr><th>Lane</th><th>Side</th><th style="text-align:right">Width</th></tr>' + rows.join('');
  }
  const spans = document.getElementById('spans');
  spans.innerHTML = (road.spans || []).length
    ? road.spans.map((sp, i) => `<li>${BRIDGE_TYPES[sp.type]?.label || sp.type} · s=${Math.round(sp.s)} m · ${sp.len} m</li>`).join('')
    : '<li class="muted">No bridge spans.</li>';
  if (nr) {
    const pts = nr.pts.map((p) => new THREE.Vector3(p[0], 0.7, p[1]));
    selLine.geometry.dispose();
    selLine.geometry = new THREE.BufferGeometry().setFromPoints(pts);
    selLine.visible = true;
  }
}

function status(msg) {
  document.getElementById('status').innerHTML = `<span class="pill">${msg}</span>`;
}

// ---------- UI wiring ----------
const profileSel = document.getElementById('profile');
const selProfile = document.getElementById('selProfile');
for (const id of PROFILE_IDS) {
  profileSel.add(new Option(PROFILES[id].label, id));
  selProfile.add(new Option(PROFILES[id].label, id));
}
profileSel.value = state.profile;
profileSel.onchange = () => (state.profile = profileSel.value);
selProfile.onchange = () => {
  const r = findRoad(state.selected);
  if (!r) return;
  commit(() => (r.profile = selProfile.value));
};
const bridgeSel = document.getElementById('bridgeType');
for (const [k, v] of Object.entries(BRIDGE_TYPES)) bridgeSel.add(new Option(v.label, k));
bridgeSel.value = state.bridge;
bridgeSel.onchange = () => (state.bridge = bridgeSel.value);
const spanLen = document.getElementById('spanLen');
spanLen.oninput = () => {
  state.spanLen = +spanLen.value;
  document.getElementById('spanLenVal').textContent = spanLen.value;
};
document.querySelectorAll('[data-tool]').forEach((b) => (b.onclick = () => setTool(b.dataset.tool)));
document.getElementById('reseed').onclick = () => {
  const r = findRoad(state.selected);
  if (r) commit(() => (r.seed = Math.floor(Math.random() * 2 ** 31)));
};
document.getElementById('removeRoad').onclick = () => {
  if (!state.selected) return;
  commit(() => (state.roads = state.roads.filter((r) => r.id !== state.selected)));
  state.selected = null;
  status('Road deleted.');
};
document.getElementById('generate').onclick = () => {
  const seed = +document.getElementById('seed').value || 1;
  commit(() => (state.roads = generateMap(seed)));
  state.selected = null;
  status(`Generated city map (seed ${seed}).`);
};
document.getElementById('clear').onclick = () => {
  commit(() => (state.roads = []));
  state.selected = null;
  status('Map cleared.');
};
document.getElementById('undo').onclick = undo;
document.getElementById('export').onclick = () => {
  const blob = new Blob([JSON.stringify({ format: 'frontier-roads', version: 1, roads: state.roads }, null, 1)], { type: 'application/json' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'frontier-roads.json';
  a.click();
};
document.getElementById('import').onclick = () => document.getElementById('importFile').click();
document.getElementById('importFile').onchange = async (e) => {
  const f = e.target.files[0];
  if (!f) return;
  const data = JSON.parse(await f.text());
  const roads = Array.isArray(data) ? data : data.roads;
  commit(() => (state.roads = roads));
  state.selected = null;
  status(`Imported ${roads.length} roads.`);
};
document.getElementById('showMarkings').onchange = (e) => (meshes.marks.visible = e.target.checked);
document.getElementById('showRoads').onchange = (e) => (meshes.road.visible = e.target.checked);
document.getElementById('showProps').onchange = (e) => (meshes.props.visible = e.target.checked);

window.addEventListener('keydown', (e) => {
  if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT') return;
  if ((e.ctrlKey || e.metaKey) && e.code === 'KeyZ') {
    e.preventDefault();
    undo();
    return;
  }
  if (e.code === 'Digit1') setTool('select');
  if (e.code === 'Digit2') setTool('draw');
  if (e.code === 'Digit3') setTool('bridge');
  if (e.code === 'Digit4') setTool('delete');
  if (e.code === 'Enter' && state.tool === 'draw') finishDraw();
  if (e.code === 'Backspace' && state.tool === 'draw') {
    e.preventDefault();
    state.draw.pop();
    updateDraw();
  }
  if (e.code === 'Escape') {
    state.draw = [];
    updateDraw();
    setSelected(null);
  }
  if (e.code === 'Delete' && state.selected) document.getElementById('removeRoad').click();
});

canvas.addEventListener('pointermove', (e) => (state.mouse = { x: e.clientX, y: e.clientY }));

// ---------- loop ----------
const clock = new THREE.Clock();
let coordsText = '';
function frame() {
  const dt = Math.min(clock.getDelta(), 0.05);
  fly.update(dt);
  if (state.dirty) rebuild();
  if (state.mouse && state.tool !== 'select') {
    const p = groundHit(state.mouse.x, state.mouse.y);
    if (p) {
      cursor.visible = true;
      cursor.position.set(p.x, 0.5, p.z);
      coordsText = `x ${p.x.toFixed(0)}  z ${p.z.toFixed(0)}`;
    } else cursor.visible = false;
  } else cursor.visible = false;
  renderer.render(scene, camera);
  requestAnimationFrame(frame);
}
window.addEventListener('resize', () => {
  renderer.setSize(window.innerWidth, window.innerHeight, false);
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
});

// Start with a small demo network so the editor opens populated.
state.roads = generateMap(42);
setTool('select');
status('Ready. Click "Generate city map" or draw roads. Hold RMB and use WASD to fly.');
frame();
window.__frontier = { state, rebuild, fly };

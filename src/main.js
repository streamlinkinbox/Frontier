// Frontier — interactive procedural tunnel generator
// Draw splines -> live all-quad tunnel mesh with junctions (X / Y / T),
// tiered procedural walls, varying vault height, one connected topology.

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { buildNetworkMesh } from './core/generate.js';
import { connectedComponents, quadEdgesToLines } from './core/topology.js';
import { toOBJColored } from './core/obj.js';
import { sampleDense } from './core/spline.js';

// ---------------------------------------------------------------- presets --

const PRESETS = {
  full: {
    splines: [
      { // trunk, hills
        points: [
          { x: -95, y: 0, z: -28 }, { x: -52, y: 10, z: 8 }, { x: -6, y: -4, z: -14 },
          { x: 38, y: 8, z: 14 }, { x: 88, y: 0, z: -16 },
        ],
      },
      { // Y branch, endpoint on trunk
        points: [
          { x: 8, y: 0, z: -5 }, { x: 16, y: 6, z: 22 }, { x: 26, y: 12, z: 50 }, { x: 24, y: 16, z: 82 },
        ],
      },
      { // X crossing (same level -> junction)
        points: [
          { x: -80, y: 11, z: 45 }, { x: -58, y: 11, z: 18 }, { x: -46, y: 11, z: -6 }, { x: -20, y: 13, z: -50 },
        ],
      },
      { // overpass (different level)
        points: [
          { x: 48, y: 24, z: -60 }, { x: 58, y: 24, z: -12 }, { x: 66, y: 24, z: 40 }, { x: 74, y: 24, z: 75 },
        ],
      },
    ],
  },
  x: {
    splines: [
      { points: [{ x: -70, y: 0, z: -14 }, { x: -28, y: 3, z: -6 }, { x: 14, y: 1, z: 6 }, { x: 64, y: 5, z: 16 }] },
      { points: [{ x: -16, y: 2, z: -58 }, { x: -6, y: 1, z: -16 }, { x: 2, y: 3, z: 18 }, { x: 12, y: 2, z: 58 }] },
    ],
  },
  y: {
    splines: [
      { points: [{ x: -80, y: 0, z: -6 }, { x: -30, y: 2, z: 4 }, { x: 22, y: 1, z: -6 }, { x: 76, y: 3, z: 6 }] },
      { points: [{ x: 2, y: 1, z: -2 }, { x: 10, y: 5, z: 24 }, { x: 16, y: 9, z: 52 }, { x: 14, y: 12, z: 82 }] },
    ],
  },
  overpass: {
    splines: [
      { points: [{ x: -80, y: 0, z: -8 }, { x: -30, y: 0, z: 0 }, { x: 20, y: 0, z: 0 }, { x: 80, y: 0, z: 8 }] },
      { points: [{ x: -10, y: 16, z: -70 }, { x: 0, y: 16, z: -20 }, { x: 0, y: 16, z: 20 }, { x: 10, y: 16, z: 70 }] },
    ],
  },
  hills: {
    splines: [
      {
        points: [
          { x: -90, y: 0, z: -30 }, { x: -50, y: 14, z: 10 }, { x: -8, y: -6, z: -16 },
          { x: 30, y: 12, z: 16 }, { x: 72, y: -2, z: -12 },
        ],
      },
    ],
  },
  custom: { splines: null },
};

const SPLINE_COLORS = [0x4da3ff, 0x3ecf8e, 0xe8a33d, 0xd66fff, 0xff6b6b, 0x62e0d4];

// ------------------------------------------------------------------ state --

const state = {
  splines: [],
  activeSpline: 0,
  selected: null, // { s, p }
  preset: 'full',
  dragging: null,
  dragMoved: false,
};

const params = {
  halfW: 4.2,
  tiers: 3,
  tierH: 2.3,
  ledgeD: 1.15,
  tierHVar: 0.4,
  ledgeDVar: 0.4,
  archRise: 3.4,
  archRiseVar: 0.5,
  stationSpacing: 3,
  roadPts: 5,
  seed: 1,
};

// ------------------------------------------------------------------ three --

const viewport = document.getElementById('viewport');
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x0d1014);
scene.fog = new THREE.Fog(0x0d1014, 260, 620);

const camera = new THREE.PerspectiveCamera(55, 1, 0.1, 1200);
camera.position.set(115, 100, 135);

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
viewport.appendChild(renderer.domElement);

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.06;
controls.maxPolarAngle = Math.PI * 0.495;
controls.minDistance = 12;
controls.maxDistance = 520;

scene.add(new THREE.HemisphereLight(0x93a4c0, 0x232b33, 1.05));
const sun = new THREE.DirectionalLight(0xfff1dc, 1.75);
sun.position.set(80, 140, 60);
scene.add(sun);
const fill = new THREE.DirectionalLight(0x5577bb, 0.55);
fill.position.set(-90, 50, -100);
scene.add(fill);

const grid = new THREE.GridHelper(520, 104, 0x2c3642, 0x1b222b);
grid.position.y = -0.02;
scene.add(grid);

const groundPlane = new THREE.Mesh(
  new THREE.PlaneGeometry(1200, 1200),
  new THREE.MeshBasicMaterial({ visible: false, side: THREE.DoubleSide })
);
groundPlane.rotation.x = -Math.PI / 2;
scene.add(groundPlane);

// mesh layers
const tunnelMat = new THREE.MeshStandardMaterial({
  vertexColors: true,
  roughness: 0.92,
  metalness: 0.04,
  side: THREE.DoubleSide,
  polygonOffset: true,
  polygonOffsetFactor: 1,
  polygonOffsetUnits: 1,
});
const tunnelMesh = new THREE.Mesh(new THREE.BufferGeometry(), tunnelMat);
scene.add(tunnelMesh);

const wireMat = new THREE.LineBasicMaterial({
  color: 0x0a0e13,
  transparent: true,
  opacity: 0.42,
  depthWrite: false,
});
const wireLines = new THREE.LineSegments(new THREE.BufferGeometry(), wireMat);
scene.add(wireLines);

const editGroup = new THREE.Group();
scene.add(editGroup);

const markerGroup = new THREE.Group();
scene.add(markerGroup);

// ---------------------------------------------------------------- editing --

const raycaster = new THREE.Raycaster();
const pointer = new THREE.Vector2();
const dragPlane = new THREE.Plane(new THREE.Vector3(0, 1, 0), 0);
const tmpV = new THREE.Vector3();

function setPointer(e) {
  const rect = renderer.domElement.getBoundingClientRect();
  pointer.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
  pointer.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;
  raycaster.setFromCamera(pointer, camera);
}

function hitHandle(e) {
  setPointer(e);
  const hits = raycaster.intersectObjects(editGroup.children.filter((o) => o.userData.handle));
  return hits.length ? hits[0] : null;
}

function hitGround(e) {
  setPointer(e);
  const hits = raycaster.intersectObject(groundPlane);
  return hits.length ? hits[0].point : null;
}

let downAt = null;
let clickCancelled = false;

renderer.domElement.addEventListener('pointerdown', (e) => {
  if (e.button !== 0) return;
  downAt = { x: e.clientX, y: e.clientY, onCanvas: true };
  clickCancelled = false;
  const hit = hitHandle(e);
  if (hit) {
    const { s, p } = hit.object.userData;
    state.selected = { s, p };
    state.activeSpline = s;
    state.dragging = { s, p, shift: e.shiftKey, lastY: e.clientY };
    state.dragMoved = false;
    controls.enabled = false;
    updateHandles();
    syncPointUI();
  }
});

window.addEventListener('pointermove', (e) => {
  if (downAt && Math.hypot(e.clientX - downAt.x, e.clientY - downAt.y) > 4) {
    clickCancelled = true;
  }
  if (!state.dragging) return;
  const { s, p, shift } = state.dragging;
  const pt = state.splines[s].points[p];
  state.dragMoved = true;
  if (shift) {
    pt.y += (state.dragging.lastY - e.clientY) * 0.08;
    state.dragging.lastY = e.clientY;
  } else {
    setPointer(e);
    dragPlane.constant = -pt.y;
    if (raycaster.ray.intersectPlane(dragPlane, tmpV)) {
      pt.x = tmpV.x;
      pt.z = tmpV.z;
    }
  }
  markCustom();
  scheduleRebuild();
  updateHandles();
  syncPointUI();
});

window.addEventListener('pointerup', (e) => {
  if (state.dragging) {
    state.dragging = null;
    controls.enabled = true;
    downAt = null;
    return;
  }
  const startedOnCanvas = downAt?.onCanvas && e.target === renderer.domElement;
  const wasClick = startedOnCanvas && !clickCancelled;
  downAt = null;
  if (!wasClick) return;

  // click on empty ground -> add a control point to the active spline
  const hit = hitHandle(e);
  if (hit) {
    const { s, p } = hit.object.userData;
    state.selected = { s, p };
    state.activeSpline = s;
    updateHandles();
    syncPointUI();
    return;
  }
  const gp = hitGround(e);
  if (!gp) return;
  if (state.splines.length === 0) {
    state.splines.push({ points: [] });
    state.activeSpline = 0;
  }
  const sp = state.splines[state.activeSpline] ?? state.splines[0];
  const last = sp.points[sp.points.length - 1];
  const y = last ? last.y : 0;
  sp.points.push({ x: gp.x, y, z: gp.z });
  state.selected = { s: state.splines.indexOf(sp), p: sp.points.length - 1 };
  markCustom();
  scheduleRebuild();
  updateHandles();
  syncPointUI();
});

window.addEventListener('keydown', (e) => {
  if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA') return;
  if (e.key === 'Delete' || e.key === 'Backspace') {
    if (state.selected) {
      deleteSelectedPoint();
      e.preventDefault();
    }
  }
});

function deleteSelectedPoint() {
  const { s, p } = state.selected;
  const sp = state.splines[s];
  if (!sp || sp.points.length <= 2) {
    if (sp && sp.points.length <= 2) {
      state.splines.splice(s, 1);
      state.activeSpline = 0;
      state.selected = null;
    }
  } else {
    sp.points.splice(p, 1);
    state.selected = null;
  }
  markCustom();
  scheduleRebuild();
  updateHandles();
  syncPointUI();
}

// ------------------------------------------------------------- viz rebuild --

function updateHandles() {
  while (editGroup.children.length) {
    const c = editGroup.children.pop();
    c.geometry?.dispose();
    c.material?.dispose();
  }
  state.splines.forEach((sp, s) => {
    const isActive = s === state.activeSpline;
    const col = SPLINE_COLORS[s % SPLINE_COLORS.length];

    // spline preview curve
    if (sp.points.length >= 2) {
      const dense = sampleDense(sp.points, 16);
      const pts = dense.map((d) => new THREE.Vector3(d.pos.x, d.pos.y, d.pos.z));
      const g = new THREE.BufferGeometry().setFromPoints(pts);
      const line = new THREE.Line(g, new THREE.LineBasicMaterial({
        color: isActive ? 0x4da3ff : 0x33506e,
        transparent: true,
        opacity: isActive ? 0.95 : 0.5,
      }));
      editGroup.add(line);
    }

    sp.points.forEach((pt, p) => {
      const selected = state.selected && state.selected.s === s && state.selected.p === p;
      // height stem
      const stemG = new THREE.BufferGeometry().setFromPoints([
        new THREE.Vector3(pt.x, 0, pt.z),
        new THREE.Vector3(pt.x, pt.y, pt.z),
      ]);
      editGroup.add(new THREE.Line(stemG, new THREE.LineBasicMaterial({
        color: 0x2c3642, transparent: true, opacity: 0.55,
      })));
      const r = selected ? 1.55 : isActive ? 1.15 : 0.95;
      const sphere = new THREE.Mesh(
        new THREE.SphereGeometry(r, 18, 14),
        new THREE.MeshBasicMaterial({ color: selected ? 0xe8a33d : col })
      );
      sphere.position.set(pt.x, pt.y, pt.z);
      sphere.userData = { handle: true, s, p };
      editGroup.add(sphere);
    });
  });
}

function updateMarkers(debug) {
  while (markerGroup.children.length) {
    const c = markerGroup.children.pop();
    c.geometry?.dispose();
    c.material?.dispose();
  }
  for (const j of debug.junctions) {
    const m = new THREE.Mesh(
      new THREE.OctahedronGeometry(1.7),
      new THREE.MeshBasicMaterial({ color: 0xe8a33d, transparent: true, opacity: 0.9 })
    );
    m.position.set(j.x, j.y + 1.2, j.z);
    markerGroup.add(m);
  }
  for (const o of debug.overpasses) {
    const m = new THREE.Mesh(
      new THREE.OctahedronGeometry(1.3),
      new THREE.MeshBasicMaterial({ color: 0xd66fff, transparent: true, opacity: 0.75 })
    );
    m.position.set(o.x, o.y + 1, o.z);
    markerGroup.add(m);
  }
}

// ---------------------------------------------------------------- rebuild --

let lastMesh = null;
let rebuildTimer = null;

function scheduleRebuild() {
  clearTimeout(rebuildTimer);
  rebuildTimer = setTimeout(rebuild, 120);
}

function rebuild() {
  const valid = state.splines.filter((s) => s.points.length >= 2);
  if (valid.length === 0) {
    tunnelMesh.geometry.dispose();
    tunnelMesh.geometry = new THREE.BufferGeometry();
    wireLines.geometry.dispose();
    wireLines.geometry = new THREE.BufferGeometry();
    setStats(null);
    updateMarkers({ junctions: [], overpasses: [] });
    return;
  }

  const netParams = {
    ...params,
    tierHVar: params.tierHVar,
    ledgeDVar: params.ledgeDVar,
    junctionDist: params.halfW * 2.6,
    detectRadius: params.halfW * 1.35,
    minArmLen: params.halfW * 5.2 + params.stationSpacing * 2,
    heightTolerance: Math.max(7, params.tiers * params.tierH * 0.9),
    junctionMergeDist: params.halfW * 2.2,
  };

  const mesh = buildNetworkMesh(valid, netParams);
  lastMesh = mesh;

  // geometry
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(mesh.positions, 3));
  geo.setAttribute('color', new THREE.BufferAttribute(mesh.colors, 3));
  const index = new Uint32Array(mesh.quads.length * 6);
  let k = 0;
  for (const q of mesh.quads) {
    index[k++] = q[0]; index[k++] = q[1]; index[k++] = q[2];
    index[k++] = q[0]; index[k++] = q[2]; index[k++] = q[3];
  }
  geo.setIndex(new THREE.BufferAttribute(index, 1));
  geo.computeVertexNormals();
  tunnelMesh.geometry.dispose();
  tunnelMesh.geometry = geo;

  // quad-only wireframe
  const wg = new THREE.BufferGeometry();
  wg.setAttribute('position', new THREE.BufferAttribute(quadEdgesToLines(mesh.positions, mesh.quads), 3));
  wireLines.geometry.dispose();
  wireLines.geometry = wg;

  updateMarkers(mesh.debug);
  setStats(mesh);
}

function setStats(mesh) {
  const $ = (id) => document.getElementById(id);
  if (!mesh) {
    ['stVerts', 'stQuads', 'stArms', 'stJunctions', 'stOverpasses', 'stComponents'].forEach((id) => ($(id).textContent = '–'));
    document.getElementById('topoBadge').className = 'badge warn';
    document.getElementById('topoBadge').textContent = 'empty scene';
    return;
  }
  const s = mesh.stats;
  const comps = connectedComponents(s.vertices, mesh.quads);
  $('stVerts').textContent = s.vertices.toLocaleString();
  $('stQuads').textContent = s.quads.toLocaleString();
  $('stArms').textContent = s.arms;
  $('stJunctions').textContent = s.junctions;
  $('stOverpasses').textContent = s.overpasses;
  $('stComponents').textContent = comps;
  const badge = document.getElementById('topoBadge');
  if (comps === 1) {
    badge.className = 'badge ok';
    badge.textContent = '✓ one connected topology';
  } else {
    badge.className = 'badge warn';
    badge.textContent = `${comps} separate shells (overpasses/branches)`;
  }
}

// -------------------------------------------------------------------- UI --

const sliders = [
  ['halfW', 1], ['tiers', 0], ['tierH', 1], ['ledgeD', 2],
  ['archRise', 1], ['archRiseVar', 2], ['stationSpacing', 1], ['roadPts', 0], ['seed', 0],
];

for (const [id, dec] of sliders) {
  const el = document.getElementById(id);
  const val = document.getElementById(`${id}Val`);
  el.addEventListener('input', () => {
    params[id] = parseFloat(el.value);
    val.textContent = dec === 0 ? el.value : parseFloat(el.value).toFixed(dec);
    scheduleRebuild();
  });
}

const wallNoise = document.getElementById('wallNoise');
wallNoise.addEventListener('input', () => {
  const v = parseFloat(wallNoise.value);
  params.tierHVar = v;
  params.ledgeDVar = v;
  document.getElementById('wallNoiseVal').textContent = v.toFixed(2);
  scheduleRebuild();
});

document.getElementById('randomSeed').addEventListener('click', () => {
  const el = document.getElementById('seed');
  el.value = String(1 + Math.floor(Math.random() * 200));
  el.dispatchEvent(new Event('input'));
});

document.getElementById('preset').addEventListener('change', (e) => {
  loadPreset(e.target.value);
});

function markCustom() {
  if (state.preset !== 'custom') {
    state.preset = 'custom';
    document.getElementById('preset').value = 'custom';
  }
}

function loadPreset(name) {
  const preset = PRESETS[name];
  if (!preset || !preset.splines) {
    if (name === 'custom' && state.splines.length === 0) {
      state.splines = [{ points: [{ x: -40, y: 0, z: 0 }, { x: -10, y: 4, z: 10 }, { x: 25, y: 0, z: -6 }] }];
    }
  } else {
    state.splines = preset.splines.map((s) => ({ points: s.points.map((p) => ({ ...p })) }));
  }
  state.preset = name;
  state.selected = null;
  state.activeSpline = 0;
  updateHandles();
  syncPointUI();
  rebuild();
  frameCamera();
}

function frameCamera() {
  const box = new THREE.Box3();
  for (const sp of state.splines) {
    for (const p of sp.points) box.expandByPoint(new THREE.Vector3(p.x, p.y, p.z));
  }
  if (box.isEmpty()) return;
  const center = box.getCenter(new THREE.Vector3());
  const size = box.getSize(new THREE.Vector3()).length();
  controls.target.copy(center);
  camera.position.copy(center).add(new THREE.Vector3(size * 0.55, size * 0.48, size * 0.62));
  controls.update();
}

document.getElementById('addSpline').addEventListener('click', () => {
  const anchor = state.splines[state.activeSpline]?.points.at(-1) ?? { x: 0, y: 0, z: 0 };
  state.splines.push({
    points: [
      { x: anchor.x + 8, y: anchor.y, z: anchor.z + 8 },
      { x: anchor.x + 28, y: anchor.y + 2, z: anchor.z + 26 },
    ],
  });
  state.activeSpline = state.splines.length - 1;
  markCustom();
  scheduleRebuild();
  updateHandles();
});

document.getElementById('delSpline').addEventListener('click', () => {
  if (state.splines.length === 0) return;
  state.splines.splice(state.activeSpline, 1);
  state.activeSpline = Math.max(0, state.activeSpline - 1);
  state.selected = null;
  markCustom();
  scheduleRebuild();
  updateHandles();
  syncPointUI();
});

document.getElementById('clearAll').addEventListener('click', () => {
  state.splines = [];
  state.selected = null;
  markCustom();
  scheduleRebuild();
  updateHandles();
  syncPointUI();
});

document.getElementById('delPoint').addEventListener('click', deleteSelectedPoint);

const ptY = document.getElementById('ptY');
ptY.addEventListener('input', () => {
  if (!state.selected) return;
  const pt = state.splines[state.selected.s]?.points[state.selected.p];
  if (!pt) return;
  pt.y = parseFloat(ptY.value);
  document.getElementById('ptYVal').textContent = pt.y.toFixed(1);
  markCustom();
  scheduleRebuild();
  updateHandles();
});

function syncPointUI() {
  const val = document.getElementById('ptYVal');
  if (!state.selected) {
    val.textContent = '–';
    return;
  }
  const pt = state.splines[state.selected.s]?.points[state.selected.p];
  if (!pt) {
    val.textContent = '–';
    return;
  }
  ptY.value = String(pt.y);
  val.textContent = pt.y.toFixed(1);
}

document.getElementById('showWire').addEventListener('change', (e) => {
  wireLines.visible = e.target.checked;
});
document.getElementById('showSplines').addEventListener('change', (e) => {
  editGroup.visible = e.target.checked;
  markerGroup.visible = e.target.checked;
});
document.getElementById('showMesh').addEventListener('change', (e) => {
  tunnelMesh.visible = e.target.checked;
});

document.getElementById('exportObj').addEventListener('click', () => {
  if (!lastMesh) return;
  const obj = toOBJColored(lastMesh.positions, lastMesh.colors, lastMesh.quads, 'tunnel_network');
  const blob = new Blob([obj], { type: 'text/plain' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'tunnel_network.obj';
  a.click();
  URL.revokeObjectURL(a.href);
});

// ------------------------------------------------------------------ boot --

function onResize() {
  const w = viewport.clientWidth;
  const h = viewport.clientHeight;
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
  renderer.setSize(w, h);
}
window.addEventListener('resize', onResize);

function animate() {
  requestAnimationFrame(animate);
  controls.update();
  renderer.render(scene, camera);
}

onResize();
loadPreset('full');
animate();

import * as THREE from 'three';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';
import { terrainHeight } from './terrain.js';
import {
  HEDGEHOGS, WIRE_LINES, STAKE_ROWS, SANDBAG_WALLS,
  TANK_MINE_FIELDS, AP_MINE_FIELDS,
} from './layout.js';

// ---------------------------------------------------------------------------
// Low-poly battlefield props: sandbags, Czech hedgehogs, dragon's teeth,
// barbed wire, shore stakes, tank mines & exploding AP mines.
// ---------------------------------------------------------------------------

const MAT = {
  sandbag: [
    new THREE.MeshStandardMaterial({ color: '#c7b085', flatShading: true, roughness: 0.95 }),
    new THREE.MeshStandardMaterial({ color: '#b9a075', flatShading: true, roughness: 0.95 }),
    new THREE.MeshStandardMaterial({ color: '#cdb78e', flatShading: true, roughness: 0.95 }),
  ],
  steel: new THREE.MeshStandardMaterial({ color: '#4d5152', flatShading: true, roughness: 0.7, metalness: 0.35 }),
  steelRust: new THREE.MeshStandardMaterial({ color: '#5c4f42', flatShading: true, roughness: 0.85, metalness: 0.2 }),
  wood: new THREE.MeshStandardMaterial({ color: '#6b5233', flatShading: true, roughness: 0.9 }),
  wire: new THREE.MeshStandardMaterial({ color: '#3c3f41', flatShading: true, roughness: 0.6, metalness: 0.45 }),
  concrete: new THREE.MeshStandardMaterial({ color: '#9d9d96', flatShading: true, roughness: 0.92 }),
  mineBody: new THREE.MeshStandardMaterial({ color: '#4a5240', flatShading: true, roughness: 0.8, metalness: 0.2 }),
  mineTop: new THREE.MeshStandardMaterial({ color: '#3d4437', flatShading: true, roughness: 0.75, metalness: 0.25 }),
  trigger: new THREE.MeshStandardMaterial({ color: '#7a2f24', flatShading: true, roughness: 0.7 }),
};

const GEO = {
  bag: new THREE.BoxGeometry(0.92, 0.4, 0.58),
  beam: new THREE.BoxGeometry(0.2, 0.2, 3.4),
  post: new THREE.BoxGeometry(0.09, 1.25, 0.09),
  barb: new THREE.BoxGeometry(0.22, 0.035, 0.035),
  wire: new THREE.CylinderGeometry(0.018, 0.018, 1, 5, 1),
  stake: new THREE.CylinderGeometry(0.09, 0.13, 2.6, 6),
  mineDisc: new THREE.CylinderGeometry(0.36, 0.36, 0.1, 10),
  mineDome: new THREE.SphereGeometry(0.3, 10, 6, 0, Math.PI * 2, 0, Math.PI / 2),
  apBody: new THREE.CylinderGeometry(0.15, 0.17, 0.11, 8),
  apPin: new THREE.CylinderGeometry(0.035, 0.05, 0.09, 6),
  prong: new THREE.BoxGeometry(0.05, 0.05, 0.16),
  tooth: new THREE.ConeGeometry(0.85, 1.5, 4),
};

function mesh(geo, mat, x, y, z, ry = 0, sx = 1, sy = 1, sz = 1) {
  const m = new THREE.Mesh(geo, mat);
  m.position.set(x, y, z);
  m.rotation.y = ry;
  m.scale.set(sx, sy, sz);
  m.castShadow = true;
  m.receiveShadow = true;
  return m;
}

// --- sandbags --------------------------------------------------------------

function sandbag(x, y, z, ry, jitter = 0) {
  const bag = mesh(
    GEO.bag,
    MAT.sandbag[(Math.abs(Math.round(x * 7 + z * 13))) % 3],
    x, y, z,
    ry + (Math.sin(x * 3.1 + z * 5.7) * 0.09)
  );
  bag.scale.y = 1 + jitter;
  return bag;
}

function sandbagWall(group, from, to, rows) {
  const dx = to[0] - from[0], dz = to[1] - from[1];
  const len = Math.hypot(dx, dz);
  const ang = Math.atan2(dx, dz);
  const n = Math.max(2, Math.round(len / 0.98));
  for (let r = 0; r < rows; r++) {
    const y0 = 0.22 + r * 0.38;
    for (let i = 0; i <= n; i++) {
      const t = (i / n) + (r % 2 ? 0.5 / n : 0);
      if (t > 1) continue;
      const x = from[0] + dx * t;
      const z = from[1] + dz * t;
      const y = terrainHeight(x, z) + y0;
      group.add(sandbag(x, y, z, ang + Math.PI / 2, 0.06 * ((i + r) % 3 - 1)));
    }
  }
}

// --- Czech hedgehog (anti-tank barricade) ----------------------------------

function hedgehog(x, z, yaw) {
  const g = new THREE.Group();
  const y = terrainHeight(x, z) + 1.15;
  const b1 = mesh(GEO.beam, MAT.steelRust, 0, 0, 0);
  const b2 = mesh(GEO.beam, MAT.steelRust, 0, 0, 0);
  b2.rotation.x = Math.PI / 2;
  const b3 = mesh(GEO.beam, MAT.steelRust, 0, 0, 0);
  b3.rotation.z = Math.PI / 2;
  g.add(b1, b2, b3);
  g.position.set(x, y, z);
  g.rotation.y = yaw;
  g.rotation.x = 0.1 * Math.sin(x);
  g.rotation.z = 0.12 * Math.cos(z);
  return g;
}

// --- dragon's teeth (concrete anti-tank blocks) ----------------------------

function dragonTeeth(group, x, z, yaw) {
  const y = terrainHeight(x, z);
  const t = mesh(GEO.tooth, MAT.concrete, x, y + 0.72, z, yaw + Math.PI / 4);
  group.add(t);
}

// --- barbed wire -----------------------------------------------------------

function wireSpan(group, x0, x1, z, heightMul = 1) {
  const y = terrainHeight(x0, z);
  const y2 = terrainHeight(x1, z);
  const len = Math.abs(x1 - x0);

  // posts
  const nPosts = Math.max(2, Math.round(len / 2.6));
  for (let i = 0; i <= nPosts; i++) {
    const t = i / nPosts;
    const x = x0 + (x1 - x0) * t;
    const py = terrainHeight(x, z);
    const post = mesh(GEO.post, MAT.wire, x, py + 0.62 * heightMul, z);
    post.scale.y = heightMul;
    post.rotation.z = Math.sin(x * 2.7) * 0.07;
    group.add(post);
  }

  // horizontal strands
  for (const hy of [0.45, 0.8, 1.15]) {
    const w = mesh(GEO.wire, MAT.wire, (x0 + x1) / 2, (y + y2) / 2 + hy * heightMul, z);
    w.rotation.z = Math.PI / 2;
    w.scale.y = len; // cylinder height axis = y before rotation
    group.add(w);
  }

  // barbs
  const nBarbs = Math.round(len / 1.3);
  for (let i = 0; i <= nBarbs; i++) {
    const t = i / nBarbs;
    const x = x0 + (x1 - x0) * t;
    const by = terrainHeight(x, z) + 0.8 * heightMul;
    const b1 = mesh(GEO.barb, MAT.wire, x, by, z, Math.sin(i * 7.3) * Math.PI);
    const b2 = mesh(GEO.barb, MAT.wire, x, by, z, Math.sin(i * 7.3) * Math.PI + Math.PI / 2);
    group.add(b1, b2);
  }
}

// --- shore stakes (tilted logs) -------------------------------------------

function stakeRow(group, x0, x1, z) {
  const n = Math.round(Math.abs(x1 - x0) / 3.4);
  for (let i = 0; i <= n; i++) {
    const t = i / n;
    const x = x0 + (x1 - x0) * t;
    const y = terrainHeight(x, z);
    const s = mesh(GEO.stake, MAT.wood, x, y + 1.0, z);
    s.rotation.x = -0.55;              // lean toward the sea
    s.rotation.y = Math.sin(x * 1.7) * 0.5;
    group.add(s);
  }
}

// --- mines -----------------------------------------------------------------

// --- merge all static prop meshes by material (huge draw-call win) ---------
function mergeStatic(group) {
  group.updateMatrixWorld(true);
  const bins = new Map();
  const meshes = [];
  group.traverse((o) => { if (o.isMesh) meshes.push(o); });
  for (const child of meshes) {
    const g = child.geometry.clone().applyMatrix4(child.matrixWorld);
    const key = child.material;
    if (!bins.has(key)) bins.set(key, []);
    bins.get(key).push(g);
  }
  group.clear();
  for (const [mat, geos] of bins) {
    const merged = mergeGeometries(geos, false);
    const m = new THREE.Mesh(merged, mat);
    m.castShadow = true;
    m.receiveShadow = true;
    group.add(m);
    for (const g of geos) g.dispose();
  }
}

export function buildProps(scene) {
  const group = new THREE.Group();
  group.name = 'props';
  scene.add(group);

  for (const w of SANDBAG_WALLS) sandbagWall(group, w.from, w.to, w.rows);
  for (const h of HEDGEHOGS) group.add(hedgehog(h.x, h.z, h.yaw));

  // dragon's teeth belt in front of the wire
  for (const zRow of [56]) {
    for (let x = -100; x <= 100; x += 6.2) {
      if (Math.abs(x) < 12) continue;               // keep main road open
      if (Math.sin(x * 3.3) > 0.55) continue;       // irregular gaps
      dragonTeeth(group, x + Math.sin(x) * 0.8, zRow + Math.sin(x * 1.9) * 1.6, x);
    }
  }

  for (const line of WIRE_LINES) {
    for (const span of line.spans) wireSpan(group, span[0], span[1], line.z);
  }
  for (const row of STAKE_ROWS) {
    for (const span of row.spans) stakeRow(group, span[0], span[1], row.z);
  }

  // --- tank mines (static, dangerous to vehicles & feet) ---
  const tankMines = [];
  for (const f of TANK_MINE_FIELDS) {
    for (let x = f.x0; x <= f.x1; x += f.step) {
      for (let z = f.z0; z <= f.z1; z += f.step) {
        const jx = Math.sin(x * 12.9898 + z * 78.233) * 1.15;
        const jz = Math.sin(x * 39.346 + z * 11.135) * 1.15;
        const px = x + jx, pz = z + jz;
        if (Math.abs(px) < 11) continue;            // never block the main road
        const y = terrainHeight(px, pz);
        const disc = mesh(GEO.mineDisc, MAT.mineBody, px, y + 0.055, pz, Math.sin(px) * Math.PI);
        const dome = mesh(GEO.mineDome, MAT.mineTop, px, y + 0.1, pz);
        group.add(disc, dome);
        tankMines.push({ x: px, z: pz, r: 1.35, type: 'tank' });
      }
    }
  }

  // --- anti-personnel mines: these explode ---
  const apMines = [];
  for (const f of AP_MINE_FIELDS) {
    for (let i = 0; i < f.count; i++) {
      const s1 = Math.sin(i * 12.9898 + f.x0 * 78.233) * 43758.5453;
      const s2 = Math.sin(i * 39.346 + f.z0 * 11.135) * 43758.5453;
      const px = f.x0 + (s1 - Math.floor(s1)) * (f.x1 - f.x0);
      const pz = f.z0 + (s2 - Math.floor(s2)) * (f.z1 - f.z0);
      if (Math.abs(px) < 10) continue;
      const y = terrainHeight(px, pz);
      const body = mesh(GEO.apBody, MAT.mineBody, px, y + 0.055, pz);
      const pin = mesh(GEO.apPin, MAT.trigger, px, y + 0.14, pz);
      group.add(body, pin);
      apMines.push({ x: px, z: pz, r: 1.15, type: 'ap' });
    }
  }

  // fold the ~1600 static pieces into one mesh per material
  mergeStatic(group);

  return { group, tankMines, apMines };
}

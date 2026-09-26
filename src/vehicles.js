import * as THREE from 'three';
import { terrainHeight } from './terrain.js';
import { TANKS } from './layout.js';

// ---------------------------------------------------------------------------
// Vehicles — a low-poly sedan (Sentra-style silhouette, lofted from profile
// sections, not a box) and stationary low-poly tanks.
// ---------------------------------------------------------------------------

const PAINT = new THREE.MeshStandardMaterial({
  color: '#2b4a78', flatShading: true, roughness: 0.5, metalness: 0.25,
});
const GLASS = new THREE.MeshStandardMaterial({
  color: '#18262f', flatShading: true, roughness: 0.18, metalness: 0.15,
});
const TIRE = new THREE.MeshStandardMaterial({ color: '#1c1f21', flatShading: true, roughness: 0.85 });
const HUB = new THREE.MeshStandardMaterial({ color: '#8b9296', flatShading: true, roughness: 0.45, metalness: 0.55 });
const DARK = new THREE.MeshStandardMaterial({ color: '#22262a', flatShading: true, roughness: 0.7 });
const LIGHT = new THREE.MeshStandardMaterial({ color: '#f2edd8', emissive: '#c9c2a2', emissiveIntensity: 0.55, roughness: 0.35 });
const TAIL = new THREE.MeshStandardMaterial({ color: '#a32922', emissive: '#5c130f', emissiveIntensity: 0.6, roughness: 0.4 });

// --- sedan body lofted from cross-sections ---------------------------------
// Each section: z (position), y0 (floor), y1 (top), w (half width).

function buildSedanBody() {
  const S = [
    { z: 2.25, y0: 0.32, y1: 0.62, w: 0.70 },  // front bumper
    { z: 2.08, y0: 0.28, y1: 0.80, w: 0.86 },  // grille / hood nose
    { z: 1.42, y0: 0.28, y1: 0.88, w: 0.91 },  // hood
    { z: 0.92, y0: 0.28, y1: 0.94, w: 0.93 },  // cowl (windshield base)
    { z: 0.32, y0: 0.30, y1: 1.31, w: 0.85 },  // windshield top
    { z: -0.58, y0: 0.30, y1: 1.34, w: 0.85 }, // roof rear
    { z: -1.18, y0: 0.30, y1: 1.12, w: 0.89 }, // rear window end
    { z: -1.86, y0: 0.28, y1: 0.88, w: 0.91 }, // trunk
    { z: -2.22, y0: 0.32, y1: 0.68, w: 0.78 }, // rear bumper
  ];

  const verts = [];
  const paintIdx = [];
  const glassIdx = [];

  const quad = (a, b, c, d, bucket) => {
    const base = verts.length / 3;
    for (const v of [a, b, c, d]) verts.push(v[0], v[1], v[2]);
    bucket.push(base, base + 1, base + 2, base, base + 2, base + 3);
  };

  for (let i = 0; i < S.length - 1; i++) {
    const s0 = S[i], s1 = S[i + 1];

    const p000 = [-s0.w, s0.y0, s0.z], p001 = [-s1.w, s1.y0, s1.z];
    const p010 = [-s0.w, s0.y1, s0.z], p011 = [-s1.w, s1.y1, s1.z];
    const p100 = [s0.w, s0.y0, s0.z], p101 = [s1.w, s1.y0, s1.z];
    const p110 = [s0.w, s0.y1, s0.z], p111 = [s1.w, s1.y1, s1.z];

    const greenhouse = s0.y1 > 1.02 || s1.y1 > 1.02;
    const isWindshield = i === 3;
    const isRearGlass = i === 5;

    // left / right sides (glass in the greenhouse, paint elsewhere)
    quad(p000, p001, p011, p010, greenhouse ? glassIdx : paintIdx);
    quad(p101, p100, p110, p111, greenhouse ? glassIdx : paintIdx);

    // top face
    let topBucket = paintIdx;
    if (isWindshield || isRearGlass) topBucket = glassIdx;
    quad(p010, p011, p111, p110, topBucket);

    // bottom face
    quad(p100, p101, p001, p000, paintIdx);
  }

  // caps (front +z needs +z normals, rear -z needs -z normals)
  const sF = S[0], sB = S[S.length - 1];
  quad([sF.w, sF.y0, sF.z], [sF.w, sF.y1, sF.z], [-sF.w, sF.y1, sF.z], [-sF.w, sF.y0, sF.z], paintIdx);
  quad([-sB.w, sB.y0, sB.z], [-sB.w, sB.y1, sB.z], [sB.w, sB.y1, sB.z], [sB.w, sB.y0, sB.z], paintIdx);

  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(verts, 3));
  const idx = paintIdx.concat(glassIdx);
  geo.setIndex(idx);
  geo.addGroup(0, paintIdx.length, 0);
  geo.addGroup(paintIdx.length, glassIdx.length, 1);
  geo.computeVertexNormals();
  return geo;
}

function wheel(x, z) {
  const g = new THREE.Group();
  const tire = new THREE.Mesh(new THREE.CylinderGeometry(0.36, 0.36, 0.26, 10), TIRE);
  tire.rotation.z = Math.PI / 2;
  tire.castShadow = true;
  const hub = new THREE.Mesh(new THREE.CylinderGeometry(0.19, 0.19, 0.28, 8), HUB);
  hub.rotation.z = Math.PI / 2;
  g.add(tire, hub);
  g.position.set(x, 0.36, z);
  return g;
}

export function buildCar() {
  const car = new THREE.Group();
  car.name = 'car';

  const body = new THREE.Mesh(buildSedanBody(), [PAINT, GLASS]);
  body.castShadow = true;
  body.receiveShadow = true;
  car.add(body);

  car.add(wheel(0.92, 1.38), wheel(-0.92, 1.38), wheel(0.92, -1.32), wheel(-0.92, -1.32));

  // bumper & trim
  const fBump = new THREE.Mesh(new THREE.BoxGeometry(1.62, 0.16, 0.18), DARK);
  fBump.position.set(0, 0.4, 2.28);
  const rBump = new THREE.Mesh(new THREE.BoxGeometry(1.58, 0.16, 0.18), DARK);
  rBump.position.set(0, 0.42, -2.26);
  const grille = new THREE.Mesh(new THREE.BoxGeometry(1.15, 0.22, 0.06), DARK);
  grille.position.set(0, 0.66, 2.18);

  // lights
  const hl1 = new THREE.Mesh(new THREE.BoxGeometry(0.34, 0.16, 0.06), LIGHT);
  hl1.position.set(0.52, 0.74, 2.19);
  const hl2 = hl1.clone(); hl2.position.x = -0.52;
  const tl1 = new THREE.Mesh(new THREE.BoxGeometry(0.38, 0.14, 0.05), TAIL);
  tl1.position.set(0.56, 0.78, -2.25);
  const tl2 = tl1.clone(); tl2.position.x = -0.56;

  // mirrors
  const m1 = new THREE.Mesh(new THREE.BoxGeometry(0.2, 0.12, 0.1), PAINT);
  m1.position.set(1.02, 1.12, 0.82);
  const m2 = m1.clone(); m2.position.x = -1.02;

  // plate
  const plate = new THREE.Mesh(new THREE.BoxGeometry(0.5, 0.12, 0.03), new THREE.MeshStandardMaterial({ color: '#d9d4c3', flatShading: true }));
  plate.position.set(0, 0.52, -2.29);

  for (const m of [fBump, rBump, grille, hl1, hl2, tl1, tl2, m1, m2, plate]) {
    m.castShadow = true;
    car.add(m);
  }

  return car;
}

// --- tanks (static) --------------------------------------------------------

const TANK_OLIVE = new THREE.MeshStandardMaterial({ color: '#59684a', flatShading: true, roughness: 0.78, metalness: 0.18 });
const TANK_DARK = new THREE.MeshStandardMaterial({ color: '#3f4634', flatShading: true, roughness: 0.82, metalness: 0.2 });
const TANK_TRACK = new THREE.MeshStandardMaterial({ color: '#2b2e26', flatShading: true, roughness: 0.9, metalness: 0.15 });
const SCORCH = new THREE.MeshStandardMaterial({ color: '#26221c', flatShading: true, roughness: 0.95 });

function buildTankMesh(variant = 'intact') {
  const t = new THREE.Group();
  const hullMat = variant === 'wreck' ? TANK_DARK : TANK_OLIVE;

  // hull
  const hull = new THREE.Mesh(new THREE.BoxGeometry(3.1, 0.85, 5.9), hullMat);
  hull.position.y = 1.12;
  const glacis = new THREE.Mesh(new THREE.BoxGeometry(3.05, 0.16, 1.7), hullMat);
  glacis.position.set(0, 1.28, 2.75);
  glacis.rotation.x = -0.62;
  const rear = new THREE.Mesh(new THREE.BoxGeometry(3.05, 0.5, 0.9), hullMat);
  rear.position.set(0, 1.05, -3.1);

  // tracks & wheels
  for (const side of [-1, 1]) {
    const track = new THREE.Mesh(new THREE.BoxGeometry(0.82, 0.92, 6.35), TANK_TRACK);
    track.position.set(side * 1.72, 0.62, 0);
    t.add(track);
    for (let i = 0; i < 5; i++) {
      const w = new THREE.Mesh(new THREE.CylinderGeometry(0.42, 0.42, 0.86, 10), TANK_TRACK);
      w.rotation.z = Math.PI / 2;
      w.position.set(side * 1.72, 0.55, -2.3 + i * 1.15);
      t.add(w);
    }
  }

  // turret
  const turret = new THREE.Mesh(new THREE.CylinderGeometry(1.32, 1.45, 0.8, 8), hullMat);
  turret.position.y = 1.95;
  const bustle = new THREE.Mesh(new THREE.BoxGeometry(1.7, 0.62, 1.5), hullMat);
  bustle.position.set(0, 1.98, -1.15);
  const mantlet = new THREE.Mesh(new THREE.BoxGeometry(1.05, 0.55, 0.55), hullMat);
  mantlet.position.set(0, 1.98, 1.25);
  const barrel = new THREE.Mesh(new THREE.CylinderGeometry(0.11, 0.14, 3.6, 8), TANK_DARK);
  barrel.rotation.x = Math.PI / 2;
  barrel.position.set(0, 1.98, 3.1);
  const hatch = new THREE.Mesh(new THREE.CylinderGeometry(0.34, 0.34, 0.16, 8), TANK_DARK);
  hatch.position.set(0.45, 2.42, -0.35);

  for (const m of [hull, glacis, rear, turret, bustle, mantlet, barrel, hatch]) {
    m.castShadow = true;
    m.receiveShadow = true;
    t.add(m);
  }

  if (variant === 'wreck') {
    // battered look: burnt patch, sagging barrel, tilt
    const burn = new THREE.Mesh(new THREE.CylinderGeometry(1.05, 1.05, 0.02, 8), SCORCH);
    burn.position.set(-0.3, 2.37, -0.9);
    t.add(burn);
    barrel.rotation.x = Math.PI / 2 + 0.12;
    t.rotation.z = 0.045;
    t.rotation.x = -0.03;
  }

  return t;
}

export function buildTanks(scene) {
  const group = new THREE.Group();
  group.name = 'tanks';
  const list = [];
  for (const t of TANKS) {
    const m = buildTankMesh(t.variant);
    m.position.set(t.x, terrainHeight(t.x, t.z) + 0.12, t.z);
    m.rotation.y = t.yaw;
    group.add(m);
    list.push({ mesh: m, x: t.x, z: t.z, r: 3.4 });
  }
  scene.add(group);
  return list;
}

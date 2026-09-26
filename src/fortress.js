import * as THREE from 'three';
import { terrainHeight } from './terrain.js';
import { WORLD, BUNKERS, TOWERS } from './layout.js';

// ---------------------------------------------------------------------------
// The great Wall (Atlantic-wall style), concrete defensive bunkers with
// sentries on lookout, and lookout towers along the parapet.
// ---------------------------------------------------------------------------

const CONCRETE = new THREE.MeshStandardMaterial({ color: '#9b9b93', flatShading: true, roughness: 0.92 });
const CONCRETE_DARK = new THREE.MeshStandardMaterial({ color: '#6f7069', flatShading: true, roughness: 0.94 });
const CONCRETE_LIGHT = new THREE.MeshStandardMaterial({ color: '#adada4', flatShading: true, roughness: 0.9 });
const SLIT = new THREE.MeshStandardMaterial({ color: '#101214', flatShading: true, roughness: 0.9 });
const EARTH = new THREE.MeshStandardMaterial({ color: '#9c8161', flatShading: true, roughness: 0.96 });
const GUNMETAL = new THREE.MeshStandardMaterial({ color: '#2c2f31', flatShading: true, roughness: 0.55, metalness: 0.5 });

// --- soldier figure --------------------------------------------------------

function buildSentry(scale = 1) {
  const g = new THREE.Group();
  const uniform = new THREE.MeshStandardMaterial({ color: '#55604a', flatShading: true, roughness: 0.85 });
  const skin = new THREE.MeshStandardMaterial({ color: '#c9a586', flatShading: true, roughness: 0.8 });
  const helmet = new THREE.MeshStandardMaterial({ color: '#454e3c', flatShading: true, roughness: 0.75 });

  const legL = new THREE.Mesh(new THREE.BoxGeometry(0.17, 0.52, 0.19), uniform);
  legL.position.set(-0.11, 0.26, 0);
  const legR = legL.clone(); legR.position.x = 0.11;
  const torso = new THREE.Mesh(new THREE.BoxGeometry(0.42, 0.52, 0.24), uniform);
  torso.position.y = 0.78;
  const head = new THREE.Mesh(new THREE.BoxGeometry(0.19, 0.2, 0.2), skin);
  head.position.y = 1.16;
  const helm = new THREE.Mesh(new THREE.SphereGeometry(0.155, 8, 6), helmet);
  helm.position.y = 1.27;
  helm.scale.y = 0.72;
  const armL = new THREE.Mesh(new THREE.BoxGeometry(0.13, 0.42, 0.15), uniform);
  armL.position.set(-0.29, 0.82, 0.08);
  armL.rotation.x = -0.55;
  const armR = armL.clone();
  armR.position.x = 0.29;

  // rifle held forward
  const rifle = new THREE.Mesh(new THREE.BoxGeometry(0.055, 0.075, 1.05), GUNMETAL);
  rifle.position.set(0.12, 0.92, 0.52);
  rifle.rotation.x = -0.06;

  for (const m of [legL, legR, torso, head, helm, armL, armR, rifle]) {
    m.castShadow = true;
    g.add(m);
  }

  g.scale.setScalar(scale);
  // muzzle tip (local +z)
  g.userData.muzzle = new THREE.Vector3(0.12, 0.92, 1.05);
  return g;
}

// --- the great wall --------------------------------------------------------

function buildWall(scene) {
  const g = new THREE.Group();
  g.name = 'wall';
  const z = WORLD.wallZ;
  const h = WORLD.wallHeight;
  const gateW = WORLD.gateHalfWidth;

  // wall slabs (gap for the gate)
  const left = new THREE.Mesh(new THREE.BoxGeometry(WORLD.wallHalfWidth - gateW, h, 11), CONCRETE);
  left.position.set(-(WORLD.wallHalfWidth + gateW) / 2, h / 2, z + 5.5);
  const right = left.clone();
  right.position.x = (WORLD.wallHalfWidth + gateW) / 2;

  // lintel over the gate
  const lintel = new THREE.Mesh(new THREE.BoxGeometry(gateW * 2 + 3, h - 9.5, 11), CONCRETE);
  lintel.position.set(0, (h + 9.5) / 2, z + 5.5);

  // gate tunnel side walls & ceiling (dark passage)
  const tunnelL = new THREE.Mesh(new THREE.BoxGeometry(0.8, 9.5, 11.2), CONCRETE_DARK);
  tunnelL.position.set(-gateW - 0.4, 4.75, z + 5.5);
  const tunnelR = tunnelL.clone(); tunnelR.position.x = gateW + 0.4;
  const tunnelTop = new THREE.Mesh(new THREE.BoxGeometry(gateW * 2 + 1.6, 0.8, 11.2), CONCRETE_DARK);
  tunnelTop.position.set(0, 9.5, z + 5.5);
  const tunnelBack = new THREE.Mesh(new THREE.BoxGeometry(gateW * 2, 9.5, 0.8), SLIT);
  tunnelBack.position.set(0, 4.75, z + 11.2);

  // heavy gate pillars
  for (const side of [-1, 1]) {
    const pillar = new THREE.Mesh(new THREE.BoxGeometry(2.6, h + 2.2, 13), CONCRETE_LIGHT);
    pillar.position.set(side * (gateW + 1.6), (h + 2.2) / 2, z + 5.5);
    g.add(pillar);
  }

  // sloped glacis along the base
  const glacis = new THREE.Mesh(new THREE.BoxGeometry(WORLD.wallHalfWidth * 2 + 20, 3.2, 8), CONCRETE_DARK);
  glacis.position.set(0, 1.1, z - 2.2);
  glacis.rotation.x = 0.42;

  // top cap slab
  const cap = new THREE.Mesh(new THREE.BoxGeometry(WORLD.wallHalfWidth * 2 + 20, 1.1, 12.6), CONCRETE_LIGHT);
  cap.position.set(0, h + 0.55, z + 5.5);

  // merlons along the parapet
  for (let x = -WORLD.wallHalfWidth; x <= WORLD.wallHalfWidth; x += 7) {
    if (Math.abs(x) < gateW + 5) continue;
    const m = new THREE.Mesh(new THREE.BoxGeometry(3.4, 1.7, 1.1), CONCRETE_LIGHT);
    m.position.set(x, h + 1.9, z + 0.4);
    g.add(m);
  }

  // vertical panel grooves on the beach face (clean detailing)
  for (let x = -WORLD.wallHalfWidth + 3; x <= WORLD.wallHalfWidth - 3; x += 6) {
    if (Math.abs(x) < gateW + 4) continue;
    const groove = new THREE.Mesh(new THREE.BoxGeometry(0.32, h - 2.5, 0.22), CONCRETE_DARK);
    groove.position.set(x, h / 2, z - 0.05);
    g.add(groove);
  }

  for (const m of [left, right, lintel, tunnelL, tunnelR, tunnelTop, tunnelBack, glacis, cap]) {
    m.castShadow = true;
    m.receiveShadow = true;
    g.add(m);
  }

  scene.add(g);
  return g;
}

// --- defensive bunker (pillbox) -------------------------------------------

function buildBunker(def) {
  const g = new THREE.Group();
  const y = terrainHeight(def.x, def.z);

  const body = new THREE.Mesh(new THREE.BoxGeometry(11, 3.4, 8.4), CONCRETE);
  body.position.y = 1.7;

  const roof = new THREE.Mesh(new THREE.BoxGeometry(12.2, 0.85, 9.6), CONCRETE_LIGHT);
  roof.position.y = 3.75;

  // sloped front face
  const slope = new THREE.Mesh(new THREE.BoxGeometry(11, 1.1, 3.2), CONCRETE_DARK);
  slope.position.set(0, 1.35, -5.2);
  slope.rotation.x = -0.5;

  // wide firing slit (dark recess + gun)
  const slit = new THREE.Mesh(new THREE.BoxGeometry(6.4, 0.62, 0.35), SLIT);
  slit.position.set(0, 2.05, -4.28);
  const gun = new THREE.Mesh(new THREE.CylinderGeometry(0.09, 0.11, 1.9, 8), GUNMETAL);
  gun.rotation.x = Math.PI / 2;
  gun.position.set(0.6, 2.05, -5.15);

  // earth mound on the roof
  const mound = new THREE.Mesh(new THREE.IcosahedronGeometry(4.6, 1), EARTH);
  mound.position.set(0, 4.35, 0.6);
  mound.scale.set(1.35, 0.42, 1.05);

  for (const m of [body, roof, slope, slit, gun, mound]) {
    m.castShadow = true;
    m.receiveShadow = true;
    g.add(m);
  }

  g.position.set(def.x, y, def.z);
  g.rotation.y = def.yaw;
  return g;
}

// --- lookout tower on the wall --------------------------------------------

function buildTower(def) {
  const g = new THREE.Group();
  const wallTop = WORLD.wallHeight + 1.1;

  const platform = new THREE.Mesh(new THREE.BoxGeometry(6.4, 0.55, 5.6), CONCRETE_LIGHT);
  platform.position.y = wallTop + 0.3;

  const cabin = new THREE.Mesh(new THREE.BoxGeometry(4.2, 1.0, 3.6), CONCRETE);
  cabin.position.y = wallTop + 1.1;

  // sandbag ring
  const bagGeo = new THREE.BoxGeometry(0.95, 0.42, 0.6);
  const bagMat = new THREE.MeshStandardMaterial({ color: '#c2ab80', flatShading: true, roughness: 0.95 });
  for (let i = 0; i < 14; i++) {
    const ang = (i / 14) * Math.PI * 2;
    const bag = new THREE.Mesh(bagGeo, bagMat);
    bag.position.set(Math.cos(ang) * 2.9, wallTop + 0.82, Math.sin(ang) * 2.5);
    bag.rotation.y = -ang;
    bag.castShadow = true;
    g.add(bag);
  }

  // mast & warning flag
  const mast = new THREE.Mesh(new THREE.CylinderGeometry(0.07, 0.07, 4.6, 6), GUNMETAL);
  mast.position.set(1.6, wallTop + 3.2, 1.4);
  const flag = new THREE.Mesh(new THREE.PlaneGeometry(1.7, 1.0), new THREE.MeshStandardMaterial({ color: '#b03a2e', side: THREE.DoubleSide, flatShading: true }));
  flag.position.set(2.5, wallTop + 4.8, 1.4);
  flag.rotation.y = Math.PI / 2;

  for (const m of [platform, cabin, mast, flag]) {
    m.castShadow = true;
    m.receiveShadow = true;
    g.add(m);
  }

  g.position.set(def.x, 0, def.z);
  return g;
}

// ---------------------------------------------------------------------------

export function buildFortress(scene) {
  buildWall(scene);

  const sentries = [];
  const group = new THREE.Group();
  group.name = 'fortress';
  scene.add(group);

  for (const b of BUNKERS) {
    group.add(buildBunker(b));
    if (b.sentry) {
      const s = buildSentry(1.05);
      // stand on the sloped face at the firing slit, rifle pointing out to sea
      // (figure faces local +z, so rotation π looks along world -z)
      const y = terrainHeight(b.x, b.z);
      const ox = -1.2, oz = -4.6; // slit offset in bunker-local space
      const cos = Math.cos(b.yaw), sin = Math.sin(b.yaw);
      s.position.set(b.x + ox * cos + oz * sin, y + 1.35, b.z - ox * sin + oz * cos);
      s.rotation.y = b.yaw + Math.PI;
      scene.add(s);
      sentries.push({
        group: s,
        x: s.position.x,
        y: s.position.y + 1.6,
        z: s.position.z,
        yaw: b.yaw + Math.PI,
        homeYaw: b.yaw + Math.PI,
        fireTimer: 1.2 + Math.random() * 1.6,
        range: 240,
        isBunker: true,
      });
    }
  }

  for (const tdef of TOWERS) {
    group.add(buildTower(tdef));
    const s = buildSentry(1.1);
    const y = WORLD.wallHeight + 1.9;
    s.position.set(tdef.x, y, tdef.z - 2.4);
    s.rotation.y = Math.PI; // facing the beach
    scene.add(s);
    sentries.push({
      group: s,
      x: s.position.x,
      y: s.position.y + 1.7,
      z: s.position.z,
      yaw: Math.PI,
      homeYaw: Math.PI,
      fireTimer: 0.8 + Math.random() * 2.2,
      range: 300,
      isBunker: false,
    });
  }

  return { sentries };
}

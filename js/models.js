import * as THREE from "three";

const FLAT = { flatShading: true };

export function lambert(color, extra = {}) {
  return new THREE.MeshLambertMaterial({ color, ...FLAT, ...extra });
}

export function phong(color, extra = {}) {
  return new THREE.MeshPhongMaterial({ color, shininess: 12, ...FLAT, ...extra });
}

function markShadow(obj) {
  obj.traverse((n) => {
    if (n.isMesh) {
      n.castShadow = true;
      n.receiveShadow = true;
    }
  });
  return obj;
}

function box(w, h, d, mat, x = 0, y = 0, z = 0) {
  const m = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), mat);
  m.position.set(x, y, z);
  m.castShadow = true;
  m.receiveShadow = true;
  return m;
}

function cyl(rTop, rBot, h, seg, mat, x = 0, y = 0, z = 0) {
  const m = new THREE.Mesh(new THREE.CylinderGeometry(rTop, rBot, h, seg), mat);
  m.position.set(x, y, z);
  m.castShadow = true;
  m.receiveShadow = true;
  return m;
}

/** Low-poly Nissan Sentra-style compact sedan. Forward = +Z. No negative scales. */
export function createCar() {
  const g = new THREE.Group();
  g.name = "sentra";

  const paint = lambert(0x4a5a38);
  const paintDark = lambert(0x3a482c);
  const trim = lambert(0x2a2c28);
  const chrome = phong(0x9aa0a4, { shininess: 80 });
  const glass = lambert(0x6e8894, { transparent: true, opacity: 0.55 });
  const rubber = lambert(0x1a1a1a);
  const hub = lambert(0x6a6e70);
  const lightF = lambert(0xf2e6b8, { emissive: 0x887744, emissiveIntensity: 0.55 });
  const lightR = lambert(0x8a2018, { emissive: 0x4a0808, emissiveIntensity: 0.4 });
  const interior = lambert(0x2a241c);

  // Custom sedan body in car space: X width, Y height, Z forward. Outward winding.
  const body = buildSedanBody(paint);
  g.add(body);

  // Undertray
  g.add(box(1.70, 0.08, 4.20, paintDark, 0, 0.20, 0.02));

  // Wheel-arch lips
  const fenderMat = paint;
  g.add(box(0.10, 0.22, 0.78, fenderMat, 0.88, 0.42, 1.28));
  g.add(box(0.10, 0.22, 0.78, fenderMat, -0.88, 0.42, 1.28));
  g.add(box(0.10, 0.22, 0.78, fenderMat, 0.88, 0.42, -1.32));
  g.add(box(0.10, 0.22, 0.78, fenderMat, -0.88, 0.42, -1.32));

  // Bumpers
  g.add(box(1.78, 0.22, 0.22, trim, 0, 0.34, 2.28));
  g.add(box(1.78, 0.22, 0.22, trim, 0, 0.34, -2.28));

  // Grille
  g.add(box(0.72, 0.22, 0.06, trim, 0, 0.58, 2.18));
  g.add(box(0.72, 0.04, 0.04, chrome, 0, 0.70, 2.20));

  // Headlights / tail
  g.add(box(0.38, 0.16, 0.08, lightF, 0.62, 0.58, 2.20));
  g.add(box(0.38, 0.16, 0.08, lightF, -0.62, 0.58, 2.20));
  g.add(box(0.38, 0.14, 0.08, lightR, 0.62, 0.58, -2.22));
  g.add(box(0.38, 0.14, 0.08, lightR, -0.62, 0.58, -2.22));

  // Windows (inset, slightly smaller than cabin)
  const windshield = new THREE.Mesh(new THREE.BoxGeometry(1.48, 0.42, 0.04), glass);
  windshield.position.set(0, 1.02, 0.72);
  windshield.rotation.x = -0.55;
  g.add(windshield);
  const rearGlass = new THREE.Mesh(new THREE.BoxGeometry(1.48, 0.36, 0.04), glass);
  rearGlass.position.set(0, 1.02, -0.95);
  rearGlass.rotation.x = 0.52;
  g.add(rearGlass);
  g.add(box(0.04, 0.32, 1.28, glass, 0.84, 1.04, -0.08));
  g.add(box(0.04, 0.32, 1.28, glass, -0.84, 1.04, -0.08));

  // Interior block so windows don't look hollow-wrong
  g.add(box(1.40, 0.28, 1.50, interior, 0, 0.78, -0.05));

  // Side mirrors
  g.add(box(0.18, 0.10, 0.14, paint, 0.98, 0.92, 0.55));
  g.add(box(0.18, 0.10, 0.14, paint, -0.98, 0.92, 0.55));

  // Window frames / pillars
  g.add(box(0.07, 0.46, 0.07, trim, 0.82, 1.02, 0.58));
  g.add(box(0.07, 0.46, 0.07, trim, -0.82, 1.02, 0.58));
  g.add(box(0.07, 0.46, 0.07, trim, 0.82, 1.02, -0.72));
  g.add(box(0.07, 0.46, 0.07, trim, -0.82, 1.02, -0.72));
  g.add(box(1.72, 0.04, 0.04, chrome, 0, 0.78, 2.05));

  // Hood star (invasion marking)
  const star = makeStar(0.22, lambert(0xe8e0d0));
  star.rotation.x = -Math.PI / 2;
  star.position.set(0, 0.825, 1.35);
  g.add(star);

  // Wheels — cylinders rotated, never negatively scaled
  const wheels = [];
  const wheelPos = [
    [0.82, 0.32, 1.28],
    [-0.82, 0.32, 1.28],
    [0.82, 0.32, -1.32],
    [-0.82, 0.32, -1.32],
  ];
  for (const [x, y, z] of wheelPos) {
    const w = makeWheel(rubber, hub);
    w.position.set(x, y, z);
    g.add(w);
    wheels.push(w);
  }

  g.userData.wheels = wheels;
  markShadow(g);
  return g;
}

function buildSedanBody(mat) {
  // Rings of the sedan from rear to front. Each ring: 8 points around the cross-section.
  // y values follow a Sentra: low nose, hood, greenhouse, trunk.
  const rings = [
    // rear bumper
    { z: -2.22, w: 0.86, y0: 0.18, y1: 0.52, y2: 0.52 },
    // trunk rear
    { z: -2.05, w: 0.90, y0: 0.18, y1: 0.72, y2: 0.72 },
    // trunk
    { z: -1.55, w: 0.90, y0: 0.22, y1: 0.78, y2: 0.78 },
    // C-pillar / rear window
    { z: -1.15, w: 0.88, y0: 0.22, y1: 0.82, y2: 1.10 },
    // roof rear
    { z: -0.85, w: 0.84, y0: 0.22, y1: 0.82, y2: 1.28 },
    // roof mid
    { z: 0.10, w: 0.84, y0: 0.22, y1: 0.80, y2: 1.30 },
    // roof front / windshield top
    { z: 0.55, w: 0.84, y0: 0.22, y1: 0.80, y2: 1.22 },
    // windshield base / cowl
    { z: 0.95, w: 0.90, y0: 0.20, y1: 0.78, y2: 0.80 },
    // hood mid
    { z: 1.55, w: 0.90, y0: 0.20, y1: 0.74, y2: 0.74 },
    // nose
    { z: 2.08, w: 0.88, y0: 0.18, y1: 0.68, y2: 0.68 },
    // front bumper
    { z: 2.22, w: 0.86, y0: 0.18, y1: 0.50, y2: 0.50 },
  ];

  const positions = [];
  const indices = [];
  const nR = rings.length;

  // Per ring: 6 verts — bottom-left, bottom-right, belt-right, roof-right, roof-left, belt-left
  function ringVerts(r) {
    const { z, w, y0, y1, y2 } = r;
    return [
      [-w, y0, z],
      [w, y0, z],
      [w, y1, z],
      [w * 0.92, y2, z],
      [-w * 0.92, y2, z],
      [-w, y1, z],
    ];
  }

  for (const r of rings) {
    for (const v of ringVerts(r)) positions.push(v[0], v[1], v[2]);
  }

  const rv = 6;
  for (let i = 0; i < nR - 1; i++) {
    const a = i * rv;
    const b = (i + 1) * rv;
    // sides: 0-1 bottom, 1-2 right lower, 2-3 right upper, 3-4 roof, 4-5 left upper, 5-0 left lower
    const edges = [
      [0, 1],
      [1, 2],
      [2, 3],
      [3, 4],
      [4, 5],
      [5, 0],
    ];
    for (const [i0, i1] of edges) {
      indices.push(a + i0, a + i1, b + i1);
      indices.push(a + i0, b + i1, b + i0);
    }
  }
  // Front cap (last ring, facing +Z)
  {
    const s = (nR - 1) * rv;
    indices.push(s + 0, s + 1, s + 2);
    indices.push(s + 0, s + 2, s + 5);
    indices.push(s + 5, s + 2, s + 3);
    indices.push(s + 5, s + 3, s + 4);
  }
  // Rear cap (first ring, facing -Z) — reverse winding
  {
    const s = 0;
    indices.push(s + 0, s + 2, s + 1);
    indices.push(s + 0, s + 5, s + 2);
    indices.push(s + 5, s + 3, s + 2);
    indices.push(s + 5, s + 4, s + 3);
  }

  const geo = new THREE.BufferGeometry();
  geo.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
  geo.setIndex(indices);
  geo.computeVertexNormals();
  const solid = geo.toNonIndexed();
  solid.computeVertexNormals();
  solid.computeBoundingBox();
  solid.computeBoundingSphere();
  geo.dispose();
  const mesh = new THREE.Mesh(solid, mat);
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  return mesh;
}

function makeWheel(rubber, hub) {
  const g = new THREE.Group();
  const tire = new THREE.Mesh(new THREE.CylinderGeometry(0.32, 0.32, 0.22, 10), rubber);
  tire.rotation.z = Math.PI / 2;
  tire.castShadow = true;
  const disc = new THREE.Mesh(new THREE.CylinderGeometry(0.18, 0.18, 0.24, 8), hub);
  disc.rotation.z = Math.PI / 2;
  g.add(tire, disc);
  return g;
}

function makeStar(r, mat) {
  const shape = new THREE.Shape();
  const spikes = 5;
  for (let i = 0; i < spikes * 2; i++) {
    const a = -Math.PI / 2 + (i * Math.PI) / spikes;
    const rad = i % 2 === 0 ? r : r * 0.4;
    const x = Math.cos(a) * rad;
    const y = Math.sin(a) * rad;
    if (i === 0) shape.moveTo(x, y);
    else shape.lineTo(x, y);
  }
  shape.closePath();
  const geo = new THREE.ExtrudeGeometry(shape, { depth: 0.02, bevelEnabled: false });
  geo.computeVertexNormals();
  return new THREE.Mesh(geo, mat);
}

/** Static low-poly medium tank. Forward +Z. */
export function createTank(wreck = false) {
  const g = new THREE.Group();
  const hullC = wreck ? 0x3a3e32 : 0x4d5338;
  const dark = wreck ? 0x2a2c24 : 0x3a4030;
  const hull = lambert(hullC);
  const iron = lambert(dark);
  const rust = lambert(0x5a4030);

  g.add(box(2.6, 0.85, 4.4, hull, 0, 0.85, 0));
  // sloped glacis
  const glacis = box(2.5, 0.18, 1.5, hull, 0, 1.15, 1.7);
  glacis.rotation.x = -0.45;
  g.add(glacis);
  g.add(box(2.4, 0.45, 2.2, hull, 0, 1.42, -0.2));

  const turret = cyl(0.95, 1.05, 0.7, 8, hull, 0, 1.95, -0.15);
  g.add(turret);
  const mantle = box(1.1, 0.45, 0.5, hull, 0, 1.95, 0.85);
  g.add(mantle);
  const barrel = cyl(0.12, 0.14, 2.6, 6, iron, 0, 1.95, 2.2);
  barrel.rotation.x = Math.PI / 2;
  g.add(barrel);
  g.add(cyl(0.18, 0.18, 0.2, 6, iron, 0, 1.95, 3.45)).rotation.x = Math.PI / 2;

  // tracks
  g.add(box(0.55, 0.7, 4.6, iron, 1.45, 0.45, 0));
  g.add(box(0.55, 0.7, 4.6, iron, -1.45, 0.45, 0));
  for (let i = -2; i <= 2; i++) {
    g.add(cyl(0.28, 0.28, 0.58, 8, rust, 1.45, 0.28, i * 0.9)).rotation.z = Math.PI / 2;
    g.add(cyl(0.28, 0.28, 0.58, 8, rust, -1.45, 0.28, i * 0.9)).rotation.z = Math.PI / 2;
  }

  if (wreck) {
    turret.rotation.z = 0.25;
    turret.rotation.y = 0.4;
    barrel.rotation.z = 0.2;
    const soot = lambert(0x1a1a16);
    g.add(box(1.2, 0.15, 1.4, soot, 0.3, 1.85, 0.2));
  }

  g.userData.radius = 2.6;
  markShadow(g);
  return g;
}

/** Low-poly twin-engine bomber. Forward +Z. */
export function createPlane() {
  const g = new THREE.Group();
  const skin = lambert(0x4a5248);
  const dark = lambert(0x2e342e);
  const glass = lambert(0x7a90a0, { transparent: true, opacity: 0.5 });

  g.add(box(1.1, 0.9, 7.2, skin, 0, 0, 0));
  g.add(box(0.7, 0.55, 1.6, skin, 0, 0.15, 3.9));
  g.add(box(0.55, 0.4, 0.9, glass, 0, 0.42, 3.5));
  // wings
  g.add(box(11.5, 0.14, 2.1, skin, 0, -0.05, 0.4));
  g.add(box(3.2, 0.12, 1.1, skin, 0, 0.55, -3.2));
  g.add(box(0.14, 1.3, 1.0, skin, 0, 0.7, -3.3));
  // engines
  const e1 = cyl(0.32, 0.32, 1.6, 6, dark, 2.4, -0.15, 0.9);
  e1.rotation.x = Math.PI / 2;
  const e2 = cyl(0.32, 0.32, 1.6, 6, dark, -2.4, -0.15, 0.9);
  e2.rotation.x = Math.PI / 2;
  g.add(e1, e2);
  const p1 = new THREE.Mesh(new THREE.BoxGeometry(0.08, 1.4, 0.18), dark);
  p1.position.set(2.4, -0.15, 1.75);
  const p2 = p1.clone();
  p2.position.x = -2.4;
  g.add(p1, p2);
  g.userData.props = [p1, p2];
  g.userData.radius = 6;
  return g;
}

export function createBomb() {
  const g = new THREE.Group();
  const iron = lambert(0x2a2c28);
  const b = cyl(0.12, 0.16, 0.7, 6, iron);
  g.add(b);
  const fin = box(0.28, 0.28, 0.04, iron, 0, 0, -0.28);
  g.add(fin);
  const fin2 = box(0.04, 0.28, 0.28, iron, 0, 0, -0.28);
  g.add(fin2);
  return g;
}

/** Concrete MG bunker with embrasure. Gun faces +Z. */
export function createBunker() {
  const g = new THREE.Group();
  const conc = lambert(0x7a776e);
  const concD = lambert(0x5c5a52);
  const dark = lambert(0x1a1a16);

  g.add(box(7.2, 2.6, 5.4, conc, 0, 1.2, 0));
  g.add(box(7.6, 0.35, 5.8, concD, 0, 2.55, 0));
  // embrasure slit
  g.add(box(2.4, 0.55, 0.4, dark, 0, 1.55, 2.55));
  // wing walls
  g.add(box(1.1, 2.4, 3.2, conc, 4.0, 1.1, 0.4));
  g.add(box(1.1, 2.4, 3.2, conc, -4.0, 1.1, 0.4));
  // rear step
  g.add(box(3.2, 0.9, 1.6, concD, 0, 0.45, -2.6));

  const bags = createSandbagStack(5.4, 2);
  bags.position.set(0, 2.72, 0.4);
  g.add(bags);

  const gun = createMachineGun();
  gun.position.set(0, 1.45, 1.7);
  g.add(gun);
  g.userData.gun = gun;
  g.userData.muzzleLocal = new THREE.Vector3(0, 0.12, 1.35);
  g.userData.radius = 4.6;
  g.userData.solid = true;
  markShadow(g);
  return g;
}

/** Open sandbag nest with MG. */
export function createNest() {
  const g = new THREE.Group();
  const pit = lambert(0x5a4a38);
  g.add(cyl(1.8, 2.1, 0.7, 8, pit, 0, 0.2, 0));
  const ring = createSandbagRing(2.0);
  ring.position.y = 0.55;
  g.add(ring);
  const gun = createMachineGun();
  gun.position.set(0, 0.72, 0);
  g.add(gun);
  g.userData.gun = gun;
  g.userData.muzzleLocal = new THREE.Vector3(0, 0.12, 1.35);
  g.userData.radius = 2.2;
  g.userData.solid = true;
  markShadow(g);
  return g;
}

/** Belt-fed machine gun on a pintle. Barrel along +Z. */
export function createMachineGun() {
  const g = new THREE.Group();
  const iron = phong(0x2c2e2a, { shininess: 40 });
  const wood = lambert(0x4a3420);
  const ammo = lambert(0x3a3a28);

  // shield
  g.add(box(0.72, 0.42, 0.04, iron, 0, 0.18, 0.28));
  // receiver
  g.add(box(0.16, 0.16, 0.55, iron, 0, 0.10, 0.15));
  // perforated jacket / barrel
  const jacket = cyl(0.055, 0.055, 1.15, 6, iron, 0, 0.12, 0.85);
  jacket.rotation.x = Math.PI / 2;
  g.add(jacket);
  const barrel = cyl(0.03, 0.03, 0.45, 6, iron, 0, 0.12, 1.55);
  barrel.rotation.x = Math.PI / 2;
  g.add(barrel);
  // bipod / pintle
  g.add(box(0.06, 0.28, 0.06, iron, 0.12, -0.05, 0.1));
  g.add(box(0.06, 0.28, 0.06, iron, -0.12, -0.05, 0.1));
  g.add(box(0.08, 0.08, 0.22, wood, 0, 0.08, -0.28));
  // ammo box
  g.add(box(0.28, 0.16, 0.18, ammo, 0.28, 0.02, 0.05));

  const flash = new THREE.Mesh(
    new THREE.ConeGeometry(0.12, 0.38, 5),
    new THREE.MeshBasicMaterial({ color: 0xffee88, transparent: true, opacity: 0 })
  );
  flash.rotation.x = Math.PI / 2;
  flash.position.set(0, 0.12, 1.85);
  g.add(flash);
  g.userData.flash = flash;
  g.userData.muzzle = flash.position;
  return g;
}

/** Anti-personnel mine — small disc. */
export function createMine() {
  const g = new THREE.Group();
  const body = lambert(0x4a4638);
  const top = lambert(0x3a382c);
  g.add(cyl(0.28, 0.30, 0.10, 8, body, 0, 0.05, 0));
  g.add(cyl(0.16, 0.16, 0.04, 8, top, 0, 0.11, 0));
  g.userData.radius = 0.55;
  g.userData.kind = "mine";
  return g;
}

/** Anti-tank mine with a proper painted X on the pressure plate. */
export function createTankMine() {
  const g = new THREE.Group();
  const body = lambert(0x3e3a2c);
  const plate = lambert(0x5a5644);
  const xMat = lambert(0xc4b070);
  g.add(cyl(0.55, 0.58, 0.16, 8, body, 0, 0.07, 0));
  g.add(cyl(0.42, 0.42, 0.05, 8, plate, 0, 0.16, 0));
  // X
  const a = box(0.62, 0.03, 0.08, xMat, 0, 0.19, 0);
  a.rotation.y = Math.PI / 4;
  const b = box(0.62, 0.03, 0.08, xMat, 0, 0.19, 0);
  b.rotation.y = -Math.PI / 4;
  g.add(a, b);
  // lugs
  g.add(box(0.12, 0.05, 0.18, body, 0.52, 0.08, 0));
  g.add(box(0.12, 0.05, 0.18, body, -0.52, 0.08, 0));
  g.userData.radius = 0.9;
  g.userData.kind = "tankmine";
  return g;
}

/** Wooden / steel Czech hedgehog. */
export function createHedgehog() {
  const g = new THREE.Group();
  const iron = lambert(0x4a4c46);
  const beam = (rot) => {
    const m = box(0.22, 0.22, 2.6, iron);
    m.rotation.set(rot[0], rot[1], rot[2]);
    return m;
  };
  g.add(beam([0.6, 0.2, 0.5]));
  g.add(beam([-0.55, 0.7, -0.4]));
  g.add(beam([0.15, -0.6, 0.85]));
  g.userData.radius = 1.3;
  g.userData.solid = true;
  markShadow(g);
  return g;
}

/** C-element / Belgian gate — steel beach obstacle. */
export function createBelgianGate() {
  const g = new THREE.Group();
  const iron = lambert(0x4a4c46);
  g.add(box(3.2, 0.12, 0.12, iron, 0, 0.08, 0));
  g.add(box(3.2, 0.12, 0.12, iron, 0, 1.7, 0));
  g.add(box(0.12, 1.8, 0.12, iron, -1.5, 0.9, 0));
  g.add(box(0.12, 1.8, 0.12, iron, 1.5, 0.9, 0));
  g.add(box(0.12, 1.8, 0.12, iron, 0, 0.9, 0));
  g.add(box(3.2, 0.08, 0.08, iron, 0, 0.9, 0));
  const d1 = box(0.08, 2.3, 0.08, iron, 0, 0.9, 0);
  d1.rotation.z = 0.72;
  const d2 = box(0.08, 2.3, 0.08, iron, 0, 0.9, 0);
  d2.rotation.z = -0.72;
  g.add(d1, d2);
  g.userData.radius = 1.7;
  g.userData.solid = true;
  markShadow(g);
  return g;
}

/** Wooden anti-vehicle barricade / X-frame. */
export function createBarricade() {
  const g = new THREE.Group();
  const wood = lambert(0x6a5238);
  const dark = lambert(0x3a2c1c);
  const a = box(0.22, 2.2, 0.22, wood, -0.7, 1.1, 0);
  a.rotation.z = 0.45;
  const b = box(0.22, 2.2, 0.22, wood, 0.7, 1.1, 0);
  b.rotation.z = -0.45;
  g.add(a, b);
  g.add(box(2.4, 0.16, 0.16, dark, 0, 0.7, 0));
  g.add(box(2.2, 0.16, 0.16, dark, 0, 1.35, 0));
  g.add(box(0.3, 0.3, 1.4, wood, -1.1, 0.15, 0));
  g.add(box(0.3, 0.3, 1.4, wood, 1.1, 0.15, 0));
  g.userData.radius = 1.5;
  g.userData.solid = true;
  markShadow(g);
  return g;
}

/** Dragon's teeth concrete pyramid. */
export function createDragonTooth() {
  const m = new THREE.Mesh(new THREE.ConeGeometry(0.85, 1.5, 4), lambert(0x7a776e));
  m.rotation.y = Math.PI / 4;
  m.castShadow = true;
  m.receiveShadow = true;
  const g = new THREE.Group();
  g.add(m);
  g.userData.radius = 0.9;
  g.userData.solid = true;
  return g;
}

export function createSandbag(mat) {
  const mesh = new THREE.Mesh(new THREE.CapsuleGeometry(0.16, 0.38, 3, 6), mat || lambert(0x8a7a4a));
  mesh.rotation.z = Math.PI / 2;
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  return mesh;
}

export function createSandbagStack(width, rows) {
  const g = new THREE.Group();
  const mat = lambert(0x8a7a4a);
  const bagW = 0.55;
  const n = Math.max(2, Math.floor(width / bagW));
  for (let r = 0; r < rows; r++) {
    const count = n - (r % 2);
    const off = (r % 2) * 0.22;
    for (let i = 0; i < count; i++) {
      const b = createSandbag(mat);
      b.position.set(-width / 2 + off + i * bagW + 0.28, 0.16 + r * 0.26, 0);
      b.rotation.y = (i % 3) * 0.05;
      g.add(b);
    }
  }
  return g;
}

export function createSandbagRing(radius) {
  const g = new THREE.Group();
  const mat = lambert(0x8a7a4a);
  const n = 14;
  for (let i = 0; i < n; i++) {
    const a = (i / n) * Math.PI * 2;
    const b = createSandbag(mat);
    b.position.set(Math.cos(a) * radius, 0.16, Math.sin(a) * radius);
    b.rotation.y = -a;
    g.add(b);
  }
  for (let i = 0; i < n; i++) {
    const a = ((i + 0.5) / n) * Math.PI * 2;
    const b = createSandbag(mat);
    b.position.set(Math.cos(a) * (radius - 0.08), 0.40, Math.sin(a) * (radius - 0.08));
    b.rotation.y = -a;
    g.add(b);
  }
  return g;
}

/**
 * Proper barbed-wire obstacle: wooden stakes, 3 horizontal strands,
 * X-bracing, and a concertina coil in front.
 * Length along X, faces +Z.
 */
export function createBarbedWire(length = 8) {
  const g = new THREE.Group();
  const wood = lambert(0x5a4630);
  const wireMat = new THREE.MeshStandardMaterial({
    color: 0x6a6e64,
    metalness: 0.7,
    roughness: 0.45,
    flatShading: true,
  });

  const posts = Math.max(2, Math.round(length / 2.4));
  const half = length / 2;
  for (let i = 0; i < posts; i++) {
    const t = posts === 1 ? 0.5 : i / (posts - 1);
    const x = -half + t * length;
    const post = box(0.12, 1.35, 0.12, wood, x, 0.62, 0);
    post.rotation.z = (i % 2 === 0 ? 0.04 : -0.05);
    post.rotation.x = 0.03;
    g.add(post);
  }

  const heights = [0.35, 0.75, 1.15];
  for (const y of heights) {
    const strand = makeWireStrand(-half, y, 0, half, y, 0, 0.018, wireMat);
    g.add(strand);
  }
  // X bracing between posts
  for (let i = 0; i < posts - 1; i++) {
    const t0 = i / (posts - 1);
    const t1 = (i + 1) / (posts - 1);
    const x0 = -half + t0 * length;
    const x1 = -half + t1 * length;
    g.add(makeWireStrand(x0, 0.3, 0, x1, 1.15, 0, 0.014, wireMat));
    g.add(makeWireStrand(x0, 1.15, 0, x1, 0.3, 0, 0.014, wireMat));
  }

  // Concertina coil in front of the fence
  const coil = makeConcertina(length * 0.92, 0.42, wireMat);
  coil.position.set(0, 0.42, 0.55);
  g.add(coil);

  g.userData.radius = length * 0.45;
  g.userData.length = length;
  g.userData.kind = "wire";
  markShadow(g);
  return g;
}

function makeWireStrand(x0, y0, z0, x1, y1, z1, radius, mat) {
  const a = new THREE.Vector3(x0, y0, z0);
  const b = new THREE.Vector3(x1, y1, z1);
  const dir = new THREE.Vector3().subVectors(b, a);
  const len = dir.length();
  const mid = new THREE.Vector3().addVectors(a, b).multiplyScalar(0.5);
  // slight sag
  const pts = [
    a.clone(),
    mid.clone().add(new THREE.Vector3(0, -0.04, (Math.random() - 0.5) * 0.05)),
    b.clone(),
  ];
  const curve = new THREE.CatmullRomCurve3(pts);
  const geo = new THREE.TubeGeometry(curve, 6, radius, 4, false);
  geo.computeVertexNormals();
  return new THREE.Mesh(geo, mat);
}

function makeConcertina(length, radius, mat) {
  const pts = [];
  const turns = Math.max(6, Math.floor(length / 0.55));
  for (let i = 0; i <= turns * 10; i++) {
    const t = i / 10;
    const a = t * Math.PI * 2;
    const x = -length / 2 + (t / turns) * length;
    const y = Math.cos(a) * radius;
    const z = Math.sin(a) * radius * 0.85;
    pts.push(new THREE.Vector3(x, y, z));
  }
  const curve = new THREE.CatmullRomCurve3(pts);
  const geo = new THREE.TubeGeometry(curve, pts.length, 0.022, 4, false);
  geo.computeVertexNormals();
  return new THREE.Mesh(geo, mat);
}

/** Massive Atlantic Wall section spanning the far end. */
export function createAtlanticWall(width = 200) {
  const g = new THREE.Group();
  const conc = lambert(0x7c786e);
  const concD = lambert(0x5e5c54);
  const concS = lambert(0x8a8680);
  const rust = lambert(0x4a3a30);

  // Main curtain wall
  g.add(box(width, 28, 8, conc, 0, 12, 0));
  g.add(box(width + 4, 4, 12, concD, 0, 1.5, 1.5));
  g.add(box(width, 3.2, 6, concS, 0, 26.4, 0));

  // Crenellations
  for (let x = -width / 2 + 4; x < width / 2; x += 8) {
    g.add(box(4.2, 3.4, 5.2, conc, x, 29.2, 0));
  }

  // Buttresses
  for (let x = -width / 2 + 10; x < width / 2; x += 22) {
    g.add(box(3.2, 22, 14, concD, x, 10, 4));
  }

  // Casemates
  for (let x = -60; x <= 60; x += 40) {
    g.add(box(12, 8, 10, conc, x, 8, 6));
    g.add(box(3.2, 1.4, 0.6, lambert(0x151512), x, 8.2, 11.2));
  }

  // Stains / dripping
  for (let i = 0; i < 18; i++) {
    const x = -width / 2 + 8 + i * (width / 18);
    g.add(box(1.6 + (i % 3), 10 + (i % 5) * 1.4, 0.2, lambert(0x5a584e), x, 8, 4.1));
  }

  // Finish banners
  const banner = lambert(0x2c3a28);
  g.add(box(18, 4.5, 0.2, banner, 0, 16, 4.2));
  const star = makeStar(1.4, lambert(0xe8e0d0));
  star.position.set(0, 16, 4.4);
  g.add(star);

  // Steel doors / objective gate recess
  g.add(box(10, 8, 1.2, rust, 0, 5, 4.2));

  g.userData.radius = width;
  markShadow(g);
  return g;
}

export function createTimberBridge(width = 5, length = 8) {
  const g = new THREE.Group();
  const wood = lambert(0x6a5238);
  const dark = lambert(0x3e2e1c);
  g.add(box(width, 0.18, length, wood, 0, 0.12, 0));
  for (let z = -length / 2; z <= length / 2; z += 0.7) {
    g.add(box(width + 0.1, 0.08, 0.22, dark, 0, 0.22, z));
  }
  g.add(box(0.18, 0.7, length, wood, width / 2, 0.45, 0));
  g.add(box(0.18, 0.7, length, wood, -width / 2, 0.45, 0));
  g.userData.kind = "bridge";
  markShadow(g);
  return g;
}

export function createCrate() {
  const g = new THREE.Group();
  const wood = lambert(0x6b5a3a);
  g.add(box(1.1, 0.9, 1.1, wood, 0, 0.45, 0));
  g.userData.radius = 0.8;
  g.userData.solid = true;
  return g;
}

export function createSmokeColumn() {
  const g = new THREE.Group();
  const mat = new THREE.MeshBasicMaterial({
    color: 0x3a3a38,
    transparent: true,
    opacity: 0.28,
    depthWrite: false,
  });
  for (let i = 0; i < 5; i++) {
    const s = new THREE.Mesh(new THREE.SphereGeometry(0.7 + i * 0.25, 6, 5), mat.clone());
    s.position.set((i % 2) * 0.2, 1 + i * 1.1, 0);
    g.add(s);
  }
  g.userData.puffs = g.children;
  return g;
}

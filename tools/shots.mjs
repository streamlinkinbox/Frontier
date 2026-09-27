/**
 * Offscreen visual review.
 *
 * The sandbox has no browser, so this drives the real rig through the real
 * gait/flight/attachment code and rasterises it on the CPU (`raster.mjs`) to
 * PNGs. It is the only way to actually LOOK at the asset here — every number
 * in `verify.mjs` can pass while the silhouette is wrong.
 *
 *   node tools/shots.mjs [outDir]      # default tools/shots/
 */
import { installDOM } from './domstub.mjs';
installDOM();
const THREE = await import('three');
const { buildMosquito } = await import('/home/user/Frontier/src/mosquito/build.js');
const { buildMaterials } = await import('/home/user/Frontier/src/mosquito/materials.js');
const { GaitController } = await import('/home/user/Frontier/src/anim/gait.js');
const SF = await import('/home/user/Frontier/src/world/surface.js');
const { render, writePNG, makeCamera, W, H } = await import('./raster.mjs');
const A = await import('/home/user/Frontier/src/mosquito/anatomy.js');

const flatField = () => {
  const f = new SF.SurfaceField();
  f.add(new SF.Heightfield({ fn: () => 0, gradient: (x, z, o) => o.set(0, 1, 0), extent: 4000 }));
  return f;
};

function colorOf(mesh) {
  const n = mesh.name || '';
  if (n === 'ground') return [58, 60, 64];
  if (/Claw|Pretarsus|Pulvillus|Empod/.test(n)) return [84, 72, 58];
  if (/Tarsomere|Tibia|Troch/.test(n)) return [178, 154, 120];
  if (/Femur|Coxa/.test(n)) return [170, 142, 106];
  if (/Wing|Membrane|Vein/.test(n)) return [214, 216, 224];
  if (/Haltere/.test(n)) return [198, 198, 206];
  if (/Antenna|Flag|Palp/.test(n)) return [98, 94, 90];
  if (/Head|Prob|Labium|Labell/.test(n)) return [152, 128, 98];
  if (/Thorax|Sclerite|Scutum/.test(n)) return [138, 120, 94];
  if (/Abdo|Terg|Stern/.test(n)) return [124, 106, 86];
  return [150, 128, 100];
}

/** Camera that frames the whole rig from `dir` with `margin` extra room. */
function frame(mosq, dir, up = [0, 1, 0], margin = 1.06) {
  mosq.root.updateWorldMatrix(true, true);
  const b = new THREE.Box3();
  mosq.root.traverse((o) => { if (o.isMesh && o.name !== 'ground') b.expandByObject(o); });
  const c = b.getCenter(new THREE.Vector3());
  const r = b.getSize(new THREE.Vector3()).length() / 2;
  const d = (r * margin) / Math.tan(24 * Math.PI / 180 / 2) * 0.62;
  const eye = [c.x + dir[0] * d, c.y + dir[1] * d, c.z + dir[2] * d];
  return makeCamera({ eye, target: [c.x, c.y, c.z], up, fov: 24 });
}

/** Dark ground grid so foot contact is visible. */
function addGround(scene, cx, cz, y, half = 9, step = 1) {
  const g = new THREE.BufferGeometry();
  const v = [];
  const push = (a, b, cc) => v.push(...a, ...b, ...cc);
  for (let i = -half; i <= half; i += step) {
    const t = 0.012;
    push([cx + i, y, cz - half], [cx + i + t, y, cz - half], [cx + i + t, y, cz + half]);
    push([cx + i, y, cz - half], [cx + i + t, y, cz + half], [cx + i, y, cz + half]);
    push([cx - half, y, cz + i], [cx + half, y, cz + i], [cx + half, y, cz + i + t]);
    push([cx - half, y, cz + i], [cx + half, y, cz + i + t], [cx - half, y, cz + i + t]);
  }
  g.setAttribute('position', new THREE.Float32BufferAttribute(v, 3));
  const m = new THREE.Mesh(g, null);
  m.name = 'ground';
  m.frustumCulled = false;
  scene.add(m);
}

function makeRig(field) {
  const mosq = buildMosquito(buildMaterials(null, 0.5));
  const f = field || flatField();
  return { mosq, f, g: new GaitController(mosq, f, { rate: 1 }) };
}

function walk(mosq, g, frames, speed = 18) {
  mosq.root.updateWorldMatrix(true, true);
  g.attach(new THREE.Vector3(0, A.GAIT.bodyClearance + 0.02, 0), new THREE.Vector3(0, 1, 0), new THREE.Vector3(1, 0, 0));
  for (let i = 0; i < frames; i++) g.update(1 / 120, { speed, accel: i < 70 ? 200 : 0 });
}

const out = process.argv[2] || '/home/user/Frontier/tools/shots';
const { mkdirSync } = await import('node:fs');
mkdirSync(out, { recursive: true });

const jobs = [];
const add = (name, fn) => jobs.push({ name, fn });

// --- resting rig, three views ------------------------------------------
for (const [name, dir, up] of [
  ['rest-3q', [-0.55, 0.34, 0.76], [0, 1, 0]],
  ['rest-side', [-1, 0.05, 0], [0, 1, 0]],
  ['rest-top', [0, 1, 0.001], [1, 0, 0]],
  ['rest-front', [0.05, 0.18, 1], [0, 1, 0]],
]) {
  add(name, () => { const { mosq } = makeRig(); return { mosq, cam: frame(mosq, dir, up) }; });
}

// --- walking, several phases, three views ------------------------------
for (const [tag, frames] of [['p1', 92], ['p2', 100], ['p3', 104], ['p4', 108]]) {
  for (const [vname, dir, up] of [['side', [-0.10, 0.09, 1], [0, 1, 0]], ['3q', [-0.40, 0.28, 0.87], [0, 1, 0]], ['front', [1, 0.12, 0.10], [0, 1, 0]]]) {
    add(`walk-${tag}-${vname}`, () => {
      const { mosq, g } = makeRig(); walk(mosq, g, frames);
      addGround(mosq.root, g.bodyPos.x, 0, 0);
      return { mosq, cam: frame(mosq, dir, up) };
    });
  }
}

// --- foot close-up ------------------------------------------------------
add('foot', () => {
  const { mosq, g } = makeRig(); walk(mosq, g, 100);
  const L = mosq.legs.R3;
  const p = L.pretarsus.getWorldPosition(new THREE.Vector3());
  const cam = makeCamera({ eye: [p.x - 1.5, p.y + 1.3, p.z + 2.4], target: [p.x, p.y + 0.1, p.z], up: [0, 1, 0], fov: 26 });
  return { mosq, cam };
});

// --- rough ground -------------------------------------------------------
add('rough', () => {
  const f = new SF.SurfaceField();
  f.add(new SF.Heightfield({
    fn: (x, z) => 0.16 * Math.sin(x * 1.9) * Math.cos(z * 2.3) + 0.08 * Math.sin(x * 4.1 + z * 3.3),
    gradient: (x, z, o) => {
      const dx = 0.16 * 1.9 * Math.cos(x * 1.9) * Math.cos(z * 2.3) + 0.08 * 4.1 * Math.cos(x * 4.1 + z * 3.3);
      const dz = -0.16 * 2.3 * Math.sin(x * 1.9) * Math.sin(z * 2.3) + 0.08 * 3.3 * Math.cos(x * 4.1 + z * 3.3);
      return o.set(-dx, 1, -dz).normalize();
    }, extent: 400,
  }));
  const mosq = buildMosquito(buildMaterials(null, 0.5));
  const g = new GaitController(mosq, f, { rate: 1 });
  walk(mosq, g, 240);
  return { mosq, cam: frame(mosq, [-0.5, 0.30, 0.81], [0, 1, 0]) };
});

// --- vertical wall ------------------------------------------------------
add('wall', () => {
  const f = new SF.SurfaceField();
  f.add(new SF.Heightfield({ fn: () => 0, gradient: (x, z, o) => o.set(0, 1, 0), extent: 4000 }));
  f.add(new SF.BoxSurface({ min: [-3000, -2000, 0], max: [3000, 3000, 40], name: 'wall' }));
  const mosq = buildMosquito(buildMaterials(null, 0.5));
  const g = new GaitController(mosq, f, { rate: 1 });
  mosq.root.updateWorldMatrix(true, true);
  g.attach(new THREE.Vector3(0, 60, 0).addScaledVector(new THREE.Vector3(0, 0, 1), A.GAIT.bodyClearance),
    new THREE.Vector3(0, 0, 1), new THREE.Vector3(0, 1, 0));
  for (let i = 0; i < 700; i++) g.update(1 / 120, { speed: 18, accel: i < 60 ? 200 : 0 });
  return { mosq, cam: frame(mosq, [-0.55, 0.24, 0.80], [0, 1, 0]) };
});

// --- tank hull (cylinder), sideways -------------------------------------
add('hull', () => {
  const f = new SF.SurfaceField();
  f.add(new SF.Heightfield({ fn: () => 0, gradient: (x, z, o) => o.set(0, 1, 0), extent: 4000 }));
  f.add(new SF.CylinderShell({ x: 0, z: 0, y0: -200, y1: 2000, r: 300, name: 'hull' }));
  const mosq = buildMosquito(buildMaterials(null, 0.5));
  const g = new GaitController(mosq, f, { rate: 1 });
  mosq.root.updateWorldMatrix(true, true);
  g.attach(new THREE.Vector3(300, 60, 0).addScaledVector(new THREE.Vector3(1, 0, 0), A.GAIT.bodyClearance),
    new THREE.Vector3(1, 0, 0), new THREE.Vector3(0, 1, 0));
  for (let i = 0; i < 600; i++) g.update(1 / 120, { speed: 18, accel: i < 60 ? 200 : 0 });
  return { mosq, cam: frame(mosq, [-0.2, 0.25, 0.95], [0, 1, 0]) };
});

for (const { name, fn } of jobs) {
  const { mosq, cam } = fn();
  writePNG(`${out}/${name}.png`, render(mosq.root, cam, { colorOf }));
}
console.log(`wrote ${jobs.length} shots to ${out}`);

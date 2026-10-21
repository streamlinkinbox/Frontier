/* ============================================================================
   FRONTIER · LEVEL C — OVERGROWN BIO-LAB / CARNIVOROUS JUNGLE CANOPY
   Greybox ("shadow-box") prototype v1 — playable 3D level blockout.

   Design pillars:
   1) The lab is the antagonist: every system exists to stop / stall the car.
   2) Simplified "pack-driving": auto-drive always ON — steer, boost, brake, cut.
   3) Track-space physics (distance s + lateral offset) = rock-solid arcade feel.

   See LEVEL_DESIGN.md for the full zone-by-zone shadow-box breakdown.
   ============================================================================ */
import * as THREE from 'three';

/* ------------------------------- utils ---------------------------------- */
const $ = id => document.getElementById(id);
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const lerp = (a, b, t) => a + (b - a) * t;
const smooth = t => t * t * (3 - 2 * t);
const rand = (a, b) => a + Math.random() * (b - a);
const damp = (a, b, lambda, dt) => lerp(a, b, 1 - Math.exp(-lambda * dt));
function fmtTime(t) {
  if (!isFinite(t)) return '—';
  const m = Math.floor(t / 60), s = t - m * 60;
  return m + ':' + (s < 10 ? '0' : '') + s.toFixed(1);
}

/* --------------------------- level definition ---------------------------- */
/* Track runs along +s (world -Z). centreX(s) bends it gently like a vine.  */
const TRACK_LEN = 1440;
const FINISH_S = 1400;

const ZONES = [
  { name: 'AIRLOCK · DECON',    sub: 'Auto-drive engaged. Learn the wheel — nothing bites. Yet.',        from: 0,    to: 200,  half: 8.0 },
  { name: 'SPECIMEN HALL',      sub: 'Snapper Jaws line the tanks. Watch for the red telegraph.',        from: 200,  to: 460,  half: 7.5 },
  { name: 'CANOPY BRIDGE',      sub: 'Narrow deck. Vines overhead — boost through or get grabbed.',      from: 460,  to: 700,  half: 4.6 },
  { name: 'SPORE GREENHOUSE',   sub: 'Puffer country. Stay out of the yellow clouds.',                   from: 700,  to: 950,  half: 7.5 },
  { name: 'HEART CHAMBER',      sub: 'The Mother Maw. Thread her tentacles. Do not stop.',               from: 950,  to: 1180, half: 8.5 },
  { name: 'EXTRACTION SPRINT',  sub: 'Everything at once. The pad is close — burn the cell.',            from: 1180, to: 1440, half: 7.0 },
];

function centerX(s) { return 11 * Math.sin(s * 0.0038) + 6.5 * Math.sin(s * 0.0105 + 1.7); }
function halfAt(s) {
  for (let i = 0; i < ZONES.length; i++) {
    const z = ZONES[i];
    if (s < z.to || i === ZONES.length - 1) {
      const blend = 26;
      if (i > 0 && s < z.from + blend) return lerp(ZONES[i - 1].half, z.half, smooth(clamp((s - z.from) / blend, 0, 1)));
      return z.half;
    }
  }
  return 7;
}
function zoneAt(s) {
  for (let i = ZONES.length - 1; i >= 0; i--) if (s >= ZONES[i].from) return i;
  return 0;
}

/* scratch vectors */
const _tan = new THREE.Vector3(), _nor = new THREE.Vector3(), _pos = new THREE.Vector3();
function frameAt(s) {
  const dx = (centerX(s + 1) - centerX(s - 1)) / 2;
  _tan.set(dx, 0, -1).normalize();
  _nor.set(-_tan.z, 0, _tan.x);                    // right-hand side of travel
  return { tan: _tan, nor: _nor, yaw: Math.atan2(-_tan.x, -_tan.z) };
}
function trackPos(s, lat, y, out) {
  const f = frameAt(s);
  out.set(centerX(s) + f.nor.x * lat, y || 0, -s + f.nor.z * lat);
  return out;
}

/* Hazard / pickup placement table — the "shadow-box" in data form.
   type: snapper | grabber | puffer | sap | bramble | growbar | door |
         tentacle | pad | nitro | repair                                        */
const PLACEMENTS = [
  // Z1 · Airlock — teach: pads & steering only
  { type: 'pad', s: 60, lat: 0 }, { type: 'nitro', s: 120, lat: -3 }, { type: 'pad', s: 165, lat: 3 },
  // Z2 · Specimen Hall — teach snappers + sap
  { type: 'snapper', s: 250, side: -1 }, { type: 'sap', s: 300, lat: 2, r: 3.4 },
  { type: 'snapper', s: 342, side: 1 }, { type: 'nitro', s: 372, lat: 0 },
  { type: 'snapper', s: 402, side: -1 }, { type: 'sap', s: 430, lat: -2.5, r: 3.2 },
  { type: 'repair', s: 448, lat: 4 },
  // Z3 · Canopy Bridge — narrow, grabs & bramble
  { type: 'grabber', s: 512 }, { type: 'bramble', s: 560, side: -1 },
  { type: 'puffer', s: 600, side: 1 }, { type: 'nitro', s: 618, lat: 0 },
  { type: 'grabber', s: 648 }, { type: 'bramble', s: 680, side: 1 },
  // Z4 · Spore Greenhouse — puffers, sap slalom, first door
  { type: 'puffer', s: 732, side: -1 }, { type: 'sap', s: 762, lat: -2, r: 3.4 },
  { type: 'puffer', s: 792, side: 1 }, { type: 'snapper', s: 822, side: -1 },
  { type: 'sap', s: 848, lat: 3, r: 3.2 }, { type: 'sap', s: 868, lat: -3.4, r: 3.2 },
  { type: 'puffer', s: 892, side: -1 }, { type: 'snapper', s: 916, side: 1 },
  { type: 'repair', s: 928, lat: 0 }, { type: 'door', s: 945, phase: 0 },
  // Z5 · Heart Chamber — Mother Maw
  { type: 'repair', s: 968, lat: -4 }, { type: 'nitro', s: 988, lat: 4 },
  { type: 'tentacle', s: 1032, phase: 0 }, { type: 'tentacle', s: 1064, phase: 2.2 },
  { type: 'snapper', s: 1088, side: 1, big: true },
  { type: 'tentacle', s: 1098, phase: 4.1 },
  { type: 'grabber', s: 1132 }, { type: 'growbar', s: 1158, side: -1 },
  // Z6 · Extraction Sprint — everything, then glory
  { type: 'door', s: 1215, phase: 2.8 }, { type: 'snapper', s: 1252, side: 1 },
  { type: 'sap', s: 1278, lat: 0, r: 3.8 }, { type: 'puffer', s: 1302, side: -1 },
  { type: 'grabber', s: 1326 }, { type: 'snapper', s: 1350, side: -1 },
  { type: 'pad', s: 1368, lat: 0 }, { type: 'pad', s: 1386, lat: 0 },
];

/* ------------------------------ three setup ------------------------------ */
const container = $('game');
const renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
renderer.setSize(innerWidth, innerHeight);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
container.appendChild(renderer.domElement);

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x0a2b16);
scene.fog = new THREE.Fog(0x0a2413, 55, 235);

const camera = new THREE.PerspectiveCamera(62, innerWidth / innerHeight, 0.1, 600);
camera.position.set(0, 6, 14);

scene.add(new THREE.HemisphereLight(0xaee6b0, 0x16241a, 0.95));
const sun = new THREE.DirectionalLight(0xfff1c4, 1.9);
sun.castShadow = true;
sun.shadow.mapSize.set(2048, 2048);
sun.shadow.camera.left = -55; sun.shadow.camera.right = 55;
sun.shadow.camera.top = 55; sun.shadow.camera.bottom = -55;
sun.shadow.camera.far = 220; sun.shadow.bias = -0.0006;
scene.add(sun); scene.add(sun.target);
const fill = new THREE.DirectionalLight(0x6fb7ff, 0.35);
fill.position.set(-30, 40, 30); scene.add(fill);

/* ------------------------------ materials -------------------------------- */
const M = {
  labWhite: new THREE.MeshStandardMaterial({ color: 0xd9e0e6, roughness: .55, metalness: .25 }),
  labGrey: new THREE.MeshStandardMaterial({ color: 0x394047, roughness: .7, metalness: .3 }),
  labDark: new THREE.MeshStandardMaterial({ color: 0x1d2226, roughness: .8, metalness: .2 }),
  blueGlow: new THREE.MeshStandardMaterial({ color: 0x1f5eff, emissive: 0x1f5eff, emissiveIntensity: 1.6 }),
  amberGlow: new THREE.MeshStandardMaterial({ color: 0xffb020, emissive: 0xffb020, emissiveIntensity: 1.4 }),
  leaf: new THREE.MeshStandardMaterial({ color: 0x2fae5f, roughness: .95, flatShading: true }),
  leafDark: new THREE.MeshStandardMaterial({ color: 0x176b34, roughness: .95, flatShading: true }),
  stem: new THREE.MeshStandardMaterial({ color: 0x2e6b34, roughness: .9 }),
  vine: new THREE.MeshStandardMaterial({ color: 0x245c2c, roughness: .9 }),
  jawOut: new THREE.MeshStandardMaterial({ color: 0x3fae4a, roughness: .7, flatShading: true }),
  teeth: new THREE.MeshStandardMaterial({ color: 0xf4f1e4, roughness: .4 }),
  puffer: new THREE.MeshStandardMaterial({ color: 0xe8dfa0, roughness: .85, flatShading: true }),
  spore: new THREE.MeshStandardMaterial({ color: 0xd8e64a, transparent: true, opacity: .34, depthWrite: false, emissive: 0x8a9a1e, emissiveIntensity: .5 }),
  sap: new THREE.MeshStandardMaterial({ color: 0x9b4dff, transparent: true, opacity: .88, roughness: .12, emissive: 0x44157e, emissiveIntensity: .7 }),
  thorn: new THREE.MeshStandardMaterial({ color: 0x4a3521, roughness: .95, flatShading: true }),
  glass: new THREE.MeshStandardMaterial({ color: 0x9fd8ff, transparent: true, opacity: .22, roughness: .1, metalness: .1 }),
  nitro: new THREE.MeshStandardMaterial({ color: 0x35c8ff, emissive: 0x1f8fff, emissiveIntensity: 1.5, roughness: .3 }),
  repair: new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: .4 }),
  cross: new THREE.MeshStandardMaterial({ color: 0x2fe07a, emissive: 0x17b356, emissiveIntensity: 1.2 }),
  ground: new THREE.MeshStandardMaterial({ color: 0x0d2314, roughness: 1 }),
  apron: new THREE.MeshStandardMaterial({ color: 0x27313a, roughness: .95 }),
};

/* --------------------------- geometry helpers ---------------------------- */
function box(w, h, d, mat, x, y, z) {
  const m = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), mat);
  m.position.set(x || 0, y || 0, z || 0); m.castShadow = true; m.receiveShadow = true; return m;
}
function cyl(r1, r2, h, mat, x, y, z, seg) {
  const m = new THREE.Mesh(new THREE.CylinderGeometry(r1, r2, h, seg || 10), mat);
  m.position.set(x || 0, y || 0, z || 0); m.castShadow = true; m.receiveShadow = true; return m;
}
function sph(r, mat, x, y, z, sx, sy, sz) {
  const m = new THREE.Mesh(new THREE.SphereGeometry(r, 14, 10), mat);
  m.position.set(x || 0, y || 0, z || 0); m.scale.set(sx || 1, sy || 1, sz || 1);
  m.castShadow = true; m.receiveShadow = true; return m;
}
function cone(r, h, mat, x, y, z) {
  const m = new THREE.Mesh(new THREE.ConeGeometry(r, h, 7), mat);
  m.position.set(x || 0, y || 0, z || 0); m.castShadow = true; return m;
}
function blob(r, mat, x, y, z, s) {
  const m = new THREE.Mesh(new THREE.IcosahedronGeometry(r, 0), mat);
  m.position.set(x || 0, y || 0, z || 0); m.scale.setScalar(s || 1);
  m.rotation.set(rand(0, 3), rand(0, 3), rand(0, 3)); m.castShadow = true; return m;
}
/* stretch a unit cylinder between two world points (stems / vines) */
const _a = new THREE.Vector3(), _b = new THREE.Vector3(), _d = new THREE.Vector3(), _up = new THREE.Vector3(0, 1, 0), _q = new THREE.Quaternion();
function stemBetween(mesh, a, b) {
  _d.subVectors(b, a);
  const len = Math.max(0.01, _d.length());
  mesh.position.copy(a).addScaledVector(_d, 0.5);
  mesh.scale.set(1, len, 1);
  _q.setFromUnitVectors(_up, _d.normalize());
  mesh.quaternion.copy(_q);
}
function makeSign(text, sub, fg, bg, wide) {
  const W = wide ? 2048 : 1024, H = wide ? 200 : 256;
  const c = document.createElement('canvas'); c.width = W; c.height = H;
  const g = c.getContext('2d');
  g.fillStyle = bg; g.fillRect(0, 0, W, H);
  g.strokeStyle = fg; g.lineWidth = wide ? 8 : 10; g.strokeRect(12, 12, W - 24, H - 24);
  g.fillStyle = fg; g.textAlign = 'center';
  const fit = (txt, weight, px, family) => {
    let size = px;
    g.font = weight + ' ' + size + 'px ' + family;
    const w = g.measureText(txt).width;
    if (w > W * 0.92) { size = Math.floor(size * (W * 0.92) / w); g.font = weight + ' ' + size + 'px ' + family; }
    return size;
  };
  fit(text, '800', wide ? 96 : 96, 'system-ui, sans-serif');
  g.fillText(text, W / 2, sub ? H * 0.48 : H * 0.63);
  if (sub) { fit(sub, '600', wide ? 46 : 44, 'ui-monospace, monospace'); g.globalAlpha = .85; g.fillText(sub, W / 2, H * 0.8); }
  const tex = new THREE.CanvasTexture(c); tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 4;
  return new THREE.MeshBasicMaterial({ map: tex });
}

/* ------------------------------- the road -------------------------------- */
function roadTexture() {
  const c = document.createElement('canvas'); c.width = 256; c.height = 256;
  const g = c.getContext('2d');
  g.fillStyle = '#272d32'; g.fillRect(0, 0, 256, 256);
  g.fillStyle = '#2e353b'; g.fillRect(4, 4, 248, 248);
  g.strokeStyle = '#1b2024'; g.lineWidth = 3; g.strokeRect(2, 2, 252, 252);
  g.fillStyle = 'rgba(255,255,255,.16)'; g.fillRect(8, 0, 5, 256); g.fillRect(243, 0, 5, 256);
  g.fillStyle = '#1f5eff';                                   // Frontier-blue guidance dash
  if (g.roundRect) { g.beginPath(); g.roundRect(124, 34, 8, 76, 4); g.fill(); } else g.fillRect(124, 34, 8, 76);
  g.fillStyle = 'rgba(55,224,139,.055)';                     // faint moss creeping in
  for (let i = 0; i < 11; i++) { g.beginPath(); g.arc(rand(0, 256), rand(0, 256), rand(3, 9), 0, 7); g.fill(); }
  const t = new THREE.CanvasTexture(c);
  t.wrapS = t.wrapT = THREE.RepeatWrapping; t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = 4;
  return t;
}
function buildRibbon(y, extraFn, mat, vScale) {
  const step = 5, from = -40, to = TRACK_LEN + 30;
  const n = Math.floor((to - from) / step) + 1;
  const pos = new Float32Array(n * 2 * 3), uv = new Float32Array(n * 2 * 2), norA = new Float32Array(n * 2 * 3);
  const idx = [];
  const p = new THREE.Vector3();
  for (let i = 0; i < n; i++) {
    const s = from + i * step;
    const w = halfAt(clamp(s, 0, TRACK_LEN)) + extraFn(s);
    trackPos(s, -w, y, p); pos.set([p.x, p.y, p.z], i * 6);
    trackPos(s, w, y, p); pos.set([p.x, p.y, p.z], i * 6 + 3);
    uv.set([0, s / (vScale || 10)], i * 4); uv.set([1, s / (vScale || 10)], i * 4 + 2);
    norA.set([0, 1, 0], i * 6); norA.set([0, 1, 0], i * 6 + 3);
    if (i < n - 1) { const a = i * 2; idx.push(a, a + 1, a + 2, a + 1, a + 3, a + 2); }
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  geo.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
  geo.setAttribute('normal', new THREE.BufferAttribute(norA, 3));
  geo.setIndex(idx);
  const mesh = new THREE.Mesh(geo, mat);
  mesh.receiveShadow = true;
  return mesh;
}
const roadMat = new THREE.MeshStandardMaterial({ map: roadTexture(), roughness: .85 });
scene.add(buildRibbon(0, () => 0.7, roadMat, 10));
const inBridge = s => s > 455 && s < 705;
scene.add(buildRibbon(-0.06, s => inBridge(s) ? 1.6 : 8.5, M.apron, 14));

const jungleFloor = new THREE.Mesh(new THREE.PlaneGeometry(700, TRACK_LEN + 500), M.ground);
jungleFloor.rotation.x = -Math.PI / 2; jungleFloor.position.set(0, -7, -(TRACK_LEN / 2));
jungleFloor.receiveShadow = true; scene.add(jungleFloor);

/* ------------------------- lab shell & canopy decor ---------------------- */
const worldGroup = new THREE.Group(); scene.add(worldGroup);
{
  const p = new THREE.Vector3(), dummy = new THREE.Object3D();

  /* curbs (instanced) */
  const curbGeo = new THREE.BoxGeometry(0.65, 0.55, 5.7);
  const steps = [];
  for (let s = -12; s < TRACK_LEN + 12; s += 6) steps.push(s);
  const curbs = new THREE.InstancedMesh(curbGeo, M.labDark, steps.length * 2);
  let ci = 0;
  for (const s of steps) for (const side of [-1, 1]) {
    const f = frameAt(s);
    trackPos(s, side * (halfAt(clamp(s, 0, TRACK_LEN)) + 0.45), 0.27, p);
    dummy.position.copy(p); dummy.rotation.set(0, f.yaw, 0); dummy.scale.setScalar(1);
    dummy.updateMatrix(); curbs.setMatrixAt(ci++, dummy.matrix);
  }
  curbs.receiveShadow = true; worldGroup.add(curbs);

  /* wall panels + windows + bridge rails/piers */
  const winMat = new THREE.MeshStandardMaterial({ color: 0x8fd8ff, emissive: 0x2f7fd8, emissiveIntensity: .9 });
  for (let s = 0; s < TRACK_LEN; s += 24) {
    const f = frameAt(s + 12), half = halfAt(s + 12);
    for (const side of [-1, 1]) {
      if (inBridge(s + 12)) {
        const rail = new THREE.Group();
        trackPos(s + 12, side * (half + 1.1), 0, p); rail.position.copy(p); rail.rotation.y = f.yaw;
        rail.add(box(0.18, 0.18, 24, M.labGrey, 0, 1.15, 0));
        rail.add(box(0.12, 0.12, 24, M.labGrey, 0, 0.6, 0));
        for (let k = -10; k <= 10; k += 5) rail.add(box(0.14, 1.2, 0.14, M.labDark, 0, 0.6, k));
        const pier = box(1.4, 8, 1.4, M.labGrey, 0, -4, 0); rail.add(pier);
        worldGroup.add(rail);
      } else {
        const wall = new THREE.Group();
        trackPos(s + 12, side * (half + 6.8), 0, p); wall.position.copy(p); wall.rotation.y = f.yaw;
        const panel = box(0.9, 6.4, 23.4, Math.random() < .5 ? M.labWhite : M.labGrey, 0, 3.2, 0);
        wall.add(panel);
        if (Math.random() < .65) {
          const win = new THREE.Mesh(new THREE.PlaneGeometry(rand(6, 13), rand(1.4, 2.2)), winMat);
          win.position.set(-side * 0.48, rand(3.4, 4.6), rand(-5, 5));
          win.rotation.y = side > 0 ? -Math.PI / 2 : Math.PI / 2;
          wall.add(win);
        }
        if (Math.random() < .5) wall.add(blob(rand(1.6, 3), Math.random() < .5 ? M.leaf : M.leafDark, rand(-.5, .5), 6.6, rand(-9, 9), 1));
        if (Math.random() < .45) { const v = cyl(0.07, 0.1, rand(2, 4.4), M.vine, -side * 0.7, rand(3.6, 4.6), rand(-10, 10), 6); wall.add(v); }
        worldGroup.add(wall);
      }
    }
  }

  /* overhead canopy beams */
  for (let s = 15; s < TRACK_LEN; s += 30) {
    const f = frameAt(s), half = halfAt(s);
    trackPos(s, 0, 9.2, p);
    const beam = box((half + 7) * 2, 0.5, 0.9, M.labDark, p.x, p.y, p.z);
    beam.rotation.y = f.yaw; worldGroup.add(beam);
  }

  /* instanced canopy leaves + jungle bushes + hanging vines */
  const leafGeo = new THREE.IcosahedronGeometry(1, 0);
  const leafMat = new THREE.MeshStandardMaterial({ roughness: .95, flatShading: true });
  const NL = 420, leaves = new THREE.InstancedMesh(leafGeo, leafMat, NL);
  const col = new THREE.Color();
  for (let i = 0; i < NL; i++) {
    const s = rand(-10, TRACK_LEN + 10), half = halfAt(clamp(s, 0, TRACK_LEN));
    const overhead = Math.random() < .62;
    const lat = rand(-1, 1) * (overhead ? half + 7 : 0) + (overhead ? 0 : Math.sign(rand(-1, 1)) * (half + rand(7.6, 19)));
    const onApron = Math.abs(lat) <= half + 8.4 && !inBridge(s);
    const y = overhead ? rand(7.6, 12.6) : (onApron ? rand(0.2, 1.2) : rand(-6.3, -4.2)); // apron-top or jungle canopy below
    trackPos(s, lat, y, p);
    dummy.position.copy(p);
    dummy.rotation.set(rand(0, 3), rand(0, 3), rand(0, 3));
    dummy.scale.set(rand(1.1, 3.2), rand(0.55, 1.1), rand(1.1, 3.2));
    dummy.updateMatrix(); leaves.setMatrixAt(i, dummy.matrix);
    leaves.setColorAt(i, col.setHSL(rand(0.26, 0.38), rand(0.5, 0.8), rand(0.2, 0.42)));
  }
  worldGroup.add(leaves);

  const vineGeo = new THREE.CylinderGeometry(0.06, 0.09, 1, 5);
  const NV = 170, vines = new THREE.InstancedMesh(vineGeo, M.vine, NV);
  for (let i = 0; i < NV; i++) {
    const s = rand(0, TRACK_LEN), half = halfAt(s);
    const lat = Math.sign(rand(-1, 1)) * rand(half - 1.5, half + 5);
    const len = rand(2, 5.5);
    trackPos(s, lat, 9.4 - len / 2, p);
    dummy.position.copy(p); dummy.rotation.set(rand(-.06, .06), 0, rand(-.06, .06));
    dummy.scale.set(1, len, 1); dummy.updateMatrix(); vines.setMatrixAt(i, dummy.matrix);
  }
  worldGroup.add(vines);

  /* specimen tanks (Specimen Hall + Greenhouse) */
  for (const [s0, s1] of [[225, 450], [720, 935]]) {
    for (let s = s0; s < s1; s += rand(30, 42)) {
      const side = Math.random() < .5 ? -1 : 1, f = frameAt(s), half = halfAt(s);
      const tank = new THREE.Group();
      trackPos(s, side * (half + 3.4), 0, p); tank.position.copy(p); tank.rotation.y = f.yaw;
      tank.add(box(3.4, 0.5, 3.4, M.labDark, 0, 0.25, 0));
      const glass = cyl(1.55, 1.55, 4.6, M.glass, 0, 2.8, 0, 18); glass.castShadow = false; tank.add(glass);
      tank.add(blob(1.1, M.leaf, rand(-.3, .3), rand(1.6, 3), rand(-.3, .3), rand(.7, 1.2)));
      tank.add(cyl(0.16, 0.28, 2.6, M.stem, 0, 1.6, 0));
      tank.add(box(3.4, 0.35, 3.4, M.labGrey, 0, 5.25, 0));
      const lightDot = sph(0.13, M.amberGlow, 0, 5.5, 0); tank.add(lightDot);
      worldGroup.add(tank);
    }
  }

  /* gantries: start + finish */
  function gantry(s, title, sub, fg, bg) {
    const g = new THREE.Group(), f = frameAt(s), half = halfAt(s);
    trackPos(s, 0, 0, p); g.position.copy(p); g.rotation.y = f.yaw;
    for (const side of [-1, 1]) g.add(box(0.9, 7.6, 0.9, M.labGrey, side * (half + 1.4), 3.8, 0));
    g.add(box((half + 1.4) * 2 + 1, 1.9, 0.7, M.labDark, 0, 7.4, 0));
    const sign = new THREE.Mesh(new THREE.PlaneGeometry((half + 1.4) * 2, 1.7), makeSign(title, sub, fg, bg, true));
    sign.position.set(0, 7.4, 0.38); g.add(sign); // plane faces +Z local = back down-track toward the approaching driver
    worldGroup.add(g);
  }
  gantry(4, 'BIO-LAB · C', 'CARNIVOROUS CANOPY — AUTHORISED VEHICLES ONLY', '#8dff57', '#0a140d');
  gantry(FINISH_S, '✦ EXTRACTION ✦', 'DECONTAMINATION BEYOND THIS POINT', '#ffffff', '#1f5eff');
  /* finish pad */
  {
    const f = frameAt(FINISH_S + 8);
    trackPos(FINISH_S + 8, 0, 0.05, p);
    const pad = box(halfAt(FINISH_S) * 2, 0.1, 14, M.blueGlow, p.x, p.y, p.z);
    pad.rotation.y = f.yaw; pad.castShadow = false; worldGroup.add(pad);
  }
  /* zone entry signs */
  ZONES.forEach((z, i) => {
    if (i === 0) return;
    const s = z.from + 6, f = frameAt(s), half = halfAt(s);
    trackPos(s, half + 2.2, 0, p);
    const g = new THREE.Group(); g.position.copy(p); g.rotation.y = f.yaw;
    g.add(box(0.25, 4.6, 0.25, M.labDark, 0, 2.3, 0));
    const sign = new THREE.Mesh(new THREE.PlaneGeometry(5.4, 1.35), makeSign('ZONE ' + (i + 1), z.name, '#8dff57', '#0a140d'));
    sign.position.set(-0.2, 4.4, 0); sign.rotation.y = -Math.PI / 2 + 0.5; g.add(sign); // angled toward road + approaching driver
    worldGroup.add(g);
  });
}

/* ------------------------------ the vehicle ------------------------------ */
const car = new THREE.Group(); car.rotation.order = 'YXZ'; scene.add(car);
const carBodyMat = new THREE.MeshStandardMaterial({ color: 0xe8edf2, roughness: .35, metalness: .35, emissive: 0x000000 });
let wheels = [], frontAxleL, frontAxleR, brakeMat, vineWrap, cutterRing, cutterT = 99, headlight;
{
  const body = box(1.95, 0.55, 3.7, carBodyMat, 0, 0.62, 0); car.add(body);
  const nose = box(1.7, 0.34, 0.9, carBodyMat, 0, 0.5, -2.05); car.add(nose);
  const stripe = box(2.0, 0.2, 3.75, M.blueGlow, 0, 0.78, 0); stripe.castShadow = false; car.add(stripe);
  const cabin = box(1.55, 0.62, 1.7, new THREE.MeshStandardMaterial({ color: 0x12242e, roughness: .12, metalness: .4 }), 0, 1.18, 0.35);
  car.add(cabin);
  const spoiler = box(2.1, 0.09, 0.55, M.labDark, 0, 1.28, 1.95); car.add(spoiler);
  car.add(box(0.12, 0.5, 0.3, M.labDark, -0.85, 1.0, 1.95)); car.add(box(0.12, 0.5, 0.3, M.labDark, 0.85, 1.0, 1.95));
  /* front cutter bar — this thing mows plants for a living */
  const bar = box(2.4, 0.22, 0.35, M.labGrey, 0, 0.38, -2.55); car.add(bar);
  for (let i = -5; i <= 5; i++) { const t = cone(0.09, 0.42, M.teeth, i * 0.22, 0.38, -2.85); t.rotation.x = -Math.PI / 2; car.add(t); }
  /* headlights + spotlight */
  const hlMat = new THREE.MeshStandardMaterial({ color: 0xfff6cc, emissive: 0xffe98a, emissiveIntensity: 2.2 });
  car.add(box(0.34, 0.14, 0.06, hlMat, -0.62, 0.62, -2.5)); car.add(box(0.34, 0.14, 0.06, hlMat, 0.62, 0.62, -2.5));
  headlight = new THREE.SpotLight(0xfff2bb, 55, 62, 0.5, 0.55, 1.4);
  headlight.position.set(0, 0.8, -2.2); car.add(headlight);
  headlight.target.position.set(0, 0.2, -26); car.add(headlight.target);
  brakeMat = new THREE.MeshStandardMaterial({ color: 0xff2233, emissive: 0xff2233, emissiveIntensity: 0.5 });
  car.add(box(1.7, 0.13, 0.06, brakeMat, 0, 0.72, 1.87));
  /* wheels */
  const tireMat = new THREE.MeshStandardMaterial({ color: 0x14181c, roughness: .9 });
  const hubMat = new THREE.MeshStandardMaterial({ color: 0x9aa6b1, roughness: .3, metalness: .6 });
  function wheel(x, z, steerable) {
    const pivot = new THREE.Group(); pivot.position.set(x, 0.42, z);
    const w = new THREE.Group();
    const tire = cyl(0.42, 0.42, 0.34, tireMat, 0, 0, 0, 16); tire.rotation.z = Math.PI / 2; w.add(tire);
    const hub = cyl(0.2, 0.2, 0.37, hubMat, 0, 0, 0, 10); hub.rotation.z = Math.PI / 2; w.add(hub);
    pivot.add(w); car.add(pivot); wheels.push(w);
    return steerable ? pivot : null;
  }
  frontAxleL = wheel(-0.98, -1.45, true); frontAxleR = wheel(0.98, -1.45, true);
  wheel(-0.98, 1.45, false); wheel(0.98, 1.45, false);
  /* vine wrap (shown while grabbed) */
  vineWrap = new THREE.Group(); vineWrap.visible = false;
  for (let i = 0; i < 3; i++) {
    const ring = new THREE.Mesh(new THREE.TorusGeometry(1.25, 0.11, 7, 16), M.vine);
    ring.position.set(0, 0.75, -1 + i); ring.rotation.set(rand(0, .4), rand(0, .4), rand(0, .4));
    ring.scale.set(1, 0.72, 1); ring.castShadow = true; vineWrap.add(ring);
    vineWrap.add(blob(0.4, M.leaf, rand(-1, 1), 1.2, -1 + i, 1));
  }
  car.add(vineWrap);
  /* cutter pulse ring */
  cutterRing = new THREE.Mesh(new THREE.TorusGeometry(1, 0.055, 8, 44),
    new THREE.MeshBasicMaterial({ color: 0x8dff57, transparent: true, opacity: 0 }));
  cutterRing.rotation.x = Math.PI / 2; cutterRing.position.y = 0.5; car.add(cutterRing);
}

/* ------------------------- hazard factory -------------------------------- */
const hazards = [];
const _p = new THREE.Vector3(), _p2 = new THREE.Vector3();

function buildJawHead(k) {
  const head = new THREE.Group();
  const mouthMat = new THREE.MeshStandardMaterial({ color: 0xd81f3c, emissive: 0x7a0a1e, emissiveIntensity: .6, roughness: .6 });
  const lower = sph(0.95 * k, M.jawOut, 0, -0.16 * k, 0.35 * k, 1, 0.42, 1.25); head.add(lower);
  const mouth = sph(0.8 * k, mouthMat, 0, 0.02 * k, 0.42 * k, 0.94, 0.3, 1.15); mouth.castShadow = false; head.add(mouth);
  const jawTop = new THREE.Group(); jawTop.position.set(0, 0.12 * k, -0.45 * k); head.add(jawTop);
  const upper = sph(0.98 * k, M.jawOut, 0, 0.1 * k, 0.85 * k, 1, 0.45, 1.3); jawTop.add(upper);
  for (let i = -3; i <= 3; i++) {
    const t = cone(0.09 * k, 0.42 * k, M.teeth, i * 0.26 * k, -0.12 * k, 1.55 * k); t.rotation.x = Math.PI; jawTop.add(t);
    const b = cone(0.08 * k, 0.34 * k, M.teeth, i * 0.26 * k, 0.14 * k, 1.15 * k); head.add(b);
  }
  for (const sx of [-1, 1]) {  // eye stalks
    const stalk = cyl(0.05 * k, 0.07 * k, 0.7 * k, M.stem, sx * 0.5 * k, 0.75 * k, -0.2 * k, 6); head.add(stalk);
    head.add(sph(0.13 * k, M.amberGlow, sx * 0.5 * k, 1.12 * k, -0.2 * k));
  }
  head.add(blob(0.65 * k, M.leafDark, 0, 0.15 * k, -0.75 * k, 1));
  return { head, jawTop, mouthMat };
}

function thornBar(len, r, mat) {
  const g = new THREE.Group();
  const log = cyl(r, r * 1.15, len, mat || M.vine, 0, 0, 0, 8); log.rotation.z = Math.PI / 2; g.add(log);
  const n = Math.floor(len / 0.8);
  for (let i = 0; i < n; i++) {
    const t = cone(0.13, rand(0.5, 0.9), M.thorn, -len / 2 + (i + .5) * (len / n), rand(-.1, .3), rand(-.15, .15));
    t.rotation.set(rand(-.5, .5), 0, rand(-.5, .5)); g.add(t);
    if (Math.random() < .4) g.add(blob(rand(.3, .55), M.leafDark, -len / 2 + i * (len / n), 0.25, 0, 1));
  }
  return g;
}

function buildHazards() {
  for (const def of PLACEMENTS) {
    const h = Object.assign({ state: 'idle', t: 0, cd: 0 }, def);
    const half = halfAt(Math.min(h.s, TRACK_LEN));
    const grp = new THREE.Group();

    if (h.type === 'snapper') {
      const k = h.big ? 2.15 : 1;
      h.k = k; h.lat = h.side * (half + 2.6);
      trackPos(h.s, h.side * (half + 3.4), 0, _p);
      const base = new THREE.Group(); base.position.copy(_p);
      base.add(sph(1.1 * k, M.leafDark, 0, 0.5 * k, 0, 1, 0.6, 1));
      base.add(blob(0.9 * k, M.leaf, 0.7 * k, 0.4 * k, 0.5 * k, 1));
      base.add(blob(0.7 * k, M.leaf, -0.7 * k, 0.35 * k, -0.4 * k, 1));
      scene.add(base);
      h.basePos = _p.clone().setY(0.8 * k);
      trackPos(h.s, h.lat, (h.big ? 3.4 : 1.9), _p2);
      h.homePos = _p2.clone();
      const { head, jawTop, mouthMat } = buildJawHead(k);
      head.position.copy(h.homePos);
      trackPos(h.s, 0, 0.7, _p); head.lookAt(_p);
      scene.add(head);
      h.head = head; h.jawTop = jawTop; h.mouthMat = mouthMat; h.homeQuat = head.quaternion.clone();
      h.stemMesh = new THREE.Mesh(new THREE.CylinderGeometry(0.16 * k, 0.24 * k, 1, 8), M.stem);
      h.stemMesh.castShadow = true; scene.add(h.stemMesh);
      h.strikeLat = 0; h.hitDone = false;
      if (h.big) buildMaw(h);
    }
    else if (h.type === 'grabber') {
      const f = frameAt(h.s);
      trackPos(h.s, 0, 0, _p); grp.position.copy(_p); grp.rotation.y = f.yaw;
      for (const side of [-1, 1]) {
        grp.add(cyl(0.32, 0.5, 6.4, M.thorn, side * (half + 1.2), 3.2, 0, 8));
        grp.add(blob(rand(1.2, 1.7), M.leaf, side * (half + 1.2), 6.6, 0, 1));
      }
      const beam = cyl(0.17, 0.17, (half + 1.2) * 2, M.vine, 0, 6.1, 0, 8); beam.rotation.z = Math.PI / 2; grp.add(beam);
      h.tendrils = [];
      for (let i = -2; i <= 2; i++) {
        const pivot = new THREE.Group(); pivot.position.set(i * (half / 2.4), 6.1, 0);
        const td = cyl(0.07, 0.12, 3.1, M.vine, 0, -1.55, 0, 6); pivot.add(td);
        pivot.add(blob(0.34, M.leaf, 0, -3.1, 0, 1));
        grp.add(pivot); h.tendrils.push(pivot);
      }
      h.disabledT = 0; scene.add(grp); h.grp = grp;
    }
    else if (h.type === 'puffer') {
      h.lat = h.side * (half + 2.4);
      h.laneLat = h.side * rand(0.5, 2.6) * (Math.random() < .5 ? 1 : -0.4);
      const f = frameAt(h.s);
      trackPos(h.s, h.lat, 0, _p); grp.position.copy(_p); grp.rotation.y = f.yaw;
      grp.add(cyl(0.35, 0.55, 1.1, M.stem, 0, 0.55, 0));
      h.bulb = sph(1.25, M.puffer, 0, 1.75, 0, 1, 0.85, 1); grp.add(h.bulb);
      for (let i = 0; i < 6; i++) grp.add(sph(0.16, M.spore, rand(-.8, .8), rand(1.3, 2.4), rand(-.8, .8)));
      grp.add(blob(0.8, M.leafDark, 0.8, 0.3, 0.6, 1));
      scene.add(grp); h.grp = grp;
      /* the cloud it belches onto the road */
      h.cloud = new THREE.Group();
      for (let i = 0; i < 8; i++) {
        const puff = sph(rand(1.4, 2.6), M.spore, rand(-3, 3), rand(0, 1.4), rand(-2.6, 2.6));
        puff.castShadow = false; h.cloud.add(puff);
      }
      trackPos(h.s, h.laneLat, 1.1, _p); h.cloud.position.copy(_p);
      h.cloud.visible = false; scene.add(h.cloud);
      h.cycleT = rand(0, 5.4); h.cloudOn = false;
    }
    else if (h.type === 'sap') {
      const f = frameAt(h.s);
      trackPos(h.s, h.lat, 0.04, _p); grp.position.copy(_p); grp.rotation.y = f.yaw;
      const disc = new THREE.Mesh(new THREE.CircleGeometry(h.r, 26), M.sap);
      disc.rotation.x = -Math.PI / 2; disc.receiveShadow = true; grp.add(disc);
      h.disc = disc;
      for (let i = 0; i < 7; i++) {
        const a = rand(0, Math.PI * 2), rr = rand(h.r * .55, h.r * 1.05);
        const b = sph(rand(.3, .7), M.sap, Math.cos(a) * rr, 0.08, Math.sin(a) * rr, 1, 0.35, 1);
        b.castShadow = false; grp.add(b);
      }
      scene.add(grp); h.grp = grp;
    }
    else if (h.type === 'bramble' || h.type === 'growbar') {
      const cover = half + 0.5;                     // from one edge past centre
      h.latA = h.side < 0 ? -half - 0.5 : -0.4;
      h.latB = h.side < 0 ? 0.4 : half + 0.5;
      const w = h.latB - h.latA;
      const f = frameAt(h.s);
      trackPos(h.s, (h.latA + h.latB) / 2, 0, _p); grp.position.copy(_p); grp.rotation.y = f.yaw;
      const bar1 = thornBar(w, 0.55); bar1.position.y = 0.55; grp.add(bar1);
      const bar2 = thornBar(w * 0.94, 0.42); bar2.position.y = 1.25; bar2.rotation.y = 0.06; grp.add(bar2);
      scene.add(grp); h.grp = grp; h.phase = rand(0, 6);
      if (h.type === 'growbar') { h.baseA = h.latA; h.baseB = h.latB; }
    }
    else if (h.type === 'tentacle') {
      const f = frameAt(h.s);
      trackPos(h.s, 0, 0.55, _p); grp.position.copy(_p); grp.rotation.y = f.yaw;
      const flesh = new THREE.MeshStandardMaterial({ color: 0xb04472, roughness: .65, emissive: 0x4a1028, emissiveIntensity: .55, flatShading: true });
      const arm = thornBar(5.4, 0.52, flesh); grp.add(arm);
      const tip = sph(0.8, flesh, 2.7, 0.18, 0, 1, 0.85, 1); grp.add(tip);
      grp.add(sph(0.3, M.amberGlow, 2.95, 0.45, 0));
      h.arm = arm; scene.add(grp); h.grp = grp;
      h.latA = -2.7; h.latB = 2.7; h.centerLat = 0;
    }
    else if (h.type === 'door') {
      const f = frameAt(h.s);
      trackPos(h.s, 0, 0, _p); grp.position.copy(_p); grp.rotation.y = f.yaw;
      for (const side of [-1, 1]) grp.add(box(1.1, 8.4, 1.4, M.labGrey, side * (half + 1.1), 4.2, 0));
      const lintel = box((half + 1.1) * 2 + 1.2, 1.5, 1.4, M.labDark, 0, 8.2, 0); grp.add(lintel);
      const stripeTex = (() => {
        const c = document.createElement('canvas'); c.width = 256; c.height = 64;
        const g = c.getContext('2d'); g.fillStyle = '#ffb020'; g.fillRect(0, 0, 256, 64);
        g.fillStyle = '#141414'; for (let x = -64; x < 256; x += 42) { g.beginPath(); g.moveTo(x, 64); g.lineTo(x + 21, 0); g.lineTo(x + 42, 0); g.lineTo(x + 21, 64); g.fill(); }
        const t = new THREE.CanvasTexture(c); t.wrapS = THREE.RepeatWrapping; t.repeat.x = 3; return t;
      })();
      const stripe = new THREE.Mesh(new THREE.PlaneGeometry((half + 1.1) * 2, 0.9), new THREE.MeshBasicMaterial({ map: stripeTex }));
      stripe.position.set(0, 8.2, 0.72); grp.add(stripe); // faces approaching driver
      h.panel = box((half + 0.7) * 2, 7.4, 0.5, new THREE.MeshStandardMaterial({ color: 0x5a646e, roughness: .45, metalness: .55 }), 0, 11, 0);
      grp.add(h.panel);
      h.lights = [];
      for (const side of [-1, 1]) {
        const lm = new THREE.MeshStandardMaterial({ color: 0x37e08b, emissive: 0x37e08b, emissiveIntensity: 2 });
        const dot = sph(0.22, lm, side * (half + 1.1), 7.3, 0.85); dot.castShadow = false;
        grp.add(dot); h.lights.push(lm);
      }
      grp.add(blob(1.6, M.leaf, -(half + 1), 8.9, 0, 1)); grp.add(blob(1.3, M.leafDark, half + 0.6, 8.8, 0, 1));
      scene.add(grp); h.grp = grp; h.closed = 0;
    }
    else if (h.type === 'pad') {
      const f = frameAt(h.s);
      trackPos(h.s, h.lat, 0.05, _p); grp.position.copy(_p); grp.rotation.y = f.yaw;
      const texC = document.createElement('canvas'); texC.width = 128; texC.height = 256;
      const g = texC.getContext('2d'); g.fillStyle = '#0b2a8f'; g.fillRect(0, 0, 128, 256);
      g.fillStyle = '#7db4ff';
      for (let i = 0; i < 3; i++) { const y = 30 + i * 75; g.beginPath(); g.moveTo(24, y + 44); g.lineTo(64, y); g.lineTo(104, y + 44); g.lineTo(104, y + 70); g.lineTo(64, y + 26); g.lineTo(24, y + 70); g.fill(); }
      const tex = new THREE.CanvasTexture(texC); tex.colorSpace = THREE.SRGBColorSpace;
      h.padMat = new THREE.MeshStandardMaterial({ map: tex, emissive: 0x1f5eff, emissiveMap: tex, emissiveIntensity: 1.1 });
      const pad = new THREE.Mesh(new THREE.BoxGeometry(3.2, 0.1, 5.4), h.padMat);
      pad.receiveShadow = true; grp.add(pad);
      scene.add(grp); h.grp = grp;
    }
    else if (h.type === 'nitro' || h.type === 'repair') {
      const f = frameAt(h.s);
      trackPos(h.s, h.lat, 1.05, _p); grp.position.copy(_p); grp.rotation.y = f.yaw;
      if (h.type === 'nitro') {
        grp.add(cyl(0.42, 0.42, 1.15, M.nitro, 0, 0, 0, 14));
        grp.add(cyl(0.16, 0.16, 0.3, M.labGrey, 0, 0.72, 0, 8));
        const ring = new THREE.Mesh(new THREE.TorusGeometry(0.75, 0.05, 6, 24), M.blueGlow);
        ring.rotation.x = Math.PI / 2; ring.position.y = -0.75; grp.add(ring);
      } else {
        grp.add(box(1.15, 0.85, 0.5, M.repair, 0, 0, 0));
        grp.add(box(0.62, 0.2, 0.54, M.cross, 0, 0, 0)); grp.add(box(0.2, 0.62, 0.54, M.cross, 0, 0, 0));
        const ring = new THREE.Mesh(new THREE.TorusGeometry(0.85, 0.05, 6, 24), M.cross);
        ring.rotation.x = Math.PI / 2; ring.position.y = -0.75; grp.add(ring);
      }
      h.taken = false; h.baseY = 1.05;
      scene.add(grp); h.grp = grp;
    }
    hazards.push(h);
  }
}

/* the Mother Maw — huge body looming over the Heart Chamber */
function buildMaw(h) {
  const grp = new THREE.Group();
  const half = halfAt(h.s);
  trackPos(1062, half + 9, 0, _p); grp.position.copy(_p);
  grp.add(sph(5.2, M.leafDark, 0, 2.4, 0, 1, 0.62, 1));
  grp.add(sph(3.6, M.leaf, -2.6, 3.4, 1.8, 1, 0.7, 1));
  grp.add(sph(3.0, M.leaf, 2.8, 3.1, -2, 1, 0.7, 1));
  for (let i = 0; i < 7; i++) {
    const petal = cone(rand(1, 1.7), rand(3.5, 5.5), new THREE.MeshStandardMaterial({ color: 0xff2f7e, roughness: .6, flatShading: true }), rand(-4, 4), rand(4.5, 6.5), rand(-4, 4));
    petal.rotation.set(rand(-.7, .7), 0, rand(-.7, .7)); grp.add(petal);
  }
  for (let i = 0; i < 5; i++) grp.add(blob(rand(1, 2), M.leaf, rand(-6, 6), rand(.5, 1.5), rand(-6, 6), 1));
  scene.add(grp);
  const glow = new THREE.PointLight(0xff2f7e, 60, 55, 1.6);
  trackPos(1075, 3, 6, _p); glow.position.copy(_p); scene.add(glow);
}
buildHazards();

/* grab vine that drops onto the car */
const grabVine = new THREE.Mesh(new THREE.CylinderGeometry(0.09, 0.13, 1, 7), M.vine);
grabVine.visible = false; grabVine.castShadow = true; scene.add(grabVine);

/* ------------------------------ particles -------------------------------- */
const PMAX = 260;
const pGeo = new THREE.BufferGeometry();
const pPos = new Float32Array(PMAX * 3), pCol = new Float32Array(PMAX * 3);
const pVel = new Float32Array(PMAX * 3), pLife = new Float32Array(PMAX);
pGeo.setAttribute('position', new THREE.BufferAttribute(pPos, 3));
pGeo.setAttribute('color', new THREE.BufferAttribute(pCol, 3));
const pPoints = new THREE.Points(pGeo, new THREE.PointsMaterial({ size: 0.32, vertexColors: true, transparent: true, opacity: .95, depthWrite: false }));
pPoints.frustumCulled = false; scene.add(pPoints);
let pCursor = 0;
const _c = new THREE.Color();
function burst(pos, hex, n, spread, up) {
  _c.set(hex);
  for (let i = 0; i < n; i++) {
    const j = pCursor; pCursor = (pCursor + 1) % PMAX;
    pPos.set([pos.x + rand(-.4, .4), pos.y + rand(-.3, .5), pos.z + rand(-.4, .4)], j * 3);
    pVel.set([rand(-spread, spread), rand(1, up || 5), rand(-spread, spread)], j * 3);
    pCol.set([_c.r * rand(.6, 1.2), _c.g * rand(.6, 1.2), _c.b * rand(.6, 1.2)], j * 3);
    pLife[j] = rand(0.45, 0.95);
  }
}
function updateParticles(dt) {
  for (let j = 0; j < PMAX; j++) {
    if (pLife[j] <= 0) { pPos[j * 3 + 1] = -50; continue; }
    pLife[j] -= dt;
    pVel[j * 3 + 1] -= 9 * dt;
    pPos[j * 3] += pVel[j * 3] * dt; pPos[j * 3 + 1] += pVel[j * 3 + 1] * dt; pPos[j * 3 + 2] += pVel[j * 3 + 2] * dt;
  }
  pGeo.attributes.position.needsUpdate = true;
  pGeo.attributes.color.needsUpdate = true;
}
/* ambient drifting spores around the car */
const sporeGroup = new THREE.Group(); scene.add(sporeGroup);
{
  const N = 320, arr = new Float32Array(N * 3);
  for (let i = 0; i < N; i++) arr.set([rand(-34, 34), rand(0.2, 13), rand(-34, 34)], i * 3);
  const g = new THREE.BufferGeometry(); g.setAttribute('position', new THREE.BufferAttribute(arr, 3));
  const pts = new THREE.Points(g, new THREE.PointsMaterial({ color: 0xd8f79a, size: 0.13, transparent: true, opacity: .75, depthWrite: false }));
  pts.frustumCulled = false; sporeGroup.add(pts);
  sporeGroup.userData.update = (dt, t) => {
    const a = g.attributes.position.array;
    for (let i = 0; i < N; i++) {
      a[i * 3 + 1] -= dt * 0.55; a[i * 3] += Math.sin(t * 0.7 + i) * dt * 0.5;
      if (a[i * 3 + 1] < 0.1) a[i * 3 + 1] = 13;
      if (a[i * 3] > 34) a[i * 3] = -34; if (a[i * 3] < -34) a[i * 3] = 34;
    }
    g.attributes.position.needsUpdate = true;
  };
}

/* -------------------------------- audio ---------------------------------- */
const AU = {
  ctx: null, engGain: null, engOsc: null, engSub: null,
  init() {
    if (this.ctx) { this.ctx.resume(); return; }
    try {
      const C = window.AudioContext || window.webkitAudioContext;
      this.ctx = new C();
      this.master = this.ctx.createGain(); this.master.gain.value = 0.5; this.master.connect(this.ctx.destination);
      const o = this.ctx.createOscillator(); o.type = 'sawtooth'; o.frequency.value = 52;
      const sub = this.ctx.createOscillator(); sub.type = 'sine'; sub.frequency.value = 26;
      const f = this.ctx.createBiquadFilter(); f.type = 'lowpass'; f.frequency.value = 380; f.Q.value = 2;
      this.engGain = this.ctx.createGain(); this.engGain.gain.value = 0;
      o.connect(f); sub.connect(f); f.connect(this.engGain); this.engGain.connect(this.master);
      o.start(); sub.start(); this.engOsc = o; this.engSub = sub;
    } catch (e) { this.ctx = null; }
  },
  engine(norm, on) {
    if (!this.ctx) return;
    const t = this.ctx.currentTime;
    this.engOsc.frequency.setTargetAtTime(50 + norm * 115, t, 0.08);
    this.engSub.frequency.setTargetAtTime(25 + norm * 58, t, 0.08);
    this.engGain.gain.setTargetAtTime(on ? 0.05 + norm * 0.05 : 0, t, 0.1);
  },
  tone(freq, dur, type, gain, slideTo) {
    if (!this.ctx) return;
    const t = this.ctx.currentTime, o = this.ctx.createOscillator(), g = this.ctx.createGain();
    o.type = type || 'sine'; o.frequency.setValueAtTime(freq, t);
    if (slideTo) o.frequency.exponentialRampToValueAtTime(Math.max(20, slideTo), t + dur);
    g.gain.setValueAtTime(gain || 0.18, t); g.gain.exponentialRampToValueAtTime(0.001, t + dur);
    o.connect(g); g.connect(this.master); o.start(t); o.stop(t + dur + 0.02);
  },
  thud() { this.tone(120, 0.22, 'sine', 0.5, 38); },
  snap() { this.tone(900, 0.07, 'square', 0.22, 160); this.tone(180, 0.14, 'sine', 0.35, 60); },
  warn() { this.tone(660, 0.1, 'triangle', 0.14); },
  chop() { this.tone(rand(500, 800), 0.06, 'square', 0.16, 200); },
  free() { this.tone(320, 0.25, 'sawtooth', 0.2, 900); },
  pickup() { this.tone(880, 0.09, 'sine', 0.2); setTimeout(() => this.tone(1320, 0.14, 'sine', 0.2), 70); },
  pad() { this.tone(240, 0.32, 'sawtooth', 0.18, 1100); },
  grab() { this.tone(220, 0.4, 'sawtooth', 0.3, 55); },
  count(final) { this.tone(final ? 990 : 550, final ? 0.45 : 0.16, 'square', 0.22); },
  win() { [523, 659, 784, 1046].forEach((f, i) => setTimeout(() => this.tone(f, 0.3, 'triangle', 0.24), i * 120)); },
  lose() { [392, 330, 262, 196].forEach((f, i) => setTimeout(() => this.tone(f, 0.35, 'triangle', 0.22), i * 150)); },
};

/* ------------------------------- input ----------------------------------- */
const keys = { left: false, right: false, boost: false, brake: false };
addEventListener('keydown', e => {
  const k = e.code;
  if (['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Space'].includes(k)) e.preventDefault();
  if (k === 'ArrowLeft' || k === 'KeyA') keys.left = true;
  if (k === 'ArrowRight' || k === 'KeyD') keys.right = true;
  if (k === 'ArrowUp' || k === 'KeyW' || k === 'ShiftLeft' || k === 'ShiftRight') keys.boost = true;
  if (k === 'ArrowDown' || k === 'KeyS') keys.brake = true;
  if (k === 'Space' && !e.repeat) pulsePressed();
  if ((k === 'KeyP' || k === 'Escape')) togglePause();
  if (k === 'KeyR' && (state === 'race' || state === 'pause' || state === 'end')) startRun();
  if (k === 'Enter') { if (state === 'menu') startRun(); else if (state === 'end') startRun(); }
});
addEventListener('keyup', e => {
  const k = e.code;
  if (k === 'ArrowLeft' || k === 'KeyA') keys.left = false;
  if (k === 'ArrowRight' || k === 'KeyD') keys.right = false;
  if (k === 'ArrowUp' || k === 'KeyW' || k === 'ShiftLeft' || k === 'ShiftRight') keys.boost = false;
  if (k === 'ArrowDown' || k === 'KeyS') keys.brake = false;
});
function bindTouch(id, prop, isPulse) {
  const el = $(id);
  const on = e => { e.preventDefault(); el.classList.add('held'); if (isPulse) pulsePressed(); else keys[prop] = true; };
  const off = e => { e.preventDefault(); el.classList.remove('held'); if (!isPulse) keys[prop] = false; };
  el.addEventListener('pointerdown', on); el.addEventListener('pointerup', off);
  el.addEventListener('pointerleave', off); el.addEventListener('pointercancel', off);
}
bindTouch('tLeft', 'left'); bindTouch('tRight', 'right');
bindTouch('tBoost', 'boost'); bindTouch('tBrake', 'brake'); bindTouch('tPulse', null, true);

/* ----------------------------- game state -------------------------------- */
let state = 'menu';               // menu | count | race | pause | end
let prevState = 'race';
const P = {                        // player in track-space
  s: 2, lat: 0, latVel: 0, speed: 0,
  hull: 100, boost: 60,
  chokedT: 0, padT: 0, invulnT: 0, pulseCd: 0,
  grabbed: false, grabH: null, grabProg: 0, inSap: false, doorHeld: false,
};
const CRUISE = 27, BOOSTV = 42, BRAKEV = 9;
const GRAB_NEED = 6;
let raceTime = 0, countT = 0, lastCount = 4, zoneIdx = -1, shake = 0, menuS = 0, endOrbit = 0;
let stats = {};
let best = NaN;
try { best = parseFloat(localStorage.getItem('frontier_biolab_best') || 'NaN'); } catch (e) { /* storage unavailable */ }

function resetStats() { stats = { hits: 0, escapes: 0, nearMiss: 0, pickups: 0, burns: 0, stuns: 0 }; }
resetStats();

function resetRun() {
  P.s = 2; P.lat = 0; P.latVel = 0; P.speed = 0; P.hull = 100; P.boost = 60;
  P.chokedT = P.padT = P.invulnT = P.pulseCd = 0;
  P.grabbed = false; P.grabH = null; P.grabProg = 0; P.inSap = false; P.doorHeld = false;
  raceTime = 0; zoneIdx = -1; shake = 0; resetStats();
  vineWrap.visible = false; grabVine.visible = false;
  for (const h of hazards) {
    h.state = 'idle'; h.t = 0; h.cd = 0;
    if (h.type === 'snapper') { h.head.position.copy(h.homePos); h.hitDone = false; h.mouthMat.emissiveIntensity = .6; }
    if (h.type === 'puffer') { h.cycleT = rand(0, 5.4); h.cloudOn = false; h.cloud.visible = false; }
    if (h.type === 'grabber') h.disabledT = 0;
    if (h.type === 'nitro' || h.type === 'repair') { h.taken = false; h.grp.visible = true; }
    if (h.type === 'pad') h.padMat.emissiveIntensity = 1.1;
  }
  $('grabPrompt').classList.remove('on');
  $('grabFx').style.opacity = 0;
  $('sporeFx').style.opacity = 0; $('dmgFx').style.opacity = 0;
  el.banner.classList.remove('on'); el.toast.classList.remove('on');
  bannerT = 0; toastT = 0; hurtChipT = 0;
  updatePips();
}

/* --------------------------------- HUD ----------------------------------- */
const el = {
  hud: $('hud'), zone: $('zoneName'), timer: $('timer'), best: $('bestLine'),
  progFill: $('progFill'), progCar: $('progCar'), hullFill: $('hullFill'), hullVal: $('hullVal'),
  boostFill: $('boostFill'), boostVal: $('boostVal'), fx: $('fxLine'), vitals: $('vitals'),
  speed: $('speedVal'), gear: $('gear'), banner: $('banner'), bKick: $('bKick'), bTitle: $('bTitle'), bSub: $('bSub'),
  toast: $('toast'), cd: $('countDown'), grab: $('grabPrompt'), pips: $('pips'),
  chipSpore: $('chipSpore'), chipSap: $('chipSap'), chipDoor: $('chipDoor'), chipHurt: $('chipHurt'),
  sporeFx: $('sporeFx'), dmgFx: $('dmgFx'), grabFx: $('grabFx'),
};
for (const z of ZONES.slice(1)) {
  const t = document.createElement('div'); t.className = 'tick';
  t.style.left = (z.from / FINISH_S * 100) + '%'; $('progWrap').appendChild(t);
}
for (let i = 0; i < GRAB_NEED; i++) { const d = document.createElement('div'); d.className = 'pip'; el.pips.appendChild(d); }
function updatePips() { [...el.pips.children].forEach((d, i) => d.classList.toggle('hit', i < Math.floor(P.grabProg))); }

let bannerT = 0, toastT = 0, hurtChipT = 0;
function showBanner(kick, title, sub) {
  el.bKick.textContent = kick; el.bTitle.textContent = title; el.bSub.textContent = sub || '';
  el.banner.classList.add('on'); bannerT = 2.7;
}
function toast(msg, color) {
  el.toast.textContent = msg; el.toast.style.color = color || '#fff';
  el.toast.classList.add('on'); toastT = 1.7;
}

/* ----------------------------- core actions ------------------------------ */
function damage(amount, knockDir) {
  if (P.invulnT > 0 || state !== 'race') return;
  P.hull = Math.max(0, P.hull - amount);
  P.invulnT = 1.0; stats.hits++;
  shake = Math.min(1.2, shake + 0.55);
  el.dmgFx.style.opacity = 1; setTimeout(() => el.dmgFx.style.opacity = 0, 160);
  hurtChipT = 1.1;
  if (knockDir) P.latVel += knockDir * 7;
  trackPos(P.s, P.lat, 0.8, _p); burst(_p, 0xff4455, 12, 3.5, 4);
  AU.thud();
  if (P.hull <= 0) wreck();
}
function startGrab(h) {
  P.grabbed = true; P.grabH = h; P.grabProg = 0; P.speed *= 0.25;
  vineWrap.visible = true; grabVine.visible = true;
  el.grab.classList.add('on'); el.grabFx.style.opacity = 1;
  updatePips(); shake = Math.min(1.2, shake + 0.5);
  trackPos(P.s, P.lat, 1, _p); burst(_p, 0x37e08b, 16, 3, 5);
  AU.grab();
}
function endGrab(escaped) {
  if (P.grabH) P.grabH.disabledT = 8;
  P.grabbed = false; P.grabH = null;
  vineWrap.visible = false; grabVine.visible = false;
  el.grab.classList.remove('on'); el.grabFx.style.opacity = 0;
  if (escaped) {
    stats.escapes++; P.invulnT = Math.max(P.invulnT, 2); P.boost = Math.min(100, P.boost + 10);
    trackPos(P.s, P.lat, 1, _p); burst(_p, 0x8dff57, 26, 5, 7);
    toast('✂ CUT FREE! +BOOST', '#8dff57'); AU.free();
  }
}
function pulsePressed() {
  if (state !== 'race') return;
  if (P.grabbed) {
    P.grabProg = Math.min(GRAB_NEED, P.grabProg + 1);
    updatePips(); AU.chop(); cutterT = 0;
    trackPos(P.s, P.lat, 1, _p); burst(_p, 0x37e08b, 5, 2.5, 4);
    vineWrap.scale.setScalar(1 - P.grabProg / GRAB_NEED * 0.25);
    if (P.grabProg >= GRAB_NEED) endGrab(true);
    return;
  }
  if (P.pulseCd > 0) return;
  if (P.boost < 15) { toast('⚠ CUTTER NEEDS 15% BOOST', '#ffb020'); return; }
  P.boost -= 15; P.pulseCd = 1.2; cutterT = 0;
  P.chokedT = 0;
  let stunnedAny = false;
  for (const h of hazards) {
    if (h.type === 'snapper' && Math.abs(h.s - P.s) < 17 && h.state !== 'stunned') {
      h.state = 'stunned'; h.t = 3.5; stunnedAny = true; stats.stuns++;
    }
  }
  toast(stunnedAny ? '✂ CUTTER PULSE — PLANT STUNNED!' : '✂ CUTTER PULSE', '#8dff57');
  AU.free();
}
function barrierCheck(h, latA, latB, thick, dmg) {
  if (Math.abs(P.s - h.s) > thick) return;
  if (P.lat > latA - 0.85 && P.lat < latB + 0.85) {
    const toLeft = Math.abs(P.lat - (latA - 0.85)), toRight = Math.abs(P.lat - (latB + 0.85));
    const dir = toLeft < toRight ? -1 : 1;
    P.lat = dir < 0 ? latA - 1.05 : latB + 1.05;
    P.speed *= 0.3;
    damage(dmg, dir);
    trackPos(P.s, P.lat, 0.8, _p); burst(_p, 0x8a6b3a, 14, 3.5, 4);
  }
}

/* ------------------------------ flow control ----------------------------- */
function show(id) { for (const s of ['startScreen', 'pauseScreen', 'endScreen']) $(s).classList.toggle('on', s === id); }
function startRun() {
  AU.init();
  resetRun();
  show(null);
  el.hud.classList.add('on'); $('touch').classList.add('on'); $('pauseBtn').classList.add('on');
  state = 'count'; countT = 3.2; lastCount = 4;
  el.cd.textContent = '3'; el.cd.classList.add('on');
  trackPos(P.s - 9.6, 0, 4.7, camPos); camera.position.copy(camPos);
  trackPos(P.s + 15, 0, 1.05, camLook); camera.lookAt(camLook);
  camera.fov = 62; camera.updateProjectionMatrix();
  el.best.textContent = 'BEST ' + fmtTime(best);
}
function togglePause() {
  if (state === 'race' || state === 'count') {
    prevState = state; state = 'pause'; show('pauseScreen'); AU.engine(0, false);
  } else if (state === 'pause') {
    state = prevState; show(null);
  }
}
function finishRun() {
  state = 'end'; el.banner.classList.remove('on'); bannerT = 0;
  if (P.grabbed) endGrab(false);
  const t = raceTime;
  const isBest = !isFinite(best) || t < best;
  if (isBest) { best = t; try { localStorage.setItem('frontier_biolab_best', String(best)); } catch (e) { } }
  const medal = t <= 80 ? ['◆ GOLD', '#ffd24d'] : t <= 105 ? ['◆ SILVER', '#dfe6ee'] : ['◆ BRONZE', '#e0954d'];
  const score = Math.max(0, Math.round(12000 - t * 55 + P.hull * 25 + stats.nearMiss * 150 + stats.escapes * 120 + stats.burns * 150 + stats.stuns * 80 - stats.hits * 250));
  $('endTag').textContent = 'EXTRACTION REACHED' + (isBest ? ' · NEW BEST!' : '');
  $('endTitle').innerHTML = 'YOU OUTRAN<br /><em>THE GREEN</em>';
  const mb = $('medalBig'); mb.textContent = medal[0]; mb.style.color = medal[1];
  $('stTime').textContent = fmtTime(t); $('stHull').textContent = Math.round(P.hull) + '%';
  $('stHits').textContent = stats.hits; $('stEscapes').textContent = stats.escapes;
  $('stNear').textContent = stats.nearMiss; $('stPick').textContent = stats.pickups;
  $('stBest').textContent = fmtTime(best); $('stScore').textContent = score.toLocaleString();
  setTimeout(() => show('endScreen'), 900);
  AU.win(); AU.engine(0, false);
}
function wreck() {
  if (state !== 'race') return;
  state = 'end'; el.banner.classList.remove('on'); bannerT = 0;
  if (P.grabbed) endGrab(false);
  trackPos(P.s, P.lat, 1, _p); burst(_p, 0xff7722, 40, 6, 8); burst(_p, 0x555555, 24, 3, 6);
  shake = 1.4;
  $('endTag').textContent = 'VEHICLE LOST · ZONE ' + (zoneAt(P.s) + 1);
  $('endTitle').innerHTML = 'THE CANOPY<br /><em style="color:#ff3b47">KEEPS IT</em>';
  const mb = $('medalBig'); mb.textContent = '✖ WRECKED AT ' + Math.round(P.s / FINISH_S * 100) + '%'; mb.style.color = '#ff3b47';
  $('stTime').textContent = fmtTime(raceTime); $('stHull').textContent = '0%';
  $('stHits').textContent = stats.hits; $('stEscapes').textContent = stats.escapes;
  $('stNear').textContent = stats.nearMiss; $('stPick').textContent = stats.pickups;
  $('stBest').textContent = fmtTime(best); $('stScore').textContent = '—';
  setTimeout(() => show('endScreen'), 1200);
  AU.lose(); AU.engine(0, false);
}

$('startBtn').addEventListener('click', startRun);
$('againBtn').addEventListener('click', startRun);
$('restartBtn1').addEventListener('click', startRun);
$('resumeBtn').addEventListener('click', togglePause);
$('pauseBtn').addEventListener('click', togglePause);
$('menuBtn').addEventListener('click', () => {
  state = 'menu'; show('startScreen');
  el.hud.classList.remove('on'); $('touch').classList.remove('on'); $('pauseBtn').classList.remove('on');
  resetRun();
});

/* --------------------------- hazard update -------------------------------- */
function doorClosedness(h, time) {
  const t = (time + (h.phase || 0)) % 6;
  if (t < 2.6) return 0;
  if (t < 3.4) return smooth((t - 2.6) / 0.8);
  if (t < 5.2) return 1;
  return 1 - smooth((t - 5.2) / 0.8);
}

function updateHazards(dt, time, playing) {
  const half = halfAt(clamp(P.s, 0, TRACK_LEN));
  for (const h of hazards) {
    const ds = h.s - P.s;
    const near = Math.abs(ds) < 170;

    if (h.type === 'snapper') {
      if (!near) continue;
      h.t -= dt; if (h.cd > 0) h.cd -= dt;
      const k = h.k;
      const f = frameAt(h.s);
      if (h.state === 'idle') {
        trackPos(h.s, h.lat, h.homePos.y, _p);
        _p.x += Math.sin(time * 1.3 + h.s) * 0.25; _p.y += Math.sin(time * 1.7 + h.s * 2) * 0.18;
        h.head.position.lerp(_p, 1 - Math.exp(-4 * dt));
        h.jawTop.rotation.x = damp(h.jawTop.rotation.x, -0.38 - Math.sin(time * 2 + h.s) * 0.08, 5, dt);
        h.mouthMat.emissiveIntensity = damp(h.mouthMat.emissiveIntensity, 0.6, 5, dt);
        const lead = clamp(P.speed * (h.big ? 1.12 : 0.88), 11, 40);  // strike lands as the car arrives
        if (playing && h.cd <= 0 && ds > 1.5 && ds < lead) {
          h.state = 'tele'; h.t = h.big ? 0.95 : 0.72;
          h.strikeLat = clamp(P.lat + P.latVel * 0.25, -half + 1, half - 1);  // aim commits NOW — dodge after the tell
          AU.warn();
        }
      } else if (h.state === 'tele') {
        trackPos(h.s, h.lat + h.side * 0.9, h.homePos.y + 0.75 * k, _p);
        _p.x += rand(-.05, .05); _p.y += rand(-.05, .05);
        h.head.position.lerp(_p, 1 - Math.exp(-8 * dt));
        h.jawTop.rotation.x = damp(h.jawTop.rotation.x, -1.15, 8, dt);
        h.mouthMat.emissiveIntensity = 2.2 + Math.sin(time * 30) * 0.8;
        if (h.t <= 0) {
          h.state = 'strike'; h.t = 0.2; h.hitDone = false;
          h.fromPos = h.head.position.clone();
          trackPos(h.s, h.strikeLat, 0.75 * k, _p); h.strikePos = _p.clone();
        }
      } else if (h.state === 'strike') {
        const pr = 1 - Math.max(0, h.t) / 0.2;
        h.head.position.lerpVectors(h.fromPos, h.strikePos, pr * pr);
        h.jawTop.rotation.x = lerp(-1.15, -0.05, Math.max(0, pr - 0.45) / 0.55);
        if (pr > 0.55 && !h.hitDone) {
          h.hitDone = true; AU.snap();
          burst(h.strikePos, 0xd81f3c, 6, 2, 3);
          if (playing && !P.grabbed) {
            const latErr = Math.abs(P.lat - h.strikeLat), sErr = Math.abs(P.s - h.s);
            if (sErr < 2.8 && latErr < 1.8 * k) {
              P.speed *= 0.55;
              damage(h.big ? 17 : 12, h.side < 0 ? 1 : -1);
              toast(h.big ? '💥 THE MAW BIT YOU!' : '💥 SNAPPER BITE!', '#ff5b64');
            } else if (sErr < 4 && latErr < 3.6 * k && P.invulnT <= 0) {
              stats.nearMiss++; toast('⚡ NEAR MISS +150', '#ffd24d');
            }
          }
        }
        if (h.t <= 0) { h.state = 'recover'; h.t = 1.15; h.fromPos = h.head.position.clone(); }
      } else if (h.state === 'recover') {
        const pr = 1 - Math.max(0, h.t) / 1.15;
        h.head.position.lerpVectors(h.fromPos, h.homePos, smooth(pr));
        h.jawTop.rotation.x = damp(h.jawTop.rotation.x, -0.4, 4, dt);
        h.mouthMat.emissiveIntensity = damp(h.mouthMat.emissiveIntensity, 0.6, 4, dt);
        if (h.t <= 0) { h.state = 'idle'; h.cd = 1.6; }
      } else if (h.state === 'stunned') {
        trackPos(h.s, h.lat, h.homePos.y - 0.8 * k, _p);
        h.head.position.lerp(_p, 1 - Math.exp(-5 * dt));
        h.jawTop.rotation.x = damp(h.jawTop.rotation.x, -0.15, 5, dt);
        h.mouthMat.emissiveIntensity = 0.25;
        h.head.quaternion.copy(h.homeQuat); h.head.rotateZ(Math.sin(time * 6) * 0.09);
        if (h.t <= 0) { h.state = 'idle'; h.head.quaternion.copy(h.homeQuat); h.cd = 1; }
      }
      stemBetween(h.stemMesh, h.basePos, h.head.position);
    }
    else if (h.type === 'grabber') {
      if (h.disabledT > 0) h.disabledT -= dt;
      const retracted = h.disabledT > 0;
      if (near) for (let i = 0; i < h.tendrils.length; i++) {
        const td = h.tendrils[i];
        const target = retracted ? 0.18 : 1;
        td.scale.y = damp(td.scale.y, target, 5, dt);
        td.rotation.x = Math.sin(time * 1.6 + i * 1.3 + h.s) * 0.13;
        td.rotation.z = Math.cos(time * 1.3 + i) * 0.1;
      }
    }
    else if (h.type === 'puffer') {
      h.cycleT = (h.cycleT + dt) % 5.4;
      const swell = h.cycleT < 2.4 ? 1 + (h.cycleT / 2.4) * 0.3 : 1;
      if (near) h.bulb.scale.set(swell, swell * 0.85, swell);
      h.cloudOn = h.cycleT >= 2.4;
      if (h.cloudOn && !h.cloud.visible && near) {
        h.cloud.visible = true;
        trackPos(h.s, h.laneLat, 1.1, _p); burst(_p, 0xd8e64a, 10, 2, 2);
      }
      if (!h.cloudOn) h.cloud.visible = false;
      if (h.cloud.visible) {
        const life = (h.cycleT - 2.4) / 3;                       // 0..1
        const sc = 0.5 + Math.sin(Math.min(1, life) * Math.PI) * 0.85;
        h.cloud.scale.setScalar(sc);
        h.cloud.rotation.y += dt * 0.35;
        if (playing && !P.grabbed && Math.abs(P.s - h.s) < 6.5 * sc && Math.abs(P.lat - h.laneLat) < 4.6 * sc) {
          if (P.chokedT <= 0) toast('◉ SPORES IN THE INTAKE!', '#d8e64a');
          P.chokedT = Math.max(P.chokedT, 1.6);
        }
      }
    }
    else if (h.type === 'sap') {
      if (near && h.disc) { const p = 1 + Math.sin(time * 2.2 + h.s) * 0.05; h.disc.scale.set(p, p, 1); }
    }
    else if (h.type === 'bramble') {
      if (playing) barrierCheck(h, h.latA, h.latB, 1.7, 8);
    }
    else if (h.type === 'growbar') {
      const ext = 0.5 + 0.5 * Math.sin(time * 0.55 + (h.phase || 0));   // grows & shrinks
      const fullA = h.baseA, fullB = h.baseB;
      if (h.side < 0) { h.latA = fullA; h.latB = lerp(fullA + 2.2, fullB + half * 0.55, ext); }
      else { h.latB = fullB; h.latA = lerp(fullB - 2.2, fullA - half * 0.55, ext); }
      if (near) {
        const f = frameAt(h.s);
        trackPos(h.s, (h.latA + h.latB) / 2, 0, _p);
        h.grp.position.copy(_p); h.grp.rotation.y = f.yaw;
        h.grp.scale.x = (h.latB - h.latA) / (fullB - fullA);
      }
      if (playing) barrierCheck(h, h.latA, h.latB, 1.7, 8);
    }
    else if (h.type === 'tentacle') {
      h.centerLat = Math.sin(time * 0.6 + (h.phase || 0)) * (half - 3.1);
      if (near) {
        const f = frameAt(h.s);
        trackPos(h.s, h.centerLat, 0.55 + Math.sin(time * 2.4 + h.phase) * 0.12, _p);
        h.grp.position.copy(_p); h.grp.rotation.y = f.yaw;
        h.arm.rotation.y = Math.sin(time * 1.1 + h.phase) * 0.22;
      }
      if (playing) barrierCheck(h, h.centerLat - 2.7, h.centerLat + 2.7, 1.8, 10);
    }
    else if (h.type === 'door') {
      h.closed = doorClosedness(h, time);
      if (near) {
        h.panel.position.y = lerp(11.2, 3.7, h.closed);
        const colr = h.closed < 0.08 ? 0x37e08b : h.closed > 0.92 ? 0xff3b47 : 0xffb020;
        for (const lm of h.lights) { lm.color.setHex(colr); lm.emissive.setHex(colr); }
      }
    }
    else if (h.type === 'pad') {
      if (h.cd > 0) { h.cd -= dt; if (h.cd <= 0) h.padMat.emissiveIntensity = 1.1; }
      if (playing && h.cd <= 0 && Math.abs(P.s - h.s) < 2.7 && Math.abs(P.lat - h.lat) < 2.1) {
        h.cd = 4; h.padMat.emissiveIntensity = 0.25; P.padT = 1.15;
        toast('▲ BOOST PAD!', '#7db4ff'); AU.pad();
        trackPos(P.s, P.lat, 0.5, _p); burst(_p, 0x1f5eff, 12, 2, 3);
      }
    }
    else if (h.type === 'nitro' || h.type === 'repair') {
      if (!h.taken && near) {
        h.grp.rotation.y += dt * 1.8;
        h.grp.position.y = h.baseY + Math.sin(time * 2.4 + h.s) * 0.16;
        if (playing && Math.abs(P.s - h.s) < 2.3 && Math.abs(P.lat - h.lat) < 1.9) {
          h.taken = true; h.grp.visible = false; stats.pickups++;
          if (h.type === 'nitro') { P.boost = Math.min(100, P.boost + 45); toast('⚡ NITRO CELL +45', '#7db4ff'); burst(h.grp.position, 0x35c8ff, 14, 2.5, 4); }
          else { P.hull = Math.min(100, P.hull + 22); toast('✚ HULL PATCH +22', '#2fe07a'); burst(h.grp.position, 0x2fe07a, 14, 2.5, 4); }
          AU.pickup();
        }
      }
    }
  }
}

/* sap + grabber-trigger + door-blocking tests that affect motion */
function checkSap() {
  let inSap = false;
  for (const h of hazards) {
    if (h.type !== 'sap') continue;
    const ds = P.s - h.s, dl = P.lat - h.lat;
    if (ds * ds + dl * dl < h.r * h.r) { inSap = true; break; }
  }
  if (inSap && !P.inSap && state === 'race') toast('◉ SAP! BOOST TO POWER THROUGH', '#b46bff');
  P.inSap = inSap;
}
function checkGrabbers(prevS) {
  if (P.grabbed) return;
  const boostingNow = (keys.boost && P.boost > 1) || P.padT > 0;
  for (const h of hazards) {
    if (h.type !== 'grabber' || h.disabledT > 0) continue;
    if (prevS < h.s && P.s >= h.s) {
      if (boostingNow && P.speed > 30) {
        h.disabledT = 8; stats.burns++;
        trackPos(h.s, P.lat, 2, _p); burst(_p, 0xffa022, 22, 4, 5);
        toast('🔥 BURNED THROUGH THE VINE! +150', '#ffb020'); AU.free();
      } else {
        startGrab(h);
      }
      break;
    }
  }
}

/* ------------------------------ player update ---------------------------- */
function updatePlayer(dt, time) {
  const prevS = P.s;
  const half = halfAt(clamp(P.s, 0, TRACK_LEN));
  const steer = (keys.right ? 1 : 0) - (keys.left ? 1 : 0);

  /* timers */
  if (P.chokedT > 0) P.chokedT -= dt;
  if (P.padT > 0) P.padT -= dt;
  if (P.invulnT > 0) P.invulnT -= dt;
  if (P.pulseCd > 0) P.pulseCd -= dt;

  /* --- simplified PACK-DRIVING: auto-drive picks the target speed --- */
  let target = CRUISE;
  const wantBoost = keys.boost && P.boost > 0.5 && !P.grabbed;
  if (P.padT > 0) target = BOOSTV + 4;
  else if (wantBoost) { target = BOOSTV; P.boost = Math.max(0, P.boost - 21 * dt); }
  else if (keys.brake) target = BRAKEV;
  if (!wantBoost && P.padT <= 0) P.boost = Math.min(100, P.boost + 3.2 * dt);   // slow regen
  if (P.chokedT > 0) target *= 0.45;                                            // spores choke engine
  checkSap();
  if (P.inSap) target *= (wantBoost || P.padT > 0) ? 0.58 : 0.33;               // sap drag
  if (P.grabbed) {
    target = 0;
    P.hull = Math.max(0, P.hull - 1.1 * dt);                                   // it's digesting you…
    P.grabProg = Math.max(0, P.grabProg - dt * 0.35); updatePips();
    if (P.hull <= 0) { wreck(); }
  }
  const rate = target > P.speed ? 0.62 : 2.1;
  P.speed += (target - P.speed) * Math.min(1, dt * rate * (P.grabbed ? 6 : 1));
  if (P.speed < 0.02 && target === 0) P.speed = 0;

  /* steering — lateral velocity, sluggish in sap */
  const maxLat = 13.5 * clamp(P.speed / CRUISE, 0.3, 1.12) * (P.inSap ? 0.55 : 1);
  const steerRate = P.grabbed ? 0 : maxLat;
  P.latVel = damp(P.latVel, steer * steerRate, P.inSap ? 5 : 9, dt);
  P.lat += P.latVel * dt;
  const latLim = half - 1.15;
  if (P.lat > latLim) { if (P.latVel > 6) shake = Math.min(1, shake + .18); P.lat = latLim; P.latVel = Math.min(P.latVel, 0); }
  if (P.lat < -latLim) { if (P.latVel < -6) shake = Math.min(1, shake + .18); P.lat = -latLim; P.latVel = Math.max(P.latVel, 0); }

  /* advance + blast-door blocking (the lab literally holds you still) */
  let nextS = P.s + P.speed * dt;
  P.doorHeld = false;
  if (!P.grabbed) for (const h of hazards) {
    if (h.type !== 'door' || h.closed < 0.5) continue;
    const stopS = h.s - 3.1;
    if (P.s <= stopS + 0.01 && nextS > stopS) { nextS = stopS; P.speed = 0; P.doorHeld = true; }
  }
  P.s = nextS;
  checkGrabbers(prevS);

  /* zone banners */
  const zi = zoneAt(P.s);
  if (zi !== zoneIdx) {
    zoneIdx = zi;
    showBanner('ZONE ' + (zi + 1) + ' / 6', ZONES[zi].name, ZONES[zi].sub);
  }
  raceTime += dt;

  if (P.s >= FINISH_S && state === 'race') finishRun();

  /* audio */
  AU.engine(clamp(P.speed / BOOSTV, 0, 1) * (P.chokedT > 0 ? 0.55 : 1), true);
  return { wantBoost };
}

/* car + camera visuals */
const camPos = new THREE.Vector3(0, 6, 16), camLook = new THREE.Vector3();
function poseCar(dt, time, wantBoost) {
  const f = frameAt(P.s);
  trackPos(P.s, P.lat, 0, _p);
  car.position.copy(_p);
  car.position.y = Math.sin(time * 22) * 0.012 * clamp(P.speed / 20, 0, 1) + (P.grabbed ? Math.sin(time * 30) * 0.03 : 0);
  car.rotation.y = f.yaw - P.latVel * 0.017;
  car.rotation.z = damp(car.rotation.z, -P.latVel * 0.016, 8, dt);
  car.rotation.x = damp(car.rotation.x, clamp((wantBoost ? 0.02 : 0) + (keys.brake ? -0.02 : 0), -.05, .05), 6, dt);
  const spin = P.speed * dt / 0.42;
  for (const w of wheels) w.rotation.x -= spin;
  const steerVis = clamp(P.latVel * 0.028, -0.42, 0.42);
  frontAxleL.rotation.y = frontAxleR.rotation.y = -steerVis;
  brakeMat.emissiveIntensity = keys.brake || P.doorHeld ? 3.2 : 0.5;
  carBodyMat.emissive.setRGB(P.invulnT > 0 ? (Math.sin(time * 34) > 0 ? 0.35 : 0.05) : 0, 0.02, 0.02);
  /* cutter ring animation */
  cutterT += dt;
  if (cutterT < 0.55) {
    const pr = cutterT / 0.55;
    cutterRing.scale.set(1 + pr * 5.5, 1 + pr * 5.5, 1);
    cutterRing.material.opacity = (1 - pr) * 0.85;
  } else cutterRing.material.opacity = 0;
  /* grab vine from canopy to roof */
  if (P.grabbed && P.grabH) {
    trackPos(P.grabH.s, P.lat * 0.6, 6.1, _a);
    _b.copy(car.position).setY(1.35);
    stemBetween(grabVine, _a, _b);
    vineWrap.rotation.y += dt * 1.2;
  }
  /* hazard-adjacent props follow */
  sporeGroup.position.copy(car.position);
  sun.position.set(car.position.x + 34, 52, car.position.z + 22);
  sun.target.position.copy(car.position);
}
function updateCamera(dt, time, wantBoost) {
  if (state === 'menu') {
    menuS = (menuS + dt * 10.5) % 480;
    trackPos(menuS, Math.sin(time * 0.3) * 3, 5.4, _p);
    camera.position.lerp(_p, 1 - Math.exp(-2.2 * dt));
    trackPos(menuS + 22, 0, 1.2, _p); camera.lookAt(_p);
    return;
  }
  if (state === 'end') {
    endOrbit += dt * 0.45;
    _p.set(car.position.x + Math.sin(endOrbit) * 9, 3.6, car.position.z + Math.cos(endOrbit) * 9);
    camera.position.lerp(_p, 1 - Math.exp(-2.5 * dt));
    camera.lookAt(car.position.x, 1, car.position.z);
    return;
  }
  const back = 9.6 + clamp(P.speed / BOOSTV, 0, 1) * 1.6;
  trackPos(P.s - back, P.lat * 0.55, 4.7, _p);
  camPos.lerp(_p, 1 - Math.exp(-5.5 * dt));
  camera.position.copy(camPos);
  if (shake > 0.003) {
    camera.position.x += rand(-1, 1) * shake * 0.22;
    camera.position.y += rand(-1, 1) * shake * 0.18;
    shake = Math.max(0, shake - dt * 1.9);
  }
  trackPos(P.s + 15, P.lat * 0.8, 1.05, _p);
  camLook.lerp(_p, 1 - Math.exp(-8 * dt));
  camera.lookAt(camLook);
  const targetFov = (wantBoost || P.padT > 0) ? 72 : 62;
  camera.fov = damp(camera.fov, targetFov, 4, dt); camera.updateProjectionMatrix();
}

/* ------------------------------- HUD sync --------------------------------- */
function updateHUD(wantBoost) {
  el.zone.textContent = 'ZONE ' + (zoneAt(P.s) + 1) + ' · ' + ZONES[zoneAt(P.s)].name;
  el.timer.innerHTML = fmtTime(raceTime) + (isFinite(best) ? ' <small>/ ' + fmtTime(best) + '</small>' : '');
  const pr = clamp(P.s / FINISH_S, 0, 1);
  el.progFill.style.width = (pr * 100) + '%';
  el.progCar.style.left = 'calc(' + (pr * 100) + '% - 1px)';
  el.hullFill.style.width = P.hull + '%'; el.hullVal.textContent = Math.round(P.hull) + '%';
  el.boostFill.style.width = P.boost + '%'; el.boostVal.textContent = Math.round(P.boost) + '%';
  el.vitals.classList.toggle('low', P.hull < 30);
  el.speed.textContent = Math.round(P.speed * 3.6);
  let gear = 'AUTO-DRIVE', gc = '';
  if (P.grabbed) { gear = '!! GRABBED !!'; gc = 'stuck'; }
  else if (P.doorHeld) { gear = 'DOOR HOLD'; gc = 'stuck'; }
  else if (P.padT > 0 || wantBoost) { gear = 'BOOST ▲▲'; gc = 'boost'; }
  else if (keys.brake) { gear = 'BRAKE ▼'; gc = 'brake'; }
  else if (P.chokedT > 0) { gear = 'CHOKED'; gc = 'brake'; }
  else if (P.inSap) { gear = 'IN SAP'; gc = 'brake'; }
  el.gear.textContent = gear; el.gear.className = gc;
  el.fx.textContent = P.grabbed ? 'GRABBED' : P.inSap ? 'SAP DRAG' : P.chokedT > 0 ? 'SPORE CHOKE' : P.doorHeld ? 'HELD BY DOOR' : 'CLEAR';
  el.fx.style.color = P.grabbed ? '#ff5b64' : P.inSap ? '#b46bff' : P.chokedT > 0 ? '#d8e64a' : P.doorHeld ? '#ffb020' : '#37e08b';
  el.chipSpore.classList.toggle('on', P.chokedT > 0);
  el.chipSap.classList.toggle('on', P.inSap);
  el.chipDoor.classList.toggle('on', P.doorHeld);
  el.chipHurt.classList.toggle('on', hurtChipT > 0);
  el.sporeFx.style.opacity = P.chokedT > 0 ? 1 : 0;
}

/* ------------------------- shadow-box plan map ---------------------------- */
function drawMap() {
  const cv = $('mapCanvas'), g = cv.getContext('2d');
  const W = cv.width, H = cv.height;
  const x0 = 46, x1 = W - 46, midY = 168, latScale = 5.2;
  const X = s => x0 + (s / TRACK_LEN) * (x1 - x0);
  const Y = (s, lat) => midY + (centerX(s) + lat) * latScale;
  g.fillStyle = '#06120a'; g.fillRect(0, 0, W, H);
  /* zone bands */
  ZONES.forEach((z, i) => {
    g.fillStyle = i % 2 ? 'rgba(255,255,255,.045)' : 'rgba(55,224,139,.05)';
    g.fillRect(X(z.from), 24, X(z.to) - X(z.from), 300);
    g.strokeStyle = 'rgba(255,255,255,.14)'; g.strokeRect(X(z.from), 24, X(z.to) - X(z.from), 300);
    g.fillStyle = '#8dff57'; g.font = '800 19px ui-monospace, monospace'; g.textAlign = 'left';
    g.fillText('Z' + (i + 1), X(z.from) + 8, 52);
    g.fillStyle = 'rgba(255,255,255,.75)'; g.font = '600 12.5px ui-monospace, monospace';
    const words = z.name.replace(' · ', ' ').split(' ');
    words.forEach((w, j) => g.fillText(w, X(z.from) + 8, 342 + j * 17));
    g.fillStyle = 'rgba(255,255,255,.38)'; g.font = '11px ui-monospace, monospace';
    g.fillText('W' + (z.half * 2).toFixed(0) + 'm', X(z.from) + 8, 312);
  });
  /* road ribbon */
  g.beginPath();
  for (let s = 0; s <= TRACK_LEN; s += 8) { const x = X(s), y = Y(s, -halfAt(s)); s === 0 ? g.moveTo(x, y) : g.lineTo(x, y); }
  for (let s = TRACK_LEN; s >= 0; s -= 8) g.lineTo(X(s), Y(s, halfAt(s)));
  g.closePath(); g.fillStyle = '#232b31'; g.fill();
  g.strokeStyle = '#4d80ff'; g.lineWidth = 2; g.stroke();
  g.beginPath();
  for (let s = 0; s <= TRACK_LEN; s += 8) { const x = X(s), y = Y(s, 0); s === 0 ? g.moveTo(x, y) : g.lineTo(x, y); }
  g.strokeStyle = 'rgba(31,94,255,.65)'; g.setLineDash([7, 6]); g.stroke(); g.setLineDash([]);
  /* hazards */
  const COLORS = { snapper: '#ff3b47', grabber: '#37e08b', puffer: '#d8e64a', sap: '#b46bff', bramble: '#ffb020', growbar: '#ffb020', door: '#ffb020', tentacle: '#ff2f7e', pad: '#4d80ff', nitro: '#35c8ff', repair: '#2fe07a' };
  for (const h of hazards) {
    const half = halfAt(Math.min(h.s, TRACK_LEN));
    const lat = h.type === 'snapper' || h.type === 'puffer' ? h.side * (half + 2.4)
      : h.type === 'bramble' || h.type === 'growbar' ? h.side * (half * 0.5)
        : h.type === 'grabber' || h.type === 'door' || h.type === 'tentacle' ? 0 : (h.lat || 0);
    const x = X(h.s), y = Y(h.s, lat);
    g.fillStyle = COLORS[h.type] || '#fff';
    if (h.type === 'door' || h.type === 'grabber') { g.fillRect(x - 2, Y(h.s, -half), 4, Y(h.s, half) - Y(h.s, -half)); }
    else if (h.type === 'sap') { g.beginPath(); g.arc(x, y, h.r * latScale * .8, 0, 7); g.globalAlpha = .55; g.fill(); g.globalAlpha = 1; }
    else { g.beginPath(); g.arc(x, y, h.big ? 9 : 5.5, 0, 7); g.fill(); g.strokeStyle = 'rgba(0,0,0,.6)'; g.lineWidth = 1.5; g.stroke(); }
    if (h.big) { g.fillStyle = '#ff2f7e'; g.font = '800 13px ui-monospace, monospace'; g.textAlign = 'center'; g.fillText('★ MOTHER MAW', x, Y(h.s, -half) - 12); }
  }
  /* start / finish */
  g.font = '800 15px ui-monospace, monospace'; g.textAlign = 'center';
  g.fillStyle = '#fff'; g.fillText('▶ START · AIRLOCK', X(0) + 54, Y(0, 0) - 34);
  g.fillStyle = '#7db4ff'; g.fillText('🏁 EXTRACTION', X(FINISH_S) - 62, Y(FINISH_S, 0) - 36);
  g.fillStyle = 'rgba(255,255,255,.4)'; g.font = '11px ui-monospace, monospace';
  g.fillText('track length 1.44 km · greybox blockout — all art is placeholder primitives', W / 2, H - 10);
}
drawMap();

/* -------------------------------- main loop ------------------------------- */
const clock = new THREE.Clock();
resetRun();
trackPos(P.s, 0, 0, _p); car.position.copy(_p);
el.best.textContent = 'BEST ' + fmtTime(best);

let gameTime = 0;
function animate() {
  const dt = Math.min(0.05, clock.getDelta());
  if (state !== 'pause') gameTime += dt;
  const time = gameTime;
  let wantBoost = false;

  if (state === 'count') {
    countT -= dt;
    const n = Math.ceil(countT);
    if (n !== lastCount && n >= 1) { lastCount = n; el.cd.textContent = n; AU.count(false); }
    if (countT <= 0) {
      state = 'race'; el.cd.textContent = 'GO!'; AU.count(true);
      setTimeout(() => el.cd.classList.remove('on'), 550);
      showBanner('ZONE 1 / 6', ZONES[0].name, ZONES[0].sub); zoneIdx = 0;
    }
    P.speed = damp(P.speed, 0, 4, dt);
    poseCar(dt, time, false); updateCamera(dt, time, false);
    updateHazards(dt, time, false);
    updateHUD(false);
  }
  else if (state === 'race') {
    const r = updatePlayer(dt, time);
    wantBoost = r.wantBoost;
    updateHazards(dt, time, true);
    poseCar(dt, time, wantBoost); updateCamera(dt, time, wantBoost);
    updateHUD(wantBoost);
  }
  else if (state === 'menu') {
    P.s = 2; P.lat = 0;
    poseCar(dt, time, false); updateCamera(dt, time, false);
    /* animate plants vs. the fly-through camera position */
    const saveS = P.s; P.s = menuS; updateHazards(dt, time, false); P.s = saveS;
  }
  else if (state === 'end') {
    P.speed = damp(P.speed, 0, 0.8, dt);
    P.s += P.speed * dt;
    poseCar(dt, time, false); updateCamera(dt, time, false);
    updateHazards(dt, time, false);
    if (P.hull <= 0 && Math.random() < 0.3) { trackPos(P.s, P.lat, 1, _p); burst(_p, Math.random() < .5 ? 0xff7722 : 0x444444, 2, 1, 3); }
  }
  /* pause: freeze the world, keep rendering */

  if (bannerT > 0) { bannerT -= dt; if (bannerT <= 0) el.banner.classList.remove('on'); }
  if (toastT > 0) { toastT -= dt; if (toastT <= 0) el.toast.classList.remove('on'); }
  if (hurtChipT > 0) hurtChipT -= dt;

  sporeGroup.userData.update(dt, time);
  updateParticles(state === 'pause' ? 0 : dt);
  renderer.render(scene, camera);
}
renderer.setAnimationLoop(animate);

addEventListener('resize', () => {
  camera.aspect = innerWidth / innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight);
});
addEventListener('contextmenu', e => { if (state === 'race') e.preventDefault(); });

/* Debug / tuning hook — e.g. in devtools: __BIOLAB__.P.s = 1050 (jump to the Maw),
   __BIOLAB__.P.hull = 100, __BIOLAB__.hazards.filter(h=>h.type==='snapper')        */
window.__BIOLAB__ = { P, hazards, ZONES, startRun, get state() { return state; }, get time() { return raceTime; }, get gameTime() { return gameTime; } };

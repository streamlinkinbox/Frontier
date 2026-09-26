import * as THREE from "three";
import {
  createCar,
  createTank,
  createPlane,
  createBomb,
  createBunker,
  createNest,
  createMine,
  createTankMine,
  createHedgehog,
  createBarricade,
  createBelgianGate,
  createDragonTooth,
  createBarbedWire,
  createAtlanticWall,
  createTimberBridge,
  createSandbagStack,
  createSmokeColumn,
  createLandingCraft,
  createRevetment,
  createCrate,
  lambert,
} from "./models.js";

/* ------------------------------------------------------------------ */
/*  World constants — long, authored battlefield, not a straight road  */
/* ------------------------------------------------------------------ */

const MIN_X = -96;
const MAX_X = 96;
const MIN_Z = -220;
const MAX_Z = 1780;
const WALL_Z = 1700;
const FINISH_Z = 1660;
const START = { x: 0, z: 58, heading: 0 };

const ROAD_WIDTH = 7.2;
const ROAD_SHOULDER = 11.5;

// Snaking approach from Sword Beach to the Atlantic Wall (~1.6 km).
const ROAD_POINTS = [
  [0, 40],
  [8, 88],
  [-16, 140],
  [22, 198],
  [-12, 255],
  [28, 325],
  [-26, 395],
  [10, 455],
  [36, 530],
  [-22, 600],
  [16, 670],
  [-34, 745],
  [8, 820],
  [32, 900],
  [-20, 980],
  [24, 1065],
  [-28, 1150],
  [14, 1235],
  [-16, 1320],
  [18, 1405],
  [-8, 1490],
  [6, 1570],
  [0, 1640],
  [0, 1710],
];

const MOUNDS = [
  // dune belt
  { x: -38, z: 170, r: 24, h: 9.5 },
  { x: 42, z: 185, r: 26, h: 10.2 },
  { x: -48, z: 250, r: 22, h: 8.4 },
  { x: 50, z: 280, r: 28, h: 11.0 },
  { x: 8, z: 290, r: 14, h: 5.2 },
  { x: -52, z: 430, r: 30, h: 11.5 },
  { x: 48, z: 470, r: 26, h: 10.0 },
  { x: -40, z: 560, r: 20, h: 8.4 },
  { x: 44, z: 640, r: 24, h: 9.6 },
  { x: -58, z: 820, r: 32, h: 12.5 },
  { x: 56, z: 860, r: 28, h: 11.2 },
  { x: -46, z: 980, r: 22, h: 8.8 },
  { x: 50, z: 1100, r: 26, h: 10.4 },
  { x: -54, z: 1220, r: 30, h: 12.0 },
  { x: 46, z: 1280, r: 24, h: 9.5 },
  { x: -36, z: 1450, r: 22, h: 8.2 },
  { x: 40, z: 1480, r: 26, h: 10.0 },
  { x: 0, z: 1525, r: 16, h: 4.6 },
];

// Zig-zag fire trenches (line segments). Road flatten punches crossings.
const TRENCHES = [
  { ax: -70, az: 500, bx: -10, bz: 518, w: 3.6, d: 2.3 },
  { ax: -10, az: 518, bx: 20, bz: 498, w: 3.6, d: 2.3 },
  { ax: 20, az: 498, bx: 75, bz: 522, w: 3.6, d: 2.3 },
  { ax: -80, az: 700, bx: -20, bz: 722, w: 3.8, d: 2.4 },
  { ax: -20, az: 722, bx: 25, bz: 690, w: 3.8, d: 2.4 },
  { ax: 25, az: 690, bx: 80, bz: 718, w: 3.8, d: 2.4 },
  { ax: -75, az: 930, bx: 5, bz: 948, w: 3.4, d: 2.2 },
  { ax: 5, az: 948, bx: 78, bz: 922, w: 3.4, d: 2.2 },
];

const BUNKERS = [
  { x: -30, z: 360, rot: Math.PI + 0.28 },
  { x: 34, z: 378, rot: Math.PI - 0.22 },
  { x: 2, z: 455, rot: Math.PI },
  { x: -40, z: 790, rot: Math.PI + 0.42 },
  { x: 42, z: 825, rot: Math.PI - 0.38 },
  { x: -22, z: 1088, rot: Math.PI + 0.18 },
  { x: 28, z: 1110, rot: Math.PI - 0.2 },
  { x: 0, z: 1260, rot: Math.PI },
  { x: -44, z: 1475, rot: Math.PI + 0.32 },
  { x: 46, z: 1495, rot: Math.PI - 0.28 },
];

const CRATERS = [
  { x: 18, z: 1188, r: 7, d: 1.6 },
  { x: -22, z: 1220, r: 9, d: 1.9 },
  { x: 8, z: 1288, r: 6, d: 1.4 },
  { x: -14, z: 1355, r: 8, d: 1.7 },
  { x: 26, z: 1420, r: 7, d: 1.5 },
  { x: -6, z: 1510, r: 10, d: 2.0 },
  { x: 32, z: 240, r: 5, d: 1.1 },
  { x: -40, z: 900, r: 8, d: 1.6 },
];

const NESTS = [
  { x: -18, z: 240, rot: Math.PI + 0.1 },
  { x: 24, z: 268, rot: Math.PI - 0.15 },
  { x: -48, z: 610, rot: Math.PI + 0.5 },
  { x: 50, z: 655, rot: Math.PI - 0.45 },
  { x: -12, z: 880, rot: Math.PI },
  { x: 18, z: 990, rot: Math.PI - 0.1 },
  { x: -36, z: 1340, rot: Math.PI + 0.25 },
  { x: 38, z: 1375, rot: Math.PI - 0.2 },
  { x: 6, z: 580, rot: Math.PI + 0.08 },
  { x: -6, z: 1555, rot: Math.PI },
];

/* ------------------------------------------------------------------ */
/*  Math helpers                                                       */
/* ------------------------------------------------------------------ */

function lerp(a, b, t) {
  return a + (b - a) * t;
}
function clamp(v, a, b) {
  return Math.max(a, Math.min(b, v));
}
function smoothstep(e0, e1, x) {
  const t = clamp((x - e0) / (e1 - e0), 0, 1);
  return t * t * (3 - 2 * t);
}
function hash2(x, z) {
  const n = Math.sin(x * 127.1 + z * 311.7) * 43758.5453;
  return n - Math.floor(n);
}
function noise2(x, z) {
  const xi = Math.floor(x);
  const zi = Math.floor(z);
  const xf = x - xi;
  const zf = z - zi;
  const u = xf * xf * (3 - 2 * xf);
  const v = zf * zf * (3 - 2 * zf);
  return lerp(
    lerp(hash2(xi, zi), hash2(xi + 1, zi), u),
    lerp(hash2(xi, zi + 1), hash2(xi + 1, zi + 1), u),
    v
  );
}
function distToSeg(px, pz, ax, az, bx, bz) {
  const abx = bx - ax;
  const abz = bz - az;
  const apx = px - ax;
  const apz = pz - az;
  const ab2 = abx * abx + abz * abz;
  const t = ab2 > 1e-6 ? clamp((apx * abx + apz * abz) / ab2, 0, 1) : 0;
  const dx = px - (ax + abx * t);
  const dz = pz - (az + abz * t);
  return Math.hypot(dx, dz);
}

const roadSegs = [];
for (let i = 0; i < ROAD_POINTS.length - 1; i++) {
  const a = ROAD_POINTS[i];
  const b = ROAD_POINTS[i + 1];
  roadSegs.push({ ax: a[0], az: a[1], bx: b[0], bz: b[1] });
}

function distToRoad(x, z) {
  let d = 1e9;
  // Only test nearby segments by z for speed
  for (let i = 0; i < roadSegs.length; i++) {
    const s = roadSegs[i];
    const zMin = Math.min(s.az, s.bz) - 16;
    const zMax = Math.max(s.az, s.bz) + 16;
    if (z < zMin || z > zMax) continue;
    const dd = distToSeg(x, z, s.ax, s.az, s.bx, s.bz);
    if (dd < d) d = dd;
  }
  return d;
}

function nearestRoadPoint(x, z) {
  let best = 1e9;
  let px = x;
  let pz = z;
  for (const s of roadSegs) {
    const abx = s.bx - s.ax;
    const abz = s.bz - s.az;
    const ab2 = abx * abx + abz * abz;
    const t = ab2 > 1e-6 ? clamp(((x - s.ax) * abx + (z - s.az) * abz) / ab2, 0, 1) : 0;
    const rx = s.ax + abx * t;
    const rz = s.az + abz * t;
    const d = Math.hypot(x - rx, z - rz);
    if (d < best) {
      best = d;
      px = rx;
      pz = rz;
    }
  }
  return { x: px, z: pz, d: best };
}

function baseHeight(z) {
  if (z < -20) return -2.4 + z * 0.004;
  if (z < 0) return lerp(-2.5, -0.2, (z + 20) / 20);
  if (z < 95) return lerp(-0.12, 1.22, smoothstep(0, 95, z));
  return 1.22 + (z - 95) * 0.0012;
}

function heightAt(x, z) {
  let h = baseHeight(z);

  if (z > 70) {
    const n = noise2(x * 0.035, z * 0.035);
    h += (n - 0.5) * 0.85;
    h += (noise2(x * 0.09, z * 0.09) - 0.5) * 0.28;
  } else if (z > 10) {
    h += (noise2(x * 0.08, z * 0.08) - 0.5) * 0.18;
  }

  for (let i = 0; i < MOUNDS.length; i++) {
    const m = MOUNDS[i];
    const dx = x - m.x;
    const dz = z - m.z;
    const d2 = dx * dx + dz * dz;
    const r2 = m.r * m.r;
    if (d2 < r2 * 2.2) {
      const d = Math.sqrt(d2);
      const g = Math.exp((-d * d) / (m.r * m.r * 0.45));
      h += m.h * g;
    }
  }

  for (let i = 0; i < TRENCHES.length; i++) {
    const t = TRENCHES[i];
    const d = distToSeg(x, z, t.ax, t.az, t.bx, t.bz);
    if (d < t.w) {
      const k = 1 - d / t.w;
      const profile = k * k * (3 - 2 * k);
      h -= t.d * profile;
    }
  }

  for (let i = 0; i < CRATERS.length; i++) {
    const c = CRATERS[i];
    const dx = x - c.x;
    const dz = z - c.z;
    const d2 = dx * dx + dz * dz;
    if (d2 < c.r * c.r) {
      const d = Math.sqrt(d2);
      const k = 1 - d / c.r;
      h -= c.d * k * k * (0.35 + 0.65 * k);
    }
  }

  const dRoad = distToRoad(x, z);
  if (dRoad < ROAD_SHOULDER) {
    const roadY = baseHeight(z) + 0.06;
    const k = 1 - smoothstep(ROAD_WIDTH * 0.55, ROAD_SHOULDER, dRoad);
    h = lerp(h, roadY, k);
  }

  return h;
}

function colorAt(x, z, h) {
  const dRoad = distToRoad(x, z);
  let r, g, b;
  if (z < 8) {
    r = 0.42; g = 0.38; b = 0.28; // wet sand
  } else if (z < 110) {
    const t = smoothstep(8, 110, z);
    r = lerp(0.76, 0.55, t);
    g = lerp(0.68, 0.58, t);
    b = lerp(0.42, 0.32, t);
  } else {
    const n = noise2(x * 0.07, z * 0.07);
    r = 0.28 + n * 0.08;
    g = 0.36 + n * 0.10;
    b = 0.18 + n * 0.04;
  }
  if (dRoad < ROAD_SHOULDER) {
    const k = 1 - smoothstep(ROAD_WIDTH * 0.4, ROAD_SHOULDER, dRoad);
    r = lerp(r, 0.36, k);
    g = lerp(g, 0.28, k);
    b = lerp(b, 0.20, k);
    if (dRoad < 1.1) {
      r *= 0.85; g *= 0.85; b *= 0.85;
    }
  }
  if (h < baseHeight(z) - 0.8) {
    r = 0.24; g = 0.18; b = 0.12;
  }
  if (z < 0) {
    r = 0.22; g = 0.28; b = 0.22;
  }
  return [r, g, b];
}

function slopeAt(x, z) {
  const e = 0.7;
  const hL = heightAt(x - e, z);
  const hR = heightAt(x + e, z);
  const hD = heightAt(x, z - e);
  const hU = heightAt(x, z + e);
  return { dx: (hR - hL) / (2 * e), dz: (hU - hD) / (2 * e) };
}

/* ------------------------------------------------------------------ */
/*  Renderer / scene                                                   */
/* ------------------------------------------------------------------ */

const canvasHolder = document.body;
const renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: "high-performance" });
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
renderer.setSize(innerWidth, innerHeight);
renderer.setClearColor(0x8a9094, 1);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.05;
canvasHolder.prepend(renderer.domElement);
renderer.domElement.id = "view";

const scene = new THREE.Scene();
scene.fog = new THREE.FogExp2(0x8e9490, 0.0034);

const camera = new THREE.PerspectiveCamera(62, innerWidth / innerHeight, 0.12, 900);
camera.position.set(0, 18, 20);

const hemi = new THREE.HemisphereLight(0xb8c0c4, 0x4a4032, 0.72);
scene.add(hemi);
const sun = new THREE.DirectionalLight(0xddd6c4, 0.95);
sun.position.set(60, 90, 40);
sun.castShadow = true;
sun.shadow.mapSize.set(2048, 2048);
sun.shadow.camera.near = 4;
sun.shadow.camera.far = 220;
sun.shadow.camera.left = -50;
sun.shadow.camera.right = 50;
sun.shadow.camera.top = 50;
sun.shadow.camera.bottom = -50;
sun.shadow.bias = -0.0007;
scene.add(sun);
scene.add(sun.target);
const fill = new THREE.DirectionalLight(0x8aa0b0, 0.18);
fill.position.set(-40, 30, -20);
scene.add(fill);

/* ------------------------------------------------------------------ */
/*  Terrain                                                            */
/* ------------------------------------------------------------------ */

function buildTerrain() {
  const width = MAX_X - MIN_X + 20;
  const depth = MAX_Z - MIN_Z;
  const segX = 92;
  const segZ = 430;
  const geo = new THREE.PlaneGeometry(width, depth, segX, segZ);
  geo.rotateX(-Math.PI / 2);
  const pos = geo.attributes.position;
  const colors = new Float32Array(pos.count * 3);
  const cx = (MIN_X + MAX_X) / 2;
  const cz = (MIN_Z + MAX_Z) / 2;
  for (let i = 0; i < pos.count; i++) {
    const x = pos.getX(i) + cx;
    const z = pos.getZ(i) + cz;
    const y = heightAt(x, z);
    pos.setY(i, y);
    const c = colorAt(x, z, y);
    colors[i * 3] = c[0];
    colors[i * 3 + 1] = c[1];
    colors[i * 3 + 2] = c[2];
  }
  geo.setAttribute("color", new THREE.BufferAttribute(colors, 3));
  geo.computeVertexNormals();
  const mat = new THREE.MeshLambertMaterial({ vertexColors: true, flatShading: true });
  const mesh = new THREE.Mesh(geo, mat);
  mesh.position.set(cx, 0, cz);
  mesh.receiveShadow = true;
  mesh.castShadow = false;
  return mesh;
}

scene.add(buildTerrain());
scene.add(buildRoadRibbon());

function buildRoadRibbon() {
  const curve = new THREE.CatmullRomCurve3(
    ROAD_POINTS.map(([x, z]) => new THREE.Vector3(x, 0, z)),
    false,
    "catmullrom",
    0.12
  );
  const N = 320;
  const hw = 4.8;
  const pts = curve.getSpacedPoints(N);
  const positions = [];
  const colors = [];
  const indices = [];
  for (let i = 0; i <= N; i++) {
    const p = pts[i];
    const prev = pts[Math.max(0, i - 1)];
    const next = pts[Math.min(N, i + 1)];
    const tx = next.x - prev.x;
    const tz = next.z - prev.z;
    const len = Math.hypot(tx, tz) || 1;
    const px = tz / len;
    const pz = -tx / len;
    const lx = p.x + px * hw;
    const lz = p.z + pz * hw;
    const rx = p.x - px * hw;
    const rz = p.z - pz * hw;
    positions.push(lx, heightAt(lx, lz) + 0.05, lz);
    positions.push(rx, heightAt(rx, rz) + 0.05, rz);
    const c = i % 18 < 2 ? 0.22 : 0.34;
    colors.push(c * 1.05, c * 0.82, c * 0.58, c * 1.05, c * 0.82, c * 0.58);
  }
  for (let i = 0; i < N; i++) {
    const a = i * 2;
    indices.push(a, a + 2, a + 1, a + 1, a + 2, a + 3);
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
  geo.setAttribute("color", new THREE.Float32BufferAttribute(colors, 3));
  geo.setIndex(indices);
  geo.computeVertexNormals();
  const mesh = new THREE.Mesh(
    geo,
    new THREE.MeshLambertMaterial({ vertexColors: true, flatShading: true })
  );
  mesh.receiveShadow = true;
  return mesh;
}

/* ------------------------------------------------------------------ */
/*  Ocean / rising tide                                                */
/* ------------------------------------------------------------------ */

const waterUniforms = {
  uTime: { value: 0 },
  uFoam: { value: 0.2 },
};
const waterMat = new THREE.ShaderMaterial({
  uniforms: waterUniforms,
  transparent: true,
  depthWrite: false,
  vertexShader: `
    uniform float uTime;
    varying float vWave;
    varying vec3 vPos;
    void main() {
      vec3 p = position;
      float w1 = sin(p.x * 0.11 + uTime * 1.3) * 0.32;
      float w2 = sin(p.z * 0.09 + uTime * 0.85) * 0.24;
      float w3 = sin((p.x + p.z) * 0.17 + uTime * 1.7) * 0.12;
      p.y += w1 + w2 + w3;
      vWave = w1 + w2;
      vPos = p;
      gl_Position = projectionMatrix * modelViewMatrix * vec4(p, 1.0);
    }
  `,
  fragmentShader: `
    varying float vWave;
    varying vec3 vPos;
    uniform float uFoam;
    void main() {
      vec3 deep = vec3(0.07, 0.18, 0.22);
      vec3 mid = vec3(0.18, 0.38, 0.40);
      vec3 foam = vec3(0.78, 0.84, 0.80);
      float h = clamp(vWave * 0.7 + 0.4, 0.0, 1.0);
      vec3 col = mix(deep, mid, h);
      float edge = smoothstep(uFoam - 8.0, uFoam, vPos.z);
      col = mix(col, foam, edge * 0.55);
      float alpha = 0.86;
      gl_FragColor = vec4(col, alpha);
    }
  `,
});
const waterGeo = new THREE.PlaneGeometry(280, 520, 44, 52);
waterGeo.rotateX(-Math.PI / 2);
const water = new THREE.Mesh(waterGeo, waterMat);
water.position.set(0, 0.15, -254);
scene.add(water);

const foamMat = new THREE.MeshBasicMaterial({
  color: 0xd8e0dc,
  transparent: true,
  opacity: 0.55,
  depthWrite: false,
});
const foam = new THREE.Mesh(new THREE.PlaneGeometry(260, 10), foamMat);
foam.rotation.x = -Math.PI / 2;
foam.position.set(0, 0.28, 6);
scene.add(foam);

const tide = { y: 0.2, front: 6, maxFront: 420 };

/* ------------------------------------------------------------------ */
/*  Sky                                                                */
/* ------------------------------------------------------------------ */

{
  const skyGeo = new THREE.SphereGeometry(700, 16, 10);
  const skyMat = new THREE.ShaderMaterial({
    side: THREE.BackSide,
    depthWrite: false,
    uniforms: {},
    vertexShader: `varying vec3 vP; void main(){ vP = position; gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.0); }`,
    fragmentShader: `
      varying vec3 vP;
      void main(){
        float h = normalize(vP).y;
        vec3 low = vec3(0.55, 0.56, 0.54);
        vec3 high = vec3(0.42, 0.48, 0.54);
        gl_FragColor = vec4(mix(low, high, clamp(h*1.2,0.0,1.0)), 1.0);
      }`,
  });
  scene.add(new THREE.Mesh(skyGeo, skyMat));
}

/* ------------------------------------------------------------------ */
/*  Gameplay collections                                               */
/* ------------------------------------------------------------------ */

const solids = []; // {x,z,r, mesh}
const mines = [];
const tankMines = [];
const wires = [];
const turrets = [];
const tanks = [];
const planes = [];
const tracers = [];
const bombs = [];
const particles = [];
const smokes = [];
const lightsFlash = [];

const tmp = new THREE.Vector3();
const tmp2 = new THREE.Vector3();
const tmpM = new THREE.Matrix4();
const tmpQ = new THREE.Quaternion();

function addSolid(mesh, x, y, z, r, rotY = 0) {
  mesh.position.set(x, y, z);
  mesh.rotation.y = rotY;
  scene.add(mesh);
  solids.push({ x, z, r, mesh });
  return mesh;
}

function placeOnGround(mesh, x, z, rotY = 0, yOff = 0) {
  const y = heightAt(x, z) + yOff;
  mesh.position.set(x, y, z);
  mesh.rotation.y = rotY;
  scene.add(mesh);
  return y;
}

/* ------------------------------------------------------------------ */
/*  Populate battlefield                                               */
/* ------------------------------------------------------------------ */

function scatter(z0, z1, count, fn, minRoad = ROAD_WIDTH + 1.4) {
  let n = 0;
  let guard = 0;
  while (n < count && guard < count * 14) {
    guard++;
    const x = lerp(MIN_X + 8, MAX_X - 8, Math.random());
    const z = lerp(z0, z1, Math.random());
    if (distToRoad(x, z) < minRoad) continue;
    fn(x, z);
    n++;
  }
}

// Beach obstacle belts — rows of hedgehogs and Belgian gates, gap on the track
function hedgehogRow(z, spacing, gap) {
  for (let x = -84; x <= 84; x += spacing) {
    if (Math.abs(x) < gap) continue;
    const zz = z + (hash2(x, z) - 0.5) * 2.2;
    const h = createHedgehog();
    placeOnGround(h, x, zz, hash2(x, zz) * Math.PI);
    solids.push({ x, z: zz, r: 1.25, mesh: h });
  }
}
hedgehogRow(34, 7.5, 9);
hedgehogRow(48, 8.2, 10);
hedgehogRow(62, 7.2, 10);

for (let x = -80; x <= 80; x += 9) {
  if (Math.abs(x) < 11) continue;
  const z = 108 + (hash2(x, 9) - 0.5) * 4;
  const g = createBelgianGate();
  placeOnGround(g, x, z, (hash2(x, 2) - 0.5) * 0.2);
  solids.push({ x, z, r: 1.7, mesh: g });
}

// Beached landing craft on the sand
for (const lc of [
  { x: -28, z: 22, rot: 0.35 },
  { x: 34, z: 18, rot: -0.55 },
  { x: -8, z: 12, rot: 0.12 },
]) {
  const m = createLandingCraft();
  placeOnGround(m, lc.x, lc.z, lc.rot, -0.2);
  solids.push({ x: lc.x, z: lc.z, r: 4.0, mesh: m });
}

// Barbed wire belts with a gap on the road — beach and inland belts
function wireRow(z, gap) {
  for (let x = -88; x < 88; x += 9.2) {
    if (Math.abs(x) < gap) continue;
    const w = createBarbedWire(8.6);
    const zz = z + (hash2(x, z) - 0.5) * 3.5;
    placeOnGround(w, x, zz, (hash2(z, x) - 0.5) * 0.15);
    wires.push({ x, z: zz, length: 8.6, mesh: w });
  }
}
wireRow(76, 11);
wireRow(92, 12);
wireRow(210, 13);
wireRow(1180, 12);

// Extra angled wire to force weaving
for (const w of [
  { x: -22, z: 330, rot: 0.7 },
  { x: 26, z: 410, rot: -0.55 },
  { x: -30, z: 760, rot: 0.5 },
  { x: 28, z: 1040, rot: -0.6 },
  { x: -18, z: 1410, rot: 0.4 },
]) {
  const m = createBarbedWire(10);
  placeOnGround(m, w.x, w.z, w.rot);
  wires.push({ x: w.x, z: w.z, length: 10, mesh: m, rot: w.rot });
}

// Anti-personnel mines off-road (tempting shortcuts)
scatter(90, 1600, 70, (x, z) => {
  const m = createMine();
  placeOnGround(m, x, z, 0, -0.02);
  mines.push({ x, z, r: 0.7, mesh: m, live: true });
});
// A few on the shoulders of the road
for (let i = 0; i < 18; i++) {
  const rp = ROAD_POINTS[4 + (i % (ROAD_POINTS.length - 8))];
  const side = i % 2 === 0 ? 1 : -1;
  const x = rp[0] + side * (ROAD_WIDTH + 1.2 + (i % 3));
  const z = rp[1] + (i % 5) * 3;
  const m = createMine();
  placeOnGround(m, x, z, 0, -0.02);
  mines.push({ x, z, r: 0.7, mesh: m, live: true });
}

// Tank mines with X — clusters in the tank graveyard and chokepoints
function plantTankMine(x, z) {
  const m = createTankMine();
  placeOnGround(m, x, z, Math.random() * Math.PI, -0.01);
  tankMines.push({ x, z, r: 1.05, mesh: m, live: true });
}
for (let i = 0; i < 22; i++) {
  const z = 640 + i * 12 + (hash2(i, 7) - 0.5) * 8;
  const x = (hash2(i, 3) - 0.5) * 70;
  if (distToRoad(x, z) < 3.2 && i % 3 !== 0) continue;
  plantTankMine(x, z);
}
for (const p of [
  [8, 300], [-14, 448], [18, 612], [-10, 840], [12, 1010], [-16, 1200], [10, 1430], [0, 1540],
]) {
  plantTankMine(p[0] + 4.5, p[1]);
  plantTankMine(p[0] - 5.2, p[1] + 6);
}

// Static tanks (some wrecks)
const tankSpots = [
  { x: -24, z: 680, rot: 0.6, wreck: true },
  { x: 22, z: 705, rot: -0.4, wreck: false },
  { x: -8, z: 740, rot: 2.5, wreck: true },
  { x: 36, z: 770, rot: 0.2, wreck: false },
  { x: -38, z: 800, rot: 1.2, wreck: true },
  { x: 14, z: 860, rot: -1.1, wreck: false },
  { x: -20, z: 910, rot: 0.3, wreck: true },
  { x: 40, z: 940, rot: 3.0, wreck: false },
  { x: 6, z: 1320, rot: 0.5, wreck: true },
  { x: -32, z: 1500, rot: -0.3, wreck: false },
];
for (const t of tankSpots) {
  const m = createTank(t.wreck);
  const y = heightAt(t.x, t.z);
  m.position.set(t.x, y, t.z);
  m.rotation.y = t.rot;
  if (t.wreck) m.rotation.z = 0.08;
  scene.add(m);
  solids.push({ x: t.x, z: t.z, r: 2.5, mesh: m });
  tanks.push(m);
  if (t.wreck) {
    const s = createSmokeColumn();
    s.position.set(t.x + 0.3, y + 1.6, t.z);
    scene.add(s);
    smokes.push(s);
  }
}

// Bunkers with machine guns
function mountTurret(root, kind, range, cone) {
  const gun = root.userData.gun;
  turrets.push({
    root,
    gun,
    kind,
    range,
    cone,
    yaw: 0,
    cooldown: 0.6 + Math.random(),
    burstLeft: 0,
    fireTimer: 0,
    flash: gun.userData.flash,
  });
}

for (const b of BUNKERS) {
  const m = createBunker();
  const y = heightAt(b.x, b.z) - 0.4;
  m.position.set(b.x, y, b.z);
  m.rotation.y = b.rot;
  scene.add(m);
  solids.push({ x: b.x, z: b.z, r: 4.4, mesh: m });
  mountTurret(m, "bunker", 92, 0.95);
  const crate = createCrate();
  placeOnGround(crate, b.x + Math.cos(b.rot) * 4.2, b.z + Math.sin(b.rot) * 4.2, b.rot);
  solids.push({ x: crate.position.x, z: crate.position.z, r: 0.8, mesh: crate });
}

// Sandbag fighting positions covering the track
for (const z of [250, 360, 450, 620, 790, 980, 1110, 1260, 1475]) {
  const nr = nearestRoadPoint(0, z);
  const side = z % 200 < 100 ? -1 : 1;
  const stack = createSandbagStack(5.2, 3);
  const x = nr.x + side * 9.5;
  placeOnGround(stack, x, nr.z, 0.15 * side);
}

for (const n of NESTS) {
  const m = createNest();
  const y = heightAt(n.x, n.z) - 0.15;
  m.position.set(n.x, y, n.z);
  m.rotation.y = n.rot;
  scene.add(m);
  solids.push({ x: n.x, z: n.z, r: 2.1, mesh: m });
  mountTurret(m, "nest", 70, 2.6);
}

// Barricades as chicanes on the track shoulders — weave, don't blast straight
const chicaneZ = [168, 188, 318, 348, 520, 575, 748, 880, 1010, 1140, 1288, 1380, 1510, 1565];
chicaneZ.forEach((z, i) => {
  const nr = nearestRoadPoint(0, z);
  const side = i % 2 === 0 ? 1 : -1;
  const x = nr.x + side * (ROAD_WIDTH * 0.55);
  const m = createBarricade();
  const rot = Math.atan2(
    (ROAD_POINTS[Math.min(i + 1, ROAD_POINTS.length - 1)][0] - nr.x),
    20
  );
  placeOnGround(m, x, nr.z, rot + side * 0.35);
  solids.push({ x, z: nr.z, r: 1.45, mesh: m });
});

// Dragon's teeth fields
for (let i = 0; i < 24; i++) {
  const z = 1230 + (i % 6) * 7;
  const x = -60 + Math.floor(i / 6) * 8 + (i % 2) * 3;
  if (distToRoad(x, z) < 5) continue;
  const m = createDragonTooth();
  placeOnGround(m, x, z, Math.PI / 4);
  solids.push({ x, z, r: 0.85, mesh: m });
}
for (let i = 0; i < 24; i++) {
  const z = 1230 + (i % 6) * 7;
  const x = 36 + Math.floor(i / 6) * 8 + (i % 2) * 3;
  if (distToRoad(x, z) < 5) continue;
  const m = createDragonTooth();
  placeOnGround(m, x, z, Math.PI / 4);
  solids.push({ x, z, r: 0.85, mesh: m });
}

// Wooden revetments along trench walls
for (const t of TRENCHES) {
  const mx = (t.ax + t.bx) / 2;
  const mz = (t.az + t.bz) / 2;
  const ang = Math.atan2(t.bx - t.ax, t.bz - t.az);
  const len = Math.hypot(t.bx - t.ax, t.bz - t.az);
  const nx = Math.cos(ang);
  const nz = -Math.sin(ang);
  const rv = createRevetment(Math.min(len, 10));
  placeOnGround(rv, mx + nx * (t.w * 0.55), mz + nz * (t.w * 0.55), ang, -0.2);
}

// Sandbag walls along trench lips
for (const t of TRENCHES) {
  const mx = (t.ax + t.bx) / 2;
  const mz = (t.az + t.bz) / 2;
  if (distToRoad(mx, mz) < 8) continue;
  const stack = createSandbagStack(6.5, 3);
  const ang = Math.atan2(t.bx - t.ax, t.bz - t.az);
  placeOnGround(stack, mx, mz - 2.2, ang, 0);
}

// Timber at road/trench crossings
for (const t of TRENCHES) {
  const nr = nearestRoadPoint((t.ax + t.bx) / 2, (t.az + t.bz) / 2);
  if (nr.d < 14) {
    const br = createTimberBridge(5.4, 9);
    placeOnGround(br, nr.x, nr.z, 0, 0.02);
  }
}

// Atlantic Wall
const wall = createAtlanticWall(210);
wall.position.set(0, heightAt(0, WALL_Z) - 1.2, WALL_Z);
scene.add(wall);
solids.push({ x: 0, z: WALL_Z, r: 18, mesh: wall, wall: true });

// Finish markers
{
  const poleM = lambert(0x2a2c24);
  for (const x of [-8, 8]) {
    const p = new THREE.Mesh(new THREE.BoxGeometry(0.18, 4.2, 0.18), poleM);
    p.position.set(x, heightAt(x, FINISH_Z) + 2.1, FINISH_Z);
    scene.add(p);
  }
  const banner = new THREE.Mesh(
    new THREE.BoxGeometry(16, 1.6, 0.08),
    lambert(0x2c4a28)
  );
  banner.position.set(0, heightAt(0, FINISH_Z) + 3.6, FINISH_Z);
  scene.add(banner);
}

// Bombers
function spawnPlane(z) {
  const p = createPlane();
  const side = Math.random() < 0.5 ? -1 : 1;
  p.position.set(side * 90, 46 + Math.random() * 10, z);
  p.rotation.y = side > 0 ? -Math.PI / 2 : Math.PI / 2;
  scene.add(p);
  planes.push({
    mesh: p,
    heading: side > 0 ? -Math.PI / 2 : Math.PI / 2,
    speed: 42 + Math.random() * 10,
    bombCd: 1.5 + Math.random() * 2,
    bank: 0,
    alt: p.position.y,
  });
}
spawnPlane(200);
spawnPlane(480);
spawnPlane(700);
spawnPlane(1050);
spawnPlane(1380);

/* ------------------------------------------------------------------ */
/*  Player car                                                         */
/* ------------------------------------------------------------------ */

const carMesh = createCar();
scene.add(carMesh);

const carShadow = new THREE.Mesh(
  new THREE.CircleGeometry(1.4, 12),
  new THREE.MeshBasicMaterial({ color: 0x000000, transparent: true, opacity: 0.32, depthWrite: false })
);
carShadow.rotation.x = -Math.PI / 2;
scene.add(carShadow);

const dmgSmoke = createSmokeColumn();
dmgSmoke.scale.setScalar(0.28);
dmgSmoke.position.set(0.15, 0.95, 1.15);
dmgSmoke.visible = false;
carMesh.add(dmgSmoke);
smokes.push(dmgSmoke);

const player = {
  x: START.x,
  z: START.z,
  y: heightAt(START.x, START.z),
  heading: START.heading,
  steer: 0,
  speed: 0,
  hp: 100,
  alive: true,
  won: false,
  invuln: 0,
  radius: 1.15,
};

function resetPlayer() {
  player.x = START.x;
  player.z = START.z;
  player.y = heightAt(START.x, START.z);
  player.heading = START.heading;
  player.steer = 0;
  player.speed = 0;
  player.hp = 100;
  player.alive = true;
  player.won = false;
  player.invuln = 0;
  carMesh.visible = true;
  carShadow.visible = true;
  carMesh.rotation.set(0, START.heading, 0);
}

const keys = Object.create(null);
addEventListener("keydown", (e) => {
  keys[e.code] = true;
  if (["ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "Space"].includes(e.code)) e.preventDefault();
});
addEventListener("keyup", (e) => {
  keys[e.code] = false;
});

/* ------------------------------------------------------------------ */
/*  Combat                                                             */
/* ------------------------------------------------------------------ */

const tracerGeo = new THREE.BoxGeometry(0.07, 0.07, 0.55);
const tracerMat = new THREE.MeshBasicMaterial({ color: 0xffc44a });
const tracerPool = [];

function getTracerMesh() {
  const m = tracerPool.pop() || new THREE.Mesh(tracerGeo, tracerMat);
  m.visible = true;
  scene.add(m);
  return m;
}

function spawnTracer(origin, dir, speed = 92) {
  const vel = dir.clone().normalize().multiplyScalar(speed);
  // slight spread
  vel.x += (Math.random() - 0.5) * 3.2;
  vel.y += (Math.random() - 0.5) * 1.4;
  vel.z += (Math.random() - 0.5) * 3.2;
  const mesh = getTracerMesh();
  mesh.position.copy(origin);
  mesh.lookAt(origin.x + vel.x, origin.y + vel.y, origin.z + vel.z);
  tracers.push({ mesh, vel, life: 1.15, origin: origin.clone() });
}

function terrainBlocks(from, to) {
  for (let i = 1; i <= 5; i++) {
    const t = i / 6;
    const x = from.x + (to.x - from.x) * t;
    const y = from.y + (to.y - from.y) * t;
    const z = from.z + (to.z - from.z) * t;
    if (heightAt(x, z) > y - 0.35) return true;
  }
  return false;
}

const muzzleWorld = new THREE.Vector3();
const aimAt = new THREE.Vector3();

function updateTurrets(dt) {
  if (!player.alive) return;
  for (const t of turrets) {
    const root = t.root;
    const gx = root.position.x;
    const gz = root.position.z;
    const dx = player.x - gx;
    const dz = player.z - gz;
    const dist = Math.hypot(dx, dz);
    t.cooldown = Math.max(0, t.cooldown - dt);
    t.fireTimer = Math.max(0, t.fireTimer - dt);

    if (t.flash) t.flash.material.opacity = Math.max(0, t.flash.material.opacity - dt * 12);

    if (dist > t.range || dist < 4) {
      if (t.burstLeft <= 0) continue;
    }

    // Desired yaw in world, then relative to bunker
    const worldYaw = Math.atan2(dx, dz);
    const local = worldYaw - root.rotation.y;
    // shortest
    let diff = local - t.yaw;
    while (diff > Math.PI) diff -= Math.PI * 2;
    while (diff < -Math.PI) diff += Math.PI * 2;
    const maxCone = t.cone;
    const targetYaw = clamp(t.yaw + diff, -maxCone, maxCone);
    t.yaw = lerp(t.yaw, targetYaw, 1 - Math.pow(0.001, dt));
    t.gun.rotation.y = t.yaw;

    const facing = t.yaw + root.rotation.y;
    const toPlayer = Math.atan2(dx, dz);
    let ang = toPlayer - facing;
    while (ang > Math.PI) ang -= Math.PI * 2;
    while (ang < -Math.PI) ang += Math.PI * 2;
    const inCone = Math.abs(ang) < 0.22 && dist < t.range;

    if (t.burstLeft <= 0 && t.cooldown <= 0 && inCone && state === "play") {
      t.burstLeft = 6 + (Math.random() * 6) | 0;
      t.fireTimer = 0;
    }

    if (t.burstLeft > 0 && t.fireTimer <= 0 && state === "play") {
      t.gun.updateWorldMatrix(true, false);
      muzzleWorld.set(0, 0.18, 2.22);
      t.gun.localToWorld(muzzleWorld);
      aimAt.set(player.x, player.y + 0.7, player.z);
      // lead
      const lead = dist / 92;
      aimAt.x += Math.sin(player.heading) * player.speed * lead * 0.65;
      aimAt.z += Math.cos(player.heading) * player.speed * lead * 0.65;
      const dir = tmp.subVectors(aimAt, muzzleWorld);
      if (!terrainBlocks(muzzleWorld, aimAt)) {
        spawnTracer(muzzleWorld, dir);
        sfx.mg();
        if (t.flash) t.flash.material.opacity = 0.95;
      }
      t.burstLeft--;
      t.fireTimer = 0.075;
      if (t.burstLeft <= 0) t.cooldown = 1.1 + Math.random() * 1.3;
    }
  }
}

function updateTracers(dt) {
  for (let i = tracers.length - 1; i >= 0; i--) {
    const tr = tracers[i];
    tr.life -= dt;
    const prev = tmp2.copy(tr.mesh.position);
    tr.mesh.position.addScaledVector(tr.vel, dt);
    tr.mesh.lookAt(tr.mesh.position.x + tr.vel.x, tr.mesh.position.y + tr.vel.y, tr.mesh.position.z + tr.vel.z);

    const hitGround = tr.mesh.position.y < heightAt(tr.mesh.position.x, tr.mesh.position.z) + 0.08;
    const dx = tr.mesh.position.x - player.x;
    const dy = tr.mesh.position.y - (player.y + 0.65);
    const dz = tr.mesh.position.z - player.z;
    const hitCar = player.alive && dx * dx + dz * dz < 1.35 * 1.35 && Math.abs(dy) < 0.9;

    if (hitCar) {
      damage(8, "MACHINE GUN");
      kickDirt(tr.mesh.position, 6, 0xffcc66);
      recycleTracer(i);
      continue;
    }
    if (hitGround || tr.life <= 0) {
      if (hitGround) kickDirt(tr.mesh.position, 4, 0xc4b080);
      recycleTracer(i);
    }
  }
}

function recycleTracer(i) {
  const tr = tracers[i];
  scene.remove(tr.mesh);
  tracerPool.push(tr.mesh);
  tracers.splice(i, 1);
}

function updatePlanes(dt) {
  for (const p of planes) {
    // steer toward a lead point over the player, then fly past
    const targetX = player.x + Math.sin(player.heading) * 18;
    const targetZ = player.z + 40;
    const dx = targetX - p.mesh.position.x;
    const dz = targetZ - p.mesh.position.z;
    const want = Math.atan2(dx, dz);
    let diff = want - p.heading;
    while (diff > Math.PI) diff -= Math.PI * 2;
    while (diff < -Math.PI) diff += Math.PI * 2;
    p.heading += clamp(diff, -0.55 * dt, 0.55 * dt);
    p.mesh.rotation.y = p.heading;
    p.mesh.rotation.z = lerp(p.mesh.rotation.z, clamp(-diff * 0.6, -0.35, 0.35), 0.08);

    p.mesh.position.x += Math.sin(p.heading) * p.speed * dt;
    p.mesh.position.z += Math.cos(p.heading) * p.speed * dt;
    p.mesh.position.y = p.alt + Math.sin(performance.now() * 0.001 + p.alt) * 0.6;

    if (p.mesh.userData.props) {
      for (const pr of p.mesh.userData.props) pr.rotation.z += dt * 28;
    }

    // recycle if far
    if (p.mesh.position.z > WALL_Z + 80 || p.mesh.position.z < MIN_Z - 40 || Math.abs(p.mesh.position.x) > 160) {
      p.mesh.position.set((Math.random() < 0.5 ? -1 : 1) * 100, p.alt, player.z - 90);
      p.heading = Math.atan2(player.x - p.mesh.position.x, player.z + 80 - p.mesh.position.z);
    }

    p.bombCd -= dt;
    const dist = Math.hypot(p.mesh.position.x - player.x, p.mesh.position.z - player.z);
    if (state === "play" && player.alive && p.bombCd <= 0 && dist < 48 && p.mesh.position.z > player.z - 10) {
      dropBomb(p);
      p.bombCd = 3.5 + Math.random() * 2.5;
    }
  }
}

function dropBomb(plane) {
  const b = createBomb();
  b.position.copy(plane.mesh.position);
  b.position.y -= 1.2;
  scene.add(b);
  const shadow = new THREE.Mesh(
    new THREE.CircleGeometry(1.1, 12),
    new THREE.MeshBasicMaterial({ color: 0x4a1208, transparent: true, opacity: 0.45, depthWrite: false })
  );
  shadow.rotation.x = -Math.PI / 2;
  scene.add(shadow);
  bombs.push({
    mesh: b,
    shadow,
    vx: Math.sin(plane.heading) * plane.speed * 0.22 + (player.x - plane.mesh.position.x) * 0.12,
    vz: Math.cos(plane.heading) * plane.speed * 0.22 + (player.z - plane.mesh.position.z) * 0.08,
    vy: -1.5,
  });
  sfx.whistle();
}

function updateBombs(dt) {
  for (let i = bombs.length - 1; i >= 0; i--) {
    const b = bombs[i];
    b.vy -= 18 * dt;
    b.mesh.position.x += b.vx * dt;
    b.mesh.position.y += b.vy * dt;
    b.mesh.position.z += b.vz * dt;
    b.mesh.rotation.x += dt * 1.2;
    const gy = heightAt(b.mesh.position.x, b.mesh.position.z);
    b.shadow.position.set(b.mesh.position.x, gy + 0.06, b.mesh.position.z);
    const fall = Math.max(0.2, b.mesh.position.y - gy);
    b.shadow.scale.setScalar(clamp(8 / fall, 0.8, 5.5));
    if (b.shadow.material) b.shadow.material.opacity = 0.28 + clamp(4 / fall, 0, 0.5);
    if (b.mesh.position.y <= gy + 0.2) {
      explode(b.mesh.position.x, gy, b.mesh.position.z, 9.5, 42, "BOMB");
      scene.remove(b.mesh);
      scene.remove(b.shadow);
      bombs.splice(i, 1);
    }
  }
}

function explode(x, y, z, radius, dmg, cause) {
  sfx.boom();
  shake.mag = Math.max(shake.mag, dmg > 0 ? 0.55 : 0.14);
  const light = new THREE.PointLight(0xffaa55, 4.5, 28);
  light.position.set(x, y + 1.4, z);
  scene.add(light);
  lightsFlash.push({ light, life: 0.35 });
  kickDirt(new THREE.Vector3(x, y + 0.4, z), 22, 0xff9944, 8);
  // scorch
  const scorch = new THREE.Mesh(
    new THREE.CircleGeometry(radius * 0.45, 10),
    new THREE.MeshLambertMaterial({ color: 0x2a2418, transparent: true, opacity: 0.7 })
  );
  scorch.rotation.x = -Math.PI / 2;
  scorch.position.set(x, y + 0.04, z);
  scene.add(scorch);

  if (player.alive) {
    const d = Math.hypot(player.x - x, player.z - z);
    if (d < radius) {
      const falloff = 1 - d / radius;
      damage(dmg * falloff, cause);
    }
  }
}

function kickDirt(pos, n, color, speed = 5) {
  const mat = new THREE.MeshBasicMaterial({ color, transparent: true, opacity: 0.95 });
  for (let i = 0; i < n; i++) {
    const m = new THREE.Mesh(new THREE.BoxGeometry(0.12, 0.12, 0.12), mat);
    m.position.copy(pos);
    scene.add(m);
    particles.push({
      mesh: m,
      vx: (Math.random() - 0.5) * speed,
      vy: Math.random() * speed * 0.8 + 1.5,
      vz: (Math.random() - 0.5) * speed,
      life: 0.45 + Math.random() * 0.4,
    });
  }
}

function updateParticles(dt) {
  for (let i = particles.length - 1; i >= 0; i--) {
    const p = particles[i];
    p.vy -= 14 * dt;
    p.mesh.position.x += p.vx * dt;
    p.mesh.position.y += p.vy * dt;
    p.mesh.position.z += p.vz * dt;
    p.life -= dt;
    p.mesh.material.opacity = clamp(p.life * 2, 0, 1);
    if (p.life <= 0) {
      scene.remove(p.mesh);
      p.mesh.geometry.dispose();
      particles.splice(i, 1);
    }
  }
  for (let i = lightsFlash.length - 1; i >= 0; i--) {
    const f = lightsFlash[i];
    f.life -= dt;
    f.light.intensity = Math.max(0, f.life * 12);
    if (f.life <= 0) {
      scene.remove(f.light);
      lightsFlash.splice(i, 1);
    }
  }
  for (const s of smokes) {
    const t = performance.now() * 0.001;
    s.children.forEach((c, i) => {
      c.position.x = Math.sin(t * 0.7 + i) * 0.25;
      c.position.z = Math.cos(t * 0.5 + i) * 0.2;
      c.material.opacity = 0.18 + Math.sin(t + i) * 0.06;
    });
  }
}

function damage(amount, cause) {
  if (!player.alive || state !== "play") return;
  if (amount <= 0) return;
  if (player.invuln > 0) return;
  player.hp -= amount;
  player.invuln = amount >= 20 ? 0.45 : 0.11;
  hitFlash = amount >= 20 ? 0.7 : 0.4;
  shake.mag = Math.max(shake.mag, amount >= 20 ? 0.45 : 0.22);
  sfx.hit();
  lastCause = cause;
  if (player.hp <= 0) {
    player.hp = 0;
    player.alive = false;
    explode(player.x, player.y, player.z, 5, 0, cause);
    carMesh.visible = false;
    carShadow.visible = false;
    endGame(false, cause);
  }
}

let lastCause = "";
let hitFlash = 0;
const shake = { mag: 0 };

/* ------------------------------------------------------------------ */
/*  Physics                                                            */
/* ------------------------------------------------------------------ */

function updateCar(dt) {
  if (!player.alive) return;

  const throttle = keys.KeyW || keys.ArrowUp ? 1 : keys.KeyS || keys.ArrowDown ? -0.55 : 0;
  const steerIn = keys.KeyA || keys.ArrowLeft ? 1 : keys.KeyD || keys.ArrowRight ? -1 : 0;
  const braking = keys.ShiftLeft || keys.ShiftRight || keys.Space;

  const onRoad = distToRoad(player.x, player.z) < ROAD_WIDTH + 0.8;
  const h = heightAt(player.x, player.z);
  const inTrench = h < baseHeight(player.z) - 0.9;
  const maxSpeed = inTrench ? 7 : onRoad ? 26 : 12.5;
  const accel = inTrench ? 6 : onRoad ? 18 : 9;

  if (braking) {
    player.speed *= Math.pow(0.08, dt);
  } else {
    player.speed += throttle * accel * dt;
  }
  const drag = onRoad ? 1.6 : 3.4;
  player.speed -= Math.sign(player.speed) * drag * dt;
  if (Math.abs(player.speed) < 0.15 && throttle === 0) player.speed = 0;
  player.speed = clamp(player.speed, -9, maxSpeed);

  const steerSpeed = 2.1 * (0.35 + Math.min(Math.abs(player.speed) / 12, 1));
  player.steer = lerp(player.steer, steerIn * 0.55, 1 - Math.pow(0.0008, dt));
  player.heading += player.steer * steerSpeed * (player.speed / maxSpeed) * dt * Math.sign(player.speed || 1);

  let nx = player.x + Math.sin(player.heading) * player.speed * dt;
  let nz = player.z + Math.cos(player.heading) * player.speed * dt;
  nx = clamp(nx, MIN_X + 2, MAX_X - 2);
  nz = clamp(nz, MIN_Z + 10, MAX_Z - 2);

  // solids
  for (const s of solids) {
    if (s.wall) {
      if (nz > WALL_Z - 10 && Math.abs(nx) < 100) {
        nz = Math.min(nz, WALL_Z - 10);
        player.speed *= 0.4;
      }
      continue;
    }
    const dx = nx - s.x;
    const dz = nz - s.z;
    const rad = s.r + player.radius;
    const d2 = dx * dx + dz * dz;
    if (d2 < rad * rad && d2 > 1e-6) {
      const d = Math.sqrt(d2);
      const push = (rad - d) / d;
      nx += dx * push;
      nz += dz * push;
      player.speed *= 0.55;
    }
  }

  player.x = nx;
  player.z = nz;

  const sl = slopeAt(player.x, player.z);
  player.y = lerp(player.y, heightAt(player.x, player.z), 1 - Math.pow(0.0002, dt));

  const pitch = Math.atan2(sl.dz, 1) * 0.85;
  const roll = Math.atan2(-sl.dx, 1) * 0.85;
  carMesh.position.set(player.x, player.y, player.z);
  carMesh.rotation.order = "YXZ";
  carMesh.rotation.y = player.heading;
  carMesh.rotation.x = pitch;
  carMesh.rotation.z = roll;

  if (carMesh.userData.wheels) {
    const spin = (player.speed * dt) / 0.32;
    carMesh.userData.wheels.forEach((w, i) => {
      w.rotation.x += spin;
      if (i < 2) w.rotation.y = player.steer * 0.7;
    });
  }

  carShadow.position.set(player.x, heightAt(player.x, player.z) + 0.04, player.z);
  dmgSmoke.visible = player.alive && player.hp < 58;
  dmgSmoke.scale.setScalar(player.hp < 28 ? 0.45 : 0.28);

  if (!onRoad && Math.abs(player.speed) > 6 && Math.random() < dt * 14) {
    kickDirt(new THREE.Vector3(player.x, player.y + 0.2, player.z), 2, 0x6a5a40, 3);
  }

  // mines
  for (const m of mines) {
    if (!m.live) continue;
    if (Math.hypot(player.x - m.x, player.z - m.z) < m.r + player.radius * 0.7) {
      m.live = false;
      scene.remove(m.mesh);
      explode(m.x, heightAt(m.x, m.z), m.z, 4.2, 30, "MINE");
    }
  }
  for (const m of tankMines) {
    if (!m.live) continue;
    if (Math.hypot(player.x - m.x, player.z - m.z) < m.r + player.radius * 0.65) {
      m.live = false;
      scene.remove(m.mesh);
      explode(m.x, heightAt(m.x, m.z), m.z, 7.5, 62, "TANK MINE");
    }
  }

  // barbed wire — slow and cut
  for (const w of wires) {
    const rot = w.rot || 0;
    const dx = player.x - w.x;
    const dz = player.z - w.z;
    const c = Math.cos(-rot);
    const s = Math.sin(-rot);
    const lx = dx * c - dz * s;
    const lz = dx * s + dz * c;
    if (Math.abs(lx) < w.length * 0.52 && Math.abs(lz) < 1.15) {
      player.speed *= Math.pow(0.12, dt);
      if (player.invuln <= 0) damage(9, "BARBED WIRE");
    }
  }

  if (player.z > FINISH_Z && player.alive) {
    player.won = true;
    endGame(true);
  }
}

function updateTide(dt) {
  if (state !== "play") return;
  tide.front += (tide.front < 140 ? 3.05 : 1.15) * dt;
  tide.y += 0.018 * dt;
  water.position.y = tide.y;
  // Plane is 520 deep; inland edge sits on the tide line.
  water.position.z = tide.front - 260;
  foam.position.set(0, tide.y + 0.12, tide.front);
  foam.position.y = tide.y + 0.12;
  waterUniforms.uFoam.value = 220;
  // drown
  if (player.alive && player.z < tide.front - 2 && player.y < tide.y + 0.6) {
    damage(55 * dt, "THE TIDE");
    document.getElementById("water-overlay").style.background = `rgba(20,50,70,${clamp((tide.y - player.y) * 0.4, 0, 0.45)})`;
  } else {
    document.getElementById("water-overlay").style.background = "rgba(20,50,70,0)";
  }
}

/* ------------------------------------------------------------------ */
/*  Camera                                                             */
/* ------------------------------------------------------------------ */

const camTarget = new THREE.Vector3();
const camPos = new THREE.Vector3(0, 22, 18);
let menuT = 0;

function updateCamera(dt) {
  if (state === "menu") {
    menuT += dt * 0.08;
    const z = 40 + (Math.sin(menuT) * 0.5 + 0.5) * 900;
    const x = Math.sin(menuT * 0.7) * 40;
    camera.position.set(x, 28 + Math.sin(menuT) * 4, z - 36);
    camera.lookAt(x * 0.3, 4, z + 20);
    return;
  }
  const back = 8.4;
  const up = 3.6;
  const fx = Math.sin(player.heading);
  const fz = Math.cos(player.heading);
  camPos.set(player.x - fx * back, player.y + up, player.z - fz * back);
  camTarget.set(player.x + fx * 10, player.y + 1.1, player.z + fz * 10);
  camera.position.lerp(camPos, 1 - Math.pow(0.0004, dt));
  const look = tmp.copy(camTarget);
  camera.lookAt(look);
  const wantFov = 58 + Math.min(Math.abs(player.speed), 26) * 0.42;
  camera.fov += (wantFov - camera.fov) * 0.08;
  camera.updateProjectionMatrix();
  if (shake.mag > 0.002) {
    camera.position.x += (Math.random() - 0.5) * shake.mag;
    camera.position.y += (Math.random() - 0.5) * shake.mag;
    shake.mag *= Math.pow(0.02, dt);
  }
  sun.position.set(player.x + 50, 80, player.z + 30);
  sun.target.position.set(player.x, player.y, player.z);
  sun.target.updateMatrixWorld();
}

/* ------------------------------------------------------------------ */
/*  Audio                                                              */
/* ------------------------------------------------------------------ */

const sfx = (() => {
  let ctx;
  function ac() {
    if (!ctx) ctx = new (window.AudioContext || window.webkitAudioContext)();
    if (ctx.state === "suspended") ctx.resume();
    return ctx;
  }
  function noiseBurst(dur, gain, freqLo, freqHi) {
    const c = ac();
    const n = c.createBuffer(1, (c.sampleRate * dur) | 0, c.sampleRate);
    const d = n.getChannelData(0);
    for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1;
    const src = c.createBufferSource();
    src.buffer = n;
    const bp = c.createBiquadFilter();
    bp.type = "bandpass";
    bp.frequency.value = (freqLo + freqHi) * 0.5;
    const g = c.createGain();
    g.gain.setValueAtTime(gain, c.currentTime);
    g.gain.exponentialRampToValueAtTime(0.001, c.currentTime + dur);
    src.connect(bp);
    bp.connect(g);
    g.connect(c.destination);
    src.start();
  }
  let engineOsc, engineGain, engineLfo;
  return {
    startEngine() {
      const c = ac();
      engineOsc = c.createOscillator();
      engineOsc.type = "sawtooth";
      engineGain = c.createGain();
      engineGain.gain.value = 0.0;
      const f = c.createBiquadFilter();
      f.type = "lowpass";
      f.frequency.value = 320;
      engineOsc.connect(f);
      f.connect(engineGain);
      engineGain.connect(c.destination);
      engineOsc.start();
    },
    engine(speed) {
      if (!engineOsc) return;
      const c = ac();
      engineOsc.frequency.setTargetAtTime(48 + Math.abs(speed) * 9, c.currentTime, 0.08);
      engineGain.gain.setTargetAtTime(0.03 + Math.min(Math.abs(speed) / 26, 1) * 0.05, c.currentTime, 0.08);
    },
    mg() {
      noiseBurst(0.045, 0.18, 800, 2400);
    },
    boom() {
      noiseBurst(0.45, 0.5, 60, 280);
      const c = ac();
      const o = c.createOscillator();
      o.type = "sine";
      o.frequency.setValueAtTime(140, c.currentTime);
      o.frequency.exponentialRampToValueAtTime(32, c.currentTime + 0.4);
      const g = c.createGain();
      g.gain.setValueAtTime(0.4, c.currentTime);
      g.gain.exponentialRampToValueAtTime(0.001, c.currentTime + 0.45);
      o.connect(g);
      g.connect(c.destination);
      o.start();
      o.stop(c.currentTime + 0.5);
    },
    hit() {
      noiseBurst(0.08, 0.22, 200, 900);
    },
    whistle() {
      const c = ac();
      const o = c.createOscillator();
      o.type = "sine";
      o.frequency.setValueAtTime(880, c.currentTime);
      o.frequency.exponentialRampToValueAtTime(180, c.currentTime + 0.8);
      const g = c.createGain();
      g.gain.setValueAtTime(0.05, c.currentTime);
      g.gain.exponentialRampToValueAtTime(0.001, c.currentTime + 0.8);
      o.connect(g);
      g.connect(c.destination);
      o.start();
      o.stop(c.currentTime + 0.85);
    },
  };
})();

/* ------------------------------------------------------------------ */
/*  UI                                                                 */
/* ------------------------------------------------------------------ */

const elHp = document.getElementById("hp-fill");
const elTide = document.getElementById("tide-fill");
const elDist = document.getElementById("distance");
const elSpeed = document.getElementById("speed");
const elWarn = document.getElementById("warning");
const elHit = document.getElementById("hitflash");
const mini = document.getElementById("minimap");
const mctx = mini.getContext("2d");

function updateHUD() {
  elHp.style.width = `${clamp(player.hp, 0, 100)}%`;
  elHp.style.background =
    player.hp > 45 ? "linear-gradient(90deg, #6b8f3a, #c4d46a)" : "linear-gradient(90deg, #7a2018, #d45a3a)";
  const tidePct = clamp(tide.front / 420, 0, 1) * 100;
  elTide.style.width = `${tidePct}%`;
  const metres = Math.max(0, FINISH_Z - player.z);
  elDist.textContent = `${metres.toFixed(0)} m`;
  elSpeed.textContent = `${Math.abs(player.speed * 3.6).toFixed(0)}`;

  let warn = "";
  if (player.z < tide.front + 40) warn = "TIDE INCOMING";
  const nearGun = turrets.some((t) => {
    const d = Math.hypot(t.root.position.x - player.x, t.root.position.z - player.z);
    return d < 55;
  });
  if (nearGun && player.z > 200) warn = "UNDER FIRE";
  const planeNear = planes.some((p) => Math.hypot(p.mesh.position.x - player.x, p.mesh.position.z - player.z) < 40);
  if (planeNear) warn = "INCOMING AIRCRAFT";
  elWarn.textContent = warn;

  elHit.style.background = `rgba(120,16,8,${hitFlash * 0.45})`;

  // minimap
  const w = mini.width;
  const h = mini.height;
  mctx.fillStyle = "#1a1c14";
  mctx.fillRect(0, 0, w, h);
  const z0 = Math.max(MIN_Z, player.z - 220);
  const z1 = player.z + 320;
  const xTo = (x) => ((x - MIN_X) / (MAX_X - MIN_X)) * w;
  const zTo = (z) => h - ((z - z0) / (z1 - z0)) * h;
  // water
  mctx.fillStyle = "#2a5560";
  mctx.fillRect(0, zTo(tide.front), w, h);
  // road
  mctx.strokeStyle = "#6a5a44";
  mctx.lineWidth = 3;
  mctx.beginPath();
  ROAD_POINTS.forEach((p, i) => {
    const X = xTo(p[0]);
    const Z = zTo(p[1]);
    if (i === 0) mctx.moveTo(X, Z);
    else mctx.lineTo(X, Z);
  });
  mctx.stroke();
  // wall
  mctx.fillStyle = "#8a8680";
  mctx.fillRect(0, zTo(WALL_Z) - 3, w, 6);
  mctx.fillStyle = "#c4a35a";
  mctx.fillRect(0, zTo(FINISH_Z) - 1, w, 2);
  // bunkers
  mctx.fillStyle = "#9a2c24";
  for (const b of BUNKERS) {
    mctx.fillRect(xTo(b.x) - 2, zTo(b.z) - 2, 4, 4);
  }
  // player
  mctx.save();
  mctx.translate(xTo(player.x), zTo(player.z));
  mctx.rotate(-player.heading);
  mctx.fillStyle = "#e8e0d0";
  mctx.beginPath();
  mctx.moveTo(0, -6);
  mctx.lineTo(4, 5);
  mctx.lineTo(-4, 5);
  mctx.closePath();
  mctx.fill();
  mctx.restore();
}

/* ------------------------------------------------------------------ */
/*  State                                                              */
/* ------------------------------------------------------------------ */

let state = "menu";
document.body.classList.add("menu-open");

function startGame() {
  document.getElementById("menu").classList.add("hidden");
  document.getElementById("end-screen").classList.add("hidden");
  document.getElementById("hud").classList.remove("hidden");
  document.body.classList.remove("menu-open");
  mini.style.display = "block";
  document.getElementById("water-overlay").style.background = "rgba(20,50,70,0)";
  resetPlayer();
  tide.front = 6;
  tide.y = 0.2;
  player.invuln = 1.2;
  state = "play";
  sfx.startEngine();
  camera.position.set(player.x, player.y + 8, player.z - 12);
}

function endGame(win, cause) {
  if (state !== "play") return;
  state = win ? "win" : "dead";
  document.getElementById("hud").classList.add("hidden");
  mini.style.display = "none";
  const screen = document.getElementById("end-screen");
  screen.classList.remove("hidden");
  document.getElementById("end-stamp").textContent = win ? "OBJECTIVE TAKEN" : "AFTER ACTION";
  document.getElementById("end-title").textContent = win ? "THE WALL" : "KIA";
  document.getElementById("end-copy").textContent = win
    ? "The sedan is against the concrete. The Atlantic Wall is in Allied hands — at least this stretch of it. Overlord continues."
    : `The assault dies on the beachhead. Cause: ${cause || lastCause || "unknown"}. The tide keeps coming.`;
}

document.getElementById("start-btn").addEventListener("click", startGame);
document.getElementById("retry-btn").addEventListener("click", () => {
  location.reload();
});

addEventListener("resize", () => {
  camera.aspect = innerWidth / innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight);
});

/* ------------------------------------------------------------------ */
/*  Loop                                                               */
/* ------------------------------------------------------------------ */

let last = performance.now();
let artilleryCd = 2.5;
function frame(now) {
  const dt = clamp((now - last) / 1000, 0, 0.05);
  last = now;
  waterUniforms.uTime.value = now * 0.001;
  hitFlash = Math.max(0, hitFlash - dt * 1.8);
  player.invuln = Math.max(0, player.invuln - dt);

  if (state === "play") {
    updateCar(dt);
    updateTide(dt);
    updateTurrets(dt);
    updateTracers(dt);
    updatePlanes(dt);
    updateBombs(dt);
    sfx.engine(player.speed);
    artilleryCd -= dt;
    if (artilleryCd <= 0) {
      artilleryCd = 2.2 + Math.random() * 2.8;
      const ax = player.x + (Math.random() - 0.5) * 90;
      const az = player.z + 40 + Math.random() * 120;
      if (Math.hypot(ax - player.x, az - player.z) > 22) {
        explode(ax, heightAt(ax, az), az, 6, 0, "ARTILLERY");
      }
    }
  } else {
    updatePlanes(dt * 0.6);
  }
  updateParticles(dt);
  updateCamera(dt);
  if (state === "play") updateHUD();
  renderer.render(scene, camera);
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);

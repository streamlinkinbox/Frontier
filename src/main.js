import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import './style.css';

/*
 * Frontier GI Lab
 * ----------------
 * A deliberately small WebGL proof of concept for the requested GTX path:
 * transient RSM-style surface samples -> three camera-relative LPV clipmaps ->
 * a coarse directional blocker volume -> half-resolution screen-space polish.
 *
 * The simulation is CPU-dispatched in this demo so its intermediate buffers can
 * be inspected easily. The data flow mirrors the compute passes that would run
 * in a game renderer; the scene itself is rendered by WebGL / Three.js.
 */

const $ = (selector) => document.querySelector(selector);
const canvas = $('#viewport');
const debugCanvas = $('#debugCanvas');
const debugCtx = debugCanvas.getContext('2d', { alpha: false });

const state = {
  paused: false,
  animateLight: true,
  cascades: true,
  blockers: true,
  ssgi: true,
  debugMode: 'final',
  lightMode: 'sunset',
  energy: 1.15,
  propagation: 7,
  surfelBudget: 768,
};

const WORLD_SIZE = 40;
const WORLD_HALF = WORLD_SIZE / 2;
const WORLD_RES = 192;
const TEX_RES = 192;
const SSGI_RES = TEX_RES / 2;
const CASCADE_SPECS = [
  { span: 18, ySpan: 5.4, nx: 34, ny: 11, nz: 34, blocker: [17, 6, 17] },
  { span: 36, ySpan: 8.4, nx: 34, ny: 11, nz: 34, blocker: [17, 6, 17] },
  { span: 72, ySpan: 14.4, nx: 34, ny: 11, nz: 34, blocker: [17, 6, 17] },
];

const rgb = (hex) => {
  const c = new THREE.Color(hex);
  return [c.r, c.g, c.b];
};
const clamp = (v, lo = 0, hi = 1) => Math.min(hi, Math.max(lo, v));
const luminance = (r, g, b) => r * 0.22 + g * 0.68 + b * 0.1;
const smoothstep = (a, b, x) => {
  const t = clamp((x - a) / (b - a));
  return t * t * (3 - 2 * t);
};

// Simple architectural blocks: they are both the render meshes and the RSM / blocker scene.
const blocks = [
  { name: 'north wall', x: 0, z: -19.1, w: 38.6, d: 0.75, h: 4.1, color: '#456bc7' },
  { name: 'south wall', x: 0, z: 19.1, w: 38.6, d: 0.75, h: 4.1, color: '#23415f' },
  { name: 'west wall', x: -19.1, z: 0, w: 0.75, d: 38.6, h: 4.1, color: '#3b5a8d' },
  { name: 'east wall', x: 19.1, z: 0, w: 0.75, d: 38.6, h: 4.1, color: '#8b4d52' },
  { name: 'blue gallery wall', x: -7.8, z: -4.9, w: 0.7, d: 15.2, h: 3.4, color: '#3159b8' },
  { name: 'amber gallery wall', x: 4.5, z: 4.3, w: 11.4, d: 0.72, h: 3.4, color: '#bd693d' },
  { name: 'central plinth', x: -0.8, z: -4.0, w: 3.2, d: 3.2, h: 2.05, color: '#62738f' },
  { name: 'warm cube', x: 10.1, z: -7.0, w: 3.3, d: 3.3, h: 2.7, color: '#bd643e' },
  { name: 'cool cube', x: -11.4, z: 9.4, w: 3.0, d: 3.0, h: 2.55, color: '#2e86a8' },
  { name: 'south bench', x: 8.8, z: 12.6, w: 7.8, d: 1.2, h: 1.05, color: '#486377' },
].map((block) => ({ ...block, rgb: rgb(block.color) }));

/** Dense world occupancy used by cheap RSM-shadow and screen-space rays. */
const world = {
  height: new Float32Array(WORLD_RES * WORLD_RES),
  color: new Float32Array(WORLD_RES * WORLD_RES * 3),
};

function worldIndex(x, z) {
  const ix = Math.floor(((x + WORLD_HALF) / WORLD_SIZE) * WORLD_RES);
  const iz = Math.floor(((z + WORLD_HALF) / WORLD_SIZE) * WORLD_RES);
  if (ix < 0 || iz < 0 || ix >= WORLD_RES || iz >= WORLD_RES) return -1;
  return iz * WORLD_RES + ix;
}

function buildWorldOccupancy() {
  for (let z = 0; z < WORLD_RES; z++) {
    for (let x = 0; x < WORLD_RES; x++) {
      const wx = ((x + 0.5) / WORLD_RES) * WORLD_SIZE - WORLD_HALF;
      const wz = ((z + 0.5) / WORLD_RES) * WORLD_SIZE - WORLD_HALF;
      const index = z * WORLD_RES + x;
      for (const block of blocks) {
        if (Math.abs(wx - block.x) <= block.w * 0.5 && Math.abs(wz - block.z) <= block.d * 0.5) {
          world.height[index] = Math.max(world.height[index], block.h);
          const o = index * 3;
          world.color[o] = block.rgb[0];
          world.color[o + 1] = block.rgb[1];
          world.color[o + 2] = block.rgb[2];
        }
      }
    }
  }
}
buildWorldOccupancy();

function worldHeightAt(x, z) {
  const i = worldIndex(x, z);
  return i < 0 ? 8 : world.height[i];
}
function worldColorAt(x, z, out) {
  const i = worldIndex(x, z);
  if (i < 0) { out[0] = out[1] = out[2] = 0; return out; }
  const o = i * 3;
  out[0] = world.color[o];
  out[1] = world.color[o + 1];
  out[2] = world.color[o + 2];
  return out;
}

function intersectsBlock(minX, maxX, minY, maxY, minZ, maxZ) {
  for (const block of blocks) {
    if (
      maxX >= block.x - block.w / 2 && minX <= block.x + block.w / 2 &&
      maxY >= 0 && minY <= block.h &&
      maxZ >= block.z - block.d / 2 && minZ <= block.z + block.d / 2
    ) return true;
  }
  return false;
}

/**
 * A camera-relative LPV volume. Color and outgoing direction are stored per cell.
 * The little blocker grid deliberately has fewer cells than the lighting grid.
 */
class LpvCascade {
  constructor(spec, index) {
    Object.assign(this, spec);
    this.index = index;
    this.cellX = this.span / this.nx;
    this.cellY = this.ySpan / this.ny;
    this.cellZ = this.span / this.nz;
    this.origin = new THREE.Vector3(-this.span / 2, -0.25, -this.span / 2);
    this.center = new THREE.Vector2(0, 0);
    this.length = this.nx * this.ny * this.nz;
    this.energy = new Float32Array(this.length * 3);
    this.nextEnergy = new Float32Array(this.length * 3);
    this.direction = new Float32Array(this.length * 3);
    this.nextDirection = new Float32Array(this.length * 3);
    this.solid = new Uint8Array(this.length);
    this.bx = this.blocker[0]; this.by = this.blocker[1]; this.bz = this.blocker[2];
    this.blockerData = new Uint8Array(this.bx * this.by * this.bz);
    this.wasRecentered = true;
    this.rebuildBlockers();
  }

  indexOf(x, y, z) { return (z * this.ny + y) * this.nx + x; }
  blockerIndex(x, y, z) { return (z * this.by + y) * this.bx + x; }

  recenter(cameraX, cameraZ, cameraRelative) {
    const targetX = cameraRelative ? cameraX : 0;
    const targetZ = cameraRelative ? cameraZ : 0;
    const snapX = Math.floor((targetX - this.span * 0.5) / this.cellX) * this.cellX;
    const snapZ = Math.floor((targetZ - this.span * 0.5) / this.cellZ) * this.cellZ;
    const changed = Math.abs(snapX - this.origin.x) > this.cellX * 1.5 || Math.abs(snapZ - this.origin.z) > this.cellZ * 1.5;
    if (changed) {
      this.origin.x = snapX;
      this.origin.z = snapZ;
      this.center.set(snapX + this.span / 2, snapZ + this.span / 2);
      // A production clipmap would scroll slabs. Clearing makes this demo's movement explicit.
      this.energy.fill(0); this.direction.fill(0);
      this.rebuildBlockers();
    }
    this.wasRecentered = changed;
  }

  rebuildBlockers() {
    this.solid.fill(0);
    this.blockerData.fill(0);
    for (let z = 0; z < this.nz; z++) for (let y = 0; y < this.ny; y++) for (let x = 0; x < this.nx; x++) {
      const minX = this.origin.x + x * this.cellX;
      const minY = this.origin.y + y * this.cellY;
      const minZ = this.origin.z + z * this.cellZ;
      if (intersectsBlock(minX, minX + this.cellX, minY, minY + this.cellY, minZ, minZ + this.cellZ)) {
        this.solid[this.indexOf(x, y, z)] = 1;
      }
    }
    const sx = this.span / this.bx, sy = this.ySpan / this.by, sz = this.span / this.bz;
    for (let z = 0; z < this.bz; z++) for (let y = 0; y < this.by; y++) for (let x = 0; x < this.bx; x++) {
      const minX = this.origin.x + x * sx;
      const minY = this.origin.y + y * sy;
      const minZ = this.origin.z + z * sz;
      if (intersectsBlock(minX, minX + sx, minY, minY + sy, minZ, minZ + sz)) {
        this.blockerData[this.blockerIndex(x, y, z)] = 1;
      }
    }
  }

  pointToCell(x, y, z) {
    const ix = Math.floor((x - this.origin.x) / this.cellX);
    const iy = Math.floor((y - this.origin.y) / this.cellY);
    const iz = Math.floor((z - this.origin.z) / this.cellZ);
    if (ix < 0 || iy < 0 || iz < 0 || ix >= this.nx || iy >= this.ny || iz >= this.nz) return -1;
    return this.indexOf(ix, iy, iz);
  }

  inject(surfel, intensity) {
    const cell = this.pointToCell(surfel.x, surfel.y, surfel.z);
    if (cell < 0) return;
    const o = cell * 3;
    const spread = intensity / (1 + this.index * 0.42);
    this.energy[o] += surfel.flux[0] * spread;
    this.energy[o + 1] += surfel.flux[1] * spread;
    this.energy[o + 2] += surfel.flux[2] * spread;
    const power = luminance(surfel.flux[0], surfel.flux[1], surfel.flux[2]) * spread;
    this.direction[o] += surfel.nx * power;
    this.direction[o + 1] += surfel.ny * power;
    this.direction[o + 2] += surfel.nz * power;
  }

  // Coarse directional blocker lookup at a cell midpoint, biased along ray travel direction.
  blockedBetween(x, y, z, dx, dy, dz) {
    const px = this.origin.x + (x + 0.5 + dx * 0.33) * this.cellX;
    const py = this.origin.y + (y + 0.5 + dy * 0.33) * this.cellY;
    const pz = this.origin.z + (z + 0.5 + dz * 0.33) * this.cellZ;
    const bx = Math.floor((px - this.origin.x) / this.span * this.bx);
    const by = Math.floor((py - this.origin.y) / this.ySpan * this.by);
    const bz = Math.floor((pz - this.origin.z) / this.span * this.bz);
    if (bx < 0 || by < 0 || bz < 0 || bx >= this.bx || by >= this.by || bz >= this.bz) return true;
    return this.blockerData[this.blockerIndex(bx, by, bz)] === 1;
  }

  propagate(steps, blockersEnabled) {
    // Preserve a short irradiance history before fresh RSM surfels are injected.
    for (let i = 0; i < this.energy.length; i++) {
      this.energy[i] *= 0.73;
      this.direction[i] *= 0.73;
    }

    const neighborOffsets = [
      [-1, 0, 0], [1, 0, 0], [0, -1, 0], [0, 1, 0], [0, 0, -1], [0, 0, 1],
    ];
    for (let pass = 0; pass < steps; pass++) {
      this.nextEnergy.fill(0);
      this.nextDirection.fill(0);
      for (let z = 0; z < this.nz; z++) for (let y = 0; y < this.ny; y++) for (let x = 0; x < this.nx; x++) {
        const cell = this.indexOf(x, y, z);
        const o = cell * 3;
        if (this.solid[cell]) continue;
        let r = this.energy[o] * 0.51;
        let g = this.energy[o + 1] * 0.51;
        let b = this.energy[o + 2] * 0.51;
        let dr = this.direction[o] * 0.51;
        let dg = this.direction[o + 1] * 0.51;
        let db = this.direction[o + 2] * 0.51;

        for (const [dx, dy, dz] of neighborOffsets) {
          const nx = x + dx, ny = y + dy, nz = z + dz;
          if (nx < 0 || ny < 0 || nz < 0 || nx >= this.nx || ny >= this.ny || nz >= this.nz) continue;
          const nCell = this.indexOf(nx, ny, nz);
          if (this.solid[nCell]) continue;
          const no = nCell * 3;
          const nl = luminance(this.energy[no], this.energy[no + 1], this.energy[no + 2]);
          if (nl < 0.00001) continue;
          // Transport follows the stored outgoing direction. The clamp intentionally preserves diffuse spread.
          const alignment = clamp((this.direction[no] * -dx + this.direction[no + 1] * -dy + this.direction[no + 2] * -dz) / (nl + 0.0001), -0.15, 1);
          let weight = 0.068 * (0.58 + 0.42 * Math.max(0, alignment));
          if (blockersEnabled && this.blockedBetween(x, y, z, dx, dy, dz)) weight *= 0.035;
          r += this.energy[no] * weight;
          g += this.energy[no + 1] * weight;
          b += this.energy[no + 2] * weight;
          dr += this.direction[no] * weight;
          dg += this.direction[no + 1] * weight;
          db += this.direction[no + 2] * weight;
        }
        this.nextEnergy[o] = r; this.nextEnergy[o + 1] = g; this.nextEnergy[o + 2] = b;
        this.nextDirection[o] = dr; this.nextDirection[o + 1] = dg; this.nextDirection[o + 2] = db;
      }
      [this.energy, this.nextEnergy] = [this.nextEnergy, this.energy];
      [this.direction, this.nextDirection] = [this.nextDirection, this.direction];
    }
  }

  sample(x, y, z, out) {
    const cell = this.pointToCell(x, y, z);
    if (cell < 0) { out[0] = out[1] = out[2] = 0; return out; }
    const o = cell * 3;
    out[0] = this.energy[o]; out[1] = this.energy[o + 1]; out[2] = this.energy[o + 2];
    return out;
  }
}

const cascades = CASCADE_SPECS.map((spec, index) => new LpvCascade(spec, index));

// Build surface elements over all wall faces; samples are transiently selected every GI update.
const surfaceSamples = [];
for (const block of blocks) {
  const step = 0.52;
  for (let y = 0.28; y < block.h - 0.1; y += 0.58) {
    for (let u = -block.d / 2 + step / 2; u < block.d / 2; u += step) {
      surfaceSamples.push({ x: block.x - block.w / 2 - 0.018, y, z: block.z + u, nx: -1, ny: 0, nz: 0, albedo: block.rgb });
      surfaceSamples.push({ x: block.x + block.w / 2 + 0.018, y, z: block.z + u, nx: 1, ny: 0, nz: 0, albedo: block.rgb });
    }
    for (let u = -block.w / 2 + step / 2; u < block.w / 2; u += step) {
      surfaceSamples.push({ x: block.x + u, y, z: block.z - block.d / 2 - 0.018, nx: 0, ny: 0, nz: -1, albedo: block.rgb });
      surfaceSamples.push({ x: block.x + u, y, z: block.z + block.d / 2 + 0.018, nx: 0, ny: 0, nz: 1, albedo: block.rgb });
    }
  }
}

const currentSurfels = [];
const surfaceColor = new Float32Array(surfaceSamples.length * 3);
const pointPositions = new Float32Array(surfaceSamples.length * 3);
for (let i = 0; i < surfaceSamples.length; i++) {
  const s = surfaceSamples[i];
  pointPositions[i * 3] = s.x; pointPositions[i * 3 + 1] = s.y; pointPositions[i * 3 + 2] = s.z;
}

// Renderer and scene.
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.08;
renderer.outputColorSpace = THREE.SRGBColorSpace;

const scene = new THREE.Scene();
scene.background = new THREE.Color('#09101a');
scene.fog = new THREE.FogExp2('#09101a', 0.024);
const camera = new THREE.PerspectiveCamera(44, 1, 0.1, 110);
const initialCamera = new THREE.Vector3(13.8, 12.2, 16.8);
const initialTarget = new THREE.Vector3(-0.7, 1.0, -0.5);
camera.position.copy(initialCamera);

const controls = new OrbitControls(camera, canvas);
controls.target.copy(initialTarget);
controls.enableDamping = true;
controls.dampingFactor = 0.07;
controls.maxPolarAngle = Math.PI * 0.49;
controls.minDistance = 7;
controls.maxDistance = 39;
controls.update();

const textureBytes = new Uint8Array(TEX_RES * TEX_RES * 4);
const aoBytes = new Uint8Array(TEX_RES * TEX_RES * 4);
textureBytes.fill(0); aoBytes.fill(255);
const indirectTexture = new THREE.DataTexture(textureBytes, TEX_RES, TEX_RES, THREE.RGBAFormat);
indirectTexture.colorSpace = THREE.SRGBColorSpace;
indirectTexture.magFilter = THREE.LinearFilter;
indirectTexture.minFilter = THREE.LinearFilter;
indirectTexture.needsUpdate = true;
const aoTexture = new THREE.DataTexture(aoBytes, TEX_RES, TEX_RES, THREE.RGBAFormat);
aoTexture.magFilter = THREE.LinearFilter;
aoTexture.minFilter = THREE.LinearFilter;
aoTexture.needsUpdate = true;

const floorGeo = new THREE.PlaneGeometry(WORLD_SIZE, WORLD_SIZE, 1, 1);
floorGeo.setAttribute('uv2', floorGeo.attributes.uv.clone());
const floorMaterial = new THREE.MeshStandardMaterial({
  color: '#172332', roughness: 0.86, metalness: 0.02,
  emissive: '#ffffff', emissiveMap: indirectTexture, emissiveIntensity: 1.1,
  aoMap: aoTexture, aoMapIntensity: 0.92,
});
const floor = new THREE.Mesh(floorGeo, floorMaterial);
floor.rotation.x = -Math.PI / 2;
floor.receiveShadow = true;
scene.add(floor);

const grid = new THREE.GridHelper(WORLD_SIZE, 40, '#27384f', '#1b2b3e');
grid.position.y = 0.012;
grid.material.transparent = true;
grid.material.opacity = 0.28;
scene.add(grid);

const blockMaterials = [];
for (const block of blocks) {
  const material = new THREE.MeshStandardMaterial({ color: block.color, roughness: 0.73, metalness: 0.02 });
  blockMaterials.push(material);
  const mesh = new THREE.Mesh(new THREE.BoxGeometry(block.w, block.h, block.d), material);
  mesh.position.set(block.x, block.h / 2, block.z);
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  scene.add(mesh);
}

// Extra objects catch direct shadows and make the camera scene less diagram-like.
const props = [];
function makeProp(x, z, radius, color) {
  const mat = new THREE.MeshStandardMaterial({ color, roughness: 0.32, metalness: 0.08, emissive: '#000000' });
  const mesh = new THREE.Mesh(new THREE.IcosahedronGeometry(radius, 2), mat);
  mesh.position.set(x, radius + 0.04, z);
  mesh.castShadow = true; mesh.receiveShadow = true;
  scene.add(mesh); props.push({ mesh, material: mat, x, z });
}
makeProp(-3.8, 8.0, 0.9, '#b3d9ef');
makeProp(8.2, -1.8, 0.7, '#c1d6ee');
makeProp(-13.8, -10.5, 0.78, '#d7e1e8');

const hemi = new THREE.HemisphereLight('#7194c4', '#10151e', 0.36);
scene.add(hemi);
const sun = new THREE.DirectionalLight('#ffd5a8', 3.3);
sun.position.set(-8, 10.5, -8);
sun.castShadow = true;
sun.shadow.mapSize.set(1536, 1536);
sun.shadow.camera.left = -25; sun.shadow.camera.right = 25;
sun.shadow.camera.top = 25; sun.shadow.camera.bottom = -25;
sun.shadow.camera.near = 0.1; sun.shadow.camera.far = 40;
sun.shadow.bias = -0.00025;
scene.add(sun, sun.target);
sun.target.position.set(0, 0, 0);
const rim = new THREE.PointLight('#4c79ba', 2.5, 16, 2);
rim.position.set(-13, 5, 11);
scene.add(rim);

const orb = new THREE.Mesh(
  new THREE.SphereGeometry(0.18, 16, 10),
  new THREE.MeshBasicMaterial({ color: '#ffe0b5', transparent: true, opacity: 0.9 }),
);
scene.add(orb);

const surfelGeometry = new THREE.BufferGeometry();
surfelGeometry.setAttribute('position', new THREE.BufferAttribute(pointPositions, 3));
surfelGeometry.setAttribute('color', new THREE.BufferAttribute(surfaceColor, 3));
const surfelPoints = new THREE.Points(surfelGeometry, new THREE.PointsMaterial({
  size: 0.115, sizeAttenuation: true, vertexColors: true, transparent: true,
  opacity: 0.78, blending: THREE.AdditiveBlending, depthWrite: false,
}));
scene.add(surfelPoints);

/** GI texture and half-resolution SSGI buffers. */
const ssgi = { color: new Float32Array(SSGI_RES * SSGI_RES * 3), ao: new Float32Array(SSGI_RES * SSGI_RES) };
const tmpColorA = [0, 0, 0];
const tmpColorB = [0, 0, 0];
const tmpColorC = [0, 0, 0];
const sunDirection = new THREE.Vector3();
let giTick = 0;
let lastGiDuration = 0;
let lastGiUpdate = -Infinity;

function updateSun(time) {
  if (state.animateLight) {
    const a = time * 0.18 + 3.85;
    sun.position.set(Math.cos(a) * 11.5, 9.2 + Math.sin(a * 0.7) * 2.2, Math.sin(a) * 10.5);
  }
  sun.color.set(state.lightMode === 'sunset' ? '#ffd1a2' : '#d8eaff');
  sun.intensity = state.lightMode === 'sunset' ? 3.35 : 2.85;
  sunDirection.copy(sun.position).sub(sun.target.position).normalize();
  orb.position.copy(sun.position).multiplyScalar(0.82);
  orb.material.color.copy(sun.color);
  orb.scale.setScalar(state.lightMode === 'sunset' ? 1 : 0.82);
}

function rayBlocked(startX, startY, startZ, dirX, dirY, dirZ) {
  // Starts safely beyond the parent surface to avoid self-intersection in the dense occupancy buffer.
  const step = 0.38;
  for (let t = 0.42; t < 45; t += step) {
    const y = startY + dirY * t;
    if (y > 6.2) return false;
    const h = worldHeightAt(startX + dirX * t, startZ + dirZ * t);
    if (h > y + 0.05) return true;
  }
  return false;
}

function selectAndLightSurfels() {
  currentSurfels.length = 0;
  surfaceColor.fill(0);
  const count = Math.min(state.surfelBudget, surfaceSamples.length);
  const stride = surfaceSamples.length / count;
  const phase = (giTick * 37) % surfaceSamples.length;
  for (let n = 0; n < count; n++) {
    const id = Math.floor((phase + n * stride) % surfaceSamples.length);
    const s = surfaceSamples[id];
    const ndotl = Math.max(0, s.nx * sunDirection.x + s.ny * sunDirection.y + s.nz * sunDirection.z);
    if (ndotl < 0.025) continue;
    const blocked = rayBlocked(s.x + s.nx * 0.32, s.y + 0.05, s.z + s.nz * 0.32, sunDirection.x, sunDirection.y, sunDirection.z);
    if (blocked) continue;
    const direct = ndotl * sun.intensity * 0.43;
    const flux = [s.albedo[0] * direct, s.albedo[1] * direct, s.albedo[2] * direct];
    currentSurfels.push({ ...s, flux });
    const co = id * 3;
    surfaceColor[co] = Math.min(1, flux[0] * 1.35);
    surfaceColor[co + 1] = Math.min(1, flux[1] * 1.35);
    surfaceColor[co + 2] = Math.min(1, flux[2] * 1.35);
  }
  surfelGeometry.attributes.color.needsUpdate = true;
  $('#surfelReadout').textContent = `${currentSurfels.length} injected`;
}

function updateCascades() {
  for (const cascade of cascades) {
    cascade.recenter(camera.position.x, camera.position.z, state.cascades);
    for (const surfel of currentSurfels) cascade.inject(surfel, 0.49);
    cascade.propagate(state.propagation, state.blockers);
  }
}

function calcSsgi() {
  const dirs = [[1, 0], [-1, 0], [0, 1], [0, -1], [.707, .707], [-.707, .707], [.707, -.707], [-.707, -.707]];
  for (let z = 0; z < SSGI_RES; z++) {
    for (let x = 0; x < SSGI_RES; x++) {
      const wx = ((x + 0.5) / SSGI_RES) * WORLD_SIZE - WORLD_HALF;
      const wz = ((z + 0.5) / SSGI_RES) * WORLD_SIZE - WORLD_HALF;
      const pixel = z * SSGI_RES + x;
      const o = pixel * 3;
      if (worldHeightAt(wx, wz) > 0.1) { ssgi.color[o] = ssgi.color[o + 1] = ssgi.color[o + 2] = 0; ssgi.ao[pixel] = 0; continue; }
      let r = 0, g = 0, b = 0, occluded = 0;
      for (const [dx, dz] of dirs) {
        let hit = false;
        for (let t = 0.27; t < 2.9; t += 0.27) {
          const sx = wx + dx * t, sz = wz + dz * t;
          if (worldHeightAt(sx, sz) > 0.15) {
            worldColorAt(sx, sz, tmpColorA);
            const f = Math.pow(1 - t / 3.15, 1.7);
            r += tmpColorA[0] * f * 0.061;
            g += tmpColorA[1] * f * 0.061;
            b += tmpColorA[2] * f * 0.061;
            occluded += 1 - t / 3.15;
            hit = true;
            break;
          }
        }
        if (!hit) occluded += 0;
      }
      // GTAO values are generated at exactly half texture resolution and are used by floor.aoMap.
      ssgi.color[o] = r; ssgi.color[o + 1] = g; ssgi.color[o + 2] = b;
      ssgi.ao[pixel] = clamp(occluded * 0.11);
    }
  }
}

function compositeLpvAt(x, z, out) {
  const distance = Math.hypot(x - camera.position.x, z - camera.position.z);
  cascades[0].sample(x, 0.12, z, tmpColorA);
  cascades[1].sample(x, 0.12, z, tmpColorB);
  cascades[2].sample(x, 0.12, z, tmpColorC);
  // Blend the three overlapping clipmaps rather than visibly popping at their boundaries.
  const t0 = smoothstep(5.6, 8.7, distance);
  const t1 = smoothstep(13.0, 17.5, distance);
  const aR = tmpColorA[0] * (1 - t0) + tmpColorB[0] * t0;
  const aG = tmpColorA[1] * (1 - t0) + tmpColorB[1] * t0;
  const aB = tmpColorA[2] * (1 - t0) + tmpColorB[2] * t0;
  out[0] = aR * (1 - t1) + tmpColorC[0] * t1;
  out[1] = aG * (1 - t1) + tmpColorC[1] * t1;
  out[2] = aB * (1 - t1) + tmpColorC[2] * t1;
  return out;
}

function rebuildIndirectTexture() {
  if (state.ssgi) calcSsgi();
  for (let z = 0; z < TEX_RES; z++) {
    const sz = Math.min(SSGI_RES - 1, Math.floor(z * SSGI_RES / TEX_RES));
    for (let x = 0; x < TEX_RES; x++) {
      const wx = ((x + 0.5) / TEX_RES) * WORLD_SIZE - WORLD_HALF;
      const wz = ((z + 0.5) / TEX_RES) * WORLD_SIZE - WORLD_HALF;
      const i = z * TEX_RES + x;
      const o = i * 4;
      const so = (sz * SSGI_RES + Math.min(SSGI_RES - 1, Math.floor(x * SSGI_RES / TEX_RES))) * 3;
      compositeLpvAt(wx, wz, tmpColorA);
      const sr = state.ssgi ? ssgi.color[so] : 0;
      const sg = state.ssgi ? ssgi.color[so + 1] : 0;
      const sb = state.ssgi ? ssgi.color[so + 2] : 0;
      // ACES will tone-map the emissive texture afterwards; keep this physically modest.
      const r = Math.max(0, tmpColorA[0] * state.energy * 0.34 + sr * state.energy);
      const g = Math.max(0, tmpColorA[1] * state.energy * 0.34 + sg * state.energy);
      const b = Math.max(0, tmpColorA[2] * state.energy * 0.34 + sb * state.energy);
      textureBytes[o] = Math.round(clamp(Math.pow(r, 1 / 2.2)) * 255);
      textureBytes[o + 1] = Math.round(clamp(Math.pow(g, 1 / 2.2)) * 255);
      textureBytes[o + 2] = Math.round(clamp(Math.pow(b, 1 / 2.2)) * 255);
      textureBytes[o + 3] = 255;
      const ao = state.ssgi ? ssgi.ao[sz * SSGI_RES + Math.min(SSGI_RES - 1, Math.floor(x * SSGI_RES / TEX_RES))] : 0;
      const a = Math.round((1 - ao * 0.67) * 255);
      aoBytes[o] = aoBytes[o + 1] = aoBytes[o + 2] = a; aoBytes[o + 3] = 255;
    }
  }
  indirectTexture.needsUpdate = true;
  aoTexture.needsUpdate = true;
}

function updatePropLighting() {
  for (const prop of props) {
    compositeLpvAt(prop.x, prop.z, tmpColorA);
    prop.material.emissive.setRGB(tmpColorA[0] * state.energy * 0.11, tmpColorA[1] * state.energy * 0.11, tmpColorA[2] * state.energy * 0.11);
  }
}

function simulateGi() {
  const started = performance.now();
  giTick++;
  selectAndLightSurfels();
  updateCascades();
  rebuildIndirectTexture();
  updatePropLighting();
  lastGiDuration = performance.now() - started;
  $('#frameTime').textContent = `${lastGiDuration.toFixed(1)} ms`;
  $('#cascadeReadout').textContent = `${CASCADE_SPECS.map((c) => c.span).join(' / ')} m`;
  drawDebug();
}

function debugMapSpace(x, z, span = WORLD_SIZE, centerX = 0, centerZ = 0) {
  return [
    ((x - (centerX - span / 2)) / span) * debugCanvas.width,
    ((z - (centerZ - span / 2)) / span) * debugCanvas.height,
  ];
}
function drawBlocksTopdown(span = WORLD_SIZE, centerX = 0, centerZ = 0, alpha = 1) {
  for (const block of blocks) {
    const [x0, y0] = debugMapSpace(block.x - block.w / 2, block.z - block.d / 2, span, centerX, centerZ);
    const [x1, y1] = debugMapSpace(block.x + block.w / 2, block.z + block.d / 2, span, centerX, centerZ);
    debugCtx.fillStyle = `rgba(127, 146, 171, ${alpha})`;
    debugCtx.fillRect(x0, y0, x1 - x0, y1 - y0);
  }
}
function drawCascadeBounds() {
  const colors = ['#81a9ff', '#7ddfe9', '#d0adff'];
  for (let i = cascades.length - 1; i >= 0; i--) {
    const c = cascades[i];
    const [x, y] = debugMapSpace(c.origin.x, c.origin.z);
    const w = c.span / WORLD_SIZE * debugCanvas.width;
    const h = c.span / WORLD_SIZE * debugCanvas.height;
    debugCtx.strokeStyle = colors[i]; debugCtx.globalAlpha = 0.68 - i * .14; debugCtx.lineWidth = 1;
    debugCtx.strokeRect(x + .5, y + .5, w, h);
  }
  debugCtx.globalAlpha = 1;
}
function drawCameraMarker() {
  const [x, y] = debugMapSpace(camera.position.x, camera.position.z);
  debugCtx.strokeStyle = '#f3fbff'; debugCtx.lineWidth = 1;
  debugCtx.beginPath(); debugCtx.arc(x, y, 3, 0, Math.PI * 2); debugCtx.stroke();
  debugCtx.beginPath(); debugCtx.moveTo(x - 5, y); debugCtx.lineTo(x + 5, y); debugCtx.moveTo(x, y - 5); debugCtx.lineTo(x, y + 5); debugCtx.stroke();
}
function setDebugDescription(title, text, color) {
  $('#debugTitle').textContent = title;
  $('#debugDescription').textContent = text;
  $('#debugSwatch').style.background = color;
  $('#debugSwatch').style.boxShadow = `0 0 8px ${color}`;
}
function drawHeatMap(source, sourceW, sourceH, colorize = false) {
  const smallW = 108, smallH = 66;
  const image = debugCtx.createImageData(smallW, smallH);
  for (let y = 0; y < smallH; y++) for (let x = 0; x < smallW; x++) {
    const sx = Math.min(sourceW - 1, Math.floor(x / smallW * sourceW));
    const sy = Math.min(sourceH - 1, Math.floor(y / smallH * sourceH));
    const si = (sy * sourceW + sx) * 4;
    const di = (y * smallW + x) * 4;
    if (colorize) {
      image.data[di] = source[si]; image.data[di + 1] = source[si + 1]; image.data[di + 2] = source[si + 2];
    } else {
      const v = source[si] / 255;
      image.data[di] = Math.round(v * 85); image.data[di + 1] = Math.round(v * 204); image.data[di + 2] = Math.round(140 + v * 115);
    }
    image.data[di + 3] = 255;
  }
  const mini = document.createElement('canvas'); mini.width = smallW; mini.height = smallH;
  mini.getContext('2d').putImageData(image, 0, 0);
  debugCtx.imageSmoothingEnabled = true;
  debugCtx.drawImage(mini, 0, 0, debugCanvas.width, debugCanvas.height);
}

function drawDebug() {
  debugCtx.fillStyle = '#09111c'; debugCtx.fillRect(0, 0, debugCanvas.width, debugCanvas.height);
  const mode = state.debugMode;
  if (mode === 'final') {
    setDebugDescription('COMPOSITE INDIRECT', 'Diffuse energy gathered from three camera-relative volumes, plus screen-space contact bounce.', '#72a6ff');
    drawHeatMap(textureBytes, TEX_RES, TEX_RES, true);
    debugCtx.globalAlpha = 0.52; drawBlocksTopdown(); debugCtx.globalAlpha = 1; drawCascadeBounds();
  }
  if (mode === 'lpv') {
    setDebugDescription('LPV CLIPMAPS', 'A top-down projection of irradiance. Blue, cyan and violet rectangles are the three scrolling volume extents.', '#72e1ec');
    const cascade = cascades[0];
    const image = new Uint8Array(cascade.nx * cascade.nz * 4);
    for (let z = 0; z < cascade.nz; z++) for (let x = 0; x < cascade.nx; x++) {
      let r = 0, g = 0, b = 0;
      for (let y = 0; y < cascade.ny; y++) { const o = cascade.indexOf(x, y, z) * 3; r = Math.max(r, cascade.energy[o]); g = Math.max(g, cascade.energy[o + 1]); b = Math.max(b, cascade.energy[o + 2]); }
      const o = (z * cascade.nx + x) * 4;
      image[o] = clamp(Math.pow(r * .34, 1 / 2.2)) * 255; image[o + 1] = clamp(Math.pow(g * .34, 1 / 2.2)) * 255; image[o + 2] = clamp(Math.pow(b * .34, 1 / 2.2)) * 255; image[o + 3] = 255;
    }
    drawHeatMap(image, cascade.nx, cascade.nz, true); drawCascadeBounds();
  }
  if (mode === 'rsm') {
    setDebugDescription('TRANSIENT RSM SURFELS', 'Lit surface samples are reselected every update; only those with direct-light visibility inject the volumes.', '#ffb16b');
    drawBlocksTopdown();
    for (const surfel of currentSurfels) {
      const [x, y] = debugMapSpace(surfel.x, surfel.z);
      debugCtx.fillStyle = `rgb(${Math.round(surfel.flux[0] * 170)}, ${Math.round(surfel.flux[1] * 170)}, ${Math.round(surfel.flux[2] * 170)})`;
      debugCtx.fillRect(x, y, 1.35, 1.35);
    }
    drawCascadeBounds();
  }
  if (mode === 'blocker') {
    setDebugDescription('DIRECTIONAL BLOCKERS', 'The intentionally smaller blue blocker volume prevents LPV transport crossing solid cells along the propagated direction.', '#b6ff8c');
    const c = cascades[0];
    for (let z = 0; z < c.bz; z++) for (let y = 0; y < c.by; y++) for (let x = 0; x < c.bx; x++) {
      if (!c.blockerData[c.blockerIndex(x, y, z)]) continue;
      const bx = (x / c.bx) * debugCanvas.width;
      const by = (z / c.bz) * debugCanvas.height;
      debugCtx.fillStyle = `rgba(182,255,140,${0.10 + y / c.by * .10})`;
      debugCtx.fillRect(bx, by, debugCanvas.width / c.bx + .3, debugCanvas.height / c.bz + .3);
    }
    drawBlocksTopdown(c.span, c.center.x, c.center.y, .9);
    debugCtx.strokeStyle = 'rgba(182,255,140,.26)';
    for (let x = 0; x <= c.bx; x++) { const px = x / c.bx * debugCanvas.width; debugCtx.beginPath(); debugCtx.moveTo(px, 0); debugCtx.lineTo(px, debugCanvas.height); debugCtx.stroke(); }
    for (let z = 0; z <= c.bz; z++) { const py = z / c.bz * debugCanvas.height; debugCtx.beginPath(); debugCtx.moveTo(0, py); debugCtx.lineTo(debugCanvas.width, py); debugCtx.stroke(); }
  }
  if (mode === 'ssgi') {
    setDebugDescription('½-RES SSGI + GTAO', 'Short visible-space rays recover coloured contact bounce and generate the separate ambient-occlusion field.', '#d0adff');
    const image = new Uint8Array(SSGI_RES * SSGI_RES * 4);
    for (let i = 0; i < SSGI_RES * SSGI_RES; i++) {
      const so = i * 3, o = i * 4;
      image[o] = clamp(Math.pow(ssgi.color[so] * state.energy * 2.2, 1 / 2.2)) * 255;
      image[o + 1] = clamp(Math.pow(ssgi.color[so + 1] * state.energy * 2.2, 1 / 2.2)) * 255;
      image[o + 2] = clamp(Math.pow(ssgi.color[so + 2] * state.energy * 2.2, 1 / 2.2)) * 255;
      image[o + 3] = 255;
    }
    drawHeatMap(image, SSGI_RES, SSGI_RES, true); debugCtx.globalAlpha = .5; drawBlocksTopdown(); debugCtx.globalAlpha = 1;
  }
  drawCameraMarker();
}

function resize() {
  const box = canvas.parentElement.getBoundingClientRect();
  renderer.setSize(box.width, box.height, false);
  camera.aspect = box.width / box.height;
  camera.updateProjectionMatrix();
}
window.addEventListener('resize', resize);
resize();

// UI binding.
function setRangeFill(input) {
  const pct = ((+input.value - +input.min) / (+input.max - +input.min)) * 100;
  input.style.background = `linear-gradient(90deg, var(--accent) ${pct}%, #293752 ${pct}%)`;
}
for (const range of document.querySelectorAll('input[type="range"]')) setRangeFill(range);
$('#energy').addEventListener('input', (event) => { state.energy = +event.target.value; $('#energyOut').textContent = `${state.energy.toFixed(2)}×`; setRangeFill(event.target); lastGiUpdate = -Infinity; });
$('#propagation').addEventListener('input', (event) => { state.propagation = +event.target.value; $('#propagationOut').textContent = state.propagation; setRangeFill(event.target); lastGiUpdate = -Infinity; });
$('#surfelBudget').addEventListener('input', (event) => { state.surfelBudget = +event.target.value; $('#surfelBudgetOut').textContent = state.surfelBudget.toLocaleString(); setRangeFill(event.target); lastGiUpdate = -Infinity; });

for (const button of document.querySelectorAll('.debug-mode')) button.addEventListener('click', () => {
  document.querySelector('.debug-mode.selected').classList.remove('selected'); button.classList.add('selected'); state.debugMode = button.dataset.mode; drawDebug();
});
for (const toggle of document.querySelectorAll('.toggle')) toggle.addEventListener('click', () => {
  const key = toggle.dataset.toggle; state[key] = !state[key]; toggle.classList.toggle('on', state[key]); toggle.setAttribute('aria-pressed', state[key]); lastGiUpdate = -Infinity;
});
for (const button of document.querySelectorAll('.scene-button')) button.addEventListener('click', () => {
  document.querySelector('.scene-button.selected').classList.remove('selected'); button.classList.add('selected'); state.lightMode = button.dataset.light; lastGiUpdate = -Infinity;
});
$('#animateLight').addEventListener('click', () => {
  state.animateLight = !state.animateLight;
  $('#animateLight em').textContent = state.animateLight ? 'ON' : 'OFF';
  $('#animateLight').style.borderColor = state.animateLight ? 'rgba(114,225,236,.26)' : 'rgba(132,151,178,.26)';
  lastGiUpdate = -Infinity;
});
$('#pauseButton').addEventListener('click', () => {
  state.paused = !state.paused;
  $('#pauseButton span:last-child').textContent = state.paused ? 'Resume' : 'Pause';
  $('#pauseButton .pause-icon').style.borderInline = state.paused ? '0' : '2px solid var(--accent)';
  $('#pauseButton .pause-icon').style.borderTop = state.paused ? '0' : '';
  $('#pauseButton .pause-icon').style.width = state.paused ? '0' : '8px';
  $('#simStatus').textContent = state.paused ? 'FROZEN' : 'SIMULATING';
  $('#simStatus').style.color = state.paused ? '#a8b2c2' : '#b9d8bd';
});
$('#resetCamera').addEventListener('click', () => {
  camera.position.copy(initialCamera); controls.target.copy(initialTarget); controls.update(); lastGiUpdate = -Infinity;
});

const keys = new Set();
window.addEventListener('keydown', (event) => {
  if (['INPUT', 'TEXTAREA'].includes(document.activeElement?.tagName)) return;
  if (['KeyW', 'KeyA', 'KeyS', 'KeyD'].includes(event.code)) { keys.add(event.code); event.preventDefault(); }
});
window.addEventListener('keyup', (event) => keys.delete(event.code));
function moveCamera(dt) {
  if (!keys.size) return;
  const view = new THREE.Vector3(); camera.getWorldDirection(view); view.y = 0; view.normalize();
  const right = new THREE.Vector3(view.z, 0, -view.x).normalize();
  const delta = new THREE.Vector3();
  if (keys.has('KeyW')) delta.add(view); if (keys.has('KeyS')) delta.sub(view);
  if (keys.has('KeyD')) delta.add(right); if (keys.has('KeyA')) delta.sub(right);
  if (delta.lengthSq() > 0) { delta.normalize().multiplyScalar(dt * 7.2); camera.position.add(delta); controls.target.add(delta); }
}

let lastFrame = performance.now();
function animate(now) {
  requestAnimationFrame(animate);
  const dt = Math.min(0.05, (now - lastFrame) / 1000); lastFrame = now;
  moveCamera(dt);
  if (!state.paused) {
    updateSun(now / 1000);
    // GI is intentionally decoupled from presentation: 10 Hz gives temporal amortisation room on GTX hardware.
    if (now - lastGiUpdate > 96) { simulateGi(); lastGiUpdate = now; }
  }
  controls.update();
  renderer.render(scene, camera);
  $('#updateReadout').textContent = `${(1000 / 96).toFixed(0)} Hz GI update · ${currentSurfels.length} surfels`;
}

updateSun(0);
simulateGi();
requestAnimationFrame(animate);

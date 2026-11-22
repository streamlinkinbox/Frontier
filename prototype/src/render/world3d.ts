import {
  BoxGeometry,
  BufferAttribute,
  BufferGeometry,
  Color,
  CylinderGeometry,
  DoubleSide,
  Euler,
  Group,
  IcosahedronGeometry,
  InstancedMesh,
  Matrix4,
  Mesh,
  MeshStandardMaterial,
  Object3D,
  PlaneGeometry,
  Points,
  PointsMaterial,
  Quaternion,
  Scene,
  Vector3,
} from 'three';
import { clamp01, lerp } from '../core/mathx';
import { fbm2 } from '../core/noise';
import { SURFACES, VEHICLE } from '../sim/tuning';
import type { Track, TrackQuery } from '../sim/track';
import type { World } from '../sim/world';
import type { Vine } from '../sim/vines';
import { makeBarkTexture, makeDirtTexture, makeLeafTexture, makeSparkTexture } from './textures';

const ROAD_COLUMNS = 15;
const TERRAIN_OFFSETS = [0, 3.5, 8, 14, 22, 32];
const MAX_TREES = 320;
const MAX_ROCKS = 260;
const MAX_LOGS = 90;
const MAX_CANOPY = 900;
const MAX_VINES = 48;
const MAX_PARTICLES = 900;

const UP = new Vector3(0, 1, 0);
const tmpM = new Matrix4();
const tmpEuler = new Euler();
const tmpQ = new Quaternion();
const tmpV = new Vector3();
const tmpS = new Vector3();
const tmpC = new Color();

/** Shared wind uniform for every foliage material. */
const windUniform = { value: 0 };

function applyWind(material: MeshStandardMaterial, amount: number, heightScale = 1) {
  material.onBeforeCompile = (shader) => {
    shader.uniforms.uTime = windUniform;
    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', '#include <common>\nuniform float uTime;')
      .replace(
        '#include <begin_vertex>',
        `#include <begin_vertex>
        #ifdef USE_INSTANCING
          float seed = instanceMatrix[3][0] * 0.37 + instanceMatrix[3][2] * 0.21;
        #else
          float seed = 0.0;
        #endif
        float sway = sin(uTime * 1.15 + seed) * 0.6 + sin(uTime * 2.7 + seed * 1.7) * 0.25;
        transformed.x += sway * ${amount.toFixed(3)} * ${heightScale.toFixed(3)};`,
      );
  };
}

// ---------------------------------------------------------------------------
//  Vine tube rendering
// ---------------------------------------------------------------------------

class VineMesh {
  readonly mesh: Mesh;
  private geometry: BufferGeometry;
  private positions: Float32Array;
  private normals: Float32Array;
  private colors: Float32Array;
  private rings: number;
  private radial: number;
  private curvePoints: Vector3[] = [];

  constructor(rings: number, radial: number) {
    this.rings = rings;
    this.radial = radial;
    const vertCount = rings * (radial + 1);
    this.positions = new Float32Array(vertCount * 3);
    this.normals = new Float32Array(vertCount * 3);
    this.colors = new Float32Array(vertCount * 3);

    const indices: number[] = [];
    for (let r = 0; r < rings - 1; r++) {
      for (let i = 0; i < radial; i++) {
        const a = r * (radial + 1) + i;
        const b = a + radial + 1;
        indices.push(a, b, a + 1, b, b + 1, a + 1);
      }
    }

    this.geometry = new BufferGeometry();
    this.geometry.setAttribute('position', new BufferAttribute(this.positions, 3));
    this.geometry.setAttribute('normal', new BufferAttribute(this.normals, 3));
    this.geometry.setAttribute('color', new BufferAttribute(this.colors, 3));
    this.geometry.setIndex(indices);

    const material = new MeshStandardMaterial({
      vertexColors: true,
      roughness: 0.72,
      metalness: 0.0,
    });
    this.mesh = new Mesh(this.geometry, material);
    this.mesh.castShadow = true;
    this.mesh.frustumCulled = false;
    this.mesh.visible = false;
    for (let i = 0; i < rings; i++) this.curvePoints.push(new Vector3());
  }

  update(vine: Vine, time: number): void {
    const spec = vine.spec;
    const root = vine.root;
    const tip = vine.state === 'latched' || vine.state === 'creeping' ? vine.contactWorld : vine.tip;

    // ---- build the curve: a hanging catenary that straightens under tension
    const axis = tmpV.copy(tip).sub(root);
    const dist = Math.max(0.001, axis.length());
    axis.multiplyScalar(1 / dist);

    const slack = Math.min(1.6, Math.max(0, vine.length - dist));
    const gravity = new Vector3(0, -1, 0);
    const sagDir = gravity.clone().addScaledVector(axis, -gravity.dot(axis));
    if (sagDir.lengthSq() < 1e-6) sagDir.set(1, 0, 0).addScaledVector(axis, -axis.x);
    sagDir.normalize();

    const perp = new Vector3().crossVectors(axis, sagDir).normalize();
    const tensionRatio = clamp01(vine.tensionSmooth / spec.maxTension);
    const vib = tensionRatio * 0.035;

    for (let i = 0; i < this.rings; i++) {
      const t = i / (this.rings - 1);
      const p = this.curvePoints[i];
      p.copy(root).lerp(tip, t);
      const sag = Math.sin(Math.PI * t) * slack * 0.42;
      p.addScaledVector(sagDir, sag);
      // organic wobble
      const w = Math.sin(t * 9.3 + time * 1.7 + vine.phase) * (0.06 + slack * 0.05);
      p.addScaledVector(perp, w);
      if (vib > 0) {
        const v = Math.sin(t * 42 + time * 46) * vib;
        p.addScaledVector(perp, v);
      }
    }

    // ---- sweep the tube
    const radiusAt = (t: number) =>
      spec.tubeRadius * Math.pow(1 - 0.55 * t, 0.7) * (1 + 0.12 * Math.sin(11 * Math.PI * t));

    let normal = new Vector3(0, 1, 0);
    for (let r = 0; r < this.rings; r++) {
      const p = this.curvePoints[r];
      const next = this.curvePoints[Math.min(r + 1, this.rings - 1)];
      const prev = this.curvePoints[Math.max(r - 1, 0)];
      const tangent = tmpS.copy(next).sub(prev).normalize();
      if (Math.abs(tangent.dot(normal)) > 0.98) normal.set(1, 0, 0);
      normal.addScaledVector(tangent, -normal.dot(tangent)).normalize();
      const binormal = tmpV.crossVectors(tangent, normal).normalize();

      const t = r / (this.rings - 1);
      const radius = radiusAt(t);
      for (let i = 0; i <= this.radial; i++) {
        const a = (i / this.radial) * Math.PI * 2;
        const nx = Math.cos(a);
        const ny = Math.sin(a);
        const dir = new Vector3()
          .addScaledVector(normal, nx)
          .addScaledVector(binormal, ny)
          .normalize();
        const idx = (r * (this.radial + 1) + i) * 3;
        this.positions[idx] = p.x + dir.x * radius;
        this.positions[idx + 1] = p.y + dir.y * radius;
        this.positions[idx + 2] = p.z + dir.z * radius;
        this.normals[idx] = dir.x;
        this.normals[idx + 1] = dir.y;
        this.normals[idx + 2] = dir.z;

        // darker at the root, brighter and wetter at the tip
        const shade = 0.32 + 0.5 * t;
        tmpC.setRGB(shade * 0.55, shade, shade * 0.42);
        this.colors[idx] = tmpC.r;
        this.colors[idx + 1] = tmpC.g;
        this.colors[idx + 2] = tmpC.b;
      }
    }

    this.geometry.attributes.position.needsUpdate = true;
    this.geometry.attributes.normal.needsUpdate = true;
    this.geometry.attributes.color.needsUpdate = true;
    this.geometry.computeBoundingSphere();
  }
}

// ---------------------------------------------------------------------------
//  Particles
// ---------------------------------------------------------------------------

interface Particle {
  life: number;
  maxLife: number;
  size: number;
  color: Color;
}

class ParticleSystem {
  readonly points: Points;
  private positions: Float32Array;
  private velocities: Float32Array;
  private sizes: Float32Array;
  private particles: Particle[] = [];
  private cursor = 0;

  constructor() {
    this.positions = new Float32Array(MAX_PARTICLES * 3);
    this.velocities = new Float32Array(MAX_PARTICLES * 3);
    this.sizes = new Float32Array(MAX_PARTICLES);
    const geo = new BufferGeometry();
    geo.setAttribute('position', new BufferAttribute(this.positions, 3));
    geo.setAttribute('size', new BufferAttribute(this.sizes, 1));
    const mat = new PointsMaterial({
      size: 0.16,
      map: makeSparkTexture(),
      transparent: true,
      depthWrite: false,
      vertexColors: true,
      sizeAttenuation: true,
    });
    this.points = new Points(geo, mat);
    this.points.frustumCulled = false;
    for (let i = 0; i < MAX_PARTICLES; i++) {
      this.particles.push({ life: 0, maxLife: 1, size: 1, color: new Color() });
      this.positions[i * 3 + 1] = -9999;
    }
  }

  spawn(pos: Vector3, vel: Vector3, life: number, size: number, color: Color): void {
    const i = this.cursor;
    this.cursor = (this.cursor + 1) % MAX_PARTICLES;
    this.positions[i * 3] = pos.x;
    this.positions[i * 3 + 1] = pos.y;
    this.positions[i * 3 + 2] = pos.z;
    this.velocities[i * 3] = vel.x;
    this.velocities[i * 3 + 1] = vel.y;
    this.velocities[i * 3 + 2] = vel.z;
    this.sizes[i] = size;
    this.particles[i].life = life;
    this.particles[i].maxLife = life;
    this.particles[i].color.copy(color);
  }

  update(dt: number): void {
    for (let i = 0; i < MAX_PARTICLES; i++) {
      const p = this.particles[i];
      if (p.life <= 0) continue;
      p.life -= dt;
      const idx = i * 3;
      if (p.life <= 0) {
        this.positions[idx + 1] = -9999;
        continue;
      }
      this.velocities[idx + 1] -= 4.2 * dt;
      this.velocities[idx] *= 1 - 1.6 * dt;
      this.velocities[idx + 2] *= 1 - 1.6 * dt;
      this.positions[idx] += this.velocities[idx] * dt;
      this.positions[idx + 1] += this.velocities[idx + 1] * dt;
      this.positions[idx + 2] += this.velocities[idx + 2] * dt;
    }
    this.points.geometry.attributes.position.needsUpdate = true;
  }
}

// ---------------------------------------------------------------------------
//  World renderer
// ---------------------------------------------------------------------------

export class WorldRenderer {
  private scene: Scene;
  private track: Track;

  private road: Mesh;
  private terrain: Mesh;
  private treeTrunks: InstancedMesh;
  private treeCanopy: InstancedMesh;
  private treeCanopyB: InstancedMesh;
  private rocks: InstancedMesh;
  private logs: InstancedMesh;
  private canopy: InstancedMesh;
  private car: Group;
  private wheels: Group[] = [];
  private particles: ParticleSystem;
  private vineMeshes: VineMesh[] = [];

  private roadBuiltTo = -1e9;
  private terrainBuiltTo = -1e9;
  private canopyBuiltTo = -1e9;
  private dirtTexture;
  private leafTexture;

  constructor(scene: Scene, track: Track) {
    this.scene = scene;
    this.track = track;
    this.dirtTexture = makeDirtTexture();
    this.leafTexture = makeLeafTexture();
    const barkTexture = makeBarkTexture();

    // ---- road ribbon
    const roadGeo = new BufferGeometry();
    this.road = new Mesh(
      roadGeo,
      new MeshStandardMaterial({
        map: this.dirtTexture,
        vertexColors: true,
        roughness: 0.94,
        metalness: 0.0,
      }),
    );
    this.road.receiveShadow = true;
    this.road.frustumCulled = false;
    scene.add(this.road);

    // ---- surrounding terrain
    this.terrain = new Mesh(
      new BufferGeometry(),
      new MeshStandardMaterial({ vertexColors: true, roughness: 1.0, metalness: 0 }),
    );
    this.terrain.receiveShadow = true;
    this.terrain.frustumCulled = false;
    scene.add(this.terrain);

    // ---- vegetation
    const trunkGeo = new CylinderGeometry(0.26, 0.44, 1, 7, 1);
    trunkGeo.translate(0, 0.5, 0);
    this.treeTrunks = new InstancedMesh(
      trunkGeo,
      new MeshStandardMaterial({ map: barkTexture, color: 0x9c8a72, roughness: 0.95 }),
      MAX_TREES,
    );
    this.treeTrunks.castShadow = true;
    this.treeTrunks.receiveShadow = true;
    this.treeTrunks.frustumCulled = false;
    scene.add(this.treeTrunks);

    const canopyGeo = new IcosahedronGeometry(1, 1);
    const canopyMatA = new MeshStandardMaterial({ color: 0x2f5c34, roughness: 0.95, flatShading: true });
    applyWind(canopyMatA, 0.05);
    this.treeCanopy = new InstancedMesh(canopyGeo, canopyMatA, MAX_TREES);
    this.treeCanopy.castShadow = true;
    this.treeCanopy.frustumCulled = false;
    scene.add(this.treeCanopy);

    const canopyMatB = new MeshStandardMaterial({ color: 0x3f7038, roughness: 0.95, flatShading: true });
    applyWind(canopyMatB, 0.07);
    this.treeCanopyB = new InstancedMesh(canopyGeo, canopyMatB, MAX_TREES);
    this.treeCanopyB.castShadow = false;
    this.treeCanopyB.frustumCulled = false;
    scene.add(this.treeCanopyB);

    // ---- rocks
    const rockGeo = new IcosahedronGeometry(1, 1);
    this.displace(rockGeo, 0.28, 1.7);
    this.rocks = new InstancedMesh(
      rockGeo,
      new MeshStandardMaterial({ color: 0x6f6a60, roughness: 0.95, flatShading: true }),
      MAX_ROCKS,
    );
    this.rocks.castShadow = true;
    this.rocks.receiveShadow = true;
    this.rocks.frustumCulled = false;
    scene.add(this.rocks);

    // ---- logs
    const logGeo = new CylinderGeometry(0.34, 0.4, 4.2, 8);
    logGeo.rotateX(Math.PI / 2);
    this.logs = new InstancedMesh(
      logGeo,
      new MeshStandardMaterial({ map: barkTexture, color: 0x8b7a63, roughness: 0.95 }),
      MAX_LOGS,
    );
    this.logs.castShadow = true;
    this.logs.receiveShadow = true;
    this.logs.frustumCulled = false;
    scene.add(this.logs);

    // ---- overhead canopy cards
    const cardGeo = new PlaneGeometry(3.4, 3.4);
    const cardMat = new MeshStandardMaterial({
      map: this.leafTexture,
      alphaTest: 0.42,
      side: DoubleSide,
      roughness: 0.9,
      color: 0x9fc27a,
    });
    applyWind(cardMat, 0.22);
    this.canopy = new InstancedMesh(cardGeo, cardMat, MAX_CANOPY);
    this.canopy.frustumCulled = false;
    scene.add(this.canopy);

    // ---- particles
    this.particles = new ParticleSystem();
    scene.add(this.particles.points);

    // ---- vines
    for (let i = 0; i < MAX_VINES; i++) {
      const vm = new VineMesh(26, 6);
      scene.add(vm.mesh);
      this.vineMeshes.push(vm);
    }

    // ---- the car
    this.car = this.buildCar();
    scene.add(this.car);
  }

  private displace(geo: BufferGeometry, amount: number, freq: number): void {
    const pos = geo.attributes.position as BufferAttribute;
    for (let i = 0; i < pos.count; i++) {
      const x = pos.getX(i);
      const y = pos.getY(i);
      const z = pos.getZ(i);
      const n =
        fbm2(x * freq + 10, z * freq + y * freq, 3, 91) * 0.6 +
        fbm2(y * freq * 2 + 3, x * freq * 2 + 7, 2, 33) * 0.4;
      const s = 1 + n * amount;
      pos.setXYZ(i, x * s, y * s, z * s);
    }
    geo.computeVertexNormals();
  }

  private buildCar(): Group {
    const group = new Group();
    const paint = new MeshStandardMaterial({ color: 0xc1552b, roughness: 0.55, metalness: 0.25 });
    const dark = new MeshStandardMaterial({ color: 0x2a2724, roughness: 0.8, metalness: 0.3 });
    const glass = new MeshStandardMaterial({
      color: 0x1b2622,
      roughness: 0.15,
      metalness: 0.6,
      transparent: true,
      opacity: 0.75,
    });

    const addBox = (w: number, h: number, d: number, x: number, y: number, z: number, mat: MeshStandardMaterial) => {
      const m = new Mesh(new BoxGeometry(w, h, d), mat);
      m.position.set(x, y, z);
      m.castShadow = true;
      group.add(m);
      return m;
    };

    // chassis tub
    addBox(1.86, 0.42, 3.5, 0, -0.08, 0, paint);
    // bonnet
    addBox(1.76, 0.26, 1.3, 0, 0.22, 1.05, paint);
    // cabin
    addBox(1.66, 0.62, 1.35, 0, 0.5, -0.35, paint);
    const windscreen = addBox(1.5, 0.46, 0.08, 0, 0.62, 0.36, glass);
    windscreen.rotation.x = -0.42;
    // roll cage
    const cageMat = new MeshStandardMaterial({ color: 0x8d9298, roughness: 0.45, metalness: 0.7 });
    for (const x of [-0.82, 0.82]) {
      const bar = new Mesh(new CylinderGeometry(0.05, 0.05, 1.15, 6), cageMat);
      bar.position.set(x, 0.62, -0.1);
      bar.rotation.z = x > 0 ? -0.12 : 0.12;
      bar.castShadow = true;
      group.add(bar);
    }
    const roofBar = new Mesh(new CylinderGeometry(0.05, 0.05, 1.7, 6), cageMat);
    roofBar.rotation.z = Math.PI / 2;
    roofBar.position.set(0, 1.18, -0.1);
    roofBar.castShadow = true;
    group.add(roofBar);

    // front bumper + cutter blade
    addBox(1.95, 0.16, 0.22, 0, -0.14, 2.0, dark);
    const blade = addBox(1.5, 0.1, 0.1, 0, -0.1, 2.12, new MeshStandardMaterial({
      color: 0xffa733,
      roughness: 0.3,
      metalness: 0.8,
      emissive: 0x361d00,
    }));
    blade.name = 'blade';
    // rear
    addBox(1.8, 0.5, 0.2, 0, 0.1, -1.72, dark);
    // light bar
    addBox(1.3, 0.12, 0.12, 0, 0.86, 0.3, new MeshStandardMaterial({
      color: 0xfff0c0,
      emissive: 0xffd27a,
      emissiveIntensity: 1.6,
      roughness: 0.4,
    }));

    // wheels
    const tyreGeo = new CylinderGeometry(VEHICLE.wheelRadius, VEHICLE.wheelRadius, 0.3, 14);
    tyreGeo.rotateZ(Math.PI / 2);
    const tyreMat = new MeshStandardMaterial({ color: 0x171614, roughness: 0.95 });
    const rimGeo = new CylinderGeometry(0.19, 0.19, 0.32, 10);
    rimGeo.rotateZ(Math.PI / 2);
    const rimMat = new MeshStandardMaterial({ color: 0x9aa0a6, roughness: 0.35, metalness: 0.85 });
    for (let i = 0; i < 4; i++) {
      const wheel = new Group();
      const tyre = new Mesh(tyreGeo, tyreMat);
      const rim = new Mesh(rimGeo, rimMat);
      tyre.castShadow = true;
      wheel.add(tyre, rim);
      group.add(wheel);
      this.wheels.push(wheel);
    }

    return group;
  }

  // ------------------------------------------------------------------ update

  update(world: World, dt: number, elapsed: number): void {
    windUniform.value = elapsed;
    this.updateRoad(world);
    this.updateTerrain(world);
    this.updateProps(world);
    this.updateCanopy(world);
    this.updateVines(world, elapsed);
    this.updateCar(world, dt);
    this.particles.update(dt);
  }

  private updateRoad(world: World): void {
    const playerS = world.playerS;
    if (Math.abs(playerS - this.roadBuiltTo) < 60 && this.roadBuiltTo > 0) return;
    this.roadBuiltTo = playerS;

    const s0 = Math.max(0, playerS - 150);
    const s1 = playerS + 480;
    const step = 2.0;
    const rows = Math.ceil((s1 - s0) / step) + 1;
    const cols = ROAD_COLUMNS;

    const positions = new Float32Array(rows * cols * 3);
    const normals = new Float32Array(rows * cols * 3);
    const uvs = new Float32Array(rows * cols * 2);
    const colors = new Float32Array(rows * cols * 3);

    for (let r = 0; r < rows; r++) {
      const s = s0 + r * step;
      const sample = this.track.sampleAt(s);
      const surf = SURFACES[sample.surface];
      tmpC.setHex(surf.color);
      for (let c = 0; c < cols; c++) {
        const f = c / (cols - 1);
        const lateral = lerp(-sample.width, sample.width, f);
        const p = this.track.pointAt(s, lateral);
        const relief = fbm2(p.x * 0.33, p.z * 0.33, 2, 3) * 0.035;
        const idx = (r * cols + c) * 3;
        positions[idx] = p.x;
        positions[idx + 1] = p.y + relief;
        positions[idx + 2] = p.z;
        normals[idx] = 0;
        normals[idx + 1] = 1;
        normals[idx + 2] = 0;
        uvs[(r * cols + c) * 2] = lateral * 0.18;
        uvs[(r * cols + c) * 2 + 1] = s * 0.18;
        colors[idx] = tmpC.r;
        colors[idx + 1] = tmpC.g;
        colors[idx + 2] = tmpC.b;
      }
    }

    const indices: number[] = [];
    for (let r = 0; r < rows - 1; r++) {
      for (let c = 0; c < cols - 1; c++) {
        const a = r * cols + c;
        const b = a + cols;
        indices.push(a, b, a + 1, b, b + 1, a + 1);
      }
    }

    const geo = this.road.geometry;
    geo.setAttribute('position', new BufferAttribute(positions, 3));
    geo.setAttribute('normal', new BufferAttribute(normals, 3));
    geo.setAttribute('uv', new BufferAttribute(uvs, 2));
    geo.setAttribute('color', new BufferAttribute(colors, 3));
    geo.setIndex(indices);
    geo.computeVertexNormals();
    geo.computeBoundingSphere();
  }

  private updateTerrain(world: World): void {
    const playerS = world.playerS;
    if (Math.abs(playerS - this.terrainBuiltTo) < 60 && this.terrainBuiltTo > 0) return;
    this.terrainBuiltTo = playerS;

    const s0 = Math.max(0, playerS - 150);
    const s1 = playerS + 420;
    const step = 4.0;
    const rows = Math.ceil((s1 - s0) / step) + 1;
    const offsets = TERRAIN_OFFSETS;
    const cols = offsets.length * 2;

    const positions = new Float32Array(rows * cols * 3);
    const colors = new Float32Array(rows * cols * 3);

    for (let r = 0; r < rows; r++) {
      const s = s0 + r * step;
      const sample = this.track.sampleAt(s);
      for (let c = 0; c < cols; c++) {
        const half = c < offsets.length ? -1 : 1;
        const oi = c < offsets.length ? offsets.length - 1 - c : c - offsets.length;
        const lateral = half * (sample.width + offsets[oi]);
        const p = this.track.pointAt(s, lateral);
        const q: TrackQuery = {
          s,
          lateral,
          centreY: sample.pos.y,
          bank: sample.bank,
          width: sample.width,
          curvature: sample.curvature,
          surface: sample.surface,
        };
        const y = this.track.heightFromQuery(p.x, p.z, q);
        const idx = (r * cols + c) * 3;
        positions[idx] = p.x;
        positions[idx + 1] = y;
        positions[idx + 2] = p.z;
        const green = clamp01(0.5 + fbm2(p.x * 0.05, p.z * 0.05, 2, 21) * 0.6);
        tmpC.setRGB(lerp(0.16, 0.24, green), lerp(0.22, 0.3, green), lerp(0.11, 0.15, green));
        colors[idx] = tmpC.r;
        colors[idx + 1] = tmpC.g;
        colors[idx + 2] = tmpC.b;
      }
    }

    const indices: number[] = [];
    for (let r = 0; r < rows - 1; r++) {
      for (let c = 0; c < cols - 1; c++) {
        const a = r * cols + c;
        const b = a + cols;
        indices.push(a, b, a + 1, b, b + 1, a + 1);
      }
    }

    const geo = this.terrain.geometry;
    geo.setAttribute('position', new BufferAttribute(positions, 3));
    geo.setAttribute('color', new BufferAttribute(colors, 3));
    geo.setIndex(indices);
    geo.computeVertexNormals();
    geo.computeBoundingSphere();
  }

  private updateProps(world: World): void {
    const playerS = world.playerS;
    let treeCount = 0;
    let rockCount = 0;
    let logCount = 0;

    for (const prop of world.props) {
      const rel = prop.s - playerS;
      if (rel < -80 || rel > 300) continue;
      const groundY = this.track.heightAt(prop.x, prop.z);

      if (prop.kind === 'tree') {
        if (treeCount >= MAX_TREES) continue;
        tmpM.compose(
          tmpV.set(prop.x, groundY - 0.2, prop.z),
          tmpQ.setFromAxisAngle(UP, prop.rot),
          tmpS.set(prop.scale, prop.h * prop.scale, prop.scale),
        );
        this.treeTrunks.setMatrixAt(treeCount, tmpM);
        tmpM.compose(
          tmpV.set(prop.x, groundY + prop.h * prop.scale * 0.86, prop.z),
          tmpQ.setFromAxisAngle(UP, prop.rot * 1.7),
          tmpS.set(prop.scale * 2.6, prop.scale * 1.5, prop.scale * 2.6),
        );
        this.treeCanopy.setMatrixAt(treeCount, tmpM);
        tmpM.compose(
          tmpV.set(prop.x + 0.6, groundY + prop.h * prop.scale * 0.72, prop.z - 0.5),
          tmpQ.setFromAxisAngle(UP, prop.rot * 2.9),
          tmpS.set(prop.scale * 2.1, prop.scale * 1.2, prop.scale * 2.1),
        );
        this.treeCanopyB.setMatrixAt(treeCount, tmpM);
        treeCount++;
      } else if (prop.kind === 'rock') {
        if (rockCount >= MAX_ROCKS) continue;
        tmpM.compose(
          tmpV.set(prop.x, groundY - prop.r * 0.35, prop.z),
          tmpQ.setFromEuler(tmpEuler.set(0.35, prop.rot, 0.2, 'YXZ')),
          tmpS.set(prop.r, prop.r * prop.h * 0.8, prop.r),
        );
        this.rocks.setMatrixAt(rockCount, tmpM);
        rockCount++;
      } else {
        if (logCount >= MAX_LOGS) continue;
        // logs are capsules: the instance is scaled to the trunk length and
        // turned to match the axis the physics uses
        const len = prop.halfLen * 2 + 0.4;
        tmpM.compose(
          tmpV.set(prop.x, groundY + prop.r * 0.8, prop.z),
          tmpQ.setFromAxisAngle(UP, prop.rot),
          tmpS.set(1, 1, len / 4.2),
        );
        this.logs.setMatrixAt(logCount, tmpM);
        logCount++;
      }
    }

    this.treeTrunks.count = treeCount;
    this.treeCanopy.count = treeCount;
    this.treeCanopyB.count = treeCount;
    this.rocks.count = rockCount;
    this.logs.count = logCount;
    this.treeTrunks.instanceMatrix.needsUpdate = true;
    this.treeCanopy.instanceMatrix.needsUpdate = true;
    this.treeCanopyB.instanceMatrix.needsUpdate = true;
    this.rocks.instanceMatrix.needsUpdate = true;
    this.logs.instanceMatrix.needsUpdate = true;
  }

  private updateCanopy(world: World): void {
    const playerS = world.playerS;
    if (Math.abs(playerS - this.canopyBuiltTo) < 40 && this.canopyBuiltTo > 0) return;
    this.canopyBuiltTo = playerS;

    const s0 = Math.max(0, playerS - 60);
    const s1 = playerS + 260;
    const step = 3.2;
    let count = 0;
    for (let s = s0; s < s1 && count < MAX_CANOPY; s += step) {
      const sample = this.track.sampleAt(s);
      const perRow = 4;
      for (let i = 0; i < perRow && count < MAX_CANOPY; i++) {
        const lateral = (Math.random() - 0.5) * sample.width * 2.6;
        const height = 5.5 + Math.random() * 8;
        const p = this.track.pointAt(s, lateral);
        tmpM.compose(
          tmpV.set(p.x, p.y + height, p.z),
          tmpQ.setFromEuler(
            tmpEuler.set(Math.random() * 0.5 - 0.25, Math.random() * Math.PI, 0, 'XYZ'),
          ),
          tmpS.set(1.4 + Math.random() * 1.4, 1.4 + Math.random() * 1.4, 1),
        );
        this.canopy.setMatrixAt(count, tmpM);
        count++;
      }
    }
    this.canopy.count = count;
    this.canopy.instanceMatrix.needsUpdate = true;
  }

  private updateVines(world: World, elapsed: number): void {
    const vines = world.vines.vines;
    const n = Math.min(vines.length, MAX_VINES);
    for (let i = 0; i < MAX_VINES; i++) {
      const vm = this.vineMeshes[i];
      if (i < n) {
        vm.mesh.visible = true;
        vm.update(vines[i], elapsed);
      } else {
        vm.mesh.visible = false;
      }
    }
  }

  private updateCar(world: World, dt: number): void {
    const v = world.vehicle;
    this.car.position.copy(v.pos);
    this.car.quaternion.copy(v.quat);

    const mounts = v.wheels;
    for (let i = 0; i < 4; i++) {
      const w = this.wheels[i];
      const m = mounts[i];
      w.position.copy(m.centre).sub(v.pos).applyQuaternion(v.quat.clone().invert());
      w.rotation.set(0, m.steer, 0);
      const tyre = w.children[0] as Mesh;
      tyre.rotation.x = -m.spin;
    }

    // dust and debris from the wheels
    if (Math.random() < 0.55) {
      for (const w of mounts) {
        if (!w.grounded) continue;
        const speed = v.speed;
        if (speed < 2) continue;
        const p = w.contact.clone();
        p.y += 0.1;
        const vel = new Vector3(
          (Math.random() - 0.5) * 2 + v.vel.x * 0.15,
          1.2 + Math.random() * 2.2,
          (Math.random() - 0.5) * 2 + v.vel.z * 0.15,
        );
        const surf = w.surfaceMu;
        const color = new Color(surf < 0.8 ? 0x4a3a24 : 0x8a7a5e);
        this.particles.spawn(p, vel, 0.6 + Math.random() * 0.7, 0.16 + Math.random() * 0.2, color);
      }
    }
    void dt;
  }

  // --------------------------------------------------------------- effects

  burstLeaves(pos: Vector3, strength: number): void {
    const count = Math.floor(6 + strength * 12);
    for (let i = 0; i < count; i++) {
      const vel = new Vector3(
        (Math.random() - 0.5) * 6 * strength,
        1 + Math.random() * 4,
        (Math.random() - 0.5) * 6 * strength,
      );
      const color = new Color(0x6f9a3f).offsetHSL(0, 0, (Math.random() - 0.5) * 0.15);
      this.particles.spawn(pos.clone(), vel, 0.9 + Math.random() * 1.1, 0.12 + Math.random() * 0.16, color);
    }
  }

  burstSap(pos: Vector3, strength: number): void {
    for (let i = 0; i < Math.floor(4 + strength * 8); i++) {
      const vel = new Vector3(
        (Math.random() - 0.5) * 4,
        Math.random() * 3,
        (Math.random() - 0.5) * 4,
      );
      this.particles.spawn(pos.clone(), vel, 0.4 + Math.random() * 0.5, 0.07 + Math.random() * 0.08, new Color(0x9fd45a));
    }
  }

  dust(pos: Vector3, strength: number): void {
    for (let i = 0; i < Math.floor(4 + strength * 10); i++) {
      const vel = new Vector3(
        (Math.random() - 0.5) * 3,
        0.6 + Math.random() * 1.6,
        (Math.random() - 0.5) * 3,
      );
      this.particles.spawn(pos.clone(), vel, 0.7 + Math.random() * 0.8, 0.2 + Math.random() * 0.3, new Color(0x8d7c5f));
    }
  }

  get particleSystem(): ParticleSystem {
    return this.particles;
  }

  get sceneRef(): Scene {
    return this.scene;
  }

  get objectRoot(): Object3D {
    return this.car;
  }
}

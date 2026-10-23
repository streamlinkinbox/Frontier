/**
 * three.js viewport: PBR solid-fill preview with image-based lighting, ground
 * plane, cascaded-looking single shadow map, quad-wireframe overlay and a
 * vertex wind shader shared by the branch mesh and the leaf cards.
 */

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { RoomEnvironment } from 'three/examples/jsm/environments/RoomEnvironment.js';
import { GpuBuffers } from '../tree/export';
import { LeafMesh } from '../tree/mesh';
import { WIND_ATTRIBUTES, WIND_FUNCTIONS, WIND_UNIFORMS } from './shaders';
import type { ObstacleKind } from '../env/environment';

export interface ObstacleGeometry {
  kind: ObstacleKind;
  positions: Float32Array;
  normals: Float32Array;
  indices: Uint32Array;
}

export interface PlacementEvent {
  /** Index of the obstacle being moved, or -1 when a new one is being placed. */
  index: number;
  x: number;
  z: number;
  /** 'move' while dragging, 'end' on release, 'add' for a click on empty ground in add mode. */
  phase: 'move' | 'end' | 'add';
}

export type DisplayMode = 'shaded' | 'wireframe' | 'levels' | 'wind' | 'junctions' | 'matcap';

export interface ViewerSettings {
  mode: DisplayMode;
  showWire: boolean;
  showLeaves: boolean;
  windEnabled: boolean;
  windStrength: number;
  windGust: number;
  windDirection: number;
  trunkFlex: number;
  limbFlex: number;
  detailFlex: number;
  autoRotate: boolean;
  showGrid: boolean;
  showObstacles: boolean;
  /** Draw the ground semi-transparent so buried roots stay visible. */
  xrayGround: boolean;
}

export const DEFAULT_VIEW: ViewerSettings = {
  mode: 'shaded',
  showWire: false,
  showLeaves: true,
  windEnabled: true,
  windStrength: 1.0,
  windGust: 0.4,
  windDirection: 35,
  trunkFlex: 1,
  limbFlex: 1,
  detailFlex: 1,
  autoRotate: false,
  showGrid: true,
  showObstacles: true,
  xrayGround: false,
};

const BARK = new THREE.Color(0x6e6258);
const LEAF = new THREE.Color(0x5f7c3a);
const ROCK = new THREE.Color(0x5b5c5e);
const BLOCK = new THREE.Color(0x6b665c);
const ROCK_HOVER = new THREE.Color(0x7d8390);
const DESERT_STEM = new THREE.Color(0x527342);
const DESERT_LEAF = new THREE.Color(0x78994f);
const DESERT_ALOE = new THREE.Color(0x4f7e5e);
const DESERT_SAND = new THREE.Color(0x252018);

export class Viewer {
  readonly renderer: THREE.WebGLRenderer;
  readonly scene = new THREE.Scene();
  readonly camera: THREE.PerspectiveCamera;
  readonly controls: OrbitControls;
  private clock = new THREE.Clock();
  private uniforms: Record<string, THREE.IUniform>;
  private treeGroup = new THREE.Group();
  private branchMesh: THREE.Mesh | null = null;
  private wireMesh: THREE.LineSegments | null = null;
  private leafMesh: THREE.Mesh | null = null;
  private obstacleGroup = new THREE.Group();
  private obstacleMeshes: THREE.Mesh[] = [];
  private ground: THREE.Mesh;
  private groundMat: THREE.MeshStandardMaterial;
  private grid: THREE.GridHelper;
  private raycaster = new THREE.Raycaster();
  private groundPlane = new THREE.Plane(new THREE.Vector3(0, 1, 0), 0);
  private hoverIndex = -1;
  private dragIndex = -1;
  private dragOffset = new THREE.Vector2();
  private placing = false;
  private placementListeners: ((e: PlacementEvent) => void)[] = [];
  private sun: THREE.DirectionalLight;
  private settings: ViewerSettings = { ...DEFAULT_VIEW };
  private treeHeight = 10;
  private pmrem: THREE.PMREMGenerator;
  private matcapTexture: THREE.Texture;
  private disposed = false;
  private frameCallbacks: ((dt: number) => void)[] = [];
  private lastFpsTime = 0;
  private frames = 0;
  fps = 0;

  constructor(private readonly canvas: HTMLCanvasElement) {
    this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: false, powerPreference: 'high-performance' });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.0;
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;

    this.scene.background = new THREE.Color(0x0c0d10);
    this.scene.fog = new THREE.Fog(0x0c0d10, 60, 220);

    this.pmrem = new THREE.PMREMGenerator(this.renderer);
    this.scene.environment = this.pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
    this.scene.environmentIntensity = 0.55;

    this.camera = new THREE.PerspectiveCamera(38, 1, 0.05, 600);
    this.camera.position.set(18, 9, 24);

    this.controls = new OrbitControls(this.camera, canvas);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.08;
    this.controls.target.set(0, 6, 0);
    this.controls.maxPolarAngle = Math.PI * 0.52;
    this.controls.minDistance = 0.05;
    this.controls.maxDistance = 300;

    this.sun = new THREE.DirectionalLight(0xfff1dc, 3.2);
    this.sun.position.set(30, 45, 20);
    this.sun.castShadow = true;
    this.sun.shadow.mapSize.set(4096, 4096);
    this.sun.shadow.bias = -0.0004;
    this.sun.shadow.normalBias = 0.02;
    this.sun.shadow.radius = 3;
    this.scene.add(this.sun);
    this.scene.add(this.sun.target);
    const hemi = new THREE.HemisphereLight(0xbcd0e8, 0x3a3229, 0.55);
    this.scene.add(hemi);

    this.groundMat = new THREE.MeshStandardMaterial({ color: 0x1a1c20, roughness: 1, metalness: 0 });
    this.ground = new THREE.Mesh(new THREE.CircleGeometry(400, 64), this.groundMat);
    this.ground.rotation.x = -Math.PI / 2;
    this.ground.receiveShadow = true;
    this.scene.add(this.ground);
    this.scene.add(this.obstacleGroup);

    this.grid = new THREE.GridHelper(80, 80, 0x3b3e46, 0x2a2d34);
    (this.grid.material as THREE.Material).transparent = true;
    (this.grid.material as THREE.Material).opacity = 0.55;
    this.grid.position.y = 0.002;
    this.scene.add(this.grid);

    this.scene.add(this.treeGroup);

    this.uniforms = {
      uTime: { value: 0 },
      uWindDir: { value: new THREE.Vector3(1, 0, 0.4).normalize() },
      uWindStrength: { value: 1 },
      uWindGust: { value: 0.4 },
      uTreeHeight: { value: 10 },
      uTrunkFlex: { value: 1 },
      uLimbFlex: { value: 1 },
      uDetailFlex: { value: 1 },
    };

    this.matcapTexture = makeMatcap();

    window.addEventListener('resize', this.resize);
    canvas.addEventListener('pointermove', this.onPointerMove, { capture: true });
    canvas.addEventListener('pointerdown', this.onPointerDown, { capture: true });
    canvas.addEventListener('pointerup', this.onPointerUp, { capture: true });
    canvas.addEventListener('pointercancel', this.onPointerUp, { capture: true });
    this.resize();
    this.renderer.setAnimationLoop(this.tick);
  }

  // ---------------------------------------------------------------------------
  // Obstacles & placement
  // ---------------------------------------------------------------------------

  onPlacement(cb: (e: PlacementEvent) => void): void {
    this.placementListeners.push(cb);
  }

  /** Enter/leave "click on the ground to add an object" mode. */
  setPlacing(on: boolean): void {
    this.placing = on;
    this.canvas.style.cursor = on ? 'crosshair' : '';
  }

  get isPlacing(): boolean {
    return this.placing;
  }

  setObstacles(obstacles: ObstacleGeometry[]): void {
    for (const m of this.obstacleMeshes) {
      this.obstacleGroup.remove(m);
      m.geometry.dispose();
      (m.material as THREE.Material).dispose();
    }
    this.obstacleMeshes = [];
    for (const o of obstacles) {
      const geo = new THREE.BufferGeometry();
      geo.setAttribute('position', new THREE.BufferAttribute(o.positions, 3));
      geo.setAttribute('normal', new THREE.BufferAttribute(o.normals, 3));
      geo.setIndex(new THREE.BufferAttribute(o.indices, 1));
      geo.computeBoundingSphere();
      const mat = new THREE.MeshStandardMaterial({ color: o.kind === 'rock' ? ROCK : BLOCK, roughness: 0.95, metalness: 0, flatShading: false });
      const m = new THREE.Mesh(geo, mat);
      m.castShadow = true;
      m.receiveShadow = true;
      m.userData.kind = o.kind;
      this.obstacleGroup.add(m);
      this.obstacleMeshes.push(m);
    }
    this.obstacleGroup.visible = this.settings.showObstacles;
    this.setHover(this.hoverIndex < obstacles.length ? this.hoverIndex : -1);
  }

  /** Move an obstacle preview mesh without regenerating (used while dragging). */
  offsetObstacle(index: number, dx: number, dz: number): void {
    const m = this.obstacleMeshes[index];
    if (m) m.position.set(dx, 0, dz);
  }

  private pointerNdc(ev: PointerEvent): THREE.Vector2 {
    const r = this.canvas.getBoundingClientRect();
    return new THREE.Vector2(((ev.clientX - r.left) / r.width) * 2 - 1, -((ev.clientY - r.top) / r.height) * 2 + 1);
  }

  private groundPoint(ev: PointerEvent): THREE.Vector3 | null {
    this.raycaster.setFromCamera(this.pointerNdc(ev), this.camera);
    const p = new THREE.Vector3();
    return this.raycaster.ray.intersectPlane(this.groundPlane, p) ? p : null;
  }

  private pickObstacle(ev: PointerEvent): number {
    if (!this.settings.showObstacles || this.obstacleMeshes.length === 0) return -1;
    this.raycaster.setFromCamera(this.pointerNdc(ev), this.camera);
    const hits = this.raycaster.intersectObjects(this.obstacleMeshes, false);
    if (!hits.length) return -1;
    return this.obstacleMeshes.indexOf(hits[0].object as THREE.Mesh);
  }

  private setHover(i: number): void {
    if (i === this.hoverIndex) return;
    const prev = this.obstacleMeshes[this.hoverIndex];
    if (prev) (prev.material as THREE.MeshStandardMaterial).color.copy(prev.userData.kind === 'rock' ? ROCK : BLOCK);
    this.hoverIndex = i;
    const cur = this.obstacleMeshes[i];
    if (cur) (cur.material as THREE.MeshStandardMaterial).color.copy(ROCK_HOVER);
    if (!this.placing) this.canvas.style.cursor = i >= 0 ? 'grab' : '';
  }

  private onPointerMove = (ev: PointerEvent): void => {
    if (this.dragIndex >= 0) {
      const p = this.groundPoint(ev);
      if (!p) return;
      const x = p.x - this.dragOffset.x;
      const z = p.z - this.dragOffset.y;
      for (const cb of this.placementListeners) cb({ index: this.dragIndex, x, z, phase: 'move' });
      return;
    }
    this.setHover(this.pickObstacle(ev));
  };

  private onPointerDown = (ev: PointerEvent): void => {
    if (ev.button !== 0) return;
    if (this.placing) {
      const p = this.groundPoint(ev);
      if (p) for (const cb of this.placementListeners) cb({ index: -1, x: p.x, z: p.z, phase: 'add' });
      ev.preventDefault();
      ev.stopImmediatePropagation();
      return;
    }
    const i = this.pickObstacle(ev);
    if (i < 0) return;
    const p = this.groundPoint(ev);
    if (!p) return;
    const m = this.obstacleMeshes[i];
    const c = m.geometry.boundingSphere!.center;
    this.dragOffset.set(p.x - c.x, p.z - c.z);
    this.dragIndex = i;
    this.controls.enabled = false;
    this.canvas.style.cursor = 'grabbing';
    this.canvas.setPointerCapture(ev.pointerId);
    ev.preventDefault();
    ev.stopImmediatePropagation();
  };

  private onPointerUp = (ev: PointerEvent): void => {
    if (this.dragIndex < 0) return;
    const i = this.dragIndex;
    this.dragIndex = -1;
    this.controls.enabled = true;
    this.canvas.style.cursor = this.placing ? 'crosshair' : 'grab';
    if (this.canvas.hasPointerCapture(ev.pointerId)) this.canvas.releasePointerCapture(ev.pointerId);
    const p = this.groundPoint(ev);
    if (p) for (const cb of this.placementListeners) cb({ index: i, x: p.x - this.dragOffset.x, z: p.z - this.dragOffset.y, phase: 'end' });
  };

  onFrame(cb: (dt: number) => void): void {
    this.frameCallbacks.push(cb);
  }

  dispose(): void {
    this.disposed = true;
    this.renderer.setAnimationLoop(null);
    window.removeEventListener('resize', this.resize);
  }

  private resize = (): void => {
    const parent = this.canvas.parentElement ?? document.body;
    const w = parent.clientWidth;
    const h = parent.clientHeight;
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / Math.max(1, h);
    this.camera.updateProjectionMatrix();
  };

  private tick = (): void => {
    if (this.disposed) return;
    const dt = this.clock.getDelta();
    const t = this.clock.elapsedTime;
    this.uniforms.uTime.value = this.settings.windEnabled ? t : 0;
    this.controls.autoRotate = this.settings.autoRotate;
    this.controls.update();
    for (const cb of this.frameCallbacks) cb(dt);
    this.renderer.render(this.scene, this.camera);
    this.frames++;
    if (t - this.lastFpsTime > 0.5) {
      this.fps = this.frames / (t - this.lastFpsTime);
      this.frames = 0;
      this.lastFpsTime = t;
    }
  };

  applySettings(s: ViewerSettings): void {
    this.settings = { ...s };
    this.uniforms.uWindStrength.value = s.windEnabled ? s.windStrength : 0;
    this.uniforms.uWindGust.value = s.windGust;
    const a = (s.windDirection * Math.PI) / 180;
    (this.uniforms.uWindDir.value as THREE.Vector3).set(Math.cos(a), 0, Math.sin(a));
    this.uniforms.uTrunkFlex.value = s.trunkFlex;
    this.uniforms.uLimbFlex.value = s.limbFlex;
    this.uniforms.uDetailFlex.value = s.detailFlex;
    if (this.wireMesh) this.wireMesh.visible = s.showWire || s.mode === 'wireframe';
    if (this.leafMesh) this.leafMesh.visible = s.showLeaves;
    this.grid.visible = s.showGrid;
    this.obstacleGroup.visible = s.showObstacles;
    this.groundMat.transparent = s.xrayGround;
    this.groundMat.opacity = s.xrayGround ? 0.35 : 1;
    this.groundMat.depthWrite = !s.xrayGround;
    this.groundMat.needsUpdate = true;
    if (this.branchMesh) {
      const mat = this.branchMesh.material as THREE.MeshStandardMaterial & { userData: { mode?: { value: number } } };
      mat.userData.mode!.value = modeIndex(s.mode);
      mat.wireframe = false;
      this.branchMesh.visible = true;
      if (s.mode === 'wireframe') {
        mat.userData.mode!.value = 6; // flat dark fill under the wire
      }
    }
  }

  setTree(buffers: GpuBuffers, leaves: LeafMesh | null, height: number, plantName = ''): void {
    this.clearTree();
    const desert = /Cactus|Agave|Aloe|Ocotillo|Sotol|Joshua Tree/i.test(plantName);
    const aloe = /Agave|Aloe|Sotol/i.test(plantName);
    const cactus = /Cactus|Joshua Tree/i.test(plantName);
    this.scene.background = desert ? new THREE.Color(0x171514) : new THREE.Color(0x0c0d10);
    (this.scene.fog as THREE.Fog).color.copy(desert ? new THREE.Color(0x171514) : new THREE.Color(0x0c0d10));
    this.groundMat.color.copy(desert ? DESERT_SAND : new THREE.Color(0x1a1c20));
    // Real height drives the framing (grasses can be a few centimetres tall);
    // the sway amplitude keeps a floor so small plants still visibly move.
    this.treeHeight = Math.max(0.05, height);
    this.uniforms.uTreeHeight.value = Math.max(0.5, height);

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(buffers.position, 3));
    geo.setAttribute('normal', new THREE.BufferAttribute(buffers.normal, 3));
    geo.setAttribute('uv', new THREE.BufferAttribute(buffers.uv, 2));
    geo.setAttribute('aWind', new THREE.BufferAttribute(buffers.wind, 4));
    geo.setAttribute('aPivot', new THREE.BufferAttribute(buffers.pivot, 3));
    geo.setAttribute('aLevel', new THREE.BufferAttribute(buffers.level, 1));
    geo.setAttribute('aJunction', new THREE.BufferAttribute(buffers.junction, 1));
    geo.setIndex(new THREE.BufferAttribute(buffers.index, 1));

    const mat = this.makeBarkMaterial(desert, cactus);
    this.branchMesh = new THREE.Mesh(geo, mat);
    this.branchMesh.castShadow = true;
    this.branchMesh.receiveShadow = true;
    this.branchMesh.frustumCulled = false;
    this.treeGroup.add(this.branchMesh);

    // Quad wireframe (true quad edges, not the triangulation).
    const wgeo = new THREE.BufferGeometry();
    wgeo.setAttribute('position', geo.getAttribute('position'));
    wgeo.setAttribute('aWind', geo.getAttribute('aWind'));
    wgeo.setAttribute('aPivot', geo.getAttribute('aPivot'));
    wgeo.setIndex(new THREE.BufferAttribute(buffers.edgeIndex, 1));
    const wmat = this.makeWireMaterial();
    this.wireMesh = new THREE.LineSegments(wgeo, wmat);
    this.wireMesh.frustumCulled = false;
    this.wireMesh.renderOrder = 2;
    this.treeGroup.add(this.wireMesh);

    if (leaves && leaves.count > 0) {
      const lgeo = new THREE.BufferGeometry();
      lgeo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(leaves.positions), 3));
      lgeo.setAttribute('normal', new THREE.BufferAttribute(new Float32Array(leaves.normals), 3));
      lgeo.setAttribute('uv', new THREE.BufferAttribute(new Float32Array(leaves.uvs), 2));
      lgeo.setAttribute('aWind', new THREE.BufferAttribute(new Float32Array(leaves.wind), 4));
      lgeo.setAttribute('aPivot', new THREE.BufferAttribute(new Float32Array(leaves.pivots), 3));
      lgeo.setIndex(new THREE.BufferAttribute(new Uint32Array(leaves.indices), 1));
      const lmat = this.makeLeafMaterial(desert, aloe);
      this.leafMesh = new THREE.Mesh(lgeo, lmat);
      this.leafMesh.castShadow = true;
      this.leafMesh.receiveShadow = true;
      this.leafMesh.frustumCulled = false;
      this.treeGroup.add(this.leafMesh);
    }

    // Shadow frustum & camera framing (tight around small plants so blades still get crisp shadows).
    const r = Math.max(1.2, this.treeHeight * 0.8);
    // (small plants keep the 1.2 m frustum: plenty of shadow-map resolution)
    const cam = this.sun.shadow.camera;
    cam.left = -r;
    cam.right = r;
    cam.top = r;
    cam.bottom = -r;
    cam.near = 1;
    cam.far = 200;
    cam.updateProjectionMatrix();
    this.sun.position.set(r * 1.2, r * 2.2, r * 0.9);
    this.sun.target.position.set(0, this.treeHeight * 0.4, 0);
    this.applySettings(this.settings);
  }

  /**
   * Fit the whole plant in view, from the standard three-quarter direction:
   * a 60 m kapok and a 10 cm turf both fill the frame. The fit is exact for
   * the visible geometry (vertices above ground) projected into the camera
   * frame; the 0.5 % most distant outliers (stray twig tips) may touch the edge.
   */
  frame(): void {
    const dir = new THREE.Vector3(0.7, 0.32, 1).normalize(); // target → camera
    const fwd = dir.clone().negate();
    const right = new THREE.Vector3().crossVectors(fwd, new THREE.Vector3(0, 1, 0)).normalize();
    const up = new THREE.Vector3().crossVectors(right, fwd);
    const tanV = Math.tan((this.camera.fov * Math.PI) / 360);
    const tanH = tanV * Math.max(0.4, this.camera.aspect);

    const pos = this.branchMesh ? (this.branchMesh.geometry.getAttribute('position').array as Float32Array) : null;
    let cx = 0;
    let cy = this.treeHeight * 0.5;
    let dist = this.treeHeight * 2;
    if (pos && pos.length >= 3) {
      // Screen-space extents of the plant.
      let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
      for (let i = 0; i < pos.length; i += 3) {
        const y = pos[i + 1];
        if (y < -0.01) continue; // roots and the sunk crown are hidden by the ground
        const x = pos[i], z = pos[i + 2];
        const sx = x * right.x + y * right.y + z * right.z;
        const sy = x * up.x + y * up.y + z * up.z;
        if (sx < minX) minX = sx;
        if (sx > maxX) maxX = sx;
        if (sy < minY) minY = sy;
        if (sy > maxY) maxY = sy;
      }
      if (!Number.isFinite(minX)) return;
      const mx = (minX + maxX) * 0.5;
      const my = (minY + maxY) * 0.5;
      // Distance from the target needed to hold every vertex inside the frustum:
      // lateral offset / tan(half fov) plus the vertex's depth towards the camera.
      let need = 0;
      const n = pos.length / 3;
      const req = new Float32Array(n);
      let k = 0;
      for (let i = 0; i < pos.length; i += 3) {
        const y = pos[i + 1];
        if (y < -0.01) continue;
        const x = pos[i], z = pos[i + 2];
        const sx = x * right.x + y * right.y + z * right.z - mx;
        const sy = x * up.x + y * up.y + z * up.z - my;
        const sz = x * dir.x + y * dir.y + z * dir.z;
        const d = Math.max(Math.abs(sx) / tanH, Math.abs(sy) / tanV) + sz;
        req[k++] = d;
        if (d > need) need = d;
      }
      // 99.5th percentile through a histogram (no sort of a million values).
      const bins = new Uint32Array(512);
      const lo = Math.min(0, need);
      const scale = 511 / Math.max(1e-6, need - lo);
      for (let i = 0; i < k; i++) bins[Math.floor((req[i] - lo) * scale)]++;
      let acc = 0;
      let b = 0;
      for (; b < 512; b++) {
        acc += bins[b];
        if (acc >= k * 0.998) break;
      }
      dist = Math.max(0.1, (lo + (b + 1) / scale) * 1.1);
      // Target: the screen-space centre projected back onto the plant's axis
      // plane, raised a little so the plant sits below the toolbar overlays.
      const centre = right.clone().multiplyScalar(mx).add(up.clone().multiplyScalar(my + dist * tanV * 0.07));
      cx = centre.x;
      cy = centre.y;
      const target = new THREE.Vector3(cx, cy, centre.z);
      this.controls.target.copy(target);
      this.camera.position.copy(dir).multiplyScalar(dist).add(target);
      this.controls.update();
      return;
    }
    this.controls.target.set(0, cy, 0);
    this.camera.position.copy(dir).multiplyScalar(dist).add(new THREE.Vector3(0, cy, 0));
    this.controls.update();
  }

  /** Low camera on the trunk base and the root system. */
  frameBase(reach = 4): void {
    const d = Math.max(0.4, reach * 1.6);
    const ty = Math.min(0.35, d * 0.12);
    this.controls.target.set(0, ty, 0);
    const dir = new THREE.Vector3(0.75, 0.42, 1).normalize();
    this.camera.position.copy(dir.multiplyScalar(d)).add(new THREE.Vector3(0, ty, 0));
    this.controls.update();
  }

  private clearTree(): void {
    for (const m of [this.branchMesh, this.wireMesh, this.leafMesh]) {
      if (!m) continue;
      this.treeGroup.remove(m);
      m.geometry.dispose();
      (m.material as THREE.Material).dispose();
    }
    this.branchMesh = this.wireMesh = this.leafMesh = null;
  }

  // ---------------------------------------------------------------------------
  // Materials
  // ---------------------------------------------------------------------------

  private injectWind(shader: THREE.WebGLProgramParametersWithUniforms): void {
    Object.assign(shader.uniforms, this.uniforms);
    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', `#include <common>\n${WIND_UNIFORMS}\n${WIND_ATTRIBUTES}\n${WIND_FUNCTIONS}`)
      .replace('#include <begin_vertex>', `vec3 transformed = applyWind(vec3(position));`);
  }

  private makeBarkMaterial(desert = false, cactus = false): THREE.MeshStandardMaterial {
    const mat = new THREE.MeshStandardMaterial({ color: desert ? DESERT_STEM : BARK, roughness: desert ? 0.72 : 0.92, metalness: 0.0, side: THREE.DoubleSide });
    const modeU = { value: 0 };
    mat.userData.mode = modeU;
    mat.onBeforeCompile = (shader) => {
      this.injectWind(shader);
      shader.uniforms.uMode = modeU;
      shader.uniforms.uDesert = { value: desert ? 1 : 0 };
      shader.uniforms.uCactus = { value: cactus ? 1 : 0 };
      shader.uniforms.uMatcap = { value: this.matcapTexture };
      shader.vertexShader = shader.vertexShader
        .replace('#include <common>', `#include <common>\nattribute float aLevel;\nattribute float aJunction;\nvarying vec4 vWindV;\nvarying float vLevel;\nvarying float vJunction;\nvarying vec3 vViewNrm;`)
        .replace('#include <fog_vertex>', `#include <fog_vertex>\nvWindV = aWind;\nvLevel = aLevel;\nvJunction = aJunction;\nvViewNrm = normalize(normalMatrix * objectNormal);`);
      shader.fragmentShader = shader.fragmentShader
        .replace(
          '#include <common>',
          `#include <common>\nuniform float uMode;\nuniform float uDesert;\nuniform float uCactus;\nuniform sampler2D uMatcap;\nvarying vec4 vWindV;\nvarying float vLevel;\nvarying float vJunction;\nvarying vec3 vViewNrm;
          // Debug palettes are authored in sRGB; convert so they survive lighting + tone mapping.
          vec3 srgbIn(vec3 c) { return pow(c, vec3(2.2)); }
          vec3 levelColor(float l) {
            if (l < 0.5) return srgbIn(vec3(0.54, 0.35, 0.23));
            if (l < 1.5) return srgbIn(vec3(0.79, 0.55, 0.29));
            if (l < 2.5) return srgbIn(vec3(0.44, 0.63, 0.42));
            if (l < 3.5) return srgbIn(vec3(0.35, 0.63, 0.79));
            return srgbIn(vec3(0.72, 0.42, 0.36));
          }
          vec3 heat(float t) {
            t = clamp(t, 0.0, 1.0);
            return srgbIn(mix(mix(vec3(0.10, 0.20, 0.55), vec3(0.20, 0.75, 0.55), smoothstep(0.0, 0.5, t)), vec3(0.98, 0.85, 0.25), smoothstep(0.5, 1.0, t)));
          }`,
        )
        .replace(
          '#include <color_fragment>',
          `#include <color_fragment>
          if (uDesert > 0.5) {
            // Use the interpolated geometric normal rather than vUv: MeshStandardMaterial does not
            // enable its UV varying unless a texture map is bound, so this remains valid for every
            // solid-colour desert material and for the worker-generated branch buffers.
            float azimuth = atan(vViewNrm.z, vViewNrm.x);
            float rib = 0.5 + 0.5 * cos(azimuth * 14.0);
            float groove = smoothstep(0.32, 0.72, rib);
            diffuseColor.rgb *= mix(vec3(0.70, 0.82, 0.56), vec3(0.42, 0.58, 0.32), groove);
            if (uCactus > 0.5) {
              float areole = smoothstep(0.88, 0.98, sin(azimuth * 7.0) * 0.5 + 0.5);
              diffuseColor.rgb += areole * 0.035;
            }
          }
          if (uMode > 0.5 && uMode < 1.5) diffuseColor.rgb = levelColor(vLevel);
          else if (uMode > 1.5 && uMode < 2.5) diffuseColor.rgb = heat(vWindV.y * 0.75 + vWindV.w * 0.25);
          else if (uMode > 2.5 && uMode < 3.5) diffuseColor.rgb = srgbIn(mix(vec3(0.62, 0.60, 0.58), vec3(0.95, 0.42, 0.18), vJunction));
          else if (uMode > 5.5) diffuseColor.rgb = vec3(0.16, 0.17, 0.19);`,
        )
        .replace(
          '#include <dithering_fragment>',
          `#include <dithering_fragment>
          if (uMode > 3.5 && uMode < 4.5) {
            vec3 n = normalize(vViewNrm);
            if (!gl_FrontFacing) n = -n;
            vec2 muv = n.xy * 0.495 + 0.5;
            gl_FragColor = linearToOutputTexel(vec4(texture2D(uMatcap, muv).rgb, 1.0));
          }`,
        );
    };
    mat.customProgramCacheKey = () => `bark-wind-v5-${desert ? 1 : 0}-${cactus ? 1 : 0}`;
    return mat;
  }

  private makeLeafMaterial(desert = false, aloe = false): THREE.MeshStandardMaterial {
    const mat = new THREE.MeshStandardMaterial({ color: desert ? (aloe ? DESERT_ALOE : DESERT_LEAF) : LEAF, roughness: desert ? 0.58 : 0.75, metalness: 0, side: THREE.DoubleSide });
    mat.onBeforeCompile = (shader) => this.injectWind(shader);
    mat.customProgramCacheKey = () => `leaf-wind-v4-${desert ? 1 : 0}-${aloe ? 1 : 0}`;
    return mat;
  }

  private makeWireMaterial(): THREE.ShaderMaterial {
    return new THREE.ShaderMaterial({
      uniforms: this.uniforms,
      transparent: true,
      depthWrite: false,
      vertexShader: /* glsl */ `
        ${WIND_UNIFORMS}
        ${WIND_ATTRIBUTES}
        ${WIND_FUNCTIONS}
        varying float vDepth;
        void main() {
          vec3 p = applyWind(position);
          vec4 mv = modelViewMatrix * vec4(p, 1.0);
          vDepth = -mv.z;
          gl_Position = projectionMatrix * mv;
          // pull towards the camera a hair to avoid z-fighting with the fill
          gl_Position.z -= 0.0006 * gl_Position.w;
        }
      `,
      fragmentShader: /* glsl */ `
        varying float vDepth;
        void main() {
          float a = clamp(1.0 - vDepth / 160.0, 0.25, 1.0);
          gl_FragColor = vec4(0.80, 0.86, 0.95, 0.55 * a);
        }
      `,
    });
  }

  screenshot(): string {
    this.renderer.render(this.scene, this.camera);
    return this.renderer.domElement.toDataURL('image/png');
  }
}

function modeIndex(m: DisplayMode): number {
  switch (m) {
    case 'levels':
      return 1;
    case 'wind':
      return 2;
    case 'junctions':
      return 3;
    case 'matcap':
      return 4;
    case 'wireframe':
      return 6;
    default:
      return 0;
  }
}

/** Procedural clay matcap: neutral studio sphere. */
function makeMatcap(): THREE.Texture {
  const size = 256;
  const c = document.createElement('canvas');
  c.width = c.height = size;
  const ctx = c.getContext('2d')!;
  const img = ctx.createImageData(size, size);
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const nx = (x / (size - 1)) * 2 - 1;
      const ny = 1 - (y / (size - 1)) * 2;
      const r2 = nx * nx + ny * ny;
      const nz = Math.sqrt(Math.max(0, 1 - r2));
      const l1 = Math.max(0, nx * -0.45 + ny * 0.6 + nz * 0.66);
      const l2 = Math.max(0, nx * 0.7 + ny * -0.2 + nz * 0.3);
      const spec = Math.pow(l1, 40) * 0.35;
      const rim = Math.pow(1 - nz, 3) * 0.25;
      const v = 0.18 + 0.62 * Math.pow(l1, 1.4) + 0.14 * l2 + spec + rim;
      const i = (y * size + x) * 4;
      img.data[i] = Math.min(255, v * 235);
      img.data[i + 1] = Math.min(255, v * 228);
      img.data[i + 2] = Math.min(255, v * 218);
      img.data[i + 3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);
  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  return tex;
}

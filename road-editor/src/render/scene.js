// three.js viewer: renderer, lights, terrain, water, the world meshes, and an Unreal-style
// fly camera (RMB to look, WASD to move, Q/E down/up, Shift to boost, wheel to change speed).
import * as THREE from 'three';
import { MATERIALS } from './materials.js';
import { terrainY, WATER_Y } from '../core/terrain.js';

export class Viewer {
  constructor(container, { half = 1200 } = {}) {
    this.container = container;
    this.half = half;
    this.renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.shadowMap.enabled = false;
    container.appendChild(this.renderer.domElement);

    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(0x9fbcd6);
    this.scene.fog = new THREE.Fog(0x9fbcd6, 1400, 5200);

    this.camera = new THREE.PerspectiveCamera(60, 1, 0.5, 14000);
    this.camera.position.set(0, 160, 360);
    this.yaw = Math.PI; // looking towards -z
    this.pitch = -0.38;
    this.speed = 90;
    this.keys = new Set();
    this.lookDrag = false;

    this.scene.add(new THREE.HemisphereLight(0xdfeaff, 0x3b3a2c, 1.15));
    const sun = new THREE.DirectionalLight(0xfff4e0, 2.2);
    sun.position.set(-600, 900, 400);
    this.scene.add(sun);

    this.worldGroup = new THREE.Group();
    this.scene.add(this.worldGroup);
    this.overlay = new THREE.Group();
    this.scene.add(this.overlay);

    this.materials = {};
    for (const [key, def] of Object.entries(MATERIALS)) {
      this.materials[key] = new THREE.MeshStandardMaterial({
        color: def.color,
        roughness: def.roughness ?? 0.9,
        metalness: def.metalness ?? 0,
        emissive: def.emissive ?? 0x000000,
        emissiveIntensity: def.emissiveIntensity ?? 0,
        vertexColors: true,
        side: THREE.DoubleSide,
      });
    }

    this.ground = this.buildGround();
    this.scene.add(this.ground);
    this.water = new THREE.Mesh(
      new THREE.PlaneGeometry(half * 2 + 900, half * 2 + 900),
      new THREE.MeshStandardMaterial({ color: 0x2c6e9b, roughness: 0.15, metalness: 0.1, transparent: true, opacity: 0.82 }),
    );
    this.water.rotation.x = -Math.PI / 2;
    this.water.position.y = WATER_Y + 0.12;
    this.scene.add(this.water);

    this.raycaster = new THREE.Raycaster();
    this.resize();
    window.addEventListener('resize', () => this.resize());
    this.bindInput();
    this.last = performance.now();
    this.frames = 0;
    this.fps = 0;
    this.fpsT = performance.now();
    this.renderer.setAnimationLoop(() => this.frame());
  }

  /** Terrain mesh over the map area (3 m grid). */
  buildGround() {
    const size = this.half * 2 + 260;
    const step = 3;
    const n = Math.round(size / step);
    const pos = new Float32Array((n + 1) * (n + 1) * 3);
    const col = new Float32Array((n + 1) * (n + 1) * 3);
    const grass = new THREE.Color(0x5b8a3c), dry = new THREE.Color(0x9b8f5d), sand = new THREE.Color(0xc9b67f), rock = new THREE.Color(0x7d7a70);
    const tmp = new THREE.Color();
    let k = 0;
    for (let j = 0; j <= n; j++) for (let i = 0; i <= n; i++) {
      const x = -size / 2 + i * step, z = -size / 2 + j * step;
      const y = terrainY(x, z);
      pos[k * 3] = x; pos[k * 3 + 1] = y; pos[k * 3 + 2] = z;
      const slope = Math.abs(terrainY(x + 3, z) - y) + Math.abs(terrainY(x, z + 3) - y);
      tmp.copy(grass).lerp(dry, Math.min(1, Math.max(0, (y - 6) / 9)));
      if (slope > 1.5) tmp.lerp(rock, Math.min(1, (slope - 1.5) / 2));
      if (y < 1.5) tmp.copy(sand);
      col[k * 3] = tmp.r; col[k * 3 + 1] = tmp.g; col[k * 3 + 2] = tmp.b;
      k++;
    }
    const idx = [];
    for (let j = 0; j < n; j++) for (let i = 0; i < n; i++) {
      const a = j * (n + 1) + i, b = a + 1, c = a + (n + 1), d = c + 1;
      idx.push(a, c, b, b, c, d);
    }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    g.setAttribute('color', new THREE.BufferAttribute(col, 3));
    g.setIndex(idx);
    g.computeVertexNormals();
    const mesh = new THREE.Mesh(g, new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 1, side: THREE.DoubleSide }));
    mesh.name = 'ground';
    return mesh;
  }

  /** Replace the world meshes with freshly built buckets. */
  setWorld(buckets) {
    this.worldGroup.traverse((o) => { if (o.geometry) o.geometry.dispose(); });
    this.worldGroup.clear();
    for (const [key, b] of buckets) {
      if (!b.count) continue;
      const g = new THREE.BufferGeometry();
      g.setAttribute('position', new THREE.Float32BufferAttribute(b.pos, 3));
      g.setAttribute('normal', new THREE.Float32BufferAttribute(b.nrm, 3));
      g.setAttribute('color', new THREE.Float32BufferAttribute(b.col, 3));
      g.setIndex(b.idx.length > 65535 ? new THREE.Uint32BufferAttribute(b.idx, 1) : new THREE.Uint16BufferAttribute(b.idx, 1));
      const mesh = new THREE.Mesh(g, this.materials[key] || this.materials.concrete);
      mesh.name = `world:${key}`;
      this.worldGroup.add(mesh);
    }
  }

  setUnderground(on) {
    const g = this.ground.material;
    g.transparent = on;
    g.opacity = on ? 0.28 : 1;
    g.depthWrite = !on;
    this.water.material.opacity = on ? 0.3 : 0.82;
  }

  /** Ray-pick the first surface under the cursor (ground, road, bridge, prop). */
  pickAt(clientX, clientY) {
    const rect = this.renderer.domElement.getBoundingClientRect();
    const ndc = new THREE.Vector2(((clientX - rect.left) / rect.width) * 2 - 1, -((clientY - rect.top) / rect.height) * 2 + 1);
    this.raycaster.setFromCamera(ndc, this.camera);
    const hits = this.raycaster.intersectObjects([this.ground, this.worldGroup], true);
    const hit = hits.find((h) => h.object !== this.water);
    if (!hit) return null;
    return { x: hit.point.x, y: hit.point.y, z: hit.point.z };
  }

  /** Project a world point to canvas pixels. */
  project(p) {
    const v = new THREE.Vector3(p.x, p.y, p.z).project(this.camera);
    const rect = this.renderer.domElement.getBoundingClientRect();
    return { x: (v.x + 1) / 2 * rect.width + rect.left, y: (-v.y + 1) / 2 * rect.height + rect.top, behind: v.z > 1 };
  }

  resize() {
    const w = this.container.clientWidth || 1, h = this.container.clientHeight || 1;
    this.renderer.setSize(w, h, false);
    this.renderer.domElement.style.width = '100%';
    this.renderer.domElement.style.height = '100%';
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
  }

  focus(x, z, y = 0, dist = 180) {
    const dir = new THREE.Vector3(0, -0.5, -1).normalize();
    this.camera.position.set(x - dir.x * dist, Math.max(y, WATER_Y) + dist * 0.6, z - dir.z * dist);
    this.yaw = Math.atan2(-dir.x, -dir.z);
    this.pitch = Math.asin(dir.y);
  }

  bindInput() {
    const el = this.renderer.domElement;
    el.addEventListener('contextmenu', (e) => e.preventDefault());
    el.addEventListener('pointerdown', (e) => {
      if (e.button === 2) { this.lookDrag = true; el.setPointerCapture(e.pointerId); }
    });
    el.addEventListener('pointerup', (e) => { if (e.button === 2) { this.lookDrag = false; el.releasePointerCapture(e.pointerId); } });
    el.addEventListener('pointermove', (e) => {
      if (!this.lookDrag) return;
      this.yaw -= e.movementX * 0.0022;
      this.pitch = Math.max(-1.5, Math.min(1.5, this.pitch - e.movementY * 0.0022));
    });
    el.addEventListener('wheel', (e) => {
      e.preventDefault();
      this.speed = Math.max(5, Math.min(800, this.speed * (e.deltaY < 0 ? 1.15 : 1 / 1.15)));
      this.onSpeed?.(this.speed);
    }, { passive: false });
    window.addEventListener('keydown', (e) => {
      if (isTyping(e)) return;
      this.keys.add(e.code);
    });
    window.addEventListener('keyup', (e) => this.keys.delete(e.code));
    window.addEventListener('blur', () => this.keys.clear());
  }

  frame() {
    const now = performance.now();
    const dt = Math.min(0.05, (now - this.last) / 1000);
    this.last = now;
    this.updateCamera(dt);
    this.renderer.render(this.scene, this.camera);
    this.frames++;
    if (now - this.fpsT > 500) {
      this.fps = Math.round((this.frames * 1000) / (now - this.fpsT));
      this.frames = 0;
      this.fpsT = now;
      this.onFps?.(this.fps);
    }
  }

  updateCamera(dt) {
    const q = new THREE.Quaternion().setFromEuler(new THREE.Euler(this.pitch, this.yaw, 0, 'YXZ'));
    this.camera.quaternion.copy(q);
    const fwd = new THREE.Vector3(0, 0, -1).applyQuaternion(q);
    const right = new THREE.Vector3(1, 0, 0).applyQuaternion(q);
    const move = new THREE.Vector3();
    const k = this.keys;
    if (k.has('KeyW')) move.add(fwd);
    if (k.has('KeyS')) move.sub(fwd);
    if (k.has('KeyD')) move.add(right);
    if (k.has('KeyA')) move.sub(right);
    if (k.has('KeyE')) move.y += 1;
    if (k.has('KeyQ')) move.y -= 1;
    if (move.lengthSq() > 0) {
      const boost = k.has('ShiftLeft') || k.has('ShiftRight') ? 4 : 1;
      move.normalize().multiplyScalar(this.speed * boost * dt);
      this.camera.position.add(move);
    }
    if (this.camera.position.y < 2) this.camera.position.y = 2;
  }
}

export function isTyping(e) {
  const t = e.target;
  return t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT' || t.isContentEditable);
}

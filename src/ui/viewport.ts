// ---------------------------------------------------------------------------
// Frontier UI / viewport — three.js scene with Unreal-Editor-style navigation:
//   RMB drag      : look around (free yaw/pitch)
//   LMB drag      : orbit around the point under view
//   W A S D       : fly along view / strafe      Q / E : down / up
//   Shift         : 3x speed                     wheel : dolly forward/back
//   F             : frame terrain (handled by app)
// Grey studio sky + grid + red/blue axis lines like the reference. The canvas
// is the ONLY child of its container — nothing may innerHTML over it.
// ---------------------------------------------------------------------------

import * as THREE from 'three';
import { MeshData } from '../core/mesh';
import { SplatSet } from '../core/splat';

const SPLAT_VERT = /* glsl */`
varying vec3 vWorld;
varying vec3 vNormal;
void main() {
  vec4 wp = modelMatrix * vec4(position, 1.0);
  vWorld = wp.xyz;
  vNormal = normalize(mat3(modelMatrix) * normal);
  gl_Position = projectionMatrix * viewMatrix * wp;
}
`;

const SPLAT_FRAG = /* glsl */`
precision highp float;
varying vec3 vWorld;
varying vec3 vNormal;
uniform sampler2D uSplatA;
uniform sampler2D uSplatB;
uniform int uSets;
uniform vec3 uColors[8];
uniform vec3 uDomainMin;
uniform vec3 uDomainSize;
uniform vec3 uLightDir;
uniform vec3 uSky;
uniform vec3 uGround;

void main() {
  vec2 uv = (vWorld.xz - uDomainMin.xz) / uDomainSize.xz;
  uv = clamp(uv, 0.001, 0.999);
  vec4 a = texture2D(uSplatA, uv);
  vec4 w = vec4(a.r, a.g, a.b, a.a);
  vec3 albedo = uColors[0] * w.x + uColors[1] * w.y + uColors[2] * w.z + uColors[3] * w.w;
  if (uSets > 1) {
    vec4 b = texture2D(uSplatB, uv);
    albedo += uColors[4] * b.x + uColors[5] * b.y + uColors[6] * b.z + uColors[7] * b.w;
  }
  float grain = fract(sin(dot(vWorld.xz, vec2(12.9898, 78.233))) * 43758.5453);
  albedo *= 0.92 + 0.16 * grain;

  vec3 n = normalize(vNormal);
  float ndl = max(dot(n, normalize(uLightDir)), 0.0);
  float hemi = 0.5 + 0.5 * n.y;
  vec3 light = mix(uGround, uSky, hemi) * 0.55 + vec3(1.0, 0.98, 0.94) * ndl * 0.9;
  gl_FragColor = vec4(albedo * light, 1.0);
}
`;

export interface ViewportOptions {
  container: HTMLElement;
  domainSize: [number, number, number];
  onViewError?: (message: string) => void;
}

export class Viewport {
  renderer: THREE.WebGLRenderer;
  scene: THREE.Scene;
  camera: THREE.PerspectiveCamera;
  private mesh: THREE.Mesh | null = null;
  private stdMat: THREE.MeshStandardMaterial;
  private splatMat: THREE.ShaderMaterial | null = null;
  private textures: THREE.DataTexture[] = [];
  wireframe = false;
  textured = true;
  private domainSize: [number, number, number];
  private hasFramed = false;
  private onViewError?: (m: string) => void;

  // fly/orbit state
  private yaw = -Math.PI * 0.25;
  private pitch = -0.42;
  private pos = new THREE.Vector3();
  private keys = new Set<string>();
  private moveSpeed = 220;
  private drag: 'look' | 'orbit' | null = null;
  private lastMouse = { x: 0, y: 0 };
  private clock = new THREE.Clock();

  constructor(opts: ViewportOptions) {
    this.domainSize = opts.domainSize;
    this.onViewError = opts.onViewError;

    this.renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
    this.renderer.setPixelRatio(Math.min(2, window.devicePixelRatio));
    this.renderer.setSize(opts.container.clientWidth || 800, opts.container.clientHeight || 600);
    this.renderer.domElement.id = 'gl';
    opts.container.appendChild(this.renderer.domElement);

    this.renderer.domElement.addEventListener('webglcontextlost', (e) => {
      e.preventDefault();
      this.onViewError?.('WebGL context lost — reload the page if the viewport stays grey.');
    });

    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(0x8c8c8c);

    this.camera = new THREE.PerspectiveCamera(55, (opts.container.clientWidth || 800) / (opts.container.clientHeight || 600), 0.5, 20000);
    this.pos.set(opts.domainSize[0] * 0.95, opts.domainSize[1] * 2.2, opts.domainSize[2] * 1.15);

    // grid + axis lines like the reference viewport
    const grid = new THREE.GridHelper(6000, 120, 0x5a5a5a, 0x6e6e6e);
    (grid.material as THREE.Material).transparent = true;
    (grid.material as THREE.Material).opacity = 0.5;
    grid.position.set(opts.domainSize[0] / 2, 0, opts.domainSize[2] / 2);
    this.scene.add(grid);

    const axisGeo = new THREE.BufferGeometry();
    axisGeo.setAttribute('position', new THREE.Float32BufferAttribute([
      -3000, 0.4, 0, 3000, 0.4, 0,
      0, 0.4, -3000, 0, 0.4, 3000,
    ], 3));
    axisGeo.setAttribute('color', new THREE.Float32BufferAttribute([
      0.82, 0.3, 0.3, 0.82, 0.3, 0.3,
      0.3, 0.55, 0.85, 0.3, 0.55, 0.85,
    ], 3));
    this.scene.add(new THREE.LineSegments(axisGeo, new THREE.LineBasicMaterial({ vertexColors: true, transparent: true, opacity: 0.9 })));

    this.scene.add(new THREE.HemisphereLight(0xdedede, 0x50504a, 0.85));
    const dir = new THREE.DirectionalLight(0xfff4e0, 1.15);
    dir.position.set(0.6, 1, 0.35);
    this.scene.add(dir);

    this.stdMat = new THREE.MeshStandardMaterial({ color: 0x9a958c, roughness: 0.94, metalness: 0 });

    this.bindNavigation();

    const loop = () => {
      requestAnimationFrame(loop);
      this.step();
      try {
        this.renderer.render(this.scene, this.camera);
      } catch (err) {
        // shader/driver problem: fall back to the plain material once
        if (this.mesh && this.splatMat && this.mesh.material === this.splatMat) {
          this.mesh.material = this.stdMat;
          this.onViewError?.('Splat shader failed on this GPU — fell back to plain shading.');
        } else {
          this.onViewError?.(`Render error: ${(err as Error).message}`);
          throw err;
        }
      }
    };
    loop();

    new ResizeObserver(() => this.resize()).observe(opts.container);
    window.addEventListener('resize', () => this.resize());
    setTimeout(() => this.resize(), 0);
  }

  // ------------------------------------------------------------- navigation
  private bindNavigation(): void {
    const el = this.renderer.domElement;
    el.addEventListener('contextmenu', (e) => e.preventDefault());
    el.addEventListener('mousedown', (e) => {
      this.drag = e.button === 2 ? 'look' : e.button === 0 ? 'orbit' : null;
      this.lastMouse = { x: e.clientX, y: e.clientY };
      e.preventDefault();
    });
    window.addEventListener('mouseup', () => { this.drag = null; });
    window.addEventListener('mousemove', (e) => {
      if (!this.drag) return;
      const dx = e.clientX - this.lastMouse.x;
      const dy = e.clientY - this.lastMouse.y;
      this.lastMouse = { x: e.clientX, y: e.clientY };
      if (this.drag === 'look') {
        this.yaw -= dx * 0.0032;
        this.pitch = Math.max(-1.52, Math.min(1.52, this.pitch - dy * 0.0032));
      } else {
        // orbit: rotate around the point 60% of current view distance ahead
        const fwd = this.forward();
        const d = 420;
        const target = this.pos.clone().add(fwd.multiplyScalar(d));
        this.yaw -= dx * 0.0032;
        this.pitch = Math.max(-1.52, Math.min(1.52, this.pitch - dy * 0.0032));
        this.pos.copy(target).add(this.forward().multiplyScalar(-d));
      }
    });
    el.addEventListener('wheel', (e) => {
      e.preventDefault();
      const fwd = this.forward();
      const step = -Math.sign(e.deltaY) * this.moveSpeed * 0.35;
      this.pos.add(fwd.multiplyScalar(step));
    }, { passive: false });

    window.addEventListener('keydown', (e) => {
      const t = e.target as HTMLElement;
      if (t.tagName === 'INPUT' || t.tagName === 'SELECT' || t.tagName === 'TEXTAREA') return;
      this.keys.add(e.code);
      if (e.code === 'ShiftLeft' || e.code === 'ShiftRight') this.keys.add('Shift');
    });
    window.addEventListener('keyup', (e) => {
      this.keys.delete(e.code);
      if (e.code === 'ShiftLeft' || e.code === 'ShiftRight') this.keys.delete('Shift');
    });
    window.addEventListener('blur', () => this.keys.clear());
  }

  private forward(): THREE.Vector3 {
    const cp = Math.cos(this.pitch);
    return new THREE.Vector3(
      -Math.sin(this.yaw) * cp,
      Math.sin(this.pitch),
      -Math.cos(this.yaw) * cp,
    ).normalize();
  }

  private right(): THREE.Vector3 {
    return new THREE.Vector3().crossVectors(this.forward(), new THREE.Vector3(0, 1, 0)).normalize();
  }

  private step(): void {
    const dt = Math.min(0.1, this.clock.getDelta());
    const boost = this.keys.has('Shift') ? 3 : 1;
    const v = new THREE.Vector3();
    const f = this.forward(), r = this.right();
    if (this.keys.has('KeyW')) v.add(f);
    if (this.keys.has('KeyS')) v.sub(f);
    if (this.keys.has('KeyD')) v.add(r);
    if (this.keys.has('KeyA')) v.sub(r);
    if (this.keys.has('KeyE')) v.y += 1;
    if (this.keys.has('KeyQ')) v.y -= 1;
    if (v.lengthSq() > 0) {
      v.normalize().multiplyScalar(this.moveSpeed * boost * dt);
      this.pos.add(v);
    }
    if (this.pos.y < 2) this.pos.y = 2;
    this.camera.position.copy(this.pos);
    this.camera.quaternion.setFromEuler(new THREE.Euler(this.pitch, this.yaw, 0, 'YXZ'));
  }

  // ------------------------------------------------------------------ scene
  resize(): void {
    const el = this.renderer.domElement.parentElement;
    if (!el) return;
    const w = el.clientWidth, h = el.clientHeight;
    if (w === 0 || h === 0) return;
    this.renderer.setSize(w, h);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
  }

  setDomain(size: [number, number, number]): void {
    this.domainSize = size;
  }

  updateMesh(mesh: MeshData | null, splat: SplatSet | null): void {
    if (this.mesh) {
      this.scene.remove(this.mesh);
      this.mesh.geometry.dispose();
      this.mesh = null;
    }
    for (const t of this.textures) t.dispose();
    this.textures = [];
    if (!mesh || mesh.positions.length === 0) return;

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(mesh.positions, 3));
    geo.setAttribute('normal', new THREE.BufferAttribute(mesh.normals, 3));
    geo.setAttribute('uv', new THREE.BufferAttribute(mesh.uvs, 2));
    geo.setIndex(new THREE.BufferAttribute(mesh.indices, 1));
    geo.computeBoundingSphere();

    let mat: THREE.Material = this.stdMat;
    if (splat && splat.textures.length > 0 && this.textured) mat = this.makeSplatMaterial(splat);
    this.mesh = new THREE.Mesh(geo, mat);
    this.mesh.frustumCulled = false;
    this.scene.add(this.mesh);
    this.applyWireframe();

    if (!this.hasFramed) {
      this.frameTerrain();
      this.hasFramed = true;
    }
  }

  private makeSplatMaterial(splat: SplatSet): THREE.Material {
    const mkTex = (bytes: Uint8Array) => {
      const t = new THREE.DataTexture(bytes, splat.nx, splat.nz, THREE.RGBAFormat, THREE.UnsignedByteType);
      t.magFilter = THREE.LinearFilter;
      t.minFilter = THREE.LinearFilter;
      t.wrapS = THREE.ClampToEdgeWrapping;
      t.wrapT = THREE.ClampToEdgeWrapping;
      t.needsUpdate = true;
      this.textures.push(t);
      return t;
    };
    const colors: THREE.Vector3[] = [];
    for (let i = 0; i < 8; i++) {
      const c = splat.layers[i]?.color ?? [0.4, 0.4, 0.4];
      colors.push(new THREE.Vector3(c[0], c[1], c[2]));
    }
    this.splatMat = new THREE.ShaderMaterial({
      vertexShader: SPLAT_VERT,
      fragmentShader: SPLAT_FRAG,
      uniforms: {
        uSplatA: { value: mkTex(splat.textures[0]) },
        uSplatB: { value: splat.textures[1] ? mkTex(splat.textures[1]) : mkTex(new Uint8Array(splat.nx * splat.nz * 4)) },
        uSets: { value: Math.min(2, splat.textures.length) },
        uColors: { value: colors },
        uDomainMin: { value: new THREE.Vector3(0, 0, 0) },
        uDomainSize: { value: new THREE.Vector3(this.domainSize[0], this.domainSize[1], this.domainSize[2]) },
        uLightDir: { value: new THREE.Vector3(0.6, 1, 0.35) },
        uSky: { value: new THREE.Vector3(0.86, 0.88, 0.92) },
        uGround: { value: new THREE.Vector3(0.32, 0.3, 0.27) },
      },
    });
    return this.splatMat;
  }

  setWireframe(on: boolean): void {
    this.wireframe = on;
    this.applyWireframe();
  }

  private applyWireframe(): void {
    if (!this.mesh) return;
    (this.mesh.material as THREE.MeshStandardMaterial).wireframe = this.wireframe;
  }

  setTextured(on: boolean): void {
    this.textured = on;
    if (this.mesh && this.splatMat) {
      this.mesh.material = on ? this.splatMat : this.stdMat;
      this.applyWireframe();
    }
  }

  snapshot(): string {
    this.renderer.render(this.scene, this.camera);
    return this.renderer.domElement.toDataURL('image/png');
  }

  /** place the camera so the whole terrain (or domain) is in view */
  frameTerrain(): void {
    const s = this.domainSize;
    let center = new THREE.Vector3(s[0] / 2, s[1] * 0.18, s[2] / 2);
    let radius = Math.hypot(s[0], s[2]) * 0.5;
    if (this.mesh?.geometry.boundingSphere) {
      const bs = this.mesh.geometry.boundingSphere;
      center = bs.center.clone();
      radius = bs.radius;
    }
    const dir = new THREE.Vector3(0.62, 0.52, 0.85).normalize();
    this.pos.copy(center).add(dir.multiplyScalar(radius * 2.05));
    // aim at center
    const look = center.clone().sub(this.pos).normalize();
    this.pitch = Math.asin(Math.max(-1, Math.min(1, look.y)));
    this.yaw = Math.atan2(-look.x, -look.z);
    this.moveSpeed = Math.max(40, radius * 0.55);
  }
}

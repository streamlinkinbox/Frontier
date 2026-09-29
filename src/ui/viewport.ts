// ---------------------------------------------------------------------------
// Frontier UI / viewport — three.js scene: grey studio sky, infinite grid with
// red/blue axis lines (per reference), orbit camera, SDF mesh with splatmap
// blending shader or plain standard material.
// ---------------------------------------------------------------------------

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
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
  // fine detail break-up so flat colours read as material
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
}

export class Viewport {
  renderer: THREE.WebGLRenderer;
  scene: THREE.Scene;
  camera: THREE.PerspectiveCamera;
  controls: OrbitControls;
  private mesh: THREE.Mesh | null = null;
  private stdMat: THREE.MeshStandardMaterial;
  private splatMat: THREE.ShaderMaterial | null = null;
  private textures: THREE.DataTexture[] = [];
  wireframe = false;
  textured = true;
  private domainSize: [number, number, number];

  constructor(opts: ViewportOptions) {
    this.domainSize = opts.domainSize;
    this.renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
    this.renderer.setPixelRatio(Math.min(2, window.devicePixelRatio));
    this.renderer.setSize(opts.container.clientWidth, opts.container.clientHeight);
    opts.container.appendChild(this.renderer.domElement);
    this.renderer.domElement.id = 'gl';

    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(0x8c8c8c);
    this.scene.fog = new THREE.Fog(0x8c8c8c, 1600, 4200);

    this.camera = new THREE.PerspectiveCamera(50, opts.container.clientWidth / opts.container.clientHeight, 0.5, 12000);
    this.camera.position.set(900, 700, 1100);

    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.target.set(512, 60, 512);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.08;
    this.controls.maxPolarAngle = Math.PI * 0.495;
    this.controls.minDistance = 40;
    this.controls.maxDistance = 5000;

    // grid + axis lines like the reference viewport
    const grid = new THREE.GridHelper(4000, 80, 0x5a5a5a, 0x6e6e6e);
    (grid.material as THREE.Material).transparent = true;
    (grid.material as THREE.Material).opacity = 0.55;
    grid.position.set(512, 0, 512);
    this.scene.add(grid);

    const axisGeo = new THREE.BufferGeometry();
    axisGeo.setAttribute('position', new THREE.Float32BufferAttribute([
      -2000, 0.4, 0, 2000, 0.4, 0,
      0, 0.4, -2000, 0, 0.4, 2000,
    ], 3));
    axisGeo.setAttribute('color', new THREE.Float32BufferAttribute([
      0.82, 0.3, 0.3, 0.82, 0.3, 0.3,
      0.3, 0.55, 0.85, 0.3, 0.55, 0.85,
    ], 3));
    const axisMat = new THREE.LineBasicMaterial({ vertexColors: true, transparent: true, opacity: 0.9 });
    this.scene.add(new THREE.LineSegments(axisGeo, axisMat));

    const hemi = new THREE.HemisphereLight(0xdedede, 0x50504a, 0.85);
    this.scene.add(hemi);
    const dir = new THREE.DirectionalLight(0xfff4e0, 1.15);
    dir.position.set(0.6, 1, 0.35);
    this.scene.add(dir);

    this.stdMat = new THREE.MeshStandardMaterial({ color: 0x9a958c, roughness: 0.94, metalness: 0 });

    const loop = () => {
      requestAnimationFrame(loop);
      this.controls.update();
      this.renderer.render(this.scene, this.camera);
    };
    loop();

    new ResizeObserver(() => this.resize()).observe(opts.container);
  }

  resize(): void {
    const el = this.renderer.domElement.parentElement;
    if (!el) return;
    const w = el.clientWidth, h = el.clientHeight;
    this.renderer.setSize(w, h);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
  }

  setDomain(size: [number, number, number]): void {
    this.domainSize = size;
    this.controls.target.set(size[0] / 2, size[1] * 0.2, size[2] / 2);
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
    if (splat && splat.textures.length > 0 && this.textured) {
      mat = this.makeSplatMaterial(splat);
    }
    this.mesh = new THREE.Mesh(geo, mat);
    this.mesh.frustumCulled = false;
    this.scene.add(this.mesh);
    this.applyWireframe();
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

  frameTerrain(): void {
    const s = this.domainSize;
    this.camera.position.set(s[0] * 0.95, s[1] * 2.2, s[2] * 1.15);
    this.controls.target.set(s[0] / 2, s[1] * 0.18, s[2] / 2);
  }
}

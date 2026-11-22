import {
  ACESFilmicToneMapping,
  AmbientLight,
  BackSide,
  Color,
  DirectionalLight,
  Fog,
  HemisphereLight,
  Mesh,
  PCFSoftShadowMap,
  PerspectiveCamera,
  Scene,
  ShaderMaterial,
  SphereGeometry,
  SRGBColorSpace,
  Vector3,
  WebGLRenderer,
} from 'three';
import { clamp01, damp, lerp } from '../core/mathx';
import type { Vehicle } from '../sim/vehicle';

const SKY_VERT = /* glsl */ `
varying vec3 vWorld;
void main() {
  vWorld = (modelMatrix * vec4(position, 1.0)).xyz;
  gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}`;

const SKY_FRAG = /* glsl */ `
varying vec3 vWorld;
uniform vec3 topColor;
uniform vec3 horizonColor;
uniform vec3 groundColor;
uniform vec3 sunDir;
uniform float canopy;   // 0 = open sky, 1 = deep canopy
void main() {
  vec3 dir = normalize(vWorld);
  float h = dir.y;
  vec3 sky = mix(horizonColor, topColor, pow(clamp(h, 0.0, 1.0), 0.65));
  vec3 col = mix(groundColor, sky, smoothstep(-0.15, 0.06, h));
  // warm sun bloom through the leaves
  float sun = pow(max(dot(dir, normalize(sunDir)), 0.0), 24.0);
  col += vec3(1.0, 0.82, 0.5) * sun * (0.55 * (1.0 - canopy) + 0.12);
  // canopy haze darkens and greens the upper hemisphere
  col = mix(col, col * vec3(0.42, 0.55, 0.38), canopy * smoothstep(0.0, 0.7, h));
  gl_FragColor = vec4(col, 1.0);
}`;

export class SceneView {
  readonly renderer: WebGLRenderer;
  readonly scene = new Scene();
  readonly camera: PerspectiveCamera;
  readonly sun: DirectionalLight;

  private hemi: HemisphereLight;
  private ambient: AmbientLight;
  private camPos = new Vector3();
  private camLook = new Vector3();
  private shakeAmp = 0;
  private shakeFreq = 14;
  private shakeTime = 0;
  private fov = 74;
  private targetFov = 74;
  private hitstop = 0;

  constructor(container: HTMLElement) {
    this.renderer = new WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 1.75));
    this.renderer.setSize(container.clientWidth, container.clientHeight);
    this.renderer.outputColorSpace = SRGBColorSpace;
    this.renderer.toneMapping = ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.05;
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = PCFSoftShadowMap;
    container.appendChild(this.renderer.domElement);

    this.scene.background = new Color(0x2c3a2c);
    this.scene.fog = new Fog(0x3b4a34, 45, 240);

    this.camera = new PerspectiveCamera(74, container.clientWidth / container.clientHeight, 0.25, 700);
    this.camera.position.set(0, 4, -8);

    this.sun = new DirectionalLight(0xffe3b0, 2.5);
    this.sun.position.set(28, 42, 18);
    this.sun.castShadow = true;
    this.sun.shadow.mapSize.set(2048, 2048);
    const cam = this.sun.shadow.camera;
    cam.left = -42;
    cam.right = 42;
    cam.top = 42;
    cam.bottom = -42;
    cam.near = 1;
    cam.far = 160;
    this.sun.shadow.bias = -0.0009;
    this.sun.shadow.normalBias = 0.035;
    this.scene.add(this.sun);
    this.scene.add(this.sun.target);

    this.hemi = new HemisphereLight(0x86a4b8, 0x2a3320, 1.15);
    this.scene.add(this.hemi);
    this.ambient = new AmbientLight(0x3d4a35, 0.55);
    this.scene.add(this.ambient);

    // sky dome
    const sky = new Mesh(
      new SphereGeometry(480, 24, 16),
      new ShaderMaterial({
        side: BackSide,
        depthWrite: false,
        fog: false,
        uniforms: {
          topColor: { value: new Color(0x4f7fa8) },
          horizonColor: { value: new Color(0xbcd0c0) },
          groundColor: { value: new Color(0x2c3324) },
          sunDir: { value: new Vector3(0.5, 0.72, 0.35) },
          canopy: { value: 0.35 },
        },
        vertexShader: SKY_VERT,
        fragmentShader: SKY_FRAG,
      }),
    );
    sky.frustumCulled = false;
    this.scene.add(sky);
  }

  resize(container: HTMLElement): void {
    const w = container.clientWidth;
    const h = container.clientHeight;
    this.renderer.setSize(w, h);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
  }

  shake(amplitude: number, frequency = 14): void {
    this.shakeAmp = Math.max(this.shakeAmp, amplitude);
    this.shakeFreq = frequency;
  }

  stop(seconds: number): void {
    this.hitstop = Math.max(this.hitstop, seconds);
  }

  get isHitstopped(): boolean {
    return this.hitstop > 0;
  }

  /** Chase camera with spring damping, speed FOV and shake. */
  updateCamera(vehicle: Vehicle, dt: number, canopy: number): void {
    if (this.hitstop > 0) {
      this.hitstop -= dt;
      dt *= 0.08;
    }

    const speed = vehicle.speed;
    this.targetFov = 74 + clamp01(speed / 42) * 18;
    this.fov = damp(this.fov, this.targetFov, 4, dt);
    if (Math.abs(this.camera.fov - this.fov) > 0.01) {
      this.camera.fov = this.fov;
      this.camera.updateProjectionMatrix();
    }

    // Follow the direction of travel rather than the chassis heading, so a
    // slide reads as the world swinging around the car.
    const velDir = new Vector3(vehicle.vel.x, 0, vehicle.vel.z);
    const carDir = new Vector3(vehicle.forward.x, 0, vehicle.forward.z);
    let dir = carDir;
    if (velDir.lengthSq() > 6) {
      dir = velDir.normalize().multiplyScalar(0.65).add(carDir.normalize().multiplyScalar(0.35)).normalize();
    }

    const back = 5.2 + clamp01(speed / 45) * 1.4;
    const height = 2.05 + clamp01(speed / 45) * 0.35;
    const desired = new Vector3()
      .copy(vehicle.pos)
      .addScaledVector(dir, -back)
      .setY(vehicle.pos.y + height);

    // keep the camera above the ground
    this.camPos.lerp(desired, 1 - Math.exp(-9 * dt));
    this.camera.position.copy(this.camPos);

    const lookTarget = new Vector3()
      .copy(vehicle.pos)
      .addScaledVector(dir, 6.5)
      .addScaledVector(vehicle.rightVec, vehicle.steerBias * 2.2)
      .setY(vehicle.pos.y + 1.15);
    this.camLook.lerp(lookTarget, 1 - Math.exp(-11 * dt));
    this.camera.lookAt(this.camLook);

    // shake
    this.shakeTime += dt;
    this.shakeAmp = damp(this.shakeAmp, 0, 6, dt);
    if (this.shakeAmp > 0.0005) {
      const a = this.shakeAmp;
      this.camera.position.x += Math.sin(this.shakeTime * this.shakeFreq) * a;
      this.camera.position.y += Math.sin(this.shakeTime * this.shakeFreq * 1.7 + 1.1) * a * 0.7;
      this.camera.rotateZ(Math.sin(this.shakeTime * this.shakeFreq * 0.9) * a * 0.35);
    }

    // the light rig follows the car so the shadow cascade stays tight
    this.sun.position.set(vehicle.pos.x + 26, vehicle.pos.y + 44, vehicle.pos.z + 16);
    this.sun.target.position.copy(vehicle.pos);
    this.sun.target.updateMatrixWorld();

    const skyMat = (this.scene.children.find((o) => o instanceof Mesh) as Mesh)?.material as
      | ShaderMaterial
      | undefined;
    if (skyMat && skyMat.uniforms) skyMat.uniforms.canopy.value = lerp(0.25, 0.85, canopy);
    const fog = this.scene.fog as Fog;
    fog.near = lerp(55, 26, canopy);
    fog.far = lerp(280, 130, canopy);
  }

  render(): void {
    this.renderer.render(this.scene, this.camera);
  }
}

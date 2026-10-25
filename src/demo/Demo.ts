// Frontier Fluid demo application: three scenes, a drivable truck, floating
// props with real buoyancy, mouse water-stirring, and a live control panel.
import { FluidSim } from '../fluid/FluidSim';
import { WaterRenderer } from '../fluid/WaterRenderer';
import { ParticleRenderer } from '../fluid/ParticleRenderer';
import { BuoyantBody } from '../fluid/Buoyancy';
import { WATER_PRESET, MUD_PRESET, type FluidInteractor, type FluidSettings, type FluidSource } from '../fluid/types';
import { Renderer, type FrameData } from '../engine/Renderer';
import { Terrain } from '../engine/Terrain';
import { BoxRenderer, type BoxObject, type BoxStyle } from '../engine/Boxes';
import { camera, reflectionMatrix } from '../engine/camera';
import { SCENES, type SceneDef } from './scenes';
import { Vehicle, type VehicleInput } from './Vehicle';
import {
  mat4Invert, mat4Multiply, mat4FromTRS, vec3, clamp,
  type Mat4, type Vec3,
} from '../engine/math';

const SUN_DIR = vec3.normalize([-0.42, 0.60, -0.34] as Vec3);
const SUN_COLOR: Vec3 = [1.0, 0.92, 0.79];
const SUN_I = 1.12;
const FOG_COLOR: Vec3 = [0.62, 0.72, 0.79];

interface Prop {
  body: BuoyantBody;
  boxes: BoxObject[];
  styles: BoxStyle[];
  locals: Mat4[];
  kind: 'crate' | 'boat';
}

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

/** Surface GPU errors in-app (the preview runs in an iframe whose console
 *  the user can't see — a hidden validation error would look like a black
 *  screen with "no errors"). Visible text = a bug report we can act on. */
const shownGpuErrors = new Set<string>();
export function showGpuError(msg: string): void {
  const key = msg.slice(0, 160);
  if (shownGpuErrors.has(key) || shownGpuErrors.size > 6) return;
  shownGpuErrors.add(key);
  console.error('[gpu]', msg);
  const box = document.getElementById('gpuerr');
  const text = document.getElementById('gpuerr-text');
  if (box && text) {
    box.style.display = 'block';
    text.textContent += (text.textContent ? '\n— — —\n' : '') + msg;
  }
}

export class Demo {
  private device: GPUDevice;
  private renderer: Renderer;
  private sim: FluidSim;
  private water: WaterRenderer;
  private particles: ParticleRenderer;
  private terrain: Terrain;
  private boxRenderer: BoxRenderer;
  private camera = new camera();
  private vehicle = new Vehicle();
  private vehicleBoxes: BoxObject[] = [];
  private props: Prop[] = [];
  private scene: SceneDef = SCENES[0];
  private sources: FluidSource[] = [];
  private input: VehicleInput = { throttle: 0, steer: 0, handbrake: false };
  private keys = new Set<string>();
  private paused = false;
  private follow = true;
  private bombT = 0;
  private bombPos: Vec3 = [0, 0, 0];
  private stir: { pos: Vec3; prev: Vec3; active: boolean } = { pos: [0, 0, 0], prev: [0, 0, 0], active: false };
  private baseFoamRate = 1600;
  private baseSplashRate = 900;
  private baseWind: [number, number] = [0.6, 0.25];
  private lastT = 0;
  /** Frames remaining under a validation error scope (startup/scene loads). */
  private errorScopeFrames = 90;
  private fpsEMA = 60;
  private msEMA = 16;
  private statT = 0;
  private time = 0;

  private constructor(device: GPUDevice, canvas: HTMLCanvasElement) {
    this.device = device;
    this.renderer = new Renderer(device, canvas);
    this.sim = new FluidSim(device, { ...WATER_PRESET });
    this.water = new WaterRenderer(device, this.renderer);
    this.particles = new ParticleRenderer(device, this.renderer);
    this.terrain = new Terrain(device, this.renderer);
    this.boxRenderer = new BoxRenderer(device, this.renderer);
    for (let i = 0; i < 6; i++) this.vehicleBoxes.push(this.boxRenderer.createObject(device));
    this.handleResize();
  }

  static async create(canvas: HTMLCanvasElement): Promise<Demo> {
    if (!navigator.gpu) throw new Error('WebGPU is not supported in this browser.');
    const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
    if (!adapter) throw new Error('No WebGPU adapter available (GPU backend unavailable).');
    const device = await adapter.requestDevice({ label: 'frontier-fluid' });
    device.lost.then((info) => {
      const el = document.getElementById('fatal');
      const msg = document.getElementById('fatal-msg');
      if (el) el.style.display = 'flex';
      if (msg) msg.textContent = `GPU device lost: ${info.message}. Reload the page to restart.`;
    });
    // Route shader-compile errors (FluidSim/Renderer modules) + uncaptured
    // GPU errors to the on-screen overlay.
    (globalThis as unknown as { __gpuErr?: (m: string) => void }).__gpuErr = showGpuError;
    device.addEventListener?.('uncapturederror', (e) => {
      showGpuError((e as GPUUncapturedErrorEvent).error.message);
    });
    const demo = new Demo(device, canvas);
    demo.wireInput(canvas);
    demo.wireUI();
    demo.loadScene(SCENES[0]);
    return demo;
  }

  // ------------------------------------------------------------------ scenes
  loadScene(def: SceneDef, resOverride?: number): void {
    this.errorScopeFrames = Math.max(this.errorScopeFrames, 60);
    this.scene = def;
    const res = resOverride ?? def.preset.resolution;
    const settings: FluidSettings = { ...def.preset, resolution: res };
    this.sim.rebuild(settings);
    this.sim.boundaryMode = def.boundaryMode;
    this.sim.edgeWaveAmp = def.edgeWaveAmp;
    this.sim.edgeWaveFreq = def.edgeWaveFreq;

    const bed = def.build(res);
    this.sim.setBed(bed.heights, bed.flowX, bed.flowZ, bed.channel);
    this.sources = bed.sources;
    this.terrain.buildFromBed(bed.heights, res, settings.worldSize, settings.center);
    this.terrain.palette = def.palette;

    this.vehicle.respawn(def.vehicleSpawn.x, def.vehicleSpawn.z, def.vehicleSpawn.yaw, this.sim);
    this.camera.target = [def.cameraStart.x, 1.5, def.cameraStart.z];
    this.camera.yaw = def.cameraStart.yaw;
    this.camera.pitch = def.cameraStart.pitch;
    this.camera.distance = def.cameraStart.dist;

    this.props = def.props.map((p) => this.makeProp(p.kind, p.x, p.z, p.yaw));

    // reset fluid state
    const enc = this.device.createCommandEncoder({ label: 'scene-reset' });
    this.sim.reset(enc);
    this.device.queue.submit([enc.finish()]);

    // sync UI
    document.querySelectorAll<HTMLButtonElement>('[data-scene]').forEach((b) =>
      b.classList.toggle('active', b.dataset.scene === def.id));
    const sel = $('simres') as unknown as HTMLSelectElement;
    if ([...sel.options].some((o) => o.value === String(res))) sel.value = String(res);
    const isMud = def.preset.viscosity > 6;
    $('mat-water').classList.toggle('active', !isMud);
    $('mat-mud').classList.toggle('active', isMud);
    this.baseFoamRate = settings.foamRate;
    this.baseSplashRate = settings.splashRate;
    this.baseWind = [...settings.wind] as [number, number];
    // per-scene defaults for weather/flow sliders
    ($('rain') as unknown as HTMLInputElement).value = '0';
    ($('flow') as unknown as HTMLInputElement).value = '100';
    ($('foamamt') as unknown as HTMLInputElement).value = '100';
    ($('splashamt') as unknown as HTMLInputElement).value = '100';
    this.syncSlidersFromSettings();
    $('blurb-title').textContent = def.name;
    $('blurb-text').textContent = def.blurb;
    $('st-sim').textContent = `${res}² @ ${settings.worldSize} m`;
  }

  private makeProp(kind: 'crate' | 'boat', x: number, z: number, yaw: number): Prop {
    const bedY = this.sim.sampleBed(x, z);
    const wl = this.sim.settings.waterLevel;
    if (kind === 'crate') {
      const body = new BuoyantBody([x, Math.max(bedY, wl) + 0.8, z], {
        halfExtents: [0.44, 0.44, 0.44], mass: 115, volumeScale: 0.9,
      });
      body.orientation = [0, Math.sin(yaw / 2), 0, Math.cos(yaw / 2)];
      return {
        body, kind,
        boxes: [this.boxRenderer.createObject(this.device)],
        styles: [{ color: [0.55, 0.38, 0.20], color2: [0.38, 0.25, 0.12], planks: true, roughness: 0.75 }],
        locals: [mat4FromTRS([0, 0, 0], 0, [0.88, 0.88, 0.88])],
      };
    }
    const body = new BuoyantBody([x, Math.max(bedY, wl) + 0.8, z], {
      halfExtents: [1.0, 0.45, 2.2], mass: 1500, volumeScale: 0.8, linearDrag: 1.2, angularDrag: 2.6,
    });
    body.orientation = [0, Math.sin(yaw / 2), 0, Math.cos(yaw / 2)];
    return {
      body, kind,
      boxes: [this.boxRenderer.createObject(this.device), this.boxRenderer.createObject(this.device)],
      styles: [
        { color: [0.62, 0.16, 0.10], color2: [0.4, 0.08, 0.05], planks: true, roughness: 0.5 },
        { color: [0.85, 0.83, 0.78], roughness: 0.4 },
      ],
      locals: [
        mat4FromTRS([0, 0, 0], 0, [2.0, 0.9, 4.4]),
        mat4FromTRS([0, 0.78, -0.5], 0, [1.35, 0.7, 1.6]),
      ],
    };
  }

  private applyMaterial(which: 'water' | 'mud'): void {
    const p = which === 'water' ? WATER_PRESET : MUD_PRESET;
    const s = this.sim.settings;
    // material/fluid behaviour only — keep this scene's space & pacing
    s.viscosity = p.viscosity; s.damping = p.damping; s.bedFriction = p.bedFriction;
    s.absorption = [...p.absorption] as Vec3; s.scatterColor = [...p.scatterColor] as Vec3;
    s.deepColor = [...p.deepColor] as Vec3; s.roughness = p.roughness; s.density = p.density;
    s.ior = p.ior;
    s.splashColor = [...p.splashColor] as Vec3; s.foamColor = [...p.foamColor] as Vec3;
    s.foamLife = [...p.foamLife] as [number, number];
    this.baseFoamRate = p.foamRate;
    this.baseSplashRate = p.splashRate;
    this.baseWind = which === 'mud' ? [0, 0] : [0.6, 0.25];
    if (which === 'mud') s.wind = [0, 0];
    s.foamRate = this.baseFoamRate * (Number(($('foamamt') as unknown as HTMLInputElement).value) / 100);
    s.splashRate = this.baseSplashRate * (Number(($('splashamt') as unknown as HTMLInputElement).value) / 100);
    $('mat-water').classList.toggle('active', which === 'water');
    $('mat-mud').classList.toggle('active', which === 'mud');
    this.syncSlidersFromSettings();
  }

  private syncSlidersFromSettings(): void {
    const s = this.sim.settings;
    const set = (id: string, v: number) => { ($(id) as unknown as HTMLInputElement).value = String(Math.round(v)); };
    set('visc', 100 * Math.log(Math.max(s.viscosity, 0.05) / 0.05) / Math.log(3000));
    set('wind', Math.hypot(s.wind[0], s.wind[1]) / 3.52 * 100);
    set('swell', this.sim.edgeWaveAmp * 100);
    this.updateSliderLabels();
  }

  private updateSliderLabels(): void {
    const s = this.sim.settings;
    $('v-visc').textContent = s.viscosity < 3 ? `${s.viscosity.toFixed(1)} (watery)` : s.viscosity < 25 ? `${s.viscosity.toFixed(0)} (syrupy)` : `${s.viscosity.toFixed(0)} (MUD)`;
    const wl = Math.hypot(s.wind[0], s.wind[1]);
    $('v-wind').textContent = wl < 0.05 ? 'calm' : `${(wl * 6).toFixed(0)} km/h`;
    $('v-swell').textContent = `${this.sim.edgeWaveAmp.toFixed(2)} m`;
    $('v-rain').textContent = s.rainRate <= 0 ? 'dry' : s.rainRate < 0.003 ? 'drizzle' : 'downpour';
    $('v-flow').textContent = `${($('flow') as unknown as HTMLInputElement).value}%`;
    $('v-foam').textContent = `${($('foamamt') as unknown as HTMLInputElement).value}%`;
    $('v-splash').textContent = `${($('splashamt') as unknown as HTMLInputElement).value}%`;
  }

  // -------------------------------------------------------------------- input
  private wireInput(canvas: HTMLCanvasElement): void {
    addEventListener('keydown', (e) => {
      if (e.target instanceof HTMLInputElement || e.target instanceof HTMLSelectElement) return;
      const k = e.key.toLowerCase();
      this.keys.add(k);
      if (['arrowup', 'arrowdown', 'arrowleft', 'arrowright', ' '].includes(k)) e.preventDefault();
      if (k === 'b') this.triggerBomb();
      if (k === 'r') this.vehicle.respawn(this.scene.vehicleSpawn.x, this.scene.vehicleSpawn.z, this.scene.vehicleSpawn.yaw, this.sim);
      if (k === '1') this.loadScene(SCENES[0]);
      if (k === '2') this.loadScene(SCENES[1]);
      if (k === '3') this.loadScene(SCENES[2]);
    });
    addEventListener('keyup', (e) => this.keys.delete(e.key.toLowerCase()));
    addEventListener('blur', () => this.keys.clear());
    addEventListener('resize', () => this.handleResize());

    let dragging = false;
    let lastX = 0, lastY = 0;
    const pointers = new Map<number, { x: number; y: number }>();
    let pinchDist = 0;

    canvas.addEventListener('pointerdown', (e) => {
      canvas.setPointerCapture(e.pointerId);
      pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
      dragging = true; lastX = e.clientX; lastY = e.clientY;
      if (pointers.size === 2) {
        const [a, b] = [...pointers.values()];
        pinchDist = Math.hypot(a.x - b.x, a.y - b.y);
      }
      if (e.shiftKey) this.stir.active = true;
    });
    canvas.addEventListener('pointermove', (e) => {
      if (!pointers.has(e.pointerId)) return;
      pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
      if (pointers.size === 2) {
        const [a, b] = [...pointers.values()];
        const d = Math.hypot(a.x - b.x, a.y - b.y);
        if (pinchDist > 0) this.camera.distance = clamp(this.camera.distance * pinchDist / Math.max(d, 1), 6, this.sim.settings.worldSize);
        pinchDist = d;
        return;
      }
      if (!dragging) return;
      const dx = e.clientX - lastX, dy = e.clientY - lastY;
      lastX = e.clientX; lastY = e.clientY;
      if (e.shiftKey) {
        this.stir.active = true;
        this.updateStirTarget(e, canvas);
      } else {
        this.stir.active = false;
        this.camera.yaw -= dx * 0.0052;
        this.camera.pitch = clamp(this.camera.pitch + dy * 0.0042, 0.05, 1.35);
      }
    });
    const endPointer = (e: PointerEvent) => {
      pointers.delete(e.pointerId);
      if (pointers.size === 0) { dragging = false; this.stir.active = false; }
    };
    canvas.addEventListener('pointerup', endPointer);
    canvas.addEventListener('pointercancel', endPointer);
    canvas.addEventListener('wheel', (e) => {
      e.preventDefault();
      this.camera.distance = clamp(this.camera.distance * (1 + e.deltaY * 0.0011), 6, this.sim.settings.worldSize);
    }, { passive: false });
  }

  private updateStirTarget(e: PointerEvent, canvas: HTMLCanvasElement): void {
    const rect = canvas.getBoundingClientRect();
    const ndcX = ((e.clientX - rect.left) / rect.width) * 2 - 1;
    const ndcY = 1 - ((e.clientY - rect.top) / rect.height) * 2;
    const aspect = this.renderer.width / this.renderer.height;
    const proj = this.camera.proj(aspect);
    const invProj = mat4Invert(proj);
    const v4 = [
      invProj[0] * ndcX + invProj[4] * ndcY + invProj[8] + invProj[12],
      invProj[1] * ndcX + invProj[5] * ndcY + invProj[9] + invProj[13],
      invProj[2] * ndcX + invProj[6] * ndcY + invProj[10] + invProj[14],
      invProj[3] * ndcX + invProj[7] * ndcY + invProj[11] + invProj[15],
    ];
    const vd: Vec3 = [v4[0] / v4[3], v4[1] / v4[3], v4[2] / v4[3]];
    const view = this.camera.view();
    const right: Vec3 = [view[0], view[4], view[8]];
    const up: Vec3 = [view[1], view[5], view[9]];
    const back: Vec3 = [view[2], view[6], view[10]];
    const dir = vec3.normalize([
      right[0] * vd[0] + up[0] * vd[1] + back[0] * vd[2],
      right[1] * vd[0] + up[1] * vd[1] + back[1] * vd[2],
      right[2] * vd[0] + up[2] * vd[1] + back[2] * vd[2],
    ] as Vec3);
    const eye = this.camera.eye();
    const wl = this.sim.settings.waterLevel;
    if (Math.abs(dir[1]) < 0.02) return;
    const t = (wl - eye[1]) / dir[1];
    if (t < 0 || t > 600) return;
    this.stir.pos = [eye[0] + dir[0] * t, wl, eye[2] + dir[2] * t];
  }

  private wireUI(): void {
    document.querySelectorAll<HTMLButtonElement>('[data-scene]').forEach((b) => {
      b.addEventListener('click', () => {
        const def = SCENES.find((s) => s.id === b.dataset.scene);
        if (def) this.loadScene(def);
      });
    });
    $('mat-water').addEventListener('click', () => this.applyMaterial('water'));
    $('mat-mud').addEventListener('click', () => this.applyMaterial('mud'));

    const on = (id: string, fn: (el: HTMLInputElement) => void) => {
      const el = $(id) as unknown as HTMLInputElement;
      el.addEventListener('input', () => { fn(el); this.updateSliderLabels(); });
    };
    on('visc', (el) => {
      this.sim.settings.viscosity = 0.05 * Math.pow(3000, Number(el.value) / 100);
    });
    on('wind', (el) => {
      const m = Number(el.value) / 100 * 1.6;
      const bl = Math.hypot(this.baseWind[0], this.baseWind[1]) || 1;
      const dir: [number, number] = this.baseWind[0] === 0 && this.baseWind[1] === 0 ? [0.9, 0.35] : [this.baseWind[0] / bl, this.baseWind[1] / bl];
      this.sim.settings.wind = [dir[0] * m * 2.2, dir[1] * m * 2.2];
    });
    on('swell', (el) => { this.sim.edgeWaveAmp = Number(el.value) / 100; });
    on('rain', (el) => {
      const t = Number(el.value) / 100;
      this.sim.settings.rainRate = t * t * 0.014;
    });
    on('flow', () => undefined);
    on('foamamt', (el) => { this.sim.settings.foamRate = this.baseFoamRate * Number(el.value) / 100; });
    on('splashamt', (el) => { this.sim.settings.splashRate = this.baseSplashRate * Number(el.value) / 100; });

    ($('simres') as unknown as HTMLSelectElement).addEventListener('change', (e) => {
      this.loadScene(this.scene, Number((e.target as HTMLSelectElement).value));
    });
    const check = (id: string, fn: (v: boolean) => void) => {
      const el = $(id) as unknown as HTMLInputElement;
      el.addEventListener('change', () => fn(el.checked));
    };
    check('opt-reflection', (v) => { this.water.reflectionEnabled = v; });
    check('opt-caustics', (v) => { this.sim.causticsEnabled = v; if (!v) this.terrain.causticStrength = 0; else this.terrain.causticStrength = 1; });
    check('opt-particles', (v) => { this.particles.enabled = v; });
    check('opt-detail', (v) => { this.sim.settings.detailNormals = v; });
    check('opt-follow', (v) => { this.follow = v; });

    $('btn-bomb').addEventListener('click', () => this.triggerBomb());
    $('btn-drop').addEventListener('click', () => this.dropCrate());
    $('btn-reset').addEventListener('click', () => {
      const enc = this.device.createCommandEncoder();
      this.sim.reset(enc);
      this.device.queue.submit([enc.finish()]);
    });
    $('btn-pause').addEventListener('click', (e) => {
      this.paused = !this.paused;
      (e.target as HTMLButtonElement).textContent = this.paused ? '▶ Resume' : '⏸ Pause';
    });
  }

  private triggerBomb(): void {
    const f = this.vehicle.forward();
    const px = this.vehicle.pos[0] + f[0] * 7;
    const pz = this.vehicle.pos[2] + f[2] * 7;
    const sample = this.sim.sampleWater(px, pz);
    this.bombPos = sample.height !== -Infinity
      ? [px, sample.height, pz]
      : [this.camera.target[0], this.sim.settings.waterLevel, this.camera.target[2]];
    this.bombT = 0.4;
  }

  private dropCrate(): void {
    const f = this.vehicle.forward();
    const x = this.vehicle.pos[0] + f[0] * 5;
    const z = this.vehicle.pos[2] + f[2] * 5;
    const prop = this.makeProp('crate', x, z, Math.random() * 3);
    prop.body.position[1] = this.vehicle.pos[1] + 5;
    prop.body.velocity = [f[0] * 3, 1.5, f[2] * 3];
    prop.body.angularVelocity = [1.2, 0.5, 0.8];
    this.props.push(prop);
    if (this.props.length > 14) this.props.shift();
  }

  private handleResize(): void {
    const dpr = Math.min(devicePixelRatio || 1, 1.75);
    const w = Math.floor(innerWidth * dpr);
    const h = Math.floor(innerHeight * dpr);
    this.renderer.resize(w, h);
  }

  // ------------------------------------------------------------------ main loop
  start(): void {
    requestAnimationFrame((t) => this.frame(t));
  }

  private readInput(): VehicleInput {
    const k = this.keys;
    this.input.throttle = (k.has('w') || k.has('arrowup') ? 1 : 0) - (k.has('s') || k.has('arrowdown') ? 1 : 0);
    this.input.steer = (k.has('a') || k.has('arrowleft') ? 1 : 0) - (k.has('d') || k.has('arrowright') ? 1 : 0);
    this.input.handbrake = k.has(' ');
    return this.input;
  }

  private frame(tMs: number): void {
    requestAnimationFrame((t) => this.frame(t));
    const scoped = this.errorScopeFrames > 0;
    if (scoped) {
      this.errorScopeFrames--;
      this.device.pushErrorScope('validation');
    }
    const t0 = performance.now();
    const dt = this.lastT === 0 ? 1 / 60 : Math.min((tMs - this.lastT) / 1000, 0.1);
    this.lastT = tMs;
    this.time += dt;

    const input = this.readInput();
    if (!this.paused) {
      this.vehicle.update(dt, input, this.sim);
      for (const p of this.props) {
        p.body.update(dt * 0.5, this.sim);
        p.body.update(dt * 0.5, this.sim);
      }
    }

    // camera follow
    if (this.follow) {
      const k = Math.min(1, dt * 4.5);
      this.camera.target[0] += (this.vehicle.pos[0] - this.camera.target[0]) * k;
      this.camera.target[1] += (this.vehicle.pos[1] + 0.6 - this.camera.target[1]) * k;
      this.camera.target[2] += (this.vehicle.pos[2] - this.camera.target[2]) * k;
    }

    // frame uniforms
    const aspect = this.renderer.width / this.renderer.height;
    const view = this.camera.view();
    const proj = this.camera.proj(aspect);
    const invProj = mat4Invert(proj);
    const viewProj = mat4Multiply(proj, view);
    const wl = this.sim.settings.waterLevel;
    const common: Omit<FrameData, 'viewProj' | 'view' | 'camPos' | 'clipLevel' | 'clipEnabled' | 'screenSize'> = {
      proj, invProj,
      time: this.time,
      sunDir: SUN_DIR as unknown as Vec3,
      sunI: SUN_I,
      sunColor: SUN_COLOR,
      fogColor: FOG_COLOR,
      fogDensity: this.scene.fogDensity,
      waterLevel: wl,
    };
    const eye = this.camera.eye();
    this.renderer.writeFrame(0, {
      ...common, viewProj, view, camPos: eye,
      clipLevel: 0, clipEnabled: false,
      screenSize: [this.renderer.width, this.renderer.height],
    });
    const reflM = reflectionMatrix(wl);
    const viewR = mat4Multiply(view, reflM);
    const reflVP = mat4Multiply(proj, viewR);
    if (this.water.reflectionEnabled) {
      this.renderer.writeFrame(1, {
        ...common, viewProj: reflVP, view: viewR,
        camPos: [eye[0], 2 * wl - eye[1], eye[2]],
        clipLevel: wl - 0.05, clipEnabled: true,
        screenSize: [this.renderer.reflWidth, this.renderer.reflHeight],
      });
    }

    // update object uniforms
    this.updateObjectUniforms();

    const encoder = this.device.createCommandEncoder({ label: 'frame' });

    // ---- physics
    if (!this.paused) {
      const interactors: FluidInteractor[] = [...this.vehicle.interactors(input)];
      for (const p of this.props) {
        const bi = p.body.toInteractor();
        interactors.push({
          kind: p.kind === 'boat' ? 1 : 0,
          position: bi.position, velocity: bi.velocity,
          radius: bi.radius, halfHeight: p.body.halfExtents[1], strength: 0.7,
        });
      }
      if (this.stir.active) {
        const vel: Vec3 = [
          (this.stir.pos[0] - this.stir.prev[0]) / Math.max(dt, 0.001),
          -0.6,
          (this.stir.pos[2] - this.stir.prev[2]) / Math.max(dt, 0.001),
        ];
        interactors.push({
          kind: 0,
          position: [this.stir.pos[0], this.stir.pos[1] - 0.2, this.stir.pos[2]],
          velocity: [clamp(vel[0], -14, 14), vel[1], clamp(vel[2], -14, 14)],
          radius: 1.35, halfHeight: 1.35, strength: 1.5,
        });
      }
      this.stir.prev = [...this.stir.pos] as Vec3;
      if (this.bombT > 0) {
        this.bombT -= dt;
        interactors.push({
          kind: 0,
          position: this.bombPos,
          velocity: [0, -7, 0],
          radius: 1.7, halfHeight: 1.7, strength: 2.2,
        });
      }
      const flowScale = Number(($('flow') as unknown as HTMLInputElement).value) / 100;
      const sources = this.sources.map((s) => ({
        ...s,
        flow: s.flow * flowScale,
        velocity: [s.velocity[0] * Math.pow(Math.max(flowScale, 0.001), 0.7), s.velocity[1] * Math.pow(Math.max(flowScale, 0.001), 0.7)] as [number, number],
      }));
      if (this.bombT > 0) {
        sources.push({ position: this.bombPos, radius: 2.4, flow: 1.4, velocity: [0, 0] });
      }
      this.sim.step(encoder, dt, interactors, sources, SUN_DIR as unknown as [number, number, number]);
    }

    // ---- reflection pass
    if (this.water.reflectionEnabled) {
      const rp = encoder.beginRenderPass(this.renderer.reflectionPassDesc());
      this.renderer.drawSky(rp, this.renderer.frameOffsetRefl);
      this.terrain.draw(rp, this.renderer, this.sim, this.renderer.frameOffsetRefl);
      this.drawBoxes(rp, this.renderer.frameOffsetRefl);
      rp.end();
    }

    // ---- opaque pass
    const op = encoder.beginRenderPass(this.renderer.opaquePassDesc());
    this.renderer.drawSky(op, this.renderer.frameOffsetMain);
    this.terrain.draw(op, this.renderer, this.sim, this.renderer.frameOffsetMain);
    this.drawBoxes(op, this.renderer.frameOffsetMain);
    op.end();

    // ---- transparents
    this.renderer.copyForRefraction(encoder);
    const tp = encoder.beginRenderPass(this.renderer.transparentPassDesc());
    this.water.draw(tp, this.renderer, this.sim, reflVP);
    this.particles.draw(tp, this.renderer, this.sim);
    tp.end();

    this.renderer.present(encoder);
    this.device.queue.submit([encoder.finish()]);
    this.sim.pollReadback();
    if (scoped) {
      this.device.popErrorScope().then((err) => {
        if (err) showGpuError(err.message);
      }).catch(() => undefined);
    }

    // stats
    const ms = performance.now() - t0;
    this.msEMA = this.msEMA * 0.92 + ms * 0.08;
    this.fpsEMA = this.fpsEMA * 0.92 + (1 / Math.max(dt, 0.0001)) * 0.08;
    this.statT += dt;
    if (this.statT > 0.25) {
      this.statT = 0;
      $('st-fps').textContent = String(Math.round(this.fpsEMA));
      $('st-ms').textContent = `${this.msEMA.toFixed(1)} ms CPU`;
      const kmh = Math.abs(this.vehicle.speedFwd * 3.6).toFixed(0);
      $('st-truck').textContent = `${kmh} km/h`;
      $('st-state').textContent = this.paused
        ? '⏸ paused'
        : this.vehicle.floating
          ? '🌊 floating'
          : this.vehicle.submersion > 0.3
            ? '💦 fording deep'
            : this.vehicle.submersion > 0.06
              ? '💧 splashing'
              : '🏜 dry land';
    }
  }

  private updateObjectUniforms(): void {
    const mats = this.vehicle.partMatrices();
    const styles: BoxStyle[] = [
      { color: [0.85, 0.36, 0.08], color2: [0.55, 0.2, 0.04], roughness: 0.35 },
      { color: [0.14, 0.17, 0.21], roughness: 0.15 },
      { color: [0.05, 0.05, 0.055], roughness: 0.85 },
      { color: [0.05, 0.05, 0.055], roughness: 0.85 },
      { color: [0.05, 0.05, 0.055], roughness: 0.85 },
      { color: [0.05, 0.05, 0.055], roughness: 0.85 },
    ];
    this.vehicleBoxes.forEach((b, i) => b.update(this.device, mats[i], styles[i]));
    for (const p of this.props) {
      const m = p.body.matrix();
      p.boxes.forEach((b, i) => b.update(this.device, mat4Multiply(m, p.locals[i]), p.styles[i]));
    }
  }

  private drawBoxes(pass: GPURenderPassEncoder, frameOffset: number): void {
    for (const b of this.vehicleBoxes) this.boxRenderer.draw(pass, this.renderer, b, frameOffset);
    for (const p of this.props) for (const b of p.boxes) this.boxRenderer.draw(pass, this.renderer, b, frameOffset);
  }
}

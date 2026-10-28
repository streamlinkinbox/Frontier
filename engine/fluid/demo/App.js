// Demo application core (browser-independent so it also runs headless in Node for tests).
import { FluidSim, FluidRenderer, FluidQueryBatch } from '../src/index.js';
import { DomainLink } from '../src/DomainLink.js';
import { mat4, v3, quat, clamp } from '../src/math.js';
import { buildScene, buildCoarseWorld, swellField, sampleBilinear } from './scene.js';
import { SceneRenderer } from './SceneRenderer.js';
import { Vehicle } from './vehicle.js';
import { Props } from './props.js';
import { EditorCamera, CAMERA_SPEEDS } from './EditorCamera.js';
import { TerrainEditor } from './TerrainEditor.js';
import { screenRay, pickScene, rayHeightfield, objectBounds } from './picking.js';

export const CAMERA_MODES = ['chase', 'low', 'orbit', 'free'];
export { CAMERA_SPEEDS };

export const QUALITY = {
  low: { res: 256, particles: 131072 },
  medium: { res: 512, particles: 262144 },
  high: { res: 768, particles: 393216 },
  ultra: { res: 1024, particles: 524288 },
};

export class App {
  constructor(device, { quality = 'medium', width = 1280, height = 720, outputFormat = 'bgra8unorm', srgbOutput = true } = {}) {
    this.device = device;
    this.quality = quality;
    this.srgbOutput = srgbOutput;
    const q = QUALITY[quality] ?? QUALITY.medium;
    const t0 = performance.now();
    const sc = (this.scene = buildScene({ res: q.res }));
    const cw = (this.far = buildCoarseWorld(sc));
    this.buildMs = performance.now() - t0;

    // ---- coarse far-field simulation: the whole visible bay is simulated, no analytic ocean ring
    const coarse = (this.coarse = new FluidSim(device, {
      nx: cw.nx, ny: cw.ny, cellSize: cw.dx, origin: cw.origin,
      maxParticles: 2048, maxQueries: 64, maxEmitters: 8,
    }));
    coarse.params.seaLevel = sc.seaLevel;
    coarse.params.manning = 0.03;
    coarse.params.waveRampTime = 3;
    coarse.setBed(cw.bed);
    coarse.setBoundaries({ south: 'ocean', west: 'open', east: 'open', north: 'wall' });
    coarse.particles.params.enabled = false;
    coarse.particles.params.emitScale = 0;

    // ---- fine nested simulation: the 160 m playable world
    const sim = (this.sim = new FluidSim(device, { nx: sc.nx, ny: sc.ny, cellSize: sc.dx, origin: sc.origin, maxParticles: q.particles, maxQueries: 2048 }));
    sim.params.seaLevel = sc.seaLevel;
    sim.params.manning = 0.025;
    sim.setBed(sc.bed);
    sim.setWater(sc.depth, sc.mud, sc.vel);
    // the fine boundary is driven by the coarse sea (DomainLink), not by a local wave-maker
    sim.setBoundaries({ south: 'open', west: 'open', east: 'open', north: 'open' });
    this.swell = { height: 0.32, period: 7.5 };
    this._applyWaves(true);
    this.link = new DomainLink(sim, coarse);
    this.riverFlow = 2.6;
    this.riverSource = sc.river.addSource(sim, { flow: this.riverFlow, speed: 0.9, mud: 0.015 });

    // fine water: camera-adaptive mesh (near = full simulation resolution, far = decimated)
    this.fluidR = new FluidRenderer(sim, {
      colorFormat: 'rgba16float', depthFormat: 'depth32float', label: 'fluidNear',
      adaptive: true, lodN: q.res >= 768 ? 384 : 320, caustics: true, particles: true,
    });
    // coarse water: regular low-resolution mesh covering the rest of the visible sea
    this.fluidRFar = new FluidRenderer(coarse, {
      colorFormat: 'rgba16float', depthFormat: 'depth32float', label: 'fluidFar',
      adaptive: false, meshStep: 1, caustics: false, particles: false,
    });
    // Nesting seam: the coarse water is drawn everywhere (it is the fallback under the fine mesh) and
    // the fine water fades in over the last few metres of its domain. Inside the domain the fine
    // surface is opaque, so the two layers never composite on top of each other there; at the border
    // they cross-fade, which hides the join without a hard line or a gap of bare ground.
    const seam = 6;
    this.fluidRFar.settings.hole = null;
    this.fluidR.settings.hole = [sc.origin[0], sc.origin[1], sc.origin[0] + sc.size, sc.origin[1] + sc.size];
    this.fluidR.settings.holeMode = 2;
    this.fluidR.settings.holeFade = seam;
    this.renderer = new SceneRenderer(device, this.fluidR, sc, { outputFormat, width, height });
    this.renderer.attachCoarse(this.fluidRFar);
    this.terrain = new TerrainEditor(this);
    this.vehicle = new Vehicle(sc);
    this.props = new Props(sc);
    this.batch = new FluidQueryBatch(sim);
    this._spawnInitialProps();

    this.light = {
      sunDir: v3.norm([-0.36, 0.5, -0.79]),
      sunColor: [3.4, 3.1, 2.75],
      skyZenith: [0.15, 0.34, 0.8],
      skyHorizon: [0.6, 0.72, 0.88],
      fogDensity: 0.0026,
      fogHeight: 0.03,
    };
    const fov = (62 * Math.PI) / 180;
    this.cam = { mode: 'chase', yaw: 0, pitch: 0.24, dist: 8.5, fov, baseFov: fov, near: 0.1, pos: null, target: null, orbitYaw: 0, orbitPitch: 0, userYaw: 0, userPitch: 0 };
    this.lastFollowMode = 'chase';
    // Unreal-style free camera ('free' mode). It never goes below the terrain or the simulated water surface.
    const pad = 260;
    this.editorCam = new EditorCamera({
      fov,
      floor: (x, z) => this.cameraFloor(x, z),
      bounds: { min: [sc.origin[0] - pad, -40, sc.origin[1] - pad], max: [sc.origin[0] + sc.size + pad, 400, sc.origin[1] + sc.size + pad] },
    });
    /** selected object: null | { kind: 'vehicle' } | { kind: 'prop', item } */
    this.selection = null;
    // one GPU query point under the camera → water height for the camera floor (async, 1–3 frames old)
    this._camWater = null;
    this._camProbeXZ = [0, 0];
    this.camProbe = { count: 1, writeQueryPoints: (out, off) => {
      const p = this.cam.pos ?? this.vehicle.body.position;
      out[off * 2] = this._camProbeXZ[0] = p[0];
      out[off * 2 + 1] = this._camProbeXZ[1] = p[2];
    } };
    this.surfaceAt = (x, z) => Math.max(this.heightAt(x, z), this._staticWater(x, z));
    this.lastInput = null;
    this.rain = false;
    this.wind = 0;
    this.time = 0;
    this.paused = false;
    this.balls = [];
  }

  _applyWaves(initField = false) {
    const { height, period } = this.swell;
    const waves = [
      { amplitude: height, period, direction: Math.PI / 2 + 0.12 },
      { amplitude: height * 0.45, period: period * 0.62, direction: Math.PI / 2 - 0.32, phase: 1.3 },
    ];
    this.coarse.setWaves(waves);
    this.sim.setWaves([]);
    if (initField) {
      // seed the whole coarse sea with the same progressive swell the wave-maker maintains, so
      // waves do not need to travel 700 m from the boundary before the demo starts
      const sw = swellField(this.far, waves, this.coarse.waveDepth);
      this.coarse.setWater(sw.depth, this.far.mud, sw.vel);
    }
  }
  setSwell(height, period) { this.swell = { height, period: period ?? this.swell.period }; this._applyWaves(); }
  setRiverFlow(q) { this.riverFlow = q; this.sim.updateSource(this.riverSource, { flow: q }); }
  setWind(speed) {
    this.wind = speed;
    const dir = [0.6, -0.8];
    this.sim.params.wind = [dir[0] * speed, dir[1] * speed];
    this.sim.params.windGust = speed * 0.00035;
    this.sim.particles.params.wind = [dir[0] * speed, 0, dir[1] * speed];
    this.coarse.params.wind = [dir[0] * speed, dir[1] * speed];
    this.coarse.params.windGust = speed * 0.0002;
  }

  _spawnInitialProps() {
    const sc = this.scene;
    const rnd = (a, b) => a + Math.random() * (b - a);
    // flotsam in the bay
    for (let i = 0; i < 7; i++) {
      const x = rnd(-35, 35), z = rnd(-66, -52);
      this.props.spawn(i % 3 === 2 ? 'barrel' : 'crate', [x, 0.6, z]);
    }
    // crates upstream: carried down the river and over the rapids
    for (const s of [0.05, 0.1, 0.2, 0.26]) {
      const p = sc.river.pointAt(s).point;
      this.props.spawn(s > 0.15 ? 'barrel' : 'crate', [p[0], sc.river.bedAt(s) + 1.4, p[1]]);
    }
    // a couple near the downstream ford
    const f = sc.fords[1];
    this.props.spawn('barrel', [f.x - 3, f.level + 1, f.z + 2]);
  }

  dropCrates() {
    const b = this.vehicle.body;
    const fwd = b.dirToWorld([0, 0, 1]);
    for (let i = 0; i < 3; i++) {
      const p = v3.add(v3.madd(b.position, fwd, 7 + i * 1.3), [(i - 1) * 1.2, 4 + i * 0.8, 0]);
      this.props.spawn(i === 1 ? 'barrel' : 'crate', p, { velocity: v3.scale(b.linVel, 0.5) });
    }
  }

  /** heavy ball dropped into the nearest water in front of the camera – big physical splash */
  cannonball() {
    let p = null;
    if (this.cam.mode === 'free' && this.cam.pos) {
      // free camera: drop it where the camera is looking
      const hit = this.pickAt(0, 0, { objects: false, maxDist: 220 });
      if (hit) p = hit.point;
    }
    if (!p) {
      const t = this.cam.target ?? this.vehicle.body.position;
      const fwd = v3.norm(v3.sub(t, this.cam.pos ?? v3.add(t, [0, 3, -8])));
      p = v3.madd(t, [fwd[0], 0, fwd[2]], 10);
    }
    const it = this.props.spawn('ball', [p[0], this.heightAt(p[0], p[2]) + 9, p[2]], { velocity: [0, -6, 0] });
    this.balls.push({ it, t: 0 });
  }

  resetWater() {
    const sc = this.scene;
    this.sim.setWater(sc.depth, sc.mud, sc.vel);
    this.sim.particles.reset();
    const sw = swellField(this.far, this.coarse.waves, this.coarse.waveDepth);
    this.coarse.setWater(sw.depth, this.far.mud, sw.vel);
    this.coarse.particles.reset();
  }

  /**
   * Push edited heights/materials to the GPU: both nested beds (water surface preserved), the
   * coarse bed under the playable rectangle, and the high-resolution terrain mesh. Called by the
   * terrain brushes; the mesh rebuild is throttled because it is much heavier than the bed upload.
   */
  syncTerrain(force = false) {
    const sc = this.scene, cb = this.far.bed;
    // coarse bed under the fine rectangle follows the sculpted fine bed exactly
    const cw = this.far, fx0 = sc.origin[0], fz0 = sc.origin[1], fx1 = fx0 + sc.size, fz1 = fz0 + sc.size;
    const i0 = Math.max(0, Math.floor((fx0 - cw.origin[0]) / cw.dx) - 1);
    const i1 = Math.min(cw.nx - 1, Math.ceil((fx1 - cw.origin[0]) / cw.dx) + 1);
    const j0 = Math.max(0, Math.floor((fz0 - cw.origin[1]) / cw.dx) - 1);
    const j1 = Math.min(cw.ny - 1, Math.ceil((fz1 - cw.origin[1]) / cw.dx) + 1);
    for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) {
      const x = cw.origin[0] + (i + 0.5) * cw.dx, z = cw.origin[1] + (j + 0.5) * cw.dx;
      cb[j * cw.nx + i] = sampleBilinear(sc.bed, sc.nx, sc.ny, sc.dx, sc.origin, x, z, (xx, zz) => this.far.heightAt(xx, zz));
    }
    this.sim.applyBed(sc.bed);
    this.coarse.applyBed(cb);
    const now = performance.now();
    if (force || now - (this._terrainMeshT ?? 0) > 120) {
      this.renderer.updateNearTerrain();
      this._terrainMeshT = now;
    }
  }

  teleport(name) {
    const s = this.scene.scenarios[name];
    if (!s) return;
    this.vehicle.reset(s.position, s.heading);
    if (this.cam.mode === 'free') this.focusOn({ kind: 'vehicle' });
    else this.cam.pos = null;
  }

  // ---- camera modes, selection, picking ---------------------------------------------------------

  /** 'chase' | 'low' | 'orbit' follow the car; 'free' is the Unreal-style editor camera. */
  setCameraMode(mode) {
    const c = this.cam;
    if (!CAMERA_MODES.includes(mode) || mode === c.mode) return;
    if (mode === 'free') {
      // take over the current view so the switch is seamless
      if (c.pos && c.target) this.editorCam.setLookAt(c.pos, c.target);
      else this.editorCam.setLookAt(v3.add(this.vehicle.body.position, [0, 4, -9]), this.vehicle.body.position);
      this.editorCam.fov = this.editorCam.baseFov = c.baseFov;
    } else {
      this.lastFollowMode = mode;
      c.userYaw = c.userPitch = 0;
    }
    c.mode = mode;
  }
  /** F8: toggle between the free camera and the last car camera (Unreal's eject / possess). */
  toggleFreeCamera() { this.setCameraMode(this.cam.mode === 'free' ? this.lastFollowMode : 'free'); }
  cycleCamera() { this.setCameraMode(CAMERA_MODES[(CAMERA_MODES.indexOf(this.cam.mode) + 1) % CAMERA_MODES.length]); }
  setFov(rad) {
    this.cam.baseFov = rad;
    this.editorCam.baseFov = rad;
    this.editorCam.fov = rad;
  }

  /** LMB drag in the car camera modes: look around the car. */
  followDrag(dx, dy) {
    const c = this.cam;
    dx *= 0.006; dy *= 0.004;
    if (c.mode === 'orbit') { c.orbitYaw -= dx; c.orbitPitch += dy; }
    else { c.userYaw -= dx; c.userPitch = clamp(c.userPitch + dy, -0.2, 1.0); }
  }
  /** wheel in the car camera modes (deltaY > 0 = zoom out) */
  followZoom(deltaY) { this.cam.dist = clamp(this.cam.dist * Math.exp(deltaY * 0.001), 4, 40); }

  get aspect() { return this.renderer.width / this.renderer.height; }

  /** Ray cast through normalised device coords (x right, y up) of the current view. */
  pickAt(ndcX, ndcY, opts) {
    const c = this.cam;
    if (!c.pos || !c.target) return null;
    const { origin, dir } = screenRay(c.pos, c.target, c.fov, this.aspect, ndcX, ndcY);
    return pickScene(this, origin, dir, opts);
  }
  /** LMB click: select the car / prop under the cursor, or clear the selection. */
  selectAt(ndcX, ndcY) {
    const hit = this.pickAt(ndcX, ndcY);
    this.selection = hit?.object ?? null;
    return this.selection;
  }
  select(obj) { this.selection = obj ?? null; }
  selectionName(sel = this.selection) {
    if (!sel) return null;
    return sel.kind === 'vehicle' ? 'Car' : sel.item.type[0].toUpperCase() + sel.item.type.slice(1);
  }
  deleteSelection() {
    const sel = this.selection;
    if (sel?.kind !== 'prop') return false;
    this.props.remove(sel.item);
    this.balls = this.balls.filter((b) => b.it !== sel.item);
    this.selection = null;
    return true;
  }
  /** F: frame an object with the free camera (the selection, or the car when nothing is selected). */
  focusOn(obj = this.selection ?? { kind: 'vehicle' }, { animate = true } = {}) {
    this.setCameraMode('free');
    const b = objectBounds(obj, this.vehicle);
    this.editorCam.focus(() => objectBounds(obj, this.vehicle).center, b.radius, { aspect: this.aspect, duration: animate ? 0.35 : 0 });
  }
  focusSelection(opts) { this.focusOn(undefined, opts); }

  /**
   * Pivot for Alt+drag orbit / dolly / track: whatever is under the screen centre (the centre of a
   * car / prop, or the terrain / water point). Falls back to the camera's own point of interest.
   */
  pivotAtScreenCenter() {
    const hit = this.pickAt(0, 0, { maxDist: 150 });
    if (!hit) return null;
    return hit.object ? objectBounds(hit.object, this.vehicle).center : hit.point;
  }

  /** Terrain height anywhere in the visible world: the detailed nested bed or the coarse far field. */
  heightAt(x, z) {
    const sc = this.scene;
    if (x > sc.origin[0] && z > sc.origin[1] && x < sc.origin[0] + sc.size && z < sc.origin[1] + sc.size) return sc.heightAt(x, z);
    return this.far.heightAt(x, z);
  }

  /** lowest eye height for the free camera: above the terrain and the (simulated) water surface */
  cameraFloor(x, z) {
    let water = this._staticWater(x, z);
    const w = this._camWater;
    if (w && Math.hypot(w.x - x, w.z - z) < 3) water = w.surface;
    return Math.max(this.heightAt(x, z) + 0.35, water + 0.25);
  }

  // water surface of the initial state (sea + river + puddles) – used where no GPU sample exists
  _staticWater(x, z) {
    const sc = this.scene;
    const inside = x > sc.origin[0] && z > sc.origin[1] && x < sc.origin[0] + sc.size && z < sc.origin[1] + sc.size;
    if (!inside) return this.heightAt(x, z) < sc.seaLevel ? sc.seaLevel : -Infinity;
    const d = sampleBilinear(sc.depth, sc.nx, sc.ny, sc.dx, sc.origin, x, z);
    return d > 0.01 ? sc.heightAt(x, z) + d : -Infinity;
  }

  _readCameraProbe() {
    const r = this.batch.resultsFor(this.camProbe);
    if (!r) return;
    const k = r.offset * 8, depth = r.data[k + 1], surf = r.data[k + 6];
    this._camWater = { x: this._camProbeXZ[0], z: this._camProbeXZ[1], surface: depth > 0.003 && surf > -1e3 ? surf : -Infinity };
  }

  /** advance both nested simulations without rendering (settles the river and the swell after load) */
  warmup(seconds, dt = 1 / 30) {
    const steps = Math.round(seconds / dt);
    const keep = this.sim.particles.params.enabled;
    this.sim.particles.params.enabled = false;
    for (let s = 0; s < steps; s++) {
      const enc = this.device.createCommandEncoder();
      this.coarse.step(enc, dt);
      this.link.edge(enc);
      this.sim.step(enc, dt);
      this.link.interior(enc);
      this.device.queue.submit([enc.finish()]);
    }
    this.sim.particles.params.enabled = keep;
  }

  resize(w, h) { this.renderer.resize(w, h); }

  _updateCamera(dt, fly = null) {
    const c = this.cam, b = this.vehicle.body;
    if (c.mode === 'free') {
      const ec = this.editorCam;
      ec.update(dt, fly);
      c.pos = ec.position.slice();
      c.target = ec.target(10);
      c.fov = ec.fov;
      return;
    }
    c.fov += (c.baseFov - c.fov) * (1 - Math.exp(-dt * 8));
    const fwd = b.dirToWorld([0, 0, 1]);
    const flat = v3.norm([fwd[0], 0, fwd[2]]);
    let target, pos;
    if (c.mode === 'chase' || c.mode === 'low') {
      const low = c.mode === 'low';
      const yaw = Math.atan2(flat[0], flat[2]) + c.userYaw + Math.PI;
      const pitch = (low ? 0.07 : c.pitch) + c.userPitch;
      const dist = low ? 5.2 : c.dist;
      target = v3.add(b.position, [0, low ? 0.4 : 0.9, 0]);
      target = v3.madd(target, flat, low ? 0.5 : 1.8);
      pos = v3.add(target, [Math.sin(yaw) * Math.cos(pitch) * dist, Math.sin(pitch) * dist + (low ? 0.15 : 0.6), Math.cos(yaw) * Math.cos(pitch) * dist]);
      c.userYaw *= Math.exp(-dt * (c.dragging ? 0 : 0.8));
      c.userPitch *= Math.exp(-dt * (c.dragging ? 0 : 0.8));
    } else {
      // free orbit around the car (C to cycle modes)
      target = v3.add(b.position, [0, 0.8, 0]);
      const yaw = c.orbitYaw, pitch = clamp(0.3 + c.orbitPitch, 0.02, 1.45);
      pos = v3.add(target, [Math.sin(yaw) * Math.cos(pitch) * c.dist * 1.6, Math.sin(pitch) * c.dist * 1.6, Math.cos(yaw) * Math.cos(pitch) * c.dist * 1.6]);
    }
    const ground = this.heightAt(pos[0], pos[2]);
    pos[1] = Math.max(pos[1], ground + 0.6, this.scene.seaLevel + 0.5);
    const k = c.pos ? 1 - Math.exp(-dt * 7) : 1;
    c.pos = c.pos ? v3.lerp(c.pos, pos, k) : pos;
    c.target = c.target && c.pos ? v3.lerp(c.target, target, Math.min(1, k * 1.6)) : target;
  }

  _viewParams() {
    const c = this.cam, r = this.renderer, L = this.light;
    const aspect = r.width / r.height;
    const view = mat4.lookAt(c.pos, c.target, [0, 1, 0]);
    const proj = mat4.perspectiveReverseZ(c.fov, aspect, c.near);
    const viewProj = mat4.multiply(proj, view);
    const fwd = v3.norm(v3.sub(c.target, c.pos));
    const right = v3.norm(v3.cross(fwd, [0, 1, 0]));
    const up = v3.cross(right, fwd);
    const lightVP = r.lightMatrix(L.sunDir, c.mode === 'free' ? this._freeShadowFocus(fwd) : v3.madd(this.vehicle.body.position, fwd, 25), 75);
    return {
      lightVP,
      view: {
        viewProj, invViewProj: mat4.invert(viewProj), camPos: c.pos, near: c.near, width: r.width, height: r.height,
        camRight: right, camUp: up, camFwd: fwd, sunDir: L.sunDir, sunColor: L.sunColor, skyZenith: L.skyZenith, skyHorizon: L.skyHorizon,
        fogDensity: L.fogDensity, fogHeight: L.fogHeight, exposure: 1, time: this.sim.time, shadowMat: lightVP, shadowsOn: r.shadows,
      },
    };
  }

  // centre of the sun shadow map for the free camera: ground point up to 35 m ahead of the camera
  _freeShadowFocus(fwd) {
    const c = this.cam;
    let flat = [fwd[0], 0, fwd[2]];
    flat = Math.hypot(flat[0], flat[2]) < 1e-3 ? [0, 0, 1] : v3.norm(flat);
    let dist = 30;
    const t = rayHeightfield(c.pos, fwd, this.surfaceAt, 400);
    if (t !== null) dist = Math.min(35, Math.hypot(fwd[0] * t, fwd[2] * t));
    const x = c.pos[0] + flat[0] * dist, z = c.pos[2] + flat[2] * dist;
    return [x, Math.max(this.heightAt(x, z), this.scene.seaLevel), z];
  }

  _instances() {
    // a selected prop can be culled (e.g. washed out of the world)
    if (this.selection?.kind === 'prop' && !this.props.items.includes(this.selection.item)) this.selection = null;
    const sel = this.selection;
    const veh = this.vehicle.instances(), props = this.props.instances();
    // selection: pattern id + 10 puts the instance into the outline mask (SceneRenderer / SELECT)
    const mark = (it) => { it.params = [...(it.params ?? [0.5, 0, 0, 0])]; it.params[2] += 10; };
    if (sel?.kind === 'vehicle') veh.forEach(mark);
    else if (sel?.kind === 'prop') { const i = this.props.items.indexOf(sel.item); if (i >= 0) mark(props[i]); }
    return [...veh, ...props].sort((a, b) => (a.mesh < b.mesh ? -1 : a.mesh > b.mesh ? 1 : 0));
  }

  /**
   * One frame: physics -> fluid coupling -> GPU sim -> render into outputView.
   * input = { throttle, brake, steer, handbrake } in [0..1] / [-1..1], plus optional
   * input.camera = FlyInput (see EditorCamera.js) while the free camera is flying (RMB held).
   */
  frame(dt, input, outputView) {
    dt = Math.min(Math.max(dt, 0.0005), 1 / 20);
    const sim = this.sim, veh = this.vehicle;
    this.lastInput = input;
    if (!this.paused) {
      this.time += dt;
      // 1) rigid bodies using the latest (async) fluid samples
      const rw = this.batch.resultsFor(veh.wheelProbe);
      if (rw) veh.setWheelWater(rw.data, rw.offset);
      const rv = this.batch.resultsFor(veh.probe);
      veh.update(dt, input, rv);
      this.props.update(dt, veh, this.batch);
      this._readCameraProbe();
      for (const b of this.balls) b.t += dt;
      const old = this.balls.filter((b) => b.t > 14);
      for (const b of old) this.props.remove(b.it);
      this.balls = this.balls.filter((b) => b.t <= 14);

      // 2) bodies -> fluid
      sim.setPrimitives([...veh.primitives(), ...this.props.primitives()]);
      for (const e of veh.emitters(sim)) sim.emit(e);
      if (this.rain && this.cam.pos) {
        const w = sim.particles.params.wind;
        sim.emit({ position: [this.cam.pos[0], this.cam.pos[1] + 14, this.cam.pos[2]], velocity: [w[0], -7.5, w[2]], spread: 0.3, radius: [32, 32], rate: 5200, kind: 'rain', size: 0.006, volume: 2e-6, life: 4, fromWater: false, mud: 0 });
      }
      sim.params.rain = this.rain ? 3e-6 : 0;
      this.batch.setProbes([veh.probe, veh.wheelProbe, this.camProbe, ...this.props.items.map((i) => i.probe)]);
      this.batch.submit(0.035);
    }

    // 3) GPU: coarse sea -> fine boundary -> fine step -> fine interior back into the coarse sea
    const enc = this.device.createCommandEncoder({ label: 'demo.frame' });
    if (!this.paused) {
      this.coarse.step(enc, dt);
      this.link.edge(enc);
      sim.step(enc, dt);
      this.link.interior(enc);
    }
    this._updateCamera(dt, input?.camera ?? null);
    this.renderer.setBrush(this.terrain.enabled ? this.terrain.ringPoints() : null);
    const vp = this._viewParams();
    this.fluidR.updateView(vp.view);
    this.fluidRFar.updateView(vp.view);
    // outputView = null: simulate only (headless tools step the world without the render cost)
    if (outputView) {
      this.renderer.setInstances(this._instances());
      this.renderer.render(enc, outputView, { lightVP: vp.lightVP, time: this.time, srgbOutput: this.srgbOutput });
    }
    this.device.queue.submit([enc.finish()]);
    sim.afterSubmit();
    if (sim.frame % 15 === 0) sim.requestStats();
  }
}

// Demo application core (browser-independent so it also runs headless in Node for tests).
import { FluidSim, FluidRenderer, FluidQueryBatch } from '../src/index.js';
import { mat4, v3, quat, clamp } from '../src/math.js';
import { buildScene } from './scene.js';
import { SceneRenderer } from './SceneRenderer.js';
import { Vehicle } from './vehicle.js';
import { Props } from './props.js';

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
    this.buildMs = performance.now() - t0;

    const sim = (this.sim = new FluidSim(device, { nx: sc.nx, ny: sc.ny, cellSize: sc.dx, origin: sc.origin, maxParticles: q.particles, maxQueries: 2048 }));
    sim.params.seaLevel = sc.seaLevel;
    sim.params.manning = 0.025;
    sim.setBed(sc.bed);
    sim.setWater(sc.depth, sc.mud, sc.vel);
    sim.setBoundaries({ south: 'ocean', west: 'open', east: 'open', north: 'wall' });
    this.swell = { height: 0.32, period: 7.5 };
    this._applyWaves();
    this.riverFlow = 2.6;
    this.riverSource = sc.river.addSource(sim, { flow: this.riverFlow, speed: 0.9, mud: 0.015 });

    this.fluidR = new FluidRenderer(sim, { colorFormat: 'rgba16float', depthFormat: 'depth32float' });
    this.renderer = new SceneRenderer(device, this.fluidR, sc, { outputFormat, width, height });
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
    this.cam = { mode: 'chase', yaw: 0, pitch: 0.24, dist: 8.5, fov: (62 * Math.PI) / 180, near: 0.1, pos: null, target: null, orbitYaw: 0, orbitPitch: 0, userYaw: 0, userPitch: 0 };
    this.rain = false;
    this.wind = 0;
    this.time = 0;
    this.paused = false;
    this.balls = [];
  }

  _applyWaves() {
    const { height, period } = this.swell;
    this.sim.setWaves([
      { amplitude: height, period, direction: Math.PI / 2 + 0.12 },
      { amplitude: height * 0.45, period: period * 0.62, direction: Math.PI / 2 - 0.32, phase: 1.3 },
    ]);
  }
  setSwell(height, period) { this.swell = { height, period: period ?? this.swell.period }; this._applyWaves(); }
  setRiverFlow(q) { this.riverFlow = q; this.sim.updateSource(this.riverSource, { flow: q }); }
  setWind(speed) {
    this.wind = speed;
    const dir = [0.6, -0.8];
    this.sim.params.wind = [dir[0] * speed, dir[1] * speed];
    this.sim.params.windGust = speed * 0.00035;
    this.sim.particles.params.wind = [dir[0] * speed, 0, dir[1] * speed];
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
    const t = this.cam.target ?? this.vehicle.body.position;
    const fwd = v3.norm(v3.sub(t, this.cam.pos ?? v3.add(t, [0, 3, -8])));
    const p = v3.madd(t, [fwd[0], 0, fwd[2]], 10);
    const it = this.props.spawn('ball', [p[0], this.scene.heightAt(p[0], p[2]) + 9, p[2]], { velocity: [0, -6, 0] });
    this.balls.push({ it, t: 0 });
  }

  resetWater() {
    const sc = this.scene;
    this.sim.setWater(sc.depth, sc.mud, sc.vel);
    this.sim.particles.reset();
  }

  teleport(name) {
    const s = this.scene.scenarios[name];
    if (s) { this.vehicle.reset(s.position, s.heading); this.cam.pos = null; }
  }

  /** advance the simulation without rendering (settles the river after load) */
  warmup(seconds, dt = 1 / 30) {
    const steps = Math.round(seconds / dt);
    const keep = this.sim.particles.params.enabled;
    this.sim.particles.params.enabled = false;
    for (let s = 0; s < steps; s++) {
      const enc = this.device.createCommandEncoder();
      this.sim.step(enc, dt);
      this.device.queue.submit([enc.finish()]);
    }
    this.sim.particles.params.enabled = keep;
  }

  resize(w, h) { this.renderer.resize(w, h); }

  _updateCamera(dt) {
    const c = this.cam, b = this.vehicle.body;
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
    const ground = this.scene.heightAt(pos[0], pos[2]);
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
    const lightVP = r.lightMatrix(L.sunDir, v3.madd(this.vehicle.body.position, fwd, 25), 75);
    return {
      lightVP,
      view: {
        viewProj, invViewProj: mat4.invert(viewProj), camPos: c.pos, near: c.near, width: r.width, height: r.height,
        camRight: right, camUp: up, camFwd: fwd, sunDir: L.sunDir, sunColor: L.sunColor, skyZenith: L.skyZenith, skyHorizon: L.skyHorizon,
        fogDensity: L.fogDensity, fogHeight: L.fogHeight, exposure: 1, time: this.sim.time, shadowMat: lightVP, shadowsOn: r.shadows,
      },
    };
  }

  /**
   * One frame: physics -> fluid coupling -> GPU sim -> render into outputView.
   * input = { throttle, brake, steer, handbrake } in [0..1] / [-1..1]
   */
  frame(dt, input, outputView) {
    dt = Math.min(Math.max(dt, 0.0005), 1 / 20);
    const sim = this.sim, veh = this.vehicle;
    if (!this.paused) {
      this.time += dt;
      // 1) rigid bodies using the latest (async) fluid samples
      const rw = this.batch.resultsFor(veh.wheelProbe);
      if (rw) veh.setWheelWater(rw.data, rw.offset);
      const rv = this.batch.resultsFor(veh.probe);
      veh.update(dt, input, rv);
      this.props.update(dt, veh, this.batch);
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
      this.batch.setProbes([veh.probe, veh.wheelProbe, ...this.props.items.map((i) => i.probe)]);
      this.batch.submit(0.035);
    }

    // 3) GPU
    const enc = this.device.createCommandEncoder({ label: 'demo.frame' });
    if (!this.paused) sim.step(enc, dt);
    this._updateCamera(dt);
    const vp = this._viewParams();
    this.fluidR.updateView(vp.view);
    this.renderer.setInstances([...veh.instances(), ...this.props.instances()].sort((a, b) => (a.mesh < b.mesh ? -1 : a.mesh > b.mesh ? 1 : 0)));
    this.renderer.render(enc, outputView, { lightVP: vp.lightVP, time: this.time, srgbOutput: this.srgbOutput });
    this.device.queue.submit([enc.finish()]);
    sim.afterSubmit();
    if (sim.frame % 15 === 0) sim.requestStats();
  }
}

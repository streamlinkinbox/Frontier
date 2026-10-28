// FluidParticles – GPU spray / mist / rain particles coupled to FluidSim.
import { EMIT_SURFACE, EMIT_CPU, SIMULATE, FINALIZE } from './shaders/particles.wgsl.js';

export const PARTICLE_KIND = { spray: 1, mist: 3, rain: 4 };
const PP_WORDS = 32;

export class FluidParticles {
  constructor(sim, maxParticles = 262144, maxEmitters = 64) {
    this.sim = sim;
    this.device = sim.device;
    this.max = maxParticles;
    this.maxEmitters = maxEmitters;
    this._emitters = [];
    this.params = {
      enabled: true,
      gravity: 9.81,
      airDrag: 0.004,          // quadratic drag coefficient / droplet size (terminal ~6.5 m/s at 18 mm)
      // Ihmsen et al. potential ranges [min, max] (clamped & normalised)
      taMin: 0.8, taMax: 5.0,    // trapped air: relative velocity of neighbouring columns (m/s)
      wcMin: 0.10, wcMax: 0.6,   // wave crest: curvature * dx of a surface moving along its normal
      keMin: 0.7, keMax: 10.0,   // kinetic energy per unit mass (m^2/s^2)
      kTA: 1.0, kWC: 1.6,
      emitScale: 700,            // particles per m^2 per second at full potential
      maxPerCell: 8,
      sprayVolume: 1.2e-5,       // m^3 of water carried by one spray particle (12 ml)
      minDepth: 0.015,
      sprayLife: 3.0, mistLife: 1.6,
      spraySize: 0.024, mistSize: 0.32,
      mistFraction: 0.12,
      sprayUp: 0.35,
      wind: [0, 0, 0],
    };
    this._pp = new ArrayBuffer(PP_WORDS * 4);
    this._f = new Float32Array(this._pp);
    this._u = new Uint32Array(this._pp);
    this._create();
  }

  _create() {
    const d = this.device, N = this.max;
    const S = GPUBufferUsage.STORAGE, CD = GPUBufferUsage.COPY_DST, CS = GPUBufferUsage.COPY_SRC;
    this.uniform = d.createBuffer({ label: 'particles.params', size: PP_WORDS * 4, usage: GPUBufferUsage.UNIFORM | CD });
    this.particles = d.createBuffer({ label: 'particles.pool', size: N * 48, usage: S | CD });
    this.deadList = d.createBuffer({ label: 'particles.dead', size: N * 4, usage: S | CD });
    this.drawList = d.createBuffer({ label: 'particles.draw', size: N * 4, usage: S });
    this.counters = d.createBuffer({ label: 'particles.counters', size: 16, usage: S | CD | CS });
    this.drawArgs = d.createBuffer({ label: 'particles.drawArgs', size: 16, usage: S | GPUBufferUsage.INDIRECT | CD });
    this.emitterBuf = d.createBuffer({ label: 'particles.emitters', size: this.maxEmitters * 64, usage: S | CD });
    this.reset();
    const mk = (code, label) => d.createComputePipeline({ layout: 'auto', label, compute: { module: d.createShaderModule({ code, label }), entryPoint: 'main' } });
    this.p = {
      emitSurface: mk(EMIT_SURFACE, 'particles.emitSurface'),
      emitCpu: mk(EMIT_CPU, 'particles.emitCpu'),
      simulate: mk(SIMULATE, 'particles.simulate'),
      finalize: mk(FINALIZE, 'particles.finalize'),
    };
  }

  /** Kill all particles. */
  reset() {
    const d = this.device, N = this.max;
    d.queue.writeBuffer(this.particles, 0, new Float32Array(N * 12));
    const dead = new Uint32Array(N);
    for (let i = 0; i < N; i++) dead[i] = N - 1 - i;
    d.queue.writeBuffer(this.deadList, 0, dead);
    d.queue.writeBuffer(this.counters, 0, new Int32Array([N, 0, 0, 0]));
    d.queue.writeBuffer(this.drawArgs, 0, new Uint32Array([6, 0, 0, 0]));
  }

  createBindGroups() {
    const s = this.sim, U = s.simUniform, P = this.uniform;
    this.bg = {
      emitSurface: s.bindGroup(this.p.emitSurface, [U, P, s.hc[0], s.vel[0], s.bed, s.aux, this.particles, this.deadList, this.counters, s.deposit], 'bg.emitSurface'),
      emitCpu: s.bindGroup(this.p.emitCpu, [U, P, this.emitterBuf, s.hc[0], s.bed, this.particles, this.deadList, this.counters, s.deposit], 'bg.emitCpu'),
      simulate: s.bindGroup(this.p.simulate, [
        { binding: 0, resource: U }, { binding: 1, resource: P }, { binding: 2, resource: s.hc[0] },
        { binding: 4, resource: s.bed }, { binding: 5, resource: this.particles },
        { binding: 6, resource: this.deadList }, { binding: 7, resource: this.drawList },
        { binding: 8, resource: this.counters }, { binding: 9, resource: s.deposit },
      ], 'bg.simulate'),
      finalize: s.bindGroup(this.p.finalize, [this.counters, this.drawArgs], 'bg.finalize'),
    };
  }

  /**
   * Queue an emitter for the next step.
   * { position:[x,y,z], velocity:[vx,vy,vz], spread (m/s), radius (m or [rx,rz]), count | rate (per s),
   *   kind: 'spray'|'mist'|'rain', size, volume (m^3 each), life (s), mud (0..1, -1 = from grid),
   *   fromWater (true: must take real water from the grid at that spot) }
   */
  emit(e) {
    if (this._emitters.length < this.maxEmitters) this._emitters.push(e);
  }

  pack(frameDt) {
    const f = this._f, u = this._u, P = this.params;
    u[0] = this.max; u[1] = this.sim.frame; u[3] = P.enabled ? 1 : 0;
    f[4] = frameDt; f[5] = this.sim.time; f[6] = P.gravity; f[7] = P.airDrag;
    f[8] = P.taMin; f[9] = P.taMax; f[10] = P.wcMin; f[11] = P.wcMax;
    f[12] = P.keMin; f[13] = P.keMax; f[14] = P.kTA; f[15] = P.kWC;
    f[16] = P.emitScale; f[17] = P.maxPerCell; f[18] = P.sprayVolume; f[19] = P.minDepth;
    f[20] = P.sprayLife; f[21] = P.mistLife; f[22] = P.spraySize; f[23] = P.mistSize;
    f[24] = P.mistFraction; f[25] = P.sprayUp;
    f[26] = P.wind[0]; f[27] = P.wind[1]; f[28] = P.wind[2];
    // emitters
    const n = this._emitters.length;
    let active = 0;
    if (n) {
      const data = new Float32Array(n * 16);
      for (const e of this._emitters) {
        let count = e.count ?? 0;
        if (e.rate != null) { const x = e.rate * frameDt; count = Math.floor(x) + (Math.random() < x % 1 ? 1 : 0); }
        if (count <= 0) continue;
        const o = active * 16;
        const p = e.position, v = e.velocity ?? [0, 0, 0];
        const r = Array.isArray(e.radius) ? e.radius : [e.radius ?? 0.1, e.radius ?? 0.1];
        const kind = PARTICLE_KIND[e.kind ?? 'spray'] ?? 1;
        data[o] = p[0]; data[o + 1] = p[1]; data[o + 2] = p[2]; data[o + 3] = Math.min(count, 16384);
        data[o + 4] = v[0]; data[o + 5] = v[1]; data[o + 6] = v[2]; data[o + 7] = e.spread ?? 0.5;
        data[o + 8] = r[0]; data[o + 9] = r[1]; data[o + 10] = kind; data[o + 11] = (e.fromWater ?? (kind === 1)) ? 1 : 0;
        data[o + 12] = e.size ?? (kind === 3 ? P.mistSize : P.spraySize);
        data[o + 13] = e.volume ?? (kind === 1 || kind === 4 ? P.sprayVolume : 0);
        data[o + 14] = e.life ?? (kind === 3 ? P.mistLife : P.sprayLife);
        data[o + 15] = e.mud ?? -1;
        active++;
      }
      if (active) this.device.queue.writeBuffer(this.emitterBuf, 0, data, 0, active * 16);
    }
    u[2] = active;
    this._activeEmitters = active;
    this._emitters.length = 0;
    this.device.queue.writeBuffer(this.uniform, 0, this._pp);
  }

  record(pass, gx, gy) {
    const run = (pipe, bg, x, y = 1) => { pass.setPipeline(pipe); pass.setBindGroup(0, bg); pass.dispatchWorkgroups(x, y); };
    run(this.p.emitSurface, this.bg.emitSurface, gx, gy);
    if (this._activeEmitters) run(this.p.emitCpu, this.bg.emitCpu, 16, this._activeEmitters);
    run(this.p.simulate, this.bg.simulate, Math.ceil(this.max / 64));
    run(this.p.finalize, this.bg.finalize, 1);
  }

  destroy() {
    for (const b of [this.uniform, this.particles, this.deadList, this.drawList, this.counters, this.drawArgs, this.emitterBuf]) b.destroy();
  }
}

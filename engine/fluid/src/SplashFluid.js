// SplashFluid – volumetric splash water (tyre sheets, bow splashes, impact crowns) as a 3D
// Position Based Fluid, two-way coupled with FluidSim: every particle is a parcel of real water
// taken from the grid where it is thrown and returned exactly where it lands (into the water body,
// leaving foam, or soaking into the ground). Rendered as a continuous liquid by SplashRenderer.
import {
  SPLASH_TABLE, SPLASH_EMIT, SPLASH_CLEAR, SPLASH_PREDICT, SPLASH_SCAN_BLOCKS, SPLASH_SCAN_SUMS, SPLASH_SCAN_ADD,
  SPLASH_SCATTER, SPLASH_LAMBDA, SPLASH_DELTA, SPLASH_VELOCITY, SPLASH_FINISH, SPLASH_CLASSIFY, SPLASH_FOAM, SPLASH_DRAWLIST, SPLASH_FINALIZE,
} from './shaders/splash.wgsl.js';

const MAX_EMITTERS = 64;
const MAX_FOAM_REQ = 4096;

export class SplashFluid {
  /**
   * @param {import('./FluidSim.js').FluidSim} sim
   * @param {{maxParticles?: number, spacing?: number}} [o]
   */
  constructor(sim, o = {}) {
    this.sim = sim;
    this.device = sim.device;
    this.max = o.maxParticles ?? 32768;
    const s = o.spacing ?? 0.035;              // rest spacing of the liquid particles (m)
    this.params = {
      enabled: true,
      spacing: s,
      vol: s * s * s,                          // water per particle (m³): 43 ml at 3.5 cm
      h: 2 * s,                                // kernel radius
      radius: 0.5 * s,                         // collision radius
      renderRadius: 0.8 * s,                   // sphere radius for screen-space rendering
      subdivision: 1,                          // 'fake' finer parcels for rendering only (looks less blobby)
      substepDt: 1 / 120,
      iterations: 2,                           // Jacobi iterations per substep (even)
      eps: 30,                                 // constraint relaxation (CFM), ~15% of sum |grad C|^2 at rest
      scorrK: 0.002, scorrDq: 0.2,             // artificial pressure (anti-clumping), scaled to this normalisation
      maxCorr: 0.2,                            // position correction cap per iteration (x spacing)
      xsph: 0.08,                              // XSPH viscosity (coherent motion inside sheets)
      cohesion: 220,                           // surface tension (Akinci et al. 2013) – holds sheets at this resolution
      gravity: 9.81,
      drag: 0.02,                              // quadratic air drag (1/m)
      maxSpeed: 20,
      life: 4,                                 // s before an airborne parcel is returned anyway
      absorbAge: 0.06,                         // s a fresh particle cannot merge back
      foamProb: 0.12, foamSize: 0.24, foamLife: 8, foamAmount: 0.4,
      wind: [0, 0],
    };
    this.frame = 0;
    this.time = 0;
    this.head = 0;
    this._emitters = [];
    this.lastEmit = -1e9;
    this.stats = { alive: 0 };
    this._create();
  }

  /** Something may be airborne (cheap CPU-side test used to skip simulation and rendering). */
  get active() { return this.params.enabled && this.time - this.lastEmit < this.params.life * 1.35 + 0.5; }

  _create() {
    const d = this.device, N = this.max, T = SPLASH_TABLE;
    const S = GPUBufferUsage.STORAGE, CD = GPUBufferUsage.COPY_DST, CS = GPUBufferUsage.COPY_SRC;
    const buf = (size, usage, label) => d.createBuffer({ size, usage, label });
    this.uniform = buf(144, GPUBufferUsage.UNIFORM | CD, 'splash.params');
    this.pos = buf(N * 16, S | CD | CS, 'splash.pos');          // xyz, life
    this.vel = buf(N * 16, S | CD | CS, 'splash.vel');          // xyz, mud
    this.pred = [buf(N * 16, S | CD, 'splash.predA'), buf(N * 16, S | CD, 'splash.predB')];   // xyz, age
    this.lambda = buf(N * 8, S | CS, 'splash.lambda');              // lambda, density
    this.render = [buf(N * 8, S | CS, 'splash.renderA'), buf(N * 8, S | CS, 'splash.renderB')];   // liquid flag, smoothed density
    this.cellRank = buf(N * 8, S, 'splash.cellRank');
    this.cellCount = buf(T * 4, S, 'splash.cellCount');
    this.cellStart = buf(T * 4, S, 'splash.cellStart');
    this.blockSums = buf((T / 256) * 4, S, 'splash.blockSums');
    this.sorted = buf(N * 4, S, 'splash.sorted');
    this.draw = buf((N + 1) * 4, S | CD, 'splash.draw');         // [count, ids...]
    this.drawArgs = buf(32, S | GPUBufferUsage.INDIRECT | CD | CS, 'splash.drawArgs');
    this.foamReq = buf((1 + MAX_FOAM_REQ * 6) * 4, S | CD, 'splash.foamReq');
    this.emitterBuf = buf(MAX_EMITTERS * 64, S | CD, 'splash.emitters');
    this.reset();

    const mk = (code, label) => d.createComputePipeline({ layout: 'auto', label, compute: { module: d.createShaderModule({ code, label }), entryPoint: 'main' } });
    this.p = {
      emit: mk(SPLASH_EMIT, 'splash.emit'), clear: mk(SPLASH_CLEAR, 'splash.clear'), predict: mk(SPLASH_PREDICT, 'splash.predict'),
      scanBlocks: mk(SPLASH_SCAN_BLOCKS, 'splash.scanBlocks'), scanSums: mk(SPLASH_SCAN_SUMS, 'splash.scanSums'), scanAdd: mk(SPLASH_SCAN_ADD, 'splash.scanAdd'),
      scatter: mk(SPLASH_SCATTER, 'splash.scatter'), lambda: mk(SPLASH_LAMBDA, 'splash.lambda'), delta: mk(SPLASH_DELTA, 'splash.delta'),
      classify: mk(SPLASH_CLASSIFY, 'splash.classify'),
      velocity: mk(SPLASH_VELOCITY, 'splash.velocity'), finish: mk(SPLASH_FINISH, 'splash.finish'), foam: mk(SPLASH_FOAM, 'splash.foam'),
      drawList: mk(SPLASH_DRAWLIST, 'splash.drawList'), finalize: mk(SPLASH_FINALIZE, 'splash.finalize'),
    };
    const sim = this.sim, U = this.uniform, [A, B] = this.pred, part = sim.particles;
    const bg = (pipe, list, label) => d.createBindGroup({ label, layout: pipe.getBindGroupLayout(0), entries: list.map((b, i) => ({ binding: i, resource: { buffer: b } })) });
    const grid = [this.cellRank, this.cellStart, this.cellCount, this.sorted];
    this.bg = {
      emit: bg(this.p.emit, [sim.simUniform, U, this.emitterBuf, sim.hc[0], sim.bed, this.pos, this.vel, A, sim.deposit], 'splash.bg.emit'),
      clear: bg(this.p.clear, [this.cellCount], 'splash.bg.clear'),
      predict: bg(this.p.predict, [U, this.pos, this.vel, A, this.cellRank, this.cellCount], 'splash.bg.predict'),
      scanBlocks: bg(this.p.scanBlocks, [this.cellCount, this.cellStart, this.blockSums], 'splash.bg.scanBlocks'),
      scanSums: bg(this.p.scanSums, [this.blockSums], 'splash.bg.scanSums'),
      scanAdd: bg(this.p.scanAdd, [this.cellStart, this.blockSums], 'splash.bg.scanAdd'),
      scatter: bg(this.p.scatter, [U, this.cellRank, this.cellStart, this.sorted], 'splash.bg.scatter'),
      lambdaA: bg(this.p.lambda, [U, A, this.lambda, ...grid], 'splash.bg.lambdaA'),
      lambdaB: bg(this.p.lambda, [U, B, this.lambda, ...grid], 'splash.bg.lambdaB'),
      deltaAB: bg(this.p.delta, [U, A, B, this.lambda, ...grid, sim.primBuf], 'splash.bg.deltaAB'),
      deltaBA: bg(this.p.delta, [U, B, A, this.lambda, ...grid, sim.primBuf], 'splash.bg.deltaBA'),
      velocity: bg(this.p.velocity, [U, this.pos, A, this.vel, ...grid], 'splash.bg.velocity'),
      classify: bg(this.p.classify, [U, A, ...grid, this.render[0], this.render[1]], 'splash.bg.classify'),
      finish: bg(this.p.finish, [sim.simUniform, U, sim.hc[0], sim.bed, this.pos, this.vel, A, sim.deposit, this.foamReq], 'splash.bg.finish'),
      foam: bg(this.p.foam, [U, this.foamReq, part.particles, part.deadList, part.counters], 'splash.bg.foam'),
      drawList: bg(this.p.drawList, [U, this.pos, this.draw], 'splash.bg.drawList'),
      finalize: bg(this.p.finalize, [this.draw, this.drawArgs, this.foamReq], 'splash.bg.finalize'),
    };
  }

  /** Remove all splash particles (their water is not returned – use after resetting the water). */
  reset() {
    const d = this.device, N = this.max;
    d.queue.writeBuffer(this.pos, 0, new Float32Array(N * 4));
    d.queue.writeBuffer(this.draw, 0, new Uint32Array(1));
    d.queue.writeBuffer(this.drawArgs, 0, new Uint32Array([6, 0, 0, 0, 0, 0, 0, 0]));
    d.queue.writeBuffer(this.foamReq, 0, new Uint32Array(1));
    this.head = 0;
    this.lastEmit = -1e9;
  }

  /**
   * Throw splash water this frame.
   * { position:[x,y,z], velocity:[vx,vy,vz], spread (m/s), radius (m or [rx,rz]),
   *   volume (m³ this frame) | rate (m³/s), fromWater (default true: the water is taken from the
   *   grid there), mud (0..1, -1 = from the water), life (s), ring (crown on a circle of radius),
   *   radialSpeed (m/s, ring only) }
   */
  emit(e) { if (this._emitters.length < MAX_EMITTERS) this._emitters.push(e); }

  _kernelConstants() {
    const P = this.params, h = P.h;
    const poly6 = 315 / (64 * Math.PI * h ** 9), spiky = -45 / (Math.PI * h ** 6);
    // lattice-normalised volume: a cubic lattice at the rest spacing has density exactly 1
    let sum = 0, count = 0;
    const n = Math.ceil(h / P.spacing);
    for (let x = -n; x <= n; x++) for (let y = -n; y <= n; y++) for (let z = -n; z <= n; z++) {
      const r2 = (x * x + y * y + z * z) * P.spacing * P.spacing;
      if (r2 < h * h) { sum += poly6 * (h * h - r2) ** 3; count++; }
    }
    const dq = P.scorrDq * h;
    this.restNeighbors = count;
    // Akinci cohesion spline normalisation 32 / (pi h^9)
    return { poly6, spiky, densVol: 1 / sum, scorrW: poly6 * (h * h - dq * dq) ** 3, cohK: 32 / (Math.PI * h ** 9) };
  }

  _pack(dt) {
    const P = this.params, k = this._kc ?? (this._kc = this._kernelConstants());
    const ab = new ArrayBuffer(144), f = new Float32Array(ab), u = new Uint32Array(ab);
    u[0] = this.max; u[1] = this._activeEmitters; u[2] = this.frame; u[3] = this.sim.prims.length;
    f[4] = dt; f[5] = P.h; f[6] = P.vol; f[7] = P.eps;
    f[8] = k.poly6; f[9] = k.spiky; f[10] = P.scorrK; f[11] = k.scorrW;
    f[12] = P.gravity; f[13] = P.drag; f[14] = P.xsph; f[15] = P.cohesion;
    f[16] = P.radius; f[17] = P.maxSpeed; f[18] = P.life; f[19] = P.absorbAge;
    f[20] = P.foamProb; f[21] = P.foamSize; f[22] = P.foamLife; f[23] = P.foamAmount;
    f[24] = k.densVol; u[25] = SPLASH_TABLE - 1; u[26] = MAX_FOAM_REQ; f[27] = k.cohK;
    f[28] = P.wind[0]; f[29] = P.wind[1]; f[30] = this._frameDt ?? dt; f[31] = P.maxCorr * P.spacing;
    f[32] = this.restNeighbors;
    this.device.queue.writeBuffer(this.uniform, 0, ab);
  }

  _packEmitters(frameDt) {
    const P = this.params, n = this._emitters.length;
    let active = 0, maxCount = 0;
    if (n) {
      const data = new Float32Array(n * 16);
      for (const e of this._emitters) {
        const vol = e.volume ?? (e.rate ?? 0) * frameDt;
        const x = vol / P.vol;
        let count = Math.floor(x) + (Math.random() < x % 1 ? 1 : 0);
        count = Math.min(count, 4096, this.max);
        if (count <= 0) continue;
        const o = active * 16, p = e.position, v = e.velocity ?? [0, 0, 0];
        const r = Array.isArray(e.radius) ? e.radius : [e.radius ?? 0.1, e.radius ?? 0.1];
        data.set([p[0], p[1], p[2], count, v[0], v[1], v[2], e.spread ?? 0.5, r[0], r[1], (e.fromWater ?? true) ? 1 : 0, e.mud ?? -1,
          this.head, e.life ?? P.life, e.ring ? 1 : 0, e.radialSpeed ?? 0], o);
        this.head = (this.head + count) % this.max;
        maxCount = Math.max(maxCount, count);
        active++;
      }
      if (active) this.device.queue.writeBuffer(this.emitterBuf, 0, data, 0, active * 16);
    }
    this._emitters.length = 0;
    this._activeEmitters = active;
    this._maxCount = maxCount;
    if (active) this.lastEmit = this.time;
  }

  /** Record this frame's splash simulation (after FluidSim.step, which consumes the deposits next frame). */
  step(encoder, frameDt) {
    this.time += frameDt;
    this._packEmitters(frameDt);
    if (!this.active) return;
    const P = this.params;
    const subs = Math.max(1, Math.ceil(frameDt / P.substepDt - 1e-6));
    const dt = frameDt / subs;
    this._frameDt = frameDt;
    this._pack(dt);
    const N = Math.ceil(this.max / 64), T = SPLASH_TABLE / 256;
    const pass = encoder.beginComputePass({ label: 'splash' });
    const run = (pipe, bg, x, y = 1) => { pass.setPipeline(pipe); pass.setBindGroup(0, bg); pass.dispatchWorkgroups(x, y); };
    if (this._activeEmitters) run(this.p.emit, this.bg.emit, Math.ceil(this._maxCount / 64), this._activeEmitters);
    for (let s = 0; s < subs; s++) {
      run(this.p.clear, this.bg.clear, T);
      run(this.p.predict, this.bg.predict, N);
      run(this.p.scanBlocks, this.bg.scanBlocks, T);
      run(this.p.scanSums, this.bg.scanSums, 1);
      run(this.p.scanAdd, this.bg.scanAdd, T);
      run(this.p.scatter, this.bg.scatter, N);
      for (let it = 0; it < P.iterations; it += 2) {
        run(this.p.lambda, this.bg.lambdaA, N);
        run(this.p.delta, this.bg.deltaAB, N);
        run(this.p.lambda, this.bg.lambdaB, N);
        run(this.p.delta, this.bg.deltaBA, N);
      }
      run(this.p.velocity, this.bg.velocity, N);
      run(this.p.finish, this.bg.finish, N);
    }
    run(this.p.classify, this.bg.classify, N);
    run(this.p.drawList, this.bg.drawList, N);
    run(this.p.foam, this.bg.foam, Math.ceil(MAX_FOAM_REQ / 64));
    run(this.p.finalize, this.bg.finalize, 1);
    pass.end();
    this.frame++;
  }

  /** Liquid/spray classification for rendering (updated once per frame). */
  get renderInfo() { return { buffer: this.render[0], smoothedDensity: true }; }

  /** Snapshot of the render classification (async, for tuning/visualisation: which parcels are liquid). */
  async readRenderInfo() {
    const N = this.max;
    const rb = this.device.createBuffer({ size: N * 8, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    const enc = this.device.createCommandEncoder();
    enc.copyBufferToBuffer(this.render[0], 0, rb, 0, N * 8);
    this.device.queue.submit([enc.finish()]);
    await rb.mapAsync(GPUMapMode.READ);
    const f = new Float32Array(rb.getMappedRange().slice(0));
    rb.unmap(); rb.destroy();
    let liquid = 0, total = 0, sum = 0, mx = 0;
    const hist = [0, 0, 0, 0, 0];
    for (let i = 0; i < N; i++) {
      if (f[i * 2 + 1] <= 0) continue;
      total++;
      if (f[i * 2] > 0.5) liquid++;
      sum += f[i * 2 + 1]; mx = Math.max(mx, f[i * 2 + 1]);
      hist[Math.min(4, Math.floor(f[i * 2 + 1] / 0.4))]++;
    }
    return { total, liquid, meanDens: sum / Math.max(total, 1), maxDens: mx, hist };
  }

  /** Read the live particle count (async, for tools/tests). */
  async readAlive() {
    const rb = this.device.createBuffer({ size: 32, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    const enc = this.device.createCommandEncoder();
    enc.copyBufferToBuffer(this.drawArgs, 0, rb, 0, 32);
    this.device.queue.submit([enc.finish()]);
    await rb.mapAsync(GPUMapMode.READ);
    const n = new Uint32Array(rb.getMappedRange())[4];
    rb.unmap(); rb.destroy();
    this.stats.alive = n;
    return n;
  }

  destroy() {
    for (const b of [this.uniform, this.pos, this.vel, ...this.pred, ...this.render, this.lambda, this.cellRank, this.cellCount, this.cellStart,
      this.blockSums, this.sorted, this.draw, this.drawArgs, this.foamReq, this.emitterBuf]) b.destroy();
  }
}

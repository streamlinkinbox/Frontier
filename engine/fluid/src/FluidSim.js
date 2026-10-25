// FluidSim – GPU shallow-water simulation with mud rheology, object coupling and particle exchange.
//
// Engine-agnostic: give it a GPUDevice, call step(encoder, dt) once per frame, and render with
// FluidRenderer (or sample its textures from your own shaders).
import * as K from './shaders/sim.wgsl.js';
import { quat } from './math.js';
import { FluidParticles } from './FluidParticles.js';

const BOUNDARY_TYPES = { wall: 0, open: 1, ocean: 2 };
const PRIM_TYPES = { box: 0, sphere: 1, cylinder: 2 };
const SIM_WORDS = 140;

export class FluidSim {
  /**
   * @param {GPUDevice} device
   * @param {object} o
   * @param {number} o.nx           grid cells along X
   * @param {number} o.ny           grid cells along Z
   * @param {number} o.cellSize     metres per cell
   * @param {number[]} o.origin     world [x, z] of the domain's min corner
   */
  constructor(device, o = {}) {
    this.device = device;
    this.nx = o.nx ?? 256;
    this.ny = o.ny ?? 256;
    this.dx = o.cellSize ?? 0.5;
    this.origin = o.origin ?? [-(this.nx * this.dx) / 2, -(this.ny * this.dx) / 2];
    this.size = [this.nx * this.dx, this.ny * this.dx];
    this.maxPrims = o.maxPrimitives ?? 128;
    this.maxQueries = o.maxQueries ?? 4096;
    this.time = 0;
    this.frame = 0;
    this.lastSubsteps = 0;
    this.lastDt = 0;

    this.params = {
      gravity: 9.81,
      manning: 0.022,          // bed roughness (s/m^1/3): 0.012 asphalt .. 0.035 rough natural channel
      viscosity: 0.015,        // eddy viscosity m^2/s
      epsDry: 1e-4,            // depth below which a donor cell cannot give water
      epsRender: 6e-4,         // thinner water is rendered as wet ground only
      kObjMax: 60,             // max stiffness for bodies piercing the surface
      objCouple: 5,            // no-slip drag rate (1/s) of water under moving bodies
      // Bingham mud (O'Brien & Julien 1988, "Aspen Pit 1" coefficients, SI units)
      mudYieldA: 0.0181, mudYieldB: 25.7, mudViscA: 0.0036, mudViscB: 22.1,
      mudCvMax: 0.39,          // volumetric sediment concentration of "pure" mud (c = 1)
      sedimentDensity: 2650,
      mudManning: 0.05,
      mudDiffusion: 0.003,
      seaLevel: 0,
      spongeWidth: 8,          // metres
      spongeRate: 8,           // 1/s at the very edge
      rain: 0,                 // m/s added uniformly (1 mm/h = 2.78e-7 m/s)
      wind: [0, 0],            // m/s
      windGust: 0,             // gust pressure head (m) – Phillips/Proudman forcing
      infiltration: 0.0006,    // m/s soak rate of thin films
      filmDepth: 0.003,        // films thinner than this soak in
      dryTime: 45,             // seconds for wet ground to dry
      cfl: 0.4,
      maxSubsteps: 16,
      maxFrameDt: 1 / 30,
      maxDepth: 2,             // used for the gravity-wave CFL estimate (auto-set by setWater)
      waveRampTime: 6,
    };
    this.boundary = [0, 0, 0, 0]; // W, E, S, N
    this.waves = [];
    this.waveDepth = 5;
    this.sources = new Map();
    this._nextSourceId = 1;
    this.prims = [];
    this._queryCount = 0;
    this._queryTag = null;
    this.queryResult = { data: new Float32Array(0), count: 0, frame: -1, tag: null };
    this.stats = { volume: 0, mudVolume: 0, wetCells: 0, maxDepth: 0, particles: 0, frame: -1 };
    this._statsPending = false;
    this._statsRequested = false;

    this._simData = new ArrayBuffer(SIM_WORDS * 4);
    this._f32 = new Float32Array(this._simData);
    this._i32 = new Int32Array(this._simData);
    this._u32 = new Uint32Array(this._simData);

    this._createResources();
    this._createPipelines();
    this.particles = new FluidParticles(this, o.maxParticles ?? 262144, o.maxEmitters ?? 64);
    this._createBindGroups();
  }

  // ------------------------------------------------------------------------------------------
  _buf(size, usage, label) {
    return this.device.createBuffer({ size: Math.max(16, Math.ceil(size / 4) * 4), usage, label });
  }

  _createResources() {
    const d = this.device, N = this.nx * this.ny;
    const S = GPUBufferUsage.STORAGE, CD = GPUBufferUsage.COPY_DST, CS = GPUBufferUsage.COPY_SRC;
    this.simUniform = this._buf(SIM_WORDS * 4, GPUBufferUsage.UNIFORM | CD, 'fluid.sim');
    this.bed = this._buf(N * 4, S | CD | CS, 'fluid.bed');
    this.hc = [this._buf(N * 8, S | CD | CS, 'fluid.hc0'), this._buf(N * 8, S | CD | CS, 'fluid.hc1')];
    this.vel = [this._buf(N * 8, S | CD | CS, 'fluid.vel0'), this._buf(N * 8, S | CD | CS, 'fluid.vel1')];
    this.lim = this._buf(N * 4, S, 'fluid.limit');
    this.objA = this._buf(N * 16, S | CD, 'fluid.objA');
    this.objV = this._buf(N * 8, S, 'fluid.objV');
    this.aux = this._buf(N * 16, S | CD | CS, 'fluid.aux');
    this.surf = this._buf(N * 4, S, 'fluid.surf');
    this.deposit = this._buf(N * 8, S | CD, 'fluid.deposit');
    this.primBuf = this._buf(this.maxPrims * 112, S | CD, 'fluid.prims');
    this.points = this._buf(this.maxQueries * 16, S | CD, 'fluid.queryPoints');
    this.results = this._buf(this.maxQueries * 32, S | CS, 'fluid.queryResults');
    this._statsGroups = Math.ceil(N / 256);
    this.statsPartial = this._buf(this._statsGroups * 16, S | CS, 'fluid.statsPartial');
    this._queryStaging = [0, 1, 2].map((i) => ({ buf: this._buf(this.maxQueries * 32, GPUBufferUsage.MAP_READ | CD, 'fluid.qstage' + i), state: 'free', count: 0, frame: 0, tag: null }));
    this._statsStaging = this._buf(this._statsGroups * 16 + 16, GPUBufferUsage.MAP_READ | CD, 'fluid.statsStage');

    // empty object intervals
    const empty = new Float32Array(N * 4);
    for (let k = 0; k < N; k++) { empty[k * 4] = 1e9; empty[k * 4 + 1] = -1e9; empty[k * 4 + 2] = 1; }
    d.queue.writeBuffer(this.objA, 0, empty);

    this.surfaceTexture = d.createTexture({ label: 'fluid.surface', size: [this.nx, this.ny], format: 'rgba32float', usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING });
    this.shadeTexture = d.createTexture({ label: 'fluid.shade', size: [this.nx, this.ny], format: 'rgba16float', usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING });
    this.bedTexture = d.createTexture({ label: 'fluid.bedTex', size: [this.nx, this.ny], format: 'r32float', usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
    this.surfaceView = this.surfaceTexture.createView();
    this.shadeView = this.shadeTexture.createView();
    this.bedView = this.bedTexture.createView();
  }

  _pipe(code, label, entryPoint = 'main') {
    const module = this.device.createShaderModule({ code, label });
    return this.device.createComputePipeline({ layout: 'auto', compute: { module, entryPoint }, label });
  }

  _createPipelines() {
    this.p = {
      advect: this._pipe(K.ADVECT, 'fluid.advect'),
      velocity: this._pipe(K.VELOCITY, 'fluid.velocity'),
      limit: this._pipe(K.LIMIT, 'fluid.limit'),
      height: this._pipe(K.HEIGHT, 'fluid.height'),
      deposit: this._pipe(K.DEPOSIT, 'fluid.deposit'),
      objects: this._pipe(K.OBJECTS, 'fluid.objects'),
      prepA: this._pipe(K.PREP_A, 'fluid.prepA'),
      prepB: this._pipe(K.PREP_B, 'fluid.prepB'),
      query: this._pipe(K.QUERY, 'fluid.query'),
      stats: this._pipe(K.STATS, 'fluid.stats'),
    };
  }

  bindGroup(pipeline, resources, label) {
    return this.device.createBindGroup({
      label,
      layout: pipeline.getBindGroupLayout(0),
      entries: resources.map((r, i) => ({ binding: i, resource: r instanceof GPUBuffer ? { buffer: r } : r })),
    });
  }

  _createBindGroups() {
    const { p, simUniform: U, bed, hc, vel, lim, objA, objV, aux, surf, deposit } = this;
    this.bg = {
      advect: [0, 1].map((q) => this.bindGroup(p.advect, [U, vel[0], vel[1], hc[q], bed], 'bg.advect' + q)),
      velocity: [0, 1].map((q) => this.bindGroup(p.velocity, [U, vel[1], vel[0], hc[q], bed, objA, objV], 'bg.velocity' + q)),
      limit: this.bindGroup(p.limit, [U, vel[0], lim], 'bg.limit'),
      height: [0, 1].map((q) => this.bindGroup(p.height, [U, vel[0], hc[q], hc[1 - q], bed, lim], 'bg.height' + q)),
      deposit: this.bindGroup(p.deposit, [U, hc[0], deposit], 'bg.deposit'),
      objects: this.bindGroup(p.objects, [U, this.primBuf, hc[0], objA, objV], 'bg.objects'),
      prepA: this.bindGroup(p.prepA, [U, hc[0], bed, objA, aux, surf, this.surfaceView], 'bg.prepA'),
      prepB: this.bindGroup(p.prepB, [U, vel[0], surf, this.shadeView], 'bg.prepB'),
      query: this.bindGroup(p.query, [U, hc[0], vel[0], bed, objA, aux, this.points, this.results], 'bg.query'),
      stats: this.bindGroup(p.stats, [U, hc[0], this.statsPartial], 'bg.stats'),
    };
    this.particles.createBindGroups();
  }

  // ------------------------------------------------------------------------------------------
  // World <-> grid helpers
  toLocal(x, z) { return [x - this.origin[0], z - this.origin[1]]; }
  cellIndex(x, z) {
    const i = Math.floor((x - this.origin[0]) / this.dx), j = Math.floor((z - this.origin[1]) / this.dx);
    if (i < 0 || j < 0 || i >= this.nx || j >= this.ny) return -1;
    return j * this.nx + i;
  }
  cellCenter(i, j) { return [this.origin[0] + (i + 0.5) * this.dx, this.origin[1] + (j + 0.5) * this.dx]; }

  /** Terrain / bed height per cell (row-major, j * nx + i). */
  setBed(heights) {
    if (heights.length !== this.nx * this.ny) throw new Error('setBed: wrong size');
    this.bedHeights = heights instanceof Float32Array ? heights : new Float32Array(heights);
    this.device.queue.writeBuffer(this.bed, 0, this.bedHeights);
    this.device.queue.writeTexture({ texture: this.bedTexture }, this.bedHeights, { bytesPerRow: this.nx * 4 }, [this.nx, this.ny]);
  }

  /**
   * Initial water state. depth: metres per cell; mud: 0..1 concentration; vel: optional interleaved
   * staggered (u, v) per cell.
   */
  setWater(depth, mud = null, vel = null) {
    const N = this.nx * this.ny;
    const hc = new Float32Array(N * 2);
    const aux = new Float32Array(N * 4);
    let maxD = 0.1;
    for (let k = 0; k < N; k++) {
      const h = Math.max(0, depth[k]);
      hc[k * 2] = h;
      hc[k * 2 + 1] = mud ? mud[k] : 0;
      aux[k * 4] = (this.bedHeights ? this.bedHeights[k] : 0) + h;
      aux[k * 4 + 1] = h > 0.0008 ? 1 : 0;
      aux[k * 4 + 3] = mud ? mud[k] : 0;
      if (h > maxD) maxD = h;
    }
    this.params.maxDepth = Math.max(this.params.maxDepth, maxD * 1.15);
    for (const b of this.hc) this.device.queue.writeBuffer(b, 0, hc);
    this.device.queue.writeBuffer(this.aux, 0, aux);
    const v = vel ?? new Float32Array(N * 2);
    for (const b of this.vel) this.device.queue.writeBuffer(b, 0, v);
    this.device.queue.writeBuffer(this.deposit, 0, new Int32Array(N * 2));
    this._initialDepth = hc;
  }

  /** boundary types per edge: 'wall' | 'open' (absorbing) | 'ocean' (wave-maker + absorbing) */
  setBoundaries({ west = 'wall', east = 'wall', south = 'wall', north = 'wall' } = {}) {
    this.boundary = [west, east, south, north].map((b) => BOUNDARY_TYPES[b] ?? 0);
    this._autoWaveDepth();
  }

  /**
   * Ocean swell entering through 'ocean' edges. Each wave: { amplitude (m), period (s),
   * direction (radians, 0 = +X, PI/2 = +Z), phase }. Waves are injected with the shallow-water
   * dispersion relation of the boundary depth and then propagate, shoal, refract and break
   * purely through the simulation.
   */
  setWaves(waves = [], waveDepth = null) {
    this.waves = waves.slice(0, 4);
    if (waveDepth != null) this.waveDepth = waveDepth; else this._autoWaveDepth();
  }

  _autoWaveDepth() {
    if (!this.bedHeights) return;
    let sum = 0, n = 0;
    const B = this.bedHeights, sl = this.params.seaLevel;
    const add = (k) => { const d = sl - B[k]; if (d > 0.2) { sum += d; n++; } };
    if (this.boundary[0] === 2) for (let j = 0; j < this.ny; j++) add(j * this.nx);
    if (this.boundary[1] === 2) for (let j = 0; j < this.ny; j++) add(j * this.nx + this.nx - 1);
    if (this.boundary[2] === 2) for (let i = 0; i < this.nx; i++) add(i);
    if (this.boundary[3] === 2) for (let i = 0; i < this.nx; i++) add((this.ny - 1) * this.nx + i);
    if (n) this.waveDepth = sum / n;
    this.params.maxDepth = Math.max(this.params.maxDepth, this.waveDepth * 1.3);
  }

  /** Volume source / drain. flow in m^3/s (negative = drain). Returns an id. */
  addSource(s) {
    if (this.sources.size >= 8) throw new Error('max 8 sources');
    const id = this._nextSourceId++;
    this.sources.set(id, { x: 0, z: 0, radius: 2, flow: 1, direction: [1, 0], speed: 0, mud: 0, ...s });
    return id;
  }
  updateSource(id, patch) { const s = this.sources.get(id); if (s) Object.assign(s, patch); }
  removeSource(id) { this.sources.delete(id); }

  /**
   * Rigid-body primitives that displace water this frame.
   * { type: 'box'|'sphere'|'cylinder', position, rotation (quat xyzw), halfExtents | radius,
   *   halfLength (cylinder along local X), velocity, angularVelocity }
   */
  setPrimitives(list) { this.prims = list.slice(0, this.maxPrims); }

  /** Particle emitters for this frame (see FluidParticles.emit). */
  emit(e) { this.particles.emit(e); }

  /**
   * Points to sample next step (world x,z pairs). Results arrive asynchronously (1-3 frames later)
   * in getQueryResults() with 8 floats per point:
   * [pressureHead, depth, velX, velZ, mud, bed, surface, dSurface/dt].
   * `tag` lets you match a result to the layout of points you submitted.
   */
  setQueryPoints(xz, count = xz.length / 2, tag = null) {
    count = Math.min(count, this.maxQueries);
    const data = new Float32Array(count * 4);
    for (let q = 0; q < count; q++) { data[q * 4] = xz[q * 2]; data[q * 4 + 1] = xz[q * 2 + 1]; data[q * 4 + 3] = 1; }
    if (count) this.device.queue.writeBuffer(this.points, 0, data);
    this._queryCount = count;
    this._queryTag = tag;
  }
  getQueryResults() { return this.queryResult; }

  requestStats() { this._statsRequested = true; }

  // ------------------------------------------------------------------------------------------
  _packSim(dt, frameDt) {
    const f = this._f32, i = this._i32, u = this._u32, P = this.params;
    i[0] = this.nx; i[1] = this.ny; u[2] = this.frame; u[3] = this.prims.length;
    f[4] = this.dx; f[5] = dt; f[6] = P.gravity; f[7] = this.time;
    f[8] = this.origin[0]; f[9] = this.origin[1]; f[10] = P.epsDry; f[11] = Math.min(25, 0.5 * this.dx / dt);
    f[12] = P.manning; f[13] = P.viscosity; f[14] = P.kObjMax; f[15] = P.objCouple;
    f[16] = P.mudYieldA; f[17] = P.mudYieldB; f[18] = P.mudViscA; f[19] = P.mudViscB;
    f[20] = P.mudCvMax; f[21] = P.sedimentDensity; f[22] = P.mudManning; f[23] = P.mudDiffusion;
    f[24] = P.seaLevel; f[25] = P.spongeWidth; f[26] = P.rain; f[27] = frameDt;
    f[28] = P.wind[0]; f[29] = P.wind[1]; f[30] = P.windGust; f[31] = this.waveDepth;
    f[32] = P.infiltration; f[33] = P.filmDepth; f[34] = P.dryTime;
    const r = Math.min(1, this.time / Math.max(P.waveRampTime, 1e-3));
    f[35] = r * r * (3 - 2 * r);
    i[36] = this.boundary[0]; i[37] = this.boundary[1]; i[38] = this.boundary[2]; i[39] = this.boundary[3];
    i[40] = this.waves.length; i[41] = Math.min(this.sources.size, 8); f[42] = P.epsRender; f[43] = P.spongeRate;
    const c = Math.sqrt(P.gravity * Math.max(this.waveDepth, 0.1));
    this.waves.forEach((w, n) => {
      const omega = (2 * Math.PI) / Math.max(w.period ?? 6, 0.5);
      const k = omega / c;
      const dir = w.direction ?? Math.PI / 2;
      f[44 + n * 4] = w.amplitude ?? 0.3; f[45 + n * 4] = Math.cos(dir); f[46 + n * 4] = Math.sin(dir); f[47 + n * 4] = k;
      f[60 + n * 4] = omega; f[61 + n * 4] = w.phase ?? 0; f[62 + n * 4] = 0; f[63 + n * 4] = 0;
    });
    let s = 0;
    for (const src of this.sources.values()) {
      if (s >= 8) break;
      const [lx, lz] = this.toLocal(src.x, src.z);
      const dl = Math.hypot(src.direction[0], src.direction[1]) || 1;
      const { radius, area } = this._sourceFootprint(lx, lz, src.radius);
      f[76 + s * 4] = lx; f[77 + s * 4] = lz; f[78 + s * 4] = radius; f[79 + s * 4] = src.flow / area;
      f[108 + s * 4] = src.direction[0] / dl; f[109 + s * 4] = src.direction[1] / dl; f[110 + s * 4] = src.speed; f[111 + s * 4] = src.mud;
      s++;
    }
    this.device.queue.writeBuffer(this.simUniform, 0, this._simData);
  }

  // Discrete area covered by a circular source (cells whose centres fall inside, as in the shader)
  _sourceFootprint(lx, lz, radius) {
    let r = Math.max(radius, this.dx * 0.75);
    const key = `${lx.toFixed(3)},${lz.toFixed(3)},${r}`;
    const hit = this._fpCaches?.get(key);
    if (hit) return hit;
    let n = 0;
    const i0 = Math.floor((lx - r) / this.dx), i1 = Math.ceil((lx + r) / this.dx);
    const j0 = Math.floor((lz - r) / this.dx), j1 = Math.ceil((lz + r) / this.dx);
    for (let j = Math.max(0, j0); j <= Math.min(this.ny - 1, j1); j++) {
      for (let i = Math.max(0, i0); i <= Math.min(this.nx - 1, i1); i++) {
        if (Math.hypot((i + 0.5) * this.dx - lx, (j + 0.5) * this.dx - lz) < r) n++;
      }
    }
    const v = { radius: r, area: Math.max(n, 1) * this.dx * this.dx };
    (this._fpCaches ??= new Map()).set(key, v);
    return this._fpCaches.size > 64 ? (this._fpCaches.clear(), v) : v;
  }

  _packPrims() {
    const n = this.prims.length;
    if (!n) return;
    const data = new Float32Array(n * 28);
    this.prims.forEach((pr, q) => {
      const o = q * 28;
      const pos = pr.position, rot = pr.rotation ?? [0, 0, 0, 1];
      const m = quat.toMat3(rot); // columns = local axes in world
      for (let r = 0; r < 3; r++) {
        const ax = m[r];
        data[o + r * 4] = ax[0]; data[o + r * 4 + 1] = ax[1]; data[o + r * 4 + 2] = ax[2];
        data[o + r * 4 + 3] = -(ax[0] * pos[0] + ax[1] * pos[1] + ax[2] * pos[2]);
      }
      const type = PRIM_TYPES[pr.type] ?? 0;
      let br;
      if (type === 0) {
        const e = pr.halfExtents; data[o + 12] = e[0]; data[o + 13] = e[1]; data[o + 14] = e[2]; br = Math.hypot(e[0], e[1], e[2]);
      } else if (type === 1) {
        data[o + 12] = pr.radius; br = pr.radius;
      } else {
        data[o + 12] = pr.radius; data[o + 13] = pr.halfLength; br = Math.hypot(pr.radius, pr.halfLength);
      }
      data[o + 15] = type;
      const v = pr.velocity ?? [0, 0, 0], w = pr.angularVelocity ?? [0, 0, 0];
      data[o + 16] = v[0]; data[o + 17] = v[1]; data[o + 18] = v[2]; data[o + 19] = br + this.dx;
      data[o + 20] = w[0]; data[o + 21] = w[1]; data[o + 22] = w[2];
      data[o + 24] = pos[0]; data[o + 25] = pos[1]; data[o + 26] = pos[2];
    });
    this.device.queue.writeBuffer(this.primBuf, 0, data);
  }

  /**
   * Advance the simulation by frameDt seconds (sub-stepped to respect the CFL limit) and record
   * all GPU work into `encoder`.
   */
  step(encoder, frameDt) {
    const P = this.params;
    frameDt = Math.min(Math.max(frameDt, 1e-4), P.maxFrameDt);
    const c = Math.sqrt(P.gravity * Math.max(P.maxDepth, 0.05)) + 2.5;
    const dtMax = (P.cfl * this.dx) / c;
    let n = Math.ceil(frameDt / dtMax);
    n += n & 1;
    n = Math.min(Math.max(n, 2), P.maxSubsteps & ~1);
    const dt = Math.min(frameDt / n, dtMax);
    const simDt = dt * n; // may be < frameDt if we hit maxSubsteps (graceful slow-motion)
    this.time += simDt;
    this.frame++;
    this.lastSubsteps = n;
    this.lastDt = dt;
    this._packSim(dt, simDt);
    this._packPrims();
    this.particles.pack(simDt);

    const gx = Math.ceil(this.nx / 8), gy = Math.ceil(this.ny / 8);
    const pass = encoder.beginComputePass({ label: 'fluid.step' });
    const run = (pipe, bg, x, y = 1) => { pass.setPipeline(pipe); pass.setBindGroup(0, bg); pass.dispatchWorkgroups(x, y); };

    run(this.p.objects, this.bg.objects, gx, gy);
    run(this.p.deposit, this.bg.deposit, gx, gy);
    let q = 0;
    for (let s = 0; s < n; s++) {
      run(this.p.advect, this.bg.advect[q], gx, gy);
      run(this.p.velocity, this.bg.velocity[q], gx, gy);
      run(this.p.limit, this.bg.limit, gx, gy);
      run(this.p.height, this.bg.height[q], gx, gy);
      q ^= 1;
    }
    this.particles.record(pass, gx, gy);
    run(this.p.prepA, this.bg.prepA, gx, gy);
    run(this.p.prepB, this.bg.prepB, gx, gy);
    const doQuery = this._queryCount > 0 && this._queryStaging.some((s) => s.state === 'free');
    if (doQuery) run(this.p.query, this.bg.query, Math.ceil(this._queryCount / 64));
    const doStats = this._statsRequested && !this._statsPending;
    if (doStats) run(this.p.stats, this.bg.stats, this._statsGroups);
    pass.end();

    this._pendingQuery = null;
    if (doQuery) {
      const st = this._queryStaging.find((s) => s.state === 'free');
      st.state = 'copy'; st.count = this._queryCount; st.frame = this.frame; st.tag = this._queryTag;
      encoder.copyBufferToBuffer(this.results, 0, st.buf, 0, this._queryCount * 32);
      this._pendingQuery = st;
    }
    this._pendingStats = false;
    if (doStats) {
      encoder.copyBufferToBuffer(this.statsPartial, 0, this._statsStaging, 0, this._statsGroups * 16);
      encoder.copyBufferToBuffer(this.particles.counters, 0, this._statsStaging, this._statsGroups * 16, 16);
      this._statsRequested = false;
      this._statsPending = true;
      this._pendingStats = true;
    }
  }

  /** Call right after queue.submit() of the encoder passed to step(): starts async readbacks. */
  afterSubmit() {
    const st = this._pendingQuery;
    if (st) {
      st.state = 'mapping';
      st.buf.mapAsync(GPUMapMode.READ, 0, st.count * 32).then(() => {
        const data = new Float32Array(st.buf.getMappedRange(0, st.count * 32).slice(0));
        st.buf.unmap();
        if (st.frame > this.queryResult.frame) this.queryResult = { data, count: st.count, frame: st.frame, tag: st.tag };
        st.state = 'free';
      }).catch(() => { st.state = 'free'; });
      this._pendingQuery = null;
    }
    if (this._pendingStats) {
      this._pendingStats = false;
      const frame = this.frame;
      const bytes = this._statsGroups * 16 + 16;
      this._statsStaging.mapAsync(GPUMapMode.READ, 0, bytes).then(() => {
        const ab = this._statsStaging.getMappedRange(0, bytes);
        const f = new Float32Array(ab, 0, this._statsGroups * 4);
        const ctr = new Int32Array(ab, this._statsGroups * 16, 4);
        let vol = 0, mud = 0, wet = 0, mx = 0;
        for (let g = 0; g < this._statsGroups; g++) { vol += f[g * 4]; mud += f[g * 4 + 1]; wet += f[g * 4 + 2]; mx = Math.max(mx, f[g * 4 + 3]); }
        const a = this.dx * this.dx;
        this.stats = { volume: vol * a, mudVolume: mud * a, wetCells: wet, maxDepth: mx, particles: ctr[2], free: ctr[0], frame };
        this._statsStaging.unmap();
        this._statsPending = false;
        // keep the CFL estimate honest
        if (mx > 0) this.params.maxDepth = Math.max(mx * 1.1, Math.min(this.params.maxDepth, mx * 1.6));
      }).catch(() => { this._statsPending = false; });
    }
  }

  /** Debug: read the full depth/mud field back to the CPU (slow; tests & tools only). */
  async readState() {
    const N = this.nx * this.ny;
    const st = this.device.createBuffer({ size: N * 8, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
    const sv = this.device.createBuffer({ size: N * 8, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
    const enc = this.device.createCommandEncoder();
    enc.copyBufferToBuffer(this.hc[0], 0, st, 0, N * 8);
    enc.copyBufferToBuffer(this.vel[0], 0, sv, 0, N * 8);
    this.device.queue.submit([enc.finish()]);
    await Promise.all([st.mapAsync(GPUMapMode.READ), sv.mapAsync(GPUMapMode.READ)]);
    const hc = new Float32Array(st.getMappedRange().slice(0));
    const vel = new Float32Array(sv.getMappedRange().slice(0));
    st.unmap(); sv.unmap(); st.destroy(); sv.destroy();
    return { hc, vel };
  }

  destroy() {
    for (const k of Object.keys(this)) { const v = this[k]; if (v && typeof v.destroy === 'function' && v !== this.device) try { v.destroy(); } catch { /* */ } }
    this.particles.destroy();
  }
}

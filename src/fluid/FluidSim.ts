// Frontier Fluid — WebGPU shallow-water fluid simulation.
//
// FluidSim owns every GPU resource for the solver and particle systems and
// exposes a small engine-facing API:
//
//   sim.setBed(heights, flowX, flowZ, channelMask)  // terrain + river data
//   sim.step(encoder, dt, interactors, sources, sunDir) // one frame of physics
//   sim.sampleWater(x, z)                            // CPU query (buoyancy etc.)
//   sim.fieldView / foamView / causticsView          // for the renderer
//
// It is deliberately renderer-agnostic: any WebGPU engine can create a
// FluidSim, feed it interactors from its physics/gameplay objects, and draw
// the resulting surface with the provided WaterRenderer (or its own shader
// sampling the same textures).

import commonWGSL from './shaders/common.wgsl?raw';
import sweWGSL from './shaders/swe.wgsl?raw';
import foamWGSL from './shaders/foamField.wgsl?raw';
import particlesWGSL from './shaders/particles.wgsl?raw';
import causticsWGSL from './shaders/caustics.wgsl?raw';
import causticsResolveWGSL from './shaders/causticsResolve.wgsl?raw';
import downsampleWGSL from './shaders/downsample.wgsl?raw';
import type { FluidSettings, FluidInteractor, FluidSource, WaterSample } from './types';

const MAX_INTERACTORS = 64;
const MAX_SOURCES = 32;
const LANDING_MAX = 8192;
export const READBACK_RES = 128;
const PARAMS_BYTES = 160;

function makeModule(device: GPUDevice, src: string, label: string): GPUShaderModule {
  const code = src.includes('//@@COMMON@@') ? src.replace('//@@COMMON@@', commonWGSL) : src;
  const mod = device.createShaderModule({ code, label });
  mod.getCompilationInfo().then((info) => {
    for (const m of info.messages) {
      if (m.type === 'error') console.error(`[fluid] shader ${label} (${m.lineNum}:${m.linePos}) ${m.message}`);
    }
  }).catch(() => undefined);
  return mod;
}

/** float32 -> float16 bits (for rgba16float uploads). */
const _f32 = new Float32Array(1);
const _u32 = new Uint32Array(_f32.buffer);
export function toHalf(v: number): number {
  _f32[0] = v;
  const f = _u32[0];
  const sign = (f >> 16) & 0x8000;
  let exp = ((f >> 23) & 0xff) - 112;
  let frac = f & 0x7fffff;
  if (exp <= 0) {
    if (exp < -10) return sign;
    frac = (frac | 0x800000) >> (1 - exp);
    if (frac & 0x1000) frac += 0x2000;
    return sign | (frac >> 13);
  }
  if (exp >= 31) return sign | 0x7c00 | (frac ? 0x200 : 0);
  if (frac & 0x1000) {
    frac += 0x2000;
    if (frac & 0x800000) { frac = 0; exp++; }
  }
  return sign | (exp << 10) | (frac >> 13);
}

interface Staging { buf: GPUBuffer; state: 'idle' | 'pending' | 'mapping' }

export class FluidSim {
  readonly device: GPUDevice;
  settings: FluidSettings;

  /** 0 = solid walls (rivers/puddles), 1 = ocean sponge + swell. */
  boundaryMode: 0 | 1 = 0;
  edgeWaveAmp = 0.6;
  edgeWaveFreq = 0.11;
  causticsEnabled = true;
  time = 0;
  frame = 0;

  // GPU resources (created in build())
  private fieldTex: GPUTexture[] = [];
  private foamTex: GPUTexture[] = [];
  private bedTex!: GPUTexture;
  private causticsTex!: GPUTexture;
  private cur = 0;      // index of current field texture
  private curFoam = 0;
  private paramsBuf!: GPUBuffer;
  private interactorBuf!: GPUBuffer;
  private sourceBuf!: GPUBuffer;
  private landingsBuf!: GPUBuffer;
  private landingMetaBuf!: GPUBuffer;
  private countersBuf!: GPUBuffer;
  splashBuf!: GPUBuffer;
  foamParticleBuf!: GPUBuffer;
  private causticAccumBuf!: GPUBuffer;
  private readbackBuf!: GPUBuffer;
  private sampler!: GPUSampler;

  private simLayout!: GPUBindGroupLayout;
  private particlesLayout!: GPUBindGroupLayout;
  private causticsLayout!: GPUBindGroupLayout;
  private causticsResolveLayout!: GPUBindGroupLayout;
  private downLayout!: GPUBindGroupLayout;
  private pipe: Record<string, GPUComputePipeline> = {};
  private simBGCache = new Map<string, GPUBindGroup>();
  private particlesBGCache = new Map<string, GPUBindGroup>();

  private staging: Staging[] = [];
  readonly cpuField = new Float32Array(READBACK_RES * READBACK_RES * 4);
  private haveCpuField = false;
  private accumulator = 0;
  /** Bed heights kept on CPU for dry-ground queries. */
  private bedCPU: Float32Array | null = null;

  constructor(device: GPUDevice, settings: FluidSettings) {
    this.device = device;
    this.settings = { ...settings };
    this.buildLayoutsAndPipelines();
    this.buildResources();
  }

  get res(): number { return this.settings.resolution; }
  get dx(): number { return this.settings.worldSize / this.settings.resolution; }

  // ------------------------------------------------------------- construction
  private buildLayoutsAndPipelines(): void {
    const d = this.device;
    const C = GPUShaderStage.COMPUTE;
    this.simLayout = d.createBindGroupLayout({
      label: 'fluid-sim',
      entries: [
        { binding: 0, visibility: C, buffer: { type: 'uniform' } },
        { binding: 1, visibility: C, texture: { sampleType: 'float' } },
        { binding: 2, visibility: C, storageTexture: { access: 'write-only', format: 'rgba16float' } },
        { binding: 3, visibility: C, texture: { sampleType: 'float' } },
        { binding: 4, visibility: C, buffer: { type: 'read-only-storage' } },
        { binding: 5, visibility: C, buffer: { type: 'read-only-storage' } },
        { binding: 6, visibility: C, buffer: { type: 'read-only-storage' } },
        { binding: 7, visibility: C, buffer: { type: 'storage' } },
        { binding: 8, visibility: C, sampler: { type: 'filtering' } },
        { binding: 9, visibility: C, texture: { sampleType: 'float' } },
        { binding: 10, visibility: C, storageTexture: { access: 'write-only', format: 'rgba16float' } },
      ],
    });
    this.particlesLayout = d.createBindGroupLayout({
      label: 'fluid-particles',
      entries: [
        { binding: 0, visibility: C, buffer: { type: 'uniform' } },
        { binding: 1, visibility: C, texture: { sampleType: 'float' } },
        { binding: 2, visibility: C, texture: { sampleType: 'float' } },
        { binding: 3, visibility: C, sampler: { type: 'filtering' } },
        { binding: 4, visibility: C, buffer: { type: 'storage' } },
        { binding: 5, visibility: C, buffer: { type: 'storage' } },
        { binding: 6, visibility: C, buffer: { type: 'storage' } },
        { binding: 7, visibility: C, buffer: { type: 'storage' } },
        { binding: 8, visibility: C, texture: { sampleType: 'float' } },
        { binding: 9, visibility: C, buffer: { type: 'storage' } },
      ],
    });
    this.causticsLayout = d.createBindGroupLayout({
      label: 'fluid-caustics',
      entries: [
        { binding: 0, visibility: C, buffer: { type: 'uniform' } },
        { binding: 1, visibility: C, texture: { sampleType: 'float' } },
        { binding: 2, visibility: C, texture: { sampleType: 'float' } },
        { binding: 3, visibility: C, sampler: { type: 'filtering' } },
        { binding: 4, visibility: C, buffer: { type: 'storage' } },
      ],
    });
    this.causticsResolveLayout = d.createBindGroupLayout({
      label: 'fluid-caustics-resolve',
      entries: [
        { binding: 0, visibility: C, buffer: { type: 'uniform' } },
        { binding: 1, visibility: C, buffer: { type: 'read-only-storage' } },
        { binding: 2, visibility: C, storageTexture: { access: 'write-only', format: 'rgba16float' } },
        { binding: 3, visibility: C, texture: { sampleType: 'float' } },
      ],
    });
    this.downLayout = d.createBindGroupLayout({
      label: 'fluid-down',
      entries: [
        { binding: 0, visibility: C, buffer: { type: 'uniform' } },
        { binding: 1, visibility: C, texture: { sampleType: 'float' } },
        { binding: 2, visibility: C, texture: { sampleType: 'float' } },
        { binding: 3, visibility: C, sampler: { type: 'filtering' } },
        { binding: 4, visibility: C, buffer: { type: 'storage' } },
      ],
    });

    const L = (bgl: GPUBindGroupLayout) => d.createPipelineLayout({ bindGroupLayouts: [bgl] });
    const simPL = L(this.simLayout);
    const partPL = L(this.particlesLayout);
    const causPL = L(this.causticsLayout);
    const causResPL = L(this.causticsResolveLayout);
    const downPL = L(this.downLayout);

    const swe = makeModule(d, sweWGSL, 'swe');
    const foam = makeModule(d, foamWGSL, 'foamField');
    const part = makeModule(d, particlesWGSL, 'particles');
    const caus = makeModule(d, causticsWGSL, 'caustics');
    const causR = makeModule(d, causticsResolveWGSL, 'causticsResolve');
    const down = makeModule(d, downsampleWGSL, 'downsample');

    const mk = (name: string, layout: GPUPipelineLayout, module: GPUShaderModule, entry: string) => {
      this.pipe[name] = d.createComputePipeline({ label: name, layout, compute: { module, entryPoint: entry } });
    };
    mk('initField', simPL, swe, 'initField');
    mk('velocity', simPL, swe, 'velocityUpdate');
    mk('continuity', simPL, swe, 'continuityUpdate');
    mk('interact', simPL, swe, 'interact');
    mk('foamUpdate', simPL, foam, 'foamUpdate');
    mk('foamInit', simPL, foam, 'foamInit');
    mk('splashUpdate', partPL, part, 'splashUpdate');
    mk('splashEmit', partPL, part, 'splashEmit');
    mk('foamPartUpdate', partPL, part, 'foamParticleUpdate');
    mk('foamEmit', partPL, part, 'foamEmit');
    mk('splashInit', partPL, part, 'splashInit');
    mk('foamPoolInit', partPL, part, 'foamPoolInit');
    mk('caustics', causPL, caus, 'causticsMain');
    mk('causticsResolve', causResPL, causR, 'causticsResolve');
    mk('downsample', downPL, down, 'downsampleMain');
  }

  private buildResources(): void {
    const d = this.device;
    const res = this.res;
    const texDesc: GPUTextureDescriptor = {
      size: [res, res],
      format: 'rgba16float',
      usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING,
    };
    this.fieldTex = [d.createTexture({ ...texDesc, label: 'fieldA' }), d.createTexture({ ...texDesc, label: 'fieldB' })];
    this.foamTex = [d.createTexture({ ...texDesc, label: 'foamA' }), d.createTexture({ ...texDesc, label: 'foamB' })];
    this.bedTex = d.createTexture({
      size: [res, res], format: 'rgba16float', label: 'bedFlow',
      usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
    });
    this.causticsTex = d.createTexture({ ...texDesc, label: 'caustics' });

    const buf = (size: number, usage: number, label: string) => d.createBuffer({ size, usage, label });
    const ST = GPUBufferUsage.STORAGE;
    this.paramsBuf = buf(PARAMS_BYTES, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, 'params');
    this.interactorBuf = buf(MAX_INTERACTORS * 48, ST | GPUBufferUsage.COPY_DST, 'interactors');
    this.sourceBuf = buf(MAX_SOURCES * 32, ST | GPUBufferUsage.COPY_DST, 'sources');
    this.landingsBuf = buf(LANDING_MAX * 16, ST, 'landings');
    this.landingMetaBuf = buf(16, ST | GPUBufferUsage.COPY_DST, 'landingMeta');
    this.countersBuf = buf(16, ST | GPUBufferUsage.COPY_DST, 'counters');
    this.splashBuf = buf(this.settings.maxSplashParticles * 32, ST | GPUBufferUsage.VERTEX, 'splashPool');
    this.foamParticleBuf = buf(this.settings.maxFoamParticles * 32, ST | GPUBufferUsage.VERTEX, 'foamPool');
    this.causticAccumBuf = buf(res * res * 4, ST, 'causticAccum');
    this.readbackBuf = buf(READBACK_RES * READBACK_RES * 16, ST | GPUBufferUsage.COPY_SRC, 'readback');
    this.staging = [0, 1].map((i) => ({
      buf: buf(READBACK_RES * READBACK_RES * 16, GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST, `staging${i}`),
      state: 'idle' as const,
    }));
    this.sampler = d.createSampler({ magFilter: 'linear', minFilter: 'linear', addressModeU: 'clamp-to-edge', addressModeV: 'clamp-to-edge' });
    this.simBGCache.clear();
    this.particlesBGCache.clear();
    this.cur = 0;
    this.curFoam = 0;
  }

  /** Recreate all GPU resources (call after changing resolution/world size). */
  rebuild(newSettings?: Partial<FluidSettings>): void {
    if (newSettings) this.settings = { ...this.settings, ...newSettings };
    for (const t of [...this.fieldTex, ...this.foamTex]) t.destroy();
    this.bedTex.destroy();
    this.causticsTex.destroy();
    for (const b of [
      this.paramsBuf, this.interactorBuf, this.sourceBuf, this.landingsBuf,
      this.landingMetaBuf, this.countersBuf, this.splashBuf, this.foamParticleBuf,
      this.causticAccumBuf, this.readbackBuf,
    ]) b.destroy();
    for (const st of this.staging) st.buf.destroy();
    this.buildResources();
    // NOTE: caller must re-upload bed data (setBed) and call reset().
    this.haveCpuField = false;
    this.accumulator = 0;
  }

  // ------------------------------------------------------------------- data in
  /** Upload terrain height + river flow data. Arrays are res*res, row-major (z rows). */
  setBed(heights: Float32Array, flowX: Float32Array, flowZ: Float32Array, channel: Float32Array): void {
    const res = this.res;
    const data = new Uint16Array(res * res * 4);
    for (let i = 0; i < res * res; i++) {
      data[i * 4 + 0] = toHalf(heights[i]);
      data[i * 4 + 1] = toHalf(flowX[i]);
      data[i * 4 + 2] = toHalf(flowZ[i]);
      data[i * 4 + 3] = toHalf(channel[i]);
    }
    this.device.queue.writeTexture(
      { texture: this.bedTex },
      data,
      { bytesPerRow: res * 8, rowsPerImage: res },
      [res, res],
    );
    this.bedCPU = heights;
  }

  /** Reset water to rest level, clear particles/foam. Records into `encoder`. */
  reset(encoder: GPUCommandEncoder): void {
    this.time = 0;
    this.frame = 0;
    this.writeParams(0, 0, 0, [0, 1, 0]);
    this.device.queue.writeBuffer(this.countersBuf, 0, new Uint32Array(4));
    this.device.queue.writeBuffer(this.landingMetaBuf, 0, new Uint32Array(4));
    // initField writes into fieldOut (= the non-current texture): swap after
    // so the freshly-filled state becomes current, then foamInit reads it.
    // (separate passes: universally-safe read-after-write ordering)
    const wg = Math.ceil(this.res / 8);
    let pass = encoder.beginComputePass({ label: 'fluid-reset-field' });
    pass.setBindGroup(0, this.simBindGroup());
    pass.setPipeline(this.pipe.initField);
    pass.dispatchWorkgroups(wg, wg);
    pass.end();
    this.swapField();
    pass = encoder.beginComputePass({ label: 'fluid-reset-foam' });
    pass.setBindGroup(0, this.simBindGroup());
    pass.setPipeline(this.pipe.foamInit);
    pass.dispatchWorkgroups(wg, wg);
    pass.end();
    this.curFoam = 1 - this.curFoam;
    pass = encoder.beginComputePass({ label: 'fluid-reset-particles' });
    pass.setBindGroup(0, this.particlesBindGroup());
    pass.setPipeline(this.pipe.splashInit);
    pass.dispatchWorkgroups(Math.ceil(this.settings.maxSplashParticles / 128));
    pass.setPipeline(this.pipe.foamPoolInit);
    pass.dispatchWorkgroups(Math.ceil(this.settings.maxFoamParticles / 128));
    pass.end();
    this.haveCpuField = false;
  }

  // ---------------------------------------------------------------- bind groups
  private simBindGroup(): GPUBindGroup {
    const key = `${this.cur}|${this.curFoam}`;
    let bg = this.simBGCache.get(key);
    if (bg) return bg;
    bg = this.device.createBindGroup({
      layout: this.simLayout,
      entries: [
        { binding: 0, resource: { buffer: this.paramsBuf } },
        { binding: 1, resource: this.fieldTex[this.cur].createView() },
        { binding: 2, resource: this.fieldTex[1 - this.cur].createView() },
        { binding: 3, resource: this.bedTex.createView() },
        { binding: 4, resource: { buffer: this.interactorBuf } },
        { binding: 5, resource: { buffer: this.sourceBuf } },
        { binding: 6, resource: { buffer: this.landingsBuf } },
        { binding: 7, resource: { buffer: this.landingMetaBuf } },
        { binding: 8, resource: this.sampler },
        { binding: 9, resource: this.foamTex[this.curFoam].createView() },
        { binding: 10, resource: this.foamTex[1 - this.curFoam].createView() },
      ],
    });
    this.simBGCache.set(key, bg);
    return bg;
  }

  private particlesBindGroup(): GPUBindGroup {
    const key = `${this.cur}|${this.curFoam}`;
    let bg = this.particlesBGCache.get(key);
    if (bg) return bg;
    bg = this.device.createBindGroup({
      layout: this.particlesLayout,
      entries: [
        { binding: 0, resource: { buffer: this.paramsBuf } },
        { binding: 1, resource: this.fieldTex[this.cur].createView() },
        { binding: 2, resource: this.bedTex.createView() },
        { binding: 3, resource: this.sampler },
        { binding: 4, resource: { buffer: this.splashBuf } },
        { binding: 5, resource: { buffer: this.foamParticleBuf } },
        { binding: 6, resource: { buffer: this.landingsBuf } },
        { binding: 7, resource: { buffer: this.countersBuf } },
        { binding: 8, resource: this.foamTex[this.curFoam].createView() },
        { binding: 9, resource: { buffer: this.landingMetaBuf } },
      ],
    });
    this.particlesBGCache.set(key, bg);
    return bg;
  }

  // --------------------------------------------------------------------- step
  private writeParams(frameDt: number, nInteractors: number, nSources: number, sunDir: [number, number, number]): void {
    const s = this.settings;
    const ab = new ArrayBuffer(PARAMS_BYTES);
    const dv = new DataView(ab);
    const U = (o: number, v: number) => dv.setUint32(o, v, true);
    const F = (o: number, v: number) => dv.setFloat32(o, v, true);
    U(0, s.resolution); U(4, s.resolution); U(8, this.frame);
    U(12, nInteractors); U(16, nSources);
    U(20, s.maxSplashParticles); U(24, s.maxFoamParticles); U(28, this.boundaryMode);
    const dx = this.dx;
    F(32, s.worldSize); F(36, dx); F(40, 1 / dx); F(44, s.timeStep); F(48, frameDt);
    F(52, this.time); F(56, s.gravity); F(60, s.viscosity); F(64, s.damping);
    F(68, s.bedFriction); F(72, s.waterLevel); F(76, s.rainRate); F(80, s.evaporation);
    const foamLifeAvg = 0.5 * (s.foamLife[0] + s.foamLife[1]);
    F(84, 1.8 / Math.max(foamLifeAvg, 0.1)); // foamDecay
    F(88, this.edgeWaveAmp); F(92, this.edgeWaveFreq);
    F(96, s.splashRate); F(100, s.foamRate); F(104, s.gravity); F(108, s.ior);
    F(112, s.center[0]); F(116, s.center[1]); F(120, s.wind[0]); F(124, s.wind[1]);
    F(128, sunDir[0]); F(132, sunDir[1]); F(136, sunDir[2]);
    F(140, s.foamLife[0]); F(144, s.foamLife[1]);
    F(148, s.foamRate / 700);  // shoreFoam
    F(152, s.foamRate / 800);  // breakFoam
    F(156, 1.6);               // foamMax
    this.device.queue.writeBuffer(this.paramsBuf, 0, ab);
  }

  step(
    encoder: GPUCommandEncoder,
    rawDt: number,
    interactors: FluidInteractor[] = [],
    sources: FluidSource[] = [],
    sunDir: [number, number, number] = [0, 1, 0],
  ): void {
    const s = this.settings;
    const dt = Math.min(Math.max(rawDt, 0.0005), 1 / 20);
    this.time += dt;
    this.frame++;

    // upload interactors / sources
    const ni = Math.min(interactors.length, MAX_INTERACTORS);
    const ns = Math.min(sources.length, MAX_SOURCES);
    if (ni > 0) {
      const data = new Float32Array(ni * 12);
      interactors.slice(0, ni).forEach((it, i) => {
        data.set([it.position[0], it.position[1], it.position[2], it.radius], i * 12);
        data.set([it.velocity[0], it.velocity[1], it.velocity[2], it.strength], i * 12 + 4);
        data.set([it.kind, it.halfHeight, 0, 0], i * 12 + 8);
      });
      this.device.queue.writeBuffer(this.interactorBuf, 0, data);
    }
    if (ns > 0) {
      const data = new Float32Array(ns * 8);
      sources.slice(0, ns).forEach((so, i) => {
        data.set([so.position[0], so.position[1], so.position[2], so.radius], i * 8);
        data.set([so.flow, so.velocity[0], so.velocity[1], 0], i * 8 + 4);
      });
      this.device.queue.writeBuffer(this.sourceBuf, 0, data);
    }
    this.writeParams(dt, ni, ns, sunDir);

    const wg = Math.ceil(this.res / 8);
    // One compute pass per stage: passes are the universal-sync point every
    // WebGPU implementation honours for read-after-write hazards between
    // ping-ponged textures, so the solver stays correct on strict validators.
    const run = (pipeName: string, bg: GPUBindGroup, x: number, y = 1) => {
      const p = encoder.beginComputePass({ label: pipeName });
      p.setPipeline(this.pipe[pipeName]);
      p.setBindGroup(0, bg);
      p.dispatchWorkgroups(x, y);
      p.end();
    };
    const grid = (pipeName: string) => run(pipeName, this.simBindGroup(), wg, wg);

    // 1) interactors / sources / rain / landing feedback (consumes landings)
    grid('interact');
    this.swapField();
    // 2) shallow-water substeps — fixed dt via accumulator so wave speed is
    //    identical at 30/60/144 Hz; `substeps` is the per-frame budget cap.
    this.accumulator = Math.min(this.accumulator + dt, s.timeStep * s.substeps * 3);
    let nSub = Math.max(1, Math.round(this.accumulator / s.timeStep));
    nSub = Math.min(nSub, s.substeps * 3);
    this.accumulator -= nSub * s.timeStep;
    for (let i = 0; i < nSub; i++) {
      grid('velocity');
      this.swapField();
      grid('continuity');
      this.swapField();
    }
    // 3) advected foam field + wetness (also consumes last frame's landings)
    grid('foamUpdate');
    this.curFoam = 1 - this.curFoam;

    // Last frame's landing events have now been consumed by (1) and (3).
    // Reset the counter, then integrate splash droplets — the ones that hit
    // the surface this frame append fresh landing events which the emitters
    // below (and next frame's solver) will consume.
    encoder.clearBuffer(this.landingMetaBuf);

    // 4) particles
    const pbg = this.particlesBindGroup();
    run('splashUpdate', pbg, Math.ceil(s.maxSplashParticles / 128));
    run('foamPartUpdate', pbg, Math.ceil(s.maxFoamParticles / 128));
    const splashCandidates = Math.min(65536, Math.round(s.splashRate * 36 * Math.min(dt * 60, 2)));
    if (splashCandidates > 0) run('splashEmit', pbg, Math.ceil(splashCandidates / 128));
    const foamCandidates = Math.min(65536, Math.round(s.foamRate * 20 * Math.min(dt * 60, 2)));
    if (foamCandidates > 0) run('foamEmit', pbg, Math.ceil(foamCandidates / 128));

    // 5) caustics
    if (this.causticsEnabled) {
      encoder.clearBuffer(this.causticAccumBuf);
      const causBG = this.device.createBindGroup({
        layout: this.causticsLayout,
        entries: [
          { binding: 0, resource: { buffer: this.paramsBuf } },
          { binding: 1, resource: this.fieldTex[this.cur].createView() },
          { binding: 2, resource: this.bedTex.createView() },
          { binding: 3, resource: this.sampler },
          { binding: 4, resource: { buffer: this.causticAccumBuf } },
        ],
      });
      run('caustics', causBG, wg, wg);
      const resBG = this.device.createBindGroup({
        layout: this.causticsResolveLayout,
        entries: [
          { binding: 0, resource: { buffer: this.paramsBuf } },
          { binding: 1, resource: { buffer: this.causticAccumBuf } },
          { binding: 2, resource: this.causticsTex.createView() },
          { binding: 3, resource: this.fieldTex[this.cur].createView() },
        ],
      });
      run('causticsResolve', resBG, wg, wg);
    }

    // 6) downsample for CPU gameplay queries
    const downBG = this.device.createBindGroup({
      layout: this.downLayout,
      entries: [
        { binding: 0, resource: { buffer: this.paramsBuf } },
        { binding: 1, resource: this.fieldTex[this.cur].createView() },
        { binding: 2, resource: this.bedTex.createView() },
        { binding: 3, resource: this.sampler },
        { binding: 4, resource: { buffer: this.readbackBuf } },
      ],
    });
    const dp = encoder.beginComputePass({ label: 'fluid-downsample' });
    dp.setPipeline(this.pipe.downsample);
    dp.setBindGroup(0, downBG);
    dp.dispatchWorkgroups(READBACK_RES / 8, READBACK_RES / 8);
    dp.end();

    const st = this.staging.find((x) => x.state === 'idle');
    if (st) {
      encoder.copyBufferToBuffer(this.readbackBuf, 0, st.buf, 0, READBACK_RES * READBACK_RES * 16);
      st.state = 'pending';
    }
  }

  private swapField(): void { this.cur = 1 - this.cur; }

  /** Call after queue.submit() — starts async readback of the CPU field. */
  pollReadback(): void {
    for (const st of this.staging) {
      if (st.state !== 'pending') continue;
      st.state = 'mapping';
      st.buf.mapAsync(GPUMapMode.READ).then(() => {
        const src = new Float32Array(st.buf.getMappedRange());
        this.cpuField.set(src);
        st.buf.unmap();
        st.state = 'idle';
        this.haveCpuField = true;
      }).catch(() => { st.state = 'idle'; });
    }
  }

  // ------------------------------------------------------------------ queries
  /** Sample water at a world position (1-frame latency). For buoyancy/gameplay. */
  sampleWater(x: number, z: number): WaterSample {
    const s = this.settings;
    const out: WaterSample = { height: -Infinity, depth: 0, velocity: [0, 0], normal: [0, 1, 0] };
    if (!this.haveCpuField) return out;
    const u = (x - s.center[0]) / s.worldSize + 0.5;
    const v = (z - s.center[1]) / s.worldSize + 0.5;
    if (u < 0 || v < 0 || u > 1 || v > 1) return out;
    const R = READBACK_RES;
    const at = (i: number, j: number, ch: number) => {
      const ci = Math.max(0, Math.min(R - 1, i));
      const cj = Math.max(0, Math.min(R - 1, j));
      return this.cpuField[(cj * R + ci) * 4 + ch];
    };
    const fx = u * R - 0.5, fz = v * R - 0.5;
    const i0 = Math.floor(fx), j0 = Math.floor(fz);
    const tx = fx - i0, tz = fz - j0;
    const bilerp = (ch: number) =>
      (at(i0, j0, ch) * (1 - tx) + at(i0 + 1, j0, ch) * tx) * (1 - tz) +
      (at(i0, j0 + 1, ch) * (1 - tx) + at(i0 + 1, j0 + 1, ch) * tx) * tz;
    const eta = bilerp(0);
    out.velocity = [bilerp(1), bilerp(2)];
    out.depth = bilerp(3);
    if (out.depth > 0.004) out.height = eta;
    // normal from height gradient
    const cell = s.worldSize / R;
    const gx = (at(i0 + 1, j0, 0) - at(i0 - 1, j0, 0)) / (2 * cell);
    const gz = (at(i0, j0 + 1, 0) - at(i0, j0 - 1, 0)) / (2 * cell);
    const inv = 1 / Math.hypot(gx, 1, gz);
    out.normal = [-gx * inv, inv, -gz * inv];
    return out;
  }

  /** Bed (terrain) height at a world position, from the CPU copy of the bed. */
  sampleBed(x: number, z: number): number {
    if (!this.bedCPU) return 0;
    const s = this.settings;
    const u = (x - s.center[0]) / s.worldSize + 0.5;
    const v = (z - s.center[1]) / s.worldSize + 0.5;
    const R = this.res;
    const fx = Math.max(0, Math.min(R - 1.001, u * R - 0.5));
    const fz = Math.max(0, Math.min(R - 1.001, v * R - 0.5));
    const i0 = Math.floor(fx), j0 = Math.floor(fz);
    const tx = fx - i0, tz = fz - j0;
    const at = (i: number, j: number) => this.bedCPU![Math.max(0, Math.min(R - 1, j)) * R + Math.max(0, Math.min(R - 1, i))];
    return (at(i0, j0) * (1 - tx) + at(i0 + 1, j0) * tx) * (1 - tz) + (at(i0, j0 + 1) * (1 - tx) + at(i0 + 1, j0 + 1) * tx) * tz;
  }

  // -------------------------------------------------------------- for renderers
  get fieldView(): GPUTextureView { return this.fieldTex[this.cur].createView(); }
  get foamView(): GPUTextureView { return this.foamTex[this.curFoam].createView(); }
  get bedView(): GPUTextureView { return this.bedTex.createView(); }
  get causticsView(): GPUTextureView { return this.causticsTex.createView(); }
  get paramsBuffer(): GPUBuffer { return this.paramsBuf; }
  get linearSampler(): GPUSampler { return this.sampler; }
}

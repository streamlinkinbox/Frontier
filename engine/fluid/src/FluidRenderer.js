// FluidRenderer – draws the simulated water surface, caustics and particles.
//
// Frame integration (see README):
//   renderer.updateView({...camera & light...})
//   renderer.renderCaustics(encoder)            // before your opaque pass (ground shaders sample it)
//   ...opaque pass into sceneColor + depth...
//   copy sceneColor -> sceneColorCopy
//   renderer.renderFoam(encoder)                // splat foam particles (reads the opaque depth)
//   pass = beginRenderPass(color: sceneColor (load), depth: depthReadOnly)
//   renderer.drawWater(pass); renderer.drawFoam(pass); renderer.drawParticles(pass)
import { WATER, CAUSTICS, PARTICLES, FOAM_SPLAT, FOAM_COMPOSITE, groundWGSL } from './shaders/render.wgsl.js';
import { createFoamPatternTexture } from './FoamPattern.js';

const VIEW_FLOATS = 84;
const WATER_FLOATS = 68;

export class FluidRenderer {
  /**
   * @param {import('./FluidSim.js').FluidSim} sim
   * @param {object} o
   * @param {GPUTextureFormat} [o.colorFormat='rgba16float']  HDR scene target format
   * @param {GPUTextureFormat} [o.depthFormat='depth32float']
   * @param {number} [o.causticsResolution]
   * @param {boolean} [o.outerOcean=true]  draw an ocean ring outside the domain (incident swell)
   * @param {'greater'|'less'} [o.depthCompare='greater']  'greater' for reverse-Z
   */
  constructor(sim, o = {}) {
    this.sim = sim;
    this.device = sim.device;
    this.colorFormat = o.colorFormat ?? 'rgba16float';
    this.depthFormat = o.depthFormat ?? 'depth32float';
    this.depthCompare = o.depthCompare ?? 'greater';
    this.meshStep = o.meshStep ?? (Math.max(sim.nx, sim.ny) > 768 ? 2 : 1);
    this.causticsRes = o.causticsResolution ?? Math.min(2048, Math.max(sim.nx, sim.ny) * 2);
    this.outerOcean = o.outerOcean ?? true;
    this.outerExtent = o.outerExtent ?? 3000;
    this.settings = {
      absorb: [0.45, 0.07, 0.035],       // water absorption coefficient (1/m), red is absorbed fastest
      scatter: [0.025, 0.04, 0.05],      // scattering coefficient of clear coastal water (1/m)
      mudAbsorb: [40, 46, 52],           // added per unit mud concentration (1/m)
      mudScatter: [14, 9.5, 6],          // sediment scattering -> brown turbid water
      mudColor: [0.11, 0.075, 0.045],    // opaque mud surface albedo
      roughness: 0.035,
      mudRoughness: 0.38,
      refractScale: 1.0,
      debugMode: 0,                      // 0 shaded, 1 depth, 2 speed, 3 mud, 4 normals
      caustics: true,
      causticsIntensity: 1.0,
      sprayColor: [0.92, 0.96, 1.0],
      sprayOpacity: 0.55,
      mistOpacity: 0.16,
      shutter: 1 / 60,
      minPixel: 1.6,
      softness: 0.25,
      particles: true,
      // particle foam (FOAM_SPLAT + FOAM_COMPOSITE)
      foam: true,
      foamTile: 3.0,               // metres covered by the foam micro-structure texture
      foamGrowth: 0.9,             // a raft spreads to (1 + growth) x its radius over its life
      foamFresh: 0.6,              // s: how long breaking-wave foam stays active whitewater
      foamAmount: 1.0,             // splat weight per raft
      foamThinning: 1.6,           // thickness ~ (life left)^thinning: bubbles burst before the raft is gone
      foamFadeIn: 0.12,            // s
      foamLift: 0.03,              // m above the surface (keeps thin-film / beach foam above the depth)
      foamDensity: 0.45,           // coverage density per unit of overlapping raft weight
      foamEdge: 0.035,             // softness of the coverage threshold
      foamLace: 0.95,              // how far residual foam fills its cells before opening into lace
      foamAlbedo: [0.5, 0.85],     // reflectance of thin .. thick foam
      foamMudColor: [0.5, 0.4, 0.29],
      foamBump: 1.0,               // relief strength (bubbles ~2 cm, lumps ~8 cm)
      foamAmbient: 0.8,
      foamUnresolved: 0.06,        // pixel footprint (m) beyond which the lace is drawn as its average
      foamOpacity: 0.97,
      bubbleScatter: 1.0,          // milky aerated water under fresh foam
    };
    this._view = new Float32Array(VIEW_FLOATS);
    this._water = new Float32Array(WATER_FLOATS);
    this._createResources();
    this._createPipelines();
  }

  _createResources() {
    const d = this.device, sim = this.sim;
    const U = GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST;
    this.viewBuffer = d.createBuffer({ label: 'fluidR.view', size: VIEW_FLOATS * 4, usage: U });
    this.waterBuffer = d.createBuffer({ label: 'fluidR.water', size: WATER_FLOATS * 4, usage: U });
    this.causticBuffers = [0, 1, 2].map((c) => d.createBuffer({ label: 'fluidR.caustics' + c, size: 80, usage: U }));
    this.particleBuffer = d.createBuffer({ label: 'fluidR.particles', size: 64, usage: U });
    this.groundBuffer = d.createBuffer({ label: 'fluidR.ground', size: 32, usage: U });
    this.sampler = d.createSampler({ magFilter: 'linear', minFilter: 'linear', addressModeU: 'clamp-to-edge', addressModeV: 'clamp-to-edge' });
    this.shadowSampler = d.createSampler({ compare: 'less', magFilter: 'linear', minFilter: 'linear' });
    this.dummyShadow = d.createTexture({ size: [1, 1], format: 'depth32float', usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.RENDER_ATTACHMENT });
    this.causticsTexture = d.createTexture({ label: 'fluidR.causticsTex', size: [this.causticsRes, this.causticsRes], format: 'rgba16float', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC });
    this.causticsView = this.causticsTexture.createView();
    // foam: micro-structure carried by the foam particles + uniforms
    this.foamPattern = createFoamPatternTexture(d, 512);
    this.foamPatternView = this.foamPattern.createView();
    this.patternSampler = d.createSampler({ magFilter: 'linear', minFilter: 'linear', mipmapFilter: 'linear', addressModeU: 'repeat', addressModeV: 'repeat', maxAnisotropy: 4 });
    this.foamSplatBuffer = d.createBuffer({ label: 'fluidR.foamSplat', size: 48, usage: U });
    this.foamCompBuffer = d.createBuffer({ label: 'fluidR.foamComp', size: 64, usage: U });

    // water grid indices
    const mw = Math.ceil(sim.nx / this.meshStep) + (sim.nx % this.meshStep ? 1 : 0);
    const mh = Math.ceil(sim.ny / this.meshStep) + (sim.ny % this.meshStep ? 1 : 0);
    this.meshW = mw; this.meshH = mh;
    this.waterIndices = this._gridIndices(mw, mh);
    // caustics grid (one vertex per sim cell)
    this.cMeshW = sim.nx; this.cMeshH = sim.ny;
    this.causticIndices = this._gridIndices(sim.nx, sim.ny);
    if (this.outerOcean) this._buildOuterRing();
  }

  _gridIndices(w, h) {
    const n = (w - 1) * (h - 1) * 6;
    const idx = new Uint32Array(n);
    let o = 0;
    for (let j = 0; j < h - 1; j++) {
      for (let i = 0; i < w - 1; i++) {
        const a = j * w + i, b = a + 1, c = a + w, e = c + 1;
        // alternate the diagonal to avoid directional artefacts
        if ((i + j) & 1) { idx[o++] = a; idx[o++] = c; idx[o++] = b; idx[o++] = b; idx[o++] = c; idx[o++] = e; }
        else { idx[o++] = a; idx[o++] = c; idx[o++] = e; idx[o++] = a; idx[o++] = e; idx[o++] = b; }
      }
    }
    const buf = this.device.createBuffer({ size: idx.byteLength, usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST });
    this.device.queue.writeBuffer(buf, 0, idx);
    return { buf, count: n };
  }

  _buildOuterRing() {
    const sim = this.sim;
    const inset = sim.dx * 0.5;
    const x0 = sim.origin[0] + inset, z0 = sim.origin[1] + inset;
    const x1 = sim.origin[0] + sim.size[0] - inset, z1 = sim.origin[1] + sim.size[1] - inset;
    const per = 48; // points per side
    const rings = [];
    let dist = 0, step = sim.dx * 2;
    while (dist < this.outerExtent) {
      const pts = [];
      const ax = x0 - dist, az = z0 - dist, bx = x1 + dist, bz = z1 + dist;
      for (let s = 0; s < per; s++) pts.push([ax + (bx - ax) * s / per, az]);
      for (let s = 0; s < per; s++) pts.push([bx, az + (bz - az) * s / per]);
      for (let s = 0; s < per; s++) pts.push([bx - (bx - ax) * s / per, bz]);
      for (let s = 0; s < per; s++) pts.push([ax, bz - (bz - az) * s / per]);
      rings.push(pts);
      dist += step;
      step *= 1.18;
    }
    const M = per * 4;
    const verts = new Float32Array(rings.length * M * 2);
    rings.forEach((r, ri) => r.forEach((p, pi) => { verts[(ri * M + pi) * 2] = p[0]; verts[(ri * M + pi) * 2 + 1] = p[1]; }));
    const idx = [];
    for (let r = 0; r < rings.length - 1; r++) {
      for (let p = 0; p < M; p++) {
        const a = r * M + p, b = r * M + ((p + 1) % M), c = (r + 1) * M + p, e = (r + 1) * M + ((p + 1) % M);
        idx.push(a, c, e, a, e, b);
      }
    }
    const d = this.device;
    this.outerVB = d.createBuffer({ size: verts.byteLength, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
    d.queue.writeBuffer(this.outerVB, 0, verts);
    const ib = new Uint32Array(idx);
    this.outerIB = d.createBuffer({ size: ib.byteLength, usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST });
    d.queue.writeBuffer(this.outerIB, 0, ib);
    this.outerCount = ib.length;
  }

  _createPipelines() {
    const d = this.device;
    const VF = GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, F = GPUShaderStage.FRAGMENT, Vx = GPUShaderStage.VERTEX;
    this.waterBGL = d.createBindGroupLayout({
      label: 'fluidR.waterBGL',
      entries: [
        { binding: 0, visibility: VF, buffer: { type: 'uniform' } },
        { binding: 1, visibility: VF, buffer: { type: 'uniform' } },
        { binding: 2, visibility: VF, texture: { sampleType: 'unfilterable-float' } },
        { binding: 3, visibility: F, texture: { sampleType: 'float' } },
        { binding: 4, visibility: F, texture: { sampleType: 'float' } },
        { binding: 5, visibility: F, texture: { sampleType: 'depth' } },
        { binding: 6, visibility: F, sampler: { type: 'filtering' } },
        { binding: 7, visibility: F, texture: { sampleType: 'depth' } },
        { binding: 8, visibility: F, sampler: { type: 'comparison' } },
        { binding: 9, visibility: F, texture: { sampleType: 'unfilterable-float' } },
        { binding: 10, visibility: F, texture: { sampleType: 'unfilterable-float' } },
      ],
    });
    const wm = d.createShaderModule({ code: WATER, label: 'fluidR.water' });
    const wl = d.createPipelineLayout({ bindGroupLayouts: [this.waterBGL] });
    const depthStencil = { format: this.depthFormat, depthWriteEnabled: false, depthCompare: this.depthCompare };
    this.waterPipe = d.createRenderPipeline({
      label: 'fluidR.waterGrid', layout: wl,
      vertex: { module: wm, entryPoint: 'vsGrid' },
      fragment: { module: wm, entryPoint: 'fsWater', targets: [{ format: this.colorFormat }] },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
      depthStencil,
    });
    if (this.outerOcean) {
      this.outerPipe = d.createRenderPipeline({
        label: 'fluidR.waterOuter', layout: wl,
        vertex: { module: wm, entryPoint: 'vsOuter', buffers: [{ arrayStride: 8, attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x2' }] }] },
        fragment: { module: wm, entryPoint: 'fsWater', targets: [{ format: this.colorFormat }] },
        primitive: { topology: 'triangle-list', cullMode: 'none' },
        depthStencil,
      });
    }

    this.causticsBGL = d.createBindGroupLayout({
      label: 'fluidR.causticsBGL',
      entries: [
        { binding: 0, visibility: Vx, buffer: { type: 'uniform' } },
        { binding: 1, visibility: Vx, texture: { sampleType: 'unfilterable-float' } },
        { binding: 2, visibility: Vx, texture: { sampleType: 'float' } },
        { binding: 3, visibility: Vx, texture: { sampleType: 'unfilterable-float' } },
        { binding: 4, visibility: Vx, sampler: { type: 'filtering' } },
      ],
    });
    const cm = d.createShaderModule({ code: CAUSTICS, label: 'fluidR.caustics' });
    const cl = d.createPipelineLayout({ bindGroupLayouts: [this.causticsBGL] });
    const add = { color: { srcFactor: 'one', dstFactor: 'one', operation: 'add' }, alpha: { srcFactor: 'one', dstFactor: 'one', operation: 'add' } };
    this.causticPipes = [GPUColorWrite.RED, GPUColorWrite.GREEN, GPUColorWrite.BLUE].map((mask, c) => d.createRenderPipeline({
      label: 'fluidR.caustics' + c, layout: cl,
      vertex: { module: cm, entryPoint: 'vs' },
      fragment: { module: cm, entryPoint: 'fs', targets: [{ format: 'rgba16float', blend: add, writeMask: mask }] },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
    }));
    this.causticBGs = this.causticBuffers.map((b) => d.createBindGroup({
      layout: this.causticsBGL,
      entries: [
        { binding: 0, resource: { buffer: b } },
        { binding: 1, resource: this.sim.surfaceView },
        { binding: 2, resource: this.sim.shadeView },
        { binding: 3, resource: this.sim.bedView },
        { binding: 4, resource: this.sampler },
      ],
    }));

    this.particleBGL = d.createBindGroupLayout({
      label: 'fluidR.particleBGL',
      entries: [
        { binding: 0, visibility: VF, buffer: { type: 'uniform' } },
        { binding: 1, visibility: VF, buffer: { type: 'uniform' } },
        { binding: 2, visibility: Vx, buffer: { type: 'read-only-storage' } },
        { binding: 3, visibility: Vx, buffer: { type: 'read-only-storage' } },
        { binding: 4, visibility: F, texture: { sampleType: 'depth' } },
      ],
    });
    const pm = d.createShaderModule({ code: PARTICLES, label: 'fluidR.particles' });
    const premul = { color: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' }, alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' } };
    this.particlePipe = d.createRenderPipeline({
      label: 'fluidR.particles', layout: d.createPipelineLayout({ bindGroupLayouts: [this.particleBGL] }),
      vertex: { module: pm, entryPoint: 'vs' },
      fragment: { module: pm, entryPoint: 'fs', targets: [{ format: this.colorFormat, blend: premul }] },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
      depthStencil,
    });

    // ---- particle foam: screen-space splat (additive, two rgba16float targets) + composite
    this.foamSplatBGL = d.createBindGroupLayout({
      label: 'fluidR.foamSplatBGL',
      entries: [
        { binding: 0, visibility: VF, buffer: { type: 'uniform' } },
        { binding: 1, visibility: VF, buffer: { type: 'uniform' } },
        { binding: 2, visibility: Vx, buffer: { type: 'read-only-storage' } },
        { binding: 3, visibility: Vx, buffer: { type: 'read-only-storage' } },
        { binding: 4, visibility: Vx, texture: { sampleType: 'float' } },
        { binding: 5, visibility: Vx, sampler: { type: 'filtering' } },
        { binding: 6, visibility: F, texture: { sampleType: 'float' } },
        { binding: 7, visibility: F, sampler: { type: 'filtering' } },
      ],
    });
    const fsm = d.createShaderModule({ code: FOAM_SPLAT, label: 'fluidR.foamSplat' });
    this.foamSplatPipe = d.createRenderPipeline({
      label: 'fluidR.foamSplat', layout: d.createPipelineLayout({ bindGroupLayouts: [this.foamSplatBGL] }),
      vertex: { module: fsm, entryPoint: 'vs' },
      fragment: { module: fsm, entryPoint: 'fs', targets: [{ format: 'rgba16float', blend: add }, { format: 'rgba16float', blend: add }] },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
      depthStencil,
    });
    this.foamCompBGL = d.createBindGroupLayout({
      label: 'fluidR.foamCompBGL',
      entries: [
        { binding: 0, visibility: F, buffer: { type: 'uniform' } },
        { binding: 1, visibility: F, buffer: { type: 'uniform' } },
        { binding: 2, visibility: F, texture: { sampleType: 'unfilterable-float' } },
        { binding: 3, visibility: F, texture: { sampleType: 'unfilterable-float' } },
        { binding: 4, visibility: F, texture: { sampleType: 'float' } },
        { binding: 5, visibility: F, sampler: { type: 'filtering' } },
        { binding: 6, visibility: F, texture: { sampleType: 'depth' } },
        { binding: 7, visibility: F, sampler: { type: 'comparison' } },
      ],
    });
    const fcm = d.createShaderModule({ code: FOAM_COMPOSITE, label: 'fluidR.foamComposite' });
    this.foamCompPipe = d.createRenderPipeline({
      label: 'fluidR.foamComposite', layout: d.createPipelineLayout({ bindGroupLayouts: [this.foamCompBGL] }),
      vertex: { module: fcm, entryPoint: 'vs' },
      fragment: { module: fcm, entryPoint: 'fs', targets: [{ format: this.colorFormat, blend: premul }] },
      primitive: { topology: 'triangle-list' },
      depthStencil: { format: this.depthFormat, depthWriteEnabled: false, depthCompare: 'always' },
    });
  }

  /**
   * Bind the engine's scene textures (call again after resize).
   * @param {GPUTextureView} sceneColorCopy  copy of the opaque HDR scene (sampled for refraction)
   * @param {GPUTextureView} sceneDepth      opaque depth (bound read-only while drawing water)
   * @param {GPUTextureView} [shadowMap]     optional directional shadow map (depth)
   * @param {[number, number]} [size]        render target size in pixels (needed for particle foam)
   */
  setSceneTextures(sceneColorCopy, sceneDepth, shadowMap = null, size = null) {
    const d = this.device;
    // screen-space foam buffers (FOAM_SPLAT targets, read by the composite and the water shader)
    const fw = size ? Math.max(1, size[0] | 0) : 1, fh = size ? Math.max(1, size[1] | 0) : 1;
    if (!size && this.settings.foam) console.warn('FluidRenderer.setSceneTextures: pass the render size to enable particle foam');
    if (!this.foamTex || this.foamTex[0].width !== fw || this.foamTex[0].height !== fh) {
      this.foamTex?.forEach((t) => t.destroy());
      this.foamTex = [0, 1].map((i) => d.createTexture({ label: 'fluidR.foam' + 'AB'[i], size: [fw, fh], format: 'rgba16float', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC }));
      this.foamViews = this.foamTex.map((t) => t.createView());
    }
    this.foamEnabledBySize = !!size;
    this.sceneDepthView = sceneDepth;
    const shadow = shadowMap ?? this.dummyShadow.createView();
    this.waterBG = d.createBindGroup({
      label: 'fluidR.waterBG', layout: this.waterBGL,
      entries: [
        { binding: 0, resource: { buffer: this.viewBuffer } },
        { binding: 1, resource: { buffer: this.waterBuffer } },
        { binding: 2, resource: this.sim.surfaceView },
        { binding: 3, resource: this.sim.shadeView },
        { binding: 4, resource: sceneColorCopy },
        { binding: 5, resource: sceneDepth },
        { binding: 6, resource: this.sampler },
        { binding: 7, resource: shadow },
        { binding: 8, resource: this.shadowSampler },
        { binding: 9, resource: this.foamViews[0] },
        { binding: 10, resource: this.foamViews[1] },
      ],
    });
    this.foamSplatBG = d.createBindGroup({
      label: 'fluidR.foamSplatBG', layout: this.foamSplatBGL,
      entries: [
        { binding: 0, resource: { buffer: this.viewBuffer } },
        { binding: 1, resource: { buffer: this.foamSplatBuffer } },
        { binding: 2, resource: { buffer: this.sim.particles.particles } },
        { binding: 3, resource: { buffer: this.sim.particles.drawList } },
        { binding: 4, resource: this.sim.shadeView },
        { binding: 5, resource: this.sampler },
        { binding: 6, resource: this.foamPatternView },
        { binding: 7, resource: this.patternSampler },
      ],
    });
    this.foamCompBG = d.createBindGroup({
      label: 'fluidR.foamCompBG', layout: this.foamCompBGL,
      entries: [
        { binding: 0, resource: { buffer: this.viewBuffer } },
        { binding: 1, resource: { buffer: this.foamCompBuffer } },
        { binding: 2, resource: this.foamViews[0] },
        { binding: 3, resource: this.foamViews[1] },
        { binding: 4, resource: this.sim.shadeView },
        { binding: 5, resource: this.sampler },
        { binding: 6, resource: shadow },
        { binding: 7, resource: this.shadowSampler },
      ],
    });
    this.particleBG = d.createBindGroup({
      label: 'fluidR.particleBG', layout: this.particleBGL,
      entries: [
        { binding: 0, resource: { buffer: this.viewBuffer } },
        { binding: 1, resource: { buffer: this.particleBuffer } },
        { binding: 2, resource: { buffer: this.sim.particles.particles } },
        { binding: 3, resource: { buffer: this.sim.particles.drawList } },
        { binding: 4, resource: sceneDepth },
      ],
    });
  }

  /**
   * Camera & lighting for this frame.
   * v = { viewProj, invViewProj, camPos, near, width, height, camRight, camUp, camFwd,
   *       sunDir (towards sun), sunColor, skyZenith, skyHorizon, fogDensity, fogHeight, exposure,
   *       time, shadowMat?, shadowsOn? }
   */
  updateView(v) {
    const f = this._view;
    f.set(v.viewProj, 0);
    f.set(v.invViewProj, 16);
    if (v.shadowMat) f.set(v.shadowMat, 32);
    f.set(v.camPos, 48); f[51] = v.near;
    f.set(v.sunDir, 52); f[55] = v.time ?? this.sim.time;
    f.set(v.sunColor, 56); f[59] = v.exposure ?? 1;
    f.set(v.skyZenith, 60); f[63] = v.fogDensity ?? 0.002;
    f.set(v.skyHorizon, 64); f[67] = v.shadowsOn ? 1 : 0;
    f.set(v.camRight, 68); f[71] = v.fogHeight ?? 0.02;
    f.set(v.camUp, 72);
    f.set(v.camFwd, 76);
    f[80] = v.width; f[81] = v.height; f[82] = 1 / v.width; f[83] = 1 / v.height;
    this.device.queue.writeBuffer(this.viewBuffer, 0, f);
    this._sunDir = v.sunDir;
    this._writeMaterial();
  }

  _writeMaterial() {
    const s = this.settings, sim = this.sim, w = this._water;
    w[0] = sim.origin[0]; w[1] = sim.origin[1]; w[2] = sim.dx; w[3] = this.meshStep;
    w[4] = sim.size[0]; w[5] = sim.size[1]; w[6] = sim.nx; w[7] = sim.ny;
    w.set(s.absorb, 8); w[11] = s.refractScale;
    w.set(s.scatter, 12); w[15] = s.roughness;
    w.set(s.mudAbsorb, 16); w[19] = s.mudRoughness;
    w.set(s.mudColor, 20); w[23] = s.debugMode;
    w[24] = sim.params.seaLevel; w[25] = sim.waveDepth; w[26] = sim.waves.length;
    w[27] = sim._f32[35]; // current wave ramp
    w[28] = this.meshW; w[29] = this.meshH; w[30] = sim.params.epsRender; w[31] = s.caustics ? 1 : 0;
    w.set(s.mudScatter, 32); w[35] = s.foam && s.particles ? s.bubbleScatter : 0;
    w.fill(0, 36, 68);
    for (let n = 0; n < sim.waves.length; n++) {
      // identical packing to the solver's wave-maker
      for (let c = 0; c < 4; c++) { w[36 + n * 4 + c] = sim._f32[44 + n * 4 + c]; w[52 + n * 4 + c] = sim._f32[60 + n * 4 + c]; }
    }
    this.device.queue.writeBuffer(this.waterBuffer, 0, w);

    const sun = this._sunDir ?? [0.3, 0.8, 0.2];
    const iors = [1.329, 1.333, 1.338];
    for (let c = 0; c < 3; c++) {
      const cp = new Float32Array(20);
      cp[0] = sim.origin[0]; cp[1] = sim.origin[1]; cp[2] = sim.size[0]; cp[3] = sim.size[1];
      cp[4] = sim.nx; cp[5] = sim.ny; cp[6] = this.cMeshW; cp[7] = this.cMeshH;
      cp[8] = sun[0]; cp[9] = sun[1]; cp[10] = sun[2]; cp[11] = iors[c];
      cp[12] = s.absorb[0]; cp[13] = s.absorb[1]; cp[14] = s.absorb[2]; cp[15] = s.causticsIntensity;
      cp[16] = sim.params.epsRender;
      this.device.queue.writeBuffer(this.causticBuffers[c], 0, cp);
    }
    const pr = new Float32Array(16);
    pr.set(s.sprayColor, 0); pr[3] = s.shutter;
    pr.set(s.mudColor, 8); pr[11] = s.sprayOpacity;
    pr[12] = s.minPixel; pr[13] = s.softness; pr[15] = s.mistOpacity;
    this.device.queue.writeBuffer(this.particleBuffer, 0, pr);
    const fs = new Float32Array(12);
    fs[0] = sim.origin[0]; fs[1] = sim.origin[1]; fs[2] = sim.size[0]; fs[3] = sim.size[1];
    fs[4] = s.foamTile; fs[5] = s.foamGrowth; fs[6] = s.foamFresh; fs[7] = s.foamLift;
    fs[8] = s.foamAmount; fs[9] = s.foamFadeIn; fs[10] = s.foamThinning;
    this.device.queue.writeBuffer(this.foamSplatBuffer, 0, fs);
    const fc = new Float32Array(16);
    fc[0] = sim.origin[0]; fc[1] = sim.origin[1]; fc[2] = sim.size[0]; fc[3] = sim.size[1];
    fc[4] = s.foamDensity; fc[5] = s.foamEdge; fc[6] = s.foamAlbedo[0]; fc[7] = s.foamAlbedo[1];
    fc.set(s.foamMudColor, 8); fc[11] = s.foamBump;
    fc[12] = s.foamLace; fc[13] = s.foamAmbient; fc[14] = s.foamUnresolved; fc[15] = s.foamOpacity;
    this.device.queue.writeBuffer(this.foamCompBuffer, 0, fc);
    const g = new Float32Array([sim.origin[0], sim.origin[1], sim.size[0], sim.size[1], sim.nx, sim.ny, s.caustics ? 1 : 0, sim.params.epsRender]);
    this.device.queue.writeBuffer(this.groundBuffer, 0, g);
  }

  /** Render the caustics light map (top-down, covers the domain). */
  renderCaustics(encoder) {
    const pass = encoder.beginRenderPass({
      label: 'fluidR.causticsPass',
      colorAttachments: [{ view: this.causticsView, loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 0 } }],
    });
    if (this.settings.caustics) {
      pass.setIndexBuffer(this.causticIndices.buf, 'uint32');
      for (let c = 0; c < 3; c++) {
        pass.setPipeline(this.causticPipes[c]);
        pass.setBindGroup(0, this.causticBGs[c]);
        pass.drawIndexed(this.causticIndices.count);
      }
    }
    pass.end();
  }

  drawWater(pass) {
    if (!this.waterBG) throw new Error('FluidRenderer: call setSceneTextures() first');
    if (this.outerOcean && this.sim.boundary.includes(2)) {
      pass.setPipeline(this.outerPipe);
      pass.setBindGroup(0, this.waterBG);
      pass.setVertexBuffer(0, this.outerVB);
      pass.setIndexBuffer(this.outerIB, 'uint32');
      pass.drawIndexed(this.outerCount);
    }
    pass.setPipeline(this.waterPipe);
    pass.setBindGroup(0, this.waterBG);
    pass.setIndexBuffer(this.waterIndices.buf, 'uint32');
    pass.drawIndexed(this.waterIndices.count);
  }

  drawParticles(pass) {
    if (!this.settings.particles) return;
    pass.setPipeline(this.particlePipe);
    pass.setBindGroup(0, this.particleBG);
    pass.drawIndirect(this.sim.particles.drawArgs, 0);
  }

  get foamActive() { return this.settings.foam && this.settings.particles && this.foamEnabledBySize; }

  /**
   * Splat the foam particles into the screen-space foam buffers. Call after the opaque pass (it
   * depth-tests against the opaque depth) and before the water pass (the water shader reads the
   * buffers for the aerated-water look). Always clears, so disabling foam is clean.
   */
  renderFoam(encoder) {
    if (!this.foamViews) throw new Error('FluidRenderer: call setSceneTextures() first');
    const clear = { r: 0, g: 0, b: 0, a: 0 };
    const pass = encoder.beginRenderPass({
      label: 'fluidR.foamSplat',
      colorAttachments: this.foamViews.map((view) => ({ view, loadOp: 'clear', storeOp: 'store', clearValue: clear })),
      depthStencilAttachment: { view: this.sceneDepthView, depthReadOnly: true },
    });
    if (this.foamActive) {
      pass.setPipeline(this.foamSplatPipe);
      pass.setBindGroup(0, this.foamSplatBG);
      pass.drawIndirect(this.sim.particles.drawArgs, 0);
    }
    pass.end();
  }

  /** Composite the foam over the water (inside the water pass, after drawWater). */
  drawFoam(pass) {
    if (!this.foamActive) return;
    pass.setPipeline(this.foamCompPipe);
    pass.setBindGroup(0, this.foamCompBG);
    pass.draw(3);
  }

  // ---- integration helpers for engine ground / object shaders --------------------------------
  /** WGSL snippet declaring the bindings + fluidGround(worldPos) helper. */
  static groundWGSL(group = 1, first = 0) { return groundWGSL(group, first); }
  /** Layout entries matching groundWGSL(group, first). */
  groundLayoutEntries(first = 0, visibility = GPUShaderStage.FRAGMENT) {
    return [
      { binding: first, visibility, buffer: { type: 'uniform' } },
      { binding: first + 1, visibility, texture: { sampleType: 'unfilterable-float' } },
      { binding: first + 2, visibility, texture: { sampleType: 'float' } },
      { binding: first + 3, visibility, sampler: { type: 'filtering' } },
    ];
  }
  groundBindEntries(first = 0) {
    return [
      { binding: first, resource: { buffer: this.groundBuffer } },
      { binding: first + 1, resource: this.sim.surfaceView },
      { binding: first + 2, resource: this.causticsView },
      { binding: first + 3, resource: this.sampler },
    ];
  }
}

// FluidRenderer – draws the simulated water surface, caustics and spray/mist particles.
//
// Frame integration (see README):
//   renderer.updateView({...camera & light...})
//   renderer.renderCaustics(encoder)            // before your opaque pass (ground shaders sample it)
//   ...opaque pass into sceneColor + depth...
//   copy sceneColor -> sceneColorCopy
//   pass = beginRenderPass(color: sceneColor (load), depth: depthReadOnly)
//   renderer.drawWater(pass); renderer.drawParticles(pass)
//
// Water is drawn with a camera-adaptive mesh (adaptive: true): cells are about one simulation cell
// wide near the camera and grow with distance, like a virtualised/clipmap mesh. The regular grid
// path stays available for coarse far-field simulations.
import { WATER, CAUSTICS, PARTICLES, groundWGSL } from './shaders/render.wgsl.js';

const VIEW_FLOATS = 84;
const WATER_FLOATS = 44;

export class FluidRenderer {
  /**
   * @param {import('./FluidSim.js').FluidSim} sim
   * @param {object} o
   * @param {GPUTextureFormat} [o.colorFormat='rgba16float']  HDR scene target format
   * @param {GPUTextureFormat} [o.depthFormat='depth32float']
   * @param {number} [o.causticsResolution]
   * @param {boolean} [o.caustics=true]  render a caustics map for ground shaders
   * @param {boolean} [o.particles=true] draw this sim's spray/mist particles
   * @param {boolean} [o.adaptive=false] camera-adaptive water mesh (fine near, coarse far)
   * @param {number} [o.lodN=320]        vertices per side of the adaptive mesh
   * @param {'greater'|'less'} [o.depthCompare='greater']  'greater' for reverse-Z
   */
  constructor(sim, o = {}) {
    this.sim = sim;
    this.device = sim.device;
    this.label = o.label ?? 'fluid';
    this.colorFormat = o.colorFormat ?? 'rgba16float';
    this.depthFormat = o.depthFormat ?? 'depth32float';
    this.depthCompare = o.depthCompare ?? 'greater';
    this.meshStep = o.meshStep ?? (Math.max(sim.nx, sim.ny) > 768 ? 2 : 1);
    this.causticsRes = o.causticsResolution ?? Math.min(2048, Math.max(sim.nx, sim.ny) * 2);
    this.adaptive = o.adaptive ?? false;
    this.lodN = Math.max(16, Math.min(768, o.lodN ?? 320));
    this.drawParticlesEnabled = o.particles ?? true;
    this.causticsEnabled = o.caustics ?? true;
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
      caustics: this.causticsEnabled,
      causticsIntensity: 1.0,
      sprayColor: [0.92, 0.96, 1.0],
      sprayOpacity: 0.55,
      mistOpacity: 0.16,
      shutter: 1 / 60,
      minPixel: 1.6,
      softness: 0.25,
      particles: this.drawParticlesEnabled,
    };
    this._view = new Float32Array(VIEW_FLOATS);
    this._water = new Float32Array(WATER_FLOATS);
    this._camPos = [0, 0, 0];
    this._createResources();
    this._createPipelines();
  }

  _createResources() {
    const d = this.device, sim = this.sim;
    const U = GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST;
    this.viewBuffer = d.createBuffer({ label: this.label + '.view', size: VIEW_FLOATS * 4, usage: U });
    this.waterBuffer = d.createBuffer({ label: this.label + '.water', size: WATER_FLOATS * 4, usage: U });
    this.causticBuffers = [0, 1, 2].map((c) => d.createBuffer({ label: this.label + '.caustics' + c, size: 80, usage: U }));
    this.particleBuffer = d.createBuffer({ label: this.label + '.particles', size: 64, usage: U });
    this.groundBuffer = d.createBuffer({ label: this.label + '.ground', size: 32, usage: U });
    this.sampler = d.createSampler({ magFilter: 'linear', minFilter: 'linear', addressModeU: 'clamp-to-edge', addressModeV: 'clamp-to-edge' });
    this.shadowSampler = d.createSampler({ compare: 'less', magFilter: 'linear', minFilter: 'linear' });
    this.dummyShadow = d.createTexture({ size: [1, 1], format: 'depth32float', usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.RENDER_ATTACHMENT });
    this.causticsTexture = d.createTexture({ label: this.label + '.causticsTex', size: [this.causticsRes, this.causticsRes], format: 'rgba16float', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC });
    this.causticsView = this.causticsTexture.createView();

    // regular grid (coarse water)
    const mw = Math.ceil(sim.nx / this.meshStep) + (sim.nx % this.meshStep ? 1 : 0);
    const mh = Math.ceil(sim.ny / this.meshStep) + (sim.ny % this.meshStep ? 1 : 0);
    this.meshW = mw; this.meshH = mh;
    this.waterIndices = this._gridIndices(mw, mh);
    // camera-adaptive grid (fine water): one vertex per grid point, warped in the vertex shader
    this.adaptiveIndices = this._gridIndices(this.lodN, this.lodN);
    // caustics grid (one vertex per sim cell)
    this.cMeshW = sim.nx; this.cMeshH = sim.ny;
    this.causticIndices = this._gridIndices(sim.nx, sim.ny);
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

  _createPipelines() {
    const d = this.device;
    const VF = GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, F = GPUShaderStage.FRAGMENT, Vx = GPUShaderStage.VERTEX;
    this.waterBGL = d.createBindGroupLayout({
      label: this.label + '.waterBGL',
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
      ],
    });
    const wm = d.createShaderModule({ code: WATER, label: this.label + '.water' });
    const wl = d.createPipelineLayout({ bindGroupLayouts: [this.waterBGL] });
    const depthStencil = { format: this.depthFormat, depthWriteEnabled: false, depthCompare: this.depthCompare };
    const waterTarget = [{ format: this.colorFormat }];
    this.waterPipe = d.createRenderPipeline({
      label: this.label + '.waterGrid', layout: wl,
      vertex: { module: wm, entryPoint: 'vsGrid' },
      fragment: { module: wm, entryPoint: 'fsWater', targets: waterTarget },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
      depthStencil,
    });
    this.adaptivePipe = d.createRenderPipeline({
      label: this.label + '.waterAdaptive', layout: wl,
      vertex: { module: wm, entryPoint: 'vsAdaptive' },
      fragment: { module: wm, entryPoint: 'fsWater', targets: waterTarget },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
      depthStencil,
    });

    this.causticsBGL = d.createBindGroupLayout({
      label: this.label + '.causticsBGL',
      entries: [
        { binding: 0, visibility: Vx, buffer: { type: 'uniform' } },
        { binding: 1, visibility: Vx, texture: { sampleType: 'unfilterable-float' } },
        { binding: 2, visibility: Vx, texture: { sampleType: 'float' } },
        { binding: 3, visibility: Vx, texture: { sampleType: 'unfilterable-float' } },
        { binding: 4, visibility: Vx, sampler: { type: 'filtering' } },
      ],
    });
    const cm = d.createShaderModule({ code: CAUSTICS, label: this.label + '.caustics' });
    const cl = d.createPipelineLayout({ bindGroupLayouts: [this.causticsBGL] });
    const add = { color: { srcFactor: 'one', dstFactor: 'one', operation: 'add' }, alpha: { srcFactor: 'one', dstFactor: 'one', operation: 'add' } };
    this.causticPipes = [GPUColorWrite.RED, GPUColorWrite.GREEN, GPUColorWrite.BLUE].map((mask, c) => d.createRenderPipeline({
      label: this.label + '.caustics' + c, layout: cl,
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
      label: this.label + '.particleBGL',
      entries: [
        { binding: 0, visibility: VF, buffer: { type: 'uniform' } },
        { binding: 1, visibility: VF, buffer: { type: 'uniform' } },
        { binding: 2, visibility: Vx, buffer: { type: 'read-only-storage' } },
        { binding: 3, visibility: Vx, buffer: { type: 'read-only-storage' } },
        { binding: 4, visibility: F, texture: { sampleType: 'depth' } },
      ],
    });
    const pm = d.createShaderModule({ code: PARTICLES, label: this.label + '.particles' });
    const premul = { color: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' }, alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' } };
    this.particlePipe = d.createRenderPipeline({
      label: this.label + '.particles', layout: d.createPipelineLayout({ bindGroupLayouts: [this.particleBGL] }),
      vertex: { module: pm, entryPoint: 'vs' },
      fragment: { module: pm, entryPoint: 'fs', targets: [{ format: this.colorFormat, blend: premul }] },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
      depthStencil,
    });
  }

  /**
   * Bind the engine's scene textures (call again after resize).
   * @param {GPUTextureView} sceneColorCopy  copy of the opaque HDR scene (sampled for refraction)
   * @param {GPUTextureView} sceneDepth      opaque depth (bound read-only while drawing water)
   * @param {GPUTextureView} [shadowMap]     optional directional shadow map (depth)
   */
  setSceneTextures(sceneColorCopy, sceneDepth, shadowMap = null) {
    const d = this.device;
    this.sceneDepthView = sceneDepth;
    const shadow = shadowMap ?? this.dummyShadow.createView();
    this.waterBG = d.createBindGroup({
      label: this.label + '.waterBG', layout: this.waterBGL,
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
      ],
    });
    this.particleBG = d.createBindGroup({
      label: this.label + '.particleBG', layout: this.particleBGL,
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
    this._camPos = v.camPos;
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
    w[24] = sim.params.seaLevel; w[25] = 0; w[26] = 0; w[27] = 0;
    w[28] = this.meshW; w[29] = this.meshH; w[30] = sim.params.epsRender; w[31] = s.caustics ? 1 : 0;
    w.set(s.mudScatter, 32); w[35] = 0;
    // camera-adaptive mesh: centre clamped to the domain, half extents to the farthest edge,
    // cubic warp chosen so the centre spacing is ~one simulation cell (and never coarser than it)
    const cell = Math.max(1e-4, this.meshStep * sim.dx);
    const xmin = sim.origin[0], zmin = sim.origin[1];
    const xmax = sim.origin[0] + sim.size[0], zmax = sim.origin[1] + sim.size[1];
    const cx = Math.min(xmax, Math.max(xmin, this._camPos[0]));
    const cz = Math.min(zmax, Math.max(zmin, this._camPos[2]));
    const Lx = Math.max(cx - xmin, xmax - cx) + 0.5 * cell;
    const Lz = Math.max(cz - zmin, zmax - cz) + 0.5 * cell;
    const ax = Math.min(0.8, Math.max(0.12, (cell * (this.lodN - 1)) / (2 * Lx)));
    const az = Math.min(0.8, Math.max(0.12, (cell * (this.lodN - 1)) / (2 * Lz)));
    w[36] = cx; w[37] = cz; w[38] = Lx; w[39] = Lz;
    w[40] = ax; w[41] = az; w[42] = this.lodN - 1; w[43] = this.adaptive ? 1 : 0;
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
    pr[12] = s.minPixel; pr[13] = s.softness; pr[14] = s.mistOpacity;
    this.device.queue.writeBuffer(this.particleBuffer, 0, pr);
    const g = new Float32Array([sim.origin[0], sim.origin[1], sim.size[0], sim.size[1], sim.nx, sim.ny, s.caustics ? 1 : 0, sim.params.epsRender]);
    this.device.queue.writeBuffer(this.groundBuffer, 0, g);
  }

  /** Render the caustics light map (top-down, covers the domain). */
  renderCaustics(encoder) {
    if (!this.settings.caustics) return;
    const pass = encoder.beginRenderPass({
      label: this.label + '.causticsPass',
      colorAttachments: [{ view: this.causticsView, loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 0 } }],
    });
    pass.setIndexBuffer(this.causticIndices.buf, 'uint32');
    for (let c = 0; c < 3; c++) {
      pass.setPipeline(this.causticPipes[c]);
      pass.setBindGroup(0, this.causticBGs[c]);
      pass.drawIndexed(this.causticIndices.count);
    }
    pass.end();
  }

  drawWater(pass) {
    if (!this.waterBG) throw new Error('FluidRenderer: call setSceneTextures() first');
    pass.setBindGroup(0, this.waterBG);
    if (this.adaptive) {
      pass.setPipeline(this.adaptivePipe);
      pass.setIndexBuffer(this.adaptiveIndices.buf, 'uint32');
      pass.drawIndexed(this.adaptiveIndices.count);
    } else {
      pass.setPipeline(this.waterPipe);
      pass.setIndexBuffer(this.waterIndices.buf, 'uint32');
      pass.drawIndexed(this.waterIndices.count);
    }
  }

  drawParticles(pass) {
    if (!this.settings.particles || !this.particleBG) return;
    pass.setPipeline(this.particlePipe);
    pass.setBindGroup(0, this.particleBG);
    pass.drawIndirect(this.sim.particles.drawArgs, 0);
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

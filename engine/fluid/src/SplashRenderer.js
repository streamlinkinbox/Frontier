// SplashRenderer – draws the SplashFluid particles as one continuous, refractive body of water
// (screen-space fluid rendering, see shaders/splashRender.wgsl.js).
//
// Frame integration (after the water pass, so the splash refracts the water surface behind it):
//   splashR.render(encoder)                                  // sphere depth + thickness, smoothing
//   encoder.copyTextureToTexture(sceneColor -> sceneColorCopy)
//   pass = beginRenderPass(color: sceneColor (load), depth: sceneDepth read-only)
//   splashR.draw(pass)                                       // composite
import { SPLASH_SPHERES, SPLASH_SPRAY, SPLASH_FILTER, SPLASH_COMPOSITE } from './shaders/splashRender.wgsl.js';

export class SplashRenderer {
  /**
   * @param {import('./SplashFluid.js').SplashFluid} splash
   * @param {import('./FluidRenderer.js').FluidRenderer} fluidRenderer  (shares its camera/light block)
   * @param {{colorFormat?: GPUTextureFormat, depthFormat?: GPUTextureFormat}} [o]
   */
  constructor(splash, fluidRenderer, o = {}) {
    this.splash = splash;
    this.fr = fluidRenderer;
    this.device = splash.device;
    this.colorFormat = o.colorFormat ?? fluidRenderer.colorFormat;
    this.depthFormat = o.depthFormat ?? fluidRenderer.depthFormat;
    this.settings = {
      enabled: true,
      radius: splash.params.renderRadius,  // sphere radius (m)
      refract: 0.035,                       // screen-space refraction strength
      alphaThick: 0.05,                     // thickness (m) at which the water is opaque to the eye
      whiteness: 0.8,                       // how white broken-up water looks
      absorb: [0.45, 0.07, 0.035],          // water absorption (1/m)
      turbidity: 0.06,
      mudAbsorb: [42, 48, 54],
      mudColor: [0.11, 0.075, 0.045],
      aerDensity: 1.6,                      // liquid parcels with low smoothed density look aerated
      aerSpeed: 9,
      liquidRho: 0.3,                       // (the liquid/spray split itself is done in SPLASH_CLASSIFY)
      sprayAlpha: 0.45,
      sprayRadius: 0.9 * splash.params.renderRadius,
      shutter: 1 / 60,
      smoothPasses: 2,                      // narrow-range filter rounds (each = horizontal + vertical)
    };
    this._create();
  }

  get active() { return this.settings.enabled && this.splash.active && !!this.compositeBG; }
  /** mesh instances per parcel (subdivided spheres) */
  get subCount() { const s = Math.max(1, this.splash.params.subdivision | 0); return s * s * s; }

  _create() {
    const d = this.device;
    const VF = GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, F = GPUShaderStage.FRAGMENT, Vx = GPUShaderStage.VERTEX;
    this.uniform = d.createBuffer({ label: 'splashR.params', size: 96, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    this.dirH = d.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    this.dirV = d.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    d.queue.writeBuffer(this.dirH, 0, new Float32Array([1, 0, 0, 0]));
    d.queue.writeBuffer(this.dirV, 0, new Float32Array([0, 1, 0, 0]));

    this.sphereBGL = d.createBindGroupLayout({
      label: 'splashR.sphereBGL',
      entries: [
        { binding: 0, visibility: VF, buffer: { type: 'uniform' } },
        { binding: 1, visibility: VF, buffer: { type: 'uniform' } },
        { binding: 2, visibility: Vx, buffer: { type: 'read-only-storage' } },
        { binding: 3, visibility: Vx, buffer: { type: 'read-only-storage' } },
        { binding: 4, visibility: Vx, buffer: { type: 'read-only-storage' } },
        { binding: 5, visibility: F, texture: { sampleType: 'depth' } },
        { binding: 6, visibility: Vx, buffer: { type: 'read-only-storage' } },
      ],
    });
    const sm = d.createShaderModule({ code: SPLASH_SPHERES, label: 'splashR.spheres' });
    const sl = d.createPipelineLayout({ bindGroupLayouts: [this.sphereBGL] });
    this.depthPipe = d.createRenderPipeline({
      label: 'splashR.depth', layout: sl,
      vertex: { module: sm, entryPoint: 'vs' },
      fragment: { module: sm, entryPoint: 'fsDepth', targets: [{ format: 'r32float' }] },
      primitive: { topology: 'triangle-list' },
      depthStencil: { format: 'depth32float', depthWriteEnabled: true, depthCompare: 'greater' },
    });
    const add = { color: { srcFactor: 'one', dstFactor: 'one', operation: 'add' }, alpha: { srcFactor: 'one', dstFactor: 'one', operation: 'add' } };
    this.thickPipe = d.createRenderPipeline({
      label: 'splashR.thickness', layout: sl,
      vertex: { module: sm, entryPoint: 'vs' },
      fragment: { module: sm, entryPoint: 'fsThick', targets: [{ format: 'rgba16float', blend: add }] },
      primitive: { topology: 'triangle-list' },
    });

    const spm = d.createShaderModule({ code: SPLASH_SPRAY, label: 'splashR.spray' });
    const premulS = { color: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' }, alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' } };
    this.sprayPipe = d.createRenderPipeline({
      label: 'splashR.spray', layout: sl,
      vertex: { module: spm, entryPoint: 'vs' },
      fragment: { module: spm, entryPoint: 'fs', targets: [{ format: this.colorFormat, blend: premulS }] },
      primitive: { topology: 'triangle-list' },
      depthStencil: { format: this.depthFormat, depthWriteEnabled: false, depthCompare: fluidRendererDepthCompare(this.fr) },
    });

    this.filterBGL = d.createBindGroupLayout({
      label: 'splashR.filterBGL',
      entries: [
        { binding: 0, visibility: F, buffer: { type: 'uniform' } },
        { binding: 1, visibility: F, buffer: { type: 'uniform' } },
        { binding: 2, visibility: F, texture: { sampleType: 'unfilterable-float' } },
        { binding: 3, visibility: F, buffer: { type: 'uniform' } },
      ],
    });
    const fm = d.createShaderModule({ code: SPLASH_FILTER, label: 'splashR.filter' });
    this.filterPipe = d.createRenderPipeline({
      label: 'splashR.filter', layout: d.createPipelineLayout({ bindGroupLayouts: [this.filterBGL] }),
      vertex: { module: fm, entryPoint: 'vs' },
      fragment: { module: fm, entryPoint: 'fs', targets: [{ format: 'r32float' }] },
      primitive: { topology: 'triangle-list' },
    });

    this.compositeBGL = d.createBindGroupLayout({
      label: 'splashR.compositeBGL',
      entries: [
        { binding: 0, visibility: F, buffer: { type: 'uniform' } },
        { binding: 1, visibility: F, buffer: { type: 'uniform' } },
        { binding: 2, visibility: F, texture: { sampleType: 'unfilterable-float' } },
        { binding: 3, visibility: F, texture: { sampleType: 'unfilterable-float' } },
        { binding: 4, visibility: F, texture: { sampleType: 'float' } },
        { binding: 5, visibility: F, sampler: { type: 'filtering' } },
        { binding: 6, visibility: F, texture: { sampleType: 'depth' } },
        { binding: 7, visibility: F, sampler: { type: 'comparison' } },
        { binding: 8, visibility: F, texture: { sampleType: 'unfilterable-float' } },
      ],
    });
    const cm = d.createShaderModule({ code: SPLASH_COMPOSITE, label: 'splashR.composite' });
    const premul = { color: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' }, alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' } };
    this.compositePipe = d.createRenderPipeline({
      label: 'splashR.composite', layout: d.createPipelineLayout({ bindGroupLayouts: [this.compositeBGL] }),
      vertex: { module: cm, entryPoint: 'vs' },
      fragment: { module: cm, entryPoint: 'fs', targets: [{ format: this.colorFormat, blend: premul }] },
      primitive: { topology: 'triangle-list' },
      depthStencil: { format: this.depthFormat, depthWriteEnabled: false, depthCompare: 'always' },
    });
  }

  /**
   * @param {GPUTextureView} sceneColorCopy  HDR scene *including the water*, sampled for refraction
   * @param {GPUTextureView} sceneDepth      opaque depth
   * @param {GPUTextureView|null} shadowMap
   * @param {[number, number]} size
   */
  setSceneTextures(sceneColorCopy, sceneDepth, shadowMap, size) {
    const d = this.device, [w, h] = size.map((x) => Math.max(1, x | 0));
    for (const t of [this.eyeA, this.eyeB, this.thick, this.zbuf]) t?.destroy();
    const T = (format, label, usage = GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING) => d.createTexture({ label, size: [w, h], format, usage });
    this.eyeA = T('r32float', 'splashR.eyeA'); this.eyeB = T('r32float', 'splashR.eyeB');
    this.thick = T('rgba16float', 'splashR.thickness');
    this.zbuf = T('depth32float', 'splashR.zbuf', GPUTextureUsage.RENDER_ATTACHMENT);
    this.eyeAV = this.eyeA.createView(); this.eyeBV = this.eyeB.createView();
    this.thickV = this.thick.createView(); this.zbufV = this.zbuf.createView();
    const s = this.splash;
    this.sphereBG = d.createBindGroup({
      layout: this.sphereBGL,
      entries: [
        { binding: 0, resource: { buffer: this.fr.viewBuffer } }, { binding: 1, resource: { buffer: this.uniform } },
        { binding: 2, resource: { buffer: s.pos } }, { binding: 3, resource: { buffer: s.vel } }, { binding: 4, resource: { buffer: s.draw } },
        { binding: 5, resource: sceneDepth }, { binding: 6, resource: { buffer: s.render[0] } },
      ],
    });
    const fbg = (src, dir) => d.createBindGroup({
      layout: this.filterBGL,
      entries: [{ binding: 0, resource: { buffer: this.fr.viewBuffer } }, { binding: 1, resource: { buffer: this.uniform } }, { binding: 2, resource: src }, { binding: 3, resource: { buffer: dir } }],
    });
    this.filterH = fbg(this.eyeAV, this.dirH);   // A -> B
    this.filterV = fbg(this.eyeBV, this.dirV);   // B -> A
    this.compositeBG = d.createBindGroup({
      layout: this.compositeBGL,
      entries: [
        { binding: 0, resource: { buffer: this.fr.viewBuffer } }, { binding: 1, resource: { buffer: this.uniform } },
        { binding: 2, resource: this.eyeAV }, { binding: 3, resource: this.thickV }, { binding: 4, resource: sceneColorCopy },
        { binding: 5, resource: this.fr.sampler }, { binding: 6, resource: shadowMap ?? this.fr.dummyShadow.createView() },
        { binding: 7, resource: this.fr.shadowSampler }, { binding: 8, resource: this.eyeAV },
      ],
    });
  }

  _writeParams() {
    const s = this.settings, f = new Float32Array(24);
    f[0] = s.radius; f[1] = s.refract; f[2] = s.alphaThick; f[3] = s.whiteness;
    f.set(s.absorb, 4); f[7] = s.turbidity;
    f.set(s.mudAbsorb, 8); f[11] = s.aerDensity;
    f.set(s.mudColor, 12); f[15] = s.aerSpeed;
    f[16] = s.liquidRho; f[17] = s.sprayAlpha; f[18] = s.sprayRadius; f[19] = s.shutter;
    const sub = Math.max(1, this.splash.params.subdivision | 0);
    new Uint32Array(f.buffer, 80, 1)[0] = sub; new Uint32Array(f.buffer, 84, 1)[0] = sub * sub * sub;
    this.device.queue.writeBuffer(this.uniform, 0, f);
  }

  /** Sphere depth + thickness + depth smoothing. Call after the opaque pass (and the water pass). */
  render(encoder) {
    if (!this.active) return;
    this._writeParams();
    const args = this.splash.drawArgs;
    {
      const pass = encoder.beginRenderPass({
        label: 'splashR.depth',
        colorAttachments: [{ view: this.eyeAV, loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 0 } }],
        depthStencilAttachment: { view: this.zbufV, depthClearValue: 0, depthLoadOp: 'clear', depthStoreOp: 'discard' },
      });
      pass.setPipeline(this.depthPipe);
      pass.setBindGroup(0, this.sphereBG);
      pass.drawIndirect(args, 0, 0, this.subCount);
      pass.end();
    }
    {
      const pass = encoder.beginRenderPass({ label: 'splashR.thickness', colorAttachments: [{ view: this.thickV, loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 0 } }] });
      pass.setPipeline(this.thickPipe);
      pass.setBindGroup(0, this.sphereBG);
      pass.drawIndirect(args, 0, 0, this.subCount);
      pass.end();
    }
    for (let k = 0; k < this.settings.smoothPasses; k++) {
      for (const [target, bg] of [[this.eyeBV, this.filterH], [this.eyeAV, this.filterV]]) {
        const pass = encoder.beginRenderPass({ label: 'splashR.smooth', colorAttachments: [{ view: target, loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 0 } }] });
        pass.setPipeline(this.filterPipe);
        pass.setBindGroup(0, bg);
        pass.draw(3);
        pass.end();
      }
    }
  }

  /** Composite the splash water over the scene and draw its spray (inside a pass on the HDR target
   *  with the scene depth attached read-only). */
  draw(pass) {
    if (!this.active) return;
    pass.setPipeline(this.compositePipe);
    pass.setBindGroup(0, this.compositeBG);
    pass.draw(3);
    pass.setPipeline(this.sprayPipe);
    pass.setBindGroup(0, this.sphereBG);
    pass.drawIndirect(this.splash.drawArgs, 0);
  }
}

function fluidRendererDepthCompare(fr) { return fr.depthCompare ?? 'greater'; }

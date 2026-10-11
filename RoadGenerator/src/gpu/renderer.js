// WebGPU deferred renderer: g-buffer → deferred decals → SSAO → PBR lighting (2-cascade shadows) → tonemap/FXAA → overlay lines.
import { GBUFFER, SHADOW, DECAL, SSAO, BLUR, LIGHTING, POST, LINES, MIP } from "./shaders.js";
import { mat4, norm, sub, add, mul, cross, dot, madd } from "../core/math.js";

const DRAW_STRIDE = 256; // bytes per draw uniform slot
const SHADOW_SIZE = 2048;
export const FLAG_INSTANCED = 1, FLAG_PAINT = 2, FLAG_EMISSIVE = 4, FLAG_NODECAL = 8, FLAG_VARIED = 16;

export class Renderer {
  static async create(canvas, opts = {}) {
    if (!navigator.gpu) throw new Error("WebGPU is not available in this browser.");
    const adapter = await navigator.gpu.requestAdapter({ powerPreference: "high-performance" });
    if (!adapter) throw new Error("No WebGPU adapter found.");
    const device = await adapter.requestDevice({ requiredLimits: { maxStorageBufferBindingSize: Math.min(adapter.limits.maxStorageBufferBindingSize, 256 * 1024 * 1024) } });
    const r = new Renderer(canvas, device, adapter, opts);
    r._init();
    return r;
  }
  constructor(canvas, device, adapter, opts) {
    this.canvas = canvas; this.device = device; this.adapter = adapter; this.opts = opts;
    this.format = navigator.gpu.getPreferredCanvasFormat();
    this.ctx = canvas.getContext("webgpu");
    this.ctx.configure({ device, format: this.format, alphaMode: "opaque" });
    this.meshes = new Map(); this._nextHandle = 1;
    this.width = 0; this.height = 0;
    this.decalCount = 0;
    this.lineCounts = { depth: 0, overlay: 0 };
    this.stats = { draws: 0, triangles: 0, frameMs: 0 };
    this.time = 0;
    this.env = { sunDir: norm([0.45, 0.7, 0.35]), sunIntensity: 3.2, sunColor: [1.0, 0.95, 0.88], ambient: 1.0, zenith: [0.25, 0.42, 0.75], horizon: [0.68, 0.74, 0.82], ground: [0.22, 0.2, 0.18], fog: [0.7, 0.75, 0.85], fogDensity: 0.0012, exposure: 1.0 };
  }
  _init() {
    const d = this.device;
    this.frameBuf = d.createBuffer({ size: 1024, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    this.cascadeBufs = [0, 1].map(() => d.createBuffer({ size: 64, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST }));
    this.drawBuf = d.createBuffer({ size: DRAW_STRIDE * 4096, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    this.drawCapacity = 4096; this.freeSlots = []; this.nextSlot = 0;
    this.emptyInstances = d.createBuffer({ size: 64, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
    this.decalBuf = d.createBuffer({ size: 160 * 2048, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
    this.lineBufs = { depth: d.createBuffer({ size: 28 * 20000, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST }), overlay: d.createBuffer({ size: 28 * 20000, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST }) };
    this.pickBuf = d.createBuffer({ size: 256, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    this.linSampler = d.createSampler({ magFilter: "linear", minFilter: "linear", mipmapFilter: "linear", addressModeU: "repeat", addressModeV: "repeat", maxAnisotropy: 16 });
    this.clampSampler = d.createSampler({ magFilter: "linear", minFilter: "linear", mipmapFilter: "linear", addressModeU: "clamp-to-edge", addressModeV: "clamp-to-edge" });
    this.nearestSampler = d.createSampler({ magFilter: "nearest", minFilter: "nearest" });
    this.shadowSampler = d.createSampler({ compare: "less", magFilter: "linear", minFilter: "linear" });
    this.shadowTex = d.createTexture({ size: [SHADOW_SIZE, SHADOW_SIZE, 2], format: "depth32float", usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING });
    this.shadowViews = [0, 1].map((i) => this.shadowTex.createView({ dimension: "2d", baseArrayLayer: i, arrayLayerCount: 1 }));
    this.shadowArrayView = this.shadowTex.createView({ dimension: "2d-array" });
    this.shadowMats = [mat4.identity(), mat4.identity()];
    // bind group layouts
    this.frameBGL = d.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } }] });
    this.drawBGL = d.createBindGroupLayout({ entries: [
      { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: "uniform", hasDynamicOffset: true, minBindingSize: DRAW_STRIDE } },
      { binding: 1, visibility: GPUShaderStage.VERTEX, buffer: { type: "read-only-storage" } }] });
    this.matBGL = d.createBindGroupLayout({ entries: [
      { binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: { viewDimension: "2d-array" } },
      { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { viewDimension: "2d-array" } },
      { binding: 2, visibility: GPUShaderStage.FRAGMENT, texture: { viewDimension: "2d-array" } },
      { binding: 3, visibility: GPUShaderStage.FRAGMENT, sampler: {} },
      { binding: 4, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } }] });
    this.cascadeBGL = d.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX, buffer: { type: "uniform" } }] });
    this.frameBG = d.createBindGroup({ layout: this.frameBGL, entries: [{ binding: 0, resource: { buffer: this.frameBuf } }] });
    this.cascadeBGs = this.cascadeBufs.map((b) => d.createBindGroup({ layout: this.cascadeBGL, entries: [{ binding: 0, resource: { buffer: b } }] }));
    const vertexLayout = { arrayStride: 48, attributes: [{ shaderLocation: 0, offset: 0, format: "float32x3" }, { shaderLocation: 1, offset: 12, format: "float32x3" }, { shaderLocation: 2, offset: 24, format: "float32x2" }, { shaderLocation: 3, offset: 32, format: "float32x4" }] };
    this.vertexLayout = vertexLayout;
    const gmod = d.createShaderModule({ code: GBUFFER });
    const gTargets = [{ format: "rgba8unorm" }, { format: "rgba16float" }, { format: "rgba8unorm" }, { format: "r32uint" }];
    const gLayout = d.createPipelineLayout({ bindGroupLayouts: [this.frameBGL, this.drawBGL, this.matBGL] });
    const mkG = (cull, bias) => d.createRenderPipeline({ layout: gLayout, vertex: { module: gmod, entryPoint: "vs", buffers: [vertexLayout] }, fragment: { module: gmod, entryPoint: "fs", targets: gTargets }, primitive: { topology: "triangle-list", cullMode: cull, frontFace: "ccw" }, depthStencil: { format: "depth32float", depthWriteEnabled: true, depthCompare: "less-equal", depthBias: bias ? -2 : 0, depthBiasSlopeScale: bias ? -1.5 : 0 } });
    this.gPipe = { back: mkG("back", false), none: mkG("none", false), paint: mkG("none", true) };
    const smod = d.createShaderModule({ code: SHADOW });
    this.shadowPipe = d.createRenderPipeline({ layout: d.createPipelineLayout({ bindGroupLayouts: [this.frameBGL, this.drawBGL, this.cascadeBGL] }), vertex: { module: smod, entryPoint: "vs", buffers: [{ arrayStride: 48, attributes: [{ shaderLocation: 0, offset: 0, format: "float32x3" }] }] }, fragment: { module: smod, entryPoint: "fs", targets: [] }, primitive: { topology: "triangle-list", cullMode: "none" }, depthStencil: { format: "depth32float", depthWriteEnabled: true, depthCompare: "less", depthBias: 2, depthBiasSlopeScale: 2.5 } });
    // decals
    this.decalBGL = d.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } }] });
    this.decalTexBGL = d.createBindGroupLayout({ entries: [
      { binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "depth" } },
      { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { viewDimension: "2d-array" } },
      { binding: 2, visibility: GPUShaderStage.FRAGMENT, texture: { viewDimension: "2d-array" } },
      { binding: 3, visibility: GPUShaderStage.FRAGMENT, sampler: {} },
      { binding: 4, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "unfilterable-float" } }] });
    const dmod = d.createShaderModule({ code: DECAL });
    const blend = { color: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha" }, alpha: { srcFactor: "zero", dstFactor: "one" } };
    this.decalPipe = d.createRenderPipeline({ layout: d.createPipelineLayout({ bindGroupLayouts: [this.frameBGL, this.decalBGL, this.decalTexBGL] }), vertex: { module: dmod, entryPoint: "vs" }, fragment: { module: dmod, entryPoint: "fs", targets: [{ format: "rgba8unorm", blend }, { format: "rgba16float", blend }, { format: "rgba8unorm", blend: { color: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha" }, alpha: { srcFactor: "zero", dstFactor: "one" } } }] }, primitive: { topology: "triangle-list", cullMode: "front" } });
    this.decalBG = d.createBindGroup({ layout: this.decalBGL, entries: [{ binding: 0, resource: { buffer: this.decalBuf } }] });
    // fullscreen passes
    const fs = (code, targets, bgl) => { const m = d.createShaderModule({ code }); return d.createRenderPipeline({ layout: d.createPipelineLayout({ bindGroupLayouts: [this.frameBGL, bgl] }), vertex: { module: m, entryPoint: "vs" }, fragment: { module: m, entryPoint: "fs", targets }, primitive: { topology: "triangle-list" } }); };
    this.ssaoBGL = d.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "depth" } }, { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: {} }, { binding: 2, visibility: GPUShaderStage.FRAGMENT, sampler: {} }] });
    this.ssaoPipe = fs(SSAO, [{ format: "r8unorm" }], this.ssaoBGL);
    this.blurBGL = d.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: {} }, { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "depth" } }, { binding: 2, visibility: GPUShaderStage.FRAGMENT, sampler: {} }] });
    this.blurPipe = fs(BLUR, [{ format: "r8unorm" }], this.blurBGL);
    this.lightBGL = d.createBindGroupLayout({ entries: [
      { binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "unfilterable-float" } },
      { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "unfilterable-float" } },
      { binding: 2, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "unfilterable-float" } },
      { binding: 3, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "depth" } },
      { binding: 4, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "uint" } },
      { binding: 5, visibility: GPUShaderStage.FRAGMENT, texture: {} },
      { binding: 6, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "depth", viewDimension: "2d-array" } },
      { binding: 7, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "comparison" } },
      { binding: 8, visibility: GPUShaderStage.FRAGMENT, sampler: {} }] });
    this.lightPipe = fs(LIGHTING, [{ format: "rgba16float" }], this.lightBGL);
    this.postBGL = d.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: {} }, { binding: 1, visibility: GPUShaderStage.FRAGMENT, sampler: {} }] });
    this.postPipe = fs(POST, [{ format: this.format }], this.postBGL);
    // lines
    const lmod = d.createShaderModule({ code: LINES });
    const lineLayout = d.createPipelineLayout({ bindGroupLayouts: [this.frameBGL] });
    const lineVB = { arrayStride: 28, attributes: [{ shaderLocation: 0, offset: 0, format: "float32x3" }, { shaderLocation: 1, offset: 12, format: "float32x4" }] };
    const lblend = { color: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha" }, alpha: { srcFactor: "one", dstFactor: "one-minus-src-alpha" } };
    this.linePipeDepth = d.createRenderPipeline({ layout: lineLayout, vertex: { module: lmod, entryPoint: "vs", buffers: [lineVB] }, fragment: { module: lmod, entryPoint: "fs", targets: [{ format: this.format, blend: lblend }] }, primitive: { topology: "line-list" }, depthStencil: { format: "depth32float", depthWriteEnabled: false, depthCompare: "less-equal" } });
    this.linePipeOverlay = d.createRenderPipeline({ layout: lineLayout, vertex: { module: lmod, entryPoint: "vs", buffers: [lineVB] }, fragment: { module: lmod, entryPoint: "fs", targets: [{ format: this.format, blend: lblend }] }, primitive: { topology: "line-list" }, depthStencil: { format: "depth32float", depthWriteEnabled: false, depthCompare: "always" } });
    // mips
    const mmod = d.createShaderModule({ code: MIP });
    this.mipBGL = d.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: {} }, { binding: 1, visibility: GPUShaderStage.FRAGMENT, sampler: {} }] });
    this.mipPipes = {};
    this._mipPipe = (format) => this.mipPipes[format] ||= d.createRenderPipeline({ layout: d.createPipelineLayout({ bindGroupLayouts: [this.mipBGL] }), vertex: { module: mmod, entryPoint: "vs" }, fragment: { module: mmod, entryPoint: "fs", targets: [{ format }] }, primitive: { topology: "triangle-list" } });
    this.resize();
  }

  // --------------------------------------------------------------------------------------------
  resize() {
    const dpr = Math.min(window.devicePixelRatio || 1, this.opts.maxDpr || 1.5);
    const w = Math.max(8, Math.floor(this.canvas.clientWidth * dpr)), h = Math.max(8, Math.floor(this.canvas.clientHeight * dpr));
    if (w === this.width && h === this.height) return;
    this.width = w; this.height = h;
    this.canvas.width = w; this.canvas.height = h;
    const d = this.device;
    const mk = (format, scale = 1, usage = GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING) => d.createTexture({ size: [Math.max(1, Math.floor(w * scale)), Math.max(1, Math.floor(h * scale))], format, usage });
    for (const t of ["gAlbedo", "gNormal", "gNormalCopy", "gOrm", "gId", "depth", "ao", "aoBlur", "hdr"]) this[t]?.destroy?.();
    this.gAlbedo = mk("rgba8unorm"); this.gNormal = mk("rgba16float", 1, GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC); this.gOrm = mk("rgba8unorm");
    this.gNormalCopy = mk("rgba16float", 1, GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST);
    this.gId = mk("r32uint", 1, GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC);
    this.depth = mk("depth32float");
    this.ao = mk("r8unorm", 0.5); this.aoBlur = mk("r8unorm", 0.5);
    this.hdr = mk("rgba16float");
    this.views = { albedo: this.gAlbedo.createView(), normal: this.gNormal.createView(), normalCopy: this.gNormalCopy.createView(), orm: this.gOrm.createView(), id: this.gId.createView(), depth: this.depth.createView(), ao: this.ao.createView(), aoBlur: this.aoBlur.createView(), hdr: this.hdr.createView() };
    this.ssaoBG = d.createBindGroup({ layout: this.ssaoBGL, entries: [{ binding: 0, resource: this.views.depth }, { binding: 1, resource: this.views.normal }, { binding: 2, resource: this.clampSampler }] });
    this.blurBG = d.createBindGroup({ layout: this.blurBGL, entries: [{ binding: 0, resource: this.views.ao }, { binding: 1, resource: this.views.depth }, { binding: 2, resource: this.clampSampler }] });
    this.lightBG = d.createBindGroup({ layout: this.lightBGL, entries: [{ binding: 0, resource: this.views.albedo }, { binding: 1, resource: this.views.normal }, { binding: 2, resource: this.views.orm }, { binding: 3, resource: this.views.depth }, { binding: 4, resource: this.views.id }, { binding: 5, resource: this.views.aoBlur }, { binding: 6, resource: this.shadowArrayView }, { binding: 7, resource: this.shadowSampler }, { binding: 8, resource: this.clampSampler }] });
    this.postBG = d.createBindGroup({ layout: this.postBGL, entries: [{ binding: 0, resource: this.views.hdr }, { binding: 1, resource: this.clampSampler }] });
    if (this.decalTex) this._decalTexBG();
  }

  // --------------------------------------------------------------------------------------------
  /** Upload a 2D-array texture set from layers of RGBA Uint8ClampedArray (all `size`²) with mips. */
  _arrayTexture(layers, size, srgb = false) {
    const d = this.device;
    const mips = Math.floor(Math.log2(size)) + 1;
    const format = srgb ? "rgba8unorm-srgb" : "rgba8unorm";
    const tex = d.createTexture({ size: [size, size, layers.length], format, mipLevelCount: mips, usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST | GPUTextureUsage.RENDER_ATTACHMENT });
    layers.forEach((data, i) => d.queue.writeTexture({ texture: tex, origin: [0, 0, i] }, data, { bytesPerRow: size * 4, rowsPerImage: size }, [size, size, 1]));
    // generate mips
    const enc = d.createCommandEncoder();
    const pipe = this._mipPipe(format);
    for (let l = 0; l < layers.length; l++) for (let m = 1; m < mips; m++) {
      const src = tex.createView({ dimension: "2d", baseMipLevel: m - 1, mipLevelCount: 1, baseArrayLayer: l, arrayLayerCount: 1 });
      const dst = tex.createView({ dimension: "2d", baseMipLevel: m, mipLevelCount: 1, baseArrayLayer: l, arrayLayerCount: 1 });
      const bg = d.createBindGroup({ layout: this.mipBGL, entries: [{ binding: 0, resource: src }, { binding: 1, resource: this.clampSampler }] });
      const pass = enc.beginRenderPass({ colorAttachments: [{ view: dst, loadOp: "clear", storeOp: "store", clearValue: [0, 0, 0, 0] }] });
      pass.setPipeline(pipe); pass.setBindGroup(0, bg); pass.draw(3); pass.end();
    }
    d.queue.submit([enc.finish()]);
    return tex;
  }
  /** materials: { names:[], size, albedo:[], normal:[], orm:[], params:[{uvScale, roughness, metal, normalStrength, tint:[r,g,b], wearTint:[r,g,b,a]}] } */
  setMaterials(set) {
    const d = this.device;
    this.materialNames = set.names;
    this.materialIndex = new Map(set.names.map((n, i) => [n, i]));
    for (const t of ["matAlbedo", "matNormal", "matOrm"]) this[t]?.destroy?.();
    this.matAlbedo = this._arrayTexture(set.albedo, set.size, true);
    this.matNormal = this._arrayTexture(set.normal, set.size, false);
    this.matOrm = this._arrayTexture(set.orm, set.size, false);
    const stride = 12; // floats (48 B, matches WGSL MaterialParams): uvScale, rough, metal, normalStrength, tint4, wearTint4
    const arr = new Float32Array(stride * set.params.length);
    set.params.forEach((p, i) => { arr.set([p.uvScale, p.roughness, p.metal, p.normalStrength, ...p.tint, 1, ...p.wearTint], i * stride); });
    this.matParams?.destroy?.();
    this.matParams = d.createBuffer({ size: Math.max(64, arr.byteLength), usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
    d.queue.writeBuffer(this.matParams, 0, arr);
    this.matBG = d.createBindGroup({ layout: this.matBGL, entries: [{ binding: 0, resource: this.matAlbedo.createView({ dimension: "2d-array" }) }, { binding: 1, resource: this.matNormal.createView({ dimension: "2d-array" }) }, { binding: 2, resource: this.matOrm.createView({ dimension: "2d-array" }) }, { binding: 3, resource: this.linSampler }, { binding: 4, resource: { buffer: this.matParams } }] });
  }
  /** Re-upload one material layer (texture layer-stack edit) without rebuilding the whole set. */
  updateMaterial(index, baked, params) {
    const d = this.device, size = baked.size;
    const write = (tex, data, format) => {
      d.queue.writeTexture({ texture: tex, origin: [0, 0, index] }, data, { bytesPerRow: size * 4, rowsPerImage: size }, [size, size, 1]);
      const mips = Math.floor(Math.log2(size)) + 1, enc = d.createCommandEncoder(), pipe = this._mipPipe(format);
      for (let m = 1; m < mips; m++) {
        const src = tex.createView({ dimension: "2d", baseMipLevel: m - 1, mipLevelCount: 1, baseArrayLayer: index, arrayLayerCount: 1 });
        const dst = tex.createView({ dimension: "2d", baseMipLevel: m, mipLevelCount: 1, baseArrayLayer: index, arrayLayerCount: 1 });
        const bg = d.createBindGroup({ layout: this.mipBGL, entries: [{ binding: 0, resource: src }, { binding: 1, resource: this.clampSampler }] });
        const pass = enc.beginRenderPass({ colorAttachments: [{ view: dst, loadOp: "clear", storeOp: "store", clearValue: [0, 0, 0, 0] }] });
        pass.setPipeline(pipe); pass.setBindGroup(0, bg); pass.draw(3); pass.end();
      }
      d.queue.submit([enc.finish()]);
    };
    write(this.matAlbedo, baked.albedo, "rgba8unorm-srgb"); write(this.matNormal, baked.normal, "rgba8unorm"); write(this.matOrm, baked.orm, "rgba8unorm");
    if (params) { const p = params; d.queue.writeBuffer(this.matParams, index * 48, new Float32Array([p.uvScale, p.roughness, p.metal, p.normalStrength, ...p.tint, 1, ...p.wearTint])); }
  }
  setDecalAtlas(set) {
    this.decalTex?.destroy?.(); this.decalNormalTex?.destroy?.();
    this.decalTex = this._arrayTexture(set.albedo, set.size, true);
    this.decalNormalTex = this._arrayTexture(set.normal, set.size, false);
    this.decalNames = set.names;
    this._decalTexBG();
  }
  _decalTexBG() {
    this.decalTexBG = this.device.createBindGroup({ layout: this.decalTexBGL, entries: [{ binding: 0, resource: this.views.depth }, { binding: 1, resource: this.decalTex.createView({ dimension: "2d-array" }) }, { binding: 2, resource: this.decalNormalTex.createView({ dimension: "2d-array" }) }, { binding: 3, resource: this.clampSampler }, { binding: 4, resource: this.views.normalCopy }] });
  }
  /** decals: [{ model: Float32Array(16) (unit cube → world), layer, opacity, normalStrength, roughness }] */
  setDecals(list) {
    const n = Math.min(list.length, 2048);
    const arr = new Float32Array(n * 40);
    const u32 = new Uint32Array(arr.buffer);
    for (let i = 0; i < n; i++) {
      const dcl = list[i], o = i * 40;
      const inv = mat4.invert(dcl.model) || mat4.identity();
      arr.set(inv, o); arr.set(dcl.model, o + 16);
      u32[o + 32] = dcl.layer | 0; u32[o + 33] = 0; u32[o + 34] = 0; u32[o + 35] = 0;
      arr[o + 36] = dcl.opacity ?? 1; arr[o + 37] = dcl.normalStrength ?? 1; arr[o + 38] = dcl.roughness ?? 0.8; arr[o + 39] = 0;
    }
    if (n) this.device.queue.writeBuffer(this.decalBuf, 0, arr);
    this.decalCount = n;
  }

  // --------------------------------------------------------------------------------------------
  _allocSlot() { if (this.freeSlots.length) return this.freeSlots.pop(); if (this.nextSlot >= this.drawCapacity) throw new Error("draw slots exhausted"); return this.nextSlot++; }
  /**
   * batch: { vertices: Float32Array (12 floats/vertex), indices: Uint32Array }
   * opts: { material, pickId, model, flags, tint, uvScale, roughnessBias, wear, instances: Float32Array(16*n), castShadow, cull: 'back'|'none', tag }
   */
  addMesh(batch, opts = {}) {
    const d = this.device;
    const vb = d.createBuffer({ size: batch.vertices.byteLength, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
    d.queue.writeBuffer(vb, 0, batch.vertices);
    const ib = d.createBuffer({ size: Math.ceil(batch.indices.byteLength / 4) * 4, usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST });
    d.queue.writeBuffer(ib, 0, batch.indices);
    const slot = this._allocSlot();
    const handle = this._nextHandle++;
    const m = { handle, vb, ib, indexCount: batch.indices.length, slot, material: this.materialIndex?.get(opts.material) ?? 0, pickId: opts.pickId || 0, flags: opts.flags || 0, model: opts.model || mat4.identity(), tint: opts.tint || [1, 1, 1, 0], uvScale: [opts.uvScale || 0, opts.roughnessBias || 0, opts.wear ?? 0.5, 0], castShadow: opts.castShadow !== false, cull: opts.cull || "back", tag: opts.tag || "", instanceCount: 1, instBuf: null, visible: true, decals: opts.decals !== false, bounds: opts.bounds || null };
    if (opts.instances) this._setInstances(m, opts.instances);
    m.bg = d.createBindGroup({ layout: this.drawBGL, entries: [{ binding: 0, resource: { buffer: this.drawBuf, offset: 0, size: DRAW_STRIDE } }, { binding: 1, resource: { buffer: m.instBuf || this.emptyInstances } }] });
    this._writeDraw(m);
    this.meshes.set(handle, m);
    return handle;
  }
  _setInstances(m, matrices) {
    m.instanceCount = matrices.length / 16;
    if (!m.instBuf || m.instBuf.size < matrices.byteLength) { m.instBuf?.destroy?.(); m.instBuf = this.device.createBuffer({ size: Math.max(64, matrices.byteLength), usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST }); m.bg = null; }
    this.device.queue.writeBuffer(m.instBuf, 0, matrices);
    m.flags |= FLAG_INSTANCED;
  }
  updateInstances(handle, matrices) {
    const m = this.meshes.get(handle); if (!m) return;
    this._setInstances(m, matrices);
    if (!m.bg) m.bg = this.device.createBindGroup({ layout: this.drawBGL, entries: [{ binding: 0, resource: { buffer: this.drawBuf, offset: 0, size: DRAW_STRIDE } }, { binding: 1, resource: { buffer: m.instBuf } }] });
    this._writeDraw(m);
  }
  _writeDraw(m) {
    const f = new Float32Array(DRAW_STRIDE / 4), u = new Uint32Array(f.buffer);
    f.set(m.model, 0); f.set(m.model, 16);
    u[32] = m.material; u[33] = m.pickId; u[34] = m.flags; u[35] = 0;
    f.set(m.tint, 36); f.set(m.uvScale, 40);
    this.device.queue.writeBuffer(this.drawBuf, m.slot * DRAW_STRIDE, f);
  }
  setTransform(handle, model) { const m = this.meshes.get(handle); if (!m) return; m.model = model; this._writeDraw(m); }
  setVisible(handle, v) { const m = this.meshes.get(handle); if (m) m.visible = v; }
  setTint(handle, tint) { const m = this.meshes.get(handle); if (!m) return; m.tint = tint; this._writeDraw(m); }
  removeMesh(handle) { const m = this.meshes.get(handle); if (!m) return; m.vb.destroy(); m.ib.destroy(); m.instBuf?.destroy?.(); this.freeSlots.push(m.slot); this.meshes.delete(handle); }
  removeTag(tag) { for (const m of [...this.meshes.values()]) if (m.tag === tag) this.removeMesh(m.handle); }
  setTagVisible(tag, v) { for (const m of this.meshes.values()) if (m.tag === tag) m.visible = v; }

  /** lines: Float32Array of [x,y,z,r,g,b,a] per vertex, pairs form segments. */
  setLines(kind, data) {
    const count = data ? data.length / 7 : 0;
    this.lineCounts[kind] = count;
    if (count) {
      if (this.lineBufs[kind].size < data.byteLength) { this.lineBufs[kind].destroy(); this.lineBufs[kind] = this.device.createBuffer({ size: data.byteLength * 2, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST }); }
      this.device.queue.writeBuffer(this.lineBufs[kind], 0, data);
    }
  }

  // --------------------------------------------------------------------------------------------
  _fitCascade(cam, near, far, index) {
    // frustum slice corners in world space
    const inv = mat4.invert(cam.viewProj);
    const corners = [];
    for (const z of [near, far]) for (const y of [-1, 1]) for (const x of [-1, 1]) {
      // convert view distance to NDC depth via projection
      const vz = -z; const pz = cam.proj[10] * vz + cam.proj[14]; const pw = -vz; const ndcZ = pz / pw;
      corners.push(mat4.transformPoint(inv, [x, y, ndcZ]));
    }
    const center = corners.reduce((a, c) => add(a, mul(c, 1 / 8)), [0, 0, 0]);
    let radius = 0; for (const c of corners) radius = Math.max(radius, Math.hypot(c[0] - center[0], c[1] - center[1], c[2] - center[2]));
    radius = Math.ceil(radius * 4) / 4;
    const L = this.env.sunDir;
    const eye = madd(center, L, radius * 2.5);
    const up = Math.abs(L[1]) > 0.95 ? [0, 0, 1] : [0, 1, 0];
    const view = mat4.lookAt(eye, center, up);
    // texel snapping
    const texel = (radius * 2) / SHADOW_SIZE;
    const c = mat4.transformPoint(view, center);
    const sx = Math.round(c[0] / texel) * texel - c[0], sy = Math.round(c[1] / texel) * texel - c[1];
    const proj = mat4.ortho(-radius + sx, radius + sx, -radius + sy, radius + sy, 0.1, radius * 5);
    const m = mat4.multiply(proj, view);
    this.shadowMats[index] = m;
    this.device.queue.writeBuffer(this.cascadeBufs[index], 0, m);
  }
  _writeFrame(cam, selection) {
    const e = this.env;
    const buf = new ArrayBuffer(1024); const f = new Float32Array(buf); const u = new Uint32Array(buf);
    f.set(cam.view, 0); f.set(cam.proj, 16); f.set(cam.viewProj, 32); f.set(mat4.invert(cam.viewProj), 48); f.set(mat4.invert(cam.proj), 64);
    f.set(this.shadowMats[0], 80); f.set(this.shadowMats[1], 96);
    let o = 112;
    f.set([...cam.position, this.time], o); o += 4;
    f.set([...e.sunDir, e.sunIntensity], o); o += 4;
    f.set([...e.sunColor, e.ambient], o); o += 4;
    f.set([...e.zenith, 1], o); o += 4;
    f.set([...e.horizon, 1], o); o += 4;
    f.set([...e.ground, 1], o); o += 4;
    f.set([this.cascadeSplit, cam.near, cam.far, e.exposure], o); o += 4;
    f.set([this.width, this.height, 1 / this.width, 1 / this.height], o); o += 4;
    u.set([selection.selected || 0, selection.hovered || 0, selection.flags || 0, 0], o); o += 4;
    f.set([...e.fog, e.fogDensity], o); o += 4;
    this.device.queue.writeBuffer(this.frameBuf, 0, buf);
  }
  _drawMeshes(pass, shadow) {
    let draws = 0, tris = 0;
    let lastPipe = null;
    for (const m of this.meshes.values()) {
      if (!m.visible || (shadow && !m.castShadow) || m.instanceCount === 0) continue;
      if (!shadow) {
        const pipe = (m.flags & FLAG_PAINT) ? this.gPipe.paint : m.cull === "none" ? this.gPipe.none : this.gPipe.back;
        if (pipe !== lastPipe) { pass.setPipeline(pipe); lastPipe = pipe; }
      }
      pass.setBindGroup(1, m.bg, [m.slot * DRAW_STRIDE]);
      pass.setVertexBuffer(0, m.vb);
      pass.setIndexBuffer(m.ib, "uint32");
      pass.drawIndexed(m.indexCount, m.instanceCount);
      draws++; tris += (m.indexCount / 3) * m.instanceCount;
    }
    if (!shadow) { this.stats.draws = draws; this.stats.triangles = tris; }
  }
  render(cam, selection = {}) {
    const t0 = performance.now();
    this.resize();
    const d = this.device;
    this.cascadeSplit = Math.min(90, cam.far * 0.25);
    this._fitCascade(cam, cam.near, this.cascadeSplit, 0);
    this._fitCascade(cam, this.cascadeSplit * 0.9, Math.min(cam.far, 700), 1);
    this._writeFrame(cam, selection);
    const enc = d.createCommandEncoder();
    // 1. shadows
    for (let i = 0; i < 2; i++) {
      const pass = enc.beginRenderPass({ colorAttachments: [], depthStencilAttachment: { view: this.shadowViews[i], depthClearValue: 1, depthLoadOp: "clear", depthStoreOp: "store" } });
      pass.setPipeline(this.shadowPipe); pass.setBindGroup(0, this.frameBG); pass.setBindGroup(2, this.cascadeBGs[i]);
      this._drawMeshes(pass, true);
      pass.end();
    }
    // 2. g-buffer
    {
      const pass = enc.beginRenderPass({
        colorAttachments: [
          { view: this.views.albedo, loadOp: "clear", storeOp: "store", clearValue: [0, 0, 0, 0] },
          { view: this.views.normal, loadOp: "clear", storeOp: "store", clearValue: [0.5, 1, 0.5, 0] },
          { view: this.views.orm, loadOp: "clear", storeOp: "store", clearValue: [1, 1, 0, 0] },
          { view: this.views.id, loadOp: "clear", storeOp: "store", clearValue: [0, 0, 0, 0] }],
        depthStencilAttachment: { view: this.views.depth, depthClearValue: 1, depthLoadOp: "clear", depthStoreOp: "store" } });
      pass.setBindGroup(0, this.frameBG);
      if (this.matBG) { pass.setBindGroup(2, this.matBG); this._drawMeshes(pass, false); }
      pass.end();
    }
    // 3. decals (read a copy of the normal buffer: a texture cannot be sampled while attached)
    if (this.decalCount && this.decalTexBG) {
      enc.copyTextureToTexture({ texture: this.gNormal }, { texture: this.gNormalCopy }, [this.width, this.height, 1]);
      const pass = enc.beginRenderPass({ colorAttachments: [
        { view: this.views.albedo, loadOp: "load", storeOp: "store" },
        { view: this.views.normal, loadOp: "load", storeOp: "store" },
        { view: this.views.orm, loadOp: "load", storeOp: "store" }] });
      pass.setPipeline(this.decalPipe); pass.setBindGroup(0, this.frameBG); pass.setBindGroup(1, this.decalBG); pass.setBindGroup(2, this.decalTexBG);
      pass.draw(36, this.decalCount);
      pass.end();
    }
    // 4. SSAO + blur
    {
      let pass = enc.beginRenderPass({ colorAttachments: [{ view: this.views.ao, loadOp: "clear", storeOp: "store", clearValue: [1, 1, 1, 1] }] });
      pass.setPipeline(this.ssaoPipe); pass.setBindGroup(0, this.frameBG); pass.setBindGroup(1, this.ssaoBG); pass.draw(3); pass.end();
      pass = enc.beginRenderPass({ colorAttachments: [{ view: this.views.aoBlur, loadOp: "clear", storeOp: "store", clearValue: [1, 1, 1, 1] }] });
      pass.setPipeline(this.blurPipe); pass.setBindGroup(0, this.frameBG); pass.setBindGroup(1, this.blurBG); pass.draw(3); pass.end();
    }
    // 5. lighting
    {
      const pass = enc.beginRenderPass({ colorAttachments: [{ view: this.views.hdr, loadOp: "clear", storeOp: "store", clearValue: [0, 0, 0, 1] }] });
      pass.setPipeline(this.lightPipe); pass.setBindGroup(0, this.frameBG); pass.setBindGroup(1, this.lightBG); pass.draw(3); pass.end();
    }
    // 6. post + lines
    {
      const target = this.ctx.getCurrentTexture().createView();
      let pass = enc.beginRenderPass({ colorAttachments: [{ view: target, loadOp: "clear", storeOp: "store", clearValue: [0, 0, 0, 1] }] });
      pass.setPipeline(this.postPipe); pass.setBindGroup(0, this.frameBG); pass.setBindGroup(1, this.postBG); pass.draw(3); pass.end();
      if (this.lineCounts.depth || this.lineCounts.overlay) {
        pass = enc.beginRenderPass({ colorAttachments: [{ view: target, loadOp: "load", storeOp: "store" }], depthStencilAttachment: { view: this.views.depth, depthReadOnly: true } });
        pass.setBindGroup(0, this.frameBG);
        if (this.lineCounts.depth) { pass.setPipeline(this.linePipeDepth); pass.setVertexBuffer(0, this.lineBufs.depth); pass.draw(this.lineCounts.depth); }
        if (this.lineCounts.overlay) { pass.setPipeline(this.linePipeOverlay); pass.setVertexBuffer(0, this.lineBufs.overlay); pass.draw(this.lineCounts.overlay); }
        pass.end();
      }
    }
    // picking request
    let pickIssued = false;
    if (this._pickReq && !this._pickBusy) {
      pickIssued = true;
      const { x, y } = this._pickReq;
      const px = Math.min(this.width - 1, Math.max(0, Math.floor(x))), py = Math.min(this.height - 1, Math.max(0, Math.floor(y)));
      enc.copyTextureToBuffer({ texture: this.gId, origin: [px, py, 0] }, { buffer: this.pickBuf, bytesPerRow: 256 }, [1, 1, 1]);
      this._pickBusy = true;
    }
    d.queue.submit([enc.finish()]);
    if (pickIssued) {
      const req = this._pickReq; this._pickReq = null;
      this.pickBuf.mapAsync(GPUMapMode.READ).then(() => {
        const id = new Uint32Array(this.pickBuf.getMappedRange())[0];
        this.pickBuf.unmap(); this._pickBusy = false;
        req.resolve(id);
      }).catch(() => { this._pickBusy = false; req.resolve(0); });
    }
    this.stats.frameMs = performance.now() - t0;
  }
  /** Returns the pick id under the canvas pixel (CSS px) at the next rendered frame. */
  pick(cssX, cssY) {
    const dpr = this.width / this.canvas.clientWidth;
    if (this._pickReq) this._pickReq.resolve(0);
    return new Promise((resolve) => { this._pickReq = { x: cssX * dpr, y: cssY * dpr, resolve }; });
  }
}

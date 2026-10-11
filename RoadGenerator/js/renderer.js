// WebGPU renderer: scene pass with sun shadow map, sky, depth-biased decals, overlay.
// Also owns the GPU texture stacks: layer compositor (compute) + mip generation.

import { SCENE_WGSL, SHADOW_WGSL, SKY_WGSL, OVERLAY_WGSL, LAYER_WGSL, MIP_WGSL } from './shaders.js';
import { mat4Multiply, mat4Ortho, mat4LookAt, mat4Invert, mat4Identity, v3 } from './math.js';
import { PARAM_FLOATS, packStack } from './layers.js';

export const MATERIAL_INDEX = { asphalt: 0, concrete: 1, pavers: 2 };
export const TEX_SIZE = 1024;
const TEX_LAYERS = 3;
const FRAME_FLOATS = 84;
const VERTEX_STRIDE = 48;
const OVERLAY_STRIDE = 28;

export class Renderer {
  static async create(canvas) {
    if (!navigator.gpu) throw new Error('WebGPU is not available in this browser. Use a current Chrome, Edge or Safari Technology Preview.');
    const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
    if (!adapter) throw new Error('No WebGPU adapter was found.');
    const device = await adapter.requestDevice({
      requiredLimits: {
        maxBufferSize: Math.min(adapter.limits.maxBufferSize, 512 * 1024 * 1024),
        maxStorageBufferBindingSize: Math.min(adapter.limits.maxStorageBufferBindingSize, 256 * 1024 * 1024),
      },
    });
    const r = new Renderer(canvas, device, adapter);
    await r.init();
    return r;
  }

  constructor(canvas, device, adapter) {
    this.canvas = canvas;
    this.device = device;
    this.adapter = adapter;
    this.info = adapter.info || {};
    this.format = navigator.gpu.getPreferredCanvasFormat();
    this.context = canvas.getContext('webgpu');
    this.context.configure({ device, format: this.format, alphaMode: 'opaque' });
    this.depthFormat = 'depth32float';
    this.meshes = { main: null, decal: null, ground: null, vehicles: null, overlay: null };
    this.materialDirty = new Set([0, 1, 2]);
    this.shadowSize = 2048;
    this.stats = { drawCalls: 0, triangles: 0, frameMs: 0 };
    this.lastFrameTime = performance.now();
  }

  async init() {
    const d = this.device;
    this.frameBuf = d.createBuffer({ size: FRAME_FLOATS * 4, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    this.lightBuf = d.createBuffer({ size: 64, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    this.overlayUniform = d.createBuffer({ size: 64, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });

    // Material texture arrays (albedo + surface), with a full mip chain.
    const mipCount = Math.log2(TEX_SIZE) + 1;
    const texDesc = {
      size: [TEX_SIZE, TEX_SIZE, TEX_LAYERS],
      format: 'rgba8unorm',
      mipLevelCount: mipCount,
      usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.STORAGE_BINDING,
    };
    this.albedoTex = d.createTexture(texDesc);
    this.surfaceTex = d.createTexture(texDesc);
    this.mipCount = mipCount;
    this.sampler = d.createSampler({ addressModeU: 'repeat', addressModeV: 'repeat', magFilter: 'linear', minFilter: 'linear', mipmapFilter: 'linear', maxAnisotropy: 8 });
    this.shadowSampler = d.createSampler({ compare: 'less-equal', magFilter: 'linear', minFilter: 'linear' });
    this.paramBuf = [0, 1, 2].map(() => d.createBuffer({ size: PARAM_FLOATS * 4, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST }));

    // Compositor pipelines
    const layerMod = d.createShaderModule({ code: LAYER_WGSL });
    this.layerPipe = await d.createComputePipelineAsync({ layout: 'auto', compute: { module: layerMod, entryPoint: 'main' } });
    const mipMod = d.createShaderModule({ code: MIP_WGSL });
    this.mipPipe = await d.createComputePipelineAsync({ layout: 'auto', compute: { module: mipMod, entryPoint: 'main' } });

    // Shadow map
    this.shadowTex = d.createTexture({ size: [this.shadowSize, this.shadowSize], format: this.depthFormat, usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING });

    // Scene pipeline
    const sceneMod = d.createShaderModule({ code: SCENE_WGSL });
    const vbLayout = [{
      arrayStride: VERTEX_STRIDE,
      attributes: [
        { shaderLocation: 0, offset: 0, format: 'float32x3' },
        { shaderLocation: 1, offset: 12, format: 'float32x3' },
        { shaderLocation: 2, offset: 24, format: 'float32x2' },
        { shaderLocation: 3, offset: 32, format: 'float32x3' },
        { shaderLocation: 4, offset: 44, format: 'float32' },
      ],
    }];
    this.sceneBGL = d.createBindGroupLayout({
      entries: [
        { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } },
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { viewDimension: '2d-array' } },
        { binding: 2, visibility: GPUShaderStage.FRAGMENT, texture: { viewDimension: '2d-array' } },
        { binding: 3, visibility: GPUShaderStage.FRAGMENT, sampler: { type: 'filtering' } },
        { binding: 4, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'depth' } },
        { binding: 5, visibility: GPUShaderStage.FRAGMENT, sampler: { type: 'comparison' } },
      ],
    });
    const sceneLayout = d.createPipelineLayout({ bindGroupLayouts: [this.sceneBGL] });
    const depthState = (bias) => ({
      format: this.depthFormat,
      depthWriteEnabled: true,
      depthCompare: 'less',
      ...(bias ? { depthBias: -6, depthBiasSlopeScale: -3, depthBiasClamp: 0 } : {}),
    });
    this.scenePipe = await d.createRenderPipelineAsync({
      layout: sceneLayout,
      vertex: { module: sceneMod, entryPoint: 'vs', buffers: vbLayout },
      fragment: { module: sceneMod, entryPoint: 'fs', targets: [{ format: this.format }] },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
      depthStencil: depthState(false),
    });
    this.decalPipe = await d.createRenderPipelineAsync({
      layout: sceneLayout,
      vertex: { module: sceneMod, entryPoint: 'vs', buffers: vbLayout },
      fragment: { module: sceneMod, entryPoint: 'fs', targets: [{ format: this.format }] },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
      depthStencil: depthState(true),
    });

    // Shadow pipeline
    const shadowMod = d.createShaderModule({ code: SHADOW_WGSL });
    this.shadowBGL = d.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX, buffer: { type: 'uniform' } }] });
    this.shadowPipe = await d.createRenderPipelineAsync({
      layout: d.createPipelineLayout({ bindGroupLayouts: [this.shadowBGL] }),
      vertex: { module: shadowMod, entryPoint: 'vs', buffers: vbLayout },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
      depthStencil: { format: this.depthFormat, depthWriteEnabled: true, depthCompare: 'less', depthBias: 2, depthBiasSlopeScale: 2 },
    });
    this.shadowBG = d.createBindGroup({ layout: this.shadowBGL, entries: [{ binding: 0, resource: { buffer: this.lightBuf } }] });

    // Sky
    const skyMod = d.createShaderModule({ code: SKY_WGSL });
    this.skyBGL = d.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } }] });
    this.skyPipe = await d.createRenderPipelineAsync({
      layout: d.createPipelineLayout({ bindGroupLayouts: [this.skyBGL] }),
      vertex: { module: skyMod, entryPoint: 'vs' },
      fragment: { module: skyMod, entryPoint: 'fs', targets: [{ format: this.format }] },
      primitive: { topology: 'triangle-list' },
      depthStencil: { format: this.depthFormat, depthWriteEnabled: false, depthCompare: 'always' },
    });
    this.skyBG = d.createBindGroup({ layout: this.skyBGL, entries: [{ binding: 0, resource: { buffer: this.frameBuf } }] });

    // Overlay (no depth test, alpha blended)
    const ovMod = d.createShaderModule({ code: OVERLAY_WGSL });
    this.overlayBGL = d.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX, buffer: { type: 'uniform' } }] });
    const ovLayout = d.createPipelineLayout({ bindGroupLayouts: [this.overlayBGL] });
    const ovVB = [{ arrayStride: OVERLAY_STRIDE, attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x3' }, { shaderLocation: 1, offset: 12, format: 'float32x4' }] }];
    const blend = { color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha', operation: 'add' }, alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' } };
    this.overlayTriPipe = await d.createRenderPipelineAsync({
      layout: ovLayout,
      vertex: { module: ovMod, entryPoint: 'vs', buffers: ovVB },
      fragment: { module: ovMod, entryPoint: 'fs', targets: [{ format: this.format, blend }] },
      primitive: { topology: 'triangle-list' },
      depthStencil: { format: this.depthFormat, depthWriteEnabled: false, depthCompare: 'always' },
    });
    this.overlayLinePipe = await d.createRenderPipelineAsync({
      layout: ovLayout,
      vertex: { module: ovMod, entryPoint: 'vs', buffers: ovVB },
      fragment: { module: ovMod, entryPoint: 'fs', targets: [{ format: this.format, blend }] },
      primitive: { topology: 'line-list' },
      depthStencil: { format: this.depthFormat, depthWriteEnabled: false, depthCompare: 'always' },
    });
    this.overlayBG = d.createBindGroup({ layout: this.overlayBGL, entries: [{ binding: 0, resource: { buffer: this.overlayUniform } }] });

    this.depthTex = null;
    this.sceneBG = null;
    this.ensureSize();
    this.buildGround();
  }

  // Bind group is rebuilt whenever a texture changes (cheap; textures are persistent objects).
  makeSceneBindGroup() {
    return this.device.createBindGroup({
      layout: this.sceneBGL,
      entries: [
        { binding: 0, resource: { buffer: this.frameBuf } },
        { binding: 1, resource: this.albedoTex.createView({ dimension: '2d-array' }) },
        { binding: 2, resource: this.surfaceTex.createView({ dimension: '2d-array' }) },
        { binding: 3, resource: this.sampler },
        { binding: 4, resource: this.shadowTex.createView() },
        { binding: 5, resource: this.shadowSampler },
      ],
    });
  }

  ensureSize() {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = Math.max(2, Math.floor(this.canvas.clientWidth * dpr));
    const h = Math.max(2, Math.floor(this.canvas.clientHeight * dpr));
    if (this.canvas.width !== w || this.canvas.height !== h || !this.depthTex) {
      this.canvas.width = w;
      this.canvas.height = h;
      if (this.depthTex) this.depthTex.destroy();
      this.depthTex = this.device.createTexture({ size: [w, h], format: this.depthFormat, usage: GPUTextureUsage.RENDER_ATTACHMENT });
      this.width = w; this.height = h;
      return true;
    }
    return false;
  }

  buildGround() {
    // Large ground plane, slightly below the road datum so road edges meet it cleanly.
    const S = 3000;
    const y = -0.03;
    const v = [];
    const quad = (x0, z0, x1, z1) => {
      const pts = [[x0, z0], [x1, z0], [x1, z1], [x0, z1]];
      for (const [x, z] of [pts[0], pts[1], pts[2], pts[0], pts[2], pts[3]]) {
        v.push(x, y, z, 0, 1, 0, x, z, 0.3, 0.3, 0.3, -3);
      }
    };
    quad(-S, -S, S, S);
    this.groundVB = this.device.createBuffer({ size: v.length * 4, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
    this.device.queue.writeBuffer(this.groundVB, 0, new Float32Array(v));
    this.groundCount = 6;
  }

  setMesh(name, built) {
    // built = { vertices: Float32Array, indices: Uint32Array }
    const old = this.meshes[name];
    if (old) { old.vb.destroy(); old.ib.destroy(); }
    if (!built || built.indices.length === 0) { this.meshes[name] = null; return; }
    const vb = this.device.createBuffer({ size: Math.max(16, built.vertices.byteLength), usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
    const ib = this.device.createBuffer({ size: Math.max(16, Math.ceil(built.indices.byteLength / 4) * 4), usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST });
    this.device.queue.writeBuffer(vb, 0, built.vertices);
    this.device.queue.writeBuffer(ib, 0, built.indices);
    this.meshes[name] = { vb, ib, count: built.indices.length, format: 'uint32' };
  }

  // Dynamic vehicles: rewritten every frame. Vehicles are capped to 25 by the simulation.
  setDynamic(name, vertices, indices) {
    const cur = this.meshes[name];
    const bytes = Math.max(vertices.byteLength, 64);
    const ibytes = Math.ceil(indices.byteLength / 4) * 4;
    if (!cur || cur.capacity < bytes || cur.icap < ibytes) {
      if (cur) { cur.vb.destroy(); cur.ib.destroy(); }
      const cap = Math.max(bytes * 2, 1 << 20);
      const icap = Math.max(ibytes * 2, 1 << 18);
      const vb = this.device.createBuffer({ size: cap, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
      const ib = this.device.createBuffer({ size: icap, usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST });
      this.meshes[name] = { vb, ib, count: 0, capacity: cap, icap };
    }
    const m = this.meshes[name];
    if (indices.length === 0) { m.count = 0; return; }
    this.device.queue.writeBuffer(m.vb, 0, vertices);
    this.device.queue.writeBuffer(m.ib, 0, indices);
    m.count = indices.length;
  }

  setOverlay(triVerts, lineVerts) {
    // triVerts / lineVerts: Float32Array with stride 7 (pos xyz, rgba)
    const ensure = (key, data) => {
      const cur = this.meshes[key];
      const bytes = Math.max(data.byteLength, 64);
      if (!cur || cur.capacity < bytes) {
        if (cur) cur.vb.destroy();
        const cap = Math.max(bytes * 2, 1 << 16);
        this.meshes[key] = { vb: this.device.createBuffer({ size: cap, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST }), capacity: cap, count: 0 };
      }
      const m = this.meshes[key];
      if (data.length) this.device.queue.writeBuffer(m.vb, 0, data);
      m.count = data.length / 7;
    };
    ensure('overlayTri', triVerts);
    ensure('overlayLine', lineVerts);
  }

  markMaterialDirty(index) { this.materialDirty.add(index); }

  // ----- texture compositing (compute) ---------------------------------------------------
  regenerateMaterials(stacks, tiles) {
    if (this.materialDirty.size === 0) return;
    const d = this.device;
    const enc = d.createCommandEncoder();
    const names = ['asphalt', 'concrete', 'pavers'];
    for (const idx of this.materialDirty) {
      const name = names[idx];
      const stack = stacks[name] || [];
      d.queue.writeBuffer(this.paramBuf[idx], 0, packStack(stack, tiles[name]));
      const pass = enc.beginComputePass();
      pass.setPipeline(this.layerPipe);
      pass.setBindGroup(0, d.createBindGroup({
        layout: this.layerPipe.getBindGroupLayout(0),
        entries: [
          { binding: 0, resource: { buffer: this.paramBuf[idx] } },
          { binding: 1, resource: this.albedoTex.createView({ dimension: '2d', baseArrayLayer: idx, arrayLayerCount: 1, baseMipLevel: 0, mipLevelCount: 1 }) },
          { binding: 2, resource: this.surfaceTex.createView({ dimension: '2d', baseArrayLayer: idx, arrayLayerCount: 1, baseMipLevel: 0, mipLevelCount: 1 }) },
        ],
      }));
      pass.dispatchWorkgroups(TEX_SIZE / 8, TEX_SIZE / 8, 1);
      pass.end();
      for (const tex of [this.albedoTex, this.surfaceTex]) {
        for (let m = 1; m < this.mipCount; m++) {
          const size = TEX_SIZE >> m;
          const mp = enc.beginComputePass();
          mp.setPipeline(this.mipPipe);
          mp.setBindGroup(0, d.createBindGroup({
            layout: this.mipPipe.getBindGroupLayout(0),
            entries: [
              { binding: 0, resource: tex.createView({ dimension: '2d', baseArrayLayer: idx, arrayLayerCount: 1, baseMipLevel: m - 1, mipLevelCount: 1 }) },
              { binding: 1, resource: tex.createView({ dimension: '2d', baseArrayLayer: idx, arrayLayerCount: 1, baseMipLevel: m, mipLevelCount: 1 }) },
            ],
          }));
          mp.dispatchWorkgroups(Math.ceil(size / 8), Math.ceil(size / 8), 1);
          mp.end();
        }
      }
    }
    d.queue.submit([enc.finish()]);
    this.materialDirty.clear();
    this.sceneBG = null;
  }

  // ----- frame ---------------------------------------------------------------------------
  render(view) {
    const d = this.device;
    const resized = this.ensureSize();
    if (resized) this.sceneBG = null;
    if (!this.sceneBG) this.sceneBG = this.makeSceneBindGroup();

    const sunDir = view.sunDir;
    const lightVP = this.computeLightVP(sunDir, view.target, view.shadowExtent || 130);
    const f = new Float32Array(FRAME_FLOATS);
    f.set(view.viewProj, 0);
    f.set(lightVP, 16);
    f.set([view.eye[0], view.eye[1], view.eye[2], view.time || 0], 32);
    f.set([sunDir[0], sunDir[1], sunDir[2], 0], 36);
    f.set([view.sunColor[0], view.sunColor[1], view.sunColor[2], 0], 40);
    f.set([view.sky.zenith[0], view.sky.zenith[1], view.sky.zenith[2], 0], 44);
    f.set([view.sky.horizon[0], view.sky.horizon[1], view.sky.horizon[2], 0], 48);
    f.set([view.sky.ground[0], view.sky.ground[1], view.sky.ground[2], 0], 52);
    f.set([view.fogDensity, 0, 0, 0], 56);
    f.set([view.wetness, view.tiles.asphalt, view.tiles.concrete, view.tiles.pavers], 60);
    f.set([this.shadowSize, view.exposure || 1, this.width, this.height], 64);
    f.set(mat4Invert(view.viewProj) || mat4Identity(), 68);
    d.queue.writeBuffer(this.frameBuf, 0, f);
    d.queue.writeBuffer(this.lightBuf, 0, lightVP);
    d.queue.writeBuffer(this.overlayUniform, 0, view.viewProj);

    const enc = d.createCommandEncoder();
    let draws = 0, tris = 0;

    // 1) shadow map
    const sp = enc.beginRenderPass({
      colorAttachments: [],
      depthStencilAttachment: { view: this.shadowTex.createView(), depthClearValue: 1, depthLoadOp: 'clear', depthStoreOp: 'store' },
    });
    sp.setPipeline(this.shadowPipe);
    sp.setBindGroup(0, this.shadowBG);
    for (const key of ['main', 'vehicles']) {
      const m = this.meshes[key];
      if (!m || !m.count) continue;
      sp.setVertexBuffer(0, m.vb);
      sp.setIndexBuffer(m.ib, 'uint32');
      sp.drawIndexed(m.count);
    }
    sp.end();

    // 2) main pass
    const pass = enc.beginRenderPass({
      colorAttachments: [{
        view: this.context.getCurrentTexture().createView(),
        clearValue: { r: view.sky.horizon[0], g: view.sky.horizon[1], b: view.sky.horizon[2], a: 1 },
        loadOp: 'clear', storeOp: 'store',
      }],
      depthStencilAttachment: { view: this.depthTex.createView(), depthClearValue: 1, depthLoadOp: 'clear', depthStoreOp: 'store' },
    });
    pass.setBindGroup(0, this.sceneBG);
    pass.setPipeline(this.skyPipe);
    pass.setBindGroup(0, this.skyBG);
    pass.draw(3);
    pass.setBindGroup(0, this.sceneBG);
    pass.setPipeline(this.scenePipe);
    pass.setVertexBuffer(0, this.groundVB);
    pass.draw(this.groundCount);
    draws++; tris += this.groundCount / 3;
    const drawIndexed = (key, pipe) => {
      const m = this.meshes[key];
      if (!m || !m.count) return;
      pass.setPipeline(pipe);
      pass.setVertexBuffer(0, m.vb);
      pass.setIndexBuffer(m.ib, 'uint32');
      pass.drawIndexed(m.count);
      draws++; tris += m.count / 3;
    };
    drawIndexed('main', this.scenePipe);
    drawIndexed('vehicles', this.scenePipe);
    drawIndexed('decal', this.decalPipe);
    // overlay
    pass.setBindGroup(0, this.overlayBG);
    const ov = (key, pipe) => {
      const m = this.meshes[key];
      if (!m || !m.count) return;
      pass.setPipeline(pipe);
      pass.setVertexBuffer(0, m.vb);
      pass.draw(m.count);
      draws++;
    };
    ov('overlayTri', this.overlayTriPipe);
    ov('overlayLine', this.overlayLinePipe);
    pass.end();
    d.queue.submit([enc.finish()]);
    this.stats.drawCalls = draws;
    this.stats.triangles = Math.round(tris);
    const now = performance.now();
    this.stats.frameMs = now - this.lastFrameTime;
    this.lastFrameTime = now;
  }

  computeLightVP(sunDir, target, extent) {
    const eye = [target[0] + sunDir[0] * 400, target[1] + sunDir[1] * 400, target[2] + sunDir[2] * 400];
    const up = Math.abs(sunDir[1]) > 0.95 ? [0, 0, 1] : [0, 1, 0];
    const view = mat4LookAt(eye, target, up);
    const proj = mat4Ortho(-extent, extent, -extent, extent, 1, 900);
    return mat4Multiply(proj, view);
  }
}

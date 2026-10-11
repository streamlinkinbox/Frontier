/* RoadForge — WebGPU renderer: MSAA PBR forward pass, directional shadow map, stencil ground cut-outs, instancing. */
(function (RF) {
  'use strict';
  const M4 = RF.M4, V = RF.V;
  const MAT_SLOT = 256, MAX_MATERIALS = 2048, SHADOW_SIZE = 4096;
  const VERTEX_LAYOUT = [
    { arrayStride: 48, stepMode: 'vertex', attributes: [
      { shaderLocation: 0, offset: 0, format: 'float32x3' }, { shaderLocation: 1, offset: 12, format: 'float32x3' },
      { shaderLocation: 2, offset: 24, format: 'float32x2' }, { shaderLocation: 3, offset: 32, format: 'float32x4' }] },
    { arrayStride: 96, stepMode: 'instance', attributes: [
      { shaderLocation: 4, offset: 0, format: 'float32x4' }, { shaderLocation: 5, offset: 16, format: 'float32x4' },
      { shaderLocation: 6, offset: 32, format: 'float32x4' }, { shaderLocation: 7, offset: 48, format: 'float32x4' },
      { shaderLocation: 8, offset: 64, format: 'float32x4' }, { shaderLocation: 9, offset: 80, format: 'float32x4' }] },
  ];
  // material id enum (matches shader switch)
  const MAT_ID = { asphalt: 0, concrete: 1, paving: 2, curb: 3, paint: 4, grass: 5, ground: 6, metal_galv: 7, metal_paint: 8, carpaint: 9, glass: 10, emissive: 11, wood: 12, ballast: 13, brick: 14, barrier: 15, billboard: 16, rubber: 17, rail: 18, plastic: 19, flat: 20, concrete_road: 21 };

  class Renderer {
    constructor(canvas) { this.canvas = canvas; this.items = new Map(); this.lights = []; this.time = 0; this.frameCount = 0; }
    async init() {
      if (!navigator.gpu) throw new Error('WebGPU is not available in this browser.');
      const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
      if (!adapter) throw new Error('No WebGPU adapter found.');
      this.adapterInfo = adapter.info || {};
      this.device = await adapter.requestDevice({ requiredLimits: { maxBufferSize: Math.min(adapter.limits.maxBufferSize, 1 << 30), maxStorageBufferBindingSize: adapter.limits.maxStorageBufferBindingSize } });
      this.device.lost.then((info) => { console.error('WebGPU device lost', info); if (this.onLost) this.onLost(info); });
      this.ctx = this.canvas.getContext('webgpu');
      this.format = navigator.gpu.getPreferredCanvasFormat();
      this.ctx.configure({ device: this.device, format: this.format, alphaMode: 'opaque' });
      this.sampleCount = 4;
      this.createResources();
      this.createPipelines();
      this.resize();
      return this;
    }
    createResources() {
      const d = this.device;
      this.frameBuf = d.createBuffer({ size: 6 * 64 + 8 * 16 + 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
      this.lightBuf = d.createBuffer({ size: 64 * 32, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
      this.matBuf = d.createBuffer({ size: MAT_SLOT * MAX_MATERIALS, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
      this.matSlots = new Map(); this.nextSlot = 0; this.freeSlots = [];
      this.shadowTex = d.createTexture({ size: [SHADOW_SIZE, SHADOW_SIZE], format: 'depth32float', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING });
      this.shadowView = this.shadowTex.createView();
      this.shadowSampler = d.createSampler({ compare: 'less', magFilter: 'linear', minFilter: 'linear' });
      this.atlasSampler = d.createSampler({ magFilter: 'linear', minFilter: 'linear', mipmapFilter: 'linear', addressModeU: 'repeat', addressModeV: 'repeat' });
      this.setAtlas(null);
      // identity instance buffer
      const id = new Float32Array(24); id.set([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0]);
      this.identityInst = this.makeBuffer(id, GPUBufferUsage.VERTEX);
      this.wireSlot = this.allocSlot(); this.device.queue.writeBuffer(this.matBuf, this.wireSlot * MAT_SLOT, this.materialData({ type: 'flat', color: [0.1, 0.9, 0.5], alpha: 0.55, flags: 1 }));
      this.bgl0 = d.createBindGroupLayout({ entries: [
        { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } },
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } },
        { binding: 2, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'depth' } },
        { binding: 3, visibility: GPUShaderStage.FRAGMENT, sampler: { type: 'comparison' } },
        { binding: 4, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'float' } },
        { binding: 5, visibility: GPUShaderStage.FRAGMENT, sampler: { type: 'filtering' } }] });
      this.bgl1 = d.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: 'uniform', hasDynamicOffset: true, minBindingSize: 80 } }] });
      this.bg1 = d.createBindGroup({ layout: this.bgl1, entries: [{ binding: 0, resource: { buffer: this.matBuf, size: 80 } }] });
      this.layout = d.createPipelineLayout({ bindGroupLayouts: [this.bgl0, this.bgl1] });
      this.makeBG0();
    }
    makeBG0() {
      const mk = (shadow) => this.device.createBindGroup({ layout: this.bgl0, entries: [
        { binding: 0, resource: { buffer: this.frameBuf } }, { binding: 1, resource: { buffer: this.lightBuf } },
        { binding: 2, resource: shadow }, { binding: 3, resource: this.shadowSampler },
        { binding: 4, resource: this.atlasView }, { binding: 5, resource: this.atlasSampler }] });
      if (!this.dummyDepth) this.dummyDepth = this.device.createTexture({ size: [4, 4], format: 'depth32float', usage: GPUTextureUsage.TEXTURE_BINDING }).createView();
      this.bg0 = mk(this.shadowView);
      this.bg0Shadow = mk(this.dummyDepth);
    }
    /** Upload an ImageBitmap/canvas as the billboard atlas (4x4 tiles). */
    setAtlas(source) {
      const d = this.device;
      if (this.atlasTex) this.atlasTex.destroy();
      if (!source) {
        this.atlasTex = d.createTexture({ size: [4, 4], format: 'rgba8unorm', usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
        d.queue.writeTexture({ texture: this.atlasTex }, new Uint8Array(64).fill(128), { bytesPerRow: 16 }, [4, 4]);
      } else {
        this.atlasTex = d.createTexture({ size: [source.width, source.height], format: 'rgba8unorm', usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST | GPUTextureUsage.RENDER_ATTACHMENT });
        d.queue.copyExternalImageToTexture({ source }, { texture: this.atlasTex }, [source.width, source.height]);
      }
      this.atlasView = this.atlasTex.createView();
      if (this.bgl0) this.makeBG0();
    }
    createPipelines() {
      const d = this.device;
      const mod = d.createShaderModule({ code: RF.shaders.main });
      mod.getCompilationInfo().then((info) => { for (const m of info.messages) if (m.type === 'error') console.error('WGSL', m.lineNum, m.message); });
      const skyMod = d.createShaderModule({ code: RF.shaders.sky });
      const depthFormat = 'depth24plus-stencil8';
      const mk = (opts) => d.createRenderPipeline({
        layout: this.layout,
        vertex: { module: mod, entryPoint: 'vs_main', buffers: VERTEX_LAYOUT },
        fragment: { module: mod, entryPoint: 'fs_main', targets: [{ format: this.format, blend: opts.blend || undefined, writeMask: opts.noColor ? 0 : 0xF }] },
        primitive: { topology: opts.topology || 'triangle-list', cullMode: opts.cull || 'back', frontFace: 'ccw' },
        depthStencil: { format: depthFormat, depthWriteEnabled: opts.depthWrite !== false, depthCompare: opts.depthCompare || 'less', depthBias: opts.bias || 0, depthBiasSlopeScale: opts.biasSlope || 0,
          stencilFront: opts.stencil || {}, stencilBack: opts.stencil || {}, stencilReadMask: 0xff, stencilWriteMask: opts.stencilWrite ?? 0xff },
        multisample: { count: this.sampleCount },
      });
      this.pipes = {
        main: mk({}),
        twoSided: mk({ cull: 'none' }),
        decal: mk({ bias: -2, biasSlope: -1.5, depthWrite: false, cull: 'none' }),
        hole: mk({ noColor: true, depthWrite: false, depthCompare: 'always', cull: 'none', stencil: { compare: 'always', passOp: 'replace' } }),
        ground: mk({ stencil: { compare: 'equal', passOp: 'keep' }, stencilWrite: 0, cull: 'none' }),
        groundXray: mk({ cull: 'none', depthWrite: false, blend: { color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha' }, alpha: { srcFactor: 'one', dstFactor: 'zero' } } }),
        overlay: mk({ depthCompare: 'always', depthWrite: false, cull: 'none', blend: { color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha' }, alpha: { srcFactor: 'one', dstFactor: 'zero' } } }),
        overlayDepth: mk({ depthCompare: 'less', depthWrite: false, cull: 'none', blend: { color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha' }, alpha: { srcFactor: 'one', dstFactor: 'zero' } } }),
        wire: mk({ topology: 'line-list', depthCompare: 'less-equal', depthWrite: false, cull: 'none', blend: { color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha' }, alpha: { srcFactor: 'one', dstFactor: 'zero' } } }),
        glass: mk({ cull: 'back', depthWrite: false, blend: { color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha' }, alpha: { srcFactor: 'one', dstFactor: 'zero' } } }),
      };
      this.shadowPipe = d.createRenderPipeline({
        layout: this.layout,
        vertex: { module: mod, entryPoint: 'vs_shadow', buffers: VERTEX_LAYOUT },
        fragment: { module: mod, entryPoint: 'fs_shadow', targets: [] },
        primitive: { topology: 'triangle-list', cullMode: 'none' },
        depthStencil: { format: 'depth32float', depthWriteEnabled: true, depthCompare: 'less', depthBias: 2, depthBiasSlopeScale: 2.5 },
      });
      this.skyPipe = d.createRenderPipeline({
        layout: this.layout,
        vertex: { module: skyMod, entryPoint: 'vs_sky' },
        fragment: { module: skyMod, entryPoint: 'fs_sky', targets: [{ format: this.format }] },
        primitive: { topology: 'triangle-list' },
        depthStencil: { format: depthFormat, depthWriteEnabled: false, depthCompare: 'less-equal' },
        multisample: { count: this.sampleCount },
      });
    }
    resize() {
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      const w = Math.max(1, Math.floor(this.canvas.clientWidth * dpr)), h = Math.max(1, Math.floor(this.canvas.clientHeight * dpr));
      if (this.canvas.width === w && this.canvas.height === h && this.msaaTex) return;
      this.canvas.width = w; this.canvas.height = h;
      if (this.msaaTex) { this.msaaTex.destroy(); this.depthTex.destroy(); }
      this.msaaTex = this.device.createTexture({ size: [w, h], sampleCount: this.sampleCount, format: this.format, usage: GPUTextureUsage.RENDER_ATTACHMENT });
      this.depthTex = this.device.createTexture({ size: [w, h], sampleCount: this.sampleCount, format: 'depth24plus-stencil8', usage: GPUTextureUsage.RENDER_ATTACHMENT });
      this.msaaView = this.msaaTex.createView(); this.depthView = this.depthTex.createView();
    }
    makeBuffer(data, usage) {
      const buf = this.device.createBuffer({ size: Math.max(16, Math.ceil(data.byteLength / 4) * 4), usage: usage | GPUBufferUsage.COPY_DST, mappedAtCreation: true });
      new (data.constructor)(buf.getMappedRange(), 0, data.length).set(data); buf.unmap();
      return buf;
    }
    /** Upload a built mesh {vertices, indices} → GPU mesh. */
    uploadMesh(mesh) {
      if (!mesh.indices.length) return null;
      return { vb: this.makeBuffer(mesh.vertices, GPUBufferUsage.VERTEX), ib: this.makeBuffer(mesh.indices, GPUBufferUsage.INDEX), count: mesh.indices.length, bounds: mesh.bounds, quadCount: mesh.quadCount, triCount: mesh.triCount, vertexCount: mesh.vertexCount, quads: mesh.quads, tris: mesh.tris, eb: null, edgeCount: 0 };
    }
    /** Line-list index buffer of quad/tri edges (topology view). */
    edgeBuffer(gpu) {
      const q = gpu.quads || [], t = gpu.tris || [];
      const n = q.length * 2 + t.length * 2; if (!n) return null;
      const e = new Uint32Array(n); let k = 0;
      for (let i = 0; i < q.length; i += 4) for (let j = 0; j < 4; j++) { e[k++] = q[i + j]; e[k++] = q[i + ((j + 1) & 3)]; }
      for (let i = 0; i < t.length; i += 3) for (let j = 0; j < 3; j++) { e[k++] = t[i + j]; e[k++] = t[i + ((j + 1) % 3)]; }
      gpu.edgeCount = n;
      return this.makeBuffer(e, GPUBufferUsage.INDEX);
    }
    destroyMesh(g) { if (!g) return; g.vb.destroy(); g.ib.destroy(); if (g.eb) g.eb.destroy(); }
    /** Material param packing. */
    materialData(spec) {
      const f = new Float32Array(20); const u = new Uint32Array(f.buffer, 64, 4);
      const base = spec.color || [1, 1, 1]; f[0] = base[0]; f[1] = base[1]; f[2] = base[2]; f[3] = spec.alpha ?? 1;
      f[4] = spec.roughness ?? 0.8; f[5] = spec.metallic ?? 0; f[6] = spec.scale ?? 1; f[7] = spec.wear ?? 0;
      f[8] = spec.cracks ?? 0; f[9] = spec.patches ?? 0; f[10] = spec.wetness ?? 0; f[11] = spec.emissive ?? 0;
      f[12] = spec.laneWidth ?? 0; f[13] = spec.laneOrigin ?? 0; f[14] = spec.pattern ?? 0; f[15] = spec.seed ?? 0;
      u[0] = MAT_ID[spec.type] ?? 0; u[1] = spec.flags ?? 0; u[2] = spec.tile ?? 0; u[3] = spec.selected ? 1 : 0;
      return f;
    }
    allocSlot() { if (this.freeSlots.length) return this.freeSlots.pop(); if (this.nextSlot >= MAX_MATERIALS) { console.warn('material slots exhausted'); return 0; } return this.nextSlot++; }
    /**
     * Add/replace a draw item. item: { id, mesh (built mesh or gpu mesh), material: spec, instances: Float32Array|null, instanceCount, pipeline: 'main'|'decal'|..., castShadow, visible }
     */
    setItem(item) {
      const prev = this.items.get(item.id);
      if (prev) this.removeItem(item.id, true);
      const gpu = item.mesh && item.mesh.vb ? item.mesh : this.uploadMesh(item.mesh);
      if (!gpu) return null;
      const slot = this.allocSlot();
      const rec = { ...item, gpu, slot, instBuf: null, instanceCount: 1, ownsMesh: !(item.mesh && item.mesh.vb) };
      this.device.queue.writeBuffer(this.matBuf, slot * MAT_SLOT, this.materialData(item.material));
      if (item.instances && item.instances.length) { rec.instBuf = this.makeBuffer(item.instances, GPUBufferUsage.VERTEX); rec.instanceCount = item.instances.length / 24; } else { rec.instBuf = this.identityInst; rec.instanceCount = 1; }
      this.items.set(item.id, rec);
      return rec;
    }
    updateMaterial(id, spec) { const rec = this.items.get(id); if (!rec) return; rec.material = spec; this.device.queue.writeBuffer(this.matBuf, rec.slot * MAT_SLOT, this.materialData(spec)); }
    updateInstances(id, instances) {
      const rec = this.items.get(id); if (!rec) return;
      const count = instances.length / 24;
      if (rec.instBuf !== this.identityInst && rec.instBuf.size >= instances.byteLength && count > 0) { this.device.queue.writeBuffer(rec.instBuf, 0, instances); rec.instanceCount = count; return; }
      if (rec.instBuf !== this.identityInst) rec.instBuf.destroy();
      if (count > 0) { rec.instBuf = this.makeBuffer(instances, GPUBufferUsage.VERTEX); rec.instanceCount = count; } else { rec.instBuf = this.identityInst; rec.instanceCount = 0; }
    }
    removeItem(id, keepSharedMesh = false) {
      const rec = this.items.get(id); if (!rec) return;
      if (rec.ownsMesh) this.destroyMesh(rec.gpu);
      if (rec.instBuf && rec.instBuf !== this.identityInst) rec.instBuf.destroy();
      this.freeSlots.push(rec.slot);
      this.items.delete(id);
    }
    removeByPrefix(prefix) { for (const id of [...this.items.keys()]) if (id.startsWith(prefix)) this.removeItem(id); }
    setLights(lights) { this.lights = lights; }

    /** env: { sunDir, sunColor, sunIntensity, night, zenith, horizon, ground, exposure, fog, planMode, wetness } */
    writeFrame(cam, env) {
      const f = new Float32Array(6 * 16 + 8 * 4 + 4);
      const vp = M4.mul(cam.proj, cam.view);
      f.set(vp, 0); f.set(cam.view, 16); f.set(cam.proj, 32); f.set(M4.invert(vp), 48); f.set(this.shadowMat, 64);
      let o = 80;
      f.set([cam.eye[0], cam.eye[1], cam.eye[2], this.time], o); o += 4;
      f.set([env.sunDir[0], env.sunDir[1], env.sunDir[2], env.sunIntensity], o); o += 4;
      f.set([env.sunColor[0], env.sunColor[1], env.sunColor[2], env.night], o); o += 4;
      f.set([env.zenith[0], env.zenith[1], env.zenith[2], env.exposure], o); o += 4;
      f.set([env.horizon[0], env.horizon[1], env.horizon[2], env.fog], o); o += 4;
      f.set([env.ground[0], env.ground[1], env.ground[2], 1 / SHADOW_SIZE], o); o += 4;
      f.set([this.canvas.width, this.canvas.height, env.wetness || 0, env.planMode ? 1 : 0], o); o += 4;
      const u = new Uint32Array(f.buffer, o * 4, 4); u[0] = Math.min(this.lights.length, 64);
      this.device.queue.writeBuffer(this.frameBuf, 0, f);
      const lf = new Float32Array(64 * 8);
      this.lights.slice(0, 64).forEach((l, i) => { lf.set([l.pos[0], l.pos[1], l.pos[2], l.radius, l.color[0], l.color[1], l.color[2], l.directional ? 1 : 0], i * 8); });
      this.device.queue.writeBuffer(this.lightBuf, 0, lf);
    }
    computeShadowMatrix(cam, env) {
      // orthographic light frustum around the camera focus point
      const focus = cam.target, dist = RF.clamp(cam.distance * 1.6, 60, 900);
      const dir = V.norm(env.sunDir);
      const eye = V.madd(focus, dir, dist * 2);
      const view = M4.lookAt(eye, focus, Math.abs(dir[1]) > 0.98 ? [0, 0, 1] : [0, 1, 0]);
      const proj = M4.ortho(-dist, dist, -dist, dist, 1, dist * 4);
      this.shadowMat = M4.mul(proj, view);
    }
    render(cam, env, opts = {}) {
      this.resize();
      this.time += 1 / 60;
      this.computeShadowMatrix(cam, env);
      this.writeFrame(cam, env);
      const d = this.device, enc = d.createCommandEncoder();
      const items = [...this.items.values()].filter((it) => it.visible !== false);
      // decals do not write depth: draw coloured lane surfacing before paint lines so the markings stay on top
      const decalRank = (it) => (it.pipeline === 'decal' ? (/\|surface_/.test(it.id) ? 0 : 1) : 0);
      items.sort((a, b) => decalRank(a) - decalRank(b));
      // shadow pass
      if (env.sunIntensity > 0.01) {
        const sp = enc.beginRenderPass({ colorAttachments: [], depthStencilAttachment: { view: this.shadowView, depthClearValue: 1, depthLoadOp: 'clear', depthStoreOp: 'store' } });
        sp.setPipeline(this.shadowPipe); sp.setBindGroup(0, this.bg0Shadow);
        for (const it of items) {
          if (it.castShadow === false || it.pipeline === 'overlay' || it.pipeline === 'overlayDepth' || it.pipeline === 'hole' || it.pipeline === 'decal') continue;
          sp.setBindGroup(1, this.bg1, [it.slot * MAT_SLOT]);
          sp.setVertexBuffer(0, it.gpu.vb); sp.setVertexBuffer(1, it.instBuf); sp.setIndexBuffer(it.gpu.ib, 'uint32');
          sp.drawIndexed(it.gpu.count, it.instanceCount);
        }
        sp.end();
      }
      const view = this.ctx.getCurrentTexture().createView();
      const pass = enc.beginRenderPass({
        colorAttachments: [{ view: this.msaaView, resolveTarget: view, clearValue: { r: 0.05, g: 0.05, b: 0.06, a: 1 }, loadOp: 'clear', storeOp: 'discard' }],
        depthStencilAttachment: { view: this.depthView, depthClearValue: 1, depthLoadOp: 'clear', depthStoreOp: 'discard', stencilClearValue: 0, stencilLoadOp: 'clear', stencilStoreOp: 'discard' },
      });
      pass.setBindGroup(0, this.bg0);
      const order = ['hole', 'ground', 'main', 'twoSided', 'decal', 'glass', 'overlayDepth', 'overlay'];
      const xray = opts.xray;
      for (const key of order) {
        let pipeKey = key;
        if (key === 'ground' && xray) pipeKey = 'groundXray';
        pass.setPipeline(this.pipes[pipeKey]);
        if (key === 'hole') pass.setStencilReference(1); else if (key === 'ground') pass.setStencilReference(0);
        for (const it of items) {
          if ((it.pipeline || 'main') !== key) continue;
          if (key === 'hole' && xray) continue;
          pass.setBindGroup(1, this.bg1, [it.slot * MAT_SLOT]);
          pass.setVertexBuffer(0, it.gpu.vb); pass.setVertexBuffer(1, it.instBuf); pass.setIndexBuffer(it.gpu.ib, 'uint32');
          pass.drawIndexed(it.gpu.count, it.instanceCount);
        }
        if (key === 'main') { pass.setPipeline(this.skyPipe); pass.draw(3); }
        if (key === 'glass' && opts.wire) {
          pass.setPipeline(this.pipes.wire);
          for (const it of items) {
            if (!['main', 'twoSided', 'ground', 'glass'].includes(it.pipeline || 'main') || it.id.startsWith('veh|') || it.id.startsWith('train|')) continue;
            if (!it.gpu.eb) it.gpu.eb = this.edgeBuffer(it.gpu);
            if (!it.gpu.eb) continue;
            pass.setBindGroup(1, this.bg1, [this.wireSlot * MAT_SLOT]);
            pass.setVertexBuffer(0, it.gpu.vb); pass.setVertexBuffer(1, it.instBuf); pass.setIndexBuffer(it.gpu.eb, 'uint32');
            pass.drawIndexed(it.gpu.edgeCount, it.instanceCount);
          }
        }
      }
      pass.end();
      d.queue.submit([enc.finish()]);
      this.frameCount++;
    }
    stats() { let tris = 0, quads = 0, draws = 0, verts = 0; for (const it of this.items.values()) { if (it.visible === false) continue; draws++; tris += (it.gpu.count / 3) * it.instanceCount; quads += (it.gpu.quadCount || 0) * it.instanceCount; verts += (it.gpu.vertexCount || 0) * it.instanceCount; } return { tris, quads, draws, verts, items: this.items.size }; }
  }

  /* ---------------- Camera ---------------- */
  class Camera {
    constructor() { this.target = [0, 0, 0]; this.distance = 120; this.yaw = -0.7; this.pitch = 0.55; this.fov = (50 * Math.PI) / 180; this.near = 0.3; this.far = 24000; this.aspect = 1; this.mode = 'orbit'; this.eye = [0, 50, 100]; this.view = M4.identity(); this.proj = M4.identity(); this.update(); }
    update() {
      if (this.mode === 'plan') {
        this.eye = [this.target[0], this.target[1] + Math.max(this.distance, 5), this.target[2] + 0.0001];
        this.view = M4.lookAt(this.eye, this.target, [0, 1, 0]);
        const h = this.distance * 0.5, w = h * this.aspect;
        this.proj = M4.ortho(-w, w, -h, h, 1, this.distance * 3 + 500);
      } else {
        const cp = Math.cos(this.pitch);
        this.eye = [this.target[0] + Math.cos(this.yaw) * cp * this.distance, this.target[1] + Math.sin(this.pitch) * this.distance, this.target[2] + Math.sin(this.yaw) * cp * this.distance];
        this.view = M4.lookAt(this.eye, this.target, [0, 1, 0]);
        this.proj = M4.perspective(this.fov, this.aspect, this.near, this.far);
      }
      this.viewProj = M4.mul(this.proj, this.view); this.invViewProj = M4.invert(this.viewProj);
    }
    orbit(dx, dy) { if (this.mode === 'plan') return; this.yaw += dx * 0.005; this.pitch = RF.clamp(this.pitch + dy * 0.005, 0.02, 1.55); this.update(); }
    pan(dx, dy, w, h) {
      // move target in the view plane projected onto the ground plane
      const right = M4.transformDir(M4.invert(this.view), [1, 0, 0]); const fwd = V.normXZ(V.sub(this.target, this.eye));
      const scale = this.mode === 'plan' ? this.distance / h : (2 * Math.tan(this.fov / 2) * this.distance) / h;
      let f = this.mode === 'plan' ? [0, 0, -1] : fwd;
      if (this.mode === 'plan') { f = V.normXZ(M4.transformDir(M4.invert(this.view), [0, 1, 0])); }
      this.target = V.add(this.target, V.add(V.mul(V.normXZ(right), -dx * scale), V.mul(f, dy * scale)));
      this.update();
    }
    zoom(f) { this.distance = RF.clamp(this.distance * f, 2, 3000); this.update(); }
    /** World ray through normalized device coords (x,y in [-1,1]). */
    ray(nx, ny) {
      const near = M4.transformPoint(this.invViewProj, [nx, ny, 0]), far = M4.transformPoint(this.invViewProj, [nx, ny, 1]);
      return { o: near, d: V.norm(V.sub(far, near)) };
    }
    project(p) { const c = M4.transformPoint(this.viewProj, p); return c; }
    focus(target, distance) { this.target = V.clone(target); if (distance) this.distance = distance; this.update(); }
  }

  RF.Renderer = Renderer; RF.Camera = Camera; RF.MAT_ID = MAT_ID;
})(globalThis.RF || (globalThis.RF = {}));

// Minimal "engine" renderer for the demo: HDR targets, shadow map, sky, terrain, instanced meshes
// and tone mapping, with the FluidRenderer slotted in between the opaque and post passes.
import { TERRAIN, MESH, SKY, SHADOW, POST, SELECT } from './shaders.js';
import { mat4, v3 } from '../src/math.js';
import { naturalHeight } from './scene.js';

const INSTANCE_FLOATS = 24;

export class SceneRenderer {
  constructor(device, fluidRenderer, scene, { outputFormat = 'bgra8unorm', width = 1280, height = 720, shadowSize = 2048, terrainUpsample } = {}) {
    this.device = device;
    // render terrain finer than the simulation grid (smooth shorelines, like a real engine terrain)
    this.terrainUpsample = terrainUpsample ?? (scene.dx > 0.4 ? 2 : 1);
    this.fr = fluidRenderer;
    this.scene = scene;
    this.outputFormat = outputFormat;
    this.shadowSize = shadowSize;
    this.exposure = 1.0;
    this.shadows = true;
    this.maxInstances = 256;
    this._createStatic();
    this.resize(width, height);
  }

  _createStatic() {
    const d = this.device, sc = this.scene;
    const VF = GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, F = GPUShaderStage.FRAGMENT;
    // ---- terrain meshes (near: simulated domain bed, far: coarse ring)
    this.near = this._terrainMesh(this._nearTerrainData());
    const f = sc.far;
    this.far = this._terrainMesh({ positions: f.positions, normals: f.normals, masks: f.masks, indices: f.indices });
    // ---- unit meshes
    this.meshes = { box: this._uploadMesh(boxMesh()), cylinder: this._uploadMesh(cylinderMesh(28)), sphere: this._uploadMesh(sphereMesh(20, 14)) };
    this.instanceBuf = d.createBuffer({ size: this.maxInstances * INSTANCE_FLOATS * 4, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
    this.instances = [];

    // ---- layouts
    this.viewBGL = d.createBindGroupLayout({ entries: [{ binding: 0, visibility: VF, buffer: { type: 'uniform' } }] });
    this.viewBG = d.createBindGroup({ layout: this.viewBGL, entries: [{ binding: 0, resource: { buffer: this.fr.viewBuffer } }] });
    this.groundBGL = d.createBindGroupLayout({
      entries: [
        ...this.fr.groundLayoutEntries(0, F),
        { binding: 4, visibility: F, texture: { sampleType: 'depth' } },
        { binding: 5, visibility: F, sampler: { type: 'comparison' } },
      ],
    });
    this.shadowTex = d.createTexture({ size: [this.shadowSize, this.shadowSize], format: 'depth32float', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING });
    this.shadowView = this.shadowTex.createView();
    this.groundBG = d.createBindGroup({
      layout: this.groundBGL,
      entries: [...this.fr.groundBindEntries(0), { binding: 4, resource: this.shadowView }, { binding: 5, resource: this.fr.shadowSampler }],
    });
    const lit = d.createPipelineLayout({ bindGroupLayouts: [this.viewBGL, this.groundBGL] });
    const depth = { format: 'depth32float', depthWriteEnabled: true, depthCompare: 'greater' };
    const tm = d.createShaderModule({ code: TERRAIN, label: 'demo.terrain' });
    const terrainVB = [
      { arrayStride: 12, attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x3' }] },
      { arrayStride: 12, attributes: [{ shaderLocation: 1, offset: 0, format: 'float32x3' }] },
      { arrayStride: 4, attributes: [{ shaderLocation: 2, offset: 0, format: 'unorm8x4' }] },
    ];
    this.terrainPipe = d.createRenderPipeline({
      label: 'demo.terrain', layout: lit,
      vertex: { module: tm, entryPoint: 'vs', buffers: terrainVB },
      fragment: { module: tm, entryPoint: 'fs', targets: [{ format: 'rgba16float' }] },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
      depthStencil: depth,
    });
    const mm = d.createShaderModule({ code: MESH, label: 'demo.mesh' });
    const meshVB = [
      { arrayStride: 24, attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x3' }, { shaderLocation: 1, offset: 12, format: 'float32x3' }] },
      { arrayStride: INSTANCE_FLOATS * 4, stepMode: 'instance', attributes: [0, 1, 2, 3, 4, 5].map((c) => ({ shaderLocation: 2 + c, offset: c * 16, format: 'float32x4' })) },
    ];
    this.meshPipe = d.createRenderPipeline({
      label: 'demo.mesh', layout: lit,
      vertex: { module: mm, entryPoint: 'vs', buffers: meshVB },
      fragment: { module: mm, entryPoint: 'fs', targets: [{ format: 'rgba16float' }] },
      primitive: { topology: 'triangle-list', cullMode: 'back', frontFace: 'ccw' },
      depthStencil: depth,
    });
    const sm = d.createShaderModule({ code: SKY, label: 'demo.sky' });
    this.skyPipe = d.createRenderPipeline({
      label: 'demo.sky', layout: d.createPipelineLayout({ bindGroupLayouts: [this.viewBGL] }),
      vertex: { module: sm, entryPoint: 'vs' },
      fragment: { module: sm, entryPoint: 'fs', targets: [{ format: 'rgba16float' }] },
      depthStencil: { format: 'depth32float', depthWriteEnabled: false, depthCompare: 'always' },
    });
    // ---- shadows
    this.lightBuf = d.createBuffer({ size: 64, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    this.shadowBGL = d.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX, buffer: { type: 'uniform' } }] });
    this.shadowBG = d.createBindGroup({ layout: this.shadowBGL, entries: [{ binding: 0, resource: { buffer: this.lightBuf } }] });
    const shm = d.createShaderModule({ code: SHADOW, label: 'demo.shadow' });
    const sl = d.createPipelineLayout({ bindGroupLayouts: [this.shadowBGL] });
    const sdepth = { format: 'depth32float', depthWriteEnabled: true, depthCompare: 'less', depthBias: 2, depthBiasSlopeScale: 2.5 };
    this.shadowTerrainPipe = d.createRenderPipeline({
      label: 'demo.shadowTerrain', layout: sl,
      vertex: { module: shm, entryPoint: 'vsTerrain', buffers: [terrainVB[0]] },
      primitive: { topology: 'triangle-list', cullMode: 'none' }, depthStencil: sdepth,
    });
    this.shadowMeshPipe = d.createRenderPipeline({
      label: 'demo.shadowMesh', layout: sl,
      vertex: { module: shm, entryPoint: 'vsMesh', buffers: [{ arrayStride: 24, attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x3' }] }, meshVB[1]] },
      primitive: { topology: 'triangle-list', cullMode: 'none' }, depthStencil: sdepth,
    });
    // ---- editor selection mask (outlined in the post pass)
    const selm = d.createShaderModule({ code: SELECT, label: 'demo.select' });
    const selLayout = d.createPipelineLayout({ bindGroupLayouts: [this.viewBGL] });
    const maxBlend = { color: { operation: 'max', srcFactor: 'one', dstFactor: 'one' }, alpha: { operation: 'max', srcFactor: 'one', dstFactor: 'one' } };
    const selPipe = (entryPoint, depthCompare) => d.createRenderPipeline({
      label: 'demo.' + entryPoint, layout: selLayout,
      vertex: { module: selm, entryPoint: 'vs', buffers: [{ arrayStride: 24, attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x3' }] }, meshVB[1]] },
      fragment: { module: selm, entryPoint, targets: [{ format: 'rg8unorm', blend: maxBlend }] },
      primitive: { topology: 'triangle-list', cullMode: 'back', frontFace: 'ccw' },
      // a small bias toward the camera so the visible pass re-hits the depth the opaque pass wrote
      depthStencil: { format: 'depth32float', depthWriteEnabled: false, depthCompare, depthBias: depthCompare === 'always' ? 0 : 16, depthBiasSlopeScale: depthCompare === 'always' ? 0 : 1 },
    });
    this.selHiddenPipe = selPipe('fsHidden', 'always');
    this.selVisiblePipe = selPipe('fsVisible', 'greater-equal');
    // ---- post
    this.postBuf = d.createBuffer({ size: 32, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    this.postBGL = d.createBindGroupLayout({ entries: [
      { binding: 0, visibility: F, texture: { sampleType: 'unfilterable-float' } },
      { binding: 1, visibility: F, buffer: { type: 'uniform' } },
      { binding: 2, visibility: F, texture: { sampleType: 'float' } },
    ] });
    const pm = d.createShaderModule({ code: POST, label: 'demo.post' });
    this.postPipe = d.createRenderPipeline({
      label: 'demo.post', layout: d.createPipelineLayout({ bindGroupLayouts: [this.postBGL] }),
      vertex: { module: pm, entryPoint: 'vs' },
      fragment: { module: pm, entryPoint: 'fs', targets: [{ format: this.outputFormat }] },
    });
  }

  // Near terrain: one vertex per simulation cell centre, plus a ring of vertices exactly on the
  // domain boundary (matching the far terrain's edge) and a skirt, so no gaps open between the two.
  _nearTerrainData() {
    const sc = this.scene;
    const { nx, ny, dx, origin, size, bed, mask } = sc;
    const u = this.terrainUpsample;
    const fx = nx * u, fz = ny * u, fd = dx / u;
    const W = fx + 2, Hn = fz + 2;
    const xs = (I) => (I === 0 ? origin[0] : I === fx + 1 ? origin[0] + size : origin[0] + (I - 0.5) * fd);
    const zs = (J) => (J === 0 ? origin[1] : J === fz + 1 ? origin[1] + size : origin[1] + (J - 0.5) * fd);
    // far terrain edge: naturalHeight - 0.05 sampled every 10 m, linear in between
    const edge = (x, z, alongX) => {
      const cell = 10;
      const t = alongX ? (x - origin[0]) / cell : (z - origin[1]) / cell;
      const i0 = Math.floor(t), f = t - i0;
      const p = (k) => (alongX ? naturalHeight(origin[0] + k * cell, z) : naturalHeight(x, origin[1] + k * cell)) - 0.05;
      return p(i0) + (p(i0 + 1) - p(i0)) * f;
    };
    const B = (i, j) => bed[Math.min(ny - 1, Math.max(0, j)) * nx + Math.min(nx - 1, Math.max(0, i))];
    const cr = (a, b, c, d, t) => b + 0.5 * t * (c - a + t * (2 * a - 5 * b + 4 * c - d + t * (3 * (b - c) + d - a)));
    const bicubic = (x, z) => {
      const gx = (x - origin[0]) / dx - 0.5, gz = (z - origin[1]) / dx - 0.5;
      const i = Math.floor(gx), j = Math.floor(gz), tx = gx - i, tz = gz - j;
      const row = (jj) => cr(B(i - 1, jj), B(i, jj), B(i + 1, jj), B(i + 2, jj), tx);
      return cr(row(j - 1), row(j), row(j + 1), row(j + 2), tz);
    };
    const hgt = new Float32Array(W * Hn);
    for (let J = 0; J < Hn; J++) for (let I = 0; I < W; I++) {
      const x = xs(I), z = zs(J);
      let h;
      if (I > 0 && I <= fx && J > 0 && J <= fz) h = u === 1 ? bed[(J - 1) * nx + (I - 1)] : bicubic(x, z);
      else if (J === 0 || J === fz + 1) h = edge(x, z, true);
      else h = edge(x, z, false);
      hgt[J * W + I] = h;
    }
    const cellOf = (I, J) => {
      const ci = Math.min(nx - 1, Math.max(0, Math.floor((xs(I) - origin[0]) / dx)));
      const cj = Math.min(ny - 1, Math.max(0, Math.floor((zs(J) - origin[1]) / dx)));
      return cj * nx + ci;
    };
    const nv = W * Hn, ns = 2 * (W + Hn) * 2;
    const positions = new Float32Array((nv + ns) * 3), normals = new Float32Array((nv + ns) * 3), masks = new Uint8Array((nv + ns) * 4);
    for (let J = 0; J < Hn; J++) for (let I = 0; I < W; I++) {
      const k = J * W + I, o = k * 3;
      positions[o] = xs(I); positions[o + 1] = hgt[k]; positions[o + 2] = zs(J);
      const iE = Math.min(W - 1, I + 1), iW = Math.max(0, I - 1), jN = Math.min(Hn - 1, J + 1), jS = Math.max(0, J - 1);
      const gx = (hgt[J * W + iE] - hgt[J * W + iW]) / (xs(iE) - xs(iW));
      const gz = (hgt[jN * W + I] - hgt[jS * W + I]) / (zs(jN) - zs(jS));
      const l = Math.hypot(gx, 1, gz);
      normals[o] = -gx / l; normals[o + 1] = 1 / l; normals[o + 2] = -gz / l;
      const c = cellOf(I, J);
      masks.set(mask.subarray(c * 4, c * 4 + 4), k * 4);
    }
    const idx = [];
    for (let J = 0; J < Hn - 1; J++) for (let I = 0; I < W - 1; I++) {
      const a = J * W + I, b = a + 1, c = a + W, e = c + 1;
      if ((I + J) & 1) idx.push(a, c, b, b, c, e); else idx.push(a, c, e, a, e, b);
    }
    // skirt: duplicate the boundary ring 25 m lower and stitch
    const ring = [];
    for (let I = 0; I < W; I++) ring.push(I);
    for (let J = 1; J < Hn; J++) ring.push(J * W + W - 1);
    for (let I = W - 2; I >= 0; I--) ring.push((Hn - 1) * W + I);
    for (let J = Hn - 2; J > 0; J--) ring.push(J * W);
    let v = nv;
    const first = v;
    for (const r of ring) {
      positions.set([positions[r * 3], positions[r * 3 + 1] - 25, positions[r * 3 + 2]], v * 3);
      normals.set([0, 1, 0], v * 3);
      masks.set(masks.subarray(r * 4, r * 4 + 4), v * 4);
      v++;
    }
    for (let q = 0; q < ring.length; q++) {
      const a = ring[q], b = ring[(q + 1) % ring.length], c = first + q, d = first + ((q + 1) % ring.length);
      idx.push(a, c, d, a, d, b);
    }
    return { positions: positions.subarray(0, v * 3), normals: normals.subarray(0, v * 3), masks: masks.subarray(0, v * 4), indices: new Uint32Array(idx) };
  }

  _terrainMesh({ positions, normals, masks, indices }) {
    const d = this.device;
    const mk = (data, usage) => { const b = d.createBuffer({ size: Math.ceil(data.byteLength / 4) * 4, usage: usage | GPUBufferUsage.COPY_DST }); d.queue.writeBuffer(b, 0, data); return b; };
    return { pos: mk(positions, GPUBufferUsage.VERTEX), nrm: mk(normals, GPUBufferUsage.VERTEX), mask: mk(masks, GPUBufferUsage.VERTEX), idx: mk(indices, GPUBufferUsage.INDEX), count: indices.length };
  }

  _uploadMesh({ vertices, indices }) {
    const d = this.device;
    const vb = d.createBuffer({ size: vertices.byteLength, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
    d.queue.writeBuffer(vb, 0, vertices);
    const ib = d.createBuffer({ size: indices.byteLength, usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST });
    d.queue.writeBuffer(ib, 0, indices);
    return { vb, ib, count: indices.length };
  }

  resize(w, h) {
    this.width = Math.max(1, w | 0); this.height = Math.max(1, h | 0);
    const d = this.device;
    for (const t of [this.hdr, this.hdrCopy, this.depth, this.selMask]) t?.destroy();
    const sz = [this.width, this.height];
    this.hdr = d.createTexture({ label: 'demo.hdr', size: sz, format: 'rgba16float', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC });
    this.hdrCopy = d.createTexture({ label: 'demo.hdrCopy', size: sz, format: 'rgba16float', usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
    this.depth = d.createTexture({ label: 'demo.depth', size: sz, format: 'depth32float', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING });
    this.selMask = d.createTexture({ label: 'demo.selectionMask', size: sz, format: 'rg8unorm', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING });
    this.hdrView = this.hdr.createView(); this.depthView = this.depth.createView(); this.selView = this.selMask.createView();
    this.fr.setSceneTextures(this.hdrCopy.createView(), this.depthView, this.shadowView, [this.width, this.height]);
    this.postBG = d.createBindGroup({ layout: this.postBGL, entries: [{ binding: 0, resource: this.hdrView }, { binding: 1, resource: { buffer: this.postBuf } }, { binding: 2, resource: this.selView }] });
  }

  /** list of { mesh: 'box'|'cylinder'|'sphere', matrix: Float32Array(16), color: [r,g,b], params: [rough, metal, pattern, emissive] } */
  setInstances(list) { this.instances = list.slice(0, this.maxInstances); }

  /** Light matrix for a directional sun shadow around `focus` (texel-snapped against shimmering). */
  lightMatrix(sunDir, focus, radius = 70) {
    const eye = v3.madd(focus, sunDir, 250);
    const up = Math.abs(sunDir[1]) > 0.95 ? [0, 0, 1] : [0, 1, 0];
    const view = mat4.lookAt(eye, focus, up);
    const texel = (2 * radius) / this.shadowSize;
    const fp = mat4.transformPoint(view, focus);
    const sx = fp[0] - Math.round(fp[0] / texel) * texel, sy = fp[1] - Math.round(fp[1] / texel) * texel;
    const proj = mat4.ortho(-radius + sx, radius + sx, -radius + sy, radius + sy, 1, 500);
    return mat4.multiply(proj, view);
  }

  render(encoder, outputView, { lightVP, time = 0, srgbOutput = true }) {
    const d = this.device;
    // instances
    const n = this.instances.length;
    if (n) {
      const data = new Float32Array(n * INSTANCE_FLOATS);
      this.instances.forEach((it, i) => {
        data.set(it.matrix, i * INSTANCE_FLOATS);
        data.set(it.color, i * INSTANCE_FLOATS + 16); data[i * INSTANCE_FLOATS + 19] = it.color.length > 3 ? it.color[3] : 0;
        data.set(it.params ?? [0.5, 0, 0, 0], i * INSTANCE_FLOATS + 20);
      });
      d.queue.writeBuffer(this.instanceBuf, 0, data);
    }
    d.queue.writeBuffer(this.lightBuf, 0, lightVP);
    const selected = this.instances.some((it) => it.params && it.params[2] > 9.5);
    const outlinePx = Math.min(4, Math.max(1.5, this.height / 360));
    d.queue.writeBuffer(this.postBuf, 0, new Float32Array([this.exposure, srgbOutput ? 1 : 0, 0.35, time % 100, selected ? 1 : 0, outlinePx, 0, 0]));

    // caustics light map (sampled by terrain + meshes)
    this.fr.renderCaustics(encoder);

    // shadow map
    {
      const pass = encoder.beginRenderPass({ label: 'demo.shadowPass', colorAttachments: [], depthStencilAttachment: { view: this.shadowView, depthClearValue: 1, depthLoadOp: 'clear', depthStoreOp: 'store' } });
      if (this.shadows) {
        pass.setBindGroup(0, this.shadowBG);
        pass.setPipeline(this.shadowTerrainPipe);
        for (const t of [this.near, this.far]) { pass.setVertexBuffer(0, t.pos); pass.setIndexBuffer(t.idx, 'uint32'); pass.drawIndexed(t.count); }
        this._drawMeshes(pass, this.shadowMeshPipe, true);
      }
      pass.end();
    }

    // opaque
    {
      const pass = encoder.beginRenderPass({
        label: 'demo.opaque',
        colorAttachments: [{ view: this.hdrView, loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 1 } }],
        depthStencilAttachment: { view: this.depthView, depthClearValue: 0, depthLoadOp: 'clear', depthStoreOp: 'store' },
      });
      pass.setBindGroup(0, this.viewBG);
      pass.setPipeline(this.skyPipe);
      pass.draw(3);
      pass.setPipeline(this.terrainPipe);
      pass.setBindGroup(1, this.groundBG);
      for (const t of [this.near, this.far]) {
        pass.setVertexBuffer(0, t.pos); pass.setVertexBuffer(1, t.nrm); pass.setVertexBuffer(2, t.mask);
        pass.setIndexBuffer(t.idx, 'uint32'); pass.drawIndexed(t.count);
      }
      this._drawMeshes(pass, this.meshPipe, false);
      pass.end();
    }
    encoder.copyTextureToTexture({ texture: this.hdr }, { texture: this.hdrCopy }, [this.width, this.height]);

    // editor selection mask: full silhouette (G) + depth-tested visible part (R)
    if (selected) {
      const pass = encoder.beginRenderPass({
        label: 'demo.selection',
        colorAttachments: [{ view: this.selView, loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 0 } }],
        depthStencilAttachment: { view: this.depthView, depthReadOnly: true },
      });
      pass.setBindGroup(0, this.viewBG);
      this._drawMeshes(pass, this.selHiddenPipe, true);
      this._drawMeshes(pass, this.selVisiblePipe, true);
      pass.end();
    }

    // particle foam: splat the foam rafts into screen-space buffers (depth-tested against the opaque scene)
    this.fr.renderFoam(encoder);

    // water + foam + spray particles (read-only depth, sampled for refraction & soft particles)
    {
      const pass = encoder.beginRenderPass({
        label: 'demo.water',
        colorAttachments: [{ view: this.hdrView, loadOp: 'load', storeOp: 'store' }],
        depthStencilAttachment: { view: this.depthView, depthReadOnly: true },
      });
      this.fr.drawWater(pass);
      this.fr.drawFoam(pass);
      this.fr.drawParticles(pass);
      pass.end();
    }

    // tone map
    {
      const pass = encoder.beginRenderPass({ label: 'demo.post', colorAttachments: [{ view: outputView, loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 1 } }] });
      pass.setPipeline(this.postPipe);
      pass.setBindGroup(0, this.postBG);
      pass.draw(3);
      pass.end();
    }
  }

  _drawMeshes(pass, pipe, shadow) {
    if (!this.instances.length) return;
    pass.setPipeline(pipe);
    if (!shadow) { pass.setBindGroup(0, this.viewBG); pass.setBindGroup(1, this.groundBG); }
    // group consecutive instances by mesh
    let start = 0;
    const list = this.instances;
    for (let i = 1; i <= list.length; i++) {
      if (i === list.length || list[i].mesh !== list[start].mesh) {
        const m = this.meshes[list[start].mesh];
        pass.setVertexBuffer(0, m.vb);
        pass.setVertexBuffer(1, this.instanceBuf, start * INSTANCE_FLOATS * 4);
        pass.setIndexBuffer(m.ib, 'uint16');
        pass.drawIndexed(m.count, i - start);
        start = i;
      }
    }
  }
}

// ---- unit meshes (interleaved pos+normal, uint16 indices) -------------------------------------
function boxMesh() {
  const v = [], idx = [];
  const faces = [
    [[1, 0, 0], [0, 1, 0], [0, 0, 1]], [[-1, 0, 0], [0, 1, 0], [0, 0, -1]],
    [[0, 1, 0], [0, 0, 1], [1, 0, 0]], [[0, -1, 0], [0, 0, -1], [1, 0, 0]],
    [[0, 0, 1], [1, 0, 0], [0, 1, 0]], [[0, 0, -1], [-1, 0, 0], [0, 1, 0]],
  ];
  for (const [n, u, w] of faces) {
    // ensure CCW winding seen from outside: (u x w) must point along n
    const c = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]];
    const flip = c[0] * n[0] + c[1] * n[1] + c[2] * n[2] < 0;
    const base = v.length / 6;
    for (const [a, b] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) {
      v.push(0.5 * (n[0] + a * u[0] + b * w[0]), 0.5 * (n[1] + a * u[1] + b * w[1]), 0.5 * (n[2] + a * u[2] + b * w[2]), ...n);
    }
    if (flip) idx.push(base, base + 2, base + 1, base, base + 3, base + 2);
    else idx.push(base, base + 1, base + 2, base, base + 2, base + 3);
  }
  return { vertices: new Float32Array(v), indices: new Uint16Array(idx) };
}
// cylinder along X, radius 0.5, length 1
function cylinderMesh(seg) {
  const v = [], idx = [];
  for (let s = 0; s <= seg; s++) {
    const a = (s / seg) * Math.PI * 2, y = Math.cos(a) * 0.5, z = Math.sin(a) * 0.5;
    v.push(-0.5, y, z, 0, y * 2, z * 2, 0.5, y, z, 0, y * 2, z * 2);
  }
  for (let s = 0; s < seg; s++) { const a = s * 2, b = a + 1, c = a + 2, e = a + 3; idx.push(a, c, b, b, c, e); }
  for (const side of [-1, 1]) {
    const center = v.length / 6;
    v.push(0.5 * side, 0, 0, side, 0, 0);
    for (let s = 0; s <= seg; s++) { const a = (s / seg) * Math.PI * 2; v.push(0.5 * side, Math.cos(a) * 0.5, Math.sin(a) * 0.5, side, 0, 0); }
    for (let s = 0; s < seg; s++) { if (side > 0) idx.push(center, center + 1 + s, center + 2 + s); else idx.push(center, center + 2 + s, center + 1 + s); }
  }
  return { vertices: new Float32Array(v), indices: new Uint16Array(idx) };
}
function sphereMesh(seg, rings) {
  const v = [], idx = [];
  for (let r = 0; r <= rings; r++) {
    const th = (r / rings) * Math.PI;
    for (let s = 0; s <= seg; s++) {
      const ph = (s / seg) * Math.PI * 2;
      const n = [Math.sin(th) * Math.cos(ph), Math.cos(th), Math.sin(th) * Math.sin(ph)];
      v.push(n[0] * 0.5, n[1] * 0.5, n[2] * 0.5, ...n);
    }
  }
  for (let r = 0; r < rings; r++) for (let s = 0; s < seg; s++) {
    const a = r * (seg + 1) + s, b = a + seg + 1;
    idx.push(a, a + 1, b, a + 1, b + 1, b);
  }
  return { vertices: new Float32Array(v), indices: new Uint16Array(idx) };
}

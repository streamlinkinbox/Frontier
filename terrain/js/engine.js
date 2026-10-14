// FRONTIER Terrain engine (WebGPU).
//
// Pipeline per generation:
//   1. Height layer stack (generators, filters, erosion) on an N×N float heightfield.
//      Volumetric primitives (spires/arches) are stamped as 2D footprints BEFORE the first
//      erosion layer, so every 2D algorithm "sees" them.
//   2. Derive maps from the final heightfield: normals, curvature, protrusion, AO, flow,
//      wear/deposition, and the hybrid masks (m0 = where the heightfield stretches, mVox =
//      dilated partition mask).
//   3. Sparse voxel bricks are allocated only where mVox > 0, the unified density field is
//      sampled and polygonised with GPU marching cubes.
//   4. Render: heightmap mesh where mVox < .5, voxel mesh where mVox > .42 (overlap band where
//      both are the identical surface), ocean, sky, shadows, post.
import { GEN_WGSL, BLUR_WGSL, THERMAL_WGSL, FIXED_WGSL, DROPLET_WGSL, FLOW_WGSL, RIVER_APPLY_WGSL, PRIM_WGSL } from './wgsl/height.js';
import { MASK_WGSL, PACK_WGSL, STATS_WGSL } from './wgsl/derive.js';
import { MC_WGSL, MC_ARGS_WGSL } from './wgsl/mesh.js';
import { RENDER_WGSL, POST_WGSL } from './wgsl/render.js';
import { TRI_TABLE } from './mctables.js';
import { HEIGHT_TYPES, VOLUME_TYPES, MATERIAL_TYPES, LEVEL } from './schema.js';
import { mat4, hexToRgb, mulberry32 } from './math.js';

const GS = typeof GPUShaderStage !== 'undefined' ? GPUShaderStage : { VERTEX: 1, FRAGMENT: 2, COMPUTE: 4 };
const GBU = typeof GPUBufferUsage !== 'undefined' ? GPUBufferUsage : {};
const GTU = typeof GPUTextureUsage !== 'undefined' ? GPUTextureUsage : {};
const MAX_PRIMS = 128;
const BRICK_SAMPLES = 17 * 17 * 17;

class Arena {
  constructor() { this.data = new ArrayBuffer(1 << 20); this.used = 0; }
  alloc(bytes) {
    const off = this.used; this.used += Math.ceil(bytes / 256) * 256;
    if (this.used > this.data.byteLength) {
      const n = new ArrayBuffer(this.data.byteLength * 2); new Uint8Array(n).set(new Uint8Array(this.data)); this.data = n;
    }
    return off;
  }
}

export class Engine {
  constructor(canvas, { adapterOptions } = {}) {
    this.canvas = canvas;
    this.adapterOptions = adapterOptions || { powerPreference: 'high-performance' };
    this.state = null;
    this.N = 0;
    this.stats = { genMs: 0, meshMs: 0, voxTris: 0, hmTris: 0, bricks: 0, N: 0, maxTris: 0, overflow: false };
    this.pendingLevel = -1; this.busy = false; this.dragging = false; this.dragLevel = -1; this.lastPreview = false;
    this.camera = null;
    this.time = 0;
    this.shadowDirty = true;
    this.onStats = null;
    this.onError = null;
    this.voxCount = 0;
    this.statsCPU = null;
  }

  // ------------------------------------------------------------------ setup
  async init() {
    const fail = (code, msg) => Object.assign(new Error(msg), { code });
    if (!navigator.gpu) throw fail('no-api', 'This browser does not expose WebGPU (navigator.gpu is missing).');
    // try the GPU first, then any adapter, then the browser's software fallback (e.g. SwiftShader)
    const attempts = this.adapterOptions ? [this.adapterOptions] : [{ powerPreference: 'high-performance' }, { powerPreference: 'low-power' }, {}, { forceFallbackAdapter: true }];
    let adapter = null;
    for (const o of attempts) {
      try { adapter = await navigator.gpu.requestAdapter(o); } catch (e) { console.warn('requestAdapter failed', o, e); }
      if (adapter) break;
    }
    if (!adapter) throw fail('no-adapter', 'The browser supports WebGPU but could not get a GPU adapter.');
    const info = adapter.info || {};
    this.isFallback = !!(adapter.isFallbackAdapter || info.isFallbackAdapter);
    this.adapterLabel = [info.vendor, info.architecture, info.description].filter(Boolean).join(' · ') || 'unknown GPU';
    console.info('WebGPU adapter:', this.adapterLabel, this.isFallback ? '(software fallback)' : '');
    const L = adapter.limits;
    if (L.maxStorageBuffersPerShaderStage < 10 || L.maxComputeInvocationsPerWorkgroup < 64) {
      throw fail('limits', `This GPU's WebGPU limits are too low (storage buffers per stage: ${L.maxStorageBuffersPerShaderStage}, need 10).`);
    }
    this.device = await adapter.requestDevice({
      requiredLimits: {
        maxStorageBufferBindingSize: L.maxStorageBufferBindingSize,
        maxBufferSize: L.maxBufferSize,
        maxStorageBuffersPerShaderStage: Math.min(L.maxStorageBuffersPerShaderStage, 10),
        maxStorageTexturesPerShaderStage: Math.min(L.maxStorageTexturesPerShaderStage, 4),
      },
    }).catch((e) => { throw fail('device', `Could not create a WebGPU device: ${e.message || e}`); });
    if (!this.device) throw fail('device', 'Could not create a WebGPU device.');
    const d = this.device;
    d.lost.then((info) => { console.warn('GPU device lost', info.reason, info.message); this.onError?.(`GPU device lost: ${info.message}`); });
    d.addEventListener?.('uncapturederror', (e) => { console.error(e.error.message); this.onError?.(e.error.message); });
    this.maxBinding = Math.min(d.limits.maxStorageBufferBindingSize, d.limits.maxBufferSize);
    this.maxTris = Math.max(65536, Math.min(Math.floor(this.maxBinding / 96), 2_000_000));
    this.maxBatch = Math.max(64, Math.min(2048, Math.floor(Math.min(this.maxBinding, 128 << 20) / (BRICK_SAMPLES * 4))));

    if (this.canvas) {
      this.context = this.canvas.getContext('webgpu');
      if (!this.context) throw fail('context', 'The canvas could not create a WebGPU context (the browser GPU process may have failed).');
      this.format = navigator.gpu.getPreferredCanvasFormat();
      this.context.configure({ device: d, format: this.format, alphaMode: 'opaque' });
    }
    this._buildPipelines();
    this._buildStatic();
  }

  _layout(entries, vis) {
    return this.device.createBindGroupLayout({
      entries: entries.map((t, i) => {
        if (!t) return null;
        let type = t, v = vis;
        if (typeof t === 'object') { type = t.t; v = t.vis; }
        const e = { binding: i, visibility: v };
        if (type === 'u') e.buffer = { type: 'uniform' };
        else if (type === 'r') e.buffer = { type: 'read-only-storage' };
        else if (type === 'w') e.buffer = { type: 'storage' };
        else if (type === 'tf') e.texture = { sampleType: 'float' };
        else if (type === 'tu') e.texture = { sampleType: 'unfilterable-float' };
        else if (type === 'td') e.texture = { sampleType: 'depth' };
        else if (type === 's') e.sampler = { type: 'filtering' };
        else if (type === 'sc') e.sampler = { type: 'comparison' };
        else if (type.startsWith('st:')) e.storageTexture = { access: 'write-only', format: type.slice(3) };
        return e;
      }).filter(Boolean),
    });
  }
  _cp(code, entry, entries, label) {
    const d = this.device;
    const key = code;
    this._modules = this._modules || new Map();
    if (!this._modules.has(key)) this._modules.set(key, d.createShaderModule({ code, label }));
    const layout = this._layout(entries, GS.COMPUTE);
    const pipeline = d.createComputePipeline({
      label: `${label}:${entry}`,
      layout: d.createPipelineLayout({ bindGroupLayouts: [layout] }),
      compute: { module: this._modules.get(key), entryPoint: entry },
    });
    return { pipeline, layout };
  }

  _buildPipelines() {
    const H = ['u', 'r', 'w'];
    this.P = {
      gen: this._cp(GEN_WGSL, 'main', H, 'gen'),
      blur: this._cp(BLUR_WGSL, 'main', ['u', 'r', 'w', 'r'], 'blur'),
      thermal: this._cp(THERMAL_WGSL, 'main', ['u', 'r', 'w', 'w'], 'thermal'),
      toFixed: this._cp(FIXED_WGSL, 'toFixed', ['u', 'r', 'w', 'w'], 'fixed'),
      fromFixed: this._cp(FIXED_WGSL, 'fromFixed', ['u', 'r', 'w', 'w'], 'fixed'),
      droplet: this._cp(DROPLET_WGSL, 'main', ['u', 'w', 'w', 'w'], 'droplet'),
      flowPrep: this._cp(FLOW_WGSL, 'prep', ['u', 'r', 'w', 'r', 'w', 'r'], 'flow'),
      flowIter: this._cp(FLOW_WGSL, 'iter', ['u', 'r', 'w', 'r', 'w', 'r'], 'flow'),
      flowChannel: this._cp(FLOW_WGSL, 'channel', ['u', 'r', 'w', 'r', 'w', 'r'], 'flow'),
      riverApply: this._cp(RIVER_APPLY_WGSL, 'main', ['u', 'r', 'w', 'r'], 'river'),
      primBase: this._cp(PRIM_WGSL, 'base', ['u', 'r', 'w', 'w'], 'prim'),
      primStamp: this._cp(PRIM_WGSL, 'stamp', ['u', 'r', 'w', 'w'], 'prim'),
      mask: this._cp(MASK_WGSL, 'main', ['u', 'r', 'r', 'w', 'w'], 'mask'),
      pack: this._cp(PACK_WGSL, 'main', ['u', 'r', 'r', 'r', 'r', 'r', 'r', 'r', 'st:r32float', 'st:rgba16float', 'st:rgba16float', 'st:rgba16float'], 'pack'),
      stats: this._cp(STATS_WGSL, 'main', ['u', 'r', 'r', 'r', 'w'], 'stats'),
      mcDensity: this._cp(MC_WGSL, 'densityMain', ['u', 'r', 'w', 'w', 'w', 'r', 'tu', 'tf', 'tf', 'tf', 's', 'u', 'r'], 'mc'),
      mcMain: this._cp(MC_WGSL, 'mcMain', ['u', 'r', 'w', 'w', 'w', 'r', 'tu', 'tf', 'tf', 'tf', 's', 'u', 'r'], 'mc'),
      mcArgs: this._cp(MC_ARGS_WGSL, 'main', ['u', 'w', 'w'], 'mcargs'),
    };
    // render layouts
    const VF = GS.VERTEX | GS.FRAGMENT, FR = GS.FRAGMENT;
    this.renderBGL0 = this._layout(['u', 's', 'tu', 'tf', 'tf', 'tf', 'u', { t: 'r', vis: FR }, { t: 'u', vis: FR }], VF);
    this.renderBGL1 = this._layout(['td', 'sc'], FR);
    this.postBGL = this._layout(['tf', 's', 'u'], FR);
    this.renderModule = this.device.createShaderModule({ code: RENDER_WGSL, label: 'render' });
    this.postModule = this.device.createShaderModule({ code: POST_WGSL, label: 'post' });
    this._renderPipes = new Map();
  }

  _renderPipelines(samples) {
    if (this._renderPipes.has(samples)) return this._renderPipes.get(samples);
    const d = this.device, m = this.renderModule;
    const layout = d.createPipelineLayout({ bindGroupLayouts: [this.renderBGL0, this.renderBGL1] });
    const shadowLayout = d.createPipelineLayout({ bindGroupLayouts: [this.renderBGL0] });
    const ms = { count: samples };
    const color = [{ format: 'rgba16float' }];
    const depth = (write, cmp) => ({ format: 'depth32float', depthWriteEnabled: write, depthCompare: cmp });
    const vox = [{ arrayStride: 32, attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x4' }, { shaderLocation: 1, offset: 16, format: 'float32x4' }] }];
    const voxShadow = [{ arrayStride: 32, attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x3' }] }];
    const mk = (vs, fs, o = {}) => d.createRenderPipeline({
      layout, label: vs,
      vertex: { module: m, entryPoint: vs, buffers: o.buffers || [] },
      fragment: { module: m, entryPoint: fs, targets: o.targets || color },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
      depthStencil: o.depth || depth(true, 'greater'),
      multisample: ms,
    });
    const blend = { color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha', operation: 'add' }, alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' } };
    const p = {
      sky: mk('vsFull', 'fsSky', { depth: depth(false, 'always') }),
      terrain: mk('vsTerrain', 'fsTerrain'),
      voxel: mk('vsVoxel', 'fsVoxel', { buffers: vox }),
      skirt: mk('vsSkirt', 'fsSkirt'),
      ocean: mk('vsOcean', 'fsOcean', { depth: depth(false, 'greater'), targets: [{ format: 'rgba16float', blend }] }),
    };
    if (!this._shadowPipes) {
      const sh = (vs, fs, buffers = []) => d.createRenderPipeline({
        layout: shadowLayout, label: vs,
        vertex: { module: m, entryPoint: vs, buffers },
        fragment: { module: m, entryPoint: fs, targets: [] },
        primitive: { topology: 'triangle-list', cullMode: 'none' },
        depthStencil: { format: 'depth32float', depthWriteEnabled: true, depthCompare: 'less', depthBias: 2, depthBiasSlopeScale: 2.0 },
      });
      this._shadowPipes = { terrain: sh('vsShadowTerrain', 'fsShadowTerrain'), voxel: sh('vsShadowVoxel', 'fsShadowVoxel', voxShadow) };
    }
    if (!this._postPipe && this.format) {
      this._postPipe = d.createRenderPipeline({
        layout: d.createPipelineLayout({ bindGroupLayouts: [this.postBGL] }),
        vertex: { module: this.postModule, entryPoint: 'vs' },
        fragment: { module: this.postModule, entryPoint: 'fs', targets: [{ format: this.format }] },
        primitive: { topology: 'triangle-list' },
      });
    }
    this._renderPipes.set(samples, p);
    return p;
  }

  _buildStatic() {
    const d = this.device;
    this.dummyR = d.createBuffer({ size: 256, usage: GBU.STORAGE });
    this.dummyW = d.createBuffer({ size: 256, usage: GBU.STORAGE });
    this.dummyW2 = d.createBuffer({ size: 256, usage: GBU.STORAGE });
    this.triBuf = d.createBuffer({ size: TRI_TABLE.byteLength, usage: GBU.STORAGE | GBU.COPY_DST });
    d.queue.writeBuffer(this.triBuf, 0, TRI_TABLE);
    this.primBuf = d.createBuffer({ size: MAX_PRIMS * 48, usage: GBU.STORAGE | GBU.COPY_DST });
    this.counterBuf = d.createBuffer({ size: 16, usage: GBU.STORAGE | GBU.COPY_DST | GBU.COPY_SRC });
    this.argsBuf = d.createBuffer({ size: 16, usage: GBU.STORAGE | GBU.INDIRECT | GBU.COPY_DST });
    this.vertBuf = d.createBuffer({ size: this.maxTris * 96, usage: GBU.STORAGE | GBU.VERTEX | GBU.COPY_SRC });
    this.frameBuf = d.createBuffer({ size: 512, usage: GBU.UNIFORM | GBU.COPY_DST });
    this.volBuf = d.createBuffer({ size: 512, usage: GBU.UNIFORM | GBU.COPY_DST });
    this.matBuf = d.createBuffer({ size: 1280, usage: GBU.UNIFORM | GBU.COPY_DST });
    this.postBuf = d.createBuffer({ size: 16, usage: GBU.UNIFORM | GBU.COPY_DST });
    this.mcUniBuf = d.createBuffer({ size: 256 * 64, usage: GBU.UNIFORM | GBU.COPY_DST });
    this.sampLinear = d.createSampler({ magFilter: 'linear', minFilter: 'linear', addressModeU: 'clamp-to-edge', addressModeV: 'clamp-to-edge' });
    this.sampShadow = d.createSampler({ compare: 'less', magFilter: 'linear', minFilter: 'linear' });
    d.queue.writeBuffer(this.argsBuf, 0, new Uint32Array([0, 1, 0, 0]));
    this.brickBuf = null; this.brickCap = 0;
    this.densBuf = d.createBuffer({ size: this.maxBatch * BRICK_SAMPLES * 4, usage: GBU.STORAGE });
    this.shadowSize = 0;
  }

  _ensureRes(N) {
    if (this.N === N && this.R) return;
    const d = this.device;
    if (this.R) for (const v of Object.values(this.R)) v.destroy?.();
    const sz = N * N * 4;
    const buf = (extra = 0) => d.createBuffer({ size: sz, usage: GBU.STORAGE | GBU.COPY_SRC | GBU.COPY_DST | extra });
    const tex = (format) => d.createTexture({ size: [N, N], format, usage: GTU.STORAGE_BINDING | GTU.TEXTURE_BINDING | GTU.COPY_SRC });
    this.R = {
      h0: buf(), h1: buf(), hF: buf(), hBlur: buf(), tmpA: buf(), tmpB: buf(),
      flowA: buf(), flowB: buf(), sBuf: buf(), m0: buf(), mVox: buf(), ha: buf(), wear: buf(), depo: buf(),
      hTex: tex('r32float'), nTex: tex('rgba16float'), aTex: tex('rgba16float'), bTex: tex('rgba16float'),
    };
    this.N = N;
    const M = Math.min(N, 1024);
    if (this.gridM !== M) {
      this.indexBuf?.destroy();
      const idx = new Uint32Array((M - 1) * (M - 1) * 6);
      let k = 0;
      for (let j = 0; j < M - 1; j++) for (let i = 0; i < M - 1; i++) {
        const a = j * M + i, b = a + 1, c = a + M, e = c + 1;
        idx[k++] = a; idx[k++] = c; idx[k++] = b; idx[k++] = b; idx[k++] = c; idx[k++] = e;
      }
      this.indexBuf = d.createBuffer({ size: idx.byteLength, usage: GBU.INDEX | GBU.COPY_DST });
      d.queue.writeBuffer(this.indexBuf, 0, idx);
      this.gridM = M; this.indexCount = idx.length;
    }
    this._renderBG = null;
  }

  // ------------------------------------------------------------------ state + scheduling
  setState(state) { this.state = state; }
  request(level) {
    if (!this.state) return;
    if (this.dragging) this.dragLevel = Math.max(this.dragLevel, level);
    if (level <= LEVEL.RENDER) { this.shadowDirty = true; return; }
    this.pendingLevel = Math.max(this.pendingLevel, level);
    if (!this.busy) this._pump();
  }
  beginDrag() { this.dragging = true; this.dragLevel = -1; }
  endDrag() {
    this.dragging = false;
    if (this.dragLevel > LEVEL.RENDER || this.lastPreview) this.request(Math.max(this.dragLevel, this.lastPreview ? LEVEL.HEIGHT : 0));
    this.dragLevel = -1;
  }
  async _pump() {
    this.busy = true;
    try {
      while (this.pendingLevel > LEVEL.RENDER) {
        const lvl = this.pendingLevel; this.pendingLevel = -1;
        await this._generate(lvl, this.dragging);
      }
    } catch (e) {
      console.error(e); this.onError?.(String(e.message || e));
    }
    this.busy = false;
  }
  whenIdle() {
    return new Promise((res) => { const t = () => (this.busy || this.pendingLevel > 0 ? setTimeout(t, 30) : res()); t(); });
  }

  // ------------------------------------------------------------------ helpers
  _bg(layout, res) {
    return this.device.createBindGroup({
      layout,
      entries: res.map((r, i) => {
        if (r === null || r === undefined) return null;
        if (r instanceof Array) return { binding: i, resource: { buffer: r[0], offset: r[1], size: r[2] } };
        if (r.__view) return { binding: i, resource: r.__view };
        if (typeof GPUBuffer !== 'undefined' && r instanceof GPUBuffer) return { binding: i, resource: { buffer: r } };
        if (r.createView) return { binding: i, resource: r.createView() };
        return { binding: i, resource: r };
      }).filter(Boolean),
    });
  }

  // ------------------------------------------------------------------ generation
  async _generate(level, preview) {
    const st = this.state, d = this.device;
    const t0 = performance.now();
    const fullN = st.world.resolution;
    const N = preview ? Math.max(128, Math.round(fullN / st.world.preview)) : fullN;
    if (N !== this.N || preview !== this.lastPreview) level = LEVEL.HEIGHT;
    if (!this.hasHeight) level = LEVEL.HEIGHT;
    this.lastPreview = preview;
    this._ensureRes(N);
    const R = this.R;
    const S = st.world.size, Hs = st.world.heightScale, texel = S / (N - 1);
    const wg = Math.ceil(N / 8);
    const arena = new Arena();
    const uniBuf = d.createBuffer({ size: 1 << 20, usage: GBU.UNIFORM | GBU.COPY_DST });
    const tempBufs = [uniBuf];

    const genUni = (kind, blend, p, mask, opacity, extra = {}) => {
      const off = arena.alloc(128);
      const u = new Uint32Array(arena.data, off, 4), f = new Float32Array(arena.data, off + 16, 28);
      u[0] = N; u[1] = kind; u[2] = blend; u[3] = mask ? mask.type : 0;
      f.set([S, Hs, texel, st.world.seed], 0);
      f.set(p.slice(0, 16), 4);
      if (mask) f.set([mask.lo, mask.hi, Math.max(mask.soft, 1e-4), mask.invert ? 1 : 0], 20);
      f.set([opacity, mask ? mask.scale : 600, mask ? mask.seed : 1, extra.aux || 0], 24);
      return [uniBuf, off, 128];
    };
    const slots = (layer) => {
      const T = HEIGHT_TYPES[layer.type];
      const p = new Array(16).fill(0);
      for (const q of T.params) if (q.slot !== undefined) {
        let v = layer.p[q.k];
        if (typeof v === 'boolean') v = v ? 1 : 0;
        p[q.slot] = v ?? q.def;
      }
      if (T.kind === 11) { p[1] = 0; }
      return p;
    };
    const P = this.P;
    let enc = d.createCommandEncoder();
    let pass = null;
    const cpass = () => (pass ||= enc.beginComputePass());
    const endPass = () => { if (pass) { pass.end(); pass = null; } };
    const run = (pp, res, x, y = 1) => {
      const ps = cpass(); ps.setPipeline(pp.pipeline); ps.setBindGroup(0, this._bg(pp.layout, res)); ps.dispatchWorkgroups(x, y);
    };
    const run2d = (pp, res) => run(pp, res, wg, wg);
    // Split heavy work into separate submissions (avoids GPU watchdog resets, keeps the page responsive).
    let written = 0, work = 0;
    const flush = async (wait = true) => {
      endPass();
      if (arena.used > written) { d.queue.writeBuffer(uniBuf, written, arena.data, written, arena.used - written); written = arena.used; }
      const tb = performance.now(), full = work > this.submitBudget;
      d.queue.submit([enc.finish()]);
      enc = d.createCommandEncoder();
      work = 0;
      if (wait) {
        await d.queue.onSubmittedWorkDone();
        const ms = Math.max(1, performance.now() - tb);
        if (full) this.submitBudget = Math.min(4e9, Math.max(2e6, this.submitBudget * Math.min(2, Math.max(0.4, 180 / ms))));
      }
    };
    this.submitBudget ||= 4e7; // texel-ops per submission, adapted to the GPU's speed
    const cost = async (c) => { work += c; if (work > this.submitBudget) await flush(); };
    const blurPass = (src, dst, tmp, radiusT, { orig = null, opacity = 1, mask = null, max = false } = {}) => {
      const u1 = genUni(0, 0, [radiusT, 0, 0, max ? 1 : 0], null, 1);
      run2d(P.blur, [u1, src, tmp, this.dummyR]);
      const u2 = genUni(0, 0, [radiusT, 1, orig ? 1 : 0, max ? 1 : 0], mask, opacity);
      run2d(P.blur, [u2, tmp, dst, orig || this.dummyR]);
    };
    const flowAccum = async (h, iters) => {
      const u = genUni(0, 0, [], null, 1);
      run2d(P.flowPrep, [u, h, R.sBuf, this.dummyR, R.flowA, this.dummyR]);
      let a = R.flowA, b = R.flowB;
      for (let i = 0; i < iters; i++) {
        run2d(P.flowIter, [u, h, this.dummyW, a, b, R.sBuf]);
        [a, b] = [b, a];
        await cost(N * N * 9);
      }
      return a;
    };
    const flowIters = Math.round(Math.min(420, Math.max(32, N * 0.4)));

    // ---- primitives (CPU placement, GPU base height + footprint)
    const prims = this._buildPrims(st);
    d.queue.writeBuffer(this.primBuf, 0, prims.data);
    this.primCount = prims.count;
    this.primTop = prims.maxTop;

    if (level >= LEVEL.HEIGHT) {
      endPass();
      enc.clearBuffer(R.h0); enc.clearBuffer(R.wear); enc.clearBuffer(R.depo);
      let cur = R.h0, oth = R.h1;
      const swap = () => { [cur, oth] = [oth, cur]; };
      const layers = st.layers.filter((l) => l.enabled);
      let stampAt = layers.findIndex((l) => HEIGHT_TYPES[l.type].sim);
      if (stampAt < 0) stampAt = layers.length;
      const doStamp = () => {
        if (!prims.count) return;
        const u = genUni(0, 0, [prims.count], null, 1);
        run(P.primBase, [u, cur, this.dummyW, this.primBuf], Math.ceil(prims.count / 64));
        run2d(P.primStamp, [u, cur, oth, this.primBuf]);
        swap();
      };
      for (let li = 0; li <= layers.length; li++) {
        if (li === stampAt) doStamp();
        if (li === layers.length) break;
        const L = layers[li];
        const T = HEIGHT_TYPES[L.type];
        const mask = L.mask.type ? { ...L.mask, lo: L.mask.type === 2 ? L.mask.lo / 90 : L.mask.lo, hi: L.mask.type === 2 ? L.mask.hi / 90 : L.mask.hi, soft: L.mask.type === 2 ? L.mask.soft / 90 : L.mask.soft } : null;
        const op = L.opacity;
        if (!T.pass) {
          run2d(P.gen, [genUni(T.kind, L.blend, slots(L), mask, op), cur, oth]);
          swap();
          await cost(N * N * 12 * 8);
        } else if (T.pass === 'smooth') {
          blurPass(cur, oth, R.tmpA, L.p.radius / texel, { orig: cur, opacity: op, mask });
          swap();
          await cost(N * N * 4 * Math.min(96, L.p.radius / texel));
        } else if (T.pass === 'thermal') {
          const its = Math.round(Math.min(600, Math.max(1, L.p.iterations * N / 512)));
          const u = genUni(0, 0, [L.p.talus, L.p.strength, L.p.hard ?? 0], mask, op);
          for (let i = 0; i < its; i++) { run2d(P.thermal, [u, cur, oth, R.depo]); swap(); await cost(N * N * 10); }
        } else if (T.pass === 'erosion') {
          const u = genUni(0, 0, [], mask, op);
          run2d(P.toFixed, [u, cur, this.dummyW, R.ha]);
          const refT = S / 511;
          const ratio = refT / texel;
          const radiusT = Math.min(4, Math.max(1, L.p.radius / texel));
          let wsum = 0; const Rr = Math.ceil(radiusT);
          for (let j = -Rr; j <= Rr; j++) for (let i = -Rr; i <= Rr; i++) wsum += Math.max(0, radiusT - Math.hypot(i, j));
          const batches = Math.max(1, L.p.batches | 0);
          const total = Math.round(L.p.rain * N * N);
          const per = Math.ceil(total / batches);
          const wgs = Math.ceil(per / 64);
          const wx = Math.min(wgs, 65535), wy = Math.ceil(wgs / 65535);
          for (let b = 0; b < batches; b++) {
            const off = arena.alloc(80);
            const uu = new Uint32Array(arena.data, off, 4), ff = new Float32Array(arena.data, off + 16, 16);
            uu[0] = N; uu[1] = per; uu[2] = b + 1; uu[3] = Math.max(4, Math.round(L.p.lifetime * ratio));
            ff.set([S, Hs, texel, (L.p.seed || 1) + st.world.seed * 131]);
            ff.set([L.p.inertia, L.p.capacity, L.p.erode, L.p.deposit], 4);
            ff.set([1 - Math.pow(1 - L.p.evaporate, 1 / ratio), L.p.gravity, L.p.minSlope, radiusT], 8);
            ff.set([wsum, 10, 0, 0], 12);
            run(P.droplet, [[uniBuf, off, 80], R.ha, R.wear, R.depo], wx, wy);
            await cost(per * uu[3] * (2 * Math.ceil(radiusT) + 1) ** 2 * 0.5);
          }
          run2d(P.fromFixed, [u, cur, oth, R.ha]);
          swap();
        } else if (T.pass === 'rivers') {
          const fl = await flowAccum(cur, flowIters);
          const u = genUni(0, 0, [L.p.depth, L.p.threshold], mask, op);
          run2d(P.flowChannel, [u, cur, this.dummyW, fl, R.tmpA, this.dummyR]);
          blurPass(R.tmpA, R.tmpA, R.tmpB, Math.max(0.5, L.p.width / texel * 0.5));
          run2d(P.riverApply, [u, cur, oth, R.tmpA]);
          swap();
        }
      }
      endPass();
      enc.copyBufferToBuffer(cur, 0, R.hF, 0, N * N * 4);
      this.hasHeight = true;
    }

    // ---- derive
    const hy = st.hybrid, mp = st.maps;
    const voxel = texel * hy.voxel;
    const B = voxel * 16;
    const cols = Math.ceil(S / B);
    const dispMax = this._dispMax(st);
    if (level >= LEVEL.DERIVE) {
      const off = arena.alloc(80);
      const du = new Uint32Array(arena.data, off, 4), df = new Float32Array(arena.data, off + 16, 16);
      du[0] = N; du[1] = prims.count; du[2] = hy.mode; du[3] = cols;
      df.set([S, Hs, texel, st.ocean.level * Hs]);
      df.set([hy.threshold, hy.softness, mp.wearScale, mp.depoScale], 4);
      df.set([mp.aoRadius, mp.protRadius, B, voxel], 8);
      df.set([mp.curvRadius, mp.curvGain, hy.margin + dispMax, mp.flowGain], 12);
      const D = [uniBuf, off, 80];
      blurPass(R.hF, R.hBlur, R.tmpA, mp.protRadius / texel);
      await cost(N * N * 4 * Math.min(96, mp.protRadius / texel));
      const fl = await flowAccum(R.hF, flowIters);
      run2d(P.mask, [D, R.hF, this.primBuf, R.m0, R.tmpA]);
      const rd = Math.max(1, (hy.margin + dispMax) / texel + 2);
      blurPass(R.tmpA, R.tmpA, R.tmpB, rd, { max: true });
      blurPass(R.tmpA, R.mVox, R.tmpB, Math.max(1, rd * 0.35));
      await flush();
      run2d(P.pack, [D, R.hF, R.hBlur, fl, R.wear, R.depo, R.m0, R.mVox, R.hTex, R.nTex, R.aTex, R.bTex]);
      await flush();
      const sw = Math.ceil(cols / 8);
      this.statsBuf?.destroy();
      this.statsBuf = d.createBuffer({ size: Math.max(16, cols * cols * 16), usage: GBU.STORAGE | GBU.COPY_SRC });
      run(P.stats, [D, R.hF, R.mVox, this.primBuf, this.statsBuf], sw, sw);
      endPass();
      const rb = d.createBuffer({ size: cols * cols * 16, usage: GBU.MAP_READ | GBU.COPY_DST });
      enc.copyBufferToBuffer(this.statsBuf, 0, rb, 0, cols * cols * 16);
      await flush(false);
      await rb.mapAsync(GPUMapMode.READ);
      this.statsCPU = { data: new Float32Array(rb.getMappedRange().slice(0)), cols };
      rb.unmap(); rb.destroy();
    } else {
      await flush(false);
    }
    const t1 = performance.now();

    // ---- voxel bricks + marching cubes
    this._writeVolUniform();
    const bricks = this._classify(st, dispMax, voxel, B);
    const nb = bricks.length / 4;
    if (nb > 0) {
      if (this.brickCap < nb) {
        this.brickBuf?.destroy();
        this.brickCap = Math.ceil(nb * 1.5);
        this.brickBuf = d.createBuffer({ size: this.brickCap * 16, usage: GBU.STORAGE | GBU.COPY_DST });
      }
      d.queue.writeBuffer(this.brickBuf, 0, bricks);
    }
    d.queue.writeBuffer(this.counterBuf, 0, new Uint32Array([0, 0, 0, 0]));
    const res = (u) => [u, this.brickBuf || this.dummyR, this.densBuf, this.vertBuf, this.counterBuf, this.triBuf,
      R.hTex, R.nTex, R.aTex, R.bTex, this.sampLinear, this.volBuf, this.primBuf];
    // adaptive batches: brick offset in the MC uniform lets every batch share the brick + density buffers
    let off = 0, slot = 0;
    this.mcBatch = this.mcBatch || 256;
    while (off < nb) {
      const cnt = Math.min(this.mcBatch, this.maxBatch, nb - off);
      const ub = new ArrayBuffer(32);
      new Float32Array(ub, 0, 4).set([0, this.voxY0, 0, voxel]); new Uint32Array(ub, 16, 4).set([cnt, this.maxTris, off, 0]);
      d.queue.writeBuffer(this.mcUniBuf, slot * 256, ub);
      const U = [this.mcUniBuf, slot * 256, 32];
      const p2 = enc.beginComputePass();
      p2.setPipeline(P.mcDensity.pipeline); p2.setBindGroup(0, this._bg(P.mcDensity.layout, res(U))); p2.dispatchWorkgroups(cnt);
      p2.setPipeline(P.mcMain.pipeline); p2.setBindGroup(0, this._bg(P.mcMain.layout, res(U))); p2.dispatchWorkgroups(cnt);
      p2.end();
      const tb = performance.now();
      d.queue.submit([enc.finish()]); enc = d.createCommandEncoder();
      await d.queue.onSubmittedWorkDone();
      const ms = Math.max(1, performance.now() - tb);
      if (cnt === this.mcBatch) this.mcBatch = Math.round(Math.min(this.maxBatch, Math.max(16, this.mcBatch * Math.min(2, Math.max(0.5, 150 / ms)))));
      off += cnt; slot = (slot + 1) % 63;
    }
    {
      const ub = new Uint32Array(8); ub.set([0, this.maxTris, 0, 0], 4);
      d.queue.writeBuffer(this.mcUniBuf, 63 * 256, ub);
      const U = [this.mcUniBuf, 63 * 256, 32];
      const p2 = enc.beginComputePass();
      p2.setPipeline(P.mcArgs.pipeline); p2.setBindGroup(0, this._bg(P.mcArgs.layout, [U, this.counterBuf, this.argsBuf])); p2.dispatchWorkgroups(1);
      p2.end();
    }
    const crb = d.createBuffer({ size: 16, usage: GBU.MAP_READ | GBU.COPY_DST });
    enc.copyBufferToBuffer(this.counterBuf, 0, crb, 0, 16);
    d.queue.submit([enc.finish()]);
    await crb.mapAsync(GPUMapMode.READ);
    const count = new Uint32Array(crb.getMappedRange())[0];
    crb.unmap(); crb.destroy();
    for (const b of tempBufs) b.destroy();

    const t2 = performance.now();
    this.voxCount = Math.min(count, this.maxTris);
    this.stats = {
      N, preview, genMs: t1 - t0, meshMs: t2 - t1, voxTris: this.voxCount, hmTris: (this.gridM - 1) ** 2 * 2,
      bricks: nb, overflow: count > this.maxTris, maxTris: this.maxTris, voxel, level,
    };
    this.shadowDirty = true;
    this._renderBG = null;
    this.onStats?.(this.stats);
  }

  _dispMax(st) {
    let s = 0;
    for (const v of st.volume) {
      if (!v.enabled || VOLUME_TYPES[v.type].prim) continue;
      s += Math.abs(v.p.amp || 0) * (v.type === 'overhang' ? 1 : v.type === 'strata' ? 0.6 : 1);
    }
    return s;
  }

  _classify(st, dispMax, voxel, B) {
    const { data, cols } = this.statsCPU;
    const mode = st.hybrid.mode;
    let gmin = Infinity;
    for (let i = 0; i < cols * cols; i++) gmin = Math.min(gmin, data[i * 4]);
    this.terrainMin = gmin;
    const y0 = Math.floor((gmin - dispMax - 4 * voxel) / B) * B - B;
    this.voxY0 = y0;
    if (mode === 0) return new Int32Array(0);
    const out = [];
    for (let bz = 0; bz < cols; bz++) for (let bx = 0; bx < cols; bx++) {
      const k = (bz * cols + bx) * 4;
      const mn = data[k], mx = data[k + 1], mm = data[k + 2];
      if (mode !== 2 && mm < 0.3) continue;
      const lo = Math.floor((mn - dispMax - 3 * voxel - y0) / B);
      const hi = Math.floor((mx + dispMax + 3 * voxel - y0) / B);
      for (let by = Math.max(0, lo); by <= hi; by++) out.push(bx, by, bz, 0);
    }
    return new Int32Array(out);
  }

  _buildPrims(st) {
    const data = new Float32Array(MAX_PRIMS * 12);
    let count = 0, maxTop = 0;
    const S = st.world.size;
    for (const v of st.volume) {
      if (!v.enabled) continue;
      const T = VOLUME_TYPES[v.type];
      if (!T.prim) continue;
      const p = v.p;
      const rnd = mulberry32(((p.seed || 1) * 9973 + st.world.seed * 7) | 0);
      const cx = p.cx * S, cz = p.cz * S, rad = p.area * S;
      const placed = [];
      const minD = T.prim === 1 ? p.radius * 2.1 : p.span * 0.7;
      for (let tries = 0; tries < p.count * 40 && placed.length < p.count && count < MAX_PRIMS; tries++) {
        const a = rnd() * Math.PI * 2, r = Math.sqrt(rnd()) * rad;
        const x = cx + Math.cos(a) * r, z = cz + Math.sin(a) * r;
        if (x < 0.03 * S || z < 0.03 * S || x > 0.97 * S || z > 0.97 * S) continue;
        if (placed.some(([px, pz]) => Math.hypot(px - x, pz - z) < minD)) continue;
        placed.push([x, z]);
        const o = count * 12;
        if (T.prim === 1) {
          const rr = p.radius * (0.7 + 0.6 * rnd());
          const hh = p.height * (1 - p.hvar * rnd());
          data.set([1, x, z, rr, hh, p.taper, 0, 0, 0, Math.floor(rnd() * 1000), p.noise, p.bulge], o);
          maxTop = Math.max(maxTop, hh + rr);
        } else {
          const span = p.span * (0.8 + 0.4 * rnd());
          data.set([2, x, z, p.thick * (0.8 + 0.4 * rnd()), p.height * (0.8 + 0.4 * rnd()), 0, rnd() * 360, span, 0, Math.floor(rnd() * 1000), p.noise, 0], o);
          maxTop = Math.max(maxTop, p.height * 1.2 + p.thick);
        }
        count++;
      }
    }
    return { data, count, maxTop };
  }

  _writeVolUniform() {
    const st = this.state, hy = st.hybrid;
    const f = new Float32Array(76);
    const layers = st.volume.filter((v) => v.enabled && !VOLUME_TYPES[v.type].prim).slice(0, 8);
    const texel = st.world.size / (this.N - 1);
    f.set([layers.length, this.primCount || 0, hy.smoothK, hy.mode], 0);
    f.set([st.world.size, st.world.heightScale, this.N, texel], 4);
    f.set([texel * hy.voxel, hy.weight[0], Math.max(hy.weight[1], hy.weight[0] + 0.01), this._dispMax(st)], 8);
    layers.forEach((v, i) => {
      const T = VOLUME_TYPES[v.type], p = v.p, o = 12 + i * 8;
      let aw = 0, b = [0, 0, 0];
      if (v.type === 'strata') { aw = p.sharp; b = [p.warp, p.tilt, p.hard]; }
      if (v.type === 'roughness') { aw = p.octaves; b = [p.ridged ? 1 : 0, 0, 0]; }
      if (v.type === 'rills') { b = [p.stretch, p.flow, 0]; }
      if (v.type === 'fractures') { aw = p.width; }
      f.set([T.kind, p.amp, p.scale, aw, ...b, p.seed || 1], o);
    });
    this.device.queue.writeBuffer(this.volBuf, 0, f);
  }

  _writeMaterials() {
    const st = this.state;
    const f = new Float32Array(4 + 8 * 32);
    const mats = st.materials.slice(0, 8);
    f.set([mats.length, 1, 0, 0], 0);
    mats.forEach((mm, i) => {
      const p = mm.p, o = 4 + i * 32;
      const T = MATERIAL_TYPES[mm.type];
      const hl = p.height[0] <= -0.5 ? -1000 : p.height[0], hh = p.height[1] >= 1.6 ? 1000 : p.height[1];
      const sl = p.slope[0] <= 0 ? -1000 : p.slope[0], sh = p.slope[1] >= 90 ? 1000 : p.slope[1];
      f.set([...hexToRgb(p.color), p.rough], o);
      f.set([...hexToRgb(p.color2), T.detail], o + 4);
      f.set([hl, hh, p.hSoft, p.opacity], o + 8);
      f.set([sl, sh, p.sSoft, 0], o + 12);
      f.set([p.curv, p.prot, p.ao, p.flow], o + 16);
      f.set([p.depo, p.wear, p.breakup, p.breakupScale], o + 20);
      f.set([p.detailScale, p.bump, p.contrast, p.varScale], o + 24);
      f.set([mm.enabled ? 1 : 0, 0, 0, 0], o + 28);
    });
    this.device.queue.writeBuffer(this.matBuf, 0, f);
  }

  // ------------------------------------------------------------------ rendering
  _ensureTargets(w, h, samples, shadowSize) {
    const d = this.device;
    if (!this.T || this.T.w !== w || this.T.h !== h || this.T.samples !== samples) {
      if (this.T) { this.T.hdr.destroy(); this.T.msaa?.destroy(); this.T.depth.destroy(); }
      const hdr = d.createTexture({ size: [w, h], format: 'rgba16float', usage: GTU.RENDER_ATTACHMENT | GTU.TEXTURE_BINDING | GTU.COPY_SRC });
      const msaa = samples > 1 ? d.createTexture({ size: [w, h], format: 'rgba16float', sampleCount: samples, usage: GTU.RENDER_ATTACHMENT }) : null;
      const depth = d.createTexture({ size: [w, h], format: 'depth32float', sampleCount: samples, usage: GTU.RENDER_ATTACHMENT });
      this.T = { w, h, samples, hdr, msaa, depth };
      this._postBG = null;
    }
    const ss = Math.max(shadowSize, 16);
    if (this.shadowSize !== ss) {
      this.shadowTex?.destroy();
      this.shadowTex = d.createTexture({ size: [ss, ss], format: 'depth32float', usage: GTU.RENDER_ATTACHMENT | GTU.TEXTURE_BINDING });
      this.shadowSize = ss; this.shadowDirty = true; this._renderBG = null;
    }
  }

  _frameUniform(w, h) {
    const st = this.state, cam = this.camera;
    const S = st.world.size, Hs = st.world.heightScale;
    const aspect = w / h;
    const eye = cam.eye(), tgt = cam.target;
    const view = mat4.lookAt(eye, tgt, [0, 1, 0]);
    const near = Math.max(0.3, cam.distance * 0.002);
    const proj = mat4.perspectiveRevInf(cam.fov, aspect, near);
    // shift the projection centre so the terrain centres in the area not covered by the inspector
    const cssW = this.canvas.clientWidth || w;
    proj[8] = -Math.min(0.5, (this.viewOffsetPx || 0) / cssW);
    const vp = mat4.mul(proj, view);
    const inv = mat4.invert(vp);
    const e = st.env;
    const az = (e.sunAz * Math.PI) / 180, el = (e.sunEl * Math.PI) / 180;
    const sd = [Math.cos(el) * Math.sin(az), Math.sin(el), Math.cos(el) * Math.cos(az)];
    const c = [S / 2, Hs * 0.35, S / 2];
    const Rr = S * 0.75 + Hs;
    const leye = [c[0] + sd[0] * Rr * 2, c[1] + sd[1] * Rr * 2, c[2] + sd[2] * Rr * 2];
    const lview = mat4.lookAt(leye, c, Math.abs(sd[1]) > 0.99 ? [0, 0, 1] : [0, 1, 0]);
    const lvp = mat4.mul(mat4.ortho(-Rr, Rr, -Rr, Rr, 1, Rr * 4.5), lview);
    const oc = st.ocean;
    const f = new Float32Array(96);
    f.set(vp, 0); f.set(inv, 16); f.set(lvp, 32);
    f.set([...eye, this.time], 48);
    f.set([...sd, e.sun], 52);
    f.set([...hexToRgb(e.sunColor), e.exposure], 56);
    f.set([...hexToRgb(e.zenith), (e.fog * e.fog * 3) / S], 60);
    f.set([...hexToRgb(e.horizon), (e.fogFall * 4) / Hs], 64);
    f.set([S, Hs, oc.enabled ? oc.level * Hs : -1e6, this.N], 68);
    f.set([...hexToRgb(oc.shallow), oc.clarity], 72);
    f.set([...hexToRgb(oc.deep), oc.waves], 76);
    f.set([oc.wavelength, oc.foam, oc.wind, (oc.enabled ? 1 : 0) | (oc.infinite ? 2 : 0)], 80);
    f.set([st.view, st.hybrid.showRegions ? 1 : 0, st.render.shadows ? 1 : 0, this.gridM], 84);
    const base = Math.min(this.terrainMin ?? 0, oc.enabled ? oc.level * Hs : 0) - Hs * 0.06;
    f.set([w, h, (2 * Math.tan(cam.fov / 2)) / h, base], 88);
    f.set([e.ambient, e.saturation, 0, st.hybrid.mode], 92);
    return f;
  }

  render(dt = 0) {
    if (!this.R || !this.context || !this.state || !this.camera) return;
    const d = this.device, st = this.state;
    this.time += dt;
    const dpr = Math.min(globalThis.devicePixelRatio || 1, 2);
    const cw = Math.max(2, Math.floor(this.canvas.clientWidth * dpr)), ch = Math.max(2, Math.floor(this.canvas.clientHeight * dpr));
    if (this.canvas.width !== cw || this.canvas.height !== ch) { this.canvas.width = cw; this.canvas.height = ch; }
    const rs = st.render.scale;
    const w = Math.max(2, Math.floor(cw * rs)), h = Math.max(2, Math.floor(ch * rs));
    const samples = st.render.msaa ? 4 : 1;
    this._ensureTargets(w, h, samples, st.render.shadows || 16);
    const pipes = this._renderPipelines(samples);
    d.queue.writeBuffer(this.frameBuf, 0, this._frameUniform(w, h));
    this._writeMaterials();
    this._writeVolUniform();
    d.queue.writeBuffer(this.postBuf, 0, new Float32Array([st.env.exposure, 0.25, this.time, 0]));
    if (!this._renderBG) {
      const R = this.R;
      this._renderBG = this._bg(this.renderBGL0, [this.frameBuf, this.sampLinear, R.hTex, R.nTex, R.aTex, R.bTex, this.volBuf, this.primBuf, this.matBuf]);
      this._renderBG1 = this._bg(this.renderBGL1, [this.shadowTex, this.sampShadow]);
    }
    if (!this._postBG) this._postBG = this._bg(this.postBGL, [this.T.hdr, this.sampLinear, this.postBuf]);

    const enc = d.createCommandEncoder();
    if (st.render.shadows && this.shadowDirty) {
      const sp = enc.beginRenderPass({ colorAttachments: [], depthStencilAttachment: { view: this.shadowTex.createView(), depthClearValue: 1, depthLoadOp: 'clear', depthStoreOp: 'store' } });
      sp.setBindGroup(0, this._renderBG);
      sp.setPipeline(this._shadowPipes.terrain); sp.setIndexBuffer(this.indexBuf, 'uint32'); sp.drawIndexed(this.indexCount);
      if (this.voxCount > 0) { sp.setPipeline(this._shadowPipes.voxel); sp.setVertexBuffer(0, this.vertBuf); sp.drawIndirect(this.argsBuf, 0); }
      sp.end();
      this.shadowDirty = false;
    }
    const T = this.T;
    const rp = enc.beginRenderPass({
      colorAttachments: [{ view: (T.msaa || T.hdr).createView(), resolveTarget: T.msaa ? T.hdr.createView() : undefined, clearValue: [0, 0, 0, 1], loadOp: 'clear', storeOp: T.msaa ? 'discard' : 'store' }],
      depthStencilAttachment: { view: T.depth.createView(), depthClearValue: 0, depthLoadOp: 'clear', depthStoreOp: 'discard' },
    });
    rp.setBindGroup(0, this._renderBG); rp.setBindGroup(1, this._renderBG1);
    rp.setPipeline(pipes.sky); rp.draw(3);
    rp.setPipeline(pipes.terrain); rp.setIndexBuffer(this.indexBuf, 'uint32'); rp.drawIndexed(this.indexCount);
    if (this.voxCount > 0 && st.hybrid.mode > 0) { rp.setPipeline(pipes.voxel); rp.setVertexBuffer(0, this.vertBuf); rp.drawIndirect(this.argsBuf, 0); }
    if (st.render.skirts) { rp.setPipeline(pipes.skirt); rp.draw(4 * (this.gridM - 1) * 6); }
    if (st.ocean.enabled) { rp.setPipeline(pipes.ocean); rp.draw(6); }
    rp.end();
    const pp = enc.beginRenderPass({ colorAttachments: [{ view: this.context.getCurrentTexture().createView(), clearValue: [0, 0, 0, 1], loadOp: 'clear', storeOp: 'store' }] });
    pp.setPipeline(this._postPipe); pp.setBindGroup(0, this._postBG); pp.draw(3);
    pp.end();
    d.queue.submit([enc.finish()]);
  }

  // ------------------------------------------------------------------ readback / export
  async readBuffer(buf, size) {
    const d = this.device;
    const rb = d.createBuffer({ size, usage: GBU.MAP_READ | GBU.COPY_DST });
    const enc = d.createCommandEncoder(); enc.copyBufferToBuffer(buf, 0, rb, 0, size); d.queue.submit([enc.finish()]);
    await rb.mapAsync(GPUMapMode.READ);
    const out = rb.getMappedRange().slice(0); rb.unmap(); rb.destroy();
    return out;
  }
  async readTexture(tex, bpp) {
    const d = this.device, N = this.N;
    const bpr = Math.ceil((N * bpp) / 256) * 256;
    const rb = d.createBuffer({ size: bpr * N, usage: GBU.MAP_READ | GBU.COPY_DST });
    const enc = d.createCommandEncoder();
    enc.copyTextureToBuffer({ texture: tex }, { buffer: rb, bytesPerRow: bpr }, [N, N]);
    d.queue.submit([enc.finish()]);
    await rb.mapAsync(GPUMapMode.READ);
    const src = new Uint8Array(rb.getMappedRange());
    const out = new Uint8Array(N * N * bpp);
    for (let y = 0; y < N; y++) out.set(src.subarray(y * bpr, y * bpr + N * bpp), y * N * bpp);
    rb.unmap(); rb.destroy();
    return out.buffer;
  }
  async readHeight() { return new Float32Array(await this.readBuffer(this.R.hF, this.N * this.N * 4)); }
  // returns interleaved [x y z ao nx ny nz w] per vertex, 3 vertices per triangle
  async readVoxelMesh() {
    if (!this.voxCount) return new Float32Array(0);
    return new Float32Array(await this.readBuffer(this.vertBuf, this.voxCount * 96));
  }
}

// IEEE half -> float
export function halfToFloat(h) {
  const s = (h & 0x8000) >> 15, e = (h & 0x7c00) >> 10, f = h & 0x03ff;
  if (e === 0) return (s ? -1 : 1) * Math.pow(2, -14) * (f / 1024);
  if (e === 31) return f ? NaN : (s ? -1 : 1) * Infinity;
  return (s ? -1 : 1) * Math.pow(2, e - 15) * (1 + f / 1024);
}

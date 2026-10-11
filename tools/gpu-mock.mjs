// A validating mock of the WebGPU API for headless tests. It does not render; it checks the validation rules
// most likely to break a real renderer: bind-group ↔ layout compatibility, attachment/binding conflicts,
// usage flags, format matches between pipelines and passes, buffer bounds and dynamic-offset alignment.
export class GPUValidationError extends Error {}
const fail = (m) => { throw new GPUValidationError(m); };

export const GPUBufferUsage = { MAP_READ: 1, MAP_WRITE: 2, COPY_SRC: 4, COPY_DST: 8, INDEX: 16, VERTEX: 32, UNIFORM: 64, STORAGE: 128, INDIRECT: 256, QUERY_RESOLVE: 512 };
export const GPUTextureUsage = { COPY_SRC: 1, COPY_DST: 2, TEXTURE_BINDING: 4, STORAGE_BINDING: 8, RENDER_ATTACHMENT: 16 };
export const GPUShaderStage = { VERTEX: 1, FRAGMENT: 2, COMPUTE: 4 };
export const GPUMapMode = { READ: 1, WRITE: 2 };

const DEPTH = new Set(["depth16unorm", "depth24plus", "depth24plus-stencil8", "depth32float", "depth32float-stencil8"]);
const BPP = { rgba8unorm: 4, "rgba8unorm-srgb": 4, bgra8unorm: 4, "bgra8unorm-srgb": 4, rgba16float: 8, r32uint: 4, r8unorm: 1, rg16float: 4, rgba32float: 16, depth32float: 4, depth24plus: 4 };
const sampleTypeOf = (fmt) => DEPTH.has(fmt) ? "depth" : fmt === "r32uint" ? "uint" : fmt.endsWith("float") || fmt.includes("unorm") ? "float" : "float";

class Buffer {
  constructor(d, desc) { this.label = desc.label; this.size = desc.size; this.usage = desc.usage; this.mapState = desc.mappedAtCreation ? "mapped" : "unmapped"; this.destroyed = false; if (desc.size % 4 && desc.mappedAtCreation) fail("mappedAtCreation size must be multiple of 4"); }
  async mapAsync(mode, offset = 0, size) { if (!(this.usage & (mode === 1 ? 1 : 2))) fail("mapAsync: buffer lacks MAP usage"); if (this.mapState !== "unmapped") fail("mapAsync on mapped buffer"); this.mapState = "mapped"; }
  getMappedRange(offset = 0, size) { if (this.mapState !== "mapped") fail("getMappedRange on unmapped buffer"); return new ArrayBuffer(size ?? this.size - offset); }
  unmap() { this.mapState = "unmapped"; }
  destroy() { this.destroyed = true; }
}
class TextureView { overlaps(o) { return this.texture === o.texture && this.mip0 < o.mip0 + o.mips && o.mip0 < this.mip0 + this.mips && this.layer0 < o.layer0 + o.layerCount && o.layer0 < this.layer0 + this.layerCount; } constructor(tex, desc = {}) { this.texture = tex; this.desc = desc; const mips = tex.mipLevelCount, layers = tex.size[2]; const b = desc.baseMipLevel || 0, c = desc.mipLevelCount ?? mips - b; this.mip0 = b; this.mips = c; this.layer0 = desc.baseArrayLayer || 0; if (b + c > mips) fail("view mip range out of bounds"); const bl = desc.baseArrayLayer || 0, cl = desc.arrayLayerCount ?? layers - bl; if (bl + cl > layers) fail("view layer range out of bounds"); this.dimension = desc.dimension || (layers > 1 ? "2d-array" : "2d"); this.layerCount = cl; if (this.dimension === "2d" && cl !== 1) fail("2d view must select one layer"); } }
class Texture {
  constructor(desc) { this.label = desc.label; this.size = Array.isArray(desc.size) ? [desc.size[0], desc.size[1] ?? 1, desc.size[2] ?? 1] : [desc.size.width, desc.size.height ?? 1, desc.size.depthOrArrayLayers ?? 1]; this.format = desc.format; this.usage = desc.usage; this.mipLevelCount = desc.mipLevelCount || 1; this.sampleCount = desc.sampleCount || 1; this.destroyed = false; if (!BPP[this.format]) fail("unknown texture format " + this.format); if (this.size[0] < 1 || this.size[1] < 1) fail("texture size must be ≥1"); const maxMips = Math.floor(Math.log2(Math.max(this.size[0], this.size[1]))) + 1; if (this.mipLevelCount > maxMips) fail("too many mip levels"); }
  createView(desc) { if (this.destroyed) fail("createView on destroyed texture"); return new TextureView(this, desc); }
  destroy() { this.destroyed = true; }
}
class Sampler { constructor(desc = {}) { this.desc = desc; this.filtering = ["linear"].includes(desc.magFilter) || ["linear"].includes(desc.minFilter) || desc.mipmapFilter === "linear"; this.comparison = !!desc.compare; if (desc.maxAnisotropy > 1 && !(desc.magFilter === "linear" && desc.minFilter === "linear" && desc.mipmapFilter === "linear")) fail("anisotropy requires all linear filters"); } }
class ShaderModule { constructor(desc) { this.code = desc.code; this.label = desc.label; } async getCompilationInfo() { return { messages: [] }; } }
class BindGroupLayout { constructor(desc) { this.entries = desc.entries; this.label = desc.label; const seen = new Set(); for (const e of desc.entries) { if (seen.has(e.binding)) fail("duplicate binding"); seen.add(e.binding); if (!(e.buffer || e.texture || e.sampler || e.storageTexture)) fail("layout entry needs a resource type"); } } }
class PipelineLayout { constructor(desc) { this.bgls = desc.bindGroupLayouts; for (const l of this.bgls) if (!(l instanceof BindGroupLayout)) fail("pipeline layout entry is not a BindGroupLayout"); } }
class BindGroup {
  constructor(desc) {
    this.layout = desc.layout; this.entries = desc.entries;
    if (!(desc.layout instanceof BindGroupLayout)) fail("bind group layout missing");
    if (desc.entries.length !== desc.layout.entries.length) fail(`bind group has ${desc.entries.length} entries, layout has ${desc.layout.entries.length}`);
    for (const e of desc.entries) {
      const le = desc.layout.entries.find((x) => x.binding === e.binding); if (!le) fail(`binding ${e.binding} not in layout`);
      const r = e.resource;
      if (le.buffer) {
        if (!(r && r.buffer instanceof Buffer)) fail(`binding ${e.binding}: expected buffer`);
        const type = le.buffer.type || "uniform", need = type === "uniform" ? GPUBufferUsage.UNIFORM : GPUBufferUsage.STORAGE;
        if (!(r.buffer.usage & need)) fail(`binding ${e.binding}: buffer lacks ${type} usage`);
        const off = r.offset || 0, size = r.size ?? r.buffer.size - off;
        if (off + size > r.buffer.size) fail(`binding ${e.binding}: range exceeds buffer`);
        if (off % 256) fail(`binding ${e.binding}: buffer offset must be 256-aligned`);
        if (le.buffer.minBindingSize && size < le.buffer.minBindingSize) fail(`binding ${e.binding}: smaller than minBindingSize`);
      } else if (le.texture) {
        if (!(r instanceof TextureView)) fail(`binding ${e.binding}: expected texture view`);
        if (!(r.texture.usage & GPUTextureUsage.TEXTURE_BINDING)) fail(`binding ${e.binding}: texture lacks TEXTURE_BINDING`);
        const st = le.texture.sampleType || "float", actual = sampleTypeOf(r.texture.format);
        const ok = st === actual || (st === "unfilterable-float" && actual === "float") || (st === "unfilterable-float" && actual === "depth") || (st === "float" && actual === "depth" && false);
        if (!ok) fail(`binding ${e.binding}: sampleType ${st} incompatible with ${r.texture.format}`);
        if (st === "float" && r.texture.format === "r32uint") fail("float sampling of uint texture");
        const vd = le.texture.viewDimension || "2d"; if (vd !== r.dimension) fail(`binding ${e.binding}: viewDimension ${vd} ≠ view ${r.dimension}`);
        if (r.desc.mipLevelCount === undefined && r.texture.mipLevelCount > 1 && (r.desc.baseMipLevel || 0) > 0) fail("odd mip view");
      } else if (le.sampler) {
        if (!(r instanceof Sampler)) fail(`binding ${e.binding}: expected sampler`);
        const t = le.sampler.type || "filtering";
        if (t === "comparison" && !r.comparison) fail(`binding ${e.binding}: comparison sampler required`);
        if (t !== "comparison" && r.comparison) fail(`binding ${e.binding}: comparison sampler given to non-comparison binding`);
        if (t === "non-filtering" && r.filtering) fail(`binding ${e.binding}: filtering sampler given to non-filtering binding`);
      }
    }
    this.dynamicCount = desc.layout.entries.filter((x) => x.buffer && x.buffer.hasDynamicOffset).length;
  }
}
class RenderPipeline {
  constructor(desc) {
    if (!(desc.layout instanceof PipelineLayout)) fail("render pipeline needs an explicit PipelineLayout (mock)");
    this.layout = desc.layout; this.desc = desc;
    if (!(desc.vertex.module instanceof ShaderModule)) fail("vertex.module missing");
    if (desc.fragment && !(desc.fragment.module instanceof ShaderModule)) fail("fragment.module missing");
    if (!new RegExp(`fn\\s+${desc.vertex.entryPoint}\\b`).test(desc.vertex.module.code)) fail(`vertex entry ${desc.vertex.entryPoint} not in module`);
    if (desc.fragment && !new RegExp(`fn\\s+${desc.fragment.entryPoint}\\b`).test(desc.fragment.module.code)) fail(`fragment entry ${desc.fragment.entryPoint} not in module`);
    // texture–sampler pair rules (filtering samplers only with float sample type; comparison only with depth)
    for (const stage of [desc.vertex, desc.fragment].filter(Boolean)) {
      const code = stage.module.code;
      const vars = new Map(); for (const mm of code.matchAll(/@group\((\d+)\)\s*@binding\((\d+)\)\s*var(?:<[^>]*>)?\s+(\w+)/g)) vars.set(mm[3], [+mm[1], +mm[2]]);
      for (const mm of code.matchAll(/textureSample(\w*)\(\s*(\w+)\s*,\s*(\w+)/g)) {
        const [kind, tex, samp] = [mm[1], mm[2], mm[3]];
        const tv = vars.get(tex), sv = vars.get(samp); if (!tv || !sv) continue;
        const te = desc.layout.bgls[tv[0]]?.entries.find((e) => e.binding === tv[1]), se = desc.layout.bgls[sv[0]]?.entries.find((e) => e.binding === sv[1]);
        if (!te || !se) fail(`shader uses ${tex}/${samp} but the pipeline layout has no such binding`);
        const st = te.texture?.sampleType || "float", sType = se.sampler?.type || "filtering";
        if (kind.startsWith("Compare") && sType !== "comparison") fail(`${tex}: textureSampleCompare needs a comparison sampler`);
        if (!kind.startsWith("Compare") && sType === "comparison") fail(`${tex}: comparison sampler used with textureSample`);
        if (sType === "filtering" && st !== "float") fail(`${tex} (sampleType ${st}) is sampled with the filtering sampler ${samp}; use a non-filtering sampler or textureLoad`);
      }
    }
    this.targets = desc.fragment ? desc.fragment.targets.map((t) => t && t.format) : [];
    this.depthFormat = desc.depthStencil ? desc.depthStencil.format : null;
    this.vertexBuffers = desc.vertex.buffers || [];
    for (const vb of this.vertexBuffers) { let max = 0; for (const a of vb.attributes) max = Math.max(max, a.offset + 16); if (vb.arrayStride % 4) fail("arrayStride must be multiple of 4"); if (max - 16 >= vb.arrayStride) fail("vertex attribute offset beyond stride"); }
    if (desc.depthStencil && desc.depthStencil.depthBias !== undefined && desc.primitive?.topology?.startsWith("line")) fail("depthBias ignored on lines");
    for (const t of desc.fragment?.targets || []) if (t && t.blend) for (const k of ["color", "alpha"]) if (t.blend[k] && !(t.blend[k].srcFactor && t.blend[k].dstFactor)) fail("blend factors missing");
  }
}
class RenderPass {
  constructor(enc, desc) {
    this.enc = enc; this.ended = false;
    this.colorFormats = desc.colorAttachments.map((a) => a ? a.view.texture.format : null);
    this.depthFormat = desc.depthStencilAttachment ? desc.depthStencilAttachment.view.texture.format : null;
    this.attached = new Set();
    const sizes = new Set();
    for (const a of desc.colorAttachments) if (a) { const t = a.view.texture; if (!(t.usage & GPUTextureUsage.RENDER_ATTACHMENT)) fail("colour attachment lacks RENDER_ATTACHMENT"); if (!["clear", "load"].includes(a.loadOp) || !["store", "discard"].includes(a.storeOp)) fail("attachment needs loadOp/storeOp"); if (a.view.layerCount !== 1) fail("attachment view must be a single layer"); this.attached.add(a.view); sizes.add(t.size[0] + "x" + t.size[1]); }
    const d = desc.depthStencilAttachment;
    if (d) { const t = d.view.texture; if (!DEPTH.has(t.format)) fail("depth attachment is not a depth format"); if (!(t.usage & GPUTextureUsage.RENDER_ATTACHMENT)) fail("depth attachment lacks RENDER_ATTACHMENT"); if (d.view.layerCount !== 1) fail("depth view must be a single layer"); if (d.depthReadOnly !== true && !(d.depthLoadOp && d.depthStoreOp)) fail("depth attachment needs depthLoadOp/depthStoreOp"); this.attached.add(d.view); this.depthReadOnly = !!d.depthReadOnly; sizes.add(t.size[0] + "x" + t.size[1]); }
    if (sizes.size > 1) fail("attachments differ in size: " + [...sizes].join(", "));
    this.pipeline = null; this.groups = []; this.vbs = []; this.ib = null; this.draws = 0;
  }
  setPipeline(p) { if (!(p instanceof RenderPipeline)) fail("setPipeline: not a pipeline"); if (p.targets.length !== this.colorFormats.length || p.targets.some((f, i) => f !== this.colorFormats[i])) fail(`pipeline targets [${p.targets}] ≠ pass [${this.colorFormats}]`); if ((p.depthFormat || null) !== this.depthFormat) fail(`pipeline depth ${p.depthFormat} ≠ pass ${this.depthFormat}`); if (this.depthReadOnly && p.desc.depthStencil?.depthWriteEnabled) fail("depth write in read-only pass"); this.pipeline = p; }
  setBindGroup(i, bg, offsets = []) { if (!(bg instanceof BindGroup)) fail("setBindGroup: not a bind group"); if (offsets.length !== bg.dynamicCount) fail(`bind group ${i}: ${offsets.length} dynamic offsets, expected ${bg.dynamicCount}`); for (const o of offsets) if (o % 256) fail("dynamic offset must be 256-aligned"); for (const e of bg.entries) { const r = e.resource; if (r instanceof TextureView && [...this.attached].some((v) => v.overlaps(r)) && !(DEPTH.has(r.texture.format) && this.depthReadOnly)) fail(`bind group ${i} binding ${e.binding}: texture is both attachment and binding in the same pass`); if (r && r.buffer instanceof Buffer && offsets.length) { const le = bg.layout.entries.find((x) => x.binding === e.binding); if (le.buffer.hasDynamicOffset) { const off = (r.offset || 0) + offsets[0], size = r.size ?? r.buffer.size; if (off + size > r.buffer.size) fail("dynamic offset range exceeds buffer"); } } } this.groups[i] = bg; }
  setVertexBuffer(slot, b, offset = 0) { if (!(b.usage & GPUBufferUsage.VERTEX)) fail("vertex buffer lacks VERTEX usage"); if (b.destroyed) fail("vertex buffer destroyed"); this.vbs[slot] = b; }
  setIndexBuffer(b, fmt) { if (!(b.usage & GPUBufferUsage.INDEX)) fail("index buffer lacks INDEX usage"); if (!["uint16", "uint32"].includes(fmt)) fail("bad index format"); this.ib = { b, fmt }; }
  _check() { if (!this.pipeline) fail("draw without pipeline"); const n = this.pipeline.layout.bgls.length; for (let i = 0; i < n; i++) { if (!this.groups[i]) fail(`draw: bind group ${i} not set`); if (this.groups[i].layout !== this.pipeline.layout.bgls[i]) fail(`draw: bind group ${i} layout does not match pipeline layout`); } for (let i = 0; i < this.pipeline.vertexBuffers.length; i++) if (!this.vbs[i]) fail(`draw: vertex buffer ${i} not set`); }
  draw(n, inst = 1) { this._check(); if (inst > 0 && n > 0) this.draws++; }
  drawIndexed(n, inst = 1) { this._check(); if (!this.ib) fail("drawIndexed without index buffer"); const bytes = this.ib.fmt === "uint32" ? 4 : 2; if (n * bytes > this.ib.b.size) fail("index count exceeds index buffer"); if (inst > 0 && n > 0) this.draws++; }
  setViewport() {} setScissorRect() {}
  end() { if (this.ended) fail("pass ended twice"); this.ended = true; this.enc.passes.push(this); }
}
class CommandEncoder {
  constructor(dev) { this.dev = dev; this.passes = []; this.open = null; this.finished = false; }
  beginRenderPass(desc) { if (this.open && !this.open.ended) fail("previous pass not ended"); this.open = new RenderPass(this, desc); return this.open; }
  copyTextureToTexture(src, dst, size) { if (!(src.texture.usage & GPUTextureUsage.COPY_SRC)) fail("copyTextureToTexture: source lacks COPY_SRC"); if (!(dst.texture.usage & GPUTextureUsage.COPY_DST)) fail("copyTextureToTexture: dest lacks COPY_DST"); if (src.texture.format !== dst.texture.format) fail("copyTextureToTexture: format mismatch"); const s = Array.isArray(size) ? size : [size.width, size.height, size.depthOrArrayLayers || 1]; if (s[0] > src.texture.size[0] || s[1] > src.texture.size[1] || s[0] > dst.texture.size[0] || s[1] > dst.texture.size[1]) fail("copyTextureToTexture: size exceeds textures"); }
  copyTextureToBuffer(src, dst, size) { if (!(src.texture.usage & GPUTextureUsage.COPY_SRC)) fail("copyTextureToBuffer: source lacks COPY_SRC"); if (!(dst.buffer.usage & GPUBufferUsage.COPY_DST)) fail("copyTextureToBuffer: buffer lacks COPY_DST"); if (DEPTH.has(src.texture.format) && src.texture.format !== "depth32float") fail("cannot copy depth24plus"); const s = Array.isArray(size) ? size : [size.width, size.height, size.depthOrArrayLayers || 1]; if (s[1] > 1 && (dst.bytesPerRow || 0) % 256) fail("bytesPerRow must be multiple of 256"); const o = src.origin || [0, 0, 0]; const ox = Array.isArray(o) ? o[0] : o.x, oy = Array.isArray(o) ? o[1] : o.y; if (ox + s[0] > src.texture.size[0] || oy + s[1] > src.texture.size[1] || ox < 0 || oy < 0) fail("copyTextureToBuffer: region out of bounds"); const bytes = (dst.offset || 0) + (s[1] - 1) * (dst.bytesPerRow || 0) + s[0] * BPP[src.texture.format]; if (bytes > dst.buffer.size) fail("copyTextureToBuffer: buffer too small"); }
  copyBufferToBuffer(a, ao, b, bo, n) { if (ao + n > a.size || bo + n > b.size) fail("copyBufferToBuffer out of range"); if (n % 4) fail("copy size must be multiple of 4"); }
  finish() { if (this.open && !this.open.ended) fail("finish with open pass"); this.finished = true; return { encoder: this }; }
}
class Queue {
  constructor(dev) { this.dev = dev; this.submitted = 0; }
  writeBuffer(b, off, data, dataOff = 0, size) { if (!(b instanceof Buffer)) fail("writeBuffer: not a buffer"); if (b.destroyed) fail("writeBuffer: destroyed buffer"); if (!(b.usage & GPUBufferUsage.COPY_DST)) fail("writeBuffer: buffer lacks COPY_DST"); const bytes = size !== undefined ? size * (data.BYTES_PER_ELEMENT || 1) : (data.byteLength - dataOff * (data.BYTES_PER_ELEMENT || 1)); if (off % 4 || bytes % 4) fail(`writeBuffer: offset/size must be multiples of 4 (${off}, ${bytes})`); if (off + bytes > b.size) fail(`writeBuffer: ${off}+${bytes} exceeds buffer size ${b.size}`); }
  writeTexture(dst, data, layout, size) { const t = dst.texture; if (!(t.usage & GPUTextureUsage.COPY_DST)) fail("writeTexture: lacks COPY_DST"); const s = Array.isArray(size) ? size : [size.width, size.height ?? 1, size.depthOrArrayLayers ?? 1]; const o = dst.origin || [0, 0, 0]; const oz = Array.isArray(o) ? o[2] || 0 : o.z || 0; if (oz + s[2] > t.size[2]) fail("writeTexture: layer out of range"); const bpr = layout.bytesPerRow ?? s[0] * BPP[t.format]; const need = (layout.offset || 0) + bpr * ((layout.rowsPerImage ?? s[1]) * (s[2] - 1) + (s[1] - 1)) + s[0] * BPP[t.format]; if (data.byteLength < need) fail(`writeTexture: data ${data.byteLength} < ${need}`); if (bpr < s[0] * BPP[t.format]) fail("writeTexture: bytesPerRow too small"); }
  submit(list) { for (const c of list) { if (!c.encoder?.finished) fail("submit of unfinished encoder"); for (const p of c.encoder.passes) this.dev.stats.draws += p.draws; this.dev.stats.passes += c.encoder.passes.length; } this.submitted++; }
  onSubmittedWorkDone() { return Promise.resolve(); }
}
class Device {
  constructor() { this.queue = new Queue(this); this.stats = { draws: 0, passes: 0 }; this.lost = new Promise(() => {}); this.features = new Set(); this.limits = { maxStorageBufferBindingSize: 128 << 20, maxBufferSize: 256 << 20, maxTextureDimension2D: 8192, maxBindGroups: 4, maxColorAttachments: 8 }; this.label = "mock"; this.created = { buffers: 0, textures: 0, pipelines: 0 }; }
  createBuffer(d) { this.created.buffers++; if (!(d.size > 0)) fail("buffer size must be > 0"); if (d.size > this.limits.maxBufferSize) fail("buffer too big"); return new Buffer(this, d); }
  createTexture(d) { this.created.textures++; return new Texture(d); }
  createSampler(d) { return new Sampler(d); }
  createShaderModule(d) { return new ShaderModule(d); }
  createBindGroupLayout(d) { return new BindGroupLayout(d); }
  createPipelineLayout(d) { return new PipelineLayout(d); }
  createBindGroup(d) { return new BindGroup(d); }
  createRenderPipeline(d) { this.created.pipelines++; return new RenderPipeline(d); }
  async createRenderPipelineAsync(d) { return this.createRenderPipeline(d); }
  createCommandEncoder() { return new CommandEncoder(this); }
  pushErrorScope() {} popErrorScope() { return Promise.resolve(null); }
  addEventListener() {} destroy() {}
}
class Adapter { constructor() { this.features = new Set(); this.limits = { maxStorageBufferBindingSize: 128 << 20, maxBufferSize: 256 << 20, maxTextureDimension2D: 8192 }; this.info = { vendor: "mock", architecture: "", device: "", description: "mock adapter" }; } async requestDevice() { return new Device(); } async requestAdapterInfo() { return this.info; } }
export class CanvasContext { constructor(canvas) { this.canvas = canvas; this.cfg = null; } configure(c) { if (!(c.device instanceof Device)) fail("configure: device missing"); this.cfg = c; } getCurrentTexture() { if (!this.cfg) fail("getCurrentTexture before configure"); return new Texture({ size: [this.canvas.width || 8, this.canvas.height || 8, 1], format: this.cfg.format, usage: GPUTextureUsage.RENDER_ATTACHMENT | (this.cfg.usage || 0) }); } unconfigure() { this.cfg = null; } }
export const gpu = { requestAdapter: async () => new Adapter(), getPreferredCanvasFormat: () => "bgra8unorm" };
export function installGlobals(g = globalThis) {
  Object.defineProperty(g, "navigator", { value: { gpu }, configurable: true });
  Object.assign(g, { GPUBufferUsage, GPUTextureUsage, GPUShaderStage, GPUMapMode });
}

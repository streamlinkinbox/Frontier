// Raw WebGPU renderer (no Three): sky gradient, chunked SDF terrain with
// geology-driven shading + debug modes, and a 3D brush gizmo.
// The terrain fragment shader deliberately does NOT contain cliff "detail
// noise": bed colour, hardness, joint proximity, sediment and moisture are
// baked per-vertex from the volume fields, and relief is geometry produced
// by the erosion processes. The only procedural term is a ±few-% grain hash
// for close-up matte breakup (uniform, can be set to 0).

import { VERT_STRIDE } from './mesher.js';

const TERRAIN_WGSL = /* wgsl */`
struct Uniforms {
  viewProj: mat4x4<f32>,
  camPos: vec3<f32>, mode: f32,
  sunDir: vec3<f32>, time: f32,
  sunColor: vec3<f32>, caveFill: f32,
  fogColor: vec3<f32>, fogDensity: f32,
  params: vec4<f32>, // sedimentStrength, moistureDarken, jointShade, grain
}
@group(0) @binding(0) var<uniform> U: Uniforms;

struct VsOut {
  @builtin(position) clip: vec4<f32>,
  @location(0) world: vec3<f32>,
  @location(1) normal: vec3<f32>,
  @location(2) color: vec3<f32>,
  @location(3) data: vec4<f32>,
}
@vertex fn vs(@location(0) pos: vec3<f32>, @location(1) nrm: vec3<f32>, @location(2) col: vec3<f32>, @location(3) data: vec4<f32>) -> VsOut {
  var o: VsOut;
  o.clip = U.viewProj * vec4<f32>(pos, 1.0);
  o.world = pos; o.normal = nrm; o.color = col; o.data = data;
  return o;
}

fn tonemap(c: vec3<f32>) -> vec3<f32> {
  let x = max(c, vec3<f32>(0.0));
  let aces = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), vec3<f32>(0.0), vec3<f32>(1.0));
  return pow(aces, vec3<f32>(1.0 / 2.2));
}
fn hash3(p: vec3<f32>) -> f32 {
  let q = fract(p * 0.3183099 + vec3<f32>(0.1, 0.17, 0.13));
  let r = q * 17.0;
  return fract(r.x * r.y * r.z * (r.x + r.y + r.z));
}
fn rampHardness(h: f32) -> vec3<f32> {
  // soft (blue) -> mid (green) -> hard (red)
  let a = mix(vec3<f32>(0.15, 0.35, 0.95), vec3<f32>(0.2, 0.85, 0.35), smoothstep(0.0, 0.55, h));
  return mix(a, vec3<f32>(0.95, 0.25, 0.15), smoothstep(0.55, 1.0, h));
}

@fragment fn fs(in: VsOut) -> @location(0) vec4<f32> {
  let n = normalize(in.normal);
  let viewDir = normalize(U.camPos - in.world);
  let hardness = in.data.x;
  let joint = in.data.y;
  let sediment = clamp(in.data.z, 0.0, 1.5);
  let moisture = clamp(in.data.w, 0.0, 1.0);
  let mode = i32(U.mode + 0.5);

  if (mode == 1) { // strata (flat bed colour, mild orientation cue)
    let s = 0.75 + 0.25 * max(n.y, 0.0);
    return vec4<f32>(tonemap(in.color * s * 1.15), 1.0);
  }
  if (mode == 2) { return vec4<f32>(tonemap(rampHardness(hardness) * (0.45 + 0.55 * max(dot(n, normalize(U.sunDir)), 0.0))), 1.0); }
  if (mode == 3) { return vec4<f32>(tonemap(vec3<f32>(0.06) + vec3<f32>(1.0, 0.92, 0.75) * joint), 1.0); }
  if (mode == 4) { return vec4<f32>(tonemap(mix(vec3<f32>(0.08), vec3<f32>(0.1, 0.45, 1.0), moisture)), 1.0); }
  if (mode == 5) { return vec4<f32>(tonemap(mix(vec3<f32>(0.08), vec3<f32>(0.72, 0.5, 0.28), clamp(sediment, 0.0, 1.0))), 1.0); }
  if (mode == 6) { // vegetation suitability approximation (exact map is computed on CPU)
    let slopeF = 1.0 - smoothstep(0.25, 0.65, 1.0 - n.y);
    let suit = clamp(slopeF * (0.25 + 0.75 * moisture) * (0.3 + 0.7 * clamp(sediment * 1.4, 0.0, 1.0)) * 1.7, 0.0, 1.0);
    return vec4<f32>(tonemap(mix(vec3<f32>(0.07), vec3<f32>(0.25, 0.9, 0.3), suit)), 1.0);
  }
  if (mode == 7) { return vec4<f32>(tonemap(n * 0.5 + 0.5), 1.0); }

  // ---- shaded
  var albedo = in.color;
  let upFacing = smoothstep(0.05, 0.75, n.y);
  let soilMix = clamp(sediment * U.params.x, 0.0, 1.0) * (0.3 + 0.7 * upFacing);
  albedo = mix(albedo, vec3<f32>(0.115, 0.082, 0.052), soilMix); // linear-space soil (renderer gamma-encodes later)
  albedo *= (1.0 - U.params.y * moisture * 0.38);            // wet darkening
  albedo *= (1.0 - U.params.z * joint * 0.32);               // fracture seams (geometry does the heavy lifting)
  albedo *= (1.0 + (hash3(floor(in.world * 5.0)) - 0.5) * U.params.w);

  let sunDir = normalize(U.sunDir);
  let ndl = dot(n, sunDir);
  let diffuse = clamp((ndl + 0.18) / 1.18, 0.0, 1.0);        // slight wrap so faces read in an editor
  let hemi = mix(vec3<f32>(0.20, 0.17, 0.14), vec3<f32>(0.42, 0.52, 0.68), n.y * 0.5 + 0.5);
  var light = hemi * 0.55 + U.sunColor * diffuse;
  // camera fill so cave interiors stay editable (editor aid, user-adjustable)
  let dist = length(U.camPos - in.world);
  let fill = U.caveFill * max(dot(n, viewDir), 0.0) / (1.0 + dist * 0.045);
  light += vec3<f32>(0.75, 0.82, 1.0) * fill;

  var col = albedo * light;
  let halfDir = normalize(sunDir + viewDir);
  let spec = pow(max(dot(n, halfDir), 0.0), mix(10.0, 42.0, hardness)) * mix(0.015, 0.08, hardness) * max(ndl, 0.0);
  col += U.sunColor * spec;

  let fogF = 1.0 - exp(-U.fogDensity * U.fogDensity * dist * dist);
  col = mix(col, U.fogColor, clamp(fogF, 0.0, 1.0));
  return vec4<f32>(tonemap(col), 1.0);
}`;

const SKY_WGSL = /* wgsl */`
struct Sky { right: vec3<f32>, tanX: f32, up: vec3<f32>, tanY: f32, forward: vec3<f32>, time: f32, sunDir: vec3<f32>, pad: f32 }
@group(0) @binding(0) var<uniform> S: Sky;
struct Out { @builtin(position) clip: vec4<f32>, @location(0) ndc: vec2<f32> }
@vertex fn vs(@builtin(vertex_index) vi: u32) -> Out {
  var o: Out;
  let x = f32(i32(vi & 1u) * 4 - 1);
  let y = f32(i32((vi >> 1u) & 1u) * 4 - 1);
  o.clip = vec4<f32>(x, y, 1.0, 1.0);
  o.ndc = vec2<f32>(x, y);
  return o;
}
@fragment fn fs(in: Out) -> @location(0) vec4<f32> {
  let dir = normalize(S.forward + S.right * (in.ndc.x * S.tanX) + S.up * (in.ndc.y * S.tanY));
  let h = clamp(dir.y, -1.0, 1.0);
  var col = mix(vec3<f32>(0.55, 0.62, 0.72), vec3<f32>(0.16, 0.32, 0.62), smoothstep(0.0, 0.55, h));
  col = mix(vec3<f32>(0.32, 0.30, 0.28), col, smoothstep(-0.25, 0.02, h));
  let s = max(dot(dir, normalize(S.sunDir)), 0.0);
  col += vec3<f32>(1.0, 0.92, 0.78) * (pow(s, 350.0) * 3.0 + pow(s, 18.0) * 0.22);
  let x = max(col, vec3<f32>(0.0));
  let aces = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), vec3<f32>(0.0), vec3<f32>(1.0));
  return vec4<f32>(pow(aces, vec3<f32>(1.0 / 2.2)), 1.0);
}`;

const LINE_WGSL = /* wgsl */`
struct LineU { viewProj: mat4x4<f32>, color: vec4<f32> }
@group(0) @binding(0) var<uniform> U: LineU;
@vertex fn vs(@location(0) pos: vec3<f32>) -> @builtin(position) vec4<f32> { return U.viewProj * vec4<f32>(pos, 1.0); }
@fragment fn fs() -> @location(0) vec4<f32> { return U.color; }`;

export class Renderer {
  constructor(canvas) {
    this.canvas = canvas;
    this.chunks = new Map();
    this.settings = {
      mode: 0, sunAzimuth: 135, sunElevation: 42, sunIntensity: 1.65,
      caveFill: 0.55, fogDensity: 0.0016, sedimentStrength: 1.0,
      moistureDarken: 0.8, jointShade: 0.8, grain: 0.05,
    };
    this.time = 0;
  }

  sunDirection() {
    const az = this.settings.sunAzimuth * Math.PI / 180;
    const el = this.settings.sunElevation * Math.PI / 180;
    return [Math.cos(el) * Math.cos(az), Math.sin(el), Math.cos(el) * Math.sin(az)];
  }

  async init() {
    if (!navigator.gpu) throw new Error('WebGPU is not available in this browser (navigator.gpu missing). Use a current Chrome/Edge.');
    const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
    if (!adapter) throw new Error('WebGPU adapter request failed.');
    this.device = await adapter.requestDevice();
    this.context = this.canvas.getContext('webgpu');
    this.format = navigator.gpu.getPreferredCanvasFormat();
    this.context.configure({ device: this.device, format: this.format, alphaMode: 'opaque' });
    const d = this.device;

    this.uniformBuffer = d.createBuffer({ size: 144, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, label: 'terrain.uniforms' });
    this.skyUniformBuffer = d.createBuffer({ size: 64, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, label: 'sky.uniforms' });
    this.lineUniformBuffer = d.createBuffer({ size: 80, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, label: 'line.uniforms' });
    const mkLayout = (frag = true) => d.createBindGroupLayout({
      entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX | (frag ? GPUShaderStage.FRAGMENT : 0), buffer: { type: 'uniform' } }],
    });
    this.terrainLayout = mkLayout(true);
    this.skyLayout = mkLayout(true);
    this.lineLayout = mkLayout(true);
    this.terrainBind = d.createBindGroup({ layout: this.terrainLayout, entries: [{ binding: 0, resource: { buffer: this.uniformBuffer } }] });
    this.skyBind = d.createBindGroup({ layout: this.skyLayout, entries: [{ binding: 0, resource: { buffer: this.skyUniformBuffer } }] });
    this.lineBind = d.createBindGroup({ layout: this.lineLayout, entries: [{ binding: 0, resource: { buffer: this.lineUniformBuffer } }] });

    const terrainModule = d.createShaderModule({ code: TERRAIN_WGSL, label: 'terrain' });
    this.terrainPipeline = d.createRenderPipeline({
      layout: d.createPipelineLayout({ bindGroupLayouts: [this.terrainLayout] }),
      vertex: {
        module: terrainModule, entryPoint: 'vs',
        buffers: [{
          arrayStride: VERT_STRIDE * 4,
          attributes: [
            { shaderLocation: 0, offset: 0, format: 'float32x3' },
            { shaderLocation: 1, offset: 12, format: 'float32x3' },
            { shaderLocation: 2, offset: 24, format: 'float32x3' },
            { shaderLocation: 3, offset: 36, format: 'float32x4' },
          ],
        }],
      },
      fragment: { module: terrainModule, entryPoint: 'fs', targets: [{ format: this.format }] },
      primitive: { topology: 'triangle-list', cullMode: 'back' },
      depthStencil: { format: 'depth24plus', depthWriteEnabled: true, depthCompare: 'less' },
    });

    const skyModule = d.createShaderModule({ code: SKY_WGSL, label: 'sky' });
    this.skyPipeline = d.createRenderPipeline({
      layout: d.createPipelineLayout({ bindGroupLayouts: [this.skyLayout] }),
      vertex: { module: skyModule, entryPoint: 'vs' },
      fragment: { module: skyModule, entryPoint: 'fs', targets: [{ format: this.format }] },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
      depthStencil: { format: 'depth24plus', depthWriteEnabled: false, depthCompare: 'always' },
    });

    const lineModule = d.createShaderModule({ code: LINE_WGSL, label: 'lines' });
    this.linePipeline = d.createRenderPipeline({
      layout: d.createPipelineLayout({ bindGroupLayouts: [this.lineLayout] }),
      vertex: { module: lineModule, entryPoint: 'vs', buffers: [{ arrayStride: 12, attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x3' }] }] },
      fragment: { module: lineModule, entryPoint: 'fs', targets: [{ format: this.format, blend: { color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha' }, alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha' } } }] },
      primitive: { topology: 'line-list' },
      depthStencil: { format: 'depth24plus', depthWriteEnabled: false, depthCompare: 'less-equal' },
    });
    this.lineBuffer = d.createBuffer({ size: 8192 * 12, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST, label: 'lines.vtx' });
    this._depth = null;
    this.resize();
  }

  resize() {
    if (!this.device) return;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = Math.max(2, Math.round(this.canvas.clientWidth * dpr));
    const h = Math.max(2, Math.round(this.canvas.clientHeight * dpr));
    if (this.canvas.width !== w || this.canvas.height !== h) {
      this.canvas.width = w; this.canvas.height = h;
      if (this._depth) this._depth.destroy();
      this._depth = this.device.createTexture({ size: [w, h], format: 'depth24plus', usage: GPUTextureUsage.RENDER_ATTACHMENT, label: 'depth' });
    }
    this.aspect = w / h;
  }

  uploadChunk(key, mesh) {
    const old = this.chunks.get(key);
    if (old) { old.buffer.destroy(); this.chunks.delete(key); }
    if (!mesh || mesh.vertexCount === 0) return;
    const buffer = this.device.createBuffer({ size: mesh.data.byteLength, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST, label: `chunk.${key}` });
    this.device.queue.writeBuffer(buffer, 0, mesh.data);
    this.chunks.set(key, { buffer, count: mesh.vertexCount });
  }
  clearChunks() {
    for (const c of this.chunks.values()) c.buffer.destroy();
    this.chunks.clear();
  }

  _brushLines(brush) {
    const pts = [];
    const c = brush.center, r = brush.radius;
    const seg = 56;
    const circle = (a, b) => {
      let prev = null;
      for (let i = 0; i <= seg; i++) {
        const t = (i / seg) * Math.PI * 2;
        const p = [c[0] + a[0] * Math.cos(t) * r + b[0] * Math.sin(t) * r,
                   c[1] + a[1] * Math.cos(t) * r + b[1] * Math.sin(t) * r,
                   c[2] + a[2] * Math.cos(t) * r + b[2] * Math.sin(t) * r];
        if (prev) pts.push(...prev, ...p);
        prev = p;
      }
    };
    circle([1, 0, 0], [0, 0, 1]);   // ground ring
    circle([1, 0, 0], [0, 1, 0]);
    circle([0, 0, 1], [0, 1, 0]);
    // centre cross
    const s = Math.min(0.7, r * 0.35);
    pts.push(c[0] - s, c[1], c[2], c[0] + s, c[1], c[2], c[0], c[1] - s, c[2], c[0], c[1] + s, c[2], c[0], c[1], c[2] - s, c[0], c[1], c[2] + s);
    return new Float32Array(pts);
  }

  render(camera, brush = null) {
    if (!this.device || !this._depth) return;
    this.resize();
    const d = this.device;
    const viewProj = camera.viewProj(this.aspect);
    const { pos, forward, right, up } = camera.getBasis();
    const sun = this.sunDirection();
    const s = this.settings;

    const u = new Float32Array(36);
    u.set(viewProj, 0);
    u[16] = pos[0]; u[17] = pos[1]; u[18] = pos[2]; u[19] = s.mode;
    u[20] = sun[0]; u[21] = sun[1]; u[22] = sun[2]; u[23] = this.time;
    const si = s.sunIntensity;
    u[24] = 1.0 * si; u[25] = 0.94 * si; u[26] = 0.84 * si; u[27] = s.caveFill;
    u[28] = 0.55; u[29] = 0.62; u[30] = 0.72; u[31] = s.fogDensity;
    u[32] = s.sedimentStrength; u[33] = s.moistureDarken; u[34] = s.jointShade; u[35] = s.grain;
    d.queue.writeBuffer(this.uniformBuffer, 0, u);

    const tanY = Math.tan(camera.fov / 2);
    const sk = new Float32Array(16);
    sk[0] = right[0]; sk[1] = right[1]; sk[2] = right[2]; sk[3] = tanY * this.aspect;
    sk[4] = up[0]; sk[5] = up[1]; sk[6] = up[2]; sk[7] = tanY;
    sk[8] = forward[0]; sk[9] = forward[1]; sk[10] = forward[2]; sk[11] = this.time;
    sk[12] = sun[0]; sk[13] = sun[1]; sk[14] = sun[2]; sk[15] = 0;
    d.queue.writeBuffer(this.skyUniformBuffer, 0, sk);

    const encoder = d.createCommandEncoder();
    const pass = encoder.beginRenderPass({
      colorAttachments: [{ view: this.context.getCurrentTexture().createView(), loadOp: 'clear', storeOp: 'store', clearValue: { r: 0.05, g: 0.05, b: 0.06, a: 1 } }],
      depthStencilAttachment: { view: this._depth.createView(), depthLoadOp: 'clear', depthStoreOp: 'store', depthClearValue: 1 },
    });
    pass.setPipeline(this.skyPipeline);
    pass.setBindGroup(0, this.skyBind);
    pass.draw(3);

    pass.setPipeline(this.terrainPipeline);
    pass.setBindGroup(0, this.terrainBind);
    for (const chunk of this.chunks.values()) {
      pass.setVertexBuffer(0, chunk.buffer);
      pass.draw(chunk.count);
    }

    if (brush && brush.visible && brush.center) {
      const lines = this._brushLines(brush);
      d.queue.writeBuffer(this.lineBuffer, 0, lines);
      const lu = new Float32Array(20);
      lu.set(viewProj, 0);
      const col = brush.color ?? [1, 1, 1];
      lu[16] = col[0]; lu[17] = col[1]; lu[18] = col[2]; lu[19] = 0.95;
      d.queue.writeBuffer(this.lineUniformBuffer, 0, lu);
      pass.setPipeline(this.linePipeline);
      pass.setBindGroup(0, this.lineBind);
      pass.setVertexBuffer(0, this.lineBuffer);
      pass.draw(lines.length / 3);
    }
    pass.end();
    d.queue.submit([encoder.finish()]);
  }
}

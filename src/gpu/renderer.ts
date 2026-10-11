/**
 * WebGPU renderer. Device-agnostic: the browser passes a canvas context format,
 * the headless check passes 'rgba8unorm' and a readback texture — the same code
 * path renders both, so anything verified headlessly is verified in the app.
 */

import type { MeshBuilder } from '../core/mesh';
import { SURFACE_WGSL, SKY_WGSL, WIRE_WGSL } from './shaders';
import { materialTable } from './materials';

export interface FrameUniforms {
  /** column-major view-projection (WebGPU clip space, y up, depth 0..1) */
  viewProj: Float32Array;
  cameraPos: [number, number, number];
  /** xyz = direction TO the sun, w = intensity */
  sunDir: [number, number, number, number];
  skyTop: [number, number, number];
  skyBottom: [number, number, number];
  ground: [number, number, number];
  /** x = horizon y in NDC, y..w reserved */
  params: [number, number, number, number];
  /** rgb = fog colour, w = fog density per metre */
  fog: [number, number, number, number];
}

export interface Batch {
  vbo: GPUBuffer;
  ibo: GPUBuffer;
  indexCount: number;
  vertexCount: number;
}

export class RoadRenderer {
  readonly device: GPUDevice;
  readonly format: GPUTextureFormat;
  readonly depthFormat: GPUTextureFormat;
  private surfacePipeline: GPURenderPipeline;
  private skyPipeline: GPURenderPipeline;
  private wirePipeline: GPURenderPipeline;
  private wireBind: GPUBindGroup;
  private frameBuffer: GPUBuffer;
  private materialBuffer: GPUBuffer;
  private surfaceBind: GPUBindGroup;
  private skyBind: GPUBindGroup;
  private depthTexture: GPUTexture | null = null;
  private depthSize: [number, number] = [0, 0];
  batches: Batch[] = [];
  private frameData = new Float32Array(16 + 4 + 4 + 4 + 4 + 4 + 4 + 4);

  private constructor(device: GPUDevice, format: GPUTextureFormat, depthFormat: GPUTextureFormat) {
    this.device = device;
    this.format = format;
    this.depthFormat = depthFormat;
    const surfaceModule = device.createShaderModule({ code: SURFACE_WGSL, label: 'surface' });
    const skyModule = device.createShaderModule({ code: SKY_WGSL, label: 'sky' });

    const vertexLayout: GPUVertexBufferLayout = {
      arrayStride: 3 * 4 + 3 * 4 + 2 * 4 + 2 * 4 + 4 + 4,
      attributes: [
        { shaderLocation: 0, offset: 0, format: 'float32x3' },
        { shaderLocation: 1, offset: 12, format: 'float32x3' },
        { shaderLocation: 2, offset: 24, format: 'float32x2' },
        { shaderLocation: 3, offset: 32, format: 'float32x2' },
        { shaderLocation: 4, offset: 40, format: 'uint32' },
        { shaderLocation: 5, offset: 44, format: 'unorm8x4' },
      ],
    };

    this.surfacePipeline = device.createRenderPipeline({
      label: 'surface',
      layout: 'auto',
      vertex: { module: surfaceModule, entryPoint: 'vs', buffers: [vertexLayout] },
      fragment: { module: surfaceModule, entryPoint: 'fs', targets: [{ format }] },
      primitive: { topology: 'triangle-list', cullMode: 'back', frontFace: 'ccw' },
      depthStencil: {
        format: depthFormat,
        depthWriteEnabled: true,
        depthCompare: 'less',
      },
    });
    this.skyPipeline = device.createRenderPipeline({
      label: 'sky',
      layout: 'auto',
      vertex: { module: skyModule, entryPoint: 'vs' },
      fragment: { module: skyModule, entryPoint: 'fs', targets: [{ format }] },
      primitive: { topology: 'triangle-list' },
      depthStencil: { format: depthFormat, depthWriteEnabled: false, depthCompare: 'always' },
    });

    const wireModule = device.createShaderModule({ code: WIRE_WGSL, label: 'wire' });
    this.wirePipeline = device.createRenderPipeline({
      label: 'wire',
      layout: 'auto',
      vertex: {
        module: wireModule,
        entryPoint: 'vs',
        buffers: [{ arrayStride: 3 * 4 + 3 * 4 + 2 * 4 + 2 * 4 + 4 + 4, attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x3' }] }],
      },
      fragment: { module: wireModule, entryPoint: 'fs', targets: [{ format }] },
      primitive: { topology: 'line-list' },
      depthStencil: { format: depthFormat, depthWriteEnabled: false, depthCompare: 'less-equal' },
    });
    this.frameBuffer = device.createBuffer({ size: this.frameData.byteLength, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, label: 'frame' });
    const table = materialTable();
    this.materialBuffer = device.createBuffer({ size: table.byteLength, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, label: 'materials' });
    device.queue.writeBuffer(this.materialBuffer, 0, table.buffer as ArrayBuffer);

    this.surfaceBind = device.createBindGroup({
      layout: this.surfacePipeline.getBindGroupLayout(0),
      entries: [
        { binding: 0, resource: { buffer: this.frameBuffer } },
        { binding: 1, resource: { buffer: this.materialBuffer } },
      ],
    });
    this.skyBind = device.createBindGroup({
      layout: this.skyPipeline.getBindGroupLayout(0),
      entries: [{ binding: 0, resource: { buffer: this.frameBuffer } }],
    });
    this.wireBind = device.createBindGroup({
      layout: this.wirePipeline.getBindGroupLayout(0),
      entries: [{ binding: 0, resource: { buffer: this.frameBuffer } }],
    });
  }

  static async create(device: GPUDevice, format: GPUTextureFormat, depthFormat: GPUTextureFormat = 'depth24plus'): Promise<RoadRenderer> {
    const r = new RoadRenderer(device, format, depthFormat);
    const info = await Promise.all([device.createShaderModule({ code: SURFACE_WGSL }).getCompilationInfo(), device.createShaderModule({ code: SKY_WGSL }).getCompilationInfo()]);
    const errors = info.flatMap((i) => i.messages.filter((m) => m.type === 'error'));
    if (errors.length) {
      throw new Error('WGSL errors:\n' + errors.map((e) => `${e.lineNum}:${e.linePos} ${e.message}`).join('\n'));
    }
    return r;
  }

  /** Upload one mesh builder as a draw batch. */
  upload(builder: MeshBuilder): Batch {
    const device = this.device;
    const packed = builder.pack();
    const n = packed.positions.length / 3;
    const bytesPerVertex = 3 * 4 + 3 * 4 + 2 * 4 + 2 * 4 + 4 + 4;
    const interleaved = new ArrayBuffer(n * bytesPerVertex);
    const f32 = new Float32Array(interleaved);
    const u32 = new Uint32Array(interleaved);
    const u8 = new Uint8Array(interleaved);
    for (let i = 0; i < n; i++) {
      const f = i * (bytesPerVertex / 4);
      const b = i * bytesPerVertex;
      f32[f] = packed.positions[i * 3];
      f32[f + 1] = packed.positions[i * 3 + 1];
      f32[f + 2] = packed.positions[i * 3 + 2];
      f32[f + 3] = packed.normals[i * 3];
      f32[f + 4] = packed.normals[i * 3 + 1];
      f32[f + 5] = packed.normals[i * 3 + 2];
      f32[f + 6] = packed.uvs[i * 2];
      f32[f + 7] = packed.uvs[i * 2 + 1];
      f32[f + 8] = packed.uv2s[i * 2];
      f32[f + 9] = packed.uv2s[i * 2 + 1];
      u32[f + 10] = packed.mats[i];
      u8[b + 44] = packed.tints[i * 4];
      u8[b + 45] = packed.tints[i * 4 + 1];
      u8[b + 46] = packed.tints[i * 4 + 2];
      u8[b + 47] = packed.tints[i * 4 + 3];
    }
    const vbo = device.createBuffer({ size: Math.max(48, interleaved.byteLength), usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST, label: 'verts' });
    device.queue.writeBuffer(vbo, 0, interleaved);
    const indexBytes = packed.indices.byteLength;
    const ibo = device.createBuffer({ size: Math.max(4, indexBytes), usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST, label: 'indices' });
    device.queue.writeBuffer(ibo, 0, packed.indices.buffer as ArrayBuffer, packed.indices.byteOffset, packed.indices.byteLength);
    const batch: Batch = { vbo, ibo, indexCount: packed.indices.length, vertexCount: n };
    this.batches.push(batch);
    return batch;
  }

  /** Upload the quad-edge line list for topology inspection. */
  uploadWire(builder: MeshBuilder, indices: Uint32Array): Batch {
    const device = this.device;
    const packed = builder.pack();
    const n = packed.positions.length / 3;
    const bytesPerVertex = 3 * 4 + 3 * 4 + 2 * 4 + 2 * 4 + 4 + 4;
    const interleaved = new ArrayBuffer(n * bytesPerVertex);
    const f32 = new Float32Array(interleaved);
    for (let i = 0; i < n; i++) {
      const f = i * (bytesPerVertex / 4);
      f32[f] = packed.positions[i * 3];
      f32[f + 1] = packed.positions[i * 3 + 1];
      f32[f + 2] = packed.positions[i * 3 + 2];
    }
    const vbo = device.createBuffer({ size: Math.max(48, interleaved.byteLength), usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST, label: 'wire-verts' });
    device.queue.writeBuffer(vbo, 0, interleaved);
    const ibo = device.createBuffer({ size: Math.max(4, indices.byteLength), usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST, label: 'wire-indices' });
    device.queue.writeBuffer(ibo, 0, indices.buffer as ArrayBuffer, indices.byteOffset, indices.byteLength);
    return { vbo, ibo, indexCount: indices.length, vertexCount: n };
  }

  /** Overlay the wireframe on top of an already-rendered frame. */
  renderWire(encoder: GPUCommandEncoder, view: GPUTextureView, width: number, height: number, batch: Batch): void {
    if (!batch.indexCount || !this.depthTexture) return;
    const pass = encoder.beginRenderPass({
      colorAttachments: [{ view, loadOp: 'load', storeOp: 'store' }],
      depthStencilAttachment: { view: this.depthTexture.createView(), depthLoadOp: 'load', depthStoreOp: 'store' },
    });
    pass.setPipeline(this.wirePipeline);
    pass.setBindGroup(0, this.wireBind);
    pass.setVertexBuffer(0, batch.vbo);
    pass.setIndexBuffer(batch.ibo, 'uint32');
    pass.drawIndexed(batch.indexCount);
    pass.end();
    void width;
    void height;
  }

  clearBatches(): void {
    for (const b of this.batches) {
      b.vbo.destroy();
      b.ibo.destroy();
    }
    this.batches = [];
  }

  private depthFor(width: number, height: number): GPUTexture {
    if (!this.depthTexture || this.depthSize[0] !== width || this.depthSize[1] !== height) {
      this.depthTexture?.destroy();
      this.depthTexture = this.device.createTexture({
        size: [width, height],
        format: this.depthFormat,
        usage: GPUTextureUsage.RENDER_ATTACHMENT,
        label: 'depth',
      });
      this.depthSize = [width, height];
    }
    return this.depthTexture;
  }

  private writeFrame(frame: FrameUniforms, width: number, height: number, aspect: number): void {
    const d = this.frameData;
    d.set(frame.viewProj, 0);
    d[16] = frame.cameraPos[0];
    d[17] = frame.cameraPos[1];
    d[18] = frame.cameraPos[2];
    d[19] = 1;
    d[20] = frame.sunDir[0];
    d[21] = frame.sunDir[1];
    d[22] = frame.sunDir[2];
    d[23] = frame.sunDir[3];
    d[24] = frame.skyTop[0];
    d[25] = frame.skyTop[1];
    d[26] = frame.skyTop[2];
    d[27] = 1;
    d[28] = frame.skyBottom[0];
    d[29] = frame.skyBottom[1];
    d[30] = frame.skyBottom[2];
    d[31] = 1;
    d[32] = frame.ground[0];
    d[33] = frame.ground[1];
    d[34] = frame.ground[2];
    d[35] = 1;
    d[36] = frame.params[0];
    d[37] = frame.params[1];
    d[38] = frame.params[2];
    d[39] = aspect;
    d[40] = frame.fog[0];
    d[41] = frame.fog[1];
    d[42] = frame.fog[2];
    d[43] = frame.fog[3];
    void width;
    void height;
    this.device.queue.writeBuffer(this.frameBuffer, 0, d.buffer as ArrayBuffer);
  }

  /** Record a full frame: sky, then every batch. */
  render(encoder: GPUCommandEncoder, view: GPUTextureView, width: number, height: number, frame: FrameUniforms): void {
    const depth = this.depthFor(width, height);
    this.writeFrame(frame, width, height, width / Math.max(1, height));
    const pass = encoder.beginRenderPass({
      colorAttachments: [
        { view, clearValue: { r: frame.skyBottom[0], g: frame.skyBottom[1], b: frame.skyBottom[2], a: 1 }, loadOp: 'clear', storeOp: 'store' },
      ],
      depthStencilAttachment: { view: depth.createView(), depthClearValue: 1, depthLoadOp: 'clear', depthStoreOp: 'store' },
    });
    pass.setPipeline(this.skyPipeline);
    pass.setBindGroup(0, this.skyBind);
    pass.draw(3);
    pass.setPipeline(this.surfacePipeline);
    pass.setBindGroup(0, this.surfaceBind);
    for (const batch of this.batches) {
      if (!batch.indexCount) continue;
      pass.setVertexBuffer(0, batch.vbo);
      pass.setIndexBuffer(batch.ibo, 'uint32');
      pass.drawIndexed(batch.indexCount);
    }
    pass.end();
  }

  dispose(): void {
    this.clearBatches();
    this.depthTexture?.destroy();
  }
}

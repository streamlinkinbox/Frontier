// Frontier Fluid — draws the simulated water surface.
// A static UV grid; the vertex shader displaces it with the solver's
// heightfield, so rendered waves == simulated waves, exactly.

import waterWGSL from './shaders/water.wgsl?raw';
import { makeRenderModule, COLOR_FORMAT, DEPTH_FORMAT, type Renderer } from '../engine/Renderer';
import type { FluidSim } from './FluidSim';
import type { Mat4 } from '../engine/math';

const WP_BYTES = 160;

export class WaterRenderer {
  private device: GPUDevice;
  private pipeline: GPURenderPipeline;
  private layout: GPUBindGroupLayout;
  private ubo: GPUBuffer;
  private vertexBuffer: GPUBuffer | null = null;
  private indexBuffer: GPUBuffer | null = null;
  private indexCount = 0;
  private meshRes = 0;
  private sampler: GPUSampler;
  reflectionEnabled = true;

  constructor(device: GPUDevice, renderer: Renderer) {
    this.device = device;
    this.sampler = device.createSampler({ magFilter: 'linear', minFilter: 'linear', addressModeU: 'clamp-to-edge', addressModeV: 'clamp-to-edge' });
    this.layout = device.createBindGroupLayout({
      label: 'water-mat',
      entries: [
        { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } },
        { binding: 1, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, texture: { sampleType: 'float' } },
        { binding: 2, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, texture: { sampleType: 'float' } },
        { binding: 3, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'float' } },
        { binding: 4, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'float' } },
        { binding: 5, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'float' } },
        { binding: 6, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'float' } },
        { binding: 7, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, sampler: { type: 'filtering' } },
      ],
    });
    this.ubo = device.createBuffer({ size: WP_BYTES, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, label: 'water-params' });
    const mod = makeRenderModule(device, waterWGSL, 'water');
    this.pipeline = device.createRenderPipeline({
      label: 'water',
      layout: device.createPipelineLayout({ bindGroupLayouts: [renderer.frameLayout, this.layout] }),
      vertex: {
        module: mod, entryPoint: 'vsMain',
        buffers: [{ arrayStride: 8, attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x2' }] }],
      },
      fragment: {
        module: mod, entryPoint: 'fsMain',
        targets: [{
          format: COLOR_FORMAT,
          blend: {
            color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha', operation: 'add' },
            alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' },
          },
        }],
      },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
      depthStencil: { format: DEPTH_FORMAT, depthWriteEnabled: false, depthCompare: 'less' },
    });
  }

  private buildMesh(res: number): void {
    this.meshRes = res;
    const n = res + 1;
    const verts = new Float32Array(n * n * 2);
    let p = 0;
    for (let j = 0; j < n; j++) {
      for (let i = 0; i < n; i++) {
        verts[p++] = i / res;
        verts[p++] = j / res;
      }
    }
    const idx = new Uint32Array(res * res * 6);
    p = 0;
    for (let j = 0; j < res; j++) {
      for (let i = 0; i < res; i++) {
        const a = j * n + i, b = a + 1, c = a + n, d2 = c + 1;
        idx[p++] = a; idx[p++] = c; idx[p++] = b;
        idx[p++] = b; idx[p++] = c; idx[p++] = d2;
      }
    }
    this.indexCount = idx.length;
    this.vertexBuffer?.destroy();
    this.indexBuffer?.destroy();
    this.vertexBuffer = this.device.createBuffer({ size: verts.byteLength, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST, label: 'water-grid-v' });
    this.indexBuffer = this.device.createBuffer({ size: idx.byteLength, usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST, label: 'water-grid-i' });
    this.device.queue.writeBuffer(this.vertexBuffer, 0, verts);
    this.device.queue.writeBuffer(this.indexBuffer, 0, idx);
  }

  draw(
    pass: GPURenderPassEncoder,
    renderer: Renderer,
    sim: FluidSim,
    reflVP: Mat4,
  ): void {
    const s = sim.settings;
    const meshRes = Math.min(512, Math.max(96, s.resolution));
    if (meshRes !== this.meshRes) this.buildMesh(meshRes);

    // WaterParams (must mirror water.wgsl offsets)
    const a = new Float32Array(WP_BYTES / 4);
    a.set(reflVP, 0);
    a.set(s.absorption, 16);
    a[19] = s.roughness;
    a.set(s.scatterColor, 20);
    a[23] = s.ior;
    a.set(s.deepColor, 24);
    a[27] = s.detailNormals ? 1 : 0;
    a.set(s.foamColor, 28);
    a[31] = this.reflectionEnabled ? 1 : 0;
    a.set(s.center, 32);
    a[34] = s.worldSize;
    a[35] = sim.dx;
    a[36] = s.resolution;
    this.device.queue.writeBuffer(this.ubo, 0, a);

    const bg = this.device.createBindGroup({
      layout: this.layout,
      entries: [
        { binding: 0, resource: { buffer: this.ubo } },
        { binding: 1, resource: sim.fieldView },
        { binding: 2, resource: sim.bedView },
        { binding: 3, resource: sim.foamView },
        { binding: 4, resource: renderer.refractTex.createView() },
        { binding: 5, resource: renderer.linDepth.createView() },
        { binding: 6, resource: renderer.reflColor.createView() },
        { binding: 7, resource: this.sampler },
      ],
    });
    pass.setPipeline(this.pipeline);
    pass.setBindGroup(0, renderer.frameBindGroup, [renderer.frameOffsetMain]);
    pass.setBindGroup(1, bg);
    pass.setVertexBuffer(0, this.vertexBuffer!);
    pass.setIndexBuffer(this.indexBuffer!, 'uint32');
    pass.drawIndexed(this.indexCount);
  }
}

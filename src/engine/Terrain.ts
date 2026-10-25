// Terrain mesh built from the same heightfield the fluid sim collides with,
// so what you see is exactly what the water flows over.
import terrainWGSL from './shaders/terrain.wgsl?raw';
import { makeRenderModule, COLOR_FORMAT, DEPTH_FORMAT, type Renderer } from './Renderer';
import type { FluidSim } from '../fluid/FluidSim';
import type { Vec3 } from './math';

export interface TerrainPalette {
  sand: Vec3;
  grass: Vec3;
  rock: Vec3;
  wet: Vec3;
}

export const VALLEY_PALETTE: TerrainPalette = {
  sand: [0.76, 0.66, 0.47],
  grass: [0.28, 0.45, 0.22],
  rock: [0.42, 0.40, 0.38],
  wet: [0.32, 0.25, 0.17],
};
export const MUD_PALETTE: TerrainPalette = {
  sand: [0.45, 0.34, 0.22],
  grass: [0.35, 0.36, 0.20],
  rock: [0.38, 0.32, 0.26],
  wet: [0.20, 0.14, 0.09],
};
export const BEACH_PALETTE: TerrainPalette = {
  sand: [0.82, 0.73, 0.54],
  grass: [0.35, 0.50, 0.28],
  rock: [0.45, 0.43, 0.40],
  wet: [0.55, 0.46, 0.33],
};

const TP_BYTES = 80;

export class Terrain {
  private device: GPUDevice;
  private pipeline: GPURenderPipeline;
  private layout: GPUBindGroupLayout;
  private ubo: GPUBuffer;
  private vertexBuffer: GPUBuffer | null = null;
  private indexBuffer: GPUBuffer | null = null;
  private indexCount = 0;
  private sampler: GPUSampler;
  palette: TerrainPalette = VALLEY_PALETTE;
  causticStrength = 1.0;

  constructor(device: GPUDevice, renderer: Renderer) {
    this.device = device;
    this.sampler = device.createSampler({ magFilter: 'linear', minFilter: 'linear', addressModeU: 'clamp-to-edge', addressModeV: 'clamp-to-edge' });
    this.layout = device.createBindGroupLayout({
      label: 'terrain-mat',
      entries: [
        { binding: 0, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } },
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'float' } },
        { binding: 2, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'float' } },
        { binding: 3, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'float' } },
        { binding: 4, visibility: GPUShaderStage.FRAGMENT, sampler: { type: 'filtering' } },
      ],
    });
    this.ubo = device.createBuffer({ size: TP_BYTES, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, label: 'terrain-params' });
    const mod = makeRenderModule(device, terrainWGSL, 'terrain');
    this.pipeline = device.createRenderPipeline({
      label: 'terrain',
      layout: device.createPipelineLayout({ bindGroupLayouts: [renderer.frameLayout, this.layout] }),
      vertex: {
        module: mod, entryPoint: 'vsMain',
        buffers: [{
          arrayStride: 24,
          attributes: [
            { shaderLocation: 0, offset: 0, format: 'float32x3' },
            { shaderLocation: 1, offset: 12, format: 'float32x3' },
          ],
        }],
      },
      fragment: {
        module: mod, entryPoint: 'fsMain',
        targets: [{ format: COLOR_FORMAT }, { format: COLOR_FORMAT }],
      },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
      depthStencil: { format: DEPTH_FORMAT, depthWriteEnabled: true, depthCompare: 'less' },
    });
  }

  /** Build geometry from a bed height array (res*res, row-major). */
  buildFromBed(heights: Float32Array, res: number, worldSize: number, center: [number, number]): void {
    const dx = worldSize / res;
    const verts = new Float32Array(res * res * 6);
    const h = (i: number, j: number) => heights[Math.max(0, Math.min(res - 1, j)) * res + Math.max(0, Math.min(res - 1, i))];
    let p = 0;
    for (let j = 0; j < res; j++) {
      for (let i = 0; i < res; i++) {
        const x = center[0] + ((i + 0.5) / res - 0.5) * worldSize;
        const z = center[1] + ((j + 0.5) / res - 0.5) * worldSize;
        const gx = (h(i + 1, j) - h(i - 1, j)) / (2 * dx);
        const gz = (h(i, j + 1) - h(i, j - 1)) / (2 * dx);
        const inv = 1 / Math.hypot(gx, 1, gz);
        verts[p++] = x; verts[p++] = h(i, j); verts[p++] = z;
        verts[p++] = -gx * inv; verts[p++] = inv; verts[p++] = -gz * inv;
      }
    }
    const idx = new Uint32Array((res - 1) * (res - 1) * 6);
    p = 0;
    for (let j = 0; j < res - 1; j++) {
      for (let i = 0; i < res - 1; i++) {
        const a = j * res + i, b = a + 1, c = a + res, d = c + 1;
        idx[p++] = a; idx[p++] = c; idx[p++] = b;
        idx[p++] = b; idx[p++] = c; idx[p++] = d;
      }
    }
    this.indexCount = idx.length;
    this.vertexBuffer?.destroy();
    this.indexBuffer?.destroy();
    this.vertexBuffer = this.device.createBuffer({ size: verts.byteLength, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST, label: 'terrain-v' });
    this.indexBuffer = this.device.createBuffer({ size: idx.byteLength, usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST, label: 'terrain-i' });
    this.device.queue.writeBuffer(this.vertexBuffer, 0, verts);
    this.device.queue.writeBuffer(this.indexBuffer, 0, idx);
  }

  draw(pass: GPURenderPassEncoder, renderer: Renderer, sim: FluidSim, frameOffset: number): void {
    if (!this.vertexBuffer) return;
    const s = sim.settings;
    const a = new Float32Array(TP_BYTES / 4);
    a.set(this.palette.sand, 0); a[3] = 0.35;
    a.set(this.palette.grass, 4); a[7] = 1.6;
    a.set(this.palette.rock, 8); a[11] = 0.35;
    a.set(this.palette.wet, 12); a[15] = this.causticStrength;
    a.set(s.center, 16);
    a[18] = s.worldSize;
    a[19] = s.waterLevel;
    this.device.queue.writeBuffer(this.ubo, 0, a);

    const bg = this.device.createBindGroup({
      layout: this.layout,
      entries: [
        { binding: 0, resource: { buffer: this.ubo } },
        { binding: 1, resource: sim.fieldView },
        { binding: 2, resource: sim.foamView },
        { binding: 3, resource: sim.causticsView },
        { binding: 4, resource: this.sampler },
      ],
    });
    pass.setPipeline(this.pipeline);
    pass.setBindGroup(0, renderer.frameBindGroup, [frameOffset]);
    pass.setBindGroup(1, bg);
    pass.setVertexBuffer(0, this.vertexBuffer);
    pass.setIndexBuffer(this.indexBuffer!, 'uint32');
    pass.drawIndexed(this.indexCount);
  }
}

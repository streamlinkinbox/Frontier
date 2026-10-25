// Frontier Fluid — instanced rendering of splash droplets + foam flecks.
// Particle state lives in GPU storage buffers (written by compute); they are
// bound directly as instanced vertex buffers — zero CPU per-particle cost.

import particlesRenderWGSL from './shaders/particlesRender.wgsl?raw';
import { makeRenderModule, COLOR_FORMAT, DEPTH_FORMAT, type Renderer } from '../engine/Renderer';
import type { FluidSim } from './FluidSim';

export class ParticleRenderer {
  private device: GPUDevice;
  private splashPipeline: GPURenderPipeline;
  private foamPipeline: GPURenderPipeline;
  private ubo: GPUBuffer;
  private bindGroup: GPUBindGroup;
  private quadBuffer: GPUBuffer;
  enabled = true;

  constructor(device: GPUDevice, renderer: Renderer) {
    this.device = device;
    const matLayout = device.createBindGroupLayout({
      label: 'particle-mat',
      entries: [{
        binding: 0,
        visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
        buffer: { type: 'uniform' },
      }],
    });
    this.ubo = device.createBuffer({ size: 32, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, label: 'particle-params' });
    this.bindGroup = device.createBindGroup({ layout: matLayout, entries: [{ binding: 0, resource: { buffer: this.ubo } }] });

    // unit quad as triangle strip: (-1,-1) (1,-1) (-1,1) (1,1)
    const quad = new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]);
    this.quadBuffer = device.createBuffer({ size: quad.byteLength, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST, label: 'particle-quad' });
    device.queue.writeBuffer(this.quadBuffer, 0, quad);

    const mod = makeRenderModule(device, particlesRenderWGSL, 'particlesRender');
    const layout = device.createPipelineLayout({ bindGroupLayouts: [renderer.frameLayout, matLayout] });
    const buffers: GPUVertexBufferLayout[] = [
      { arrayStride: 8, stepMode: 'vertex', attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x2' }] },
      {
        arrayStride: 32, stepMode: 'instance',
        attributes: [
          { shaderLocation: 1, offset: 0, format: 'float32x4' },
          { shaderLocation: 2, offset: 16, format: 'float32x4' },
        ],
      },
    ];
    const blend: GPUBlendState = {
      color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha', operation: 'add' },
      alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' },
    };
    const mkPipe = (vs: string, fs: string, label: string) => device.createRenderPipeline({
      label,
      layout,
      vertex: { module: mod, entryPoint: vs, buffers },
      fragment: { module: mod, entryPoint: fs, targets: [{ format: COLOR_FORMAT, blend }] },
      primitive: { topology: 'triangle-strip', cullMode: 'none' },
      depthStencil: { format: DEPTH_FORMAT, depthWriteEnabled: false, depthCompare: 'less' },
    });
    this.splashPipeline = mkPipe('vsSplash', 'fsSplash', 'splash-particles');
    this.foamPipeline = mkPipe('vsFoam', 'fsFoam', 'foam-particles');
  }

  draw(pass: GPURenderPassEncoder, renderer: Renderer, sim: FluidSim): void {
    if (!this.enabled) return;
    const s = sim.settings;
    const a = new Float32Array(8);
    a.set(s.splashColor, 0);
    a[3] = 1.25;
    a.set(s.foamColor, 4);
    a[7] = 1.1;
    this.device.queue.writeBuffer(this.ubo, 0, a);

    pass.setBindGroup(0, renderer.frameBindGroup, [renderer.frameOffsetMain]);
    pass.setBindGroup(1, this.bindGroup);
    pass.setVertexBuffer(0, this.quadBuffer);

    // foam first (flat on surface), spray on top
    pass.setPipeline(this.foamPipeline);
    pass.setVertexBuffer(1, sim.foamParticleBuf);
    pass.draw(4, s.maxFoamParticles);

    pass.setPipeline(this.splashPipeline);
    pass.setVertexBuffer(1, sim.splashBuf);
    pass.draw(4, s.maxSplashParticles);
  }
}

// Unit-box geometry + material shared by crates, the boat and vehicle parts.
import boxWGSL from './shaders/box.wgsl?raw';
import { makeRenderModule, COLOR_FORMAT, DEPTH_FORMAT, type Renderer } from './Renderer';
import type { Mat4, Vec3 } from './math';

const OBJ_BYTES = 96;

export interface BoxStyle {
  color: Vec3;
  color2?: Vec3;
  planks?: boolean;
  roughness?: number;
}

export class BoxObject {
  ubo: GPUBuffer;
  bindGroup: GPUBindGroup;
  constructor(device: GPUDevice, layout: GPUBindGroupLayout) {
    this.ubo = device.createBuffer({ size: OBJ_BYTES, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, label: 'box-ubo' });
    this.bindGroup = device.createBindGroup({ layout, entries: [{ binding: 0, resource: { buffer: this.ubo } }] });
  }
  update(device: GPUDevice, model: Mat4, style: BoxStyle): void {
    const a = new Float32Array(OBJ_BYTES / 4);
    a.set(model, 0);
    a.set(style.color, 16);
    a[19] = style.planks ? 1 : 0;
    a.set(style.color2 ?? style.color, 20);
    a[23] = style.roughness ?? 0.6;
    device.queue.writeBuffer(this.ubo, 0, a);
  }
}

function buildBoxGeometry(device: GPUDevice): { vb: GPUBuffer; ib: GPUBuffer; count: number } {
  // 24 verts (pos, normal), unit cube centred at origin, half-extent 0.5.
  const faces: Array<{ n: Vec3; v: Vec3[] }> = [
    { n: [0, 0, 1], v: [[-0.5, -0.5, 0.5], [0.5, -0.5, 0.5], [0.5, 0.5, 0.5], [-0.5, 0.5, 0.5]] },
    { n: [0, 0, -1], v: [[0.5, -0.5, -0.5], [-0.5, -0.5, -0.5], [-0.5, 0.5, -0.5], [0.5, 0.5, -0.5]] },
    { n: [1, 0, 0], v: [[0.5, -0.5, 0.5], [0.5, -0.5, -0.5], [0.5, 0.5, -0.5], [0.5, 0.5, 0.5]] },
    { n: [-1, 0, 0], v: [[-0.5, -0.5, -0.5], [-0.5, -0.5, 0.5], [-0.5, 0.5, 0.5], [-0.5, 0.5, -0.5]] },
    { n: [0, 1, 0], v: [[-0.5, 0.5, 0.5], [0.5, 0.5, 0.5], [0.5, 0.5, -0.5], [-0.5, 0.5, -0.5]] },
    { n: [0, -1, 0], v: [[-0.5, -0.5, -0.5], [0.5, -0.5, -0.5], [0.5, -0.5, 0.5], [-0.5, -0.5, 0.5]] },
  ];
  const verts = new Float32Array(24 * 6);
  const idx = new Uint16Array(36);
  let p = 0, q = 0;
  faces.forEach((f, fi) => {
    for (const v of f.v) {
      verts[p++] = v[0]; verts[p++] = v[1]; verts[p++] = v[2];
      verts[p++] = f.n[0]; verts[p++] = f.n[1]; verts[p++] = f.n[2];
    }
    const b = fi * 4;
    idx[q++] = b; idx[q++] = b + 1; idx[q++] = b + 2;
    idx[q++] = b; idx[q++] = b + 2; idx[q++] = b + 3;
  });
  const vb = device.createBuffer({ size: verts.byteLength, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST, label: 'box-v' });
  const ib = device.createBuffer({ size: idx.byteLength, usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST, label: 'box-i' });
  device.queue.writeBuffer(vb, 0, verts);
  device.queue.writeBuffer(ib, 0, idx);
  return { vb, ib, count: 36 };
}

export class BoxRenderer {
  private pipeline: GPURenderPipeline;
  readonly layout: GPUBindGroupLayout;
  private geo: { vb: GPUBuffer; ib: GPUBuffer; count: number };

  constructor(device: GPUDevice, renderer: Renderer) {
    this.layout = device.createBindGroupLayout({
      label: 'box-mat',
      entries: [{
        binding: 0,
        visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
        buffer: { type: 'uniform' },
      }],
    });
    const mod = makeRenderModule(device, boxWGSL, 'box');
    this.pipeline = device.createRenderPipeline({
      label: 'box',
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
    this.geo = buildBoxGeometry(device);
  }

  createObject(device: GPUDevice): BoxObject {
    return new BoxObject(device, this.layout);
  }

  draw(pass: GPURenderPassEncoder, renderer: Renderer, obj: BoxObject, frameOffset: number): void {
    pass.setPipeline(this.pipeline);
    pass.setBindGroup(0, renderer.frameBindGroup, [frameOffset]);
    pass.setBindGroup(1, obj.bindGroup);
    pass.setVertexBuffer(0, this.geo.vb);
    pass.setIndexBuffer(this.geo.ib, 'uint16');
    pass.drawIndexed(this.geo.count);
  }
}

/**
 * Renderer. All geometry comes from GPU buffers the simulation already owns:
 *   - tyre mesh : particle positions + per-vertex normals (compute), indexed triangles
 *   - rim       : static mesh transformed by the hub pose in the vertex shader
 *   - ground    : procedural grid that follows the camera
 */
import { perspective, lookAt, multiply } from './math.js';

const CAM_BYTES = 112; // mat4 (64) + eye, light, opts (3 x 16)

/** Static rim mesh in hub-local coordinates (axle along +Z, rolling direction +X). */
export function buildRimMesh(rimRadius = 0.2032, halfWidth = 0.09) {
  const verts = [];
  const push = (p, n) => verts.push(p[0], p[1], p[2], n[0], n[1], n[2]);
  const quad = (a, b, c, d, n) => {
    push(a, n); push(b, n); push(c, n);
    push(a, n); push(c, n); push(d, n);
  };
  const SEG = 64;
  const rBarrel = rimRadius - 0.006;
  // barrel (hidden behind the bead, kept for completeness)
  for (let k = 0; k < SEG; k++) {
    const a0 = (2 * Math.PI * k) / SEG, a1 = (2 * Math.PI * (k + 1)) / SEG;
    const c0 = [Math.cos(a0), Math.sin(a0), 0], c1 = [Math.cos(a1), Math.sin(a1), 0];
    quad(
      [c0[0] * rBarrel, c0[1] * rBarrel, -halfWidth], [c1[0] * rBarrel, c1[1] * rBarrel, -halfWidth],
      [c1[0] * rBarrel, c1[1] * rBarrel, halfWidth], [c0[0] * rBarrel, c0[1] * rBarrel, halfWidth],
      [c0[0], c0[1], 0]
    );
  }
  // flat faces (dish) on both sides and a hub cap
  for (const s of [-1, 1]) {
    const z = s * halfWidth;
    const nz = [0, 0, s];
    for (let k = 0; k < SEG; k++) {
      const a0 = (2 * Math.PI * k) / SEG, a1 = (2 * Math.PI * (k + 1)) / SEG;
      const p0 = [rimRadius * Math.cos(a0), rimRadius * Math.sin(a0), z];
      const p1 = [rimRadius * Math.cos(a1), rimRadius * Math.sin(a1), z];
      const q0 = [0.05 * Math.cos(a0), 0.05 * Math.sin(a0), z + s * 0.02];
      const q1 = [0.05 * Math.cos(a1), 0.05 * Math.sin(a1), z + s * 0.02];
      quad(q0, p0, p1, q1, nz);
    }
    for (let k = 0; k < SEG; k++) {
      const a0 = (2 * Math.PI * k) / SEG, a1 = (2 * Math.PI * (k + 1)) / SEG;
      push([0, 0, z + s * 0.03], nz);
      push([0.05 * Math.cos(a0), 0.05 * Math.sin(a0), z + s * 0.02], nz);
      push([0.05 * Math.cos(a1), 0.05 * Math.sin(a1), z + s * 0.02], nz);
    }
  }
  // five spokes: thin blades on each face, from the hub cap to the rim
  for (let sp = 0; sp < 5; sp++) {
    const a = (2 * Math.PI * sp) / 5 + 0.3;
    const dir = [Math.cos(a), Math.sin(a), 0];
    const tan = [-Math.sin(a), Math.cos(a), 0];
    const r0 = 0.05, r1 = rimRadius - 0.01, hw = 0.011, t0 = 0.075, t1 = 0.09;
    for (const s of [-1, 1]) {
      const z0 = s * t0, z1 = s * t1;
      const P = (r, w, z) => [dir[0] * r + tan[0] * w, dir[1] * r + tan[1] * w, z];
      const nz = [0, 0, s];
      quad(P(r0, -hw, z1), P(r1, -hw, z1), P(r1, hw, z1), P(r0, hw, z1), nz);
      // side walls
      quad(P(r0, -hw, z0), P(r1, -hw, z0), P(r1, -hw, z1), P(r0, -hw, z1), [-tan[0], -tan[1], 0]);
      quad(P(r0, hw, z0), P(r0, hw, z1), P(r1, hw, z1), P(r1, hw, z0), [tan[0], tan[1], 0]);
    }
  }
  return new Float32Array(verts);
}

export class TyreRenderer {
  constructor(device, format, module, canvas) {
    this.device = device;
    this.format = format;
    this.module = module;
    this.canvas = canvas;
    this.depth = null;
  }

  /**
   * @param {TyreSim} sim
   */
  async init(sim) {
    const d = this.device;
    this.sim = sim;
    this.camBuffer = d.createBuffer({ size: CAM_BYTES, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    this.indexBuffer = d.createBuffer({
      size: sim.topo.triIndices.byteLength,
      usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST,
    });
    d.queue.writeBuffer(this.indexBuffer, 0, sim.topo.triIndices);
    this.indexCount = sim.topo.T * 3;

    const rim = buildRimMesh();
    this.rimVertexCount = rim.length / 6;
    this.rimBuffer = d.createBuffer({ size: rim.byteLength, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
    d.queue.writeBuffer(this.rimBuffer, 0, rim);

    const depthStencil = { format: 'depth24plus', depthWriteEnabled: true, depthCompare: 'less' };
    const target = [{ format: this.format }];
    const [tyre, rimPipe, ground] = await Promise.all([
      d.createRenderPipelineAsync({
        layout: 'auto',
        label: 'tyre',
        vertex: { module: this.module, entryPoint: 'vs_tyre' },
        fragment: { module: this.module, entryPoint: 'fs_tyre', targets: target },
        primitive: { topology: 'triangle-list', cullMode: 'none' },
        depthStencil,
      }),
      d.createRenderPipelineAsync({
        layout: 'auto',
        label: 'rim',
        vertex: {
          module: this.module,
          entryPoint: 'vs_rim',
          buffers: [{
            arrayStride: 24,
            attributes: [
              { shaderLocation: 0, offset: 0, format: 'float32x3' },
              { shaderLocation: 1, offset: 12, format: 'float32x3' },
            ],
          }],
        },
        fragment: { module: this.module, entryPoint: 'fs_rim', targets: target },
        primitive: { topology: 'triangle-list', cullMode: 'none' },
        depthStencil,
      }),
      d.createRenderPipelineAsync({
        layout: 'auto',
        label: 'ground',
        vertex: { module: this.module, entryPoint: 'vs_ground' },
        fragment: { module: this.module, entryPoint: 'fs_ground', targets: target },
        primitive: { topology: 'triangle-list', cullMode: 'none' },
        depthStencil,
      }),
    ]);
    this.pipes = { tyre, rim: rimPipe, ground };
    this.bg = {
      tyre: d.createBindGroup({
        layout: tyre.getBindGroupLayout(0),
        entries: [
          { binding: 0, resource: { buffer: this.camBuffer } },
          { binding: 1, resource: { buffer: sim.buf.POS } },
          { binding: 2, resource: { buffer: sim.buf.VNRM } },
        ],
      }),
      rim: d.createBindGroup({
        layout: rimPipe.getBindGroupLayout(0),
        entries: [
          { binding: 0, resource: { buffer: this.camBuffer } },
          { binding: 3, resource: { buffer: sim.buf.HUB } },
        ],
      }),
      ground: d.createBindGroup({
        layout: ground.getBindGroupLayout(0),
        entries: [{ binding: 0, resource: { buffer: this.camBuffer } }],
      }),
    };
    this.resize();
  }

  resize() {
    const w = Math.max(1, this.canvas.width);
    const h = Math.max(1, this.canvas.height);
    if (this.depth && this.depth.width === w && this.depth.height === h) return;
    this.depth?.destroy();
    this.depth = this.device.createTexture({
      size: [w, h],
      format: 'depth24plus',
      usage: GPUTextureUsage.RENDER_ATTACHMENT,
    });
    this.depthView = this.depth.createView();
  }

  /**
   * @param {GPUCommandEncoder} encoder
   * @param {GPUTextureView} view
   * @param {{target:number[], yaw:number, pitch:number, dist:number, cameraX:number, showStrain:boolean, strainScale:number}} cam
   */
  record(encoder, view, cam) {
    this.resize();
    const aspect = this.canvas.width / this.canvas.height;
    const tgt = [cam.cameraX, 0.30, 0];
    const cp = Math.cos(cam.pitch), sp = Math.sin(cam.pitch);
    const eye = [
      tgt[0] + cam.dist * cp * Math.sin(cam.yaw),
      tgt[1] + cam.dist * sp,
      tgt[2] + cam.dist * cp * Math.cos(cam.yaw),
    ];
    const vp = multiply(perspective(Math.PI / 5.2, aspect, 0.02, 80), lookAt(eye, tgt, [0, 1, 0]));
    const light = [0.35, 0.9, 0.5];
    const buf = new Float32Array(CAM_BYTES / 4);
    buf.set(vp, 0);
    buf.set([eye[0], eye[1], eye[2], 1], 16);
    buf.set([light[0], light[1], light[2], 0], 20);
    buf.set([cam.strainScale, cam.showStrain ? 1 : 0, cam.cameraX, 0], 24);
    this.device.queue.writeBuffer(this.camBuffer, 0, buf);

    const pass = encoder.beginRenderPass({
      label: 'tyre-view',
      colorAttachments: [{
        view,
        clearValue: { r: 0.035, g: 0.04, b: 0.05, a: 1 },
        loadOp: 'clear',
        storeOp: 'store',
      }],
      depthStencilAttachment: {
        view: this.depthView,
        depthClearValue: 1.0,
        depthLoadOp: 'clear',
        depthStoreOp: 'store',
      },
    });
    pass.setPipeline(this.pipes.ground);
    pass.setBindGroup(0, this.bg.ground);
    pass.draw(6);

    pass.setPipeline(this.pipes.rim);
    pass.setBindGroup(0, this.bg.rim);
    pass.setVertexBuffer(0, this.rimBuffer);
    pass.draw(this.rimVertexCount);

    pass.setPipeline(this.pipes.tyre);
    pass.setBindGroup(0, this.bg.tyre);
    pass.setIndexBuffer(this.indexBuffer, 'uint32');
    pass.drawIndexed(this.indexCount);
    pass.end();
  }
}

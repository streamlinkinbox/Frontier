// DomainLink – two-way nesting between a fine FluidSim and a coarse far-field FluidSim.
import { LINK_EDGE, LINK_TO_COARSE } from './shaders/link.wgsl.js';

export class DomainLink {
  constructor(fine, coarse) {
    this.fine = fine;
    this.coarse = coarse;
    this.device = fine.device;
    this.uniform = this.device.createBuffer({ label: 'link.params', size: 56, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    const mk = (code, label) => this.device.createComputePipeline({
      label, layout: 'auto', compute: { module: this.device.createShaderModule({ code, label }), entryPoint: 'main' },
    });
    this.edgePipe = mk(LINK_EDGE, 'link.edge');
    this.toCoarsePipe = mk(LINK_TO_COARSE, 'link.toCoarse');
    this.edgeBG = fine.bindGroup(this.edgePipe, [this.uniform, coarse.hc[0], coarse.vel[0], fine.hc[0], fine.vel[0]], 'link.edgeBG');
    this.toCoarseBG = fine.bindGroup(this.toCoarsePipe, [this.uniform, fine.hc[0], fine.vel[0], coarse.hc[0], coarse.vel[0]], 'link.toCoarseBG');
    this.edgeGroups = [Math.ceil(fine.nx / 8), Math.ceil(fine.ny / 8)];
    this.coarseGroups = [Math.ceil(coarse.nx / 8), Math.ceil(coarse.ny / 8)];
    this._pack();
  }

  _pack() {
    const f = this.fine, c = this.coarse;
    // WGSL std140-like alignment: the second vec2 starts at byte 24, not 20
    const a = new Float32Array(14);
    a[0] = f.origin[0]; a[1] = f.origin[1]; a[2] = f.dx; a[3] = f.nx;
    a[4] = f.ny; a[6] = c.origin[0]; a[7] = c.origin[1]; a[8] = c.dx;
    a[9] = c.nx; a[10] = c.ny;
    this.device.queue.writeBuffer(this.uniform, 0, a);
  }

  /** Copy coarse boundary state into the fine edge cells (run before fine.step). */
  edge(encoder) {
    this._pack();
    const pass = encoder.beginComputePass({ label: 'link.coarseToFineEdge' });
    pass.setPipeline(this.edgePipe);
    pass.setBindGroup(0, this.edgeBG);
    pass.dispatchWorkgroups(this.edgeGroups[0], this.edgeGroups[1]);
    pass.end();
  }

  /** Overwrite the coarse cells under the fine patch with the fine state (run after fine.step). */
  interior(encoder) {
    this._pack();
    const pass = encoder.beginComputePass({ label: 'link.fineToCoarse' });
    pass.setPipeline(this.toCoarsePipe);
    pass.setBindGroup(0, this.toCoarseBG);
    pass.dispatchWorkgroups(this.coarseGroups[0], this.coarseGroups[1]);
    pass.end();
  }
}

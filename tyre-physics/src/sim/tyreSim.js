/**
 * GPU tyre simulation: owns every buffer and pipeline, and records the frame's compute work.
 *
 * There is no CPU physics path. The host writes a uniform block and the initial state once,
 * then only records and submits command buffers. The host reads back a small telemetry
 * block asynchronously (see gpu/readback.js); particle data never leaves the GPU.
 */
import { buildTyreTopology, EDGE_TYPE } from '../tyre/topology.js';
import { PHYS, PARAM_LAYOUT, PARAM_BYTES, TELEM_BYTES } from '../tyre/config.js';

/** Binding index for each named resource in tyre_sim.wgsl (group 0). */
const BIND = Object.freeze({
  P: 0, POS: 1, PREV: 2, VEL: 3, LAMC: 4, PINACC: 5, ACC: 6, EDGES: 7, EPARAM: 8, ELAM: 9,
  RANGE: 10, ATT: 11, HUB: 12, TRIS: 13, VTRI: 14, VNRM: 15, RED: 16, VOL: 17, TRIREST: 18,
  TELEM: 19, X0: 20, CL0: 21, CLC: 22, CLR: 23, VNBR: 24,
});

/** Resources each entry point statically uses (must match the WGSL exactly). */
const USES = Object.freeze({
  frame_reset: ['P', 'ACC'],
  substep_reset: ['P', 'ELAM', 'LAMC', 'PINACC', 'VOL'],
  hub_predict: ['P', 'HUB'],
  hub_final: ['P', 'RED', 'HUB'],
  predict: ['P', 'POS', 'PREV', 'VEL'],
  velocity: ['P', 'POS', 'PREV', 'VEL'],
  xpbd_edges: ['P', 'POS', 'EDGES', 'EPARAM', 'ELAM', 'RANGE'],
  sm_cluster: ['P', 'POS', 'X0', 'CL0', 'CLC', 'CLR', 'VNBR'],
  sm_goal: ['P', 'POS', 'X0', 'CL0', 'CLC', 'CLR', 'VNBR'],
  vol_pass1: ['P', 'POS', 'TRIS', 'VTRI', 'TRIREST', 'RED'],
  vol_final: ['P', 'RED', 'VOL'],
  vol_apply: ['P', 'POS', 'VOL', 'TRIS', 'VTRI'],
  pins: ['P', 'POS', 'ATT', 'HUB', 'PINACC'],
  attach_pass1: ['P', 'POS', 'HUB', 'PINACC', 'RED'],
  contact: ['P', 'POS', 'PREV', 'LAMC', 'PINACC', 'ACC', 'HUB'],
  telem_pass1: ['P', 'ACC', 'RED'],
  telem_final: ['P', 'RED', 'VOL', 'HUB', 'TELEM'],
  normals: ['P', 'POS', 'TRIS', 'VTRI', 'TRIREST', 'VNRM'],
});

const ENTRY_POINTS = Object.keys(USES);

const STORAGE = GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC;

export class TyreSim {
  /**
   * @param {GPUDevice} device
   * @param {GPUShaderModule} module compiled tyre_sim.wgsl
   * @param {{NP:number, NC:number}} preset
   * @param {{timestamps: boolean}} opts
   */
  constructor(device, module, preset, opts = {}) {
    this.device = device;
    this.module = module;
    this.preset = preset;
    this.timestamps = !!opts.timestamps;
    this.topo = buildTyreTopology({ NP: preset.NP, NC: preset.NC, tyreMass: PHYS.tyreMass });
    this.pipelines = {};
    this.bind = {};
    this.stiffnessScale = 1.0;
    this.maxRed = 1;
    this.preBar = 2.3; // inflation used to pre-tension the membrane (see writeEdgeParams)
  }

  /** Compile all entry points (asynchronously to avoid blocking the main thread). */
  async init() {
    const t = this.topo;
    const d = this.device;
    const N = t.N;
    this.maxRed = Math.max(Math.ceil(N / 256), Math.ceil(t.T / 256), Math.max(1, Math.ceil(t.K / 256)));
    /** total rest surface area [m^2], used for the patch-area estimate */
    this.restArea = t.trisRestArea.reduce((a, b) => a + b, 0);

    // ---- buffers ----
    const mk = (size, label) => d.createBuffer({ size: Math.max(16, size), usage: STORAGE, label });
    this.buf = {
      P: d.createBuffer({ size: PARAM_BYTES, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, label: 'params' }),
      POS: mk(N * 16, 'pos'),
      PREV: mk(N * 16, 'prev'),
      VEL: mk(N * 16, 'vel'),
      LAMC: mk(N * 4, 'lamC'),
      PINACC: mk(N * 16, 'pinAcc'),
      ACC: mk(N * 16, 'acc'),
      EDGES: mk(t.E * 8, 'edges'),
      EPARAM: mk(t.E * 8, 'eparam'),
      ELAM: mk(t.E * 4, 'elam'),
      ATT: mk(t.K * 16, 'attach'),
      HUB: mk(5 * 16, 'hub'),
      TRIS: mk(t.T * 16, 'tris'),
      VTRI: mk(N * 6 * 4, 'vtri'),
      VNRM: mk(N * 16, 'vnormal'),
      RED: mk(this.maxRed * 16, 'reduce'),
      VOL: mk(2 * 16, 'volume'),
      TRIREST: mk(t.T * 4, 'triRest'),
      TELEM: mk(TELEM_BYTES, 'telemetry'),
      X0: mk(N * 16, 'x0'),
      CL0: mk(N * 16, 'cl0'),
      CLC: mk(N * 16, 'clC'),
      CLR: mk(N * 48, 'clR'),
      VNBR: mk(N * 12 * 4, 'vnbr'),
      RANGE: null,
    };
    this.buf.TELEM_RESOLVE = null;

    // static topology
    d.queue.writeBuffer(this.buf.X0, 0, t.x0);
    d.queue.writeBuffer(this.buf.CL0, 0, t.cl0);
    d.queue.writeBuffer(this.buf.TRIS, 0, t.tris);
    d.queue.writeBuffer(this.buf.VTRI, 0, t.vtri);
    d.queue.writeBuffer(this.buf.TRIREST, 0, t.trisRestArea);
    d.queue.writeBuffer(this.buf.EDGES, 0, t.edges);
    d.queue.writeBuffer(this.buf.VNBR, 0, t.vnbr);
    d.queue.writeBuffer(this.buf.ATT, 0, t.attachBuffer);
    this.writeEdgeParams();

    // per-colour uniform ranges for XPBD
    this.colorBuffers = t.colorRanges.map((r) => {
      const b = d.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
      d.queue.writeBuffer(b, 0, new Uint32Array([r.start, r.count, 0, 0]));
      return b;
    });

    // ---- pipelines ----
    const pipes = await Promise.all(
      ENTRY_POINTS.map((name) =>
        d.createComputePipelineAsync({ layout: 'auto', compute: { module: this.module, entryPoint: name }, label: name })
      )
    );
    ENTRY_POINTS.forEach((name, k) => (this.pipelines[name] = pipes[k]));

    // ---- bind groups (static) ----
    const res = { ...this.buf };
    const bg = (name, overrides = {}) => {
      const pipe = this.pipelines[name];
      const entries = USES[name].map((key) => ({
        binding: BIND[key],
        resource: { buffer: overrides[key] ?? res[key] },
      }));
      return d.createBindGroup({ layout: pipe.getBindGroupLayout(0), entries, label: name });
    };
    this.bind = {};
    for (const name of ENTRY_POINTS) {
      if (name === 'xpbd_edges') continue;
      this.bind[name] = bg(name);
    }
    this.bind.xpbd_edges = this.colorBuffers.map((cb) => bg('xpbd_edges', { RANGE: cb }));

    // ---- timestamps ----
    if (this.timestamps) {
      this.querySet = d.createQuerySet({ type: 'timestamp', count: 2 });
      this.tsResolve = d.createBuffer({ size: 16, usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC });
    }

    this.reset();
  }

  /** Write the initial state: tyre at rest above the road, hub already rolling at speed. */
  reset(speedKmh = this.lastSpeedKmh ?? 36, bar = this.preBar, hubY = null) {
    const t = this.topo;
    if (bar !== this.preBar) {
      this.preBar = bar;
      this.writeEdgeParams();
    }
    const d = this.device;
    const N = t.N;
    const V = speedKmh / 3.6;
    const omega0 = -V / PHYS.effectiveRadius;
    const h0y = hubY ?? 0.316 + PHYS.contactRadius + 0.002;

    const pos = new Float32Array(N * 4);
    const vel = new Float32Array(N * 4);
    for (let v = 0; v < N; v++) {
      const lx = t.pos[3 * v], ly = t.pos[3 * v + 1], lz = t.pos[3 * v + 2];
      pos[4 * v] = lx;
      pos[4 * v + 1] = h0y + ly;
      pos[4 * v + 2] = lz;
      pos[4 * v + 3] = 1 / t.vertexMass;
      vel[4 * v] = V - omega0 * ly;
      vel[4 * v + 1] = omega0 * lx;
      vel[4 * v + 2] = 0;
    }
    d.queue.writeBuffer(this.buf.POS, 0, pos);
    d.queue.writeBuffer(this.buf.PREV, 0, pos);
    d.queue.writeBuffer(this.buf.VEL, 0, vel);
    d.queue.writeBuffer(this.buf.ACC, 0, new Float32Array(N * 4));
    d.queue.writeBuffer(this.buf.LAMC, 0, new Float32Array(N));
    d.queue.writeBuffer(this.buf.PINACC, 0, new Float32Array(N * 4));
    d.queue.writeBuffer(this.buf.VOL, 0, new Float32Array(8));
    d.queue.writeBuffer(this.buf.HUB, 0, new Float32Array([
      0, h0y, 0, 0,
      V, 0, 0, omega0,
      0, 0, 0, 0,
      0, 0, 0, 0,
      0, 0, 0, 0,
    ]));
    this.simTime = 0;
    this.frameIndex = 0;
    this.lastSpeedKmh = speedKmh;
  }

  /** Membrane stiffness multiplier (scales all compliances). */
  setStiffness(scale) {
    this.stiffnessScale = scale;
    this.writeEdgeParams();
  }

  /**
   * Edge rest lengths and compliances.
   *
   * A pressurised membrane needs tension to hold its gas load. The rest geometry is the
   * unpressurised shape, so stretch edges are pre-tensioned: rest = L (1 - alpha * T), where
   * T = p * r / 2 is the line tension for inflation pressure p and profile radius r. Then the
   * edges already carry the gas load at the start, and the membrane does not balloon or
   * launch at t = 0. Compliance is scaled by the stiffness slider and the rest length follows,
   * so the pre-tension stays the same.
   */
  writeEdgeParams() {
    const t = this.topo;
    const data = new Float32Array(t.E * 2);
    const s = this.stiffnessScale;
    const T = (this.preBar * 1e5 * PHYS.preTensionRadius) / 2;
    for (let e = 0; e < t.E; e++) {
      const type = t.edgeType[e];
      let alpha = PHYS.alphaStretch;
      let rest = t.edgeRest[e];
      if (type === EDGE_TYPE.SHEAR_D || type === EDGE_TYPE.SHEAR_A) alpha = PHYS.alphaShear;
      if (type === EDGE_TYPE.BEND_U || type === EDGE_TYPE.BEND_V) alpha = PHYS.alphaBend;
      if (type === EDGE_TYPE.STRETCH_U || type === EDGE_TYPE.STRETCH_V) {
        rest = rest * (1 - alpha * s * T);
      }
      data[2 * e] = rest;
      data[2 * e + 1] = alpha * s;
    }
    this.device.queue.writeBuffer(this.buf.EPARAM, 0, data);
  }

  /**
   * Build the uniform block for this frame.
   * @param {object} cfg {speedKmh, slipDeg, payloadKg, bar, mu, solver, iterations, nSub}
   */
  writeParams(cfg) {
    const t = this.topo;
    const { nSub, iterations } = cfg;
    const frameDt = nSub * PHYS.dtSub;
    const V = cfg.speedKmh / 3.6;
    const latTarget = V * Math.tan((cfg.slipDeg * Math.PI) / 180);
    const hubM = PHYS.hubMass;
    const totalMass = hubM + cfg.payloadKg;
    const p0 = cfg.bar * 1e5;
    const V0 = t.restVolume;
    // Start-up ramps (numerical settle-in; see README). Measured targets are not ramped.
    const smooth = (x) => { const c = Math.min(1, Math.max(0, x)); return c * c * (3 - 2 * c); };
    const loadRamp = smooth(this.simTime / PHYS.loadRampTime);
    const pRamp = Math.max(0.02, smooth(this.simTime / PHYS.pressureRampTime));
    const volAlpha = V0 / (PHYS.gamma * p0 * pRamp);
    const volTarget = V0 * (1 + 1 / PHYS.gamma);
    const values = {
      dt: PHYS.dtSub,
      invDt: 1 / PHYS.dtSub,
      invDt2: 1 / (PHYS.dtSub * PHYS.dtSub),
      frameDt,
      gravityY: -(cfg.gravity ?? PHYS.gravity), // cfg.gravity: test override only
      totalMass,
      hubInertia: PHYS.hubInertia,
      servoKv: totalMass / PHYS.servoTau,
      speedTarget: V,
      latTarget,
      servoKw: PHYS.hubInertia / PHYS.motorTau,
      omegaTarget: -V / PHYS.effectiveRadius,
      rimRadius: 0.2032,
      rimHalfWidth: 0.09,
      contactRadius: PHYS.contactRadius,
      mu: cfg.mu,
      volAlpha,
      volTarget,
      damping: PHYS.damping,
      smBeta: PHYS.smBeta,
      frameIndex: this.frameIndex,
      simTime: this.simTime,
      restVolume: V0,
      tyreMass: t.tyreMass,
      numVerts: t.N,
      numTris: t.T,
      numAttach: t.K,
      numEdges: t.E,
      numVertWG: Math.ceil(t.N / 256),
      numTriWG: Math.ceil(t.T / 256),
      numAttachWG: Math.max(1, Math.ceil(t.K / 256)),
      frameSamples: nSub * iterations,
      loadRamp,
      payloadFrac: cfg.payloadKg / totalMass,
      hubDamp: cfg.hubDamp ?? PHYS.hubDamp, // cfg.hubDamp: test override only
      pad0: 0,
    };
    const buf = new ArrayBuffer(PARAM_BYTES);
    const f = new Float32Array(buf);
    const u = new Uint32Array(buf);
    PARAM_LAYOUT.forEach(([name, type], i) => {
      if (type === 'u') u[i] = values[name];
      else f[i] = values[name];
    });
    this.device.queue.writeBuffer(this.buf.P, 0, buf);
    this.lastParams = values;
  }

  /**
   * Record one frame of simulation into a single compute pass.
   * @param {GPUCommandEncoder} encoder
   * @param {{nSub:number, iterations:number, solver:'xpbd'|'shape'}} cfg
   */
  recordFrame(encoder, { nSub, iterations, solver }) {
    const t = this.topo;
    const pipes = this.pipelines;
    const bind = this.bind;
    const pass = encoder.beginComputePass({
      label: 'tyre-sim',
      timestampWrites: this.timestamps
        ? { querySet: this.querySet, beginningOfPassWriteIndex: 0, endOfPassWriteIndex: 1 }
        : undefined,
    });
    const run = (name, groups, bg = bind[name]) => {
      pass.setPipeline(pipes[name]);
      pass.setBindGroup(0, bg);
      pass.dispatchWorkgroups(groups);
    };
    const nv256 = Math.ceil(t.N / 256);
    const nv64 = Math.ceil(t.N / 64);
    const nTri = Math.ceil(t.T / 256);
    const nAtt64 = Math.max(1, Math.ceil(t.K / 64));

    run('frame_reset', nv256);
    for (let s = 0; s < nSub; s++) {
      run('substep_reset', Math.ceil(Math.max(t.E, t.N, 2) / 256));
      run('hub_predict', 1);
      run('predict', nv256);
      for (let it = 0; it < iterations; it++) {
        if (solver === 'xpbd') {
          for (let c = 0; c < t.colourCount; c++) {
            run('xpbd_edges', Math.ceil(t.colorRanges[c].count / 64), bind.xpbd_edges[c]);
          }
        } else {
          run('sm_cluster', nv64);
          run('sm_goal', nv64);
        }
        run('vol_pass1', nTri);
        run('vol_final', 1);
        run('vol_apply', nv256);
        run('pins', nAtt64);
        run('contact', nv256);
      }
      run('attach_pass1', nv256);
      run('hub_final', 1);
      run('velocity', nv256);
    }
    run('telem_pass1', nv256);
    run('telem_final', 1);
    pass.end();
    this.simTime += nSub * PHYS.dtSub;
    this.frameIndex += 1;
    if (this.timestamps) {
      encoder.resolveQuerySet(this.querySet, 0, 2, this.tsResolve, 0);
    }
  }

  /** Render normals/strain for the tyre mesh (cheap, one pass). */
  recordNormals(encoder) {
    const pass = encoder.beginComputePass({ label: 'tyre-normals' });
    pass.setPipeline(this.pipelines.normals);
    pass.setBindGroup(0, this.bind.normals);
    pass.dispatchWorkgroups(Math.ceil(this.topo.N / 256));
    pass.end();
  }

  /**
   * Debug/test helper: copy a named buffer to the host (async). Not used by the frame loop.
   * @param {string} key one of this.buf's keys (e.g. 'POS', 'VEL', 'HUB')
   * @returns {Promise<Float32Array>}
   */
  async readBuffer(key) {
    const src = this.buf[key];
    const size = src.size;
    const stg = this.device.createBuffer({ size, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    const enc = this.device.createCommandEncoder();
    enc.copyBufferToBuffer(src, 0, stg, 0, size);
    this.device.queue.submit([enc.finish()]);
    await stg.mapAsync(GPUMapMode.READ);
    const out = new Float32Array(stg.getMappedRange().slice(0));
    stg.unmap();
    stg.destroy();
    return out;
  }

  /** Decode the 128-byte telemetry block written by telem_final. */
  static decodeTelemetry(f32) {
    const v = (i) => [f32[4 * i], f32[4 * i + 1], f32[4 * i + 2], f32[4 * i + 3]];
    const [Fz, Fx, FzLat, contacts] = v(0);
    const [V, pressure, strainMean, volErr] = v(1);
    const [hx, hy, hz, theta] = v(2);
    const [vx, vy, vz, omega] = v(3);
    const [frameIndex, simTime, payloadW, tyreW] = v(4);
    const [rJx, rJy, rJz, rTau] = v(5);
    return {
      Fz, Fx, FzLat, contacts,
      V, pressure, strainMean, volErr,
      hub: { x: hx, y: hy, z: hz, theta },
      hubVel: { x: vx, y: vy, z: vz, omega },
      frameIndex, simTime, targetLoad: payloadW + tyreW,
      reaction: { x: rJx, y: rJy, z: rJz, tau: rTau },
    };
  }
}

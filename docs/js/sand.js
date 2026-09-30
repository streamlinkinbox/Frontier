// GTX tier sand: MLS-MPM (Hu et al. 2018) with a no-tension granular pressure model and Coulomb
// friction at the ground. Particles are only spawned at tyre contact patches and recycled in a ring
// buffer. The MPM grid is transient (cleared every substep), so it simply re-centres on the car.
// P2G uses fixed-point atomic<i32> (WGSL has no float atomics).

import { TIER } from './tier.js?v=16';
export const SAND_MAX = TIER.sand;
export const SAND_GRID = [64, 32, 64];
export const SAND_H = 0.125; // 8 x 4 x 8 m around the car
const FIX = 65536.0; // fixed-point scale
const STRIDE = 20; // floats per particle (5 x vec4)

const COMMON = /* wgsl */`
struct Particle { pos: vec4f, vel: vec4f, c0: vec4f, c1: vec4f, c2: vec4f };
struct SP {
  dims: vec4<i32>, origin: vec4f, misc: vec4f, misc2: vec4f,
  carC: vec4f, carH: vec4f, carV: vec4f, blast: vec4f, tor: vec4f, tor1: vec4f,
};
@group(0) @binding(0) var<uniform> sp: SP;
const FIX: f32 = ${FIX.toFixed(1)};

fn gIdx(c: vec3<i32>) -> u32 { let d = sp.dims.xyz; return u32(c.x + d.x * (c.y + d.y * c.z)); }
fn stencilOk(cell: vec3<i32>) -> bool {
  return all(cell >= vec3<i32>(1)) && all(cell < sp.dims.xyz - vec3<i32>(1));
}
fn qweights(d: vec3f) -> array<vec3f, 3> {
  return array<vec3f, 3>(0.5 * (0.5 - d) * (0.5 - d), 0.75 - d * d, 0.5 * (0.5 + d) * (0.5 + d));
}
fn toFix(x: f32) -> i32 { return i32(clamp(x, -30000.0, 30000.0) * FIX); }
// car-local coordinates
fn carLocal(wp: vec3f) -> vec3f {
  let d = wp - sp.carC.xyz; let h = sp.carC.w;
  return vec3f(d.x * cos(h) - d.z * sin(h), d.y, d.x * sin(h) + d.z * cos(h));
}
fn carVelAt(wp: vec3f) -> vec3f {
  let d = wp - sp.carC.xyz; let w = sp.carV.w;
  return vec3f(sp.carV.x + w * d.z, 0.0, sp.carV.z - w * d.x);
}
`;

const SRC = {
  clear: `
@group(0) @binding(1) var<storage, read_write> grid: array<vec4<i32>>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  if (g.x >= arrayLength(&grid)) { return; }
  grid[g.x] = vec4<i32>(0);
}`,

  p2g1: `
@group(0) @binding(1) var<storage, read> ps: array<Particle>;
@group(0) @binding(2) var<storage, read_write> grid: array<atomic<i32>>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  if (g.x >= arrayLength(&ps)) { return; }
  let p = ps[g.x];
  if (p.pos.w <= 0.0) { return; }
  let h = sp.origin.w;
  let x = (p.pos.xyz - sp.origin.xyz) / h;
  let cell = vec3<i32>(floor(x));
  if (!stencilOk(cell)) { return; }
  var w = qweights(x - vec3f(cell) - 0.5);
  let v = p.vel.xyz / h;
  let C = mat3x3f(p.c0.xyz, p.c1.xyz, p.c2.xyz);
  for (var gx = 0; gx < 3; gx++) { for (var gy = 0; gy < 3; gy++) { for (var gz = 0; gz < 3; gz++) {
    let wt = w[gx].x * w[gy].y * w[gz].z;
    let cx = cell + vec3<i32>(gx - 1, gy - 1, gz - 1);
    let dist = vec3f(cx) - x + 0.5;
    let mom = wt * (v + C * dist);
    let i = gIdx(cx) * 4u;
    atomicAdd(&grid[i], toFix(mom.x));
    atomicAdd(&grid[i + 1u], toFix(mom.y));
    atomicAdd(&grid[i + 2u], toFix(mom.z));
    atomicAdd(&grid[i + 3u], toFix(wt));
  }}}
}`,

  // density from grid mass -> granular pressure (no tension) + viscous friction -> momentum
  p2g2: `
@group(0) @binding(1) var<storage, read> ps: array<Particle>;
@group(0) @binding(2) var<storage, read_write> grid: array<atomic<i32>>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  if (g.x >= arrayLength(&ps)) { return; }
  let p = ps[g.x];
  if (p.pos.w <= 0.0) { return; }
  let h = sp.origin.w;
  let x = (p.pos.xyz - sp.origin.xyz) / h;
  let cell = vec3<i32>(floor(x));
  if (!stencilOk(cell)) { return; }
  var w = qweights(x - vec3f(cell) - 0.5);
  var density = 0.0;
  for (var gx = 0; gx < 3; gx++) { for (var gy = 0; gy < 3; gy++) { for (var gz = 0; gz < 3; gz++) {
    let wt = w[gx].x * w[gy].y * w[gz].z;
    let cx = cell + vec3<i32>(gx - 1, gy - 1, gz - 1);
    density += f32(atomicLoad(&grid[gIdx(cx) * 4u + 3u])) / FIX * wt;
  }}}
  let volume = 1.0 / max(density, 1e-3);
  let pressure = sp.misc.z * max(density / sp.misc.w - 1.0, 0.0);
  let C = mat3x3f(p.c0.xyz, p.c1.xyz, p.c2.xyz);
  let strain = C + transpose(C);
  let stress = mat3x3f(-pressure, 0.0, 0.0, 0.0, -pressure, 0.0, 0.0, 0.0, -pressure) + sp.misc2.x * strain;
  let term = -volume * 4.0 * sp.misc.x * stress;
  for (var gx = 0; gx < 3; gx++) { for (var gy = 0; gy < 3; gy++) { for (var gz = 0; gz < 3; gz++) {
    let wt = w[gx].x * w[gy].y * w[gz].z;
    let cx = cell + vec3<i32>(gx - 1, gy - 1, gz - 1);
    let dist = vec3f(cx) - x + 0.5;
    let mom = wt * (term * dist);
    let i = gIdx(cx) * 4u;
    atomicAdd(&grid[i], toFix(mom.x));
    atomicAdd(&grid[i + 1u], toFix(mom.y));
    atomicAdd(&grid[i + 2u], toFix(mom.z));
  }}}
}`,

  grid: `
@group(0) @binding(1) var<storage, read_write> grid: array<vec4<i32>>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  if (g.x >= arrayLength(&grid)) { return; }
  let raw = grid[g.x];
  let m = f32(raw.w) / FIX;
  if (m <= 1e-6) { grid[g.x] = vec4<i32>(0); return; }
  let d = sp.dims.xyz;
  let c = vec3<i32>(i32(g.x) % d.x, (i32(g.x) / d.x) % d.y, i32(g.x) / (d.x * d.y));
  let h = sp.origin.w;
  var v = vec3f(raw.xyz) / FIX / m;
  v.y += sp.misc.y * sp.misc.x;
  let wp = sp.origin.xyz + (vec3f(c) + 0.5) * h;
  // ground with Coulomb friction (sand piles instead of flowing)
  if (wp.y < h * 0.9 && v.y < 0.0) {
    let vn = -v.y;
    v.y = 0.0;
    let vt = length(v.xz);
    if (vt > 1e-6) { v = vec3f(v.x * max(0.0, 1.0 - sp.misc2.y * vn / vt), 0.0, v.z * max(0.0, 1.0 - sp.misc2.y * vn / vt)); }
  }
  // car body box (slightly inflated) pushes sand with its velocity
  // tornado wind drags the sand (grid units)
  if (sp.tor.z > 0.0) {
    let rel = wp.xz - sp.tor.xy;
    let r = max(length(rel), 0.05);
    let rc = sp.tor.w * (0.55 + wp.y * 0.09);
    let vt = sp.tor.z * select(rc / r, r / rc, r < rc);
    let tang = vec2f(-rel.y, rel.x) / r;
    let inflow = -rel / r * sp.tor.z * 0.45 * exp(-wp.y / 3.0) * clamp(r / rc, 0.0, 1.5);
    let rr = r / (rc * 1.4);
    let wind = vec3f(tang.x * vt + inflow.x, sp.tor1.x * exp(-rr * rr), tang.y * vt + inflow.y) / h;
    v = mix(v, wind, clamp(exp(-r / (rc * 2.5)) * sp.misc.x * 5.0, 0.0, 1.0));
  }
  // explosion shock pushes sand radially
  if (sp.blast.w > 0.0) {
    let q = wp - sp.blast.xyz;
    let r = length(q);
    v += (q / max(r, 0.1)) * sp.blast.w * exp(-r * r / 18.0) / h + vec3f(0.0, sp.blast.w * 0.4 * exp(-r * r / 18.0) / h, 0.0);
  }
  let lp = carLocal(wp);
  if (all(abs(lp) < sp.carH.xyz + vec3f(h))) {
    v = carVelAt(wp) / h;
  }
  grid[g.x] = bitcast<vec4<i32>>(vec4f(v, m));
}`,

  g2p: `
@group(0) @binding(1) var<storage, read_write> ps: array<Particle>;
@group(0) @binding(2) var<storage, read> grid: array<vec4f>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  if (g.x >= arrayLength(&ps)) { return; }
  var p = ps[g.x];
  if (p.pos.w <= 0.0) { return; }
  let h = sp.origin.w; let dt = sp.misc.x;
  let x = (p.pos.xyz - sp.origin.xyz) / h;
  let cell = vec3<i32>(floor(x));
  var vel = p.vel.xyz;
  if (stencilOk(cell)) {
    var w = qweights(x - vec3f(cell) - 0.5);
    var v = vec3f(0.0);
    var B = mat3x3f(vec3f(0.0), vec3f(0.0), vec3f(0.0));
    for (var gx = 0; gx < 3; gx++) { for (var gy = 0; gy < 3; gy++) { for (var gz = 0; gz < 3; gz++) {
      let wt = w[gx].x * w[gy].y * w[gz].z;
      let cx = cell + vec3<i32>(gx - 1, gy - 1, gz - 1);
      let dist = vec3f(cx) - x + 0.5;
      let gv = grid[gIdx(cx)].xyz;
      let wv = wt * gv;
      v += wv;
      B += mat3x3f(wv * dist.x, wv * dist.y, wv * dist.z);
    }}}
    let C = B * 4.0;
    p.c0 = vec4f(C[0], 0.0); p.c1 = vec4f(C[1], 0.0); p.c2 = vec4f(C[2], 0.0);
    vel = v * h;
  } else {
    vel.y -= 9.81 * dt;
    p.c0 = vec4f(0.0); p.c1 = vec4f(0.0); p.c2 = vec4f(0.0);
  }
  vel = clamp(vel, vec3f(-40.0), vec3f(40.0));
  var pos = p.pos.xyz + vel * dt;
  let r = 0.025;
  if (pos.y < r) {
    pos.y = r;
    if (vel.y < 0.0) { vel.y = 0.0; }
    vel = vec3f(vel.x * 0.9, vel.y, vel.z * 0.9);
  }
  // car box: project out through the nearest face
  let lp = carLocal(pos);
  let he = sp.carH.xyz;
  if (all(abs(lp) < he)) {
    let pen = he - abs(lp);
    var lo = lp;
    if (pen.x < pen.y && pen.x < pen.z) { lo.x = sign(lp.x) * he.x; }
    else if (pen.y < pen.z) { lo.y = sign(lp.y) * he.y; }
    else { lo.z = sign(lp.z) * he.z; }
    let hh = sp.carC.w;
    // back to world (inverse of carLocal rotation)
    let wx = lo.x * cos(hh) + lo.z * sin(hh);
    let wz = -lo.x * sin(hh) + lo.z * cos(hh);
    pos = sp.carC.xyz + vec3f(wx, lo.y, wz);
    vel = carVelAt(pos);
  }
  var life = p.pos.w - dt;
  if (distance(pos.xz, sp.carC.xz) > 30.0) { life = 0.0; }
  p.pos = vec4f(pos, life);
  p.vel = vec4f(vel, p.vel.w);
  ps[g.x] = p;
}`,
};

const RENDER = /* wgsl */`
struct Particle { pos: vec4f, vel: vec4f, c0: vec4f, c1: vec4f, c2: vec4f };
struct Cam { viewProj: mat4x4f, invViewProj: mat4x4f, camPos: vec4f, lightDir: vec4f, screen: vec4f };
@group(0) @binding(0) var<uniform> cam: Cam;
@group(0) @binding(1) var<storage, read> ps: array<Particle>;
struct VO { @builtin(position) pos: vec4f, @location(0) uv: vec2f, @location(1) col: vec3f };
@vertex fn vs(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VO {
  var o: VO;
  let p = ps[ii];
  if (p.pos.w <= 0.0) { o.pos = vec4f(2.0, 2.0, 2.0, 1.0); return o; }
  var corners = array<vec2f, 6>(vec2f(-1,-1), vec2f(1,-1), vec2f(1,1), vec2f(-1,-1), vec2f(1,1), vec2f(-1,1));
  let q = corners[vi];
  let seed = p.vel.w;
  let r0 = fract(seed * 7.13);
  // skewed grain-size distribution: mostly fine grains, a few coarse ones
  let size = (0.006 + 0.016 * r0 * r0 * r0 + 0.004 * fract(seed * 3.7)) * clamp(p.pos.w * 1.5, 0.0, 1.0) * max(1.0, p.c0.w);
  let toCam = normalize(cam.camPos.xyz - p.pos.xyz);
  let right = normalize(cross(vec3f(0.0, 1.0, 0.0), toCam));
  let up = cross(toCam, right);
  let wp = p.pos.xyz + (right * q.x + up * q.y) * size;
  o.pos = cam.viewProj * vec4f(wp, 1.0);
  o.uv = q;
  let tint = fract(seed * 13.7);
  let dark = step(0.9, fract(seed * 5.31));
  var col = mix(vec3f(0.70, 0.53, 0.33), vec3f(0.95, 0.84, 0.64), tint);
  col = mix(col, vec3f(0.42, 0.33, 0.24), dark * 0.7);   // occasional dark mineral grains
  col *= 0.85 + 0.3 * fract(seed * 23.1);
  o.col = col;
  return o;
}
@fragment fn fs(i: VO) -> @location(0) vec4f {
  let r2 = dot(i.uv, i.uv);
  if (r2 > 1.0) { discard; }
  let n = vec3f(i.uv.x, i.uv.y, sqrt(1.0 - r2));
  let l = clamp(0.45 + 0.55 * dot(n, normalize(vec3f(0.3, 0.8, 0.5))), 0.0, 1.0);
  return vec4f(i.col * (0.45 + 0.75 * l), 1.0);
}
`;

export class Sand {
  constructor(device, format, checkModule) {
    this.device = device;
    const [gx, gy, gz] = SAND_GRID;
    this.cells = gx * gy * gz;
    this.substeps = 3;
    this.head = 0;
    this.lifeCPU = new Float32Array(SAND_MAX); // approximate alive count for HUD
    this.pbuf = device.createBuffer({ size: SAND_MAX * STRIDE * 4, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.VERTEX, label: 'sandParticles' });
    this.gbuf = device.createBuffer({ size: this.cells * 16, usage: GPUBufferUsage.STORAGE, label: 'sandGrid' });
    this.bytes = SAND_MAX * STRIDE * 4 + this.cells * 16;
    this.params = new Float32Array(10 * 4);
    this.paramsI = new Int32Array(this.params.buffer);
    this.ubo = device.createBuffer({ size: this.params.byteLength, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, label: 'sandParams' });

    this.pipe = {}; this.bg = {};
    const binds = { clear: [this.gbuf], p2g1: [this.pbuf, this.gbuf], p2g2: [this.pbuf, this.gbuf], grid: [this.gbuf], g2p: [this.pbuf, this.gbuf] };
    for (const [k, src] of Object.entries(SRC)) {
      const module = checkModule(device.createShaderModule({ code: COMMON + src, label: 'sand-' + k }), 'sand-' + k);
      this.pipe[k] = device.createComputePipeline({ layout: 'auto', compute: { module, entryPoint: 'main' }, label: 'sand-' + k });
      // 'clear' doesn't reference sp -> its auto layout has no binding 0
      const entries = binds[k].map((b, i) => ({ binding: i + 1, resource: { buffer: b } }));
      if (k !== 'clear') entries.unshift({ binding: 0, resource: { buffer: this.ubo } });
      this.bg[k] = device.createBindGroup({ layout: this.pipe[k].getBindGroupLayout(0), entries });
    }
    const rmod = checkModule(device.createShaderModule({ code: RENDER, label: 'sand-render' }), 'sand-render');
    this.renderPipe = device.createRenderPipeline({
      layout: 'auto', label: 'sand-render',
      vertex: { module: rmod, entryPoint: 'vs' },
      fragment: { module: rmod, entryPoint: 'fs', targets: [{ format }] },
      primitive: { topology: 'triangle-list' },
      depthStencil: { format: 'depth32float', depthWriteEnabled: true, depthCompare: 'less' },
    });
    this.staging = new Float32Array(Math.ceil(256 * TIER.sandSpawn) * STRIDE);
  }

  makeRenderBindGroup(camUbo) {
    this.renderBG = this.device.createBindGroup({
      layout: this.renderPipe.getBindGroupLayout(0),
      entries: [{ binding: 0, resource: { buffer: camUbo } }, { binding: 1, resource: { buffer: this.pbuf } }],
    });
  }

  spawn(car, dt) {
    let n = 0;
    const S = this.staging, maxN = S.length / STRIDE;
    const [rgx, rgz] = car.right();
    for (const wh of car.wheels) {
      if (wh.sand <= 0) continue;
      let count = Math.floor(wh.sand * 110 * TIER.sandSpawn * (dt * 60) + Math.random());
      const tread = -wh.spinVel * car.wheelR; // bottom-of-tyre surface speed along wheel forward
      const kx = wh.vel[0] + tread * wh.fwd[0], kz = wh.vel[2] + tread * wh.fwd[2];
      const kl = Math.hypot(kx, kz) || 1;
      for (let i = 0; i < count && n < maxN; i++, n++) {
        const o = n * STRIDE;
        const side = (Math.random() - 0.5) * car.wheelW * 1.2;
        const back = 0.25 + Math.random() * 0.2;
        const r = 0.5 + Math.random() * 0.6;
        S[o] = wh.pos[0] + rgx * side + (kx / kl) * back;
        S[o + 1] = 0.04 + Math.random() * 0.15;
        S[o + 2] = wh.pos[2] + rgz * side + (kz / kl) * back;
        S[o + 3] = 2.5 + Math.random() * 2.0; // life (s)
        S[o + 4] = kx * r * 0.75 + (Math.random() - 0.5) * 1.2;
        S[o + 5] = 0.8 + Math.random() * (1.2 + kl * 0.35);
        S[o + 6] = kz * r * 0.75 + (Math.random() - 0.5) * 1.2;
        S[o + 7] = Math.random() * 100; // seed
        for (let k = 8; k < STRIDE; k++) S[o + k] = 0;
      }
    }
    // ring-buffer upload (split at wrap)
    let off = 0;
    while (off < n) {
      const chunk = Math.min(n - off, SAND_MAX - this.head);
      this.device.queue.writeBuffer(this.pbuf, this.head * STRIDE * 4, S, off * STRIDE, chunk * STRIDE);
      for (let i = 0; i < chunk; i++) this.lifeCPU[this.head + i] = 4.5;
      this.head = (this.head + chunk) % SAND_MAX;
      off += chunk;
    }
    this.spawned = n;
  }

  burst(x, z, n, speed) {
    const S = new Float32Array(n * STRIDE);
    for (let i = 0; i < n; i++) {
      const o = i * STRIDE, a = Math.random() * Math.PI * 2, up = 0.3 + Math.random() * 0.9, sp = speed * (0.3 + Math.random() * 0.7);
      S[o] = x + Math.cos(a) * 0.8 * Math.random(); S[o + 1] = 0.1 + Math.random() * 0.8; S[o + 2] = z + Math.sin(a) * 0.8 * Math.random();
      S[o + 3] = 3 + Math.random() * 2;
      S[o + 4] = Math.cos(a) * sp; S[o + 5] = up * speed; S[o + 6] = Math.sin(a) * sp; S[o + 7] = Math.random() * 100;
    }
    let off = 0;
    while (off < n) {
      const chunk = Math.min(n - off, SAND_MAX - this.head);
      this.device.queue.writeBuffer(this.pbuf, this.head * STRIDE * 4, S, off * STRIDE, chunk * STRIDE);
      for (let i = 0; i < chunk; i++) this.lifeCPU[this.head + i] = 5;
      this.head = (this.head + chunk) % SAND_MAX; off += chunk;
    }
  }

  aliveEstimate(dt) {
    let a = 0;
    for (let i = 0; i < SAND_MAX; i++) { if (this.lifeCPU[i] > 0) { this.lifeCPU[i] -= dt; a++; } }
    return a;
  }

  update(dt, car) {
    const [gx, gy, gz] = SAND_GRID, h = SAND_H;
    const ox = Math.round(car.x / h - gx / 2) * h, oz = Math.round(car.z / h - gz / 2) * h;
    const P = this.params, I = this.paramsI;
    P.fill(0);
    I[0] = gx; I[1] = gy; I[2] = gz;
    P.set([ox, -2 * h, oz, h], 4);
    const sdt = dt / this.substeps;
    P.set([sdt, -9.81 / h, 900.0, 6.0], 8);          // dt, gravity (grid units), stiffness, rest density
    P.set([0.4, 0.9, SAND_MAX, 0], 12);               // viscosity, friction, count
    P.set([car.x, car.bodyY, car.z, car.heading], 16);
    P.set([car.halfExt[0], car.halfExt[1], car.halfExt[2], 0], 20);
    P.set([car.vx, 0, car.vz, car.w], 24);
    if (this.tornado) { const T = this.tornado; P.set([T.x, T.z, T.vMax * T.strength, T.coreR], 32); P.set([T.updraft * T.strength, 0, 0, 0], 36); }
    if (this.blast) { P.set([this.blast[0], this.blast[1], this.blast[2], this.blast[3] / this.substeps], 28); this.blast = null; }
    this.device.queue.writeBuffer(this.ubo, 0, P);
  }

  encode(enc) {
    const pass = enc.beginComputePass({ label: 'sand' });
    const pw = Math.ceil(SAND_MAX / 64), gw = Math.ceil(this.cells / 64);
    for (let s = 0; s < this.substeps; s++) {
      for (const [k, n] of [['clear', gw], ['p2g1', pw], ['p2g2', pw], ['grid', gw], ['g2p', pw]]) {
        pass.setPipeline(this.pipe[k]); pass.setBindGroup(0, this.bg[k]); pass.dispatchWorkgroups(n);
      }
    }
    pass.end();
  }

  draw(pass) {
    pass.setPipeline(this.renderPipe);
    pass.setBindGroup(0, this.renderBG);
    pass.draw(6, SAND_MAX);
  }
}

// Tornado: wandering Rankine vortex (solid-body core + 1/r outer flow) with inflow near the ground and
// an updraft inside the funnel. Drives: GPU dust/debris particles (this file), the smoke grid, the MPM sand,
// the car (CPU force) and the crates (CPU rigid lift + fling).
import { TIER } from './tier.js?v=18';
export const TORNADO_MAX = TIER.tornado;
const STRIDE = 20; // same Particle layout as sand => can reuse the sand billboard renderer

const SRC = /* wgsl */`
struct Particle { pos: vec4f, vel: vec4f, c0: vec4f, c1: vec4f, c2: vec4f };
struct TP { t0: vec4f, t1: vec4f };   // t0: x, z, vMax, coreR   t1: updraft, height, dt, time
@group(0) @binding(0) var<uniform> tp: TP;
@group(0) @binding(1) var<storage, read_write> ps: array<Particle>;

fn hash(n: f32) -> f32 { return fract(sin(n) * 43758.5453); }

// funnel radius widens with height
fn coreAt(y: f32) -> f32 { return tp.t0.w * (0.55 + y * 0.09); }

fn windAt(p: vec3f) -> vec3f {
  let rel = p.xz - tp.t0.xy;
  let r = max(length(rel), 0.05);
  let rc = coreAt(p.y);
  let vt = tp.t0.z * select(rc / r, r / rc, r < rc);          // Rankine vortex
  let tang = vec2f(-rel.y, rel.x) / r;
  let ground = exp(-p.y / 3.0);
  let inflow = -rel / r * tp.t0.z * 0.45 * ground * clamp(r / rc, 0.0, 1.5);
  let up = tp.t1.x * exp(-pow(r / (rc * 1.4), 2.0)) + tp.t1.x * 0.15 * exp(-r / (rc * 3.0));
  let xz = tang * vt + inflow;
  return vec3f(xz.x, up, xz.y);
}

@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  let i = g.x;
  if (i >= arrayLength(&ps)) { return; }
  var p = ps[i];
  let dt = tp.t1.z;
  let seed = f32(i) * 0.6180339 + 1.0;
  let rel = p.pos.xz - tp.t0.xy;
  let r = length(rel);
  let dead = p.pos.w <= 0.0 || p.pos.y > tp.t1.y * (0.6 + 0.4 * hash(seed)) || r > coreAt(p.pos.y) * 4.0 || tp.t0.z <= 0.0;
  if (dead) {
    if (tp.t0.z <= 0.0) { p.pos.w = 0.0; ps[i] = p; return; }
    // respawn on a ring at the base (sand sucked off the ground)
    let k = hash(seed + tp.t1.w * 1.37);
    let a = k * 6.2831;
    let rr = tp.t0.w * (0.4 + 2.2 * hash(seed * 3.1 + tp.t1.w));
    p.pos = vec4f(tp.t0.x + cos(a) * rr, 0.05 + hash(seed * 7.7 + tp.t1.w) * 1.2, tp.t0.y + sin(a) * rr, 10.0);
    p.vel = vec4f(0.0, 0.0, 0.0, p.vel.w);
    if (p.vel.w == 0.0) { p.vel.w = hash(seed * 11.3) * 100.0; }
    // size multiplier: mostly dust, some bigger debris
    let big = hash(seed * 5.9);
    p.c0 = vec4f(0.0, 0.0, 0.0, select(1.0 + big * 1.2, 2.5 + big * 2.5, big > 0.95));
    p.c1 = vec4f(0.0); p.c2 = vec4f(0.0);
  }
  // particles lag behind the wind (heavier = more lag), plus turbulence
  let w = windAt(p.pos.xyz);
  let heavy = select(1.0, 0.35, p.c0.w > 2.4);
  let turb = vec3f(hash(seed + tp.t1.w * 3.0) - 0.5, hash(seed * 2.0 + tp.t1.w * 3.0) - 0.5, hash(seed * 4.0 + tp.t1.w * 3.0) - 0.5) * 6.0;
  var v = p.vel.xyz;
  v += (w + turb - v) * clamp(dt * 3.5 * heavy, 0.0, 1.0);
  v.y -= 9.81 * dt * (1.0 - heavy * 0.7);
  var pos = p.pos.xyz + v * dt;
  if (pos.y < 0.02) { pos.y = 0.02; v.y = abs(v.y) * 0.2; }
  p.pos = vec4f(pos, p.pos.w);
  p.vel = vec4f(v, p.vel.w);
  ps[i] = p;
}
`;

export class Tornado {
  constructor(device, checkModule, sandRenderPipe, camUbo) {
    this.device = device;
    this.x = 40; this.z = 30; this.heading = Math.random() * 6.28;
    this.speed = 4; this.vMax = 24; this.coreR = 3.2; this.updraft = 16; this.height = 28;
    this.time = 0; this.strength = 1;
    this.pbuf = device.createBuffer({ size: TORNADO_MAX * STRIDE * 4, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST, label: 'tornadoParticles' });
    this.bytes = TORNADO_MAX * STRIDE * 4;
    this.ubo = device.createBuffer({ size: 32, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, label: 'tornadoParams' });
    const module = checkModule(device.createShaderModule({ code: SRC, label: 'tornado' }), 'tornado');
    this.pipe = device.createComputePipeline({ layout: 'auto', compute: { module, entryPoint: 'main' }, label: 'tornado' });
    this.bg = device.createBindGroup({ layout: this.pipe.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: this.ubo } }, { binding: 1, resource: { buffer: this.pbuf } }] });
    this.renderPipe = sandRenderPipe;
    this.renderBG = device.createBindGroup({ layout: sandRenderPipe.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: camUbo } }, { binding: 1, resource: { buffer: this.pbuf } }] });
  }

  // CPU copy of the wind field (for car + crates); same formula as the shader
  wind(x, y, z) {
    const rx = x - this.x, rz = z - this.z, r = Math.max(Math.hypot(rx, rz), 0.05);
    const rc = this.coreR * (0.55 + y * 0.09);
    const V = this.vMax * this.strength;
    const vt = V * (r < rc ? r / rc : rc / r);
    const ground = Math.exp(-y / 3);
    const inflow = V * 0.45 * ground * Math.min(1.5, r / rc);
    const up = this.updraft * this.strength * (Math.exp(-((r / (rc * 1.4)) ** 2)) + 0.15 * Math.exp(-r / (rc * 3)));
    return { x: (-rz / r) * vt - (rx / r) * inflow, y: up, z: (rx / r) * vt - (rz / r) * inflow, r, rc };
  }

  update(dt, car, strength) {
    this.strength = strength;
    this.time += dt;
    // random wander: smooth random turning + slow speed changes; steer back if it strays too far
    this.heading += (Math.sin(this.time * 0.37) * 0.6 + Math.sin(this.time * 0.13 + 2) * 0.5 + (Math.random() - 0.5) * 0.8) * dt;
    this.speed = 3.5 + 2 * Math.sin(this.time * 0.21);
    const dx = car.x - this.x, dz = car.z - this.z, d = Math.hypot(dx, dz);
    if (d > 55) { const want = Math.atan2(dx, dz); let dh = want - this.heading; dh = Math.atan2(Math.sin(dh), Math.cos(dh)); this.heading += dh * Math.min(1, dt * 0.8); }
    this.x += Math.sin(this.heading) * this.speed * dt;
    this.z += Math.cos(this.heading) * this.speed * dt;
    const B = 130;
    if (Math.abs(this.x) > B || Math.abs(this.z) > B) { this.heading += Math.PI * dt; this.x = Math.max(-B, Math.min(B, this.x)); this.z = Math.max(-B, Math.min(B, this.z)); }
    this.device.queue.writeBuffer(this.ubo, 0, new Float32Array([this.x, this.z, this.vMax * strength, this.coreR, this.updraft * strength, this.height, 1 / 60, this.time % 1000]));
  }

  encode(enc) {
    const p = enc.beginComputePass({ label: 'tornado' });
    p.setPipeline(this.pipe); p.setBindGroup(0, this.bg); p.dispatchWorkgroups(Math.ceil(TORNADO_MAX / 64));
    p.end();
  }
  draw(pass) {
    pass.setPipeline(this.renderPipe); pass.setBindGroup(0, this.renderBG); pass.draw(6, TORNADO_MAX);
  }
}

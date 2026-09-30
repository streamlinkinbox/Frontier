// Puddles (GTX): per-puddle GPU wave-equation surface (tyres push the water -> bow waves + wakes)
// + 3D splash particles thrown by the tyres (rooster tail + side sheets) that collide with the
// spinning tyres and the car body box. Clear and muddy water have different look/viscosity.

export const WATER_N = 128;           // surface cells per side, per puddle
export const SPLASH_MAX = 32768;      // splash droplets (8k per wheel)
const NP = 3;
const UBO_VEC4 = 20;

// shoreline: irregular radius so puddles look natural (mirrored exactly on the GPU)
export function shoreR(p, ang) {
  return p.r * (1 + 0.12 * Math.sin(3 * ang + p.seed) + 0.07 * Math.sin(5 * ang + p.seed * 2.1) + 0.04 * Math.sin(9 * ang + p.seed * 0.7));
}
export function puddleAt(puddles, x, z) {
  for (let i = 0; i < puddles.length; i++) {
    const p = puddles[i], dx = x - p.x, dz = z - p.z;
    const d = Math.hypot(dx, dz);
    if (d < p.r * 1.3 && d < shoreR(p, Math.atan2(dz, dx))) return i;
  }
  return -1;
}

const COMMON = /* wgsl */`
struct W {
  misc: vec4f,                 // dt, time, N, particleCount
  pud: array<vec4f, 3>,        // x, z, radius, mud(0..1)
  wP: array<vec4f, 4>,         // wheel x, spin(rad/s), z, emit rate (particles/s)
  wV: array<vec4f, 4>,         // contact-patch vel x, 0, z, tread speed (m/s)
  wF: array<vec4f, 4>,         // wheel fwd x, 0, z, puddle index (-1 = dry)
  carC: vec4f, carH: vec4f, carV: vec4f,
  misc2: vec4f,                // splashGain, waveGain, wheelW, wheelR
};
@group(0) @binding(0) var<uniform> U: W;
const N: i32 = ${WATER_N};
fn pudSeed(i: i32) -> f32 { return f32(i) * 2.7 + 1.3; }
fn shore(i: i32, q: vec2f) -> f32 {
  let p = U.pud[i]; let a = atan2(q.y, q.x); let s = pudSeed(i);
  return p.z * (1.0 + 0.12 * sin(3.0 * a + s) + 0.07 * sin(5.0 * a + s * 2.1) + 0.04 * sin(9.0 * a + s * 0.7));
}
fn ext(i: i32) -> f32 { return U.pud[i].z * 1.35; }
fn cellPos(i: i32, x: i32, z: i32) -> vec2f {
  let e = ext(i); let dx = 2.0 * e / f32(N);
  return U.pud[i].xy + (vec2f(f32(x), f32(z)) + 0.5) * dx - e;
}
fn hIdx(i: i32, x: i32, z: i32) -> u32 {
  return u32(i * N * N + clamp(z, 0, N - 1) * N + clamp(x, 0, N - 1));
}
fn hash(n: u32) -> f32 {
  var x = n * 747796405u + 2891336453u;
  x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
  return f32((x >> 22u) ^ x) / 4294967295.0;
}
`;

const WAVE = COMMON + /* wgsl */`
@group(0) @binding(1) var<storage, read> hA: array<vec2f>;
@group(0) @binding(2) var<storage, read_write> hB: array<vec2f>;
fn inside(i: i32, x: i32, z: i32) -> bool {
  let q = cellPos(i, x, z) - U.pud[i].xy;
  return length(q) < shore(i, q);
}
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  let x = i32(g.x); let z = i32(g.y); let i = i32(g.z);
  if (x >= N || z >= N || i >= 3) { return; }
  let id = hIdx(i, x, z);
  if (!inside(i, x, z)) { hB[id] = vec2f(0.0); return; }
  let dt = U.misc.x;
  let mud = U.pud[i].w;
  let e = ext(i); let dx = 2.0 * e / f32(N);
  var s = hA[id];
  let h = s.x;
  // reflective shoreline: outside neighbours mirror the centre height
  var lap = -4.0 * h;
  var nb = array<vec2<i32>, 4>(vec2<i32>(1, 0), vec2<i32>(-1, 0), vec2<i32>(0, 1), vec2<i32>(0, -1));
  for (var k = 0; k < 4; k++) {
    let o = vec2<i32>(x, z) + nb[k];
    lap += select(h, hA[hIdx(i, o.x, o.y)].x, inside(i, o.x, o.y));
  }
  let c = mix(1.4, 0.8, mud);                         // shallow-water wave speed (mud is slower/viscous)
  s.y += c * c * lap / (dx * dx) * dt;
  s.y *= 1.0 - dt * mix(1.2, 4.5, mud);                // damping
  // tyres: depress the water under the contact patch and heap a bow wave in front of it
  let wp = cellPos(i, x, z);
  for (var w = 0; w < 4; w++) {
    if (i32(U.wF[w].w) != i) { continue; }
    let f = U.wF[w].xz; let r = vec2f(f.y, -f.x);
    let q = wp - U.wP[w].xz;
    let a = dot(q, f); let sd = dot(q, r);
    let sp = length(U.wV[w].xz);
    let foot = exp(-(a * a / 0.06 + sd * sd / 0.012));
    let bow = exp(-((a - 0.4) * (a - 0.4) / 0.04 + sd * sd / 0.05));
    s.y += (-foot * (1.5 + sp * 1.2) + bow * sp * 0.5) * dt * U.misc2.y;
  }
  s.x = clamp(h + s.y * dt, -0.07, 0.09);
  hB[id] = s;
}`;

const SPLASH = COMMON + /* wgsl */`
struct Pt { p: vec4f, v: vec4f };    // pos + life, vel + (mud*1 + seed)
@group(0) @binding(1) var<storage, read_write> P: array<Pt>;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  let idx = g.x;
  if (idx >= u32(U.misc.w)) { return; }
  let dt = U.misc.x;
  var pt = P[idx];
  let R = U.misc2.w; let HW = U.misc2.z * 0.5;
  if (pt.p.w <= 0.0) {
    // stateless emission: particle idx belongs to wheel idx&3 and respawns with p = rate*dt/quota
    let w = i32(idx & 3u);
    let pi = i32(U.wF[w].w);
    let rate = U.wP[w].w;
    let seed = idx * 9781u + u32(U.misc.y * 1000.0) * 6271u;
    if (pi < 0 || rate <= 0.0 || hash(seed) > rate * dt / (U.misc.w * 0.25)) { P[idx] = pt; return; }
    let f = U.wF[w].xyz; let r = vec3f(f.z, 0.0, -f.x);
    let vel = U.wV[w].xyz; let sp = length(vel.xz);
    let tread = U.wV[w].w;
    let k = vel + f * tread;                           // what the tyre surface throws backwards
    let side = select(-1.0, 1.0, hash(seed + 1u) > 0.5);
    let h2 = hash(seed + 2u); let h3 = hash(seed + 3u); let h4 = hash(seed + 4u);
    var v: vec3f;
    var pos = vec3f(U.wP[w].x, 0.04, U.wP[w].z) + r * side * HW * (0.6 + 0.6 * h4);
    if (hash(seed + 5u) < 0.55) {
      // side sheet: water pushed out sideways + up by the tyre ploughing through
      v = vel * (0.35 + 0.35 * h2) + r * side * (0.6 + sp * (0.18 + 0.25 * h3)) + vec3f(0.0, 0.8 + sp * (0.12 + 0.22 * h2), 0.0);
      pos += f * (0.2 + 0.2 * h3);
    } else {
      // rooster tail: flung off the tread behind the wheel (bigger with wheelspin)
      v = k * (0.35 + 0.45 * h2) + vec3f(0.0, 1.0 + length(k) * (0.18 + 0.3 * h3), 0.0) + r * (h4 - 0.5) * 1.5;
      pos -= normalize(k + vec3f(1e-4)) * (0.3 + 0.1 * h3);
    }
    pt.p = vec4f(pos, 0.6 + 1.2 * hash(seed + 6u));
    pt.v = vec4f(v, f32(pi) * 4.0 + U.pud[pi].w * 2.0 + hash(seed + 7u) * 0.99);
    P[idx] = pt; return;
  }
  var p = pt.p.xyz; var v = pt.v.xyz;
  v.y -= 9.81 * dt;
  v *= 1.0 - 0.35 * dt;                                // air drag
  p += v * dt;
  // spinning tyres: push out of the cylinder, pick up the tread's surface velocity
  for (var w = 0; w < 4; w++) {
    let f = U.wF[w].xyz; let ax = vec3f(f.z, 0.0, -f.x);
    let c = vec3f(U.wP[w].x, R, U.wP[w].z);
    let q = p - c; let al = dot(q, ax); let rad = q - ax * al; let rl = length(rad);
    if (abs(al) < HW && rl < R && rl > 1e-4) {
      let n = rad / rl;
      p = c + ax * al + n * R * 1.02;
      let surf = vec3f(U.wV[w].x, 0.0, U.wV[w].z) + cross(ax * U.wP[w].y, n * R) * 0.9;
      var rel = v - surf; let vn = dot(rel, n);
      if (vn < 0.0) { rel -= 1.25 * vn * n; }
      v = surf + rel * 0.85;
    }
  }
  // car body (oriented box)
  let hd = U.carC.w; let cs = cos(hd); let sn = sin(hd);
  let d = p - U.carC.xyz;
  let l = vec3f(d.x * cs - d.z * sn, d.y, d.x * sn + d.z * cs);
  let pen = U.carH.xyz - abs(l);
  if (all(pen > vec3f(0.0))) {
    var nl = vec3f(0.0);
    if (pen.x < pen.y && pen.x < pen.z) { nl.x = sign(l.x); } else if (pen.y < pen.z) { nl.y = sign(l.y); } else { nl.z = sign(l.z); }
    let lp = l + nl * (dot(pen, abs(nl)) + 0.01);
    p = U.carC.xyz + vec3f(lp.x * cs + lp.z * sn, lp.y, -lp.x * sn + lp.z * cs);
    let n = vec3f(nl.x * cs + nl.z * sn, nl.y, -nl.x * sn + nl.z * cs);
    let bv = vec3f(U.carV.x + U.carV.w * d.z, 0.0, U.carV.z - U.carV.w * d.x);
    var rel = v - bv; let vn = dot(rel, n);
    if (vn < 0.0) { rel -= 1.3 * vn * n; }
    v = bv + rel * 0.7;                                // droplets run off the panels
  }
  var life = pt.p.w - dt;
  if (p.y < 0.03) { life = 0.0; }                      // back into the puddle / soaked into sand
  pt.p = vec4f(p, life); pt.v = vec4f(v, pt.v.w);
  P[idx] = pt;
}`;

const RENDER_COMMON = COMMON + /* wgsl */`
struct Cam { viewProj: mat4x4f, invViewProj: mat4x4f, camPos: vec4f, lightDir: vec4f };
@group(0) @binding(1) var<uniform> cam: Cam;
fn sky(d: vec3f) -> vec3f { return mix(vec3f(0.78, 0.8, 0.82), vec3f(0.42, 0.6, 0.86), clamp(d.y * 1.6, 0.0, 1.0)); }
`;

const SURFACE = RENDER_COMMON + /* wgsl */`
@group(0) @binding(2) var<storage, read> H: array<vec2f>;
struct VO { @builtin(position) pos: vec4f, @location(0) wp: vec3f, @location(1) n: vec3f, @location(2) info: vec3f };
@vertex fn vs(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VO {
  let i = i32(ii);
  let quad = i32(vi / 6u); let corner = vi % 6u;
  let cx = quad % (N - 1); let cz = quad / (N - 1);
  var offs = array<vec2<i32>, 6>(vec2<i32>(0,0), vec2<i32>(1,0), vec2<i32>(1,1), vec2<i32>(0,0), vec2<i32>(1,1), vec2<i32>(0,1));
  let x = cx + offs[corner].x; let z = cz + offs[corner].y;
  let e = ext(i); let dx = 2.0 * e / f32(N);
  let h = H[hIdx(i, x, z)].x;
  let hx = H[hIdx(i, x + 1, z)].x - H[hIdx(i, x - 1, z)].x;
  let hz = H[hIdx(i, x, z + 1)].x - H[hIdx(i, x, z - 1)].x;
  let p2 = cellPos(i, x, z);
  let q = p2 - U.pud[i].xy;
  var o: VO;
  o.wp = vec3f(p2.x, 0.035 + h, p2.y);
  o.pos = cam.viewProj * vec4f(o.wp, 1.0);
  o.n = normalize(vec3f(-hx / (2.0 * dx), 1.0, -hz / (2.0 * dx)));
  o.info = vec3f(length(q) / shore(i, q), U.pud[i].w, length(vec2f(hx, hz)) / dx);
  return o;
}
@fragment fn fs(v: VO) -> @location(0) vec4f {
  let d = v.info.x; let mud = v.info.y;
  if (d > 1.12) { discard; }
  if (d > 1.0) { return vec4f(0.0, 0.0, 0.0, 0.3 * (1.0 - (d - 1.0) / 0.12)); }   // wet sand rim
  let L = normalize(cam.lightDir.xyz);
  let V = normalize(cam.camPos.xyz - v.wp);
  let n = normalize(mix(v.n, vec3f(0.0, 1.0, 0.0), 0.15));
  let fres = 0.02 + 0.98 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
  let R = reflect(-V, n);
  let spec = pow(max(dot(R, L), 0.0), mix(240.0, 60.0, mud)) * mix(4.0, 0.8, mud);
  let diff = max(dot(n, L), 0.0);
  let foam = smoothstep(0.25, 0.9, v.info.z) + smoothstep(0.015, 0.05, v.wp.y - 0.035);
  // clear: transparent, darkens the sand below, mirror-like sky; mud: opaque brown, dull
  let clearCol = vec3f(0.05, 0.08, 0.08) + sky(R) * fres + vec3f(1.0, 0.95, 0.85) * spec;
  let clearA = 0.3 + 0.65 * fres;
  let mudBase = vec3f(0.30, 0.22, 0.14) * (0.45 + 0.75 * diff);
  let mudCol = mudBase * (1.0 - fres * 0.5) + sky(R) * fres * 0.5 + vec3f(1.0, 0.95, 0.85) * spec;
  var col = mix(clearCol, mudCol * 0.95, mud);
  var a = mix(clearA, 0.95, mud);
  let foamCol = mix(vec3f(0.92, 0.95, 0.96), vec3f(0.55, 0.43, 0.3), mud) * (0.55 + 0.5 * diff);
  let fm = clamp(foam, 0.0, 0.85);
  col = mix(col, foamCol * max(a, 0.7), fm); a = mix(a, max(a, 0.7), fm);
  let edge = smoothstep(1.0, 0.9, d);                  // shallow edge fades to the wet rim
  a = mix(0.3, a, edge); col *= mix(0.2, 1.0, edge);
  let fog = 1.0 - exp(-distance(cam.camPos.xyz, v.wp) * 0.006);
  col = mix(col, vec3f(0.78, 0.8, 0.82) * a, fog);
  return vec4f(col, a);
}`;

const DROPS = RENDER_COMMON + /* wgsl */`
struct Pt { p: vec4f, v: vec4f };
@group(0) @binding(2) var<storage, read> P: array<Pt>;
struct VO { @builtin(position) pos: vec4f, @location(0) uv: vec2f, @location(1) info: vec3f };
@vertex fn vs(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VO {
  var o: VO;
  let pt = P[ii];
  if (pt.p.w <= 0.0) { o.pos = vec4f(2.0, 2.0, 2.0, 1.0); return o; }
  var corners = array<vec2f, 6>(vec2f(-1,-1), vec2f(1,-1), vec2f(1,1), vec2f(-1,-1), vec2f(1,1), vec2f(-1,1));
  let c = corners[vi];
  let seed = fract(pt.v.w);
  let mud = fract(floor(pt.v.w) * 0.25) * 2.0;         // (pi*4 + mud*2) -> mud flag
  let size = 0.02 + 0.035 * seed;
  let vel = pt.v.xyz; let sp = length(vel);
  let toCam = normalize(cam.camPos.xyz - pt.p.xyz);
  var a1 = select(vec3f(0.0, 1.0, 0.0), vel / sp, sp > 1e-3);
  var a2 = cross(a1, toCam);
  if (length(a2) < 1e-3) { a2 = vec3f(1.0, 0.0, 0.0); }
  a2 = normalize(a2);
  let wp = pt.p.xyz + a1 * c.y * (size + sp * 0.012) + a2 * c.x * size;   // motion-stretched droplet
  o.pos = cam.viewProj * vec4f(wp, 1.0);
  o.uv = c;
  o.info = vec3f(clamp(mud, 0.0, 1.0), min(pt.p.w * 3.0, 1.0), seed);
  return o;
}
@fragment fn fs(v: VO) -> @location(0) vec4f {
  let r2 = dot(v.uv, v.uv);
  if (r2 > 1.0) { discard; }
  let L = normalize(cam.lightDir.xyz);
  let nz = sqrt(1.0 - r2);
  let n = vec3f(v.uv.x, v.uv.y, nz);
  let rim = pow(1.0 - nz, 2.0);
  let mud = v.info.x;
  let clearC = vec3f(0.72, 0.8, 0.86) * (0.5 + 0.5 * rim) + vec3f(1.0) * pow(max(dot(n, normalize(vec3f(0.3, 0.5, 1.0))), 0.0), 30.0) * 0.8;
  let mudC = vec3f(0.33, 0.24, 0.15) * (0.6 + 0.5 * max(L.y, 0.0)) * (1.0 - 0.3 * rim);
  let col = mix(clearC, mudC, mud);
  let a = mix(0.35 + 0.4 * rim, 0.92, mud) * v.info.y * smoothstep(1.0, 0.6, r2);
  return vec4f(col * a, a);
}`;

export class Water {
  constructor(device, format, checkModule, puddles, camUbo) {
    this.device = device;
    this.puddles = puddles;
    const nH = NP * WATER_N * WATER_N;
    const st = GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST;
    this.hA = device.createBuffer({ size: nH * 8, usage: st, label: 'waterHA' });
    this.hB = device.createBuffer({ size: nH * 8, usage: st, label: 'waterHB' });
    this.pts = device.createBuffer({ size: SPLASH_MAX * 32, usage: st, label: 'splash' });
    this.bytes = nH * 16 + SPLASH_MAX * 32;
    this.u = new Float32Array(UBO_VEC4 * 4);
    this.ubo = device.createBuffer({ size: this.u.byteLength, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, label: 'waterU' });
    this.time = 0;

    const cp = (code, label) => device.createComputePipeline({ layout: 'auto', label, compute: { module: checkModule(device.createShaderModule({ code, label }), label), entryPoint: 'main' } });
    this.wavePipe = cp(WAVE, 'water-wave');
    this.splashPipe = cp(SPLASH, 'water-splash');
    const bg = (pipe, bufs) => device.createBindGroup({ layout: pipe.getBindGroupLayout(0), entries: bufs.map((b, i) => ({ binding: i, resource: { buffer: b } })) });
    this.waveAB = bg(this.wavePipe, [this.ubo, this.hA, this.hB]);
    this.waveBA = bg(this.wavePipe, [this.ubo, this.hB, this.hA]);
    this.splashBG = bg(this.splashPipe, [this.ubo, this.pts]);

    const blend = { color: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' }, alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' } };
    const rp = (code, label) => {
      const module = checkModule(device.createShaderModule({ code, label }), label);
      return device.createRenderPipeline({
        layout: 'auto', label,
        vertex: { module, entryPoint: 'vs' },
        fragment: { module, entryPoint: 'fs', targets: [{ format, blend }] },
        primitive: { topology: 'triangle-list', cullMode: 'none' },
        depthStencil: { format: 'depth32float', depthWriteEnabled: false, depthCompare: 'less' },
      });
    };
    this.surfPipe = rp(SURFACE, 'water-surface');
    this.dropPipe = rp(DROPS, 'water-drops');
    this.surfBG = bg(this.surfPipe, [this.ubo, camUbo, this.hA]);
    this.dropBG = bg(this.dropPipe, [this.ubo, camUbo, this.pts]);
  }

  // wet[i] = puddle index for wheel i (or -1); returns nothing, fills uniforms
  update(dt, car, wet, S) {
    this.time += dt;
    const u = this.u; u.fill(0);
    u.set([dt, this.time, WATER_N, SPLASH_MAX], 0);
    this.puddles.forEach((p, i) => u.set([p.x, p.z, p.r, p.mud], 4 + i * 4));
    car.wheels.forEach((wh, i) => {
      const pi = wet[i];
      const tread = -wh.spinVel * car.wheelR;
      const sp = Math.hypot(wh.vel[0], wh.vel[2]);
      const slip = Math.abs(wh.slipLat) + Math.abs(wh.slipLong);
      const rate = pi >= 0 && (sp > 0.8 || slip > 1) ? (sp * 170 + slip * 90) * (S.splash ?? 1) : 0;
      u.set([wh.pos[0], wh.spinVel, wh.pos[2], rate], 8 + i * 4);
      u.set([wh.vel[0], 0, wh.vel[2], tread], 24 + i * 4);
      u.set([wh.fwd[0], 0, wh.fwd[2], pi], 40 + i * 4);
    });
    u.set([car.x, car.bodyY, car.z, car.heading], 56);
    u.set([car.halfExt[0], car.halfExt[1], car.halfExt[2], 0], 60);
    u.set([car.vx, 0, car.vz, car.w], 64);
    u.set([S.splash ?? 1, S.waves ?? 1, car.wheelW, car.wheelR], 72);
    this.device.queue.writeBuffer(this.ubo, 0, u);
  }

  encode(enc) {
    const p = enc.beginComputePass({ label: 'water' });
    p.setPipeline(this.wavePipe);
    const wg = Math.ceil(WATER_N / 8);
    // two substeps per frame (A->B->A) for a stable, faster-moving wave
    p.setBindGroup(0, this.waveAB); p.dispatchWorkgroups(wg, wg, NP);
    p.setBindGroup(0, this.waveBA); p.dispatchWorkgroups(wg, wg, NP);
    p.setPipeline(this.splashPipe); p.setBindGroup(0, this.splashBG);
    p.dispatchWorkgroups(Math.ceil(SPLASH_MAX / 64));
    p.end();
  }

  draw(pass) {
    pass.setPipeline(this.surfPipe); pass.setBindGroup(0, this.surfBG);
    pass.draw((WATER_N - 1) * (WATER_N - 1) * 6, NP);
    pass.setPipeline(this.dropPipe); pass.setBindGroup(0, this.dropBG);
    pass.draw(6, SPLASH_MAX);
  }
}

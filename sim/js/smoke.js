// GTX tier: dense 3D Eulerian smoke grid that follows the car.
// Stable Fluids + MacCormack advection + vorticity confinement + Jacobi pressure.
// All fields are storage buffers (no read-write storage textures => works on core WebGPU everywhere).

const LOW = new URLSearchParams(globalThis.location?.search || '').has('lowres');
export const SMOKE_DIMS = LOW ? [64, 32, 64] : [128, 64, 128];
export const SMOKE_H = LOW ? 0.5 : 0.25; // metres per cell  => 32 x 16 x 32 m domain either way
const WG = [4, 4, 4];
export const MAX_CRATES = 8;

// Uniform layout (vec4 units) — keep in sync with writeParams()
//  0 dims(i32)  1 shift(i32)  2 origin.xyz,h  3 dt,time,vort,buoy
//  4 carC.xyz,heading  5 carH.xyz  6 carV.xyz,w  7 densDecay,tempDecay,velDecay,weight
//  8..11 em[4] (xyz, strength)  12..15 emV[4] (xyz vel, radius)
//  16..23 crate[8] (x,z,yaw,active)  24..31 crateS[8] (hx,hy,hz,_)
const PARAM_VEC4 = 32;

export const SMOKE_COMMON = /* wgsl */`
struct P {
  dims: vec4<i32>, shift: vec4<i32>, origin: vec4f, misc: vec4f,
  carC: vec4f, carH: vec4f, carV: vec4f, diss: vec4f,
  em: array<vec4f, 4>, emV: array<vec4f, 4>,
  box: array<vec4f, ${MAX_CRATES}>, boxS: array<vec4f, ${MAX_CRATES}>,
};
@group(0) @binding(0) var<uniform> prm: P;

fn cellIdx(c: vec3<i32>) -> u32 {
  let d = prm.dims.xyz;
  let q = clamp(c, vec3<i32>(0), d - vec3<i32>(1));
  return u32(q.x + d.x * (q.y + d.y * q.z));
}
fn inDomain(c: vec3<i32>) -> bool {
  return all(c >= vec3<i32>(0)) && all(c < prm.dims.xyz);
}
fn cellWorld(c: vec3<i32>) -> vec3f {
  return prm.origin.xyz + (vec3f(c) + vec3f(0.5)) * prm.origin.w;
}
// returns (solid velocity xyz, solid flag)
fn solidAt(wp: vec3f) -> vec4f {
  if (wp.y < 0.0) { return vec4f(0.0, 0.0, 0.0, 1.0); }
  let h = prm.carC.w;
  let d = wp - prm.carC.xyz;
  let cs = cos(h); let sn = sin(h);
  let lx = d.x * cs - d.z * sn;
  let lz = d.x * sn + d.z * cs;
  if (abs(lx) < prm.carH.x && abs(d.y) < prm.carH.y && abs(lz) < prm.carH.z) {
    let w = prm.carV.w; // yaw rate: v + w x r
    return vec4f(prm.carV.x + w * d.z, 0.0, prm.carV.z - w * d.x, 1.0);
  }
  for (var i = 0; i < ${MAX_CRATES}; i++) {
    let cr = prm.box[i];
    if (cr.w < 0.5) { continue; }
    let s = prm.boxS[i];
    let e = wp - vec3f(cr.x, s.y, cr.y);
    let c2 = cos(cr.z); let s2 = sin(cr.z);
    let ex = e.x * c2 - e.z * s2;
    let ez = e.x * s2 + e.z * c2;
    if (abs(ex) < s.x && abs(e.y) < s.y && abs(ez) < s.z) { return vec4f(0.0, 0.0, 0.0, 1.0); }
  }
  return vec4f(0.0);
}
`;

// Generates a trilinear sampler over a vec4 storage buffer. g = continuous cell coords (centres at ints).
const sampler = (fn, buf) => /* wgsl */`
fn ${fn}(g: vec3f) -> vec4f {
  let b = floor(g); let f = g - b; let i = vec3<i32>(b);
  let c000 = ${buf}[cellIdx(i)];
  let c100 = ${buf}[cellIdx(i + vec3<i32>(1,0,0))];
  let c010 = ${buf}[cellIdx(i + vec3<i32>(0,1,0))];
  let c110 = ${buf}[cellIdx(i + vec3<i32>(1,1,0))];
  let c001 = ${buf}[cellIdx(i + vec3<i32>(0,0,1))];
  let c101 = ${buf}[cellIdx(i + vec3<i32>(1,0,1))];
  let c011 = ${buf}[cellIdx(i + vec3<i32>(0,1,1))];
  let c111 = ${buf}[cellIdx(i + vec3<i32>(1,1,1))];
  return mix(mix(mix(c000, c100, f.x), mix(c010, c110, f.x), f.y),
             mix(mix(c001, c101, f.x), mix(c011, c111, f.x), f.y), f.z);
}`;

const HEAD = /* wgsl */`
@compute @workgroup_size(${WG[0]}, ${WG[1]}, ${WG[2]})
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let c = vec3<i32>(gid);
  if (!inDomain(c)) { return; }
  let id = cellIdx(c);
`;

const SRC = {
  obstacle: `
@group(0) @binding(1) var<storage, read_write> solid: array<vec4f>;
${HEAD}
  solid[id] = solidAt(cellWorld(c));
}`,

  // forward semi-Lagrangian step into T buffers (old frame shifted by prm.shift)
  advect: `
@group(0) @binding(1) var<storage, read> velS: array<vec4f>;
@group(0) @binding(2) var<storage, read> denS: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> velT: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> denT: array<vec4f>;
@group(0) @binding(5) var<storage, read> solid: array<vec4f>;
${sampler('sVel', 'velS')}
${sampler('sDen', 'denS')}
${HEAD}
  let dt = prm.misc.x; let h = prm.origin.w;
  let gO = vec3f(c + prm.shift.xyz);
  let v = velS[cellIdx(c + prm.shift.xyz)].xyz;
  let back = gO - v * dt / h;
  let dmax = vec3f(prm.dims.xyz) - vec3f(0.5);
  var nv = sVel(back);
  var nd = sDen(back);
  if (any(back < vec3f(-0.5)) || any(back > dmax)) { nd = vec4f(0.0); nv = vec4f(0.0); }
  let s = solid[id];
  if (s.w > 0.5) { nv = vec4f(s.xyz, 0.0); nd = vec4f(0.0); }
  velT[id] = nv;
  denT[id] = nd;
}`,

  // MacCormack correction with min/max limiter, then dissipation
  maccormack: `
@group(0) @binding(1) var<storage, read> velS: array<vec4f>;
@group(0) @binding(2) var<storage, read> denS: array<vec4f>;
@group(0) @binding(3) var<storage, read> velT: array<vec4f>;
@group(0) @binding(4) var<storage, read> denT: array<vec4f>;
@group(0) @binding(5) var<storage, read_write> velD: array<vec4f>;
@group(0) @binding(6) var<storage, read_write> denD: array<vec4f>;
@group(0) @binding(7) var<storage, read> solid: array<vec4f>;
${sampler('sVelT', 'velT')}
${sampler('sDenT', 'denT')}
${HEAD}
  let dt = prm.misc.x; let h = prm.origin.w;
  let cO = c + prm.shift.xyz;
  let v = velS[cellIdx(cO)].xyz;
  let s = solid[id];
  if (s.w > 0.5) { velD[id] = vec4f(s.xyz, 0.0); denD[id] = vec4f(0.0); return; }
  // forward-trace the corrected-to field back again (in new frame coords)
  let fwd = vec3f(c) + v * dt / h;
  let backV = sVelT(fwd);
  let backD = sDenT(fwd);
  var nv = velT[id] + 0.5 * (velS[cellIdx(cO)] - backV);
  var nd = denT[id] + 0.5 * (denS[cellIdx(cO)] - backD);
  // limiter: clamp to the source neighbourhood at the departure point
  let dep = vec3f(cO) - v * dt / h;
  let b = vec3<i32>(floor(dep));
  var vmin = vec4f(1e9); var vmax = vec4f(-1e9);
  var dmin = vec4f(1e9); var dmax = vec4f(-1e9);
  for (var k = 0; k < 8; k++) {
    let o = vec3<i32>(k & 1, (k >> 1) & 1, (k >> 2) & 1);
    let j = cellIdx(b + o);
    let a = velS[j]; let e = denS[j];
    vmin = min(vmin, a); vmax = max(vmax, a);
    dmin = min(dmin, e); dmax = max(dmax, e);
  }
  let tV = velT[id]; let tD = denT[id];
  let dlim = vec3f(prm.dims.xyz) - vec3f(0.5);
  if (any(dep < vec3f(-0.5)) || any(dep > dlim)) { nv = tV; nd = tD; }
  nv = clamp(nv, vmin, vmax);
  nd = clamp(nd, dmin, dmax);
  // dissipation
  nd = vec4f(nd.x * prm.diss.x, nd.y * prm.diss.y, 0.0, 0.0);
  nv = vec4f(nv.xyz * prm.diss.z, 0.0);
  velD[id] = nv;
  denD[id] = max(nd, vec4f(0.0));
}`,

  curl: `
@group(0) @binding(1) var<storage, read> vel: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> curl: array<vec4f>;
${HEAD}
  let h2 = 0.5 / prm.origin.w;
  let L = vel[cellIdx(c - vec3<i32>(1,0,0))].xyz; let R = vel[cellIdx(c + vec3<i32>(1,0,0))].xyz;
  let B = vel[cellIdx(c - vec3<i32>(0,1,0))].xyz; let T = vel[cellIdx(c + vec3<i32>(0,1,0))].xyz;
  let K = vel[cellIdx(c - vec3<i32>(0,0,1))].xyz; let F = vel[cellIdx(c + vec3<i32>(0,0,1))].xyz;
  let w = vec3f((T.z - B.z) - (F.y - K.y), (F.x - K.x) - (R.z - L.z), (R.y - L.y) - (T.x - B.x)) * h2;
  curl[id] = vec4f(w, length(w));
}`,

  forces: `
@group(0) @binding(1) var<storage, read_write> vel: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> den: array<vec4f>;
@group(0) @binding(3) var<storage, read> curl: array<vec4f>;
@group(0) @binding(4) var<storage, read> solid: array<vec4f>;
${HEAD}
  let s = solid[id];
  if (s.w > 0.5) { vel[id] = vec4f(s.xyz, 0.0); den[id] = vec4f(0.0); return; }
  let dt = prm.misc.x; let h = prm.origin.w;
  var v = vel[id].xyz;
  var d = den[id];
  // vorticity confinement
  let wL = curl[cellIdx(c - vec3<i32>(1,0,0))].w; let wR = curl[cellIdx(c + vec3<i32>(1,0,0))].w;
  let wB = curl[cellIdx(c - vec3<i32>(0,1,0))].w; let wT = curl[cellIdx(c + vec3<i32>(0,1,0))].w;
  let wK = curl[cellIdx(c - vec3<i32>(0,0,1))].w; let wF = curl[cellIdx(c + vec3<i32>(0,0,1))].w;
  let eta = vec3f(wR - wL, wT - wB, wF - wK);
  let el = length(eta);
  if (el > 1e-5) {
    let N = eta / el;
    v += prm.misc.z * h * cross(N, curl[id].xyz) * dt;
  }
  // buoyancy (hot smoke rises, dense smoke sinks slightly)
  v.y += (prm.misc.w * d.y - prm.diss.w * d.x) * dt;
  // emitters (tyre contact patches)
  let wp = cellWorld(c);
  for (var i = 0; i < 4; i++) {
    let e = prm.em[i];
    if (e.w <= 0.0) { continue; }
    let r = prm.emV[i].w;
    let q = wp - e.xyz;
    let f = exp(-dot(q, q) / (r * r));
    if (f < 0.01) { continue; }
    d.x += e.w * f * dt * 6.0;
    d.y += e.w * f * dt * 4.0;
    v = mix(v, prm.emV[i].xyz, clamp(f * e.w * dt * 8.0, 0.0, 1.0));
  }
  d.x = min(d.x, 4.0); d.y = min(d.y, 4.0);
  vel[id] = vec4f(v, 0.0);
  den[id] = d;
}`,

  divergence: `
@group(0) @binding(1) var<storage, read> vel: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> div: array<f32>;
@group(0) @binding(3) var<storage, read> solid: array<vec4f>;
fn vn(c: vec3<i32>) -> vec3f {
  if (c.y < 0) { return vec3f(0.0); }
  let j = cellIdx(c);
  let s = solid[j];
  if (s.w > 0.5 && inDomain(c)) { return s.xyz; }
  return vel[j].xyz;
}
${HEAD}
  let L = vn(c - vec3<i32>(1,0,0)).x; let R = vn(c + vec3<i32>(1,0,0)).x;
  let B = vn(c - vec3<i32>(0,1,0)).y; let T = vn(c + vec3<i32>(0,1,0)).y;
  let K = vn(c - vec3<i32>(0,0,1)).z; let F = vn(c + vec3<i32>(0,0,1)).z;
  div[id] = 0.5 / prm.origin.w * ((R - L) + (T - B) + (F - K));
}`,

  jacobi: `
@group(0) @binding(1) var<storage, read> pIn: array<f32>;
@group(0) @binding(2) var<storage, read_write> pOut: array<f32>;
@group(0) @binding(3) var<storage, read> div: array<f32>;
@group(0) @binding(4) var<storage, read> solid: array<vec4f>;
fn pn(c: vec3<i32>, pc: f32) -> f32 {
  if (c.y < 0) { return pc; }                 // ground: Neumann
  if (!inDomain(c)) { return 0.0; }            // open sides/top: Dirichlet
  let j = cellIdx(c);
  if (solid[j].w > 0.5) { return pc; }         // obstacles: Neumann
  return pIn[j];
}
${HEAD}
  if (solid[id].w > 0.5) { pOut[id] = 0.0; return; }
  let pc = pIn[id];
  let s = pn(c - vec3<i32>(1,0,0), pc) + pn(c + vec3<i32>(1,0,0), pc)
        + pn(c - vec3<i32>(0,1,0), pc) + pn(c + vec3<i32>(0,1,0), pc)
        + pn(c - vec3<i32>(0,0,1), pc) + pn(c + vec3<i32>(0,0,1), pc);
  let h = prm.origin.w;
  pOut[id] = (s - h * h * div[id]) / 6.0;
}`,

  project: `
@group(0) @binding(1) var<storage, read> p: array<f32>;
@group(0) @binding(2) var<storage, read_write> vel: array<vec4f>;
@group(0) @binding(3) var<storage, read> solid: array<vec4f>;
fn pn(c: vec3<i32>, pc: f32) -> f32 {
  if (c.y < 0) { return pc; }
  if (!inDomain(c)) { return 0.0; }
  let j = cellIdx(c);
  if (solid[j].w > 0.5) { return pc; }
  return p[j];
}
${HEAD}
  let s = solid[id];
  if (s.w > 0.5) { vel[id] = vec4f(s.xyz, 0.0); return; }
  let pc = p[id];
  let g = vec3f(pn(c + vec3<i32>(1,0,0), pc) - pn(c - vec3<i32>(1,0,0), pc),
                pn(c + vec3<i32>(0,1,0), pc) - pn(c - vec3<i32>(0,1,0), pc),
                pn(c + vec3<i32>(0,0,1), pc) - pn(c - vec3<i32>(0,0,1), pc)) * (0.5 / prm.origin.w);
  var v = vel[id].xyz - g;
  if (c.y == 0 && v.y < 0.0) { v.y = 0.0; }
  vel[id] = vec4f(v, 0.0);
}`,
};

// Volumetric ray-march of the density buffer, composited over the lit scene.
const RENDER_SRC = /* wgsl */`
struct Cam { viewProj: mat4x4f, invViewProj: mat4x4f, camPos: vec4f, lightDir: vec4f, screen: vec4f };
@group(0) @binding(1) var<uniform> cam: Cam;
@group(0) @binding(2) var<storage, read> den: array<vec4f>;
@group(0) @binding(3) var depthTex: texture_depth_2d;
${sampler('sDen', 'den')}

@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
  var p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
  return vec4f(p[i], 0.0, 1.0);
}
fn unproject(ndc: vec3f) -> vec3f {
  let w = cam.invViewProj * vec4f(ndc, 1.0);
  return w.xyz / w.w;
}
fn hash(p: vec2f) -> f32 { return fract(sin(dot(p, vec2f(12.9898, 78.233))) * 43758.5453); }
fn densAt(wp: vec3f) -> f32 {
  let g = (wp - prm.origin.xyz) / prm.origin.w - vec3f(0.5);
  return sDen(g).x;
}

@fragment fn fs(@builtin(position) fc: vec4f) -> @location(0) vec4f {
  let uv = fc.xy / cam.screen.xy;
  let ndc = vec2f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
  let ro = cam.camPos.xyz;
  let farP = unproject(vec3f(ndc, 1.0));
  let rd = normalize(farP - ro);
  let dz = textureLoad(depthTex, vec2<i32>(fc.xy), 0);
  let sceneP = unproject(vec3f(ndc, dz));
  let tScene = select(distance(ro, sceneP), 1e6, dz >= 1.0);

  let h = prm.origin.w;
  let bmin = prm.origin.xyz;
  let bmax = prm.origin.xyz + vec3f(prm.dims.xyz) * h;
  let inv = 1.0 / rd;
  let t0s = (bmin - ro) * inv; let t1s = (bmax - ro) * inv;
  let tmin = min(t0s, t1s); let tmax = max(t0s, t1s);
  let tn = max(max(max(tmin.x, tmin.y), tmin.z), 0.0);
  let tf = min(min(min(tmax.x, tmax.y), tmax.z), tScene);
  if (tf <= tn) { discard; }

  let stepLen = h * 1.1;
  let n = min(i32((tf - tn) / stepLen) + 1, 96);
  var t = tn + stepLen * hash(fc.xy + vec2f(cam.screen.z * 7.0, 0.0));
  var trans = 1.0;
  var col = vec3f(0.0);
  let L = normalize(cam.lightDir.xyz);
  let sun = vec3f(1.0, 0.93, 0.82) * 1.25;
  let amb = vec3f(0.55, 0.62, 0.72);
  let center = (bmin.xz + bmax.xz) * 0.5;
  let halfW = (bmax.xz - bmin.xz) * 0.5;
  for (var i = 0; i < n; i++) {
    if (t > tf || trans < 0.02) { break; }
    let p = ro + rd * t;
    var d = densAt(p);
    // fade at the travelling domain border to hide the cut
    let e = halfW - abs(p.xz - center);
    d *= smoothstep(0.0, 3.0, min(e.x, e.y));
    if (d > 0.003) {
      let sigma = d * 2.2;
      let sh = densAt(p + L * 0.5) + densAt(p + L * 1.4) + 0.5 * densAt(p + L * 2.8);
      let lt = exp(-sh * 1.3);
      let height = clamp(p.y / 6.0, 0.0, 1.0);
      let c = (sun * lt + amb * (0.55 + 0.45 * height)) * vec3f(0.93, 0.92, 0.9);
      let a = 1.0 - exp(-sigma * stepLen);
      col += trans * a * c;
      trans *= 1.0 - a;
    }
    t += stepLen;
  }
  let alpha = 1.0 - trans;
  if (alpha < 0.002) { discard; }
  return vec4f(col, alpha);
}
`;

export class Smoke {
  constructor(device, format, checkModule) {
    this.device = device;
    const [nx, ny, nz] = SMOKE_DIMS;
    this.n = nx * ny * nz;
    this.origin = [0, 0, 0];
    this.originCell = null;
    this.params = new Float32Array(PARAM_VEC4 * 4);
    this.paramsI = new Int32Array(this.params.buffer);
    this.jacobiIters = 24;
    this.time = 0;

    const mk = (bytes, label) => device.createBuffer({ size: bytes, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST, label });
    const v4 = this.n * 16, f1 = this.n * 4;
    this.buf = {
      velA: mk(v4, 'velA'), velB: mk(v4, 'velB'), velT: mk(v4, 'velT'),
      denA: mk(v4, 'denA'), denB: mk(v4, 'denB'), denT: mk(v4, 'denT'),
      curl: mk(v4, 'curl'), solid: mk(v4, 'solid'),
      pA: mk(f1, 'pA'), pB: mk(f1, 'pB'), div: mk(f1, 'div'),
    };
    this.bytes = 8 * v4 + 3 * f1;
    this.ubo = device.createBuffer({ size: this.params.byteLength, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, label: 'smokeParams' });

    this.pipe = {};
    for (const [k, src] of Object.entries(SRC)) {
      const module = checkModule(device.createShaderModule({ code: SMOKE_COMMON + src, label: 'smoke-' + k }), 'smoke-' + k);
      this.pipe[k] = device.createComputePipeline({ layout: 'auto', compute: { module, entryPoint: 'main' }, label: 'smoke-' + k });
    }
    const B = this.buf, u = this.ubo;
    const bg = (p, list) => device.createBindGroup({
      layout: this.pipe[p].getBindGroupLayout(0),
      entries: [{ binding: 0, resource: { buffer: u } }, ...list.map((b, i) => ({ binding: i + 1, resource: { buffer: b } }))],
    });
    this.bg = {
      obstacle: bg('obstacle', [B.solid]),
      advect: bg('advect', [B.velA, B.denA, B.velT, B.denT, B.solid]),
      maccormack: bg('maccormack', [B.velA, B.denA, B.velT, B.denT, B.velB, B.denB, B.solid]),
      curl: bg('curl', [B.velB, B.curl]),
      forces: bg('forces', [B.velB, B.denB, B.curl, B.solid]),
      divergence: bg('divergence', [B.velB, B.div, B.solid]),
      jacobiAB: bg('jacobi', [B.pA, B.pB, B.div, B.solid]),
      jacobiBA: bg('jacobi', [B.pB, B.pA, B.div, B.solid]),
      project: bg('project', [B.pA, B.velB, B.solid]),
    };
    this.wg = [Math.ceil(nx / WG[0]), Math.ceil(ny / WG[1]), Math.ceil(nz / WG[2])];

    // render pipeline
    const rmod = checkModule(device.createShaderModule({ code: SMOKE_COMMON + RENDER_SRC, label: 'smoke-render' }), 'smoke-render');
    this.renderPipe = device.createRenderPipeline({
      layout: 'auto', label: 'smoke-render',
      vertex: { module: rmod, entryPoint: 'vs' },
      fragment: {
        module: rmod, entryPoint: 'fs',
        targets: [{ format, blend: {
          color: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' },
          alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' } } }],
      },
      primitive: { topology: 'triangle-list' },
    });
  }

  makeRenderBindGroup(camUbo, depthView) {
    this.renderBG = this.device.createBindGroup({
      layout: this.renderPipe.getBindGroupLayout(0),
      entries: [
        { binding: 0, resource: { buffer: this.ubo } },
        { binding: 1, resource: { buffer: camUbo } },
        { binding: 2, resource: { buffer: this.buf.denA } },
        { binding: 3, resource: depthView },
      ],
    });
  }

  // Build uniforms. Domain follows the car, snapped to whole cells (shift is applied during advection).
  update(dt, car, emitters, crates) {
    const [nx, ny, nz] = SMOKE_DIMS, h = SMOKE_H;
    // bias the domain behind the car so the trail stays in view
    const [fx, fz] = car.fwd();
    const cx = Math.round((car.x - fx * 5) / h - nx / 2), cz = Math.round((car.z - fz * 5) / h - nz / 2);
    let sx = 0, sz = 0;
    if (this.originCell) { sx = cx - this.originCell[0]; sz = cz - this.originCell[1]; }
    this.originCell = [cx, cz];
    this.origin = [cx * h, 0, cz * h];
    this.time += dt;
    const P = this.params, I = this.paramsI;
    P.fill(0);
    I[0] = nx; I[1] = ny; I[2] = nz;
    I[4] = sx; I[5] = 0; I[6] = sz;
    P.set([this.origin[0], 0, this.origin[2], h], 8);
    P.set([dt, this.time, 5.0, 2.2], 12);
    P.set([car.x, car.bodyY, car.z, car.heading], 16);
    P.set([car.halfExt[0], car.halfExt[1], car.halfExt[2], 0], 20);
    P.set([car.vx, 0, car.vz, car.w], 24);
    P.set([Math.exp(-0.28 * dt), Math.exp(-1.2 * dt), Math.exp(-0.15 * dt), 0.35], 28);
    for (let i = 0; i < 4; i++) {
      const e = emitters[i];
      if (!e) continue;
      P.set([e.pos[0], e.pos[1], e.pos[2], e.strength], 32 + i * 4);
      P.set([e.vel[0], e.vel[1], e.vel[2], e.radius], 48 + i * 4);
    }
    for (let i = 0; i < MAX_CRATES; i++) {
      const c = crates[i];
      if (!c) continue;
      P.set([c.x, c.z, c.yaw, 1], 64 + i * 4);
      P.set([c.hx, c.hy, c.hz, 0], 96 + i * 4);
    }
    this.device.queue.writeBuffer(this.ubo, 0, P);
  }

  encode(enc) {
    const pass = enc.beginComputePass({ label: 'smoke' });
    const run = (p, g) => { pass.setPipeline(this.pipe[p]); pass.setBindGroup(0, this.bg[g || p]); pass.dispatchWorkgroups(...this.wg); };
    run('obstacle'); run('advect'); run('maccormack'); run('curl'); run('forces'); run('divergence');
    for (let i = 0; i < this.jacobiIters; i++) run('jacobi', i % 2 ? 'jacobiBA' : 'jacobiAB');
    run('project');
    pass.end();
    // B -> A (current state lives in A for next step & rendering)
    enc.copyBufferToBuffer(this.buf.velB, 0, this.buf.velA, 0, this.n * 16);
    enc.copyBufferToBuffer(this.buf.denB, 0, this.buf.denA, 0, this.n * 16);
  }

  draw(pass) {
    pass.setPipeline(this.renderPipe);
    pass.setBindGroup(0, this.renderBG);
    pass.draw(3);
  }
}

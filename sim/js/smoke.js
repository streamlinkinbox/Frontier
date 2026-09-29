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
//  32..35 em2[4] (wheel axle axis xyz, spin rad/s)  36..39 wheel[4] (centre xyz, radius)
//  40 rp0 (absorb, shadowK, ambientK, brightness) 41 rp1 (tint rgb, phase g) 42 rp2 (lightDir xyz, dustMix)
//  44..47 ex[4] (blast centre xyz, age s)  48..51 exP[4] (radius, fuel, impulse, active)
//  44..51 ex[8] (centre xyz, age)  52..59 exP[8] (radius, fuel, impulse, mode 1=blast 2=plume)
//  60..67 exQ[8] (seed, upward bias, shape noise, stretch)
export const MAX_SOURCES = 8;
const PARAM_VEC4 = 68;

export const SMOKE_COMMON = /* wgsl */`
struct P {
  dims: vec4<i32>, shift: vec4<i32>, origin: vec4f, misc: vec4f,
  carC: vec4f, carH: vec4f, carV: vec4f, diss: vec4f,
  em: array<vec4f, 4>, emV: array<vec4f, 4>,
  box: array<vec4f, ${MAX_CRATES}>, boxS: array<vec4f, ${MAX_CRATES}>,
  em2: array<vec4f, 4>, wheel: array<vec4f, 4>,
  rp0: vec4f, rp1: vec4f, rp2: vec4f, rp3: vec4f,
  ex: array<vec4f, 8>, exP: array<vec4f, 8>, exQ: array<vec4f, 8>,
};
@group(0) @binding(0) var<uniform> prm: P;

// gas combustion rate (1/s): premixed gas burns fast once hot enough
fn burnRate(d: vec4f) -> f32 {
  if (d.z > 0.002 && d.y > 0.25) { return d.z * 7.0 + 0.4; }
  return 0.0;
}
fn hash3(p: vec3f) -> f32 { return fract(sin(dot(p, vec3f(127.1, 311.7, 74.7))) * 43758.5453); }
fn vnoise3(p: vec3f) -> f32 {
  let i = floor(p); let f = fract(p); let u = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(hash3(i), hash3(i + vec3f(1,0,0)), u.x), mix(hash3(i + vec3f(0,1,0)), hash3(i + vec3f(1,1,0)), u.x), u.y),
             mix(mix(hash3(i + vec3f(0,0,1)), hash3(i + vec3f(1,0,1)), u.x), mix(hash3(i + vec3f(0,1,1)), hash3(i + vec3f(1,1,1)), u.x), u.y), u.z);
}
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
  for (var i = 0; i < 4; i++) {
    let wc = prm.wheel[i];
    if (wc.w <= 0.0) { continue; }
    let ax = prm.em2[i].xyz;
    let q = wp - wc.xyz;
    let along = dot(q, ax);
    let radial = q - ax * along;
    if (abs(along) < 0.16 && length(radial) < wc.w) {
      return vec4f(prm.carV.xyz + cross(ax, radial) * prm.em2[i].w, 1.0);
    }
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
  nd = vec4f(nd.x * prm.diss.x, nd.y * prm.diss.y, nd.z * 0.995, nd.w * prm.diss.x);
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
    d.x += e.w * f * dt * 2.2;
    d.y += e.w * f * dt * 2.0;
    v = mix(v, prm.emV[i].xyz, clamp(f * e.w * dt * 4.0, 0.0, 1.0));
  }
  // tyre-driven swirl: air dragged around the spinning wheel (drift smoke curls around the tyre)
  for (var i = 0; i < 4; i++) {
    let wc = prm.wheel[i];
    if (wc.w <= 0.0 || prm.em[i].w <= 0.0) { continue; }
    let ax = prm.em2[i].xyz;
    let q = wp - wc.xyz;
    let along = dot(q, ax);
    let radial = q - ax * along;
    let rl = length(radial);
    let rr = (rl - wc.w * 1.35) / 0.35;
    let band = exp(-rr * rr) * exp(-along * along / 0.25);
    if (band < 0.02) { continue; }
    let spin = clamp(prm.em2[i].w, -40.0, 40.0);
    let tang = cross(ax, radial / max(rl, 1e-3)) * spin * wc.w * 0.55;
    v = mix(v, prm.carV.xyz * 0.3 + tang, clamp(band * dt * 10.0, 0.0, 1.0));
  }
  // gas explosions (mode 1) and lingering burning plumes (mode 2); every source has its own seed/shape
  for (var i = 0; i < 8; i++) {
    let b = prm.ex[i]; let bp = prm.exP[i]; let bq = prm.exQ[i];
    if (bp.w < 0.5) { continue; }
    let q = wp - b.xyz;
    if (bp.w < 1.5) {
      if (b.w >= 0.08) { continue; }
      // irregular, stretched gas cloud: fbm-perturbed radius -> no two fireballs look alike
      let qs = vec3f(q.x, q.y / bq.w, q.z);
      let r = length(qs);
      let np = qs * 0.9 + vec3f(bq.x * 17.0, bq.x * 5.3, bq.x * 11.0);
      let n = vnoise3(np) * 0.65 + vnoise3(np * 2.3) * 0.35;
      let rEff = bp.x * (1.0 + bq.z * (n - 0.5) * 2.0);
      let f = 1.0 - smoothstep(rEff * 0.35, rEff, r);
      if (f <= 0.0) { continue; }
      d.z += bp.y * f * dt * 12.0 * (0.6 + 0.8 * n);
      d.y = max(d.y, 0.6 * f);
      let dir = q / max(length(q), 0.05);
      v += (dir * bp.z + vec3f(0.0, bq.y, 0.0)) * f * dt * 12.0;
    } else {
      // burning wreck: flickering gas leak near the ground feeding a rising sooty plume
      let r2 = dot(q, q);
      let rad = bp.x;
      let f = exp(-r2 / (rad * rad));
      if (f < 0.02) { continue; }
      let flick = 0.6 + 0.4 * vnoise3(vec3f(prm.misc.y * 3.0 + bq.x * 13.0, q.x * 1.5, q.z * 1.5));
      d.z += bp.y * f * flick * dt * 6.0;
      d.y = max(d.y, (0.9 + 0.6 * flick) * f);
      d.x += bp.y * 1.2 * f * dt;
      d.w += bp.y * 1.2 * f * dt;
      v.y += bq.y * f * dt;
    }
  }
  // combustion: fuel -> heat + soot
  let br = min(burnRate(d) * dt, d.z);
  d.z -= br;
  d.y += br * 3.5;
  d.x += br * 1.1;
  d.w += br * 1.0;
  d.x = min(d.x, 3.0); d.y = min(d.y, 8.0); d.z = min(d.z, 3.0); d.w = min(d.w, d.x);
  vel[id] = vec4f(v, 0.0);
  den[id] = d;
}`,

  divergence: `
@group(0) @binding(1) var<storage, read> vel: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> div: array<f32>;
@group(0) @binding(3) var<storage, read> solid: array<vec4f>;
@group(0) @binding(4) var<storage, read> den: array<vec4f>;
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
  // burning gas expands: negative divergence target pushes flow outward (fireball growth)
  let expansion = burnRate(den[id]) * min(den[id].z, 1.0) * 2.5;
  div[id] = 0.5 / prm.origin.w * ((R - L) + (T - B) + (F - K)) - expansion;
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

  lighting: `
@group(0) @binding(1) var<storage, read> den: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> lightVol: array<vec4f>;
${HEAD}
  let L = normalize(prm.rp2.xyz);
  let h = prm.origin.w;
  // sun: march 20 cells toward the light (self-shadowing)
  var p = vec3f(c) + 0.5;
  var sunOD = 0.0;
  for (var i = 0; i < 20; i++) {
    p += L;
    let q = vec3<i32>(floor(p));
    if (!inDomain(q)) { break; }
    sunOD += den[cellIdx(q)].x;
  }
  // sky: march straight up (ambient occlusion from smoke above)
  var skyOD = 0.0;
  for (var j = 1; j <= 10; j++) {
    let q = c + vec3<i32>(0, j * 2, 0);
    if (q.y >= prm.dims.y) { break; }
    skyOD += den[cellIdx(q)].x * 2.0;
  }
  // local density-based occlusion (dense cores are darker)
  var occ = 0.0;
  occ += den[cellIdx(c + vec3<i32>(2,0,0))].x + den[cellIdx(c - vec3<i32>(2,0,0))].x;
  occ += den[cellIdx(c + vec3<i32>(0,0,2))].x + den[cellIdx(c - vec3<i32>(0,0,2))].x;
  lightVol[id] = vec4f(sunOD * h, skyOD * h, occ * h * 0.5, 0.0);
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
@group(0) @binding(4) var<storage, read> lightVol: array<vec4f>;
${sampler('sDen', 'den')}
${sampler('sLight', 'lightVol')}

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
  let L = normalize(prm.rp2.xyz);
  let absorb = prm.rp0.x;
  // Henyey-Greenstein phase (forward scattering: smoke glows when looking toward the sun)
  let g = prm.rp1.w;
  let ct = dot(rd, L);
  let phase = (1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * ct, 1e-4), 1.5);
  let sunCol = vec3f(1.0, 0.92, 0.80) * 1.45;
  let skyCol = vec3f(0.52, 0.62, 0.78);
  let bounce = vec3f(0.78, 0.62, 0.42) * 0.45;          // warm light bounced off the sand
  let tyreSmoke = vec3f(0.80, 0.81, 0.84);               // burnt-rubber smoke: cool grey-white
  let dust = vec3f(0.74, 0.61, 0.45);                    // kicked-up sand dust
  let albedo = mix(tyreSmoke, dust, prm.rp2.w) * prm.rp1.xyz;
  let center = (bmin.xz + bmax.xz) * 0.5;
  let halfW = (bmax.xz - bmin.xz) * 0.5;
  var fire = vec3f(0.0);
  for (var i = 0; i < n; i++) {
    if (t > tf || trans < 0.01) { break; }
    let p = ro + rd * t;
    let g3 = (p - prm.origin.xyz) / prm.origin.w - vec3f(0.5);
    let d4 = sDen(g3);
    var d = d4.x;
    let e = halfW - abs(p.xz - center);
    let edge = smoothstep(0.0, 2.5, min(e.x, e.y));
    d *= edge;
    // fire: emissive, blackbody-ish ramp from deep red to yellow-white with temperature
    let T = d4.y * edge;
    if (T > 0.6) {
      let fl = clamp((T - 0.6) / 3.0, 0.0, 1.0);
      let fireCol = mix(vec3f(1.0, 0.16, 0.02), vec3f(1.0, 0.72, 0.32), fl) + vec3f(0.4) * fl * fl;
      let emis = fireCol * pow(T - 0.6, 1.4) * 1.3;
      fire += trans * emis * stepLen;
    }
    if (d > 0.002) {
      let lv = sLight(g3);
      let sigma = d * absorb * (1.0 + 0.8 * clamp(d4.w / max(d4.x, 1e-3), 0.0, 1.0));
      let sunT = exp(-lv.x * absorb * prm.rp0.y);
      let powder = 1.0 - exp(-2.0 * sigma);              // Beer-Powder: darker edges facing the light
      let skyT = exp(-lv.y * absorb * 0.6);
      let ao = exp(-lv.z * absorb * 0.5);
      let hgt = clamp(p.y / 4.0, 0.0, 1.0);
      let direct = sunCol * sunT * mix(1.0, powder * 2.0, 0.5) * phase;
      let ambient = (skyCol * skyT * (0.6 + 0.4 * hgt) + bounce * (1.0 - hgt)) * ao * prm.rp0.z;
      let soot = clamp(d4.w / max(d4.x, 1e-3), 0.0, 1.0);
      let alb = mix(albedo, vec3f(0.07, 0.065, 0.06), soot);   // explosion soot is near-black
      let c = alb * (direct + ambient) * prm.rp0.w;
      let a = 1.0 - exp(-sigma * stepLen);
      col += trans * a * c;
      trans *= 1.0 - a;
    }
    t += stepLen;
  }
  // filmic-ish roll-off so hot cores stay orange/yellow instead of clipping to white
  col += (vec3f(1.0) - exp(-fire * 1.4)) * vec3f(1.0, 0.93, 0.85);
  let alpha = 1.0 - trans;
  if (alpha < 0.002 && dot(fire, fire) < 1e-4) { discard; }
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
      curl: mk(v4, 'curl'), solid: mk(v4, 'solid'), light: mk(v4, 'lightVol'),
      pA: mk(f1, 'pA'), pB: mk(f1, 'pB'), div: mk(f1, 'div'),
    };
    this.bytes = 9 * v4 + 3 * f1;
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
      divergence: bg('divergence', [B.velB, B.div, B.solid, B.denB]),
      jacobiAB: bg('jacobi', [B.pA, B.pB, B.div, B.solid]),
      jacobiBA: bg('jacobi', [B.pB, B.pA, B.div, B.solid]),
      project: bg('project', [B.pA, B.velB, B.solid]),
      lighting: bg('lighting', [B.denA, B.light]),
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
        { binding: 4, resource: { buffer: this.buf.light } },
      ],
    });
  }

  // Build uniforms. Domain follows the car, snapped to whole cells (shift is applied during advection).
  update(dt, car, emitters, crates, S, lightDir) {
    const [nx, ny, nz] = SMOKE_DIMS, h = SMOKE_H;
    // bias the domain behind the car so the trail stays in view
    // centre: slightly behind the car, pulled toward the nearest active explosion/plume so it stays in the grid
    const [fx, fz] = car.fwd();
    let tx = car.x - fx * 3, tz = car.z - fz * 3;
    let best = null, bd = 18 * 18;
    for (const src of this.sources || []) { const d2 = (src.x - car.x) ** 2 + (src.z - car.z) ** 2; if (d2 < bd) { bd = d2; best = src; } }
    if (best) { tx = (car.x + best.x) * 0.5; tz = (car.z + best.z) * 0.5; }
    // hysteresis: only move the centre when it drifts > 2 m (avoids constant resampling)
    if (!this.center || Math.hypot(tx - this.center[0], tz - this.center[1]) > 2) this.center = [tx, tz];
    const cx = Math.round(this.center[0] / h - nx / 2), cz = Math.round(this.center[1] / h - nz / 2);
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
    P.set([dt, this.time, S.vorticity, S.buoyancy], 12);
    P.set([car.x, car.bodyY, car.z, car.heading], 16);
    P.set([car.halfExt[0], car.halfExt[1], car.halfExt[2], 0], 20);
    P.set([car.vx, 0, car.vz, car.w], 24);
    P.set([Math.exp(-S.fade * dt), Math.exp(-1.2 * dt), Math.exp(-0.15 * dt), 0.35], 28);
    P.set([S.opacity, S.shadow, S.ambient, S.brightness], 160);
    P.set([S.tint, S.tint, S.tint * 1.02, S.phase], 164);
    P.set([lightDir[0], lightDir[1], lightDir[2], S.dust], 168);
    (this.sources || []).slice(0, 8).forEach((b, i) => {
      P.set([b.x, b.y, b.z, b.age], 176 + i * 4);
      P.set([b.radius, b.fuel, b.impulse, b.mode], 208 + i * 4);
      P.set([b.seed, b.up, b.noise, b.stretch], 240 + i * 4);
    });
    for (let i = 0; i < 4; i++) {
      const e = emitters[i];
      if (!e) continue;
      P.set([e.pos[0], e.pos[1], e.pos[2], e.strength * S.emission], 32 + i * 4);
      P.set([e.vel[0], e.vel[1], e.vel[2], e.radius], 48 + i * 4);
      P.set([e.axis[0], e.axis[1], e.axis[2], e.spin], 128 + i * 4);
      P.set([e.center[0], e.center[1], e.center[2], e.wheelR], 144 + i * 4);
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
    const lp = enc.beginComputePass({ label: 'smoke-light' });
    lp.setPipeline(this.pipe.lighting); lp.setBindGroup(0, this.bg.lighting); lp.dispatchWorkgroups(...this.wg);
    lp.end();
  }

  draw(pass) {
    pass.setPipeline(this.renderPipe);
    pass.setBindGroup(0, this.renderBG);
    pass.draw(3);
  }
}

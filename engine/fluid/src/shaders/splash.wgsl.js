// Volumetric splash liquid: Position Based Fluids (Macklin & Müller 2013) on the GPU.
//
// Splash water (tyre sheets, bow splashes, impact crowns) is a 3D particle liquid: every particle
// is a parcel of real water (SplashFluid.vol m³) taken out of the shallow-water grid when it is
// thrown and put back exactly where it lands. Between the two it is an incompressible liquid:
//   * neighbours through a hashed uniform grid rebuilt every substep (counting sort + scan)
//   * PBF density constraints (Jacobi, ping-pong) with the artificial pressure s_corr
//   * XSPH viscosity + Akinci cohesion (surface tension) so the water holds together in sheets,
//     ligaments and blobs before it breaks up
//   * collisions with the car body / wheels / props (the solver's primitive list)
//   * landing: merges into the water body (and leaves a raft of foam) or soaks into the ground
import { SIM_STRUCT, HELPERS, IDX, STATE_ACCESS } from './common.wgsl.js';

export const SPLASH_TABLE = 131072;   // hash buckets (power of two, = 512 scan blocks of 256)

const SPLASH_STRUCT = /* wgsl */ `
struct SplashParams {
  maxParticles: u32, numEmitters: u32, frame: u32, numPrims: u32,   // 0
  dt: f32, h: f32, vol: f32, eps: f32,                              // 4   vol: water per particle (m³)
  poly6: f32, spiky: f32, scorrK: f32, scorrW: f32,                 // 8
  gravity: f32, drag: f32, xsph: f32, cohesion: f32,                // 12
  radius: f32, maxSpeed: f32, life: f32, absorbAge: f32,            // 16
  foamProb: f32, foamSize: f32, foamLife: f32, foamAmount: f32,     // 20
  densVol: f32, tableMask: u32, maxFoamReq: u32, cohK: f32,         // 24  densVol: lattice-normalised volume (rest density = 1)
  windX: f32, windZ: f32, frameDt: f32, maxCorr: f32,               // 28  maxCorr: position correction cap per iteration (m)
  restN: f32, padA: f32, padB: f32, padC: f32,                      // 30  restN: neighbour count at rest density
};                                                                  // 32 words
const INVALID: u32 = 0xffffffffu;
fn cellHash(c: vec3i) -> u32 {
  return ((u32(c.x) * 73856093u) ^ (u32(c.y) * 19349663u) ^ (u32(c.z) * 83492791u)) & SP.tableMask;
}
fn kW(r2: f32) -> f32 {                     // poly6
  let h2 = SP.h * SP.h;
  if (r2 >= h2) { return 0.0; }
  let t = h2 - r2;
  return SP.poly6 * t * t * t;
}
fn kGrad(d: vec3f, r: f32) -> vec3f {       // spiky gradient, d = p_i - p_j
  if (r >= SP.h || r < 1e-7) { return vec3f(0.0); }
  let t = SP.h - r;
  return (SP.spiky * t * t / r) * d;
}
fn kCoh(r: f32) -> f32 {                    // Akinci et al. 2013 cohesion spline (unnormalised)
  let h = SP.h;
  if (r >= h) { return 0.0; }
  let a = (h - r) * (h - r) * (h - r) * r * r * r;
  if (2.0 * r > h) { return SP.cohK * a; }
  return SP.cohK * (2.0 * a - h * h * h * h * h * h / 64.0);
}
`;

// loop over the particles in the 27 neighbouring grid cells (hash buckets visited once each)
const NEIGHBORS = (body) => /* wgsl */ `
  {
    let cc = vec3i(floor(pi / SP.h));
    var seen: array<u32, 27>;
    var ns = 0u;
    for (var dz = -1; dz <= 1; dz++) {
      for (var dy = -1; dy <= 1; dy++) {
        for (var dx = -1; dx <= 1; dx++) {
          let bkt = cellHash(cc + vec3i(dx, dy, dz));
          var dup = false;
          for (var q = 0u; q < ns; q++) { if (seen[q] == bkt) { dup = true; } }
          if (dup) { continue; }
          seen[ns] = bkt;
          ns++;
          let s0 = cellStart[bkt];
          let s1 = s0 + cellCount[bkt];
          for (var kk = s0; kk < s1; kk++) {
            let j = sorted[kk];
            ${body}
          }
        }
      }
    }
  }`;

const DEPOSIT_FN = /* wgsl */ `
fn cellOfLocal(lp: vec2f) -> i32 {
  let i = clamp(i32(floor(lp.x / S.dx)), 0, S.nx - 1);
  let j = clamp(i32(floor(lp.y / S.dx)), 0, S.ny - 1);
  return j * S.nx + i;
}
fn depositAt(lp: vec2f, vol: f32, mud: f32) {
  let k = cellOfLocal(lp);
  atomicAdd(&deposit[2 * k], i32(round(vol * DEPOSIT_SCALE)));
  atomicAdd(&deposit[2 * k + 1], i32(round(vol * mud * DEPOSIT_SCALE)));
}
fn inside(lp: vec2f) -> bool {
  let L = vec2f(f32(S.nx), f32(S.ny)) * S.dx;
  return lp.x >= 0.0 && lp.y >= 0.0 && lp.x < L.x && lp.y < L.y;
}
`;

// ---------------------------------------------------------------------------------------------
export const SPLASH_EMIT = /* wgsl */ `
${SIM_STRUCT}
${HELPERS}
${SPLASH_STRUCT}
struct Emitter {
  a: vec4f,   // position xyz, count
  b: vec4f,   // velocity xyz, spread (m/s)
  c: vec4f,   // radiusX, radiusZ, fromWater (0/1), mud (-1 = from the water)
  d: vec4f,   // first ring-buffer slot, life (s), ring (0/1), radial speed (m/s)
};
@group(0) @binding(0) var<uniform> S: Sim;
@group(0) @binding(1) var<uniform> SP: SplashParams;
@group(0) @binding(2) var<storage, read> emitters: array<Emitter>;
@group(0) @binding(3) var<storage, read> hc: array<vec2f>;
@group(0) @binding(4) var<storage, read> bed: array<f32>;
@group(0) @binding(5) var<storage, read_write> pos: array<vec4f>;
@group(0) @binding(6) var<storage, read_write> vel: array<vec4f>;
@group(0) @binding(7) var<storage, read_write> pred: array<vec4f>;
@group(0) @binding(8) var<storage, read_write> deposit: array<atomic<i32>>;
${IDX}
${STATE_ACCESS}
${DEPOSIT_FN}
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let ei = gid.y;
  if (ei >= SP.numEmitters) { return; }
  let E = emitters[ei];
  let t = gid.x;
  if (t >= u32(E.a.w)) { return; }
  let slot = (u32(E.d.x) + t) % SP.maxParticles;
  // the ring buffer wrapped onto a live particle: give its water back first
  let old = pos[slot];
  if (old.w > 0.0) { depositAt(vec2f(old.x - S.originX, old.z - S.originZ), SP.vol, vel[slot].w); }
  pos[slot] = vec4f(old.xyz, 0.0);
  var seed = hashu(t * 7919u + ei * 104729u + SP.frame * 1299709u + 17u);
  var off = vec2f(0.0);
  var radial = vec2f(0.0);
  let a = rnd(&seed) * 6.2831853;
  if (E.d.z > 0.5) {
    // crown: on a ring, thrown outward
    radial = vec2f(cos(a), sin(a));
    off = radial * E.c.x * (0.85 + 0.3 * rnd(&seed));
  } else {
    off = vec2f(cos(a), sin(a)) * sqrt(rnd(&seed)) * E.c.xy;
  }
  var wp = E.a.xyz + vec3f(off.x, 0.0, off.y);
  let lp = vec2f(wp.x - S.originX, wp.z - S.originZ);
  if (!inside(lp)) { return; }
  var mud = E.c.w;
  if (E.c.z > 0.5) {
    // the thrown water must exist: reserve it from that column, and give up rather than run the
    // cell dry (the solver clamps depth at 0, which would create water)
    let k = cellOfLocal(lp);
    let h = hc[k].x;
    if (h < 0.015) { return; }
    if (mud < 0.0) { mud = hc[k].y; }
    let st = sampleState(lp);
    wp.y = max(wp.y, st.w);
    let q = i32(round(SP.vol * DEPOSIT_SCALE));
    let prev = atomicAdd(&deposit[2 * k], -q);
    if (f32(prev - q) < -0.6 * h * S.dx * S.dx * DEPOSIT_SCALE) { atomicAdd(&deposit[2 * k], q); return; }
    atomicAdd(&deposit[2 * k + 1], -i32(round(SP.vol * mud * DEPOSIT_SCALE)));
  }
  mud = clamp(mud, 0.0, 1.0);
  let sp = (vec3f(rnd(&seed), rnd(&seed), rnd(&seed)) - 0.5) * 2.0 * E.b.w;
  let v = E.b.xyz + sp + vec3f(radial.x, 0.0, radial.y) * E.d.w * (0.8 + 0.4 * rnd(&seed));
  // water leaves the source continuously during the frame: spread the frame's parcels along the
  // stream instead of stacking them in one layer (which the density solve would blow apart)
  wp += v * (rnd(&seed) * SP.frameDt);
  pos[slot] = vec4f(wp, E.d.y * (0.7 + 0.6 * rnd(&seed)));
  vel[slot] = vec4f(v, mud);
  pred[slot] = vec4f(wp, 0.0);
}
`;

// ---------------------------------------------------------------------------------------------
export const SPLASH_CLEAR = /* wgsl */ `
@group(0) @binding(0) var<storage, read_write> cellCount: array<u32>;
@compute @workgroup_size(256)
fn main(@builtin(global_invocation_id) gid: vec3u) { cellCount[gid.x] = 0u; }
`;

// gravity, air drag, prediction, and insertion into the hash grid (count per bucket)
export const SPLASH_PREDICT = /* wgsl */ `
${SPLASH_STRUCT}
@group(0) @binding(0) var<uniform> SP: SplashParams;
@group(0) @binding(1) var<storage, read> pos: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> vel: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> pred: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> cellRank: array<vec2u>;
@group(0) @binding(5) var<storage, read_write> cellCount: array<atomic<u32>>;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  if (i >= SP.maxParticles) { return; }
  let p = pos[i];
  if (p.w <= 0.0) { cellRank[i] = vec2u(INVALID, 0u); return; }
  let vv = vel[i];
  var v = vv.xyz;
  v.y -= SP.gravity * SP.dt;
  let rel = v - vec3f(SP.windX, 0.0, SP.windZ);
  v -= rel * min(SP.drag * length(rel) * SP.dt, 0.5);
  let s = length(v);
  if (s > SP.maxSpeed) { v *= SP.maxSpeed / s; }
  vel[i] = vec4f(v, vv.w);
  let q = p.xyz + v * SP.dt;
  pred[i] = vec4f(q, pred[i].w + SP.dt);          // w: age
  let b = cellHash(vec3i(floor(q / SP.h)));
  cellRank[i] = vec2u(b, atomicAdd(&cellCount[b], 1u));
}
`;

// exclusive prefix sum of the bucket counts (3 kernels: blocks of 256, block sums, add back)
export const SPLASH_SCAN_BLOCKS = /* wgsl */ `
@group(0) @binding(0) var<storage, read> cellCount: array<u32>;
@group(0) @binding(1) var<storage, read_write> cellStart: array<u32>;
@group(0) @binding(2) var<storage, read_write> blockSums: array<u32>;
var<workgroup> tmp: array<u32, 256>;
@compute @workgroup_size(256)
fn main(@builtin(global_invocation_id) gid: vec3u, @builtin(local_invocation_index) li: u32, @builtin(workgroup_id) wg: vec3u) {
  let v = cellCount[gid.x];
  tmp[li] = v;
  workgroupBarrier();
  for (var o = 1u; o < 256u; o = o << 1u) {
    var add = 0u;
    if (li >= o) { add = tmp[li - o]; }
    workgroupBarrier();
    tmp[li] = tmp[li] + add;
    workgroupBarrier();
  }
  cellStart[gid.x] = tmp[li] - v;
  if (li == 255u) { blockSums[wg.x] = tmp[255]; }
}
`;
export const SPLASH_SCAN_SUMS = /* wgsl */ `
@group(0) @binding(0) var<storage, read_write> blockSums: array<u32>;   // 512 entries
var<workgroup> tmp: array<u32, 256>;
@compute @workgroup_size(256)
fn main(@builtin(local_invocation_index) li: u32) {
  let a = blockSums[2u * li];
  let b = blockSums[2u * li + 1u];
  tmp[li] = a + b;
  workgroupBarrier();
  for (var o = 1u; o < 256u; o = o << 1u) {
    var add = 0u;
    if (li >= o) { add = tmp[li - o]; }
    workgroupBarrier();
    tmp[li] = tmp[li] + add;
    workgroupBarrier();
  }
  let excl = tmp[li] - (a + b);
  blockSums[2u * li] = excl;
  blockSums[2u * li + 1u] = excl + a;
}
`;
export const SPLASH_SCAN_ADD = /* wgsl */ `
@group(0) @binding(0) var<storage, read_write> cellStart: array<u32>;
@group(0) @binding(1) var<storage, read> blockSums: array<u32>;
@compute @workgroup_size(256)
fn main(@builtin(global_invocation_id) gid: vec3u) { cellStart[gid.x] = cellStart[gid.x] + blockSums[gid.x / 256u]; }
`;
export const SPLASH_SCATTER = /* wgsl */ `
${SPLASH_STRUCT}
@group(0) @binding(0) var<uniform> SP: SplashParams;
@group(0) @binding(1) var<storage, read> cellRank: array<vec2u>;
@group(0) @binding(2) var<storage, read> cellStart: array<u32>;
@group(0) @binding(3) var<storage, read_write> sorted: array<u32>;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  if (i >= SP.maxParticles) { return; }
  let cr = cellRank[i];
  if (cr.x == INVALID) { return; }
  sorted[cellStart[cr.x] + cr.y] = i;
}
`;

// ---------------------------------------------------------------------------------------------
// PBF density constraint: lambda_i = -C_i / (sum |grad C_i|^2 + eps),  C_i = rho_i / rho_0 - 1
export const SPLASH_LAMBDA = /* wgsl */ `
${SPLASH_STRUCT}
@group(0) @binding(0) var<uniform> SP: SplashParams;
@group(0) @binding(1) var<storage, read> pred: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> lambda: array<vec2f>;   // lambda, density
@group(0) @binding(3) var<storage, read> cellRank: array<vec2u>;
@group(0) @binding(4) var<storage, read> cellStart: array<u32>;
@group(0) @binding(5) var<storage, read> cellCount: array<u32>;
@group(0) @binding(6) var<storage, read> sorted: array<u32>;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  if (i >= SP.maxParticles) { return; }
  if (cellRank[i].x == INVALID) { return; }
  let pi = pred[i].xyz;
  let h2 = SP.h * SP.h;
  var rho = 0.0;
  var gi = vec3f(0.0);
  var sg2 = 0.0;
  ${NEIGHBORS(/* wgsl */ `
            let d = pi - pred[j].xyz;
            let r2 = dot(d, d);
            if (r2 < h2) {
              rho += kW(r2);
              if (j != i) {
                let g = kGrad(d, sqrt(r2)) * SP.densVol;
                gi += g;
                sg2 += dot(g, g);
              }
            }`)}
  rho *= SP.densVol;
  let C = max(rho - 1.0, 0.0);
  lambda[i] = vec2f(-C / (dot(gi, gi) + sg2 + SP.eps), rho);
}
`;

// position correction (Jacobi) + collisions with the car / props
export const SPLASH_DELTA = /* wgsl */ `
${SPLASH_STRUCT}
struct Prim { m0: vec4f, m1: vec4f, m2: vec4f, ext: vec4f, lin: vec4f, ang: vec4f, ctr: vec4f };
@group(0) @binding(0) var<uniform> SP: SplashParams;
@group(0) @binding(1) var<storage, read> predIn: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> predOut: array<vec4f>;
@group(0) @binding(3) var<storage, read> lambda: array<vec2f>;
@group(0) @binding(4) var<storage, read> cellRank: array<vec2u>;
@group(0) @binding(5) var<storage, read> cellStart: array<u32>;
@group(0) @binding(6) var<storage, read> cellCount: array<u32>;
@group(0) @binding(7) var<storage, read> sorted: array<u32>;
@group(0) @binding(8) var<storage, read> prims: array<Prim>;

// push a particle out of the solver's primitives (boxes, spheres, X-axis cylinders)
fn collide(q0: vec3f) -> vec3f {
  var q = q0;
  for (var n = 0u; n < SP.numPrims; n++) {
    let P = prims[n];
    let dc = q - P.ctr.xyz;
    let br = P.lin.w + SP.radius;
    if (dot(dc, dc) > br * br) { continue; }
    let lp = vec3f(dot(P.m0.xyz, q) + P.m0.w, dot(P.m1.xyz, q) + P.m1.w, dot(P.m2.xyz, q) + P.m2.w);
    let ty = i32(P.ext.w + 0.5);
    var np = lp;
    var hit = false;
    if (ty == 0) {
      let e = P.ext.xyz + vec3f(SP.radius);
      let pen = e - abs(lp);
      if (pen.x > 0.0 && pen.y > 0.0 && pen.z > 0.0) {
        hit = true;
        let sg = select(vec3f(-1.0), vec3f(1.0), lp >= vec3f(0.0));
        if (pen.x <= pen.y && pen.x <= pen.z) { np.x = sg.x * e.x; }
        else if (pen.y <= pen.z) { np.y = sg.y * e.y; }
        else { np.z = sg.z * e.z; }
      }
    } else if (ty == 1) {
      let rr = P.ext.x + SP.radius;
      let l = length(lp);
      if (l < rr) { hit = true; np = select(vec3f(0.0, rr, 0.0), lp * (rr / max(l, 1e-6)), l > 1e-6); }
    } else {
      let rr = P.ext.x + SP.radius;
      let hl = P.ext.y + SP.radius;
      let rl = length(lp.yz);
      if (abs(lp.x) < hl && rl < rr) {
        hit = true;
        if (hl - abs(lp.x) < rr - rl) { np.x = select(-hl, hl, lp.x >= 0.0); }
        else {
          let yz = select(vec2f(rr, 0.0), lp.yz * (rr / max(rl, 1e-6)), rl > 1e-6);
          np = vec3f(lp.x, yz.x, yz.y);
        }
      }
    }
    if (hit) { q = P.ctr.xyz + P.m0.xyz * np.x + P.m1.xyz * np.y + P.m2.xyz * np.z; }
  }
  return q;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  if (i >= SP.maxParticles) { return; }
  if (cellRank[i].x == INVALID) { return; }
  let p4 = predIn[i];
  let pi = p4.xyz;
  let li = lambda[i].x;
  let h2 = SP.h * SP.h;
  var dp = vec3f(0.0);
  ${NEIGHBORS(/* wgsl */ `
            if (j != i) {
              let d = pi - predIn[j].xyz;
              let r2 = dot(d, d);
              if (r2 < h2 && r2 > 1e-12) {
                let wr = kW(r2) / SP.scorrW;
                let sc = -SP.scorrK * wr * wr * wr * wr;
                dp += (li + lambda[j].x + sc) * kGrad(d, sqrt(r2));
              }
            }`)}
  // cap the correction: residual overlap relaxes over a few substeps instead of turning into an
  // explosive velocity (PBD derives velocity from the position change)
  var corr = dp * SP.densVol;
  let cl = length(corr);
  if (cl > SP.maxCorr) { corr *= SP.maxCorr / cl; }
  let q = collide(pi + corr);
  predOut[i] = vec4f(q, p4.w);
}
`;

// new velocity from the position change, XSPH viscosity and cohesion (surface tension)
export const SPLASH_VELOCITY = /* wgsl */ `
${SPLASH_STRUCT}
@group(0) @binding(0) var<uniform> SP: SplashParams;
@group(0) @binding(1) var<storage, read> pos: array<vec4f>;
@group(0) @binding(2) var<storage, read> pred: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> vel: array<vec4f>;
@group(0) @binding(4) var<storage, read> cellRank: array<vec2u>;
@group(0) @binding(5) var<storage, read> cellStart: array<u32>;
@group(0) @binding(6) var<storage, read> cellCount: array<u32>;
@group(0) @binding(7) var<storage, read> sorted: array<u32>;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  if (i >= SP.maxParticles) { return; }
  if (cellRank[i].x == INVALID) { return; }
  let pi = pred[i].xyz;
  let vi = (pi - pos[i].xyz) / SP.dt;
  let h2 = SP.h * SP.h;
  var dv = vec3f(0.0);
  var coh = vec3f(0.0);
  ${NEIGHBORS(/* wgsl */ `
            if (j != i) {
              let d = pred[j].xyz - pi;
              let r2 = dot(d, d);
              if (r2 < h2 && r2 > 1e-12) {
                let vj = (pred[j].xyz - pos[j].xyz) / SP.dt;
                dv += (vj - vi) * kW(r2);
                let r = sqrt(r2);
                coh += kCoh(r) * (d / r);
              }
            }`)}
  var v = vi + (SP.xsph * dv + SP.cohesion * SP.dt * coh) * SP.densVol;
  let s = length(v);
  if (s > SP.maxSpeed) { v *= SP.maxSpeed / s; }
  vel[i] = vec4f(v, vel[i].w);
}
`;

// commit positions; land in the water / on the ground (water goes back to the grid); age out
export const SPLASH_FINISH = /* wgsl */ `
${SIM_STRUCT}
${HELPERS}
${SPLASH_STRUCT}
@group(0) @binding(0) var<uniform> S: Sim;
@group(0) @binding(1) var<uniform> SP: SplashParams;
@group(0) @binding(2) var<storage, read> hc: array<vec2f>;
@group(0) @binding(3) var<storage, read> bed: array<f32>;
@group(0) @binding(4) var<storage, read_write> pos: array<vec4f>;
@group(0) @binding(5) var<storage, read> vel: array<vec4f>;
@group(0) @binding(6) var<storage, read> pred: array<vec4f>;
@group(0) @binding(7) var<storage, read_write> deposit: array<atomic<i32>>;
@group(0) @binding(8) var<storage, read_write> foamReq: array<atomic<u32>>;   // [count, (x, z, surface, vx, vz, mud) ...] as f32 bits
${IDX}
${STATE_ACCESS}
${DEPOSIT_FN}
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  if (i >= SP.maxParticles) { return; }
  let p = pos[i];
  if (p.w <= 0.0) { return; }
  let q4 = pred[i];
  let q = q4.xyz;
  let vv = vel[i];
  let v = vv.xyz;
  let mud = vv.w;
  let life = p.w - SP.dt;
  let lp = vec2f(q.x - S.originX, q.z - S.originZ);
  if (!inside(lp)) { pos[i] = vec4f(q, 0.0); return; }   // thrown out of the domain: lost, like spray
  let st = sampleState(lp);                                  // bed, depth, mud, surface
  var land = false;
  var intoWater = false;
  if (st.y > 0.008) {
    // below the water surface and not clearly rising out of it (fresh parcels leave upward fast):
    // merges back into the water body; anything pushed under the bed too
    intoWater = (q.y < st.w && q4.w > SP.absorbAge && (v.y < 0.4 || q4.w > 0.5)) || q.y < st.x;
  } else {
    land = q.y < st.x + SP.radius;                           // hit the ground: soaks in there
  }
  if (land || intoWater || life <= 0.0) {
    depositAt(lp, SP.vol, mud);
    if (intoWater) {
      // an impact entrains air: some landings leave a raft of foam
      var seed = hashu(i * 9781u + SP.frame * 6271u + 3u);
      if (rnd(&seed) < SP.foamProb) {
        let slot = atomicAdd(&foamReq[0], 1u);
        if (slot < SP.maxFoamReq) {
          let o = 1u + slot * 6u;
          atomicStore(&foamReq[o], bitcast<u32>(q.x));
          atomicStore(&foamReq[o + 1u], bitcast<u32>(q.z));
          atomicStore(&foamReq[o + 2u], bitcast<u32>(st.w));
          atomicStore(&foamReq[o + 3u], bitcast<u32>(v.x));
          atomicStore(&foamReq[o + 4u], bitcast<u32>(v.z));
          atomicStore(&foamReq[o + 5u], bitcast<u32>(mud));
        }
      }
    }
    pos[i] = vec4f(q, 0.0);
    return;
  }
  pos[i] = vec4f(q, life);
}
`;

// turn foam requests into foam rafts in the shared particle pool (FluidParticles)
export const SPLASH_FOAM = /* wgsl */ `
${HELPERS}
${SPLASH_STRUCT}
struct Particle { pos: vec3f, life: f32, vel: vec3f, maxLife: f32, size: f32, vol: f32, mud: f32, kind: u32 };
@group(0) @binding(0) var<uniform> SP: SplashParams;
@group(0) @binding(1) var<storage, read> foamReq: array<u32>;
@group(0) @binding(2) var<storage, read_write> particles: array<Particle>;
@group(0) @binding(3) var<storage, read> deadList: array<u32>;
@group(0) @binding(4) var<storage, read_write> counters: array<atomic<i32>>;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let r = gid.x;
  if (r >= min(foamReq[0], SP.maxFoamReq)) { return; }
  let prev = atomicSub(&counters[0], 1);
  if (prev <= 0) { atomicAdd(&counters[0], 1); return; }
  let idx = deadList[prev - 1];
  let o = 1u + r * 6u;
  var seed = hashu(r * 7919u + SP.frame * 104729u + 5u);
  var f: Particle;
  f.pos = vec3f(bitcast<f32>(foamReq[o]), bitcast<f32>(foamReq[o + 2u]) + 0.012, bitcast<f32>(foamReq[o + 1u]));
  f.vel = vec3f(bitcast<f32>(foamReq[o + 3u]) * 0.3, SP.foamAmount, bitcast<f32>(foamReq[o + 4u]) * 0.3);  // vel.y: raft thickness
  f.size = SP.foamSize * (0.5 + 0.5 * rnd(&seed));
  f.vol = 0.0;
  f.mud = bitcast<f32>(foamReq[o + 5u]);
  f.kind = 2u;
  f.maxLife = SP.foamLife * (0.4 + 0.6 * rnd(&seed));
  f.life = f.maxLife;
  particles[idx] = f;
}
`;

// neighbourhood statistics for rendering: a smoothed density (poly6 weights, which do not spike
// like the density solve's kernel) and the fraction of a full (rest lattice) neighbourhood.
// Isolated parcels become droplets/spray, coherent ones keep the liquid surface – with hysteresis
// so a parcel does not flicker between the two from frame to frame.
export const SPLASH_CLASSIFY = /* wgsl */ `
${SPLASH_STRUCT}
@group(0) @binding(0) var<uniform> SP: SplashParams;
@group(0) @binding(1) var<storage, read> pred: array<vec4f>;
@group(0) @binding(2) var<storage, read> cellRank: array<vec2u>;
@group(0) @binding(3) var<storage, read> cellStart: array<u32>;
@group(0) @binding(4) var<storage, read> cellCount: array<u32>;
@group(0) @binding(5) var<storage, read> sorted: array<u32>;
@group(0) @binding(6) var<storage, read_write> render: array<vec2f>;   // smoothed density, support
@group(0) @binding(7) var<storage, read_write> prev: array<vec2f>;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  if (i >= SP.maxParticles) { return; }
  if (cellRank[i].x == INVALID) { render[i] = vec2f(0.0); prev[i] = vec2f(0.0); return; }   // dead slot
  let pi = pred[i].xyz;
  let h2 = SP.h * SP.h;
  var rho = 0.0;
  var n = 0.0;
  ${NEIGHBORS(/* wgsl */ `
            let d = pi - pred[j].xyz;
            let r2 = dot(d, d);
            if (r2 < h2) { rho += kW(r2); n += 1.0; }`)}
  let dens = rho * SP.densVol;
  let support = n / max(SP.restN, 1.0);
  // hysteresis: coherent stays liquid until it really breaks up, and a droplet has to find real
  // company before it merges into the surface
  let wasLiquid = prev[i].x > 0.5;
  let liquid = select(support > 0.16, support > 0.06, wasLiquid);
  let r = vec2f(select(0.0, 1.0, liquid), dens);
  render[i] = r; prev[i] = r;
}
`;

// compact list of live particles for rendering + indirect draw arguments
export const SPLASH_DRAWLIST = /* wgsl */ `
${SPLASH_STRUCT}
@group(0) @binding(0) var<uniform> SP: SplashParams;
@group(0) @binding(1) var<storage, read> pos: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> draw: array<atomic<u32>>;   // [count, ids...]
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  if (i >= SP.maxParticles) { return; }
  if (pos[i].w <= 0.0) { return; }
  let s = atomicAdd(&draw[0], 1u);
  atomicStore(&draw[1u + s], i);
}
`;
export const SPLASH_FINALIZE = /* wgsl */ `
@group(0) @binding(0) var<storage, read_write> draw: array<atomic<u32>>;
@group(0) @binding(1) var<storage, read_write> drawArgs: array<u32>;
@group(0) @binding(2) var<storage, read_write> foamReq: array<atomic<u32>>;
@compute @workgroup_size(1)
fn main() {
  let n = atomicExchange(&draw[0], 0u);
  drawArgs[0] = 6u;
  drawArgs[1] = n;
  drawArgs[2] = 0u;
  drawArgs[3] = 0u;
  drawArgs[4] = n;          // alive count (stats)
  atomicStore(&foamReq[0], 0u);
}
`;

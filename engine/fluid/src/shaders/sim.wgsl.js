// Shallow-water solver kernels (staggered MAC grid, momentum-conservative upwind advection,
// upwind + hydrostatic-reconstruction mass fluxes with an outflow limiter, Manning bed friction,
// Bingham-plastic mud rheology, wet/dry fronts, sponge / wave-maker boundaries, sources, and
// two-way object coupling through an effective pressure head).
import { SIM_STRUCT, HELPERS, IDX, VEL_ACCESS, STATE_ACCESS } from './common.wgsl.js';

const WG = '@compute @workgroup_size(8, 8)';
const CELL = /* wgsl */ `
  let i = i32(gid.x); let j = i32(gid.y);
  if (i >= S.nx || j >= S.ny) { return; }
  let k = j * S.nx + i;
`;

// Incident ocean swell + sponge weight (requires S)
const BOUNDARY = /* wgsl */ `
// Linear long-wave incident field: returns (eta', u, v)
fn incident(p: vec2f) -> vec3f {
  var e = 0.0;
  var uv = vec2f(0.0);
  for (var n = 0; n < S.numWaves; n++) {
    let A = S.waveA[n];
    let Bw = S.waveB[n];
    let ph = A.w * dot(A.yz, p) - Bw.x * S.time + Bw.y;
    let a = A.x * S.waveRamp * sin(ph);
    e += a;
    uv += A.yz * (a * Bw.x / (A.w * max(S.waveDepth, 0.1)));
  }
  return vec3f(e, uv);
}
// returns (relaxation weight 0..1, 1 if the strongest edge is an ocean wave-maker)
fn sponge(p: vec2f) -> vec2f {
  let L = vec2f(f32(S.nx), f32(S.ny)) * S.dx;
  var d = vec4f(p.x, L.x - p.x, p.y, L.y - p.y);
  var w = 0.0;
  var oc = 0.0;
  for (var e = 0; e < 4; e++) {
    let bt = S.boundary[e];
    if (bt == 0) { continue; }
    let we = clamp(1.0 - d[e] / max(S.spongeWidth, 1e-3), 0.0, 1.0);
    if (we > w) { w = we; oc = select(0.0, 1.0, bt == 2); }
  }
  return vec2f(w * w, oc);
}
`;

// ---------------------------------------------------------------------------------------------
// 1) Momentum advection – Stelling & Duinmeijer (2003) momentum-conservative upwind scheme on the
//    staggered grid. Unlike semi-Lagrangian advection it transports momentum into newly wetted
//    faces (correct dam-break / run-up fronts) and conserves momentum across bores & hydraulic
//    jumps (correct breaking-wave and rapids speeds). Stable because |u| <= 0.5 dx/dt.
export const ADVECT = /* wgsl */ `
${SIM_STRUCT}
${HELPERS}
@group(0) @binding(0) var<uniform> S: Sim;
@group(0) @binding(1) var<storage, read> vel: array<vec2f>;
@group(0) @binding(2) var<storage, read_write> velOut: array<vec2f>;
@group(0) @binding(3) var<storage, read> hc: array<vec2f>;
@group(0) @binding(4) var<storage, read> bed: array<f32>;
${IDX}
${VEL_ACCESS}
// depth that crosses a face (upwind donor + hydrostatic reconstruction), same as the mass flux
fn faceDepth(kL: i32, kR: i32, u: f32) -> f32 {
  let kD = select(kR, kL, u >= 0.0);
  let hD = hc[kD].x;
  return clamp(bed[kD] + hD - max(bed[kL], bed[kR]), 0.0, hD);
}
// discharge per unit width through u-face (i+1/2, j) and v-face (i, j+1/2)
fn qx(i: i32, j: i32) -> f32 {
  if (i < 0 || i >= S.nx - 1 || j < 0 || j >= S.ny) { return 0.0; }
  let k = j * S.nx + i;
  let u = vel[k].x;
  return u * faceDepth(k, k + 1, u);
}
fn qy(i: i32, j: i32) -> f32 {
  if (j < 0 || j >= S.ny - 1 || i < 0 || i >= S.nx) { return 0.0; }
  let k = j * S.nx + i;
  let v = vel[k].y;
  return v * faceDepth(k, k + S.nx, v);
}
${WG}
fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${CELL}
  let nu = min(S.viscosity * S.dt / (S.dx * S.dx), 0.2);
  let inv = 1.0 / S.dx;
  var o = vec2f(0.0);
  let hC = hc[k].x;
  if (i < S.nx - 1) {
    let u = uAt(i, j);
    let hbar = 0.5 * (hC + hc[k + 1].x);
    var un = u;
    if (hbar > S.epsDry) {
      // x-direction (cell centres i and i+1 bound the u control volume)
      let q0 = 0.5 * (qx(i - 1, j) + qx(i, j));
      let q1 = 0.5 * (qx(i, j) + qx(i + 1, j));
      let us0 = select(u, uAt(i - 1, j), q0 >= 0.0);
      let us1 = select(uAt(i + 1, j), u, q1 >= 0.0);
      let ax = ((q1 * us1 - q0 * us0) - u * (q1 - q0)) * inv;
      // z-direction (corners (i+1/2, j+-1/2))
      let qT = 0.5 * (qy(i, j) + qy(i + 1, j));
      let qB = 0.5 * (qy(i, j - 1) + qy(i + 1, j - 1));
      let uT = select(uAt(i, j + 1), u, qT >= 0.0);
      let uB = select(u, uAt(i, j - 1), qB >= 0.0);
      let az = ((qT * uT - qB * uB) - u * (qT - qB)) * inv;
      un = u - S.dt * (ax + az) / hbar;
    }
    o.x = un + nu * (uAt(i - 1, j) + uAt(i + 1, j) + uAt(i, j - 1) + uAt(i, j + 1) - 4.0 * u);
  }
  if (j < S.ny - 1) {
    let v = vAt(i, j);
    let hbar = 0.5 * (hC + hc[k + S.nx].x);
    var vn = v;
    if (hbar > S.epsDry) {
      let q0 = 0.5 * (qy(i, j - 1) + qy(i, j));
      let q1 = 0.5 * (qy(i, j) + qy(i, j + 1));
      let vs0 = select(v, vAt(i, j - 1), q0 >= 0.0);
      let vs1 = select(vAt(i, j + 1), v, q1 >= 0.0);
      let az = ((q1 * vs1 - q0 * vs0) - v * (q1 - q0)) * inv;
      let qR = 0.5 * (qx(i, j) + qx(i, j + 1));
      let qL = 0.5 * (qx(i - 1, j) + qx(i - 1, j + 1));
      let vR = select(vAt(i + 1, j), v, qR >= 0.0);
      let vL = select(v, vAt(i - 1, j), qL >= 0.0);
      let ax = ((qR * vR - qL * vL) - v * (qR - qL)) * inv;
      vn = v - S.dt * (ax + az) / hbar;
    }
    o.y = vn + nu * (vAt(i - 1, j) + vAt(i + 1, j) + vAt(i, j - 1) + vAt(i, j + 1) - 4.0 * v);
  }
  velOut[k] = o;
}
`;

// ---------------------------------------------------------------------------------------------
// 2) Velocity update: pressure gradient (incl. object head + wind pressure), wet/dry, friction,
//    mud rheology, object drag, sources, sponge.  Reads advected vel, writes velOut.
export const VELOCITY = /* wgsl */ `
${SIM_STRUCT}
${HELPERS}
@group(0) @binding(0) var<uniform> S: Sim;
@group(0) @binding(1) var<storage, read> vel: array<vec2f>;
@group(0) @binding(2) var<storage, read_write> velOut: array<vec2f>;
@group(0) @binding(3) var<storage, read> hc: array<vec2f>;
@group(0) @binding(4) var<storage, read> bed: array<f32>;
@group(0) @binding(5) var<storage, read> objA: array<vec4f>;
@group(0) @binding(6) var<storage, read> objV: array<vec2f>;
${IDX}
${VEL_ACCESS}
${BOUNDARY}

// submerged object thickness in a column, and stiffness (piercing bodies act like a lid)
fn objHead(k: i32, B: f32, h: f32) -> vec2f {
  let o = objA[k];
  if (o.y <= o.x) { return vec2f(0.0, 0.0); }
  let top = B + h;
  let s = max(0.0, min(o.y, top) - max(o.x, B));
  let kk = select(1.0, o.z, o.y > top);
  return vec2f(s, kk);
}
fn windHead(i: i32, j: i32) -> f32 {
  if (S.windGust <= 0.0) { return 0.0; }
  let p = (vec2f(f32(i), f32(j)) + 0.5) * S.dx - vec2f(S.windX, S.windZ) * S.time;
  return S.windGust * (0.65 * noise2(p * 0.11) + 0.35 * noise2(p * 0.29 + vec2f(17.0, 3.0)));
}

fn faceUpdate(u0: f32, vt: f32, eL: f32, eR: f32, BL: f32, BR: f32, hcL: vec2f, hcR: vec2f,
              ovL: f32, ovR: f32, sL: f32, sR: f32, tauW: f32) -> f32 {
  var u = u0 - S.g * S.dt / S.dx * (eR - eL);
  let fwd = u >= 0.0;
  let hD = select(hcR.x, hcL.x, fwd);
  let BD = select(BR, BL, fwd);
  // hydrostatic reconstruction: depth of water that can actually cross this face
  let hf = clamp(BD + hD - max(BL, BR), 0.0, hD);
  if (hf <= S.epsDry) { return 0.0; }
  // wind shear stress on the free surface (rho_air * Cd * |U10| * U10), acts on the column
  u += S.dt * tauW / (1000.0 * max(hf, 0.05));
  // drag towards velocity of bodies occupying the column (no-slip coupling)
  let sMax = max(sL, sR);
  if (sMax > 0.0) {
    var vo = ovR;
    if (sL > 0.0 && sR > 0.0) { vo = 0.5 * (ovL + ovR); } else if (sL > 0.0) { vo = ovL; }
    let frac = clamp(sMax / (hf + sMax), 0.0, 1.0);
    u = mix(u, vo, frac * (1.0 - exp(-S.objCouple * S.dt)));
  }
  let hm = max(hf, 0.004);
  let c = clamp(select(hcR.y, hcL.y, fwd), 0.0, 1.0);
  // Manning bed friction (implicit)
  let speed = sqrt(u * u + vt * vt) + 1e-5;
  let n = mix(S.manning, S.mudManning, c);
  u = u / (1.0 + S.dt * S.g * n * n * speed / pow(hm, 1.3333333));
  // Bingham plastic mud (O'Brien & Julien): viscous + yield stress
  if (c > 0.002) {
    let cv = c * S.mudCvMax;
    let rho = 1000.0 + (S.sedimentDensity - 1000.0) * cv;
    let mu = S.mudViscA * (exp(S.mudViscB * cv) - 1.0);
    let tauY = S.mudYieldA * (exp(S.mudYieldB * cv) - 1.0);
    u = u / (1.0 + S.dt * 3.0 * mu / (rho * hm * hm));
    let sp = sqrt(u * u + vt * vt) + 1e-5;
    u = u * max(0.0, 1.0 - S.dt * tauY / (rho * hm * sp));
  }
  return clamp(u, -S.velMax, S.velMax);
}

${WG}
fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${CELL}
  var v = vel[k];
  let hcC = hc[k];
  let BC = bed[k];
  let oC = objHead(k, BC, hcC.x);
  let eC = BC + hcC.x + oC.x * oC.y + windHead(i, j);
  let ovC = objV[k];
  let wv = vec2f(S.windX, S.windZ);
  let tau = 1.2 * 1.3e-3 * length(wv) * wv;
  if (i < S.nx - 1) {
    let kR = k + 1;
    let hcR = hc[kR]; let BR = bed[kR];
    let oR = objHead(kR, BR, hcR.x);
    let eR = BR + hcR.x + oR.x * oR.y + windHead(i + 1, j);
    let vt = 0.25 * (vAt(i, j) + vAt(i + 1, j) + vAt(i, j - 1) + vAt(i + 1, j - 1));
    v.x = faceUpdate(v.x, vt, eC, eR, BC, BR, hcC, hcR, ovC.x, objV[kR].x, oC.x, oR.x, tau.x);
  } else { v.x = 0.0; }
  if (j < S.ny - 1) {
    let kN = k + S.nx;
    let hcN = hc[kN]; let BN = bed[kN];
    let oN = objHead(kN, BN, hcN.x);
    let eN = BN + hcN.x + oN.x * oN.y + windHead(i, j + 1);
    let ut = 0.25 * (uAt(i, j) + uAt(i - 1, j) + uAt(i, j + 1) + uAt(i - 1, j + 1));
    v.y = faceUpdate(v.y, ut, eC, eN, BC, BN, hcC, hcN, ovC.y, objV[kN].y, oC.x, oN.x, tau.y);
  } else { v.y = 0.0; }

  // momentum sources (river inflow direction)
  let pu = vec2f(f32(i) + 1.0, f32(j) + 0.5) * S.dx;
  let pv = vec2f(f32(i) + 0.5, f32(j) + 1.0) * S.dx;
  let ar = 1.0 - exp(-6.0 * S.dt);
  for (var s = 0; s < S.numSources; s++) {
    let a = S.srcA[s]; let b = S.srcB[s];
    if (b.z <= 0.0) { continue; }
    if (distance(pu, a.xy) < a.z) { v.x = mix(v.x, b.x * b.z, ar); }
    if (distance(pv, a.xy) < a.z) { v.y = mix(v.y, b.y * b.z, ar); }
  }
  // sponge / wave-maker relaxation
  let su = sponge(pu);
  if (su.x > 0.0) {
    var t = 0.0;
    if (su.y > 0.5) {
      let inc = incident(pu);
      let Bf = 0.5 * (BC + bed[ci(i + 1, j)]);
      if (S.seaLevel + inc.x > Bf + 0.05) { t = inc.y; }
    }
    v.x = mix(v.x, t, clamp(su.x * S.spongeRate * S.dt, 0.0, 1.0));
  }
  let sv = sponge(pv);
  if (sv.x > 0.0) {
    var t = 0.0;
    if (sv.y > 0.5) {
      let inc = incident(pv);
      let Bf = 0.5 * (BC + bed[ci(i, j + 1)]);
      if (S.seaLevel + inc.x > Bf + 0.05) { t = inc.z; }
    }
    v.y = mix(v.y, t, clamp(sv.x * S.spongeRate * S.dt, 0.0, 1.0));
  }
  velOut[k] = v;
}
`;

// ---------------------------------------------------------------------------------------------
// 3) Outflow limiter (guarantees non-negative depth, keeps mass exactly conserved)
export const LIMIT = /* wgsl */ `
${SIM_STRUCT}
@group(0) @binding(0) var<uniform> S: Sim;
@group(0) @binding(1) var<storage, read> vel: array<vec2f>;
@group(0) @binding(2) var<storage, read_write> lim: array<f32>;
${IDX}
${VEL_ACCESS}
${WG}
fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${CELL}
  let out = max(uAt(i, j), 0.0) + max(-uAt(i - 1, j), 0.0) + max(vAt(i, j), 0.0) + max(-vAt(i, j - 1), 0.0);
  lim[k] = min(1.0, S.dx / (S.dt * out + 1e-9));
}
`;

// ---------------------------------------------------------------------------------------------
// 4) Height + mud concentration update (finite-volume, conservative), sources, rain, sponge
export const HEIGHT = /* wgsl */ `
${SIM_STRUCT}
@group(0) @binding(0) var<uniform> S: Sim;
@group(0) @binding(1) var<storage, read> vel: array<vec2f>;
@group(0) @binding(2) var<storage, read> hc: array<vec2f>;
@group(0) @binding(3) var<storage, read_write> hcOut: array<vec2f>;
@group(0) @binding(4) var<storage, read> bed: array<f32>;
@group(0) @binding(5) var<storage, read> lim: array<f32>;
${IDX}
${BOUNDARY}
// volume flux per unit width across a face (m^2/s) and the matching sediment flux
fn flux(u: f32, kL: i32, kR: i32) -> vec2f {
  if (u == 0.0) { return vec2f(0.0); }
  let kD = select(kR, kL, u > 0.0);
  let d = hc[kD];
  let hf = clamp(bed[kD] + d.x - max(bed[kL], bed[kR]), 0.0, d.x);
  let q = u * hf * lim[kD];
  return vec2f(q, q * d.y);
}
fn mudDiff(k: i32, kn: i32, c: f32, h: f32) -> f32 {
  let n = hc[kn];
  return (n.y - c) * min(h, n.x);
}
${WG}
fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${CELL}
  let d = hc[k];
  let B = bed[k];
  var fE = vec2f(0.0); var fW = vec2f(0.0); var fN = vec2f(0.0); var fS = vec2f(0.0);
  if (i < S.nx - 1) { fE = flux(vel[k].x, k, k + 1); }
  if (i > 0) { fW = flux(vel[k - 1].x, k - 1, k); }
  if (j < S.ny - 1) { fN = flux(vel[k].y, k, k + S.nx); }
  if (j > 0) { fS = flux(vel[k - S.nx].y, k - S.nx, k); }
  let sc = S.dt / S.dx;
  var h = d.x - sc * (fE.x - fW.x + fN.x - fS.x);
  var ch = d.x * d.y - sc * (fE.y - fW.y + fN.y - fS.y);

  // turbulent mixing of sediment between wet neighbours (conservative)
  if (S.mudDiffusion > 0.0 && d.x > S.epsDry) {
    var acc = 0.0;
    if (i < S.nx - 1) { acc += mudDiff(k, k + 1, d.y, d.x); }
    if (i > 0) { acc += mudDiff(k, k - 1, d.y, d.x); }
    if (j < S.ny - 1) { acc += mudDiff(k, k + S.nx, d.y, d.x); }
    if (j > 0) { acc += mudDiff(k, k - S.nx, d.y, d.x); }
    ch += min(S.mudDiffusion * S.dt / (S.dx * S.dx), 0.2) * acc;
  }

  // rain and infiltration of thin films (splashes soak into the ground)
  h += S.rain * S.dt;
  if (h < S.filmDepth && h > 0.0) {
    let dh = min(h, S.infiltration * S.dt);
    ch -= dh * clamp(ch / h, 0.0, 1.0);
    h -= dh;
  }

  // volume sources / drains
  let p = (vec2f(f32(i), f32(j)) + 0.5) * S.dx;
  for (var s = 0; s < S.numSources; s++) {
    let a = S.srcA[s]; let b = S.srcB[s];
    let r = distance(p, a.xy);
    if (r < a.z) {
      let dv = a.w * S.dt;   // a.w = flow / (discrete source area), so the total is exactly Q*dt
      if (dv >= 0.0) { h += dv; ch += dv * b.w; }
      else { let rm = min(h, -dv); ch -= rm * clamp(ch / max(h, 1e-6), 0.0, 1.0); h -= rm; }
    }
  }

  // sponge (open boundary) / wave-maker relaxation
  let sw = sponge(p);
  if (sw.x > 0.0) {
    var eta = S.seaLevel;
    if (sw.y > 0.5) { eta += incident(p).x; }
    let ht = max(0.0, eta - B);
    let a = clamp(sw.x * S.spongeRate * S.dt, 0.0, 1.0);
    let cOld = select(0.0, clamp(ch / max(h, 1e-9), 0.0, 1.0), h > 1e-6);
    let hn = mix(h, ht, a);
    ch = hn * mix(cOld, 0.0, a);   // open water entering through the boundary is clear
    h = hn;
  }

  h = max(h, 0.0);
  ch = max(ch, 0.0);
  let c = select(d.y, clamp(ch / max(h, 1e-9), 0.0, 1.0), h > 1e-5);
  hcOut[k] = vec2f(h, c);
}
`;

// ---------------------------------------------------------------------------------------------
// 5) Apply particle <-> grid volume exchange (fixed-point atomics written by particle kernels)
export const DEPOSIT = /* wgsl */ `
${SIM_STRUCT}
${HELPERS}
@group(0) @binding(0) var<uniform> S: Sim;
@group(0) @binding(1) var<storage, read_write> hcRW: array<vec2f>;
@group(0) @binding(2) var<storage, read_write> deposit: array<atomic<i32>>;
${WG}
fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${CELL}
  let dvq = atomicExchange(&deposit[2 * k], 0);
  let dsq = atomicExchange(&deposit[2 * k + 1], 0);
  if (dvq == 0 && dsq == 0) { return; }
  let area = S.dx * S.dx;
  let dv = f32(dvq) / (DEPOSIT_SCALE * area);
  let ds = f32(dsq) / (DEPOSIT_SCALE * area);
  let d = hcRW[k];
  let h = max(0.0, d.x + dv);
  let ch = max(0.0, d.x * d.y + ds);
  let c = select(d.y, clamp(ch / max(h, 1e-9), 0.0, 1.0), h > 1e-5);
  hcRW[k] = vec2f(h, c);
}
`;

// ---------------------------------------------------------------------------------------------
// 6) Rasterise rigid-body primitives into per-column occupancy intervals [z0, z1] + velocity
export const OBJECTS = /* wgsl */ `
${SIM_STRUCT}
struct Prim {
  m0: vec4f, m1: vec4f, m2: vec4f,   // world -> local affine rows
  ext: vec4f,                        // box: half extents | sphere: r | cylinder(X axis): r, halfLen ; w = type
  lin: vec4f,                        // linear velocity, w = bounding radius
  ang: vec4f,                        // angular velocity
  ctr: vec4f,                        // centre (world)
};
@group(0) @binding(0) var<uniform> S: Sim;
@group(0) @binding(1) var<storage, read> prims: array<Prim>;
@group(0) @binding(2) var<storage, read> hc: array<vec2f>;
@group(0) @binding(3) var<storage, read_write> objA: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> objV: array<vec2f>;
${IDX}
fn slab(o: f32, d: f32, e: f32, t: vec2f) -> vec2f {
  if (abs(d) < 1e-7) {
    if (abs(o) > e) { return vec2f(1.0, -1.0); }
    return t;
  }
  let t1 = (-e - o) / d; let t2 = (e - o) / d;
  return vec2f(max(t.x, min(t1, t2)), min(t.y, max(t1, t2)));
}
fn circle(oy: f32, oz: f32, dy: f32, dz: f32, r: f32, t: vec2f) -> vec2f {
  let a = dy * dy + dz * dz;
  if (a < 1e-9) {
    if (oy * oy + oz * oz > r * r) { return vec2f(1.0, -1.0); }
    return t;
  }
  let b = oy * dy + oz * dz;
  let c = oy * oy + oz * oz - r * r;
  let disc = b * b - a * c;
  if (disc < 0.0) { return vec2f(1.0, -1.0); }
  let sq = sqrt(disc);
  return vec2f(max(t.x, (-b - sq) / a), min(t.y, (-b + sq) / a));
}
${WG}
fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${CELL}
  let wx = S.originX + (f32(i) + 0.5) * S.dx;
  let wz = S.originZ + (f32(j) + 0.5) * S.dx;
  var z0 = 1e9; var z1 = -1e9;
  var vsum = vec2f(0.0); var wsum = 0.0;
  for (var q = 0u; q < S.numPrims; q++) {
    let P = prims[q];
    let dc = vec2f(wx - P.ctr.x, wz - P.ctr.z);
    if (dot(dc, dc) > P.lin.w * P.lin.w) { continue; }
    let o = vec3f(P.m0.x * wx + P.m0.z * wz + P.m0.w, P.m1.x * wx + P.m1.z * wz + P.m1.w, P.m2.x * wx + P.m2.z * wz + P.m2.w);
    let d = vec3f(P.m0.y, P.m1.y, P.m2.y);
    var t = vec2f(-1e9, 1e9);
    let ty = i32(P.ext.w);
    if (ty == 0) {
      t = slab(o.x, d.x, P.ext.x, t); t = slab(o.y, d.y, P.ext.y, t); t = slab(o.z, d.z, P.ext.z, t);
    } else if (ty == 1) {
      let b = dot(o, d); let c = dot(o, o) - P.ext.x * P.ext.x;
      let disc = b * b - c;
      if (disc >= 0.0) { let sq = sqrt(disc); t = vec2f(-b - sq, -b + sq); } else { t = vec2f(1.0, -1.0); }
    } else {
      t = slab(o.x, d.x, P.ext.y, t);
      t = circle(o.y, o.z, d.y, d.z, P.ext.x, t);
    }
    if (t.y > t.x) {
      z0 = min(z0, t.x); z1 = max(z1, t.y);
      let pm = vec3f(wx, 0.5 * (t.x + t.y), wz);
      let pv = P.lin.xyz + cross(P.ang.xyz, pm - P.ctr.xyz);
      vsum += pv.xz; wsum += 1.0;
    }
  }
  var kk = 1.0;
  if (z1 > z0) {
    // stiffness of the lid constraint limited by the local gravity-wave CFL
    var hmax = 0.01;
    for (var dj = -1; dj <= 1; dj++) { for (var di = -1; di <= 1; di++) { hmax = max(hmax, hc[ci(i + di, j + dj)].x); } }
    let c2 = 0.2 * (S.dx / S.dt) * (S.dx / S.dt);
    kk = clamp(c2 / (S.g * hmax) - 1.0, 1.0, S.kObjMax);
  }
  objA[k] = vec4f(z0, z1, kk, 0.0);
  objV[k] = vsum / max(wsum, 1.0);
}
`;

// ---------------------------------------------------------------------------------------------
// 7a) Render prep: surface height with shoreline extrapolation, wetness memory, surface texture
export const PREP_A = /* wgsl */ `
${SIM_STRUCT}
@group(0) @binding(0) var<uniform> S: Sim;
@group(0) @binding(1) var<storage, read> hc: array<vec2f>;
@group(0) @binding(2) var<storage, read> bed: array<f32>;
@group(0) @binding(3) var<storage, read> objA: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> aux: array<vec4f>;
@group(0) @binding(5) var<storage, read_write> surf: array<f32>;
@group(0) @binding(6) var surfTex: texture_storage_2d<rgba32float, write>;
${IDX}
${WG}
fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${CELL}
  let d = hc[k];
  let B = bed[k];
  var sh = B + d.x;
  // fully submerged bodies raise the visible surface by their thickness
  let o = objA[k];
  if (o.y > o.x && o.y < B + d.x) { sh += max(0.0, o.y - max(o.x, B)); }
  if (d.x <= S.epsRender) {
    var sum = 0.0; var cnt = 0.0;
    for (var dj = -1; dj <= 1; dj++) {
      for (var di = -1; di <= 1; di++) {
        if (di == 0 && dj == 0) { continue; }
        let kn = ci(i + di, j + dj);
        let hn = hc[kn].x;
        if (hn > S.epsRender) { sum += bed[kn] + hn; cnt += 1.0; }
      }
    }
    if (cnt > 0.0) { sh = min(sum / cnt, B - 0.003); } else { sh = B - 0.25; }
  }
  var a = aux[k];
  let eta = B + d.x;
  var wet = a.y;
  if (d.x > 0.0008) { wet = 1.0; } else { wet = max(0.0, wet - S.frameDt / max(S.dryTime, 0.1)); }
  var stain = a.w;
  if (d.x > 0.0008) { stain = mix(stain, d.y, 0.25); }
  aux[k] = vec4f(eta, wet, a.z, stain);
  surf[k] = sh;
  textureStore(surfTex, vec2i(i, j), vec4f(sh, d.x, select(stain, d.y, d.x > 0.0008), wet));
}
`;

// 7b) Render prep: surface gradient + flow speed into a filterable texture for shading/caustics
export const PREP_B = /* wgsl */ `
${SIM_STRUCT}
@group(0) @binding(0) var<uniform> S: Sim;
@group(0) @binding(1) var<storage, read> vel: array<vec2f>;
@group(0) @binding(2) var<storage, read> surf: array<f32>;
@group(0) @binding(3) var shadeTex: texture_storage_2d<rgba16float, write>;
${IDX}
${VEL_ACCESS}
${WG}
fn main(@builtin(global_invocation_id) gid: vec3u) {
  ${CELL}
  let sE = surf[ci(i + 1, j)]; let sW = surf[ci(i - 1, j)];
  let sN = surf[ci(i, j + 1)]; let sS = surf[ci(i, j - 1)];
  // one-sided differences at the domain border
  let dxE = select(2.0, 1.0, i == 0 || i == S.nx - 1);
  let dzN = select(2.0, 1.0, j == 0 || j == S.ny - 1);
  var g = vec2f((sE - sW) / (dxE * S.dx), (sN - sS) / (dzN * S.dx));
  g = clamp(g, vec2f(-4.0), vec2f(4.0));
  let U = velC(i, j);
  let curl = (velC(i + 1, j).y - velC(i - 1, j).y - velC(i, j + 1).x + velC(i, j - 1).x) / (2.0 * S.dx);
  textureStore(shadeTex, vec2i(i, j), vec4f(g, length(U), abs(curl)));
}
`;

// ---------------------------------------------------------------------------------------------
// 8) Point queries for buoyancy / gameplay (async readback)
export const QUERY = /* wgsl */ `
${SIM_STRUCT}
@group(0) @binding(0) var<uniform> S: Sim;
@group(0) @binding(1) var<storage, read> hc: array<vec2f>;
@group(0) @binding(2) var<storage, read> vel: array<vec2f>;
@group(0) @binding(3) var<storage, read> bed: array<f32>;
@group(0) @binding(4) var<storage, read> objA: array<vec4f>;
@group(0) @binding(5) var<storage, read> aux: array<vec4f>;
@group(0) @binding(6) var<storage, read> points: array<vec4f>;   // xz world, count in [0].w of header? (uses arrayLength)
@group(0) @binding(7) var<storage, read_write> results: array<vec4f>;
${IDX}
${VEL_ACCESS}
${STATE_ACCESS}
fn headAt(k: i32) -> f32 {
  let d = hc[k]; let B = bed[k];
  let o = objA[k];
  var e = B + d.x;
  if (o.y > o.x) {
    let top = B + d.x;
    let s = max(0.0, min(o.y, top) - max(o.x, B));
    e += s * select(1.0, o.z, o.y > top);
  }
  return e;
}
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let q = i32(gid.x);
  if (q >= i32(arrayLength(&points))) { return; }
  let wp = points[q];
  if (wp.w < 0.5) { return; }
  let lp = vec2f(wp.x - S.originX, wp.y - S.originZ);
  let L = vec2f(f32(S.nx), f32(S.ny)) * S.dx;
  if (lp.x < 0.0 || lp.y < 0.0 || lp.x > L.x || lp.y > L.y) {
    results[2 * q] = vec4f(-1e4, 0.0, 0.0, 0.0);
    results[2 * q + 1] = vec4f(0.0, -1e4, -1e4, 0.0);
    return;
  }
  let st = sampleState(lp);
  let U = sampleVel(lp);
  // pressure head (includes displacement by bodies) – bilinear
  let f = lp / S.dx - 0.5;
  let fl = floor(f); let t = f - fl;
  let i0 = i32(fl.x); let j0 = i32(fl.y);
  let e = mix(mix(headAt(ci(i0, j0)), headAt(ci(i0 + 1, j0)), t.x), mix(headAt(ci(i0, j0 + 1)), headAt(ci(i0 + 1, j0 + 1)), t.x), t.y);
  let kc = ci(i32(lp.x / S.dx), i32(lp.y / S.dx));
  let dEta = (st.w - aux[kc].x) / max(S.frameDt, 1e-4);
  results[2 * q] = vec4f(select(st.x - 0.001, e, st.y > S.epsDry), st.y, U.x, U.y);
  results[2 * q + 1] = vec4f(st.z, st.x, st.w, dEta);
}
`;

// ---------------------------------------------------------------------------------------------
// 9) Total water volume (per-workgroup partial sums) – debugging / mass conservation checks
export const STATS = /* wgsl */ `
${SIM_STRUCT}
@group(0) @binding(0) var<uniform> S: Sim;
@group(0) @binding(1) var<storage, read> hc: array<vec2f>;
@group(0) @binding(2) var<storage, read_write> partial: array<vec4f>;
var<workgroup> red: array<vec4f, 256>;
@compute @workgroup_size(256)
fn main(@builtin(global_invocation_id) gid: vec3u, @builtin(local_invocation_index) li: u32, @builtin(workgroup_id) wg: vec3u) {
  let k = i32(gid.x);
  var v = vec4f(0.0);
  if (k < S.nx * S.ny) {
    let d = hc[k];
    v = vec4f(d.x, d.x * d.y, select(0.0, 1.0, d.x > S.epsRender), d.x);
  }
  red[li] = v;
  workgroupBarrier();
  var s = 128u;
  loop {
    if (s == 0u) { break; }
    if (li < s) {
      let a = red[li]; let b = red[li + s];
      red[li] = vec4f(a.xyz + b.xyz, max(a.w, b.w));
    }
    workgroupBarrier();
    s = s >> 1u;
  }
  if (li == 0u) { partial[wg.x] = red[0]; }
}
`;

// ---------------------------------------------------------------------------------------------
// 10) Terrain edit: move the bed while keeping the water surface where it was. Used by the editor
// brushes (raising the bed squeezes the column, lowering it lets water fill the new hollow).
export const BED_EDIT = /* wgsl */ `
struct BedEdit { nx: u32, ny: u32, pad0: u32, pad1: u32, };
@group(0) @binding(0) var<uniform> E: BedEdit;
@group(0) @binding(1) var<storage, read> oldBed: array<f32>;
@group(0) @binding(2) var<storage, read> newBed: array<f32>;
@group(0) @binding(3) var<storage, read_write> hc: array<vec2f>;
@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let i = i32(gid.x); let j = i32(gid.y);
  if (i >= i32(E.nx) || j >= i32(E.ny)) { return; }
  let k = j * i32(E.nx) + i;
  let d = hc[k];
  let surface = oldBed[k] + d.x;
  hc[k] = vec2f(max(0.0, surface - newBed[k]), d.y);
}
`;

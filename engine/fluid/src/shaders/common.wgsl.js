// Shared WGSL declarations for the shallow-water solver and particle system.
// Layout of `Sim` must match FluidSim._packSim() (offsets in 4-byte words noted on the right).

export const SIM_STRUCT = /* wgsl */ `
struct Sim {
  nx: i32, ny: i32, frame: u32, numPrims: u32,                    // 0
  dx: f32, dt: f32, g: f32, time: f32,                            // 4
  originX: f32, originZ: f32, epsDry: f32, velMax: f32,           // 8
  manning: f32, viscosity: f32, kObjMax: f32, objCouple: f32,     // 12
  mudYieldA: f32, mudYieldB: f32, mudViscA: f32, mudViscB: f32,   // 16
  mudCvMax: f32, sedimentDensity: f32, mudManning: f32, mudDiffusion: f32, // 20
  seaLevel: f32, spongeWidth: f32, rain: f32, frameDt: f32,       // 24
  windX: f32, windZ: f32, windGust: f32, waveDepth: f32,          // 28
  infiltration: f32, filmDepth: f32, dryTime: f32, waveRamp: f32, // 32
  boundary: vec4<i32>,                                            // 36  (W, E, S, N): 0 wall, 1 open, 2 ocean
  numWaves: i32, numSources: i32, epsRender: f32, spongeRate: f32,// 40
  waveA: array<vec4f, 4>,   // amplitude, dirX, dirZ, k             44
  waveB: array<vec4f, 4>,   // omega, phase, -, -                   60
  srcA: array<vec4f, 8>,    // x, z (local m), radius, flow/area m/s 76
  srcB: array<vec4f, 8>,    // dirX, dirZ, speed, mud               108
};                                                                // 140 words
`;

export const PART_STRUCT = /* wgsl */ `
struct Particle {
  pos: vec3f, life: f32,
  vel: vec3f, maxLife: f32,
  size: f32, vol: f32, mud: f32, kind: u32,
};
struct PartParams {
  maxParticles: u32, frame: u32, numEmitters: u32, enabled: u32,  // 0
  dt: f32, time: f32, gravity: f32, airDrag: f32,                 // 4
  taMin: f32, taMax: f32, wcMin: f32, wcMax: f32,                 // 8
  keMin: f32, keMax: f32, kTA: f32, kWC: f32,                     // 12
  emitScale: f32, maxPerCell: f32, sprayVolume: f32, minDepth: f32, // 16
  sprayLife: f32, foamLife: f32, mistLife: f32, foamFraction: f32,  // 20
  spraySize: f32, foamSize: f32, mistSize: f32, mistFraction: f32,  // 24
  foamDecayTurb: f32, foamStrandDecay: f32, foamDiffusion: f32, surfaceDrift: f32, // 28
  windX: f32, windY: f32, windZ: f32, impactFoamProb: f32,        // 32
  rainRate: f32, rainRadius: f32, rainCenterX: f32, rainCenterZ: f32, // 36
  rainHeight: f32, rainVolume: f32, rainSize: f32, sprayUp: f32,  // 40
};                                                                // 44 words
const KIND_SPRAY: u32 = 1u;
const KIND_FOAM: u32 = 2u;
const KIND_MIST: u32 = 3u;
const KIND_RAIN: u32 = 4u;
`;

export const HELPERS = /* wgsl */ `
const DEPOSIT_SCALE: f32 = 1.0e7;   // fixed-point units per m^3 for atomic volume exchange
fn hashu(n: u32) -> u32 {
  var x = n;
  x ^= x >> 16u; x *= 0x7feb352du;
  x ^= x >> 15u; x *= 0x846ca68bu;
  x ^= x >> 16u;
  return x;
}
fn rnd(s: ptr<function, u32>) -> f32 {
  *s = hashu(*s + 0x9e3779b9u);
  return f32(*s >> 8u) * (1.0 / 16777216.0);
}
fn phi(I: f32, lo: f32, hi: f32) -> f32 {
  return (min(I, hi) - min(I, lo)) / max(hi - lo, 1e-6);
}
fn hash2i(p: vec2i) -> f32 {
  return f32(hashu((u32(p.x) * 73856093u) ^ (u32(p.y) * 19349663u))) * (1.0 / 4294967295.0);
}
fn noise2(p: vec2f) -> f32 {
  let i = vec2i(floor(p));
  let f = fract(p);
  let u = f * f * (3.0 - 2.0 * f);
  let a = hash2i(i); let b = hash2i(i + vec2i(1, 0));
  let c = hash2i(i + vec2i(0, 1)); let d = hash2i(i + vec2i(1, 1));
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 2.0 - 1.0;
}
`;

// Requires: S (Sim)
export const IDX = /* wgsl */ `
fn ci(i: i32, j: i32) -> i32 { return clamp(j, 0, S.ny - 1) * S.nx + clamp(i, 0, S.nx - 1); }
fn inDomain(i: i32, j: i32) -> bool { return i >= 0 && j >= 0 && i < S.nx && j < S.ny; }
`;

// Requires: S, vel (array<vec2f>; .x = u on east face, .y = v on north face)
export const VEL_ACCESS = /* wgsl */ `
fn uAt(i: i32, j: i32) -> f32 {
  if (i < 0 || i >= S.nx - 1) { return 0.0; }
  return vel[clamp(j, 0, S.ny - 1) * S.nx + i].x;
}
fn vAt(i: i32, j: i32) -> f32 {
  if (j < 0 || j >= S.ny - 1) { return 0.0; }
  return vel[j * S.nx + clamp(i, 0, S.nx - 1)].y;
}
// cell-centred velocity
fn velC(i: i32, j: i32) -> vec2f {
  return vec2f(0.5 * (uAt(i - 1, j) + uAt(i, j)), 0.5 * (vAt(i, j - 1) + vAt(i, j)));
}
// bilinear velocity at a local position (metres)
fn sampleVel(lp: vec2f) -> vec2f {
  let fu = lp / S.dx - vec2f(1.0, 0.5);
  let iu = floor(fu); let tu = fu - iu;
  let i0 = i32(iu.x); let j0 = i32(iu.y);
  let u = mix(mix(uAt(i0, j0), uAt(i0 + 1, j0), tu.x), mix(uAt(i0, j0 + 1), uAt(i0 + 1, j0 + 1), tu.x), tu.y);
  let fv = lp / S.dx - vec2f(0.5, 1.0);
  let iv = floor(fv); let tv = fv - iv;
  let i1 = i32(iv.x); let j1 = i32(iv.y);
  let v = mix(mix(vAt(i1, j1), vAt(i1 + 1, j1), tv.x), mix(vAt(i1, j1 + 1), vAt(i1 + 1, j1 + 1), tv.x), tv.y);
  return vec2f(u, v);
}
`;

// Requires: S, hc (array<vec2f>: depth, mud), bed (array<f32>)
export const STATE_ACCESS = /* wgsl */ `
// returns (bed, depth, mud, surface) bilinearly interpolated at a local position (metres)
fn sampleState(lp: vec2f) -> vec4f {
  let f = lp / S.dx - 0.5;
  let fl = floor(f); let t = f - fl;
  let i0 = i32(fl.x); let j0 = i32(fl.y);
  let k00 = ci(i0, j0); let k10 = ci(i0 + 1, j0); let k01 = ci(i0, j0 + 1); let k11 = ci(i0 + 1, j0 + 1);
  let b = mix(mix(bed[k00], bed[k10], t.x), mix(bed[k01], bed[k11], t.x), t.y);
  let a00 = hc[k00]; let a10 = hc[k10]; let a01 = hc[k01]; let a11 = hc[k11];
  let h = mix(mix(a00.x, a10.x, t.x), mix(a01.x, a11.x, t.x), t.y);
  let c = mix(mix(a00.y, a10.y, t.x), mix(a01.y, a11.y, t.x), t.y);
  return vec4f(b, h, c, b + h);
}
`;

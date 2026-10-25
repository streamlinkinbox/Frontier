// Frontier Fluid — shared WGSL chunk. Prepended to every compute shader by
// the loader (the marker line `//@@COMMON@@` is replaced at module creation).
//
// State textures (all simulation-resolution, rgba16float):
//   field   : x = water depth d (m), y = velocity u (m/s, +x),
//             z = velocity v (m/s, +z), w = splash/breaking energy
//   bedFlow : x = bed height b (m), y/z = river flow target velocity,
//             w = channel mask (0..1, how strongly flow forcing applies)
//   foam    : x = advected foam density, y = terrain wetness (0..1)
// Free surface height eta = b + d.  Wave speed c = sqrt(g * d) — real physics.

struct SimParams {
  res : vec2<u32>,
  frame : u32,
  interactorCount : u32,
  sourceCount : u32,
  splashCount : u32,
  foamParticleCount : u32,
  boundaryMode : u32,        // 0 = solid walls, 1 = ocean (sponge + level restore + swell)
  worldSize : f32,
  dx : f32,
  invDx : f32,
  dt : f32,                  // solver substep dt
  frameDt : f32,             // whole-frame dt (particles, foam, interactors)
  time : f32,
  gravity : f32,
  viscosity : f32,
  damping : f32,
  bedFriction : f32,
  waterLevel : f32,
  rainRate : f32,
  evaporation : f32,
  foamDecay : f32,
  edgeWaveAmp : f32,
  edgeWaveFreq : f32,
  splashRate : f32,
  foamRate : f32,
  particleGravity : f32,
  ior : f32,
  center : vec2<f32>,
  wind : vec2<f32>,
  sunDir : vec3<f32>,        // direction TOWARD the sun, normalised
  foamLifeMin : f32,
  foamLifeMax : f32,
  shoreFoam : f32,
  breakFoam : f32,
  foamMax : f32,
};

struct Interactor {
  posR : vec4<f32>,   // xyz = centre, w = radius
  velS : vec4<f32>,   // xyz = velocity, w = strength multiplier
  kindH : vec4<f32>,  // x = kind (0 sphere, 1 column), y = half height
};

struct Source {
  posR : vec4<f32>,    // xyz = centre, w = radius
  flowVel : vec4<f32>, // x = flow (m/s of depth at centre), yz = injection velocity
};

fn cellWorld(p : vec2<i32>, res : vec2<u32>, center : vec2<f32>, worldSize : f32) -> vec2<f32> {
  let uv = (vec2<f32>(p) + 0.5) / vec2<f32>(res);
  return center + (uv - 0.5) * worldSize;
}

fn worldUV(xz : vec2<f32>, center : vec2<f32>, worldSize : f32) -> vec2<f32> {
  return (xz - center) / worldSize + 0.5;
}

fn clampCell(p : vec2<i32>, res : vec2<u32>) -> vec2<i32> {
  return clamp(p, vec2<i32>(0), vec2<i32>(res) - vec2<i32>(1));
}

// PCG-style integer hash -> [0,1)
fn hashF(n : u32) -> f32 {
  var x = n * 747796405u + 2891336453u;
  x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
  x = (x >> 22u) ^ x;
  return f32(x) / 4294967296.0;
}

fn rand2(seed : u32) -> vec2<f32> {
  return vec2<f32>(hashF(seed), hashF(seed * 1664525u + 1013904223u));
}

const DEPTH_EPS : f32 = 0.0008;
const LANDING_MAX : u32 = 8192u;
const LANDING_FEEDBACK_MAX : u32 = 256u; // droplet ripples fed back per frame (foam uses all)

// Ring-buffer allocation heads for the particle pools (never cleared after init).
struct Counters {
  splashHead : atomic<u32>,
  foamHead : atomic<u32>,
  pad0 : u32,
  pad1 : u32,
};

// Number of valid droplet-landing events this frame (cleared by TS after use).
struct LandingMeta {
  count : atomic<u32>,
  pad0 : u32,
  pad1 : u32,
  pad2 : u32,
};

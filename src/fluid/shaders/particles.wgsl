//@@COMMON@@
// Frontier Fluid — GPU particle systems: ballistic SPLASH spray + FOAM flecks.
//
// Splash particles are true ballistic droplets: spawned where the solver's
// splash-energy field is high (vehicle wheels churning, plunging bodies,
// breaking crests), integrated under gravity, and when they fall back into
// the surface they (a) spawn foam flecks and (b) feed a ripple impulse back
// into the shallow-water solver. Nothing is keyframed.
//
// Foam particles are flecks of aerated water that live ON the surface: they
// are carried by the simulated velocity field, pile up in convergence zones
// and along shorelines, swirl in eddies behind wheels, and pop over time.
//
// Emission uses rejection sampling against the solver's energy/foam fields,
// writing into ring buffers with atomic heads (oldest particles recycle).
//
// Bindings (PARTICLES layout): 0 P | 1 field | 2 bedFlow | 3 sampler |
//   4 splash pool | 5 foam pool | 6 landings | 7 counters | 8 foamField |
//   9 landingMeta

struct SplashParticle {
  pos : vec3<f32>,
  size : f32,
  vel : vec3<f32>,
  life : f32,   // remaining life; <= 0 = dead
};

struct FoamParticle {
  pos : vec3<f32>,
  size : f32,
  ageLife : vec2<f32>,  // x = age, y = lifespan; age >= lifespan = dead
  seedSpin : vec2<f32>, // x = random seed, y = spin (rad/s)
};

@group(0) @binding(0) var<uniform> P : SimParams;
@group(0) @binding(1) var field : texture_2d<f32>;
@group(0) @binding(2) var bedFlow : texture_2d<f32>;
@group(0) @binding(3) var samp : sampler;
@group(0) @binding(4) var<storage, read_write> splashPool : array<SplashParticle>;
@group(0) @binding(5) var<storage, read_write> foamPool : array<FoamParticle>;
@group(0) @binding(6) var<storage, read_write> landings : array<vec4<f32>>;
@group(0) @binding(7) var<storage, read_write> counters : Counters;
@group(0) @binding(8) var foamField : texture_2d<f32>;
@group(0) @binding(9) var<storage, read_write> landingMeta : LandingMeta;

fn sampleField(xz : vec2<f32>) -> vec4<f32> {
  let uv = worldUV(xz, P.center, P.worldSize);
  if (any(uv < vec2<f32>(0.0)) || any(uv > vec2<f32>(1.0))) { return vec4<f32>(0.0); }
  return textureSampleLevel(field, samp, uv, 0.0);
}
fn sampleBed(xz : vec2<f32>) -> f32 {
  let uv = worldUV(xz, P.center, P.worldSize);
  return textureSampleLevel(bedFlow, samp, clamp(uv, vec2<f32>(0.0), vec2<f32>(1.0)), 0.0).x;
}
fn surfaceY(xz : vec2<f32>) -> f32 {
  let f = sampleField(xz);
  return sampleBed(xz) + f.x;
}

// ------------------------------------------------------------- splash update
@compute @workgroup_size(128)
fn splashUpdate(@builtin(global_invocation_id) id : vec3<u32>) {
  let i = id.x;
  if (i >= P.splashCount) { return; }
  var p = splashPool[i];
  if (p.life <= 0.0) { return; }
  let dt = max(P.frameDt, 0.0001);

  p.life -= dt;
  p.vel.y -= P.particleGravity * dt;
  p.vel *= exp(-0.12 * dt); // air drag
  p.pos += p.vel * dt;

  // hit the simulated surface (or dry ground) => land: feed the solver +
  // foam systems, then die.
  let bed = sampleBed(p.pos.xz);
  let f = sampleField(p.pos.xz);
  let sy = bed + f.x;
  if (p.pos.y <= sy) {
    if (f.x > 0.004) {
      let energy = clamp(length(p.vel) * 0.12 + 0.15, 0.0, 1.5);
      let slot = atomicAdd(&landingMeta.count, 1u);
      if (slot < LANDING_MAX) { landings[slot] = vec4<f32>(p.pos.x, p.pos.z, energy, p.size); }
    }
    p.life = 0.0;
    p.pos = vec3<f32>(0.0, -1000.0, 0.0);
  }
  if (p.life <= 0.0) { p.pos = vec3<f32>(0.0, -1000.0, 0.0); }
  splashPool[i] = p;
}

// ------------------------------------------------------------- splash emit
// One thread = one emission candidate. Randomly probe the splash-energy
// field; accept with probability proportional to energy so spray density
// tracks the physics. P.splashRate scales how many candidates TS dispatches.
@compute @workgroup_size(128)
fn splashEmit(@builtin(global_invocation_id) id : vec3<u32>) {
  let seed = id.x * 2654435761u ^ (P.frame * 40503u) ^ 0x51ab3u;
  let r0 = rand2(seed);
  let r1 = rand2(seed ^ 0x9e3779b9u);
  let r2 = rand2(seed ^ 0x27d4eb2du);

  // sometimes spawn directly at fresh droplet landings (secondary crown splash)
  var xz : vec2<f32>;
  var boost = 0.0;
  let lc = min(atomicLoad(&landingMeta.count), LANDING_MAX);
  if (lc > 0u && r0.x < 0.15) {
    let L = landings[u32(r0.y * f32(lc)) % lc];
    xz = L.xy + (r1 - 0.5) * P.dx * 2.0;
    boost = L.z * 0.8;
  } else {
    xz = P.center + (r0 - 0.5) * P.worldSize;
  }

  let f = sampleField(xz);
  let energy = f.w + boost;
  if (f.x <= 0.004 || energy <= 0.02) { return; }
  if (r1.x > clamp(energy * 0.55, 0.0, 1.0)) { return; } // rejection sample

  let slot = atomicAdd(&counters.splashHead, 1u) % P.splashCount;
  let surfVel = f.yz;
  let ang = r1.y * 6.2831853;
  let radial = (0.25 + 1.5 * r2.x) * sqrt(energy);
  var vel = vec3<f32>(
    surfVel.x * 0.85 + cos(ang) * radial,
    (0.9 + 3.4 * r2.y) * sqrt(energy) + boost * 1.2,
    surfVel.y * 0.85 + sin(ang) * radial,
  );
  let sy = sampleBed(xz) + f.x;
  splashPool[slot] = SplashParticle(
    vec3<f32>(xz.x, sy + 0.02, xz.y),
    0.02 + 0.075 * r2.x * (0.5 + energy * 0.5),
    vel,
    0.45 + 0.9 * r1.x,
  );
}

// --------------------------------------------------------------- foam update
@compute @workgroup_size(128)
fn foamParticleUpdate(@builtin(global_invocation_id) id : vec3<u32>) {
  let i = id.x;
  if (i >= P.foamParticleCount) { return; }
  var p = foamPool[i];
  if (p.ageLife.x >= p.ageLife.y) { return; }
  let dt = max(P.frameDt, 0.0001);

  let f = sampleField(p.pos.xz);
  let bed = sampleBed(p.pos.xz);

  if (f.x > 0.003) {
    // carried by the simulated flow (+ gentle jitter so sheets break up)
    let jit = (rand2(i * 1973u ^ P.frame * 9277u) - 0.5) * 0.35 * dt;
    p.pos.x += (f.y + jit.x) * dt;
    p.pos.z += (f.z + jit.y) * dt;
    p.pos.y = bed + f.x + 0.012;
    p.ageLife.x += dt;
  } else {
    // beached: stick to the ground and pop fast
    p.pos.y = bed + 0.008;
    p.ageLife.x += dt * 2.5;
  }

  // foam flecks slowly grow as they merge, then fade (shader fades alpha)
  p.size = min(p.size + dt * 0.012, 0.55);

  if (p.ageLife.x >= p.ageLife.y) { p.pos = vec3<f32>(0.0, -1000.0, 0.0); }
  foamPool[i] = p;
}

// ----------------------------------------------------------------- foam emit
@compute @workgroup_size(128)
fn foamEmit(@builtin(global_invocation_id) id : vec3<u32>) {
  let seed = id.x * 2246822519u ^ (P.frame * 3266489917u) ^ 0xf00du;
  let r0 = rand2(seed);
  let r1 = rand2(seed ^ 0x85ebca6bu);
  let r2 = rand2(seed ^ 0xc2b2ae35u);

  // 45% of emissions anchor on droplet landings (where spray just hit),
  // the rest probe the advected foam-density field (breaking crests etc.).
  var xz : vec2<f32>;
  var strength = 0.0;
  let lc = min(atomicLoad(&landingMeta.count), LANDING_MAX);
  if (lc > 0u && r0.x < 0.45) {
    let L = landings[u32(r0.y * f32(lc)) % lc];
    if (L.z <= 0.0) { return; }
    xz = L.xy + (r1 - 0.5) * (P.dx * 3.0);
    strength = 0.55 + L.z;
  } else {
    xz = P.center + (r0 - 0.5) * P.worldSize;
    let uv = worldUV(xz, P.center, P.worldSize);
    strength = textureSampleLevel(foamField, samp, uv, 0.0).x;
    if (r1.x > clamp(strength * 0.8, 0.0, 1.0)) { return; }
  }

  let f = sampleField(xz);
  if (f.x <= 0.003 || strength <= 0.03) { return; }

  let slot = atomicAdd(&counters.foamHead, 1u) % P.foamParticleCount;
  let life = mix(P.foamLifeMin, P.foamLifeMax, r2.x);
  foamPool[slot] = FoamParticle(
    vec3<f32>(xz.x, sampleBed(xz) + f.x + 0.012, xz.y),
    0.06 + 0.30 * r2.y,
    vec2<f32>(0.0, life),
    vec2<f32>(r1.y, (r2.x - 0.5) * 1.6),
  );
}

// ------------------------------------------------------------------- pooling
@compute @workgroup_size(128)
fn splashInit(@builtin(global_invocation_id) id : vec3<u32>) {
  if (id.x >= P.splashCount) { return; }
  splashPool[id.x] = SplashParticle(vec3<f32>(0.0, -1000.0, 0.0), 0.0, vec3<f32>(0.0), 0.0);
}
@compute @workgroup_size(128)
fn foamPoolInit(@builtin(global_invocation_id) id : vec3<u32>) {
  if (id.x >= P.foamParticleCount) { return; }
  foamPool[id.x] = FoamParticle(vec3<f32>(0.0, -1000.0, 0.0), 0.0, vec2<f32>(1.0, 0.0), vec2<f32>(0.0));
}

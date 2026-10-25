//@@COMMON@@
// Frontier Fluid — shallow-water equations solver (compute).
//
// Explicit, collocated-grid SWE with:
//   * semi-Lagrangian momentum advection (crisp wakes behind vehicles)
//   * donor-cell (upwind) mass fluxes with a bed-barrier face depth, so
//     wet/dry shorelines stay clean and water cannot climb banks it shouldn't
//   * Jacobi-style viscosity (unconditionally stable — thick mud is safe)
//   * Manning-style bed friction, wind stress, river flow-map forcing
//   * volume-displacing interactors (wheels, hulls, characters)
//   * sources/sinks, rain, droplet-landing ripple feedback
//   * ocean boundary mode: sponge layer + level restore + swell injection
//
// Bindings (SIM layout, group 0):
//   0 SimParams | 1 fieldIn | 2 fieldOut | 3 bedFlow | 4 interactors
//   5 sources   | 6 landings | 7 landingMeta | 8 sampler | 9 foamIn | 10 foamOut

@group(0) @binding(0) var<uniform> P : SimParams;
@group(0) @binding(1) var fieldIn : texture_2d<f32>;
@group(0) @binding(2) var fieldOut : texture_storage_2d<rgba16float, write>;
@group(0) @binding(3) var bedFlow : texture_2d<f32>;
@group(0) @binding(4) var<storage, read> interactors : array<Interactor>;
@group(0) @binding(5) var<storage, read> sources : array<Source>;
@group(0) @binding(6) var<storage, read> landings : array<vec4<f32>>;
@group(0) @binding(7) var<storage, read_write> landingMeta : LandingMeta;
@group(0) @binding(8) var samp : sampler;

fn F(p : vec2<i32>) -> vec4<f32> { return textureLoad(fieldIn, clampCell(p, P.res), 0); }
fn B(p : vec2<i32>) -> vec4<f32> { return textureLoad(bedFlow, clampCell(p, P.res), 0); }

// Surface height of a neighbour, with the dry-cell rule: a dry neighbour that
// sits higher than us must not "pull" water uphill through the pressure gradient.
fn etaN(fN : vec4<f32>, bN : vec4<f32>, etaC : f32) -> f32 {
  let e = bN.x + fN.x;
  if (fN.x <= DEPTH_EPS) { return min(e, etaC); }
  return e;
}

// ---------------------------------------------------------------- initField
@compute @workgroup_size(8, 8)
fn initField(@builtin(global_invocation_id) id : vec3<u32>) {
  if (id.x >= P.res.x || id.y >= P.res.y) { return; }
  let xy = vec2<i32>(id.xy);
  let bed = B(xy).x;
  let d = max(0.0, P.waterLevel - bed);
  textureStore(fieldOut, xy, vec4<f32>(d, 0.0, 0.0, 0.0));
}

// ------------------------------------------------------------ velocityUpdate
@compute @workgroup_size(8, 8)
fn velocityUpdate(@builtin(global_invocation_id) id : vec3<u32>) {
  if (id.x >= P.res.x || id.y >= P.res.y) { return; }
  let xy = vec2<i32>(id.xy);
  let res = vec2<i32>(P.res);

  let c = F(xy);
  let bedC = B(xy);
  let d = c.x;
  var vel = c.yz;

  if (d > DEPTH_EPS) {
    // --- semi-Lagrangian self-advection of momentum
    let wpos = cellWorld(xy, P.res, P.center, P.worldSize);
    let btUV = worldUV(wpos - vel * P.dt, P.center, P.worldSize);
    vel = textureSampleLevel(fieldIn, samp, clamp(btUV, vec2<f32>(0.0), vec2<f32>(1.0)), 0.0).yz;

    // --- pressure gradient: -g * grad(eta)   (eta = bed + depth)
    let etaC = bedC.x + d;
    let eR = etaN(F(xy + vec2<i32>(1, 0)), B(xy + vec2<i32>(1, 0)), etaC);
    let eL = etaN(F(xy - vec2<i32>(1, 0)), B(xy - vec2<i32>(1, 0)), etaC);
    let eT = etaN(F(xy + vec2<i32>(0, 1)), B(xy + vec2<i32>(0, 1)), etaC);
    let eD = etaN(F(xy - vec2<i32>(0, 1)), B(xy - vec2<i32>(0, 1)), etaC);
    let grad = vec2<f32>(eR - eL, eT - eD) * (0.5 * P.invDx);
    vel -= P.gravity * P.dt * grad;

    // --- viscosity via Jacobi smoothing toward the neighbour mean
    //     (stable for any viscosity: essential for mud)
    let vAvg = 0.25 * (F(xy + vec2<i32>(1, 0)).yz + F(xy - vec2<i32>(1, 0)).yz
                     + F(xy + vec2<i32>(0, 1)).yz + F(xy - vec2<i32>(0, 1)).yz);
    let viscK = 1.0 - exp(-P.viscosity * P.dt * P.invDx * P.invDx * 4.0);
    vel = mix(vel, vAvg, clamp(viscK, 0.0, 0.92));

    // --- bed friction (stronger as water shallows) + global damping
    let fric = exp(-P.dt * P.bedFriction / max(d, 0.04));
    vel *= fric * exp(-P.damping * P.dt);

    // --- wind stress (only bites on reasonably deep, exposed water)
    vel += P.wind * P.dt * smoothstep(0.02, 0.35, d) * 0.06;

    // --- river flow-map forcing: relax toward baked channel velocity
    let chan = bedC.w;
    if (chan > 0.001 && d > 0.015) {
      vel = mix(vel, bedC.yz, chan * clamp(P.dt * 1.6, 0.0, 1.0));
    }

    // --- ocean swell injection on the west strip
    if (P.boundaryMode == 1u && xy.x < 5) {
      let phase = 6.2831853 * P.edgeWaveFreq * P.time + wpos.y * 0.045;
      let swellTarget = P.edgeWaveAmp * sin(phase);
      vel.x = mix(vel.x, swellTarget, 0.55);
    }

    // --- solid domain walls
    if (P.boundaryMode == 0u) {
      if (xy.x == 0) { vel.x = max(vel.x, 0.0); }
      if (xy.x == res.x - 1) { vel.x = min(vel.x, 0.0); }
      if (xy.y == 0) { vel.y = max(vel.y, 0.0); }
      if (xy.y == res.y - 1) { vel.y = min(vel.y, 0.0); }
    } else {
      // sponge layer: bleed momentum near the border so waves exit cleanly
      let edge = min(min(xy.x, xy.y), min(res.x - 1 - xy.x, res.y - 1 - xy.y));
      let sponge = smoothstep(0.0, 14.0, f32(edge));
      vel *= mix(0.86, 1.0, sponge);
    }

    // --- CFL-friendly speed clamp
    let maxSpeed = min(0.9 * P.dx / P.dt, 28.0);
    let sp = length(vel);
    if (sp > maxSpeed) { vel *= maxSpeed / sp; }
  } else {
    vel = vec2<f32>(0.0);
  }

  textureStore(fieldOut, xy, vec4<f32>(d, vel.x, vel.y, c.w));
}

// ---------------------------------------------------------- continuityUpdate
// Upwinded face depth limited by the higher of the two beds (the "sill"):
// water only crosses a face by the amount its surface clears the sill.
fn faceDepth(u : f32, fC : vec4<f32>, bC : f32, fN : vec4<f32>, bN : f32) -> f32 {
  let etaUp = select(bN + fN.x, bC + fC.x, u > 0.0);
  return max(0.0, etaUp - max(bC, bN));
}

@compute @workgroup_size(8, 8)
fn continuityUpdate(@builtin(global_invocation_id) id : vec3<u32>) {
  if (id.x >= P.res.x || id.y >= P.res.y) { return; }
  let xy = vec2<i32>(id.xy);
  let res = vec2<i32>(P.res);

  let c = F(xy);
  let bC = B(xy).x;
  let fR = F(xy + vec2<i32>(1, 0)); let bR = B(xy + vec2<i32>(1, 0)).x;
  let fL = F(xy - vec2<i32>(1, 0)); let bL = B(xy - vec2<i32>(1, 0)).x;
  let fT = F(xy + vec2<i32>(0, 1)); let bT = B(xy + vec2<i32>(0, 1)).x;
  let fD = F(xy - vec2<i32>(0, 1)); let bD = B(xy - vec2<i32>(0, 1)).x;

  // face velocities (average of the two adjacent cells), zeroed at walls
  var uE = 0.5 * (c.y + fR.y);
  var uW = 0.5 * (c.y + fL.y);
  var vN = 0.5 * (c.z + fT.z);
  var vS = 0.5 * (c.z + fD.z);
  if (P.boundaryMode == 0u) {
    if (xy.x == res.x - 1) { uE = 0.0; }
    if (xy.x == 0) { uW = 0.0; }
    if (xy.y == res.y - 1) { vN = 0.0; }
    if (xy.y == 0) { vS = 0.0; }
  }

  // Signed face fluxes in the +x / +z direction (volume per unit width).
  let fluxE = uE * faceDepth(uE, c, bC, fR, bR);
  let fluxW = uW * faceDepth(uW, fL, bL, c, bC);
  let fluxN = vN * faceDepth(vN, c, bC, fT, bT);
  let fluxS = vS * faceDepth(vS, fD, bD, c, bC);

  let div = (fluxE - fluxW) + (fluxN - fluxS);
  var d = c.x - P.dt * P.invDx * div;

  // rain adds water everywhere; evaporation removes it (puddles dry out)
  d += P.rainRate * P.dt;
  d = max(0.0, d - P.evaporation * P.dt);

  // ocean: relax depth toward rest level inside the sponge band
  if (P.boundaryMode == 1u) {
    let edge = min(min(xy.x, xy.y), min(res.x - 1 - xy.x, res.y - 1 - xy.y));
    let sponge = 1.0 - smoothstep(0.0, 16.0, f32(edge));
    let rest = max(0.0, P.waterLevel - bC);
    d = mix(d, rest, sponge * clamp(P.dt * 1.2, 0.0, 1.0));
  }

  d = max(d, 0.0);
  if (d < 0.00005) { d = 0.0; }
  var vel = c.yz;
  if (d <= DEPTH_EPS) { vel = vec2<f32>(0.0); }

  // --- splash/breaking energy: steep, fast-moving crests break
  var energy = c.w * exp(-2.6 * P.dt);
  if (d > 0.01) {
    let etaC = bC + d;
    let gx = ((bR + fR.x) - (bL + fL.x)) * 0.5 * P.invDx;
    let gz = ((bT + fT.x) - (bD + fD.x)) * 0.5 * P.invDx;
    let steep = length(vec2<f32>(gx, gz));
    let speed = length(vel);
    // Froude-scaled breaking: waves break when surface slope is high relative
    // to what the local depth can support.
    let breaking = smoothstep(0.42, 1.05, steep) * clamp(speed * 0.7, 0.0, 2.0);
    energy += breaking * P.dt * P.breakFoam;
    _ = etaC;
  }
  energy = min(energy, 4.0);

  textureStore(fieldOut, xy, vec4<f32>(d, vel.x, vel.y, energy));
}

// ------------------------------------------------------------------- interact
// Runs once per frame: rigid interactors displace volume and drag water with
// them (wheels, hulls, feet), sources inject/drain, rain drops pock the
// surface, and landed droplets feed ripples + energy back into the solver.
@compute @workgroup_size(8, 8)
fn interact(@builtin(global_invocation_id) id : vec3<u32>) {
  if (id.x >= P.res.x || id.y >= P.res.y) { return; }
  let xy = vec2<i32>(id.xy);
  var c = F(xy);
  let bed = B(xy).x;
  let wpos = cellWorld(xy, P.res, P.center, P.worldSize);
  var d = c.x;
  var vel = c.yz;
  var energy = c.w;
  let eta = bed + d;
  let dt = max(P.frameDt, 0.0001);

  // ---- rigid interactors
  for (var i = 0u; i < P.interactorCount; i++) {
    let it = interactors[i];
    let R = it.posR.w;
    if (R <= 0.0) { continue; }
    let off = wpos - it.posR.xz;
    let dist = length(off);
    let reach = R + P.dx * 1.5;
    if (dist > reach) { continue; }
    let kind = it.kindH.x;
    let halfH = select(R, max(it.kindH.y, 0.01), kind > 0.5);
    let bottom = it.posR.y - halfH;
    let top = it.posR.y + halfH;
    if (eta < bottom - P.dx || bed > top) { continue; } // fully above water / buried
    let submersion = clamp((eta - bottom) / max(2.0 * halfH, 0.001), 0.0, 1.0);
    if (submersion <= 0.0 || d <= DEPTH_EPS) { continue; }

    let infl = (1.0 - smoothstep(R * 0.45, reach, dist)) * submersion * it.velS.w;
    if (infl <= 0.0) { continue; }

    // 1) depress the surface under the body (volume displacement) — the
    //    solver piles the displaced water up around it as a bow wave.
    let targetD = max(0.0, bottom - bed);
    d = mix(d, min(d, targetD), clamp(infl * 0.55, 0.0, 0.85));

    // 2) drag water along with the body + radial outwash from impact/plunge.
    let hv = it.velS.xz;
    let hSpeed = length(hv);
    let plunge = max(0.0, -it.velS.y);
    vel = mix(vel, hv, clamp(infl * 0.5, 0.0, 0.8));
    var dir = vec2<f32>(0.0, 1.0);
    if (dist > 0.0001) { dir = off / dist; }
    let outwash = infl * (plunge * 1.6 + hSpeed * 0.22) * clamp(dt * 18.0, 0.0, 1.0);
    vel += dir * outwash;

    // 3) churn => splash + foam energy for the particle systems.
    energy += infl * (hSpeed * 0.30 + plunge * 1.5 + abs(it.velS.y) * 0.4) * dt * 14.0;
  }

  // ---- sources / sinks (river head, drain, hose, waterfall base...)
  for (var i = 0u; i < P.sourceCount; i++) {
    let s = sources[i];
    let R = s.posR.w;
    if (R <= 0.0) { continue; }
    let dist = length(wpos - s.posR.xz);
    if (dist > R) { continue; }
    let fall = 1.0 - smoothstep(0.0, R, dist);
    d = max(0.0, d + s.flowVel.x * fall * dt);
    if (s.flowVel.x > 0.0) {
      vel = mix(vel, s.flowVel.yz, fall * clamp(dt * 3.0, 0.0, 0.7));
      energy += fall * s.flowVel.x * dt * 1.2;
    }
  }

  // ---- rain: stochastic depth bumps become expanding rings via the solver
  if (P.rainRate > 0.0) {
    let cellId = u32(xy.x) + u32(xy.y) * P.res.x;
    let h = hashF((cellId * 2654435761u) ^ (P.frame * 40503u));
    let prob = clamp(P.rainRate * 220.0 * dt, 0.0, 0.6);
    if (h < prob) {
      d += 0.006 + 0.01 * hashF(cellId ^ 0x9e3779b9u ^ P.frame);
      energy += 0.012;
    }
  }

  // ---- droplet landings feed micro-ripples back in (closed loop)
  let lc = min(atomicLoad(&landingMeta.count), LANDING_FEEDBACK_MAX);
  for (var i = 0u; i < lc; i++) {
    let L = landings[i];
    if (L.z <= 0.0) { continue; }
    let off = wpos - L.xy;
    let dist = length(off);
    let rad = P.dx * 1.6;
    if (dist > rad || d <= DEPTH_EPS) { continue; }
    let fall = 1.0 - dist / rad;
    var dir = vec2<f32>(0.0, 1.0);
    if (dist > 0.0001) { dir = off / dist; }
    vel += dir * fall * L.z * 0.25;
    d += fall * L.z * 0.0015;
    energy += fall * L.z * 0.35;
  }

  energy = min(energy, 4.0);
  if (d <= DEPTH_EPS) { vel = vec2<f32>(0.0); }
  textureStore(fieldOut, xy, vec4<f32>(max(d, 0.0), vel.x, vel.y, energy));
}

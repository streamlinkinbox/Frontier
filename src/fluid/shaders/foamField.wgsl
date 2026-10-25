//@@COMMON@@
// Frontier Fluid — advected foam-density field + terrain wetness.
//
// This is NOT depth-fade "foam" painted in the water shader. Foam density is
// a simulated scalar: it is generated where the physics says water is being
// aerated (compression/crest breaking, shore wash, interactor churn,
// droplet landings), then transported by the actual simulated surface
// velocity (semi-Lagrangian advection) and dissipated over time. The foam
// PARTICLES (particles.wgsl) ride on top of this for sparkle and volume.
//
// foam texture: x = foam density, y = wetness (darkens terrain, slow to dry)
// Bindings (SIM layout): 0 P | 1 fieldIn | 3 bedFlow | 6 landings |
//                        7 landingMeta | 8 sampler | 9 foamIn | 10 foamOut

@group(0) @binding(0) var<uniform> P : SimParams;
@group(0) @binding(1) var fieldIn : texture_2d<f32>;
@group(0) @binding(3) var bedFlow : texture_2d<f32>;
@group(0) @binding(6) var<storage, read> landings : array<vec4<f32>>;
@group(0) @binding(7) var<storage, read_write> landingMeta : LandingMeta;
@group(0) @binding(8) var samp : sampler;
@group(0) @binding(9) var foamIn : texture_2d<f32>;
@group(0) @binding(10) var foamOut : texture_storage_2d<rgba16float, write>;

fn F(p : vec2<i32>) -> vec4<f32> { return textureLoad(fieldIn, clampCell(p, P.res), 0); }
fn B(p : vec2<i32>) -> f32 { return textureLoad(bedFlow, clampCell(p, P.res), 0).x; }

@compute @workgroup_size(8, 8)
fn foamUpdate(@builtin(global_invocation_id) id : vec3<u32>) {
  if (id.x >= P.res.x || id.y >= P.res.y) { return; }
  let xy = vec2<i32>(id.xy);
  let dt = max(P.frameDt, 0.0001);
  let wpos = cellWorld(xy, P.res, P.center, P.worldSize);
  let c = F(xy);
  let d = c.x;
  let vel = c.yz;
  let prev = textureLoad(foamIn, xy, 0);

  // ---- semi-Lagrangian advection by the simulated surface velocity
  let btUV = worldUV(wpos - vel * dt, P.center, P.worldSize);
  var foam = textureSampleLevel(foamIn, samp, clamp(btUV, vec2<f32>(0.0), vec2<f32>(1.0)), 0.0).x;

  // ---- dissipation (bubbles pop; faster in deep still water)
  foam *= exp(-P.foamDecay * dt);

  if (d > DEPTH_EPS) {
    // compression: converging flow (wave faces, wakes colliding) aerates water
    let div = (F(xy + vec2<i32>(1, 0)).y - F(xy - vec2<i32>(1, 0)).y
             + F(xy + vec2<i32>(0, 1)).z - F(xy - vec2<i32>(0, 1)).z) * 0.5 * P.invDx;
    let speed = length(vel);
    foam += max(0.0, -div) * speed * dt * P.breakFoam * 1.6;

    // crest steepness (breaking waves / standing waves in the river)
    let bC = B(xy);
    let gx = ((B(xy + vec2<i32>(1, 0)) + F(xy + vec2<i32>(1, 0)).x)
            - (B(xy - vec2<i32>(1, 0)) + F(xy - vec2<i32>(1, 0)).x)) * 0.5 * P.invDx;
    let gz = ((B(xy + vec2<i32>(0, 1)) + F(xy + vec2<i32>(0, 1)).x)
            - (B(xy - vec2<i32>(0, 1)) + F(xy - vec2<i32>(0, 1)).x)) * 0.5 * P.invDx;
    let steep = length(vec2<f32>(gx, gz));
    foam += smoothstep(0.35, 0.95, steep) * dt * P.breakFoam * 2.2;
    _ = bC;

    // shoreline wash: thin water moving over the bed leaves a lace edge
    let shore = (1.0 - smoothstep(0.0, 0.14, d)) * clamp(speed * 2.2, 0.0, 1.0);
    foam += shore * dt * P.shoreFoam;

    // interactor churn + wave-breaking energy deposited by the solver
    foam += c.w * dt * P.breakFoam * 2.6;

    // droplet landings pop foam in
    let lc = min(atomicLoad(&landingMeta.count), LANDING_FEEDBACK_MAX);
    for (var i = 0u; i < lc; i++) {
      let L = landings[i];
      if (L.z <= 0.0) { continue; }
      let dist = length(wpos - L.xy);
      let rad = P.dx * 1.8;
      if (dist <= rad) { foam += (1.0 - dist / rad) * L.z * 0.5; }
    }
  } else {
    // beached foam on drying ground vanishes quickly
    foam *= exp(-2.2 * dt);
  }
  foam = clamp(foam, 0.0, P.foamMax);

  // ---- wetness: saturates instantly under water, dries over ~30 s
  var wet = prev.y;
  if (d > 0.004) { wet = min(1.0, wet + dt * 6.0); }
  else { wet = max(0.0, wet - dt * 0.03); }

  textureStore(foamOut, xy, vec4<f32>(foam, wet, 0.0, 0.0));
}

@compute @workgroup_size(8, 8)
fn foamInit(@builtin(global_invocation_id) id : vec3<u32>) {
  if (id.x >= P.res.x || id.y >= P.res.y) { return; }
  let xy = vec2<i32>(id.xy);
  let d = F(xy).x;
  textureStore(foamOut, xy, vec4<f32>(0.0, select(0.0, 1.0, d > 0.004), 0.0, 0.0));
}

//@@FRAME@@
// Frontier Fluid — particle rendering.
// Splash: camera-facing droplets stretched along their velocity.
// Foam: flat flecks hugging the surface, procedural bubble breakup,
//       rotated by per-particle spin, faded over their simulated lifetime.

struct ParticleParams {
  splashColor : vec3<f32>,
  splashBright : f32,
  foamColor : vec3<f32>,
  foamBright : f32,
};
@group(1) @binding(0) var<uniform> PP : ParticleParams;

// ------------------------------------------------------------------- splash
struct SplashOut {
  @builtin(position) clipPos : vec4<f32>,
  @location(0) corner : vec2<f32>,
  @location(1) alpha : f32,
  @location(2) shade : f32,
};

@vertex
fn vsSplash(
  @location(0) corner : vec2<f32>,
  @location(1) posSize : vec4<f32>,
  @location(2) velLife : vec4<f32>,
) -> SplashOut {
  var out : SplashOut;
  let life = velLife.w;
  let size = posSize.w;
  if (life <= 0.0 || size <= 0.0) {
    out.clipPos = vec4<f32>(0.0, 0.0, 2.0, 1.0);
    out.corner = corner;
    out.alpha = 0.0;
    out.shade = 0.0;
    return out;
  }
  let pos = posSize.xyz;
  let vel = velLife.xyz;
  let speed = length(vel);

  // screen-space velocity direction => stretch the billboard along motion
  var velView = (F.view * vec4<f32>(vel, 0.0)).xy;
  let vl = length(velView);
  if (vl > 0.001) { velView /= vl; } else { velView = vec2<f32>(0.0, 1.0); }
  let stretch = 1.0 + clamp(speed * 0.16, 0.0, 1.8);
  let along = dot(corner, velView) * stretch;
  let across = dot(corner, vec2<f32>(-velView.y, velView.x));
  let c2 = velView * along + vec2<f32>(-velView.y, velView.x) * across;

  let right = vec3<f32>(F.view[0][0], F.view[1][0], F.view[2][0]);
  let up = vec3<f32>(F.view[0][1], F.view[1][1], F.view[2][1]);
  let wp = pos + (right * c2.x + up * c2.y) * size;
  out.clipPos = F.viewProj * vec4<f32>(wp, 1.0);
  out.corner = corner;
  out.alpha = smoothstep(0.0, 0.10, life) * 0.9;
  out.shade = 0.75 + clamp(speed * 0.06, 0.0, 0.5);
  return out;
}

@fragment
fn fsSplash(in : SplashOut) -> @location(0) vec4<f32> {
  let r = length(in.corner);
  let body = 1.0 - smoothstep(0.15, 1.0, r);
  let a = body * in.alpha;
  if (a < 0.004) { discard; }
  // cheap droplet shading: bright core, sun-side sparkle
  let glint = pow(clamp(1.0 - length(in.corner - vec2<f32>(0.25, 0.35)), 0.0, 1.0), 3.0);
  let col = PP.splashColor * PP.splashBright * in.shade + F.sunColor * glint * 0.7 * F.sunI;
  return vec4<f32>(col, a);
}

// --------------------------------------------------------------------- foam
struct FoamOut {
  @builtin(position) clipPos : vec4<f32>,
  @location(0) corner : vec2<f32>,
  @location(1) fade : f32,
  @location(2) seed : f32,
  @location(3) worldPos : vec3<f32>,
};

@vertex
fn vsFoam(
  @location(0) corner : vec2<f32>,
  @location(1) posSize : vec4<f32>,
  @location(2) misc : vec4<f32>, // age, life, seed, spin
) -> FoamOut {
  var out : FoamOut;
  let age = misc.x;
  let life = max(misc.y, 0.001);
  if (age >= life || posSize.w <= 0.0) {
    out.clipPos = vec4<f32>(0.0, 0.0, 2.0, 1.0);
    out.corner = corner;
    out.fade = 0.0;
    out.seed = 0.0;
    out.worldPos = vec3<f32>(0.0);
    return out;
  }
  let t = age / life;
  let ang = misc.z * 6.2831853 + F.time * misc.w;
  let ca = cos(ang);
  let sa = sin(ang);
  let rc = vec2<f32>(corner.x * ca - corner.y * sa, corner.x * sa + corner.y * ca);
  let wp = posSize.xyz + vec3<f32>(rc.x, 0.0, rc.y) * posSize.w;
  out.clipPos = F.viewProj * vec4<f32>(wp, 1.0);
  out.corner = corner;
  out.fade = smoothstep(0.0, 0.10, t) * (1.0 - smoothstep(0.55, 1.0, t));
  out.seed = misc.z;
  out.worldPos = wp;
  return out;
}

@fragment
fn fsFoam(in : FoamOut) -> @location(0) vec4<f32> {
  let r = length(in.corner);
  let disk = 1.0 - smoothstep(0.55, 1.0, r);
  // bubble breakup: two noise scales in the fleck's own space
  let n1 = vnoise(in.corner * 3.0 + in.seed * 61.0);
  let n2 = vnoise(in.corner * 7.0 - in.seed * 37.0 + F.time * 0.15);
  let bubbles = smoothstep(0.28, 0.75, n1 * 0.6 + n2 * 0.4 + (1.0 - r) * 0.25);
  let a = disk * bubbles * in.fade * 0.92;
  if (a < 0.004) { discard; }
  let shade = 0.78 + 0.30 * n2;
  var col = PP.foamColor * PP.foamBright * shade * (0.6 + 0.4 * F.sunI);
  col = applyFog(col, in.worldPos);
  return vec4<f32>(col, a);
}

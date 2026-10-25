// Shared per-frame uniforms for all render materials (group 0, dynamic offset
// selects main-camera slot 0 or reflection-camera slot 1).
struct Frame {
  viewProj : mat4x4<f32>,
  view : mat4x4<f32>,
  proj : mat4x4<f32>,
  invProj : mat4x4<f32>,
  camPos : vec3<f32>,
  time : f32,
  sunDir : vec3<f32>,     // toward the sun, normalised
  sunI : f32,
  sunColor : vec3<f32>,
  clipLevel : f32,        // world Y below which to discard (reflection pass)
  fogColor : vec3<f32>,
  clipEnabled : f32,
  screenSize : vec2<f32>,
  fogDensity : f32,
  waterLevel : f32,
};

@group(0) @binding(0) var<uniform> F : Frame;

fn applyFog(color : vec3<f32>, worldPos : vec3<f32>) -> vec3<f32> {
  let dist = length(worldPos - F.camPos);
  let f = 1.0 - exp(-F.fogDensity * dist);
  return mix(color, F.fogColor, clamp(f, 0.0, 1.0));
}

fn skyGradient(dir : vec3<f32>) -> vec3<f32> {
  let h = clamp(dir.y, -1.0, 1.0);
  let zen = vec3<f32>(0.16, 0.36, 0.62);
  let hor = vec3<f32>(0.74, 0.82, 0.88);
  let gnd = vec3<f32>(0.36, 0.34, 0.30);
  var c = mix(hor, zen, pow(smoothstep(0.0, 0.65, h), 0.8));
  c = mix(c, gnd, 1.0 - smoothstep(-0.25, 0.0, h));
  let sunAmt = max(dot(dir, F.sunDir), 0.0);
  c += F.sunColor * (pow(sunAmt, 350.0) * 3.0 + pow(sunAmt, 10.0) * 0.18) * F.sunI;
  return c;
}

fn hash21(p : vec2<f32>) -> f32 {
  var q = fract(p * vec2<f32>(234.34, 435.345));
  q += dot(q, q + 34.23);
  return fract(q.x * q.y);
}
fn vnoise(p : vec2<f32>) -> f32 {
  let i = floor(p);
  let f = fract(p);
  let u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash21(i), hash21(i + vec2<f32>(1.0, 0.0)), u.x),
             mix(hash21(i + vec2<f32>(0.0, 1.0)), hash21(i + vec2<f32>(1.0, 1.0)), u.x), u.y);
}

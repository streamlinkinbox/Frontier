// Final blit: ACES filmic tonemap + gamma to the canvas.
@group(0) @binding(0) var src : texture_2d<f32>;

struct VSOut { @builtin(position) clipPos : vec4<f32> };

@vertex
fn vsMain(@builtin(vertex_index) idx : u32) -> VSOut {
  var out : VSOut;
  let p = vec2<f32>(f32((idx << 1u) & 2u), f32(idx & 2u)) * 2.0 - 1.0;
  out.clipPos = vec4<f32>(p, 0.0, 1.0);
  return out;
}

fn aces(x : vec3<f32>) -> vec3<f32> {
  return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), vec3<f32>(0.0), vec3<f32>(1.0));
}

@fragment
fn fsMain(@builtin(position) fragPos : vec4<f32>) -> @location(0) vec4<f32> {
  let c = textureLoad(src, vec2<i32>(fragPos.xy), 0).rgb;
  let mapped = aces(c * 1.05);
  return vec4<f32>(pow(mapped, vec3<f32>(1.0 / 2.2)), 1.0);
}

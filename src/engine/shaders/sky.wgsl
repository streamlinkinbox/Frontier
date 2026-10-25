//@@FRAME@@
// Fullscreen sky. Drawn first (no depth write), everything else overdraws it.

struct VSOut {
  @builtin(position) clipPos : vec4<f32>,
  @location(0) dir : vec3<f32>,
};

@vertex
fn vsMain(@builtin(vertex_index) idx : u32) -> VSOut {
  var out : VSOut;
  let p = vec2<f32>(f32((idx << 1u) & 2u), f32(idx & 2u)) * 2.0 - 1.0;
  out.clipPos = vec4<f32>(p, 0.9999, 1.0);
  let v4 = F.invProj * vec4<f32>(p.x, p.y, 1.0, 1.0);
  let vd = normalize(v4.xyz / v4.w);
  let right = vec3<f32>(F.view[0][0], F.view[1][0], F.view[2][0]);
  let up = vec3<f32>(F.view[0][1], F.view[1][1], F.view[2][1]);
  let back = vec3<f32>(F.view[0][2], F.view[1][2], F.view[2][2]);
  out.dir = normalize(right * vd.x + up * vd.y + back * vd.z);
  return out;
}

struct FSOut {
  @location(0) color : vec4<f32>,
  @location(1) linDepth : vec4<f32>,
};

@fragment
fn fsMain(in : VSOut) -> FSOut {
  var out : FSOut;
  out.color = vec4<f32>(skyGradient(normalize(in.dir)), 1.0);
  out.linDepth = vec4<f32>(900.0, 0.0, 0.0, 0.0);
  return out;
}

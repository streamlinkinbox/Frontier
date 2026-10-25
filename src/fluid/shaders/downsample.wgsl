//@@COMMON@@
// Frontier Fluid — downsample the sim to a small grid for CPU readback.
// Gameplay code (buoyancy, AI, audio, gameplay triggers) samples water
// height/velocity on the CPU from this buffer with one frame of latency.
// Output per cell: (surface height eta, u, v, depth).
// Bindings (DOWN layout): 0 P | 1 field | 2 bedFlow | 3 sampler | 4 readback

@group(0) @binding(0) var<uniform> P : SimParams;
@group(0) @binding(1) var field : texture_2d<f32>;
@group(0) @binding(2) var bedFlow : texture_2d<f32>;
@group(0) @binding(3) var samp : sampler;
@group(0) @binding(4) var<storage, read_write> readback : array<vec4<f32>>;

const RB_RES : u32 = 128u;

@compute @workgroup_size(8, 8)
fn downsampleMain(@builtin(global_invocation_id) id : vec3<u32>) {
  if (id.x >= RB_RES || id.y >= RB_RES) { return; }
  let uv = (vec2<f32>(id.xy) + 0.5) / f32(RB_RES);
  let f = textureSampleLevel(field, samp, uv, 0.0);
  let bed = textureSampleLevel(bedFlow, samp, uv, 0.0).x;
  readback[id.y * RB_RES + id.x] = vec4<f32>(bed + f.x, f.y, f.z, f.x);
}

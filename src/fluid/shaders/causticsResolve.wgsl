//@@COMMON@@
// Frontier Fluid — caustics resolve: fixed-point accumulation -> soft texture.
// Bindings (RESOLVE layout): 0 P | 1 accumRead | 2 causticsOut | 3 field

@group(0) @binding(0) var<uniform> P : SimParams;
@group(0) @binding(1) var<storage, read> accumRead : array<u32>;
@group(0) @binding(2) var causticsOut : texture_storage_2d<rgba16float, write>;
@group(0) @binding(3) var field : texture_2d<f32>;

fn acc(x : i32, y : i32) -> f32 {
  let cx = clamp(x, 0, i32(P.res.x) - 1);
  let cy = clamp(y, 0, i32(P.res.y) - 1);
  return f32(accumRead[u32(cy) * P.res.x + u32(cx)]) / 220.0;
}

@compute @workgroup_size(8, 8)
fn causticsResolve(@builtin(global_invocation_id) id : vec3<u32>) {
  if (id.x >= P.res.x || id.y >= P.res.y) { return; }
  let x = i32(id.x);
  let y = i32(id.y);
  // 3x3 tent blur — hides scatter granularity, mimics penumbra/area light.
  var v = 0.0;
  v += acc(x, y) * 4.0;
  v += (acc(x + 1, y) + acc(x - 1, y) + acc(x, y + 1) + acc(x, y - 1)) * 2.0;
  v += acc(x + 1, y + 1) + acc(x - 1, y - 1) + acc(x + 1, y - 1) + acc(x - 1, y + 1);
  v /= 16.0;
  // filmic-ish soft knee so filaments bloom without clipping to white slabs
  let c = v / (1.0 + v * 0.55);
  // only meaningful under water — zero it on dry cells (terrain is above eta)
  let d = textureLoad(field, vec2<i32>(id.xy), 0).x;
  let mask = smoothstep(0.0, 0.05, d);
  textureStore(causticsOut, vec2<i32>(id.xy), vec4<f32>(c * mask, 0.0, 0.0, 0.0));
}

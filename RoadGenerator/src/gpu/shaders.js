// WGSL shaders for the deferred renderer. Validated by `npm run check:wgsl` (naga).

export const COMMON = /* wgsl */ `
struct Frame {
  view : mat4x4<f32>,
  proj : mat4x4<f32>,
  viewProj : mat4x4<f32>,
  invViewProj : mat4x4<f32>,
  invProj : mat4x4<f32>,
  shadow0 : mat4x4<f32>,
  shadow1 : mat4x4<f32>,
  cameraPos : vec4<f32>,     // xyz, w = time
  sunDir : vec4<f32>,        // xyz toward sun, w = intensity
  sunColor : vec4<f32>,      // rgb, w = ambient scale
  skyZenith : vec4<f32>,
  skyHorizon : vec4<f32>,
  groundColor : vec4<f32>,
  params : vec4<f32>,        // x = cascade split distance, y = near, z = far, w = exposure
  viewport : vec4<f32>,      // width, height, 1/w, 1/h
  select : vec4<u32>,        // x = selected pick id, y = hovered id, z = flags (bit0 wireframe tint, bit1 underground view), w = unused
  fog : vec4<f32>,           // rgb colour, w density
};
@group(0) @binding(0) var<uniform> F : Frame;

fn linearDepth(d : f32) -> f32 {
  // standard [0,1] perspective depth → view-space distance along -Z
  let n = F.params.y; let f = F.params.z;
  return n * f / (f - d * (f - n));
}
fn worldFromDepth(uv : vec2<f32>, depth : f32) -> vec3<f32> {
  let ndc = vec4<f32>(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
  let p = F.invViewProj * ndc;
  return p.xyz / p.w;
}
fn hash12(p : vec2<f32>) -> f32 {
  var p3 = fract(vec3<f32>(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}
fn hash22(p : vec2<f32>) -> vec2<f32> {
  var p3 = fract(vec3<f32>(p.xyx) * vec3<f32>(0.1031, 0.1030, 0.0973));
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.xx + p3.yz) * p3.zy);
}
fn vnoise(p : vec2<f32>) -> f32 {
  let i = floor(p); let f = fract(p);
  let u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash12(i), hash12(i + vec2<f32>(1.0, 0.0)), u.x), mix(hash12(i + vec2<f32>(0.0, 1.0)), hash12(i + vec2<f32>(1.0, 1.0)), u.x), u.y);
}
fn fbm(p : vec2<f32>) -> f32 {
  var v = 0.0; var a = 0.5; var q = p;
  for (var i = 0; i < 4; i++) { v += a * vnoise(q); q = q * 2.03 + vec2<f32>(17.1, 9.7); a *= 0.5; }
  return v;
}
`;

// ---------------------------------------------------------------------------------------------
// G-buffer pass: static + dynamic meshes, optional instancing via storage buffer.
// ---------------------------------------------------------------------------------------------
export const GBUFFER = /* wgsl */ `
${COMMON}
struct Draw {
  model : mat4x4<f32>,
  normalMat : mat4x4<f32>,
  material : u32,
  pickId : u32,
  flags : u32,        // bit0 = instanced, bit1 = paint (alpha-test with wear), bit2 = emissive, bit3 = no decals, bit4 = per-instance palette
  instanceBase : u32,
  tint : vec4<f32>,   // rgb multiply, a = emissive strength
  uvScale : vec4<f32>,// x = uv scale override (0 = material default), y = roughness bias, z = wear strength, w = unused
};
@group(1) @binding(0) var<uniform> D : Draw;
@group(1) @binding(1) var<storage, read> instances : array<mat4x4<f32>>;

struct MaterialParams { uvScale : f32, roughness : f32, metal : f32, normalStrength : f32, tint : vec4<f32>, wearTint : vec4<f32> };
@group(2) @binding(0) var albedoTex : texture_2d_array<f32>;
@group(2) @binding(1) var normalTex : texture_2d_array<f32>;
@group(2) @binding(2) var ormTex : texture_2d_array<f32>;
@group(2) @binding(3) var matSampler : sampler;
@group(2) @binding(4) var<storage, read> materials : array<MaterialParams>;

struct VSIn {
  @location(0) pos : vec3<f32>,
  @location(1) nrm : vec3<f32>,
  @location(2) uv : vec2<f32>,
  @location(3) col : vec4<f32>,
  @builtin(instance_index) inst : u32,
};
struct VSOut {
  @builtin(position) clip : vec4<f32>,
  @location(0) wpos : vec3<f32>,
  @location(1) nrm : vec3<f32>,
  @location(2) uv : vec2<f32>,
  @location(3) col : vec4<f32>,
  @location(4) @interpolate(flat) inst : u32,
};
@vertex fn vs(in : VSIn) -> VSOut {
  var model = D.model;
  if ((D.flags & 1u) != 0u) { model = instances[D.instanceBase + in.inst]; }
  let wp = model * vec4<f32>(in.pos, 1.0);
  var o : VSOut;
  o.clip = F.viewProj * wp;
  o.wpos = wp.xyz;
  // normal matrix: use model rotation (uniform scale assumed for instances)
  o.nrm = normalize((model * vec4<f32>(in.nrm, 0.0)).xyz);
  o.uv = in.uv;
  o.col = in.col;
  o.inst = in.inst;
  return o;
}
// Per-instance palette (bit4): hue-varied body colours for vehicles sharing one draw.
fn instancePalette(i : u32) -> vec3<f32> {
  let h = f32((i * 2654435761u) % 1000u) / 1000.0;
  let s = f32((i * 40503u + 7u) % 1000u) / 1000.0;
  // mostly neutral car park: white/silver/black/grey with occasional saturated colours
  if (s < 0.28) { return vec3<f32>(0.92, 0.92, 0.9); }
  if (s < 0.5) { return vec3<f32>(0.55, 0.56, 0.58); }
  if (s < 0.68) { return vec3<f32>(0.06, 0.06, 0.07); }
  if (s < 0.8) { return vec3<f32>(0.3, 0.3, 0.32); }
  let c = clamp(abs(fract(h + vec3<f32>(0.0, 0.667, 0.333)) * 6.0 - 3.0) - 1.0, vec3<f32>(0.0), vec3<f32>(1.0));
  return mix(vec3<f32>(0.1), c * 0.7, 0.9);
}
struct GOut {
  @location(0) albedo : vec4<f32>,
  @location(1) normal : vec4<f32>,
  @location(2) orm : vec4<f32>,
  @location(3) id : u32,
};
// Cotangent frame from screen-space derivatives (no vertex tangents needed).
fn perturbNormal(N : vec3<f32>, p : vec3<f32>, uv : vec2<f32>, mapN : vec3<f32>) -> vec3<f32> {
  let dp1 = dpdx(p); let dp2 = dpdy(p);
  let duv1 = dpdx(uv); let duv2 = dpdy(uv);
  let dp2perp = cross(dp2, N); let dp1perp = cross(N, dp1);
  let T = dp2perp * duv1.x + dp1perp * duv2.x;
  let B = dp2perp * duv1.y + dp1perp * duv2.y;
  let invmax = inverseSqrt(max(dot(T, T), dot(B, B)) + 1e-12);
  let TBN = mat3x3<f32>(T * invmax, B * invmax, N);
  return normalize(TBN * mapN);
}
@fragment fn fs(in : VSOut, @builtin(front_facing) ff : bool) -> GOut {
  let M = materials[D.material];
  var scale = M.uvScale;
  if (D.uvScale.x > 0.0) { scale = D.uvScale.x; }
  let uv = in.uv / scale;
  let layer = i32(D.material);
  var alb = textureSample(albedoTex, matSampler, uv, layer);
  let nm = textureSample(normalTex, matSampler, uv, layer).xyz * 2.0 - 1.0;
  let orm = textureSample(ormTex, matSampler, uv, layer);
  // second octave sample at a different scale breaks tiling repetition
  let alb2 = textureSample(albedoTex, matSampler, uv * 0.137 + vec2<f32>(0.31, 0.77), layer);
  let detailMix = 0.35;
  alb = mix(alb, alb * (alb2 * 1.6 + 0.2), detailMix);
  var N = normalize(in.nrm);
  if (!ff) { N = -N; }
  let mapN = normalize(vec3<f32>(nm.xy * M.normalStrength, nm.z));
  N = perturbNormal(N, in.wpos, uv, mapN);
  var rough = clamp(orm.g * M.roughness + D.uvScale.y, 0.03, 1.0);
  var color = alb.rgb * M.tint.rgb * D.tint.rgb;
  if ((D.flags & 16u) != 0u) { color *= instancePalette(in.inst) * 1.6; }
  // Paint: alpha test with procedural wear so markings look driven-over.
  if ((D.flags & 2u) != 0u) {
    let w = fbm(in.wpos.xz * 1.7) * 0.6 + fbm(in.wpos.xz * 9.0) * 0.4;
    let wearAmt = D.uvScale.z;
    let edge = 0.42 + wearAmt * 0.45;
    if (w < edge - 0.5) { discard; }
    // thin/faded paint: blend toward the asphalt tone
    let fade = smoothstep(edge - 0.2, edge + 0.25, w);
    color = mix(vec3<f32>(0.16, 0.16, 0.16), color, 0.35 + 0.65 * fade);
    rough = mix(0.75, 0.45, fade);
  }
  // Vertex wear (wheel paths): darker, smoother, slightly polished asphalt; variation channel tints.
  let wear = in.col.r;
  let variation = in.col.b;
  color = mix(color, color * M.wearTint.rgb, wear * M.wearTint.a);
  color *= 0.92 + 0.16 * variation;
  rough = mix(rough, rough * 0.8, wear * 0.6);
  var ao = orm.r * in.col.g;
  var o : GOut;
  o.albedo = vec4<f32>(color, select(0.0, D.tint.a, (D.flags & 4u) != 0u));
  o.normal = vec4<f32>(N * 0.5 + 0.5, select(1.0, 0.0, (D.flags & 8u) != 0u));
  o.orm = vec4<f32>(ao, rough, clamp(orm.b * M.metal + M.metal * 0.0, 0.0, 1.0), 1.0);
  o.id = D.pickId;
  return o;
}
`;

// ---------------------------------------------------------------------------------------------
// Shadow depth pass
// ---------------------------------------------------------------------------------------------
export const SHADOW = /* wgsl */ `
${COMMON}
struct Draw { model : mat4x4<f32>, normalMat : mat4x4<f32>, material : u32, pickId : u32, flags : u32, instanceBase : u32, tint : vec4<f32>, uvScale : vec4<f32> };
@group(1) @binding(0) var<uniform> D : Draw;
@group(1) @binding(1) var<storage, read> instances : array<mat4x4<f32>>;
struct Cascade { mat : mat4x4<f32> };
@group(2) @binding(0) var<uniform> C : Cascade;
@vertex fn vs(@location(0) pos : vec3<f32>, @builtin(instance_index) inst : u32) -> @builtin(position) vec4<f32> {
  var model = D.model;
  if ((D.flags & 1u) != 0u) { model = instances[D.instanceBase + inst]; }
  return C.mat * model * vec4<f32>(pos, 1.0);
}
@fragment fn fs() {}
`;

// ---------------------------------------------------------------------------------------------
// Deferred decals: oriented boxes projected onto the depth buffer, blended into the g-buffer.
// ---------------------------------------------------------------------------------------------
export const DECAL = /* wgsl */ `
${COMMON}
struct Decal { invModel : mat4x4<f32>, model : mat4x4<f32>, layer : u32, flags : u32, pad0 : u32, pad1 : u32, params : vec4<f32> }; // params: x opacity, y normal strength, z roughness, w rotation
@group(1) @binding(0) var<storage, read> decals : array<Decal>;
@group(2) @binding(0) var depthTex : texture_depth_2d;
@group(2) @binding(1) var decalAlbedo : texture_2d_array<f32>;
@group(2) @binding(2) var decalNormal : texture_2d_array<f32>;
@group(2) @binding(3) var decalSampler : sampler;
@group(2) @binding(4) var normalBuf : texture_2d<f32>;
struct VSOut { @builtin(position) clip : vec4<f32>, @location(0) @interpolate(flat) id : u32 };
// unit cube, 36 vertices from vertex_index
fn cubeVertex(i : u32) -> vec3<f32> {
  let faces = array<vec3<f32>, 36>(
    vec3<f32>(-1.0,-1.0, 1.0), vec3<f32>( 1.0,-1.0, 1.0), vec3<f32>( 1.0, 1.0, 1.0), vec3<f32>(-1.0,-1.0, 1.0), vec3<f32>( 1.0, 1.0, 1.0), vec3<f32>(-1.0, 1.0, 1.0),
    vec3<f32>( 1.0,-1.0,-1.0), vec3<f32>(-1.0,-1.0,-1.0), vec3<f32>(-1.0, 1.0,-1.0), vec3<f32>( 1.0,-1.0,-1.0), vec3<f32>(-1.0, 1.0,-1.0), vec3<f32>( 1.0, 1.0,-1.0),
    vec3<f32>(-1.0,-1.0,-1.0), vec3<f32>(-1.0,-1.0, 1.0), vec3<f32>(-1.0, 1.0, 1.0), vec3<f32>(-1.0,-1.0,-1.0), vec3<f32>(-1.0, 1.0, 1.0), vec3<f32>(-1.0, 1.0,-1.0),
    vec3<f32>( 1.0,-1.0, 1.0), vec3<f32>( 1.0,-1.0,-1.0), vec3<f32>( 1.0, 1.0,-1.0), vec3<f32>( 1.0,-1.0, 1.0), vec3<f32>( 1.0, 1.0,-1.0), vec3<f32>( 1.0, 1.0, 1.0),
    vec3<f32>(-1.0, 1.0, 1.0), vec3<f32>( 1.0, 1.0, 1.0), vec3<f32>( 1.0, 1.0,-1.0), vec3<f32>(-1.0, 1.0, 1.0), vec3<f32>( 1.0, 1.0,-1.0), vec3<f32>(-1.0, 1.0,-1.0),
    vec3<f32>(-1.0,-1.0,-1.0), vec3<f32>( 1.0,-1.0,-1.0), vec3<f32>( 1.0,-1.0, 1.0), vec3<f32>(-1.0,-1.0,-1.0), vec3<f32>( 1.0,-1.0, 1.0), vec3<f32>(-1.0,-1.0, 1.0));
  return faces[i];
}
@vertex fn vs(@builtin(vertex_index) vi : u32, @builtin(instance_index) ii : u32) -> VSOut {
  let d = decals[ii];
  let p = d.model * vec4<f32>(cubeVertex(vi), 1.0);
  var o : VSOut; o.clip = F.viewProj * p; o.id = ii; return o;
}
struct DOut { @location(0) albedo : vec4<f32>, @location(1) normal : vec4<f32>, @location(2) orm : vec4<f32> };
@fragment fn fs(in : VSOut) -> DOut {
  let d = decals[in.id];
  let uvScreen = in.clip.xy * F.viewport.zw;
  let depth = textureLoad(depthTex, vec2<i32>(in.clip.xy), 0);
  let wp = worldFromDepth(uvScreen, depth);
  let lp = (d.invModel * vec4<f32>(wp, 1.0)).xyz;
  if (any(abs(lp) > vec3<f32>(1.0))) { discard; }
  // surface must face roughly up (don't project onto kerb faces/poles)
  let nb = textureLoad(normalBuf, vec2<i32>(in.clip.xy), 0);
  if (nb.a < 0.5) { discard; }
  let sn = nb.xyz * 2.0 - 1.0;
  if (sn.y < 0.6) { discard; }
  let uv = lp.xz * 0.5 + 0.5;
  let sizeW = length((d.model * vec4<f32>(1.0, 0.0, 0.0, 0.0)).xyz) * 2.0;
  let texelsPerMetre = f32(textureDimensions(decalAlbedo).x) / max(sizeW, 0.01);
  let pixelWorld = distance(wp, F.cameraPos.xyz) * 2.0 * F.viewport.z; // approx world size of a pixel
  let lod = clamp(log2(max(pixelWorld * texelsPerMetre, 1e-4)), 0.0, 8.0);
  let a = textureSampleLevel(decalAlbedo, decalSampler, uv, i32(d.layer), lod);
  let n = textureSampleLevel(decalNormal, decalSampler, uv, i32(d.layer), lod);
  // fade at the box's vertical extents
  let edge = 1.0 - smoothstep(0.7, 1.0, abs(lp.y));
  let alpha = a.a * d.params.x * edge;
  if (alpha < 0.01) { discard; }
  // tangent-space normal → world, using the decal frame (x = model right, z = model forward)
  let tn = normalize(vec3<f32>((n.xy * 2.0 - 1.0) * d.params.y, n.z));
  let R = normalize((d.model * vec4<f32>(1.0, 0.0, 0.0, 0.0)).xyz);
  let Fw = normalize((d.model * vec4<f32>(0.0, 0.0, 1.0, 0.0)).xyz);
  let U = vec3<f32>(0.0, 1.0, 0.0);
  let wn = normalize(R * tn.x + Fw * tn.y + U * tn.z);
  var o : DOut;
  o.albedo = vec4<f32>(a.rgb, alpha);
  o.normal = vec4<f32>(wn * 0.5 + 0.5, alpha * d.params.y);
  o.orm = vec4<f32>(1.0, d.params.z, 0.0, alpha);
  return o;
}
`;

// ---------------------------------------------------------------------------------------------
// SSAO (half resolution) + blur
// ---------------------------------------------------------------------------------------------
export const SSAO = /* wgsl */ `
${COMMON}
@group(1) @binding(0) var depthTex : texture_depth_2d;
@group(1) @binding(1) var normalTex : texture_2d<f32>;
@group(1) @binding(2) var samp : sampler;
struct VSOut { @builtin(position) clip : vec4<f32>, @location(0) uv : vec2<f32> };
@vertex fn vs(@builtin(vertex_index) vi : u32) -> VSOut {
  var pos = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
  var o : VSOut; o.clip = vec4<f32>(pos[vi], 0.0, 1.0); o.uv = pos[vi] * vec2<f32>(0.5, -0.5) + 0.5; return o;
}
@fragment fn fs(in : VSOut) -> @location(0) vec4<f32> {
  let d = textureSampleLevel(depthTex, samp, in.uv, 0);
  if (d >= 0.99999) { return vec4<f32>(1.0); }
  let P = worldFromDepth(in.uv, d);
  let N = normalize(textureSampleLevel(normalTex, samp, in.uv, 0.0).xyz * 2.0 - 1.0);
  let dist = distance(P, F.cameraPos.xyz);
  let radius = clamp(dist * 0.02, 0.25, 1.4);
  let noise = hash22(in.clip.xy);
  var occ = 0.0;
  let samplesN = 12;
  // build basis
  let up = select(vec3<f32>(0.0, 1.0, 0.0), vec3<f32>(1.0, 0.0, 0.0), abs(N.y) > 0.9);
  let T = normalize(cross(up, N)); let B = cross(N, T);
  for (var i = 0; i < samplesN; i++) {
    let fi = f32(i);
    let ang = (fi + noise.x) * 2.399963;
    let r = sqrt((fi + 0.5 + noise.y) / f32(samplesN));
    let h = 1.0 - r * r;
    let dir = T * (cos(ang) * r) + B * (sin(ang) * r) + N * (h * 0.8 + 0.2);
    let S = P + dir * radius;
    let c = F.viewProj * vec4<f32>(S, 1.0);
    let suv = (c.xy / c.w) * vec2<f32>(0.5, -0.5) + 0.5;
    if (any(suv < vec2<f32>(0.0)) || any(suv > vec2<f32>(1.0))) { continue; }
    let sd = textureSampleLevel(depthTex, samp, suv, 0);
    let sceneZ = linearDepth(sd);
    let sampleZ = linearDepth(c.z / c.w);
    let diff = sampleZ - sceneZ;
    if (diff > 0.03) { occ += smoothstep(0.0, 1.0, radius / max(diff, 1e-3)); }
  }
  let ao = 1.0 - occ / f32(samplesN);
  return vec4<f32>(clamp(ao, 0.0, 1.0));
}
`;

export const BLUR = /* wgsl */ `
${COMMON}
@group(1) @binding(0) var src : texture_2d<f32>;
@group(1) @binding(1) var depthTex : texture_depth_2d;
@group(1) @binding(2) var samp : sampler;
struct VSOut { @builtin(position) clip : vec4<f32>, @location(0) uv : vec2<f32> };
@vertex fn vs(@builtin(vertex_index) vi : u32) -> VSOut {
  var pos = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
  var o : VSOut; o.clip = vec4<f32>(pos[vi], 0.0, 1.0); o.uv = pos[vi] * vec2<f32>(0.5, -0.5) + 0.5; return o;
}
@fragment fn fs(in : VSOut) -> @location(0) vec4<f32> {
  let dims = vec2<f32>(textureDimensions(src));
  let texel = 1.0 / dims;
  let d0 = linearDepth(textureSampleLevel(depthTex, samp, in.uv, 0));
  var sum = 0.0; var wsum = 0.0;
  for (var y = -2; y <= 2; y++) { for (var x = -2; x <= 2; x++) {
    let o = vec2<f32>(f32(x), f32(y)) * texel;
    let d = linearDepth(textureSampleLevel(depthTex, samp, in.uv + o, 0));
    let w = exp(-abs(d - d0) * 2.0);
    sum += textureSampleLevel(src, samp, in.uv + o, 0.0).r * w; wsum += w;
  } }
  return vec4<f32>(sum / max(wsum, 1e-4));
}
`;

// ---------------------------------------------------------------------------------------------
// Lighting (fullscreen): PBR + cascaded shadows + sky ambient + fog + selection outline + sky
// ---------------------------------------------------------------------------------------------
export const LIGHTING = /* wgsl */ `
${COMMON}
@group(1) @binding(0) var albedoTex : texture_2d<f32>;
@group(1) @binding(1) var normalTex : texture_2d<f32>;
@group(1) @binding(2) var ormTex : texture_2d<f32>;
@group(1) @binding(3) var depthTex : texture_depth_2d;
@group(1) @binding(4) var idTex : texture_2d<u32>;
@group(1) @binding(5) var aoTex : texture_2d<f32>;
@group(1) @binding(6) var shadowTex : texture_depth_2d_array;
@group(1) @binding(7) var shadowSamp : sampler_comparison;
@group(1) @binding(8) var linSamp : sampler;
struct VSOut { @builtin(position) clip : vec4<f32>, @location(0) uv : vec2<f32> };
@vertex fn vs(@builtin(vertex_index) vi : u32) -> VSOut {
  var pos = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
  var o : VSOut; o.clip = vec4<f32>(pos[vi], 0.0, 1.0); o.uv = pos[vi] * vec2<f32>(0.5, -0.5) + 0.5; return o;
}
fn D_GGX(NoH : f32, a : f32) -> f32 { let a2 = a * a; let d = NoH * NoH * (a2 - 1.0) + 1.0; return a2 / (3.14159265 * d * d + 1e-7); }
fn V_Smith(NoV : f32, NoL : f32, a : f32) -> f32 { let a2 = a * a; let gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2); let gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2); return 0.5 / (gv + gl + 1e-5); }
fn F_Schlick(VoH : f32, f0 : vec3<f32>) -> vec3<f32> { return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0); }
fn shadow(wp : vec3<f32>, N : vec3<f32>, viewDist : f32) -> f32 {
  var layer = 0; var m = F.shadow0;
  if (viewDist > F.params.x) { layer = 1; m = F.shadow1; }
  let bias = N * (select(0.06, 0.25, layer == 1)) ;
  let lp = m * vec4<f32>(wp + bias, 1.0);
  let uv = lp.xy * vec2<f32>(0.5, -0.5) + 0.5;
  if (any(uv < vec2<f32>(0.0)) || any(uv > vec2<f32>(1.0)) || lp.z > 1.0) { return 1.0; }
  let texel = 1.0 / f32(textureDimensions(shadowTex).x);
  var s = 0.0;
  for (var y = -1; y <= 1; y++) { for (var x = -1; x <= 1; x++) {
    s += textureSampleCompareLevel(shadowTex, shadowSamp, uv + vec2<f32>(f32(x), f32(y)) * texel * 1.2, layer, lp.z - 0.0008);
  } }
  return s / 9.0;
}
fn sky(dir : vec3<f32>) -> vec3<f32> {
  let t = clamp(dir.y, -1.0, 1.0);
  var c = mix(F.skyHorizon.rgb, F.skyZenith.rgb, pow(max(t, 0.0), 0.45));
  if (t < 0.0) { c = mix(F.skyHorizon.rgb, F.groundColor.rgb, pow(-t, 0.5)); }
  // sun disc + glow
  let sd = max(dot(dir, F.sunDir.xyz), 0.0);
  c += F.sunColor.rgb * (pow(sd, 1200.0) * 40.0 + pow(sd, 8.0) * 0.08) * F.sunDir.w;
  return c;
}
@fragment fn fs(in : VSOut) -> @location(0) vec4<f32> {
  let px = vec2<i32>(in.clip.xy);
  let depth = textureLoad(depthTex, px, 0);
  let camDir = normalize(worldFromDepth(in.uv, 0.5) - F.cameraPos.xyz);
  var color : vec3<f32>;
  if (depth >= 0.99999) {
    color = sky(camDir);
  } else {
    let alb = textureLoad(albedoTex, px, 0);
    let nrm = textureLoad(normalTex, px, 0);
    let orm = textureLoad(ormTex, px, 0);
    let ssao = textureSampleLevel(aoTex, linSamp, in.uv, 0.0).r;
    let P = worldFromDepth(in.uv, depth);
    let N = normalize(nrm.xyz * 2.0 - 1.0);
    let V = normalize(F.cameraPos.xyz - P);
    let L = normalize(F.sunDir.xyz);
    let H = normalize(V + L);
    let rough = clamp(orm.g, 0.04, 1.0);
    let metal = orm.b;
    let a = rough * rough;
    let NoL = max(dot(N, L), 0.0); let NoV = max(dot(N, V), 1e-4); let NoH = max(dot(N, H), 0.0); let VoH = max(dot(V, H), 0.0);
    let f0 = mix(vec3<f32>(0.04), alb.rgb, metal);
    let diffuseColor = alb.rgb * (1.0 - metal);
    let Fs = F_Schlick(VoH, f0);
    let spec = D_GGX(NoH, a) * V_Smith(NoV, NoL, a) * Fs;
    let viewDist = distance(P, F.cameraPos.xyz);
    let sh = shadow(P, N, viewDist);
    let sunRadiance = F.sunColor.rgb * F.sunDir.w;
    let direct = (diffuseColor / 3.14159265 + spec) * sunRadiance * NoL * sh;
    // ambient: hemispherical sky + ground bounce, GGX-ish specular approximation
    let ao = orm.r * ssao;
    let skyAmb = mix(F.groundColor.rgb, F.skyZenith.rgb, N.y * 0.5 + 0.5) * F.sunColor.w;
    let R = reflect(-V, N);
    let specAmb = sky(R) * F_Schlick(NoV, f0) * (1.0 - rough * 0.85) * F.sunColor.w * 0.35;
    let ambient = (diffuseColor * skyAmb + specAmb) * ao;
    color = direct + ambient + alb.rgb * alb.a * 4.0;
    // underground X-ray tint
    if ((F.select.z & 2u) != 0u && P.y < -0.3) { color = mix(color, vec3<f32>(0.2, 0.6, 1.0), 0.15); }
    // fog
    let fogAmt = 1.0 - exp(-viewDist * F.fog.w);
    color = mix(color, sky(camDir) * 0.9 + F.fog.rgb * 0.1, clamp(fogAmt, 0.0, 1.0));
  }
  // selection outline from the id buffer
  let id = textureLoad(idTex, px, 0).r;
  var outline = 0.0;
  if (F.select.x != 0u || F.select.y != 0u) {
    let idR = textureLoad(idTex, px + vec2<i32>(2, 0), 0).r; let idL = textureLoad(idTex, px - vec2<i32>(2, 0), 0).r;
    let idU = textureLoad(idTex, px + vec2<i32>(0, 2), 0).r; let idD = textureLoad(idTex, px - vec2<i32>(0, 2), 0).r;
    let sel = F.select.x;
    let onEdge = (id == sel) != (idR == sel) || (id == sel) != (idL == sel) || (id == sel) != (idU == sel) || (id == sel) != (idD == sel);
    if (sel != 0u && onEdge) { outline = 1.0; }
    let hov = F.select.y;
    if (hov != 0u && hov != sel && id == hov) { color = mix(color, vec3<f32>(1.0, 1.0, 1.0), 0.06); }
    if (sel != 0u && id == sel) { color = mix(color, vec3<f32>(1.0, 0.75, 0.35), 0.08); }
  }
  color = mix(color, vec3<f32>(1.0, 0.72, 0.25), outline);
  return vec4<f32>(color, 1.0);
}
`;

// ---------------------------------------------------------------------------------------------
// Post: ACES tonemap + FXAA → swapchain
// ---------------------------------------------------------------------------------------------
export const POST = /* wgsl */ `
${COMMON}
@group(1) @binding(0) var hdr : texture_2d<f32>;
@group(1) @binding(1) var samp : sampler;
struct VSOut { @builtin(position) clip : vec4<f32>, @location(0) uv : vec2<f32> };
@vertex fn vs(@builtin(vertex_index) vi : u32) -> VSOut {
  var pos = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
  var o : VSOut; o.clip = vec4<f32>(pos[vi], 0.0, 1.0); o.uv = pos[vi] * vec2<f32>(0.5, -0.5) + 0.5; return o;
}
fn aces(x : vec3<f32>) -> vec3<f32> {
  let a = 2.51; let b = 0.03; let c = 2.43; let d = 0.59; let e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3<f32>(0.0), vec3<f32>(1.0));
}
fn tone(c : vec3<f32>) -> vec3<f32> { return aces(c * F.params.w); }
fn luma(c : vec3<f32>) -> f32 { return dot(c, vec3<f32>(0.299, 0.587, 0.114)); }
@fragment fn fs(in : VSOut) -> @location(0) vec4<f32> {
  let texel = F.viewport.zw;
  // FXAA 3.11-lite on tonemapped colour
  let rgbM = tone(textureSample(hdr, samp, in.uv).rgb);
  let rgbNW = tone(textureSample(hdr, samp, in.uv + vec2<f32>(-1.0, -1.0) * texel).rgb);
  let rgbNE = tone(textureSample(hdr, samp, in.uv + vec2<f32>(1.0, -1.0) * texel).rgb);
  let rgbSW = tone(textureSample(hdr, samp, in.uv + vec2<f32>(-1.0, 1.0) * texel).rgb);
  let rgbSE = tone(textureSample(hdr, samp, in.uv + vec2<f32>(1.0, 1.0) * texel).rgb);
  let lM = luma(rgbM); let lNW = luma(rgbNW); let lNE = luma(rgbNE); let lSW = luma(rgbSW); let lSE = luma(rgbSE);
  let lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
  let lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
  var dir = vec2<f32>(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));
  let dirReduce = max((lNW + lNE + lSW + lSE) * 0.25 * 0.125, 1.0 / 128.0);
  let rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
  dir = clamp(dir * rcpDirMin, vec2<f32>(-8.0), vec2<f32>(8.0)) * texel;
  let rgbA = 0.5 * (tone(textureSample(hdr, samp, in.uv + dir * (1.0 / 3.0 - 0.5)).rgb) + tone(textureSample(hdr, samp, in.uv + dir * (2.0 / 3.0 - 0.5)).rgb));
  let rgbB = rgbA * 0.5 + 0.25 * (tone(textureSample(hdr, samp, in.uv + dir * -0.5).rgb) + tone(textureSample(hdr, samp, in.uv + dir * 0.5).rgb));
  let lB = luma(rgbB);
  var c = rgbB;
  if (lB < lMin || lB > lMax) { c = rgbA; }
  // subtle vignette + gamma
  let v = 1.0 - 0.18 * pow(length(in.uv - 0.5) * 1.3, 2.5);
  c = pow(c * v, vec3<f32>(1.0 / 2.2));
  return vec4<f32>(c, 1.0);
}
`;

// ---------------------------------------------------------------------------------------------
// Overlay lines (gizmos, spline handles, grid) — drawn after post on the swapchain with depth test option
// ---------------------------------------------------------------------------------------------
export const LINES = /* wgsl */ `
${COMMON}
struct VSOut { @builtin(position) clip : vec4<f32>, @location(0) col : vec4<f32> };
@vertex fn vs(@location(0) pos : vec3<f32>, @location(1) col : vec4<f32>) -> VSOut {
  var o : VSOut; o.clip = F.viewProj * vec4<f32>(pos, 1.0); o.col = col; return o;
}
@fragment fn fs(in : VSOut) -> @location(0) vec4<f32> { return in.col; }
`;

// Mip generation (2D array layer blit)
export const MIP = /* wgsl */ `
@group(0) @binding(0) var src : texture_2d<f32>;
@group(0) @binding(1) var samp : sampler;
struct VSOut { @builtin(position) clip : vec4<f32>, @location(0) uv : vec2<f32> };
@vertex fn vs(@builtin(vertex_index) vi : u32) -> VSOut {
  var pos = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
  var o : VSOut; o.clip = vec4<f32>(pos[vi], 0.0, 1.0); o.uv = pos[vi] * vec2<f32>(0.5, -0.5) + 0.5; return o;
}
@fragment fn fs(in : VSOut) -> @location(0) vec4<f32> { return textureSample(src, samp, in.uv); }
`;

export const ALL = { GBUFFER, SHADOW, DECAL, SSAO, BLUR, LIGHTING, POST, LINES, MIP };

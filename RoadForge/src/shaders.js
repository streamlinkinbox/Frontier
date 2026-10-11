/* RoadForge — WGSL shaders: PBR lighting, procedural road/paving/metal materials, shadows, sky. */
(function (RF) {
  'use strict';

  const COMMON = /* wgsl */`
struct Frame {
  viewProj : mat4x4<f32>,
  view : mat4x4<f32>,
  proj : mat4x4<f32>,
  invViewProj : mat4x4<f32>,
  shadowMat : mat4x4<f32>,
  camPos : vec4<f32>,        // xyz, time
  sunDir : vec4<f32>,        // xyz, sun intensity
  sunColor : vec4<f32>,      // rgb, night factor (0 day .. 1 night)
  skyZenith : vec4<f32>,     // rgb, exposure
  skyHorizon : vec4<f32>,    // rgb, fog density
  groundColor : vec4<f32>,   // rgb, shadow texel size
  misc : vec4<f32>,          // viewport w, h, wetness global, plan mode
  lightCount : vec4<u32>,
};
struct PointLight { pos : vec4<f32>, color : vec4<f32> };
struct Lights { l : array<PointLight, 64> };
struct Material {
  base : vec4<f32>,
  p0 : vec4<f32>,   // roughness, metallic, scale, wear
  p1 : vec4<f32>,   // cracks, patches, wetness, emissive
  p2 : vec4<f32>,   // laneWidth, laneOrigin, pattern, seed
  ids : vec4<u32>,  // matId, flags, texTile, selected
};
@group(0) @binding(0) var<uniform> F : Frame;
@group(0) @binding(1) var<uniform> L : Lights;
@group(0) @binding(2) var shadowTex : texture_depth_2d;
@group(0) @binding(3) var shadowSamp : sampler_comparison;
@group(0) @binding(4) var atlasTex : texture_2d<f32>;
@group(0) @binding(5) var atlasSamp : sampler;
@group(1) @binding(0) var<uniform> M : Material;

struct VSIn {
  @location(0) pos : vec3<f32>,
  @location(1) nrm : vec3<f32>,
  @location(2) uv : vec2<f32>,
  @location(3) aux : vec4<f32>,
  @location(4) i0 : vec4<f32>,
  @location(5) i1 : vec4<f32>,
  @location(6) i2 : vec4<f32>,
  @location(7) i3 : vec4<f32>,
  @location(8) icol : vec4<f32>,
  @location(9) iext : vec4<f32>,
};
struct VSOut {
  @builtin(position) clip : vec4<f32>,
  @location(0) wpos : vec3<f32>,
  @location(1) nrm : vec3<f32>,
  @location(2) uv : vec2<f32>,
  @location(3) aux : vec4<f32>,
  @location(4) icol : vec4<f32>,
  @location(5) iext : vec4<f32>,
  @location(6) spos : vec4<f32>,
};

fn hash12(p : vec2<f32>) -> f32 { var p3 = fract(vec3<f32>(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
fn hash22(p : vec2<f32>) -> vec2<f32> { var p3 = fract(vec3<f32>(p.xyx) * vec3<f32>(0.1031, 0.1030, 0.0973)); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.xx + p3.yz) * p3.zy); }
fn hash13(p : vec3<f32>) -> f32 { var p3 = fract(p * 0.1031); p3 += dot(p3, p3.zyx + 31.32); return fract((p3.x + p3.y) * p3.z); }
fn vnoise(p : vec2<f32>) -> f32 {
  let i = floor(p); let f = fract(p); let u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash12(i), hash12(i + vec2<f32>(1.0, 0.0)), u.x), mix(hash12(i + vec2<f32>(0.0, 1.0)), hash12(i + vec2<f32>(1.0, 1.0)), u.x), u.y);
}
fn fbm(p : vec2<f32>, oct : i32) -> f32 {
  var a = 0.5; var f = 1.0; var s = 0.0; var n = 0.0; var q = p;
  for (var i = 0; i < 6; i++) { if (i >= oct) { break; } s += a * vnoise(q * f); n += a; a *= 0.5; f *= 2.07; q += vec2<f32>(13.7, 7.1); }
  return s / n;
}
// Voronoi: returns (edge distance, cell id hash, dist to centre)
fn voronoi(p : vec2<f32>) -> vec3<f32> {
  let n = floor(p); let f = fract(p);
  var md = 8.0; var mr = vec2<f32>(0.0); var mg = vec2<f32>(0.0); var id = 0.0;
  for (var j = -1; j <= 1; j++) { for (var i = -1; i <= 1; i++) {
    let g = vec2<f32>(f32(i), f32(j)); let o = hash22(n + g); let r = g + o - f; let d = dot(r, r);
    if (d < md) { md = d; mr = r; mg = g; id = hash12(n + g); }
  } }
  var ed = 8.0;
  for (var j = -2; j <= 2; j++) { for (var i = -2; i <= 2; i++) {
    let g = mg + vec2<f32>(f32(i), f32(j)); let o = hash22(n + g); let r = g + o - f;
    if (dot(mr - r, mr - r) > 0.00001) { ed = min(ed, dot(0.5 * (mr + r), normalize(r - mr))); }
  } }
  return vec3<f32>(ed, id, sqrt(md));
}
`;

  const VS = /* wgsl */`
@vertex fn vs_main(in : VSIn) -> VSOut {
  let model = mat4x4<f32>(in.i0, in.i1, in.i2, in.i3);
  let wp = model * vec4<f32>(in.pos, 1.0);
  var o : VSOut;
  o.clip = F.viewProj * wp;
  if ((M.ids.y & 1u) != 0u) { o.clip.z -= 0.00025 * o.clip.w; } // wire / overlay lines: pull towards the camera
  o.wpos = wp.xyz;
  o.nrm = normalize((model * vec4<f32>(in.nrm, 0.0)).xyz);
  o.uv = in.uv; o.aux = in.aux; o.icol = in.icol; o.iext = in.iext;
  o.spos = F.shadowMat * wp;
  return o;
}
@vertex fn vs_shadow(in : VSIn) -> @builtin(position) vec4<f32> {
  let model = mat4x4<f32>(in.i0, in.i1, in.i2, in.i3);
  return F.shadowMat * (model * vec4<f32>(in.pos, 1.0));
}
`;

  const MATERIALS = /* wgsl */`
struct Surf { albedo : vec3<f32>, rough : f32, metal : f32, nrm : vec3<f32>, emissive : vec3<f32>, alpha : f32, wet : f32, ao : f32 };

fn perturb(n : vec3<f32>, p : vec3<f32>, h0 : f32, hx : f32, hz : f32, eps : f32, strength : f32) -> vec3<f32> {
  // height-field bump: gradient in world XZ (surfaces are mostly horizontal) projected on the tangent plane
  let g = vec3<f32>((hx - h0) / eps, 0.0, (hz - h0) / eps) * strength;
  let t = g - n * dot(g, n);
  return normalize(n - t);
}

// ---------- asphalt ----------
fn asphaltHeight(p : vec2<f32>, scale : f32) -> f32 {
  let g = vnoise(p * 55.0 * scale) * 0.6 + vnoise(p * 140.0 * scale) * 0.4;   // aggregate grains
  let m = fbm(p * 1.3, 3);
  return g * 0.7 + m * 0.3;
}
fn asphalt(p : vec3<f32>, n : vec3<f32>, uv : vec2<f32>, aux : vec4<f32>, dist : f32) -> Surf {
  var s : Surf;
  let scale = M.p0.z;
  let wearAmt = M.p0.w; let crackAmt = M.p1.x; let patchAmt = M.p1.y; let wetAmt = max(M.p1.z, F.misc.z);
  let xz = p.xz + vec2<f32>(M.p2.w * 17.0);
  let detail = clamp(1.0 - dist / 90.0, 0.0, 1.0);
  let eps = 0.004 + dist * 0.0004;
  let h0 = asphaltHeight(xz, scale); let hx = asphaltHeight(xz + vec2<f32>(eps, 0.0), scale); let hz = asphaltHeight(xz + vec2<f32>(0.0, eps), scale);
  let grain = h0;
  // base colour: dark bituminous with lighter exposed aggregate
  var col = M.base.rgb * (0.78 + 0.5 * grain);
  let macroN = fbm(xz * 0.07, 4);
  col *= 0.86 + 0.28 * macroN;
  // seams / old patches
  if (patchAmt > 0.001) {
    let c = voronoi(xz * 0.045 + vec2<f32>(3.1));
    let isPatch = step(1.0 - patchAmt * 0.5, c.y);
    let edge = smoothstep(0.0, 0.03, c.x);
    col = mix(col, col * (0.55 + 0.25 * c.y), isPatch * edge);
    col *= 1.0 - (1.0 - edge) * isPatch * 0.35;
  }
  // wheel-path wear (lighter, smoother) based on lateral position (aux.z) relative to lane layout
  var wear = 0.0;
  if (abs(aux.z) < 40.0 && M.p2.x > 0.5) {
    let lw = M.p2.x; let lf = fract((aux.z - M.p2.y) / lw);
    let w1 = exp(-pow((lf - 0.25) / 0.09, 2.0)); let w2 = exp(-pow((lf - 0.75) / 0.09, 2.0));
    wear = (w1 + w2) * wearAmt * (0.6 + 0.4 * fbm(xz * 0.3, 2));
  }
  col = mix(col, col * 1.28 + vec3<f32>(0.02), clamp(wear, 0.0, 1.0) * 0.8);
  // tyre marks (darker streaks) inside the wheel paths
  col *= 1.0 - 0.18 * wear * smoothstep(0.55, 0.8, fbm(xz * vec2<f32>(0.9, 6.0), 2));
  // cracks: voronoi edges (alligator cracking) modulated by a mask
  var crack = 0.0;
  if (crackAmt > 0.001) {
    let mask = smoothstep(1.0 - crackAmt, 1.0 - crackAmt + 0.35, fbm(xz * 0.11 + vec2<f32>(9.0), 3));
    let v = voronoi(xz * 1.4); let v2 = voronoi(xz * 3.1 + vec2<f32>(5.0));
    let line = (1.0 - smoothstep(0.0, 0.03, v.x)) + 0.6 * (1.0 - smoothstep(0.0, 0.025, v2.x)) * step(0.6, mask);
    crack = clamp(line, 0.0, 1.0) * mask;
  }
  col *= 1.0 - crack * 0.75;
  // damage (pothole rim/interior): exposed aggregate, broken edges
  let dmg = aux.x;
  if (dmg > 0.001) {
    let rubble = vnoise(xz * 30.0);
    col = mix(col, vec3<f32>(0.22, 0.2, 0.17) * (0.5 + rubble), smoothstep(0.1, 0.8, dmg));
    col *= 1.0 - 0.3 * smoothstep(0.0, 0.4, dmg) * (1.0 - smoothstep(0.4, 0.9, dmg));
  }
  // construction: milled surface (grooves along the road)
  var bumpStrength = 0.9 * detail;
  if (aux.w > 1.5 && aux.w < 2.5) {
    let groove = 0.5 + 0.5 * sin(uv.y * 160.0);
    col = vec3<f32>(0.42, 0.41, 0.39) * (0.75 + 0.3 * groove) * (0.8 + 0.4 * vnoise(xz * 20.0));
    bumpStrength = 2.5 * detail;
  }
  // wetness / puddles: low spots (noise) + around damage
  var wet = 0.0;
  if (wetAmt > 0.001) {
    let puddle = smoothstep(0.62, 0.72, fbm(xz * 0.21 + vec2<f32>(21.0), 3)) * wetAmt;
    wet = clamp(puddle + dmg * wetAmt * 0.9, 0.0, 1.0);
    col = mix(col, col * 0.35, wet);
  }
  s.albedo = col;
  s.rough = clamp(0.92 - 0.25 * wear - wet * 0.85 + crack * 0.05, 0.05, 1.0);
  s.metal = 0.0;
  var nn = perturb(n, p, h0, hx, hz, eps, bumpStrength * 0.012 * (1.0 - wet));
  if (crack > 0.3) { nn = normalize(nn + vec3<f32>(0.0, -0.1, 0.0) * crack); }
  s.nrm = nn;
  s.emissive = vec3<f32>(0.0); s.alpha = 1.0; s.wet = wet; s.ao = 1.0 - crack * 0.4 - dmg * 0.3;
  return s;
}

// ---------- concrete ----------
fn concrete(p : vec3<f32>, n : vec3<f32>, uv : vec2<f32>, dist : f32, jointLen : f32) -> Surf {
  var s : Surf;
  let xz = p.xz;
  let eps = 0.003 + dist * 0.0004;
  let hf = fbm(xz * 9.0, 3); let hx = fbm((xz + vec2<f32>(eps, 0.0)) * 9.0, 3); let hz = fbm((xz + vec2<f32>(0.0, eps)) * 9.0, 3);
  var col = M.base.rgb * (0.85 + 0.3 * hf) * (0.9 + 0.2 * fbm(xz * 0.4, 3));
  // pores
  let pores = smoothstep(0.78, 0.9, vnoise(xz * 90.0));
  col *= 1.0 - pores * 0.25;
  // joints
  if (jointLen > 0.0) {
    let ju = abs(fract(uv.x / jointLen + 0.5) - 0.5) * jointLen;
    let j = 1.0 - smoothstep(0.004, 0.012, ju);
    col *= 1.0 - j * 0.45;
  }
  // grime at low heights handled by caller via ao
  s.albedo = col; s.rough = 0.8 - 0.1 * hf; s.metal = 0.0;
  s.nrm = perturb(n, p, hf, hx, hz, eps, 0.004 * clamp(1.0 - dist / 60.0, 0.0, 1.0));
  s.emissive = vec3<f32>(0.0); s.alpha = 1.0; s.wet = 0.0; s.ao = 1.0;
  return s;
}

// ---------- paving patterns ----------
// returns (edge distance in metres, stone id, bevel height 0..1)
fn pavingCell(uv : vec2<f32>, pattern : u32, size : f32) -> vec3<f32> {
  var q = uv;
  if (pattern == 0u) { // stretcher bond 200x100
    let w = 0.2 * size; let h = 0.1 * size;
    let row = floor(q.y / h); q.x += select(0.0, w * 0.5, fract(row * 0.5) > 0.25);
    let c = vec2<f32>(floor(q.x / w), row); let f = vec2<f32>(fract(q.x / w) * w, fract(q.y / h) * h);
    let e = min(min(f.x, w - f.x), min(f.y, h - f.y));
    return vec3<f32>(e, hash12(c), 1.0);
  } else if (pattern == 1u) { // herringbone 45°
    let w = 0.2 * size; let h = 0.1 * size;
    let r = mat2x2<f32>(0.7071, 0.7071, -0.7071, 0.7071) * q;
    // classic herringbone in rotated frame: cell (i,j) of size h; brick direction alternates
    let cx = floor(r.x / h); let cy = floor(r.y / h);
    let fx = fract(r.x / h) * h; let fy = fract(r.y / h) * h;
    let diag = fract((cx - cy) / 2.0 + 1.0) * 2.0;   // 0 or 1 pattern parity
    var e = 0.0; var id = 0.0;
    if (diag < 0.5) { // horizontal brick spanning two cells in x
      let bx = floor((r.x) / w); let within = fract(r.x / w) * w; e = min(min(within, w - within), min(fy, h - fy)); id = hash12(vec2<f32>(bx, cy) + 0.37);
    } else {
      let by = floor((r.y) / w); let within = fract(r.y / w) * w; e = min(min(within, w - within), min(fx, h - fx)); id = hash12(vec2<f32>(cx, by) + 0.71);
    }
    return vec3<f32>(e, id, 1.0);
  } else if (pattern == 2u) { // basketweave
    let w = 0.2 * size; let cx = floor(q.x / w); let cy = floor(q.y / w); let f = vec2<f32>(fract(q.x / w), fract(q.y / w)) * w;
    let par = fract((cx + cy) * 0.5) > 0.25;
    var e = 0.0; var id = 0.0;
    if (par) { let sub = floor(f.y / (w * 0.5)); let fy = f.y - sub * w * 0.5; e = min(min(f.x, w - f.x), min(fy, w * 0.5 - fy)); id = hash12(vec2<f32>(cx, cy * 2.0 + sub)); }
    else { let sub = floor(f.x / (w * 0.5)); let fx = f.x - sub * w * 0.5; e = min(min(fx, w * 0.5 - fx), min(f.y, w - f.y)); id = hash12(vec2<f32>(cx * 2.0 + sub, cy) + 0.5); }
    return vec3<f32>(e, id, 1.0);
  } else if (pattern == 3u) { // large slabs 600x600 (offset)
    let w = 0.6 * size; let row = floor(q.y / w); q.x += select(0.0, w * 0.5, fract(row * 0.5) > 0.25);
    let c = vec2<f32>(floor(q.x / w), row); let f = vec2<f32>(fract(q.x / w), fract(q.y / w)) * w;
    return vec3<f32>(min(min(f.x, w - f.x), min(f.y, w - f.y)), hash12(c), 1.0);
  } else if (pattern == 4u) { // hexagonal
    let sz = 0.25 * size; let hx = q / sz;
    let r = vec2<f32>(1.0, 1.7320508); let hh = r * 0.5;
    let a = (fract(hx / r) - 0.5) * r; let b = (fract((hx - hh) / r) - 0.5) * r;
    let ga = dot(a, a) < dot(b, b); let g = select(b, a, ga);
    let idc = select(floor((hx - hh) / r) + 0.5, floor(hx / r), ga);
    let dHex = max(abs(g.x) * 0.866 + abs(g.y) * 0.5, abs(g.y));
    return vec3<f32>((0.5 - dHex) * sz * 0.9, hash12(idc), 1.0);
  } else if (pattern == 5u) { // flagstone (irregular)
    let v = voronoi(q / (0.55 * size)); return vec3<f32>(v.x * 0.55 * size, v.y, 1.0);
  } else if (pattern == 6u) { // cobblestone (domed setts)
    let v = voronoi(q / (0.11 * size)); return vec3<f32>(v.x * 0.11 * size, v.y, 1.0 - v.z * 1.4);
  } else { // 7 tactile blister
    let w = 0.067; let f = fract(q / w) - 0.5; let d = length(f) * w; let dome = 1.0 - smoothstep(0.014, 0.022, d);
    return vec3<f32>(0.05, hash12(floor(q / 0.4)), dome * 2.0);
  }
}
fn paving(p : vec3<f32>, n : vec3<f32>, uv : vec2<f32>, aux : vec4<f32>, dist : f32) -> Surf {
  var s : Surf;
  var pattern = u32(M.p2.z + 0.5);
  if (aux.w > 0.5 && aux.w < 1.5) { pattern = 7u; }
  let size = M.p0.z;
  let detail = clamp(1.0 - dist / 70.0, 0.0, 1.0);
  let eps = 0.006 + dist * 0.0006;
  let c0 = pavingCell(uv, pattern, size); let cx = pavingCell(uv + vec2<f32>(eps, 0.0), pattern, size); let cz = pavingCell(uv + vec2<f32>(0.0, eps), pattern, size);
  let mortarW = select(0.004, 0.012, pattern == 5u || pattern == 6u);
  let bevel = 0.012;
  let hFn = fn_h(c0, mortarW, bevel, pattern); let hX = fn_h(cx, mortarW, bevel, pattern); let hZ = fn_h(cz, mortarW, bevel, pattern);
  let stone = M.base.rgb * (0.82 + 0.36 * c0.y) * (0.92 + 0.16 * fbm(p.xz * 14.0, 2)) * (0.9 + 0.2 * fbm(p.xz * 0.5, 2));
  let mortar = vec3<f32>(0.42, 0.41, 0.38) * (0.85 + 0.3 * vnoise(p.xz * 40.0));
  let isMortar = 1.0 - smoothstep(mortarW * 0.6, mortarW * 1.4, c0.x);
  var col = mix(stone, mortar, isMortar);
  if (pattern == 7u) { col = M.p1.w * vec3<f32>(0.0) + mix(vec3<f32>(0.78, 0.55, 0.28), vec3<f32>(0.85, 0.6, 0.3), c0.z * 0.5); } // tactile buff/yellow
  // grime between stones + wear
  col *= 1.0 - 0.15 * (1.0 - smoothstep(0.0, 0.03, c0.x));
  s.albedo = col; s.rough = mix(0.78, 0.95, isMortar) - 0.1 * c0.y; s.metal = 0.0;
  s.nrm = perturb(n, p, hFn, hX, hZ, eps, 1.0 * detail);
  s.emissive = vec3<f32>(0.0); s.alpha = 1.0; s.wet = 0.0; s.ao = 1.0 - isMortar * 0.3;
  return s;
}
fn fn_h(c : vec3<f32>, mortarW : f32, bevel : f32, pattern : u32) -> f32 {
  var h = smoothstep(mortarW, mortarW + bevel, c.x) * 0.006;   // bevelled stone edge
  if (pattern == 6u) { h += clamp(c.z, 0.0, 1.0) * 0.012; }
  if (pattern == 7u) { h = c.z * 0.004; }
  return h;
}

// ---------- kerb (precast concrete / granite) ----------
fn kerb(p : vec3<f32>, n : vec3<f32>, uv : vec2<f32>, dist : f32) -> Surf {
  var s : Surf;
  let granite = M.p2.z > 0.5;
  let xz = p.xz;
  var col = M.base.rgb;
  if (granite) {
    let sp = vnoise(xz * 220.0); let sp2 = vnoise(xz * 90.0 + 3.0);
    col = M.base.rgb * (0.6 + 0.5 * sp) * (0.8 + 0.4 * sp2);
    col = mix(col, vec3<f32>(0.12, 0.12, 0.13), smoothstep(0.8, 0.9, sp));
  } else {
    col = M.base.rgb * (0.86 + 0.25 * fbm(xz * 12.0, 3));
  }
  // segment joints every 0.915 m along the kerb
  let ju = abs(fract(uv.x / 0.915 + 0.5) - 0.5) * 0.915;
  col *= 1.0 - (1.0 - smoothstep(0.003, 0.009, ju)) * 0.5;
  // grime near the gutter (uv.y small = bottom)
  col *= 0.75 + 0.25 * smoothstep(0.0, 0.05, uv.y);
  s.albedo = col; s.rough = select(0.85, 0.45, granite); s.metal = 0.0; s.nrm = n;
  s.emissive = vec3<f32>(0.0); s.alpha = 1.0; s.wet = 0.0; s.ao = 1.0;
  return s;
}

// ---------- paint ----------
fn paint(p : vec3<f32>, n : vec3<f32>, uv : vec2<f32>, dist : f32) -> Surf {
  var s : Surf;
  let xz = p.xz;
  let wear = M.p0.w;
  let w1 = fbm(xz * 2.3, 3); let w2 = vnoise(xz * 45.0);
  let worn = smoothstep(1.0 - wear * 1.1, 1.0 - wear * 1.1 + 0.25, w1 * 0.75 + w2 * 0.25);
  if (worn > 0.55) { discard; }
  var col = M.base.rgb * (0.86 + 0.2 * vnoise(xz * 60.0));
  col *= 1.0 - worn * 0.5;
  s.albedo = col; s.rough = 0.55 + 0.2 * worn; s.metal = 0.0; s.nrm = n;
  s.emissive = vec3<f32>(0.0); s.alpha = 1.0; s.wet = 0.0; s.ao = 1.0;
  return s;
}

// ---------- grass / ground ----------
fn ground(p : vec3<f32>, n : vec3<f32>, dist : f32, isGrass : bool, fw : vec2<f32>) -> Surf {
  var s : Surf;
  let xz = p.xz;
  let m = fbm(xz * 0.05, 4); let d = fbm(xz * 0.9, 3); let fine = vnoise(xz * 25.0);
  let grass = vec3<f32>(0.19, 0.27, 0.11) * (0.8 + 0.5 * d) * (0.85 + 0.3 * fine);
  let dirt = vec3<f32>(0.33, 0.28, 0.2) * (0.85 + 0.3 * fine);
  let dry = vec3<f32>(0.36, 0.34, 0.18) * (0.8 + 0.4 * d);
  var col = mix(grass, dry, smoothstep(0.45, 0.7, m));
  col = mix(col, dirt, smoothstep(0.6, 0.85, fbm(xz * 0.17 + 40.0, 3)) * 0.8);
  if (!isGrass) { col = mix(col, dirt, 0.5); }
  col = col * M.base.rgb * 2.0;
  if (F.misc.w > 0.5) { // plan-mode grid
    let g1 = abs(fract(xz / 10.0 + 0.5) - 0.5) * 10.0; let g2 = abs(fract(xz / 50.0 + 0.5) - 0.5) * 50.0;
    let l1 = 1.0 - smoothstep(fw, fw * 2.0, g1); let l2 = 1.0 - smoothstep(fw, fw * 2.0, g2);
    col = mix(col, col * 0.5 + 0.2, max(l1.x, l1.y) * 0.25 + max(l2.x, l2.y) * 0.5);
  }
  s.albedo = col; s.rough = 0.95; s.metal = 0.0; s.nrm = n; s.emissive = vec3<f32>(0.0); s.alpha = M.base.a; s.wet = 0.0; s.ao = 1.0;
  return s;
}

// ---------- metals, rubber, glass, car paint, emissive, misc ----------
fn metal(p : vec3<f32>, n : vec3<f32>, uv : vec2<f32>, galv : bool) -> Surf {
  var s : Surf;
  var col = M.base.rgb;
  if (galv) { let sp = voronoi(p.xz * 18.0 + p.y * 7.0); col = M.base.rgb * (0.75 + 0.45 * sp.y); }
  // rust streaks downward
  let rust = smoothstep(0.7, 0.9, fbm(vec2<f32>(p.x + p.z, p.y * 0.3) * 6.0, 3)) * M.p0.w;
  col = mix(col, vec3<f32>(0.35, 0.16, 0.07), rust);
  s.albedo = col; s.rough = mix(M.p0.x, 0.9, rust); s.metal = mix(M.p0.y, 0.1, rust); s.nrm = n;
  s.emissive = vec3<f32>(0.0); s.alpha = 1.0; s.wet = 0.0; s.ao = 1.0;
  return s;
}
fn carpaint(p : vec3<f32>, n : vec3<f32>, tint : vec3<f32>) -> Surf {
  var s : Surf;
  let flake = vnoise(p.xz * 400.0 + p.y * 300.0);
  s.albedo = tint * (0.9 + 0.2 * flake); s.rough = 0.22; s.metal = 0.55; s.nrm = n;
  s.emissive = vec3<f32>(0.0); s.alpha = 1.0; s.wet = 1.0; s.ao = 1.0; // wet flag re-used as clearcoat
  return s;
}
fn woodMat(p : vec3<f32>, n : vec3<f32>, uv : vec2<f32>) -> Surf {
  var s : Surf;
  let g = fbm(vec2<f32>(uv.x * 2.0, uv.y * 30.0), 3);
  s.albedo = M.base.rgb * (0.7 + 0.5 * g); s.rough = 0.85; s.metal = 0.0; s.nrm = n; s.emissive = vec3<f32>(0.0); s.alpha = 1.0; s.wet = 0.0; s.ao = 1.0;
  return s;
}
fn ballast(p : vec3<f32>, n : vec3<f32>, dist : f32) -> Surf {
  var s : Surf;
  let v = voronoi(p.xz * 14.0); let v2 = voronoi(p.xz * 30.0 + 7.0);
  let eps = 0.01; let h0 = (1.0 - v.z) * 0.03 + (1.0 - v2.z) * 0.012;
  let vx = voronoi((p.xz + vec2<f32>(eps, 0.0)) * 14.0); let vz = voronoi((p.xz + vec2<f32>(0.0, eps)) * 14.0);
  s.albedo = M.base.rgb * (0.6 + 0.6 * v.y) * (0.8 + 0.4 * v2.y) * (1.0 - smoothstep(0.0, 0.05, v.x) * 0.0);
  s.albedo *= 0.7 + 0.3 * smoothstep(0.0, 0.08, v.x);
  s.rough = 0.9; s.metal = 0.0;
  s.nrm = perturb(n, p, h0, (1.0 - vx.z) * 0.03, (1.0 - vz.z) * 0.03, eps, clamp(1.0 - dist / 50.0, 0.0, 1.0));
  s.emissive = vec3<f32>(0.0); s.alpha = 1.0; s.wet = 0.0; s.ao = 1.0;
  return s;
}
fn brick(p : vec3<f32>, n : vec3<f32>, uv : vec2<f32>) -> Surf {
  var s : Surf;
  let w = 0.225; let h = 0.075; var q = uv;
  let row = floor(q.y / h); q.x += select(0.0, w * 0.5, fract(row * 0.5) > 0.25);
  let f = vec2<f32>(fract(q.x / w) * w, fract(q.y / h) * h); let id = hash12(vec2<f32>(floor(q.x / w), row));
  let e = min(min(f.x, w - f.x), min(f.y, h - f.y));
  let mortar = 1.0 - smoothstep(0.004, 0.01, e);
  var col = mix(M.base.rgb * (0.7 + 0.6 * id) * (0.9 + 0.2 * vnoise(p.xz * 60.0 + p.y * 60.0)), vec3<f32>(0.6, 0.58, 0.54), mortar);
  col *= 0.7 + 0.3 * smoothstep(0.0, 0.6, p.y);
  s.albedo = col; s.rough = 0.9; s.metal = 0.0; s.nrm = n; s.emissive = vec3<f32>(0.0); s.alpha = 1.0; s.wet = 0.0; s.ao = 1.0 - mortar * 0.3;
  return s;
}
fn billboard(c : vec4<f32>, n : vec3<f32>) -> Surf {
  var s : Surf;
  s.albedo = c.rgb; s.rough = 0.5; s.metal = 0.0; s.nrm = n; s.emissive = c.rgb * M.p1.w * F.sunColor.w; s.alpha = 1.0; s.wet = 0.0; s.ao = 1.0;
  return s;
}
`;

  const FS = /* wgsl */`
fn shadowFactor(sp : vec4<f32>, ndl : f32) -> f32 {
  var p = sp.xyz / sp.w;
  let uv = vec2<f32>(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || p.z > 1.0 || p.z < 0.0) { return 1.0; }
  let bias = 0.0008 + 0.0025 * (1.0 - ndl);
  let texel = F.groundColor.w;
  var sum = 0.0;
  for (var j = -2; j <= 2; j++) { for (var i = -2; i <= 2; i++) {
    sum += textureSampleCompareLevel(shadowTex, shadowSamp, uv + vec2<f32>(f32(i), f32(j)) * texel, p.z - bias);
  } }
  return sum / 25.0;
}
fn D_GGX(nh : f32, a : f32) -> f32 { let a2 = a * a; let d = nh * nh * (a2 - 1.0) + 1.0; return a2 / (3.14159265 * d * d + 1e-6); }
fn G_Smith(nv : f32, nl : f32, a : f32) -> f32 { let k = (a + 1.0) * (a + 1.0) / 8.0; return (nv / (nv * (1.0 - k) + k)) * (nl / (nl * (1.0 - k) + k)); }
fn F_Schlick(vh : f32, f0 : vec3<f32>) -> vec3<f32> { return f0 + (1.0 - f0) * pow(1.0 - vh, 5.0); }
fn envBRDF(nv : f32, rough : f32, f0 : vec3<f32>) -> vec3<f32> {
  let c0 = vec4<f32>(-1.0, -0.0275, -0.572, 0.022); let c1 = vec4<f32>(1.0, 0.0425, 1.04, -0.04);
  let r = rough * c0 + c1; let a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
  let ab = vec2<f32>(-1.04, 1.04) * a004 + r.zw;
  return f0 * ab.x + ab.y;
}
fn skyEnv(dir : vec3<f32>) -> vec3<f32> {
  let up = clamp(dir.y, -1.0, 1.0);
  var c = mix(F.skyHorizon.rgb, F.skyZenith.rgb, pow(max(up, 0.0), 0.45));
  let below = smoothstep(0.0, -0.15, up);
  c = mix(c, F.groundColor.rgb * 0.8, below);
  let sd = max(dot(dir, F.sunDir.xyz), 0.0);
  c += F.sunColor.rgb * pow(sd, 8.0) * 0.08 * F.sunDir.w * (1.0 - below);
  return c;
}
fn skyColor(dir : vec3<f32>) -> vec3<f32> {
  var c = skyEnv(dir);
  let sd = max(dot(dir, F.sunDir.xyz), 0.0);
  let below = smoothstep(0.0, -0.15, dir.y);
  c += F.sunColor.rgb * (pow(sd, 2500.0) * 60.0 + pow(sd, 16.0) * 0.1) * F.sunDir.w * (1.0 - below);
  return c;
}
fn aces(x : vec3<f32>) -> vec3<f32> { let a = 2.51; let b = 0.03; let c = 2.43; let d = 0.59; let e = 0.14; return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3<f32>(0.0), vec3<f32>(1.0)); }

fn shade(in : VSOut, s : Surf) -> vec3<f32> {
  let V = normalize(F.camPos.xyz - in.wpos);
  var N = s.nrm;
  if (dot(N, V) < 0.0) { N = -N; }
  let f0 = mix(vec3<f32>(0.04), s.albedo, s.metal);
  let diffuse = s.albedo * (1.0 - s.metal);
  let rough = clamp(s.rough, 0.04, 1.0); let a = rough * rough;
  let nv = max(dot(N, V), 1e-4);
  var col = vec3<f32>(0.0);
  // sun
  let Ld = F.sunDir.xyz; let nl = max(dot(N, Ld), 0.0);
  if (nl > 0.0 && F.sunDir.w > 0.0) {
    let H = normalize(V + Ld); let nh = max(dot(N, H), 0.0); let vh = max(dot(V, H), 0.0);
    let Fs = F_Schlick(vh, f0); let spec = D_GGX(nh, a) * G_Smith(nv, nl, rough) * Fs / (4.0 * nv * nl + 1e-4);
    let sh = shadowFactor(in.spos, nl);
    col += (diffuse / 3.14159265 * (1.0 - Fs) + spec) * F.sunColor.rgb * F.sunDir.w * nl * sh;
  }
  // sky / ground ambient (hemisphere)
  let hemi = N.y * 0.5 + 0.5;
  let amb = mix(F.groundColor.rgb * 0.35, F.skyZenith.rgb * 0.55 + F.skyHorizon.rgb * 0.2, hemi);
  col += diffuse * amb * s.ao;
  // environment specular
  let R = reflect(-V, N);
  var env = skyEnv(R);
  env = mix(env, (F.skyZenith.rgb + F.skyHorizon.rgb) * 0.45, rough * 0.85);
  col += env * envBRDF(nv, rough, f0) * s.ao * (0.4 + 0.6 * smoothstep(-0.2, 0.3, R.y));
  // clearcoat lobe for car paint
  if (s.wet > 0.5 && s.metal > 0.3) { col += skyEnv(R) * F_Schlick(nv, vec3<f32>(0.04)) * 0.6; }
  // point lights (street lighting at night)
  let cnt = F.lightCount.x;
  for (var i = 0u; i < 64u; i++) {
    if (i >= cnt) { break; }
    let pl = L.l[i]; let d = pl.pos.xyz - in.wpos; let dist2 = dot(d, d); let rad = pl.pos.w;
    if (dist2 > rad * rad) { continue; }
    let dist = sqrt(dist2); let Lp = d / dist; let nlp = max(dot(N, Lp), 0.0);
    let win = pow(clamp(1.0 - pow(dist / rad, 4.0), 0.0, 1.0), 2.0);
    let att = win / (dist2 + 1.0);
    // downward-facing luminaire: cosine distribution
    let dir = pl.color.w; let cone = select(1.0, max(-Lp.y, 0.0) * 1.3 + 0.1, dir > 0.5);
    let H = normalize(V + Lp); let nh = max(dot(N, H), 0.0);
    let spec = D_GGX(nh, a) * G_Smith(nv, nlp, rough) * F_Schlick(max(dot(V, H), 0.0), f0) / (4.0 * nv * nlp + 1e-4);
    col += (diffuse / 3.14159265 + spec) * pl.color.rgb * att * nlp * cone;
  }
  col += s.emissive;
  // fog
  let dist = length(F.camPos.xyz - in.wpos);
  let fogF = 1.0 - exp(-dist * F.skyHorizon.w);
  var fd = normalize(in.wpos - F.camPos.xyz); fd.y = max(fd.y, 0.0); // fog tends to the horizon colour, never the below-horizon ground tint
  col = mix(col, skyColor(normalize(fd)) * 0.9, fogF);
  return col;
}

@fragment fn fs_main(in : VSOut) -> @location(0) vec4<f32> {
  let n = normalize(in.nrm);
  let dist = length(F.camPos.xyz - in.wpos);
  let id = M.ids.x;
  // derivative-dependent work happens here, in uniform control flow
  let fw = fwidth(in.wpos.xz) * 1.2;
  let tile = select(f32(M.ids.z), in.iext.y, in.iext.z > 0.5); let tiles = 4.0;
  let tuv = vec2<f32>((fract(in.uv.x) + (tile % tiles)) / tiles, 1.0 - (fract(in.uv.y) + floor(tile / tiles)) / tiles);
  let atlasCol = textureSample(atlasTex, atlasSamp, tuv);
  var s : Surf;
  switch (id) {
    case 0u: { s = asphalt(in.wpos, n, in.uv, in.aux, dist); }
    case 1u: { s = concrete(in.wpos, n, in.uv, dist, 4.5); }
    case 2u: { s = paving(in.wpos, n, in.uv, in.aux, dist); }
    case 3u: { s = kerb(in.wpos, n, in.uv, dist); }
    case 4u: { s = paint(in.wpos, n, in.uv, dist); }
    case 5u: { s = ground(in.wpos, n, dist, true, fw); }
    case 6u: { s = ground(in.wpos, n, dist, false, fw); }
    case 7u: { s = metal(in.wpos, n, in.uv, true); }
    case 8u: { s = metal(in.wpos, n, in.uv, false); }
    case 9u: { s = carpaint(in.wpos, n, select(M.base.rgb, in.icol.rgb, in.icol.a > 0.5)); }
    case 10u: { // glass
      s.albedo = vec3<f32>(0.02, 0.03, 0.04); s.rough = 0.08; s.metal = 0.9; s.nrm = n; s.emissive = vec3<f32>(0.0); s.alpha = 1.0; s.wet = 0.0; s.ao = 1.0; }
    case 11u: { // emissive (lamps, signal lenses)
      let tint = select(M.base.rgb, in.icol.rgb, in.icol.a > 0.5);
      let on = select(1.0, in.iext.x, in.iext.w > 0.5);
      s.albedo = tint * 0.3; s.rough = 0.4; s.metal = 0.0; s.nrm = n; s.emissive = tint * M.p1.w * max(F.sunColor.w, M.p1.z) * on; s.alpha = 1.0; s.wet = 0.0; s.ao = 1.0; }
    case 12u: { s = woodMat(in.wpos, n, in.uv); }
    case 13u: { s = ballast(in.wpos, n, dist); }
    case 14u: { s = brick(in.wpos, n, in.uv); }
    case 15u: { s = concrete(in.wpos, n, in.uv, dist, 0.0); s.albedo *= 0.75 + 0.25 * smoothstep(0.0, 0.3, in.uv.x); } // barrier, grime at base
    case 16u: { s = billboard(atlasCol, n); }
    case 17u: { // rubber / tyre
      s.albedo = vec3<f32>(0.03, 0.03, 0.03) * (0.8 + 0.4 * vnoise(in.uv * 60.0)); s.rough = 0.8; s.metal = 0.0; s.nrm = n; s.emissive = vec3<f32>(0.0); s.alpha = 1.0; s.wet = 0.0; s.ao = 1.0; }
    case 18u: { // rail steel: polished running surface on top
      let top = smoothstep(0.6, 0.95, n.y);
      s.albedo = mix(vec3<f32>(0.33, 0.2, 0.12), vec3<f32>(0.75, 0.75, 0.74), top); s.rough = mix(0.8, 0.25, top); s.metal = mix(0.3, 1.0, top); s.nrm = n; s.emissive = vec3<f32>(0.0); s.alpha = 1.0; s.wet = 0.0; s.ao = 1.0; }
    case 19u: { // plastic / cones (orange with reflective band via uv.y)
      let band = smoothstep(0.45, 0.47, in.uv.y) * (1.0 - smoothstep(0.62, 0.64, in.uv.y));
      s.albedo = mix(M.base.rgb, vec3<f32>(0.9), band); s.rough = mix(0.5, 0.3, band); s.metal = 0.0; s.nrm = n; s.emissive = vec3<f32>(0.0); s.alpha = 1.0; s.wet = 0.0; s.ao = 1.0; }
    case 20u: { // flat unlit colour (gizmos / overlays)
      let tint = select(M.base.rgb, in.icol.rgb, in.icol.a > 0.5);
      return vec4<f32>(pow(tint, vec3<f32>(1.0 / 2.2)), M.base.a); }
    case 21u: { s = concrete(in.wpos, n, in.uv, dist, 4.5); s.albedo *= 1.1; } // concrete road (jointed)
    default: { s = asphalt(in.wpos, n, in.uv, in.aux, dist); }
  }
  if (M.ids.w == 1u) { s.emissive += vec3<f32>(0.25, 0.5, 0.9) * 0.25; s.albedo = mix(s.albedo, vec3<f32>(0.3, 0.6, 1.0), 0.25); }
  var col = shade(in, s);
  col = aces(col * F.skyZenith.w);
  col = pow(col, vec3<f32>(1.0 / 2.2));
  return vec4<f32>(col, s.alpha);
}
@fragment fn fs_shadow() {}
`;

  const SKY = /* wgsl */`
struct SkyOut { @builtin(position) pos : vec4<f32>, @location(0) ndc : vec2<f32> };
@vertex fn vs_sky(@builtin(vertex_index) vi : u32) -> SkyOut {
  var p = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
  var o : SkyOut; o.pos = vec4<f32>(p[vi], 1.0, 1.0); o.ndc = p[vi]; return o;
}
@fragment fn fs_sky(in : SkyOut) -> @location(0) vec4<f32> {
  let far = F.invViewProj * vec4<f32>(in.ndc, 1.0, 1.0); let near = F.invViewProj * vec4<f32>(in.ndc, 0.0, 1.0);
  let dir = normalize(far.xyz / far.w - near.xyz / near.w);
  var c = skyColor(dir);
  // stars at night
  if (F.sunColor.w > 0.3 && dir.y > 0.0) { let st = hash12(floor(dir.xz / max(dir.y, 0.05) * 400.0)); c += vec3<f32>(step(0.997, st) * 0.6 * F.sunColor.w); }
  c = aces(c * F.skyZenith.w); c = pow(c, vec3<f32>(1.0 / 2.2));
  return vec4<f32>(c, 1.0);
}
`;

  const SURF_DECL = MATERIALS.split('\n').find((l) => l.startsWith('struct Surf')) + '\n';
  RF.shaders = { main: COMMON + VS + MATERIALS + FS, sky: COMMON + SURF_DECL + FS.split('@fragment fn fs_main')[0] + SKY };
})(globalThis.RF || (globalThis.RF = {}));

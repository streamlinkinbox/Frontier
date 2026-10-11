// WGSL sources. Kept as strings so the app ships as plain static files (no bundler).

// ---------------------------------------------------------------------------------------------
// Layer compositor: evaluates the procedural layer stack per texel into albedo + surface maps.
// All noise is periodic (cell counts are whole numbers per tile) so textures tile seamlessly.
// ---------------------------------------------------------------------------------------------
export const LAYER_WGSL = /* wgsl */`
struct Layer {
  info: vec4f,   // type, blend, opacity, enabled
  p0: vec4f,     // scale, seed, density, amount
  p1: vec4f,     // colA.rgb, height
  p2: vec4f,     // colB.rgb, rough
  p3: vec4f,     // a, b, 0, 0
};
struct Params {
  info: vec4f,   // count, tile metres, 0, 0
  layers: array<Layer, 16>,
};
@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(1) var outAlbedo: texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(2) var outSurface: texture_storage_2d<rgba8unorm, write>;

struct LO { col: vec3f, mask: f32, h: f32, r: f32 };

fn ihash(x: u32, y: u32, s: u32) -> u32 {
  var h = x * 374761393u + y * 668265263u + s * 2246822519u;
  h = (h ^ (h >> 13u)) * 1274126177u;
  return h ^ (h >> 16u);
}
fn rnd(x: i32, y: i32, per: i32, seed: u32) -> f32 {
  let wx = u32(((x % per) + per) % per);
  let wy = u32(((y % per) + per) % per);
  return f32(ihash(wx, wy, seed) & 0xffffffu) / 16777216.0;
}
fn vnoise(p: vec2f, per: i32, seed: u32) -> f32 {
  let i = vec2i(floor(p));
  let f = fract(p);
  let u = f * f * (3.0 - 2.0 * f);
  let a = rnd(i.x, i.y, per, seed);
  let b = rnd(i.x + 1, i.y, per, seed);
  let c = rnd(i.x, i.y + 1, per, seed);
  let d = rnd(i.x + 1, i.y + 1, per, seed);
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
fn fbm(p: vec2f, per: i32, seed: u32) -> f32 {
  var s = 0.0;
  var a = 0.5;
  var q = p;
  var pp = per;
  for (var o = 0u; o < 5u; o++) {
    s += a * vnoise(q, pp, seed + o * 17u);
    q = q * 2.0;
    pp = pp * 2;
    a = a * 0.5;
  }
  return s / 0.9375;
}
fn voronoi(p: vec2f, per: i32, seed: u32) -> vec2f {
  let i = vec2i(floor(p));
  let f = fract(p);
  var f1 = 8.0;
  var f2 = 8.0;
  for (var y = -1; y <= 1; y++) {
    for (var x = -1; x <= 1; x++) {
      let c = i + vec2i(x, y);
      let jx = rnd(c.x, c.y, per, seed);
      let jy = rnd(c.x, c.y, per, seed + 77u);
      let o = vec2f(f32(x) + jx, f32(y) + jy);
      let d = length(o - f);
      if (d < f1) { f2 = f1; f1 = d; } else if (d < f2) { f2 = d; }
    }
  }
  return vec2f(f1, f2);
}

fn evalLayer(L: Layer, uv: vec2f, tile: f32, idx: u32) -> LO {
  var o = LO(vec3f(0.0), 0.0, 0.0, 0.5);
  let typ = i32(L.info.x + 0.5);
  let scale = max(L.p0.x, 0.02);
  let cells = max(1, i32(round(tile / scale)));
  let pc = uv * f32(cells);
  let seed = u32(L.p0.y) * 131u + idx * 7919u + 1u;
  let dens = L.p0.z;
  let amt = L.p0.w;
  let colA = L.p1.xyz;
  let colB = L.p2.xyz;
  let height = L.p1.w;
  let rough = L.p2.w;
  let a = L.p3.x;
  let b = L.p3.y;
  switch typ {
    case 0: { // base
      let n = fbm(pc, cells, seed);
      let t = clamp(0.5 + (n - 0.5) * (0.6 + amt * 2.0), 0.0, 1.0);
      o.col = mix(colB, colA, t);
      o.mask = 1.0;
      o.h = (n - 0.5) * height * 0.8;
      o.r = rough;
    }
    case 1: { // aggregate grain
      let fine = 8;
      let ci = pc * f32(fine);
      let per = cells * fine;
      let hv = rnd(i32(floor(ci.x)), i32(floor(ci.y)), per, seed);
      let presence = smoothstep(1.0 - dens - 0.02, 1.0 - dens + 0.02, hv);
      o.col = mix(colA, colB, fract(hv * 7.31));
      o.mask = presence * clamp(amt * 1.6, 0.0, 1.0);
      o.h = (hv - 0.5) * height;
      o.r = 0.85;
    }
    case 2: { // cracks: voronoi borders, thinned by a noise threshold
      let wob = (vnoise(pc * 2.0, cells * 2, seed + 3u) - 0.5) * a * 0.6;
      let v = voronoi(pc + vec2f(wob), cells, seed);
      let edge = v.y - v.x;
      let w = 0.012 + b * 0.05;
      let presence = smoothstep(1.0 - dens - 0.15, 1.0 - dens + 0.15, vnoise(pc * 1.3, cells, seed + 9u));
      let line = 1.0 - smoothstep(w * 0.5, w, edge);
      o.col = colA;
      o.mask = line * presence * clamp(amt * 1.5, 0.0, 1.0);
      o.h = height * o.mask;
      o.r = 0.95;
    }
    case 3: { // potholes: round depressions in a jittered grid
      var m = 0.0;
      let base = vec2i(floor(pc));
      for (var y = -1; y <= 1; y++) {
        for (var x = -1; x <= 1; x++) {
          let c = base + vec2i(x, y);
          let pres = rnd(c.x, c.y, cells, seed);
          if (pres < dens) {
            let cen = vec2f(f32(c.x), f32(c.y)) + vec2f(0.3 + 0.4 * rnd(c.x, c.y, cells, seed + 5u), 0.3 + 0.4 * rnd(c.x, c.y, cells, seed + 9u));
            let rad = (0.12 + a * 0.38) * (0.75 + 0.5 * rnd(c.x, c.y, cells, seed + 13u));
            let d = length(pc - cen) / rad;
            let dn = d * (1.0 + (vnoise(pc * 3.0, cells * 3, seed + 21u) - 0.5) * 0.35);
            let mm = 1.0 - smoothstep(0.7, 1.0, dn);
            m = max(m, mm);
            o.h = min(o.h, height * (1.0 - smoothstep(0.0, 1.0, dn)) * mm);
          }
        }
      }
      o.col = vec3f(0.035, 0.035, 0.04);
      o.mask = m * clamp(amt * 1.4, 0.0, 1.0);
      o.r = rough;
    }
    case 4: { // repair patches: fuzzy rectangles of different tone
      let base = vec2i(floor(pc));
      var m = 0.0;
      var tone = 1.0;
      for (var y = -1; y <= 1; y++) {
        for (var x = -1; x <= 1; x++) {
          let c = base + vec2i(x, y);
          if (rnd(c.x, c.y, cells, seed) < dens) {
            let loc = pc - (vec2f(f32(c.x), f32(c.y)) + 0.5);
            let half = 0.22 + 0.16 * rnd(c.x, c.y, cells, seed + 3u);
            let fuzz = (vnoise(pc * 4.0, cells * 4, seed + 31u) - 0.5) * a * 0.25;
            let dd = max(abs(loc.x), abs(loc.y)) + fuzz;
            let mm = 1.0 - smoothstep(half - 0.03, half + 0.03 + a * 0.08, dd);
            if (mm > m) { tone = 0.92 + 0.16 * rnd(c.x, c.y, cells, seed + 7u); }
            m = max(m, mm);
          }
        }
      }
      o.col = colA * tone;
      o.mask = m * clamp(amt * 1.5, 0.0, 1.0);
      o.r = rough;
    }
    case 5: { // oil & stains
      let n = fbm(pc * 0.7 + vec2f(3.1, 1.7), cells, seed);
      let m = smoothstep(1.0 - dens - 0.08, 1.0 - dens + 0.12, n);
      o.col = colA;
      o.mask = m * clamp(amt * 1.6, 0.0, 1.0);
      o.r = rough;
    }
    case 6: { // traffic wear: two wheel tracks per tile, broken up by noise
      let k = max(a, 0.05) * 4.0 + 1.0;
      let track = 0.5 + 0.5 * cos(6.2831853 * (pc.x / f32(cells)) * k);
      let n = fbm(pc * 1.5, cells, seed);
      let m = smoothstep(0.35, 0.95, track * 0.7 + n * 0.5);
      o.col = colA;
      o.mask = m * clamp(amt * 1.4, 0.0, 1.0);
      o.r = rough;
    }
    case 7: { // puddles
      let n = fbm(pc * 0.5, cells, seed);
      let m = smoothstep(1.0 - dens - 0.06, 1.0 - dens + 0.06, n);
      o.col = vec3f(0.02, 0.03, 0.035);
      o.mask = m * clamp(amt * 2.0, 0.0, 1.0);
      o.r = 0.06;
    }
    case 8: { // interlocking pavers, running bond
      var q = pc;
      let row = i32(floor(q.y));
      if (((row % 2) + 2) % 2 == 1) { q.x = q.x + 0.5; }
      let cid = vec2i(floor(q));
      let loc = fract(q);
      let jw = a * 0.5;
      let inside = step(jw, loc.x) * step(jw, loc.y) * step(loc.x, 1.0 - jw) * step(loc.y, 1.0 - jw);
      let t = rnd(cid.x, cid.y, cells * 2, seed);
      let paver = mix(colA, colB, t) * (1.0 + (t - 0.5) * amt);
      o.col = mix(paver * 0.55, paver, inside);
      o.mask = 1.0;
      o.h = height * (1.0 - inside) + (t - 0.5) * 0.05 * height * inside;
      o.r = 0.7;
    }
    case 9: { // setts / cobbles (voronoi stones)
      let v = voronoi(pc, cells, seed);
      let joint = smoothstep(a * 0.5, a * 0.5 + 0.06, v.y - v.x);
      let t = rnd(i32(floor(pc.x)), i32(floor(pc.y)), cells, seed + 2u);
      let stone = mix(colA, colB, t) * (1.0 + (t - 0.5) * amt);
      o.col = mix(stone * 0.5, stone, joint);
      o.mask = 1.0;
      o.h = height * (1.0 - joint) * 0.6;
      o.r = 0.75;
    }
    case 10: { // slab joints (concrete)
      let loc = fract(pc);
      let edge = min(min(loc.x, 1.0 - loc.x), min(loc.y, 1.0 - loc.y));
      let jw = max(a, 0.005) * 0.5;
      let joint = 1.0 - smoothstep(jw, jw + 0.015, edge);
      o.col = colA;
      o.mask = joint * clamp(amt * 1.4, 0.0, 1.0);
      o.h = height * joint;
      o.r = 0.9;
    }
    case 11: { // gravel & spall
      let fine = 4;
      let ci = pc * f32(fine);
      let per = cells * fine;
      let cid = vec2i(floor(ci));
      let hv = rnd(cid.x, cid.y, per, seed);
      let loc = fract(ci) - 0.5;
      let stone = 1.0 - smoothstep(0.3, 0.42, length(loc) + (hv - 0.5) * 0.1);
      let present = step(1.0 - dens, hv);
      o.col = mix(colA, colB, fract(hv * 5.13));
      o.mask = stone * present * clamp(amt * 1.6, 0.0, 1.0);
      o.h = height * 0.5 * o.mask;
      o.r = 0.8;
    }
    case 12: { // grime
      let n = fbm(pc * 0.5 + vec2f(7.0, 2.0), cells, seed);
      let m = smoothstep(1.0 - dens - 0.2, 1.0 - dens + 0.3, n);
      o.col = colA;
      o.mask = m * clamp(amt * 1.3, 0.0, 1.0);
      o.r = rough;
    }
    default: {}
  }
  return o;
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let dims = textureDimensions(outAlbedo);
  if (gid.x >= dims.x || gid.y >= dims.y) { return; }
  let uv = (vec2f(gid.xy) + vec2f(0.5)) / vec2f(dims);
  let tile = P.info.y;
  var acc = vec3f(0.5);
  var hgt = 0.0;
  var rough = 0.8;
  let count = i32(P.info.x);
  for (var i = 0; i < count; i++) {
    let L = P.layers[i];
    if (L.info.w < 0.5) { continue; }
    let lo = evalLayer(L, uv, tile, u32(i));
    let m = clamp(lo.mask * L.info.z, 0.0, 1.0);
    let blend = i32(L.info.y + 0.5);
    if (blend == 1) {
      acc = mix(acc, acc * lo.col, m);
    } else {
      acc = mix(acc, lo.col, m);
    }
    hgt = hgt + lo.h * m;
    rough = mix(rough, lo.r, m);
  }
  textureStore(outAlbedo, vec2i(gid.xy), vec4f(clamp(acc, vec3f(0.0), vec3f(1.0)), 1.0));
  textureStore(outSurface, vec2i(gid.xy), vec4f(clamp(0.5 + hgt, 0.0, 1.0), clamp(rough, 0.0, 1.0), 1.0, 1.0));
}
`;

// Box-filter mip generation between two single-level views.
export const MIP_WGSL = /* wgsl */`
@group(0) @binding(0) var src: texture_2d<f32>;
@group(0) @binding(1) var dst: texture_storage_2d<rgba8unorm, write>;
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let d = textureDimensions(dst);
  if (gid.x >= d.x || gid.y >= d.y) { return; }
  let b = vec2i(gid.xy) * 2;
  let c = (textureLoad(src, b, 0) + textureLoad(src, b + vec2i(1, 0), 0) + textureLoad(src, b + vec2i(0, 1), 0) + textureLoad(src, b + vec2i(1, 1), 0)) * 0.25;
  textureStore(dst, vec2i(gid.xy), c);
}
`;

// ---------------------------------------------------------------------------------------------
// Main lit scene. Vertex: pos(3) nrm(3) uv(2, unused) col(3) mat(1), stride 48 bytes.
// ---------------------------------------------------------------------------------------------
export const SCENE_WGSL = /* wgsl */`
struct Frame {
  viewProj: mat4x4f,
  lightVP: mat4x4f,
  camPos: vec4f,       // xyz, time
  sunDir: vec4f,       // xyz towards sun
  sunColor: vec4f,
  skyZenith: vec4f,
  skyHorizon: vec4f,
  groundColor: vec4f,
  fog: vec4f,          // density, 0, 0, 0
  surf: vec4f,         // wetness, tile asphalt, tile concrete, tile pavers
  misc: vec4f,         // shadow size, exposure, width, height
  invViewProj: mat4x4f,
};
@group(0) @binding(0) var<uniform> frame: Frame;
@group(0) @binding(1) var albedoTex: texture_2d_array<f32>;
@group(0) @binding(2) var surfaceTex: texture_2d_array<f32>;
@group(0) @binding(3) var texSamp: sampler;
@group(0) @binding(4) var shadowTex: texture_depth_2d;
@group(0) @binding(5) var shadowSamp: sampler_comparison;

struct VIn {
  @location(0) pos: vec3f,
  @location(1) nrm: vec3f,
  @location(2) uv: vec2f,
  @location(3) col: vec3f,
  @location(4) mat: f32,
};
struct VOut {
  @builtin(position) clip: vec4f,
  @location(0) wpos: vec3f,
  @location(1) nrm: vec3f,
  @location(2) col: vec3f,
  @location(3) @interpolate(flat) mat: f32,
};

@vertex fn vs(v: VIn) -> VOut {
  var o: VOut;
  o.clip = frame.viewProj * vec4f(v.pos, 1.0);
  o.wpos = v.pos;
  o.nrm = v.nrm;
  o.col = v.col;
  o.mat = v.mat;
  return o;
}

fn hash2(p: vec2f) -> f32 {
  return fract(sin(dot(p, vec2f(127.1, 311.7))) * 43758.5453);
}
fn vn(p: vec2f) -> f32 {
  let i = floor(p);
  let f = fract(p);
  let u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash2(i), hash2(i + vec2f(1.0, 0.0)), u.x), mix(hash2(i + vec2f(0.0, 1.0)), hash2(i + vec2f(1.0, 1.0)), u.x), u.y);
}
fn fbm2(p: vec2f) -> f32 {
  var s = 0.0;
  var a = 0.5;
  var q = p;
  for (var i = 0; i < 5; i++) {
    s += a * vn(q);
    q = q * 2.03 + vec2f(1.7, 9.2);
    a *= 0.5;
  }
  return s / 0.97;
}

fn shadowAt(wpos: vec3f, N: vec3f) -> f32 {
  let p = wpos + N * 0.06;
  let lp = frame.lightVP * vec4f(p, 1.0);
  let q = lp.xyz / lp.w;
  let uv = vec2f(q.x * 0.5 + 0.5, -q.y * 0.5 + 0.5);
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || q.z > 1.0) { return 1.0; }
  let texel = 1.0 / frame.misc.x;
  let ref_ = q.z - 0.0015;
  var s = 0.0;
  for (var y = -1; y <= 1; y++) {
    for (var x = -1; x <= 1; x++) {
      s += textureSampleCompareLevel(shadowTex, shadowSamp, uv + vec2f(f32(x), f32(y)) * texel * 1.5, ref_);
    }
  }
  return s / 9.0;
}

fn aces(x: vec3f) -> vec3f {
  return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), vec3f(0.0), vec3f(1.0));
}

@fragment fn fs(i: VOut) -> @location(0) vec4f {
  let camPos = frame.camPos.xyz;
  var V = normalize(camPos - i.wpos);
  var N = normalize(i.nrm);
  if (dot(N, V) < 0.0) { N = -N; }
  let m = i.mat;
  let dux = dpdx(i.wpos.xz);
  let duy = dpdy(i.wpos.xz);

  // Emissive lamps
  if (m < -1.5 && m > -2.5) {
    let c = i.col * 3.2;
    return vec4f(aces(c) * 1.0, 1.0);
  }
  // Unlit overlays (gizmos) are drawn by a separate pipeline; guard anyway.
  if (m < -3.5) { return vec4f(i.col, 1.0); }

  // Inputs are sRGB-authored colours; shade in linear light.
  var albedo = pow(max(i.col, vec3f(0.0)), vec3f(2.2));
  var rough = 0.8;
  var bumpPerturb = false;
  var wet = frame.surf.x;

  if (m < -2.5) {
    // Procedural ground (grass / soil), no textures needed
    let p = i.wpos.xz * 0.06;
    let n = fbm2(p);
    let n2 = fbm2(i.wpos.xz * 0.6);
    let base = mix(vec3f(0.19, 0.25, 0.12), vec3f(0.30, 0.33, 0.18), n);
    albedo = pow(mix(base, vec3f(0.22, 0.2, 0.15), smoothstep(0.62, 0.8, n2) * 0.6), vec3f(2.2));
    rough = 0.95;
    wet = wet * 0.5;
  }
  if (m >= -0.5 && m < 2.5) {
    let layer = i32(m + 0.5);
    var tile = frame.surf.y;
    if (layer == 1) { tile = frame.surf.z; }
    if (layer == 2) { tile = frame.surf.w; }
    let uv = i.wpos.xz / tile;
    let gx = dux / tile;
    let gy = duy / tile;
    let footprint = max(length(gx), length(gy)) * 1024.0;
    let lod = clamp(log2(max(footprint, 1.0)), 0.0, 9.0);
    albedo = pow(textureSampleGrad(albedoTex, texSamp, uv, layer, gx, gy).rgb, vec3f(2.2)) * pow(max(i.col, vec3f(0.0)), vec3f(2.2));
    let sf = textureSampleLevel(surfaceTex, texSamp, uv, layer, lod);
    rough = sf.g;
    bumpPerturb = true;
    let texel = 1.0 / 1024.0;
    let hC = sf.r;
    let hX = textureSampleLevel(surfaceTex, texSamp, uv + vec2f(texel, 0.0), layer, lod).r;
    let hZ = textureSampleLevel(surfaceTex, texSamp, uv + vec2f(0.0, texel), layer, lod).r;
    let slopeX = (hX - hC) / (texel * tile);
    let slopeZ = (hZ - hC) / (texel * tile);
    if (abs(N.y) > 0.5) {
      N = normalize(vec3f(N.x - slopeX * 0.05, N.y, N.z - slopeZ * 0.05));
    }
  }
  // Wetness: darker, glossier surfaces
  albedo = albedo * (1.0 - wet * 0.35);
  rough = mix(rough, 0.08, wet * 0.8);

  let L = normalize(frame.sunDir.xyz);
  let ndl = max(dot(N, L), 0.0);
  let sh = shadowAt(i.wpos, N);

  // Hemispheric sky ambient + ground bounce
  let up = N.y * 0.5 + 0.5;
  let ambient = mix(frame.groundColor.rgb * 0.5, frame.skyZenith.rgb * 0.55, up);

  // GGX specular (sun)
  let H = normalize(L + V);
  let a = max(rough * rough, 0.004);
  let ndh = max(dot(N, H), 0.0);
  let dn = ndh * ndh * (a * a - 1.0) + 1.0;
  let D = (a * a) / (3.14159 * dn * dn);
  let F = 0.04 + 0.96 * pow(1.0 - max(dot(H, V), 0.0), 5.0);
  let spec = D * F * ndl * sh * 0.25 / max(4.0 * max(dot(N, V), 0.05) * max(ndl, 0.05), 0.001);

  var col = albedo * (ambient + frame.sunColor.rgb * ndl * sh) + frame.sunColor.rgb * spec * (0.2 + wet * 0.8);
  // Ambient occlusion from the vertex colour darkening at curbs / joints
  col = col * mix(0.82, 1.0, clamp(i.col.r, 0.0, 1.0)) ;

  // Distance fog toward the horizon colour
  let dist = length(i.wpos - camPos);
  let fogF = 1.0 - exp(-frame.fog.x * dist);
  col = mix(col, frame.skyHorizon.rgb, clamp(fogF, 0.0, 1.0));

  col = col * frame.misc.y;
  col = aces(col);
  col = pow(col, vec3f(1.0 / 2.2));
  return vec4f(col, 1.0);
}
`;

// Depth-only pass for the sun shadow map.
export const SHADOW_WGSL = /* wgsl */`
struct Light { vp: mat4x4f };
@group(0) @binding(0) var<uniform> light: Light;
struct VIn {
  @location(0) pos: vec3f,
  @location(1) nrm: vec3f,
  @location(2) uv: vec2f,
  @location(3) col: vec3f,
  @location(4) mat: f32,
};
@vertex fn vs(v: VIn) -> @builtin(position) vec4f {
  return light.vp * vec4f(v.pos, 1.0);
}
`;

// Sky: analytic gradient plus a soft sun disc and glow, drawn as a fullscreen triangle.
export const SKY_WGSL = /* wgsl */`
struct Frame {
  viewProj: mat4x4f,
  lightVP: mat4x4f,
  camPos: vec4f,
  sunDir: vec4f,
  sunColor: vec4f,
  skyZenith: vec4f,
  skyHorizon: vec4f,
  groundColor: vec4f,
  fog: vec4f,
  surf: vec4f,
  misc: vec4f,
  invViewProj: mat4x4f,
};
@group(0) @binding(0) var<uniform> frame: Frame;
struct VOut { @builtin(position) clip: vec4f, @location(0) ndc: vec2f };
@vertex fn vs(@builtin(vertex_index) vi: u32) -> VOut {
  var p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
  var o: VOut;
  o.clip = vec4f(p[vi], 0.9999, 1.0);
  o.ndc = p[vi];
  return o;
}
fn aces(x: vec3f) -> vec3f {
  return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), vec3f(0.0), vec3f(1.0));
}
@fragment fn fs(i: VOut) -> @location(0) vec4f {
  let far = frame.invViewProj * vec4f(i.ndc, 1.0, 1.0);
  let dir = normalize(far.xyz / far.w - frame.camPos.xyz);
  let up = clamp(dir.y, -0.2, 1.0);
  var sky = mix(frame.skyHorizon.rgb, frame.skyZenith.rgb, pow(clamp(up, 0.0, 1.0), 0.45));
  let sunD = normalize(frame.sunDir.xyz);
  let cs = max(dot(dir, sunD), 0.0);
  let glow = pow(cs, 8.0) * 0.45 + pow(cs, 64.0) * 0.6;
  let disc = smoothstep(0.9994, 0.9998, cs);
  sky = sky + frame.sunColor.rgb * 0.35 * glow + frame.sunColor.rgb * disc * 4.0;
  sky = aces(sky * frame.misc.y);
  return vec4f(pow(sky, vec3f(1.0 / 2.2)), 1.0);
}
`;

// Overlay: gizmos, handles, previews. Position + RGBA colour, no depth test.
export const OVERLAY_WGSL = /* wgsl */`
struct Frame { viewProj: mat4x4f };
@group(0) @binding(0) var<uniform> frame: Frame;
struct VIn { @location(0) pos: vec3f, @location(1) col: vec4f };
struct VOut { @builtin(position) clip: vec4f, @location(0) col: vec4f };
@vertex fn vs(v: VIn) -> VOut {
  var o: VOut;
  o.clip = frame.viewProj * vec4f(v.pos, 1.0);
  o.col = v.col;
  return o;
}
@fragment fn fs(i: VOut) -> @location(0) vec4f { return i.col; }
`;

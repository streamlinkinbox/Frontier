// The unified density field.
//
//   F(p) = (H(xz) - y) * n_y            <- the (eroded) heightfield, as an approximate signed distance
//        + w(xz) * V(p)                 <- volumetric detail, only where the heightfield stretches
//   F    = smax(F, Prims(p) + V, k)     <- SDF primitives (spires / arches) unioned in
//
// H, w, flow, blurred height etc. all come from the SAME 2D maps the heightmap renderer uses,
// so erosion, flow and every other 2D algorithm affect both representations identically.
// Where w == 0 the zero set of F is exactly the heightfield, so the voxel and heightmap meshes
// coincide in the transition band and the seam is invisible (normals match analytically too).
export const VOLUME_DECL = (b) => /* wgsl */ `
struct VolL { a: vec4f, b: vec4f }
struct Vol {
  info: vec4f,    // layerCount, primCount, smoothK, hybridMode
  world: vec4f,   // S, Hs, N, texel
  extra: vec4f,   // voxel size, wLo, wHi, dispMax
  L: array<VolL, 8>,
}
struct Prim { a: vec4f, b: vec4f, c: vec4f }
@group(0) @binding(${b.h}) var hTex: texture_2d<f32>;
@group(0) @binding(${b.n}) var nTex: texture_2d<f32>;
@group(0) @binding(${b.a}) var aTex: texture_2d<f32>;
@group(0) @binding(${b.b}) var bTex: texture_2d<f32>;
@group(0) @binding(${b.s}) var samp: sampler;
@group(0) @binding(${b.v}) var<uniform> vol: Vol;
@group(0) @binding(${b.p}) var<storage, read> prims: array<Prim>;
`;

export const VOLUME_FN = /* wgsl */ `
fn hLoad(i: vec2i) -> f32 {
  let n = i32(vol.world.z);
  return textureLoad(hTex, clamp(i, vec2i(0), vec2i(n - 1)), 0).r;
}
// bilinear heightfield in metres; uv in [0,1] (texel-corner convention)
fn hBil(uv: vec2f) -> f32 {
  let t = clamp(uv, vec2f(0.0), vec2f(1.0)) * (vol.world.z - 1.0);
  let i = vec2i(floor(t)); let f = fract(t);
  return mix(mix(hLoad(i), hLoad(i + vec2i(1, 0)), f.x),
             mix(hLoad(i + vec2i(0, 1)), hLoad(i + vec2i(1, 1)), f.x), f.y) * vol.world.y;
}
fn tuv(uv: vec2f) -> vec2f { let n = vol.world.z; return (clamp(uv, vec2f(0.0), vec2f(1.0)) * (n - 1.0) + 0.5) / n; }
fn sN(uv: vec2f) -> vec4f { return textureSampleLevel(nTex, samp, tuv(uv), 0.0); }
fn sA(uv: vec2f) -> vec4f { return textureSampleLevel(aTex, samp, tuv(uv), 0.0); }
fn sB(uv: vec2f) -> vec4f { return textureSampleLevel(bTex, samp, tuv(uv), 0.0); }

fn volDetail(p: vec3f, w: f32, hb: f32, flow: f32) -> f32 {
  if (w <= 0.0) { return 0.0; }
  var v = 0.0;
  let cnt = i32(vol.info.x);
  for (var i = 0; i < cnt; i++) {
    let L = vol.L[i];
    let kind = i32(L.a.x); let amp = L.a.y; let sc = max(L.a.z, 0.1);
    let seed = u32(L.b.w) * 7919u + 3u;
    switch kind {
      case 1: { // strata ledges: a.w sharpness, b.x warp(m), b.y tilt(deg), b.z hardness variation
        let yy = p.y + L.b.x * fbm3(p / (sc * 7.0), 2, seed) + tan(radians(L.b.y)) * p.x;
        let layer = floor(yy / sc);
        let t = yy / sc - layer;
        let sh = clamp(L.a.w, 0.03, 0.9);
        let prof = smoothstep(0.0, 1.0 - sh, t) * (1.0 - smoothstep(1.0 - sh, 1.0, t));
        let hard = mix(1.0, 0.25 + 0.75 * hash11(layer, seed), L.b.z);
        v += amp * hard * (prof - 0.5);
      }
      case 2: { // overhang / undercut: tilts faces outward above the local mean height
        v += amp * clamp((p.y - hb) / sc, -1.0, 1.0);
      }
      case 3: { // 3D roughness: a.w octaves, b.x ridged
        var nz = fbm3(p / sc, clamp(i32(L.a.w), 1, 6), seed);
        if (L.b.x > 0.5) { nz = 0.35 - abs(nz); }
        v += amp * nz;
      }
      case 4: { // vertical rills / flutes: b.x vertical stretch, b.y flow influence
        let q = vec3f(p.x / sc, p.y / (sc * max(L.b.x, 1.0)), p.z / sc);
        let g = pow(1.0 - abs(noise3(q, seed)), 4.0);
        v -= amp * g * mix(1.0, 0.3 + flow * 2.0, clamp(L.b.y, 0.0, 1.0));
      }
      case 5: { // blocky fractures: voronoi-like cells in 3D (cheap: abs noise at two scales)
        let n1 = abs(noise3(p / sc, seed));
        let n2 = abs(noise3(p / (sc * 0.45) + vec3f(4.1), seed + 1u));
        v -= amp * (1.0 - smoothstep(0.0, 0.08 + L.a.w * 0.2, min(n1, n2 * 1.3)));
      }
      default: {}
    }
  }
  return v * w;
}

fn primD(p: vec3f) -> f32 {
  var best = -1e9;
  let cnt = i32(vol.info.y);
  for (var i = 0; i < cnt; i++) {
    let P = prims[i];
    let rel = p.xz - P.a.yz;
    let base = P.c.x;
    if (P.a.x < 1.5) {
      let r0 = P.a.w; let H = P.b.x;
      if (dot(rel, rel) > r0 * r0 * 3.2 || p.y > base + H + r0 || p.y < base - r0 * 2.0) { continue; }
      let t = clamp((p.y - base) / H, 0.0, 1.0);
      let nz = noise3(vec3f(p.x, p.y * 0.4, p.z) / (r0 * 0.8), u32(P.c.y));
      let r = r0 * mix(1.0, P.b.y, t) * (1.0 + P.c.w * 0.22 * sin(t * 11.0 + P.c.y) + P.c.z * 0.35 * nz);
      let side = length(rel) - r;
      let cap = p.y - (base + H);
      best = max(best, -smax(side, cap, r * 0.6));
    } else {
      let ang = radians(P.b.z);
      let dir = vec2f(cos(ang), sin(ang)); let perp = vec2f(-dir.y, dir.x);
      let u = dot(rel, dir); let wv = dot(rel, perp);
      let a = P.b.w * 0.5; let b = P.b.x; let T = P.a.w;
      if (abs(u) > a + T * 2.5 || abs(wv) > T * 3.0 || p.y > base + b + T * 2.5 || p.y < base - T * 3.0) { continue; }
      let q = vec2f(u / a, (p.y - base) / b);
      let lq = max(length(q), 1e-4);
      let e = (lq - 1.0) * mix(a, b, clamp(abs(q.y) / lq, 0.0, 1.0));
      let up = clamp(q.y, 0.0, 1.0);
      let th = T * mix(1.15, 0.7, up) * (1.0 + P.c.z * 0.4 * noise3(p / (T * 1.3), u32(P.c.y)));
      let sd = length(vec2f(e, wv * mix(1.0, 1.35, up))) - th;
      best = max(best, -sd);
    }
  }
  return best;
}

// returns (full density, base heightfield part)
fn densityParts(p: vec3f) -> vec2f {
  let uv = p.xz / vol.world.x;
  let h = hBil(uv);
  let nn = sN(uv);
  let A = sA(uv); let B = sB(uv);
  let base = (h - p.y) * max(nn.y, 0.08);
  let w = smoothstep(vol.extra.y, vol.extra.z, A.a);
  let vd = volDetail(p, w, B.z * vol.world.y, A.z);
  var d = base + vd;
  if (vol.info.y > 0.5) {
    let pd = primD(p);
    if (pd > -1e8) { d = smax(d, pd + vd * 0.5, vol.info.z); }
  }
  return vec2f(d, base);
}
fn density(p: vec3f) -> f32 { return densityParts(p).x; }

// Outward normal = normalize(n_heightfield - grad(F - base)). Where w == 0 this is exactly the
// heightfield normal, which is what makes the hybrid seam disappear.
fn voxNormal(p: vec3f, eps: f32) -> vec3f {
  let nh = normalize(sN(p.xz / vol.world.x).xyz);
  var g = vec3f(0.0);
  var k = vec3f(1.0, -1.0, -1.0); var d = densityParts(p + k * eps); g += k * (d.x - d.y);
  k = vec3f(-1.0, -1.0, 1.0); d = densityParts(p + k * eps); g += k * (d.x - d.y);
  k = vec3f(-1.0, 1.0, -1.0); d = densityParts(p + k * eps); g += k * (d.x - d.y);
  k = vec3f(1.0, 1.0, 1.0); d = densityParts(p + k * eps); g += k * (d.x - d.y);
  g = g / (4.0 * eps);
  let n = nh - g;
  if (dot(n, n) < 1e-8) { return nh; }
  return normalize(n);
}
`;

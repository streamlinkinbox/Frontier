// Render shaders: sky, heightmap surface, voxel surface, diorama skirts, ocean, shadow passes, post.
import { NOISE } from './common.js';
import { VOLUME_DECL, VOLUME_FN } from './volume.js';

const FRAME = /* wgsl */ `
struct Frame {
  viewProj: mat4x4f,
  invViewProj: mat4x4f,
  lightVP: mat4x4f,
  cam: vec4f,       // pos, time
  sun: vec4f,       // dir, intensity
  sunCol: vec4f,    // rgb, exposure
  zenith: vec4f,    // rgb, fog density
  horizon: vec4f,   // rgb, fog height falloff
  world: vec4f,     // S, Hs, seaY (m), N
  oc1: vec4f,       // shallow rgb, clarity (m)
  oc2: vec4f,       // deep rgb, wave amplitude (m)
  oc3: vec4f,       // wavelength (m), foam, wind deg, flags (1 enabled, 2 infinite)
  view: vec4f,      // view mode, region tint, shadows, mesh M
  res: vec4f,       // w, h, pixel angle (rad), skirt base y
  misc: vec4f,      // ambient, saturation, -, hybrid mode
}
struct MatL { c0: vec4f, c1: vec4f, hgt: vec4f, slp: vec4f, w0: vec4f, w1: vec4f, w2: vec4f, en: vec4f }
struct Mats { info: vec4f, L: array<MatL, 8> }
@group(0) @binding(0) var<uniform> F: Frame;
@group(0) @binding(8) var<uniform> mats: Mats;
@group(1) @binding(0) var shadowTex: texture_depth_2d;
@group(1) @binding(1) var shadowSamp: sampler_comparison;
`;

const LIGHTING = /* wgsl */ `
fn sky(dir: vec3f, disk: bool) -> vec3f {
  let L = normalize(F.sun.xyz);
  let y = dir.y;
  let t = pow(clamp(y, 0.0, 1.0), 0.45);
  var c = mix(F.horizon.rgb, F.zenith.rgb, t);
  c = mix(c, F.horizon.rgb * 0.7, smoothstep(0.0, -0.3, y));
  let mu = max(dot(dir, L), 0.0);
  let sunC = F.sunCol.rgb * F.sun.w;
  c += sunC * (pow(mu, 6.0) * 0.12 + pow(mu, 48.0) * 0.3) * (1.0 - 0.6 * t);
  if (disk) { c += sunC * smoothstep(0.99955, 0.9998, mu) * 25.0; }
  return c;
}
fn applyFog(col: vec3f, p: vec3f) -> vec3f {
  let d = length(p - F.cam.xyz);
  let dir = (p - F.cam.xyz) / max(d, 1e-3);
  let hAvg = max(0.5 * (p.y + F.cam.y) - F.world.z, 0.0);
  let amt = 1.0 - exp(-d * F.zenith.w * exp(-hAvg * F.horizon.w));
  return mix(col, sky(dir, false), clamp(amt, 0.0, 1.0));
}
fn shadowAt(p: vec3f, n: vec3f) -> f32 {
  if (F.view.z < 0.5) { return 1.0; }
  let lp = F.lightVP * vec4f(p + n * F.world.x * 0.0006, 1.0);
  let c = lp.xyz / lp.w;
  let uv = vec2f(c.x * 0.5 + 0.5, 0.5 - c.y * 0.5);
  let ts = 1.0 / f32(textureDimensions(shadowTex).x);
  var s = 0.0;
  for (var j = -1; j <= 1; j++) {
    for (var i = -1; i <= 1; i++) {
      s += textureSampleCompareLevel(shadowTex, shadowSamp, uv + vec2f(f32(i), f32(j)) * ts * 1.25, c.z - 0.0004);
    }
  }
  if (any(uv < vec2f(0.0)) || any(uv > vec2f(1.0))) { return 1.0; }
  return s / 9.0;
}
fn ramp(t: f32) -> vec3f {
  let x = clamp(t, 0.0, 1.0);
  let a = vec3f(0.05, 0.08, 0.20); let b = vec3f(0.12, 0.37, 1.0); let c = vec3f(0.55, 0.85, 0.75);
  let d = vec3f(0.95, 0.85, 0.45); let e = vec3f(1.0, 1.0, 1.0);
  if (x < 0.25) { return mix(a, b, x * 4.0); }
  if (x < 0.5) { return mix(b, c, (x - 0.25) * 4.0); }
  if (x < 0.75) { return mix(c, d, (x - 0.5) * 4.0); }
  return mix(d, e, (x - 0.75) * 4.0);
}
fn diverge(t: f32) -> vec3f {
  let x = clamp(t, -1.0, 1.0);
  return select(mix(vec3f(0.85), vec3f(0.12, 0.37, 1.0), -x), mix(vec3f(0.85), vec3f(1.0, 0.45, 0.2), x), x > 0.0);
}
fn mw(v: f32, w: f32) -> f32 {
  if (w >= 0.0) { return mix(1.0, clamp(v, 0.0, 1.0), w); }
  return mix(1.0, 1.0 - clamp(v, 0.0, 1.0), -w);
}
// procedural detail per material type: (albedo modulation 0..1, bump gradient xyz)
fn detail(kind: i32, p: vec3f, ds: f32, seed: u32) -> vec4f {
  switch kind {
    case 0: { // rock: layered, fractured
      let g1 = gnoise3d(p / ds, seed);
      let g2 = gnoise3d(p / (ds * 0.31), seed + 1u);
      let g3 = gnoise3d(p / (ds * 0.09), seed + 2u);
      let bandv = fract(p.y / (ds * 2.2) + g1.x * 0.35);
      let a = 0.5 + 0.22 * g1.x + 0.14 * g2.x + 0.07 * g3.x + 0.12 * (smoothstep(0.2, 0.5, bandv) - 0.5);
      return vec4f(a, g1.yzw * 0.45 + g2.yzw * 0.35 + g3.yzw * 0.25);
    }
    case 1: { // sand: wind ripples
      let wa = radians(F.oc3.z + 90.0);
      let wd = vec2f(cos(wa), sin(wa));
      let g = gnoise3d(p / (ds * 6.0), seed);
      let ph = dot(p.xz, wd) / ds * TAU + g.x * 5.0;
      let f = gnoise3d(p / (ds * 0.15), seed + 3u);
      return vec4f(0.5 + 0.06 * sin(ph) + 0.05 * f.x, vec3f(wd.x, 0.0, wd.y) * cos(ph) * 0.55 + f.yzw * 0.08);
    }
    case 2: { // vegetation / moss: mottled
      let g1 = gnoise3d(p / ds, seed);
      let g2 = gnoise3d(p / (ds * 0.2), seed + 4u);
      return vec4f(0.5 + 0.35 * g1.x + 0.2 * g2.x, g2.yzw * 0.35 + g1.yzw * 0.1);
    }
    case 3: { // snow: smooth drifts
      let g = gnoise3d(p / ds, seed);
      return vec4f(0.9 + 0.08 * g.x, g.yzw * 0.12);
    }
    case 4: { // scree / gravel
      let g1 = gnoise3d(p / ds, seed);
      let g2 = gnoise3d(p / (ds * 0.35), seed + 5u);
      let s = sign(g1.x);
      return vec4f(0.45 + 0.4 * abs(g1.x) + 0.1 * g2.x, s * g1.yzw * 0.7 + g2.yzw * 0.3);
    }
    default: { // soil / mud
      let g = gnoise3d(p / ds, seed);
      return vec4f(0.5 + 0.2 * g.x, g.yzw * 0.2);
    }
  }
}

fn shade(p: vec3f, n0: vec3f, uv: vec2f, isVox: f32, ao3: f32) -> vec3f {
  let A = sA(uv); let B = sB(uv); let NT = sN(uv);
  let Hs = F.world.y;
  let hN = p.y / Hs;
  let slope = degrees(acos(clamp(n0.y, -1.0, 1.0)));
  let curv = A.x * 0.5 + 0.5; let ao2 = A.y; let flow = A.z;
  let prot = NT.w * 0.5 + 0.5; let dep = B.x; let wear = B.y;
  let ao = ao2 * ao3;
  let mode = i32(F.view.x);
  let L = normalize(F.sun.xyz);
  let sunC = F.sunCol.rgb * F.sun.w;
  let sh = shadowAt(p, n0);
  let fp = length(p - F.cam.xyz) * F.res.z;   // pixel footprint (m)

  if (mode > 0 && mode != 11) {
    var c = vec3f(0.0);
    switch mode {
      case 1: { c = ramp((p.y - F.world.z) / Hs + 0.25); }
      case 2: { c = ramp(slope / 90.0); }
      case 3: { c = diverge(A.x * 1.5); }
      case 4: { c = diverge(NT.w); }
      case 5: { c = vec3f(ao); }
      case 6: { c = mix(vec3f(0.04, 0.05, 0.08), vec3f(0.35, 0.65, 1.0), flow) + vec3f(pow(flow, 4.0)); }
      case 7: { c = mix(vec3f(0.06), vec3f(1.0, 0.55, 0.25), wear); }
      case 8: { c = mix(vec3f(0.06), vec3f(0.95, 0.85, 0.5), dep); }
      case 9: {
        c = mix(vec3f(0.25), vec3f(0.85), clamp(1.0 - B.w, 0.0, 1.0) * 0.6);
        if (isVox > 0.5) { c = mix(vec3f(0.12, 0.37, 1.0), vec3f(1.0, 0.45, 0.2), A.w); }
      }
      case 10: { c = n0 * 0.5 + 0.5; }
      default: {}
    }
    let lit = 0.55 + 0.45 * max(dot(n0, L), 0.0) * (0.4 + 0.6 * sh);
    return c * lit;
  }

  // ---- material layer stack
  var alb = vec3f(0.42, 0.40, 0.38);
  var rough = 0.85;
  var bg = vec3f(0.0);
  let cnt = i32(mats.info.x);
  for (var i = 0; i < cnt; i++) {
    let M = mats.L[i];
    if (M.en.x < 0.5) { continue; }
    var c = M.hgt.w;
    c *= band(hN, M.hgt.x, M.hgt.y, M.hgt.z);
    c *= band(slope, M.slp.x, M.slp.y, M.slp.z);
    c *= mw(curv, M.w0.x) * mw(prot, M.w0.y) * mw(ao2, M.w0.z) * mw(flow, M.w0.w);
    c *= mw(dep, M.w1.x) * mw(wear, M.w1.y);
    if (c <= 0.002) { continue; }
    let seed = u32(i) * 31u + 7u;
    let bn = fbm3(p / max(M.w1.w, 0.5), 3, seed);
    let k = 0.5 / max(M.w2.z, 1.0);
    c = smoothstep(0.5 - k, 0.5 + k, c + bn * M.w1.z);
    if (c <= 0.002) { continue; }
    let det = detail(i32(M.c1.w), p, max(M.w2.x, 0.05), seed);
    // fade sub-pixel detail (prevents sparkle / aliasing at distance)
    let fade = 1.0 - smoothstep(0.12, 0.5, fp / max(M.w2.x, 0.05));
    let vn = clamp(fbm3(p / max(M.w2.w, 0.5), 3, seed + 9u) * 1.4 + 0.5, 0.0, 1.0);
    let col = mix(M.c0.rgb, M.c1.rgb, vn) * mix(1.0, 0.7 + 0.6 * det.x, fade);
    alb = mix(alb, col, c);
    rough = mix(rough, M.c0.a, c);
    bg = mix(bg, det.yzw * M.w2.y * fade, c);
  }
  let lum = dot(alb, vec3f(0.299, 0.587, 0.114));
  alb = max(mix(vec3f(lum), alb, F.misc.y), vec3f(0.0));
  let gperp = bg - dot(bg, n0) * n0;
  let n = normalize(n0 - gperp * mats.info.y);

  if (mode == 11) { return alb; }

  // ---- lighting
  let V = normalize(F.cam.xyz - p);
  let ndl = max(dot(n, L), 0.0);
  let H = normalize(L + V);
  let ndh = max(dot(n, H), 0.0);
  let ndv = max(dot(n, V), 1e-3);
  let a = rough * rough; let a2 = a * a;
  let dd = ndh * ndh * (a2 - 1.0) + 1.0;
  let D = a2 / (PI * dd * dd);
  let kk = (rough + 1.0) * (rough + 1.0) / 8.0;
  let Gs = (ndl / (ndl * (1.0 - kk) + kk)) * (ndv / (ndv * (1.0 - kk) + kk));
  let Fr = 0.04 + 0.96 * pow(1.0 - max(dot(H, V), 0.0), 5.0);
  let spec = D * Gs * Fr / (4.0 * ndv + 1e-3);
  let skyAmb = mix(F.horizon.rgb * 0.55, F.zenith.rgb * 1.1, n.y * 0.5 + 0.5) * F.misc.x;
  let bounce = sunC * alb * 0.06 * clamp(0.5 - 0.5 * n.y, 0.0, 1.0);
  var col = alb * (sunC * ndl * sh + skyAmb * ao + bounce * ao) + spec * sunC * sh;

  if (F.view.y > 0.5 && isVox > 0.5) {
    col = mix(col, vec3f(0.12, 0.37, 1.0) * (0.35 + dot(col, vec3f(0.33))), 0.55);
  }
  return col;
}
`;

const DECL = NOISE + FRAME + VOLUME_DECL({ h: 2, n: 3, a: 4, b: 5, s: 1, v: 6, p: 7 }) + VOLUME_FN + LIGHTING;

export const RENDER_WGSL = DECL + /* wgsl */ `
struct VOut { @builtin(position) pos: vec4f, @location(0) wp: vec3f }

fn gridPos(vi: u32) -> vec3f {
  let M = u32(F.view.w);
  let i = vi % M; let j = vi / M;
  let uv = vec2f(f32(i), f32(j)) / f32(M - 1u);
  return vec3f(uv.x * F.world.x, hBil(uv), uv.y * F.world.x);
}

// ------------------------------------------------ sky
struct FOut { @builtin(position) pos: vec4f, @location(0) ndc: vec2f }
@vertex fn vsFull(@builtin(vertex_index) vi: u32) -> FOut {
  var q = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
  let v = q[vi];
  return FOut(vec4f(v, 0.0, 1.0), v);
}
@fragment fn fsSky(in: FOut) -> @location(0) vec4f {
  let a = F.invViewProj * vec4f(in.ndc, 1.0, 1.0);
  let b = F.invViewProj * vec4f(in.ndc, 0.5, 1.0);
  let dir = normalize(b.xyz / b.w - a.xyz / a.w);
  return vec4f(sky(dir, true), 1.0);
}

// ------------------------------------------------ heightmap surface
@vertex fn vsTerrain(@builtin(vertex_index) vi: u32) -> VOut {
  let p = gridPos(vi);
  return VOut(F.viewProj * vec4f(p, 1.0), p);
}
@fragment fn fsTerrain(in: VOut) -> @location(0) vec4f {
  let uv = in.wp.xz / F.world.x;
  let B = sB(uv);
  if (F.misc.w > 0.5 && B.w >= 0.5) { discard; }
  let n = normalize(sN(uv).xyz);
  return vec4f(applyFog(shade(in.wp, n, uv, 0.0, 1.0), in.wp), 1.0);
}

// ------------------------------------------------ voxel surface (marching cubes output)
struct VxOut { @builtin(position) pos: vec4f, @location(0) wp: vec3f, @location(1) n: vec3f, @location(2) aw: vec2f }
@vertex fn vsVoxel(@location(0) p: vec4f, @location(1) nn: vec4f) -> VxOut {
  return VxOut(F.viewProj * vec4f(p.xyz, 1.0), p.xyz, nn.xyz, vec2f(p.w, nn.w));
}
@fragment fn fsVoxel(in: VxOut) -> @location(0) vec4f {
  let uv = in.wp.xz / F.world.x;
  if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) { discard; }
  let B = sB(uv);
  if (B.w < 0.42) { discard; }
  // where the volume adds nothing the surface IS the heightfield: use its exact normal so the seam vanishes
  let nh = normalize(sN(uv).xyz);
  let n = normalize(mix(nh, normalize(in.n), clamp(in.aw.y * 1.5, 0.0, 1.0)));
  return vec4f(applyFog(shade(in.wp, n, uv, 1.0, in.aw.x), in.wp), 1.0);
}

// ------------------------------------------------ diorama skirt
struct SOut { @builtin(position) pos: vec4f, @location(0) wp: vec3f, @location(1) nrm: vec3f, @location(2) top: f32 }
@vertex fn vsSkirt(@builtin(vertex_index) vi: u32) -> SOut {
  let M = u32(F.view.w); let segs = M - 1u;
  let e = vi / (segs * 6u); let r = vi % (segs * 6u);
  let s = r / 6u; let c = r % 6u;
  var along = array<u32, 6>(0u, 1u, 0u, 1u, 1u, 0u);
  var istop = array<u32, 6>(1u, 1u, 0u, 1u, 0u, 0u);
  let t = f32(s + along[c]) / f32(segs);
  var uv = vec2f(0.0); var nrm = vec3f(0.0);
  switch e {
    case 0u: { uv = vec2f(t, 0.0); nrm = vec3f(0.0, 0.0, -1.0); }
    case 1u: { uv = vec2f(1.0, t); nrm = vec3f(1.0, 0.0, 0.0); }
    case 2u: { uv = vec2f(1.0 - t, 1.0); nrm = vec3f(0.0, 0.0, 1.0); }
    default: { uv = vec2f(0.0, 1.0 - t); nrm = vec3f(-1.0, 0.0, 0.0); }
  }
  let hy = hBil(uv);
  let y = select(F.res.w, hy, istop[c] == 1u);
  let p = vec3f(uv.x * F.world.x, y, uv.y * F.world.x);
  return SOut(F.viewProj * vec4f(p, 1.0), p, nrm, hy);
}
@fragment fn fsSkirt(in: SOut) -> @location(0) vec4f {
  let depth = in.top - in.wp.y;
  let nz = fbm3(in.wp / vec3f(40.0, 9.0, 40.0), 3, 41u);
  let fine = noise3(in.wp / 2.5, 7u);
  let bandv = fract(in.wp.y / 17.0 + nz * 0.55);
  var c = mix(vec3f(0.115, 0.105, 0.095), vec3f(0.19, 0.175, 0.16), smoothstep(0.15, 0.85, bandv));
  c *= 0.9 + 0.12 * fine;
  // dark soil horizon right under the surface
  c = mix(vec3f(0.06, 0.05, 0.04), c, smoothstep(1.0, 9.0 + 4.0 * nz, depth));
  let L = normalize(F.sun.xyz);
  let sunC = F.sunCol.rgb * F.sun.w;
  let amb = mix(F.horizon.rgb, F.zenith.rgb, 0.35) * F.misc.x * 0.55;
  var col = c * (sunC * max(dot(in.nrm, L), 0.0) * 0.6 + amb);
  // fade the bottom edge into a soft base
  col *= mix(0.55, 1.0, smoothstep(F.res.w, F.res.w + (in.top - F.res.w) * 0.35, in.wp.y));
  if (in.wp.y < F.world.z && (u32(F.oc3.w) & 1u) != 0u) {
    col = mix(col, F.oc2.rgb * 0.6, clamp((F.world.z - in.wp.y) / max(F.oc1.w * 4.0, 1.0), 0.0, 0.85));
  }
  return vec4f(applyFog(col, in.wp), 1.0);
}

// ------------------------------------------------ ocean
// sum of directional gravity waves; components finer than the pixel footprint are faded out (no aliasing)
fn waves(xz: vec2f, t: f32, fp: f32) -> vec3f {
  var h = 0.0; var d = vec2f(0.0);
  let wa = radians(F.oc3.z);
  let amp0 = F.oc2.w; let lam0 = max(F.oc3.x, 1.0);
  for (var i = 0; i < 11; i++) {
    let fi = f32(i);
    let ang = wa + (hash11(fi, 77u) - 0.5) * 2.2;
    let dir = vec2f(cos(ang), sin(ang));
    let lam = lam0 * pow(0.74, fi) * (0.85 + 0.3 * hash11(fi, 78u));
    let fade = 1.0 - smoothstep(0.1, 0.3, fp / lam);
    if (fade <= 0.0) { break; }
    let k = TAU / lam;
    let w = sqrt(9.81 * k);
    let a = amp0 * pow(0.66, fi) * fade;
    let ph = k * dot(dir, xz) - w * t + hash11(fi, 79u) * TAU;
    h += a * sin(ph);
    d += a * k * cos(ph) * dir;
  }
  return vec3f(h, d);
}
@vertex fn vsOcean(@builtin(vertex_index) vi: u32) -> VOut {
  var q = array<vec2f, 6>(vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(0.0, 1.0), vec2f(1.0, 0.0), vec2f(1.0, 1.0), vec2f(0.0, 1.0));
  let S = F.world.x;
  let ext = select(0.0, S * 60.0, (u32(F.oc3.w) & 2u) != 0u);
  let xz = mix(vec2f(-ext), vec2f(S + ext), q[vi]);
  let p = vec3f(xz.x, F.world.z, xz.y);
  return VOut(F.viewProj * vec4f(p, 1.0), p);
}
@fragment fn fsOcean(in: VOut) -> @location(0) vec4f {
  let p = in.wp; let t = F.cam.w;
  let uv = p.xz / F.world.x;
  // outside the heightfield, continue the edge depth (sloping down) so there's no colour seam at the border
  let uvc = clamp(uv, vec2f(0.0), vec2f(1.0));
  let outside = length(uv - uvc) * F.world.x;
  var depth = F.world.z - hBil(uvc);
  if (outside > 0.0) { depth = max(depth, 0.0) + outside * 0.25 + 1.0; }
  if (depth < 0.0) { discard; }
  let dist = length(F.cam.xyz - p);
  let V = normalize(F.cam.xyz - p);
  // anisotropic footprint: at grazing angles one pixel spans fp/|V.y| metres along the view ray
  let fp = dist * F.res.z / max(abs(V.y), 0.04);
  let wv = waves(p.xz, t, fp);
  let r1 = gnoise3d(vec3f(p.x * 0.32, t * 0.45, p.z * 0.32), 11u);
  let r2 = gnoise3d(vec3f(p.x * 1.3, t * 0.9, p.z * 1.3), 12u);
  let rf1 = 1.0 - smoothstep(0.12, 0.4, fp * 0.32); let rf2 = 1.0 - smoothstep(0.12, 0.4, fp * 1.3);
  let g = wv.yz + vec2f(r1.y, r1.w) * 0.032 * rf1 + vec2f(r2.y, r2.w) * 0.018 * rf2;
  let n = normalize(vec3f(-g.x, 1.0, -g.y));
  let L = normalize(F.sun.xyz);
  let sunC = F.sunCol.rgb * F.sun.w;
  let clarity = max(F.oc1.w, 0.1);
  let absorb = 1.0 - exp(-depth / clarity);
  let waterCol = mix(F.oc1.rgb, F.oc2.rgb, absorb);
  let fres = 0.02 + 0.98 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
  let R = reflect(-V, n);
  let refl = sky(vec3f(R.x, abs(R.y), R.z), false);
  let sh = shadowAt(p, vec3f(0.0, 1.0, 0.0));
  let rl = max(dot(R, L), 0.0);
  let gl = mix(900.0, 120.0, smoothstep(0.5, 8.0, fp));
  let spec = (pow(rl, gl) * 80.0 * gl / 900.0 + pow(rl, 90.0) * 0.6) * sh;
  let skyAmb = mix(F.horizon.rgb, F.zenith.rgb, 0.6) * F.misc.x;
  let scatter = waterCol * (skyAmb * 0.9 + sunC * max(L.y, 0.0) * 0.35 * (0.5 + 0.5 * sh));
  let fa = F.oc3.y;
  let shore = 1.0 - smoothstep(0.0, 1.0 + 7.0 * fa, depth);
  let fn1 = noise3(vec3f(p.x * 0.12, t * 0.25, p.z * 0.12), 5u) * 0.5 + 0.5;
  let fn2 = noise3(vec3f(p.x * 0.6, t * 0.5, p.z * 0.6), 6u) * 0.5 + 0.5;
  let bands = 0.5 + 0.5 * sin(depth * 1.3 - t * 1.7 + fn1 * 6.0);
  var foam = shore * smoothstep(0.45, 0.85, bands * (0.6 + 0.6 * fn2) + shore * 0.45);
  // whitecaps: only on the highest crests, broken up by large-scale patches, filtered with distance
  let fpatch = smoothstep(0.55, 0.85, noise3(vec3f(p.x * 0.015, t * 0.05, p.z * 0.015), 8u) * 0.5 + 0.5);
  let crest = smoothstep(0.62, 0.95, wv.x / max(F.oc2.w * 1.6, 0.01) + 0.25 * fn2);
  foam += crest * fpatch * fn2 * 0.5 * (1.0 - smoothstep(4.0, 30.0, fp));
  foam = clamp(foam * fa, 0.0, 1.0);
  var col = mix(scatter, refl, fres) + spec * sunC;
  col = mix(col, vec3f(0.92) * (sunC * max(L.y, 0.0) * (0.5 + 0.5 * sh) + skyAmb), foam);
  let alpha = clamp(max(absorb * 1.1, fres) + foam, 0.0, 1.0);
  return vec4f(applyFog(col, p), alpha);
}

// ------------------------------------------------ shadow passes (depth only, same partition as the main pass)
struct ShOut { @builtin(position) pos: vec4f, @location(0) wp: vec3f }
@vertex fn vsShadowTerrain(@builtin(vertex_index) vi: u32) -> ShOut {
  let p = gridPos(vi);
  return ShOut(F.lightVP * vec4f(p, 1.0), p);
}
@fragment fn fsShadowTerrain(in: ShOut) {
  let uv = in.wp.xz / F.world.x;
  if (F.misc.w > 0.5 && sB(uv).w >= 0.5) { discard; }
}
@vertex fn vsShadowVoxel(@location(0) p: vec3f) -> ShOut { return ShOut(F.lightVP * vec4f(p, 1.0), p); }
@fragment fn fsShadowVoxel(in: ShOut) {
  let uv = in.wp.xz / F.world.x;
  if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0 || sB(uv).w < 0.42) { discard; }
}
`;

export const POST_WGSL = /* wgsl */ `
struct Post { p: vec4f }   // exposure, vignette, time, -
@group(0) @binding(0) var hdr: texture_2d<f32>;
@group(0) @binding(1) var smp: sampler;
@group(0) @binding(2) var<uniform> P: Post;
struct FOut { @builtin(position) pos: vec4f, @location(0) uv: vec2f }
@vertex fn vs(@builtin(vertex_index) vi: u32) -> FOut {
  var q = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
  let v = q[vi];
  return FOut(vec4f(v, 0.0, 1.0), vec2f(v.x * 0.5 + 0.5, 0.5 - v.y * 0.5));
}
fn aces(x: vec3f) -> vec3f {
  return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), vec3f(0.0), vec3f(1.0));
}
@fragment fn fs(in: FOut) -> @location(0) vec4f {
  var c = textureSampleLevel(hdr, smp, in.uv, 0.0).rgb;
  c = aces(c * P.p.x);
  let d = length(in.uv - 0.5);
  c *= 1.0 - P.p.y * smoothstep(0.35, 0.85, d);
  c = pow(c, vec3f(1.0 / 2.2));
  let dither = (fract(sin(dot(in.pos.xy, vec2f(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;
  return vec4f(c + dither, 1.0);
}
`;

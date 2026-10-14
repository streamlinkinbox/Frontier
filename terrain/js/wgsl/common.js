// Shared WGSL: hashing, gradient noise (2D/3D, with analytic derivatives), fBm family, voronoi.
// Everything is evaluated in WORLD units (metres) so results are resolution independent.
export const NOISE = /* wgsl */ `
const PI = 3.14159265;
const TAU = 6.2831853;
const ROT2 = mat2x2f(0.8, 0.6, -0.6, 0.8);
const ROT3 = mat3x3f(0.00, 0.80, 0.60, -0.80, 0.36, -0.48, -0.60, -0.48, 0.64);

fn pcg(v: u32) -> u32 {
  let s = v * 747796405u + 2891336453u;
  let w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
  return (w >> 22u) ^ w;
}
fn hash2i(p: vec2i, seed: u32) -> u32 { return pcg(pcg(bitcast<u32>(p.x) ^ seed) + bitcast<u32>(p.y)); }
fn hash3i(p: vec3i, seed: u32) -> u32 { return pcg(pcg(pcg(bitcast<u32>(p.x) ^ seed) + bitcast<u32>(p.y)) + bitcast<u32>(p.z)); }
fn u01(h: u32) -> f32 { return f32(h >> 8u) * (1.0 / 16777216.0); }
fn hash11(x: f32, seed: u32) -> f32 { return u01(pcg(bitcast<u32>(i32(floor(x))) ^ pcg(seed))); }

fn grad2(p: vec2i, seed: u32) -> vec2f { let a = u01(hash2i(p, seed)) * TAU; return vec2f(cos(a), sin(a)); }

// 2D gradient noise, value only. ~[-1,1]
fn perlin2(x: vec2f, seed: u32) -> f32 {
  let i = vec2i(floor(x)); let f = fract(x);
  let u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
  let a = dot(grad2(i, seed), f);
  let b = dot(grad2(i + vec2i(1, 0), seed), f - vec2f(1.0, 0.0));
  let c = dot(grad2(i + vec2i(0, 1), seed), f - vec2f(0.0, 1.0));
  let d = dot(grad2(i + vec2i(1, 1), seed), f - vec2f(1.0, 1.0));
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 1.4142;
}

// 2D gradient noise with analytic derivatives: (value, d/dx, d/dy)
fn perlin2d(x: vec2f, seed: u32) -> vec3f {
  let i = vec2i(floor(x)); let f = fract(x);
  let u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
  let du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
  let ga = grad2(i, seed); let gb = grad2(i + vec2i(1, 0), seed);
  let gc = grad2(i + vec2i(0, 1), seed); let gd = grad2(i + vec2i(1, 1), seed);
  let va = dot(ga, f); let vb = dot(gb, f - vec2f(1.0, 0.0));
  let vc = dot(gc, f - vec2f(0.0, 1.0)); let vd = dot(gd, f - vec2f(1.0, 1.0));
  let k1 = vb - va; let k2 = vc - va; let k4 = va - vb - vc + vd;
  let v = va + u.x * k1 + u.y * k2 + u.x * u.y * k4;
  let g = ga + u.x * (gb - ga) + u.y * (gc - ga) + u.x * u.y * (ga - gb - gc + gd)
        + du * vec2f(k1 + k4 * u.y, k2 + k4 * u.x);
  return vec3f(v, g.x, g.y) * 1.4142;
}

fn grad3(p: vec3i, seed: u32) -> vec3f {
  let h = hash3i(p, seed);
  return normalize(vec3f(f32(h & 1023u), f32((h >> 10u) & 1023u), f32((h >> 20u) & 1023u)) / 511.5 - 1.0 + vec3f(1e-4));
}

// 3D gradient noise with analytic derivatives: (value, grad) — Inigo Quilez formulation.
fn gnoise3d(x: vec3f, seed: u32) -> vec4f {
  let i = vec3i(floor(x)); let f = fract(x);
  let u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
  let du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
  let ga = grad3(i + vec3i(0, 0, 0), seed); let gb = grad3(i + vec3i(1, 0, 0), seed);
  let gc = grad3(i + vec3i(0, 1, 0), seed); let gd = grad3(i + vec3i(1, 1, 0), seed);
  let ge = grad3(i + vec3i(0, 0, 1), seed); let gf = grad3(i + vec3i(1, 0, 1), seed);
  let gg = grad3(i + vec3i(0, 1, 1), seed); let gh = grad3(i + vec3i(1, 1, 1), seed);
  let va = dot(ga, f - vec3f(0.0, 0.0, 0.0)); let vb = dot(gb, f - vec3f(1.0, 0.0, 0.0));
  let vc = dot(gc, f - vec3f(0.0, 1.0, 0.0)); let vd = dot(gd, f - vec3f(1.0, 1.0, 0.0));
  let ve = dot(ge, f - vec3f(0.0, 0.0, 1.0)); let vf = dot(gf, f - vec3f(1.0, 0.0, 1.0));
  let vg = dot(gg, f - vec3f(0.0, 1.0, 1.0)); let vh = dot(gh, f - vec3f(1.0, 1.0, 1.0));
  let k0 = va; let k1 = vb - va; let k2 = vc - va; let k3 = ve - va;
  let k4 = va - vb - vc + vd; let k5 = va - vc - ve + vg; let k6 = va - vb - ve + vf;
  let k7 = -va + vb + vc - vd + ve - vf - vg + vh;
  let v = k0 + k1 * u.x + k2 * u.y + k3 * u.z + k4 * u.x * u.y + k5 * u.y * u.z + k6 * u.z * u.x + k7 * u.x * u.y * u.z;
  let g = ga + u.x * (gb - ga) + u.y * (gc - ga) + u.z * (ge - ga)
        + u.x * u.y * (ga - gb - gc + gd) + u.y * u.z * (ga - gc - ge + gg) + u.z * u.x * (ga - gb - ge + gf)
        + u.x * u.y * u.z * (-ga + gb + gc - gd + ge - gf - gg + gh)
        + du * vec3f(k1 + k4 * u.y + k6 * u.z + k7 * u.y * u.z,
                     k2 + k5 * u.z + k4 * u.x + k7 * u.z * u.x,
                     k3 + k6 * u.x + k5 * u.y + k7 * u.x * u.y);
  return vec4f(v, g) * 1.6;
}
fn noise3(x: vec3f, seed: u32) -> f32 { return gnoise3d(x, seed).x; }

fn fbm2(p0: vec2f, oct: i32, lac: f32, gain: f32, seed: u32) -> f32 {
  var p = p0; var a = 0.5; var s = 0.0; var n = 0.0;
  for (var i = 0; i < oct; i++) {
    s += a * perlin2(p, seed + u32(i) * 101u); n += a; a *= gain;
    p = ROT2 * p * lac + vec2f(17.3, -9.1);
  }
  return s / max(n, 1e-5);
}
fn billow2(p0: vec2f, oct: i32, lac: f32, gain: f32, seed: u32) -> f32 {
  var p = p0; var a = 0.5; var s = 0.0; var n = 0.0;
  for (var i = 0; i < oct; i++) {
    s += a * abs(perlin2(p, seed + u32(i) * 101u)); n += a; a *= gain;
    p = ROT2 * p * lac + vec2f(17.3, -9.1);
  }
  return s / max(n, 1e-5);
}
// Musgrave-style ridged multifractal: valleys stay smooth, ridges accumulate detail.
fn ridged2(p0: vec2f, oct: i32, lac: f32, gain: f32, sharp: f32, seed: u32) -> f32 {
  var p = p0; var a = 0.5; var s = 0.0; var n = 0.0; var w = 1.0;
  for (var i = 0; i < oct; i++) {
    var r = 1.0 - abs(perlin2(p, seed + u32(i) * 101u));
    r = pow(clamp(r, 0.0, 1.0), sharp) * w;
    w = clamp(r * 1.6, 0.0, 1.0);
    s += r * a; n += a; a *= gain;
    p = ROT2 * p * lac + vec2f(17.3, -9.1);
  }
  return s / max(n, 1e-5);
}
// Derivative-damped fBm (gives naturally "eroded" looking slopes).
fn erosive2(p0: vec2f, oct: i32, lac: f32, gain: f32, strength: f32, seed: u32) -> f32 {
  var p = p0; var a = 0.5; var s = 0.0; var n = 0.0; var d = vec2f(0.0);
  for (var i = 0; i < oct; i++) {
    let v = perlin2d(p, seed + u32(i) * 101u);
    d += v.yz * a;
    s += a * v.x / (1.0 + strength * dot(d, d)); n += a; a *= gain;
    p = ROT2 * p * lac + vec2f(17.3, -9.1);
  }
  return s / max(n, 1e-5);
}
fn fbm3(p0: vec3f, oct: i32, seed: u32) -> f32 {
  var p = p0; var a = 0.5; var s = 0.0; var n = 0.0;
  for (var i = 0; i < oct; i++) {
    s += a * noise3(p, seed + u32(i) * 131u); n += a; a *= 0.5;
    p = ROT3 * p * 2.03 + vec3f(3.1, -7.7, 1.9);
  }
  return s / max(n, 1e-5);
}
// returns (F1, F2, cellRandom)
fn voronoi2(x: vec2f, jitter: f32, seed: u32) -> vec3f {
  let i = floor(x); let f = x - i;
  var f1 = 8.0; var f2 = 8.0; var id = 0.0;
  for (var j = -1; j <= 1; j++) {
    for (var k = -1; k <= 1; k++) {
      let g = vec2f(f32(k), f32(j));
      let h = hash2i(vec2i(i + g), seed);
      let o = vec2f(u01(h), u01(pcg(h))) * jitter + 0.5 * (1.0 - jitter);
      let d = length(g + o - f);
      if (d < f1) { f2 = f1; f1 = d; id = u01(pcg(h + 7u)); } else if (d < f2) { f2 = d; }
    }
  }
  return vec3f(f1, f2, id);
}
fn smax(a: f32, b: f32, k: f32) -> f32 {
  let h = max(k - abs(a - b), 0.0) / max(k, 1e-5);
  return max(a, b) + h * h * k * 0.25;
}
fn band(x: f32, lo: f32, hi: f32, s: f32) -> f32 {
  let ss = max(s, 1e-4);
  return smoothstep(lo - ss, lo + ss, x) * (1.0 - smoothstep(hi - ss, hi + ss, x));
}
`;

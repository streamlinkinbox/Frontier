// Heightfield compute passes. Heights are normalised (1.0 == heightScale metres).
// Texel (x,y) <-> world (x/(N-1)*S, y/(N-1)*S). All radii / scales are in metres.
import { NOISE } from './common.js';

// Uniform shared by every height pass. p0..p3 = 16 per-layer parameter slots.
const GEN_STRUCT = /* wgsl */ `
struct Gen {
  n: u32, kind: u32, blend: u32, maskType: u32,
  world: vec4f,   // S, Hs, texel(m), globalSeed
  p0: vec4f, p1: vec4f, p2: vec4f, p3: vec4f,
  m0: vec4f,      // mask lo, hi, soft, invert
  m1: vec4f,      // opacity, maskScale, maskSeed, aux
}
struct Prim { a: vec4f, b: vec4f, c: vec4f }
`;

// common accessors; "hin" must be declared by the including module
const HEIGHT_COMMON = /* wgsl */ `
fn NN() -> i32 { return i32(G.n); }
fn idx(x: i32, y: i32) -> i32 { let n = i32(G.n); return clamp(y, 0, n - 1) * n + clamp(x, 0, n - 1); }
fn ld(x: i32, y: i32) -> f32 { return hin[idx(x, y)]; }
fn bil(t: vec2f) -> f32 {
  let i = vec2i(floor(t)); let f = fract(t);
  return mix(mix(ld(i.x, i.y), ld(i.x + 1, i.y), f.x), mix(ld(i.x, i.y + 1), ld(i.x + 1, i.y + 1), f.x), f.y);
}
fn worldP(x: i32, y: i32) -> vec2f { return vec2f(f32(x), f32(y)) / f32(i32(G.n) - 1) * G.world.x; }
fn slopeDeg(x: i32, y: i32) -> f32 {
  let k = G.world.y / (2.0 * G.world.z);
  let dx = (ld(x + 1, y) - ld(x - 1, y)) * k;
  let dy = (ld(x, y + 1) - ld(x, y - 1)) * k;
  return degrees(atan(length(vec2f(dx, dy))));
}
fn maskAt(x: i32, y: i32, P: vec2f) -> f32 {
  let t = G.maskType;
  if (t == 0u) { return 1.0; }
  var v = 0.0;
  if (t == 1u) { v = ld(x, y); }
  else if (t == 2u) { v = slopeDeg(x, y) / 90.0; }
  else { v = fbm2(P / max(G.m1.y, 1.0), 4, 2.0, 0.5, u32(G.m1.z) * 977u + 5u) * 0.75 + 0.5; }
  var m = band(v, G.m0.x, G.m0.y, G.m0.z);
  if (G.m0.w > 0.5) { m = 1.0 - m; }
  return m;
}
`;

const HEAD = (extra) => NOISE + GEN_STRUCT + /* wgsl */ `
@group(0) @binding(0) var<uniform> G: Gen;
@group(0) @binding(1) var<storage, read> hin: array<f32>;
${extra}
` + HEIGHT_COMMON;

// ---------------------------------------------------------------- generators + simple filters
export const GEN_WGSL = HEAD(`@group(0) @binding(2) var<storage, read_write> hout: array<f32>;`) + /* wgsl */ `
fn warpP(P: vec2f, seed: u32) -> vec2f {
  let wa = G.p1.z;
  if (wa <= 0.0) { return P; }
  let q = P / max(G.p1.w, 1.0);
  return P + wa * vec2f(fbm2(q, 4, 2.0, 0.5, seed ^ 0x9e37u), fbm2(q + vec2f(5.2, 1.3), 4, 2.0, 0.5, seed ^ 0x7f4au));
}
fn generator(k: u32, P0: vec2f, seed: u32) -> f32 {
  let S = G.world.x;
  let P = warpP(P0, seed);
  let sc = max(G.p0.x, 1.0);
  let oct = clamp(i32(G.p0.w), 1, 12);
  let lac = G.p1.x; let gain = G.p1.y;
  let q = P / sc + G.p3.xy;
  var v = 0.0;
  switch k {
    case 1u: { v = clamp(fbm2(q, oct, lac, gain, seed) * 0.9 + 0.5, 0.0, 1.0); }
    case 2u: { v = ridged2(q, oct, lac, gain, max(G.p2.x, 0.5), seed); }
    case 3u: { v = billow2(q, oct, lac, gain, seed) * 1.8; }
    case 4u: {
      let vr = voronoi2(q, clamp(G.p2.y, 0.0, 1.0), seed);
      let mode = i32(G.p2.x); let e = max(G.p2.z, 0.01);
      if (mode == 0) { v = 1.0 - clamp(vr.x * vr.x * 1.4, 0.0, 1.0); }
      else if (mode == 1) { v = smoothstep(0.0, e, vr.y - vr.x); }
      else { v = (0.25 + 0.75 * vr.z) * smoothstep(0.0, e, vr.y - vr.x); }
    }
    case 5u: {
      let c = G.p2.xy * S; let rad = max(G.p2.z, 1.0);
      let d = length(P - c) / rad;
      let rn = ridged2(q, oct, lac, gain, 2.0, seed);
      let rough = G.p3.z;
      let cone = clamp(1.0 - d + (rn - 0.5) * rough, 0.0, 1.0);
      v = pow(cone, max(G.p2.w, 0.2)) * (1.0 - 0.35 * rough + 0.7 * rough * rn);
    }
    case 6u: {
      let ang = radians(G.p2.x);
      let dir = vec2f(cos(ang), sin(ang)); let perp = vec2f(-dir.y, dir.x);
      let u = dot(P, dir) / sc + fbm2(P / (sc * 3.0), 3, 2.0, 0.5, seed) * 1.2;
      let w2 = dot(P, perp) / (sc * 3.5);
      let t = fract(u + perlin2(vec2f(w2, 0.37), seed + 5u) * 0.9);
      let a = clamp(G.p2.y, 0.05, 0.95);
      var pr = select((1.0 - t) / (1.0 - a), t / a, t < a);
      pr = 1.0 - pow(1.0 - clamp(pr, 0.0, 1.0), max(G.p2.z, 0.3));
      let modu = clamp(0.6 + fbm2(P / (sc * 7.0), 3, 2.0, 0.5, seed + 9u) * G.p2.w * 1.6, 0.0, 1.2);
      v = pr * modu;
    }
    case 7u: {
      let c = G.p2.xy * S; let rad = max(G.p2.z, 1.0);
      let d = length(P0 - c) + fbm2(q, oct, lac, gain, seed + 3u) * rad * G.p3.z;
      v = 1.0 - smoothstep(rad - max(G.p2.w, 1.0), rad, d);
    }
    case 8u: {
      let c = abs(fbm2(q, oct, lac, gain, seed));
      v = -(1.0 - smoothstep(G.p2.x, G.p2.x + max(G.p2.y, 0.002), c));
    }
    case 9u: {
      let ang = radians(G.p2.x);
      let dir = vec2f(cos(ang), sin(ang));
      let d = dot(P - vec2f(S * 0.5), dir) - (G.p2.y - 0.5) * S;
      let w = max(G.p2.z, 1.0);
      v = 1.0 - smoothstep(-w * 0.5, w * 0.5, d);
    }
    case 10u: { v = clamp(erosive2(q, oct, lac, gain, G.p2.x, seed) * 0.9 + 0.5, 0.0, 1.0); }
    default: { v = 0.0; }
  }
  return G.p0.z + G.p0.y * v;
}
fn blendOp(h: f32, g: f32, mode: u32) -> f32 {
  switch mode {
    case 0u: { return g; }
    case 1u: { return h + g; }
    case 2u: { return h - g; }
    case 3u: { return h * g; }
    case 4u: { return max(h, g); }
    case 5u: { return min(h, g); }
    default: { return h + g; }
  }
}
@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let n = i32(G.n);
  let x = i32(gid.x); let y = i32(gid.y);
  if (x >= n || y >= n) { return; }
  let P = worldP(x, y);
  let h = ld(x, y);
  let seed = u32(G.p3.w) * 7919u + u32(G.world.w) * 104729u + 13u;
  var r = h;
  let k = G.kind;
  if (k < 20u) {
    r = blendOp(h, generator(k, P, seed), G.blend);
  } else if (k == 20u) {
    // domain warp of the existing heightfield
    let q = P / max(G.p0.x, 1.0);
    let o = clamp(i32(G.p0.w), 1, 8);
    let off = G.p0.y * vec2f(fbm2(q, o, 2.0, 0.5, seed), fbm2(q + vec2f(7.1, 3.3), o, 2.0, 0.5, seed + 1u));
    r = bil((P + off) / G.world.x * f32(n - 1));
  } else if (k == 21u) {
    // terraces: flat treads with risers of width p0.y (small = vertical)
    let steps = max(G.p0.x, 1.0);
    let jit = fbm2(P / max(G.p0.w, 1.0), 3, 2.0, 0.5, seed) * G.p0.z;
    let hh = h * steps + G.p1.x + jit;
    let i = floor(hh); let f = hh - i;
    let rw = clamp(G.p0.y, 0.01, 1.0);
    let f2 = smoothstep(1.0 - rw, 1.0, f);
    r = (i + f2 - G.p1.x) / steps;
  } else if (k == 22u) {
    var c = pow(max(h, 0.0), max(G.p0.x, 0.05));
    c = (c - 0.5) * G.p0.y + 0.5 + G.p0.z;
    if (G.p0.w > 0.5) { c = 1.0 - c; }
    r = clamp(c, G.p1.x, G.p1.y) * G.p1.z;
  }
  hout[idx(x, y)] = mix(h, r, G.m1.x * maskAt(x, y, P));
}
`;

// ---------------------------------------------------------------- separable gaussian / max filter
// p0.x radius (texels), p0.y dir (0=x,1=y), p0.z final (mix with horig using mask/opacity), p0.w mode (0 gauss, 1 max)
export const BLUR_WGSL = HEAD(`
@group(0) @binding(2) var<storage, read_write> hout: array<f32>;
@group(0) @binding(3) var<storage, read> horig: array<f32>;`) + /* wgsl */ `
@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let n = i32(G.n);
  let x = i32(gid.x); let y = i32(gid.y);
  if (x >= n || y >= n) { return; }
  let r = max(G.p0.x, 0.0);
  let R = min(i32(ceil(r)), 96);
  let dir = select(vec2i(1, 0), vec2i(0, 1), G.p0.y > 0.5);
  var s = 0.0; var ws = 0.0; var mx = -1e30;
  let sig = max(r * 0.5, 0.5);
  for (var i = -R; i <= R; i++) {
    let q = vec2i(x, y) + dir * i;
    let v = ld(q.x, q.y);
    let w = exp(-f32(i * i) / (2.0 * sig * sig));
    s += v * w; ws += w; mx = max(mx, v);
  }
  var b = s / max(ws, 1e-6);
  if (G.p0.w > 0.5) { b = mx; }
  let id = idx(x, y);
  if (G.p0.z > 0.5) {
    hout[id] = mix(horig[id], b, G.m1.x * maskAt(x, y, worldP(x, y)));
  } else {
    hout[id] = b;
  }
}
`;

// ---------------------------------------------------------------- thermal erosion (talus relaxation, mass conserving)
export const THERMAL_WGSL = HEAD(`
@group(0) @binding(2) var<storage, read_write> hout: array<f32>;
@group(0) @binding(3) var<storage, read_write> depo: array<atomic<i32>>;`) + /* wgsl */ `
@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let n = i32(G.n);
  let x = i32(gid.x); let y = i32(gid.y);
  if (x >= n || y >= n) { return; }
  let h = ld(x, y);
  let T = tan(radians(G.p0.x)) * G.world.z / G.world.y;
  var ds = 0.0;
  for (var j = -1; j <= 1; j++) {
    for (var i = -1; i <= 1; i++) {
      if (i == 0 && j == 0) { continue; }
      let dist = length(vec2f(f32(i), f32(j)));
      let d = ld(x + i, y + j) - h;
      let ex = abs(d) - T * dist;
      if (ex > 0.0) { ds += sign(d) * ex * 0.5; }
    }
  }
  let o = G.m1.x * maskAt(x, y, worldP(x, y));
  let dh = 0.11 * G.p0.y * ds * o;
  hout[idx(x, y)] = h + dh;
  if (dh > 0.0) { atomicAdd(&depo[idx(x, y)], i32(dh * G.world.y * 10000.0)); }
}
`;

// ---------------------------------------------------------------- erosion helpers (f32 <-> fixed point atomics)
const FX = '16777216.0';
export const FIXED_WGSL = HEAD(`
@group(0) @binding(2) var<storage, read_write> hout: array<f32>;
@group(0) @binding(3) var<storage, read_write> ha: array<atomic<i32>>;`) + /* wgsl */ `
@compute @workgroup_size(8, 8)
fn toFixed(@builtin(global_invocation_id) gid: vec3u) {
  let n = i32(G.n); let x = i32(gid.x); let y = i32(gid.y);
  if (x >= n || y >= n) { return; }
  atomicStore(&ha[idx(x, y)], i32(round(ld(x, y) * ${FX})));
}
@compute @workgroup_size(8, 8)
fn fromFixed(@builtin(global_invocation_id) gid: vec3u) {
  let n = i32(G.n); let x = i32(gid.x); let y = i32(gid.y);
  if (x >= n || y >= n) { return; }
  let h = ld(x, y);
  let e = f32(atomicLoad(&ha[idx(x, y)])) / ${FX};
  hout[idx(x, y)] = mix(h, e, G.m1.x * maskAt(x, y, worldP(x, y)));
}
`;

// ---------------------------------------------------------------- hydraulic droplet erosion (GPU, atomics)
export const DROPLET_WGSL = NOISE + /* wgsl */ `
struct Ero {
  n: u32, count: u32, batch: u32, lifetime: u32,
  world: vec4f,   // S, Hs, texel, seed
  a: vec4f,       // inertia, capacity, erode, deposit
  b: vec4f,       // evaporate(per step), gravity, minSlope, radius(texels)
  c: vec4f,       // brushWeightSum, maxSpeed, -, -
}
@group(0) @binding(0) var<uniform> E: Ero;
@group(0) @binding(1) var<storage, read_write> ha: array<atomic<i32>>;
@group(0) @binding(2) var<storage, read_write> wear: array<atomic<i32>>;
@group(0) @binding(3) var<storage, read_write> depo: array<atomic<i32>>;

fn cid(c: vec2i) -> i32 { let n = i32(E.n); return clamp(c.y, 0, n - 1) * n + clamp(c.x, 0, n - 1); }
fn hm(c: vec2i) -> f32 { return f32(atomicLoad(&ha[cid(c)])) / ${FX} * E.world.y; }
fn hgrad(pos: vec2f) -> vec3f {
  let c = vec2i(floor(pos)); let f = fract(pos);
  let h00 = hm(c); let h10 = hm(c + vec2i(1, 0)); let h01 = hm(c + vec2i(0, 1)); let h11 = hm(c + vec2i(1, 1));
  let gx = (h10 - h00) * (1.0 - f.y) + (h11 - h01) * f.y;
  let gy = (h01 - h00) * (1.0 - f.x) + (h11 - h10) * f.x;
  let h = mix(mix(h00, h10, f.x), mix(h01, h11, f.x), f.y);
  return vec3f(h, gx, gy);
}
fn addH(c: vec2i, metres: f32) {
  atomicAdd(&ha[cid(c)], i32(round(metres / E.world.y * ${FX})));
}
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u, @builtin(num_workgroups) nw: vec3u) {
  let id = gid.x + gid.y * nw.x * 64u;
  if (id >= E.count) { return; }
  let n = f32(E.n);
  var rng = pcg(id * 9781u + E.batch * 6271u + u32(E.world.w) * 31337u + 1u);
  rng = pcg(rng); let rx = u01(rng);
  rng = pcg(rng); let ry = u01(rng);
  var pos = vec2f(1.0 + rx * (n - 3.0), 1.0 + ry * (n - 3.0));
  var dir = vec2f(0.0);
  var speed = 1.0; var water = 1.0; var sed = 0.0;
  let tx = E.world.z;
  let R = i32(ceil(E.b.w));
  for (var s = 0u; s < E.lifetime; s++) {
    let cell = vec2i(floor(pos)); let off = fract(pos);
    let hg = hgrad(pos);
    dir = dir * E.a.x - hg.yz * (1.0 - E.a.x);
    let len = length(dir);
    if (len < 1e-7) {
      rng = pcg(rng); let an = u01(rng) * TAU; dir = vec2f(cos(an), sin(an));
    } else { dir = dir / len; }
    let np = pos + dir;
    if (np.x < 1.0 || np.y < 1.0 || np.x > n - 2.0 || np.y > n - 2.0) { break; }
    let dh = hgrad(np).x - hg.x;   // metres
    let cap = max(-dh, E.b.z * tx) * speed * water * E.a.y;
    if (sed > cap || dh > 0.0) {
      let amt = select((sed - cap) * E.a.w, min(dh, sed), dh > 0.0);
      sed -= amt;
      addH(cell, amt * (1.0 - off.x) * (1.0 - off.y));
      addH(cell + vec2i(1, 0), amt * off.x * (1.0 - off.y));
      addH(cell + vec2i(0, 1), amt * (1.0 - off.x) * off.y);
      addH(cell + vec2i(1, 1), amt * off.x * off.y);
      atomicAdd(&depo[cid(cell)], i32(amt * 10000.0));
    } else {
      let amt = min((cap - sed) * E.a.z, -dh);
      let c = vec2i(round(pos));
      for (var j = -R; j <= R; j++) {
        for (var i = -R; i <= R; i++) {
          let w = max(0.0, E.b.w - length(vec2f(f32(i), f32(j)))) / E.c.x;
          if (w > 0.0) {
            let q = c + vec2i(i, j);
            addH(q, -amt * w);
            atomicAdd(&wear[cid(q)], i32(amt * w * 10000.0));
          }
        }
      }
      sed += amt;
    }
    speed = min(sqrt(max(speed * speed - dh / tx * E.b.y, 0.0)), E.c.y);
    water *= (1.0 - E.b.x);
    pos = np;
    if (water < 0.01) { break; }
  }
}
`;

// ---------------------------------------------------------------- multiple-flow-direction accumulation + river carving
export const FLOW_WGSL = HEAD(`
@group(0) @binding(2) var<storage, read_write> outS: array<f32>;
@group(0) @binding(3) var<storage, read> fin: array<f32>;
@group(0) @binding(4) var<storage, read_write> fout: array<f32>;
@group(0) @binding(5) var<storage, read> sIn: array<f32>;`) + /* wgsl */ `
fn wgt(hFrom: f32, hTo: f32, dist: f32) -> f32 { return pow(max(hFrom - hTo, 0.0), 1.1) / dist; }
@compute @workgroup_size(8, 8)
fn prep(@builtin(global_invocation_id) gid: vec3u) {
  let n = i32(G.n); let x = i32(gid.x); let y = i32(gid.y);
  if (x >= n || y >= n) { return; }
  let h = ld(x, y);
  var s = 0.0;
  for (var j = -1; j <= 1; j++) { for (var i = -1; i <= 1; i++) {
    if (i == 0 && j == 0) { continue; }
    s += wgt(h, ld(x + i, y + j), length(vec2f(f32(i), f32(j))));
  } }
  outS[idx(x, y)] = s;
  fout[idx(x, y)] = 1.0;
}
@compute @workgroup_size(8, 8)
fn iter(@builtin(global_invocation_id) gid: vec3u) {
  let n = i32(G.n); let x = i32(gid.x); let y = i32(gid.y);
  if (x >= n || y >= n) { return; }
  let h = ld(x, y);
  var f = 1.0;
  for (var j = -1; j <= 1; j++) { for (var i = -1; i <= 1; i++) {
    if (i == 0 && j == 0) { continue; }
    let q = idx(x + i, y + j);
    let sj = sIn[q];
    if (sj > 0.0) { f += fin[q] * wgt(hin[q], h, length(vec2f(f32(i), f32(j)))) / sj; }
  } }
  fout[idx(x, y)] = f;
}
// channel strength from accumulated flow -> fout (blurred later, then applied by RIVER_APPLY)
@compute @workgroup_size(8, 8)
fn channel(@builtin(global_invocation_id) gid: vec3u) {
  let n = i32(G.n); let x = i32(gid.x); let y = i32(gid.y);
  if (x >= n || y >= n) { return; }
  let fl = log2(max(fin[idx(x, y)], 1.0)) / log2(f32(n) * f32(n) * 0.25);
  fout[idx(x, y)] = smoothstep(G.p0.y, G.p0.y + 0.25, fl);
}
`;

export const RIVER_APPLY_WGSL = HEAD(`
@group(0) @binding(2) var<storage, read_write> hout: array<f32>;
@group(0) @binding(3) var<storage, read> chan: array<f32>;`) + /* wgsl */ `
@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let n = i32(G.n); let x = i32(gid.x); let y = i32(gid.y);
  if (x >= n || y >= n) { return; }
  let h = ld(x, y);
  let c = clamp(chan[idx(x, y)] * 1.6, 0.0, 1.0);
  let r = h - G.p0.x / G.world.y * c;
  hout[idx(x, y)] = mix(h, r, G.m1.x * maskAt(x, y, worldP(x, y)));
}
`;

// ---------------------------------------------------------------- volumetric primitives: ground base + 2D footprint stamp
// Prim.a: kind(1 spire,2 arch), x, z, radius/thickness   Prim.b: height, taper, angle, span   Prim.c: base(m), seed, noise, bulge
export const PRIM_WGSL = HEAD(`
@group(0) @binding(2) var<storage, read_write> hout: array<f32>;
@group(0) @binding(3) var<storage, read_write> prims: array<Prim>;`) + /* wgsl */ `
fn texOf(P: vec2f) -> vec2f { return P / G.world.x * f32(i32(G.n) - 1); }
fn hAt(P: vec2f) -> f32 { return bil(clamp(texOf(P), vec2f(0.0), vec2f(f32(i32(G.n) - 1)))) * G.world.y; }
fn archFeet(p: Prim) -> array<vec2f, 2> {
  let a = radians(p.b.z); let d = vec2f(cos(a), sin(a)) * p.b.w * 0.5;
  return array<vec2f, 2>(p.a.yz - d, p.a.yz + d);
}
@compute @workgroup_size(64)
fn base(@builtin(global_invocation_id) gid: vec3u) {
  let i = i32(gid.x);
  if (i >= i32(G.p0.x)) { return; }
  var p = prims[i];
  var b = 1e9;
  if (p.a.x < 1.5) {
    for (var k = 0; k < 9; k++) {
      let an = f32(k) * TAU / 8.0;
      let o = select(vec2f(cos(an), sin(an)) * p.a.w * 1.1, vec2f(0.0), k == 8);
      b = min(b, hAt(p.a.yz + o));
    }
  } else {
    var f = archFeet(p);
    b = min(hAt(f[0]), hAt(f[1]));
  }
  p.c.x = b - 2.0;
  prims[i] = p;
}
@compute @workgroup_size(8, 8)
fn stamp(@builtin(global_invocation_id) gid: vec3u) {
  let n = i32(G.n); let x = i32(gid.x); let y = i32(gid.y);
  if (x >= n || y >= n) { return; }
  let P = worldP(x, y);
  var h = ld(x, y);
  let cnt = i32(G.p0.x);
  for (var i = 0; i < cnt; i++) {
    let p = prims[i];
    if (p.a.x < 1.5) {
      let d = length(P - p.a.yz);
      let pr = 1.0 - smoothstep(p.a.w * 0.45, p.a.w * 0.95, d);
      let top = (p.c.x + p.b.x * 0.82) / G.world.y;
      h = h + max(top - h, 0.0) * pr;
    } else {
      var f = archFeet(p);
      for (var k = 0; k < 2; k++) {
        let d = length(P - f[k]);
        let pr = 1.0 - smoothstep(p.a.w * 0.5, p.a.w * 1.1, d);
        let top = (p.c.x + p.b.x * 0.3) / G.world.y;
        h = h + max(top - h, 0.0) * pr;
      }
    }
  }
  hout[idx(x, y)] = h;
}
`;

export { GEN_STRUCT, HEIGHT_COMMON };

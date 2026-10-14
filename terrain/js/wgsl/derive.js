// Derivation: hybrid mask (where heightfield stretches -> voxels), surface maps, and per-brick stats.
import { NOISE } from './common.js';

const D_STRUCT = /* wgsl */ `
struct D {
  n: u32, primCount: u32, mode: u32, cols: u32,
  world: vec4f,   // S, Hs, texel, seaLevel(m)
  mask: vec4f,    // threshold deg, softness deg, wearScale m, depoScale m
  misc: vec4f,    // aoRadius m, protrusion radius m, brick world m, voxel m
  misc2: vec4f,   // curvature radius m, curvature gain, prim margin m, flow gain
}
struct Prim { a: vec4f, b: vec4f, c: vec4f }
`;
const ACC = /* wgsl */ `
fn idx(x: i32, y: i32) -> i32 { let n = i32(U.n); return clamp(y, 0, n - 1) * n + clamp(x, 0, n - 1); }
fn ld(x: i32, y: i32) -> f32 { return hin[idx(x, y)]; }
fn worldP(x: i32, y: i32) -> vec2f { return vec2f(f32(x), f32(y)) / f32(i32(U.n) - 1) * U.world.x; }
fn primFoot(p: Prim, P: vec2f, margin: f32) -> f32 {
  if (p.a.x < 1.5) {
    let d = length(P - p.a.yz);
    return 1.0 - smoothstep(p.a.w * 1.05, p.a.w * 1.05 + margin, d);
  }
  let a = radians(p.b.z); let dir = vec2f(cos(a), sin(a));
  let rel = P - p.a.yz;
  let t = clamp(dot(rel, dir), -p.b.w * 0.5, p.b.w * 0.5);
  let d = length(rel - dir * t);
  return 1.0 - smoothstep(p.a.w * 1.2, p.a.w * 1.2 + margin, d);
}
`;

// m0  : displacement weight source (steepness + primitive footprints), 0..1
// mbin: soft-binary version that gets dilated + blurred into mVox (mesh partition mask)
export const MASK_WGSL = NOISE + D_STRUCT + /* wgsl */ `
@group(0) @binding(0) var<uniform> U: D;
@group(0) @binding(1) var<storage, read> hin: array<f32>;
@group(0) @binding(2) var<storage, read> prims: array<Prim>;
@group(0) @binding(3) var<storage, read_write> m0: array<f32>;
@group(0) @binding(4) var<storage, read_write> mbin: array<f32>;
` + ACC + /* wgsl */ `
@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let n = i32(U.n); let x = i32(gid.x); let y = i32(gid.y);
  if (x >= n || y >= n) { return; }
  let k = U.world.y / (2.0 * U.world.z);
  let dx = (ld(x + 1, y) - ld(x - 1, y)) * k;
  let dy = (ld(x, y + 1) - ld(x, y - 1)) * k;
  let s = degrees(atan(length(vec2f(dx, dy))));
  var m = smoothstep(U.mask.x - U.mask.y, U.mask.x + U.mask.y, s);
  let P = worldP(x, y);
  for (var i = 0; i < i32(U.primCount); i++) { m = max(m, primFoot(prims[i], P, U.misc2.z)); }
  if (U.mode == 0u) { m = 0.0; }
  m0[idx(x, y)] = m;
  var b = clamp(m * 8.0, 0.0, 1.0);
  if (U.mode == 2u) { b = 1.0; }
  mbin[idx(x, y)] = b;
}
`;

export const PACK_WGSL = NOISE + D_STRUCT + /* wgsl */ `
@group(0) @binding(0) var<uniform> U: D;
@group(0) @binding(1) var<storage, read> hin: array<f32>;
@group(0) @binding(2) var<storage, read> hblur: array<f32>;
@group(0) @binding(3) var<storage, read> flow: array<f32>;
@group(0) @binding(4) var<storage, read> wear: array<i32>;
@group(0) @binding(5) var<storage, read> depo: array<i32>;
@group(0) @binding(6) var<storage, read> m0: array<f32>;
@group(0) @binding(7) var<storage, read> mvox: array<f32>;
@group(0) @binding(8) var hTexW: texture_storage_2d<r32float, write>;
@group(0) @binding(9) var nTexW: texture_storage_2d<rgba16float, write>;
@group(0) @binding(10) var aTexW: texture_storage_2d<rgba16float, write>;
@group(0) @binding(11) var bTexW: texture_storage_2d<rgba16float, write>;
` + ACC + /* wgsl */ `
fn bilH(t: vec2f) -> f32 {
  let i = vec2i(floor(t)); let f = fract(t);
  return mix(mix(ld(i.x, i.y), ld(i.x + 1, i.y), f.x), mix(ld(i.x, i.y + 1), ld(i.x + 1, i.y + 1), f.x), f.y);
}
@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let n = i32(U.n); let x = i32(gid.x); let y = i32(gid.y);
  if (x >= n || y >= n) { return; }
  let tx = U.world.z; let Hs = U.world.y;
  let h = ld(x, y);
  let k = Hs / (2.0 * tx);
  let dx = (ld(x + 1, y) - ld(x - 1, y)) * k;
  let dy = (ld(x, y + 1) - ld(x, y - 1)) * k;
  let nrm = normalize(vec3f(-dx, 1.0, -dy));

  // curvature (laplacian at a fixed world radius)
  let s = max(1, i32(round(U.misc2.x / tx)));
  let lap = (ld(x + s, y) + ld(x - s, y) + ld(x, y + s) + ld(x, y - s) - 4.0 * h) * Hs / (f32(s * s) * tx * tx);
  let curv = tanh(-lap * U.misc2.y);   // + = convex

  // protrusion (height above large-scale mean)
  let prot = tanh((h - hblur[idx(x, y)]) * Hs / max(U.misc.y * 0.15, 0.5));

  // horizon based ambient occlusion
  var occ = 0.0;
  let R = U.misc.x;
  for (var d = 0; d < 12; d++) {
    let an = (f32(d) + 0.5) * TAU / 12.0;
    let dir = vec2f(cos(an), sin(an));
    var mx = 0.0;
    for (var st = 1; st <= 10; st++) {
      let dist = R * pow(f32(st) / 10.0, 1.7) + tx;
      let t = vec2f(f32(x), f32(y)) + dir * dist / tx;
      let hh = bilH(clamp(t, vec2f(0.0), vec2f(f32(n - 1))));
      mx = max(mx, (hh - h) * Hs / dist);
    }
    occ += mx / sqrt(1.0 + mx * mx);
  }
  let ao = clamp(1.0 - occ / 12.0 * 1.25, 0.0, 1.0);

  let fl = clamp(log2(max(flow[idx(x, y)], 1.0)) / log2(f32(n) * f32(n) * 0.25) * U.misc2.w, 0.0, 1.0);
  let we = 1.0 - exp(-f32(max(wear[idx(x, y)], 0)) / 10000.0 / max(U.mask.z, 1e-3));
  let de = 1.0 - exp(-f32(max(depo[idx(x, y)], 0)) / 10000.0 / max(U.mask.w, 1e-3));

  let c = vec2i(x, y);
  textureStore(hTexW, c, vec4f(h, 0.0, 0.0, 0.0));
  textureStore(nTexW, c, vec4f(nrm, prot));
  textureStore(aTexW, c, vec4f(curv, ao, fl, m0[idx(x, y)]));
  textureStore(bTexW, c, vec4f(de, we, hblur[idx(x, y)], mvox[idx(x, y)]));
}
`;

// One thread per brick column: min/max height over the footprint, max partition mask, primitive extents.
export const STATS_WGSL = NOISE + D_STRUCT + /* wgsl */ `
@group(0) @binding(0) var<uniform> U: D;
@group(0) @binding(1) var<storage, read> hin: array<f32>;
@group(0) @binding(2) var<storage, read> mvox: array<f32>;
@group(0) @binding(3) var<storage, read> prims: array<Prim>;
@group(0) @binding(4) var<storage, read_write> stats: array<vec4f>;
` + ACC + /* wgsl */ `
@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let cols = i32(U.cols);
  let bx = i32(gid.x); let bz = i32(gid.y);
  if (bx >= cols || bz >= cols) { return; }
  let n = i32(U.n); let tx = U.world.z; let B = U.misc.z; let v = U.misc.w;
  let x0w = f32(bx) * B - 2.0 * v; let x1w = f32(bx + 1) * B + 2.0 * v;
  let z0w = f32(bz) * B - 2.0 * v; let z1w = f32(bz + 1) * B + 2.0 * v;
  let x0 = clamp(i32(floor(x0w / tx)), 0, n - 1); let x1 = clamp(i32(ceil(x1w / tx)), 0, n - 1);
  let z0 = clamp(i32(floor(z0w / tx)), 0, n - 1); let z1 = clamp(i32(ceil(z1w / tx)), 0, n - 1);
  var mn = 1e9; var mx = -1e9; var mm = 0.0;
  for (var z = z0; z <= z1; z++) {
    for (var x = x0; x <= x1; x++) {
      let h = ld(x, z);
      mn = min(mn, h); mx = max(mx, h); mm = max(mm, mvox[idx(x, z)]);
    }
  }
  mn *= U.world.y; mx *= U.world.y;
  for (var i = 0; i < i32(U.primCount); i++) {
    let p = prims[i];
    var rad = p.a.w * 1.6;
    if (p.a.x > 1.5) { rad = p.b.w * 0.5 + p.a.w * 1.6; }
    let cx = clamp(p.a.y, x0w, x1w); let cz = clamp(p.a.z, z0w, z1w);
    if (length(vec2f(cx, cz) - p.a.yz) < rad) {
      mx = max(mx, p.c.x + p.b.x + p.a.w);
      mn = min(mn, p.c.x - p.a.w);
      mm = 1.0;
    }
  }
  stats[bz * cols + bx] = vec4f(mn, mx, mm, 0.0);
}
`;

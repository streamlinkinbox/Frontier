// GPU marching cubes over sparse 16^3 bricks that sit only where the heightfield stretches.
import { NOISE } from './common.js';
import { VOLUME_DECL, VOLUME_FN } from './volume.js';

export const MC_WGSL = NOISE + VOLUME_DECL({ h: 6, n: 7, a: 8, b: 9, s: 10, v: 11, p: 12 }) + VOLUME_FN + /* wgsl */ `
struct MC { origin: vec4f, info: vec4u }   // origin xyz + voxel size ; info: brickCount, maxTris, brickOffset
@group(0) @binding(0) var<uniform> M: MC;
@group(0) @binding(1) var<storage, read> bricks: array<vec4i>;
@group(0) @binding(2) var<storage, read_write> dens: array<f32>;
@group(0) @binding(3) var<storage, read_write> verts: array<f32>;
@group(0) @binding(4) var<storage, read_write> counter: array<atomic<u32>>;
@group(0) @binding(5) var<storage, read> tri: array<i32>;

@compute @workgroup_size(64)
fn densityMain(@builtin(workgroup_id) wid: vec3u, @builtin(local_invocation_index) li: u32) {
  let b = wid.x;
  if (b >= M.info.x) { return; }
  let br = bricks[b + M.info.z];
  let v = M.origin.w;
  let o = M.origin.xyz + vec3f(br.xyz) * 16.0 * v;
  for (var i = li; i < 4913u; i += 64u) {
    let ix = i % 17u; let iy = (i / 17u) % 17u; let iz = i / 289u;
    dens[b * 4913u + i] = density(o + vec3f(f32(ix), f32(iy), f32(iz)) * v);
  }
}

@compute @workgroup_size(64)
fn mcMain(@builtin(workgroup_id) wid: vec3u, @builtin(local_invocation_index) li: u32) {
  let b = wid.x;
  if (b >= M.info.x) { return; }
  var CORNER = array<vec3u, 8>(vec3u(0u, 0u, 0u), vec3u(1u, 0u, 0u), vec3u(1u, 1u, 0u), vec3u(0u, 1u, 0u),
                               vec3u(0u, 0u, 1u), vec3u(1u, 0u, 1u), vec3u(1u, 1u, 1u), vec3u(0u, 1u, 1u));
  var EDGE = array<vec2u, 12>(vec2u(0u, 1u), vec2u(1u, 2u), vec2u(2u, 3u), vec2u(3u, 0u), vec2u(4u, 5u), vec2u(5u, 6u),
                              vec2u(6u, 7u), vec2u(7u, 4u), vec2u(0u, 4u), vec2u(1u, 5u), vec2u(2u, 6u), vec2u(3u, 7u));
  let br = bricks[b + M.info.z];
  let v = M.origin.w;
  let o = M.origin.xyz + vec3f(br.xyz) * 16.0 * v;
  let base0 = b * 4913u;
  for (var c = li; c < 4096u; c += 64u) {
    let cell = vec3u(c % 16u, (c / 16u) % 16u, c / 256u);
    var val: array<f32, 8>;
    var ci = 0u;
    for (var k = 0u; k < 8u; k++) {
      let q = cell + CORNER[k];
      val[k] = dens[base0 + q.x + q.y * 17u + q.z * 289u];
      if (val[k] < 0.0) { ci |= (1u << k); }
    }
    if (ci == 0u || ci == 255u) { continue; }
    let cp = o + vec3f(cell) * v;
    for (var t = 0u; t < 15u; t += 3u) {
      if (tri[ci * 16u + t] < 0) { break; }
      let slot = atomicAdd(&counter[0], 1u);
      if (slot >= M.info.y) { break; }
      for (var j = 0u; j < 3u; j++) {
        let e = EDGE[u32(tri[ci * 16u + t + j])];
        let a = val[e.x]; let bb = val[e.y];
        let tt = clamp(a / (a - bb), 0.0, 1.0);
        let pos = cp + mix(vec3f(CORNER[e.x]), vec3f(CORNER[e.y]), tt) * v;
        // per-vertex analytic normal + volumetric occlusion, computed once here instead of per pixel
        let uv = pos.xz / vol.world.x;
        let nh = normalize(sN(uv).xyz);
        let n = voxNormal(pos, v * 0.35);
        let wv = smoothstep(vol.extra.y, vol.extra.z, sA(uv).w);
        var occ = 0.0;
        for (var k = 1; k <= 3; k++) {
          let dd = v * f32(k) * 1.6;
          occ += max(dd + density(pos + n * dd), 0.0) / dd / f32(k);
        }
        let dev = clamp(length(n - nh) * 3.0, 0.0, 1.0);
        let ao = mix(1.0, clamp(1.0 - occ * 0.45, 0.25, 1.0), clamp(max(wv, dev), 0.0, 1.0));
        let w = (slot * 3u + j) * 8u;
        verts[w] = pos.x; verts[w + 1u] = pos.y; verts[w + 2u] = pos.z; verts[w + 3u] = ao;
        verts[w + 4u] = n.x; verts[w + 5u] = n.y; verts[w + 6u] = n.z; verts[w + 7u] = max(wv, dev);
      }
    }
  }
}
`;

export const MC_ARGS_WGSL = /* wgsl */ `
struct MC { origin: vec4f, info: vec4u }
@group(0) @binding(0) var<uniform> M: MC;
@group(0) @binding(1) var<storage, read_write> counter: array<u32>;
@group(0) @binding(2) var<storage, read_write> args: array<u32>;
@compute @workgroup_size(1)
fn main() {
  args[0] = min(counter[0], M.info.y) * 3u;
  args[1] = 1u; args[2] = 0u; args[3] = 0u;
}
`;

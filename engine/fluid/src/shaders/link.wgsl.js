// Two-way coupling between a fine nested domain and the coarse far-field simulation.
//
//  * coarse -> fine edge: copy the coarse water depth / mud / velocity along the fine domain border
//    (bilinear in world space) before the fine grid is stepped, so waves and river flow enter the
//    fine patch seamlessly instead of reflecting off its boundary.
//  * fine -> coarse interior: average the fine state over every coarse cell covered by the fine
//    patch after the fine step, so the coarse sea carries the same volume and momentum (and the
//    river water that leaves the patch).
//
// Both kernels use the same 12-word Link uniform and one workgroup layout, but separate bind
// groups (source/destination roles swap).

const LINK_STRUCT = /* wgsl */ `
struct Link {
  fOrigin: vec2f, fDx: f32, fNx: f32,
  fNy: f32, cOrigin: vec2f, cDx: f32,
  cNx: f32, cNy: f32, pad0: f32, pad1: f32,
};
fn cIdx(i: i32, j: i32) -> i32 { return clamp(j, 0, i32(L.cNy) - 1) * i32(L.cNx) + clamp(i, 0, i32(L.cNx) - 1); }
fn fIdx(i: i32, j: i32) -> i32 { return clamp(j, 0, i32(L.fNy) - 1) * i32(L.fNx) + clamp(i, 0, i32(L.fNx) - 1); }
`;

export const LINK_EDGE = /* wgsl */ `
${LINK_STRUCT}
@group(0) @binding(0) var<uniform> L: Link;
@group(0) @binding(1) var<storage, read> cHc: array<vec2f>;
@group(0) @binding(2) var<storage, read> cVel: array<vec2f>;
@group(0) @binding(3) var<storage, read_write> fHc: array<vec2f>;
@group(0) @binding(4) var<storage, read_write> fVel: array<vec2f>;
@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let i = i32(gid.x); let j = i32(gid.y);
  if (i >= i32(L.fNx) || j >= i32(L.fNy)) { return; }
  if (i > 0 && j > 0 && i < i32(L.fNx) - 1 && j < i32(L.fNy) - 1) { return; }
  let world = L.fOrigin + (vec2f(f32(i), f32(j)) + 0.5) * L.fDx;
  let f = (world - L.cOrigin) / L.cDx - 0.5;
  let fl = floor(f);
  let t = f - fl;
  let i0 = i32(fl.x); let j0 = i32(fl.y);
  let k00 = cIdx(i0, j0); let k10 = cIdx(i0 + 1, j0); let k01 = cIdx(i0, j0 + 1); let k11 = cIdx(i0 + 1, j0 + 1);
  let h = mix(mix(cHc[k00].x, cHc[k10].x, t.x), mix(cHc[k01].x, cHc[k11].x, t.x), t.y);
  let c = mix(mix(cHc[k00].y, cHc[k10].y, t.x), mix(cHc[k01].y, cHc[k11].y, t.x), t.y);
  let u = mix(mix(cVel[k00].x, cVel[k10].x, t.x), mix(cVel[k01].x, cVel[k11].x, t.x), t.y);
  let v = mix(mix(cVel[k00].y, cVel[k10].y, t.x), mix(cVel[k01].y, cVel[k11].y, t.x), t.y);
  let k = fIdx(i, j);
  fHc[k] = vec2f(max(h, 0.0), c);
  fVel[k] = vec2f(u, v);
}
`;

export const LINK_TO_COARSE = /* wgsl */ `
${LINK_STRUCT}
@group(0) @binding(0) var<uniform> L: Link;
@group(0) @binding(1) var<storage, read> fHc: array<vec2f>;
@group(0) @binding(2) var<storage, read> fVel: array<vec2f>;
@group(0) @binding(3) var<storage, read_write> cHc: array<vec2f>;
@group(0) @binding(4) var<storage, read_write> cVel: array<vec2f>;
@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let i = i32(gid.x); let j = i32(gid.y);
  if (i >= i32(L.cNx) || j >= i32(L.cNy)) { return; }
  let lo = L.fOrigin;
  let hi = L.fOrigin + vec2f(f32(L.fNx), f32(L.fNy)) * L.fDx;
  let world = L.cOrigin + (vec2f(f32(i), f32(j)) + 0.5) * L.cDx;
  if (world.x < lo.x || world.y < lo.y || world.x > hi.x || world.y > hi.y) { return; }
  var sh = 0.0; var sc = 0.0; var su = 0.0; var sv = 0.0;
  for (var sj = 0; sj < 4; sj++) {
    for (var si = 0; si < 4; si++) {
      let w = world + (vec2f(f32(si), f32(sj)) + 0.5) * (L.cDx / 4.0) - 0.5 * L.cDx;
      let f = (w - L.fOrigin) / L.fDx - 0.5;
      let fl = floor(f); let t = f - fl;
      let i0 = i32(fl.x); let j0 = i32(fl.y);
      let k00 = fIdx(i0, j0); let k10 = fIdx(i0 + 1, j0); let k01 = fIdx(i0, j0 + 1); let k11 = fIdx(i0 + 1, j0 + 1);
      let h = mix(mix(fHc[k00].x, fHc[k10].x, t.x), mix(fHc[k01].x, fHc[k11].x, t.x), t.y);
      let c = mix(mix(fHc[k00].y, fHc[k10].y, t.x), mix(fHc[k01].y, fHc[k11].y, t.x), t.y);
      let u = mix(mix(fVel[k00].x, fVel[k10].x, t.x), mix(fVel[k01].x, fVel[k11].x, t.x), t.y);
      let v = mix(mix(fVel[k00].y, fVel[k10].y, t.x), mix(fVel[k01].y, fVel[k11].y, t.x), t.y);
      sh += h; sc += c; su += u; sv += v;
    }
  }
  let k = i32(j) * i32(L.cNx) + i32(i);
  cHc[k] = vec2f(sh * (1.0 / 16.0), sc * (1.0 / 16.0));
  cVel[k] = vec2f(su * (1.0 / 16.0), sv * (1.0 / 16.0));
}
`;

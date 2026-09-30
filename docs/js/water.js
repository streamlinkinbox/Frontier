// Puddles (GTX): real 3D liquid.
//  * Terrain is dented into bowls; each bowl is filled with water PARTICLES.
//  * Simulation: MLS-MPM (Hu et al. 2018), weakly-compressible fluid (Tait EOS, gamma 7) + viscosity,
//    same fixed-point atomic P2G scheme as the sand solver. The puddle the car is in/near is simulated
//    live (128x24x128 grid @ 10 cm, 6 substeps/frame); the others rest (settled, frozen).
//  * Collisions: dented terrain (free-slip), spinning tyres (moving no-slip cylinders), car body box.
//  * Rendering: Screen-Space Fluid Rendering (GDC 2010): particle spheres -> eye-depth + thickness
//    buffers -> separable narrow-range depth filter (Truong & Yuksel 2018) -> normals from the smoothed
//    depth -> Fresnel reflection, sun glints, Beer-Lambert absorption by thickness (clear vs mud).

export const BED_D = 0.26;            // dent depth at the centre (m)
export const WATER_LEVEL = -0.04;     // still-water level (just below the rim)
const H = 0.1;                        // MPM cell size (m)
const GRID = [128, 24, 128];          // 12.8 x 2.4 x 12.8 m around the active puddle
const ORIGIN_Y = -0.4;
const SPACING = H / 2;                // 8 particles per cell at rest
const RHO0 = 8.0;
const SOUND = 110.0;                  // speed of sound (cells/s) -> 11 m/s, weakly compressible
const SUB = 4;                        // substeps per frame (CFL ~0.46)
const FIX = 16384.0;
const P_RADIUS = 0.066;               // render sphere radius (m); overlaps neighbours for a continuous surface
const STRIDE = 20;                    // floats per particle

export function shoreR(p, ang) {
  return p.r * (1 + 0.12 * Math.sin(3 * ang + p.seed) + 0.07 * Math.sin(5 * ang + p.seed * 2.1) + 0.04 * Math.sin(9 * ang + p.seed * 0.7));
}
function bedN(dn) { return dn >= 1 ? 0 : -BED_D * Math.pow(1 - dn * dn, 1.5); }
export function groundY(puddles, x, z) {
  for (const p of puddles) {
    const dx = x - p.x, dz = z - p.z, d = Math.hypot(dx, dz);
    if (d > p.r * 1.3) continue;
    const dn = d / shoreR(p, Math.atan2(dz, dx));
    if (dn < 1) return bedN(dn);
  }
  return 0;
}
export function puddleAt(puddles, x, z) {
  for (let i = 0; i < puddles.length; i++) {
    const p = puddles[i], dx = x - p.x, dz = z - p.z, d = Math.hypot(dx, dz);
    if (d < p.r * 1.3 && bedN(d / shoreR(p, Math.atan2(dz, dx))) < WATER_LEVEL + 0.01) return i;
  }
  return -1;
}

// ---------------------------------------------------------------- simulation
const TERRAIN = /* wgsl */`
const BED_D: f32 = ${BED_D};
fn shore(i: i32, q: vec2f) -> f32 {
  let p = U.pud[i]; let a = atan2(q.y, q.x); let s = f32(i) * 2.7 + 1.3;
  return p.z * (1.0 + 0.12 * sin(3.0 * a + s) + 0.07 * sin(5.0 * a + s * 2.1) + 0.04 * sin(9.0 * a + s * 0.7));
}
fn dnAt(i: i32, p: vec2f) -> f32 { let q = p - U.pud[i].xy; return length(q) / shore(i, q); }
fn bedAt(i: i32, p: vec2f) -> f32 {
  let dn = dnAt(i, p);
  if (dn >= 1.0) { return 0.0; }
  let k = 1.0 - dn * dn;
  return -BED_D * k * sqrt(k);
}
fn groundAt(p: vec2f) -> f32 { return min(bedAt(0, p), min(bedAt(1, p), bedAt(2, p))); }
fn groundN(p: vec2f) -> vec3f {
  let e = 0.05;
  let gx = groundAt(p + vec2f(e, 0.0)) - groundAt(p - vec2f(e, 0.0));
  let gz = groundAt(p + vec2f(0.0, e)) - groundAt(p - vec2f(0.0, e));
  return normalize(vec3f(-gx / (2.0 * e), 1.0, -gz / (2.0 * e)));
}
`;

const SIM_COMMON = /* wgsl */`
struct Particle { pos: vec4f, vel: vec4f, c0: vec4f, c1: vec4f, c2: vec4f };
struct FP {
  dims: vec4<i32>,
  rng: vec4<u32>,              // first particle, count
  origin: vec4f,               // xyz, h
  misc: vec4f,                 // dt, gravity (cells/s^2), K, rho0
  misc2: vec4f,                // viscosity, tyre push, wheel half-width, wheel radius
  carC: vec4f, carH: vec4f, carV: vec4f,
  pud: array<vec4f, 3>,        // x, z, r, mud
  wC: array<vec4f, 4>,         // wheel centre xyz, spin (rad/s)
  wA: array<vec4f, 4>,         // wheel axle axis xyz, -
};
@group(0) @binding(0) var<uniform> U: FP;
const FIX: f32 = ${FIX.toFixed(1)};
${TERRAIN}
fn gIdx(c: vec3<i32>) -> u32 { let d = U.dims.xyz; return u32(c.x + d.x * (c.y + d.y * c.z)); }
fn stencilOk(cell: vec3<i32>) -> bool { return all(cell >= vec3<i32>(1)) && all(cell < U.dims.xyz - vec3<i32>(1)); }
fn qweights(d: vec3f) -> array<vec3f, 3> {
  return array<vec3f, 3>(0.5 * (0.5 - d) * (0.5 - d), 0.75 - d * d, 0.5 * (0.5 + d) * (0.5 + d));
}
fn toFix(x: f32) -> i32 { return i32(clamp(x, -120000.0, 120000.0) * FIX); }
fn carLocal(wp: vec3f) -> vec3f {
  let d = wp - U.carC.xyz; let h = U.carC.w;
  return vec3f(d.x * cos(h) - d.z * sin(h), d.y, d.x * sin(h) + d.z * cos(h));
}
fn carVelAt(wp: vec3f) -> vec3f {
  let d = wp - U.carC.xyz; let w = U.carV.w;
  return vec3f(U.carV.x + w * d.z, 0.0, U.carV.z - w * d.x);
}
fn treadVel(ax: vec3f, spin: f32, rad: vec3f) -> vec3f {
  let t = cross(ax * spin, rad) * U.misc2.y;
  let l = length(t);
  return select(t, t * (4.0 / l), l > 4.0);            // tyre drags at most ~4 m/s of water with it
}
// returns (surface velocity, inside flag) of the tyre solids at wp (inflated by 'pad')
fn tyreAt(wp: vec3f, pad: f32) -> vec4f {
  for (var w = 0; w < 4; w++) {
    let c = U.wC[w].xyz; let ax = U.wA[w].xyz;
    let q = wp - c; let al = dot(q, ax); let rad = q - ax * al;
    if (abs(al) < U.misc2.z + pad && length(rad) < U.misc2.w + pad) {
      return vec4f(carVelAt(wp) + treadVel(ax, U.wC[w].w, rad), 1.0);
    }
  }
  return vec4f(0.0);
}
`;

const SIM = {
  clear: `
@group(0) @binding(1) var<storage, read_write> grid: array<vec4<i32>>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  if (g.x >= arrayLength(&grid) || U.dims.x <= 0) { return; }
  grid[g.x] = vec4<i32>(0);
}`,
  p2g1: `
@group(0) @binding(1) var<storage, read> ps: array<Particle>;
@group(0) @binding(2) var<storage, read_write> grid: array<atomic<i32>>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  if (g.x >= U.rng.y) { return; }
  let p = ps[U.rng.x + g.x];
  if (p.pos.w <= 0.0) { return; }
  let x = (p.pos.xyz - U.origin.xyz) / U.origin.w;
  let cell = vec3<i32>(floor(x));
  if (!stencilOk(cell)) { return; }
  var w = qweights(x - vec3f(cell) - 0.5);
  let v = p.vel.xyz / U.origin.w;
  let C = mat3x3f(p.c0.xyz, p.c1.xyz, p.c2.xyz);
  for (var gx = 0; gx < 3; gx++) { for (var gy = 0; gy < 3; gy++) { for (var gz = 0; gz < 3; gz++) {
    let wt = w[gx].x * w[gy].y * w[gz].z;
    let cx = cell + vec3<i32>(gx - 1, gy - 1, gz - 1);
    let mom = wt * (v + C * (vec3f(cx) - x + 0.5));
    let i = gIdx(cx) * 4u;
    atomicAdd(&grid[i], toFix(mom.x)); atomicAdd(&grid[i + 1u], toFix(mom.y));
    atomicAdd(&grid[i + 2u], toFix(mom.z)); atomicAdd(&grid[i + 3u], toFix(wt));
  }}}
}`,
  // density -> Tait pressure + viscous stress -> momentum
  p2g2: `
@group(0) @binding(1) var<storage, read> ps: array<Particle>;
@group(0) @binding(2) var<storage, read_write> grid: array<atomic<i32>>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  if (g.x >= U.rng.y) { return; }
  let p = ps[U.rng.x + g.x];
  if (p.pos.w <= 0.0) { return; }
  let x = (p.pos.xyz - U.origin.xyz) / U.origin.w;
  let cell = vec3<i32>(floor(x));
  if (!stencilOk(cell)) { return; }
  var w = qweights(x - vec3f(cell) - 0.5);
  var density = 0.0;
  for (var gx = 0; gx < 3; gx++) { for (var gy = 0; gy < 3; gy++) { for (var gz = 0; gz < 3; gz++) {
    let wt = w[gx].x * w[gy].y * w[gz].z;
    density += f32(atomicLoad(&grid[gIdx(cell + vec3<i32>(gx - 1, gy - 1, gz - 1)) * 4u + 3u])) / FIX * wt;
  }}}
  // isolated spray drops have ~0 density -> huge volume -> exploding stress. Clamp to 1.5x rest volume.
  let volume = min(1.0 / max(density, 1e-3), 1.5 / U.misc.w);
  let rr = clamp(density / U.misc.w, 0.0, 1.35);
  let r2 = rr * rr; let r7 = r2 * r2 * r2 * rr;
  let pressure = max(0.0, U.misc.z * (r7 - 1.0));
  let C = mat3x3f(p.c0.xyz, p.c1.xyz, p.c2.xyz);
  let stress = mat3x3f(-pressure, 0.0, 0.0, 0.0, -pressure, 0.0, 0.0, 0.0, -pressure) + U.misc2.x * (C + transpose(C));
  let term = -volume * 4.0 * U.misc.x * stress;
  for (var gx = 0; gx < 3; gx++) { for (var gy = 0; gy < 3; gy++) { for (var gz = 0; gz < 3; gz++) {
    let wt = w[gx].x * w[gy].y * w[gz].z;
    let cx = cell + vec3<i32>(gx - 1, gy - 1, gz - 1);
    let mom = wt * (term * (vec3f(cx) - x + 0.5));
    let i = gIdx(cx) * 4u;
    atomicAdd(&grid[i], toFix(mom.x)); atomicAdd(&grid[i + 1u], toFix(mom.y)); atomicAdd(&grid[i + 2u], toFix(mom.z));
  }}}
}`,
  grid: `
@group(0) @binding(1) var<storage, read_write> grid: array<vec4<i32>>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  if (g.x >= arrayLength(&grid)) { return; }
  let raw = grid[g.x];
  let m = f32(raw.w) / FIX;
  if (m <= 1e-6) { grid[g.x] = vec4<i32>(0); return; }
  let d = U.dims.xyz; let h = U.origin.w;
  let c = vec3<i32>(i32(g.x) % d.x, (i32(g.x) / d.x) % d.y, i32(g.x) / (d.x * d.y));
  var v = vec3f(raw.xyz) / FIX / m;
  v.y += U.misc.y * U.misc.x;
  let wp = U.origin.xyz + (vec3f(c) + 0.5) * h;
  // dented terrain: free-slip (no flow into the ground)
  if (wp.y < groundAt(wp.xz) + h * 0.5) {
    let n = groundN(wp.xz);
    let vn = dot(v, n);
    if (vn < 0.0) { v -= vn * n; v *= 0.995; }
  }
  // domain walls
  if (c.x < 2 || c.x > d.x - 3) { v.x = 0.0; }
  if (c.z < 2 || c.z > d.z - 3) { v.z = 0.0; }
  if (c.y > d.y - 3 && v.y > 0.0) { v.y = 0.0; }
  // spinning tyres and the car body are moving solids: the water takes their velocity
  let ty = tyreAt(wp, h * 0.5);
  if (ty.w > 0.5) { v = ty.xyz / h; }
  let lp = carLocal(wp);
  if (all(abs(lp) < U.carH.xyz + vec3f(h * 0.5))) { v = carVelAt(wp) / h; }
  grid[g.x] = bitcast<vec4<i32>>(vec4f(v, m));
}`,
  g2p: `
@group(0) @binding(1) var<storage, read_write> ps: array<Particle>;
@group(0) @binding(2) var<storage, read> grid: array<vec4f>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  if (g.x >= U.rng.y) { return; }
  let id = U.rng.x + g.x;
  var p = ps[id];
  if (p.pos.w <= 0.0) { return; }
  let h = U.origin.w; let dt = U.misc.x;
  let x = (p.pos.xyz - U.origin.xyz) / h;
  let cell = vec3<i32>(floor(x));
  var vel = p.vel.xyz;
  if (stencilOk(cell)) {
    var w = qweights(x - vec3f(cell) - 0.5);
    var v = vec3f(0.0);
    var B = mat3x3f(vec3f(0.0), vec3f(0.0), vec3f(0.0));
    for (var gx = 0; gx < 3; gx++) { for (var gy = 0; gy < 3; gy++) { for (var gz = 0; gz < 3; gz++) {
      let wt = w[gx].x * w[gy].y * w[gz].z;
      let cx = cell + vec3<i32>(gx - 1, gy - 1, gz - 1);
      let dist = vec3f(cx) - x + 0.5;
      let wv = wt * grid[gIdx(cx)].xyz;
      v += wv;
      B += mat3x3f(wv * dist.x, wv * dist.y, wv * dist.z);
    }}}
    let C = B * 4.0;
    p.c0 = vec4f(C[0], 0.0); p.c1 = vec4f(C[1], 0.0); p.c2 = vec4f(C[2], 0.0);
    vel = v * h;
  } else {
    vel.y -= 9.81 * dt;                                  // outside the live grid: ballistic
    p.c0 = vec4f(0.0); p.c1 = vec4f(0.0); p.c2 = vec4f(0.0);
  }
  let sp = length(vel);
  if (sp > 14.0) { vel *= 14.0 / sp; }
  var pos = p.pos.xyz + vel * dt;
  // terrain
  let gy = groundAt(pos.xz) + 0.012;
  if (pos.y < gy) {
    pos.y = gy;
    let n = groundN(pos.xz); let vn = dot(vel, n);
    if (vn < 0.0) { vel -= vn * n; }
  }
  // tyres: push out radially, carry the tread velocity
  for (var w = 0; w < 4; w++) {
    let c = U.wC[w].xyz; let ax = U.wA[w].xyz;
    let q = pos - c; let al = dot(q, ax); let rad = q - ax * al; let rl = length(rad);
    if (abs(al) < U.misc2.z && rl < U.misc2.w && rl > 1e-4) {
      pos = c + ax * al + rad / rl * U.misc2.w;
      let tv = carVelAt(pos) + treadVel(ax, U.wC[w].w, rad / rl * U.misc2.w);
      var rel = vel - tv; let n = rad / rl; let vn = dot(rel, n);
      if (vn < 0.0) { rel -= vn * n; }
      vel = tv + rel * 0.6;
    }
  }
  // car body box
  let lp = carLocal(pos); let he = U.carH.xyz;
  if (all(abs(lp) < he)) {
    let pen = he - abs(lp);
    var lo = lp;
    if (pen.x < pen.y && pen.x < pen.z) { lo.x = sign(lp.x) * he.x; } else if (pen.y < pen.z) { lo.y = sign(lp.y) * he.y; } else { lo.z = sign(lp.z) * he.z; }
    let hh = U.carC.w;
    pos = U.carC.xyz + vec3f(lo.x * cos(hh) + lo.z * sin(hh), lo.y, -lo.x * sin(hh) + lo.z * cos(hh));
    let nl = sign(lo - lp) * vec3f(select(0.0, 1.0, lo.x != lp.x), select(0.0, 1.0, lo.y != lp.y), select(0.0, 1.0, lo.z != lp.z));
    let n = vec3f(nl.x * cos(hh) + nl.z * sin(hh), nl.y, -nl.x * sin(hh) + nl.z * cos(hh));
    let cv = carVelAt(pos);
    var rel = vel - cv; let vn = dot(rel, n);
    if (vn < 0.0) { rel -= 1.2 * vn * n; }
    vel = cv + rel * 0.8;                                   // splashes bounce/slide off the panels
  }
  // water thrown onto dry sand soaks in (life drains); in the puddle it stays alive
  var life = p.pos.w;
  if (groundAt(pos.xz) > -0.01 && pos.y < 0.05) { life -= dt * 2.5; } else if (pos.y > 0.3) { life -= dt * 0.25; } else { life = min(1.0, life + dt); }
  p.pos = vec4f(pos, life);
  p.vel = vec4f(vel, p.vel.w);
  ps[id] = p;
}`,
};

// ---------------------------------------------------------------- rendering (screen-space fluid)
const R_COMMON = /* wgsl */`
struct Particle { pos: vec4f, vel: vec4f, c0: vec4f, c1: vec4f, c2: vec4f };
struct Cam { viewProj: mat4x4f, invViewProj: mat4x4f, camPos: vec4f, lightDir: vec4f, screen: vec4f };
struct RP { pud: array<vec4f, 3>, misc: vec4f };   // misc: radius, blur dir x, blur dir y, -
@group(0) @binding(0) var<uniform> cam: Cam;
@group(0) @binding(1) var<uniform> R: RP;
fn camFwd() -> vec3f {
  let a = cam.invViewProj * vec4f(0.0, 0.0, 0.0, 1.0); let b = cam.invViewProj * vec4f(0.0, 0.0, 1.0, 1.0);
  return normalize(b.xyz / b.w - a.xyz / a.w);
}
fn sky(d: vec3f) -> vec3f { return mix(vec3f(0.78, 0.8, 0.82), vec3f(0.42, 0.6, 0.86), clamp(d.y * 1.6, 0.0, 1.0)); }
`;

const SPHERES = R_COMMON + /* wgsl */`
@group(0) @binding(2) var<storage, read> ps: array<Particle>;
struct VO { @builtin(position) pos: vec4f, @location(0) uv: vec2f, @location(1) c: vec3f, @location(2) mud: f32, @location(3) r: f32 };
@vertex fn vs(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VO {
  var o: VO;
  let p = ps[ii];
  if (p.pos.w <= 0.0) { o.pos = vec4f(2.0, 2.0, 2.0, 1.0); return o; }
  var corners = array<vec2f, 6>(vec2f(-1,-1), vec2f(1,-1), vec2f(1,1), vec2f(-1,-1), vec2f(1,1), vec2f(-1,1));
  let q = corners[vi];
  // fast-moving spray is drawn as smaller droplets; soaking drops shrink
  let r = R.misc.x * clamp(p.pos.w * 2.0, 0.3, 1.0) * mix(1.0, 0.35, smoothstep(1.2, 4.0, length(p.vel.xyz)));
  let toCam = normalize(cam.camPos.xyz - p.pos.xyz);
  let right = normalize(cross(vec3f(0.0, 1.0, 0.0), toCam));
  let up = cross(toCam, right);
  o.pos = cam.viewProj * vec4f(p.pos.xyz + (right * q.x + up * q.y) * r, 1.0);
  o.uv = q; o.c = p.pos.xyz; o.r = r;
  o.mud = R.pud[min(u32(p.vel.w), 2u)].w;
  return o;
}
fn sphere(i: VO) -> vec4f {                       // world pos on the sphere + nz
  let r2 = dot(i.uv, i.uv);
  if (r2 > 1.0) { discard; }
  let nz = sqrt(1.0 - r2);
  let toCam = normalize(cam.camPos.xyz - i.c);
  let right = normalize(cross(vec3f(0.0, 1.0, 0.0), toCam));
  let up = cross(toCam, right);
  return vec4f(i.c + (right * i.uv.x + up * i.uv.y + toCam * nz) * i.r, nz);
}
struct DO { @location(0) d: vec4f, @builtin(frag_depth) z: f32 };
@fragment fn fsDepth(i: VO) -> DO {
  let s = sphere(i);
  let clip = cam.viewProj * vec4f(s.xyz, 1.0);
  var o: DO;
  o.d = vec4f(dot(s.xyz - cam.camPos.xyz, camFwd()), 0.0, 0.0, 1.0);   // linear eye depth
  o.z = clip.z / clip.w;
  return o;
}
@fragment fn fsThick(i: VO) -> @location(0) vec4f {
  let s = sphere(i);
  let t = s.w * 2.0 * i.r * 0.33;
  return vec4f(t, t * i.mud, 0.0, 1.0);
}`;

const FULLSCREEN = /* wgsl */`
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
  var p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
  return vec4f(p[i], 0.0, 1.0);
}`;

// separable narrow-range filter: ignores samples behind the surface, clamps samples in front of it
const BLUR = R_COMMON + FULLSCREEN + /* wgsl */`
@group(0) @binding(2) var src: texture_2d<f32>;
@fragment fn fs(@builtin(position) fp: vec4f) -> @location(0) vec4f {
  let p = vec2<i32>(fp.xy);
  let dc = textureLoad(src, p, 0).r;
  if (dc <= 0.0) { return vec4f(0.0); }
  let dims = vec2<i32>(textureDimensions(src));
  let rpx = R.misc.x * 4.5 * cam.screen.y / (2.0 * tan(0.5) * dc);    // filter radius in pixels
  let kr = clamp(i32(rpx), 2, 28);
  let sigma = f32(kr) * 0.5;
  let thr = R.misc.x * 3.0;
  let dir = vec2<i32>(R.misc.yz);
  var sum = 0.0; var wsum = 0.0;
  for (var k = -kr; k <= kr; k++) {
    let q = clamp(p + dir * k, vec2<i32>(0), dims - vec2<i32>(1));
    var ds = textureLoad(src, q, 0).r;
    if (ds <= 0.0 || ds > dc + thr) { continue; }
    ds = max(ds, dc - thr);
    let w = exp(-f32(k * k) / (2.0 * sigma * sigma));
    sum += ds * w; wsum += w;
  }
  return vec4f(sum / max(wsum, 1e-5), 0.0, 0.0, 1.0);
}`;

const COMPOSITE = R_COMMON + FULLSCREEN + /* wgsl */`
@group(0) @binding(2) var depthSm: texture_2d<f32>;
@group(0) @binding(3) var thick: texture_2d<f32>;
@group(0) @binding(4) var sceneDepth: texture_depth_2d;
fn worldAt(p: vec2<i32>, d: f32) -> vec3f {
  let ndc = vec2f((f32(p.x) + 0.5) / cam.screen.x * 2.0 - 1.0, 1.0 - (f32(p.y) + 0.5) / cam.screen.y * 2.0);
  let far = cam.invViewProj * vec4f(ndc, 1.0, 1.0);
  let dir = normalize(far.xyz / far.w - cam.camPos.xyz);
  return cam.camPos.xyz + dir * d / dot(dir, camFwd());
}
@fragment fn fs(@builtin(position) fp: vec4f) -> @location(0) vec4f {
  let p = vec2<i32>(fp.xy);
  let d = textureLoad(depthSm, p, 0).r;
  if (d <= 0.0) { discard; }
  let wp = worldAt(p, d);
  let clip = cam.viewProj * vec4f(wp, 1.0);
  if (clip.z / clip.w > textureLoad(sceneDepth, p, 0) + 1e-6) { discard; }   // hidden behind the car etc.
  // normal from the smoothed depth (pick the smaller one-sided difference to keep edges sharp)
  let dims = vec2<i32>(textureDimensions(depthSm));
  var nb: array<vec3f, 4>;
  var offs = array<vec2<i32>, 4>(vec2<i32>(1, 0), vec2<i32>(-1, 0), vec2<i32>(0, 1), vec2<i32>(0, -1));
  for (var k = 0; k < 4; k++) {
    let q = clamp(p + offs[k], vec2<i32>(0), dims - vec2<i32>(1));
    let dq = textureLoad(depthSm, q, 0).r;
    nb[k] = select(wp, worldAt(q, dq), dq > 0.0 && abs(dq - d) < 0.3);
  }
  var ddx = nb[0] - wp; if (length(wp - nb[1]) < length(ddx) && length(wp - nb[1]) > 0.0) { ddx = wp - nb[1]; }
  var ddy = nb[2] - wp; if (length(wp - nb[3]) < length(ddy) && length(wp - nb[3]) > 0.0) { ddy = wp - nb[3]; }
  let V = normalize(cam.camPos.xyz - wp);
  var n = normalize(cross(ddx, ddy));
  if (length(cross(ddx, ddy)) < 1e-9) { n = vec3f(0.0, 1.0, 0.0); }
  if (dot(n, V) < 0.0) { n = -n; }
  let th = textureLoad(thick, p, 0);
  let t = th.r;
  // gravity keeps a puddle surface level: damp particle-scale bumps on mostly-upward normals,
  // and flatten thin edges so they don't turn into sky-coloured mirrors
  let up = vec3f(0.0, 1.0, 0.0);
  n = normalize(mix(n, up, smoothstep(0.2, 0.9, n.y) * 0.9));
  n = normalize(mix(up, n, smoothstep(0.004, 0.05, t))); let mud = clamp(th.g / max(t, 1e-4), 0.0, 1.0);
  let L = normalize(cam.lightDir.xyz);
  let cosV = max(dot(n, V), 0.0);
  let fres = 0.02 + 0.98 * pow(1.0 - cosV, 5.0);
  let Rv = reflect(-V, n);
  let spec = pow(max(dot(Rv, L), 0.0), mix(500.0, 90.0, mud)) * mix(8.0, 1.2, mud);
  let diff = max(dot(n, L), 0.0);
  // Beer-Lambert absorption through the fluid thickness
  let sigma = mix(vec3f(4.0, 1.6, 1.1), vec3f(30.0, 36.0, 45.0), mud);
  let T = exp(-sigma * t);
  let absorbA = clamp(1.0 - (T.x + T.y + T.z) / 3.0, 0.0, 1.0);
  let inscatter = mix(vec3f(0.10, 0.19, 0.2) * (0.6 + 0.6 * diff), vec3f(0.33, 0.24, 0.15) * (0.45 + 0.75 * diff), mud);
  var col = inscatter * absorbA;
  var a = absorbA;
  let refl = (sky(Rv) + vec3f(1.0, 0.95, 0.85) * spec) * fres * mix(1.0, 0.55, mud);
  col = col * (1.0 - fres) + refl; a = a + fres * (1.0 - a);
  let edge = smoothstep(0.0, R.misc.x * 0.2, t);
  col *= edge; a *= edge;
  let fog = 1.0 - exp(-distance(cam.camPos.xyz, wp) * 0.006);
  col = mix(col, vec3f(0.78, 0.8, 0.82) * a, fog);
  return vec4f(col, a);
}`;

// dented puddle bed (the flat ground is cut away inside each shoreline by scene.js)
const BED = /* wgsl */`
struct Cam { viewProj: mat4x4f, invViewProj: mat4x4f, camPos: vec4f, lightDir: vec4f, screen: vec4f };
struct RP { pud: array<vec4f, 3>, misc: vec4f };
@group(0) @binding(0) var<uniform> cam: Cam;
@group(0) @binding(1) var<uniform> U: RP;
${TERRAIN}
const NB: i32 = 128;
fn h2(p: vec2f) -> f32 { return fract(sin(dot(p, vec2f(127.1, 311.7))) * 43758.5453); }
fn vn2(p: vec2f) -> f32 {
  let i = floor(p); let f = fract(p); let u = f * f * (3.0 - 2.0 * f);
  return mix(mix(h2(i), h2(i + vec2f(1, 0)), u.x), mix(h2(i + vec2f(0, 1)), h2(i + vec2f(1, 1)), u.x), u.y);
}
struct VO { @builtin(position) pos: vec4f, @location(0) wp: vec3f, @location(1) n: vec3f, @location(2) info: vec2f };
@vertex fn vs(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VO {
  let i = i32(ii);
  let quad = i32(vi / 6u); let corner = vi % 6u;
  var offs = array<vec2<i32>, 6>(vec2<i32>(0,0), vec2<i32>(1,0), vec2<i32>(1,1), vec2<i32>(0,0), vec2<i32>(1,1), vec2<i32>(0,1));
  let c = vec2<i32>(quad % (NB - 1), quad / (NB - 1)) + offs[corner];
  let e = U.pud[i].z * 1.3;
  let p = U.pud[i].xy + (vec2f(c) / f32(NB - 1) * 2.0 - 1.0) * e;
  let b = bedAt(i, p);
  var o: VO;
  o.wp = vec3f(p.x, b, p.y);
  o.pos = cam.viewProj * vec4f(o.wp, 1.0);
  o.n = groundN(p);
  o.info = vec2f(dnAt(i, p), U.pud[i].w);
  return o;
}
@fragment fn fs(v: VO) -> @location(0) vec4f {
  if (v.info.x > 1.0) { discard; }
  let L = normalize(cam.lightDir.xyz);
  let p = v.wp.xz;
  let nz = vn2(p * 1.7) * 0.6 + vn2(p * 9.0) * 0.4;
  var base = mix(vec3f(0.74, 0.58, 0.38), vec3f(0.86, 0.72, 0.50), nz * 0.8);
  let wet = smoothstep(${WATER_LEVEL} + 0.04, ${WATER_LEVEL}, v.wp.y);
  base *= mix(0.62, 0.42, wet);
  base = mix(base, vec3f(0.22, 0.16, 0.1) * (0.8 + 0.4 * nz), v.info.y * smoothstep(0.95, 0.6, v.info.x));
  let n = normalize(v.n + vec3f(vn2(p * 6.0) - 0.5, 0.0, vn2(p * 6.0 + 7.0) - 0.5) * 0.15);
  let diff = max(dot(n, L), 0.0);
  let hemi = mix(vec3f(0.35, 0.3, 0.25), vec3f(0.55, 0.65, 0.8), n.y * 0.5 + 0.5);
  var c = base * (hemi * 0.75 + vec3f(1.0, 0.94, 0.85) * diff * 0.95);
  let fog = 1.0 - exp(-distance(cam.camPos.xyz, v.wp) * 0.006);
  return vec4f(mix(c, vec3f(0.78, 0.8, 0.82), fog), 1.0);
}`;

export class Water {
  constructor(device, format, checkModule, puddles, camUbo) {
    this.device = device; this.format = format; this.puddles = puddles; this.camUbo = camUbo;
    // ---- fill each dent with a particle lattice up to the still-water level
    const parts = [], ranges = [];
    const jit = (() => { let s = 12345; return () => ((s = (s * 16807) % 2147483647) / 2147483647 - 0.5) * SPACING * 0.3; })();
    puddles.slice(0, 3).forEach((p, i) => {
      const start = parts.length / STRIDE, e = p.r * 1.3;
      for (let x = -e; x < e; x += SPACING) for (let z = -e; z < e; z += SPACING) {
        const dn = Math.hypot(x, z) / shoreR(p, Math.atan2(z, x));
        if (dn >= 1) continue;
        for (let y = bedN(dn) + SPACING * 0.5; y < WATER_LEVEL; y += SPACING) {
          parts.push(p.x + x + jit(), y, p.z + z + jit(), 1, 0, 0, 0, i, ...new Array(12).fill(0));
        }
      }
      ranges.push([start, parts.length / STRIDE - start]);
    });
    this.count = parts.length / STRIDE;
    this.ranges = ranges;
    this.initData = new Float32Array(parts);
    const st = GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST;
    this.pbuf = device.createBuffer({ size: Math.max(16, this.initData.byteLength), usage: st, label: 'waterParticles' });
    device.queue.writeBuffer(this.pbuf, 0, this.initData);
    this.cells = GRID[0] * GRID[1] * GRID[2];
    this.gbuf = device.createBuffer({ size: this.cells * 16, usage: GPUBufferUsage.STORAGE, label: 'waterGrid' });
    this.bytes = this.initData.byteLength + this.cells * 16;
    this.u = new Float32Array(36 * 4); this.uI = new Int32Array(this.u.buffer); this.uU = new Uint32Array(this.u.buffer);
    this.ubo = device.createBuffer({ size: this.u.byteLength, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, label: 'waterSim' });
    this.active = -1; this.origin = [0, ORIGIN_Y, 0];

    this.pipe = {}; this.bg = {};
    const binds = { clear: [this.gbuf], p2g1: [this.pbuf, this.gbuf], p2g2: [this.pbuf, this.gbuf], grid: [this.gbuf], g2p: [this.pbuf, this.gbuf] };
    for (const [k, src] of Object.entries(SIM)) {
      const label = 'water-' + k;
      const module = checkModule(device.createShaderModule({ code: SIM_COMMON + src, label }), label);
      this.pipe[k] = device.createComputePipeline({ layout: 'auto', compute: { module, entryPoint: 'main' }, label });
      this.bg[k] = device.createBindGroup({ layout: this.pipe[k].getBindGroupLayout(0), entries: [this.ubo, ...binds[k]].map((b, i) => ({ binding: i, resource: { buffer: b } })) });
    }

    // ---- render resources
    const mkU = () => device.createBuffer({ size: 64, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    this.rU = mkU(); this.rUH = mkU(); this.rUV = mkU();
    const rp = new Float32Array(16);
    puddles.slice(0, 3).forEach((p, i) => rp.set([p.x, p.z, p.r, p.mud], i * 4));
    rp.set([P_RADIUS, 0, 0, 0], 12); device.queue.writeBuffer(this.rU, 0, rp);
    rp.set([P_RADIUS, 1, 0, 0], 12); device.queue.writeBuffer(this.rUH, 0, rp);
    rp.set([P_RADIUS, 0, 1, 0], 12); device.queue.writeBuffer(this.rUV, 0, rp);
    const mod = (code, label) => checkModule(device.createShaderModule({ code, label }), label);
    const sph = mod(SPHERES, 'water-spheres');
    this.depthPipe = device.createRenderPipeline({
      layout: 'auto', label: 'water-depth',
      vertex: { module: sph, entryPoint: 'vs' },
      fragment: { module: sph, entryPoint: 'fsDepth', targets: [{ format: 'r32float' }] },
      primitive: { topology: 'triangle-list' },
      depthStencil: { format: 'depth32float', depthWriteEnabled: true, depthCompare: 'less' },
    });
    this.thickPipe = device.createRenderPipeline({
      layout: 'auto', label: 'water-thick',
      vertex: { module: sph, entryPoint: 'vs' },
      fragment: { module: sph, entryPoint: 'fsThick', targets: [{ format: 'rg16float', blend: { color: { srcFactor: 'one', dstFactor: 'one', operation: 'add' }, alpha: { srcFactor: 'one', dstFactor: 'one', operation: 'add' } } }] },
      primitive: { topology: 'triangle-list' },
      depthStencil: { format: 'depth32float', depthWriteEnabled: false, depthCompare: 'less' },
    });
    const FS = GPUShaderStage.FRAGMENT, VF = GPUShaderStage.FRAGMENT | GPUShaderStage.VERTEX;
    const ubE = (b) => ({ binding: b, visibility: VF, buffer: { type: 'uniform' } });
    const txE = (b, sampleType) => ({ binding: b, visibility: FS, texture: { sampleType } });
    this.blurBGL = device.createBindGroupLayout({ entries: [ubE(0), ubE(1), txE(2, 'unfilterable-float')] });
    this.compBGL = device.createBindGroupLayout({ entries: [ubE(0), ubE(1), txE(2, 'unfilterable-float'), txE(3, 'unfilterable-float'), txE(4, 'depth')] });
    const blur = mod(BLUR, 'water-blur');
    this.blurPipe = device.createRenderPipeline({ layout: device.createPipelineLayout({ bindGroupLayouts: [this.blurBGL] }), label: 'water-blur', vertex: { module: blur, entryPoint: 'vs' }, fragment: { module: blur, entryPoint: 'fs', targets: [{ format: 'r32float' }] }, primitive: { topology: 'triangle-list' } });
    const comp = mod(COMPOSITE, 'water-composite');
    const pre = { color: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' }, alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' } };
    this.compPipe = device.createRenderPipeline({ layout: device.createPipelineLayout({ bindGroupLayouts: [this.compBGL] }), label: 'water-composite', vertex: { module: comp, entryPoint: 'vs' }, fragment: { module: comp, entryPoint: 'fs', targets: [{ format, blend: pre }] }, primitive: { topology: 'triangle-list' } });
    const bedM = mod(BED, 'water-bed');
    this.bedPipe = device.createRenderPipeline({
      layout: 'auto', label: 'water-bed',
      vertex: { module: bedM, entryPoint: 'vs' }, fragment: { module: bedM, entryPoint: 'fs', targets: [{ format }] },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
      depthStencil: { format: 'depth32float', depthWriteEnabled: true, depthCompare: 'less' },
    });
    this.bedBG = device.createBindGroup({ layout: this.bedPipe.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: camUbo } }, { binding: 1, resource: { buffer: this.rU } }] });
    this.sphBG = (pipe) => device.createBindGroup({ layout: pipe.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: camUbo } }, { binding: 1, resource: { buffer: this.rU } }, { binding: 2, resource: { buffer: this.pbuf } }] });
    this.depthBG = this.sphBG(this.depthPipe);
    this.thickBG = this.sphBG(this.thickPipe);
  }

  // (re)create screen-size targets; sceneDepth = the main depth texture
  resize(w, h, sceneDepth) {
    for (const t of [this.zTex, this.dA, this.dB, this.thTex]) t?.destroy();
    const D = this.device, RA = GPUTextureUsage.RENDER_ATTACHMENT, TB = GPUTextureUsage.TEXTURE_BINDING;
    this.zTex = D.createTexture({ size: [w, h], format: 'depth32float', usage: RA });
    this.dA = D.createTexture({ size: [w, h], format: 'r32float', usage: RA | TB });
    this.dB = D.createTexture({ size: [w, h], format: 'r32float', usage: RA | TB });
    this.thTex = D.createTexture({ size: [w, h], format: 'rg16float', usage: RA | TB });
    this.sceneDepthView = sceneDepth.createView();
    const blurBG = (u, src) => D.createBindGroup({ layout: this.blurBGL, entries: [{ binding: 0, resource: { buffer: this.camUbo } }, { binding: 1, resource: { buffer: u } }, { binding: 2, resource: src.createView() }] });
    this.blurH = blurBG(this.rUH, this.dA);
    this.blurV = blurBG(this.rUV, this.dB);
    this.compBG = D.createBindGroup({ layout: this.compBGL, entries: [
      { binding: 0, resource: { buffer: this.camUbo } }, { binding: 1, resource: { buffer: this.rU } },
      { binding: 2, resource: this.dA.createView() }, { binding: 3, resource: this.thTex.createView() }, { binding: 4, resource: this.sceneDepthView }] });
  }

  update(dt, car, wet, S) {
    // live-simulate the puddle nearest the car; switch only once the car is well away from the current one
    let best = -1, bd = 1e9;
    this.puddles.slice(0, 3).forEach((p, i) => { const d = Math.hypot(p.x - car.x, p.z - car.z) - p.r; if (d < bd) { bd = d; best = i; } });
    if (this.active < 0) this.active = best;
    else if (best !== this.active) {
      const cur = this.puddles[this.active];
      if (Math.hypot(cur.x - car.x, cur.z - car.z) - cur.r > 12) this.active = best;
    }
    const P = this.puddles[this.active];
    const ox = P.x - (GRID[0] * H) / 2, oz = P.z - (GRID[2] * H) / 2;
    this.origin = [ox, ORIGIN_Y, oz];
    const u = this.u; u.fill(0);
    this.uI.set([GRID[0], GRID[1], GRID[2], 0], 0);
    const [start, count] = this.ranges[this.active] || [0, 0];
    this.uU.set([start, count, 0, 0], 4);
    this.simCount = count;
    const K = (SOUND * SOUND * RHO0) / 7;
    const visc = (P.mud ? 8 : 1.2) * (S.waterVisc ?? 1);
    u.set([ox, ORIGIN_Y, oz, H], 8);
    u.set([dt / SUB, -9.81 / H, K, RHO0], 12);
    u.set([visc, (S.tyrePush ?? 1) * (P.mud ? 0.35 : 0.6), car.wheelW * 0.5, car.wheelR], 16);
    const bodyOff = car.bodyOff || 0;
    u.set([car.x, car.bodyY + bodyOff, car.z, car.heading], 20);
    u.set([car.halfExt[0], car.halfExt[1], car.halfExt[2], 0], 24);
    u.set([car.vx, 0, car.vz, car.w], 28);
    this.puddles.slice(0, 3).forEach((p, i) => u.set([p.x, p.z, p.r, p.mud], 32 + i * 4));
    car.wheels.forEach((wh, i) => {
      const yaw = car.heading + (wh.front ? car.steer : 0);
      u.set([wh.pos[0], car.wheelR + (wh.gy || 0), wh.pos[2], wh.spinVel], 44 + i * 4);
      u.set([Math.cos(yaw), 0, -Math.sin(yaw), 0], 60 + i * 4);
    });
    this.device.queue.writeBuffer(this.ubo, 0, u);
    void wet;
  }

  encode(enc) {
    if (!this.simCount) return;
    const p = enc.beginComputePass({ label: 'water-mpm' });
    const gw = Math.ceil(this.cells / 64), pw = Math.ceil(this.simCount / 64);
    const run = (k, n) => { p.setPipeline(this.pipe[k]); p.setBindGroup(0, this.bg[k]); p.dispatchWorkgroups(n); };
    for (let s = 0; s < SUB; s++) { run('clear', gw); run('p2g1', pw); run('p2g2', pw); run('grid', gw); run('g2p', pw); }
    p.end();
  }

  drawOpaque(pass) {
    pass.setPipeline(this.bedPipe); pass.setBindGroup(0, this.bedBG);
    pass.draw(127 * 127 * 6, Math.min(3, this.puddles.length));
  }

  // screen-space fluid: call after the opaque scene pass has ended
  render(enc, colorView) {
    if (!this.dA || !this.count) return;
    let pass = enc.beginRenderPass({
      colorAttachments: [{ view: this.dA.createView(), loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 0 } }],
      depthStencilAttachment: { view: this.zTex.createView(), depthClearValue: 1, depthLoadOp: 'clear', depthStoreOp: 'discard' },
    });
    pass.setPipeline(this.depthPipe); pass.setBindGroup(0, this.depthBG); pass.draw(6, this.count); pass.end();
    pass = enc.beginRenderPass({
      colorAttachments: [{ view: this.thTex.createView(), loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 0 } }],
      depthStencilAttachment: { view: this.sceneDepthView, depthReadOnly: true },
    });
    pass.setPipeline(this.thickPipe); pass.setBindGroup(0, this.thickBG); pass.draw(6, this.count); pass.end();
    for (let it = 0; it < 3; it++) {
      for (const [bg, dst] of [[this.blurH, this.dB], [this.blurV, this.dA]]) {
        pass = enc.beginRenderPass({ colorAttachments: [{ view: dst.createView(), loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 0 } }] });
        pass.setPipeline(this.blurPipe); pass.setBindGroup(0, bg); pass.draw(3); pass.end();
      }
    }
    pass = enc.beginRenderPass({ colorAttachments: [{ view: colorView, loadOp: 'load', storeOp: 'store' }] });
    pass.setPipeline(this.compPipe); pass.setBindGroup(0, this.compBG); pass.draw(3); pass.end();
  }
}

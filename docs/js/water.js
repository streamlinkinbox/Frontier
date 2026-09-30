// Puddles (GTX): real 3D liquid.
//  * Terrain is dented into bowls; each bowl is filled with water PARTICLES.
//  * Simulation: PIC/FLIP (Zhu & Bridson 2005) on a MAC grid, INCOMPRESSIBLE: every substep the
//    particle velocities are splatted to cell faces, a Jacobi pressure solve removes divergence (plus a
//    density drift correction that keeps the volume), and particles take the velocity change back
//    (95% FLIP / 5% PIC; mud more PIC = damped). The puddle near the car is live (160x24x160 @ 8 cm);
//    the others rest.
//  * Collisions: dented terrain (free-slip), spinning tyres (moving no-slip cylinders), car body box.
//  * Rendering: Screen-Space Fluid Rendering (GDC 2010): particle spheres -> eye-depth + thickness
//    buffers -> separable narrow-range depth filter (Truong & Yuksel 2018) -> normals from the smoothed
//    depth + blurred thickness -> Fresnel reflection, sun glints, Beer-Lambert absorption by thickness (clear vs mud).

//  * Diffuse material (Ihmsen, Akinci, Akinci, Teschner 2012 'Unified spray, foam and bubbles'):
//    fluid particles with high kinetic energy spawn secondary particles where air is trapped (FLIP vs grid
//    velocity difference) or on wave crests (rising surface particles). Each frame they are classified by
//    local fluid density: spray (ballistic + drag), foam (rides the surface velocity, decays), bubbles
//    (buoyant, dragged by the flow). Rendered as lit soft sprites over the fluid.

export const BED_D = 0.26;            // dent depth at the centre (m)
export const WATER_LEVEL = -0.04;     // still-water level (just below the rim)
const H = 0.08;                       // FLIP cell size (m)
const GRID = [160, 24, 160];          // 12.8 x 1.92 x 12.8 m around the active puddle
const ORIGIN_Y = -0.32;
const SPACING = H / 2;                // 8 particles per cell at rest (4 cm)
const PPC = 8.0;
const SUB = 2;                        // substeps per frame
const JACOBI = 30;                    // pressure iterations per substep (warm-started)
const FIX = 65536.0;
const P_RADIUS = 0.06;               // render sphere radius (m); overlaps neighbours for a continuous surface
const DIFF_N = 65536;                 // spray/foam/bubble particles (Ihmsen et al. 2012)
const STRIDE = 8;                     // floats per particle (pos+life, vel+home)

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
struct Particle { pos: vec4f, vel: vec4f };     // pos + life, vel + home puddle
struct FP {
  dims: vec4<i32>,             // cells nx, ny, nz
  rng: vec4<u32>,              // first particle, count
  origin: vec4f,               // xyz, h
  misc: vec4f,                 // dt, gravity (m/s^2), flip ratio, ppc (rest particles per cell)
  misc2: vec4f,                // density correction, tyre push, wheel half-width, wheel radius
  carC: vec4f, carH: vec4f, carV: vec4f,
  pud: array<vec4f, 3>,        // x, z, r, mud
  wC: array<vec4f, 4>,         // wheel centre xyz, spin (rad/s)
  wA: array<vec4f, 4>,         // wheel axle axis xyz, -
};
@group(0) @binding(0) var<uniform> U: FP;
const FIX: f32 = ${FIX.toFixed(1)};
${TERRAIN}
fn nd() -> vec3<i32> { return U.dims.xyz + vec3<i32>(1); }
fn cIdx(c: vec3<i32>) -> u32 { let d = U.dims.xyz; let q = clamp(c, vec3<i32>(0), d - vec3<i32>(1)); return u32(q.x + d.x * (q.y + d.y * q.z)); }
fn nIdx(c: vec3<i32>) -> u32 { let d = nd(); let q = clamp(c, vec3<i32>(0), d - vec3<i32>(1)); return u32(q.x + d.x * (q.y + d.y * q.z)); }
fn inCells(c: vec3<i32>) -> bool { return all(c >= vec3<i32>(0)) && all(c < U.dims.xyz); }
fn toFix(x: f32) -> i32 { return i32(clamp(x, -30000.0, 30000.0) * FIX); }
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
  return select(t, t * (4.0 / l), l > 4.0);            // a tyre drags at most ~4 m/s of water with it
}
// solid at wp: (velocity xyz, 1) or 0 — dented terrain, spinning tyres, car body
fn solidAt(wp: vec3f) -> vec4f {
  if (wp.y < groundAt(wp.xz)) { return vec4f(0.0, 0.0, 0.0, 1.0); }
  for (var w = 0; w < 4; w++) {
    let c = U.wC[w].xyz; let ax = U.wA[w].xyz;
    let q = wp - c; let al = dot(q, ax); let rad = q - ax * al;
    if (abs(al) < U.misc2.z && length(rad) < U.misc2.w) { return vec4f(carVelAt(wp) + treadVel(ax, U.wC[w].w, rad), 1.0); }
  }
  if (all(abs(carLocal(wp)) < U.carH.xyz)) { return vec4f(carVelAt(wp), 1.0); }
  return vec4f(0.0);
}
// trilinear sample of one staggered component (comp 0=u,1=v,2=w) from a node vec4 grid
fn stagW(g: vec3f, comp: i32) -> vec3f {
  var o = vec3f(0.5); o[comp] = 0.0;
  return g - o;
}
`;

const SIM = {
  clear: `
@group(0) @binding(1) var<storage, read_write> acc: array<i32>;
@group(0) @binding(2) var<storage, read_write> cnt: array<u32>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  let n1 = u32(nd().x * nd().y * nd().z);
  if (g.x < n1) { for (var k = 0u; k < 8u; k++) { acc[g.x * 8u + k] = 0; } }
  if (g.x < arrayLength(&cnt)) { cnt[g.x] = 0u; }
}`,
  // particle -> MAC faces (weighted velocity) + particle count per cell
  p2g: `
@group(0) @binding(1) var<storage, read> ps: array<Particle>;
@group(0) @binding(2) var<storage, read_write> acc: array<atomic<i32>>;
@group(0) @binding(3) var<storage, read_write> cnt: array<atomic<u32>>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) gi: vec3<u32>) {
  if (gi.x >= U.rng.y) { return; }
  let p = ps[U.rng.x + gi.x];
  if (p.pos.w <= 0.0) { return; }
  let g = (p.pos.xyz - U.origin.xyz) / U.origin.w;
  let cell = vec3<i32>(floor(g));
  if (!inCells(cell)) { return; }
  atomicAdd(&cnt[cIdx(cell)], 1u);
  for (var comp = 0; comp < 3; comp++) {
    let s = stagW(g, comp);
    let b = vec3<i32>(floor(s)); let f = s - vec3f(b);
    for (var k = 0; k < 8; k++) {
      let o = vec3<i32>(k & 1, (k >> 1) & 1, (k >> 2) & 1);
      let w = select(1.0 - f.x, f.x, o.x == 1) * select(1.0 - f.y, f.y, o.y == 1) * select(1.0 - f.z, f.z, o.z == 1);
      let n = b + o;
      if (any(n < vec3<i32>(0)) || any(n >= nd())) { continue; }
      let i = nIdx(n) * 8u + u32(comp) * 2u;
      atomicAdd(&acc[i], toFix(p.vel[comp] * w));
      atomicAdd(&acc[i + 1u], toFix(w));
    }
  }
}`,
  // cell types: 0 air, 1 fluid, 2 solid (+ solid velocity)
  mark: `
@group(0) @binding(1) var<storage, read> cnt: array<u32>;
@group(0) @binding(2) var<storage, read_write> ctype: array<vec4f>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  let d = U.dims.xyz;
  if (g.x >= u32(d.x * d.y * d.z)) { return; }
  let c = vec3<i32>(i32(g.x) % d.x, (i32(g.x) / d.x) % d.y, i32(g.x) / (d.x * d.y));
  let wp = U.origin.xyz + (vec3f(c) + 0.5) * U.origin.w;
  let s = solidAt(wp);
  var t = select(0.0, 1.0, cnt[g.x] > 0u);
  if (s.w > 0.5 || c.x == 0 || c.z == 0 || c.x == d.x - 1 || c.z == d.z - 1 || c.y == 0) { t = 2.0; }
  ctype[g.x] = vec4f(s.xyz, t);
}`,
  // faces: weighted average -> velocity (saved as 'old' for FLIP), + gravity, solid faces take solid velocity
  norm: `
@group(0) @binding(1) var<storage, read> acc: array<i32>;
@group(0) @binding(2) var<storage, read> ctype: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> vel: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> velOld: array<vec4f>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  let n = nd();
  if (g.x >= u32(n.x * n.y * n.z)) { return; }
  let c = vec3<i32>(i32(g.x) % n.x, (i32(g.x) / n.x) % n.y, i32(g.x) / (n.x * n.y));
  var v = vec4f(0.0); var old = vec4f(0.0);
  for (var comp = 0; comp < 3; comp++) {
    let wsum = f32(acc[g.x * 8u + u32(comp) * 2u + 1u]) / FIX;
    var u = 0.0;
    if (wsum > 1e-4) { u = f32(acc[g.x * 8u + u32(comp) * 2u]) / FIX / wsum; }
    old[comp] = u;
    if (comp == 1) { u += U.misc.y * U.misc.x; }
    // face between cell c-e and cell c
    var e = vec3<i32>(0); e[comp] = 1;
    let a = c - e;
    let ta = select(2.0, ctype[cIdx(a)].w, inCells(a));
    let tb = select(2.0, ctype[cIdx(c)].w, inCells(c));
    if (ta > 1.5 || tb > 1.5) {
      var sv = 0.0;
      if (ta > 1.5 && inCells(a)) { sv = ctype[cIdx(a)][comp]; }
      if (tb > 1.5 && inCells(c)) { sv = ctype[cIdx(c)][comp]; }
      u = sv;
    }
    v[comp] = u;
  }
  vel[g.x] = v; velOld[g.x] = old;
}`,
  // divergence of fluid cells, with a drift correction that pushes over-packed cells apart (keeps volume)
  div: `
@group(0) @binding(1) var<storage, read> vel: array<vec4f>;
@group(0) @binding(2) var<storage, read> ctype: array<vec4f>;
@group(0) @binding(3) var<storage, read> cnt: array<u32>;
@group(0) @binding(4) var<storage, read_write> dv: array<f32>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  let d = U.dims.xyz;
  if (g.x >= u32(d.x * d.y * d.z)) { return; }
  if (ctype[g.x].w < 0.5 || ctype[g.x].w > 1.5) { dv[g.x] = 0.0; return; }
  let c = vec3<i32>(i32(g.x) % d.x, (i32(g.x) / d.x) % d.y, i32(g.x) / (d.x * d.y));
  let h = U.origin.w;
  let v0 = vel[nIdx(c)];
  let div = (vel[nIdx(c + vec3<i32>(1, 0, 0))].x - v0.x + vel[nIdx(c + vec3<i32>(0, 1, 0))].y - v0.y + vel[nIdx(c + vec3<i32>(0, 0, 1))].z - v0.z) / h;
  let over = max(f32(cnt[g.x]) / U.misc.w - 1.0, 0.0);
  dv[g.x] = div - U.misc2.x * over / U.misc.x;
}`,
  // Jacobi on the scaled pressure q = p*dt/rho:  q = (sum fluid-neighbour q - h^2 div) / (#non-solid neighbours)
  jacobi: `
@group(0) @binding(1) var<storage, read> qIn: array<f32>;
@group(0) @binding(2) var<storage, read_write> qOut: array<f32>;
@group(0) @binding(3) var<storage, read> ctype: array<vec4f>;
@group(0) @binding(4) var<storage, read> dv: array<f32>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  let d = U.dims.xyz;
  if (g.x >= u32(d.x * d.y * d.z)) { return; }
  let t = ctype[g.x].w;
  if (t < 0.5 || t > 1.5) { qOut[g.x] = 0.0; return; }
  let c = vec3<i32>(i32(g.x) % d.x, (i32(g.x) / d.x) % d.y, i32(g.x) / (d.x * d.y));
  var nb = array<vec3<i32>, 6>(vec3<i32>(1,0,0), vec3<i32>(-1,0,0), vec3<i32>(0,1,0), vec3<i32>(0,-1,0), vec3<i32>(0,0,1), vec3<i32>(0,0,-1));
  var sum = 0.0; var ns = 0.0;
  for (var k = 0; k < 6; k++) {
    let q = c + nb[k];
    if (!inCells(q)) { continue; }
    let i = cIdx(q); let tq = ctype[i].w;
    if (tq > 1.5) { continue; }
    ns += 1.0;
    if (tq > 0.5) { sum += qIn[i]; }
  }
  let h = U.origin.w;
  qOut[g.x] = select(0.0, (sum - h * h * dv[g.x]) / ns, ns > 0.0);
}`,
  // subtract the pressure gradient on faces touching fluid (solid faces keep the solid velocity)
  project: `
@group(0) @binding(1) var<storage, read> q: array<f32>;
@group(0) @binding(2) var<storage, read> ctype: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> vel: array<vec4f>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  let n = nd();
  if (g.x >= u32(n.x * n.y * n.z)) { return; }
  let c = vec3<i32>(i32(g.x) % n.x, (i32(g.x) / n.x) % n.y, i32(g.x) / (n.x * n.y));
  var v = vel[g.x];
  let h = U.origin.w;
  for (var comp = 0; comp < 3; comp++) {
    var e = vec3<i32>(0); e[comp] = 1;
    let a = c - e;
    if (!inCells(a) || !inCells(c)) { continue; }
    let ta = ctype[cIdx(a)].w; let tb = ctype[cIdx(c)].w;
    if (ta > 1.5 || tb > 1.5) { continue; }
    if (ta < 0.5 && tb < 0.5) { continue; }
    let qa = select(0.0, q[cIdx(a)], ta > 0.5);
    let qb = select(0.0, q[cIdx(c)], tb > 0.5);
    v[comp] -= (qb - qa) / h;
  }
  vel[g.x] = v;
}`,
  // grid -> particles (FLIP/PIC blend), advect, collide
  g2p: `
@group(0) @binding(1) var<storage, read_write> ps: array<Particle>;
@group(0) @binding(2) var<storage, read> vel: array<vec4f>;
@group(0) @binding(3) var<storage, read> velOld: array<vec4f>;
@group(0) @binding(4) var<storage, read> cnt: array<u32>;
@group(0) @binding(5) var<storage, read_write> dps: array<Particle>;
@group(0) @binding(6) var<storage, read_write> dhead: array<atomic<u32>>;
fn hash(n: u32) -> f32 { var x = n * 747796405u + 2891336453u; x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u; return f32((x >> 22u) ^ x) / 4294967295.0; }
fn phi(x: f32, a: f32, b: f32) -> f32 { return clamp((x - a) / (b - a), 0.0, 1.0); }
fn sampleComp(g: vec3f, comp: i32, useOld: bool) -> f32 {
  let s = stagW(g, comp);
  let b = vec3<i32>(floor(s)); let f = s - vec3f(b);
  var r = 0.0;
  for (var k = 0; k < 8; k++) {
    let o = vec3<i32>(k & 1, (k >> 1) & 1, (k >> 2) & 1);
    let w = select(1.0 - f.x, f.x, o.x == 1) * select(1.0 - f.y, f.y, o.y == 1) * select(1.0 - f.z, f.z, o.z == 1);
    let i = nIdx(b + o);
    r += w * select(vel[i][comp], velOld[i][comp], useOld);
  }
  return r;
}
fn sampleVel(g: vec3f, useOld: bool) -> vec3f { return vec3f(sampleComp(g, 0, useOld), sampleComp(g, 1, useOld), sampleComp(g, 2, useOld)); }
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) gi: vec3<u32>) {
  if (gi.x >= U.rng.y) { return; }
  let id = U.rng.x + gi.x;
  var p = ps[id];
  if (p.pos.w <= 0.0) { return; }
  let h = U.origin.w; let dt = U.misc.x;
  var g = (p.pos.xyz - U.origin.xyz) / h;
  var vel = p.vel.xyz;
  let cell = vec3<i32>(floor(g));
  if (all(cell >= vec3<i32>(1)) && all(cell < U.dims.xyz - vec3<i32>(1))) {
    let vn = sampleVel(g, false);
    let vo = sampleVel(g, true);
    vel = mix(vn, vel + (vn - vo), U.misc.z);          // FLIP (lively) / PIC (stable) blend
    // ---- diffuse material generation (Ihmsen 2012): kinetic energy x (trapped air + wave crest)
    let ik = phi(0.5 * dot(vel, vel), 0.6, 6.0);
    if (ik > 0.0) {
      let ita = phi(length(vel - vn), 0.15, 1.5);
      let above = cell + vec3<i32>(0, 1, 0);
      let surf = select(0.0, 1.0, inCells(above) && cnt[cIdx(above)] < 2u);
      let iwc = surf * phi(vel.y, 0.2, 2.0);
      let rate = ik * (60.0 * ita + 40.0 * iwc) * dt;     // expected spawns this substep
      let seed = id * 9781u + u32(U.dims.w);
      if (hash(seed) < rate) {
        let slot = atomicAdd(&dhead[0], 1u) % arrayLength(&dps);
        let j = vec3f(hash(seed + 1u), hash(seed + 2u), hash(seed + 3u)) - 0.5;
        var d: Particle;
        d.pos = vec4f(p.pos.xyz + j * 0.06, 1.0);
        d.vel = vec4f(vel * (0.9 + 0.3 * hash(seed + 4u)) + j * 0.5, 0.0);
        dps[slot] = d;
      }
    }
  } else {
    vel.y += U.misc.y * dt;                              // outside the live grid: ballistic
  }
  let sp = length(vel);
  if (sp > 14.0) { vel *= 14.0 / sp; }
  var pos = p.pos.xyz + vel * dt;
  // terrain (exact dent shape, not the staircase grid)
  let gy = groundAt(pos.xz) + 0.01;
  if (pos.y < gy) {
    pos.y = gy;
    let n = groundN(pos.xz); let vn = dot(vel, n);
    if (vn < 0.0) { vel -= vn * n; }
  }
  // tyres
  for (var w = 0; w < 4; w++) {
    let c = U.wC[w].xyz; let ax = U.wA[w].xyz;
    let q = pos - c; let al = dot(q, ax); let rad = q - ax * al; let rl = length(rad);
    if (abs(al) < U.misc2.z && rl < U.misc2.w && rl > 1e-4) {
      let n = rad / rl;
      pos = c + ax * al + n * U.misc2.w * 1.01;
      let tv = carVelAt(pos) + treadVel(ax, U.wC[w].w, n * U.misc2.w);
      var rel = vel - tv; let vn = dot(rel, n);
      if (vn < 0.0) { rel -= vn * n; }
      vel = tv + rel * 0.7;
    }
  }
  // car body: slide/bounce off
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
    vel = cv + rel * 0.8;
  }
  var life = p.pos.w;
  if (groundAt(pos.xz) > -0.01 && pos.y < 0.05) { life -= dt * 2.5; } else if (pos.y > 0.3) { life -= dt * 0.25; } else { life = min(1.0, life + dt); }
  p.pos = vec4f(pos, life);
  p.vel = vec4f(vel, p.vel.w);
  ps[id] = p;
}`,
  // diffuse particles: classify by local fluid density, advect
  diffuse: `
@group(0) @binding(1) var<storage, read_write> dps: array<Particle>;
@group(0) @binding(2) var<storage, read> vel: array<vec4f>;
@group(0) @binding(3) var<storage, read> cnt: array<u32>;
fn gridVel(g: vec3f) -> vec3f {
  var r = vec3f(0.0);
  for (var comp = 0; comp < 3; comp++) {
    let s = stagW(g, comp); let b = vec3<i32>(floor(s)); let f = s - vec3f(b);
    for (var k = 0; k < 8; k++) {
      let o = vec3<i32>(k & 1, (k >> 1) & 1, (k >> 2) & 1);
      let w = select(1.0 - f.x, f.x, o.x == 1) * select(1.0 - f.y, f.y, o.y == 1) * select(1.0 - f.z, f.z, o.z == 1);
      r[comp] += w * vel[nIdx(b + o)][comp];
    }
  }
  return r;
}
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) gi: vec3<u32>) {
  if (gi.x >= arrayLength(&dps)) { return; }
  var p = dps[gi.x];
  if (p.pos.w <= 0.0) { return; }
  let dt = U.misc.x * ${SUB}.0;
  var pos = p.pos.xyz; var v = p.vel.xyz; var life = p.pos.w;
  let g = (pos - U.origin.xyz) / U.origin.w;
  let cell = vec3<i32>(floor(g));
  var n = 0u; var nUp = 0u;
  if (inCells(cell)) {
    n = cnt[cIdx(cell)];
    for (var k = 0; k < 6; k++) {               // 3x3 column neighbourhood density
      let o = vec3<i32>((k % 3) - 1, 0, (k / 3) * 2 - 1);
      n += cnt[cIdx(cell + o)] / 2u;
    }
    nUp = cnt[cIdx(cell + vec3<i32>(0, 1, 0))];
  }
  var kind = 0.0;
  if (n < 6u) {                                   // spray: ballistic with air drag
    v.y += U.misc.y * dt; v *= 1.0 - 0.4 * dt;
    life -= dt * 0.35;
  } else if (nUp < 3u || n < 20u) {               // foam: carried by the surface flow
    kind = 1.0;
    let mud = U.pud[min(u32(U.carH.w), 2u)].w;
    let fv = gridVel(g);
    v = vec3f(fv.x, max(fv.y, -0.3), fv.z);
    life -= dt * (0.2 + 0.4 * mud);
  } else {                                        // bubble: buoyant + dragged
    kind = 2.0;
    let fv = gridVel(g);
    v = mix(v, fv, 0.3) + vec3f(0.0, 4.0 * dt, 0.0);
    life -= dt * 0.6;
  }
  pos += v * dt;
  let gy = groundAt(pos.xz) + 0.005;
  if (pos.y < gy) { pos.y = gy; v.y = max(v.y, 0.0); v *= 0.5; if (kind == 0.0) { life -= 0.15; } }
  let lp = carLocal(pos);
  if (all(abs(lp) < U.carH.xyz)) { life = 0.0; }
  let mud = U.pud[min(u32(U.carH.w), 2u)].w;
  p.pos = vec4f(pos, life);
  p.vel = vec4f(v, kind + 3.0 * U.pud[min(u32(U.carH.w), 2u)].w);
  dps[gi.x] = p;
}`,
};

// ---------------------------------------------------------------- rendering (screen-space fluid)
const R_COMMON = /* wgsl */`
struct Particle { pos: vec4f, vel: vec4f };
struct Cam { viewProj: mat4x4f, invViewProj: mat4x4f, camPos: vec4f, lightDir: vec4f, screen: vec4f };
struct RP { pud: array<vec4f, 3>, misc: vec4f };   // misc: radius, blur dir x, blur dir y, thickness scale
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
  let t = s.w * 2.0 * i.r * R.misc.w;                // overlapping spheres -> real water thickness
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
  let rpx = R.misc.x * 6.0 * cam.screen.y / (2.0 * tan(0.5) * dc);    // filter radius in pixels
  let kr = clamp(i32(rpx), 3, 32);
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
  // thickness: 5x5 gaussian-ish blur (removes per-particle speckle)
  var th = vec4f(0.0); var tw = 0.0;
  let step = max(1, i32(R.misc.x * 1.5 * cam.screen.y / (2.0 * tan(0.5) * d)));
  for (var yy = -2; yy <= 2; yy++) { for (var xx = -2; xx <= 2; xx++) {
    let w = exp(-f32(xx * xx + yy * yy) * 0.35);
    th += textureLoad(thick, clamp(p + vec2<i32>(xx, yy) * step, vec2<i32>(0), dims - vec2<i32>(1)), 0) * w; tw += w;
  } }
  th /= tw;
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
const FOAM = R_COMMON + /* wgsl */`
@group(0) @binding(2) var<storage, read> dps: array<Particle>;
struct VO { @builtin(position) pos: vec4f, @location(0) uv: vec2f, @location(1) info: vec4f };
@vertex fn vs(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VO {
  var o: VO;
  let p = dps[ii];
  if (p.pos.w <= 0.0) { o.pos = vec4f(2.0, 2.0, 2.0, 1.0); return o; }
  var corners = array<vec2f, 6>(vec2f(-1,-1), vec2f(1,-1), vec2f(1,1), vec2f(-1,-1), vec2f(1,1), vec2f(-1,-1));
  corners[5] = vec2f(-1, 1);
  let q = corners[vi];
  let kind = p.vel.w % 3.0; let mud = floor(p.vel.w / 3.0);
  let rnd = fract(sin(f32(ii) * 12.9898) * 43758.55);
  var wp = p.pos.xyz; var r: f32; var a: f32;
  if (kind < 0.5) {                          // spray droplet: camera-facing, stretched along velocity
    r = 0.012 + 0.012 * rnd; a = 0.85;
    let toCam = normalize(cam.camPos.xyz - wp);
    let vv = p.vel.xyz - toCam * dot(p.vel.xyz, toCam);
    var ax = select(normalize(cross(vec3f(0.0, 1.0, 0.0), toCam)), normalize(vv), length(vv) > 0.3);
    let st = 1.0 + min(length(vv) * 0.6, 3.0);
    let up = normalize(cross(toCam, ax));
    wp += ax * q.x * r * st + up * q.y * r;
  } else if (kind < 1.5) {                   // foam patch: flat on the surface
    r = 0.035 + 0.04 * rnd; a = 0.75;
    let c = cos(rnd * 6.28); let sn = sin(rnd * 6.28);
    wp += vec3f(q.x * c - q.y * sn, 0.004, q.x * sn + q.y * c) * r;
  } else {                                   // bubble
    r = 0.008 + 0.008 * rnd; a = 0.35;
    let toCam = normalize(cam.camPos.xyz - wp);
    let right = normalize(cross(vec3f(0.0, 1.0, 0.0), toCam)); let up = cross(toCam, right);
    wp += (right * q.x + up * q.y) * r;
  }
  wp.y += (R.misc.x - ${P_RADIUS}) * 0.0;       // keeps binding 1 live for the auto layout
  o.pos = cam.viewProj * vec4f(wp, 1.0);
  o.uv = q;
  o.info = vec4f(kind, mud, a * smoothstep(0.0, 0.25, p.pos.w), rnd);
  return o;
}
fn h2(p: vec2f) -> f32 { return fract(sin(dot(p, vec2f(127.1, 311.7))) * 43758.55); }
@fragment fn fs(i: VO) -> @location(0) vec4f {
  let r2 = dot(i.uv, i.uv);
  if (r2 > 1.0) { discard; }
  let L = normalize(cam.lightDir.xyz);
  let lit = 0.55 + 0.5 * max(L.y, 0.0);
  var base = mix(vec3f(0.95, 0.97, 1.0), vec3f(0.55, 0.42, 0.3), i.info.y);
  var a = i.info.z;
  if (i.info.x > 0.5 && i.info.x < 1.5) {
    // foam: bubbly cell texture, soft edge
    let cell = h2(floor(i.uv * 4.0 + i.info.w * 17.0));
    a *= (1.0 - r2) * (0.55 + 0.45 * cell);
  } else if (i.info.x < 0.5) {
    let nz = sqrt(1.0 - r2);
    base *= 0.8 + 0.4 * nz;
    a *= smoothstep(1.0, 0.6, r2);
  } else {
    a *= smoothstep(0.5, 1.0, r2) + 0.2;       // bubble: bright rim
  }
  return vec4f(base * lit * a, a);
}`;

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
          parts.push(p.x + x + jit(), y, p.z + z + jit(), 1, 0, 0, 0, i);
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
    this.nodes = (GRID[0] + 1) * (GRID[1] + 1) * (GRID[2] + 1);
    const sb = (bytes, label) => device.createBuffer({ size: bytes, usage: st, label });
    this.acc = sb(this.nodes * 32, 'flipAcc');          // 3 x (weighted vel, weight) fixed-point, padded to 8
    this.cnt = sb(this.cells * 4, 'flipCount');
    this.ctype = sb(this.cells * 16, 'flipCellType');
    this.vel = sb(this.nodes * 16, 'flipVel');
    this.velOld = sb(this.nodes * 16, 'flipVelOld');
    this.dv = sb(this.cells * 4, 'flipDiv');
    this.qA = sb(this.cells * 4, 'flipPressureA');
    this.qB = sb(this.cells * 4, 'flipPressureB');
    this.dbuf = sb(DIFF_N * 32, 'waterDiffuse'); this.dhead = sb(16, 'waterDiffuseHead');
    this.bytes = this.initData.byteLength + this.nodes * 64 + this.cells * 32 + DIFF_N * 32;
    this.u = new Float32Array(36 * 4); this.uI = new Int32Array(this.u.buffer); this.uU = new Uint32Array(this.u.buffer);
    this.ubo = device.createBuffer({ size: this.u.byteLength, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, label: 'waterSim' });
    this.active = -1; this.origin = [0, ORIGIN_Y, 0];

    this.pipe = {}; this.bg = {};
    const binds = {
      clear: [this.acc, this.cnt], p2g: [this.pbuf, this.acc, this.cnt], mark: [this.cnt, this.ctype],
      norm: [this.acc, this.ctype, this.vel, this.velOld], div: [this.vel, this.ctype, this.cnt, this.dv],
      jacobiAB: [this.qA, this.qB, this.ctype, this.dv], jacobiBA: [this.qB, this.qA, this.ctype, this.dv],
      project: [this.qA, this.ctype, this.vel], g2p: [this.pbuf, this.vel, this.velOld, this.cnt, this.dbuf, this.dhead],
      diffuse: [this.dbuf, this.vel, this.cnt],
    };
    for (const [k, src] of Object.entries(SIM)) {
      const label = 'water-' + k;
      const module = checkModule(device.createShaderModule({ code: SIM_COMMON + src, label }), label);
      this.pipe[k] = device.createComputePipeline({ layout: 'auto', compute: { module, entryPoint: 'main' }, label });
    }
    for (const [k, list] of Object.entries(binds)) {
      const pipe = this.pipe[k.startsWith('jacobi') ? 'jacobi' : k];
      this.bg[k] = device.createBindGroup({ layout: pipe.getBindGroupLayout(0), entries: [this.ubo, ...list].map((b, i) => ({ binding: i, resource: { buffer: b } })) });
    }

    // ---- render resources
    const mkU = () => device.createBuffer({ size: 64, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    this.rU = mkU(); this.rUH = mkU(); this.rUV = mkU();
    const rp = new Float32Array(16);
    puddles.slice(0, 3).forEach((p, i) => rp.set([p.x, p.z, p.r, p.mud], i * 4));
    const TS = SPACING ** 3 / ((4 / 3) * Math.PI * P_RADIUS ** 3);
    rp.set([P_RADIUS, 0, 0, TS], 12); device.queue.writeBuffer(this.rU, 0, rp);
    rp.set([P_RADIUS, 1, 0, TS], 12); device.queue.writeBuffer(this.rUH, 0, rp);
    rp.set([P_RADIUS, 0, 1, TS], 12); device.queue.writeBuffer(this.rUV, 0, rp);
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
    const foamM = mod(FOAM, 'water-foam');
    this.foamPipe = device.createRenderPipeline({
      layout: 'auto', label: 'water-foam',
      vertex: { module: foamM, entryPoint: 'vs' }, fragment: { module: foamM, entryPoint: 'fs', targets: [{ format, blend: pre }] },
      primitive: { topology: 'triangle-list' },
      depthStencil: { format: 'depth32float', depthWriteEnabled: false, depthCompare: 'less' },
    });
    this.foamBG = device.createBindGroup({ layout: this.foamPipe.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: camUbo } }, { binding: 1, resource: { buffer: this.rU } }, { binding: 2, resource: { buffer: this.dbuf } }] });
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
    const prev = this.active;
    if (this.active < 0) this.active = best;
    else if (best !== this.active) {
      const cur = this.puddles[this.active];
      if (Math.hypot(cur.x - car.x, cur.z - car.z) - cur.r > 12) this.active = best;
    }
    if (prev !== this.active) { const z = new Float32Array(this.cells); this.device.queue.writeBuffer(this.qA, 0, z); this.device.queue.writeBuffer(this.qB, 0, z); }
    const P = this.puddles[this.active];
    const ox = P.x - (GRID[0] * H) / 2, oz = P.z - (GRID[2] * H) / 2;
    this.origin = [ox, ORIGIN_Y, oz];
    const u = this.u; u.fill(0);
    this.frame = (this.frame || 0) + 1;
    this.uI.set([GRID[0], GRID[1], GRID[2], this.frame & 0xffff], 0);
    const [start, count] = this.ranges[this.active] || [0, 0];
    this.uU.set([start, count, 0, 0], 4);
    this.simCount = count;
    // FLIP ratio: clear water 0.96 (lively), mud 0.75 (heavily damped = thick); the viscosity slider shifts it
    const flip = Math.max(0.3, Math.min(0.99, 1 - (P.mud ? 0.25 : 0.04) * (S.waterVisc ?? 1)));
    u.set([ox, ORIGIN_Y, oz, H], 8);
    u.set([dt / SUB, -9.81, flip, PPC], 12);
    u.set([0.05, (S.tyrePush ?? 1) * (P.mud ? 0.35 : 0.6), car.wheelW * 0.5, car.wheelR], 16);
    const bodyOff = car.bodyOff || 0;
    u.set([car.x, car.bodyY + bodyOff, car.z, car.heading], 20);
    u.set([car.halfExt[0], car.halfExt[1], car.halfExt[2], this.active], 24);
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
    const p = enc.beginComputePass({ label: 'water-flip' });
    const cw = Math.ceil(this.cells / 64), nw = Math.ceil(this.nodes / 64), pw = Math.ceil(this.simCount / 64);
    const aw = Math.ceil(Math.max(this.nodes, this.cells) / 64);
    const run = (k, n, pk = k) => { p.setPipeline(this.pipe[pk]); p.setBindGroup(0, this.bg[k]); p.dispatchWorkgroups(n); };
    for (let s = 0; s < SUB; s++) {
      run('clear', aw); run('p2g', pw); run('mark', cw); run('norm', nw); run('div', cw);
      for (let i = 0; i < JACOBI; i++) run(i % 2 ? 'jacobiBA' : 'jacobiAB', cw, 'jacobi');   // even count -> result in qA
      run('project', nw); run('g2p', pw);
    }
    run('diffuse', Math.ceil(DIFF_N / 64));
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
    for (let it = 0; it < 4; it++) {
      for (const [bg, dst] of [[this.blurH, this.dB], [this.blurV, this.dA]]) {
        pass = enc.beginRenderPass({ colorAttachments: [{ view: dst.createView(), loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 0 } }] });
        pass.setPipeline(this.blurPipe); pass.setBindGroup(0, bg); pass.draw(3); pass.end();
      }
    }
    pass = enc.beginRenderPass({ colorAttachments: [{ view: colorView, loadOp: 'load', storeOp: 'store' }] });
    pass.setPipeline(this.compPipe); pass.setBindGroup(0, this.compBG); pass.draw(3); pass.end();
    pass = enc.beginRenderPass({ colorAttachments: [{ view: colorView, loadOp: 'load', storeOp: 'store' }], depthStencilAttachment: { view: this.sceneDepthView, depthReadOnly: true } });
    pass.setPipeline(this.foamPipe); pass.setBindGroup(0, this.foamBG); pass.draw(6, DIFF_N); pass.end();
  }
}

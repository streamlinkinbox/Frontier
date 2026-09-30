// Puddles (GTX): the terrain is dented into a real bowl and filled with water simulated by a
// mass-conserving shallow-water solver ("virtual pipes", Mei et al. 2007) on a 128x128 grid per
// puddle. Water has volume, flows downhill, settles flat, and is shoved aside by the tyres (bow wave,
// trough, wake that sloshes back). Plus 3D splash droplets that collide with the tyres and body.

export const WATER_N = 128;           // cells per side, per puddle
export const SPLASH_MAX = 32768;      // splash droplets (8k per wheel)
export const BED_D = 0.26;            // dent depth at the centre (m)
export const WATER_LEVEL = -0.035;    // initial still-water level (just below the rim)
const NP = 3;
const UBO_VEC4 = 20;
const G = 9.81;

// shoreline: irregular radius (mirrored exactly on the GPU; seed = i*2.7+1.3)
export function shoreR(p, ang) {
  return p.r * (1 + 0.12 * Math.sin(3 * ang + p.seed) + 0.07 * Math.sin(5 * ang + p.seed * 2.1) + 0.04 * Math.sin(9 * ang + p.seed * 0.7));
}
function bedN(dn) { return dn >= 1 ? 0 : -BED_D * Math.pow(1 - dn * dn, 1.5); }
// terrain height (dent) at x,z
export function groundY(puddles, x, z) {
  for (const p of puddles) {
    const dx = x - p.x, dz = z - p.z, d = Math.hypot(dx, dz);
    if (d > p.r * 1.3) continue;
    const dn = d / shoreR(p, Math.atan2(dz, dx));
    if (dn < 1) return bedN(dn);
  }
  return 0;
}
// puddle index if the point is under water (below the still-water level)
export function puddleAt(puddles, x, z) {
  for (let i = 0; i < puddles.length; i++) {
    const p = puddles[i], dx = x - p.x, dz = z - p.z, d = Math.hypot(dx, dz);
    if (d < p.r * 1.3 && bedN(d / shoreR(p, Math.atan2(dz, dx))) < WATER_LEVEL + 0.01) return i;
  }
  return -1;
}

const COMMON = /* wgsl */`
struct W {
  misc: vec4f,                 // dt, time, N, particleCount
  pud: array<vec4f, 3>,        // x, z, radius, mud(0..1)
  wP: array<vec4f, 4>,         // wheel x, spin(rad/s), z, emit rate (particles/s)
  wV: array<vec4f, 4>,         // contact-patch vel x, ground y, z, tread speed (m/s)
  wF: array<vec4f, 4>,         // wheel fwd x, 0, z, puddle index (-1 = dry)
  carC: vec4f, carH: vec4f, carV: vec4f,
  misc2: vec4f,                // splashGain, waveGain, wheelW, wheelR
};
@group(0) @binding(0) var<uniform> U: W;
const N: i32 = ${WATER_N};
const BED_D: f32 = ${BED_D};
fn pudSeed(i: i32) -> f32 { return f32(i) * 2.7 + 1.3; }
fn shore(i: i32, q: vec2f) -> f32 {
  let p = U.pud[i]; let a = atan2(q.y, q.x); let s = pudSeed(i);
  return p.z * (1.0 + 0.12 * sin(3.0 * a + s) + 0.07 * sin(5.0 * a + s * 2.1) + 0.04 * sin(9.0 * a + s * 0.7));
}
fn ext(i: i32) -> f32 { return U.pud[i].z * 1.3; }
fn cellSize(i: i32) -> f32 { return 2.0 * ext(i) / f32(N); }
fn cellPos(i: i32, x: i32, z: i32) -> vec2f {
  return U.pud[i].xy + (vec2f(f32(x), f32(z)) + 0.5) * cellSize(i) - ext(i);
}
fn dnAt(i: i32, p: vec2f) -> f32 { let q = p - U.pud[i].xy; return length(q) / shore(i, q); }
fn bedAt(i: i32, p: vec2f) -> f32 {
  let dn = dnAt(i, p);
  if (dn >= 1.0) { return 0.0; }
  let k = 1.0 - dn * dn;
  return -BED_D * k * sqrt(k);
}
fn groundAt(p: vec2f) -> f32 { return min(bedAt(0, p), min(bedAt(1, p), bedAt(2, p))); }
fn hIdx(i: i32, x: i32, z: i32) -> u32 {
  return u32(i * N * N + clamp(z, 0, N - 1) * N + clamp(x, 0, N - 1));
}
fn hash(n: u32) -> f32 {
  var x = n * 747796405u + 2891336453u;
  x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
  return f32((x >> 22u) ^ x) / 4294967295.0;
}
`;

// pass 1: pipe fluxes (m^3/s) to the 4 neighbours, driven by the water-surface height difference
const FLUX = COMMON + /* wgsl */`
@group(0) @binding(1) var<storage, read> depth: array<f32>;
@group(0) @binding(2) var<storage, read_write> flux: array<vec4f>;   // +x, -x, +z, -z
fn tyreHead(i: i32, p: vec2f, d: f32) -> f32 {
  // tyres are moving obstacles: they raise the local pressure head -> water is pushed out of the way
  var hd = 0.0;
  for (var w = 0; w < 4; w++) {
    if (i32(U.wF[w].w) != i) { continue; }
    let f = U.wF[w].xz; let r = vec2f(f.y, -f.x);
    let q = p - U.wP[w].xz;
    let a = dot(q, f); let sd = dot(q, r);
    let foot = exp(-(a * a / 0.05 + sd * sd / 0.012));
    let sp = length(U.wV[w].xz);
    hd += foot * min(d, 0.15) * (0.6 + sp * 0.08) * U.misc2.y;
  }
  return hd;
}
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  let x = i32(g.x); let z = i32(g.y); let i = i32(g.z);
  if (x >= N || z >= N || i >= 3) { return; }
  let id = hIdx(i, x, z);
  let dt = U.misc.x;
  let dx = cellSize(i);
  let p = cellPos(i, x, z);
  let d = depth[id];
  let H = bedAt(i, p) + d + tyreHead(i, p, d);
  var f = flux[id] * (1.0 - dt * mix(0.8, 4.0, U.pud[i].w));          // friction (mud is viscous)
  var nb = array<vec2<i32>, 4>(vec2<i32>(1, 0), vec2<i32>(-1, 0), vec2<i32>(0, 1), vec2<i32>(0, -1));
  var o = array<f32, 4>(f.x, f.y, f.z, f.w);
  for (var k = 0; k < 4; k++) {
    let n = vec2<i32>(x, z) + nb[k];
    if (n.x < 0 || n.y < 0 || n.x >= N || n.y >= N) { o[k] = 0.0; continue; }
    let pn = cellPos(i, n.x, n.y);
    let dn = depth[hIdx(i, n.x, n.y)];
    let Hn = bedAt(i, pn) + dn + tyreHead(i, pn, dn);
    o[k] = max(0.0, o[k] + dt * ${G} * dx * (H - Hn));
  }
  // never drain more water than the cell holds
  let out = o[0] + o[1] + o[2] + o[3];
  let s = select(1.0, min(1.0, d * dx * dx / (out * dt)), out > 0.0);
  flux[id] = vec4f(o[0], o[1], o[2], o[3]) * s;
}`;

// pass 2: depth += net inflow (exact volume conservation)
const DEPTH = COMMON + /* wgsl */`
@group(0) @binding(1) var<storage, read_write> depth: array<f32>;
@group(0) @binding(2) var<storage, read> flux: array<vec4f>;
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  let x = i32(g.x); let z = i32(g.y); let i = i32(g.z);
  if (x >= N || z >= N || i >= 3) { return; }
  let id = hIdx(i, x, z);
  let f = flux[id];
  var inflow = 0.0;
  if (x > 0)     { inflow += flux[hIdx(i, x - 1, z)].x; }
  if (x < N - 1) { inflow += flux[hIdx(i, x + 1, z)].y; }
  if (z > 0)     { inflow += flux[hIdx(i, x, z - 1)].z; }
  if (z < N - 1) { inflow += flux[hIdx(i, x, z + 1)].w; }
  let dx = cellSize(i);
  depth[id] = max(0.0, depth[id] + U.misc.x * (inflow - (f.x + f.y + f.z + f.w)) / (dx * dx));
}`;

const SPLASH = COMMON + /* wgsl */`
struct Pt { p: vec4f, v: vec4f };    // pos + life, vel + (puddle*4 + mud*2 + seed)
@group(0) @binding(1) var<storage, read_write> P: array<Pt>;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) g: vec3<u32>) {
  let idx = g.x;
  if (idx >= u32(U.misc.w)) { return; }
  let dt = U.misc.x;
  var pt = P[idx];
  let R = U.misc2.w; let HW = U.misc2.z * 0.5;
  if (pt.p.w <= 0.0) {
    // stateless emission: particle idx belongs to wheel idx&3 and respawns with p = rate*dt/quota
    let w = i32(idx & 3u);
    let pi = i32(U.wF[w].w);
    let rate = U.wP[w].w;
    let seed = idx * 9781u + u32(U.misc.y * 1000.0) * 6271u;
    if (pi < 0 || rate <= 0.0 || hash(seed) > rate * dt / (U.misc.w * 0.25)) { P[idx] = pt; return; }
    let f = U.wF[w].xyz; let r = vec3f(f.z, 0.0, -f.x);
    let vel = vec3f(U.wV[w].x, 0.0, U.wV[w].z); let sp = length(vel.xz);
    let tread = U.wV[w].w;
    let k = vel + f * tread;                           // what the tyre surface throws backwards
    let side = select(-1.0, 1.0, hash(seed + 1u) > 0.5);
    let h2 = hash(seed + 2u); let h3 = hash(seed + 3u); let h4 = hash(seed + 4u);
    var v: vec3f;
    var pos = vec3f(U.wP[w].x, ${WATER_LEVEL} + 0.03, U.wP[w].z) + r * side * HW * (0.6 + 0.6 * h4);
    if (hash(seed + 5u) < 0.55) {
      v = vel * (0.35 + 0.35 * h2) + r * side * (0.6 + sp * (0.18 + 0.25 * h3)) + vec3f(0.0, 0.8 + sp * (0.12 + 0.22 * h2), 0.0);
      pos += f * (0.2 + 0.2 * h3);
    } else {
      v = k * (0.35 + 0.45 * h2) + vec3f(0.0, 1.0 + length(k) * (0.18 + 0.3 * h3), 0.0) + r * (h4 - 0.5) * 1.5;
      pos -= normalize(k + vec3f(1e-4)) * (0.3 + 0.1 * h3);
    }
    pt.p = vec4f(pos, 0.6 + 1.2 * hash(seed + 6u));
    pt.v = vec4f(v, f32(pi) * 4.0 + U.pud[pi].w * 2.0 + hash(seed + 7u) * 0.99);
    P[idx] = pt; return;
  }
  var p = pt.p.xyz; var v = pt.v.xyz;
  v.y -= 9.81 * dt;
  v *= 1.0 - 0.35 * dt;
  p += v * dt;
  for (var w = 0; w < 4; w++) {
    let f = U.wF[w].xyz; let ax = vec3f(f.z, 0.0, -f.x);
    let c = vec3f(U.wP[w].x, R + U.wV[w].y, U.wP[w].z);
    let q = p - c; let al = dot(q, ax); let rad = q - ax * al; let rl = length(rad);
    if (abs(al) < HW && rl < R && rl > 1e-4) {
      let n = rad / rl;
      p = c + ax * al + n * R * 1.02;
      let surf = vec3f(U.wV[w].x, 0.0, U.wV[w].z) + cross(ax * U.wP[w].y, n * R) * 0.9;
      var rel = v - surf; let vn = dot(rel, n);
      if (vn < 0.0) { rel -= 1.25 * vn * n; }
      v = surf + rel * 0.85;
    }
  }
  let hd = U.carC.w; let cs = cos(hd); let sn = sin(hd);
  let d = p - U.carC.xyz;
  let l = vec3f(d.x * cs - d.z * sn, d.y, d.x * sn + d.z * cs);
  let pen = U.carH.xyz - abs(l);
  if (all(pen > vec3f(0.0))) {
    var nl = vec3f(0.0);
    if (pen.x < pen.y && pen.x < pen.z) { nl.x = sign(l.x); } else if (pen.y < pen.z) { nl.y = sign(l.y); } else { nl.z = sign(l.z); }
    let lp = l + nl * (dot(pen, abs(nl)) + 0.01);
    p = U.carC.xyz + vec3f(lp.x * cs + lp.z * sn, lp.y, -lp.x * sn + lp.z * cs);
    let n = vec3f(nl.x * cs + nl.z * sn, nl.y, -nl.x * sn + nl.z * cs);
    let bv = vec3f(U.carV.x + U.carV.w * d.z, 0.0, U.carV.z - U.carV.w * d.x);
    var rel = v - bv; let vn = dot(rel, n);
    if (vn < 0.0) { rel -= 1.3 * vn * n; }
    v = bv + rel * 0.7;
  }
  var life = pt.p.w - dt;
  if (p.y < max(groundAt(p.xz), ${WATER_LEVEL}) + 0.01) { life = 0.0; }   // fell back into the water / onto sand
  pt.p = vec4f(p, life); pt.v = vec4f(v, pt.v.w);
  P[idx] = pt;
}`;

const RENDER_COMMON = COMMON + /* wgsl */`
struct Cam { viewProj: mat4x4f, invViewProj: mat4x4f, camPos: vec4f, lightDir: vec4f };
@group(0) @binding(1) var<uniform> cam: Cam;
fn sky(d: vec3f) -> vec3f { return mix(vec3f(0.78, 0.8, 0.82), vec3f(0.42, 0.6, 0.86), clamp(d.y * 1.6, 0.0, 1.0)); }
fn gridVert(vi: u32) -> vec2<i32> {
  let quad = i32(vi / 6u); let corner = vi % 6u;
  var offs = array<vec2<i32>, 6>(vec2<i32>(0,0), vec2<i32>(1,0), vec2<i32>(1,1), vec2<i32>(0,0), vec2<i32>(1,1), vec2<i32>(0,1));
  return vec2<i32>(quad % (N - 1), quad / (N - 1)) + offs[corner];
}
fn h2(p: vec2f) -> f32 { return fract(sin(dot(p, vec2f(127.1, 311.7))) * 43758.5453); }
fn vn2(p: vec2f) -> f32 {
  let i = floor(p); let f = fract(p); let u = f * f * (3.0 - 2.0 * f);
  return mix(mix(h2(i), h2(i + vec2f(1, 0)), u.x), mix(h2(i + vec2f(0, 1)), h2(i + vec2f(1, 1)), u.x), u.y);
}
`;

// the dented puddle bed (replaces the flat ground inside the shoreline): wet sand / mud
const BED = RENDER_COMMON + /* wgsl */`
struct VO { @builtin(position) pos: vec4f, @location(0) wp: vec3f, @location(1) n: vec3f, @location(2) info: vec2f };
@vertex fn vs(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VO {
  let i = i32(ii); let c = gridVert(vi);
  let p = cellPos(i, c.x, c.y); let e = cellSize(i);
  let b = bedAt(i, p);
  let gx = bedAt(i, p + vec2f(e, 0.0)) - bedAt(i, p - vec2f(e, 0.0));
  let gz = bedAt(i, p + vec2f(0.0, e)) - bedAt(i, p - vec2f(0.0, e));
  var o: VO;
  o.wp = vec3f(p.x, b, p.y);
  o.pos = cam.viewProj * vec4f(o.wp, 1.0);
  o.n = normalize(vec3f(-gx / (2.0 * e), 1.0, -gz / (2.0 * e)));
  o.info = vec2f(dnAt(i, p), U.pud[i].w);
  return o;
}
@fragment fn fs(v: VO) -> @location(0) vec4f {
  if (v.info.x > 1.0) { discard; }
  let L = normalize(cam.lightDir.xyz);
  let p = v.wp.xz;
  let nz = vn2(p * 1.7) * 0.6 + vn2(p * 9.0) * 0.4;
  var base = mix(vec3f(0.74, 0.58, 0.38), vec3f(0.86, 0.72, 0.50), nz * 0.8);
  let wet = smoothstep(${WATER_LEVEL} + 0.03, ${WATER_LEVEL} - 0.01, v.wp.y);   // soaked below the waterline
  base *= mix(0.62, 0.42, wet);                                                   // damp sand darkens
  base = mix(base, vec3f(0.22, 0.16, 0.1) * (0.8 + 0.4 * nz), v.info.y * smoothstep(0.95, 0.6, v.info.x)); // mud bottom
  let n = normalize(v.n + vec3f(vn2(p * 6.0) - 0.5, 0.0, vn2(p * 6.0 + 7.0) - 0.5) * 0.15);
  let diff = max(dot(n, L), 0.0);
  let hemi = mix(vec3f(0.35, 0.3, 0.25), vec3f(0.55, 0.65, 0.8), n.y * 0.5 + 0.5);
  var c = base * (hemi * 0.75 + vec3f(1.0, 0.94, 0.85) * diff * 0.95);
  let fog = 1.0 - exp(-distance(cam.camPos.xyz, v.wp) * 0.006);
  c = mix(c, vec3f(0.78, 0.8, 0.82), fog);
  return vec4f(c, 1.0);
}`;

// the water itself: surface = bed + simulated depth; colour by Beer-Lambert absorption over the depth
const SURFACE = RENDER_COMMON + /* wgsl */`
@group(0) @binding(2) var<storage, read> depth: array<f32>;
struct VO { @builtin(position) pos: vec4f, @location(0) wp: vec3f, @location(1) n: vec3f, @location(2) info: vec3f };
fn surfH(i: i32, x: i32, z: i32) -> f32 {
  let d = depth[hIdx(i, x, z)];
  return bedAt(i, cellPos(i, clamp(x, 0, N - 1), clamp(z, 0, N - 1))) + d;
}
@vertex fn vs(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VO {
  let i = i32(ii); let c = gridVert(vi);
  let p = cellPos(i, c.x, c.y); let e = cellSize(i);
  let d = depth[hIdx(i, c.x, c.y)];
  let b = bedAt(i, p);
  // only use wet neighbours for the slope, so the shoreline doesn't tilt the surface into the bank
  let hc = b + d;
  var hx1 = surfH(i, c.x + 1, c.y); var hx0 = surfH(i, c.x - 1, c.y);
  var hz1 = surfH(i, c.x, c.y + 1); var hz0 = surfH(i, c.x, c.y - 1);
  if (depth[hIdx(i, c.x + 1, c.y)] < 0.003) { hx1 = hc; }
  if (depth[hIdx(i, c.x - 1, c.y)] < 0.003) { hx0 = hc; }
  if (depth[hIdx(i, c.x, c.y + 1)] < 0.003) { hz1 = hc; }
  if (depth[hIdx(i, c.x, c.y - 1)] < 0.003) { hz0 = hc; }
  var o: VO;
  o.wp = vec3f(p.x, hc + select(-0.01, 0.0, d > 0.003), p.y);
  o.pos = cam.viewProj * vec4f(o.wp, 1.0);
  o.n = normalize(vec3f(-(hx1 - hx0) / (2.0 * e), 1.0, -(hz1 - hz0) / (2.0 * e)));
  o.info = vec3f(d, U.pud[i].w, length(vec2f(hx1 - hx0, hz1 - hz0)) / e);
  return o;
}
@fragment fn fs(v: VO) -> @location(0) vec4f {
  let d = v.info.x; let mud = v.info.y;
  if (d < 0.003) { discard; }
  let L = normalize(cam.lightDir.xyz);
  let V = normalize(cam.camPos.xyz - v.wp);
  let n = v.n;
  let cosV = max(dot(n, V), 0.0);
  let fres = 0.02 + 0.98 * pow(1.0 - cosV, 5.0);
  let R = reflect(-V, n);
  let spec = pow(max(dot(R, L), 0.0), mix(400.0, 80.0, mud)) * mix(6.0, 1.0, mud);
  let diff = max(dot(n, L), 0.0);
  // Beer-Lambert: light travels d/cos down and back up through the water
  let path = d * (1.0 / max(cosV, 0.2) + 1.0);
  let sigma = mix(vec3f(4.5, 2.2, 1.6), vec3f(40.0, 48.0, 60.0), mud);    // clear: slight blue-green; mud: opaque
  let T = exp(-sigma * path);
  let inscatter = mix(vec3f(0.05, 0.1, 0.1), vec3f(0.34, 0.25, 0.16) * (0.5 + 0.7 * diff), mud);
  // premultiplied: rgb = water colour added, a = how much of the bed below is hidden
  let absorbA = 1.0 - (T.x + T.y + T.z) / 3.0;
  var col = inscatter * absorbA;
  var a = absorbA;
  let refl = (sky(R) + vec3f(1.0, 0.95, 0.85) * spec) * fres * mix(1.0, 0.6, mud);
  col = col * (1.0 - fres) + refl; a = a + fres * (1.0 - a);
  // disturbed water: foam/froth on steep slopes (bow wave, wake)
  let foam = smoothstep(0.35, 1.2, v.info.z) * smoothstep(0.004, 0.03, d);
  let foamCol = mix(vec3f(0.95), vec3f(0.6, 0.47, 0.33), mud) * (0.55 + 0.5 * diff);
  col = mix(col, foamCol, foam * 0.7); a = mix(a, 1.0, foam * 0.7);
  let edge = smoothstep(0.003, 0.02, d);                 // soft waterline
  col *= edge; a *= edge;
  let fog = 1.0 - exp(-distance(cam.camPos.xyz, v.wp) * 0.006);
  col = mix(col, vec3f(0.78, 0.8, 0.82) * a, fog);
  return vec4f(col, a);
}`;

const DROPS = RENDER_COMMON + /* wgsl */`
struct Pt { p: vec4f, v: vec4f };
@group(0) @binding(2) var<storage, read> P: array<Pt>;
struct VO { @builtin(position) pos: vec4f, @location(0) uv: vec2f, @location(1) info: vec3f };
@vertex fn vs(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VO {
  var o: VO;
  let pt = P[ii];
  if (pt.p.w <= 0.0) { o.pos = vec4f(2.0, 2.0, 2.0, 1.0); return o; }
  var corners = array<vec2f, 6>(vec2f(-1,-1), vec2f(1,-1), vec2f(1,1), vec2f(-1,-1), vec2f(1,1), vec2f(-1,1));
  let c = corners[vi];
  let seed = fract(pt.v.w);
  let mud = fract(floor(pt.v.w) * 0.25) * 2.0;
  let size = (0.02 + 0.035 * seed) * (0.85 + 0.15 * clamp(U.misc2.x, 0.0, 3.0));
  let vel = pt.v.xyz; let sp = length(vel);
  let toCam = normalize(cam.camPos.xyz - pt.p.xyz);
  var a1 = select(vec3f(0.0, 1.0, 0.0), vel / sp, sp > 1e-3);
  var a2 = cross(a1, toCam);
  if (length(a2) < 1e-3) { a2 = vec3f(1.0, 0.0, 0.0); }
  a2 = normalize(a2);
  let wp = pt.p.xyz + a1 * c.y * (size + sp * 0.012) + a2 * c.x * size;
  o.pos = cam.viewProj * vec4f(wp, 1.0);
  o.uv = c;
  o.info = vec3f(clamp(mud, 0.0, 1.0), min(pt.p.w * 3.0, 1.0), seed);
  return o;
}
@fragment fn fs(v: VO) -> @location(0) vec4f {
  let r2 = dot(v.uv, v.uv);
  if (r2 > 1.0) { discard; }
  let L = normalize(cam.lightDir.xyz);
  let nz = sqrt(1.0 - r2);
  let n = vec3f(v.uv.x, v.uv.y, nz);
  let rim = pow(1.0 - nz, 2.0);
  let mud = v.info.x;
  let clearC = vec3f(0.72, 0.8, 0.86) * (0.5 + 0.5 * rim) + vec3f(1.0) * pow(max(dot(n, normalize(vec3f(0.3, 0.5, 1.0))), 0.0), 30.0) * 0.8;
  let mudC = vec3f(0.33, 0.24, 0.15) * (0.6 + 0.5 * max(L.y, 0.0)) * (1.0 - 0.3 * rim);
  let col = mix(clearC, mudC, mud);
  let a = mix(0.35 + 0.4 * rim, 0.92, mud) * v.info.y * smoothstep(1.0, 0.6, r2);
  return vec4f(col * a, a);
}`;

export class Water {
  constructor(device, format, checkModule, puddles, camUbo) {
    this.device = device;
    this.puddles = puddles;
    const nH = NP * WATER_N * WATER_N;
    const st = GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST;
    this.depth = device.createBuffer({ size: nH * 4, usage: st, label: 'waterDepth' });
    this.flux = device.createBuffer({ size: nH * 16, usage: st, label: 'waterFlux' });
    this.pts = device.createBuffer({ size: SPLASH_MAX * 32, usage: st, label: 'splash' });
    this.bytes = nH * 20 + SPLASH_MAX * 32;
    this.u = new Float32Array(UBO_VEC4 * 4);
    this.ubo = device.createBuffer({ size: this.u.byteLength, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, label: 'waterU' });
    this.time = 0;
    this.fill();

    const cp = (code, label) => device.createComputePipeline({ layout: 'auto', label, compute: { module: checkModule(device.createShaderModule({ code, label }), label), entryPoint: 'main' } });
    this.fluxPipe = cp(FLUX, 'water-flux');
    this.depthPipe = cp(DEPTH, 'water-depth');
    this.splashPipe = cp(SPLASH, 'water-splash');
    const bg = (pipe, bufs) => device.createBindGroup({ layout: pipe.getBindGroupLayout(0), entries: bufs.map((b, i) => ({ binding: i, resource: { buffer: b } })) });
    this.fluxBG = bg(this.fluxPipe, [this.ubo, this.depth, this.flux]);
    this.depthBG = bg(this.depthPipe, [this.ubo, this.depth, this.flux]);
    this.splashBG = bg(this.splashPipe, [this.ubo, this.pts]);

    const blend = { color: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' }, alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' } };
    const rp = (code, label, opaque) => {
      const module = checkModule(device.createShaderModule({ code, label }), label);
      return device.createRenderPipeline({
        layout: 'auto', label,
        vertex: { module, entryPoint: 'vs' },
        fragment: { module, entryPoint: 'fs', targets: [opaque ? { format } : { format, blend }] },
        primitive: { topology: 'triangle-list', cullMode: 'none' },
        depthStencil: { format: 'depth32float', depthWriteEnabled: !!opaque, depthCompare: 'less' },
      });
    };
    this.bedPipe = rp(BED, 'water-bed', true);
    this.surfPipe = rp(SURFACE, 'water-surface');
    this.dropPipe = rp(DROPS, 'water-drops');
    this.bedBG = bg(this.bedPipe, [this.ubo, camUbo]);
    this.surfBG = bg(this.surfPipe, [this.ubo, camUbo, this.depth]);
    this.dropBG = bg(this.dropPipe, [this.ubo, camUbo, this.pts]);
  }

  // fill each dent with still water up to WATER_LEVEL (CPU mirror of bedAt)
  fill() {
    const N = WATER_N, d = new Float32Array(NP * N * N);
    this.puddles.slice(0, NP).forEach((p, i) => {
      const e = p.r * 1.3, dx = (2 * e) / N;
      for (let z = 0; z < N; z++) for (let x = 0; x < N; x++) {
        const qx = (x + 0.5) * dx - e, qz = (z + 0.5) * dx - e;
        const dn = Math.hypot(qx, qz) / shoreR(p, Math.atan2(qz, qx));
        d[i * N * N + z * N + x] = Math.max(0, WATER_LEVEL - bedN(dn));
      }
    });
    this.device.queue.writeBuffer(this.depth, 0, d);
    this.device.queue.writeBuffer(this.flux, 0, new Float32Array(NP * N * N * 4));
  }

  update(dt, car, wet, S) {
    this.time += dt;
    const SUB = 4;
    this.sub = SUB;
    const u = this.u; u.fill(0);
    u.set([dt / SUB, this.time, WATER_N, SPLASH_MAX], 0);
    this.puddles.forEach((p, i) => u.set([p.x, p.z, p.r, p.mud], 4 + i * 4));
    car.wheels.forEach((wh, i) => {
      const pi = wet[i];
      const tread = -wh.spinVel * car.wheelR;
      const sp = Math.hypot(wh.vel[0], wh.vel[2]);
      const slip = Math.abs(wh.slipLat) + Math.abs(wh.slipLong);
      const rate = pi >= 0 && (sp > 0.8 || slip > 1) ? (sp * 170 + slip * 90) * (S.splash ?? 1) : 0;
      u.set([wh.pos[0], wh.spinVel, wh.pos[2], rate], 8 + i * 4);
      u.set([wh.vel[0], wh.gy || 0, wh.vel[2], tread], 24 + i * 4);
      u.set([wh.fwd[0], 0, wh.fwd[2], pi], 40 + i * 4);
    });
    u.set([car.x, car.bodyY + (car.bodyOff || 0), car.z, car.heading], 56);
    u.set([car.halfExt[0], car.halfExt[1], car.halfExt[2], 0], 60);
    u.set([car.vx, 0, car.vz, car.w], 64);
    u.set([S.splash ?? 1, S.waves ?? 1, car.wheelW, car.wheelR], 72);
    this.device.queue.writeBuffer(this.ubo, 0, u);
  }

  encode(enc) {
    const p = enc.beginComputePass({ label: 'water' });
    const wg = Math.ceil(WATER_N / 8);
    // SUB substeps of dt/SUB for the fluid (stable, smooth sloshing); droplets ride along with the same substeps
    for (let s = 0; s < this.sub; s++) {
      p.setPipeline(this.fluxPipe); p.setBindGroup(0, this.fluxBG); p.dispatchWorkgroups(wg, wg, NP);
      p.setPipeline(this.depthPipe); p.setBindGroup(0, this.depthBG); p.dispatchWorkgroups(wg, wg, NP);
      p.setPipeline(this.splashPipe); p.setBindGroup(0, this.splashBG); p.dispatchWorkgroups(Math.ceil(SPLASH_MAX / 64));
    }
    p.end();
  }

  drawOpaque(pass) {
    pass.setPipeline(this.bedPipe); pass.setBindGroup(0, this.bedBG);
    pass.draw((WATER_N - 1) * (WATER_N - 1) * 6, NP);
  }
  draw(pass) {
    pass.setPipeline(this.surfPipe); pass.setBindGroup(0, this.surfBG);
    pass.draw((WATER_N - 1) * (WATER_N - 1) * 6, NP);
    pass.setPipeline(this.dropPipe); pass.setBindGroup(0, this.dropBG);
    pass.draw(6, SPLASH_MAX);
  }
}

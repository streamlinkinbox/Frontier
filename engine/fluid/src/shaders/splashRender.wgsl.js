// Screen-space fluid rendering of the splash liquid (van der Laan, Green & Sainz 2009 / NVIDIA
// GDC 2010; depth smoothing with the narrow-range filter of Truong & Yuksel 2018):
//   1. every particle is drawn as a sphere into an eye-depth map (nearest surface wins) and, with
//      additive blending, into a thickness map (+ mud and aeration carried along)
//   2. the depth map is smoothed by a separable narrow-range filter -> one continuous surface
//   3. composite: normals from the smoothed depth; Fresnel sky reflection, sun glints, refraction of
//      the scene behind (including the water surface), Beer–Lambert absorption by the thickness,
//      milky aerated water where the splash is broken up, brown where it carries mud
import { VIEW_STRUCT, SKY_FN, SHADOW_FN } from './render.wgsl.js';

const SR_STRUCT = /* wgsl */ `
struct SR {
  radius: f32, refract: f32, alphaThick: f32, whiteness: f32,
  absorb: vec3f, turbidity: f32,
  mudAbsorb: vec3f, aerDensity: f32,
  mudColor: vec3f, aerSpeed: f32,
  liquidRho: f32, sprayAlpha: f32, sprayRadius: f32, shutter: f32,
  sub: u32, subN: u32, pad0: f32, pad1: f32,
};
`;

export const SPLASH_SPHERES = /* wgsl */ `
${VIEW_STRUCT}
${SR_STRUCT}
@group(0) @binding(0) var<uniform> V: View;
@group(0) @binding(1) var<uniform> R: SR;
@group(0) @binding(2) var<storage, read> pos: array<vec4f>;
@group(0) @binding(3) var<storage, read> vel: array<vec4f>;
@group(0) @binding(4) var<storage, read> draw: array<u32>;
@group(0) @binding(5) var sceneDepth: texture_depth_2d;
@group(0) @binding(6) var<storage, read> info: array<vec2f>;   // liquid flag, smoothed density

struct VO {
  @builtin(position) pos: vec4f,
  @location(0) c: vec2f,
  @location(1) @interpolate(flat) center: vec3f,
  @location(2) @interpolate(flat) info: vec4f,     // mud, aeration
};
fn hash3(n: u32) -> vec3f {          // Mitchell's best-candidate offsets, used in order
  var i = n;
  if (i < 12u) { i = 12u; }
  let f = f32(i) * 0.6180339887 + fract(f32(i) * 0.7548776662);
  return vec3f(fract(f), fract(f * 1.618034 + 0.37), fract(f * 2.618034 + 0.11)) * 2.0 - 1.0;
}
@vertex fn vs(@builtin(vertex_index) vi: u32, @builtin(instance_index) iii: u32) -> VO {
  var corners = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
  let ii = iii / R.sub;
  let sIdx = iii % R.sub;
  let id = draw[1u + ii];
  var o: VO;
  // only coherent liquid (enough neighbours) forms the surface; isolated parcels are spray (SPRAY)
  if (info[id].x < 0.5) { o.pos = vec4f(2.0, 2.0, 2.0, 1.0); return o; }
  // a parcel is drawn as R.sub smaller spheres at the vertices of a tiny lattice, so the liquid
  // surface resolves finer detail than the simulation resolution
  var p = pos[id].xyz;
  var r = R.radius;
  if (R.sub > 1u) {
    let f = f32(R.sub);
    let sc = R.radius / (sqrt(f) * 1.05);
    p += hash3(sIdx) * sc * 1.9;
    r = sc;
  }
  let c = corners[vi];
  // a little larger than the sphere so its projection is always covered
  let wp = p + (V.camRight * c.x + V.camUp * c.y) * r * 1.15;
  o.pos = V.viewProj * vec4f(wp, 1.0);
  o.c = c * 1.15;
  o.center = p;
  // broken-up water is white: sparse or fast parcels are aerated
  let rho = info[id].y;
  let sp = length(vel[id].xyz);
  o.info.w = r;
  let aer = clamp(1.0 - rho * R.aerDensity, 0.0, 1.0) * 0.75 + clamp((sp - 2.5) / R.aerSpeed, 0.0, 0.35);
  o.info = vec4f(clamp(vel[id].w, 0.0, 1.0), clamp(aer, 0.0, 1.0), 0.0, 0.0);
  return o;
}
fn spherePoint(in: VO, d2: f32) -> vec3f {
  let toCam = normalize(V.camPos - in.center);
  return in.center + (V.camRight * in.c.x + V.camUp * in.c.y) * R.radius + toCam * sqrt(1.0 - d2) * R.radius;
}
struct DOut { @builtin(frag_depth) depth: f32, @location(0) eye: f32 };
@fragment fn fsDepth(in: VO) -> DOut {
  let d2 = dot(in.c, in.c);
  if (d2 > 1.0) { discard; }
  let P = spherePoint(in, d2);
  let clip = V.viewProj * vec4f(P, 1.0);
  let z = clip.z / clip.w;
  if (textureLoad(sceneDepth, vec2i(in.pos.xy), 0) > z) { discard; }   // behind opaque geometry (reverse-Z)
  var o: DOut;
  o.depth = z;
  o.eye = dot(P - V.camPos, V.camFwd);
  return o;
}
@fragment fn fsThick(in: VO) -> @location(0) vec4f {
  let d2 = dot(in.c, in.c);
  if (d2 > 1.0) { discard; }
  let P = spherePoint(in, d2);
  let clip = V.viewProj * vec4f(P, 1.0);
  if (textureLoad(sceneDepth, vec2i(in.pos.xy), 0) > clip.z / clip.w) { discard; }
  let t = 2.0 * sqrt(1.0 - d2) * in.info.w;      // path length through this sphere (m)
  return vec4f(t, t * in.info.x, t * in.info.y, 1.0);
}
`;

// Broken-up splash water: isolated parcels stand for clouds of small droplets, drawn as soft,
// translucent, motion-blurred spray (bright where it scatters sunlight forward), not as spheres.
export const SPLASH_SPRAY = /* wgsl */ `
${VIEW_STRUCT}
${SR_STRUCT}
@group(0) @binding(0) var<uniform> V: View;
@group(0) @binding(1) var<uniform> R: SR;
@group(0) @binding(2) var<storage, read> pos: array<vec4f>;
@group(0) @binding(3) var<storage, read> vel: array<vec4f>;
@group(0) @binding(4) var<storage, read> draw: array<u32>;
@group(0) @binding(5) var sceneDepth: texture_depth_2d;
@group(0) @binding(6) var<storage, read> info: array<vec2f>;
${SKY_FN}
struct SO {
  @builtin(position) pos: vec4f,
  @location(0) c: vec2f,
  @location(1) color: vec4f,
};
@vertex fn vs(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> SO {
  var corners = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
  let id = draw[1u + ii];
  var o: SO;
  if (info[id].x > 0.5) { o.pos = vec4f(2.0, 2.0, 2.0, 1.0); return o; }
  let p = pos[id].xyz;
  let v = vel[id].xyz;
  let c = corners[vi];
  // stretched along the screen-space motion over the shutter time
  let vp = v - V.camFwd * dot(v, V.camFwd);
  let vl = length(vp);
  var axis = V.camUp;
  if (vl > 1e-3) { axis = vp / vl; }
  let side = normalize(cross(axis, V.camFwd));
  let r = R.sprayRadius * (0.6 + 0.8 * fract(f32(id) * 0.6180339));
  let halfLen = r + vl * R.shutter * 0.5;
  o.pos = V.viewProj * vec4f(p + side * c.x * r + axis * c.y * halfLen, 1.0);
  o.c = c;
  let toCam = normalize(V.camPos - p);
  let fwd = pow(max(dot(-toCam, V.sunDir), 0.0), 6.0);
  let amb = skyColor(vec3f(0.0, 1.0, 0.0), false);
  let mud = clamp(vel[id].w, 0.0, 1.0);
  var col = (amb * 0.85 + V.sunColor * (0.22 + 1.4 * fwd)) * vec3f(0.93, 0.96, 1.0);
  col = mix(col, R.mudColor * (amb * 0.7 + V.sunColor * 0.6), clamp(mud * 1.6, 0.0, 1.0));
  o.color = vec4f(col, R.sprayAlpha * (r / halfLen) * mix(1.0, 2.2, clamp(mud * 1.6, 0.0, 1.0)));
  return o;
}
@fragment fn fs(in: SO) -> @location(0) vec4f {
  let q = 1.0 - dot(in.c, in.c);
  if (q <= 0.0) { discard; }
  let a = in.color.a * q * q;
  return vec4f(in.color.rgb * a, a);
}
`;

// separable narrow-range filter on the eye depth (empty pixels hold 0)
export const SPLASH_FILTER = /* wgsl */ `
${VIEW_STRUCT}
${SR_STRUCT}
@group(0) @binding(0) var<uniform> V: View;
@group(0) @binding(1) var<uniform> R: SR;
@group(0) @binding(2) var src: texture_2d<f32>;
@group(0) @binding(3) var<uniform> dirU: vec4f;
@vertex fn vs(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4f {
  var p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
  return vec4f(p[vi], 0.0, 1.0);
}
@fragment fn fs(@builtin(position) fc: vec4f) -> @location(0) f32 {
  let ip = vec2i(fc.xy);
  let z = textureLoad(src, ip, 0).r;
  if (z <= 0.0) { return 0.0; }
  let dir = vec2i(dirU.xy);
  let hi = vec2i(textureDimensions(src)) - 1;
  // filter width ~ the projected particle size
  let focal = V.viewProj[1][1] * V.screen.y * 0.5;
  let half = i32(clamp(R.radius * focal / z * 2.0, 1.0, 16.0));
  let sigma = max(f32(half) * 0.5, 0.5);
  let gk = 0.5 / (sigma * sigma);
  let threshold = R.radius * 5.0;
  let boundHigh = z + R.radius;
  var lo = z - threshold;
  var top = z + threshold;
  var sum = z;
  var wsum = 1.0;
  for (var i = 1; i <= half; i++) {
    var s = vec2f(textureLoad(src, clamp(ip + dir * i, vec2i(0), hi), 0).r, textureLoad(src, clamp(ip - dir * i, vec2i(0), hi), 0).r);
    // too close (or empty): skip symmetrically; too far: clamp (keeps silhouettes, no bleeding)
    if (any(s < vec2f(lo))) { continue; }
    s = select(s, vec2f(boundHigh), s > vec2f(top));
    lo = min(lo, min(s.x, s.y) - threshold);
    top = max(top, max(s.x, s.y) + threshold);
    let w = exp(-f32(i * i) * gk);
    sum += (s.x + s.y) * w;
    wsum += 2.0 * w;
  }
  return sum / wsum;
}
`;

export const SPLASH_COMPOSITE = /* wgsl */ `
${VIEW_STRUCT}
${SR_STRUCT}
@group(0) @binding(0) var<uniform> V: View;
@group(0) @binding(1) var<uniform> R: SR;
@group(0) @binding(2) var depthTex: texture_2d<f32>;
@group(0) @binding(3) var thickTex: texture_2d<f32>;
@group(0) @binding(4) var sceneColor: texture_2d<f32>;
@group(0) @binding(5) var linSamp: sampler;
@group(0) @binding(6) var shadowMap: texture_depth_2d;
@group(0) @binding(7) var shadowSamp: sampler_comparison;
@group(0) @binding(8) var smoothDepth: texture_2d<f32>;
${SKY_FN}
${SHADOW_FN}
@vertex fn vs(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4f {
  var p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
  return vec4f(p[vi], 0.0, 1.0);
}
fn worldAt(ip: vec2i, z: f32) -> vec3f {
  let ndc = vec2f((f32(ip.x) + 0.5) * V.invScreen.x * 2.0 - 1.0, 1.0 - (f32(ip.y) + 0.5) * V.invScreen.y * 2.0);
  let hp = V.invViewProj * vec4f(ndc, 0.5, 1.0);
  let dir = normalize(hp.xyz / hp.w - V.camPos);
  return V.camPos + dir * (z / max(dot(dir, V.camFwd), 1e-3));
}
fn ggx(N: vec3f, Vd: vec3f, L: vec3f, rough: f32) -> f32 {
  let H = normalize(Vd + L);
  let nh = max(dot(N, H), 0.0); let nl = max(dot(N, L), 0.0); let nv = max(dot(N, Vd), 1e-3);
  let a = rough * rough; let a2 = a * a;
  let dd = nh * nh * (a2 - 1.0) + 1.0;
  let k = a * 0.5;
  let G = (nl / (nl * (1.0 - k) + k)) * (nv / (nv * (1.0 - k) + k));
  let F = 0.02 + 0.98 * pow(1.0 - max(dot(H, Vd), 0.0), 5.0);
  return min(a2 / (3.14159 * dd * dd) * G * F / (4.0 * nv), 400.0);
}
@fragment fn fs(@builtin(position) fc: vec4f) -> @location(0) vec4f {
  let ip = vec2i(fc.xy);
  let z = textureLoad(depthTex, ip, 0).r;
  if (z <= 0.0) { discard; }
  let hi = vec2i(textureDimensions(depthTex)) - 1;
  let P = worldAt(ip, z);
  // normal from the smoothed depth: take the neighbour on the side with the smaller step so the
  // normal never smears across a silhouette
  let ir = min(ip + vec2i(1, 0), hi); let il = max(ip - vec2i(1, 0), vec2i(0));
  let id = min(ip + vec2i(0, 1), hi); let iu = max(ip - vec2i(0, 1), vec2i(0));
  let zr = textureLoad(depthTex, ir, 0).r; let zl = textureLoad(depthTex, il, 0).r;
  let zd = textureLoad(depthTex, id, 0).r; let zu = textureLoad(depthTex, iu, 0).r;
  let Vd = normalize(V.camPos - P);
  var ddx = V.camRight * 0.01;
  var ddy = -V.camUp * 0.01;
  let okR = zr > 0.0; let okL = zl > 0.0; let okD = zd > 0.0; let okU = zu > 0.0;
  if (okR && (!okL || abs(zr - z) <= abs(z - zl))) { ddx = worldAt(ir, zr) - P; } else if (okL) { ddx = P - worldAt(il, zl); }
  if (okD && (!okU || abs(zd - z) <= abs(z - zu))) { ddy = worldAt(id, zd) - P; } else if (okU) { ddy = P - worldAt(iu, zu); }
  var N = normalize(cross(ddy, ddx));
  if (dot(N, Vd) < 0.0) { N = -N; }

  let th = textureLoad(thickTex, ip, 0);
  let T = th.x;
  let mud = clamp(th.y / max(T, 1e-5), 0.0, 1.0);
  let aer = clamp(th.z / max(T, 1e-5), 0.0, 1.0);
  let L = V.sunDir;
  let sh = shadowAt(P, N);
  // reflection + sun glint
  let cosT = max(dot(N, Vd), 0.0);
  let F = 0.02 + 0.98 * pow(1.0 - cosT, 5.0);
  let refl = skyColor(reflect(-Vd, N), false);
  let spec = V.sunColor * sh * ggx(N, Vd, L, 0.07);
  // refraction: bend the view ray into the water and sample the scene behind. Only offsets whose
  // splash surface is closer than this one's are allowed (no splash pixels bleeding in where the
  // bent ray leaves the silhouette – that showed up as white scalloped rims).
  let uv = fc.xy * V.invScreen;
  let nv = vec2f(dot(N, V.camRight), -dot(N, V.camUp));
  let off = nv * R.refract * min(T, 0.25) * 4.0;
  var behind = textureSampleLevel(sceneColor, linSamp, uv, 0.0).rgb;
  for (var k = 1; k <= 3; k++) {
    let q = clamp(fc.xy + off * (f32(k) / 3.0), vec2f(0.0), V.screen - 1.0);
    let qs = textureLoad(smoothDepth, vec2i(q), 0).r;
    if (qs <= 0.0 || qs > z * 1.02) {
      behind = textureSampleLevel(sceneColor, linSamp, clamp(uv + off * (f32(k) / 3.0) * V.invScreen, vec2f(0.001), vec2f(0.999)), 0.0).rgb;
      break;
    }
  }
  // Beer–Lambert through the splash; turbid / muddy water scatters light back
  let sigma = R.absorb + R.mudAbsorb * mud;
  let trans = exp(-sigma * T);
  let amb = skyColor(vec3f(0.0, 1.0, 0.0), false);
  let lit = V.sunColor * sh * clamp(dot(N, L) * 0.5 + 0.5, 0.0, 1.0) + amb * 0.9;
  let scatterCol = mix(vec3f(0.72, 0.84, 0.86), R.mudColor * 1.7, mud);
  var col = behind * trans + scatterCol * lit * (1.0 - trans) * (R.turbidity + 0.75 * mud);
  // broken-up, aerated water is milky white
  col = mix(col, scatterCol * lit * 0.8, aer * R.whiteness);
  col = mix(col, refl, F * (1.0 - 0.6 * aer)) + spec * (1.0 - 0.5 * aer);
  col = applyFog(col, P);
  let alpha = clamp(T / R.alphaThick, 0.0, 1.0);
  return vec4f(col * alpha, alpha);
}
`;

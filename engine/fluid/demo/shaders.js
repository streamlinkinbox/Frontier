// Demo "engine" shaders (terrain, meshes, sky, shadows, post). They integrate the fluid module the
// way a real engine would: through FluidRenderer.groundWGSL() for wetness/mud/caustics and the
// shared View block.
import { VIEW_STRUCT, SKY_FN, SHADOW_FN, groundWGSL } from '../src/shaders/render.wgsl.js';

const NOISE = /* wgsl */ `
fn h21(p: vec2f) -> f32 { return fract(sin(dot(p, vec2f(127.1, 311.7))) * 43758.5453); }
fn vn(p: vec2f) -> f32 {
  let i = floor(p); let f = fract(p); let u = f * f * (3.0 - 2.0 * f);
  return mix(mix(h21(i), h21(i + vec2f(1.0, 0.0)), u.x), mix(h21(i + vec2f(0.0, 1.0)), h21(i + vec2f(1.0, 1.0)), u.x), u.y);
}
fn fbm3(p: vec2f) -> f32 { return 0.5 * vn(p) + 0.3 * vn(p * 2.1 + 3.7) + 0.2 * vn(p * 4.3 - 1.3); }
fn voro(p: vec2f) -> f32 {
  let ip = floor(p); let fp = fract(p);
  var d = 8.0;
  for (var y = -1; y <= 1; y++) { for (var x = -1; x <= 1; x++) {
    let g = vec2f(f32(x), f32(y));
    let o = vec2f(h21(ip + g), h21(ip + g + 17.0));
    d = min(d, length(g + o - fp));
  } }
  return d;
}
`;

const COMMON = /* wgsl */ `
${VIEW_STRUCT}
@group(0) @binding(0) var<uniform> V: View;
${groundWGSL(1, 0)}
@group(1) @binding(4) var shadowMap: texture_depth_2d;
@group(1) @binding(5) var shadowSamp: sampler_comparison;
${SKY_FN}
${SHADOW_FN}
${NOISE}
fn ambientLight(n: vec3f) -> vec3f {
  let sky = skyColor(normalize(vec3f(n.x * 0.3, 1.0, n.z * 0.3)), false);
  let ground = vec3f(0.16, 0.14, 0.11) * (V.sunColor.r * 0.25);
  return mix(ground, sky, n.y * 0.5 + 0.5) * 0.62;
}
`;

export const TERRAIN = /* wgsl */ `
${COMMON}
struct TOut {
  @builtin(position) pos: vec4f,
  @location(0) world: vec3f,
  @location(1) nrm: vec3f,
  @location(2) mask: vec4f,
};
@vertex fn vs(@location(0) p: vec3f, @location(1) n: vec3f, @location(2) m: vec4f) -> TOut {
  var o: TOut;
  o.pos = V.viewProj * vec4f(p, 1.0);
  o.world = p; o.nrm = n; o.mask = m;
  return o;
}
@fragment fn fs(in: TOut) -> @location(0) vec4f {
  let wp = in.world;
  var n = normalize(in.nrm);
  let nz = fbm3(wp.xz * 0.45);
  let nz2 = fbm3(wp.xz * 2.3 + 11.0);
  let grass = mix(vec3f(0.085, 0.15, 0.035), vec3f(0.17, 0.22, 0.07), nz) * (0.85 + 0.3 * nz2);
  let rock = mix(vec3f(0.24, 0.23, 0.21), vec3f(0.36, 0.34, 0.31), nz2);
  let sand = mix(vec3f(0.56, 0.48, 0.34), vec3f(0.68, 0.60, 0.44), nz) * (0.92 + 0.12 * nz2);
  let dirt = mix(vec3f(0.27, 0.20, 0.13), vec3f(0.36, 0.28, 0.19), nz2) * (0.9 + 0.2 * vn(wp.xz * 6.0));
  let peb = voro(wp.xz * 4.0);
  let pebble = mix(vec3f(0.22, 0.20, 0.17), vec3f(0.44, 0.40, 0.33), smoothstep(0.15, 0.5, peb)) * (0.8 + 0.35 * h21(floor(wp.xz * 4.0)));
  let mudc = vec3f(0.17, 0.12, 0.075) * (0.9 + 0.2 * nz2);
  var albedo = grass;
  albedo = mix(albedo, sand, in.mask.b);
  albedo = mix(albedo, pebble, in.mask.g);
  albedo = mix(albedo, dirt, in.mask.r);
  albedo = mix(albedo, mudc, in.mask.a);
  let slope = 1.0 - n.y;
  albedo = mix(albedo, rock, smoothstep(0.32, 0.5, slope + (nz - 0.5) * 0.15) * (1.0 - in.mask.r));

  // fluid interaction: wet darkening, mud stains, caustics / underwater light
  let fg = fluidGround(wp);
  let wet = fg.wet * fg.inside;
  albedo = mix(albedo, mudc, clamp(fg.mud * 1.4, 0.0, 1.0) * smoothstep(0.02, 0.4, wet) * 0.85);
  let porous = 1.0 - in.mask.g * 0.4;
  albedo *= mix(1.0, 0.5, wet * porous);

  let sh = shadowAt(wp, n);
  let ndl = max(dot(n, V.sunDir), 0.0);
  let sun = V.sunColor * ndl * sh * fg.light;
  var col = albedo * (sun + ambientLight(n));
  // wet specular sheen
  let Vd = normalize(V.camPos - wp);
  let H = normalize(Vd + V.sunDir);
  let gloss = mix(10.0, 140.0, wet);
  let fres = 0.04 + 0.96 * pow(1.0 - max(dot(n, Vd), 0.0), 5.0);
  col += V.sunColor * pow(max(dot(n, H), 0.0), gloss) * (gloss + 8.0) / 25.0 * fres * (0.03 + 0.9 * wet) * sh;
  col += skyColor(reflect(-Vd, n), false) * fres * wet * 0.35;
  col = applyFog(col, wp);
  return vec4f(col, 1.0);
}
`;

export const MESH = /* wgsl */ `
${COMMON}
struct MOut {
  @builtin(position) pos: vec4f,
  @location(0) world: vec3f,
  @location(1) nrm: vec3f,
  @location(2) color: vec4f,
  @location(3) params: vec4f,
  @location(4) lpos: vec3f,
};
@vertex fn vs(@location(0) p: vec3f, @location(1) n: vec3f,
              @location(2) m0: vec4f, @location(3) m1: vec4f, @location(4) m2: vec4f, @location(5) m3: vec4f,
              @location(6) color: vec4f, @location(7) params: vec4f) -> MOut {
  let M = mat4x4f(m0, m1, m2, m3);
  let wp = M * vec4f(p, 1.0);
  var o: MOut;
  o.pos = V.viewProj * wp;
  o.world = wp.xyz;
  o.nrm = normalize((M * vec4f(n, 0.0)).xyz);
  o.color = color; o.params = params; o.lpos = p;
  return o;
}
@fragment fn fs(in: MOut) -> @location(0) vec4f {
  var n = normalize(in.nrm);
  let wp = in.world;
  var albedo = in.color.rgb;
  var rough = in.params.x;
  var metal = in.params.y;
  let pattern = i32(in.params.z + 0.5);
  if (pattern == 1) {
    // wooden crate: planks + frame
    let q = abs(in.lpos);
    let edge = max(max(step(0.40, q.x) * step(0.40, q.y), step(0.40, q.y) * step(0.40, q.z)), step(0.40, q.x) * step(0.40, q.z));
    let plank = 0.85 + 0.15 * sin((in.lpos.y + in.lpos.x * 0.2) * 40.0);
    albedo *= mix(plank * (0.85 + 0.3 * vn(in.lpos.xz * 9.0 + in.lpos.y * 3.0)), 0.6, edge);
  } else if (pattern == 2) {
    // tyre: dark rubber with a light rim disc on the side
    let r = length(in.lpos.yz);
    let side = abs(in.lpos.x) > 0.11;
    if (side && r < 0.22) { albedo = vec3f(0.55, 0.56, 0.58); rough = 0.3; }
    else if (side && r < 0.25) { albedo = vec3f(0.05); }
    else { albedo = vec3f(0.035) * (0.8 + 0.4 * step(0.5, fract(atan2(in.lpos.y, in.lpos.z) * 6.0))); }
  } else if (pattern == 3) {
    // barrel bands
    let band = step(0.9, fract(in.lpos.x * 2.6 + 0.45));
    albedo = mix(albedo, albedo * 0.45, band);
  }
  // mud caked on from driving through the mud pit (instance colour alpha), thickest low down
  let coat = in.color.a;
  if (coat > 0.0) {
    let hgt = select(in.lpos.y + 0.5, 0.0, pattern == 2);
    let n2 = vn(in.lpos.xz * 23.0 + in.lpos.y * 7.0);
    let m = smoothstep(0.0, 0.25, coat * 1.3 - hgt * 0.9 + (n2 - 0.5) * 0.35);
    albedo = mix(albedo, vec3f(0.13, 0.09, 0.055) * (0.8 + 0.4 * n2), m);
    rough = mix(rough, 0.85, m);
    metal = mix(metal, 0.0, m);
  }
  let fg = fluidGround(wp);
  // mud / wet coating on the lower part of objects that have been in mud or water
  let under = select(0.0, 1.0, wp.y < fg.surface && fg.depth > 0.003);
  let sh = shadowAt(wp, n);
  let ndl = max(dot(n, V.sunDir), 0.0);
  var light = V.sunColor * sh;
  if (under > 0.5) { light *= fg.light * exp(-vec3f(0.42, 0.075, 0.035) * (fg.surface - wp.y) * 2.0); }
  let Vd = normalize(V.camPos - wp);
  let H = normalize(Vd + V.sunDir);
  let f0 = mix(vec3f(0.04), albedo, metal);
  let fres = f0 + (1.0 - f0) * pow(1.0 - max(dot(n, Vd), 0.0), 5.0);
  let gloss = 2.0 / max(rough * rough, 0.002) - 2.0;
  let spec = pow(max(dot(n, H), 0.0), gloss) * (gloss + 8.0) / 25.0;
  var col = albedo * (1.0 - metal) * (light * ndl + ambientLight(n)) + light * fres * spec * ndl;
  col += skyColor(reflect(-Vd, n), false) * fres * (1.0 - rough) * 0.8;
  col += albedo * in.params.w;
  col = applyFog(col, wp);
  return vec4f(col, 1.0);
}
`;

export const SKY = /* wgsl */ `
${VIEW_STRUCT}
@group(0) @binding(0) var<uniform> V: View;
${SKY_FN}
struct SOut { @builtin(position) pos: vec4f, @location(0) ndc: vec2f };
@vertex fn vs(@builtin(vertex_index) vi: u32) -> SOut {
  var p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
  var o: SOut;
  o.pos = vec4f(p[vi], 0.0, 1.0);
  o.ndc = p[vi];
  return o;
}
@fragment fn fs(in: SOut) -> @location(0) vec4f {
  let a = V.invViewProj * vec4f(in.ndc, 1.0, 1.0);
  let b = V.invViewProj * vec4f(in.ndc, 0.0001, 1.0);
  let d = normalize(b.xyz / b.w - a.xyz / a.w);
  var c = skyColor(d, true);
  // soft high clouds
  if (d.y > 0.0) {
    let uv = d.xz / (d.y + 0.08) * 0.6 + vec2f(V.time * 0.004, 0.0);
    var cl = 0.0; var amp = 0.5; var f = 1.0;
    for (var o = 0; o < 4; o++) {
      let q = uv * f;
      let i = floor(q); let fr = fract(q); let u = fr * fr * (3.0 - 2.0 * fr);
      let h = fract(sin(dot(i, vec2f(127.1, 311.7))) * 43758.5);
      let h1 = fract(sin(dot(i + vec2f(1.0, 0.0), vec2f(127.1, 311.7))) * 43758.5);
      let h2 = fract(sin(dot(i + vec2f(0.0, 1.0), vec2f(127.1, 311.7))) * 43758.5);
      let h3 = fract(sin(dot(i + vec2f(1.0, 1.0), vec2f(127.1, 311.7))) * 43758.5);
      cl += amp * mix(mix(h, h1, u.x), mix(h2, h3, u.x), u.y);
      f *= 2.1; amp *= 0.5;
    }
    let cov = smoothstep(0.52, 0.85, cl) * smoothstep(0.0, 0.25, d.y);
    c = mix(c, V.sunColor * 0.42 + V.skyHorizon * 0.5, cov * 0.75);
  }
  return vec4f(c, 1.0);
}
`;

export const SHADOW = /* wgsl */ `
@group(0) @binding(0) var<uniform> lightVP: mat4x4f;
@vertex fn vsTerrain(@location(0) p: vec3f) -> @builtin(position) vec4f { return lightVP * vec4f(p, 1.0); }
@vertex fn vsMesh(@location(0) p: vec3f, @location(2) m0: vec4f, @location(3) m1: vec4f, @location(4) m2: vec4f, @location(5) m3: vec4f) -> @builtin(position) vec4f {
  return lightVP * (mat4x4f(m0, m1, m2, m3) * vec4f(p, 1.0));
}
`;

export const POST = /* wgsl */ `
struct Post { exposure: f32, srgb: f32, vignette: f32, time: f32 };
@group(0) @binding(0) var hdr: texture_2d<f32>;
@group(0) @binding(1) var<uniform> P: Post;
@vertex fn vs(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4f {
  var p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
  return vec4f(p[vi], 0.0, 1.0);
}
fn aces(x: vec3f) -> vec3f {
  let a = 2.51; let b = 0.03; let c = 2.43; let d = 0.59; let e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3f(0.0), vec3f(1.0));
}
@fragment fn fs(@builtin(position) fc: vec4f) -> @location(0) vec4f {
  let dims = vec2f(textureDimensions(hdr));
  var c = textureLoad(hdr, vec2i(fc.xy), 0).rgb * P.exposure;
  // cheap bloom-ish glow from bright neighbours (sun glints on water)
  var glow = vec3f(0.0);
  for (var i = 1; i <= 4; i++) {
    let o = f32(i) * 3.0;
    glow += max(textureLoad(hdr, vec2i(clamp(fc.xy + vec2f(o, 0.0), vec2f(0.0), dims - 1.0)), 0).rgb - 4.0, vec3f(0.0));
    glow += max(textureLoad(hdr, vec2i(clamp(fc.xy - vec2f(o, 0.0), vec2f(0.0), dims - 1.0)), 0).rgb - 4.0, vec3f(0.0));
    glow += max(textureLoad(hdr, vec2i(clamp(fc.xy + vec2f(0.0, o), vec2f(0.0), dims - 1.0)), 0).rgb - 4.0, vec3f(0.0));
    glow += max(textureLoad(hdr, vec2i(clamp(fc.xy - vec2f(0.0, o), vec2f(0.0), dims - 1.0)), 0).rgb - 4.0, vec3f(0.0));
  }
  c += glow * 0.012 * P.exposure;
  c = aces(c);
  let uv = fc.xy / dims - 0.5;
  c *= 1.0 - P.vignette * dot(uv, uv) * 1.2;
  if (P.srgb > 0.5) { c = pow(c, vec3f(1.0 / 2.2)); }
  // dither against banding
  let n = fract(sin(dot(fc.xy + P.time, vec2f(12.9898, 78.233))) * 43758.5453) - 0.5;
  c += n / 255.0;
  return vec4f(c, 1.0);
}
`;

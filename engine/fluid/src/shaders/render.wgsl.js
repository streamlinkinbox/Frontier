// Rendering shaders: water surface, caustics, particles, and helper snippets for engine shaders.

// Camera / lighting block shared by all fluid render pipelines (FluidRenderer.setView packs it).
export const VIEW_STRUCT = /* wgsl */ `
struct View {
  viewProj: mat4x4f,
  invViewProj: mat4x4f,
  shadowMat: mat4x4f,
  camPos: vec3f, near: f32,
  sunDir: vec3f, time: f32,
  sunColor: vec3f, exposure: f32,
  skyZenith: vec3f, fogDensity: f32,
  skyHorizon: vec3f, shadowsOn: f32,
  camRight: vec3f, fogHeight: f32,
  camUp: vec3f, pad0: f32,
  camFwd: vec3f, pad1: f32,
  screen: vec2f, invScreen: vec2f,
};
`;

// Simple analytic sky. Engines can replace this with their own environment lookup.
export const SKY_FN = /* wgsl */ `
fn skyColor(d: vec3f, withSun: bool) -> vec3f {
  let y = d.y;
  var c = mix(V.skyHorizon, V.skyZenith, pow(clamp(y, 0.0, 1.0), 0.45));
  if (y < 0.0) { c = mix(V.skyHorizon, V.skyHorizon * vec3f(0.42, 0.42, 0.40), clamp(-y * 4.0, 0.0, 1.0)); }
  let mu = max(dot(d, V.sunDir), 0.0);
  c += V.sunColor * (0.035 * pow(mu, 5.0) + 0.10 * pow(mu, 48.0));
  if (withSun) { c += V.sunColor * 9.0 * smoothstep(0.99955, 0.99988, mu); }
  return c;
}
fn applyFog(col: vec3f, worldPos: vec3f) -> vec3f {
  let dist = distance(worldPos, V.camPos);
  let dir = (worldPos - V.camPos) / max(dist, 1e-3);
  // exponential height fog
  let hf = exp(-max(worldPos.y, 0.0) * V.fogHeight);
  let f = 1.0 - exp(-dist * V.fogDensity * (0.35 + 0.65 * hf));
  let fogCol = skyColor(normalize(vec3f(dir.x, max(dir.y, 0.02), dir.z)), false) * 0.95;
  return mix(col, fogCol, clamp(f, 0.0, 1.0));
}
`;

// Shadow lookup (3x3 PCF). Requires shadowMap/shadowSamp bindings and V.
export const SHADOW_FN = /* wgsl */ `
fn shadowAt(wp: vec3f, nrm: vec3f) -> f32 {
  if (V.shadowsOn < 0.5) { return 1.0; }
  let sp = V.shadowMat * vec4f(wp + nrm * 0.06, 1.0);
  let uv = sp.xy * vec2f(0.5, -0.5) + 0.5;
  if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0 || sp.z > 1.0) { return 1.0; }
  let ts = 1.0 / vec2f(textureDimensions(shadowMap));
  var s = 0.0;
  for (var y = -1; y <= 1; y++) {
    for (var x = -1; x <= 1; x++) {
      s += textureSampleCompareLevel(shadowMap, shadowSamp, uv + vec2f(f32(x), f32(y)) * ts, sp.z - 0.0015);
    }
  }
  return s / 9.0;
}
`;

const WATER_STRUCT = /* wgsl */ `
struct Water {
  origin: vec2f, cell: f32, meshStep: f32,
  size: vec2f, nx: f32, ny: f32,
  absorb: vec3f, refractScale: f32,
  scatter: vec3f, roughness: f32,
  mudAbsorb: vec3f, mudRough: f32,
  mudColor: vec3f, debugMode: f32,
  seaLevel: f32, waveDepth: f32, numWaves: f32, waveRamp: f32,
  meshW: f32, meshH: f32, epsRender: f32, causticsOn: f32,
  mudScatter: vec3f, bubble: f32,
  waveA: array<vec4f, 4>,
  waveB: array<vec4f, 4>,
};
`;

export const WATER = /* wgsl */ `
${VIEW_STRUCT}
${WATER_STRUCT}
@group(0) @binding(0) var<uniform> V: View;
@group(0) @binding(1) var<uniform> W: Water;
@group(0) @binding(2) var surfTex: texture_2d<f32>;
@group(0) @binding(3) var shadeTex: texture_2d<f32>;
@group(0) @binding(4) var sceneColor: texture_2d<f32>;
@group(0) @binding(5) var sceneDepth: texture_depth_2d;
@group(0) @binding(6) var linSamp: sampler;
@group(0) @binding(7) var shadowMap: texture_depth_2d;
@group(0) @binding(8) var shadowSamp: sampler_comparison;
@group(0) @binding(9) var foamA: texture_2d<f32>;     // screen-space foam splat (FOAM_SPLAT)
@group(0) @binding(10) var foamB: texture_2d<f32>;
${SKY_FN}
${SHADOW_FN}

struct VOut {
  @builtin(position) pos: vec4f,
  @location(0) world: vec3f,
  @location(1) data: vec4f,    // depth, mud, outer flag, wetness
};

// incident swell outside the simulated domain (same components that drive the wave-maker)
fn outerWave(xz: vec2f) -> vec3f {
  var e = 0.0; var g = vec2f(0.0);
  let lp = xz - W.origin;
  for (var n = 0; n < i32(W.numWaves); n++) {
    let A = W.waveA[n]; let B = W.waveB[n];
    let ph = A.w * dot(A.yz, lp) - B.x * V.time + B.y;
    let a = A.x * W.waveRamp;
    e += a * sin(ph);
    g += A.yz * (a * A.w * cos(ph));
  }
  return vec3f(e, g);
}

@vertex fn vsGrid(@builtin(vertex_index) vi: u32) -> VOut {
  let mw = u32(W.meshW);
  let gx = i32(vi % mw); let gz = i32(vi / mw);
  let st = i32(W.meshStep);
  let tx = min(gx * st, i32(W.nx) - 1);
  let tz = min(gz * st, i32(W.ny) - 1);
  let s = textureLoad(surfTex, vec2i(tx, tz), 0);
  let wp = vec3f(W.origin.x + (f32(tx) + 0.5) * W.cell, s.x, W.origin.y + (f32(tz) + 0.5) * W.cell);
  var o: VOut;
  o.pos = V.viewProj * vec4f(wp, 1.0);
  o.world = wp;
  o.data = vec4f(s.y, s.z, 0.0, s.w);
  return o;
}

@vertex fn vsOuter(@location(0) xz: vec2f) -> VOut {
  let w = outerWave(xz);
  let wp = vec3f(xz.x, W.seaLevel + w.x, xz.y);
  var o: VOut;
  o.pos = V.viewProj * vec4f(wp, 1.0);
  o.world = wp;
  o.data = vec4f(50.0, 0.0, 1.0, 1.0);
  return o;
}

fn ggx(N: vec3f, Vd: vec3f, L: vec3f, rough: f32) -> f32 {
  let H = normalize(Vd + L);
  let nh = max(dot(N, H), 0.0); let nl = max(dot(N, L), 0.0); let nv = max(dot(N, Vd), 1e-3);
  let a = max(rough * rough, 0.0015); let a2 = a * a;
  let dd = nh * nh * (a2 - 1.0) + 1.0;
  let D = a2 / (3.14159 * dd * dd);
  let k = a * 0.5;
  let G = (nl / (nl * (1.0 - k) + k)) * (nv / (nv * (1.0 - k) + k));
  let F = 0.02 + 0.98 * pow(1.0 - max(dot(H, Vd), 0.0), 5.0);
  return min(D * G * F / (4.0 * nv), 600.0);
}

fn mh(p: vec2f) -> f32 { return fract(sin(dot(p, vec2f(127.1, 311.7))) * 43758.5453); }
fn mnoise(p: vec2f) -> f32 {
  let i = floor(p); let f = fract(p); let u = f * f * (3.0 - 2.0 * f);
  let n = mix(mix(mh(i), mh(i + vec2f(1.0, 0.0)), u.x), mix(mh(i + vec2f(0.0, 1.0)), mh(i + vec2f(1.0, 1.0)), u.x), u.y);
  return n + 0.5 * mix(mix(mh(i * 2.0 + 7.0), mh(i * 2.0 + vec2f(8.0, 7.0)), u.x), mix(mh(i * 2.0 + vec2f(7.0, 8.0)), mh(i * 2.0 + 8.0), u.x), u.y);
}
fn heat(t: f32) -> vec3f {
  let x = clamp(t, 0.0, 1.0);
  return clamp(vec3f(1.5 - abs(4.0 * x - 3.0), 1.5 - abs(4.0 * x - 2.0), 1.5 - abs(4.0 * x - 1.0)), vec3f(0.0), vec3f(1.0));
}

@fragment fn fsWater(in: VOut) -> @location(0) vec4f {
  let fc = in.pos;
  let suv = fc.xy * V.invScreen;
  let isOuter = in.data.z > 0.5;
  var grad = vec2f(0.0);
  var flow = vec4f(0.0);
  if (isOuter) {
    grad = outerWave(in.world.xz).yz;
  } else {
    let uv = (in.world.xz - W.origin) / W.size;
    flow = textureSampleLevel(shadeTex, linSamp, uv, 0.0);
    grad = flow.xy;
  }
  let N = normalize(vec3f(-grad.x, 1.0, -grad.y));
  let toCam = V.camPos - in.world;
  let dist = length(toCam);
  let Vd = toCam / dist;
  let depthH = in.data.x;
  let mud = clamp(in.data.y, 0.0, 1.0);

  // ---- mud micro-relief (granular lumps; material texture, not waves)
  let mudSurf = smoothstep(0.35, 0.9, mud);
  var Nm = N;
  if (mudSurf > 0.0) {
    let q = in.world.xz * 2.7;
    let e = 0.05;
    let g2 = vec2f(mnoise(q + vec2f(e, 0.0)) - mnoise(q - vec2f(e, 0.0)), mnoise(q + vec2f(0.0, e)) - mnoise(q - vec2f(0.0, e))) / (2.0 * e);
    Nm = normalize(N + vec3f(-g2.x, 0.0, -g2.y) * 0.035 * mudSurf);
  }

  // ---- scene behind the surface (screen-space refraction along the refracted ray)
  let dims = vec2i(textureDimensions(sceneDepth));
  let pix = clamp(vec2i(fc.xy), vec2i(0), dims - 1);
  let dScene = textureLoad(sceneDepth, pix, 0);
  let waterDist = V.near / max(fc.z, 1e-7);
  let sceneDist = V.near / max(dScene, 1e-7);
  let thick0 = max(sceneDist - waterDist, 0.0);
  var thick = thick0;
  let rd = refract(-Vd, N, 1.0 / 1.333);
  let pr = in.world + rd * min(thick, 4.0) * W.refractScale;
  let cp = V.viewProj * vec4f(pr, 1.0);
  var ruv = cp.xy / cp.w * vec2f(0.5, -0.5) + 0.5;
  ruv = clamp(ruv, vec2f(0.001), vec2f(0.999));
  let rpix = clamp(vec2i(ruv * vec2f(dims)), vec2i(0), dims - 1);
  let dR = textureLoad(sceneDepth, rpix, 0);
  let sceneDistR = V.near / max(dR, 1e-7);
  if (sceneDistR < waterDist) { ruv = suv; } else { thick = max(sceneDistR - waterDist, 0.0); }
  let path = max(thick, depthH * 0.5);
  let behind = textureSampleLevel(sceneColor, linSamp, ruv, 0.0).rgb;
  let straight = textureSampleLevel(sceneColor, linSamp, suv, 0.0).rgb;

  // ---- lighting terms
  let L = V.sunDir;
  let sh = shadowAt(in.world, vec3f(0.0, 1.0, 0.0));
  let sunI = V.sunColor * sh;
  let amb = skyColor(vec3f(0.0, 1.0, 0.0), false) * 0.55;

  // ---- participating medium: absorption + scattering (water + suspended sediment)
  let sigA = W.absorb + W.mudAbsorb * mud;
  let sigS = W.scatter + W.mudScatter * mud;
  let sigT = sigA + sigS;
  let T = exp(-sigT * path);
  let ssAlbedo = sigS / max(sigT, vec3f(1e-4));
  let srcL = sunI * (0.06 + 0.12 * max(L.y, 0.0)) + amb * 0.32;
  let inscat = ssAlbedo * (1.0 - T) * srcL;
  var refr = behind * T + inscat;

  // ---- aerated water: fresh whitewater drags clouds of bubbles under the surface, which scatter
  // light strongly with little absorption -> the water under and around breaking foam turns pale,
  // milky aquamarine (amount from the foam particles splatted at this pixel)
  let fz = textureLoad(foamA, pix, 0).x * (1.0 / 16.0);        // stored x16 (FOAM_SPLAT)
  let fzFresh = textureLoad(foamB, pix, 0).x * (1.0 / 16.0);
  let aer = (1.0 - exp(-(fz * 0.015 + fzFresh * 0.35))) * W.bubble * (1.0 - mudSurf);
  let bubbleL = (sunI * (0.10 + 0.28 * max(L.y, 0.0)) + amb * 0.95) * mix(vec3f(0.6, 0.86, 0.84), vec3f(0.62, 0.55, 0.42), mud);
  refr = mix(refr, bubbleL, clamp(aer, 0.0, 0.85));

  // ---- reflection
  var R = reflect(-Vd, Nm);
  R.y = max(R.y, 0.015);
  let refl = skyColor(normalize(R), false);
  let cosT = max(dot(Nm, Vd), 0.0);
  let F = 0.02 + 0.98 * pow(1.0 - cosT, 5.0);
  let rough = mix(W.roughness + 0.04 * clamp(flow.w * 0.2, 0.0, 1.0), W.mudRough, mudSurf);
  let spec = sunI * ggx(Nm, Vd, L, rough);
  var col = mix(refr, refl, F) + spec;

  // ---- thick mud reads as an opaque, wet, diffuse surface
  if (mudSurf > 0.0) {
    let nl = max(dot(Nm, L), 0.0);
    let md = W.mudColor * (sunI * nl + amb * 0.9) + spec * 0.5 + refl * F;
    col = mix(col, md, mudSurf);
  }

  // ---- debug views
  let dm = i32(W.debugMode);
  if (dm == 1) { col = heat(depthH / 2.0) * (0.4 + 0.6 * N.y); }
  else if (dm == 2) { col = heat(flow.z / 4.0); }
  else if (dm == 3) { col = mix(vec3f(0.1, 0.35, 0.9), vec3f(0.55, 0.32, 0.12), mud); }
  else if (dm == 4) { col = N * 0.5 + 0.5; }

  col = applyFog(col, in.world);
  // shoreline / thin films: fade by the true water depth at this pixel (from the depth buffer),
  // so the waterline follows the real intersection with the terrain (rendered wet underneath)
  let vdepth = thick0 * max(Vd.y, 0.06);
  let a = select(smoothstep(0.0, 0.03, vdepth) * smoothstep(0.0, W.epsRender, depthH + W.epsRender * 0.5), 1.0, isOuter);
  return vec4f(mix(straight, col, a), 1.0);
}
`;

// ---------------------------------------------------------------------------------------------
// Caustics: refract sunlight through the simulated surface, project onto the bed, and measure the
// change of area of each projected triangle (dpdx/dpdy) -> focused/defocused light intensity.
export const CAUSTICS = /* wgsl */ `
struct CP {
  origin: vec2f, size: vec2f,
  nx: f32, ny: f32, meshW: f32, meshH: f32,
  sunDir: vec3f, ior: f32,
  absorb: vec3f, intensity: f32,
  epsRender: f32, p0: f32, p1: f32, p2: f32,
};
@group(0) @binding(0) var<uniform> C: CP;
@group(0) @binding(1) var surfTex: texture_2d<f32>;
@group(0) @binding(2) var shadeTex: texture_2d<f32>;
@group(0) @binding(3) var bedTex: texture_2d<f32>;
@group(0) @binding(4) var linSamp: sampler;

fn loadBed(p: vec2i) -> f32 { return textureLoad(bedTex, clamp(p, vec2i(0), vec2i(i32(C.nx) - 1, i32(C.ny) - 1)), 0).x; }
fn bedAt(xz: vec2f) -> f32 {
  let f = (xz - C.origin) / C.size * vec2f(C.nx, C.ny) - 0.5;
  let fl = floor(f); let t = f - fl; let i = vec2i(fl);
  return mix(mix(loadBed(i), loadBed(i + vec2i(1, 0)), t.x), mix(loadBed(i + vec2i(0, 1)), loadBed(i + vec2i(1, 1)), t.x), t.y);
}
fn loadSurf(p: vec2i) -> vec4f { return textureLoad(surfTex, clamp(p, vec2i(0), vec2i(i32(C.nx) - 1, i32(C.ny) - 1)), 0); }
fn surfAt(uv: vec2f) -> vec4f {
  let f = uv * vec2f(C.nx, C.ny) - 0.5;
  let fl = floor(f); let t = f - fl; let i = vec2i(fl);
  return mix(mix(loadSurf(i), loadSurf(i + vec2i(1, 0)), t.x), mix(loadSurf(i + vec2i(0, 1)), loadSurf(i + vec2i(1, 1)), t.x), t.y);
}
fn trace(P: vec3f, d: vec3f) -> vec3f {
  let dy = max(-d.y, 0.05);
  var t = max(P.y - bedAt(P.xz), 0.0) / dy;
  for (var it = 0; it < 3; it++) {
    let q = P + d * t;
    t = max(P.y - bedAt(q.xz), 0.0) / dy;
  }
  return P + d * t;
}
struct COut {
  @builtin(position) pos: vec4f,
  @location(0) oldP: vec2f,
  @location(1) newP: vec2f,
  @location(2) atten: vec3f,
};
@vertex fn vs(@builtin(vertex_index) vi: u32) -> COut {
  let mw = u32(C.meshW);
  let g = vec2f(f32(vi % mw), f32(vi / mw));
  let uv = (g + 0.5) / vec2f(C.meshW, C.meshH);
  let s = surfAt(uv);
  let gr = textureSampleLevel(shadeTex, linSamp, uv, 0.0).xy;
  let xz = C.origin + uv * C.size;
  let P = vec3f(xz.x, s.x, xz.y);
  let N = normalize(vec3f(-gr.x, 1.0, -gr.y));
  let Ld = -C.sunDir;
  let rr = refract(Ld, N, 1.0 / C.ior);
  let rf = refract(Ld, vec3f(0.0, 1.0, 0.0), 1.0 / C.ior);
  let hitR = trace(P, rr);
  let hitF = trace(P, rf);
  var o: COut;
  let tuv = (hitR.xz - C.origin) / C.size;
  o.pos = vec4f(tuv.x * 2.0 - 1.0, 1.0 - tuv.y * 2.0, 0.0, 1.0);
  o.oldP = hitF.xz;
  o.newP = hitR.xz;
  let wet = smoothstep(C.epsRender, 0.02, s.y);
  let pathLen = distance(P, hitR);
  // Fresnel transmission at the surface
  let ct = max(dot(-Ld, N), 0.0);
  let Ft = 1.0 - (0.02 + 0.98 * pow(1.0 - ct, 5.0));
  o.atten = exp(-C.absorb * pathLen) * wet * Ft * C.intensity;
  return o;
}
fn cross2(a: vec2f, b: vec2f) -> f32 { return a.x * b.y - a.y * b.x; }
@fragment fn fs(in: COut) -> @location(0) vec4f {
  let oldA = abs(cross2(dpdx(in.oldP), dpdy(in.oldP)));
  let newA = abs(cross2(dpdx(in.newP), dpdy(in.newP)));
  let I = oldA / max(newA, 1e-10);
  return vec4f(in.atten * min(I, 40.0), 1.0);
}
`;

// ---------------------------------------------------------------------------------------------
export const PARTICLES = /* wgsl */ `
${VIEW_STRUCT}
struct Particle {
  pos: vec3f, life: f32,
  vel: vec3f, maxLife: f32,
  size: f32, vol: f32, mud: f32, kind: u32,
};
struct PR {
  sprayColor: vec3f, shutter: f32,
  foamColor: vec3f, foamOpacity: f32,
  mudColor: vec3f, sprayOpacity: f32,
  minPixel: f32, softness: f32, foamGrow: f32, mistOpacity: f32,
};
@group(0) @binding(0) var<uniform> V: View;
@group(0) @binding(1) var<uniform> R: PR;
@group(0) @binding(2) var<storage, read> particles: array<Particle>;
@group(0) @binding(3) var<storage, read> drawList: array<u32>;
@group(0) @binding(4) var sceneDepth: texture_depth_2d;
${SKY_FN}

struct POut {
  @builtin(position) pos: vec4f,
  @location(0) uv: vec2f,
  @location(1) color: vec4f,
  @location(2) @interpolate(flat) info: vec4f,   // kind, seed, viewDist, life fraction
};

fn hashf(n: u32) -> f32 {
  var x = n; x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u;
  return f32(x) * (1.0 / 4294967295.0);
}

@vertex fn vs(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> POut {
  let pid = drawList[ii];
  let p = particles[pid];
  var o: POut;
  // foam rafts are rendered as a continuous surface by FOAM_SPLAT + FOAM_COMPOSITE
  if (p.kind == 2u) { o.pos = vec4f(2.0, 2.0, 2.0, 1.0); return o; }
  var corners = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
  let c = corners[vi];
  let lifeF = clamp(p.life / max(p.maxLife, 1e-3), 0.0, 1.0);
  let dist = max(distance(p.pos, V.camPos), 0.1);
  // keep particles at least ~minPixel wide, fading their opacity to conserve energy
  let pixWorld = dist * 2.0 * V.invScreen.y / max(V.viewProj[1][1], 1e-3);
  var size = p.size;
  var alpha = 1.0;
  let minW = R.minPixel * pixWorld;
  if (size < minW) { alpha = (size * size) / (minW * minW); size = minW; }
  var wp: vec3f;
  let sunUp = max(V.sunDir.y, 0.0);
  let amb = skyColor(vec3f(0.0, 1.0, 0.0), false);
  var col: vec3f;
  {
    // camera-facing, stretched along screen-space motion (motion blur at shutter speed)
    let v = p.vel;
    let vp = v - V.camFwd * dot(v, V.camFwd);
    let vl = length(vp);
    var axis = V.camUp;
    if (vl > 1e-3) { axis = vp / vl; }
    let side = normalize(cross(axis, V.camFwd));
    var halfLen = size + vl * R.shutter * 0.5;
    if (p.kind == 3u) { halfLen = size; axis = V.camUp; }
    wp = p.pos + side * c.x * size + normalize(axis) * c.y * halfLen;
    alpha *= size / halfLen;
    let toCam = normalize(V.camPos - p.pos);
    let fwdScatter = pow(max(dot(-toCam, V.sunDir), 0.0), 6.0);
    if (p.kind == 4u) {
      alpha *= 0.3;
      col = (amb * 0.9 + V.sunColor * 0.25) * R.sprayColor;
    } else if (p.kind == 3u) {
      alpha *= R.mistOpacity * lifeF;
      col = (amb * 0.8 + V.sunColor * (0.3 + 1.2 * fwdScatter)) * R.sprayColor;
    } else {
      alpha *= R.sprayOpacity * smoothstep(0.0, 0.15, lifeF);
      // droplets are clear water: bright where they scatter sunlight forward, otherwise mostly sky
      let water = (amb * 0.8 + V.sunColor * (0.14 + 1.5 * fwdScatter)) * R.sprayColor;
      let mudC = R.mudColor * (amb * 0.6 + V.sunColor * (0.2 + 0.8 * sunUp));
      col = mix(water, mudC, clamp(p.mud * 1.8, 0.0, 1.0));
      if (p.mud > 0.3) { alpha = min(1.0, alpha * 1.6); }
    }
  }
  o.pos = V.viewProj * vec4f(wp, 1.0);
  o.uv = c;
  o.color = vec4f(col, alpha);
  o.info = vec4f(f32(p.kind), hashf(pid), dist, lifeF);
  return o;
}

@fragment fn fs(in: POut) -> @location(0) vec4f {
  let r = length(in.uv);
  if (r > 1.0) { discard; }
  let kind = u32(in.info.x + 0.5);
  var a = in.color.a;
  if (kind == 3u) {
    a *= (1.0 - r) * (1.0 - r);
  } else {
    // a spray particle is a cluster of droplets: soft, denser in the middle
    let q = 1.0 - r * r;
    a *= q * q;
  }
  // soft particles against opaque geometry
  let dims = vec2i(textureDimensions(sceneDepth));
  let d = textureLoad(sceneDepth, clamp(vec2i(in.pos.xy), vec2i(0), dims - 1), 0);
  let sceneDist = V.near / max(d, 1e-7);
  let partDist = V.near / max(in.pos.z, 1e-7);
  a *= clamp((sceneDist - partDist) / R.softness, 0.0, 1.0);
  if (a < 0.002) { discard; }
  return vec4f(in.color.rgb * a, a);
}
`;

// ---------------------------------------------------------------------------------------------
// Particle foam, rendered as a continuous surface.
//
// The foam is the simulated foam particles ("rafts"): where they are, how dense, how old, how they
// drift, stretch, rotate and decay all comes from FluidParticles. Each raft is splatted into two
// screen-space buffers as a small patch lying on the water surface. Every raft carries its own
// randomly placed and rotated window of the foam micro-structure (FoamPattern), so the structure
// moves exactly with the particles – Lagrangian texture advection (Yu, Neyret, Bruneton &
// Holzschuch 2011). Overlapping patches are combined with their variance-preserving blend
//      P = mean + Σ w (P_i − mean) / sqrt(Σ w²)
// which keeps the structure crisp however many rafts overlap. The composite then shows foam where
// the pattern lies below the local foam density (coverage = P(pattern < density)): dense fresh
// foam is solid with a frayed rim, thinning foam opens up into lace along the bubble-raft walls,
// and the last of it breaks into filaments and specks before it vanishes.
//   foamA = (Σw, Σw², Σw(Pmarble − ½), Σw(Place − ½))   foamB = (Σw·fresh, Σw·mud, Σw·dist/64, Σw(Pbubble − ½))
// The buffers are rgba16float (blendable everywhere): weights are stored x16 (FOAM_W) so that w²
// of a kernel's faint tail stays in the normal half-float range, and the tail itself is cut off.
const PARTICLE_STRUCT = /* wgsl */ `
struct Particle {
  pos: vec3f, life: f32,
  vel: vec3f, maxLife: f32,
  size: f32, vol: f32, mud: f32, kind: u32,
};
`;

export const FOAM_SPLAT = /* wgsl */ `
${VIEW_STRUCT}
${PARTICLE_STRUCT}
struct FoamSplat {
  origin: vec2f, size: vec2f,
  tile: f32, growth: f32, freshSeconds: f32, lift: f32,
  amount: f32, fadeIn: f32, thinning: f32, pad1: f32,
};
@group(0) @binding(0) var<uniform> V: View;
@group(0) @binding(1) var<uniform> FS: FoamSplat;
@group(0) @binding(2) var<storage, read> particles: array<Particle>;
@group(0) @binding(3) var<storage, read> drawList: array<u32>;
@group(0) @binding(4) var shadeTex: texture_2d<f32>;
@group(0) @binding(5) var linSamp: sampler;
@group(0) @binding(6) var patternTex: texture_2d<f32>;
@group(0) @binding(7) var patternSamp: sampler;

struct SOut {
  @builtin(position) pos: vec4f,
  @location(0) local: vec2f,                     // kernel coordinates, unit disc
  @location(1) puv: vec2f,                       // this raft's window into the foam pattern
  @location(2) dist: f32,                        // distance to the camera
  @location(3) @interpolate(flat) prm: vec4f,    // amount, fresh, mud
};
struct SplatOut { @location(0) a: vec4f, @location(1) b: vec4f };

fn hashu(n: u32) -> u32 {
  var x = n; x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u;
  return x;
}
fn h01(n: u32) -> f32 { return f32(hashu(n)) * (1.0 / 4294967295.0); }

@vertex fn vs(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> SOut {
  var o: SOut;
  let pid = drawList[ii];
  let p = particles[pid];
  if (p.kind != 2u) { o.pos = vec4f(2.0, 2.0, 2.0, 1.0); return o; }
  var corners = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
  let c = corners[vi];
  let lifeF = clamp(p.life / max(p.maxLife, 1e-3), 0.0, 1.0);
  let age = 1.0 - lifeF;
  // a raft spreads out as it ages
  let r = p.size * (1.0 + FS.growth * age);
  // lie in the tangent plane of the simulated surface, lifted slightly so the opaque depth test
  // never clips foam on very thin films or freshly stranded on the beach
  let uv = (p.pos.xz - FS.origin) / FS.size;
  let g = textureSampleLevel(shadeTex, linSamp, uv, 0.0).xy;
  let tx = normalize(vec3f(1.0, g.x, 0.0));
  let tz = normalize(vec3f(0.0, g.y, 1.0));
  let n = normalize(cross(tz, tx));
  let wp = p.pos + n * FS.lift + (tx * c.x + tz * c.y) * r;
  o.pos = V.viewProj * vec4f(wp, 1.0);
  o.local = c;
  // the raft's own patch of structure: a random place in the pattern, rotated by a random angle
  // minus the rotation it has picked up from the flow's vorticity (p.vol, see SIMULATE)
  let ang = h01(pid * 3u + 1u) * 6.2831853 - p.vol;
  let cs = cos(ang); let sn = sin(ang);
  let d = c * r;
  o.puv = vec2f(cs * d.x - sn * d.y, sn * d.x + cs * d.y) / FS.tile + vec2f(h01(pid * 7u + 3u), h01(pid * 11u + 5u)) * 17.0;
  o.dist = distance(wp, V.camPos);
  // foam thins as its bubbles burst (faster than the raft's life runs out); fade in over the
  // first fraction of a second. vel.y = thickness at birth (whitewater 1, droplet foam less).
  let birth = clamp(p.vel.y, 0.0, 1.0);
  let ageS = age * p.maxLife;
  let amount = FS.amount * birth * pow(lifeF, FS.thinning) * smoothstep(0.0, FS.fadeIn, ageS);
  // actively breaking whitewater for the first fraction of a second
  let fresh = (1.0 - smoothstep(0.0, FS.freshSeconds, ageS)) * birth;
  o.prm = vec4f(amount, fresh, clamp(p.mud, 0.0, 1.0), 0.0);
  return o;
}

const FOAM_W: f32 = 16.0;
@fragment fn fs(in: SOut) -> SplatOut {
  let pat = textureSample(patternTex, patternSamp, in.puv);   // uniform control flow (mip selection)
  let d2 = dot(in.local, in.local);
  let k = max(1.0 - d2, 0.0);
  // smooth kernel x foam amount, thickness varying in clumps inside the raft (mean 1)
  let w = k * k * k * in.prm.x * (0.55 + 0.9 * pat.a) * FOAM_W;
  if (w < 0.02) { discard; }
  var o: SplatOut;
  o.a = vec4f(w, w * w, w * (pat.r - 0.5), w * (pat.g - 0.5));
  o.b = vec4f(w * in.prm.y, w * in.prm.z, w * in.dist * (1.0 / 64.0), w * (pat.b - 0.5));
  return o;
}
`;

export const FOAM_COMPOSITE = /* wgsl */ `
${VIEW_STRUCT}
struct FoamComp {
  origin: vec2f, size: vec2f,
  density: f32, edge: f32, albedoThin: f32, albedoThick: f32,
  mudFoam: vec3f, bump: f32,
  lace: f32, ambient: f32, unresolved: f32, opacity: f32,
};
@group(0) @binding(0) var<uniform> V: View;
@group(0) @binding(1) var<uniform> FC: FoamComp;
@group(0) @binding(2) var foamA: texture_2d<f32>;
@group(0) @binding(3) var foamB: texture_2d<f32>;
@group(0) @binding(4) var shadeTex: texture_2d<f32>;
@group(0) @binding(5) var linSamp: sampler;
@group(0) @binding(6) var shadowMap: texture_depth_2d;
@group(0) @binding(7) var shadowSamp: sampler_comparison;
${SKY_FN}
${SHADOW_FN}

@vertex fn vs(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4f {
  var p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
  return vec4f(p[vi], 0.0, 1.0);
}

@fragment fn fs(@builtin(position) fc: vec4f) -> @location(0) vec4f {
  let ip = vec2i(fc.xy);
  let a = textureLoad(foamA, ip, 0);
  let b = textureLoad(foamB, ip, 0);
  let Wt = a.x * (1.0 / 16.0);                 // weights are stored x16 (see FOAM_SPLAT)
  let iw = 1.0 / max(a.x, 1e-4);
  // variance-preserving blend of the overlapping rafts' patterns (Yu et al. 2011, eq. 4)
  let vn = inverseSqrt(max(a.y, 1e-4));
  let Pm = clamp(0.5 + a.z * vn, -0.2, 1.2);   // marbling: ragged patch structure
  let Pl = clamp(0.5 + a.w * vn, -0.2, 1.2);   // bubble-raft lace (low on the cell walls)
  let Pb = clamp(0.5 + b.w * vn, 0.0, 1.0);    // fine bubbles
  let fresh = clamp(b.x * iw, 0.0, 1.0);
  let mud = clamp(b.y * iw, 0.0, 1.0);
  let dist = select(b.z * iw * 64.0, 1.0, a.x < 1e-4);
  // the foam's position on this pixel's view ray
  let ndc = vec2f(fc.x * V.invScreen.x * 2.0 - 1.0, 1.0 - fc.y * V.invScreen.y * 2.0);
  let hp = V.invViewProj * vec4f(ndc, 0.5, 1.0);
  let dir = normalize(hp.xyz / hp.w - V.camPos);
  let wp = V.camPos + dir * dist;

  // ---- coverage = P(pattern < density)
  let D = 1.0 - exp(-Wt * FC.density);
  // Level of detail: once a pixel spans more than a few centimetres, the fine lace and bubbles
  // are unresolved, but the patch-scale marbling (0.3–1 m) still is – so the threshold hands over
  // to the marbling (equalised like the lace: same mean coverage) instead of blurring into a veil.
  // Much further away the marbling is replaced by its mean and the threshold widened by its
  // variance (sigma 0.29), i.e. the expected coverage: no shimmer, energy preserved.
  let pixM = dist * 2.0 * V.invScreen.y / max(V.viewProj[1][1], 1e-3) / max(abs(dir.y), 0.25);
  let uf = smoothstep(FC.unresolved * 0.3, FC.unresolved, pixM);            // lace, bubbles
  let uc = smoothstep(FC.unresolved * 3.0, FC.unresolved * 10.0, pixM);     // marbling
  let PmF = mix(Pm, 0.5, uc);
  // fresh, churning whitewater: a solid core with a frothy, ragged rim
  let Pa = mix(Pm * 0.7 + Pb * 0.3, PmF, uf);
  // residual foam: as it thins, the raft opens up into lace along its cell walls, and the last
  // of the lace breaks up into fragments and specks (the bubble term) rather than thin outlines
  let Pr = mix(Pl * 0.7 + Pm * 0.18 + Pb * 0.12, PmF, uf);
  let thick = clamp(D * mix(0.7, 1.0, fresh), 0.0, 1.0);
  let soft = 1.0 + 1.5 * (1.0 - thick);         // thin foam fades out, thick foam has a crisp rim
  let eA = FC.edge + 0.6 * fwidth(Pa) + 0.5 * uc;
  let eR = (FC.edge + 0.6 * fwidth(Pr)) * soft + 0.5 * uc;
  let covA = smoothstep(-eA, eA, D * 1.2 - Pa);
  let covR = smoothstep(-eR, eR, D * FC.lace - Pr);
  let cov = mix(covR, covA, fresh);

  // ---- relief (metres): ~2 cm bubble texture on ~8 cm lumps of foam that follow the marbling,
  // as a bump from screen-space derivatives of the reconstructed position
  let PmC = clamp(PmF, 0.0, 1.0);
  let hgt = cov * (0.02 * (0.5 + 0.5 * Pb) + 0.08 * PmC) * (0.4 + 0.6 * thick);
  let dpx = dpdx(wp); let dpy = dpdy(wp);
  let dhx = dpdx(hgt); let dhy = dpdy(hgt);
  if (a.x < 1e-3 || cov < 0.004) { discard; }

  let uvS = (wp.xz - FC.origin) / FC.size;
  let g = textureSampleLevel(shadeTex, linSamp, uvS, 0.0).xy;
  let N0 = normalize(vec3f(-g.x, 1.0, -g.y));
  let r1 = cross(dpy, N0); let r2 = cross(N0, dpx);
  let det = dot(dpx, r1);
  var sg = vec3f(0.0);
  if (abs(det) > 1e-12) { sg = (dhx * r1 + dhy * r2) / det; }
  sg = sg * FC.bump;
  sg = sg / max(1.0, length(sg) / 1.2);
  let Nf = normalize(N0 - sg);

  // ---- shading: foam is a dense diffuse scatterer (reflectance ~0.5 thin .. ~0.85 thick fresh);
  // thin foam is translucent and takes on the water's colour
  let L = V.sunDir;
  let sh = shadowAt(wp, N0);
  let wrap = max((dot(Nf, L) + 0.3) / 1.3, 0.0);
  let skyI = skyColor(normalize(vec3f(Nf.x * 0.5, 1.0, Nf.z * 0.5)), false);
  let micro = mix(0.74, 1.0, Pb);                    // crevices between bubbles are darker
  // thicker and thinner zones inside a patch (marbling): dense foam is not one flat white
  let albedo = mix(FC.albedoThin, FC.albedoThick, thick) * micro * mix(0.84, 1.05, PmC);
  let tint = mix(vec3f(0.82, 0.92, 0.94), vec3f(1.0), thick);
  let base = mix(tint, FC.mudFoam, mud);
  var col = albedo * base * (V.sunColor * sh * wrap + skyI * FC.ambient);
  // wet glints on fresh foam
  let H = normalize(L - dir);
  col += V.sunColor * sh * pow(max(dot(Nf, H), 0.0), 160.0) * 0.5 * fresh * Pb * Pb;
  col = applyFog(col, wp);
  let alpha = cov * mix(FC.opacity * 0.6, FC.opacity, thick);
  return vec4f(col * alpha, alpha);
}
`;

// ---------------------------------------------------------------------------------------------
// Snippet for engine ground / object shaders: wetness, mud stains, underwater caustic light.
// Generated with the bind group index and first binding so it can be merged into any shader.
export function groundWGSL(group = 1, first = 0) {
  return /* wgsl */ `
struct FluidGround { origin: vec2f, size: vec2f, nx: f32, ny: f32, causticsOn: f32, epsRender: f32, };
@group(${group}) @binding(${first}) var<uniform> FG: FluidGround;
@group(${group}) @binding(${first + 1}) var fgSurf: texture_2d<f32>;
@group(${group}) @binding(${first + 2}) var fgCaustics: texture_2d<f32>;
@group(${group}) @binding(${first + 3}) var fgSamp: sampler;
struct FluidGroundOut { light: vec3f, wet: f32, depth: f32, mud: f32, surface: f32, inside: f32, };
fn fgLoad(p: vec2i) -> vec4f { return textureLoad(fgSurf, clamp(p, vec2i(0), vec2i(i32(FG.nx) - 1, i32(FG.ny) - 1)), 0); }
// light: multiplier for direct sunlight (caustics + absorption under water, 1 when dry)
fn fluidGround(wp: vec3f) -> FluidGroundOut {
  var o: FluidGroundOut;
  o.light = vec3f(1.0); o.wet = 0.0; o.depth = 0.0; o.mud = 0.0; o.surface = -1e9; o.inside = 0.0;
  let uv = (wp.xz - FG.origin) / FG.size;
  if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) { return o; }
  o.inside = 1.0;
  let f = uv * vec2f(FG.nx, FG.ny) - 0.5;
  let fl = floor(f); let t = f - fl; let i = vec2i(fl);
  let s = mix(mix(fgLoad(i), fgLoad(i + vec2i(1, 0)), t.x), mix(fgLoad(i + vec2i(0, 1)), fgLoad(i + vec2i(1, 1)), t.x), t.y);
  o.surface = s.x; o.depth = s.y; o.mud = s.z; o.wet = s.w;
  let under = smoothstep(FG.epsRender, 0.03, s.y) * select(0.0, 1.0, wp.y < s.x + 0.02);
  if (FG.causticsOn > 0.5 && under > 0.0) {
    let c = textureSampleLevel(fgCaustics, fgSamp, uv, 0.0).rgb;
    o.light = mix(vec3f(1.0), c, under);
  }
  return o;
}
`;
}

/** WGSL for the road studio: sky gradient pass + surface pass. */

export const SKY_WGSL = /* wgsl */ `
struct Frame {
  viewProj : mat4x4<f32>,
  cameraPos : vec4<f32>,
  sunDir : vec4<f32>,
  skyTop : vec4<f32>,
  skyBottom : vec4<f32>,
  ground : vec4<f32>,
  params : vec4<f32>,
  fog : vec4<f32>,
};
@group(0) @binding(0) var<uniform> frame : Frame;

struct VSOut {
  @builtin(position) pos : vec4<f32>,
  @location(0) ndc : vec2<f32>,
};

@vertex
fn vs(@builtin(vertex_index) vi : u32) -> VSOut {
  var p = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
  var out : VSOut;
  out.pos = vec4<f32>(p[vi], 1.0, 1.0);
  out.ndc = p[vi];
  return out;
}

@fragment
fn fs(in : VSOut) -> @location(0) vec4<f32> {
  let horizonY = frame.params.x;
  let t = clamp((in.ndc.y - horizonY) * 1.35 + 0.42, -0.2, 1.4);
  let up = smoothstep(-0.05, 1.0, t);
  var col = mix(frame.skyBottom.rgb, frame.skyTop.rgb, up);
  // sun glow
  let sun = max(dot(normalize(vec3<f32>(in.ndc.x, t, 1.0)), normalize(frame.sunDir.xyz)), 0.0);
  col = col + frame.sunDir.w * 0.22 * pow(sun, 24.0) * vec3<f32>(1.0, 0.94, 0.82);
  col = mix(col, frame.fog.rgb, frame.fog.w * 0.5);
  return vec4<f32>(col, 1.0);
}
`;

export const SURFACE_WGSL = /* wgsl */ `
struct Frame {
  viewProj : mat4x4<f32>,
  cameraPos : vec4<f32>,
  sunDir : vec4<f32>,
  skyTop : vec4<f32>,
  skyBottom : vec4<f32>,
  ground : vec4<f32>,
  params : vec4<f32>,
  fog : vec4<f32>,
};
@group(0) @binding(0) var<uniform> frame : Frame;

struct Mat {
  albedo : vec4<f32>,
  params : vec4<f32>,
};
@group(0) @binding(1) var<uniform> mats : array<Mat, 32>;

struct VSIn {
  @location(0) pos : vec3<f32>,
  @location(1) nrm : vec3<f32>,
  @location(2) uv : vec2<f32>,
  @location(3) uv2 : vec2<f32>,
  @location(4) mat : u32,
  @location(5) tint : vec4<f32>,
};

struct VSOut {
  @builtin(position) pos : vec4<f32>,
  @location(0) nrm : vec3<f32>,
  @location(1) uv : vec2<f32>,
  @location(2) uv2 : vec2<f32>,
  @location(3) world : vec3<f32>,
  @location(4) @interpolate(flat) mat : u32,
  @location(5) tint : vec4<f32>,
};

@vertex
fn vs(in : VSIn) -> VSOut {
  var out : VSOut;
  out.pos = frame.viewProj * vec4<f32>(in.pos, 1.0);
  out.nrm = in.nrm;
  out.uv = in.uv;
  out.uv2 = in.uv2;
  out.world = in.pos;
  out.mat = in.mat;
  out.tint = in.tint;
  return out;
}

fn hash2(p : vec2<f32>) -> f32 {
  var q = fract(p * vec2<f32>(127.1, 311.7));
  q = q + vec2<f32>(dot(q, q + 19.19));
  return fract(q.x * q.y);
}

fn valueNoise(p : vec2<f32>) -> f32 {
  let i = floor(p);
  let f = fract(p);
  let u = f * f * (3.0 - 2.0 * f);
  let a = hash2(i);
  let b = hash2(i + vec2<f32>(1.0, 0.0));
  let c = hash2(i + vec2<f32>(0.0, 1.0));
  let d = hash2(i + vec2<f32>(1.0, 1.0));
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

fn fbm(p : vec2<f32>) -> f32 {
  var v = 0.0;
  var amp = 0.5;
  var q = p;
  for (var i = 0; i < 4; i = i + 1) {
    v = v + amp * valueNoise(q);
    q = q * 2.03;
    amp = amp * 0.5;
  }
  return v;
}

@fragment
fn fs(in : VSOut) -> @location(0) vec4<f32> {
  let m = mats[in.mat];
  var albedo = m.albedo.rgb * in.tint.rgb;
  let rough = m.params.x;
  let style = m.params.y;

  // metre-space surface detail: aggregate, jointing, wear
  let uv = in.uv;
  var grain = 0.0;
  if (style < 0.5) {
    // asphalt: coarse aggregate + wheel-track wear
    grain = fbm(uv * 3.4) * 0.16 + fbm(uv * 24.0) * 0.09;
    let wear = valueNoise(uv * 0.22) * 0.07;
    albedo = albedo * (0.94 + grain) + vec3<f32>(wear * 0.5);
  } else if (style < 1.5) {
    // concrete / slabs: panel joints on a 1.8 m grid
    let g = abs(fract(uv / 1.8) - 0.5);
    let joint = smoothstep(0.46, 0.5, max(g.x, g.y));
    grain = fbm(uv * 9.0) * 0.07;
    albedo = albedo * (0.97 + grain) * (1.0 - joint * 0.28);
  } else if (style < 2.5) {
    // block paving: staggered bonds
    let cell = vec2<f32>(0.22, 0.11);
    var p = uv / cell;
    let row = floor(p.y);
    p.x = p.x + select(0.0, 0.5, fract(row * 0.5) > 0.25);
    let f = fract(p);
    let mortar = smoothstep(0.40, 0.5, max(abs(f.x - 0.5), abs(f.y - 0.5)));
    grain = fbm(uv * 18.0) * 0.10;
    albedo = albedo * (0.96 + grain) * (1.0 - mortar * 0.35);
  } else if (style < 3.5) {
    // tactile / kerb: fine cast texture
    grain = fbm(uv * 40.0) * 0.05;
    albedo = albedo * (0.99 + grain);
  } else if (style < 4.5) {
    // grass / soil / gravel: clumps
    grain = fbm(uv * 6.0) * 0.35 + fbm(uv * 30.0) * 0.16;
    let clump = valueNoise(uv * 0.6);
    albedo = albedo * (0.78 + grain) + albedo * clump * 0.25;
  } else {
    // paint / steel: crisp
    grain = fbm(uv * 12.0) * 0.03;
    albedo = albedo * (0.99 + grain);
  }
  albedo = clamp(albedo, vec3<f32>(0.0), vec3<f32>(1.0));

  let n = normalize(in.nrm);
  let sun = normalize(frame.sunDir.xyz);
  let lambert = max(dot(n, sun), 0.0);
  // soft terminator so the low sun does not clip to black on flat asphalt
  let soft = pow(clamp(dot(n, sun) * 0.5 + 0.5, 0.0, 1.0), 1.6);
  let hemi = 0.5 + 0.5 * n.y;
  let skyAmb = frame.skyTop.rgb * mix(0.55, 1.0, hemi);
  let ambient = mix(frame.ground.rgb, skyAmb, hemi) * 0.22;

  // specular: wet-ish highlight on smooth materials
  let view = normalize(frame.cameraPos.xyz - in.world);
  let halfV = normalize(sun + view);
  let spec = pow(max(dot(n, halfV), 0.0), mix(8.0, 90.0, 1.0 - rough)) * (1.0 - rough) * 0.22;

  let sunLight = frame.sunDir.w * mix(lambert, soft, 0.45) * (1.0 - rough * 0.5);
  var col = albedo * (ambient + vec3<f32>(sunLight)) + vec3<f32>(spec) * frame.sunDir.w;

  // aerial perspective: only bites at long range
  let dist = length(frame.cameraPos.xyz - in.world);
  let fogF = 1.0 - exp(-dist * frame.fog.w);
  col = mix(col, frame.fog.rgb, clamp(fogF, 0.0, 0.9));

  // ACES-ish filmic curve + sRGB transfer
  col = col * 1.15;
  col = (col * (2.51 * col + vec3<f32>(0.03))) / (col * (2.43 * col + vec3<f32>(0.59)) + vec3<f32>(0.14));
  col = pow(clamp(col, vec3<f32>(0.0), vec3<f32>(1.0)), vec3<f32>(0.4545));
  return vec4<f32>(col, 1.0);
}
`;

export const WIRE_WGSL = /* wgsl */ `
struct Frame {
  viewProj : mat4x4<f32>,
  cameraPos : vec4<f32>,
  sunDir : vec4<f32>,
  skyTop : vec4<f32>,
  skyBottom : vec4<f32>,
  ground : vec4<f32>,
  params : vec4<f32>,
  fog : vec4<f32>,
};
@group(0) @binding(0) var<uniform> frame : Frame;

@vertex
fn vs(@location(0) pos : vec3<f32>) -> @builtin(position) vec4<f32> {
  return frame.viewProj * vec4<f32>(pos, 1.0);
}

@fragment
fn fs() -> @location(0) vec4<f32> {
  return vec4<f32>(0.06, 0.62, 0.72, 1.0);
}
`;

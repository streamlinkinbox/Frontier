//@@FRAME@@
// Terrain: height/slope-blended procedural albedo, sun lighting, simulated
// wetness darkening, and underwater CAUSTICS from the fluid solver.
// Outputs linear view depth to location 1 (the water shader derives the
// true water path length from it).

struct TerrainParams {
  colSand : vec3<f32>,  sandH : f32,
  colGrass : vec3<f32>, grassH : f32,
  colRock : vec3<f32>,  rockSlope : f32,
  colWet : vec3<f32>,   causticStrength : f32,
  simCenter : vec2<f32>,
  simWorldSize : f32,
  waterLevel : f32,
};
@group(1) @binding(0) var<uniform> T : TerrainParams;
@group(1) @binding(1) var fieldTex : texture_2d<f32>;
@group(1) @binding(2) var foamTex : texture_2d<f32>;
@group(1) @binding(3) var causticsTex : texture_2d<f32>;
@group(1) @binding(4) var samp : sampler;

struct VSOut {
  @builtin(position) clipPos : vec4<f32>,
  @location(0) worldPos : vec3<f32>,
  @location(1) normal : vec3<f32>,
  @location(2) viewZ : f32,
};

@vertex
fn vsMain(@location(0) pos : vec3<f32>, @location(1) norm : vec3<f32>) -> VSOut {
  var out : VSOut;
  out.worldPos = pos;
  out.normal = norm;
  out.viewZ = -(F.view * vec4<f32>(pos, 1.0)).z;
  out.clipPos = F.viewProj * vec4<f32>(pos, 1.0);
  return out;
}

struct FSOut {
  @location(0) color : vec4<f32>,
  @location(1) linDepth : vec4<f32>,
};

@fragment
fn fsMain(in : VSOut) -> FSOut {
  if (F.clipEnabled > 0.5 && in.worldPos.y < F.clipLevel) { discard; }
  let N = normalize(in.normal);
  let simUV = (in.worldPos.xz - T.simCenter) / T.simWorldSize + 0.5;
  let inSim = all(simUV >= vec2<f32>(0.0)) && all(simUV <= vec2<f32>(1.0));

  var waterDepth = 0.0;
  var wet = 0.0;
  var caustic = 0.0;
  if (inSim) {
    waterDepth = textureSampleLevel(fieldTex, samp, simUV, 0.0).x;
    wet = textureSampleLevel(foamTex, samp, simUV, 0.0).y;
    caustic = textureSampleLevel(causticsTex, samp, simUV, 0.0).x;
  }
  let underwater = waterDepth > 0.008;

  // ---- albedo by height & slope with noise breakup
  let hgt = in.worldPos.y - T.waterLevel;
  let slope = 1.0 - N.y;
  let nBig = vnoise(in.worldPos.xz * 0.08);
  let nSmall = vnoise(in.worldPos.xz * 1.4);
  var albedo = mix(T.colSand, T.colGrass, smoothstep(T.sandH, T.grassH, hgt + (nBig - 0.5) * 1.4));
  albedo = mix(albedo, T.colRock, smoothstep(T.rockSlope, T.rockSlope + 0.25, slope + (nSmall - 0.5) * 0.12));
  albedo *= 0.82 + 0.36 * nSmall;
  // shore blend under water (sandy bed reads through refraction)
  if (underwater) { albedo = mix(albedo, T.colSand * (0.85 + 0.3 * nSmall), 0.55); }
  // simulated wetness: darker, richer soil that dries slowly
  albedo = mix(albedo, T.colWet, wet * 0.55);
  albedo *= (1.0 - wet * 0.25);

  // ---- lighting
  let ndl = max(dot(N, F.sunDir), 0.0);
  var sunAmt = ndl;
  if (underwater) {
    // caustics modulate the sunlight that made it through the waves
    sunAmt *= clamp(0.45 + caustic * T.causticStrength * 1.7, 0.0, 3.2);
    wet = 1.0;
  }
  let hemi = mix(vec3<f32>(0.22, 0.19, 0.15), vec3<f32>(0.45, 0.58, 0.75), N.y * 0.5 + 0.5);
  var col = albedo * (hemi * 0.85 + F.sunColor * F.sunI * sunAmt);

  // wet specular sheen
  let V = normalize(F.camPos - in.worldPos);
  let H = normalize(V + F.sunDir);
  let spec = pow(max(dot(N, H), 0.0), 60.0) * wet * 0.35;
  col += F.sunColor * F.sunI * spec;

  col = applyFog(col, in.worldPos);
  var out : FSOut;
  out.color = vec4<f32>(col, 1.0);
  out.linDepth = vec4<f32>(in.viewZ, 0.0, 0.0, 0.0);
  return out;
}

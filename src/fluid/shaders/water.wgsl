//@@FRAME@@
// Frontier Fluid — water surface material.
//
// The mesh is displaced in the vertex shader by the *simulated* heightfield:
// every wave you see — wakes, bow waves, river standing waves, pond ripples,
// ocean swell shoaling on the beach — is real geometry from the solver.
// Shading is physical-ish: Beer-Lambert absorption over the true
// view-ray water path (from the scene depth buffer), Fresnel, planar
// reflections, sun glints, through-wave scattering, and foam driven by the
// simulated foam-density field. An optional flow-advected micro-normal layer
// adds sub-grid sparkle (normals only, toggleable) — it never moves water.

struct WaterParams {
  reflVP : mat4x4<f32>,
  absorption : vec3<f32>,
  roughness : f32,
  scatterColor : vec3<f32>,
  ior : f32,
  deepColor : vec3<f32>,
  detailAmt : f32,
  foamColor : vec3<f32>,
  useReflection : f32,
  simCenter : vec2<f32>,
  simWorldSize : f32,
  simDx : f32,
  simRes : f32,
  pad0 : f32,
  pad1 : vec2<f32>,
};

@group(1) @binding(0) var<uniform> W : WaterParams;
@group(1) @binding(1) var fieldTex : texture_2d<f32>;
@group(1) @binding(2) var bedTex : texture_2d<f32>;
@group(1) @binding(3) var foamTex : texture_2d<f32>;
@group(1) @binding(4) var refractTex : texture_2d<f32>;
@group(1) @binding(5) var linDepthTex : texture_2d<f32>;
@group(1) @binding(6) var reflectionTex : texture_2d<f32>;
@group(1) @binding(7) var samp : sampler;

struct VSOut {
  @builtin(position) clipPos : vec4<f32>,
  @location(0) worldPos : vec3<f32>,
  @location(1) simUV : vec2<f32>,
  @location(2) viewZ : f32,
  @location(3) depth : f32,
  @location(4) surfVel : vec2<f32>,
};

@vertex
fn vsMain(@location(0) uvIn : vec2<f32>) -> VSOut {
  var out : VSOut;
  let uv = uvIn;
  let f = textureSampleLevel(fieldTex, samp, uv, 0.0);
  let bed = textureSampleLevel(bedTex, samp, uv, 0.0).x;
  let wxz = W.simCenter + (uv - 0.5) * W.simWorldSize;
  var y = bed + f.x;
  if (f.x < 0.0025) { y = bed - 0.045; } // tuck dry cells under the bed
  let wp = vec3<f32>(wxz.x, y, wxz.y);
  out.worldPos = wp;
  out.simUV = uv;
  out.depth = f.x;
  out.surfVel = f.yz;
  out.viewZ = -(F.view * vec4<f32>(wp, 1.0)).z;
  out.clipPos = F.viewProj * vec4<f32>(wp, 1.0);
  return out;
}

fn etaAt(uv : vec2<f32>) -> f32 {
  return textureSampleLevel(bedTex, samp, uv, 0.0).x + textureSampleLevel(fieldTex, samp, uv, 0.0).x;
}

fn noiseGrad(p : vec2<f32>) -> vec2<f32> {
  let e = 0.35;
  let c = vnoise(p);
  return vec2<f32>(vnoise(p + vec2<f32>(e, 0.0)) - c, vnoise(p + vec2<f32>(0.0, e)) - c) / e;
}

@fragment
fn fsMain(in : VSOut, @builtin(position) fragPos : vec4<f32>) -> @location(0) vec4<f32> {
  if (in.depth < 0.0012) { discard; }
  let texel = 1.0 / W.simRes;

  // --- geometric normal from the simulated surface
  let gx = (etaAt(in.simUV + vec2<f32>(texel, 0.0)) - etaAt(in.simUV - vec2<f32>(texel, 0.0))) / (2.0 * W.simDx);
  let gz = (etaAt(in.simUV + vec2<f32>(0.0, texel)) - etaAt(in.simUV - vec2<f32>(0.0, texel))) / (2.0 * W.simDx);
  var N = normalize(vec3<f32>(-gx, 1.0, -gz));
  let steep = length(vec2<f32>(gx, gz));

  // --- optional micro-detail normals (sub-grid sparkle, flow-advected)
  if (W.detailAmt > 0.001) {
    let flowP = in.worldPos.xz - in.surfVel * F.time * 0.35;
    let g1 = noiseGrad(flowP * 1.35 + F.time * 0.22);
    let g2 = noiseGrad(flowP * 3.9 - F.time * 0.31);
    let agitation = 0.35 + clamp(length(in.surfVel) * 0.5 + steep * 0.8, 0.0, 1.2);
    let dg = (g1 * 0.65 + g2 * 0.35) * W.detailAmt * agitation * 0.16;
    N = normalize(N + vec3<f32>(-dg.x, 0.0, -dg.y));
  }

  let V = normalize(F.camPos - in.worldPos);       // toward camera
  let Vw = -V;                                       // view direction
  let ndv = clamp(dot(N, V), 0.0, 1.0);
  let f0 = pow((W.ior - 1.0) / (W.ior + 1.0), 2.0);
  let fresnel = f0 + (1.0 - f0) * pow(1.0 - ndv, 5.0);

  // --- refraction + true water path length from scene depth
  let screenUV = fragPos.xy / F.screenSize;
  let refrStrength = 0.045 * clamp(1.5 - ndv, 0.4, 1.5);
  var rUV = clamp(screenUV + N.xz * refrStrength, vec2<f32>(0.001), vec2<f32>(0.999));
  var sceneZ = textureSampleLevel(linDepthTex, samp, rUV, 0.0).x;
  if (sceneZ < in.viewZ) { // distortion pulled a foreground pixel: fall back
    rUV = screenUV;
    sceneZ = textureSampleLevel(linDepthTex, samp, rUV, 0.0).x;
  }
  let path = clamp(max(sceneZ - in.viewZ, 0.0), 0.0, 60.0);
  let sceneCol = textureSampleLevel(refractTex, samp, rUV, 0.0).rgb;

  let transmit = exp(-W.absorption * path);
  let shallowF = exp(-path * 0.30);
  let bodyCol = mix(W.deepColor, W.scatterColor, shallowF);
  var col = sceneCol * transmit + bodyCol * (1.0 - (transmit.r + transmit.g + transmit.b) / 3.0);

  // --- reflection (planar render target, or sky gradient fallback)
  var reflCol : vec3<f32>;
  let R = reflect(Vw, N);
  if (W.useReflection > 0.5) {
    let rc = W.reflVP * vec4<f32>(in.worldPos, 1.0);
    var used = false;
    reflCol = skyGradient(normalize(vec3<f32>(R.x, abs(R.y), R.z)));
    if (rc.w > 0.001) {
      var puv = rc.xy / rc.w * vec2<f32>(0.5, -0.5) + 0.5;
      puv += N.xz * 0.05;
      if (all(puv > vec2<f32>(0.002)) && all(puv < vec2<f32>(0.998))) {
        reflCol = textureSampleLevel(reflectionTex, samp, puv, 0.0).rgb;
        used = true;
      }
    }
    _ = used;
  } else {
    reflCol = skyGradient(normalize(vec3<f32>(R.x, abs(R.y), R.z)));
  }
  reflCol *= (1.0 - W.roughness * 0.55);
  col = mix(col, reflCol, clamp(fresnel * 1.6, 0.0, 1.0));

  // --- sun glint (Blinn-Phong shaped by roughness)
  let H = normalize(V + F.sunDir);
  let specPow = mix(950.0, 36.0, clamp(W.roughness, 0.0, 1.0));
  let spec = pow(max(dot(N, H), 0.0), specPow) * (0.25 + fresnel * 2.2);
  col += F.sunColor * F.sunI * spec;

  // --- through-wave scattering (light glowing through crests toward camera)
  let through = pow(clamp(dot(Vw, normalize(vec3<f32>(-F.sunDir.x, -0.22, -F.sunDir.z))), 0.0, 1.0), 3.0);
  let crest = clamp(steep * 1.4 + exp(-path * 0.8) * 0.35, 0.0, 1.0);
  col += W.scatterColor * F.sunColor * F.sunI * through * crest * 0.55;

  // --- foam from the simulated foam-density field
  let foamD = textureSampleLevel(foamTex, samp, in.simUV, 0.0).x;
  if (foamD > 0.01) {
    let p1 = vnoise(in.worldPos.xz * 2.7 - in.surfVel * F.time * 0.22);
    let p2 = vnoise(in.worldPos.xz * 8.5 + in.surfVel * F.time * 0.13);
    let pattern = p1 * 0.62 + p2 * 0.38;
    let foamMask = smoothstep(0.42, 0.95, foamD * (0.45 + 0.85 * pattern) * 1.25);
    let foamLight = 0.72 + 0.28 * pattern;
    let foamCol = W.foamColor * foamLight * (0.55 + 0.45 * F.sunI);
    col = mix(col, foamCol, foamMask * 0.94);
  }

  col = applyFog(col, in.worldPos);
  let edgeAlpha = smoothstep(0.0, 0.055, path + in.depth * 0.4);
  let alpha = clamp(max(edgeAlpha, fresnel * 1.4), 0.0, 1.0);
  return vec4<f32>(col, alpha);
}

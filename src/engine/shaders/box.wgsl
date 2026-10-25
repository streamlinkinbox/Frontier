//@@FRAME@@
// Generic solid box/props material: flat-shaded colour, optional plank
// grooves (crates), wet sheen below the waterline. Used for floating crates,
// the boat, and every part of the demo vehicle.

struct ObjParams {
  model : mat4x4<f32>,
  color : vec3<f32>,
  planks : f32,      // 1 = wood plank grooves
  color2 : vec3<f32>,
  roughness : f32,
};
@group(1) @binding(0) var<uniform> O : ObjParams;

struct VSOut {
  @builtin(position) clipPos : vec4<f32>,
  @location(0) worldPos : vec3<f32>,
  @location(1) normal : vec3<f32>,
  @location(2) viewZ : f32,
  @location(3) localPos : vec3<f32>,
};

@vertex
fn vsMain(@location(0) pos : vec3<f32>, @location(1) norm : vec3<f32>) -> VSOut {
  var out : VSOut;
  let wp = (O.model * vec4<f32>(pos, 1.0)).xyz;
  // normal matrix = rotation part (models here are rigid + uniform-ish scale)
  out.normal = normalize((O.model * vec4<f32>(norm, 0.0)).xyz);
  out.worldPos = wp;
  out.localPos = pos;
  out.viewZ = -(F.view * vec4<f32>(wp, 1.0)).z;
  out.clipPos = F.viewProj * vec4<f32>(wp, 1.0);
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
  var albedo = O.color;

  if (O.planks > 0.5) {
    // plank grooves + edge battens from local position
    let lp = in.localPos;
    let grooveCoord = select(lp.y, lp.x, abs(N.y) > 0.5);
    let g = fract(grooveCoord * 3.0 + 0.5);
    let groove = smoothstep(0.0, 0.06, g) * (1.0 - smoothstep(0.08, 0.14, g));
    albedo *= (1.0 - groove * 0.45);
    let edge = max(max(abs(lp.x), abs(lp.y)), abs(lp.z));
    albedo = mix(albedo, O.color2, smoothstep(0.86, 0.98, edge) * 0.8);
    albedo *= 0.85 + 0.3 * vnoise(lp.xy * 6.0 + lp.zx * 4.0);
  } else {
    albedo *= 0.92 + 0.16 * vnoise(in.localPos.xz * 5.0 + in.localPos.yy * 3.0);
  }

  let ndl = max(dot(N, F.sunDir), 0.0);
  let hemi = mix(vec3<f32>(0.22, 0.19, 0.15), vec3<f32>(0.45, 0.58, 0.75), N.y * 0.5 + 0.5);
  var col = albedo * (hemi * 0.9 + F.sunColor * F.sunI * ndl);

  // specular (vehicle paint glints; stronger when wet below waterline)
  let V = normalize(F.camPos - in.worldPos);
  let H = normalize(V + F.sunDir);
  let wet = 1.0 - smoothstep(-0.1, 0.25, in.worldPos.y - F.waterLevel);
  let shin = mix(140.0, 18.0, clamp(O.roughness, 0.0, 1.0));
  let spec = pow(max(dot(N, H), 0.0), shin) * (0.18 + wet * 0.6);
  col += F.sunColor * F.sunI * spec;
  col *= (1.0 - wet * 0.18);

  col = applyFog(col, in.worldPos);
  var out : FSOut;
  out.color = vec4<f32>(col, 1.0);
  out.linDepth = vec4<f32>(in.viewZ, 0.0, 0.0, 0.0);
  return out;
}

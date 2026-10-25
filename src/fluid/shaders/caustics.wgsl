//@@COMMON@@
// Frontier Fluid — CAUSTICS computed from the simulated surface.
//
// For every surface texel we refract the sun direction through the actual
// simulated surface normal (Snell, using the material IOR), march the
// refracted ray to the bed, and measure how much the ray bundle is focused
// or spread by comparing the bed footprint of neighbouring rays (the
// Jacobian of the surface->bed mapping). Focused = bright caustic filament,
// spread = dim. Energy is scattered into a fixed-point accumulation buffer
// at the bed texel it actually lands on, then resolved to a texture the
// terrain shader multiplies its sunlight by. Wave the water and the
// caustics wave — because they are the same waves.
//
// Bindings (CAUSTICS layout): 0 P | 1 field | 2 bedFlow | 3 sampler | 4 accum
// Bindings (RESOLVE layout):  0 P | 1 accumRead | 2 causticsOut

@group(0) @binding(0) var<uniform> P : SimParams;
@group(0) @binding(1) var field : texture_2d<f32>;
@group(0) @binding(2) var bedFlow : texture_2d<f32>;
@group(0) @binding(3) var samp : sampler;
@group(0) @binding(4) var<storage, read_write> accum : array<atomic<u32>>;

fn bedAt(xz : vec2<f32>) -> f32 {
  return textureSampleLevel(bedFlow, samp, worldUV(xz, P.center, P.worldSize), 0.0).x;
}

// Surface point + normal at a cell, from the simulated height field.
fn surfacePoint(xy : vec2<i32>) -> vec4<f32> { // xyz = point, w = depth
  let f = textureLoad(field, clampCell(xy, P.res), 0);
  let bed = textureLoad(bedFlow, clampCell(xy, P.res), 0).x;
  let wxz = cellWorld(xy, P.res, P.center, P.worldSize);
  return vec4<f32>(wxz.x, bed + f.x, wxz.y, f.x);
}
fn etaAt(xy : vec2<i32>) -> f32 {
  let p = clampCell(xy, P.res);
  return textureLoad(bedFlow, p, 0).x + textureLoad(field, p, 0).x;
}
fn surfaceNormal(xy : vec2<i32>) -> vec3<f32> {
  let gx = (etaAt(xy + vec2<i32>(1, 0)) - etaAt(xy - vec2<i32>(1, 0))) * 0.5 * P.invDx;
  let gz = (etaAt(xy + vec2<i32>(0, 1)) - etaAt(xy - vec2<i32>(0, 1))) * 0.5 * P.invDx;
  return normalize(vec3<f32>(-gx, 1.0, -gz));
}

// March a refracted ray from surface point p down to the bed.
fn refractToBed(p : vec3<f32>, n : vec3<f32>) -> vec3<f32> {
  let I = -P.sunDir; // incident direction (from sun toward surface)
  let R = refract(I, n, 1.0 / P.ior);
  var dir = R;
  if (dot(R, R) < 0.001 || R.y > -0.02) { dir = vec3<f32>(I.x, -abs(I.y) - 0.05, I.z); }
  dir = normalize(dir);
  // Fixed-point iteration against the bed heightfield (gentle beds: ~exact).
  var t = max(0.0, (p.y - bedAt(p.xz)) / max(0.08, -dir.y));
  for (var i = 0; i < 3; i++) {
    let xz = p.xz + dir.xz * t;
    t = max(0.0, (p.y - bedAt(xz)) / max(0.08, -dir.y));
  }
  return p + dir * t;
}

@compute @workgroup_size(8, 8)
fn causticsMain(@builtin(global_invocation_id) id : vec3<u32>) {
  if (id.x >= P.res.x || id.y >= P.res.y) { return; }
  if (P.sunDir.y < 0.03) { return; } // sun down
  let xy = vec2<i32>(id.xy);

  let sp = surfacePoint(xy);
  let depth = sp.w;
  if (depth < 0.015) { return; }

  let n = surfaceNormal(xy);
  let p = sp.xyz;
  let hitC = refractToBed(p, n);

  // neighbouring rays for the Jacobian (ray-bundle area change)
  let xyR = xy + vec2<i32>(1, 0);
  let xyT = xy + vec2<i32>(0, 1);
  let hitR = refractToBed(surfacePoint(xyR).xyz, surfaceNormal(xyR));
  let hitT = refractToBed(surfacePoint(xyT).xyz, surfaceNormal(xyT));

  let a1 = hitR.xz - hitC.xz;
  let a2 = hitT.xz - hitC.xz;
  let area = abs(a1.x * a2.y - a1.y * a2.x);
  let cellArea = P.dx * P.dx;

  var intensity = cellArea / max(area, cellArea * 0.04);
  // physical fades: Fresnel transmission in, scattering/absorption with depth
  let ndv = clamp(dot(n, P.sunDir), 0.0, 1.0);
  let fresnelIn = 1.0 - (0.02 + 0.98 * pow(1.0 - ndv, 5.0));
  intensity *= fresnelIn;
  intensity *= exp(-depth * 0.22) * smoothstep(0.015, 0.2, depth);
  intensity = min(intensity, 9.0);
  if (intensity <= 0.001) { return; }

  // scatter energy onto the bed texel the ray actually hits
  let huv = worldUV(hitC.xz, P.center, P.worldSize);
  if (any(huv < vec2<f32>(0.0)) || any(huv > vec2<f32>(1.0))) { return; }
  let cell = vec2<u32>(huv * vec2<f32>(P.res));
  let idx = cell.y * P.res.x + cell.x;
  atomicAdd(&accum[idx], u32(intensity * 220.0));
}

// ------------------------------------------------------------- resolve pass
// (separate module bindings are declared in caustics-resolve chunk below via
//  a second shader module built from this same file with a different entry —
//  see FluidSim: the resolve entry uses RESOLVE_LAYOUT with its own module.)

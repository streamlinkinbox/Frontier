import * as THREE from 'three';

// Procedural limestone / sediment shader (3D object-space noise => no UVs, no tiling, no seams).
// Features: multi-scale bump with analytic noise derivatives, sedimentary strata, iron staining,
// calcite flowstone, damp drip streaks (darker + glossier), floor sediment with grit, baked AO.
// Octaves fade out by pixel footprint so the micro-detail never shimmers.

export const NOISE_GLSL = /* glsl */`
float rHash13(vec3 p3){ p3 = fract(p3 * .1031); p3 += dot(p3, p3.zyx + 31.32); return fract((p3.x + p3.y) * p3.z); }
vec4 rNoised(vec3 x){
  vec3 i = floor(x), w = fract(x);
  vec3 u = w*w*w*(w*(w*6.0-15.0)+10.0);
  vec3 du = 30.0*w*w*(w*(w-2.0)+1.0);
  float a = rHash13(i), b = rHash13(i+vec3(1,0,0)), c = rHash13(i+vec3(0,1,0)), d = rHash13(i+vec3(1,1,0));
  float e = rHash13(i+vec3(0,0,1)), f = rHash13(i+vec3(1,0,1)), g = rHash13(i+vec3(0,1,1)), h = rHash13(i+vec3(1,1,1));
  float k0 = a, k1 = b-a, k2 = c-a, k3 = e-a, k4 = a-b-c+d, k5 = a-c-e+g, k6 = a-b-e+f, k7 = -a+b+c-d+e-f-g+h;
  return vec4(2.0*(k0 + k1*u.x + k2*u.y + k3*u.z + k4*u.x*u.y + k5*u.y*u.z + k6*u.z*u.x + k7*u.x*u.y*u.z) - 1.0,
    2.0*du*vec3(k1 + k4*u.y + k6*u.z + k7*u.y*u.z, k2 + k5*u.z + k4*u.x + k7*u.z*u.x, k3 + k6*u.x + k5*u.y + k7*u.x*u.y));
}
// fbm with derivatives; lod = world size of a pixel in noise units
vec4 rFbm(vec3 p, int oct, float lod){
  vec4 s = vec4(0.0); float a = 0.5, f = 1.0;
  for (int i = 0; i < 7; i++){
    if (i >= oct) break;
    float fade = 1.0 - smoothstep(0.25, 0.6, f * lod);
    vec4 n = rNoised(p * f + float(i) * 13.7);
    s += a * fade * vec4(n.x, n.yzw * f);
    a *= 0.5; f *= 2.02;
  }
  return s;
}
`;

export function createRockMaterial({ instanced = false } = {}) {
  const m = new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.85, metalness: 0 });
  m.onBeforeCompile = (shader) => {
    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', `#include <common>
        attribute vec3 aAO;
        varying vec3 vRockP; varying vec3 vRockN; varying vec3 vAO;`)
      .replace('#include <begin_vertex>', `#include <begin_vertex>
        {
          vec4 wp = vec4(transformed, 1.0);
          vec3 wn = normal;
          #ifdef USE_INSTANCING
            wp = instanceMatrix * wp; wn = mat3(instanceMatrix) * wn;
          #endif
          wp = modelMatrix * wp;
          vRockP = wp.xyz; vRockN = normalize(mat3(modelMatrix) * wn);
          ${instanced ? 'vAO = vec3(0.85, 0.5, 0.0);' : 'vAO = aAO;'}
        }`);
    shader.fragmentShader = shader.fragmentShader
      .replace('#include <common>', `#include <common>
        varying vec3 vRockP; varying vec3 vRockN; varying vec3 vAO;
        ${NOISE_GLSL}
        vec3 gRockNormalW; float gRockRough; float gWet;`)
      .replace('#include <color_fragment>', `#include <color_fragment>
        {
          vec3 p = vRockP;
          vec3 N = normalize(vRockN);
          float pix = length(fwidth(p));
          // ---- macro colour zones
          vec4 zone = rFbm(p * 0.035, 3, pix * 0.035);
          vec4 mid = rFbm(p * 0.22 + 3.1, 4, pix * 0.22);
          // strata (sedimentary layering), warped
          float strataCoord = p.y * 0.9 + zone.x * 3.0 + mid.x * 0.6;
          float strata = sin(strataCoord) * 0.5 + 0.5;
          float strataFine = sin(p.y * 6.3 + mid.x * 2.0) * 0.5 + 0.5;
          // ---- bump: large lumps, pitted grain, fine grit
          vec4 b1 = rFbm(p * 0.45, 4, pix * 0.45);
          vec4 b2 = rFbm(p * 2.4 + 7.0, 3, pix * 2.4);
          vec4 b3 = rFbm(p * 11.0 + 1.3, 2, pix * 11.0);
          float floorMask = ${instanced ? 'max(smoothstep(0.55, 0.85, N.y), 0.7)' : 'smoothstep(0.55, 0.85, N.y)'};
          float ceilMask = smoothstep(-0.35, -0.75, N.y);
          vec3 grad = b1.yzw * 0.45 * 0.55 + b2.yzw * 2.4 * 0.12 + b3.yzw * 11.0 * (0.018 + 0.04 * floorMask);
          grad += vec3(0.0, cos(strataCoord) * 0.9 * 0.18, 0.0);
          // ---- moisture: vertical drip streaks on walls and ceilings
          float streak = rNoised(vec3(p.x * 0.55, p.y * 0.035, p.z * 0.55) + zone.x).x;
          float wet = smoothstep(0.25, 0.6, streak + 0.35 * ceilMask - 0.45 * floorMask + zone.x * 0.3);
          wet = max(wet, ceilMask * smoothstep(0.1, 0.5, mid.x + 0.2) * 0.8);
          // calcite flowstone sheets
          float flow = smoothstep(0.35, 0.75, rNoised(vec3(p.x * 0.08, p.y * 0.02, p.z * 0.08) + 9.0).x + (1.0 - floorMask) * 0.15 - floorMask * 0.5);
          // ---- albedo (linear)
          vec3 limeA = vec3(0.17, 0.145, 0.115), limeB = vec3(0.27, 0.235, 0.19), iron = vec3(0.16, 0.085, 0.045), calc = vec3(0.42, 0.38, 0.31);
          vec3 col = mix(limeA, limeB, clamp(0.5 + 0.6 * zone.x + 0.25 * mid.x, 0.0, 1.0));
          col *= mix(0.82, 1.08, strata) * mix(0.93, 1.04, strataFine);
          col = mix(col, iron, smoothstep(0.3, 0.8, rNoised(p * 0.06 + 4.0).x + 0.4 * mid.x) * 0.55);
          col = mix(col, calc, flow * 0.75);
          // floor sediment: dusty earth + scattered grit
          vec3 sed = mix(vec3(0.15, 0.12, 0.09), vec3(0.24, 0.195, 0.15), 0.5 + 0.5 * mid.x);
          float grit = smoothstep(0.55, 0.8, rNoised(p * 9.0).x) * (1.0 - smoothstep(0.1, 0.4, pix * 9.0));
          vec4 clump = rFbm(p * 1.3 + 17.0, 3, pix * 1.3);
          sed *= mix(0.55, 1.15, smoothstep(-0.5, 0.6, clump.x));               // compacted vs loose dust
          sed = mix(sed, vec3(0.07, 0.05, 0.035), smoothstep(0.35, 0.7, rNoised(p * 0.7 + 5.0).x) * 0.55); // organic detritus / guano
          float gritDark = smoothstep(0.6, 0.85, rNoised(p * 7.0 + 2.0).x) * (1.0 - smoothstep(0.1, 0.4, pix * 7.0));
          sed = mix(sed, vec3(0.34, 0.31, 0.27), grit * 0.55);
          sed = mix(sed, vec3(0.05, 0.04, 0.03), gritDark * 0.6);
          grad += clump.yzw * 1.3 * 0.05 * floorMask;
          col = mix(col, sed, floorMask * smoothstep(-0.2, 0.3, mid.x + 0.3));
          // cavity dirt + micro variation
          col *= mix(0.72, 1.0, smoothstep(0.2, 0.8, vAO.y)) * (0.9 + 0.2 * b2.x);
          col *= mix(1.0, 0.5, wet);
          diffuseColor.rgb = col;
          gRockRough = mix(mix(0.88, 0.95, floorMask), 0.22, wet * (1.0 - floorMask * 0.6));
          gRockRough = mix(gRockRough, 0.45, flow * 0.6 * (1.0 - wet));
          gWet = wet;
          float bumpK = mix(1.0, 0.45, flow) * mix(1.0, 0.6, wet);
          vec3 g = grad * bumpK;
          gRockNormalW = normalize(N - (g - N * dot(g, N)));
        }`)
      .replace('#include <roughnessmap_fragment>', `float roughnessFactor = gRockRough;`)
      .replace('#include <normal_fragment_maps>', `normal = normalize((viewMatrix * vec4(gRockNormalW, 0.0)).xyz);`)
      .replace('#include <aomap_fragment>', `
        {
          float ao = vAO.x;
          reflectedLight.indirectDiffuse *= ao;
          reflectedLight.indirectSpecular *= ao * ao;
        }`);
  };
  m.customProgramCacheKey = () => 'rock' + (instanced ? 'I' : '');
  return m;
}

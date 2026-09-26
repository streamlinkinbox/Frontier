// Mine surface material: vertex colour albedo + per-vertex metalness, with
// procedural world-space detail (rock grain, concrete, tread plate) and
// derivative-based bump mapping. No UV dependency -> seamless everywhere.
import * as THREE from 'three';

const NOISE_GLSL = /* glsl */`
  float mhash(vec3 p){ p = fract(p*0.3183099+.1); p*=17.0; return fract(p.x*p.y*p.z*(p.x+p.y+p.z)); }
  float mnoise(vec3 x){
    vec3 i = floor(x); vec3 f = fract(x); f = f*f*(3.0-2.0*f);
    return mix(mix(mix(mhash(i+vec3(0,0,0)),mhash(i+vec3(1,0,0)),f.x),
                   mix(mhash(i+vec3(0,1,0)),mhash(i+vec3(1,1,0)),f.x),f.y),
               mix(mix(mhash(i+vec3(0,0,1)),mhash(i+vec3(1,0,1)),f.x),
                   mix(mhash(i+vec3(0,1,1)),mhash(i+vec3(1,1,1)),f.x),f.y),f.z);
  }
  float mfbm(vec3 p){ float a=0.5, s=0.0; for(int i=0;i<4;i++){ s+=a*mnoise(p); p*=2.07; a*=0.5; } return s; }
`;

export function createMineMaterial() {
  const mat = new THREE.MeshStandardMaterial({
    vertexColors: true,
    roughness: 0.92,
    metalness: 0.0,
    side: THREE.FrontSide,
    polygonOffset: true,
    polygonOffsetFactor: 1,
    polygonOffsetUnits: 1,
  });
  mat.onBeforeCompile = (shader) => {
    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', `#include <common>
        attribute float metal;
        varying float vMetal;
        varying vec3 vWPos;
        varying vec3 vWNrm;`)
      .replace('#include <worldpos_vertex>', `#include <worldpos_vertex>
        vMetal = metal;
        vWPos = (modelMatrix * vec4(transformed, 1.0)).xyz;
        vWNrm = normalize(mat3(modelMatrix) * objectNormal);`);
    shader.fragmentShader = shader.fragmentShader
      .replace('#include <common>', `#include <common>
        varying float vMetal;
        varying vec3 vWPos;
        varying vec3 vWNrm;
        ${NOISE_GLSL}
        float surfHeight(vec3 p, float metalW, float floorW){
          float rock = mfbm(p*1.3)*0.7 + mfbm(p*6.0)*0.3;
          // tread plate: diamond bumps on junction plates
          vec2 q = p.xz*3.0; vec2 g = abs(fract(q)-0.5);
          float tread = smoothstep(0.18, 0.05, abs(g.x-g.y)) * 0.35 + mnoise(p*9.0)*0.08;
          float conc = mnoise(p*5.0)*0.35 + mnoise(p*14.0)*0.12;
          float ground = mix(conc, tread, clamp(metalW*1.3,0.0,1.0));
          return mix(rock, ground, floorW);
        }`)
      .replace('#include <color_fragment>', `#include <color_fragment>
        float floorW = smoothstep(0.55, 0.85, vWNrm.y);
        float grain = mfbm(vWPos*2.2);
        diffuseColor.rgb *= mix(0.75 + 0.55*grain, 0.85 + 0.3*mnoise(vWPos*4.0), floorW);
        // damp patches on the rock + dust on the road
        float wet = smoothstep(0.62, 0.72, mfbm(vWPos*0.35 + 3.0)) * (1.0-floorW);
        diffuseColor.rgb *= 1.0 - wet*0.35;`)
      .replace('#include <roughnessmap_fragment>', `
        float roughnessFactor = mix(roughness, 0.35, clamp(vMetal,0.0,1.0));
        roughnessFactor = mix(roughnessFactor, 0.28, wet);`)
      .replace('#include <metalnessmap_fragment>', `float metalnessFactor = vMetal;`)
      .replace('#include <normal_fragment_maps>', `#include <normal_fragment_maps>
        {
          float h = surfHeight(vWPos, vMetal, floorW);
          vec3 dpdx = dFdx(-vViewPosition), dpdy = dFdy(-vViewPosition);
          float dhx = dFdx(h), dhy = dFdy(h);
          vec3 r1 = cross(dpdy, normal), r2 = cross(normal, dpdx);
          float det = dot(dpdx, r1);
          float strength = mix(0.9, 0.18, floorW);
          vec3 grad = sign(det) * (dhx * r1 + dhy * r2);
          normal = normalize(abs(det) * normal - strength * grad);
        }`);
  };
  return mat;
}

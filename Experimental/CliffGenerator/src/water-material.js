// River / lake water: a standard material whose colour and opacity follow the water depth
// (per-vertex `aDepth` from buildWaterGeometry) — shallow water is clear and shows the gravel
// bed, deeper water takes the water colour, so shores and bars fade in instead of ending in a
// hard dark edge.
import * as THREE from 'three';

export function makeWaterBodyUniforms() {
  return { uShallow: { value: new THREE.Color(0x4f7f7a) }, uClearDepth: { value: 1.5 }, uFoam: { value: 1 } };
}

export function makeWaterBodyMaterial(uniforms) {
  const material = new THREE.MeshStandardMaterial({ color: 0x1d4552, roughness: 0.16, metalness: 0.05, transparent: true, opacity: 0.85, envMapIntensity: 1.2 });
  material.onBeforeCompile = (shader) => {
    shader.uniforms.uShallow = uniforms.uShallow;
    shader.uniforms.uClearDepth = uniforms.uClearDepth;
    shader.uniforms.uFoam = uniforms.uFoam;
    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', 'attribute float aDepth;\nattribute float aFoam;\nvarying float vDepth;\nvarying float vFoam;\nvarying vec3 vWp;\n#include <common>')
      .replace('#include <begin_vertex>', '#include <begin_vertex>\nvDepth = aDepth;\nvFoam = aFoam;\nvWp = ( modelMatrix * vec4( transformed, 1.0 ) ).xyz;');
    shader.fragmentShader = shader.fragmentShader
      .replace('#include <common>', 'uniform vec3 uShallow;\nuniform float uClearDepth;\nuniform float uFoam;\nvarying float vDepth;\nvarying float vFoam;\nvarying vec3 vWp;\n#include <common>\nfloat foamHash( vec2 p ) { return fract( sin( dot( p, vec2( 127.1, 311.7 ) ) ) * 43758.5453 ); }\nfloat foamNoise( vec2 p ) { vec2 i = floor( p ), f = fract( p ); f = f * f * ( 3.0 - 2.0 * f ); return mix( mix( foamHash( i ), foamHash( i + vec2( 1.0, 0.0 ) ), f.x ), mix( foamHash( i + vec2( 0.0, 1.0 ) ), foamHash( i + vec2( 1.0, 1.0 ) ), f.x ), f.y ); }')
      .replace('#include <color_fragment>', '#include <color_fragment>\ndiffuseColor.rgb = mix( uShallow, diffuseColor.rgb, smoothstep( 0.0, uClearDepth * 4.0, vDepth ) );\ndiffuseColor.a *= mix( 0.35, 1.0, smoothstep( 0.0, uClearDepth, vDepth ) );\nfloat foamN = foamNoise( vWp.xz * 0.9 ) * 0.6 + foamNoise( vWp.xz * 3.7 + 11.0 ) * 0.4;\nfloat foam = uFoam * vFoam * smoothstep( 0.25, 0.75, foamN + vFoam * 0.5 );\ndiffuseColor.rgb = mix( diffuseColor.rgb, vec3( 0.92, 0.95, 0.96 ), foam );\ndiffuseColor.a = max( diffuseColor.a, foam * 0.95 );')
      .replace('#include <roughnessmap_fragment>', '#include <roughnessmap_fragment>\nroughnessFactor = mix( roughnessFactor, 0.85, uFoam * vFoam );');
  };
  material.customProgramCacheKey = () => 'water-body';
  return material;
}

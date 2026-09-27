import * as THREE from 'three';

// Shared uniforms
export const furUniforms = {
  uShells: { value: 16 },
  uPixelWorld: { value: 0.001 }, // world-space size of one pixel at distance 1 (updated on resize)
  uCombStrength: { value: 1.0 },
  uGravity: { value: new THREE.Vector3(0, -1, 0) },
};

const HASH = /* glsl */ `
float fHash12(vec2 p){ vec3 p3 = fract(vec3(p.xyx) * .1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
vec2 fHash22(vec2 p){ vec3 p3 = fract(vec3(p.xyx) * vec3(.1031, .1030, .0973)); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.xx + p3.yz) * p3.zy); }
`;

function sheenTint(shader) {
  // Tint the velvet sheen by the local hair colour (black setae keep only a faint cool sheen).
  // The chunk must be expanded first: onBeforeCompile sees unresolved #include directives.
  shader.fragmentShader = shader.fragmentShader.replace(
    '#include <lights_physical_fragment>',
    THREE.ShaderChunk.lights_physical_fragment.replace(
      'material.sheenColor = sheenColor;',
      'material.sheenColor = sheenColor * (vec3(0.03) + vColor.rgb * 0.55);'
    )
  );
}

// ------------------------------------------------------------------------------------------
// Cuticle / fur-root layer. Rendered opaque, casts shadows.
// ------------------------------------------------------------------------------------------
export function createFurBaseMaterial() {
  const m = new THREE.MeshPhysicalMaterial({
    vertexColors: true,
    roughness: 0.9,
    metalness: 0,
    sheen: 1.0,
    sheenRoughness: 0.6,
    sheenColor: new THREE.Color(1, 1, 1),
    specularIntensity: 0.12,
  });
  m.onBeforeCompile = (shader) => {
    shader.fragmentShader = shader.fragmentShader.replace(
      '#include <color_fragment>',
      `#include <color_fragment>
       diffuseColor.rgb *= 0.34; // hair roots + cuticle between setae`
    );
    sheenTint(shader);
  };
  m.customProgramCacheKey = () => 'furBase';
  return m;
}

// ------------------------------------------------------------------------------------------
// Shell fur (dense short pile). Rendered as N instanced shells (gl_InstanceID = shell index).
// Strand identity comes from a jittered cell grid in "fur UV" space (centimetres), each strand
// tapering to a point; sub-pixel strands resolve to their expected coverage (alpha-to-coverage),
// so the pile never shimmers or moires at distance.
// ------------------------------------------------------------------------------------------
export function createFurShellMaterial() {
  const m = new THREE.MeshPhysicalMaterial({
    vertexColors: true,
    roughness: 0.85,
    metalness: 0,
    sheen: 1.0,
    sheenRoughness: 0.5,
    sheenColor: new THREE.Color(1, 1, 1),
    specularIntensity: 0.14,
    alphaToCoverage: true,
  });
  m.onBeforeCompile = (shader) => {
    Object.assign(shader.uniforms, furUniforms);
    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', `#include <common>
        attribute vec3 aFur;      // x: pile length (cm), y: strands per cm, z: stiffness 0..1
        attribute vec3 aComb;     // object-space combing direction
        attribute vec2 aFurUV;    // cm-scaled surface parameterisation
        uniform float uShells; uniform float uCombStrength; uniform vec3 uGravity;
        varying vec2 vFurUV; varying vec3 vFur; varying float vShellH;`)
      .replace('#include <begin_vertex>', `#include <begin_vertex>
        float sh = (float(gl_InstanceID) + 1.0) / uShells;
        vShellH = sh; vFurUV = aFurUV; vFur = aFur;
        vec3 gObj = transpose(mat3(modelMatrix)) * uGravity;
        float droop = (1.0 - aFur.z) * 0.35;
        transformed += normal * aFur.x * sh + (aComb * uCombStrength + gObj * droop) * aFur.x * sh * sh * 0.9;`);
    shader.fragmentShader = shader.fragmentShader
      .replace('#include <common>', `#include <common>
        varying vec2 vFurUV; varying vec3 vFur; varying float vShellH;
        ${HASH}`)
      .replace('#include <color_fragment>', `#include <color_fragment>
        {
          vec2 g = vFurUV * vFur.y;
          vec2 id = floor(g), f = fract(g);
          float best = 8.0, rl = 0.0;
          for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
            vec2 o = vec2(float(i), float(j));
            vec2 d = o + 0.15 + 0.7 * fHash22(id + o) - f;
            float dd = dot(d, d);
            if (dd < best) { best = dd; rl = fHash12(id + o + 17.17); }
          }
          best = sqrt(best);
          float h = vShellH;
          float len = mix(0.45, 1.0, rl);
          float tn = h / len;
          float radius = 0.4 * pow(max(1.0 - tn, 0.0), 0.8);
          float aa = max(fwidth(best), 1e-4) * 0.8;
          float mask = (1.0 - smoothstep(radius - aa, radius + aa, best)) * step(tn, 1.0);
          float cellPx = 1.0 / max(length(fwidth(g)), 1e-5);
          float avgCov = clamp(0.62 * pow(max(1.0 - h, 0.0), 1.7), 0.0, 1.0);
          mask = mix(avgCov, mask, smoothstep(1.4, 4.0, cellPx));
          if (mask < 0.01) discard;
          diffuseColor.a = mask;
          diffuseColor.rgb *= mix(0.32, 1.12, pow(h, 0.8)) * mix(0.8, 1.2, rl);
        }`);
    sheenTint(shader);
  };
  m.customProgramCacheKey = () => 'furShell';
  return m;
}

// ------------------------------------------------------------------------------------------
// Guard setae: long individual hairs as camera-facing ribbons expanded in the vertex shader.
// Width is clamped to ~1 px and the excess converted to coverage -> no aliasing / sparkle.
// ------------------------------------------------------------------------------------------
export function createHairStrandMaterial() {
  const m = new THREE.MeshPhysicalMaterial({
    vertexColors: true,
    roughness: 0.5,
    metalness: 0,
    sheen: 0.5,
    sheenRoughness: 0.4,
    sheenColor: new THREE.Color(1, 1, 1),
    specularIntensity: 0.35,
    side: THREE.DoubleSide,
    alphaToCoverage: true,
  });
  m.onBeforeCompile = (shader) => {
    Object.assign(shader.uniforms, furUniforms);
    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', `#include <common>
        attribute vec3 aTan; attribute float aSide; attribute float aWidth; attribute float aT;
        uniform float uPixelWorld;
        varying float vHairA; varying float vHairT;`)
      .replace('#include <begin_vertex>', `#include <begin_vertex>
        {
          vec4 wp = modelMatrix * vec4(position, 1.0);
          mat3 m3 = mat3(modelMatrix);
          vec3 tW = normalize(m3 * aTan);
          vec3 V = cameraPosition - wp.xyz;
          float dist = length(V); V /= max(dist, 1e-5);
          vec3 sW = cross(tW, V);
          float sl = length(sW);
          sW = sl > 1e-4 ? sW / sl : normalize(cross(tW, vec3(0.0, 1.0, 0.0)) + 1e-4);
          float w = aWidth * (1.0 - aT * 0.8);
          float px = uPixelWorld * dist;
          float wr = max(w, px * 0.9);
          vHairA = clamp(w / wr, 0.0, 1.0);
          vHairT = aT;
          transformed += transpose(m3) * sW * (aSide * wr * 0.5);
        }`);
    shader.fragmentShader = shader.fragmentShader
      .replace('#include <common>', `#include <common>
        varying float vHairA; varying float vHairT;`)
      .replace('#include <color_fragment>', `#include <color_fragment>
        diffuseColor.a = vHairA * (1.0 - smoothstep(0.82, 1.0, vHairT));
        if (diffuseColor.a < 0.01) discard;`);
    sheenTint(shader);
  };
  m.customProgramCacheKey = () => 'hairStrand';
  return m;
}

export function createChitinMaterial(color = 0x0b0605, opts = {}) {
  return new THREE.MeshPhysicalMaterial({
    color, roughness: 0.28, metalness: 0, clearcoat: 1.0, clearcoatRoughness: 0.12,
    specularIntensity: 1.0, ...opts,
  });
}

export function createFangMaterial() {
  // Black sclerotised fang with a translucent reddish-brown tip (vertex colour gradient)
  return new THREE.MeshPhysicalMaterial({
    vertexColors: true, roughness: 0.22, metalness: 0, clearcoat: 1.0, clearcoatRoughness: 0.08,
    specularIntensity: 1.0,
  });
}

export function createEyeMaterial() {
  return new THREE.MeshPhysicalMaterial({
    color: 0x050506, roughness: 0.04, metalness: 0, clearcoat: 1.0, clearcoatRoughness: 0.02,
    iridescence: 0.35, iridescenceIOR: 1.6, iridescenceThicknessRange: [250, 600], ior: 1.5,
    specularIntensity: 1.0,
  });
}

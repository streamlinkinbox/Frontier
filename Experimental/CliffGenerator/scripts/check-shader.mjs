// Assembles the injected surface shader exactly as Three.js would and validates it with
// glslangValidator (GLSL ES 3.00). Usage: node scripts/check-shader.mjs [path-to-glslangValidator]
import * as THREE from 'three';
import { WebGLProgram } from 'three/src/renderers/webgl/WebGLProgram.js';
import { writeFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { makeSurfaceUniforms, makeSurfaceMaterial } from '../src/surface-shader.js';
import { makeWaterBodyMaterial, makeWaterBodyUniforms } from '../src/water-material.js';

const validator = process.argv[2] || 'glslangValidator';
const sources = {};
const gl = new Proxy({}, {
  get(_, prop) {
    if (prop === 'shaderSource') return (shader, src) => { sources[shader.type] = src; };
    if (prop === 'createShader') return (type) => ({ type: type === 0x8B31 ? 'vert' : 'frag' });
    if (prop === 'VERTEX_SHADER') return 0x8B31;
    if (prop === 'FRAGMENT_SHADER') return 0x8B30;
    if (prop === 'getShaderParameter' || prop === 'getProgramParameter') return () => true;
    if (prop === 'getShaderInfoLog' || prop === 'getProgramInfoLog') return () => '';
    return () => ({});
  },
});
const renderer = { getContext: () => gl, debug: { checkShaderErrors: false } };

let failed = false;
for (const kind of ['terrain', 'rock', 'water']) {
  const isRock = kind === 'rock';
  const material = kind === 'water' ? makeWaterBodyMaterial(makeWaterBodyUniforms()) : makeSurfaceMaterial(makeSurfaceUniforms(), { isRock });
  const lib = THREE.ShaderLib.standard;
  const shader = { uniforms: THREE.UniformsUtils.clone(lib.uniforms), vertexShader: lib.vertexShader, fragmentShader: lib.fragmentShader };
  material.onBeforeCompile(shader, renderer);
  const parameters = {
    shaderType: 'MeshStandardMaterial', shaderName: 'MeshStandardMaterial', defines: { STANDARD: '' },
    vertexShader: shader.vertexShader, fragmentShader: shader.fragmentShader, glslVersion: null, precision: 'highp',
    instancing: isRock, instancingColor: isRock, shadowMapEnabled: true, shadowMapType: THREE.PCFSoftShadowMap, envMap: true, envMapMode: THREE.CubeUVReflectionMapping,
    envMapCubeUVHeight: 256, combine: THREE.MultiplyOperation, useFog: true, fog: true, fogExp2: true, toneMapping: THREE.ACESFilmicToneMapping,
    outputColorSpace: THREE.SRGBColorSpace, opaque: true, numDirLights: 1, numDirLightShadows: 1, numPointLights: 0, numSpotLights: 0, numSpotLightMaps: 0,
    numSpotLightShadowsWithMaps: 0, numRectAreaLights: 0, numHemiLights: 1, numPointLightShadows: 0, numSpotLightShadows: 0, numClippingPlanes: 0, numClipIntersection: 0,
    numLightProbes: 0, rendererExtensionFragDepth: true, rendererExtensionDrawBuffers: true, rendererExtensionShaderTextureLod: true, isWebGL2: true,
    customProgramCacheKey: '', uniforms: shader.uniforms, extensionClipCullDistance: false, extensionMultiDraw: false,
  };
  new WebGLProgram(renderer, 'key', parameters, { releaseStatesOfProgram() {} });
  for (const stage of ['vert', 'frag']) {
    const file = `/tmp/cliff-${kind}.${stage}`;
    writeFileSync(file, sources[stage].replace(/\baverage\(/g, "average_("));
    const run = spawnSync(validator, [file], { encoding: 'utf8' });
    if (run.error) { console.error(run.error.message); process.exit(2); }
    const out = String(run.stdout || "") + String(run.stderr || "");
    const ok = run.status === 0;
    console.log(`${kind.padEnd(7)} ${stage}: ${ok ? 'OK' : 'FAILED'}`);
    if (!ok) { failed = true; console.log(out.split('\n').filter((l) => /ERROR|WARNING/.test(l)).slice(0, 30).join('\n')); }
  }
}
process.exit(failed ? 1 : 0);

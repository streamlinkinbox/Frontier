/**
 * MATERIALS
 *
 * Physically-based cuticle. Mosquito chitin is not a flat brown: it is a dark
 * sclerotised surface with a strong thin-film sheen (this is why hand-lens
 * photos of resting mosquitoes show blue/green/gold highlights), micro-sculpted
 * microtrichia, and pale lanceolate scales in specific places.
 *
 * Every map is procedural (see ../render/textures.js), so the whole build has
 * zero external asset dependencies.
 */

import * as THREE from 'three';
import {
  makeCuticle, scutumPattern, makeCompoundEye, makeLegStrip,
  makeWingMaps, makeAbdomenMap, makeRadialAlpha,
} from '../render/textures.js';

export function buildMaterials(env, quality = 1) {
  const S = quality; // 0.5 = low-res maps on constrained hardware
  const px = (n) => Math.max(256, Math.round(n * S));

  /* ---------------------------------------------------------- cuticle */
  const cuticleMap = makeCuticle({
    w: px(1024), h: px(1024), base: '#1a1512', seed: 12,
  });
  cuticleMap.repeat.set(2, 2);

  const cuticleRough = makeRadialAlpha(px(256), 1.0);

  const cuticle = new THREE.MeshPhysicalMaterial({
    color: 0xffffff,
    map: cuticleMap,
    roughness: 0.36,
    metalness: 0.0,
    // Cuticle is a multilayer dielectric: the sheen is thin-film interference.
    iridescence: 0.55,
    iridescenceIOR: 1.42,
    iridescenceThicknessRange: [180, 520],
    clearcoat: 0.55,
    clearcoatRoughness: 0.22,
    sheen: 0.25,
    sheenColor: new THREE.Color(0x3a5a70),
    envMapIntensity: 1.15,
  });
  cuticle.name = 'Cuticle';

  /* ----------------------------------------------------------- scutum */
  const scutumMap = makeCuticle({
    w: px(1024), h: px(1024), base: '#141010',
    pattern: scutumPattern, seed: 77,
  });
  const scutum = cuticle.clone();
  scutum.map = scutumMap;
  scutum.roughness = 0.42;
  scutum.iridescence = 0.75;
  scutum.name = 'Scutum';

  /* -------------------------------------------------------------- eye */
  const eyeMaps = makeCompoundEye(px(1024), [0.26, 0.42, 0.23]);
  const eye = new THREE.MeshPhysicalMaterial({
    map: eyeMaps.map,
    roughnessMap: eyeMaps.roughnessMap,
    roughness: 0.30,
    metalness: 0.05,
    iridescence: 0.9,
    iridescenceIOR: 1.5,
    iridescenceThicknessRange: [260, 700],
    clearcoat: 0.9,
    clearcoatRoughness: 0.06,
    envMapIntensity: 1.4,
  });
  eye.name = 'CompoundEye';

  const ocellus = new THREE.MeshPhysicalMaterial({
    color: 0x0a0d0a, roughness: 0.08, metalness: 0.0,
    clearcoat: 1, clearcoatRoughness: 0.03, iridescence: 1, iridescenceIOR: 1.6,
  });

  /* -------------------------------------------------------- pale scale */
  const paleScale = new THREE.MeshPhysicalMaterial({
    color: 0xd9dccb, roughness: 0.48, metalness: 0.0,
    iridescence: 0.6, iridescenceIOR: 1.35, iridescenceThicknessRange: [200, 460],
    sheen: 0.5, sheenColor: new THREE.Color(0xbfd4d0), envMapIntensity: 1.0,
  });

  /* -------------------------------------------------------------- seta */
  const seta = new THREE.MeshStandardMaterial({
    color: 0x0e0b08, roughness: 0.52, metalness: 0.0, envMapIntensity: 0.7,
  });

  /* ---------------------------------------------------------- labium */
  const labium = new THREE.MeshPhysicalMaterial({
    color: 0x2a211a, roughness: 0.34, metalness: 0.0,
    iridescence: 0.45, iridescenceIOR: 1.4, iridescenceThicknessRange: [150, 420],
    clearcoat: 0.6, clearcoatRoughness: 0.2, envMapIntensity: 1.0,
  });
  const labiumTip = labium.clone(); labiumTip.color = new THREE.Color(0x1c1611);
  const stylet = new THREE.MeshPhysicalMaterial({
    color: 0x6b5a48, roughness: 0.18, metalness: 0.25,
    clearcoat: 1, clearcoatRoughness: 0.05, envMapIntensity: 1.6,
  });
  const membrane = new THREE.MeshPhysicalMaterial({
    color: 0x6d4a3c, roughness: 0.22, metalness: 0.0,
    transmission: 0.25, thickness: 0.02, ior: 1.36,
    clearcoat: 0.8, clearcoatRoughness: 0.1,
  });

  /* -------------------------------------------------------------- legs */
  // Three strip maps, one per leg pair (S14/S15: fore < mid < hind, and the
  // bands differ per pair).
  const legMats = {};
  for (const key of ['fore', 'mid', 'hind']) {
    const map = makeLegStrip({
      bands: key === 'fore' ? 2 : key === 'mid' ? 3 : 4,
      femurStripe: key !== 'hind',
      kneeSpot: true,
      tibialBands: key === 'fore' ? 1 : 2,
      seed: key === 'fore' ? 5 : key === 'mid' ? 9 : 17,
    });
    legMats[key] = cuticle.clone();
    legMats[key].map = map;
    legMats[key].roughness = 0.38;
    legMats[key].clearcoat = 0.45;
  }
  const pretarsus = new THREE.MeshPhysicalMaterial({
    color: 0x241b14, roughness: 0.30, metalness: 0.05,
    iridescence: 0.4, clearcoat: 0.7, clearcoatRoughness: 0.15,
  });

  /* -------------------------------------------------------------- wing */
  const wm = makeWingMaps(px(1024));
  const wing = new THREE.MeshStandardMaterial({
    map: wm.map,
    alphaMap: wm.alphaMap,
    transparent: true,
    opacity: 0.92,
    alphaTest: 0.04,
    side: THREE.DoubleSide,
    roughness: 0.30,
    metalness: 0.0,
    envMapIntensity: 0.9,
    // Membrane is thin and lit from both sides; a little emissive keeps the
    // venation readable against a bright sky.
    emissive: 0x1a1712,
    emissiveIntensity: 0.35,
  });
  wing.name = 'WingMembrane';
  const vein = new THREE.MeshPhysicalMaterial({
    color: 0x1a1512, roughness: 0.36, metalness: 0.0,
    iridescence: 0.5, clearcoat: 0.5, clearcoatRoughness: 0.2, envMapIntensity: 0.9,
  });

  /* ----------------------------------------------------------- abdomen */
  const abdomenMap = makeAbdomenMap(px(1024), px(512));
  abdomenMap.repeat.set(1, 1);
  const abdomenUniforms = {
    uEngorge: { value: 0 },
    uBlood: { value: 0 },
    uTime: { value: 0 },
  };
  const abdomen = cuticle.clone();
  abdomen.map = abdomenMap;
  abdomen.roughness = 0.34;
  abdomen.name = 'Abdomen';
  abdomen.userData.uniforms = abdomenUniforms;
  abdomen.onBeforeCompile = (shader) => {
    Object.assign(shader.uniforms, abdomenUniforms);
    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', `
        #include <common>
        uniform float uEngorge;
        uniform float uTime;
      `)
      .replace('#include <begin_vertex>', `
        #include <begin_vertex>
        // Blood-engorgement: the tergites balloon outward and the whole
        // abdomen lengthens. Radially outward in the segment's local frame
        // (the tergite mesh is swept along local +Y, so radial = local XZ).
        float rad = length(transformed.xz);
        if (rad > 1e-5) {
          float belly = 1.0 - 0.35 * pow(abs(normalize(transformed.xz).y * 0.0 + transformed.z / rad) , 0.0);
          float swell = 1.0 + uEngorge * 0.90 * (0.55 + 0.45 * pow(max(0.0, transformed.z / rad * 0.5 + 0.5), 1.5));
          transformed.xz *= swell;
          // Slight ventral bias — the gut fills the underside first
          transformed.y *= 1.0 + uEngorge * 0.10;
        }
        // Peristaltic ripple while the cibarial pump runs
        transformed.xz *= 1.0 + 0.020 * uEngorge * sin(transformed.y * 26.0 - uTime * 5.0);
      `);
    shader.fragmentShader = shader.fragmentShader
      .replace('#include <common>', `
        #include <common>
        uniform float uEngorge;
        uniform float uBlood;
      `)
      .replace('#include <map_fragment>', `
        #include <map_fragment>
        // The engorged gut shows through the translucent tergal cuticle.
        vec3 gut = vec3(0.34, 0.035, 0.030);
        float ventral = 1.0 - clamp(abs(vMapUv.y * 2.0 - 1.0), 0.0, 1.0);
        float bleed = uBlood * (0.35 + 0.65 * pow(ventral, 1.6));
        diffuseColor.rgb = mix(diffuseColor.rgb, gut, clamp(bleed, 0.0, 0.92));
        diffuseColor.rgb *= 1.0 + uEngorge * 0.12;
      `);
    abdomen.userData.shader = shader;
  };
  abdomen.customProgramCacheKey = () => 'abdomen-engorge-v1';

  /* ------------------------------------------------------------- misc */
  const blood = new THREE.MeshPhysicalMaterial({
    color: 0x5a0a08, roughness: 0.08, metalness: 0.0,
    transmission: 0.6, thickness: 0.08, ior: 1.35,
    clearcoat: 1, clearcoatRoughness: 0.05, envMapIntensity: 1.5,
  });
  const saliva = new THREE.MeshPhysicalMaterial({
    color: 0xdfe8e4, roughness: 0.02, metalness: 0.0,
    transmission: 0.85, thickness: 0.02, ior: 1.33,
    transparent: true, opacity: 0.75, clearcoat: 1, clearcoatRoughness: 0.0,
  });

  const all = {
    cuticle, scutum, eye, ocellus, paleScale, seta,
    labium, labiumTip, stylet, membrane,
    pretarsus, wing, vein, abdomen, blood, saliva,
    legFore: legMats.fore, legMid: legMats.mid, legHind: legMats.hind,
  };

  if (env) for (const k in all) { all[k].envMap = env; all[k].needsUpdate = true; }

  all.legFor = { L1: legMats.fore, R1: legMats.fore, L2: legMats.mid, R2: legMats.mid, L3: legMats.hind, R3: legMats.hind };
  all.abdomenUniforms = abdomenUniforms;
  return all;
}

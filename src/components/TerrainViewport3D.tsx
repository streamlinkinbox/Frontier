import React, { useEffect, useRef } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { ExtractedSDFMesh } from '../engine/marchingCubes';
import { SDFTerrainVolume } from '../engine/terrainState';

interface TerrainViewport3DProps {
  meshData: ExtractedSDFMesh | null;
  volume: SDFTerrainVolume | null;
  wireframe?: boolean;
  showRiverWater?: boolean;
}

export const TerrainViewport3D: React.FC<TerrainViewport3DProps> = ({
  meshData,
  volume,
  wireframe = false,
}) => {
  const mountRef = useRef<HTMLDivElement | null>(null);
  const sceneRef = useRef<THREE.Scene | null>(null);
  const terrainMeshRef = useRef<THREE.Mesh | null>(null);
  const rendererRef = useRef<THREE.WebGLRenderer | null>(null);

  useEffect(() => {
    const container = mountRef.current;
    if (!container) return;

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x7c7d80);
    scene.fog = new THREE.FogExp2(0x6d6e72, 0.00045);
    sceneRef.current = scene;

    const camera = new THREE.PerspectiveCamera(
      42,
      container.clientWidth / Math.max(1, container.clientHeight),
      1.0,
      100000
    );
    camera.position.set(410, 265, 430);

    const renderer = new THREE.WebGLRenderer({
      antialias: true,
      powerPreference: 'high-performance',
    });
    renderer.setSize(container.clientWidth, container.clientHeight);
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.04;
    container.appendChild(renderer.domElement);
    rendererRef.current = renderer;

    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.06;
    controls.target.set(0, 48, 0);
    controls.maxDistance = 25000;
    controls.minDistance = 8;
    controls.update();

    const groundGeo = new THREE.PlaneGeometry(4000, 4000);
    const groundMat = new THREE.MeshBasicMaterial({
      color: 0x5c5d63,
      depthWrite: true,
    });
    const groundPlane = new THREE.Mesh(groundGeo, groundMat);
    groundPlane.rotation.x = -Math.PI / 2;
    groundPlane.position.y = -0.6;
    scene.add(groundPlane);

    const gridHelper = new THREE.GridHelper(2400, 120, 0x2e2f33, 0x3a3b40);
    gridHelper.position.y = -0.25;
    scene.add(gridHelper);

    const xAxisGeo = new THREE.BufferGeometry().setFromPoints([
      new THREE.Vector3(-1200, 0.05, 0),
      new THREE.Vector3(1200, 0.05, 0),
    ]);
    const xAxisMat = new THREE.LineBasicMaterial({ color: 0xd94848 });
    scene.add(new THREE.Line(xAxisGeo, xAxisMat));

    const zAxisGeo = new THREE.BufferGeometry().setFromPoints([
      new THREE.Vector3(0, 0.05, -1200),
      new THREE.Vector3(0, 0.05, 1200),
    ]);
    const zAxisMat = new THREE.LineBasicMaterial({ color: 0x3d84d9 });
    scene.add(new THREE.Line(zAxisGeo, zAxisMat));

    const hemiLight = new THREE.HemisphereLight(0xf3f6fc, 0x38322c, 0.86);
    scene.add(hemiLight);

    const sunLight = new THREE.DirectionalLight(0xfff4e0, 2.15);
    sunLight.position.set(420, 480, 260);
    sunLight.castShadow = true;
    sunLight.shadow.mapSize.width = 2048;
    sunLight.shadow.mapSize.height = 2048;
    sunLight.shadow.camera.near = 50;
    sunLight.shadow.camera.far = 1600;
    const d = 360;
    sunLight.shadow.camera.left = -d;
    sunLight.shadow.camera.right = d;
    sunLight.shadow.camera.top = d;
    sunLight.shadow.camera.bottom = -d;
    sunLight.shadow.bias = -0.0004;
    scene.add(sunLight);

    const rimLight = new THREE.DirectionalLight(0x9dc1ff, 0.58);
    rimLight.position.set(-360, 180, -310);
    scene.add(rimLight);

    let animId = 0;
    const animate = () => {
      animId = requestAnimationFrame(animate);
      controls.update();
      renderer.render(scene, camera);
    };
    animate();

    const handleResize = () => {
      if (!container) return;
      const w = container.clientWidth;
      const h = Math.max(1, container.clientHeight);
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
      renderer.setSize(w, h);
    };

    const resizeObserver = new ResizeObserver(handleResize);
    resizeObserver.observe(container);

    return () => {
      cancelAnimationFrame(animId);
      resizeObserver.disconnect();
      controls.dispose();
      renderer.dispose();
      if (renderer.domElement.parentNode === container) {
        container.removeChild(renderer.domElement);
      }
    };
  }, []);

  useEffect(() => {
    const scene = sceneRef.current;
    if (!scene) return;

    if (terrainMeshRef.current) {
      scene.remove(terrainMeshRef.current);
      terrainMeshRef.current.geometry.dispose();
      (terrainMeshRef.current.material as THREE.Material).dispose();
      terrainMeshRef.current = null;
    }

    if (!meshData || meshData.vertexCount === 0) return;

    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.BufferAttribute(meshData.positions, 3));
    geometry.setAttribute('normal', new THREE.BufferAttribute(meshData.normals, 3));
    geometry.setAttribute('color', new THREE.BufferAttribute(meshData.colors, 3));
    geometry.setIndex(new THREE.BufferAttribute(meshData.indices, 1));
    geometry.computeBoundingSphere();

    const faultOffset = volume?.strataConfig?.faultOffset ?? 14.0;
    const faultAngleRad = ((volume?.strataConfig?.faultAngle ?? 32.0) * Math.PI) / 180.0;
    const faultDip = volume?.strataConfig?.faultDip ?? 0.28;

    const material = new THREE.MeshStandardMaterial({
      vertexColors: true,
      roughness: 0.80,
      metalness: 0.04,
      side: THREE.DoubleSide,
      wireframe,
      flatShading: false,
    });

    // Non-Uniform Stratigraphic Column Shader:
    // Includes 3D dipping tectonic fault line, pinch-out seam taper, warm-umber crevice AO,
    // and vertical desert varnish weathering streaks!
    material.onBeforeCompile = (shader) => {
      shader.uniforms.uFaultOffset = { value: faultOffset };
      shader.uniforms.uFaultCos = { value: Math.cos(faultAngleRad) };
      shader.uniforms.uFaultSin = { value: Math.sin(faultAngleRad) };
      shader.uniforms.uFaultDip = { value: faultDip };

      shader.vertexShader = shader.vertexShader.replace(
        '#include <common>',
        `#include <common>
        varying vec3 vTerrainWorldPos;
        varying vec3 vTerrainWorldNormal;`
      );
      shader.vertexShader = shader.vertexShader.replace(
        '#include <begin_vertex>',
        `#include <begin_vertex>
        vTerrainWorldPos = position;
        vTerrainWorldNormal = normalize(normal);`
      );

      shader.fragmentShader = shader.fragmentShader.replace(
        '#include <common>',
        `#include <common>
        uniform float uFaultOffset;
        uniform float uFaultCos;
        uniform float uFaultSin;
        uniform float uFaultDip;
        varying vec3 vTerrainWorldPos;
        varying vec3 vTerrainWorldNormal;

        float hash31(vec3 p) {
          p = fract(p * 0.1031);
          p += dot(p, p.yzx + 33.33);
          return fract((p.x + p.y) * p.z);
        }

        float noise3D(vec3 x) {
          vec3 i = floor(x);
          vec3 f = fract(x);
          f = f * f * (3.0 - 2.0 * f);
          return mix(
            mix(mix(hash31(i + vec3(0,0,0)), hash31(i + vec3(1,0,0)), f.x),
                mix(hash31(i + vec3(0,1,0)), hash31(i + vec3(1,1,0)), f.x), f.y),
            mix(mix(hash31(i + vec3(0,0,1)), hash31(i + vec3(1,0,1)), f.x),
                mix(hash31(i + vec3(0,1,1)), hash31(i + vec3(1,1,1)), f.x), f.y),
            f.z
          );
        }`
      );

      shader.fragmentShader = shader.fragmentShader.replace(
        '#include <color_fragment>',
        `#include <color_fragment>
        vec3 wp = vTerrainWorldPos;
        vec3 wn = normalize(vTerrainWorldNormal);
        float cliffFactor = clamp((0.82 - abs(wn.y)) * 2.1, 0.0, 1.0);

        // 1. Multi-Block 3D Dipping Tectonic Faults + Spatially Varying Dip & Fold
        // Ensures different monoliths sit at different stratigraphic elevations and tilt angles!
        float faultWarp = (noise3D(vec3(wp.x * 0.0082, 0.3, wp.z * 0.0082)) - 0.5) * 42.0;
        float faultCoord = wp.x * uFaultCos + wp.z * uFaultSin + (wp.y - 95.0) * uFaultDip + faultWarp;
        float faultTanh = tanh(faultCoord * 0.095);
        float faultStep = faultTanh * uFaultOffset;
        float faultScar = max(0.0, 1.0 - faultTanh * faultTanh);

        // Secondary cross-fault & per-formation block shift
        float blockShift = (noise3D(vec3(wp.x * 0.0048 - 17.3, 0.7, wp.z * 0.0048 + 41.9)) - 0.5) * 52.0;
        float crossFault = tanh((wp.x * -0.55 + wp.z * 0.83 + faultWarp * 0.65) * 0.058) * (uFaultOffset * 0.65);

        // Spatially varying dip direction (tilts differently on different formations)
        float localDipX = 0.036 + (noise3D(vec3(wp.x * 0.0035, 1.1, wp.z * 0.0035)) - 0.5) * 0.065;
        float localDipZ = -0.028 + (noise3D(vec3(wp.x * 0.0035 + 19.0, 2.3, wp.z * 0.0035)) - 0.5) * 0.065;
        float dip = wp.x * localDipX + wp.z * localDipZ;
        float fold3 = (noise3D(wp * 0.0085) - 0.5) * 14.5;
        float effY = wp.y + dip + faultStep + crossFault + blockShift + fold3;

        // 2. Massive Un-Striped Eolian Sandstone Mask:
        // In real Monument Valley / Wadi Rum cliffs, ~55% of rock faces are massive cliff-forming
        // sandstone where horizontal seams pinch out completely, leaving clean sweeping rock faces!
        float massiveZone = smoothstep(0.36, 0.66, noise3D(vec3(wp.x * 0.0095 - 31.4, wp.y * 0.0075, wp.z * 0.0095 + 23.8)));
        float beddingPresence = 1.0 - massiveZone * 0.82;

        // 3. Irregular Multi-Scale Bed Partition with Strong Lateral Pinch-Outs
        float baseCell = 21.0;
        float cellIdx = floor(effY / baseCell);
        float bestTop = 9999.0;
        float bestBot = -9999.0;
        float activeBedId = cellIdx;

        float bPrev = -9999.0;
        for (int k = -2; k <= 3; k++) {
          float bid = cellIdx + float(k);
          float hSeed = fract(sin(bid * 127.1 + 311.7) * 43758.5453);

          float staticOffset = (hSeed - 0.5) * baseCell * 1.58;
          float latWave = (noise3D(vec3(wp.x * 0.012 + bid * 11.3, 0.2, wp.z * 0.012 - bid * 8.7)) - 0.5) * 26.0;

          float bY = max(bPrev, bid * baseCell + staticOffset + latWave);
          if (effY >= bPrev && effY < bY && (bY - bPrev) > 0.25) {
            bestBot = bPrev;
            bestTop = bY;
            activeBedId = bid;
          }
          bPrev = bY;
        }

        float bedWidth = max(0.5, bestTop - bestBot);
        float u = clamp((effY - bestBot) / bedWidth, 0.0, 1.0);

        // Smooth Pinch-Out Seam Taper: when a band pinches out or enters a massive zone, seams vanish!
        float pinchTaper = smoothstep(1.2, 5.8, bedWidth) * beddingPresence;

        float bedType = fract(sin(activeBedId * 419.2 + 93.1) * 31415.926);
        float bedTone = fract(sin(activeBedId * 173.9 + 19.7) * 65432.1);

        // Strong lateral cut-off so individual seams only run partway across a cliff face!
        float strikeCut = smoothstep(
          0.36 + bedTone * 0.26,
          0.62 + bedTone * 0.24,
          noise3D(vec3(wp.x * 0.014 - activeBedId * 5.7, wp.y * 0.007, wp.z * 0.014 + activeBedId * 9.1))
        ) * pinchTaper;

        // Soft natural bedding contact crevice (only where strikeCut is active!)
        float contactSeam = exp(-min(u, 1.0 - u) * bedWidth * 1.45) * strikeCut;

        // 4. Sweeping Diagonal Aeolian Dune Cross-Bedding (characteristic of desert sandstone!)
        float crossSetDir = (bedTone > 0.48 ? 1.0 : -1.0);
        float crossWarp = (noise3D(wp * 0.025) - 0.5) * 4.5;
        float crossPhase = wp.y * 0.85 + (wp.x * 0.38 - wp.z * 0.34) * crossSetDir + crossWarp;
        float crossForeset = sin(crossPhase) * 0.055 * (0.45 + 0.55 * massiveZone);

        float bandShade = 0.0;
        float bandNormalY = 0.0;

        if (bedWidth > 20.0 || massiveZone > 0.55) {
          // WIDE MASSIVE SANDSTONE FACE: Sweeping diagonal cross-bedding + subtle vertical rock facets
          float rockFacet = (noise3D(vec3(wp.x * 0.048, wp.y * 0.016, wp.z * 0.048)) - 0.5) * 0.11;
          float capLedge = smoothstep(0.78, 0.96, u) * 0.09 * strikeCut;
          bandShade = capLedge + rockFacet + crossForeset;
          bandNormalY = (capLedge * 0.55 - contactSeam * 0.32) + cos(crossPhase) * 0.08 * massiveZone;
        } else if (bedType < 0.36) {
          // LOCALIZED RECESSED SHALE PARTING / ALCOVE SLOT
          float slot = sin(u * 3.14159);
          bandShade = (-slot * 0.14) * strikeCut + crossForeset * 0.5;
          bandNormalY = cos(u * 3.14159) * 0.22 * strikeCut;
        } else {
          // INTERBEDDED SANDSTONE & CROSS-STRATA LEDGE
          bandShade = (crossForeset * 1.3 + (bedTone - 0.5) * 0.09) * (0.35 + 0.65 * strikeCut);
          bandNormalY = (cos(crossPhase) * 0.12 - contactSeam * 0.28) * strikeCut;
        }

        // 5. Strictly Plumb Vertical Tectonic Hairline Joints (Zero Y-wiggle so joints are razor-straight vertically!)
        // Joints run strictly vertically inside each sandstone tier and arrest/step at horizontal bedding seams!
        float tierGroup = floor(activeBedId * 0.5);
        float tierShift = fract(sin(tierGroup * 91.7 + 17.3) * 43758.5453) * 19.0;
        float crackCluster = smoothstep(0.42, 0.70, noise3D(vec3(wp.x * 0.014 + 19.3, 0.5, wp.z * 0.014 - 37.1)));
        float vPlane1 = abs(noise3D(vec3(wp.x * 0.046 + tierShift, 1.3, wp.z * 0.046 - tierShift)) - 0.5);
        float vPlane2 = abs(noise3D(vec3(wp.x * 0.032 - tierShift * 0.7, 2.7, wp.z * 0.032 + tierShift * 0.7)) - 0.5);
        float vertFissure = (
          smoothstep(0.022, 0.0, vPlane1) * 0.85 +
          smoothstep(0.018, 0.0, vPlane2) * 0.65
        ) * crackCluster;

        // 6. Vertical Desert Varnish Runoff Curtains & Fresh Ochre Spall Contrast
        // Dark manganese-iron oxide streaks dripping vertically from upper ledges
        float varnishNoise = noise3D(vec3(wp.x * 0.058, wp.y * 0.0032, wp.z * 0.058));
        float upperWallMask = smoothstep(45.0, 95.0, wp.y);
        float varnishStreak = smoothstep(0.48, 0.80, varnishNoise) * 0.24 * upperWallMask * cliffFactor;

        // Regional 3D mineral color variation (shifts between warm golden ochre & deep burnt sienna)
        float mineralPatch = noise3D(vec3(wp.x * 0.0075 + 11.3, wp.y * 0.006, wp.z * 0.0075 - 29.7));
        vec3 warmGoldShift = mix(vec3(0.92, 0.86, 0.82), vec3(1.12, 1.04, 0.90), mineralPatch);
        diffuseColor.rgb *= mix(vec3(1.0), warmGoldShift, cliffFactor * 0.65);

        // Freshly spalled lower/mid rock alcoves expose brighter golden-salmon sandstone
        float spallPatch = smoothstep(0.54, 0.78, noise3D(vec3(wp.x * 0.021 - 9.2, wp.y * 0.024, wp.z * 0.021 + 37.4)));
        float freshSpall = spallPatch * (1.0 - upperWallMask * 0.7) * cliffFactor;
        diffuseColor.rgb = mix(diffuseColor.rgb, diffuseColor.rgb * vec3(1.18, 1.12, 0.96), freshSpall * 0.45);

        // Warm dark-umber crevice AO + non-periodic tectonic fissures + desert varnish + fault scar
        vec3 creviceUmber = diffuseColor.rgb * vec3(0.40, 0.30, 0.26);
        float creviceWeight = clamp(
          (contactSeam * 0.38 + vertFissure * 0.42 + varnishStreak + faultScar * 0.22) * cliffFactor,
          0.0,
          0.68
        );
        diffuseColor.rgb = mix(diffuseColor.rgb, creviceUmber, creviceWeight);

        float microGrain = (noise3D(wp * 0.65) - 0.5) * 0.045;
        diffuseColor.rgb = clamp(diffuseColor.rgb * (1.0 + bandShade * cliffFactor + microGrain), 0.02, 1.0);`
      );

      shader.fragmentShader = shader.fragmentShader.replace(
        '#include <normal_fragment_maps>',
        `#include <normal_fragment_maps>
        vec2 horizPush = normalize(wn.xz + vec2(1e-4)) * (bandShade * 0.75 * cliffFactor);
        normal = normalize(normal + vec3(horizPush.x, bandNormalY * cliffFactor, horizPush.y));`
      );
    };

    const mesh = new THREE.Mesh(geometry, material);
    mesh.castShadow = true;
    mesh.receiveShadow = true;
    scene.add(mesh);
    terrainMeshRef.current = mesh;
  }, [meshData, volume, wireframe]);

  return (
    <div
      ref={mountRef}
      className="w-full h-full relative overflow-hidden bg-[#7c7d80]"
    />
  );
};

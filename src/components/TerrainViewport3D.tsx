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

        // 1. 3D Dipping Tectonic Fault Plane + Regional Dip + 3D Fold
        float faultWarp = (noise3D(vec3(wp.x * 0.0082, 0.3, wp.z * 0.0082)) - 0.5) * 42.0;
        float faultCoord = wp.x * uFaultCos + wp.z * uFaultSin + (wp.y - 95.0) * uFaultDip + faultWarp;
        float faultTanh = tanh(faultCoord * 0.095);
        float faultStep = faultTanh * uFaultOffset;
        float faultScar = max(0.0, 1.0 - faultTanh * faultTanh);

        float dip = wp.x * 0.028 - wp.z * 0.022;
        float fold3 = (noise3D(wp * 0.0085) - 0.5) * 10.5;
        float effY = wp.y + dip + faultStep + fold3;

        // 2. 1D Irregular Voronoi / Variable-Width Bed Partition with Lateral Pinch-Out!
        float baseCell = 15.0;
        float cellIdx = floor(effY / baseCell);
        float bestTop = 9999.0;
        float bestBot = -9999.0;
        float activeBedId = cellIdx;

        float bPrev = -9999.0;
        for (int k = -2; k <= 3; k++) {
          float bid = cellIdx + float(k);
          float hSeed = fract(sin(bid * 127.1 + 311.7) * 43758.5453);

          float staticOffset = (hSeed - 0.5) * baseCell * 1.42;
          float latWave = (noise3D(vec3(wp.x * 0.011 + bid * 11.3, 0.2, wp.z * 0.011 - bid * 8.7)) - 0.5) * 17.5;

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

        // Smooth Pinch-Out Seam Taper: when a band pinches out to zero width laterally,
        // its contact seam fades smoothly to zero instead of stair-stepping!
        float pinchTaper = smoothstep(0.8, 4.5, bedWidth);

        float bedType = fract(sin(activeBedId * 419.2 + 93.1) * 31415.926);
        float bedTone = fract(sin(activeBedId * 173.9 + 19.7) * 65432.1);

        float strikeCut = smoothstep(
          0.22 + bedTone * 0.32,
          0.44 + bedTone * 0.32,
          noise3D(vec3(wp.x * 0.013 - activeBedId * 5.7, wp.y * 0.006, wp.z * 0.013 + activeBedId * 9.1))
        ) * pinchTaper;

        // Soft natural bedding contact crevice (no harsh black marker lines!)
        float contactSeam = exp(-min(u, 1.0 - u) * bedWidth * 1.35) * strikeCut;

        float bandShade = 0.0;
        float bandNormalY = 0.0;

        if (bedWidth > 18.5) {
          // WIDE MASSIVE MONOLITH BAND: Vertical rock fracture columns + caprock ledge
          float vertJoint = (noise3D(vec3(wp.x * 0.075, wp.y * 0.014, wp.z * 0.075)) - 0.5) * 0.13;
          float capLedge = smoothstep(0.76, 0.96, u) * 0.11;
          bandShade = (capLedge + vertJoint) * strikeCut;
          bandNormalY = (capLedge * 0.65 - contactSeam * 0.38) * strikeCut;
        } else if (bedType < 0.42) {
          // THIN / MEDIUM RECESSED SHALE SLOT
          float slot = sin(u * 3.14159);
          bandShade = (-slot * 0.15) * strikeCut;
          bandNormalY = cos(u * 3.14159) * 0.24 * strikeCut;
        } else {
          // MIXED / CROSS-BEDDED BAND: Tilted aeolian cross-strata
          float tiltDir = (bedTone > 0.5 ? 1.0 : -1.0);
          float crossPhase = (effY * 1.45 + (wp.x * 0.48 + wp.z * 0.42) * tiltDir);
          float crossLamina = sin(crossPhase) * 0.07;
          bandShade = (crossLamina + (bedTone - 0.5) * 0.11) * strikeCut;
          bandNormalY = (cos(crossPhase) * 0.14 - contactSeam * 0.34) * strikeCut;
        }

        // 3. Vertical Tectonic Joint Fissures, Columnar Fluting & Desert Varnish Streaks
        float varnishNoise = noise3D(vec3(wp.x * 0.065, wp.y * 0.0045, wp.z * 0.065));
        float varnishStreak = smoothstep(0.46, 0.82, varnishNoise) * 0.14 * cliffFactor;

        // Sharp vertical fracture cracks running top-to-bottom along sandstone monoliths
        float jWarp = (noise3D(vec3(wp.x * 0.022, wp.y * 0.012, wp.z * 0.022)) - 0.5) * 8.5;
        float vCrack1 = abs(sin((wp.x * 0.93 - wp.z * 0.35 + jWarp) * 0.14));
        float vCrack2 = abs(sin((wp.x * 0.35 + wp.z * 0.93 - jWarp) * 0.16));
        float vertFissure = max(
          smoothstep(0.11, 0.0, vCrack1),
          smoothstep(0.09, 0.0, vCrack2) * 0.85
        );

        // Warm dark-umber crevice AO + vertical joint fissures + desert varnish + tectonic fault scar
        vec3 creviceUmber = diffuseColor.rgb * vec3(0.46, 0.36, 0.31);
        float creviceWeight = clamp(
          (contactSeam * 0.42 + vertFissure * 0.38 + varnishStreak + faultScar * 0.22) * cliffFactor,
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

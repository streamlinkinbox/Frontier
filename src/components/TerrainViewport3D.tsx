import React, { useEffect, useRef } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { ExtractedSDFMesh } from '../engine/marchingCubes';
import { SDFTerrainVolume, ArchSplineControl } from '../engine/terrainState';
import { GraphNodeData } from '../engine/graphEvaluator';

interface TerrainViewport3DProps {
  meshData: ExtractedSDFMesh | null;
  volume: SDFTerrainVolume | null;
  wireframe?: boolean;
  showRiverWater?: boolean;
  selectedNode?: GraphNodeData | null;
  onUpdateArchSplines?: (nodeId: string, nextSplines: ArchSplineControl[]) => void;
}

export const TerrainViewport3D: React.FC<TerrainViewport3DProps> = ({
  meshData,
  volume,
  wireframe = false,
  selectedNode = null,
  onUpdateArchSplines,
}) => {
  const mountRef = useRef<HTMLDivElement | null>(null);
  const sceneRef = useRef<THREE.Scene | null>(null);
  const cameraRef = useRef<THREE.PerspectiveCamera | null>(null);
  const controlsRef = useRef<OrbitControls | null>(null);
  const terrainMeshRef = useRef<THREE.Mesh | null>(null);
  const splineGroupRef = useRef<THREE.Group | null>(null);
  const rendererRef = useRef<THREE.WebGLRenderer | null>(null);
  const prevArchModeRef = useRef<boolean | null>(null);

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
    camera.position.set(26, 84, 248);
    cameraRef.current = camera;

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
    controls.target.set(-12, 98, -12);
    controls.maxDistance = 25000;
    controls.minDistance = 8;
    controls.update();
    controlsRef.current = controls;

    const splineGroup = new THREE.Group();
    scene.add(splineGroup);
    splineGroupRef.current = splineGroup;

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

    const hemiLight = new THREE.HemisphereLight(0xf3f6fc, 0x543828, 0.90);
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

    // Warm upward slickrock bounce light to illuminate 3D arch undersides (soffits) & alcoves
    const bounceLight = new THREE.DirectionalLight(0xff8e42, 0.64);
    bounceLight.position.set(-30, -240, 160);
    scene.add(bounceLight);

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
        float cliffFactor = clamp((0.82 - wn.y) * 2.1, 0.0, 1.0);

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

        // 4. Sweeping Diagonal Aeolian Dune Cross-Bedding + Fine Sedimentary Micro-Laminae
        float crossSetDir = (bedTone > 0.48 ? 1.0 : -1.0);
        // Irregular, dipping cross-beds: retain sedimentary direction, but warp each set so it
        // reads as broken foresets rather than evenly spaced sine stripes.
        float crossWarp = (noise3D(wp * 0.021) - 0.5) * 10.0;
        float crossPhase = wp.y * 0.47 + (wp.x * 0.24 - wp.z * 0.21) * crossSetDir + crossWarp;
        float crossRough = noise3D(vec3(wp.x * 0.052, wp.y * 0.031, wp.z * 0.052)) - 0.5;
        float crossForeset = (sin(crossPhase) * 0.032 + crossRough * 0.035) * (0.45 + 0.55 * massiveZone);

        // Non-repeating fine lamina/grain relief. The old 2.4m sine period aliased into
        // uniform zebra bands; these warped volumetric fields vary between ~6m and ~15m.
        float microPinch = smoothstep(0.32, 0.68, noise3D(vec3(wp.x * 0.024, wp.y * 0.015, wp.z * 0.024)));
        float grainMid = noise3D(vec3(wp.x * 0.12 + wp.y * 0.025, wp.y * 0.095, wp.z * 0.12 - wp.y * 0.018));
        float grainFine = noise3D(vec3(wp.x * 0.16 - wp.z * 0.025, wp.y * 0.13, wp.z * 0.16 + wp.x * 0.018));
        float microLamina = ((grainMid - 0.5) * 0.105 + (grainFine - 0.5) * 0.045) * microPinch;
        float microNormalY = ((grainMid - 0.5) * 0.22 + (grainFine - 0.5) * 0.10) * microPinch;

        float bandShade = 0.0;
        float bandNormalY = 0.0;

        if (bedWidth > 20.0 || massiveZone > 0.55) {
          // WIDE MASSIVE SANDSTONE FACE: Sweeping diagonal cross-bedding + crisp vertical rock facets
          float rockFacet = (noise3D(vec3(wp.x * 0.048, 0.2, wp.z * 0.048)) - 0.5) * 0.12;
          float capLedge = smoothstep(0.78, 0.96, u) * 0.10 * strikeCut;
          bandShade = capLedge + rockFacet + crossForeset + microLamina * 0.65;
          bandNormalY = (capLedge * 0.58 - contactSeam * 0.34) + cos(crossPhase) * 0.08 * massiveZone + microNormalY * 0.7;
        } else if (bedType < 0.36) {
          // LOCALIZED RECESSED SHALE PARTING / ALCOVE SLOT
          float slot = sin(u * 3.14159);
          bandShade = (-slot * 0.14) * strikeCut + crossForeset * 0.5 + microLamina;
          bandNormalY = cos(u * 3.14159) * 0.24 * strikeCut + microNormalY;
        } else {
          // INTERBEDDED SANDSTONE & CROSS-STRATA LEDGE
          bandShade = (crossForeset * 1.2 + (bedTone - 0.5) * 0.09) * (0.35 + 0.65 * strikeCut) + microLamina;
          bandNormalY = (cos(crossPhase) * 0.12 - contactSeam * 0.28) * strikeCut + microNormalY;
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

        // Warm golden-terracotta slickrock bounce glow on downward-facing 3D arch vaults (soffits)
        float archSoffitBounce = clamp(-wn.y, 0.0, 1.0) * smoothstep(38.0, 74.0, wp.y);
        diffuseColor.rgb *= mix(vec3(1.0), vec3(1.24, 1.06, 0.88), archSoffitBounce * 0.55);

        // Broad mineral mottling plus voxel-safe grain; avoid a single tiny speckle frequency.
        float mineralGrain = (noise3D(wp * 0.055) - 0.5) * 0.095;
        float chippedGrain = (noise3D(wp * 0.14) - 0.5) * 0.075;
        float microGrain = mineralGrain + chippedGrain;
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

    // Frame camera through the Double Arch windows when switching to a 3D Natural Arches preset,
    // or restore overview angle when switching to standard terrain/monoliths
    const hasArches = Boolean(volume?.has3DArches);
    if (prevArchModeRef.current !== hasArches && cameraRef.current && controlsRef.current) {
      prevArchModeRef.current = hasArches;
      if (hasArches) {
        cameraRef.current.position.set(26, 84, 248);
        controlsRef.current.target.set(-12, 98, -12);
      } else {
        cameraRef.current.position.set(410, 265, 430);
        controlsRef.current.target.set(0, 48, 0);
      }
      controlsRef.current.update();
    }
  }, [meshData, volume, wireframe]);

  // 3D Arch Spline Curves & Interactive Draggable Control Points (visible when SDFNaturalArches node is selected)
  useEffect(() => {
    const group = splineGroupRef.current;
    const container = mountRef.current;
    const camera = cameraRef.current;
    const controls = controlsRef.current;
    if (!group || !container || !camera || !controls) return;

    while (group.children.length > 0) {
      const child = group.children[0];
      group.remove(child);
    }

    const isArchNodeSelected =
      selectedNode?.type === 'SDFNaturalArches' &&
      selectedNode?.params?.showSplineGuides !== false;

    const splines: ArchSplineControl[] =
      (isArchNodeSelected ? selectedNode?.params?.archSplines : null) ||
      volume?.archSplines ||
      [];

    if (!isArchNodeSelected || splines.length === 0) {
      return;
    }

    const handleMeshes: THREE.Mesh[] = [];

    splines.forEach((ctrl, sIdx) => {
      if (ctrl.enabled === false) return;

      const steps = 40;
      const extradosPts: THREE.Vector3[] = [];
      const intradosPts: THREE.Vector3[] = [];
      const uc = Math.max(0.22, Math.min(0.78, ctrl.crownPosU));
      const winHalf = Math.max(0.14, Math.min(0.44, ctrl.windowWidthFrac * 0.5));
      const minBridge = Math.max(15, ctrl.bridgeThickness);

      const evalXZ = (u: number) => {
        const om = 1.0 - u;
        return {
          wx: om * om * ctrl.x0 + 2.0 * om * u * ctrl.xc + u * u * ctrl.x1,
          wz: om * om * ctrl.z0 + 2.0 * om * u * ctrl.zc + u * u * ctrl.z1,
        };
      };

      const evalTopY = (u: number) => {
        let bell = 0;
        if (u <= uc) {
          bell = 1.0 - Math.pow(1.0 - u / uc, 1.85);
        } else {
          bell = 1.0 - Math.pow((u - uc) / (1.0 - uc), 1.85);
        }
        const pierLerp = ctrl.pierHeight0 * (1.0 - u) + ctrl.pierHeight1 * u;
        return pierLerp + (ctrl.crownHeight - pierLerp) * bell;
      };

      for (let i = 0; i <= steps; i++) {
        const u = i / steps;
        const { wx, wz } = evalXZ(u);
        const yTop = evalTopY(u);
        extradosPts.push(new THREE.Vector3(wx, yTop + 2.5, wz));

        const deltaU = (u - ctrl.windowCenterU) / winHalf;
        if (Math.abs(deltaU) <= 1.0) {
          const vaultPow = Math.max(1.25, Math.min(3.0, ctrl.vaultPower ?? 1.95));
          const ySill = ctrl.sillHeight + 11.0 * Math.pow(Math.min(1.1, Math.abs(deltaU)), 1.6);
          const vProf = Math.pow(Math.max(0, 1.0 - Math.pow(Math.abs(deltaU), vaultPow)), 0.78);
          const rawApex = Math.min(yTop - minBridge, ctrl.windowApexHeight);
          const yVault = Math.min(yTop - minBridge, ySill + Math.max(10, rawApex - ySill) * vProf);
          intradosPts.push(new THREE.Vector3(wx, yVault, wz));
        }
      }

      const extGeo = new THREE.BufferGeometry().setFromPoints(extradosPts);
      const extMat = new THREE.LineBasicMaterial({
        color: sIdx === 0 ? 0xfbbf24 : 0x34d399,
        depthTest: false,
      });
      const extLine = new THREE.Line(extGeo, extMat);
      extLine.renderOrder = 10;
      group.add(extLine);

      if (intradosPts.length > 2) {
        const intGeo = new THREE.BufferGeometry().setFromPoints(intradosPts);
        const intMat = new THREE.LineBasicMaterial({
          color: 0x38bdf8,
          depthTest: false,
        });
        const intLine = new THREE.Line(intGeo, intMat);
        intLine.renderOrder = 10;
        group.add(intLine);
      }

      // Add Draggable 3D Control Spheres for Yellow Arch (pier0, crown, pier1) AND Blue Arch (vaultL, vault, vaultR)
      const addHandle = (
        pos: THREE.Vector3,
        color: number,
        handleRole: 'crown' | 'vault' | 'vaultL' | 'vaultR' | 'pier0' | 'pier1',
        radius: number = 5.0
      ) => {
        const sphereGeo = new THREE.SphereGeometry(radius, 16, 16);
        const sphereMat = new THREE.MeshBasicMaterial({
          color,
          depthTest: false,
        });
        const m = new THREE.Mesh(sphereGeo, sphereMat);
        m.position.copy(pos);
        m.renderOrder = 12;
        m.userData = { splineIdx: sIdx, handleRole };
        group.add(m);
        handleMeshes.push(m);
      };

      // Yellow Arch Handles (Left Pier, Crown Apex, Right Pier)
      const crownXZ = evalXZ(ctrl.crownPosU);
      addHandle(
        new THREE.Vector3(crownXZ.wx, ctrl.crownHeight + 2.5, crownXZ.wz),
        0xfbbf24,
        'crown',
        5.4
      );
      addHandle(
        new THREE.Vector3(ctrl.x0, ctrl.pierHeight0, ctrl.z0),
        0xf59e0b,
        'pier0',
        5.0
      );
      addHandle(
        new THREE.Vector3(ctrl.x1, ctrl.pierHeight1, ctrl.z1),
        0xf59e0b,
        'pier1',
        5.0
      );

      // Blue Arch Handles (Left Foot, Vault Peak, Right Foot)
      const uLeftFoot = Math.max(0.08, ctrl.windowCenterU - winHalf);
      const uRightFoot = Math.min(0.92, ctrl.windowCenterU + winHalf);
      const vaultLeftXZ = evalXZ(uLeftFoot);
      const vaultRightXZ = evalXZ(uRightFoot);
      const vaultPeakXZ = evalXZ(ctrl.windowCenterU);

      addHandle(
        new THREE.Vector3(vaultPeakXZ.wx, ctrl.windowApexHeight, vaultPeakXZ.wz),
        0x38bdf8,
        'vault',
        5.4
      );
      addHandle(
        new THREE.Vector3(vaultLeftXZ.wx, ctrl.sillHeight + 11.0, vaultLeftXZ.wz),
        0x0ea5e9,
        'vaultL',
        4.6
      );
      addHandle(
        new THREE.Vector3(vaultRightXZ.wx, ctrl.sillHeight + 11.0, vaultRightXZ.wz),
        0x0ea5e9,
        'vaultR',
        4.6
      );
    });

    // Raycast dragging for 3D Arch Spline Control Spheres
    const raycaster = new THREE.Raycaster();
    const pointer = new THREE.Vector2();
    const dragPlane = new THREE.Plane();
    const intersectPt = new THREE.Vector3();
    let activeHandle: THREE.Mesh | null = null;

    const getPointerNDC = (e: PointerEvent) => {
      const rect = container.getBoundingClientRect();
      pointer.x = ((e.clientX - rect.left) / Math.max(1, rect.width)) * 2 - 1;
      pointer.y = -((e.clientY - rect.top) / Math.max(1, rect.height)) * 2 + 1;
    };

    const onPointerDown = (e: PointerEvent) => {
      if (!onUpdateArchSplines || !selectedNode) return;
      getPointerNDC(e);
      raycaster.setFromCamera(pointer, camera);
      const hits = raycaster.intersectObjects(handleMeshes, false);
      if (hits.length > 0) {
        activeHandle = hits[0].object as THREE.Mesh;
        controls.enabled = false;
        const camDir = new THREE.Vector3();
        camera.getWorldDirection(camDir);
        dragPlane.setFromNormalAndCoplanarPoint(camDir, activeHandle.position);
      }
    };

    const onPointerMove = (e: PointerEvent) => {
      if (!activeHandle) return;
      getPointerNDC(e);
      raycaster.setFromCamera(pointer, camera);
      if (raycaster.ray.intersectPlane(dragPlane, intersectPt)) {
        activeHandle.position.y = Math.max(28, Math.min(228, intersectPt.y));
        activeHandle.position.x = Math.max(-210, Math.min(210, intersectPt.x));
        activeHandle.position.z = Math.max(-210, Math.min(210, intersectPt.z));
      }
    };

    const onPointerUp = () => {
      if (!activeHandle || !onUpdateArchSplines || !selectedNode) return;
      const { splineIdx, handleRole } = activeHandle.userData;
      const newY = Math.round(activeHandle.position.y);
      const newX = Math.round(activeHandle.position.x);
      const newZ = Math.round(activeHandle.position.z);

      const nextSplines = splines.map((s, idx) => {
        if (idx !== splineIdx) return s;

        // Project dragged (newX, newZ) onto chord (x0,z0)->(x1,z1) to get updated normalized u
        const abx = s.x1 - s.x0;
        const abz = s.z1 - s.z0;
        const lenSq = Math.max(1.0, abx * abx + abz * abz);
        const projU = ((newX - s.x0) * abx + (newZ - s.z0) * abz) / lenSq;

        if (handleRole === 'crown') {
          const nextCrown = Math.max(s.sillHeight + 42, Math.min(225, newY));
          return {
            ...s,
            crownPosU: Number(Math.max(0.22, Math.min(0.78, projU)).toFixed(2)),
            crownHeight: nextCrown,
            windowApexHeight: Math.min(
              s.windowApexHeight,
              nextCrown - Math.max(15, s.bridgeThickness)
            ),
          };
        }
        if (handleRole === 'vault') {
          const maxV = s.crownHeight - Math.max(15, s.bridgeThickness);
          return {
            ...s,
            windowCenterU: Number(Math.max(0.24, Math.min(0.76, projU)).toFixed(2)),
            windowApexHeight: Math.max(s.sillHeight + 20, Math.min(maxV, newY)),
          };
        }
        if (handleRole === 'vaultL') {
          const uRight = Math.min(0.90, s.windowCenterU + s.windowWidthFrac * 0.5);
          const uLeft = Math.max(0.06, Math.min(uRight - 0.24, projU));
          const nextWidth = Math.max(0.26, Math.min(0.84, uRight - uLeft));
          const nextCenter = 0.5 * (uLeft + uRight);
          return {
            ...s,
            windowWidthFrac: Number(nextWidth.toFixed(2)),
            windowCenterU: Number(nextCenter.toFixed(2)),
            sillHeight: Math.max(24, Math.min(s.windowApexHeight - 22, newY - 11)),
          };
        }
        if (handleRole === 'vaultR') {
          const uLeft = Math.max(0.10, s.windowCenterU - s.windowWidthFrac * 0.5);
          const uRight = Math.min(0.94, Math.max(uLeft + 0.24, projU));
          const nextWidth = Math.max(0.26, Math.min(0.84, uRight - uLeft));
          const nextCenter = 0.5 * (uLeft + uRight);
          return {
            ...s,
            windowWidthFrac: Number(nextWidth.toFixed(2)),
            windowCenterU: Number(nextCenter.toFixed(2)),
            sillHeight: Math.max(24, Math.min(s.windowApexHeight - 22, newY - 11)),
          };
        }
        if (handleRole === 'pier0') {
          return { ...s, x0: newX, z0: newZ, pierHeight0: Math.max(55, Math.min(210, newY)) };
        }
        if (handleRole === 'pier1') {
          return { ...s, x1: newX, z1: newZ, pierHeight1: Math.max(55, Math.min(210, newY)) };
        }
        return s;
      });

      activeHandle = null;
      controls.enabled = true;
      onUpdateArchSplines(selectedNode.id, nextSplines);
    };

    const domElem = rendererRef.current?.domElement;
    if (domElem) {
      domElem.addEventListener('pointerdown', onPointerDown);
      window.addEventListener('pointermove', onPointerMove);
      window.addEventListener('pointerup', onPointerUp);
    }

    return () => {
      if (domElem) {
        domElem.removeEventListener('pointerdown', onPointerDown);
      }
      window.removeEventListener('pointermove', onPointerMove);
      window.removeEventListener('pointerup', onPointerUp);
      controls.enabled = true;
    };
  }, [selectedNode, volume, onUpdateArchSplines]);

  return (
    <div
      ref={mountRef}
      className="w-full h-full relative overflow-hidden bg-[#7c7d80]"
    />
  );
};

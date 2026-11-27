import * as THREE from 'three';
import { clamp, fbm2D, lerp, mulberry32, ridge2D, smoothstep } from '../math/noise';
import { ProceduralTextureLibrary } from '../graphics/proceduralTextures';
import { WorldObstacle } from './trailCorridor';

export interface FractureBranch {
  points: THREE.Vector2[]; // (x, z) on the ice sheet
  segLengths: number[];
  totalLength: number;
  spawnAtMainDist: number;
  revealedLength: number;
  speedScale: number;
}

export interface TravelingIceFracture {
  id: number;
  origin: THREE.Vector2;
  mainBranch: FractureBranch;
  offshoots: FractureBranch[];
  triggered: boolean;
  triggerRadius: number;
  currentSpeedMps: number;
  targetSpeedMps: number;
  speedChangeTimer: number;
  rngState: number;
  openWidthScale: number;
  playerSpawned: boolean;
  tipGlowMesh: THREE.Mesh;
  lastTipShardDist: number;
}

export interface ProceduralIceSlab {
  id: number;
  gx: number; // -6 .. +6 across the 72m lake
  gz: number; // global Voronoi row index (chunkIndex * 16 + localRow)
  baseCenterX: number;
  baseCenterZ: number;
  polyLocal: Array<{ x: number; z: number }>; // CCW in XZ (signedAreaXZ < 0) relative to baseCenter
  vertStart: number;
  vertCount: number;
  restLocalPos: Float32Array; // [lx, ly, lz] * vertCount
  restNormals: Float32Array; // [nx, ny, nz] * vertCount

  // Live 3D Cracking & Floe Physics State!
  crackStress: number; // 0..1 -> drives shader hairline micro-cracks & frosted white rim!
  targetCrackAmount: number; // 0 = intact seamless sheet, 1 = physically severed 3D floe!
  crackAmount: number;
  pushDirX: number;
  pushDirZ: number;
  maxPushDist: number; // 0.35..0.88m lateral chasm opening!
  shrinkMax: number; // 0.05..0.14 polygonal secondary crack gap!
  targetHeaveY: number; // +0.10..+0.26m pressure-ridge lip OR -0.18..-0.05m sunken floe!
  targetPitchX: number; // 3D pitch tilt (rad)
  targetRollZ: number; // 3D roll tilt (rad)
  bobPhase: number;

  // Current animated world transform used by both GPU mesh & 4x4 wheel physics!
  curDispX: number;
  curDispZ: number;
  curY: number;
  curPitchX: number;
  curRollZ: number;
  curScaleXZ: number;
  gpuDirty: boolean;
}

export interface PopUpIceShard {
  active: boolean;
  x: number;
  y: number;
  z: number;
  rotX: number;
  rotY: number;
  rotZ: number;
  targetScaleXZ: number;
  targetScaleY: number;
  growProgress: number;
}

export interface FallingCaveIcicle {
  id: number;
  kind: 'POINTY_ICICLE' | 'ICE_BLOCK';
  ceilPos: THREE.Vector3;
  floorY: number;
  currentY: number;
  vy: number;
  lengthY: number;
  radiusXZ: number;
  shudderTimer: number;
  shudderDuration: number;
  triggerDist: number;
  state: 'HANGING' | 'SHUDDERING' | 'FALLING' | 'IMPALED';
  mesh: THREE.Mesh;
  shadowWarnRing: THREE.Mesh;
  obstacle: WorldObstacle;
  spawnedImpactFracture: boolean;
}

export interface GlacialIceChunkData {
  chunkIndex: number;
  zStart: number;
  isCaveChunk: boolean;
  rowCourseCenterX: Float32Array;
  slabs: ProceduralIceSlab[];
  slabGrid: Array<ProceduralIceSlab | null>; // Fast O(1) lookup: (localRow * SLAB_COLS + colIdx)
  iceSlabsMesh: THREE.Mesh;
  iceSlabsGeo: THREE.BufferGeometry;
  shardInstancedMesh: THREE.InstancedMesh;
  shards: PopUpIceShard[];
  nextShardSlot: number;
  fractures: TravelingIceFracture[];
  fallingIcicles: FallingCaveIcicle[];
}

const CAVE_RADIUS_X = 38.5;
const CAVE_RADIUS_Y = 32.0;

// World-seamless 2D Voronoi grid constants (16 rows * 7.0m === 112.0m CHUNK_LENGTH!)
const VORONOI_ROWS_PER_CHUNK = 16;
const VORONOI_DZ = 7.0; // 16 * 7.0 = 112.0m exact chunk match!
const VORONOI_HALF_COLS = 6; // gx = -6 .. +6 -> 13 columns * 5.8m = 75.4m span!
const SLAB_COLS = VORONOI_HALF_COLS * 2 + 1; // 13
const VORONOI_DX = 5.8;
const SLAB_THICKNESS = 0.88; // Thick 88cm 3D crystalline glacier ice slabs!
const SLAB_BEVEL_DROP = 0.048;
const SUBGLACIAL_WATER_Y = -0.58; // Dark freezing arctic water visible inside every cracked 3D chasm!
const MAX_POPUP_SHARDS = 96;

export class GlacialIceBuilder {
  public readonly proceduralIceSlabMat: THREE.MeshStandardMaterial;
  public readonly subGlacialWaterMat: THREE.ShaderMaterial;
  public readonly iceCaveVaultMat: THREE.MeshStandardMaterial;
  public readonly crystalIcicleMat: THREE.MeshStandardMaterial;
  public readonly shudderIcicleMat: THREE.MeshStandardMaterial;
  public readonly glacierBlockMat: THREE.MeshStandardMaterial;
  public readonly popShardMat: THREE.MeshStandardMaterial;
  public readonly tipSparkMat: THREE.MeshBasicMaterial;
  public readonly impactWarnMat: THREE.MeshBasicMaterial;
  public readonly arcticGodrayMat: THREE.ShaderMaterial;

  // Global blowing spindrift snow & ice-crystal mist across the flat ice!
  public readonly atmosphereFxGroup = new THREE.Group();
  private snowPoints!: THREE.Points;
  private snowPositions!: Float32Array;
  private snowVelocities!: Float32Array;
  private readonly snowCount = 1350;

  private readonly pointyIcicleGeo: THREE.BufferGeometry;
  private readonly staticSmallIcicleGeo: THREE.BufferGeometry;
  private readonly popShardGeo: THREE.BufferGeometry;
  private readonly jaggedIceBlockGeos: THREE.BufferGeometry[] = [];
  private readonly warnRingGeo: THREE.BufferGeometry;
  private readonly sparkSphereGeo: THREE.BufferGeometry;
  private readonly subWaterPlaneGeo: THREE.BufferGeometry;
  private readonly timeUniform = { value: 0 };

  private nextFractureId = 1;
  private nextIcicleId = 1;
  private nextSlabId = 1;
  private playerStressTimer = 0.6;
  private crackJoltCooldown = 0;
  private readonly _shardDummy = new THREE.Object3D();

  public onIceFractureBurst?: (worldPos: THREE.Vector3) => void;
  public onIcicleShatterSlam?: (
    worldPos: THREE.Vector3,
    kind: 'POINTY_ICICLE' | 'ICE_BLOCK'
  ) => void;
  public onVehicleCrossOpenCrack?: (worldPos: THREE.Vector3) => void;

  constructor(textures: ProceduralTextureLibrary) {
    // 1. PROCEDURAL 3D ICE-SLAB PBR MATERIAL + 3-LAYER PARALLAX & LIVE CRACK SHADER!
    // Combines real 3D Voronoi slab geometry with shader-driven internal parallax veins,
    // frozen air bubbles, dynamic hairline stress-cracks, and frosted white cleaved 3D rims!
    this.proceduralIceSlabMat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.32, 0.32),
      roughness: 0.11,
      metalness: 0.14,
    });

    this.proceduralIceSlabMat.onBeforeCompile = (shader) => {
      shader.uniforms.uIceTime = this.timeUniform;

      shader.vertexShader = shader.vertexShader
        .replace(
          '#include <common>',
          `#include <common>
          attribute vec3 aSlabProps;
          varying vec3 vIceWorldPos;
          varying vec3 vSlabProps;
          `
        )
        .replace(
          '#include <project_vertex>',
          `vec4 wpIce = modelMatrix * vec4(transformed, 1.0);
          vIceWorldPos = wpIce.xyz;
          vSlabProps = aSlabProps;
          #include <project_vertex>
          `
        );

      shader.fragmentShader = shader.fragmentShader
        .replace(
          '#include <common>',
          `#include <common>
          uniform float uIceTime;
          varying vec3 vIceWorldPos;
          varying vec3 vSlabProps;

          vec2 iceHash2(vec2 p) {
            p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
            return fract(sin(p) * 43758.5453);
          }

          // Sharp Voronoi crystal-grain boundary distance (0.0 right on hairline internal cracks!)
          float iceVoronoiCrack(vec2 x) {
            vec2 n = floor(x);
            vec2 f = fract(x);
            vec2 mg, mr;
            float md = 8.0;
            for (int j = -1; j <= 1; j++) {
              for (int i = -1; i <= 1; i++) {
                vec2 g = vec2(float(i), float(j));
                vec2 o = iceHash2(n + g);
                vec2 r = g + o - f;
                float d = dot(r, r);
                if (d < md) {
                  md = d;
                  mr = r;
                  mg = g;
                }
              }
            }
            md = 8.0;
            for (int j = -1; j <= 1; j++) {
              for (int i = -1; i <= 1; i++) {
                vec2 g = mg + vec2(float(i), float(j));
                vec2 o = iceHash2(n + g);
                vec2 r = g + o - f;
                if (dot(mr - r, mr - r) > 0.0001) {
                  md = min(md, dot(0.5 * (mr + r), normalize(r - mr)));
                }
              }
            }
            return md;
          }
          `
        )
        .replace(
          '#include <color_fragment>',
          `#include <color_fragment>
          float crackStress = clamp(vSlabProps.x, 0.0, 1.0);
          float wallFactor = clamp(vSlabProps.y, 0.0, 1.0);
          float edgeDistNorm = clamp(vSlabProps.z, 0.0, 1.0);

          // 3-Layer Parallax Ray-Offset into the thick translucent glacier ice!
          vec3 viewRay = normalize(vIceWorldPos - cameraPosition);
          vec2 parallaxShift = viewRay.xz / max(abs(viewRay.y), 0.26);

          vec2 uvSurface = vIceWorldPos.xz * 0.24;
          vec2 uvMidDepth = (vIceWorldPos.xz + parallaxShift * 0.28) * 0.38;
          vec2 uvDeepAbyss = (vIceWorldPos.xz + parallaxShift * 0.64) * 0.17;

          float grainSurf = iceVoronoiCrack(uvSurface);
          float grainMid = iceVoronoiCrack(uvMidDepth);
          float grainDeep = iceVoronoiCrack(uvDeepAbyss);

          // Deep abyssal sapphire base -> translucent turquoise mid-ice -> pale frost veins!
          vec3 abyssalBlue = vec3(0.09, 0.30, 0.49);
          vec3 glacierCyan = vec3(0.30, 0.68, 0.88);
          vec3 paleCrystal = vec3(0.74, 0.94, 1.0);
          vec3 crushedFrost = vec3(0.95, 0.99, 1.0);

          float deepVein = smoothstep(0.14, 0.0, grainDeep);
          float midVein = smoothstep(0.075, 0.0, grainMid);
          vec3 iceCol = mix(abyssalBlue, glacierCyan, 0.52 + 0.38 * smoothstep(0.0, 0.35, grainSurf));
          iceCol = mix(iceCol, paleCrystal, deepVein * 0.42 + midVein * 0.34);

          // Parallax trapped frozen air micro-bubbles inside the slab!
          float bubbleField = fract(sin(dot(floor(uvMidDepth * 9.0), vec2(41.3, 289.1))) * 19432.2);
          float bubbleMask = smoothstep(0.94, 0.995, bubbleField) * (1.0 - wallFactor);
          iceCol = mix(iceCol, crushedFrost, bubbleMask * 0.48);

          // LIVE SHADER SPIDERWEB CRACKS driven by crackStress as fractures rip through!
          float fineCrack = iceVoronoiCrack(vIceWorldPos.xz * 0.72);
          float microCrack = iceVoronoiCrack((vIceWorldPos.xz + parallaxShift * 0.10) * 1.65);
          float stressLine1 = smoothstep(0.012 + 0.068 * crackStress, 0.0, fineCrack) * smoothstep(0.04, 0.30, crackStress);
          float stressLine2 = smoothstep(0.045 * crackStress, 0.0, microCrack) * smoothstep(0.25, 0.85, crackStress);
          float shaderCrackMask = clamp(stressLine1 + stressLine2 * 0.75, 0.0, 1.0);
          iceCol = mix(iceCol, crushedFrost, shaderCrackMask * 0.92);

          // Procedural 3D Voronoi Slab Seam & Cleaved Crushed-Ice Rim!
          float subtleNaturalSeam = smoothstep(0.08, 0.0, edgeDistNorm) * 0.16;
          float activeCrackedRim = smoothstep(0.22, 0.0, edgeDistNorm) * smoothstep(0.08, 0.65, crackStress);
          iceCol = mix(iceCol, crushedFrost, clamp(subtleNaturalSeam + activeCrackedRim * 0.92, 0.0, 1.0));

          // Vertical 3D Cleaved Ice-Cliff Wall (exposed when 3D slabs split open!)
          if (wallFactor > 0.35) {
            float colStriation = 0.5 + 0.5 * sin((vIceWorldPos.x * 13.0 + vIceWorldPos.z * 13.0) + grainSurf * 6.0);
            float depthDarken = smoothstep(-0.82, -0.04, vIceWorldPos.y);
            vec3 wallCol = mix(vec3(0.05, 0.22, 0.38), vec3(0.55, 0.90, 1.0), depthDarken * (0.68 + 0.32 * colStriation));
            // Frosted white upper cleaved lip!
            float upperLip = smoothstep(-0.18, -0.02, vIceWorldPos.y);
            wallCol = mix(wallCol, crushedFrost, upperLip * 0.78);
            iceCol = mix(iceCol, wallCol, smoothstep(0.35, 0.85, wallFactor));
          }

          diffuseColor.rgb *= iceCol;
          `
        )
        .replace(
          '#include <roughnessmap_fragment>',
          `#include <roughnessmap_fragment>
          float rCrack = clamp(vSlabProps.x, 0.0, 1.0);
          float rEdge = smoothstep(0.18, 0.0, clamp(vSlabProps.z, 0.0, 1.0)) * rCrack;
          float fCrack = smoothstep(0.06 * rCrack, 0.0, iceVoronoiCrack(vIceWorldPos.xz * 0.72)) * rCrack;
          // Mirror-glossy black-ice (0.075) on intact surface, crunchy frosted crystal (0.58) on cracked rims!
          roughnessFactor = clamp(0.075 + (rEdge + fCrack) * 0.48 + vSlabProps.y * 0.22, 0.06, 0.72);
          `
        )
        .replace(
          '#include <emissivemap_fragment>',
          `#include <emissivemap_fragment>
          float eStress = clamp(vSlabProps.x, 0.0, 1.0);
          float activePulse = sin(eStress * 3.14159) * 0.55;
          float wallSubsurface = vSlabProps.y * smoothstep(-0.85, -0.08, vIceWorldPos.y) * 0.42;
          totalEmissiveRadiance += vec3(0.08, 0.42, 0.64) * (0.22 + activePulse + wallSubsurface);
          `
        );
    };
    this.proceduralIceSlabMat.customProgramCacheKey = () => 'procedural-3d-cracking-ice-v2';

    // 2. SUB-GLACIAL FREEZING DARK ARCTIC WATER (visible down inside every cracked 3D chasm!)
    this.subGlacialWaterMat = new THREE.ShaderMaterial({
      uniforms: { uTime: this.timeUniform },
      vertexShader: `
        varying vec3 vWorldPos;
        void main() {
          vec4 wp = modelMatrix * vec4(position, 1.0);
          vWorldPos = wp.xyz;
          gl_Position = projectionMatrix * viewMatrix * wp;
        }
      `,
      fragmentShader: `
        uniform float uTime;
        varying vec3 vWorldPos;
        void main() {
          float w1 = sin(vWorldPos.x * 1.9 + uTime * 1.7 + cos(vWorldPos.z * 1.4 - uTime * 1.1));
          float w2 = cos(vWorldPos.z * 2.7 - uTime * 2.1 + sin(vWorldPos.x * 2.3));
          float caustic = smoothstep(0.55, 0.98, 0.5 + 0.25 * (w1 + w2));
          vec3 deepAbyss = vec3(0.015, 0.065, 0.125);
          vec3 coldTeal = vec3(0.05, 0.24, 0.36);
          vec3 slushGlint = vec3(0.42, 0.85, 0.98);
          vec3 col = mix(deepAbyss, coldTeal, 0.45 + 0.35 * w1);
          col = mix(col, slushGlint, caustic * 0.42);
          gl_FragColor = vec4(col, 1.0);
        }
      `,
    });

    // 3. Translucent crystalline blue-cyan Glacial Cave Vault
    this.iceCaveVaultMat = new THREE.MeshStandardMaterial({
      color: 0x7fd4f5,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(1.55, 1.55),
      roughness: 0.18,
      metalness: 0.16,
      vertexColors: true,
      emissive: new THREE.Color(0x0f4c6e),
      emissiveIntensity: 0.48,
      side: THREE.DoubleSide,
    });

    // 4. Razor-sharp pointy crystal cave icicles (stalactites)
    this.crystalIcicleMat = new THREE.MeshStandardMaterial({
      color: 0xbff2ff,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(1.1, 1.1),
      roughness: 0.1,
      metalness: 0.22,
      transparent: true,
      opacity: 0.9,
      emissive: new THREE.Color(0x2892bd),
      emissiveIntensity: 0.45,
    });

    this.shudderIcicleMat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      roughness: 0.08,
      metalness: 0.25,
      emissive: new THREE.Color(0x7af0ff),
      emissiveIntensity: 1.1,
    });

    // 5. Heavy calving blue-white glacier ice blocks
    this.glacierBlockMat = new THREE.MeshStandardMaterial({
      color: 0x99e2ff,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(1.4, 1.4),
      roughness: 0.22,
      metalness: 0.15,
      emissive: new THREE.Color(0x165b80),
      emissiveIntensity: 0.38,
    });

    // 6. Jagged 3D Pop-Up Pressure-Ridge Ice Shards along splitting cracks!
    this.popShardMat = new THREE.MeshStandardMaterial({
      color: 0xdff8ff,
      roughness: 0.12,
      metalness: 0.2,
      emissive: new THREE.Color(0x2da6d4),
      emissiveIntensity: 0.52,
    });

    this.tipSparkMat = new THREE.MeshBasicMaterial({
      color: 0xbffaff,
      transparent: true,
      opacity: 0.92,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });

    this.impactWarnMat = new THREE.MeshBasicMaterial({
      color: 0x5ce6ff,
      transparent: true,
      opacity: 0.42,
      side: THREE.DoubleSide,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });

    // 7. Pale-cyan arctic skylight rays inside the Glacial Cave!
    this.arcticGodrayMat = new THREE.ShaderMaterial({
      transparent: true,
      depthWrite: false,
      side: THREE.DoubleSide,
      blending: THREE.AdditiveBlending,
      uniforms: { uTime: this.timeUniform },
      vertexShader: `
        varying vec2 vUv;
        varying vec3 vNormalView;
        varying vec3 vViewDir;
        void main() {
          vUv = uv;
          vec4 mvPos = modelViewMatrix * vec4(position, 1.0);
          vNormalView = normalize(normalMatrix * normal);
          vViewDir = normalize(-mvPos.xyz);
          gl_Position = projectionMatrix * mvPos;
        }
      `,
      fragmentShader: `
        varying vec2 vUv;
        varying vec3 vNormalView;
        varying vec3 vViewDir;
        uniform float uTime;
        void main() {
          float facing = abs(dot(normalize(vNormalView), normalize(vViewDir)));
          float softEdge = pow(facing, 1.6);
          float vFade = smoothstep(0.0, 0.15, vUv.y) * (0.4 + 0.6 * vUv.y);
          float shimmer = 0.72 + 0.28 * sin(vUv.x * 22.0 + uTime * 0.9);
          vec3 col = mix(vec3(0.45, 0.88, 1.0), vec3(0.88, 0.98, 1.0), vUv.y);
          gl_FragColor = vec4(col, softEdge * vFade * shimmer * 0.22);
        }
      `,
    });

    this.pointyIcicleGeo = this.createFlutedPointyIcicleGeometry(7);
    this.staticSmallIcicleGeo = this.createFlutedPointyIcicleGeometry(5);
    this.popShardGeo = this.createUpwardCrystalShardGeometry();

    for (let b = 0; b < 3; b++) {
      this.jaggedIceBlockGeos.push(this.createJaggedIceBlockGeometry(101 + b * 43));
    }

    const ringGeo = new THREE.RingGeometry(0.55, 1.0, 20);
    ringGeo.rotateX(-Math.PI * 0.5);
    this.warnRingGeo = ringGeo;

    this.sparkSphereGeo = new THREE.SphereGeometry(0.48, 8, 8);

    const waterGeo = new THREE.PlaneGeometry(118.0, 112.6, 1, 1);
    waterGeo.rotateX(-Math.PI * 0.5);
    this.subWaterPlaneGeo = waterGeo;

    this.initSpindriftSnow();
  }

  /**
   * Builds a multi-faceted, fluted, razor-sharp pointy cave stalactite icicle!
   * Origin (0,0,0) is at the wide ceiling root; sharp needle point is at (0, -1, 0).
   */
  private createFlutedPointyIcicleGeometry(radialSides: number): THREE.BufferGeometry {
    const heightSegs = 8;
    const geo = new THREE.ConeGeometry(1.0, 1.0, radialSides, heightSegs, false);
    geo.rotateX(Math.PI);
    geo.translate(0, -0.5, 0);

    const pos = geo.attributes.position as THREE.BufferAttribute;
    for (let i = 0; i < pos.count; i++) {
      const x = pos.getX(i);
      const y = pos.getY(i);
      const z = pos.getZ(i);
      const u = clamp(-y, 0, 1);
      if (u > 0.02 && u < 0.96) {
        const ang = Math.atan2(z, x);
        const flute = 1.0 + 0.18 * Math.sin(ang * radialSides + y * 5.0);
        const profile = Math.pow(1.0 - u, 0.22) * flute;
        pos.setX(i, x * profile);
        pos.setZ(i, z * profile);
      }
    }
    geo.computeVertexNormals();
    return geo;
  }

  /**
   * Builds an angular 3D upward-pointing crystal prism shard that erupts along cracking 3D slab lips!
   */
  private createUpwardCrystalShardGeometry(): THREE.BufferGeometry {
    const geo = new THREE.ConeGeometry(0.42, 1.0, 5, 1, false);
    geo.translate(0, 0.36, 0);
    const nonIdx = geo.index ? geo.toNonIndexed() : geo;
    nonIdx.computeVertexNormals();
    return nonIdx;
  }

  private createJaggedIceBlockGeometry(seed: number): THREE.BufferGeometry {
    const rng = mulberry32(seed);
    const geo = new THREE.DodecahedronGeometry(1.0, 0);
    const pos = geo.attributes.position as THREE.BufferAttribute;
    for (let i = 0; i < pos.count; i++) {
      const vx = pos.getX(i);
      const vy = pos.getY(i);
      const vz = pos.getZ(i);
      const s = 0.78 + 0.36 * (fbm2D(vx * 1.9 + seed, vz * 1.9 - vy, 2) * 0.5 + 0.5);
      pos.setXYZ(i, vx * s * 1.15, vy * s * 0.85 + (rng() - 0.5) * 0.08, vz * s * 1.1);
    }
    const nonIndexed = geo.index ? geo.toNonIndexed() : geo;
    nonIndexed.computeVertexNormals();
    return nonIndexed;
  }

  private initSpindriftSnow(): void {
    this.snowPositions = new Float32Array(this.snowCount * 3);
    this.snowVelocities = new Float32Array(this.snowCount * 3);
    const rng = mulberry32(771234);

    for (let i = 0; i < this.snowCount; i++) {
      const ix = i * 3;
      this.snowPositions[ix] = (rng() - 0.5) * 140;
      this.snowPositions[ix + 1] = rng() * 28;
      this.snowPositions[ix + 2] = (rng() - 0.5) * 160;

      this.snowVelocities[ix] = lerp(8.0, 19.0, rng());
      this.snowVelocities[ix + 1] = lerp(-2.4, -0.5, rng());
      this.snowVelocities[ix + 2] = lerp(4.0, 14.0, rng());
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(this.snowPositions, 3));

    const mat = new THREE.PointsMaterial({
      color: 0xe6f8ff,
      size: 0.38,
      transparent: true,
      opacity: 0.65,
      depthWrite: false,
    });

    this.snowPoints = new THREE.Points(geo, mat);
    this.snowPoints.frustumCulled = false;
    this.atmosphereFxGroup.add(this.snowPoints);
  }

  /**
   * Deterministic world-space jittered Voronoi seed (uX, z) so adjacent chunks stitch with ZERO seam!
   */
  private getVoronoiSeed(gx: number, gz: number): { x: number; z: number } {
    const h1 = Math.imul(gx * 374761393 + gz * 668265263 + 0x9e3779b9, 1274126177) >>> 0;
    const h2 = Math.imul(gx * 2246822519 + gz * 3266489917 + 0x85ebca6b, 668265263) >>> 0;
    const jx = (h1 / 0xffffffff - 0.5) * 0.74 * VORONOI_DX;
    const jz = (h2 / 0xffffffff - 0.5) * 0.74 * VORONOI_DZ;
    return {
      x: gx * VORONOI_DX + jx,
      z: -(gz + 0.5) * VORONOI_DZ + jz,
    };
  }

  /**
   * Sutherland-Hodgman half-plane clip: keeps points X satisfying (X - M) . N <= 0
   */
  private clipPolygonHalfPlane(
    poly: Array<{ x: number; z: number }>,
    mx: number,
    mz: number,
    nx: number,
    nz: number
  ): Array<{ x: number; z: number }> {
    const out: Array<{ x: number; z: number }> = [];
    const n = poly.length;
    if (n === 0) return out;

    for (let i = 0; i < n; i++) {
      const a = poly[i];
      const b = poly[(i + 1) % n];
      const da = (a.x - mx) * nx + (a.z - mz) * nz;
      const db = (b.x - mx) * nx + (b.z - mz) * nz;

      if (da <= 0) {
        out.push(a);
        if (db > 0) {
          const t = da / (da - db);
          out.push({ x: a.x + (b.x - a.x) * t, z: a.z + (b.z - a.z) * t });
        }
      } else if (db <= 0) {
        const t = da / (da - db);
        out.push({ x: a.x + (b.x - a.x) * t, z: a.z + (b.z - a.z) * t });
      }
    }
    return out;
  }

  /**
   * Procedurally generates the watertight 3D Voronoi Ice-Slab Sheet for a 112m chunk!
   * Every cell is extruded into a thick (88cm) 3D crystalline ice plate with beveled frost rims
   * and flat-shaded vertical crystal cliff walls that physically separate, heave, and tilt when cracked!
   */
  private buildProcedural3DIceSlabSheet(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    specSampler: (z: number) => { centerX: number; halfWidth: number; baseY: number }
  ): {
    rowCourseCenterX: Float32Array;
    slabs: ProceduralIceSlab[];
    slabGrid: Array<ProceduralIceSlab | null>;
    iceSlabsMesh: THREE.Mesh;
    iceSlabsGeo: THREE.BufferGeometry;
  } {
    const rowCourseCenterX = new Float32Array(VORONOI_ROWS_PER_CHUNK);
    const slabs: ProceduralIceSlab[] = [];
    const slabGrid: Array<ProceduralIceSlab | null> = new Array(
      VORONOI_ROWS_PER_CHUNK * SLAB_COLS
    ).fill(null);

    const baseGz = chunkIndex * VORONOI_ROWS_PER_CHUNK;

    // First pass: compute clipped 2D Voronoi polygons and total vertex/index counts
    interface BuiltCell {
      gx: number;
      gz: number;
      localRow: number;
      colIdx: number;
      cx: number;
      cz: number;
      polyLocal: Array<{ x: number; z: number }>;
    }
    const builtCells: BuiltCell[] = [];
    let totalVerts = 0;
    let totalIndices = 0;

    for (let r = 0; r < VORONOI_ROWS_PER_CHUNK; r++) {
      const gz = baseGz + r;
      rowCourseCenterX[r] = specSampler(-(gz + 0.5) * VORONOI_DZ).centerX;
      for (let gx = -VORONOI_HALF_COLS; gx <= VORONOI_HALF_COLS; gx++) {
        const seed = this.getVoronoiSeed(gx, gz);
        const span = 16.0;
        let poly: Array<{ x: number; z: number }> = [
          { x: seed.x - span, z: seed.z - span },
          { x: seed.x - span, z: seed.z + span },
          { x: seed.x + span, z: seed.z + span },
          { x: seed.x + span, z: seed.z - span },
        ];

        for (let dz = -2; dz <= 2; dz++) {
          for (let dx = -2; dx <= 2; dx++) {
            if (dx === 0 && dz === 0) continue;
            const nb = this.getVoronoiSeed(gx + dx, gz + dz);
            const mx = 0.5 * (seed.x + nb.x);
            const mz = 0.5 * (seed.z + nb.z);
            const nx = nb.x - seed.x;
            const nz = nb.z - seed.z;
            poly = this.clipPolygonHalfPlane(poly, mx, mz, nx, nz);
            if (poly.length < 3) break;
          }
          if (poly.length < 3) break;
        }

        if (poly.length < 3) continue;

        // Compute polygon centroid in (uX, z) and shift by course centerX(z)
        let sumX = 0;
        let sumZ = 0;
        for (const p of poly) {
          sumX += p.x;
          sumZ += p.z;
        }
        const uCenterX = sumX / poly.length;
        const centerZ = sumZ / poly.length;
        const centerOffsetAtZ = specSampler(centerZ).centerX;

        // Convert vertices to world space relative to slab world centroid (cx, cz)
        const worldCx = centerOffsetAtZ + uCenterX;
        const polyLocal = poly.map((p) => ({
          x: specSampler(p.z).centerX + p.x - worldCx,
          z: p.z - centerZ,
        }));

        // Enforce CCW winding when viewed from +Y (signedAreaXZ < 0 -> +Y top normal & outward walls!)
        let area2 = 0;
        for (let i = 0; i < polyLocal.length; i++) {
          const a = polyLocal[i];
          const b = polyLocal[(i + 1) % polyLocal.length];
          area2 += a.x * b.z - b.x * a.z;
        }
        if (area2 > 0) {
          polyLocal.reverse();
        }

        const m = polyLocal.length;
        // Vertices: 1 center + m inner-bevel + m outer-rim + 4*m flat-shaded vertical cliff walls
        totalVerts += 1 + 2 * m + 4 * m;
        // Triangles: m top cap + 2*m bevel ring + 2*m vertical walls = 5*m triangles (15*m indices)
        totalIndices += 15 * m;

        builtCells.push({
          gx,
          gz,
          localRow: r,
          colIdx: gx + VORONOI_HALF_COLS,
          cx: worldCx,
          cz: centerZ,
          polyLocal,
        });
      }
    }

    const positions = new Float32Array(totalVerts * 3);
    const normals = new Float32Array(totalVerts * 3);
    const uvs = new Float32Array(totalVerts * 2);
    const slabProps = new Float32Array(totalVerts * 3);
    const indices = new Uint32Array(totalIndices);

    let vCursor = 0;
    let iCursor = 0;

    for (const cell of builtCells) {
      const m = cell.polyLocal.length;
      const vertStart = vCursor;
      const vertCount = 1 + 2 * m + 4 * m;
      const restLocalPos = new Float32Array(vertCount * 3);
      const restNormals = new Float32Array(vertCount * 3);

      const writeVert = (
        localIdx: number,
        lx: number,
        ly: number,
        lz: number,
        nx: number,
        ny: number,
        nz: number,
        wallFactor: number,
        edgeDistNorm: number
      ) => {
        const l3 = localIdx * 3;
        restLocalPos[l3] = lx;
        restLocalPos[l3 + 1] = ly;
        restLocalPos[l3 + 2] = lz;
        restNormals[l3] = nx;
        restNormals[l3 + 1] = ny;
        restNormals[l3 + 2] = nz;

        const gIdx = vertStart + localIdx;
        const g3 = gIdx * 3;
        const g2 = gIdx * 2;
        positions[g3] = cell.cx + lx;
        positions[g3 + 1] = ly;
        positions[g3 + 2] = cell.cz + lz;
        normals[g3] = nx;
        normals[g3 + 1] = ny;
        normals[g3 + 2] = nz;
        uvs[g2] = (cell.cx + lx) * 0.16;
        uvs[g2 + 1] = (cell.cz + lz) * 0.16;

        slabProps[g3] = 0.0; // crackStress starts at 0 (pristine frozen lake!)
        slabProps[g3 + 1] = wallFactor;
        slabProps[g3 + 2] = edgeDistNorm;
      };

      // 0: Slab Top Centroid
      writeVert(0, 0, 0, 0, 0, 1, 0, 0.0, 1.0);

      // 1 .. m: Inner Bevel Ring (92% toward outer edge, y = 0)
      for (let i = 0; i < m; i++) {
        const p = cell.polyLocal[i];
        writeVert(1 + i, p.x * 0.92, 0, p.z * 0.92, 0, 1, 0, 0.0, 0.12);
      }

      // 1+m .. 2m: Outer Beveled Rim (100% outer edge, y = -SLAB_BEVEL_DROP)
      for (let i = 0; i < m; i++) {
        const p = cell.polyLocal[i];
        const len = Math.hypot(p.x, p.z) || 1;
        const nx = (p.x / len) * 0.38;
        const nz = (p.z / len) * 0.38;
        const ny = 0.925;
        writeVert(1 + m + i, p.x, -SLAB_BEVEL_DROP, p.z, nx, ny, nz, 0.42, 0.0);
      }

      // Top Cap Triangles (center -> inner_i -> inner_{i+1})
      for (let i = 0; i < m; i++) {
        const next = (i + 1) % m;
        indices[iCursor++] = vertStart + 0;
        indices[iCursor++] = vertStart + 1 + i;
        indices[iCursor++] = vertStart + 1 + next;
      }

      // Beveled Frost Lip Triangles
      for (let i = 0; i < m; i++) {
        const next = (i + 1) % m;
        const in0 = vertStart + 1 + i;
        const in1 = vertStart + 1 + next;
        const out0 = vertStart + 1 + m + i;
        const out1 = vertStart + 1 + m + next;
        indices[iCursor++] = in0;
        indices[iCursor++] = out0;
        indices[iCursor++] = out1;
        indices[iCursor++] = in0;
        indices[iCursor++] = out1;
        indices[iCursor++] = in1;
      }

      // Flat-Shaded Vertical 3D Crystal Ice-Cliff Walls (exposed when 3D slabs crack apart!)
      const wallBaseOffset = 1 + 2 * m;
      for (let i = 0; i < m; i++) {
        const next = (i + 1) % m;
        const p0 = cell.polyLocal[i];
        const p1 = cell.polyLocal[next];
        const ex = p1.x - p0.x;
        const ez = p1.z - p0.z;
        const eLen = Math.hypot(ex, ez) || 1;
        // For signedAreaXZ < 0, (0, -1, 0) x (ex, 0, ez) = (-ez, 0, ex) points strictly OUTWARD!
        const wnx = -ez / eLen;
        const wnz = ex / eLen;

        const wIdx = wallBaseOffset + i * 4;
        writeVert(wIdx + 0, p0.x, -SLAB_BEVEL_DROP, p0.z, wnx, 0, wnz, 1.0, 0.0);
        writeVert(wIdx + 1, p0.x, -SLAB_THICKNESS, p0.z, wnx, 0, wnz, 1.0, 0.0);
        writeVert(wIdx + 2, p1.x, -SLAB_THICKNESS, p1.z, wnx, 0, wnz, 1.0, 0.0);
        writeVert(wIdx + 3, p1.x, -SLAB_BEVEL_DROP, p1.z, wnx, 0, wnz, 1.0, 0.0);

        const gW = vertStart + wIdx;
        indices[iCursor++] = gW + 0;
        indices[iCursor++] = gW + 1;
        indices[iCursor++] = gW + 2;
        indices[iCursor++] = gW + 0;
        indices[iCursor++] = gW + 2;
        indices[iCursor++] = gW + 3;
      }

      const hSeed = Math.imul(cell.gx * 73856093 ^ (cell.gz * 19349663), 2654435761) >>> 0;
      const slab: ProceduralIceSlab = {
        id: this.nextSlabId++,
        gx: cell.gx,
        gz: cell.gz,
        baseCenterX: cell.cx,
        baseCenterZ: cell.cz,
        polyLocal: cell.polyLocal,
        vertStart,
        vertCount,
        restLocalPos,
        restNormals,
        crackStress: 0,
        targetCrackAmount: 0,
        crackAmount: 0,
        pushDirX: 0,
        pushDirZ: 0,
        maxPushDist: 0,
        shrinkMax: 0.05 + ((hSeed & 0xff) / 255) * 0.07,
        targetHeaveY: 0,
        targetPitchX: 0,
        targetRollZ: 0,
        bobPhase: ((hSeed >>> 8) & 0xff) * 0.04,
        curDispX: 0,
        curDispZ: 0,
        curY: 0,
        curPitchX: 0,
        curRollZ: 0,
        curScaleXZ: 1.0,
        gpuDirty: false,
      };

      slabs.push(slab);
      slabGrid[cell.localRow * SLAB_COLS + cell.colIdx] = slab;
      vCursor += vertCount;
    }

    const iceSlabsGeo = new THREE.BufferGeometry();
    const posAttr = new THREE.BufferAttribute(positions, 3);
    posAttr.setUsage(THREE.DynamicDrawUsage);
    const normAttr = new THREE.BufferAttribute(normals, 3);
    normAttr.setUsage(THREE.DynamicDrawUsage);
    const propAttr = new THREE.BufferAttribute(slabProps, 3);
    propAttr.setUsage(THREE.DynamicDrawUsage);

    iceSlabsGeo.setAttribute('position', posAttr);
    iceSlabsGeo.setAttribute('normal', normAttr);
    iceSlabsGeo.setAttribute('uv', new THREE.BufferAttribute(uvs, 2));
    iceSlabsGeo.setAttribute('aSlabProps', propAttr);
    iceSlabsGeo.setIndex(new THREE.BufferAttribute(indices, 1));
    iceSlabsGeo.boundingSphere = new THREE.Sphere(
      new THREE.Vector3(specSampler(zStart - 56).centerX, 0, zStart - 56),
      96
    );

    const iceSlabsMesh = new THREE.Mesh(iceSlabsGeo, this.proceduralIceSlabMat);
    iceSlabsMesh.receiveShadow = true;
    iceSlabsMesh.castShadow = true;
    iceSlabsMesh.frustumCulled = false;
    iceSlabsMesh.userData.uniqueGeo = true;
    chunkGroup.add(iceSlabsMesh);

    // Add the Sub-Glacial Freezing Dark Arctic Water Sheet right underneath the 3D ice slabs!
    const subWater = new THREE.Mesh(this.subWaterPlaneGeo, this.subGlacialWaterMat);
    subWater.position.set(
      specSampler(zStart - 56).centerX,
      SUBGLACIAL_WATER_Y,
      zStart - 56
    );
    chunkGroup.add(subWater);

    return { rowCourseCenterX, slabs, slabGrid, iceSlabsMesh, iceSlabsGeo };
  }

  /**
   * Spawns a 3D faceted crystal shard erupting upward along a newly cracked 3D slab seam!
   */
  private spawnPopUpIceShard(
    data: GlacialIceChunkData,
    x: number,
    y: number,
    z: number,
    rngVal: number
  ): void {
    const slot = data.nextShardSlot % MAX_POPUP_SHARDS;
    data.nextShardSlot++;
    const sh = data.shards[slot];
    sh.active = true;
    sh.x = x;
    sh.y = y - 0.12;
    sh.z = z;
    sh.rotX = (rngVal - 0.5) * 0.65;
    sh.rotY = rngVal * Math.PI * 2;
    sh.rotZ = (((rngVal * 17.3) % 1) - 0.5) * 0.65;
    sh.targetScaleXZ = lerp(0.55, 1.25, (rngVal * 7.1) % 1);
    sh.targetScaleY = lerp(0.45, 1.18, (rngVal * 13.7) % 1);
    sh.growProgress = 0.02;
  }

  /**
   * Builds a jagged branching 2D fracture path across the flat ice sheet.
   * As it travels at a randomly increasing & slowing rate, it physically cracks, separates,
   * tilts, and heaves the procedural 3D Voronoi ice slabs along its path!
   */
  private createTravelingFracture(
    chunkGroup: THREE.Group,
    originX: number,
    originZ: number,
    primaryAngle: number,
    targetLength: number,
    triggerRadius: number,
    startTriggered: boolean,
    playerSpawned: boolean,
    rng: () => number
  ): TravelingIceFracture {
    const mainPts: THREE.Vector2[] = [new THREE.Vector2(originX, originZ)];
    const mainLens: number[] = [];
    let curX = originX;
    let curZ = originZ;
    let curAng = primaryAngle;
    let mainTotal = 0;
    const segCount = 18;
    const baseStep = targetLength / segCount;

    for (let i = 0; i < segCount; i++) {
      curAng += (rng() - 0.5) * 0.72;
      const step = baseStep * lerp(0.68, 1.35, rng());
      curX = clamp(curX + Math.cos(curAng) * step, originX - 38.0, originX + 38.0);
      curZ += Math.sin(curAng) * step;
      mainPts.push(new THREE.Vector2(curX, curZ));
      mainLens.push(step);
      mainTotal += step;
    }

    const mainBranch: FractureBranch = {
      points: mainPts,
      segLengths: mainLens,
      totalLength: mainTotal,
      spawnAtMainDist: 0,
      revealedLength: startTriggered ? 2.5 : 0,
      speedScale: 1.0,
    };

    const offshoots: FractureBranch[] = [];
    const offshootCount = 5;
    for (let b = 0; b < offshootCount; b++) {
      const nodeIdx = Math.min(
        mainPts.length - 2,
        Math.max(2, Math.floor(((b + 0.6) / offshootCount) * (mainPts.length - 2)))
      );
      let spawnDist = 0;
      for (let k = 0; k < nodeIdx; k++) spawnDist += mainLens[k];

      const startPt = mainPts[nodeIdx];
      const parentSegDx = mainPts[nodeIdx + 1].x - startPt.x;
      const parentSegDz = mainPts[nodeIdx + 1].y - startPt.y;
      const parentAng = Math.atan2(parentSegDz, parentSegDx);
      const sideSign = b % 2 === 0 ? 1 : -1;
      let bAng = parentAng + sideSign * lerp(0.48, 1.05, rng());

      const bPts: THREE.Vector2[] = [startPt.clone()];
      const bLens: number[] = [];
      let bx = startPt.x;
      let bz = startPt.y;
      let bTotal = 0;
      const bSegs = 6;
      for (let s = 0; s < bSegs; s++) {
        bAng += (rng() - 0.5) * 0.58;
        const bStep = lerp(2.2, 4.8, rng());
        bx = clamp(bx + Math.cos(bAng) * bStep, originX - 38.0, originX + 38.0);
        bz += Math.sin(bAng) * bStep;
        bPts.push(new THREE.Vector2(bx, bz));
        bLens.push(bStep);
        bTotal += bStep;
      }

      offshoots.push({
        points: bPts,
        segLengths: bLens,
        totalLength: bTotal,
        spawnAtMainDist: spawnDist,
        revealedLength: 0,
        speedScale: lerp(0.65, 1.35, rng()),
      });
    }

    const tipGlowMesh = new THREE.Mesh(this.sparkSphereGeo, this.tipSparkMat);
    tipGlowMesh.visible = false;
    chunkGroup.add(tipGlowMesh);

    const initSpeed = rng() < 0.55 ? lerp(32.0, 58.0, rng()) : lerp(3.2, 7.8, rng());

    return {
      id: this.nextFractureId++,
      origin: new THREE.Vector2(originX, originZ),
      mainBranch,
      offshoots,
      triggered: startTriggered,
      triggerRadius,
      currentSpeedMps: initSpeed,
      targetSpeedMps: initSpeed,
      speedChangeTimer: lerp(0.18, 0.55, rng()),
      rngState: Math.floor(rng() * 0xffffffff) >>> 0,
      openWidthScale: lerp(0.85, 1.35, rng()),
      playerSpawned,
      tipGlowMesh,
      lastTipShardDist: 0,
    };
  }

  /**
   * Computes current 2D position & tangent of a FractureBranch at distance `dist` along it.
   */
  private getBranchPointAtDist(
    branch: FractureBranch,
    dist: number
  ): { x: number; z: number; nx: number; nz: number } {
    let rem = clamp(dist, 0, branch.totalLength);
    for (let i = 0; i < branch.segLengths.length; i++) {
      const segLen = branch.segLengths[i];
      const p0 = branch.points[i];
      const p1 = branch.points[i + 1];
      if (rem <= segLen || i === branch.segLengths.length - 1) {
        const t = segLen > 1e-4 ? clamp(rem / segLen, 0, 1) : 0;
        const dx = p1.x - p0.x;
        const dz = p1.y - p0.y;
        const inv = 1.0 / (Math.hypot(dx, dz) || 1);
        return {
          x: p0.x + dx * t,
          z: p0.y + dz * t,
          nx: -dz * inv,
          nz: dx * inv,
        };
      }
      rem -= segLen;
    }
    const last = branch.points[branch.points.length - 1];
    return { x: last.x, z: last.y, nx: 1, nz: 0 };
  }

  /**
   * Applies real 3D physical cracking, lateral chasm separation, buckling, and tilting
   * to all ProceduralIceSlabs near a propagating fracture tip!
   */
  private crackNearby3DIceSlabs(
    data: GlacialIceChunkData,
    tipX: number,
    tipZ: number,
    normalX: number,
    normalZ: number,
    widthScale: number,
    isMainBranch: boolean
  ): void {
    const affectRadius = isMainBranch ? 8.8 : 5.8;
    const crackCoreRadius = isMainBranch ? 5.6 : 3.8;

    // Only inspect the small neighborhood of Voronoi cells around (tipX, tipZ) -> O(1) cost!
    const approxLocalRow = Math.floor((data.zStart - tipZ) / VORONOI_DZ);
    const minR = Math.max(0, approxLocalRow - 2);
    const maxR = Math.min(VORONOI_ROWS_PER_CHUNK - 1, approxLocalRow + 2);

    for (let r = minR; r <= maxR; r++) {
      for (let c = 0; c < SLAB_COLS; c++) {
        const slab = data.slabGrid[r * SLAB_COLS + c];
        if (!slab) continue;

        const dx = slab.baseCenterX - tipX;
        const dz = slab.baseCenterZ - tipZ;
        const dist = Math.hypot(dx, dz);
        if (dist > affectRadius) continue;

        // 1. Pre-Crack Shader Stress Wave races ahead through the translucent 3D ice!
        const stressHere = smoothstep(affectRadius, crackCoreRadius * 0.45, dist);
        if (stressHere > slab.crackStress) {
          slab.crackStress = stressHere;
          slab.gpuDirty = true;
        }

        // 2. Physical 3D Slab Separation, Tilting & Buckling inside crackCoreRadius!
        if (dist < crackCoreRadius) {
          const falloff = smoothstep(crackCoreRadius, 0.4, dist);
          const newTargetCrack = clamp(falloff * (isMainBranch ? 1.0 : 0.72), 0, 1);

          if (newTargetCrack > slab.targetCrackAmount) {
            // Keep outer bank slabs anchored slightly more so the lake edges meet snow cleanly
            const bankDamp = Math.abs(slab.gx) >= VORONOI_HALF_COLS ? 0.25 : 1.0;
            const signedSide = dx * normalX + dz * normalZ >= 0 ? 1 : -1;

            slab.targetCrackAmount = newTargetCrack;
            slab.pushDirX = normalX * signedSide;
            slab.pushDirZ = normalZ * signedSide;
            slab.maxPushDist =
              (isMainBranch ? 0.62 : 0.34) * widthScale * (0.55 + 0.45 * falloff) * bankDamp;

            // Deterministic per-slab 3D heave & tilt: alternating upward pressure-ridge lips & sunken floes!
            const h = ((slab.id * 2654435761) >>> 0) / 0xffffffff;
            const isBuckleUp = h < 0.56;
            slab.targetHeaveY =
              (isBuckleUp ? lerp(0.09, 0.24, h / 0.56) : lerp(-0.17, -0.05, (h - 0.56) / 0.44)) *
              falloff *
              bankDamp;

            // Tilt slab edges dramatically in 3D (2.5°..6.2°) while keeping smooth 4x4 drivability!
            const tiltMag = lerp(0.042, 0.105, falloff) * bankDamp * (isBuckleUp ? 1 : -0.8);
            slab.targetRollZ = -slab.pushDirX * tiltMag;
            slab.targetPitchX = slab.pushDirZ * tiltMag;
            slab.gpuDirty = true;
          }
        }
      }
    }
  }

  /**
   * Craters & shatters the 3D Voronoi ice slabs when a falling cave stalactite or ice block slams down!
   */
  private crater3DSlabsAtImpact(
    data: GlacialIceChunkData,
    ix: number,
    iz: number,
    isBlock: boolean
  ): void {
    const blastRadius = isBlock ? 9.5 : 7.2;
    for (const slab of data.slabs) {
      const dx = slab.baseCenterX - ix;
      const dz = slab.baseCenterZ - iz;
      const dist = Math.hypot(dx, dz);
      if (dist > blastRadius) continue;

      const falloff = smoothstep(blastRadius, 0.8, dist);
      slab.crackStress = Math.max(slab.crackStress, clamp(falloff * 1.25, 0, 1));
      slab.targetCrackAmount = Math.max(slab.targetCrackAmount, falloff);

      const inv = 1.0 / (dist || 1);
      const bankDamp = Math.abs(slab.gx) >= VORONOI_HALF_COLS ? 0.25 : 1.0;
      slab.pushDirX = dx * inv;
      slab.pushDirZ = dz * inv;
      slab.maxPushDist = Math.max(slab.maxPushDist, (isBlock ? 0.78 : 0.54) * falloff * bankDamp);
      slab.targetHeaveY = (dist < 3.4 ? -0.24 : 0.18) * falloff * bankDamp;
      slab.targetRollZ = -slab.pushDirX * 0.095 * falloff * bankDamp;
      slab.targetPitchX = slab.pushDirZ * 0.095 * falloff * bankDamp;
      slab.gpuDirty = true;
    }

    // Ring of erupting 3D crystal shards around the impact crater!
    for (let s = 0; s < 7; s++) {
      const ang = (s / 7) * Math.PI * 2 + ix * 0.3;
      const rad = lerp(1.2, 3.2, (s * 0.37) % 1);
      this.spawnPopUpIceShard(
        data,
        ix + Math.cos(ang) * rad,
        0,
        iz + Math.sin(ang) * rad,
        ((s + 1) * 0.29) % 1
      );
    }
  }

  /**
   * Re-writes a cracked/tilting slab's 3D vertices & normals into the chunk's single BufferGeometry!
   */
  private writeSlabTransformToBuffer(
    slab: ProceduralIceSlab,
    posArr: Float32Array,
    normArr: Float32Array,
    propArr: Float32Array
  ): void {
    const cx = slab.baseCenterX + slab.curDispX;
    const cy = slab.curY;
    const cz = slab.baseCenterZ + slab.curDispZ;
    const scaleXZ = slab.curScaleXZ;

    const sinR = Math.sin(slab.curRollZ);
    const cosR = Math.cos(slab.curRollZ);
    const sinP = Math.sin(slab.curPitchX);
    const cosP = Math.cos(slab.curPitchX);

    const restP = slab.restLocalPos;
    const restN = slab.restNormals;
    const count = slab.vertCount;
    const vStart = slab.vertStart;

    for (let i = 0; i < count; i++) {
      const i3 = i * 3;
      const lx = restP[i3] * scaleXZ;
      const ly = restP[i3 + 1];
      const lz = restP[i3 + 2] * scaleXZ;

      // Rotate around X (pitch) then Z (roll)
      const y1 = ly * cosP - lz * sinP;
      const z1 = ly * sinP + lz * cosP;
      const x2 = lx * cosR - y1 * sinR;
      const y2 = lx * sinR + y1 * cosR;

      const g3 = (vStart + i) * 3;
      posArr[g3] = cx + x2;
      posArr[g3 + 1] = cy + y2;
      posArr[g3 + 2] = cz + z1;

      const nx = restN[i3];
      const ny = restN[i3 + 1];
      const nz = restN[i3 + 2];
      const ny1 = ny * cosP - nz * sinP;
      const nz1 = ny * sinP + nz * cosP;
      const nx2 = nx * cosR - ny1 * sinR;
      const ny2 = nx * sinR + ny1 * cosR;

      normArr[g3] = nx2;
      normArr[g3 + 1] = ny2;
      normArr[g3 + 2] = nz1;

      propArr[g3] = slab.crackStress;
    }
  }

  /**
   * Sampled by TrailCorridorWorld.sampleHeightRaw so the 4x4's suspension & tires
   * physically ride on tilted 3D cracked ice slabs AND splash/jolt across opened 3D chasms!
   */
  public sampleLiveIceSurface(
    data: GlacialIceChunkData,
    x: number,
    z: number
  ): { active: boolean; deltaY: number; waterDepth: number } {
    if (z > data.zStart || z <= data.zStart - 112.0) {
      return { active: false, deltaY: 0, waterDepth: 0 };
    }

    const approxRow = clamp(
      Math.floor((data.zStart - z) / VORONOI_DZ),
      0,
      VORONOI_ROWS_PER_CHUNK - 1
    );
    const relX = x - data.rowCourseCenterX[approxRow];
    if (Math.abs(relX) > 36.5) {
      return { active: false, deltaY: 0, waterDepth: 0 };
    }
    const approxCol = clamp(
      Math.round(relX / VORONOI_DX) + VORONOI_HALF_COLS,
      0,
      SLAB_COLS - 1
    );

    // Check 3x3 neighborhood of shifted 3D Voronoi slabs
    let bestSlab: ProceduralIceSlab | null = null;
    let insideShiftedSlab = false;
    let anyNearbyCracked = false;

    for (let dr = -1; dr <= 1 && !insideShiftedSlab; dr++) {
      const r = approxRow + dr;
      if (r < 0 || r >= VORONOI_ROWS_PER_CHUNK) continue;
      for (let dc = -1; dc <= 1; dc++) {
        const c = approxCol + dc;
        if (c < 0 || c >= SLAB_COLS) continue;
        const slab = data.slabGrid[r * SLAB_COLS + c];
        if (!slab) continue;
        if (slab.crackAmount > 0.15) anyNearbyCracked = true;

        const sx = x - (slab.baseCenterX + slab.curDispX);
        const sz = z - (slab.baseCenterZ + slab.curDispZ);
        const scale = slab.curScaleXZ;
        const poly = slab.polyLocal;
        let inside = true;
        for (let i = 0; i < poly.length; i++) {
          const a = poly[i];
          const b = poly[(i + 1) % poly.length];
          const ax = a.x * scale;
          const az = a.z * scale;
          const bx = b.x * scale;
          const bz = b.z * scale;
          // Since signedAreaXZ < 0, interior points satisfy (b - a) cross_xz (p - a) <= 0
          const cross = (bx - ax) * (sz - az) - (bz - az) * (sx - ax);
          if (cross > 0.02) {
            inside = false;
            break;
          }
        }

        if (inside) {
          bestSlab = slab;
          insideShiftedSlab = true;
          break;
        }
      }
    }

    if (insideShiftedSlab && bestSlab) {
      if (bestSlab.crackAmount < 0.005) {
        return { active: true, deltaY: 0, waterDepth: 0 };
      }
      const lx = x - (bestSlab.baseCenterX + bestSlab.curDispX);
      const lz = z - (bestSlab.baseCenterZ + bestSlab.curDispZ);
      const planeY =
        bestSlab.curY + lx * Math.sin(bestSlab.curRollZ) - lz * Math.sin(bestSlab.curPitchX);
      return { active: true, deltaY: planeY, waterDepth: 0 };
    }

    // If (x, z) is NOT on any shifted slab and nearby slabs cracked apart, the wheel is over an OPEN 3D ICE CHASM!
    if (anyNearbyCracked) {
      return {
        active: true,
        deltaY: -0.34, // Real 3D fissure trench drop that suspension dips & jolts across!
        waterDepth: 0.15, // Kicks up sparkling sub-glacial slush/water rooster-tails!
      };
    }

    return { active: true, deltaY: 0, waterDepth: 0 };
  }

  /**
   * Builds the Towering Translucent Crystalline Glacial Ice Cave + Static & Falling Pointy Stalactites!
   */
  private buildGlacialIceCaveAndIcicles(
    chunkGroup: THREE.Group,
    fallingIcicles: FallingCaveIcicle[],
    obstacles: WorldObstacle[],
    zStart: number,
    chunkLength: number,
    rng: () => number,
    specSampler: (z: number) => { centerX: number; halfWidth: number; baseY: number },
    allocObstacleId: () => number,
    chunkIndex: number
  ): void {
    const skylights = [0.22, 0.54, 0.84].map((u) => ({
      zCenter: zStart - u * chunkLength,
      zHalf: lerp(4.5, 7.2, rng()),
      thetaCenter: lerp(1.25, 1.9, rng()),
      thetaHalf: lerp(0.14, 0.24, rng()),
    }));

    const zSegs = 26;
    const aSegs = 28;
    const thetaMin = -0.05;
    const thetaMax = Math.PI + 0.05;
    const cols = aSegs + 1;
    const rows = zSegs + 1;

    const positions = new Float32Array(cols * rows * 3);
    const colors = new Float32Array(cols * rows * 3);
    const uvs = new Float32Array(cols * rows * 2);
    const indices: number[] = [];

    const colDeepIce = new THREE.Color(0x2878a8);
    const colFrostCyan = new THREE.Color(0x9cecff);
    const colSnowRim = new THREE.Color(0xeafaff);
    const tmpCol = new THREE.Color();

    const inSkylight = (z: number, theta: number): number => {
      let best = 0;
      for (const sk of skylights) {
        const nz = (z - sk.zCenter) / sk.zHalf;
        const nt = (theta - sk.thetaCenter) / sk.thetaHalf;
        const d = nz * nz + nt * nt + Math.sin(z * 0.6 + theta * 8.0) * 0.14;
        if (d < 1.0) best = Math.max(best, 1.0 - d);
      }
      return best;
    };

    let pPtr = 0;
    let uvPtr = 0;
    for (let rz = 0; rz < rows; rz++) {
      const tz = rz / zSegs;
      const z = zStart - tz * chunkLength;
      const sp = specSampler(z);
      const portalFlare =
        1.0 + (smoothstep(0.12, 0.0, tz) + smoothstep(0.88, 1.0, tz)) * 0.12;

      for (let ca = 0; ca < cols; ca++) {
        const ta = ca / aSegs;
        const theta = lerp(thetaMin, thetaMax, ta);

        const facet =
          ridge2D(z * 0.075, theta * 3.8, 2) * 3.4 +
          Math.sin(theta * 16.0 + z * 0.09) * 1.1;
        const rx = (CAVE_RADIUS_X - facet) * portalFlare;
        const ry = (CAVE_RADIUS_Y - facet * 0.85) * portalFlare;

        positions[pPtr] = sp.centerX + Math.cos(theta) * rx;
        positions[pPtr + 1] = sp.baseY - 1.2 + Math.sin(theta) * ry;
        positions[pPtr + 2] = z;

        uvs[uvPtr++] = ta * 6.0;
        uvs[uvPtr++] = z * 0.07;

        const skProx = inSkylight(z, theta);
        tmpCol.copy(colDeepIce).lerp(colFrostCyan, clamp(facet * 0.24, 0, 0.88));
        if (skProx > 0.01) tmpCol.lerp(colSnowRim, clamp(skProx * 1.7, 0, 0.95));

        colors[pPtr] = tmpCol.r;
        colors[pPtr + 1] = tmpCol.g;
        colors[pPtr + 2] = tmpCol.b;
        pPtr += 3;
      }
    }

    for (let rz = 0; rz < zSegs; rz++) {
      const zMid = zStart - ((rz + 0.5) / zSegs) * chunkLength;
      for (let ca = 0; ca < aSegs; ca++) {
        const thetaMid = lerp(thetaMin, thetaMax, (ca + 0.5) / aSegs);
        if (inSkylight(zMid, thetaMid) > 0.24) continue;
        const a = rz * cols + ca;
        const b = a + 1;
        const c = a + cols;
        const d = c + 1;
        indices.push(a, c, b, b, c, d);
      }
    }

    const caveGeo = new THREE.BufferGeometry();
    caveGeo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    caveGeo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
    caveGeo.setAttribute('uv', new THREE.BufferAttribute(uvs, 2));
    caveGeo.setIndex(indices);
    caveGeo.computeVertexNormals();

    const caveMesh = new THREE.Mesh(caveGeo, this.iceCaveVaultMat);
    caveMesh.receiveShadow = true;
    caveMesh.castShadow = true;
    caveMesh.userData.uniqueGeo = true;
    chunkGroup.add(caveMesh);

    // Arctic Godray Beams through cave skylights
    const beamGeo = new THREE.CylinderGeometry(1.4, 5.8, 1.0, 12, 1, true);
    for (const sk of skylights) {
      const sp = specSampler(sk.zCenter);
      const sx = sp.centerX + Math.cos(sk.thetaCenter) * (CAVE_RADIUS_X * 0.55);
      const sy = sp.baseY + Math.sin(sk.thetaCenter) * (CAVE_RADIUS_Y * 0.88);
      const beamLen = sy + 2.0;

      const beam = new THREE.Mesh(beamGeo, this.arcticGodrayMat);
      beam.scale.set(1.35, beamLen, 1.35);
      beam.position.set(sx - 2.5, sy - beamLen * 0.45, sk.zCenter);
      beam.rotation.z = -0.18;
      chunkGroup.add(beam);
    }

    // Dense Static Ceiling Icicle Bristles (~90 sharp stalactites overhead!)
    const staticCount = 90;
    const dummy = new THREE.Object3D();
    const staticMatrices: THREE.Matrix4[] = [];
    for (let i = 0; i < staticCount; i++) {
      const iz = zStart - rng() * chunkLength;
      const theta = lerp(0.32, Math.PI - 0.32, rng());
      if (inSkylight(iz, theta) > 0.15) continue;

      const sp = specSampler(iz);
      const facet = ridge2D(iz * 0.075, theta * 3.8, 2) * 3.2;
      const ix = sp.centerX + Math.cos(theta) * (CAVE_RADIUS_X - facet - 0.5);
      const iy = sp.baseY - 0.5 + Math.sin(theta) * (CAVE_RADIUS_Y - facet * 0.85);

      const radXZ = lerp(0.28, 0.92, rng());
      const lenY = lerp(2.6, 8.2, rng());
      dummy.position.set(ix, iy + 0.35, iz);
      dummy.rotation.set((rng() - 0.5) * 0.08, rng() * Math.PI * 2, (rng() - 0.5) * 0.08);
      dummy.scale.set(radXZ, lenY, radXZ);
      dummy.updateMatrix();
      staticMatrices.push(dummy.matrix.clone());
    }

    if (staticMatrices.length > 0) {
      const im = new THREE.InstancedMesh(
        this.staticSmallIcicleGeo,
        this.crystalIcicleMat,
        staticMatrices.length
      );
      staticMatrices.forEach((m, idx) => im.setMatrixAt(idx, m));
      im.instanceMatrix.needsUpdate = true;
      chunkGroup.add(im);
    }

    // 11 Dynamic Falling Pointy Cave Icicles & Heavy Calving Ice Blocks!
    const fallCount = 11;
    for (let f = 0; f < fallCount; f++) {
      const fz = zStart - ((f + 0.18 + rng() * 0.55) / fallCount) * chunkLength;
      const sp = specSampler(fz);
      const laneSpread = (rng() - 0.5) * 2.0 * (sp.halfWidth * 0.78);
      const fx = sp.centerX + laneSpread;
      const normX = clamp((fx - sp.centerX) / CAVE_RADIUS_X, -0.82, 0.82);
      const theta = Math.acos(normX);
      const facet = ridge2D(fz * 0.075, theta * 3.8, 2) * 3.0;
      const ceilY = sp.baseY - 0.4 + Math.sin(theta) * (CAVE_RADIUS_Y - facet * 0.85);
      const floorY = 0;

      const isPointy = rng() < 0.75;
      const radXZ = isPointy ? lerp(0.68, 1.32, rng()) : lerp(1.35, 2.25, rng());
      const lenY = isPointy ? lerp(7.2, 12.6, rng()) : radXZ * 1.65;

      const geo = isPointy
        ? this.pointyIcicleGeo
        : this.jaggedIceBlockGeos[f % this.jaggedIceBlockGeos.length];
      const mat = isPointy ? this.crystalIcicleMat : this.glacierBlockMat;

      const mesh = new THREE.Mesh(geo, mat);
      mesh.position.set(fx, ceilY, fz);
      if (isPointy) {
        mesh.scale.set(radXZ, lenY, radXZ);
        mesh.rotation.y = rng() * Math.PI * 2;
      } else {
        mesh.scale.set(radXZ, radXZ * 0.9, radXZ);
        mesh.rotation.set(rng() * Math.PI, rng() * Math.PI, rng() * Math.PI);
      }
      mesh.castShadow = true;
      mesh.receiveShadow = true;
      chunkGroup.add(mesh);

      const warnRing = new THREE.Mesh(this.warnRingGeo, this.impactWarnMat);
      warnRing.position.set(fx, floorY + 0.06, fz);
      warnRing.scale.setScalar(radXZ * 1.9);
      warnRing.visible = false;
      chunkGroup.add(warnRing);

      const obs: WorldObstacle = {
        id: allocObstacleId(),
        chunkIndex,
        kind: 'boulder',
        position: new THREE.Vector3(fx, floorY - 80.0, fz),
        radius: isPointy ? radXZ * 0.88 : radXZ * 1.05,
        height: isPointy ? lenY * 0.72 : radXZ * 1.8,
        canShearVine: true,
      };
      obstacles.push(obs);

      fallingIcicles.push({
        id: this.nextIcicleId++,
        kind: isPointy ? 'POINTY_ICICLE' : 'ICE_BLOCK',
        ceilPos: new THREE.Vector3(fx, ceilY, fz),
        floorY,
        currentY: ceilY,
        vy: 0,
        lengthY: lenY,
        radiusXZ: radXZ,
        shudderTimer: 0,
        shudderDuration: lerp(0.28, 0.52, rng()),
        triggerDist: lerp(42.0, 96.0, rng()),
        state: 'HANGING',
        mesh,
        shadowWarnRing: warnRing,
        obstacle: obs,
        spawnedImpactFracture: false,
      });
    }
  }

  /**
   * Populates a 112m Glacial Ice chunk with:
   * 1. Procedurally generated 3D Voronoi Ice-Slab Surface (208 thick 3D plates over dark sub-glacial water!)
   * 2. Alternating Open Frozen Lake Flats & Towering Icicle Caves
   * 3. Multi-area variable-speed traveling fractures that physically split, heave, and tilt the 3D slabs!
   */
  public populateGlacialIceChunk(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    chunkLength: number,
    rng: () => number,
    groundSampler: (x: number, z: number) => number,
    specSampler: (z: number) => { centerX: number; halfWidth: number; baseY: number },
    obstacles: WorldObstacle[],
    allocObstacleId: () => number
  ): GlacialIceChunkData {
    const fractures: TravelingIceFracture[] = [];
    const fallingIcicles: FallingCaveIcicle[] = [];

    const isCaveChunk = chunkIndex >= 1 && chunkIndex % 2 === 1;

    // 1. Build the Procedural 3D Voronoi Ice-Slab Sheet + Sub-Glacial Dark Water!
    const { rowCourseCenterX, slabs, slabGrid, iceSlabsMesh, iceSlabsGeo } =
      this.buildProcedural3DIceSlabSheet(chunkGroup, chunkIndex, zStart, specSampler);

    // 2. Preallocate 96 Instanced 3D Pop-Up Crystal Shards (erupt along cracking 3D slab lips!)
    const shardInstancedMesh = new THREE.InstancedMesh(
      this.popShardGeo,
      this.popShardMat,
      MAX_POPUP_SHARDS
    );
    shardInstancedMesh.frustumCulled = false;
    shardInstancedMesh.castShadow = true;
    const shards: PopUpIceShard[] = [];
    this._shardDummy.scale.set(0, 0, 0);
    this._shardDummy.updateMatrix();
    for (let i = 0; i < MAX_POPUP_SHARDS; i++) {
      shardInstancedMesh.setMatrixAt(i, this._shardDummy.matrix);
      shards.push({
        active: false,
        x: 0,
        y: 0,
        z: 0,
        rotX: 0,
        rotY: 0,
        rotZ: 0,
        targetScaleXZ: 1,
        targetScaleY: 1,
        growProgress: 0,
      });
    }
    shardInstancedMesh.instanceMatrix.needsUpdate = true;
    chunkGroup.add(shardInstancedMesh);

    if (isCaveChunk) {
      this.buildGlacialIceCaveAndIcicles(
        chunkGroup,
        fallingIcicles,
        obstacles,
        zStart,
        chunkLength,
        rng,
        specSampler,
        allocObstacleId,
        chunkIndex
      );
    } else {
      // Perimeter Turquoise Glacier Seracs bordering the open frozen lake
      const dummy = new THREE.Object3D();
      const seracMats: THREE.Matrix4[] = [];
      for (let g = 0; g < 22; g++) {
        const gz = zStart - rng() * chunkLength;
        const sp = specSampler(gz);
        const side = g % 2 === 0 ? -1 : 1;
        const gx = sp.centerX + side * lerp(sp.halfWidth + 8.0, 96.0, rng());
        const gy = groundSampler(gx, gz);
        dummy.position.set(gx, gy + lerp(6.0, 18.0, rng()), gz);
        dummy.rotation.set(Math.PI, rng() * Math.PI * 2, (rng() - 0.5) * 0.28);
        dummy.scale.set(lerp(3.5, 8.5, rng()), lerp(12.0, 32.0, rng()), lerp(3.5, 8.5, rng()));
        dummy.updateMatrix();
        seracMats.push(dummy.matrix.clone());
      }
      if (seracMats.length > 0) {
        const im = new THREE.InstancedMesh(
          this.staticSmallIcicleGeo,
          this.glacierBlockMat,
          seracMats.length
        );
        seracMats.forEach((m, idx) => im.setMatrixAt(idx, m));
        im.instanceMatrix.needsUpdate = true;
        chunkGroup.add(im);
      }
    }

    // 3. Seed Multi-Zone Traveling 3D Ice Fractures across Left, Center, Right & Diagonal lanes!
    if (chunkIndex >= 0) {
      const faultCount = 6;
      for (let f = 0; f < faultCount; f++) {
        // In Chunk 0, place first showcase fracture right in front of the camera (-22m..-85m) so you immediately watch the 3D ice crack open!
        const fz =
          chunkIndex === 0 && f === 0
            ? -21.0
            : chunkIndex === 0 && f === 1
            ? -36.0
            : zStart - ((f + 0.14 + rng() * 0.6) / faultCount) * chunkLength;
        if (Math.abs(fz) < 16) continue;
        const sp = specSampler(fz);

        const laneBias =
          chunkIndex === 0 && f === 0
            ? -0.32
            : chunkIndex === 0 && f === 1
            ? 0.42
            : f % 3 === 0
            ? -0.68
            : f % 3 === 1
            ? 0.68
            : (rng() - 0.5) * 0.42;
        const ox = sp.centerX + laneBias * sp.halfWidth;
        const aimAng = -Math.PI * 0.5 + (-laneBias * 0.82 + (rng() - 0.5) * 0.48);

        fractures.push(
          this.createTravelingFracture(
            chunkGroup,
            ox,
            fz,
            aimAng,
            lerp(48.0, 82.0, rng()),
            lerp(46.0, 86.0, rng()),
            chunkIndex === 0 && (f === 0 || f === 1), // Immediately start cracking 3D slabs right in front of spawn!
            false,
            rng
          )
        );
      }
    }

    return {
      chunkIndex,
      zStart,
      isCaveChunk,
      rowCourseCenterX,
      slabs,
      slabGrid,
      iceSlabsMesh,
      iceSlabsGeo,
      shardInstancedMesh,
      shards,
      nextShardSlot: 0,
      fractures,
      fallingIcicles,
    };
  }

  /**
   * Advances variable-speed traveling fractures, physically cracks/separates/tilts 3D Voronoi ice slabs,
   * pops up 3D jagged crystal shards, spawns live fractures where the player drives, and drops cave icicles!
   */
  public stepChunkHazards(
    chunkGroup: THREE.Group,
    data: GlacialIceChunkData,
    elapsedTime: number,
    dt: number,
    focusPos: THREE.Vector3,
    vehiclePos: THREE.Vector3,
    vehicleVel: THREE.Vector3,
    vehicleSpeedMps: number,
    passiveObserver: boolean,
    groundSampler: (x: number, z: number) => number
  ): void {
    this.timeUniform.value = elapsedTime;
    this.crackJoltCooldown = Math.max(0, this.crackJoltCooldown - dt);

    // A. Dynamic Player-Driven Multi-Area 3D Ice Fractures!
    const zEnd = data.zStart - 112;
    if (
      !passiveObserver &&
      vehiclePos.z <= data.zStart &&
      vehiclePos.z > zEnd &&
      vehicleSpeedMps > 5.5 &&
      data.fractures.length < 12
    ) {
      this.playerStressTimer -= dt;
      if (this.playerStressTimer <= 0) {
        const dynRng = mulberry32(
          (Math.floor(elapsedTime * 1000) ^ (data.chunkIndex * 7919)) >>> 0
        );
        this.playerStressTimer = lerp(1.5, 2.9, dynRng());

        const spawnZ = vehiclePos.z - lerp(3.0, 20.0, dynRng());
        const sRow = clamp(
          Math.floor((data.zStart - spawnZ) / VORONOI_DZ),
          0,
          VORONOI_ROWS_PER_CHUNK - 1
        );
        const cX = data.rowCourseCenterX[sRow];
        const spawnX = clamp(vehiclePos.x + (dynRng() - 0.5) * 16.0, cX - 30.0, cX + 30.0);
        const travelAng = -Math.PI * 0.5 + (dynRng() - 0.5) * 1.35;

        const liveFrac = this.createTravelingFracture(
          chunkGroup,
          spawnX,
          spawnZ,
          travelAng,
          lerp(44.0, 74.0, dynRng()),
          999,
          true,
          true,
          dynRng
        );
        data.fractures.push(liveFrac);
        if (this.onIceFractureBurst) {
          this.onIceFractureBurst(new THREE.Vector3(spawnX, 0.08, spawnZ));
        }
      }
    }

    // B. Step Variable-Speed Fractures (Randomly Accelerating & Slowing!) -> Crack 3D Voronoi Slabs!
    for (const fr of data.fractures) {
      if (!fr.triggered) {
        const dFocus = Math.hypot(focusPos.x - fr.origin.x, focusPos.z - fr.origin.y);
        const dCar = Math.hypot(vehiclePos.x - fr.origin.x, vehiclePos.z - fr.origin.y);
        if (Math.min(dFocus, dCar) < fr.triggerRadius) {
          fr.triggered = true;
          if (this.onIceFractureBurst) {
            this.onIceFractureBurst(new THREE.Vector3(fr.origin.x, 0.08, fr.origin.y));
          }
        }
      }

      if (!fr.triggered) continue;

      const stillGrowing = fr.mainBranch.revealedLength < fr.mainBranch.totalLength;
      let anyOffshootGrowing = false;
      for (const off of fr.offshoots) {
        if (
          fr.mainBranch.revealedLength >= off.spawnAtMainDist &&
          off.revealedLength < off.totalLength
        ) {
          anyOffshootGrowing = true;
        }
      }

      if (stillGrowing || anyOffshootGrowing) {
        // Randomly alternate between rapid crack-surge acceleration (35..66 m/s) and slow groaning creep (2.2..8.5 m/s)!
        fr.speedChangeTimer -= dt;
        if (fr.speedChangeTimer <= 0) {
          fr.rngState = (fr.rngState * 1664525 + 1013904223) >>> 0;
          const r1 = fr.rngState / 0xffffffff;
          fr.rngState = (fr.rngState * 1664525 + 1013904223) >>> 0;
          const r2 = fr.rngState / 0xffffffff;

          const goFast = fr.targetSpeedMps < 18.0 ? r1 < 0.78 : r1 < 0.32;
          fr.targetSpeedMps = goFast ? lerp(35.0, 66.0, r2) : lerp(2.2, 8.5, r2);
          fr.speedChangeTimer = goFast ? lerp(0.16, 0.42, r1) : lerp(0.28, 0.68, r1);
        }

        fr.currentSpeedMps = lerp(fr.currentSpeedMps, fr.targetSpeedMps, clamp(8.5 * dt, 0, 1));
        const microPulse = 0.75 + 0.35 * Math.sin(elapsedTime * 19.0 + fr.id * 3.1);
        const stepDist = fr.currentSpeedMps * microPulse * dt;

        if (stillGrowing) {
          const prevLen = fr.mainBranch.revealedLength;
          fr.mainBranch.revealedLength = Math.min(
            fr.mainBranch.totalLength,
            prevLen + stepDist
          );

          // Sub-sample along stepDist so even 60 m/s surge bursts crack every single 3D slab in their path!
          const subSteps = Math.max(1, Math.ceil((fr.mainBranch.revealedLength - prevLen) / 2.2));
          for (let s = 1; s <= subSteps; s++) {
            const sampleD = lerp(prevLen, fr.mainBranch.revealedLength, s / subSteps);
            const pt = this.getBranchPointAtDist(fr.mainBranch, sampleD);
            this.crackNearby3DIceSlabs(
              data,
              pt.x,
              pt.z,
              pt.nx,
              pt.nz,
              fr.openWidthScale,
              true
            );

            if (sampleD - fr.lastTipShardDist >= 3.4) {
              fr.lastTipShardDist = sampleD;
              fr.rngState = (fr.rngState * 1664525 + 1013904223) >>> 0;
              const rShard = fr.rngState / 0xffffffff;
              this.spawnPopUpIceShard(
                data,
                pt.x + (rShard - 0.5) * 1.2,
                0,
                pt.z + (((rShard * 11.3) % 1) - 0.5) * 1.2,
                rShard
              );
            }
          }

          const tipPt = this.getBranchPointAtDist(fr.mainBranch, fr.mainBranch.revealedLength);
          fr.tipGlowMesh.position.set(tipPt.x, 0.14, tipPt.z);
          fr.tipGlowMesh.visible = true;
          fr.tipGlowMesh.scale.setScalar(clamp(fr.currentSpeedMps / 26.0, 0.48, 2.15));
        } else {
          fr.tipGlowMesh.visible = false;
        }

        for (const off of fr.offshoots) {
          if (
            fr.mainBranch.revealedLength >= off.spawnAtMainDist &&
            off.revealedLength < off.totalLength
          ) {
            const prevOff = off.revealedLength;
            off.revealedLength = Math.min(
              off.totalLength,
              prevOff + stepDist * off.speedScale
            );
            const offPt = this.getBranchPointAtDist(off, off.revealedLength);
            this.crackNearby3DIceSlabs(
              data,
              offPt.x,
              offPt.z,
              offPt.nx,
              offPt.nz,
              fr.openWidthScale * 0.75,
              false
            );
          }
        }
      } else {
        fr.tipGlowMesh.visible = false;
      }

      // Crisp jolt + ice-shatter burst when the 4x4 crosses an opened 3D chasm!
      if (!passiveObserver && this.crackJoltCooldown <= 0 && fr.mainBranch.revealedLength > 6.0) {
        let accum = 0;
        for (let s = 0; s < fr.mainBranch.segLengths.length; s += 2) {
          if (accum + 3.0 > fr.mainBranch.revealedLength) break;
          const pt = fr.mainBranch.points[s];
          const dSq =
            (vehiclePos.x - pt.x) * (vehiclePos.x - pt.x) +
            (vehiclePos.z - pt.y) * (vehiclePos.z - pt.y);
          if (dSq < 2.1 * 2.1 && Math.abs(vehiclePos.y) < 1.9) {
            this.crackJoltCooldown = 0.55;
            vehicleVel.y = Math.max(vehicleVel.y, 2.25);
            vehicleVel.x += (pt.x > vehiclePos.x ? -1 : 1) * 2.2;
            if (this.onVehicleCrossOpenCrack) {
              this.onVehicleCrossOpenCrack(vehiclePos.clone());
            }
            break;
          }
          accum += fr.mainBranch.segLengths[s] + (fr.mainBranch.segLengths[s + 1] || 0);
        }
      }
    }

    // C. Animate 3D Cracking Voronoi Ice Slabs (Separation, Buckling, Tilting & Buoyant Water Bobbing!)
    const posAttr = data.iceSlabsGeo.attributes.position as THREE.BufferAttribute;
    const normAttr = data.iceSlabsGeo.attributes.normal as THREE.BufferAttribute;
    const propAttr = data.iceSlabsGeo.attributes.aSlabProps as THREE.BufferAttribute;
    const posArr = posAttr.array as Float32Array;
    const normArr = normAttr.array as Float32Array;
    const propArr = propAttr.array as Float32Array;
    let anySlabUpdated = false;

    const crackRate = clamp(5.2 * dt, 0, 1);
    for (const slab of data.slabs) {
      if (slab.targetCrackAmount <= 0.001 && !slab.gpuDirty) continue;

      if (Math.abs(slab.targetCrackAmount - slab.crackAmount) > 0.001) {
        slab.crackAmount = lerp(slab.crackAmount, slab.targetCrackAmount, crackRate);
        slab.gpuDirty = true;
      }

      // Buoyant sub-glacial water bobbing once severed when close to the camera/player!
      const distZ = Math.abs(focusPos.z - slab.baseCenterZ);
      const buoyantActive = slab.crackAmount > 0.35 && distZ < 105.0;
      const bobY = buoyantActive
        ? Math.sin(elapsedTime * 2.05 + slab.bobPhase) * 0.028 * slab.crackAmount
        : 0;
      const bobTilt = buoyantActive
        ? Math.cos(elapsedTime * 1.7 + slab.bobPhase * 1.3) * 0.009 * slab.crackAmount
        : 0;

      if (buoyantActive || slab.gpuDirty) {
        const ease = smoothstep(0.0, 1.0, slab.crackAmount);
        slab.curDispX = slab.pushDirX * slab.maxPushDist * ease;
        slab.curDispZ = slab.pushDirZ * slab.maxPushDist * ease;
        slab.curScaleXZ = 1.0 - slab.shrinkMax * ease;
        slab.curY = slab.targetHeaveY * ease + bobY;
        slab.curPitchX = slab.targetPitchX * ease + bobTilt;
        slab.curRollZ = slab.targetRollZ * ease - bobTilt * 0.7;

        this.writeSlabTransformToBuffer(slab, posArr, normArr, propArr);
        slab.gpuDirty = false;
        anySlabUpdated = true;
      }
    }

    if (anySlabUpdated) {
      posAttr.needsUpdate = true;
      normAttr.needsUpdate = true;
      propAttr.needsUpdate = true;
    }

    // D. Animate Erupting 3D Pop-Up Crystal Shards along Cracked Seams!
    let anyShardDirty = false;
    for (let i = 0; i < MAX_POPUP_SHARDS; i++) {
      const sh = data.shards[i];
      if (!sh.active || sh.growProgress >= 1.0) continue;
      sh.growProgress = Math.min(1.0, sh.growProgress + dt * 3.8);
      const sEase = smoothstep(0.0, 1.0, sh.growProgress);
      this._shardDummy.position.set(sh.x, sh.y, sh.z);
      this._shardDummy.rotation.set(sh.rotX, sh.rotY, sh.rotZ);
      this._shardDummy.scale.set(
        sh.targetScaleXZ * sEase,
        sh.targetScaleY * sEase,
        sh.targetScaleXZ * sEase
      );
      this._shardDummy.updateMatrix();
      data.shardInstancedMesh.setMatrixAt(i, this._shardDummy.matrix);
      anyShardDirty = true;
    }
    if (anyShardDirty) {
      data.shardInstancedMesh.instanceMatrix.needsUpdate = true;
    }

    // E. Step Falling Pointy Cave Icicles & Heavy Calving Ice Blocks -> Smash & Crater 3D Slabs!
    for (const ic of data.fallingIcicles) {
      if (ic.state === 'HANGING') {
        const dzFocus = focusPos.z - ic.ceilPos.z;
        const dzCar = vehiclePos.z - ic.ceilPos.z;
        const activeDz = passiveObserver ? dzFocus : dzCar;
        if (activeDz > -6.0 && activeDz < ic.triggerDist) {
          ic.state = 'SHUDDERING';
          ic.shudderTimer = 0;
          ic.mesh.material = this.shudderIcicleMat;
          ic.shadowWarnRing.visible = true;
        }
      } else if (ic.state === 'SHUDDERING') {
        ic.shudderTimer += dt;
        ic.mesh.position.set(
          ic.ceilPos.x + Math.sin(elapsedTime * 58.0 + ic.id) * 0.22,
          ic.ceilPos.y,
          ic.ceilPos.z + Math.cos(elapsedTime * 52.0 + ic.id) * 0.22
        );
        const pulse = 1.0 + Math.sin(ic.shudderTimer * 24.0) * 0.25;
        ic.shadowWarnRing.scale.setScalar(ic.radiusXZ * 1.9 * pulse);

        if (ic.shudderTimer >= ic.shudderDuration) {
          ic.state = 'FALLING';
          ic.mesh.position.set(ic.ceilPos.x, ic.ceilPos.y, ic.ceilPos.z);
          ic.mesh.material =
            ic.kind === 'POINTY_ICICLE' ? this.crystalIcicleMat : this.glacierBlockMat;
          ic.vy = -3.5;
        }
      } else if (ic.state === 'FALLING') {
        ic.vy -= 28.5 * dt;
        ic.currentY += ic.vy * dt;

        const stopY =
          ic.kind === 'POINTY_ICICLE'
            ? ic.floorY + ic.lengthY * 0.72
            : ic.floorY + ic.radiusXZ * 0.78;

        if (ic.kind === 'ICE_BLOCK') {
          ic.mesh.rotation.x += 1.8 * dt;
          ic.mesh.rotation.z += 1.4 * dt;
        }

        if (ic.currentY <= stopY) {
          ic.currentY = stopY;
          ic.state = 'IMPALED';
          ic.shadowWarnRing.visible = false;
          ic.obstacle.position.set(ic.ceilPos.x, ic.floorY, ic.ceilPos.z);

          // Physically smash & crater the 3D Voronoi ice slabs around the impact point!
          this.crater3DSlabsAtImpact(
            data,
            ic.ceilPos.x,
            ic.ceilPos.z,
            ic.kind === 'ICE_BLOCK'
          );

          const impactWorld = new THREE.Vector3(ic.ceilPos.x, ic.floorY, ic.ceilPos.z);
          if (this.onIcicleShatterSlam) {
            this.onIcicleShatterSlam(impactWorld, ic.kind);
          }

          if (!ic.spawnedImpactFracture && data.fractures.length < 14) {
            ic.spawnedImpactFracture = true;
            const impRng = mulberry32((ic.id * 2654435761) >>> 0);
            data.fractures.push(
              this.createTravelingFracture(
                chunkGroup,
                ic.ceilPos.x,
                ic.ceilPos.z,
                impRng() * Math.PI * 2,
                lerp(34.0, 58.0, impRng()),
                999,
                true,
                false,
                impRng
              )
            );
          }
        }
        ic.mesh.position.y = ic.currentY;
      }
    }
  }

  /**
   * Steps blowing sub-zero spindrift snow particles across the flat 3D ice sheet!
   */
  public stepGlobalIceAtmosphere(
    active: boolean,
    focusPos: THREE.Vector3,
    dt: number
  ): void {
    this.atmosphereFxGroup.visible = active;
    if (!active) return;

    const posAttr = this.snowPoints.geometry.attributes.position as THREE.BufferAttribute;
    const arr = this.snowPositions;
    const vels = this.snowVelocities;

    for (let i = 0; i < this.snowCount; i++) {
      const ix = i * 3;
      arr[ix] += vels[ix] * dt;
      arr[ix + 1] += vels[ix + 1] * dt;
      arr[ix + 2] += vels[ix + 2] * dt;

      const dx = arr[ix] - focusPos.x;
      const dz = arr[ix + 2] - focusPos.z;
      if (dx > 70) arr[ix] -= 140;
      else if (dx < -70) arr[ix] += 140;

      if (dz > 80) arr[ix + 2] -= 160;
      else if (dz < -80) arr[ix + 2] += 160;

      if (arr[ix + 1] < focusPos.y - 4.0) {
        arr[ix + 1] = focusPos.y + 24.0 + (i % 7);
      }
    }
    posAttr.needsUpdate = true;
  }
}

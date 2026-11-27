import * as THREE from 'three';
import { clamp, fbm2D, lerp, mulberry32, noise2D, ridge2D, smoothstep } from '../math/noise';
import { ProceduralTextureLibrary } from '../graphics/proceduralTextures';
import { DeadwoodBuilder, DeadwoodChunkData } from './deadwoodSystem';
import { DesertVillageBuilder, DesertVillageChunkData } from './desertVillage';
import { GiantHollowLogBuilder, GiantHollowLogChunkData } from './giantHollowLogSystem';
import { GlacialIceBuilder, GlacialIceChunkData } from './glacialIceSystem';
import { SingularityChunkData, SingularityDesertBuilder } from './singularityDesertSystem';

export type BiomeId =
  | 'FOREST'
  | 'SWAMP'
  | 'DESERT'
  | 'DEADWOOD'
  | 'SINGULARITY'
  | 'GIANT_LOG'
  | 'GLACIAL_ICE';

export type SurfaceType =
  | 'PACKED_DIRT'
  | 'WET_MUD'
  | 'MUDSLIDE'
  | 'LOOSE_VERGE'
  | 'MOSS_ROCK'
  | 'SHALLOW_WATER'
  | 'HOLLOW_LOG'
  | 'CRISSCROSS_ROOT'
  | 'DESERT_SAND'
  | 'GLACIAL_ICE';

export interface BiomeAtmosphereSpec {
  id: BiomeId;
  title: string;
  subtitle: string;
  fogColorHex: number;
  fogDensity: number;
  skyHorizonRGB: [number, number, number];
  skyZenithRGB: [number, number, number];
  sunColorHex: number;
  sunIntensity: number;
  hemiSkyHex: number;
  hemiGroundHex: number;
}

export const BIOME_SPECS: Record<BiomeId, BiomeAtmosphereSpec> = {
  FOREST: {
    id: 'FOREST',
    title: 'PRIMEVAL TITAN FOREST',
    subtitle: 'MUDSLIDE SLOPES · HOLLOW LOGS · CANOPY VINES',
    fogColorHex: 0xc5bf9e,
    fogDensity: 0.0034,
    skyHorizonRGB: [0.82, 0.78, 0.64],
    skyZenithRGB: [0.27, 0.49, 0.62],
    sunColorHex: 0xfff0cc,
    sunIntensity: 3.0,
    hemiSkyHex: 0xdce8dc,
    hemiGroundHex: 0x3e3b26,
  },
  SWAMP: {
    id: 'SWAMP',
    title: 'GOTHIC MANGROVE BAYOU',
    subtitle: 'WEAVE-THROUGH STILT ROOTS · BLACKWATER LAGOONS',
    fogColorHex: 0x67796a,
    fogDensity: 0.0038,
    skyHorizonRGB: [0.54, 0.64, 0.56],
    skyZenithRGB: [0.18, 0.31, 0.30],
    sunColorHex: 0xefeac4,
    sunIntensity: 2.6,
    hemiSkyHex: 0xbfd4c4,
    hemiGroundHex: 0x242e22,
  },
  DESERT: {
    id: 'DESERT',
    title: 'BADLANDS SHACK VILLAGE',
    subtitle: 'RALLY SHANTYTOWN · COLLAPSING TOWERS · ROLLING BARRELS',
    fogColorHex: 0xdcb486,
    fogDensity: 0.0026,
    skyHorizonRGB: [0.90, 0.72, 0.50],
    skyZenithRGB: [0.24, 0.48, 0.74],
    sunColorHex: 0xffe4b5,
    sunIntensity: 3.35,
    hemiSkyHex: 0xffe6c8,
    hemiGroundHex: 0x6e472b,
  },
  DEADWOOD: {
    id: 'DEADWOOD',
    title: 'ASHEN DEADWOOD HOLLOW',
    subtitle: 'DEAD TIMBERLAND · ERUPTING LIVE ROOTS · GROUND HEAVE',
    fogColorHex: 0x8e9993,
    fogDensity: 0.0046,
    skyHorizonRGB: [0.60, 0.65, 0.62],
    skyZenithRGB: [0.36, 0.41, 0.40],
    sunColorHex: 0xfff0d3,
    sunIntensity: 2.85,
    hemiSkyHex: 0xd5ddd7,
    hemiGroundHex: 0x333a2e,
  },
  SINGULARITY: {
    id: 'SINGULARITY',
    title: 'VITRIFIED SINGULARITY DESERT',
    subtitle: 'STEEP SPLIT CLIFFS · MARS SANDSTORM · LIGHTNING & FLYING ROCKS',
    fogColorHex: 0xb65934,
    fogDensity: 0.0088, // Thick poor-visibility Mars sandstorm haze!
    skyHorizonRGB: [0.78, 0.36, 0.19],
    skyZenithRGB: [0.31, 0.12, 0.10],
    sunColorHex: 0xffa062,
    sunIntensity: 3.05,
    hemiSkyHex: 0xe88658,
    hemiGroundHex: 0x2a1511,
  },
  GIANT_LOG: {
    id: 'GIANT_LOG',
    title: 'COLOSSAL HOLLOW LOG ECOSYSTEM',
    subtitle: 'OUTSIDE GODRAYS · LOG-WALL PARASITE WORMS · MESSY COBWEBS',
    fogColorHex: 0x2c2216,
    fogDensity: 0.0052,
    skyHorizonRGB: [0.86, 0.80, 0.52],
    skyZenithRGB: [0.40, 0.62, 0.44],
    sunColorHex: 0xffe49c,
    sunIntensity: 3.45,
    hemiSkyHex: 0x8ea36d,
    hemiGroundHex: 0x2b1b10,
  },
  GLACIAL_ICE: {
    id: 'GLACIAL_ICE',
    title: 'SUB-ZERO GLACIAL ICE FLATS & CAVE',
    subtitle: 'FLAT SLIPPERY ICE · LIVE TRAVELING FRACTURES · FALLING CAVE ICICLES',
    fogColorHex: 0xb6e2f5,
    fogDensity: 0.0043,
    skyHorizonRGB: [0.76, 0.91, 0.98],
    skyZenithRGB: [0.20, 0.44, 0.68],
    sunColorHex: 0xeaf8ff,
    sunIntensity: 3.15,
    hemiSkyHex: 0xc8efff,
    hemiGroundHex: 0x244963,
  },
};

export interface TrailPointSpec {
  centerX: number;
  slopeX: number;
  curvature: number;
  baseY: number;
  halfWidth: number;
  mudPatch: number;
  splitFactor: number;
  splitOffset: number;
  /** >0 during Steep Hillside Flash-Flood Mudslide sections! */
  mudslideFactor: number;
  /** +1 = hillside slopes Left->Right (+X downhill), -1 = Right->Left (-X downhill) */
  mudslideDirX: number;
}

export interface TerrainSample {
  height: number;
  normal: THREE.Vector3;
  surface: SurfaceType;
  friction: number;
  rollingResistance: number;
  mudiness: number;
  waterDepth: number;
  /** Lateral downhill push speed (m/s) on Flash-Flood Mudslides */
  mudslidePushX: number;
  trailCenterX: number;
  trailHalfWidth: number;
  signedOffset: number;
}

export interface WorldObstacle {
  id: number;
  chunkIndex: number;
  kind: 'boulder' | 'shear_rock' | 'buttress_tree' | 'titan_tree' | 'fallen_log' | 'rolling_boulder';
  position: THREE.Vector3;
  radius: number;
  height: number;
  axisX?: number;
  axisZ?: number;
  halfLength?: number;
  underClearanceY?: number;
  velocityX?: number;
  velocityZ?: number;
  canShearVine: boolean;
}

export interface HollowLogTunnel {
  id: number;
  chunkIndex: number;
  center: THREE.Vector3;
  axisX: number;
  axisZ: number;
  yaw: number;
  halfLength: number;
  innerRadius: number;
  outerRadius: number;
}

/**
 * Colossal Static Drive-On / Crisscrossing Ground Root Bridge!
 * Players & AI can drive right up onto the flattened bark deck, crisscross over bog water/other roots, OR drive underneath!
 */
export interface CrisscrossRootSegment {
  ax: number;
  ay: number;
  az: number;
  bx: number;
  by: number;
  bz: number;
  deckHalfWidth: number; // ~2.25m (4.5m wide drivable root deck!)
  thickness: number; // ~1.25m vertical thickness (underpass if deckY - thickness > carY!)
}

export interface CrisscrossRootBridge {
  id: number;
  chunkIndex: number;
  minZ: number;
  maxZ: number;
  segments: CrisscrossRootSegment[];
}

export interface WaterPuddleZone {
  x: number;
  z: number;
  radiusX: number;
  radiusZ: number;
  waterSurfaceY: number;
  maxDepth: number;
}

export interface RollingBoulder {
  id: number;
  mesh: THREE.Mesh;
  position: THREE.Vector3;
  velocity: THREE.Vector3;
  radius: number;
  dirX: number;
  spawnX: number;
  spawnZ: number;
  maxTravelX: number;
  obstacle: WorldObstacle;
}

export interface DustDevil {
  mesh: THREE.Mesh;
  position: THREE.Vector3;
  baseX: number;
  baseZ: number;
  radius: number;
  phase: number;
}

export interface FuelPickup {
  id: number;
  chunkIndex: number;
  position: THREE.Vector3;
  mesh: THREE.Mesh;
  collectedCooldown: number;
}

export interface PortalGate {
  z: number;
  centerX: number;
  centerY: number;
  radius: number;
  isFinalFinish: boolean;
  group: THREE.Group;
  ringMesh: THREE.Mesh;
}

export interface FallingTreeEvent {
  id: number;
  chunkIndex: number;
  pivotGroup: THREE.Group;
  basePos: THREE.Vector3;
  fallDirAngle: number;
  currentTilt: number;
  maxTilt: number;
  angularVel: number;
  state: 'STANDING' | 'FALLING' | 'FALLEN';
  triggerDistance: number;
  trunkLength: number;
  trunkRadius: number;
  dynamicObstacle: WorldObstacle;
}

export interface CanopyAnchor {
  id: number;
  chunkIndex: number;
  position: THREE.Vector3;
  branchTangent: THREE.Vector3;
  trailCenterBelow: THREE.Vector3;
  trailHalfWidth: number;
  preferredArchetype: 'STRIKER' | 'CURTAIN' | 'ARCH_GROWER';
}

export interface RootAnchor {
  id: number;
  chunkIndex: number;
  position: THREE.Vector3;
  reachTargetPos: THREE.Vector3;
}

interface ChunkRecord {
  group: THREE.Group;
  terrainGeo: THREE.BufferGeometry;
  obstacles: WorldObstacle[];
  hollowLogs: HollowLogTunnel[];
  rootBridges: CrisscrossRootBridge[];
  puddles: WaterPuddleZone[];
  fallingTrees: FallingTreeEvent[];
  rollingBoulders: RollingBoulder[];
  dustDevils: DustDevil[];
  fuelPickups: FuelPickup[];
  anchors: CanopyAnchor[];
  rootAnchors: RootAnchor[];
  deadwoodData?: DeadwoodChunkData;
  desertVillageData?: DesertVillageChunkData;
  singularityData?: SingularityChunkData;
  giantLogData?: GiantHollowLogChunkData;
  glacialIceData?: GlacialIceChunkData;
}

// SPACIOUS OPEN-WORLD EXPEDITION DIMENSIONS (320m total width, 72m-108m wide open driving corridor!)
const CHUNK_LENGTH = 112;
const CORRIDOR_HALF_SPAN = 160;
const CHUNK_Z_SEGMENTS = 74;
const CHUNK_X_SEGMENTS = 82;

export class TrailCorridorWorld {
  public readonly group = new THREE.Group();
  public readonly chunkLength = CHUNK_LENGTH;

  public currentBiome: BiomeId = 'FOREST';
  public activePortal: PortalGate | null = null;

  public readonly playerPosUniform = { value: new THREE.Vector3() };
  public readonly playerVelUniform = { value: new THREE.Vector3() };
  public readonly timeUniform = { value: 0 };

  public onTreeStartFalling?: (worldPos: THREE.Vector3) => void;
  public onTreeSlamImpact?: (worldPos: THREE.Vector3) => void;

  private activeChunks = new Map<number, ChunkRecord>();
  private nextObstacleId = 1;
  private nextAnchorId = 1;
  private nextRootId = 1;
  private nextLogId = 1;
  private nextBridgeId = 1;
  private nextPickupId = 1;

  private deadwoodBuilder: DeadwoodBuilder;
  private desertVillageBuilder: DesertVillageBuilder;
  public readonly singularityBuilder: SingularityDesertBuilder;
  public readonly giantLogBuilder: GiantHollowLogBuilder;
  public readonly glacialIceBuilder: GlacialIceBuilder;

  private terrainMat: THREE.MeshStandardMaterial;
  private rockMat: THREE.MeshStandardMaterial;
  private desertRockMat: THREE.MeshStandardMaterial;
  private shearRockMat: THREE.MeshStandardMaterial;
  private barkMat: THREE.MeshStandardMaterial;
  private mangroveBarkMat: THREE.MeshStandardMaterial;
  private bioFungiMat: THREE.MeshStandardMaterial;
  private swampRootBridgeMat: THREE.MeshStandardMaterial;
  private hollowLogMat: THREE.MeshStandardMaterial;
  private swampBlackwaterMat: THREE.ShaderMaterial;
  private fernMat: THREE.MeshStandardMaterial;
  private broadleafMat: THREE.MeshStandardMaterial;
  private canopyLeafMat: THREE.MeshStandardMaterial;
  private swampCanopyMat: THREE.MeshStandardMaterial;
  private highLianaMat: THREE.MeshStandardMaterial;
  private sunShaftMat: THREE.ShaderMaterial;
  private waterMat: THREE.ShaderMaterial;
  private mudslideFlowMat: THREE.ShaderMaterial;
  private dustDevilMat: THREE.ShaderMaterial;
  private fuelCellMat: THREE.MeshStandardMaterial;

  private boulderGeos: THREE.BufferGeometry[] = [];
  private shearSpireGeo!: THREE.BufferGeometry;
  private cactusPillarGeo!: THREE.BufferGeometry;
  private midTrunkGeo!: THREE.BufferGeometry;
  private fallingTrunkGeo!: THREE.BufferGeometry;
  private titanTrunkGeo!: THREE.BufferGeometry;
  private titanCrownGeo!: THREE.BufferGeometry;
  private highLianaCurtainGeo!: THREE.BufferGeometry;
  private logGeo!: THREE.BufferGeometry;
  private fernClumpGeo!: THREE.BufferGeometry;
  private broadleafClumpGeo!: THREE.BufferGeometry;
  private canopyClusterGeo!: THREE.BufferGeometry;
  private sunShaftGeo!: THREE.BufferGeometry;
  private puddleDiscGeo!: THREE.BufferGeometry;
  private mudslideSheetGeo!: THREE.BufferGeometry;
  private dustDevilGeo!: THREE.BufferGeometry;
  private fuelCanisterGeo!: THREE.BufferGeometry;

  private onChunkSpawnedCallbacks: Array<
    (chunkIndex: number, anchors: CanopyAnchor[], rootAnchors: RootAnchor[]) => void
  > = [];
  private onChunkRemovedCallbacks: Array<(chunkIndex: number) => void> = [];

  constructor(private textures: ProceduralTextureLibrary) {
    this.deadwoodBuilder = new DeadwoodBuilder(textures);
    this.desertVillageBuilder = new DesertVillageBuilder(textures);
    this.singularityBuilder = new SingularityDesertBuilder(textures);
    this.giantLogBuilder = new GiantHollowLogBuilder(textures);
    this.glacialIceBuilder = new GlacialIceBuilder(textures);

    this.terrainMat = new THREE.MeshStandardMaterial({
      map: textures.terrainDetail.map,
      normalMap: textures.terrainDetail.normalMap,
      normalScale: new THREE.Vector2(1.2, 1.2),
      vertexColors: true,
      roughness: 0.87,
      metalness: 0.02,
    });

    this.rockMat = new THREE.MeshStandardMaterial({
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(1.4, 1.4),
      vertexColors: true,
      roughness: 0.8,
      metalness: 0.05,
    });

    this.desertRockMat = new THREE.MeshStandardMaterial({
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(1.5, 1.5),
      color: new THREE.Color(0xc46b3b),
      roughness: 0.82,
      metalness: 0.03,
    });

    this.shearRockMat = new THREE.MeshStandardMaterial({
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(1.75, 1.75),
      color: new THREE.Color(0xa69d8b),
      roughness: 0.56,
      metalness: 0.16,
    });

    this.barkMat = new THREE.MeshStandardMaterial({
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      normalScale: new THREE.Vector2(1.65, 1.65),
      color: new THREE.Color(0x7c634a),
      roughness: 0.88,
      metalness: 0.01,
    });

    this.swampRootBridgeMat = new THREE.MeshStandardMaterial({
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      normalScale: new THREE.Vector2(1.5, 1.5),
      vertexColors: true,
      roughness: 0.78,
      metalness: 0.02,
    });

    this.hollowLogMat = new THREE.MeshStandardMaterial({
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      normalScale: new THREE.Vector2(1.5, 1.5),
      vertexColors: true,
      side: THREE.DoubleSide,
      roughness: 0.82,
      metalness: 0.02,
    });

    // Weathered Gothic Mangrove Bark (vertex-colored pale silvery-ash upper arches -> wet moss & peat at waterline)
    this.mangroveBarkMat = new THREE.MeshStandardMaterial({
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      normalScale: new THREE.Vector2(1.85, 1.85),
      vertexColors: true,
      roughness: 0.84,
      metalness: 0.02,
    });

    // Natural warm woodland ochre shelf polypore bracket fungi (zero glowing neon!)
    this.bioFungiMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(0xb0844c),
      roughness: 0.86,
      metalness: 0.01,
    });

    this.swampBlackwaterMat = new THREE.ShaderMaterial({
      transparent: true,
      depthWrite: false,
      uniforms: { uTime: this.timeUniform },
      vertexShader: `
        varying vec2 vUv;
        varying vec3 vWorldPos;
        void main() {
          vUv = uv;
          vec4 wp = modelMatrix * vec4(position, 1.0);
          vWorldPos = wp.xyz;
          gl_Position = projectionMatrix * viewMatrix * wp;
        }
      `,
      fragmentShader: `
        varying vec2 vUv;
        varying vec3 vWorldPos;
        uniform float uTime;
        void main() {
          float radial = length((vUv - 0.5) * 2.0);
          float edgeAlpha = smoothstep(1.0, 0.52, radial);
          if (edgeAlpha < 0.01) discard;
          float ripple = sin(vWorldPos.x * 2.2 + uTime * 1.4) * cos(vWorldPos.z * 2.0 - uTime * 1.1) * 0.08;
          vec3 viewDir = normalize(cameraPosition - vWorldPos);
          vec3 norm = normalize(vec3(ripple, 1.0, ripple));
          float fresnel = pow(1.0 - clamp(dot(viewDir, norm), 0.0, 1.0), 2.2);
          vec3 blackPeat = vec3(0.04, 0.08, 0.07);
          vec3 mistReflect = vec3(0.44, 0.58, 0.53);
          vec3 col = mix(blackPeat, mistReflect, clamp(0.22 + fresnel * 0.78, 0.0, 0.92));
          gl_FragColor = vec4(col, edgeAlpha * 0.88);
        }
      `,
    });

    this.fernMat = this.createInteractiveFoliageMaterial(textures.fernAtlas, 0.9, 1.25);
    this.broadleafMat = this.createInteractiveFoliageMaterial(textures.broadleafAtlas, 0.82, 1.15);

    this.canopyLeafMat = new THREE.MeshStandardMaterial({
      map: textures.broadleafAtlas,
      alphaTest: 0.36,
      side: THREE.DoubleSide,
      color: new THREE.Color(0x527d34),
      roughness: 0.72,
    });

    this.swampCanopyMat = new THREE.MeshStandardMaterial({
      map: textures.broadleafAtlas,
      alphaTest: 0.36,
      side: THREE.DoubleSide,
      color: new THREE.Color(0x476b40),
      roughness: 0.76,
    });

    this.highLianaMat = new THREE.MeshStandardMaterial({
      map: textures.vineStem.map,
      color: new THREE.Color(0x6e9d40),
      roughness: 0.72,
    });

    this.fuelCellMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(0x9b3626),
      roughness: 0.58,
      metalness: 0.42,
    });

    this.sunShaftMat = new THREE.ShaderMaterial({
      transparent: true,
      depthWrite: false,
      side: THREE.DoubleSide,
      blending: THREE.AdditiveBlending,
      uniforms: { uTime: this.timeUniform },
      vertexShader: `
        varying vec2 vUv;
        varying float vViewFade;
        void main() {
          vUv = uv;
          vec4 mvPos = modelViewMatrix * vec4(position, 1.0);
          vec3 viewDir = normalize(-mvPos.xyz);
          vec3 viewNorm = normalize(normalMatrix * normal);
          vViewFade = pow(abs(dot(viewDir, viewNorm)), 1.35);
          gl_Position = projectionMatrix * mvPos;
        }
      `,
      fragmentShader: `
        varying vec2 vUv;
        varying float vViewFade;
        uniform float uTime;
        void main() {
          float verticalFade = smoothstep(0.0, 0.25, vUv.y) * smoothstep(1.0, 0.15, vUv.y);
          float shimmer = 0.85 + 0.15 * sin(uTime * 0.55 + vUv.x * 12.57);
          float alpha = verticalFade * vViewFade * 0.08 * shimmer;
          gl_FragColor = vec4(1.0, 0.90, 0.58, alpha);
        }
      `,
    });

    this.waterMat = new THREE.ShaderMaterial({
      transparent: true,
      depthWrite: false,
      uniforms: { uTime: this.timeUniform, uPlayerPos: this.playerPosUniform },
      vertexShader: `
        varying vec2 vUv;
        varying vec3 vWorldPos;
        void main() {
          vUv = uv;
          vec4 wp = modelMatrix * vec4(position, 1.0);
          vWorldPos = wp.xyz;
          gl_Position = projectionMatrix * viewMatrix * wp;
        }
      `,
      fragmentShader: `
        varying vec2 vUv;
        varying vec3 vWorldPos;
        uniform float uTime;
        uniform vec3 uPlayerPos;
        void main() {
          vec2 centered = (vUv - 0.5) * 2.0;
          float radial = length(centered);
          float edgeAlpha = smoothstep(1.0, 0.60, radial);
          if (edgeAlpha < 0.01) discard;

          float w1 = sin(vWorldPos.x * 3.8 + uTime * 2.5) * cos(vWorldPos.z * 3.4 - uTime * 2.0);
          float carDist = length(vWorldPos.xz - uPlayerPos.xz);
          float wake = sin(carDist * 6.0 - uTime * 13.0) * exp(-carDist * 0.2) * 1.4;
          float ripple = (w1 + wake) * 0.12;

          vec3 viewDir = normalize(cameraPosition - vWorldPos);
          vec3 norm = normalize(vec3(ripple, 1.0, ripple * 0.85));
          float fresnel = pow(1.0 - clamp(dot(viewDir, norm), 0.0, 1.0), 2.5);

          vec3 deepWater = vec3(0.11, 0.21, 0.17);
          vec3 skyReflect = vec3(0.56, 0.74, 0.70);
          vec3 sunDir = normalize(vec3(-0.42, 0.68, -0.6));
          float spec = pow(max(dot(norm, normalize(sunDir + viewDir)), 0.0), 42.0) * 0.8;

          vec3 col = mix(deepWater, skyReflect, clamp(0.28 + fresnel * 0.7, 0.0, 0.94)) + vec3(1.0, 0.94, 0.76) * spec;
          gl_FragColor = vec4(col, edgeAlpha * 0.84);
        }
      `,
    });

    // Animated Sideways Flash-Flood Mudslide Sheet Shader!
    this.mudslideFlowMat = new THREE.ShaderMaterial({
      transparent: true,
      depthWrite: false,
      uniforms: { uTime: this.timeUniform },
      vertexShader: `
        varying vec2 vUv;
        void main() {
          vUv = uv;
          gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
        }
      `,
      fragmentShader: `
        varying vec2 vUv;
        uniform float uTime;
        void main() {
          float fadeZ = smoothstep(0.0, 0.18, vUv.y) * smoothstep(1.0, 0.82, vUv.y);
          float fadeX = smoothstep(0.0, 0.12, vUv.x) * smoothstep(1.0, 0.88, vUv.x);
          float flow = vUv.x * 8.0 - uTime * 3.8;
          float streak = 0.5 + 0.5 * sin(flow + sin(vUv.y * 16.0) * 1.8);
          float foam = smoothstep(0.75, 0.98, sin(flow * 2.1 + vUv.y * 22.0));
          vec3 mudCol = mix(vec3(0.26, 0.17, 0.10), vec3(0.55, 0.41, 0.26), streak * 0.65);
          mudCol = mix(mudCol, vec3(0.85, 0.76, 0.62), foam * 0.45);
          gl_FragColor = vec4(mudCol, fadeX * fadeZ * 0.76);
        }
      `,
    });

    // Swirling Desert Dust Devil Vortex Shader!
    this.dustDevilMat = new THREE.ShaderMaterial({
      transparent: true,
      depthWrite: false,
      side: THREE.DoubleSide,
      uniforms: { uTime: this.timeUniform },
      vertexShader: `
        varying vec2 vUv;
        void main() {
          vUv = uv;
          gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
        }
      `,
      fragmentShader: `
        varying vec2 vUv;
        uniform float uTime;
        void main() {
          float hFade = smoothstep(0.0, 0.15, vUv.y) * smoothstep(1.0, 0.25, vUv.y);
          float swirl = sin(vUv.x * 18.85 + vUv.y * 14.0 - uTime * 9.5);
          float alpha = hFade * (0.26 + 0.14 * swirl);
          gl_FragColor = vec4(0.88, 0.68, 0.45, alpha);
        }
      `,
    });

    this.buildAssetGeometries();
    this.singularityBuilder.stormFxGroup.visible = false;
    this.group.add(this.singularityBuilder.stormFxGroup);
    this.giantLogBuilder.atmosphereFxGroup.visible = false;
    this.group.add(this.giantLogBuilder.atmosphereFxGroup);
    this.glacialIceBuilder.atmosphereFxGroup.visible = false;
    this.group.add(this.glacialIceBuilder.atmosphereFxGroup);
  }

  public onChunkSpawned(
    cb: (chunkIndex: number, anchors: CanopyAnchor[], rootAnchors: RootAnchor[]) => void
  ): void {
    this.onChunkSpawnedCallbacks.push(cb);
  }

  public onChunkRemoved(cb: (chunkIndex: number) => void): void {
    this.onChunkRemovedCallbacks.push(cb);
  }

  /**
   * Teleports the world into a new Biome and rebuilds active chunks around startZ!
   */
  public setBiomeAndRebuild(biome: BiomeId, startZ: number): void {
    this.currentBiome = biome;
    this.singularityBuilder.stormFxGroup.visible = biome === 'SINGULARITY';
    this.giantLogBuilder.clearActiveWebSnare();
    this.giantLogBuilder.atmosphereFxGroup.visible = biome === 'GIANT_LOG';
    this.glacialIceBuilder.atmosphereFxGroup.visible = biome === 'GLACIAL_ICE';
    this.resetAround(startZ);
  }

  /**
   * Spawns a Grounded Weathered Stone & Timber Pass Gate at the end of the stage!
   */
  public setPortalGateAt(portalZ: number, isFinalFinish: boolean): void {
    if (this.activePortal) {
      this.group.remove(this.activePortal.group);
      this.activePortal = null;
    }

    const spec = this.getTrailSpec(portalZ);
    const groundY = this.sampleHeightRaw(spec.centerX, portalZ).height;
    const halfSpan = 12.5;
    const pillarH = 14.5;

    const pGroup = new THREE.Group();
    pGroup.position.set(spec.centerX, groundY, portalZ);

    const stoneMat = this.currentBiome === 'DESERT' ? this.desertRockMat : this.rockMat;

    // Massive weathered stone piers + stepped plinths on left & right
    for (const side of [-1, 1]) {
      const plinth = new THREE.Mesh(new THREE.BoxGeometry(4.8, 1.4, 5.6), stoneMat);
      plinth.position.set(side * halfSpan, 0.5, 0);
      plinth.castShadow = true;
      plinth.receiveShadow = true;
      pGroup.add(plinth);

      const pier = new THREE.Mesh(new THREE.BoxGeometry(3.6, pillarH, 4.4), stoneMat);
      pier.position.set(side * halfSpan, pillarH * 0.5, 0);
      pier.castShadow = true;
      pier.receiveShadow = true;
      pGroup.add(pier);
    }

    // Heavy stone & timber lintel crossbeam overhead
    const lintel = new THREE.Mesh(
      new THREE.BoxGeometry(halfSpan * 2.0 + 5.6, 2.8, 4.8),
      stoneMat
    );
    lintel.position.set(0, pillarH + 1.1, 0);
    lintel.castShadow = true;
    lintel.receiveShadow = true;
    pGroup.add(lintel);

    // Subtle atmospheric mist veil inside the stone pass (no sci-fi neon!)
    const mistMat = new THREE.MeshBasicMaterial({
      color: isFinalFinish ? 0xe8d4a2 : 0xd3d5c4,
      transparent: true,
      opacity: 0.36,
      side: THREE.DoubleSide,
      depthWrite: false,
    });
    const veil = new THREE.Mesh(
      new THREE.PlaneGeometry(halfSpan * 2.0 - 3.4, pillarH - 0.8),
      mistMat
    );
    veil.position.set(0, pillarH * 0.5, 0);
    pGroup.add(veil);

    this.group.add(pGroup);
    this.activePortal = {
      z: portalZ,
      centerX: spec.centerX,
      centerY: groundY + pillarH * 0.5,
      radius: halfSpan,
      isFinalFinish,
      group: pGroup,
      ringMesh: veil,
    };
  }

  private createInteractiveFoliageMaterial(
    atlas: THREE.CanvasTexture,
    windAmp: number,
    pushRadiusScale: number
  ): THREE.MeshStandardMaterial {
    const mat = new THREE.MeshStandardMaterial({
      map: atlas,
      alphaTest: 0.35,
      side: THREE.DoubleSide,
      roughness: 0.68,
      metalness: 0.02,
      vertexColors: true,
    });

    mat.onBeforeCompile = (shader) => {
      shader.uniforms.uTime = this.timeUniform;
      shader.uniforms.uPlayerPos = this.playerPosUniform;
      shader.uniforms.uPlayerVel = this.playerVelUniform;

      shader.vertexShader =
        `
        uniform float uTime;
        uniform vec3 uPlayerPos;
        uniform vec3 uPlayerVel;
        ` + shader.vertexShader;

      shader.vertexShader = shader.vertexShader.replace(
        '#include <project_vertex>',
        `
        vec4 mvPosition = vec4( transformed, 1.0 );
        #ifdef USE_INSTANCING
          mvPosition = instanceMatrix * mvPosition;
        #endif

        vec4 worldPos4 = modelMatrix * mvPosition;
        float flex = clamp(transformed.y * 0.58, 0.0, 1.35);

        float gust = sin(uTime * 1.9 + worldPos4.x * 0.35 + worldPos4.z * 0.28)
                   + 0.45 * cos(uTime * 3.4 + worldPos4.z * 0.7);
        worldPos4.x += gust * 0.075 * ${windAmp.toFixed(2)} * flex;
        worldPos4.z += gust * 0.055 * ${windAmp.toFixed(2)} * flex;

        vec2 toPlant = worldPos4.xz - uPlayerPos.xz;
        float dist = length(toPlant);
        float pushZone = ${pushRadiusScale.toFixed(2)} * 3.4;
        if (dist < pushZone && dist > 0.001) {
          float strength = pow(1.0 - dist / pushZone, 1.65) * flex;
          vec2 pushDir = normalize(toPlant + uPlayerVel.xz * 0.06);
          worldPos4.xz += pushDir * strength * 1.35;
          worldPos4.y -= strength * 0.52;
        }

        mvPosition = viewMatrix * worldPos4;
        gl_Position = projectionMatrix * mvPosition;
        `
      );
    };

    return mat;
  }

  private buildAssetGeometries(): void {
    for (let v = 0; v < 3; v++) {
      const geo = new THREE.IcosahedronGeometry(1.0, 2);
      const pos = geo.attributes.position;
      const colors = new Float32Array(pos.count * 3);
      const seed = v * 37.3;

      for (let i = 0; i < pos.count; i++) {
        const vx = pos.getX(i);
        const vy = pos.getY(i);
        const vz = pos.getZ(i);

        const disp =
          0.78 +
          0.26 * fbm2D(vx * 1.8 + seed, vz * 1.8 - seed, 3) +
          0.12 * ridge2D(vy * 2.2 + seed, vx * 2.0, 2);
        pos.setXYZ(i, vx * disp, vy * disp * 0.88, vz * disp);

        const mossFactor = smoothstep(0.08, 0.65, vy);
        colors[i * 3] = lerp(0.54, 0.36, mossFactor);
        colors[i * 3 + 1] = lerp(0.5, 0.54, mossFactor);
        colors[i * 3 + 2] = lerp(0.45, 0.25, mossFactor);
      }
      geo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
      geo.computeVertexNormals();
      this.boulderGeos.push(geo);
    }

    {
      const geo = new THREE.ConeGeometry(1.0, 2.8, 7, 4);
      geo.translate(0, 1.2, 0);
      const pos = geo.attributes.position;
      for (let i = 0; i < pos.count; i++) {
        const x = pos.getX(i);
        const y = pos.getY(i);
        const z = pos.getZ(i);
        const crag = 0.8 + 0.36 * noise2D(x * 2.3 + y, z * 2.3 - y);
        pos.setXYZ(i, x * crag + y * 0.1, y, z * crag);
      }
      geo.computeVertexNormals();
      this.shearSpireGeo = geo;
    }

    // Desert Saguaro / Sandstone Hoodoo Pillar
    {
      const main = new THREE.CylinderGeometry(0.42, 0.55, 7.5, 8);
      main.translate(0, 3.6, 0);
      const armL = new THREE.CylinderGeometry(0.28, 0.32, 2.8, 7);
      armL.translate(-0.9, 4.4, 0);
      const armR = new THREE.CylinderGeometry(0.28, 0.32, 2.4, 7);
      armR.translate(0.9, 3.8, 0);
      this.cactusPillarGeo = this.mergeGeometriesSimple([main, armL, armR]);
    }

    {
      const height = 18.0;
      const geo = new THREE.CylinderGeometry(0.42, 0.95, height, 11, 9);
      const pos = geo.attributes.position;
      for (let i = 0; i < pos.count; i++) {
        const x = pos.getX(i);
        const y = pos.getY(i);
        const z = pos.getZ(i);
        const hNorm = clamp((y + height * 0.5) / height, 0, 1);
        const angle = Math.atan2(z, x);
        const buttress =
          Math.pow(1 - smoothstep(0.0, 0.34, hNorm), 1.8) *
          Math.max(0, Math.cos(angle * 5.0 + Math.sin(hNorm * 3.0) * 0.4)) *
          1.35;
        const radScale = 1.0 + buttress + noise2D(angle * 2.0, hNorm * 6.0) * 0.12;
        pos.setXYZ(i, x * radScale + Math.pow(hNorm, 2.0) * 0.65, y + height * 0.48, z * radScale);
      }
      geo.computeVertexNormals();
      this.midTrunkGeo = geo;
    }

    {
      const height = 25.0;
      const geo = new THREE.CylinderGeometry(0.36, 0.68, height, 10, 8);
      geo.translate(0, height * 0.5, 0);
      this.fallingTrunkGeo = geo;
    }

    {
      const height = 52.0;
      const geo = new THREE.CylinderGeometry(1.85, 3.65, height, 18, 16);
      const pos = geo.attributes.position;
      for (let i = 0; i < pos.count; i++) {
        const x = pos.getX(i);
        const y = pos.getY(i);
        const z = pos.getZ(i);
        const hNorm = clamp((y + height * 0.5) / height, 0, 1);
        const angle = Math.atan2(z, x);
        const rootFlare =
          Math.pow(1 - smoothstep(0.0, 0.26, hNorm), 2.1) *
          (0.65 + 1.55 * Math.pow(Math.max(0, Math.cos(angle * 6.0 + hNorm * 2.2)), 1.6));
        const crownFlare = smoothstep(0.78, 1.0, hNorm) * 0.85;
        const radScale = 1.0 + rootFlare + crownFlare + noise2D(angle * 3.5, hNorm * 9.0) * 0.16;
        pos.setXYZ(i, x * radScale, y + height * 0.48, z * radScale);
      }
      geo.computeVertexNormals();
      this.titanTrunkGeo = geo;
    }

    {
      const positions: number[] = [];
      const normals: number[] = [];
      const uvs: number[] = [];
      const indices: number[] = [];
      const rng = mulberry32(91823);
      let vo = 0;

      for (let i = 0; i < 18; i++) {
        const yaw = rng() * Math.PI * 2;
        const pitch = (rng() - 0.5) * 0.55;
        const dist = rng() * 9.5;
        const az = rng() * Math.PI * 2;
        const ox = Math.cos(az) * dist;
        const oy = (rng() - 0.3) * 6.5;
        const oz = Math.sin(az) * dist;
        const s = 5.5 + rng() * 4.8;

        const q = new THREE.Quaternion().setFromEuler(new THREE.Euler(pitch, yaw, (rng() - 0.5) * 0.4));
        const corners = [
          new THREE.Vector3(-s, 0, -s * 0.6),
          new THREE.Vector3(s, 0, -s * 0.6),
          new THREE.Vector3(-s, 0, s * 0.6),
          new THREE.Vector3(s, 0, s * 0.6),
        ];
        const n = new THREE.Vector3(0, 1, 0).applyQuaternion(q);
        for (const c of corners) {
          c.applyQuaternion(q);
          positions.push(c.x + ox, c.y + oy, c.z + oz);
          normals.push(n.x, n.y, n.z);
        }
        uvs.push(0, 0, 1, 0, 0, 1, 1, 1);
        indices.push(vo, vo + 1, vo + 2, vo + 1, vo + 3, vo + 2);
        vo += 4;
      }

      const geo = new THREE.BufferGeometry();
      geo.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
      geo.setAttribute('normal', new THREE.Float32BufferAttribute(normals, 3));
      geo.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
      geo.setIndex(indices);
      this.titanCrownGeo = geo;
    }

    {
      const bundleGeos: THREE.BufferGeometry[] = [];
      const rng = mulberry32(55123);
      for (let s = 0; s < 5; s++) {
        const ox = (rng() - 0.5) * 7.5;
        const oz = (rng() - 0.5) * 7.5;
        const drop = 18.0 + rng() * 14.0;
        const curve = new THREE.CatmullRomCurve3([
          new THREE.Vector3(ox, 0, oz),
          new THREE.Vector3(ox + (rng() - 0.5) * 1.4, -drop * 0.45, oz + (rng() - 0.5) * 1.4),
          new THREE.Vector3(ox + (rng() - 0.5) * 1.8, -drop, oz + (rng() - 0.5) * 1.8),
        ]);
        bundleGeos.push(new THREE.TubeGeometry(curve, 8, 0.14 + rng() * 0.08, 5, false));
      }
      this.highLianaCurtainGeo = this.mergeGeometriesSimple(bundleGeos);
    }

    {
      const geo = new THREE.CylinderGeometry(0.46, 0.56, 1.0, 10, 4);
      geo.rotateZ(Math.PI * 0.5);
      this.logGeo = geo;
    }

    this.fernClumpGeo = this.buildArchedFoliageClump(7, 2.05, 0.82, 0.68);
    this.broadleafClumpGeo = this.buildArchedFoliageClump(5, 1.85, 1.25, 0.52);
    this.canopyClusterGeo = this.buildCanopyFoliageCluster();

    {
      const geo = new THREE.CylinderGeometry(1.8, 5.4, 38.0, 12, 1, true);
      geo.translate(0, 18.0, 0);
      geo.rotateZ(-0.34);
      geo.rotateX(0.18);
      this.sunShaftGeo = geo;
    }

    {
      const geo = new THREE.PlaneGeometry(2.0, 2.0, 14, 14);
      geo.rotateX(-Math.PI * 0.5);
      this.puddleDiscGeo = geo;
    }

    {
      const geo = new THREE.PlaneGeometry(58.0, 56.0, 18, 18);
      geo.rotateX(-Math.PI * 0.5);
      this.mudslideSheetGeo = geo;
    }

    // 28m Tall Desert Dust Devil Funnel!
    {
      const geo = new THREE.CylinderGeometry(6.8, 1.6, 28.0, 16, 6, true);
      geo.translate(0, 14.0, 0);
      this.dustDevilGeo = geo;
    }

    // Glowing Bio-Fuel Canister Cell
    {
      const geo = new THREE.OctahedronGeometry(0.95, 1);
      geo.scale(0.85, 1.35, 0.85);
      this.fuelCanisterGeo = geo;
    }
  }

  private mergeGeometriesSimple(geos: THREE.BufferGeometry[]): THREE.BufferGeometry {
    const positions: number[] = [];
    const normals: number[] = [];
    const uvs: number[] = [];
    const indices: number[] = [];
    let vertOffset = 0;

    for (const g of geos) {
      const p = g.attributes.position;
      const n = g.attributes.normal;
      const u = g.attributes.uv;
      for (let i = 0; i < p.count; i++) {
        positions.push(p.getX(i), p.getY(i), p.getZ(i));
        normals.push(n.getX(i), n.getY(i), n.getZ(i));
        uvs.push(u ? u.getX(i) : 0, u ? u.getY(i) : 0);
      }
      const idx = g.getIndex();
      if (idx) {
        for (let i = 0; i < idx.count; i++) indices.push(vertOffset + idx.getX(i));
      }
      vertOffset += p.count;
      g.dispose();
    }

    const merged = new THREE.BufferGeometry();
    merged.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
    merged.setAttribute('normal', new THREE.Float32BufferAttribute(normals, 3));
    merged.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
    merged.setIndex(indices);
    return merged;
  }

  /**
   * Builds a colossal, flattened-crown Static Crisscrossing Ground Root Bridge!
   * Emerges smoothly out of the swamp/forest floor, arches 2.5m-5.8m high across the arena,
   * and gives players a 4.6m-wide wooden bark skyway to drive & crisscross on!
   */
  private buildCrisscrossRootBridge(
    chunkIndex: number,
    curve: THREE.CatmullRomCurve3,
    deckHalfWidth: number
  ): { mesh: THREE.Mesh; bridge: CrisscrossRootBridge } {
    const lengthSegs = 34;
    const radialSegs = 12;
    const positions: number[] = [];
    const colors: number[] = [];
    const uvs: number[] = [];
    const indices: number[] = [];
    const segments: CrisscrossRootSegment[] = [];

    const pts = curve.getSpacedPoints(lengthSegs);
    const thickness = 1.25;
    let minZ = Infinity;
    let maxZ = -Infinity;

    const up = new THREE.Vector3(0, 1, 0);
    const tangent = new THREE.Vector3();
    const right = new THREE.Vector3();

    for (let i = 0; i <= lengthSegs; i++) {
      const p = pts[i];
      if (p.z < minZ) minZ = p.z;
      if (p.z > maxZ) maxZ = p.z;

      if (i < lengthSegs) {
        const pn = pts[i + 1];
        segments.push({
          ax: p.x,
          ay: p.y,
          az: p.z,
          bx: pn.x,
          by: pn.y,
          bz: pn.z,
          deckHalfWidth,
          thickness,
        });
      }

      if (i < lengthSegs) tangent.subVectors(pts[i + 1], p).normalize();
      else tangent.subVectors(p, pts[i - 1]).normalize();

      right.crossVectors(tangent, up).normalize();
      const localUp = new THREE.Vector3().crossVectors(right, tangent).normalize();

      for (let r = 0; r <= radialSegs; r++) {
        const u = r / radialSegs;
        const ang = u * Math.PI * 2;
        const cosA = Math.cos(ang);
        const sinA = Math.sin(ang);

        // Wide flattened top driving deck (y near 0 on top, rounded gnarled root belly underneath!)
        const rx = cosA * deckHalfWidth * (sinA > 0 ? 1.02 : 0.88);
        const ry = sinA >= 0 ? sinA * 0.22 : sinA * thickness;
        const barkWave = noise2D(ang * 3.0, i * 0.45) * 0.08;

        const wx = p.x + right.x * (rx + barkWave) + localUp.x * ry;
        const wy = p.y + right.y * (rx + barkWave) + localUp.y * ry;
        const wz = p.z + right.z * (rx + barkWave) + localUp.z * ry;

        positions.push(wx, wy, wz);
        uvs.push(u * 3.0, i * 0.35);

        // Moss-trimmed edges with warm high-visibility golden-redwood center bark deck!
        const isTopDeck = sinA > 0.15 && Math.abs(cosA) < 0.78;
        const edgeMoss = sinA > 0.0 && Math.abs(cosA) >= 0.78;
        if (isTopDeck) {
          colors.push(0.66, 0.48, 0.31);
        } else if (edgeMoss) {
          colors.push(0.32, 0.56, 0.24);
        } else {
          colors.push(0.38, 0.27, 0.18);
        }
      }
    }

    const stride = radialSegs + 1;
    for (let i = 0; i < lengthSegs; i++) {
      for (let r = 0; r < radialSegs; r++) {
        const a = i * stride + r;
        const b = a + 1;
        const c = a + stride;
        const d = c + 1;
        indices.push(a, c, b, b, c, d);
      }
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
    geo.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));
    geo.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
    geo.setIndex(indices);
    geo.computeVertexNormals();

    const mesh = new THREE.Mesh(geo, this.swampRootBridgeMat);
    mesh.castShadow = true;
    mesh.receiveShadow = true;
    mesh.userData.uniqueGeo = true;

    return {
      mesh,
      bridge: {
        id: this.nextBridgeId++,
        chunkIndex,
        minZ: minZ - 4,
        maxZ: maxZ + 4,
        segments,
      },
    };
  }

  /**
   * Builds an organic, gnarled, longitudinally-fluted cylindrical tree root tube
   * (modeled after real Rhizophora mangrove stilt roots & banyan buttresses!).
   */
  private buildOrganicMangroveTube(
    curve: THREE.CatmullRomCurve3,
    radStart: number,
    radEnd: number,
    lengthSegs: number,
    radialSegs: number,
    waterLevelY: number,
    seed: number
  ): THREE.BufferGeometry {
    const pts = curve.getPoints(lengthSegs);
    const positions: number[] = [];
    const colors: number[] = [];
    const uvs: number[] = [];
    const indices: number[] = [];

    const up = new THREE.Vector3(0, 1, 0);
    const tangent = new THREE.Vector3();
    const right = new THREE.Vector3();
    const localUp = new THREE.Vector3();

    for (let i = 0; i <= lengthSegs; i++) {
      const t = i / lengthSegs;
      const p = pts[i];
      if (i < lengthSegs) tangent.subVectors(pts[i + 1], p).normalize();
      else tangent.subVectors(p, pts[i - 1]).normalize();

      if (Math.abs(tangent.y) > 0.96) {
        right.set(1, 0, 0);
      } else {
        right.crossVectors(tangent, up).normalize();
      }
      localUp.crossVectors(right, tangent).normalize();

      // Natural organic root flare at collar & slight swelling before mud entry
      const baseR = lerp(radStart, radEnd, Math.pow(t, 0.72));

      for (let r = 0; r <= radialSegs; r++) {
        const u = r / radialSegs;
        const ang = u * Math.PI * 2;
        // Bark buttress flutes spiraling gently down the cylindrical root
        const flute =
          1.0 +
          0.14 * Math.cos(ang * 5.0 + t * 4.2 + seed) +
          0.08 * noise2D(ang * 2.5 + seed, t * 7.0);
        const rad = baseR * flute;

        const rx = Math.cos(ang) * rad;
        const ry = Math.sin(ang) * rad;

        const wx = p.x + right.x * rx + localUp.x * ry;
        const wy = p.y + right.y * rx + localUp.y * ry;
        const wz = p.z + right.z * rx + localUp.z * ry;

        positions.push(wx, wy, wz);
        uvs.push(u * 2.5, t * 6.0);

        // Pale silvery-taupe bark on high arches -> mossy olive mid-zone -> dark wet peat at waterline!
        const heightAboveWater = wy - waterLevelY;
        const wetMask = 1.0 - smoothstep(0.1, 2.2, heightAboveWater);
        const mossBand = smoothstep(0.4, 2.4, heightAboveWater) * smoothstep(6.8, 2.8, heightAboveWater);

        let cr = lerp(0.64, 0.16, wetMask);
        let cg = lerp(0.60, 0.17, wetMask);
        let cb = lerp(0.54, 0.13, wetMask);

        cr = lerp(cr, 0.27, mossBand * 0.68);
        cg = lerp(cg, 0.44, mossBand * 0.68);
        cb = lerp(cb, 0.24, mossBand * 0.68);

        colors.push(cr, cg, cb);
      }
    }

    const stride = radialSegs + 1;
    for (let i = 0; i < lengthSegs; i++) {
      for (let r = 0; r < radialSegs; r++) {
        const a = i * stride + r;
        const b = a + 1;
        const c = a + stride;
        const d = c + 1;
        indices.push(a, c, b, b, c, d);
      }
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
    geo.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));
    geo.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
    geo.setIndex(indices);
    geo.computeVertexNormals();
    return geo;
  }

  /**
   * Spawns a Colossal Mangrove Stilt-Root Patriarch whose trunk sits suspended 10m-16m in the air
   * on sprawling, branching cylindrical stilt-root arches (with secondary prop-roots, pneumatophore knees,
   * bioluminescent shelf fungi & hanging Spanish moss) that players can weave directly THROUGH and UNDER!
   */
  private spawnMangroveCathedralTree(
    chunkGroup: THREE.Group,
    obstacles: WorldObstacle[],
    canopyClusters: THREE.Matrix4[],
    highLianaTransforms: THREE.Matrix4[],
    chunkIndex: number,
    centerX: number,
    centerZ: number,
    rng: () => number,
    wideSpanFactor = 1.0
  ): void {
    const baseGroundY = this.sampleHeightRaw(centerX, centerZ).height;
    const collarHeight = lerp(10.5, 15.8, rng()) * wideSpanFactor;
    const collarY = baseGroundY + collarHeight;
    const dummy = new THREE.Object3D();

    const subGeos: THREE.BufferGeometry[] = [];

    // 1. Upper Gnarled Cathedral Trunk rising from the suspended root-collar up into the misty canopy!
    const upperTrunkCurve = new THREE.CatmullRomCurve3([
      new THREE.Vector3(centerX, collarY - 1.4, centerZ),
      new THREE.Vector3(
        centerX + (rng() - 0.5) * 2.4,
        collarY + 8.5,
        centerZ + (rng() - 0.5) * 2.4
      ),
      new THREE.Vector3(
        centerX + (rng() - 0.5) * 4.5,
        collarY + 19.0,
        centerZ + (rng() - 0.5) * 4.5
      ),
      new THREE.Vector3(
        centerX + (rng() - 0.5) * 6.0,
        collarY + 29.0,
        centerZ + (rng() - 0.5) * 6.0
      ),
    ]);
    subGeos.push(
      this.buildOrganicMangroveTube(
        upperTrunkCurve,
        2.15,
        0.95,
        14,
        10,
        baseGroundY,
        chunkIndex * 7.1
      )
    );

    // Canopy crown & hanging Spanish moss curtains from high boughs
    const topPt = upperTrunkCurve.getPoint(0.95);
    for (let c = 0; c < 4; c++) {
      const ang = (c / 4) * Math.PI * 2 + rng() * 0.5;
      dummy.position.set(
        topPt.x + Math.cos(ang) * 7.5,
        topPt.y - 2.0 + rng() * 3.0,
        topPt.z + Math.sin(ang) * 7.5
      );
      dummy.rotation.set(0, rng() * Math.PI * 2, 0);
      dummy.scale.setScalar(lerp(2.1, 3.0, rng()));
      dummy.updateMatrix();
      canopyClusters.push(dummy.matrix.clone());
    }
    dummy.position.set(centerX, collarY + 18.0, centerZ);
    dummy.rotation.set(0, rng() * Math.PI * 2, 0);
    dummy.scale.setScalar(1.35);
    dummy.updateMatrix();
    highLianaTransforms.push(dummy.matrix.clone());

    // 2. 8 Colossal Radiating Primary Stilt-Root Arches + Bifurcating Secondary Prop-Roots!
    const primaryCount = 8;
    for (let i = 0; i < primaryCount; i++) {
      const azim = (i / primaryCount) * Math.PI * 2 + (rng() - 0.5) * 0.28;
      const radialSpan = lerp(15.5, 27.5, rng()) * wideSpanFactor;
      const endX = centerX + Math.cos(azim) * radialSpan;
      const endZ = centerZ + Math.sin(azim) * radialSpan;
      const endGroundY = this.sampleHeightRaw(endX, endZ).height;

      // High Gothic shoulder arch so vehicles/cameras easily clear 8m-13m tall vaulted openings!
      const midR = radialSpan * 0.52;
      const midLateralBow = (rng() - 0.5) * 3.6;
      const perpX = -Math.sin(azim);
      const perpZ = Math.cos(azim);

      const p0 = new THREE.Vector3(
        centerX + Math.cos(azim) * 1.1,
        collarY + lerp(-0.8, 1.8, rng()),
        centerZ + Math.sin(azim) * 1.1
      );
      const p1 = new THREE.Vector3(
        centerX + Math.cos(azim) * (radialSpan * 0.28) + perpX * midLateralBow * 0.5,
        collarY + lerp(0.5, 2.4, rng()), // Sweeps UP slightly from collar like Gothic mangrove vaults!
        centerZ + Math.sin(azim) * (radialSpan * 0.28) + perpZ * midLateralBow * 0.5
      );
      const p2 = new THREE.Vector3(
        centerX + Math.cos(azim) * midR + perpX * midLateralBow,
        lerp(endGroundY, collarY, 0.66),
        centerZ + Math.sin(azim) * midR + perpZ * midLateralBow
      );
      const p3 = new THREE.Vector3(endX, endGroundY - 1.2, endZ);

      const rootCurve = new THREE.CatmullRomCurve3([p0, p1, p2, p3]);
      subGeos.push(
        this.buildOrganicMangroveTube(
          rootCurve,
          lerp(1.05, 1.38, rng()),
          lerp(0.52, 0.72, rng()),
          18,
          9,
          baseGroundY,
          i * 3.7 + chunkIndex
        )
      );

      // Physical collision pillar where the giant stilt root plunges into the mud/water!
      obstacles.push({
        id: this.nextObstacleId++,
        chunkIndex,
        kind: 'buttress_tree',
        position: new THREE.Vector3(endX, endGroundY, endZ),
        radius: 0.82,
        height: 6.5,
        canShearVine: true,
      });

      // Secondary Bifurcating Prop-Root branching off mid-arch (like real mangrove stilt tangles!)
      if (i % 2 === 0 || rng() < 0.65) {
        const branchOrigin = rootCurve.getPoint(lerp(0.42, 0.58, rng()));
        const subAngle = azim + (rng() < 0.5 ? -1 : 1) * lerp(0.22, 0.42, rng());
        const subDist = radialSpan * lerp(0.76, 1.15, rng());
        const sx = centerX + Math.cos(subAngle) * subDist;
        const sz = centerZ + Math.sin(subAngle) * subDist;
        const sy = this.sampleHeightRaw(sx, sz).height;

        const propCurve = new THREE.CatmullRomCurve3([
          branchOrigin,
          new THREE.Vector3(
            lerp(branchOrigin.x, sx, 0.48),
            lerp(sy, branchOrigin.y, 0.68),
            lerp(branchOrigin.z, sz, 0.48)
          ),
          new THREE.Vector3(sx, sy - 1.0, sz),
        ]);
        subGeos.push(
          this.buildOrganicMangroveTube(
            propCurve,
            0.62,
            0.36,
            12,
            7,
            baseGroundY,
            i * 9.1
          )
        );

        obstacles.push({
          id: this.nextObstacleId++,
          chunkIndex,
          kind: 'buttress_tree',
          position: new THREE.Vector3(sx, sy, sz),
          radius: 0.55,
          height: 5.0,
          canShearVine: true,
        });
      }

      // Glowing Bioluminescent Shelf Fungi cluster on damp lower stilt-root knee!
      if (i % 3 === 0) {
        const fungiPt = rootCurve.getPoint(0.78);
        const shelf = new THREE.Mesh(
          new THREE.CylinderGeometry(0.65, 0.35, 0.14, 9),
          this.bioFungiMat
        );
        shelf.position.set(fungiPt.x, fungiPt.y + 0.2, fungiPt.z);
        chunkGroup.add(shelf);
      }
    }

    // 3. Vertical Pneumatophore Breathing Root Spikes poking up from muddy bayou floor!
    for (let k = 0; k < 14; k++) {
      const ka = rng() * Math.PI * 2;
      const kr = lerp(4.0, 24.0, rng());
      const kx = centerX + Math.cos(ka) * kr;
      const kz = centerZ + Math.sin(ka) * kr;
      const ky = this.sampleHeightRaw(kx, kz).height;
      const kh = lerp(0.85, 2.15, rng());
      const kneeGeo = new THREE.ConeGeometry(lerp(0.14, 0.26, rng()), kh, 6);
      kneeGeo.translate(kx, ky + kh * 0.42, kz);
      const vCount = kneeGeo.attributes.position.count;
      const kCols = new Float32Array(vCount * 3);
      for (let v = 0; v < vCount; v++) {
        kCols[v * 3] = 0.26;
        kCols[v * 3 + 1] = 0.28;
        kCols[v * 3 + 2] = 0.20;
      }
      kneeGeo.setAttribute('color', new THREE.BufferAttribute(kCols, 3));
      subGeos.push(kneeGeo);
    }

    const mergedMangroveGeo = this.mergeGeometriesSimple(subGeos);
    subGeos.forEach((g) => g.dispose());

    const mangroveMesh = new THREE.Mesh(mergedMangroveGeo, this.mangroveBarkMat);
    mangroveMesh.castShadow = true;
    mangroveMesh.receiveShadow = true;
    mangroveMesh.userData.uniqueGeo = true;
    chunkGroup.add(mangroveMesh);
  }

  private createHollowLogGeometry(
    halfLen: number,
    innerR: number,
    outerR: number,
    seed: number
  ): THREE.BufferGeometry {
    const zSegs = 26;
    const arcSegs = 26;
    const positions: number[] = [];
    const colors: number[] = [];
    const uvs: number[] = [];
    const indices: number[] = [];

    let vo = 0;
    const addShell = (radius: number, isInner: boolean) => {
      const baseV = vo;
      for (let iz = 0; iz <= zSegs; iz++) {
        const tz = iz / zSegs;
        const zLocal = (tz * 2 - 1) * halfLen;

        for (let ia = 0; ia <= arcSegs; ia++) {
          const ta = ia / arcSegs;
          const angle = ta * Math.PI * 2;
          const endJagged = Math.sin(angle * 3.0 + seed) * 1.35 + Math.cos(angle * 5.0 - seed) * 0.7;
          const zWarped = clamp(
            zLocal + (tz < 0.08 || tz > 0.92 ? endJagged * (tz < 0.5 ? -1 : 1) : 0),
            -halfLen - 1.8,
            halfLen + 1.8
          );

          const barkRough = isInner
            ? noise2D(angle * 2.5, zWarped * 0.35 + seed) * 0.08
            : ridge2D(angle * 3.5 + seed, zWarped * 0.28, 2) * 0.24;

          const r = radius + barkRough;
          const x = Math.cos(angle) * r;
          let y = Math.sin(angle) * r;
          if (!isInner && y < -innerR * 0.82) y = lerp(y, -innerR * 0.96, 0.55);

          positions.push(x, y, zWarped);
          uvs.push(ta * 4.0, (zWarped + halfLen) * 0.18);

          if (isInner) {
            const bottomMask = smoothstep(-0.35, -0.95, Math.sin(angle));
            colors.push(
              lerp(0.76, 0.34, bottomMask),
              lerp(0.52, 0.38, bottomMask),
              lerp(0.30, 0.22, bottomMask)
            );
          } else {
            const topMoss = smoothstep(0.25, 0.88, Math.sin(angle));
            colors.push(
              lerp(0.46, 0.28, topMoss),
              lerp(0.33, 0.48, topMoss),
              lerp(0.23, 0.20, topMoss)
            );
          }
          vo++;
        }
      }

      const ringStride = arcSegs + 1;
      for (let iz = 0; iz < zSegs; iz++) {
        const tzMid = (iz + 0.5) / zSegs;
        for (let ia = 0; ia < arcSegs; ia++) {
          const angleMid = ((ia + 0.5) / arcSegs) * Math.PI * 2;
          const inRoof = Math.sin(angleMid) > 0.72;
          if (inRoof && (Math.abs(tzMid - 0.33) < 0.08 || Math.abs(tzMid - 0.68) < 0.08)) continue;

          const a = baseV + iz * ringStride + ia;
          const b = a + 1;
          const c = a + ringStride;
          const d = c + 1;
          if (isInner) indices.push(a, c, b, b, c, d);
          else indices.push(a, b, c, b, d, c);
        }
      }
      return baseV;
    };

    const innerBase = addShell(innerR, true);
    const outerBase = addShell(outerR, false);

    const ringStride = arcSegs + 1;
    for (const endZIdx of [0, zSegs]) {
      const iRow = innerBase + endZIdx * ringStride;
      const oRow = outerBase + endZIdx * ringStride;
      for (let ia = 0; ia < arcSegs; ia++) {
        const i0 = iRow + ia;
        const i1 = i0 + 1;
        const o0 = oRow + ia;
        const o1 = o0 + 1;
        if (endZIdx === 0) indices.push(i0, o0, i1, i1, o0, o1);
        else indices.push(i0, i1, o0, i1, o1, o0);
      }
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
    geo.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));
    geo.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
    geo.setIndex(indices);
    geo.computeVertexNormals();
    return geo;
  }

  private buildArchedFoliageClump(
    frondCount: number,
    frondLength: number,
    frondWidth: number,
    archDrop: number
  ): THREE.BufferGeometry {
    const positions: number[] = [];
    const normals: number[] = [];
    const uvs: number[] = [];
    const colors: number[] = [];
    const indices: number[] = [];

    const segs = 4;
    let vertOffset = 0;
    const rng = mulberry32(Math.round(frondCount * 997 + frondLength * 100));

    for (let f = 0; f < frondCount; f++) {
      const azim = (f / frondCount) * Math.PI * 2 + (rng() - 0.5) * 0.45;
      const tilt = 0.36 + rng() * 0.42;
      const scale = 0.82 + rng() * 0.38;
      const len = frondLength * scale;
      const wid = frondWidth * scale;

      const cosA = Math.cos(azim);
      const sinA = Math.sin(azim);

      for (let s = 0; s <= segs; s++) {
        const t = s / segs;
        const radial = Math.pow(t, 0.88) * len * Math.sin(tilt + t * 0.65);
        const height =
          Math.sin(t * Math.PI * 0.68) * len * Math.cos(tilt * 0.65) - t * t * archDrop * scale;

        const cx = cosA * radial;
        const cz = sinA * radial;
        const cy = Math.max(0.02, height);
        const wx = -sinA * wid * 0.5;
        const wz = cosA * wid * 0.5;

        const shade = lerp(0.5, 1.06, Math.pow(t, 0.65));
        positions.push(cx - wx, cy, cz - wz, cx + wx, cy, cz + wz);
        normals.push(cosA * 0.25, 0.94, sinA * 0.25, cosA * 0.25, 0.94, sinA * 0.25);
        uvs.push(0, t, 1, t);
        colors.push(shade, shade, shade * 0.92, shade, shade, shade * 0.92);
      }

      for (let s = 0; s < segs; s++) {
        const i0 = vertOffset + s * 2;
        indices.push(i0, i0 + 1, i0 + 2, i0 + 1, i0 + 3, i0 + 2);
      }
      vertOffset += (segs + 1) * 2;
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
    geo.setAttribute('normal', new THREE.Float32BufferAttribute(normals, 3));
    geo.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
    geo.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));
    geo.setIndex(indices);
    return geo;
  }

  private buildCanopyFoliageCluster(): THREE.BufferGeometry {
    const positions: number[] = [];
    const normals: number[] = [];
    const uvs: number[] = [];
    const indices: number[] = [];
    const rng = mulberry32(4411);
    let vo = 0;

    for (let i = 0; i < 10; i++) {
      const yaw = rng() * Math.PI * 2;
      const pitch = (rng() - 0.5) * 0.85;
      const ox = (rng() - 0.5) * 3.4;
      const oy = (rng() - 0.5) * 1.4;
      const oz = (rng() - 0.5) * 3.4;
      const s = 2.4 + rng() * 1.8;

      const q = new THREE.Quaternion().setFromEuler(new THREE.Euler(pitch, yaw, (rng() - 0.5) * 0.6));
      const corners = [
        new THREE.Vector3(-s, 0, -s * 0.5),
        new THREE.Vector3(s, 0, -s * 0.5),
        new THREE.Vector3(-s, 0, s * 0.5),
        new THREE.Vector3(s, 0, s * 0.5),
      ];
      const n = new THREE.Vector3(0, 1, 0).applyQuaternion(q);
      for (const c of corners) {
        c.applyQuaternion(q);
        positions.push(c.x + ox, c.y + oy, c.z + oz);
        normals.push(n.x, n.y, n.z);
      }
      uvs.push(0, 0, 1, 0, 0, 1, 1, 1);
      indices.push(vo, vo + 1, vo + 2, vo + 1, vo + 3, vo + 2);
      vo += 4;
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
    geo.setAttribute('normal', new THREE.Float32BufferAttribute(normals, 3));
    geo.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
    geo.setIndex(indices);
    return geo;
  }

  /**
   * SPACIOUS, WIDE-OPEN RALLY ARENA CORRIDOR (halfWidth = 34m - 52m -> 68m to 104m wide!)
   * Features Periodic Split Roads AND Steep Hillside Flash-Flood Mudslide Zones!
   */
  public getTrailSpec(z: number): TrailPointSpec {
    const distForward = Math.max(0, -z);
    const introFade = smoothstep(8, 55, distForward);

    const calcCenter = (zf: number): number => {
      const d = Math.max(0, -zf);
      const fade = smoothstep(8, 55, d);
      const primary = Math.sin(zf * 0.011) * 24.0;
      const secondary = Math.sin(zf * 0.029 + 1.3) * 8.5;
      const organic = noise2D(zf * 0.008, 3.7) * 11.0;
      return (primary + secondary + organic) * fade;
    };

    const eps = 0.5;
    const c0 = calcCenter(z);
    const cPrev = calcCenter(z - eps);
    const cNext = calcCenter(z + eps);
    const slopeX = (cNext - cPrev) / (2 * eps);
    const curvature = (cNext - 2 * c0 + cPrev) / (eps * eps);

    const elevScale =
      this.currentBiome === 'DESERT' ? 1.65 : this.currentBiome === 'SWAMP' ? 0.55 : 1.0;
    const elevFade = smoothstep(10, 60, distForward);
    const cliffRollerFade = smoothstep(20, 82, distForward);
    const baseY =
      this.currentBiome === 'GLACIAL_ICE'
        ? 0 // True flat frozen glacial lake / ice sheet plane!
        : this.currentBiome === 'SINGULARITY'
        ? 38.0 +
          (Math.sin(z * 0.017) * 14.5 +
            Math.sin(z * 0.039 + 0.9) * 5.2 +
            noise2D(z * 0.012, 19.4) * 8.0) *
            cliffRollerFade
        : (Math.sin(z * 0.016) * 2.6 +
            Math.sin(z * 0.041 + 0.8) * 0.75 +
            noise2D(z * 0.012, 19.4) * 2.0) *
          elevFade *
          elevScale;

    // Periodic Split-Road Fork (~every 336m in Forest/Swamp, ~every 224m dramatic cliff-split in Singularity!)
    let splitFactor = 0;
    if (this.currentBiome === 'SINGULARITY' && distForward > 68) {
      const period = 224;
      const phase = ((distForward - 68) % period) / period;
      splitFactor = smoothstep(0.06, 0.18, phase) * smoothstep(0.58, 0.44, phase);
    } else if (
      distForward > 95 &&
      (this.currentBiome === 'FOREST' || this.currentBiome === 'SWAMP')
    ) {
      const period = 336;
      const phase = ((distForward - 95) % period) / period;
      splitFactor = smoothstep(0.05, 0.16, phase) * smoothstep(0.48, 0.36, phase);
    }
    const splitOffset = splitFactor * (this.currentBiome === 'SINGULARITY' ? 19.0 : 18.5);

    // Periodic Flash-Flood Hillside Mudslide Zone (occurs in non-split stretches in FOREST & SWAMP!)
    let mudslideFactor = 0;
    let mudslideDirX = 1;
    if (
      (this.currentBiome === 'FOREST' || this.currentBiome === 'SWAMP') &&
      distForward > 160
    ) {
      const period = 336;
      const phase = ((distForward - 95) % period) / period;
      mudslideFactor = smoothstep(0.56, 0.66, phase) * smoothstep(0.92, 0.82, phase);
      mudslideDirX = Math.floor((distForward - 95) / period) % 2 === 0 ? 1 : -1;
    }

    // VAST, SPACIOUS HALF-WIDTH (in SINGULARITY, dynamically pinches from 22.5m plateau down to 7.2m thin cliff spine!)
    const widthWave = Math.sin(z * 0.022 + 0.7) * 6.5 + noise2D(z * 0.018, 41.2) * 5.0;
    const cliffPinch = Math.pow(Math.sin(z * 0.029 + 0.5) * 0.5 + 0.5, 1.25) * introFade;
    const baseHalfWidth =
      this.currentBiome === 'GLACIAL_ICE'
        ? 32.0
        : this.currentBiome === 'GIANT_LOG'
        ? 26.5
        : this.currentBiome === 'SINGULARITY'
        ? lerp(22.5, 7.2, cliffPinch)
        : this.currentBiome === 'DEADWOOD'
        ? clamp(21.0 + widthWave * 0.45 * introFade, 18.0, 26.0)
        : this.currentBiome === 'DESERT'
        ? clamp(28.0 + widthWave * 0.45 * introFade, 24.0, 34.0)
        : clamp(38.0 + widthWave * introFade, 33.0, 48.0);
    const halfWidth =
      this.currentBiome === 'SINGULARITY'
        ? baseHalfWidth + splitOffset * 0.35
        : baseHalfWidth + splitOffset * 0.65;

    const mudNoise = fbm2D(z * 0.032, 7.2, 3) * 0.5 + 0.5;
    const mudPatch =
      (this.currentBiome === 'SWAMP'
        ? smoothstep(0.32, 0.68, mudNoise)
        : this.currentBiome === 'DESERT' ||
          this.currentBiome === 'SINGULARITY' ||
          this.currentBiome === 'GLACIAL_ICE'
        ? 0
        : smoothstep(0.54, 0.82, mudNoise)) * introFade;

    return {
      centerX: c0,
      slopeX,
      curvature,
      baseY,
      halfWidth,
      mudPatch,
      splitFactor,
      splitOffset,
      mudslideFactor,
      mudslideDirX,
    };
  }

  public sampleHeightRaw(
    x: number,
    z: number
  ): {
    height: number;
    surface: SurfaceType;
    mudiness: number;
    waterDepth: number;
    mudslidePushX: number;
    spec: TrailPointSpec;
    signedOffset: number;
    distFromActiveBranch: number;
  } {
    const spec = this.getTrailSpec(z);
    const d = x - spec.centerX;
    const absD = Math.abs(d);
    const hw = spec.halfWidth;

    const branchHalfWidth = lerp(
      hw,
      this.currentBiome === 'SINGULARITY' ? 7.8 : 16.5,
      spec.splitFactor
    );

    let distFromBranch = absD;
    let medianIslandBump = 0;
    let splitElevationBias = 0;
    if (spec.splitFactor > 0.02) {
      const dLeft = Math.abs(d + spec.splitOffset);
      const dRight = Math.abs(d - spec.splitOffset);
      distFromBranch = Math.min(dLeft, dRight);

      if (this.currentBiome === 'SINGULARITY') {
        // High-road vs Low-road split cliff paths separated by a sheer -42m chasm!
        const sideBlend = clamp(d / Math.max(2.0, spec.splitOffset * 0.45), -1, 1);
        splitElevationBias = -sideBlend * 2.8 * spec.splitFactor;
        if (absD < spec.splitOffset && distFromBranch > branchHalfWidth * 0.95) {
          const chasmMask =
            smoothstep(branchHalfWidth * 0.95, branchHalfWidth + 3.2, distFromBranch) *
            spec.splitFactor;
          medianIslandBump =
            -42.0 * chasmMask + ridge2D(x * 0.14, z * 0.14, 2) * 3.2 * chasmMask;
        }
      } else {
        const islandHalfW = Math.max(0.5, spec.splitOffset * 0.42);
        if (absD < islandHalfW * 1.4) {
          const islandMask =
            smoothstep(islandHalfW * 1.4, islandHalfW * 0.35, absD) * spec.splitFactor;
          medianIslandBump = islandMask * (2.1 + 0.55 * ridge2D(x * 0.16, z * 0.16, 2));
        }
      }
    }

    const bankSlope = clamp(-spec.curvature * 12.0, -0.09, 0.09);
    const bankHeight =
      this.currentBiome === 'GLACIAL_ICE'
        ? 0
        : d * bankSlope * smoothstep(hw * 1.35, hw * 0.25, absD);

    // Steep Hillside Cross-Camber during Flash-Flood Mudslide Sections!
    const mudslideCamberSlope = -spec.mudslideDirX * 0.135 * spec.mudslideFactor;
    const mudslideTiltHeight = clamp(d, -42, 42) * mudslideCamberSlope;
    const mudslidePushX =
      spec.mudslideFactor > 0.12 && absD < hw * 0.92
        ? spec.mudslideDirX * spec.mudslideFactor * 6.8
        : 0;

    // Sweeping Sand Dunes!
    const duneWaves =
      this.currentBiome === 'DESERT'
        ? Math.sin(x * 0.065 + z * 0.045) * 1.65 * smoothstep(4, 24, distFromBranch) +
          ridge2D(x * 0.032, z * 0.032, 2) * 1.35
        : this.currentBiome === 'SINGULARITY'
        ? Math.sin(x * 0.11 + z * 0.08) * 0.24 * smoothstep(branchHalfWidth, 2.0, distFromBranch)
        : 0;

    const microRough =
      this.currentBiome === 'GLACIAL_ICE'
        ? fbm2D(x * 0.15, z * 0.15, 2) * 0.015 // Mirror-smooth flat glacial ice sheet!
        : fbm2D(x * 0.22, z * 0.22, 3) * (this.currentBiome === 'SWAMP' ? 0.1 : 0.14) +
          noise2D(x * 1.0, z * 1.0) * 0.025;

    let cliffRelief = 0;
    let forestFlankBlend = smoothstep(hw * 0.92, hw + 38.0, absD);
    let outerHill = Math.pow(Math.max(0, absD - (hw + 22.0)) * 0.042, 1.6) * 7.5;
    let rootMounds =
      fbm2D(x * 0.045, z * 0.045, 3) * (2.4 * forestFlankBlend) +
      ridge2D(x * 0.075, z * 0.075, 2) * (1.4 * forestFlankBlend);

    if (this.currentBiome === 'GLACIAL_ICE') {
      forestFlankBlend = 0;
      outerHill = 0;
      rootMounds = 0;
      // Dead-flat frozen lake plane across the entire 68m center, rising into jagged snow-glacier walls outside!
      const glacierShoulder = smoothstep(34.0, 56.0, absD);
      cliffRelief =
        glacierShoulder * (18.5 + ridge2D(x * 0.065, z * 0.065, 3) * 7.5);
    } else if (this.currentBiome === 'GIANT_LOG') {
      forestFlankBlend = 0;
      outerHill = 0;
      rootMounds = 0;
      // Colossal curved cylindrical heartwood half-pipe floor + longitudinal wood grain & annular ring ribs!
      const clampedSpan = Math.min(absD, 46.0);
      const halfPipeBowl = Math.pow(Math.max(0, clampedSpan - 8.0) / 30.0, 2.35) * 16.5;
      const woodGrain = Math.sin(d * 0.85 + fbm2D(x * 0.12, z * 0.06, 2) * 1.6) * 0.09;
      const annularRib =
        Math.pow(Math.sin(z * 0.225) * 0.5 + 0.5, 4.0) * 0.24 * smoothstep(8.0, 28.0, absD);
      const outerBerm = smoothstep(39.0, 62.0, absD) * 12.0;
      cliffRelief = halfPipeBowl + woodGrain + annularRib + outerBerm;
    } else if (this.currentBiome === 'SINGULARITY') {
      forestFlankBlend = 0;
      outerHill = 0;
      rootMounds = 0;

      const distForward = Math.max(0, -z);
      const cliffActive = smoothstep(14, 46, distForward);
      if (absD >= spec.splitOffset * 0.95 && distFromBranch > branchHalfWidth * 0.95) {
        const brinkBlend =
          smoothstep(branchHalfWidth * 0.96, branchHalfWidth + 4.8, distFromBranch) * cliffActive;
        const wallSelector = Math.sin(z * 0.014 + 0.8);
        const isBenchWall =
          (wallSelector > 0.36 && d < 0) || (wallSelector < -0.36 && d > 0);
        const cragDetail =
          ridge2D(x * 0.085, z * 0.085, 3) * 4.6 + fbm2D(x * 0.18, z * 0.18, 2) * 1.8;

        if (isBenchWall) {
          // Sheer towering volcanic obsidian cliff wall rising +38m right beside the ledge!
          cliffRelief = (38.0 + cragDetail) * brinkBlend;
        } else {
          // Sheer -48m vertical precipice plunging into the canyon abyss + far canyon rim walls!
          const farRimRise = smoothstep(78.0, 142.0, absD) * 62.0;
          cliffRelief = (-48.0 + cragDetail * 0.75) * brinkBlend + farRimRise * cliffActive;
        }
      }
    }

    const localMud = clamp(
      spec.mudPatch *
        smoothstep(branchHalfWidth * 0.9, 0.0, distFromBranch) *
        smoothstep(0.32, 0.75, fbm2D(x * 0.11 + 5.1, z * 0.11, 2) * 0.5 + 0.5) +
        spec.mudslideFactor * 0.82,
      0,
      1
    );

    let height =
      spec.baseY +
      splitElevationBias +
      bankHeight +
      mudslideTiltHeight +
      duneWaves -
      localMud * 0.15 +
      medianIslandBump +
      cliffRelief +
      microRough * lerp(0.65, 1.5, forestFlankBlend) +
      forestFlankBlend * 1.2 +
      rootMounds +
      outerHill;

    let waterDepth = 0;
    const cIdx = Math.floor(-z / CHUNK_LENGTH);
    for (let ci = cIdx - 1; ci <= cIdx + 1; ci++) {
      const rec = this.activeChunks.get(ci);
      if (!rec) continue;
      if (rec.deadwoodData) {
        height += this.deadwoodBuilder.sampleGroundHeaveHeight(rec.deadwoodData, x, z);
      }
      for (const p of rec.puddles) {
        const nx = (x - p.x) / p.radiusX;
        const nz = (z - p.z) / p.radiusZ;
        const rSq = nx * nx + nz * nz;
        if (rSq < 1.0) {
          const basinProfile = Math.pow(1.0 - rSq, 1.3);
          height -= p.maxDepth * basinProfile;
          const depthHere = p.waterSurfaceY - height;
          if (depthHere > waterDepth) waterDepth = depthHere;
        }
      }
    }

    let surface: SurfaceType =
      this.currentBiome === 'GLACIAL_ICE'
        ? 'GLACIAL_ICE'
        : this.currentBiome === 'GIANT_LOG'
        ? 'HOLLOW_LOG'
        : this.currentBiome === 'DESERT' || this.currentBiome === 'SINGULARITY'
        ? 'DESERT_SAND'
        : 'PACKED_DIRT';
    if (waterDepth > 0.03) {
      surface = 'SHALLOW_WATER';
    } else if (spec.mudslideFactor > 0.25 && absD < hw * 0.9) {
      surface = 'MUDSLIDE';
    } else if (
      this.currentBiome !== 'GIANT_LOG' &&
      this.currentBiome !== 'GLACIAL_ICE' &&
      (absD > hw + 18.0 || medianIslandBump > 0.8)
    ) {
      surface = 'MOSS_ROCK';
    } else if (
      this.currentBiome !== 'DESERT' &&
      this.currentBiome !== 'SINGULARITY' &&
      this.currentBiome !== 'GIANT_LOG' &&
      this.currentBiome !== 'GLACIAL_ICE' &&
      localMud > 0.36
    ) {
      surface = 'WET_MUD';
    } else if (
      this.currentBiome !== 'DESERT' &&
      this.currentBiome !== 'GIANT_LOG' &&
      this.currentBiome !== 'GLACIAL_ICE' &&
      distFromBranch > branchHalfWidth * 0.78
    ) {
      surface = 'LOOSE_VERGE';
    }

    return {
      height,
      surface,
      mudiness: localMud,
      waterDepth: Math.max(0, waterDepth),
      mudslidePushX,
      spec,
      signedOffset: d,
      distFromActiveBranch: distFromBranch,
    };
  }

  private _sampleNorm = new THREE.Vector3();

  public sampleTerrain(x: number, z: number, outNormal?: THREE.Vector3): TerrainSample {
    const c = this.sampleHeightRaw(x, z);
    const eps = 0.36;
    const hR = this.sampleHeightRaw(x + eps, z).height;
    const hL = this.sampleHeightRaw(x - eps, z).height;
    const hF = this.sampleHeightRaw(x, z + eps).height;
    const hB = this.sampleHeightRaw(x, z - eps).height;

    const normal = outNormal ?? this._sampleNorm.clone();
    normal.set(hL - hR, 2 * eps, hB - hF).normalize();

    let friction = 1.08;
    let rollingResistance = 0.017;
    switch (c.surface) {
      case 'PACKED_DIRT':
        friction = lerp(1.1, 0.76, c.mudiness);
        rollingResistance = lerp(0.017, 0.048, c.mudiness);
        break;
      case 'MUDSLIDE':
        friction = 0.62; // Slick flowing hillside mudslide!
        rollingResistance = 0.058;
        break;
      case 'SHALLOW_WATER':
        friction = 0.8;
        rollingResistance = 0.052 + c.waterDepth * 0.18;
        break;
      case 'WET_MUD':
        friction = 0.72;
        rollingResistance = 0.062;
        break;
      case 'LOOSE_VERGE':
        friction = 0.94;
        rollingResistance = 0.028;
        break;
      case 'MOSS_ROCK':
        friction = 0.82;
        rollingResistance = 0.034;
        break;
      case 'HOLLOW_LOG':
      case 'CRISSCROSS_ROOT':
        friction = 1.04; // Great bark grip on top of giant crisscrossing roots!
        rollingResistance = 0.016;
        break;
      case 'DESERT_SAND':
        friction = this.currentBiome === 'SINGULARITY' ? 1.12 : 0.92;
        rollingResistance = this.currentBiome === 'SINGULARITY' ? 0.015 : 0.032;
        break;
      case 'GLACIAL_ICE':
        friction = 0.48; // Slippery glassy black-ice — exhilarating powerslides & drifts!
        rollingResistance = 0.008; // Ultra-fast glide across the flat frozen lake!
        break;
    }

    return {
      height: c.height,
      normal,
      surface: c.surface,
      friction,
      rollingResistance,
      mudiness: c.mudiness,
      waterDepth: c.waterDepth,
      mudslidePushX: c.mudslidePushX,
      trailCenterX: c.spec.centerX,
      trailHalfWidth: c.spec.halfWidth,
      signedOffset: c.signedOffset,
    };
  }

  /**
   * Samples elevated Static Crisscrossing Giant Root Bridges at (x, z) relative to referenceY!
   * Allows driving smoothly UP ONTO the 4.6m-wide crisscrossing roots AND driving UNDERNEATH elevated spans!
   */
  public sampleCrisscrossRootDeck(
    x: number,
    z: number,
    referenceY: number,
    bridges: CrisscrossRootBridge[],
    outNormal: THREE.Vector3
  ): number | null {
    let bestDeckY: number | null = null;

    for (const br of bridges) {
      if (z < br.minZ || z > br.maxZ) continue;
      for (const seg of br.segments) {
        const abx = seg.bx - seg.ax;
        const abz = seg.bz - seg.az;
        const lenSq = abx * abx + abz * abz + 1e-6;
        const t = clamp(((x - seg.ax) * abx + (z - seg.az) * abz) / lenSq, 0, 1);
        const cx = seg.ax + abx * t;
        const cz = seg.az + abz * t;
        const dx = x - cx;
        const dz = z - cz;
        const latDistSq = dx * dx + dz * dz;
        const hw = seg.deckHalfWidth;

        if (latDistSq < hw * hw) {
          const latDist = Math.sqrt(latDistSq);
          const crownDrop = Math.pow(latDist / hw, 2.2) * 0.28;
          const deckY = lerp(seg.ay, seg.by, t) + 0.18 - crownDrop;

          // Only snap wheels onto this root deck if the car/wheel is above its underside (allows driving underneath high arches!)
          if (referenceY >= deckY - seg.thickness && (bestDeckY === null || deckY > bestDeckY)) {
            bestDeckY = deckY;
            const slopeVecY = (seg.by - seg.ay) / Math.sqrt(lenSq);
            outNormal
              .set(
                (dx / (hw + 0.1)) * 0.28,
                1.0,
                (dz / (hw + 0.1)) * 0.28 - slopeVecY * 0.4
              )
              .normalize();
          }
        }
      }
    }

    return bestDeckY;
  }

  public updateStreaming(
    playerPos: THREE.Vector3,
    playerVel: THREE.Vector3,
    elapsedTime: number,
    dt = 1 / 60,
    vehiclePos: THREE.Vector3 = playerPos,
    vehicleSpeedMps = 0,
    passiveObserver = false,
    vehicleVel: THREE.Vector3 = playerVel,
    isBoosting = false,
    throttleInput = 0,
    steerInput = 0
  ): void {
    this.playerPosUniform.value.copy(playerPos);
    this.playerVelUniform.value.copy(playerVel);
    this.timeUniform.value = elapsedTime;

    if (this.activePortal) {
      const mat = this.activePortal.ringMesh.material as THREE.MeshBasicMaterial;
      mat.opacity = 0.32 + Math.sin(elapsedTime * 1.1) * 0.08;
    }

    const currentChunk = Math.floor(-playerPos.z / CHUNK_LENGTH);
    const minChunk = currentChunk - 1;
    const maxChunk = currentChunk + 3;

    for (const [idx, record] of this.activeChunks.entries()) {
      if (idx < minChunk || idx > maxChunk) {
        this.group.remove(record.group);
        record.terrainGeo.dispose();
        record.group.traverse((obj) => {
          if ((obj as THREE.Mesh).isMesh && obj.userData.uniqueGeo) {
            (obj as THREE.Mesh).geometry.dispose();
          }
        });
        this.activeChunks.delete(idx);
        for (const cb of this.onChunkRemovedCallbacks) cb(idx);
      }
    }

    for (let idx = minChunk; idx <= maxChunk; idx++) {
      if (!this.activeChunks.has(idx)) {
        this.buildChunk(idx);
      }
    }

    this.stepDynamicSetPieces(
      playerPos,
      playerVel,
      elapsedTime,
      dt,
      vehiclePos,
      vehicleSpeedMps,
      passiveObserver,
      vehicleVel,
      isBoosting,
      throttleInput,
      steerInput
    );
  }

  private stepDynamicSetPieces(
    playerPos: THREE.Vector3,
    playerVel: THREE.Vector3,
    elapsedTime: number,
    dt: number,
    vehiclePos: THREE.Vector3,
    vehicleSpeedMps: number,
    passiveObserver: boolean,
    vehicleVel: THREE.Vector3,
    isBoosting: boolean,
    throttleInput: number,
    steerInput: number
  ): void {
    const forwardSpeed = Math.max(0, -playerVel.z);

    for (const rec of this.activeChunks.values()) {
      // 1. Dynamic Falling Trees
      for (const ft of rec.fallingTrees) {
        if (ft.state === 'STANDING') {
          const dzAhead = playerPos.z - ft.basePos.z;
          const horizDist = Math.hypot(playerPos.x - ft.basePos.x, dzAhead);
          const dynamicTrigger = clamp(ft.triggerDistance + forwardSpeed * 1.15, 44, 92);
          if (dzAhead > 8 && dzAhead < dynamicTrigger && horizDist < dynamicTrigger + 22) {
            ft.state = 'FALLING';
            ft.angularVel = 0.24;
            if (this.onTreeStartFalling) this.onTreeStartFalling(ft.basePos.clone());
          }
        } else if (ft.state === 'FALLING') {
          const gravTorque = ((1.5 * 9.81) / 14.5) * Math.sin(Math.max(0.14, ft.currentTilt));
          ft.angularVel += gravTorque * dt;
          ft.currentTilt += ft.angularVel * dt;

          if (ft.currentTilt >= ft.maxTilt) {
            ft.currentTilt = ft.maxTilt;
            ft.state = 'FALLEN';
            ft.angularVel = 0;
            const impactPt = new THREE.Vector3(
              ft.basePos.x + Math.cos(ft.fallDirAngle) * (ft.trunkLength * 0.55),
              ft.basePos.y + 0.8,
              ft.basePos.z - Math.sin(ft.fallDirAngle) * (ft.trunkLength * 0.55)
            );
            if (this.onTreeSlamImpact) this.onTreeSlamImpact(impactPt);
          }

          ft.pivotGroup.rotation.set(0, ft.fallDirAngle, -ft.currentTilt);
          this.syncFallingTreeObstacle(ft);
        }
      }

      // 2. Tumbling Mudslide / Canyon Rolling Boulders!
      for (const rb of rec.rollingBoulders) {
        rb.position.x += rb.velocity.x * dt;
        rb.position.z += rb.velocity.z * dt;
        const groundY = this.sampleHeightRaw(rb.position.x, rb.position.z).height;
        const bounce = Math.abs(Math.sin(elapsedTime * 5.2 + rb.id)) * 0.45;
        rb.position.y = groundY + rb.radius * 0.85 + bounce;

        rb.mesh.position.copy(rb.position);
        rb.mesh.rotation.z -= rb.dirX * (rb.velocity.length() / rb.radius) * dt;
        rb.mesh.rotation.x += 0.8 * dt;

        rb.obstacle.position.copy(rb.position);
        rb.obstacle.position.y = groundY;

        // Loop boulder cleanly from uphill ridge once it rolls all the way past the far flank!
        if (Math.abs(rb.position.x - rb.spawnX) > rb.maxTravelX) {
          rb.position.x = rb.spawnX;
          rb.position.z = rb.spawnZ;
        }
      }

      // 3. Desert Swirling Dust Devils!
      for (const dd of rec.dustDevils) {
        dd.position.x = dd.baseX + Math.sin(elapsedTime * 0.65 + dd.phase) * 22.0;
        dd.position.z = dd.baseZ + Math.cos(elapsedTime * 0.48 + dd.phase) * 14.0;
        dd.position.y = this.sampleHeightRaw(dd.position.x, dd.position.z).height - 0.5;
        dd.mesh.position.copy(dd.position);
        dd.mesh.rotation.y = elapsedTime * 4.5;
      }

      // 4. Fuel Jerrycan Pickups
      for (const fp of rec.fuelPickups) {
        if (fp.collectedCooldown > 0) {
          fp.collectedCooldown = Math.max(0, fp.collectedCooldown - dt);
          fp.mesh.visible = fp.collectedCooldown <= 0;
        }
      }

      // 5. DEADWOOD: Live Subterranean Erupting Roots, Ground-Heave Ridges & Uprooted Dead Snags!
      if (rec.deadwoodData) {
        this.deadwoodBuilder.stepChunkHazards(
          rec.group,
          rec.deadwoodData,
          dt,
          playerPos,
          vehiclePos,
          vehicleSpeedMps,
          passiveObserver
        );
      }

      // 6. DESERT SHACK VILLAGE: Spinning Wind-Pumps, Collapsing Water Towers, Rolling Oil Drums & Swinging Gates!
      if (rec.desertVillageData) {
        this.desertVillageBuilder.stepChunkHazards(
          rec.desertVillageData,
          dt,
          elapsedTime,
          playerPos,
          vehiclePos,
          (x, z) => this.sampleHeightRaw(x, z).height
        );
      }

      // 7. VITRIFIED SINGULARITY DESERT: Levitating Obsidian Monoliths, Gyro-Rings & Wind-Hurled Storm Boulders!
      if (rec.singularityData) {
        this.singularityBuilder.stepChunkHazards(
          rec.singularityData,
          elapsedTime,
          dt,
          (z) => {
            const sp = this.getTrailSpec(z);
            return { centerX: sp.centerX, halfWidth: sp.halfWidth };
          },
          (x, z) => this.sampleHeightRaw(x, z).height
        );
      }

      // 8. COLOSSAL HOLLOW LOG: Worms, Wall-to-Wall Sticky Web Snares, Growing Mushrooms & Sap!
      if (rec.giantLogData) {
        this.giantLogBuilder.stepChunkHazards(
          rec.giantLogData,
          elapsedTime,
          dt,
          playerPos,
          vehiclePos,
          vehicleVel,
          throttleInput,
          steerInput,
          isBoosting,
          passiveObserver
        );
      }

      // 9. SUB-ZERO GLACIAL ICE: Variable-Rate Traveling Bottom Fractures & Falling Cave Icicles!
      if (rec.glacialIceData) {
        this.glacialIceBuilder.stepChunkHazards(
          rec.group,
          rec.glacialIceData,
          elapsedTime,
          dt,
          playerPos,
          vehiclePos,
          vehicleVel,
          vehicleSpeedMps,
          passiveObserver,
          (x, z) => this.sampleHeightRaw(x, z).height
        );
      }
    }

    this.singularityBuilder.stepGlobalStormFx(
      this.currentBiome === 'SINGULARITY',
      playerPos,
      dt,
      (x, z) => this.sampleHeightRaw(x, z).height
    );
    this.giantLogBuilder.stepGlobalLogAtmosphere(
      this.currentBiome === 'GIANT_LOG',
      playerPos,
      vehiclePos,
      vehicleVel,
      throttleInput,
      steerInput,
      isBoosting,
      passiveObserver,
      dt
    );
    this.glacialIceBuilder.stepGlobalIceAtmosphere(
      this.currentBiome === 'GLACIAL_ICE',
      playerPos,
      dt
    );
  }

  /**
   * Checks if a vehicle collects a glowing Bio-Fuel Canister (+38% Fuel + Boost)!
   */
  public tryCollectFuelPickup(pos: THREE.Vector3, radius = 3.2): boolean {
    for (const rec of this.activeChunks.values()) {
      for (const fp of rec.fuelPickups) {
        if (fp.collectedCooldown > 0) continue;
        if (pos.distanceToSquared(fp.position) < radius * radius) {
          fp.collectedCooldown = 12.0;
          fp.mesh.visible = false;
          return true;
        }
      }
    }
    return false;
  }

  /**
   * Returns lateral/swirl wind force if inside a Desert Dust Devil vortex!
   */
  public sampleDustDevilForce(pos: THREE.Vector3, outForce: THREE.Vector3): boolean {
    outForce.set(0, 0, 0);
    if (this.currentBiome !== 'DESERT' && this.currentBiome !== 'SINGULARITY') return false;
    for (const rec of this.activeChunks.values()) {
      for (const dd of rec.dustDevils) {
        const dx = pos.x - dd.position.x;
        const dz = pos.z - dd.position.z;
        const dist = Math.hypot(dx, dz);
        if (dist < dd.radius && dist > 0.2) {
          const strength = (1 - dist / dd.radius) * 8500;
          // Tangential swirl + slight upward lift!
          outForce.set((-dz / dist) * strength, strength * 0.25, (dx / dist) * strength);
          return true;
        }
      }
    }
    if (this.currentBiome === 'SINGULARITY') {
      // Rhythmic Martian sandstorm crosswind gusts across exposed cliff ridges!
      const gustWave = Math.sin(this.timeUniform.value * 1.45 + pos.z * 0.035);
      if (Math.abs(gustWave) > 0.72) {
        outForce.set(gustWave * 950, 0, 0);
        return true;
      }
    }
    return false;
  }

  private syncFallingTreeObstacle(ft: FallingTreeEvent): void {
    const sinT = Math.sin(ft.currentTilt);
    const cosT = Math.cos(ft.currentTilt);
    const axX = Math.cos(ft.fallDirAngle);
    const axZ = -Math.sin(ft.fallDirAngle);

    if (sinT < 0.55) {
      ft.dynamicObstacle.kind = 'buttress_tree';
      ft.dynamicObstacle.position.copy(ft.basePos);
      ft.dynamicObstacle.radius = 0.72;
      ft.dynamicObstacle.height = 18.0;
      ft.dynamicObstacle.halfLength = undefined;
    } else {
      const midDist = ft.trunkLength * 0.46 * sinT;
      const midHeight = ft.basePos.y + ft.trunkLength * 0.46 * cosT + 0.32;
      ft.dynamicObstacle.kind = 'fallen_log';
      ft.dynamicObstacle.position.set(
        ft.basePos.x + axX * midDist,
        midHeight,
        ft.basePos.z + axZ * midDist
      );
      ft.dynamicObstacle.axisX = axX;
      ft.dynamicObstacle.axisZ = axZ;
      ft.dynamicObstacle.halfLength = ft.trunkLength * 0.46 * sinT;
      ft.dynamicObstacle.radius = ft.trunkRadius;
      ft.dynamicObstacle.height = midHeight + ft.trunkRadius;
      ft.dynamicObstacle.underClearanceY = midHeight - ft.trunkRadius;
    }
  }

  public resetAround(z: number): void {
    for (const [idx, record] of this.activeChunks.entries()) {
      this.group.remove(record.group);
      record.terrainGeo.dispose();
      record.group.traverse((obj) => {
        if ((obj as THREE.Mesh).isMesh && obj.userData.uniqueGeo) {
          (obj as THREE.Mesh).geometry.dispose();
        }
      });
      for (const cb of this.onChunkRemovedCallbacks) cb(idx);
    }
    this.activeChunks.clear();
    this.updateStreaming(new THREE.Vector3(0, 0, z), new THREE.Vector3(), this.timeUniform.value, 0);
  }

  public getNearbyObstacles(zMin: number, zMax: number, out: WorldObstacle[] = []): WorldObstacle[] {
    out.length = 0;
    for (const record of this.activeChunks.values()) {
      for (const obs of record.obstacles) {
        if (obs.position.z >= zMin - 16 && obs.position.z <= zMax + 16) {
          out.push(obs);
        }
      }
    }
    return out;
  }

  public getNearbyHollowLogs(zMin: number, zMax: number, out: HollowLogTunnel[] = []): HollowLogTunnel[] {
    out.length = 0;
    for (const record of this.activeChunks.values()) {
      for (const hl of record.hollowLogs) {
        if (hl.center.z + hl.halfLength >= zMin - 6 && hl.center.z - hl.halfLength <= zMax + 6) {
          out.push(hl);
        }
      }
    }
    return out;
  }

  public getNearbyRootBridges(
    zMin: number,
    zMax: number,
    out: CrisscrossRootBridge[] = []
  ): CrisscrossRootBridge[] {
    out.length = 0;
    for (const record of this.activeChunks.values()) {
      for (const br of record.rootBridges) {
        if (br.maxZ >= zMin - 4 && br.minZ <= zMax + 4) {
          out.push(br);
        }
      }
    }
    return out;
  }

  private buildChunk(chunkIndex: number): void {
    const zStart = -chunkIndex * CHUNK_LENGTH;
    const biome = this.currentBiome;
    const chunkGroup = new THREE.Group();
    const obstacles: WorldObstacle[] = [];
    const hollowLogs: HollowLogTunnel[] = [];
    const rootBridges: CrisscrossRootBridge[] = [];
    const puddles: WaterPuddleZone[] = [];
    const fallingTrees: FallingTreeEvent[] = [];
    const rollingBoulders: RollingBoulder[] = [];
    const dustDevils: DustDevil[] = [];
    const fuelPickups: FuelPickup[] = [];
    const anchors: CanopyAnchor[] = [];
    const rootAnchors: RootAnchor[] = [];

    const biomeSalt =
      biome === 'SWAMP'
        ? 7719
        : biome === 'DESERT'
        ? 31337
        : biome === 'DEADWOOD'
        ? 18443
        : biome === 'SINGULARITY'
        ? 55987
        : biome === 'GIANT_LOG'
        ? 88421
        : biome === 'GLACIAL_ICE'
        ? 63913
        : 0;
    const rng = mulberry32(((chunkIndex + 4096 + biomeSalt) * 2654435761) >>> 0);
    const isSafeZone = (z: number) => Math.abs(z) < 28;

    const chunkRecord: ChunkRecord = {
      group: chunkGroup,
      terrainGeo: new THREE.BufferGeometry(),
      obstacles,
      hollowLogs,
      rootBridges,
      puddles,
      fallingTrees,
      rollingBoulders,
      dustDevils,
      fuelPickups,
      anchors,
      rootAnchors,
    };
    this.activeChunks.set(chunkIndex, chunkRecord);

    // A. NATURAL WATER PUDDLES / BLACKWATER LAGOONS / GLOWING TURQUOISE SALT-BRINE POOLS!
    if (chunkIndex >= 0 && biome !== 'DESERT' && biome !== 'GLACIAL_ICE') {
      if (biome === 'SINGULARITY') {
        // Glowing turquoise mineral salt-brine pools in wide plateau hollows & deep canyon floor!
        for (let p = 0; p < 3; p++) {
          const zPud = zStart - ((p + 0.22 + rng() * 0.45) / 3) * CHUNK_LENGTH;
          if (isSafeZone(zPud)) continue;
          const spec = this.getTrailSpec(zPud);
          const onCanyonFloor = p > 0 || spec.halfWidth < 13.5 || spec.splitFactor > 0.2;
          const side = p % 2 === 0 ? -1 : 1;
          const xPud = onCanyonFloor
            ? spec.centerX + side * lerp(spec.halfWidth + 18.0, spec.halfWidth + 46.0, rng())
            : spec.centerX + (rng() - 0.5) * (spec.halfWidth * 0.55);
          const baseSample = this.sampleHeightRaw(xPud, zPud);
          const rx = onCanyonFloor ? lerp(14.0, 26.0, rng()) : lerp(6.5, 10.5, rng());
          const rz = onCanyonFloor ? lerp(18.0, 34.0, rng()) : lerp(8.0, 13.0, rng());
          const maxDepth = onCanyonFloor ? 0.35 : 0.16;
          const waterY = baseSample.height - 0.02;

          if (!onCanyonFloor) {
            puddles.push({
              x: xPud,
              z: zPud,
              radiusX: rx,
              radiusZ: rz,
              waterSurfaceY: waterY,
              maxDepth,
            });
          }

          const brineMesh = new THREE.Mesh(
            this.puddleDiscGeo,
            this.singularityBuilder.saltBrineWaterMat
          );
          brineMesh.position.set(xPud, waterY, zPud);
          brineMesh.scale.set(rx, 1, rz);
          brineMesh.renderOrder = 2;
          chunkGroup.add(brineMesh);
        }
      } else {
        const puddleCount = biome === 'SWAMP' ? 8 : biome === 'DEADWOOD' ? 2 : 3;
        for (let p = 0; p < puddleCount; p++) {
          const zPud = zStart - ((p + 0.25 + rng() * 0.45) / puddleCount) * CHUNK_LENGTH;
          if (isSafeZone(zPud)) continue;

          const spec = this.getTrailSpec(zPud);
          const xPud =
            spec.centerX + (rng() - 0.5) * (spec.halfWidth * (biome === 'SWAMP' ? 1.45 : 0.85));
          const baseSample = this.sampleHeightRaw(xPud, zPud);
          const rx = biome === 'SWAMP' ? lerp(14.0, 28.0, rng()) : lerp(6.0, 11.5, rng());
          const rz = biome === 'SWAMP' ? lerp(16.0, 32.0, rng()) : lerp(8.0, 15.5, rng());
          const maxDepth = biome === 'SWAMP' ? lerp(0.28, 0.42, rng()) : lerp(0.18, 0.28, rng());
          const waterY = baseSample.height - 0.03;

          puddles.push({
            x: xPud,
            z: zPud,
            radiusX: rx,
            radiusZ: rz,
            waterSurfaceY: waterY,
            maxDepth,
          });

          const wMat =
            biome === 'GIANT_LOG'
              ? this.giantLogBuilder.sapAmberWaterMat
              : biome === 'SWAMP' || biome === 'DEADWOOD'
              ? this.swampBlackwaterMat
              : this.waterMat;
          const waterMesh = new THREE.Mesh(this.puddleDiscGeo, wMat);
          waterMesh.position.set(xPud, waterY, zPud);
          waterMesh.scale.set(rx, 1, rz);
          waterMesh.renderOrder = 2;
          chunkGroup.add(waterMesh);
        }
      }
    }

    // 1. VAST 320m-WIDE SPACIOUS TERRAIN MESH
    const cols = CHUNK_X_SEGMENTS + 1;
    const rows = CHUNK_Z_SEGMENTS + 1;
    const vertCount = cols * rows;
    const positions = new Float32Array(vertCount * 3);
    const colors = new Float32Array(vertCount * 3);
    const uvs = new Float32Array(vertCount * 2);
    const indices = new Uint32Array(CHUNK_X_SEGMENTS * CHUNK_Z_SEGMENTS * 6);

    const colDirt =
      biome === 'GLACIAL_ICE'
        ? new THREE.Color(0x52a8d6) // Glassy translucent sapphire-cyan flat lake ice!
        : biome === 'GIANT_LOG'
        ? new THREE.Color(0x7c4829) // Warm ancient redwood/cedar heartwood!
        : biome === 'SINGULARITY'
        ? new THREE.Color(0xefd0ae) // Pearl-champagne rippled silica sand crown!
        : biome === 'DESERT'
        ? new THREE.Color(0xdca86e) // Sun-baked golden desert sand & packed caliche street
        : biome === 'SWAMP'
        ? new THREE.Color(0x3a3b2c) // Damp dark bayou silt
        : biome === 'DEADWOOD'
        ? new THREE.Color(0x838476) // Weathered grey-olive Deadwood track (#838476)
        : new THREE.Color(0x8b6745);
    const colRut =
      biome === 'GLACIAL_ICE'
        ? new THREE.Color(0x8ee0fa) // Frosted pale-turquoise crystal ice veins!
        : biome === 'GIANT_LOG'
        ? new THREE.Color(0x9f6438) // Fibrous golden sapwood grain!
        : biome === 'SINGULARITY'
        ? new THREE.Color(0xd9875e) // Rose-gold wind-rippled silica dune tracks!
        : biome === 'DESERT'
        ? new THREE.Color(0xbd7d46) // Dusty terracotta tire ruts
        : biome === 'SWAMP'
        ? new THREE.Color(0x282b20)
        : biome === 'DEADWOOD'
        ? new THREE.Color(0x636558)
        : new THREE.Color(0x6d4b2f);
    const colMud =
      biome === 'GLACIAL_ICE'
        ? new THREE.Color(0x245e85) // Deep sub-surface abyssal blue ice
        : biome === 'GIANT_LOG'
        ? new THREE.Color(0x344622) // Damp interior emerald moss & rot pockets
        : biome === 'SINGULARITY'
        ? new THREE.Color(0x8a3a24) // Martian iron-oxide rust strata
        : biome === 'DESERT'
        ? new THREE.Color(0x9e6336) // Sun-cracked dry red clay
        : biome === 'SWAMP'
        ? new THREE.Color(0x161d17) // Blackwater peat
        : biome === 'DEADWOOD'
        ? new THREE.Color(0x383a2c) // Deadwood dark mud hollow (#383a2c)
        : new THREE.Color(0x362518);
    const colVerge =
      biome === 'GLACIAL_ICE'
        ? new THREE.Color(0xdff4fc) // Wind-packed powder snow bordering the flat ice!
        : biome === 'GIANT_LOG'
        ? new THREE.Color(0x4b3822) // Mossy curved inner sapwood bank!
        : biome === 'SINGULARITY'
        ? new THREE.Color(0x1c1726) // Fractured black volcanic obsidian cliff brink!
        : biome === 'DESERT'
        ? new THREE.Color(0xc27b47) // Warm red-ochre canyon talus
        : biome === 'SWAMP'
        ? new THREE.Color(0x2b4528)
        : biome === 'DEADWOOD'
        ? new THREE.Color(0x55594c) // Deadwood ashen olive earth (#55594c)
        : new THREE.Color(0x3d5c28);
    const colOuter =
      biome === 'GLACIAL_ICE'
        ? new THREE.Color(0x89cbe8) // Towering turquoise glacier shoulders!
        : biome === 'GIANT_LOG'
        ? new THREE.Color(0x2c1d13) // Dark ancient outer bark wall!
        : biome === 'SINGULARITY'
        ? new THREE.Color(0x15121d) // Deep volcanic obsidian canyon escarpment!
        : biome === 'DESERT'
        ? new THREE.Color(0xa85832) // Terracotta red-rock canyon wall
        : biome === 'SWAMP'
        ? new THREE.Color(0x18291b)
        : biome === 'DEADWOOD'
        ? new THREE.Color(0x41453b)
        : new THREE.Color(0x273c1d);
    const colSaltFloor = new THREE.Color(0xbfd8d4);
    const tmpColor = new THREE.Color();

    let ptr = 0;
    let uvPtr = 0;
    for (let rz = 0; rz < rows; rz++) {
      const tz = rz / CHUNK_Z_SEGMENTS;
      const z = zStart - tz * CHUNK_LENGTH;
      const centerSpec = this.getTrailSpec(z);

      for (let cx = 0; cx < cols; cx++) {
        const uNorm = (cx / CHUNK_X_SEGMENTS) * 2 - 1;
        const shapedOffset =
          (0.42 * uNorm + 0.58 * Math.pow(Math.abs(uNorm), 2.0) * Math.sign(uNorm)) *
          CORRIDOR_HALF_SPAN;
        const x = centerSpec.centerX + shapedOffset;

        const s = this.sampleHeightRaw(x, z);
        positions[ptr] = x;
        positions[ptr + 1] = s.height;
        positions[ptr + 2] = z;

        uvs[uvPtr] = x * 0.2;
        uvs[uvPtr + 1] = z * 0.2;
        uvPtr += 2;

        const absD = Math.abs(s.signedOffset);
        const hw = centerSpec.halfWidth;
        const branchW = lerp(
          hw,
          biome === 'SINGULARITY' ? 7.8 : 16.5,
          centerSpec.splitFactor
        );
        const dBr = s.distFromActiveBranch;

        const vergeMask = smoothstep(branchW * 0.72, branchW * 1.06, dBr);
        const outerMask = smoothstep(hw + 8.0, hw + 55.0, absD);
        const patchVar = fbm2D(x * 0.08, z * 0.08, 2) * 0.1;

        if (biome === 'SINGULARITY') {
          const elevDiff = s.height - centerSpec.baseY;
          const cliffWallMask = smoothstep(2.4, 9.5, Math.abs(elevDiff));
          const strataBand = Math.sin(s.height * 0.55 + z * 0.04) * 0.5 + 0.5;
          const duneRipple = Math.sin(x * 0.45 + z * 0.32) * 0.5 + 0.5;

          // Pearl-champagne & rose-gold rippled silica dunes on the cliff crown!
          tmpColor.copy(colDirt).lerp(colRut, duneRipple * 0.55);
          // Fractured black volcanic obsidian + Martian iron-rust cliff strata on steep walls!
          tmpColor.lerp(colVerge, Math.max(vergeMask * 0.92, cliffWallMask));
          tmpColor.lerp(colMud, cliffWallMask * smoothstep(0.62, 0.92, strataBand) * 0.55);
          // Bleached turquoise-mineral salt crust down in the deep canyon floor!
          if (elevDiff < -34.0) {
            tmpColor.lerp(colSaltFloor, smoothstep(-34.0, -45.0, elevDiff) * 0.58);
          }
          tmpColor.offsetHSL(0, 0, patchVar * 0.65);
        } else {
          tmpColor.copy(colDirt).lerp(colRut, smoothstep(branchW * 0.5, 0, dBr) * 0.45);
          tmpColor.lerp(colMud, clamp(s.mudiness * 0.88, 0, 1));
          tmpColor.lerp(colVerge, vergeMask * 0.85);
          tmpColor.lerp(colOuter, outerMask);
          tmpColor.offsetHSL(0, 0, patchVar);
        }

        colors[ptr] = tmpColor.r;
        colors[ptr + 1] = tmpColor.g;
        colors[ptr + 2] = tmpColor.b;
        ptr += 3;
      }
    }

    let idxPtr = 0;
    for (let rz = 0; rz < CHUNK_Z_SEGMENTS; rz++) {
      for (let cx = 0; cx < CHUNK_X_SEGMENTS; cx++) {
        const a = rz * cols + cx;
        const b = a + 1;
        const c = a + cols;
        const d = c + 1;
        indices[idxPtr++] = a;
        indices[idxPtr++] = b;
        indices[idxPtr++] = c;
        indices[idxPtr++] = b;
        indices[idxPtr++] = d;
        indices[idxPtr++] = c;
      }
    }

    const terrainGeo = chunkRecord.terrainGeo;
    terrainGeo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    terrainGeo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
    terrainGeo.setAttribute('uv', new THREE.BufferAttribute(uvs, 2));
    terrainGeo.setIndex(new THREE.BufferAttribute(indices, 1));
    terrainGeo.computeVertexNormals();

    const terrainMesh = new THREE.Mesh(terrainGeo, this.terrainMat);
    terrainMesh.receiveShadow = true;
    chunkGroup.add(terrainMesh);

    // B. FLASH-FLOOD HILLSIDE MUDSLIDE & CANYON ROCKSLIDE BOULDER ZONES!
    const midZ = zStart - CHUNK_LENGTH * 0.5;
    const midSpec = this.getTrailSpec(midZ);
    if (
      chunkIndex >= 0 &&
      biome !== 'DEADWOOD' &&
      (midSpec.mudslideFactor > 0.2 || (biome === 'DESERT' && chunkIndex % 2 === 1))
    ) {
      const dirX = midSpec.mudslideDirX;
      if (biome !== 'DESERT') {
        const slideSheet = new THREE.Mesh(this.mudslideSheetGeo, this.mudslideFlowMat);
        const groundMid = this.sampleHeightRaw(midSpec.centerX, midZ).height;
        slideSheet.position.set(midSpec.centerX, groundMid + 0.08, midZ);
        slideSheet.rotation.z = -dirX * 0.132;
        if (dirX < 0) slideSheet.scale.x = -1;
        slideSheet.renderOrder = 1;
        chunkGroup.add(slideSheet);
      }

      const boulderCount = biome === 'DESERT' ? 2 : 3;
      for (let b = 0; b < boulderCount; b++) {
        const bz = zStart - (0.22 + b * 0.27) * CHUNK_LENGTH;
        const bSpec = this.getTrailSpec(bz);
        const spawnX = bSpec.centerX - dirX * (bSpec.halfWidth + 8.0 + b * 9.0);
        const rad = lerp(1.65, 2.45, rng());
        const bMat = biome === 'DESERT' ? this.desertRockMat : this.rockMat;
        const bMesh = new THREE.Mesh(this.boulderGeos[b % 3], bMat);
        bMesh.scale.setScalar(rad);
        bMesh.castShadow = true;
        chunkGroup.add(bMesh);

        const gY = this.sampleHeightRaw(spawnX, bz).height;
        const bPos = new THREE.Vector3(spawnX + dirX * (b * 18.0), gY + rad, bz);
        const speedX = dirX * lerp(9.5, 14.5, rng());
        const obs: WorldObstacle = {
          id: this.nextObstacleId++,
          chunkIndex,
          kind: 'rolling_boulder',
          position: bPos.clone(),
          radius: rad * 0.9,
          height: rad * 2.0,
          canShearVine: true,
        };
        obstacles.push(obs);

        rollingBoulders.push({
          id: obs.id,
          mesh: bMesh,
          position: bPos,
          velocity: new THREE.Vector3(speedX, 0, -lerp(1.0, 3.5, rng())),
          radius: rad,
          dirX,
          spawnX: bSpec.centerX - dirX * (bSpec.halfWidth + 12.0),
          spawnZ: bz,
          maxTravelX: (bSpec.halfWidth + 16.0) * 2.0,
          obstacle: obs,
        });
      }
    }

    const boulderTransforms: Array<{ geoIdx: number; matrix: THREE.Matrix4 }> = [];
    const shearTransforms: THREE.Matrix4[] = [];
    const cactusTransforms: THREE.Matrix4[] = [];
    const midTreeTransforms: THREE.Matrix4[] = [];
    const titanTreeTransforms: THREE.Matrix4[] = [];
    const titanCrownTransforms: THREE.Matrix4[] = [];
    const highLianaTransforms: THREE.Matrix4[] = [];
    const canopyClusters: THREE.Matrix4[] = [];
    const sunShaftTransforms: THREE.Matrix4[] = [];
    const dummy = new THREE.Object3D();

    // C. BIOME SIGNATURE ARCHITECTURES!
    //    - SWAMP: Colossal Gothic Mangrove Stilt-Root Cathedrals (weave-through arched roots, zero bugs!)
    //    - FOREST: Giant Ground Root Bridges
    //    - DEADWOOD: Dead Timberland + Live Subterranean Erupting Roots, Ground-Heaves & Uprooted Snags!
    //    - DESERT: Gritty Badlands Shack Village + Collapsing Water Towers, Rolling Oil Drums & Swinging Gates!
    if (biome === 'SWAMP' && chunkIndex >= 0) {
      const lateralSlots = [-19.0, 17.5, -6.5, 22.0];
      for (let m = 0; m < 4; m++) {
        const mz = zStart - (0.14 + m * 0.23) * CHUNK_LENGTH;
        if (mz > -24) continue;
        const mSpec = this.getTrailSpec(mz);
        const mx = mSpec.centerX + lateralSlots[(m + chunkIndex) % lateralSlots.length] + (rng() - 0.5) * 6.0;
        this.spawnMangroveCathedralTree(
          chunkGroup,
          obstacles,
          canopyClusters,
          highLianaTransforms,
          chunkIndex,
          mx,
          mz,
          rng,
          lerp(0.95, 1.22, rng())
        );
      }
    } else if (biome === 'FOREST' && chunkIndex >= 0 && chunkIndex % 2 === 1) {
      const zEntry = zStart - 0.22 * CHUNK_LENGTH;
      const zExit = zEntry - 62.0;
      if (zEntry < -30) {
        const s0 = this.getTrailSpec(zEntry);
        const sMid = this.getTrailSpec((zEntry + zExit) * 0.5);
        const s1 = this.getTrailSpec(zExit);
        const x0 = s0.centerX - 18.0;
        const xMid = sMid.centerX;
        const x1 = s1.centerX + 18.0;
        const y0 = this.sampleHeightRaw(x0, zEntry).height - 0.35;
        const y1 = this.sampleHeightRaw(x1, zExit).height - 0.35;
        const archPeakY = this.sampleHeightRaw(xMid, (zEntry + zExit) * 0.5).height + 3.4;

        const curve = new THREE.CatmullRomCurve3([
          new THREE.Vector3(x0, y0, zEntry),
          new THREE.Vector3(lerp(x0, xMid, 0.5), lerp(y0, archPeakY, 0.68), lerp(zEntry, zExit, 0.25)),
          new THREE.Vector3(xMid, archPeakY, (zEntry + zExit) * 0.5),
          new THREE.Vector3(lerp(xMid, x1, 0.5), lerp(archPeakY, y1, 0.32), lerp(zEntry, zExit, 0.75)),
          new THREE.Vector3(x1, y1, zExit),
        ]);
        const built = this.buildCrisscrossRootBridge(chunkIndex, curve, 2.2);
        chunkGroup.add(built.mesh);
        rootBridges.push(built.bridge);
      }
    } else if (biome === 'DEADWOOD') {
      chunkRecord.deadwoodData = this.deadwoodBuilder.populateDeadwoodChunk(
        chunkGroup,
        chunkIndex,
        zStart,
        CHUNK_LENGTH,
        CORRIDOR_HALF_SPAN,
        rng,
        (x, z) => this.sampleHeightRaw(x, z).height,
        (z) => {
          const sp = this.getTrailSpec(z);
          return { centerX: sp.centerX, halfWidth: sp.halfWidth };
        },
        obstacles,
        rootBridges,
        () => this.nextObstacleId++,
        () => this.nextBridgeId++
      );
    } else if (biome === 'DESERT') {
      chunkRecord.desertVillageData = this.desertVillageBuilder.populateDesertVillageChunk(
        chunkGroup,
        chunkIndex,
        zStart,
        CHUNK_LENGTH,
        rng,
        (x, z) => this.sampleHeightRaw(x, z).height,
        (z) => {
          const sp = this.getTrailSpec(z);
          return { centerX: sp.centerX, halfWidth: sp.halfWidth, slopeX: sp.slopeX };
        },
        obstacles,
        rootBridges,
        () => this.nextObstacleId++,
        () => this.nextBridgeId++
      );

      if (chunkIndex >= 0) {
        // Natural dusty desert whirlwinds along canyon outskirts
        for (let d = 0; d < 2; d++) {
          const dz = zStart - (0.28 + d * 0.45) * CHUNK_LENGTH;
          const ds = this.getTrailSpec(dz);
          const dx = ds.centerX + (rng() - 0.5) * (ds.halfWidth * 0.85);
          const dy = this.sampleHeightRaw(dx, dz).height;
          const dMesh = new THREE.Mesh(this.dustDevilGeo, this.dustDevilMat);
          dMesh.position.set(dx, dy, dz);
          chunkGroup.add(dMesh);
          dustDevils.push({
            mesh: dMesh,
            position: new THREE.Vector3(dx, dy, dz),
            baseX: dx,
            baseZ: dz,
            radius: 9.5,
            phase: rng() * Math.PI * 2,
          });
        }
      }
    } else if (biome === 'SINGULARITY') {
      chunkRecord.singularityData = this.singularityBuilder.populateSingularityChunk(
        chunkGroup,
        chunkIndex,
        zStart,
        CHUNK_LENGTH,
        rng,
        (x, z) => this.sampleHeightRaw(x, z).height,
        (z) => {
          const sp = this.getTrailSpec(z);
          return {
            centerX: sp.centerX,
            halfWidth: sp.halfWidth,
            slopeX: sp.slopeX,
            baseY: sp.baseY,
            splitFactor: sp.splitFactor,
            splitOffset: sp.splitOffset,
          };
        },
        obstacles,
        () => this.nextObstacleId++
      );

      if (chunkIndex >= 0) {
        // Whipping Martian Sandstorm Dust Vortices sweeping across the cliff ledges!
        for (let d = 0; d < 3; d++) {
          const dz = zStart - (0.2 + d * 0.31) * CHUNK_LENGTH;
          const ds = this.getTrailSpec(dz);
          const dx = ds.centerX + (rng() - 0.5) * (ds.halfWidth * 0.82);
          const dy = this.sampleHeightRaw(dx, dz).height;
          const dMesh = new THREE.Mesh(this.dustDevilGeo, this.dustDevilMat);
          dMesh.position.set(dx, dy, dz);
          dMesh.scale.set(1.35, 1.55, 1.35);
          chunkGroup.add(dMesh);
          dustDevils.push({
            mesh: dMesh,
            position: new THREE.Vector3(dx, dy, dz),
            baseX: dx,
            baseZ: dz,
            radius: 10.5,
            phase: rng() * Math.PI * 2,
          });
        }
      }
    } else if (biome === 'GIANT_LOG') {
      chunkRecord.giantLogData = this.giantLogBuilder.populateGiantLogChunk(
        chunkGroup,
        chunkIndex,
        zStart,
        CHUNK_LENGTH,
        rng,
        (x, z) => this.sampleHeightRaw(x, z).height,
        (z) => {
          const sp = this.getTrailSpec(z);
          return {
            centerX: sp.centerX,
            halfWidth: sp.halfWidth,
            baseY: sp.baseY,
            slopeX: sp.slopeX,
          };
        },
        obstacles,
        rootBridges,
        () => this.nextObstacleId++,
        () => this.nextBridgeId++
      );

      // Hanging ecosystem moss-vines dangling down from the upper hollow-log ribs!
      if (chunkIndex >= 0) {
        for (let v = 0; v < 4; v++) {
          const vz = zStart - ((v + 0.2 + rng() * 0.5) / 4) * CHUNK_LENGTH;
          if (vz > -18) continue;
          const sp = this.getTrailSpec(vz);
          const vx = sp.centerX + (rng() - 0.5) * (sp.halfWidth * 1.1);
          const floorY = this.sampleHeightRaw(vx, vz).height;
          anchors.push({
            id: this.nextAnchorId++,
            chunkIndex,
            position: new THREE.Vector3(vx, floorY + lerp(13.5, 17.5, rng()), vz),
            branchTangent: new THREE.Vector3(1, 0, 0),
            trailCenterBelow: new THREE.Vector3(vx, floorY, vz),
            trailHalfWidth: sp.halfWidth,
            preferredArchetype: 'CURTAIN',
          });
        }
      }
    } else if (biome === 'GLACIAL_ICE') {
      chunkRecord.glacialIceData = this.glacialIceBuilder.populateGlacialIceChunk(
        chunkGroup,
        chunkIndex,
        zStart,
        CHUNK_LENGTH,
        rng,
        (x, z) => this.sampleHeightRaw(x, z).height,
        (z) => {
          const sp = this.getTrailSpec(z);
          return { centerX: sp.centerX, halfWidth: sp.halfWidth, baseY: sp.baseY };
        },
        obstacles,
        () => this.nextObstacleId++
      );
    }

    // F. GIANT DRIVE-THROUGH HOLLOW FALLEN LOG TUNNEL (Forest!)
    if (chunkIndex >= 0 && biome === 'FOREST') {
      const zLog = zStart - (0.5 + (rng() - 0.5) * 0.16) * CHUNK_LENGTH;
      if (zLog < -48) {
        const spec = this.getTrailSpec(zLog);
        const xLog =
          spec.splitFactor > 0.3
            ? spec.centerX - spec.splitOffset
            : spec.centerX + (chunkIndex % 2 === 0 ? -9.0 : 9.0);
        const yaw = Math.atan2(-spec.slopeX, 1.0);
        const halfLen = lerp(13.0, 16.0, rng());
        const innerR = 3.55;
        const outerR = 4.18;
        const groundAtMid = this.sampleHeightRaw(xLog, zLog).height;
        const centerY = groundAtMid + innerR - 0.22;

        const hlGeo = this.createHollowLogGeometry(halfLen, innerR, outerR, chunkIndex * 13.7);
        const hlMesh = new THREE.Mesh(hlGeo, this.hollowLogMat);
        hlMesh.position.set(xLog, centerY, zLog);
        hlMesh.rotation.set(0, yaw, 0);
        hlMesh.castShadow = true;
        hlMesh.receiveShadow = true;
        hlMesh.userData.uniqueGeo = true;
        chunkGroup.add(hlMesh);

        const fwdX = -Math.sin(yaw);
        const fwdZ = -Math.cos(yaw);
        hollowLogs.push({
          id: this.nextLogId++,
          chunkIndex,
          center: new THREE.Vector3(xLog, centerY, zLog),
          axisX: fwdX,
          axisZ: fwdZ,
          yaw,
          halfLength: halfLen,
          innerRadius: innerR,
          outerRadius: outerR,
        });
      }
    }

    const isInsideHollowLog = (x: number, z: number, pad = 6.0): boolean => {
      for (const hl of hollowLogs) {
        const dx = x - hl.center.x;
        const dz = z - hl.center.z;
        const along = Math.abs(dx * hl.axisX + dz * hl.axisZ);
        const lat = Math.abs(-dx * hl.axisZ + dz * hl.axisX);
        if (along < hl.halfLength + pad && lat < hl.outerRadius + pad) return true;
      }
      return false;
    };

    // G. DYNAMIC FALLING TREES (Forest!)
    if (chunkIndex >= 0 && biome === 'FOREST') {
      const zTree = zStart - (0.68 + rng() * 0.14) * CHUNK_LENGTH;
      if (zTree < -55) {
        const spec = this.getTrailSpec(zTree);
        const side = chunkIndex % 2 === 0 ? -1 : 1;
        const xTree = spec.centerX + side * lerp(18.0, 26.0, rng());
        const finalZTree = isInsideHollowLog(xTree, zTree, 8.0) ? zTree - 20.0 : zTree;
        const groundY = this.sampleHeightRaw(xTree, finalZTree).height;

        const pivotGroup = new THREE.Group();
        pivotGroup.position.set(xTree, groundY - 0.2, finalZTree);
        const fallDirAngle = side < 0 ? (rng() - 0.5) * 0.3 : Math.PI + (rng() - 0.5) * 0.3;
        pivotGroup.rotation.set(0, fallDirAngle, 0);

        const trunkMesh = new THREE.Mesh(this.fallingTrunkGeo, this.barkMat);
        trunkMesh.castShadow = true;
        pivotGroup.add(trunkMesh);

        const crownMesh = new THREE.Mesh(this.canopyClusterGeo, this.canopyLeafMat);
        crownMesh.position.set(0, 23.5, 0);
        crownMesh.scale.setScalar(1.7);
        pivotGroup.add(crownMesh);
        chunkGroup.add(pivotGroup);

        const dynObs: WorldObstacle = {
          id: this.nextObstacleId++,
          chunkIndex,
          kind: 'buttress_tree',
          position: new THREE.Vector3(xTree, groundY, finalZTree),
          radius: 0.72,
          height: 20.0,
          canShearVine: true,
        };
        obstacles.push(dynObs);

        fallingTrees.push({
          id: dynObs.id,
          chunkIndex,
          pivotGroup,
          basePos: new THREE.Vector3(xTree, groundY, finalZTree),
          fallDirAngle,
          currentTilt: 0.06,
          maxTilt: rng() < 0.45 ? 1.29 : 1.48,
          angularVel: 0,
          state: 'STANDING',
          triggerDistance: lerp(56, 72, rng()),
          trunkLength: 24.0,
          trunkRadius: 0.52,
          dynamicObstacle: dynObs,
        });
      }
    }

    // H. SPACIOUSLY BORDERED PERIMETER TREES / RED-SANDSTONE CANYON MESAS!
    const perimeterCount =
      biome === 'DEADWOOD' ||
      biome === 'SINGULARITY' ||
      biome === 'GIANT_LOG' ||
      biome === 'GLACIAL_ICE'
        ? 0
        : biome === 'DESERT'
        ? 22
        : biome === 'SWAMP'
        ? 22
        : 30;
    for (let i = 0; i < perimeterCount; i++) {
      const z = zStart - rng() * CHUNK_LENGTH;
      const spec = this.getTrailSpec(z);
      const side = i % 2 === 0 ? -1 : 1;

      let x: number;
      if (i === 0 && spec.splitFactor > 0.6 && biome === 'FOREST') {
        x = spec.centerX + (rng() - 0.5) * 3.0;
      } else if (i < 8) {
        x = spec.centerX + side * lerp(spec.halfWidth + 10.0, spec.halfWidth + 28.0, rng());
      } else {
        x = spec.centerX + side * lerp(spec.halfWidth + 28.0, CORRIDOR_HALF_SPAN - 12.0, rng());
      }

      if (isSafeZone(z) && Math.abs(x - spec.centerX) < 28) continue;
      const ground = this.sampleHeightRaw(x, z);

      if (biome === 'DESERT') {
        // Towering Natural Red-Sandstone Canyon Mesas & Buttes framing the village valley!
        const rad = lerp(6.0, 14.0, rng());
        const hScale = lerp(1.7, 3.6, rng());
        dummy.position.set(x, ground.height + rad * 0.38, z);
        dummy.rotation.set(0, rng() * Math.PI * 2, 0);
        dummy.scale.set(rad, rad * hScale, rad);
        dummy.updateMatrix();
        boulderTransforms.push({ geoIdx: i % 3, matrix: dummy.matrix.clone() });
      } else {
        const scaleXZ = lerp(0.95, 1.5, rng());
        const scaleY = lerp(0.92, 1.32, rng());
        dummy.position.set(x, ground.height - 0.6, z);
        dummy.rotation.set(0, rng() * Math.PI * 2, 0);
        dummy.scale.set(scaleXZ, scaleY, scaleXZ);
        dummy.updateMatrix();
        titanTreeTransforms.push(dummy.matrix.clone());

        const crownY = ground.height + 44.0 * scaleY;
        dummy.position.set(x, crownY, z);
        dummy.scale.set(scaleXZ * 1.25, 1.1, scaleXZ * 1.25);
        dummy.updateMatrix();
        titanCrownTransforms.push(dummy.matrix.clone());

        if (i < 16) {
          dummy.position.set(x, crownY - 4.0, z);
          dummy.scale.setScalar(lerp(0.95, 1.4, rng()));
          dummy.updateMatrix();
          highLianaTransforms.push(dummy.matrix.clone());
        }

        if (Math.abs(x - spec.centerX) < spec.halfWidth + 16) {
          obstacles.push({
            id: this.nextObstacleId++,
            chunkIndex,
            kind: 'titan_tree',
            position: new THREE.Vector3(x, ground.height, z),
            radius: 3.2 * scaleXZ,
            height: 45.0 * scaleY,
            canShearVine: true,
          });

          if (chunkIndex >= 0 && rootAnchors.length < 4 && rng() < 0.6) {
            const dirToTrail = Math.sign(spec.centerX - x) || 1;
            const rbX = x + dirToTrail * 3.5;
            const rbY = this.sampleHeightRaw(rbX, z).height;
            rootAnchors.push({
              id: this.nextRootId++,
              chunkIndex,
              position: new THREE.Vector3(rbX, rbY + 0.12, z),
              reachTargetPos: new THREE.Vector3(rbX + dirToTrail * 8.5, rbY + 0.6, z - 5.0),
            });
          }
        }
      }
    }

    // I. WELL-SPACED ROADSIDE BOULDERS & SLATE ROCKS
    const sparseCount =
      biome === 'GIANT_LOG' || biome === 'GLACIAL_ICE'
        ? 0
        : biome === 'SINGULARITY'
        ? 2
        : biome === 'DESERT'
        ? 3
        : biome === 'SWAMP'
        ? 4
        : biome === 'DEADWOOD'
        ? 6
        : 7;
    for (let i = 0; i < sparseCount; i++) {
      const z = zStart - ((i + 0.3 + rng() * 0.4) / sparseCount) * CHUNK_LENGTH;
      if (isSafeZone(z)) continue;
      const spec = this.getTrailSpec(z);
      const x =
        biome === 'DESERT'
          ? spec.centerX + (i % 2 === 0 ? -1 : 1) * lerp(spec.halfWidth * 0.72, spec.halfWidth * 1.1, rng())
          : spec.centerX + (rng() - 0.5) * 2 * (spec.halfWidth * 0.85);
      if (isInsideHollowLog(x, z, 7.0)) continue;

      let tooClose = false;
      for (const prev of obstacles) {
        if (Math.hypot(prev.position.x - x, prev.position.z - z) < prev.radius + 16.0) {
          tooClose = true;
          break;
        }
      }
      if (tooClose) continue;

      const ground = this.sampleHeightRaw(x, z);
      if (ground.waterDepth > 0.08) continue;

      const rad = lerp(1.1, 1.85, rng());
      const hScale = lerp(1.05, 1.75, rng());
      dummy.position.set(x, ground.height - 0.2, z);
      dummy.rotation.set(0, rng() * Math.PI * 2, 0);
      dummy.scale.set(rad, hScale, rad);
      dummy.updateMatrix();
      shearTransforms.push(dummy.matrix.clone());

      obstacles.push({
        id: this.nextObstacleId++,
        chunkIndex,
        kind: 'shear_rock',
        position: new THREE.Vector3(x, ground.height, z),
        radius: rad * 0.9,
        height: 2.8 * hScale,
        canShearVine: true,
      });
    }

    // Natural Desert Saguaro Cacti dotting the outer canyon slopes behind the shacks!
    if (biome === 'DESERT') {
      for (let c = 0; c < 24; c++) {
        const z = zStart - rng() * CHUNK_LENGTH;
        const spec = this.getTrailSpec(z);
        const side = rng() < 0.5 ? -1 : 1;
        const x = spec.centerX + side * lerp(spec.halfWidth * 0.88, CORRIDOR_HALF_SPAN - 22, rng());
        const gy = this.sampleHeightRaw(x, z).height;
        dummy.position.set(x, gy - 0.2, z);
        dummy.rotation.set(0, rng() * Math.PI * 2, 0);
        dummy.scale.setScalar(lerp(0.85, 1.4, rng()));
        dummy.updateMatrix();
        cactusTransforms.push(dummy.matrix.clone());
      }
    }

    // J. OVERARCHING CANOPY BOUGHS & HANGING VINES (Forest & Swamp only!)
    if (chunkIndex >= 0 && (biome === 'FOREST' || biome === 'SWAMP')) {
      const archCount = biome === 'SWAMP' ? 4 : 6;
      for (let a = 0; a < archCount; a++) {
        const zArch = zStart - ((a + 0.22 + rng() * 0.45) / archCount) * CHUNK_LENGTH;
        if (zArch > -16) continue;

        const spec = this.getTrailSpec(zArch);
        const spanHalf = spec.halfWidth + 8.0;
        const leftGround = this.sampleHeightRaw(spec.centerX - spanHalf, zArch).height;
        const rightGround = this.sampleHeightRaw(spec.centerX + spanHalf, zArch).height;
        const crownHeight = spec.baseY + (biome === 'SWAMP' ? lerp(13.5, 17.5, rng()) : lerp(9.6, 13.4, rng()));
        const skewZ = (rng() - 0.5) * 9.0;

        const p0 = new THREE.Vector3(spec.centerX - spanHalf, leftGround + 7.5, zArch - skewZ);
        const p1 = new THREE.Vector3(spec.centerX - spanHalf * 0.4, crownHeight, zArch - skewZ * 0.35);
        const p2 = new THREE.Vector3(spec.centerX + spanHalf * 0.4, crownHeight, zArch + skewZ * 0.35);
        const p3 = new THREE.Vector3(spec.centerX + spanHalf, rightGround + 7.5, zArch + skewZ);

        const curve = new THREE.CatmullRomCurve3([p0, p1, p2, p3]);
        const boughRadius = lerp(0.82, 1.15, rng());
        const boughGeo = new THREE.TubeGeometry(curve, 22, boughRadius, 8, false);
        const boughMesh = new THREE.Mesh(
          boughGeo,
          biome === 'SWAMP' ? this.mangroveBarkMat : this.barkMat
        );
        boughMesh.castShadow = true;
        boughMesh.userData.uniqueGeo = true;
        chunkGroup.add(boughMesh);

        for (const tLeaf of [0.2, 0.4, 0.6, 0.8]) {
          const pt = curve.getPoint(tLeaf);
          dummy.position.set(pt.x, pt.y + 1.1, pt.z);
          dummy.rotation.set(0, rng() * Math.PI * 2, 0);
          dummy.scale.setScalar(lerp(1.4, 2.1, rng()));
          dummy.updateMatrix();
          canopyClusters.push(dummy.matrix.clone());
        }

        const anchorSlots = 5;
        for (let s = 0; s < anchorSlots; s++) {
          const uBranch = clamp(0.14 + ((s + 0.2 + rng() * 0.6) / anchorSlots) * 0.72, 0.14, 0.86);
          const anchorPt = curve.getPoint(uBranch);
          const tangent = curve.getTangent(uBranch);
          const centerGround = this.sampleHeightRaw(anchorPt.x, anchorPt.z).height;
          const archRoll = rng();
          const preferredArchetype: CanopyAnchor['preferredArchetype'] =
            archRoll < 0.56 ? 'CURTAIN' : archRoll < 0.86 ? 'STRIKER' : 'ARCH_GROWER';

          anchors.push({
            id: this.nextAnchorId++,
            chunkIndex,
            position: anchorPt.clone().add(new THREE.Vector3(0, -boughRadius * 0.65, 0)),
            branchTangent: tangent,
            trailCenterBelow: new THREE.Vector3(anchorPt.x, centerGround, anchorPt.z),
            trailHalfWidth: spec.halfWidth,
            preferredArchetype,
          });
        }
      }
    }

    // Sunbeams (Forest & Swamp!)
    if (biome === 'FOREST' || biome === 'SWAMP') {
      for (let i = 0; i < 5; i++) {
        const z = zStart - ((i + 0.3) / 5) * CHUNK_LENGTH;
        const spec = this.getTrailSpec(z);
        const x = spec.centerX + (rng() - 0.5) * spec.halfWidth * 1.2;
        dummy.position.set(x, this.sampleHeightRaw(x, z).height - 1.0, z);
        dummy.rotation.set(0, 0, 0);
        dummy.scale.setScalar(lerp(1.0, 1.5, rng()));
        dummy.updateMatrix();
        sunShaftTransforms.push(dummy.matrix.clone());
      }
    }

    const fernMatrices: THREE.Matrix4[] = [];
    const broadleafMatrices: THREE.Matrix4[] = [];
    const floraCount =
      biome === 'GIANT_LOG'
        ? 52
        : biome === 'DESERT'
        ? 18
        : biome === 'DEADWOOD' || biome === 'SINGULARITY' || biome === 'GLACIAL_ICE'
        ? 0
        : biome === 'SWAMP'
        ? 110
        : 135;
    for (let i = 0; i < floraCount; i++) {
      const z = zStart - rng() * CHUNK_LENGTH;
      const spec = this.getTrailSpec(z);
      const side = rng() < 0.5 ? -1 : 1;
      const offset =
        biome === 'GIANT_LOG'
          ? side * lerp(spec.halfWidth * 0.45, spec.halfWidth * 1.15, rng())
          : biome === 'DESERT'
          ? side * lerp(spec.halfWidth * 0.82, CORRIDOR_HALF_SPAN - 20, rng())
          : rng() < 0.25
          ? (rng() - 0.5) * 2 * (spec.halfWidth * 0.92)
          : side * lerp(spec.halfWidth * 0.85, CORRIDOR_HALF_SPAN - 20, rng());
      const x = spec.centerX + offset;
      const ground = this.sampleHeightRaw(x, z);
      if (ground.waterDepth > 0.05) continue;

      dummy.position.set(x, ground.height - 0.06, z);
      dummy.rotation.set(0, rng() * Math.PI * 2, 0);
      dummy.scale.setScalar(lerp(0.85, 1.85, rng()));
      dummy.updateMatrix();
      if (rng() < 0.62) fernMatrices.push(dummy.matrix.clone());
      else broadleafMatrices.push(dummy.matrix.clone());
    }

    const addInstanced = (
      geo: THREE.BufferGeometry,
      mat: THREE.Material,
      mats: THREE.Matrix4[],
      castShadow: boolean,
      receiveShadow: boolean
    ) => {
      if (mats.length === 0) return;
      const im = new THREE.InstancedMesh(geo, mat, mats.length);
      im.castShadow = castShadow;
      im.receiveShadow = receiveShadow;
      mats.forEach((m, idx) => im.setMatrixAt(idx, m));
      im.instanceMatrix.needsUpdate = true;
      chunkGroup.add(im);
    };

    const mainRockMat = biome === 'DESERT' ? this.desertRockMat : this.rockMat;
    for (let g = 0; g < 3; g++) {
      const subset = boulderTransforms.filter((b) => b.geoIdx === g).map((b) => b.matrix);
      addInstanced(this.boulderGeos[g], mainRockMat, subset, true, true);
    }

    addInstanced(
      this.shearSpireGeo,
      biome === 'DESERT' ? this.desertRockMat : this.shearRockMat,
      shearTransforms,
      true,
      true
    );
    addInstanced(
      this.cactusPillarGeo,
      this.canopyLeafMat,
      cactusTransforms,
      true,
      true
    );
    addInstanced(
      this.titanTrunkGeo,
      biome === 'SWAMP' ? this.mangroveBarkMat : this.barkMat,
      titanTreeTransforms,
      true,
      true
    );
    addInstanced(
      this.titanCrownGeo,
      biome === 'SWAMP' ? this.swampCanopyMat : this.canopyLeafMat,
      titanCrownTransforms,
      false,
      false
    );
    addInstanced(this.highLianaCurtainGeo, this.highLianaMat, highLianaTransforms, false, false);
    addInstanced(this.midTrunkGeo, this.barkMat, midTreeTransforms, true, true);
    addInstanced(
      this.canopyClusterGeo,
      biome === 'SWAMP' ? this.swampCanopyMat : this.canopyLeafMat,
      canopyClusters,
      true,
      false
    );
    addInstanced(this.fernClumpGeo, this.fernMat, fernMatrices, true, true);
    addInstanced(this.broadleafClumpGeo, this.broadleafMat, broadleafMatrices, true, true);
    addInstanced(this.sunShaftGeo, this.sunShaftMat, sunShaftTransforms, false, false);

    this.group.add(chunkGroup);

    for (const cb of this.onChunkSpawnedCallbacks) {
      cb(chunkIndex, anchors, rootAnchors);
    }
  }
}

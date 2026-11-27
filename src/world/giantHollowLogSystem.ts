import * as THREE from 'three';
import { clamp, fbm2D, lerp, mulberry32, smoothstep } from '../math/noise';
import { ProceduralTextureLibrary } from '../graphics/proceduralTextures';
import { CrisscrossRootBridge, WorldObstacle } from './trailCorridor';

export interface StationaryLogWorm {
  id: number;
  boreHolePos: THREE.Vector3;
  emergeDir: THREE.Vector3;
  lateralTang: THREE.Vector3;
  maxReach: number;
  baseRadius: number;
  phase: number;
  state: 'PEEKING' | 'LUNGING' | 'LATCHED' | 'RETRACTING';
  extension: number;
  stateTimer: number;
  strikeCooldown: number;
  group: THREE.Group;
  segmentMeshes: THREE.Mesh[];
  headGroup: THREE.Group;
  mandibleMeshes: THREE.Mesh[];
  currentTipWorld: THREE.Vector3;
}

export interface MessyWebSnareZone {
  id: number;
  anchorCenter: THREE.Vector3;
  wallAnchors: THREE.Vector3[];
  halfWidthX: number;
  halfDepthZ: number;
  trapHeightY: number;
  snareGroup: THREE.Group;
  tearProgress: number; // 0 = intact sticky snare, 1 = ripped open
  stretchVec: THREE.Vector3;
}

export type MushroomArchetype = 'BOUNCE_CAP' | 'SPORE_PUFFBALL' | 'TOADSTOOL_PILLAR';

export interface LiveGrowingMushroom {
  id: number;
  kind: MushroomArchetype;
  position: THREE.Vector3;
  growth: number; // 0.06 .. 1.0 live sprouting!
  growthSpeed: number;
  triggered: boolean;
  triggerDist: number;
  capRadius: number;
  fullHeight: number;
  squashBounce: number;
  puffTimer: number;
  burstActive: number; // 0..1 expanding spore cloud
  group: THREE.Group;
  stemMesh: THREE.Mesh;
  capMesh: THREE.Mesh;
  sporeCloudMesh?: THREE.Mesh;
  dynamicObstacle?: WorldObstacle;
}

export interface FallingSapGlob {
  dripOrigin: THREE.Vector3;
  floorPos: THREE.Vector3;
  currentY: number;
  vy: number;
  swellTimer: number;
  swellDuration: number;
  state: 'SWELLING' | 'FALLING' | 'SPLAT_COOLDOWN';
  globMesh: THREE.Mesh;
  puddleMesh: THREE.Mesh;
  puddleRadius: number;
}

export interface GiantHollowLogChunkData {
  worms: StationaryLogWorm[];
  webSnares: MessyWebSnareZone[];
  mushrooms: LiveGrowingMushroom[];
  sapGlobs: FallingSapGlob[];
}

interface SkylightHoleSpec {
  zCenter: number;
  zHalfLen: number;
  thetaCenter: number;
  thetaHalfSpan: number;
}

const VAULT_RADIUS_X = 39.5;
const VAULT_RADIUS_Y = 34.0;
const WORM_SEGMENT_COUNT = 11;
const SNARE_TETHER_COUNT = 8;

export class GiantHollowLogBuilder {
  public readonly innerHeartwoodMat: THREE.MeshStandardMaterial;
  public readonly outerBarkVaultMat: THREE.MeshStandardMaterial;
  public readonly boreHoleCollarMat: THREE.MeshStandardMaterial;
  public readonly shelfFungusTopMat: THREE.MeshStandardMaterial;
  public readonly shelfFungusGlowMat: THREE.MeshStandardMaterial;
  public readonly beardMossMat: THREE.MeshStandardMaterial;
  public readonly wormFleshMat: THREE.MeshStandardMaterial;
  public readonly wormHeadChitinMat: THREE.MeshStandardMaterial;

  // Non-orbweaver chaotic funnel/sheet/tangle silk materials!
  public readonly silkCableMat: THREE.MeshStandardMaterial;
  public readonly tangleLineMat: THREE.LineBasicMaterial;
  public readonly messySheetWebMat: THREE.ShaderMaterial;

  // Mushroom & sap ecosystem materials!
  public readonly mushroomStemMat: THREE.MeshStandardMaterial;
  public readonly bounceCapMat: THREE.MeshStandardMaterial;
  public readonly puffballMat: THREE.MeshStandardMaterial;
  public readonly toadstoolCapMat: THREE.MeshStandardMaterial;
  public readonly sporeBurstMat: THREE.MeshBasicMaterial;
  public readonly sapGlobMat: THREE.MeshStandardMaterial;

  public readonly godrayBeamMat: THREE.ShaderMaterial;
  public readonly dappledSunPoolMat: THREE.ShaderMaterial;
  public readonly sapAmberWaterMat: THREE.ShaderMaterial;

  // Global hollow-log spore motes + live 3D sticky web-snare tethers wrapping trapped vehicles!
  public readonly atmosphereFxGroup = new THREE.Group();
  private sporePoints!: THREE.Points;
  private sporePositions!: Float32Array;
  private sporeVelocities!: Float32Array;
  private readonly sporeCount = 1100;

  // Live Stuck-in-Web state!
  private activeSnare: MessyWebSnareZone | null = null;
  private snareEscapeProgress = 0;
  private snareImmunityTimer = 0;
  private snareTetherMeshes: THREE.Mesh[] = [];

  private readonly wormSegGeo: THREE.BufferGeometry;
  private readonly wormMandibleGeo: THREE.BufferGeometry;
  private readonly boreCollarGeo: THREE.BufferGeometry;
  private readonly shelfConkGeo: THREE.BufferGeometry;
  private readonly cocoonGeo: THREE.BufferGeometry;
  private readonly mushStemGeo: THREE.BufferGeometry;
  private readonly mushDomeCapGeo: THREE.BufferGeometry;
  private readonly puffballGeo: THREE.BufferGeometry;
  private readonly sapDropGeo: THREE.BufferGeometry;
  private readonly unitCylinderGeo: THREE.BufferGeometry;
  private readonly timeUniform = { value: 0 };

  private nextWormId = 1;
  private nextWebId = 1;
  private nextMushId = 1;

  public onWormStrikeVehicle?: (worldPos: THREE.Vector3) => void;
  public onVehicleStuckInWeb?: (worldPos: THREE.Vector3) => void;
  public onVehicleBreakFreeWeb?: (worldPos: THREE.Vector3) => void;
  public onMushroomBounce?: (worldPos: THREE.Vector3) => void;
  public onPuffballBurst?: (worldPos: THREE.Vector3) => void;

  constructor(textures: ProceduralTextureLibrary) {
    // 1. Warm ancient redwood/cedar inner heartwood
    this.innerHeartwoodMat = new THREE.MeshStandardMaterial({
      color: 0x6e4127,
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      normalScale: new THREE.Vector2(1.45, 1.45),
      roughness: 0.84,
      metalness: 0.03,
      vertexColors: true,
      side: THREE.DoubleSide,
    });

    this.outerBarkVaultMat = new THREE.MeshStandardMaterial({
      color: 0x3b2a1d,
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      normalScale: new THREE.Vector2(1.75, 1.75),
      roughness: 0.92,
      metalness: 0.02,
    });

    this.boreHoleCollarMat = new THREE.MeshStandardMaterial({
      color: 0x1c120c,
      map: textures.bark.map,
      normalMap: textures.rock.normalMap,
      roughness: 0.95,
      metalness: 0.04,
    });

    this.shelfFungusTopMat = new THREE.MeshStandardMaterial({
      color: 0xb86b35,
      map: textures.bark.map,
      normalMap: textures.terrainDetail.normalMap,
      roughness: 0.72,
      metalness: 0.04,
    });

    this.shelfFungusGlowMat = new THREE.MeshStandardMaterial({
      color: 0x9be89d,
      roughness: 0.45,
      metalness: 0.05,
      emissive: new THREE.Color(0x4bc96b),
      emissiveIntensity: 0.62,
    });

    this.beardMossMat = new THREE.MeshStandardMaterial({
      color: 0x4f6e32,
      map: textures.terrainDetail.map,
      roughness: 0.88,
      metalness: 0.02,
      side: THREE.DoubleSide,
    });

    // 2. Stationary Log-Emerging Parasite Worms
    this.wormFleshMat = new THREE.MeshStandardMaterial({
      color: 0xd9a17c,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.85, 0.85),
      roughness: 0.24,
      metalness: 0.08,
      emissive: new THREE.Color(0x3b180e),
      emissiveIntensity: 0.22,
    });

    this.wormHeadChitinMat = new THREE.MeshStandardMaterial({
      color: 0x291411,
      roughness: 0.28,
      metalness: 0.35,
      emissive: new THREE.Color(0x4a1208),
      emissiveIntensity: 0.28,
    });

    // 3. NON-ORBWEAVER Messy Funnel-Web / Cobweb-Spider Network Materials!
    // ZERO circular rings, ZERO radial spokes — pure chaotic 3D matted sheet-silk & irregular cross-strands!
    this.silkCableMat = new THREE.MeshStandardMaterial({
      color: 0xf0ede4,
      roughness: 0.48,
      metalness: 0.04,
      emissive: new THREE.Color(0x454238),
      emissiveIntensity: 0.32,
    });

    this.tangleLineMat = new THREE.LineBasicMaterial({
      color: 0xf2efe6,
      transparent: true,
      opacity: 0.58,
      depthWrite: false,
    });

    this.messySheetWebMat = new THREE.ShaderMaterial({
      transparent: true,
      depthWrite: false,
      side: THREE.DoubleSide,
      uniforms: {
        uTime: this.timeUniform,
      },
      vertexShader: `
        varying vec2 vUv;
        varying vec3 vWorldPos;
        uniform float uTime;
        void main() {
          vUv = uv;
          vec3 pos = position;
          // Heavy sagging hammock-sheet billow (funnel-spider / black-widow sheet web, NOT flat wheel!)
          float pin = sin(uv.x * 3.14159) * sin(uv.y * 3.14159);
          pos.y -= pin * 0.95;
          pos.z += sin(uTime * 1.4 + uv.x * 4.7 + uv.y * 5.3) * 0.34 * pin;
          vec4 wp = modelMatrix * vec4(pos, 1.0);
          vWorldPos = wp.xyz;
          gl_Position = projectionMatrix * viewMatrix * wp;
        }
      `,
      fragmentShader: `
        varying vec2 vUv;
        varying vec3 vWorldPos;
        uniform float uTime;

        float hash(vec2 p) {
          return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
        }

        float noise2(vec2 p) {
          vec2 i = floor(p);
          vec2 f = fract(p);
          f = f * f * (3.0 - 2.0 * f);
          return mix(
            mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),
            mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x),
            f.y
          );
        }

        // Chaotic multi-angle non-radial cobweb fiber tangle (zero spokes, zero concentric rings!)
        float fiberLayer(vec2 p, float angle, float scale) {
          float c = cos(angle);
          float s = sin(angle);
          vec2 rp = vec2(p.x * c - p.y * s, p.x * s + p.y * c) * scale;
          float warp = noise2(rp * 0.18) * 3.8 + noise2(rp * 0.52) * 1.4;
          float line = abs(fract(rp.y + warp) - 0.5) * 2.0;
          return smoothstep(0.84, 0.995, 1.0 - line);
        }

        void main() {
          vec2 wp = vUv * 12.0 + vWorldPos.xz * 0.08;

          // 5 completely asymmetric, warped random-angle silk fiber tangles!
          float f1 = fiberLayer(wp, 0.37, 1.9);
          float f2 = fiberLayer(wp + 3.1, 1.29, 2.4);
          float f3 = fiberLayer(wp - 5.7, 2.18, 3.1);
          float f4 = fiberLayer(wp + 8.4, -0.74, 2.7);
          float f5 = fiberLayer(wp * 1.4, 0.08, 1.4);

          float fibers = clamp(f1 * 0.72 + f2 * 0.68 + f3 * 0.58 + f4 * 0.62 + f5 * 0.75, 0.0, 1.2);

          // Cloudy, dusty matted funnel-silk sheet patches + irregular ragged holes
          float fbmSilk =
            noise2(wp * 0.55) * 0.55 +
            noise2(wp * 1.35) * 0.30 +
            noise2(wp * 3.10) * 0.15;

          float raggedTears = smoothstep(0.28, 0.56, fbmSilk);
          float denseFloc = smoothstep(0.50, 0.88, fbmSilk) * 0.52;

          // Organic wispy edges stretched between log walls & anchor cables
          float edgeX = smoothstep(0.0, 0.14, vUv.x) * smoothstep(1.0, 0.86, vUv.x);
          float edgeY = smoothstep(0.0, 0.16, vUv.y) * smoothstep(1.0, 0.82, vUv.y);
          float edgeMask = clamp(edgeX * edgeY * (0.65 + 0.7 * fbmSilk), 0.0, 1.0);

          float alpha = clamp((fibers * 0.78 + denseFloc) * raggedTears * edgeMask, 0.0, 0.90);
          if (alpha < 0.04) discard;

          vec3 dustyCol = vec3(0.78, 0.76, 0.70);
          vec3 brightSilk = vec3(0.99, 0.98, 0.93);
          vec3 col = mix(dustyCol, brightSilk, clamp(fibers, 0.0, 1.0));
          gl_FragColor = vec4(col, alpha);
        }
      `,
    });

    // 4. Live Growing Mushroom Materials!
    this.mushroomStemMat = new THREE.MeshStandardMaterial({
      color: 0xe8dec8,
      map: textures.bark.map,
      roughness: 0.74,
      metalness: 0.02,
    });

    // Bouncy golden-amber jelly-cap mushroom (launches vehicle upward!)
    this.bounceCapMat = new THREE.MeshStandardMaterial({
      color: 0xf5a638,
      map: textures.terrainDetail.map,
      roughness: 0.32,
      metalness: 0.06,
      emissive: new THREE.Color(0xc46b14),
      emissiveIntensity: 0.48,
    });

    // Explosive emerald-chartreuse spore puffball!
    this.puffballMat = new THREE.MeshStandardMaterial({
      color: 0xa6e856,
      map: textures.rock.map,
      roughness: 0.52,
      metalness: 0.04,
      emissive: new THREE.Color(0x5ab822),
      emissiveIntensity: 0.55,
    });

    // Towering crimson-russet forest toadstool pillar!
    this.toadstoolCapMat = new THREE.MeshStandardMaterial({
      color: 0xb8432a,
      map: textures.rock.map,
      roughness: 0.58,
      metalness: 0.04,
      emissive: new THREE.Color(0x3d1208),
      emissiveIntensity: 0.22,
    });

    this.sporeBurstMat = new THREE.MeshBasicMaterial({
      color: 0xbdf569,
      transparent: true,
      opacity: 0.48,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });

    this.sapGlobMat = new THREE.MeshStandardMaterial({
      color: 0xf29b24,
      roughness: 0.14,
      metalness: 0.12,
      emissive: new THREE.Color(0x8f4b06),
      emissiveIntensity: 0.52,
    });

    // 5. Outside Godray Shafts & Sunlit Floor Pools
    this.godrayBeamMat = new THREE.ShaderMaterial({
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
          float softEdge = pow(facing, 1.55);
          float verticalFade = smoothstep(0.0, 0.16, vUv.y) * (0.42 + 0.58 * pow(vUv.y, 0.75));
          float rays = 0.68 + 0.32 * sin(vUv.x * 25.1327 + uTime * 0.55 + sin(vUv.y * 8.0) * 1.2);
          float moteShimmer = 0.85 + 0.15 * sin(vUv.y * 32.0 - uTime * 1.4 + vUv.x * 12.56);
          vec3 col = mix(vec3(0.95, 0.82, 0.44), vec3(1.0, 0.92, 0.62), vUv.y);
          gl_FragColor = vec4(col, softEdge * verticalFade * rays * moteShimmer * 0.25);
        }
      `,
    });

    this.dappledSunPoolMat = new THREE.ShaderMaterial({
      transparent: true,
      depthWrite: false,
      blending: THREE.AdditiveBlending,
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
          vec2 c = vUv * 2.0 - 1.0;
          float r = length(c);
          float radial = smoothstep(1.0, 0.1, r);
          float dapple = 0.65 + 0.35 * sin(c.x * 9.0 + uTime * 0.7) * cos(c.y * 9.0 - uTime * 0.5);
          gl_FragColor = vec4(vec3(1.0, 0.86, 0.48), radial * dapple * 0.36);
        }
      `,
    });

    this.sapAmberWaterMat = new THREE.ShaderMaterial({
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
          float dist = length(vUv - vec2(0.5)) * 2.0;
          float edge = smoothstep(1.0, 0.68, dist);
          float ripple = sin(vWorldPos.x * 2.6 + uTime * 1.9) * cos(vWorldPos.z * 2.4 - uTime * 1.5);
          vec3 col = mix(vec3(0.26, 0.13, 0.04), vec3(0.84, 0.52, 0.14), 0.48 + 0.25 * ripple);
          gl_FragColor = vec4(col, edge * 0.86);
        }
      `,
    });

    // Shared Reusable Geometries
    const segGeo = new THREE.SphereGeometry(1.0, 12, 10);
    segGeo.scale(1.0, 0.92, 0.82);
    this.wormSegGeo = segGeo;

    const mandGeo = new THREE.ConeGeometry(0.22, 1.05, 6);
    mandGeo.rotateX(Math.PI * 0.38);
    mandGeo.translate(0, 0.18, 0.48);
    this.wormMandibleGeo = mandGeo;

    this.boreCollarGeo = new THREE.TorusGeometry(1.45, 0.52, 8, 14);

    const conkGeo = new THREE.CylinderGeometry(1.0, 0.55, 0.38, 14, 1, false, 0, Math.PI * 1.15);
    conkGeo.rotateY(-Math.PI * 0.575);
    this.shelfConkGeo = conkGeo;

    const cocGeo = new THREE.SphereGeometry(0.65, 8, 8);
    cocGeo.scale(0.75, 1.6, 0.75);
    this.cocoonGeo = cocGeo;

    const stemGeo = new THREE.CylinderGeometry(0.68, 1.0, 1.0, 10);
    stemGeo.translate(0, 0.5, 0);
    this.mushStemGeo = stemGeo;

    const capGeo = new THREE.SphereGeometry(1.0, 16, 10, 0, Math.PI * 2, 0, Math.PI * 0.56);
    this.mushDomeCapGeo = capGeo;

    const puffGeo = new THREE.SphereGeometry(1.0, 14, 12);
    puffGeo.scale(1.0, 0.88, 1.0);
    puffGeo.translate(0, 0.78, 0);
    this.puffballGeo = puffGeo;

    const dropGeo = new THREE.SphereGeometry(0.85, 10, 10);
    dropGeo.scale(0.82, 1.35, 0.82);
    this.sapDropGeo = dropGeo;

    const unitCyl = new THREE.CylinderGeometry(0.055, 0.055, 1.0, 5);
    this.unitCylinderGeo = unitCyl;

    this.initSporeMotesAndSnareTethers();
  }

  private initSporeMotesAndSnareTethers(): void {
    this.sporePositions = new Float32Array(this.sporeCount * 3);
    this.sporeVelocities = new Float32Array(this.sporeCount * 3);
    const colors = new Float32Array(this.sporeCount * 3);
    const rng = mulberry32(448812);

    for (let i = 0; i < this.sporeCount; i++) {
      const ix = i * 3;
      this.sporePositions[ix] = (rng() - 0.5) * 86;
      this.sporePositions[ix + 1] = rng() * 34;
      this.sporePositions[ix + 2] = (rng() - 0.5) * 160;

      this.sporeVelocities[ix] = (rng() - 0.5) * 1.4;
      this.sporeVelocities[ix + 1] = lerp(-0.35, 0.65, rng());
      this.sporeVelocities[ix + 2] = (rng() - 0.5) * 1.6;

      const isGreenSpore = rng() < 0.38;
      colors[ix] = isGreenSpore ? 0.62 : 1.0;
      colors[ix + 1] = isGreenSpore ? 0.96 : 0.88;
      colors[ix + 2] = isGreenSpore ? 0.58 : 0.52;
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(this.sporePositions, 3));
    geo.setAttribute('color', new THREE.BufferAttribute(colors, 3));

    const mat = new THREE.PointsMaterial({
      size: 0.42,
      vertexColors: true,
      transparent: true,
      opacity: 0.68,
      depthWrite: false,
      blending: THREE.AdditiveBlending,
    });

    this.sporePoints = new THREE.Points(geo, mat);
    this.sporePoints.frustumCulled = false;
    this.atmosphereFxGroup.add(this.sporePoints);

    // 8 dynamic elastic sticky silk snare cables that wrap onto your buggy when STUCK in a web!
    for (let t = 0; t < SNARE_TETHER_COUNT; t++) {
      const tether = new THREE.Mesh(this.unitCylinderGeo, this.silkCableMat);
      tether.visible = false;
      tether.frustumCulled = false;
      this.atmosphereFxGroup.add(tether);
      this.snareTetherMeshes.push(tether);
    }
  }

  public clearActiveWebSnare(): void {
    this.activeSnare = null;
    this.snareEscapeProgress = 0;
    this.snareImmunityTimer = 0;
    for (const m of this.snareTetherMeshes) m.visible = false;
  }

  /**
   * Helper: creates a sagging 3D silk bridge cable spanning between two points across the hollow log!
   */
  private addSilkBridgeCable(
    parent: THREE.Group,
    pA: THREE.Vector3,
    pB: THREE.Vector3,
    sagMeters: number,
    thickness: number
  ): void {
    const mid = new THREE.Vector3().addVectors(pA, pB).multiplyScalar(0.5);
    mid.y -= sagMeters;
    const curve = new THREE.QuadraticBezierCurve3(pA, mid, pB);
    const tubeGeo = new THREE.TubeGeometry(curve, 10, thickness, 5, false);
    const mesh = new THREE.Mesh(tubeGeo, this.silkCableMat);
    mesh.userData.uniqueGeo = true;
    parent.add(mesh);
  }

  /**
   * Builds the CONTINUOUS WALL-TO-WALL & END-TO-END MESSY COBWEB NETWORK spanning across the entire log!
   * - Wall-to-wall sagging silk bridge cables stretch from Left Log Wall (-37m) to Right Log Wall (+37m)
   * - Longitudinal silk trunk lines connect from zStart to zStart - chunkLength so the network is continuous
   * - Chaotic 3D space-filling silk tangle filaments (hundreds of non-radial diagonal & vertical gumfoot lines!)
   * - Low sagging funnel-sheet snare pockets that TRAP vehicles if hit, paired with open silk-arch weaving gaps!
   */
  private buildWallToWallCobwebNetwork(
    chunkGroup: THREE.Group,
    webSnares: MessyWebSnareZone[],
    chunkIndex: number,
    zStart: number,
    chunkLength: number,
    rng: () => number,
    specSampler: (z: number) => { centerX: number; halfWidth: number; baseY: number },
    groundSampler: (x: number, z: number) => number
  ): void {
    if (chunkIndex < 0) return;

    const tanglePositions: number[] = [];
    const pushFilament = (a: THREE.Vector3, b: THREE.Vector3) => {
      tanglePositions.push(a.x, a.y, a.z, b.x, b.y, b.z);
    };

    // 1. Continuous Longitudinal Trunk Cables running from one end of the log chunk to the other,
    // crisscrossing from Left Wall to Right Wall to form an unbroken overhead/side spiderweb network!
    const stations = 7;
    const leftNodes: THREE.Vector3[] = [];
    const rightNodes: THREE.Vector3[] = [];
    const crownNodes: THREE.Vector3[] = [];

    for (let s = 0; s <= stations; s++) {
      const z = zStart - (s / stations) * chunkLength;
      const sp = specSampler(z);

      const lTheta = Math.PI - lerp(0.18, 0.55, (s * 0.37) % 1);
      const rTheta = lerp(0.18, 0.55, ((s + 2) * 0.41) % 1);
      const cTheta = Math.PI * 0.5 + ((s % 2 === 0 ? 1 : -1) * 0.24);

      const pL = new THREE.Vector3(
        sp.centerX + Math.cos(lTheta) * (VAULT_RADIUS_X - 1.6),
        sp.baseY - 1.5 + Math.sin(lTheta) * (VAULT_RADIUS_Y - 1.6),
        z
      );
      const pR = new THREE.Vector3(
        sp.centerX + Math.cos(rTheta) * (VAULT_RADIUS_X - 1.6),
        sp.baseY - 1.5 + Math.sin(rTheta) * (VAULT_RADIUS_Y - 1.6),
        z
      );
      const pC = new THREE.Vector3(
        sp.centerX + Math.cos(cTheta) * (VAULT_RADIUS_X - 3.5),
        sp.baseY - 1.5 + Math.sin(cTheta) * (VAULT_RADIUS_Y - 5.5),
        z
      );

      leftNodes.push(pL);
      rightNodes.push(pR);
      crownNodes.push(pC);

      // Full-span Wall-to-Wall silk bridge cables stretching from Left Wall all the way to Right Wall!
      if (Math.abs(z) > 26) {
        this.addSilkBridgeCable(chunkGroup, pL, pR, lerp(3.2, 6.8, rng()), 0.085);
        this.addSilkBridgeCable(chunkGroup, pL, pC, lerp(1.4, 2.8, rng()), 0.065);
        this.addSilkBridgeCable(chunkGroup, pC, pR, lerp(1.4, 2.8, rng()), 0.065);
      }

      // Connect longitudinally to previous station so the web network runs end-to-end down the entire log!
      if (s > 0 && Math.abs(z) > 22) {
        this.addSilkBridgeCable(chunkGroup, leftNodes[s - 1], pR, lerp(3.5, 6.2, rng()), 0.07);
        this.addSilkBridgeCable(chunkGroup, rightNodes[s - 1], pL, lerp(3.5, 6.2, rng()), 0.07);
        pushFilament(leftNodes[s - 1], pL);
        pushFilament(rightNodes[s - 1], pR);
        pushFilament(crownNodes[s - 1], pC);

        // Dense chaotic 3D cross-tangle filaments spanning between stations from wall to wall!
        for (let f = 0; f < 26; f++) {
          const uA = rng();
          const uB = rng();
          const a = new THREE.Vector3().lerpVectors(leftNodes[s - 1], rightNodes[s - 1], uA);
          const b = new THREE.Vector3().lerpVectors(pL, pR, uB);
          a.y += lerp(-4.5, 6.5, rng()) * Math.sin(uA * Math.PI);
          b.y += lerp(-4.5, 6.5, rng()) * Math.sin(uB * Math.PI);
          pushFilament(a, b);
        }
      }
    }

    // 2. Low-Hanging Sticky Funnel/Sheet Snare Pockets inside the Wall-to-Wall Network!
    // Each station spans wall-to-wall overhead, while dropping a dense sticky floor-snare across ~55% of the width
    // and leaving a clear high-arched weaving opening on the alternating side!
    const snareStations = 5;
    for (let k = 0; k < snareStations; k++) {
      const sz = zStart - ((k + 0.22 + rng() * 0.35) / snareStations) * chunkLength;
      if (Math.abs(sz) < 32) continue;

      const sp = specSampler(sz);
      const leftWallX = sp.centerX - 36.5;
      const rightWallX = sp.centerX + 36.5;
      const leftWallY = groundSampler(sp.centerX - 28.0, sz) + lerp(8.5, 14.5, rng());
      const rightWallY = groundSampler(sp.centerX + 28.0, sz) + lerp(8.5, 14.5, rng());

      // Overhead bridge cable spanning 100% from Left Log Wall to Right Log Wall at this snare!
      const pWallL = new THREE.Vector3(leftWallX, leftWallY, sz - 2.0);
      const pWallR = new THREE.Vector3(rightWallX, rightWallY, sz + 2.0);
      this.addSilkBridgeCable(chunkGroup, pWallL, pWallR, 2.4, 0.095);

      // Alternate where the low sticky funnel-sheet drops down to the floor so players WEAVE through the network!
      const pattern = (chunkIndex * 3 + k) % 3;
      let snareX = sp.centerX;
      let halfWidthX = sp.halfWidth * 0.46;
      if (pattern === 0) {
        // Snare drops on LEFT & CENTER -> open weaving tunnel on RIGHT!
        snareX = sp.centerX - sp.halfWidth * 0.34;
        halfWidthX = sp.halfWidth * 0.50;
      } else if (pattern === 1) {
        // Snare drops on RIGHT & CENTER -> open weaving tunnel on LEFT!
        snareX = sp.centerX + sp.halfWidth * 0.34;
        halfWidthX = sp.halfWidth * 0.50;
      } else {
        // Snare drops in CENTER funnel -> weave around LEFT or RIGHT curved log banks!
        snareX = sp.centerX + (rng() - 0.5) * 3.0;
        halfWidthX = sp.halfWidth * 0.40;
      }

      const floorY = groundSampler(snareX, sz);
      const trapHeightY = lerp(8.5, 12.5, rng());

      const snareGroup = new THREE.Group();
      snareGroup.position.set(snareX, floorY, sz);
      chunkGroup.add(snareGroup);

      // Multi-angle draped horizontal/sloped funnel-sheets & chaotic vertical curtains (ZERO orb circles!)
      for (let sh = 0; sh < 5; sh++) {
        const w = halfWidthX * lerp(1.65, 2.25, rng());
        const h = trapHeightY * lerp(0.75, 1.15, rng());
        const sheetGeo = new THREE.PlaneGeometry(w, h, 8, 8);
        const sheetMesh = new THREE.Mesh(sheetGeo, this.messySheetWebMat);

        if (sh < 2) {
          // Sloped hammock canopy sheets like real funnel-web / sheet-web spiders!
          sheetMesh.position.set(
            (rng() - 0.5) * halfWidthX * 0.4,
            trapHeightY * lerp(0.28, 0.62, rng()),
            (rng() - 0.5) * 3.2
          );
          sheetMesh.rotation.set(
            -Math.PI * lerp(0.22, 0.38, rng()),
            (rng() - 0.5) * 0.75,
            (rng() - 0.5) * 0.35
          );
        } else {
          // Chaotic skewed vertical/diagonal sticky trap sheets!
          sheetMesh.position.set(
            (rng() - 0.5) * halfWidthX * 0.5,
            trapHeightY * lerp(0.42, 0.56, rng()),
            (sh - 3) * 1.65
          );
          sheetMesh.rotation.set(
            (rng() - 0.5) * 0.42,
            (rng() - 0.5) * 0.95,
            (rng() - 0.5) * 0.48
          );
        }
        sheetMesh.renderOrder = 4;
        sheetMesh.userData.uniqueGeo = true;
        snareGroup.add(sheetMesh);
      }

      // Tie this snare pocket all the way out to BOTH log walls + floor gumfoot trap-lines!
      const localWallL = new THREE.Vector3(leftWallX - snareX, leftWallY - floorY, -2.0);
      const localWallR = new THREE.Vector3(rightWallX - snareX, rightWallY - floorY, 2.0);
      this.addSilkBridgeCable(
        snareGroup,
        localWallL,
        new THREE.Vector3(-halfWidthX * 0.4, trapHeightY * 0.55, 0),
        1.5,
        0.075
      );
      this.addSilkBridgeCable(
        snareGroup,
        localWallR,
        new THREE.Vector3(halfWidthX * 0.4, trapHeightY * 0.55, 0),
        1.5,
        0.075
      );

      // Vertical sticky gumfoot trap-lines anchored down into the heartwood floor + chaotic 3D lattice!
      for (let g = 0; g < 34; g++) {
        const ax = snareX + (rng() - 0.5) * halfWidthX * 2.1;
        const az = sz + (rng() - 0.5) * 6.5;
        const ay = groundSampler(ax, az);
        const bx = snareX + (rng() - 0.5) * halfWidthX * 2.4;
        const bz = sz + (rng() - 0.5) * 8.0;
        const by = ay + lerp(2.5, trapHeightY + 4.5, rng());
        pushFilament(new THREE.Vector3(ax, ay, az), new THREE.Vector3(bx, by, bz));
      }

      // Wrapped silk prey/debris cocoons suspended inside the funnel tangle
      for (let c = 0; c < 3; c++) {
        const coc = new THREE.Mesh(this.cocoonGeo, this.silkCableMat);
        coc.position.set(
          (rng() - 0.5) * halfWidthX * 1.2,
          trapHeightY * lerp(0.28, 0.72, rng()),
          (rng() - 0.5) * 2.5
        );
        coc.rotation.set((rng() - 0.5) * 0.5, rng() * Math.PI, (rng() - 0.5) * 0.5);
        snareGroup.add(coc);
      }

      // Also stretch high overhead silk canopy threads across the OPEN lane so the entire log feels roofed in web!
      const openSideX = pattern === 0 ? sp.centerX + sp.halfWidth * 0.45 : sp.centerX - sp.halfWidth * 0.45;
      const openFloorY = groundSampler(openSideX, sz);
      for (let hi = 0; hi < 14; hi++) {
        pushFilament(
          new THREE.Vector3(
            openSideX + (rng() - 0.5) * 18.0,
            openFloorY + lerp(7.0, 15.0, rng()),
            sz + (rng() - 0.5) * 12.0
          ),
          new THREE.Vector3(
            snareX + (rng() - 0.5) * 14.0,
            floorY + lerp(6.5, 14.0, rng()),
            sz + (rng() - 0.5) * 12.0
          )
        );
      }

      const wallAnchors: THREE.Vector3[] = [
        pWallL.clone(),
        pWallR.clone(),
        new THREE.Vector3(snareX - halfWidthX, floorY + 0.3, sz - 2.5),
        new THREE.Vector3(snareX + halfWidthX, floorY + 0.3, sz - 2.5),
        new THREE.Vector3(snareX - halfWidthX * 0.8, floorY + trapHeightY, sz + 2.0),
        new THREE.Vector3(snareX + halfWidthX * 0.8, floorY + trapHeightY, sz + 2.0),
        new THREE.Vector3(snareX, floorY + 0.2, sz + 3.2),
        new THREE.Vector3(snareX, floorY + trapHeightY * 0.85, sz - 3.2),
      ];

      webSnares.push({
        id: this.nextWebId++,
        anchorCenter: new THREE.Vector3(snareX, floorY + 1.2, sz),
        wallAnchors,
        halfWidthX,
        halfDepthZ: 3.6,
        trapHeightY,
        snareGroup,
        tearProgress: 0,
        stretchVec: new THREE.Vector3(),
      });
    }

    if (tanglePositions.length > 0) {
      const lineGeo = new THREE.BufferGeometry();
      lineGeo.setAttribute('position', new THREE.Float32BufferAttribute(tanglePositions, 3));
      const lines = new THREE.LineSegments(lineGeo, this.tangleLineMat);
      lines.userData.uniqueGeo = true;
      chunkGroup.add(lines);
    }
  }

  /**
   * Builds the colossal overhead & side hollow-log wooden vault shell, skylight breaches,
   * volumetric outside godrays, bracket shelf-fungi, and peeling bark jump-ramps!
   */
  private buildHollowLogVaultAndGodrays(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    chunkLength: number,
    rng: () => number,
    specSampler: (z: number) => { centerX: number; halfWidth: number; baseY: number; slopeX: number },
    groundSampler: (x: number, z: number) => number,
    rootBridges: CrisscrossRootBridge[],
    allocBridgeId: () => number
  ): void {
    const skylights: SkylightHoleSpec[] = [];
    const holeCount = 4;
    for (let h = 0; h < holeCount; h++) {
      const zCenter = zStart - ((h + 0.22 + rng() * 0.52) / holeCount) * chunkLength;
      const sideBias = h % 2 === 0 ? lerp(1.12, 1.52, rng()) : lerp(1.62, 2.04, rng());
      skylights.push({
        zCenter,
        zHalfLen: lerp(5.2, 8.8, rng()),
        thetaCenter: sideBias,
        thetaHalfSpan: lerp(0.18, 0.30, rng()),
      });
    }

    const zSegs = 28;
    const aSegs = 30;
    const thetaMin = -0.08;
    const thetaMax = Math.PI + 0.08;

    const cols = aSegs + 1;
    const rows = zSegs + 1;
    const positions = new Float32Array(cols * rows * 3);
    const colors = new Float32Array(cols * rows * 3);
    const uvs = new Float32Array(cols * rows * 2);
    const indices: number[] = [];

    const colHeart = new THREE.Color(0x7a482b);
    const colRot = new THREE.Color(0x3a2518);
    const colMoss = new THREE.Color(0x3c5229);
    const colSunRim = new THREE.Color(0xc99652);
    const tmpCol = new THREE.Color();

    const isInsideSkylight = (z: number, theta: number): number => {
      let maxIn = 0;
      for (const sk of skylights) {
        const nz = (z - sk.zCenter) / sk.zHalfLen;
        const nt = (theta - sk.thetaCenter) / sk.thetaHalfSpan;
        const jag = Math.sin(z * 0.55 + theta * 9.0) * 0.16;
        const d = nz * nz + nt * nt + jag;
        if (d < 1.0) maxIn = Math.max(maxIn, 1.0 - d);
      }
      return maxIn;
    };

    let pPtr = 0;
    let uvPtr = 0;
    for (let rz = 0; rz < rows; rz++) {
      const tz = rz / zSegs;
      const z = zStart - tz * chunkLength;
      const sp = specSampler(z);
      const ribBulge = Math.pow(Math.sin(z * 0.225) * 0.5 + 0.5, 4.0) * 2.35;

      for (let ca = 0; ca < cols; ca++) {
        const ta = ca / aSegs;
        const theta = lerp(thetaMin, thetaMax, ta);
        const grainFlute =
          Math.sin(theta * 22.0 + fbm2D(z * 0.06, theta * 3.0, 2) * 2.2) * 1.15 +
          fbm2D(z * 0.09, theta * 4.5, 2) * 1.45;
        const rx = VAULT_RADIUS_X - ribBulge + grainFlute;
        const ry = VAULT_RADIUS_Y - ribBulge * 0.85 + grainFlute * 0.85;

        positions[pPtr] = sp.centerX + Math.cos(theta) * rx;
        positions[pPtr + 1] = sp.baseY - 1.5 + Math.sin(theta) * ry;
        positions[pPtr + 2] = z;

        uvs[uvPtr++] = ta * 8.0;
        uvs[uvPtr++] = z * 0.08;

        const mossPatch = clamp(fbm2D(z * 0.08 + 11.2, theta * 3.5, 2) * 0.5 + 0.45, 0, 1);
        const skyProx = isInsideSkylight(z, theta);
        tmpCol.copy(colHeart).lerp(colRot, clamp(ribBulge * 0.32, 0, 0.75));
        tmpCol.lerp(colMoss, smoothstep(0.52, 0.85, mossPatch) * 0.58);
        if (skyProx > 0.01) tmpCol.lerp(colSunRim, clamp(skyProx * 1.8, 0, 0.85));

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
        if (isInsideSkylight(zMid, thetaMid) > 0.24) continue;
        const a = rz * cols + ca;
        const b = a + 1;
        const c = a + cols;
        const d = c + 1;
        indices.push(a, c, b, b, c, d);
      }
    }

    const vaultGeo = new THREE.BufferGeometry();
    vaultGeo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    vaultGeo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
    vaultGeo.setAttribute('uv', new THREE.BufferAttribute(uvs, 2));
    vaultGeo.setIndex(indices);
    vaultGeo.computeVertexNormals();

    const vaultMesh = new THREE.Mesh(vaultGeo, this.innerHeartwoodMat);
    vaultMesh.castShadow = true;
    vaultMesh.receiveShadow = true;
    vaultMesh.userData.uniqueGeo = true;
    chunkGroup.add(vaultMesh);

    // Volumetric Outside Godrays & Hanging Moss at every skylight breach!
    for (const sk of skylights) {
      const sp = specSampler(sk.zCenter);
      const holeX = sp.centerX + Math.cos(sk.thetaCenter) * (VAULT_RADIUS_X - 0.5);
      const holeY = sp.baseY - 1.5 + Math.sin(sk.thetaCenter) * (VAULT_RADIUS_Y - 0.5);
      const holePos = new THREE.Vector3(holeX, holeY, sk.zCenter);

      const dropApprox = Math.max(18.0, holeY - sp.baseY);
      const targetX = clamp(holeX + dropApprox * 0.52, sp.centerX - 28.0, sp.centerX + 28.0);
      const targetZ = sk.zCenter + dropApprox * 0.42;
      const floorY = groundSampler(targetX, targetZ);
      const floorPos = new THREE.Vector3(targetX, floorY + 0.06, targetZ);

      const beamVec = new THREE.Vector3().subVectors(floorPos, holePos);
      const beamLen = beamVec.length();
      const midPos = new THREE.Vector3().addVectors(holePos, floorPos).multiplyScalar(0.5);

      const topR = Math.max(2.6, sk.zHalfLen * 0.58);
      const botR = topR * 2.05;

      for (let layer = 0; layer < 2; layer++) {
        const scaleR = layer === 0 ? 1.0 : 0.52;
        const rayGeo = new THREE.CylinderGeometry(
          topR * scaleR,
          botR * scaleR,
          beamLen + 4.0,
          16,
          1,
          true
        );
        const rayMesh = new THREE.Mesh(rayGeo, this.godrayBeamMat);
        rayMesh.position.copy(midPos);
        rayMesh.quaternion.setFromUnitVectors(
          new THREE.Vector3(0, -1, 0),
          beamVec.clone().normalize()
        );
        rayMesh.renderOrder = 5;
        rayMesh.userData.uniqueGeo = true;
        chunkGroup.add(rayMesh);
      }

      const poolGeo = new THREE.PlaneGeometry(botR * 2.5, botR * 2.9);
      poolGeo.rotateX(-Math.PI * 0.5);
      const poolMesh = new THREE.Mesh(poolGeo, this.dappledSunPoolMat);
      poolMesh.position.copy(floorPos);
      poolMesh.renderOrder = 3;
      poolMesh.userData.uniqueGeo = true;
      chunkGroup.add(poolMesh);

      for (let m = 0; m < 5; m++) {
        const ang = (m / 5) * Math.PI * 2 + rng() * 0.5;
        const mx = holeX + Math.cos(ang) * topR * 0.85;
        const mz = sk.zCenter + Math.sin(ang) * sk.zHalfLen * 0.72;
        const mossH = lerp(3.5, 7.5, rng());
        const mossGeo = new THREE.ConeGeometry(lerp(0.65, 1.35, rng()), mossH, 5);
        mossGeo.rotateX(Math.PI);
        const mossMesh = new THREE.Mesh(mossGeo, this.beardMossMat);
        mossMesh.position.set(mx, holeY - mossH * 0.45, mz);
        mossMesh.userData.uniqueGeo = true;
        chunkGroup.add(mossMesh);
      }
    }

    // Tiered Glowing Bracket Shelf-Fungi along the curved walls!
    for (let f = 0; f < 22; f++) {
      const fz = zStart - rng() * chunkLength;
      const sp = specSampler(fz);
      const side = f % 2 === 0 ? 1 : -1;
      const wallTheta = side > 0 ? lerp(0.08, 0.58, rng()) : lerp(Math.PI - 0.58, Math.PI - 0.08, rng());
      const wx = sp.centerX + Math.cos(wallTheta) * (VAULT_RADIUS_X - 1.4);
      const wy = sp.baseY - 1.5 + Math.sin(wallTheta) * (VAULT_RADIUS_Y - 1.4);

      const clusterCount = 2 + Math.floor(rng() * 3);
      for (let c = 0; c < clusterCount; c++) {
        const rad = lerp(2.2, 5.4, rng()) * (1.0 - c * 0.18);
        const conkGroup = new THREE.Group();
        conkGroup.position.set(wx, wy + c * 1.15, fz + (c - 1) * 1.6);
        conkGroup.rotation.y = side > 0 ? -Math.PI * 0.5 : Math.PI * 0.5;

        const cap = new THREE.Mesh(this.shelfConkGeo, this.shelfFungusTopMat);
        cap.scale.set(rad, lerp(0.8, 1.5, rng()), rad * 0.85);
        cap.castShadow = true;
        conkGroup.add(cap);

        const gills = new THREE.Mesh(this.shelfConkGeo, this.shelfFungusGlowMat);
        gills.scale.set(rad * 0.94, 0.45, rad * 0.8);
        gills.position.y = -0.18;
        conkGroup.add(gills);

        chunkGroup.add(conkGroup);
      }
    }

    // Fallen Inner-Bark Jump Ramps (let players vault right OVER low sticky cobweb tangles!)
    if (chunkIndex >= 0) {
      for (let r = 0; r < 2; r++) {
        const rz = zStart - (0.32 + r * 0.44) * chunkLength;
        if (Math.abs(rz) < 36) continue;
        const sp = specSampler(rz);
        const rx = sp.centerX + (r % 2 === 0 ? -7.5 : 7.5);
        const len = 13.5;
        const ay = groundSampler(rx, rz + len * 0.5) - 0.1;
        const by = groundSampler(rx, rz - len * 0.5) + 2.85;

        const rampGeo = new THREE.BoxGeometry(5.6, 0.7, len);
        const rampMesh = new THREE.Mesh(rampGeo, this.outerBarkVaultMat);
        rampMesh.position.set(rx, (ay + by) * 0.5 - 0.15, rz);
        rampMesh.rotation.x = Math.atan2(by - ay, len);
        rampMesh.castShadow = true;
        rampMesh.receiveShadow = true;
        rampMesh.userData.uniqueGeo = true;
        chunkGroup.add(rampMesh);

        rootBridges.push({
          id: allocBridgeId(),
          chunkIndex,
          minZ: rz - len * 0.5 - 2,
          maxZ: rz + len * 0.5 + 2,
          segments: [
            {
              ax: rx,
              ay,
              az: rz + len * 0.5,
              bx: rx,
              by,
              bz: rz - len * 0.5,
              deckHalfWidth: 2.8,
              thickness: 0.9,
            },
          ],
        });
      }
    }
  }

  /**
   * Spawns Live Growing Mushrooms that sprout & inflate out of the rotten heartwood floor!
   * - BOUNCE_CAP: Squishy golden-amber jelly dome that launches your buggy into the air over webs!
   * - SPORE_PUFFBALL: Swells up and ruptures in a glowing green spore cloud + shockwave!
   * - TOADSTOOL_PILLAR: Rapidly grows tall right out of floor cracks as a dynamic slalom obstacle!
   */
  private spawnLiveGrowingMushrooms(
    chunkGroup: THREE.Group,
    mushrooms: LiveGrowingMushroom[],
    obstacles: WorldObstacle[],
    chunkIndex: number,
    zStart: number,
    chunkLength: number,
    rng: () => number,
    specSampler: (z: number) => { centerX: number; halfWidth: number },
    groundSampler: (x: number, z: number) => number,
    allocObstacleId: () => number
  ): void {
    if (chunkIndex < 0) return;

    const count = 9;
    for (let i = 0; i < count; i++) {
      const mz = zStart - ((i + 0.16 + rng() * 0.55) / count) * chunkLength;
      if (Math.abs(mz) < 30) continue;

      const sp = specSampler(mz);
      const mx = sp.centerX + (rng() - 0.5) * (sp.halfWidth * 1.45);
      const my = groundSampler(mx, mz);

      const roll = i % 3;
      const kind: MushroomArchetype =
        roll === 0 ? 'BOUNCE_CAP' : roll === 1 ? 'SPORE_PUFFBALL' : 'TOADSTOOL_PILLAR';

      const group = new THREE.Group();
      group.position.set(mx, my - 0.15, mz);
      chunkGroup.add(group);

      let capRadius = 3.2;
      let fullHeight = 2.2;
      let stemMesh: THREE.Mesh;
      let capMesh: THREE.Mesh;
      let sporeCloudMesh: THREE.Mesh | undefined;
      let dynamicObstacle: WorldObstacle | undefined;

      if (kind === 'BOUNCE_CAP') {
        capRadius = lerp(2.8, 4.2, rng());
        fullHeight = lerp(1.5, 2.2, rng());

        stemMesh = new THREE.Mesh(this.mushStemGeo, this.mushroomStemMat);
        stemMesh.scale.set(capRadius * 0.45, fullHeight * 0.65, capRadius * 0.45);
        group.add(stemMesh);

        capMesh = new THREE.Mesh(this.mushDomeCapGeo, this.bounceCapMat);
        capMesh.position.y = fullHeight * 0.58;
        capMesh.scale.set(capRadius, fullHeight * 0.65, capRadius);
        capMesh.castShadow = true;
        group.add(capMesh);
      } else if (kind === 'SPORE_PUFFBALL') {
        capRadius = lerp(2.1, 3.3, rng());
        fullHeight = capRadius * 1.4;

        stemMesh = new THREE.Mesh(this.mushStemGeo, this.mushroomStemMat);
        stemMesh.scale.set(capRadius * 0.35, 0.5, capRadius * 0.35);
        group.add(stemMesh);

        capMesh = new THREE.Mesh(this.puffballGeo, this.puffballMat);
        capMesh.scale.setScalar(capRadius);
        capMesh.castShadow = true;
        group.add(capMesh);

        const cloudGeo = new THREE.SphereGeometry(1.0, 12, 10);
        sporeCloudMesh = new THREE.Mesh(cloudGeo, this.sporeBurstMat);
        sporeCloudMesh.position.y = capRadius * 0.8;
        sporeCloudMesh.visible = false;
        sporeCloudMesh.userData.uniqueGeo = true;
        group.add(sporeCloudMesh);
      } else {
        // Towering Toadstool Pillar shooting up out of the wood floor!
        capRadius = lerp(2.6, 3.8, rng());
        fullHeight = lerp(6.8, 11.2, rng());

        stemMesh = new THREE.Mesh(this.mushStemGeo, this.mushroomStemMat);
        stemMesh.scale.set(1.15, fullHeight, 1.15);
        stemMesh.castShadow = true;
        group.add(stemMesh);

        capMesh = new THREE.Mesh(this.mushDomeCapGeo, this.toadstoolCapMat);
        capMesh.position.y = fullHeight * 0.95;
        capMesh.scale.set(capRadius, capRadius * 0.52, capRadius);
        capMesh.castShadow = true;
        group.add(capMesh);

        // Glowing underside gills!
        const gillDisc = new THREE.Mesh(
          new THREE.CylinderGeometry(capRadius * 0.92, capRadius * 0.35, 0.25, 12),
          this.shelfFungusGlowMat
        );
        gillDisc.position.y = -0.08;
        gillDisc.userData.uniqueGeo = true;
        capMesh.add(gillDisc);

        dynamicObstacle = {
          id: allocObstacleId(),
          chunkIndex,
          kind: 'buttress_tree',
          position: new THREE.Vector3(mx, my, mz),
          radius: 0.1, // Grows live as the mushroom sprouts!
          height: 0.5,
          canShearVine: true,
        };
        obstacles.push(dynamicObstacle);
      }

      group.scale.setScalar(0.08); // Starts as tiny bud hidden in wood crack until triggered!

      mushrooms.push({
        id: this.nextMushId++,
        kind,
        position: new THREE.Vector3(mx, my, mz),
        growth: 0.08,
        growthSpeed: lerp(0.55, 0.95, rng()),
        triggered: false,
        triggerDist: lerp(65, 92, rng()),
        capRadius,
        fullHeight,
        squashBounce: 0,
        puffTimer: rng() * 3.0,
        burstActive: 0,
        group,
        stemMesh,
        capMesh,
        sporeCloudMesh,
        dynamicObstacle,
      });
    }
  }

  /**
   * Spawns Overhead Dripping Sticky Amber Sap Globs falling from ceiling cracks!
   */
  private spawnDrippingSapGlobs(
    chunkGroup: THREE.Group,
    sapGlobs: FallingSapGlob[],
    chunkIndex: number,
    zStart: number,
    chunkLength: number,
    rng: () => number,
    specSampler: (z: number) => { centerX: number; halfWidth: number; baseY: number },
    groundSampler: (x: number, z: number) => number
  ): void {
    if (chunkIndex < 0) return;

    const dripCount = 3;
    for (let d = 0; d < dripCount; d++) {
      const dz = zStart - ((d + 0.28 + rng() * 0.4) / dripCount) * chunkLength;
      if (Math.abs(dz) < 34) continue;

      const sp = specSampler(dz);
      const dx = sp.centerX + (rng() - 0.5) * (sp.halfWidth * 1.1);
      const floorY = groundSampler(dx, dz);
      const ceilY = sp.baseY + VAULT_RADIUS_Y - 5.5;

      const globMesh = new THREE.Mesh(this.sapDropGeo, this.sapGlobMat);
      globMesh.position.set(dx, ceilY, dz);
      chunkGroup.add(globMesh);

      const puddleRad = lerp(4.2, 6.2, rng());
      const pudGeo = new THREE.CircleGeometry(puddleRad, 16);
      pudGeo.rotateX(-Math.PI * 0.5);
      const puddleMesh = new THREE.Mesh(pudGeo, this.sapAmberWaterMat);
      puddleMesh.position.set(dx, floorY + 0.05, dz);
      puddleMesh.renderOrder = 2;
      puddleMesh.userData.uniqueGeo = true;
      chunkGroup.add(puddleMesh);

      sapGlobs.push({
        dripOrigin: new THREE.Vector3(dx, ceilY, dz),
        floorPos: new THREE.Vector3(dx, floorY, dz),
        currentY: ceilY,
        vy: 0,
        swellTimer: rng() * 2.2,
        swellDuration: lerp(1.6, 2.8, rng()),
        state: 'SWELLING',
        globMesh,
        puddleMesh,
        puddleRadius: puddleRad,
      });
    }
  }

  private spawnStationaryLogWorm(
    chunkGroup: THREE.Group,
    worms: StationaryLogWorm[],
    borePos: THREE.Vector3,
    emergeDir: THREE.Vector3,
    maxReach: number,
    baseRadius: number,
    rng: () => number
  ): void {
    const group = new THREE.Group();
    chunkGroup.add(group);

    const collar = new THREE.Mesh(this.boreCollarGeo, this.boreHoleCollarMat);
    collar.position.copy(borePos);
    collar.scale.setScalar(baseRadius * 0.95);
    collar.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), emergeDir);
    group.add(collar);

    const segmentMeshes: THREE.Mesh[] = [];
    for (let s = 0; s < WORM_SEGMENT_COUNT; s++) {
      const u = s / (WORM_SEGMENT_COUNT - 1);
      const segRad = baseRadius * lerp(1.08, 0.58, Math.pow(u, 1.3));
      const seg = new THREE.Mesh(this.wormSegGeo, this.wormFleshMat);
      seg.scale.set(segRad, segRad, segRad * 1.15);
      seg.castShadow = true;
      seg.position.copy(borePos);
      group.add(seg);
      segmentMeshes.push(seg);
    }

    const headGroup = new THREE.Group();
    headGroup.position.copy(borePos);
    const mandibleMeshes: THREE.Mesh[] = [];
    for (let m = 0; m < 4; m++) {
      const mHolder = new THREE.Group();
      mHolder.rotation.z = (m / 4) * Math.PI * 2 + Math.PI * 0.25;
      const jaw = new THREE.Mesh(this.wormMandibleGeo, this.wormHeadChitinMat);
      jaw.scale.setScalar(baseRadius * 0.95);
      mHolder.add(jaw);
      headGroup.add(mHolder);
      mandibleMeshes.push(jaw);
    }
    group.add(headGroup);

    const upRef = Math.abs(emergeDir.y) > 0.9 ? new THREE.Vector3(1, 0, 0) : new THREE.Vector3(0, 1, 0);
    const lateralTang = new THREE.Vector3().crossVectors(emergeDir, upRef).normalize();

    worms.push({
      id: this.nextWormId++,
      boreHolePos: borePos.clone(),
      emergeDir: emergeDir.clone().normalize(),
      lateralTang,
      maxReach,
      baseRadius,
      phase: rng() * Math.PI * 2,
      state: 'PEEKING',
      extension: 0.12,
      stateTimer: 0,
      strikeCooldown: 0,
      group,
      segmentMeshes,
      headGroup,
      mandibleMeshes,
      currentTipWorld: borePos.clone(),
    });
  }

  public populateGiantLogChunk(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    chunkLength: number,
    rng: () => number,
    groundSampler: (x: number, z: number) => number,
    specSampler: (z: number) => {
      centerX: number;
      halfWidth: number;
      baseY: number;
      slopeX: number;
    },
    obstacles: WorldObstacle[],
    rootBridges: CrisscrossRootBridge[],
    allocObstacleId: () => number,
    allocBridgeId: () => number
  ): GiantHollowLogChunkData {
    const worms: StationaryLogWorm[] = [];
    const webSnares: MessyWebSnareZone[] = [];
    const mushrooms: LiveGrowingMushroom[] = [];
    const sapGlobs: FallingSapGlob[] = [];

    // 1. Hollow Log Vault Shell, Skylight Godrays, Shelf-Fungi & Fallen Bark Jump-Ramps!
    this.buildHollowLogVaultAndGodrays(
      chunkGroup,
      chunkIndex,
      zStart,
      chunkLength,
      rng,
      specSampler,
      groundSampler,
      rootBridges,
      allocBridgeId
    );

    // 2. End-to-End, Wall-to-Wall Non-Orbweaver Messy Funnel/Tangle Spiderweb Network!
    this.buildWallToWallCobwebNetwork(
      chunkGroup,
      webSnares,
      chunkIndex,
      zStart,
      chunkLength,
      rng,
      specSampler,
      groundSampler
    );

    // 3. Live Sprouting & Growing Mushrooms (Bounce Caps, Explosive Spore Puffballs & Toadstool Pillars!)
    this.spawnLiveGrowingMushrooms(
      chunkGroup,
      mushrooms,
      obstacles,
      chunkIndex,
      zStart,
      chunkLength,
      rng,
      specSampler,
      groundSampler,
      allocObstacleId
    );

    // 4. Overhead Dripping Sticky Amber Sap Globs!
    this.spawnDrippingSapGlobs(
      chunkGroup,
      sapGlobs,
      chunkIndex,
      zStart,
      chunkLength,
      rng,
      specSampler,
      groundSampler
    );

    if (chunkIndex < 0) {
      return { worms, webSnares, mushrooms, sapGlobs };
    }

    // 5. Stationary Log-Emerging Parasite Worms (7 bore-holes per chunk!)
    const wormCount = 7;
    for (let w = 0; w < wormCount; w++) {
      const wz = zStart - ((w + 0.18 + rng() * 0.55) / wormCount) * chunkLength;
      if (Math.abs(wz) < 30) continue;

      const sp = specSampler(wz);
      const side = w % 2 === 0 ? -1 : 1;
      const mountRoll = rng();

      let borePos: THREE.Vector3;
      let emergeDir: THREE.Vector3;
      let maxReach: number;
      let baseRad: number;

      if (mountRoll < 0.65) {
        const wallX = sp.centerX + side * lerp(sp.halfWidth * 0.72, sp.halfWidth * 0.98, rng());
        const wallFloorY = groundSampler(wallX, wz);
        borePos = new THREE.Vector3(wallX, wallFloorY + lerp(1.6, 5.2, rng()), wz);
        emergeDir = new THREE.Vector3(-side * 0.92, lerp(-0.24, 0.08, rng()), (rng() - 0.5) * 0.28).normalize();
        maxReach = lerp(10.5, 15.2, rng());
        baseRad = lerp(0.78, 1.15, rng());
      } else if (mountRoll < 0.86) {
        const floorX = sp.centerX + side * lerp(5.5, sp.halfWidth * 0.55, rng());
        const floorY = groundSampler(floorX, wz);
        borePos = new THREE.Vector3(floorX, floorY - 0.15, wz);
        emergeDir = new THREE.Vector3(-side * 0.62, 0.74, (rng() - 0.5) * 0.25).normalize();
        maxReach = lerp(7.8, 11.2, rng());
        baseRad = lerp(0.72, 0.98, rng());
      } else {
        const ceilX = sp.centerX + (rng() - 0.5) * (sp.halfWidth * 0.65);
        const floorY = groundSampler(ceilX, wz);
        borePos = new THREE.Vector3(ceilX, floorY + lerp(9.5, 13.0, rng()), wz);
        emergeDir = new THREE.Vector3((rng() - 0.5) * 0.35, -0.92, (rng() - 0.5) * 0.25).normalize();
        maxReach = lerp(7.5, 10.8, rng());
        baseRad = lerp(0.76, 1.05, rng());
      }

      this.spawnStationaryLogWorm(chunkGroup, worms, borePos, emergeDir, maxReach, baseRad, rng);
    }

    return { worms, webSnares, mushrooms, sapGlobs };
  }

  /**
   * Steps chunk-local Stationary Parasite Worms, Messy Web Snares, Live Growing Mushrooms & Dripping Sap!
   */
  public stepChunkHazards(
    data: GiantHollowLogChunkData,
    elapsedTime: number,
    dt: number,
    focusPos: THREE.Vector3,
    vehiclePos: THREE.Vector3,
    vehicleVel: THREE.Vector3,
    throttleInput: number,
    steerInput: number,
    isBoosting: boolean,
    passiveObserver: boolean
  ): void {
    this.timeUniform.value = elapsedTime;

    // 1. LIVE GROWING MUSHROOMS!
    for (const m of data.mushrooms) {
      const distFocus = m.position.distanceTo(focusPos);
      if (!m.triggered && distFocus < m.triggerDist) {
        m.triggered = true;
      }

      if (m.triggered && m.growth < 1.0) {
        m.growth = Math.min(1.0, m.growth + m.growthSpeed * dt);
      }

      m.squashBounce = lerp(m.squashBounce, 0, clamp(10.0 * dt, 0, 1));

      // Smooth organic stem-first, cap-unfurl growth animation!
      const stemScale = smoothstep(0.05, 0.75, m.growth);
      const capUnfurl = smoothstep(0.22, 1.0, m.growth);
      const squashY = 1.0 - m.squashBounce * 0.42;
      const squashXZ = 1.0 + m.squashBounce * 0.32;

      m.group.scale.set(
        Math.max(0.08, capUnfurl * squashXZ),
        Math.max(0.08, stemScale * squashY),
        Math.max(0.08, capUnfurl * squashXZ)
      );

      if (m.dynamicObstacle) {
        m.dynamicObstacle.radius = 1.15 * capUnfurl;
        m.dynamicObstacle.height = m.fullHeight * stemScale;
      }

      if (m.kind === 'BOUNCE_CAP' && !passiveObserver && m.growth > 0.45) {
        const dx = vehiclePos.x - m.position.x;
        const dz = vehiclePos.z - m.position.z;
        const horizDist = Math.hypot(dx, dz);
        const capTopY = m.position.y + m.fullHeight * stemScale;
        if (
          horizDist < m.capRadius * capUnfurl + 1.0 &&
          Math.abs(vehiclePos.y - capTopY) < 2.2 &&
          vehicleVel.y < 6.0
        ) {
          // Springy fungal trampoline launch right over cobwebs & floor worms!
          vehicleVel.y = 11.8;
          m.squashBounce = 1.0;
          if (this.onMushroomBounce) this.onMushroomBounce(m.position.clone());
        }
      } else if (m.kind === 'SPORE_PUFFBALL' && m.growth > 0.75) {
        m.puffTimer += dt;
        // Swell rhythmically before rupturing (or burst immediately if clipped by vehicle!)
        const carHit =
          !passiveObserver &&
          Math.hypot(vehiclePos.x - m.position.x, vehiclePos.z - m.position.z) <
            m.capRadius * capUnfurl + 1.3;

        if (m.burstActive <= 0 && (carHit || m.puffTimer > 4.2)) {
          m.burstActive = 0.01;
          m.puffTimer = 0;
          m.squashBounce = 0.85;
          if (carHit) {
            const push = new THREE.Vector3(
              vehiclePos.x - m.position.x,
              0.45,
              vehiclePos.z - m.position.z
            ).normalize();
            vehicleVel.addScaledVector(push, 8.5);
            if (this.onPuffballBurst) this.onPuffballBurst(m.position.clone());
          }
        }

        if (m.burstActive > 0) {
          m.burstActive += dt * 1.45;
          if (m.sporeCloudMesh) {
            m.sporeCloudMesh.visible = true;
            m.sporeCloudMesh.scale.setScalar(m.capRadius * (1.0 + m.burstActive * 3.4));
          }
          if (m.burstActive >= 1.0) {
            m.burstActive = 0;
            if (m.sporeCloudMesh) m.sporeCloudMesh.visible = false;
            // Re-sprouts fresh puffball!
            m.growth = 0.25;
          }
        } else {
          const pulse = 1.0 + Math.sin(m.puffTimer * 6.5) * 0.08;
          m.capMesh.scale.setScalar(m.capRadius * pulse);
        }
      }
    }

    // 2. DRIPPING STICKY AMBER SAP GLOBS!
    for (const sg of data.sapGlobs) {
      if (sg.state === 'SWELLING') {
        sg.swellTimer += dt;
        const u = clamp(sg.swellTimer / sg.swellDuration, 0, 1);
        sg.globMesh.visible = true;
        sg.globMesh.position.copy(sg.dripOrigin);
        sg.globMesh.scale.setScalar(0.35 + u * 1.15);
        if (u >= 1.0) {
          sg.state = 'FALLING';
          sg.currentY = sg.dripOrigin.y;
          sg.vy = -2.0;
        }
      } else if (sg.state === 'FALLING') {
        sg.vy -= 22.0 * dt;
        sg.currentY += sg.vy * dt;
        sg.globMesh.position.y = sg.currentY;
        if (sg.currentY <= sg.floorPos.y + 0.4) {
          sg.state = 'SPLAT_COOLDOWN';
          sg.swellTimer = 0;
          sg.globMesh.visible = false;
        }
      } else {
        sg.swellTimer += dt;
        if (sg.swellTimer > 1.2) {
          sg.state = 'SWELLING';
          sg.swellTimer = 0;
        }
      }

      // Sticky viscous resin drag if driving through the amber sap pool!
      if (!passiveObserver) {
        const dSap = Math.hypot(vehiclePos.x - sg.floorPos.x, vehiclePos.z - sg.floorPos.z);
        if (dSap < sg.puddleRadius && Math.abs(vehiclePos.y - sg.floorPos.y) < 1.8) {
          const resinDrag = Math.exp(-2.6 * dt);
          vehicleVel.x *= resinDrag;
          vehicleVel.z *= resinDrag;
        }
      }
    }

    // 3. STATIONARY LOG-EMERGING PARASITE WORMS!
    for (const w of data.worms) {
      w.strikeCooldown = Math.max(0, w.strikeCooldown - dt);
      w.stateTimer += dt;

      const distToFocus = w.boreHolePos.distanceTo(focusPos);
      const distToVehicle = w.boreHolePos.distanceTo(vehiclePos);

      let targetExt = 0.14;
      let jawOpen = 0.22;

      if (passiveObserver) {
        const nearCam = smoothstep(75, 22, distToFocus);
        targetExt = 0.16 + 0.42 * nearCam + 0.08 * Math.sin(elapsedTime * 1.9 + w.phase);
        jawOpen = 0.25 + 0.25 * Math.sin(elapsedTime * 3.2 + w.phase);
        w.state = 'PEEKING';
      } else {
        if (w.state === 'LATCHED') {
          targetExt = 0.96;
          jawOpen = 0.12;
          if (isBoosting || distToVehicle > w.maxReach + 4.2 || w.stateTimer > 1.05) {
            w.state = 'RETRACTING';
            w.stateTimer = 0;
            w.strikeCooldown = 2.4;
          } else {
            const pullDir = new THREE.Vector3().subVectors(w.boreHolePos, vehiclePos).setY(0);
            if (pullDir.lengthSq() > 0.01) {
              pullDir.normalize();
              vehicleVel.addScaledVector(pullDir, 11.5 * dt);
            }
            const dragDamp = Math.exp(-1.55 * dt);
            vehicleVel.x *= dragDamp;
            vehicleVel.z *= dragDamp;
          }
        } else if (w.state === 'RETRACTING') {
          targetExt = 0.15;
          jawOpen = 0.18;
          if (w.stateTimer > 1.1) {
            w.state = 'PEEKING';
            w.stateTimer = 0;
          }
        } else if (distToVehicle < 64.0 && vehiclePos.z > w.boreHolePos.z - 14.0) {
          w.state = 'LUNGING';
          targetExt = 1.0;
          jawOpen = 0.78 + 0.18 * Math.sin(elapsedTime * 12.0 + w.phase);
        } else {
          w.state = 'PEEKING';
          targetExt = 0.16 + 0.06 * Math.sin(elapsedTime * 2.2 + w.phase);
        }
      }

      const extRate = w.state === 'LUNGING' ? 6.8 : w.state === 'RETRACTING' ? 3.8 : 2.6;
      w.extension = lerp(w.extension, targetExt, clamp(extRate * dt, 0, 1));

      const activeLen = w.extension * w.maxReach;
      const toCar = new THREE.Vector3().subVectors(vehiclePos, w.boreHolePos);
      const canAimAtCar =
        !passiveObserver && (w.state === 'LUNGING' || w.state === 'LATCHED') && toCar.lengthSq() > 1.0;
      if (canAimAtCar) toCar.normalize();

      let prevPos = w.boreHolePos.clone();
      for (let s = 0; s < WORM_SEGMENT_COUNT; s++) {
        const u = (s + 1) / WORM_SEGMENT_COUNT;
        const distOut = u * activeLen;

        const dir = w.emergeDir.clone();
        if (canAimAtCar) {
          const dotOut = toCar.dot(w.emergeDir);
          if (dotOut > -0.15) dir.lerp(toCar, u * 0.68).normalize();
        }

        const writheAmp = (0.28 + 0.55 * u) * w.extension;
        const writheLat = Math.sin(elapsedTime * 5.8 - u * 4.5 + w.phase) * writheAmp;
        const writheVert = Math.cos(elapsedTime * 4.6 - u * 3.8 + w.phase) * writheAmp * 0.55;

        const segPos = w.boreHolePos
          .clone()
          .addScaledVector(dir, distOut)
          .addScaledVector(w.lateralTang, writheLat);
        segPos.y += writheVert;

        const segMesh = w.segmentMeshes[s];
        segMesh.position.copy(segPos);

        const segTangent = new THREE.Vector3().subVectors(segPos, prevPos);
        if (segTangent.lengthSq() > 1e-5) {
          segTangent.normalize();
          segMesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), segTangent);
        }

        if (!passiveObserver && w.state === 'LUNGING' && w.strikeCooldown <= 0 && u > 0.55) {
          if (segPos.distanceTo(vehiclePos) < w.baseRadius + 1.85) {
            w.state = 'LATCHED';
            w.stateTimer = 0;
            vehicleVel.multiplyScalar(0.76);
            if (this.onWormStrikeVehicle) this.onWormStrikeVehicle(segPos.clone());
          }
        }

        if (s === WORM_SEGMENT_COUNT - 1) {
          w.currentTipWorld.copy(segPos);
          w.headGroup.position.copy(segPos);
          if (segTangent.lengthSq() > 1e-5) {
            w.headGroup.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), segTangent);
          }
        }
        prevPos.copy(segPos);
      }

      for (const jaw of w.mandibleMeshes) jaw.rotation.x = jawOpen;
    }

    // 4. MESSY WEB SNARE ZONES — PLAYERS HITTING THE WEB ACTUALLY GET STUCK!
    for (const ws of data.webSnares) {
      if (ws.tearProgress > 0 && this.activeSnare !== ws) {
        ws.tearProgress = Math.max(0, ws.tearProgress - 0.1 * dt);
      }

      if (
        !passiveObserver &&
        !this.activeSnare &&
        this.snareImmunityTimer <= 0 &&
        ws.tearProgress < 0.45
      ) {
        const dx = Math.abs(vehiclePos.x - ws.anchorCenter.x);
        const dz = Math.abs(vehiclePos.z - ws.anchorCenter.z);
        const dy = vehiclePos.y - (ws.anchorCenter.y - 1.2);

        // Note: jumping HIGH over the snare via a Bark Ramp or Bounce Mushroom clears trapHeightY!
        if (dx < ws.halfWidthX + 0.9 && dz < ws.halfDepthZ && dy >= -1.0 && dy < ws.trapHeightY * 0.78) {
          // TRAPPED / STUCK IN THE STICKY SPIDERWEB NETWORK!
          this.activeSnare = ws;
          this.snareEscapeProgress = 0;
          vehicleVel.multiplyScalar(0.16);
          if (this.onVehicleStuckInWeb) {
            this.onVehicleStuckInWeb(vehiclePos.clone());
          }
        }
      }

      if (this.activeSnare === ws) {
        ws.stretchVec.subVectors(vehiclePos, ws.anchorCenter).clampLength(0, 4.2);
        ws.snareGroup.position.set(
          ws.anchorCenter.x + ws.stretchVec.x * 0.38,
          ws.anchorCenter.y - 1.2,
          ws.anchorCenter.z + ws.stretchVec.z * 0.48
        );
      } else {
        ws.stretchVec.lerp(new THREE.Vector3(0, 0, 0), clamp(6.0 * dt, 0, 1));
        ws.snareGroup.position.set(ws.anchorCenter.x, ws.anchorCenter.y - 1.2, ws.anchorCenter.z);
      }

      ws.snareGroup.scale.set(
        1.0 + ws.tearProgress * 0.25,
        clamp(1.0 - ws.tearProgress * 0.72, 0.22, 1.0),
        1.0
      );
      ws.snareGroup.visible = ws.tearProgress < 0.9;
    }
  }

  /**
   * Steps global hollow-log spores AND updates the live "STUCK IN WEB" elastic tether snare on the vehicle!
   */
  public stepGlobalLogAtmosphere(
    active: boolean,
    focusPos: THREE.Vector3,
    vehiclePos: THREE.Vector3,
    vehicleVel: THREE.Vector3,
    throttleInput: number,
    steerInput: number,
    isBoosting: boolean,
    passiveObserver: boolean,
    dt: number
  ): void {
    this.atmosphereFxGroup.visible = active;
    if (!active || passiveObserver) {
      if (this.activeSnare) this.clearActiveWebSnare();
      if (!active) return;
    }

    this.snareImmunityTimer = Math.max(0, this.snareImmunityTimer - dt);

    // Handle active STUCK-IN-WEB snare physics & visual stretching silk cables!
    if (this.activeSnare && !passiveObserver) {
      const ws = this.activeSnare;
      const toAnchor = new THREE.Vector3().subVectors(ws.anchorCenter, vehiclePos);
      const dist = toAnchor.length();

      // Strong sticky elastic silk tension holding the vehicle pinned inside the web!
      if (dist > 0.05) {
        vehicleVel.addScaledVector(toAnchor.normalize(), Math.min(dist * 16.5, 48.0) * dt);
      }
      const stickyHold = Math.exp(-9.5 * dt);
      vehicleVel.x *= stickyHold;
      vehicleVel.z *= stickyHold;

      // Revving throttle, rocking steering (A/D), or hitting SHIFT Boost strains & snaps the silk!
      const struggleRate =
        0.38 +
        Math.abs(throttleInput) * 0.62 +
        Math.abs(steerInput) * 0.35 +
        (isBoosting ? 1.85 : 0);
      this.snareEscapeProgress += struggleRate * dt;

      // Render 8 taut stretching white silk cables anchoring the trapped buggy to the log walls & floor!
      for (let i = 0; i < SNARE_TETHER_COUNT; i++) {
        const mesh = this.snareTetherMeshes[i];
        const anchorPt = ws.wallAnchors[i % ws.wallAnchors.length];
        const carAttach = new THREE.Vector3(
          vehiclePos.x + ((i % 2 === 0 ? -0.8 : 0.8)),
          vehiclePos.y + (i < 4 ? 0.85 : 0.35),
          vehiclePos.z + (i % 3 - 1) * 1.1
        );
        const delta = new THREE.Vector3().subVectors(anchorPt, carAttach);
        const len = delta.length();
        mesh.visible = len > 0.2;
        if (mesh.visible) {
          mesh.position.copy(carAttach).addScaledVector(delta, 0.5);
          mesh.scale.set(1.0, len, 1.0);
          mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), delta.normalize());
        }
      }

      if (this.snareEscapeProgress >= 1.0 || dist > 18.0) {
        // SNAP FREE FROM THE COBWEB!
        ws.tearProgress = 1.0;
        this.activeSnare = null;
        this.snareEscapeProgress = 0;
        this.snareImmunityTimer = 1.1;
        for (const m of this.snareTetherMeshes) m.visible = false;
        // Surge forward as the taut silk snaps!
        vehicleVel.z -= 6.5;
        if (this.onVehicleBreakFreeWeb) {
          this.onVehicleBreakFreeWeb(vehiclePos.clone());
        }
      }
    } else {
      for (const m of this.snareTetherMeshes) m.visible = false;
    }

    // Update floating golden dust & bioluminescent green fungal spores
    const halfX = 42;
    const halfZ = 82;
    for (let i = 0; i < this.sporeCount; i++) {
      const ix = i * 3;
      let px = this.sporePositions[ix] + this.sporeVelocities[ix] * dt;
      let py = this.sporePositions[ix + 1] + this.sporeVelocities[ix + 1] * dt;
      let pz = this.sporePositions[ix + 2] + this.sporeVelocities[ix + 2] * dt;

      if (px - focusPos.x > halfX) px -= halfX * 2;
      else if (px - focusPos.x < -halfX) px += halfX * 2;

      if (pz - focusPos.z > halfZ) pz -= halfZ * 2;
      else if (pz - focusPos.z < -halfZ) pz += halfZ * 2;

      if (py - focusPos.y > 26) py -= 34;
      else if (py - focusPos.y < -8) py += 34;

      this.sporePositions[ix] = px;
      this.sporePositions[ix + 1] = py;
      this.sporePositions[ix + 2] = pz;
    }
    (this.sporePoints.geometry.attributes.position as THREE.BufferAttribute).needsUpdate = true;
  }
}

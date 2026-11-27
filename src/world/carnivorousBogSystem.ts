import * as THREE from 'three';
import { clamp, fbm2D, lerp, mulberry32, ridge2D, smoothstep } from '../math/noise';
import { ProceduralTextureLibrary } from '../graphics/proceduralTextures';
import { WorldObstacle } from './trailCorridor';

export interface BogFloatingPlatform {
  id: number;
  kind: 'VICTORIA_LILYPAD' | 'PEAT_ISLAND';
  x: number;
  z: number;
  radius: number;
  baseY: number;
  curY: number;
  velY: number;
  pitchX: number;
  velPitchX: number;
  rollZ: number;
  velRollZ: number;
  dwellTime: number; // How long the 4x4 has lingered on this pad -> submerges if too long!
  submerged: boolean;
  bobPhase: number;
  yaw: number;
  mesh: THREE.Group;
  rippleRing: THREE.Mesh;
  rippleScale: number;
  rippleOpacity: number;
}

export type FlytrapState =
  | 'POISED_AMBUSH'
  | 'TRIGGER_COIL'
  | 'BALLISTIC_SNAP'
  | 'DIGESTIVE_WRITHE'
  | 'TURGOR_REOPEN';

export interface ColossalFlytrap {
  id: number;
  basePos: THREE.Vector3;
  restAimYaw: number;
  curAimYaw: number;
  scale: number;
  stalkLength: number;
  state: FlytrapState;
  stateTimer: number;
  ambientCycleTimer: number; // Periodic non-player insect-catch snaps down-course!
  isAmbientSnap: boolean;

  // Live articulated S-curve stalk & bivalve jaw state
  lobeOpenAngle: number; // rad per lobe (0.98 = cocked wide open, 0.05 = clamped tight!)
  lungeForward: number; // 0 = poised, -1.4m = coiled back, +4.4m = ballistic strike!
  headDropY: number;
  headPosWorld: THREE.Vector3;

  stalkGeo: THREE.BufferGeometry;
  stalkMesh: THREE.Mesh;
  jawHeadGroup: THREE.Group;
  leftLobeGroup: THREE.Group;
  rightLobeGroup: THREE.Group;
  clampedVehicleTimer: number;
}

export interface CurlingSundewStalk {
  id: number;
  basePos: THREE.Vector3;
  aimYaw: number;
  height: number;
  curlProgress: number; // 0 = tall graceful shepherd's-crook arch, 1 = tight logarithmic spiral!
  targetCurl: number;
  holdTimer: number;
  ambientTimer: number;
  swayPhase: number;
  stemGeo: THREE.BufferGeometry;
  stemMesh: THREE.Mesh;
  glandInstancedMesh: THREE.InstancedMesh;
  tipWorldPos: THREE.Vector3;
}

export interface PitcherGrappleAnchor {
  id: number;
  chunkIndex: number;
  restPos: THREE.Vector3;
  worldPos: THREE.Vector3;
  dipY: number;
  dipVelY: number;
  swayPhase: number;
  pitcherGroup: THREE.Group;
  haloMesh: THREE.Mesh;
}

export interface CarnivorousBogChunkData {
  chunkIndex: number;
  zStart: number;
  platforms: BogFloatingPlatform[];
  flytraps: ColossalFlytrap[];
  sundews: CurlingSundewStalk[];
  pitcherAnchors: PitcherGrappleAnchor[];
}

const LAGOON_WATER_Y = -0.38;
const STALK_RINGS = 10;
const STALK_SIDES = 7;
const SUNDEW_SEGS = 14;
const SUNDEW_SIDES = 6;
const SUNDEW_GLANDS = 26;

export class CarnivorousBogBuilder {
  public readonly lilyPadTopMat: THREE.MeshStandardMaterial;
  public readonly lilyPadUndersideMat: THREE.MeshStandardMaterial;
  public readonly peatIslandMat: THREE.MeshStandardMaterial;
  public readonly flytrapOuterMat: THREE.MeshStandardMaterial;
  public readonly flytrapDigestiveMat: THREE.MeshStandardMaterial;
  public readonly flytrapTeethMat: THREE.MeshStandardMaterial;
  public readonly petioleStalkMat: THREE.MeshStandardMaterial;
  public readonly sundewStemMat: THREE.MeshStandardMaterial;
  public readonly sundewDewMat: THREE.MeshStandardMaterial;
  public readonly pitcherBodyMat: THREE.MeshStandardMaterial;
  public readonly pitcherCollarMat: THREE.MeshStandardMaterial;
  public readonly bogBoughMat: THREE.MeshStandardMaterial;
  public readonly acidLagoonWaterMat: THREE.ShaderMaterial;
  public readonly rippleRingMat: THREE.MeshBasicMaterial;
  public readonly grappleHaloMat: THREE.MeshBasicMaterial;
  public readonly grappleTendrilMat: THREE.MeshStandardMaterial;
  public readonly swampSunbeamMat: THREE.ShaderMaterial;

  // Global bog atmosphere + live 3D [LMB] Pitcher-Tendril Grapple Rope!
  public readonly atmosphereFxGroup = new THREE.Group();
  private sporePoints!: THREE.Points;
  private sporePositions!: Float32Array;
  private sporeVelocities!: Float32Array;
  private readonly sporeCount = 1150;

  private grappleRopeGeo!: THREE.BufferGeometry;
  private grappleRopeMesh!: THREE.Mesh;
  public activeGrappleAnchor: PitcherGrappleAnchor | null = null;
  public grappleRestLength = 0;

  // Shared botanical geometries
  private readonly lilyPadDeckGeo: THREE.BufferGeometry;
  private readonly lilyPadRibsGeo: THREE.BufferGeometry;
  private readonly peatIslandGeos: THREE.BufferGeometry[] = [];
  private readonly leftFlytrapLobeGeo: THREE.BufferGeometry;
  private readonly rightFlytrapLobeGeo: THREE.BufferGeometry;
  private readonly marginalSpineGeo: THREE.BufferGeometry;
  private readonly basalLeafGeo: THREE.BufferGeometry;
  private readonly sundewGlandGeo: THREE.BufferGeometry;
  private readonly nepenthesPitcherGeo: THREE.BufferGeometry;
  private readonly nepenthesCollarGeo: THREE.BufferGeometry;
  private readonly nepenthesLidGeo: THREE.BufferGeometry;
  private readonly rippleGeo: THREE.BufferGeometry;
  private readonly haloRingGeo: THREE.BufferGeometry;

  private readonly timeUniform = { value: 0 };
  private nextPlatformId = 1;
  private nextFlytrapId = 1;
  private nextSundewId = 1;
  private nextPitcherId = 1;
  private sundewMucilageTimer = 0;
  private flytrapClampCooldown = 0;

  private readonly _dummy = new THREE.Object3D();
  private readonly _vA = new THREE.Vector3();
  private readonly _vB = new THREE.Vector3();
  private readonly _vC = new THREE.Vector3();
  private readonly _vD = new THREE.Vector3();

  public onFlytrapSnapShut?: (worldPos: THREE.Vector3, caughtVehicle: boolean) => void;
  public onVehicleBreakFreeFlytrap?: (worldPos: THREE.Vector3) => void;
  public onSundewMucilageHit?: (worldPos: THREE.Vector3) => void;
  public onLilyPadSubmerge?: (worldPos: THREE.Vector3) => void;
  public onPitcherGrappleLatch?: (worldPos: THREE.Vector3) => void;
  public onPitcherGrappleRelease?: (worldPos: THREE.Vector3) => void;

  constructor(textures: ProceduralTextureLibrary) {
    // 1. Victoria amazonica Giant Lily-Pad Waxy Leaf Blade & Crimson Structural Ribs
    this.lilyPadTopMat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      map: textures.vineStem.map,
      normalMap: textures.vineStem.normalMap,
      normalScale: new THREE.Vector2(0.65, 0.65),
      roughness: 0.24,
      metalness: 0.06,
      vertexColors: true,
      side: THREE.DoubleSide,
    });

    this.lilyPadUndersideMat = new THREE.MeshStandardMaterial({
      color: 0x6b1d26,
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      normalScale: new THREE.Vector2(0.85, 0.85),
      roughness: 0.42,
      metalness: 0.05,
    });

    // 2. Floating Sphagnum Peat-Moss Island
    this.peatIslandMat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      map: textures.terrainDetail.map,
      normalMap: textures.terrainDetail.normalMap,
      normalScale: new THREE.Vector2(1.15, 1.15),
      roughness: 0.82,
      metalness: 0.02,
      vertexColors: true,
      side: THREE.DoubleSide,
    });

    // 3. Botanical Dionaea Flytrap Materials (Leathery veined outer cuticle + glistening blood-crimson interior!)
    this.flytrapOuterMat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      map: textures.vineStem.map,
      normalMap: textures.vineStem.normalMap,
      normalScale: new THREE.Vector2(0.65, 0.65),
      roughness: 0.32,
      metalness: 0.05,
      vertexColors: true,
      side: THREE.DoubleSide,
    });

    this.flytrapDigestiveMat = new THREE.MeshStandardMaterial({
      color: 0x8a1122,
      normalMap: textures.vineStem.normalMap,
      normalScale: new THREE.Vector2(0.45, 0.45),
      roughness: 0.11, // Wet glistening digestive nectar sheen!
      metalness: 0.08,
      emissive: new THREE.Color(0x2d0309),
      emissiveIntensity: 0.38,
    });

    this.flytrapTeethMat = new THREE.MeshStandardMaterial({
      color: 0xd9d29c, // Bone-chartreuse interlocking marginal cilia spines!
      roughness: 0.26,
      metalness: 0.06,
      emissive: new THREE.Color(0x2c1b08),
      emissiveIntensity: 0.2,
    });

    this.petioleStalkMat = new THREE.MeshStandardMaterial({
      color: 0x3b5922,
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      normalScale: new THREE.Vector2(0.75, 0.75),
      roughness: 0.44,
      metalness: 0.04,
    });

    // 4. Drosera Sundew Tentacle & Glistening Ruby Mucilage Dewdrops
    this.sundewStemMat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      roughness: 0.28,
      metalness: 0.06,
      vertexColors: true,
    });

    this.sundewDewMat = new THREE.MeshStandardMaterial({
      color: 0xff3b24,
      roughness: 0.04, // Mirror-wet sticky mucilage jewel!
      metalness: 0.18,
      transparent: true,
      opacity: 0.9,
      emissive: new THREE.Color(0xc8220c),
      emissiveIntensity: 0.68,
    });

    // 5. Nepenthes Rajah Giant Hanging Pitcher Traps
    this.pitcherBodyMat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      normalScale: new THREE.Vector2(0.65, 0.65),
      roughness: 0.26,
      metalness: 0.06,
      vertexColors: true,
      side: THREE.DoubleSide,
    });

    this.pitcherCollarMat = new THREE.MeshStandardMaterial({
      color: 0x871824,
      normalMap: textures.bark.normalMap,
      normalScale: new THREE.Vector2(1.2, 1.2),
      roughness: 0.16, // Nectar-slick ribbed peristome lip!
      metalness: 0.1,
      emissive: new THREE.Color(0x2b0408),
      emissiveIntensity: 0.35,
    });

    this.bogBoughMat = new THREE.MeshStandardMaterial({
      color: 0x2c2419,
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      normalScale: new THREE.Vector2(1.3, 1.3),
      roughness: 0.88,
      metalness: 0.02,
    });

    // 6. Acid-Green Bioluminescent Digestive Bog Lagoon Water Shader!
    this.acidLagoonWaterMat = new THREE.ShaderMaterial({
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

        float bogHash(vec2 p) {
          return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
        }
        float bogNoise(vec2 p) {
          vec2 i = floor(p);
          vec2 f = fract(p);
          f = f * f * (3.0 - 2.0 * f);
          return mix(
            mix(bogHash(i), bogHash(i + vec2(1.0, 0.0)), f.x),
            mix(bogHash(i + vec2(0.0, 1.0)), bogHash(i + vec2(1.0, 1.0)), f.x),
            f.y
          );
        }

        void main() {
          vec2 uv = vWorldPos.xz * 0.14;
          float n1 = bogNoise(uv + vec2(uTime * 0.06, -uTime * 0.08));
          float n2 = bogNoise(uv * 2.7 - vec2(uTime * 0.09, uTime * 0.05));
          float algaeSwirl = bogNoise(uv * 0.65 + vec2(n1 * 0.8, n2 * 0.8));

          vec3 darkPeatDepth = vec3(0.04, 0.11, 0.05);
          vec3 murkyOlive = vec3(0.14, 0.30, 0.10);
          vec3 acidChartreuse = vec3(0.44, 0.72, 0.16);
          vec3 gasShimmer = vec3(0.72, 0.94, 0.34);

          vec3 col = mix(darkPeatDepth, murkyOlive, smoothstep(0.22, 0.75, algaeSwirl));
          float duckweed = smoothstep(0.58, 0.84, n1 * 0.6 + n2 * 0.4);
          col = mix(col, acidChartreuse, duckweed * 0.62);

          float caustic = smoothstep(0.84, 0.98, 0.5 + 0.5 * sin(vWorldPos.x * 1.6 + n1 * 5.0 + uTime * 1.4));
          col = mix(col, gasShimmer, caustic * 0.28);

          gl_FragColor = vec4(col, 1.0);
        }
      `,
    });

    this.rippleRingMat = new THREE.MeshBasicMaterial({
      color: 0x9be846,
      transparent: true,
      opacity: 0.45,
      side: THREE.DoubleSide,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });

    this.grappleHaloMat = new THREE.MeshBasicMaterial({
      color: 0xb4ff4a,
      transparent: true,
      opacity: 0.55,
      side: THREE.DoubleSide,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });

    this.grappleTendrilMat = new THREE.MeshStandardMaterial({
      color: 0x6b8f28,
      roughness: 0.32,
      metalness: 0.08,
      emissive: new THREE.Color(0x365910),
      emissiveIntensity: 0.45,
    });

    this.swampSunbeamMat = new THREE.ShaderMaterial({
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
          float vFade = smoothstep(0.0, 0.16, vUv.y) * (0.35 + 0.65 * vUv.y);
          float shimmer = 0.76 + 0.24 * sin(vUv.x * 18.0 + uTime * 0.7);
          vec3 col = mix(vec3(0.56, 0.84, 0.28), vec3(0.92, 0.98, 0.65), vUv.y);
          gl_FragColor = vec4(col, softEdge * vFade * shimmer * 0.16);
        }
      `,
    });

    // Build shared botanical geometries
    const lilyGeos = this.createVictoriaLilyPadGeometries();
    this.lilyPadDeckGeo = lilyGeos.deckGeo;
    this.lilyPadRibsGeo = lilyGeos.ribsGeo;

    for (let i = 0; i < 3; i++) {
      this.peatIslandGeos.push(this.createFloatingPeatIslandGeo(401 + i * 73));
    }

    this.leftFlytrapLobeGeo = this.createBotanicalFlytrapLobeGeo(-1);
    this.rightFlytrapLobeGeo = this.createBotanicalFlytrapLobeGeo(1);
    this.marginalSpineGeo = this.createCurvedMarginalSpineGeo();
    this.basalLeafGeo = this.createBasalPhyllodiaLeafGeo();
    this.sundewGlandGeo = this.createSundewGlandDropletGeo();

    const pitcherGeos = this.createNepenthesPitcherGeometries();
    this.nepenthesPitcherGeo = pitcherGeos.bodyGeo;
    this.nepenthesCollarGeo = pitcherGeos.collarGeo;
    this.nepenthesLidGeo = pitcherGeos.lidGeo;

    const rGeo = new THREE.RingGeometry(0.86, 1.0, 28);
    rGeo.rotateX(-Math.PI * 0.5);
    this.rippleGeo = rGeo;

    this.haloRingGeo = new THREE.RingGeometry(1.15, 1.55, 24);

    this.initAtmosphereAndGrappleRope();
  }

  /**
   * Sculpted 3D Victoria amazonica Giant Water-Platter Lily Pad (unit radius = 1.0):
   * - Flat waxy leaf blade with embossed radial vascular veins
   * - Upturned fluted vertical outer lip wall with 2 opposite V-shaped drainage notches!
   * - Architectural underside crimson structural rib girders!
   */
  private createVictoriaLilyPadGeometries(): {
    deckGeo: THREE.BufferGeometry;
    ribsGeo: THREE.BufferGeometry;
  } {
    const radialSegs = 44;
    const ringSegs = 7;
    // Radii from 0.0 -> 0.92 (flat veined deck) -> 0.98 (upturned inner wall) -> 1.01 (lip crest) -> 1.02 (outer skirt)
    const ringRadii = [0.0, 0.25, 0.52, 0.76, 0.91, 0.97, 1.0, 1.015];
    const ringHeights = [0.015, 0.008, 0.004, 0.0, 0.006, 0.068, 0.072, -0.055];

    const cols = radialSegs + 1;
    const rows = ringSegs + 1;
    const positions = new Float32Array(cols * rows * 3);
    const colors = new Float32Array(cols * rows * 3);
    const uvs = new Float32Array(cols * rows * 2);
    const indices: number[] = [];

    const colCenter = new THREE.Color(0x3f7a2d);
    const colBlade = new THREE.Color(0x2d5e23);
    const colVein = new THREE.Color(0x8cb536);
    const colCrimsonLip = new THREE.Color(0x87212b);
    const tmp = new THREE.Color();

    let pPtr = 0;
    let uvPtr = 0;
    for (let r = 0; r < rows; r++) {
      const baseRad = ringRadii[r];
      const baseH = ringHeights[r];

      for (let c = 0; c < cols; c++) {
        const theta = (c / radialSegs) * Math.PI * 2;
        // Two V-shaped drainage notches at theta = 0 and theta = PI, plus primary sinus cleft!
        const notchDist0 = Math.abs(Math.atan2(Math.sin(theta), Math.cos(theta)));
        const notchDistPi = Math.abs(Math.atan2(Math.sin(theta - Math.PI), Math.cos(theta - Math.PI)));
        const notchFactor =
          smoothstep(0.13, 0.0, notchDist0) + smoothstep(0.11, 0.0, notchDistPi) * 0.85;

        // Subtle organic fluted leaf margin & raised radial vascular veins!
        const veinWave = Math.pow(0.5 + 0.5 * Math.cos(theta * 16.0), 3.0);
        const rad = baseRad * (1.0 - notchFactor * (r >= 4 ? 0.08 : 0.02));
        let y = baseH;
        if (r >= 5 && r <= 6) {
          // Lower the upturned rim wall cleanly at the drainage notches!
          y = lerp(baseH, 0.005, clamp(notchFactor, 0, 1));
        } else if (r >= 1 && r <= 4) {
          y += veinWave * 0.011;
        }

        const x = Math.cos(theta) * rad;
        const z = Math.sin(theta) * rad;
        positions[pPtr] = x;
        positions[pPtr + 1] = y;
        positions[pPtr + 2] = z;

        uvs[uvPtr++] = x * 1.8 + 0.5;
        uvs[uvPtr++] = z * 1.8 + 0.5;

        if (r === 0) {
          tmp.copy(colVein);
        } else if (r <= 4) {
          tmp.copy(colCenter).lerp(colBlade, r / 4);
          tmp.lerp(colVein, veinWave * 0.55);
        } else {
          tmp.copy(colCrimsonLip).lerp(colBlade, r === 5 ? 0.25 : 0.0);
        }

        colors[pPtr] = tmp.r;
        colors[pPtr + 1] = tmp.g;
        colors[pPtr + 2] = tmp.b;
        pPtr += 3;
      }
    }

    for (let r = 0; r < ringSegs; r++) {
      for (let c = 0; c < radialSegs; c++) {
        const a = r * cols + c;
        const b = a + 1;
        const d = a + cols;
        const e = d + 1;
        // True counter-clockwise (+Y upward normal!) face winding: (b - a) is +Z, (d - a) is +X -> (+Z) x (+X) = +Y!
        indices.push(a, b, e, a, e, d);
      }
    }

    const deckGeo = new THREE.BufferGeometry();
    deckGeo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    deckGeo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
    deckGeo.setAttribute('uv', new THREE.BufferAttribute(uvs, 2));
    deckGeo.setIndex(indices);
    deckGeo.computeVertexNormals();

    // Underside Radial Structural Rib Girders + Central Umbilical Stalk
    const ribBoxes: THREE.BufferGeometry[] = [];
    const ribCount = 12;
    for (let i = 0; i < ribCount; i++) {
      const ang = (i / ribCount) * Math.PI * 2;
      const rib = new THREE.BoxGeometry(0.92, 0.065, 0.032);
      rib.translate(0.46, -0.058, 0);
      rib.rotateY(ang);
      ribBoxes.push(rib);
    }
    const stalk = new THREE.CylinderGeometry(0.08, 0.055, 0.85, 7);
    stalk.translate(0, -0.44, 0);
    ribBoxes.push(stalk);

    const ribsGeo = this.mergeSimpleGeometries(ribBoxes);
    return { deckGeo, ribsGeo };
  }

  /**
   * Sculpted irregular 3D Floating Sphagnum Peat-Moss Island (unit radius = 1.0)
   */
  private createFloatingPeatIslandGeo(seed: number): THREE.BufferGeometry {
    const radialSegs = 28;
    const rings = [
      { r: 0.0, y: 0.14, col: 0x3b5e28 },
      { r: 0.45, y: 0.11, col: 0x48692c },
      { r: 0.82, y: 0.05, col: 0x344d21 },
      { r: 1.0, y: -0.06, col: 0x2c2216 },
      { r: 0.92, y: -0.58, col: 0x1a130c },
    ];
    const cols = radialSegs + 1;
    const rows = rings.length;
    const positions = new Float32Array(cols * rows * 3);
    const colors = new Float32Array(cols * rows * 3);
    const uvs = new Float32Array(cols * rows * 2);
    const indices: number[] = [];
    const tmp = new THREE.Color();

    let pPtr = 0;
    let uvPtr = 0;
    for (let r = 0; r < rows; r++) {
      const spec = rings[r];
      tmp.setHex(spec.col);
      for (let c = 0; c < cols; c++) {
        const theta = (c / radialSegs) * Math.PI * 2;
        const nx = Math.cos(theta);
        const nz = Math.sin(theta);
        const organic =
          spec.r > 0.01
            ? 0.84 + 0.28 * (fbm2D(nx * 1.8 + seed, nz * 1.8 - seed, 2) * 0.5 + 0.5)
            : 1.0;
        const rad = spec.r * organic;
        const bump =
          r <= 2 && spec.r > 0 ? fbm2D(nx * 3.1 + seed, nz * 3.1, 2) * 0.045 : 0;

        const x = nx * rad;
        const z = nz * rad;
        positions[pPtr] = x;
        positions[pPtr + 1] = spec.y + bump;
        positions[pPtr + 2] = z;

        uvs[uvPtr++] = x * 1.4;
        uvs[uvPtr++] = z * 1.4;

        colors[pPtr] = tmp.r;
        colors[pPtr + 1] = tmp.g;
        colors[pPtr + 2] = tmp.b;
        pPtr += 3;
      }
    }

    for (let r = 0; r < rows - 1; r++) {
      for (let c = 0; c < radialSegs; c++) {
        const a = r * cols + c;
        const b = a + 1;
        const d = a + cols;
        const e = d + 1;
        // True counter-clockwise (+Y upward / outward normal!) face winding
        indices.push(a, b, e, a, e, d);
      }
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    geo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
    geo.setAttribute('uv', new THREE.BufferAttribute(uvs, 2));
    geo.setIndex(indices);
    geo.computeVertexNormals();
    return geo;
  }

  /**
   * Sculpted Botanical Dionaea Bivalve Snap-Trap Lobe!
   * Hinge midrib sits along local Z (-0.5 .. +0.5) at X = 0, Y = 0;
   * Curved kidney valve rises along Y (0 .. 1.0) and cups slightly inward!
   * Includes BOTH outer veined green-bronze cuticle AND inner blood-crimson digestive cavity + 3 trigger hairs!
   */
  private createBotanicalFlytrapLobeGeo(sideSign: -1 | 1): THREE.BufferGeometry {
    const uSegs = 14; // Along jaw length Z (-0.5 .. +0.5)
    const vSegs = 8; // From midrib hinge (v=0) up to toothed outer margin (v=1)
    const cols = uSegs + 1;
    const rows = vSegs + 1;
    const shellVerts = cols * rows;

    // Two-sided thick botanical valve: outer dorsal cuticle + inner concave crimson digestive wall + 3 trigger hairs!
    const hairVerts = 3 * 5;
    const totalVerts = shellVerts * 2 + hairVerts;
    const positions = new Float32Array(totalVerts * 3);
    const colors = new Float32Array(totalVerts * 3);
    const uvs = new Float32Array(totalVerts * 2);
    const indices: number[] = [];

    const colOuterBase = new THREE.Color(0x35591f);
    const colOuterVein = new THREE.Color(0x638529);
    const colRimBlush = new THREE.Color(0x7c2d24);
    const colCrimsonDeep = new THREE.Color(0x660818);
    const colCrimsonBright = new THREE.Color(0xa6172b);
    const tmp = new THREE.Color();

    for (let rv = 0; rv < rows; rv++) {
      const v = rv / vSegs; // 0 at midrib -> 1 at marginal spine rim
      for (let cu = 0; cu < cols; cu++) {
        const u = cu / uSegs; // 0 at rear base -> 1 at front tip
        const zNorm = (u - 0.5) * 2.0; // -1 .. +1

        // Authentic kidney-shaped Dionaea lobe silhouette!
        const archHeight = Math.sqrt(Math.max(0.02, 1.0 - zNorm * zNorm * 0.88));
        const y = v * archHeight * 0.92;
        const z = zNorm * 0.5 * (0.82 + 0.18 * Math.sin(v * Math.PI * 0.65));

        // Natural concave cupping so the two lobes form a sealed digestive chamber when shut!
        const cupBulge = Math.sin(v * Math.PI) * archHeight * 0.21;
        const veinRib = Math.pow(0.5 + 0.5 * Math.cos(u * Math.PI * 11.0), 2.5) * 0.014 * v;
        const thickness = lerp(0.055, 0.018, v);

        // 1. Outer Dorsal Cuticle Vertex
        const outIdx = rv * cols + cu;
        const o3 = outIdx * 3;
        positions[o3] = sideSign * (cupBulge + thickness + veinRib);
        positions[o3 + 1] = y;
        positions[o3 + 2] = z;
        uvs[outIdx * 2] = u;
        uvs[outIdx * 2 + 1] = v;

        tmp.copy(colOuterBase).lerp(colOuterVein, smoothstep(0.0, 0.012, veinRib) * 0.7);
        if (v > 0.75) tmp.lerp(colRimBlush, smoothstep(0.75, 1.0, v) * 0.65);
        colors[o3] = tmp.r;
        colors[o3 + 1] = tmp.g;
        colors[o3 + 2] = tmp.b;

        // 2. Inner Concave Blood-Crimson Digestive Cavity Vertex
        const inIdx = shellVerts + outIdx;
        const i3 = inIdx * 3;
        positions[i3] = sideSign * cupBulge;
        positions[i3 + 1] = y;
        positions[i3 + 2] = z;
        uvs[inIdx * 2] = u;
        uvs[inIdx * 2 + 1] = v;

        tmp.copy(colCrimsonDeep).lerp(colCrimsonBright, smoothstep(0.15, 0.82, v));
        if (v > 0.88) tmp.lerp(colOuterVein, smoothstep(0.88, 1.0, v) * 0.55);
        colors[i3] = tmp.r;
        colors[i3 + 1] = tmp.g;
        colors[i3 + 2] = tmp.b;
      }
    }

    // Stitch Outer & Inner surfaces with proper outward normals for both sides!
    for (let rv = 0; rv < vSegs; rv++) {
      for (let cu = 0; cu < uSegs; cu++) {
        const a = rv * cols + cu;
        const b = a + 1;
        const c = a + cols;
        const d = c + 1;

        if (sideSign > 0) {
          indices.push(a, c, b, b, c, d);
          indices.push(shellVerts + a, shellVerts + b, shellVerts + c);
          indices.push(shellVerts + b, shellVerts + d, shellVerts + c);
        } else {
          indices.push(a, b, c, b, d, c);
          indices.push(shellVerts + a, shellVerts + c, shellVerts + b);
          indices.push(shellVerts + b, shellVerts + c, shellVerts + d);
        }
      }
    }

    // Stitch outer margin lip (rv = vSegs) connecting outer & inner shells!
    const topRow = vSegs * cols;
    for (let cu = 0; cu < uSegs; cu++) {
      const o0 = topRow + cu;
      const o1 = o0 + 1;
      const i0 = shellVerts + o0;
      const i1 = shellVerts + o1;
      if (sideSign > 0) {
        indices.push(o0, i0, o1, o1, i0, i1);
      } else {
        indices.push(o0, o1, i0, o1, i1, i0);
      }
    }

    // Add 3 Sensory Trigger Hairs protruding inward from the crimson digestive face!
    const hairUVs = [
      { u: 0.34, v: 0.45 },
      { u: 0.5, v: 0.56 },
      { u: 0.66, v: 0.45 },
    ];
    let hBase = shellVerts * 2;
    for (const h of hairUVs) {
      const zNorm = (h.u - 0.5) * 2.0;
      const archH = Math.sqrt(Math.max(0.02, 1.0 - zNorm * zNorm * 0.88));
      const hy = h.v * archH * 0.92;
      const hz = zNorm * 0.5 * 0.92;
      const hx = sideSign * (Math.sin(h.v * Math.PI) * archH * 0.21);
      const rHair = 0.012;
      const tipX = hx - sideSign * 0.16;

      const pts = [
        [hx, hy - rHair, hz - rHair],
        [hx, hy - rHair, hz + rHair],
        [hx, hy + rHair, hz + rHair],
        [hx, hy + rHair, hz - rHair],
        [tipX, hy + 0.03, hz],
      ];
      for (let k = 0; k < 5; k++) {
        const v3 = (hBase + k) * 3;
        positions[v3] = pts[k][0];
        positions[v3 + 1] = pts[k][1];
        positions[v3 + 2] = pts[k][2];
        colors[v3] = 0.22;
        colors[v3 + 1] = 0.04;
        colors[v3 + 2] = 0.06;
      }
      indices.push(
        hBase + 0, hBase + 1, hBase + 4,
        hBase + 1, hBase + 2, hBase + 4,
        hBase + 2, hBase + 3, hBase + 4,
        hBase + 3, hBase + 0, hBase + 4
      );
      hBase += 5;
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    geo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
    geo.setAttribute('uv', new THREE.BufferAttribute(uvs, 2));
    geo.setIndex(indices);
    geo.computeVertexNormals();
    return geo;
  }

  /**
   * Long curved marginal cilia tooth (interlocks with opposite lobe's teeth on snap!)
   */
  private createCurvedMarginalSpineGeo(): THREE.BufferGeometry {
    const geo = new THREE.ConeGeometry(0.024, 0.34, 5, 5);
    geo.translate(0, 0.17, 0);
    const pos = geo.attributes.position as THREE.BufferAttribute;
    for (let i = 0; i < pos.count; i++) {
      const y = pos.getY(i);
      const u = clamp(y / 0.34, 0, 1);
      // Graceful predatory inward hook curve!
      pos.setX(i, pos.getX(i) + u * u * 0.075);
    }
    geo.computeVertexNormals();
    return geo;
  }

  /**
   * Arched winged basal phyllodia rosette leaf around the base of a Colossal Flytrap!
   */
  private createBasalPhyllodiaLeafGeo(): THREE.BufferGeometry {
    const geo = new THREE.PlaneGeometry(0.38, 1.0, 4, 8);
    geo.rotateX(-Math.PI * 0.5);
    geo.translate(0, 0, 0.5);
    const pos = geo.attributes.position as THREE.BufferAttribute;
    const colors = new Float32Array(pos.count * 3);
    for (let i = 0; i < pos.count; i++) {
      const x = pos.getX(i);
      const z = pos.getZ(i); // 0 .. 1
      const widthEnv = Math.sin(Math.pow(z, 0.7) * Math.PI) * 1.15 + 0.15;
      const archY = Math.sin(z * Math.PI * 0.85) * 0.22 - Math.abs(x) * 0.18;
      pos.setXYZ(i, x * widthEnv, archY, z);
      colors[i * 3] = lerp(0.16, 0.28, z);
      colors[i * 3 + 1] = lerp(0.28, 0.42, z);
      colors[i * 3 + 2] = 0.11;
    }
    geo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
    geo.computeVertexNormals();
    return geo;
  }

  /**
   * Fine crimson glandular filament + glistening mucilage droplet for Curling Sundews!
   */
  private createSundewGlandDropletGeo(): THREE.BufferGeometry {
    const hair = new THREE.CylinderGeometry(0.012, 0.022, 0.52, 5);
    hair.rotateZ(-Math.PI * 0.5);
    hair.translate(0.26, 0, 0);
    const drop = new THREE.SphereGeometry(0.072, 7, 6);
    drop.translate(0.54, 0, 0);
    return this.mergeSimpleGeometries([hair, drop]);
  }

  /**
   * Sculpted 3D Nepenthes Rajah Giant Hanging Pitcher Plant (Ascidium Urn + Ribbed Peristome Collar + Spotted Hood Lid)!
   */
  private createNepenthesPitcherGeometries(): {
    bodyGeo: THREE.BufferGeometry;
    collarGeo: THREE.BufferGeometry;
    lidGeo: THREE.BufferGeometry;
  } {
    // Lathe profile from bottom tendril base (-1.0) -> bloated lower digestive belly (-0.55) -> waist (-0.1) -> mouth (0.45)
    const lathePts = [
      new THREE.Vector2(0.03, -1.0),
      new THREE.Vector2(0.26, -0.86),
      new THREE.Vector2(0.46, -0.62),
      new THREE.Vector2(0.42, -0.34),
      new THREE.Vector2(0.31, -0.05),
      new THREE.Vector2(0.35, 0.28),
      new THREE.Vector2(0.38, 0.44),
    ];
    const bodyGeo = new THREE.LatheGeometry(lathePts, 18);
    const bPos = bodyGeo.attributes.position as THREE.BufferAttribute;
    const bCol = new Float32Array(bPos.count * 3);
    const cBottom = new THREE.Color(0x5c141d);
    const cMid = new THREE.Color(0x6b7d24);
    const cSpot = new THREE.Color(0x4a1119);
    const tmp = new THREE.Color();

    for (let i = 0; i < bPos.count; i++) {
      const x = bPos.getX(i);
      const y = bPos.getY(i);
      const z = bPos.getZ(i);
      const u = clamp((y + 1.0) / 1.44, 0, 1);
      const mottle = smoothstep(0.45, 0.78, fbm2D(x * 4.5 + y * 2.2, z * 4.5 - y * 1.7, 2) * 0.5 + 0.5);
      tmp.copy(cBottom).lerp(cMid, smoothstep(0.15, 0.78, u));
      tmp.lerp(cSpot, mottle * 0.68);
      bCol[i * 3] = tmp.r;
      bCol[i * 3 + 1] = tmp.g;
      bCol[i * 3 + 2] = tmp.b;
    }
    bodyGeo.setAttribute('color', new THREE.BufferAttribute(bCol, 3));
    bodyGeo.computeVertexNormals();

    // Corrugated crimson peristome collar ring at y = +0.44
    const collarGeo = new THREE.TorusGeometry(0.38, 0.068, 8, 20);
    collarGeo.rotateX(Math.PI * 0.5);
    collarGeo.scale(1.0, 0.85, 1.12);
    collarGeo.translate(0, 0.44, 0);

    // Overarching operculum hood lid above the mouth (keeps rain out in real Nepenthes!)
    const lidGeo = new THREE.SphereGeometry(0.36, 12, 8, 0, Math.PI * 2, 0, Math.PI * 0.42);
    lidGeo.scale(0.95, 0.45, 1.18);
    lidGeo.rotateX(0.38);
    lidGeo.translate(0, 0.68, -0.14);
    const lPos = lidGeo.attributes.position as THREE.BufferAttribute;
    const lCol = new Float32Array(lPos.count * 3);
    for (let i = 0; i < lPos.count; i++) {
      lCol[i * 3] = 0.48;
      lCol[i * 3 + 1] = 0.62;
      lCol[i * 3 + 2] = 0.18;
    }
    lidGeo.setAttribute('color', new THREE.BufferAttribute(lCol, 3));

    return { bodyGeo, collarGeo, lidGeo };
  }

  private mergeSimpleGeometries(geos: THREE.BufferGeometry[]): THREE.BufferGeometry {
    let totalVerts = 0;
    let totalIdx = 0;
    for (const g of geos) {
      const nonIdx = g.index ? g.toNonIndexed() : g;
      totalVerts += nonIdx.attributes.position.count;
      totalIdx += nonIdx.attributes.position.count;
    }
    const pos = new Float32Array(totalVerts * 3);
    const norm = new Float32Array(totalVerts * 3);
    let offset = 0;
    for (const raw of geos) {
      const g = raw.index ? raw.toNonIndexed() : raw;
      g.computeVertexNormals();
      const p = g.attributes.position.array as Float32Array;
      const n = g.attributes.normal.array as Float32Array;
      pos.set(p, offset);
      norm.set(n, offset);
      offset += p.length;
    }
    const merged = new THREE.BufferGeometry();
    merged.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    merged.setAttribute('normal', new THREE.BufferAttribute(norm, 3));
    void totalIdx;
    return merged;
  }

  private initAtmosphereAndGrappleRope(): void {
    // 1. Drifting Bioluminescent Acid-Chartreuse Bog Spores
    this.sporePositions = new Float32Array(this.sporeCount * 3);
    this.sporeVelocities = new Float32Array(this.sporeCount * 3);
    const rng = mulberry32(991823);

    for (let i = 0; i < this.sporeCount; i++) {
      const ix = i * 3;
      this.sporePositions[ix] = (rng() - 0.5) * 140;
      this.sporePositions[ix + 1] = rng() * 24;
      this.sporePositions[ix + 2] = (rng() - 0.5) * 160;

      this.sporeVelocities[ix] = (rng() - 0.5) * 1.8;
      this.sporeVelocities[ix + 1] = lerp(0.35, 1.35, rng());
      this.sporeVelocities[ix + 2] = (rng() - 0.5) * 1.8;
    }

    const sporeGeo = new THREE.BufferGeometry();
    sporeGeo.setAttribute('position', new THREE.BufferAttribute(this.sporePositions, 3));
    const sporeMat = new THREE.PointsMaterial({
      color: 0xb8ff4d,
      size: 0.34,
      transparent: true,
      opacity: 0.68,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });
    this.sporePoints = new THREE.Points(sporeGeo, sporeMat);
    this.sporePoints.frustumCulled = false;
    this.atmosphereFxGroup.add(this.sporePoints);

    // 2. Live 3D Braided Pitcher-Tendril Grapple Line (12 rings x 6 sides)
    const ropeRings = 12;
    const ropeSides = 6;
    const rCols = ropeSides + 1;
    const rPos = new Float32Array(ropeRings * rCols * 3);
    const rNorm = new Float32Array(ropeRings * rCols * 3);
    const rIdx: number[] = [];
    for (let r = 0; r < ropeRings - 1; r++) {
      for (let c = 0; c < ropeSides; c++) {
        const a = r * rCols + c;
        const b = a + 1;
        const d = a + rCols;
        const e = d + 1;
        rIdx.push(a, d, b, b, d, e);
      }
    }
    this.grappleRopeGeo = new THREE.BufferGeometry();
    const posAttr = new THREE.BufferAttribute(rPos, 3);
    posAttr.setUsage(THREE.DynamicDrawUsage);
    const normAttr = new THREE.BufferAttribute(rNorm, 3);
    normAttr.setUsage(THREE.DynamicDrawUsage);
    this.grappleRopeGeo.setAttribute('position', posAttr);
    this.grappleRopeGeo.setAttribute('normal', normAttr);
    this.grappleRopeGeo.setIndex(rIdx);

    this.grappleRopeMesh = new THREE.Mesh(this.grappleRopeGeo, this.grappleTendrilMat);
    this.grappleRopeMesh.visible = false;
    this.grappleRopeMesh.frustumCulled = false;
    this.atmosphereFxGroup.add(this.grappleRopeMesh);
  }

  /**
   * Builds a Colossal Dionaea Snap-Trap with basal rosette, live articulated cubic-Bezier S-stalk,
   * and hinged bivalve crimson jaws with 34 interlocking curved marginal spine teeth!
   */
  private createColossalFlytrap(
    chunkGroup: THREE.Group,
    basePos: THREE.Vector3,
    aimYaw: number,
    scale: number,
    rng: () => number
  ): ColossalFlytrap {
    // 1. Basal Phyllodia Rosette hugging the peat island / bank
    const rosette = new THREE.Group();
    rosette.position.copy(basePos);
    const leafCount = 6;
    for (let i = 0; i < leafCount; i++) {
      const leaf = new THREE.Mesh(this.basalLeafGeo, this.flytrapOuterMat);
      leaf.rotation.y = (i / leafCount) * Math.PI * 2 + (rng() - 0.5) * 0.35;
      leaf.scale.set(scale * 1.15, scale * 0.95, scale * lerp(1.0, 1.35, rng()));
      rosette.add(leaf);
    }
    chunkGroup.add(rosette);

    // 2. Dynamic Articulated S-Curve Muscular Petiole Stalk
    const cols = STALK_SIDES + 1;
    const sPos = new Float32Array(STALK_RINGS * cols * 3);
    const sNorm = new Float32Array(STALK_RINGS * cols * 3);
    const sUv = new Float32Array(STALK_RINGS * cols * 2);
    const sIdx: number[] = [];
    for (let r = 0; r < STALK_RINGS; r++) {
      for (let c = 0; c < cols; c++) {
        const k = (r * cols + c) * 2;
        sUv[k] = c / STALK_SIDES;
        sUv[k + 1] = (r / (STALK_RINGS - 1)) * 3.0;
      }
    }
    for (let r = 0; r < STALK_RINGS - 1; r++) {
      for (let c = 0; c < STALK_SIDES; c++) {
        const a = r * cols + c;
        const b = a + 1;
        const d = a + cols;
        const e = d + 1;
        sIdx.push(a, d, b, b, d, e);
      }
    }
    const stalkGeo = new THREE.BufferGeometry();
    const spAttr = new THREE.BufferAttribute(sPos, 3);
    spAttr.setUsage(THREE.DynamicDrawUsage);
    const snAttr = new THREE.BufferAttribute(sNorm, 3);
    snAttr.setUsage(THREE.DynamicDrawUsage);
    stalkGeo.setAttribute('position', spAttr);
    stalkGeo.setAttribute('normal', snAttr);
    stalkGeo.setAttribute('uv', new THREE.BufferAttribute(sUv, 2));
    stalkGeo.setIndex(sIdx);

    const stalkMesh = new THREE.Mesh(stalkGeo, this.petioleStalkMat);
    stalkMesh.castShadow = true;
    stalkMesh.frustumCulled = false;
    stalkMesh.userData.uniqueGeo = true;
    chunkGroup.add(stalkMesh);

    // 3. Hinged Bivalve Jaw Head + Interlocking Marginal Cilia Spines!
    const jawHeadGroup = new THREE.Group();
    const jawLength = 4.6 * scale;
    const jawHeight = 2.55 * scale;

    const buildHalfJaw = (sideSign: -1 | 1): THREE.Group => {
      const halfGroup = new THREE.Group();
      const lobeGeo = sideSign < 0 ? this.leftFlytrapLobeGeo : this.rightFlytrapLobeGeo;
      const lobeMesh = new THREE.Mesh(lobeGeo, this.flytrapOuterMat);
      lobeMesh.scale.set(jawHeight, jawHeight, jawLength);
      lobeMesh.castShadow = true;
      lobeMesh.receiveShadow = true;
      halfGroup.add(lobeMesh);

      // Glistening inner digestive nectar lining patch
      const spineCount = 16;
      const spineIm = new THREE.InstancedMesh(
        this.marginalSpineGeo,
        this.flytrapTeethMat,
        spineCount
      );
      // Stagger left vs right teeth by 0.5 spacing so they interlace cleanly like prison bars on snap!
      const stagger = sideSign < 0 ? 0.0 : 0.5;
      for (let s = 0; s < spineCount; s++) {
        const u = clamp((s + 0.25 + stagger) / (spineCount + 0.5), 0.05, 0.95);
        const zNorm = (u - 0.5) * 2.0;
        const archH = Math.sqrt(Math.max(0.02, 1.0 - zNorm * zNorm * 0.88));
        const rimY = archH * 0.92 * jawHeight;
        const rimZ = zNorm * 0.5 * (0.82 + 0.18 * Math.sin(Math.PI * 0.65)) * jawLength;
        const rimX = sideSign * 0.02 * jawHeight;

        this._dummy.position.set(rimX, rimY * 0.97, rimZ);
        // Angle teeth slightly inward toward the opposite lobe so they cross & interlock on closure!
        const fanPitch = zNorm * 0.42;
        this._dummy.rotation.set(fanPitch, sideSign < 0 ? 0 : Math.PI, -sideSign * 0.28);
        const toothScale = scale * (0.75 + 0.45 * archH);
        this._dummy.scale.set(toothScale, toothScale * 1.15, toothScale);
        this._dummy.updateMatrix();
        spineIm.setMatrixAt(s, this._dummy.matrix);
      }
      spineIm.instanceMatrix.needsUpdate = true;
      spineIm.castShadow = true;
      halfGroup.add(spineIm);

      return halfGroup;
    };

    const leftLobeGroup = buildHalfJaw(-1);
    const rightLobeGroup = buildHalfJaw(1);
    jawHeadGroup.add(leftLobeGroup, rightLobeGroup);
    chunkGroup.add(jawHeadGroup);

    const ft: ColossalFlytrap = {
      id: this.nextFlytrapId++,
      basePos: basePos.clone(),
      restAimYaw: aimYaw,
      curAimYaw: aimYaw,
      scale,
      stalkLength: lerp(5.4, 7.4, rng()) * scale,
      state: 'POISED_AMBUSH',
      stateTimer: 0,
      ambientCycleTimer: lerp(1.5, 7.5, rng()),
      isAmbientSnap: false,
      lobeOpenAngle: 0.96,
      lungeForward: 0,
      headDropY: 0,
      headPosWorld: new THREE.Vector3(),
      stalkGeo,
      stalkMesh,
      jawHeadGroup,
      leftLobeGroup,
      rightLobeGroup,
      clampedVehicleTimer: 0,
    };

    this.updateFlytrapKinematics(ft, 0);
    return ft;
  }

  /**
   * Rebuilds the live 3D cubic-Bezier S-curve muscular petiole stalk and orients the bivalve jaws!
   */
  private updateFlytrapKinematics(ft: ColossalFlytrap, elapsedTime: number): void {
    const dirX = Math.sin(ft.curAimYaw);
    const dirZ = Math.cos(ft.curAimYaw);

    const breathe = Math.sin(elapsedTime * 1.6 + ft.id * 1.9) * 0.14;
    const reachHoriz = (1.65 + ft.lungeForward) * ft.scale;
    const headY =
      ft.basePos.y +
      ft.stalkLength * 0.72 +
      ft.headDropY +
      breathe -
      Math.max(0, ft.lungeForward) * 0.36;

    // 4-point Cubic Bezier muscular S-stem: rises from root bulb, arches back, then sweeps forward into the trap neck!
    const p0 = this._vA.copy(ft.basePos);
    const p1 = this._vB.set(
      ft.basePos.x - dirX * (1.25 * ft.scale - ft.lungeForward * 0.15),
      ft.basePos.y + ft.stalkLength * 0.42,
      ft.basePos.z - dirZ * (1.25 * ft.scale - ft.lungeForward * 0.15)
    );
    const p2 = this._vC.set(
      ft.basePos.x + dirX * (reachHoriz * 0.45),
      headY + 1.35 * ft.scale,
      ft.basePos.z + dirZ * (reachHoriz * 0.45)
    );
    const p3 = this._vD.set(
      ft.basePos.x + dirX * reachHoriz,
      headY,
      ft.basePos.z + dirZ * reachHoriz
    );

    ft.headPosWorld.copy(p3);

    const posAttr = ft.stalkGeo.attributes.position as THREE.BufferAttribute;
    const normAttr = ft.stalkGeo.attributes.normal as THREE.BufferAttribute;
    const posArr = posAttr.array as Float32Array;
    const normArr = normAttr.array as Float32Array;
    const cols = STALK_SIDES + 1;

    for (let r = 0; r < STALK_RINGS; r++) {
      const t = r / (STALK_RINGS - 1);
      const it = 1 - t;
      const b0 = it * it * it;
      const b1 = 3 * it * it * t;
      const b2 = 3 * it * t * t;
      const b3 = t * t * t;

      const cx = b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x;
      const cy = b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y;
      const cz = b0 * p0.z + b1 * p1.z + b2 * p2.z + b3 * p3.z;

      // Analytic Bezier tangent
      const d0 = 3 * it * it;
      const d1 = 6 * it * t;
      const d2 = 3 * t * t;
      let tx = d0 * (p1.x - p0.x) + d1 * (p2.x - p1.x) + d2 * (p3.x - p2.x);
      let ty = d0 * (p1.y - p0.y) + d1 * (p2.y - p1.y) + d2 * (p3.y - p2.y);
      let tz = d0 * (p1.z - p0.z) + d1 * (p2.z - p1.z) + d2 * (p3.z - p2.z);
      const tLen = Math.hypot(tx, ty, tz) || 1;
      tx /= tLen;
      ty /= tLen;
      tz /= tLen;

      // Orthonormal ring frame
      let rx = dirZ;
      let ry = 0;
      let rz = -dirX;
      // Cross tangent x right -> binormal
      let bx = ty * rz - tz * ry;
      let by = tz * rx - tx * rz;
      let bz = tx * ry - ty * rx;
      const bLen = Math.hypot(bx, by, bz) || 1;
      bx /= bLen;
      by /= bLen;
      bz /= bLen;
      rx = by * tz - bz * ty;
      ry = bz * tx - bx * tz;
      rz = bx * ty - by * tx;

      const rad = lerp(0.46, 0.21, t) * ft.scale;
      for (let c = 0; c < cols; c++) {
        const ang = (c / STALK_SIDES) * Math.PI * 2;
        const flute = 1.0 + 0.12 * Math.cos(ang * 5.0);
        const ca = Math.cos(ang);
        const sa = Math.sin(ang);
        const nx = rx * ca + bx * sa;
        const ny = ry * ca + by * sa;
        const nz = rz * ca + bz * sa;

        const idx = (r * cols + c) * 3;
        posArr[idx] = cx + nx * rad * flute;
        posArr[idx + 1] = cy + ny * rad * flute;
        posArr[idx + 2] = cz + nz * rad * flute;
        normArr[idx] = nx;
        normArr[idx + 1] = ny;
        normArr[idx + 2] = nz;
      }
    }
    posAttr.needsUpdate = true;
    normAttr.needsUpdate = true;

    // Position & orient jaw head at neck apex, pitched downward toward prey!
    const strikePitch = 0.42 + clamp(ft.lungeForward * 0.12, -0.22, 0.45);
    const shudder =
      ft.state === 'TRIGGER_COIL'
        ? Math.sin(elapsedTime * 54.0 + ft.id) * 0.045
        : ft.state === 'DIGESTIVE_WRITHE'
        ? Math.sin(elapsedTime * 18.0 + ft.id) * 0.03
        : 0;

    ft.jawHeadGroup.position.copy(p3);
    ft.jawHeadGroup.rotation.set(strikePitch, ft.curAimYaw + shudder, 0, 'YXZ');

    // Rotate bivalve lobes around midrib Z: positive = open wide, near-zero = snapped shut with interlocking spines!
    ft.leftLobeGroup.rotation.z = ft.lobeOpenAngle;
    ft.rightLobeGroup.rotation.z = -ft.lobeOpenAngle;
  }

  /**
   * Creates a Curling Crimson Drosera Sundew Tentacle with 26 glistening ruby mucilage dewdrops!
   */
  private createCurlingSundewStalk(
    chunkGroup: THREE.Group,
    basePos: THREE.Vector3,
    aimYaw: number,
    height: number,
    rng: () => number
  ): CurlingSundewStalk {
    const cols = SUNDEW_SIDES + 1;
    const vertCount = SUNDEW_SEGS * cols;
    const sPos = new Float32Array(vertCount * 3);
    const sNorm = new Float32Array(vertCount * 3);
    const sCol = new Float32Array(vertCount * 3);
    const sIdx: number[] = [];

    const cBase = new THREE.Color(0x36611e);
    const cTip = new THREE.Color(0xa81922);
    const tmp = new THREE.Color();

    for (let r = 0; r < SUNDEW_SEGS; r++) {
      const u = r / (SUNDEW_SEGS - 1);
      tmp.copy(cBase).lerp(cTip, smoothstep(0.2, 0.88, u));
      for (let c = 0; c < cols; c++) {
        const k = (r * cols + c) * 3;
        sCol[k] = tmp.r;
        sCol[k + 1] = tmp.g;
        sCol[k + 2] = tmp.b;
      }
    }
    for (let r = 0; r < SUNDEW_SEGS - 1; r++) {
      for (let c = 0; c < SUNDEW_SIDES; c++) {
        const a = r * cols + c;
        const b = a + 1;
        const d = a + cols;
        const e = d + 1;
        sIdx.push(a, d, b, b, d, e);
      }
    }

    const stemGeo = new THREE.BufferGeometry();
    const posAttr = new THREE.BufferAttribute(sPos, 3);
    posAttr.setUsage(THREE.DynamicDrawUsage);
    const normAttr = new THREE.BufferAttribute(sNorm, 3);
    normAttr.setUsage(THREE.DynamicDrawUsage);
    stemGeo.setAttribute('position', posAttr);
    stemGeo.setAttribute('normal', normAttr);
    stemGeo.setAttribute('color', new THREE.BufferAttribute(sCol, 3));
    stemGeo.setIndex(sIdx);

    const stemMesh = new THREE.Mesh(stemGeo, this.sundewStemMat);
    stemMesh.castShadow = true;
    stemMesh.frustumCulled = false;
    stemMesh.userData.uniqueGeo = true;
    chunkGroup.add(stemMesh);

    const glandInstancedMesh = new THREE.InstancedMesh(
      this.sundewGlandGeo,
      this.sundewDewMat,
      SUNDEW_GLANDS
    );
    glandInstancedMesh.frustumCulled = false;
    chunkGroup.add(glandInstancedMesh);

    const sundew: CurlingSundewStalk = {
      id: this.nextSundewId++,
      basePos: basePos.clone(),
      aimYaw,
      height,
      curlProgress: 0.12,
      targetCurl: 0.12,
      holdTimer: 0,
      ambientTimer: lerp(2.0, 9.0, rng()),
      swayPhase: rng() * Math.PI * 2,
      stemGeo,
      stemMesh,
      glandInstancedMesh,
      tipWorldPos: new THREE.Vector3(),
    };

    this.updateSundewLogSpiralGeometry(sundew, 0);
    return sundew;
  }

  /**
   * Updates the tip-first logarithmic spiral tentacle geometry & positions all 26 glistening mucilage droplets!
   */
  private updateSundewLogSpiralGeometry(sd: CurlingSundewStalk, elapsedTime: number): void {
    const yaw = sd.aimYaw + Math.sin(elapsedTime * 1.1 + sd.swayPhase) * 0.08;
    const dirX = Math.sin(yaw);
    const dirZ = Math.cos(yaw);
    const rightX = dirZ;
    const rightZ = -dirX;

    const posAttr = sd.stemGeo.attributes.position as THREE.BufferAttribute;
    const normAttr = sd.stemGeo.attributes.normal as THREE.BufferAttribute;
    const posArr = posAttr.array as Float32Array;
    const normArr = normAttr.array as Float32Array;
    const cols = SUNDEW_SIDES + 1;

    const segLen = sd.height / (SUNDEW_SEGS - 1);
    let curX = sd.basePos.x;
    let curY = sd.basePos.y;
    let curZ = sd.basePos.z;
    let cumPitch = 0.06; // Slight natural lean at base

    const spineX = new Float32Array(SUNDEW_SEGS);
    const spineY = new Float32Array(SUNDEW_SEGS);
    const spineZ = new Float32Array(SUNDEW_SEGS);
    const spinePitch = new Float32Array(SUNDEW_SEGS);

    for (let r = 0; r < SUNDEW_SEGS; r++) {
      const u = r / (SUNDEW_SEGS - 1);
      spineX[r] = curX;
      spineY[r] = curY;
      spineZ[r] = curZ;
      spinePitch[r] = cumPitch;

      // Radial normal vectors around current pitch in the (dir, +Y) plane
      const fHoriz = Math.cos(cumPitch);
      const tx = dirX * Math.sin(cumPitch);
      const ty = Math.cos(cumPitch);
      const tz = dirZ * Math.sin(cumPitch);
      void tx;
      void ty;
      void tz;

      // In-plane normal perpendicular to tangent: (-cosPitch * dir, sinPitch)
      const inPlaneX = dirX * Math.cos(cumPitch);
      const inPlaneY = -Math.sin(cumPitch);
      const inPlaneZ = dirZ * Math.cos(cumPitch);

      const rad = lerp(0.28, 0.075, u);
      for (let c = 0; c < cols; c++) {
        const ang = (c / SUNDEW_SIDES) * Math.PI * 2;
        const ca = Math.cos(ang);
        const sa = Math.sin(ang);
        const nx = rightX * ca + inPlaneX * sa;
        const ny = inPlaneY * sa;
        const nz = rightZ * ca + inPlaneZ * sa;

        const idx = (r * cols + c) * 3;
        posArr[idx] = curX + nx * rad;
        posArr[idx + 1] = curY + ny * rad;
        posArr[idx + 2] = curZ + nz * rad;
        normArr[idx] = nx;
        normArr[idx + 1] = ny;
        normArr[idx + 2] = nz;
      }

      // Tip-first logarithmic spiral bending!
      const tipWeight = Math.pow(u, 1.65);
      const archBase = 0.085 * u;
      const spiralBend = sd.curlProgress * tipWeight * 0.44;
      cumPitch += archBase + spiralBend;

      curX += dirX * Math.sin(cumPitch) * segLen;
      curY += fHoriz * segLen;
      curZ += dirZ * Math.sin(cumPitch) * segLen;
    }

    sd.tipWorldPos.set(
      spineX[SUNDEW_SEGS - 1],
      spineY[SUNDEW_SEGS - 1],
      spineZ[SUNDEW_SEGS - 1]
    );
    posAttr.needsUpdate = true;
    normAttr.needsUpdate = true;

    // Distribute 26 glistening ruby glandular hairs along the upper 68% of the curling tentacle!
    for (let g = 0; g < SUNDEW_GLANDS; g++) {
      const uGland = lerp(0.32, 0.98, g / (SUNDEW_GLANDS - 1));
      const fIdx = uGland * (SUNDEW_SEGS - 1);
      const i0 = Math.min(SUNDEW_SEGS - 2, Math.floor(fIdx));
      const frac = fIdx - i0;
      const gx = lerp(spineX[i0], spineX[i0 + 1], frac);
      const gy = lerp(spineY[i0], spineY[i0 + 1], frac);
      const gz = lerp(spineZ[i0], spineZ[i0 + 1], frac);
      const gPitch = lerp(spinePitch[i0], spinePitch[i0 + 1], frac);

      const radialAzimuth = yaw + g * 2.39996; // Golden-angle botanical phyllotaxis!
      this._dummy.position.set(gx, gy, gz);
      this._dummy.rotation.set(0, radialAzimuth, -gPitch * 0.55 + 0.25, 'YXZ');
      const gScale = lerp(1.35, 0.85, uGland);
      this._dummy.scale.setScalar(gScale);
      this._dummy.updateMatrix();
      sd.glandInstancedMesh.setMatrixAt(g, this._dummy.matrix);
    }
    sd.glandInstancedMesh.instanceMatrix.needsUpdate = true;
  }

  /**
   * Builds an overarching bog-bough spanning a wide lagoon water gap, draped with
   * sculpted 3D Nepenthes Rajah Hanging Pitcher Plants & a [LMB] Grapple Anchor!
   */
  private buildOverhangingPitcherGrappleSpan(
    chunkGroup: THREE.Group,
    pitcherAnchors: PitcherGrappleAnchor[],
    chunkIndex: number,
    zGap: number,
    specSampler: (z: number) => { centerX: number; halfWidth: number; baseY: number },
    rng: () => number
  ): void {
    const sp = specSampler(zGap);
    const spanHalf = 42.0;
    const crownY = 17.5 + rng() * 2.5;

    // Gnarled overarching bog-cypress bough spanning the lagoon
    const curve = new THREE.CatmullRomCurve3([
      new THREE.Vector3(sp.centerX - spanHalf, -1.5, zGap + 4.0),
      new THREE.Vector3(sp.centerX - spanHalf * 0.45, crownY * 0.88, zGap + 1.5),
      new THREE.Vector3(sp.centerX, crownY, zGap),
      new THREE.Vector3(sp.centerX + spanHalf * 0.45, crownY * 0.88, zGap - 1.5),
      new THREE.Vector3(sp.centerX + spanHalf, -1.5, zGap - 4.0),
    ]);
    const boughGeo = new THREE.TubeGeometry(curve, 18, 1.05, 7, false);
    const boughMesh = new THREE.Mesh(boughGeo, this.bogBoughMat);
    boughMesh.castShadow = true;
    boughMesh.userData.uniqueGeo = true;
    chunkGroup.add(boughMesh);

    // 3 Hanging Giant Nepenthes Rajah Pitcher Plants along the bough (center = primary [LMB] Grapple Target!)
    const uSlots = [0.34, 0.5, 0.66];
    for (let s = 0; s < uSlots.length; s++) {
      const boughPt = curve.getPoint(uSlots[s]);
      const tendrilDrop = s === 1 ? 3.8 : lerp(3.2, 5.0, rng());
      const pitcherScale = s === 1 ? 2.65 : lerp(1.85, 2.35, rng());

      // Upper attachment tendril from bough down to pitcher
      const tendrilGeo = new THREE.CylinderGeometry(0.08, 0.11, tendrilDrop, 6);
      tendrilGeo.translate(0, -tendrilDrop * 0.5, 0);
      const tendrilMesh = new THREE.Mesh(tendrilGeo, this.petioleStalkMat);
      tendrilMesh.position.copy(boughPt);
      tendrilMesh.userData.uniqueGeo = true;
      chunkGroup.add(tendrilMesh);

      const pitcherGroup = new THREE.Group();
      const restPos = new THREE.Vector3(boughPt.x, boughPt.y - tendrilDrop, boughPt.z);
      pitcherGroup.position.copy(restPos);

      const body = new THREE.Mesh(this.nepenthesPitcherGeo, this.pitcherBodyMat);
      body.scale.setScalar(pitcherScale);
      body.castShadow = true;
      const collar = new THREE.Mesh(this.nepenthesCollarGeo, this.pitcherCollarMat);
      collar.scale.setScalar(pitcherScale);
      const lid = new THREE.Mesh(this.nepenthesLidGeo, this.pitcherBodyMat);
      lid.scale.setScalar(pitcherScale);
      pitcherGroup.add(body, collar, lid);

      // Bioluminescent target ring on grappleable pitchers
      const haloMesh = new THREE.Mesh(this.haloRingGeo, this.grappleHaloMat);
      haloMesh.position.set(0, -pitcherScale * 0.45, 0);
      haloMesh.visible = false;
      pitcherGroup.add(haloMesh);

      chunkGroup.add(pitcherGroup);

      pitcherAnchors.push({
        id: this.nextPitcherId++,
        chunkIndex,
        restPos,
        worldPos: restPos.clone(),
        dipY: 0,
        dipVelY: 0,
        swayPhase: rng() * Math.PI * 2,
        pitcherGroup,
        haloMesh,
      });
    }
  }

  /**
   * Populates a 112m Carnivorous Bog chunk:
   * - Acid-green digestive lagoon + chain of buoyant 3D Victoria lily-pad rafts & floating peat islands
   * - Wide water gaps spanned by overarching Nepenthes Pitcher-Plant [LMB] grapple vines
   * - Colossal 5-stage snapping Dionaea flytraps & logarithmic-coiling Drosera sundews!
   */
  public populateCarnivorousBogChunk(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    chunkLength: number,
    rng: () => number,
    specSampler: (z: number) => { centerX: number; halfWidth: number; baseY: number },
    obstacles: WorldObstacle[],
    allocObstacleId: () => number
  ): CarnivorousBogChunkData {
    const platforms: BogFloatingPlatform[] = [];
    const flytraps: ColossalFlytrap[] = [];
    const sundews: CurlingSundewStalk[] = [];
    const pitcherAnchors: PitcherGrappleAnchor[] = [];
    void obstacles;
    void allocObstacleId;

    // 1. Acid-Green Bioluminescent Lagoon Water Sheet at y = -0.38m
    const lagoonGeo = new THREE.PlaneGeometry(118.0, chunkLength + 0.8, 1, 1);
    lagoonGeo.rotateX(-Math.PI * 0.5);
    const lagoonMesh = new THREE.Mesh(lagoonGeo, this.acidLagoonWaterMat);
    lagoonMesh.position.set(
      specSampler(zStart - chunkLength * 0.5).centerX,
      LAGOON_WATER_Y,
      zStart - chunkLength * 0.5
    );
    lagoonMesh.userData.uniqueGeo = true;
    chunkGroup.add(lagoonMesh);

    // 2. Spawn 4 Golden-Chartreuse Swamp Sunbeams piercing the bog mist
    const beamGeo = new THREE.CylinderGeometry(1.8, 6.2, 26.0, 10, 1, true);
    for (let b = 0; b < 4; b++) {
      const bz = zStart - ((b + 0.22) / 4) * chunkLength;
      const sp = specSampler(bz);
      const beam = new THREE.Mesh(beamGeo, this.swampSunbeamMat);
      beam.position.set(sp.centerX + (rng() - 0.5) * 32.0, 11.5, bz);
      beam.rotation.z = -0.22;
      chunkGroup.add(beam);
    }

    // 3. Build Buoyant 3D Victoria Lily-Pad Rafts & Floating Peat Islands!
    // Deliberate wide acid-lagoon grapple gaps around u = 0.46 and u = 0.88 (with side lily-pad bypass stepping stones!)
    const spawnPlatform = (
      kind: 'VICTORIA_LILYPAD' | 'PEAT_ISLAND',
      x: number,
      z: number,
      radius: number
    ): BogFloatingPlatform => {
      const group = new THREE.Group();
      const baseY = kind === 'PEAT_ISLAND' ? 0.08 : 0.04;
      const yaw = rng() * Math.PI * 2;
      group.position.set(x, baseY, z);
      group.rotation.y = yaw;

      if (kind === 'VICTORIA_LILYPAD') {
        const deck = new THREE.Mesh(this.lilyPadDeckGeo, this.lilyPadTopMat);
        deck.scale.set(radius, radius * 0.85, radius);
        deck.receiveShadow = true;
        deck.castShadow = true;
        const ribs = new THREE.Mesh(this.lilyPadRibsGeo, this.lilyPadUndersideMat);
        ribs.scale.set(radius, radius * 0.85, radius);
        group.add(deck, ribs);
      } else {
        const pGeo = this.peatIslandGeos[Math.floor(rng() * this.peatIslandGeos.length)];
        const island = new THREE.Mesh(pGeo, this.peatIslandMat);
        island.scale.set(radius, lerp(1.2, 1.55, rng()), radius);
        island.receiveShadow = true;
        island.castShadow = true;
        group.add(island);
      }
      chunkGroup.add(group);

      const rippleRing = new THREE.Mesh(this.rippleGeo, this.rippleRingMat);
      rippleRing.position.set(x, LAGOON_WATER_Y + 0.03, z);
      rippleRing.scale.setScalar(radius * 1.04);
      rippleRing.visible = false;
      chunkGroup.add(rippleRing);

      const plat: BogFloatingPlatform = {
        id: this.nextPlatformId++,
        kind,
        x,
        z,
        radius,
        baseY,
        curY: baseY,
        velY: 0,
        pitchX: 0,
        velPitchX: 0,
        rollZ: 0,
        velRollZ: 0,
        dwellTime: 0,
        submerged: false,
        bobPhase: rng() * Math.PI * 2,
        yaw,
        mesh: group,
        rippleRing,
        rippleScale: 1.0,
        rippleOpacity: 0,
      };
      platforms.push(plat);
      return plat;
    };

    if (chunkIndex <= 0) {
      // Ensure Chunk 0 / -1 has a broad Floating Peat Landing right under spawn (-2m .. -22m) so showcase car sits level!
      if (chunkIndex === 0) {
        const sp0 = specSampler(-12);
        spawnPlatform('PEAT_ISLAND', sp0.centerX, -12.0, 11.5);
      }
    }

    // Structured stepping-stone & twin-lane raft layout along the 112m chunk:
    // Slots 0..3 lead up to Grapple Gap 1 (u ~ 0.48), Slots 5..7 lead up to Grapple Gap 2 (u ~ 0.90)
    const rowCount = 10;
    for (let r = 0; r < rowCount; r++) {
      const u = (r + 0.5) / rowCount;
      const pz = zStart - u * chunkLength;
      if (chunkIndex === 0 && pz > -23.0) continue;

      const sp = specSampler(pz);
      const isGrappleGap = r === 4 || r === 8;

      if (isGrappleGap) {
        // Wide center acid-water gap (~18m) with Overhanging Nepenthes Pitcher-Plant Grapple Vines overhead,
        // PLUS flanking outer Victoria lily pads on left/right for momentum-jumpers!
        this.buildOverhangingPitcherGrappleSpan(
          chunkGroup,
          pitcherAnchors,
          chunkIndex,
          pz,
          specSampler,
          rng
        );
        const flankSide = r === 4 ? -1 : 1;
        spawnPlatform(
          'VICTORIA_LILYPAD',
          sp.centerX + flankSide * 15.5,
          pz,
          lerp(5.8, 7.2, rng())
        );
      } else if (r === 2 || r === 6) {
        // Anchor Floating Peat Island + flanking Giant Lily Pad!
        const side = (r + chunkIndex) % 2 === 0 ? -1 : 1;
        const peat = spawnPlatform(
          'PEAT_ISLAND',
          sp.centerX + side * 4.2,
          pz,
          lerp(8.2, 9.8, rng())
        );
        spawnPlatform(
          'VICTORIA_LILYPAD',
          sp.centerX - side * 12.5,
          pz - 2.0,
          lerp(6.0, 7.6, rng())
        );

        // Mount a Colossal Snapping Dionaea Flytrap on the edge of the Peat Island!
        const ftX = peat.x + side * (peat.radius * 0.62);
        const ftZ = peat.z - 1.5;
        // Aim inward across the lily-pad route!
        const aimToTrack = Math.atan2(sp.centerX - ftX, -4.0);
        flytraps.push(
          this.createColossalFlytrap(
            chunkGroup,
            new THREE.Vector3(ftX, peat.baseY + 0.05, ftZ),
            aimToTrack,
            lerp(0.95, 1.25, rng()),
            rng
          )
        );
      } else {
        // Staggered pair of Giant Victoria amazonica Lily-Pad Rafts!
        const primaryOffset = Math.sin(r * 1.7 + chunkIndex) * 6.8;
        spawnPlatform(
          'VICTORIA_LILYPAD',
          sp.centerX + primaryOffset,
          pz,
          lerp(6.4, 8.4, rng())
        );
        if (rng() < 0.72) {
          spawnPlatform(
            'VICTORIA_LILYPAD',
            sp.centerX - primaryOffset * 1.45 + (rng() - 0.5) * 4.0,
            pz - 2.5,
            lerp(5.2, 6.8, rng())
          );
        }
      }
    }

    // 4. Additional Bank-Mounted Colossal Flytraps along the misty lagoon shores (3 more per chunk -> 5 total/chunk!)
    for (let f = 0; f < 3; f++) {
      const fz = zStart - ((f + 0.28 + rng() * 0.35) / 3) * chunkLength;
      if (Math.abs(fz) < 26) continue;
      const sp = specSampler(fz);
      const side = (f + chunkIndex) % 2 === 0 ? -1 : 1;
      const fx = sp.centerX + side * lerp(19.5, 27.5, rng());
      const aimInward = Math.atan2(-side * 16.0, -5.5);
      flytraps.push(
        this.createColossalFlytrap(
          chunkGroup,
          new THREE.Vector3(fx, -0.15, fz),
          aimInward,
          lerp(1.05, 1.38, rng()),
          rng
        )
      );
    }

    // 5. Clusters of Curling Crimson Drosera Sundew Tentacles (9 per chunk!)
    const sundewCount = 9;
    for (let s = 0; s < sundewCount; s++) {
      const sz = zStart - ((s + 0.16 + rng() * 0.55) / sundewCount) * chunkLength;
      if (Math.abs(sz) < 22) continue;
      const sp = specSampler(sz);
      const side = s % 2 === 0 ? -1 : 1;
      const sx = sp.centerX + side * lerp(8.5, 24.0, rng());
      const aimYaw = Math.atan2(sp.centerX - sx, -3.5);
      sundews.push(
        this.createCurlingSundewStalk(
          chunkGroup,
          new THREE.Vector3(sx, -0.25, sz),
          aimYaw,
          lerp(7.5, 11.4, rng()),
          rng
        )
      );
    }

    return {
      chunkIndex,
      zStart,
      platforms,
      flytraps,
      sundews,
      pitcherAnchors,
    };
  }

  /**
   * Sampled by TrailCorridorWorld.sampleHeightRaw so the 4x4's suspension & tires
   * physically tilt, sink, and launch off 3D Victoria Lily-Pad Rafts & Peat Islands,
   * and wade into deep acid-green mire if they fall off or linger until a pad submerges!
   */
  public sampleLiveBogSurface(
    data: CarnivorousBogChunkData,
    x: number,
    z: number
  ): {
    active: boolean;
    height: number;
    waterDepth: number;
    onLilyPad: boolean;
  } {
    if (z > data.zStart + 10.0 || z < data.zStart - 122.0) {
      return { active: false, height: 0, waterDepth: 0, onLilyPad: false };
    }

    let bestY = -999;
    let foundPlatform = false;
    let onLily = false;

    for (const p of data.platforms) {
      const dx = x - p.x;
      const dz = z - p.z;
      const dSq = dx * dx + dz * dz;
      const rMax = p.radius * 1.01;
      if (dSq > rMax * rMax) continue;

      const dist = Math.sqrt(dSq);
      const uRad = dist / p.radius;

      // Exact live 3D tilted plane of this buoyant raft + upturned Victoria rim lip!
      let deckY = p.curY + dx * Math.sin(p.rollZ) - dz * Math.sin(p.pitchX);
      if (p.kind === 'VICTORIA_LILYPAD' && uRad > 0.89) {
        deckY += smoothstep(0.89, 0.99, uRad) * (p.radius * 0.052);
      } else if (p.kind === 'PEAT_ISLAND') {
        deckY += (1.0 - uRad * uRad) * 0.14;
      }

      if (deckY > bestY) {
        bestY = deckY;
        foundPlatform = true;
        onLily = p.kind === 'VICTORIA_LILYPAD';
      }
    }

    if (foundPlatform) {
      const wDepth = Math.max(0, LAGOON_WATER_Y - bestY);
      return {
        active: true,
        height: bestY,
        waterDepth: wDepth,
        onLilyPad: onLily,
      };
    }

    // Open Acid-Green Lagoon Water Gap between lily pads!
    return {
      active: true,
      height: -0.92,
      waterDepth: LAGOON_WATER_Y - -0.92, // 0.54m deep bog lagoon water!
      onLilyPad: false,
    };
  }

  /**
   * Attempts to latch the player's [LMB] Pitcher-Tendril Grapple onto the best forward overhanging Nepenthes Pitcher!
   */
  public tryLatchPitcherGrapple(
    activeChunks: Iterable<CarnivorousBogChunkData>,
    vehiclePos: THREE.Vector3
  ): boolean {
    let bestAnchor: PitcherGrappleAnchor | null = null;
    let bestScore = Infinity;

    for (const chunk of activeChunks) {
      for (const a of chunk.pitcherAnchors) {
        const dz = vehiclePos.z - a.worldPos.z; // Positive when anchor is ahead (-Z)!
        if (dz < -6.0 || dz > 48.0) continue;
        const dx = a.worldPos.x - vehiclePos.x;
        const dist3D = a.worldPos.distanceTo(vehiclePos);
        if (dist3D > 52.0 || Math.abs(dx) > 28.0) continue;

        // Prefer center-aligned forward anchors right over the water gap ahead!
        const score = Math.abs(dz - 21.0) * 1.2 + Math.abs(dx) * 1.4;
        if (score < bestScore) {
          bestScore = score;
          bestAnchor = a;
        }
      }
    }

    if (!bestAnchor) return false;

    this.activeGrappleAnchor = bestAnchor;
    this.grappleRestLength = clamp(
      bestAnchor.worldPos.distanceTo(vehiclePos) * 0.92,
      9.5,
      34.0
    );
    bestAnchor.dipVelY -= 4.8; // Tug the giant pitcher urn visibly downward on latch!
    this.grappleRopeMesh.visible = true;

    if (this.onPitcherGrappleLatch) {
      this.onPitcherGrappleLatch(bestAnchor.worldPos.clone());
    }
    return true;
  }

  public releasePitcherGrapple(vehicleVel?: THREE.Vector3): void {
    if (!this.activeGrappleAnchor) return;
    const relPos = this.activeGrappleAnchor.worldPos.clone();
    this.activeGrappleAnchor = null;
    this.grappleRopeMesh.visible = false;

    // Clean low-arc pendulum release onto the far lily-pad!
    if (vehicleVel && vehicleVel.z < -5.0) {
      vehicleVel.y = clamp(vehicleVel.y, 2.2, 5.6);
      vehicleVel.z -= 2.5;
    }

    if (this.onPitcherGrappleRelease) {
      this.onPitcherGrappleRelease(relPos);
    }
  }

  /**
   * 240Hz Pendulum Rope Constraint for [LMB] Pitcher-Plant Grapple-Swinging across wide water gaps!
   */
  public stepPitcherGrappleSubstep(
    dt: number,
    vehiclePos: THREE.Vector3,
    vehicleVel: THREE.Vector3
  ): void {
    const anchor = this.activeGrappleAnchor;
    if (!anchor) return;

    // Auto-release cleanly on the forward upswing past the gap so the 4x4 arcs onto the far lily-pad!
    if (
      vehiclePos.z < anchor.worldPos.z - 6.5 ||
      (vehiclePos.z < anchor.worldPos.z - 1.5 && vehiclePos.y > 5.2)
    ) {
      this.releasePitcherGrapple(vehicleVel);
      return;
    }

    // Smoothly reel tendril toward (anchorY - 4.0m) so the bottom of the pendulum clears the acid lagoon by ~4m!
    const idealClearanceLen = Math.max(8.5, anchor.worldPos.y - 4.0);
    const isReeling = this.grappleRestLength > idealClearanceLen;
    if (isReeling) {
      this.grappleRestLength = Math.max(
        idealClearanceLen,
        this.grappleRestLength - 8.5 * dt
      );
    }

    const toAnchor = this._vA.copy(anchor.worldPos).sub(vehiclePos);
    const dist = toAnchor.length();
    if (dist < 1e-3) return;
    toAnchor.divideScalar(dist);

    if (dist > this.grappleRestLength) {
      const stretch = dist - this.grappleRestLength;
      // Softly project position onto the taut botanical tendril arc
      vehiclePos.addScaledVector(toAnchor, stretch * 0.28);

      // Remove outward radial velocity along the taut vine so energy converts cleanly into pendulum swing!
      const radialVel = vehicleVel.dot(toAnchor);
      if (radialVel < 0) {
        vehicleVel.addScaledVector(toAnchor, -radialVel);
      }

      // Gentle forward swing pump while airborne below the anchor
      if (vehiclePos.y < anchor.worldPos.y - 3.5) {
        vehicleVel.z -= 9.5 * dt;
      }

      // Pull the overhanging giant Nepenthes pitcher visibly downward under the buggy's weight!
      anchor.dipVelY -= clamp(stretch * 12.0 + 3.5, 0, 18.0) * dt;
    }
  }

  private updateGrappleRopeMesh(vehiclePos: THREE.Vector3, elapsedTime: number): void {
    const anchor = this.activeGrappleAnchor;
    if (!anchor) {
      this.grappleRopeMesh.visible = false;
      return;
    }
    this.grappleRopeMesh.visible = true;

    const start = this._vA.set(vehiclePos.x, vehiclePos.y + 1.05, vehiclePos.z);
    const end = anchor.worldPos;
    const ropeRings = 12;
    const ropeSides = 6;
    const rCols = ropeSides + 1;
    const posAttr = this.grappleRopeGeo.attributes.position as THREE.BufferAttribute;
    const normAttr = this.grappleRopeGeo.attributes.normal as THREE.BufferAttribute;
    const posArr = posAttr.array as Float32Array;
    const normArr = normAttr.array as Float32Array;

    const dx = end.x - start.x;
    const dy = end.y - start.y;
    const dz = end.z - start.z;
    const len = Math.hypot(dx, dy, dz) || 1;
    const tx = dx / len;
    const ty = dy / len;
    const tz = dz / len;

    // Orthonormal basis around tendril direction
    let rx = -tz;
    let ry = 0;
    let rz = tx;
    const rLen = Math.hypot(rx, ry, rz) || 1;
    rx /= rLen;
    rz /= rLen;
    const bx = ty * rz - tz * ry;
    const by = tz * rx - tx * rz;
    const bz = tx * ry - ty * rx;

    for (let r = 0; r < ropeRings; r++) {
      const u = r / (ropeRings - 1);
      // Slight organic tendril coil & vibration along middle
      const vib = Math.sin(u * Math.PI) * Math.sin(elapsedTime * 28.0 + u * 9.0) * 0.08;
      const cx = lerp(start.x, end.x, u) + rx * vib;
      const cy = lerp(start.y, end.y, u) - Math.sin(u * Math.PI) * 0.22;
      const cz = lerp(start.z, end.z, u) + rz * vib;
      const rad = lerp(0.085, 0.065, u);

      for (let c = 0; c < rCols; c++) {
        const ang = (c / ropeSides) * Math.PI * 2 + u * 6.0;
        const ca = Math.cos(ang);
        const sa = Math.sin(ang);
        const nx = rx * ca + bx * sa;
        const ny = ry * ca + by * sa;
        const nz = rz * ca + bz * sa;
        const idx = (r * rCols + c) * 3;
        posArr[idx] = cx + nx * rad;
        posArr[idx + 1] = cy + ny * rad;
        posArr[idx + 2] = cz + nz * rad;
        normArr[idx] = nx;
        normArr[idx + 1] = ny;
        normArr[idx + 2] = nz;
      }
    }
    posAttr.needsUpdate = true;
    normAttr.needsUpdate = true;
  }

  /**
   * Steps 3D Buoyant Lily-Pad Tilting/Submersion, 5-Stage Biomechanical Flytrap Snaps,
   * Logarithmic-Coiling Sundew Tentacles, and Swaying Pitcher Grapple Anchors!
   */
  public stepChunkHazards(
    data: CarnivorousBogChunkData,
    elapsedTime: number,
    dt: number,
    focusPos: THREE.Vector3,
    vehiclePos: THREE.Vector3,
    vehicleVel: THREE.Vector3,
    vehicleSpeedMps: number,
    throttleInput: number,
    steerInput: number,
    isBoosting: boolean,
    passiveObserver: boolean
  ): void {
    this.timeUniform.value = elapsedTime;
    this.flytrapClampCooldown = Math.max(0, this.flytrapClampCooldown - dt);

    // 1. Step Buoyant 3D Victoria Lily-Pad Rafts & Floating Peat Islands!
    for (const p of data.platforms) {
      const dx = vehiclePos.x - p.x;
      const dz = vehiclePos.z - p.z;
      const distXZ = Math.hypot(dx, dz);
      const carOnPad =
        !passiveObserver &&
        distXZ < p.radius * 1.03 &&
        Math.abs(vehiclePos.y - p.curY) < 2.6;

      const waveBob = Math.sin(elapsedTime * 1.55 + p.bobPhase) * 0.035;
      let targetY = p.baseY + waveBob;
      let targetRollZ = Math.cos(elapsedTime * 1.2 + p.bobPhase) * 0.014;
      let targetPitchX = Math.sin(elapsedTime * 1.1 + p.bobPhase * 1.3) * 0.014;

      if (carOnPad) {
        if (p.kind === 'VICTORIA_LILYPAD') {
          // Linger penalty: slow/stopped buggy builds dwellTime rapidly; fast momentum skims lightly!
          const lingerRate =
            vehicleSpeedMps < 5.0 ? 1.35 : vehicleSpeedMps < 13.0 ? 0.75 : 0.32;
          p.dwellTime = Math.min(4.2, p.dwellTime + lingerRate * dt);

          const submergeDepth =
            0.14 + smoothstep(0.65, 2.5, p.dwellTime) * 0.72;
          targetY = p.baseY - submergeDepth;

          if (!p.submerged && targetY < LAGOON_WATER_Y - 0.14) {
            p.submerged = true;
            if (this.onLilyPadSubmerge) {
              this.onLilyPadSubmerge(new THREE.Vector3(p.x, LAGOON_WATER_Y, p.z));
            }
          }

          // Real 3D Weight-Transfer Torque Tilt!
          const normX = clamp(dx / p.radius, -1, 1);
          const normZ = clamp(dz / p.radius, -1, 1);
          targetRollZ = -normX * 0.155; // ~8.9 deg max 3D roll!
          targetPitchX = normZ * 0.155; // ~8.9 deg max 3D pitch!

          p.rippleOpacity = Math.max(p.rippleOpacity, 0.52);
        } else {
          // Heavy Floating Peat Island: supportive recovery hummock with sturdy cushioned tilt
          targetY = p.baseY - 0.09;
          targetRollZ = -clamp(dx / p.radius, -1, 1) * 0.055;
          targetPitchX = clamp(dz / p.radius, -1, 1) * 0.055;
        }
      } else {
        // Recover buoyancy smoothly once weight leaves the lily pad!
        p.dwellTime = Math.max(0, p.dwellTime - 0.85 * dt);
        if (p.dwellTime < 0.5) p.submerged = false;
        if (p.dwellTime > 0.1) {
          targetY = p.baseY - smoothstep(0.65, 2.5, p.dwellTime) * 0.72;
        }
      }

      // Spring-damper integration for organic buoyant mass & momentum!
      const kY = p.kind === 'VICTORIA_LILYPAD' ? 16.0 : 12.0;
      const dY = 4.6;
      p.velY += ((targetY - p.curY) * kY - p.velY * dY) * dt;
      p.curY += p.velY * dt;

      p.velRollZ += ((targetRollZ - p.rollZ) * 18.0 - p.velRollZ * 4.8) * dt;
      p.rollZ += p.velRollZ * dt;

      p.velPitchX += ((targetPitchX - p.pitchX) * 18.0 - p.velPitchX * 4.8) * dt;
      p.pitchX += p.velPitchX * dt;

      p.mesh.position.y = p.curY;
      p.mesh.rotation.set(p.pitchX, p.yaw, p.rollZ, 'YXZ');

      // Expanding acid-green water ripples around loaded/sinking pads
      if (p.rippleOpacity > 0.01) {
        p.rippleRing.visible = true;
        p.rippleScale = 1.02 + ((elapsedTime * 0.85 + p.bobPhase) % 1.0) * 0.36;
        p.rippleRing.scale.setScalar(p.radius * p.rippleScale);
        p.rippleOpacity = Math.max(0, p.rippleOpacity - 0.55 * dt);
      } else {
        p.rippleRing.visible = false;
      }
    }

    // 2. Step 5-Stage Biomechanical Colossal Dionaea Flytraps!
    for (const ft of data.flytraps) {
      if (Math.abs(focusPos.z - ft.basePos.z) > 145.0) continue;

      const toCarX = vehiclePos.x - ft.basePos.x;
      const toCarZ = vehiclePos.z - ft.basePos.z;
      const distToCar = Math.hypot(toCarX, toCarZ);

      if (ft.state === 'POISED_AMBUSH') {
        ft.lobeOpenAngle = lerp(ft.lobeOpenAngle, 0.96, clamp(4.0 * dt, 0, 1));
        ft.lungeForward = lerp(ft.lungeForward, 0.0, clamp(4.0 * dt, 0, 1));
        ft.headDropY = lerp(ft.headDropY, 0.0, clamp(4.0 * dt, 0, 1));

        // Subtle predatory sensory tracking toward vehicle when in Drive Mode
        if (!passiveObserver && distToCar < 26.0) {
          const desiredYaw = Math.atan2(toCarX, toCarZ);
          let diff = desiredYaw - ft.curAimYaw;
          while (diff > Math.PI) diff -= Math.PI * 2;
          while (diff < -Math.PI) diff += Math.PI * 2;
          ft.curAimYaw += clamp(diff, -0.55, 0.55) * clamp(2.8 * dt, 0, 1);
        } else {
          ft.curAimYaw = lerp(ft.curAimYaw, ft.restAimYaw, clamp(1.5 * dt, 0, 1));
        }

        // Trigger 1: Real predatory strike when 4x4 enters strike envelope in Drive Mode!
        if (!passiveObserver && distToCar < 15.5 * ft.scale) {
          ft.state = 'TRIGGER_COIL';
          ft.stateTimer = 0;
          ft.isAmbientSnap = false;
        } else {
          // Trigger 2: Periodic ambient swamp-prey snap further down-course so observers see the full biomechanics!
          ft.ambientCycleTimer -= dt;
          if (ft.ambientCycleTimer <= 0 && (passiveObserver || distToCar > 24.0)) {
            ft.ambientCycleTimer = lerp(6.5, 12.5, ((ft.id * 97) % 100) / 100);
            ft.state = 'TRIGGER_COIL';
            ft.stateTimer = 0;
            ft.isAmbientSnap = true;
          }
        }
      } else if (ft.state === 'TRIGGER_COIL') {
        // 0.30s Muscular S-Stalk Coil-Back & Elastic Lobe Flare!
        ft.stateTimer += dt;
        const u = clamp(ft.stateTimer / 0.3, 0, 1);
        const ease = u * u;
        ft.lobeOpenAngle = lerp(0.96, 1.12, ease); // Flare wider before snap!
        ft.lungeForward = lerp(0.0, -1.45, ease); // Coil backward like a viper!
        ft.headDropY = lerp(0.0, -0.65 * ft.scale, ease);

        if (ft.stateTimer >= 0.3) {
          ft.state = 'BALLISTIC_SNAP';
          ft.stateTimer = 0;
        }
      } else if (ft.state === 'BALLISTIC_SNAP') {
        // 0.095s (95ms!) Explosive Elastic Snap-Buckling Closure!
        ft.stateTimer += dt;
        const u = clamp(ft.stateTimer / 0.095, 0, 1);
        const ballistic = 1.0 - Math.pow(1.0 - u, 3.0);

        ft.lobeOpenAngle = lerp(1.12, 0.055, ballistic); // Interlock 34 curved spines!
        ft.lungeForward = lerp(-1.45, 4.35, ballistic); // Whip forward across the bank!
        ft.headDropY = lerp(-0.65 * ft.scale, -1.85 * ft.scale, ballistic);

        if (ft.stateTimer >= 0.095) {
          ft.state = 'DIGESTIVE_WRITHE';
          ft.stateTimer = 0;

          const jawDist = Math.hypot(
            vehiclePos.x - ft.headPosWorld.x,
            vehiclePos.z - ft.headPosWorld.z
          );
          const caughtVehicle =
            !passiveObserver &&
            !ft.isAmbientSnap &&
            this.flytrapClampCooldown <= 0 &&
            jawDist < 3.85 * ft.scale &&
            Math.abs(vehiclePos.y - ft.headPosWorld.y) < 3.4;

          if (caughtVehicle) {
            ft.clampedVehicleTimer = 1.45;
            this.flytrapClampCooldown = 3.2;
          }

          if (this.onFlytrapSnapShut) {
            this.onFlytrapSnapShut(ft.headPosWorld.clone(), caughtVehicle);
          }
        }
      } else if (ft.state === 'DIGESTIVE_WRITHE') {
        // 1.35s Tightly Clamped Digestive Squeeze & Muscular Thrashing!
        ft.stateTimer += dt;
        ft.lobeOpenAngle = 0.042 + Math.sin(ft.stateTimer * 22.0) * 0.015;
        ft.lungeForward = lerp(4.35, 3.2, clamp(ft.stateTimer / 1.35, 0, 1));

        if (!passiveObserver && ft.clampedVehicleTimer > 0) {
          // Player can mash [SHIFT] Boost + [W]/[A]/[D] to rip free early!
          const breakPower =
            (isBoosting ? 2.6 : 0.7) +
            Math.abs(throttleInput) * 0.65 +
            Math.abs(steerInput) * 0.55;
          ft.clampedVehicleTimer -= breakPower * dt;

          // Pull/anchor chassis toward the clamped crimson jaws until freed!
          const pullX = ft.headPosWorld.x - vehiclePos.x;
          const pullZ = ft.headPosWorld.z - vehiclePos.z;
          vehicleVel.x = lerp(vehicleVel.x, pullX * 3.8, clamp(9.0 * dt, 0, 1));
          vehicleVel.z = lerp(vehicleVel.z, pullZ * 3.8, clamp(9.0 * dt, 0, 1));

          if (ft.clampedVehicleTimer <= 0) {
            // Burst free out of the interlocking spine cage!
            vehicleVel.z -= 8.5;
            vehicleVel.y = Math.max(vehicleVel.y, 2.8);
            ft.state = 'TURGOR_REOPEN';
            ft.stateTimer = 0;
            if (this.onVehicleBreakFreeFlytrap) {
              this.onVehicleBreakFreeFlytrap(vehiclePos.clone());
            }
          }
        }

        if (ft.stateTimer >= 1.35) {
          ft.clampedVehicleTimer = 0;
          ft.state = 'TURGOR_REOPEN';
          ft.stateTimer = 0;
        }
      } else if (ft.state === 'TURGOR_REOPEN') {
        // 2.7s Slow Agonizing Hydraulic Re-Cocking (28x slower than the snap!)
        ft.stateTimer += dt;
        const u = clamp(ft.stateTimer / 2.7, 0, 1);
        const slowTurgor = u * u * (3.0 - 2.0 * u);
        ft.lobeOpenAngle = lerp(0.05, 0.96, slowTurgor);
        ft.lungeForward = lerp(3.2, 0.0, slowTurgor);
        ft.headDropY = lerp(-1.85 * ft.scale, 0.0, slowTurgor);

        if (ft.stateTimer >= 2.7) {
          ft.state = 'POISED_AMBUSH';
          ft.stateTimer = 0;
        }
      }

      this.updateFlytrapKinematics(ft, elapsedTime);
    }

    // 3. Step Logarithmic-Coiling Crimson Drosera Sundew Tentacles!
    for (const sd of data.sundews) {
      if (Math.abs(focusPos.z - sd.basePos.z) > 135.0) continue;

      const distCar = Math.hypot(
        vehiclePos.x - sd.basePos.x,
        vehiclePos.z - sd.basePos.z
      );

      if (!passiveObserver && distCar < 11.5) {
        sd.targetCurl = 1.0;
        sd.holdTimer = 1.4;
      } else if (sd.holdTimer > 0) {
        sd.holdTimer -= dt;
      } else {
        sd.ambientTimer -= dt;
        if (sd.ambientTimer <= 0) {
          sd.ambientTimer = lerp(5.0, 11.0, ((sd.id * 53) % 100) / 100);
          sd.targetCurl = sd.targetCurl > 0.5 ? 0.12 : 0.92;
          sd.holdTimer = 1.8;
        } else if (sd.holdTimer <= 0 && sd.targetCurl > 0.5) {
          sd.targetCurl = 0.12;
        }
      }

      // Fast thigmonastic wrap (4.2/s), slow viscous uncoiling (0.85/s)!
      const rate = sd.targetCurl > sd.curlProgress ? 4.2 : 0.85;
      if (Math.abs(sd.targetCurl - sd.curlProgress) > 0.002) {
        sd.curlProgress = lerp(sd.curlProgress, sd.targetCurl, clamp(rate * dt, 0, 1));
      }
      this.updateSundewLogSpiralGeometry(sd, elapsedTime);

      // Sticky ruby mucilage drag if vehicle brushes the curled glandular tip!
      if (!passiveObserver && sd.curlProgress > 0.45) {
        const dTip = Math.hypot(
          vehiclePos.x - sd.tipWorldPos.x,
          vehiclePos.z - sd.tipWorldPos.z
        );
        if (dTip < 3.8 && this.sundewMucilageTimer <= 0) {
          this.sundewMucilageTimer = 1.35;
          if (this.onSundewMucilageHit) {
            this.onSundewMucilageHit(sd.tipWorldPos.clone());
          }
        }
      }
    }

    // Apply viscous sundew mucilage tire drag if coated (unless burning through with Boost!)
    if (this.sundewMucilageTimer > 0) {
      this.sundewMucilageTimer = Math.max(
        0,
        this.sundewMucilageTimer - (isBoosting ? 2.8 : 1.0) * dt
      );
      if (!passiveObserver && !isBoosting) {
        const drag = Math.exp(-2.6 * dt);
        vehicleVel.x *= drag;
        vehicleVel.z *= drag;
      }
    }

    // 4. Step Swaying Overhanging Nepenthes Pitcher-Plant Grapple Anchors!
    for (const pa of data.pitcherAnchors) {
      const springF = -pa.dipY * 18.0 - pa.dipVelY * 3.8;
      pa.dipVelY += springF * dt;
      pa.dipY = clamp(pa.dipY + pa.dipVelY * dt, -2.2, 0.8);

      const swayX = Math.sin(elapsedTime * 1.25 + pa.swayPhase) * 0.32;
      const swayZ = Math.cos(elapsedTime * 1.05 + pa.swayPhase) * 0.22;
      pa.worldPos.set(
        pa.restPos.x + swayX,
        pa.restPos.y + pa.dipY,
        pa.restPos.z + swayZ
      );
      pa.pitcherGroup.position.copy(pa.worldPos);
      pa.pitcherGroup.rotation.z = swayX * 0.14;
      pa.pitcherGroup.rotation.x = -swayZ * 0.14;

      // Show bioluminescent [LMB] grapple target halo when player approaches in Drive Mode!
      const dz = vehiclePos.z - pa.worldPos.z;
      const inRange =
        !passiveObserver &&
        dz > -5.0 &&
        dz < 48.0 &&
        pa.worldPos.distanceTo(vehiclePos) < 52.0;
      pa.haloMesh.visible = inRange || this.activeGrappleAnchor === pa;
      if (pa.haloMesh.visible) {
        pa.haloMesh.lookAt(focusPos);
        const pulse =
          this.activeGrappleAnchor === pa
            ? 1.35
            : 0.92 + Math.sin(elapsedTime * 6.5 + pa.id) * 0.14;
        pa.haloMesh.scale.setScalar(pulse);
      }
    }
  }

  /**
   * Steps drifting bioluminescent acid-chartreuse bog spores & updates the 3D [LMB] grapple rope!
   */
  public stepGlobalBogAtmosphere(
    active: boolean,
    focusPos: THREE.Vector3,
    vehiclePos: THREE.Vector3,
    elapsedTime: number,
    dt: number
  ): void {
    this.atmosphereFxGroup.visible = active;
    if (!active) {
      if (this.activeGrappleAnchor) this.releasePitcherGrapple();
      return;
    }

    const posAttr = this.sporePoints.geometry.attributes.position as THREE.BufferAttribute;
    const arr = this.sporePositions;
    const vels = this.sporeVelocities;

    for (let i = 0; i < this.sporeCount; i++) {
      const ix = i * 3;
      arr[ix] += (vels[ix] + Math.sin(elapsedTime * 0.9 + i) * 0.35) * dt;
      arr[ix + 1] += vels[ix + 1] * dt;
      arr[ix + 2] += (vels[ix + 2] + Math.cos(elapsedTime * 0.8 + i) * 0.35) * dt;

      const dx = arr[ix] - focusPos.x;
      const dz = arr[ix + 2] - focusPos.z;
      if (dx > 70) arr[ix] -= 140;
      else if (dx < -70) arr[ix] += 140;

      if (dz > 80) arr[ix + 2] -= 160;
      else if (dz < -80) arr[ix + 2] += 160;

      if (arr[ix + 1] > focusPos.y + 22.0) {
        arr[ix + 1] = -0.3;
      }
    }
    posAttr.needsUpdate = true;

    this.updateGrappleRopeMesh(vehiclePos, elapsedTime);
  }
}

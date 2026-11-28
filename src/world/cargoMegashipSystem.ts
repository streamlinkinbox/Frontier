import * as THREE from 'three';
import { ProceduralTextureLibrary } from '../graphics/proceduralTextures';
import { clamp, lerp, mulberry32, smoothstep } from '../math/noise';
import { WorldObstacle } from './trailCorridor';

export interface LocalContainerBox {
  /** Local coordinates inside `shipGroup` (`lx`, `lz`, `baseY` above local deck 0) */
  lx: number;
  lz: number;
  halfW: number;
  halfL: number;
  bottomY: number;
  topY: number;
  /** True if players can drive onto the roof (`topY`) via an attached Ro-Ro ramp! */
  drivableRoof: boolean;
}

export interface LocalRampSpan {
  lx: number;
  lzStart: number;
  lzEnd: number;
  halfW: number;
  yStart: number;
  yEnd: number;
}

/**
 * Unsecured Loose Cargo Container / Heavy Industrial Crate that slides & spins
 * across the wet tilted steel ship deck as storm waves roll, pitch & yaw the hull!
 */
export interface SlidingDeckCargo {
  lx: number;
  lz: number;
  vx: number;
  vz: number;
  localYaw: number;
  yawVel: number;
  halfW: number;
  halfL: number;
  height: number;
  bayZMin: number;
  bayZMax: number;
  /** Cached world-space velocity for 240Hz vehicle momentum transfer */
  worldVx: number;
  worldVz: number;
  slamCooldown: number;
  meshGroup: THREE.Group;
}

/**
 * Rotating Shipboard Industrial Electromagnet Crane that sweeps across the ship deck,
 * pulls nearby vehicles inward, HOISTS & SWINGS captured vehicles high above the deck,
 * and drops them when `[SHIFT]` Boost overloads the magnet or the crane cable shorts out!
 */
export interface MagneticVehicleCrane {
  parentShip: CargoMegashipInstance;
  pedestalLx: number;
  pedestalLz: number;
  boomLength: number;
  boomTopY: number;
  baseBoomYaw: number;
  sweepAmp: number;
  sweepFreq: number;
  phase: number;
  /** Live crane kinematic state */
  currentBoomYaw: number;
  localTipX: number;
  localTipZ: number;
  localMagnetY: number;
  magnetWorldPos: THREE.Vector3;
  prevMagnetWorldPos: THREE.Vector3;
  magnetWorldVel: THREE.Vector3;
  hasPrevWorldPos: boolean;
  state: 'SWEEPING' | 'CAPTURED' | 'COOLDOWN';
  stateTimer: number;
  boostBreakAccum: number;
  pendulumSwayX: number;
  pendulumSwayZ: number;
  turretGroup: THREE.Group;
  cableMesh: THREE.Mesh;
  magnetHeadGroup: THREE.Group;
  coilGlowMesh: THREE.Mesh;
  dangerRingMesh: THREE.Mesh;
  beaconMesh: THREE.Mesh;
}

/**
 * Towering Malfunctioning Cargo Crane whose hoist cables overload, spark, SNAP & RELEASE
 * a multi-ton 20ft Shipping Container in freefall onto the pitching ship deck, where the
 * dropped container crashes and slides across the tilted deck with the waves!
 */
export interface BreakableCargoCrane {
  parentShip: CargoMegashipInstance;
  pedestalLx: number;
  pedestalLz: number;
  boomLength: number;
  boomTopY: number;
  baseBoomYaw: number;
  sweepAmp: number;
  period: number;
  phase: number;
  state: 'HOIST_SWAY' | 'OVERLOAD_SPARKING' | 'SNAPPED_FREEFALL' | 'CRASHED_SLIDING';
  prevState: 'HOIST_SWAY' | 'OVERLOAD_SPARKING' | 'SNAPPED_FREEFALL' | 'CRASHED_SLIDING';
  /** Live local coordinates of the suspended or dropped 20ft container inside `shipGroup` */
  cargoLx: number;
  cargoLy: number;
  cargoLz: number;
  cargoFallVy: number;
  cargoSlideVx: number;
  cargoSlideVz: number;
  cargoRollTilt: number;
  cargoLocalYaw: number;
  halfW: number;
  halfL: number;
  height: number;
  worldVx: number;
  worldVz: number;
  turretGroup: THREE.Group;
  intactCablesGroup: THREE.Group;
  snappedCableMesh: THREE.Mesh;
  cargoGroup: THREE.Group;
  impactRingMesh: THREE.Mesh;
  beaconMesh: THREE.Mesh;
}

export interface CargoMegashipInstance {
  x: number;
  z: number;
  baseY: number;
  halfBeam: number;
  halfLength: number;
  /** Unsteady 4-DOF storm wave animation parameters (`Heave + Pitch + Roll + Wave Yaw!`) */
  heaveAmp: number;
  heaveFreq: number;
  pitchAmp: number;
  pitchFreq: number;
  rollAmp: number;
  rollFreq: number;
  yawAmp: number;
  yawFreq: number;
  phase: number;
  /** Live state updated each frame */
  liveCenterY: number;
  dCenterYDt: number;
  livePitch: number;
  dPitchDt: number;
  liveRoll: number;
  dRollDt: number;
  liveYaw: number;
  dYawDt: number;
  rowIdx: number;
  shipIdx: number;
  shipGroup: THREE.Group;
  radarSpinner: THREE.Object3D | null;
  containerBoxes: LocalContainerBox[];
  rooftopRamps: LocalRampSpan[];
  slidingCargos: SlidingDeckCargo[];
  magneticCranes: MagneticVehicleCrane[];
  breakableCranes: BreakableCargoCrane[];
  surfWashZ: number;
  surfWashPhase: number;
  surfFoamMesh: THREE.Mesh | null;
}

/**
 * Overhead Port Ship-to-Shore Gantry Crane that slides & locks a coupled 40ft Container Drawbridge
 * across the storm-tossed ocean channel between Ship A's yawing/pitching bow and Ship B's stern!
 */
export interface GantryContainerBridge {
  srcShip: CargoMegashipInstance;
  dstShip: CargoMegashipInstance;
  srcLocalX: number;
  srcLocalZ: number;
  dstLocalX: number;
  dstLocalZ: number;
  baseSpanLen: number;
  ax: number;
  az: number;
  bx: number;
  bz: number;
  dirX: number;
  dirZ: number;
  rightX: number;
  rightZ: number;
  yaw: number;
  spanLen: number;
  halfWidth: number;
  period: number;
  phase: number;
  slideDirSign: number;
  /** Live crane lateral offset (0 when LOCKED, up to +-14.5m when sliding cargo!) */
  currentLatOffset: number;
  liveLatVel: number;
  currentLiftY: number;
  liveAy: number;
  liveBy: number;
  isLocked: boolean;
  wasLocked: boolean;
  trolleyGroup: THREE.Group;
  bridgeSpanGroup: THREE.Group;
  beaconMeshes: THREE.Mesh[];
}

/**
 * Articulated Steel Ro-Ro Stern-to-Bow Ramp OR Forecastle Stunt Jump Ramp
 * whose endpoints dynamically track the live 3D yawing/pitching/rolling bow & stern of both ships!
 */
export interface ArticulatedShipSpan {
  kind: 'RORO_BRIDGE' | 'BOW_JUMP_RAMP';
  srcShip: CargoMegashipInstance;
  dstShip: CargoMegashipInstance;
  srcLocalX: number;
  srcLocalZ: number;
  dstLocalX: number;
  dstLocalZ: number;
  baseSpanLen: number;
  ax: number;
  az: number;
  bx: number;
  bz: number;
  dirX: number;
  dirZ: number;
  rightX: number;
  rightZ: number;
  yaw: number;
  spanLen: number;
  halfWidth: number;
  liveAy: number;
  liveBy: number;
  launchGroup: THREE.Group;
  catchGroup: THREE.Group | null;
}

export type ShipLinkKind = 'GANTRY_CRANE_BRIDGE' | 'RORO_BRIDGE' | 'BOW_JUMP_RAMP';

export interface ShipGraphLink {
  fromIdx: number;
  toIdx: number;
  kind: ShipLinkKind;
}

export interface CargoMegashipChunkData {
  group: THREE.Group;
  ships: CargoMegashipInstance[];
  ownedShips: CargoMegashipInstance[];
  gantryBridges: GantryContainerBridge[];
  shipSpans: ArticulatedShipSpan[];
  zStart: number;
  zLength: number;
}

const SHIP_ROW_SPACING_Z = 112.0; // 1 full Megaship Fleet Row (`80m` ship + `32m` ocean channel) per 112m chunk!
const FIRST_SHIP_CENTER_Z = -44.0; // Stern starts right at `z = -2m` so you spawn right onto Flagship 0's stern deck!
// Multi-ship fleet branching & convergence: 1 -> 2 -> 3 -> 2 -> (repeat)!
const ROW_SHIP_COUNTS = [1, 2, 3, 2] as const;

const CONTAINER_PALETTE = [
  0xb83527, // Hapag / Maersk Rust-Crimson
  0x1c5b8e, // Maritime Ocean Blue
  0xc98b24, // MSC Ochre-Amber
  0x1f6e5c, // Evergreen Sea-Emerald
  0x7a3326, // Oxide Maroon
  0x2c4257, // Slate Navy
] as const;

export class CargoMegashipBuilder {
  public readonly atmosphereFxGroup = new THREE.Group();
  public currentLightningFlash = 0.0;
  public activeCapturedCrane: MagneticVehicleCrane | null = null;

  // Materials
  private readonly hullSteelMat: THREE.MeshStandardMaterial;
  private readonly deckSteelMat: THREE.MeshStandardMaterial;
  private readonly bridgeWhiteMat: THREE.MeshStandardMaterial;
  private readonly bridgeWindowGlowMat: THREE.MeshBasicMaterial;
  private readonly containerMat: THREE.MeshStandardMaterial;
  private readonly containerWoodFloorMat: THREE.MeshStandardMaterial;
  private readonly gantryCraneMat: THREE.MeshStandardMaterial;
  private readonly yellowCraneMat: THREE.MeshStandardMaterial;
  private readonly gantryDarkSteelMat: THREE.MeshStandardMaterial;
  private readonly hazardStripeMat: THREE.MeshStandardMaterial;
  private readonly lockGreenBeaconMat: THREE.MeshBasicMaterial;
  private readonly slideAmberBeaconMat: THREE.MeshBasicMaterial;
  private readonly dangerRedBeaconMat: THREE.MeshBasicMaterial;
  private readonly magnetCyanGlowMat: THREE.MeshBasicMaterial;
  private readonly magnetDimMat: THREE.MeshBasicMaterial;
  private readonly magnetFieldRingMat: THREE.MeshBasicMaterial;
  private readonly dropDangerRingMat: THREE.MeshBasicMaterial;
  private readonly navPortRedMat: THREE.MeshBasicMaterial;
  private readonly navStarboardGreenMat: THREE.MeshBasicMaterial;
  private readonly oceanStormMat: THREE.ShaderMaterial;
  private readonly deckSurfFoamMat: THREE.MeshBasicMaterial;

  // Shared Geometries
  private readonly shipHullGeo: THREE.BufferGeometry;
  private readonly corrugatedBoxGeo: THREE.BufferGeometry;
  private readonly unitBoxGeo: THREE.BoxGeometry;
  private readonly cableCylGeo: THREE.CylinderGeometry;
  private readonly pedestalCylGeo: THREE.CylinderGeometry;
  private readonly magnetDiscGeo: THREE.CylinderGeometry;
  private readonly magnetCoilTorusGeo: THREE.TorusGeometry;
  private readonly deckZoneRingGeo: THREE.RingGeometry;
  private readonly beaconSphereGeo: THREE.SphereGeometry;
  private readonly surfPlaneGeo: THREE.PlaneGeometry;

  // Global Storm Ocean Mesh & Slanting Rain Particles
  private oceanMesh!: THREE.Mesh;
  private readonly rainCount = 1350;
  private rainPoints!: THREE.Points;
  private rainPositions!: Float32Array;
  private lightningTimer = 4.2;

  // Callbacks
  public onGantryBridgeLocked: ((pos: THREE.Vector3) => void) | null = null;
  public onStormThunder: ((intensity: number) => void) | null = null;
  public onMagnetCapture: ((pos: THREE.Vector3) => void) | null = null;
  public onMagnetRelease: ((pos: THREE.Vector3, brokeByBoost: boolean) => void) | null = null;
  public onCraneBreakSpark: ((pos: THREE.Vector3) => void) | null = null;
  public onCraneCargoCrash: ((pos: THREE.Vector3) => void) | null = null;
  public onLooseCargoSlam: ((pos: THREE.Vector3) => void) | null = null;

  private readonly _dummy = new THREE.Object3D();
  private readonly _euler = new THREE.Euler();
  private readonly _quat = new THREE.Quaternion();
  private readonly _targetQuat = new THREE.Quaternion();
  private readonly _color = new THREE.Color();
  private readonly _hitContact = new THREE.Vector3();
  private readonly _tmpA = new THREE.Vector3();
  private readonly _tmpB = new THREE.Vector3();

  constructor(textures: ProceduralTextureLibrary) {
    this.atmosphereFxGroup.name = 'CargoMegashipsAtmosphereFX';

    this.hullSteelMat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.55, 0.55),
      roughness: 0.42,
      metalness: 0.68,
      vertexColors: true,
      side: THREE.DoubleSide,
    });

    this.deckSteelMat = new THREE.MeshStandardMaterial({
      color: 0x323b44,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.85, 0.85),
      roughness: 0.36, // Wet rain-slicked steel weather deck!
      metalness: 0.68,
    });

    this.bridgeWhiteMat = new THREE.MeshStandardMaterial({
      color: 0xdce4ec,
      map: textures.rock.map,
      roughness: 0.44,
      metalness: 0.32,
    });

    this.bridgeWindowGlowMat = new THREE.MeshBasicMaterial({
      color: 0xffd28a,
    });

    const corrTex = this.createCorrugatedContainerTexture();
    this.containerMat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      map: corrTex,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.45, 0.45),
      roughness: 0.46,
      metalness: 0.58,
    });

    this.containerWoodFloorMat = new THREE.MeshStandardMaterial({
      color: 0x70563c,
      map: textures.bark.map,
      roughness: 0.72,
      metalness: 0.12,
    });

    this.gantryCraneMat = new THREE.MeshStandardMaterial({
      color: 0xc94228, // Iconic Port Gantry Signal Orange-Red!
      map: textures.rock.map,
      roughness: 0.42,
      metalness: 0.66,
    });

    this.yellowCraneMat = new THREE.MeshStandardMaterial({
      color: 0xd89b1c, // Industrial Electromagnet Crane Safety Ochre-Yellow!
      map: textures.rock.map,
      roughness: 0.38,
      metalness: 0.64,
    });

    this.gantryDarkSteelMat = new THREE.MeshStandardMaterial({
      color: 0x202730,
      map: textures.rock.map,
      roughness: 0.48,
      metalness: 0.76,
    });

    const hazardTex = this.createHazardChevronTexture();
    this.hazardStripeMat = new THREE.MeshStandardMaterial({
      map: hazardTex,
      roughness: 0.45,
      metalness: 0.42,
    });

    this.lockGreenBeaconMat = new THREE.MeshBasicMaterial({ color: 0x36ff8f });
    this.slideAmberBeaconMat = new THREE.MeshBasicMaterial({ color: 0xff9d24 });
    this.dangerRedBeaconMat = new THREE.MeshBasicMaterial({ color: 0xff2b2b });
    this.magnetCyanGlowMat = new THREE.MeshBasicMaterial({ color: 0x45f3ff });
    this.magnetDimMat = new THREE.MeshBasicMaterial({ color: 0x224c58 });

    this.magnetFieldRingMat = new THREE.MeshBasicMaterial({
      color: 0x3cf0ff,
      transparent: true,
      opacity: 0.52,
      blending: THREE.AdditiveBlending,
      side: THREE.DoubleSide,
      depthWrite: false,
    });

    this.dropDangerRingMat = new THREE.MeshBasicMaterial({
      color: 0xff3b24,
      transparent: true,
      opacity: 0.58,
      blending: THREE.AdditiveBlending,
      side: THREE.DoubleSide,
      depthWrite: false,
    });

    this.navPortRedMat = new THREE.MeshBasicMaterial({ color: 0xff3838 });
    this.navStarboardGreenMat = new THREE.MeshBasicMaterial({ color: 0x2eff78 });

    this.deckSurfFoamMat = new THREE.MeshBasicMaterial({
      color: 0xd9f2ff,
      transparent: true,
      opacity: 0.48,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });

    // Shared Geometries
    this.shipHullGeo = this.createUnitMegashipHullGeometry();
    this.corrugatedBoxGeo = new THREE.BoxGeometry(1, 1, 1);
    this.unitBoxGeo = new THREE.BoxGeometry(1, 1, 1);
    this.cableCylGeo = new THREE.CylinderGeometry(0.065, 0.065, 1.0, 6);
    this.pedestalCylGeo = new THREE.CylinderGeometry(1.15, 1.45, 1.0, 12);
    this.magnetDiscGeo = new THREE.CylinderGeometry(1.75, 1.85, 0.72, 20);
    this.magnetCoilTorusGeo = new THREE.TorusGeometry(1.58, 0.22, 10, 24);
    this.magnetCoilTorusGeo.rotateX(Math.PI * 0.5);

    this.deckZoneRingGeo = new THREE.RingGeometry(2.7, 4.1, 28);
    this.deckZoneRingGeo.rotateX(-Math.PI * 0.5);

    this.beaconSphereGeo = new THREE.SphereGeometry(0.28, 10, 8);
    this.surfPlaneGeo = new THREE.PlaneGeometry(1, 1, 8, 4);
    this.surfPlaneGeo.rotateX(-Math.PI * 0.5);

    // 3D Vertex-Displaced Storm-Tossed North-Atlantic Swell Shader!
    this.oceanStormMat = new THREE.ShaderMaterial({
      uniforms: {
        uTime: { value: 0 },
        uFlash: { value: 0 },
      },
      vertexShader: /* glsl */ `
        uniform float uTime;
        varying vec3 vWorldPos;
        varying float vCrest;
        void main() {
          vec4 wp = modelMatrix * vec4(position, 1.0);
          // Stronger, steeper rogue ocean swells matching the unsteady pitching/rolling/yawing ships!
          float w1 = sin(wp.x * 0.062 + wp.z * 0.048 + uTime * 1.65) * 2.15;
          float w2 = cos(wp.x * 0.038 - wp.z * 0.075 + uTime * 1.28) * 1.55;
          float w3 = sin((wp.x - wp.z) * 0.13 + uTime * 2.55) * 0.62;
          float h = w1 + w2 + w3;
          wp.y += h;
          vWorldPos = wp.xyz;
          vCrest = clamp((h - 0.95) / 2.65, 0.0, 1.0);
          gl_Position = projectionMatrix * viewMatrix * wp;
        }
      `,
      fragmentShader: /* glsl */ `
        uniform float uTime;
        uniform float uFlash;
        varying vec3 vWorldPos;
        varying float vCrest;
        void main() {
          vec3 troughCol = vec3(0.025, 0.075, 0.125);
          vec3 midWaveCol = vec3(0.065, 0.19, 0.28);
          vec3 foamCol = vec3(0.76, 0.89, 0.96);

          float streak = sin(vWorldPos.x * 0.35 + sin(vWorldPos.z * 0.18 + uTime * 1.6) * 2.4) * 0.5 + 0.5;
          float foamMask = smoothstep(0.32, 0.92, vCrest + streak * 0.26);

          vec3 col = mix(troughCol, midWaveCol, clamp(vCrest * 1.25, 0.0, 1.0));
          col = mix(col, foamCol, foamMask * 0.78);
          col += vec3(0.38, 0.48, 0.62) * uFlash * (0.35 + foamMask * 0.65);

          float dist = length(vWorldPos.xz - cameraPosition.xz);
          float alpha = 1.0 - smoothstep(145.0, 245.0, dist);
          gl_FragColor = vec4(col, alpha);
        }
      `,
      transparent: true,
      depthWrite: true,
    });

    const oceanGeo = new THREE.PlaneGeometry(360, 340, 88, 88);
    oceanGeo.rotateX(-Math.PI * 0.5);
    this.oceanMesh = new THREE.Mesh(oceanGeo, this.oceanStormMat);
    this.oceanMesh.position.y = -14.5;
    this.oceanMesh.frustumCulled = false;
    this.atmosphereFxGroup.add(this.oceanMesh);

    // Wind-Driven Slanting Storm Rain Streaks!
    this.rainPositions = new Float32Array(this.rainCount * 3);
    const rng = mulberry32(448219);
    for (let i = 0; i < this.rainCount; i++) {
      const ix = i * 3;
      this.rainPositions[ix] = (rng() - 0.5) * 175;
      this.rainPositions[ix + 1] = lerp(-12.0, 34.0, rng());
      this.rainPositions[ix + 2] = (rng() - 0.5) * 165;
    }
    const rGeo = new THREE.BufferGeometry();
    rGeo.setAttribute('position', new THREE.BufferAttribute(this.rainPositions, 3));
    const rMat = new THREE.PointsMaterial({
      color: 0xa8cbe0,
      size: 0.28,
      transparent: true,
      opacity: 0.65,
      depthWrite: false,
    });
    this.rainPoints = new THREE.Points(rGeo, rMat);
    this.rainPoints.frustumCulled = false;
    this.atmosphereFxGroup.add(this.rainPoints);
  }

  public releaseAnyCapturedCrane(): void {
    if (this.activeCapturedCrane) {
      this.activeCapturedCrane.state = 'COOLDOWN';
      this.activeCapturedCrane.stateTimer = 0;
      this.activeCapturedCrane = null;
    }
  }

  // ===========================================================================
  // PROCEDURAL CORRUGATED STEEL & HAZARD CHEVRON TEXTURES + HULL GEOMETRY
  // ===========================================================================

  private createCorrugatedContainerTexture(): THREE.CanvasTexture {
    const canvas = document.createElement('canvas');
    canvas.width = 256;
    canvas.height = 128;
    const ctx = canvas.getContext('2d');
    if (ctx) {
      ctx.fillStyle = '#d8dde2';
      ctx.fillRect(0, 0, 256, 128);
      for (let x = 0; x < 256; x += 8) {
        ctx.fillStyle = x % 16 === 0 ? '#aeb6bf' : '#f2f5f8';
        ctx.fillRect(x, 6, 4, 116);
      }
      ctx.strokeStyle = '#6e7680';
      ctx.lineWidth = 6;
      ctx.strokeRect(3, 3, 250, 122);
    }
    const tex = new THREE.CanvasTexture(canvas);
    tex.wrapS = THREE.RepeatWrapping;
    tex.wrapT = THREE.RepeatWrapping;
    return tex;
  }

  private createHazardChevronTexture(): THREE.CanvasTexture {
    const canvas = document.createElement('canvas');
    canvas.width = 128;
    canvas.height = 128;
    const ctx = canvas.getContext('2d');
    if (ctx) {
      ctx.fillStyle = '#e0a01b';
      ctx.fillRect(0, 0, 128, 128);
      ctx.fillStyle = '#1c2026';
      for (let i = -128; i < 256; i += 32) {
        ctx.beginPath();
        ctx.moveTo(i, 0);
        ctx.lineTo(i + 16, 0);
        ctx.lineTo(i + 144, 128);
        ctx.lineTo(i + 128, 128);
        ctx.closePath();
        ctx.fill();
      }
    }
    const tex = new THREE.CanvasTexture(canvas);
    tex.wrapS = THREE.RepeatWrapping;
    tex.wrapT = THREE.RepeatWrapping;
    return tex;
  }

  private createUnitMegashipHullGeometry(): THREE.BufferGeometry {
    const segs = 40;
    const rings = [
      { y: 0.0, scaleR: 0.0, flareBow: 1.0, col: [0.22, 0.26, 0.3] }, // Center weather deck
      { y: 0.0, scaleR: 0.98, flareBow: 1.0, col: [0.25, 0.29, 0.34] }, // Weather deck edge
      { y: 0.55, scaleR: 1.0, flareBow: 1.03, col: [0.16, 0.2, 0.25] }, // Raised safety gunwale coaming
      { y: -0.1, scaleR: 1.01, flareBow: 1.04, col: [0.14, 0.18, 0.23] }, // Upper hull sheer strake
      { y: -6.8, scaleR: 0.98, flareBow: 0.92, col: [0.11, 0.15, 0.2] }, // Dark navy-charcoal topsides
      { y: -9.5, scaleR: 0.92, flareBow: 0.85, col: [0.68, 0.18, 0.14] }, // Oxide-red Plimsoll boot-top!
      { y: -18.5, scaleR: 0.74, flareBow: 0.72, col: [0.46, 0.12, 0.1] }, // Deep submerged bulbous keel!
    ];

    const positions: number[] = [];
    const colors: number[] = [];
    const uvs: number[] = [];
    const indices: number[] = [];

    for (let r = 0; r < rings.length; r++) {
      const ring = rings[r];
      for (let s = 0; s <= segs; s++) {
        const u = s / segs;
        const theta = u * Math.PI * 2;
        const rawX = Math.cos(theta);
        const rawZ = Math.sin(theta);

        const absSin = Math.abs(rawZ);
        const midbodyX = Math.sign(rawX) * Math.pow(Math.abs(rawX), 0.42);
        const bowPinch =
          rawZ < -0.45 ? lerp(1.0, 0.74 * ring.flareBow, (-rawZ - 0.45) / 0.55) : 1.0;
        const sternSquare =
          rawZ > 0.5 ? lerp(1.0, 0.92, (rawZ - 0.5) / 0.5) : 1.0;

        const px =
          ring.scaleR === 0
            ? 0
            : midbodyX * ring.scaleR * bowPinch * sternSquare;
        const pz =
          ring.scaleR === 0
            ? 0
            : Math.sign(rawZ) * Math.pow(absSin, 0.76) * ring.scaleR;

        positions.push(px, ring.y, pz);
        colors.push(ring.col[0], ring.col[1], ring.col[2]);
        uvs.push(px * 4.0, pz * 8.0 + ring.y * 0.5);
      }
    }

    const stride = segs + 1;
    for (let r = 0; r < rings.length - 1; r++) {
      for (let s = 0; s < segs; s++) {
        const a = r * stride + s;
        const b = a + 1;
        const c = (r + 1) * stride + s;
        const d = c + 1;
        indices.push(a, b, d, a, d, c);
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

  // ===========================================================================
  // EXACT 4-DOF UNSTEADY STORM POSE (`HEAVE + PITCH + ROLL + WAVE YAW`)
  // ===========================================================================

  public sampleShipPose(
    spec: {
      baseY: number;
      heaveAmp: number;
      heaveFreq: number;
      pitchAmp: number;
      pitchFreq: number;
      rollAmp: number;
      rollFreq: number;
      yawAmp: number;
      yawFreq: number;
      phase: number;
    },
    elapsed: number
  ): {
    centerY: number;
    dCenterYDt: number;
    pitch: number;
    dPitchDt: number;
    roll: number;
    dRollDt: number;
    yaw: number;
    dYawDt: number;
  } {
    const hArg = elapsed * spec.heaveFreq + spec.phase;
    const pArg = elapsed * spec.pitchFreq + spec.phase * 1.35 + 0.4;
    const rArg = elapsed * spec.rollFreq + spec.phase * 0.85 + 1.1;
    const rRogueArg = elapsed * (spec.rollFreq * 1.85) + spec.phase * 1.7 + 2.2;
    const yArg = elapsed * spec.yawFreq + spec.phase * 1.15 + 0.75;

    // Compound storm swell + rogue cross-wave roll & wave-induced hull yaw rotation!
    const centerY =
      spec.baseY +
      spec.heaveAmp * Math.sin(hArg) +
      0.32 * spec.heaveAmp * Math.cos(hArg * 1.7 + 0.8);
    const dCenterYDt =
      spec.heaveAmp * spec.heaveFreq * Math.cos(hArg) -
      0.32 * spec.heaveAmp * (spec.heaveFreq * 1.7) * Math.sin(hArg * 1.7 + 0.8);

    const pitch =
      spec.pitchAmp * Math.sin(pArg) + 0.24 * spec.pitchAmp * Math.sin(pArg * 1.9 + 1.2);
    const dPitchDt =
      spec.pitchAmp * spec.pitchFreq * Math.cos(pArg) +
      0.24 * spec.pitchAmp * (spec.pitchFreq * 1.9) * Math.cos(pArg * 1.9 + 1.2);

    const roll =
      spec.rollAmp * Math.sin(rArg) + 0.28 * spec.rollAmp * Math.sin(rRogueArg);
    const dRollDt =
      spec.rollAmp * spec.rollFreq * Math.cos(rArg) +
      0.28 * spec.rollAmp * (spec.rollFreq * 1.85) * Math.cos(rRogueArg);

    const yaw = spec.yawAmp * Math.sin(yArg);
    const dYawDt = spec.yawAmp * spec.yawFreq * Math.cos(yArg);

    return {
      centerY,
      dCenterYDt,
      pitch,
      dPitchDt,
      roll,
      dRollDt,
      yaw,
      dYawDt,
    };
  }

  /**
   * Exact 3D world position of `(lx, localY, lz)` inside `shipGroup` rotated by `Euler(pitch, yaw, roll, 'YXZ')`!
   * In Three.js `'YXZ'`: `v_world = center + Ry(yaw) * Rx(pitch) * Rz(roll) * v_local`.
   */
  public shipLocalToWorld(
    ship: Pick<
      CargoMegashipInstance,
      'x' | 'z' | 'liveCenterY' | 'livePitch' | 'liveRoll' | 'liveYaw'
    >,
    lx: number,
    localY: number,
    lz: number,
    out: THREE.Vector3
  ): THREE.Vector3 {
    const sinR = Math.sin(ship.liveRoll);
    const cosR = Math.cos(ship.liveRoll);
    const sinP = Math.sin(ship.livePitch);
    const cosP = Math.cos(ship.livePitch);
    const sinY = Math.sin(ship.liveYaw);
    const cosY = Math.cos(ship.liveYaw);

    const x1 = lx * cosR - localY * sinR;
    const y1 = lx * sinR + localY * cosR;

    const x2 = x1;
    const y2 = y1 * cosP - lz * sinP;
    const z2 = y1 * sinP + lz * cosP;

    return out.set(
      ship.x + x2 * cosY + z2 * sinY,
      ship.liveCenterY + y2,
      ship.z - x2 * sinY + z2 * cosY
    );
  }

  /**
   * Exact world Y of `(lx, localY, lz)` inside `shipGroup` (note: `Ry(yaw)` preserves `Y`!).
   */
  public shipLocalToWorldY(
    ship: Pick<CargoMegashipInstance, 'liveCenterY' | 'livePitch' | 'liveRoll'>,
    lx: number,
    localY: number,
    lz: number
  ): number {
    const sinR = Math.sin(ship.liveRoll);
    const cosR = Math.cos(ship.liveRoll);
    const sinP = Math.sin(ship.livePitch);
    const cosP = Math.cos(ship.livePitch);
    const y1 = lx * sinR + localY * cosR;
    return ship.liveCenterY + y1 * cosP - lz * sinP;
  }

  /**
   * Exact closed-form inverse mapping world `(wx, wz)` onto local `(lx, lz)` on the plane `localY`
   * of a yawed, pitched, and rolled `CargoMegashipInstance`!
   */
  public worldToShipLocalXZ(
    ship: Pick<CargoMegashipInstance, 'x' | 'z' | 'livePitch' | 'liveRoll' | 'liveYaw'>,
    wx: number,
    wz: number,
    localY = 0.0
  ): { lx: number; lz: number } {
    const dx = wx - ship.x;
    const dz = wz - ship.z;
    const sinY = Math.sin(ship.liveYaw);
    const cosY = Math.cos(ship.liveYaw);
    const x2 = dx * cosY - dz * sinY;
    const z2 = dx * sinY + dz * cosY;

    const sinR = Math.sin(ship.liveRoll);
    const cosR = Math.max(0.25, Math.cos(ship.liveRoll));
    const sinP = Math.sin(ship.livePitch);
    const cosP = Math.max(0.25, Math.cos(ship.livePitch));

    const lx = (x2 + localY * sinR) / cosR;
    const y1 = lx * sinR + localY * cosR;
    const lz = (z2 - y1 * sinP) / cosP;
    return { lx, lz };
  }

  // ===========================================================================
  // DETERMINISTIC MULTI-SHIP FLEET GRAPH (`1 -> 2 -> 3 -> 2 -> 1`)
  // ===========================================================================

  public getRowSpecs(
    rowIdx: number,
    sampleCourseX: (z: number) => number
  ): Array<{
    x: number;
    z: number;
    baseY: number;
    halfBeam: number;
    halfLength: number;
    heaveAmp: number;
    heaveFreq: number;
    pitchAmp: number;
    pitchFreq: number;
    rollAmp: number;
    rollFreq: number;
    yawAmp: number;
    yawFreq: number;
    phase: number;
    rowIdx: number;
    shipIdx: number;
  }> {
    const safeRow = Math.max(0, rowIdx);
    const zCenter = FIRST_SHIP_CENTER_Z - safeRow * SHIP_ROW_SPACING_Z;
    const centerX = sampleCourseX(zCenter);
    const count = ROW_SHIP_COUNTS[safeRow % ROW_SHIP_COUNTS.length];
    const rng = mulberry32(safeRow * 61331 + 19427);

    if (count === 1) {
      const isSpawnFlagship = safeRow === 0;
      return [
        {
          x: centerX,
          z: zCenter,
          baseY: isSpawnFlagship ? 2.2 : 2.9,
          halfBeam: 16.5, // Colossal 33m-wide Ultra-Large Container Vessel (ULCV)!
          halfLength: 42.0, // 84m hull length!
          // Dramatically unsteady storm swell: deep heave, steep pitch, heavy roll & wave-induced yaw rotation!
          heaveAmp: isSpawnFlagship ? 1.25 : 1.75,
          heaveFreq: 0.95,
          pitchAmp: isSpawnFlagship ? 0.058 : 0.082,
          pitchFreq: 0.84,
          rollAmp: isSpawnFlagship ? 0.125 : 0.168,
          rollFreq: 0.78,
          yawAmp: isSpawnFlagship ? 0.078 : 0.115,
          yawFreq: 0.56,
          phase: safeRow * 1.7,
          rowIdx: safeRow,
          shipIdx: 0,
        },
      ];
    }

    const totalSpanX = count === 3 ? 88.0 : 50.0;
    const halfBeam = count === 3 ? 13.5 : 15.0;
    const halfLength = 40.0;
    const baseHeights = count === 3 ? [2.0, 4.0, 1.6] : [1.8, 3.6];

    const out = [];
    for (let i = 0; i < count; i++) {
      const frac = i / (count - 1) - 0.5;
      const xOff = frac * totalSpanX;
      out.push({
        x: centerX + xOff,
        z: zCenter,
        baseY: baseHeights[i] + (safeRow >= 2 ? (rng() - 0.5) * 0.8 : 0.0),
        halfBeam,
        halfLength,
        heaveAmp: lerp(1.35, 2.05, rng()),
        heaveFreq: lerp(0.88, 1.18, rng()),
        pitchAmp: lerp(0.068, 0.102, rng()),
        pitchFreq: lerp(0.76, 1.02, rng()),
        rollAmp: lerp(0.145, 0.205, rng()),
        rollFreq: lerp(0.72, 0.96, rng()),
        yawAmp: lerp(0.092, 0.148, rng()),
        yawFreq: lerp(0.52, 0.74, rng()),
        phase: safeRow * 1.9 + i * 2.15,
        rowIdx: safeRow,
        shipIdx: i,
      });
    }
    return out;
  }

  public sampleGantryLockState(
    period: number,
    phase: number,
    slideDirSign: number,
    elapsed: number
  ): { latOffset: number; latVel: number; liftY: number; isLocked: boolean } {
    const cyc = (((elapsed + phase) % period) + period) % period;
    const u = cyc / period; // 0..1
    const lockWindow = 0.64; // 64% LOCKED (~5.1s), 36% CRANE SLIDING (~2.9s)!

    if (u < lockWindow) {
      return {
        latOffset: 0.0,
        latVel: 0.0,
        liftY: 0.0,
        isLocked: true,
      };
    }

    const s = (u - lockWindow) / (1.0 - lockWindow);
    const maxSlide = 14.5 * slideDirSign;
    const sinWave = Math.sin(Math.PI * s);
    const dSinDs = Math.PI * Math.cos(Math.PI * s);

    return {
      latOffset: maxSlide * sinWave,
      latVel: (maxSlide * dSinDs) / (period * (1.0 - lockWindow)),
      liftY: 1.75 * sinWave,
      isLocked: false,
    };
  }

  public getRowLinks(
    _rowIdx: number,
    specsA: ReturnType<CargoMegashipBuilder['getRowSpecs']>,
    specsB: ReturnType<CargoMegashipBuilder['getRowSpecs']>
  ): ShipGraphLink[] {
    const M = specsA.length;
    const N = specsB.length;

    const pairSet = new Set<string>();
    const rawPairs: Array<{ fromIdx: number; toIdx: number }> = [];
    const addPair = (u: number, v: number, isBonus = false) => {
      if (u < 0 || u >= M || v < 0 || v >= N) return;
      if (isBonus && Math.abs(specsB[v].x - specsA[u].x) > 58.0) return;
      const key = `${u}_${v}`;
      if (!pairSet.has(key)) {
        pairSet.add(key);
        rawPairs.push({ fromIdx: u, toIdx: v });
      }
    };

    const maxMN = Math.max(M, N);
    for (let k = 0; k < maxMN; k++) {
      const u = M === 1 ? 0 : Math.round((k * (M - 1)) / (maxMN - 1));
      const v = N === 1 ? 0 : Math.round((k * (N - 1)) / (maxMN - 1));
      addPair(u, v, false);
    }

    if (M === 2 && N === 3) {
      addPair(0, 1, true);
      addPair(1, 1, true);
    } else if (M === 3 && N === 2) {
      addPair(1, 0, true);
      addPair(1, 1, true);
    }

    return rawPairs.map((p, idx) => {
      let kind: ShipLinkKind = 'RORO_BRIDGE';
      if (M === 1 && N === 2) {
        kind = p.toIdx === 0 ? 'GANTRY_CRANE_BRIDGE' : 'RORO_BRIDGE';
      } else if (idx === 0) {
        kind = 'GANTRY_CRANE_BRIDGE';
      } else if (p.toIdx === N - 1 && specsB[p.toIdx].baseY <= specsA[p.fromIdx].baseY + 0.5) {
        kind = 'BOW_JUMP_RAMP';
      } else if (idx % 2 === 1) {
        kind = 'GANTRY_CRANE_BRIDGE';
      }
      return {
        fromIdx: p.fromIdx,
        toIdx: p.toIdx,
        kind,
      };
    });
  }

  public findNearestShipSpawn(
    x: number,
    z: number,
    sampleCourseX: (z: number) => number
  ): { x: number; y: number; z: number; yaw: number } {
    const approxRow = Math.max(
      0,
      Math.round((FIRST_SHIP_CENTER_Z - z) / SHIP_ROW_SPACING_Z)
    );
    let best: ReturnType<CargoMegashipBuilder['getRowSpecs']>[number] | null = null;
    let bestDistSq = Infinity;

    for (let r = Math.max(0, approxRow - 1); r <= approxRow + 1; r++) {
      for (const sp of this.getRowSpecs(r, sampleCourseX)) {
        const dSq = (x - sp.x) * (x - sp.x) + (z - sp.z) * (z - sp.z);
        if (dSq < bestDistSq) {
          bestDistSq = dSq;
          best = sp;
        }
      }
    }

    if (!best) {
      const s0 = this.getRowSpecs(0, sampleCourseX)[0];
      return { x: s0.x, y: s0.baseY + 0.6, z: s0.z + s0.halfLength - 9.5, yaw: 0 };
    }

    return {
      x: best.x,
      y: best.baseY + 0.6,
      z: best.z + best.halfLength - 9.5,
      yaw: 0,
    };
  }

  // ===========================================================================
  // CHUNK POPULATION — SHIPS, SLIDING CARGO, MAGNETIC & BREAKABLE CRANES
  // ===========================================================================

  public populateCargoMegashipChunk(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    zLength: number,
    sampleCourseX: (z: number) => number,
    _obstacles: WorldObstacle[]
  ): CargoMegashipChunkData {
    const group = new THREE.Group();
    group.name = `CargoMegashipChunk_${chunkIndex}`;
    chunkGroup.add(group);

    const ships: CargoMegashipInstance[] = [];
    const ownedShips: CargoMegashipInstance[] = [];
    const gantryBridges: GantryContainerBridge[] = [];
    const shipSpans: ArticulatedShipSpan[] = [];

    const specsCur = this.getRowSpecs(chunkIndex, sampleCourseX);
    const specsNext = this.getRowSpecs(chunkIndex + 1, sampleCourseX);

    const spawnShipRow = (
      specList: ReturnType<CargoMegashipBuilder['getRowSpecs']>,
      visualOwned: boolean
    ): CargoMegashipInstance[] => {
      const out: CargoMegashipInstance[] = [];
      for (const sp of specList) {
        const shipGroup = new THREE.Group();
        shipGroup.position.set(sp.x, sp.baseY, sp.z);

        const containerBoxes: LocalContainerBox[] = [];
        const rooftopRamps: LocalRampSpan[] = [];
        const slidingCargos: SlidingDeckCargo[] = [];
        let radarSpinner: THREE.Object3D | null = null;
        let surfFoamMesh: THREE.Mesh | null = null;

        this.buildMegashipDeckCargoAndSuperstructure(
          shipGroup,
          sp,
          visualOwned,
          containerBoxes,
          rooftopRamps,
          slidingCargos,
          (r) => {
            radarSpinner = r;
          },
          (f) => {
            surfFoamMesh = f;
          }
        );

        if (visualOwned) {
          group.add(shipGroup);
        }

        const inst: CargoMegashipInstance = {
          x: sp.x,
          z: sp.z,
          baseY: sp.baseY,
          halfBeam: sp.halfBeam,
          halfLength: sp.halfLength,
          heaveAmp: sp.heaveAmp,
          heaveFreq: sp.heaveFreq,
          pitchAmp: sp.pitchAmp,
          pitchFreq: sp.pitchFreq,
          rollAmp: sp.rollAmp,
          rollFreq: sp.rollFreq,
          yawAmp: sp.yawAmp,
          yawFreq: sp.yawFreq,
          phase: sp.phase,
          liveCenterY: sp.baseY,
          dCenterYDt: 0,
          livePitch: 0,
          dPitchDt: 0,
          liveRoll: 0,
          dRollDt: 0,
          liveYaw: 0,
          dYawDt: 0,
          rowIdx: sp.rowIdx,
          shipIdx: sp.shipIdx,
          shipGroup,
          radarSpinner,
          containerBoxes,
          rooftopRamps,
          slidingCargos,
          magneticCranes: [],
          breakableCranes: [],
          surfWashZ: -sp.halfLength * 0.52,
          surfWashPhase: sp.phase * 1.4,
          surfFoamMesh,
        };

        // Attach Shipboard Rotating Electromagnet Crane + Malfunctioning Breakable Cargo Crane on owned ships!
        if (visualOwned) {
          const craneSide = (sp.rowIdx + sp.shipIdx) % 2 === 0 ? 1 : -1;
          inst.magneticCranes.push(
            this.buildMagneticVehicleCrane(
              inst,
              craneSide * (sp.halfBeam - 1.85),
              -15.5,
              craneSide
            )
          );
          inst.breakableCranes.push(
            this.buildBreakableCargoCrane(
              inst,
              -craneSide * (sp.halfBeam - 1.85),
              17.5,
              -craneSide,
              sp.rowIdx * 2.3 + sp.shipIdx * 3.1
            )
          );
          // Flagship & Wide Megaships get a 2nd Breaking Cargo Crane over the forecastle bow bay too!
          if (sp.halfBeam >= 15.0) {
            inst.breakableCranes.push(
              this.buildBreakableCargoCrane(
                inst,
                -craneSide * (sp.halfBeam - 1.85),
                -26.5,
                -craneSide,
                sp.rowIdx * 2.3 + sp.shipIdx * 3.1 + 4.4
              )
            );
          }
        }

        ships.push(inst);
        if (visualOwned) ownedShips.push(inst);
        out.push(inst);
      }
      return out;
    };

    const rowCur = chunkIndex >= 0 ? spawnShipRow(specsCur, true) : [];
    const rowNext = chunkIndex + 1 >= 0 ? spawnShipRow(specsNext, false) : [];

    // Connect Bow of `rowCur` ships across the 32m Storm Ocean Channel to Stern of `rowNext` ships!
    const links = chunkIndex >= 0 ? this.getRowLinks(chunkIndex, specsCur, specsNext) : [];
    const outBy = new Map<number, ShipGraphLink[]>();
    const inBy = new Map<number, ShipGraphLink[]>();
    for (const l of links) {
      if (!outBy.has(l.fromIdx)) outBy.set(l.fromIdx, []);
      outBy.get(l.fromIdx)!.push(l);
      if (!inBy.has(l.toIdx)) inBy.set(l.toIdx, []);
      inBy.get(l.toIdx)!.push(l);
    }
    for (const arr of outBy.values()) {
      arr.sort((a, b) => rowNext[a.toIdx].x - rowNext[b.toIdx].x);
    }
    for (const arr of inBy.values()) {
      arr.sort((a, b) => rowCur[a.fromIdx].x - rowCur[b.fromIdx].x);
    }

    links.forEach((link, idx) => {
      const src = rowCur[link.fromIdx];
      const dst = rowNext[link.toIdx];
      const outArr = outBy.get(link.fromIdx)!;
      const inArr = inBy.get(link.toIdx)!;

      const computeEndLocalX = (
        halfBeam: number,
        slot: number,
        total: number,
        otherX: number,
        selfX: number
      ): number => {
        if (total <= 1) {
          return clamp((otherX - selfX) * 0.16, -halfBeam * 0.42, halfBeam * 0.42);
        }
        const frac = (slot / (total - 1)) * 2.0 - 1.0;
        return frac * halfBeam * 0.48;
      };

      const srcLocalX = computeEndLocalX(
        src.halfBeam,
        outArr.indexOf(link),
        outArr.length,
        dst.x,
        src.x
      );
      const srcLocalZ = -src.halfLength + 2.4;
      const dstLocalX = computeEndLocalX(
        dst.halfBeam,
        inArr.indexOf(link),
        inArr.length,
        src.x,
        dst.x
      );
      const dstLocalZ = dst.halfLength - 2.4;

      if (link.kind === 'GANTRY_CRANE_BRIDGE') {
        gantryBridges.push(
          this.buildGantryCraneContainerBridge(
            group,
            src,
            dst,
            srcLocalX,
            srcLocalZ,
            dstLocalX,
            dstLocalZ,
            chunkIndex * 2.1 + idx * 1.4,
            idx % 2 === 0 ? -1 : 1
          )
        );
      } else {
        shipSpans.push(
          this.buildArticulatedShipSpan(
            group,
            link.kind,
            src,
            dst,
            srcLocalX,
            srcLocalZ,
            dstLocalX,
            dstLocalZ
          )
        );
      }
    });

    if (rowCur.length > 0) {
      const deckFlood = new THREE.PointLight(0xffcb85, 24.0, 74.0, 1.3);
      deckFlood.position.set(rowCur[0].x, rowCur[0].baseY + 15.5, rowCur[0].z + 8.0);
      group.add(deckFlood);
    }

    return {
      group,
      ships,
      ownedShips,
      gantryBridges,
      shipSpans,
      zStart,
      zLength,
    };
  }

  // ===========================================================================
  // SHIP DECK CARGO, OPEN-DOOR TUNNEL, ROOFTOP HIGHWAY & LOOSE SLIDING BOXES
  // ===========================================================================

  private buildMegashipDeckCargoAndSuperstructure(
    shipGroup: THREE.Group,
    sp: {
      halfBeam: number;
      halfLength: number;
      rowIdx: number;
      shipIdx: number;
    },
    visualOwned: boolean,
    containerBoxes: LocalContainerBox[],
    rooftopRamps: LocalRampSpan[],
    slidingCargos: SlidingDeckCargo[],
    onRadar: (r: THREE.Object3D) => void,
    onSurfFoam: (m: THREE.Mesh) => void
  ): void {
    const rng = mulberry32(sp.rowIdx * 91813 + sp.shipIdx * 17321 + 551);
    const hb = sp.halfBeam;
    const hl = sp.halfLength;

    if (visualOwned) {
      const hullMesh = new THREE.Mesh(this.shipHullGeo, this.hullSteelMat);
      hullMesh.scale.set(hb, 1.0, hl);
      hullMesh.castShadow = true;
      hullMesh.receiveShadow = true;
      shipGroup.add(hullMesh);

      const portBeacon = new THREE.Mesh(this.beaconSphereGeo, this.navPortRedMat);
      portBeacon.position.set(-hb * 0.78, 1.4, -hl * 0.88);
      portBeacon.scale.setScalar(1.35);
      shipGroup.add(portBeacon);

      const stbdBeacon = new THREE.Mesh(this.beaconSphereGeo, this.navStarboardGreenMat);
      stbdBeacon.position.set(hb * 0.78, 1.4, -hl * 0.88);
      stbdBeacon.scale.setScalar(1.35);
      shipGroup.add(stbdBeacon);

      // Drive-Under White Multi-Story Navigation Bridge Superstructure
      const bridgeZ = hl * 0.42;
      for (const side of [-1, 1]) {
        const leg = new THREE.Mesh(this.unitBoxGeo, this.bridgeWhiteMat);
        leg.position.set(side * (hb - 1.5), 4.2, bridgeZ);
        leg.scale.set(2.4, 8.4, 6.2);
        leg.castShadow = true;
        shipGroup.add(leg);
      }

      const wheelhouse = new THREE.Mesh(this.unitBoxGeo, this.bridgeWhiteMat);
      wheelhouse.position.set(0, 9.8, bridgeZ);
      wheelhouse.scale.set(hb * 2.08, 4.6, 6.8);
      wheelhouse.castShadow = true;
      shipGroup.add(wheelhouse);

      const winStrip = new THREE.Mesh(this.unitBoxGeo, this.bridgeWindowGlowMat);
      winStrip.position.set(0, 10.9, bridgeZ - 3.45);
      winStrip.scale.set(hb * 1.85, 0.95, 0.15);
      shipGroup.add(winStrip);

      const funnel = new THREE.Mesh(this.unitBoxGeo, this.gantryCraneMat);
      funnel.position.set(hb * 0.45, 13.8, bridgeZ + 1.2);
      funnel.scale.set(2.4, 3.8, 3.2);
      shipGroup.add(funnel);

      const radarMast = new THREE.Group();
      radarMast.position.set(0, 13.2, bridgeZ - 1.2);
      const radarBar = new THREE.Mesh(this.unitBoxGeo, this.bridgeWhiteMat);
      radarBar.scale.set(4.8, 0.35, 0.55);
      radarMast.add(radarBar);
      shipGroup.add(radarMast);
      onRadar(radarMast);

      const surfMesh = new THREE.Mesh(this.surfPlaneGeo, this.deckSurfFoamMat);
      surfMesh.position.set(0, 0.12, -hl * 0.52);
      surfMesh.scale.set(hb * 1.65, 1.0, 13.5);
      shipGroup.add(surfMesh);
      onSurfFoam(surfMesh);
    }

    const containerMats: Array<{ mat: THREE.Matrix4; colorHex: number }> = [];
    const darkSteelMats: THREE.Matrix4[] = [];

    const addClosedContainerStack = (
      lx: number,
      lz: number,
      width: number,
      length: number,
      tiers: number,
      drivableRoof: boolean
    ) => {
      const tierH = 3.0;
      const totalH = tiers * tierH;
      containerBoxes.push({
        lx,
        lz,
        halfW: width * 0.5,
        halfL: length * 0.5,
        bottomY: 0.0,
        topY: totalH,
        drivableRoof,
      });

      if (visualOwned) {
        for (let t = 0; t < tiers; t++) {
          this._dummy.position.set(lx, (t + 0.5) * tierH, lz);
          this._dummy.rotation.set(0, 0, 0);
          this._dummy.scale.set(width, tierH - 0.06, length);
          this._dummy.updateMatrix();
          const colHex =
            CONTAINER_PALETTE[
              Math.floor(rng() * CONTAINER_PALETTE.length) % CONTAINER_PALETTE.length
            ];
          containerMats.push({ mat: this._dummy.matrix.clone(), colorHex: colHex });
        }
      }
    };

    // LANE A: OPEN-DOOR 40FT SHIPPING CONTAINER TUNNEL!
    const flipSides = (sp.rowIdx + sp.shipIdx) % 2 === 1;
    const sideMult = flipSides ? -1 : 1;

    const tunnelCenterX = -4.6 * sideMult;
    const tunnelCenterZ = 2.0;
    const tunnelClearHalfW = 2.55;
    const tunnelWallThick = 0.34;
    const tunnelLen = 17.5;
    const tunnelHeight = 4.25;

    for (const wSide of [-1, 1]) {
      const wallX = tunnelCenterX + wSide * (tunnelClearHalfW + tunnelWallThick * 0.5);
      containerBoxes.push({
        lx: wallX,
        lz: tunnelCenterZ,
        halfW: tunnelWallThick * 0.5,
        halfL: tunnelLen * 0.5,
        bottomY: 0.0,
        topY: tunnelHeight,
        drivableRoof: false,
      });
    }

    if (visualOwned) {
      const tunnelColor = new THREE.Color(
        CONTAINER_PALETTE[(sp.rowIdx + sp.shipIdx) % CONTAINER_PALETTE.length]
      );
      const tunnelWallMat = this.containerMat.clone();
      tunnelWallMat.color.copy(tunnelColor);

      for (const wSide of [-1, 1]) {
        const wMesh = new THREE.Mesh(this.corrugatedBoxGeo, tunnelWallMat);
        wMesh.position.set(
          tunnelCenterX + wSide * (tunnelClearHalfW + tunnelWallThick * 0.5),
          tunnelHeight * 0.5,
          tunnelCenterZ
        );
        wMesh.scale.set(tunnelWallThick, tunnelHeight, tunnelLen);
        wMesh.castShadow = true;
        shipGroup.add(wMesh);

        for (const zEnd of [-1, 1]) {
          const door = new THREE.Mesh(this.corrugatedBoxGeo, tunnelWallMat);
          door.position.set(
            tunnelCenterX + wSide * (tunnelClearHalfW + 0.95),
            tunnelHeight * 0.5,
            tunnelCenterZ + zEnd * (tunnelLen * 0.5 + 0.9)
          );
          door.rotation.y = wSide * zEnd * 0.52;
          door.scale.set(0.18, tunnelHeight - 0.1, 2.2);
          shipGroup.add(door);
        }
      }

      const roofMesh = new THREE.Mesh(this.corrugatedBoxGeo, tunnelWallMat);
      roofMesh.position.set(tunnelCenterX, tunnelHeight + 0.16, tunnelCenterZ);
      roofMesh.scale.set((tunnelClearHalfW + tunnelWallThick) * 2.0, 0.32, tunnelLen);
      roofMesh.castShadow = true;
      shipGroup.add(roofMesh);

      const floorMesh = new THREE.Mesh(this.unitBoxGeo, this.containerWoodFloorMat);
      floorMesh.position.set(tunnelCenterX, 0.04, tunnelCenterZ);
      floorMesh.scale.set(tunnelClearHalfW * 2.0, 0.08, tunnelLen);
      floorMesh.receiveShadow = true;
      shipGroup.add(floorMesh);

      const stripLight = new THREE.Mesh(this.unitBoxGeo, this.bridgeWindowGlowMat);
      stripLight.position.set(tunnelCenterX, tunnelHeight - 0.12, tunnelCenterZ);
      stripLight.scale.set(0.45, 0.12, tunnelLen * 0.85);
      shipGroup.add(stripLight);
    }

    // LANE B: ELEVATED CONTAINER ROOFTOP HIGHWAY!
    const roofLaneX = 5.4 * sideMult;
    const roofW = 6.2;
    const roofY = 3.0;

    rooftopRamps.push({
      lx: roofLaneX,
      lzStart: 12.5,
      lzEnd: 24.5,
      halfW: roofW * 0.5,
      yStart: roofY,
      yEnd: 0.0,
    });

    addClosedContainerStack(roofLaneX, 5.75, roofW, 13.5, 1, true);
    addClosedContainerStack(roofLaneX, -7.75, roofW, 13.5, 1, true);

    rooftopRamps.push({
      lx: roofLaneX,
      lzStart: -23.5,
      lzEnd: -14.5,
      halfW: roofW * 0.5,
      yStart: 0.0,
      yEnd: roofY,
    });

    if (visualOwned) {
      const buildVisualRamp = (zA: number, yA: number, zB: number, yB: number) => {
        const dz = zB - zA;
        const len = Math.hypot(dz, yB - yA);
        const pitch = Math.atan2(yA - yB, Math.abs(dz));
        this._dummy.position.set(roofLaneX, (yA + yB) * 0.5 - 0.14, (zA + zB) * 0.5);
        this._dummy.rotation.set(zB > zA ? pitch : -pitch, 0, 0);
        this._dummy.scale.set(roofW, 0.28, len);
        this._dummy.updateMatrix();
        darkSteelMats.push(this._dummy.matrix.clone());
      };
      buildVisualRamp(12.5, roofY, 24.5, 0.0);
      buildVisualRamp(-23.5, 0.0, -14.5, roofY);
    }

    // PERIMETER MULTI-STORY SHIPPING CONTAINER WALLS (leaving midships crane bays open!)
    if (hb >= 14.2) {
      addClosedContainerStack(-hb + 2.2, 0.0, 3.2, 13.2, 3, false);
      addClosedContainerStack(hb - 2.2, 0.0, 3.2, 13.2, 2, false);
    }

    // Center-bow divider stack (`z = -25.5m`) splitting Port & Starboard bow bridge approaches cleanly!
    addClosedContainerStack(0.0, -25.5, 2.8, 7.5, 2, false);

    if (visualOwned && containerMats.length > 0) {
      const cIm = new THREE.InstancedMesh(
        this.corrugatedBoxGeo,
        this.containerMat,
        containerMats.length
      );
      for (let i = 0; i < containerMats.length; i++) {
        cIm.setMatrixAt(i, containerMats[i].mat);
        this._color.setHex(containerMats[i].colorHex);
        cIm.setColorAt(i, this._color);
      }
      cIm.instanceMatrix.needsUpdate = true;
      if (cIm.instanceColor) cIm.instanceColor.needsUpdate = true;
      cIm.castShadow = true;
      cIm.receiveShadow = true;
      shipGroup.add(cIm);
    }

    if (visualOwned && darkSteelMats.length > 0) {
      const rIm = new THREE.InstancedMesh(
        this.unitBoxGeo,
        this.hazardStripeMat,
        darkSteelMats.length
      );
      for (let i = 0; i < darkSteelMats.length; i++) {
        rIm.setMatrixAt(i, darkSteelMats[i]);
      }
      rIm.instanceMatrix.needsUpdate = true;
      rIm.castShadow = true;
      rIm.receiveShadow = true;
      shipGroup.add(rIm);
    }

    // =========================================================================
    // UNSECURED LOOSE DECK CARGO CONTAINERS & HEAVY CRATES SLIDING WITH WAVES!
    // =========================================================================
    if (visualOwned) {
      const looseSpecs = [
        // 1. Aft-Midships Unsecured 20ft Container sliding port-to-starboard across the aft deck bay!
        {
          lx: -7.5 * sideMult,
          lz: 18.8,
          w: 2.35,
          l: 4.8,
          h: 2.45,
          bayZMin: 14.2,
          bayZMax: 24.5,
          colHex: 0xc98b24,
        },
        // 2. Forecastle Surf-Bay Unsecured 20ft Container sliding violently across the bow waist!
        {
          lx: 7.2 * sideMult,
          lz: -17.5,
          w: 2.35,
          l: 5.1,
          h: 2.5,
          bayZMin: -21.2,
          bayZMax: -12.8,
          colHex: 0xb83527,
        },
        // 3. Heavy Industrial Machinery Crate sliding along the aft-starboard / aft-port apron!
        {
          lx: -9.5 * sideMult,
          lz: -11.8,
          w: 2.45,
          l: 3.1,
          h: 2.15,
          bayZMin: -15.5,
          bayZMax: -8.2,
          colHex: 0x1c5b8e,
        },
      ];

      for (const ls of looseSpecs) {
        const cargoMeshGroup = new THREE.Group();
        cargoMeshGroup.position.set(ls.lx, 0.02, ls.lz);

        const bodyMat = this.containerMat.clone();
        bodyMat.color.setHex(ls.colHex);
        const boxMesh = new THREE.Mesh(this.corrugatedBoxGeo, bodyMat);
        boxMesh.position.set(0, ls.h * 0.5, 0);
        boxMesh.scale.set(ls.w, ls.h, ls.l);
        boxMesh.castShadow = true;
        boxMesh.receiveShadow = true;
        cargoMeshGroup.add(boxMesh);

        // Broken yellow-black hazard lashing band + snapped tie-down chain stubs so players spot loose cargo instantly!
        const band = new THREE.Mesh(this.unitBoxGeo, this.hazardStripeMat);
        band.position.set(0, ls.h * 0.52, 0);
        band.scale.set(ls.w + 0.08, 0.34, ls.l + 0.08);
        cargoMeshGroup.add(band);

        for (const cSide of [-1, 1]) {
          const snappedChain = new THREE.Mesh(this.cableCylGeo, this.gantryDarkSteelMat);
          snappedChain.position.set(cSide * (ls.w * 0.5 + 0.18), 0.55, ls.l * 0.38);
          snappedChain.rotation.z = cSide * 0.48;
          snappedChain.scale.set(1.2, 1.15, 1.2);
          cargoMeshGroup.add(snappedChain);
        }

        shipGroup.add(cargoMeshGroup);
        slidingCargos.push({
          lx: ls.lx,
          lz: ls.lz,
          vx: (rng() - 0.5) * 3.0,
          vz: 0,
          localYaw: (rng() - 0.5) * 0.14,
          yawVel: 0,
          halfW: ls.w * 0.5,
          halfL: ls.l * 0.5,
          height: ls.h,
          bayZMin: ls.bayZMin,
          bayZMax: ls.bayZMax,
          worldVx: 0,
          worldVz: 0,
          slamCooldown: 0,
          meshGroup: cargoMeshGroup,
        });
      }
    }
  }

  // ===========================================================================
  // ROTATING ELECTROMAGNET VEHICLE-PICKUP CRANE (`MagneticVehicleCrane`)
  // ===========================================================================

  private buildMagneticVehicleCrane(
    parentShip: CargoMegashipInstance,
    pedestalLx: number,
    pedestalLz: number,
    sideSign: number
  ): MagneticVehicleCrane {
    const craneRoot = new THREE.Group();
    craneRoot.position.set(pedestalLx, 0, pedestalLz);
    parentShip.shipGroup.add(craneRoot);

    // 1. Grounded Heavy Steel Gunwale Pedestal Column + Hazard Collar
    const pedH = 4.2;
    const pedMesh = new THREE.Mesh(this.pedestalCylGeo, this.gantryDarkSteelMat);
    pedMesh.position.set(0, pedH * 0.5, 0);
    pedMesh.scale.set(1, pedH, 1);
    pedMesh.castShadow = true;
    craneRoot.add(pedMesh);

    const collar = new THREE.Mesh(this.unitBoxGeo, this.hazardStripeMat);
    collar.position.set(0, 0.55, 0);
    collar.scale.set(3.2, 1.1, 3.2);
    craneRoot.add(collar);

    // 2. 360-Degree Slewing Turret, Operator Cab & Angled Luffing Jib Boom!
    const boomLength = 11.4;
    const boomTopY = 13.2;
    const turretGroup = new THREE.Group();
    turretGroup.position.set(0, pedH, 0);
    craneRoot.add(turretGroup);

    const cab = new THREE.Mesh(this.unitBoxGeo, this.yellowCraneMat);
    cab.position.set(0, 1.35, -0.4);
    cab.scale.set(2.9, 2.7, 3.4);
    cab.castShadow = true;
    turretGroup.add(cab);

    const cabWin = new THREE.Mesh(this.unitBoxGeo, this.magnetCyanGlowMat);
    cabWin.position.set(0, 1.65, 1.32);
    cabWin.scale.set(2.1, 0.95, 0.12);
    turretGroup.add(cabWin);

    const counterWeight = new THREE.Mesh(this.unitBoxGeo, this.gantryDarkSteelMat);
    counterWeight.position.set(0, 1.1, -2.6);
    counterWeight.scale.set(2.8, 1.9, 1.8);
    turretGroup.add(counterWeight);

    // Jib Boom extending along `+Z` of `turretGroup` out to `(0, boomTopY - pedH, boomLength)`!
    const riseY = boomTopY - pedH;
    const jibDiag = Math.hypot(boomLength, riseY);
    const jibPitch = -Math.atan2(riseY, boomLength);
    const jibMesh = new THREE.Mesh(this.unitBoxGeo, this.yellowCraneMat);
    jibMesh.position.set(0, riseY * 0.5 + 1.2, boomLength * 0.5);
    jibMesh.rotation.x = jibPitch;
    jibMesh.scale.set(1.15, 1.05, jibDiag);
    jibMesh.castShadow = true;
    turretGroup.add(jibMesh);

    const beaconMesh = new THREE.Mesh(this.beaconSphereGeo, this.magnetCyanGlowMat);
    beaconMesh.position.set(0, riseY + 1.95, boomLength);
    beaconMesh.scale.setScalar(1.9);
    turretGroup.add(beaconMesh);

    // 3. Telescoping Hoist Cable & Glowing Circular Industrial Electromagnet Disc!
    const cableMesh = new THREE.Mesh(this.cableCylGeo, this.gantryDarkSteelMat);
    cableMesh.position.set(0, riseY * 0.5, boomLength);
    turretGroup.add(cableMesh);

    const magnetHeadGroup = new THREE.Group();
    magnetHeadGroup.position.set(0, 0, boomLength);
    turretGroup.add(magnetHeadGroup);

    const discMesh = new THREE.Mesh(this.magnetDiscGeo, this.gantryDarkSteelMat);
    discMesh.castShadow = true;
    magnetHeadGroup.add(discMesh);

    const coilGlowMesh = new THREE.Mesh(this.magnetCoilTorusGeo, this.magnetCyanGlowMat);
    coilGlowMesh.position.set(0, -0.28, 0);
    magnetHeadGroup.add(coilGlowMesh);

    // 4. Projected Pulsing Cyan Magnetic Capture Ring on the Ship Deck!
    const dangerRingMesh = new THREE.Mesh(this.deckZoneRingGeo, this.magnetFieldRingMat);
    dangerRingMesh.position.set(pedestalLx, 0.11, pedestalLz);
    parentShip.shipGroup.add(dangerRingMesh);

    // Point inward across the ship deck (`+Z` of `turretGroup` rotated by `baseBoomYaw` points toward centerline!)
    const baseBoomYaw = sideSign > 0 ? -Math.PI * 0.5 : Math.PI * 0.5;

    return {
      parentShip,
      pedestalLx,
      pedestalLz,
      boomLength,
      boomTopY: boomTopY + 1.2,
      baseBoomYaw,
      sweepAmp: 0.72,
      sweepFreq: 0.78,
      phase: parentShip.phase + 0.9,
      currentBoomYaw: baseBoomYaw,
      localTipX: pedestalLx - sideSign * boomLength,
      localTipZ: pedestalLz,
      localMagnetY: 4.15,
      magnetWorldPos: new THREE.Vector3(),
      prevMagnetWorldPos: new THREE.Vector3(),
      magnetWorldVel: new THREE.Vector3(),
      hasPrevWorldPos: false,
      state: 'SWEEPING',
      stateTimer: 0,
      boostBreakAccum: 0,
      pendulumSwayX: 0,
      pendulumSwayZ: 0,
      turretGroup,
      cableMesh,
      magnetHeadGroup,
      coilGlowMesh,
      dangerRingMesh,
      beaconMesh,
    };
  }

  // ===========================================================================
  // MALFUNCTIONING / BREAKING CARGO CRANE (`BreakableCargoCrane`)
  // ===========================================================================

  private buildBreakableCargoCrane(
    parentShip: CargoMegashipInstance,
    pedestalLx: number,
    pedestalLz: number,
    sideSign: number,
    phase: number
  ): BreakableCargoCrane {
    const craneRoot = new THREE.Group();
    craneRoot.position.set(pedestalLx, 0, pedestalLz);
    parentShip.shipGroup.add(craneRoot);

    const towerH = 19.5;
    const boomLength = 12.2;

    // 1. Towering Steel Port Hammerhead Crane Mast bolted to the Ship's Gunwale!
    const mast = new THREE.Mesh(this.unitBoxGeo, this.gantryCraneMat);
    mast.position.set(0, towerH * 0.5, 0);
    mast.scale.set(2.2, towerH, 2.2);
    mast.castShadow = true;
    craneRoot.add(mast);

    const baseCollar = new THREE.Mesh(this.unitBoxGeo, this.hazardStripeMat);
    baseCollar.position.set(0, 0.7, 0);
    baseCollar.scale.set(3.4, 1.4, 3.4);
    craneRoot.add(baseCollar);

    // 2. Slewing Hammerhead Jib Boom (`turretGroup` at top of mast)!
    const turretGroup = new THREE.Group();
    turretGroup.position.set(0, towerH, 0);
    craneRoot.add(turretGroup);

    const jib = new THREE.Mesh(this.unitBoxGeo, this.gantryCraneMat);
    jib.position.set(0, 0.6, (boomLength - 3.5) * 0.5);
    jib.scale.set(1.55, 1.45, boomLength + 5.5);
    jib.castShadow = true;
    turretGroup.add(jib);

    const counterBlock = new THREE.Mesh(this.unitBoxGeo, this.gantryDarkSteelMat);
    counterBlock.position.set(0, -0.2, -3.6);
    counterBlock.scale.set(2.6, 2.1, 2.8);
    turretGroup.add(counterBlock);

    const beaconMesh = new THREE.Mesh(this.beaconSphereGeo, this.slideAmberBeaconMat);
    beaconMesh.position.set(0, 1.85, boomLength);
    beaconMesh.scale.setScalar(2.1);
    turretGroup.add(beaconMesh);

    // Intact Winch Cables + Whipped Snapped Cable Mesh!
    const intactCablesGroup = new THREE.Group();
    intactCablesGroup.position.set(0, 0, boomLength);
    for (const cx of [-0.9, 0.9]) {
      const c = new THREE.Mesh(this.cableCylGeo, this.gantryDarkSteelMat);
      c.position.set(cx, -0.5, 0);
      intactCablesGroup.add(c);
    }
    turretGroup.add(intactCablesGroup);

    const snappedCableMesh = new THREE.Mesh(this.cableCylGeo, this.hazardStripeMat);
    snappedCableMesh.position.set(0.95, -3.2, boomLength + 0.4);
    snappedCableMesh.rotation.z = 0.65;
    snappedCableMesh.scale.set(1.3, 6.4, 1.3);
    snappedCableMesh.visible = false;
    turretGroup.add(snappedCableMesh);

    // 3. Suspended / Freefalling / Deck-Sliding 20ft Cargo Container (`cargoGroup` in `shipGroup` space)!
    const halfW = 1.25;
    const halfL = 2.75;
    const height = 2.55;
    const cargoGroup = new THREE.Group();
    parentShip.shipGroup.add(cargoGroup);

    const cMat = this.containerMat.clone();
    cMat.color.setHex(0xd43b28); // High-visibility Crimson-Orange Breakaway Cargo Container!
    const boxMesh = new THREE.Mesh(this.corrugatedBoxGeo, cMat);
    boxMesh.position.set(0, height * 0.5, 0);
    boxMesh.scale.set(halfW * 2.0, height, halfL * 2.0);
    boxMesh.castShadow = true;
    boxMesh.receiveShadow = true;
    cargoGroup.add(boxMesh);

    const spreader = new THREE.Mesh(this.unitBoxGeo, this.hazardStripeMat);
    spreader.position.set(0, height + 0.22, 0);
    spreader.scale.set(halfW * 2.08, 0.36, halfL * 1.9);
    cargoGroup.add(spreader);

    // 4. Deck Drop-Zone Warning Reticle!
    const impactRingMesh = new THREE.Mesh(this.deckZoneRingGeo, this.dropDangerRingMat);
    impactRingMesh.position.set(pedestalLx, 0.12, pedestalLz);
    parentShip.shipGroup.add(impactRingMesh);

    const baseBoomYaw = sideSign > 0 ? -Math.PI * 0.5 : Math.PI * 0.5;
    const initLx = pedestalLx + Math.sin(baseBoomYaw) * boomLength;
    const initLz = pedestalLz + Math.cos(baseBoomYaw) * boomLength;

    return {
      parentShip,
      pedestalLx,
      pedestalLz,
      boomLength,
      boomTopY: towerH,
      baseBoomYaw,
      sweepAmp: 0.34,
      period: 9.2,
      phase,
      state: 'HOIST_SWAY',
      prevState: 'HOIST_SWAY',
      cargoLx: initLx,
      cargoLy: 10.8,
      cargoLz: initLz,
      cargoFallVy: 0,
      cargoSlideVx: 0,
      cargoSlideVz: 0,
      cargoRollTilt: 0,
      cargoLocalYaw: 0,
      halfW,
      halfL,
      height,
      worldVx: 0,
      worldVz: 0,
      turretGroup,
      intactCablesGroup,
      snappedCableMesh,
      cargoGroup,
      impactRingMesh,
      beaconMesh,
    };
  }

  // ===========================================================================
  // OVERHEAD PORT GANTRY CRANE SLIDING & LOCKING CONTAINER BRIDGE
  // ===========================================================================

  private buildGantryCraneContainerBridge(
    chunkGroup: THREE.Group,
    srcShip: CargoMegashipInstance,
    dstShip: CargoMegashipInstance,
    srcLocalX: number,
    srcLocalZ: number,
    dstLocalX: number,
    dstLocalZ: number,
    phase: number,
    slideDirSign: number
  ): GantryContainerBridge {
    const ax = srcShip.x + srcLocalX;
    const az = srcShip.z + srcLocalZ;
    const bx = dstShip.x + dstLocalX;
    const bz = dstShip.z + dstLocalZ;
    const dx = bx - ax;
    const dz = bz - az;
    const baseSpanLen = Math.hypot(dx, dz);
    const dirX = dx / baseSpanLen;
    const dirZ = dz / baseSpanLen;
    const yaw = Math.atan2(-dirX, -dirZ);
    const rightX = Math.cos(yaw);
    const rightZ = -Math.sin(yaw);

    const midX = (ax + bx) * 0.5;
    const midZ = (az + bz) * 0.5;
    const boomY = 27.5;
    const halfWidth = 4.1;

    const portalGroup = new THREE.Group();
    portalGroup.position.set(midX, 0, midZ);
    portalGroup.rotation.y = yaw;
    chunkGroup.add(portalGroup);

    const boomHalfSpan = 34.0;
    for (const side of [-1, 1]) {
      for (const zOff of [-4.2, 4.2]) {
        const leg = new THREE.Mesh(this.unitBoxGeo, this.gantryCraneMat);
        leg.position.set(side * 26.0, (boomY - 22.0) * 0.5 - 22.0 + 11.0, zOff);
        leg.scale.set(1.65, boomY + 22.0, 1.45);
        leg.castShadow = true;
        portalGroup.add(leg);
      }
      const caisson = new THREE.Mesh(this.unitBoxGeo, this.gantryDarkSteelMat);
      caisson.position.set(side * 26.0, -13.0, 0);
      caisson.scale.set(5.2, 12.0, 12.5);
      portalGroup.add(caisson);
    }

    for (const zOff of [-4.2, 4.2]) {
      const boom = new THREE.Mesh(this.unitBoxGeo, this.gantryCraneMat);
      boom.position.set(0, boomY, zOff);
      boom.scale.set(boomHalfSpan * 2.0, 1.85, 1.5);
      portalGroup.add(boom);
    }

    const beaconMeshes: THREE.Mesh[] = [];
    for (const side of [-1, 1]) {
      const sig = new THREE.Mesh(this.beaconSphereGeo, this.lockGreenBeaconMat);
      sig.position.set(side * (halfWidth + 1.8), srcShip.baseY + 3.4, baseSpanLen * 0.36);
      sig.scale.setScalar(2.1);
      portalGroup.add(sig);
      beaconMeshes.push(sig);
    }

    const trolleyGroup = new THREE.Group();
    trolleyGroup.position.set(0, boomY - 1.2, 0);
    const trolleyCab = new THREE.Mesh(this.unitBoxGeo, this.gantryDarkSteelMat);
    trolleyCab.scale.set(9.2, 1.6, 10.4);
    trolleyGroup.add(trolleyCab);
    portalGroup.add(trolleyGroup);

    const bridgeSpanGroup = new THREE.Group();
    chunkGroup.add(bridgeSpanGroup);

    const deckSlab = new THREE.Mesh(this.unitBoxGeo, this.deckSteelMat);
    deckSlab.position.set(0, -0.22, 0);
    deckSlab.scale.set(halfWidth * 2.0, 0.44, baseSpanLen + 1.6);
    deckSlab.castShadow = true;
    deckSlab.receiveShadow = true;
    bridgeSpanGroup.add(deckSlab);

    const centerStripe = new THREE.Mesh(this.unitBoxGeo, this.hazardStripeMat);
    centerStripe.position.set(0, 0.02, 0);
    centerStripe.scale.set(0.65, 0.05, baseSpanLen + 1.0);
    bridgeSpanGroup.add(centerStripe);

    for (const side of [-1, 1]) {
      const sideWall = new THREE.Mesh(this.corrugatedBoxGeo, this.gantryCraneMat);
      sideWall.position.set(side * (halfWidth + 0.16), 0.55, 0);
      sideWall.scale.set(0.36, 1.35, baseSpanLen + 0.8);
      sideWall.castShadow = true;
      bridgeSpanGroup.add(sideWall);

      for (const zCable of [-baseSpanLen * 0.28, baseSpanLen * 0.28]) {
        const cableH = boomY - srcShip.baseY;
        const cable = new THREE.Mesh(this.cableCylGeo, this.gantryDarkSteelMat);
        cable.position.set(side * (halfWidth + 0.16), cableH * 0.5, zCable);
        cable.scale.set(1.0, cableH, 1.0);
        bridgeSpanGroup.add(cable);
      }
    }

    const gb: GantryContainerBridge = {
      srcShip,
      dstShip,
      srcLocalX,
      srcLocalZ,
      dstLocalX,
      dstLocalZ,
      baseSpanLen,
      ax,
      az,
      bx,
      bz,
      dirX,
      dirZ,
      rightX,
      rightZ,
      yaw,
      spanLen: baseSpanLen,
      halfWidth,
      period: 8.0,
      phase,
      slideDirSign,
      currentLatOffset: 0,
      liveLatVel: 0,
      currentLiftY: 0,
      liveAy: srcShip.baseY,
      liveBy: dstShip.baseY,
      isLocked: true,
      wasLocked: true,
      trolleyGroup,
      bridgeSpanGroup,
      beaconMeshes,
    };
    this.syncGantryBridgeMesh(gb);
    return gb;
  }

  private syncGantryBridgeMesh(gb: GantryContainerBridge): void {
    // Track live 3D yawed, pitched, rolled & heaved bow/stern world sockets!
    this.shipLocalToWorld(gb.srcShip, gb.srcLocalX, 0.04, gb.srcLocalZ, this._tmpA);
    this.shipLocalToWorld(gb.dstShip, gb.dstLocalX, 0.04, gb.dstLocalZ, this._tmpB);

    gb.ax = this._tmpA.x;
    gb.az = this._tmpA.z;
    gb.bx = this._tmpB.x;
    gb.bz = this._tmpB.z;
    gb.liveAy = this._tmpA.y + gb.currentLiftY;
    gb.liveBy = this._tmpB.y + gb.currentLiftY;

    const dx = gb.bx - gb.ax;
    const dz = gb.bz - gb.az;
    gb.spanLen = Math.max(4.0, Math.hypot(dx, dz));
    gb.dirX = dx / gb.spanLen;
    gb.dirZ = dz / gb.spanLen;
    gb.yaw = Math.atan2(-gb.dirX, -gb.dirZ);
    gb.rightX = Math.cos(gb.yaw);
    gb.rightZ = -Math.sin(gb.yaw);

    const midX = (gb.ax + gb.bx) * 0.5 + gb.rightX * gb.currentLatOffset;
    const midZ = (gb.az + gb.bz) * 0.5 + gb.rightZ * gb.currentLatOffset;
    const midY = (gb.liveAy + gb.liveBy) * 0.5;
    const pitch = Math.atan2(gb.liveBy - gb.liveAy, gb.spanLen);

    gb.bridgeSpanGroup.position.set(midX, midY, midZ);
    gb.bridgeSpanGroup.scale.set(1, 1, gb.spanLen / gb.baseSpanLen);
    this._euler.set(pitch, gb.yaw, 0, 'YXZ');
    gb.bridgeSpanGroup.rotation.copy(this._euler);

    gb.trolleyGroup.position.x = gb.currentLatOffset;
  }

  // ===========================================================================
  // ARTICULATED RO-RO STERN RAMPS & FORECASTLE BOW WAVE-JUMP RAMPS
  // ===========================================================================

  private buildArticulatedShipSpan(
    chunkGroup: THREE.Group,
    kind: 'RORO_BRIDGE' | 'BOW_JUMP_RAMP',
    srcShip: CargoMegashipInstance,
    dstShip: CargoMegashipInstance,
    srcLocalX: number,
    srcLocalZ: number,
    dstLocalX: number,
    dstLocalZ: number
  ): ArticulatedShipSpan {
    const ax = srcShip.x + srcLocalX;
    const az = srcShip.z + srcLocalZ;
    const bx = dstShip.x + dstLocalX;
    const bz = dstShip.z + dstLocalZ;
    const dx = bx - ax;
    const dz = bz - az;
    const baseSpanLen = Math.hypot(dx, dz);
    const dirX = dx / baseSpanLen;
    const dirZ = dz / baseSpanLen;
    const yaw = Math.atan2(-dirX, -dirZ);
    const rightX = Math.cos(yaw);
    const rightZ = -Math.sin(yaw);
    const halfWidth = 4.25;

    const launchGroup = new THREE.Group();
    chunkGroup.add(launchGroup);
    let catchGroup: THREE.Group | null = null;

    if (kind === 'RORO_BRIDGE') {
      const deck = new THREE.Mesh(this.unitBoxGeo, this.deckSteelMat);
      deck.position.set(0, -0.18, 0);
      deck.scale.set(halfWidth * 2.04, 0.36, baseSpanLen + 1.6);
      deck.castShadow = true;
      deck.receiveShadow = true;
      launchGroup.add(deck);

      for (const side of [-1, 1]) {
        const truss = new THREE.Mesh(this.unitBoxGeo, this.hazardStripeMat);
        truss.position.set(side * (halfWidth + 0.15), 0.42, 0);
        truss.scale.set(0.32, 0.95, baseSpanLen + 1.2);
        launchGroup.add(truss);
      }
    } else {
      const rampLen = baseSpanLen * 0.48;
      const rampDeck = new THREE.Mesh(this.unitBoxGeo, this.hazardStripeMat);
      rampDeck.position.set(0, -0.16, 0);
      rampDeck.scale.set((halfWidth + 0.4) * 2.0, 0.34, rampLen + 1.0);
      rampDeck.castShadow = true;
      launchGroup.add(rampDeck);

      catchGroup = new THREE.Group();
      const catchLen = baseSpanLen * 0.28;
      const catchDeck = new THREE.Mesh(this.unitBoxGeo, this.deckSteelMat);
      catchDeck.position.set(0, -0.16, 0);
      catchDeck.scale.set((halfWidth + 1.2) * 2.0, 0.32, catchLen + 1.0);
      catchGroup.add(catchDeck);
      chunkGroup.add(catchGroup);
    }

    const span: ArticulatedShipSpan = {
      kind,
      srcShip,
      dstShip,
      srcLocalX,
      srcLocalZ,
      dstLocalX,
      dstLocalZ,
      baseSpanLen,
      ax,
      az,
      bx,
      bz,
      dirX,
      dirZ,
      rightX,
      rightZ,
      yaw,
      spanLen: baseSpanLen,
      halfWidth,
      liveAy: srcShip.baseY,
      liveBy: dstShip.baseY,
      launchGroup,
      catchGroup,
    };
    this.syncArticulatedShipSpan(span);
    return span;
  }

  private syncArticulatedShipSpan(sp: ArticulatedShipSpan): void {
    this.shipLocalToWorld(sp.srcShip, sp.srcLocalX, 0.04, sp.srcLocalZ, this._tmpA);
    this.shipLocalToWorld(sp.dstShip, sp.dstLocalX, 0.04, sp.dstLocalZ, this._tmpB);

    sp.ax = this._tmpA.x;
    sp.az = this._tmpA.z;
    sp.bx = this._tmpB.x;
    sp.bz = this._tmpB.z;
    sp.liveAy = this._tmpA.y;
    sp.liveBy = this._tmpB.y;

    const dx = sp.bx - sp.ax;
    const dz = sp.bz - sp.az;
    sp.spanLen = Math.max(4.0, Math.hypot(dx, dz));
    sp.dirX = dx / sp.spanLen;
    sp.dirZ = dz / sp.spanLen;
    sp.yaw = Math.atan2(-sp.dirX, -sp.dirZ);
    sp.rightX = Math.cos(sp.yaw);
    sp.rightZ = -Math.sin(sp.yaw);

    const zScale = sp.spanLen / sp.baseSpanLen;

    if (sp.kind === 'RORO_BRIDGE') {
      const midY = (sp.liveAy + sp.liveBy) * 0.5;
      const pitch = Math.atan2(sp.liveBy - sp.liveAy, sp.spanLen);
      sp.launchGroup.position.set((sp.ax + sp.bx) * 0.5, midY, (sp.az + sp.bz) * 0.5);
      sp.launchGroup.scale.set(1, 1, zScale);
      this._euler.set(pitch, sp.yaw, 0, 'YXZ');
      sp.launchGroup.rotation.copy(this._euler);
    } else {
      const rampFrac = 0.48;
      const rampLen = sp.spanLen * rampFrac;
      const lipY = sp.liveAy + 2.35;
      const pitch = Math.atan2(lipY - sp.liveAy, rampLen);
      sp.launchGroup.position.set(
        sp.ax + sp.dirX * (rampLen * 0.5),
        (sp.liveAy + lipY) * 0.5,
        sp.az + sp.dirZ * (rampLen * 0.5)
      );
      sp.launchGroup.scale.set(1, 1, zScale);
      this._euler.set(pitch, sp.yaw, 0, 'YXZ');
      sp.launchGroup.rotation.copy(this._euler);

      if (sp.catchGroup) {
        const catchFrac = 0.28;
        const catchLen = sp.spanLen * catchFrac;
        const catchStartY = sp.liveBy - 0.85;
        const cPitch = Math.atan2(sp.liveBy - catchStartY, catchLen);
        sp.catchGroup.position.set(
          sp.bx - sp.dirX * (catchLen * 0.5),
          (catchStartY + sp.liveBy) * 0.5,
          sp.bz - sp.dirZ * (catchLen * 0.5)
        );
        sp.catchGroup.scale.set(1, 1, zScale);
        this._euler.set(cPitch, sp.yaw, 0, 'YXZ');
        sp.catchGroup.rotation.copy(this._euler);
      }
    }
  }

  // ===========================================================================
  // LIVE SURFACE HEIGHT, MOVING-SHIP VELOCITY, MAGNETIC CRANE & CARGO WALLS
  // ===========================================================================

  public sampleLiveCargoSurface(
    data: CargoMegashipChunkData,
    x: number,
    z: number
  ): { active: boolean; height: number } {
    let bestH = -15.5;
    let found = false;

    // 1. Yawing, Pitching, Rolling & Heaving Cargo Megaship Decks + Elevated Container Rooftops!
    for (let i = 0; i < data.ships.length; i++) {
      const s = data.ships[i];
      if (
        Math.abs(x - s.x) > s.halfBeam + s.halfLength * 0.35 + 2.0 ||
        Math.abs(z - s.z) > s.halfLength + 6.0
      ) {
        continue;
      }

      const deckLoc = this.worldToShipLocalXZ(s, x, z, 0.0);
      const lx = deckLoc.lx;
      const lz = deckLoc.lz;
      if (Math.abs(lx) > s.halfBeam + 0.8 || Math.abs(lz) > s.halfLength + 0.8) continue;

      const bowPinch =
        lz < -s.halfLength * 0.55
          ? lerp(1.0, 0.78, (-lz - s.halfLength * 0.55) / (s.halfLength * 0.45))
          : 1.0;
      const effHalfBeam = s.halfBeam * bowPinch;
      if (Math.abs(lx) <= effHalfBeam) {
        let localY = 0.0;
        if (Math.abs(lx) > effHalfBeam - 0.55) {
          localY += smoothstep(effHalfBeam - 0.55, effHalfBeam, Math.abs(lx)) * 0.42;
        }

        // Check Sloped Ro-Ro Container Rooftop Ramps (`y: 0 -> +3.0m`)!
        for (let r = 0; r < s.rooftopRamps.length; r++) {
          const rmp = s.rooftopRamps[r];
          if (
            Math.abs(lx - rmp.lx) <= rmp.halfW + 0.15 &&
            lz >= rmp.lzStart - 0.25 &&
            lz <= rmp.lzEnd + 0.25
          ) {
            const f = clamp((lz - rmp.lzStart) / (rmp.lzEnd - rmp.lzStart), 0, 1);
            localY = Math.max(localY, lerp(rmp.yStart, rmp.yEnd, f));
          }
        }

        const deckWorldH = this.shipLocalToWorldY(s, lx, localY, lz);
        if (deckWorldH > bestH) {
          bestH = deckWorldH;
          found = true;
        }
      }

      // Check Drivable 40ft Container Rooftop Highways (`localY = +3.0m`) using exact `localY = 3.0` inverse!
      const roofLoc = this.worldToShipLocalXZ(s, x, z, 3.0);
      for (let c = 0; c < s.containerBoxes.length; c++) {
        const cb = s.containerBoxes[c];
        if (!cb.drivableRoof) continue;
        if (
          Math.abs(roofLoc.lx - cb.lx) <= cb.halfW &&
          Math.abs(roofLoc.lz - cb.lz) <= cb.halfL
        ) {
          const roofWorldH = this.shipLocalToWorldY(s, roofLoc.lx, cb.topY, roofLoc.lz);
          if (roofWorldH > bestH) {
            bestH = roofWorldH;
            found = true;
          }
        }
      }
    }

    // 2. Port Gantry Crane Sliding & Locking 40ft Container Drawbridges!
    for (let g = 0; g < data.gantryBridges.length; g++) {
      const gb = data.gantryBridges[g];
      const liveAx = gb.ax + gb.rightX * gb.currentLatOffset;
      const liveAz = gb.az + gb.rightZ * gb.currentLatOffset;
      const dx = x - liveAx;
      const dz = z - liveAz;
      const along = dx * gb.dirX + dz * gb.dirZ;
      if (along < -0.85 || along > gb.spanLen + 0.85) continue;

      const lat = dx * gb.rightX + dz * gb.rightZ;
      const effW = gb.halfWidth + (along < 3.0 || along > gb.spanLen - 3.0 ? 0.75 : 0.0);
      if (Math.abs(lat) <= effW) {
        const f = clamp(along / gb.spanLen, 0, 1);
        const curb =
          Math.abs(lat) > effW - 0.35
            ? smoothstep(effW - 0.35, effW, Math.abs(lat)) * 0.32
            : 0.0;
        const h = lerp(gb.liveAy, gb.liveBy, f) + curb;
        if (h > bestH) {
          bestH = h;
          found = true;
        }
      }
    }

    // 3. Articulated Ro-Ro Stern Bridges & Forecastle Bow Wave-Jump Ramps!
    for (let s = 0; s < data.shipSpans.length; s++) {
      const sp = data.shipSpans[s];
      const dx = x - sp.ax;
      const dz = z - sp.az;
      const along = dx * sp.dirX + dz * sp.dirZ;
      if (along < -0.85 || along > sp.spanLen + 0.85) continue;

      const lat = dx * sp.rightX + dz * sp.rightZ;
      if (sp.kind === 'RORO_BRIDGE') {
        const effW = sp.halfWidth + (along < 3.5 || along > sp.spanLen - 3.5 ? 0.9 : 0.0);
        if (Math.abs(lat) <= effW) {
          const f = clamp(along / sp.spanLen, 0, 1);
          const curb =
            Math.abs(lat) > effW - 0.35
              ? smoothstep(effW - 0.35, effW, Math.abs(lat)) * 0.28
              : 0.0;
          const h = lerp(sp.liveAy, sp.liveBy, f) + curb;
          if (h > bestH) {
            bestH = h;
            found = true;
          }
        }
      } else {
        const rampEnd = sp.spanLen * 0.48;
        const catchStart = sp.spanLen * 0.72;
        if (along <= rampEnd && Math.abs(lat) <= sp.halfWidth + 0.45) {
          const u = clamp(along / rampEnd, 0, 1);
          const h = lerp(sp.liveAy, sp.liveAy + 2.35, u);
          if (h > bestH) {
            bestH = h;
            found = true;
          }
        } else if (along >= catchStart && Math.abs(lat) <= sp.halfWidth + 1.2) {
          const u = clamp((along - catchStart) / (sp.spanLen - catchStart), 0, 1);
          const h = lerp(sp.liveBy - 0.85, sp.liveBy, u);
          if (h > bestH) {
            bestH = h;
            found = true;
          }
        }
      }
    }

    return { active: found, height: bestH };
  }

  public sampleSurfaceVelocityInChunk(
    data: CargoMegashipChunkData,
    x: number,
    z: number,
    outVel: THREE.Vector3
  ): boolean {
    for (let g = 0; g < data.gantryBridges.length; g++) {
      const gb = data.gantryBridges[g];
      const liveAx = gb.ax + gb.rightX * gb.currentLatOffset;
      const liveAz = gb.az + gb.rightZ * gb.currentLatOffset;
      const dx = x - liveAx;
      const dz = z - liveAz;
      const along = dx * gb.dirX + dz * gb.dirZ;
      if (
        along >= 0 &&
        along <= gb.spanLen &&
        Math.abs(dx * gb.rightX + dz * gb.rightZ) <= gb.halfWidth + 0.5
      ) {
        outVel.set(gb.rightX * gb.liveLatVel, 0, gb.rightZ * gb.liveLatVel);
        return true;
      }
    }

    // 4-DOF Yawing, Pitching, Rolling & Heaving Ship Deck + Surf Wash Velocity!
    for (let i = 0; i < data.ships.length; i++) {
      const s = data.ships[i];
      const loc = this.worldToShipLocalXZ(s, x, z, 0.0);
      if (Math.abs(loc.lx) <= s.halfBeam && Math.abs(loc.lz) <= s.halfLength) {
        const vy =
          s.dCenterYDt +
          loc.lx * Math.cos(s.liveRoll) * s.dRollDt -
          loc.lz * Math.cos(s.livePitch) * s.dPitchDt;

        // Tangential deck velocity from wave-induced ship YAW rotation (`wx = sx + x2*cosY + z2*sinY` -> `dwx/dt = (z - sz)*dYawDt`, `dwz/dt = -(x - sx)*dYawDt`)!
        let vx = (z - s.z) * s.dYawDt;
        let vz = -(x - s.x) * s.dYawDt;

        if (Math.abs(loc.lz - s.surfWashZ) < 6.5 && s.surfFoamMesh && s.surfFoamMesh.visible) {
          const localSurfVx = Math.sin(s.surfWashPhase) * 1.65;
          vx += localSurfVx * Math.cos(s.liveYaw);
          vz -= localSurfVx * Math.sin(s.liveYaw);
        }

        outVel.set(vx, vy, vz);
        return true;
      }
    }
    return false;
  }

  /**
   * 240Hz Substep for Rotating Electromagnet Cranes (`MagneticVehicleCrane`):
   * - Applies proximity magnetic pull when `carPos` approaches within `6.4m` of the magnet disc!
   * - Captures, hoists (`+9.8m`), and swings the vehicle across the deck!
   * - Releases when `[SHIFT]` Boost overloads the coils or when the crane cable shorts out!
   */
  public stepMagneticCraneSubstepInChunk(
    data: CargoMegashipChunkData,
    dt: number,
    carPos: THREE.Vector3,
    carQuat: THREE.Quaternion,
    linearVel: THREE.Vector3,
    angularVel: THREE.Vector3,
    isBoosting: boolean,
    passiveObserver: boolean
  ): void {
    if (passiveObserver) {
      this.releaseAnyCapturedCrane();
      return;
    }

    for (let i = 0; i < data.ownedShips.length; i++) {
      const s = data.ownedShips[i];
      for (let m = 0; m < s.magneticCranes.length; m++) {
        const mc = s.magneticCranes[m];
        if (!mc.hasPrevWorldPos) continue;

        if (mc.state === 'SWEEPING' && !this.activeCapturedCrane) {
          const dx = carPos.x - mc.magnetWorldPos.x;
          const dz = carPos.z - mc.magnetWorldPos.z;
          const distXZ = Math.hypot(dx, dz);
          const dy = mc.magnetWorldPos.y - carPos.y;

          // Magnetic Field Proximity Pull (`distXZ < 6.4m`)!
          if (distXZ < 6.4 && dy > -0.6 && dy < 5.8) {
            const pullStrength = 1.0 - distXZ / 6.4;
            const invD = 1.0 / Math.max(0.25, distXZ);
            linearVel.x -= dx * invD * pullStrength * 16.5 * dt;
            linearVel.z -= dz * invD * pullStrength * 16.5 * dt;
            linearVel.y += pullStrength * 13.5 * dt;

            // Lock onto vehicle roof if too close (`distXZ < 4.15m`)!
            if (distXZ < 4.15 && dy < 4.9) {
              mc.state = 'CAPTURED';
              mc.stateTimer = 0;
              mc.boostBreakAccum = 0;
              this.activeCapturedCrane = mc;
              if (this.onMagnetCapture) {
                this.onMagnetCapture(mc.magnetWorldPos.clone());
              }
            }
          }
        } else if (mc.state === 'CAPTURED') {
          // Allow `[SHIFT]` Boost to overload & snap free from the electromagnet!
          if (isBoosting) {
            mc.boostBreakAccum += dt;
            if (mc.boostBreakAccum >= 0.32) {
              mc.state = 'COOLDOWN';
              mc.stateTimer = 0;
              if (this.activeCapturedCrane === mc) this.activeCapturedCrane = null;
              const fwd = this._tmpA.set(0, 0, -1).applyQuaternion(carQuat);
              linearVel.addScaledVector(fwd, 9.5);
              linearVel.y = Math.max(linearVel.y, 2.2);
              if (this.onMagnetRelease) {
                this.onMagnetRelease(mc.magnetWorldPos.clone(), true);
              }
              continue;
            }
          } else {
            mc.boostBreakAccum = Math.max(0, mc.boostBreakAccum - dt * 0.5);
          }

          // Hold & swing dangling 4x4 directly beneath the energized Electromagnet Disc!
          const attachX = mc.magnetWorldPos.x + mc.pendulumSwayX;
          const attachY = mc.magnetWorldPos.y - 1.28;
          const attachZ = mc.magnetWorldPos.z + mc.pendulumSwayZ;
          const snapAlpha = 1.0 - Math.exp(-32.0 * dt);

          carPos.x = lerp(carPos.x, attachX, snapAlpha);
          carPos.y = lerp(carPos.y, attachY, snapAlpha);
          carPos.z = lerp(carPos.z, attachZ, snapAlpha);

          linearVel.copy(mc.magnetWorldVel);
          angularVel.multiplyScalar(Math.exp(-14.0 * dt));

          // Gently level roll/pitch while slewing vehicle yaw smoothly with crane boom!
          const fwd = this._tmpA.set(0, 0, -1).applyQuaternion(carQuat);
          const curYaw = Math.atan2(-fwd.x, -fwd.z);
          const swayPitch = clamp(-mc.magnetWorldVel.z * 0.025, -0.18, 0.18);
          const swayRoll = clamp(mc.magnetWorldVel.x * 0.025, -0.22, 0.22);
          this._euler.set(swayPitch, curYaw + dt * 0.55, swayRoll, 'YXZ');
          this._targetQuat.setFromEuler(this._euler);
          carQuat.slerp(this._targetQuat, 1.0 - Math.exp(-10.0 * dt));
        }
      }
    }
  }

  /**
   * 240Hz Rigid-Body Collision against:
   *  1. Fixed Shipping-Container Stacks & Open-Door Container Tunnel Walls (rotated in ship local space!)
   *  2. Unsecured Loose Deck Cargo Containers sliding across the tilted deck (with live momentum transfer!)
   *  3. Dropped Cargo Containers released by Malfunctioning / Broken Cargo Cranes!
   *  4. Crane Pedestals!
   */
  public resolveShipContainerCollisionsInChunk(
    data: CargoMegashipChunkData,
    carPos: THREE.Vector3,
    forwardWorld: THREE.Vector3,
    rightWorld: THREE.Vector3,
    linearVel: THREE.Vector3,
    angularVel: THREE.Vector3,
    passiveProtection: boolean,
    onImpact?: (intensity: number, worldPoint: THREE.Vector3) => void
  ): void {
    if (passiveProtection || this.activeCapturedCrane) return;

    const hullOffsets = [-1.25, 0.0, 1.15];
    const sphereRad = 0.94;

    for (let i = 0; i < data.ships.length; i++) {
      const s = data.ships[i];
      if (
        Math.abs(carPos.x - s.x) > s.halfBeam + s.halfLength * 0.35 + 4.0 ||
        Math.abs(carPos.z - s.z) > s.halfLength + 6.0
      ) {
        continue;
      }

      const cosY = Math.cos(s.liveYaw);
      const sinY = Math.sin(s.liveYaw);

      const collideShipLocalBox = (
        boxLx: number,
        boxLz: number,
        halfW: number,
        halfL: number,
        topLocalY: number,
        drivableRoof: boolean,
        boxWorldVx: number,
        boxWorldVz: number,
        onLooseNudge?: (impLocalX: number, impLocalZ: number) => void
      ) => {
        const roofWorldY = this.shipLocalToWorldY(s, boxLx, topLocalY, boxLz);
        if (carPos.y >= roofWorldY - (drivableRoof ? 0.58 : -0.15)) {
          return;
        }

        for (let h = 0; h < hullOffsets.length; h++) {
          const zOff = hullOffsets[h];
          const sx = carPos.x + forwardWorld.x * -zOff;
          const sz = carPos.z + forwardWorld.z * -zOff;
          const sLoc = this.worldToShipLocalXZ(s, sx, sz, 0.0);

          const clampedLx = clamp(sLoc.lx, boxLx - halfW, boxLx + halfW);
          const clampedLz = clamp(sLoc.lz, boxLz - halfL, boxLz + halfL);
          const dlx = sLoc.lx - clampedLx;
          const dlz = sLoc.lz - clampedLz;
          const distSq = dlx * dlx + dlz * dlz;

          if (distSq < sphereRad * sphereRad) {
            let nlx = 0;
            let nlz = 0;
            let overlap = 0;

            if (distSq > 1e-6) {
              const dist = Math.sqrt(distSq);
              nlx = dlx / dist;
              nlz = dlz / dist;
              overlap = sphereRad - dist;
            } else {
              const penX = halfW - Math.abs(sLoc.lx - boxLx);
              const penZ = halfL - Math.abs(sLoc.lz - boxLz);
              if (penX < penZ) {
                nlx = sLoc.lx >= boxLx ? 1 : -1;
                overlap = penX + sphereRad;
              } else {
                nlz = sLoc.lz >= boxLz ? 1 : -1;
                overlap = penZ + sphereRad;
              }
            }

            // Rotate normal `(nlx, nlz)` from ship local into world XZ using `Ry(liveYaw)`!
            const nx = nlx * cosY + nlz * sinY;
            const nz = -nlx * sinY + nlz * cosY;

            carPos.x += nx * overlap * 0.92;
            carPos.z += nz * overlap * 0.92;

            const relVx = linearVel.x - boxWorldVx;
            const relVz = linearVel.z - boxWorldVz;
            const relVn = relVx * nx + relVz * nz;
            if (relVn < 0) {
              const jMag = -1.28 * relVn;
              linearVel.x += nx * jMag;
              linearVel.z += nz * jMag;

              const localHitX = nx * rightWorld.x + nz * rightWorld.z;
              angularVel.y += clamp(-zOff * localHitX * jMag * 0.075, -1.6, 1.6);

              if (onLooseNudge) {
                onLooseNudge(-nlx * jMag * 0.22, -nlz * jMag * 0.22);
              }

              if (Math.abs(relVn) > 2.4 && onImpact) {
                this.shipLocalToWorld(s, clampedLx, 0.8, clampedLz, this._hitContact);
                onImpact(clamp(Math.abs(relVn) / 15.0, 0.25, 1.0), this._hitContact);
              }
            }
          }
        }
      };

      // 1. Fixed Container Stacks & Open-Door Container Tunnel Walls!
      for (let c = 0; c < s.containerBoxes.length; c++) {
        const cb = s.containerBoxes[c];
        collideShipLocalBox(
          cb.lx,
          cb.lz,
          cb.halfW,
          cb.halfL,
          cb.topY,
          cb.drivableRoof,
          0,
          0
        );
      }

      // 2. Unsecured Sliding Deck Cargo Containers & Crates!
      for (let sc = 0; sc < s.slidingCargos.length; sc++) {
        const cargo = s.slidingCargos[sc];
        collideShipLocalBox(
          cargo.lx,
          cargo.lz,
          cargo.halfW,
          cargo.halfL,
          cargo.height,
          false,
          cargo.worldVx,
          cargo.worldVz,
          (impX, impZ) => {
            cargo.vx = clamp(cargo.vx + impX, -11.5, 11.5);
            cargo.vz = clamp(cargo.vz + impZ, -11.5, 11.5);
          }
        );
      }

      // 3. Dropped Cargo Containers from Malfunctioning / Broken Cranes!
      for (let bc = 0; bc < s.breakableCranes.length; bc++) {
        const crane = s.breakableCranes[bc];
        if (crane.cargoLy < 3.4) {
          collideShipLocalBox(
            crane.cargoLx,
            crane.cargoLz,
            crane.halfW,
            crane.halfL,
            crane.cargoLy + crane.height,
            false,
            crane.worldVx,
            crane.worldVz
          );
        }
      }

      // 4. Crane Gunwale Pedestals!
      for (let m = 0; m < s.magneticCranes.length; m++) {
        const mc = s.magneticCranes[m];
        collideShipLocalBox(mc.pedestalLx, mc.pedestalLz, 1.6, 1.6, 5.0, false, 0, 0);
      }
      for (let b = 0; b < s.breakableCranes.length; b++) {
        const bc = s.breakableCranes[b];
        collideShipLocalBox(bc.pedestalLx, bc.pedestalLz, 1.7, 1.7, 8.0, false, 0, 0);
      }
    }
  }

  // ===========================================================================
  // CHUNK & GLOBAL STORM ANIMATION STEP
  // ===========================================================================

  private stepLooseDeckCargoOnShip(s: CargoMegashipInstance, dt: number): void {
    if (s.slidingCargos.length === 0) return;

    // Gravity component tangent to the rolling & pitching wet steel ship deck!
    const gDeckX = -9.81 * Math.sin(s.liveRoll) * 1.75;
    const gDeckZ = 9.81 * Math.sin(s.livePitch) * 1.65;
    const cosY = Math.cos(s.liveYaw);
    const sinY = Math.sin(s.liveYaw);

    for (let i = 0; i < s.slidingCargos.length; i++) {
      const c = s.slidingCargos[i];
      c.slamCooldown = Math.max(0, c.slamCooldown - dt);

      c.vx += gDeckX * dt;
      c.vz += gDeckZ * dt;

      // Wet-steel kinetic friction + viscous drag
      const spd = Math.hypot(c.vx, c.vz);
      if (spd > 1e-4) {
        const decel = (0.58 + spd * 0.24) * dt;
        const nextSpd = Math.min(11.0, Math.max(0, spd - decel));
        c.vx *= nextSpd / spd;
        c.vz *= nextSpd / spd;
      }

      c.lx += c.vx * dt;
      c.lz += c.vz * dt;
      c.localYaw = clamp(c.localYaw + c.yawVel * dt, -0.38, 0.38);
      c.yawVel *= Math.exp(-2.8 * dt);

      // Rebound off Ship Port & Starboard Gunwale Bulwarks!
      const bowPinch =
        c.lz < -s.halfLength * 0.55
          ? lerp(1.0, 0.78, (-c.lz - s.halfLength * 0.55) / (s.halfLength * 0.45))
          : 1.0;
      const maxX = s.halfBeam * bowPinch - c.halfW - 0.85;
      if (Math.abs(c.lx) > maxX) {
        const hitSpd = Math.abs(c.vx);
        c.lx = Math.sign(c.lx) * maxX;
        c.vx = -c.vx * 0.42;
        c.yawVel += Math.sign(c.lx) * 0.55;
        if (hitSpd > 3.4 && c.slamCooldown <= 0) {
          c.slamCooldown = 1.4;
          if (this.onLooseCargoSlam) {
            this.shipLocalToWorld(s, c.lx, 0.8, c.lz, this._tmpA);
            this.onLooseCargoSlam(this._tmpA.clone());
          }
        }
      }

      // Rebound off fore/aft deck bay bulkheads!
      if (c.lz < c.bayZMin) {
        c.lz = c.bayZMin;
        c.vz = Math.abs(c.vz) * 0.38;
      } else if (c.lz > c.bayZMax) {
        c.lz = c.bayZMax;
        c.vz = -Math.abs(c.vz) * 0.38;
      }

      // Rebound off Fixed Container Stacks & Tunnel Walls!
      for (let b = 0; b < s.containerBoxes.length; b++) {
        const cb = s.containerBoxes[b];
        const dx = c.lx - cb.lx;
        const dz = c.lz - cb.lz;
        const ox = c.halfW + cb.halfW + 0.15 - Math.abs(dx);
        const oz = c.halfL + cb.halfL + 0.15 - Math.abs(dz);
        if (ox > 0 && oz > 0) {
          if (ox < oz) {
            const sgn = dx >= 0 ? 1 : -1;
            c.lx += sgn * ox;
            if (c.vx * sgn < 0) {
              const hitSpd = Math.abs(c.vx);
              c.vx = -c.vx * 0.4;
              c.yawVel += sgn * 0.45;
              if (hitSpd > 3.6 && c.slamCooldown <= 0) {
                c.slamCooldown = 1.4;
                if (this.onLooseCargoSlam) {
                  this.shipLocalToWorld(s, c.lx, 0.8, c.lz, this._tmpA);
                  this.onLooseCargoSlam(this._tmpA.clone());
                }
              }
            }
          } else {
            const sgn = dz >= 0 ? 1 : -1;
            c.lz += sgn * oz;
            if (c.vz * sgn < 0) c.vz = -c.vz * 0.38;
          }
        }
      }

      // Cache world sliding velocity for 240Hz vehicle impact transfer!
      c.worldVx = c.vx * cosY + c.vz * sinY;
      c.worldVz = -c.vx * sinY + c.vz * cosY;

      c.meshGroup.position.set(c.lx, 0.02, c.lz);
      c.meshGroup.rotation.y = c.localYaw;
    }
  }

  private stepMagneticCranesOnShip(
    s: CargoMegashipInstance,
    dt: number,
    elapsed: number
  ): void {
    for (let m = 0; m < s.magneticCranes.length; m++) {
      const mc = s.magneticCranes[m];
      mc.stateTimer += dt;

      if (mc.state === 'SWEEPING') {
        mc.currentBoomYaw =
          mc.baseBoomYaw + Math.sin(elapsed * mc.sweepFreq + mc.phase) * mc.sweepAmp;
        mc.localMagnetY = lerp(
          mc.localMagnetY,
          4.1 + Math.sin(elapsed * 2.1 + mc.phase) * 0.25,
          1.0 - Math.exp(-8.0 * dt)
        );
        mc.coilGlowMesh.material = this.magnetCyanGlowMat;
        mc.beaconMesh.material = this.magnetCyanGlowMat;
        mc.dangerRingMesh.visible = true;
        const pulse = 0.92 + Math.sin(elapsed * 7.5 + mc.phase) * 0.12;
        mc.dangerRingMesh.scale.set(pulse, 1, pulse);
      } else if (mc.state === 'CAPTURED') {
        // Winch captured 4x4 HIGH into the storm air (`localMagnetY -> 9.8m`) & slew boom across the ship!
        const slewDir = mc.pedestalLx > 0 ? -1 : 1;
        mc.currentBoomYaw = clamp(
          mc.currentBoomYaw + slewDir * 0.62 * dt,
          mc.baseBoomYaw - 0.92,
          mc.baseBoomYaw + 0.92
        );
        mc.localMagnetY = lerp(mc.localMagnetY, 9.8, 1.0 - Math.exp(-3.4 * dt));
        mc.coilGlowMesh.material = this.lockGreenBeaconMat;
        mc.beaconMesh.material =
          Math.floor(elapsed * 12) % 2 === 0
            ? this.dangerRedBeaconMat
            : this.magnetCyanGlowMat;
        mc.dangerRingMesh.visible = true;
        mc.dangerRingMesh.scale.set(1.18, 1, 1.18);

        // Crane Electromagnet Overload / Cable Short-Circuit Release after 2.55s!
        if (mc.stateTimer >= 2.55) {
          mc.state = 'COOLDOWN';
          mc.stateTimer = 0;
          if (this.activeCapturedCrane === mc) this.activeCapturedCrane = null;
          if (this.onCraneBreakSpark) {
            this.onCraneBreakSpark(mc.magnetWorldPos.clone());
          }
          if (this.onMagnetRelease) {
            this.onMagnetRelease(mc.magnetWorldPos.clone(), false);
          }
        }
      } else {
        // COOLDOWN: rebooting de-energized magnet coils
        mc.currentBoomYaw =
          mc.baseBoomYaw + Math.sin(elapsed * mc.sweepFreq + mc.phase) * mc.sweepAmp;
        mc.localMagnetY = lerp(mc.localMagnetY, 6.2, 1.0 - Math.exp(-4.0 * dt));
        mc.coilGlowMesh.material = this.magnetDimMat;
        mc.beaconMesh.material = this.slideAmberBeaconMat;
        mc.dangerRingMesh.visible = false;
        if (mc.stateTimer >= 4.5) {
          mc.state = 'SWEEPING';
          mc.stateTimer = 0;
        }
      }

      // Sync Turret Yaw, Telescoping Hoist Cable & Magnet Head!
      mc.turretGroup.rotation.y = mc.currentBoomYaw;
      const tipLocalYInTurret = mc.boomTopY - 4.2;
      const magnetYInTurret = mc.localMagnetY - 4.2;
      const cableLen = Math.max(0.6, tipLocalYInTurret - magnetYInTurret);

      mc.cableMesh.position.set(
        0,
        (tipLocalYInTurret + magnetYInTurret) * 0.5,
        mc.boomLength
      );
      mc.cableMesh.scale.set(1, cableLen, 1);
      mc.magnetHeadGroup.position.set(0, magnetYInTurret, mc.boomLength);

      mc.localTipX = mc.pedestalLx + Math.sin(mc.currentBoomYaw) * mc.boomLength;
      mc.localTipZ = mc.pedestalLz + Math.cos(mc.currentBoomYaw) * mc.boomLength;
      mc.dangerRingMesh.position.set(mc.localTipX, 0.11, mc.localTipZ);

      // Update exact 3D world magnet position & velocity for 240Hz vehicle capture/swing!
      this.shipLocalToWorld(
        s,
        mc.localTipX,
        mc.localMagnetY,
        mc.localTipZ,
        this._tmpA
      );
      if (mc.hasPrevWorldPos && dt > 1e-5) {
        mc.magnetWorldVel
          .subVectors(this._tmpA, mc.prevMagnetWorldPos)
          .divideScalar(dt);
      } else {
        mc.magnetWorldVel.set(0, 0, 0);
        mc.hasPrevWorldPos = true;
      }
      mc.prevMagnetWorldPos.copy(this._tmpA);
      mc.magnetWorldPos.copy(this._tmpA);
      mc.pendulumSwayX = Math.sin(elapsed * 4.4 + mc.phase) * 0.32;
      mc.pendulumSwayZ = Math.cos(elapsed * 3.9 + mc.phase) * 0.28;
    }
  }

  private stepBreakableCranesOnShip(
    s: CargoMegashipInstance,
    dt: number,
    elapsed: number
  ): void {
    const cosY = Math.cos(s.liveYaw);
    const sinY = Math.sin(s.liveYaw);

    for (let b = 0; b < s.breakableCranes.length; b++) {
      const bc = s.breakableCranes[b];
      const cyc = (((elapsed + bc.phase) % bc.period) + bc.period) % bc.period;
      const u = cyc / bc.period; // 0..1

      const boomYaw =
        bc.baseBoomYaw + Math.sin(elapsed * 0.55 + bc.phase) * bc.sweepAmp;
      bc.turretGroup.rotation.y = boomYaw;

      const tipLx = bc.pedestalLx + Math.sin(boomYaw) * bc.boomLength;
      const tipLz = bc.pedestalLz + Math.cos(boomYaw) * bc.boomLength;
      const hoistHeightY = 10.6;

      if (u < 0.36) {
        // PHASE 1: HOIST_SWAY — heavy 20ft container swaying overhead on 4 cables!
        bc.state = 'HOIST_SWAY';
        bc.cargoLx = tipLx;
        bc.cargoLy = hoistHeightY;
        bc.cargoLz = tipLz;
        bc.cargoFallVy = 0;
        bc.cargoSlideVx = 0;
        bc.cargoSlideVz = 0;
        bc.cargoRollTilt = Math.sin(elapsed * 2.8 + bc.phase) * 0.18;
        bc.cargoLocalYaw = boomYaw + Math.PI * 0.5;
        bc.snappedCableMesh.visible = false;
        bc.intactCablesGroup.visible = true;
        bc.beaconMesh.material = this.slideAmberBeaconMat;
        bc.impactRingMesh.visible = false;
      } else if (u < 0.52) {
        // PHASE 2: OVERLOAD_SPARKING — winch overload klaxon, shuddering cables & red impact reticle!
        bc.state = 'OVERLOAD_SPARKING';
        const jitter = Math.sin(elapsed * 42.0) * 0.16;
        bc.cargoLx = tipLx + jitter;
        bc.cargoLy = hoistHeightY - 0.35 + Math.cos(elapsed * 38.0) * 0.12;
        bc.cargoLz = tipLz;
        bc.cargoRollTilt = 0.24 + jitter * 0.6;
        bc.cargoLocalYaw = boomYaw + Math.PI * 0.5;
        bc.snappedCableMesh.visible = false;
        bc.intactCablesGroup.visible = true;
        bc.beaconMesh.material =
          Math.floor(elapsed * 14) % 2 === 0
            ? this.dangerRedBeaconMat
            : this.slideAmberBeaconMat;
        bc.impactRingMesh.visible = true;
        bc.impactRingMesh.position.set(tipLx, 0.12, tipLz);
        const rPulse = 0.85 + Math.sin(elapsed * 16.0) * 0.18;
        bc.impactRingMesh.scale.set(rPulse, 1, rPulse);

        if (bc.prevState !== 'OVERLOAD_SPARKING' && this.onCraneBreakSpark) {
          this.shipLocalToWorld(s, tipLx, bc.boomTopY - 1.0, tipLz, this._tmpA);
          this.onCraneBreakSpark(this._tmpA.clone());
        }
      } else if (u < 0.91) {
        // PHASE 3 & 4: CABLE SNAPS -> FREEFALL -> DECK CRASH & WAVE-DRIVEN SLIDING!
        bc.snappedCableMesh.visible = true;
        bc.snappedCableMesh.rotation.z = 0.55 + Math.sin(elapsed * 9.0) * 0.28;
        bc.intactCablesGroup.visible = false;
        bc.beaconMesh.material = this.dangerRedBeaconMat;

        if (
          bc.prevState !== 'SNAPPED_FREEFALL' &&
          bc.prevState !== 'CRASHED_SLIDING'
        ) {
          // Initialize cable snap release!
          bc.state = 'SNAPPED_FREEFALL';
          bc.cargoLx = tipLx;
          bc.cargoLy = hoistHeightY - 0.35;
          bc.cargoLz = tipLz;
          bc.cargoFallVy = -1.5;
          bc.cargoSlideVx = -9.81 * Math.sin(s.liveRoll) * 0.35;
          bc.cargoSlideVz = 0;
          if (this.onCraneBreakSpark) {
            this.shipLocalToWorld(s, tipLx, bc.boomTopY - 1.5, tipLz, this._tmpA);
            this.onCraneBreakSpark(this._tmpA.clone());
          }
        }

        if (bc.cargoLy > 0.03 && bc.state !== 'CRASHED_SLIDING') {
          bc.state = 'SNAPPED_FREEFALL';
          bc.cargoFallVy -= 15.8 * dt;
          bc.cargoLy += bc.cargoFallVy * dt;
          bc.cargoRollTilt = clamp(bc.cargoRollTilt + 0.55 * dt, -0.45, 0.45);
          bc.impactRingMesh.visible = true;
          bc.impactRingMesh.position.set(bc.cargoLx, 0.12, bc.cargoLz);

          if (bc.cargoLy <= 0.03) {
            bc.cargoLy = 0.03;
            bc.cargoFallVy = 0;
            bc.cargoRollTilt = 0;
            bc.state = 'CRASHED_SLIDING';
            if (this.onCraneCargoCrash) {
              this.shipLocalToWorld(s, bc.cargoLx, 0.4, bc.cargoLz, this._tmpA);
              this.onCraneCargoCrash(this._tmpA.clone());
            }
          }
        } else {
          // Released cargo container slides across the pitching/rolling steel deck!
          bc.state = 'CRASHED_SLIDING';
          bc.cargoLy = 0.03;
          bc.cargoRollTilt = 0;
          bc.impactRingMesh.visible = false;

          bc.cargoSlideVx += -9.81 * Math.sin(s.liveRoll) * 1.65 * dt;
          bc.cargoSlideVz += 9.81 * Math.sin(s.livePitch) * 1.45 * dt;
          bc.cargoSlideVx *= Math.exp(-0.55 * dt);
          bc.cargoSlideVz *= Math.exp(-0.55 * dt);
          bc.cargoLx += bc.cargoSlideVx * dt;
          bc.cargoLz += bc.cargoSlideVz * dt;

          const maxSlideX = s.halfBeam * 0.82 - bc.halfW - 0.8;
          if (Math.abs(bc.cargoLx) > maxSlideX) {
            bc.cargoLx = Math.sign(bc.cargoLx) * maxSlideX;
            bc.cargoSlideVx = -bc.cargoSlideVx * 0.4;
          }
          bc.cargoLz = clamp(
            bc.cargoLz,
            -s.halfLength + 7.0,
            s.halfLength - 11.0
          );
        }
      } else {
        // Winch salvage reset (`0.91 .. 1.0`) smoothly lifting fresh container back up to boom!
        bc.state = 'HOIST_SWAY';
        const resetAlpha = (u - 0.91) / 0.09;
        bc.cargoLx = lerp(bc.cargoLx, tipLx, 1.0 - Math.exp(-10.0 * dt));
        bc.cargoLz = lerp(bc.cargoLz, tipLz, 1.0 - Math.exp(-10.0 * dt));
        bc.cargoLy = lerp(0.03, hoistHeightY, smoothstep(0.0, 1.0, resetAlpha));
        bc.cargoRollTilt = 0;
        bc.cargoSlideVx = 0;
        bc.cargoSlideVz = 0;
        bc.snappedCableMesh.visible = false;
        bc.intactCablesGroup.visible = true;
        bc.impactRingMesh.visible = false;
        bc.beaconMesh.material = this.slideAmberBeaconMat;
      }

      // Sync intact cable length when attached!
      if (bc.intactCablesGroup.visible) {
        const cLen = Math.max(0.8, bc.boomTopY - (bc.cargoLy + bc.height));
        for (let cIdx = 0; cIdx < bc.intactCablesGroup.children.length; cIdx++) {
          const ch = bc.intactCablesGroup.children[cIdx];
          ch.position.y = -cLen * 0.5;
          ch.scale.set(1, cLen, 1);
        }
      }

      bc.worldVx = bc.cargoSlideVx * cosY + bc.cargoSlideVz * sinY;
      bc.worldVz = -bc.cargoSlideVx * sinY + bc.cargoSlideVz * cosY;

      bc.cargoGroup.position.set(bc.cargoLx, bc.cargoLy, bc.cargoLz);
      this._euler.set(0, bc.cargoLocalYaw, bc.cargoRollTilt, 'YXZ');
      bc.cargoGroup.rotation.copy(this._euler);

      bc.prevState = bc.state;
    }
  }

  public stepChunkHazards(
    data: CargoMegashipChunkData,
    dt: number,
    elapsed: number
  ): void {
    const clampedDt = Math.min(dt, 0.033);

    // 1. Yaw, Pitch, Roll & Heave all Cargo Megaships on the Storm Ocean Swell!
    for (let i = 0; i < data.ships.length; i++) {
      const s = data.ships[i];
      const pose = this.sampleShipPose(s, elapsed);
      s.liveCenterY = pose.centerY;
      s.dCenterYDt = pose.dCenterYDt;
      s.livePitch = pose.pitch;
      s.dPitchDt = pose.dPitchDt;
      s.liveRoll = pose.roll;
      s.dRollDt = pose.dRollDt;
      s.liveYaw = pose.yaw;
      s.dYawDt = pose.dYawDt;

      s.shipGroup.position.set(s.x, s.liveCenterY, s.z);
      this._euler.set(s.livePitch, s.liveYaw, s.liveRoll, 'YXZ');
      s.shipGroup.rotation.copy(this._euler);

      if (s.radarSpinner) {
        s.radarSpinner.rotation.y = elapsed * 2.6;
      }

      s.surfWashPhase = elapsed * 1.45 + s.phase;
      if (s.surfFoamMesh) {
        const waveSweep = Math.sin(s.surfWashPhase);
        s.surfFoamMesh.position.x = waveSweep * (s.halfBeam * 0.38);
        s.surfFoamMesh.visible = Math.abs(waveSweep) > 0.18;
      }

      // Step Unsecured Sliding Deck Cargo, Rotating Electromagnet Cranes & Breaking Cargo Cranes!
      if (s.slidingCargos.length > 0) {
        this.stepLooseDeckCargoOnShip(s, clampedDt);
      }
      if (s.magneticCranes.length > 0) {
        this.stepMagneticCranesOnShip(s, clampedDt, elapsed);
      }
      if (s.breakableCranes.length > 0) {
        this.stepBreakableCranesOnShip(s, clampedDt, elapsed);
      }
    }

    // 2. Step Overhead Port Gantry Cranes Sliding & Locking 40ft Container Drawbridges!
    for (let g = 0; g < data.gantryBridges.length; g++) {
      const gb = data.gantryBridges[g];
      const st = this.sampleGantryLockState(gb.period, gb.phase, gb.slideDirSign, elapsed);
      gb.currentLatOffset = st.latOffset;
      gb.liveLatVel = st.latVel;
      gb.currentLiftY = st.liftY;
      gb.isLocked = st.isLocked;

      if (gb.isLocked !== gb.wasLocked) {
        const mat = gb.isLocked ? this.lockGreenBeaconMat : this.slideAmberBeaconMat;
        for (let b = 0; b < gb.beaconMeshes.length; b++) {
          gb.beaconMeshes[b].material = mat;
        }
        if (gb.isLocked && this.onGantryBridgeLocked) {
          this.onGantryBridgeLocked(
            new THREE.Vector3(
              (gb.ax + gb.bx) * 0.5,
              (gb.liveAy + gb.liveBy) * 0.5 + 0.5,
              (gb.az + gb.bz) * 0.5
            )
          );
        }
        gb.wasLocked = gb.isLocked;
      }

      this.syncGantryBridgeMesh(gb);
    }

    // 3. Sync Articulated Ro-Ro Bridges & Bow Jump Ramps to both yawing/pitching ships!
    for (let s = 0; s < data.shipSpans.length; s++) {
      this.syncArticulatedShipSpan(data.shipSpans[s]);
    }
  }

  public stepGlobalStormOceanAtmosphere(
    active: boolean,
    focusPos: THREE.Vector3,
    elapsed: number,
    dt: number
  ): void {
    this.atmosphereFxGroup.visible = active;
    if (!active) {
      this.currentLightningFlash = 0.0;
      this.releaseAnyCapturedCrane();
      return;
    }

    const clampedDt = Math.min(dt, 0.05);
    this.oceanMesh.position.x = focusPos.x;
    this.oceanMesh.position.z = focusPos.z;
    this.oceanStormMat.uniforms.uTime.value = elapsed;

    this.lightningTimer -= clampedDt;
    if (this.lightningTimer <= 0) {
      this.lightningTimer = 5.0 + ((Math.sin(elapsed * 7.3) * 0.5 + 0.5) * 6.0);
      this.currentLightningFlash = 0.88;
      if (this.onStormThunder) {
        this.onStormThunder(0.8);
      }
    } else {
      this.currentLightningFlash = Math.max(0, this.currentLightningFlash - clampedDt * 3.4);
    }
    this.oceanStormMat.uniforms.uFlash.value = this.currentLightningFlash;

    const posAttr = this.rainPoints.geometry.attributes.position as THREE.BufferAttribute;
    for (let i = 0; i < this.rainCount; i++) {
      const ix = i * 3;
      let x = this.rainPositions[ix] + 8.5 * clampedDt;
      let y = this.rainPositions[ix + 1] - 35.0 * clampedDt;
      let z = this.rainPositions[ix + 2] + 3.2 * clampedDt;

      if (x - focusPos.x > 86) x -= 172;
      if (x - focusPos.x < -86) x += 172;
      if (y < focusPos.y - 16) y += 48;
      if (z - focusPos.z > 82) z -= 164;
      if (z - focusPos.z < -82) z += 164;

      this.rainPositions[ix] = x;
      this.rainPositions[ix + 1] = y;
      this.rainPositions[ix + 2] = z;
    }
    posAttr.needsUpdate = true;
  }
}

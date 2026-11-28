import * as THREE from 'three';
import { ProceduralTextureLibrary } from '../graphics/proceduralTextures';
import { clamp, lerp, mulberry32, smoothstep } from '../math/noise';
import { WorldObstacle } from './trailCorridor';

export type RailcarKind =
  | 'FLATBED_TRANSFER'
  | 'ROOF_RAMP_CAR'
  | 'BOXCAR_DUAL_TIER'
  | 'TANKER_CAR'
  | 'LOG_SPILL_FLATCAR'
  | 'ARMORED_LOCOMOTIVE';

export type TrackDisasterKind = 'DYNAMITED_TRESTLE' | 'ROCKSLIDE_TUNNEL';

export interface RollingLogHazard {
  /** Offset along the flatcar (`-halfLen .. +halfLen`, where `+` is aft/backward!) */
  localZ: number;
  rollAngle: number;
  radius: number;
  halfWidth: number;
  active: boolean;
  mesh: THREE.Mesh;
}

export interface RunawayRailcarInstance {
  trackIdx: number; // 0 = Left (-16m), 1 = Center (0m), 2 = Right (+16m)
  slotIdx: number; // 0..5 within chunk
  kind: RailcarKind;
  /** Base (un-surged) center Z in world coordinates */
  baseZ: number;
  /** Live world center `(liveX, liveY, liveZ)` updated each frame with track surge, curve & bucking! */
  liveX: number;
  liveY: number;
  liveZ: number;
  liveYaw: number;
  liveRoll: number;
  livePitch: number;
  liveSurgeVelZ: number;
  halfWidth: number;
  halfLength: number;
  deckHeightOffset: number; // +1.45m above rail datum
  roofHeightOffset: number; // +4.65m for BOXCAR_DUAL_TIER / LOCOMOTIVE cab
  hasSideJumpLeft: boolean;
  hasSideJumpRight: boolean;
  /** Decoupling gap: if true, this car drifts forward (`-Z`) away from the car behind it! */
  isDecouplingLead: boolean;
  liveDecoupleOffsetZ: number;
  /** Rolling timber logs on LOG_SPILL_FLATCAR */
  rollingLogs: RollingLogHazard[];
  logCyclePhase: number;
  chainWasIntact: boolean;
  /** Periodic scalding steam blast on ARMORED_LOCOMOTIVE */
  steamPhase: number;
  steamBlastActive: boolean;
  steamPlumeMesh: THREE.Mesh | null;
  carGroup: THREE.Group;
  wheelMeshes: THREE.Object3D[];
}

export interface TrackDisasterZone {
  trackIdx: number;
  kind: TrackDisasterKind;
  zStart: number; // upstream (-Z entry)
  zEnd: number; // downstream (-Z exit, more negative)
  centerX: number;
  centerZ: number;
  yaw: number;
  /** For DYNAMITED_TRESTLE: periodic detonation & collapse drop (`0` intact -> `-22m` collapsed!) */
  period: number;
  phase: number;
  isCollapsed: boolean;
  wasCollapsed: boolean;
  collapseDropY: number;
  collapsePitch: number;
  collapsibleGroup: THREE.Group | null;
  blastFlashMesh: THREE.Mesh | null;
  /** For ROCKSLIDE_TUNNEL: solid 240Hz boulder wall blocking the dead-end track! */
  rockWallHalfW: number;
  rockWallHalfL: number;
}

export interface SwingingSignalBoom {
  z: number;
  centerX: number;
  pivotX: number;
  pivotY: number;
  armLength: number;
  phase: number;
  freq: number;
  currentSwingYaw: number;
  angularVelYaw: number;
  tipWorldPos: THREE.Vector3;
  armGroup: THREE.Group;
}

export interface RunawayTrainChunkData {
  group: THREE.Group;
  chunkIndex: number;
  blockedTrackIdx: number;
  railcars: RunawayRailcarInstance[];
  disasters: TrackDisasterZone[];
  signalBooms: SwingingSignalBoom[];
  sampleCourseX: (z: number) => number;
  zStart: number;
  zLength: number;
}

const TRACK_SPACING_X = 14.0; // Close-quarters parallel gorge tracks: West (-14m), Center (0m), East (+14m)
const RAIL_DATUM_Y = 2.0; // Elevated high on 40m trestle bents above the -38m canyon river!
const SLOTS_PER_CHUNK = 6; // 6 railcars * 18.667m = 112.0m chunk!

export class RunawayTrainBuilder {
  public readonly atmosphereFxGroup = new THREE.Group();

  // Materials
  private readonly armorSteelMat: THREE.MeshStandardMaterial;
  private readonly crimsonTrainMat: THREE.MeshStandardMaterial;
  private readonly boxcarOliveMat: THREE.MeshStandardMaterial;
  private readonly tankerSteelMat: THREE.MeshStandardMaterial;
  private readonly flatbedTimberMat: THREE.MeshStandardMaterial;
  private readonly trestleWoodMat: THREE.MeshStandardMaterial;
  private readonly logBarkMat: THREE.MeshStandardMaterial;
  private readonly logEndGrainMat: THREE.MeshStandardMaterial;
  private readonly hazardChevronMat: THREE.MeshStandardMaterial;
  private readonly rockGraniteMat: THREE.MeshStandardMaterial;
  private readonly fireboxGlowMat: THREE.MeshBasicMaterial;
  private readonly headlampGlowMat: THREE.MeshBasicMaterial;
  private readonly dynamiteRedGlowMat: THREE.MeshBasicMaterial;
  private readonly signalGreenMat: THREE.MeshBasicMaterial;
  private readonly steamPlumeMat: THREE.MeshBasicMaterial;
  private readonly gorgeRiverMat: THREE.ShaderMaterial;

  // Shared Geometries
  private readonly unitBoxGeo: THREE.BoxGeometry;
  private readonly wheelCylGeo: THREE.CylinderGeometry;
  private readonly tankerCylGeo: THREE.CylinderGeometry;
  private readonly boilerCylGeo: THREE.CylinderGeometry;
  private readonly logCylGeo: THREE.CylinderGeometry;
  private readonly steamConeGeo: THREE.ConeGeometry;
  private readonly boulderGeo: THREE.IcosahedronGeometry;
  private readonly lanternSphereGeo: THREE.SphereGeometry;

  // Global Canyon Whitewater River & High-Speed Flying Cinders/Sparks
  private riverMesh!: THREE.Mesh;
  private readonly cinderCount = 1100;
  private cinderPoints!: THREE.Points;
  private cinderPositions!: Float32Array;

  // Event Callbacks
  public onTrestleDynamiteBlast: ((pos: THREE.Vector3) => void) | null = null;
  public onLogChainSnap: ((pos: THREE.Vector3) => void) | null = null;
  public onLocomotiveSteamBlast: ((pos: THREE.Vector3) => void) | null = null;
  public onSignalBoomImpact: ((pos: THREE.Vector3) => void) | null = null;

  private readonly _dummy = new THREE.Object3D();
  private readonly _euler = new THREE.Euler();
  private readonly _hitContact = new THREE.Vector3();

  constructor(textures: ProceduralTextureLibrary) {
    this.atmosphereFxGroup.name = 'RunawayTrainsAtmosphereFX';

    this.armorSteelMat = new THREE.MeshStandardMaterial({
      color: 0x242a32,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.65, 0.65),
      roughness: 0.42,
      metalness: 0.78,
    });

    this.crimsonTrainMat = new THREE.MeshStandardMaterial({
      color: 0x9e2a22,
      map: textures.rock.map,
      roughness: 0.45,
      metalness: 0.62,
    });

    this.boxcarOliveMat = new THREE.MeshStandardMaterial({
      color: 0x3f4d3b,
      map: textures.rock.map,
      roughness: 0.54,
      metalness: 0.52,
    });

    this.tankerSteelMat = new THREE.MeshStandardMaterial({
      color: 0x4b5663,
      map: textures.rock.map,
      roughness: 0.34,
      metalness: 0.82,
    });

    this.flatbedTimberMat = new THREE.MeshStandardMaterial({
      color: 0x6e5136,
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      roughness: 0.78,
      metalness: 0.12,
    });

    this.trestleWoodMat = new THREE.MeshStandardMaterial({
      color: 0x4d3723,
      map: textures.bark.map,
      roughness: 0.84,
      metalness: 0.08,
    });

    this.logBarkMat = new THREE.MeshStandardMaterial({
      color: 0x5c3a21,
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      roughness: 0.82,
      metalness: 0.06,
    });

    this.logEndGrainMat = new THREE.MeshStandardMaterial({
      color: 0xc49a68,
      map: textures.bark.map,
      roughness: 0.72,
      metalness: 0.05,
    });

    this.hazardChevronMat = new THREE.MeshStandardMaterial({
      map: this.createRailHazardTexture(),
      roughness: 0.46,
      metalness: 0.42,
    });

    this.rockGraniteMat = new THREE.MeshStandardMaterial({
      color: 0x56524d,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      roughness: 0.86,
      metalness: 0.12,
    });

    this.fireboxGlowMat = new THREE.MeshBasicMaterial({ color: 0xff6a1a });
    this.headlampGlowMat = new THREE.MeshBasicMaterial({ color: 0xfff0c2 });
    this.dynamiteRedGlowMat = new THREE.MeshBasicMaterial({ color: 0xff291c });
    this.signalGreenMat = new THREE.MeshBasicMaterial({ color: 0x38ff88 });

    this.steamPlumeMat = new THREE.MeshBasicMaterial({
      color: 0xeef7ff,
      transparent: true,
      opacity: 0.58,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
      side: THREE.DoubleSide,
    });

    this.unitBoxGeo = new THREE.BoxGeometry(1, 1, 1);

    this.wheelCylGeo = new THREE.CylinderGeometry(0.56, 0.56, 0.24, 14);
    this.wheelCylGeo.rotateZ(Math.PI * 0.5);

    this.tankerCylGeo = new THREE.CylinderGeometry(2.15, 2.15, 1.0, 18);
    this.tankerCylGeo.rotateX(Math.PI * 0.5);

    this.boilerCylGeo = new THREE.CylinderGeometry(1.75, 1.85, 1.0, 16);
    this.boilerCylGeo.rotateX(Math.PI * 0.5);

    // Horizontal timber log aligned along X (`rotateZ(PI/2)`) so it rolls along Z!
    this.logCylGeo = new THREE.CylinderGeometry(1.0, 1.0, 1.0, 14);
    this.logCylGeo.rotateZ(Math.PI * 0.5);

    this.steamConeGeo = new THREE.ConeGeometry(2.4, 8.5, 12, 1, true);
    // Flip cone so narrow nozzle is at bottom `y = 0` and wide plume flares upward `+Y`!
    this.steamConeGeo.rotateX(Math.PI);
    this.steamConeGeo.translate(0, 4.25, 0);

    this.boulderGeo = new THREE.IcosahedronGeometry(1.0, 1);
    this.lanternSphereGeo = new THREE.SphereGeometry(0.32, 10, 8);

    // Roaring Canyon Whitewater River Shader at `y = -36.0m`!
    this.gorgeRiverMat = new THREE.ShaderMaterial({
      uniforms: {
        uTime: { value: 0 },
      },
      vertexShader: /* glsl */ `
        uniform float uTime;
        varying vec3 vWorldPos;
        varying float vRapids;
        void main() {
          vec4 wp = modelMatrix * vec4(position, 1.0);
          float r1 = sin(wp.x * 0.18 + wp.z * 0.14 + uTime * 4.8) * 0.65;
          float r2 = cos(wp.x * 0.26 - wp.z * 0.22 - uTime * 3.9) * 0.42;
          wp.y += r1 + r2;
          vWorldPos = wp.xyz;
          vRapids = clamp((r1 + r2 + 0.35) / 1.2, 0.0, 1.0);
          gl_Position = projectionMatrix * viewMatrix * wp;
        }
      `,
      fragmentShader: /* glsl */ `
        uniform float uTime;
        varying vec3 vWorldPos;
        varying float vRapids;
        void main() {
          vec3 deepCol = vec3(0.05, 0.22, 0.26);
          vec3 glacialTurquoise = vec3(0.14, 0.46, 0.52);
          vec3 whitewater = vec3(0.86, 0.94, 0.98);

          // Fast longitudinal whitewater streaks rushing past at train speed!
          float rush = sin(vWorldPos.x * 0.75 + sin(vWorldPos.z * 0.12 + uTime * 9.5) * 3.0) * 0.5 + 0.5;
          float foam = smoothstep(0.38, 0.88, vRapids + rush * 0.34);

          vec3 col = mix(deepCol, glacialTurquoise, vRapids);
          col = mix(col, whitewater, foam * 0.82);

          float dist = length(vWorldPos.xz - cameraPosition.xz);
          float alpha = 1.0 - smoothstep(135.0, 230.0, dist);
          gl_FragColor = vec4(col, alpha);
        }
      `,
      transparent: true,
      depthWrite: true,
    });

    const riverGeo = new THREE.PlaneGeometry(140, 340, 48, 72);
    riverGeo.rotateX(-Math.PI * 0.5);
    this.riverMesh = new THREE.Mesh(riverGeo, this.gorgeRiverMat);
    this.riverMesh.position.y = -35.5;
    this.riverMesh.frustumCulled = false;
    this.atmosphereFxGroup.add(this.riverMesh);

    // High-Velocity Coal Cinders, Sparks & Gorge Mist streaking backward (`+Z`) at 42 m/s!
    this.cinderPositions = new Float32Array(this.cinderCount * 3);
    const rng = mulberry32(773911);
    for (let i = 0; i < this.cinderCount; i++) {
      const ix = i * 3;
      this.cinderPositions[ix] = (rng() - 0.5) * 96;
      this.cinderPositions[ix + 1] = lerp(-8.0, 24.0, rng());
      this.cinderPositions[ix + 2] = (rng() - 0.5) * 170;
    }
    const cGeo = new THREE.BufferGeometry();
    cGeo.setAttribute('position', new THREE.BufferAttribute(this.cinderPositions, 3));
    const cMat = new THREE.PointsMaterial({
      color: 0xff9a3c,
      size: 0.26,
      transparent: true,
      opacity: 0.82,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });
    this.cinderPoints = new THREE.Points(cGeo, cMat);
    this.cinderPoints.frustumCulled = false;
    this.atmosphereFxGroup.add(this.cinderPoints);
  }

  private createRailHazardTexture(): THREE.CanvasTexture {
    const canvas = document.createElement('canvas');
    canvas.width = 128;
    canvas.height = 128;
    const ctx = canvas.getContext('2d');
    if (ctx) {
      ctx.fillStyle = '#d9981e';
      ctx.fillRect(0, 0, 128, 128);
      ctx.fillStyle = '#1b1e24';
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

  // ===========================================================================
  // PARALLEL GORGE TRACK GEOMETRY, CURVE BANKING & OVERTAKING SURGE
  // ===========================================================================

  /**
   * Deterministic choice of which track (`0`, `1`, or `2`) ends in a Rockslide Tunnel
   * or Dynamited Trestle in `chunkIndex`!
   * - Chunk 0 blocks Track 0 (Left) so Center Track 1 spawn is 100% clear!
   * - Chunk 1 blocks Track 1 (Center) -> FORCES Center drivers to jump sideways onto Left or Right train!
   * - Chunk 2 blocks Track 2 (Right), Chunk 3 blocks Track 0, etc.!
   */
  public getBlockedTrackForChunk(chunkIndex: number): number {
    if (chunkIndex < 0) return -1;
    return ((chunkIndex % 3) + 3) % 3;
  }

  /**
   * Live longitudinal overtaking surge (`surgeZ` and `dSurgeDt`) of each parallel train consist!
   * Shared across all chunks so coupled cars stay seamlessly joined across chunk boundaries
   * while Left, Center, and Right trains visibly overtake and drop back alongside one another!
   */
  public sampleTrackSurge(
    trackIdx: number,
    elapsed: number
  ): { surgeZ: number; surgeVelZ: number } {
    if (trackIdx === 1) {
      const arg = elapsed * 0.65;
      return {
        surgeZ: Math.sin(arg) * 2.2,
        surgeVelZ: Math.cos(arg) * 2.2 * 0.65,
      };
    }
    const freq = trackIdx === 0 ? 0.74 : 0.81;
    const phase = trackIdx === 0 ? 1.15 : 3.85;
    const amp = 8.4;
    const arg = elapsed * freq + phase;
    return {
      surgeZ: Math.sin(arg) * amp,
      surgeVelZ: Math.cos(arg) * amp * freq,
    };
  }

  public sampleTrackPoseAtZ(
    trackIdx: number,
    z: number,
    elapsed: number,
    sampleCourseX: (z: number) => number
  ): {
    x: number;
    y: number;
    yaw: number;
    roll: number;
    pitch: number;
  } {
    const cx = sampleCourseX(z);
    const cxAhead = sampleCourseX(z - 4.0);
    const cxBehind = sampleCourseX(z + 4.0);
    const dxdz = (cxAhead - cxBehind) / -8.0;
    const yaw = Math.atan2(-(cxAhead - cxBehind), 8.0);

    // Second derivative -> authentic railroad superelevation banking into gorge curves!
    const curvature = (cxAhead - 2.0 * cx + cxBehind) / 16.0;
    const curveBank = clamp(curvature * 18.0, -0.13, 0.13);

    // High-speed rail-switch clatter & suspension bucking!
    const switchBuckRoll =
      Math.sin(elapsed * 6.4 + z * 0.13 + trackIdx * 2.1) * 0.038 +
      Math.sin(elapsed * 11.5 + z * 0.29) * 0.014;
    const buckHeave =
      Math.abs(Math.sin(elapsed * 7.8 + z * 0.18 + trackIdx * 1.7)) * 0.16;
    const buckPitch = Math.sin(elapsed * 5.2 + z * 0.11 + trackIdx) * 0.016;

    const latOffset = (trackIdx - 1) * TRACK_SPACING_X;
    // Perpendicular offset along gorge normal so parallel tracks maintain exact spacing around curves!
    const normScale = 1.0 / Math.hypot(1.0, dxdz);

    return {
      x: cx + latOffset * normScale,
      y: RAIL_DATUM_Y + buckHeave,
      yaw,
      roll: curveBank + switchBuckRoll,
      pitch: buckPitch,
    };
  }

  public findNearestTrainSpawn(
    x: number,
    z: number,
    sampleCourseX: (z: number) => number
  ): { x: number; y: number; z: number; yaw: number } {
    const chunkIdx = Math.max(0, Math.floor(-z / 112.0));
    const blocked = this.getBlockedTrackForChunk(chunkIdx);
    const nextBlocked = this.getBlockedTrackForChunk(chunkIdx + 1);

    let bestTrack = 1;
    let bestDist = Infinity;
    const cx = sampleCourseX(z);

    for (let t = 0; t < 3; t++) {
      if (t === blocked || t === nextBlocked) continue;
      const tx = cx + (t - 1) * TRACK_SPACING_X;
      const d = Math.abs(x - tx);
      if (d < bestDist) {
        bestDist = d;
        bestTrack = t;
      }
    }

    const slotLen = 112.0 / SLOTS_PER_CHUNK;
    const snappedZ = -(Math.floor(-z / slotLen) + 0.5) * slotLen;
    const finalZ = Math.min(-10.0, snappedZ);
    const pose = this.sampleTrackPoseAtZ(bestTrack, finalZ, 0, sampleCourseX);

    return {
      x: pose.x,
      y: pose.y + 1.65,
      z: finalZ,
      yaw: pose.yaw,
    };
  }

  // ===========================================================================
  // CHUNK POPULATION — 3 PARALLEL TRESTLES, RUNAWAY CONSISTS & TRACK DISASTERS
  // ===========================================================================

  public populateRunawayTrainChunk(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    zLength: number,
    sampleCourseX: (z: number) => number,
    _obstacles: WorldObstacle[]
  ): RunawayTrainChunkData {
    const group = new THREE.Group();
    group.name = `RunawayTrainChunk_${chunkIndex}`;
    chunkGroup.add(group);

    const railcars: RunawayRailcarInstance[] = [];
    const disasters: TrackDisasterZone[] = [];
    const signalBooms: SwingingSignalBoom[] = [];

    const blockedTrackIdx = this.getBlockedTrackForChunk(chunkIndex);
    const prevBlockedTrackIdx = this.getBlockedTrackForChunk(chunkIndex - 1);
    const slotLen = zLength / SLOTS_PER_CHUNK; // 18.667m

    // 1. Build Elevated Timber-and-Iron Trestle Bridges + Steel Rails & Crossties for all 3 Tracks!
    this.buildParallelGorgeTrestles(
      group,
      chunkIndex,
      zStart,
      zLength,
      blockedTrackIdx,
      sampleCourseX,
      disasters
    );

    // 2. Populate Coupled Runaway Armored Freight Consists on all 3 Parallel Tracks!
    for (let tIdx = 0; tIdx < 3; tIdx++) {
      for (let s = 0; s < SLOTS_PER_CHUNK; s++) {
        // Skip the disaster cavern/collapse zone (`slots 4 & 5`) on `blockedTrackIdx`,
        // AND skip `slot 0` right downstream of `prevBlockedTrackIdx`'s rockslide/collapsed bridge so trains look authentically separated!
        if (chunkIndex >= 0 && tIdx === blockedTrackIdx && s >= 4) {
          continue;
        }
        if (chunkIndex >= 1 && tIdx === prevBlockedTrackIdx && s === 0) {
          continue;
        }

        const baseZ = zStart - (s + 0.5) * slotLen;
        const kind = this.chooseRailcarKind(chunkIndex, tIdx, s, blockedTrackIdx);

        // Give flatcars/tankers canted Side-Transfer Jump Wings whenever an adjacent track has a train!
        const hasSideJumpLeft = tIdx > 0;
        const hasSideJumpRight = tIdx < 2;

        // Select non-spawn mid-consist boxcar/flatcar pairs to DECOUPLE & DRIFT APART mid-jump!
        const isDecouplingLead =
          chunkIndex >= 0 &&
          tIdx !== blockedTrackIdx &&
          s === 3 &&
          (chunkIndex + tIdx) % 2 === 1;

        const carInst = this.buildRunawayRailcar(
          group,
          chunkIndex,
          tIdx,
          s,
          kind,
          baseZ,
          slotLen,
          hasSideJumpLeft,
          hasSideJumpRight,
          isDecouplingLead,
          sampleCourseX
        );
        railcars.push(carInst);
      }
    }

    // 3. Build Overhead Iron Railroad Signal Gantry with Swinging Semaphore / Mail-Hook Boom!
    if (chunkIndex >= 0) {
      const boomZ = zStart - zLength * 0.82;
      signalBooms.push(
        this.buildSwingingSignalGantry(group, chunkIndex, boomZ, sampleCourseX)
      );
    }

    // Warm Locomotive Headlamp Pool lighting the parallel tracks!
    const lampZ = zStart - zLength * 0.5;
    const headLight = new THREE.PointLight(0xffd796, 22.0, 72.0, 1.3);
    headLight.position.set(sampleCourseX(lampZ), RAIL_DATUM_Y + 12.0, lampZ);
    group.add(headLight);

    return {
      group,
      chunkIndex,
      blockedTrackIdx,
      railcars,
      disasters,
      signalBooms,
      sampleCourseX,
      zStart,
      zLength,
    };
  }

  private chooseRailcarKind(
    chunkIndex: number,
    trackIdx: number,
    slotIdx: number,
    blockedTrackIdx: number
  ): RailcarKind {
    // Spawn car on Center Track (`chunkIndex === 0, trackIdx === 1, slotIdx <= 1`) is a clear, wide Armored Flatbed!
    if (chunkIndex === 0 && trackIdx === 1 && slotIdx <= 1) {
      return 'FLATBED_TRANSFER';
    }

    // On the doomed `blockedTrackIdx`, Slot 3 is the Lead Armored Dreadnought Locomotive (with Steam Blast!)
    // and Slot 2 right behind it is a Side-Jump Transfer Flatcar so players can leap sideways onto the neighboring train!
    if (chunkIndex >= 0 && trackIdx === blockedTrackIdx) {
      if (slotIdx === 3) return 'ARMORED_LOCOMOTIVE';
      if (slotIdx === 2) return 'FLATBED_TRANSFER';
      if (slotIdx === 1) return 'LOG_SPILL_FLATCAR';
      return 'FLATBED_TRANSFER';
    }

    // On parallel surviving tracks:
    // - Slots 0 -> 1 form a coupled `ROOF_RAMP_CAR` -> `BOXCAR_DUAL_TIER` sequence (drive OVER roof OR INSIDE open doors!)
    // - Slots 2 -> 3 form a wide-open `FLATBED_TRANSFER` + `TANKER_CAR` landing zone right alongside the doomed track's jump window!
    // - Slots 4 -> 5 feature Rolling-Log Flatcars, Armored Steam Locomotives & Secondary Dual-Tier Boxcars!
    if (slotIdx === 0) return 'ROOF_RAMP_CAR';
    if (slotIdx === 1) return 'BOXCAR_DUAL_TIER';
    if (slotIdx === 2) return 'FLATBED_TRANSFER';
    if (slotIdx === 3) return 'TANKER_CAR';
    if (slotIdx === 4) {
      return (chunkIndex + trackIdx) % 2 === 0 ? 'LOG_SPILL_FLATCAR' : 'ROOF_RAMP_CAR';
    }
    return (chunkIndex + trackIdx) % 2 === 0 ? 'ARMORED_LOCOMOTIVE' : 'BOXCAR_DUAL_TIER';
  }

  // ===========================================================================
  // 3D TIMBER TRESTLES, DYNAMITED TRESTLE COLLAPSE & ROCKSLIDE TUNNEL PORTALS
  // ===========================================================================

  private buildParallelGorgeTrestles(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    zLength: number,
    blockedTrackIdx: number,
    sampleCourseX: (z: number) => number,
    disasters: TrackDisasterZone[]
  ): void {
    const slotLen = zLength / SLOTS_PER_CHUNK;
    const disasterZStart = zStart - 4.0 * slotLen; // start of Slot 4
    const disasterZEnd = zStart - zLength; // end of Slot 5

    const timberMats: THREE.Matrix4[] = [];
    const steelRailMats: THREE.Matrix4[] = [];

    const bentStepZ = 9.333; // 12 trestle bents per 112m chunk
    const bentCount = Math.round(zLength / bentStepZ);

    for (let tIdx = 0; tIdx < 3; tIdx++) {
      for (let b = 0; b < bentCount; b++) {
        const zMid = zStart - (b + 0.5) * bentStepZ;
        const inDynamitedSpan =
          chunkIndex >= 0 &&
          chunkIndex % 2 === 0 &&
          tIdx === blockedTrackIdx &&
          zMid <= disasterZStart &&
          zMid >= disasterZEnd;

        // Dynamited trestle bents live inside `collapsibleGroup` so they visually collapse into the gorge!
        if (inDynamitedSpan) continue;

        const pose = this.sampleTrackPoseAtZ(tIdx, zMid, 0, sampleCourseX);
        const legH = 39.0;

        // Left & Right battered timber trestle legs + cross-brace reaching down to y = -37m!
        for (const side of [-1, 1]) {
          this._dummy.position.set(
            pose.x + side * 2.1,
            RAIL_DATUM_Y - legH * 0.5,
            zMid
          );
          this._dummy.rotation.set(0, pose.yaw, side * -0.055);
          this._dummy.scale.set(0.55, legH, 0.55);
          this._dummy.updateMatrix();
          timberMats.push(this._dummy.matrix.clone());
        }

        // Trestle cap beam + stringer deck
        this._dummy.position.set(pose.x, RAIL_DATUM_Y - 0.35, zMid);
        this._dummy.rotation.set(0, pose.yaw, 0);
        this._dummy.scale.set(5.4, 0.55, bentStepZ + 0.25);
        this._dummy.updateMatrix();
        timberMats.push(this._dummy.matrix.clone());

        // Twin heavy-gauge steel rails (`1.435m` / armored broad gauge `2.6m`)
        for (const rSide of [-1.35, 1.35]) {
          this._dummy.position.set(pose.x + rSide, RAIL_DATUM_Y + 0.08, zMid);
          this._dummy.rotation.set(0, pose.yaw, 0);
          this._dummy.scale.set(0.18, 0.22, bentStepZ + 0.15);
          this._dummy.updateMatrix();
          steelRailMats.push(this._dummy.matrix.clone());
        }
      }
    }

    if (timberMats.length > 0) {
      const tIm = new THREE.InstancedMesh(
        this.unitBoxGeo,
        this.trestleWoodMat,
        timberMats.length
      );
      for (let i = 0; i < timberMats.length; i++) tIm.setMatrixAt(i, timberMats[i]);
      tIm.instanceMatrix.needsUpdate = true;
      tIm.castShadow = true;
      tIm.receiveShadow = true;
      chunkGroup.add(tIm);
    }

    if (steelRailMats.length > 0) {
      const rIm = new THREE.InstancedMesh(
        this.unitBoxGeo,
        this.armorSteelMat,
        steelRailMats.length
      );
      for (let i = 0; i < steelRailMats.length; i++) rIm.setMatrixAt(i, steelRailMats[i]);
      rIm.instanceMatrix.needsUpdate = true;
      chunkGroup.add(rIm);
    }

    // Build the Track Closure Disaster (`DYNAMITED_TRESTLE` or `ROCKSLIDE_TUNNEL`) on `blockedTrackIdx`!
    if (chunkIndex >= 0 && blockedTrackIdx >= 0) {
      const midZ = (disasterZStart + disasterZEnd) * 0.5;
      const spanLen = disasterZStart - disasterZEnd;
      const pose = this.sampleTrackPoseAtZ(blockedTrackIdx, midZ, 0, sampleCourseX);
      const isDynamite = chunkIndex % 2 === 0;

      if (isDynamite) {
        // DYNAMITED WOODEN TRESTLE BRIDGE (`collapsibleGroup` buckles & drops into the canyon!)
        const collapsibleGroup = new THREE.Group();
        collapsibleGroup.position.set(pose.x, RAIL_DATUM_Y, midZ);
        collapsibleGroup.rotation.y = pose.yaw;
        chunkGroup.add(collapsibleGroup);

        const deck = new THREE.Mesh(this.unitBoxGeo, this.trestleWoodMat);
        deck.position.set(0, -0.35, 0);
        deck.scale.set(5.6, 0.6, spanLen);
        deck.castShadow = true;
        collapsibleGroup.add(deck);

        for (let b = -1; b <= 1; b++) {
          for (const side of [-1, 1]) {
            const leg = new THREE.Mesh(this.unitBoxGeo, this.trestleWoodMat);
            leg.position.set(side * 2.1, -14.0, b * 11.0);
            leg.scale.set(0.55, 28.0, 0.55);
            collapsibleGroup.add(leg);
          }
          // Glowing Red Dynamite Charge Bundles strapped to the timber bents!
          const dynCharge = new THREE.Mesh(this.lanternSphereGeo, this.dynamiteRedGlowMat);
          dynCharge.position.set(0, 0.55, b * 10.5);
          dynCharge.scale.setScalar(2.2);
          collapsibleGroup.add(dynCharge);
        }

        // Warning Railroad Crossbuck Barrier at upstream lip!
        const crossbuck = new THREE.Mesh(this.unitBoxGeo, this.hazardChevronMat);
        crossbuck.position.set(0, 1.35, spanLen * 0.46);
        crossbuck.scale.set(5.2, 0.75, 0.45);
        collapsibleGroup.add(crossbuck);

        const blastFlashMesh = new THREE.Mesh(this.lanternSphereGeo, this.fireboxGlowMat);
        blastFlashMesh.position.set(pose.x, RAIL_DATUM_Y + 2.5, midZ);
        blastFlashMesh.scale.setScalar(0.01);
        blastFlashMesh.visible = false;
        chunkGroup.add(blastFlashMesh);

        disasters.push({
          trackIdx: blockedTrackIdx,
          kind: 'DYNAMITED_TRESTLE',
          zStart: disasterZStart,
          zEnd: disasterZEnd,
          centerX: pose.x,
          centerZ: midZ,
          yaw: pose.yaw,
          period: 7.6,
          phase: chunkIndex * 2.1,
          isCollapsed: false,
          wasCollapsed: false,
          collapseDropY: 0,
          collapsePitch: 0,
          collapsibleGroup,
          blastFlashMesh,
          rockWallHalfW: 0,
          rockWallHalfL: 0,
        });
      } else {
        // DEAD-END ROCKSLIDE MOUNTAIN TUNNEL PORTAL (`240Hz` solid granite boulder wall!)
        const portalGroup = new THREE.Group();
        portalGroup.position.set(pose.x, RAIL_DATUM_Y, midZ);
        portalGroup.rotation.y = pose.yaw;
        chunkGroup.add(portalGroup);

        // Towering Stone & Timber Tunnel Portal Arch (`11m` wide x `11.5m` tall)
        for (const side of [-1, 1]) {
          const pier = new THREE.Mesh(this.unitBoxGeo, this.rockGraniteMat);
          pier.position.set(side * 4.2, 5.4, spanLen * 0.32);
          pier.scale.set(2.4, 11.2, 7.5);
          pier.castShadow = true;
          portalGroup.add(pier);
        }
        const lintel = new THREE.Mesh(this.unitBoxGeo, this.rockGraniteMat);
        lintel.position.set(0, 10.2, spanLen * 0.32);
        lintel.scale.set(11.2, 2.8, 8.0);
        lintel.castShadow = true;
        portalGroup.add(lintel);

        // Warning Hazard Crossbuck + Red Danger Lanterns!
        const bar = new THREE.Mesh(this.unitBoxGeo, this.hazardChevronMat);
        bar.position.set(0, 1.5, spanLen * 0.45);
        bar.scale.set(6.2, 0.85, 0.45);
        portalGroup.add(bar);

        for (const side of [-2.6, 2.6]) {
          const redLamp = new THREE.Mesh(this.lanternSphereGeo, this.dynamiteRedGlowMat);
          redLamp.position.set(side, 2.4, spanLen * 0.46);
          redLamp.scale.setScalar(1.85);
          portalGroup.add(redLamp);
        }

        // Choked Caved-In Granite Rockslide Pile sealing the tunnel mouth!
        const rng = mulberry32(chunkIndex * 49123 + 811);
        for (let r = 0; r < 14; r++) {
          const rock = new THREE.Mesh(this.boulderGeo, this.rockGraniteMat);
          const rx = (rng() - 0.5) * 6.8;
          const ry = 1.1 + rng() * 6.8;
          const rz = spanLen * 0.25 + (rng() - 0.5) * 5.2;
          rock.position.set(rx, ry, rz);
          rock.rotation.set(rng() * 3, rng() * 3, rng() * 3);
          rock.scale.set(
            1.6 + rng() * 1.6,
            1.4 + rng() * 1.5,
            1.6 + rng() * 1.6
          );
          rock.castShadow = true;
          portalGroup.add(rock);
        }

        disasters.push({
          trackIdx: blockedTrackIdx,
          kind: 'ROCKSLIDE_TUNNEL',
          zStart: disasterZStart,
          zEnd: disasterZEnd,
          centerX: pose.x,
          centerZ: midZ + spanLen * 0.26,
          yaw: pose.yaw,
          period: 1,
          phase: 0,
          isCollapsed: true,
          wasCollapsed: true,
          collapseDropY: 0,
          collapsePitch: 0,
          collapsibleGroup: null,
          blastFlashMesh: null,
          rockWallHalfW: 4.8,
          rockWallHalfL: 6.2,
        });
      }
    }
  }

  // ===========================================================================
  // 3D ARMORED RAILCAR BUILDER (LOCOMOTIVES, BOXCARS, TANKERS & LOG FLATCARS)
  // ===========================================================================

  private buildRunawayRailcar(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    trackIdx: number,
    slotIdx: number,
    kind: RailcarKind,
    baseZ: number,
    slotLen: number,
    hasSideJumpLeft: boolean,
    hasSideJumpRight: boolean,
    isDecouplingLead: boolean,
    sampleCourseX: (z: number) => number
  ): RunawayRailcarInstance {
    const carGroup = new THREE.Group();
    const initPose = this.sampleTrackPoseAtZ(trackIdx, baseZ, 0, sampleCourseX);
    carGroup.position.set(initPose.x, initPose.y, baseZ);
    chunkGroup.add(carGroup);

    const halfWidth = 3.35; // 6.7m wide heavy-gauge armored railcar deck!
    const arcScale = 1.0 / Math.max(0.84, Math.cos(initPose.yaw));
    const halfLength = (slotLen * 0.5 - 0.32) * arcScale; // Scales with curve arc-length so coupled cars never gap on gorge bends!
    const deckHeightOffset = 1.45;
    const roofHeightOffset = 4.65;

    // 1. Heavy Riveted Steel Underframe + Front/Rear Articulated Gangway Coupler Plates!
    const underframe = new THREE.Mesh(this.unitBoxGeo, this.armorSteelMat);
    underframe.position.set(0, deckHeightOffset - 0.34, 0);
    underframe.scale.set(halfWidth * 1.92, 0.58, halfLength * 2.0);
    underframe.castShadow = true;
    carGroup.add(underframe);

    const couplerPlate = new THREE.Mesh(this.unitBoxGeo, this.armorSteelMat);
    couplerPlate.position.set(0, deckHeightOffset - 0.08, 0);
    couplerPlate.scale.set(3.8, 0.16, slotLen + 0.35);
    carGroup.add(couplerPlate);

    // 2. Fore & Aft Dual-Axle Bogie Trucks with 8 Spinning Iron Flanged Wheels!
    const wheelMeshes: THREE.Object3D[] = [];
    for (const zBogie of [-halfLength * 0.68, halfLength * 0.68]) {
      for (const zAxle of [-0.85, 0.85]) {
        const axleGroup = new THREE.Group();
        axleGroup.position.set(0, 0.56, zBogie + zAxle);
        for (const xSide of [-1.42, 1.42]) {
          const wMesh = new THREE.Mesh(this.wheelCylGeo, this.tankerSteelMat);
          wMesh.position.set(xSide, 0, 0);
          axleGroup.add(wMesh);
        }
        carGroup.add(axleGroup);
        wheelMeshes.push(axleGroup);
      }
    }

    // 3. Canted Side-Transfer Jump Wings (`+1.05m` lift across `1.45m` wing) for Train-to-Train Leaps!
    const buildSideJumpWing = (sideSign: number) => {
      for (const zWing of [-halfLength * 0.42, halfLength * 0.42]) {
        const wing = new THREE.Mesh(this.unitBoxGeo, this.hazardChevronMat);
        wing.position.set(sideSign * (halfWidth + 0.65), deckHeightOffset + 0.44, zWing);
        wing.rotation.z = sideSign * 0.42;
        wing.scale.set(1.65, 0.22, 6.2);
        wing.castShadow = true;
        carGroup.add(wing);
      }
    };
    if (
      hasSideJumpLeft &&
      (kind === 'FLATBED_TRANSFER' || kind === 'TANKER_CAR' || kind === 'ROOF_RAMP_CAR')
    ) {
      buildSideJumpWing(-1);
    }
    if (
      hasSideJumpRight &&
      (kind === 'FLATBED_TRANSFER' || kind === 'TANKER_CAR' || kind === 'ROOF_RAMP_CAR')
    ) {
      buildSideJumpWing(1);
    }

    const rollingLogs: RollingLogHazard[] = [];
    let steamPlumeMesh: THREE.Mesh | null = null;

    // 4. Archetype-Specific Superstructure!
    if (kind === 'FLATBED_TRANSFER') {
      const deck = new THREE.Mesh(this.unitBoxGeo, this.flatbedTimberMat);
      deck.position.set(0, deckHeightOffset - 0.04, 0);
      deck.scale.set(halfWidth * 2.0, 0.18, halfLength * 2.0);
      deck.receiveShadow = true;
      carGroup.add(deck);
    } else if (kind === 'ROOF_RAMP_CAR') {
      // Sloped Steel Boarding Ramp climbing from `deckHeightOffset (+1.45m)` at `+halfLength` (aft)
      // up to `roofHeightOffset (+4.65m)` at `-halfLength` (fore) right onto the Boxcar Roof ahead!
      const flatDeck = new THREE.Mesh(this.unitBoxGeo, this.flatbedTimberMat);
      flatDeck.position.set(0, deckHeightOffset - 0.04, 0);
      flatDeck.scale.set(halfWidth * 2.0, 0.18, halfLength * 2.0);
      carGroup.add(flatDeck);

      const rise = roofHeightOffset - deckHeightOffset;
      const run = halfLength * 1.88;
      const rampLen = Math.hypot(run, rise);
      const rampPitch = Math.atan2(rise, run);
      const rampMesh = new THREE.Mesh(this.unitBoxGeo, this.hazardChevronMat);
      rampMesh.position.set(0, (deckHeightOffset + roofHeightOffset) * 0.5 - 0.1, 0);
      rampMesh.rotation.x = rampPitch;
      rampMesh.scale.set(5.4, 0.28, rampLen);
      rampMesh.castShadow = true;
      rampMesh.receiveShadow = true;
      carGroup.add(rampMesh);
    } else if (kind === 'BOXCAR_DUAL_TIER') {
      // Open-Ended Armored Boxcar: Drive INSIDE (`y = +1.45m`, `5.1m` clear width) OR ON ROOF (`y = +4.65m`)!
      const floor = new THREE.Mesh(this.unitBoxGeo, this.flatbedTimberMat);
      floor.position.set(0, deckHeightOffset - 0.04, 0);
      floor.scale.set(halfWidth * 2.0, 0.18, halfLength * 2.0);
      floor.receiveShadow = true;
      carGroup.add(floor);

      const wallH = roofHeightOffset - deckHeightOffset;
      const wallThick = 0.34;
      const clearHalfW = 2.58;

      for (const side of [-1, 1]) {
        // Fore & Aft wall panels with a wide open central sliding side-door gap (`6.4m` wide) in the middle!
        for (const zSeg of [-halfLength * 0.66, halfLength * 0.66]) {
          const panel = new THREE.Mesh(this.unitBoxGeo, this.boxcarOliveMat);
          panel.position.set(
            side * (clearHalfW + wallThick * 0.5),
            deckHeightOffset + wallH * 0.5,
            zSeg
          );
          panel.scale.set(wallThick, wallH, halfLength * 0.66);
          panel.castShadow = true;
          carGroup.add(panel);
        }
      }

      const roof = new THREE.Mesh(this.unitBoxGeo, this.crimsonTrainMat);
      roof.position.set(0, roofHeightOffset - 0.14, 0);
      roof.scale.set((clearHalfW + wallThick) * 2.04, 0.28, halfLength * 2.04);
      roof.castShadow = true;
      roof.receiveShadow = true;
      carGroup.add(roof);

      const catwalk = new THREE.Mesh(this.unitBoxGeo, this.flatbedTimberMat);
      catwalk.position.set(0, roofHeightOffset + 0.03, 0);
      catwalk.scale.set(2.8, 0.08, slotLen + 0.2);
      carGroup.add(catwalk);

      const cageLight = new THREE.Mesh(this.unitBoxGeo, this.headlampGlowMat);
      cageLight.position.set(0, roofHeightOffset - 0.34, 0);
      cageLight.scale.set(0.45, 0.12, halfLength * 1.4);
      carGroup.add(cageLight);
    } else if (kind === 'TANKER_CAR') {
      // Cylindrical Pressurized Steel Tanker cradled under a wide drivable top deck (`+1.45m` side decks + central catwalk)!
      const deck = new THREE.Mesh(this.unitBoxGeo, this.armorSteelMat);
      deck.position.set(0, deckHeightOffset - 0.04, 0);
      deck.scale.set(halfWidth * 2.0, 0.18, halfLength * 2.0);
      carGroup.add(deck);

      // Sleek low-slung twin side pressure tanks leaving a wide `5.4m` drivable upper armored deck!
      const tank = new THREE.Mesh(this.tankerCylGeo, this.tankerSteelMat);
      tank.position.set(0, deckHeightOffset - 0.55, 0);
      tank.scale.set(1.25, 0.55, halfLength * 1.78);
      tank.castShadow = true;
      carGroup.add(tank);

      const stripe = new THREE.Mesh(this.unitBoxGeo, this.hazardChevronMat);
      stripe.position.set(0, deckHeightOffset + 0.06, 0);
      stripe.scale.set(0.75, 0.04, halfLength * 1.9);
      carGroup.add(stripe);
    } else if (kind === 'LOG_SPILL_FLATCAR') {
      // Flatbed Timber Car with Snapping Chains & Rolling Giant Redwood Logs!
      const deck = new THREE.Mesh(this.unitBoxGeo, this.flatbedTimberMat);
      deck.position.set(0, deckHeightOffset - 0.04, 0);
      deck.scale.set(halfWidth * 2.0, 0.18, halfLength * 2.0);
      deck.receiveShadow = true;
      carGroup.add(deck);

      // Side stake pockets
      for (const side of [-1, 1]) {
        for (let st = -2; st <= 2; st++) {
          const stake = new THREE.Mesh(this.unitBoxGeo, this.armorSteelMat);
          stake.position.set(side * (halfWidth - 0.14), deckHeightOffset + 0.55, st * 3.4);
          stake.scale.set(0.18, 1.1, 0.18);
          carGroup.add(stake);
        }
      }

      // 2 Spilling Rolling Timber Logs (`radius = 0.58m`, `width = 4.9m`)!
      for (let l = 0; l < 2; l++) {
        const radius = 0.56;
        const logHalfW = 2.35;
        const logMesh = new THREE.Mesh(this.logCylGeo, this.logBarkMat);
        logMesh.scale.set(logHalfW * 2.0, radius, radius);
        logMesh.position.set(0, deckHeightOffset + radius, -halfLength * 0.5 + l * 4.2);
        logMesh.castShadow = true;
        carGroup.add(logMesh);

        rollingLogs.push({
          localZ: -halfLength * 0.55 + l * 4.5,
          rollAngle: 0,
          radius,
          halfWidth: logHalfW,
          active: true,
          mesh: logMesh,
        });
      }
    } else {
      // ARMORED_LOCOMOTIVE — Sloped Drivable Boiler Ramp, Glowing Firebox, Cowcatcher & Steam Blast!
      const deck = new THREE.Mesh(this.unitBoxGeo, this.armorSteelMat);
      deck.position.set(0, deckHeightOffset - 0.04, 0);
      deck.scale.set(halfWidth * 2.0, 0.22, halfLength * 2.0);
      carGroup.add(deck);

      // Sloped Armored Cowcatcher Plow at forward nose (`-halfLength`)!
      const plow = new THREE.Mesh(this.unitBoxGeo, this.crimsonTrainMat);
      plow.position.set(0, 0.85, -halfLength - 0.55);
      plow.rotation.x = 0.52;
      plow.scale.set(halfWidth * 1.85, 1.35, 1.4);
      carGroup.add(plow);

      // Glowing Headlamp on Nose!
      const headlamp = new THREE.Mesh(this.lanternSphereGeo, this.headlampGlowMat);
      headlamp.position.set(0, deckHeightOffset + 1.95, -halfLength + 0.4);
      headlamp.scale.setScalar(2.1);
      carGroup.add(headlamp);

      // Drivable Sloped Armored Hood / Boiler (`aft +halfLength` at `+1.45m` -> `cab` at `+3.65m` -> `nose` launch lip!)
      const boiler = new THREE.Mesh(this.boilerCylGeo, this.armorSteelMat);
      boiler.position.set(0, deckHeightOffset + 1.1, -1.2);
      boiler.scale.set(1.35, 0.95, halfLength * 1.45);
      boiler.castShadow = true;
      carGroup.add(boiler);

      // Glowing Orange Furnace Firebox Vents along sides!
      for (const side of [-1, 1]) {
        const firebox = new THREE.Mesh(this.unitBoxGeo, this.fireboxGlowMat);
        firebox.position.set(side * 2.42, deckHeightOffset + 0.65, 2.2);
        firebox.scale.set(0.14, 0.55, 3.8);
        carGroup.add(firebox);
      }

      // Twin Smokestacks + Periodic High-Pressure Scalding Steam Blast Plume!
      const stack = new THREE.Mesh(this.wheelCylGeo, this.crimsonTrainMat);
      stack.rotation.z = Math.PI * 0.5;
      stack.position.set(0, deckHeightOffset + 2.55, -halfLength * 0.52);
      stack.scale.set(1.4, 3.2, 1.4);
      carGroup.add(stack);

      steamPlumeMesh = new THREE.Mesh(this.steamConeGeo, this.steamPlumeMat);
      steamPlumeMesh.position.set(0, deckHeightOffset + 2.2, -halfLength * 0.45);
      steamPlumeMesh.visible = false;
      carGroup.add(steamPlumeMesh);
    }

    // If `isDecouplingLead`, add a bright yellow-black launch kick-plate on the aft edge!
    if (isDecouplingLead) {
      const kick = new THREE.Mesh(this.unitBoxGeo, this.hazardChevronMat);
      kick.position.set(0, deckHeightOffset + 0.28, halfLength + 0.4);
      kick.rotation.x = -0.24;
      kick.scale.set(5.2, 0.22, 1.8);
      carGroup.add(kick);
    }

    return {
      trackIdx,
      slotIdx,
      kind,
      baseZ,
      liveX: initPose.x,
      liveY: initPose.y,
      liveZ: baseZ,
      liveYaw: initPose.yaw,
      liveRoll: initPose.roll,
      livePitch: initPose.pitch,
      liveSurgeVelZ: 0,
      halfWidth,
      halfLength,
      deckHeightOffset,
      roofHeightOffset,
      hasSideJumpLeft,
      hasSideJumpRight,
      isDecouplingLead,
      liveDecoupleOffsetZ: 0,
      rollingLogs,
      logCyclePhase: chunkIndex * 1.7 + trackIdx * 2.3 + slotIdx * 0.9,
      chainWasIntact: true,
      steamPhase: chunkIndex * 1.9 + trackIdx * 2.7,
      steamBlastActive: false,
      steamPlumeMesh,
      carGroup,
      wheelMeshes,
    };
  }

  // ===========================================================================
  // OVERHEAD IRON RAILROAD SIGNAL GANTRY WITH SWINGING BOOM (`SwingingSignalBoom`)
  // ===========================================================================

  private buildSwingingSignalGantry(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    z: number,
    sampleCourseX: (z: number) => number
  ): SwingingSignalBoom {
    const cx = sampleCourseX(z);
    const gantrySpanHalfX = 25.5;
    const beamY = RAIL_DATUM_Y + 9.4;

    const gantryGroup = new THREE.Group();
    gantryGroup.position.set(cx, 0, z);
    chunkGroup.add(gantryGroup);

    // Left & Right Iron Signal Bridge Towers straddling all 3 parallel tracks!
    for (const side of [-1, 1]) {
      const tower = new THREE.Mesh(this.unitBoxGeo, this.armorSteelMat);
      tower.position.set(side * gantrySpanHalfX, (beamY + 34.0) * 0.5 - 34.0, 0);
      tower.scale.set(1.4, beamY + 34.0, 1.4);
      tower.castShadow = true;
      gantryGroup.add(tower);
    }

    const crossBridge = new THREE.Mesh(this.unitBoxGeo, this.armorSteelMat);
    crossBridge.position.set(0, beamY, 0);
    crossBridge.scale.set(gantrySpanHalfX * 2.04, 1.15, 1.25);
    gantryGroup.add(crossBridge);

    // Track Signal Lanterns above Left, Center, Right tracks!
    const blocked = this.getBlockedTrackForChunk(chunkIndex);
    for (let t = 0; t < 3; t++) {
      const sig = new THREE.Mesh(
        this.lanternSphereGeo,
        t === blocked ? this.dynamiteRedGlowMat : this.signalGreenMat
      );
      sig.position.set((t - 1) * TRACK_SPACING_X, beamY - 0.95, 0.75);
      sig.scale.setScalar(1.9);
      gantryGroup.add(sig);
    }

    // Swinging Counterweighted Mail-Hook / Signal Crane Boom sweeping across the upper boxcar roof height (`y = RAIL_DATUM_Y + 5.65m = 7.65m`)!
    const sideSign = chunkIndex % 2 === 0 ? -1 : 1;
    const pivotLocalX = sideSign * 16.0;
    const pivotY = RAIL_DATUM_Y + 5.65;
    const armLength = 15.5;

    const armGroup = new THREE.Group();
    armGroup.position.set(cx + pivotLocalX, pivotY, z);
    chunkGroup.add(armGroup);

    const dropPost = new THREE.Mesh(this.unitBoxGeo, this.armorSteelMat);
    dropPost.position.set(0, (beamY - pivotY) * 0.5, 0);
    dropPost.scale.set(0.65, beamY - pivotY, 0.65);
    armGroup.add(dropPost);

    const boomBar = new THREE.Mesh(this.unitBoxGeo, this.hazardChevronMat);
    boomBar.position.set(0, 0, armLength * 0.5);
    boomBar.scale.set(0.55, 0.65, armLength);
    boomBar.castShadow = true;
    armGroup.add(boomBar);

    const tipWeight = new THREE.Mesh(this.lanternSphereGeo, this.dynamiteRedGlowMat);
    tipWeight.position.set(0, 0, armLength);
    tipWeight.scale.setScalar(1.8);
    armGroup.add(tipWeight);

    return {
      z,
      centerX: cx,
      pivotX: cx + pivotLocalX,
      pivotY,
      armLength,
      phase: chunkIndex * 1.85,
      freq: 1.35,
      currentSwingYaw: 0,
      angularVelYaw: 0,
      tipWorldPos: new THREE.Vector3(cx + pivotLocalX, pivotY, z),
      armGroup,
    };
  }

  // ===========================================================================
  // LIVE SURFACE HEIGHT, TRAIN SURGE VELOCITY & 240HZ HAZARD COLLISIONS
  // ===========================================================================

  private worldToCarLocalXZ(
    car: RunawayRailcarInstance,
    wx: number,
    wz: number
  ): { lx: number; lz: number } {
    const dx = wx - car.liveX;
    const dz = wz - car.liveZ;
    const cosY = Math.cos(car.liveYaw);
    const sinY = Math.sin(car.liveYaw);
    return {
      lx: dx * cosY - dz * sinY,
      lz: dx * sinY + dz * cosY,
    };
  }

  private carLocalDeckWorldY(
    car: RunawayRailcarInstance,
    lx: number,
    localY: number,
    lz: number
  ): number {
    return (
      car.liveY +
      localY +
      lx * Math.sin(car.liveRoll) -
      lz * Math.sin(car.livePitch)
    );
  }

  public sampleLiveTrainSurface(
    data: RunawayTrainChunkData,
    x: number,
    z: number,
    referenceY = -999.0
  ): { active: boolean; height: number } {
    let bestH = -36.5;
    let found = false;

    // 1. Live Surging, Banking & Bucking Runaway Railcars!
    for (let i = 0; i < data.railcars.length; i++) {
      const car = data.railcars[i];
      if (
        Math.abs(z - car.liveZ) > car.halfLength + 3.2 ||
        Math.abs(x - car.liveX) > car.halfWidth + 8.5
      ) {
        continue;
      }

      const loc = this.worldToCarLocalXZ(car, x, z);
      const lx = loc.lx;
      const lz = loc.lz;

      // Include coupler gangway bridge (`+-0.95m` beyond `halfLength`, unless decoupled!)
      const aftPad = car.isDecouplingLead ? 0.85 : 1.05;
      if (lz < -car.halfLength - 1.05 || lz > car.halfLength + aftPad) continue;

      const wingExtraW =
        (lx < 0 && car.hasSideJumpLeft) || (lx > 0 && car.hasSideJumpRight) ? 1.45 : 0.0;
      if (Math.abs(lx) > car.halfWidth + wingExtraW) continue;

      let localY = car.deckHeightOffset;

      // Side-Transfer Jump Wing Ramp lift (`+1.05m` outward launch angle onto neighboring train!)
      if (
        Math.abs(lx) > car.halfWidth - 0.2 &&
        (car.kind === 'FLATBED_TRANSFER' ||
          car.kind === 'TANKER_CAR' ||
          car.kind === 'ROOF_RAMP_CAR')
      ) {
        const wingT = clamp((Math.abs(lx) - (car.halfWidth - 0.2)) / 1.55, 0, 1);
        localY += wingT * 1.05;
      }

      if (car.kind === 'ROOF_RAMP_CAR' && Math.abs(lx) <= car.halfWidth) {
        const rampT = clamp(
          (car.halfLength - lz) / (car.halfLength * 1.88),
          0,
          1
        );
        localY = Math.max(localY, lerp(car.deckHeightOffset, car.roofHeightOffset, rampT));
      } else if (car.kind === 'BOXCAR_DUAL_TIER') {
        const roofWorldY = this.carLocalDeckWorldY(car, lx, car.roofHeightOffset, lz);
        if (Math.abs(lx) <= car.halfWidth && referenceY >= roofWorldY - 1.85) {
          localY = car.roofHeightOffset;
        }
      } else if (car.kind === 'LOG_SPILL_FLATCAR') {
        // Rolling Redwood Logs bump/vault profile on deck!
        for (let l = 0; l < car.rollingLogs.length; l++) {
          const log = car.rollingLogs[l];
          if (!log.active) continue;
          const dzLog = Math.abs(lz - log.localZ);
          if (dzLog < log.radius * 1.15 && Math.abs(lx) <= log.halfWidth) {
            const hBump =
              Math.sqrt(Math.max(0, 1.0 - Math.pow(dzLog / (log.radius * 1.15), 2))) *
              (log.radius * 1.35);
            localY = Math.max(localY, car.deckHeightOffset + hBump);
          }
        }
      } else if (car.kind === 'ARMORED_LOCOMOTIVE' && Math.abs(lx) <= 2.2) {
        // Sloped drivable boiler/hood launch ramp on Armored Locomotive!
        const hoodT = clamp((car.halfLength - lz) / (car.halfLength * 1.75), 0, 1);
        localY = Math.max(localY, car.deckHeightOffset + hoodT * 1.65);
      }

      // Decoupling aft launch kick-plate (`+0.55m` lip)
      if (car.isDecouplingLead && lz > car.halfLength - 1.2) {
        const kickT = clamp((lz - (car.halfLength - 1.2)) / 1.8, 0, 1);
        localY += kickT * 0.48;
      }

      const worldH = this.carLocalDeckWorldY(car, lx, localY, lz);
      if (worldH > bestH) {
        bestH = worldH;
        found = true;
      }
    }

    // 2. Underlying Timber Trestle Deck (provides railbed between decoupled gaps, EXCEPT when Dynamited & Collapsed!)
    for (let tIdx = 0; tIdx < 3; tIdx++) {
      if (z > data.zStart + 0.5 || z < data.zStart - data.zLength - 0.5) continue;

      let trestleGone = false;
      let sagDrop = 0;
      for (let d = 0; d < data.disasters.length; d++) {
        const dis = data.disasters[d];
        if (dis.trackIdx === tIdx && z <= dis.zStart && z >= dis.zEnd) {
          if (dis.kind === 'DYNAMITED_TRESTLE' && dis.isCollapsed) {
            trestleGone = true;
          } else if (dis.kind === 'DYNAMITED_TRESTLE') {
            sagDrop = 0.25;
          }
        }
      }
      if (trestleGone) continue;

      // Narrow `2.6m` half-width curved timber railbed under the cars!
      const railPose = this.sampleTrackPoseAtZ(tIdx, z, 0, data.sampleCourseX);
      if (Math.abs(x - railPose.x) <= 2.55) {
        const h = RAIL_DATUM_Y - sagDrop;
        if (h > bestH) {
          bestH = h;
          found = true;
        }
      }
    }

    return { active: found, height: bestH };
  }

  public sampleSurfaceVelocityInChunk(
    data: RunawayTrainChunkData,
    x: number,
    z: number,
    outVel: THREE.Vector3
  ): boolean {
    for (let i = 0; i < data.railcars.length; i++) {
      const car = data.railcars[i];
      if (
        Math.abs(z - car.liveZ) <= car.halfLength + 1.0 &&
        Math.abs(x - car.liveX) <= car.halfWidth + 1.1
      ) {
        outVel.set(0, 0, car.liveSurgeVelZ);
        return true;
      }
    }
    return false;
  }

  /**
   * 240Hz Rigid-Body Collisions & Locomotive Steam Blast Forces:
   *  1. Armored Boxcar Side Walls (blocking side clip while allowing center interior drive-through AND rooftop driving!)
   *  2. Dead-End Caved-In Rockslide Tunnel Boulder Barrier!
   *  3. Swinging Overhead Railroad Signal Booms (knocking rooftop drivers sideways!)
   *  4. Scalding Locomotive Steam Blasts (upward + sideways pressure jolt!)
   */
  public resolveTrainCollisionsAndHazardsInChunk(
    data: RunawayTrainChunkData,
    dt: number,
    carPos: THREE.Vector3,
    forwardWorld: THREE.Vector3,
    rightWorld: THREE.Vector3,
    linearVel: THREE.Vector3,
    angularVel: THREE.Vector3,
    passiveProtection: boolean,
    onImpact?: (intensity: number, worldPoint: THREE.Vector3) => void
  ): void {
    if (passiveProtection) return;

    const hullOffsets = [-1.25, 0.0, 1.15];
    const sphereRad = 0.92;

    const pushAgainstLocalAABB = (
      originX: number,
      originZ: number,
      yaw: number,
      boxLx: number,
      boxLz: number,
      halfW: number,
      halfL: number,
      extraImpulseVx = 0,
      extraImpulseVz = 0
    ): boolean => {
      const cosY = Math.cos(yaw);
      const sinY = Math.sin(yaw);
      let anyHit = false;

      for (let h = 0; h < hullOffsets.length; h++) {
        const zOff = hullOffsets[h];
        const sx = carPos.x + forwardWorld.x * -zOff;
        const sz = carPos.z + forwardWorld.z * -zOff;
        const dx = sx - originX;
        const dz = sz - originZ;
        const slx = dx * cosY - dz * sinY;
        const slz = dx * sinY + dz * cosY;

        const clx = clamp(slx, boxLx - halfW, boxLx + halfW);
        const clz = clamp(slz, boxLz - halfL, boxLz + halfL);
        const dlx = slx - clx;
        const dlz = slz - clz;
        const dSq = dlx * dlx + dlz * dlz;

        if (dSq < sphereRad * sphereRad) {
          anyHit = true;
          let nlx = 0;
          let nlz = 0;
          let overlap = 0;
          if (dSq > 1e-6) {
            const d = Math.sqrt(dSq);
            nlx = dlx / d;
            nlz = dlz / d;
            overlap = sphereRad - d;
          } else {
            const px = halfW - Math.abs(slx - boxLx);
            const pz = halfL - Math.abs(slz - boxLz);
            if (px < pz) {
              nlx = slx >= boxLx ? 1 : -1;
              overlap = px + sphereRad;
            } else {
              nlz = slz >= boxLz ? 1 : -1;
              overlap = pz + sphereRad;
            }
          }

          const nx = nlx * cosY + nlz * sinY;
          const nz = -nlx * sinY + nlz * cosY;
          carPos.x += nx * overlap * 0.92;
          carPos.z += nz * overlap * 0.92;

          const relVx = linearVel.x - extraImpulseVx;
          const relVz = linearVel.z - extraImpulseVz;
          const vn = relVx * nx + relVz * nz;
          if (vn < 0) {
            const jMag = -1.25 * vn;
            linearVel.x += nx * jMag;
            linearVel.z += nz * jMag;
            const localHitX = nx * rightWorld.x + nz * rightWorld.z;
            angularVel.y += clamp(-zOff * localHitX * jMag * 0.075, -1.5, 1.5);

            if (Math.abs(vn) > 2.6 && onImpact) {
              this._hitContact.set(sx, carPos.y, sz);
              onImpact(clamp(Math.abs(vn) / 15.0, 0.25, 1.0), this._hitContact);
            }
          }
        }
      }
      return anyHit;
    };

    // 1. Boxcar Side Walls, Rolling Logs & Locomotive Steam Blasts!
    for (let i = 0; i < data.railcars.length; i++) {
      const car = data.railcars[i];
      if (
        Math.abs(carPos.z - car.liveZ) > car.halfLength + 4.0 ||
        Math.abs(carPos.x - car.liveX) > car.halfWidth + 9.5
      ) {
        continue;
      }

      if (car.kind === 'BOXCAR_DUAL_TIER') {
        const roofWorldY = car.liveY + car.roofHeightOffset;
        // Only collide with side panels when BELOW the drivable Boxcar Roof!
        if (carPos.y < roofWorldY - 0.55) {
          for (const side of [-1, 1]) {
            for (const zSeg of [-car.halfLength * 0.66, car.halfLength * 0.66]) {
              pushAgainstLocalAABB(
                car.liveX,
                car.liveZ,
                car.liveYaw,
                side * 2.75,
                zSeg,
                0.2,
                car.halfLength * 0.33
              );
            }
          }
        }
      } else if (car.kind === 'ARMORED_LOCOMOTIVE' && car.steamBlastActive) {
        // Scalding High-Pressure Steam Blast upward + outward jolt!
        const loc = this.worldToCarLocalXZ(car, carPos.x, carPos.z);
        const dzSteam = loc.lz - -car.halfLength * 0.45;
        if (Math.hypot(loc.lx, dzSteam) < 5.2 && carPos.y < car.liveY + 11.5) {
          linearVel.y += 18.5 * dt;
          linearVel.x += Math.sign(loc.lx || 1) * 9.5 * dt;
        }
      }
    }

    // 2. Dead-End Rockslide Tunnel Solid Granite Barrier!
    for (let d = 0; d < data.disasters.length; d++) {
      const dis = data.disasters[d];
      if (dis.kind !== 'ROCKSLIDE_TUNNEL') continue;
      if (Math.abs(carPos.z - dis.centerZ) > dis.rockWallHalfL + 4.0) continue;
      if (carPos.y > RAIL_DATUM_Y + 12.0) continue;

      pushAgainstLocalAABB(
        dis.centerX,
        dis.centerZ,
        dis.yaw,
        0,
        0,
        dis.rockWallHalfW,
        dis.rockWallHalfL
      );
    }

    // 3. Swinging Overhead Railroad Signal Booms (sweeps at `y = 7.65m` across Boxcar Roofs!)
    for (let s = 0; s < data.signalBooms.length; s++) {
      const sb = data.signalBooms[s];
      if (Math.abs(carPos.z - sb.z) > sb.armLength + 3.0) continue;
      if (Math.abs(carPos.y - sb.pivotY) > 0.72) continue;

      const hit = pushAgainstLocalAABB(
        sb.pivotX,
        sb.z,
        sb.currentSwingYaw,
        0,
        sb.armLength * 0.5,
        0.45,
        sb.armLength * 0.5,
        Math.cos(sb.currentSwingYaw) * sb.angularVelYaw * sb.armLength * 0.5,
        -Math.sin(sb.currentSwingYaw) * sb.angularVelYaw * sb.armLength * 0.5
      );
      if (hit && this.onSignalBoomImpact) {
        this.onSignalBoomImpact(carPos.clone());
      }
    }
  }

  // ===========================================================================
  // CHUNK & GLOBAL GORGE ANIMATION STEP
  // ===========================================================================

  public stepChunkHazards(
    data: RunawayTrainChunkData,
    dt: number,
    elapsed: number,
    sampleCourseX: (z: number) => number
  ): void {
    const clampedDt = Math.min(dt, 0.033);

    // 1. Step Parallel Runaway Railcars (Surge, Curve Banking, Decoupling, Rolling Logs & Steam Blasts)!
    for (let i = 0; i < data.railcars.length; i++) {
      const car = data.railcars[i];
      const surge = this.sampleTrackSurge(car.trackIdx, elapsed);

      // Decoupling cars unlatch & drift up to `-6.2m` forward away from the trailing consist!
      if (car.isDecouplingLead) {
        const decoupleWave = Math.max(0, Math.sin(elapsed * 0.85 + car.logCyclePhase));
        car.liveDecoupleOffsetZ = -decoupleWave * 6.2;
      } else {
        car.liveDecoupleOffsetZ = 0;
      }

      car.liveZ = car.baseZ + surge.surgeZ + car.liveDecoupleOffsetZ;
      car.liveSurgeVelZ = surge.surgeVelZ;

      const pose = this.sampleTrackPoseAtZ(car.trackIdx, car.liveZ, elapsed, sampleCourseX);
      car.liveX = pose.x;
      car.liveY = pose.y;
      car.liveYaw = pose.yaw;
      car.liveRoll = pose.roll;
      car.livePitch = pose.pitch;

      car.carGroup.position.set(car.liveX, car.liveY, car.liveZ);
      this._euler.set(car.livePitch, car.liveYaw, car.liveRoll, 'YXZ');
      car.carGroup.rotation.copy(this._euler);

      // Spin all iron bogie wheelsets furiously (`~115 km/h` runaway train wheel RPM)!
      const wheelSpin = -elapsed * 28.0;
      for (let w = 0; w < car.wheelMeshes.length; w++) {
        car.wheelMeshes[w].rotation.x = wheelSpin;
      }

      // Step Snapping Chains & Rolling Giant Timber Logs (`LOG_SPILL_FLATCAR`)!
      if (car.rollingLogs.length > 0) {
        const cyc = (((elapsed + car.logCyclePhase) % 5.4) + 5.4) % 5.4;
        const chainIntact = cyc < 1.3;
        if (!chainIntact && car.chainWasIntact && this.onLogChainSnap) {
          this.onLogChainSnap(new THREE.Vector3(car.liveX, car.liveY + 2.2, car.liveZ));
        }
        car.chainWasIntact = chainIntact;

        for (let l = 0; l < car.rollingLogs.length; l++) {
          const log = car.rollingLogs[l];
          if (chainIntact) {
            log.localZ = -car.halfLength * 0.58 + l * 3.4;
            log.rollAngle = Math.sin(elapsed * 18.0) * 0.08;
            log.active = true;
            log.mesh.visible = true;
            log.mesh.position.set(0, car.deckHeightOffset + log.radius, log.localZ);
            log.mesh.rotation.x = log.rollAngle;
          } else {
            const tRoll = Math.max(0, cyc - 1.3 - l * 0.55);
            const rollDist = 0.5 * 5.8 * tRoll * tRoll;
            log.localZ = -car.halfLength * 0.58 + l * 3.4 + rollDist;
            log.rollAngle = rollDist / log.radius;

            if (log.localZ <= car.halfLength + 1.2) {
              log.active = true;
              log.mesh.visible = true;
              log.mesh.position.set(0, car.deckHeightOffset + log.radius, log.localZ);
              log.mesh.rotation.x = log.rollAngle;
            } else {
              // Plunges off the rear flatcar coupler into the gorge!
              const overDrop = (log.localZ - (car.halfLength + 1.2)) * 1.4;
              log.active = false;
              log.mesh.visible = overDrop < 14.0;
              log.mesh.position.set(
                0,
                car.deckHeightOffset + log.radius - overDrop,
                log.localZ
              );
              log.mesh.rotation.x = log.rollAngle;
            }
          }
        }
      }

      // Step Periodic High-Pressure Locomotive Steam Blasts!
      if (car.kind === 'ARMORED_LOCOMOTIVE' && car.steamPlumeMesh) {
        const sCyc = (((elapsed + car.steamPhase) % 4.6) + 4.6) % 4.6;
        const wasActive = car.steamBlastActive;
        car.steamBlastActive = sCyc > 2.8 && sCyc < 4.35;
        car.steamPlumeMesh.visible = car.steamBlastActive;
        if (car.steamBlastActive) {
          const pScale = 0.85 + Math.sin(elapsed * 24.0) * 0.22;
          car.steamPlumeMesh.scale.set(pScale, 1.0 + (sCyc - 2.8) * 0.25, pScale);
          if (!wasActive && this.onLocomotiveSteamBlast) {
            this.onLocomotiveSteamBlast(
              new THREE.Vector3(car.liveX, car.liveY + 4.0, car.liveZ)
            );
          }
        }
      }
    }

    // 2. Step Dynamited Wooden Trestle Explosions & Canyon Collapses!
    for (let d = 0; d < data.disasters.length; d++) {
      const dis = data.disasters[d];
      if (dis.kind !== 'DYNAMITED_TRESTLE' || !dis.collapsibleGroup) continue;

      const cyc = (((elapsed + dis.phase) % dis.period) + dis.period) % dis.period;
      const u = cyc / dis.period;
      // 0.0 .. 0.34: Intact rigging with glowing dynamite fuses
      // 0.34 .. 0.88: BOOM!! Trestle buckles & plunges -22m into the canyon gorge!
      // 0.88 .. 1.00: Resetting span for continuous observer/replay spectacle
      dis.isCollapsed = u >= 0.34 && u < 0.88;

      if (dis.isCollapsed && !dis.wasCollapsed && this.onTrestleDynamiteBlast) {
        this.onTrestleDynamiteBlast(
          new THREE.Vector3(dis.centerX, RAIL_DATUM_Y + 1.5, dis.centerZ)
        );
      }
      dis.wasCollapsed = dis.isCollapsed;

      if (dis.isCollapsed) {
        const tFall = clamp((u - 0.34) / 0.24, 0, 1);
        dis.collapseDropY = -22.0 * tFall * tFall;
        dis.collapsePitch = 0.58 * tFall;
        if (dis.blastFlashMesh) {
          dis.blastFlashMesh.visible = tFall < 0.35;
          const fScale = lerp(18.0, 1.0, tFall / 0.35);
          dis.blastFlashMesh.scale.setScalar(Math.max(0.1, fScale));
        }
      } else if (u >= 0.88) {
        const tReset = (u - 0.88) / 0.12;
        dis.collapseDropY = lerp(-22.0, 0.0, smoothstep(0, 1, tReset));
        dis.collapsePitch = lerp(0.58, 0.0, smoothstep(0, 1, tReset));
        if (dis.blastFlashMesh) dis.blastFlashMesh.visible = false;
      } else {
        dis.collapseDropY = Math.sin(elapsed * 22.0) * 0.06; // Fuse-tremor!
        dis.collapsePitch = 0;
        if (dis.blastFlashMesh) dis.blastFlashMesh.visible = false;
      }

      dis.collapsibleGroup.position.set(
        dis.centerX,
        RAIL_DATUM_Y + dis.collapseDropY,
        dis.centerZ
      );
      this._euler.set(dis.collapsePitch, dis.yaw, 0, 'YXZ');
      dis.collapsibleGroup.rotation.copy(this._euler);
    }

    // 3. Step Swinging Overhead Railroad Signal Booms!
    for (let s = 0; s < data.signalBooms.length; s++) {
      const sb = data.signalBooms[s];
      const baseDir = sb.pivotX < sb.centerX ? Math.PI * 0.5 : -Math.PI * 0.5;
      const arg = elapsed * sb.freq + sb.phase;
      sb.currentSwingYaw = baseDir + Math.sin(arg) * 0.72;
      sb.angularVelYaw = Math.cos(arg) * sb.freq * 0.72;
      sb.armGroup.rotation.y = sb.currentSwingYaw;
      sb.tipWorldPos.set(
        sb.pivotX + Math.sin(sb.currentSwingYaw) * sb.armLength,
        sb.pivotY,
        sb.z + Math.cos(sb.currentSwingYaw) * sb.armLength
      );
    }

    void clampedDt;
  }

  public stepGlobalGorgeAtmosphere(
    active: boolean,
    focusPos: THREE.Vector3,
    elapsed: number,
    dt: number
  ): void {
    this.atmosphereFxGroup.visible = active;
    if (!active) return;

    const clampedDt = Math.min(dt, 0.05);
    this.riverMesh.position.x = focusPos.x;
    this.riverMesh.position.z = focusPos.z;
    this.gorgeRiverMat.uniforms.uTime.value = elapsed;

    // Streak hot coal cinders & rail sparks backward (`+Z` at +44 m/s) for visceral 110 km/h runaway train speed!
    const posAttr = this.cinderPoints.geometry.attributes.position as THREE.BufferAttribute;
    for (let i = 0; i < this.cinderCount; i++) {
      const ix = i * 3;
      let x = this.cinderPositions[ix] + Math.sin(elapsed * 3.0 + i) * 1.8 * clampedDt;
      let y = this.cinderPositions[ix + 1] + 2.4 * clampedDt;
      let z = this.cinderPositions[ix + 2] + 44.0 * clampedDt;

      if (x - focusPos.x > 48) x -= 96;
      if (x - focusPos.x < -48) x += 96;
      if (y > focusPos.y + 22) y -= 32;
      if (z - focusPos.z > 85) z -= 170;
      if (z - focusPos.z < -85) z += 170;

      this.cinderPositions[ix] = x;
      this.cinderPositions[ix + 1] = y;
      this.cinderPositions[ix + 2] = z;
    }
    posAttr.needsUpdate = true;
  }
}

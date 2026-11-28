import * as THREE from 'three';
import { ProceduralTextureLibrary } from '../graphics/proceduralTextures';
import { clamp, lerp, mulberry32, smoothstep } from '../math/noise';
import { WorldObstacle } from './trailCorridor';

export interface ConveyorBoulderHazard {
  /** Distance along conveyor local Z (`-halfLen .. +halfLen`, where `-` is toward `-Z` down-course) */
  localZ: number;
  lateralX: number;
  radius: number;
  rollAngle: number;
  active: boolean;
  mesh: THREE.Mesh;
}

export interface ConveyorBridgeInstance {
  /** World pivot / midpoint of the elevated steel truss conveyor bridge */
  centerX: number;
  centerZ: number;
  yaw: number;
  halfWidth: number;
  halfLength: number;
  /** Entry (aft `+halfLength`) and Exit (fore `-halfLength`) deck world Y */
  aftY: number;
  foreY: number;
  /**
   * Belt surface speed along the conveyor's forward (`-localZ`) axis in m/s:
   * `+14.0` = high-speed downstream launch belt (`-14 m/s` world Z),
   * `-14.0` = upstream counter-flow belt (`+14 m/s` world Z fighting player!).
   */
  beltSpeedForwardMps: number;
  boulders: ConveyorBoulderHazard[];
  rollerMeshes: THREE.Object3D[];
  crusherDrumLeft: THREE.Mesh | null;
  crusherDrumRight: THREE.Mesh | null;
}

export interface BaggerExcavatorInstance {
  chunkIndex: number;
  /** Base home position of the 4-track crawler undercarriage */
  baseX: number;
  baseZ: number;
  deckY: number;
  /** Slow crawling motion along the pit spine (`±4.2m`) */
  crawlPhase: number;
  crawlFreq: number;
  crawlAmpZ: number;
  liveX: number;
  liveZ: number;
  liveCrawlVelZ: number;

  /** Slewing 68m Steel Lattice Boom + 20m Rotating Toothed Bucket Wheel! */
  slewPhase: number;
  slewFreq: number;
  slewAmpYaw: number;
  baseBoomYaw: number;
  liveBoomYaw: number;
  liveBoomAngularVelYaw: number;
  boomLength: number; // 54.0m forward from turntable pivot (`+28m` counterweight = 82m-90m machine!)
  boomHalfWidth: number; // 3.65m (`7.3m` wide drivable grated boom catwalk!)
  boomStartRelY: number; // 0.0m relative to `deckY` (`3.2m` world Y)
  boomTipRelY: number; // +4.8m relative to `deckY` (`8.0m` world Y at boom head!)

  /** 20m-diameter (`radius = 10.0m`) Rotating Toothed Bucket Wheel at boom head */
  wheelRadius: number;
  wheelLateralOffset: number; // Mounted beside the exit ramp so wheel spins menacingly next to roadway!
  wheelSpinAngle: number;
  wheelSpinSpeed: number;
  wheelWorldPos: THREE.Vector3;

  undercarriageGroup: THREE.Group;
  slewSuperstructureGroup: THREE.Group;
  bucketWheelMesh: THREE.Group;
  crawlerSprockets: THREE.Object3D[];
}

export interface MiningHaulTruckInstance {
  laneXOffset: number;
  homeZ: number;
  patrolAmpZ: number;
  patrolFreq: number;
  patrolPhase: number;
  benchY: number;
  halfWidth: number; // 3.7m (`7.4m` wide 400-ton mining truck!)
  halfLength: number; // 8.4m (`16.8m` long!)
  tailgateRelY: number; // +0.12m flush entry lip at aft (`+halfLength`)
  crestRelY: number; // +5.15m tilted dump-bed stunt ramp crest over front cab (`-halfLength`)!

  liveX: number;
  liveY: number;
  liveZ: number;
  liveYaw: number;
  liveVelX: number;
  liveVelZ: number;

  truckGroup: THREE.Group;
  wheelMeshes: THREE.Object3D[];
}

export interface RipplingBenchBlastZone {
  side: -1 | 1; // -1 = West Bench, +1 = East Bench
  breachZStart: number; // Aft edge of pit chasm breach in bench
  breachZEnd: number; // Fore edge of pit chasm breach in bench
  centerX: number;
  centerZ: number;
  yaw: number;
  benchY: number;
  slabHalfWidth: number;
  slabHalfLength: number;

  period: number;
  phase: number;
  /** Sequential 4-charge ripple detonation state */
  firedChargesMask: number;
  deployProgress: number; // 0 = vertical against cliff highwall, 1 = toppled flat/ramped across breach!
  isBridgeDeployed: boolean;

  slabPivotGroup: THREE.Group;
  chargeMeshes: THREE.Mesh[];
  blastFlashMeshes: THREE.Mesh[];
}

export interface StripMineChunkData {
  group: THREE.Group;
  chunkIndex: number;
  breachSide: -1 | 1;
  breachZStart: number;
  breachZEnd: number;
  excavators: BaggerExcavatorInstance[];
  conveyors: ConveyorBridgeInstance[];
  haulTrucks: MiningHaulTruckInstance[];
  benchBlasts: RipplingBenchBlastZone[];
  sampleCourseX: (z: number) => number;
  zStart: number;
  zLength: number;
}

const BENCH_OFFSET_X = 20.5; // West Bench (-20.5m) & East Bench (+20.5m)
const BENCH_HALF_WIDTH = 6.2; // 12.4m wide blast-cut limestone haul benches!
const BENCH_DATUM_Y = 2.0; // +2.0m quarry bench datum above -42m flooded pit lake!
const PIT_SUMP_WATER_Y = -36.5;

/**
 * LEVEL [14]: OPEN-PIT STRIP MINE & COLOSSAL BUCKET-WHEEL EXCAVATORS ('STRIP_MINE')
 *
 * Authentic industrial mega-quarry engineering & 240Hz moving-machinery physics:
 *  1. Colossal 90m Krupp `Bagger 288` Bucket-Wheel Excavators:
 *     - Crawling 4-pod caterpillar base + rear boarding ramp (`+2.0m -> +3.2m`).
 *     - 68m Slewing Steel Lattice Boom (`slews ±21m` across the open pit chasm with real `ω × r`
 *       tangential tire surface velocity!) climbing to `+8.0m` at the boom-head jump ramp.
 *     - 20m-Diameter Rotating Toothed Bucket Wheel (`12 steel digging buckets`) chewing at the boom head.
 *  2. Live Moving Conveyor-Belt Skybridges (`±14.0 m/s` belt surface physics!):
 *     - Downstream `+14 m/s` launch conveyors rocket vehicles across pit chasms.
 *     - Upstream `-14 m/s` counter-flow conveyors spill tumbling blasted limestone boulders into
 *       spinning Rotary Jaw Rock-Crusher Hoppers.
 *  3. 400-Ton Caterpillar-Yellow Mining Haul Trucks (`Rolling Stunt Ramps`!):
 *     - Rumbling monster dump trucks with `3.9m` tires and tilted hydraulic dump beds (`+2.1m -> +7.15m`).
 *  4. Sequential Rippling Quarry-Wall Dynamite Blasts:
 *     - 1-2-3-4 ripple borehole detonations topple hinged limestone highwall slabs into fresh
 *       rock-ramp shortcuts across blasted bench breaches.
 */
export class StripMineBuilder {
  // Shared industrial PBR materials
  private readonly baggerSteelMat: THREE.MeshStandardMaterial;
  private readonly baggerYellowMat: THREE.MeshStandardMaterial;
  private readonly haulTruckYellowMat: THREE.MeshStandardMaterial;
  private readonly darkIronMat: THREE.MeshStandardMaterial;
  private readonly conveyorBeltDownstreamMat: THREE.MeshStandardMaterial;
  private readonly conveyorBeltUpstreamMat: THREE.MeshStandardMaterial;
  private readonly limestoneBenchMat: THREE.MeshStandardMaterial;
  private readonly blastedRockMat: THREE.MeshStandardMaterial;
  private readonly hazardChevronMat: THREE.MeshStandardMaterial;
  private readonly dynamiteRedGlowMat: THREE.MeshBasicMaterial;
  private readonly blastFlashMat: THREE.MeshBasicMaterial;
  private readonly amberBeaconMat: THREE.MeshBasicMaterial;
  private readonly cabGlassMat: THREE.MeshStandardMaterial;

  // Shared procedural geometries
  private readonly unitBoxGeo: THREE.BoxGeometry;
  private readonly wheelCylGeo: THREE.CylinderGeometry;
  private readonly rollerCylGeo: THREE.CylinderGeometry;
  private readonly bucketScoopGeo: THREE.BoxGeometry;
  private readonly boulderGeo: THREE.IcosahedronGeometry;
  private readonly sphereGeo: THREE.SphereGeometry;

  // Global Open-Pit Sump Lake & Drifting Quarry Limestone Dust Plumes
  public readonly atmosphereFxGroup = new THREE.Group();
  private readonly sumpLakeMesh: THREE.Mesh;
  private readonly sumpLakeMat: THREE.ShaderMaterial;
  private readonly dustParticles: THREE.Points;
  private readonly dustPositions: Float32Array;
  private readonly dustCount = 340;

  // Live audio/particle callbacks wired in `main.ts`
  public onBenchDynamiteBlast: ((worldPos: THREE.Vector3, chargeIndex: number) => void) | null =
    null;
  public onBucketWheelImpact: ((worldPos: THREE.Vector3) => void) | null = null;
  public onOreBoulderImpact: ((worldPos: THREE.Vector3) => void) | null = null;

  private readonly _dummy = new THREE.Object3D();
  private readonly _hitContact = new THREE.Vector3();

  constructor(textures: ProceduralTextureLibrary) {
    this.atmosphereFxGroup.name = 'StripMineAtmosphereFX';
    this.atmosphereFxGroup.visible = false;

    this.baggerSteelMat = new THREE.MeshStandardMaterial({
      color: 0x3b4652,
      roughness: 0.52,
      metalness: 0.78,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.28, 0.28),
    });

    this.baggerYellowMat = new THREE.MeshStandardMaterial({
      color: 0xd99b1e,
      roughness: 0.46,
      metalness: 0.55,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.22, 0.22),
    });

    this.haulTruckYellowMat = new THREE.MeshStandardMaterial({
      color: 0xf0ad1a,
      roughness: 0.38,
      metalness: 0.48,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.18, 0.18),
    });

    this.darkIronMat = new THREE.MeshStandardMaterial({
      color: 0x1e2228,
      roughness: 0.64,
      metalness: 0.72,
    });

    this.conveyorBeltDownstreamMat = this.createChevronBeltMaterial(true);
    this.conveyorBeltUpstreamMat = this.createChevronBeltMaterial(false);

    const limeMap = textures.rock.map.clone();
    limeMap.needsUpdate = true;
    limeMap.wrapS = THREE.RepeatWrapping;
    limeMap.wrapT = THREE.RepeatWrapping;
    limeMap.repeat.set(4, 8);

    this.limestoneBenchMat = new THREE.MeshStandardMaterial({
      color: 0xc8b594,
      map: limeMap,
      normalMap: textures.rock.normalMap,
      roughness: 0.88,
      metalness: 0.06,
    });

    this.blastedRockMat = new THREE.MeshStandardMaterial({
      color: 0xb59d78,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      roughness: 0.92,
      metalness: 0.05,
    });

    this.hazardChevronMat = this.createHazardStripeMaterial();

    this.dynamiteRedGlowMat = new THREE.MeshBasicMaterial({
      color: 0xff2e12,
    });

    this.blastFlashMat = new THREE.MeshBasicMaterial({
      color: 0xffbe55,
      transparent: true,
      opacity: 0.9,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });

    this.amberBeaconMat = new THREE.MeshBasicMaterial({
      color: 0xffaa18,
    });

    this.cabGlassMat = new THREE.MeshStandardMaterial({
      color: 0x8ad4ff,
      emissive: 0x1b4560,
      emissiveIntensity: 0.55,
      roughness: 0.15,
      metalness: 0.85,
    });

    this.unitBoxGeo = new THREE.BoxGeometry(1, 1, 1);

    this.wheelCylGeo = new THREE.CylinderGeometry(1.0, 1.0, 1.0, 20);
    this.wheelCylGeo.rotateZ(Math.PI * 0.5); // Axle along local X!

    this.rollerCylGeo = new THREE.CylinderGeometry(0.18, 0.18, 1.0, 10);
    this.rollerCylGeo.rotateZ(Math.PI * 0.5);

    this.bucketScoopGeo = new THREE.BoxGeometry(2.3, 1.85, 2.2);
    this.boulderGeo = new THREE.IcosahedronGeometry(1.0, 1);
    this.sphereGeo = new THREE.SphereGeometry(0.32, 10, 8);

    // Deep Open-Pit Mineral Turquoise Sump Lake at y = -36.5m!
    const lakeGeo = new THREE.PlaneGeometry(160, 620, 24, 64);
    lakeGeo.rotateX(-Math.PI * 0.5);
    this.sumpLakeMat = new THREE.ShaderMaterial({
      transparent: false,
      uniforms: {
        uTime: { value: 0 },
      },
      vertexShader: /* glsl */ `
        uniform float uTime;
        varying vec3 vWorldPos;
        void main() {
          vec3 p = position;
          vec4 wp = modelMatrix * vec4(p, 1.0);
          wp.y += sin(wp.z * 0.16 + uTime * 2.2) * 0.18 + cos(wp.x * 0.22 - uTime * 1.7) * 0.14;
          vWorldPos = wp.xyz;
          gl_Position = projectionMatrix * viewMatrix * wp;
        }
      `,
      fragmentShader: /* glsl */ `
        uniform float uTime;
        varying vec3 vWorldPos;
        void main() {
          float caustic = sin(vWorldPos.x * 0.45 + uTime * 1.4) * cos(vWorldPos.z * 0.35 - uTime * 1.1);
          float mineral = smoothstep(-0.3, 0.75, caustic);
          vec3 deepTeal = vec3(0.04, 0.24, 0.28);
          vec3 brightTurquoise = vec3(0.14, 0.56, 0.58);
          vec3 col = mix(deepTeal, brightTurquoise, mineral * 0.72);
          gl_FragColor = vec4(col, 1.0);
        }
      `,
    });
    this.sumpLakeMesh = new THREE.Mesh(lakeGeo, this.sumpLakeMat);
    this.sumpLakeMesh.position.y = PIT_SUMP_WATER_Y;
    this.atmosphereFxGroup.add(this.sumpLakeMesh);

    // Drifting Sunlit Limestone Quarry Dust Motes
    const dustGeo = new THREE.BufferGeometry();
    this.dustPositions = new Float32Array(this.dustCount * 3);
    for (let i = 0; i < this.dustCount; i++) {
      this.dustPositions[i * 3 + 0] = (Math.random() - 0.5) * 115.0;
      this.dustPositions[i * 3 + 1] = -8.0 + Math.random() * 38.0;
      this.dustPositions[i * 3 + 2] = (Math.random() - 0.5) * 240.0;
    }
    dustGeo.setAttribute('position', new THREE.BufferAttribute(this.dustPositions, 3));
    const dustMat = new THREE.PointsMaterial({
      color: 0xe8d2a6,
      size: 0.72,
      map: textures.particleSoft,
      transparent: true,
      opacity: 0.45,
      depthWrite: false,
    });
    this.dustParticles = new THREE.Points(dustGeo, dustMat);
    this.dustParticles.frustumCulled = false;
    this.atmosphereFxGroup.add(this.dustParticles);
  }

  private createHazardStripeMaterial(): THREE.MeshStandardMaterial {
    const c = document.createElement('canvas');
    c.width = 128;
    c.height = 128;
    const ctx = c.getContext('2d');
    if (ctx) {
      ctx.fillStyle = '#eb9e15';
      ctx.fillRect(0, 0, 128, 128);
      ctx.fillStyle = '#181a1e';
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
    const tex = new THREE.CanvasTexture(c);
    tex.wrapS = THREE.RepeatWrapping;
    tex.wrapT = THREE.RepeatWrapping;
    tex.repeat.set(1, 4);
    return new THREE.MeshStandardMaterial({
      map: tex,
      roughness: 0.48,
      metalness: 0.35,
    });
  }

  private createChevronBeltMaterial(downstreamBoost: boolean): THREE.MeshStandardMaterial {
    const c = document.createElement('canvas');
    c.width = 128;
    c.height = 256;
    const ctx = c.getContext('2d');
    if (ctx) {
      ctx.fillStyle = '#1c1f24';
      ctx.fillRect(0, 0, 128, 256);

      // Edge safety stripes: bright green-cyan for +14 m/s downstream launch belt, fiery orange for -14 m/s upstream belt!
      ctx.fillStyle = downstreamBoost ? '#32e696' : '#ff6b24';
      ctx.fillRect(4, 0, 10, 256);
      ctx.fillRect(114, 0, 10, 256);

      // Molded rubber V-chevrons showing belt travel direction!
      ctx.strokeStyle = downstreamBoost ? '#48d68d' : '#ff8538';
      ctx.lineWidth = 8;
      for (let y = 16; y < 256; y += 42) {
        ctx.beginPath();
        if (downstreamBoost) {
          ctx.moveTo(24, y + 18);
          ctx.lineTo(64, y);
          ctx.lineTo(104, y + 18);
        } else {
          ctx.moveTo(24, y);
          ctx.lineTo(64, y + 18);
          ctx.lineTo(104, y);
        }
        ctx.stroke();
      }
    }
    const tex = new THREE.CanvasTexture(c);
    tex.wrapS = THREE.RepeatWrapping;
    tex.wrapT = THREE.RepeatWrapping;
    tex.repeat.set(1, 9);
    return new THREE.MeshStandardMaterial({
      map: tex,
      roughness: 0.78,
      metalness: 0.16,
    });
  }

  // ===========================================================================
  // SPAWN / RECOVERY HELPER FOR VEHICLE & CAMERA
  // ===========================================================================

  public findNearestQuarrySpawn(
    x: number,
    z: number,
    sampleCourseX: (z: number) => number
  ): { x: number; y: number; z: number; yaw: number } {
    const clampedZ = Math.min(-10.0, z);
    const cx = sampleCourseX(clampedZ);
    const cxAhead = sampleCourseX(clampedZ - 4.0);
    const yaw = Math.atan2(-(cxAhead - cx), 4.0);

    // Starter staging platform near z >= -26m is centered at `cx`!
    if (clampedZ > -26.0) {
      return { x: cx, y: BENCH_DATUM_Y + 0.85, z: clampedZ, yaw };
    }

    const cIdx = Math.max(0, Math.floor(-clampedZ / 112.0));
    const breachSide: -1 | 1 = cIdx % 2 === 0 ? -1 : 1;
    // Prefer the intact unbroken quarry haul bench if near the breach, otherwise nearest bench!
    const preferredSide = x < cx ? -1 : 1;
    const safeSide = preferredSide === breachSide ? -breachSide : preferredSide;

    return {
      x: cx + safeSide * BENCH_OFFSET_X,
      y: BENCH_DATUM_Y + 0.85,
      z: clampedZ,
      yaw,
    };
  }

  // ===========================================================================
  // CHUNK POPULATION — QUARRY BENCHES, BAGGER 288, CONVEYORS, HAUL TRUCKS & BLASTS
  // ===========================================================================

  public populateStripMineChunk(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    zLength: number,
    sampleCourseX: (z: number) => number,
    _obstacles: WorldObstacle[]
  ): StripMineChunkData {
    const group = new THREE.Group();
    group.name = `StripMineChunk_${chunkIndex}`;
    chunkGroup.add(group);

    const excavators: BaggerExcavatorInstance[] = [];
    const conveyors: ConveyorBridgeInstance[] = [];
    const haulTrucks: MiningHaulTruckInstance[] = [];
    const benchBlasts: RipplingBenchBlastZone[] = [];

    const breachSide: -1 | 1 = chunkIndex % 2 === 0 ? -1 : 1;
    const breachZStart = zStart - zLength * 0.64; // e.g. -71.7m in chunk 0
    const breachZEnd = zStart - zLength * 0.84; // e.g. -94.1m (`22.4m` pit chasm gap!)

    // 1. Build Terraced West & East Limestone Quarry Haul Benches + Starter Staging Deck (`z > -28m`)!
    this.buildQuarryHaulBenches(
      group,
      chunkIndex,
      zStart,
      zLength,
      breachSide,
      breachZStart,
      breachZEnd,
      sampleCourseX
    );

    if (chunkIndex >= 0) {
      // 2. Build Colossal 90m Krupp `Bagger 288` Crawling Bucket-Wheel Excavator in Pit Center!
      const baggerZ = zStart - zLength * 0.44;
      excavators.push(
        this.buildBagger288Excavator(group, chunkIndex, baggerZ, sampleCourseX)
      );

      // 3. Build 2 Live Moving Conveyor-Belt Skybridges (`+14 m/s` Launch Belt & `-14 m/s` Ore-Boulder Belt)!
      conveyors.push(
        ...this.buildChunkConveyorSkybridges(group, chunkIndex, zStart, zLength, sampleCourseX)
      );

      // 4. Build 2 House-Sized 400-Ton Caterpillar-Yellow Mining Haul Trucks (`Rolling Stunt Ramps`!)!
      haulTrucks.push(
        ...this.buildChunkMiningHaulTrucks(
          group,
          chunkIndex,
          zStart,
          zLength,
          breachSide,
          sampleCourseX
        )
      );

      // 5. Build Sequential Rippling Quarry-Wall Bench Dynamite Blast & Hinged Rock-Ramp Shortcut!
      benchBlasts.push(
        this.buildRipplingBenchBlast(
          group,
          chunkIndex,
          breachSide,
          breachZStart,
          breachZEnd,
          sampleCourseX
        )
      );

      // Warm Quarry Sodium Floodlight illuminating the Bagger 288 & Crusher Pit!
      const pitLight = new THREE.PointLight(0xffd28a, 26.0, 84.0, 1.25);
      pitLight.position.set(sampleCourseX(baggerZ), BENCH_DATUM_Y + 18.0, baggerZ);
      group.add(pitLight);
    }

    return {
      group,
      chunkIndex,
      breachSide,
      breachZStart,
      breachZEnd,
      excavators,
      conveyors,
      haulTrucks,
      benchBlasts,
      sampleCourseX,
      zStart,
      zLength,
    };
  }

  // ===========================================================================
  // 1. TERRACED LIMESTONE QUARRY HAUL BENCHES & STARTER STAGING CAUSEWAY
  // ===========================================================================

  private buildQuarryHaulBenches(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    zLength: number,
    breachSide: -1 | 1,
    breachZStart: number,
    breachZEnd: number,
    sampleCourseX: (z: number) => number
  ): void {
    const benchSegs = 14;
    const segLen = zLength / benchSegs; // 8.0m segments
    const rockMats: THREE.Matrix4[] = [];
    const bermMats: THREE.Matrix4[] = [];

    for (let s = 0; s < benchSegs; s++) {
      const zMid = zStart - (s + 0.5) * segLen;
      const cx = sampleCourseX(zMid);
      const cxAhead = sampleCourseX(zMid - 3.0);
      const yaw = Math.atan2(-(cxAhead - cx), 3.0);

      // Wide central starter staging causeway (`z > -28m`) fanning out to West Bench, Center Bagger Ramp, & East Bench!
      if (zMid > -28.0) {
        this._dummy.position.set(cx, BENCH_DATUM_Y - 19.0, zMid);
        this._dummy.rotation.set(0, yaw, 0);
        this._dummy.scale.set(54.0, 38.0, segLen + 0.6);
        this._dummy.updateMatrix();
        rockMats.push(this._dummy.matrix.clone());
        continue;
      }

      for (const side of [-1, 1] as const) {
        if (
          chunkIndex >= 0 &&
          side === breachSide &&
          zMid < breachZStart &&
          zMid > breachZEnd
        ) {
          continue; // Open -42m Pit Chasm Breach bridged by Rippling Bench Blast or Haul Truck Jump!
        }

        const bx = cx + side * BENCH_OFFSET_X;
        this._dummy.position.set(bx, BENCH_DATUM_Y - 19.0, zMid);
        this._dummy.rotation.set(0, yaw, 0);
        this._dummy.scale.set(BENCH_HALF_WIDTH * 2.0, 38.0, segLen + 0.65);
        this._dummy.updateMatrix();
        rockMats.push(this._dummy.matrix.clone());

        // Outer highwall quarry safety berm (`orange/yellow chevron boulders`)
        this._dummy.position.set(
          bx + side * (BENCH_HALF_WIDTH - 0.55),
          BENCH_DATUM_Y + 0.42,
          zMid
        );
        this._dummy.rotation.set(0, yaw, 0);
        this._dummy.scale.set(0.95, 0.84, segLen * 0.72);
        this._dummy.updateMatrix();
        bermMats.push(this._dummy.matrix.clone());
      }
    }

    if (rockMats.length > 0) {
      const im = new THREE.InstancedMesh(
        this.unitBoxGeo,
        this.limestoneBenchMat,
        rockMats.length
      );
      for (let i = 0; i < rockMats.length; i++) im.setMatrixAt(i, rockMats[i]);
      im.instanceMatrix.needsUpdate = true;
      im.castShadow = true;
      im.receiveShadow = true;
      chunkGroup.add(im);
    }

    if (bermMats.length > 0) {
      const bm = new THREE.InstancedMesh(
        this.unitBoxGeo,
        this.hazardChevronMat,
        bermMats.length
      );
      for (let i = 0; i < bermMats.length; i++) bm.setMatrixAt(i, bermMats[i]);
      bm.instanceMatrix.needsUpdate = true;
      chunkGroup.add(bm);
    }
  }

  // ===========================================================================
  // 2. COLOSSAL 90-METER KRUPP `BAGGER 288` BUCKET-WHEEL EXCAVATOR
  // ===========================================================================

  private buildBagger288Excavator(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zCenter: number,
    sampleCourseX: (z: number) => number
  ): BaggerExcavatorInstance {
    const cx = sampleCourseX(zCenter);
    const cxAhead = sampleCourseX(zCenter - 8.0);
    const baseYaw = Math.atan2(-(cxAhead - cx), 8.0);
    const deckY = BENCH_DATUM_Y + 1.25; // +3.25m turntable deck!

    // Solid Limestone Pit Spine Pad supporting the crawling Bagger 288 + Aft Boarding Causeway!
    const spinePad = new THREE.Mesh(this.unitBoxGeo, this.limestoneBenchMat);
    spinePad.position.set(cx, BENCH_DATUM_Y - 19.0, zCenter + 12.0);
    spinePad.rotation.y = baseYaw;
    spinePad.scale.set(17.5, 38.0, 44.0);
    spinePad.receiveShadow = true;
    chunkGroup.add(spinePad);

    // Cross-Pit Limestone Causeway linking BOTH West & East Benches into the Bagger Boarding Ramp (`zCenter + 24m`)!
    const entryCrossZ = zCenter + 24.5;
    const entryCx = sampleCourseX(entryCrossZ);
    const crossBridge = new THREE.Mesh(this.unitBoxGeo, this.limestoneBenchMat);
    crossBridge.position.set(entryCx, BENCH_DATUM_Y - 19.0, entryCrossZ);
    crossBridge.rotation.y = baseYaw;
    crossBridge.scale.set(BENCH_OFFSET_X * 2.0 + 8.0, 38.0, 11.5);
    crossBridge.receiveShadow = true;
    chunkGroup.add(crossBridge);

    // Crawling Undercarriage Group
    const undercarriageGroup = new THREE.Group();
    undercarriageGroup.position.set(cx, 0, zCenter);
    undercarriageGroup.rotation.y = baseYaw;
    chunkGroup.add(undercarriageGroup);

    const crawlerSprockets: THREE.Object3D[] = [];
    // 4 Giant Caterpillar Track Pods (`17m` long x `2.4m` wide x `1.9m` tall)!
    for (const xSide of [-5.2, 5.2]) {
      for (const zPod of [-6.5, 6.5]) {
        const trackBody = new THREE.Mesh(this.unitBoxGeo, this.darkIronMat);
        trackBody.position.set(xSide, BENCH_DATUM_Y + 0.58, zPod);
        trackBody.scale.set(2.3, 1.16, 10.2);
        trackBody.castShadow = true;
        undercarriageGroup.add(trackBody);

        for (const zEnd of [-4.6, 4.6]) {
          const sp = new THREE.Mesh(this.wheelCylGeo, this.baggerYellowMat);
          sp.position.set(xSide, BENCH_DATUM_Y + 0.58, zPod + zEnd);
          sp.scale.set(2.45, 0.58, 0.58);
          undercarriageGroup.add(sp);
          crawlerSprockets.push(sp);
        }
      }
    }

    // Heavy Turntable Ring & Deck (`14.4m` wide x `18m` long at `deckY = +3.25m`)
    const turntableDeck = new THREE.Mesh(this.unitBoxGeo, this.baggerSteelMat);
    turntableDeck.position.set(0, deckY - 0.25, 0);
    turntableDeck.scale.set(14.4, 0.55, 18.0);
    turntableDeck.castShadow = true;
    turntableDeck.receiveShadow = true;
    undercarriageGroup.add(turntableDeck);

    // Flush Rear Steel Boarding Ramp (`z = +9.0 .. +21.0` climbing cleanly from `BENCH_DATUM_Y = 2.0m` to `deckY = 3.25m`!)
    const boardRise = deckY - BENCH_DATUM_Y;
    const boardRun = 12.5;
    const boardPitch = Math.atan2(boardRise, boardRun);
    const boardRamp = new THREE.Mesh(this.unitBoxGeo, this.hazardChevronMat);
    boardRamp.position.set(0, (BENCH_DATUM_Y + deckY) * 0.5 - 0.08, 14.8);
    boardRamp.rotation.x = boardPitch;
    boardRamp.scale.set(9.6, 0.28, Math.hypot(boardRun, boardRise));
    boardRamp.castShadow = true;
    boardRamp.receiveShadow = true;
    undercarriageGroup.add(boardRamp);

    // Slewing Superstructure (`68m` Forward Boom + `24m` Mast Tower + `26m` Counterweight Tail Boom = `94m` total span!)
    const slewSuperstructureGroup = new THREE.Group();
    slewSuperstructureGroup.position.set(cx, deckY, zCenter);
    slewSuperstructureGroup.rotation.y = baseYaw;
    chunkGroup.add(slewSuperstructureGroup);

    const boomLength = 54.0;
    const boomHalfWidth = 3.65; // 7.3m wide grated steel boom roadway!
    const boomTipRelY = 4.75; // Climbs smoothly from +3.25m to +8.00m world Y!
    const boomPitch = Math.atan2(boomTipRelY, boomLength);
    const boomHypot = Math.hypot(boomLength, boomTipRelY);

    // Towering 23m Krupp A-Frame Suspension Mast + Operator Control Cabins!
    for (const xLeg of [-4.2, 4.2]) {
      const mastLeg = new THREE.Mesh(this.unitBoxGeo, this.baggerYellowMat);
      mastLeg.position.set(xLeg, 11.2, 1.5);
      mastLeg.rotation.z = -Math.sign(xLeg) * 0.11;
      mastLeg.scale.set(0.95, 22.5, 1.15);
      mastLeg.castShadow = true;
      slewSuperstructureGroup.add(mastLeg);
    }
    const mastCap = new THREE.Mesh(this.unitBoxGeo, this.baggerSteelMat);
    mastCap.position.set(0, 22.2, 1.5);
    mastCap.scale.set(7.6, 1.4, 2.6);
    slewSuperstructureGroup.add(mastCap);

    // Elevated Glass-Fronted Bagger Operator Cabin perched off the side of the mast!
    const opCab = new THREE.Mesh(this.unitBoxGeo, this.baggerYellowMat);
    opCab.position.set(5.6, 7.8, -3.5);
    opCab.scale.set(3.2, 3.2, 4.8);
    slewSuperstructureGroup.add(opCab);

    const opWindow = new THREE.Mesh(this.unitBoxGeo, this.cabGlassMat);
    opWindow.position.set(5.6, 8.1, -5.95);
    opWindow.scale.set(2.8, 1.6, 0.2);
    slewSuperstructureGroup.add(opWindow);

    // 26m Counterweight Tail Boom (`+Z` aft, overhead at `y = +8.5m` so vehicles board cleanly underneath it!)
    for (const xSide of [-3.6, 3.6]) {
      const tailGirder = new THREE.Mesh(this.unitBoxGeo, this.baggerSteelMat);
      tailGirder.position.set(xSide, 8.8, 14.5);
      tailGirder.scale.set(0.75, 1.6, 26.0);
      slewSuperstructureGroup.add(tailGirder);
    }
    const ballastBlock = new THREE.Mesh(this.unitBoxGeo, this.darkIronMat);
    ballastBlock.position.set(0, 8.5, 26.5);
    ballastBlock.scale.set(9.4, 5.2, 5.8);
    ballastBlock.castShadow = true;
    slewSuperstructureGroup.add(ballastBlock);

    // Drivable 54m Forward Lattice Boom Roadway (`z = 0 .. -54m`, climbing `0 -> +4.75m`)!
    const boomDeck = new THREE.Mesh(this.unitBoxGeo, this.baggerSteelMat);
    boomDeck.position.set(0, boomTipRelY * 0.5 - 0.14, -boomLength * 0.5);
    boomDeck.rotation.x = boomPitch;
    boomDeck.scale.set(boomHalfWidth * 2.0, 0.32, boomHypot);
    boomDeck.castShadow = true;
    boomDeck.receiveShadow = true;
    slewSuperstructureGroup.add(boomDeck);

    // Center Yellow Chevron Guide-Strip + Side Lattice Truss Chords along the 54m Boom!
    const boomStripe = new THREE.Mesh(this.unitBoxGeo, this.hazardChevronMat);
    boomStripe.position.set(-0.6, boomTipRelY * 0.5 + 0.04, -boomLength * 0.5);
    boomStripe.rotation.x = boomPitch;
    boomStripe.scale.set(1.6, 0.06, boomHypot);
    slewSuperstructureGroup.add(boomStripe);

    for (const xTruss of [-boomHalfWidth - 0.35, boomHalfWidth + 0.35]) {
      const topChord = new THREE.Mesh(this.unitBoxGeo, this.baggerYellowMat);
      topChord.position.set(xTruss, boomTipRelY * 0.5 + 2.6, -boomLength * 0.5);
      topChord.rotation.x = boomPitch;
      topChord.scale.set(0.55, 0.55, boomHypot);
      slewSuperstructureGroup.add(topChord);

      for (let b = 1; b <= 6; b++) {
        const frac = b / 6.5;
        const post = new THREE.Mesh(this.unitBoxGeo, this.baggerSteelMat);
        post.position.set(xTruss, boomTipRelY * frac + 1.3, -boomLength * frac);
        post.scale.set(0.42, 2.6, 0.42);
        slewSuperstructureGroup.add(post);
      }

      // Steel Stay Cables from 22.2m Mast Cap out to Boom Head!
      const stayDx = 0;
      const stayDy = 22.2 - (boomTipRelY + 2.6);
      const stayDz = 1.5 - -boomLength * 0.88;
      const stayLen = Math.hypot(stayDx, stayDy, stayDz);
      const stayCable = new THREE.Mesh(this.unitBoxGeo, this.darkIronMat);
      stayCable.position.set(
        xTruss,
        (22.2 + boomTipRelY + 2.6) * 0.5,
        (1.5 - boomLength * 0.88) * 0.5
      );
      stayCable.rotation.x = -Math.atan2(stayDy, stayDz);
      stayCable.scale.set(0.18, 0.18, stayLen);
      slewSuperstructureGroup.add(stayCable);
    }

    // Boom-Head Launch Ramp Kick-Lip at `-boomLength` (`+0.85m` kick so vehicles soar onto the benches ahead!)
    const headKick = new THREE.Mesh(this.unitBoxGeo, this.hazardChevronMat);
    headKick.position.set(-0.65, boomTipRelY + 0.35, -boomLength + 2.2);
    headKick.rotation.x = boomPitch + 0.16;
    headKick.scale.set(5.4, 0.28, 4.8);
    slewSuperstructureGroup.add(headKick);

    // Colossal 20-Meter-Diameter (`radius = 10.0m`) Rotating Toothed Bucket Wheel!
    // Mounted on the starboard side (`+4.85m`) of the boom head so its 12 giant steel buckets
    // slice right past the boom-head roadway while leaving a clear `5.2m` launch ramp on the port side!
    const wheelRadius = 10.0;
    const wheelLateralOffset = 4.85;
    const bucketWheelMesh = new THREE.Group();
    bucketWheelMesh.position.set(wheelLateralOffset, boomTipRelY + 1.8, -boomLength - 1.5);
    slewSuperstructureGroup.add(bucketWheelMesh);

    // Heavy Hub + Twin Rim Rings + 8 Radial Spokes + 12 Toothed Excavator Buckets (`20m` diameter!)
    const hub = new THREE.Mesh(this.wheelCylGeo, this.baggerYellowMat);
    hub.scale.set(2.2, 2.4, 2.4);
    bucketWheelMesh.add(hub);

    for (let s = 0; s < 8; s++) {
      const ang = (s / 8) * Math.PI * 2;
      const spoke = new THREE.Mesh(this.unitBoxGeo, this.baggerSteelMat);
      spoke.position.set(0, Math.cos(ang) * wheelRadius * 0.5, Math.sin(ang) * wheelRadius * 0.5);
      spoke.rotation.x = -ang;
      spoke.scale.set(1.2, wheelRadius, 0.65);
      bucketWheelMesh.add(spoke);
    }

    const bucketCount = 12;
    for (let b = 0; b < bucketCount; b++) {
      const ang = (b / bucketCount) * Math.PI * 2;
      const rB = wheelRadius - 0.45;
      const bucket = new THREE.Mesh(
        this.bucketScoopGeo,
        b % 2 === 0 ? this.baggerYellowMat : this.darkIronMat
      );
      bucket.position.set(0, Math.cos(ang) * rB, Math.sin(ang) * rB);
      bucket.rotation.x = -ang;
      bucket.castShadow = true;
      bucketWheelMesh.add(bucket);

      // Rim chord segment connecting adjacent buckets into a true 20m circular wheel!
      const rimSeg = new THREE.Mesh(this.unitBoxGeo, this.baggerSteelMat);
      const midAng = ang + Math.PI / bucketCount;
      rimSeg.position.set(
        0,
        Math.cos(midAng) * (wheelRadius - 1.35),
        Math.sin(midAng) * (wheelRadius - 1.35)
      );
      rimSeg.rotation.x = -midAng;
      rimSeg.scale.set(1.5, 0.65, ((2 * Math.PI * (wheelRadius - 1.35)) / bucketCount) * 1.06);
      bucketWheelMesh.add(rimSeg);
    }

    return {
      chunkIndex,
      baseX: cx,
      baseZ: zCenter,
      deckY,
      crawlPhase: chunkIndex * 1.3,
      crawlFreq: 0.42,
      crawlAmpZ: 3.6,
      liveX: cx,
      liveZ: zCenter,
      liveCrawlVelZ: 0,
      slewPhase: chunkIndex * 2.05,
      slewFreq: 0.56,
      slewAmpYaw: 0.39, // Sweeps `54m * sin(0.39) = ±20.5m` right across from West Bench to East Bench!
      baseBoomYaw: baseYaw,
      liveBoomYaw: baseYaw,
      liveBoomAngularVelYaw: 0,
      boomLength,
      boomHalfWidth,
      boomStartRelY: 0,
      boomTipRelY,
      wheelRadius,
      wheelLateralOffset,
      wheelSpinAngle: 0,
      wheelSpinSpeed: 1.45,
      wheelWorldPos: new THREE.Vector3(cx + wheelLateralOffset, deckY + boomTipRelY + 1.8, zCenter - boomLength),
      undercarriageGroup,
      slewSuperstructureGroup,
      bucketWheelMesh,
      crawlerSprockets,
    };
  }

  // ===========================================================================
  // 3. LIVE MOVING CONVEYOR-BELT SKYBRIDGES (`±14 m/s` BELTS + TUMBLING BOULDERS!)
  // ===========================================================================

  private buildChunkConveyorSkybridges(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    zLength: number,
    sampleCourseX: (z: number) => number
  ): ConveyorBridgeInstance[] {
    const bridges: ConveyorBridgeInstance[] = [];

    // Conveyor A (First half of chunk): High-Speed `+14.0 m/s` Downstream Launch Conveyor Skybridge
    // spanning diagonally across the open pit from one Haul Bench over to the opposite Bench!
    const zAStart = zStart - 16.0;
    const zAEnd = zStart - 60.0;
    const sideA: -1 | 1 = chunkIndex % 2 === 0 ? -1 : 1;
    const xAStart = sampleCourseX(zAStart) + sideA * (BENCH_OFFSET_X - 2.5);
    const xAEnd = sampleCourseX(zAEnd) - sideA * (BENCH_OFFSET_X - 2.5);

    bridges.push(
      this.createSingleConveyorSkybridge(
        chunkGroup,
        xAStart,
        zAStart,
        BENCH_DATUM_Y + 0.05, // Flush `+2.05m` entry right off the quarry bench!
        xAEnd,
        zAEnd,
        BENCH_DATUM_Y + 3.4, // Climbs to `+5.4m` launch discharge head over opposite bench!
        14.0, // +14.0 m/s downstream boost belt!
        false,
        chunkIndex * 31 + 7
      )
    );

    // Conveyor B (Second half of chunk): `-14.0 m/s` Upstream Counter-Flow Ore Conveyor + Tumbling Boulders
    // & Spinning Rotary Jaw Rock-Crusher Hopper!
    const zBStart = zStart - 58.0;
    const zBEnd = zStart - 104.0;
    const sideB: -1 | 1 = -sideA as -1 | 1;
    const xBStart = sampleCourseX(zBStart) + sideB * (BENCH_OFFSET_X - 1.5);
    const xBEnd = sampleCourseX(zBEnd) + sideB * (BENCH_OFFSET_X - 1.5);

    bridges.push(
      this.createSingleConveyorSkybridge(
        chunkGroup,
        xBStart,
        zBStart,
        BENCH_DATUM_Y + 0.05, // Flush entry off unbroken bench!
        xBEnd,
        zBEnd,
        BENCH_DATUM_Y + 2.2,
        -14.0, // -14.0 m/s upstream counter-flow belt with tumbling blasted ore boulders!
        true,
        chunkIndex * 53 + 19
      )
    );

    return bridges;
  }

  private createSingleConveyorSkybridge(
    chunkGroup: THREE.Group,
    xStart: number,
    zStart: number,
    aftY: number,
    xEnd: number,
    zEnd: number,
    foreY: number,
    beltSpeedForwardMps: number,
    withBouldersAndCrusher: boolean,
    seed: number
  ): ConveyorBridgeInstance {
    const dx = xEnd - xStart;
    const dz = zEnd - zStart; // negative (toward -Z)
    const horizLen = Math.hypot(dx, dz);
    const halfLength = horizLen * 0.5;
    const halfWidth = 3.55; // 7.1m wide conveyor truss deck!
    const centerX = (xStart + xEnd) * 0.5;
    const centerZ = (zStart + zEnd) * 0.5;
    const yaw = Math.atan2(-dx, -dz);
    const midY = (aftY + foreY) * 0.5;
    const pitch = Math.atan2(foreY - aftY, horizLen);
    const slopeLen = Math.hypot(horizLen, foreY - aftY);

    const bridgeGroup = new THREE.Group();
    bridgeGroup.position.set(centerX, 0, centerZ);
    bridgeGroup.rotation.y = yaw;
    chunkGroup.add(bridgeGroup);

    // Tall Steel Trestle Support Towers plunging down into the -42m Quarry Pit!
    for (const zFrac of [-0.62, 0.0, 0.62]) {
      const towerZ = zFrac * halfLength;
      const towerTopY = lerp(foreY, aftY, (zFrac + 1) * 0.5) - 0.5;
      const towerH = towerTopY - -41.0;
      for (const xLeg of [-halfWidth + 0.45, halfWidth - 0.45]) {
        const leg = new THREE.Mesh(this.unitBoxGeo, this.baggerSteelMat);
        leg.position.set(xLeg, -41.0 + towerH * 0.5, towerZ);
        leg.scale.set(0.62, towerH, 0.62);
        bridgeGroup.add(leg);
      }
    }

    // Tilted Conveyor Deck Group
    const slopeGroup = new THREE.Group();
    slopeGroup.position.set(0, midY, 0);
    slopeGroup.rotation.x = pitch;
    bridgeGroup.add(slopeGroup);

    // Steel Truss Frame + Moving Rubber Chevron Belt Surface!
    const frame = new THREE.Mesh(this.unitBoxGeo, this.baggerSteelMat);
    frame.position.set(0, -0.32, 0);
    frame.scale.set(halfWidth * 2.08, 0.52, slopeLen);
    frame.castShadow = true;
    slopeGroup.add(frame);

    const beltMat =
      beltSpeedForwardMps > 0
        ? this.conveyorBeltDownstreamMat
        : this.conveyorBeltUpstreamMat;
    const beltMesh = new THREE.Mesh(this.unitBoxGeo, beltMat);
    beltMesh.position.set(0, -0.02, 0);
    beltMesh.scale.set(halfWidth * 1.88, 0.12, slopeLen);
    beltMesh.receiveShadow = true;
    slopeGroup.add(beltMesh);

    // Side Truss Guardrails (`0.85m` tall steel conveyor skirts) + Spinning Idler Rollers!
    const rollerMeshes: THREE.Object3D[] = [];
    for (const side of [-1, 1]) {
      const skirt = new THREE.Mesh(this.unitBoxGeo, this.baggerYellowMat);
      skirt.position.set(side * (halfWidth + 0.18), 0.38, 0);
      skirt.scale.set(0.32, 0.78, slopeLen);
      slopeGroup.add(skirt);
    }

    for (const zEndPulley of [-slopeLen * 0.5, slopeLen * 0.5]) {
      const pulley = new THREE.Mesh(this.rollerCylGeo, this.hazardChevronMat);
      pulley.position.set(0, -0.15, zEndPulley);
      pulley.scale.set(halfWidth * 1.9, 2.2, 2.2);
      slopeGroup.add(pulley);
      rollerMeshes.push(pulley);
    }

    const boulders: ConveyorBoulderHazard[] = [];
    let crusherDrumLeft: THREE.Mesh | null = null;
    let crusherDrumRight: THREE.Mesh | null = null;

    if (withBouldersAndCrusher) {
      const rng = mulberry32(seed);
      for (let b = 0; b < 3; b++) {
        const radius = lerp(1.05, 1.32, rng());
        const lateralX = (b - 1) * 1.85 + (rng() - 0.5) * 0.4;
        const localZ = -halfLength * 0.7 + b * (halfLength * 0.65);
        const bMesh = new THREE.Mesh(this.boulderGeo, this.blastedRockMat);
        bMesh.scale.setScalar(radius);
        bMesh.castShadow = true;
        bMesh.position.set(lateralX, radius, localZ);
        slopeGroup.add(bMesh);

        boulders.push({
          localZ,
          lateralX,
          radius,
          rollAngle: rng() * 6.0,
          active: true,
          mesh: bMesh,
        });
      }

      // Industrial Rotary Jaw / Twin-Roll Rock-Crusher Hopper flanking the aft discharge (`+halfLength`)!
      const hopperGroup = new THREE.Group();
      hopperGroup.position.set(0, aftY, halfLength + 1.5);
      bridgeGroup.add(hopperGroup);

      for (const side of [-1, 1]) {
        const cheek = new THREE.Mesh(this.unitBoxGeo, this.baggerSteelMat);
        cheek.position.set(side * (halfWidth + 1.4), 1.8, 0);
        cheek.scale.set(1.8, 4.6, 5.4);
        cheek.castShadow = true;
        hopperGroup.add(cheek);

        const drum = new THREE.Mesh(this.wheelCylGeo, this.darkIronMat);
        drum.position.set(side * (halfWidth + 0.9), -0.9, 0);
        drum.rotation.z = Math.PI * 0.5;
        drum.scale.set(1.8, 1.15, 1.15);
        hopperGroup.add(drum);
        if (side < 0) crusherDrumLeft = drum;
        else crusherDrumRight = drum;
      }

      const archBeam = new THREE.Mesh(this.unitBoxGeo, this.hazardChevronMat);
      archBeam.position.set(0, 4.35, 0);
      archBeam.scale.set(halfWidth * 2.6, 0.85, 1.4);
      hopperGroup.add(archBeam);
    }

    return {
      centerX,
      centerZ,
      yaw,
      halfWidth,
      halfLength,
      aftY,
      foreY,
      beltSpeedForwardMps,
      boulders,
      rollerMeshes,
      crusherDrumLeft,
      crusherDrumRight,
    };
  }

  // ===========================================================================
  // 4. 400-TON CATERPILLAR-YELLOW MINING HAUL TRUCKS (`ROLLING STUNT RAMPS`!)
  // ===========================================================================

  private buildChunkMiningHaulTrucks(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    zLength: number,
    breachSide: -1 | 1,
    sampleCourseX: (z: number) => number
  ): MiningHaulTruckInstance[] {
    const trucks: MiningHaulTruckInstance[] = [];

    // Truck 1: Positioned right before `breachZStart` on `breachSide` so players can launch OFF its
    // tilted 400-ton yellow dump bed right over the 22m Pit Chasm Breach even BEFORE the dynamite slab drops!
    const zTruckBreach = zStart - zLength * 0.58; // right ahead of `0.64 * zLength` breach!
    trucks.push(
      this.createSingleHaulTruck(
        chunkGroup,
        breachSide * BENCH_OFFSET_X,
        zTruckBreach,
        4.2, // Patrols gently right in front of the breach lip!
        0.55,
        chunkIndex * 1.4,
        sampleCourseX
      )
    );

    // Truck 2: Rumbling along the opposite Quarry Haul Bench (`zStart - 0.30 * zLength`)!
    const zTruckOther = zStart - zLength * 0.3;
    trucks.push(
      this.createSingleHaulTruck(
        chunkGroup,
        -breachSide * BENCH_OFFSET_X,
        zTruckOther,
        10.5,
        0.48,
        chunkIndex * 2.3 + 1.1,
        sampleCourseX
      )
    );

    return trucks;
  }

  private createSingleHaulTruck(
    chunkGroup: THREE.Group,
    laneXOffset: number,
    homeZ: number,
    patrolAmpZ: number,
    patrolFreq: number,
    patrolPhase: number,
    sampleCourseX: (z: number) => number
  ): MiningHaulTruckInstance {
    const cx = sampleCourseX(homeZ);
    const cxAhead = sampleCourseX(homeZ - 4.0);
    const initYaw = Math.atan2(-(cxAhead - cx), 4.0);

    const halfWidth = 3.75; // 7.5m wide monster 400-ton dump truck!
    const halfLength = 8.4; // 16.8m long!
    const tailgateRelY = 0.08; // Flush rear entry ramp (`+2.08m` world Y)!
    const crestRelY = 5.15; // Soars to `+7.15m` world Y over the cab shield!

    const truckGroup = new THREE.Group();
    truckGroup.position.set(cx + laneXOffset, BENCH_DATUM_Y, homeZ);
    truckGroup.rotation.y = initYaw;
    chunkGroup.add(truckGroup);

    // 1. Heavy Chassis Frame & Radiator Grille + Operator Cab (`front = -halfLength`)!
    const chassis = new THREE.Mesh(this.unitBoxGeo, this.darkIronMat);
    chassis.position.set(0, 1.45, -1.2);
    chassis.scale.set(halfWidth * 1.55, 1.35, halfLength * 1.45);
    chassis.castShadow = true;
    truckGroup.add(chassis);

    const frontBumper = new THREE.Mesh(this.unitBoxGeo, this.haulTruckYellowMat);
    frontBumper.position.set(0, 1.85, -halfLength + 1.1);
    frontBumper.scale.set(halfWidth * 1.88, 2.1, 2.2);
    frontBumper.castShadow = true;
    truckGroup.add(frontBumper);

    const grille = new THREE.Mesh(this.unitBoxGeo, this.darkIronMat);
    grille.position.set(0, 2.0, -halfLength + 0.02);
    grille.scale.set(3.4, 1.65, 0.18);
    truckGroup.add(grille);

    // Left-Side Operator Cab tucked safely UNDER the raised dump-bed canopy visor (`y = 2.6..4.1m`)!
    const cab = new THREE.Mesh(this.unitBoxGeo, this.haulTruckYellowMat);
    cab.position.set(-2.15, 3.25, -halfLength + 2.1);
    cab.scale.set(2.3, 1.65, 2.8);
    truckGroup.add(cab);

    const windshield = new THREE.Mesh(this.unitBoxGeo, this.cabGlassMat);
    windshield.position.set(-2.15, 3.4, -halfLength + 0.68);
    windshield.scale.set(1.95, 0.95, 0.15);
    truckGroup.add(windshield);

    const strobe = new THREE.Mesh(this.sphereGeo, this.amberBeaconMat);
    strobe.position.set(2.4, 3.3, -halfLength + 1.2);
    strobe.scale.setScalar(1.3);
    truckGroup.add(strobe);

    // 2. 4 Colossal `3.9m`-Diameter (`radius = 1.95m`) Off-Highway Mining Tires!
    const wheelMeshes: THREE.Object3D[] = [];
    for (const zAxle of [-halfLength + 3.1, halfLength - 3.6]) {
      const axleGroup = new THREE.Group();
      axleGroup.position.set(0, 1.92, zAxle);
      for (const xSide of [-halfWidth + 0.65, halfWidth - 0.65]) {
        const tire = new THREE.Mesh(this.wheelCylGeo, this.darkIronMat);
        tire.position.set(xSide, 0, 0);
        tire.scale.set(1.38, 1.92, 1.92);
        tire.castShadow = true;
        axleGroup.add(tire);

        const rim = new THREE.Mesh(this.wheelCylGeo, this.haulTruckYellowMat);
        rim.position.set(xSide, 0, 0);
        rim.scale.set(1.44, 1.05, 1.05);
        axleGroup.add(rim);
      }
      truckGroup.add(axleGroup);
      wheelMeshes.push(axleGroup);
    }

    // Twin Telescopic Hydraulic Dump Cylinders propping up the tilted bed!
    for (const xCyl of [-1.45, 1.45]) {
      const cyl = new THREE.Mesh(this.unitBoxGeo, this.baggerSteelMat);
      cyl.position.set(xCyl, 2.65, -2.2);
      cyl.rotation.x = -0.28;
      cyl.scale.set(0.38, 3.8, 0.38);
      truckGroup.add(cyl);
    }

    // 3. Tilted Ribbed Steel 400-Ton Dump Bed (`Rolling Stunt Ramp` from `tailgateRelY` at `+halfLength` to `crestRelY` at `-halfLength`)!
    const bedRise = crestRelY - tailgateRelY;
    const bedRun = halfLength * 2.0;
    const bedPitch = Math.atan2(bedRise, bedRun);
    const bedHypot = Math.hypot(bedRun, bedRise);

    const dumpBed = new THREE.Mesh(this.unitBoxGeo, this.haulTruckYellowMat);
    dumpBed.position.set(0, (tailgateRelY + crestRelY) * 0.5 - 0.14, 0);
    dumpBed.rotation.x = bedPitch;
    dumpBed.scale.set(halfWidth * 2.0, 0.36, bedHypot);
    dumpBed.castShadow = true;
    dumpBed.receiveShadow = true;
    truckGroup.add(dumpBed);

    const centerChevron = new THREE.Mesh(this.unitBoxGeo, this.hazardChevronMat);
    centerChevron.position.set(0, (tailgateRelY + crestRelY) * 0.5 + 0.06, 0);
    centerChevron.rotation.x = bedPitch;
    centerChevron.scale.set(1.9, 0.05, bedHypot * 0.96);
    truckGroup.add(centerChevron);

    // Low flared side ribs along the dump bed!
    for (const side of [-1, 1]) {
      const sideRib = new THREE.Mesh(this.unitBoxGeo, this.haulTruckYellowMat);
      sideRib.position.set(
        side * (halfWidth - 0.22),
        (tailgateRelY + crestRelY) * 0.5 + 0.35,
        0
      );
      sideRib.rotation.x = bedPitch;
      sideRib.scale.set(0.42, 0.68, bedHypot * 0.92);
      truckGroup.add(sideRib);
    }

    return {
      laneXOffset,
      homeZ,
      patrolAmpZ,
      patrolFreq,
      patrolPhase,
      benchY: BENCH_DATUM_Y,
      halfWidth,
      halfLength,
      tailgateRelY,
      crestRelY,
      liveX: cx + laneXOffset,
      liveY: BENCH_DATUM_Y,
      liveZ: homeZ,
      liveYaw: initYaw,
      liveVelX: 0,
      liveVelZ: 0,
      truckGroup,
      wheelMeshes,
    };
  }

  // ===========================================================================
  // 5. SEQUENTIAL RIPPLING QUARRY-WALL BENCH DYNAMITE BLASTS & ROCK-RAMP SHORTCUTS
  // ===========================================================================

  private buildRipplingBenchBlast(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    side: -1 | 1,
    breachZStart: number,
    breachZEnd: number,
    sampleCourseX: (z: number) => number
  ): RipplingBenchBlastZone {
    const midZ = (breachZStart + breachZEnd) * 0.5;
    const cx = sampleCourseX(midZ);
    const cxAhead = sampleCourseX(midZ - 4.0);
    const yaw = Math.atan2(-(cxAhead - cx), 4.0);
    const benchCenterX = cx + side * BENCH_OFFSET_X;

    const breachLen = breachZStart - breachZEnd; // 22.4m
    const slabHalfLength = breachLen * 0.54; // Overlaps both lips of the breach by ~1.0m!
    const slabHalfWidth = BENCH_HALF_WIDTH; // 6.2m (`12.4m` wide!)

    // Hinge Pivot sits on the outer highwall edge (`benchCenterX + side * slabHalfWidth`) so when upright
    // it stands as a towering vertical limestone cliff monolith, and when blasted it topples inward across the breach!
    const slabPivotGroup = new THREE.Group();
    slabPivotGroup.position.set(benchCenterX + side * slabHalfWidth, BENCH_DATUM_Y, midZ);
    slabPivotGroup.rotation.y = yaw;
    chunkGroup.add(slabPivotGroup);

    const slabMesh = new THREE.Mesh(this.unitBoxGeo, this.blastedRockMat);
    // Extending `-side * slabHalfWidth` from the outer cliff hinge across the full `12.4m` bench width!
    slabMesh.position.set(-side * slabHalfWidth, -0.55, 0);
    slabMesh.scale.set(slabHalfWidth * 2.0, 1.35, slabHalfLength * 2.0);
    slabMesh.castShadow = true;
    slabMesh.receiveShadow = true;
    slabPivotGroup.add(slabMesh);

    // Chevron marker boulders along the blasted rock shortcut ramp!
    const rampStripe = new THREE.Mesh(this.unitBoxGeo, this.hazardChevronMat);
    rampStripe.position.set(-side * slabHalfWidth, 0.16, 0);
    rampStripe.scale.set(1.5, 0.08, slabHalfLength * 1.9);
    slabPivotGroup.add(rampStripe);

    // 4 Sequential Borehole Dynamite Charges + Additive Detonation Flash Spheres along the Highwall!
    const chargeMeshes: THREE.Mesh[] = [];
    const blastFlashMeshes: THREE.Mesh[] = [];
    const wallGroup = new THREE.Group();
    wallGroup.position.set(benchCenterX + side * (slabHalfWidth + 1.2), BENCH_DATUM_Y, midZ);
    wallGroup.rotation.y = yaw;
    chunkGroup.add(wallGroup);

    for (let c = 0; c < 4; c++) {
      const cz = lerp(slabHalfLength * 0.72, -slabHalfLength * 0.72, c / 3);
      const charge = new THREE.Mesh(this.sphereGeo, this.dynamiteRedGlowMat);
      charge.position.set(0, 3.8 + (c % 2) * 0.9, cz);
      charge.scale.setScalar(2.1);
      wallGroup.add(charge);
      chargeMeshes.push(charge);

      const flash = new THREE.Mesh(this.sphereGeo, this.blastFlashMat);
      flash.position.set(-side * 1.2, 3.8, cz);
      flash.scale.setScalar(0.1);
      flash.visible = false;
      wallGroup.add(flash);
      blastFlashMeshes.push(flash);
    }

    return {
      side,
      breachZStart,
      breachZEnd,
      centerX: benchCenterX,
      centerZ: midZ,
      yaw,
      benchY: BENCH_DATUM_Y,
      slabHalfWidth,
      slabHalfLength,
      period: 10.0,
      phase: chunkIndex * 2.4,
      firedChargesMask: 0,
      deployProgress: 1.0,
      isBridgeDeployed: true,
      slabPivotGroup,
      chargeMeshes,
      blastFlashMeshes,
    };
  }

  // ===========================================================================
  // LIVE SURFACE HEIGHT, MOVING BELT / SLEWING BOOM VELOCITY & 240HZ COLLISIONS
  // ===========================================================================

  private worldToLocalXZ(
    originX: number,
    originZ: number,
    yaw: number,
    wx: number,
    wz: number
  ): { lx: number; lz: number } {
    const dx = wx - originX;
    const dz = wz - originZ;
    const cosY = Math.cos(yaw);
    const sinY = Math.sin(yaw);
    return {
      lx: dx * cosY - dz * sinY,
      lz: dx * sinY + dz * cosY,
    };
  }

  public sampleLiveStripMineSurface(
    data: StripMineChunkData,
    x: number,
    z: number,
    referenceY?: number
  ): { active: boolean; height: number; waterDepth: number } {
    let bestH = -Infinity;
    let found = false;

    const acceptTier = (candidateY: number): boolean => {
      // Allow driving UNDERNEATH elevated Slewing Excavator Booms & High Conveyor Overpasses!
      if (referenceY !== undefined && candidateY > BENCH_DATUM_Y + 2.45) {
        if (referenceY < candidateY - 2.15) {
          return false;
        }
      }
      return true;
    };

    // 1. Slewing 68m `Bagger 288` Bucket-Wheel Excavator (Boarding Ramp + Turntable + Slewing Boom)!
    for (let i = 0; i < data.excavators.length; i++) {
      const ex = data.excavators[i];
      if (Math.abs(z - ex.liveZ) > ex.boomLength + 28.0) continue;

      // 1A: Crawling Turntable Deck (`z_local in [-9..+9]`) & Aft Boarding Ramp (`z_local in [+9..+21.5]`)
      const baseLoc = this.worldToLocalXZ(ex.liveX, ex.liveZ, ex.baseBoomYaw, x, z);
      if (Math.abs(baseLoc.lx) <= 7.2 && baseLoc.lz >= -9.0 && baseLoc.lz <= 9.0) {
        if (ex.deckY > bestH) {
          bestH = ex.deckY;
          found = true;
        }
      } else if (Math.abs(baseLoc.lx) <= 4.85 && baseLoc.lz > 8.6 && baseLoc.lz <= 21.2) {
        const rampT = clamp((21.1 - baseLoc.lz) / 12.3, 0, 1);
        const rampY = lerp(BENCH_DATUM_Y, ex.deckY, rampT);
        if (rampY > bestH) {
          bestH = rampY;
          found = true;
        }
      }

      // 1B: Slewing 54m Forward Steel Lattice Boom (`lz in [-boomLength - 0.8 .. +2.5]`)!
      const boomLoc = this.worldToLocalXZ(ex.liveX, ex.liveZ, ex.liveBoomYaw, x, z);
      if (
        Math.abs(boomLoc.lx) <= ex.boomHalfWidth &&
        boomLoc.lz <= 2.5 &&
        boomLoc.lz >= -ex.boomLength - 0.8
      ) {
        const tBoom = clamp(-boomLoc.lz / ex.boomLength, 0, 1);
        let boomY = ex.deckY + tBoom * ex.boomTipRelY;
        // Boom-head launch kick-ramp (`last 4.5m`)!
        if (boomLoc.lz < -ex.boomLength + 4.5) {
          const kickT = clamp((-ex.boomLength + 4.5 - boomLoc.lz) / 4.5, 0, 1);
          boomY += kickT * 0.72;
        }
        if (acceptTier(boomY) && boomY > bestH) {
          bestH = boomY;
          found = true;
        }
      }
    }

    // 2. Live Moving Conveyor-Belt Skybridges!
    for (let i = 0; i < data.conveyors.length; i++) {
      const cv = data.conveyors[i];
      if (Math.abs(z - cv.centerZ) > cv.halfLength + 12.0) continue;
      const loc = this.worldToLocalXZ(cv.centerX, cv.centerZ, cv.yaw, x, z);
      if (Math.abs(loc.lx) <= cv.halfWidth && Math.abs(loc.lz) <= cv.halfLength + 0.85) {
        const t = clamp((cv.halfLength - loc.lz) / (cv.halfLength * 2.0), 0, 1);
        const beltY = lerp(cv.aftY, cv.foreY, t);
        if (acceptTier(beltY) && beltY > bestH) {
          bestH = beltY;
          found = true;
        }
      }
    }

    // 3. 400-Ton Caterpillar-Yellow Mining Haul Trucks (`Rolling Stunt Ramps`!)
    for (let i = 0; i < data.haulTrucks.length; i++) {
      const tr = data.haulTrucks[i];
      if (Math.abs(z - tr.liveZ) > tr.halfLength + 4.0) continue;
      const loc = this.worldToLocalXZ(tr.liveX, tr.liveZ, tr.liveYaw, x, z);
      if (Math.abs(loc.lx) <= tr.halfWidth && Math.abs(loc.lz) <= tr.halfLength + 0.65) {
        const tRamp = clamp((tr.halfLength - loc.lz) / (tr.halfLength * 2.0), 0, 1);
        const bedY = tr.liveY + lerp(tr.tailgateRelY, tr.crestRelY, tRamp);
        if (acceptTier(bedY) && bedY > bestH) {
          bestH = bedY;
          found = true;
        }
      }
    }

    // 4. Sequential Rippling Bench Blast Hinged Rock-Ramp Shortcut!
    for (let i = 0; i < data.benchBlasts.length; i++) {
      const bb = data.benchBlasts[i];
      if (!bb.isBridgeDeployed) continue;
      if (Math.abs(z - bb.centerZ) > bb.slabHalfLength + 2.5) continue;
      const loc = this.worldToLocalXZ(bb.centerX, bb.centerZ, bb.yaw, x, z);
      if (Math.abs(loc.lx) <= bb.slabHalfWidth && Math.abs(loc.lz) <= bb.slabHalfLength) {
        // Slight upward launch pitch (`+0.85m` toward `-Z`) across the freshly blasted rock ramp!
        const tFwd = clamp((bb.slabHalfLength - loc.lz) / (bb.slabHalfLength * 2.0), 0, 1);
        const rockY = bb.benchY + tFwd * 0.85;
        if (rockY > bestH) {
          bestH = rockY;
          found = true;
        }
      }
    }

    // 5. Static Limestone Quarry Haul Benches, Starter Staging Deck & Bagger Cross-Causeways!
    if (z <= data.zStart + 0.6 && z >= data.zStart - data.zLength - 0.6) {
      const cx = data.sampleCourseX(z);
      const d = x - cx;

      // Starter staging causeway (`z > -28m`)
      if (z > -28.0 && Math.abs(d) <= 27.0) {
        if (BENCH_DATUM_Y > bestH) {
          bestH = BENCH_DATUM_Y;
          found = true;
        }
      }

      // Bagger 288 Center Spine Pad & Cross-Pit Causeway (`chunkIndex >= 0`)
      if (data.chunkIndex >= 0) {
        const baggerZ = data.zStart - data.zLength * 0.44;
        if (Math.abs(z - (baggerZ + 12.0)) <= 22.0 && Math.abs(d) <= 8.75) {
          if (BENCH_DATUM_Y > bestH) {
            bestH = BENCH_DATUM_Y;
            found = true;
          }
        }
        const entryCrossZ = baggerZ + 24.5;
        if (Math.abs(z - entryCrossZ) <= 5.75 && Math.abs(d) <= BENCH_OFFSET_X + 4.0) {
          if (BENCH_DATUM_Y > bestH) {
            bestH = BENCH_DATUM_Y;
            found = true;
          }
        }
      }

      // West (`-20.5m`) & East (`+20.5m`) Terraced Quarry Haul Benches!
      for (const side of [-1, 1] as const) {
        const inBreach =
          data.chunkIndex >= 0 &&
          side === data.breachSide &&
          z < data.breachZStart &&
          z > data.breachZEnd;
        if (!inBreach && Math.abs(d - side * BENCH_OFFSET_X) <= BENCH_HALF_WIDTH) {
          if (BENCH_DATUM_Y > bestH) {
            bestH = BENCH_DATUM_Y;
            found = true;
          }
        }
      }
    }

    if (found) {
      return { active: true, height: bestH, waterDepth: 0 };
    }

    return { active: false, height: -42.0, waterDepth: 0 };
  }

  /**
   * Feeds real `±14.0 m/s` Conveyor-Belt surface speed, `ω × r` Slewing Bagger-288 Boom velocity,
   * AND Rolling 400-Ton Haul Truck velocity directly into the 240Hz tire contact patch!
   */
  public sampleSurfaceVelocityInChunk(
    data: StripMineChunkData,
    x: number,
    z: number,
    outVel: THREE.Vector3
  ): boolean {
    // 1. Live `±14.0 m/s` Conveyor-Belt Skybridges!
    for (let i = 0; i < data.conveyors.length; i++) {
      const cv = data.conveyors[i];
      if (Math.abs(z - cv.centerZ) > cv.halfLength + 12.0) continue;
      const loc = this.worldToLocalXZ(cv.centerX, cv.centerZ, cv.yaw, x, z);
      if (Math.abs(loc.lx) <= cv.halfWidth && Math.abs(loc.lz) <= cv.halfLength + 0.85) {
        const fwdX = -Math.sin(cv.yaw);
        const fwdZ = -Math.cos(cv.yaw);
        outVel.set(
          fwdX * cv.beltSpeedForwardMps,
          0,
          fwdZ * cv.beltSpeedForwardMps
        );
        return true;
      }
    }

    // 2. Slewing 68m `Bagger 288` Boom (`v = v_crawl + ω × r`) & Crawling Turntable!
    for (let i = 0; i < data.excavators.length; i++) {
      const ex = data.excavators[i];
      if (Math.abs(z - ex.liveZ) > ex.boomLength + 26.0) continue;

      const boomLoc = this.worldToLocalXZ(ex.liveX, ex.liveZ, ex.liveBoomYaw, x, z);
      if (
        Math.abs(boomLoc.lx) <= ex.boomHalfWidth &&
        boomLoc.lz <= 2.5 &&
        boomLoc.lz >= -ex.boomLength - 0.8
      ) {
        // Point `(lx, lz)` rotated by `yaw`:
        // wx = liveX + lx * cosY + lz * sinY  => d(wx)/dt = (-lx * sinY + lz * cosY) * dYaw/dt
        // wz = liveZ - lx * sinY + lz * cosY  => d(wz)/dt = (-lx * cosY - lz * sinY) * dYaw/dt
        const cosY = Math.cos(ex.liveBoomYaw);
        const sinY = Math.sin(ex.liveBoomYaw);
        const w = ex.liveBoomAngularVelYaw;
        outVel.set(
          (-boomLoc.lx * sinY + boomLoc.lz * cosY) * w,
          0,
          ex.liveCrawlVelZ + (-boomLoc.lx * cosY - boomLoc.lz * sinY) * w
        );
        return true;
      }

      const baseLoc = this.worldToLocalXZ(ex.liveX, ex.liveZ, ex.baseBoomYaw, x, z);
      if (Math.abs(baseLoc.lx) <= 7.2 && baseLoc.lz >= -9.0 && baseLoc.lz <= 21.2) {
        outVel.set(0, 0, ex.liveCrawlVelZ);
        return true;
      }
    }

    // 3. Rolling 400-Ton Mining Haul Trucks!
    for (let i = 0; i < data.haulTrucks.length; i++) {
      const tr = data.haulTrucks[i];
      if (Math.abs(z - tr.liveZ) > tr.halfLength + 3.5) continue;
      const loc = this.worldToLocalXZ(tr.liveX, tr.liveZ, tr.liveYaw, x, z);
      if (Math.abs(loc.lx) <= tr.halfWidth && Math.abs(loc.lz) <= tr.halfLength + 0.65) {
        outVel.set(tr.liveVelX, 0, tr.liveVelZ);
        return true;
      }
    }

    return false;
  }

  /**
   * 240Hz Rigid-Body Collisions:
   *  - 20m Rotating Toothed Bucket Wheel at the `Bagger 288` Boom Head
   *  - Tumbling Blasted Limestone Ore Boulders on `-14 m/s` Conveyor Belts
   *  - Unblasted Vertical Quarry Cliff Monolith (before the Rippling Bench Blast topples it!)
   */
  public resolveStripMineCollisionsInChunk(
    data: StripMineChunkData,
    carPos: THREE.Vector3,
    forwardWorld: THREE.Vector3,
    rightWorld: THREE.Vector3,
    linearVel: THREE.Vector3,
    angularVel: THREE.Vector3,
    passiveObserver: boolean,
    onImpact?: (intensity: number, worldPoint: THREE.Vector3) => void
  ): void {
    if (passiveObserver) return;

    const hullOffsets = [-2.1, 0.0, 2.1];
    const sphereRad = 1.05;

    const pushLocalBox = (
      originX: number,
      originZ: number,
      yaw: number,
      boxLx: number,
      boxLz: number,
      halfW: number,
      halfL: number,
      extraVx = 0,
      extraVz = 0,
      extraLiftVy = 0
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
            nlx = slx >= boxLx ? 1 : -1;
            overlap = halfW + sphereRad;
          }

          const nx = nlx * cosY + nlz * sinY;
          const nz = -nlx * sinY + nlz * cosY;
          carPos.x += nx * overlap * 0.92;
          carPos.z += nz * overlap * 0.92;

          const relVx = linearVel.x - extraVx;
          const relVz = linearVel.z - extraVz;
          const vn = relVx * nx + relVz * nz;
          if (vn < 0) {
            const jMag = -1.3 * vn;
            linearVel.x += nx * jMag;
            linearVel.z += nz * jMag;
            if (extraLiftVy > 0) {
              linearVel.y = Math.max(linearVel.y, extraLiftVy);
            }
            const localHitX = nx * rightWorld.x + nz * rightWorld.z;
            angularVel.y += clamp(-zOff * localHitX * jMag * 0.08, -1.6, 1.6);

            if (Math.abs(vn) > 2.4 && onImpact) {
              this._hitContact.set(sx, carPos.y, sz);
              onImpact(clamp(Math.abs(vn) / 15.0, 0.28, 1.0), this._hitContact);
            }
          }
        }
      }
      return anyHit;
    };

    // 1. Colossal 20m Rotating Toothed Bucket Wheel (`radius = 10.0m`, `thickness = 2.4m`)!
    for (let i = 0; i < data.excavators.length; i++) {
      const ex = data.excavators[i];
      if (Math.abs(carPos.z - ex.wheelWorldPos.z) > ex.wheelRadius + 5.0) continue;
      if (Math.abs(carPos.y - ex.wheelWorldPos.y) > ex.wheelRadius + 1.2) continue;

      const hit = pushLocalBox(
        ex.liveX,
        ex.liveZ,
        ex.liveBoomYaw,
        ex.wheelLateralOffset,
        -ex.boomLength - 1.5,
        1.35,
        ex.wheelRadius * 0.88,
        -Math.sin(ex.liveBoomYaw) * 8.0,
        -Math.cos(ex.liveBoomYaw) * 8.0,
        6.5 // Upward bucket-tooth scoop toss!
      );
      if (hit && this.onBucketWheelImpact) {
        this.onBucketWheelImpact(carPos.clone());
      }
    }

    // 2. Tumbling Blasted Limestone Ore Boulders on Upstream Conveyors!
    for (let i = 0; i < data.conveyors.length; i++) {
      const cv = data.conveyors[i];
      if (cv.boulders.length === 0) continue;
      if (Math.abs(carPos.z - cv.centerZ) > cv.halfLength + 6.0) continue;

      const fwdX = -Math.sin(cv.yaw);
      const fwdZ = -Math.cos(cv.yaw);
      const bVx = fwdX * cv.beltSpeedForwardMps;
      const bVz = fwdZ * cv.beltSpeedForwardMps;

      for (let b = 0; b < cv.boulders.length; b++) {
        const bld = cv.boulders[b];
        if (!bld.active) continue;
        const t = clamp((cv.halfLength - bld.localZ) / (cv.halfLength * 2.0), 0, 1);
        const bWorldY = lerp(cv.aftY, cv.foreY, t) + bld.radius;
        if (Math.abs(carPos.y - bWorldY) > bld.radius + 1.35) continue;

        const hit = pushLocalBox(
          cv.centerX,
          cv.centerZ,
          cv.yaw,
          bld.lateralX,
          bld.localZ,
          bld.radius * 0.85,
          bld.radius * 0.85,
          bVx,
          bVz,
          3.2
        );
        if (hit && this.onOreBoulderImpact) {
          this.onOreBoulderImpact(carPos.clone());
        }
      }
    }

    // 3. Side Chassis Collisions against 400-Ton Mining Haul Trucks (if approached from the sides rather than rear ramp!)
    for (let i = 0; i < data.haulTrucks.length; i++) {
      const tr = data.haulTrucks[i];
      if (Math.abs(carPos.z - tr.liveZ) > tr.halfLength + 3.5) continue;
      const loc = this.worldToLocalXZ(tr.liveX, tr.liveZ, tr.liveYaw, carPos.x, carPos.z);
      const tRamp = clamp((tr.halfLength - loc.lz) / (tr.halfLength * 2.0), 0, 1);
      const bedWorldY = tr.liveY + lerp(tr.tailgateRelY, tr.crestRelY, tRamp);
      if (carPos.y < bedWorldY - 0.55 && loc.lz < tr.halfLength - 1.5) {
        pushLocalBox(
          tr.liveX,
          tr.liveZ,
          tr.liveYaw,
          0,
          -0.8,
          tr.halfWidth * 0.94,
          tr.halfLength * 0.82,
          tr.liveVelX,
          tr.liveVelZ
        );
      }
    }
  }

  // ===========================================================================
  // CHUNK & GLOBAL QUARRY ANIMATION STEP
  // ===========================================================================

  public stepChunkHazards(
    data: StripMineChunkData,
    dt: number,
    elapsed: number,
    sampleCourseX: (z: number) => number
  ): void {
    const clampedDt = Math.min(dt, 0.033);

    // Animate scrolling chevron conveyor belt UVs!
    if (this.conveyorBeltDownstreamMat.map) {
      this.conveyorBeltDownstreamMat.map.offset.y = -(elapsed * 1.6) % 1;
    }
    if (this.conveyorBeltUpstreamMat.map) {
      this.conveyorBeltUpstreamMat.map.offset.y = (elapsed * 1.6) % 1;
    }

    // 1. Step `Bagger 288` Crawling Undercarriage, Slewing 68m Boom & 20m Rotating Bucket Wheel!
    for (let i = 0; i < data.excavators.length; i++) {
      const ex = data.excavators[i];
      const crawlArg = elapsed * ex.crawlFreq + ex.crawlPhase;
      ex.liveZ = ex.baseZ + Math.sin(crawlArg) * ex.crawlAmpZ;
      ex.liveCrawlVelZ = Math.cos(crawlArg) * ex.crawlAmpZ * ex.crawlFreq;
      ex.liveX = sampleCourseX(ex.liveZ);

      const slewArg = elapsed * ex.slewFreq + ex.slewPhase;
      ex.liveBoomYaw = ex.baseBoomYaw + Math.sin(slewArg) * ex.slewAmpYaw;
      ex.liveBoomAngularVelYaw = Math.cos(slewArg) * ex.slewAmpYaw * ex.slewFreq;

      ex.undercarriageGroup.position.set(ex.liveX, 0, ex.liveZ);
      ex.slewSuperstructureGroup.position.set(ex.liveX, ex.deckY, ex.liveZ);
      ex.slewSuperstructureGroup.rotation.y = ex.liveBoomYaw;

      // Rotate 20m Toothed Bucket Wheel & Crawler Sprockets!
      ex.wheelSpinAngle += ex.wheelSpinSpeed * clampedDt;
      ex.bucketWheelMesh.rotation.x = -ex.wheelSpinAngle;

      for (let s = 0; s < ex.crawlerSprockets.length; s++) {
        ex.crawlerSprockets[s].rotation.x = -ex.liveZ * 0.9;
      }

      // Compute exact world position of the 20m Bucket Wheel center for HUD & 240Hz collisions!
      const cosY = Math.cos(ex.liveBoomYaw);
      const sinY = Math.sin(ex.liveBoomYaw);
      const wLx = ex.wheelLateralOffset;
      const wLz = -ex.boomLength - 1.5;
      ex.wheelWorldPos.set(
        ex.liveX + wLx * cosY + wLz * sinY,
        ex.deckY + ex.boomTipRelY + 1.8,
        ex.liveZ - wLx * sinY + wLz * cosY
      );
    }

    // 2. Step Conveyor Rollers, Crusher Drums & Tumbling Blasted Ore Boulders!
    for (let i = 0; i < data.conveyors.length; i++) {
      const cv = data.conveyors[i];
      const spinSign = Math.sign(cv.beltSpeedForwardMps);
      for (let r = 0; r < cv.rollerMeshes.length; r++) {
        cv.rollerMeshes[r].rotation.x = -spinSign * elapsed * 14.0;
      }
      if (cv.crusherDrumLeft) cv.crusherDrumLeft.rotation.y = elapsed * 8.5;
      if (cv.crusherDrumRight) cv.crusherDrumRight.rotation.y = -elapsed * 8.5;

      if (cv.boulders.length > 0) {
        const span = cv.halfLength * 2.0;
        for (let b = 0; b < cv.boulders.length; b++) {
          const bld = cv.boulders[b];
          // Upstream belt (`beltSpeedForwardMps < 0`) moves boulders toward `+localZ` (`aft` into the crusher hopper!)
          bld.localZ += -cv.beltSpeedForwardMps * clampedDt * 0.65;
          bld.rollAngle += (Math.abs(cv.beltSpeedForwardMps) * clampedDt * 0.65) / bld.radius;

          if (bld.localZ > cv.halfLength + 1.2) {
            bld.localZ -= span + 2.0;
          } else if (bld.localZ < -cv.halfLength - 1.2) {
            bld.localZ += span + 2.0;
          }
          bld.active = Math.abs(bld.localZ) <= cv.halfLength;
          bld.mesh.position.set(bld.lateralX, bld.radius, bld.localZ);
          bld.mesh.rotation.x = bld.rollAngle;
        }
      }
    }

    // 3. Step 400-Ton Caterpillar-Yellow Mining Haul Trucks!
    for (let i = 0; i < data.haulTrucks.length; i++) {
      const tr = data.haulTrucks[i];
      const arg = elapsed * tr.patrolFreq + tr.patrolPhase;
      const newZ = tr.homeZ + Math.sin(arg) * tr.patrolAmpZ;
      const velZ = Math.cos(arg) * tr.patrolAmpZ * tr.patrolFreq;
      const cx = sampleCourseX(newZ);
      const cxAhead = sampleCourseX(newZ - 3.5);
      const dxdz = (cxAhead - cx) / -3.5;

      tr.liveZ = newZ;
      tr.liveX = cx + tr.laneXOffset;
      tr.liveYaw = Math.atan2(-(cxAhead - cx), 3.5);
      tr.liveVelZ = velZ;
      tr.liveVelX = dxdz * velZ;

      tr.truckGroup.position.set(tr.liveX, tr.liveY, tr.liveZ);
      tr.truckGroup.rotation.y = tr.liveYaw;

      const tireSpin = -newZ / 1.92;
      for (let w = 0; w < tr.wheelMeshes.length; w++) {
        tr.wheelMeshes[w].rotation.x = tireSpin;
      }
    }

    // 4. Step Sequential 1-2-3-4 Rippling Quarry-Wall Bench Dynamite Blasts & Hinged Rock-Ramp Shortcut!
    for (let i = 0; i < data.benchBlasts.length; i++) {
      const bb = data.benchBlasts[i];
      const u = ((((elapsed + bb.phase) % bb.period) + bb.period) % bb.period) / bb.period;
      // Cycle (`10.0s`):
      //   u in [0.00 .. 0.14): Upright cliff face (`deployProgress = 0`), armed red charges!
      //   u in [0.14 .. 0.24): 1-2-3-4 Sequential Ripple Detonation (`charges 0, 1, 2, 3` fire 0.22s apart!)
      //   u in [0.18 .. 0.30): Cliff monolith topples inward (`hinge angle 1.32 -> 0.0`) onto breach!
      //   u in [0.30 .. 0.88): Fresh Blasted Rock-Ramp Shortcut deployed (`isBridgeDeployed = true` for `5.8s`!)
      //   u in [0.88 .. 1.00): Resets smoothly for next quarry blast cycle.
      if (u < 0.14) {
        bb.firedChargesMask = 0;
        bb.deployProgress = 0;
      } else if (u < 0.88) {
        for (let c = 0; c < 4; c++) {
          const fireU = 0.14 + c * 0.024;
          const bit = 1 << c;
          if (u >= fireU && (bb.firedChargesMask & bit) === 0) {
            bb.firedChargesMask |= bit;
            if (this.onBenchDynamiteBlast) {
              this.onBenchDynamiteBlast(
                new THREE.Vector3(
                  bb.centerX + bb.side * bb.slabHalfWidth,
                  bb.benchY + 3.8,
                  bb.centerZ + lerp(bb.slabHalfLength * 0.7, -bb.slabHalfLength * 0.7, c / 3)
                ),
                c
              );
            }
          }
          const flashAge = (u - fireU) * bb.period;
          const flash = bb.blastFlashMeshes[c];
          if (flashAge >= 0 && flashAge < 0.42) {
            flash.visible = true;
            flash.scale.setScalar(lerp(13.5, 0.8, flashAge / 0.42));
          } else {
            flash.visible = false;
          }
          bb.chargeMeshes[c].visible = u < fireU;
        }

        bb.deployProgress = smoothstep(0.18, 0.3, u);
      } else {
        bb.firedChargesMask = 0;
        for (let c = 0; c < 4; c++) {
          bb.chargeMeshes[c].visible = true;
          bb.blastFlashMeshes[c].visible = false;
        }
        bb.deployProgress = 1.0 - smoothstep(0.88, 1.0, u);
      }

      bb.isBridgeDeployed = bb.deployProgress > 0.82;
      // Rotate around outer cliff hinge: `0` = flat drivable bridge, `side * 1.35 rad` = vertical highwall slab!
      const uprightTilt = (1.0 - bb.deployProgress) * 1.35;
      bb.slabPivotGroup.rotation.set(0.038 * bb.deployProgress, bb.yaw, -bb.side * uprightTilt);
    }
  }

  public stepGlobalStripMineAtmosphere(
    active: boolean,
    focusPos: THREE.Vector3,
    elapsed: number,
    dt: number
  ): void {
    this.atmosphereFxGroup.visible = active;
    if (!active) return;

    const clampedDt = Math.min(dt, 0.05);
    this.sumpLakeMesh.position.x = focusPos.x;
    this.sumpLakeMesh.position.z = Math.floor(focusPos.z / 24.0) * 24.0 - 95.0;
    this.sumpLakeMat.uniforms.uTime.value = elapsed;

    const posAttr = this.dustParticles.geometry.getAttribute('position') as THREE.BufferAttribute;
    for (let i = 0; i < this.dustCount; i++) {
      let px = this.dustPositions[i * 3 + 0];
      let py = this.dustPositions[i * 3 + 1];
      let pz = this.dustPositions[i * 3 + 2];

      px += Math.sin(elapsed * 0.9 + i) * 3.2 * clampedDt;
      py += 1.4 * clampedDt;
      pz += 6.5 * clampedDt;

      if (pz > focusPos.z + 55.0) pz -= 240.0;
      else if (pz < focusPos.z - 185.0) pz += 240.0;

      if (px > focusPos.x + 58.0) px -= 116.0;
      else if (px < focusPos.x - 58.0) px += 116.0;

      if (py > 32.0) py = -8.0;

      this.dustPositions[i * 3 + 0] = px;
      this.dustPositions[i * 3 + 1] = py;
      this.dustPositions[i * 3 + 2] = pz;
    }
    posAttr.needsUpdate = true;
  }
}

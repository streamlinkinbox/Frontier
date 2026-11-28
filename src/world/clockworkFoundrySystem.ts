import * as THREE from 'three';
import { ProceduralTextureLibrary } from '../graphics/proceduralTextures';
import { clamp, lerp, mulberry32, smoothstep } from '../math/noise';
import { CrisscrossRootBridge, CrisscrossRootSegment, WorldObstacle } from './trailCorridor';

export interface ClockworkGearIsland {
  x: number;
  z: number;
  radius: number;
  baseY: number;
  currentY: number;
  velY: number;
  /** Angular velocity around +Y (rad/s). 0 if reciprocating Steam-Piston island! */
  omega: number;
  angle: number;
  /** Inner radius that stays stationary (used on Row 0 Spawn Master Hub so spawn is calm!) */
  stationaryInnerRadius: number;
  isPiston: boolean;
  pistonStrokeY: number;
  pistonPhase: number;
  wasAtTop: boolean;
  rowIdx: number;
  islandIdx: number;
  mesh: THREE.Mesh;
  hubCapMesh: THREE.Mesh | null;
}

export interface DrivenShaftOrGearVisual {
  obj: THREE.Object3D;
  omega: number;
  angle: number;
  axis: 'X' | 'Y' | 'Z';
}

export interface SeesawBeamBridge {
  ax: number;
  ay: number;
  az: number;
  bx: number;
  by: number;
  bz: number;
  midX: number;
  midZ: number;
  midBaseY: number;
  pivotLift: number;
  dirX: number;
  dirZ: number;
  rightX: number;
  rightZ: number;
  yaw: number;
  length: number;
  halfLen: number;
  halfWidth: number;
  baseSlope: number;
  /** Live tangent of seesaw tilt in [-maxTan, +maxTan]: +maxTan = entry flush on A, -maxTan = exit flush on B! */
  tanTilt: number;
  velTan: number;
  maxTan: number;
  seatedExit: boolean;
  beamMesh: THREE.Group;
}

export interface ArticulatedPistonGangway {
  srcIsland: ClockworkGearIsland;
  dstIsland: ClockworkGearIsland;
  srcOffX: number;
  srcOffZ: number;
  dstOffX: number;
  dstOffZ: number;
  ax: number;
  az: number;
  bx: number;
  bz: number;
  dirX: number;
  dirZ: number;
  rightX: number;
  rightZ: number;
  yaw: number;
  length: number;
  halfWidth: number;
  liveAy: number;
  liveBy: number;
  bridgeGroup: THREE.Group;
}

/**
 * Intermittent Geneva Locking-Gear Pathway:
 * - One Brass Drive Gear (`driverMesh`) turns NON-STOP (`driverOmega`), driving a giant
 *   Open-Ring Spoked Bridge-Gear (`lockingGearGroup`)!
 * - During the LOCKED phase (~62% of cycle), a heavy Cast-Iron Ratchet Pawl locks the
 *   diametral bridge beam dead-flush between Entry Pier and Exit Pier so cars can cross!
 * - During the INDEXING phase (~38% of cycle), the pawl lifts and the continuously turning
 *   Drive Gear rotates the Bridge-Gear by 180° (`PI` rad) across the open molten pit!
 */
export interface LockingGearPathway {
  ax: number;
  ay: number;
  az: number;
  bx: number;
  by: number;
  bz: number;
  midX: number;
  midY: number;
  midZ: number;
  dirX: number;
  dirZ: number;
  rightX: number;
  rightZ: number;
  baseYaw: number;
  spanLen: number;
  approachLen: number;
  lockR: number;
  beamHalfWidth: number;
  period: number;
  phase: number;
  /** Current world rotation.y of lockingGearGroup */
  currentYaw: number;
  /** Live angular velocity (rad/s) — 0 when mechanically LOCKED! */
  liveOmega: number;
  isLocked: boolean;
  wasLocked: boolean;
  lockingGearGroup: THREE.Group;
  driverGroup: THREE.Group;
  driverOmega: number;
  driverAngle: number;
  pawlPivot: THREE.Group;
  beaconMeshes: THREE.Mesh[];
}

/**
 * Colossal Vertical Rotating Slotted Gear-Wall (`27m` diameter!):
 * - Stands upright like a towering rotating steel/brass wall slicing across a wide riveted
 *   foundry girder causeway, driven continuously by a meshing side Brass Pinion Gear!
 * - Cut through the solid cast-iron gear web are `4` narrow single-vehicle passages
 *   (`~4.8m` wide = room for 1 vehicle at a time!). When a slot sweeps across the road deck,
 *   1 car can thread through; otherwise the solid iron gear wall blocks passage!
 */
export interface VerticalSlottedGearWall {
  ax: number;
  ay: number;
  az: number;
  bx: number;
  by: number;
  bz: number;
  wallX: number;
  wallZ: number;
  deckY: number;
  axleY: number;
  dirX: number;
  dirZ: number;
  rightX: number;
  rightZ: number;
  yaw: number;
  spanLen: number;
  causewayHalfWidth: number;
  outerRadius: number;
  slotInnerR: number;
  slotOuterR: number;
  slotHalfAng: number;
  halfThick: number;
  omega: number;
  angle: number;
  gearWheelGroup: THREE.Group;
  pinionMesh: THREE.Mesh;
  pinionOmega: number;
  pinionAngle: number;
}

export type ClockworkSpanKind =
  | 'LOCKING_GEAR_BRIDGE'
  | 'GEAR_WALL_CAUSEWAY'
  | 'SEESAW_BRIDGE'
  | 'PISTON_GANGWAY'
  | 'HIGH_ARCH_CATWALK'
  | 'FOUNDRY_JUMP_RAMP';

export interface ClockworkGraphLink {
  fromIdx: number;
  toIdx: number;
  kind: ClockworkSpanKind;
  archBumpY: number;
}

export interface ClockworkChunkData {
  group: THREE.Group;
  islands: ClockworkGearIsland[];
  ownedIslands: ClockworkGearIsland[];
  drivenVisuals: DrivenShaftOrGearVisual[];
  seesaws: SeesawBeamBridge[];
  gangways: ArticulatedPistonGangway[];
  lockingPathways: LockingGearPathway[];
  gearWalls: VerticalSlottedGearWall[];
  zStart: number;
  zLength: number;
}

const ROW_SPACING_Z = 58.0;
const FIRST_ROW_Z_OFFSET = 12.0;
// Multi-gear branching & re-convergence rhythm: 1 -> 3 -> 4 -> 2 -> 3 -> 2 -> (repeat)!
const ROW_GEAR_COUNTS = [1, 3, 4, 2, 3, 2] as const;

export class ClockworkFoundryBuilder {
  public readonly atmosphereFxGroup = new THREE.Group();

  // Materials
  private readonly brassGearMat: THREE.MeshStandardMaterial;
  private readonly castIronMat: THREE.MeshStandardMaterial;
  private readonly darkGantryIronMat: THREE.MeshStandardMaterial;
  private readonly copperBoilerMat: THREE.MeshStandardMaterial;
  private readonly seesawDeckMat: THREE.MeshStandardMaterial;
  private readonly rampChevronMat: THREE.MeshStandardMaterial;
  private readonly furnaceCoreGlowMat: THREE.MeshBasicMaterial;
  private readonly moltenPourMat: THREE.MeshBasicMaterial;
  private readonly rampBeaconGlowMat: THREE.MeshBasicMaterial;
  private readonly lockReadyGlowMat: THREE.MeshBasicMaterial;
  private readonly lockTurningGlowMat: THREE.MeshBasicMaterial;
  private readonly moltenSlagSheetMat: THREE.MeshBasicMaterial;

  // Shared Geometries
  private readonly cogwheel24Geo: THREE.BufferGeometry;
  private readonly cogwheel16Geo: THREE.BufferGeometry;
  private readonly openLockingRingGeo: THREE.BufferGeometry;
  private readonly verticalOuterToothedRingGeo: THREE.BufferGeometry;
  private readonly verticalSolidWallSectorGeo: THREE.BufferGeometry;
  private readonly pistonIslandGeo: THREE.BufferGeometry;
  private readonly hubMedallionGeo: THREE.CylinderGeometry;
  private readonly shaftCylGeo: THREE.CylinderGeometry;
  private readonly unitBoxGeo: THREE.BoxGeometry;
  private readonly trunnionCylGeo: THREE.CylinderGeometry;
  private readonly verticalHubCylGeo: THREE.CylinderGeometry;
  private readonly beaconSphereGeo: THREE.SphereGeometry;

  // Global Molten Slag Glow Sheets & Rising Golden Forge Embers
  private readonly slagPlanes: THREE.Mesh[] = [];
  private readonly emberCount = 1100;
  private emberPoints!: THREE.Points;
  private emberPositions!: Float32Array;
  private emberVelocities!: Float32Array;

  // Callbacks
  public onSeesawAnvilSeat: ((pos: THREE.Vector3) => void) | null = null;
  public onGearPathwayLocked: ((pos: THREE.Vector3) => void) | null = null;
  public onPistonSteamVent: ((pos: THREE.Vector3) => void) | null = null;

  private readonly _dummy = new THREE.Object3D();
  private readonly _euler = new THREE.Euler();
  private readonly _hitContact = new THREE.Vector3();

  constructor(textures: ProceduralTextureLibrary) {
    this.atmosphereFxGroup.name = 'ClockworkFoundryAtmosphereFX';

    this.brassGearMat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.8, 0.8),
      roughness: 0.32,
      metalness: 0.82,
      vertexColors: true,
      side: THREE.DoubleSide,
    });

    this.castIronMat = new THREE.MeshStandardMaterial({
      color: 0x3e434b,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.95, 0.95),
      roughness: 0.44,
      metalness: 0.78,
    });

    this.darkGantryIronMat = new THREE.MeshStandardMaterial({
      color: 0x2a2d33,
      map: textures.rock.map,
      roughness: 0.52,
      metalness: 0.76,
    });

    this.copperBoilerMat = new THREE.MeshStandardMaterial({
      color: 0xc46d3b,
      map: textures.rock.map,
      roughness: 0.34,
      metalness: 0.8,
      vertexColors: true,
      side: THREE.DoubleSide,
    });

    this.seesawDeckMat = new THREE.MeshStandardMaterial({
      color: 0x5c534b,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      roughness: 0.5,
      metalness: 0.64,
    });

    this.rampChevronMat = new THREE.MeshStandardMaterial({
      color: 0xe8a829,
      map: textures.rock.map,
      roughness: 0.36,
      metalness: 0.62,
      emissive: new THREE.Color(0x804200),
      emissiveIntensity: 0.55,
    });

    this.furnaceCoreGlowMat = new THREE.MeshBasicMaterial({
      color: 0xff7b1c,
    });

    this.moltenPourMat = new THREE.MeshBasicMaterial({
      color: 0xffa638,
    });

    this.rampBeaconGlowMat = new THREE.MeshBasicMaterial({
      color: 0xffe066,
    });

    this.lockReadyGlowMat = new THREE.MeshBasicMaterial({
      color: 0x7dff78, // Crisp emerald-brass "GEAR LOCKED — CLEAR TO CROSS" signal!
    });

    this.lockTurningGlowMat = new THREE.MeshBasicMaterial({
      color: 0xff471a, // Fiery furnace-red "GEAR INDEXING / UNLOCKED" warning!
    });

    const slagTex = this.createMoltenSlagGlowTexture();
    this.moltenSlagSheetMat = new THREE.MeshBasicMaterial({
      map: slagTex,
      color: 0xff6a14,
      transparent: true,
      opacity: 0.58,
      depthWrite: false,
      side: THREE.DoubleSide,
    });

    this.cogwheel24Geo = this.createBrassCogwheelGeo(24);
    this.cogwheel16Geo = this.createBrassCogwheelGeo(16);
    this.openLockingRingGeo = this.createOpenLockingRingGeo(24);
    this.verticalOuterToothedRingGeo = this.createVerticalOuterToothedRingGeo(
      11.2,
      13.6,
      14.85,
      1.15,
      32
    );
    this.verticalSolidWallSectorGeo = this.createVerticalWallSectorGeo(
      4.3,
      11.3,
      0.34,
      Math.PI * 0.5 - 0.34,
      1.15
    );
    this.pistonIslandGeo = this.createSteamPistonGeo();

    this.hubMedallionGeo = new THREE.CylinderGeometry(1.0, 1.04, 0.48, 24);
    this.hubMedallionGeo.translate(0, -0.22, 0);

    this.shaftCylGeo = new THREE.CylinderGeometry(0.42, 0.42, 1.0, 12);
    this.unitBoxGeo = new THREE.BoxGeometry(1, 1, 1);

    this.trunnionCylGeo = new THREE.CylinderGeometry(0.72, 0.72, 1.0, 16);
    this.trunnionCylGeo.rotateZ(Math.PI * 0.5);

    this.verticalHubCylGeo = new THREE.CylinderGeometry(4.45, 4.45, 2.65, 28);
    this.verticalHubCylGeo.rotateX(Math.PI * 0.5);

    this.beaconSphereGeo = new THREE.SphereGeometry(0.3, 8, 6);

    this.initMoltenFoundryAtmosphere();
  }

  private createMoltenSlagGlowTexture(): THREE.CanvasTexture {
    const canvas = document.createElement('canvas');
    canvas.width = 128;
    canvas.height = 128;
    const ctx = canvas.getContext('2d')!;
    ctx.clearRect(0, 0, 128, 128);
    const grad = ctx.createRadialGradient(64, 64, 6, 64, 64, 62);
    grad.addColorStop(0.0, 'rgba(255, 215, 110, 0.92)');
    grad.addColorStop(0.45, 'rgba(255, 115, 24, 0.62)');
    grad.addColorStop(1.0, 'rgba(140, 32, 4, 0.0)');
    ctx.fillStyle = grad;
    ctx.fillRect(0, 0, 128, 128);
    const tex = new THREE.CanvasTexture(canvas);
    tex.needsUpdate = true;
    return tex;
  }

  /**
   * Sculpted 3D Involute Brass & Cast-Iron Cogwheel Island Geometry
   */
  private createBrassCogwheelGeo(toothCount: number): THREE.BufferGeometry {
    const segsPerTooth = 4;
    const radialSegs = toothCount * segsPerTooth;
    const rings = [
      { y: 0.0, r: 0.0, toothAmp: 0.0, col: 0x4b5059 },
      { y: 0.0, r: 0.28, toothAmp: 0.0, col: 0xc28a38 },
      { y: 0.0, r: 0.72, toothAmp: 0.0, col: 0x464b54 },
      { y: 0.0, r: 0.99, toothAmp: 0.0, col: 0xdea447 },
      { y: -0.07, r: 1.0, toothAmp: 0.11, col: 0xe8b154 },
      { y: -1.55, r: 1.0, toothAmp: 0.11, col: 0x9c6c28 },
      { y: -1.78, r: 0.93, toothAmp: 0.0, col: 0x3e424a },
      { y: -3.4, r: 0.26, toothAmp: 0.0, col: 0x2f333b },
      { y: -48.0, r: 0.17, toothAmp: 0.0, col: 0xff6e1a },
    ];

    const cols = radialSegs + 1;
    const rows = rings.length;
    const pos = new Float32Array(cols * rows * 3);
    const col = new Float32Array(cols * rows * 3);
    const uv = new Float32Array(cols * rows * 2);
    const indices: number[] = [];
    const tmp = new THREE.Color();

    let pPtr = 0;
    let uvPtr = 0;
    for (let r = 0; r < rows; r++) {
      const sp = rings[r];
      tmp.setHex(sp.col);
      for (let c = 0; c < cols; c++) {
        const u = c / radialSegs;
        const theta = u * Math.PI * 2;
        const toothWave = Math.cos(theta * toothCount);
        const toothOut = sp.toothAmp * smoothstep(-0.25, 0.35, toothWave);
        const rad = sp.r + toothOut;

        const nx = Math.cos(theta);
        const nz = Math.sin(theta);
        pos[pPtr] = nx * rad;
        pos[pPtr + 1] = sp.y;
        pos[pPtr + 2] = nz * rad;

        uv[uvPtr++] = r <= 4 ? nx * rad * 2.2 : u * toothCount * 0.5;
        uv[uvPtr++] = r <= 4 ? nz * rad * 2.2 : sp.y * 0.25;

        let spokeShade = 1.0;
        if (r === 2) {
          const spokeCos = Math.abs(Math.cos(theta * 4.0));
          spokeShade = spokeCos > 0.76 ? 1.38 : 0.72;
        }
        col[pPtr] = clamp(tmp.r * spokeShade, 0, 1);
        col[pPtr + 1] = clamp(tmp.g * spokeShade, 0, 1);
        col[pPtr + 2] = clamp(tmp.b * spokeShade, 0, 1);
        pPtr += 3;
      }
    }

    for (let r = 0; r < rows - 1; r++) {
      for (let c = 0; c < radialSegs; c++) {
        const a = r * cols + c;
        const b = a + 1;
        const d = a + cols;
        const e = d + 1;
        indices.push(a, b, e, a, e, d);
      }
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    geo.setAttribute('color', new THREE.BufferAttribute(col, 3));
    geo.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
    geo.setIndex(indices);
    geo.computeVertexNormals();
    return geo;
  }

  /**
   * Open-Center Hollow Toothed Brass Gear Ring (`r = 0.85 .. 1.00`, teeth out to `1.11`, open molten void inside!)
   * Used for Geneva Intermittent Locking-Gear Pathways so only the Diametral Bridge-Beam crosses the center!
   */
  private createOpenLockingRingGeo(toothCount: number): THREE.BufferGeometry {
    const radialSegs = toothCount * 4;
    const rings = [
      { y: -1.35, r: 0.85, toothAmp: 0.0, col: 0x3b3f47 }, // Inner wall bottom
      { y: 0.0, r: 0.85, toothAmp: 0.0, col: 0xc9913d }, // Inner top rim
      { y: 0.0, r: 0.99, toothAmp: 0.0, col: 0xe3ac4f }, // Outer top brass ring (+Y normal!)
      { y: -0.05, r: 1.0, toothAmp: 0.11, col: 0xebba60 }, // Involute tooth top
      { y: -1.42, r: 1.0, toothAmp: 0.11, col: 0x996926 }, // Involute tooth bottom
      { y: -1.52, r: 0.85, toothAmp: 0.0, col: 0x3b3f47 }, // Bottom close
    ];

    const cols = radialSegs + 1;
    const rows = rings.length;
    const pos = new Float32Array(cols * rows * 3);
    const col = new Float32Array(cols * rows * 3);
    const uv = new Float32Array(cols * rows * 2);
    const indices: number[] = [];
    const tmp = new THREE.Color();

    let pPtr = 0;
    let uvPtr = 0;
    for (let r = 0; r < rows; r++) {
      const sp = rings[r];
      tmp.setHex(sp.col);
      for (let c = 0; c < cols; c++) {
        const u = c / radialSegs;
        const theta = u * Math.PI * 2;
        const toothOut = sp.toothAmp * smoothstep(-0.25, 0.35, Math.cos(theta * toothCount));
        const rad = sp.r + toothOut;
        const nx = Math.cos(theta);
        const nz = Math.sin(theta);

        pos[pPtr] = nx * rad;
        pos[pPtr + 1] = sp.y;
        pos[pPtr + 2] = nz * rad;
        uv[uvPtr++] = u * toothCount * 0.5;
        uv[uvPtr++] = sp.r * 2.0 + sp.y * 0.3;
        col[pPtr] = tmp.r;
        col[pPtr + 1] = tmp.g;
        col[pPtr + 2] = tmp.b;
        pPtr += 3;
      }
    }

    for (let r = 0; r < rows - 1; r++) {
      for (let c = 0; c < radialSegs; c++) {
        const a = r * cols + c;
        const b = a + 1;
        const d = a + cols;
        const e = d + 1;
        indices.push(a, b, e, a, e, d);
      }
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    geo.setAttribute('color', new THREE.BufferAttribute(col, 3));
    geo.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
    geo.setIndex(indices);
    geo.computeVertexNormals();
    return geo;
  }

  /**
   * Continuous Outer Toothed Brass Ring in XY plane (`z in [-halfThick, +halfThick]`)
   * for the Colossal `27m`-Tall Vertical Rotating Gear-Wall!
   */
  private createVerticalOuterToothedRingGeo(
    innerR: number,
    outerR: number,
    toothTipR: number,
    halfThick: number,
    toothCount: number
  ): THREE.BufferGeometry {
    const segs = toothCount * 4;
    // Cross-section loop around the ring in (r, z) so front, back, inner arch & outer teeth are 100% solid!
    const loop = [
      { r: innerR, z: halfThick, amp: 0, col: 0xc9903b },
      { r: outerR, z: halfThick, amp: toothTipR - outerR, col: 0xe5ae50 },
      { r: outerR, z: -halfThick, amp: toothTipR - outerR, col: 0xb88232 },
      { r: innerR, z: -halfThick, amp: 0, col: 0x996926 },
      { r: innerR, z: halfThick, amp: 0, col: 0xc9903b },
    ];

    const cols = segs + 1;
    const rows = loop.length;
    const pos = new Float32Array(cols * rows * 3);
    const col = new Float32Array(cols * rows * 3);
    const uv = new Float32Array(cols * rows * 2);
    const indices: number[] = [];
    const tmp = new THREE.Color();

    let pPtr = 0;
    let uvPtr = 0;
    for (let r = 0; r < rows; r++) {
      const lp = loop[r];
      tmp.setHex(lp.col);
      for (let c = 0; c < cols; c++) {
        const u = c / segs;
        const theta = u * Math.PI * 2;
        const tooth = lp.amp * smoothstep(-0.22, 0.34, Math.cos(theta * toothCount));
        const rad = lp.r + tooth;
        pos[pPtr] = Math.cos(theta) * rad;
        pos[pPtr + 1] = Math.sin(theta) * rad;
        pos[pPtr + 2] = lp.z;
        uv[uvPtr++] = u * toothCount * 0.5;
        uv[uvPtr++] = r * 0.5;
        col[pPtr] = tmp.r;
        col[pPtr + 1] = tmp.g;
        col[pPtr + 2] = tmp.b;
        pPtr += 3;
      }
    }

    for (let r = 0; r < rows - 1; r++) {
      for (let c = 0; c < segs; c++) {
        const a = r * cols + c;
        const b = a + 1;
        const d = a + cols;
        const e = d + 1;
        indices.push(a, e, b, a, d, e);
      }
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    geo.setAttribute('color', new THREE.BufferAttribute(col, 3));
    geo.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
    geo.setIndex(indices);
    geo.computeVertexNormals();
    return geo;
  }

  /**
   * Watertight 3D Extruded Annular Sector (`theta in [angStart, angEnd]`, `r in [rInner, rOuter]`, `z in [-halfThick, +halfThick]`)
   * forming the 4 Solid Cast-Iron Wall Quadrants between the 4 Single-Vehicle Passages!
   */
  private createVerticalWallSectorGeo(
    rInner: number,
    rOuter: number,
    angStart: number,
    angEnd: number,
    halfThick: number
  ): THREE.BufferGeometry {
    const arcSegs = 12;
    const positions: number[] = [];
    const uvs: number[] = [];
    const indices: number[] = [];

    const pushQuad = (
      ax: number,
      ay: number,
      az: number,
      bx: number,
      by: number,
      bz: number,
      cx: number,
      cy: number,
      cz: number,
      dx: number,
      dy: number,
      dz: number
    ) => {
      const base = positions.length / 3;
      positions.push(ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy, dz);
      uvs.push(0, 0, 1, 0, 1, 1, 0, 1);
      indices.push(base, base + 2, base + 1, base, base + 3, base + 2);
    };

    for (let s = 0; s < arcSegs; s++) {
      const a0 = lerp(angStart, angEnd, s / arcSegs);
      const a1 = lerp(angStart, angEnd, (s + 1) / arcSegs);
      const c0 = Math.cos(a0);
      const s0 = Math.sin(a0);
      const c1 = Math.cos(a1);
      const s1 = Math.sin(a1);

      // Front face (+Z)
      pushQuad(
        c0 * rInner,
        s0 * rInner,
        halfThick,
        c1 * rInner,
        s1 * rInner,
        halfThick,
        c1 * rOuter,
        s1 * rOuter,
        halfThick,
        c0 * rOuter,
        s0 * rOuter,
        halfThick
      );
      // Back face (-Z)
      pushQuad(
        c1 * rInner,
        s1 * rInner,
        -halfThick,
        c0 * rInner,
        s0 * rInner,
        -halfThick,
        c0 * rOuter,
        s0 * rOuter,
        -halfThick,
        c1 * rOuter,
        s1 * rOuter,
        -halfThick
      );
    }

    // Left Slot-Jamb Wall (at angStart — facing into the single-car passage!)
    {
      const c = Math.cos(angStart);
      const s = Math.sin(angStart);
      pushQuad(
        c * rInner,
        s * rInner,
        -halfThick,
        c * rInner,
        s * rInner,
        halfThick,
        c * rOuter,
        s * rOuter,
        halfThick,
        c * rOuter,
        s * rOuter,
        -halfThick
      );
    }
    // Right Slot-Jamb Wall (at angEnd — facing into the next single-car passage!)
    {
      const c = Math.cos(angEnd);
      const s = Math.sin(angEnd);
      pushQuad(
        c * rInner,
        s * rInner,
        halfThick,
        c * rInner,
        s * rInner,
        -halfThick,
        c * rOuter,
        s * rOuter,
        -halfThick,
        c * rOuter,
        s * rOuter,
        halfThick
      );
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
    geo.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
    geo.setIndex(indices);
    geo.computeVertexNormals();
    return geo;
  }

  private createSteamPistonGeo(): THREE.BufferGeometry {
    const radialSegs = 28;
    const rings = [
      { y: 0.0, r: 0.0, col: 0xd99b4e },
      { y: 0.0, r: 0.75, col: 0xc7743c },
      { y: 0.0, r: 1.0, col: 0xe6af55 },
      { y: -1.1, r: 1.02, col: 0x8f4d28 },
      { y: -2.4, r: 0.98, col: 0xc46d3b },
      { y: -3.2, r: 0.42, col: 0x5c636e },
      { y: -48.0, r: 0.36, col: 0xff7824 },
    ];

    const cols = radialSegs + 1;
    const rows = rings.length;
    const pos = new Float32Array(cols * rows * 3);
    const col = new Float32Array(cols * rows * 3);
    const uv = new Float32Array(cols * rows * 2);
    const indices: number[] = [];
    const tmp = new THREE.Color();

    let pPtr = 0;
    let uvPtr = 0;
    for (let r = 0; r < rows; r++) {
      const sp = rings[r];
      tmp.setHex(sp.col);
      for (let c = 0; c < cols; c++) {
        const u = c / radialSegs;
        const theta = u * Math.PI * 2;
        const nx = Math.cos(theta);
        const nz = Math.sin(theta);
        pos[pPtr] = nx * sp.r;
        pos[pPtr + 1] = sp.y;
        pos[pPtr + 2] = nz * sp.r;
        uv[uvPtr++] = nx * sp.r * 1.8;
        uv[uvPtr++] = r <= 2 ? nz * sp.r * 1.8 : sp.y * 0.22;
        col[pPtr] = tmp.r;
        col[pPtr + 1] = tmp.g;
        col[pPtr + 2] = tmp.b;
        pPtr += 3;
      }
    }

    for (let r = 0; r < rows - 1; r++) {
      for (let c = 0; c < radialSegs; c++) {
        const a = r * cols + c;
        const b = a + 1;
        const d = a + cols;
        const e = d + 1;
        indices.push(a, b, e, a, e, d);
      }
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    geo.setAttribute('color', new THREE.BufferAttribute(col, 3));
    geo.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
    geo.setIndex(indices);
    geo.computeVertexNormals();
    return geo;
  }

  private initMoltenFoundryAtmosphere(): void {
    const slagGeo = new THREE.PlaneGeometry(200, 175);
    slagGeo.rotateX(-Math.PI * 0.5);
    const slagHeights = [-27.5, -32.5, -35.8];
    for (let i = 0; i < slagHeights.length; i++) {
      const m = new THREE.Mesh(slagGeo, this.moltenSlagSheetMat);
      m.position.set(0, slagHeights[i], 0);
      m.renderOrder = -1;
      this.atmosphereFxGroup.add(m);
      this.slagPlanes.push(m);
    }

    this.emberPositions = new Float32Array(this.emberCount * 3);
    this.emberVelocities = new Float32Array(this.emberCount * 3);
    const rng = mulberry32(994127);
    for (let i = 0; i < this.emberCount; i++) {
      const ix = i * 3;
      this.emberPositions[ix] = (rng() - 0.5) * 165;
      this.emberPositions[ix + 1] = lerp(-32.0, 22.0, rng());
      this.emberPositions[ix + 2] = (rng() - 0.5) * 155;

      this.emberVelocities[ix] = (rng() - 0.5) * 2.4;
      this.emberVelocities[ix + 1] = lerp(2.2, 6.4, rng());
      this.emberVelocities[ix + 2] = (rng() - 0.5) * 2.4;
    }

    const eGeo = new THREE.BufferGeometry();
    eGeo.setAttribute('position', new THREE.BufferAttribute(this.emberPositions, 3));
    const eMat = new THREE.PointsMaterial({
      color: 0xffb347,
      size: 0.34,
      transparent: true,
      opacity: 0.82,
      depthWrite: false,
    });
    this.emberPoints = new THREE.Points(eGeo, eMat);
    this.emberPoints.frustumCulled = false;
    this.atmosphereFxGroup.add(this.emberPoints);
  }

  // ===========================================================================
  // DETERMINISTIC MULTI-GEAR & STEAM-PISTON GRAPH (`1 -> 3 -> 4 -> 2 -> 3 -> 2`)
  // ===========================================================================

  public getRowSpecs(
    rowIdx: number,
    sampleCourseX: (z: number) => number
  ): Array<{
    x: number;
    z: number;
    radius: number;
    baseY: number;
    omega: number;
    stationaryInnerRadius: number;
    isPiston: boolean;
    pistonStrokeY: number;
    pistonPhase: number;
    rowIdx: number;
    islandIdx: number;
  }> {
    const safeRow = Math.max(0, rowIdx);
    const zRow = -(FIRST_ROW_Z_OFFSET + safeRow * ROW_SPACING_Z);
    const centerX = sampleCourseX(zRow);
    const count = ROW_GEAR_COUNTS[safeRow % ROW_GEAR_COUNTS.length];
    const rng = mulberry32(safeRow * 71917 + 31337);

    if (count === 1) {
      const baseY = safeRow === 0 ? 0.0 : lerp(-1.0, 2.2, rng());
      return [
        {
          x: centerX,
          z: zRow,
          radius: 23.0,
          baseY,
          omega: safeRow % 2 === 0 ? 0.13 : -0.13,
          stationaryInnerRadius: safeRow === 0 ? 6.2 : 0.0,
          isPiston: false,
          pistonStrokeY: 0,
          pistonPhase: 0,
          rowIdx: safeRow,
          islandIdx: 0,
        },
      ];
    }

    const heightPresets: Record<number, number[]> = {
      2: [-1.2, 2.6],
      3: [-1.4, -0.4, -2.0],
      4: [2.4, -1.6, 1.8, -2.4],
    };
    const heights = heightPresets[count] ?? [0];
    const totalSpanX = count === 4 ? 90.0 : count === 3 ? 72.0 : 46.0;
    const radius = count === 4 ? 12.8 : count === 3 ? 14.8 : 19.5;

    const out = [];
    for (let i = 0; i < count; i++) {
      const frac = i / (count - 1) - 0.5;
      const xOff = frac * totalSpanX;
      // Place Steam-Piston Elevators in Row 2+ (`4`-island slot 1, and `3`-island slot 1 on rows >= 3)
      // so Row 0 -> Row 1 showcases the 3 hero clockwork bridges cleanly!
      const isPiston =
        (count === 4 && i === 1) || (safeRow >= 3 && count === 3 && i === 1);
      const baseY = heights[i] + (safeRow >= 3 ? (rng() - 0.5) * 0.9 : 0.0);
      const dirSign = (safeRow + i) % 2 === 0 ? 1 : -1;
      const omega = isPiston ? 0.0 : dirSign * lerp(0.17, 0.25, rng());

      out.push({
        x: centerX + xOff,
        z: zRow,
        radius: isPiston ? 13.2 : radius,
        baseY,
        omega,
        stationaryInnerRadius: 0.0,
        isPiston,
        pistonStrokeY: isPiston ? 4.4 : 0.0,
        pistonPhase: safeRow * 1.35 + i * 0.9,
        rowIdx: safeRow,
        islandIdx: i,
      });
    }
    return out;
  }

  public samplePistonY(
    baseY: number,
    strokeY: number,
    phase: number,
    elapsed: number
  ): { y: number; velY: number; atTop: boolean } {
    const period = 7.2;
    const cyc = (((elapsed + phase) % period) + period) % period;
    const u = cyc / period;
    let liftFrac = 0;
    let dLiftDu = 0;
    if (u < 0.18) {
      liftFrac = 0;
    } else if (u < 0.5) {
      const s = (u - 0.18) / 0.32;
      liftFrac = smoothstep(0, 1, s);
      dLiftDu = (6 * s * (1 - s)) / 0.32;
    } else if (u < 0.68) {
      liftFrac = 1.0;
    } else {
      const s = (u - 0.68) / 0.32;
      liftFrac = 1.0 - smoothstep(0, 1, s);
      dLiftDu = (-6 * s * (1 - s)) / 0.32;
    }
    return {
      y: baseY + strokeY * liftFrac,
      velY: (strokeY * dLiftDu) / period,
      atTop: u >= 0.5 && u < 0.54,
    };
  }

  /**
   * Computes exact Geneva Intermittent Lock state for a `LOCKING_GEAR_BRIDGE`:
   * - `u in [0.00, 0.62]` (`62%` of cycle): **MECHANICALLY LOCKED** (`deltaYaw = 0` or `PI`, `liveOmega = 0`)!
   * - `u in [0.62, 1.00]` (`38%` of cycle): **INDEXING 180°** (`deltaYaw` sweeps `0 -> PI` smoothly)!
   */
  public sampleGenevaLockState(
    period: number,
    phase: number,
    elapsed: number
  ): { deltaYaw: number; liveOmega: number; isLocked: boolean; pawlLift: number } {
    const halfTurns = Math.floor((elapsed + phase) / period);
    const cyc = (((elapsed + phase) % period) + period) % period;
    const u = cyc / period; // 0..1
    const lockWindow = 0.62;
    const parityBase = (halfTurns % 2) * Math.PI;

    if (u < lockWindow) {
      return {
        deltaYaw: parityBase,
        liveOmega: 0.0,
        isLocked: true,
        pawlLift: 0.0,
      };
    }

    const s = (u - lockWindow) / (1.0 - lockWindow); // 0..1
    const stepFrac = smoothstep(0, 1, s);
    const dStepDu = (6 * s * (1 - s)) / (1.0 - lockWindow);
    const pawlLift = Math.sin(Math.PI * clamp(s * 1.1, 0, 1)) * 0.48;

    return {
      deltaYaw: parityBase + stepFrac * Math.PI,
      liveOmega: (Math.PI * dStepDu) / period,
      isLocked: false,
      pawlLift,
    };
  }

  public getRowLinks(
    rowIdx: number,
    specsA: ReturnType<ClockworkFoundryBuilder['getRowSpecs']>,
    specsB: ReturnType<ClockworkFoundryBuilder['getRowSpecs']>
  ): ClockworkGraphLink[] {
    const M = specsA.length;
    const N = specsB.length;

    const pairSet = new Set<string>();
    const rawPairs: Array<{ fromIdx: number; toIdx: number }> = [];
    const addPair = (u: number, v: number, isBonus = false) => {
      if (u < 0 || u >= M || v < 0 || v >= N) return;
      if (isBonus && Math.abs(specsB[v].x - specsA[u].x) > 64.0) return;
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

    if (M === 3 && N === 4) {
      addPair(0, 1, true);
      addPair(1, 1, true);
      addPair(1, 2, true);
      addPair(2, 2, true);
    } else if (M === 4 && N === 2) {
      addPair(1, 1, true);
      addPair(2, 0, true);
    } else if (M === 2 && N === 3) {
      addPair(0, 1, true);
      addPair(1, 1, true);
    } else if (M === 3 && N === 2) {
      addPair(1, 0, true);
      addPair(1, 1, true);
    }

    const links: ClockworkGraphLink[] = rawPairs.map((p, idx) => {
      const a = specsA[p.fromIdx];
      const b = specsB[p.toIdx];
      const dy = b.baseY - a.baseY;

      let kind: ClockworkSpanKind = 'SEESAW_BRIDGE';
      if (a.isPiston || b.isPiston) {
        kind = 'PISTON_GANGWAY';
      } else if (M === 1 && N === 3) {
        // Signature Opening Foundry Showcase from Master Hub (0m):
        // Left = Geneva Locking-Gear Pathway | Center = Colossal Vertical Rotating Gear-Wall | Right = Counterweight Seesaw!
        kind =
          p.toIdx === 0
            ? 'LOCKING_GEAR_BRIDGE'
            : p.toIdx === 1
            ? 'GEAR_WALL_CAUSEWAY'
            : 'SEESAW_BRIDGE';
      } else if (p.fromIdx === 0 && p.toIdx === 0) {
        kind = rowIdx % 2 === 1 ? 'LOCKING_GEAR_BRIDGE' : 'GEAR_WALL_CAUSEWAY';
      } else if (idx % 4 === 1 && Math.abs(dy) < 3.6) {
        kind = 'GEAR_WALL_CAUSEWAY';
      } else if (idx % 4 === 2 && Math.abs(dy) < 3.4) {
        kind = 'LOCKING_GEAR_BRIDGE';
      } else if (dy <= -1.6 && p.toIdx === N - 1) {
        kind = 'FOUNDRY_JUMP_RAMP';
      } else if (dy >= 2.6) {
        kind = 'HIGH_ARCH_CATWALK';
      }

      return {
        fromIdx: p.fromIdx,
        toIdx: p.toIdx,
        kind,
        archBumpY: kind === 'HIGH_ARCH_CATWALK' ? 1.8 : 0.0,
      };
    });

    // Separate any 2D X-crossing spans cleanly in 3D: Upper = High-Arch Catwalk!
    for (let i = 0; i < links.length; i++) {
      for (let j = i + 1; j < links.length; j++) {
        const L1 = links[i];
        const L2 = links[j];
        const crosses =
          (L1.fromIdx < L2.fromIdx && L1.toIdx > L2.toIdx) ||
          (L1.fromIdx > L2.fromIdx && L1.toIdx < L2.toIdx);
        if (!crosses) continue;

        const upper = !specsA[L1.fromIdx].isPiston && !specsB[L1.toIdx].isPiston ? L1 : L2;
        const lower = upper === L1 ? L2 : L1;
        upper.kind = 'HIGH_ARCH_CATWALK';
        upper.archBumpY = 5.6;
        if (lower.kind === 'HIGH_ARCH_CATWALK' || lower.kind === 'FOUNDRY_JUMP_RAMP') {
          lower.kind =
            specsA[lower.fromIdx].isPiston || specsB[lower.toIdx].isPiston
              ? 'PISTON_GANGWAY'
              : 'SEESAW_BRIDGE';
        }
      }
    }

    return links;
  }

  public findNearestGearSpawn(
    x: number,
    z: number,
    sampleCourseX: (z: number) => number
  ): { x: number; y: number; z: number; yaw: number } {
    const approxRow = Math.max(0, Math.round((-z - FIRST_ROW_Z_OFFSET) / ROW_SPACING_Z));
    let best: ReturnType<ClockworkFoundryBuilder['getRowSpecs']>[number] | null = null;
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
      return { x: s0.x, y: s0.baseY, z: s0.z, yaw: 0 };
    }

    const cur = this.getRowSpecs(best.rowIdx, sampleCourseX);
    const nxt = this.getRowSpecs(best.rowIdx + 1, sampleCourseX);
    const links = this.getRowLinks(best.rowIdx, cur, nxt).filter(
      (l) => l.fromIdx === best!.islandIdx
    );
    let yaw = 0;
    if (links.length > 0) {
      const mid = links[Math.floor(links.length * 0.5)];
      const tgt = nxt[mid.toIdx];
      yaw = Math.atan2(-(tgt.x - best.x), -(tgt.z - best.z));
    }

    return {
      x: best.x,
      y: best.baseY + (best.isPiston ? best.pistonStrokeY * 0.5 : 0),
      z: best.z,
      yaw,
    };
  }

  private computeGearRimSocket(
    spec: { x: number; z: number; radius: number; baseY: number },
    slotIdx: number,
    slotCount: number,
    otherX: number,
    isOutgoing: boolean,
    halfWidth: number,
    insetFactor = 0.88
  ): { x: number; y: number; z: number; offX: number; offZ: number } {
    let dx = 0;
    if (slotCount <= 1) {
      dx = clamp((otherX - spec.x) * 0.18, -spec.radius * 0.36, spec.radius * 0.36);
    } else {
      const spread = slotCount <= 2 ? 0.44 : slotCount === 3 ? 0.56 : 0.64;
      const frac = (slotIdx / (slotCount - 1)) * 2.0 - 1.0;
      dx = frac * spec.radius * spread;
    }

    const outerNx = Math.min(0.88, (Math.abs(dx) + halfWidth) / spec.radius);
    const dzMag = spec.radius * Math.sqrt(Math.max(0.14, insetFactor - outerNx * outerNx));
    const dz = isOutgoing ? -dzMag : dzMag;

    return {
      x: spec.x + dx,
      y: spec.baseY + 0.03,
      z: spec.z + dz,
      offX: dx,
      offZ: dz,
    };
  }

  public getLinkEndpoints(
    rowIdx: number,
    fromIdx: number,
    toIdx: number,
    sampleCourseX: (z: number) => number
  ): { startPt: THREE.Vector3; endPt: THREE.Vector3; kind: ClockworkSpanKind } | null {
    const srcSpecs = this.getRowSpecs(rowIdx, sampleCourseX);
    const dstSpecs = this.getRowSpecs(rowIdx + 1, sampleCourseX);
    const links = this.getRowLinks(rowIdx, srcSpecs, dstSpecs);
    const link = links.find((l) => l.fromIdx === fromIdx && l.toIdx === toIdx);
    if (!link) return null;

    const outList = links
      .filter((l) => l.fromIdx === fromIdx)
      .sort((a, b) => dstSpecs[a.toIdx].x - dstSpecs[b.toIdx].x);
    const inList = links
      .filter((l) => l.toIdx === toIdx)
      .sort((a, b) => srcSpecs[a.fromIdx].x - srcSpecs[b.fromIdx].x);

    const halfWidth = 3.85;
    const s = this.computeGearRimSocket(
      srcSpecs[fromIdx],
      outList.indexOf(link),
      outList.length,
      dstSpecs[toIdx].x,
      true,
      halfWidth
    );
    const e = this.computeGearRimSocket(
      dstSpecs[toIdx],
      inList.indexOf(link),
      inList.length,
      srcSpecs[fromIdx].x,
      false,
      halfWidth
    );
    return {
      startPt: new THREE.Vector3(s.x, s.y, s.z),
      endPt: new THREE.Vector3(e.x, e.y, e.z),
      kind: link.kind,
    };
  }

  // ===========================================================================
  // CHUNK POPULATION — HEAVY MECHANICAL FOUNDRY HALL & CLOCKWORK MECHANISMS
  // ===========================================================================

  public populateClockworkChunk(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    zLength: number,
    sampleCourseX: (z: number) => number,
    _obstacles: WorldObstacle[],
    rootBridges: CrisscrossRootBridge[],
    nextBridgeId: () => number
  ): ClockworkChunkData {
    const group = new THREE.Group();
    group.name = `ClockworkFoundryChunk_${chunkIndex}`;
    chunkGroup.add(group);
    const rng = mulberry32(chunkIndex * 48917 + 7719);

    const islands: ClockworkGearIsland[] = [];
    const ownedIslands: ClockworkGearIsland[] = [];
    const drivenVisuals: DrivenShaftOrGearVisual[] = [];
    const seesaws: SeesawBeamBridge[] = [];
    const gangways: ArticulatedPistonGangway[] = [];
    const lockingPathways: LockingGearPathway[] = [];
    const gearWalls: VerticalSlottedGearWall[] = [];

    const ironMats: THREE.Matrix4[] = [];
    const gantryMats: THREE.Matrix4[] = [];
    const chevronMats: THREE.Matrix4[] = [];
    const beaconPositions: THREE.Vector3[] = [];
    const furnacePositions: THREE.Vector3[] = [];

    const rowBase = chunkIndex * 2;
    const specs0 = this.getRowSpecs(rowBase, sampleCourseX);
    const specs1 = this.getRowSpecs(rowBase + 1, sampleCourseX);
    const specs2 = this.getRowSpecs(rowBase + 2, sampleCourseX);

    const spawnIslandList = (
      specList: ReturnType<ClockworkFoundryBuilder['getRowSpecs']>,
      visualOwned: boolean
    ): ClockworkGearIsland[] => {
      const created: ClockworkGearIsland[] = [];
      for (const sp of specList) {
        const geo = sp.isPiston
          ? this.pistonIslandGeo
          : sp.radius >= 18.0
          ? this.cogwheel24Geo
          : this.cogwheel16Geo;
        const mat = sp.isPiston ? this.copperBoilerMat : this.brassGearMat;

        const mesh = new THREE.Mesh(geo, mat);
        mesh.position.set(sp.x, sp.baseY, sp.z);
        mesh.scale.set(sp.radius, 1.0, sp.radius);
        mesh.castShadow = true;
        mesh.receiveShadow = true;

        let hubCapMesh: THREE.Mesh | null = null;
        if (visualOwned) {
          group.add(mesh);

          // Heavy Cast-Iron Pillow-Block Axle Bearing Collar beneath every Cogwheel!
          this._dummy.position.set(sp.x, sp.baseY - 3.6, sp.z);
          this._dummy.rotation.set(0, 0, 0);
          this._dummy.scale.set(sp.radius * 0.42, 1.5, sp.radius * 0.42);
          this._dummy.updateMatrix();
          gantryMats.push(this._dummy.matrix.clone());

          const capR =
            sp.stationaryInnerRadius > 0 ? sp.stationaryInnerRadius : sp.radius * 0.2;
          hubCapMesh = new THREE.Mesh(this.hubMedallionGeo, this.castIronMat);
          hubCapMesh.position.set(sp.x, sp.baseY + 0.02, sp.z);
          hubCapMesh.scale.set(capR, 1.0, capR);
          hubCapMesh.receiveShadow = true;
          group.add(hubCapMesh);

          furnacePositions.push(new THREE.Vector3(sp.x, sp.baseY + 0.32, sp.z));

          // Interlocking Brass Drive-Train Pinion + Overhead Vertical Power Drop-Shaft!
          if (!sp.isPiston && (sp.islandIdx === 0 || sp.islandIdx === specList.length - 1)) {
            const outSide = sp.islandIdx === 0 ? -1 : 1;
            const idlerR = lerp(6.6, 8.8, rng());
            const meshDist = sp.radius + idlerR + sp.radius * 0.08 + idlerR * 0.08;
            const ix = sp.x + outSide * meshDist;
            const iz = sp.z + 2.0;

            const idlerMesh = new THREE.Mesh(this.cogwheel16Geo, this.brassGearMat);
            idlerMesh.position.set(ix, sp.baseY - 0.1, iz);
            idlerMesh.scale.set(idlerR, 1.0, idlerR);
            group.add(idlerMesh);
            const idlerOmega = -sp.omega * (sp.radius / idlerR);
            drivenVisuals.push({
              obj: idlerMesh,
              omega: idlerOmega,
              angle: 0.2,
              axis: 'Y',
            });

            // Vertical Overhead Steel Power Drop-Shaft rising from this Drive Pinion up to the +26m Gantry Crane!
            const dropH = 26.5 - sp.baseY;
            const dropShaft = new THREE.Mesh(this.shaftCylGeo, this.castIronMat);
            dropShaft.position.set(ix, sp.baseY + dropH * 0.5, iz);
            dropShaft.scale.set(1.25, dropH, 1.25);
            group.add(dropShaft);

            // Pillow-block bearing bracket clamping the drop-shaft!
            this._dummy.position.set(ix, sp.baseY + 0.35, iz);
            this._dummy.rotation.set(0, 0, 0);
            this._dummy.scale.set(2.4, 0.75, 2.4);
            this._dummy.updateMatrix();
            gantryMats.push(this._dummy.matrix.clone());
          }
        }

        const isl: ClockworkGearIsland = {
          x: sp.x,
          z: sp.z,
          radius: sp.radius,
          baseY: sp.baseY,
          currentY: sp.baseY,
          velY: 0,
          omega: sp.omega,
          angle: (sp.rowIdx * 0.7 + sp.islandIdx * 1.1) % (Math.PI * 2),
          stationaryInnerRadius: sp.stationaryInnerRadius,
          isPiston: sp.isPiston,
          pistonStrokeY: sp.pistonStrokeY,
          pistonPhase: sp.pistonPhase,
          wasAtTop: false,
          rowIdx: sp.rowIdx,
          islandIdx: sp.islandIdx,
          mesh,
          hubCapMesh,
        };
        islands.push(isl);
        if (visualOwned) ownedIslands.push(isl);
        created.push(isl);
      }
      return created;
    };

    const row0 = spawnIslandList(specs0, true);
    const row1 = spawnIslandList(specs1, true);
    const row2 = spawnIslandList(specs2, false);

    // =========================================================================
    // GROUNDED INDUSTRIAL FOUNDRY GANTRY ARCHITECTURE (Replaces Floating Pendulums!)
    // =========================================================================
    for (let gIdx = 0; gIdx < 2; gIdx++) {
      const zGantry = zStart - (gIdx === 0 ? 28.0 : 86.0);
      const cx = sampleCourseX(zGantry);
      const gantryY = 26.5;

      // 1. Twin Colossal Riveted Cast-Iron Lattice Columns rising out of the -36m Molten Slag Pit!
      for (const side of [-1, 1]) {
        const colX = cx + side * 70.0;
        this._dummy.position.set(colX, -6.0, zGantry);
        this._dummy.rotation.set(0, 0, 0);
        this._dummy.scale.set(3.4, 68.0, 3.4);
        this._dummy.updateMatrix();
        gantryMats.push(this._dummy.matrix.clone());

        // Diagonal Knee-Brace Strut supporting the overhead gantry bridge!
        this._dummy.position.set(colX - side * 8.5, gantryY - 5.5, zGantry);
        this._dummy.rotation.set(0, 0, side * 0.68);
        this._dummy.scale.set(1.2, 16.5, 1.4);
        this._dummy.updateMatrix();
        gantryMats.push(this._dummy.matrix.clone());

        // Vertical Wall-Mounted Gear Train + Bearing Housing bolted directly onto the Lattice Column!
        const wgR = lerp(13.5, 17.5, rng());
        const wallGear = new THREE.Mesh(this.cogwheel24Geo, this.brassGearMat);
        wallGear.position.set(colX - side * 1.8, 11.0, zGantry);
        wallGear.rotation.z = side * Math.PI * 0.5;
        wallGear.scale.set(wgR, 1.0, wgR);
        group.add(wallGear);
        drivenVisuals.push({
          obj: wallGear,
          omega: side * 0.24,
          angle: rng() * Math.PI,
          axis: 'X',
        });

        // Smelting Crucible Ladle + Glowing Vertical Stream of Molten Brass pouring into the -36m Slag Sea!
        if ((chunkIndex + gIdx + (side > 0 ? 1 : 0)) % 2 === 0) {
          const ladleX = cx + side * 61.0;
          const ladleY = 7.5;
          const ladle = new THREE.Mesh(this.trunnionCylGeo, this.darkGantryIronMat);
          ladle.position.set(ladleX, ladleY, zGantry + 12.0);
          ladle.scale.set(3.2, 4.6, 3.2);
          group.add(ladle);

          const streamH = ladleY - -36.0;
          const pourStream = new THREE.Mesh(this.shaftCylGeo, this.moltenPourMat);
          pourStream.position.set(
            ladleX - side * 1.8,
            ladleY - streamH * 0.5,
            zGantry + 12.0
          );
          pourStream.scale.set(1.35, streamH, 1.35);
          group.add(pourStream);

          furnacePositions.push(new THREE.Vector3(ladleX - side * 1.8, -34.8, zGantry + 12.0));
        }
      }

      // 2. Full-Span Overhead Riveted Box-Girder Gantry Bridge (`144m` span across the cavern ceiling!)
      this._dummy.position.set(cx, gantryY, zGantry);
      this._dummy.rotation.set(0, 0, 0);
      this._dummy.scale.set(144.0, 2.2, 2.6);
      this._dummy.updateMatrix();
      gantryMats.push(this._dummy.matrix.clone());

      // 3. Continuously-Spinning Overhead Horizontal Steel Line-Shaft + Bevel Transfer Gears!
      const lineShaft = new THREE.Mesh(this.shaftCylGeo, this.castIronMat);
      lineShaft.position.set(cx, gantryY - 1.9, zGantry);
      lineShaft.rotation.z = Math.PI * 0.5;
      lineShaft.scale.set(1.4, 138.0, 1.4);
      group.add(lineShaft);

      for (const bevelOffX of [-36.0, 0.0, 36.0]) {
        const bevel = new THREE.Mesh(this.cogwheel16Geo, this.brassGearMat);
        bevel.position.set(cx + bevelOffX, gantryY - 1.9, zGantry);
        bevel.rotation.z = Math.PI * 0.5;
        bevel.scale.set(3.4, 1.0, 3.4);
        group.add(bevel);
        drivenVisuals.push({
          obj: bevel,
          omega: 0.85,
          angle: bevelOffX * 0.1,
          axis: 'X',
        });
      }
    }

    // =========================================================================
    // BUILD ALL MECHANICAL SPANS BETWEEN GEAR ROWS
    // =========================================================================
    let spanCounter = 0;
    const buildLayerSpans = (
      rIdx: number,
      srcList: ClockworkGearIsland[],
      dstList: ClockworkGearIsland[],
      srcSpecs: ReturnType<ClockworkFoundryBuilder['getRowSpecs']>,
      dstSpecs: ReturnType<ClockworkFoundryBuilder['getRowSpecs']>
    ) => {
      const links = this.getRowLinks(rIdx, srcSpecs, dstSpecs);
      const outBy = new Map<number, ClockworkGraphLink[]>();
      const inBy = new Map<number, ClockworkGraphLink[]>();
      for (const l of links) {
        if (!outBy.has(l.fromIdx)) outBy.set(l.fromIdx, []);
        outBy.get(l.fromIdx)!.push(l);
        if (!inBy.has(l.toIdx)) inBy.set(l.toIdx, []);
        inBy.get(l.toIdx)!.push(l);
      }
      for (const arr of outBy.values()) {
        arr.sort((a, b) => dstList[a.toIdx].x - dstList[b.toIdx].x);
      }
      for (const arr of inBy.values()) {
        arr.sort((a, b) => srcList[a.fromIdx].x - srcList[b.fromIdx].x);
      }

      for (const link of links) {
        spanCounter++;
        const src = srcList[link.fromIdx];
        const dst = dstList[link.toIdx];
        const outArr = outBy.get(link.fromIdx)!;
        const inArr = inBy.get(link.toIdx)!;
        const halfWidth = 3.95;

        const s = this.computeGearRimSocket(
          srcSpecs[link.fromIdx],
          outArr.indexOf(link),
          outArr.length,
          dst.x,
          true,
          halfWidth
        );
        const e = this.computeGearRimSocket(
          dstSpecs[link.toIdx],
          inArr.indexOf(link),
          inArr.length,
          src.x,
          false,
          halfWidth
        );

        if (link.kind === 'LOCKING_GEAR_BRIDGE') {
          lockingPathways.push(
            this.buildLockingGearPathway(
              group,
              s.x,
              s.y,
              s.z,
              e.x,
              e.y,
              e.z,
              halfWidth,
              rIdx * 1.7 + spanCounter * 1.15,
              gantryMats,
              ironMats,
              furnacePositions
            )
          );
        } else if (link.kind === 'GEAR_WALL_CAUSEWAY') {
          gearWalls.push(
            this.buildVerticalGearWallCauseway(
              group,
              s.x,
              s.y,
              s.z,
              e.x,
              e.y,
              e.z,
              halfWidth + 0.35,
              (rIdx + spanCounter) % 2 === 0 ? 1 : -1,
              gantryMats,
              ironMats,
              chevronMats,
              beaconPositions
            )
          );
        } else if (link.kind === 'SEESAW_BRIDGE') {
          seesaws.push(
            this.buildSeesawBeamBridge(
              group,
              s.x,
              s.y,
              s.z,
              e.x,
              e.y,
              e.z,
              halfWidth,
              furnacePositions
            )
          );
        } else if (link.kind === 'PISTON_GANGWAY') {
          gangways.push(
            this.buildArticulatedPistonGangway(
              group,
              src,
              dst,
              s.offX,
              s.offZ,
              e.offX,
              e.offZ,
              halfWidth
            )
          );
        } else if (link.kind === 'HIGH_ARCH_CATWALK') {
          this.buildHighArchCatwalk(
            s.x,
            s.y,
            s.z,
            e.x,
            e.y,
            e.z,
            link.archBumpY,
            halfWidth,
            rootBridges,
            nextBridgeId,
            chunkIndex,
            ironMats,
            beaconPositions
          );
        } else {
          this.buildFoundryJumpRamp(
            s.x,
            s.y,
            s.z,
            e.x,
            e.y,
            e.z,
            halfWidth,
            rootBridges,
            nextBridgeId,
            chunkIndex,
            ironMats,
            chevronMats,
            beaconPositions
          );
        }
      }
    };

    buildLayerSpans(rowBase, row0, row1, specs0, specs1);
    buildLayerSpans(rowBase + 1, row1, row2, specs1, specs2);

    const emitInstanced = (
      geo: THREE.BufferGeometry,
      mat: THREE.Material,
      mats: THREE.Matrix4[]
    ) => {
      if (mats.length === 0) return;
      const im = new THREE.InstancedMesh(geo, mat, mats.length);
      for (let i = 0; i < mats.length; i++) im.setMatrixAt(i, mats[i]);
      im.instanceMatrix.needsUpdate = true;
      im.castShadow = true;
      im.receiveShadow = true;
      group.add(im);
    };

    emitInstanced(this.unitBoxGeo, this.castIronMat, ironMats);
    emitInstanced(this.unitBoxGeo, this.darkGantryIronMat, gantryMats);
    emitInstanced(this.unitBoxGeo, this.rampChevronMat, chevronMats);

    if (furnacePositions.length > 0) {
      const fIm = new THREE.InstancedMesh(
        this.beaconSphereGeo,
        this.furnaceCoreGlowMat,
        furnacePositions.length
      );
      for (let i = 0; i < furnacePositions.length; i++) {
        this._dummy.position.copy(furnacePositions[i]);
        this._dummy.rotation.set(0, 0, 0);
        this._dummy.scale.set(2.4, 0.85, 2.4);
        this._dummy.updateMatrix();
        fIm.setMatrixAt(i, this._dummy.matrix);
      }
      fIm.instanceMatrix.needsUpdate = true;
      group.add(fIm);
    }

    if (beaconPositions.length > 0) {
      const bIm = new THREE.InstancedMesh(
        this.beaconSphereGeo,
        this.rampBeaconGlowMat,
        beaconPositions.length
      );
      for (let i = 0; i < beaconPositions.length; i++) {
        this._dummy.position.copy(beaconPositions[i]);
        this._dummy.rotation.set(0, 0, 0);
        this._dummy.scale.set(1.4, 1.7, 1.4);
        this._dummy.updateMatrix();
        bIm.setMatrixAt(i, this._dummy.matrix);
      }
      bIm.instanceMatrix.needsUpdate = true;
      group.add(bIm);
    }

    const forgeLight = new THREE.PointLight(0xff7722, 24.0, 62.0, 1.3);
    forgeLight.position.set(row0[0].x, -9.0, row0[0].z - 28.0);
    group.add(forgeLight);

    return {
      group,
      islands,
      ownedIslands,
      drivenVisuals,
      seesaws,
      gangways,
      lockingPathways,
      gearWalls,
      zStart,
      zLength,
    };
  }

  // ===========================================================================
  // MECHANISM 1: GENEVA INTERMITTENT LOCKING-GEAR PATHWAY
  // ===========================================================================

  private buildLockingGearPathway(
    chunkGroup: THREE.Group,
    ax: number,
    ay: number,
    az: number,
    bx: number,
    by: number,
    bz: number,
    beamHalfWidth: number,
    phase: number,
    gantryMats: THREE.Matrix4[],
    ironMats: THREE.Matrix4[],
    furnacePositions: THREE.Vector3[]
  ): LockingGearPathway {
    const dx = bx - ax;
    const dz = bz - az;
    const spanLen = Math.hypot(dx, dz);
    const dirX = dx / spanLen;
    const dirZ = dz / spanLen;
    const baseYaw = Math.atan2(-dirX, -dirZ);
    const rightX = Math.cos(baseYaw);
    const rightZ = -Math.sin(baseYaw);

    const approachLen = clamp((spanLen - 23.0) * 0.5, 2.2, 6.8);
    const lockR = Math.max(8.5, (spanLen - 2.0 * approachLen) * 0.5);
    const midX = (ax + bx) * 0.5;
    const midY = (ay + by) * 0.5;
    const midZ = (az + bz) * 0.5;

    // 1. Fixed Riveted Approach Piers on Entry (`A`) & Exit (`B`) + Lock-Signal Semaphores!
    const beaconMeshes: THREE.Mesh[] = [];
    for (const isStart of [true, false]) {
      const s0 = isStart ? 0.0 : spanLen - approachLen;
      const s1 = isStart ? approachLen : spanLen;
      const y0 = isStart ? ay : midY;
      const y1 = isStart ? midY : by;
      const xm = ax + dirX * ((s0 + s1) * 0.5);
      const zm = az + dirZ * ((s0 + s1) * 0.5);
      const ym = (y0 + y1) * 0.5;
      const pitch = Math.atan2(y1 - y0, approachLen);

      this._dummy.position.set(xm, ym - 0.16, zm);
      this._euler.set(pitch, baseYaw, 0, 'YXZ');
      this._dummy.rotation.copy(this._euler);
      this._dummy.scale.set((beamHalfWidth + 0.55) * 2.0, 0.34, approachLen + 0.35);
      this._dummy.updateMatrix();
      ironMats.push(this._dummy.matrix.clone());

      // Semaphore Signal Posts showing GREEN ("GEAR LOCKED") vs RED-ORANGE ("ROTATING")!
      if (isStart) {
        for (const side of [-1, 1]) {
          const px = xm + rightX * side * (beamHalfWidth + 0.55);
          const pz = zm + rightZ * side * (beamHalfWidth + 0.55);
          this._dummy.position.set(px, ym + 1.25, pz);
          this._dummy.rotation.set(0, baseYaw, 0);
          this._dummy.scale.set(0.32, 2.5, 0.32);
          this._dummy.updateMatrix();
          gantryMats.push(this._dummy.matrix.clone());

          const sig = new THREE.Mesh(this.beaconSphereGeo, this.lockReadyGlowMat);
          sig.position.set(px, ym + 2.65, pz);
          sig.scale.setScalar(1.7);
          chunkGroup.add(sig);
          beaconMeshes.push(sig);
        }
      }
    }

    // 2. Giant Open-Ring Locking Bridge-Gear (`lockingGearGroup`)!
    const lockingGearGroup = new THREE.Group();
    lockingGearGroup.position.set(midX, midY, midZ);
    lockingGearGroup.rotation.y = baseYaw;

    // 2a. Hollow Toothed Brass Outer Ring (open -36m molten pit inside!)
    const ringMesh = new THREE.Mesh(this.openLockingRingGeo, this.brassGearMat);
    ringMesh.scale.set(lockR, 1.0, lockR);
    ringMesh.castShadow = true;
    ringMesh.receiveShadow = true;
    lockingGearGroup.add(ringMesh);

    // 2b. Single Diametral Riveted Bridge-Beam spanning from `-lockR` to `+lockR`!
    const beamDeck = new THREE.Mesh(this.unitBoxGeo, this.seesawDeckMat);
    beamDeck.position.set(0, -0.18, 0);
    beamDeck.scale.set(beamHalfWidth * 2.0, 0.36, lockR * 2.01);
    beamDeck.castShadow = true;
    beamDeck.receiveShadow = true;
    lockingGearGroup.add(beamDeck);

    // Glowing brass center alignment strip along the locking bridge beam!
    const alignStripe = new THREE.Mesh(this.unitBoxGeo, this.rampChevronMat);
    alignStripe.position.set(0, 0.01, 0);
    alignStripe.scale.set(0.75, 0.05, lockR * 1.94);
    lockingGearGroup.add(alignStripe);

    // Riveted Cast-Iron Box-Girder Side Curbs along the Diametral Bridge-Beam!
    for (const side of [-1, 1]) {
      const curb = new THREE.Mesh(this.unitBoxGeo, this.castIronMat);
      curb.position.set(side * (beamHalfWidth + 0.1), 0.24, 0);
      curb.scale.set(0.3, 0.68, lockR * 1.72);
      lockingGearGroup.add(curb);
    }

    // Deep Foundry Axle Shaft + Bearing Capstan beneath the Locking Gear!
    const centerAxle = new THREE.Mesh(this.shaftCylGeo, this.castIronMat);
    centerAxle.position.set(0, -24.0, 0);
    centerAxle.scale.set(3.2, 48.0, 3.2);
    lockingGearGroup.add(centerAxle);

    furnacePositions.push(new THREE.Vector3(midX, midY + 0.22, midZ));
    chunkGroup.add(lockingGearGroup);

    // 3. Continuously-Spinning Brass Drive Gear (`driverGroup`) + Geneva Striker Pin!
    const sideSign = midX < 0 ? -1 : 1;
    const driverR = 6.8;
    const meshDist = lockR * 1.04 + driverR * 1.04;
    const drvX = midX + rightX * sideSign * meshDist;
    const drvZ = midZ + rightZ * sideSign * meshDist;

    const driverGroup = new THREE.Group();
    driverGroup.position.set(drvX, midY - 0.04, drvZ);

    const driverGearMesh = new THREE.Mesh(this.cogwheel16Geo, this.brassGearMat);
    driverGearMesh.scale.set(driverR, 1.0, driverR);
    driverGroup.add(driverGearMesh);

    // Raised Copper Geneva Cam & Striker Pin on top of the continuously turning Drive Gear!
    const camHalf = new THREE.Mesh(this.hubMedallionGeo, this.copperBoilerMat);
    camHalf.position.set(0, 0.42, 0);
    camHalf.scale.set(driverR * 0.48, 1.4, driverR * 0.48);
    driverGroup.add(camHalf);

    const strikerPin = new THREE.Mesh(this.shaftCylGeo, this.rampChevronMat);
    strikerPin.position.set(driverR * 0.76, 0.55, 0);
    strikerPin.scale.set(1.3, 1.2, 1.3);
    driverGroup.add(strikerPin);

    chunkGroup.add(driverGroup);

    // Overhead vertical power shaft driving the continuously spinning Drive Gear!
    const drvDropH = 26.5 - midY;
    const drvDropShaft = new THREE.Mesh(this.shaftCylGeo, this.castIronMat);
    drvDropShaft.position.set(drvX, midY + drvDropH * 0.5, drvZ);
    drvDropShaft.scale.set(1.15, drvDropH, 1.15);
    chunkGroup.add(drvDropShaft);

    // 4. Mechanical Cast-Iron Ratchet Locking Pawl clamping the rim during LOCKED phase!
    const pawlDist = lockR * 1.06;
    const pawlPivot = new THREE.Group();
    pawlPivot.position.set(
      midX - rightX * sideSign * (pawlDist + 2.2),
      midY + 0.55,
      midZ - rightZ * sideSign * (pawlDist + 2.2)
    );
    pawlPivot.rotation.y = baseYaw;

    const pawlArm = new THREE.Mesh(this.unitBoxGeo, this.copperBoilerMat);
    pawlArm.position.set(sideSign * 1.8, -0.15, 0);
    pawlArm.scale.set(4.2, 0.65, 1.25);
    pawlPivot.add(pawlArm);
    chunkGroup.add(pawlPivot);

    // Pedestal under the Locking Pawl
    this._dummy.position.set(pawlPivot.position.x, midY - 19.0, pawlPivot.position.z);
    this._dummy.rotation.set(0, baseYaw, 0);
    this._dummy.scale.set(2.2, 39.5, 2.2);
    this._dummy.updateMatrix();
    gantryMats.push(this._dummy.matrix.clone());

    const period = 7.4;
    const driverOmega = (-sideSign * (Math.PI * 2)) / period; // Exactly 1 full turn per Geneva lock cycle!

    return {
      ax,
      ay,
      az,
      bx,
      by,
      bz,
      midX,
      midY,
      midZ,
      dirX,
      dirZ,
      rightX,
      rightZ,
      baseYaw,
      spanLen,
      approachLen,
      lockR,
      beamHalfWidth,
      period,
      phase,
      currentYaw: baseYaw,
      liveOmega: 0,
      isLocked: true,
      wasLocked: true,
      lockingGearGroup,
      driverGroup,
      driverOmega,
      driverAngle: 0,
      pawlPivot,
      beaconMeshes,
    };
  }

  // ===========================================================================
  // MECHANISM 2: COLOSSAL VERTICAL ROTATING SLOTTED GEAR-WALL (`1 CAR AT A TIME!`)
  // ===========================================================================

  private buildVerticalGearWallCauseway(
    chunkGroup: THREE.Group,
    ax: number,
    ay: number,
    az: number,
    bx: number,
    by: number,
    bz: number,
    causewayHalfWidth: number,
    spinSign: number,
    gantryMats: THREE.Matrix4[],
    ironMats: THREE.Matrix4[],
    chevronMats: THREE.Matrix4[],
    beaconPositions: THREE.Vector3[]
  ): VerticalSlottedGearWall {
    const dx = bx - ax;
    const dz = bz - az;
    const spanLen = Math.hypot(dx, dz);
    const dirX = dx / spanLen;
    const dirZ = dz / spanLen;
    const yaw = Math.atan2(-dirX, -dirZ);
    const rightX = Math.cos(yaw);
    const rightZ = -Math.sin(yaw);

    const wallX = (ax + bx) * 0.5;
    const wallZ = (az + bz) * 0.5;
    const deckY = (ay + by) * 0.5;
    const axleY = deckY - 6.8;
    const outerRadius = 13.6;
    const slotInnerR = 4.4;
    const slotOuterR = 11.2;
    const slotHalfAng = 0.34; // ~4.8m wide passage at road level = 1 vehicle at a time!
    const halfThick = 1.15;

    // 1. Build Entry & Exit Riveted Foundry Girder Causeway Halves flanking the 2.5m Gear-Slot Trench!
    const trenchGap = halfThick + 0.22;
    const halfSpanSegLen = spanLen * 0.5 - trenchGap;
    for (const isEntry of [true, false]) {
      const s0 = isEntry ? 0.0 : spanLen * 0.5 + trenchGap;
      const s1 = isEntry ? spanLen * 0.5 - trenchGap : spanLen;
      const y0 = isEntry ? ay : deckY;
      const y1 = isEntry ? deckY : by;
      const xm = ax + dirX * ((s0 + s1) * 0.5);
      const zm = az + dirZ * ((s0 + s1) * 0.5);
      const ym = (y0 + y1) * 0.5;
      const pitch = Math.atan2(y1 - y0, halfSpanSegLen);

      this._dummy.position.set(xm, ym - 0.18, zm);
      this._euler.set(pitch, yaw, 0, 'YXZ');
      this._dummy.rotation.copy(this._euler);
      this._dummy.scale.set(causewayHalfWidth * 2.04, 0.36, halfSpanSegLen + 0.25);
      this._dummy.updateMatrix();
      ironMats.push(this._dummy.matrix.clone());

      // Heavy Riveted Side Box-Girders stopping crisply at the Gear-Wall Portal!
      for (const side of [-1, 1]) {
        const rx = xm + rightX * side * (causewayHalfWidth + 0.16);
        const rz = zm + rightZ * side * (causewayHalfWidth + 0.16);
        this._dummy.position.set(rx, ym + 0.32, rz);
        this._euler.set(pitch, yaw, 0, 'YXZ');
        this._dummy.rotation.copy(this._euler);
        this._dummy.scale.set(0.36, 0.85, halfSpanSegLen);
        this._dummy.updateMatrix();
        ironMats.push(this._dummy.matrix.clone());
      }
    }

    // 2. Heavy Riveted Cast-Iron Portal Bearing Frame straddling the Giant Vertical Gear-Wall!
    const frameGroup = new THREE.Group();
    frameGroup.position.set(wallX, axleY, wallZ);
    frameGroup.rotation.y = yaw;
    chunkGroup.add(frameGroup);

    // Fore & Aft Axle Pillow-Block Bearing Pedestals plunging into the -36m Molten Pit!
    for (const zSide of [-1, 1]) {
      this._dummy.position.set(
        wallX - dirX * zSide * 2.4,
        axleY - 16.0,
        wallZ - dirZ * zSide * 2.4
      );
      this._dummy.rotation.set(0, yaw, 0);
      this._dummy.scale.set(5.2, 34.0, 1.8);
      this._dummy.updateMatrix();
      gantryMats.push(this._dummy.matrix.clone());
    }

    // Longitudinal Steel Axle Shaft supporting the 27m Vertical Gear-Wall!
    const mainAxle = new THREE.Mesh(this.shaftCylGeo, this.castIronMat);
    mainAxle.rotation.x = Math.PI * 0.5;
    mainAxle.scale.set(3.4, 8.2, 3.4);
    frameGroup.add(mainAxle);

    // Side Gantry Bearing Towers + Overhead Crossbeam framing the Giant Gear!
    for (const side of [-1, 1]) {
      const towerX = wallX + rightX * side * 16.4;
      const towerZ = wallZ + rightZ * side * 16.4;
      this._dummy.position.set(towerX, deckY - 6.0, towerZ);
      this._dummy.rotation.set(0, yaw, 0);
      this._dummy.scale.set(2.2, 38.0, 3.8);
      this._dummy.updateMatrix();
      gantryMats.push(this._dummy.matrix.clone());

      beaconPositions.push(
        new THREE.Vector3(
          wallX + rightX * side * (causewayHalfWidth + 0.5),
          deckY + 2.4,
          wallZ + rightZ * side * (causewayHalfWidth + 0.5)
        )
      );
    }

    // 3. The Colossal `27.2m`-Diameter Vertical Rotating Slotted Gear-Wall (`gearWheelGroup`)!
    const gearWheelGroup = new THREE.Group();
    frameGroup.add(gearWheelGroup);

    // 3a. Outer Toothed Burnished-Brass Gear Ring (`r = 11.2m .. 14.85m`, 32 involute teeth!)
    const outerRingMesh = new THREE.Mesh(this.verticalOuterToothedRingGeo, this.brassGearMat);
    outerRingMesh.castShadow = true;
    outerRingMesh.receiveShadow = true;
    gearWheelGroup.add(outerRingMesh);

    // 3b. Inner Cast-Iron Axle Boss (`r = 4.45m`)
    const innerHubMesh = new THREE.Mesh(this.verticalHubCylGeo, this.castIronMat);
    gearWheelGroup.add(innerHubMesh);

    // 3c. The `4` Solid Cast-Iron Wall Quadrants + Glowing Brass Jamb Chevrons framing the 4 Single-Car Passages!
    for (let q = 0; q < 4; q++) {
      const qRot = q * (Math.PI * 0.5);
      const sectorMesh = new THREE.Mesh(this.verticalSolidWallSectorGeo, this.castIronMat);
      sectorMesh.rotation.z = qRot;
      sectorMesh.castShadow = true;
      sectorMesh.receiveShadow = true;
      gearWheelGroup.add(sectorMesh);

      // Raised Brass Stiffener Rib down the middle of each solid iron quadrant!
      const midAng = qRot + Math.PI * 0.25;
      const midR = (slotInnerR + slotOuterR) * 0.5;
      const rib = new THREE.Mesh(this.unitBoxGeo, this.copperBoilerMat);
      rib.position.set(Math.cos(midAng) * midR, Math.sin(midAng) * midR, 0);
      rib.rotation.z = midAng;
      rib.scale.set(slotOuterR - slotInnerR - 0.3, 0.85, halfThick * 2.18);
      gearWheelGroup.add(rib);

      // Glowing Amber Brass Clearance Trim Bars framing both jambs of each 1-Car Passage!
      for (const jambSign of [-1, 1]) {
        const jambAng = qRot + jambSign * (slotHalfAng + 0.035);
        const trim = new THREE.Mesh(this.unitBoxGeo, this.rampChevronMat);
        trim.position.set(Math.cos(jambAng) * midR, Math.sin(jambAng) * midR, 0);
        trim.rotation.z = jambAng;
        trim.scale.set(slotOuterR - slotInnerR - 0.2, 0.34, halfThick * 2.22);
        gearWheelGroup.add(trim);
      }
    }

    // 4. Meshing Side Brass Drive-Pinion Gear powering the Giant Vertical Gear-Wall!
    const pinionR = 4.6;
    const pinionDist = outerRadius + pinionR + 0.95;
    const pinionMesh = new THREE.Mesh(this.cogwheel16Geo, this.brassGearMat);
    pinionMesh.position.set(spinSign * pinionDist, 6.8, 0);
    pinionMesh.rotation.x = Math.PI * 0.5;
    pinionMesh.scale.set(pinionR, 1.35, pinionR);
    frameGroup.add(pinionMesh);

    // Horizontal drive shaft into the side Gantry Bearing Tower!
    const pShaft = new THREE.Mesh(this.shaftCylGeo, this.castIronMat);
    pShaft.position.set(spinSign * pinionDist, 6.8, 0);
    pShaft.rotation.x = Math.PI * 0.5;
    pShaft.scale.set(1.6, 6.5, 1.6);
    frameGroup.add(pShaft);

    // Chevron guide arrows painted onto the road deck approaching the Gear-Wall!
    for (const dOff of [-8.5, -4.5]) {
      this._dummy.position.set(
        wallX + dirX * dOff,
        deckY + 0.02,
        wallZ + dirZ * dOff
      );
      this._dummy.rotation.set(0, yaw, 0);
      this._dummy.scale.set(3.4, 0.05, 1.4);
      this._dummy.updateMatrix();
      chevronMats.push(this._dummy.matrix.clone());
    }

    const omega = spinSign * 0.38;
    const pinionOmega = -omega * (outerRadius / pinionR);

    return {
      ax,
      ay,
      az,
      bx,
      by,
      bz,
      wallX,
      wallZ,
      deckY,
      axleY,
      dirX,
      dirZ,
      rightX,
      rightZ,
      yaw,
      spanLen,
      causewayHalfWidth,
      outerRadius,
      slotInnerR,
      slotOuterR,
      slotHalfAng,
      halfThick,
      omega,
      angle: 0.0, // Starts with a 1-car slot sweeping right into view at top-dead-center (`theta = PI/2`)!
      gearWheelGroup,
      pinionMesh,
      pinionOmega,
      pinionAngle: 0,
    };
  }

  // ===========================================================================
  // SEESAW BRIDGES, ARTICULATED PISTON GANGWAYS, CATWALKS & JUMP RAMPS
  // ===========================================================================

  private buildSeesawBeamBridge(
    chunkGroup: THREE.Group,
    ax: number,
    ay: number,
    az: number,
    bx: number,
    by: number,
    bz: number,
    halfWidth: number,
    furnacePositions: THREE.Vector3[]
  ): SeesawBeamBridge {
    const dx = bx - ax;
    const dz = bz - az;
    const length = Math.hypot(dx, dz);
    const halfLen = length * 0.5;
    const dirX = dx / length;
    const dirZ = dz / length;
    const yaw = Math.atan2(-dirX, -dirZ);
    const rightX = Math.cos(yaw);
    const rightZ = -Math.sin(yaw);

    const midX = (ax + bx) * 0.5;
    const midZ = (az + bz) * 0.5;
    const midBaseY = (ay + by) * 0.5;
    const pivotLift = 1.05;
    const baseSlope = (by - ay) / length;
    const maxTan = pivotLift / halfLen;

    const pillarH = 40.0 + midBaseY;
    const fulcrumPost = new THREE.Mesh(this.unitBoxGeo, this.castIronMat);
    fulcrumPost.position.set(midX, midBaseY + pivotLift - pillarH * 0.5 - 0.5, midZ);
    fulcrumPost.rotation.y = yaw;
    fulcrumPost.scale.set(halfWidth * 2.35, pillarH, 1.55);
    chunkGroup.add(fulcrumPost);

    const trunnionAxle = new THREE.Mesh(this.trunnionCylGeo, this.copperBoilerMat);
    trunnionAxle.position.set(midX, midBaseY + pivotLift - 0.35, midZ);
    trunnionAxle.rotation.y = yaw;
    trunnionAxle.scale.set(1.1, halfWidth * 2.7, 1.1);
    chunkGroup.add(trunnionAxle);

    furnacePositions.push(new THREE.Vector3(midX, midBaseY + pivotLift + 0.15, midZ));

    const beamMesh = new THREE.Group();
    beamMesh.position.set(midX, midBaseY + pivotLift, midZ);

    const deck = new THREE.Mesh(this.unitBoxGeo, this.seesawDeckMat);
    deck.position.set(0, -0.16, 0);
    deck.scale.set(halfWidth * 2.0, 0.32, length + 0.6);
    deck.castShadow = true;
    deck.receiveShadow = true;
    beamMesh.add(deck);

    for (const side of [-1, 1]) {
      const girder = new THREE.Mesh(this.unitBoxGeo, this.castIronMat);
      girder.position.set(side * (halfWidth + 0.12), 0.24, 0);
      girder.scale.set(0.32, 0.72, length + 0.4);
      girder.castShadow = true;
      beamMesh.add(girder);
    }

    const counterWeight = new THREE.Mesh(this.trunnionCylGeo, this.copperBoilerMat);
    counterWeight.position.set(0, -1.45, halfLen * 0.48);
    counterWeight.scale.set(1.9, halfWidth * 1.45, 1.9);
    beamMesh.add(counterWeight);

    chunkGroup.add(beamMesh);

    const seesaw: SeesawBeamBridge = {
      ax,
      ay,
      az,
      bx,
      by,
      bz,
      midX,
      midZ,
      midBaseY,
      pivotLift,
      dirX,
      dirZ,
      rightX,
      rightZ,
      yaw,
      length,
      halfLen,
      halfWidth,
      baseSlope,
      tanTilt: maxTan,
      velTan: 0,
      maxTan,
      seatedExit: false,
      beamMesh,
    };
    this.syncSeesawMesh(seesaw);
    return seesaw;
  }

  private syncSeesawMesh(sw: SeesawBeamBridge): void {
    const totalTan = sw.baseSlope + sw.tanTilt;
    const pitchX = Math.atan(totalTan);
    this._euler.set(pitchX, sw.yaw, 0, 'YXZ');
    sw.beamMesh.rotation.copy(this._euler);
  }

  private buildArticulatedPistonGangway(
    chunkGroup: THREE.Group,
    srcIsland: ClockworkGearIsland,
    dstIsland: ClockworkGearIsland,
    srcOffX: number,
    srcOffZ: number,
    dstOffX: number,
    dstOffZ: number,
    halfWidth: number
  ): ArticulatedPistonGangway {
    const ax = srcIsland.x + srcOffX;
    const az = srcIsland.z + srcOffZ;
    const bx = dstIsland.x + dstOffX;
    const bz = dstIsland.z + dstOffZ;
    const dx = bx - ax;
    const dz = bz - az;
    const length = Math.hypot(dx, dz);
    const dirX = dx / length;
    const dirZ = dz / length;
    const yaw = Math.atan2(-dirX, -dirZ);
    const rightX = Math.cos(yaw);
    const rightZ = -Math.sin(yaw);

    const bridgeGroup = new THREE.Group();
    const deck = new THREE.Mesh(this.unitBoxGeo, this.seesawDeckMat);
    deck.position.set(0, -0.15, 0);
    deck.scale.set(halfWidth * 2.04, 0.3, length + 0.8);
    deck.castShadow = true;
    deck.receiveShadow = true;
    bridgeGroup.add(deck);

    for (const side of [-1, 1]) {
      const rod = new THREE.Mesh(this.unitBoxGeo, this.copperBoilerMat);
      rod.position.set(side * (halfWidth + 0.14), 0.28, 0);
      rod.scale.set(0.3, 0.65, length + 0.6);
      bridgeGroup.add(rod);
    }

    chunkGroup.add(bridgeGroup);

    const gw: ArticulatedPistonGangway = {
      srcIsland,
      dstIsland,
      srcOffX,
      srcOffZ,
      dstOffX,
      dstOffZ,
      ax,
      az,
      bx,
      bz,
      dirX,
      dirZ,
      rightX,
      rightZ,
      yaw,
      length,
      halfWidth,
      liveAy: srcIsland.currentY + 0.03,
      liveBy: dstIsland.currentY + 0.03,
      bridgeGroup,
    };
    this.syncGangwayMesh(gw);
    return gw;
  }

  private syncGangwayMesh(gw: ArticulatedPistonGangway): void {
    gw.liveAy = gw.srcIsland.currentY + 0.03;
    gw.liveBy = gw.dstIsland.currentY + 0.03;
    const midY = (gw.liveAy + gw.liveBy) * 0.5;
    const pitchX = Math.atan2(gw.liveBy - gw.liveAy, gw.length);
    gw.bridgeGroup.position.set((gw.ax + gw.bx) * 0.5, midY, (gw.az + gw.bz) * 0.5);
    this._euler.set(pitchX, gw.yaw, 0, 'YXZ');
    gw.bridgeGroup.rotation.copy(this._euler);
  }

  private buildHighArchCatwalk(
    ax: number,
    ay: number,
    az: number,
    bx: number,
    by: number,
    bz: number,
    archBumpY: number,
    halfWidth: number,
    rootBridges: CrisscrossRootBridge[],
    nextBridgeId: () => number,
    chunkIndex: number,
    ironMats: THREE.Matrix4[],
    beaconPositions: THREE.Vector3[]
  ): void {
    const segCount = 12;
    const dx = bx - ax;
    const dz = bz - az;
    const totalLen = Math.hypot(dx, dz);
    const yaw = Math.atan2(-dx, -dz);
    const rightX = Math.cos(yaw);
    const rightZ = -Math.sin(yaw);

    const heightAt = (t: number): number =>
      lerp(ay, by, smoothstep(0, 1, t)) + archBumpY * Math.pow(Math.sin(Math.PI * t), 1.35);

    const segments: CrisscrossRootSegment[] = [];
    let minZ = Infinity;
    let maxZ = -Infinity;

    for (let s = 0; s < segCount; s++) {
      const t0 = s / segCount;
      const t1 = (s + 1) / segCount;
      const x0 = lerp(ax, bx, t0);
      const z0 = lerp(az, bz, t0);
      const y0 = heightAt(t0);
      const x1 = lerp(ax, bx, t1);
      const z1 = lerp(az, bz, t1);
      const y1 = heightAt(t1);
      minZ = Math.min(minZ, z0, z1);
      maxZ = Math.max(maxZ, z0, z1);

      const flare = s === 0 || s === segCount - 1 ? 1.3 : 0.0;
      const segHalfW = halfWidth + flare;

      segments.push({
        ax: x0,
        ay: y0 - 0.18,
        az: z0,
        bx: x1,
        by: y1 - 0.18,
        bz: z1,
        deckHalfWidth: segHalfW,
        thickness: 1.35,
      });

      const xm = (x0 + x1) * 0.5;
      const ym = (y0 + y1) * 0.5;
      const zm = (z0 + z1) * 0.5;
      const segLen = totalLen / segCount;
      const pitch = Math.atan2(y1 - y0, segLen);

      this._dummy.position.set(xm, ym - 0.14, zm);
      this._euler.set(pitch, yaw, 0, 'YXZ');
      this._dummy.rotation.copy(this._euler);
      this._dummy.scale.set(segHalfW * 2.0, 0.28, segLen + 0.12);
      this._dummy.updateMatrix();
      ironMats.push(this._dummy.matrix.clone());

      for (const side of [-1, 1]) {
        const off = side * (segHalfW - 0.15);
        this._dummy.position.set(xm + rightX * off, ym + 0.25, zm + rightZ * off);
        this._euler.set(pitch, yaw, 0, 'YXZ');
        this._dummy.rotation.copy(this._euler);
        this._dummy.scale.set(0.28, 0.5, segLen + 0.1);
        this._dummy.updateMatrix();
        ironMats.push(this._dummy.matrix.clone());
      }
    }

    beaconPositions.push(
      new THREE.Vector3(lerp(ax, bx, 0.5), heightAt(0.5) + 1.8, lerp(az, bz, 0.5))
    );

    rootBridges.push({
      id: nextBridgeId(),
      chunkIndex,
      segments,
      minZ: minZ - 2.0,
      maxZ: maxZ + 2.0,
    });
  }

  private buildFoundryJumpRamp(
    ax: number,
    ay: number,
    az: number,
    bx: number,
    by: number,
    bz: number,
    halfWidth: number,
    rootBridges: CrisscrossRootBridge[],
    nextBridgeId: () => number,
    chunkIndex: number,
    ironMats: THREE.Matrix4[],
    chevronMats: THREE.Matrix4[],
    beaconPositions: THREE.Vector3[]
  ): void {
    const dx = bx - ax;
    const dz = bz - az;
    const totalLen = Math.hypot(dx, dz);
    const yaw = Math.atan2(-dx, -dz);
    const rightX = Math.cos(yaw);
    const rightZ = -Math.sin(yaw);

    const launchEndFrac = 0.46;
    const catchStartFrac = 0.71;
    const rampRise = clamp(2.15 + Math.max(0, by - ay) * 0.35, 1.85, 3.0);
    const launchYAt = (u: number): number => ay + rampRise * Math.pow(u, 1.38);

    const segments: CrisscrossRootSegment[] = [];
    let minZ = Infinity;
    let maxZ = -Infinity;

    const launchSegs = 6;
    for (let s = 0; s < launchSegs; s++) {
      const u0 = s / launchSegs;
      const u1 = (s + 1) / launchSegs;
      const t0 = u0 * launchEndFrac;
      const t1 = u1 * launchEndFrac;

      const x0 = lerp(ax, bx, t0);
      const z0 = lerp(az, bz, t0);
      const y0 = launchYAt(u0);
      const x1 = lerp(ax, bx, t1);
      const z1 = lerp(az, bz, t1);
      const y1 = launchYAt(u1);
      minZ = Math.min(minZ, z0, z1);
      maxZ = Math.max(maxZ, z0, z1);

      segments.push({
        ax: x0,
        ay: y0 - 0.18,
        az: z0,
        bx: x1,
        by: y1 - 0.18,
        bz: z1,
        deckHalfWidth: halfWidth + 0.3,
        thickness: 1.35,
      });

      const xm = (x0 + x1) * 0.5;
      const ym = (y0 + y1) * 0.5;
      const zm = (z0 + z1) * 0.5;
      const segLen = (totalLen * launchEndFrac) / launchSegs;
      const pitch = Math.atan2(y1 - y0, segLen);

      this._dummy.position.set(xm, ym - 0.14, zm);
      this._euler.set(pitch, yaw, 0, 'YXZ');
      this._dummy.rotation.copy(this._euler);
      this._dummy.scale.set(halfWidth * 2.1, 0.28, segLen + 0.1);
      this._dummy.updateMatrix();
      if (s >= launchSegs - 2) {
        chevronMats.push(this._dummy.matrix.clone());
      } else {
        ironMats.push(this._dummy.matrix.clone());
      }
    }

    {
      const lipX = lerp(ax, bx, launchEndFrac);
      const lipZ = lerp(az, bz, launchEndFrac);
      const lipY = launchYAt(1.0);
      for (const side of [-1, 1]) {
        beaconPositions.push(
          new THREE.Vector3(
            lipX + rightX * side * (halfWidth + 0.2),
            lipY + 1.9,
            lipZ + rightZ * side * (halfWidth + 0.2)
          )
        );
      }
    }

    const catchSegs = 4;
    const catchHalfW = halfWidth + 0.9;
    const catchDrop = 0.95;
    for (let s = 0; s < catchSegs; s++) {
      const u0 = s / catchSegs;
      const u1 = (s + 1) / catchSegs;
      const t0 = lerp(catchStartFrac, 1.0, u0);
      const t1 = lerp(catchStartFrac, 1.0, u1);
      const x0 = lerp(ax, bx, t0);
      const z0 = lerp(az, bz, t0);
      const y0 = lerp(by - catchDrop, by, smoothstep(0, 1, u0));
      const x1 = lerp(ax, bx, t1);
      const z1 = lerp(az, bz, t1);
      const y1 = lerp(by - catchDrop, by, smoothstep(0, 1, u1));
      minZ = Math.min(minZ, z0, z1);
      maxZ = Math.max(maxZ, z0, z1);

      segments.push({
        ax: x0,
        ay: y0 - 0.18,
        az: z0,
        bx: x1,
        by: y1 - 0.18,
        bz: z1,
        deckHalfWidth: catchHalfW,
        thickness: 1.45,
      });

      const xm = (x0 + x1) * 0.5;
      const ym = (y0 + y1) * 0.5;
      const zm = (z0 + z1) * 0.5;
      const segLen = (totalLen * (1.0 - catchStartFrac)) / catchSegs;
      const pitch = Math.atan2(y1 - y0, segLen);

      this._dummy.position.set(xm, ym - 0.12, zm);
      this._euler.set(pitch, yaw, 0, 'YXZ');
      this._dummy.rotation.copy(this._euler);
      this._dummy.scale.set(catchHalfW * 2.0, 0.24, segLen + 0.1);
      this._dummy.updateMatrix();
      if (s === 0) {
        chevronMats.push(this._dummy.matrix.clone());
      } else {
        ironMats.push(this._dummy.matrix.clone());
      }
    }

    rootBridges.push({
      id: nextBridgeId(),
      chunkIndex,
      segments,
      minZ: minZ - 2.0,
      maxZ: maxZ + 2.0,
    });
  }

  // ===========================================================================
  // LIVE SURFACE HEIGHT, CONVEYOR VELOCITY & ROTATING GEAR-WALL COLLISION
  // ===========================================================================

  public sampleLiveClockworkSurface(
    data: ClockworkChunkData,
    x: number,
    z: number
  ): { active: boolean; height: number } {
    let bestH = -36.0;
    let found = false;

    // 1. Rotating Brass Cogwheel Islands & Rising Steam-Piston Islands!
    let skirtH = -36.0;
    let hasSkirt = false;
    for (let i = 0; i < data.islands.length; i++) {
      const isl = data.islands[i];
      const dx = x - isl.x;
      const dz = z - isl.z;
      const rSq = (dx * dx + dz * dz) / (isl.radius * isl.radius);
      if (rSq <= 0.98) {
        found = true;
        bestH = Math.max(bestH, isl.currentY);
      } else if (rSq < 1.12) {
        hasSkirt = true;
        const drop = smoothstep(0.98, 1.12, rSq);
        skirtH = Math.max(skirtH, lerp(isl.currentY, -36.0, drop));
      }
    }

    // 2. Intermittent Geneva Locking-Gear Pathways (`LOCKING_GEAR_BRIDGE`)!
    for (let l = 0; l < data.lockingPathways.length; l++) {
      const lp = data.lockingPathways[l];
      const dxA = x - lp.ax;
      const dzA = z - lp.az;
      const along = dxA * lp.dirX + dzA * lp.dirZ;

      if (along >= -0.65 && along <= lp.spanLen + 0.65) {
        const lat = dxA * lp.rightX + dzA * lp.rightZ;
        // 2a. Fixed Entry Pier (`0 .. approachLen`) & Exit Pier (`spanLen - approachLen .. spanLen`)
        if (along <= lp.approachLen + 0.25 && Math.abs(lat) <= lp.beamHalfWidth + 0.85) {
          const f = clamp(along / lp.approachLen, 0, 1);
          bestH = Math.max(bestH, lerp(lp.ay, lp.midY, f));
          found = true;
        } else if (
          along >= lp.spanLen - lp.approachLen - 0.25 &&
          Math.abs(lat) <= lp.beamHalfWidth + 0.85
        ) {
          const f = clamp((along - (lp.spanLen - lp.approachLen)) / lp.approachLen, 0, 1);
          bestH = Math.max(bestH, lerp(lp.midY, lp.by, f));
          found = true;
        }
      }

      // 2b. Live-Rotated Open-Ring Bridge-Gear!
      const rx = x - lp.midX;
      const rz = z - lp.midZ;
      const rSq = rx * rx + rz * rz;
      if (rSq <= (lp.lockR + 0.45) * (lp.lockR + 0.45)) {
        // Transform `(rx, rz)` into `lockingGearGroup`'s live rotated frame (`currentYaw`)!
        const sinY = Math.sin(lp.currentYaw);
        const cosY = Math.cos(lp.currentYaw);
        // Local -Z along (-sinY, -cosY), local +X along (cosY, -sinY)
        const localAlong = rx * -sinY + rz * -cosY;
        const localLat = rx * cosY + rz * -sinY;

        const rNorm = Math.sqrt(rSq) / lp.lockR;
        const onBridgeChord =
          Math.abs(localAlong) <= lp.lockR + 0.4 &&
          Math.abs(localLat) <= lp.beamHalfWidth + 0.25;
        const onOuterRing = rNorm >= 0.85 && rNorm <= 1.0;

        if (onBridgeChord || onOuterRing) {
          const curbLift =
            onBridgeChord && Math.abs(localLat) > lp.beamHalfWidth - 0.25
              ? smoothstep(lp.beamHalfWidth - 0.25, lp.beamHalfWidth + 0.15, Math.abs(localLat)) *
                0.26
              : 0.0;
          bestH = Math.max(bestH, lp.midY + curbLift);
          found = true;
        }
      }
    }

    // 3. Riveted Causeways under Colossal Vertical Rotating Gear-Walls (`GEAR_WALL_CAUSEWAY`)!
    for (let w = 0; w < data.gearWalls.length; w++) {
      const gw = data.gearWalls[w];
      const dx = x - gw.ax;
      const dz = z - gw.az;
      const along = dx * gw.dirX + dz * gw.dirZ;
      if (along < -0.65 || along > gw.spanLen + 0.65) continue;

      const lat = dx * gw.rightX + dz * gw.rightZ;
      const flare =
        along < 4.0
          ? (1.0 - along / 4.0) * 1.1
          : along > gw.spanLen - 4.0
          ? ((along - (gw.spanLen - 4.0)) / 4.0) * 1.1
          : 0.0;
      const effW = gw.causewayHalfWidth + flare;
      if (Math.abs(lat) <= effW) {
        const f = clamp(along / gw.spanLen, 0, 1);
        const curbLift =
          Math.abs(lat) > effW - 0.35
            ? smoothstep(effW - 0.35, effW, Math.abs(lat)) * 0.28
            : 0.0;
        const h = lerp(gw.ay, gw.by, f) + curbLift;
        if (h > bestH) {
          bestH = h;
          found = true;
        }
      }
    }

    // 4. Weight-Tilting Counterweight Seesaw Balance-Beam Bridges!
    for (let s = 0; s < data.seesaws.length; s++) {
      const sw = data.seesaws[s];
      const dx = x - sw.midX;
      const dz = z - sw.midZ;
      const alongFromMid = dx * sw.dirX + dz * sw.dirZ;
      if (Math.abs(alongFromMid) > sw.halfLen + 0.65) continue;

      const lat = dx * sw.rightX + dz * sw.rightZ;
      const endFlare =
        Math.abs(alongFromMid) > sw.halfLen - 4.0
          ? smoothstep(sw.halfLen - 4.0, sw.halfLen, Math.abs(alongFromMid)) * 1.15
          : 0.0;
      const effHalfW = sw.halfWidth + endFlare;

      if (Math.abs(lat) <= effHalfW) {
        const clampedS = clamp(alongFromMid, -sw.halfLen, sw.halfLen);
        const curbLift =
          Math.abs(lat) > effHalfW - 0.35
            ? smoothstep(effHalfW - 0.35, effHalfW, Math.abs(lat)) * 0.28
            : 0.0;
        const h =
          sw.midBaseY +
          sw.pivotLift +
          clampedS * (sw.baseSlope + sw.tanTilt) +
          curbLift;
        if (h > bestH) {
          bestH = h;
          found = true;
        }
      }
    }

    // 5. Live-Articulated Steam-Piston Locomotive Gangways!
    for (let g = 0; g < data.gangways.length; g++) {
      const gw = data.gangways[g];
      const dx = x - gw.ax;
      const dz = z - gw.az;
      const along = dx * gw.dirX + dz * gw.dirZ;
      if (along < -0.65 || along > gw.length + 0.65) continue;

      const lat = dx * gw.rightX + dz * gw.rightZ;
      const endFlare =
        along < 4.0
          ? (1.0 - along / 4.0) * 1.15
          : along > gw.length - 4.0
          ? ((along - (gw.length - 4.0)) / 4.0) * 1.15
          : 0.0;
      const effHalfW = gw.halfWidth + endFlare;

      if (Math.abs(lat) <= effHalfW) {
        const f = clamp(along / gw.length, 0, 1);
        const curbLift =
          Math.abs(lat) > effHalfW - 0.35
            ? smoothstep(effHalfW - 0.35, effHalfW, Math.abs(lat)) * 0.28
            : 0.0;
        const h = lerp(gw.liveAy, gw.liveBy, f) + curbLift;
        if (h > bestH) {
          bestH = h;
          found = true;
        }
      }
    }

    if (!found && hasSkirt) {
      found = true;
      bestH = skirtH;
    }

    return { active: found, height: bestH };
  }

  public sampleSurfaceVelocityInChunk(
    data: ClockworkChunkData,
    x: number,
    z: number,
    outVel: THREE.Vector3
  ): boolean {
    // Stationary approach aprons on Seesaws, Gear-Wall Causeways, and Locking-Gear Approach Piers
    for (let s = 0; s < data.seesaws.length; s++) {
      const sw = data.seesaws[s];
      const dx = x - sw.midX;
      const dz = z - sw.midZ;
      if (
        Math.abs(dx * sw.dirX + dz * sw.dirZ) <= sw.halfLen + 0.5 &&
        Math.abs(dx * sw.rightX + dz * sw.rightZ) <= sw.halfWidth + 1.1
      ) {
        outVel.set(0, 0, 0);
        return false;
      }
    }
    for (let w = 0; w < data.gearWalls.length; w++) {
      const gw = data.gearWalls[w];
      const dx = x - gw.ax;
      const dz = z - gw.az;
      const along = dx * gw.dirX + dz * gw.dirZ;
      if (
        along >= -0.5 &&
        along <= gw.spanLen + 0.5 &&
        Math.abs(dx * gw.rightX + dz * gw.rightZ) <= gw.causewayHalfWidth + 1.0
      ) {
        outVel.set(0, 0, 0);
        return false;
      }
    }

    // Live-turning Geneva Locking Bridge-Gear (0 velocity when LOCKED, rotational velocity when INDEXING!)
    for (let l = 0; l < data.lockingPathways.length; l++) {
      const lp = data.lockingPathways[l];
      const dxA = x - lp.ax;
      const dzA = z - lp.az;
      const along = dxA * lp.dirX + dzA * lp.dirZ;
      const lat = dxA * lp.rightX + dzA * lp.rightZ;
      if (
        (along >= -0.5 && along <= lp.approachLen + 0.25 && Math.abs(lat) <= lp.beamHalfWidth + 0.9) ||
        (along >= lp.spanLen - lp.approachLen - 0.25 &&
          along <= lp.spanLen + 0.5 &&
          Math.abs(lat) <= lp.beamHalfWidth + 0.9)
      ) {
        outVel.set(0, 0, 0);
        return false;
      }

      const rx = x - lp.midX;
      const rz = z - lp.midZ;
      if (rx * rx + rz * rz <= lp.lockR * lp.lockR) {
        if (Math.abs(lp.liveOmega) < 1e-4) {
          outVel.set(0, 0, 0);
          return true;
        }
        outVel.set(lp.liveOmega * rz, 0, -lp.liveOmega * rx);
        return true;
      }
    }

    for (let i = 0; i < data.islands.length; i++) {
      const isl = data.islands[i];
      const dx = x - isl.x;
      const dz = z - isl.z;
      const distSq = dx * dx + dz * dz;
      if (distSq <= isl.radius * isl.radius) {
        if (
          isl.stationaryInnerRadius > 0 &&
          distSq <= isl.stationaryInnerRadius * isl.stationaryInnerRadius
        ) {
          outVel.set(0, 0, 0);
          return true;
        }
        if (isl.isPiston) {
          outVel.set(0, isl.velY, 0);
          return true;
        }
        outVel.set(isl.omega * dz, 0, -isl.omega * dx);
        return true;
      }
    }
    return false;
  }

  /**
   * 240Hz Collision Solver for the Colossal Vertical Rotating Slotted Gear-Walls!
   * - Allows 1 vehicle at a time to pass cleanly through any of the `4` rotating `4.8m`-wide slots!
   * - Blocks longitudinal impact against the solid `2.3m`-thick cast-iron gear wall sectors!
   * - Gently guides a car laterally if a slot jamb sweeps sideways while the car is mid-passage!
   */
  public resolveVerticalGearWallCollisionsInChunk(
    data: ClockworkChunkData,
    carPos: THREE.Vector3,
    forwardWorld: THREE.Vector3,
    rightWorld: THREE.Vector3,
    linearVel: THREE.Vector3,
    angularVel: THREE.Vector3,
    passiveProtection: boolean,
    onImpact?: (intensity: number, worldPoint: THREE.Vector3) => void
  ): void {
    if (passiveProtection || data.gearWalls.length === 0) return;

    const hullOffsets = [-1.3, 0.0, 1.2];
    const sphereRad = 0.92;
    const halfPi = Math.PI * 0.5;

    for (let w = 0; w < data.gearWalls.length; w++) {
      const gw = data.gearWalls[w];
      const dCenter =
        (carPos.x - gw.wallX) * gw.dirX + (carPos.z - gw.wallZ) * gw.dirZ;
      if (Math.abs(dCenter) > gw.halfThick + 3.2) continue;

      for (let h = 0; h < hullOffsets.length; h++) {
        const zOff = hullOffsets[h];
        const sx = carPos.x + forwardWorld.x * -zOff;
        const sz = carPos.z + forwardWorld.z * -zOff;
        const sy = carPos.y + 0.35;

        const sAlong = (sx - gw.wallX) * gw.dirX + (sz - gw.wallZ) * gw.dirZ;
        const maxLong = gw.halfThick + sphereRad;
        if (Math.abs(sAlong) >= maxLong) continue;

        // Coordinates in `frameGroup` (`lx` = right, `ly` = height above axleY)
        const lx = (sx - gw.wallX) * gw.rightX + (sz - gw.wallZ) * gw.rightZ;
        const ly = sy - gw.axleY;
        const r = Math.hypot(lx, ly);
        if (r > gw.outerRadius + sphereRad) continue;

        // Exact inverse rotation into `gearWheelGroup` (`rotation.z = gw.angle`)!
        const cosA = Math.cos(gw.angle);
        const sinA = Math.sin(gw.angle);
        const gx = lx * cosA + ly * sinA;
        const gy = -lx * sinA + ly * cosA;

        const theta = Math.atan2(gy, gx);
        const nearestSlotIdx = Math.round(theta / halfPi);
        const dAng = theta - nearestSlotIdx * halfPi; // in [-PI/4 .. +PI/4]
        const arcDistFromSlotCenter = Math.abs(dAng) * r;
        const slotHalfWidthAtR = gw.slotHalfAng * r;

        const inRadialWindow =
          r - sphereRad >= gw.slotInnerR && r + sphereRad <= gw.slotOuterR;
        const inSlotWidth = arcDistFromSlotCenter + sphereRad <= slotHalfWidthAtR;

        if (inRadialWindow && inSlotWidth) {
          // Inside the rotating 1-vehicle passage — CLEAR TO PASS THROUGH!
          continue;
        }

        if (inRadialWindow && Math.abs(sAlong) < gw.halfThick - 0.1) {
          // Car is already partway through the passage & touching a rotating brass jamb:
          // smoothly nudge laterally toward the center of the open 1-car slot!
          const pushSign = dAng > 0 ? -1 : 1; // in gear tangent direction
          // At top of wheel (`ly > 0`), +theta tangent points along `-right` (`-lx`), so push toward `dAng = 0`:
          const slotCenterWorldAng = nearestSlotIdx * halfPi + gw.angle;
          const targetLx = Math.cos(slotCenterWorldAng) * r;
          const latErr = targetLx - lx;
          const nudge = clamp(latErr, -0.35, 0.35) * 0.65;
          carPos.x += gw.rightX * nudge;
          carPos.z += gw.rightZ * nudge;
          const slotVx = -gw.omega * ly;
          const vLat = linearVel.x * gw.rightX + linearVel.z * gw.rightZ;
          linearVel.x += gw.rightX * (slotVx - vLat) * 0.35;
          linearVel.z += gw.rightZ * (slotVx - vLat) * 0.35;
          continue;
        }

        // Solid cast-iron gear wall face impact! Push longitudinally out of the rotating wall!
        const signLong = sAlong >= 0 ? 1 : -1;
        const overlap = maxLong - Math.abs(sAlong);
        const nx = gw.dirX * signLong;
        const nz = gw.dirZ * signLong;

        carPos.x += nx * overlap * 0.92;
        carPos.z += nz * overlap * 0.92;

        const vn = linearVel.x * nx + linearVel.z * nz;
        if (vn < 0) {
          const jMag = -1.28 * vn;
          linearVel.x += nx * jMag;
          linearVel.z += nz * jMag;

          // Impart slight tangential friction kick from the spinning iron gear face!
          const faceTangentialVx = -gw.omega * ly;
          linearVel.x += gw.rightX * faceTangentialVx * 0.12;
          linearVel.z += gw.rightZ * faceTangentialVx * 0.12;

          const localHitX = nx * rightWorld.x + nz * rightWorld.z;
          angularVel.y += clamp(-zOff * localHitX * jMag * 0.08, -1.6, 1.6);

          if (Math.abs(vn) > 2.4 && onImpact) {
            this._hitContact.set(sx - nx * sphereRad, sy, sz - nz * sphereRad);
            onImpact(clamp(Math.abs(vn) / 16.0, 0.25, 1.0), this._hitContact);
          }
        }
      }
    }
  }

  // ===========================================================================
  // CHUNK ANIMATION & SIMULATION STEP
  // ===========================================================================

  public stepChunkHazards(
    data: ClockworkChunkData,
    dt: number,
    elapsed: number,
    vehiclePos: THREE.Vector3,
    passiveProtection: boolean
  ): void {
    const clampedDt = Math.min(dt, 0.033);

    // 1. Rotate All Brass Cogwheel Islands & Step Reciprocating Steam-Piston Elevators!
    for (let i = 0; i < data.islands.length; i++) {
      const isl = data.islands[i];
      if (isl.isPiston) {
        const p = this.samplePistonY(isl.baseY, isl.pistonStrokeY, isl.pistonPhase, elapsed);
        isl.currentY = p.y;
        isl.velY = p.velY;
        isl.mesh.position.y = p.y;
        if (isl.hubCapMesh) isl.hubCapMesh.position.y = p.y + 0.02;

        if (p.atTop && !isl.wasAtTop) {
          isl.wasAtTop = true;
          if (this.onPistonSteamVent && data.ownedIslands.includes(isl)) {
            this.onPistonSteamVent(new THREE.Vector3(isl.x, p.y + 0.5, isl.z));
          }
        } else if (!p.atTop) {
          isl.wasAtTop = false;
        }
      } else {
        isl.angle += isl.omega * clampedDt;
        isl.mesh.rotation.y = isl.angle;
        if (isl.hubCapMesh && isl.stationaryInnerRadius <= 0) {
          isl.hubCapMesh.rotation.y = isl.angle;
        }
      }
    }

    // 2. Step Meshed Idler Gears, Overhead Bevel Transfer Gears & Wall Gears!
    for (let i = 0; i < data.drivenVisuals.length; i++) {
      const dv = data.drivenVisuals[i];
      dv.angle += dv.omega * clampedDt;
      if (dv.axis === 'Y') {
        dv.obj.rotation.y = dv.angle;
      } else if (dv.axis === 'X') {
        dv.obj.rotation.x = dv.angle;
      } else {
        dv.obj.rotation.z = dv.angle;
      }
    }

    // 3. Step Intermittent Geneva Locking-Gear Pathways!
    for (let l = 0; l < data.lockingPathways.length; l++) {
      const lp = data.lockingPathways[l];
      // Continuously-turning Brass Drive Gear NEVER stops!
      lp.driverAngle += lp.driverOmega * clampedDt;
      lp.driverGroup.rotation.y = lp.driverAngle;

      const lockState = this.sampleGenevaLockState(lp.period, lp.phase, elapsed);
      lp.currentYaw = lp.baseYaw + lockState.deltaYaw;
      lp.liveOmega = lockState.liveOmega;
      lp.isLocked = lockState.isLocked;
      lp.lockingGearGroup.rotation.y = lp.currentYaw;
      lp.pawlPivot.rotation.z = lockState.pawlLift;

      if (lp.isLocked !== lp.wasLocked) {
        const sigMat = lp.isLocked ? this.lockReadyGlowMat : this.lockTurningGlowMat;
        for (let b = 0; b < lp.beaconMeshes.length; b++) {
          lp.beaconMeshes[b].material = sigMat;
        }
        if (lp.isLocked && this.onGearPathwayLocked) {
          this.onGearPathwayLocked(new THREE.Vector3(lp.midX, lp.midY + 0.4, lp.midZ));
        }
        lp.wasLocked = lp.isLocked;
      }
    }

    // 4. Step Colossal Vertical Rotating Slotted Gear-Walls + Meshing Side Pinions!
    for (let w = 0; w < data.gearWalls.length; w++) {
      const gw = data.gearWalls[w];
      gw.angle += gw.omega * clampedDt;
      gw.gearWheelGroup.rotation.z = gw.angle;
      gw.pinionAngle += gw.pinionOmega * clampedDt;
      gw.pinionMesh.rotation.z = gw.pinionAngle;
    }

    // 5. Step Live-Articulated Steam-Piston Gangways!
    for (let g = 0; g < data.gangways.length; g++) {
      this.syncGangwayMesh(data.gangways[g]);
    }

    // 6. Step Torque-Driven Counterweight Seesaw Balance-Beam Bridges!
    for (let s = 0; s < data.seesaws.length; s++) {
      const sw = data.seesaws[s];
      const dx = vehiclePos.x - sw.midX;
      const dz = vehiclePos.z - sw.midZ;
      const alongFromMid = dx * sw.dirX + dz * sw.dirZ;
      const lat = dx * sw.rightX + dz * sw.rightZ;

      let targetTan = sw.maxTan;
      let springRate = 4.2;

      if (
        !passiveProtection &&
        Math.abs(alongFromMid) <= sw.halfLen + 1.0 &&
        Math.abs(lat) <= sw.halfWidth + 1.2
      ) {
        const expectedY =
          sw.midBaseY + sw.pivotLift + alongFromMid * (sw.baseSlope + sw.tanTilt);
        if (Math.abs(vehiclePos.y - expectedY) < 3.4) {
          const leverFrac = clamp((alongFromMid + 3.5) / (sw.halfLen * 0.32), -1.0, 1.0);
          targetTan = -leverFrac * sw.maxTan * 1.28;
          springRate = 24.0;
        }
      }

      const accelTan = (targetTan - sw.tanTilt) * springRate - sw.velTan * 7.2;
      sw.velTan += accelTan * clampedDt;
      sw.tanTilt += sw.velTan * clampedDt;

      if (sw.tanTilt <= -sw.maxTan) {
        sw.tanTilt = -sw.maxTan;
        if (sw.velTan < -0.02 && !sw.seatedExit) {
          sw.seatedExit = true;
          if (this.onSeesawAnvilSeat) {
            this.onSeesawAnvilSeat(new THREE.Vector3(sw.bx, sw.by, sw.bz));
          }
        }
        sw.velTan = Math.max(0, sw.velTan);
      } else if (sw.tanTilt >= sw.maxTan) {
        sw.tanTilt = sw.maxTan;
        sw.velTan = Math.min(0, sw.velTan);
        sw.seatedExit = false;
      } else if (sw.tanTilt > -sw.maxTan * 0.5) {
        sw.seatedExit = false;
      }

      this.syncSeesawMesh(sw);
    }
  }

  public stepGlobalFoundryAtmosphere(
    active: boolean,
    focusPos: THREE.Vector3,
    elapsed: number,
    dt: number
  ): void {
    this.atmosphereFxGroup.visible = active;
    if (!active) return;

    for (let i = 0; i < this.slagPlanes.length; i++) {
      const sp = this.slagPlanes[i];
      sp.position.x = focusPos.x + Math.sin(elapsed * 0.3 + i * 1.9) * 6.0;
      sp.position.z = focusPos.z + Math.cos(elapsed * 0.24 + i * 1.5) * 6.0;
    }

    const clampedDt = Math.min(dt, 0.05);
    const posAttr = this.emberPoints.geometry.attributes.position as THREE.BufferAttribute;
    for (let i = 0; i < this.emberCount; i++) {
      const ix = i * 3;
      let x =
        this.emberPositions[ix] +
        (this.emberVelocities[ix] + Math.sin(elapsed * 1.8 + i) * 0.9) * clampedDt;
      let y = this.emberPositions[ix + 1] + this.emberVelocities[ix + 1] * clampedDt;
      let z = this.emberPositions[ix + 2] + this.emberVelocities[ix + 2] * clampedDt;

      if (x - focusPos.x > 82) x -= 164;
      if (x - focusPos.x < -82) x += 164;
      if (y > focusPos.y + 24) y -= 56;
      if (y < focusPos.y - 32) y += 56;
      if (z - focusPos.z > 75) z -= 150;
      if (z - focusPos.z < -75) z += 150;

      this.emberPositions[ix] = x;
      this.emberPositions[ix + 1] = y;
      this.emberPositions[ix + 2] = z;
    }
    posAttr.needsUpdate = true;
  }
}

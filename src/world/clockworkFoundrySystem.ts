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

export interface IdlerGearVisual {
  mesh: THREE.Mesh;
  omega: number;
  angle: number;
  axis: 'Y' | 'X';
}

export interface SwingingPendulumVisual {
  pivotGroup: THREE.Group;
  freq: number;
  phase: number;
  maxSwingRad: number;
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

export type ClockworkSpanKind =
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
  idlers: IdlerGearVisual[];
  pendulums: SwingingPendulumVisual[];
  seesaws: SeesawBeamBridge[];
  gangways: ArticulatedPistonGangway[];
  zStart: number;
  zLength: number;
}

const ROW_SPACING_Z = 56.0;
const FIRST_ROW_Z_OFFSET = 12.0;
// Multi-gear branching & re-convergence rhythm: 1 -> 3 -> 4 -> 2 -> 3 -> 2 -> (repeat)!
const ROW_GEAR_COUNTS = [1, 3, 4, 2, 3, 2] as const;

/**
 * Builds Level [11] CLOCKWORK GEAR-ISLAND FOUNDRY & SEESAW BRIDGES ('CLOCKWORK_FOUNDRY', hotkey [-]):
 * - Colossal live-rotating interlocking Brass & Cast-Iron Cogwheel Islands (`1 -> 3 -> 4 -> 2 -> 3...`)
 *   at varied heights over a glowing -36m Molten Brass/Slag Foundry Abyss!
 * - Real 240Hz rotational conveyor surface physics: riding a spinning cogwheel physically carries and
 *   rotates your 4x4 around its rim so you can line up with 3-4 branching exits!
 * - Weight-Tilting Counterweight Seesaw Balance-Beam Bridges: entry end waits 0.00mm flush on your gear;
 *   driving past the central bronze trunnion fulcrum tips the bridge smoothly down onto the target gear!
 * - Reciprocating Giant Brass Steam-Piston Elevator Islands + Live-Articulated Locomotive Gangways
 *   that pump up and down in real time with the piston stroke!
 * - Riveted Foundry Stunt Jump Ramps + Giant Swinging Clockwork Escapement Pendulums!
 */
export class ClockworkFoundryBuilder {
  public readonly atmosphereFxGroup = new THREE.Group();

  // Materials
  private readonly brassGearMat: THREE.MeshStandardMaterial;
  private readonly castIronMat: THREE.MeshStandardMaterial;
  private readonly copperBoilerMat: THREE.MeshStandardMaterial;
  private readonly seesawDeckMat: THREE.MeshStandardMaterial;
  private readonly rampChevronMat: THREE.MeshStandardMaterial;
  private readonly furnaceCoreGlowMat: THREE.MeshBasicMaterial;
  private readonly rampBeaconGlowMat: THREE.MeshBasicMaterial;
  private readonly moltenSlagSheetMat: THREE.MeshBasicMaterial;

  // Shared Geometries
  private readonly cogwheel24Geo: THREE.BufferGeometry;
  private readonly cogwheel16Geo: THREE.BufferGeometry;
  private readonly pistonIslandGeo: THREE.BufferGeometry;
  private readonly hubMedallionGeo: THREE.CylinderGeometry;
  private readonly unitBoxGeo: THREE.BoxGeometry;
  private readonly trunnionCylGeo: THREE.CylinderGeometry;
  private readonly beaconSphereGeo: THREE.SphereGeometry;
  private readonly pendulumBobGeo: THREE.CylinderGeometry;

  // Global Molten Slag Glow Sheets & Rising Golden Forge Embers
  private readonly slagPlanes: THREE.Mesh[] = [];
  private readonly emberCount = 1100;
  private emberPoints!: THREE.Points;
  private emberPositions!: Float32Array;
  private emberVelocities!: Float32Array;

  // Callbacks
  public onSeesawAnvilSeat: ((pos: THREE.Vector3) => void) | null = null;
  public onPistonSteamVent: ((pos: THREE.Vector3) => void) | null = null;

  private readonly _dummy = new THREE.Object3D();
  private readonly _euler = new THREE.Euler();

  constructor(textures: ProceduralTextureLibrary) {
    this.atmosphereFxGroup.name = 'ClockworkFoundryAtmosphereFX';

    this.brassGearMat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.75, 0.75),
      roughness: 0.34,
      metalness: 0.78,
      vertexColors: true,
      side: THREE.DoubleSide,
    });

    this.castIronMat = new THREE.MeshStandardMaterial({
      color: 0x454a52,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.9, 0.9),
      roughness: 0.46,
      metalness: 0.74,
    });

    this.copperBoilerMat = new THREE.MeshStandardMaterial({
      color: 0xc46d3b,
      map: textures.rock.map,
      roughness: 0.35,
      metalness: 0.78,
      vertexColors: true,
      side: THREE.DoubleSide,
    });

    this.seesawDeckMat = new THREE.MeshStandardMaterial({
      color: 0x6e5b49,
      map: textures.bark.map,
      normalMap: textures.rock.normalMap,
      roughness: 0.55,
      metalness: 0.52,
    });

    this.rampChevronMat = new THREE.MeshStandardMaterial({
      color: 0xe8a829,
      map: textures.rock.map,
      roughness: 0.38,
      metalness: 0.55,
      emissive: new THREE.Color(0x804200),
      emissiveIntensity: 0.5,
    });

    this.furnaceCoreGlowMat = new THREE.MeshBasicMaterial({
      color: 0xff7b1c,
    });

    this.rampBeaconGlowMat = new THREE.MeshBasicMaterial({
      color: 0xffe066,
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
    this.pistonIslandGeo = this.createSteamPistonGeo();
    this.hubMedallionGeo = new THREE.CylinderGeometry(1.0, 1.04, 0.45, 24);
    this.hubMedallionGeo.translate(0, -0.22, 0);
    this.unitBoxGeo = new THREE.BoxGeometry(1, 1, 1);
    this.trunnionCylGeo = new THREE.CylinderGeometry(0.68, 0.68, 1.0, 14);
    this.trunnionCylGeo.rotateZ(Math.PI * 0.5);
    this.beaconSphereGeo = new THREE.SphereGeometry(0.28, 8, 6);
    this.pendulumBobGeo = new THREE.CylinderGeometry(3.4, 3.4, 0.85, 24);
    this.pendulumBobGeo.rotateX(Math.PI * 0.5);

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
   * Sculpted 3D Involute Brass & Cast-Iron Cogwheel Geometry (unit deck radius = 1.0, teeth out to 1.11,
   * deep foundry axle shaft extending down to y = -48m into the molten slag sea!)
   */
  private createBrassCogwheelGeo(toothCount: number): THREE.BufferGeometry {
    const segsPerTooth = 4;
    const radialSegs = toothCount * segsPerTooth;
    // Polar rings from center axle out to involute gear teeth and down the heavy rim & shaft!
    const rings = [
      { y: 0.0, r: 0.0, toothAmp: 0.0, col: 0x565c66 }, // Hub center
      { y: 0.0, r: 0.28, toothAmp: 0.0, col: 0xb88435 }, // Inner brass collar
      { y: 0.0, r: 0.72, toothAmp: 0.0, col: 0x525761 }, // Cast-iron spoked web deck
      { y: 0.0, r: 0.99, toothAmp: 0.0, col: 0xd9a145 }, // Outer burnished brass driving ring!
      { y: -0.07, r: 1.0, toothAmp: 0.11, col: 0xe3ac4f }, // Involute gear tooth top face (slightly recessed -7cm so bridges never z-fight!)
      { y: -1.45, r: 1.0, toothAmp: 0.11, col: 0x9c6c28 }, // Gear tooth bottom face
      { y: -1.65, r: 0.94, toothAmp: 0.0, col: 0x454952 }, // Under-rim flange
      { y: -3.2, r: 0.24, toothAmp: 0.0, col: 0x363a42 }, // Conical under-gear spider brace
      { y: -48.0, r: 0.16, toothAmp: 0.0, col: 0xff6e1a }, // Deep glowing foundry drive-shaft plunging into molten slag!
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
        // Trapezoidal involute tooth profile!
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

        // Paint 6 radial cast-iron spokes + warm furnace-glow rim contrast on the top deck!
        let spokeShade = 1.0;
        if (r === 2) {
          const spokeCos = Math.abs(Math.cos(theta * 3.0));
          spokeShade = spokeCos > 0.72 ? 1.35 : 0.76;
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
        // True +Y upward normals on deck & outward radial normals on gear teeth!
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
   * Colossal Reciprocating Copper & Brass Steam-Piston Head + Telescoping Cylinder Rod!
   */
  private createSteamPistonGeo(): THREE.BufferGeometry {
    const radialSegs = 28;
    const rings = [
      { y: 0.0, r: 0.0, col: 0xd99b4e },
      { y: 0.0, r: 0.75, col: 0xc7743c },
      { y: 0.0, r: 1.0, col: 0xe6af55 }, // Polished brass piston crown rim
      { y: -1.1, r: 1.02, col: 0x8f4d28 }, // Piston compression ring skirt
      { y: -2.4, r: 0.98, col: 0xc46d3b },
      { y: -3.2, r: 0.42, col: 0x5c636e }, // Heavy polished steel piston rod collar
      { y: -48.0, r: 0.36, col: 0xff7824 }, // Polished hydraulic piston ram plunging into furnace cylinder!
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
    // Glowing Molten Brass & Slag Crucible Sheets down at y = -28m, -33m, -36m!
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

    // Rising Incandescent Forge Sparks & Golden Steam Motes!
    this.emberPositions = new Float32Array(this.emberCount * 3);
    this.emberVelocities = new Float32Array(this.emberCount * 3);
    const rng = mulberry32(994127);
    for (let i = 0; i < this.emberCount; i++) {
      const ix = i * 3;
      this.emberPositions[ix] = (rng() - 0.5) * 165;
      this.emberPositions[ix + 1] = lerp(-32.0, 22.0, rng());
      this.emberPositions[ix + 2] = (rng() - 0.5) * 155;

      this.emberVelocities[ix] = (rng() - 0.5) * 2.4;
      this.emberVelocities[ix + 1] = lerp(2.2, 6.4, rng()); // Buoyant upward thermal draft!
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
      // Colossal Master Mainspring Gear Hub (`radius = 23m`) with calm stationary inner capstan (`r <= 6.2m`)!
      const baseY = safeRow === 0 ? 0.0 : lerp(-1.0, 2.4, rng());
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
      2: [-1.4, 3.0],
      3: [-2.2, -1.0, -1.8], // Middle slot in 3-row is a Rising Steam-Piston (-1.0m .. +3.6m)!
      4: [2.8, -2.0, 3.8, -2.4],
    };
    const heights = heightPresets[count] ?? [0];
    const totalSpanX = count === 4 ? 90.0 : count === 3 ? 72.0 : 46.0;
    const radius = count === 4 ? 12.8 : count === 3 ? 14.5 : 19.5;

    const out = [];
    for (let i = 0; i < count; i++) {
      const frac = i / (count - 1) - 0.5;
      const xOff = frac * totalSpanX;
      // Put a Reciprocating Brass Steam-Piston Elevator in the center of 3-gear rows (and slot 1 of odd 4-gear rows)!
      const isPiston =
        (count === 3 && i === 1) || (count === 4 && i === 1 && safeRow % 2 === 0);
      const baseY = heights[i] + (safeRow >= 3 ? (rng() - 0.5) * 1.0 : 0.0);
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
        pistonStrokeY: isPiston ? 4.5 : 0.0,
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
    // Smooth industrial steam-piston cycle with generous top & bottom dwell plateaus!
    const period = 7.2;
    const cyc = (((elapsed + phase) % period) + period) % period;
    const u = cyc / period; // 0..1
    let liftFrac = 0;
    let dLiftDu = 0;
    if (u < 0.18) {
      // Bottom dwell
      liftFrac = 0;
    } else if (u < 0.5) {
      // Smooth upward power stroke!
      const s = (u - 0.18) / 0.32;
      liftFrac = smoothstep(0, 1, s);
      dLiftDu = (6 * s * (1 - s)) / 0.32;
    } else if (u < 0.68) {
      // Top dead-center dwell!
      liftFrac = 1.0;
    } else {
      // Smooth downward exhaust stroke!
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

    // Rich branching & convergence across gears!
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

    const links: ClockworkGraphLink[] = rawPairs.map((p) => {
      const a = specsA[p.fromIdx];
      const b = specsB[p.toIdx];
      const dy = b.baseY - a.baseY;

      let kind: ClockworkSpanKind = 'SEESAW_BRIDGE';
      if (a.isPiston || b.isPiston) {
        // Any span touching a moving Steam-Piston Island is a Live-Articulated Locomotive Gangway!
        kind = 'PISTON_GANGWAY';
      } else if (M === 1 && N === 3) {
        // Signature Spawn Trio: Left = Seesaw Bridge, Center = Piston Gangway, Right = Foundry Stunt Jump Ramp!
        kind = p.toIdx === 2 ? 'FOUNDRY_JUMP_RAMP' : 'SEESAW_BRIDGE';
      } else if (dy <= -1.6 && p.toIdx === N - 1) {
        kind = 'FOUNDRY_JUMP_RAMP';
      } else if (dy >= 2.4) {
        kind = 'HIGH_ARCH_CATWALK';
      }

      return {
        fromIdx: p.fromIdx,
        toIdx: p.toIdx,
        kind,
        archBumpY: kind === 'HIGH_ARCH_CATWALK' ? 1.8 : 0.0,
      };
    });

    // Separate any 2D crossing spans cleanly in 3D: Upper = High-Arch Catwalk, Lower = Seesaw / Gangway!
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
        upper.archBumpY = 5.4;
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
  // CHUNK POPULATION
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
    const idlers: IdlerGearVisual[] = [];
    const pendulums: SwingingPendulumVisual[] = [];
    const seesaws: SeesawBeamBridge[] = [];
    const gangways: ArticulatedPistonGangway[] = [];

    const ironMats: THREE.Matrix4[] = [];
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

          // Central cast-iron axle capstan medallion (stationary on Row 0 Master Hub!)
          const capR =
            sp.stationaryInnerRadius > 0 ? sp.stationaryInnerRadius : sp.radius * 0.2;
          hubCapMesh = new THREE.Mesh(this.hubMedallionGeo, this.castIronMat);
          hubCapMesh.position.set(sp.x, sp.baseY + 0.02, sp.z);
          hubCapMesh.scale.set(capR, 1.0, capR);
          hubCapMesh.receiveShadow = true;
          group.add(hubCapMesh);

          furnacePositions.push(new THREE.Vector3(sp.x, sp.baseY + 0.32, sp.z));

          // Spawn an Interlocking Side-Idler Cogwheel meshing teeth-to-teeth beside outer rotating gears!
          if (!sp.isPiston && (sp.islandIdx === 0 || sp.islandIdx === specList.length - 1)) {
            const outSide = sp.islandIdx === 0 ? -1 : 1;
            const idlerR = lerp(6.8, 9.2, rng());
            const meshDist = sp.radius + idlerR + sp.radius * 0.08 + idlerR * 0.08;
            const idlerMesh = new THREE.Mesh(this.cogwheel16Geo, this.brassGearMat);
            idlerMesh.position.set(sp.x + outSide * meshDist, sp.baseY - 0.12, sp.z + 2.5);
            idlerMesh.scale.set(idlerR, 1.0, idlerR);
            group.add(idlerMesh);
            idlers.push({
              mesh: idlerMesh,
              omega: -sp.omega * (sp.radius / idlerR), // Exact mechanical gear-ratio sync!
              angle: 0.2,
              axis: 'Y',
            });
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
    const row2 = spawnIslandList(specs2, false); // Ghost physics reference for seamless cross-chunk bridges!

    // Colossal Vertical Escapement Wall-Gears & Overhead Swinging Brass Pendulums framing the cavern!
    for (let side of [-1, 1]) {
      const zWall = zStart - (side < 0 ? 34.0 : 82.0);
      const xWall = sampleCourseX(zWall) + side * 72.0;
      const wallGear = new THREE.Mesh(this.cogwheel24Geo, this.brassGearMat);
      wallGear.position.set(xWall, 12.0, zWall);
      wallGear.rotation.z = side * Math.PI * 0.5;
      const wgR = lerp(16.0, 22.0, rng());
      wallGear.scale.set(wgR, 1.0, wgR);
      group.add(wallGear);
      idlers.push({
        mesh: wallGear,
        omega: side * 0.22,
        angle: rng() * Math.PI,
        axis: 'X',
      });
    }

    // Majestic Overhead Swinging Brass Clockwork Pendulum (`yPivot = +34m`, bob swings at `y = +10m` high overhead!)
    {
      const zPend = zStart - 56.0;
      const xPend = sampleCourseX(zPend);
      const pivotGroup = new THREE.Group();
      pivotGroup.position.set(xPend, 35.0, zPend);

      const rodLen = 23.5;
      const rod = new THREE.Mesh(this.unitBoxGeo, this.castIronMat);
      rod.position.set(0, -rodLen * 0.5, 0);
      rod.scale.set(0.36, rodLen, 0.36);
      pivotGroup.add(rod);

      const bob = new THREE.Mesh(this.pendulumBobGeo, this.copperBoilerMat);
      bob.position.set(0, -rodLen, 0);
      pivotGroup.add(bob);

      const core = new THREE.Mesh(this.beaconSphereGeo, this.furnaceCoreGlowMat);
      core.position.set(0, -rodLen, 0);
      core.scale.setScalar(3.5);
      pivotGroup.add(core);

      group.add(pivotGroup);
      pendulums.push({
        pivotGroup,
        freq: 1.15,
        phase: chunkIndex * 1.9,
        maxSwingRad: 0.48,
      });
    }

    // Build all Seesaw Bridges, Articulated Piston Gangways, High-Arch Catwalks & Foundry Jump Ramps!
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
        const src = srcList[link.fromIdx];
        const dst = dstList[link.toIdx];
        const outArr = outBy.get(link.fromIdx)!;
        const inArr = inBy.get(link.toIdx)!;
        const halfWidth = 3.85;

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

        if (link.kind === 'SEESAW_BRIDGE') {
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

    // Warm molten up-light illuminating the brass undersides of the giant cogwheels!
    const forgeLight = new THREE.PointLight(0xff7722, 22.0, 56.0, 1.3);
    forgeLight.position.set(row0[0].x, -10.0, row0[0].z - 28.0);
    group.add(forgeLight);

    return {
      group,
      islands,
      ownedIslands,
      idlers,
      pendulums,
      seesaws,
      gangways,
      zStart,
      zLength,
    };
  }

  // ===========================================================================
  // SPAN BUILDERS
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
    const pivotLift = 1.05; // +1.05m trunnion lift -> entry flush on A, exit +2.10m raised until tipped!
    const baseSlope = (by - ay) / length;
    const maxTan = pivotLift / halfLen;

    // Stationary Central Bronze Trunnion Fulcrum Pillar rising from the molten foundry pit!
    const pillarH = 40.0 + midBaseY;
    const fulcrumPost = new THREE.Mesh(this.unitBoxGeo, this.castIronMat);
    fulcrumPost.position.set(midX, midBaseY + pivotLift - pillarH * 0.5 - 0.5, midZ);
    fulcrumPost.rotation.y = yaw;
    fulcrumPost.scale.set(halfWidth * 2.35, pillarH, 1.45);
    chunkGroup.add(fulcrumPost);

    const trunnionAxle = new THREE.Mesh(this.trunnionCylGeo, this.copperBoilerMat);
    trunnionAxle.position.set(midX, midBaseY + pivotLift - 0.35, midZ);
    trunnionAxle.rotation.y = yaw;
    trunnionAxle.scale.set(1, halfWidth * 2.6, 1);
    chunkGroup.add(trunnionAxle);

    furnacePositions.push(new THREE.Vector3(midX, midBaseY + pivotLift + 0.15, midZ));

    // Pivoting Riveted Iron & Timber Seesaw Beam Group!
    const beamMesh = new THREE.Group();
    beamMesh.position.set(midX, midBaseY + pivotLift, midZ);

    const deck = new THREE.Mesh(this.unitBoxGeo, this.seesawDeckMat);
    deck.position.set(0, -0.16, 0);
    deck.scale.set(halfWidth * 2.0, 0.32, length + 0.6);
    deck.castShadow = true;
    deck.receiveShadow = true;
    beamMesh.add(deck);

    // Left & Right Riveted Cast-Iron Box-Girder Side Rails
    for (const side of [-1, 1]) {
      const girder = new THREE.Mesh(this.unitBoxGeo, this.castIronMat);
      girder.position.set(side * (halfWidth + 0.12), 0.24, 0);
      girder.scale.set(0.32, 0.72, length + 0.4);
      girder.castShadow = true;
      beamMesh.add(girder);
    }

    // Heavy Copper Counterweight Drum slung beneath the entry (+Z local) half!
    const counterWeight = new THREE.Mesh(this.trunnionCylGeo, this.copperBoilerMat);
    counterWeight.position.set(0, -1.35, halfLen * 0.48);
    counterWeight.scale.set(1.8, halfWidth * 1.4, 1.8);
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
      tanTilt: maxTan, // Starts resting flush on entry Gear A!
      velTan: 0,
      maxTan,
      seatedExit: false,
      beamMesh,
    };
    this.syncSeesawMesh(seesaw);
    return seesaw;
  }

  private syncSeesawMesh(sw: SeesawBeamBridge): void {
    // Local -Z points along (dirX, dirZ) from A (s = -halfL) to B (s = +halfL):
    // Positive pitchX lifts local -Z (exit B) and lowers local +Z (entry A)!
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

    // Locomotive Connecting-Rod Brass Side Trusses + Hinge Knuckles!
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

    beaconPositions.push(new THREE.Vector3(lerp(ax, bx, 0.5), heightAt(0.5) + 1.8, lerp(az, bz, 0.5)));

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

    // Twin Launch-Lip Beacons!
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

    // Wide Forgiving Sloped Steel Catch-Landing Apron (`t = 0.71 .. 1.0`)!
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
  // LIVE SURFACE HEIGHT, CONVEYOR VELOCITY & SEESAW / GEAR / PISTON SIMULATION
  // ===========================================================================

  public sampleLiveClockworkSurface(
    data: ClockworkChunkData,
    x: number,
    z: number
  ): { active: boolean; height: number } {
    let bestH = -36.0;
    let found = false;

    // 1. Check Rotating Brass Cogwheel Islands & Rising Steam-Piston Islands!
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

    // 2. Check Weight-Tilting Counterweight Seesaw Balance-Beam Bridges!
    for (let s = 0; s < data.seesaws.length; s++) {
      const sw = data.seesaws[s];
      const dx = x - sw.midX;
      const dz = z - sw.midZ;
      const alongFromMid = dx * sw.dirX + dz * sw.dirZ; // [-halfLen .. +halfLen]
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

    // 3. Check Live-Articulated Steam-Piston Locomotive Gangways!
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

  /**
   * Computes real horizontal tangential conveyor velocity `(-omega * dz, velY, +omega * dx)`
   * at wheel contact `(x, z)` so spinning Brass Cogwheels & Rising Steam-Pistons physically
   * carry and rotate the 4x4 through real tire contact!
   */
  public sampleSurfaceVelocityInChunk(
    data: ClockworkChunkData,
    x: number,
    z: number,
    outVel: THREE.Vector3
  ): boolean {
    // Stationary seesaw decks overlap gear rims at their flared sockets — once on a seesaw deck,
    // tires grip the stationary seesaw beam rather than the gear rotating beneath it!
    for (let s = 0; s < data.seesaws.length; s++) {
      const sw = data.seesaws[s];
      const dx = x - sw.midX;
      const dz = z - sw.midZ;
      const along = dx * sw.dirX + dz * sw.dirZ;
      if (Math.abs(along) <= sw.halfLen + 0.5) {
        const lat = dx * sw.rightX + dz * sw.rightZ;
        if (Math.abs(lat) <= sw.halfWidth + 1.1) {
          outVel.set(0, 0, 0);
          return false;
        }
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
        // In Three.js, rotation.y += omega * dt maps (x, z) -> (x*cos + z*sin, -x*sin + z*cos):
        // d/dt (dx, dz) = (+omega * dz, -omega * dx)!
        outVel.set(isl.omega * dz, 0, -isl.omega * dx);
        return true;
      }
    }
    return false;
  }

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

    // 2. Step Decorative Meshed Idler Gears & Vertical Wall Escapement Gears!
    for (let i = 0; i < data.idlers.length; i++) {
      const idl = data.idlers[i];
      idl.angle += idl.omega * clampedDt;
      if (idl.axis === 'Y') {
        idl.mesh.rotation.y = idl.angle;
      } else {
        idl.mesh.rotation.x = idl.angle;
      }
    }

    // 3. Step Overhead Swinging Brass Clockwork Pendulums!
    for (let p = 0; p < data.pendulums.length; p++) {
      const pend = data.pendulums[p];
      pend.pivotGroup.rotation.z =
        Math.sin(elapsed * pend.freq + pend.phase) * pend.maxSwingRad;
    }

    // 4. Step Live-Articulated Steam-Piston Gangways!
    for (let g = 0; g < data.gangways.length; g++) {
      this.syncGangwayMesh(data.gangways[g]);
    }

    // 5. Step Torque-Driven Counterweight Seesaw Balance-Beam Bridges!
    for (let s = 0; s < data.seesaws.length; s++) {
      const sw = data.seesaws[s];
      const dx = vehiclePos.x - sw.midX;
      const dz = vehiclePos.z - sw.midZ;
      const alongFromMid = dx * sw.dirX + dz * sw.dirZ;
      const lat = dx * sw.rightX + dz * sw.rightZ;

      // Default copper counterweight gently biases `tanTilt` toward `+maxTan` (entry end flush on Gear A!)
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
          // Vehicle weight torque! Approaching & crossing the central bronze trunnion (`s > -3.5m`)
          // drives targetTan past the anvil stop (`1.28x`) so the beam seats crisply onto exit Gear B!
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

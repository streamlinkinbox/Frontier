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

export interface CargoMegashipInstance {
  x: number;
  z: number;
  baseY: number;
  halfBeam: number;
  halfLength: number;
  /** Wave animation parameters */
  heaveAmp: number;
  heaveFreq: number;
  pitchAmp: number;
  pitchFreq: number;
  rollAmp: number;
  rollFreq: number;
  phase: number;
  /** Live state updated each frame */
  liveCenterY: number;
  dCenterYDt: number;
  livePitch: number;
  dPitchDt: number;
  liveRoll: number;
  dRollDt: number;
  rowIdx: number;
  shipIdx: number;
  shipGroup: THREE.Group;
  radarSpinner: THREE.Object3D | null;
  containerBoxes: LocalContainerBox[];
  rooftopRamps: LocalRampSpan[];
  surfWashZ: number;
  surfWashPhase: number;
  surfFoamMesh: THREE.Mesh | null;
}

/**
 * Overhead Port Ship-to-Shore Gantry Crane that slides & locks a coupled 40ft Container Drawbridge
 * across the storm-tossed ocean channel between Ship A's bow and Ship B's stern!
 */
export interface GantryContainerBridge {
  srcShip: CargoMegashipInstance;
  dstShip: CargoMegashipInstance;
  srcLocalX: number;
  srcLocalZ: number;
  dstLocalX: number;
  dstLocalZ: number;
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
 * whose endpoints track the live 3D pitching/rolling bow & stern of both ships!
 */
export interface ArticulatedShipSpan {
  kind: 'RORO_BRIDGE' | 'BOW_JUMP_RAMP';
  srcShip: CargoMegashipInstance;
  dstShip: CargoMegashipInstance;
  srcLocalX: number;
  srcLocalZ: number;
  dstLocalX: number;
  dstLocalZ: number;
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

  // Materials
  private readonly hullSteelMat: THREE.MeshStandardMaterial;
  private readonly deckSteelMat: THREE.MeshStandardMaterial;
  private readonly bridgeWhiteMat: THREE.MeshStandardMaterial;
  private readonly bridgeWindowGlowMat: THREE.MeshBasicMaterial;
  private readonly containerMat: THREE.MeshStandardMaterial;
  private readonly containerWoodFloorMat: THREE.MeshStandardMaterial;
  private readonly gantryCraneMat: THREE.MeshStandardMaterial;
  private readonly gantryDarkSteelMat: THREE.MeshStandardMaterial;
  private readonly hazardStripeMat: THREE.MeshStandardMaterial;
  private readonly lockGreenBeaconMat: THREE.MeshBasicMaterial;
  private readonly slideAmberBeaconMat: THREE.MeshBasicMaterial;
  private readonly navPortRedMat: THREE.MeshBasicMaterial;
  private readonly navStarboardGreenMat: THREE.MeshBasicMaterial;
  private readonly oceanStormMat: THREE.ShaderMaterial;
  private readonly deckSurfFoamMat: THREE.MeshBasicMaterial;

  // Shared Geometries
  private readonly shipHullGeo: THREE.BufferGeometry;
  private readonly corrugatedBoxGeo: THREE.BufferGeometry;
  private readonly unitBoxGeo: THREE.BoxGeometry;
  private readonly cableCylGeo: THREE.CylinderGeometry;
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

  private readonly _dummy = new THREE.Object3D();
  private readonly _euler = new THREE.Euler();
  private readonly _color = new THREE.Color();
  private readonly _hitContact = new THREE.Vector3();

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
      roughness: 0.38, // Wet rain-slicked steel weather deck!
      metalness: 0.65,
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

    this.gantryDarkSteelMat = new THREE.MeshStandardMaterial({
      color: 0x262f38,
      map: textures.rock.map,
      roughness: 0.48,
      metalness: 0.76,
    });

    this.hazardStripeMat = new THREE.MeshStandardMaterial({
      color: 0xf2b629,
      map: textures.rock.map,
      roughness: 0.36,
      metalness: 0.52,
      emissive: new THREE.Color(0x6e4200),
      emissiveIntensity: 0.45,
    });

    this.lockGreenBeaconMat = new THREE.MeshBasicMaterial({
      color: 0x5cff8a,
    });

    this.slideAmberBeaconMat = new THREE.MeshBasicMaterial({
      color: 0xff5224,
    });

    this.navPortRedMat = new THREE.MeshBasicMaterial({
      color: 0xff3b30,
    });

    this.navStarboardGreenMat = new THREE.MeshBasicMaterial({
      color: 0x34eb77,
    });

    this.oceanStormMat = new THREE.ShaderMaterial({
      uniforms: {
        uTime: { value: 0 },
        uFlash: { value: 0 },
      },
      vertexShader: `
        uniform float uTime;
        varying vec3 vWorldPos;
        varying float vWaveCrest;
        void main() {
          vec4 wp = modelMatrix * vec4(position, 1.0);
          float swell1 = sin(wp.x * 0.055 + wp.z * 0.038 + uTime * 1.45) * 1.35;
          float swell2 = cos(wp.x * 0.09 - wp.z * 0.075 - uTime * 1.95) * 0.68;
          float chop = sin(wp.x * 0.28 + wp.z * 0.24 + uTime * 3.4) * 0.22;
          float totalH = swell1 + swell2 + chop;
          wp.y += totalH;
          vWorldPos = wp.xyz;
          vWaveCrest = smoothstep(0.75, 2.05, totalH);
          gl_Position = projectionMatrix * viewMatrix * wp;
        }
      `,
      fragmentShader: `
        uniform float uTime;
        uniform float uFlash;
        varying vec3 vWorldPos;
        varying float vWaveCrest;
        void main() {
          vec3 troughCol = vec3(0.03, 0.08, 0.13);
          vec3 midCol = vec3(0.07, 0.19, 0.26);
          vec3 foamCol = vec3(0.74, 0.86, 0.92);
          float streak = sin(vWorldPos.x * 0.45 + sin(vWorldPos.z * 0.18 + uTime * 1.2) * 2.2) * 0.5 + 0.5;
          vec3 col = mix(troughCol, midCol, smoothstep(0.0, 0.6, vWaveCrest + streak * 0.25));
          col = mix(col, foamCol, vWaveCrest * (0.45 + 0.45 * streak));
          col += vec3(0.22, 0.28, 0.36) * uFlash;
          gl_FragColor = vec4(col, 0.96);
        }
      `,
      transparent: true,
    });

    const foamTex = this.createSurfFoamTexture();
    this.deckSurfFoamMat = new THREE.MeshBasicMaterial({
      map: foamTex,
      color: 0xd8f3ff,
      transparent: true,
      opacity: 0.62,
      depthWrite: false,
      side: THREE.DoubleSide,
    });

    this.shipHullGeo = this.createCargoShipHullGeo();
    this.corrugatedBoxGeo = new THREE.BoxGeometry(1, 1, 1);
    this.unitBoxGeo = new THREE.BoxGeometry(1, 1, 1);
    this.cableCylGeo = new THREE.CylinderGeometry(0.12, 0.12, 1.0, 8);
    this.beaconSphereGeo = new THREE.SphereGeometry(0.34, 8, 6);
    this.surfPlaneGeo = new THREE.PlaneGeometry(1, 1);
    this.surfPlaneGeo.rotateX(-Math.PI * 0.5);

    this.initStormOceanAtmosphere();
  }

  private createCorrugatedContainerTexture(): THREE.CanvasTexture {
    const canvas = document.createElement('canvas');
    canvas.width = 256;
    canvas.height = 128;
    const ctx = canvas.getContext('2d')!;
    ctx.fillStyle = '#d8dde3';
    ctx.fillRect(0, 0, 256, 128);
    // Vertical steel container corrugation ribs!
    for (let x = 0; x < 256; x += 8) {
      ctx.fillStyle = x % 16 === 0 ? '#aeb6c0' : '#f2f5f8';
      ctx.fillRect(x, 6, 4, 116);
    }
    // Heavy top & bottom ISO frame rails + corner castings!
    ctx.fillStyle = '#8b949e';
    ctx.fillRect(0, 0, 256, 7);
    ctx.fillRect(0, 121, 256, 7);
    ctx.fillRect(0, 0, 9, 128);
    ctx.fillRect(247, 0, 9, 128);
    // Crisp white cargo stencil markings
    ctx.fillStyle = 'rgba(255,255,255,0.65)';
    ctx.fillRect(22, 20, 38, 8);
    ctx.fillRect(22, 33, 24, 5);
    const tex = new THREE.CanvasTexture(canvas);
    tex.wrapS = THREE.RepeatWrapping;
    tex.wrapT = THREE.ClampToEdgeWrapping;
    tex.needsUpdate = true;
    return tex;
  }

  private createSurfFoamTexture(): THREE.CanvasTexture {
    const canvas = document.createElement('canvas');
    canvas.width = 128;
    canvas.height = 128;
    const ctx = canvas.getContext('2d')!;
    ctx.clearRect(0, 0, 128, 128);
    const grad = ctx.createRadialGradient(64, 64, 6, 64, 64, 62);
    grad.addColorStop(0.0, 'rgba(235, 250, 255, 0.90)');
    grad.addColorStop(0.55, 'rgba(145, 215, 238, 0.52)');
    grad.addColorStop(1.0, 'rgba(60, 130, 165, 0.0)');
    ctx.fillStyle = grad;
    ctx.fillRect(0, 0, 128, 128);
    const tex = new THREE.CanvasTexture(canvas);
    tex.needsUpdate = true;
    return tex;
  }

  /**
   * Sculpted Unit Cargo Megaship Hull Geometry (`x in [-1, +1]` = halfBeam, `z in [-1, +1]` = halfLength,
   * flat Main Weather Deck at `y = 0.0`, crimson anti-fouling lower hull down to `y = -20.0m`!)
   */
  private createCargoShipHullGeo(): THREE.BufferGeometry {
    const radialSegs = 44;
    // Profile rings from flat steel weather deck (`y = 0`) down the flared freeboard, Plimsoll waterline,
    // crimson anti-fouling hull & bulbous bow keel!
    const rings = [
      { y: 0.0, scaleR: 0.0, bowPinch: 1.0, col: 0x353e47 }, // Center deck
      { y: 0.0, scaleR: 0.98, bowPinch: 1.0, col: 0x3c4650 }, // Gunwale deck edge (+Y normal!)
      { y: 0.42, scaleR: 1.0, bowPinch: 1.0, col: 0xd9dce0 }, // Low steel safety coaming curb
      { y: -0.25, scaleR: 1.01, bowPinch: 1.0, col: 0x1e2833 }, // Upper hull sheer strake
      { y: -8.5, scaleR: 0.98, bowPinch: 0.92, col: 0x18212b }, // Dark navy armored freeboard
      { y: -11.2, scaleR: 0.95, bowPinch: 0.88, col: 0xe6edf2 }, // Crisp white Plimsoll waterline stripe!
      { y: -12.2, scaleR: 0.94, bowPinch: 0.95, col: 0x8f241e }, // Crimson anti-fouling lower hull + bulbous bow!
      { y: -18.5, scaleR: 0.74, bowPinch: 0.85, col: 0x611612 }, // Deep bilge keel
      { y: -20.5, scaleR: 0.0, bowPinch: 0.7, col: 0x3d0d0b }, // Keel bottom close
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
        const rawX = Math.cos(theta);
        const rawZ = Math.sin(theta);

        // Boxy rectangular-amidships ship planform (`superellipse` with tapered forward bow at `rawZ < 0`!)
        const signX = Math.sign(rawX);
        const signZ = Math.sign(rawZ);
        const rectX = signX * Math.pow(Math.abs(rawX), 0.36);
        const rectZ = signZ * Math.pow(Math.abs(rawZ), 0.34);

        // Slight ship bow taper at forward `-Z` (`rectZ < -0.55`) while keeping wide bow sockets!
        const bowFactor = rectZ < -0.55 ? lerp(1.0, 0.78 * sp.bowPinch, (-rectZ - 0.55) / 0.45) : 1.0;

        const px = rectX * sp.scaleR * bowFactor;
        const pz = rectZ * sp.scaleR;

        pos[pPtr] = px;
        pos[pPtr + 1] = sp.y;
        pos[pPtr + 2] = pz;

        uv[uvPtr++] = r <= 1 ? px * 3.5 : u * 8.0;
        uv[uvPtr++] = r <= 1 ? pz * 8.0 : sp.y * 0.2;

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

  private initStormOceanAtmosphere(): void {
    // Vast 340m x 320m 3D Vertex-Displaced Storm Ocean Surface at y = -14.5m!
    const oceanGeo = new THREE.PlaneGeometry(340, 320, 72, 72);
    oceanGeo.rotateX(-Math.PI * 0.5);
    this.oceanMesh = new THREE.Mesh(oceanGeo, this.oceanStormMat);
    this.oceanMesh.position.set(0, -14.5, 0);
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

  // ===========================================================================
  // EXACT 3D PITCH / ROLL / HEAVE SHIP TRANSFORM KINEMATICS
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
  } {
    const hArg = elapsed * spec.heaveFreq + spec.phase;
    const pArg = elapsed * spec.pitchFreq + spec.phase * 1.35 + 0.4;
    const rArg = elapsed * spec.rollFreq + spec.phase * 0.85 + 1.1;

    return {
      centerY: spec.baseY + spec.heaveAmp * Math.sin(hArg),
      dCenterYDt: spec.heaveAmp * spec.heaveFreq * Math.cos(hArg),
      pitch: spec.pitchAmp * Math.sin(pArg),
      dPitchDt: spec.pitchAmp * spec.pitchFreq * Math.cos(pArg),
      roll: spec.rollAmp * Math.sin(rArg),
      dRollDt: spec.rollAmp * spec.rollFreq * Math.cos(rArg),
    };
  }

  /**
   * Exact world Y of a point `(lx, localY, lz)` inside `shipGroup` rotated by `Euler(pitch, 0, roll, 'YXZ')`!
   * In Three.js `'YXZ'` with `yaw = 0`: `v_world = Rx(pitch) * Rz(roll) * v_local + center`:
   *   `y1 = lx * sin(roll) + localY * cos(roll)`
   *   `worldY = liveCenterY + y1 * cos(pitch) - lz * sin(pitch)`!
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
          baseY: isSpawnFlagship ? 1.8 : 2.6,
          halfBeam: 16.5, // Colossal 33m-wide Ultra-Large Container Vessel (ULCV)!
          halfLength: 42.0, // 84m hull length!
          heaveAmp: isSpawnFlagship ? 0.55 : 0.85,
          heaveFreq: 0.95,
          pitchAmp: isSpawnFlagship ? 0.018 : 0.028,
          pitchFreq: 0.82,
          rollAmp: isSpawnFlagship ? 0.036 : 0.056,
          rollFreq: 0.74,
          phase: safeRow * 1.7,
          rowIdx: safeRow,
          shipIdx: 0,
        },
      ];
    }

    const totalSpanX = count === 3 ? 88.0 : 50.0;
    const halfBeam = count === 3 ? 13.5 : 15.0;
    const halfLength = 40.0;
    const baseHeights = count === 3 ? [1.6, 3.8, 1.2] : [1.4, 3.4];

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
        heaveAmp: lerp(0.7, 1.05, rng()),
        heaveFreq: lerp(0.88, 1.18, rng()),
        pitchAmp: lerp(0.022, 0.034, rng()),
        pitchFreq: lerp(0.75, 0.98, rng()),
        rollAmp: lerp(0.048, 0.068, rng()),
        rollFreq: lerp(0.68, 0.92, rng()),
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

    const s = (u - lockWindow) / (1.0 - lockWindow); // 0..1
    const maxSlide = 14.5 * slideDirSign;
    const sinWave = Math.sin(Math.PI * s);
    const dSinDs = Math.PI * Math.cos(Math.PI * s);

    return {
      latOffset: maxSlide * sinWave,
      latVel: (maxSlide * dSinDs) / (period * (1.0 - lockWindow)),
      liftY: 1.65 * sinWave,
      isLocked: false,
    };
  }

  public getRowLinks(
    rowIdx: number,
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
        // Opening Flagship Bow Fork: Left = Port Gantry Crane Locking Container Bridge, Right = Articulated Ro-Ro Truss Bridge!
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
      return { x: s0.x, y: s0.baseY, z: s0.z + s0.halfLength - 10.0, yaw: 0 };
    }

    // Spawn on the clear Stern Apron (`+halfLength - 9.0m`) facing forward (`-Z`, yaw = 0) down the container hold!
    return {
      x: best.x,
      y: best.baseY + 0.1,
      z: best.z + best.halfLength - 9.5,
      yaw: 0,
    };
  }

  // ===========================================================================
  // CHUNK POPULATION — CARGO MEGASHIPS, CONTAINER MAZES & PORT GANTRY CRANES
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
        let radarSpinner: THREE.Object3D | null = null;
        let surfFoamMesh: THREE.Mesh | null = null;

        // Populate local container & ramp geometry AND physics boxes identically so ghost ships have exact deck heights!
        this.buildMegashipDeckCargoAndSuperstructure(
          shipGroup,
          sp,
          visualOwned,
          containerBoxes,
          rooftopRamps,
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
          phase: sp.phase,
          liveCenterY: sp.baseY,
          dCenterYDt: 0,
          livePitch: 0,
          dPitchDt: 0,
          liveRoll: 0,
          dRollDt: 0,
          rowIdx: sp.rowIdx,
          shipIdx: sp.shipIdx,
          shipGroup,
          radarSpinner,
          containerBoxes,
          rooftopRamps,
          surfWashZ: -sp.halfLength * 0.52,
          surfWashPhase: sp.phase * 1.4,
          surfFoamMesh,
        };
        ships.push(inst);
        if (visualOwned) ownedShips.push(inst);
        out.push(inst);
      }
      return out;
    };

    const rowCur = spawnShipRow(specsCur, true);
    const rowNext = spawnShipRow(specsNext, false);

    // Connect Bow of `rowCur` ships across the 32m Storm Ocean Channel to Stern of `rowNext` ships!
    const links = this.getRowLinks(chunkIndex, specsCur, specsNext);
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
      const srcLocalZ = -src.halfLength + 2.4; // Forward Bow Socket!
      const dstLocalX = computeEndLocalX(
        dst.halfBeam,
        inArr.indexOf(link),
        inArr.length,
        src.x,
        dst.x
      );
      const dstLocalZ = dst.halfLength - 2.4; // Aft Stern Socket!

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

    // Warm Maritime Sodium Floodlight illuminating the Flagship Container Deck!
    const deckFlood = new THREE.PointLight(0xffcb85, 22.0, 68.0, 1.3);
    deckFlood.position.set(rowCur[0].x, rowCur[0].baseY + 14.5, rowCur[0].z + 8.0);
    group.add(deckFlood);

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

  /**
   * Populates a Cargo Megaship's Hull, Drive-Under White Bridge Superstructure,
   * Multi-Color 40ft Shipping Container Stacks, Open-Door Drive-Through Container Tunnel,
   * and Elevated Ro-Ro Container Rooftop Highway!
   */
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
    onRadar: (r: THREE.Object3D) => void,
    onSurfFoam: (m: THREE.Mesh) => void
  ): void {
    const rng = mulberry32(sp.rowIdx * 91813 + sp.shipIdx * 17321 + 551);
    const hb = sp.halfBeam;
    const hl = sp.halfLength;

    if (visualOwned) {
      // 1. Sculpted 3D Bulbous-Bow Steel Ship Hull + Non-Skid Weather Deck!
      const hullMesh = new THREE.Mesh(this.shipHullGeo, this.hullSteelMat);
      hullMesh.scale.set(hb, 1.0, hl);
      hullMesh.castShadow = true;
      hullMesh.receiveShadow = true;
      shipGroup.add(hullMesh);

      // Port (Red) & Starboard (Green) Bow Navigation Beacons!
      const portBeacon = new THREE.Mesh(this.beaconSphereGeo, this.navPortRedMat);
      portBeacon.position.set(-hb * 0.78, 1.4, -hl * 0.88);
      portBeacon.scale.setScalar(1.35);
      shipGroup.add(portBeacon);

      const stbdBeacon = new THREE.Mesh(this.beaconSphereGeo, this.navStarboardGreenMat);
      stbdBeacon.position.set(hb * 0.78, 1.4, -hl * 0.88);
      stbdBeacon.scale.setScalar(1.35);
      shipGroup.add(stbdBeacon);

      // 2. Iconic White Multi-Story Navigation Bridge Superstructure spanning HIGH OVERHEAD (`y = +6.4m .. +13.2m`)
      // on Twin Steel Bridge-Wing Towers (`z = +hl * 0.42`) so players drive both deck & rooftop highways right underneath!
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

      // Glowing Warm Bridge Navigation Windows!
      const winStrip = new THREE.Mesh(this.unitBoxGeo, this.bridgeWindowGlowMat);
      winStrip.position.set(0, 10.9, bridgeZ - 3.45);
      winStrip.scale.set(hb * 1.85, 0.95, 0.15);
      shipGroup.add(winStrip);

      // Twin Maritime Exhaust Funnels + Spinning Marine Radar Scanner!
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

      // Whitewater Storm Surf Sheet washing across the forward bow waist!
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

    // =========================================================================
    // LANE A (PORT / CENTER-LEFT): OPEN-DOOR 40FT SHIPPING CONTAINER TUNNEL!
    // =========================================================================
    // Players can thread straight INSIDE this open-ended 40ft Cargo Container (`width = 5.4m` clear, `length = 17.5m`)!
    const flipSides = (sp.rowIdx + sp.shipIdx) % 2 === 1;
    const sideMult = flipSides ? -1 : 1;

    const tunnelCenterX = -4.6 * sideMult;
    const tunnelCenterZ = 2.0;
    const tunnelClearHalfW = 2.55; // 5.1m clear inside width for exhilarating drive-through!
    const tunnelWallThick = 0.34;
    const tunnelLen = 17.5;
    const tunnelHeight = 4.25;

    // Left & Right solid steel container walls of the Open-Door Container Tunnel
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

      // Left wall, Right wall, & Corrugated Container Roof of the Open-Door Tunnel!
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

        // Swung-Open Steel Container Doors at both Aft (`+Z`) & Fore (`-Z`) ends!
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

      // Wooden container floor planks + warm interior cargo strip-light!
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

    // =========================================================================
    // LANE B (STARBOARD / CENTER-RIGHT): ELEVATED CONTAINER ROOFTOP HIGHWAY!
    // =========================================================================
    // Sloped Ro-Ro Steel Ramp (`z: +24.5m -> +12.5m`, `y: 0 -> +3.0m`) -> `27m`-long Double-Wide Container Roof (`z: +12.5m -> -14.5m`, `y = +3.0m`)
    // -> Sloped Down-Ramp / Jump Lip (`z: -14.5m -> -24.5m`)!
    const roofLaneX = 5.4 * sideMult;
    const roofW = 6.2;
    const roofY = 3.0;

    // 1. Aft Up-Ramp (`lz: +24.5m` down-end -> `+12.5m` top-end; note `lzStart < lzEnd`!)
    rooftopRamps.push({
      lx: roofLaneX,
      lzStart: 12.5,
      lzEnd: 24.5,
      halfW: roofW * 0.5,
      yStart: roofY,
      yEnd: 0.0,
    });

    // 2. Two end-to-end Double-Wide 40ft Container Stacks (`lz: -14.5m .. +12.5m`) with `drivableRoof = true`!
    addClosedContainerStack(roofLaneX, 5.75, roofW, 13.5, 1, true);
    addClosedContainerStack(roofLaneX, -7.75, roofW, 13.5, 1, true);

    // 3. Forward Rooftop Launch / Down-Ramp (`lz: -23.5m` bow-end -> `-14.5m` roof-end)!
    rooftopRamps.push({
      lx: roofLaneX,
      lzStart: -23.5,
      lzEnd: -14.5,
      halfW: roofW * 0.5,
      yStart: 0.0,
      yEnd: roofY,
    });

    if (visualOwned) {
      // Visual Steel Ro-Ro Diamond-Plate Ramps + Hazard Chevrons!
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

    // =========================================================================
    // PERIMETER MULTI-STORY SHIPPING CONTAINER CANYON WALLS (`2-3` TIERS TALL!)
    // =========================================================================
    if (hb >= 14.2) {
      for (const zSlot of [-16.0, 0.0, 16.0]) {
        // Outer Port & Starboard high container stacks framing the Megaship!
        addClosedContainerStack(-hb + 2.2, zSlot, 3.2, 13.2, zSlot === 0 ? 3 : 2, false);
        addClosedContainerStack(hb - 2.2, zSlot, 3.2, 13.2, zSlot === -16.0 ? 3 : 2, false);
      }
    }

    // Center-bow divider stack (`z = -24.0m`) splitting Port & Starboard bow bridge approaches cleanly!
    addClosedContainerStack(0.0, -24.0, 2.8, 8.5, 2, false);

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
    const spanLen = Math.hypot(dx, dz);
    const dirX = dx / spanLen;
    const dirZ = dz / spanLen;
    const yaw = Math.atan2(-dirX, -dirZ);
    const rightX = Math.cos(yaw);
    const rightZ = -Math.sin(yaw);

    const midX = (ax + bx) * 0.5;
    const midZ = (az + bz) * 0.5;
    const boomY = 27.5;
    const halfWidth = 4.1;

    // 1. Stationary Port Ship-to-Shore Gantry Crane Portal Frame rising out of the Storm Sea!
    const portalGroup = new THREE.Group();
    portalGroup.position.set(midX, 0, midZ);
    portalGroup.rotation.y = yaw;
    chunkGroup.add(portalGroup);

    const boomHalfSpan = 32.0;
    for (const side of [-1, 1]) {
      // Twin A-Frame Gantry Legs planted on sea caissons down at y = -22m!
      for (const zOff of [-4.2, 4.2]) {
        const leg = new THREE.Mesh(this.unitBoxGeo, this.gantryCraneMat);
        leg.position.set(side * 25.0, (boomY - 22.0) * 0.5 - 22.0 + 11.0, zOff);
        leg.scale.set(1.65, boomY + 22.0, 1.45);
        leg.castShadow = true;
        portalGroup.add(leg);
      }
      // Sea-caisson concrete/steel dolphin base
      const caisson = new THREE.Mesh(this.unitBoxGeo, this.gantryDarkSteelMat);
      caisson.position.set(side * 25.0, -13.0, 0);
      caisson.scale.set(5.2, 12.0, 12.5);
      portalGroup.add(caisson);
    }

    // Twin Overhead Horizontal Gantry Crane Booms (`64m` wide!)
    for (const zOff of [-4.2, 4.2]) {
      const boom = new THREE.Mesh(this.unitBoxGeo, this.gantryCraneMat);
      boom.position.set(0, boomY, zOff);
      boom.scale.set(boomHalfSpan * 2.0, 1.85, 1.5);
      portalGroup.add(boom);
    }

    // Lock-Status Harbor Strobe Beacons on Entry!
    const beaconMeshes: THREE.Mesh[] = [];
    for (const side of [-1, 1]) {
      const sig = new THREE.Mesh(this.beaconSphereGeo, this.lockGreenBeaconMat);
      sig.position.set(side * (halfWidth + 1.8), srcShip.baseY + 3.4, spanLen * 0.36);
      sig.scale.setScalar(2.1);
      portalGroup.add(sig);
      beaconMeshes.push(sig);
    }

    // 2. Live-Rolling Overhead Crane Trolley (`trolleyGroup` inside `portalGroup`)!
    const trolleyGroup = new THREE.Group();
    trolleyGroup.position.set(0, boomY - 1.2, 0);
    const trolleyCab = new THREE.Mesh(this.unitBoxGeo, this.gantryDarkSteelMat);
    trolleyCab.scale.set(9.2, 1.6, 10.4);
    trolleyGroup.add(trolleyCab);
    portalGroup.add(trolleyGroup);

    // 3. Suspended 40ft Container Drawbridge Span (`bridgeSpanGroup` in world space)!
    const bridgeSpanGroup = new THREE.Group();
    chunkGroup.add(bridgeSpanGroup);

    // Flat-Rack / Open-Top Coupled 40ft Bridge Container Deck + Low Corrugated Side Bulwarks!
    const deckSlab = new THREE.Mesh(this.unitBoxGeo, this.deckSteelMat);
    deckSlab.position.set(0, -0.22, 0);
    deckSlab.scale.set(halfWidth * 2.0, 0.44, spanLen + 1.2);
    deckSlab.castShadow = true;
    deckSlab.receiveShadow = true;
    bridgeSpanGroup.add(deckSlab);

    const centerStripe = new THREE.Mesh(this.unitBoxGeo, this.hazardStripeMat);
    centerStripe.position.set(0, 0.02, 0);
    centerStripe.scale.set(0.65, 0.05, spanLen + 0.6);
    bridgeSpanGroup.add(centerStripe);

    for (const side of [-1, 1]) {
      const sideWall = new THREE.Mesh(this.corrugatedBoxGeo, this.gantryCraneMat);
      sideWall.position.set(side * (halfWidth + 0.16), 0.55, 0);
      sideWall.scale.set(0.36, 1.35, spanLen + 0.4);
      sideWall.castShadow = true;
      bridgeSpanGroup.add(sideWall);

      // 4 Vertical Steel Crane Hoist Cables rising from the Container Spreader up to the Overhead Trolley!
      for (const zCable of [-spanLen * 0.28, spanLen * 0.28]) {
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
      ax,
      az,
      bx,
      bz,
      dirX,
      dirZ,
      rightX,
      rightZ,
      yaw,
      spanLen,
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
    gb.liveAy =
      this.shipLocalToWorldY(gb.srcShip, gb.srcLocalX, 0.04, gb.srcLocalZ) + gb.currentLiftY;
    gb.liveBy =
      this.shipLocalToWorldY(gb.dstShip, gb.dstLocalX, 0.04, gb.dstLocalZ) + gb.currentLiftY;

    const midX = (gb.ax + gb.bx) * 0.5 + gb.rightX * gb.currentLatOffset;
    const midZ = (gb.az + gb.bz) * 0.5 + gb.rightZ * gb.currentLatOffset;
    const midY = (gb.liveAy + gb.liveBy) * 0.5;
    const pitch = Math.atan2(gb.liveBy - gb.liveAy, gb.spanLen);

    gb.bridgeSpanGroup.position.set(midX, midY, midZ);
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
    const spanLen = Math.hypot(dx, dz);
    const dirX = dx / spanLen;
    const dirZ = dz / spanLen;
    const yaw = Math.atan2(-dirX, -dirZ);
    const rightX = Math.cos(yaw);
    const rightZ = -Math.sin(yaw);
    const halfWidth = 4.15;

    const launchGroup = new THREE.Group();
    chunkGroup.add(launchGroup);
    let catchGroup: THREE.Group | null = null;

    if (kind === 'RORO_BRIDGE') {
      const deck = new THREE.Mesh(this.unitBoxGeo, this.deckSteelMat);
      deck.position.set(0, -0.18, 0);
      deck.scale.set(halfWidth * 2.04, 0.36, spanLen + 1.2);
      deck.castShadow = true;
      deck.receiveShadow = true;
      launchGroup.add(deck);

      for (const side of [-1, 1]) {
        const truss = new THREE.Mesh(this.unitBoxGeo, this.hazardStripeMat);
        truss.position.set(side * (halfWidth + 0.15), 0.42, 0);
        truss.scale.set(0.32, 0.95, spanLen + 0.8);
        launchGroup.add(truss);
      }
    } else {
      // Forecastle Bow Stunt Jump Ramp (`0 .. 0.48 * spanLen`) + Wide Stern Catch Apron (`0.72 .. 1.0 * spanLen`)!
      const rampLen = spanLen * 0.48;
      const rampDeck = new THREE.Mesh(this.unitBoxGeo, this.hazardStripeMat);
      rampDeck.position.set(0, -0.16, 0);
      rampDeck.scale.set((halfWidth + 0.4) * 2.0, 0.34, rampLen + 0.8);
      rampDeck.castShadow = true;
      launchGroup.add(rampDeck);

      catchGroup = new THREE.Group();
      const catchLen = spanLen * 0.28;
      const catchDeck = new THREE.Mesh(this.unitBoxGeo, this.deckSteelMat);
      catchDeck.position.set(0, -0.16, 0);
      catchDeck.scale.set((halfWidth + 1.1) * 2.0, 0.32, catchLen + 0.8);
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
      ax,
      az,
      bx,
      bz,
      dirX,
      dirZ,
      rightX,
      rightZ,
      yaw,
      spanLen,
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
    sp.liveAy = this.shipLocalToWorldY(sp.srcShip, sp.srcLocalX, 0.04, sp.srcLocalZ);
    sp.liveBy = this.shipLocalToWorldY(sp.dstShip, sp.dstLocalX, 0.04, sp.dstLocalZ);

    if (sp.kind === 'RORO_BRIDGE') {
      const midY = (sp.liveAy + sp.liveBy) * 0.5;
      const pitch = Math.atan2(sp.liveBy - sp.liveAy, sp.spanLen);
      sp.launchGroup.position.set((sp.ax + sp.bx) * 0.5, midY, (sp.az + sp.bz) * 0.5);
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
        this._euler.set(cPitch, sp.yaw, 0, 'YXZ');
        sp.catchGroup.rotation.copy(this._euler);
      }
    }
  }

  // ===========================================================================
  // LIVE SURFACE HEIGHT, MOVING-SHIP VELOCITY & CONTAINER WALL COLLISIONS
  // ===========================================================================

  public sampleLiveCargoSurface(
    data: CargoMegashipChunkData,
    x: number,
    z: number
  ): { active: boolean; height: number } {
    let bestH = -15.5;
    let found = false;

    // 1. Pitching, Rolling & Heaving Cargo Megaship Decks + Elevated Container Rooftop Highways!
    for (let i = 0; i < data.ships.length; i++) {
      const s = data.ships[i];
      const lx = x - s.x;
      const lz = z - s.z;
      if (Math.abs(lx) > s.halfBeam + 0.8 || Math.abs(lz) > s.halfLength + 0.8) continue;

      const bowPinch =
        lz < -s.halfLength * 0.55
          ? lerp(1.0, 0.78, (-lz - s.halfLength * 0.55) / (s.halfLength * 0.45))
          : 1.0;
      const effHalfBeam = s.halfBeam * bowPinch;
      if (Math.abs(lx) > effHalfBeam) continue;

      // Base steel weather deck (`localY = 0`) + subtle gunwale safety coaming curb!
      let localY = 0.0;
      if (Math.abs(lx) > effHalfBeam - 0.45) {
        localY += smoothstep(effHalfBeam - 0.45, effHalfBeam, Math.abs(lx)) * 0.34;
      }

      // Check Sloped Ro-Ro Container Rooftop Ramps (`y: 0 -> +3.0m`)!
      for (let r = 0; r < s.rooftopRamps.length; r++) {
        const rmp = s.rooftopRamps[r];
        if (
          Math.abs(lx - rmp.lx) <= rmp.halfW &&
          lz >= rmp.lzStart - 0.25 &&
          lz <= rmp.lzEnd + 0.25
        ) {
          const f = clamp((lz - rmp.lzStart) / (rmp.lzEnd - rmp.lzStart), 0, 1);
          localY = Math.max(localY, lerp(rmp.yStart, rmp.yEnd, f));
        }
      }

      // Check Drivable 40ft Container Rooftop Highways (`localY = +3.0m`)!
      for (let c = 0; c < s.containerBoxes.length; c++) {
        const cb = s.containerBoxes[c];
        if (!cb.drivableRoof) continue;
        if (Math.abs(lx - cb.lx) <= cb.halfW && Math.abs(lz - cb.lz) <= cb.halfL) {
          localY = Math.max(localY, cb.topY);
        }
      }

      const worldH = this.shipLocalToWorldY(s, lx, localY, lz);
      if (worldH > bestH) {
        bestH = worldH;
        found = true;
      }
    }

    // 2. Port Gantry Crane Sliding & Locking 40ft Container Drawbridges!
    for (let g = 0; g < data.gantryBridges.length; g++) {
      const gb = data.gantryBridges[g];
      // Live origin shifted laterally by `gb.currentLatOffset`!
      const liveAx = gb.ax + gb.rightX * gb.currentLatOffset;
      const liveAz = gb.az + gb.rightZ * gb.currentLatOffset;
      const dx = x - liveAx;
      const dz = z - liveAz;
      const along = dx * gb.dirX + dz * gb.dirZ;
      if (along < -0.75 || along > gb.spanLen + 0.75) continue;

      const lat = dx * gb.rightX + dz * gb.rightZ;
      const effW = gb.halfWidth + (along < 3.0 || along > gb.spanLen - 3.0 ? 0.65 : 0.0);
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
      if (along < -0.75 || along > sp.spanLen + 0.75) continue;

      const lat = dx * sp.rightX + dz * sp.rightZ;
      if (sp.kind === 'RORO_BRIDGE') {
        const effW = sp.halfWidth + (along < 3.5 || along > sp.spanLen - 3.5 ? 0.85 : 0.0);
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
        // BOW_JUMP_RAMP: Launch (`0 .. 0.48`) + Open Ocean Gap (`0.48 .. 0.72`) + Catch Apron (`0.72 .. 1.0`)!
        const rampEnd = sp.spanLen * 0.48;
        const catchStart = sp.spanLen * 0.72;
        if (along <= rampEnd && Math.abs(lat) <= sp.halfWidth + 0.4) {
          const u = clamp(along / rampEnd, 0, 1);
          const h = lerp(sp.liveAy, sp.liveAy + 2.35, u);
          if (h > bestH) {
            bestH = h;
            found = true;
          }
        } else if (along >= catchStart && Math.abs(lat) <= sp.halfWidth + 1.1) {
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
    // 1. Sliding Gantry Crane Container Bridge velocity!
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

    // 2. Pitching, Rolling & Heaving Ship Deck + Whitewater Surf Wash velocity!
    for (let i = 0; i < data.ships.length; i++) {
      const s = data.ships[i];
      const lx = x - s.x;
      const lz = z - s.z;
      if (Math.abs(lx) <= s.halfBeam && Math.abs(lz) <= s.halfLength) {
        const vy =
          s.dCenterYDt +
          lx * Math.cos(s.liveRoll) * s.dRollDt -
          lz * Math.cos(s.livePitch) * s.dPitchDt;
        // Subtle lateral surf push when crossing the foaming bow surf zone!
        let surfVx = 0;
        if (Math.abs(lz - s.surfWashZ) < 6.5 && s.surfFoamMesh && s.surfFoamMesh.visible) {
          surfVx = Math.sin(s.surfWashPhase) * 1.45;
        }
        outVel.set(surfVx, vy, 0);
        return true;
      }
    }
    return false;
  }

  /**
   * 240Hz Rigid-Body Box Collision against Shipping-Container Stacks & Open-Door Container Tunnel Walls!
   * - Allows driving cleanly OVER `drivableRoof` containers when `carY >= roofWorldY - 0.55m`!
   * - Allows driving cleanly INSIDE Open-Door Container Tunnels while solid container sides guide/deflect!
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
    if (passiveProtection) return;

    const hullOffsets = [-1.25, 0.0, 1.15];
    const sphereRad = 0.94;

    for (let i = 0; i < data.ships.length; i++) {
      const s = data.ships[i];
      if (
        Math.abs(carPos.x - s.x) > s.halfBeam + 3.0 ||
        Math.abs(carPos.z - s.z) > s.halfLength + 3.0
      ) {
        continue;
      }

      for (let c = 0; c < s.containerBoxes.length; c++) {
        const cb = s.containerBoxes[c];
        const roofWorldY = this.shipLocalToWorldY(s, cb.lx, cb.topY, cb.lz);
        // If car is up on the Container Rooftop Highway (or jumping over this stack), skip wall collision!
        if (carPos.y >= roofWorldY - (cb.drivableRoof ? 0.58 : -0.15)) {
          continue;
        }

        const boxWorldX = s.x + cb.lx;
        const boxWorldZ = s.z + cb.lz;

        for (let h = 0; h < hullOffsets.length; h++) {
          const zOff = hullOffsets[h];
          const sx = carPos.x + forwardWorld.x * -zOff;
          const sz = carPos.z + forwardWorld.z * -zOff;

          const clampedX = clamp(sx, boxWorldX - cb.halfW, boxWorldX + cb.halfW);
          const clampedZ = clamp(sz, boxWorldZ - cb.halfL, boxWorldZ + cb.halfL);
          const dx = sx - clampedX;
          const dz = sz - clampedZ;
          const distSq = dx * dx + dz * dz;

          if (distSq < sphereRad * sphereRad) {
            let nx = 0;
            let nz = 0;
            let overlap = 0;

            if (distSq > 1e-6) {
              const dist = Math.sqrt(distSq);
              nx = dx / dist;
              nz = dz / dist;
              overlap = sphereRad - dist;
            } else {
              // Sphere center inside AABB — push out along shallowest face!
              const penX = cb.halfW - Math.abs(sx - boxWorldX);
              const penZ = cb.halfL - Math.abs(sz - boxWorldZ);
              if (penX < penZ) {
                nx = sx >= boxWorldX ? 1 : -1;
                overlap = penX + sphereRad;
              } else {
                nz = sz >= boxWorldZ ? 1 : -1;
                overlap = penZ + sphereRad;
              }
            }

            carPos.x += nx * overlap * 0.9;
            carPos.z += nz * overlap * 0.9;

            const vn = linearVel.x * nx + linearVel.z * nz;
            if (vn < 0) {
              const jMag = -1.22 * vn;
              linearVel.x += nx * jMag;
              linearVel.z += nz * jMag;

              const localHitX = nx * rightWorld.x + nz * rightWorld.z;
              angularVel.y += clamp(-zOff * localHitX * jMag * 0.075, -1.5, 1.5);

              if (Math.abs(vn) > 2.6 && onImpact) {
                this._hitContact.set(clampedX, carPos.y, clampedZ);
                onImpact(clamp(Math.abs(vn) / 16.0, 0.25, 1.0), this._hitContact);
              }
            }
          }
        }
      }
    }
  }

  // ===========================================================================
  // CHUNK & GLOBAL STORM ANIMATION STEP
  // ===========================================================================

  public stepChunkHazards(
    data: CargoMegashipChunkData,
    dt: number,
    elapsed: number
  ): void {
    const clampedDt = Math.min(dt, 0.033);

    // 1. Pitch, Roll & Heave all Cargo Megaships on the 3D Ocean Swell!
    for (let i = 0; i < data.ships.length; i++) {
      const s = data.ships[i];
      const pose = this.sampleShipPose(s, elapsed);
      s.liveCenterY = pose.centerY;
      s.dCenterYDt = pose.dCenterYDt;
      s.livePitch = pose.pitch;
      s.dPitchDt = pose.dPitchDt;
      s.liveRoll = pose.roll;
      s.dRollDt = pose.dRollDt;

      s.shipGroup.position.set(s.x, s.liveCenterY, s.z);
      this._euler.set(s.livePitch, 0, s.liveRoll, 'YXZ');
      s.shipGroup.rotation.copy(this._euler);

      if (s.radarSpinner) {
        s.radarSpinner.rotation.y = elapsed * 2.6;
      }

      s.surfWashPhase = elapsed * 1.35 + s.phase;
      if (s.surfFoamMesh) {
        const waveSweep = Math.sin(s.surfWashPhase);
        s.surfFoamMesh.position.x = waveSweep * (s.halfBeam * 0.35);
        s.surfFoamMesh.visible = Math.abs(waveSweep) > 0.22;
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

    // 3. Sync Articulated Ro-Ro Bridges & Bow Jump Ramps to both ships' live bow/stern!
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
      return;
    }

    const clampedDt = Math.min(dt, 0.05);
    this.oceanMesh.position.x = focusPos.x;
    this.oceanMesh.position.z = focusPos.z;
    this.oceanStormMat.uniforms.uTime.value = elapsed;

    // Periodic Distant Storm-Cloud Lightning Flashes!
    this.lightningTimer -= clampedDt;
    if (this.lightningTimer <= 0) {
      this.lightningTimer = 5.5 + ((Math.sin(elapsed * 7.3) * 0.5 + 0.5) * 6.5);
      this.currentLightningFlash = 0.85;
      if (this.onStormThunder) {
        this.onStormThunder(0.75);
      }
    } else {
      this.currentLightningFlash = Math.max(0, this.currentLightningFlash - clampedDt * 3.4);
    }
    this.oceanStormMat.uniforms.uFlash.value = this.currentLightningFlash;

    // Step Slanting Storm Rain Streaks!
    const posAttr = this.rainPoints.geometry.attributes.position as THREE.BufferAttribute;
    for (let i = 0; i < this.rainCount; i++) {
      const ix = i * 3;
      let x = this.rainPositions[ix] + 7.5 * clampedDt; // Crosswind slant!
      let y = this.rainPositions[ix + 1] - 34.0 * clampedDt; // Fast driving rain!
      let z = this.rainPositions[ix + 2] + 3.0 * clampedDt;

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

import * as THREE from 'three';
import { ProceduralTextureLibrary } from '../graphics/proceduralTextures';
import { clamp, fbm2D, lerp, mulberry32, smoothstep } from '../math/noise';
import { CrisscrossRootBridge, CrisscrossRootSegment, WorldObstacle } from './trailCorridor';

export interface RockPillarLanding {
  x: number;
  z: number;
  radiusX: number;
  radiusZ: number;
  deckY: number;
  rowIdx: number;
  islandIdx: number;
}

export interface FallingPlankHalf {
  mesh: THREE.Mesh;
  vel: THREE.Vector3;
  rotVel: THREE.Vector3;
  life: number;
}

export interface SwayingRopeBridge {
  ax: number;
  ay: number;
  az: number;
  bx: number;
  by: number;
  bz: number;
  dirX: number;
  dirZ: number;
  rightX: number;
  rightZ: number;
  yaw: number;
  length: number;
  halfWidth: number;
  maxCatenaryDrop: number;
  plankCount: number;
  plankSpacing: number;

  // Live 1D coupled wave + torsional roll + wind sway state per plank
  restY: Float32Array;
  sagY: Float32Array;
  velY: Float32Array;
  rollZ: Float32Array;
  velRoll: Float32Array;
  swayX: Float32Array;
  isRotten: Uint8Array;
  crackTimer: Float32Array; // > 0 when cracking right before snapping!
  snappedCooldown: Float32Array; // > 0 while plank is snapped missing!

  // Visual meshes updated live every frame!
  intactPlankIm: THREE.InstancedMesh;
  rottenPlankIm: THREE.InstancedMesh;
  postIm: THREE.InstancedMesh;
  cableRopeMesh: THREE.Mesh;
  cableRopeGeo: THREE.BufferGeometry;
  windPhase: number;
}

export type CanyonSpanKind = 'ROPE_BRIDGE' | 'TIMBER_TRESTLE' | 'JUMP_RAMP';

export interface CanyonGraphLink {
  fromIdx: number;
  toIdx: number;
  kind: CanyonSpanKind;
  archBumpY: number;
  crossClearFrac: number; // -1 if no crossing, else [0..1] along this trestle where a lower bridge passes underneath!
}

export interface CanyonBridgeChunkData {
  group: THREE.Group;
  pillars: RockPillarLanding[];
  ropeBridges: SwayingRopeBridge[];
  fallingHalves: FallingPlankHalf[];
  zStart: number;
  zLength: number;
  trestleCreakCooldown: number;
  rampCount: number;
  trestleCount: number;
}

const CABLE_STRANDS_PER_BRIDGE = 4; // Left main catenary, Right main catenary, Left handrail, Right handrail!
const CABLE_SEGS = 20;
const CABLE_SIDES = 5;

// 2 full sky-island tiers per 112m chunk -> deterministic multi-island archipelago DAG!
const ROW_SPACING_Z = 56.0;
const FIRST_ROW_Z_OFFSET = 10.0;
// Exact multi-island fan-out & convergence rhythm: 1 -> 5 -> 2 -> 3 -> 4 -> 2 -> 5 -> 3 -> (repeat)!
const ROW_ISLAND_COUNTS = [1, 5, 2, 3, 4, 2, 5, 3] as const;

/**
 * Builds Level [10] MULTI-ISLAND CANYON ROPE-BRIDGES, TIMBER TRESTLES & CHASM JUMP RAMPS ('CANYON_BRIDGES'):
 * - True Directed-Graph Sky-Island Archipelago (`1 -> 5 -> 2 -> 3 -> 4 -> 2 -> 5 -> 3 ...`):
 *     * 1 wide Hub Mesa fans out via 5 bridges & stunt ramps to 5 distinct Sky-Islands at varied heights,
 *       which re-converge onto 2 Confluence Islands, then fan out to 3 Islands, etc.!
 *     * Every island sits at its own distinct elevation (`y` from `-4.2m` low mist terraces up to `+6.2m`
 *       high sandstone watchtower pinnacles) above a `-48m` misty canyon abyss.
 * - Three Distinct Span Types:
 *     1. Physically Simulated 3D Swaying, Sagging & Twisting Wooden Suspension Rope-Bridges
 *        with cracking/snapping rotten planks and live 4-strand catenary cables.
 *     2. Sloped & Arched Rickety Timber Mine Railway Trestles (crossing high OVER lower rope-bridges!).
 *     3. Cantilevered Canyon Stunt Jump Ramps + Open `-48m` Air Gaps + Wide Sloped Catch Aprons
 *        lit by glowing amber/cyan lip beacons so drivers can launch across the chasm instead of taking a bridge!
 */
export class CanyonRopeBridgeBuilder {
  public readonly atmosphereFxGroup = new THREE.Group();

  // Materials
  private readonly pillarRockMat: THREE.MeshStandardMaterial;
  private readonly weatheredPlankMat: THREE.MeshStandardMaterial;
  private readonly rottenPlankMat: THREE.MeshStandardMaterial;
  private readonly trestleTimberMat: THREE.MeshStandardMaterial;
  private readonly braidedRopeMat: THREE.MeshStandardMaterial;
  private readonly rustedIronRailMat: THREE.MeshStandardMaterial;
  private readonly rampChevronMat: THREE.MeshStandardMaterial;
  private readonly lanternGlowMat: THREE.MeshBasicMaterial;
  private readonly rampBeaconGlowMat: THREE.MeshBasicMaterial;
  private readonly chasmMistSheetMat: THREE.MeshBasicMaterial;

  // Shared Geometries
  private readonly pillarBodyGeos: THREE.BufferGeometry[] = [];
  private readonly plankGeo: THREE.BoxGeometry;
  private readonly halfPlankGeo: THREE.BoxGeometry;
  private readonly railPostGeo: THREE.CylinderGeometry;
  private readonly anchorTowerGeo: THREE.BufferGeometry;
  private readonly unitBoxGeo: THREE.BoxGeometry;
  private readonly lanternBulbGeo: THREE.SphereGeometry;
  private readonly oreCartGeo: THREE.BufferGeometry;

  // Global Canyon Mist Sheets & Wind Dust Motes
  private readonly mistPlanes: THREE.Mesh[] = [];
  private readonly windMoteCount = 1100;
  private windPoints!: THREE.Points;
  private windPositions!: Float32Array;
  private windVelocities!: Float32Array;

  // Callbacks
  public onRottenPlankSnap: ((pos: THREE.Vector3) => void) | null = null;
  public onBridgeCreak: ((pos: THREE.Vector3) => void) | null = null;

  private readonly _dummy = new THREE.Object3D();
  private readonly _quat = new THREE.Quaternion();
  private readonly _euler = new THREE.Euler();

  constructor(textures: ProceduralTextureLibrary) {
    this.atmosphereFxGroup.name = 'CanyonBridgeAtmosphereFX';

    this.pillarRockMat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(1.25, 1.25),
      roughness: 0.86,
      metalness: 0.04,
      vertexColors: true,
      side: THREE.DoubleSide,
    });

    this.weatheredPlankMat = new THREE.MeshStandardMaterial({
      color: 0x9a7248,
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      normalScale: new THREE.Vector2(0.8, 0.8),
      roughness: 0.78,
      metalness: 0.04,
    });

    // Visibly dark, mossy, splintered rotten planks so skilled drivers can read them!
    this.rottenPlankMat = new THREE.MeshStandardMaterial({
      color: 0x3b2c1e,
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      normalScale: new THREE.Vector2(1.35, 1.35),
      roughness: 0.94,
      metalness: 0.02,
      emissive: new THREE.Color(0x1f1206),
      emissiveIntensity: 0.25,
    });

    this.trestleTimberMat = new THREE.MeshStandardMaterial({
      color: 0x735134,
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      normalScale: new THREE.Vector2(0.9, 0.9),
      roughness: 0.82,
      metalness: 0.05,
    });

    this.braidedRopeMat = new THREE.MeshStandardMaterial({
      color: 0xb89662,
      map: textures.vineStem.map,
      normalMap: textures.vineStem.normalMap,
      roughness: 0.85,
      metalness: 0.02,
      side: THREE.DoubleSide,
    });

    this.rustedIronRailMat = new THREE.MeshStandardMaterial({
      color: 0x594a42,
      map: textures.rock.map,
      roughness: 0.42,
      metalness: 0.76,
    });

    // High-visibility weathered gold/amber timber lip planks on Chasm Stunt Jump Ramps!
    this.rampChevronMat = new THREE.MeshStandardMaterial({
      color: 0xd99b26,
      map: textures.bark.map,
      roughness: 0.62,
      metalness: 0.12,
      emissive: new THREE.Color(0x6b3d05),
      emissiveIntensity: 0.45,
    });

    this.lanternGlowMat = new THREE.MeshBasicMaterial({
      color: 0xffbf54,
    });

    this.rampBeaconGlowMat = new THREE.MeshBasicMaterial({
      color: 0x5cf2ff,
    });

    const mistTex = this.createSoftCloudMistTexture();
    this.chasmMistSheetMat = new THREE.MeshBasicMaterial({
      map: mistTex,
      color: 0xdce6eb,
      transparent: true,
      opacity: 0.36,
      depthWrite: false,
      side: THREE.DoubleSide,
    });

    for (let v = 0; v < 3; v++) {
      this.pillarBodyGeos.push(this.createSculptedRockPillarGeo(v * 29.7 + 5.1));
    }

    this.plankGeo = new THREE.BoxGeometry(1.0, 0.24, 0.96);
    this.halfPlankGeo = new THREE.BoxGeometry(3.1, 0.22, 0.92);
    this.railPostGeo = new THREE.CylinderGeometry(0.075, 0.09, 1.38, 6);
    this.railPostGeo.translate(0, 0.69, 0);
    this.anchorTowerGeo = this.createBridgeAnchorTowerGeo();
    this.unitBoxGeo = new THREE.BoxGeometry(1, 1, 1);
    this.lanternBulbGeo = new THREE.SphereGeometry(0.24, 8, 6);
    this.oreCartGeo = this.createMineOreCartGeo();

    this.initChasmMistAndWind();
  }

  private createSoftCloudMistTexture(): THREE.CanvasTexture {
    const canvas = document.createElement('canvas');
    canvas.width = 128;
    canvas.height = 128;
    const ctx = canvas.getContext('2d')!;
    ctx.clearRect(0, 0, 128, 128);
    const grad = ctx.createRadialGradient(64, 64, 8, 64, 64, 62);
    grad.addColorStop(0.0, 'rgba(240, 246, 250, 0.85)');
    grad.addColorStop(0.55, 'rgba(218, 228, 235, 0.42)');
    grad.addColorStop(1.0, 'rgba(200, 214, 224, 0.0)');
    ctx.fillStyle = grad;
    ctx.fillRect(0, 0, 128, 128);
    const tex = new THREE.CanvasTexture(canvas);
    tex.needsUpdate = true;
    return tex;
  }

  /**
   * Sculpted towering flat-topped Sandstone / Canyon Granite Spire Pillar
   * (unit radius = 1, height = 60m: y = 0 .. -60m so even +8m elevated sky-islands plunge below -48m!)
   */
  private createSculptedRockPillarGeo(seed: number): THREE.BufferGeometry {
    const radialSegs = 28;
    const rings = [
      { y: 0.0, r: 0.0, col: 0x9c7b5c }, // Flat crown center!
      { y: 0.0, r: 0.68, col: 0x967556 },
      { y: -0.04, r: 1.0, col: 0x8a6648 }, // Rim brink
      { y: -2.8, r: 1.04, col: 0x7a5238 }, // Overhanging sandstone caprock ledge!
      { y: -11.5, r: 0.88, col: 0x694530 },
      { y: -26.0, r: 0.78, col: 0x4f3528 },
      { y: -42.0, r: 0.92, col: 0x3b2a24 },
      { y: -60.0, r: 1.18, col: 0x28201e }, // Deep abyssal base
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
        const theta = (c / radialSegs) * Math.PI * 2;
        const nx = Math.cos(theta);
        const nz = Math.sin(theta);
        const crag =
          r <= 2
            ? 1.0 + (r === 2 ? fbm2D(nx * 2.5 + seed, nz * 2.5, 2) * 0.035 : 0)
            : 0.86 + 0.28 * (fbm2D(nx * 2.4 + seed, sp.y * 0.09 - seed, 3) * 0.5 + 0.5);
        const x = nx * sp.r * crag;
        const z = nz * sp.r * crag;

        pos[pPtr] = x;
        pos[pPtr + 1] = sp.y;
        pos[pPtr + 2] = z;

        uv[uvPtr++] = r <= 2 ? x * 1.8 : theta * 2.2;
        uv[uvPtr++] = r <= 2 ? z * 1.8 : sp.y * 0.18;

        const strata = r >= 3 ? 0.9 + 0.14 * Math.sin(sp.y * 0.85 + nx * 1.5) : 1.0;
        col[pPtr] = tmp.r * strata;
        col[pPtr + 1] = tmp.g * strata;
        col[pPtr + 2] = tmp.b * strata;
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

  private createBridgeAnchorTowerGeo(): THREE.BufferGeometry {
    const parts: THREE.BufferGeometry[] = [];
    for (const sx of [-4.15, 4.15]) {
      const legF = new THREE.BoxGeometry(0.42, 5.4, 0.42);
      legF.rotateX(0.12);
      legF.translate(sx, 2.55, -0.32);
      parts.push(legF);

      const legB = new THREE.BoxGeometry(0.42, 5.4, 0.42);
      legB.rotateX(-0.12);
      legB.translate(sx, 2.55, 0.32);
      parts.push(legB);
    }
    const capBeam = new THREE.BoxGeometry(9.4, 0.48, 0.56);
    capBeam.translate(0, 5.15, 0);
    parts.push(capBeam);

    return this.mergeGeometries(parts);
  }

  private createMineOreCartGeo(): THREE.BufferGeometry {
    const tub = new THREE.BoxGeometry(1.35, 0.95, 1.95);
    tub.translate(0, 0.78, 0);
    const chassis = new THREE.BoxGeometry(1.1, 0.22, 1.75);
    chassis.translate(0, 0.28, 0);
    return this.mergeGeometries([tub, chassis]);
  }

  private mergeGeometries(geos: THREE.BufferGeometry[]): THREE.BufferGeometry {
    let total = 0;
    for (const raw of geos) {
      const g = raw.index ? raw.toNonIndexed() : raw;
      total += g.attributes.position.count;
    }
    const pos = new Float32Array(total * 3);
    const norm = new Float32Array(total * 3);
    const uv = new Float32Array(total * 2);
    let pOff = 0;
    let uvOff = 0;
    for (const raw of geos) {
      const g = raw.index ? raw.toNonIndexed() : raw;
      g.computeVertexNormals();
      pos.set(g.attributes.position.array as Float32Array, pOff);
      norm.set(g.attributes.normal.array as Float32Array, pOff);
      if (g.attributes.uv) {
        uv.set(g.attributes.uv.array as Float32Array, uvOff);
      }
      pOff += g.attributes.position.count * 3;
      uvOff += g.attributes.position.count * 2;
    }
    const out = new THREE.BufferGeometry();
    out.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    out.setAttribute('normal', new THREE.BufferAttribute(norm, 3));
    out.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
    return out;
  }

  private initChasmMistAndWind(): void {
    // Wide 190m canyon mist cloud sheets drifting below the sky-island archipelago!
    const mistPlaneGeo = new THREE.PlaneGeometry(195, 165);
    mistPlaneGeo.rotateX(-Math.PI * 0.5);
    const mistHeights = [-12.0, -21.5, -33.0];
    for (let i = 0; i < mistHeights.length; i++) {
      const m = new THREE.Mesh(mistPlaneGeo, this.chasmMistSheetMat);
      m.position.set(0, mistHeights[i], 0);
      m.renderOrder = -1;
      this.atmosphereFxGroup.add(m);
      this.mistPlanes.push(m);
    }

    this.windPositions = new Float32Array(this.windMoteCount * 3);
    this.windVelocities = new Float32Array(this.windMoteCount * 3);
    const rng = mulberry32(771239);
    for (let i = 0; i < this.windMoteCount; i++) {
      const ix = i * 3;
      this.windPositions[ix] = (rng() - 0.5) * 165;
      this.windPositions[ix + 1] = lerp(-24.0, 18.0, rng());
      this.windPositions[ix + 2] = (rng() - 0.5) * 155;

      this.windVelocities[ix] = lerp(5.5, 12.5, rng());
      this.windVelocities[ix + 1] = (rng() - 0.46) * 1.1;
      this.windVelocities[ix + 2] = (rng() - 0.5) * 2.4;
    }
    const wGeo = new THREE.BufferGeometry();
    wGeo.setAttribute('position', new THREE.BufferAttribute(this.windPositions, 3));
    const wMat = new THREE.PointsMaterial({
      color: 0xefe7dc,
      size: 0.28,
      transparent: true,
      opacity: 0.48,
      depthWrite: false,
    });
    this.windPoints = new THREE.Points(wGeo, wMat);
    this.windPoints.frustumCulled = false;
    this.atmosphereFxGroup.add(this.windPoints);
  }

  // ===========================================================================
  // DETERMINISTIC GLOBAL MULTI-ISLAND GRAPH (`1 -> 5 -> 2 -> 3 -> 4 -> 2 -> 5 -> 3`)
  // ===========================================================================

  /**
   * Pure deterministic generator for Sky-Island Row `rowIdx` so adjacent streamed chunks
   * agree to 0.000mm on island positions, radii, AND varied elevations!
   */
  public getRowIslands(
    rowIdx: number,
    sampleCourseX: (z: number) => number
  ): RockPillarLanding[] {
    const safeRow = Math.max(0, rowIdx);
    const zRow = -(FIRST_ROW_Z_OFFSET + safeRow * ROW_SPACING_Z);
    const centerX = sampleCourseX(zRow);
    const count = ROW_ISLAND_COUNTS[safeRow % ROW_ISLAND_COUNTS.length];
    const rng = mulberry32(safeRow * 91813 + 44117);

    const out: RockPillarLanding[] = [];

    if (count === 1) {
      // Grand Fan-Out Hub Mesa (wide enough to radiate 5 bridges/ramps cleanly across its front rim!)
      const deckY = safeRow === 0 ? 0.0 : lerp(-1.2, 2.6, rng());
      out.push({
        x: centerX,
        z: zRow,
        radiusX: 27.5,
        radiusZ: 13.8,
        deckY,
        rowIdx: safeRow,
        islandIdx: 0,
      });
      return out;
    }

    // Distinct multi-height templates per island count so neighboring islands visibly step up/down in 3D!
    const heightPresets: Record<number, number[]> = {
      2: [2.4, -2.2],
      3: [-2.8, 4.6, 1.0],
      4: [-3.0, 3.8, -1.2, 4.8],
      5: [-3.4, 3.0, -0.8, 5.2, 1.5],
    };
    const baseHeights = heightPresets[count] ?? [0];
    const flipSign = Math.floor(safeRow / ROW_ISLAND_COUNTS.length) % 2 === 1 ? -1 : 1;

    // Generous lateral spread across the 148m wide Grand Canyon abyss so all 5 islands have roomy 24.4m x 30m flat decks!
    const totalSpanX =
      count === 5 ? 104.0 : count === 4 ? 86.0 : count === 3 ? 70.0 : 46.0;
    const rx =
      count === 5 ? 12.2 : count === 4 ? 13.6 : count === 3 ? 16.0 : 21.0;
    const rz = count === 5 ? 15.0 : 15.2;

    for (let i = 0; i < count; i++) {
      const frac = i / (count - 1) - 0.5; // -0.5 .. +0.5
      const xOff = frac * totalSpanX + (rng() - 0.5) * 1.4;
      const zJitter = (rng() - 0.5) * 2.2;
      const hIdx = flipSign > 0 ? i : count - 1 - i;
      const deckY =
        baseHeights[hIdx] + (safeRow >= 4 ? (rng() - 0.5) * 1.2 : 0.0);

      out.push({
        x: centerX + xOff,
        z: zRow + zJitter,
        radiusX: rx,
        radiusZ: rz,
        deckY,
        rowIdx: safeRow,
        islandIdx: i,
      });
    }

    return out;
  }

  /**
   * Builds the Directed Multi-Bridge & Jump-Ramp Graph between `rowA` (`M` islands) and `rowB` (`N` islands):
   * - Guarantees every island in `rowA` has >= 1 outgoing link AND every island in `rowB` has >= 1 incoming link.
   * - Adds fan-out / convergence / X-crisscross links (`1 -> 5` = 5 links, `5 -> 2` = 7 links, `2 -> 3` = 5 links, etc.).
   * - Converts ~25-30% of favorable downhill/level spans into exhilarating Canyon Stunt Jump Ramps!
   * - Automatically elevates crossing spans into High-Arch Timber Trestles so upper/lower bridges never collide!
   */
  public getRowLinks(
    rowIdx: number,
    islandsA: RockPillarLanding[],
    islandsB: RockPillarLanding[]
  ): CanyonGraphLink[] {
    const M = islandsA.length;
    const N = islandsB.length;
    const rng = mulberry32(rowIdx * 54721 + 8819);

    const pairSet = new Set<string>();
    const rawPairs: Array<{ fromIdx: number; toIdx: number }> = [];
    const addPair = (u: number, v: number, isBonus = false) => {
      if (u < 0 || u >= M || v < 0 || v >= N) return;
      if (isBonus && Math.abs(islandsB[v].x - islandsA[u].x) > 68.0) return;
      const key = `${u}_${v}`;
      if (!pairSet.has(key)) {
        pairSet.add(key);
        rawPairs.push({ fromIdx: u, toIdx: v });
      }
    };

    // 1. Full surjection + injection coverage across max(M, N) (guarantees 0 disconnected islands & full 1->5 fan-out!)
    const maxMN = Math.max(M, N);
    for (let k = 0; k < maxMN; k++) {
      const u = M === 1 ? 0 : Math.round((k * (M - 1)) / (maxMN - 1));
      const v = N === 1 ? 0 : Math.round((k * (N - 1)) / (maxMN - 1));
      addPair(u, v, false);
    }

    // 2. Rich Multi-Bridge Branching & Crisscrossing Choices!
    if (M === 5 && N === 2) {
      addPair(2, 0, true);
      addPair(2, 1, true);
      addPair(3, 0, true);
    } else if (M === 2 && N === 3) {
      addPair(0, 0, true);
      addPair(0, 1, true);
      addPair(1, 1, true);
      addPair(1, 2, true);
      addPair(0, 2, true);
    } else if (M === 3 && N === 4) {
      addPair(0, 1, true);
      addPair(1, 1, true);
      addPair(1, 2, true);
      addPair(2, 2, true);
    } else if (M === 4 && N === 2) {
      addPair(1, 1, true);
      addPair(2, 0, true);
    } else if (M === 2 && N === 5) {
      addPair(0, 0, true);
      addPair(0, 1, true);
      addPair(0, 2, true);
      addPair(1, 2, true);
      addPair(1, 3, true);
      addPair(1, 4, true);
    } else if (M === 5 && N === 3) {
      addPair(1, 1, true);
      addPair(3, 1, true);
      addPair(2, 0, true);
      addPair(2, 2, true);
    } else if (M > 1 && N > 1) {
      addPair(Math.min(M - 1, 1), 0, true);
      addPair(0, Math.min(N - 1, 1), true);
    }

    // 3. Assign initial Span Types (Rope-Bridge vs Sloped Timber Trestle vs Stunt Jump Ramp!)
    const links: CanyonGraphLink[] = rawPairs.map((p, idx) => {
      const a = islandsA[p.fromIdx];
      const b = islandsB[p.toIdx];
      const dy = b.deckY - a.deckY;

      let kind: CanyonSpanKind = 'ROPE_BRIDGE';
      if (M === 1 && N === 5) {
        // Signature 5-way Spawn Fan-Out showcasing ALL 3 span types side-by-side!
        // 0: Stunt Jump Ramp (-3.4m), 1: Sloped Trestle (+3.0m), 2: Center Hero Rope-Bridge (-0.8m),
        // 3: High Trestle (+5.2m), 4: Outer Swaying Rope-Bridge (+1.5m)
        kind =
          p.toIdx === 0
            ? 'JUMP_RAMP'
            : p.toIdx === 1 || p.toIdx === 3
            ? 'TIMBER_TRESTLE'
            : 'ROPE_BRIDGE';
      } else if (dy >= 1.2) {
        kind = 'TIMBER_TRESTLE';
      } else if (dy <= -1.8 && rawPairs.length >= 3 && (idx === 0 || rng() < 0.55)) {
        kind = 'JUMP_RAMP';
      } else if (rng() < 0.28) {
        kind = 'TIMBER_TRESTLE';
      }

      return {
        fromIdx: p.fromIdx,
        toIdx: p.toIdx,
        kind,
        archBumpY: kind === 'TIMBER_TRESTLE' ? 1.4 : 0.0,
        crossClearFrac: -1,
      };
    });

    // Guarantee at least ONE exhilarating Stunt Jump Ramp per multi-bridge layer (as long as >= 3 spans exist
    // so cautious drivers ALWAYS also have full-deck bridges to choose from!)
    if (links.length >= 3 && !links.some((l) => l.kind === 'JUMP_RAMP')) {
      let bestIdx = -1;
      let bestDrop = Infinity;
      for (let i = 0; i < links.length; i++) {
        const dy = islandsB[links[i].toIdx].deckY - islandsA[links[i].fromIdx].deckY;
        if (dy < bestDrop) {
          bestDrop = dy;
          bestIdx = i;
        }
      }
      if (bestIdx >= 0 && bestDrop <= 0.6) {
        links[bestIdx].kind = 'JUMP_RAMP';
        links[bestIdx].archBumpY = 0;
      }
    }

    // 4. Detect 2D X-Crossovers (`(u1 < u2 && v1 > v2)` or vice-versa) and separate them cleanly in 3D!
    //    Upper span -> High-Arch Timber Trestle (`archBumpY >= 4.6m`), Lower span -> Low-Belly Rope-Bridge!
    for (let aIdx = 0; aIdx < links.length; aIdx++) {
      for (let bIdx = aIdx + 1; bIdx < links.length; bIdx++) {
        const L1 = links[aIdx];
        const L2 = links[bIdx];
        const crosses =
          (L1.fromIdx < L2.fromIdx && L1.toIdx > L2.toIdx) ||
          (L1.fromIdx > L2.fromIdx && L1.toIdx < L2.toIdx);
        if (!crosses) continue;

        const avgY1 =
          (islandsA[L1.fromIdx].deckY + islandsB[L1.toIdx].deckY) * 0.5;
        const avgY2 =
          (islandsA[L2.fromIdx].deckY + islandsB[L2.toIdx].deckY) * 0.5;

        const upper = avgY1 >= avgY2 ? L1 : L2;
        const lower = avgY1 >= avgY2 ? L2 : L1;

        upper.kind = 'TIMBER_TRESTLE';
        lower.kind = 'ROPE_BRIDGE';

        // Compute approximate intersection fraction along `upper`
        const x1a = islandsA[upper.fromIdx].x;
        const x1b = islandsB[upper.toIdx].x;
        const x2a = islandsA[lower.fromIdx].x;
        const x2b = islandsB[lower.toIdx].x;
        const denom = x1b - x1a - (x2b - x2a);
        const tCross =
          Math.abs(denom) > 0.1 ? clamp((x2a - x1a) / denom, 0.25, 0.75) : 0.5;

        const lowerEstY =
          lerp(islandsA[lower.fromIdx].deckY, islandsB[lower.toIdx].deckY, tCross) - 2.1;
        const upperBaseY = lerp(
          islandsA[upper.fromIdx].deckY,
          islandsB[upper.toIdx].deckY,
          tCross
        );
        const neededArch = clamp(
          (lowerEstY + 5.8 - upperBaseY) / Math.max(0.55, Math.sin(Math.PI * tCross)),
          2.2,
          4.2
        );
        upper.archBumpY = Math.max(upper.archBumpY, neededArch);
        upper.crossClearFrac = tCross;
      }
    }

    return links;
  }

  /**
   * Finds the closest solid flat-topped Sky-Island to `(x, z)` so `[R]` reset, chasm rescue,
   * and `[J]` camera jumps always land squarely on a real island at its exact elevation!
   */
  public findNearestIslandSpawn(
    x: number,
    z: number,
    sampleCourseX: (z: number) => number
  ): { x: number; y: number; z: number; yaw: number } {
    const approxRow = Math.max(0, Math.round((-z - FIRST_ROW_Z_OFFSET) / ROW_SPACING_Z));
    let bestIsland: RockPillarLanding | null = null;
    let bestDistSq = Infinity;

    for (let r = Math.max(0, approxRow - 1); r <= approxRow + 1; r++) {
      const rowIslands = this.getRowIslands(r, sampleCourseX);
      for (const isl of rowIslands) {
        const dx = x - isl.x;
        const dz = z - isl.z;
        const dSq = dx * dx + dz * dz;
        if (dSq < bestDistSq) {
          bestDistSq = dSq;
          bestIsland = isl;
        }
      }
    }

    if (!bestIsland) {
      const row0 = this.getRowIslands(0, sampleCourseX)[0];
      return { x: row0.x, y: row0.deckY, z: row0.z, yaw: 0 };
    }

    // Aim toward the middle outgoing bridge/ramp of this island!
    const nextRow = this.getRowIslands(bestIsland.rowIdx + 1, sampleCourseX);
    const curRow = this.getRowIslands(bestIsland.rowIdx, sampleCourseX);
    const links = this.getRowLinks(bestIsland.rowIdx, curRow, nextRow).filter(
      (l) => l.fromIdx === bestIsland!.islandIdx
    );
    let yaw = 0;
    if (links.length > 0) {
      const midLink = links[Math.floor(links.length * 0.5)];
      const tgt = nextRow[midLink.toIdx];
      yaw = Math.atan2(-(tgt.x - bestIsland.x), -(tgt.z - bestIsland.z));
    }

    return {
      x: bestIsland.x,
      y: bestIsland.deckY,
      z: bestIsland.z,
      yaw,
    };
  }

  /**
   * Returns exact world-space `startPt`, `endPt`, and `kind` for a specific link `(fromIdx -> toIdx)`
   * in row `rowIdx`!
   */
  public getLinkEndpoints(
    rowIdx: number,
    fromIdx: number,
    toIdx: number,
    sampleCourseX: (z: number) => number
  ): {
    startPt: THREE.Vector3;
    endPt: THREE.Vector3;
    kind: CanyonSpanKind;
    archBumpY: number;
  } | null {
    const srcIslands = this.getRowIslands(rowIdx, sampleCourseX);
    const dstIslands = this.getRowIslands(rowIdx + 1, sampleCourseX);
    const links = this.getRowLinks(rowIdx, srcIslands, dstIslands);
    const link = links.find((l) => l.fromIdx === fromIdx && l.toIdx === toIdx);
    if (!link) return null;

    const outList = links
      .filter((l) => l.fromIdx === fromIdx)
      .sort((a, b) => dstIslands[a.toIdx].x - dstIslands[b.toIdx].x);
    const inList = links
      .filter((l) => l.toIdx === toIdx)
      .sort((a, b) => srcIslands[a.fromIdx].x - srcIslands[b.fromIdx].x);

    const halfWidth =
      link.kind === 'JUMP_RAMP' ? 3.85 : outList.length >= 4 ? 3.45 : 3.75;
    const inset = link.kind === 'ROPE_BRIDGE' ? 0.86 : 0.74;

    const s = this.computeIslandRimSocket(
      srcIslands[fromIdx],
      outList.indexOf(link),
      outList.length,
      dstIslands[toIdx].x,
      true,
      halfWidth,
      inset
    );
    const e = this.computeIslandRimSocket(
      dstIslands[toIdx],
      inList.indexOf(link),
      inList.length,
      srcIslands[fromIdx].x,
      false,
      halfWidth,
      inset
    );

    return {
      startPt: new THREE.Vector3(s.x, s.y, s.z),
      endPt: new THREE.Vector3(e.x, e.y, e.z),
      kind: link.kind,
      archBumpY: link.archBumpY,
    };
  }

  /**
   * Computes flush rim socket `(sx, sy, sz)` on an elliptical Sky-Island so multiple bridges
   * radiating from (or converging onto) the same island spread out cleanly with zero overlap
   * and zero step-wall across the flat `rSq <= 0.94` rock rim!
   */
  private computeIslandRimSocket(
    island: RockPillarLanding,
    slotIdx: number,
    slotCount: number,
    otherX: number,
    isOutgoing: boolean,
    deckHalfWidth: number,
    insetFactor = 0.85
  ): { x: number; y: number; z: number } {
    let dx = 0;
    if (slotCount <= 1) {
      dx = clamp((otherX - island.x) * 0.18, -island.radiusX * 0.35, island.radiusX * 0.35);
    } else {
      const spread = slotCount <= 2 ? 0.44 : slotCount === 3 ? 0.54 : 0.64;
      const frac = (slotIdx / (slotCount - 1)) * 2.0 - 1.0; // -1 .. +1
      dx = frac * island.radiusX * spread;
    }

    const outerNx = Math.min(0.86, (Math.abs(dx) + deckHalfWidth) / island.radiusX);
    const dzMag =
      island.radiusZ *
      Math.sqrt(Math.max(0.12, insetFactor - outerNx * outerNx));

    return {
      x: island.x + dx,
      y: island.deckY + 0.03,
      z: island.z + (isOutgoing ? -dzMag : dzMag),
    };
  }

  /**
   * Populates a 112m chunk (`2` full Sky-Island Rows + `2` full Multi-Bridge/Trestle/Ramp Layers)!
   */
  public populateCanyonBridgeChunk(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    zLength: number,
    sampleCourseX: (z: number) => number,
    obstacles: WorldObstacle[],
    rootBridges: CrisscrossRootBridge[],
    nextObstacleId: () => number,
    nextBridgeId: () => number
  ): CanyonBridgeChunkData {
    const group = new THREE.Group();
    group.name = `CanyonBridgeChunk_${chunkIndex}`;
    chunkGroup.add(group);
    const rng = mulberry32(chunkIndex * 65123 + 19283);

    const pillars: RockPillarLanding[] = [];
    const ropeBridges: SwayingRopeBridge[] = [];
    const timberMats: THREE.Matrix4[] = [];
    const railMats: THREE.Matrix4[] = [];
    const chevronMats: THREE.Matrix4[] = [];
    const towerMats: THREE.Matrix4[] = [];
    const lanternPositions: THREE.Vector3[] = [];
    const rampBeaconPositions: THREE.Vector3[] = [];

    let rampCount = 0;
    let trestleCount = 0;

    const rowBase = chunkIndex * 2;
    const row0Islands = this.getRowIslands(rowBase, sampleCourseX);
    const row1Islands = this.getRowIslands(rowBase + 1, sampleCourseX);
    const row2Islands = this.getRowIslands(rowBase + 2, sampleCourseX);

    // Spawn 3D Rock Island Meshes for the 2 rows owned by this chunk (`rowBase` and `rowBase + 1`)
    for (const rowList of [row0Islands, row1Islands]) {
      for (const isl of rowList) {
        const geo =
          this.pillarBodyGeos[(isl.rowIdx * 3 + isl.islandIdx) % this.pillarBodyGeos.length];
        const mesh = new THREE.Mesh(geo, this.pillarRockMat);
        mesh.position.set(isl.x, isl.deckY, isl.z);
        mesh.scale.set(isl.radiusX, 1.0, isl.radiusZ);
        mesh.castShadow = true;
        mesh.receiveShadow = true;
        group.add(mesh);

        pillars.push(isl);

        // Occasional scenic rusted Mine Ore-Cart parked safely near the side edge of wide islands!
        if (isl.radiusX >= 18.0 && (isl.rowIdx + isl.islandIdx) % 2 === 0) {
          const side = isl.islandIdx % 2 === 0 ? -1 : 1;
          const cx = isl.x + side * (isl.radiusX * 0.76);
          const cz = isl.z + 1.5;
          const cartMesh = new THREE.Mesh(this.oreCartGeo, this.rustedIronRailMat);
          cartMesh.position.set(cx, isl.deckY + 0.02, cz);
          cartMesh.rotation.set(0, 0.45 * side, 0.06);
          cartMesh.castShadow = true;
          group.add(cartMesh);

          obstacles.push({
            id: nextObstacleId(),
            chunkIndex,
            kind: 'boulder',
            position: new THREE.Vector3(cx, isl.deckY, cz),
            radius: 1.05,
            height: 1.35,
            canShearVine: true,
          });
        }
      }
    }

    // Also include `row2Islands` in physics `pillars` array (visual mesh is spawned by `chunkIndex + 1`)
    // so `sampleLiveCanyonSurface` has 0.00mm seamless island coverage right across the chunk boundary!
    for (const isl2 of row2Islands) {
      pillars.push(isl2);
    }

    // Decorative Background Canyon Needle Spires out in the far abyssal mist (`|x| > 60m`)
    for (let s = 0; s < 4; s++) {
      const us = lerp(14.0, 98.0, (s + 0.25 + rng() * 0.5) / 4);
      const zs = zStart - us;
      const side = s % 2 === 0 ? -1 : 1;
      const xs = sampleCourseX(zs) + side * lerp(60.0, 78.0, rng());
      const spire = new THREE.Mesh(
        this.pillarBodyGeos[(chunkIndex + s) % 3],
        this.pillarRockMat
      );
      spire.position.set(xs, lerp(-10.0, -1.5, rng()), zs);
      const sr = lerp(4.8, 8.5, rng());
      spire.scale.set(sr, lerp(0.85, 1.15, rng()), sr);
      group.add(spire);
    }

    // Helper to build all Multi-Bridges, Sloped Trestles, & Stunt Jump Ramps between two Island Rows!
    const buildLayerSpans = (
      rIdx: number,
      srcIslands: RockPillarLanding[],
      dstIslands: RockPillarLanding[]
    ) => {
      const links = this.getRowLinks(rIdx, srcIslands, dstIslands);

      // Group outgoing per source island (sorted left-to-right by destination X)
      const outByIsland = new Map<number, CanyonGraphLink[]>();
      const inByIsland = new Map<number, CanyonGraphLink[]>();
      for (const l of links) {
        if (!outByIsland.has(l.fromIdx)) outByIsland.set(l.fromIdx, []);
        outByIsland.get(l.fromIdx)!.push(l);
        if (!inByIsland.has(l.toIdx)) inByIsland.set(l.toIdx, []);
        inByIsland.get(l.toIdx)!.push(l);
      }
      for (const list of outByIsland.values()) {
        list.sort((a, b) => dstIslands[a.toIdx].x - dstIslands[b.toIdx].x);
      }
      for (const list of inByIsland.values()) {
        list.sort((a, b) => srcIslands[a.fromIdx].x - srcIslands[b.fromIdx].x);
      }

      for (const link of links) {
        const src = srcIslands[link.fromIdx];
        const dst = dstIslands[link.toIdx];
        const outList = outByIsland.get(link.fromIdx)!;
        const inList = inByIsland.get(link.toIdx)!;
        const outSlot = outList.indexOf(link);
        const inSlot = inList.indexOf(link);

        const halfWidth =
          link.kind === 'JUMP_RAMP' ? 3.85 : outList.length >= 4 ? 3.45 : 3.75;
        const inset = link.kind === 'ROPE_BRIDGE' ? 0.86 : 0.74;

        const startPt = this.computeIslandRimSocket(
          src,
          outSlot,
          outList.length,
          dst.x,
          true,
          halfWidth,
          inset
        );
        const endPt = this.computeIslandRimSocket(
          dst,
          inSlot,
          inList.length,
          src.x,
          false,
          halfWidth,
          inset
        );

        if (link.kind === 'ROPE_BRIDGE') {
          const catenaryDrop = clamp(
            Math.hypot(endPt.x - startPt.x, endPt.z - startPt.z) * 0.045,
            1.25,
            2.25
          );
          const rb = this.buildSwayingRopeBridge(
            group,
            startPt.x,
            startPt.y,
            startPt.z,
            endPt.x,
            endPt.y,
            endPt.z,
            halfWidth,
            catenaryDrop,
            rng,
            towerMats,
            lanternPositions
          );
          ropeBridges.push(rb);
        } else if (link.kind === 'TIMBER_TRESTLE') {
          trestleCount++;
          this.buildSlopedTimberMineTrestle(
            startPt.x,
            startPt.y,
            startPt.z,
            endPt.x,
            endPt.y,
            endPt.z,
            link.archBumpY,
            link.crossClearFrac,
            halfWidth,
            rootBridges,
            nextBridgeId,
            chunkIndex,
            timberMats,
            railMats,
            lanternPositions
          );
        } else {
          rampCount++;
          this.buildCanyonJumpRamp(
            startPt.x,
            startPt.y,
            startPt.z,
            endPt.x,
            endPt.y,
            endPt.z,
            halfWidth,
            rootBridges,
            nextBridgeId,
            chunkIndex,
            timberMats,
            railMats,
            chevronMats,
            rampBeaconPositions
          );
        }
      }
    };

    buildLayerSpans(rowBase, row0Islands, row1Islands);
    buildLayerSpans(rowBase + 1, row1Islands, row2Islands);

    // Emit Batched Instanced Meshes for all Trestles, Ramps, Iron Rails, Chevrons, Towers & Beacons!
    const emitInstanced = (
      geo: THREE.BufferGeometry,
      mat: THREE.Material,
      mats: THREE.Matrix4[],
      castShadow = true
    ) => {
      if (mats.length === 0) return;
      const im = new THREE.InstancedMesh(geo, mat, mats.length);
      for (let i = 0; i < mats.length; i++) {
        im.setMatrixAt(i, mats[i]);
      }
      im.instanceMatrix.needsUpdate = true;
      im.castShadow = castShadow;
      im.receiveShadow = true;
      group.add(im);
    };

    emitInstanced(this.unitBoxGeo, this.trestleTimberMat, timberMats, true);
    emitInstanced(this.unitBoxGeo, this.rustedIronRailMat, railMats, false);
    emitInstanced(this.unitBoxGeo, this.rampChevronMat, chevronMats, false);
    emitInstanced(this.anchorTowerGeo, this.trestleTimberMat, towerMats, true);

    if (lanternPositions.length > 0) {
      const lanternIm = new THREE.InstancedMesh(
        this.lanternBulbGeo,
        this.lanternGlowMat,
        lanternPositions.length
      );
      for (let i = 0; i < lanternPositions.length; i++) {
        this._dummy.position.copy(lanternPositions[i]);
        this._dummy.rotation.set(0, 0, 0);
        this._dummy.scale.set(1, 1.25, 1);
        this._dummy.updateMatrix();
        lanternIm.setMatrixAt(i, this._dummy.matrix);
      }
      lanternIm.instanceMatrix.needsUpdate = true;
      group.add(lanternIm);
    }

    if (rampBeaconPositions.length > 0) {
      const beaconIm = new THREE.InstancedMesh(
        this.lanternBulbGeo,
        this.rampBeaconGlowMat,
        rampBeaconPositions.length
      );
      for (let i = 0; i < rampBeaconPositions.length; i++) {
        this._dummy.position.copy(rampBeaconPositions[i]);
        this._dummy.rotation.set(0, 0, 0);
        this._dummy.scale.set(1.45, 1.75, 1.45);
        this._dummy.updateMatrix();
        beaconIm.setMatrixAt(i, this._dummy.matrix);
      }
      beaconIm.instanceMatrix.needsUpdate = true;
      group.add(beaconIm);
    }

    // Warm canyon lantern PointLight over the central island of Row 0
    if (row0Islands.length > 0) {
      const midIsl = row0Islands[Math.floor(row0Islands.length * 0.5)];
      const heroLight = new THREE.PointLight(0xffb44c, 16.0, 42.0, 1.4);
      heroLight.position.set(midIsl.x, midIsl.deckY + 5.0, midIsl.z);
      group.add(heroLight);
    }

    return {
      group,
      pillars,
      ropeBridges,
      fallingHalves: [],
      zStart,
      zLength,
      trestleCreakCooldown: 0,
      rampCount,
      trestleCount,
    };
  }

  // ===========================================================================
  // SPAN TYPE 1: SWAYING, SAGGING & TWISTING 3D WOODEN ROPE-SUSPENSION BRIDGE
  // ===========================================================================

  private buildSwayingRopeBridge(
    group: THREE.Group,
    ax: number,
    ay: number,
    az: number,
    bx: number,
    by: number,
    bz: number,
    halfWidth: number,
    maxCatenaryDrop: number,
    rng: () => number,
    towerMats: THREE.Matrix4[],
    lanternPositions: THREE.Vector3[]
  ): SwayingRopeBridge {
    const dx = bx - ax;
    const dz = bz - az;
    const length = Math.hypot(dx, dz);
    const dirX = dx / length;
    const dirZ = dz / length;
    const yaw = Math.atan2(-dirX, -dirZ);
    const rightX = Math.cos(yaw);
    const rightZ = -Math.sin(yaw);

    const plankSpacing = 1.18;
    const plankCount = Math.max(12, Math.round(length / plankSpacing));
    const actualSpacing = length / (plankCount - 1);

    for (const endPt of [
      { x: ax, y: ay, z: az },
      { x: bx, y: by, z: bz },
    ]) {
      this._dummy.position.set(endPt.x, endPt.y - 0.15, endPt.z);
      this._dummy.rotation.set(0, yaw, 0);
      this._dummy.scale.set(halfWidth / 3.85, 1.0, 1.0);
      this._dummy.updateMatrix();
      towerMats.push(this._dummy.matrix.clone());

      lanternPositions.push(
        new THREE.Vector3(
          endPt.x - rightX * (halfWidth + 0.35),
          endPt.y + 4.75,
          endPt.z - rightZ * (halfWidth + 0.35)
        ),
        new THREE.Vector3(
          endPt.x + rightX * (halfWidth + 0.35),
          endPt.y + 4.75,
          endPt.z + rightZ * (halfWidth + 0.35)
        )
      );
    }

    const restY = new Float32Array(plankCount);
    const sagY = new Float32Array(plankCount);
    const velY = new Float32Array(plankCount);
    const rollZ = new Float32Array(plankCount);
    const velRoll = new Float32Array(plankCount);
    const swayX = new Float32Array(plankCount);
    const isRotten = new Uint8Array(plankCount);
    const crackTimer = new Float32Array(plankCount);
    const snappedCooldown = new Float32Array(plankCount);

    let lastRottenIdx = -10;
    for (let i = 0; i < plankCount; i++) {
      const f = i / (plankCount - 1);
      // Zero-derivative smooth abutment shelf (`sin^1.75`) so bridges meet variable-height rock rims seamlessly!
      restY[i] =
        lerp(ay, by, f) - maxCatenaryDrop * Math.pow(Math.sin(Math.PI * f), 1.75);

      if (i >= 4 && i <= plankCount - 5 && i - lastRottenIdx >= 5 && rng() < 0.32) {
        isRotten[i] = 1;
        lastRottenIdx = i;
      }
    }

    const intactIm = new THREE.InstancedMesh(this.plankGeo, this.weatheredPlankMat, plankCount);
    intactIm.castShadow = true;
    intactIm.receiveShadow = true;
    intactIm.frustumCulled = false;
    group.add(intactIm);

    const rottenIm = new THREE.InstancedMesh(this.plankGeo, this.rottenPlankMat, plankCount);
    rottenIm.castShadow = true;
    rottenIm.receiveShadow = true;
    rottenIm.frustumCulled = false;
    group.add(rottenIm);

    const postIm = new THREE.InstancedMesh(
      this.railPostGeo,
      this.trestleTimberMat,
      plankCount * 2
    );
    postIm.castShadow = true;
    postIm.frustumCulled = false;
    group.add(postIm);

    const vertsPerStrand = (CABLE_SEGS + 1) * (CABLE_SIDES + 1);
    const totalCableVerts = CABLE_STRANDS_PER_BRIDGE * vertsPerStrand;
    const cPos = new Float32Array(totalCableVerts * 3);
    const cNorm = new Float32Array(totalCableVerts * 3);
    const cUv = new Float32Array(totalCableVerts * 2);
    const cIdx: number[] = [];

    for (let st = 0; st < CABLE_STRANDS_PER_BRIDGE; st++) {
      const baseV = st * vertsPerStrand;
      const ringCols = CABLE_SIDES + 1;
      for (let r = 0; r <= CABLE_SEGS; r++) {
        for (let c = 0; c <= CABLE_SIDES; c++) {
          const uvOff = (baseV + r * ringCols + c) * 2;
          cUv[uvOff] = (r / CABLE_SEGS) * 14.0;
          cUv[uvOff + 1] = c / CABLE_SIDES;
        }
      }
      for (let r = 0; r < CABLE_SEGS; r++) {
        for (let c = 0; c < CABLE_SIDES; c++) {
          const a = baseV + r * ringCols + c;
          const b = a + 1;
          const d = a + ringCols;
          const e = d + 1;
          cIdx.push(a, d, b, b, d, e);
        }
      }
    }

    const cableRopeGeo = new THREE.BufferGeometry();
    cableRopeGeo.setAttribute('position', new THREE.BufferAttribute(cPos, 3));
    cableRopeGeo.setAttribute('normal', new THREE.BufferAttribute(cNorm, 3));
    cableRopeGeo.setAttribute('uv', new THREE.BufferAttribute(cUv, 2));
    cableRopeGeo.setIndex(cIdx);

    const cableRopeMesh = new THREE.Mesh(cableRopeGeo, this.braidedRopeMat);
    cableRopeMesh.frustumCulled = false;
    cableRopeMesh.userData.uniqueGeo = true;
    group.add(cableRopeMesh);

    const bridge: SwayingRopeBridge = {
      ax,
      ay,
      az,
      bx,
      by,
      bz,
      dirX,
      dirZ,
      rightX,
      rightZ,
      yaw,
      length,
      halfWidth,
      maxCatenaryDrop,
      plankCount,
      plankSpacing: actualSpacing,
      restY,
      sagY,
      velY,
      rollZ,
      velRoll,
      swayX,
      isRotten,
      crackTimer,
      snappedCooldown,
      intactPlankIm: intactIm,
      rottenPlankIm: rottenIm,
      postIm,
      cableRopeMesh,
      cableRopeGeo,
      windPhase: rng() * Math.PI * 2,
    };

    this.syncRopeBridgeMeshes(bridge);
    return bridge;
  }

  // ===========================================================================
  // SPAN TYPE 2: SLOPED & ARCHED RICKETY TIMBER MINE RAILWAY TRESTLE
  // ===========================================================================

  private buildSlopedTimberMineTrestle(
    ax: number,
    ay: number,
    az: number,
    bx: number,
    by: number,
    bz: number,
    archBumpY: number,
    crossClearFrac: number,
    deckHalfWidth: number,
    rootBridges: CrisscrossRootBridge[],
    nextBridgeId: () => number,
    chunkIndex: number,
    timberMats: THREE.Matrix4[],
    railMats: THREE.Matrix4[],
    lanternPositions: THREE.Vector3[]
  ): void {
    const segCount = 14;
    const dx = bx - ax;
    const dz = bz - az;
    const totalLen = Math.hypot(dx, dz);
    const yaw = Math.atan2(-dx, -dz);
    const rightX = Math.cos(yaw);
    const rightZ = -Math.sin(yaw);

    // Smooth S-curve between `ay` and `by` + smooth arch bump over any lower crossing bridge!
    const heightAt = (t: number): number => {
      const s = smoothstep(0.0, 1.0, t);
      const arch = archBumpY * Math.pow(Math.sin(Math.PI * t), 1.35);
      return lerp(ay, by, s) + arch;
    };

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

      const flare =
        s === 0 || s === segCount - 1
          ? 1.45
          : s === 1 || s === segCount - 2
          ? 0.7
          : 0.0;
      const segHalfW = deckHalfWidth + flare;

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

      for (const latFrac of [-0.72, 0.0, 0.72]) {
        const off = latFrac * deckHalfWidth;
        this._dummy.position.set(xm + rightX * off, ym - 0.36, zm + rightZ * off);
        this._euler.set(pitch, yaw, 0, 'YXZ');
        this._dummy.rotation.copy(this._euler);
        this._dummy.scale.set(0.4, 0.5, segLen + 0.18);
        this._dummy.updateMatrix();
        timberMats.push(this._dummy.matrix.clone());
      }

      const tieSub = 3;
      for (let k = 0; k < tieSub; k++) {
        const tk = lerp(t0, t1, (k + 0.5) / tieSub);
        const tx = lerp(ax, bx, tk);
        const tz = lerp(az, bz, tk);
        const ty = heightAt(tk);
        this._dummy.position.set(tx, ty - 0.08, tz);
        this._euler.set(pitch, yaw, 0, 'YXZ');
        this._dummy.rotation.copy(this._euler);
        this._dummy.scale.set(segHalfW * 2.04, 0.16, 0.68);
        this._dummy.updateMatrix();
        timberMats.push(this._dummy.matrix.clone());
      }

      for (const railOff of [-1.2, 1.2]) {
        this._dummy.position.set(xm + rightX * railOff, ym + 0.05, zm + rightZ * railOff);
        this._euler.set(pitch, yaw, 0, 'YXZ');
        this._dummy.rotation.copy(this._euler);
        this._dummy.scale.set(0.14, 0.12, segLen + 0.08);
        this._dummy.updateMatrix();
        railMats.push(this._dummy.matrix.clone());
      }
      for (const curbSide of [-1, 1]) {
        const cOff = curbSide * (deckHalfWidth - 0.2);
        this._dummy.position.set(xm + rightX * cOff, ym + 0.16, zm + rightZ * cOff);
        this._euler.set(pitch, yaw, 0, 'YXZ');
        this._dummy.rotation.copy(this._euler);
        this._dummy.scale.set(0.28, 0.28, segLen + 0.08);
        this._dummy.updateMatrix();
        timberMats.push(this._dummy.matrix.clone());
      }
    }

    // Tall X-braced timber bents plunging into the gorge (automatically skipping any lower-bridge crossover!)
    const candidateBents = [0.16, 0.32, 0.5, 0.68, 0.84];
    for (const bf of candidateBents) {
      if (crossClearFrac >= 0 && Math.abs(bf - crossClearFrac) < 0.19) continue;

      const bxPos = lerp(ax, bx, bf);
      const bzPos = lerp(az, bz, bf);
      const topY = heightAt(bf) - 0.55;
      const bentH = Math.max(12.0, 42.0 + topY);

      this._dummy.position.set(bxPos, topY, bzPos);
      this._dummy.rotation.set(0, yaw, 0);
      this._dummy.scale.set(deckHalfWidth * 2.15, 0.44, 0.46);
      this._dummy.updateMatrix();
      timberMats.push(this._dummy.matrix.clone());

      for (const pFrac of [-0.86, 0.86]) {
        const latOff = pFrac * deckHalfWidth;
        const batterAngle = -pFrac * 0.08;
        const midOff = latOff + pFrac * 1.35;
        this._dummy.position.set(
          bxPos + rightX * midOff,
          topY - bentH * 0.5,
          bzPos + rightZ * midOff
        );
        this._euler.set(0, yaw, batterAngle, 'YXZ');
        this._dummy.rotation.copy(this._euler);
        this._dummy.scale.set(0.38, bentH, 0.38);
        this._dummy.updateMatrix();
        timberMats.push(this._dummy.matrix.clone());
      }

      for (const tierDrop of [6.0, 15.0]) {
        const ty = topY - tierDrop;
        this._dummy.position.set(bxPos, ty, bzPos);
        this._dummy.rotation.set(0, yaw, 0);
        this._dummy.scale.set(deckHalfWidth * 2.3 + tierDrop * 0.14, 0.26, 0.28);
        this._dummy.updateMatrix();
        timberMats.push(this._dummy.matrix.clone());

        for (const diagSign of [-1, 1]) {
          this._dummy.position.set(bxPos, ty + 3.0, bzPos);
          this._euler.set(0, yaw, diagSign * 0.65, 'YXZ');
          this._dummy.rotation.copy(this._euler);
          this._dummy.scale.set(0.22, 8.6, 0.22);
          this._dummy.updateMatrix();
          timberMats.push(this._dummy.matrix.clone());
        }
      }

      if (bf === 0.32 || bf === 0.68) {
        for (const lSide of [-1, 1]) {
          const lx = bxPos + rightX * lSide * (deckHalfWidth + 0.15);
          const lz = bzPos + rightZ * lSide * (deckHalfWidth + 0.15);
          const ly = heightAt(bf);
          this._dummy.position.set(lx, ly + 1.3, lz);
          this._dummy.rotation.set(0, yaw, 0);
          this._dummy.scale.set(0.16, 2.6, 0.16);
          this._dummy.updateMatrix();
          timberMats.push(this._dummy.matrix.clone());
          lanternPositions.push(new THREE.Vector3(lx, ly + 2.55, lz));
        }
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
  // SPAN TYPE 3: CANTILEVERED CANYON STUNT JUMP RAMP + OPEN CHASM GAP + CATCH APRON
  // ===========================================================================

  /**
   * Builds a thrilling Cantilevered Timber & Iron Stunt Jump Ramp instead of a full bridge!
   * - Launch Ramp (`t = 0.0 .. 0.45`): smooth upward ski-jump curve jutting ~16m out over the -48m abyss,
   *   supported by 45-degree cliff-bolted timber struts and lit by glowing cyan/amber lip beacons.
   * - Open Chasm Air Gap (`t = 0.45 .. 0.72`): ~10-12m of pure -48m canyon air!
   * - Wide Sloped Catch-Landing Apron (`t = 0.72 .. 1.0`): 9.4m-wide forgiving landing deck onto the target island!
   */
  private buildCanyonJumpRamp(
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
    timberMats: THREE.Matrix4[],
    railMats: THREE.Matrix4[],
    chevronMats: THREE.Matrix4[],
    rampBeaconPositions: THREE.Vector3[]
  ): void {
    const dx = bx - ax;
    const dz = bz - az;
    const totalLen = Math.hypot(dx, dz);
    const yaw = Math.atan2(-dx, -dz);
    const rightX = Math.cos(yaw);
    const rightZ = -Math.sin(yaw);

    const launchEndFrac = 0.46;
    const catchStartFrac = 0.71;
    // Calibrated launch rise so any 4x4 at >= 10.5 m/s (~38 km/h) clears the chasm gap cleanly!
    const rampRise = clamp(2.15 + Math.max(0, by - ay) * 0.35, 1.85, 3.1);

    const launchSegs = 6;
    const launchSegments: CrisscrossRootSegment[] = [];
    let minZ = Infinity;
    let maxZ = -Infinity;

    const launchYAt = (u: number): number => ay + rampRise * Math.pow(u, 1.38);

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

      launchSegments.push({
        ax: x0,
        ay: y0 - 0.18,
        az: z0,
        bx: x1,
        by: y1 - 0.18,
        bz: z1,
        deckHalfWidth: halfWidth + 0.25,
        thickness: 1.35,
      });

      const xm = (x0 + x1) * 0.5;
      const ym = (y0 + y1) * 0.5;
      const zm = (z0 + z1) * 0.5;
      const segLen = (totalLen * launchEndFrac) / launchSegs;
      const pitch = Math.atan2(y1 - y0, segLen);

      // Heavy cantilevered timber ramp stringers
      for (const latFrac of [-0.78, 0.0, 0.78]) {
        const off = latFrac * halfWidth;
        this._dummy.position.set(xm + rightX * off, ym - 0.34, zm + rightZ * off);
        this._euler.set(pitch, yaw, 0, 'YXZ');
        this._dummy.rotation.copy(this._euler);
        this._dummy.scale.set(0.44, 0.52, segLen + 0.16);
        this._dummy.updateMatrix();
        timberMats.push(this._dummy.matrix.clone());
      }

      // Solid closely-packed ramp deck planks (bright amber chevron boards on final 2 lip segments!)
      const isLip = s >= launchSegs - 2;
      for (let k = 0; k < 3; k++) {
        const uk = lerp(u0, u1, (k + 0.5) / 3);
        const tk = uk * launchEndFrac;
        this._dummy.position.set(lerp(ax, bx, tk), launchYAt(uk) - 0.07, lerp(az, bz, tk));
        this._euler.set(pitch, yaw, 0, 'YXZ');
        this._dummy.rotation.copy(this._euler);
        this._dummy.scale.set(halfWidth * 2.1, 0.16, segLen * 0.31);
        this._dummy.updateMatrix();
        if (isLip) {
          chevronMats.push(this._dummy.matrix.clone());
        } else {
          timberMats.push(this._dummy.matrix.clone());
        }
      }

      // Twin wide iron wheel-runner launch plates + side guide rails!
      for (const runnerOff of [-1.25, 1.25]) {
        this._dummy.position.set(xm + rightX * runnerOff, ym + 0.03, zm + rightZ * runnerOff);
        this._euler.set(pitch, yaw, 0, 'YXZ');
        this._dummy.rotation.copy(this._euler);
        this._dummy.scale.set(0.72, 0.06, segLen + 0.06);
        this._dummy.updateMatrix();
        railMats.push(this._dummy.matrix.clone());
      }
      for (const side of [-1, 1]) {
        const cOff = side * (halfWidth + 0.05);
        this._dummy.position.set(xm + rightX * cOff, ym + 0.22, zm + rightZ * cOff);
        this._euler.set(pitch, yaw, 0, 'YXZ');
        this._dummy.rotation.copy(this._euler);
        this._dummy.scale.set(0.28, 0.36, segLen + 0.06);
        this._dummy.updateMatrix();
        timberMats.push(this._dummy.matrix.clone());
      }
    }

    // Diagonal 45-degree Cantilever Cliff-Brace Struts kicking back from the ramp lip into Island A's rock face!
    {
      const lipX = lerp(ax, bx, launchEndFrac);
      const lipZ = lerp(az, bz, launchEndFrac);
      const lipY = launchYAt(1.0);
      const braceX = lerp(ax, lipX, 0.52);
      const braceZ = lerp(az, lipZ, 0.52);
      const braceLen = Math.hypot(lipX - ax, lipZ - az) * 0.95;

      for (const side of [-0.78, 0.0, 0.78]) {
        const off = side * halfWidth;
        this._dummy.position.set(braceX + rightX * off, lipY - 4.6, braceZ + rightZ * off);
        this._euler.set(0.68, yaw, 0, 'YXZ');
        this._dummy.rotation.copy(this._euler);
        this._dummy.scale.set(0.42, 0.42, braceLen);
        this._dummy.updateMatrix();
        timberMats.push(this._dummy.matrix.clone());
      }

      // Tall Twin Glowing Cyan Launch-Lip Beacons so players spot Stunt Jump Ramps from 120m away!
      for (const side of [-1, 1]) {
        const bxLip = lipX + rightX * side * (halfWidth + 0.22);
        const bzLip = lipZ + rightZ * side * (halfWidth + 0.22);
        this._dummy.position.set(bxLip, lipY + 1.15, bzLip);
        this._dummy.rotation.set(0, yaw, 0);
        this._dummy.scale.set(0.18, 2.3, 0.18);
        this._dummy.updateMatrix();
        chevronMats.push(this._dummy.matrix.clone());
        rampBeaconPositions.push(new THREE.Vector3(bxLip, lipY + 2.45, bzLip));
      }
    }

    // Wide Forgiving Sloped Wooden Catch-Landing Apron (`t = catchStartFrac .. 1.0`) on Target Island B!
    const catchSegs = 4;
    const catchHalfWidth = halfWidth + 0.85; // Extra wide 9.4m landing zone!
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

      launchSegments.push({
        ax: x0,
        ay: y0 - 0.18,
        az: z0,
        bx: x1,
        by: y1 - 0.18,
        bz: z1,
        deckHalfWidth: catchHalfWidth,
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
      this._dummy.scale.set(catchHalfWidth * 2.0, 0.24, segLen + 0.1);
      this._dummy.updateMatrix();
      if (s === 0) {
        chevronMats.push(this._dummy.matrix.clone());
      } else {
        timberMats.push(this._dummy.matrix.clone());
      }
    }

    // Twin Catch-Apron Entry Beacons on Target Island B!
    {
      const cx0 = lerp(ax, bx, catchStartFrac);
      const cz0 = lerp(az, bz, catchStartFrac);
      const cy0 = by - catchDrop;
      for (const side of [-1, 1]) {
        rampBeaconPositions.push(
          new THREE.Vector3(
            cx0 + rightX * side * (catchHalfWidth + 0.15),
            cy0 + 1.65,
            cz0 + rightZ * side * (catchHalfWidth + 0.15)
          )
        );
      }
    }

    rootBridges.push({
      id: nextBridgeId(),
      chunkIndex,
      segments: launchSegments,
      minZ: minZ - 2.0,
      maxZ: maxZ + 2.0,
    });
  }

  // ===========================================================================
  // LIVE 3D MESH DEFORMATION, SURFACE SAMPLING & COUPLED WAVE PHYSICS
  // ===========================================================================

  private syncRopeBridgeMeshes(br: SwayingRopeBridge): void {
    const n = br.plankCount;
    const width = br.halfWidth * 2.0;

    for (let i = 0; i < n; i++) {
      const f = i / (n - 1);
      const sway = br.swayX[i];
      const px = lerp(br.ax, br.bx, f) + br.rightX * sway;
      const pz = lerp(br.az, br.bz, f) + br.rightZ * sway;
      const py = br.restY[i] + br.sagY[i];

      const iPrev = Math.max(0, i - 1);
      const iNext = Math.min(n - 1, i + 1);
      const dy =
        br.restY[iNext] + br.sagY[iNext] - (br.restY[iPrev] + br.sagY[iPrev]);
      const ds = Math.max(0.5, (iNext - iPrev) * br.plankSpacing);
      const pitch = Math.atan2(dy, ds);
      const roll = br.rollZ[i];

      this._euler.set(pitch, br.yaw, roll, 'YXZ');
      this._quat.setFromEuler(this._euler);

      const isMissing = br.snappedCooldown[i] > 0;
      const isRot = br.isRotten[i] === 1;

      this._dummy.position.set(px, py - 0.12, pz);
      this._dummy.quaternion.copy(this._quat);

      if (!isMissing && !isRot) {
        this._dummy.scale.set(width, 1.0, 1.0);
      } else {
        this._dummy.scale.set(0, 0, 0);
      }
      this._dummy.updateMatrix();
      br.intactPlankIm.setMatrixAt(i, this._dummy.matrix);

      if (!isMissing && isRot) {
        const jitter = br.crackTimer[i] > 0 ? Math.sin(br.crackTimer[i] * 90.0) * 0.06 : 0;
        this._dummy.position.set(px, py - 0.13 + jitter, pz);
        this._dummy.scale.set(width * 0.98, 0.92, 0.92);
      } else {
        this._dummy.scale.set(0, 0, 0);
      }
      this._dummy.updateMatrix();
      br.rottenPlankIm.setMatrixAt(i, this._dummy.matrix);

      for (let sideIdx = 0; sideIdx < 2; sideIdx++) {
        const sSign = sideIdx === 0 ? -1 : 1;
        const postSlot = i * 2 + sideIdx;
        if (i % 2 === 0 && !isMissing) {
          const edgeDrop = -sSign * Math.sin(roll) * (br.halfWidth - 0.12);
          const edgeCos = Math.cos(roll) * (br.halfWidth - 0.12);
          this._dummy.position.set(
            px + br.rightX * sSign * edgeCos,
            py + edgeDrop,
            pz + br.rightZ * sSign * edgeCos
          );
          this._dummy.quaternion.copy(this._quat);
          this._dummy.scale.set(1, 1, 1);
        } else {
          this._dummy.scale.set(0, 0, 0);
        }
        this._dummy.updateMatrix();
        br.postIm.setMatrixAt(postSlot, this._dummy.matrix);
      }
    }

    br.intactPlankIm.instanceMatrix.needsUpdate = true;
    br.rottenPlankIm.instanceMatrix.needsUpdate = true;
    br.postIm.instanceMatrix.needsUpdate = true;

    const posAttr = br.cableRopeGeo.attributes.position as THREE.BufferAttribute;
    const normAttr = br.cableRopeGeo.attributes.normal as THREE.BufferAttribute;
    const pArr = posAttr.array as Float32Array;
    const nArr = normAttr.array as Float32Array;
    const vertsPerStrand = (CABLE_SEGS + 1) * (CABLE_SIDES + 1);
    const ringCols = CABLE_SIDES + 1;

    for (let st = 0; st < CABLE_STRANDS_PER_BRIDGE; st++) {
      const isMainCable = st < 2;
      const sideSign = st % 2 === 0 ? -1 : 1;
      const radius = isMainCable ? 0.085 : 0.052;
      const baseV = st * vertsPerStrand;

      for (let r = 0; r <= CABLE_SEGS; r++) {
        const f = r / CABLE_SEGS;
        const pFloat = f * (n - 1);
        const i0 = Math.min(n - 1, Math.floor(pFloat));
        const i1 = Math.min(n - 1, i0 + 1);
        const frac = pFloat - i0;

        const sway = lerp(br.swayX[i0], br.swayX[i1], frac);
        const deckY =
          lerp(br.restY[i0] + br.sagY[i0], br.restY[i1] + br.sagY[i1], frac);
        const roll = lerp(br.rollZ[i0], br.rollZ[i1], frac);

        const latDist = br.halfWidth + (isMainCable ? 0.22 : -0.12);
        const edgeDrop = -sideSign * Math.sin(roll) * latDist;
        const cx = lerp(br.ax, br.bx, f) + br.rightX * (sway + sideSign * latDist);
        const cz = lerp(br.az, br.bz, f) + br.rightZ * (sway + sideSign * latDist);

        const towerSweep = isMainCable
          ? Math.pow(Math.abs(f * 2.0 - 1.0), 2.1) * 3.45 + 1.42
          : 1.28;
        const cy = deckY + edgeDrop + towerSweep;

        for (let c = 0; c <= CABLE_SIDES; c++) {
          const ang = (c / CABLE_SIDES) * Math.PI * 2;
          const cosA = Math.cos(ang);
          const sinA = Math.sin(ang);
          const nx = br.rightX * cosA;
          const ny = sinA;
          const nz = br.rightZ * cosA;

          const v3 = (baseV + r * ringCols + c) * 3;
          pArr[v3] = cx + nx * radius;
          pArr[v3 + 1] = cy + ny * radius;
          pArr[v3 + 2] = cz + nz * radius;

          nArr[v3] = nx;
          nArr[v3 + 1] = ny;
          nArr[v3 + 2] = nz;
        }
      }
    }

    posAttr.needsUpdate = true;
    normAttr.needsUpdate = true;
  }

  public sampleLiveCanyonSurface(
    data: CanyonBridgeChunkData,
    x: number,
    z: number
  ): { active: boolean; height: number; onRopeBridge: boolean } {
    let bestH = -48.0;
    let found = false;
    let onRopeBridge = false;

    // 1. Check Sculpted Flat-Topped Multi-Height Sky-Islands
    let skirtH = -48.0;
    let hasSkirt = false;
    for (let i = 0; i < data.pillars.length; i++) {
      const p = data.pillars[i];
      const nx = (x - p.x) / p.radiusX;
      const nz = (z - p.z) / p.radiusZ;
      const rSq = nx * nx + nz * nz;
      if (rSq <= 0.94) {
        found = true;
        const micro = fbm2D(x * 0.18, z * 0.18, 2) * 0.04;
        bestH = Math.max(bestH, p.deckY + micro);
      } else if (rSq < 1.16) {
        hasSkirt = true;
        const drop = smoothstep(0.94, 1.16, rSq);
        skirtH = Math.max(skirtH, lerp(p.deckY, -48.0, drop));
      }
    }

    // 2. Check Swaying, Sagging, Twisting 3D Rope-Suspension Bridges!
    for (let b = 0; b < data.ropeBridges.length; b++) {
      const br = data.ropeBridges[b];
      const dx = x - br.ax;
      const dz = z - br.az;
      const along = dx * br.dirX + dz * br.dirZ;
      if (along < -0.6 || along > br.length + 0.6) continue;

      const clampedAlong = clamp(along, 0, br.length);
      const pFloat = (clampedAlong / br.length) * (br.plankCount - 1);
      const i0 = Math.min(br.plankCount - 1, Math.floor(pFloat));
      const i1 = Math.min(br.plankCount - 1, i0 + 1);
      const frac = pFloat - i0;

      const sway = lerp(br.swayX[i0], br.swayX[i1], frac);
      const rawLat = dx * br.rightX + dz * br.rightZ - sway;

      if (Math.abs(rawLat) <= br.halfWidth + 0.14) {
        const nearestIdx = Math.round(pFloat);
        const plankDist = Math.abs(pFloat - nearestIdx);
        const snappedDip =
          br.snappedCooldown[nearestIdx] > 0
            ? -0.58 * Math.cos(clamp(plankDist * 2.0, 0, 1) * Math.PI * 0.5)
            : 0.0;

        const deckCenterY =
          lerp(br.restY[i0] + br.sagY[i0], br.restY[i1] + br.sagY[i1], frac) + snappedDip;
        const roll = lerp(br.rollZ[i0], br.rollZ[i1], frac);
        const rollTiltY = -rawLat * Math.sin(roll);
        const outerCurb =
          Math.abs(rawLat) > br.halfWidth - 0.35
            ? smoothstep(br.halfWidth - 0.35, br.halfWidth + 0.1, Math.abs(rawLat)) * 0.28
            : 0.0;
        const plankRumble = Math.abs(Math.sin(pFloat * Math.PI)) * 0.02;
        const h = deckCenterY + rollTiltY + outerCurb + plankRumble;

        if (h > bestH) {
          bestH = h;
          found = true;
          onRopeBridge = true;
        }
      }
    }

    if (!found && hasSkirt) {
      found = true;
      bestH = skirtH;
    }

    return { active: found, height: bestH, onRopeBridge };
  }

  public stepChunkHazards(
    data: CanyonBridgeChunkData,
    dt: number,
    elapsed: number,
    vehiclePos: THREE.Vector3,
    vehicleSpeed: number,
    passiveProtection: boolean
  ): void {
    const clampedDt = Math.min(dt, 0.033);
    if (data.trestleCreakCooldown > 0) data.trestleCreakCooldown -= clampedDt;

    for (let b = 0; b < data.ropeBridges.length; b++) {
      const br = data.ropeBridges[b];
      const n = br.plankCount;

      const dx = vehiclePos.x - br.ax;
      const dz = vehiclePos.z - br.az;
      const along = dx * br.dirX + dz * br.dirZ;
      let carPlankFloat = -1;
      let carLat = 0;

      if (!passiveProtection && along >= 0 && along <= br.length) {
        const idxApprox = clamp(Math.round((along / br.length) * (n - 1)), 0, n - 1);
        const lat = dx * br.rightX + dz * br.rightZ - br.swayX[idxApprox];
        const approxDeckY = br.restY[idxApprox] + br.sagY[idxApprox];
        if (
          Math.abs(lat) <= br.halfWidth + 0.5 &&
          Math.abs(vehiclePos.y - approxDeckY) < 3.2
        ) {
          carPlankFloat = (along / br.length) * (n - 1);
          carLat = lat;

          if (vehicleSpeed > 3.5 && data.trestleCreakCooldown <= 0) {
            data.trestleCreakCooldown = 2.4;
            if (this.onBridgeCreak) {
              this.onBridgeCreak(vehiclePos.clone());
            }
          }
        }
      }

      for (let i = 1; i < n - 1; i++) {
        const f = i / (n - 1);
        const envelope = Math.sin(f * Math.PI);

        const windTarget =
          Math.sin(elapsed * 1.45 + br.windPhase + f * 2.6) * 0.34 * envelope +
          Math.cos(elapsed * 2.9 + br.windPhase * 1.7) * 0.1 * envelope;
        br.swayX[i] = lerp(br.swayX[i], windTarget, clamp(3.5 * clampedDt, 0, 1));

        const neighborAvgY = (br.sagY[i - 1] + br.sagY[i + 1]) * 0.5;
        let accelY = (neighborAvgY - br.sagY[i]) * 185.0 - br.sagY[i] * 22.0 - br.velY[i] * 5.2;

        const neighborAvgRoll = (br.rollZ[i - 1] + br.rollZ[i + 1]) * 0.5;
        let accelRoll =
          (neighborAvgRoll - br.rollZ[i]) * 140.0 - br.rollZ[i] * 34.0 - br.velRoll[i] * 6.0;

        if (carPlankFloat >= 0) {
          const distPlanks = Math.abs(i - carPlankFloat);
          if (distPlanks < 3.2) {
            const wKernel = Math.cos((distPlanks / 3.2) * Math.PI * 0.5);
            accelY -= (26.5 + Math.min(18.0, vehicleSpeed * 0.55)) * wKernel;
            const normLat = clamp(carLat / br.halfWidth, -1, 1);
            accelRoll -= normLat * 14.5 * wKernel;
            br.swayX[i] += normLat * 0.32 * wKernel * clampedDt;

            if (
              distPlanks < 1.05 &&
              br.isRotten[i] === 1 &&
              br.snappedCooldown[i] <= 0 &&
              br.crackTimer[i] <= 0
            ) {
              br.crackTimer[i] = 0.34; // 0.34s crack warning so fast 4x4s clear both axles before snap!
              br.velY[i] -= 2.4;
            }
          }
        }

        br.velY[i] += accelY * clampedDt;
        br.velRoll[i] += accelRoll * clampedDt;

        if (br.crackTimer[i] > 0) {
          br.crackTimer[i] -= clampedDt;
          if (br.crackTimer[i] <= 0) {
            br.crackTimer[i] = 0;
            br.snappedCooldown[i] = 9.5;
            this.spawnSnappedPlankHalves(data, br, i);
          }
        } else if (br.snappedCooldown[i] > 0) {
          br.snappedCooldown[i] = Math.max(0, br.snappedCooldown[i] - clampedDt);
        }
      }

      for (let i = 1; i < n - 1; i++) {
        br.sagY[i] = clamp(br.sagY[i] + br.velY[i] * clampedDt, -0.92, 0.48);
        br.rollZ[i] = clamp(br.rollZ[i] + br.velRoll[i] * clampedDt, -0.13, 0.13);
      }

      this.syncRopeBridgeMeshes(br);
    }

    for (let i = data.fallingHalves.length - 1; i >= 0; i--) {
      const fh = data.fallingHalves[i];
      fh.life -= clampedDt;
      fh.vel.y -= 21.0 * clampedDt;
      fh.mesh.position.addScaledVector(fh.vel, clampedDt);
      fh.mesh.rotation.x += fh.rotVel.x * clampedDt;
      fh.mesh.rotation.y += fh.rotVel.y * clampedDt;
      fh.mesh.rotation.z += fh.rotVel.z * clampedDt;

      if (fh.life <= 0 || fh.mesh.position.y < -48.0) {
        data.group.remove(fh.mesh);
        data.fallingHalves.splice(i, 1);
      }
    }
  }

  private spawnSnappedPlankHalves(
    data: CanyonBridgeChunkData,
    br: SwayingRopeBridge,
    plankIdx: number
  ): void {
    const f = plankIdx / (br.plankCount - 1);
    const px = lerp(br.ax, br.bx, f) + br.rightX * br.swayX[plankIdx];
    const pz = lerp(br.az, br.bz, f) + br.rightZ * br.swayX[plankIdx];
    const py = br.restY[plankIdx] + br.sagY[plankIdx];
    const snapPos = new THREE.Vector3(px, py, pz);

    if (this.onRottenPlankSnap) {
      this.onRottenPlankSnap(snapPos);
    }

    for (const side of [-1, 1]) {
      const halfMesh = new THREE.Mesh(this.halfPlankGeo, this.rottenPlankMat);
      halfMesh.position.set(
        px + br.rightX * side * (br.halfWidth * 0.48),
        py - 0.15,
        pz + br.rightZ * side * (br.halfWidth * 0.48)
      );
      halfMesh.rotation.set(0, br.yaw, side * 0.28);
      data.group.add(halfMesh);

      data.fallingHalves.push({
        mesh: halfMesh,
        vel: new THREE.Vector3(
          br.rightX * side * 2.8 + (Math.random() - 0.5) * 1.5,
          -2.4 - Math.random() * 2.0,
          br.rightZ * side * 2.8 + (Math.random() - 0.5) * 1.5
        ),
        rotVel: new THREE.Vector3(
          (Math.random() - 0.5) * 7.0,
          (Math.random() - 0.5) * 5.0,
          side * (4.5 + Math.random() * 4.0)
        ),
        life: 3.4,
      });
    }
  }

  public stepGlobalCanyonAtmosphere(
    active: boolean,
    focusPos: THREE.Vector3,
    elapsed: number,
    dt: number
  ): void {
    this.atmosphereFxGroup.visible = active;
    if (!active) return;

    for (let i = 0; i < this.mistPlanes.length; i++) {
      const mp = this.mistPlanes[i];
      const driftX = Math.sin(elapsed * 0.22 + i * 2.1) * 9.5;
      const driftZ = Math.cos(elapsed * 0.17 + i * 1.7) * 7.5;
      mp.position.x = focusPos.x + driftX;
      mp.position.z = focusPos.z + driftZ;
    }

    const clampedDt = Math.min(dt, 0.05);
    const posAttr = this.windPoints.geometry.attributes.position as THREE.BufferAttribute;
    for (let i = 0; i < this.windMoteCount; i++) {
      const ix = i * 3;
      let x = this.windPositions[ix] + this.windVelocities[ix] * clampedDt;
      let y = this.windPositions[ix + 1] + this.windVelocities[ix + 1] * clampedDt;
      let z = this.windPositions[ix + 2] + this.windVelocities[ix + 2] * clampedDt;

      if (x - focusPos.x > 82) x -= 164;
      if (x - focusPos.x < -82) x += 164;
      if (y > focusPos.y + 18) y -= 42;
      if (y < focusPos.y - 24) y += 42;
      if (z - focusPos.z > 75) z -= 150;
      if (z - focusPos.z < -75) z += 150;

      this.windPositions[ix] = x;
      this.windPositions[ix + 1] = y;
      this.windPositions[ix + 2] = z;
    }
    posAttr.needsUpdate = true;
  }
}

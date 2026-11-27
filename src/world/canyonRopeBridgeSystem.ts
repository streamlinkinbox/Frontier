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

export interface CanyonBridgeChunkData {
  group: THREE.Group;
  pillars: RockPillarLanding[];
  ropeBridges: SwayingRopeBridge[];
  fallingHalves: FallingPlankHalf[];
  zStart: number;
  zLength: number;
  trestleCreakCooldown: number;
}

const CABLE_STRANDS_PER_BRIDGE = 4; // Left main catenary, Right main catenary, Left handrail, Right handrail!
const CABLE_SEGS = 20;
const CABLE_SIDES = 5;

/**
 * Builds Level [10] SWINGING CANYON ROPE-BRIDGES & TIMBER MINE TRESTLES ('CANYON_BRIDGES'):
 * - Minimal solid ground: a dizzying -48m deep misty canyon chasm punctuated only by sculpted
 *   flat-topped sandstone/granite Rock Pillars
 * - Physically simulated 3D Swaying & Twisting Wooden Suspension Bridges:
 *     * Coupled catenary flexural wave sag under vehicle weight
 *     * 3D torsional roll/twist when driving off-center near left/right rope railings
 *     * Lateral canyon crosswind sway + live-deforming 3D suspension cables & rope handrails
 *     * Cracked Rotten Planks that creak and physically SNAP in two into tumbling 3D half-planks!
 * - Upper / Lower Crisscrossing Spans: Mid-canyon rock pillars fork into an Elevated Rickety
 *   Timber Mine Railway Trestle (tall X-braced timber bents + clear-span king-post truss +
 *   iron mine rails) crossing diagonally OVER a Low-Slung Swaying Rope-Bridge!
 */
export class CanyonRopeBridgeBuilder {
  public readonly atmosphereFxGroup = new THREE.Group();

  // Materials
  private readonly pillarRockMat: THREE.MeshStandardMaterial;
  private readonly pillarTopMat: THREE.MeshStandardMaterial;
  private readonly weatheredPlankMat: THREE.MeshStandardMaterial;
  private readonly rottenPlankMat: THREE.MeshStandardMaterial;
  private readonly trestleTimberMat: THREE.MeshStandardMaterial;
  private readonly braidedRopeMat: THREE.MeshStandardMaterial;
  private readonly rustedIronRailMat: THREE.MeshStandardMaterial;
  private readonly lanternGlowMat: THREE.MeshBasicMaterial;
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
  private readonly windMoteCount = 950;
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

    this.pillarTopMat = new THREE.MeshStandardMaterial({
      color: 0x9c7a5b,
      map: textures.terrainDetail.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.85, 0.85),
      roughness: 0.82,
      metalness: 0.03,
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

    this.lanternGlowMat = new THREE.MeshBasicMaterial({
      color: 0xffbf54,
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
    this.halfPlankGeo = new THREE.BoxGeometry(3.2, 0.22, 0.92);
    this.railPostGeo = new THREE.CylinderGeometry(0.075, 0.09, 1.38, 6);
    this.railPostGeo.translate(0, 0.69, 0);
    this.anchorTowerGeo = this.createBridgeAnchorTowerGeo();
    this.unitBoxGeo = new THREE.BoxGeometry(1, 1, 1);
    this.lanternBulbGeo = new THREE.SphereGeometry(0.22, 8, 6);
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
   * Sculpted towering flat-topped Sandstone / Canyon Granite Spire Pillar (unit radius = 1, height = 52m: y = 0 .. -52m!)
   */
  private createSculptedRockPillarGeo(seed: number): THREE.BufferGeometry {
    const radialSegs = 28;
    const rings = [
      { y: 0.0, r: 0.0, col: 0x9c7b5c }, // Flat crown center!
      { y: 0.0, r: 0.65, col: 0x967556 },
      { y: -0.04, r: 1.0, col: 0x8a6648 }, // Rim brink
      { y: -2.8, r: 1.04, col: 0x7a5238 }, // Overhanging sandstone caprock ledge!
      { y: -10.5, r: 0.88, col: 0x694530 },
      { y: -24.0, r: 0.78, col: 0x4f3528 },
      { y: -38.0, r: 0.92, col: 0x3b2a24 },
      { y: -52.0, r: 1.18, col: 0x28201e }, // Flared abyssal base
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
        // Keep the top driving rim (r <= 2) close to true ellipse so physics radius matches 3D rim!
        const crag =
          r <= 2
            ? 1.0 + (r === 2 ? fbm2D(nx * 2.5 + seed, nz * 2.5, 2) * 0.04 : 0)
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
        // True +Y upward on crown & outward radial normals on cliff walls!
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
   * Heavy Timber A-Frame Suspension Bridge Cable Tower (straddles bridge entrance!)
   */
  private createBridgeAnchorTowerGeo(): THREE.BufferGeometry {
    const parts: THREE.BufferGeometry[] = [];
    // Left & Right twin timber A-frame pylons + overhead crossbeam
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
    // High overhead timber portal crossbeam (y = 5.15m -> plenty of clearance for 4x4!)
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
    // 1. Layered Swirling Canyon Mist Cloud Sheets below the bridges (y = -11m, -21m, -33m)
    const mistPlaneGeo = new THREE.PlaneGeometry(130, 145);
    mistPlaneGeo.rotateX(-Math.PI * 0.5);
    const mistHeights = [-10.5, -19.5, -31.0];
    for (let i = 0; i < mistHeights.length; i++) {
      const m = new THREE.Mesh(mistPlaneGeo, this.chasmMistSheetMat);
      m.position.set(0, mistHeights[i], 0);
      m.renderOrder = -1;
      this.atmosphereFxGroup.add(m);
      this.mistPlanes.push(m);
    }

    // 2. Whipping Canyon Crosswind Dust & Mist Wisps
    this.windPositions = new Float32Array(this.windMoteCount * 3);
    this.windVelocities = new Float32Array(this.windMoteCount * 3);
    const rng = mulberry32(771239);
    for (let i = 0; i < this.windMoteCount; i++) {
      const ix = i * 3;
      this.windPositions[ix] = (rng() - 0.5) * 120;
      this.windPositions[ix + 1] = lerp(-24.0, 16.0, rng());
      this.windPositions[ix + 2] = (rng() - 0.5) * 150;

      // Brisk lateral canyon wind blowing across the chasm (+X)
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

  /**
   * Populates a 112m chunk of [10] SWINGING CANYON ROPE-BRIDGES & TIMBER MINE TRESTLES:
   * - Entry Rock Pillar (u = 0..15m) -> Hero Swaying Rope-Bridge (u = 13..47m)
   * - Mid-Chasm Split Spire (u = 45..61m) -> Splits into CRISSCROSSING UPPER & LOWER BRIDGES:
   *     * UPPER HIGH-ROAD: Rickety Ascending Timber Mine Railway Trestle (y = 0 -> +6.2m -> 0,
   *       diagonal -8.5m -> +8.5m, registered as CrisscrossRootBridge so cars drive ON TOP OR UNDER!)
   *     * LOWER LOW-ROAD: Low-Slung Twisting Canyon Rope-Bridge (y = 0 -> -2.2m -> 0,
   *       diagonal +8.5m -> -8.5m passing directly UNDER the clear-span trestle truss at u = 79m!)
   * - Exit Confluence Rock Pillar (u = 97..112m) connecting seamlessly to next chunk's Entry Pillar!
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
    const towerMats: THREE.Matrix4[] = [];
    const lanternPositions: THREE.Vector3[] = [];

    const addRockPillar = (
      x: number,
      z: number,
      rx: number,
      rz: number,
      deckY: number,
      variant: number
    ) => {
      const geo = this.pillarBodyGeos[variant % this.pillarBodyGeos.length];
      const mesh = new THREE.Mesh(geo, this.pillarRockMat);
      mesh.position.set(x, deckY, z);
      mesh.scale.set(rx, 1.0, rz);
      mesh.castShadow = true;
      mesh.receiveShadow = true;
      group.add(mesh);

      pillars.push({ x, z, radiusX: rx, radiusZ: rz, deckY });
    };

    // -------------------------------------------------------------------------
    // 1. SCULPTED FLAT-TOPPED CANYON ROCK PILLARS (Minimal Solid Ground!)
    //    Entry & Exit pillars overlap across chunk borders for 0.0mm seamless transitions,
    //    and every bridge abutment anchors deep inside its pillar's flat rSq <= 0.40 crown!
    // -------------------------------------------------------------------------
    const zEntry = zStart - 7.5;
    const xEntry = sampleCourseX(zEntry);
    const entryRz = chunkIndex === 0 ? 13.5 : 10.5;
    addRockPillar(
      xEntry,
      zEntry,
      chunkIndex === 0 ? 18.5 : 17.5,
      entryRz,
      0.0,
      chunkIndex
    );

    const zMid = zStart - 53.0;
    const xMid = sampleCourseX(zMid);
    const midRx = 24.5;
    const midRz = 13.2;
    // Wide double-branch Mid-Canyon Split Butte!
    addRockPillar(xMid, zMid, midRx, midRz, 0.0, chunkIndex + 1);

    const zExit = zStart - 104.5;
    const xExit = sampleCourseX(zExit);
    const exitRx = 24.5;
    const exitRz = 13.2;
    addRockPillar(xExit, zExit, exitRx, exitRz, 0.0, chunkIndex + 2);

    // Decorative Background Canyon Needle Spires out in the deep mist (pure visual depth!)
    for (let s = 0; s < 4; s++) {
      const us = lerp(15.0, 96.0, (s + 0.25 + rng() * 0.5) / 4);
      const zs = zStart - us;
      const side = s % 2 === 0 ? -1 : 1;
      const xs = sampleCourseX(zs) + side * lerp(28.0, 44.0, rng());
      const spire = new THREE.Mesh(
        this.pillarBodyGeos[(chunkIndex + s) % 3],
        this.pillarRockMat
      );
      spire.position.set(xs, lerp(-8.0, -2.5, rng()), zs);
      const sr = lerp(4.2, 7.5, rng());
      spire.scale.set(sr, lerp(0.85, 1.15, rng()), sr);
      group.add(spire);
    }

    // -------------------------------------------------------------------------
    // 2. SPAN 1: HERO SWAYING WOODEN SUSPENSION BRIDGE (Flush Rim-to-Rim!)
    // -------------------------------------------------------------------------
    const zB1Start = zEntry - entryRz * 0.88;
    const zB1End = zMid + midRz * 0.88;
    const mainRopeBridge = this.buildSwayingRopeBridge(
      group,
      xEntry,
      0.04,
      zB1Start,
      xMid,
      0.04,
      zB1End,
      3.95, // 7.9m wide wooden suspension deck!
      1.35, // Parabolic catenary sag
      rng,
      towerMats,
      lanternPositions
    );
    ropeBridges.push(mainRopeBridge);

    // -------------------------------------------------------------------------
    // 3. SPAN 2A (LOWER CRISSCROSSING SPAN):
    //    Twisting Low-Slung Rope-Bridge crossing DIAGONALLY (+8.0m -> -8.0m) at y = -2.25m!
    //    Flush rim-to-rim at rSq = (8/20.5)^2 + (8.4/10.8)^2 = 0.757 (right inside flat rim!)
    // -------------------------------------------------------------------------
    const flipCrisscross = chunkIndex % 2 === 0 ? 1 : -1;
    const zCrossStart = zMid - 11.2;
    const zCrossEnd = zExit + 11.2;
    const lowerStartX = xMid + flipCrisscross * 7.6;
    const lowerEndX = xExit - flipCrisscross * 7.6;

    const lowerRopeBridge = this.buildSwayingRopeBridge(
      group,
      lowerStartX,
      0.04,
      zCrossStart,
      lowerEndX,
      0.04,
      zCrossEnd,
      3.85, // 7.7m wide swaying deck
      2.35, // Deep catenary belly dipping -2.35m below the Upper Mine Trestle!
      rng,
      towerMats,
      lanternPositions
    );
    ropeBridges.push(lowerRopeBridge);

    // -------------------------------------------------------------------------
    // 4. SPAN 2B (UPPER CRISSCROSSING SPAN):
    //    Elevated Rickety Timber Mine Railway Trestle climbing to y = +4.65m
    //    and crossing DIAGONALLY (-7.6m -> +7.6m) DIRECTLY OVER the Lower Rope-Bridge!
    // -------------------------------------------------------------------------
    const zTrestleStart = zMid - 6.8;
    const zTrestleEnd = zExit + 6.8;
    const upperStartX = xMid - flipCrisscross * 7.6;
    const upperEndX = xExit + flipCrisscross * 7.6;

    this.buildElevatedTimberMineTrestle(
      group,
      upperStartX,
      zTrestleStart,
      upperEndX,
      zTrestleEnd,
      4.65, // Soars +4.65m above pillars -> 7.0m vertical clearance over Lower Rope-Bridge!
      4.05, // 8.1m wide timber railway trestle deck!
      rootBridges,
      nextBridgeId,
      chunkIndex,
      timberMats,
      railMats,
      lanternPositions
    );

    // Scenic abandoned iron Mine Ore-Cart parked safely on the Mid-Chasm Split Spire edge!
    {
      const cartMesh = new THREE.Mesh(this.oreCartGeo, this.rustedIronRailMat);
      cartMesh.position.set(xMid + flipCrisscross * 15.2, 0.02, zMid + 2.5);
      cartMesh.rotation.set(0, 0.42 * flipCrisscross, 0.08);
      cartMesh.castShadow = true;
      group.add(cartMesh);

      obstacles.push({
        id: nextObstacleId(),
        chunkIndex,
        kind: 'boulder',
        position: new THREE.Vector3(cartMesh.position.x, 0, cartMesh.position.z),
        radius: 1.15,
        height: 1.4,
        canShearVine: true,
      });
    }

    // -------------------------------------------------------------------------
    // 5. EMIT BATCHED INSTANCED TIMBER TRESTLE, RAILS, ANCHOR TOWERS & LANTERNS
    // -------------------------------------------------------------------------
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

      // Warm amber canyon lantern PointLight at the Mid-Chasm Split Spire!
      const heroLight = new THREE.PointLight(0xffb44c, 16.0, 34.0, 1.5);
      heroLight.position.set(xMid, 4.2, zMid);
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
    };
  }

  /**
   * Constructs a physically simulated Swaying & Twisting 3D Wooden Rope-Suspension Bridge!
   */
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
    // Forward bridge direction (dirX, 0, dirZ); yaw aligns local -Z with (dirX, dirZ)
    const yaw = Math.atan2(-dirX, -dirZ);
    const rightX = Math.cos(yaw);
    const rightZ = -Math.sin(yaw);

    const plankSpacing = 1.18;
    const plankCount = Math.max(12, Math.round(length / plankSpacing));
    const actualSpacing = length / (plankCount - 1);

    // Timber A-Frame Suspension Cable Towers at both bridge abutments!
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
      // Natural unloaded parabolic catenary curve
      restY[i] = lerp(ay, by, f) - maxCatenaryDrop * 4.0 * f * (1.0 - f);

      // Mark ~16% of interior planks as Rotten (at least 5 planks apart so momentum jumps cleanly!)
      if (i >= 4 && i <= plankCount - 5 && i - lastRottenIdx >= 5 && rng() < 0.34) {
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

    const postIm = new THREE.InstancedMesh(this.railPostGeo, this.trestleTimberMat, plankCount * 2);
    postIm.castShadow = true;
    postIm.frustumCulled = false;
    group.add(postIm);

    // Live-deforming 4-strand Braided Rope Geometry (2 parabolic main cables + 2 handrail ropes!)
    const vertsPerStrand = (CABLE_SEGS + 1) * (CABLE_SIDES + 1);
    const totalVerts = CABLE_STRANDS_PER_BRIDGE * vertsPerStrand;
    const cPos = new Float32Array(totalVerts * 3);
    const cNorm = new Float32Array(totalVerts * 3);
    const cUv = new Float32Array(totalVerts * 2);
    const cIdx: number[] = [];

    const ringCols = CABLE_SIDES + 1;
    for (let st = 0; st < CABLE_STRANDS_PER_BRIDGE; st++) {
      const baseV = st * vertsPerStrand;
      for (let r = 0; r <= CABLE_SEGS; r++) {
        for (let c = 0; c <= CABLE_SIDES; c++) {
          const uvOff = (baseV + r * ringCols + c) * 2;
          cUv[uvOff] = c / CABLE_SIDES;
          cUv[uvOff + 1] = r * 0.8;
        }
      }
      for (let r = 0; r < CABLE_SEGS; r++) {
        for (let c = 0; c < CABLE_SIDES; c++) {
          const a = baseV + r * ringCols + c;
          const b = a + 1;
          const d = a + ringCols;
          const e = d + 1;
          cIdx.push(a, b, e, a, e, d);
        }
      }
    }

    const cableRopeGeo = new THREE.BufferGeometry();
    const posAttr = new THREE.BufferAttribute(cPos, 3);
    posAttr.setUsage(THREE.DynamicDrawUsage);
    const normAttr = new THREE.BufferAttribute(cNorm, 3);
    normAttr.setUsage(THREE.DynamicDrawUsage);
    cableRopeGeo.setAttribute('position', posAttr);
    cableRopeGeo.setAttribute('normal', normAttr);
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

    // Initialize visual plank & cable transforms at rest!
    this.syncRopeBridgeMeshes(bridge);
    return bridge;
  }

  /**
   * Builds the Elevated Rickety Timber Mine Railway Trestle (Upper Crisscrossing Span!):
   * - Registers smooth multi-segment `CrisscrossRootBridge` so the 4x4 can drive ON TOP OF
   *   the +5.85m railway deck OR pass cleanly UNDERNEATH it on the Lower Rope-Bridge!
   * - Features tall X-braced timber bents + a wide Clear-Span King-Post Truss over the midpoint
   *   so zero timber posts ever block the Lower Rope-Bridge crossing underneath!
   */
  private buildElevatedTimberMineTrestle(
    _group: THREE.Group,
    ax: number,
    az: number,
    bx: number,
    bz: number,
    peakHeightY: number,
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

    // Smooth arched railway trestle elevation profile (~21° climbable railway grade!):
    const heightAt = (t: number): number => {
      const rampUp = smoothstep(0.0, 0.42, t);
      const rampDown = 1.0 - smoothstep(0.58, 1.0, t);
      return peakHeightY * rampUp * rampDown;
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

      // Note: sampleCrisscrossRootDeck adds +0.18m to lerp(ay, by, t), so store ay = y0 - 0.18!
      segments.push({
        ax: x0,
        ay: y0 - 0.18,
        az: z0,
        bx: x1,
        by: y1 - 0.18,
        bz: z1,
        deckHalfWidth,
        thickness: 1.35, // Allows vehicles below (deckY - 1.35m) to pass cleanly underneath!
      });

      const xm = (x0 + x1) * 0.5;
      const ym = (y0 + y1) * 0.5;
      const zm = (z0 + z1) * 0.5;
      const segLen = totalLen / segCount;
      const pitch = Math.atan2(y1 - y0, segLen);

      // 1. Heavy Longitudinal Timber Trestle Stringer Beams (left, center, right)
      for (const latFrac of [-0.72, 0.0, 0.72]) {
        const off = latFrac * deckHalfWidth;
        this._dummy.position.set(xm + rightX * off, ym - 0.36, zm + rightZ * off);
        this._euler.set(pitch, yaw, 0, 'YXZ');
        this._dummy.rotation.copy(this._euler);
        this._dummy.scale.set(0.42, 0.52, segLen + 0.18);
        this._dummy.updateMatrix();
        timberMats.push(this._dummy.matrix.clone());
      }

      // 2. Closely-Spaced Wooden Railway Crossties + Outer Wheel Curb Guard Timbers
      const tieSub = 3;
      for (let k = 0; k < tieSub; k++) {
        const tk = lerp(t0, t1, (k + 0.5) / tieSub);
        const tx = lerp(ax, bx, tk);
        const tz = lerp(az, bz, tk);
        const ty = heightAt(tk);
        this._dummy.position.set(tx, ty - 0.08, tz);
        this._euler.set(pitch, yaw, 0, 'YXZ');
        this._dummy.rotation.copy(this._euler);
        this._dummy.scale.set(deckHalfWidth * 2.04, 0.16, 0.68);
        this._dummy.updateMatrix();
        timberMats.push(this._dummy.matrix.clone());
      }

      // 3. Twin Narrow-Gauge Iron Mine Rails + Low Outer Curb Beams
      for (const railOff of [-1.25, 1.25]) {
        this._dummy.position.set(xm + rightX * railOff, ym + 0.05, zm + rightZ * railOff);
        this._euler.set(pitch, yaw, 0, 'YXZ');
        this._dummy.rotation.copy(this._euler);
        this._dummy.scale.set(0.14, 0.12, segLen + 0.08);
        this._dummy.updateMatrix();
        railMats.push(this._dummy.matrix.clone());
      }
      for (const curbSide of [-1, 1]) {
        const cOff = curbSide * (deckHalfWidth - 0.22);
        this._dummy.position.set(xm + rightX * cOff, ym + 0.16, zm + rightZ * cOff);
        this._euler.set(pitch, yaw, 0, 'YXZ');
        this._dummy.rotation.copy(this._euler);
        this._dummy.scale.set(0.28, 0.28, segLen + 0.08);
        this._dummy.updateMatrix();
        timberMats.push(this._dummy.matrix.clone());
      }
    }

    // 4. Tall Tapered X-Braced Timber Trestle Bents Plunging -36m into the Misty Gorge!
    //    CRITICAL: Skip t in [0.36, 0.64] (where the Lower Rope-Bridge crosses underneath!)
    //    and build a dramatic overhead King-Post Truss across that central clear span!
    const bentFracs = [0.14, 0.28, 0.35, 0.65, 0.74, 0.86];
    for (const bf of bentFracs) {
      const bxPos = lerp(ax, bx, bf);
      const bzPos = lerp(az, bz, bf);
      const topY = heightAt(bf) - 0.55;
      const bentH = 34.0 + topY;

      // Cap beam
      this._dummy.position.set(bxPos, topY, bzPos);
      this._dummy.rotation.set(0, yaw, 0);
      this._dummy.scale.set(deckHalfWidth * 2.15, 0.45, 0.48);
      this._dummy.updateMatrix();
      timberMats.push(this._dummy.matrix.clone());

      // 4 Battered Vertical/Slanted Timber Trestle Posts
      for (const pFrac of [-0.88, -0.32, 0.32, 0.88]) {
        const latOff = pFrac * deckHalfWidth;
        const batterAngle = -pFrac * 0.085;
        const midOff = latOff + pFrac * 1.45;
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

      // Horizontal tier girts + Diagonal X-braces!
      for (const tierDrop of [5.5, 13.0, 21.5]) {
        const ty = topY - tierDrop;
        this._dummy.position.set(bxPos, ty, bzPos);
        this._dummy.rotation.set(0, yaw, 0);
        this._dummy.scale.set(deckHalfWidth * 2.4 + tierDrop * 0.14, 0.28, 0.3);
        this._dummy.updateMatrix();
        timberMats.push(this._dummy.matrix.clone());

        for (const diagSign of [-1, 1]) {
          this._dummy.position.set(bxPos, ty + 2.8, bzPos);
          this._euler.set(0, yaw, diagSign * 0.68, 'YXZ');
          this._dummy.rotation.copy(this._euler);
          this._dummy.scale.set(0.22, 8.8, 0.22);
          this._dummy.updateMatrix();
          timberMats.push(this._dummy.matrix.clone());
        }
      }

      // Hanging Mine Lantern Posts on outer trestle bents
      if (bf === 0.28 || bf === 0.74) {
        for (const lSide of [-1, 1]) {
          const lx = bxPos + rightX * lSide * (deckHalfWidth + 0.15);
          const lz = bzPos + rightZ * lSide * (deckHalfWidth + 0.15);
          const ly = heightAt(bf);
          this._dummy.position.set(lx, ly + 1.35, lz);
          this._dummy.rotation.set(0, yaw, 0);
          this._dummy.scale.set(0.16, 2.7, 0.16);
          this._dummy.updateMatrix();
          timberMats.push(this._dummy.matrix.clone());
          lanternPositions.push(new THREE.Vector3(lx, ly + 2.65, lz));
        }
      }
    }

    // Central Clear-Span Side Truss Rails (t = 0.35 .. 0.65) framing the sky-high crossover!
    {
      const xm = lerp(ax, bx, 0.5);
      const zm = lerp(az, bz, 0.5);
      const ym = heightAt(0.5);
      const trussLen = totalLen * 0.3;
      for (const side of [-1, 1]) {
        const tx = xm + rightX * side * (deckHalfWidth - 0.15);
        const tz = zm + rightZ * side * (deckHalfWidth - 0.15);
        // Top truss chord handrail (y + 1.45m)
        this._dummy.position.set(tx, ym + 1.45, tz);
        this._dummy.rotation.set(0, yaw, 0);
        this._dummy.scale.set(0.24, 0.24, trussLen);
        this._dummy.updateMatrix();
        timberMats.push(this._dummy.matrix.clone());
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

  /**
   * Updates all 3D plank instances, handrail posts, and 4-strand catenary rope tubes
   * from the live coupled wave / twist / sway simulation!
   */
  private syncRopeBridgeMeshes(br: SwayingRopeBridge): void {
    const n = br.plankCount;
    const width = br.halfWidth * 2.0;

    for (let i = 0; i < n; i++) {
      const f = i / (n - 1);
      const sway = br.swayX[i];
      const px = lerp(br.ax, br.bx, f) + br.rightX * sway;
      const pz = lerp(br.az, br.bz, f) + br.rightZ * sway;
      const py = br.restY[i] + br.sagY[i];

      // Local longitudinal slope pitch along the sagging catenary!
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

      // Intact vs Rotten plank visibility via scale (zero draw-call overhead!)
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
        // Slight jitter while cracking!
        const jitter = br.crackTimer[i] > 0 ? Math.sin(br.crackTimer[i] * 90.0) * 0.06 : 0;
        this._dummy.position.set(px, py - 0.13 + jitter, pz);
        this._dummy.scale.set(width * 0.98, 0.92, 0.92);
      } else {
        this._dummy.scale.set(0, 0, 0);
      }
      this._dummy.updateMatrix();
      br.rottenPlankIm.setMatrixAt(i, this._dummy.matrix);

      // Left & Right Rope-Handrail Timber Posts on every 2nd plank!
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

    // Update the 4 live 3D Braided Suspension & Handrail Ropes!
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

        // Main cables sweep down from 4.85m tower tops to 1.45m at mid-span; handrail ropes follow posts at +1.28m!
        const towerSweep = isMainCable ? Math.pow(Math.abs(f * 2.0 - 1.0), 2.1) * 3.45 + 1.42 : 1.28;
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

  /**
   * Samples live Canyon Rock Pillars & Swaying/Twisting Suspension Rope-Bridges!
   * (Note: The Elevated Timber Mine Trestle is sampled via `sampleCrisscrossRootDeck`
   * using wheel 3D height so cars can BOTH drive on top of the trestle AND pass underneath!)
   */
  public sampleLiveCanyonSurface(
    data: CanyonBridgeChunkData,
    x: number,
    z: number
  ): { active: boolean; height: number; onRopeBridge: boolean } {
    let bestH = -48.0;
    let found = false;
    let onRopeBridge = false;

    // 1. Check Sculpted Flat-Topped Rock Pillars
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
      } else if (rSq < 1.18) {
        hasSkirt = true;
        const drop = smoothstep(0.94, 1.18, rSq);
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

      // Account for live lateral wind/load sway of the bridge!
      const sway = lerp(br.swayX[i0], br.swayX[i1], frac);
      const rawLat = dx * br.rightX + dz * br.rightZ - sway;

      if (Math.abs(rawLat) <= br.halfWidth + 0.12) {
        const nearestIdx = Math.round(pFloat);
        const plankDist = Math.abs(pFloat - nearestIdx);
        // Snapped rotten plank creates a sharp 0.58m wheel-drop pocket across the 1.18m gap
        // (avoids a 48m finite-difference wall spike so fast 4x4s skip & thump cleanly across!)
        const snappedDip =
          br.snappedCooldown[nearestIdx] > 0
            ? -0.58 * Math.cos(clamp(plankDist * 2.0, 0, 1) * Math.PI * 0.5)
            : 0.0;

        const deckCenterY =
          lerp(br.restY[i0] + br.sagY[i0], br.restY[i1] + br.sagY[i1], frac) + snappedDip;
        const roll = lerp(br.rollZ[i0], br.rollZ[i1], frac);
        // 3D torsional roll tilts the left vs right side of the plank!
        const rollTiltY = -rawLat * Math.sin(roll);
        // Subtle plank-to-plank wooden rumble!
        const plankRumble = Math.abs(Math.sin(pFloat * Math.PI)) * 0.022;
        const h = deckCenterY + rollTiltY + plankRumble;

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

  /**
   * Steps 3D Coupled Suspension-Bridge Wave Physics, Torsional Roll, Wind Sway,
   * Rotten Plank Cracking/Snapping, and Falling Broken Plank Halves!
   */
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

      // Check if vehicle is currently riding THIS suspension bridge!
      const dx = vehiclePos.x - br.ax;
      const dz = vehiclePos.z - br.az;
      const along = dx * br.dirX + dz * br.dirZ;
      let carPlankFloat = -1;
      let carLat = 0;

      if (!passiveProtection && along >= 0 && along <= br.length) {
        const idxApprox = clamp(
          Math.round((along / br.length) * (n - 1)),
          0,
          n - 1
        );
        const lat = dx * br.rightX + dz * br.rightZ - br.swayX[idxApprox];
        const approxDeckY = br.restY[idxApprox] + br.sagY[idxApprox];
        // Crucially verify vertical proximity (`|vehiclePos.y - deckY| < 3.2m`) so driving on the
        // +5.85m Upper Timber Trestle overhead does NOT depress or snap the Lower Rope-Bridge below!
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

      // Apply Vehicle Weight Sag + Torsional Twist Torque + Lateral Sway!
      for (let i = 1; i < n - 1; i++) {
        const f = i / (n - 1);
        const envelope = Math.sin(f * Math.PI);

        // Ambient canyon crosswind sway
        const windTarget =
          Math.sin(elapsed * 1.45 + br.windPhase + f * 2.6) * 0.36 * envelope +
          Math.cos(elapsed * 2.9 + br.windPhase * 1.7) * 0.11 * envelope;
        br.swayX[i] = lerp(br.swayX[i], windTarget, clamp(3.5 * clampedDt, 0, 1));

        // Coupled neighbor cable tension + restoring spring
        const neighborAvgY = (br.sagY[i - 1] + br.sagY[i + 1]) * 0.5;
        let accelY = (neighborAvgY - br.sagY[i]) * 185.0 - br.sagY[i] * 22.0 - br.velY[i] * 5.2;

        const neighborAvgRoll = (br.rollZ[i - 1] + br.rollZ[i + 1]) * 0.5;
        let accelRoll =
          (neighborAvgRoll - br.rollZ[i]) * 140.0 - br.rollZ[i] * 34.0 - br.velRoll[i] * 6.0;

        if (carPlankFloat >= 0) {
          const distPlanks = Math.abs(i - carPlankFloat);
          if (distPlanks < 3.2) {
            const wKernel = Math.cos((distPlanks / 3.2) * Math.PI * 0.5);
            // Heavy 4x4 downward load + dynamic bounce impact!
            accelY -= (26.5 + Math.min(18.0, vehicleSpeed * 0.55)) * wKernel;
            // Off-center weight twists the bridge in 3D roll toward the loaded side!
            const normLat = clamp(carLat / br.halfWidth, -1, 1);
            accelRoll -= normLat * 14.5 * wKernel;
            br.swayX[i] += normLat * 0.35 * wKernel * clampedDt;

            // Trigger Rotten Plank Crack -> Snap!
            if (
              distPlanks < 1.05 &&
              br.isRotten[i] === 1 &&
              br.snappedCooldown[i] <= 0 &&
              br.crackTimer[i] <= 0
            ) {
              br.crackTimer[i] = 0.085; // 85ms sharp crack warning, then SNAP!
              br.velY[i] -= 2.8;
            }
          }
        }

        br.velY[i] += accelY * clampedDt;
        br.velRoll[i] += accelRoll * clampedDt;

        // Tick Rotten Plank Cracking & Respawn Timers
        if (br.crackTimer[i] > 0) {
          br.crackTimer[i] -= clampedDt;
          if (br.crackTimer[i] <= 0) {
            br.crackTimer[i] = 0;
            br.snappedCooldown[i] = 9.5; // Snapped open for 9.5s!
            this.spawnSnappedPlankHalves(data, br, i);
          }
        } else if (br.snappedCooldown[i] > 0) {
          br.snappedCooldown[i] = Math.max(0, br.snappedCooldown[i] - clampedDt);
        }
      }

      // Integrate interior plank positions (endpoints 0 and n-1 remain pinned to rock abutments!)
      for (let i = 1; i < n - 1; i++) {
        br.sagY[i] = clamp(br.sagY[i] + br.velY[i] * clampedDt, -0.92, 0.48);
        br.rollZ[i] = clamp(br.rollZ[i] + br.velRoll[i] * clampedDt, -0.13, 0.13);
      }

      this.syncRopeBridgeMeshes(br);
    }

    // Step Falling Broken 3D Plank Halves tumbling into the -48m Canyon Mist!
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

    // Drift chasm mist sheets smoothly below active viewpoint
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

      if (x - focusPos.x > 60) x -= 120;
      if (x - focusPos.x < -60) x += 120;
      if (y > focusPos.y + 18) y -= 40;
      if (y < focusPos.y - 24) y += 40;
      if (z - focusPos.z > 75) z -= 150;
      if (z - focusPos.z < -75) z += 150;

      this.windPositions[ix] = x;
      this.windPositions[ix + 1] = y;
      this.windPositions[ix + 2] = z;
    }
    posAttr.needsUpdate = true;
  }
}

import * as THREE from 'three';
import { clamp, lerp, mulberry32, smoothstep } from '../math/noise';
import { ProceduralTextureLibrary } from '../graphics/proceduralTextures';
import { CanopyAnchor, RootAnchor, TrailCorridorWorld, WorldObstacle } from '../world/trailCorridor';
import { CHASSIS_HARDPOINTS, VehicleRigidBody } from '../physics/vehiclePhysics';

export type VineArchetype = 'STRIKER' | 'CURTAIN' | 'ARCH_GROWER' | 'ROOT_SNARE';
export type VineLifecycleState =
  | 'BUDDING'
  | 'GROWING'
  | 'STRIKING'
  | 'LATCHED'
  | 'SNAPPED_RECOIL';

export type SeverReason = 'TENSION_SNAP' | 'ROCK_SHEAR' | 'WRENCH_ESCAPE' | 'HYDRAULIC_SHRED';

export interface ActiveLatchedTelemetry {
  vineId: number;
  archetype: VineArchetype;
  hardpointLabel: string;
  localAttach: THREE.Vector3;
  tensionN: number;
  maxTensionN: number;
  strain01: number;
  escapeProgress01: number;
  overgrowth01: number;
  isScrapingRock: boolean;
}

export interface VineDesignParams {
  growthSpeedScale: number;
  strikeAggression: number;
  tensileStrengthScale: number;
  adhesionRadiusScale: number;
}

interface VineInstance {
  id: number;
  chunkIndex: number;
  archetype: VineArchetype;
  state: VineLifecycleState;

  anchorPos: THREE.Vector3;
  archTargetPos: THREE.Vector3;

  posX: Float32Array;
  posY: Float32Array;
  posZ: Float32Array;
  prevX: Float32Array;
  prevY: Float32Array;
  prevZ: Float32Array;

  currentLength: number;
  matureLength: number;
  baseRadius: number;
  tipRadius: number;
  growthRate: number;
  nutationPhase: number;

  latchedNodeIndex: number;
  attachLocalPoint: THREE.Vector3;
  hardpointLabel: string;
  strikeTargetLocal: THREE.Vector3;
  latchedDuration: number;
  overgrowth01: number;
  reelingOffset: number;

  currentTensionN: number;
  baseBreakTensionN: number;
  strainIntegral: number;
  rockChafeDamage: number;
  wrenchEscapeDamage: number;
  isScrapingRock: boolean;
  recoilTimer: number;
}

const NODES_PER_VINE = 18;
const RADIAL_SIDES = 7;
const MAX_POOLED_VINES = 280; // Holds ALL hanging curtains, strikers, arches & ground roots across 5 chunks!
const VERTS_PER_RING = RADIAL_SIDES + 1;
const VERTS_PER_VINE = NODES_PER_VINE * VERTS_PER_RING;
const INDICES_PER_VINE = (NODES_PER_VINE - 1) * RADIAL_SIDES * 6;

export class VineEcosystem {
  public readonly group = new THREE.Group();
  public readonly chassisCreepGroup = new THREE.Group();

  public params: VineDesignParams = {
    growthSpeedScale: 1.0,
    strikeAggression: 1.0,
    tensileStrengthScale: 1.0,
    adhesionRadiusScale: 1.0,
  };

  public vinesSeveredCount = 0;
  public vinesShearedCount = 0;
  public rootsEscapedCount = 0;
  public totalLatchedCount = 0;

  public onVineLatched?: (archetype: VineArchetype, label: string, worldPos: THREE.Vector3) => void;
  public onVineSevered?: (
    reason: SeverReason,
    archetype: VineArchetype,
    peakForceKN: number,
    worldBreakPos: THREE.Vector3
  ) => void;
  public onStrikerWarning?: (worldTipPos: THREE.Vector3) => void;

  private vines: VineInstance[] = [];
  private nextVineId = 1;
  private lastPlayerZ = 0;

  private tubeGeo: THREE.BufferGeometry;
  private tubePosAttr: THREE.BufferAttribute;
  private tubeNormAttr: THREE.BufferAttribute;
  private tubeUvAttr: THREE.BufferAttribute;
  private tubeColAttr: THREE.BufferAttribute;
  private tubeMesh: THREE.Mesh;

  private leafInstanced: THREE.InstancedMesh;
  private calyxJawInstanced: THREE.InstancedMesh;

  private chassisTendrilMeshes: Array<{
    mesh: THREE.Mesh;
    maxScale: number;
    hardpointIdx: number;
  }> = [];

  private rng = mulberry32(882371);

  private _tmpA = new THREE.Vector3();
  private _tmpB = new THREE.Vector3();
  private _tmpC = new THREE.Vector3();
  private _tangent = new THREE.Vector3();
  private _normal = new THREE.Vector3();
  private _binormal = new THREE.Vector3();
  private _color = new THREE.Color();
  private _dummy = new THREE.Object3D();

  constructor(textures: ProceduralTextureLibrary) {
    const totalVerts = MAX_POOLED_VINES * VERTS_PER_VINE;
    const totalIndices = MAX_POOLED_VINES * INDICES_PER_VINE;

    const positions = new Float32Array(totalVerts * 3);
    const normals = new Float32Array(totalVerts * 3);
    const uvs = new Float32Array(totalVerts * 2);
    const colors = new Float32Array(totalVerts * 3);
    const indices = new Uint32Array(totalIndices);

    let idxPtr = 0;
    for (let v = 0; v < MAX_POOLED_VINES; v++) {
      const baseV = v * VERTS_PER_VINE;
      for (let ring = 0; ring < NODES_PER_VINE - 1; ring++) {
        const r0 = baseV + ring * VERTS_PER_RING;
        const r1 = r0 + VERTS_PER_RING;
        for (let s = 0; s < RADIAL_SIDES; s++) {
          const a = r0 + s;
          const b = a + 1;
          const c = r1 + s;
          const d = c + 1;
          indices[idxPtr++] = a;
          indices[idxPtr++] = b;
          indices[idxPtr++] = c;
          indices[idxPtr++] = b;
          indices[idxPtr++] = d;
          indices[idxPtr++] = c;
        }
      }
    }

    this.tubeGeo = new THREE.BufferGeometry();
    this.tubePosAttr = new THREE.BufferAttribute(positions, 3);
    this.tubePosAttr.setUsage(THREE.DynamicDrawUsage);
    this.tubeNormAttr = new THREE.BufferAttribute(normals, 3);
    this.tubeNormAttr.setUsage(THREE.DynamicDrawUsage);
    this.tubeUvAttr = new THREE.BufferAttribute(uvs, 2);
    this.tubeUvAttr.setUsage(THREE.DynamicDrawUsage);
    this.tubeColAttr = new THREE.BufferAttribute(colors, 3);
    this.tubeColAttr.setUsage(THREE.DynamicDrawUsage);

    this.tubeGeo.setAttribute('position', this.tubePosAttr);
    this.tubeGeo.setAttribute('normal', this.tubeNormAttr);
    this.tubeGeo.setAttribute('uv', this.tubeUvAttr);
    this.tubeGeo.setAttribute('color', this.tubeColAttr);
    this.tubeGeo.setIndex(new THREE.BufferAttribute(indices, 1));
    this.tubeGeo.setDrawRange(0, 0);
    this.tubeGeo.boundingSphere = new THREE.Sphere(new THREE.Vector3(), 16000);

    // Rich botanical stem material with subtle sunlit chlorophyll rim glow so vines read vividly against mist!
    const vineMat = new THREE.MeshStandardMaterial({
      map: textures.vineStem.map,
      normalMap: textures.vineStem.normalMap,
      normalScale: new THREE.Vector2(1.5, 1.5),
      vertexColors: true,
      emissive: new THREE.Color(0x182809),
      emissiveIntensity: 0.42,
      roughness: 0.62,
      metalness: 0.04,
    });

    this.tubeMesh = new THREE.Mesh(this.tubeGeo, vineMat);
    this.tubeMesh.castShadow = true;
    this.tubeMesh.receiveShadow = true;
    this.tubeMesh.frustumCulled = false;
    this.group.add(this.tubeMesh);

    // Lush, unmistakable cascading tropical vine leaves (1.08m x 0.68m!)
    const leafGeo = new THREE.PlaneGeometry(1.08, 0.68, 1, 2);
    leafGeo.translate(0.54, 0, 0);
    const lPos = leafGeo.attributes.position;
    for (let i = 0; i < lPos.count; i++) {
      const x = lPos.getX(i);
      lPos.setY(i, lPos.getY(i) - x * x * 0.22);
    }
    leafGeo.computeVertexNormals();

    const leafMat = new THREE.MeshStandardMaterial({
      map: textures.broadleafAtlas,
      alphaTest: 0.35,
      side: THREE.DoubleSide,
      color: new THREE.Color(0x95ca58),
      emissive: new THREE.Color(0x1e330b),
      emissiveIntensity: 0.35,
      roughness: 0.55,
    });
    this.leafInstanced = new THREE.InstancedMesh(leafGeo, leafMat, MAX_POOLED_VINES * 14);
    this.leafInstanced.frustumCulled = false;
    this.leafInstanced.count = 0;
    this.group.add(this.leafInstanced);

    const clawGroupGeo = this.buildPredatoryCalyxGeometry();
    const clawMat = new THREE.MeshStandardMaterial({
      vertexColors: true,
      roughness: 0.38,
      metalness: 0.06,
      emissive: new THREE.Color(0x521d06),
      emissiveIntensity: 0.65,
    });
    this.calyxJawInstanced = new THREE.InstancedMesh(clawGroupGeo, clawMat, MAX_POOLED_VINES);
    this.calyxJawInstanced.frustumCulled = false;
    this.calyxJawInstanced.count = 0;
    this.group.add(this.calyxJawInstanced);

    this.buildChassisOvergrowthRig(vineMat);
  }

  private buildPredatoryCalyxGeometry(): THREE.BufferGeometry {
    const positions: number[] = [];
    const normals: number[] = [];
    const colors: number[] = [];
    const indices: number[] = [];
    let vo = 0;

    const prongs = 3;
    const segs = 5;
    for (let p = 0; p < prongs; p++) {
      const azim = (p / prongs) * Math.PI * 2;
      const cosA = Math.cos(azim);
      const sinA = Math.sin(azim);

      for (let s = 0; s <= segs; s++) {
        const t = s / segs;
        const flare = Math.sin(t * Math.PI * 0.88) * 0.34;
        const zAlong = t * 0.72;
        const halfW = (1 - t * 0.82) * 0.11;

        const cx = cosA * flare;
        const cy = sinA * flare;
        const wx = -sinA * halfW;
        const wy = cosA * halfW;

        positions.push(cx - wx, cy - wy, zAlong, cx + wx, cy + wy, zAlong);
        normals.push(cosA, sinA, 0.25, cosA, sinA, 0.25);

        const r = lerp(0.38, 1.0, t);
        const g = lerp(0.58, 0.42, t);
        const b = lerp(0.18, 0.08, t);
        colors.push(r, g, b, r, g, b);
      }

      for (let s = 0; s < segs; s++) {
        const i0 = vo + s * 2;
        indices.push(i0, i0 + 1, i0 + 2, i0 + 1, i0 + 3, i0 + 2);
      }
      vo += (segs + 1) * 2;
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
    geo.setAttribute('normal', new THREE.Float32BufferAttribute(normals, 3));
    geo.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));
    geo.setIndex(indices);
    geo.computeVertexNormals();
    return geo;
  }

  private buildChassisOvergrowthRig(vineMat: THREE.Material): void {
    const paths: THREE.Vector3[][] = [
      [
        new THREE.Vector3(-0.62, 0.94, -0.42),
        new THREE.Vector3(-0.74, 0.72, -0.75),
        new THREE.Vector3(-0.52, 0.53, -1.18),
        new THREE.Vector3(-0.12, 0.51, -1.58),
      ],
      [
        new THREE.Vector3(0.62, 0.94, -0.42),
        new THREE.Vector3(0.74, 0.72, -0.75),
        new THREE.Vector3(0.48, 0.53, -1.22),
        new THREE.Vector3(0.08, 0.5, -1.64),
      ],
      [
        new THREE.Vector3(-0.64, 0.96, -0.35),
        new THREE.Vector3(-0.15, 0.99, -0.15),
        new THREE.Vector3(0.28, 0.98, 0.15),
        new THREE.Vector3(0.64, 0.96, 0.52),
      ],
      [
        new THREE.Vector3(-0.6, 0.95, 0.58),
        new THREE.Vector3(-0.35, 0.85, 1.08),
        new THREE.Vector3(0.0, 0.74, 1.55),
        new THREE.Vector3(0.32, 0.65, 1.75),
      ],
      [
        new THREE.Vector3(0.64, 0.95, 0.55),
        new THREE.Vector3(0.82, 0.68, 0.85),
        new THREE.Vector3(0.88, 0.44, 0.32),
        new THREE.Vector3(0.86, 0.38, -0.25),
      ],
      [
        new THREE.Vector3(-0.64, 0.95, 0.48),
        new THREE.Vector3(-0.85, 0.65, 0.25),
        new THREE.Vector3(-0.92, 0.42, -0.22),
        new THREE.Vector3(-0.78, 0.45, -0.82),
      ],
    ];

    paths.forEach((pts, idx) => {
      const curve = new THREE.CatmullRomCurve3(pts);
      const geo = new THREE.TubeGeometry(curve, 16, 0.036, 6, false);
      const count = geo.attributes.position.count;
      const cols = new Float32Array(count * 3);
      for (let i = 0; i < count; i++) {
        cols[i * 3] = 0.52;
        cols[i * 3 + 1] = 0.78;
        cols[i * 3 + 2] = 0.34;
      }
      geo.setAttribute('color', new THREE.BufferAttribute(cols, 3));
      const mesh = new THREE.Mesh(geo, vineMat);
      mesh.scale.setScalar(0.001);
      mesh.visible = false;
      this.chassisCreepGroup.add(mesh);
      this.chassisTendrilMeshes.push({ mesh, maxScale: 1.0, hardpointIdx: idx });
    });
  }

  /**
   * Distance-aware pool check: NEVER evicts vines near the player!
   * Only evicts whichever unlatched vine is furthest away from lastPlayerZ if pool is full.
   */
  private ensurePoolSpace(): boolean {
    if (this.vines.length < MAX_POOLED_VINES) return true;
    let furthestIdx = -1;
    let maxDist = -1;
    for (let i = 0; i < this.vines.length; i++) {
      const v = this.vines[i];
      if (v.state === 'LATCHED') continue;
      const d = Math.abs(v.anchorPos.z - this.lastPlayerZ);
      if (d > maxDist) {
        maxDist = d;
        furthestIdx = i;
      }
    }
    if (furthestIdx >= 0) {
      this.vines.splice(furthestIdx, 1);
      return true;
    }
    return false;
  }

  public spawnForChunk(
    chunkIndex: number,
    anchors: CanopyAnchor[],
    rootAnchors: RootAnchor[],
    world: TrailCorridorWorld
  ): void {
    // 1. Spawn Lush, Highly Visible Overhead Hanging Vines (CURTAIN, STRIKER, ARCH_GROWER)
    for (const anchor of anchors) {
      if (!this.ensurePoolSpace()) break;

      if (anchor.preferredArchetype === 'CURTAIN') {
        // Spawn a rich 3-strand hanging vine curtain right across the path / skylight!
        const strandCount = 3;
        for (let s = 0; s < strandCount; s++) {
          if (!this.ensurePoolSpace()) break;
          const offsetAlongBranch = (s - 1) * (1.85 + this.rng() * 1.15);
          const root = anchor.position
            .clone()
            .addScaledVector(anchor.branchTangent, offsetAlongBranch);
          const groundBelow = world.sampleHeightRaw(root.x, root.z).height;
          const clearanceAboveGround = lerp(0.95, 1.95, this.rng());
          const matureLen = clamp(root.y - (groundBelow + clearanceAboveGround), 4.8, 15.5);

          this.createVineInstance({
            chunkIndex,
            archetype: 'CURTAIN',
            anchorPos: root,
            archTargetPos: new THREE.Vector3(root.x, groundBelow + 1.0, root.z),
            // Start 72%-88% draped down immediately so hanging curtains are super visible down the forest!
            initialLength: matureLen * lerp(0.72, 0.88, this.rng()),
            matureLength: matureLen,
            baseRadius: 0.145,
            tipRadius: 0.068,
            growthRate: lerp(7.0, 11.0, this.rng()),
            baseBreakTensionN: 12400,
          });
        }
      } else if (anchor.preferredArchetype === 'STRIKER') {
        const groundBelow = anchor.trailCenterBelow.y;
        const heightSpan = anchor.position.y - groundBelow;
        const ambushLen = clamp(heightSpan - 2.6, 5.2, 12.2);
        this.createVineInstance({
          chunkIndex,
          archetype: 'STRIKER',
          anchorPos: anchor.position.clone(),
          archTargetPos: anchor.trailCenterBelow.clone(),
          // Hang clearly visible beneath the bough before growing & lunging!
          initialLength: ambushLen * lerp(0.62, 0.78, this.rng()),
          matureLength: ambushLen,
          baseRadius: 0.185,
          tipRadius: 0.082,
          growthRate: lerp(10.5, 15.5, this.rng()),
          baseBreakTensionN: 19200,
        });
      } else {
        const side = anchor.position.x > anchor.trailCenterBelow.x ? -1 : 1;
        const targetX = anchor.trailCenterBelow.x + side * anchor.trailHalfWidth * 0.68;
        const targetZ = anchor.position.z - lerp(5.0, 9.5, this.rng());
        const targetY = world.sampleHeightRaw(targetX, targetZ).height + lerp(1.2, 2.2, this.rng());
        const target = new THREE.Vector3(targetX, targetY, targetZ);
        const span = anchor.position.distanceTo(target);
        const mature = clamp(span * 1.04, 8.5, 21.0);

        this.createVineInstance({
          chunkIndex,
          archetype: 'ARCH_GROWER',
          anchorPos: anchor.position.clone(),
          archTargetPos: target,
          initialLength: mature * 0.45,
          matureLength: mature,
          baseRadius: 0.155,
          tipRadius: 0.07,
          growthRate: lerp(9.5, 14.5, this.rng()),
          baseBreakTensionN: 15200,
        });
      }
    }

    // 2. Spawn Living Forest-Floor Snare Roots (ROOT_SNARE)
    for (const rAnchor of rootAnchors) {
      if (!this.ensurePoolSpace()) break;
      const span = rAnchor.position.distanceTo(rAnchor.reachTargetPos);
      this.createVineInstance({
        chunkIndex,
        archetype: 'ROOT_SNARE',
        anchorPos: rAnchor.position.clone(),
        archTargetPos: rAnchor.reachTargetPos.clone(),
        initialLength: 1.4,
        matureLength: clamp(span * 1.08, 5.5, 13.5),
        baseRadius: 0.18,
        tipRadius: 0.062,
        growthRate: lerp(8.5, 13.0, this.rng()),
        baseBreakTensionN: 13800,
      });
    }
  }

  public removeChunkVines(chunkIndex: number): void {
    this.vines = this.vines.filter((v) => v.chunkIndex !== chunkIndex || v.state === 'LATCHED');
  }

  public resetAll(): void {
    this.vines.length = 0;
    this.tubeGeo.setDrawRange(0, 0);
    this.leafInstanced.count = 0;
    this.calyxJawInstanced.count = 0;
    for (const ct of this.chassisTendrilMeshes) {
      ct.mesh.visible = false;
      ct.mesh.scale.setScalar(0.001);
    }
  }

  public triggerHydraulicPurge(vehicle: VehicleRigidBody): number {
    let purged = 0;
    for (const v of this.vines) {
      if (v.state !== 'LATCHED') continue;
      if (v.archetype === 'ROOT_SNARE') {
        this.severVine(v, 'HYDRAULIC_SHRED', v.currentTensionN / 1000, vehicle);
        purged++;
      } else {
        v.wrenchEscapeDamage += 0.68;
        if (v.wrenchEscapeDamage >= 1.0 || v.currentTensionN > v.baseBreakTensionN * 0.45) {
          this.severVine(v, 'HYDRAULIC_SHRED', v.currentTensionN / 1000, vehicle);
          purged++;
        }
      }
    }
    return purged;
  }

  public spawnTestOverheadStrike(vehicle: VehicleRigidBody, world: TrailCorridorWorld): void {
    const fwd = new THREE.Vector3(0, 0, -1).applyQuaternion(vehicle.quaternion);
    const speed = Math.max(vehicle.forwardSpeedMps, 8);
    const aheadDist = clamp(speed * 0.95, 10, 26);
    const anchorX = vehicle.position.x + fwd.x * aheadDist + (this.rng() - 0.5) * 3.2;
    const anchorZ = vehicle.position.z + fwd.z * aheadDist;
    const groundY = world.sampleHeightRaw(anchorX, anchorZ).height;
    const anchorPos = new THREE.Vector3(anchorX, groundY + 12.0, anchorZ);

    this.ensurePoolSpace();

    const v = this.createVineInstance({
      chunkIndex: Math.floor(-anchorZ / world.chunkLength),
      archetype: 'STRIKER',
      anchorPos,
      archTargetPos: vehicle.position.clone(),
      initialLength: 6.2,
      matureLength: 9.8,
      baseRadius: 0.185,
      tipRadius: 0.082,
      growthRate: 18.0,
      baseBreakTensionN: 19200,
    });
    v.state = 'GROWING';
  }

  private createVineInstance(opts: {
    chunkIndex: number;
    archetype: VineArchetype;
    anchorPos: THREE.Vector3;
    archTargetPos: THREE.Vector3;
    initialLength: number;
    matureLength: number;
    baseRadius: number;
    tipRadius: number;
    growthRate: number;
    baseBreakTensionN: number;
  }): VineInstance {
    const posX = new Float32Array(NODES_PER_VINE);
    const posY = new Float32Array(NODES_PER_VINE);
    const posZ = new Float32Array(NODES_PER_VINE);
    const prevX = new Float32Array(NODES_PER_VINE);
    const prevY = new Float32Array(NODES_PER_VINE);
    const prevZ = new Float32Array(NODES_PER_VINE);

    const phase = this.rng() * Math.PI * 2;
    const isRoot = opts.archetype === 'ROOT_SNARE';

    for (let i = 0; i < NODES_PER_VINE; i++) {
      const t = i / (NODES_PER_VINE - 1);
      if (isRoot) {
        const dirX = opts.archTargetPos.x - opts.anchorPos.x;
        const dirZ = opts.archTargetPos.z - opts.anchorPos.z;
        const len = Math.hypot(dirX, dirZ) + 1e-4;
        const x = opts.anchorPos.x + (dirX / len) * t * opts.initialLength;
        const y = opts.anchorPos.y + Math.sin(t * Math.PI) * 0.35;
        const z = opts.anchorPos.z + (dirZ / len) * t * opts.initialLength;
        posX[i] = prevX[i] = x;
        posY[i] = prevY[i] = y;
        posZ[i] = prevZ[i] = z;
      } else {
        // Natural graceful botanical drape right from spawn so far-ahead curtains look lush immediately!
        const swayR = Math.sin(t * Math.PI) * 0.38;
        const x = opts.anchorPos.x + Math.cos(t * 3.2 + phase) * swayR;
        const y = opts.anchorPos.y - t * opts.initialLength;
        const z = opts.anchorPos.z + Math.sin(t * 3.2 + phase) * swayR;
        posX[i] = prevX[i] = x;
        posY[i] = prevY[i] = y;
        posZ[i] = prevZ[i] = z;
      }
    }

    const hp = CHASSIS_HARDPOINTS[Math.floor(this.rng() * 5)];
    const inst: VineInstance = {
      id: this.nextVineId++,
      chunkIndex: opts.chunkIndex,
      archetype: opts.archetype,
      state: 'BUDDING',
      anchorPos: opts.anchorPos,
      archTargetPos: opts.archTargetPos,
      posX,
      posY,
      posZ,
      prevX,
      prevY,
      prevZ,
      currentLength: opts.initialLength,
      matureLength: opts.matureLength,
      baseRadius: opts.baseRadius,
      tipRadius: opts.tipRadius,
      growthRate: opts.growthRate,
      nutationPhase: phase,
      latchedNodeIndex: NODES_PER_VINE - 1,
      attachLocalPoint: hp.localPos.clone(),
      hardpointLabel: hp.label,
      strikeTargetLocal: hp.localPos.clone(),
      latchedDuration: 0,
      overgrowth01: 0,
      reelingOffset: 0,
      currentTensionN: 0,
      baseBreakTensionN: opts.baseBreakTensionN,
      strainIntegral: 0,
      rockChafeDamage: 0,
      wrenchEscapeDamage: 0,
      isScrapingRock: false,
      recoilTimer: 0,
    };

    this.vines.push(inst);
    return inst;
  }

  public stepPhysicsSubstep(
    dt: number,
    elapsedTime: number,
    vehicle: VehicleRigidBody,
    world: TrailCorridorWorld,
    nearbyObstacles: WorldObstacle[],
    focusPos?: THREE.Vector3,
    allowVehicleAttack = true
  ): void {
    const activeFocus = focusPos ?? vehicle.position;
    this.lastPlayerZ = activeFocus.z;
    vehicle.netVineForceWorld.set(0, 0, 0);
    vehicle.netVineTensionN = 0;

    let maxOvergrowth = 0;
    let latchedCount = 0;
    for (let i = 0; i < this.vines.length; i++) {
      if (this.vines[i].state === 'LATCHED') latchedCount++;
    }

    const growthScale = this.params.growthSpeedScale;
    const cutterMult = vehicle.currentPreset.passiveCutterMultiplier;

    for (let vi = this.vines.length - 1; vi >= 0; vi--) {
      const v = this.vines[vi];
      const isRoot = v.archetype === 'ROOT_SNARE';

      const dxFocus = activeFocus.x - v.anchorPos.x;
      const dzFocus = activeFocus.z - v.anchorPos.z;
      const focusHorizDistSq = dxFocus * dxFocus + dzFocus * dzFocus;

      const dxCar = vehicle.position.x - v.anchorPos.x;
      const dyCar = vehicle.position.y - v.anchorPos.y;
      const dzCar = vehicle.position.z - v.anchorPos.z;
      const horizDistSq = dxCar * dxCar + dzCar * dzCar;

      if (v.state !== 'LATCHED' && v.state !== 'SNAPPED_RECOIL') {
        if (focusHorizDistSq > 195 * 195) continue;
        if (allowVehicleAttack && dzFocus < -48) continue;
      }

      if (v.state === 'BUDDING') {
        const wakeDist = isRoot ? 95 : 145;
        if (focusHorizDistSq < wakeDist * wakeDist) {
          v.state = 'GROWING';
        }
      }

      if (v.state === 'GROWING') {
        v.currentLength = Math.min(
          v.matureLength,
          v.currentLength + v.growthRate * growthScale * dt
        );

        if (
          allowVehicleAttack &&
          v.archetype === 'STRIKER' &&
          horizDistSq < 36 * 36 &&
          dzCar > -7.0 &&
          v.currentLength > v.matureLength * 0.55
        ) {
          v.state = 'STRIKING';
          const overheadHps = CHASSIS_HARDPOINTS.filter((h) => h.preferOverhead);
          const chosen = overheadHps[v.id % overheadHps.length];
          v.strikeTargetLocal.copy(chosen.localPos);
          v.hardpointLabel = chosen.label;
          if (this.onStrikerWarning) {
            this.onStrikerWarning(
              new THREE.Vector3(
                v.posX[NODES_PER_VINE - 1],
                v.posY[NODES_PER_VINE - 1],
                v.posZ[NODES_PER_VINE - 1]
              )
            );
          }
        }
      } else if (v.state === 'STRIKING') {
        if (!allowVehicleAttack) {
          v.state = 'GROWING';
        } else {
          const strikeMaxReach = Math.max(v.matureLength + 7.5, Math.abs(dyCar) * 1.32);
          v.currentLength = Math.min(
            strikeMaxReach,
            v.currentLength + v.growthRate * 1.7 * growthScale * dt
          );
          if (dzCar < -11.5) {
            v.state = 'GROWING';
          }
        }
      } else if (v.state === 'SNAPPED_RECOIL') {
        v.recoilTimer += dt;
        v.currentLength = Math.max(2.0, v.currentLength - 6.8 * dt);
        if (v.recoilTimer > 5.0 && focusHorizDistSq > 55 * 55) {
          this.vines.splice(vi, 1);
          continue;
        }
      }

      const damping = v.state === 'LATCHED' ? 0.993 : 0.986;
      const windWave = elapsedTime * 2.1 + v.nutationPhase;

      let strikeGoalX = 0,
        strikeGoalY = 0,
        strikeGoalZ = 0;
      if (v.state === 'STRIKING') {
        vehicle.localToWorld(v.strikeTargetLocal, this._tmpA);
        const tipIdx = NODES_PER_VINE - 1;
        const distToTarget = Math.hypot(
          this._tmpA.x - v.posX[tipIdx],
          this._tmpA.y - v.posY[tipIdx],
          this._tmpA.z - v.posZ[tipIdx]
        );
        const leadTime = clamp(distToTarget / 36.0, 0.04, 0.34) * this.params.strikeAggression;
        strikeGoalX = this._tmpA.x + vehicle.linearVelocity.x * leadTime;
        strikeGoalY = this._tmpA.y + vehicle.linearVelocity.y * leadTime * 0.4;
        strikeGoalZ = this._tmpA.z + vehicle.linearVelocity.z * leadTime;
      }

      for (let i = 1; i < NODES_PER_VINE; i++) {
        const x = v.posX[i];
        const y = v.posY[i];
        const z = v.posZ[i];

        const vx = (x - v.prevX[i]) * damping;
        const vy = (y - v.prevY[i]) * damping;
        const vz = (z - v.prevZ[i]) * damping;

        v.prevX[i] = x;
        v.prevY[i] = y;
        v.prevZ[i] = z;

        const tNode = i / (NODES_PER_VINE - 1);
        let ax = Math.sin(windWave + tNode * 3.1) * (isRoot ? 0.4 : 1.75);
        let ay = -9.81;
        let az = Math.cos(windWave * 0.82 + tNode * 2.7) * (isRoot ? 0.4 : 1.45);

        if (isRoot && v.state === 'GROWING') {
          const archLift = Math.sin(tNode * Math.PI) * 18.5;
          const tipSeek = Math.pow(tNode, 1.5);
          ax += (v.archTargetPos.x - x) * 5.5 * tipSeek;
          ay += archLift + (v.archTargetPos.y - y) * 4.5 * tipSeek;
          az += (v.archTargetPos.z - z) * 5.5 * tipSeek;
        } else if (v.state === 'GROWING' && v.currentLength < v.matureLength * 0.96) {
          const curlAmp = tNode * tNode * 6.5;
          ax += Math.cos(elapsedTime * 5.2 + v.nutationPhase + tNode * 4.0) * curlAmp;
          az += Math.sin(elapsedTime * 5.2 + v.nutationPhase + tNode * 4.0) * curlAmp;
        }

        if (v.archetype === 'ARCH_GROWER' && v.state === 'GROWING') {
          const tipWeight = Math.pow(tNode, 2.2);
          ax += (v.archTargetPos.x - x) * 4.8 * tipWeight;
          ay += (v.archTargetPos.y - y) * 5.2 * tipWeight + 5.5 * tipWeight;
          az += (v.archTargetPos.z - z) * 4.8 * tipWeight;
        }

        if (v.state === 'STRIKING' && i >= NODES_PER_VINE - 6) {
          const tipFactor = (i - (NODES_PER_VINE - 6)) / 5;
          const toX = strikeGoalX - x;
          const toY = strikeGoalY - y;
          const toZ = strikeGoalZ - z;
          const d = Math.hypot(toX, toY, toZ) + 1e-4;
          const lungeAccel = 72.0 * this.params.strikeAggression * tipFactor;
          ax += (toX / d) * lungeAccel;
          ay += (toY / d) * lungeAccel + 5.0;
          az += (toZ / d) * lungeAccel;
        }

        const dtSq = dt * dt;
        v.posX[i] = x + vx + ax * dtSq;
        v.posY[i] = y + vy + ay * dtSq;
        v.posZ[i] = z + vz + az * dtSq;
      }

      v.posX[0] = v.anchorPos.x;
      v.posY[0] = v.anchorPos.y;
      v.posZ[0] = v.anchorPos.z;

      // 3. Check Grapple / Contact-Stick / Ground-Root Wheel Snare (disabled in Observer / Peaceful mode)!
      if (
        allowVehicleAttack &&
        (v.state === 'GROWING' || v.state === 'STRIKING') &&
        latchedCount < 3
      ) {
        const stickPadding = 0.28 * this.params.adhesionRadiusScale;
        const strikeCaptureRad = 1.3 * this.params.adhesionRadiusScale;

        let didLatch = false;
        if (v.state === 'STRIKING') {
          vehicle.localToWorld(v.strikeTargetLocal, this._tmpA);
          const tipIdx = NODES_PER_VINE - 1;
          const tipWorld = this._tmpB.set(v.posX[tipIdx], v.posY[tipIdx], v.posZ[tipIdx]);
          if (tipWorld.distanceToSquared(this._tmpA) < strikeCaptureRad * strikeCaptureRad) {
            v.latchedNodeIndex = tipIdx;
            v.attachLocalPoint.copy(v.strikeTargetLocal);
            didLatch = true;
          }
        }

        if (!didLatch && isRoot && horizDistSq < 15 * 15) {
          for (let i = 5; i < NODES_PER_VINE && !didLatch; i++) {
            const nx = v.posX[i];
            const ny = v.posY[i];
            const nz = v.posZ[i];
            for (let wi = 0; wi < 4; wi++) {
              const w = vehicle.wheels[wi];
              const dx = nx - w.hubPositionWorld.x;
              const dy = ny - w.hubPositionWorld.y;
              const dz = nz - w.hubPositionWorld.z;
              const captureR = (w.radius + 0.52) * this.params.adhesionRadiusScale;
              if (dx * dx + dy * dy + dz * dz < captureR * captureR) {
                v.latchedNodeIndex = i;
                const axleHp = CHASSIS_HARDPOINTS[5 + wi];
                v.attachLocalPoint.copy(axleHp.localPos);
                v.hardpointLabel = `${axleHp.label} (ROOT)`;
                didLatch = true;
                break;
              }
            }
          }
        }

        if (!didLatch && horizDistSq < 12 * 12) {
          for (let i = 4; i < NODES_PER_VINE; i++) {
            this._tmpB.set(v.posX[i], v.posY[i], v.posZ[i]);
            if (vehicle.testHullContact(this._tmpB, stickPadding)) {
              v.latchedNodeIndex = i;
              vehicle.projectWorldPointOntoHull(this._tmpB, v.attachLocalPoint);
              if (isRoot) {
                v.hardpointLabel = 'UNDERBODY SKID (ROOT)';
              } else {
                v.hardpointLabel =
                  v.attachLocalPoint.y > 0.72
                    ? 'ROOF CAGE (ADHERED)'
                    : v.attachLocalPoint.z < -0.8
                    ? 'FRONT NOSE (ADHERED)'
                    : 'SIDE POD (ADHERED)';
              }
              didLatch = true;
              break;
            }
          }
        }

        if (didLatch) {
          v.state = 'LATCHED';
          v.latchedDuration = 0;
          v.reelingOffset = 0;
          v.strainIntegral = 0;
          v.rockChafeDamage = 0;
          v.wrenchEscapeDamage = 0;
          latchedCount++;
          this.totalLatchedCount++;
          vehicle.localToWorld(v.attachLocalPoint, this._tmpC);
          if (this.onVineLatched) {
            this.onVineLatched(v.archetype, v.hardpointLabel, this._tmpC.clone());
          }
        }
      }

      // 4. XPBD Distance Constraints
      const activeTotalLength = Math.max(
        1.5,
        v.currentLength - (v.state === 'LATCHED' ? v.reelingOffset : 0)
      );
      const segRestLen = activeTotalLength / (NODES_PER_VINE - 1);
      const solverIters = v.state === 'LATCHED' ? 8 : 4;

      if (v.state === 'LATCHED') {
        vehicle.localToWorld(v.attachLocalPoint, this._tmpC);
      }

      for (let iter = 0; iter < solverIters; iter++) {
        v.posX[0] = v.anchorPos.x;
        v.posY[0] = v.anchorPos.y;
        v.posZ[0] = v.anchorPos.z;

        if (v.state === 'LATCHED') {
          v.posX[v.latchedNodeIndex] = this._tmpC.x;
          v.posY[v.latchedNodeIndex] = this._tmpC.y;
          v.posZ[v.latchedNodeIndex] = this._tmpC.z;
        }

        for (let i = 0; i < NODES_PER_VINE - 1; i++) {
          const j = i + 1;
          const dx = v.posX[j] - v.posX[i];
          const dy = v.posY[j] - v.posY[i];
          const dz = v.posZ[j] - v.posZ[i];
          const dist = Math.hypot(dx, dy, dz) + 1e-6;
          const diff = (dist - segRestLen) / dist;

          const w0 =
            i === 0 || (v.state === 'LATCHED' && i === v.latchedNodeIndex) ? 0.0 : 0.5;
          const w1 = v.state === 'LATCHED' && j === v.latchedNodeIndex ? 0.0 : 0.5;
          const wSum = w0 + w1;
          if (wSum > 0) {
            const corrX = dx * diff;
            const corrY = dy * diff;
            const corrZ = dz * diff;
            v.posX[i] += corrX * (w0 / wSum);
            v.posY[i] += corrY * (w0 / wSum);
            v.posZ[i] += corrZ * (w0 / wSum);
            v.posX[j] -= corrX * (w1 / wSum);
            v.posY[j] -= corrY * (w1 / wSum);
            v.posZ[j] -= corrZ * (w1 / wSum);
          }
        }
      }

      for (let i = 4; i < NODES_PER_VINE; i += 2) {
        if (v.state === 'LATCHED' && i === v.latchedNodeIndex) continue;
        const minClearance = isRoot ? 0.08 : 0.14;
        const gy = world.sampleHeightRaw(v.posX[i], v.posZ[i]).height + minClearance;
        if (v.posY[i] < gy) {
          v.posY[i] = gy;
        }
      }

      // 5. Two-Way Tether Physics + Escape Counterplay
      v.currentTensionN = 0;
      if (v.state === 'LATCHED') {
        v.latchedDuration += dt;
        v.overgrowth01 = clamp(v.latchedDuration / 6.0, 0, 1);
        if (v.overgrowth01 > maxOvergrowth) maxOvergrowth = v.overgrowth01;

        const reelSpeed = (isRoot ? 1.1 : 1.45) * growthScale;
        v.reelingOffset = Math.min(v.currentLength * 0.42, v.reelingOffset + reelSpeed * dt);

        vehicle.localToWorld(v.attachLocalPoint, this._tmpC);
        const latchedRestSpan = segRestLen * v.latchedNodeIndex;
        const toAnchorX = v.anchorPos.x - this._tmpC.x;
        const toAnchorY = v.anchorPos.y - this._tmpC.y;
        const toAnchorZ = v.anchorPos.z - this._tmpC.z;
        const chordDist = Math.hypot(toAnchorX, toAnchorY, toAnchorZ) + 1e-5;
        const stretch = chordDist - latchedRestSpan;

        const prevIdx = Math.max(0, v.latchedNodeIndex - 1);
        let dirX = v.posX[prevIdx] - this._tmpC.x;
        let dirY = v.posY[prevIdx] - this._tmpC.y;
        let dirZ = v.posZ[prevIdx] - this._tmpC.z;
        const segLen = Math.hypot(dirX, dirY, dirZ);
        if (segLen > 1e-4) {
          dirX = dirX * 0.55 + (toAnchorX / chordDist) * 0.45 * segLen;
          dirY = dirY * 0.55 + (toAnchorY / chordDist) * 0.45 * segLen;
          dirZ = dirZ * 0.55 + (toAnchorZ / chordDist) * 0.45 * segLen;
          const n = Math.hypot(dirX, dirY, dirZ) + 1e-5;
          dirX /= n;
          dirY /= n;
          dirZ /= n;
        } else {
          dirX = toAnchorX / chordDist;
          dirY = toAnchorY / chordDist;
          dirZ = toAnchorZ / chordDist;
        }

        let tensionN = 0;
        let effectiveTensionForSnap = 0;

        if (stretch > -0.25) {
          const effStretch = Math.max(0, stretch + 0.25);
          const kLinear = isRoot ? 3300 : 2850;
          const kNonLinear = isRoot ? 260 : 240;
          const separatingSpeed = -(
            vehicle.linearVelocity.x * dirX +
            vehicle.linearVelocity.y * dirY +
            vehicle.linearVelocity.z * dirZ
          );
          const damperForce = Math.max(0, separatingSpeed * 420);
          effectiveTensionForSnap =
            kLinear * effStretch + kNonLinear * effStretch * effStretch + damperForce;

          const dragResistScale = isRoot
            ? 1.0 - vehicle.currentPreset.rootDragResistance * 0.48
            : 1.0 - vehicle.currentPreset.rootDragResistance * 0.2;
          tensionN = effectiveTensionForSnap * dragResistScale;
        }

        v.currentTensionN = effectiveTensionForSnap;
        const forceWorld = this._tmpA.set(dirX * tensionN, dirY * tensionN, dirZ * tensionN);
        if (!isRoot) {
          const remainingLiftBudget = Math.max(0, 5800 - vehicle.netVineForceWorld.y);
          forceWorld.y = Math.min(forceWorld.y, Math.min(3600, remainingLiftBudget));
        } else {
          forceWorld.y = Math.max(forceWorld.y, -3200);
        }

        vehicle.applyExternalForceAtLocalPoint(v.attachLocalPoint, forceWorld);
        vehicle.netVineForceWorld.add(forceWorld);
        vehicle.netVineTensionN += effectiveTensionForSnap;

        let maxWheelSlip = 0;
        for (const w of vehicle.wheels) {
          if (w.combinedSlip > maxWheelSlip) maxWheelSlip = w.combinedSlip;
        }
        const burnoutSaw = maxWheelSlip * 0.28;
        const wrenchRate =
          (vehicle.wrenchIntensity * (isRoot ? 0.56 : 0.34) + burnoutSaw) * cutterMult;
        if (wrenchRate > 0.12) {
          v.wrenchEscapeDamage += wrenchRate * dt;
        } else {
          v.wrenchEscapeDamage = Math.max(0, v.wrenchEscapeDamage - 0.15 * dt);
        }

        const maxTensionN =
          v.baseBreakTensionN *
          this.params.tensileStrengthScale *
          (1.0 + v.overgrowth01 * 0.32);
        const maxGeometricReach = Math.max(22.0, v.matureLength * 2.15);

        v.isScrapingRock = false;
        if (tensionN > 1600 && nearbyObstacles.length > 0) {
          for (let s = 1; s < v.latchedNodeIndex; s++) {
            const ax = v.posX[s];
            const ay = v.posY[s];
            const az = v.posZ[s];
            const bx = v.posX[s + 1];
            const by = v.posY[s + 1];
            const bz = v.posZ[s + 1];

            const abx = bx - ax;
            const abz = bz - az;
            const abLenSq = abx * abx + abz * abz + 1e-6;

            for (const obs of nearbyObstacles) {
              if (!obs.canShearVine) continue;
              const uSeg = clamp(
                ((obs.position.x - ax) * abx + (obs.position.z - az) * abz) / abLenSq,
                0,
                1
              );
              const closeY = lerp(ay, by, uSeg);
              if (closeY > obs.position.y + obs.height + 0.45) continue;

              const closeX = ax + abx * uSeg;
              const closeZ = az + abz * uSeg;
              const ox = closeX - obs.position.x;
              const oz = closeZ - obs.position.z;
              const rCheck = obs.radius + 0.36;

              if (ox * ox + oz * oz < rCheck * rCheck) {
                v.isScrapingRock = true;
                const sharpness = (obs.kind === 'shear_rock' ? 4.8 : 3.0) * cutterMult;
                v.rockChafeDamage += sharpness * (1 + tensionN / 7000) * dt;
                const d = Math.hypot(ox, oz) + 1e-4;
                const pushOut = rCheck - d;
                v.posX[s] += (ox / d) * pushOut;
                v.posZ[s] += (oz / d) * pushOut;
              }
            }
          }
        }

        const strainRatio = effectiveTensionForSnap / maxTensionN;
        if (strainRatio > 0.85) {
          v.strainIntegral += (strainRatio - 0.82) * 3.4 * dt;
        } else {
          v.strainIntegral = Math.max(0, v.strainIntegral - 0.8 * dt);
        }

        if (v.rockChafeDamage >= 1.0) {
          this.severVine(v, 'ROCK_SHEAR', effectiveTensionForSnap / 1000, vehicle);
        } else if (v.wrenchEscapeDamage >= 1.0) {
          this.severVine(v, 'WRENCH_ESCAPE', effectiveTensionForSnap / 1000, vehicle);
        } else if (
          strainRatio >= 1.0 ||
          v.strainIntegral >= 0.4 ||
          chordDist > maxGeometricReach
        ) {
          this.severVine(
            v,
            'TENSION_SNAP',
            Math.max(effectiveTensionForSnap, maxTensionN) / 1000,
            vehicle
          );
        }
      }
    }

    for (let i = 0; i < this.chassisTendrilMeshes.length; i++) {
      const item = this.chassisTendrilMeshes[i];
      const target = latchedCount > 0 ? clamp(maxOvergrowth * 1.15 - i * 0.12, 0, 1) : 0;
      const cur = item.mesh.visible ? item.mesh.scale.x : 0;
      const next = lerp(cur, target, clamp((target > cur ? 3.5 : 4.8) * dt, 0, 1));
      if (next < 0.02) {
        item.mesh.visible = false;
      } else {
        item.mesh.visible = true;
        item.mesh.scale.setScalar(next);
      }
    }
  }

  private severVine(
    v: VineInstance,
    reason: SeverReason,
    peakForceKN: number,
    vehicle: VehicleRigidBody
  ): void {
    const breakNode = Math.max(3, v.latchedNodeIndex - 3);
    const breakWorld = new THREE.Vector3(
      v.posX[breakNode],
      v.posY[breakNode],
      v.posZ[breakNode]
    );

    const wasRoot = v.archetype === 'ROOT_SNARE';
    v.state = 'SNAPPED_RECOIL';
    v.recoilTimer = 0;
    v.currentTensionN = 0;
    v.isScrapingRock = false;

    for (let i = Math.floor(NODES_PER_VINE * 0.4); i < NODES_PER_VINE; i++) {
      v.prevY[i] -= wasRoot ? -0.1 : 0.16;
    }

    this.vinesSeveredCount++;
    if (wasRoot) this.rootsEscapedCount++;
    if (reason === 'ROCK_SHEAR') {
      this.vinesShearedCount++;
      vehicle.boostCharge = Math.min(1.0, vehicle.boostCharge + 0.25);
      vehicle.shredCharge = Math.min(1.0, vehicle.shredCharge + 0.3);
    }

    if (this.onVineSevered) {
      this.onVineSevered(reason, v.archetype, peakForceKN, breakWorld);
    }
  }

  public getLatchedTelemetry(out: ActiveLatchedTelemetry[] = []): ActiveLatchedTelemetry[] {
    out.length = 0;
    for (const v of this.vines) {
      if (v.state !== 'LATCHED') continue;
      const maxT =
        v.baseBreakTensionN *
        this.params.tensileStrengthScale *
        (1.0 + v.overgrowth01 * 0.32);
      out.push({
        vineId: v.id,
        archetype: v.archetype,
        hardpointLabel: v.hardpointLabel,
        localAttach: v.attachLocalPoint,
        tensionN: v.currentTensionN,
        maxTensionN: maxT,
        strain01: clamp(v.currentTensionN / maxT, 0, 1),
        escapeProgress01: clamp(Math.max(v.wrenchEscapeDamage, v.rockChafeDamage), 0, 1),
        overgrowth01: v.overgrowth01,
        isScrapingRock: v.isScrapingRock,
      });
    }
    return out;
  }

  public updateVisualGeometry(): void {
    const posArr = this.tubePosAttr.array as Float32Array;
    const normArr = this.tubeNormAttr.array as Float32Array;
    const uvArr = this.tubeUvAttr.array as Float32Array;
    const colArr = this.tubeColAttr.array as Float32Array;

    let activeVineSlot = 0;
    let leafSlot = 0;
    let clawSlot = 0;
    const maxLeaves = this.leafInstanced.instanceMatrix.count;

    for (let vi = 0; vi < this.vines.length && activeVineSlot < MAX_POOLED_VINES; vi++) {
      const v = this.vines[vi];
      const isRoot = v.archetype === 'ROOT_SNARE';
      const baseVert = activeVineSlot * VERTS_PER_VINE;

      const maxT =
        v.baseBreakTensionN *
        this.params.tensileStrengthScale *
        (1.0 + v.overgrowth01 * 0.32);
      const strain =
        v.state === 'LATCHED'
          ? clamp(Math.max(v.currentTensionN / maxT, v.wrenchEscapeDamage * 0.9), 0, 1)
          : 0;
      const growthMaturity = clamp(v.currentLength / v.matureLength, 0.25, 1.0);
      const thicknessGrow = lerp(0.82, 1.0 + v.overgrowth01 * 0.22, growthMaturity);

      this._normal.set(1, 0, 0);

      for (let i = 0; i < NODES_PER_VINE; i++) {
        const t = i / (NODES_PER_VINE - 1);
        const cx = v.posX[i];
        const cy = v.posY[i];
        const cz = v.posZ[i];

        if (i === 0) {
          this._tangent.set(v.posX[1] - cx, v.posY[1] - cy, v.posZ[1] - cz);
        } else if (i === NODES_PER_VINE - 1) {
          this._tangent.set(cx - v.posX[i - 1], cy - v.posY[i - 1], cz - v.posZ[i - 1]);
        } else {
          this._tangent.set(
            v.posX[i + 1] - v.posX[i - 1],
            v.posY[i + 1] - v.posY[i - 1],
            v.posZ[i + 1] - v.posZ[i - 1]
          );
        }
        if (this._tangent.lengthSq() < 1e-8) this._tangent.set(0, -1, 0);
        else this._tangent.normalize();

        const dotTN = this._normal.dot(this._tangent);
        this._normal.addScaledVector(this._tangent, -dotTN);
        if (this._normal.lengthSq() < 1e-5) {
          this._normal.set(Math.abs(this._tangent.x) < 0.8 ? 1 : 0, 0, 1);
          this._normal.addScaledVector(this._tangent, -this._normal.dot(this._tangent));
        }
        this._normal.normalize();
        this._binormal.crossVectors(this._tangent, this._normal).normalize();

        const nodeSwell = 1.0 + Math.sin(t * Math.PI * 10) * 0.1;
        const necking = 1.0 - strain * 0.22 * Math.sin(t * Math.PI);
        const radius =
          lerp(v.baseRadius, v.tipRadius, Math.pow(t, 0.72)) *
          thicknessGrow *
          nodeSwell *
          necking;

        if (isRoot) {
          this._color.setRGB(
            lerp(0.38, 0.82, Math.pow(t, 1.6)),
            lerp(0.25, 0.46, Math.pow(t, 1.5)),
            lerp(0.15, 0.16, t)
          );
        } else if (v.archetype === 'STRIKER') {
          // Vivid backlit olive-emerald stem transitioning to glowing amber-crimson striker tip!
          this._color.setRGB(
            lerp(0.46, 0.98, Math.pow(t, 1.6)),
            lerp(0.68, 0.46, Math.pow(t, 1.5)),
            lerp(0.26, 0.14, t)
          );
        } else if (v.archetype === 'CURTAIN') {
          // Rich sunlit chartreuse-emerald hanging curtain with golden nectar tip!
          this._color.setRGB(
            lerp(0.40, 0.86, Math.pow(t, 1.4)),
            lerp(0.72, 0.92, t),
            lerp(0.26, 0.28, t)
          );
        } else {
          this._color.setRGB(lerp(0.48, 0.72, t), lerp(0.66, 0.84, t), lerp(0.28, 0.32, t));
        }

        if (strain > 0.32 || v.isScrapingRock) {
          const hotZone = Math.sin(t * Math.PI) * smoothstep(0.32, 0.98, strain);
          const chafeFlash = v.isScrapingRock ? 0.55 : 0;
          const warn = clamp(hotZone + chafeFlash, 0, 1);
          this._color.r = lerp(this._color.r, 1.0, warn);
          this._color.g = lerp(this._color.g, v.isScrapingRock ? 0.82 : 0.34, warn);
          this._color.b = lerp(this._color.b, 0.12, warn);
        }

        const vUv = t * v.currentLength * 0.65;
        const ringBase = (baseVert + i * VERTS_PER_RING) * 3;
        const uvBase = (baseVert + i * VERTS_PER_RING) * 2;

        for (let s = 0; s < VERTS_PER_RING; s++) {
          const uFrac = s / RADIAL_SIDES;
          const angle = uFrac * Math.PI * 2;
          const braidMod = 1.0 + (isRoot ? 0.2 : 0.15) * Math.cos(angle * 3.0 + t * 18.0);
          const r = radius * braidMod;

          const cosA = Math.cos(angle);
          const sinA = Math.sin(angle);

          const nx = this._normal.x * cosA + this._binormal.x * sinA;
          const ny = this._normal.y * cosA + this._binormal.y * sinA;
          const nz = this._normal.z * cosA + this._binormal.z * sinA;

          const pIdx = ringBase + s * 3;
          posArr[pIdx] = cx + nx * r;
          posArr[pIdx + 1] = cy + ny * r;
          posArr[pIdx + 2] = cz + nz * r;

          normArr[pIdx] = nx;
          normArr[pIdx + 1] = ny;
          normArr[pIdx + 2] = nz;

          colArr[pIdx] = this._color.r;
          colArr[pIdx + 1] = this._color.g;
          colArr[pIdx + 2] = this._color.b;

          const uIdx = uvBase + s * 2;
          uvArr[uIdx] = uFrac;
          uvArr[uIdx + 1] = vUv;
        }

        // Lush cascading foliage garlands along EVERY hanging vine node!
        if (!isRoot && i >= 1 && i <= 15 && leafSlot < maxLeaves) {
          const sproutScale = clamp((growthMaturity - t * 0.2) * 1.65, 0, 1.0);
          if (sproutScale > 0.12) {
            this._dummy.position.set(cx, cy, cz);
            const sideSign = i % 2 === 0 ? 1 : -1;
            this._dummy.lookAt(
              cx + this._normal.x * sideSign + this._binormal.x * 0.35,
              cy - 0.32,
              cz + this._normal.z * sideSign + this._binormal.z * 0.35
            );
            this._dummy.rotateY(sideSign * 0.65);
            this._dummy.scale.setScalar(sproutScale * lerp(1.32, 0.78, t));
            this._dummy.updateMatrix();
            this.leafInstanced.setMatrixAt(leafSlot++, this._dummy.matrix);
          }
        }

        // Predatory glowing calyx blossoms on ALL vine tips (including hanging curtains!) so tips stand out!
        if (i === NODES_PER_VINE - 1 && clawSlot < MAX_POOLED_VINES) {
          this._dummy.position.set(cx, cy, cz);
          this._tmpA.set(cx + this._tangent.x, cy + this._tangent.y, cz + this._tangent.z);
          this._dummy.lookAt(this._tmpA);
          const jawOpen =
            v.state === 'STRIKING'
              ? 1.7
              : v.archetype === 'CURTAIN'
              ? 0.92
              : v.state === 'LATCHED'
              ? 0.95
              : 1.25;
          this._dummy.scale.set(jawOpen, jawOpen, 1.25);
          this._dummy.updateMatrix();
          this.calyxJawInstanced.setMatrixAt(clawSlot++, this._dummy.matrix);
        }
      }

      activeVineSlot++;
    }

    this.tubePosAttr.needsUpdate = true;
    this.tubeNormAttr.needsUpdate = true;
    this.tubeUvAttr.needsUpdate = true;
    this.tubeColAttr.needsUpdate = true;
    this.tubeGeo.setDrawRange(0, activeVineSlot * INDICES_PER_VINE);

    this.leafInstanced.count = leafSlot;
    if (leafSlot > 0) this.leafInstanced.instanceMatrix.needsUpdate = true;

    this.calyxJawInstanced.count = clawSlot;
    if (clawSlot > 0) this.calyxJawInstanced.instanceMatrix.needsUpdate = true;
  }
}

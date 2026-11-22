import * as THREE from 'three';
import { clamp, lerp } from '../math/noise';
import { ProceduralTextureLibrary } from '../graphics/proceduralTextures';
import { CrisscrossRootBridge, WorldObstacle } from './trailCorridor';

export interface DeadwoodBranchDef {
  origin: THREE.Vector3;
  direction: THREE.Vector3;
  birth: number;
  depth: number;
  baseRadius: number;
  speed: number;
  thicken: number;
  frequency: number;
  phase: number;
  curl: number;
  lift: number;
  shape: 'arch' | 'hook' | 'twist';
  archSpan: number;
  archHeight: number;
  bendRadius: number;
  parent: number;
  attachDist: number;
  broken: boolean;
  mesh: THREE.Mesh;
  burlMesh: THREE.Mesh;
  obstacleSlots: WorldObstacle[];
}

export interface DeadwoodDirtClod {
  mesh: THREE.Mesh;
  angle: number;
  speed: number;
  baseSize: number;
}

export interface DeadwoodRootEruption {
  x: number;
  y: number;
  z: number;
  warningDuration: number;
  age: number;
  renderedAge: number;
  active: boolean;
  eruptedBurstDone: boolean;
  group: THREE.Group;
  fissures: THREE.Mesh[];
  clods: DeadwoodDirtClod[];
  branches: DeadwoodBranchDef[];
}

export interface DeadwoodGroundHeave {
  x: number;
  baseY: number;
  z: number;
  halfWidth: number;
  halfLength: number;
  age: number;
  active: boolean;
  moundMesh: THREE.Mesh;
  basePositions: Float32Array;
  fissureMesh: THREE.Mesh;
  buriedRootMesh: THREE.Mesh;
  bridge: CrisscrossRootBridge;
}

export interface DeadwoodTighteningArch {
  centerX: number;
  baseY: number;
  z: number;
  age: number;
  lastGeoAge: number;
  active: boolean;
  mesh: THREE.Mesh;
  leftPillarObs: WorldObstacle;
  rightPillarObs: WorldObstacle;
}

export interface DeadwoodUprootedSnag {
  baseX: number;
  baseY: number;
  z: number;
  side: number;
  length: number;
  kind: 'under_fork' | 'road_block';
  age: number;
  active: boolean;
  pivot: THREE.Group;
  clods: DeadwoodDirtClod[];
  trunkObstacle: WorldObstacle;
}

export interface BarkSplinterParticle {
  mesh: THREE.Mesh;
  vel: THREE.Vector3;
  age: number;
}

export interface DeadwoodChunkData {
  eruptions: DeadwoodRootEruption[];
  heaves: DeadwoodGroundHeave[];
  arches: DeadwoodTighteningArch[];
  uprootedSnags: DeadwoodUprootedSnag[];
  splinters: BarkSplinterParticle[];
}

const TUBE_RINGS = 36;
const TUBE_SIDES = 7;

export class DeadwoodBuilder {
  public readonly deadBarkMat: THREE.MeshStandardMaterial;
  public readonly liveRootMat: THREE.MeshStandardMaterial;
  public readonly fissureDarkMat: THREE.MeshStandardMaterial;
  public readonly deadEarthMat: THREE.MeshStandardMaterial;
  public readonly slateStoneMat: THREE.MeshStandardMaterial;
  public readonly markerReflectorMat: THREE.MeshStandardMaterial;

  private readonly burlGeo = new THREE.IcosahedronGeometry(1, 1);
  private readonly clodGeo = new THREE.IcosahedronGeometry(1, 0);
  private readonly splinterGeo = new THREE.BoxGeometry(0.18, 0.1, 0.58);

  private _va = new THREE.Vector3();
  private _vb = new THREE.Vector3();
  private _tangent = new THREE.Vector3();
  private _normal = new THREE.Vector3();
  private _binormal = new THREE.Vector3();
  private _up = new THREE.Vector3(0, 1, 0);
  private _altAxis = new THREE.Vector3(1, 0, 0);

  constructor(textures: ProceduralTextureLibrary) {
    // Charred & bleached dead snag bark (#42463c / silver-ash tint)
    this.deadBarkMat = new THREE.MeshStandardMaterial({
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      normalScale: new THREE.Vector2(1.4, 1.4),
      color: new THREE.Color(0x585c50),
      roughness: 0.94,
      metalness: 0.01,
    });

    // Living subterranean gnarled root wood (#4c4b3c warm dark rootwood)
    this.liveRootMat = new THREE.MeshStandardMaterial({
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      normalScale: new THREE.Vector2(1.65, 1.65),
      color: new THREE.Color(0x594d39),
      roughness: 0.86,
      metalness: 0.02,
    });

    this.fissureDarkMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(0x1f241f),
      roughness: 0.98,
    });

    this.deadEarthMat = new THREE.MeshStandardMaterial({
      map: textures.terrainDetail.map,
      normalMap: textures.terrainDetail.normalMap,
      color: new THREE.Color(0x55594c),
      roughness: 0.95,
    });

    this.slateStoneMat = new THREE.MeshStandardMaterial({
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      color: new THREE.Color(0x777b70),
      roughness: 0.9,
    });

    this.markerReflectorMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(0xbc9566),
      roughness: 0.55,
      metalness: 0.25,
    });
  }

  private createDynamicRootTubeGeo(): THREE.BufferGeometry {
    const geo = new THREE.BufferGeometry();
    const vertCount = (TUBE_RINGS + 1) * (TUBE_SIDES + 1);
    const pos = new Float32Array(vertCount * 3);
    const uvs = new Float32Array(vertCount * 2);
    const indices: number[] = [];

    for (let i = 0; i <= TUBE_RINGS; i++) {
      for (let j = 0; j <= TUBE_SIDES; j++) {
        const idx = (i * (TUBE_SIDES + 1) + j) * 2;
        uvs[idx] = (j / TUBE_SIDES) * 2.0;
        uvs[idx + 1] = (i / TUBE_RINGS) * 6.0;
      }
    }

    for (let i = 0; i < TUBE_RINGS; i++) {
      for (let j = 0; j < TUBE_SIDES; j++) {
        const a = i * (TUBE_SIDES + 1) + j;
        const b = a + TUBE_SIDES + 1;
        indices.push(a, b, a + 1, a + 1, b, b + 1);
      }
    }

    geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    geo.setAttribute('uv', new THREE.BufferAttribute(uvs, 2));
    geo.setIndex(indices);
    return geo;
  }

  /**
   * Material-space curve evaluation (inspired by Project-Deadwood):
   * Growing tips reveal NEW curve geometry ahead rather than stretching existing bark!
   */
  private evalBranchLength(b: DeadwoodBranchDef, age: number): number {
    return Math.max(0, age - b.birth) * b.speed;
  }

  private evalBranchRadius(b: DeadwoodBranchDef, age: number, s = 0): number {
    const len = this.evalBranchLength(b, age);
    if (len <= 0) return 0;
    const localAge = Math.max(0, age - b.birth - s / b.speed);
    const taper = Math.min(1, (len - s) / 1.65 + 0.04);
    return (b.baseRadius + Math.sqrt(localAge) * b.thicken) * taper;
  }

  private evalBranchPoint(b: DeadwoodBranchDef, s: number, groundBaseY: number, out: THREE.Vector3): THREE.Vector3 {
    const bend = (s * s) / (s + 5.0);
    const isArch = b.shape === 'arch';
    const isHook = b.shape === 'hook';
    const angle = s / b.bendRadius;
    const forward = isHook ? Math.sin(angle) * b.bendRadius : s;

    const rise = isArch
      ? Math.sin(clamp(s / b.archSpan, 0, 1.15) * Math.PI) * b.archHeight
      : isHook
      ? (1 - Math.cos(angle)) * b.bendRadius * 0.45
      : 0;

    const px =
      b.origin.x +
      b.direction.x * forward +
      Math.sin(s * b.frequency + b.phase) * bend * b.curl;
    const py = Math.max(
      groundBaseY - 0.65,
      b.origin.y +
        b.direction.y * s * (isArch || isHook ? 0.22 : 0.65) +
        Math.sin(s * b.frequency * 0.8) * bend * b.lift +
        rise
    );
    const pz =
      b.origin.z +
      b.direction.z * forward +
      (Math.cos(s * b.frequency + b.phase) - Math.cos(b.phase)) * bend * b.curl;

    return out.set(px, py, pz);
  }

  private appendTaperedBeam(
    positions: number[],
    normals: number[],
    uvs: number[],
    indices: number[],
    a: THREE.Vector3,
    b: THREE.Vector3,
    rBottom: number,
    rTop: number,
    radialSegs = 6
  ): void {
    const dir = new THREE.Vector3().subVectors(b, a);
    const len = dir.length();
    if (len < 0.05) return;
    dir.normalize();

    const ref = Math.abs(dir.y) > 0.92 ? new THREE.Vector3(1, 0, 0) : new THREE.Vector3(0, 1, 0);
    const right = new THREE.Vector3().crossVectors(dir, ref).normalize();
    const fwd = new THREE.Vector3().crossVectors(right, dir).normalize();

    const baseVert = positions.length / 3;
    const rings = 3;
    for (let ir = 0; ir <= rings; ir++) {
      const t = ir / rings;
      const cx = lerp(a.x, b.x, t);
      const cy = lerp(a.y, b.y, t);
      const cz = lerp(a.z, b.z, t);
      const rad = lerp(rBottom, rTop, t);

      for (let is = 0; is <= radialSegs; is++) {
        const ang = (is / radialSegs) * Math.PI * 2;
        const cosA = Math.cos(ang);
        const sinA = Math.sin(ang);
        const flute = 1.0 + 0.12 * Math.sin(ang * 3.0 + t * 5.0);
        const nx = right.x * cosA + fwd.x * sinA;
        const ny = right.y * cosA + fwd.y * sinA;
        const nz = right.z * cosA + fwd.z * sinA;

        positions.push(cx + nx * rad * flute, cy + ny * rad * flute, cz + nz * rad * flute);
        normals.push(nx, ny, nz);
        uvs.push((is / radialSegs) * 2.0, t * len * 0.35);
      }
    }

    const stride = radialSegs + 1;
    for (let ir = 0; ir < rings; ir++) {
      for (let is = 0; is < radialSegs; is++) {
        const i0 = baseVert + ir * stride + is;
        const i1 = i0 + 1;
        const i2 = i0 + stride;
        const i3 = i2 + 1;
        indices.push(i0, i2, i1, i1, i2, i3);
      }
    }
  }

  public populateDeadwoodChunk(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    chunkLength: number,
    corridorHalfSpan: number,
    rng: () => number,
    sampleHeight: (x: number, z: number) => number,
    getCenterAndHalfWidth: (z: number) => { centerX: number; halfWidth: number },
    obstacles: WorldObstacle[],
    rootBridges: CrisscrossRootBridge[],
    allocObstacleId: () => number,
    allocBridgeId: () => number
  ): DeadwoodChunkData {
    const eruptions: DeadwoodRootEruption[] = [];
    const heaves: DeadwoodGroundHeave[] = [];
    const arches: DeadwoodTighteningArch[] = [];
    const uprootedSnags: DeadwoodUprootedSnag[] = [];
    const splinters: BarkSplinterParticle[] = [];

    // 1. BATCHED FOREST OF BLEACHED & CHARRED DEAD SNAGS + STUMPS (1 draw call per chunk!)
    const snagPos: number[] = [];
    const snagNorm: number[] = [];
    const snagUv: number[] = [];
    const snagIdx: number[] = [];

    const snagCount = 46;
    for (let i = 0; i < snagCount; i++) {
      const z = zStart - rng() * chunkLength;
      const { centerX, halfWidth } = getCenterAndHalfWidth(z);
      const side = i % 2 === 0 ? -1 : 1;

      // Place dead snags from the road shoulders all the way across the misty timberland!
      const offsetMag =
        i < 14
          ? lerp(halfWidth * 0.62, halfWidth + 14.0, rng())
          : lerp(halfWidth + 12.0, corridorHalfSpan - 12.0, rng());
      const x = centerX + side * offsetMag;
      if (z > -26 && Math.abs(x - centerX) < 28) continue;

      const gy = sampleHeight(x, z);
      const h = lerp(11.0, 29.0, rng());
      const leanX = (rng() - 0.5) * 5.2;
      const leanZ = (rng() - 0.5) * 4.0;
      const rBase = lerp(0.58, 1.25, rng());
      const rTip = lerp(0.11, 0.24, rng());

      const basePt = new THREE.Vector3(x, gy - 0.35, z);
      const tipPt = new THREE.Vector3(x + leanX, gy + h, z + leanZ);
      this.appendTaperedBeam(snagPos, snagNorm, snagUv, snagIdx, basePt, tipPt, rBase, rTip, 7);

      // 3-4 stark, bare leafless skeletal limbs jutting from the dead trunk
      const limbCount = 3 + Math.floor(rng() * 2);
      for (let b = 0; b < limbCount; b++) {
        const tAttach = 0.38 + b * 0.16 + rng() * 0.08;
        if (tAttach > 0.94) continue;
        const attachPt = basePt.clone().lerp(tipPt, tAttach);
        const limbYaw = rng() * Math.PI * 2;
        const limbLen = lerp(3.2, 7.5, rng());
        const limbEnd = new THREE.Vector3(
          attachPt.x + Math.cos(limbYaw) * limbLen,
          attachPt.y + lerp(1.5, 4.4, rng()),
          attachPt.z + Math.sin(limbYaw) * limbLen
        );
        this.appendTaperedBeam(
          snagPos,
          snagNorm,
          snagUv,
          snagIdx,
          attachPt,
          limbEnd,
          rBase * 0.32,
          0.04,
          5
        );
      }

      // Exposed dead root flare claws at base
      for (let rf = 0; rf < 3; rf++) {
        const fa = (rf / 3) * Math.PI * 2 + rng() * 0.6;
        const fStart = new THREE.Vector3(x, gy + 1.1, z);
        const fEnd = new THREE.Vector3(
          x + Math.cos(fa) * (rBase * 2.4),
          gy - 0.3,
          z + Math.sin(fa) * (rBase * 2.4)
        );
        this.appendTaperedBeam(snagPos, snagNorm, snagUv, snagIdx, fStart, fEnd, rBase * 0.42, 0.12, 5);
      }

      if (Math.abs(x - centerX) < halfWidth + 16.0) {
        obstacles.push({
          id: allocObstacleId(),
          chunkIndex,
          kind: 'titan_tree',
          position: new THREE.Vector3(x, gy, z),
          radius: rBase * 0.95,
          height: h,
          canShearVine: true,
        });
      }
    }

    if (snagPos.length > 0) {
      const batchGeo = new THREE.BufferGeometry();
      batchGeo.setAttribute('position', new THREE.Float32BufferAttribute(snagPos, 3));
      batchGeo.setAttribute('normal', new THREE.Float32BufferAttribute(snagNorm, 3));
      batchGeo.setAttribute('uv', new THREE.Float32BufferAttribute(snagUv, 2));
      batchGeo.setIndex(snagIdx);
      const batchMesh = new THREE.Mesh(batchGeo, this.deadBarkMat);
      batchMesh.castShadow = true;
      batchMesh.receiveShadow = true;
      batchMesh.userData.uniqueGeo = true;
      chunkGroup.add(batchMesh);
    }

    // 2. WEATHERED STONE ROADSIDE MARKER POSTS (Authentic Project-Deadwood Verge Posts!)
    for (let m = 0; m < 2; m++) {
      const mz = zStart - (0.24 + m * 0.5) * chunkLength;
      if (mz > -20) continue;
      const { centerX, halfWidth } = getCenterAndHalfWidth(mz);
      for (const side of [-1, 1]) {
        const mx = centerX + side * (halfWidth * 0.56);
        const my = sampleHeight(mx, mz);
        const post = new THREE.Mesh(new THREE.BoxGeometry(0.36, 1.9, 0.36), this.slateStoneMat);
        post.position.set(mx, my + 0.9, mz);
        post.castShadow = true;
        post.userData.uniqueGeo = true;
        chunkGroup.add(post);

        const cap = new THREE.Mesh(new THREE.BoxGeometry(0.4, 0.24, 0.4), this.markerReflectorMat);
        cap.position.set(mx, my + 1.62, mz);
        cap.userData.uniqueGeo = true;
        chunkGroup.add(cap);
      }
    }

    if (chunkIndex < 0) {
      return { eruptions, heaves, arches, uprootedSnags, splinters };
    }

    // 3. LIVE SUBTERRANEAN ERUPTING & GROWING ROOTS ON THE ROAD (5 multi-branch clusters per chunk!)
    const eruptionCount = 5;
    for (let e = 0; e < eruptionCount; e++) {
      const ez = zStart - (0.12 + e * 0.18 + rng() * 0.05) * chunkLength;
      if (ez > -30) continue;
      const { centerX, halfWidth } = getCenterAndHalfWidth(ez);
      // Place right across the active driving path so roots grow onto & across the road!
      const ex = centerX + (rng() - 0.5) * (halfWidth * 0.92);
      const ey = sampleHeight(ex, ez);

      eruptions.push(
        this.createRootEruptionCluster(
          chunkGroup,
          chunkIndex,
          ex,
          ey,
          ez,
          centerX,
          rng,
          obstacles,
          allocObstacleId
        )
      );
    }

    // 4. SUBTERRANEAN GROUND-HEAVE RIDGES (Road cracks open & lifts as a massive root surfaces!)
    const heaveZ = zStart - (0.34 + rng() * 0.12) * chunkLength;
    if (heaveZ < -38) {
      const { centerX } = getCenterAndHalfWidth(heaveZ);
      const hx = centerX + (rng() - 0.5) * 14.0;
      const hy = sampleHeight(hx, heaveZ);
      heaves.push(
        this.createGroundHeave(chunkGroup, chunkIndex, hx, hy, heaveZ, rng, rootBridges, allocBridgeId)
      );
    }

    // 5. TIGHTENING LIVING ROOT ARCHWAY OVER THE ROAD!
    if (chunkIndex % 2 === 0) {
      const archZ = zStart - 0.64 * chunkLength;
      if (archZ < -48) {
        const { centerX } = getCenterAndHalfWidth(archZ);
        const ax = centerX + (rng() - 0.5) * 8.0;
        const ay = sampleHeight(ax, archZ);
        arches.push(
          this.createTighteningRootArch(
            chunkGroup,
            chunkIndex,
            ax,
            ay,
            archZ,
            obstacles,
            allocObstacleId
          )
        );
      }
    }

    // 6. ROOT-UPROOTED FALLING DEAD TIMBER SNAG (Either Fork-Caught Underpass or Road-Block!)
    const snagZ = zStart - 0.84 * chunkLength;
    if (snagZ < -55) {
      const { centerX } = getCenterAndHalfWidth(snagZ);
      const side = chunkIndex % 2 === 0 ? 1 : -1;
      const kind: 'under_fork' | 'road_block' = chunkIndex % 2 === 0 ? 'under_fork' : 'road_block';
      uprootedSnags.push(
        this.createUprootedDeadSnag(
          chunkGroup,
          chunkIndex,
          centerX,
          snagZ,
          side,
          kind,
          sampleHeight,
          rng,
          obstacles,
          allocObstacleId
        )
      );
    }

    return { eruptions, heaves, arches, uprootedSnags, splinters };
  }

  private createRootEruptionCluster(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    x: number,
    y: number,
    z: number,
    trailCenterX: number,
    rng: () => number,
    obstacles: WorldObstacle[],
    allocObstacleId: () => number
  ): DeadwoodRootEruption {
    const group = new THREE.Group();
    chunkGroup.add(group);

    const warningDuration = 0.55 + rng() * 0.65;
    const branches: DeadwoodBranchDef[] = [];

    const addBranch = (
      origin: THREE.Vector3,
      dir: THREE.Vector3,
      birth: number,
      depth: number,
      baseRadius: number,
      parentIdx: number,
      attachDist: number
    ) => {
      const shapeRoll = rng();
      const shape: 'arch' | 'hook' | 'twist' =
        shapeRoll < 0.48 ? 'arch' : shapeRoll < 0.78 ? 'hook' : 'twist';

      const geo = this.createDynamicRootTubeGeo();
      geo.userData.uniqueGeo = true;
      const mesh = new THREE.Mesh(geo, this.liveRootMat);
      mesh.castShadow = true;
      mesh.receiveShadow = true;
      mesh.frustumCulled = false;
      mesh.visible = false;
      mesh.userData.uniqueGeo = true;
      group.add(mesh);

      const burlMesh = new THREE.Mesh(this.burlGeo, this.liveRootMat);
      burlMesh.castShadow = true;
      burlMesh.visible = false;
      group.add(burlMesh);

      // Register 3 dynamic obstacle spheres along primary thick root branches so they physically block the road!
      const obstacleSlots: WorldObstacle[] = [];
      if (depth === 0) {
        for (let s = 0; s < 3; s++) {
          const obs: WorldObstacle = {
            id: allocObstacleId(),
            chunkIndex,
            kind: 'buttress_tree',
            position: new THREE.Vector3(x, y - 20, z), // parked underground until grown!
            radius: 0.05,
            height: 0.1,
            canShearVine: true,
          };
          obstacles.push(obs);
          obstacleSlots.push(obs);
        }
      }

      const bDef: DeadwoodBranchDef = {
        origin: origin.clone(),
        direction: dir.clone().normalize(),
        birth,
        depth,
        baseRadius,
        speed: lerp(3.4, 6.8, rng()),
        thicken: lerp(0.11, 0.22, rng()),
        frequency: lerp(0.14, 0.36, rng()),
        phase: rng() * Math.PI * 2,
        curl: lerp(0.18, 0.62, rng()),
        lift: (rng() - 0.32) * 0.52,
        shape,
        archSpan: lerp(13.0, 24.0, rng()),
        archHeight: lerp(3.6, 6.8, rng()),
        bendRadius: lerp(6.0, 11.5, rng()),
        parent: parentIdx,
        attachDist,
        broken: false,
        mesh,
        burlMesh,
        obstacleSlots,
      };

      const myIndex = branches.push(bDef) - 1;

      if (depth < 2) {
        const childCount = depth === 0 ? 2 + Math.floor(rng() * 2) : 1;
        for (let c = 0; c < childCount; c++) {
          const att = lerp(2.2, depth === 0 ? 8.5 : 4.5, rng());
          const attachPos = this.evalBranchPoint(bDef, att, y, new THREE.Vector3());
          const ang = rng() * Math.PI * 2;
          const childDir = new THREE.Vector3(
            Math.cos(ang),
            rng() < 0.45 ? lerp(0.05, 0.28, rng()) : lerp(0.32, 0.95, rng()),
            Math.sin(ang)
          );
          addBranch(
            attachPos,
            childDir,
            birth + att / bDef.speed,
            depth + 1,
            baseRadius * lerp(0.46, 0.68, rng()),
            myIndex,
            att
          );
        }
      }
    };

    // Aim primary root preferentially across the road toward trailCenterX so it visibly blocks/crosses the path!
    const dirTowardRoad = Math.sign(trailCenterX - x) || (rng() < 0.5 ? 1 : -1);
    const baseYaw =
      Math.abs(trailCenterX - x) > 4.0
        ? (dirTowardRoad > 0 ? 0 : Math.PI) + (rng() - 0.5) * 0.85
        : rng() * Math.PI * 2;

    const primaryDir = new THREE.Vector3(
      Math.cos(baseYaw),
      lerp(0.18, 0.58, rng()),
      Math.sin(baseYaw)
    );
    addBranch(
      new THREE.Vector3(x, y - 0.55, z),
      primaryDir,
      warningDuration,
      0,
      lerp(0.34, 0.62, rng()),
      -1,
      0
    );

    // Dark radial soil fissures on the road surface
    const fissures: THREE.Mesh[] = [];
    for (let f = 0; f < 6; f++) {
      const ang = (f / 6) * Math.PI * 2 + rng() * 0.4;
      const len = lerp(1.6, 3.8, rng());
      const fGeo = new THREE.BoxGeometry(0.12, 0.06, len);
      fGeo.userData.uniqueGeo = true;
      const fMesh = new THREE.Mesh(fGeo, this.fissureDarkMat);
      fMesh.position.set(x + Math.sin(ang) * len * 0.38, y + 0.04, z + Math.cos(ang) * len * 0.38);
      fMesh.rotation.y = ang;
      fMesh.visible = false;
      fMesh.userData.uniqueGeo = true;
      group.add(fMesh);
      fissures.push(fMesh);
    }

    // Displaced clods of dirt thrown up during eruption
    const clods: DeadwoodDirtClod[] = [];
    for (let c = 0; c < 10; c++) {
      const cMesh = new THREE.Mesh(this.clodGeo, this.deadEarthMat);
      cMesh.visible = false;
      cMesh.castShadow = true;
      group.add(cMesh);
      clods.push({
        mesh: cMesh,
        angle: rng() * Math.PI * 2,
        speed: lerp(1.8, 5.2, rng()),
        baseSize: lerp(0.18, 0.48, rng()),
      });
    }

    return {
      x,
      y,
      z,
      warningDuration,
      age: 0,
      renderedAge: -1,
      active: false,
      eruptedBurstDone: false,
      group,
      fissures,
      clods,
      branches,
    };
  }

  private createGroundHeave(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    x: number,
    baseY: number,
    z: number,
    _rng: () => number,
    rootBridges: CrisscrossRootBridge[],
    allocBridgeId: () => number
  ): DeadwoodGroundHeave {
    const halfWidth = 6.5;
    const halfLength = 11.0;

    const moundGeo = new THREE.PlaneGeometry(halfWidth * 2, halfLength * 2, 16, 16);
    moundGeo.rotateX(-Math.PI / 2);
    moundGeo.userData.uniqueGeo = true;
    const basePositions = new Float32Array(moundGeo.attributes.position.array);

    const moundMesh = new THREE.Mesh(moundGeo, this.deadEarthMat);
    moundMesh.position.set(x, baseY + 0.03, z);
    moundMesh.receiveShadow = true;
    moundMesh.castShadow = true;
    moundMesh.visible = false;
    moundMesh.userData.uniqueGeo = true;
    chunkGroup.add(moundMesh);

    const fissureGeo = new THREE.BoxGeometry(0.28, 0.12, halfLength * 1.65);
    fissureGeo.userData.uniqueGeo = true;
    const fissureMesh = new THREE.Mesh(fissureGeo, this.fissureDarkMat);
    fissureMesh.position.set(x, baseY + 0.08, z);
    fissureMesh.visible = false;
    fissureMesh.userData.uniqueGeo = true;
    chunkGroup.add(fissureMesh);

    const rootCylGeo = new THREE.CylinderGeometry(0.75, 0.98, halfLength * 1.6, 8);
    rootCylGeo.rotateX(Math.PI / 2);
    rootCylGeo.userData.uniqueGeo = true;
    const buriedRootMesh = new THREE.Mesh(rootCylGeo, this.liveRootMat);
    buriedRootMesh.position.set(x, baseY - 1.1, z);
    buriedRootMesh.castShadow = true;
    buriedRootMesh.visible = false;
    buriedRootMesh.userData.uniqueGeo = true;
    chunkGroup.add(buriedRootMesh);

    const bridge: CrisscrossRootBridge = {
      id: allocBridgeId(),
      chunkIndex,
      minZ: z - halfLength,
      maxZ: z + halfLength,
      segments: [
        {
          ax: x,
          ay: baseY - 5.0,
          az: z + halfLength * 0.85,
          bx: x,
          by: baseY - 5.0,
          bz: z - halfLength * 0.85,
          deckHalfWidth: halfWidth * 0.72,
          thickness: 1.4,
        },
      ],
    };
    rootBridges.push(bridge);

    return {
      x,
      baseY,
      z,
      halfWidth,
      halfLength,
      age: 0,
      active: false,
      moundMesh,
      basePositions,
      fissureMesh,
      buriedRootMesh,
      bridge,
    };
  }

  private createTighteningRootArch(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    centerX: number,
    baseY: number,
    z: number,
    obstacles: WorldObstacle[],
    allocObstacleId: () => number
  ): DeadwoodTighteningArch {
    const geo = new THREE.BufferGeometry();
    geo.userData.uniqueGeo = true;
    const mesh = new THREE.Mesh(geo, this.liveRootMat);
    mesh.castShadow = true;
    mesh.receiveShadow = true;
    mesh.visible = false;
    mesh.userData.uniqueGeo = true;
    chunkGroup.add(mesh);

    const leftPillarObs: WorldObstacle = {
      id: allocObstacleId(),
      chunkIndex,
      kind: 'buttress_tree',
      position: new THREE.Vector3(centerX - 11.0, baseY - 20, z),
      radius: 0.9,
      height: 7.0,
      canShearVine: true,
    };
    const rightPillarObs: WorldObstacle = {
      id: allocObstacleId(),
      chunkIndex,
      kind: 'buttress_tree',
      position: new THREE.Vector3(centerX + 11.0, baseY - 20, z),
      radius: 0.9,
      height: 7.0,
      canShearVine: true,
    };
    obstacles.push(leftPillarObs, rightPillarObs);

    return {
      centerX,
      baseY,
      z,
      age: 0,
      lastGeoAge: -1,
      active: false,
      mesh,
      leftPillarObs,
      rightPillarObs,
    };
  }

  private createUprootedDeadSnag(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    centerX: number,
    z: number,
    side: number,
    kind: 'under_fork' | 'road_block',
    sampleHeight: (x: number, z: number) => number,
    rng: () => number,
    obstacles: WorldObstacle[],
    allocObstacleId: () => number
  ): DeadwoodUprootedSnag {
    const baseX = centerX + side * 19.5;
    const baseY = sampleHeight(baseX, z);
    const length = 33.0;

    const pivot = new THREE.Group();
    pivot.position.set(baseX, baseY + 0.75, z);
    chunkGroup.add(pivot);

    // Build dead trunk + bare limbs inside pivot
    const pos: number[] = [];
    const norm: number[] = [];
    const uv: number[] = [];
    const idx: number[] = [];
    this.appendTaperedBeam(
      pos,
      norm,
      uv,
      idx,
      new THREE.Vector3(0, 0, 0),
      new THREE.Vector3(0, length, 0),
      1.08,
      0.38,
      7
    );
    for (const [t, sgn] of [
      [0.46, 1],
      [0.66, -1],
      [0.82, 1],
    ] as const) {
      this.appendTaperedBeam(
        pos,
        norm,
        uv,
        idx,
        new THREE.Vector3(0, length * t, 0),
        new THREE.Vector3(sgn * 4.2, length * t + 4.0, sgn * 2.8),
        0.3,
        0.06,
        5
      );
    }
    const trunkGeo = new THREE.BufferGeometry();
    trunkGeo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
    trunkGeo.setAttribute('normal', new THREE.Float32BufferAttribute(norm, 3));
    trunkGeo.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
    trunkGeo.setIndex(idx);
    trunkGeo.userData.uniqueGeo = true;
    const trunkMesh = new THREE.Mesh(trunkGeo, this.deadBarkMat);
    trunkMesh.castShadow = true;
    trunkMesh.receiveShadow = true;
    trunkMesh.userData.uniqueGeo = true;
    pivot.add(trunkMesh);

    // Jagged broken stump base left behind
    const stump = new THREE.Mesh(new THREE.CylinderGeometry(1.15, 1.45, 1.5, 7), this.deadBarkMat);
    stump.position.set(baseX, baseY + 0.55, z);
    stump.castShadow = true;
    stump.userData.uniqueGeo = true;
    chunkGroup.add(stump);

    // If 'under_fork', build the sturdy Y-forked dead tree on the opposite bank that catches the falling trunk!
    if (kind === 'under_fork') {
      const catchX = centerX - side * 16.5;
      const catchY = sampleHeight(catchX, z);
      const fPos: number[] = [];
      const fNorm: number[] = [];
      const fUv: number[] = [];
      const fIdx: number[] = [];
      this.appendTaperedBeam(
        fPos,
        fNorm,
        fUv,
        fIdx,
        new THREE.Vector3(catchX, catchY - 0.3, z),
        new THREE.Vector3(catchX, catchY + 8.6, z),
        0.95,
        0.62,
        7
      );
      for (const dzOff of [-2.4, 2.4]) {
        this.appendTaperedBeam(
          fPos,
          fNorm,
          fUv,
          fIdx,
          new THREE.Vector3(catchX, catchY + 6.8, z),
          new THREE.Vector3(catchX, catchY + 10.6, z + dzOff),
          0.46,
          0.2,
          6
        );
      }
      const forkGeo = new THREE.BufferGeometry();
      forkGeo.setAttribute('position', new THREE.Float32BufferAttribute(fPos, 3));
      forkGeo.setAttribute('normal', new THREE.Float32BufferAttribute(fNorm, 3));
      forkGeo.setAttribute('uv', new THREE.Float32BufferAttribute(fUv, 2));
      forkGeo.setIndex(fIdx);
      forkGeo.userData.uniqueGeo = true;
      const forkMesh = new THREE.Mesh(forkGeo, this.deadBarkMat);
      forkMesh.castShadow = true;
      forkMesh.userData.uniqueGeo = true;
      chunkGroup.add(forkMesh);
    }

    const clods: DeadwoodDirtClod[] = [];
    for (let c = 0; c < 12; c++) {
      const cm = new THREE.Mesh(this.clodGeo, this.deadEarthMat);
      cm.visible = false;
      chunkGroup.add(cm);
      clods.push({
        mesh: cm,
        angle: rng() * Math.PI * 2,
        speed: lerp(2.2, 6.0, rng()),
        baseSize: lerp(0.22, 0.52, rng()),
      });
    }

    const trunkObstacle: WorldObstacle = {
      id: allocObstacleId(),
      chunkIndex,
      kind: 'buttress_tree',
      position: new THREE.Vector3(baseX, baseY, z),
      radius: 1.05,
      height: 22.0,
      canShearVine: true,
    };
    obstacles.push(trunkObstacle);

    return {
      baseX,
      baseY,
      z,
      side,
      length,
      kind,
      age: 0,
      active: false,
      pivot,
      clods,
      trunkObstacle,
    };
  }

  private rebuildEruptionBranchMesh(r: DeadwoodRootEruption, b: DeadwoodBranchDef): void {
    const length = this.evalBranchLength(b, r.age);
    if (length <= 0.04 || b.broken) {
      b.mesh.visible = false;
      b.burlMesh.visible = false;
      for (const obs of b.obstacleSlots) {
        obs.position.y = r.y - 25.0;
        obs.radius = 0.05;
      }
      return;
    }

    // Update child origin if attached to parent
    if (b.parent >= 0) {
      const parentB = r.branches[b.parent];
      if (parentB.broken) {
        b.broken = true;
        b.mesh.visible = false;
        b.burlMesh.visible = false;
        return;
      }
      this.evalBranchPoint(parentB, b.attachDist, r.y, b.origin);
    }

    b.mesh.visible = true;
    const attr = b.mesh.geometry.attributes.position as THREE.BufferAttribute;

    for (let i = 0; i <= TUBE_RINGS; i++) {
      const dist = (i / TUBE_RINGS) * length;
      this.evalBranchPoint(b, dist, r.y, this._va);
      this.evalBranchPoint(b, dist + 0.04, r.y, this._vb);
      this._tangent.subVectors(this._vb, this._va).normalize();

      const ref = Math.abs(this._tangent.y) > 0.94 ? this._altAxis : this._up;
      this._normal.crossVectors(this._tangent, ref).normalize();
      this._binormal.crossVectors(this._tangent, this._normal).normalize();

      const radius = this.evalBranchRadius(b, r.age, dist);
      for (let j = 0; j <= TUBE_SIDES; j++) {
        const ang = (j / TUBE_SIDES) * Math.PI * 2;
        const gnarl = 1.0 + 0.12 * Math.sin(dist * 4.5 + j * 2.1);
        const rad = radius * gnarl;
        const cosA = Math.cos(ang);
        const sinA = Math.sin(ang);
        const vx = this._va.x + (this._normal.x * cosA + this._binormal.x * sinA) * rad;
        const vy = this._va.y + (this._normal.y * cosA + this._binormal.y * sinA) * rad;
        const vz = this._va.z + (this._normal.z * cosA + this._binormal.z * sinA) * rad;
        attr.setXYZ(i * (TUBE_SIDES + 1) + j, vx, vy, vz);
      }
    }

    attr.needsUpdate = true;
    b.mesh.geometry.computeVertexNormals();

    // Root knuckle burl at branch origin
    const burlRad = this.evalBranchRadius(b, r.age, 0) * 1.32;
    b.burlMesh.visible = burlRad > 0.08;
    b.burlMesh.position.copy(b.origin);
    b.burlMesh.scale.set(burlRad, burlRad * 0.72, burlRad);

    // Sync road-blocking dynamic obstacles along primary root!
    if (b.obstacleSlots.length > 0) {
      const sampleFracs = [0.22, 0.52, 0.82];
      for (let s = 0; s < b.obstacleSlots.length; s++) {
        const distS = length * sampleFracs[s];
        this.evalBranchPoint(b, distS, r.y, this._va);
        const radS = this.evalBranchRadius(b, r.age, distS);
        const obs = b.obstacleSlots[s];
        // Only block the car where the root sits within vehicle hull height (not high overhead arches!)
        if (radS >= 0.24 && this._va.y - radS < r.y + 1.85 && this._va.y + radS > r.y + 0.15) {
          obs.position.set(this._va.x, r.y, this._va.z);
          obs.radius = Math.max(0.45, radS * 1.15);
          obs.height = Math.max(1.2, this._va.y - r.y + radS);
        } else {
          obs.position.set(this._va.x, r.y - 25.0, this._va.z);
          obs.radius = 0.05;
        }
      }
    }
  }

  private spawnSplinterBurst(
    chunkGroup: THREE.Group,
    splinters: BarkSplinterParticle[],
    pos: THREE.Vector3
  ): void {
    for (let i = 0; i < 6; i++) {
      if (splinters.length > 48) {
        const oldest = splinters.shift()!;
        chunkGroup.remove(oldest.mesh);
      }
      const m = new THREE.Mesh(this.splinterGeo, this.liveRootMat);
      m.position.copy(pos);
      chunkGroup.add(m);
      splinters.push({
        mesh: m,
        vel: new THREE.Vector3(
          (Math.random() - 0.5) * 8.0,
          2.5 + Math.random() * 4.5,
          (Math.random() - 0.5) * 8.0
        ),
        age: 0,
      });
    }
  }

  public stepChunkHazards(
    chunkGroup: THREE.Group,
    data: DeadwoodChunkData,
    dt: number,
    focusPos: THREE.Vector3,
    vehiclePos: THREE.Vector3,
    vehicleSpeedMps: number,
    passiveObserver: boolean
  ): void {
    // 1. Live Erupting & Growing Root Clusters!
    for (const r of data.eruptions) {
      const distFocus = Math.abs(focusPos.z - r.z);
      const distVeh = Math.abs(vehiclePos.z - r.z);
      if (!r.active && (distFocus < 132 || distVeh < 132)) {
        r.active = true;
      }
      if (!r.active) continue;

      r.age += dt;

      // Show radial soil fissures immediately upon warning
      for (const f of r.fissures) {
        f.visible = true;
        const spread = 1.0 + clamp((r.age - r.warningDuration * 0.5) * 0.85, 0, 1.8);
        f.scale.x = spread;
      }

      // Throw dirt clods upward as root breaks through soil crust!
      const burstT = r.age - r.warningDuration;
      for (const c of r.clods) {
        if (burstT > 0 && burstT < 1.65) {
          c.mesh.visible = true;
          const h = Math.max(0, c.speed * burstT - 4.9 * burstT * burstT);
          c.mesh.position.set(
            r.x + Math.cos(c.angle) * c.speed * 0.55 * burstT,
            r.y + 0.1 + h,
            r.z + Math.sin(c.angle) * c.speed * 0.55 * burstT
          );
          c.mesh.scale.setScalar(c.baseSize);
          c.mesh.rotation.set(burstT * 4.0, c.angle, burstT * 3.0);
        } else {
          c.mesh.visible = false;
        }
      }

      // Rebuild growing root tubes up to ~7.5s maturity at 24 Hz!
      if (r.age < 8.5 && r.age - r.renderedAge > 1 / 24) {
        for (const b of r.branches) {
          this.rebuildEruptionBranchMesh(r, b);
        }
        r.renderedAge = r.age;
      }

      // Allow driving car to snap thin secondary rootlets (< 0.30m) on impact!
      if (!passiveObserver && vehicleSpeedMps > 5.0 && distVeh < 24.0) {
        for (let bi = 1; bi < r.branches.length; bi++) {
          const b = r.branches[bi];
          if (b.broken) continue;
          const len = this.evalBranchLength(b, r.age);
          if (len <= 0.2) continue;
          const midPt = this.evalBranchPoint(b, len * 0.5, r.y, this._va);
          const rad = this.evalBranchRadius(b, r.age, len * 0.5);
          if (
            rad < 0.32 &&
            Math.hypot(midPt.x - vehiclePos.x, midPt.z - vehiclePos.z) < 1.85 + rad &&
            Math.abs(midPt.y - vehiclePos.y) < 1.8
          ) {
            b.broken = true;
            this.rebuildEruptionBranchMesh(r, b);
            this.spawnSplinterBurst(chunkGroup, data.splinters, midPt);
          }
        }
      }
    }

    // 2. Subterranean Ground-Heave Ridges!
    for (const h of data.heaves) {
      if (!h.active && (Math.abs(focusPos.z - h.z) < 118 || Math.abs(vehiclePos.z - h.z) < 118)) {
        h.active = true;
      }
      if (!h.active) continue;

      const prevAge = h.age;
      h.age += dt;
      h.moundMesh.visible = true;
      h.fissureMesh.visible = true;
      h.buriedRootMesh.visible = h.age > 0.9;

      if (h.age < 4.8 && Math.floor(prevAge * 20) !== Math.floor(h.age * 20)) {
        const liftFactor = clamp((h.age - 0.6) / 2.8, 0, 1);
        const posAttr = h.moundMesh.geometry.attributes.position as THREE.BufferAttribute;
        for (let i = 0; i < posAttr.count; i++) {
          const bx = h.basePositions[i * 3];
          const bz = h.basePositions[i * 3 + 2];
          const nx = bx / h.halfWidth;
          const nz = bz / h.halfLength;
          const dome = Math.pow(Math.max(0, 1 - nx * nx), 2) * Math.pow(Math.max(0, 1 - nz * nz), 2);
          const heaveY = dome * liftFactor * 1.45;
          const splitX = bx + (bx < 0 ? -0.22 : 0.22) * liftFactor * dome;
          posAttr.setXYZ(i, splitX, heaveY, bz);
        }
        posAttr.needsUpdate = true;
        h.moundMesh.geometry.computeVertexNormals();

        const crestY = h.baseY + liftFactor * 1.45;
        h.buriedRootMesh.position.y = h.baseY - 1.0 + liftFactor * 1.68;
        h.fissureMesh.position.y = crestY + 0.04;
        h.fissureMesh.scale.x = 1.0 + liftFactor * 3.2;

        // Lift drivable mound bridge segment in sync!
        h.bridge.segments[0].ay = crestY;
        h.bridge.segments[0].by = crestY;
      }
    }

    // 3. Tightening Living Root Archways!
    for (const a of data.arches) {
      if (!a.active && (Math.abs(focusPos.z - a.z) < 145 || Math.abs(vehiclePos.z - a.z) < 145)) {
        a.active = true;
      }
      if (!a.active) continue;

      a.age += dt;
      if (a.age < 13.5 && a.age - a.lastGeoAge > 1 / 18) {
        const grow = clamp(a.age / 2.2, 0.05, 1);
        const tighten = clamp((a.age - 4.8) / 7.5, 0, 1);
        const spanHalfW = (11.5 - 5.2 * tighten) * grow;
        const archH = (7.4 - 2.6 * tighten) * grow;
        const tubeRad = Math.max(0.12, (0.48 + 0.68 * tighten) * grow);

        const pts: THREE.Vector3[] = [];
        for (let i = 0; i <= 32; i++) {
          const t = i / 32;
          const ang = t * Math.PI;
          pts.push(
            new THREE.Vector3(
              a.centerX + Math.cos(ang) * spanHalfW,
              a.baseY - 0.35 + Math.sin(ang) * archH,
              a.z + Math.sin(ang * 2.0) * 1.35
            )
          );
        }
        a.mesh.geometry.dispose();
        const newGeo = new THREE.TubeGeometry(new THREE.CatmullRomCurve3(pts), 32, tubeRad, 8, false);
        newGeo.userData.uniqueGeo = true;
        a.mesh.geometry = newGeo;
        a.mesh.visible = true;
        a.lastGeoAge = a.age;

        a.leftPillarObs.position.set(a.centerX - spanHalfW, a.baseY, a.z);
        a.leftPillarObs.radius = tubeRad * 1.15;
        a.rightPillarObs.position.set(a.centerX + spanHalfW, a.baseY, a.z);
        a.rightPillarObs.radius = tubeRad * 1.15;
      }
    }

    // 4. Root-Uprooted Shuddering & Falling Dead Snags!
    for (const s of data.uprootedSnags) {
      if (!s.active && (Math.abs(focusPos.z - s.z) < 125 || Math.abs(vehiclePos.z - s.z) < 125)) {
        s.active = true;
      }
      if (!s.active) continue;

      s.age += dt;
      const fallT = clamp((s.age - 1.05) / 2.25, 0, 1);
      const targetMaxAngle = s.kind === 'under_fork' ? 1.32 : Math.PI * 0.49;
      let angle = targetMaxAngle * fallT * fallT;
      if (s.age < 1.05) {
        angle += Math.sin(s.age * 34.0) * 0.018; // Intense root-uprooting shudder!
      }
      s.pivot.rotation.z = s.side * angle;

      // Impact earth clods when trunk slams!
      const impactAge = s.age - 3.3;
      for (let ci = 0; ci < s.clods.length; ci++) {
        const c = s.clods[ci];
        if (impactAge >= 0 && impactAge < 1.6) {
          c.mesh.visible = true;
          const frac = (ci + 1) / (s.clods.length + 1);
          const impactX = s.baseX - s.side * Math.sin(angle) * s.length * frac;
          const h = Math.max(0, c.speed * impactAge - 4.9 * impactAge * impactAge);
          c.mesh.position.set(
            impactX + Math.cos(c.angle) * c.speed * 0.5 * impactAge,
            s.baseY + 0.4 + h,
            s.z + Math.sin(c.angle) * c.speed * 0.5 * impactAge
          );
          c.mesh.scale.setScalar(c.baseSize);
        } else {
          c.mesh.visible = false;
        }
      }

      // If road_block snag has landed flat across one half of the road, place obstacle at mid-span!
      if (s.kind === 'road_block' && fallT > 0.85) {
        s.trunkObstacle.position.set(s.baseX - s.side * (s.length * 0.45), s.baseY, s.z);
        s.trunkObstacle.radius = 1.65;
        s.trunkObstacle.height = 2.2;
      }
    }

    // 5. Bark splinter burst particles
    for (let i = data.splinters.length - 1; i >= 0; i--) {
      const p = data.splinters[i];
      p.age += dt;
      p.vel.y -= 9.8 * dt;
      p.mesh.position.addScaledVector(p.vel, dt);
      p.mesh.rotation.x += 6.0 * dt;
      p.mesh.rotation.z += 4.5 * dt;
      if (p.age > 1.35) {
        chunkGroup.remove(p.mesh);
        data.splinters.splice(i, 1);
      }
    }
  }

  public sampleGroundHeaveHeight(data: DeadwoodChunkData, x: number, z: number): number {
    let extra = 0;
    for (const h of data.heaves) {
      if (!h.active || h.age <= 0.6) continue;
      const nx = (x - h.x) / h.halfWidth;
      const nz = (z - h.z) / h.halfLength;
      if (Math.abs(nx) >= 1.0 || Math.abs(nz) >= 1.0) continue;
      const liftFactor = clamp((h.age - 0.6) / 2.8, 0, 1);
      const dome = Math.pow(Math.max(0, 1 - nx * nx), 2) * Math.pow(Math.max(0, 1 - nz * nz), 2);
      const val = dome * liftFactor * 1.45;
      if (val > extra) extra = val;
    }
    return extra;
  }
}

import * as THREE from 'three';
import { clamp, fbm2D, lerp, mulberry32, ridge2D, smoothstep } from '../math/noise';
import { ProceduralTextureLibrary } from '../graphics/proceduralTextures';
import { WorldObstacle } from './trailCorridor';

export interface FractureBranch {
  points: THREE.Vector2[]; // (x, z) on the flat ice sheet
  segLengths: number[];
  totalLength: number;
  spawnAtMainDist: number; // Main crack distance at which this branch begins splitting off
  revealedLength: number;
  speedScale: number;
}

export interface TravelingIceFracture {
  id: number;
  origin: THREE.Vector2;
  mainBranch: FractureBranch;
  offshoots: FractureBranch[];
  triggered: boolean;
  triggerRadius: number;
  currentSpeedMps: number;
  targetSpeedMps: number;
  speedChangeTimer: number;
  rngState: number;
  openWidthScale: number; // Widens behind the traveling crack tip!
  fissureMesh: THREE.Mesh;
  rimLineSegments: THREE.LineSegments;
  tipGlowMesh: THREE.Mesh;
  geometryDirty: boolean;
}

export interface FallingCaveIcicle {
  id: number;
  kind: 'POINTY_ICICLE' | 'ICE_BLOCK';
  ceilPos: THREE.Vector3;
  floorY: number;
  currentY: number;
  vy: number;
  lengthY: number;
  radiusXZ: number;
  shudderTimer: number;
  shudderDuration: number;
  triggerDist: number;
  state: 'HANGING' | 'SHUDDERING' | 'FALLING' | 'IMPALED';
  mesh: THREE.Mesh;
  shadowWarnRing: THREE.Mesh;
  obstacle: WorldObstacle;
  spawnedImpactFracture: boolean;
}

export interface GlacialIceChunkData {
  chunkIndex: number;
  zStart: number;
  isCaveChunk: boolean;
  fractures: TravelingIceFracture[];
  fallingIcicles: FallingCaveIcicle[];
}

const CAVE_RADIUS_X = 38.5;
const CAVE_RADIUS_Y = 32.0;
const MAX_FRACTURE_TRIS = 640; // Preallocated dynamic ribbon buffer per fracture for zero GC stutter!
const MAX_FRACTURE_RIM_VERTS = 520;

export class GlacialIceBuilder {
  public readonly iceCaveVaultMat: THREE.MeshStandardMaterial;
  public readonly crystalIcicleMat: THREE.MeshStandardMaterial;
  public readonly shudderIcicleMat: THREE.MeshStandardMaterial;
  public readonly glacierBlockMat: THREE.MeshStandardMaterial;
  public readonly fissureCoreMat: THREE.MeshBasicMaterial;
  public readonly fractureRimMat: THREE.LineBasicMaterial;
  public readonly tipSparkMat: THREE.MeshBasicMaterial;
  public readonly impactWarnMat: THREE.MeshBasicMaterial;
  public readonly arcticGodrayMat: THREE.ShaderMaterial;
  public readonly glassyIceSheenMat: THREE.ShaderMaterial;

  // Global blowing spindrift snow & ice-crystal mist across the flat ice!
  public readonly atmosphereFxGroup = new THREE.Group();
  private snowPoints!: THREE.Points;
  private snowPositions!: Float32Array;
  private snowVelocities!: Float32Array;
  private readonly snowCount = 1350;

  private readonly pointyIcicleGeo: THREE.BufferGeometry;
  private readonly staticSmallIcicleGeo: THREE.BufferGeometry;
  private readonly jaggedIceBlockGeos: THREE.BufferGeometry[] = [];
  private readonly warnRingGeo: THREE.BufferGeometry;
  private readonly sparkSphereGeo: THREE.BufferGeometry;
  private readonly timeUniform = { value: 0 };

  private nextFractureId = 1;
  private nextIcicleId = 1;
  private playerStressTimer = 0;

  public onIceFractureBurst?: (worldPos: THREE.Vector3) => void;
  public onIcicleShatterSlam?: (
    worldPos: THREE.Vector3,
    kind: 'POINTY_ICICLE' | 'ICE_BLOCK'
  ) => void;
  public onVehicleCrossOpenCrack?: (worldPos: THREE.Vector3) => void;

  private crackJoltCooldown = 0;

  constructor(textures: ProceduralTextureLibrary) {
    // 1. Translucent crystalline blue-cyan Glacial Cave Vault
    this.iceCaveVaultMat = new THREE.MeshStandardMaterial({
      color: 0x7fd4f5,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(1.55, 1.55),
      roughness: 0.18,
      metalness: 0.16,
      vertexColors: true,
      emissive: new THREE.Color(0x0f4c6e),
      emissiveIntensity: 0.48,
      side: THREE.DoubleSide,
    });

    // 2. Razor-sharp pointy crystal cave icicles (stalactites)
    this.crystalIcicleMat = new THREE.MeshStandardMaterial({
      color: 0xbff2ff,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(1.1, 1.1),
      roughness: 0.1,
      metalness: 0.22,
      transparent: true,
      opacity: 0.9,
      emissive: new THREE.Color(0x2892bd),
      emissiveIntensity: 0.45,
    });

    // Bright white-cyan flash material while an icicle is cracking loose overhead!
    this.shudderIcicleMat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      roughness: 0.08,
      metalness: 0.25,
      emissive: new THREE.Color(0x7af0ff),
      emissiveIntensity: 1.1,
    });

    // 3. Heavy calving blue-white glacier ice blocks
    this.glacierBlockMat = new THREE.MeshStandardMaterial({
      color: 0x9ce3fc,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(1.35, 1.35),
      roughness: 0.16,
      metalness: 0.18,
      emissive: new THREE.Color(0x16648c),
      emissiveIntensity: 0.38,
    });

    // 4. Deep glowing sub-zero cyan-abyss crack interior + bright white stress-fracture lips!
    this.fissureCoreMat = new THREE.MeshBasicMaterial({
      color: 0x0b2538,
      side: THREE.DoubleSide,
    });

    this.fractureRimMat = new THREE.LineBasicMaterial({
      color: 0xe8fbff,
      transparent: true,
      opacity: 0.95,
    });

    this.tipSparkMat = new THREE.MeshBasicMaterial({
      color: 0x8cf6ff,
      transparent: true,
      opacity: 0.92,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });

    this.impactWarnMat = new THREE.MeshBasicMaterial({
      color: 0x5ce6ff,
      transparent: true,
      opacity: 0.42,
      side: THREE.DoubleSide,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });

    // 5. Pale-cyan arctic skylight rays inside the Glacial Cave!
    this.arcticGodrayMat = new THREE.ShaderMaterial({
      transparent: true,
      depthWrite: false,
      side: THREE.DoubleSide,
      blending: THREE.AdditiveBlending,
      uniforms: { uTime: this.timeUniform },
      vertexShader: `
        varying vec2 vUv;
        varying vec3 vNormalView;
        varying vec3 vViewDir;
        void main() {
          vUv = uv;
          vec4 mvPos = modelViewMatrix * vec4(position, 1.0);
          vNormalView = normalize(normalMatrix * normal);
          vViewDir = normalize(-mvPos.xyz);
          gl_Position = projectionMatrix * mvPos;
        }
      `,
      fragmentShader: `
        varying vec2 vUv;
        varying vec3 vNormalView;
        varying vec3 vViewDir;
        uniform float uTime;
        void main() {
          float facing = abs(dot(normalize(vNormalView), normalize(vViewDir)));
          float softEdge = pow(facing, 1.6);
          float vFade = smoothstep(0.0, 0.15, vUv.y) * (0.4 + 0.6 * vUv.y);
          float shimmer = 0.72 + 0.28 * sin(vUv.x * 22.0 + uTime * 0.9);
          vec3 col = mix(vec3(0.45, 0.88, 1.0), vec3(0.88, 0.98, 1.0), vUv.y);
          gl_FragColor = vec4(col, softEdge * vFade * shimmer * 0.22);
        }
      `,
    });

    // 6. Specular Glassy Blue Black-Ice Overlay Sheen on the Flat Ice Plane!
    this.glassyIceSheenMat = new THREE.ShaderMaterial({
      transparent: true,
      depthWrite: false,
      uniforms: { uTime: this.timeUniform },
      vertexShader: `
        varying vec2 vUv;
        varying vec3 vWorldPos;
        void main() {
          vUv = uv;
          vec4 wp = modelMatrix * vec4(position, 1.0);
          vWorldPos = wp.xyz;
          gl_Position = projectionMatrix * viewMatrix * wp;
        }
      `,
      fragmentShader: `
        varying vec2 vUv;
        varying vec3 vWorldPos;
        uniform float uTime;
        void main() {
          float d = length(vUv - vec2(0.5)) * 2.0;
          float edge = smoothstep(1.0, 0.35, d);
          float vein = abs(sin(vWorldPos.x * 0.35 + sin(vWorldPos.z * 0.22) * 2.1));
          float glint = smoothstep(0.85, 0.99, vein);
          vec3 deepIce = vec3(0.16, 0.48, 0.68);
          vec3 frostGlint = vec3(0.78, 0.96, 1.0);
          vec3 col = mix(deepIce, frostGlint, glint * 0.65);
          gl_FragColor = vec4(col, edge * 0.36);
        }
      `,
    });

    // Build razor-sharp fluted pointy cave icicle geometry (tip pointing straight DOWN at -Y!)
    this.pointyIcicleGeo = this.createFlutedPointyIcicleGeometry(7);
    this.staticSmallIcicleGeo = this.createFlutedPointyIcicleGeometry(5);

    for (let b = 0; b < 3; b++) {
      this.jaggedIceBlockGeos.push(this.createJaggedIceBlockGeometry(101 + b * 43));
    }

    const ringGeo = new THREE.RingGeometry(0.55, 1.0, 20);
    ringGeo.rotateX(-Math.PI * 0.5);
    this.warnRingGeo = ringGeo;

    this.sparkSphereGeo = new THREE.SphereGeometry(0.55, 8, 8);

    this.initSpindriftSnow();
  }

  /**
   * Builds a multi-faceted, fluted, razor-sharp pointy cave stalactite icicle!
   * Origin (0,0,0) is at the wide ceiling root; sharp needle point is at (0, -1, 0).
   */
  private createFlutedPointyIcicleGeometry(radialSides: number): THREE.BufferGeometry {
    const heightSegs = 8;
    const geo = new THREE.ConeGeometry(1.0, 1.0, radialSides, heightSegs, false);
    // Flip cone so wide base is at y = 0 and razor-sharp point is at y = -1.0!
    geo.rotateX(Math.PI);
    geo.translate(0, -0.5, 0);

    const pos = geo.attributes.position as THREE.BufferAttribute;
    for (let i = 0; i < pos.count; i++) {
      const x = pos.getX(i);
      const y = pos.getY(i); // 0 at top root .. -1 at sharp tip
      const z = pos.getZ(i);
      const u = clamp(-y, 0, 1);
      if (u > 0.02 && u < 0.96) {
        const ang = Math.atan2(z, x);
        const flute = 1.0 + 0.18 * Math.sin(ang * radialSides + y * 5.0);
        // Slight organic crystalline taper bulge near upper third, needle-thin lower half
        const profile = Math.pow(1.0 - u, 0.22) * flute;
        pos.setX(i, x * profile);
        pos.setZ(i, z * profile);
      }
    }
    geo.computeVertexNormals();
    return geo;
  }

  private createJaggedIceBlockGeometry(seed: number): THREE.BufferGeometry {
    const rng = mulberry32(seed);
    const geo = new THREE.DodecahedronGeometry(1.0, 0);
    const pos = geo.attributes.position as THREE.BufferAttribute;
    for (let i = 0; i < pos.count; i++) {
      const vx = pos.getX(i);
      const vy = pos.getY(i);
      const vz = pos.getZ(i);
      const s = 0.78 + 0.36 * (fbm2D(vx * 1.9 + seed, vz * 1.9 - vy, 2) * 0.5 + 0.5);
      pos.setXYZ(i, vx * s * 1.15, vy * s * 0.85 + (rng() - 0.5) * 0.08, vz * s * 1.1);
    }
    const nonIndexed = geo.index ? geo.toNonIndexed() : geo;
    nonIndexed.computeVertexNormals();
    return nonIndexed;
  }

  private initSpindriftSnow(): void {
    this.snowPositions = new Float32Array(this.snowCount * 3);
    this.snowVelocities = new Float32Array(this.snowCount * 3);
    const rng = mulberry32(771234);

    for (let i = 0; i < this.snowCount; i++) {
      const ix = i * 3;
      this.snowPositions[ix] = (rng() - 0.5) * 140;
      this.snowPositions[ix + 1] = rng() * 28;
      this.snowPositions[ix + 2] = (rng() - 0.5) * 160;

      // Crisp sub-zero spindrift skimming fast across the flat ice!
      this.snowVelocities[ix] = lerp(8.0, 19.0, rng());
      this.snowVelocities[ix + 1] = lerp(-2.4, -0.5, rng());
      this.snowVelocities[ix + 2] = lerp(4.0, 14.0, rng());
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(this.snowPositions, 3));

    const mat = new THREE.PointsMaterial({
      color: 0xe6f8ff,
      size: 0.38,
      transparent: true,
      opacity: 0.65,
      depthWrite: false,
    });

    this.snowPoints = new THREE.Points(geo, mat);
    this.snowPoints.frustumCulled = false;
    this.atmosphereFxGroup.add(this.snowPoints);
  }

  /**
   * Builds a jagged branching 2D fracture path across the flat ice sheet
   * and allocates live dynamic BufferGeometries so the crack propagates smoothly at variable speed!
   */
  private createTravelingFracture(
    chunkGroup: THREE.Group,
    originX: number,
    originZ: number,
    dirAngle: number,
    totalReach: number,
    triggerRadius: number,
    startTriggered: boolean,
    rng: () => number
  ): TravelingIceFracture {
    const mainPts: THREE.Vector2[] = [new THREE.Vector2(originX, originZ)];
    const mainLengths: number[] = [];
    let mainTotal = 0;

    const segCount = 18;
    const stepLen = totalReach / segCount;
    let curX = originX;
    let curZ = originZ;
    let curAng = dirAngle;

    for (let s = 0; s < segCount; s++) {
      // Sharp crystalline fracture kinks!
      curAng += (rng() - 0.5) * 0.72;
      const len = stepLen * lerp(0.7, 1.35, rng());
      const nx = curX + Math.cos(curAng) * len;
      const nz = curZ + Math.sin(curAng) * len;
      mainPts.push(new THREE.Vector2(nx, nz));
      mainLengths.push(len);
      mainTotal += len;
      curX = nx;
      curZ = nz;
    }

    const mainBranch: FractureBranch = {
      points: mainPts,
      segLengths: mainLengths,
      totalLength: mainTotal,
      spawnAtMainDist: 0,
      revealedLength: startTriggered ? 1.2 : 0,
      speedScale: 1.0,
    };

    // 5 forked secondary spiderweb offshoots branching off kink nodes along the main crack!
    const offshoots: FractureBranch[] = [];
    let accumDist = 0;
    for (let s = 2; s < segCount - 2; s += 3) {
      accumDist = mainLengths.slice(0, s).reduce((a, b) => a + b, 0);
      const forkOrigin = mainPts[s];
      const parentSeg = new THREE.Vector2().subVectors(mainPts[s], mainPts[s - 1]);
      const baseAng = Math.atan2(parentSeg.y, parentSeg.x);
      const forkSide = s % 2 === 0 ? 1 : -1;
      let fAng = baseAng + forkSide * lerp(0.48, 1.05, rng());

      const fPts: THREE.Vector2[] = [forkOrigin.clone()];
      const fLens: number[] = [];
      let fTotal = 0;
      let fx = forkOrigin.x;
      let fz = forkOrigin.y;
      const fSegs = 6;
      for (let k = 0; k < fSegs; k++) {
        fAng += (rng() - 0.5) * 0.65;
        const fl = lerp(2.2, 4.6, rng());
        fx += Math.cos(fAng) * fl;
        fz += Math.sin(fAng) * fl;
        fPts.push(new THREE.Vector2(fx, fz));
        fLens.push(fl);
        fTotal += fl;
      }

      offshoots.push({
        points: fPts,
        segLengths: fLens,
        totalLength: fTotal,
        spawnAtMainDist: accumDist,
        revealedLength: 0,
        speedScale: lerp(0.65, 1.25, rng()),
      });
    }

    // Dynamic Ribbon Mesh for the dark sub-zero fissure opening
    const fissureGeo = new THREE.BufferGeometry();
    const posBuf = new Float32Array(MAX_FRACTURE_TRIS * 3 * 3);
    const posAttr = new THREE.BufferAttribute(posBuf, 3);
    posAttr.setUsage(THREE.DynamicDrawUsage);
    fissureGeo.setAttribute('position', posAttr);
    fissureGeo.setDrawRange(0, 0);

    const fissureMesh = new THREE.Mesh(fissureGeo, this.fissureCoreMat);
    fissureMesh.frustumCulled = false;
    fissureMesh.renderOrder = 3;
    fissureMesh.userData.uniqueGeo = true;
    chunkGroup.add(fissureMesh);

    // Bright frosted white-cyan crystalline crack rim lines
    const rimGeo = new THREE.BufferGeometry();
    const rimBuf = new Float32Array(MAX_FRACTURE_RIM_VERTS * 3);
    const rimAttr = new THREE.BufferAttribute(rimBuf, 3);
    rimAttr.setUsage(THREE.DynamicDrawUsage);
    rimGeo.setAttribute('position', rimAttr);
    rimGeo.setDrawRange(0, 0);

    const rimLineSegments = new THREE.LineSegments(rimGeo, this.fractureRimMat);
    rimLineSegments.frustumCulled = false;
    rimLineSegments.renderOrder = 4;
    rimLineSegments.userData.uniqueGeo = true;
    chunkGroup.add(rimLineSegments);

    // Glinting cyan fracture-tip stress spark!
    const tipGlowMesh = new THREE.Mesh(this.sparkSphereGeo, this.tipSparkMat);
    tipGlowMesh.visible = false;
    tipGlowMesh.renderOrder = 5;
    chunkGroup.add(tipGlowMesh);

    const initFast = rng() < 0.55;
    const initSpeed = initFast ? lerp(34.0, 58.0, rng()) : lerp(4.0, 9.5, rng());

    return {
      id: this.nextFractureId++,
      origin: new THREE.Vector2(originX, originZ),
      mainBranch,
      offshoots,
      triggered: startTriggered,
      triggerRadius,
      currentSpeedMps: initSpeed,
      targetSpeedMps: initSpeed,
      speedChangeTimer: lerp(0.18, 0.45, rng()),
      rngState: Math.floor(rng() * 0xffffffff) >>> 0,
      openWidthScale: lerp(0.85, 1.35, rng()),
      fissureMesh,
      rimLineSegments,
      tipGlowMesh,
      geometryDirty: startTriggered,
    };
  }

  /**
   * Rebuilds the live 3D ribbon + frosted rim vertices up to `branch.revealedLength`!
   */
  private updateFractureGeometry(
    fr: TravelingIceFracture,
    groundSampler: (x: number, z: number) => number
  ): void {
    const triPos = (fr.fissureMesh.geometry.attributes.position as THREE.BufferAttribute)
      .array as Float32Array;
    const rimPos = (fr.rimLineSegments.geometry.attributes.position as THREE.BufferAttribute)
      .array as Float32Array;

    let triFloats = 0;
    let rimFloats = 0;

    const writeBranchRibbon = (br: FractureBranch, isMain: boolean) => {
      if (br.revealedLength <= 0.05) return;
      let rem = br.revealedLength;
      const maxHalfW = (isMain ? 0.58 : 0.26) * fr.openWidthScale;

      let prevLX = 0;
      let prevLZ = 0;
      let prevLY = 0;
      let prevRX = 0;
      let prevRZ = 0;
      let prevRY = 0;
      let hasPrev = false;

      let distFromStart = 0;
      for (let i = 0; i < br.segLengths.length; i++) {
        if (rem <= 0) break;
        const segLen = br.segLengths[i];
        const useLen = Math.min(rem, segLen);
        const tEnd = useLen / Math.max(0.001, segLen);

        const p0 = br.points[i];
        const p1 = br.points[i + 1];
        const ex = lerp(p0.x, p1.x, tEnd);
        const ez = lerp(p0.y, p1.y, tEnd);

        const dx = p1.x - p0.x;
        const dz = p1.y - p0.y;
        const invL = 1.0 / Math.max(0.001, Math.hypot(dx, dz));
        const nx = -dz * invL;
        const nz = dx * invL;

        if (!hasPrev) {
          const distToTip0 = Math.max(0, br.revealedLength - distFromStart);
          const hw0 = clamp(distToTip0 * 0.045, 0.03, maxHalfW);
          const y0 = groundSampler(p0.x, p0.y) + 0.045;
          prevLX = p0.x + nx * hw0;
          prevLZ = p0.y + nz * hw0;
          prevLY = y0;
          prevRX = p0.x - nx * hw0;
          prevRZ = p0.y - nz * hw0;
          prevRY = y0;
          hasPrev = true;
        }

        const distAtEnd = distFromStart + useLen;
        const distToTip1 = Math.max(0, br.revealedLength - distAtEnd);
        // Razor-thin right at the moving crack tip, widening behind the tip as the ice plates part!
        const hw1 = clamp(distToTip1 * 0.045, 0.025, maxHalfW);
        const y1 = groundSampler(ex, ez) + 0.045;

        const curLX = ex + nx * hw1;
        const curLZ = ez + nz * hw1;
        const curRX = ex - nx * hw1;
        const curRZ = ez - nz * hw1;

        if (triFloats + 18 <= triPos.length) {
          // Triangle 1
          triPos[triFloats++] = prevLX;
          triPos[triFloats++] = prevLY;
          triPos[triFloats++] = prevLZ;
          triPos[triFloats++] = prevRX;
          triPos[triFloats++] = prevRY;
          triPos[triFloats++] = prevRZ;
          triPos[triFloats++] = curLX;
          triPos[triFloats++] = y1;
          triPos[triFloats++] = curLZ;
          // Triangle 2
          triPos[triFloats++] = prevRX;
          triPos[triFloats++] = prevRY;
          triPos[triFloats++] = prevRZ;
          triPos[triFloats++] = curRX;
          triPos[triFloats++] = y1;
          triPos[triFloats++] = curRZ;
          triPos[triFloats++] = curLX;
          triPos[triFloats++] = y1;
          triPos[triFloats++] = curLZ;
        }

        if (rimFloats + 12 <= rimPos.length) {
          rimPos[rimFloats++] = prevLX;
          rimPos[rimFloats++] = prevLY + 0.02;
          rimPos[rimFloats++] = prevLZ;
          rimPos[rimFloats++] = curLX;
          rimPos[rimFloats++] = y1 + 0.02;
          rimPos[rimFloats++] = curLZ;

          rimPos[rimFloats++] = prevRX;
          rimPos[rimFloats++] = prevRY + 0.02;
          rimPos[rimFloats++] = prevRZ;
          rimPos[rimFloats++] = curRX;
          rimPos[rimFloats++] = y1 + 0.02;
          rimPos[rimFloats++] = curRZ;
        }

        if (isMain && rem <= segLen) {
          fr.tipGlowMesh.position.set(ex, y1 + 0.12, ez);
        }

        prevLX = curLX;
        prevLZ = curLZ;
        prevLY = y1;
        prevRX = curRX;
        prevRZ = curRZ;
        prevRY = y1;

        distFromStart = distAtEnd;
        rem -= useLen;
      }
    };

    writeBranchRibbon(fr.mainBranch, true);
    for (const off of fr.offshoots) {
      writeBranchRibbon(off, false);
    }

    const posAttr = fr.fissureMesh.geometry.attributes.position as THREE.BufferAttribute;
    posAttr.needsUpdate = true;
    fr.fissureMesh.geometry.setDrawRange(0, triFloats / 3);

    const rimAttr = fr.rimLineSegments.geometry.attributes.position as THREE.BufferAttribute;
    rimAttr.needsUpdate = true;
    fr.rimLineSegments.geometry.setDrawRange(0, rimFloats / 3);
  }

  /**
   * Builds the Towering Translucent Crystalline Glacial Ice Cave + Static & Falling Pointy Stalactites!
   */
  private buildGlacialIceCaveAndIcicles(
    chunkGroup: THREE.Group,
    fallingIcicles: FallingCaveIcicle[],
    obstacles: WorldObstacle[],
    chunkIndex: number,
    zStart: number,
    chunkLength: number,
    rng: () => number,
    specSampler: (z: number) => { centerX: number; halfWidth: number; baseY: number },
    groundSampler: (x: number, z: number) => number,
    allocObstacleId: () => number
  ): void {
    // 3 cyan skylight crevasses in the upper glacier cave roof
    const skylights = [0.22, 0.54, 0.84].map((u) => ({
      zCenter: zStart - u * chunkLength,
      zHalf: lerp(4.5, 7.2, rng()),
      thetaCenter: lerp(1.25, 1.9, rng()),
      thetaHalf: lerp(0.14, 0.24, rng()),
    }));

    const zSegs = 26;
    const aSegs = 28;
    const thetaMin = -0.05;
    const thetaMax = Math.PI + 0.05;
    const cols = aSegs + 1;
    const rows = zSegs + 1;

    const positions = new Float32Array(cols * rows * 3);
    const colors = new Float32Array(cols * rows * 3);
    const uvs = new Float32Array(cols * rows * 2);
    const indices: number[] = [];

    const colDeepIce = new THREE.Color(0x2878a8);
    const colFrostCyan = new THREE.Color(0x9cecff);
    const colSnowRim = new THREE.Color(0xeafaff);
    const tmpCol = new THREE.Color();

    const inSkylight = (z: number, theta: number): number => {
      let best = 0;
      for (const sk of skylights) {
        const nz = (z - sk.zCenter) / sk.zHalf;
        const nt = (theta - sk.thetaCenter) / sk.thetaHalf;
        const d = nz * nz + nt * nt + Math.sin(z * 0.6 + theta * 8.0) * 0.14;
        if (d < 1.0) best = Math.max(best, 1.0 - d);
      }
      return best;
    };

    let pPtr = 0;
    let uvPtr = 0;
    for (let rz = 0; rz < rows; rz++) {
      const tz = rz / zSegs;
      const z = zStart - tz * chunkLength;
      const sp = specSampler(z);
      // Taper entrance & exit portals slightly so entering/exiting the ice cave looks like a dramatic glacier mouth!
      const portalFlare =
        1.0 + (smoothstep(0.12, 0.0, tz) + smoothstep(0.88, 1.0, tz)) * 0.12;

      for (let ca = 0; ca < cols; ca++) {
        const ta = ca / aSegs;
        const theta = lerp(thetaMin, thetaMax, ta);

        // Angular crystalline glacier facets!
        const facet =
          ridge2D(z * 0.075, theta * 3.8, 2) * 3.4 +
          Math.sin(theta * 16.0 + z * 0.09) * 1.1;
        const rx = (CAVE_RADIUS_X - facet) * portalFlare;
        const ry = (CAVE_RADIUS_Y - facet * 0.85) * portalFlare;

        positions[pPtr] = sp.centerX + Math.cos(theta) * rx;
        positions[pPtr + 1] = sp.baseY - 0.8 + Math.sin(theta) * ry;
        positions[pPtr + 2] = z;

        uvs[uvPtr++] = ta * 6.0;
        uvs[uvPtr++] = z * 0.07;

        const skProx = inSkylight(z, theta);
        tmpCol.copy(colDeepIce).lerp(colFrostCyan, clamp(facet * 0.24, 0, 0.88));
        if (skProx > 0.01) tmpCol.lerp(colSnowRim, clamp(skProx * 1.7, 0, 0.95));

        colors[pPtr] = tmpCol.r;
        colors[pPtr + 1] = tmpCol.g;
        colors[pPtr + 2] = tmpCol.b;
        pPtr += 3;
      }
    }

    for (let rz = 0; rz < zSegs; rz++) {
      const zMid = zStart - ((rz + 0.5) / zSegs) * chunkLength;
      for (let ca = 0; ca < aSegs; ca++) {
        const thetaMid = lerp(thetaMin, thetaMax, (ca + 0.5) / aSegs);
        if (inSkylight(zMid, thetaMid) > 0.24) continue;
        const a = rz * cols + ca;
        const b = a + 1;
        const c = a + cols;
        const d = c + 1;
        indices.push(a, c, b, b, c, d);
      }
    }

    const caveGeo = new THREE.BufferGeometry();
    caveGeo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    caveGeo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
    caveGeo.setAttribute('uv', new THREE.BufferAttribute(uvs, 2));
    caveGeo.setIndex(indices);
    caveGeo.computeVertexNormals();

    const caveMesh = new THREE.Mesh(caveGeo, this.iceCaveVaultMat);
    caveMesh.castShadow = true;
    caveMesh.receiveShadow = true;
    caveMesh.userData.uniqueGeo = true;
    chunkGroup.add(caveMesh);

    // Pale-cyan glacier crevasse sunbeams slanting down into the ice cave!
    for (const sk of skylights) {
      const sp = specSampler(sk.zCenter);
      const hx = sp.centerX + Math.cos(sk.thetaCenter) * (CAVE_RADIUS_X - 1.0);
      const hy = sp.baseY + Math.sin(sk.thetaCenter) * (CAVE_RADIUS_Y - 1.0);
      const holePos = new THREE.Vector3(hx, hy, sk.zCenter);
      const floorPos = new THREE.Vector3(
        clamp(hx + 11.0, sp.centerX - 24, sp.centerX + 24),
        sp.baseY + 0.08,
        sk.zCenter + 9.0
      );
      const beamVec = new THREE.Vector3().subVectors(floorPos, holePos);
      const rayGeo = new THREE.CylinderGeometry(3.2, 6.8, beamVec.length() + 3.0, 14, 1, true);
      const rayMesh = new THREE.Mesh(rayGeo, this.arcticGodrayMat);
      rayMesh.position.copy(holePos).addScaledVector(beamVec, 0.5);
      rayMesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, -1, 0), beamVec.normalize());
      rayMesh.renderOrder = 5;
      rayMesh.userData.uniqueGeo = true;
      chunkGroup.add(rayMesh);
    }

    // 90 Instanced Decorative Ceiling Icicles bristling across the cave roof!
    const dummy = new THREE.Object3D();
    const decorMats: THREE.Matrix4[] = [];
    for (let i = 0; i < 90; i++) {
      const iz = zStart - rng() * chunkLength;
      const sp = specSampler(iz);
      const theta = lerp(0.45, Math.PI - 0.45, rng());
      if (inSkylight(iz, theta) > 0.1) continue;
      const ix = sp.centerX + Math.cos(theta) * (CAVE_RADIUS_X - 2.2);
      const iy = sp.baseY - 0.8 + Math.sin(theta) * (CAVE_RADIUS_Y - 1.8);
      dummy.position.set(ix, iy, iz);
      dummy.rotation.set(0, rng() * Math.PI * 2, 0);
      dummy.scale.set(lerp(0.45, 1.15, rng()), lerp(2.8, 7.5, rng()), lerp(0.45, 1.15, rng()));
      dummy.updateMatrix();
      decorMats.push(dummy.matrix.clone());
    }
    if (decorMats.length > 0) {
      const im = new THREE.InstancedMesh(
        this.staticSmallIcicleGeo,
        this.crystalIcicleMat,
        decorMats.length
      );
      decorMats.forEach((m, idx) => im.setMatrixAt(idx, m));
      im.instanceMatrix.needsUpdate = true;
      chunkGroup.add(im);
    }

    // 11 FALLING SHARP POINTY CAVE ICICLES & HEAVY CALVING ICE BLOCKS!
    const fallCount = 11;
    for (let f = 0; f < fallCount; f++) {
      const fz = zStart - ((f + 0.14 + rng() * 0.65) / fallCount) * chunkLength;
      if (Math.abs(fz) < 30) continue;

      const sp = specSampler(fz);
      const fx = sp.centerX + (rng() - 0.5) * (sp.halfWidth * 1.55);
      const floorY = groundSampler(fx, fz);

      const relX = clamp((fx - sp.centerX) / CAVE_RADIUS_X, -0.82, 0.82);
      const ceilY =
        sp.baseY - 0.8 + Math.sqrt(Math.max(0.15, 1.0 - relX * relX)) * (CAVE_RADIUS_Y - 1.2);

      // 75% razor-sharp pointy cave stalactites, 25% heavy angular glacier ice blocks!
      const isPointy = f % 4 !== 3;
      const radXZ = isPointy ? lerp(1.15, 1.95, rng()) : lerp(1.85, 2.85, rng());
      const lenY = isPointy ? lerp(7.2, 12.6, rng()) : radXZ * 1.75;

      const mesh = new THREE.Mesh(
        isPointy
          ? this.pointyIcicleGeo
          : this.jaggedIceBlockGeos[f % this.jaggedIceBlockGeos.length],
        isPointy ? this.crystalIcicleMat : this.glacierBlockMat
      );
      mesh.position.set(fx, ceilY, fz);
      mesh.rotation.y = rng() * Math.PI * 2;
      if (isPointy) {
        mesh.scale.set(radXZ, lenY, radXZ);
      } else {
        mesh.scale.setScalar(radXZ);
      }
      mesh.castShadow = true;
      chunkGroup.add(mesh);

      // Subtle glowing cyan target ring on the glassy ice directly beneath a falling icicle!
      const warnRing = new THREE.Mesh(this.warnRingGeo, this.impactWarnMat);
      warnRing.position.set(fx, floorY + 0.07, fz);
      warnRing.scale.setScalar(radXZ * 1.9);
      warnRing.visible = false;
      warnRing.renderOrder = 3;
      chunkGroup.add(warnRing);

      const obs: WorldObstacle = {
        id: allocObstacleId(),
        chunkIndex,
        kind: 'shear_rock',
        // Hidden far below floor while hanging; snaps onto floor the instant it impales!
        position: new THREE.Vector3(fx, floorY - 500, fz),
        radius: isPointy ? radXZ * 0.82 : radXZ * 0.92,
        height: isPointy ? lenY * 0.72 : radXZ * 1.9,
        canShearVine: true,
      };
      obstacles.push(obs);

      fallingIcicles.push({
        id: this.nextIcicleId++,
        kind: isPointy ? 'POINTY_ICICLE' : 'ICE_BLOCK',
        ceilPos: new THREE.Vector3(fx, ceilY, fz),
        floorY,
        currentY: ceilY,
        vy: 0,
        lengthY: lenY,
        radiusXZ: radXZ,
        shudderTimer: 0,
        shudderDuration: lerp(0.28, 0.52, rng()),
        triggerDist: lerp(42.0, 96.0, rng()),
        state: 'HANGING',
        mesh,
        shadowWarnRing: warnRing,
        obstacle: obs,
        spawnedImpactFracture: false,
      });
    }
  }

  /**
   * Populates a 112m Glacial Ice chunk:
   * - Alternating Wide-Open Slippery Frozen Lake Flats (even chunks) & Towering Icicle Caves (odd chunks)!
   * - Multi-zone live-propagating bottom ice fractures across different areas where the player drives!
   */
  public populateGlacialIceChunk(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    chunkLength: number,
    rng: () => number,
    groundSampler: (x: number, z: number) => number,
    specSampler: (z: number) => { centerX: number; halfWidth: number; baseY: number },
    obstacles: WorldObstacle[],
    allocObstacleId: () => number
  ): GlacialIceChunkData {
    const fractures: TravelingIceFracture[] = [];
    const fallingIcicles: FallingCaveIcicle[] = [];

    // Chunk 0 starts as an open frozen flat looking straight ahead into the towering Ice Cave mouth at Chunk 1 (-112m)!
    const isCaveChunk = chunkIndex >= 1 && chunkIndex % 2 === 1;

    // Glassy blue specular ice-sheen patches across the flat ice plane
    for (let s = 0; s < 4; s++) {
      const sz = zStart - ((s + 0.25) / 4) * chunkLength;
      const sp = specSampler(sz);
      const discGeo = new THREE.CircleGeometry(1.0, 20);
      discGeo.rotateX(-Math.PI * 0.5);
      const disc = new THREE.Mesh(discGeo, this.glassyIceSheenMat);
      disc.position.set(
        sp.centerX + (rng() - 0.5) * 24.0,
        groundSampler(sp.centerX, sz) + 0.03,
        sz
      );
      disc.scale.set(lerp(14, 24, rng()), 1, lerp(18, 30, rng()));
      disc.renderOrder = 2;
      disc.userData.uniqueGeo = true;
      chunkGroup.add(disc);
    }

    if (isCaveChunk) {
      this.buildGlacialIceCaveAndIcicles(
        chunkGroup,
        fallingIcicles,
        obstacles,
        chunkIndex,
        zStart,
        chunkLength,
        rng,
        specSampler,
        groundSampler,
        allocObstacleId
      );
    } else {
      // Open Frozen Lake Sheet: Jagged turquoise glacier seracs & overhanging ice-arches on the outer perimeter,
      // plus 4 overhanging glacier-shelf icicle drops near the edges!
      const dummy = new THREE.Object3D();
      const seracMats: THREE.Matrix4[] = [];
      for (let g = 0; g < 22; g++) {
        const gz = zStart - rng() * chunkLength;
        const sp = specSampler(gz);
        const side = g % 2 === 0 ? -1 : 1;
        const gx = sp.centerX + side * lerp(sp.halfWidth + 8.0, 96.0, rng());
        const gy = groundSampler(gx, gz);
        dummy.position.set(gx, gy + lerp(6.0, 18.0, rng()), gz);
        dummy.rotation.set(Math.PI, rng() * Math.PI * 2, (rng() - 0.5) * 0.28);
        dummy.scale.set(lerp(3.5, 8.5, rng()), lerp(12.0, 32.0, rng()), lerp(3.5, 8.5, rng()));
        dummy.updateMatrix();
        seracMats.push(dummy.matrix.clone());
      }
      if (seracMats.length > 0) {
        const im = new THREE.InstancedMesh(
          this.staticSmallIcicleGeo,
          this.glacierBlockMat,
          seracMats.length
        );
        seracMats.forEach((m, idx) => im.setMatrixAt(idx, m));
        im.instanceMatrix.needsUpdate = true;
        chunkGroup.add(im);
      }
    }

    // Seed 6 Multi-Zone Traveling Bottom Ice Fractures across different areas (Left, Center, Right & Diagonal!)
    if (chunkIndex >= 0) {
      const faultCount = 6;
      for (let f = 0; f < faultCount; f++) {
        const fz = zStart - ((f + 0.12 + rng() * 0.65) / faultCount) * chunkLength;
        if (Math.abs(fz) < 18) continue;
        const sp = specSampler(fz);

        // Spread origins across Left, Right, and Center driving lanes!
        const laneBias = f % 3 === 0 ? -0.68 : f % 3 === 1 ? 0.68 : (rng() - 0.5) * 0.4;
        const ox = sp.centerX + laneBias * sp.halfWidth;
        // Aim diagonally across the player's driving corridor (-Z & across X)!
        const aimAng =
          -Math.PI * 0.5 + (-laneBias * 0.85 + (rng() - 0.5) * 0.55);

        fractures.push(
          this.createTravelingFracture(
            chunkGroup,
            ox,
            fz,
            aimAng,
            lerp(44.0, 78.0, rng()),
            lerp(42.0, 82.0, rng()),
            f === 0 && chunkIndex === 0, // Start one fracture immediately visible near spawn!
            rng
          )
        );
      }
    }

    return {
      chunkIndex,
      zStart,
      isCaveChunk,
      fractures,
      fallingIcicles,
    };
  }

  /**
   * Advances all live-propagating bottom ice fractures (at randomly accelerating & slowing speeds!),
   * dynamically spawns fresh wheel-stress fractures where the player drives,
   * and drops razor-sharp pointy cave icicles & ice blocks!
   */
  public stepChunkHazards(
    chunkGroup: THREE.Group,
    data: GlacialIceChunkData,
    elapsedTime: number,
    dt: number,
    focusPos: THREE.Vector3,
    vehiclePos: THREE.Vector3,
    vehicleVel: THREE.Vector3,
    vehicleSpeedMps: number,
    passiveObserver: boolean,
    groundSampler: (x: number, z: number) => number
  ): void {
    this.timeUniform.value = elapsedTime;
    this.crackJoltCooldown = Math.max(0, this.crackJoltCooldown - dt);

    // A. Dynamic Player-Driven Ice Fractures!
    // Whenever the player drives across this chunk's ice sheet, spawn fresh traveling fractures right in their area!
    const zEnd = data.zStart - 112;
    if (
      !passiveObserver &&
      vehiclePos.z <= data.zStart &&
      vehiclePos.z > zEnd &&
      vehicleSpeedMps > 6.0 &&
      data.fractures.length < 11
    ) {
      this.playerStressTimer -= dt;
      if (this.playerStressTimer <= 0) {
        const dynRng = mulberry32(
          (Math.floor(elapsedTime * 1000) ^ (data.chunkIndex * 7919)) >>> 0
        );
        this.playerStressTimer = lerp(1.6, 3.1, dynRng());

        // Nucleate right under/beside or slightly ahead of where the player is driving!
        const spawnX = vehiclePos.x + (dynRng() - 0.5) * 14.0;
        const spawnZ = vehiclePos.z - lerp(2.0, 18.0, dynRng());
        const travelAng = -Math.PI * 0.5 + (dynRng() - 0.5) * 1.35;

        const liveFrac = this.createTravelingFracture(
          chunkGroup,
          spawnX,
          spawnZ,
          travelAng,
          lerp(42.0, 72.0, dynRng()),
          999,
          true,
          dynRng
        );
        data.fractures.push(liveFrac);
        if (this.onIceFractureBurst) {
          this.onIceFractureBurst(
            new THREE.Vector3(spawnX, groundSampler(spawnX, spawnZ), spawnZ)
          );
        }
      }
    }

    // B. Step Every Traveling Ice Fracture at a RANDOM ACCELERATING & DECELERATING RATE!
    for (const fr of data.fractures) {
      if (!fr.triggered) {
        const dFocus = Math.hypot(focusPos.x - fr.origin.x, focusPos.z - fr.origin.y);
        const dCar = Math.hypot(vehiclePos.x - fr.origin.x, vehiclePos.z - fr.origin.y);
        if (Math.min(dFocus, dCar) < fr.triggerRadius) {
          fr.triggered = true;
          fr.geometryDirty = true;
          if (this.onIceFractureBurst) {
            this.onIceFractureBurst(
              new THREE.Vector3(fr.origin.x, groundSampler(fr.origin.x, fr.origin.y), fr.origin.y)
            );
          }
        }
      }

      if (!fr.triggered) continue;

      const stillGrowing = fr.mainBranch.revealedLength < fr.mainBranch.totalLength;
      let anyOffshootGrowing = false;
      for (const off of fr.offshoots) {
        if (
          fr.mainBranch.revealedLength >= off.spawnAtMainDist &&
          off.revealedLength < off.totalLength
        ) {
          anyOffshootGrowing = true;
        }
      }

      if (stillGrowing || anyOffshootGrowing) {
        // Randomly alternate between rapid crack-burst acceleration (35..66 m/s) and slow groaning creep (2.2..8.5 m/s)!
        fr.speedChangeTimer -= dt;
        if (fr.speedChangeTimer <= 0) {
          fr.rngState = (fr.rngState * 1664525 + 1013904223) >>> 0;
          const r1 = fr.rngState / 0xffffffff;
          fr.rngState = (fr.rngState * 1664525 + 1013904223) >>> 0;
          const r2 = fr.rngState / 0xffffffff;

          // Alternate burst vs slowdown!
          const goFast = fr.targetSpeedMps < 18.0 ? r1 < 0.78 : r1 < 0.32;
          fr.targetSpeedMps = goFast ? lerp(35.0, 66.0, r2) : lerp(2.2, 8.5, r2);
          fr.speedChangeTimer = goFast ? lerp(0.16, 0.42, r1) : lerp(0.28, 0.68, r1);
        }

        fr.currentSpeedMps = lerp(fr.currentSpeedMps, fr.targetSpeedMps, clamp(8.5 * dt, 0, 1));
        const microPulse = 0.75 + 0.35 * Math.sin(elapsedTime * 19.0 + fr.id * 3.1);
        const stepDist = fr.currentSpeedMps * microPulse * dt;

        if (stillGrowing) {
          fr.mainBranch.revealedLength = Math.min(
            fr.mainBranch.totalLength,
            fr.mainBranch.revealedLength + stepDist
          );
          fr.tipGlowMesh.visible = true;
          const sparkScale = clamp(fr.currentSpeedMps / 28.0, 0.45, 2.1);
          fr.tipGlowMesh.scale.setScalar(sparkScale);
        } else {
          fr.tipGlowMesh.visible = false;
        }

        for (const off of fr.offshoots) {
          if (
            fr.mainBranch.revealedLength >= off.spawnAtMainDist &&
            off.revealedLength < off.totalLength
          ) {
            off.revealedLength = Math.min(
              off.totalLength,
              off.revealedLength + stepDist * off.speedScale
            );
          }
        }

        fr.geometryDirty = true;
      } else {
        fr.tipGlowMesh.visible = false;
      }

      if (fr.geometryDirty) {
        this.updateFractureGeometry(fr, groundSampler);
        fr.geometryDirty = false;
      }

      // Check if driving vehicle crosses an opened ice fracture seam -> crisp ice-trench jolt & drift kick!
      if (!passiveObserver && this.crackJoltCooldown <= 0 && fr.mainBranch.revealedLength > 6.0) {
        let accum = 0;
        for (let s = 0; s < fr.mainBranch.segLengths.length; s += 2) {
          if (accum + 3.0 > fr.mainBranch.revealedLength) break;
          const pt = fr.mainBranch.points[s];
          const dSq =
            (vehiclePos.x - pt.x) * (vehiclePos.x - pt.x) +
            (vehiclePos.z - pt.y) * (vehiclePos.z - pt.y);
          if (dSq < 2.1 * 2.1 && Math.abs(vehiclePos.y - groundSampler(pt.x, pt.y)) < 1.8) {
            this.crackJoltCooldown = 0.55;
            // Jolt chassis across the opened ice fissure!
            vehicleVel.y = Math.max(vehicleVel.y, 2.35);
            vehicleVel.x += (pt.x > vehiclePos.x ? -1 : 1) * 2.4;
            if (this.onVehicleCrossOpenCrack) {
              this.onVehicleCrossOpenCrack(vehiclePos.clone());
            }
            break;
          }
          accum += fr.mainBranch.segLengths[s] + (fr.mainBranch.segLengths[s + 1] || 0);
        }
      }
    }

    // C. Step Falling Pointy Cave Icicles (Stalactites) & Heavy Calving Ice Blocks!
    for (const ic of data.fallingIcicles) {
      if (ic.state === 'HANGING') {
        const dzFocus = focusPos.z - ic.ceilPos.z;
        const dzCar = vehiclePos.z - ic.ceilPos.z;
        const activeDz = passiveObserver ? dzFocus : dzCar;
        if (activeDz > -6.0 && activeDz < ic.triggerDist) {
          ic.state = 'SHUDDERING';
          ic.shudderTimer = 0;
          ic.mesh.material = this.shudderIcicleMat;
          ic.shadowWarnRing.visible = true;
        }
      } else if (ic.state === 'SHUDDERING') {
        ic.shudderTimer += dt;
        // High-frequency crystalline vibration overhead before snapping loose!
        ic.mesh.position.set(
          ic.ceilPos.x + Math.sin(elapsedTime * 58.0 + ic.id) * 0.22,
          ic.ceilPos.y,
          ic.ceilPos.z + Math.cos(elapsedTime * 52.0 + ic.id) * 0.22
        );
        const pulse = 1.0 + Math.sin(ic.shudderTimer * 24.0) * 0.25;
        ic.shadowWarnRing.scale.setScalar(ic.radiusXZ * 1.9 * pulse);

        if (ic.shudderTimer >= ic.shudderDuration) {
          ic.state = 'FALLING';
          ic.mesh.position.set(ic.ceilPos.x, ic.ceilPos.y, ic.ceilPos.z);
          ic.mesh.material =
            ic.kind === 'POINTY_ICICLE' ? this.crystalIcicleMat : this.glacierBlockMat;
          ic.vy = -3.5;
        }
      } else if (ic.state === 'FALLING') {
        ic.vy -= 28.5 * dt;
        ic.currentY += ic.vy * dt;

        // Pointy icicle tip is at currentY - lengthY; impale 28% into the solid ice floor!
        const stopY =
          ic.kind === 'POINTY_ICICLE'
            ? ic.floorY + ic.lengthY * 0.72
            : ic.floorY + ic.radiusXZ * 0.78;

        if (ic.kind === 'ICE_BLOCK') {
          ic.mesh.rotation.x += 1.8 * dt;
          ic.mesh.rotation.z += 1.4 * dt;
        }

        if (ic.currentY <= stopY) {
          ic.currentY = stopY;
          ic.state = 'IMPALED';
          ic.shadowWarnRing.visible = false;
          // Activate live obstacle collider on the flat ice road!
          ic.obstacle.position.set(ic.ceilPos.x, ic.floorY, ic.ceilPos.z);

          const impactWorld = new THREE.Vector3(ic.ceilPos.x, ic.floorY, ic.ceilPos.z);
          if (this.onIcicleShatterSlam) {
            this.onIcicleShatterSlam(impactWorld, ic.kind);
          }

          // Impacting stalactite/block shatters the ice floor, spawning a brand-new live traveling fracture!
          if (!ic.spawnedImpactFracture && data.fractures.length < 13) {
            ic.spawnedImpactFracture = true;
            const impRng = mulberry32((ic.id * 2654435761) >>> 0);
            data.fractures.push(
              this.createTravelingFracture(
                chunkGroup,
                ic.ceilPos.x,
                ic.ceilPos.z,
                impRng() * Math.PI * 2,
                lerp(32.0, 56.0, impRng()),
                999,
                true,
                impRng
              )
            );
          }
        }
        ic.mesh.position.y = ic.currentY;
      }
    }
  }

  /**
   * Steps blowing sub-zero spindrift snow particles across the flat ice!
   */
  public stepGlobalIceAtmosphere(
    active: boolean,
    focusPos: THREE.Vector3,
    dt: number
  ): void {
    this.atmosphereFxGroup.visible = active;
    if (!active) return;

    const halfX = 70;
    const halfZ = 80;
    for (let i = 0; i < this.snowCount; i++) {
      const ix = i * 3;
      let px = this.snowPositions[ix] + this.snowVelocities[ix] * dt;
      let py = this.snowPositions[ix + 1] + this.snowVelocities[ix + 1] * dt;
      let pz = this.snowPositions[ix + 2] + this.snowVelocities[ix + 2] * dt;

      if (px - focusPos.x > halfX) px -= halfX * 2;
      else if (px - focusPos.x < -halfX) px += halfX * 2;

      if (pz - focusPos.z > halfZ) pz -= halfZ * 2;
      else if (pz - focusPos.z < -halfZ) pz += halfZ * 2;

      if (py - focusPos.y > 22) py -= 28;
      else if (py - focusPos.y < -6) py += 28;

      this.snowPositions[ix] = px;
      this.snowPositions[ix + 1] = py;
      this.snowPositions[ix + 2] = pz;
    }
    (this.snowPoints.geometry.attributes.position as THREE.BufferAttribute).needsUpdate = true;
  }
}

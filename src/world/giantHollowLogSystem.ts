import * as THREE from 'three';
import { clamp, fbm2D, lerp, mulberry32, smoothstep } from '../math/noise';
import { ProceduralTextureLibrary } from '../graphics/proceduralTextures';
import { WorldObstacle } from './trailCorridor';

export interface StationaryLogWorm {
  id: number;
  boreHolePos: THREE.Vector3;
  emergeDir: THREE.Vector3;
  lateralTang: THREE.Vector3;
  maxReach: number;
  baseRadius: number;
  phase: number;
  state: 'PEEKING' | 'LUNGING' | 'LATCHED' | 'RETRACTING';
  extension: number; // 0..1 of maxReach
  stateTimer: number;
  strikeCooldown: number;
  group: THREE.Group;
  segmentMeshes: THREE.Mesh[];
  headGroup: THREE.Group;
  mandibleMeshes: THREE.Mesh[];
  currentTipWorld: THREE.Vector3;
}

export interface MessyCobwebCluster {
  id: number;
  center: THREE.Vector3;
  halfWidthX: number;
  halfDepthZ: number;
  heightY: number;
  group: THREE.Group;
  curtainGroup: THREE.Group;
  stretchOffset: number;
  tearProgress: number; // 0 = intact, 1 = torn open (slowly regrows)
  swayPhase: number;
}

export interface GiantHollowLogChunkData {
  worms: StationaryLogWorm[];
  cobwebs: MessyCobwebCluster[];
}

interface SkylightHoleSpec {
  zCenter: number;
  zHalfLen: number;
  thetaCenter: number; // radians: 0 = right (+X), PI/2 = top (+Y), PI = left (-X)
  thetaHalfSpan: number;
}

const VAULT_RADIUS_X = 39.5;
const VAULT_RADIUS_Y = 34.0;
const WORM_SEGMENT_COUNT = 11;

export class GiantHollowLogBuilder {
  public readonly innerHeartwoodMat: THREE.MeshStandardMaterial;
  public readonly outerBarkVaultMat: THREE.MeshStandardMaterial;
  public readonly boreHoleCollarMat: THREE.MeshStandardMaterial;
  public readonly shelfFungusTopMat: THREE.MeshStandardMaterial;
  public readonly shelfFungusGlowMat: THREE.MeshStandardMaterial;
  public readonly beardMossMat: THREE.MeshStandardMaterial;
  public readonly wormFleshMat: THREE.MeshStandardMaterial;
  public readonly wormHeadChitinMat: THREE.MeshStandardMaterial;
  public readonly silkStrandMat: THREE.MeshStandardMaterial;
  public readonly cobwebCurtainMat: THREE.ShaderMaterial;
  public readonly godrayBeamMat: THREE.ShaderMaterial;
  public readonly dappledSunPoolMat: THREE.ShaderMaterial;
  public readonly sapAmberWaterMat: THREE.ShaderMaterial;

  // Global drifting golden spores & dust motes inside the hollow log!
  public readonly atmosphereFxGroup = new THREE.Group();
  private sporePoints!: THREE.Points;
  private sporePositions!: Float32Array;
  private sporeVelocities!: Float32Array;
  private readonly sporeCount = 1100;

  private readonly wormSegGeo: THREE.BufferGeometry;
  private readonly wormMandibleGeo: THREE.BufferGeometry;
  private readonly boreCollarGeo: THREE.BufferGeometry;
  private readonly shelfConkGeo: THREE.BufferGeometry;
  private readonly cocoonGeo: THREE.BufferGeometry;
  private readonly timeUniform = { value: 0 };

  private nextWormId = 1;
  private nextWebId = 1;

  public onWormStrikeVehicle?: (worldPos: THREE.Vector3) => void;
  public onCobwebTorn?: (worldPos: THREE.Vector3) => void;

  constructor(textures: ProceduralTextureLibrary) {
    // 1. Warm ancient redwood/cedar inner heartwood with fibrous grain
    this.innerHeartwoodMat = new THREE.MeshStandardMaterial({
      color: 0x6e4127,
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      normalScale: new THREE.Vector2(1.45, 1.45),
      roughness: 0.84,
      metalness: 0.03,
      vertexColors: true,
      side: THREE.DoubleSide,
    });

    // 2. Dark mossy ancient bark ribs & gnarled wood knots
    this.outerBarkVaultMat = new THREE.MeshStandardMaterial({
      color: 0x3b2a1d,
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      normalScale: new THREE.Vector2(1.75, 1.75),
      roughness: 0.92,
      metalness: 0.02,
    });

    // 3. Charred/rotted woodworm bore-hole socket in log wall
    this.boreHoleCollarMat = new THREE.MeshStandardMaterial({
      color: 0x1c120c,
      map: textures.bark.map,
      normalMap: textures.rock.normalMap,
      roughness: 0.95,
      metalness: 0.04,
    });

    // 4. Giant bracket shelf-fungi (warm banded ochre-russet upper cap + glowing amber-jade gills)
    this.shelfFungusTopMat = new THREE.MeshStandardMaterial({
      color: 0xb86b35,
      map: textures.bark.map,
      normalMap: textures.terrainDetail.normalMap,
      roughness: 0.72,
      metalness: 0.04,
    });

    this.shelfFungusGlowMat = new THREE.MeshStandardMaterial({
      color: 0x9be89d,
      roughness: 0.45,
      metalness: 0.05,
      emissive: new THREE.Color(0x4bc96b),
      emissiveIntensity: 0.62,
    });

    // 5. Hanging interior moss & emerald lichen curtains around skylight holes
    this.beardMossMat = new THREE.MeshStandardMaterial({
      color: 0x4f6e32,
      map: textures.terrainDetail.map,
      roughness: 0.88,
      metalness: 0.02,
      side: THREE.DoubleSide,
    });

    // 6. Glistening segmented wood-parasite worm flesh & dark chitin mandibles
    this.wormFleshMat = new THREE.MeshStandardMaterial({
      color: 0xd9a17c,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.85, 0.85),
      roughness: 0.24, // Wet glistening mucus sheen!
      metalness: 0.08,
      emissive: new THREE.Color(0x3b180e),
      emissiveIntensity: 0.22,
    });

    this.wormHeadChitinMat = new THREE.MeshStandardMaterial({
      color: 0x291411,
      roughness: 0.28,
      metalness: 0.35,
      emissive: new THREE.Color(0x4a1208),
      emissiveIntensity: 0.28,
    });

    // 7. Thick structural cobweb anchor silk cables
    this.silkStrandMat = new THREE.MeshStandardMaterial({
      color: 0xedeae2,
      roughness: 0.55,
      metalness: 0.05,
      emissive: new THREE.Color(0x38362e),
      emissiveIntensity: 0.25,
    });

    // 8. Procedural Messy Tangled Cobweb Curtain Shader (fibrous crisscrossed threads + tattered holes!)
    this.cobwebCurtainMat = new THREE.ShaderMaterial({
      transparent: true,
      depthWrite: false,
      side: THREE.DoubleSide,
      uniforms: {
        uTime: this.timeUniform,
      },
      vertexShader: `
        varying vec2 vUv;
        varying vec3 vWorldPos;
        uniform float uTime;
        void main() {
          vUv = uv;
          vec3 pos = position;
          // Gentle draft billow in the hollow log air currents
          float edgePin = sin(uv.x * 3.14159) * sin(uv.y * 3.14159);
          pos.z += sin(uTime * 1.8 + uv.x * 6.0 + uv.y * 4.0) * 0.38 * edgePin;
          vec4 wp = modelMatrix * vec4(pos, 1.0);
          vWorldPos = wp.xyz;
          gl_Position = projectionMatrix * viewMatrix * wp;
        }
      `,
      fragmentShader: `
        varying vec2 vUv;
        varying vec3 vWorldPos;
        uniform float uTime;

        float hash21(vec2 p) {
          p = fract(p * vec2(123.34, 456.21));
          p += dot(p, p + 45.32);
          return fract(p.x * p.y);
        }

        void main() {
          vec2 centered = vUv * 2.0 - 1.0;
          float r = length(centered);
          float ang = atan(centered.y, centered.x);

          // Irregular radial spokes + messy off-center spiral & diagonal cross-tangles!
          float spokes1 = abs(sin(ang * 17.0 + sin(r * 9.0) * 0.45));
          float spokes2 = abs(sin(ang * 29.0 - r * 5.0 + 1.3));
          float spiral1 = abs(sin(r * 26.0 + sin(ang * 7.0) * 1.4));
          float spiral2 = abs(sin(r * 41.0 - cos(ang * 11.0) * 1.9));
          float diag1 = abs(sin((vUv.x * 1.3 + vUv.y * 0.9) * 38.0 + sin(vUv.y * 15.0) * 1.6));
          float diag2 = abs(sin((vUv.x * 0.8 - vUv.y * 1.4) * 33.0 + cos(vUv.x * 19.0) * 1.5));

          float webLines =
            smoothstep(0.86, 0.995, 1.0 - spokes1) * 0.75 +
            smoothstep(0.90, 0.998, 1.0 - spokes2) * 0.55 +
            smoothstep(0.84, 0.992, 1.0 - spiral1) * 0.72 +
            smoothstep(0.88, 0.996, 1.0 - spiral2) * 0.52 +
            smoothstep(0.88, 0.995, 1.0 - diag1) * 0.60 +
            smoothstep(0.89, 0.995, 1.0 - diag2) * 0.50;

          // Dusty matted silk clumps + ragged torn holes so players can see through the messy web!
          float clump = sin(vUv.x * 14.0 + sin(vUv.y * 9.0) * 2.2) * cos(vUv.y * 12.0 - vUv.x * 5.0);
          float holeMask = smoothstep(-0.32, 0.25, clump);
          float mattedSheet = smoothstep(0.25, 0.88, clump) * 0.34;

          // Scalloped outer border so web looks stretched between anchor points, never rectangular!
          float scallop = 0.88 + 0.14 * sin(ang * 9.0);
          float borderFade = smoothstep(scallop + 0.12, scallop - 0.28, r);

          float alpha = clamp((webLines + mattedSheet) * holeMask * borderFade, 0.0, 0.88);
          if (alpha < 0.04) discard;

          // Warm golden-ivory silk catching backlit godrays
          vec3 silkCol = mix(vec3(0.82, 0.80, 0.74), vec3(1.0, 0.98, 0.90), clamp(webLines, 0.0, 1.0));
          gl_FragColor = vec4(silkCol, alpha);
        }
      `,
    });

    // 9. Volumetric Outside Godray Sunbeam Shafts pouring through skylight knot-holes!
    this.godrayBeamMat = new THREE.ShaderMaterial({
      transparent: true,
      depthWrite: false,
      side: THREE.DoubleSide,
      blending: THREE.AdditiveBlending,
      uniforms: {
        uTime: this.timeUniform,
      },
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
          // Soft cylindrical silhouette fade so beam has zero hard edges
          float facing = abs(dot(normalize(vNormalView), normalize(vViewDir)));
          float softEdge = pow(facing, 1.55);

          // Brightest near the skylight hole at top (vUv.y = 1), softly diffusing toward floor (vUv.y = 0)
          float verticalFade = smoothstep(0.0, 0.16, vUv.y) * (0.42 + 0.58 * pow(vUv.y, 0.75));

          // Shimmering forest-canopy light rays & drifting dust bands
          float rays = 0.68 + 0.32 * sin(vUv.x * 25.1327 + uTime * 0.55 + sin(vUv.y * 8.0) * 1.2);
          float moteShimmer = 0.85 + 0.15 * sin(vUv.y * 32.0 - uTime * 1.4 + vUv.x * 12.56);

          vec3 topGold = vec3(1.0, 0.92, 0.62);
          vec3 midEmeraldGold = vec3(0.95, 0.82, 0.44);
          vec3 col = mix(midEmeraldGold, topGold, vUv.y);

          float alpha = softEdge * verticalFade * rays * moteShimmer * 0.24;
          gl_FragColor = vec4(col, alpha);
        }
      `,
    });

    // 10. Dappled golden sunlight pool on the damp heartwood floor directly beneath each godray!
    this.dappledSunPoolMat = new THREE.ShaderMaterial({
      transparent: true,
      depthWrite: false,
      blending: THREE.AdditiveBlending,
      uniforms: {
        uTime: this.timeUniform,
      },
      vertexShader: `
        varying vec2 vUv;
        void main() {
          vUv = uv;
          gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
        }
      `,
      fragmentShader: `
        varying vec2 vUv;
        uniform float uTime;
        void main() {
          vec2 c = vUv * 2.0 - 1.0;
          float r = length(c);
          float radial = smoothstep(1.0, 0.1, r);
          float dapple = 0.65 + 0.35 * sin(c.x * 9.0 + uTime * 0.7) * cos(c.y * 9.0 - uTime * 0.5);
          vec3 col = vec3(1.0, 0.86, 0.48);
          gl_FragColor = vec4(col, radial * dapple * 0.36);
        }
      `,
    });

    // 11. Rich golden-amber tree-sap & rot-water pools in floor hollows
    this.sapAmberWaterMat = new THREE.ShaderMaterial({
      transparent: true,
      depthWrite: false,
      uniforms: {
        uTime: this.timeUniform,
      },
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
          float dist = length(vUv - vec2(0.5)) * 2.0;
          float edge = smoothstep(1.0, 0.68, dist);
          float ripple = sin(vWorldPos.x * 2.6 + uTime * 1.9) * cos(vWorldPos.z * 2.4 - uTime * 1.5);
          vec3 deepSap = vec3(0.24, 0.12, 0.04);
          vec3 amberGlow = vec3(0.78, 0.48, 0.14);
          vec3 col = mix(deepSap, amberGlow, 0.45 + 0.25 * ripple);
          gl_FragColor = vec4(col, edge * 0.84);
        }
      `,
    });

    // Shared geometries for worms, shelf-fungi & cocoons
    const segGeo = new THREE.SphereGeometry(1.0, 12, 10);
    segGeo.scale(1.0, 0.92, 0.82);
    this.wormSegGeo = segGeo;

    const mandGeo = new THREE.ConeGeometry(0.22, 1.05, 6);
    mandGeo.rotateX(Math.PI * 0.38);
    mandGeo.translate(0, 0.18, 0.48);
    this.wormMandibleGeo = mandGeo;

    const collarGeo = new THREE.TorusGeometry(1.45, 0.52, 8, 14);
    this.boreCollarGeo = collarGeo;

    const conkGeo = new THREE.CylinderGeometry(1.0, 0.55, 0.38, 14, 1, false, 0, Math.PI * 1.15);
    conkGeo.rotateY(-Math.PI * 0.575);
    this.shelfConkGeo = conkGeo;

    const cocGeo = new THREE.SphereGeometry(0.55, 8, 8);
    cocGeo.scale(0.72, 1.65, 0.72);
    this.cocoonGeo = cocGeo;

    this.initSporeMotes();
  }

  private initSporeMotes(): void {
    this.sporePositions = new Float32Array(this.sporeCount * 3);
    this.sporeVelocities = new Float32Array(this.sporeCount * 3);
    const colors = new Float32Array(this.sporeCount * 3);
    const rng = mulberry32(448812);

    for (let i = 0; i < this.sporeCount; i++) {
      const ix = i * 3;
      this.sporePositions[ix] = (rng() - 0.5) * 86;
      this.sporePositions[ix + 1] = rng() * 34;
      this.sporePositions[ix + 2] = (rng() - 0.5) * 160;

      this.sporeVelocities[ix] = (rng() - 0.5) * 1.4;
      this.sporeVelocities[ix + 1] = lerp(-0.35, 0.65, rng());
      this.sporeVelocities[ix + 2] = (rng() - 0.5) * 1.6;

      // Mix of warm golden dust motes & pale bioluminescent green fungal spores!
      const isGreenSpore = rng() < 0.38;
      colors[ix] = isGreenSpore ? 0.62 : 1.0;
      colors[ix + 1] = isGreenSpore ? 0.96 : 0.88;
      colors[ix + 2] = isGreenSpore ? 0.58 : 0.52;
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(this.sporePositions, 3));
    geo.setAttribute('color', new THREE.BufferAttribute(colors, 3));

    const mat = new THREE.PointsMaterial({
      size: 0.42,
      vertexColors: true,
      transparent: true,
      opacity: 0.68,
      depthWrite: false,
      blending: THREE.AdditiveBlending,
    });

    this.sporePoints = new THREE.Points(geo, mat);
    this.sporePoints.frustumCulled = false;
    this.atmosphereFxGroup.add(this.sporePoints);
  }

  /**
   * Builds the colossal overhead & side hollow-log wooden vault shell for this 112m chunk,
   * complete with jagged skylight knot-hole breaches & volumetric godrays slanting from outside!
   */
  private buildHollowLogVaultAndGodrays(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    chunkLength: number,
    rng: () => number,
    specSampler: (z: number) => { centerX: number; baseY: number; slopeX: number },
    groundSampler: (x: number, z: number) => number
  ): void {
    // Choose 4 natural skylight breaches / split bark cracks along this 112m log section!
    const skylights: SkylightHoleSpec[] = [];
    const holeCount = 4;
    for (let h = 0; h < holeCount; h++) {
      const zCenter = zStart - ((h + 0.22 + rng() * 0.52) / holeCount) * chunkLength;
      const sideBias = h % 2 === 0 ? lerp(1.12, 1.52, rng()) : lerp(1.62, 2.04, rng());
      skylights.push({
        zCenter,
        zHalfLen: lerp(5.2, 8.8, rng()),
        thetaCenter: sideBias, // Upper vault crown (PI/2 = 1.57 rad is straight overhead)
        thetaHalfSpan: lerp(0.18, 0.30, rng()),
      });
    }

    const zSegs = 28; // 4m longitudinal steps
    const aSegs = 30; // Smooth cylindrical arch from right base (-0.08) over top (PI/2) to left base (PI+0.08)
    const thetaMin = -0.08;
    const thetaMax = Math.PI + 0.08;

    const cols = aSegs + 1;
    const rows = zSegs + 1;
    const positions = new Float32Array(cols * rows * 3);
    const colors = new Float32Array(cols * rows * 3);
    const uvs = new Float32Array(cols * rows * 2);
    const indices: number[] = [];

    const colHeart = new THREE.Color(0x7a482b);
    const colRot = new THREE.Color(0x3a2518);
    const colMoss = new THREE.Color(0x3c5229);
    const colSunRim = new THREE.Color(0xc99652);
    const tmpCol = new THREE.Color();

    const isInsideSkylight = (z: number, theta: number): number => {
      let maxIn = 0;
      for (const sk of skylights) {
        const nz = (z - sk.zCenter) / sk.zHalfLen;
        const nt = (theta - sk.thetaCenter) / sk.thetaHalfSpan;
        const jag = Math.sin(z * 0.55 + theta * 9.0) * 0.16;
        const d = nz * nz + nt * nt + jag;
        if (d < 1.0) maxIn = Math.max(maxIn, 1.0 - d);
      }
      return maxIn;
    };

    let pPtr = 0;
    let uvPtr = 0;
    for (let rz = 0; rz < rows; rz++) {
      const tz = rz / zSegs;
      const z = zStart - tz * chunkLength;
      const sp = specSampler(z);

      // Periodic annular growth-ring ribs bulging inward every ~28m!
      const ribBulge = Math.pow(Math.sin(z * 0.225) * 0.5 + 0.5, 4.0) * 2.35;

      for (let ca = 0; ca < cols; ca++) {
        const ta = ca / aSegs;
        const theta = lerp(thetaMin, thetaMax, ta);

        const grainFlute =
          Math.sin(theta * 22.0 + fbm2D(z * 0.06, theta * 3.0, 2) * 2.2) * 1.15 +
          fbm2D(z * 0.09, theta * 4.5, 2) * 1.45;
        const rx = VAULT_RADIUS_X - ribBulge + grainFlute;
        const ry = VAULT_RADIUS_Y - ribBulge * 0.85 + grainFlute * 0.85;

        const vx = sp.centerX + Math.cos(theta) * rx;
        const vy = sp.baseY - 1.5 + Math.sin(theta) * ry;

        positions[pPtr] = vx;
        positions[pPtr + 1] = vy;
        positions[pPtr + 2] = z;

        uvs[uvPtr++] = ta * 8.0;
        uvs[uvPtr++] = (z * 0.08);

        // Rich woody heartwood + mossy damp patches + warm sunlit rim around skylight holes!
        const mossPatch = clamp(fbm2D(z * 0.08 + 11.2, theta * 3.5, 2) * 0.5 + 0.45, 0, 1);
        const skyProx = isInsideSkylight(z, theta);
        tmpCol.copy(colHeart).lerp(colRot, clamp(ribBulge * 0.32, 0, 0.75));
        tmpCol.lerp(colMoss, smoothstep(0.52, 0.85, mossPatch) * 0.58);
        if (skyProx > 0.01) {
          tmpCol.lerp(colSunRim, clamp(skyProx * 1.8, 0, 0.85));
        }

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
        // Punch genuine jagged skylight openings right out of the upper wooden log roof!
        if (isInsideSkylight(zMid, thetaMid) > 0.24) continue;

        const a = rz * cols + ca;
        const b = a + 1;
        const c = a + cols;
        const d = c + 1;
        indices.push(a, c, b, b, c, d);
      }
    }

    const vaultGeo = new THREE.BufferGeometry();
    vaultGeo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    vaultGeo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
    vaultGeo.setAttribute('uv', new THREE.BufferAttribute(uvs, 2));
    vaultGeo.setIndex(indices);
    vaultGeo.computeVertexNormals();

    const vaultMesh = new THREE.Mesh(vaultGeo, this.innerHeartwoodMat);
    vaultMesh.castShadow = true;
    vaultMesh.receiveShadow = true;
    vaultMesh.userData.uniqueGeo = true;
    chunkGroup.add(vaultMesh);

    // Spawn Volumetric Outside Godrays & Hanging Skylight Moss for every roof opening!
    for (const sk of skylights) {
      const sp = specSampler(sk.zCenter);
      const holeX = sp.centerX + Math.cos(sk.thetaCenter) * (VAULT_RADIUS_X - 0.5);
      const holeY = sp.baseY - 1.5 + Math.sin(sk.thetaCenter) * (VAULT_RADIUS_Y - 0.5);
      const holePos = new THREE.Vector3(holeX, holeY, sk.zCenter);

      // Sunbeams angle along the exact directional sun vector (+42, -78, +34) so volumetric rays match real roof shadows!
      const dropApprox = Math.max(18.0, holeY - sp.baseY);
      const slantX = dropApprox * 0.52;
      const slantZ = dropApprox * 0.42;
      const targetX = clamp(holeX + slantX, sp.centerX - 28.0, sp.centerX + 28.0);
      const targetZ = sk.zCenter + slantZ;
      const floorY = groundSampler(targetX, targetZ);
      const floorPos = new THREE.Vector3(targetX, floorY + 0.06, targetZ);

      const beamVec = new THREE.Vector3().subVectors(floorPos, holePos);
      const beamLen = beamVec.length();
      const midPos = new THREE.Vector3().addVectors(holePos, floorPos).multiplyScalar(0.5);

      const topR = Math.max(2.6, sk.zHalfLen * 0.58);
      const botR = topR * 2.05;

      // Outer golden atmospheric shaft + crisp inner sun-core ray!
      for (let layer = 0; layer < 2; layer++) {
        const scaleR = layer === 0 ? 1.0 : 0.52;
        const rayGeo = new THREE.CylinderGeometry(
          topR * scaleR,
          botR * scaleR,
          beamLen + 4.0,
          16,
          1,
          true
        );
        const rayMesh = new THREE.Mesh(rayGeo, this.godrayBeamMat);
        rayMesh.position.copy(midPos);
        rayMesh.quaternion.setFromUnitVectors(
          new THREE.Vector3(0, -1, 0),
          beamVec.clone().normalize()
        );
        rayMesh.renderOrder = 5;
        rayMesh.userData.uniqueGeo = true;
        chunkGroup.add(rayMesh);
      }

      // Glowing golden dappled sunlight pool where the godray illuminates the heartwood road!
      const poolGeo = new THREE.PlaneGeometry(botR * 2.5, botR * 2.9);
      poolGeo.rotateX(-Math.PI * 0.5);
      const poolMesh = new THREE.Mesh(poolGeo, this.dappledSunPoolMat);
      poolMesh.position.copy(floorPos);
      poolMesh.renderOrder = 3;
      poolMesh.userData.uniqueGeo = true;
      chunkGroup.add(poolMesh);

      // Shaggy hanging forest moss & bark splinters draping down through the skylight hole!
      for (let m = 0; m < 5; m++) {
        const ang = (m / 5) * Math.PI * 2 + rng() * 0.5;
        const mx = holeX + Math.cos(ang) * topR * 0.85;
        const mz = sk.zCenter + Math.sin(ang) * sk.zHalfLen * 0.72;
        const mossH = lerp(3.5, 7.5, rng());
        const mossGeo = new THREE.ConeGeometry(lerp(0.65, 1.35, rng()), mossH, 5);
        mossGeo.rotateX(Math.PI);
        const mossMesh = new THREE.Mesh(mossGeo, this.beardMossMat);
        mossMesh.position.set(mx, holeY - mossH * 0.45, mz);
        mossMesh.userData.uniqueGeo = true;
        chunkGroup.add(mossMesh);
      }
    }

    // Populate Tiered Glowing Bracket Shelf-Fungi along the inner curved wooden walls!
    for (let f = 0; f < 22; f++) {
      const fz = zStart - rng() * chunkLength;
      const sp = specSampler(fz);
      const side = f % 2 === 0 ? 1 : -1;
      // Theta along lower/mid curved walls so players race right past giant glowing mushroom shelves!
      const wallTheta = side > 0 ? lerp(0.08, 0.58, rng()) : lerp(Math.PI - 0.58, Math.PI - 0.08, rng());
      const wx = sp.centerX + Math.cos(wallTheta) * (VAULT_RADIUS_X - 1.4);
      const wy = sp.baseY - 1.5 + Math.sin(wallTheta) * (VAULT_RADIUS_Y - 1.4);

      const clusterCount = 2 + Math.floor(rng() * 3);
      for (let c = 0; c < clusterCount; c++) {
        const rad = lerp(2.2, 5.4, rng()) * (1.0 - c * 0.18);
        const conkGroup = new THREE.Group();
        conkGroup.position.set(wx, wy + c * 1.15, fz + (c - 1) * 1.6);
        conkGroup.rotation.y = side > 0 ? -Math.PI * 0.5 : Math.PI * 0.5;

        const cap = new THREE.Mesh(this.shelfConkGeo, this.shelfFungusTopMat);
        cap.scale.set(rad, lerp(0.8, 1.5, rng()), rad * 0.85);
        cap.castShadow = true;
        conkGroup.add(cap);

        const gills = new THREE.Mesh(this.shelfConkGeo, this.shelfFungusGlowMat);
        gills.scale.set(rad * 0.94, 0.45, rad * 0.8);
        gills.position.y = -0.18;
        conkGroup.add(gills);

        chunkGroup.add(conkGroup);
      }
    }

    // Splintered Rotten Heartwood Core Pillar on wide straightaways (creates dramatic dual lanes inside the log!)
    if (chunkIndex >= 0 && chunkIndex % 2 === 1) {
      const pz = zStart - 0.52 * chunkLength;
      if (Math.abs(pz) > 38) {
        const sp = specSampler(pz);
        const px = sp.centerX + (rng() - 0.5) * 5.0;
        const py = groundSampler(px, pz);
        const pillarH = VAULT_RADIUS_Y * 0.96;
        const pillarGeo = new THREE.CylinderGeometry(2.3, 3.4, pillarH, 10);
        const pillarMesh = new THREE.Mesh(pillarGeo, this.outerBarkVaultMat);
        pillarMesh.position.set(px, py + pillarH * 0.48, pz);
        pillarMesh.rotation.z = (rng() - 0.5) * 0.14;
        pillarMesh.castShadow = true;
        pillarMesh.receiveShadow = true;
        pillarMesh.userData.uniqueGeo = true;
        chunkGroup.add(pillarMesh);
      }
    }
  }

  /**
   * Spawns a Stationary Wood-Borer Parasite Worm rooted inside a dark bore-hole in the log wall/rib!
   * It NEVER walks — it emerges/lunges out from its bore-hole to strike passing vehicles!
   */
  private spawnStationaryLogWorm(
    chunkGroup: THREE.Group,
    worms: StationaryLogWorm[],
    borePos: THREE.Vector3,
    emergeDir: THREE.Vector3,
    maxReach: number,
    baseRadius: number,
    rng: () => number
  ): void {
    const group = new THREE.Group();
    chunkGroup.add(group);

    // Dark splintered bore-hole collar embedded flush into the log wall/ceiling/floor!
    const collar = new THREE.Mesh(this.boreCollarGeo, this.boreHoleCollarMat);
    collar.position.copy(borePos);
    collar.scale.setScalar(baseRadius * 0.95);
    collar.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), emergeDir);
    group.add(collar);

    const segmentMeshes: THREE.Mesh[] = [];
    for (let s = 0; s < WORM_SEGMENT_COUNT; s++) {
      const u = s / (WORM_SEGMENT_COUNT - 1);
      const segRad = baseRadius * lerp(1.08, 0.58, Math.pow(u, 1.3));
      const seg = new THREE.Mesh(this.wormSegGeo, this.wormFleshMat);
      seg.scale.set(segRad, segRad, segRad * 1.15);
      seg.castShadow = true;
      seg.position.copy(borePos);
      group.add(seg);
      segmentMeshes.push(seg);
    }

    // Chitinous 4-mandible lamprey parasite head at tip!
    const headGroup = new THREE.Group();
    headGroup.position.copy(borePos);
    const mandibleMeshes: THREE.Mesh[] = [];
    for (let m = 0; m < 4; m++) {
      const mHolder = new THREE.Group();
      mHolder.rotation.z = (m / 4) * Math.PI * 2 + Math.PI * 0.25;
      const jaw = new THREE.Mesh(this.wormMandibleGeo, this.wormHeadChitinMat);
      jaw.scale.setScalar(baseRadius * 0.95);
      mHolder.add(jaw);
      headGroup.add(mHolder);
      mandibleMeshes.push(jaw);
    }
    group.add(headGroup);

    const upRef = Math.abs(emergeDir.y) > 0.9 ? new THREE.Vector3(1, 0, 0) : new THREE.Vector3(0, 1, 0);
    const lateralTang = new THREE.Vector3().crossVectors(emergeDir, upRef).normalize();

    worms.push({
      id: this.nextWormId++,
      boreHolePos: borePos.clone(),
      emergeDir: emergeDir.clone().normalize(),
      lateralTang,
      maxReach,
      baseRadius,
      phase: rng() * Math.PI * 2,
      state: 'PEEKING',
      extension: 0.12,
      stateTimer: 0,
      strikeCooldown: 0,
      group,
      segmentMeshes,
      headGroup,
      mandibleMeshes,
      currentTipWorld: borePos.clone(),
    });
  }

  /**
   * Spawns a Messy Multi-Layered Cobweb Slalom Shroud spanning part of the log interior
   * so players must weave through the open sunlit gaps!
   */
  private spawnMessyCobwebCluster(
    chunkGroup: THREE.Group,
    cobwebs: MessyCobwebCluster[],
    centerX: number,
    floorY: number,
    centerZ: number,
    halfWidthX: number,
    heightY: number,
    rng: () => number
  ): void {
    const group = new THREE.Group();
    group.position.set(centerX, floorY, centerZ);
    chunkGroup.add(group);

    const curtainGroup = new THREE.Group();
    group.add(curtainGroup);

    // 1. Overlapping crooked gossamer web curtains at messy diagonal angles!
    const sheetCount = 4;
    for (let s = 0; s < sheetCount; s++) {
      const w = halfWidthX * lerp(1.55, 2.15, rng());
      const h = heightY * lerp(0.82, 1.12, rng());
      const planeGeo = new THREE.PlaneGeometry(w, h, 8, 8);
      const sheet = new THREE.Mesh(planeGeo, this.cobwebCurtainMat);
      sheet.position.set(
        (rng() - 0.5) * halfWidthX * 0.45,
        heightY * lerp(0.45, 0.58, rng()),
        (s - 1.5) * 1.35
      );
      sheet.rotation.set(
        (rng() - 0.5) * 0.28,
        (rng() - 0.5) * 0.62,
        (rng() - 0.5) * 0.35
      );
      sheet.renderOrder = 4;
      sheet.userData.uniqueGeo = true;
      curtainGroup.add(sheet);
    }

    // 2. Thick messy structural anchor silk cables radiating out to the log walls, ceiling & floor!
    const strandCount = 14;
    for (let i = 0; i < strandCount; i++) {
      const p0 = new THREE.Vector3(
        (rng() - 0.5) * halfWidthX * 0.65,
        heightY * lerp(0.25, 0.78, rng()),
        (rng() - 0.5) * 2.5
      );
      const ang = (i / strandCount) * Math.PI * 2 + (rng() - 0.5) * 0.35;
      const reachX = Math.cos(ang) * halfWidthX * lerp(1.15, 1.55, rng());
      const reachY = clamp(
        heightY * 0.52 + Math.sin(ang) * heightY * 0.75,
        -0.3,
        heightY * 1.35
      );
      const p1 = new THREE.Vector3(reachX, reachY, (rng() - 0.5) * 5.2);

      const delta = new THREE.Vector3().subVectors(p1, p0);
      const len = delta.length();
      if (len < 0.5) continue;

      const strandGeo = new THREE.CylinderGeometry(0.045, 0.065, len, 5);
      const strandMesh = new THREE.Mesh(strandGeo, this.silkStrandMat);
      strandMesh.position.copy(p0).addScaledVector(delta, 0.5);
      strandMesh.quaternion.setFromUnitVectors(
        new THREE.Vector3(0, 1, 0),
        delta.normalize()
      );
      strandMesh.userData.uniqueGeo = true;
      curtainGroup.add(strandMesh);
    }

    // 3. Suspended silk cocoons / wrapped debris clumps tangled inside the web!
    const cocoonCount = 2 + Math.floor(rng() * 3);
    for (let c = 0; c < cocoonCount; c++) {
      const coc = new THREE.Mesh(this.cocoonGeo, this.silkStrandMat);
      coc.position.set(
        (rng() - 0.5) * halfWidthX * 1.1,
        heightY * lerp(0.32, 0.78, rng()),
        (rng() - 0.5) * 2.0
      );
      coc.rotation.set((rng() - 0.5) * 0.4, rng() * Math.PI, (rng() - 0.5) * 0.4);
      coc.scale.setScalar(lerp(0.85, 1.45, rng()));
      curtainGroup.add(coc);
    }

    cobwebs.push({
      id: this.nextWebId++,
      center: new THREE.Vector3(centerX, floorY, centerZ),
      halfWidthX,
      halfDepthZ: 3.4,
      heightY,
      group,
      curtainGroup,
      stretchOffset: 0,
      tearProgress: 0,
      swayPhase: rng() * Math.PI * 2,
    });
  }

  /**
   * Populates a 112m Giant Hollow Log chunk with:
   * - Colossal curved heartwood vault ceiling + skylight knot-holes + outside godrays + shelf-fungi
   * - Stationary wood-borer parasite worms emerging from bore-holes in the log walls/ribs
   * - Staggered messy cobweb curtains that players must weave through!
   */
  public populateGiantLogChunk(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    chunkLength: number,
    rng: () => number,
    groundSampler: (x: number, z: number) => number,
    specSampler: (z: number) => {
      centerX: number;
      halfWidth: number;
      baseY: number;
      slopeX: number;
    },
    obstacles: WorldObstacle[],
    allocObstacleId: () => number
  ): GiantHollowLogChunkData {
    const worms: StationaryLogWorm[] = [];
    const cobwebs: MessyCobwebCluster[] = [];

    // 1. Build the colossal overhead hollow-log shell, skylight breaches, godrays & shelf-fungi!
    this.buildHollowLogVaultAndGodrays(
      chunkGroup,
      chunkIndex,
      zStart,
      chunkLength,
      rng,
      specSampler,
      groundSampler
    );

    if (chunkIndex < 0) {
      return { worms, cobwebs };
    }

    // Add solid collider for the central splintered heartwood pillar on odd chunks
    if (chunkIndex % 2 === 1) {
      const pz = zStart - 0.52 * chunkLength;
      if (Math.abs(pz) > 38) {
        const sp = specSampler(pz);
        obstacles.push({
          id: allocObstacleId(),
          chunkIndex,
          kind: 'buttress_tree',
          position: new THREE.Vector3(sp.centerX, groundSampler(sp.centerX, pz), pz),
          radius: 3.1,
          height: 28.0,
          canShearVine: true,
        });
      }
    }

    // 2. Stationary Log-Emerging Parasite Worms (7 bore-holes per chunk along walls, ribs & knots!)
    const wormCount = 7;
    for (let w = 0; w < wormCount; w++) {
      const wz = zStart - ((w + 0.18 + rng() * 0.55) / wormCount) * chunkLength;
      if (Math.abs(wz) < 30) continue;

      const sp = specSampler(wz);
      const side = w % 2 === 0 ? -1 : 1;
      const mountRoll = rng();

      let borePos: THREE.Vector3;
      let emergeDir: THREE.Vector3;
      let maxReach: number;
      let baseRad: number;

      if (mountRoll < 0.65) {
        // Wall-mounted bore-hole on the curved inner log wall, lunging horizontally & down across the road!
        const wallX = sp.centerX + side * lerp(sp.halfWidth * 0.72, sp.halfWidth * 0.98, rng());
        const wallFloorY = groundSampler(wallX, wz);
        const boreY = wallFloorY + lerp(1.6, 5.2, rng());
        borePos = new THREE.Vector3(wallX, boreY, wz);
        emergeDir = new THREE.Vector3(-side * 0.92, lerp(-0.24, 0.08, rng()), (rng() - 0.5) * 0.28).normalize();
        maxReach = lerp(10.5, 15.2, rng());
        baseRad = lerp(0.78, 1.15, rng());
      } else if (mountRoll < 0.86) {
        // Rotten floor knot-hole near inner lane, lunging upward & across!
        const floorX = sp.centerX + side * lerp(5.5, sp.halfWidth * 0.55, rng());
        const floorY = groundSampler(floorX, wz);
        borePos = new THREE.Vector3(floorX, floorY - 0.15, wz);
        emergeDir = new THREE.Vector3(-side * 0.62, 0.74, (rng() - 0.5) * 0.25).normalize();
        maxReach = lerp(7.8, 11.2, rng());
        baseRad = lerp(0.72, 0.98, rng());
      } else {
        // Low overhead rib bore-hole lunging down toward passing roofs!
        const ceilX = sp.centerX + (rng() - 0.5) * (sp.halfWidth * 0.65);
        const floorY = groundSampler(ceilX, wz);
        borePos = new THREE.Vector3(ceilX, floorY + lerp(9.5, 13.0, rng()), wz);
        emergeDir = new THREE.Vector3((rng() - 0.5) * 0.35, -0.92, (rng() - 0.5) * 0.25).normalize();
        maxReach = lerp(7.5, 10.8, rng());
        baseRad = lerp(0.76, 1.05, rng());
      }

      this.spawnStationaryLogWorm(
        chunkGroup,
        worms,
        borePos,
        emergeDir,
        maxReach,
        baseRad,
        rng
      );
    }

    // 3. Staggered Messy Cobweb Slalom Curtains (5 per chunk — alternating Left / Right / Center!)
    // Leaves clear open corridors on the opposite side so players weave rapidly left-right-left!
    const webCount = 5;
    for (let c = 0; c < webCount; c++) {
      const cz = zStart - ((c + 0.24 + rng() * 0.32) / webCount) * chunkLength;
      if (Math.abs(cz) < 32) continue;

      const sp = specSampler(cz);
      const pattern = (chunkIndex * 3 + c) % 3;
      let webX = sp.centerX;
      let halfSpanX = sp.halfWidth * 0.46;

      if (pattern === 0) {
        // Dense messy cobweb blocking LEFT & CENTER-LEFT -> weave RIGHT!
        webX = sp.centerX - sp.halfWidth * 0.34;
        halfSpanX = sp.halfWidth * 0.48;
      } else if (pattern === 1) {
        // Dense messy cobweb blocking RIGHT & CENTER-RIGHT -> weave LEFT!
        webX = sp.centerX + sp.halfWidth * 0.34;
        halfSpanX = sp.halfWidth * 0.48;
      } else {
        // Tangled central funnel-web shroud -> weave around EITHER left or right bank!
        webX = sp.centerX + (rng() - 0.5) * 3.5;
        halfSpanX = sp.halfWidth * 0.38;
      }

      const floorY = groundSampler(webX, cz);
      const webHeight = lerp(9.5, 14.5, rng());
      this.spawnMessyCobwebCluster(
        chunkGroup,
        cobwebs,
        webX,
        floorY,
        cz,
        halfSpanX,
        webHeight,
        rng
      );
    }

    return { worms, cobwebs };
  }

  /**
   * Steps all stationary log-emerging parasite worms & messy cobwebs in a chunk,
   * and applies parasite lunge strikes / sticky cobweb drag to the active vehicle!
   */
  public stepChunkHazards(
    data: GiantHollowLogChunkData,
    elapsedTime: number,
    dt: number,
    focusPos: THREE.Vector3,
    vehiclePos: THREE.Vector3,
    vehicleVel: THREE.Vector3,
    isBoosting: boolean,
    passiveObserver: boolean
  ): void {
    this.timeUniform.value = elapsedTime;

    // 1. Stationary Log-Emerging Parasite Worms!
    for (const w of data.worms) {
      w.strikeCooldown = Math.max(0, w.strikeCooldown - dt);
      w.stateTimer += dt;

      const distToFocus = w.boreHolePos.distanceTo(focusPos);
      const distToVehicle = w.boreHolePos.distanceTo(vehiclePos);

      let targetExt = 0.14;
      let jawOpen = 0.22;

      if (passiveObserver) {
        // In peaceful Unreal Free-Cam, worms emerge partway from bore-holes and writhe organically as ecosystem life!
        const nearCam = smoothstep(75, 22, distToFocus);
        targetExt = 0.16 + 0.42 * nearCam + 0.08 * Math.sin(elapsedTime * 1.9 + w.phase);
        jawOpen = 0.25 + 0.25 * Math.sin(elapsedTime * 3.2 + w.phase);
        w.state = 'PEEKING';
      } else {
        // Drive Mode: Stationary worms erupt out of the log to attack passing vehicles!
        if (w.state === 'LATCHED') {
          targetExt = 0.96;
          jawOpen = 0.12; // Clamped mandibles!
          if (isBoosting || distToVehicle > w.maxReach + 4.2 || w.stateTimer > 1.05) {
            w.state = 'RETRACTING';
            w.stateTimer = 0;
            w.strikeCooldown = 2.4;
          } else {
            // Parasitic tether drag pulling vehicle velocity toward the log bore-hole!
            const pullDir = new THREE.Vector3()
              .subVectors(w.boreHolePos, vehiclePos)
              .setY(0);
            if (pullDir.lengthSq() > 0.01) {
              pullDir.normalize();
              vehicleVel.addScaledVector(pullDir, 11.5 * dt);
            }
            const dragDamp = Math.exp(-1.55 * dt);
            vehicleVel.x *= dragDamp;
            vehicleVel.z *= dragDamp;
          }
        } else if (w.state === 'RETRACTING') {
          targetExt = 0.15;
          jawOpen = 0.18;
          if (w.stateTimer > 1.1) {
            w.state = 'PEEKING';
            w.stateTimer = 0;
          }
        } else if (distToVehicle < 64.0 && vehiclePos.z > w.boreHolePos.z - 14.0) {
          w.state = 'LUNGING';
          targetExt = 1.0;
          jawOpen = 0.78 + 0.18 * Math.sin(elapsedTime * 12.0 + w.phase);
        } else {
          w.state = 'PEEKING';
          targetExt = 0.16 + 0.06 * Math.sin(elapsedTime * 2.2 + w.phase);
        }
      }

      const extRate = w.state === 'LUNGING' ? 6.8 : w.state === 'RETRACTING' ? 3.8 : 2.6;
      w.extension = lerp(w.extension, targetExt, clamp(extRate * dt, 0, 1));

      // Compute target aim vector (curves dynamically toward nearby vehicle during lunge, while base NEVER moves!)
      const activeLen = w.extension * w.maxReach;
      const toCar = new THREE.Vector3().subVectors(vehiclePos, w.boreHolePos);
      const canAimAtCar = !passiveObserver && (w.state === 'LUNGING' || w.state === 'LATCHED') && toCar.lengthSq() > 1.0;
      if (canAimAtCar) toCar.normalize();

      let prevPos = w.boreHolePos.clone();
      for (let s = 0; s < WORM_SEGMENT_COUNT; s++) {
        const u = (s + 1) / WORM_SEGMENT_COUNT;
        const distOut = u * activeLen;

        // Blend from fixed bore-hole emergence direction at base (u=0) toward lunge target near head (u=1)
        const dir = w.emergeDir.clone();
        if (canAimAtCar) {
          // Keep emerging forward out of wood (never fold backward into wall!)
          const dotOut = toCar.dot(w.emergeDir);
          if (dotOut > -0.15) {
            dir.lerp(toCar, u * 0.68).normalize();
          }
        }

        const writheAmp = (0.28 + 0.55 * u) * w.extension;
        const writheLat = Math.sin(elapsedTime * 5.8 - u * 4.5 + w.phase) * writheAmp;
        const writheVert = Math.cos(elapsedTime * 4.6 - u * 3.8 + w.phase) * writheAmp * 0.55;

        const segPos = w.boreHolePos
          .clone()
          .addScaledVector(dir, distOut)
          .addScaledVector(w.lateralTang, writheLat);
        segPos.y += writheVert;

        const segMesh = w.segmentMeshes[s];
        segMesh.position.copy(segPos);

        const segTangent = new THREE.Vector3().subVectors(segPos, prevPos);
        if (segTangent.lengthSq() > 1e-5) {
          segTangent.normalize();
          segMesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), segTangent);
        }

        // Check parasite strike along outer 45% of the lunging worm!
        if (
          !passiveObserver &&
          w.state === 'LUNGING' &&
          w.strikeCooldown <= 0 &&
          u > 0.55
        ) {
          const hitDist = segPos.distanceTo(vehiclePos);
          if (hitDist < w.baseRadius + 1.85) {
            w.state = 'LATCHED';
            w.stateTimer = 0;
            // Immediate physical jolt when parasite jaws clamp onto the chassis!
            vehicleVel.multiplyScalar(0.76);
            if (this.onWormStrikeVehicle) {
              this.onWormStrikeVehicle(segPos.clone());
            }
          }
        }

        if (s === WORM_SEGMENT_COUNT - 1) {
          w.currentTipWorld.copy(segPos);
          w.headGroup.position.copy(segPos);
          if (segTangent.lengthSq() > 1e-5) {
            w.headGroup.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), segTangent);
          }
        }
        prevPos.copy(segPos);
      }

      for (const jaw of w.mandibleMeshes) {
        jaw.rotation.x = jawOpen;
      }
    }

    // 2. Messy Cobweb Curtains (Interactive Stretch, Sticky Drag & Silk Tear!)
    for (const cw of data.cobwebs) {
      if (cw.tearProgress > 0) {
        // Torn cobwebs stay parted while you pass, then slowly spin back together behind you
        cw.tearProgress = Math.max(0, cw.tearProgress - 0.14 * dt);
      }
      cw.stretchOffset = lerp(cw.stretchOffset, 0, clamp(6.0 * dt, 0, 1));

      if (!passiveObserver && cw.tearProgress < 0.65) {
        const dx = Math.abs(vehiclePos.x - cw.center.x);
        const dz = Math.abs(vehiclePos.z - cw.center.z);
        const dy = vehiclePos.y - cw.center.y;

        if (dx < cw.halfWidthX + 1.1 && dz < cw.halfDepthZ && dy > -1.0 && dy < cw.heightY) {
          // Vehicle plowed straight into the messy cobweb instead of weaving around!
          cw.stretchOffset = clamp((cw.center.z - vehiclePos.z) * 0.65, -2.8, 2.8);
          const stickyDrag = Math.exp(-1.85 * dt);
          vehicleVel.x *= stickyDrag;
          vehicleVel.z *= stickyDrag;

          if (dz < 1.35 && cw.tearProgress < 0.2) {
            cw.tearProgress = 1.0;
            if (this.onCobwebTorn) {
              this.onCobwebTorn(vehiclePos.clone());
            }
          }
        }
      }

      const intactScale = 1.0 - cw.tearProgress * 0.68;
      cw.curtainGroup.position.z = -cw.stretchOffset;
      cw.curtainGroup.scale.set(
        1.0 + cw.tearProgress * 0.28,
        clamp(intactScale, 0.25, 1.0),
        1.0 + Math.abs(cw.stretchOffset) * 0.25
      );
      cw.curtainGroup.visible = cw.tearProgress < 0.92;
    }
  }

  /**
   * Updates floating golden dust & bioluminescent spore motes around the camera inside the giant log!
   */
  public stepGlobalLogAtmosphere(
    active: boolean,
    focusPos: THREE.Vector3,
    dt: number
  ): void {
    this.atmosphereFxGroup.visible = active;
    if (!active) return;

    const halfX = 42;
    const halfZ = 82;
    for (let i = 0; i < this.sporeCount; i++) {
      const ix = i * 3;
      let px = this.sporePositions[ix] + this.sporeVelocities[ix] * dt;
      let py = this.sporePositions[ix + 1] + this.sporeVelocities[ix + 1] * dt;
      let pz = this.sporePositions[ix + 2] + this.sporeVelocities[ix + 2] * dt;

      if (px - focusPos.x > halfX) px -= halfX * 2;
      else if (px - focusPos.x < -halfX) px += halfX * 2;

      if (pz - focusPos.z > halfZ) pz -= halfZ * 2;
      else if (pz - focusPos.z < -halfZ) pz += halfZ * 2;

      if (py - focusPos.y > 26) py -= 34;
      else if (py - focusPos.y < -8) py += 34;

      this.sporePositions[ix] = px;
      this.sporePositions[ix + 1] = py;
      this.sporePositions[ix + 2] = pz;
    }
    (this.sporePoints.geometry.attributes.position as THREE.BufferAttribute).needsUpdate = true;
  }
}

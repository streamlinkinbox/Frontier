import * as THREE from 'three';
import { clamp, lerp, mulberry32, ridge2D } from '../math/noise';
import { ProceduralTextureLibrary } from '../graphics/proceduralTextures';
import { WorldObstacle } from './trailCorridor';

export interface FloatingSingularityMonolith {
  group: THREE.Group;
  outerRing: THREE.Mesh;
  innerRing: THREE.Mesh;
  beamMesh: THREE.Mesh;
  baseY: number;
  bobPhase: number;
}

export interface FlyingStormDebris {
  mesh: THREE.Mesh;
  position: THREE.Vector3;
  velocity: THREE.Vector3;
  rotSpeed: THREE.Vector3;
  radius: number;
  airborneMode: boolean; // true = hurtling high/mid-air across cliff, false = skipping & slamming across road!
  spawnAnchorZ: number;
  obstacle: WorldObstacle;
}

export interface ActiveLightningBolt {
  group: THREE.Group;
  impactFlare: THREE.Mesh;
  age: number;
  duration: number;
}

export interface SingularityChunkData {
  monoliths: FloatingSingularityMonolith[];
  flyingDebris: FlyingStormDebris[];
}

export class SingularityDesertBuilder {
  public readonly leviathanBoneMat: THREE.MeshStandardMaterial;
  public readonly obsidianMonolithMat: THREE.MeshStandardMaterial;
  public readonly solarMirrorGoldMat: THREE.MeshStandardMaterial;
  public readonly fulguriteCrystalMat: THREE.MeshStandardMaterial;
  public readonly geomagBeamMat: THREE.MeshBasicMaterial;
  public readonly saltBrineWaterMat: THREE.ShaderMaterial;

  private readonly lightningCoreMat: THREE.MeshBasicMaterial;
  private readonly lightningGlowMat: THREE.MeshBasicMaterial;
  private readonly impactFlareMat: THREE.MeshBasicMaterial;

  private readonly fulguriteSpireGeo: THREE.BufferGeometry;
  private readonly stormRockGeos: THREE.BufferGeometry[] = [];
  private readonly timeUniform = { value: 0 };

  // Global camera-following Mars Sandstorm & Forked Lightning FX group!
  public readonly stormFxGroup = new THREE.Group();
  private sandstormPoints!: THREE.Points;
  private sandPositions!: Float32Array;
  private sandVelocities!: Float32Array;
  private readonly sandCount = 1500;

  private activeBolts: ActiveLightningBolt[] = [];
  private nextBoltTimer = 1.4;
  private rng = mulberry32(9091337);

  /** 0..1 instantaneous lightning flash intensity for sky/fog/directional light flare! */
  public currentLightningFlash = 0;
  public onLightningStrike?: (strikePos: THREE.Vector3) => void;

  constructor(textures: ProceduralTextureLibrary) {
    // 1. Sun-bleached fossilized Sand-Leviathan bone ivory
    this.leviathanBoneMat = new THREE.MeshStandardMaterial({
      color: 0xf0e4cf,
      map: textures.bark.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.85, 0.85),
      roughness: 0.72,
      metalness: 0.06,
    });

    // 2. Glossy fractured volcanic black obsidian with faint purple-cyan sheen
    this.obsidianMonolithMat = new THREE.MeshStandardMaterial({
      color: 0x1a1724,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(1.4, 1.4),
      roughness: 0.22,
      metalness: 0.78,
      emissive: new THREE.Color(0x091a24),
      emissiveIntensity: 0.35,
    });

    // 3. Burnished golden-bronze orbital solar collector alloy
    this.solarMirrorGoldMat = new THREE.MeshStandardMaterial({
      color: 0xe6a946,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.45, 0.45),
      roughness: 0.24,
      metalness: 0.88,
      emissive: new THREE.Color(0x3a2005),
      emissiveIntensity: 0.22,
    });

    // 4. Iridescent cyan Fulgurite lightning-glass crystal spires
    this.fulguriteCrystalMat = new THREE.MeshStandardMaterial({
      color: 0x6bf2ff,
      roughness: 0.14,
      metalness: 0.45,
      emissive: new THREE.Color(0x18b8d8),
      emissiveIntensity: 0.68,
    });

    // 5. Glowing cyan geomagnetic singularity tether column
    this.geomagBeamMat = new THREE.MeshBasicMaterial({
      color: 0x52f2ff,
      transparent: true,
      opacity: 0.34,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
      side: THREE.DoubleSide,
    });

    // 6. Glowing Turquoise Mineral Salt-Brine Pool Shader
    this.saltBrineWaterMat = new THREE.ShaderMaterial({
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
          vec2 d = vUv - vec2(0.5);
          float r = length(d) * 2.0;
          float alpha = smoothstep(1.0, 0.62, r) * 0.88;
          float wave = sin(vWorldPos.x * 1.6 + uTime * 2.4) * cos(vWorldPos.z * 1.5 - uTime * 1.9);
          float saltRim = smoothstep(0.68, 0.96, r);
          vec3 coreTurquoise = vec3(0.08, 0.82, 0.84) + wave * 0.08;
          vec3 rimCrust = vec3(0.92, 0.86, 0.78);
          vec3 col = mix(coreTurquoise, rimCrust, saltRim * 0.65);
          gl_FragColor = vec4(col, alpha);
        }
      `,
    });

    this.lightningCoreMat = new THREE.MeshBasicMaterial({
      color: 0xeaffff,
      transparent: true,
      opacity: 0.98,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });

    this.lightningGlowMat = new THREE.MeshBasicMaterial({
      color: 0x4deeff,
      transparent: true,
      opacity: 0.52,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });

    this.impactFlareMat = new THREE.MeshBasicMaterial({
      color: 0x7cf6ff,
      transparent: true,
      opacity: 0.85,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
      side: THREE.DoubleSide,
    });

    this.fulguriteSpireGeo = this.createFulguriteCrystalClusterGeo();
    this.buildStormRockGeometries();
    this.initMarsSandstormParticles(textures);
    this.stormFxGroup.visible = false;
  }

  private createFulguriteCrystalClusterGeo(): THREE.BufferGeometry {
    const parts: THREE.BufferGeometry[] = [];
    const specs = [
      { x: 0, z: 0, h: 3.4, r: 0.36, tx: 0.06, tz: -0.04 },
      { x: 0.32, z: 0.18, h: 2.2, r: 0.24, tx: 0.24, tz: 0.12 },
      { x: -0.28, z: 0.15, h: 2.5, r: 0.26, tx: -0.22, tz: 0.14 },
      { x: 0.12, z: -0.30, h: 1.85, r: 0.20, tx: 0.10, tz: -0.26 },
    ];
    for (const s of specs) {
      const cone = new THREE.ConeGeometry(s.r, s.h, 6);
      cone.translate(0, s.h * 0.5, 0);
      cone.rotateX(s.tz);
      cone.rotateZ(-s.tx);
      cone.translate(s.x, 0, s.z);
      parts.push(cone);
    }
    return this.mergeSimple(parts);
  }

  private buildStormRockGeometries(): void {
    for (let k = 0; k < 3; k++) {
      const g = new THREE.DodecahedronGeometry(1.0, 1);
      const r = mulberry32(4411 + k * 97);
      const pos = g.attributes.position as THREE.BufferAttribute;
      for (let i = 0; i < pos.count; i++) {
        const vx = pos.getX(i);
        const vy = pos.getY(i);
        const vz = pos.getZ(i);
        const jag = 0.72 + ridge2D(vx * 2.1 + k * 5, vz * 2.1 - vy * 1.7, 2) * 0.56;
        pos.setXYZ(i, vx * jag, vy * jag * lerp(0.68, 1.15, r()), vz * jag);
      }
      g.computeVertexNormals();
      this.stormRockGeos.push(g);
    }
  }

  private initMarsSandstormParticles(textures: ProceduralTextureLibrary): void {
    const geo = new THREE.BufferGeometry();
    this.sandPositions = new Float32Array(this.sandCount * 3);
    this.sandVelocities = new Float32Array(this.sandCount * 3);
    const colors = new Float32Array(this.sandCount * 3);

    for (let i = 0; i < this.sandCount; i++) {
      this.sandPositions[i * 3] = (this.rng() - 0.5) * 190;
      this.sandPositions[i * 3 + 1] = this.rng() * 52 - 8;
      this.sandPositions[i * 3 + 2] = (this.rng() - 0.5) * 190;

      // Howling Martian crosswind (+X/+Z diagonal gale!)
      this.sandVelocities[i * 3] = lerp(26.0, 46.0, this.rng());
      this.sandVelocities[i * 3 + 1] = lerp(-2.2, 3.8, this.rng());
      this.sandVelocities[i * 3 + 2] = lerp(6.0, 18.0, this.rng());

      // Warm Martian rust-ochre + champagne silica dust!
      const tint = this.rng();
      colors[i * 3] = lerp(0.84, 0.96, tint);
      colors[i * 3 + 1] = lerp(0.42, 0.64, tint);
      colors[i * 3 + 2] = lerp(0.24, 0.42, tint);
    }

    geo.setAttribute('position', new THREE.BufferAttribute(this.sandPositions, 3));
    geo.setAttribute('color', new THREE.BufferAttribute(colors, 3));

    const mat = new THREE.PointsMaterial({
      size: 2.35,
      map: textures.particleSoft,
      vertexColors: true,
      transparent: true,
      opacity: 0.46,
      depthWrite: false,
    });

    this.sandstormPoints = new THREE.Points(geo, mat);
    this.sandstormPoints.frustumCulled = false;
    this.stormFxGroup.add(this.sandstormPoints);
  }

  private mergeSimple(geos: THREE.BufferGeometry[]): THREE.BufferGeometry {
    let totalV = 0;
    let totalI = 0;
    for (const g of geos) {
      totalV += g.attributes.position.count;
      totalI += g.index ? g.index.count : g.attributes.position.count;
    }
    const pos = new Float32Array(totalV * 3);
    const norm = new Float32Array(totalV * 3);
    const uv = new Float32Array(totalV * 2);
    const idx = new Uint32Array(totalI);

    let vOff = 0;
    let iOff = 0;
    for (const g of geos) {
      if (!g.attributes.normal) g.computeVertexNormals();
      const p = g.attributes.position as THREE.BufferAttribute;
      const n = g.attributes.normal as THREE.BufferAttribute;
      const u = g.attributes.uv as THREE.BufferAttribute | undefined;
      pos.set(p.array as Float32Array, vOff * 3);
      norm.set(n.array as Float32Array, vOff * 3);
      if (u) uv.set(u.array as Float32Array, vOff * 2);
      if (g.index) {
        const gi = g.index.array;
        for (let k = 0; k < gi.length; k++) idx[iOff + k] = gi[k] + vOff;
        iOff += gi.length;
      } else {
        for (let k = 0; k < p.count; k++) idx[iOff + k] = k + vOff;
        iOff += p.count;
      }
      vOff += p.count;
    }

    const out = new THREE.BufferGeometry();
    out.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    out.setAttribute('normal', new THREE.BufferAttribute(norm, 3));
    out.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
    out.setIndex(new THREE.BufferAttribute(idx, 1));
    return out;
  }

  /**
   * 1) Colossal Fossilized Sand-Leviathan Ribcage Vault:
   *    42m-long sun-bleached ivory ribcage (9 paired curved ribs + segmented dorsal vertebral ridge)
   *    arching 17m overhead right across the cliff course!
   */
  private spawnLeviathanRibcageVault(
    chunkGroup: THREE.Group,
    obstacles: WorldObstacle[],
    chunkIndex: number,
    centerX: number,
    centerZ: number,
    yaw: number,
    groundSampler: (x: number, z: number) => number,
    allocObstacleId: () => number
  ): void {
    const vaultGroup = new THREE.Group();
    const baseY = groundSampler(centerX, centerZ);
    vaultGroup.position.set(centerX, baseY, centerZ);
    vaultGroup.rotation.y = yaw;

    const boneGeos: THREE.BufferGeometry[] = [];
    const ribPairs = 9;
    const totalLength = 42.0;
    const archPeakH = 17.0;
    const baseSpanHalf = 14.5;

    // Dorsal segmented vertebral spine running along the 17m crown!
    const spinePts: THREE.Vector3[] = [];
    for (let i = 0; i <= ribPairs; i++) {
      const t = i / ribPairs;
      const zLocal = (0.5 - t) * totalLength;
      const archCrown = archPeakH - Math.pow((t - 0.5) * 2.0, 2) * 3.2;
      spinePts.push(new THREE.Vector3(0, archCrown, zLocal));

      // Vertebral dorsal fin process + centrum disc!
      const disc = new THREE.CylinderGeometry(1.15, 1.25, 2.2, 8);
      disc.rotateX(Math.PI * 0.5);
      disc.translate(0, archCrown, zLocal);
      boneGeos.push(disc);

      const dorsalSpike = new THREE.ConeGeometry(0.55, 3.4, 5);
      dorsalSpike.rotateX(-0.22);
      dorsalSpike.translate(0, archCrown + 2.1, zLocal);
      boneGeos.push(dorsalSpike);
    }
    const spineCurve = new THREE.CatmullRomCurve3(spinePts);
    boneGeos.push(new THREE.TubeGeometry(spineCurve, 28, 0.72, 7, false));

    const cosY = Math.cos(yaw);
    const sinY = Math.sin(yaw);

    // 9 Paired Curved Ivory Ribs plunging into the silica dunes on either side of the road!
    for (let r = 0; r < ribPairs; r++) {
      const t = (r + 0.5) / ribPairs;
      const zLocal = (0.5 - t) * totalLength;
      const taper = 1.0 - Math.pow((t - 0.5) * 1.85, 2) * 0.24;
      const crownY = (archPeakH - Math.pow((t - 0.5) * 2.0, 2) * 3.2);
      const spanX = baseSpanHalf * taper;

      for (const side of [-1, 1]) {
        const ribCurve = new THREE.CatmullRomCurve3([
          new THREE.Vector3(side * 0.5, crownY, zLocal),
          new THREE.Vector3(side * spanX * 0.62, crownY * 0.86, zLocal + side * 0.4),
          new THREE.Vector3(side * spanX * 1.06, crownY * 0.44, zLocal),
          new THREE.Vector3(side * spanX * 0.94, -3.5, zLocal),
        ]);
        boneGeos.push(new THREE.TubeGeometry(ribCurve, 16, 0.62 * taper, 7, false));

        // World-space collider at each rib's ground anchor so cars weave cleanly inside the 27m-wide skeleton!
        const wx = centerX + cosY * (side * spanX * 0.94) + sinY * zLocal;
        const wz = centerZ - sinY * (side * spanX * 0.94) + cosY * zLocal;
        obstacles.push({
          id: allocObstacleId(),
          chunkIndex,
          kind: 'boulder',
          position: new THREE.Vector3(wx, baseY, wz),
          radius: 0.95,
          height: 7.5,
          canShearVine: false,
        });
      }
    }

    const mergedBones = this.mergeSimple(boneGeos);
    boneGeos.forEach((g) => g.dispose());
    const ribMesh = new THREE.Mesh(mergedBones, this.leviathanBoneMat);
    ribMesh.castShadow = true;
    ribMesh.receiveShadow = true;
    ribMesh.userData.uniqueGeo = true;
    vaultGroup.add(ribMesh);

    chunkGroup.add(vaultGroup);
  }

  /**
   * 2) Animated Levitating Obsidian Singularity Monolith:
   *    16m-24m black-obsidian diamond obelisk suspended 12m-18m in mid-air above a glowing
   *    cyan geomagnetic tether column, surrounded by 2 counter-rotating golden & obsidian gyro-rings!
   */
  private spawnFloatingSingularityMonolith(
    chunkGroup: THREE.Group,
    monoliths: FloatingSingularityMonolith[],
    x: number,
    z: number,
    groundY: number,
    rng: () => number
  ): void {
    const hoverHeight = lerp(13.5, 18.5, rng());
    const obeliskH = lerp(16.0, 24.0, rng());
    const obeliskR = obeliskH * 0.21;

    // Glowing cyan geomagnetic tether beam anchoring the monolith to the canyon/cliff below!
    const beamH = hoverHeight + obeliskH * 0.65 + 18.0;
    const beamGeo = new THREE.CylinderGeometry(0.65, 1.65, beamH, 12, 1, true);
    const beamMesh = new THREE.Mesh(beamGeo, this.geomagBeamMat);
    beamMesh.position.set(x, groundY + beamH * 0.5 - 12.0, z);
    beamMesh.userData.uniqueGeo = true;
    chunkGroup.add(beamMesh);

    const monoGroup = new THREE.Group();
    const baseY = groundY + hoverHeight + obeliskH * 0.42;
    monoGroup.position.set(x, baseY, z);

    // Sharp faceted black-obsidian diamond obelisk!
    const obeliskGeo = new THREE.OctahedronGeometry(1.0, 0);
    obeliskGeo.scale(obeliskR, obeliskH * 0.5, obeliskR);
    const obeliskMesh = new THREE.Mesh(obeliskGeo, this.obsidianMonolithMat);
    obeliskMesh.castShadow = true;
    obeliskMesh.userData.uniqueGeo = true;
    monoGroup.add(obeliskMesh);

    // Outer Golden-Bronze Gyroscopic Ring
    const outerRingGeo = new THREE.TorusGeometry(obeliskR * 2.05, 0.34, 8, 32);
    const outerRing = new THREE.Mesh(outerRingGeo, this.solarMirrorGoldMat);
    outerRing.castShadow = true;
    outerRing.userData.uniqueGeo = true;
    monoGroup.add(outerRing);

    // Inner Counter-Rotating Obsidian Gyroscopic Ring
    const innerRingGeo = new THREE.TorusGeometry(obeliskR * 1.48, 0.26, 8, 28);
    const innerRing = new THREE.Mesh(innerRingGeo, this.obsidianMonolithMat);
    innerRing.userData.uniqueGeo = true;
    monoGroup.add(innerRing);

    chunkGroup.add(monoGroup);

    monoliths.push({
      group: monoGroup,
      outerRing,
      innerRing,
      beamMesh,
      baseY,
      bobPhase: rng() * Math.PI * 2,
    });
  }

  /**
   * 3) Half-Buried Orbital Solar-Mirror Dish:
   *    28m-diameter golden parabolic collector dish jutting at 38 degrees out of the dunes
   *    with tripod struts and a suspended crystalline focal pod!
   */
  private spawnBuriedSolarMirrorDish(
    chunkGroup: THREE.Group,
    x: number,
    z: number,
    groundY: number,
    sideSign: number,
    rng: () => number
  ): void {
    const dishGroup = new THREE.Group();
    dishGroup.position.set(x, groundY - 2.4, z);
    // Tilt at 38 degrees (0.663 rad) facing toward the cliff road!
    dishGroup.rotation.y = sideSign > 0 ? -Math.PI * 0.62 + (rng() - 0.5) * 0.25 : Math.PI * 0.62 + (rng() - 0.5) * 0.25;
    dishGroup.rotation.x = (38 * Math.PI) / 180;

    const dishRadius = 14.0; // 28m diameter!
    const profilePts: THREE.Vector2[] = [];
    for (let i = 0; i <= 10; i++) {
      const t = i / 10;
      const r = t * dishRadius;
      const depth = t * t * 3.8;
      profilePts.push(new THREE.Vector2(r, depth));
    }
    const dishGeo = new THREE.LatheGeometry(profilePts, 28);
    const dishMesh = new THREE.Mesh(dishGeo, this.solarMirrorGoldMat);
    dishMesh.material = this.solarMirrorGoldMat.clone();
    dishMesh.material.side = THREE.DoubleSide;
    dishMesh.castShadow = true;
    dishMesh.receiveShadow = true;
    dishMesh.userData.uniqueGeo = true;
    dishGroup.add(dishMesh);

    // Outer structural rim ring
    const rimGeo = new THREE.TorusGeometry(dishRadius, 0.42, 8, 32);
    rimGeo.rotateX(Math.PI * 0.5);
    rimGeo.translate(0, 3.8, 0);
    const rimMesh = new THREE.Mesh(rimGeo, this.obsidianMonolithMat);
    rimMesh.userData.uniqueGeo = true;
    dishGroup.add(rimMesh);

    // 3 Tripod Struts converging on the suspended Crystalline Focal Pod!
    const focalY = 12.2;
    const strutGeos: THREE.BufferGeometry[] = [];
    for (let k = 0; k < 3; k++) {
      const a = (k / 3) * Math.PI * 2;
      const basePt = new THREE.Vector3(Math.cos(a) * (dishRadius * 0.88), 3.2, Math.sin(a) * (dishRadius * 0.88));
      const topPt = new THREE.Vector3(0, focalY, 0);
      const curve = new THREE.LineCurve3(basePt, topPt);
      strutGeos.push(new THREE.TubeGeometry(curve, 2, 0.24, 6, false));
    }
    const mergedStruts = this.mergeSimple(strutGeos);
    strutGeos.forEach((g) => g.dispose());
    const strutsMesh = new THREE.Mesh(mergedStruts, this.obsidianMonolithMat);
    strutsMesh.castShadow = true;
    strutsMesh.userData.uniqueGeo = true;
    dishGroup.add(strutsMesh);

    // Suspended Crystalline Focal Pod
    const podGeo = new THREE.OctahedronGeometry(1.45, 0);
    const podMesh = new THREE.Mesh(podGeo, this.fulguriteCrystalMat);
    podMesh.position.set(0, focalY, 0);
    podMesh.userData.uniqueGeo = true;
    dishGroup.add(podMesh);

    chunkGroup.add(dishGroup);
  }

  /**
   * Populates a 112m chunk of Vitrified Singularity Cliffs with:
   * - Colossal Leviathan Ribcage Vaults on broad plateau crests
   * - Levitating Obsidian Singularity Monoliths hovering beside sheer cliff drops
   * - 28m Half-Buried Golden Orbital Solar-Mirror Dishes
   * - Iridescent Cyan Fulgurite Crystal Spires
   * - Violent Wind-Hurled Flying Storm Rocks & Skipping Cliff Boulders!
   */
  public populateSingularityChunk(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    chunkLength: number,
    rng: () => number,
    groundSampler: (x: number, z: number) => number,
    specSampler: (z: number) => {
      centerX: number;
      halfWidth: number;
      slopeX: number;
      splitFactor: number;
      splitOffset: number;
    },
    obstacles: WorldObstacle[],
    allocObstacleId: () => number
  ): SingularityChunkData {
    const monoliths: FloatingSingularityMonolith[] = [];
    const flyingDebris: FlyingStormDebris[] = [];

    // 1. Colossal Fossilized Sand-Leviathan Ribcage Vaults (on non-split cliff plateaus!)
    if (chunkIndex >= 0 && chunkIndex % 2 === 0) {
      const zRib = zStart - 0.48 * chunkLength;
      if (zRib < -38) {
        const sp = specSampler(zRib);
        const ribX = sp.splitFactor > 0.25 ? sp.centerX - sp.splitOffset : sp.centerX;
        const yaw = Math.atan2(-sp.slopeX, 1.0);
        this.spawnLeviathanRibcageVault(
          chunkGroup,
          obstacles,
          chunkIndex,
          ribX,
          zRib,
          yaw,
          groundSampler,
          allocObstacleId
        );
      }
    }

    // 2. Two Levitating Obsidian Singularity Monoliths hovering above the cliff chasms!
    if (chunkIndex >= 0) {
      for (let m = 0; m < 2; m++) {
        const mz = zStart - (0.22 + m * 0.52) * chunkLength;
        if (mz > -24) continue;
        const sp = specSampler(mz);
        const side = (chunkIndex + m) % 2 === 0 ? -1 : 1;
        const mx = sp.centerX + side * lerp(sp.halfWidth + 14.0, sp.halfWidth + 32.0, rng());
        const my = groundSampler(mx, mz);
        this.spawnFloatingSingularityMonolith(chunkGroup, monoliths, mx, mz, my, rng);
      }
    }

    // 3. Half-Buried 28m Golden Orbital Solar-Mirror Dish!
    if (chunkIndex >= 0) {
      const sz = zStart - (chunkIndex % 2 === 0 ? 0.82 : 0.38) * chunkLength;
      if (sz < -32) {
        const sp = specSampler(sz);
        const dishSide = chunkIndex % 2 === 0 ? 1 : -1;
        const sx = sp.centerX + dishSide * lerp(sp.halfWidth + 12.0, sp.halfWidth + 24.0, rng());
        const sy = groundSampler(sx, sz);
        this.spawnBuriedSolarMirrorDish(chunkGroup, sx, sz, sy, dishSide, rng);
      }
    }

    // 4. Iridescent Cyan Fulgurite Lightning-Glass Crystal Spires along cliff edges & canyon walls!
    const crystalMatrices: THREE.Matrix4[] = [];
    const dummy = new THREE.Object3D();
    for (let c = 0; c < 26; c++) {
      const cz = zStart - rng() * chunkLength;
      const sp = specSampler(cz);
      const side = rng() < 0.5 ? -1 : 1;
      // Place right along dramatic cliff brinks and outer obsidian ledges!
      const cx =
        c < 10
          ? sp.centerX + side * lerp(sp.halfWidth * 0.82, sp.halfWidth + 4.5, rng())
          : sp.centerX + side * lerp(sp.halfWidth + 8.0, 95.0, rng());
      const cy = groundSampler(cx, cz);
      dummy.position.set(cx, cy - 0.25, cz);
      dummy.rotation.set((rng() - 0.5) * 0.35, rng() * Math.PI * 2, (rng() - 0.5) * 0.35);
      const sc = lerp(0.85, 1.95, rng());
      dummy.scale.set(sc * 0.9, sc * lerp(1.0, 1.65, rng()), sc * 0.9);
      dummy.updateMatrix();
      crystalMatrices.push(dummy.matrix.clone());
    }
    if (crystalMatrices.length > 0) {
      const im = new THREE.InstancedMesh(
        this.fulguriteSpireGeo,
        this.fulguriteCrystalMat,
        crystalMatrices.length
      );
      crystalMatrices.forEach((m, idx) => im.setMatrixAt(idx, m));
      im.instanceMatrix.needsUpdate = true;
      chunkGroup.add(im);
    }

    // 5. Violent Sandstorm Flying Rocks & Boulders hurtling across the cliff!
    if (chunkIndex >= 0) {
      const debrisCount = 7;
      for (let d = 0; d < debrisCount; d++) {
        const dz = zStart - ((d + 0.18 + rng() * 0.6) / debrisCount) * chunkLength;
        if (Math.abs(dz) < 32) continue;
        const sp = specSampler(dz);
        const airborneMode = d % 2 === 1; // Half streak overhead in the gale, half slam & skip across the cliff road!
        const rad = airborneMode ? lerp(1.1, 2.4, rng()) : lerp(1.35, 2.35, rng());
        const mat = d % 3 === 0 ? this.obsidianMonolithMat : this.solarMirrorGoldMat;
        const rockMesh = new THREE.Mesh(
          this.stormRockGeos[d % this.stormRockGeos.length],
          d % 3 === 2 ? this.obsidianMonolithMat : mat
        );
        rockMesh.scale.setScalar(rad);
        rockMesh.castShadow = true;
        chunkGroup.add(rockMesh);

        const startX = sp.centerX - 48.0 + rng() * 65.0;
        const gy = groundSampler(startX, dz);
        const pos = new THREE.Vector3(
          startX,
          gy + (airborneMode ? lerp(5.5, 16.0, rng()) : rad + 2.0),
          dz
        );

        const obs: WorldObstacle = {
          id: allocObstacleId(),
          chunkIndex,
          kind: 'rolling_boulder',
          position: new THREE.Vector3(pos.x, airborneMode ? pos.y - 500 : gy, pos.z),
          radius: rad * 0.88,
          height: rad * 2.1,
          canShearVine: true,
        };
        if (!airborneMode) obstacles.push(obs);

        flyingDebris.push({
          mesh: rockMesh,
          position: pos,
          velocity: new THREE.Vector3(
            lerp(18.0, 31.0, rng()), // Howling gale hurling rocks West -> East across the cliff!
            airborneMode ? lerp(-1.5, 1.5, rng()) : 0,
            lerp(-4.5, 4.5, rng())
          ),
          rotSpeed: new THREE.Vector3(
            (rng() - 0.5) * 7.5,
            (rng() - 0.5) * 7.5,
            (rng() - 0.5) * 7.5
          ),
          radius: rad,
          airborneMode,
          spawnAnchorZ: dz,
          obstacle: obs,
        });
      }
    }

    return { monoliths, flyingDebris };
  }

  /**
   * Spawns a dramatic 3D jagged forked electrostatic sandstorm lightning bolt!
   */
  private triggerRandomLightningStrike(
    focusPos: THREE.Vector3,
    groundSampler: (x: number, z: number) => number
  ): void {
    const angle = (this.rng() - 0.5) * 1.9;
    const distAhead = lerp(38.0, 145.0, this.rng());
    const strikeX = focusPos.x + Math.sin(angle) * distAhead;
    const strikeZ = focusPos.z - Math.cos(angle) * distAhead;
    const strikeY = groundSampler(strikeX, strikeZ);
    const skyTopY = Math.max(focusPos.y, strikeY) + lerp(95.0, 130.0, this.rng());

    const boltGroup = new THREE.Group();
    const coreGeos: THREE.BufferGeometry[] = [];
    const glowGeos: THREE.BufferGeometry[] = [];

    // Main jagged trunk (14 segments from storm cloud down to cliff/dune!)
    const segs = 14;
    const trunkPts: THREE.Vector3[] = [];
    for (let i = 0; i <= segs; i++) {
      const t = i / segs;
      const spread = Math.sin(t * Math.PI) * 9.5;
      const px = lerp(strikeX + (this.rng() - 0.5) * 18.0, strikeX, t) + (i > 0 && i < segs ? (this.rng() - 0.5) * spread : 0);
      const py = lerp(skyTopY, strikeY, t);
      const pz = lerp(strikeZ + (this.rng() - 0.5) * 18.0, strikeZ, t) + (i > 0 && i < segs ? (this.rng() - 0.5) * spread : 0);
      trunkPts.push(new THREE.Vector3(px, py, pz));
    }

    const mainCurve = new THREE.CatmullRomCurve3(trunkPts);
    coreGeos.push(new THREE.TubeGeometry(mainCurve, 20, 0.28, 5, false));
    glowGeos.push(new THREE.TubeGeometry(mainCurve, 20, 0.95, 5, false));

    // 3-4 Forked Secondary Lightning Branches!
    const branchCount = 3 + Math.floor(this.rng() * 2);
    for (let b = 0; b < branchCount; b++) {
      const idx = 3 + Math.floor(this.rng() * (segs - 5));
      const p0 = trunkPts[idx];
      const bPts: THREE.Vector3[] = [p0.clone()];
      let cur = p0.clone();
      const sideX = (this.rng() < 0.5 ? -1 : 1) * lerp(5.5, 14.0, this.rng());
      const sideZ = (this.rng() - 0.5) * 12.0;
      for (let s = 1; s <= 4; s++) {
        cur = cur.clone().add(
          new THREE.Vector3(
            sideX * 0.35 + (this.rng() - 0.5) * 3.0,
            -lerp(5.0, 9.5, this.rng()),
            sideZ * 0.35 + (this.rng() - 0.5) * 3.0
          )
        );
        bPts.push(cur);
      }
      const bCurve = new THREE.CatmullRomCurve3(bPts);
      coreGeos.push(new THREE.TubeGeometry(bCurve, 8, 0.14, 4, false));
      glowGeos.push(new THREE.TubeGeometry(bCurve, 8, 0.48, 4, false));
    }

    const mCore = this.mergeSimple(coreGeos);
    const mGlow = this.mergeSimple(glowGeos);
    coreGeos.forEach((g) => g.dispose());
    glowGeos.forEach((g) => g.dispose());

    const coreMesh = new THREE.Mesh(mCore, this.lightningCoreMat);
    const glowMesh = new THREE.Mesh(mGlow, this.lightningGlowMat);
    coreMesh.userData.uniqueGeo = true;
    glowMesh.userData.uniqueGeo = true;
    boltGroup.add(coreMesh, glowMesh);

    // Molten Fulgurite ground-strike ring flare!
    const flareGeo = new THREE.CircleGeometry(6.5, 16);
    flareGeo.rotateX(-Math.PI * 0.5);
    const impactFlare = new THREE.Mesh(flareGeo, this.impactFlareMat);
    impactFlare.position.set(strikeX, strikeY + 0.35, strikeZ);
    impactFlare.userData.uniqueGeo = true;
    boltGroup.add(impactFlare);

    this.stormFxGroup.add(boltGroup);
    this.activeBolts.push({
      group: boltGroup,
      impactFlare,
      age: 0,
      duration: lerp(0.22, 0.36, this.rng()),
    });

    if (this.onLightningStrike) {
      this.onLightningStrike(new THREE.Vector3(strikeX, strikeY, strikeZ));
    }
  }

  /**
   * Steps chunk-local levitating monoliths & wind-hurled flying storm rocks!
   */
  public stepChunkHazards(
    data: SingularityChunkData,
    elapsedTime: number,
    dt: number,
    specSampler: (z: number) => { centerX: number; halfWidth: number },
    groundSampler: (x: number, z: number) => number
  ): void {
    this.timeUniform.value = elapsedTime;

    // 1. Levitating Obsidian Singularity Monoliths & Counter-Rotating Gyro-Rings
    for (const fm of data.monoliths) {
      fm.group.position.y = fm.baseY + Math.sin(elapsedTime * 1.45 + fm.bobPhase) * 1.85;
      fm.group.rotation.y = elapsedTime * 0.28 + fm.bobPhase;
      fm.outerRing.rotation.x = elapsedTime * 1.15 + fm.bobPhase;
      fm.outerRing.rotation.y = elapsedTime * 0.72;
      fm.innerRing.rotation.z = -elapsedTime * 1.65 + fm.bobPhase;
      fm.innerRing.rotation.x = -elapsedTime * 0.88;
    }

    // 2. Wind-Hurled Flying Storm Rocks & Skipping Cliff Boulders!
    for (const fd of data.flyingDebris) {
      fd.position.addScaledVector(fd.velocity, dt);
      fd.mesh.rotation.x += fd.rotSpeed.x * dt;
      fd.mesh.rotation.y += fd.rotSpeed.y * dt;
      fd.mesh.rotation.z += fd.rotSpeed.z * dt;

      const sp = specSampler(fd.position.z);
      const localGround = groundSampler(fd.position.x, fd.position.z);

      if (fd.airborneMode) {
        // Hurtling overhead & past cliff flanks through the swirling Mars dust!
        const waveY = Math.sin(elapsedTime * 2.4 + fd.spawnAnchorZ) * 2.2;
        fd.mesh.position.set(fd.position.x, Math.max(localGround + 3.5, fd.position.y + waveY), fd.position.z);
      } else {
        // Slamming, skipping & bouncing violently across the narrow cliff road!
        fd.velocity.y -= 21.0 * dt;
        if (fd.position.y < localGround + fd.radius * 0.88) {
          fd.position.y = localGround + fd.radius * 0.88;
          fd.velocity.y = lerp(5.5, 10.5, 0.6); // High-energy rocky skip!
        }
        fd.mesh.position.copy(fd.position);
        fd.obstacle.position.set(fd.position.x, localGround, fd.position.z);
      }

      // Wrap storm debris back upwind (-X) so the gale continuously hurls boulders across the cliffs!
      if (fd.position.x > sp.centerX + 68.0) {
        fd.position.x = sp.centerX - 64.0;
        fd.position.z = fd.spawnAnchorZ;
        const upwindGround = groundSampler(fd.position.x, fd.position.z);
        fd.position.y =
          upwindGround + (fd.airborneMode ? 11.0 : fd.radius + 9.0);
        fd.velocity.y = 0;
      }
    }
  }

  /**
   * Steps global Mars Sandstorm particle curtains & random forked 3D lightning strikes!
   */
  public stepGlobalStormFx(
    active: boolean,
    focusPos: THREE.Vector3,
    dt: number,
    groundSampler: (x: number, z: number) => number
  ): void {
    this.stormFxGroup.visible = active;
    if (!active) {
      this.currentLightningFlash = 0;
      return;
    }

    // 1. Update high-velocity whipping Mars sandstorm particles in a 190m box around camera!
    const halfBox = 95;
    for (let i = 0; i < this.sandCount; i++) {
      const ix = i * 3;
      let px = this.sandPositions[ix] + this.sandVelocities[ix] * dt;
      let py = this.sandPositions[ix + 1] + this.sandVelocities[ix + 1] * dt;
      let pz = this.sandPositions[ix + 2] + this.sandVelocities[ix + 2] * dt;

      if (px - focusPos.x > halfBox) px -= halfBox * 2;
      else if (px - focusPos.x < -halfBox) px += halfBox * 2;

      if (pz - focusPos.z > halfBox) pz -= halfBox * 2;
      else if (pz - focusPos.z < -halfBox) pz += halfBox * 2;

      if (py - focusPos.y > 42) py -= 56;
      else if (py - focusPos.y < -14) py += 56;

      this.sandPositions[ix] = px;
      this.sandPositions[ix + 1] = py;
      this.sandPositions[ix + 2] = pz;
    }
    (this.sandstormPoints.geometry.attributes.position as THREE.BufferAttribute).needsUpdate = true;

    // 2. Random Sandstorm Lightning Strikes!
    this.nextBoltTimer -= dt;
    if (this.nextBoltTimer <= 0) {
      this.triggerRandomLightningStrike(focusPos, groundSampler);
      // Occasional rapid double-strike in intense Martian electrical squalls!
      this.nextBoltTimer =
        this.rng() < 0.28 ? lerp(0.18, 0.42, this.rng()) : lerp(1.35, 3.35, this.rng());
    }

    let maxFlash = 0;
    for (let i = this.activeBolts.length - 1; i >= 0; i--) {
      const b = this.activeBolts[i];
      b.age += dt;
      const u = b.age / b.duration;
      // Realistic double-flicker discharge envelope
      const flicker =
        u < 0.25
          ? 1.0
          : u < 0.45
          ? 0.28
          : u < 0.72
          ? 0.92
          : Math.max(0, 1.0 - (u - 0.72) / 0.28);

      b.impactFlare.scale.setScalar(0.7 + u * 1.4);
      b.group.visible = flicker > 0.15;
      if (flicker > maxFlash) maxFlash = flicker;

      if (b.age >= b.duration) {
        this.stormFxGroup.remove(b.group);
        b.group.traverse((o) => {
          if ((o as THREE.Mesh).isMesh && o.userData.uniqueGeo) {
            (o as THREE.Mesh).geometry.dispose();
          }
        });
        this.activeBolts.splice(i, 1);
      }
    }
    this.currentLightningFlash = maxFlash;
  }
}

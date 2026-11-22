import * as THREE from 'three';
import { clamp, lerp } from '../math/noise';
import { ProceduralTextureLibrary } from '../graphics/proceduralTextures';
import { CrisscrossRootBridge, WorldObstacle } from './trailCorridor';

export interface SpinningWindmill {
  rotorGroup: THREE.Group;
  yawGroup: THREE.Group;
  spinSpeed: number;
  phase: number;
}

export interface CollapsingWaterTower {
  pivotGroup: THREE.Group;
  baseX: number;
  baseY: number;
  z: number;
  fallSide: number; // +1 or -1 toward road
  age: number;
  active: boolean;
  obstacle: WorldObstacle;
  debrisPlanks: Array<{
    mesh: THREE.Mesh;
    angle: number;
    speed: number;
  }>;
}

export interface RunawayBarrel {
  mesh: THREE.Mesh;
  kind: 'oil_drum' | 'tractor_tire';
  position: THREE.Vector3;
  spawnX: number;
  spawnZ: number;
  dirX: number;
  speed: number;
  radius: number;
  maxTravelX: number;
  phase: number;
  obstacle: WorldObstacle;
}

export interface SwingingCorralGate {
  hingeGroup: THREE.Group;
  hingeX: number;
  hingeY: number;
  hingeZ: number;
  gateWidth: number;
  baseSign: number;
  swingSpeed: number;
  phase: number;
  obstacle: WorldObstacle;
}

export interface SwayingAwning {
  mesh: THREE.Mesh;
  baseY: number;
  phase: number;
}

export interface DesertVillageChunkData {
  windmills: SpinningWindmill[];
  waterTowers: CollapsingWaterTower[];
  runawayBarrels: RunawayBarrel[];
  swingingGates: SwingingCorralGate[];
  awnings: SwayingAwning[];
}

export class DesertVillageBuilder {
  public readonly weatheredTimberMat: THREE.MeshStandardMaterial;
  public readonly darkBeamMat: THREE.MeshStandardMaterial;
  public readonly rustedCorrugatedMat: THREE.MeshStandardMaterial;
  public readonly galvanizedTinMat: THREE.MeshStandardMaterial;
  public readonly adobePlasterMat: THREE.MeshStandardMaterial;
  public readonly burlapAwningMat: THREE.MeshStandardMaterial;
  public readonly terracottaClothMat: THREE.MeshStandardMaterial;
  public readonly oilDrumRedMat: THREE.MeshStandardMaterial;
  public readonly oilDrumOchreMat: THREE.MeshStandardMaterial;
  public readonly oldTireRubberMat: THREE.MeshStandardMaterial;
  public readonly wireCableMat: THREE.MeshStandardMaterial;

  private readonly crateGeo = new THREE.BoxGeometry(1.15, 1.05, 1.15);
  private readonly oilDrumGeo = new THREE.CylinderGeometry(0.46, 0.46, 1.32, 12);
  private readonly tractorTireGeo = new THREE.TorusGeometry(0.68, 0.28, 8, 16);
  private readonly plankDebrisGeo = new THREE.BoxGeometry(0.24, 0.08, 1.45);

  constructor(textures: ProceduralTextureLibrary) {
    // Sun-bleached rough-sawn timber planks
    this.weatheredTimberMat = new THREE.MeshStandardMaterial({
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      normalScale: new THREE.Vector2(0.85, 0.85),
      color: new THREE.Color(0x967554),
      roughness: 0.91,
      metalness: 0.02,
    });

    // Dark creosote structural posts & beams
    this.darkBeamMat = new THREE.MeshStandardMaterial({
      map: textures.bark.map,
      normalMap: textures.bark.normalMap,
      color: new THREE.Color(0x523c2a),
      roughness: 0.93,
      metalness: 0.02,
    });

    // Oxidized burnt-rust corrugated sheet-iron roof panels
    this.rustedCorrugatedMat = new THREE.MeshStandardMaterial({
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.7, 0.7),
      color: new THREE.Color(0x8f462a),
      roughness: 0.72,
      metalness: 0.42,
    });

    // Sun-faded galvanized zinc sheet metal
    this.galvanizedTinMat = new THREE.MeshStandardMaterial({
      map: textures.rock.map,
      color: new THREE.Color(0xa2a5a6),
      roughness: 0.54,
      metalness: 0.56,
    });

    // Sun-baked cracked adobe / mud-brick lower walls
    this.adobePlasterMat = new THREE.MeshStandardMaterial({
      map: textures.terrainDetail.map,
      normalMap: textures.terrainDetail.normalMap,
      color: new THREE.Color(0xc89f72),
      roughness: 0.94,
      metalness: 0.0,
    });

    // Dusty sun-bleached burlap shade cloth
    this.burlapAwningMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(0xc9ae82),
      roughness: 0.92,
      side: THREE.DoubleSide,
    });

    // Faded rust-red market cloth awning
    this.terracottaClothMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(0x9d4b32),
      roughness: 0.89,
      side: THREE.DoubleSide,
    });

    // Weathered 55-gallon steel oil drums
    this.oilDrumRedMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(0x8e3324),
      roughness: 0.62,
      metalness: 0.45,
    });

    this.oilDrumOchreMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(0xa6782b),
      roughness: 0.64,
      metalness: 0.42,
    });

    this.oldTireRubberMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(0x282726),
      roughness: 0.92,
      metalness: 0.04,
    });

    this.wireCableMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(0x22201e),
      roughness: 0.8,
    });

    // Orient oilDrumGeo horizontally on its side for rolling barrels when cloned if needed,
    // we'll rotate rolling barrel meshes in local space!
  }

  /**
   * Builds a multi-part weathered desert shanty / shack with timber walls, corrugated tin roof,
   * covered front porch on wooden posts, window shutters, chimney pipe & roadside props!
   */
  private spawnDesertShack(
    chunkGroup: THREE.Group,
    x: number,
    groundY: number,
    z: number,
    facingYaw: number,
    rng: () => number,
    obstacles: WorldObstacle[],
    chunkIndex: number,
    allocObstacleId: () => number
  ): void {
    const shack = new THREE.Group();
    shack.position.set(x, groundY, z);
    shack.rotation.y = facingYaw;

    const width = lerp(6.2, 9.6, rng());
    const depth = lerp(5.4, 8.2, rng());
    const wallH = rng() < 0.32 ? lerp(5.4, 6.8, rng()) : lerp(3.4, 4.5, rng()); // 1-story or tall 2-story shack!
    const useAdobeBase = rng() < 0.45;

    // Timber stilt / stone leveling plinths so shacks sit naturally on uneven desert sand!
    const plinthGeo = new THREE.BoxGeometry(width + 0.4, 1.2, depth + 0.4);
    plinthGeo.userData.uniqueGeo = true;
    const plinth = new THREE.Mesh(plinthGeo, this.darkBeamMat);
    plinth.position.y = 0.15;
    plinth.castShadow = true;
    plinth.receiveShadow = true;
    plinth.userData.uniqueGeo = true;
    shack.add(plinth);

    // Main cabin walls
    const bodyGeo = new THREE.BoxGeometry(width, wallH, depth);
    bodyGeo.userData.uniqueGeo = true;
    const body = new THREE.Mesh(
      bodyGeo,
      useAdobeBase ? this.adobePlasterMat : this.weatheredTimberMat
    );
    body.position.y = 0.7 + wallH * 0.5;
    body.castShadow = true;
    body.receiveShadow = true;
    body.userData.uniqueGeo = true;
    shack.add(body);

    // Patched corrugated sheet-metal siding panel nailed onto one wall for lived-in grit!
    const patchGeo = new THREE.BoxGeometry(width * 0.42, wallH * 0.55, depth + 0.08);
    patchGeo.userData.uniqueGeo = true;
    const patch = new THREE.Mesh(
      patchGeo,
      rng() < 0.5 ? this.rustedCorrugatedMat : this.galvanizedTinMat
    );
    patch.position.set((rng() - 0.5) * width * 0.35, 0.7 + wallH * 0.42, 0);
    patch.userData.uniqueGeo = true;
    shack.add(patch);

    // Corner vertical timber framing posts
    const postGeo = new THREE.BoxGeometry(0.24, wallH + 0.3, 0.24);
    postGeo.userData.uniqueGeo = true;
    for (const sx of [-1, 1]) {
      for (const sz of [-1, 1]) {
        const p = new THREE.Mesh(postGeo, this.darkBeamMat);
        p.position.set(sx * (width * 0.5), 0.7 + wallH * 0.5, sz * (depth * 0.5));
        p.userData.uniqueGeo = true;
        shack.add(p);
      }
    }

    // Slanted Corrugated-Tin Lean-To or Gable Roof with overhanging eaves!
    const roofMat = rng() < 0.62 ? this.rustedCorrugatedMat : this.galvanizedTinMat;
    if (rng() < 0.55) {
      // Lean-to pitched corrugated tin sheet
      const roofGeo = new THREE.BoxGeometry(width + 1.3, 0.16, depth + 1.5);
      roofGeo.userData.uniqueGeo = true;
      const roof = new THREE.Mesh(roofGeo, roofMat);
      roof.position.set(0, 0.7 + wallH + 0.35, 0);
      roof.rotation.x = 0.16;
      roof.rotation.z = (rng() - 0.5) * 0.06;
      roof.castShadow = true;
      roof.receiveShadow = true;
      roof.userData.uniqueGeo = true;
      shack.add(roof);
    } else {
      // Two-panel Gable Roof
      const halfSpan = width * 0.58;
      const panelGeo = new THREE.BoxGeometry(halfSpan, 0.15, depth + 1.4);
      panelGeo.userData.uniqueGeo = true;
      for (const side of [-1, 1]) {
        const panel = new THREE.Mesh(panelGeo, roofMat);
        panel.position.set(side * halfSpan * 0.44, 0.7 + wallH + 0.55, 0);
        panel.rotation.z = -side * 0.32;
        panel.castShadow = true;
        panel.userData.uniqueGeo = true;
        shack.add(panel);
      }
    }

    // Covered Wooden Front Porch facing +Z (toward the village street!)
    const porchDepth = 2.4;
    const porchZ = depth * 0.5 + porchDepth * 0.5;
    const deckGeo = new THREE.BoxGeometry(width * 0.92, 0.22, porchDepth);
    deckGeo.userData.uniqueGeo = true;
    const deck = new THREE.Mesh(deckGeo, this.weatheredTimberMat);
    deck.position.set(0, 0.68, porchZ);
    deck.castShadow = true;
    deck.receiveShadow = true;
    deck.userData.uniqueGeo = true;
    shack.add(deck);

    // Porch awning roof & 3 crooked wooden support posts
    const porchRoofGeo = new THREE.BoxGeometry(width * 0.96, 0.12, porchDepth + 0.5);
    porchRoofGeo.userData.uniqueGeo = true;
    const porchRoof = new THREE.Mesh(porchRoofGeo, this.galvanizedTinMat);
    porchRoof.position.set(0, 3.35, porchZ + 0.1);
    porchRoof.rotation.x = 0.22;
    porchRoof.castShadow = true;
    porchRoof.userData.uniqueGeo = true;
    shack.add(porchRoof);

    const pPostGeo = new THREE.BoxGeometry(0.16, 2.75, 0.16);
    pPostGeo.userData.uniqueGeo = true;
    for (const pxFrac of [-0.42, 0, 0.42]) {
      const pp = new THREE.Mesh(pPostGeo, this.darkBeamMat);
      pp.position.set(pxFrac * width, 2.0, depth * 0.5 + porchDepth - 0.15);
      pp.rotation.z = (rng() - 0.5) * 0.06;
      pp.castShadow = true;
      pp.userData.uniqueGeo = true;
      shack.add(pp);
    }

    // Dark recessed doorway + wooden window shutters on front wall
    const doorGeo = new THREE.BoxGeometry(1.15, 2.15, 0.12);
    doorGeo.userData.uniqueGeo = true;
    const door = new THREE.Mesh(doorGeo, this.wireCableMat);
    door.position.set(0, 1.78, depth * 0.5 + 0.03);
    door.userData.uniqueGeo = true;
    shack.add(door);

    for (const wxSide of [-1, 1]) {
      if (width > 6.8) {
        const winGeo = new THREE.BoxGeometry(0.95, 0.95, 0.1);
        winGeo.userData.uniqueGeo = true;
        const win = new THREE.Mesh(winGeo, this.wireCableMat);
        win.position.set(wxSide * width * 0.3, 2.25, depth * 0.5 + 0.03);
        win.userData.uniqueGeo = true;
        shack.add(win);

        // Tilted wooden shutter
        const shutGeo = new THREE.BoxGeometry(0.48, 0.98, 0.08);
        shutGeo.userData.uniqueGeo = true;
        const shut = new THREE.Mesh(shutGeo, this.weatheredTimberMat);
        shut.position.set(wxSide * (width * 0.3 + 0.52), 2.25, depth * 0.5 + 0.12);
        shut.rotation.y = wxSide * 0.35;
        shut.userData.uniqueGeo = true;
        shack.add(shut);
      }
    }

    // Rusted stovepipe chimney sticking out of roof
    const pipeGeo = new THREE.CylinderGeometry(0.16, 0.18, 1.9, 7);
    pipeGeo.userData.uniqueGeo = true;
    const pipe = new THREE.Mesh(pipeGeo, this.rustedCorrugatedMat);
    pipe.position.set(width * 0.28, 0.7 + wallH + 1.1, -depth * 0.2);
    pipe.rotation.z = -0.07;
    pipe.castShadow = true;
    pipe.userData.uniqueGeo = true;
    shack.add(pipe);

    // Roadside wooden crates & oil drums beside the porch
    for (let c = 0; c < 2; c++) {
      const crate = new THREE.Mesh(this.crateGeo, this.weatheredTimberMat);
      crate.position.set(
        (c === 0 ? -1 : 1) * (width * 0.36),
        1.25,
        depth * 0.5 + porchDepth * 0.45
      );
      crate.rotation.y = rng() * 0.5;
      crate.castShadow = true;
      shack.add(crate);
    }

    chunkGroup.add(shack);

    // Register solid collision footprint for the shack
    obstacles.push({
      id: allocObstacleId(),
      chunkIndex,
      kind: 'boulder',
      position: new THREE.Vector3(x, groundY, z),
      radius: Math.max(width, depth) * 0.52,
      height: wallH + 1.5,
      canShearVine: true,
    });
  }

  /**
   * Spawns a classic skeletal timber & steel farm wind-pump with a spinning 12-blade turbine wheel!
   */
  private spawnSpinningWindPump(
    chunkGroup: THREE.Group,
    windmills: SpinningWindmill[],
    x: number,
    groundY: number,
    z: number,
    rng: () => number
  ): void {
    const towerH = lerp(12.5, 16.5, rng());
    const tower = new THREE.Group();
    tower.position.set(x, groundY, z);

    // 4-legged tapered wooden trestle tower
    const baseSpread = 2.4;
    const topSpread = 0.55;
    for (const sx of [-1, 1]) {
      for (const sz of [-1, 1]) {
        const p0 = new THREE.Vector3(sx * baseSpread, -0.3, sz * baseSpread);
        const p1 = new THREE.Vector3(sx * topSpread, towerH, sz * topSpread);
        const legCurve = new THREE.LineCurve3(p0, p1);
        const legGeo = new THREE.TubeGeometry(legCurve, 2, 0.14, 5, false);
        legGeo.userData.uniqueGeo = true;
        const legMesh = new THREE.Mesh(legGeo, this.darkBeamMat);
        legMesh.castShadow = true;
        legMesh.userData.uniqueGeo = true;
        tower.add(legMesh);
      }
    }

    // Horizontal cross-braces up the trestle
    for (const hFrac of [0.28, 0.56, 0.82]) {
      const wHere = lerp(baseSpread, topSpread, hFrac) * 2.0;
      const braceGeo = new THREE.BoxGeometry(wHere, 0.12, wHere);
      braceGeo.userData.uniqueGeo = true;
      const brace = new THREE.Mesh(braceGeo, this.weatheredTimberMat);
      brace.position.y = towerH * hFrac;
      brace.userData.uniqueGeo = true;
      tower.add(brace);
    }

    // Top yaw head + tail vane + 12-blade galvanized steel wind wheel!
    const yawGroup = new THREE.Group();
    yawGroup.position.y = towerH + 0.45;
    yawGroup.rotation.y = rng() * Math.PI * 2;
    tower.add(yawGroup);

    const vaneGeo = new THREE.BoxGeometry(0.08, 1.15, 2.4);
    vaneGeo.userData.uniqueGeo = true;
    const vane = new THREE.Mesh(vaneGeo, this.rustedCorrugatedMat);
    vane.position.set(0, 0.2, -1.55);
    vane.userData.uniqueGeo = true;
    yawGroup.add(vane);

    const rotorGroup = new THREE.Group();
    rotorGroup.position.set(0, 0.15, 0.85);
    yawGroup.add(rotorGroup);

    const rimGeo = new THREE.TorusGeometry(2.35, 0.08, 6, 20);
    rimGeo.userData.uniqueGeo = true;
    const rim = new THREE.Mesh(rimGeo, this.galvanizedTinMat);
    rim.userData.uniqueGeo = true;
    rotorGroup.add(rim);

    const bladeGeo = new THREE.BoxGeometry(0.38, 1.95, 0.04);
    bladeGeo.userData.uniqueGeo = true;
    const bladeCount = 12;
    for (let b = 0; b < bladeCount; b++) {
      const ang = (b / bladeCount) * Math.PI * 2;
      const blade = new THREE.Mesh(bladeGeo, this.galvanizedTinMat);
      blade.position.set(Math.cos(ang) * 1.32, Math.sin(ang) * 1.32, 0);
      blade.rotation.z = ang - Math.PI * 0.5;
      blade.rotation.y = 0.38;
      blade.castShadow = true;
      blade.userData.uniqueGeo = true;
      rotorGroup.add(blade);
    }

    chunkGroup.add(tower);
    windmills.push({
      rotorGroup,
      yawGroup,
      spinSpeed: lerp(2.4, 4.2, rng()),
      phase: rng() * Math.PI * 2,
    });
  }

  /**
   * DYNAMIC OBSTACLE 1: Collapsing Wooden Water-Tower Trestle!
   * Shudders and topples onto one side of the dusty village street when approached!
   */
  private spawnCollapsingWaterTower(
    chunkGroup: THREE.Group,
    waterTowers: CollapsingWaterTower[],
    obstacles: WorldObstacle[],
    chunkIndex: number,
    x: number,
    groundY: number,
    z: number,
    fallSide: number,
    rng: () => number,
    allocObstacleId: () => number
  ): void {
    const pivotGroup = new THREE.Group();
    pivotGroup.position.set(x, groundY, z);
    chunkGroup.add(pivotGroup);

    const legH = 8.4;
    const legSpread = 1.85;
    const legGeo = new THREE.BoxGeometry(0.28, legH, 0.28);
    legGeo.userData.uniqueGeo = true;
    for (const sx of [-1, 1]) {
      for (const sz of [-1, 1]) {
        const leg = new THREE.Mesh(legGeo, this.darkBeamMat);
        leg.position.set(sx * legSpread, legH * 0.5, sz * legSpread);
        leg.castShadow = true;
        leg.userData.uniqueGeo = true;
        pivotGroup.add(leg);
      }
    }

    const deckGeo = new THREE.BoxGeometry(4.6, 0.32, 4.6);
    deckGeo.userData.uniqueGeo = true;
    const deck = new THREE.Mesh(deckGeo, this.weatheredTimberMat);
    deck.position.y = legH;
    deck.castShadow = true;
    deck.userData.uniqueGeo = true;
    pivotGroup.add(deck);

    // Slatted wooden stave barrel water tank + conical rusted tin cap
    const tankGeo = new THREE.CylinderGeometry(2.05, 2.15, 3.8, 12);
    tankGeo.userData.uniqueGeo = true;
    const tank = new THREE.Mesh(tankGeo, this.weatheredTimberMat);
    tank.position.y = legH + 2.05;
    tank.castShadow = true;
    tank.userData.uniqueGeo = true;
    pivotGroup.add(tank);

    // Iron hoop bands around wooden water tank
    for (const hy of [legH + 0.8, legH + 2.0, legH + 3.2]) {
      const hoopGeo = new THREE.TorusGeometry(2.14, 0.06, 6, 16);
      hoopGeo.rotateX(Math.PI / 2);
      hoopGeo.userData.uniqueGeo = true;
      const hoop = new THREE.Mesh(hoopGeo, this.rustedCorrugatedMat);
      hoop.position.y = hy;
      hoop.userData.uniqueGeo = true;
      pivotGroup.add(hoop);
    }

    const capGeo = new THREE.ConeGeometry(2.35, 1.25, 12);
    capGeo.userData.uniqueGeo = true;
    const cap = new THREE.Mesh(capGeo, this.rustedCorrugatedMat);
    cap.position.y = legH + 4.55;
    cap.castShadow = true;
    cap.userData.uniqueGeo = true;
    pivotGroup.add(cap);

    const debrisPlanks: CollapsingWaterTower['debrisPlanks'] = [];
    for (let d = 0; d < 10; d++) {
      const m = new THREE.Mesh(this.plankDebrisGeo, this.weatheredTimberMat);
      m.visible = false;
      chunkGroup.add(m);
      debrisPlanks.push({
        mesh: m,
        angle: rng() * Math.PI * 2,
        speed: lerp(2.5, 7.0, rng()),
      });
    }

    const obstacle: WorldObstacle = {
      id: allocObstacleId(),
      chunkIndex,
      kind: 'boulder',
      position: new THREE.Vector3(x, groundY, z),
      radius: 2.3,
      height: 12.0,
      canShearVine: true,
    };
    obstacles.push(obstacle);

    waterTowers.push({
      pivotGroup,
      baseX: x,
      baseY: groundY,
      z,
      fallSide,
      age: 0,
      active: false,
      obstacle,
      debrisPlanks,
    });
  }

  /**
   * DYNAMIC OBSTACLE 2: Wind-Slamming Wooden Corral / Ranch Gate!
   */
  private spawnSwingingCorralGate(
    chunkGroup: THREE.Group,
    swingingGates: SwingingCorralGate[],
    obstacles: WorldObstacle[],
    chunkIndex: number,
    hingeX: number,
    groundY: number,
    z: number,
    swingTowardCenterSign: number,
    rng: () => number,
    allocObstacleId: () => number
  ): void {
    // Sturdy timber hinge post
    const postGeo = new THREE.BoxGeometry(0.42, 3.6, 0.42);
    postGeo.userData.uniqueGeo = true;
    const post = new THREE.Mesh(postGeo, this.darkBeamMat);
    post.position.set(hingeX, groundY + 1.6, z);
    post.castShadow = true;
    post.userData.uniqueGeo = true;
    chunkGroup.add(post);

    const gateWidth = 7.4;
    const hingeGroup = new THREE.Group();
    hingeGroup.position.set(hingeX, groundY + 0.35, z);
    chunkGroup.add(hingeGroup);

    // 3 horizontal timber rails + diagonal cross-brace + rusted tin warning sign
    const railGeo = new THREE.BoxGeometry(gateWidth, 0.2, 0.14);
    railGeo.userData.uniqueGeo = true;
    for (const ry of [0.55, 1.35, 2.15]) {
      const rail = new THREE.Mesh(railGeo, this.weatheredTimberMat);
      rail.position.set(swingTowardCenterSign * gateWidth * 0.5, ry, 0);
      rail.castShadow = true;
      rail.userData.uniqueGeo = true;
      hingeGroup.add(rail);
    }

    const endPostGeo = new THREE.BoxGeometry(0.22, 2.1, 0.18);
    endPostGeo.userData.uniqueGeo = true;
    const endPost = new THREE.Mesh(endPostGeo, this.darkBeamMat);
    endPost.position.set(swingTowardCenterSign * (gateWidth - 0.12), 1.35, 0);
    endPost.userData.uniqueGeo = true;
    hingeGroup.add(endPost);

    const signGeo = new THREE.BoxGeometry(1.8, 0.95, 0.08);
    signGeo.userData.uniqueGeo = true;
    const sign = new THREE.Mesh(signGeo, this.rustedCorrugatedMat);
    sign.position.set(swingTowardCenterSign * gateWidth * 0.55, 1.35, 0.1);
    sign.userData.uniqueGeo = true;
    hingeGroup.add(sign);

    const obstacle: WorldObstacle = {
      id: allocObstacleId(),
      chunkIndex,
      kind: 'boulder',
      position: new THREE.Vector3(hingeX, groundY, z),
      radius: 1.85,
      height: 2.6,
      canShearVine: false,
    };
    obstacles.push(obstacle);

    swingingGates.push({
      hingeGroup,
      hingeX,
      hingeY: groundY,
      hingeZ: z,
      gateWidth,
      baseSign: swingTowardCenterSign,
      swingSpeed: lerp(1.5, 2.35, rng()),
      phase: rng() * Math.PI * 2,
      obstacle,
    });
  }

  public populateDesertVillageChunk(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    chunkLength: number,
    rng: () => number,
    sampleHeight: (x: number, z: number) => number,
    getCenterAndHalfWidth: (z: number) => { centerX: number; halfWidth: number; slopeX: number },
    obstacles: WorldObstacle[],
    rootBridges: CrisscrossRootBridge[],
    allocObstacleId: () => number,
    allocBridgeId: () => number
  ): DesertVillageChunkData {
    const windmills: SpinningWindmill[] = [];
    const waterTowers: CollapsingWaterTower[] = [];
    const runawayBarrels: RunawayBarrel[] = [];
    const swingingGates: SwingingCorralGate[] = [];
    const awnings: SwayingAwning[] = [];

    if (chunkIndex < 0) {
      return { windmills, waterTowers, runawayBarrels, swingingGates, awnings };
    }

    // 1. SHANTYTOWN SHACKS FLANKING BOTH SIDES OF THE VILLAGE STREET (plus occasional central island shack!)
    const rowCount = 4;
    for (let r = 0; r < rowCount; r++) {
      const zRow = zStart - (0.12 + r * 0.23 + (rng() - 0.5) * 0.04) * chunkLength;
      if (zRow > -28) continue;
      const { centerX, halfWidth, slopeX } = getCenterAndHalfWidth(zRow);
      const roadYaw = Math.atan2(-slopeX, 1.0);

      for (const side of [-1, 1]) {
        const streetEdgeDist = lerp(halfWidth * 0.52, halfWidth * 0.78, rng());
        const sx = centerX + side * streetEdgeDist;
        const sy = sampleHeight(sx, zRow);
        // Face the front porch inward toward the dusty village street!
        const facingYaw = roadYaw + (side < 0 ? Math.PI * 0.5 : -Math.PI * 0.5) + (rng() - 0.5) * 0.24;

        this.spawnDesertShack(
          chunkGroup,
          sx,
          sy,
          zRow,
          facingYaw,
          rng,
          obstacles,
          chunkIndex,
          allocObstacleId
        );

        // Leaning wooden board fence connecting shacks along the street edge
        if (r < rowCount - 1 && rng() < 0.7) {
          const fenceZ = zRow - 9.5;
          const fy = sampleHeight(sx - side * 2.5, fenceZ);
          const fenceGeo = new THREE.BoxGeometry(0.18, 1.35, 7.5);
          fenceGeo.userData.uniqueGeo = true;
          const fence = new THREE.Mesh(fenceGeo, this.weatheredTimberMat);
          fence.position.set(sx - side * 3.8, fy + 0.65, fenceZ);
          fence.rotation.y = roadYaw + (rng() - 0.5) * 0.15;
          fence.rotation.z = (rng() - 0.5) * 0.12;
          fence.castShadow = true;
          fence.userData.uniqueGeo = true;
          chunkGroup.add(fence);
        }
      }

      // Every odd chunk, place 1 central market shack island splitting the wide road into twin village alleys!
      if (r === 2 && chunkIndex % 2 === 1 && zRow < -55) {
        const islandX = centerX + (rng() - 0.5) * 4.0;
        const islandY = sampleHeight(islandX, zRow);
        this.spawnDesertShack(
          chunkGroup,
          islandX,
          islandY,
          zRow,
          roadYaw + Math.PI,
          rng,
          obstacles,
          chunkIndex,
          allocObstacleId
        );
      }
    }

    // 2. WOODEN TELEGRAPH POLES, SAGGING WIRES & OVERHEAD BURLAP MARKET AWNINGS ACROSS THE STREET!
    let prevLeftWireTop: THREE.Vector3 | null = null;
    let prevRightWireTop: THREE.Vector3 | null = null;

    for (let p = 0; p < 3; p++) {
      const pz = zStart - (0.16 + p * 0.34) * chunkLength;
      if (pz > -22) continue;
      const { centerX, halfWidth } = getCenterAndHalfWidth(pz);
      const spanX = halfWidth * 0.46;

      const leftX = centerX - spanX;
      const rightX = centerX + spanX;
      const leftY = sampleHeight(leftX, pz);
      const rightY = sampleHeight(rightX, pz);
      const poleH = 9.4;

      for (const [px, py] of [
        [leftX, leftY],
        [rightX, rightY],
      ]) {
        const poleGeo = new THREE.CylinderGeometry(0.16, 0.24, poleH, 6);
        poleGeo.userData.uniqueGeo = true;
        const pole = new THREE.Mesh(poleGeo, this.darkBeamMat);
        pole.position.set(px, py + poleH * 0.48, pz);
        pole.rotation.z = (rng() - 0.5) * 0.06;
        pole.castShadow = true;
        pole.userData.uniqueGeo = true;
        chunkGroup.add(pole);

        const crossArmGeo = new THREE.BoxGeometry(1.8, 0.14, 0.14);
        crossArmGeo.userData.uniqueGeo = true;
        const arm = new THREE.Mesh(crossArmGeo, this.weatheredTimberMat);
        arm.position.set(px, py + poleH - 0.55, pz);
        arm.userData.uniqueGeo = true;
        chunkGroup.add(arm);
      }

      const curLeftTop = new THREE.Vector3(leftX, leftY + poleH - 0.55, pz);
      const curRightTop = new THREE.Vector3(rightX, rightY + poleH - 0.55, pz);

      // Overhead sun-faded burlap/terracotta shade awning stretched high across the street!
      const midAwningY = Math.max(leftY, rightY) + 8.3;
      const spanW = rightX - leftX;
      const awningGeo = new THREE.PlaneGeometry(spanW, 3.6, 8, 2);
      awningGeo.rotateX(Math.PI * 0.46);
      awningGeo.userData.uniqueGeo = true;
      // Add natural catenary sag in the middle of the cloth
      const aAttr = awningGeo.attributes.position as THREE.BufferAttribute;
      for (let vi = 0; vi < aAttr.count; vi++) {
        const vx = aAttr.getX(vi);
        const normX = vx / (spanW * 0.5);
        aAttr.setY(vi, aAttr.getY(vi) - (1 - normX * normX) * 0.85);
      }
      awningGeo.computeVertexNormals();

      const awningMesh = new THREE.Mesh(
        awningGeo,
        p % 2 === 0 ? this.burlapAwningMat : this.terracottaClothMat
      );
      awningMesh.position.set(centerX, midAwningY, pz);
      awningMesh.castShadow = true;
      awningMesh.userData.uniqueGeo = true;
      chunkGroup.add(awningMesh);
      awnings.push({
        mesh: awningMesh,
        baseY: midAwningY,
        phase: rng() * Math.PI * 2,
      });

      // Sagging utility wires along the street
      if (prevLeftWireTop && prevRightWireTop) {
        for (const [a, b] of [
          [prevLeftWireTop, curLeftTop],
          [prevRightWireTop, curRightTop],
        ]) {
          const mid = a.clone().lerp(b, 0.5);
          mid.y -= 1.15;
          const wireCurve = new THREE.CatmullRomCurve3([a, mid, b]);
          const wireGeo = new THREE.TubeGeometry(wireCurve, 10, 0.04, 4, false);
          wireGeo.userData.uniqueGeo = true;
          const wireMesh = new THREE.Mesh(wireGeo, this.wireCableMat);
          wireMesh.userData.uniqueGeo = true;
          chunkGroup.add(wireMesh);
        }
      }
      prevLeftWireTop = curLeftTop;
      prevRightWireTop = curRightTop;
    }

    // 3. DRIVABLE RICKETY WOODEN BOARDWALK RAMP IN THE VILLAGE STREET!
    const rampZStart = zStart - 0.36 * chunkLength;
    const rampZEnd = rampZStart - 18.0;
    if (rampZStart < -36) {
      const { centerX, slopeX } = getCenterAndHalfWidth(rampZStart);
      const rampSide = chunkIndex % 2 === 0 ? -1 : 1;
      const rx = centerX + rampSide * 8.5;
      const y0 = sampleHeight(rx, rampZStart) - 0.15;
      const yApex = sampleHeight(rx, rampZEnd) + 2.35;
      const deckHalfW = 2.6;

      // Build timber plank ramp mesh + trestle supports
      const rampGroup = new THREE.Group();
      const len = Math.hypot(rampZEnd - rampZStart, yApex - y0);
      const pitch = Math.atan2(yApex - y0, Math.abs(rampZEnd - rampZStart));
      const plankDeckGeo = new THREE.BoxGeometry(deckHalfW * 2.0, 0.28, len);
      plankDeckGeo.userData.uniqueGeo = true;
      const plankDeck = new THREE.Mesh(plankDeckGeo, this.weatheredTimberMat);
      plankDeck.position.set(rx, (y0 + yApex) * 0.5, (rampZStart + rampZEnd) * 0.5);
      plankDeck.rotation.x = pitch;
      plankDeck.rotation.y = Math.atan2(-slopeX, 1.0);
      plankDeck.castShadow = true;
      plankDeck.receiveShadow = true;
      plankDeck.userData.uniqueGeo = true;
      rampGroup.add(plankDeck);

      for (const sx of [-deckHalfW + 0.25, deckHalfW - 0.25]) {
        const legGeo = new THREE.BoxGeometry(0.26, 2.6, 0.26);
        legGeo.userData.uniqueGeo = true;
        const leg = new THREE.Mesh(legGeo, this.darkBeamMat);
        leg.position.set(rx + sx, yApex - 1.25, rampZEnd + 0.8);
        leg.castShadow = true;
        leg.userData.uniqueGeo = true;
        rampGroup.add(leg);
      }
      chunkGroup.add(rampGroup);

      // Register drivable ramp deck so vehicle wheels climb & launch cleanly off the wooden ramp!
      rootBridges.push({
        id: allocBridgeId(),
        chunkIndex,
        minZ: rampZEnd - 1.0,
        maxZ: rampZStart + 1.0,
        segments: [
          {
            ax: rx,
            ay: y0,
            az: rampZStart,
            bx: rx,
            by: yApex,
            bz: rampZEnd,
            deckHalfWidth: deckHalfW,
            thickness: 0.9,
          },
        ],
      });
    }

    // 4. SPINNING RUSTED FARM WIND-PUMPS (2 per chunk!)
    for (let w = 0; w < 2; w++) {
      const wz = zStart - (0.22 + w * 0.52) * chunkLength;
      if (wz > -24) continue;
      const { centerX, halfWidth } = getCenterAndHalfWidth(wz);
      const wSide = (chunkIndex + w) % 2 === 0 ? -1 : 1;
      const wx = centerX + wSide * lerp(halfWidth * 0.68, halfWidth * 0.95, rng());
      const wy = sampleHeight(wx, wz);
      this.spawnSpinningWindPump(chunkGroup, windmills, wx, wy, wz, rng);
    }

    // 5. DYNAMIC OBSTACLE A: COLLAPSING WOODEN WATER TOWER TRESTLE!
    const wtZ = zStart - 0.58 * chunkLength;
    if (wtZ < -45) {
      const { centerX, halfWidth } = getCenterAndHalfWidth(wtZ);
      const side = chunkIndex % 2 === 0 ? 1 : -1;
      const wtX = centerX + side * (halfWidth * 0.42);
      const wtY = sampleHeight(wtX, wtZ);
      this.spawnCollapsingWaterTower(
        chunkGroup,
        waterTowers,
        obstacles,
        chunkIndex,
        wtX,
        wtY,
        wtZ,
        -side, // falls inward toward the village street!
        rng,
        allocObstacleId
      );
    }

    // 6. DYNAMIC OBSTACLE B: RUNAWAY ROLLING 55-GALLON OIL DRUMS & TRACTOR TIRES ACROSS THE STREET!
    const barrelCount = 4;
    for (let b = 0; b < barrelCount; b++) {
      const bz = zStart - (0.18 + b * 0.21 + rng() * 0.04) * chunkLength;
      if (bz > -30) continue;
      const { centerX, halfWidth } = getCenterAndHalfWidth(bz);
      const dirX = b % 2 === 0 ? 1 : -1;
      const spawnX = centerX - dirX * (halfWidth * 0.58);
      const startX = spawnX + dirX * rng() * halfWidth * 0.9;
      const startY = sampleHeight(startX, bz);

      const isTire = b % 3 === 2;
      const radius = isTire ? 0.68 : 0.52;
      const mesh = new THREE.Mesh(
        isTire ? this.tractorTireGeo : this.oilDrumGeo,
        isTire
          ? this.oldTireRubberMat
          : b % 2 === 0
          ? this.oilDrumRedMat
          : this.oilDrumOchreMat
      );
      if (!isTire) {
        // Lay cylinder on its side along Z so rolling across X spins cleanly!
        mesh.rotation.x = Math.PI * 0.5;
      }
      mesh.position.set(startX, startY + radius, bz);
      mesh.castShadow = true;
      chunkGroup.add(mesh);

      const speed = lerp(7.5, 13.0, rng());
      const obs: WorldObstacle = {
        id: allocObstacleId(),
        chunkIndex,
        kind: 'rolling_boulder',
        position: new THREE.Vector3(startX, startY, bz),
        radius: radius * 1.15,
        height: radius * 2.0,
        velocityX: dirX * speed,
        velocityZ: 0,
        canShearVine: false,
      };
      obstacles.push(obs);

      runawayBarrels.push({
        mesh,
        kind: isTire ? 'tractor_tire' : 'oil_drum',
        position: new THREE.Vector3(startX, startY + radius, bz),
        spawnX,
        spawnZ: bz,
        dirX,
        speed,
        radius,
        maxTravelX: halfWidth * 1.18,
        phase: rng() * Math.PI * 2,
        obstacle: obs,
      });
    }

    // 7. DYNAMIC OBSTACLE C: WIND-SLAMMING WOODEN RANCH / CORRAL GATES!
    const gateZ = zStart - 0.81 * chunkLength;
    if (gateZ < -42) {
      const { centerX, halfWidth } = getCenterAndHalfWidth(gateZ);
      for (const gSide of [-1, 1]) {
        const gx = centerX + gSide * (halfWidth * 0.36);
        const gy = sampleHeight(gx, gateZ);
        this.spawnSwingingCorralGate(
          chunkGroup,
          swingingGates,
          obstacles,
          chunkIndex,
          gx,
          gy,
          gateZ,
          -gSide,
          rng,
          allocObstacleId
        );
      }
    }

    return { windmills, waterTowers, runawayBarrels, swingingGates, awnings };
  }

  public stepChunkHazards(
    data: DesertVillageChunkData,
    dt: number,
    elapsedTime: number,
    focusPos: THREE.Vector3,
    vehiclePos: THREE.Vector3,
    sampleHeight: (x: number, z: number) => number
  ): void {
    // 1. Spin Farm Wind-Pumps in the canyon wind!
    for (const wm of data.windmills) {
      wm.rotorGroup.rotation.z += wm.spinSpeed * dt;
      wm.yawGroup.rotation.y += Math.sin(elapsedTime * 0.4 + wm.phase) * 0.08 * dt;
    }

    // 2. Step Collapsing Wooden Water Towers when approached!
    for (const wt of data.waterTowers) {
      if (
        !wt.active &&
        (Math.abs(focusPos.z - wt.z) < 98.0 || Math.abs(vehiclePos.z - wt.z) < 98.0)
      ) {
        wt.active = true;
      }
      if (!wt.active) continue;

      wt.age += dt;
      const fallT = clamp((wt.age - 0.85) / 1.95, 0, 1);
      let tilt = fallT * fallT * 1.44;
      if (wt.age < 0.85) {
        tilt += Math.sin(wt.age * 36.0) * 0.025; // Creaking timber leg snap tremor!
      }
      wt.pivotGroup.rotation.z = -wt.fallSide * tilt;

      const landedX = wt.baseX + wt.fallSide * Math.sin(tilt) * 8.6;
      if (fallT > 0.65) {
        wt.obstacle.position.set(landedX, wt.baseY, wt.z);
        wt.obstacle.radius = 2.65;
        wt.obstacle.height = 3.6;
      }

      // Burst wooden plank splinters upon ground slam!
      const slamAge = wt.age - 2.8;
      for (const dp of wt.debrisPlanks) {
        if (slamAge >= 0 && slamAge < 1.55) {
          dp.mesh.visible = true;
          const h = Math.max(0, dp.speed * slamAge - 5.2 * slamAge * slamAge);
          dp.mesh.position.set(
            landedX + Math.cos(dp.angle) * dp.speed * 0.55 * slamAge,
            wt.baseY + 0.35 + h,
            wt.z + Math.sin(dp.angle) * dp.speed * 0.55 * slamAge
          );
          dp.mesh.rotation.set(slamAge * 5.0, dp.angle, slamAge * 4.0);
        } else {
          dp.mesh.visible = false;
        }
      }
    }

    // 3. Step Runaway Rolling 55-Gallon Oil Drums & Tractor Tires!
    for (const rb of data.runawayBarrels) {
      rb.position.x += rb.dirX * rb.speed * dt;
      const gy = sampleHeight(rb.position.x, rb.position.z);
      const bounce = Math.abs(Math.sin(elapsedTime * 6.8 + rb.phase)) * 0.24;
      rb.position.y = gy + rb.radius + bounce;

      rb.mesh.position.copy(rb.position);
      if (rb.kind === 'tractor_tire') {
        rb.mesh.rotation.z -= rb.dirX * (rb.speed / rb.radius) * dt;
      } else {
        rb.mesh.rotation.z -= rb.dirX * (rb.speed / rb.radius) * dt;
      }

      rb.obstacle.position.set(rb.position.x, gy, rb.position.z);

      if (Math.abs(rb.position.x - rb.spawnX) > rb.maxTravelX) {
        rb.position.x = rb.spawnX;
      }
    }

    // 4. Step Wind-Slamming Wooden Corral / Ranch Gates!
    for (const g of data.swingingGates) {
      // Swings between open (75° clear) and slammed across the gap (8°)
      const gust = Math.sin(elapsedTime * g.swingSpeed + g.phase);
      const swingYaw = g.baseSign * (0.62 + gust * 0.68);
      g.hingeGroup.rotation.y = swingYaw;

      const midDist = g.gateWidth * 0.58;
      const obsX = g.hingeX + g.baseSign * Math.cos(swingYaw) * midDist;
      const obsZ = g.hingeZ - g.baseSign * Math.sin(swingYaw) * midDist;
      g.obstacle.position.set(obsX, g.hingeY, obsZ);
    }

    // 5. Subtle canyon breeze ripple on overhead burlap awnings
    for (const aw of data.awnings) {
      aw.mesh.position.y = aw.baseY + Math.sin(elapsedTime * 2.6 + aw.phase) * 0.14;
      aw.mesh.rotation.z = Math.sin(elapsedTime * 1.9 + aw.phase) * 0.035;
    }
  }
}

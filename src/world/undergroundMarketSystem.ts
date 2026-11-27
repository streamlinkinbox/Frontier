import * as THREE from 'three';
import { ProceduralTextureLibrary } from '../graphics/proceduralTextures';
import { clamp, fbm2D, lerp, mulberry32, smoothstep } from '../math/noise';
import { WorldObstacle } from './trailCorridor';

export interface FlickeringNeonNode {
  signMatIndex: number;
  light: THREE.PointLight | null;
  puddleMesh: THREE.Mesh;
  bulbMesh: THREE.Mesh;
  worldPos: THREE.Vector3;
  baseColor: THREE.Color;
  baseIntensity: number;
  flickerSeed: number;
  brokenBallast: boolean; // True = erratic dying neon/fluorescent stutter + occasional sparks!
  sparkCooldown: number;
}

export interface SmashableMarketCrate {
  mesh: THREE.Mesh;
  pos: THREE.Vector3;
  vel: THREE.Vector3;
  rotVel: THREE.Vector3;
  baseY: number;
  smashed: boolean;
  settleTimer: number;
}

export interface UndergroundMarketChunkData {
  group: THREE.Group;
  flickerNodes: FlickeringNeonNode[];
  crates: SmashableMarketCrate[];
  zStart: number;
  zLength: number;
}

interface NeonPaletteEntry {
  hex: number;
  name: string;
  tileIndex: number;
}

const NEON_PALETTE: NeonPaletteEntry[] = [
  { hex: 0x00f0ff, name: 'CYAN_ELECTRO', tileIndex: 0 },
  { hex: 0xff2478, name: 'MAGENTA_NOODLE', tileIndex: 1 },
  { hex: 0xffa61a, name: 'AMBER_TEA', tileIndex: 2 },
  { hex: 0x1dff8e, name: 'JADE_PHARMACY', tileIndex: 3 },
  { hex: 0xff2b2b, name: 'CRIMSON_MEAT', tileIndex: 4 },
  { hex: 0x9e47ff, name: 'VIOLET_ARCADE', tileIndex: 5 },
  { hex: 0x00e5ff, name: 'SPLIT_LEFT', tileIndex: 6 },
  { hex: 0xff3b8d, name: 'SPLIT_RIGHT', tileIndex: 7 },
];

/**
 * Computes the subterranean market concourse half-width at local chunk offset u in [0, 112]:
 * - u = 0..20m: Winding single-aisle crooked bazaar & corner bend (halfW = 12.4m)
 * - u = 20..38m: Y-Junction Diverging Throat (flares smoothly 12.4m -> 23.2m!)
 * - u = 38..88m: 50m-long Twin Split Market Aisles around Central Shop Island (halfW = 23.2m)
 * - u = 88..104m: Y-Confluence Merge back into subway-tile plaza (23.2m -> 12.4m)
 * - u = 104..112m: Vaulted concourse connector (halfW = 12.4m)
 */
export function sampleUndergroundMarketHalfWidth(uInChunk: number, chunkLen = 112): number {
  const u = ((uInChunk % chunkLen) + chunkLen) % chunkLen;
  const splitOpen = smoothstep(20.0, 38.0, u);
  const splitClose = 1.0 - smoothstep(87.5, 104.5, u);
  const splitFactor = splitOpen * splitClose;
  return lerp(12.4, 23.2, splitFactor);
}

/**
 * Builds Level [9] ABANDONED UNDERGROUND MARKET ('UNDERGROUND_MARKET'):
 * - Enclosed subterranean concrete ceiling vaults, grime-stained subway-tile walls,
 *   structural hazard-striped pillars, overhead HVAC ducts, and wet neon-reflective tile floors
 * - True Y-Junction Splitting Aisles (forking around a chamfered central wedge kiosk &
 *   two-block shop island into Left Electronics Arcade vs. Right Wet-Market Food Alley,
 *   plus a 90-degree mid-island cross-alley with 4 chamfered corner storefronts!)
 * - Abandoned Recessed Shops (corrugated steel roll-down shutters at jammed/crooked heights,
 *   awnings, dusty counters, rusted AC condenser units) & Open-Air Hawker Stalls
 * - Procedural Glowing Neon Blade Signs, Y-Split Directional Arrows, Flickering Broken
 *   Fluorescent Ceiling Strips, Wet-Floor Puddle Reflections, and Smashable Market Crates!
 */
export class UndergroundMarketBuilder {
  public readonly atmosphereFxGroup = new THREE.Group();

  // Materials
  private readonly shellVaultMat: THREE.MeshStandardMaterial;
  private readonly wetTileFloorMat: THREE.MeshStandardMaterial;
  private readonly shopConcreteMat: THREE.MeshStandardMaterial;
  private readonly shutterSteelMat: THREE.MeshStandardMaterial;
  private readonly rustedMetalMat: THREE.MeshStandardMaterial;
  private readonly stallTarpMat: THREE.MeshStandardMaterial;
  private readonly crateMat: THREE.MeshStandardMaterial;
  private readonly signFrameMat: THREE.MeshStandardMaterial;
  private readonly neonAtlasTex: THREE.CanvasTexture;
  private readonly neonSignMats: THREE.MeshBasicMaterial[] = [];
  private readonly neonTubeMats: THREE.MeshBasicMaterial[] = [];
  private readonly puddleGlowTex: THREE.CanvasTexture;
  private readonly fluorescentLitMat: THREE.MeshBasicMaterial;
  private readonly interiorDimMat: THREE.MeshStandardMaterial;

  // Shared Architectural & Prop Geometries
  private readonly boxGeo: THREE.BoxGeometry;
  private readonly pillarGeo: THREE.BoxGeometry;
  private readonly shutterGeo: THREE.BoxGeometry;
  private readonly awningGeo: THREE.BufferGeometry;
  private readonly acUnitGeo: THREE.BufferGeometry;
  private readonly stallFrameGeo: THREE.BufferGeometry;
  private readonly stallCanopyGeo: THREE.BufferGeometry;
  private readonly crateGeo: THREE.BoxGeometry;
  private readonly ductGeo: THREE.CylinderGeometry;
  private readonly signPanelGeo: THREE.BoxGeometry;
  private readonly neonTubeGeo: THREE.CylinderGeometry;
  private readonly puddlePlaneGeo: THREE.PlaneGeometry;

  // Floating subterranean dust motes in neon beams
  private readonly dustCount = 900;
  private dustPoints!: THREE.Points;
  private dustPositions!: Float32Array;
  private dustVelocities!: Float32Array;

  // Callbacks
  public onCrateSmash: ((pos: THREE.Vector3) => void) | null = null;
  public onNeonSparkZap: ((pos: THREE.Vector3) => void) | null = null;

  constructor(textures: ProceduralTextureLibrary) {
    this.atmosphereFxGroup.name = 'UndergroundMarketAtmosphereFX';

    this.neonAtlasTex = this.createNeonSignAtlasTexture();
    this.puddleGlowTex = this.createSoftRadialGlowTexture();

    // Subterranean tiled wall + vaulted concrete ceiling material
    this.shellVaultMat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.85, 0.85),
      roughness: 0.78,
      metalness: 0.08,
      vertexColors: true,
      side: THREE.DoubleSide,
    });

    // Wet damp subterranean market tile floor (reflects neon point lights!)
    this.wetTileFloorMat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      map: textures.rock.map,
      normalMap: textures.terrainDetail.normalMap,
      normalScale: new THREE.Vector2(0.38, 0.38),
      roughness: 0.19,
      metalness: 0.28,
      vertexColors: true,
    });

    this.shopConcreteMat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      map: textures.rock.map,
      normalMap: textures.rock.normalMap,
      normalScale: new THREE.Vector2(0.75, 0.75),
      roughness: 0.76,
      metalness: 0.1,
    });

    // Corrugated steel security shutters (catches bright specular neon highlights!)
    this.shutterSteelMat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      map: textures.tireTread.map,
      normalMap: textures.tireTread.normalMap,
      normalScale: new THREE.Vector2(1.2, 1.2),
      roughness: 0.38,
      metalness: 0.68,
    });

    this.rustedMetalMat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      map: textures.bark.map,
      normalMap: textures.rock.normalMap,
      roughness: 0.62,
      metalness: 0.52,
    });

    this.stallTarpMat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      map: textures.terrainDetail.map,
      roughness: 0.72,
      metalness: 0.04,
      side: THREE.DoubleSide,
    });

    this.crateMat = new THREE.MeshStandardMaterial({
      color: 0xb87638,
      map: textures.bark.map,
      roughness: 0.65,
      metalness: 0.08,
    });

    this.signFrameMat = new THREE.MeshStandardMaterial({
      color: 0x161920,
      roughness: 0.45,
      metalness: 0.72,
    });

    this.interiorDimMat = new THREE.MeshStandardMaterial({
      color: 0x0d1016,
      roughness: 0.9,
      metalness: 0.1,
    });

    this.fluorescentLitMat = new THREE.MeshBasicMaterial({
      color: 0xdff6ff,
    });

    for (let i = 0; i < NEON_PALETTE.length; i++) {
      const entry = NEON_PALETTE[i];
      const tileTex = this.createSingleSignTileTexture(i, entry.hex);
      this.neonSignMats.push(
        new THREE.MeshBasicMaterial({
          map: tileTex,
        })
      );
      this.neonTubeMats.push(
        new THREE.MeshBasicMaterial({
          color: entry.hex,
        })
      );
    }

    // Shared Geometries
    this.boxGeo = new THREE.BoxGeometry(1, 1, 1);
    this.pillarGeo = new THREE.BoxGeometry(0.95, 8.4, 0.95);
    this.shutterGeo = new THREE.BoxGeometry(0.12, 1.0, 1.0);
    this.awningGeo = this.createSlopedAwningGeo();
    this.acUnitGeo = this.createAcCondenserGeo();
    this.stallFrameGeo = this.createHawkerStallFrameGeo();
    this.stallCanopyGeo = this.createHawkerCanopyGeo();
    this.crateGeo = new THREE.BoxGeometry(0.92, 0.68, 0.74);
    this.ductGeo = new THREE.CylinderGeometry(0.42, 0.42, 1.0, 8);
    this.ductGeo.rotateX(Math.PI * 0.5);
    this.signPanelGeo = new THREE.BoxGeometry(0.14, 1.0, 1.0);
    this.neonTubeGeo = new THREE.CylinderGeometry(0.045, 0.045, 1.0, 6);
    const pGeo = new THREE.PlaneGeometry(1.0, 1.0);
    pGeo.rotateX(-Math.PI * 0.5);
    this.puddlePlaneGeo = pGeo;

    this.initSubterraneanDustMotes();
  }

  /**
   * Procedural crisp glowing neon signboard textures (abandoned underground market shops + Y-split arrows!)
   */
  private createNeonSignAtlasTexture(): THREE.CanvasTexture {
    const canvas = document.createElement('canvas');
    canvas.width = 64;
    canvas.height = 64;
    return new THREE.CanvasTexture(canvas);
  }

  private createSingleSignTileTexture(tileIdx: number, neonHex: number): THREE.CanvasTexture {
    const canvas = document.createElement('canvas');
    canvas.width = 256;
    canvas.height = 128;
    const ctx = canvas.getContext('2d')!;

    const col = new THREE.Color(neonHex);
    const r = Math.round(col.r * 255);
    const g = Math.round(col.g * 255);
    const b = Math.round(col.b * 255);
    const brightCss = `rgb(${Math.min(255, r + 95)}, ${Math.min(255, g + 95)}, ${Math.min(255, b + 95)})`;
    const coreCss = `rgb(${r}, ${g}, ${b})`;
    const dimBgCss = `rgb(${Math.round(r * 0.12 + 10)}, ${Math.round(g * 0.12 + 12)}, ${Math.round(b * 0.14 + 18)})`;

    // Dark weathered acrylic sign backing
    ctx.fillStyle = dimBgCss;
    ctx.fillRect(0, 0, 256, 128);

    // Outer neon border tube with double-stroke glow
    ctx.strokeStyle = coreCss;
    ctx.lineWidth = 9;
    ctx.strokeRect(10, 10, 236, 108);
    ctx.strokeStyle = brightCss;
    ctx.lineWidth = 3.5;
    ctx.strokeRect(10, 10, 236, 108);

    // Inner graphic motifs per shop category (pure geometric neon tube bending!)
    ctx.strokeStyle = brightCss;
    ctx.fillStyle = coreCss;
    ctx.lineWidth = 5;

    if (tileIdx === 6) {
      // Y-JUNCTION LEFT SPLIT DIRECTIONAL NEON SIGN (<< ARCADE)
      for (const ox of [54, 88]) {
        ctx.beginPath();
        ctx.moveTo(ox + 26, 32);
        ctx.lineTo(ox, 64);
        ctx.lineTo(ox + 26, 96);
        ctx.stroke();
      }
      // Stylised neon circuit / bars on right
      ctx.fillRect(132, 40, 88, 10);
      ctx.fillRect(132, 60, 64, 10);
      ctx.fillRect(132, 80, 96, 10);
    } else if (tileIdx === 7) {
      // Y-JUNCTION RIGHT SPLIT DIRECTIONAL NEON SIGN (MARKET >>)
      ctx.fillRect(32, 40, 88, 10);
      ctx.fillRect(48, 60, 72, 10);
      ctx.fillRect(26, 80, 94, 10);
      for (const ox of [148, 182]) {
        ctx.beginPath();
        ctx.moveTo(ox, 32);
        ctx.lineTo(ox + 26, 64);
        ctx.lineTo(ox, 96);
        ctx.stroke();
      }
    } else if (tileIdx === 0) {
      // Electronics / Retro Radio Repair (oscilloscope wave + circle)
      ctx.beginPath();
      ctx.arc(58, 64, 26, 0, Math.PI * 2);
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(104, 64);
      ctx.lineTo(126, 34);
      ctx.lineTo(150, 94);
      ctx.lineTo(174, 38);
      ctx.lineTo(198, 84);
      ctx.lineTo(224, 64);
      ctx.stroke();
    } else if (tileIdx === 1) {
      // Underground Noodle / Ramen Stall (steaming bowl!)
      ctx.beginPath();
      ctx.arc(72, 68, 30, 0, Math.PI);
      ctx.closePath();
      ctx.stroke();
      for (const sx of [56, 72, 88]) {
        ctx.beginPath();
        ctx.moveTo(sx, 56);
        ctx.quadraticCurveTo(sx - 7, 42, sx + 4, 28);
        ctx.stroke();
      }
      ctx.fillRect(124, 44, 96, 12);
      ctx.fillRect(124, 72, 74, 12);
    } else if (tileIdx === 2) {
      // Night Tea / Lantern Kiosk
      ctx.strokeRect(42, 34, 48, 60);
      ctx.fillRect(54, 24, 24, 10);
      ctx.fillRect(54, 94, 24, 10);
      ctx.fillRect(114, 42, 104, 11);
      ctx.fillRect(114, 64, 82, 11);
      ctx.fillRect(114, 84, 96, 8);
    } else if (tileIdx === 3) {
      // Underground Apothecary / Pharmacy Cross
      ctx.fillRect(56, 32, 22, 64);
      ctx.fillRect(35, 53, 64, 22);
      ctx.fillRect(118, 44, 102, 12);
      ctx.fillRect(118, 70, 86, 12);
    } else if (tileIdx === 4) {
      // Wet-Market Fishmonger / Butcher Emblem
      ctx.beginPath();
      ctx.ellipse(76, 64, 34, 18, 0, 0, Math.PI * 2);
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(110, 64);
      ctx.lineTo(128, 46);
      ctx.lineTo(128, 82);
      ctx.closePath();
      ctx.stroke();
      ctx.fillRect(146, 46, 78, 12);
      ctx.fillRect(146, 70, 66, 12);
    } else {
      // Violet Underground Arcade / Pawn
      ctx.beginPath();
      ctx.moveTo(68, 30);
      ctx.lineTo(98, 64);
      ctx.lineTo(68, 98);
      ctx.lineTo(38, 64);
      ctx.closePath();
      ctx.stroke();
      ctx.fillRect(118, 40, 100, 12);
      ctx.fillRect(118, 62, 78, 10);
      ctx.fillRect(118, 82, 92, 10);
    }

    // Subtle grime / burnt-out vertical band for abandoned wear
    ctx.fillStyle = 'rgba(8, 10, 14, 0.24)';
    ctx.fillRect(156, 0, 14, 128);

    const tex = new THREE.CanvasTexture(canvas);
    tex.colorSpace = THREE.SRGBColorSpace;
    tex.needsUpdate = true;
    return tex;
  }

  private createSoftRadialGlowTexture(): THREE.CanvasTexture {
    const canvas = document.createElement('canvas');
    canvas.width = 64;
    canvas.height = 64;
    const ctx = canvas.getContext('2d')!;
    const grad = ctx.createRadialGradient(32, 32, 2, 32, 32, 32);
    grad.addColorStop(0.0, 'rgba(255, 255, 255, 0.95)');
    grad.addColorStop(0.4, 'rgba(255, 255, 255, 0.42)');
    grad.addColorStop(1.0, 'rgba(255, 255, 255, 0.0)');
    ctx.fillStyle = grad;
    ctx.fillRect(0, 0, 64, 64);
    const tex = new THREE.CanvasTexture(canvas);
    tex.needsUpdate = true;
    return tex;
  }

  private createSlopedAwningGeo(): THREE.BufferGeometry {
    const geo = new THREE.BoxGeometry(1.0, 0.14, 1.0);
    const pos = geo.attributes.position as THREE.BufferAttribute;
    for (let i = 0; i < pos.count; i++) {
      const x = pos.getX(i); // +0.5 extends outward into aisle
      if (x > 0) {
        pos.setY(i, pos.getY(i) - 0.36);
      }
    }
    geo.computeVertexNormals();
    return geo;
  }

  private createAcCondenserGeo(): THREE.BufferGeometry {
    const body = new THREE.BoxGeometry(0.55, 0.78, 1.08);
    const grille = new THREE.CylinderGeometry(0.28, 0.28, 0.08, 10);
    grille.rotateZ(Math.PI * 0.5);
    grille.translate(0.27, 0.02, -0.15);
    return this.mergeGeometries([body, grille]);
  }

  private createHawkerStallFrameGeo(): THREE.BufferGeometry {
    const parts: THREE.BufferGeometry[] = [];
    // Counter base
    const counter = new THREE.BoxGeometry(1.55, 0.92, 2.75);
    counter.translate(0, 0.46, 0);
    parts.push(counter);
    // 4 tubular steel corner poles supporting canopy
    for (const sx of [-0.72, 0.72]) {
      for (const sz of [-1.3, 1.3]) {
        const pole = new THREE.CylinderGeometry(0.04, 0.04, 2.55, 6);
        pole.translate(sx, 1.28, sz);
        parts.push(pole);
      }
    }
    return this.mergeGeometries(parts);
  }

  private createHawkerCanopyGeo(): THREE.BufferGeometry {
    const geo = new THREE.PlaneGeometry(2.15, 3.15, 4, 4);
    geo.rotateX(-Math.PI * 0.5);
    const pos = geo.attributes.position as THREE.BufferAttribute;
    for (let i = 0; i < pos.count; i++) {
      const x = pos.getX(i);
      const z = pos.getZ(i);
      // Center ridge peak + natural sagging tattered fabric droop
      const ridge = (1.0 - Math.abs(x) / 1.075) * 0.34;
      const sag = -Math.cos((z / 3.15) * Math.PI) * 0.07;
      pos.setY(i, ridge + sag);
    }
    geo.computeVertexNormals();
    return geo;
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

  private initSubterraneanDustMotes(): void {
    this.dustPositions = new Float32Array(this.dustCount * 3);
    this.dustVelocities = new Float32Array(this.dustCount * 3);
    const colors = new Float32Array(this.dustCount * 3);
    const rng = mulberry32(448192);
    const cCyan = new THREE.Color(0x6beeff);
    const cPink = new THREE.Color(0xff7bc2);
    const cAmber = new THREE.Color(0xffcc70);

    for (let i = 0; i < this.dustCount; i++) {
      const ix = i * 3;
      this.dustPositions[ix] = (rng() - 0.5) * 64;
      this.dustPositions[ix + 1] = rng() * 8.2;
      this.dustPositions[ix + 2] = (rng() - 0.5) * 120;

      this.dustVelocities[ix] = (rng() - 0.5) * 0.55;
      this.dustVelocities[ix + 1] = (rng() - 0.45) * 0.32;
      this.dustVelocities[ix + 2] = (rng() - 0.5) * 0.55;

      const pick = rng();
      const col = pick < 0.42 ? cCyan : pick < 0.75 ? cPink : cAmber;
      colors[ix] = col.r;
      colors[ix + 1] = col.g;
      colors[ix + 2] = col.b;
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(this.dustPositions, 3));
    geo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
    const mat = new THREE.PointsMaterial({
      size: 0.16,
      vertexColors: true,
      transparent: true,
      opacity: 0.55,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });
    this.dustPoints = new THREE.Points(geo, mat);
    this.dustPoints.frustumCulled = false;
    this.atmosphereFxGroup.add(this.dustPoints);
  }

  /**
   * Populates a 112m chunk of the Abandoned Underground Market with:
   * - Curved subterranean ceiling vault + stained subway-tile outer walls + wet floor
   * - True Y-Junction Splitting Aisle Island (Front Wedge Kiosk + Left Arcade + Right Wet-Market
   *   + 90-degree Mid-Island Cross-Alley with 4 Corner Shops + Rear Merge Plaza!)
   * - Recessed shuttered shops, hawker stalls, flickering neon blade signs, overhead HVAC & lights
   */
  public populateUndergroundMarketChunk(
    chunkGroup: THREE.Group,
    chunkIndex: number,
    zStart: number,
    zLength: number,
    sampleCourseX: (z: number) => number,
    obstacles: WorldObstacle[],
    nextObstacleId: () => number
  ): UndergroundMarketChunkData {
    const group = new THREE.Group();
    group.name = `UndergroundMarketChunk_${chunkIndex}`;
    chunkGroup.add(group);
    const rng = mulberry32(chunkIndex * 83491 + 51923);

    const flickerNodes: FlickeringNeonNode[] = [];
    const crates: SmashableMarketCrate[] = [];

    const tangentYawAt = (z: number): number => {
      const x0 = sampleCourseX(z + 1.0);
      const x1 = sampleCourseX(z - 1.0);
      // Forward travel is along -Z; yaw rotates local -Z toward (x1 - x0, -2.0)
      return Math.atan2(-(x1 - x0), 2.0);
    };

    // -------------------------------------------------------------------------
    // 1. CONTINUOUS CURVED & SPLITTING SUBTERRANEAN SHELL (Vault + Walls + Wet Floor)
    // -------------------------------------------------------------------------
    this.buildSubterraneanConcourseShell(group, zStart, zLength, sampleCourseX);

    // Collect batched static instance transforms for rock-solid 60 FPS!
    const concreteMats: THREE.Matrix4[] = [];
    const concreteColors: THREE.Color[] = [];
    const shutterMats: THREE.Matrix4[] = [];
    const shutterColors: THREE.Color[] = [];
    const awningMats: THREE.Matrix4[] = [];
    const awningColors: THREE.Color[] = [];
    const acMats: THREE.Matrix4[] = [];
    const interiorMats: THREE.Matrix4[] = [];
    const pillarMats: THREE.Matrix4[] = [];
    const pillarColors: THREE.Color[] = [];
    const ductMats: THREE.Matrix4[] = [];
    const stallFrameMats: THREE.Matrix4[] = [];
    const stallCanopyMats: THREE.Matrix4[] = [];
    const stallCanopyColors: THREE.Color[] = [];
    const signFrameMats: THREE.Matrix4[] = [];
    const fluorFixtureMats: THREE.Matrix4[] = [];

    const dummy = new THREE.Object3D();
    const tmpMat = new THREE.Matrix4();
    const colTmp = new THREE.Color();

    // Helper: Spawns a glowing Neon Signboard + Wet Floor Puddle Reflection + optional PointLight!
    const addNeonSign = (
      wx: number,
      wy: number,
      wz: number,
      yaw: number,
      widthZ: number,
      heightY: number,
      paletteIdx: number,
      brokenBallast: boolean,
      withHeroPointLight: boolean
    ) => {
      const entry = NEON_PALETTE[paletteIdx % NEON_PALETTE.length];
      const baseCol = new THREE.Color(entry.hex);

      // Metal mounting frame + bracket arm
      dummy.position.set(wx, wy, wz);
      dummy.rotation.set(0, yaw, 0);
      dummy.scale.set(1.12, heightY + 0.14, widthZ + 0.14);
      dummy.updateMatrix();
      signFrameMats.push(dummy.matrix.clone());

      // Glowing double-sided neon panel
      const signMesh = new THREE.Mesh(
        this.signPanelGeo,
        this.neonSignMats[paletteIdx % this.neonSignMats.length]
      );
      signMesh.position.set(wx, wy, wz);
      signMesh.rotation.set(0, yaw, 0);
      signMesh.scale.set(1.22, heightY, widthZ);
      group.add(signMesh);

      // Wet concrete floor reflection decal directly underneath!
      const puddleMat = new THREE.MeshBasicMaterial({
        map: this.puddleGlowTex,
        color: entry.hex,
        transparent: true,
        opacity: 0.34,
        blending: THREE.AdditiveBlending,
        depthWrite: false,
      });
      const puddleMesh = new THREE.Mesh(this.puddlePlaneGeo, puddleMat);
      puddleMesh.position.set(wx, 0.045, wz);
      puddleMesh.scale.set(Math.max(4.5, widthZ * 2.4), 1, Math.max(4.5, widthZ * 2.6));
      group.add(puddleMesh);

      let ptLight: THREE.PointLight | null = null;
      if (withHeroPointLight) {
        ptLight = new THREE.PointLight(entry.hex, 18.0, 26.0, 1.5);
        ptLight.position.set(wx, wy - 0.35, wz);
        group.add(ptLight);
      }

      flickerNodes.push({
        signMatIndex: paletteIdx,
        light: ptLight,
        puddleMesh,
        bulbMesh: signMesh,
        worldPos: new THREE.Vector3(wx, wy, wz),
        baseColor: baseCol,
        baseIntensity: ptLight ? 18.0 : 0,
        flickerSeed: rng() * 100.0,
        brokenBallast,
        sparkCooldown: rng() * 2.5,
      });
    };

    // Helper: Builds an Abandoned Recessed Storefront Bay facing along `facingYaw`!
    // In local frame of `facingYaw`, +X points OUTWARD into the drivable aisle!
    const addStorefrontBay = (
      wx: number,
      wz: number,
      facingYaw: number,
      bayWidth: number,
      shopDepth: number,
      themeBranch: 'MIXED' | 'ELECTRO_LEFT' | 'FOOD_RIGHT',
      isCornerHero: boolean
    ) => {
      const cosY = Math.cos(facingYaw);
      const sinY = Math.sin(facingYaw);
      const localToWorld = (lx: number, ly: number, lz: number): THREE.Vector3 =>
        new THREE.Vector3(wx + lx * cosY + lz * sinY, ly, wz - lx * sinY + lz * cosY);

      // 1. Dark recessed shop interior alcove + back wall
      const backCenter = localToWorld(-shopDepth * 0.5, 2.25, 0);
      dummy.position.copy(backCenter);
      dummy.rotation.set(0, facingYaw, 0);
      dummy.scale.set(shopDepth, 4.5, bayWidth - 0.35);
      dummy.updateMatrix();
      interiorMats.push(dummy.matrix.clone());

      // Upper concrete fascia beam above shopfront (y = 4.5 .. 7.8m)
      const fasciaPos = localToWorld(-shopDepth * 0.45, 6.15, 0);
      dummy.position.copy(fasciaPos);
      dummy.rotation.set(0, facingYaw, 0);
      dummy.scale.set(shopDepth * 1.05, 3.3, bayWidth + 0.12);
      dummy.updateMatrix();
      concreteMats.push(dummy.matrix.clone());
      concreteColors.push(colTmp.setHex(isCornerHero ? 0x3a3f4a : 0x2f343d).clone());

      // Left & Right dividing shop pilasters
      for (const sSide of [-1, 1]) {
        const pilPos = localToWorld(-0.18, 3.9, sSide * (bayWidth * 0.5));
        dummy.position.copy(pilPos);
        dummy.rotation.set(0, facingYaw, 0);
        dummy.scale.set(0.55, 7.8, 0.48);
        dummy.updateMatrix();
        concreteMats.push(dummy.matrix.clone());
        concreteColors.push(colTmp.setHex(0x464b55).clone());
      }

      // Abandoned shop counter inside half-open shutters
      const shutterDrop = rng();
      const shutterOpenHeight =
        shutterDrop < 0.36 ? 0.0 : shutterDrop < 0.76 ? lerp(1.1, 2.3, rng()) : 3.2;
      if (shutterOpenHeight > 0.6) {
        const cntPos = localToWorld(-0.55, 0.52, 0);
        dummy.position.copy(cntPos);
        dummy.rotation.set(0, facingYaw, 0);
        dummy.scale.set(0.85, 1.04, bayWidth * 0.72);
        dummy.updateMatrix();
        concreteMats.push(dummy.matrix.clone());
        concreteColors.push(colTmp.setHex(0x52493f).clone());
      }

      // 2. Corrugated Roll-Down Steel Security Shutter (some jammed crooked!)
      const shutterH = Math.max(0.65, 4.1 - shutterOpenHeight);
      const jammedRoll = shutterOpenHeight > 0.4 && rng() < 0.55 ? (rng() - 0.5) * 0.14 : 0;
      const shutPos = localToWorld(-0.08, 4.15 - shutterH * 0.5, 0);
      dummy.position.copy(shutPos);
      dummy.rotation.set(jammedRoll, facingYaw, 0);
      dummy.scale.set(1.0, shutterH, bayWidth - 0.42);
      dummy.updateMatrix();
      shutterMats.push(dummy.matrix.clone());
      const shutTint =
        themeBranch === 'ELECTRO_LEFT'
          ? 0x5d6d7e
          : themeBranch === 'FOOD_RIGHT'
            ? 0x6e5c52
            : 0x636972;
      shutterColors.push(colTmp.setHex(shutTint).offsetHSL(0, 0, (rng() - 0.5) * 0.1).clone());

      // 3. Sloped Storefront Awning Canopy
      if (rng() < 0.78 || isCornerHero) {
        const awnPos = localToWorld(0.68, 4.28, 0);
        dummy.position.copy(awnPos);
        dummy.rotation.set(0, facingYaw, 0);
        dummy.scale.set(1.45, 1.0, bayWidth - 0.22);
        dummy.updateMatrix();
        awningMats.push(dummy.matrix.clone());
        const awnPalette =
          themeBranch === 'ELECTRO_LEFT'
            ? [0x1c4b66, 0x2b3654, 0x1e5752]
            : themeBranch === 'FOOD_RIGHT'
              ? [0x752626, 0x6e471b, 0x265437]
              : [0x2a4559, 0x692b32, 0x4f4325];
        awningColors.push(
          colTmp.setHex(awnPalette[Math.floor(rng() * awnPalette.length)]).clone()
        );
      }

      // 4. Exterior Rusted Wall-Mounted HVAC Condenser Unit
      if (rng() < 0.65) {
        const acPos = localToWorld(0.32, 5.35 + rng() * 0.7, (rng() - 0.5) * (bayWidth * 0.45));
        dummy.position.copy(acPos);
        dummy.rotation.set(0, facingYaw, 0);
        dummy.scale.set(1, 1, 1);
        dummy.updateMatrix();
        acMats.push(dummy.matrix.clone());
      }

      // 5. Projecting Neon Blade Sign OR Fascia Neon Signboard!
      if (rng() < 0.78 || isCornerHero) {
        let palIdx = Math.floor(rng() * 6);
        if (themeBranch === 'ELECTRO_LEFT') {
          palIdx = [0, 5, 3][Math.floor(rng() * 3)]; // Cyan, Violet, Jade
        } else if (themeBranch === 'FOOD_RIGHT') {
          palIdx = [1, 2, 4][Math.floor(rng() * 3)]; // Magenta, Amber, Crimson
        }

        const broken = rng() < 0.38;
        if (rng() < 0.58 || isCornerHero) {
          // Projecting perpendicular Blade Sign sticking out into the aisle so drivers read it down-corridor!
          const bladeOut = isCornerHero ? 1.85 : 1.45;
          const bPos = localToWorld(bladeOut, 4.95 + rng() * 0.85, (rng() - 0.5) * 1.2);
          addNeonSign(
            bPos.x,
            bPos.y,
            bPos.z,
            facingYaw + Math.PI * 0.5,
            isCornerHero ? 2.45 : 1.95,
            isCornerHero ? 1.15 : 0.95,
            palIdx,
            broken,
            false
          );
        } else {
          // Flush overhead Fascia Neon Signboard above the awning!
          const fPos = localToWorld(0.24, 5.15, 0);
          addNeonSign(fPos.x, fPos.y, fPos.z, facingYaw, bayWidth * 0.68, 0.95, palIdx, broken, false);
        }
      }
    };

    // Helper: Spawns an Abandoned Open-Air Hawker Stall + Smashable Market Crates!
    const addHawkerStall = (
      wx: number,
      wz: number,
      yaw: number,
      tarpHex: number,
      spawnCrates: boolean
    ) => {
      dummy.position.set(wx, 0, wz);
      dummy.rotation.set(0, yaw, 0);
      dummy.scale.set(1, 1, 1);
      dummy.updateMatrix();
      stallFrameMats.push(dummy.matrix.clone());

      // Slightly tilted tattered canopy
      dummy.position.set(wx, 2.52, wz);
      dummy.rotation.set((rng() - 0.5) * 0.08, yaw, (rng() - 0.5) * 0.11);
      dummy.scale.set(1, 1, 1);
      dummy.updateMatrix();
      stallCanopyMats.push(dummy.matrix.clone());
      stallCanopyColors.push(colTmp.setHex(tarpHex).clone());

      // Stall collision post (modest radius so vehicle bumps off solid iron stall counter)
      obstacles.push({
        id: nextObstacleId(),
        chunkIndex,
        kind: 'boulder',
        position: new THREE.Vector3(wx, 0, wz),
        radius: 1.05,
        height: 2.6,
        canShearVine: true,
      });

      if (spawnCrates) {
        const crateCnt = 2 + Math.floor(rng() * 2);
        for (let k = 0; k < crateCnt; k++) {
          const ang = yaw + (rng() - 0.5) * 2.2;
          const dist = 1.65 + rng() * 1.1;
          const cx = wx + Math.cos(ang) * dist;
          const cz = wz + Math.sin(ang) * dist;
          const cMesh = new THREE.Mesh(this.crateGeo, this.crateMat);
          cMesh.position.set(cx, 0.34, cz);
          cMesh.rotation.set(0, rng() * Math.PI * 2, 0);
          cMesh.castShadow = true;
          group.add(cMesh);
          crates.push({
            mesh: cMesh,
            pos: cMesh.position,
            vel: new THREE.Vector3(),
            rotVel: new THREE.Vector3(),
            baseY: 0.34,
            smashed: false,
            settleTimer: 0,
          });
        }
      }
    };

    // -------------------------------------------------------------------------
    // 2. OUTER CURVING SUBTERRANEAN WALL STOREFRONTS (u = 3 .. zLength - 3)
    //    Follows every curve, corner bend, and Y-junction flare seamlessly!
    // -------------------------------------------------------------------------
    const bayStep = 6.0;
    for (let u = 3.0; u <= zLength - 3.0; u += bayStep) {
      const z = zStart - u;
      const cx = sampleCourseX(z);
      const cYaw = tangentYawAt(z);
      const halfW = sampleUndergroundMarketHalfWidth(u, zLength);

      // Compute local wall slope d(halfW)/du so shops along the Y-flare rotate cleanly with the wall!
      const hwPrev = sampleUndergroundMarketHalfWidth(Math.max(0, u - 1.5), zLength);
      const hwNext = sampleUndergroundMarketHalfWidth(Math.min(zLength, u + 1.5), zLength);
      const flareAngle = Math.atan2(hwNext - hwPrev, 3.0);

      const cosC = Math.cos(cYaw);
      const sinC = Math.sin(cYaw);
      // Right unit vector perpendicular to course travel (-Z): (cosC, 0, -sinC)
      const rx = cosC;
      const rz = -sinC;

      const inSplit = u >= 32.0 && u <= 92.0;
      const isSharpBend = Math.abs(sampleCourseX(z - 4) - 2 * cx + sampleCourseX(z + 4)) > 0.28;

      // LEFT OUTER WALL SHOP (-halfW, facing +right inward into Left Arcade!)
      const lx = cx - rx * (halfW - 0.55);
      const lz = z - rz * (halfW - 0.55);
      addStorefrontBay(
        lx,
        lz,
        cYaw + flareAngle,
        bayStep - 0.15,
        1.7,
        inSplit ? 'ELECTRO_LEFT' : 'MIXED',
        isSharpBend
      );

      // RIGHT OUTER WALL SHOP (+halfW, facing -left inward into Right Wet-Market!)
      const rwx = cx + rx * (halfW - 0.55);
      const rwz = z + rz * (halfW - 0.55);
      addStorefrontBay(
        rwx,
        rwz,
        cYaw + Math.PI - flareAngle,
        bayStep - 0.15,
        1.7,
        inSplit ? 'FOOD_RIGHT' : 'MIXED',
        isSharpBend
      );
    }

    // -------------------------------------------------------------------------
    // 3. CENTRAL Y-JUNCTION SPLITTER ISLAND + 90-DEGREE MID-ISLAND CROSS ALLEY!
    //    - Front Chamfered Wedge Kiosk (u = 32.0m) with << ARCADE | MARKET >> Neon Split!
    //    - North Shop Island Block (u = 36.5 .. 58.5m, width = 10.6m)
    //    - 90-Degree Cross-Cut Connector Alley (u = 58.5 .. 66.5m, 8.0m wide!) with 4 Corner Shops!
    //    - South Shop Island Block (u = 66.5 .. 88.0m, width = 10.6m)
    //    - Rear Confluence Kiosk Nose (u = 91.5m)
    // -------------------------------------------------------------------------
    const islandHalfW = 5.3; // Leaves ~17.9m on EACH side (23.2 - 5.3 = 17.9m wide split aisles!)

    // 3A. HERO Y-JUNCTION PROW KIOSK (u = 32.0m) — First thing you see when the market splits!
    {
      const zProw = zStart - 32.0;
      const xProw = sampleCourseX(zProw);
      const yawProw = tangentYawAt(zProw);
      const cosP = Math.cos(yawProw);
      const sinP = Math.sin(yawProw);

      // Chamfered concrete kiosk core + roof column reaching the ceiling vault
      dummy.position.set(xProw, 4.1, zProw);
      dummy.rotation.set(0, yawProw + Math.PI * 0.25, 0);
      dummy.scale.set(4.8, 8.2, 4.8);
      dummy.updateMatrix();
      concreteMats.push(dummy.matrix.clone());
      concreteColors.push(colTmp.setHex(0x3b404b).clone());

      // Front-facing shuttered ticket/tea kiosk window facing oncoming players (+Z -> yaw + PI/2)
      addStorefrontBay(
        xProw + sinP * 2.5,
        zProw + cosP * 2.5,
        yawProw + Math.PI * 0.5,
        4.6,
        1.1,
        'MIXED',
        true
      );

      // GIANT OVERHEAD Y-JUNCTION DIRECTIONAL NEON SIGNS!
      // Left glowing Cyan Arrow Sign: "<< ARCADE"
      addNeonSign(
        xProw - cosP * 3.2 + sinP * 3.3,
        5.9,
        zProw + sinP * 3.2 + cosP * 3.3,
        yawProw + Math.PI * 0.5 + 0.24,
        3.9,
        1.48,
        6, // SPLIT_LEFT
        false,
        chunkIndex % 2 === 0 // Hero PointLight!
      );
      // Right glowing Hot-Magenta Arrow Sign: "MARKET >>"
      addNeonSign(
        xProw + cosP * 3.2 + sinP * 3.3,
        5.9,
        zProw - sinP * 3.2 + cosP * 3.3,
        yawProw + Math.PI * 0.5 - 0.24,
        3.9,
        1.48,
        7, // SPLIT_RIGHT
        false,
        chunkIndex % 2 !== 0 // Hero PointLight!
      );

      obstacles.push({
        id: nextObstacleId(),
        chunkIndex,
        kind: 'boulder',
        position: new THREE.Vector3(xProw, 0, zProw),
        radius: 3.15,
        height: 8.2,
        canShearVine: true,
      });
    }

    // 3B. NORTH & SOUTH SPLITTER ISLAND SHOP BLOCKS + WIDE 90-DEGREE CROSS-ALLEY CORNERS!
    const islandBlocks = [
      { u0: 35.5, u1: 55.5 }, // North Island Block
      { u0: 67.0, u1: 88.0 }, // South Island Block (11.5m wide 90-degree driftable cross-alley in between!)
    ];

    for (let bIdx = 0; bIdx < islandBlocks.length; bIdx++) {
      const blk = islandBlocks[bIdx];
      const uMid = (blk.u0 + blk.u1) * 0.5;
      const len = blk.u1 - blk.u0;
      const zMid = zStart - uMid;
      const xMid = sampleCourseX(zMid);
      const yawMid = tangentYawAt(zMid);

      // Full-height subterranean shop-island core reaching ceiling
      dummy.position.set(xMid, 4.2, zMid);
      dummy.rotation.set(0, yawMid, 0);
      dummy.scale.set(islandHalfW * 2.0 - 1.2, 8.4, len - 0.6);
      dummy.updateMatrix();
      concreteMats.push(dummy.matrix.clone());
      concreteColors.push(colTmp.setHex(0x2d323b).clone());

      // Solid physics obstacle cylinders along the island block (leaving the full 11.5m 90-deg cross-alley wide open!)
      for (let uObs = blk.u0 + 4.4; uObs <= blk.u1 - 4.4; uObs += 4.2) {
        const zo = zStart - uObs;
        obstacles.push({
          id: nextObstacleId(),
          chunkIndex,
          kind: 'boulder',
          position: new THREE.Vector3(sampleCourseX(zo), 0, zo),
          radius: 4.2,
          height: 8.2,
          canShearVine: true,
        });
      }

      // Line BOTH left & right faces of the island block with outward-facing storefronts!
      for (let uBay = blk.u0 + 2.8; uBay <= blk.u1 - 2.5; uBay += 5.4) {
        const zb = zStart - uBay;
        const xb = sampleCourseX(zb);
        const yb = tangentYawAt(zb);
        const cosB = Math.cos(yb);
        const sinB = Math.sin(yb);
        const isCorner =
          Math.abs(uBay - (blk.u0 + 2.8)) < 1.0 || Math.abs(uBay - (blk.u1 - 2.5)) < 1.0;

        // Left face of island (facing -X into Left Electronics Arcade -> yaw + PI)
        addStorefrontBay(
          xb - cosB * (islandHalfW - 0.35),
          zb + sinB * (islandHalfW - 0.35),
          yb + Math.PI,
          5.2,
          1.35,
          'ELECTRO_LEFT',
          isCorner
        );

        // Right face of island (facing +X into Right Wet-Market Alley -> yaw)
        addStorefrontBay(
          xb + cosB * (islandHalfW - 0.35),
          zb - sinB * (islandHalfW - 0.35),
          yb,
          5.2,
          1.35,
          'FOOD_RIGHT',
          isCorner
        );
      }

      // 90-DEGREE CROSS-ALLEY END-WALL STOREFRONTS (facing directly into the 90-degree cross passage!)
      const crossEdgeU = bIdx === 0 ? blk.u1 : blk.u0;
      const crossFacingSign = bIdx === 0 ? -1 : 1;
      const zCross = zStart - crossEdgeU;
      const xCross = sampleCourseX(zCross);
      const yCross = tangentYawAt(zCross);
      addStorefrontBay(
        xCross,
        zCross,
        yCross + (crossFacingSign > 0 ? Math.PI * 0.5 : -Math.PI * 0.5),
        8.0,
        1.2,
        bIdx === 0 ? 'ELECTRO_LEFT' : 'FOOD_RIGHT',
        true
      );
    }

    // Dedicated glowing Neon Archway Sign over the 90-degree Mid-Island Cross-Alley (u = 61.25m)!
    {
      const zAlley = zStart - 61.25;
      const xAlley = sampleCourseX(zAlley);
      const yAlley = tangentYawAt(zAlley);
      addNeonSign(
        xAlley,
        5.5,
        zAlley,
        yAlley,
        4.8,
        1.15,
        chunkIndex % 2 === 0 ? 1 : 0,
        true,
        true // Real glowing PointLight illuminating the 90-degree cross-alley!
      );
    }

    // 3C. REAR CONFLUENCE PILLAR KIOSK (u = 91.5m) where Left & Right aisles merge back!
    {
      const zTail = zStart - 91.5;
      const xTail = sampleCourseX(zTail);
      const yTail = tangentYawAt(zTail);
      dummy.position.set(xTail, 4.1, zTail);
      dummy.rotation.set(0, yTail + Math.PI * 0.25, 0);
      dummy.scale.set(3.9, 8.2, 3.9);
      dummy.updateMatrix();
      concreteMats.push(dummy.matrix.clone());
      concreteColors.push(colTmp.setHex(0x3a3f4a).clone());

      obstacles.push({
        id: nextObstacleId(),
        chunkIndex,
        kind: 'boulder',
        position: new THREE.Vector3(xTail, 0, zTail),
        radius: 2.55,
        height: 8.2,
        canShearVine: true,
      });
    }

    // -------------------------------------------------------------------------
    // 4. CEILING FLUORESCENT BALLAST TRACKS + OVERHEAD HVAC DUCTS + PILLARS
    //    Literally splits into a Y-branch overhead along both aisles!
    // -------------------------------------------------------------------------
    for (let u = 4.0; u <= zLength - 4.0; u += 7.5) {
      if (chunkIndex === 0 && u < 20.0) continue;

      const z = zStart - u;
      const cx = sampleCourseX(z);
      const yaw = tangentYawAt(z);
      const cosY = Math.cos(yaw);
      const sinY = Math.sin(yaw);
      const halfW = sampleUndergroundMarketHalfWidth(u, zLength);

      if (u < 28.0 || u > 93.0) {
        // Single-aisle vaulted concourse: overhead central fluorescent tube + side pillars
        dummy.position.set(cx, 8.45, z);
        dummy.rotation.set(0, yaw, 0);
        dummy.scale.set(0.35, 0.14, 4.2);
        dummy.updateMatrix();
        fluorFixtureMats.push(dummy.matrix.clone());

        for (const sSide of [-1, 1]) {
          dummy.position.set(
            cx + cosY * sSide * (halfW - 2.2),
            7.45,
            z - sinY * sSide * (halfW - 2.2)
          );
          dummy.rotation.set(0, yaw, 0);
          dummy.scale.set(1, 1, 7.6);
          dummy.updateMatrix();
          ductMats.push(dummy.matrix.clone());
        }

        if (Math.abs(u - 11.5) < 4.0 || Math.abs(u - 103.0) < 4.0) {
          const pSide = rng() < 0.5 ? -1 : 1;
          const px = cx + cosY * pSide * 6.8;
          const pz = z - sinY * pSide * 6.8;
          dummy.position.set(px, 4.2, pz);
          dummy.rotation.set(0, yaw, 0);
          dummy.scale.set(1, 1, 1);
          dummy.updateMatrix();
          pillarMats.push(dummy.matrix.clone());
          pillarColors.push(colTmp.setHex(0x6b6f78).clone());
          obstacles.push({
            id: nextObstacleId(),
            chunkIndex,
            kind: 'boulder',
            position: new THREE.Vector3(px, 0, pz),
            radius: 0.72,
            height: 8.4,
            canShearVine: true,
          });
        }
      } else {
        // Y-SPLIT DUAL AISLES: Twin ceiling fluorescent tracks + HVAC ducts down BOTH Left & Right branches!
        const aisleCenterOffset = (halfW + islandHalfW) * 0.5;
        for (const branchSign of [-1, 1]) {
          const bx = cx + cosY * branchSign * aisleCenterOffset;
          const bz = z - sinY * branchSign * aisleCenterOffset;

          dummy.position.set(bx, 8.45, bz);
          dummy.rotation.set(0, yaw, 0);
          dummy.scale.set(0.35, 0.14, 4.2);
          dummy.updateMatrix();
          fluorFixtureMats.push(dummy.matrix.clone());

          dummy.position.set(
            bx + cosY * branchSign * 4.2,
            7.45,
            bz - sinY * branchSign * 4.2
          );
          dummy.rotation.set(0, yaw, 0);
          dummy.scale.set(1, 1, 7.6);
          dummy.updateMatrix();
          ductMats.push(dummy.matrix.clone());
        }
      }
    }

    // Add 3 more Hero Neon PointLights (Left Arcade, Right Food Alley, and Corner Concourse!)
    {
      const zLeftHero = zStart - 48.0;
      const cxL = sampleCourseX(zLeftHero);
      addNeonSign(cxL - 14.2, 5.7, zLeftHero, tangentYawAt(zLeftHero), 3.4, 1.2, 0, true, true);

      const zRightHero = zStart - 78.0;
      const cxR = sampleCourseX(zRightHero);
      addNeonSign(cxR + 14.2, 5.7, zRightHero, tangentYawAt(zRightHero), 3.4, 1.2, 2, false, true);

      const zBendHero = zStart - 13.5;
      const cxB = sampleCourseX(zBendHero);
      addNeonSign(cxB, 6.2, zBendHero, tangentYawAt(zBendHero) + Math.PI * 0.5, 3.6, 1.15, 5, true, true);
    }

    // -------------------------------------------------------------------------
    // 5. ABANDONED OPEN-AIR HAWKER STALLS & SMASHABLE PRODUCE CRATES IN AISLES!
    // -------------------------------------------------------------------------
    const tarpPalette = [0x6e2b2b, 0x2b5447, 0x6b5326, 0x2b3f63, 0x5c344f];
    for (const uStall of [10.0, 20.0, 99.0]) {
      if (chunkIndex === 0 && uStall < 22.0) continue;
      const zs = zStart - uStall;
      const cxs = sampleCourseX(zs);
      const ys = tangentYawAt(zs);
      const side = rng() < 0.5 ? -1 : 1;
      const off = side * lerp(4.8, 7.8, rng());
      addHawkerStall(
        cxs + Math.cos(ys) * off,
        zs - Math.sin(ys) * off,
        ys + (rng() - 0.5) * 0.25,
        tarpPalette[Math.floor(rng() * tarpPalette.length)],
        true
      );
    }

    // Split-aisle hawker stalls in Left Electronics Arcade & Right Wet-Market Alley!
    for (let uStall = 40.0; uStall <= 84.0; uStall += 11.0) {
      // Keep the mouths of the 90-degree mid-island cross-alley (u = 55.5 .. 67.0m) completely clear!
      if (uStall > 52.5 && uStall < 70.0) continue;
      const zs = zStart - uStall;
      const cxs = sampleCourseX(zs);
      const ys = tangentYawAt(zs);
      const cosS = Math.cos(ys);
      const sinS = Math.sin(ys);

      const lOff = -(rng() < 0.5 ? 8.3 : 19.2);
      addHawkerStall(
        cxs + cosS * lOff,
        zs - sinS * lOff,
        ys + (rng() - 0.5) * 0.22,
        tarpPalette[Math.floor(rng() * tarpPalette.length)],
        true
      );

      const rOff = rng() < 0.5 ? 8.3 : 19.2;
      addHawkerStall(
        cxs + cosS * rOff,
        zs - sinS * rOff,
        ys + Math.PI + (rng() - 0.5) * 0.22,
        tarpPalette[Math.floor(rng() * tarpPalette.length)],
        true
      );
    }

    // Extra loose smashable crates scattered across the 90-degree cross-alley & Y-merge!
    for (let c = 0; c < 10; c++) {
      const uCrate = lerp(28.0, 94.0, rng());
      const zc = zStart - uCrate;
      const cxc = sampleCourseX(zc);
      const offX = (rng() - 0.5) * 28.0;
      if (
        Math.abs(offX) < islandHalfW + 0.8 &&
        ((uCrate > 35.5 && uCrate < 59.0) || (uCrate > 66.0 && uCrate < 89.0))
      ) {
        continue;
      }
      const cMesh = new THREE.Mesh(this.crateGeo, this.crateMat);
      cMesh.position.set(cxc + offX, 0.34, zc);
      cMesh.rotation.set(0, rng() * Math.PI * 2, 0);
      group.add(cMesh);
      crates.push({
        mesh: cMesh,
        pos: cMesh.position,
        vel: new THREE.Vector3(),
        rotVel: new THREE.Vector3(),
        baseY: 0.34,
        smashed: false,
        settleTimer: 0,
      });
    }

    // -------------------------------------------------------------------------
    // 6. EMIT BATCHED INSTANCED MESHES FOR ULTRA-FAST GPU RENDERING
    // -------------------------------------------------------------------------
    const addInstanced = (
      geo: THREE.BufferGeometry,
      mat: THREE.Material,
      matrices: THREE.Matrix4[],
      colors?: THREE.Color[],
      castShadow = false
    ) => {
      if (matrices.length === 0) return;
      const im = new THREE.InstancedMesh(geo, mat, matrices.length);
      for (let i = 0; i < matrices.length; i++) {
        im.setMatrixAt(i, matrices[i]);
        if (colors && colors[i]) {
          im.setColorAt(i, colors[i]);
        }
      }
      im.instanceMatrix.needsUpdate = true;
      if (im.instanceColor) im.instanceColor.needsUpdate = true;
      im.castShadow = castShadow;
      im.receiveShadow = true;
      void tmpMat;
      group.add(im);
    };

    addInstanced(this.boxGeo, this.shopConcreteMat, concreteMats, concreteColors, true);
    addInstanced(this.shutterGeo, this.shutterSteelMat, shutterMats, shutterColors, true);
    addInstanced(this.awningGeo, this.stallTarpMat, awningMats, awningColors, true);
    addInstanced(this.acUnitGeo, this.rustedMetalMat, acMats);
    addInstanced(this.boxGeo, this.interiorDimMat, interiorMats);
    addInstanced(this.pillarGeo, this.shopConcreteMat, pillarMats, pillarColors, true);
    addInstanced(this.ductGeo, this.rustedMetalMat, ductMats);
    addInstanced(this.stallFrameGeo, this.rustedMetalMat, stallFrameMats, undefined, true);
    addInstanced(this.stallCanopyGeo, this.stallTarpMat, stallCanopyMats, stallCanopyColors, true);
    addInstanced(this.signPanelGeo, this.signFrameMat, signFrameMats);
    addInstanced(this.boxGeo, this.fluorescentLitMat, fluorFixtureMats);

    return {
      group,
      flickerNodes,
      crates,
      zStart,
      zLength,
    };
  }

  /**
   * Builds the continuous enclosed subterranean concrete ceiling vault, tiled outer walls,
   * and wet reflective dark market tile floor following all S-curves and Y-junction splits!
   */
  private buildSubterraneanConcourseShell(
    group: THREE.Group,
    zStart: number,
    zLength: number,
    sampleCourseX: (z: number) => number
  ): void {
    const zSegs = 60; // Every 1.5m along 90m chunk -> silky-smooth curves & Y-junction flares!
    // Cross-section profile (7 points around outer walls + vaulted underground roof):
    // 0: Left floor base (-1.0 * halfW, y = -0.2)
    // 1: Left lower subway tile wall (-1.0 * halfW, y = 3.6)
    // 2: Left upper concrete wall (-1.0 * halfW, y = 7.7)
    // 3: Left coved ceiling haunch (-0.72 * halfW, y = 8.75)
    // 4: Central ceiling vault crown (0.0, y = 9.15)
    // 5: Right coved ceiling haunch (+0.72 * halfW, y = 8.75)
    // 6: Right upper concrete wall (+1.0 * halfW, y = 7.7)
    // 7: Right lower subway tile wall (+1.0 * halfW, y = 3.6)
    // 8: Right floor base (+1.0 * halfW, y = -0.2)
    const profX = [-1.0, -1.0, -1.0, -0.72, 0.0, 0.72, 1.0, 1.0, 1.0];
    const profY = [-0.2, 3.6, 7.7, 8.75, 9.15, 8.75, 7.7, 3.6, -0.2];
    const profCols = [
      0x22262e, // Damp baseboard
      0x4e5859, // Grime-stained subway tile wall
      0x343942, // Concrete utility plenum
      0x232730, // Dark coved concrete ceiling haunch
      0x1d2028, // Vaulted subterranean ceiling crown
      0x232730,
      0x343942,
      0x4e5859,
      0x22262e,
    ];

    const pCount = profX.length;
    const rows = zSegs + 1;
    const vPos = new Float32Array(rows * pCount * 3);
    const vCol = new Float32Array(rows * pCount * 3);
    const vUv = new Float32Array(rows * pCount * 2);
    const vIdx: number[] = [];

    // Also build the continuous Wet Subterranean Tile Floor Ribbon (y = +0.028m)
    const fCols = 7;
    const fPos = new Float32Array(rows * fCols * 3);
    const fCol = new Float32Array(rows * fCols * 3);
    const fUv = new Float32Array(rows * fCols * 2);
    const fIdx: number[] = [];

    const tmpC = new THREE.Color();

    for (let r = 0; r < rows; r++) {
      const u = (r / zSegs) * zLength;
      const z = zStart - u;
      const cx = sampleCourseX(z);
      const dx = sampleCourseX(z - 1.0) - sampleCourseX(z + 1.0);
      const yaw = Math.atan2(-dx, 2.0);
      const rx = Math.cos(yaw);
      const rz = -Math.sin(yaw);
      const halfW = sampleUndergroundMarketHalfWidth(u, zLength) + 1.15;

      // Subtle transverse structural rib darkening every 6m
      const beamBand = Math.cos((u / 6.0) * Math.PI * 2) > 0.78 ? 0.76 : 1.0;

      for (let c = 0; c < pCount; c++) {
        const idx = r * pCount + c;
        const off = profX[c] * halfW;
        vPos[idx * 3] = cx + rx * off;
        vPos[idx * 3 + 1] = profY[c];
        vPos[idx * 3 + 2] = z + rz * off;

        vUv[idx * 2] = c * 1.6;
        vUv[idx * 2 + 1] = u * 0.22;

        tmpC.setHex(profCols[c]).multiplyScalar(beamBand);
        vCol[idx * 3] = tmpC.r;
        vCol[idx * 3 + 1] = tmpC.g;
        vCol[idx * 3 + 2] = tmpC.b;
      }

      // Wet subterranean tile floor vertices across [-halfW, +halfW]
      for (let fc = 0; fc < fCols; fc++) {
        const frac = (fc / (fCols - 1)) * 2.0 - 1.0; // -1 .. +1
        const off = frac * halfW;
        const wx = cx + rx * off;
        const wz = z + rz * off;
        const fVert = r * fCols + fc;

        fPos[fVert * 3] = wx;
        fPos[fVert * 3 + 1] = 0.026;
        fPos[fVert * 3 + 2] = wz;

        fUv[fVert * 2] = (frac * halfW) * 0.35;
        fUv[fVert * 2 + 1] = u * 0.35;

        // Checkerboard concourse tiles + wet puddle darkening patches
        const puddle = smoothstep(0.35, 0.75, fbm2D(wx * 0.14, wz * 0.14, 2) * 0.5 + 0.5);
        const gutter = Math.abs(Math.abs(frac) - 0.58) < 0.08 ? 0.72 : 1.0;
        const baseLum = lerp(0.24, 0.13, puddle) * gutter;
        fCol[fVert * 3] = baseLum * 0.92;
        fCol[fVert * 3 + 1] = baseLum * 0.98;
        fCol[fVert * 3 + 2] = baseLum * 1.12;
      }
    }

    for (let r = 0; r < zSegs; r++) {
      for (let c = 0; c < pCount - 1; c++) {
        const a = r * pCount + c;
        const b = a + 1;
        const d = a + pCount;
        const e = d + 1;
        // Point normals INWARD into the subterranean market concourse (+X on left wall, -Y on ceiling, -X on right wall)!
        vIdx.push(a, e, b, a, d, e);
      }
      for (let fc = 0; fc < fCols - 1; fc++) {
        const a = r * fCols + fc;
        const b = a + 1;
        const d = a + fCols;
        const e = d + 1;
        // True +Y upward floor normal: (b - a) is +X, (d - a) is -Z -> (+X) x (-Z) = +Y!
        fIdx.push(a, b, e, a, e, d);
      }
    }

    const vaultGeo = new THREE.BufferGeometry();
    vaultGeo.setAttribute('position', new THREE.BufferAttribute(vPos, 3));
    vaultGeo.setAttribute('color', new THREE.BufferAttribute(vCol, 3));
    vaultGeo.setAttribute('uv', new THREE.BufferAttribute(vUv, 2));
    vaultGeo.setIndex(vIdx);
    vaultGeo.computeVertexNormals();

    const vaultMesh = new THREE.Mesh(vaultGeo, this.shellVaultMat);
    vaultMesh.receiveShadow = true;
    vaultMesh.userData.uniqueGeo = true;
    group.add(vaultMesh);

    const floorGeo = new THREE.BufferGeometry();
    floorGeo.setAttribute('position', new THREE.BufferAttribute(fPos, 3));
    floorGeo.setAttribute('color', new THREE.BufferAttribute(fCol, 3));
    floorGeo.setAttribute('uv', new THREE.BufferAttribute(fUv, 2));
    floorGeo.setIndex(fIdx);
    floorGeo.computeVertexNormals();

    const floorMesh = new THREE.Mesh(floorGeo, this.wetTileFloorMat);
    floorMesh.receiveShadow = true;
    floorMesh.userData.uniqueGeo = true;
    group.add(floorMesh);
  }

  /**
   * Animates flickering broken neon signs, wet-floor puddle reflections, PointLight distance culling,
   * and smashable abandoned market crates!
   */
  public stepChunkHazards(
    data: UndergroundMarketChunkData,
    dt: number,
    elapsed: number,
    cameraPos: THREE.Vector3,
    vehiclePos: THREE.Vector3,
    vehicleSpeed: number,
    passiveProtection: boolean,
    vehicleVel?: THREE.Vector3
  ): void {
    const clampedDt = Math.min(dt, 0.05);

    // 1. Flickering Neon Signs & Broken Ballasts
    for (let i = 0; i < data.flickerNodes.length; i++) {
      const node = data.flickerNodes[i];
      const distCamSq = node.worldPos.distanceToSquared(cameraPos);

      let flicker = 1.0;
      if (node.brokenBallast) {
        const wave =
          Math.sin(elapsed * 23.0 + node.flickerSeed * 5.0) *
          Math.cos(elapsed * 41.0 + node.flickerSeed * 11.0);
        const dropOut = Math.sin(elapsed * 3.1 + node.flickerSeed) > 0.72;
        if (dropOut && wave < 0.15) {
          flicker = 0.08;
        } else if (wave < -0.55) {
          flicker = 0.35;
        }

        node.sparkCooldown -= clampedDt;
        if (node.sparkCooldown <= 0 && flicker < 0.2 && distCamSq < 75 * 75) {
          node.sparkCooldown = 3.2 + (node.flickerSeed % 3.5);
          if (this.onNeonSparkZap) {
            this.onNeonSparkZap(node.worldPos);
          }
        }
      } else {
        flicker = 0.92 + 0.08 * Math.sin(elapsed * 6.0 + node.flickerSeed);
      }

      node.bulbMesh.visible = flicker > 0.12;
      const pMat = node.puddleMesh.material as THREE.MeshBasicMaterial;
      pMat.opacity = 0.34 * flicker;

      if (node.light) {
        if (distCamSq < 105 * 105) {
          node.light.visible = true;
          node.light.intensity = node.baseIntensity * flicker;
        } else {
          node.light.visible = false;
        }
      }
    }

    // 2. Smashable Abandoned Market Crates
    for (let i = 0; i < data.crates.length; i++) {
      const crate = data.crates[i];

      if (!passiveProtection && vehicleSpeed > 1.2) {
        const dx = crate.pos.x - vehiclePos.x;
        const dz = crate.pos.z - vehiclePos.z;
        const dSq = dx * dx + dz * dz;
        if (dSq < 2.25 * 2.25 && crate.pos.y < vehiclePos.y + 1.5) {
          const d = Math.max(0.25, Math.sqrt(dSq));
          const pushSpeed = Math.max(8.5, vehicleSpeed * 1.15);
          crate.vel.set(
            (dx / d) * pushSpeed + (vehicleVel ? vehicleVel.x * 0.35 : 0),
            3.6 + Math.min(4.5, vehicleSpeed * 0.18),
            (dz / d) * pushSpeed + (vehicleVel ? vehicleVel.z * 0.35 : 0)
          );
          crate.rotVel.set(
            (Math.random() - 0.5) * 14.0,
            (Math.random() - 0.5) * 14.0,
            (Math.random() - 0.5) * 14.0
          );
          if (!crate.smashed) {
            crate.smashed = true;
            if (this.onCrateSmash) {
              this.onCrateSmash(crate.pos.clone());
            }
          }
          crate.settleTimer = 2.6;
        }
      }

      if (crate.settleTimer > 0) {
        crate.settleTimer -= clampedDt;
        crate.vel.y -= 18.5 * clampedDt;
        crate.pos.addScaledVector(crate.vel, clampedDt);
        crate.mesh.rotation.x += crate.rotVel.x * clampedDt;
        crate.mesh.rotation.y += crate.rotVel.y * clampedDt;
        crate.mesh.rotation.z += crate.rotVel.z * clampedDt;

        if (crate.pos.y < crate.baseY) {
          crate.pos.y = crate.baseY;
          crate.vel.y = -crate.vel.y * 0.32;
          crate.vel.x *= 0.72;
          crate.vel.z *= 0.72;
          crate.rotVel.multiplyScalar(0.68);
        }
      }
    }
  }

  public stepGlobalMarketAtmosphere(dt: number, focusPos: THREE.Vector3, active: boolean): void {
    this.atmosphereFxGroup.visible = active;
    if (!active) return;

    const clampedDt = Math.min(dt, 0.05);
    const posAttr = this.dustPoints.geometry.attributes.position as THREE.BufferAttribute;

    for (let i = 0; i < this.dustCount; i++) {
      const ix = i * 3;
      let x = this.dustPositions[ix] + this.dustVelocities[ix] * clampedDt;
      let y = this.dustPositions[ix + 1] + this.dustVelocities[ix + 1] * clampedDt;
      let z = this.dustPositions[ix + 2] + this.dustVelocities[ix + 2] * clampedDt;

      if (y < 0.25) y = 8.0;
      if (y > 8.2) y = 0.3;
      if (x - focusPos.x > 32) x -= 64;
      if (x - focusPos.x < -32) x += 64;
      if (z - focusPos.z > 60) z -= 120;
      if (z - focusPos.z < -60) z += 120;

      this.dustPositions[ix] = x;
      this.dustPositions[ix + 1] = y;
      this.dustPositions[ix + 2] = z;
    }
    posAttr.needsUpdate = true;
  }

  public disposeChunk(data: UndergroundMarketChunkData): void {
    data.group.traverse((obj) => {
      if ((obj as THREE.Mesh).isMesh) {
        const mesh = obj as THREE.Mesh;
        if (mesh.userData.uniqueGeo) {
          mesh.geometry.dispose();
        }
      }
    });
  }
}

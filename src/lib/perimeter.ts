import * as THREE from 'three';
import { RoofParams } from './types';
import { getMaterials } from './materials';
import type { Check, PartInfo } from './buildRoof';

export interface PerimeterContext {
  L: number;
  S: number;
  wallTop: number;
  entryFrontZ: number;
  stairWidth: number;
}

export interface PerimeterResult {
  group: THREE.Group;
  part: PartInfo | null;
  checks: Check[];
  wallLength: number;
}

// ---------------- Helpers ----------------
function wbox(
  w: number,
  h: number,
  d: number,
  mat: THREE.Material | THREE.Material[],
  x = 0,
  y = 0,
  z = 0,
  rx = 0,
  ry = 0,
  rz = 0,
): THREE.Mesh {
  const mesh = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), mat);
  mesh.position.set(x, y, z);
  mesh.rotation.set(rx, ry, rz);
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  return mesh;
}

function wcyl(
  rt: number,
  rb: number,
  h: number,
  seg: number,
  mat: THREE.Material,
  x = 0,
  y = 0,
  z = 0,
  rx = 0,
  ry = 0,
  rz = 0,
): THREE.Mesh {
  const mesh = new THREE.Mesh(new THREE.CylinderGeometry(rt, rb, h, seg), mat);
  mesh.position.set(x, y, z);
  mesh.rotation.set(rx, ry, rz);
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  return mesh;
}

// ---------------- Procedural Relief & Tile Textures ----------------
const perimTexCache = new Map<string, THREE.CanvasTexture>();

function getPerimTex(id: string, w: number, h: number, draw: (ctx: CanvasRenderingContext2D) => void): THREE.CanvasTexture {
  const hit = perimTexCache.get(id);
  if (hit) return hit;
  const canvas = document.createElement('canvas');
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext('2d')!;
  draw(ctx);
  const tex = new THREE.CanvasTexture(canvas);
  tex.wrapS = THREE.RepeatWrapping;
  tex.wrapT = THREE.ClampToEdgeWrapping;
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.userData.shared = true;
  perimTexCache.set(id, tex);
  return tex;
}

/** Bas-relief cloud scroll carving on stone socle (须弥座 / 卷草纹) */
function getSocleReliefTex(): THREE.CanvasTexture {
  return getPerimTex('socle_scroll_relief', 512, 128, (ctx) => {
    // Warm limestone / grey stone base
    ctx.fillStyle = '#9e998e';
    ctx.fillRect(0, 0, 512, 128);

    // Stone grain
    ctx.fillStyle = 'rgba(60, 55, 48, 0.18)';
    for (let i = 0; i < 400; i++) {
      const x = Math.random() * 512;
      const y = Math.random() * 128;
      ctx.fillRect(x, y, 2, 2);
    }

    // Carved classical Chinese cloud/spiral ruyi scrolls (如意卷草纹)
    ctx.strokeStyle = '#625d54';
    ctx.lineWidth = 6;
    ctx.fillStyle = 'rgba(80, 75, 68, 0.25)';

    for (let x = 64; x < 512; x += 128) {
      // Intricate scroll relief
      ctx.beginPath();
      ctx.arc(x, 64, 32, 0.2 * Math.PI, 1.8 * Math.PI);
      ctx.arc(x + 12, 64, 18, 1.8 * Math.PI, 0.2 * Math.PI, true);
      ctx.stroke();

      // Inner spiral eye
      ctx.beginPath();
      ctx.arc(x - 6, 64, 8, 0, Math.PI * 2);
      ctx.fill();

      // Top/bottom border moulding lines
      ctx.strokeStyle = '#4e4942';
      ctx.lineWidth = 3;
      ctx.beginPath();
      ctx.moveTo(0, 10);
      ctx.lineTo(512, 10);
      ctx.moveTo(0, 118);
      ctx.lineTo(512, 118);
      ctx.stroke();
    }
  });
}

/** Tiled wall cap texture (miniature hongawara pantiles for wall ridge) */
function getWallTileTex(colorHex = '#2f5255'): THREE.CanvasTexture {
  return getPerimTex(`wall_tile_${colorHex}`, 256, 256, (ctx) => {
    ctx.fillStyle = colorHex;
    ctx.fillRect(0, 0, 256, 256);

    // Half-round cylindrical tile courses (筒瓦 ridges)
    for (let x = 0; x < 256; x += 32) {
      // Highlight on crown
      const grad = ctx.createLinearGradient(x, 0, x + 32, 0);
      grad.addColorStop(0, 'rgba(0,0,0,0.35)');
      grad.addColorStop(0.3, 'rgba(255,255,255,0.22)');
      grad.addColorStop(0.7, 'rgba(255,255,255,0.08)');
      grad.addColorStop(1, 'rgba(0,0,0,0.45)');
      ctx.fillStyle = grad;
      ctx.fillRect(x, 0, 32, 256);
    }

    // Horizontal tile course overlap seams (压六露四)
    ctx.fillStyle = 'rgba(0,0,0,0.4)';
    for (let y = 0; y < 256; y += 48) {
      ctx.fillRect(0, y, 256, 4);
    }
  });
}

// ---------------- Wall Segment Builder ----------------

/**
 * Builds a single straight section of tile-capped Chinese courtyard wall.
 * Length along X, thickness along Z, height along Y.
 */
function buildWallSection(
  length: number,
  height: number,
  thickness: number,
  mats: ReturnType<typeof getMaterials>,
  pillarColor: string,
  tileColor: string,
  opts: {
    startPost?: boolean;
    endPost?: boolean;
    hasTileRoof?: boolean;
    cornerSwept?: boolean;
  } = {},
): THREE.Group {
  const g = new THREE.Group();

  const { startPost = true, endPost = true, hasTileRoof = true, cornerSwept = false } = opts;

  const socleH = 0.52; // Lower stone socle base (须弥座)
  const bodyH = height - socleH - (hasTileRoof ? 0.38 : 0.05);

  const socleTex = getSocleReliefTex();
  const socleMat = new THREE.MeshStandardMaterial({
    map: socleTex,
    roughness: 0.85,
    bumpMap: socleTex,
    bumpScale: 0.03,
  });

  const wallPlaster = mats.plaster;

  const redPillarMat = new THREE.MeshStandardMaterial({
    color: pillarColor,
    roughness: 0.55,
  });

  const goldRingMat = new THREE.MeshStandardMaterial({
    color: '#d4af37',
    metalness: 0.7,
    roughness: 0.35,
  });

  const tileTex = getWallTileTex(tileColor);
  const wallTileMat = new THREE.MeshStandardMaterial({
    map: tileTex,
    color: tileColor,
    roughness: 0.6,
  });

  // 1. Lower Carved Stone Socle (Base with relief scrolls)
  const socle = wbox(length, socleH, thickness * 1.14, socleMat, 0, socleH / 2, 0);
  g.add(socle);
  // Socle top step moulding
  g.add(wbox(length + 0.04, 0.05, thickness * 1.22, mats.stone, 0, socleH + 0.025, 0));

  // 2. White Stucco Wall Body (粉墙 / 墙身)
  const wallBody = wbox(length, bodyH, thickness, wallPlaster, 0, socleH + 0.05 + bodyH / 2, 0);
  g.add(wallBody);

  // 3. Red Timber Intermediate Rail / Frieze under eaves (额枋 / 阑额)
  const friezeH = 0.09;
  const friezeY = socleH + 0.05 + bodyH + friezeH / 2;
  g.add(wbox(length, friezeH, thickness * 1.08, redPillarMat, 0, friezeY, 0));

  // Small dentil bracket blocks along the frieze
  const dentilSpacing = 0.45;
  const numDentils = Math.max(1, Math.floor(length / dentilSpacing));
  for (let i = 0; i <= numDentils; i++) {
    const dx = -length / 2 + (i * length) / numDentils;
    g.add(wbox(0.06, 0.05, thickness * 1.18, redPillarMat, dx, friezeY, 0));
  }

  // 4. Cylindrical Lacquered Columns at post intervals (柱子)
  const postR = thickness * 0.55;
  const postH = height - (hasTileRoof ? 0.35 : 0.02);

  const postsX: number[] = [];
  if (startPost) postsX.push(-length / 2);
  if (endPost) postsX.push(length / 2);

  for (const px of postsX) {
    // Left & right double twin column effect as in the reference photo
    for (const sz of [-1, 1]) {
      const pz = sz * (thickness * 0.28);
      // Main pillar
      g.add(wcyl(postR * 0.85, postR, postH, 16, redPillarMat, px, postH / 2, pz));
      // Base stone footing
      g.add(wcyl(postR * 1.15, postR * 1.25, 0.12, 16, mats.stone, px, 0.06, pz));
      // Gold decorative rings at base and collar
      g.add(wcyl(postR * 0.95, postR * 0.95, 0.03, 16, goldRingMat, px, 0.18, pz));
      g.add(wcyl(postR * 0.86, postR * 0.86, 0.03, 16, goldRingMat, px, postH - 0.15, pz));
    }
  }

  // 5. Miniature Gabled Tile Roof (瓦顶 / 院墙瓦顶)
  if (hasTileRoof) {
    const roofY = height - 0.28;
    const roofSpanD = thickness * 2.8;
    const roofSlopeLen = roofSpanD * 0.58;
    const roofPitch = 0.42; // ~23 degree pitch

    const eavesOverhang = 0.18;
    const roofLen = length + eavesOverhang * 2;

    // Front slope (+Z)
    const fSlope = wbox(roofLen, 0.032, roofSlopeLen, wallTileMat, 0, roofY + 0.06, roofSpanD * 0.24, roofPitch, 0, 0);
    g.add(fSlope);

    // Back slope (-Z)
    const bSlope = wbox(roofLen, 0.032, roofSlopeLen, wallTileMat, 0, roofY + 0.06, -roofSpanD * 0.24, -roofPitch, 0, 0);
    g.add(bSlope);

    // Ridge cap tiles (正脊)
    g.add(wbox(roofLen + 0.08, 0.07, 0.09, mats.ridge, 0, roofY + 0.14, 0));
    // Ridge roll cylinder
    g.add(wcyl(0.035, 0.035, roofLen + 0.08, 12, mats.ridge, 0, roofY + 0.18, 0, 0, 0, Math.PI / 2));

    // Curved upturned ridge ends (chiwen / swallow tail tips) at roof ends
    if (cornerSwept) {
      for (const sx of [-1, 1]) {
        const tipX = sx * (roofLen / 2 + 0.05);
        const tip = wbox(0.14, 0.16, 0.08, mats.ridge, tipX, roofY + 0.24, 0, 0, 0, -sx * 0.45);
        g.add(tip);
      }
    }
  }

  return g;
}

/**
 * Builds a 90-degree corner module (corner enclosure with corner hip ridge & swept corners).
 */
export function buildCornerModule(
  size: number,
  height: number,
  thickness: number,
  mats: ReturnType<typeof getMaterials>,
  pillarColor: string,
  tileColor: string,
): THREE.Group {
  const g = new THREE.Group();

  // Segment along X from 0 to size
  const segX = buildWallSection(size, height, thickness, mats, pillarColor, tileColor, {
    startPost: false,
    endPost: true,
    hasTileRoof: true,
    cornerSwept: false,
  });
  segX.position.set(size / 2, 0, 0);
  g.add(segX);

  // Segment along Z from 0 to size
  const segZ = buildWallSection(size, height, thickness, mats, pillarColor, tileColor, {
    startPost: false,
    endPost: true,
    hasTileRoof: true,
    cornerSwept: false,
  });
  segZ.rotation.y = Math.PI / 2;
  segZ.position.set(0, 0, size / 2);
  g.add(segZ);

  // Big sturdy corner pillar at vertex (0, 0, 0)
  const redPillarMat = new THREE.MeshStandardMaterial({ color: pillarColor, roughness: 0.55 });
  const goldRingMat = new THREE.MeshStandardMaterial({ color: '#d4af37', metalness: 0.7, roughness: 0.35 });
  const postR = thickness * 0.7;
  const postH = height - 0.3;

  g.add(wcyl(postR * 0.9, postR, postH, 16, redPillarMat, 0, postH / 2, 0));
  g.add(wcyl(postR * 1.3, postR * 1.4, 0.14, 16, mats.stone, 0, 0.07, 0));
  g.add(wcyl(postR * 1.05, postR * 1.05, 0.035, 16, goldRingMat, 0, 0.22, 0));
  g.add(wcyl(postR * 0.95, postR * 0.95, 0.035, 16, goldRingMat, 0, postH - 0.15, 0));

  // Corner hip-cap tile intersection at apex
  const roofY = height - 0.28;
  const cornerFinial = wbox(0.18, 0.25, 0.18, mats.ridge, 0, roofY + 0.22, 0);
  cornerFinial.rotation.y = Math.PI / 4;
  g.add(cornerFinial);

  return g;
}

// ---------------- Main Perimeter Generator ----------------

export function buildPerimeter(p: RoofParams, ctx: PerimeterContext): PerimeterResult {
  const group = new THREE.Group();
  const checks: Check[] = [];

  if (!p.wallEnclosureEnabled || p.wallEnclosureMode === 'none') {
    return { group, part: null, checks, wallLength: 0 };
  }

  const mats = getMaterials();
  const L = ctx.L;
  const S = ctx.S;
  const stairW = ctx.stairWidth;
  const frontZ = ctx.entryFrontZ;

  const wallH = p.wallHeight || 2.4;
  const wallThick = 0.24;
  const pillarColor = p.wallPillarColor || '#8a2b22'; // Imperial vermilion / terracotta red
  const tileColor = p.wallTileColor || '#2f5255'; // Classic turquoise / celadon jade glazed roof tile

  let totalWallLen = 0;

  // 1. Courtyard Wall Wings extending left & right along front facade
  const wingGap = stairW + 1.8; // Open courtyard entrance opening in front
  const wingLen = Math.max(3.2, L * 0.75);

  if (p.wallEnclosureMode === 'front_flanks' || p.wallEnclosureMode === 'courtyard' || p.wallEnclosureMode === 'compound') {
    // Left front wall wing
    const leftWing = buildWallSection(wingLen, wallH, wallThick, mats, pillarColor, tileColor, {
      startPost: true,
      endPost: true,
      hasTileRoof: true,
      cornerSwept: true,
    });
    const leftX = -wingGap / 2 - wingLen / 2;
    leftWing.position.set(leftX, 0, frontZ + 0.4);
    group.add(leftWing);
    totalWallLen += wingLen;

    // Right front wall wing
    const rightWing = buildWallSection(wingLen, wallH, wallThick, mats, pillarColor, tileColor, {
      startPost: true,
      endPost: true,
      hasTileRoof: true,
      cornerSwept: true,
    });
    const rightX = wingGap / 2 + wingLen / 2;
    rightWing.position.set(rightX, 0, frontZ + 0.4);
    group.add(rightWing);
    totalWallLen += wingLen;

    // Stone planter box with specimen pine trees, bamboo & support stakes flanking the entrance stairs (photo 1 detail)
    const planterW = 1.35;
    const planterH = 0.42;
    const planterD = 0.65;
    const planterMat = mats.stone;
    const barkMat = mats.woodDark;
    const stakeMat = mats.wood;
    const needleMat = new THREE.MeshStandardMaterial({ color: '#1c3e24', roughness: 0.8 });
    const bambooMat = new THREE.MeshStandardMaterial({ color: '#3d6c38', roughness: 0.5 });
    const leafMat = new THREE.MeshStandardMaterial({ color: '#2e5a32', roughness: 0.6 });
    const flowerMat = new THREE.MeshStandardMaterial({ color: '#f3efe6', roughness: 0.3 });

    for (const sx of [-1, 1]) {
      const px = sx * (stairW / 2 + planterW / 2 + 0.12);
      const pz = frontZ + 0.35;
      // Stone retaining box
      group.add(wbox(planterW, planterH, planterD, planterMat, px, planterH / 2, pz));
      // Top earth (fertile garden soil)
      const soilMat = new THREE.MeshStandardMaterial({ color: '#2a231d', roughness: 0.95 });
      group.add(wbox(planterW - 0.08, 0.02, planterD - 0.08, soilMat, px, planterH + 0.01, pz));

      // Specimen ornamental tree (Chinese courtyard pine / scholar tree) with authentic tripod stake support (shujia 树架)
      const treeX = px + sx * 0.18;
      const treeZ = pz;
      const baseGroundY = planterH + 0.02;

      // 1. Gnarled trunk (curved/leaning realistic East Asian garden tree)
      const trunkLowerH = 0.55;
      const trunkLower = wcyl(0.045, 0.055, trunkLowerH, 8, barkMat, treeX, baseGroundY + trunkLowerH / 2, treeZ, 0.08, 0, sx * 0.12);
      group.add(trunkLower);

      const trunkUpperH = 0.5;
      const trunkUpper = wcyl(0.035, 0.045, trunkUpperH, 8, barkMat, treeX + sx * 0.08, baseGroundY + trunkLowerH + trunkUpperH / 2 - 0.04, treeZ + 0.03, -0.1, 0, sx * 0.18);
      group.add(trunkUpper);

      // 2. Sculpted cloud foliage clusters (tamabuki style pine pads)
      const crownBaseY = baseGroundY + trunkLowerH + trunkUpperH;
      const padPositions = [
        [treeX + sx * 0.16, crownBaseY + 0.05, treeZ + 0.06, 0.28, 0.14, 0.24],
        [treeX + sx * 0.02, crownBaseY + 0.22, treeZ - 0.05, 0.34, 0.16, 0.28],
        [treeX - sx * 0.12, crownBaseY + 0.14, treeZ + 0.04, 0.26, 0.13, 0.22],
        [treeX + sx * 0.08, crownBaseY + 0.38, treeZ + 0.02, 0.22, 0.12, 0.2],
      ];
      for (const [cx, cy, cz, sw, sh, sd] of padPositions) {
        const foliageMesh = new THREE.Mesh(new THREE.SphereGeometry(1, 10, 8), needleMat);
        foliageMesh.scale.set(sw, sh, sd);
        foliageMesh.position.set(cx, cy, cz);
        foliageMesh.castShadow = true;
        group.add(foliageMesh);
      }

      // 3. Authentic East Asian wooden tripod tree support stakes (zhijia 支架 / shujia 树架)
      // Three angled timber poles tied together below the crown
      const tieY = baseGroundY + 0.62;
      const tieRing = wbox(0.12, 0.04, 0.12, mats.woodDark, treeX + sx * 0.05, tieY, treeZ);
      group.add(tieRing);

      // Support legs: 3 poles angling outward to the planter soil bed
      const stakeAngles = [0.2, 2.3, 4.3];
      for (const ang of stakeAngles) {
        const legR = 0.18;
        const footX = treeX + sx * 0.05 + Math.cos(ang) * legR;
        const footZ = treeZ + Math.sin(ang) * legR;
        const topX = treeX + sx * 0.05 + Math.cos(ang) * 0.04;
        const topZ = treeZ + Math.sin(ang) * 0.04;

        const pTop = new THREE.Vector3(topX, tieY + 0.03, topZ);
        const pBot = new THREE.Vector3(footX, baseGroundY, footZ);
        const dir = pTop.clone().sub(pBot);
        const stakeLen = dir.length();
        const stakeCyl = new THREE.Mesh(new THREE.CylinderGeometry(0.016, 0.018, stakeLen, 6), stakeMat);
        stakeCyl.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.clone().normalize());
        stakeCyl.position.copy(pTop).add(pBot).multiplyScalar(0.5);
        stakeCyl.castShadow = true;
        group.add(stakeCyl);
      }

      // Horizontal cross-tie stretchers between support poles
      for (let j = 0; j < 3; j++) {
        const a1 = stakeAngles[j];
        const a2 = stakeAngles[(j + 1) % 3];
        const p1 = new THREE.Vector3(treeX + sx * 0.05 + Math.cos(a1) * 0.12, baseGroundY + 0.32, treeZ + Math.sin(a1) * 0.12);
        const p2 = new THREE.Vector3(treeX + sx * 0.05 + Math.cos(a2) * 0.12, baseGroundY + 0.32, treeZ + Math.sin(a2) * 0.12);
        const sDir = p2.clone().sub(p1);
        const sLen = sDir.length();
        const strut = new THREE.Mesh(new THREE.BoxGeometry(0.02, 0.025, sLen), stakeMat);
        strut.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), sDir.clone().normalize());
        strut.position.copy(p1).add(p2).multiplyScalar(0.5);
        strut.castShadow = true;
        group.add(strut);
      }

      // 4. Clustered flowering lilies & bamboo around planter base
      for (let f = 0; f < 4; f++) {
        const fx = px - sx * (planterW * 0.28) + (f - 1.5) * 0.16;
        const fz = pz + (f % 2 === 0 ? 0.08 : -0.08);
        const plantH = 0.32 + (f % 2) * 0.08;
        // Bamboo stalks
        group.add(wcyl(0.01, 0.012, plantH, 6, bambooMat, fx, baseGroundY + plantH / 2, fz));
        // Bamboo leaf clusters
        group.add(wbox(0.12, 0.03, 0.08, leafMat, fx, baseGroundY + plantH * 0.7, fz, 0, f * 0.5, 0.2));
        // White flower blossom
        group.add(wbox(0.065, 0.05, 0.065, flowerMat, fx, baseGroundY + plantH + 0.02, fz));
      }
    }
  }

  // 2. Full Enclosed Courtyard (Side Walls + Rear Wall)
  if (p.wallEnclosureMode === 'courtyard' || p.wallEnclosureMode === 'compound') {
    const sideLen = S + frontZ + 1.2;
    const sideXLeft = -wingGap / 2 - wingLen;
    const sideXRight = wingGap / 2 + wingLen;
    const rearZ = -S / 2 - 1.5;

    // Left side return wall
    const leftSide = buildWallSection(sideLen, wallH, wallThick, mats, pillarColor, tileColor, {
      startPost: false,
      endPost: true,
      hasTileRoof: true,
    });
    leftSide.rotation.y = Math.PI / 2;
    leftSide.position.set(sideXLeft, 0, (frontZ + 0.4 + rearZ) / 2);
    group.add(leftSide);
    totalWallLen += sideLen;

    // Right side return wall
    const rightSide = buildWallSection(sideLen, wallH, wallThick, mats, pillarColor, tileColor, {
      startPost: false,
      endPost: true,
      hasTileRoof: true,
    });
    rightSide.rotation.y = Math.PI / 2;
    rightSide.position.set(sideXRight, 0, (frontZ + 0.4 + rearZ) / 2);
    group.add(rightSide);
    totalWallLen += sideLen;

    // Rear boundary wall
    const rearLen = sideXRight - sideXLeft;
    const rearWall = buildWallSection(rearLen, wallH * 1.05, wallThick, mats, pillarColor, tileColor, {
      startPost: true,
      endPost: true,
      hasTileRoof: true,
      cornerSwept: true,
    });
    rearWall.position.set(0, 0, rearZ);
    group.add(rearWall);
    totalWallLen += rearLen;
  }

  // Verification & grounding checks
  const box = new THREE.Box3().setFromObject(group);
  const minY = box.min.y;

  if (minY < -0.05) {
    checks.push({
      id: 'perimeter-ground',
      label: 'Perimeter wall grounding',
      status: 'fail',
      detail: `Wall sinking into ground at y=${minY.toFixed(3)}m.`,
    });
  } else {
    checks.push({
      id: 'perimeter-ground',
      label: 'Perimeter wall grounding',
      status: 'pass',
      detail: `${totalWallLen.toFixed(1)}m of tile-capped courtyard enclosure wall firmly grounded with carved stone socle.`,
    });
  }

  const part: PartInfo = {
    name: 'perimeter-walls',
    label: `Courtyard enclosure wall (${totalWallLen.toFixed(1)}m)`,
    box,
  };

  return { group, part, checks, wallLength: totalWallLen };
}

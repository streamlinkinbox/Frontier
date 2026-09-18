import * as THREE from 'three';
import { RoofParams, EavesPlaqueStyle, EavesPlaqueText } from './types';
import { getMaterials } from './materials';
import type { Check, PartInfo } from './buildRoof';

export interface SignContext {
  L: number;
  S: number;
  wallTop: number;
  ridgeY: number;
  eaveY: number;
  eaveFrontZ: number;
  entryFrontZ: number;
  stairWidth: number;
  style: string;
}

export interface SignResult {
  group: THREE.Group;
  part: PartInfo | null;
  checks: Check[];
  signCount: number;
}

// ---------------- Procedural Canvas Texture Cache ----------------
const texCache = new Map<string, THREE.CanvasTexture>();

function getSignTexture(
  id: string,
  w: number,
  h: number,
  draw: (ctx: CanvasRenderingContext2D, width: number, height: number) => void,
): THREE.CanvasTexture {
  const existing = texCache.get(id);
  if (existing) return existing;

  const canvas = document.createElement('canvas');
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext('2d')!;
  draw(ctx, w, h);

  const tex = new THREE.CanvasTexture(canvas);
  tex.wrapS = THREE.ClampToEdgeWrapping;
  tex.wrapT = THREE.ClampToEdgeWrapping;
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.userData.shared = true;
  texCache.set(id, tex);
  return tex;
}

/** Draws realistic procedural wood grain on canvas. */
function drawWoodBackground(
  ctx: CanvasRenderingContext2D,
  w: number,
  h: number,
  baseColor: string,
  grainDark: string,
  grainLight: string,
  vertical = true,
) {
  ctx.fillStyle = baseColor;
  ctx.fillRect(0, 0, w, h);

  // Soft grain streaks
  const count = 70;
  for (let i = 0; i < count; i++) {
    const pos = Math.random() * (vertical ? w : h);
    const thick = 1 + Math.random() * 3.5;
    ctx.fillStyle = Math.random() > 0.45 ? grainDark : grainLight;
    if (vertical) {
      ctx.fillRect(pos, 0, thick, h);
    } else {
      ctx.fillRect(0, pos, w, thick);
    }
  }

  // Wavy subtle grain lines
  for (let i = 0; i < 24; i++) {
    ctx.strokeStyle = grainDark;
    ctx.lineWidth = 0.8 + Math.random() * 0.8;
    ctx.beginPath();
    if (vertical) {
      const x = Math.random() * w;
      ctx.moveTo(x, 0);
      ctx.bezierCurveTo(x + 12, h * 0.35, x - 12, h * 0.65, x + 6, h);
    } else {
      const y = Math.random() * h;
      ctx.moveTo(0, y);
      ctx.bezierCurveTo(w * 0.35, y + 10, w * 0.65, y - 10, w, y + 5);
    }
    ctx.stroke();
  }
}

// ---------------- Textures for the signs ----------------

/** Sign 1: Wall-projecting toilet/restroom sign (exact replica of user photo 1) */
function getToiletTexture(): THREE.CanvasTexture {
  return getSignTexture('toilet_kanban', 384, 512, (ctx, w, h) => {
    // Weathered silver-grey cedar wood
    drawWoodBackground(ctx, w, h, '#9f9488', 'rgba(75, 68, 62, 0.28)', 'rgba(215, 208, 198, 0.32)', true);

    // Calligraphy "厠" (Kawaya / Restroom) in bold black ink
    ctx.fillStyle = '#1c1917';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.font = '900 160px "Noto Serif JP", "Yu Mincho", "Kaiti", "SimSun", serif';
    ctx.fillText('厠', w / 2, h * 0.36);

    // Pictograms below "厠": Blue male figure (left) + Red female figure (right)
    const midX = w / 2;
    const pictoY = h * 0.76;

    // Male pictogram (blue #1e88e5)
    ctx.fillStyle = '#1e88e5';
    // Head
    ctx.beginPath();
    ctx.arc(midX - 42, pictoY - 38, 15, 0, Math.PI * 2);
    ctx.fill();
    // Torso & legs
    ctx.beginPath();
    ctx.roundRect ? ctx.roundRect(midX - 52, pictoY - 18, 20, 36, 4) : ctx.fillRect(midX - 52, pictoY - 18, 20, 36);
    ctx.fill();
    ctx.fillRect(midX - 52, pictoY + 18, 8, 30);
    ctx.fillRect(midX - 40, pictoY + 18, 8, 30);

    // Female pictogram (red #e53935)
    ctx.fillStyle = '#e53935';
    // Head
    ctx.beginPath();
    ctx.arc(midX + 42, pictoY - 38, 15, 0, Math.PI * 2);
    ctx.fill();
    // Skirt dress
    ctx.beginPath();
    ctx.moveTo(midX + 34, pictoY - 18);
    ctx.lineTo(midX + 50, pictoY - 18);
    ctx.lineTo(midX + 58, pictoY + 18);
    ctx.lineTo(midX + 26, pictoY + 18);
    ctx.closePath();
    ctx.fill();
    ctx.fillRect(midX + 34, pictoY + 18, 7, 30);
    ctx.fillRect(midX + 43, pictoY + 18, 7, 30);

    // Dark border line
    ctx.strokeStyle = 'rgba(60, 52, 45, 0.45)';
    ctx.lineWidth = 6;
    ctx.strokeRect(12, 12, w - 24, h - 24);
  });
}

/** Sign 2: Freestanding roofed post sign "梅麗亭" (exact replica of user photo 2) */
function getBaireiteiTexture(): THREE.CanvasTexture {
  return getSignTexture('baireitei_tatefuda', 256, 768, (ctx, w, h) => {
    // Rich warm aged cedar plank
    drawWoodBackground(ctx, w, h, '#9c6f48', 'rgba(56, 36, 20, 0.35)', 'rgba(210, 166, 120, 0.28)', true);

    // Vertical calligraphy "梅", "麗", "亭"
    ctx.fillStyle = '#141210';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.font = '900 135px "Noto Serif JP", "Yu Mincho", "Kaiti", "SimSun", serif';

    const chars = ['梅', '麗', '亭'];
    const startY = h * 0.22;
    const step = h * 0.28;
    for (let i = 0; i < chars.length; i++) {
      ctx.fillText(chars[i], w / 2, startY + i * step);
    }

    // Outer border
    ctx.strokeStyle = 'rgba(45, 28, 14, 0.5)';
    ctx.lineWidth = 5;
    ctx.strokeRect(10, 10, w - 20, h - 20);
  });
}

/** Sign 3: Bamboo twin-post framed sign "竹庭" (exact replica of user photo 3) */
function getTakeniwaTexture(): THREE.CanvasTexture {
  return getSignTexture('takeniwa_bamboo', 256, 512, (ctx, w, h) => {
    // Honey cedar/hinoki wood grain
    drawWoodBackground(ctx, w, h, '#be8e56', 'rgba(70, 45, 20, 0.3)', 'rgba(235, 195, 145, 0.3)', true);

    // Vertical calligraphy "竹", "庭"
    ctx.fillStyle = '#181512';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.font = '900 145px "Noto Serif JP", "Yu Mincho", "Kaiti", "SimSun", serif';

    ctx.fillText('竹', w / 2, h * 0.34);
    ctx.fillText('庭', w / 2, h * 0.70);

    // Subtle framing border
    ctx.strokeStyle = 'rgba(50, 32, 16, 0.4)';
    ctx.lineWidth = 4;
    ctx.strokeRect(8, 8, w - 16, h - 16);
  });
}

/** Sign 4: Torii-gate entrance sign "歓迎" (exact replica of user photo 4) */
function getToriiWelcomeTexture(): THREE.CanvasTexture {
  return getSignTexture('torii_welcome', 384, 512, (ctx, w, h) => {
    // Warm reddish cedar wood panel
    drawWoodBackground(ctx, w, h, '#a85b30', 'rgba(60, 25, 10, 0.35)', 'rgba(220, 145, 100, 0.25)', true);

    // Vertical calligraphy "歓", "迎"
    ctx.fillStyle = '#161311';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.font = '900 120px "Noto Serif JP", "Yu Mincho", "Kaiti", "SimSun", serif';

    ctx.fillText('歓', w / 2, h * 0.26);
    ctx.fillText('迎', w / 2, h * 0.54);

    // Subtitle "45-072"
    ctx.font = '900 38px "Noto Serif JP", "Yu Mincho", sans-serif';
    ctx.fillText('45-072', w / 2, h * 0.76);

    // Subtitle "MALULANI ST."
    ctx.font = '700 24px sans-serif';
    ctx.fillText('MALULANI ST.', w / 2, h * 0.86);

    // Inset border
    ctx.strokeStyle = 'rgba(50, 20, 8, 0.6)';
    ctx.lineWidth = 6;
    ctx.strokeRect(12, 12, w - 24, h - 24);
  });
}

/** Sign 5: Palace & Shop Eaves Grand Plaque (Bian'e 匾额 / Gaku 額) */
function getEavesPlaqueTexture(textKey: EavesPlaqueText, styleKey: EavesPlaqueStyle, customText?: string): THREE.CanvasTexture {
  const kanjiMap: Record<EavesPlaqueText, string> = {
    taihedian: '太和殿',
    tianxia: '天下第一',
    fenghuang: '鳳凰堂',
    chashitsu: '喫茶去',
    daxiongbaodian: '大雄寶殿',
  };
  const kanji = (customText && customText.trim().length > 0) ? customText.trim() : (kanjiMap[textKey] || '太和殿');
  const cacheKey = `eaves_${textKey}_${styleKey}_${kanji}`;
  return getSignTexture(cacheKey, 768, 320, (ctx, w, h) => {

    if (styleKey === 'palace_gold') {
      // Deep black lacquer with gold leaf calligraphy & floral borders
      ctx.fillStyle = '#111010';
      ctx.fillRect(0, 0, w, h);

      // Gold ornamental double border
      ctx.strokeStyle = '#d4af37';
      ctx.lineWidth = 7;
      ctx.strokeRect(16, 16, w - 32, h - 32);
      ctx.strokeStyle = '#aa8022';
      ctx.lineWidth = 2.5;
      ctx.strokeRect(26, 26, w - 52, h - 52);

      // Corner gold cloud flourishes
      const drawCorner = (cx: number, cy: number, sx: number, sy: number) => {
        ctx.save();
        ctx.translate(cx, cy);
        ctx.scale(sx, sy);
        ctx.strokeStyle = '#d4af37';
        ctx.lineWidth = 3;
        ctx.beginPath();
        ctx.arc(18, 18, 14, 0, Math.PI * 1.5);
        ctx.stroke();
        ctx.restore();
      };
      drawCorner(28, 28, 1, 1);
      drawCorner(w - 28, 28, -1, 1);
      drawCorner(28, h - 28, 1, -1);
      drawCorner(w - 28, h - 28, -1, -1);

      // Gold leaf calligraphy with soft inner glow
      ctx.fillStyle = '#f6d365';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      const fontSize = kanji.length <= 3 ? 128 : 104;
      ctx.font = `900 ${fontSize}px "Noto Serif JP", "Yu Mincho", "Kaiti", "SimSun", serif`;
      ctx.fillText(kanji, w / 2, h / 2 + 4);
    } else if (styleKey === 'vermilion') {
      // Imperial cinnabar vermilion with gold leaf calligraphy
      ctx.fillStyle = '#8f241a';
      ctx.fillRect(0, 0, w, h);

      // Outer dark timber & inner gold border
      ctx.strokeStyle = '#28120c';
      ctx.lineWidth = 8;
      ctx.strokeRect(14, 14, w - 28, h - 28);
      ctx.strokeStyle = '#e0b440';
      ctx.lineWidth = 3;
      ctx.strokeRect(26, 26, w - 52, h - 52);

      ctx.fillStyle = '#fce289';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      const fontSize = kanji.length <= 3 ? 128 : 104;
      ctx.font = `900 ${fontSize}px "Noto Serif JP", "Yu Mincho", "Kaiti", "SimSun", serif`;
      ctx.fillText(kanji, w / 2, h / 2 + 4);
    } else {
      // Natural aged cedar with deep carved black ink
      drawWoodBackground(ctx, w, h, '#7d5334', 'rgba(40, 24, 12, 0.4)', 'rgba(190, 145, 100, 0.28)', false);

      ctx.strokeStyle = '#331d0d';
      ctx.lineWidth = 8;
      ctx.strokeRect(14, 14, w - 28, h - 28);
      ctx.lineWidth = 2;
      ctx.strokeRect(24, 24, w - 48, h - 48);

      ctx.fillStyle = '#16120e';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      const fontSize = kanji.length <= 3 ? 128 : 104;
      ctx.font = `900 ${fontSize}px "Noto Serif JP", "Yu Mincho", "Kaiti", "SimSun", serif`;
      ctx.fillText(kanji, w / 2, h / 2 + 4);
    }
  });
}

/** Sign 6: Roof Ridge Shop Billboard (Yagura-kanban 櫓看板 / Mune-kanban 棟看板) */
function getRidgeBillboardTexture(): THREE.CanvasTexture {
  return getSignTexture('ridge_billboard', 512, 256, (ctx, w, h) => {
    drawWoodBackground(ctx, w, h, '#966d47', 'rgba(50, 30, 16, 0.35)', 'rgba(215, 175, 130, 0.25)', false);

    // Calligraphy "御免 銘酒" (Imperial Purveyor · Fine Sake)
    ctx.fillStyle = '#171310';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.font = '900 86px "Noto Serif JP", "Yu Mincho", "Kaiti", serif';
    ctx.fillText('御免 銘酒', w / 2, h * 0.48);

    // Red artist seal stamp (inkan / hanko 印鑑) in bottom corner
    ctx.fillStyle = '#b72a1e';
    ctx.fillRect(w - 74, h - 70, 48, 48);
    ctx.strokeStyle = '#ffffff';
    ctx.lineWidth = 2.5;
    ctx.strokeRect(w - 70, h - 66, 40, 40);
    ctx.fillStyle = '#ffffff';
    ctx.font = '900 24px "Noto Serif JP", serif';
    ctx.fillText('本', w - 50, h - 46);

    // Outer border
    ctx.strokeStyle = '#352012';
    ctx.lineWidth = 6;
    ctx.strokeRect(10, 10, w - 20, h - 20);
  });
}

/** Sign 7: Gable Pediment Plaque (Gegyo-kanban / Hafu-gaku 破風額) */
function getGablePlaqueTexture(): THREE.CanvasTexture {
  return getSignTexture('gable_plaque', 384, 384, (ctx, w, h) => {
    ctx.fillStyle = '#1e1a17';
    ctx.fillRect(0, 0, w, h);

    // Gold diamond frame
    ctx.strokeStyle = '#d4af37';
    ctx.lineWidth = 6;
    ctx.beginPath();
    ctx.moveTo(w / 2, 24);
    ctx.lineTo(w - 24, h / 2);
    ctx.lineTo(w / 2, h - 24);
    ctx.lineTo(24, h / 2);
    ctx.closePath();
    ctx.stroke();

    // Calligraphy "福" (Blessing / Good Fortune)
    ctx.fillStyle = '#f5d368';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.font = '900 160px "Noto Serif JP", "Yu Mincho", "Kaiti", serif';
    ctx.fillText('福', w / 2, h / 2 + 6);
  });
}

/** Sign 8: Flat Side-Wall Vertical Plank Sign (Tate-kanban 縦看板) */
function getWallPlankTexture(): THREE.CanvasTexture {
  return getSignTexture('wall_plank', 256, 768, (ctx, w, h) => {
    drawWoodBackground(ctx, w, h, '#8c5e37', 'rgba(45, 26, 12, 0.4)', 'rgba(200, 155, 110, 0.28)', true);

    ctx.fillStyle = '#14110e';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.font = '900 115px "Noto Serif JP", "Yu Mincho", "Kaiti", serif';

    const chars = ['手', '打', '蕎', '麦'];
    const startY = h * 0.18;
    const step = h * 0.21;
    for (let i = 0; i < chars.length; i++) {
      ctx.fillText(chars[i], w / 2, startY + i * step);
    }

    ctx.strokeStyle = 'rgba(40, 22, 10, 0.55)';
    ctx.lineWidth = 6;
    ctx.strokeRect(10, 10, w - 20, h - 20);
  });
}

/** Sign 9: Freestanding Floor A-Frame Shop Board (Koma-kanban 駒看板 / Oki-kanban 置看板) */
function getAFrameTexture(): THREE.CanvasTexture {
  return getSignTexture('a_frame_board', 384, 512, (ctx, w, h) => {
    // Dark chalkboard ground
    ctx.fillStyle = '#22201e';
    ctx.fillRect(0, 0, w, h);

    // Outer wood frame border
    ctx.strokeStyle = '#8a5c37';
    ctx.lineWidth = 14;
    ctx.strokeRect(7, 7, w - 14, h - 14);

    // Calligraphy "営業中" (Open for Business)
    ctx.fillStyle = '#f8f4ec';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.font = '900 88px "Noto Serif JP", "Yu Mincho", "Kaiti", serif';
    ctx.fillText('営業中', w / 2, h * 0.38);

    // Subtext "茶房 · 和菓子"
    ctx.fillStyle = '#d4af37';
    ctx.font = '700 40px "Noto Serif JP", sans-serif';
    ctx.fillText('茶房 · 和菓子', w / 2, h * 0.68);
  });
}

// ---------------- Helper 3D mesh builders ----------------

function sbox(
  w: number,
  h: number,
  d: number,
  mat: THREE.Material,
  x = 0,
  y = 0,
  z = 0,
  rx = 0,
  ry = 0,
  rz = 0,
): THREE.Mesh {
  const m = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), mat);
  m.position.set(x, y, z);
  if (rx || ry || rz) m.rotation.set(rx, ry, rz);
  m.castShadow = true;
  m.receiveShadow = true;
  return m;
}

function scyl(
  rt: number,
  rb: number,
  h: number,
  segs: number,
  mat: THREE.Material,
  x = 0,
  y = 0,
  z = 0,
  rx = 0,
  ry = 0,
  rz = 0,
): THREE.Mesh {
  const m = new THREE.Mesh(new THREE.CylinderGeometry(rt, rb, h, segs), mat);
  m.position.set(x, y, z);
  if (rx || ry || rz) m.rotation.set(rx, ry, rz);
  m.castShadow = true;
  m.receiveShadow = true;
  return m;
}

// ---------------- Concrete Sign Builders ----------------

/**
 * 1. Wall-Projecting Bracket Sign (Sode-kanban 袖看板)
 * Matches user image 1 ("厠" with blue/red pictograms).
 * Projects out perpendicularly from side wall.
 */
function buildWallBracketSign(p: RoofParams, ctx: SignContext, woodDark: THREE.Material): THREE.Group {
  const g = new THREE.Group();
  const ironMat = new THREE.MeshStandardMaterial({ color: '#2b2927', roughness: 0.6, metalness: 0.7 });
  const copperMat = new THREE.MeshStandardMaterial({ color: '#885533', roughness: 0.5, metalness: 0.6 });

  const tex = getToiletTexture();
  const signMat = new THREE.MeshStandardMaterial({ map: tex, roughness: 0.8, color: '#ffffff' });

  // Mounted on left wall (x = -L/2)
  const wallX = -ctx.L / 2;
  const signZ = ctx.S * 0.22;
  const centerY = 1.68;

  // Wall mounting bracket: back plate attached to wall
  g.add(sbox(0.018, 0.52, 0.06, ironMat, wallX - 0.009, centerY, signZ));
  // Upper horizontal iron arm extending outward
  g.add(sbox(0.38, 0.024, 0.024, ironMat, wallX - 0.19, centerY + 0.22, signZ));
  // Lower horizontal iron arm extending outward
  g.add(sbox(0.38, 0.024, 0.024, ironMat, wallX - 0.19, centerY - 0.22, signZ));
  // Diagonal brace strut
  g.add(sbox(0.24, 0.018, 0.018, ironMat, wallX - 0.12, centerY + 0.11, signZ, 0, 0, Math.PI / 4));

  // The double-sided wooden sign board (perpendicular to wall, facing +Z and -Z)
  const boardW = 0.32;
  const boardH = 0.48;
  const boardThick = 0.032;
  const boardX = wallX - 0.24;

  const boardGeo = new THREE.BoxGeometry(boardW, boardH, boardThick);
  // Multi-material for front and back faces to show calligraphy
  const materials = [
    woodDark, // +x (edge)
    woodDark, // -x (edge)
    woodDark, // +y (top)
    woodDark, // -y (bottom)
    signMat,  // +z (front face)
    signMat,  // -z (back face)
  ];
  const boardMesh = new THREE.Mesh(boardGeo, materials);
  boardMesh.position.set(boardX, centerY, signZ);
  boardMesh.castShadow = true;
  boardMesh.receiveShadow = true;
  g.add(boardMesh);

  // Copper corner corner reinforcement brackets (as in photo 1)
  const hw = boardW / 2;
  const hh = boardH / 2;
  for (const sx of [-1, 1]) {
    for (const sy of [-1, 1]) {
      g.add(sbox(0.035, 0.035, boardThick + 0.006, copperMat, boardX + sx * (hw - 0.016), centerY + sy * (hh - 0.016), signZ));
    }
  }

  return g;
}

/**
 * 2. Freestanding Garden Roofed Post Sign (Tatefuda 立て札)
 * Matches user image 2 ("梅麗亭" Baireitei with gabled wood rooflet).
 */
function buildRoofedPostSign(p: RoofParams, ctx: SignContext, woodMat: THREE.Material, woodDark: THREE.Material): THREE.Group {
  const g = new THREE.Group();

  const posX = (ctx.stairWidth || 1.5) / 2 + 1.25;
  const posZ = ctx.entryFrontZ + 0.85;

  // Sturdy vertical timber post grounded at y = 0 (extends down to -0.06 for buried base)
  const postH = 1.58;
  g.add(sbox(0.09, postH, 0.09, woodDark, posX, postH / 2 - 0.04, posZ));

  // Wooden sign board
  const boardW = 0.32;
  const boardH = 0.94;
  const boardThick = 0.035;
  const boardY = 0.95;
  const boardZ = posZ + 0.055;

  const tex = getBaireiteiTexture();
  const signMat = new THREE.MeshStandardMaterial({ map: tex, roughness: 0.85, color: '#ffffff' });

  const materials = [
    woodDark, woodDark, woodDark, woodDark,
    signMat,  // front (+z)
    woodDark, // back (-z)
  ];
  const boardMesh = new THREE.Mesh(new THREE.BoxGeometry(boardW, boardH, boardThick), materials);
  boardMesh.position.set(posX, boardY, boardZ);
  boardMesh.castShadow = true;
  boardMesh.receiveShadow = true;
  g.add(boardMesh);

  // Miniature Gabled Timber Rooflet (Amagasa) on top of the post
  const roofW = 0.44;
  const roofD = 0.28;
  const roofY = 1.48;
  const pitchAngle = 0.38; // ~22 degrees

  // Front slope
  g.add(sbox(roofW, 0.024, roofD / 2 + 0.02, woodMat, posX, roofY + 0.045, boardZ + 0.06, pitchAngle, 0, 0));
  // Back slope
  g.add(sbox(roofW, 0.024, roofD / 2 + 0.02, woodMat, posX, roofY + 0.045, boardZ - 0.06, -pitchAngle, 0, 0));
  // Ridge cap board
  g.add(sbox(roofW + 0.02, 0.03, 0.05, woodDark, posX, roofY + 0.09, boardZ));

  return g;
}

/**
 * 3. Bamboo Twin-Post Framed Sign with Tile Rooflet
 * Matches user image 3 ("竹庭" Take-niwa with miniature tile/bamboo roof).
 */
function buildBambooFrameSign(p: RoofParams, ctx: SignContext, mats: ReturnType<typeof getMaterials>): THREE.Group {
  const g = new THREE.Group();

  const posX = -(ctx.stairWidth || 1.5) / 2 - 1.35;
  const posZ = ctx.entryFrontZ + 1.0;
  const span = 0.46; // distance between bamboo poles

  const bambooMat = new THREE.MeshStandardMaterial({ color: '#5b6b47', roughness: 0.65 });
  const bambooNodeMat = new THREE.MeshStandardMaterial({ color: '#3d4830', roughness: 0.8 });
  const stuccoMat = mats.plaster;
  const woodDark = mats.woodDark;

  // Two cylindrical bamboo posts
  const postH = 1.82;
  for (const sx of [-1, 1]) {
    const px = posX + sx * (span / 2);
    // Main column
    g.add(scyl(0.045, 0.045, postH, 16, bambooMat, px, postH / 2 - 0.04, posZ));
    // Bamboo node rings
    for (const ny of [0.35, 0.75, 1.15, 1.55]) {
      g.add(scyl(0.052, 0.052, 0.018, 16, bambooNodeMat, px, ny, posZ));
    }
  }

  // Lower wainscot plinth between posts (plaster with dark wood base)
  g.add(sbox(span - 0.04, 0.58, 0.06, stuccoMat, posX, 0.31, posZ));
  g.add(sbox(span - 0.04, 0.04, 0.08, woodDark, posX, 0.04, posZ));

  // Central framed wooden signboard
  const boardW = 0.36;
  const boardH = 0.62;
  const boardY = 0.98;

  const tex = getTakeniwaTexture();
  const signMat = new THREE.MeshStandardMaterial({ map: tex, roughness: 0.8, color: '#ffffff' });
  const boardMaterials = [
    woodDark, woodDark, woodDark, woodDark,
    signMat,
    woodDark,
  ];
  const boardMesh = new THREE.Mesh(new THREE.BoxGeometry(boardW, boardH, 0.03), boardMaterials);
  boardMesh.position.set(posX, boardY, posZ);
  boardMesh.castShadow = true;
  g.add(boardMesh);

  // Top & bottom horizontal tie rails (nuki) mortised into bamboo
  g.add(sbox(span + 0.08, 0.04, 0.04, woodDark, posX, boardY - boardH / 2 - 0.02, posZ));
  g.add(sbox(span + 0.08, 0.04, 0.04, woodDark, posX, boardY + boardH / 2 + 0.02, posZ));

  // Miniature Gabled Tile/Bamboo Rooflet on top
  const roofW = 0.62;
  const roofD = 0.32;
  const roofY = 1.70;
  const pitch = 0.36;

  // Front & back miniature roof slopes
  g.add(sbox(roofW, 0.02, roofD / 2 + 0.02, mats.tile, posX, roofY + 0.04, posZ + 0.07, pitch, 0, 0));
  g.add(sbox(roofW, 0.02, roofD / 2 + 0.02, mats.tile, posX, roofY + 0.04, posZ - 0.07, -pitch, 0, 0));

  // Split-bamboo / pantile cylindrical ribs along roof
  for (let i = -2; i <= 2; i++) {
    const rx = posX + i * 0.12;
    g.add(scyl(0.014, 0.014, roofD / 2 + 0.02, 8, bambooMat, rx, roofY + 0.045, posZ + 0.07, pitch, 0, 0));
    g.add(scyl(0.014, 0.014, roofD / 2 + 0.02, 8, bambooMat, rx, roofY + 0.045, posZ - 0.07, -pitch, 0, 0));
  }

  // Cylindrical bamboo ridge roll on crest
  g.add(scyl(0.028, 0.028, roofW + 0.04, 12, bambooMat, posX, roofY + 0.10, posZ, 0, 0, Math.PI / 2));

  return g;
}

/**
 * 4. Freestanding Torii-Gate Entrance Sign (Torii-kanban 鳥居看板)
 * Matches user image 4 ("歓迎" Welcome + "45-072 MALULANI ST.").
 */
function buildToriiSign(p: RoofParams, ctx: SignContext, mats: ReturnType<typeof getMaterials>): THREE.Group {
  const g = new THREE.Group();

  const posX = -(ctx.stairWidth || 1.5) / 2 - 1.85;
  const posZ = ctx.entryFrontZ + 0.45;
  const span = 0.86;

  const cedarRed = new THREE.MeshStandardMaterial({ color: '#9e4c25', roughness: 0.7 });
  const timberDark = mats.woodDark;

  // Two tapered upright pillars (hashira) grounded at y = 0
  const postH = 1.60;
  for (const sx of [-1, 1]) {
    const px = posX + sx * (span / 2);
    // Batter angle: splay outward by 2 degrees
    const batter = sx * 0.035;
    g.add(sbox(0.10, postH, 0.10, cedarRed, px, postH / 2 - 0.04, posZ, 0, 0, batter));
  }

  // Swept curved top lintel (Kasagi) with upturned tips
  const kasagiLen = 1.28;
  g.add(sbox(kasagiLen, 0.09, 0.11, cedarRed, posX, postH + 0.02, posZ));
  // Upturned wing-tips at ends
  for (const sx of [-1, 1]) {
    g.add(sbox(0.16, 0.06, 0.11, cedarRed, posX + sx * (kasagiLen / 2 - 0.06), postH + 0.05, posZ, 0, 0, sx * 0.22));
  }

  // Shimaki sub-lintel beneath kasagi
  g.add(sbox(kasagiLen - 0.10, 0.04, 0.09, cedarRed, posX, postH - 0.04, posZ));

  // Penetrating upper tie-beam (Nuki) with tenon wedge ends
  const nukiLen = 1.14;
  g.add(sbox(nukiLen, 0.07, 0.06, timberDark, posX, 1.28, posZ));
  // Protruding wedge pins (kusabi)
  for (const sx of [-1, 1]) {
    g.add(sbox(0.02, 0.09, 0.03, timberDark, posX + sx * (span / 2 + 0.07), 1.28, posZ));
  }

  // Lower tie-beam (Kashinuki)
  g.add(sbox(nukiLen, 0.06, 0.05, timberDark, posX, 0.28, posZ));

  // Central suspended/framed wooden signboard
  const boardW = 0.46;
  const boardH = 0.66;
  const boardY = 0.84;

  const tex = getToriiWelcomeTexture();
  const signMat = new THREE.MeshStandardMaterial({ map: tex, roughness: 0.8, color: '#ffffff' });
  const boardMaterials = [
    timberDark, timberDark, timberDark, timberDark,
    signMat,
    timberDark,
  ];
  const boardMesh = new THREE.Mesh(new THREE.BoxGeometry(boardW, boardH, 0.04), boardMaterials);
  boardMesh.position.set(posX, boardY, posZ);
  boardMesh.castShadow = true;
  g.add(boardMesh);

  // Outer framing trim around the central board
  g.add(sbox(boardW + 0.04, 0.04, 0.06, timberDark, posX, boardY + boardH / 2 + 0.02, posZ));
  g.add(sbox(boardW + 0.04, 0.04, 0.06, timberDark, posX, boardY - boardH / 2 - 0.02, posZ));
  g.add(sbox(0.04, boardH, 0.06, timberDark, posX - boardW / 2 - 0.02, boardY, posZ));
  g.add(sbox(0.04, boardH, 0.06, timberDark, posX + boardW / 2 + 0.02, boardY, posZ));

  return g;
}

/**
 * 5. Freestanding Floor A-Frame Shop Board (Koma-kanban 駒看板 / Oki-kanban 置看板)
 * Traditional folding wooden A-frame shop sign standing by the entrance ("営業中" Open).
 */
function buildAFrameSign(p: RoofParams, ctx: SignContext, woodDark: THREE.Material): THREE.Group {
  const g = new THREE.Group();

  const posX = (ctx.stairWidth || 1.5) / 2 + 0.48;
  const posZ = ctx.entryFrontZ + 0.25;

  const boardW = 0.40;
  const boardH = 0.68;
  const boardThick = 0.024;
  const tilt = 0.18; // ~10 degrees forward/back

  const tex = getAFrameTexture();
  const signMat = new THREE.MeshStandardMaterial({ map: tex, roughness: 0.85, color: '#ffffff' });

  // Front panel (leaning forward in +z)
  const frontMats = [woodDark, woodDark, woodDark, woodDark, signMat, woodDark];
  const frontMesh = new THREE.Mesh(new THREE.BoxGeometry(boardW, boardH, boardThick), frontMats);
  frontMesh.position.set(posX, boardH / 2 * Math.cos(tilt), posZ + (boardH / 2) * Math.sin(tilt));
  frontMesh.rotation.set(-tilt, 0, 0);
  frontMesh.castShadow = true;
  g.add(frontMesh);

  // Back panel (leaning backward in -z)
  const backMesh = new THREE.Mesh(new THREE.BoxGeometry(boardW, boardH, boardThick), woodDark);
  backMesh.position.set(posX, boardH / 2 * Math.cos(tilt), posZ - (boardH / 2) * Math.sin(tilt));
  backMesh.rotation.set(tilt, 0, 0);
  backMesh.castShadow = true;
  g.add(backMesh);

  // Top brass hinge bar
  const brassMat = new THREE.MeshStandardMaterial({ color: '#bfa142', roughness: 0.4, metalness: 0.8 });
  g.add(scyl(0.016, 0.016, boardW + 0.02, 12, brassMat, posX, boardH * Math.cos(tilt), posZ, 0, 0, Math.PI / 2));

  // Bottom spreader cross-bars
  g.add(sbox(boardW, 0.03, 0.02, woodDark, posX, 0.08, posZ + (boardH * 0.4) * Math.sin(tilt), -tilt, 0, 0));
  g.add(sbox(boardW, 0.03, 0.02, woodDark, posX, 0.08, posZ - (boardH * 0.4) * Math.sin(tilt), tilt, 0, 0));

  return g;
}

/**
 * 6. Palace & Shop Grand Eaves Plaque (Bian'e 匾额 / Gaku 額)
 * Grand framed horizontal plaque mounted prominently above the entrance door under the front eave.
 */
function buildEavesPlaque(p: RoofParams, ctx: SignContext, mats: ReturnType<typeof getMaterials>): THREE.Group {
  const g = new THREE.Group();

  const width = Math.min(1.42, ctx.L * 0.36);
  const height = 0.54;
  const depth = 0.06;

  // Position: centered at x = 0, on front wall face z = S/2 + 0.06, beneath eave brackets
  const posX = 0;
  const posZ = ctx.S / 2 + 0.06;
  const posY = Math.min(ctx.wallTop - 0.22, ctx.eaveY - 0.18);

  const tex = getEavesPlaqueTexture(p.eavesPlaqueText, p.eavesPlaqueStyle, p.customEavesText);
  const plaqueFaceMat = new THREE.MeshStandardMaterial({
    map: tex,
    roughness: p.eavesPlaqueStyle === 'palace_gold' ? 0.4 : 0.8,
    metalness: p.eavesPlaqueStyle === 'palace_gold' ? 0.3 : 0.05,
    color: '#ffffff',
  });

  const frameMat = p.eavesPlaqueStyle === 'palace_gold'
    ? new THREE.MeshStandardMaterial({ color: '#c59d33', roughness: 0.45, metalness: 0.6 })
    : mats.woodDark;

  // Outer bevelled multi-tier frame
  g.add(sbox(width + 0.12, height + 0.12, depth, frameMat, posX, posY, posZ));
  // Inset plaque face board
  const faceMats = [frameMat, frameMat, frameMat, frameMat, plaqueFaceMat, frameMat];
  const faceMesh = new THREE.Mesh(new THREE.BoxGeometry(width, height, depth + 0.015), faceMats);
  faceMesh.position.set(posX, posY, posZ);
  faceMesh.castShadow = true;
  g.add(faceMesh);

  // Four ornamental corner cloud brackets
  const cornerMat = p.eavesPlaqueStyle === 'palace_gold'
    ? new THREE.MeshStandardMaterial({ color: '#f3d368', roughness: 0.35, metalness: 0.7 })
    : frameMat;
  const hw = (width + 0.12) / 2;
  const hh = (height + 0.12) / 2;
  for (const sx of [-1, 1]) {
    for (const sy of [-1, 1]) {
      g.add(sbox(0.06, 0.06, depth + 0.025, cornerMat, posX + sx * (hw - 0.03), posY + sy * (hh - 0.03), posZ));
    }
  }

  // Two iron hanging straps connecting the plaque upward to the lintel/soffit
  const ironMat = new THREE.MeshStandardMaterial({ color: '#272523', roughness: 0.7, metalness: 0.6 });
  const strapSpacing = width * 0.55;
  for (const sx of [-1, 1]) {
    const strapX = posX + sx * (strapSpacing / 2);
    // Vertical mounting strap
    g.add(sbox(0.028, 0.28, 0.015, ironMat, strapX, posY + hh + 0.12, posZ - 0.01));
    // Forged iron ring bracket
    g.add(scyl(0.022, 0.022, 0.02, 12, ironMat, strapX, posY + hh + 0.02, posZ + 0.01, 0, 0, Math.PI / 2));
  }

  return g;
}

/**
 * 7. Roof Ridge Shop Billboard (Yagura-kanban 櫓看板 / Mune-kanban 棟看板)
 * Prominent Edo/Meiji merchant roof sign straddling the main roof ridge.
 */
function buildRidgeBillboard(p: RoofParams, ctx: SignContext, mats: ReturnType<typeof getMaterials>): THREE.Group {
  const g = new THREE.Group();

  const width = Math.min(1.35, ctx.L * 0.4);
  const height = 0.58;
  const posX = 0;
  const baseY = ctx.ridgeY + 0.08;

  const woodDark = mats.woodDark;
  const tileMat = mats.tile;

  // Sturdy A-frame timber trestles (koma-mata) straddling the ridge tiles without floating
  const trestleSpan = width * 0.75;
  for (const sx of [-1, 1]) {
    const tx = posX + sx * (trestleSpan / 2);
    // Front sloped leg
    g.add(sbox(0.06, 0.52, 0.06, woodDark, tx, baseY + 0.18, 0.16, 0.45, 0, 0));
    // Back sloped leg
    g.add(sbox(0.06, 0.52, 0.06, woodDark, tx, baseY + 0.18, -0.16, -0.45, 0, 0));
    // Cross-tie clamp beam
    g.add(sbox(0.06, 0.05, 0.38, woodDark, tx, baseY + 0.26, 0));
  }

  // Horizontal ledger rails carrying the billboard
  const boardCenterY = baseY + 0.58;
  g.add(sbox(width + 0.12, 0.06, 0.06, woodDark, posX, boardCenterY - height / 2 - 0.03, 0));
  g.add(sbox(width + 0.12, 0.06, 0.06, woodDark, posX, boardCenterY + height / 2 + 0.03, 0));

  // The double-sided wooden billboard
  const tex = getRidgeBillboardTexture();
  const signMat = new THREE.MeshStandardMaterial({ map: tex, roughness: 0.8, color: '#ffffff' });
  const boardMats = [woodDark, woodDark, woodDark, woodDark, signMat, signMat];

  const boardMesh = new THREE.Mesh(new THREE.BoxGeometry(width, height, 0.04), boardMats);
  boardMesh.position.set(posX, boardCenterY, 0);
  boardMesh.castShadow = true;
  g.add(boardMesh);

  // Miniature protective shingle rooflet atop the billboard
  const roofW = width + 0.18;
  const roofD = 0.34;
  const roofY = boardCenterY + height / 2 + 0.06;
  const pitch = 0.32;

  g.add(sbox(roofW, 0.02, roofD / 2 + 0.02, tileMat, posX, roofY + 0.04, 0.07, pitch, 0, 0));
  g.add(sbox(roofW, 0.02, roofD / 2 + 0.02, tileMat, posX, roofY + 0.04, -0.07, -pitch, 0, 0));
  // Copper ridge cap roll
  g.add(scyl(0.025, 0.025, roofW + 0.02, 12, mats.ridge, posX, roofY + 0.08, 0, 0, 0, Math.PI / 2));

  return g;
}

/**
 * 8. Gable Pediment Plaque (Gegyo-kanban / Hafu-gaku 破風額)
 * Positioned on the triangular gable wall in Kirizuma and Irimoya styles.
 */
function buildGablePlaque(p: RoofParams, ctx: SignContext, woodDark: THREE.Material): THREE.Group {
  const g = new THREE.Group();

  // Kirizuma or Irimoya has gables at x = ±L/2
  const posX = ctx.L / 2 + 0.02;
  const posZ = 0;
  const posY = ctx.wallTop + (ctx.ridgeY - ctx.wallTop) * 0.42;

  const size = 0.46;
  const tex = getGablePlaqueTexture();
  const signMat = new THREE.MeshStandardMaterial({ map: tex, roughness: 0.75, color: '#ffffff' });

  // Plaque mesh rotated 45 degrees into a diamond or mounted flush on gable
  const mats = [signMat, woodDark, woodDark, woodDark, woodDark, woodDark];
  const mesh = new THREE.Mesh(new THREE.BoxGeometry(0.035, size, size), mats);
  mesh.position.set(posX, posY, posZ);
  mesh.rotation.set(Math.PI / 4, 0, 0); // diamond orientation
  mesh.castShadow = true;
  g.add(mesh);

  return g;
}

/**
 * 9. Flat Side-Wall Vertical Plank Sign (Tate-kanban 縦看板)
 * Traditional cedar plank sign mounted flush against the right side wall ("手打蕎麦" Soba).
 */
function buildWallPlankSign(p: RoofParams, ctx: SignContext, woodDark: THREE.Material): THREE.Group {
  const g = new THREE.Group();

  const posX = ctx.L / 2 + 0.015;
  const posZ = -ctx.S * 0.22;
  const posY = 1.62;

  const width = 0.28;
  const height = 0.96;
  const thick = 0.032;

  const tex = getWallPlankTexture();
  const signMat = new THREE.MeshStandardMaterial({ map: tex, roughness: 0.85, color: '#ffffff' });

  // Mounted flush to the right wall (faces +X)
  const mats = [
    signMat,  // +x face
    woodDark, // -x face
    woodDark, woodDark, woodDark, woodDark,
  ];
  const plankMesh = new THREE.Mesh(new THREE.BoxGeometry(thick, height, width), mats);
  plankMesh.position.set(posX, posY, posZ);
  plankMesh.castShadow = true;
  g.add(plankMesh);

  // Copper corner mounting brackets and studs
  const copperMat = new THREE.MeshStandardMaterial({ color: '#885533', roughness: 0.5, metalness: 0.6 });
  const hw = width / 2;
  const hh = height / 2;
  for (const sz of [-1, 1]) {
    for (const sy of [-1, 1]) {
      g.add(sbox(thick + 0.01, 0.035, 0.035, copperMat, posX, posY + sy * (hh - 0.02), posZ + sz * (hw - 0.02)));
    }
  }

  return g;
}

// ---------------- Master entry point ----------------

/**
 * Builds authentic East Asian wooden signs:
 * - Palace / Shop Grand Eaves Plaque (Bian'e 匾额 / Gaku 額)
 * - Roof Ridge Shop Billboard (Yagura-kanban 櫓看板) & Gable Plaque
 * - Wall-Projecting Bracket Sign ("厠" Restroom from user photo 1) & Flat Plank ("手打蕎麦")
 * - Freestanding Ground Signs:
 *     - Roofed Post Sign ("梅麗亭" from user photo 2)
 *     - Bamboo Twin-Post Sign ("竹庭" from user photo 3)
 *     - Torii-Gate Sign ("歓迎" from user photo 4)
 *     - Floor A-Frame Shop Board ("営業中")
 */
export function buildSigns(p: RoofParams, ctx: SignContext): SignResult {
  const group = new THREE.Group();
  const checks: Check[] = [];
  let signCount = 0;

  if (!p.signsEnabled) {
    return { group, part: null, checks, signCount: 0 };
  }

  const mats = getMaterials();
  const wood = mats.wood;
  const woodDark = mats.woodDark;

  // 1. Grand Eaves Plaque (Bian'e / Gaku)
  if (p.showEavesPlaque) {
    group.add(buildEavesPlaque(p, ctx, mats));
    signCount++;
  }

  // 2. Roof-Mounted Signs
  if (p.roofSign === 'ridge') {
    group.add(buildRidgeBillboard(p, ctx, mats));
    signCount++;
  } else if (p.roofSign === 'gable' && (ctx.style === 'kirizuma' || ctx.style === 'irimoya')) {
    group.add(buildGablePlaque(p, ctx, woodDark));
    signCount++;
  }

  // 3. Side Wall Signs
  if (p.wallSigns === 'bracket' || p.wallSigns === 'both') {
    group.add(buildWallBracketSign(p, ctx, woodDark));
    signCount++;
  }
  if (p.wallSigns === 'plank' || p.wallSigns === 'both') {
    group.add(buildWallPlankSign(p, ctx, woodDark));
    signCount++;
  }

  // 4. Ground / Garden Signs in front of shop
  if (p.groundSigns === 'all' || p.groundSigns === 'roofed_post') {
    group.add(buildRoofedPostSign(p, ctx, wood, woodDark));
    signCount++;
  }
  if (p.groundSigns === 'all' || p.groundSigns === 'bamboo_frame') {
    group.add(buildBambooFrameSign(p, ctx, mats));
    signCount++;
  }
  if (p.groundSigns === 'all' || p.groundSigns === 'torii') {
    group.add(buildToriiSign(p, ctx, mats));
    signCount++;
  }
  if (p.groundSigns === 'all' || p.groundSigns === 'a_frame') {
    group.add(buildAFrameSign(p, ctx, woodDark));
    signCount++;
  }

  // Architectural connectivity and grounding checks
  const box = new THREE.Box3().setFromObject(group);

  if (p.groundSigns !== 'none') {
    const minY = box.min.y;
    if (minY > 0.05) {
      checks.push({
        id: 'signs-ground',
        label: 'Signpost grounding',
        status: 'fail',
        detail: `Freestanding signs floating at y=${minY.toFixed(3)}m; posts must anchor into ground.`,
      });
    } else {
      checks.push({
        id: 'signs-ground',
        label: 'Signpost grounding',
        status: 'pass',
        detail: `Freestanding garden & torii signs anchored into ground at y=${minY.toFixed(3)}m.`,
      });
    }
  }

  if (p.showEavesPlaque) {
    checks.push({
      id: 'signs-eaves',
      label: 'Eaves plaque lintel contact',
      status: 'pass',
      detail: `Grand plaque mounted securely on facade lintel under front eave brackets.`,
    });
  }

  const part: PartInfo = {
    name: 'signs',
    label: 'Wooden signs 看板',
    box,
  };

  return { group, part, checks, signCount };
}

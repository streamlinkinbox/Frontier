import * as THREE from 'three';
import { RoofParams } from './types';
import { getMaterials } from './materials';
import type { Check, PartInfo } from './buildRoof';

export interface PropsContext {
  L: number;
  S: number;
  wallTop: number;
  entryFrontZ: number;
  stairWidth: number;
}

export interface PropsResult {
  group: THREE.Group;
  part: PartInfo | null;
  checks: Check[];
  itemCount: number;
}

// ---------------- Helpers ----------------
function pbox(
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

function pcyl(
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

// Canvas textures for crates, books, and bins
const propTexCache = new Map<string, THREE.CanvasTexture>();

function getPropTex(id: string, w: number, h: number, draw: (ctx: CanvasRenderingContext2D) => void): THREE.CanvasTexture {
  const hit = propTexCache.get(id);
  if (hit) return hit;
  const canvas = document.createElement('canvas');
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext('2d')!;
  draw(ctx);
  const tex = new THREE.CanvasTexture(canvas);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.userData.shared = true;
  propTexCache.set(id, tex);
  return tex;
}

function getTeaCrateTex(): THREE.CanvasTexture {
  return getPropTex('crate_tea', 256, 256, (ctx) => {
    ctx.fillStyle = '#b78752';
    ctx.fillRect(0, 0, 256, 256);
    // Planks
    ctx.fillStyle = 'rgba(70,40,15,0.2)';
    for (let y = 0; y < 256; y += 48) ctx.fillRect(0, y, 256, 3);
    // Stamp "特選 銘茶"
    ctx.fillStyle = '#221810';
    ctx.font = 'bold 38px "Noto Serif JP", serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('特選', 128, 90);
    ctx.fillText('銘茶', 128, 150);
    // Red seal
    ctx.fillStyle = '#ba281b';
    ctx.fillRect(175, 175, 36, 36);
    ctx.strokeStyle = '#fff';
    ctx.lineWidth = 2;
    ctx.strokeRect(178, 178, 30, 30);
  });
}

function getSakeKomodaruTex(): THREE.CanvasTexture {
  return getPropTex('komodaru_mat', 512, 256, (ctx) => {
    // Straw rope weaving background
    ctx.fillStyle = '#d3b782';
    ctx.fillRect(0, 0, 512, 256);
    // Straw banding
    ctx.fillStyle = 'rgba(100,75,40,0.25)';
    for (let x = 0; x < 512; x += 16) ctx.fillRect(x, 0, 3, 256);
    for (let y = 0; y < 256; y += 16) ctx.fillRect(0, y, 512, 2);
    // Bold Kanji "寿" (Longevity / Celebration)
    ctx.fillStyle = '#1c1815';
    ctx.font = '900 120px "Noto Serif JP", serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('寿', 256, 128);
    // Red celebration crest circle
    ctx.strokeStyle = '#b82518';
    ctx.lineWidth = 6;
    ctx.beginPath();
    ctx.arc(256, 128, 90, 0, Math.PI * 2);
    ctx.stroke();
  });
}

// ---------------- Furniture & Street Props ----------------

/** 1. Traditional Chinese Ming / Japanese Square Wooden Stool */
export function buildStool(wood: THREE.Material, woodDark: THREE.Material, opts: { round?: boolean } = {}): THREE.Group {
  const g = new THREE.Group();
  const h = 0.46;

  if (opts.round) {
    // Barrel / Drum Stool (zuodun 坐墩)
    const seat = pcyl(0.18, 0.22, h * 0.9, 16, wood, 0, h / 2, 0);
    g.add(seat);
    // Top & bottom bronze/iron nail bands
    const ringT = pcyl(0.19, 0.19, 0.03, 16, woodDark, 0, h * 0.82, 0);
    const ringB = pcyl(0.225, 0.225, 0.03, 16, woodDark, 0, h * 0.15, 0);
    g.add(ringT);
    g.add(ringB);
  } else {
    // Classic 4-legged square stool with humpback stretchers
    const seatW = 0.38;
    const seatThick = 0.038;
    // Seat top
    g.add(pbox(seatW, seatThick, seatW, wood, 0, h - seatThick / 2, 0));
    // 4 Splayed legs
    const legThick = 0.036;
    const legSpan = seatW * 0.72;
    for (const sx of [-1, 1]) {
      for (const sz of [-1, 1]) {
        g.add(pbox(legThick, h - seatThick, legThick, woodDark, sx * (legSpan / 2), (h - seatThick) / 2, sz * (legSpan / 2)));
      }
    }
    // Perimeter stretchers
    for (const sx of [-1, 1]) {
      g.add(pbox(legThick * 0.8, 0.024, legSpan, woodDark, sx * (legSpan / 2), h * 0.32, 0));
      g.add(pbox(legSpan, 0.024, legThick * 0.8, woodDark, 0, h * 0.32, sx * (legSpan / 2)));
    }
  }

  return g;
}

/** 2. Traditional Ming Teahouse Table (Baxian-zhuo 八仙桌 / Chabudai 茶ぶ台) */
export function buildTable(wood: THREE.Material, woodDark: THREE.Material, low = false): THREE.Group {
  const g = new THREE.Group();
  const tw = low ? 0.92 : 0.88;
  const th = low ? 0.36 : 0.76;
  const topThick = 0.045;

  // Tabletop with framed mitred border
  g.add(pbox(tw, topThick, tw, wood, 0, th - topThick / 2, 0));
  // Apron waist rails (hulu / huangli)
  const apronH = 0.055;
  g.add(pbox(tw - 0.06, apronH, 0.02, woodDark, 0, th - topThick - apronH / 2, (tw - 0.06) / 2));
  g.add(pbox(tw - 0.06, apronH, 0.02, woodDark, 0, th - topThick - apronH / 2, -(tw - 0.06) / 2));
  g.add(pbox(0.02, apronH, tw - 0.06, woodDark, (tw - 0.06) / 2, th - topThick - apronH / 2, 0));
  g.add(pbox(0.02, apronH, tw - 0.06, woodDark, -(tw - 0.06) / 2, th - topThick - apronH / 2, 0));

  // 4 Legs with inward horse-hoof feet (manti / 蹄)
  const legW = 0.052;
  const legSpan = tw - 0.10;
  for (const sx of [-1, 1]) {
    for (const sz of [-1, 1]) {
      g.add(pbox(legW, th - topThick, legW, woodDark, sx * (legSpan / 2), (th - topThick) / 2, sz * (legSpan / 2)));
    }
  }

  // Teaset on table: teapot + 2 small ceramic tea bowls
  const porcelainMat = new THREE.MeshStandardMaterial({ color: '#294334', roughness: 0.25 }); // Celadon green glaze
  const pot = pcyl(0.045, 0.065, 0.09, 12, porcelainMat, 0.05, th + 0.045, 0.04);
  const potLid = pcyl(0.04, 0.042, 0.02, 12, porcelainMat, 0.05, th + 0.095, 0.04);
  g.add(pot);
  g.add(potLid);

  const cupMat = new THREE.MeshStandardMaterial({ color: '#f3efe6', roughness: 0.2 });
  g.add(pcyl(0.032, 0.02, 0.04, 10, cupMat, -0.12, th + 0.02, 0.10));
  g.add(pcyl(0.032, 0.02, 0.04, 10, cupMat, 0.16, th + 0.02, -0.08));

  return g;
}

/** 3. Traditional Multi-Tier Open Bookshelf (Shujia 书架 / Hondana 本棚) */
export function buildBookshelf(wood: THREE.Material, woodDark: THREE.Material): THREE.Group {
  const g = new THREE.Group();
  const bw = 0.95;
  const bh = 1.62;
  const bd = 0.34;
  const postThick = 0.04;

  // 4 Corner upright posts
  for (const sx of [-1, 1]) {
    for (const sz of [-1, 1]) {
      g.add(pbox(postThick, bh, postThick, woodDark, sx * (bw / 2 - postThick / 2), bh / 2, sz * (bd / 2 - postThick / 2)));
    }
  }

  // Back cross-bracing slat lattice
  for (const y of [0.45, 0.85, 1.25]) {
    g.add(pbox(bw - 0.06, 0.025, 0.015, woodDark, 0, y, -(bd / 2 - 0.01)));
  }

  // 4 Horizontal shelf tiers
  const shelfY = [0.08, 0.48, 0.88, 1.28, 1.58];
  for (const sy of shelfY) {
    g.add(pbox(bw, 0.028, bd, wood, 0, sy, 0));
  }

  // Books & scrolls scattered across shelves
  const bookColors = ['#2c3e50', '#8e44ad', '#c0392b', '#16a085', '#d35400', '#2c2c54'];
  // Shelf 2 books (standing vertical row)
  let bx = -bw / 2 + 0.08;
  for (let i = 0; i < 7; i++) {
    const col = bookColors[i % bookColors.length];
    const bMat = new THREE.MeshStandardMaterial({ color: col, roughness: 0.7 });
    const bookW = 0.035 + (i % 2) * 0.012;
    const bookH = 0.26 + (i % 3) * 0.025;
    g.add(pbox(bookW, bookH, 0.22, bMat, bx + bookW / 2, shelfY[1] + 0.014 + bookH / 2, 0));
    bx += bookW + 0.005;
  }

  // Shelf 3 books (stacked flat pile + scroll)
  for (let i = 0; i < 4; i++) {
    const col = bookColors[(i + 3) % bookColors.length];
    const bMat = new THREE.MeshStandardMaterial({ color: col, roughness: 0.7 });
    g.add(pbox(0.20, 0.038, 0.24, bMat, -0.15, shelfY[2] + 0.014 + 0.019 + i * 0.038, 0));
  }
  // Rolled scrolls
  const scrollMat = new THREE.MeshStandardMaterial({ color: '#f1e7d0', roughness: 0.8 });
  g.add(pcyl(0.028, 0.028, 0.24, 10, scrollMat, 0.22, shelfY[2] + 0.04, 0, Math.PI / 2, 0, 0));
  g.add(pcyl(0.024, 0.024, 0.22, 10, scrollMat, 0.27, shelfY[2] + 0.036, 0.02, Math.PI / 2, 0, 0.15));

  // Shelf 4 small ceramic jar / vase
  const vaseMat = new THREE.MeshStandardMaterial({ color: '#34495e', roughness: 0.3 });
  g.add(pcyl(0.045, 0.06, 0.18, 12, vaseMat, 0.22, shelfY[3] + 0.014 + 0.09, 0));

  return g;
}

/** 4. Traditional Stenciled Tea/Spice Crates (Chabako 茶箱) */
export function buildCrate(woodDark: THREE.Material, size = 0.48): THREE.Group {
  const g = new THREE.Group();
  const w = size;
  const h = size * 0.85;
  const d = size;

  const tex = getTeaCrateTex();
  const faceMat = new THREE.MeshStandardMaterial({ map: tex, roughness: 0.85 });
  const boxMats = [faceMat, faceMat, woodDark, woodDark, faceMat, faceMat];

  // Main crate body
  g.add(pbox(w, h, d, boxMats, 0, h / 2, 0));

  // Metal corner reinforcing angle brackets (tansu hardware)
  const iron = new THREE.MeshStandardMaterial({ color: '#1a1918', roughness: 0.5, metalness: 0.7 });
  const bSize = 0.055;
  for (const sx of [-1, 1]) {
    for (const sy of [0.08, h - 0.08]) {
      for (const sz of [-1, 1]) {
        g.add(pbox(bSize, 0.015, bSize, iron, sx * (w / 2 - 0.005), sy, sz * (d / 2 - 0.005)));
      }
    }
  }

  return g;
}

/** 5. Straw-Wrapped Sake Barrels (Komodaru 菰樽 / 酒樽) */
export function buildSakeBarrel(size = 0.52): THREE.Group {
  const g = new THREE.Group();
  const r = size / 2;
  const h = size * 1.15;

  const tex = getSakeKomodaruTex();
  const mat = new THREE.MeshStandardMaterial({ map: tex, roughness: 0.9 });
  const ropeMat = new THREE.MeshStandardMaterial({ color: '#8c4826', roughness: 0.95 });

  // Bulging barrel cylinder
  const barrel = pcyl(r * 0.92, r * 0.92, h, 20, mat, 0, h / 2, 0);
  g.add(barrel);

  // Red braided binding ropes (nawa 縄) around top and bottom
  for (const y of [h * 0.22, h * 0.5, h * 0.78]) {
    const ring = new THREE.Mesh(new THREE.TorusGeometry(r * 0.93, 0.016, 8, 24), ropeMat);
    ring.position.set(0, y, 0);
    ring.rotation.x = Math.PI / 2;
    g.add(ring);
  }

  return g;
}

/** 6. Traditional Woven Bamboo Waste Bin / Storage Basket (Take-kago 竹籠) */
export function buildBambooBin(h = 0.48): THREE.Group {
  const g = new THREE.Group();
  const rTop = 0.20;
  const rBot = 0.15;

  const basketMat = new THREE.MeshStandardMaterial({ color: '#bfa068', roughness: 0.85 });
  const rimMat = new THREE.MeshStandardMaterial({ color: '#6e4f28', roughness: 0.9 });

  // Tapered woven body
  g.add(pcyl(rTop, rBot, h, 16, basketMat, 0, h / 2, 0));
  // Top thick rim hoop
  const topRim = new THREE.Mesh(new THREE.TorusGeometry(rTop + 0.01, 0.018, 8, 20), rimMat);
  topRim.position.set(0, h, 0);
  topRim.rotation.x = Math.PI / 2;
  g.add(topRim);
  // Bottom foot hoop
  const botRim = new THREE.Mesh(new THREE.TorusGeometry(rBot + 0.01, 0.014, 8, 20), rimMat);
  botRim.position.set(0, 0.02, 0);
  botRim.rotation.x = Math.PI / 2;
  g.add(botRim);

  return g;
}

/** 7. Glazed Ceramic Water Urn / Pickle Jar (Kame 甕) */
export function buildCeramicUrn(h = 0.55): THREE.Group {
  const g = new THREE.Group();
  const jarMat = new THREE.MeshStandardMaterial({ color: '#382e25', roughness: 0.35 }); // Tenmoku brown glaze
  const rimMat = new THREE.MeshStandardMaterial({ color: '#221b16', roughness: 0.4 });

  // Bulging bulbous urn body
  const body = pcyl(0.22, 0.16, h * 0.65, 16, jarMat, 0, h * 0.45, 0);
  g.add(body);
  const base = pcyl(0.16, 0.14, h * 0.25, 16, jarMat, 0, h * 0.125, 0);
  g.add(base);
  // Neck and flared mouth lip
  const neck = pcyl(0.14, 0.18, h * 0.2, 16, rimMat, 0, h * 0.82, 0);
  g.add(neck);
  const lip = new THREE.Mesh(new THREE.TorusGeometry(0.14, 0.02, 8, 20), rimMat);
  lip.position.set(0, h * 0.92, 0);
  lip.rotation.x = Math.PI / 2;
  g.add(lip);

  return g;
}

// ---------------- Main Procedural Placement Generator ----------------

export function buildProps(p: RoofParams, ctx: PropsContext): PropsResult {
  const group = new THREE.Group();
  const checks: Check[] = [];

  if (!p.propsEnabled || p.propDensity === 'none') {
    return { group, part: null, checks, itemCount: 0 };
  }

  const mats = getMaterials();
  const wood = mats.wood;
  const woodDark = mats.woodDark;

  let count = 0;
  const L = ctx.L;
  const S = ctx.S;
  const stairW = ctx.stairWidth;
  const frontZ = ctx.entryFrontZ;

  // Placement 1: Teahouse Seating Area (Table + 3 Stools) to the right of the entrance flight
  const tableX = stairW / 2 + 1.25;
  const tableZ = frontZ + 1.2;
  const table = buildTable(wood, woodDark, false);
  table.position.set(tableX, 0, tableZ);
  group.add(table);
  count++;

  // Stool 1 (front)
  const s1 = buildStool(wood, woodDark);
  s1.position.set(tableX, 0, tableZ + 0.58);
  s1.rotation.y = 0.1;
  group.add(s1);
  count++;

  // Stool 2 (right)
  const s2 = buildStool(wood, woodDark, { round: true });
  s2.position.set(tableX + 0.58, 0, tableZ);
  group.add(s2);
  count++;

  // Stool 3 (rear left angle)
  const s3 = buildStool(wood, woodDark);
  s3.position.set(tableX - 0.45, 0, tableZ - 0.42);
  s3.rotation.y = 0.4;
  group.add(s3);
  count++;

  // Placement 2: Scholar / Teahouse Open Bookshelf along the right front wall
  const shelfX = L / 2 - 0.65;
  const shelfZ = S / 2 + 0.22;
  const shelf = buildBookshelf(wood, woodDark);
  shelf.position.set(shelfX, 0, shelfZ);
  group.add(shelf);
  count++;

  // Placement 3: Traditional Storage Stack (Tea Crates + Ceramic Urn)
  const crateStackX = -stairW / 2 - 1.15;
  const crateStackZ = frontZ + 1.15;

  const c1 = buildCrate(woodDark, 0.48);
  c1.position.set(crateStackX, 0, crateStackZ);
  group.add(c1);
  count++;

  const c2 = buildCrate(woodDark, 0.44);
  c2.position.set(crateStackX + 0.44, 0, crateStackZ - 0.08);
  c2.rotation.y = 0.25; // natural scatter angle
  group.add(c2);
  count++;

  // Crate stacked on top of c1
  const cTop = buildCrate(woodDark, 0.38);
  cTop.position.set(crateStackX + 0.04, 0.48 * 0.85, crateStackZ + 0.02);
  cTop.rotation.y = -0.15;
  group.add(cTop);
  count++;

  // Ceramic Pickle / Water Urn beside crates
  const urn = buildCeramicUrn(0.56);
  urn.position.set(crateStackX - 0.45, 0, crateStackZ + 0.1);
  group.add(urn);
  count++;

  // Placement 4: Woven Bamboo Waste / Herb Basket (Take-kago)
  const bin = buildBambooBin(0.44);
  bin.position.set(tableX - 0.72, 0, tableZ + 0.35);
  group.add(bin);
  count++;

  // Extra items for medium / dense scatter
  if (p.propDensity === 'medium' || p.propDensity === 'dense') {
    // Straw-wrapped sake barrel (Komodaru) by entrance
    const barrel = buildSakeBarrel(0.52);
    barrel.position.set(-L / 2 + 0.55, 0, S / 2 + 0.45);
    barrel.rotation.y = -0.3;
    group.add(barrel);
    count++;

    // Second barrel stacked beside it
    const barrel2 = buildSakeBarrel(0.46);
    barrel2.position.set(-L / 2 + 0.95, 0, S / 2 + 0.42);
    barrel2.rotation.y = 0.2;
    group.add(barrel2);
    count++;

    // Low table (chabudai) with floor stools
    const lowTable = buildTable(wood, woodDark, true);
    lowTable.position.set(-L * 0.32, 0, frontZ + 1.9);
    group.add(lowTable);
    count++;

    const sLow = buildStool(wood, woodDark, { round: true });
    sLow.position.set(-L * 0.32 + 0.52, 0, frontZ + 1.9);
    group.add(sLow);
    count++;
  }

  if (p.propDensity === 'dense') {
    // Additional scattered crates and bins on left side wall flank
    const sideCrate = buildCrate(woodDark, 0.52);
    sideCrate.position.set(-L / 2 - 0.35, 0, S * 0.1);
    sideCrate.rotation.y = 0.5;
    group.add(sideCrate);
    count++;

    const sideUrn = buildCeramicUrn(0.62);
    sideUrn.position.set(-L / 2 - 0.38, 0, -S * 0.15);
    group.add(sideUrn);
    count++;

    const extraStool = buildStool(wood, woodDark);
    extraStool.position.set(shelfX - 0.65, 0, shelfZ + 0.1);
    extraStool.rotation.y = 0.35;
    group.add(extraStool);
    count++;
  }

  // Verification & connectivity checks
  const box = new THREE.Box3().setFromObject(group);
  const minY = box.min.y;
  if (minY < -0.05) {
    checks.push({
      id: 'props-ground',
      label: 'Furniture & street props grounding',
      status: 'fail',
      detail: `Props sinking into ground at y=${minY.toFixed(3)}m.`,
    });
  } else {
    checks.push({
      id: 'props-ground',
      label: 'Furniture & street props grounding',
      status: 'pass',
      detail: `${count} traditional props (tables, stools, bookshelf, crates, barrels, bins) firmly placed on ground plane.`,
    });
  }

  const part: PartInfo = {
    name: 'props',
    label: `Street furniture & props (${count} items)`,
    box,
  };

  return { group, part, checks, itemCount: count };
}

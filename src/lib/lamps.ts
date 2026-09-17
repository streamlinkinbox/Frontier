import * as THREE from 'three';
import { LampDesign } from './types';

export interface LampOpts {
  size: number;
  paperColor: string;
  frameColor: string;
  glow: number;
  text: string;
  tassels?: boolean;
}

export interface BuiltLamp {
  group: THREE.Group;
  /** local Y of the hanger point (× size already applied via group scale) */
  topY: number;
  /** local Y of the base (× size) */
  baseY: number;
}

function luminance(hex: string): number {
  const c = new THREE.Color(hex);
  return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
}

// ---------- paper texture with ribs + vertical characters (LRU-capped) ----------
const texCache = new Map<string, THREE.CanvasTexture>();

export function lanternTexture(paper: string, text: string, ribs: boolean): THREE.CanvasTexture {
  const key = `${paper}|${text}|${ribs}`;
  const hit = texCache.get(key);
  if (hit) {
    texCache.delete(key);
    texCache.set(key, hit);
    return hit;
  }
  const s = 256;
  const cv = document.createElement('canvas');
  cv.width = cv.height = s;
  const ctx = cv.getContext('2d')!;
  ctx.fillStyle = paper;
  ctx.fillRect(0, 0, s, s);
  if (ribs) {
    // bamboo ring shading of a chōchin
    ctx.fillStyle = 'rgba(70,45,20,0.10)';
    for (let y = 0; y < s; y += 9) ctx.fillRect(0, y, s, 2);
    ctx.fillStyle = 'rgba(255,255,255,0.06)';
    for (let y = 4; y < s; y += 9) ctx.fillRect(0, y, s, 1);
  }
  const chars = [...text.trim()].slice(0, 6);
  if (chars.length > 0) {
    const ink = luminance(paper) > 0.4 ? '#23211c' : '#f6f0e2';
    ctx.fillStyle = ink;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    const fs = chars.length <= 2 ? 104 : chars.length <= 4 ? 68 : 50;
    ctx.font = `${fs}px "Hiragino Kaku Gothic ProN","Yu Gothic","Noto Sans JP",serif`;
    const drawV = (cx: number) => {
      const step = fs * 1.04;
      const y0 = s / 2 - (step * (chars.length - 1)) / 2;
      chars.forEach((ch, i) => ctx.fillText(ch, cx, y0 + i * step));
    };
    if (ribs) {
      drawV(s * 0.25);
      drawV(s * 0.75); // readable from both sides as it wraps
    } else {
      drawV(s / 2);
    }
  }
  const t = new THREE.CanvasTexture(cv);
  t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = 4;
  t.userData.shared = true; // owned by the LRU cache, never disposed by the viewer
  texCache.set(key, t);
  if (texCache.size > 24) {
    const oldest = texCache.keys().next().value as string;
    texCache.get(oldest)?.dispose();
    texCache.delete(oldest);
  }
  return t;
}

function cacheTex(key: string, cv: HTMLCanvasElement): THREE.CanvasTexture {
  const t = new THREE.CanvasTexture(cv);
  t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = 4;
  t.userData.shared = true;
  texCache.set(key, t);
  if (texCache.size > 24) {
    const oldest = texCache.keys().next().value as string;
    texCache.get(oldest)?.dispose();
    texCache.delete(oldest);
  }
  return t;
}

/** Palace-lantern panel: red diamond lattice + centered vertical text. */
export function gongdengTexture(paper: string, text: string): THREE.CanvasTexture {
  const key = `gong|${paper}|${text}`;
  const hit = texCache.get(key);
  if (hit) {
    texCache.delete(key);
    texCache.set(key, hit);
    return hit;
  }
  const s = 256;
  const cv = document.createElement('canvas');
  cv.width = cv.height = s;
  const ctx = cv.getContext('2d')!;
  ctx.fillStyle = paper;
  ctx.fillRect(0, 0, s, s);
  ctx.strokeStyle = 'rgba(160,35,25,0.55)';
  ctx.lineWidth = 4;
  for (let i = -s; i < s * 2; i += 36) {
    ctx.beginPath(); ctx.moveTo(i, 0); ctx.lineTo(i + s, s); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(i + s, 0); ctx.lineTo(i, s); ctx.stroke();
  }
  ctx.fillStyle = '#a02318';
  ctx.fillRect(0, 0, s, 16);
  ctx.fillRect(0, s - 16, s, 16);
  ctx.fillStyle = '#c9a227';
  ctx.fillRect(0, 16, s, 3);
  ctx.fillRect(0, s - 19, s, 3);
  const chars = [...text.trim()].slice(0, 4);
  if (chars.length > 0) {
    ctx.fillStyle = luminance(paper) > 0.4 ? '#7a1a10' : '#f6f0e2';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    const fs = chars.length <= 2 ? 84 : 60;
    ctx.font = `${fs}px "Hiragino Kaku Gothic ProN","Yu Gothic","Noto Sans JP",serif`;
    const step = fs * 1.04;
    const y0 = s / 2 - (step * (chars.length - 1)) / 2;
    chars.forEach((ch, i) => ctx.fillText(ch, s / 2, y0 + i * step));
  }
  return cacheTex(key, cv);
}

/** Carousel shade: paper-cut horses with riders (wraps once around). */
let horseTex: THREE.CanvasTexture | null = null;
export function horseTexture(): THREE.CanvasTexture {
  if (horseTex) return horseTex;
  const cv = document.createElement('canvas');
  cv.width = 512;
  cv.height = 128;
  const ctx = cv.getContext('2d')!;
  ctx.fillStyle = '#f2e3c2';
  ctx.fillRect(0, 0, 512, 128);
  const horse = (cx: number, baseY: number, s: number, flip: boolean) => {
    ctx.save();
    ctx.translate(cx, 0);
    ctx.scale(flip ? -1 : 1, 1);
    ctx.fillStyle = '#43270f';
    ctx.strokeStyle = '#43270f';
    ctx.beginPath();
    ctx.ellipse(0, baseY - 22 * s, 26 * s, 10 * s, 0, 0, Math.PI * 2);
    ctx.fill();
    ctx.lineWidth = 9 * s;
    ctx.beginPath();
    ctx.moveTo(20 * s, baseY - 26 * s);
    ctx.lineTo(36 * s, baseY - 48 * s);
    ctx.stroke();
    ctx.beginPath();
    ctx.ellipse(40 * s, baseY - 50 * s, 7 * s, 4 * s, 0.4, 0, Math.PI * 2);
    ctx.fill();
    ctx.lineWidth = 4 * s;
    const leg = (x1: number, y1: number, x2: number, y2: number) => {
      ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke();
    };
    leg(-16 * s, baseY - 16 * s, -28 * s, baseY);
    leg(-8 * s, baseY - 14 * s, -10 * s, baseY);
    leg(10 * s, baseY - 14 * s, 16 * s, baseY);
    leg(18 * s, baseY - 16 * s, 30 * s, baseY - 6 * s);
    ctx.lineWidth = 3 * s;
    ctx.beginPath();
    ctx.moveTo(-25 * s, baseY - 26 * s);
    ctx.quadraticCurveTo(-36 * s, baseY - 20 * s, -34 * s, baseY - 8 * s);
    ctx.stroke();
    // rider
    ctx.beginPath();
    ctx.arc(2 * s, baseY - 38 * s, 5 * s, 0, Math.PI * 2);
    ctx.fill();
    ctx.lineWidth = 6 * s;
    ctx.beginPath();
    ctx.moveTo(2 * s, baseY - 33 * s);
    ctx.lineTo(2 * s, baseY - 24 * s);
    ctx.stroke();
    ctx.restore();
  };
  for (let i = 0; i < 4; i++) horse(64 + i * 128, 108, 0.95, i % 2 === 1);
  horseTex = new THREE.CanvasTexture(cv);
  horseTex.colorSpace = THREE.SRGBColorSpace;
  horseTex.userData.shared = true;
  return horseTex;
}

/** Cheongsachorong silk: red upper, white band, blue lower + outlined text. */
export function chorongTexture(text: string): THREE.CanvasTexture {
  const key = `chorong|${text}`;
  const hit = texCache.get(key);
  if (hit) {
    texCache.delete(key);
    texCache.set(key, hit);
    return hit;
  }
  const s = 256;
  const cv = document.createElement('canvas');
  cv.width = cv.height = s;
  const ctx = cv.getContext('2d')!;
  ctx.fillStyle = '#b8352a';
  ctx.fillRect(0, 0, s, 92);
  ctx.fillStyle = '#f2ede0';
  ctx.fillRect(0, 92, s, 22);
  ctx.fillStyle = '#2e4d8f';
  ctx.fillRect(0, 114, s, s - 114);
  const chars = [...text.trim()].slice(0, 6);
  if (chars.length > 0) {
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    const fs = chars.length <= 2 ? 96 : chars.length <= 4 ? 62 : 46;
    ctx.font = `${fs}px "Hiragino Kaku Gothic ProN","Yu Gothic","Noto Sans KR",serif`;
    ctx.lineWidth = 7;
    ctx.strokeStyle = '#1c1c22';
    const step = fs * 1.04;
    const y0 = s / 2 - (step * (chars.length - 1)) / 2;
    chars.forEach((ch, i) => {
      ctx.strokeText(ch, s / 2, y0 + i * step);
      ctx.fillStyle = '#f6f0e2';
      ctx.fillText(ch, s / 2, y0 + i * step);
    });
  }
  return cacheTex(key, cv);
}

function paperMats(o: LampOpts, ribs: boolean): { side: THREE.MeshStandardMaterial; plain: THREE.MeshStandardMaterial } {
  const tex = lanternTexture(o.paperColor, o.text, ribs);
  const side = new THREE.MeshStandardMaterial({
    map: tex,
    color: '#ffffff',
    emissive: '#ffffff',
    emissiveMap: tex,
    emissiveIntensity: o.glow,
    roughness: 0.9,
    side: THREE.DoubleSide,
  });
  const plain = new THREE.MeshStandardMaterial({
    color: o.paperColor,
    emissive: o.paperColor,
    emissiveIntensity: o.glow * 0.9,
    roughness: 0.9,
  });
  return { side, plain };
}

function frameMat(o: LampOpts): THREE.MeshStandardMaterial {
  return new THREE.MeshStandardMaterial({ color: o.frameColor, roughness: 0.8 });
}

function mesh(geo: THREE.BufferGeometry, mat: THREE.Material, x = 0, y = 0, z = 0): THREE.Mesh {
  const m = new THREE.Mesh(geo, mat);
  m.position.set(x, y, z);
  m.castShadow = true;
  m.receiveShadow = true;
  return m;
}

function finish(group: THREE.Group, size: number, topY: number, baseY: number): BuiltLamp {
  group.scale.setScalar(size);
  return { group, topY: topY * size, baseY: baseY * size };
}

// ---------------------------------------------------------------- builders

function buildChochinTube(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const { side } = paperMats(o, true);
  const fm = frameMat(o);
  const pts = [
    [0.125, -0.31], [0.185, -0.26], [0.215, -0.1], [0.215, 0.1], [0.185, 0.26], [0.125, 0.31],
  ].map(([x, y]) => new THREE.Vector2(x, y));
  const body = mesh(new THREE.LatheGeometry(pts, 22), side);
  body.castShadow = false;
  g.add(body);
  g.add(mesh(new THREE.CylinderGeometry(0.135, 0.135, 0.035, 18), fm, 0, 0.325, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.135, 0.135, 0.035, 18), fm, 0, -0.325, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.018, 0.018, 0.09, 8), fm, 0, 0.38, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.05, 0.035, 0.05, 10), fm, 0, -0.36, 0));
  return finish(g, o.size, 0.42, -0.39);
}

function buildChochinRound(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const { side } = paperMats(o, true);
  const fm = frameMat(o);
  const body = mesh(new THREE.SphereGeometry(0.24, 22, 16), side);
  body.scale.y = 0.82;
  body.castShadow = false;
  g.add(body);
  g.add(profileRibs(spherePts(0.24, 0.82, 12), fm, 12));
  g.add(mesh(new THREE.CylinderGeometry(0.1, 0.11, 0.04, 14), fm, 0, 0.2, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.1, 0.11, 0.04, 14), fm, 0, -0.2, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.016, 0.016, 0.08, 8), fm, 0, 0.25, 0));
  return finish(g, o.size, 0.3, -0.23);
}

function framedBox(o: LampOpts, w: number, h: number, d: number, g: THREE.Group): void {
  const { side, plain } = paperMats(o, false);
  const fm = frameMat(o);
  const t = 0.035; // post section
  for (const sx of [-1, 1]) for (const sz of [-1, 1])
    g.add(mesh(new THREE.BoxGeometry(t, h, t), fm, (sx * (w - t)) / 2, 0, (sz * (d - t)) / 2));
  // top + bottom frames
  for (const sy of [-1, 1]) {
    g.add(mesh(new THREE.BoxGeometry(w, t, t), fm, 0, (sy * (h - t)) / 2, (d - t) / 2));
    g.add(mesh(new THREE.BoxGeometry(w, t, t), fm, 0, (sy * (h - t)) / 2, -(d - t) / 2));
    g.add(mesh(new THREE.BoxGeometry(t, t, d - 2 * t), fm, (w - t) / 2, (sy * (h - t)) / 2, 0));
    g.add(mesh(new THREE.BoxGeometry(t, t, d - 2 * t), fm, -(w - t) / 2, (sy * (h - t)) / 2, 0));
  }
  // mid rail on the two wide faces
  g.add(mesh(new THREE.BoxGeometry(w, t * 0.8, t * 0.7), fm, 0, 0, (d - t) / 2));
  g.add(mesh(new THREE.BoxGeometry(w, t * 0.8, t * 0.7), fm, 0, 0, -(d - t) / 2));
  // paper core: text faces on the 4 sides, plain top/bottom — order +x,-x,+y,-y,+z,-z
  const core = new THREE.Mesh(
    new THREE.BoxGeometry(w - 0.02, h - 0.02, d - 0.02),
    [side, side, plain, plain, side, side],
  );
  core.castShadow = false;
  core.receiveShadow = true;
  g.add(core);
}

function buildAndon(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const w = 0.36;
  const h = 0.52;
  const d = 0.28;
  framedBox(o, w, h, d, g);
  const fm = frameMat(o);
  g.add(mesh(new THREE.BoxGeometry(w + 0.04, 0.03, d + 0.04), fm, 0, h / 2 + 0.015, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.015, 0.015, 0.07, 8), fm, 0, h / 2 + 0.06, 0));
  return finish(g, o.size, h / 2 + 0.1, -h / 2);
}

function buildKiriko(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const w = 0.3;
  const h = 0.95;
  const d = 0.3;
  framedBox(o, w, h, d, g);
  const fm = frameMat(o);
  for (const sx of [-1, 1]) for (const sz of [-1, 1])
    g.add(mesh(new THREE.BoxGeometry(0.05, 0.07, 0.05), fm, (sx * (w - 0.05)) / 2, -h / 2 - 0.035, (sz * (d - 0.05)) / 2));
  g.add(mesh(new THREE.BoxGeometry(w + 0.06, 0.045, d + 0.06), fm, 0, h / 2 + 0.022, 0));
  g.add(mesh(new THREE.SphereGeometry(0.045, 12, 10), fm, 0, h / 2 + 0.08, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.015, 0.015, 0.08, 8), fm, 0, h / 2 + 0.14, 0));
  return finish(g, o.size, h / 2 + 0.18, -h / 2 - 0.07);
}

function buildAkari(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const { plain } = paperMats(o, false);
  const fm = frameMat(o);
  const globe = mesh(new THREE.SphereGeometry(0.26, 22, 16), plain, 0, 0.36, 0);
  globe.castShadow = false;
  g.add(globe);
  const ring = mesh(new THREE.TorusGeometry(0.1, 0.018, 8, 18), fm, 0, 0.13, 0);
  ring.rotation.x = Math.PI / 2;
  g.add(ring);
  for (let i = 0; i < 3; i++) {
    const a = (i / 3) * Math.PI * 2;
    const leg = mesh(new THREE.CylinderGeometry(0.014, 0.014, 0.3, 6), fm, Math.cos(a) * 0.14, 0.07, Math.sin(a) * 0.14);
    leg.rotation.z = Math.cos(a) * 0.5;
    leg.rotation.x = -Math.sin(a) * 0.5;
    g.add(leg);
  }
  return finish(g, o.size, 0.64, 0);
}

function buildToro(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const stone = new THREE.MeshStandardMaterial({ color: '#9aa0a3', roughness: 0.92 });
  const stoneDark = new THREE.MeshStandardMaterial({ color: '#7e8489', roughness: 0.95 });
  const glowMat = new THREE.MeshStandardMaterial({
    color: '#ffd9a0', emissive: '#ffc27d', emissiveIntensity: Math.max(0.15, o.glow), roughness: 0.9,
  });
  // base (kiso), pillar (sao), platform (chūdai), fire box (hibukuro), roof (kasa), jewel (hōju)
  g.add(mesh(new THREE.BoxGeometry(0.6, 0.08, 0.6), stoneDark, 0, 0.04, 0));
  g.add(mesh(new THREE.BoxGeometry(0.5, 0.08, 0.5), stone, 0, 0.12, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.1, 0.13, 0.5, 12), stone, 0, 0.39, 0));
  g.add(mesh(new THREE.BoxGeometry(0.44, 0.1, 0.44), stone, 0, 0.69, 0));
  g.add(mesh(new THREE.BoxGeometry(0.32, 0.28, 0.32), stone, 0, 0.88, 0));
  const win = new THREE.PlaneGeometry(0.15, 0.17);
  for (const [x, z, ry] of [[0, 0.161, 0], [0, -0.161, Math.PI], [0.161, 0, Math.PI / 2], [-0.161, 0, -Math.PI / 2]] as const) {
    const w = mesh(win, glowMat, x, 0.88, z);
    w.rotation.y = ry;
    w.castShadow = false;
    g.add(w);
  }
  const kasa = tiledKasa(0.36, 0.24, 4, stoneDark);
  kasa.position.y = 1.14;
  g.add(kasa);
  g.add(mesh(new THREE.SphereGeometry(0.07, 12, 10), stone, 0, 1.3, 0));
  return finish(g, o.size, 1.38, 0);
}

function buildKakuChochin(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const { side } = paperMats(o, true);
  const fm = frameMat(o);
  const core = mesh(new THREE.BoxGeometry(0.3, 0.52, 0.3), side);
  core.castShadow = false;
  g.add(core);
  g.add(mesh(new THREE.BoxGeometry(0.34, 0.045, 0.34), fm, 0, 0.28, 0));
  g.add(mesh(new THREE.BoxGeometry(0.34, 0.045, 0.34), fm, 0, -0.28, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.016, 0.016, 0.08, 8), fm, 0, 0.34, 0));
  return finish(g, o.size, 0.38, -0.31);
}

function buildBonbori(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const { side } = paperMats(o, true);
  const fm = frameMat(o);
  const body = mesh(new THREE.CylinderGeometry(0.19, 0.15, 0.5, 6, 1, true), side);
  body.castShadow = false;
  g.add(body);
  g.add(mesh(new THREE.CylinderGeometry(0.2, 0.2, 0.045, 6), fm, 0, 0.27, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.16, 0.16, 0.04, 6), fm, 0, -0.27, 0));
  g.add(mesh(new THREE.SphereGeometry(0.035, 10, 8), fm, 0, -0.31, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.016, 0.016, 0.08, 8), fm, 0, 0.33, 0));
  return finish(g, o.size, 0.37, -0.34);
}

function buildGongdeng(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const tex = gongdengTexture(o.paperColor, o.text);
  const panel = new THREE.MeshStandardMaterial({
    map: tex, color: '#ffffff', emissive: '#ffffff', emissiveMap: tex,
    emissiveIntensity: o.glow, roughness: 0.85, side: THREE.DoubleSide,
  });
  const fm = frameMat(o);
  const core = mesh(new THREE.BoxGeometry(0.26, 0.5, 0.26), panel);
  core.castShadow = false;
  g.add(core);
  for (const sx of [-1, 1]) for (const sz of [-1, 1])
    g.add(mesh(new THREE.BoxGeometry(0.045, 0.56, 0.045), fm, sx * 0.15, 0, sz * 0.15));
  g.add(mesh(new THREE.BoxGeometry(0.36, 0.05, 0.36), fm, 0, 0.3, 0));
  g.add(mesh(new THREE.BoxGeometry(0.36, 0.05, 0.36), fm, 0, -0.3, 0));
  const dome = mesh(new THREE.ConeGeometry(0.13, 0.12, 4), fm, 0, 0.385, 0);
  dome.rotation.y = Math.PI / 4;
  g.add(dome);
  g.add(mesh(new THREE.SphereGeometry(0.035, 10, 8), fm, 0, 0.46, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.014, 0.014, 0.08, 8), fm, 0, 0.52, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.012, 0.012, 0.1, 6), fm, 0, -0.37, 0));
  if (o.tassels === false) return finish(g, o.size, 0.56, -0.42);
  const tassel = buildTassel(o, 0.3, 0.05);
  tassel.position.y = -0.42;
  g.add(tassel);
  return finish(g, o.size, 0.56, -0.72);
}

function buildZoumadeng(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const fm = frameMat(o);
  g.add(mesh(new THREE.CylinderGeometry(0.24, 0.24, 0.045, 20), fm, 0, 0.27, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.24, 0.24, 0.045, 20), fm, 0, -0.27, 0));
  for (let i = 0; i < 6; i++) {
    const a = (i / 6) * Math.PI * 2;
    g.add(mesh(new THREE.BoxGeometry(0.03, 0.5, 0.03), fm, Math.cos(a) * 0.225, 0, Math.sin(a) * 0.225));
  }
  g.add(mesh(new THREE.CylinderGeometry(0.016, 0.016, 0.09, 8), fm, 0, 0.33, 0));
  // spinning inner shade with paper-cut horses + heat impeller
  const spin = new THREE.Group();
  const tex = horseTexture();
  const shade = new THREE.Mesh(
    new THREE.CylinderGeometry(0.17, 0.17, 0.42, 24, 1, true),
    new THREE.MeshStandardMaterial({
      map: tex, color: '#ffffff', emissive: '#ffffff', emissiveMap: tex,
      emissiveIntensity: o.glow, roughness: 0.9, side: THREE.DoubleSide,
    }),
  );
  shade.castShadow = false;
  spin.add(shade);
  for (let i = 0; i < 4; i++) {
    const bl = mesh(new THREE.BoxGeometry(0.2, 0.015, 0.07), fm, 0, 0.24, 0);
    bl.rotation.y = (i * Math.PI) / 2;
    bl.rotation.x = 0.35;
    spin.add(bl);
  }
  spin.add(mesh(new THREE.ConeGeometry(0.04, 0.08, 8), fm, 0, 0.28, 0));
  spin.userData.spin = 0.6;
  g.add(spin);
  const candle = mesh(
    new THREE.CylinderGeometry(0.025, 0.03, 0.09, 8),
    new THREE.MeshStandardMaterial({ color: '#ffd9a0', emissive: '#ffb45e', emissiveIntensity: Math.max(0.4, o.glow) }),
    0, -0.2, 0,
  );
  candle.castShadow = false;
  g.add(candle);
  return finish(g, o.size, 0.38, -0.3);
}

function buildChorong(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const tex = chorongTexture(o.text);
  const mat = new THREE.MeshStandardMaterial({
    map: tex, color: '#ffffff', emissive: '#ffffff', emissiveMap: tex,
    emissiveIntensity: o.glow, roughness: 0.85, side: THREE.DoubleSide,
  });
  const fm = frameMat(o);
  const pts = [
    [0.12, -0.3], [0.18, -0.24], [0.205, -0.08], [0.205, 0.08], [0.18, 0.24], [0.12, 0.3],
  ].map(([x, y]) => new THREE.Vector2(x, y));
  const body = mesh(new THREE.LatheGeometry(pts, 22), mat);
  body.castShadow = false;
  g.add(body);
  g.add(mesh(new THREE.CylinderGeometry(0.13, 0.13, 0.035, 18), fm, 0, 0.315, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.13, 0.13, 0.035, 18), fm, 0, -0.315, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.018, 0.018, 0.09, 8), fm, 0, 0.37, 0));
  return finish(g, o.size, 0.41, -0.34);
}

function buildKasugaToro(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const stone = new THREE.MeshStandardMaterial({ color: '#9aa0a3', roughness: 0.92 });
  const stoneDark = new THREE.MeshStandardMaterial({ color: '#7e8489', roughness: 0.95 });
  const glowMat = new THREE.MeshStandardMaterial({
    color: '#ffd9a0', emissive: '#ffc27d', emissiveIntensity: Math.max(0.15, o.glow), roughness: 0.9,
  });
  // tall hexagonal pedestal lantern: base, shaft, platform, latticed fire box, kasa, jewel
  g.add(mesh(new THREE.CylinderGeometry(0.34, 0.38, 0.07, 6), stoneDark, 0, 0.035, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.26, 0.3, 0.07, 6), stone, 0, 0.105, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.09, 0.11, 0.66, 6), stone, 0, 0.47, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.2, 0.2, 0.09, 6), stone, 0, 0.845, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.16, 0.16, 0.3, 6), stone, 0, 1.04, 0));
  const win = new THREE.PlaneGeometry(0.12, 0.18);
  for (let i = 0; i < 6; i++) {
    const a = Math.PI / 6 + (i / 6) * Math.PI * 2; // hex face centers
    const r = 0.141; // apothem + hair
    const w = mesh(win, glowMat, Math.sin(a) * r, 1.04, Math.cos(a) * r);
    w.rotation.y = a;
    w.castShadow = false;
    g.add(w);
    const bv = mesh(new THREE.BoxGeometry(0.016, 0.18, 0.012), stoneDark, Math.sin(a) * (r + 0.005), 1.04, Math.cos(a) * (r + 0.005));
    bv.rotation.y = a;
    g.add(bv);
    const bh = mesh(new THREE.BoxGeometry(0.12, 0.016, 0.012), stoneDark, Math.sin(a) * (r + 0.005), 1.04, Math.cos(a) * (r + 0.005));
    bh.rotation.y = a;
    g.add(bh);
  }
  const kasa = tiledKasa(0.3, 0.2, 6, stoneDark);
  kasa.position.y = 1.29;
  g.add(kasa);
  g.add(mesh(new THREE.SphereGeometry(0.06, 12, 10), stone, 0, 1.43, 0));
  return finish(g, o.size, 1.5, 0);
}

function stoneMats(glow: number) {
  return {
    stone: new THREE.MeshStandardMaterial({ color: '#9aa0a3', roughness: 0.92 }),
    stoneDark: new THREE.MeshStandardMaterial({ color: '#7e8489', roughness: 0.95 }),
    glow: new THREE.MeshStandardMaterial({ color: '#ffd9a0', emissive: '#ffc27d', emissiveIntensity: Math.max(0.15, glow), roughness: 0.9 }),
  };
}

function buildYukimiToro(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const { stone, stoneDark, glow } = stoneMats(o.glow);
  // squat snow-viewing lantern on legs with a broad snow-catching kasa
  for (const sx of [-1, 1]) for (const sz of [-1, 1])
    g.add(mesh(new THREE.CylinderGeometry(0.05, 0.06, 0.22, 8), stoneDark, sx * 0.16, 0.11, sz * 0.16));
  g.add(mesh(new THREE.BoxGeometry(0.5, 0.08, 0.5), stone, 0, 0.26, 0));
  g.add(mesh(new THREE.BoxGeometry(0.34, 0.24, 0.34), stone, 0, 0.42, 0));
  const win = new THREE.PlaneGeometry(0.17, 0.15);
  for (const [x, z, ry] of [[0, 0.171, 0], [0, -0.171, Math.PI], [0.171, 0, Math.PI / 2], [-0.171, 0, -Math.PI / 2]] as const) {
    const w = mesh(win, glow, x, 0.42, z);
    w.rotation.y = ry;
    w.castShadow = false;
    g.add(w);
  }
  const kasa = tiledKasa(0.46, 0.16, 6, stoneDark);
  kasa.position.y = 0.62;
  g.add(kasa);
  g.add(mesh(new THREE.SphereGeometry(0.06, 12, 10), stone, 0, 0.74, 0));
  return finish(g, o.size, 0.8, 0);
}

function buildOribeToro(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const { stone, stoneDark, glow } = stoneMats(o.glow);
  // ikekomi: post buried directly, no base; moon windows; ~1.38 m
  g.add(mesh(new THREE.CylinderGeometry(0.14, 0.17, 0.55, 10), stone, 0, 0.275, 0));
  // subtle cross relief (kakure-kirishitan trademark of the Oribe type)
  g.add(mesh(new THREE.BoxGeometry(0.025, 0.13, 0.012), stoneDark, 0, 0.32, 0.155));
  g.add(mesh(new THREE.BoxGeometry(0.075, 0.025, 0.012), stoneDark, 0, 0.34, 0.155));
  g.add(mesh(new THREE.BoxGeometry(0.4, 0.08, 0.4), stone, 0, 0.59, 0));
  g.add(mesh(new THREE.BoxGeometry(0.3, 0.26, 0.3), stone, 0, 0.76, 0));
  // front/rear square windows
  const sq = new THREE.PlaneGeometry(0.14, 0.15);
  for (const [z, ry] of [[0.151, 0], [-0.151, Math.PI]] as const) {
    const w = mesh(sq, glow, 0, 0.76, z);
    w.rotation.y = ry;
    w.castShadow = false;
    g.add(w);
  }
  // full moon (right) + crescent moon (left)
  const moon = new THREE.CircleGeometry(0.07, 20);
  const full = mesh(moon, glow, 0.151, 0.76, 0);
  full.rotation.y = Math.PI / 2;
  full.castShadow = false;
  g.add(full);
  const cres = mesh(moon, glow, -0.151, 0.76, 0);
  cres.rotation.y = -Math.PI / 2;
  cres.castShadow = false;
  g.add(cres);
  const bite = mesh(new THREE.CircleGeometry(0.07, 20), stone, -0.156, 0.775, 0.03);
  bite.rotation.y = -Math.PI / 2;
  g.add(bite);
  const kasa = tiledKasa(0.3, 0.18, 4, stoneDark);
  kasa.position.y = 0.98;
  g.add(kasa);
  g.add(mesh(new THREE.SphereGeometry(0.05, 10, 8), stone, 0, 1.1, 0));
  return finish(g, o.size, 1.16, 0);
}

function buildOkiToro(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const { stone, stoneDark, glow } = stoneMats(o.glow);
  // movable: no post, rests directly on the ground
  g.add(mesh(new THREE.BoxGeometry(0.3, 0.08, 0.3), stoneDark, 0, 0.04, 0));
  g.add(mesh(new THREE.BoxGeometry(0.24, 0.2, 0.24), stone, 0, 0.18, 0));
  const win = new THREE.PlaneGeometry(0.12, 0.12);
  for (const [x, z, ry] of [[0, 0.121, 0], [0, -0.121, Math.PI], [0.121, 0, Math.PI / 2], [-0.121, 0, -Math.PI / 2]] as const) {
    const w = mesh(win, glow, x, 0.18, z);
    w.rotation.y = ry;
    w.castShadow = false;
    g.add(w);
  }
  const kasa = tiledKasa(0.24, 0.14, 4, stoneDark);
  kasa.position.y = 0.35;
  g.add(kasa);
  g.add(mesh(new THREE.SphereGeometry(0.045, 10, 8), stone, 0, 0.45, 0));
  return finish(g, o.size, 0.5, 0);
}

function buildRankeiToro(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const { stone, stoneDark, glow } = stoneMats(o.glow);
  // tsuri-dōrō: hangs from eaves by a ring; firebox + bottom knob
  g.add(mesh(new THREE.TorusGeometry(0.05, 0.012, 8, 16), stoneDark, 0, 0.34, 0));
  g.add(mesh(new THREE.BoxGeometry(0.26, 0.05, 0.26), stoneDark, 0, 0.25, 0));
  g.add(mesh(new THREE.BoxGeometry(0.2, 0.24, 0.2), stone, 0, 0.105, 0));
  const win = new THREE.PlaneGeometry(0.1, 0.14);
  for (const [x, z, ry] of [[0, 0.101, 0], [0, -0.101, Math.PI], [0.101, 0, Math.PI / 2], [-0.101, 0, -Math.PI / 2]] as const) {
    const w = mesh(win, glow, x, 0.1, z);
    w.rotation.y = ry;
    w.castShadow = false;
    g.add(w);
  }
  g.add(mesh(new THREE.BoxGeometry(0.24, 0.04, 0.24), stoneDark, 0, -0.04, 0));
  g.add(mesh(new THREE.SphereGeometry(0.035, 10, 8), stone, 0, -0.08, 0));
  return finish(g, o.size, 0.4, -0.12);
}

function buildPagodaToro(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const { stone, stoneDark, glow } = stoneMats(o.glow);
  // cast-concrete tiered pagoda lantern with a light chamber
  g.add(mesh(new THREE.BoxGeometry(0.44, 0.1, 0.44), stoneDark, 0, 0.05, 0));
  g.add(mesh(new THREE.BoxGeometry(0.3, 0.26, 0.3), stone, 0, 0.23, 0));
  const win = new THREE.PlaneGeometry(0.13, 0.15);
  for (const [x, z, ry] of [[0, 0.151, 0], [0, -0.151, Math.PI], [0.151, 0, Math.PI / 2], [-0.151, 0, -Math.PI / 2]] as const) {
    const w = mesh(win, glow, x, 0.23, z);
    w.rotation.y = ry;
    w.castShadow = false;
    g.add(w);
  }
  const roof1 = tiledKasa(0.34, 0.16, 4, stoneDark);
  roof1.position.y = 0.44;
  g.add(roof1);
  g.add(mesh(new THREE.BoxGeometry(0.22, 0.18, 0.22), stone, 0, 0.61, 0));
  const roof2 = tiledKasa(0.26, 0.13, 4, stoneDark);
  roof2.position.y = 0.765;
  g.add(roof2);
  g.add(mesh(new THREE.BoxGeometry(0.15, 0.13, 0.15), stone, 0, 0.895, 0));
  const roof3 = tiledKasa(0.19, 0.11, 4, stoneDark);
  roof3.position.y = 1.015;
  g.add(roof3);
  g.add(mesh(new THREE.CylinderGeometry(0.02, 0.02, 0.16, 8), stone, 0, 1.14, 0));
  for (const ry of [1.09, 1.14, 1.19]) {
    const ring = mesh(new THREE.TorusGeometry(0.035, 0.01, 6, 14), stoneDark, 0, ry, 0);
    ring.rotation.x = Math.PI / 2;
    g.add(ring);
  }
  g.add(mesh(new THREE.SphereGeometry(0.025, 10, 8), stone, 0, 1.23, 0));
  return finish(g, o.size, 1.26, 0);
}

function buildConcreteBollard(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const concrete = new THREE.MeshStandardMaterial({ color: '#b9bcbe', roughness: 0.95 });
  const slit = new THREE.MeshStandardMaterial({ color: '#ffe7c0', emissive: '#ffc98a', emissiveIntensity: Math.max(0.2, o.glow), roughness: 0.8 });
  g.add(mesh(new THREE.BoxGeometry(0.22, 0.9, 0.22), concrete, 0, 0.45, 0));
  g.add(mesh(new THREE.BoxGeometry(0.225, 0.07, 0.225), slit, 0, 0.68, 0));
  g.add(mesh(new THREE.BoxGeometry(0.26, 0.05, 0.26), concrete, 0, 0.925, 0));
  return finish(g, o.size, 0.95, 0);
}

// ---------- hanging-lantern furniture: tassels, rib lines, tiled stone kasa ----------

let tasselCount = 0;

/** Swaying festival tassel (fángsuì 房穗): knot + wrapped head + strand skirt. Origin = hang point. */
function buildTassel(o: LampOpts, len = 0.34, r = 0.045): THREE.Group {
  const t = new THREE.Group();
  const cord = frameMat(o);
  const thread = new THREE.MeshStandardMaterial({ color: '#b32b1e', roughness: 0.9 });
  t.add(mesh(new THREE.SphereGeometry(0.022, 10, 8), cord, 0, -0.02, 0));
  t.add(mesh(new THREE.CylinderGeometry(0.026, 0.032, 0.06, 10), thread, 0, -0.07, 0));
  const skirtLen = len - 0.12;
  const bell = mesh(new THREE.CylinderGeometry(0.03, r, skirtLen, 12, 1, true), thread, 0, -0.1 - skirtLen / 2, 0);
  bell.castShadow = false;
  t.add(bell);
  for (let i = 0; i < 10; i++) {
    const a = (i / 10) * Math.PI * 2;
    const s = mesh(
      new THREE.CylinderGeometry(0.005, 0.004, skirtLen, 5), thread,
      Math.cos(a) * r * 0.7, -0.1 - skirtLen / 2, Math.sin(a) * r * 0.7,
    );
    s.rotation.z = Math.cos(a) * 0.12;
    s.rotation.x = -Math.sin(a) * 0.12;
    s.castShadow = false;
    t.add(s);
  }
  t.add(mesh(new THREE.SphereGeometry(0.014, 8, 6), cord, 0, -len + 0.02, 0));
  t.userData.swaySpeed = 1.3;
  t.userData.swayPhase = (tasselCount++ * 1.7) % (Math.PI * 2);
  return t;
}

/** Bamboo rib lines following a lathe profile (classic ribbed-lantern look). */
function profileRibs(pts: [number, number][], mat: THREE.Material, count = 10, lift = 0.006): THREE.Group {
  const ribs = new THREE.Group();
  const v = pts.map(([x, y]) => new THREE.Vector3(x + lift, y, 0));
  const geo = new THREE.TubeGeometry(new THREE.CatmullRomCurve3(v), 24, 0.006, 5);
  for (let i = 0; i < count; i++) {
    const r = new THREE.Mesh(geo, mat);
    r.rotation.y = (i / count) * Math.PI * 2;
    r.castShadow = false;
    ribs.add(r);
  }
  return ribs;
}

/** Ellipsoid profile sampler (for rib lines over spheres). */
function spherePts(r: number, squash: number, n: number): [number, number][] {
  const pts: [number, number][] = [];
  for (let i = 0; i <= n; i++) {
    const t = (i / n) * Math.PI;
    pts.push([Math.sin(t) * r, Math.cos(t) * r * squash]);
  }
  return pts;
}

/** Stone kasa (umbrella) with tile ribs, eave rim and corner nubs. Centered at origin. */
function tiledKasa(r: number, h: number, seg: number, mat: THREE.Material): THREE.Group {
  const k = new THREE.Group();
  const cone = mesh(new THREE.ConeGeometry(r, h, seg), mat);
  if (seg === 4) cone.rotation.y = Math.PI / 4;
  k.add(cone);
  const tilt = Math.atan2(r, h);
  const ribs = seg === 4 ? 4 : seg * 2;
  for (let i = 0; i < ribs; i++) {
    const yaw = seg === 4 ? -(Math.PI / 4 + (i / 4) * Math.PI * 2) : (i / ribs) * Math.PI * 2;
    const holder = new THREE.Group();
    holder.rotation.y = yaw;
    const rib = mesh(new THREE.CylinderGeometry(0.012, 0.014, Math.hypot(r, h) * 0.92, 6), mat, r * 0.5 + 0.006, 0, 0);
    rib.rotation.z = tilt;
    rib.castShadow = false;
    holder.add(rib);
    k.add(holder);
  }
  if (seg === 4) {
    const rim = mesh(new THREE.BoxGeometry(r * 1.45, 0.03, r * 1.45), mat, 0, -h / 2, 0);
    rim.rotation.y = Math.PI / 4;
    k.add(rim);
    for (let i = 0; i < 4; i++) {
      const a = Math.PI / 4 + (i / 4) * Math.PI * 2;
      k.add(mesh(new THREE.SphereGeometry(0.02, 8, 6), mat, Math.sin(a) * r * 0.99, -h / 2 + 0.02, Math.cos(a) * r * 0.99));
    }
  } else {
    const rim = mesh(new THREE.TorusGeometry(r * 0.97, 0.014, 6, seg * 4), mat, 0, -h / 2, 0);
    rim.rotation.x = Math.PI / 2;
    k.add(rim);
  }
  return k;
}

function palacePanel(o: LampOpts): THREE.MeshStandardMaterial {
  const tex = gongdengTexture(o.paperColor, o.text);
  return new THREE.MeshStandardMaterial({
    map: tex, color: '#ffffff', emissive: '#ffffff', emissiveMap: tex,
    emissiveIntensity: o.glow, roughness: 0.85, side: THREE.DoubleSide,
  });
}

function buildDiscLantern(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const { side } = paperMats(o, true);
  const fm = frameMat(o);
  const pts: [number, number][] = [[0.07, -0.13], [0.2, -0.11], [0.29, -0.04], [0.31, 0.03], [0.24, 0.1], [0.1, 0.13]];
  const body = mesh(new THREE.LatheGeometry(pts.map(([x, y]) => new THREE.Vector2(x, y)), 26), side);
  body.castShadow = false;
  g.add(body);
  g.add(profileRibs(pts, fm, 12));
  g.add(mesh(new THREE.CylinderGeometry(0.11, 0.11, 0.04, 16), fm, 0, 0.15, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.08, 0.08, 0.04, 14), fm, 0, -0.15, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.016, 0.016, 0.08, 8), fm, 0, 0.2, 0));
  return finish(g, o.size, 0.24, -0.17);
}

function buildMelonLantern(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const { side } = paperMats(o, true);
  const fm = frameMat(o);
  const pts: [number, number][] = [[0.02, -0.3], [0.12, -0.24], [0.19, -0.12], [0.21, 0.02], [0.17, 0.16], [0.09, 0.26], [0.02, 0.3]];
  const body = mesh(new THREE.LatheGeometry(pts.map(([x, y]) => new THREE.Vector2(x, y)), 24), side);
  body.castShadow = false;
  g.add(body);
  g.add(profileRibs(pts, fm, 12));
  g.add(mesh(new THREE.CylinderGeometry(0.05, 0.05, 0.04, 12), fm, 0, 0.31, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.05, 0.05, 0.04, 12), fm, 0, -0.31, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.014, 0.014, 0.08, 8), fm, 0, 0.36, 0));
  return finish(g, o.size, 0.4, -0.33);
}

function buildBarrelLantern(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const { side } = paperMats(o, true);
  const fm = frameMat(o);
  const pts: [number, number][] = [[0.13, -0.28], [0.19, -0.22], [0.215, -0.08], [0.215, 0.08], [0.19, 0.22], [0.13, 0.28]];
  const body = mesh(new THREE.LatheGeometry(pts.map(([x, y]) => new THREE.Vector2(x, y)), 24), side);
  body.castShadow = false;
  g.add(body);
  g.add(profileRibs(pts, fm, 12));
  for (const by of [-0.12, 0.12]) {
    const band = mesh(new THREE.TorusGeometry(0.216, 0.01, 6, 26), fm, 0, by, 0);
    band.rotation.x = Math.PI / 2;
    g.add(band);
  }
  g.add(mesh(new THREE.CylinderGeometry(0.14, 0.14, 0.04, 16), fm, 0, 0.3, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.14, 0.14, 0.04, 16), fm, 0, -0.3, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.016, 0.016, 0.08, 8), fm, 0, 0.35, 0));
  return finish(g, o.size, 0.39, -0.32);
}

function buildGourdLantern(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const { side } = paperMats(o, true);
  const fm = frameMat(o);
  const pts: [number, number][] = [
    [0.05, -0.3], [0.14, -0.27], [0.19, -0.18], [0.185, -0.09], [0.1, -0.02],
    [0.075, 0.03], [0.11, 0.09], [0.135, 0.17], [0.11, 0.25], [0.05, 0.29],
  ];
  const body = mesh(new THREE.LatheGeometry(pts.map(([x, y]) => new THREE.Vector2(x, y)), 22), side);
  body.castShadow = false;
  g.add(body);
  g.add(profileRibs(pts, fm, 8));
  const waist = mesh(new THREE.TorusGeometry(0.078, 0.012, 6, 20), fm, 0, 0.03, 0);
  waist.rotation.x = Math.PI / 2;
  g.add(waist);
  g.add(mesh(new THREE.CylinderGeometry(0.06, 0.06, 0.04, 12), fm, 0, 0.3, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.06, 0.06, 0.04, 12), fm, 0, -0.31, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.014, 0.014, 0.08, 8), fm, 0, 0.35, 0));
  return finish(g, o.size, 0.39, -0.33);
}

function buildHexPalace(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const panel = palacePanel(o);
  const fm = frameMat(o);
  const core = mesh(new THREE.CylinderGeometry(0.21, 0.24, 0.44, 6, 1, true), panel);
  core.castShadow = false;
  g.add(core);
  for (let i = 0; i < 6; i++) {
    const a = (i / 6) * Math.PI * 2 + Math.PI / 6;
    g.add(mesh(new THREE.BoxGeometry(0.04, 0.5, 0.04), fm, Math.cos(a) * 0.222, 0, Math.sin(a) * 0.222));
  }
  g.add(mesh(new THREE.CylinderGeometry(0.226, 0.226, 0.035, 6), fm, 0, 0, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.27, 0.27, 0.05, 6), fm, 0, 0.245, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.27, 0.27, 0.05, 6), fm, 0, -0.245, 0));
  g.add(mesh(new THREE.ConeGeometry(0.1, 0.1, 6), fm, 0, 0.32, 0));
  g.add(mesh(new THREE.SphereGeometry(0.032, 10, 8), fm, 0, 0.385, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.014, 0.014, 0.08, 8), fm, 0, 0.44, 0));
  return finish(g, o.size, 0.48, -0.27);
}

function buildDiamondGongdeng(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const panel = palacePanel(o);
  const fm = frameMat(o);
  const top = mesh(new THREE.CylinderGeometry(0.05, 0.23, 0.24, 8), panel, 0, 0.12, 0);
  const bot = mesh(new THREE.CylinderGeometry(0.23, 0.05, 0.24, 8), panel, 0, -0.12, 0);
  top.castShadow = false;
  bot.castShadow = false;
  g.add(top, bot);
  // gilt edge bars from the apexes to the equator vertices
  const barLen = Math.hypot(0.23, 0.24);
  const barTilt = Math.atan2(0.23, 0.24);
  for (let i = 0; i < 8; i++) {
    const yaw = (i / 8) * Math.PI * 2;
    for (const s of [1, -1]) {
      const holder = new THREE.Group();
      holder.rotation.y = yaw;
      const bar = mesh(new THREE.BoxGeometry(0.02, barLen, 0.02), fm, 0.126, s * 0.128, 0);
      bar.rotation.z = s > 0 ? barTilt : -barTilt;
      bar.castShadow = false;
      holder.add(bar);
      g.add(holder);
    }
  }
  const band = mesh(new THREE.TorusGeometry(0.23, 0.014, 6, 8), fm, 0, 0, 0);
  band.rotation.x = Math.PI / 2;
  g.add(band);
  g.add(mesh(new THREE.CylinderGeometry(0.06, 0.06, 0.04, 8), fm, 0, 0.25, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.06, 0.06, 0.04, 8), fm, 0, -0.25, 0));
  g.add(mesh(new THREE.SphereGeometry(0.03, 10, 8), fm, 0, 0.29, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.014, 0.014, 0.08, 8), fm, 0, 0.34, 0));
  return finish(g, o.size, 0.38, -0.27);
}

function buildRoofChochin(o: LampOpts): BuiltLamp {
  const g = new THREE.Group();
  const { side } = paperMats(o, true);
  const fm = frameMat(o);
  const body = mesh(new THREE.SphereGeometry(0.26, 24, 18), side);
  body.castShadow = false;
  g.add(body);
  g.add(profileRibs(spherePts(0.26, 1, 12), fm, 12));
  g.add(mesh(new THREE.CylinderGeometry(0.09, 0.1, 0.04, 14), fm, 0, -0.26, 0));
  // mini tiled roof cap (festival crest lantern)
  const tile = new THREE.MeshStandardMaterial({ color: '#3a3f45', roughness: 0.85 });
  g.add(mesh(new THREE.BoxGeometry(0.3, 0.03, 0.3), fm, 0, 0.27, 0));
  const cap = mesh(new THREE.ConeGeometry(0.24, 0.16, 4), tile, 0, 0.365, 0);
  cap.rotation.y = Math.PI / 4;
  g.add(cap);
  const tilt = Math.atan2(0.24, 0.16);
  for (let i = 0; i < 4; i++) {
    const holder = new THREE.Group();
    holder.rotation.y = -(Math.PI / 4 + (i / 4) * Math.PI * 2);
    const rib = mesh(new THREE.CylinderGeometry(0.008, 0.008, Math.hypot(0.24, 0.16) * 0.92, 6), tile, 0.126, 0.365, 0);
    rib.rotation.z = tilt;
    rib.castShadow = false;
    holder.add(rib);
    g.add(holder);
  }
  g.add(mesh(new THREE.BoxGeometry(0.05, 0.05, 0.2), tile, 0, 0.45, 0));
  g.add(mesh(new THREE.SphereGeometry(0.028, 10, 8), fm, 0, 0.49, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.014, 0.014, 0.08, 8), fm, 0, 0.54, 0));
  return finish(g, o.size, 0.58, -0.28);
}

function buildLampInner(design: LampDesign, o: LampOpts): BuiltLamp {
  switch (design) {
    case 'chochin-tube': return buildChochinTube(o);
    case 'chochin-round': return buildChochinRound(o);
    case 'kaku-chochin': return buildKakuChochin(o);
    case 'andon': return buildAndon(o);
    case 'kiriko': return buildKiriko(o);
    case 'bonbori': return buildBonbori(o);
    case 'gongdeng': return buildGongdeng(o);
    case 'zoumadeng': return buildZoumadeng(o);
    case 'chorong': return buildChorong(o);
    case 'akari': return buildAkari(o);
    case 'toro': return buildToro(o);
    case 'kasuga-toro': return buildKasugaToro(o);
    case 'yukimi-toro': return buildYukimiToro(o);
    case 'oribe-toro': return buildOribeToro(o);
    case 'oki-toro': return buildOkiToro(o);
    case 'rankei-toro': return buildRankeiToro(o);
    case 'pagoda-toro': return buildPagodaToro(o);
    case 'concrete-bollard': return buildConcreteBollard(o);
    case 'disc-lantern': return buildDiscLantern(o);
    case 'melon-lantern': return buildMelonLantern(o);
    case 'barrel-lantern': return buildBarrelLantern(o);
    case 'gourd-lantern': return buildGourdLantern(o);
    case 'hex-palace': return buildHexPalace(o);
    case 'diamond-gongdeng': return buildDiamondGongdeng(o);
    case 'roof-chochin': return buildRoofChochin(o);
  }
}

/** Paper hanging designs that get festival tassels + a top ring (gongdeng builds its own). */
const TASSELED: LampDesign[] = [
  'chochin-tube', 'chochin-round', 'kaku-chochin', 'chorong', 'zoumadeng',
  'disc-lantern', 'melon-lantern', 'barrel-lantern', 'gourd-lantern',
  'hex-palace', 'diamond-gongdeng', 'roof-chochin',
];

export function buildLamp(design: LampDesign, o: LampOpts): BuiltLamp {
  const b = buildLampInner(design, o);
  if (o.tassels === false || !TASSELED.includes(design)) return b;
  const len = 0.34;
  const t = buildTassel(o, len);
  t.position.y = b.baseY / o.size; // children live in unit space (group carries xsize)
  b.group.add(t);
  const ring = mesh(new THREE.TorusGeometry(0.028, 0.008, 6, 14), frameMat(o), 0, b.topY / o.size + 0.02, 0);
  b.group.add(ring);
  return { group: b.group, topY: b.topY + 0.056 * o.size, baseY: b.baseY - len * o.size };
}

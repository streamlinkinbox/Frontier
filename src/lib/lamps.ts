import * as THREE from 'three';
import { LampDesign } from './types';

export interface LampOpts {
  size: number;
  paperColor: string;
  frameColor: string;
  glow: number;
  text: string;
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
  g.add(mesh(new THREE.BoxGeometry(0.52, 0.14, 0.52), stoneDark, 0, 0.07, 0));
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
  const roof = mesh(new THREE.ConeGeometry(0.36, 0.24, 4), stoneDark, 0, 1.14, 0);
  roof.rotation.y = Math.PI / 4;
  g.add(roof);
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
  const tassel = mesh(new THREE.ConeGeometry(0.05, 0.15, 10), fm, 0, -0.49, 0);
  tassel.rotation.x = Math.PI;
  g.add(tassel);
  return finish(g, o.size, 0.56, -0.57);
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
  g.add(mesh(new THREE.CylinderGeometry(0.26, 0.3, 0.12, 6), stoneDark, 0, 0.06, 0));
  g.add(mesh(new THREE.CylinderGeometry(0.09, 0.11, 0.62, 6), stone, 0, 0.49, 0));
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
  g.add(mesh(new THREE.ConeGeometry(0.3, 0.2, 6), stoneDark, 0, 1.29, 0));
  g.add(mesh(new THREE.SphereGeometry(0.06, 12, 10), stone, 0, 1.43, 0));
  return finish(g, o.size, 1.5, 0);
}

export function buildLamp(design: LampDesign, o: LampOpts): BuiltLamp {
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
  }
}

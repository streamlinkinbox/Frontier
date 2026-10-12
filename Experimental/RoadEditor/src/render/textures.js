// Procedural PBR-ish textures painted on canvases at startup (no binary assets in git).
import * as THREE from 'three';
import { ATLAS_GRID, DECALS } from '../road/decals.js';

function canvas(w, h = w) { const c = document.createElement('canvas'); c.width = w; c.height = h; return c; }
function rand(seed) { let a = seed >>> 0; return () => { a = (a + 0x6d2b79f5) | 0; let t = Math.imul(a ^ (a >>> 15), 1 | a); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }

// Tileable value noise on a grid
function tileNoise(size, period, seed) {
  const r = rand(seed), g = new Float32Array(period * period);
  for (let i = 0; i < g.length; i++) g[i] = r();
  const out = new Float32Array(size * size);
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    const fx = x / size * period, fy = y / size * period;
    const x0 = Math.floor(fx), y0 = Math.floor(fy), tx = fx - x0, ty = fy - y0;
    const sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
    const a = g[(y0 % period) * period + (x0 % period)], b = g[(y0 % period) * period + ((x0 + 1) % period)];
    const c = g[((y0 + 1) % period) * period + (x0 % period)], d = g[((y0 + 1) % period) * period + ((x0 + 1) % period)];
    out[y * size + x] = (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
  }
  return out;
}
function fbm(size, seed, octaves = [[4, 0.5], [8, 0.25], [16, 0.15], [32, 0.1]]) {
  const out = new Float32Array(size * size);
  octaves.forEach(([p, w], k) => { const n = tileNoise(size, p, seed + k * 101); for (let i = 0; i < out.length; i++) out[i] += n[i] * w; });
  return out;
}

function toTexture(c, { repeat = 1, srgb = true, aniso = 8 } = {}) {
  const t = new THREE.CanvasTexture(c);
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.repeat.set(repeat, repeat);
  t.anisotropy = aniso;
  if (srgb) t.colorSpace = THREE.SRGBColorSpace;
  t.generateMipmaps = true;
  t.minFilter = THREE.LinearMipmapLinearFilter;
  return t;
}

function normalFromHeight(h, size, strength = 2) {
  const c = canvas(size), ctx = c.getContext('2d'), img = ctx.createImageData(size, size);
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    const l = h[y * size + ((x - 1 + size) % size)], r = h[y * size + ((x + 1) % size)];
    const u = h[((y - 1 + size) % size) * size + x], d = h[((y + 1) % size) * size + x];
    let nx = (l - r) * strength, ny = (u - d) * strength, nz = 1;
    const len = Math.hypot(nx, ny, nz); nx /= len; ny /= len; nz /= len;
    const i = (y * size + x) * 4;
    img.data[i] = (nx * 0.5 + 0.5) * 255; img.data[i + 1] = (ny * 0.5 + 0.5) * 255; img.data[i + 2] = (nz * 0.5 + 0.5) * 255; img.data[i + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
  return c;
}

// Paint pixel buffer from function(x,y,i) -> [r,g,b]
function paint(size, fn) {
  const c = canvas(size), ctx = c.getContext('2d'), img = ctx.createImageData(size, size);
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    const i = y * size + x, [r, g, b] = fn(x, y, i);
    img.data[i * 4] = r; img.data[i * 4 + 1] = g; img.data[i * 4 + 2] = b; img.data[i * 4 + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
  return c;
}

export function makeAsphalt(size = 512, tint = [1, 1, 1], seed = 1, worn = 0) {
  const r = rand(seed);
  const n = fbm(size, seed), fine = tileNoise(size, 128, seed + 9), blot = tileNoise(size, 3, seed + 3);
  const h = new Float32Array(size * size);
  const col = paint(size, (x, y, i) => {
    let v = 58 + (n[i] - 0.5) * 20 + (fine[i] - 0.5) * 26 + (blot[i] - 0.5) * 14 * (1 + worn);
    const sp = r();
    if (sp > 0.985) v += 40 * r(); else if (sp < 0.02) v -= 18;
    h[i] = fine[i] * 0.6 + n[i] * 0.4 + (sp > 0.985 ? 0.5 : 0);
    return [v * tint[0], v * tint[1], v * tint[2]];
  });
  // cracks and patches
  const ctx = col.getContext('2d');
  ctx.strokeStyle = 'rgba(15,15,16,0.55)'; ctx.lineWidth = 1.1;
  for (let k = 0; k < 5 + worn * 10; k++) {
    let x = r() * size, y = r() * size; ctx.beginPath(); ctx.moveTo(x, y);
    for (let j = 0; j < 14; j++) { x += (r() - 0.5) * 22; y += (r() - 0.5) * 22; ctx.lineTo(x, y); }
    ctx.stroke();
  }
  ctx.fillStyle = `rgba(${20 * tint[0]},${20 * tint[1]},${20 * tint[2]},0.12)`;
  for (let k = 0; k < 3; k++) { ctx.beginPath(); ctx.ellipse(r() * size, r() * size, 30 + r() * 50, 10 + r() * 20, r() * 3, 0, 6.3); ctx.fill(); }
  return { color: col, normal: normalFromHeight(h, size, 1.6) };
}

function slabs(size, cols, rows, base, jitter, joint, seed, offsetRows = false) {
  const r = rand(seed);
  const n = fbm(size, seed + 4), fine = tileNoise(size, 160, seed + 5);
  const tone = []; for (let k = 0; k < cols * rows * 2; k++) tone.push((r() - 0.5) * jitter);
  const h = new Float32Array(size * size);
  const col = paint(size, (x, y, i) => {
    const cy = Math.floor(y / size * rows);
    const off = offsetRows && cy % 2 ? 0.5 : 0;
    const fx = x / size * cols + off, fy = y / size * rows;
    const gx = fx - Math.floor(fx), gy = fy - Math.floor(fy);
    const id = (Math.floor(fx) % cols) + cy * cols;
    const edge = Math.min(gx, 1 - gx) * size / cols, edgeY = Math.min(gy, 1 - gy) * size / rows;
    const isJoint = edge < joint || edgeY < joint;
    let v = base + tone[id] + (n[i] - 0.5) * 16 + (fine[i] - 0.5) * 12;
    h[i] = isJoint ? 0 : 0.6 + fine[i] * 0.2;
    if (isJoint) v *= 0.55;
    return [v, v * 0.985, v * 0.96];
  });
  return { color: col, normal: normalFromHeight(h, size, 3) };
}

function pavers(size, seed, c1, c2) {
  // herringbone-ish brick pavers
  const r = rand(seed), fine = tileNoise(size, 128, seed);
  const h = new Float32Array(size * size);
  const n = 8; // bricks per side (2:1)
  const tones = Array.from({ length: 400 }, () => r());
  const col = paint(size, (x, y, i) => {
    const u = x / size * n, v = y / size * n * 2;
    const row = Math.floor(v), uu = u + (row % 2 ? 0.5 : 0);
    const bx = uu - Math.floor(uu), by = v - row;
    const joint = Math.min(bx, 1 - bx) * size / n < 1.6 || Math.min(by, 1 - by) * size / n / 2 < 1.6;
    const t = tones[(Math.floor(uu) * 31 + row * 17) % 400];
    const base = t < 0.5 ? c1 : c2;
    const k = 0.85 + t * 0.3 + (fine[i] - 0.5) * 0.15;
    h[i] = joint ? 0 : 0.7;
    return joint ? [70, 68, 64] : [base[0] * k, base[1] * k, base[2] * k];
  });
  return { color: col, normal: normalFromHeight(h, size, 3) };
}

function cobbles(size, seed) {
  const r = rand(seed), fine = tileNoise(size, 96, seed);
  const h = new Float32Array(size * size);
  const rows = 10, cols = 10;
  const tone = Array.from({ length: 200 }, () => r());
  const col = paint(size, (x, y, i) => {
    const v = y / size * rows, row = Math.floor(v);
    const u = x / size * cols + (row % 2) * 0.5 + Math.sin(row * 1.7) * 0.1;
    const bx = u - Math.floor(u), by = v - row;
    const dx = (bx - 0.5) * 2, dy = (by - 0.5) * 2;
    const d = Math.max(Math.abs(dx) ** 3 + Math.abs(dy) ** 3, 0) ** (1 / 3);
    const id = (Math.floor(u) * 13 + row * 7) % 200;
    const inStone = d < 0.86;
    const t = tone[id];
    const lum = 92 + t * 40 + (fine[i] - 0.5) * 22 - d * 18;
    h[i] = inStone ? (1 - d * d) * 0.9 : 0;
    return inStone ? [lum, lum * 0.97, lum * 0.93] : [52, 50, 46];
  });
  return { color: col, normal: normalFromHeight(h, size, 5) };
}

function speckle(size, seed, base, spread, grain = 1) {
  const r = rand(seed), n = fbm(size, seed), fine = tileNoise(size, 200, seed + 1);
  const h = new Float32Array(size * size);
  const col = paint(size, (x, y, i) => {
    const k = (n[i] - 0.5) * 0.5 + (fine[i] - 0.5) * 0.6 * grain + (r() - 0.5) * 0.25 * grain;
    h[i] = fine[i] * 0.7 + r() * 0.3 * grain;
    return [base[0] * (1 + k * spread), base[1] * (1 + k * spread), base[2] * (1 + k * spread)];
  });
  return { color: col, normal: normalFromHeight(h, size, 2.2) };
}

function grass(size, seed) {
  const r = rand(seed), n = fbm(size, seed), fine = tileNoise(size, 220, seed + 2);
  const col = paint(size, (x, y, i) => {
    const k = n[i], f = fine[i], q = r();
    return [62 + k * 34 + f * 14 + q * 10, 88 + k * 40 + f * 18 + q * 10, 42 + k * 16 + q * 6];
  });
  const h = fine;
  return { color: col, normal: normalFromHeight(h, size, 1.2) };
}

function tactile(size) {
  const c = canvas(size), ctx = c.getContext('2d');
  ctx.fillStyle = '#d7aa21'; ctx.fillRect(0, 0, size, size);
  const n = 8;
  for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
    const cx = (x + 0.5) / n * size, cy = (y + 0.5) / n * size;
    const g = ctx.createRadialGradient(cx - 3, cy - 3, 1, cx, cy, size / n * 0.32);
    g.addColorStop(0, '#f7d65c'); g.addColorStop(1, '#a9800f');
    ctx.fillStyle = g; ctx.beginPath(); ctx.arc(cx, cy, size / n * 0.3, 0, 7); ctx.fill();
  }
  return c;
}

function fence(size) {
  const c = canvas(size), ctx = c.getContext('2d');
  ctx.clearRect(0, 0, size, size);
  ctx.strokeStyle = '#b9bcc0'; ctx.lineWidth = 2.2;
  const n = 10;
  for (let k = -n; k <= n * 2; k++) {
    ctx.beginPath(); ctx.moveTo(k * size / n, 0); ctx.lineTo(k * size / n + size, size); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(k * size / n, 0); ctx.lineTo(k * size / n - size, size); ctx.stroke();
  }
  ctx.fillStyle = '#9da0a4'; ctx.fillRect(0, 0, size, 6); ctx.fillRect(0, size - 6, size, 6);
  return c;
}

function stoneWall(size, seed) {
  return slabs(size, 4, 8, 128, 26, 2.2, seed, true);
}

// Road-marking decal atlas
export function makeDecalAtlas(size = 2048) {
  const c = canvas(size), ctx = c.getContext('2d');
  ctx.clearRect(0, 0, size, size);
  const cell = size / ATLAS_GRID;
  const paintCell = (idx, fn) => { const x = (idx % ATLAS_GRID) * cell, y = Math.floor(idx / ATLAS_GRID) * cell; ctx.save(); ctx.translate(x, y); ctx.beginPath(); ctx.rect(0, 0, cell, cell); ctx.clip(); fn(cell); ctx.restore(); };
  const text = (idx, str, color = '#f4f4ef') => paintCell(idx, s => {
    ctx.fillStyle = color; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    ctx.font = `bold ${s * 0.36}px "Arial Narrow", Arial, sans-serif`;
    ctx.save(); ctx.translate(s / 2, s / 2); ctx.scale(Math.min(1, 0.92 * s / ctx.measureText(str).width), 2.0); ctx.fillText(str, 0, 0); ctx.restore();
  });
  text(DECALS.bus.cell, 'BUS');
  text(DECALS.only.cell, 'ONLY');
  text(DECALS.stop.cell, 'STOP');
  text(DECALS.slow.cell, 'SLOW');
  text(DECALS.tram.cell, 'TRAM');
  text(DECALS.keep.cell, 'KEEP');
  paintCell(DECALS.busStop.cell, s => {
    ctx.fillStyle = '#e7b923'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    ctx.font = `bold ${s * 0.2}px Arial, sans-serif`;
    ctx.save(); ctx.translate(s / 2, s * 0.3); ctx.scale(1, 1.8); ctx.fillText('BUS', 0, 0); ctx.restore();
    ctx.save(); ctx.translate(s / 2, s * 0.72); ctx.scale(1, 1.8); ctx.fillText('STOP', 0, 0); ctx.restore();
  });
  // bicycle pictogram
  paintCell(DECALS.bike.cell, s => {
    ctx.strokeStyle = '#f4f4ef'; ctx.lineWidth = s * 0.05; ctx.lineCap = 'round';
    const r = s * 0.17;
    ctx.beginPath(); ctx.arc(s * 0.5, s * 0.75, r, 0, 7); ctx.stroke();
    ctx.beginPath(); ctx.arc(s * 0.5, s * 0.25, r, 0, 7); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(s * 0.5, s * 0.75); ctx.lineTo(s * 0.4, s * 0.48); ctx.lineTo(s * 0.58, s * 0.42); ctx.lineTo(s * 0.5, s * 0.25); ctx.moveTo(s * 0.4, s * 0.48); ctx.lineTo(s * 0.62, s * 0.55); ctx.stroke();
    ctx.fillStyle = '#f4f4ef'; ctx.beginPath(); ctx.arc(s * 0.36, s * 0.52, s * 0.05, 0, 7); ctx.fill();
  });
  // arrows (pointing up = forward)
  const arrow = (idx, kinds) => paintCell(idx, s => {
    ctx.fillStyle = '#f4f4ef';
    const stem = (x0) => { ctx.fillRect(x0 - s * 0.05, s * 0.42, s * 0.1, s * 0.56); };
    for (const k of kinds) {
      if (k === 'S') { stem(s / 2); ctx.beginPath(); ctx.moveTo(s / 2, s * 0.02); ctx.lineTo(s * 0.7, s * 0.44); ctx.lineTo(s * 0.3, s * 0.44); ctx.fill(); }
      if (k === 'L' || k === 'R') {
        const d = k === 'L' ? -1 : 1, cx = s / 2;
        if (!kinds.includes('S')) stem(cx);
        ctx.save(); ctx.translate(cx, s * 0.55); ctx.scale(d, 1);
        ctx.beginPath(); ctx.moveTo(-s * 0.05, 0.0); ctx.quadraticCurveTo(-s * 0.05, -s * 0.22, s * 0.2, -s * 0.24); ctx.lineTo(s * 0.2, -s * 0.14); ctx.quadraticCurveTo(s * 0.05, -s * 0.12, s * 0.05, 0); ctx.fill();
        ctx.beginPath(); ctx.moveTo(s * 0.42, -s * 0.19); ctx.lineTo(s * 0.18, -s * 0.36); ctx.lineTo(s * 0.18, -s * 0.02); ctx.fill();
        ctx.restore();
      }
    }
  });
  arrow(DECALS.arrowS.cell, ['S']); arrow(DECALS.arrowL.cell, ['L']); arrow(DECALS.arrowR.cell, ['R']);
  arrow(DECALS.arrowSL.cell, ['S', 'L']); arrow(DECALS.arrowSR.cell, ['S', 'R']);
  // RXR
  paintCell(DECALS.rxr.cell, s => {
    ctx.strokeStyle = '#f4f4ef'; ctx.lineWidth = s * 0.05;
    ctx.beginPath(); ctx.moveTo(s * 0.12, s * 0.05); ctx.lineTo(s * 0.88, s * 0.95); ctx.moveTo(s * 0.88, s * 0.05); ctx.lineTo(s * 0.12, s * 0.95); ctx.stroke();
    ctx.fillStyle = '#f4f4ef'; ctx.font = `bold ${s * 0.22}px Arial`; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    ctx.save(); ctx.translate(s * 0.24, s * 0.5); ctx.scale(1, 2.2); ctx.fillText('R', 0, 0); ctx.restore();
    ctx.save(); ctx.translate(s * 0.76, s * 0.5); ctx.scale(1, 2.2); ctx.fillText('R', 0, 0); ctx.restore();
  });
  // manhole cover
  paintCell(DECALS.manhole.cell, s => {
    const g = ctx.createRadialGradient(s / 2, s / 2, s * 0.1, s / 2, s / 2, s * 0.48);
    g.addColorStop(0, '#5a5853'); g.addColorStop(1, '#3a3936');
    ctx.fillStyle = g; ctx.beginPath(); ctx.arc(s / 2, s / 2, s * 0.47, 0, 7); ctx.fill();
    ctx.strokeStyle = '#2a2927'; ctx.lineWidth = s * 0.012;
    for (let k = -6; k <= 6; k++) { ctx.beginPath(); ctx.moveTo(s * 0.12, s / 2 + k * s * 0.06); ctx.lineTo(s * 0.88, s / 2 + k * s * 0.06); ctx.stroke(); }
    ctx.lineWidth = s * 0.03; ctx.strokeStyle = '#6b6963'; ctx.beginPath(); ctx.arc(s / 2, s / 2, s * 0.45, 0, 7); ctx.stroke();
  });
  // storm drain grate
  paintCell(DECALS.drain.cell, s => {
    ctx.fillStyle = '#3b3a37'; ctx.fillRect(s * 0.05, s * 0.05, s * 0.9, s * 0.9);
    ctx.fillStyle = '#121212';
    for (let k = 0; k < 9; k++) ctx.fillRect(s * 0.15 + k * s * 0.08, s * 0.12, s * 0.04, s * 0.76);
  });
  return c;
}

// Sign face atlas (4x4) for props
export const SIGNS = { stop: 0, yield: 1, crossing: 2, hump: 3, railAdvance: 4, crossbuck: 5, busStop: 6, metro: 7, keepRight: 8, gateStripes: 9, adPanel: 10, rxrLights: 11 };
export function makeSignAtlas(size = 1024) {
  const c = canvas(size), ctx = c.getContext('2d');
  const cell = size / 4;
  const at = (idx, fn) => { const x = (idx % 4) * cell, y = Math.floor(idx / 4) * cell; ctx.save(); ctx.translate(x, y); fn(cell); ctx.restore(); };
  ctx.clearRect(0, 0, size, size);
  const poly = (s, n, r, rot = 0) => { ctx.beginPath(); for (let k = 0; k < n; k++) { const a = rot + k / n * Math.PI * 2; ctx.lineTo(s / 2 + Math.cos(a) * r, s / 2 + Math.sin(a) * r); } ctx.closePath(); };
  at(SIGNS.stop, s => { ctx.fillStyle = '#ddd'; poly(s, 8, s * 0.5, Math.PI / 8); ctx.fill(); ctx.fillStyle = '#c0141c'; poly(s, 8, s * 0.46, Math.PI / 8); ctx.fill(); ctx.fillStyle = '#fff'; ctx.font = `bold ${s * 0.28}px Arial`; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillText('STOP', s / 2, s / 2 + 4); });
  at(SIGNS.yield, s => { ctx.fillStyle = '#c0141c'; ctx.beginPath(); ctx.moveTo(s * 0.02, s * 0.08); ctx.lineTo(s * 0.98, s * 0.08); ctx.lineTo(s / 2, s * 0.94); ctx.closePath(); ctx.fill(); ctx.fillStyle = '#fff'; ctx.beginPath(); ctx.moveTo(s * 0.2, s * 0.19); ctx.lineTo(s * 0.8, s * 0.19); ctx.lineTo(s / 2, s * 0.72); ctx.closePath(); ctx.fill(); ctx.fillStyle = '#c0141c'; ctx.font = `bold ${s * 0.1}px Arial`; ctx.textAlign = 'center'; ctx.fillText('YIELD', s / 2, s * 0.36); });
  const diamond = (s, bg, draw) => { ctx.fillStyle = '#222'; poly(s, 4, s * 0.5, 0); ctx.fill(); ctx.fillStyle = bg; poly(s, 4, s * 0.46, 0); ctx.fill(); ctx.fillStyle = '#111'; draw(s); };
  at(SIGNS.crossing, s => diamond(s, '#c6e619', s => { ctx.beginPath(); ctx.arc(s * 0.5, s * 0.3, s * 0.06, 0, 7); ctx.fill(); ctx.fillRect(s * 0.46, s * 0.36, s * 0.08, s * 0.22); ctx.beginPath(); ctx.moveTo(s * 0.46, s * 0.56); ctx.lineTo(s * 0.36, s * 0.72); ctx.lineTo(s * 0.42, s * 0.74); ctx.lineTo(s * 0.52, s * 0.58); ctx.lineTo(s * 0.6, s * 0.74); ctx.lineTo(s * 0.66, s * 0.72); ctx.lineTo(s * 0.54, s * 0.54); ctx.fill(); ctx.fillRect(s * 0.25, s * 0.78, s * 0.5, s * 0.03); }));
  at(SIGNS.hump, s => diamond(s, '#f1c40f', s => { ctx.beginPath(); ctx.moveTo(s * 0.22, s * 0.62); ctx.quadraticCurveTo(s * 0.5, s * 0.25, s * 0.78, s * 0.62); ctx.lineTo(s * 0.78, s * 0.68); ctx.lineTo(s * 0.22, s * 0.68); ctx.fill(); ctx.font = `bold ${s * 0.1}px Arial`; ctx.textAlign = 'center'; ctx.fillText('HUMP', s / 2, s * 0.82); }));
  at(SIGNS.railAdvance, s => { ctx.fillStyle = '#222'; ctx.beginPath(); ctx.arc(s / 2, s / 2, s * 0.5, 0, 7); ctx.fill(); ctx.fillStyle = '#f1c40f'; ctx.beginPath(); ctx.arc(s / 2, s / 2, s * 0.46, 0, 7); ctx.fill(); ctx.strokeStyle = '#111'; ctx.lineWidth = s * 0.06; ctx.beginPath(); ctx.moveTo(s * 0.2, s * 0.2); ctx.lineTo(s * 0.8, s * 0.8); ctx.moveTo(s * 0.8, s * 0.2); ctx.lineTo(s * 0.2, s * 0.8); ctx.stroke(); ctx.fillStyle = '#111'; ctx.font = `bold ${s * 0.2}px Arial`; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillText('R', s * 0.24, s * 0.5); ctx.fillText('R', s * 0.76, s * 0.5); });
  at(SIGNS.crossbuck, s => { ctx.fillStyle = '#c0141c'; ctx.fillRect(0, s * 0.36, s, s * 0.28); ctx.fillStyle = '#f5f5f5'; ctx.fillRect(s * 0.02, s * 0.39, s * 0.96, s * 0.22); ctx.fillStyle = '#111'; ctx.font = `bold ${s * 0.1}px Arial`; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillText('RAILROAD', s / 2, s * 0.5); });
  at(SIGNS.busStop, s => { ctx.fillStyle = '#1d4f91'; ctx.fillRect(0, 0, s, s); ctx.fillStyle = '#fff'; ctx.font = `bold ${s * 0.18}px Arial`; ctx.textAlign = 'center'; ctx.fillText('BUS', s / 2, s * 0.42); ctx.fillText('STOP', s / 2, s * 0.66); });
  at(SIGNS.metro, s => { ctx.fillStyle = '#c63b2f'; ctx.fillRect(0, 0, s, s); ctx.fillStyle = '#fff'; ctx.font = `bold ${s * 0.72}px Arial`; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillText('M', s / 2, s * 0.54); });
  at(SIGNS.keepRight, s => { ctx.fillStyle = '#eee'; ctx.beginPath(); ctx.arc(s / 2, s / 2, s * 0.5, 0, 7); ctx.fill(); ctx.fillStyle = '#1d5fb0'; ctx.beginPath(); ctx.arc(s / 2, s / 2, s * 0.46, 0, 7); ctx.fill(); ctx.strokeStyle = '#fff'; ctx.lineWidth = s * 0.08; ctx.beginPath(); ctx.moveTo(s * 0.35, s * 0.3); ctx.lineTo(s * 0.62, s * 0.62); ctx.stroke(); ctx.fillStyle = '#fff'; ctx.beginPath(); ctx.moveTo(s * 0.72, s * 0.72); ctx.lineTo(s * 0.48, s * 0.7); ctx.lineTo(s * 0.7, s * 0.48); ctx.fill(); });
  at(SIGNS.gateStripes, s => { for (let k = 0; k < 8; k++) { ctx.fillStyle = k % 2 ? '#f2f2f2' : '#c8161e'; ctx.fillRect(k * s / 8, 0, s / 8, s); } });
  at(SIGNS.adPanel, s => { const g = ctx.createLinearGradient(0, 0, s, s); g.addColorStop(0, '#2b6fd6'); g.addColorStop(1, '#d64f8a'); ctx.fillStyle = g; ctx.fillRect(0, 0, s, s); ctx.fillStyle = 'rgba(255,255,255,.9)'; ctx.font = `bold ${s * 0.16}px Arial`; ctx.textAlign = 'center'; ctx.fillText('FRONTIER', s / 2, s * 0.45); ctx.font = `${s * 0.08}px Arial`; ctx.fillText('ENGINE / 01', s / 2, s * 0.6); });
  at(SIGNS.rxrLights, s => { ctx.fillStyle = '#111'; ctx.fillRect(0, 0, s, s); for (const x of [0.27, 0.73]) { ctx.fillStyle = '#3a0505'; ctx.beginPath(); ctx.arc(s * x, s / 2, s * 0.19, 0, 7); ctx.fill(); ctx.fillStyle = '#e11'; ctx.beginPath(); ctx.arc(s * x, s / 2, s * 0.14, 0, 7); ctx.fill(); } });
  return c;
}

export function signUV(idx) {
  const x = idx % 4, y = Math.floor(idx / 4);
  return [x / 4 + 0.003, 1 - (y + 1) / 4 + 0.003, (x + 1) / 4 - 0.003, 1 - y / 4 - 0.003];
}

export function makeWaterNormal(size = 512) {
  const h = fbm(size, 77, [[6, 0.5], [12, 0.3], [24, 0.2], [48, 0.1]]);
  return normalFromHeight(h, size, 4);
}

export function buildTextureSet() {
  const T = {};
  const wrap = (pair, repeat, opts = {}) => ({ map: toTexture(pair.color, { repeat, ...opts }), normal: pair.normal ? toTexture(pair.normal, { repeat, srgb: false }) : null });
  T.asphalt = wrap(makeAsphalt(512, [1, 1, 1.03], 11, 0), 1 / 6);
  T.asphaltOld = wrap(makeAsphalt(512, [1.05, 1.0, 0.95], 23, 1), 1 / 6);
  T.busRed = wrap(makeAsphalt(512, [2.0, 0.85, 0.75], 31, 0), 1 / 6);
  T.bikeGreen = wrap(makeAsphalt(512, [0.8, 1.55, 0.95], 37, 0), 1 / 6);
  T.concrete = wrap(slabs(512, 1, 1, 150, 8, 1.2, 41), 1 / 4.5);
  T.sidewalk = wrap(slabs(512, 3, 3, 165, 18, 1.6, 43), 1 / 4.5);
  T.curb = wrap(slabs(512, 1, 1, 175, 6, 1.0, 47), 1 / 1.5);
  T.pavers = wrap(pavers(512, 53, [150, 95, 78], [128, 112, 98]), 1 / 2.4);
  T.cobble = wrap(cobbles(512, 59), 1 / 2.2);
  T.gravel = wrap(speckle(512, 61, [128, 118, 102], 0.9, 1.4), 1 / 3);
  T.ballast = wrap(speckle(512, 67, [112, 108, 102], 1.0, 1.8), 1 / 2);
  T.dirt = wrap(speckle(512, 71, [116, 92, 66], 0.7, 1.0), 1 / 5);
  T.grass = wrap(grass(512, 73), 1 / 5);
  T.quay = wrap(stoneWall(512, 79), 1 / 4);
  T.tactile = { map: toTexture(tactile(256), { repeat: 1 / 0.6 }) };
  T.fence = { map: toTexture(fence(256), { repeat: 1 / 1.6 }) };
  T.decals = new THREE.CanvasTexture(makeDecalAtlas(2048)); T.decals.colorSpace = THREE.SRGBColorSpace; T.decals.anisotropy = 8;
  T.signs = new THREE.CanvasTexture(makeSignAtlas(1024)); T.signs.colorSpace = THREE.SRGBColorSpace; T.signs.anisotropy = 4;
  T.waterNormal = toTexture(makeWaterNormal(512), { repeat: 1 / 30, srgb: false });
  T.groundDetail = wrap(speckle(512, 83, [208, 206, 200], 0.55, 0.9), 1);
  return T;
}

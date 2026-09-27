// Procedural texture painters. Everything the scene needs is painted on
// canvases at start-up so there are zero external image assets.
import * as THREE from '../vendor/three.module.js';
import { Simplex2, mulberry32, clamp, lerp, smoothstep } from './noise.js';

const snoise = new Simplex2(4242);

function canvas(w, h) {
  const c = document.createElement('canvas');
  c.width = w; c.height = h;
  return c;
}

export function toTexture(c, { srgb = true, repeat = null, aniso = 8, wrap = false } = {}) {
  const t = new THREE.CanvasTexture(c);
  if (srgb) t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = aniso;
  if (wrap || repeat) { t.wrapS = t.wrapT = THREE.RepeatWrapping; }
  if (repeat) t.repeat.set(repeat[0], repeat[1]);
  t.generateMipmaps = true;
  t.minFilter = THREE.LinearMipmapLinearFilter;
  t.needsUpdate = true;
  return t;
}

/** Convert a greyscale height canvas into a tangent-space normal map. */
export function heightToNormal(hc, strength = 2.0, wrap = true) {
  const w = hc.width, h = hc.height;
  const src = hc.getContext('2d').getImageData(0, 0, w, h).data;
  const out = canvas(w, h);
  const ctx = out.getContext('2d');
  const img = ctx.createImageData(w, h);
  const d = img.data;
  const H = (x, y) => {
    if (wrap) { x = (x + w) % w; y = (y + h) % h; } else { x = clamp(x, 0, w - 1); y = clamp(y, 0, h - 1); }
    return src[(y * w + x) * 4] / 255;
  };
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const dx = (H(x + 1, y) - H(x - 1, y)) * strength;
      const dy = (H(x, y + 1) - H(x, y - 1)) * strength;
      let nx = -dx, ny = -dy, nz = 1;
      const l = Math.hypot(nx, ny, nz); nx /= l; ny /= l; nz /= l;
      const i = (y * w + x) * 4;
      d[i] = (nx * 0.5 + 0.5) * 255; d[i + 1] = (ny * 0.5 + 0.5) * 255; d[i + 2] = (nz * 0.5 + 0.5) * 255; d[i + 3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);
  return out;
}

const rgb = (r, g, b) => `rgb(${r | 0},${g | 0},${b | 0})`;
const mixc = (a, b, t) => [lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t)];

/* ----------------------------------------------------------------------------
 * FEATHER
 * A single flight/contour feather painted with: calamus, rachis (shaft),
 * barbs fanning out at an angle, occasional splits in the vane, a downy
 * base, asymmetric vanes (narrow leading edge) and colour zones.
 * Returns { map, normalMap, roughnessMap } textures. Feather points +Y (up).
 * -------------------------------------------------------------------------- */
export function makeFeather({
  w = 256, h = 768,
  base = [42, 30, 22], mid = [58, 42, 30], tip = [28, 20, 15], edge = null,
  shaft = [150, 135, 110], shaftDark = [60, 48, 36],
  asym = 0.35,          // 0 = symmetric, 1 = one-sided
  tipShape = 'pointed', // 'pointed' | 'round'
  downy = 0.28,         // fraction of the length that is fluffy
  splits = 0.35,        // probability density of vane splits
  seed = 1,
  white = false,
} = {}) {
  const rnd = mulberry32(seed * 7919 + 13);
  const col = canvas(w, h), hgt = canvas(w, h);
  const cc = col.getContext('2d'), hc = hgt.getContext('2d');
  cc.clearRect(0, 0, w, h);
  hc.fillStyle = '#000'; hc.fillRect(0, 0, w, h);

  const cx = w * 0.5;
  const maxHalf = w * 0.46;
  // vane half width as a function of t (0 = base, 1 = tip)
  const shape = (t) => {
    const calamus = smoothstep(0.0, 0.16, t);
    let s = Math.pow(Math.sin(Math.PI * Math.pow(t, 0.72)), tipShape === 'round' ? 0.55 : 0.85);
    if (tipShape === 'round') s = Math.max(s, (1 - smoothstep(0.86, 1.0, t)) * 0.9 * smoothstep(0.1, 0.4, t));
    return s * calamus;
  };
  const halfL = (t) => maxHalf * (1 - asym) * shape(t) * 0.55 + 0.6; // leading (narrow) side
  const halfR = (t) => maxHalf * (1 + asym * 0.45) * shape(t) + 0.6; // trailing (wide) side

  // Vane outline path (y grows downward on canvas, so base at bottom)
  const yOf = (t) => h - 2 - t * (h - 4);
  const outline = (ctx) => {
    ctx.beginPath();
    const N = 120;
    for (let i = 0; i <= N; i++) { const t = i / N; ctx.lineTo(cx - halfL(t), yOf(t)); }
    for (let i = N; i >= 0; i--) { const t = i / N; ctx.lineTo(cx + halfR(t), yOf(t)); }
    ctx.closePath();
  };

  // Base fill (gradient along the length)
  const grad = cc.createLinearGradient(0, h, 0, 0);
  const c0 = white ? [235, 232, 225] : base, c1 = white ? [246, 244, 238] : mid, c2 = white ? [225, 220, 210] : tip;
  grad.addColorStop(0, rgb(...c0)); grad.addColorStop(0.5, rgb(...c1)); grad.addColorStop(1, rgb(...c2));
  cc.save(); outline(cc); cc.clip();
  cc.fillStyle = grad; cc.fillRect(0, 0, w, h);

  // Barbs: fine strokes fanning from the rachis toward the tip.
  const barbAngle = 0.62; // radians from the shaft
  const step = 2.2;
  cc.lineWidth = 1.1;
  let splitAcc = 0;
  for (let y = h - 2; y > 0; y -= step) {
    const t = 1 - y / h;
    const s = shape(t);
    if (s < 0.02) continue;
    // occasionally leave a wedge gap in the vane (split barbs)
    if (rnd() < splits * 0.02) splitAcc = 4 + rnd() * 6;
    const gap = splitAcc > 0; if (gap) splitAcc--;
    for (const side of [-1, 1]) {
      const half = side < 0 ? halfL(t) : halfR(t);
      const len = half * (0.92 + rnd() * 0.12);
      const shade = (rnd() - 0.5) * 26 + (side < 0 ? -6 : 0);
      const fluff = t < downy ? 1 - t / downy : 0;
      const baseCol = mixc(mixc(c0, c1, smoothstep(0.2, 0.6, t)), c2, smoothstep(0.55, 1, t));
      const edgeCol = edge ? edge : baseCol;
      const cA = mixc(baseCol, edgeCol, 0.0), cB = mixc(baseCol, edgeCol, 0.85);
      const alpha = gap ? 0.15 : (fluff > 0 ? 0.35 + 0.3 * (1 - fluff) : 0.75 + rnd() * 0.25);
      const g = cc.createLinearGradient(cx, y, cx + side * len * Math.cos(barbAngle * 0.5), y - len * Math.sin(barbAngle));
      g.addColorStop(0, `rgba(${cA[0] + shade | 0},${cA[1] + shade | 0},${cA[2] + shade | 0},${alpha})`);
      g.addColorStop(1, `rgba(${cB[0] + shade | 0},${cB[1] + shade | 0},${cB[2] + shade | 0},${alpha * (edge ? 1 : 0.85)})`);
      cc.strokeStyle = g;
      cc.beginPath();
      const wob = fluff > 0 ? (rnd() - 0.5) * 8 * fluff : (rnd() - 0.5) * 1.2;
      cc.moveTo(cx, y);
      cc.quadraticCurveTo(cx + side * len * 0.5 + wob, y - len * Math.tan(barbAngle) * 0.5 + wob * 0.6, cx + side * len, y - len * Math.tan(barbAngle) * (fluff > 0 ? 0.6 : 1));
      cc.stroke();
      // height: barbs are ridges
      hc.strokeStyle = `rgba(255,255,255,${gap ? 0.05 : 0.35 + rnd() * 0.25})`;
      hc.lineWidth = 1;
      hc.beginPath(); hc.moveTo(cx, y);
      hc.quadraticCurveTo(cx + side * len * 0.5 + wob, y - len * Math.tan(barbAngle) * 0.5 + wob * 0.6, cx + side * len, y - len * Math.tan(barbAngle) * (fluff > 0 ? 0.6 : 1));
      hc.stroke();
    }
  }
  // darker leading edge & subtle sheen band
  cc.globalAlpha = 0.25;
  const sheen = cc.createLinearGradient(0, 0, w, 0);
  sheen.addColorStop(0, 'rgba(0,0,0,0.35)'); sheen.addColorStop(0.3, 'rgba(255,255,255,0.10)'); sheen.addColorStop(0.7, 'rgba(0,0,0,0.0)'); sheen.addColorStop(1, 'rgba(0,0,0,0.25)');
  cc.fillStyle = sheen; cc.fillRect(0, 0, w, h);
  cc.globalAlpha = 1;
  cc.restore();

  // Rachis (shaft): tapers from thick calamus to hair-thin tip
  const drawShaft = (ctx, colA, colB, mul = 1) => {
    for (let i = 0; i < 40; i++) {
      const t0 = i / 40, t1 = (i + 1) / 40;
      const wdt = lerp(w * 0.055, w * 0.006, Math.pow(t0, 0.8)) * mul;
      const c = mixc(colA, colB, t0);
      ctx.strokeStyle = rgb(...c); ctx.lineWidth = wdt; ctx.lineCap = 'round';
      ctx.beginPath(); ctx.moveTo(cx + Math.sin(t0 * 3) * 0.6, yOf(t0)); ctx.lineTo(cx + Math.sin(t1 * 3) * 0.6, yOf(t1 * 0.995)); ctx.stroke();
    }
  };
  drawShaft(cc, shaft, mixc(shaftDark, tip, 0.5));
  // shaft highlight
  cc.globalAlpha = 0.45; drawShaft(cc, [255, 250, 240], shaft, 0.35); cc.globalAlpha = 1;
  drawShaft(hc, [255, 255, 255], [180, 180, 180], 1.15);
  // Fill the gap between transparent pixels & barbs so alphaTest does not eat the vane
  const normalMap = heightToNormal(hgt, 1.6, false);
  const map = toTexture(col, { srgb: true, aniso: 16 });
  map.wrapS = map.wrapT = THREE.ClampToEdgeWrapping;
  const nrm = toTexture(normalMap, { srgb: false, aniso: 16 });
  nrm.wrapS = nrm.wrapT = THREE.ClampToEdgeWrapping;
  return { map, normalMap: nrm };
}

/* ----------------------------------------------------------------------------
 * PLUMAGE — overlapping rows of scalloped contour feathers (body / head).
 * -------------------------------------------------------------------------- */
export function makePlumage({ size = 1024, base = [48, 34, 24], edge = [92, 70, 50], dark = [22, 15, 10], white = false, seed = 3, scale = 1 } = {}) {
  const rnd = mulberry32(seed * 31 + 7);
  const col = canvas(size, size), hgt = canvas(size, size);
  const cc = col.getContext('2d'), hc = hgt.getContext('2d');
  const b = white ? [232, 228, 220] : base, e = white ? [250, 248, 242] : edge, d = white ? [200, 196, 188] : dark;
  cc.fillStyle = rgb(...d); cc.fillRect(0, 0, size, size);
  hc.fillStyle = '#404040'; hc.fillRect(0, 0, size, size);
  const fw = 44 * scale, fh = 78 * scale;
  const rows = Math.ceil(size / (fh * 0.42)) + 2, cols = Math.ceil(size / (fw * 0.82)) + 2;
  // paint top-down so that lower feathers overlap the ones above (like real plumage)
  for (let r = -1; r < rows; r++) {
    for (let c = -1; c < cols; c++) {
      const x = c * fw * 0.82 + (r % 2 ? fw * 0.41 : 0) + (rnd() - 0.5) * 6;
      const y = r * fh * 0.42 + (rnd() - 0.5) * 6;
      const tone = (rnd() - 0.5) * 22;
      const drawOne = (ctx, ox, oy) => {
        ctx.save(); ctx.translate(x + ox, y + oy);
        ctx.rotate((rnd() - 0.5) * 0.18);
        ctx.beginPath();
        ctx.moveTo(-fw * 0.5, -fh * 0.5);
        ctx.bezierCurveTo(-fw * 0.62, fh * 0.1, -fw * 0.35, fh * 0.55, 0, fh * 0.5);
        ctx.bezierCurveTo(fw * 0.35, fh * 0.55, fw * 0.62, fh * 0.1, fw * 0.5, -fh * 0.5);
        ctx.closePath();
        return () => ctx.restore();
      };
      // colour
      let done = drawOne(cc, 0, 0);
      const g = cc.createLinearGradient(0, -fh * 0.5, 0, fh * 0.5);
      g.addColorStop(0, rgb(b[0] + tone - 12, b[1] + tone - 10, b[2] + tone - 8));
      g.addColorStop(0.65, rgb(b[0] + tone, b[1] + tone, b[2] + tone));
      g.addColorStop(0.9, rgb(e[0] + tone, e[1] + tone, e[2] + tone));
      g.addColorStop(1, rgb(e[0] + tone + 10, e[1] + tone + 10, e[2] + tone + 8));
      cc.fillStyle = g; cc.fill();
      // barbs
      cc.clip();
      cc.lineWidth = 1;
      for (let i = -fw * 0.5; i < fw * 0.5; i += 2.4) {
        const a = 0.18 + rnd() * 0.25;
        cc.strokeStyle = rnd() < 0.5 ? `rgba(0,0,0,${a * 0.7})` : `rgba(255,255,255,${a * 0.35})`;
        cc.beginPath(); cc.moveTo(0, -fh * 0.5); cc.lineTo(i * 1.15, fh * 0.55); cc.stroke();
      }
      // shaft
      cc.strokeStyle = `rgba(255,240,220,0.22)`; cc.lineWidth = 1.5;
      cc.beginPath(); cc.moveTo(0, -fh * 0.5); cc.lineTo(0, fh * 0.5); cc.stroke();
      // shadow of the overlapping feather above
      const sg = cc.createLinearGradient(0, -fh * 0.5, 0, -fh * 0.1);
      sg.addColorStop(0, 'rgba(0,0,0,0.55)'); sg.addColorStop(1, 'rgba(0,0,0,0)');
      cc.fillStyle = sg; cc.fillRect(-fw, -fh, fw * 2, fh * 2);
      done();
      // height
      done = drawOne(hc, 0, 0);
      const hg = hc.createLinearGradient(0, -fh * 0.5, 0, fh * 0.5);
      hg.addColorStop(0, '#303030'); hg.addColorStop(0.7, '#8a8a8a'); hg.addColorStop(1, '#b0b0b0');
      hc.fillStyle = hg; hc.fill();
      hc.clip();
      hc.lineWidth = 1;
      for (let i = -fw * 0.5; i < fw * 0.5; i += 2.4) {
        hc.strokeStyle = `rgba(255,255,255,${0.12 + rnd() * 0.15})`;
        hc.beginPath(); hc.moveTo(0, -fh * 0.5); hc.lineTo(i * 1.15, fh * 0.55); hc.stroke();
      }
      done();
    }
  }
  // large scale mottling
  const img = cc.getImageData(0, 0, size, size); const px = img.data;
  for (let y = 0; y < size; y += 1) for (let x = 0; x < size; x += 1) {
    const n = snoise.fbm(x / 180 + seed, y / 180, 3) * 0.16 + 1;
    const i = (y * size + x) * 4;
    px[i] *= n; px[i + 1] *= n; px[i + 2] *= n;
  }
  cc.putImageData(img, 0, 0);
  return {
    map: toTexture(col, { srgb: true, wrap: true }),
    normalMap: toTexture(heightToNormal(hgt, 1.8), { srgb: false, wrap: true }),
  };
}

/* ----------------------------------------------------------------------------
 * SCALY SKIN — bird tarsus / toes (yellow) or beak base (cere).
 * -------------------------------------------------------------------------- */
export function makeScales({ size = 512, base = [222, 176, 44], dark = [150, 108, 20], seed = 9 } = {}) {
  const rnd = mulberry32(seed);
  const col = canvas(size, size), hgt = canvas(size, size);
  const cc = col.getContext('2d'), hc = hgt.getContext('2d');
  cc.fillStyle = rgb(...dark); cc.fillRect(0, 0, size, size);
  hc.fillStyle = '#202020'; hc.fillRect(0, 0, size, size);
  const cell = 26;
  for (let y = -cell; y < size + cell; y += cell * 0.85) {
    for (let x = -cell; x < size + cell; x += cell) {
      const ox = ((y / (cell * 0.85)) | 0) % 2 ? cell * 0.5 : 0;
      const px = x + ox + (rnd() - 0.5) * 5, py = y + (rnd() - 0.5) * 5;
      const r = cell * (0.42 + rnd() * 0.1);
      const t = (rnd() - 0.5) * 30;
      const g = cc.createRadialGradient(px - r * 0.3, py - r * 0.3, r * 0.1, px, py, r);
      g.addColorStop(0, rgb(base[0] + t + 20, base[1] + t + 20, base[2] + t + 10));
      g.addColorStop(0.8, rgb(base[0] + t, base[1] + t, base[2] + t));
      g.addColorStop(1, rgb(dark[0] + t, dark[1] + t, dark[2]));
      cc.fillStyle = g; cc.beginPath(); cc.ellipse(px, py, r, r * 0.8, 0, 0, Math.PI * 2); cc.fill();
      const hg = hc.createRadialGradient(px, py, 0, px, py, r);
      hg.addColorStop(0, '#e0e0e0'); hg.addColorStop(0.75, '#909090'); hg.addColorStop(1, '#101010');
      hc.fillStyle = hg; hc.beginPath(); hc.ellipse(px, py, r, r * 0.8, 0, 0, Math.PI * 2); hc.fill();
    }
  }
  return { map: toTexture(col, { srgb: true, wrap: true }), normalMap: toTexture(heightToNormal(hgt, 2.4), { srgb: false, wrap: true }) };
}

/* ----------------------------------------------------------------------------
 * ROCK — layered sandstone with strata, cracks & grit. Tileable.
 * -------------------------------------------------------------------------- */
export function makeRock({ size = 1024, seed = 11, warm = true } = {}) {
  const col = canvas(size, size), hgt = canvas(size, size);
  const cc = col.getContext('2d'), hc = hgt.getContext('2d');
  const img = cc.createImageData(size, size), him = hc.createImageData(size, size);
  const d = img.data, hd = him.data;
  const n2 = new Simplex2(seed);
  const A = warm ? [176, 138, 98] : [120, 118, 112], B = warm ? [116, 86, 58] : [70, 68, 64], C = warm ? [206, 176, 132] : [160, 158, 150];
  const TAU = Math.PI * 2;
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      // tileable coordinates via 4D-ish trick: sample on a torus using two 2D noises
      const u = x / size, v = y / size;
      const px = Math.cos(u * TAU) * 1.6, py = Math.sin(u * TAU) * 1.6, pz = Math.cos(v * TAU) * 1.6, pw = Math.sin(v * TAU) * 1.6;
      const f1 = n2.fbm(px + pz * 0.7, py + pw * 0.7, 5);
      const f2 = n2.fbm(px * 2.3 + 5.1 + pw, py * 2.3 + pz, 4);
      const rid = n2.ridged(px * 3 + 9 + pz, py * 3 + pw, 4);
      // strata (horizontal bands, distorted)
      const strat = Math.sin(v * TAU * 7 + f1 * 3.5) * 0.5 + 0.5;
      const grit = n2.noise(x * 0.35, y * 0.35) * 0.5 + 0.5;
      let hgtv = 0.5 + f1 * 0.25 + rid * 0.18 - strat * 0.12 + (grit - 0.5) * 0.08;
      // cracks
      const crack = Math.pow(1 - Math.abs(n2.noise(px * 4 + pw * 2, py * 4 + pz * 2)), 14);
      hgtv -= crack * 0.5;
      hgtv = clamp(hgtv, 0, 1);
      let c = mixc(B, A, smoothstep(0.25, 0.75, hgtv));
      c = mixc(c, C, smoothstep(0.55, 0.95, strat) * 0.35);
      const m = 1 + (f2 * 0.16) - crack * 0.6 + (grit - 0.5) * 0.12;
      const i = (y * size + x) * 4;
      d[i] = clamp(c[0] * m, 0, 255); d[i + 1] = clamp(c[1] * m, 0, 255); d[i + 2] = clamp(c[2] * m, 0, 255); d[i + 3] = 255;
      hd[i] = hd[i + 1] = hd[i + 2] = hgtv * 255; hd[i + 3] = 255;
    }
  }
  cc.putImageData(img, 0, 0); hc.putImageData(him, 0, 0);
  return { map: toTexture(col, { srgb: true, wrap: true, aniso: 16 }), normalMap: toTexture(heightToNormal(hgt, 3.2), { srgb: false, wrap: true, aniso: 16 }) };
}

/* ----------------------------------------------------------------------------
 * GROUND — dry grass / scrub / dirt mixture. Tileable.
 * -------------------------------------------------------------------------- */
export function makeGround({ size = 1024, seed = 21 } = {}) {
  const col = canvas(size, size), hgt = canvas(size, size);
  const cc = col.getContext('2d'), hc = hgt.getContext('2d');
  const img = cc.createImageData(size, size), him = hc.createImageData(size, size);
  const d = img.data, hd = him.data;
  const n2 = new Simplex2(seed);
  const TAU = Math.PI * 2;
  const dirt = [128, 104, 74], grass = [112, 108, 58], dry = [158, 140, 88], stone = [140, 132, 120];
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    const u = x / size, v = y / size;
    const px = Math.cos(u * TAU) * 2, py = Math.sin(u * TAU) * 2, pz = Math.cos(v * TAU) * 2, pw = Math.sin(v * TAU) * 2;
    const f = n2.fbm(px + pz, py + pw, 5);
    const g = n2.fbm(px * 3 + pw + 4, py * 3 + pz, 4);
    const grit = n2.noise(x * 0.5, y * 0.5);
    let c = mixc(dirt, grass, smoothstep(-0.2, 0.3, f));
    c = mixc(c, dry, smoothstep(0.1, 0.5, g) * 0.6);
    const pebble = Math.pow(clamp(n2.noise(x * 0.09 + 3, y * 0.09) , 0, 1), 3);
    c = mixc(c, stone, pebble);
    const m = 1 + grit * 0.18;
    const i = (y * size + x) * 4;
    d[i] = clamp(c[0] * m, 0, 255); d[i + 1] = clamp(c[1] * m, 0, 255); d[i + 2] = clamp(c[2] * m, 0, 255); d[i + 3] = 255;
    const hv = clamp(0.5 + f * 0.2 + grit * 0.15 + pebble * 0.4, 0, 1);
    hd[i] = hd[i + 1] = hd[i + 2] = hv * 255; hd[i + 3] = 255;
  }
  cc.putImageData(img, 0, 0); hc.putImageData(him, 0, 0);
  return { map: toTexture(col, { srgb: true, wrap: true, aniso: 16 }), normalMap: toTexture(heightToNormal(hgt, 2.0), { srgb: false, wrap: true, aniso: 16 }) };
}

/* ----------------------------------------------------------------------------
 * ASPHALT — road ribbon texture. U across the road, V along it (tiles in V).
 * -------------------------------------------------------------------------- */
export function makeAsphalt({ w = 512, h = 1024, seed = 5 } = {}) {
  const col = canvas(w, h), hgt = canvas(w, h);
  const cc = col.getContext('2d'), hc = hgt.getContext('2d');
  const img = cc.createImageData(w, h), him = hc.createImageData(w, h);
  const d = img.data, hd = him.data;
  const n2 = new Simplex2(seed);
  const TAU = Math.PI * 2;
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const v = y / h;
    const pz = Math.cos(v * TAU) * 3, pw = Math.sin(v * TAU) * 3;
    const grain = n2.noise(x * 0.9, y * 0.9) * 0.5 + 0.5;
    const patch = n2.fbm(x / 140 + pz, pw + y / 140, 4);
    const wear = Math.exp(-Math.pow((x / w - 0.27) / 0.09, 2)) + Math.exp(-Math.pow((x / w - 0.73) / 0.09, 2)); // tyre tracks
    let g = 62 + grain * 30 + patch * 12 - wear * 10;
    const i = (y * w + x) * 4;
    let r = g * 1.02, gg = g, b = g * 0.98;
    // lane markings: edges solid, centre dashed
    const u = x / w;
    const edgeLine = (Math.abs(u - 0.035) < 0.012 || Math.abs(u - 0.965) < 0.012) ? 1 : 0;
    const dash = (Math.abs(u - 0.5) < 0.012 && (v * 8) % 1 < 0.5) ? 1 : 0;
    const paint = Math.max(edgeLine, dash) * (0.55 + grain * 0.45) * (1 - wear * 0.2);
    if (paint > 0) {
      const pc = dash ? [232, 200, 90] : [225, 225, 218];
      r = lerp(r, pc[0], paint); gg = lerp(gg, pc[1], paint); b = lerp(b, pc[2], paint);
    }
    d[i] = r; d[i + 1] = gg; d[i + 2] = b; d[i + 3] = 255;
    hd[i] = hd[i + 1] = hd[i + 2] = (0.5 + (grain - 0.5) * 0.6 + patch * 0.1) * 255; hd[i + 3] = 255;
  }
  cc.putImageData(img, 0, 0); hc.putImageData(him, 0, 0);
  const map = toTexture(col, { srgb: true, aniso: 16 }); map.wrapS = THREE.ClampToEdgeWrapping; map.wrapT = THREE.RepeatWrapping;
  const nrm = toTexture(heightToNormal(hgt, 1.2), { srgb: false, aniso: 16 }); nrm.wrapS = THREE.ClampToEdgeWrapping; nrm.wrapT = THREE.RepeatWrapping;
  return { map, normalMap: nrm };
}

/* Soft round sprite for dust/smoke and a hard spark dot. */
export function makeSprite(kind = 'soft', size = 128) {
  const c = canvas(size, size), ctx = c.getContext('2d');
  const g = ctx.createRadialGradient(size / 2, size / 2, 0, size / 2, size / 2, size / 2);
  if (kind === 'soft') { g.addColorStop(0, 'rgba(255,255,255,0.9)'); g.addColorStop(0.35, 'rgba(255,255,255,0.45)'); g.addColorStop(1, 'rgba(255,255,255,0)'); }
  else { g.addColorStop(0, 'rgba(255,255,255,1)'); g.addColorStop(0.2, 'rgba(255,255,255,0.9)'); g.addColorStop(1, 'rgba(255,255,255,0)'); }
  ctx.fillStyle = g; ctx.fillRect(0, 0, size, size);
  if (kind === 'soft') { // break up the blob
    const n2 = new Simplex2(77);
    const img = ctx.getImageData(0, 0, size, size); const d = img.data;
    for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) { const i = (y * size + x) * 4 + 3; d[i] *= 0.6 + 0.4 * (n2.fbm(x / 18, y / 18, 3) * 0.5 + 0.5); }
    ctx.putImageData(img, 0, 0);
  }
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; return t;
}

/* Eye: yellow iris with radial fibres and a black pupil (bald eagle). */
export function makeEye(size = 256) {
  const c = canvas(size, size), ctx = c.getContext('2d');
  ctx.fillStyle = '#f0e6d0'; ctx.fillRect(0, 0, size, size);
  const cx = size / 2, cy = size / 2;
  const g = ctx.createRadialGradient(cx, cy, size * 0.08, cx, cy, size * 0.36);
  g.addColorStop(0, '#5a3a06'); g.addColorStop(0.25, '#d9a21b'); g.addColorStop(0.8, '#f2c744'); g.addColorStop(1, '#8a5a10');
  ctx.fillStyle = g; ctx.beginPath(); ctx.arc(cx, cy, size * 0.36, 0, Math.PI * 2); ctx.fill();
  const rnd = mulberry32(3);
  for (let i = 0; i < 260; i++) {
    const a = rnd() * Math.PI * 2; const r0 = size * 0.12, r1 = size * (0.3 + rnd() * 0.07);
    ctx.strokeStyle = rnd() < 0.5 ? 'rgba(80,40,0,0.35)' : 'rgba(255,240,180,0.35)'; ctx.lineWidth = 1 + rnd();
    ctx.beginPath(); ctx.moveTo(cx + Math.cos(a) * r0, cy + Math.sin(a) * r0); ctx.lineTo(cx + Math.cos(a + 0.05) * r1, cy + Math.sin(a + 0.05) * r1); ctx.stroke();
  }
  ctx.fillStyle = '#050403'; ctx.beginPath(); ctx.arc(cx, cy, size * 0.13, 0, Math.PI * 2); ctx.fill();
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; return t;
}

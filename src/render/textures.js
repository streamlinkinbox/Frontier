/**
 * PROCEDURAL TEXTURE FOUNDRY
 *
 * Every map in the scene is synthesised on a canvas at load time: no external
 * assets, no licensing, deterministic across machines. The maps are authored
 * from measured insect structure — ommatidial packing on the compound eyes,
 * lanceolate scale rows and microtrichia on the wing membrane, inter-articular
 * pale rings on the legs (S14/S15).
 */

import * as THREE from 'three';

/* ------------------------------------------------------------- utilities */

function canvas(w, h) {
  const c = document.createElement('canvas');
  c.width = w; c.height = h;
  return { c, x: c.getContext('2d', { willReadFrequently: false }) };
}

function toTexture(c, { srgb = true, repeat = null, aniso = 8, flipY = true } = {}) {
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = srgb ? THREE.SRGBColorSpace : THREE.NoColorSpace;
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.anisotropy = aniso;
  t.flipY = flipY;
  if (repeat) t.repeat.set(repeat[0], repeat[1]);
  t.needsUpdate = true;
  return t;
}

/** Cheap deterministic value noise so the maps are identical every run. */
function mulberry(seed) {
  let a = seed >>> 0;
  return () => {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function fbmField(w, h, octaves, seed, gain = 0.5) {
  const rnd = mulberry(seed);
  const out = new Float32Array(w * h);
  let amp = 1, total = 0;
  for (let o = 0; o < octaves; o++) {
    const gw = Math.max(2, Math.round(w / Math.pow(2, octaves - o)));
    const gh = Math.max(2, Math.round(h / Math.pow(2, octaves - o)));
    const grid = new Float32Array(gw * gh);
    for (let i = 0; i < grid.length; i++) grid[i] = rnd();
    for (let y = 0; y < h; y++) {
      const fy = (y / h) * (gh - 1), y0 = Math.floor(fy), ty = fy - y0;
      const sy = ty * ty * (3 - 2 * ty);
      const y1 = Math.min(gh - 1, y0 + 1);
      for (let x = 0; x < w; x++) {
        const fx = (x / w) * (gw - 1), x0 = Math.floor(fx), tx = fx - x0;
        const sx = tx * tx * (3 - 2 * tx);
        const x1 = Math.min(gw - 1, x0 + 1);
        const a = grid[y0 * gw + x0] * (1 - sx) + grid[y0 * gw + x1] * sx;
        const b = grid[y1 * gw + x0] * (1 - sx) + grid[y1 * gw + x1] * sx;
        out[y * w + x] += (a * (1 - sy) + b * sy) * amp;
      }
    }
    total += amp; amp *= gain;
  }
  for (let i = 0; i < out.length; i++) out[i] /= total;
  return out;
}

/* --------------------------------------------------------- compound eye */

/**
 * Compound eye map. Mosquito eyes are a dense hexagonal array of ommatidia
 * (~6 500-8 000 per eye) with dark pigment between them; the whole array
 * carries the species' iridescent green/gold sheen. We render the hex lattice
 * analytically so the facets stay crisp under a close camera.
 */
export function makeCompoundEye(size = 1024, tint = [0.30, 0.46, 0.26]) {
  const { c, x } = canvas(size, size);
  // Base pigment
  const g = x.createLinearGradient(0, 0, size, size);
  g.addColorStop(0, '#0a1408');
  g.addColorStop(0.5, '#12200e');
  g.addColorStop(1, '#0a1408');
  x.fillStyle = g; x.fillRect(0, 0, size, size);

  const rnd = mulberry(7741);
  const cols = 78, rows = 90;
  const cw = size / cols, ch = size / rows;
  const hr = Math.min(cw, ch) * 0.5;

  for (let j = -1; j <= rows; j++) {
    for (let i = -1; i <= cols; i++) {
      const cx = (i + (j % 2 ? 0.5 : 0)) * cw + cw * 0.5;
      const cy = (j + 0.5) * ch;
      // Each ommatidium is a shallow truncated cone: brighter at the centre,
      // with a dark rim where the pigment cells sit.
      const jitter = 0.86 + rnd() * 0.28;
      const hueShift = (rnd() - 0.5) * 0.30;
      const r = Math.round(THREE.MathUtils.clamp(
        (tint[0] * (1 + hueShift) + rnd() * 0.10) * 255 * jitter, 0, 255));
      const gg = Math.round(THREE.MathUtils.clamp(
        (tint[1] * (1 + hueShift * 0.6) + rnd() * 0.12) * 255 * jitter, 0, 255));
      const b = Math.round(THREE.MathUtils.clamp(
        (tint[2] * (1 - hueShift * 0.4) + rnd() * 0.07) * 255 * jitter, 0, 255));

      const grad = x.createRadialGradient(cx - hr * 0.28, cy - hr * 0.32, hr * 0.08, cx, cy, hr);
      grad.addColorStop(0, `rgb(${Math.min(255, r * 1.55) | 0},${Math.min(255, gg * 1.55) | 0},${Math.min(255, b * 1.45) | 0})`);
      grad.addColorStop(0.62, `rgb(${r},${gg},${b})`);
      grad.addColorStop(1, 'rgb(6,12,6)');
      x.fillStyle = grad;
      // Hexagonal ommatidium cross-section
      x.beginPath();
      for (let k = 0; k < 6; k++) {
        const a = (k / 6) * Math.PI * 2 + Math.PI / 6;
        const px = cx + Math.cos(a) * hr * 0.98;
        const py = cy + Math.sin(a) * hr * 0.98;
        k === 0 ? x.moveTo(px, py) : x.lineTo(px, py);
      }
      x.closePath(); x.fill();
    }
  }

  // Setae and the fine pale "hairy" specular glints the eyes pick up in macro shots
  x.globalAlpha = 0.16;
  for (let i = 0; i < 900; i++) {
    const px = rnd() * size, py = rnd() * size, len = 6 + rnd() * 16, a = rnd() * Math.PI * 2;
    x.strokeStyle = 'rgba(210,225,200,0.7)'; x.lineWidth = 0.7;
    x.beginPath(); x.moveTo(px, py); x.lineTo(px + Math.cos(a) * len, py + Math.sin(a) * len); x.stroke();
  }
  x.globalAlpha = 1;

  // Roughness map: ommatidial centres are smooth, the pigment web is rough.
  const rc = canvas(size >> 1, size >> 1);
  rc.x.fillStyle = '#b4b4b4'; rc.x.fillRect(0, 0, rc.c.width, rc.c.height);
  const rr = mulberry(2211);
  for (let i = 0; i < 2400; i++) {
    const px = rr() * rc.c.width, py = rr() * rc.c.height, r = 3 + rr() * 9;
    rc.x.fillStyle = `rgba(70,70,70,0.5)`;
    rc.x.beginPath(); rc.x.arc(px, py, r, 0, Math.PI * 2); rc.x.fill();
  }
  return { map: toTexture(c, { aniso: 16 }), roughnessMap: toTexture(rc.c, { srgb: false, aniso: 8 }) };
}

/* ------------------------------------------------------------- cuticle */

/**
 * Generic sclerite map: dark brown-black chitin with micro-sculpture and a
 * faint mottling. `pattern` injects species markings.
 */
export function makeCuticle({ w = 1024, h = 1024, base = '#171310', pattern = null, seed = 12, scaleTex = 1 } = {}) {
  const { c, x } = canvas(w, h);
  x.fillStyle = base; x.fillRect(0, 0, w, h);

  const field = fbmField(w, h, 5, seed);
  const img = x.getImageData(0, 0, w, h);
  const d = img.data;
  for (let i = 0; i < w * h; i++) {
    const n = (field[i] - 0.5) * 46;
    const k = i * 4;
    d[k] = THREE.MathUtils.clamp(d[k] + n, 0, 255);
    d[k + 1] = THREE.MathUtils.clamp(d[k + 1] + n * 0.92, 0, 255);
    d[k + 2] = THREE.MathUtils.clamp(d[k + 2] + n * 0.80, 0, 255);
  }
  x.putImageData(img, 0, 0);

  if (pattern) pattern(x, w, h);
  return toTexture(c, { aniso: 16, repeat: [scaleTex, scaleTex] });
}

/** S14: the silver "lyre" of pale scales on the Aedes scutum, plus median line. */
export function scutumPattern(x, w, h) {
  const silver = 'rgba(216,219,206,0.92)';
  const dim = 'rgba(170,175,164,0.55)';
  // Median longitudinal pale line (runs front→back down the middle)
  x.fillStyle = dim;
  x.fillRect(w * 0.485, h * 0.06, w * 0.03, h * 0.80);
  // Lyre: two curved arms sweeping out then closing back toward the scutellum
  x.strokeStyle = silver; x.lineCap = 'round';
  x.lineWidth = w * 0.030;
  for (const s of [-1, 1]) {
    x.beginPath();
    x.moveTo(w * (0.5 + s * 0.055), h * 0.14);
    x.bezierCurveTo(
      w * (0.5 + s * 0.30), h * 0.26,
      w * (0.5 + s * 0.33), h * 0.52,
      w * (0.5 + s * 0.20), h * 0.70);
    x.stroke();
    // posterior branch of the lyre
    x.beginPath();
    x.moveTo(w * (0.5 + s * 0.20), h * 0.70);
    x.bezierCurveTo(
      w * (0.5 + s * 0.11), h * 0.78,
      w * (0.5 + s * 0.07), h * 0.84,
      w * (0.5 + s * 0.045), h * 0.88);
    x.stroke();
  }
  // Scutellar spot pair
  x.fillStyle = silver;
  x.beginPath(); x.ellipse(w * 0.40, h * 0.93, w * 0.035, h * 0.022, 0, 0, 7); x.fill();
  x.beginPath(); x.ellipse(w * 0.60, h * 0.93, w * 0.035, h * 0.022, 0, 0, 7); x.fill();

  // Break the solid lines into scale texture
  const rnd = mulberry(93);
  x.globalCompositeOperation = 'destination-out';
  for (let i = 0; i < 5000; i++) {
    x.fillStyle = `rgba(0,0,0,${0.25 + rnd() * 0.5})`;
    x.fillRect(rnd() * w, rnd() * h, 2 + rnd() * 4, 2 + rnd() * 3);
  }
  x.globalCompositeOperation = 'source-over';
}

/* --------------------------------------------------------------- legs */

/**
 * Leg strip map. U runs along the leg (femur → tarsus → claw), V around the
 * circumference. S14/S15: pale inter-articular rings at every joint, a
 * longitudinal pale line down the fore/mid femur, a white knee spot, and
 * pale tarsal bands.
 */
export function makeLegStrip({ bands = 4, femurStripe = true, kneeSpot = true, tibialBands = 1, seed = 5 } = {}) {
  const W = 2048, H = 256;
  const { c, x } = canvas(W, H);
  // Base chitin
  x.fillStyle = '#1b1510'; x.fillRect(0, 0, W, H);
  const field = fbmField(W, H, 4, seed, 0.55);
  const img = x.getImageData(0, 0, W, H); const d = img.data;
  for (let i = 0; i < W * H; i++) {
    const n = (field[i] - 0.5) * 40, k = i * 4;
    d[k] = THREE.MathUtils.clamp(d[k] + n * 1.0, 0, 255);
    d[k + 1] = THREE.MathUtils.clamp(d[k + 1] + n * 0.8, 0, 255);
    d[k + 2] = THREE.MathUtils.clamp(d[k + 2] + n * 0.6, 0, 255);
  }
  x.putImageData(img, 0, 0);

  const pale = (a) => `rgba(226,228,214,${a})`;
  // Segment boundaries (fractions of total leg length) — coxa+femur+tibia+tarsus+claw
  const marks = [0.0, 0.055, 0.075, 0.345, 0.370, 0.720, 0.745, 0.900, 0.955, 1.0];
  for (const m of marks) {
    const px = m * W, wdt = W * 0.006;
    x.fillStyle = pale(0.85); x.fillRect(px - wdt * 0.5, 0, wdt, H);
  }
  // Longitudinal pale line down the femur (dorsal side => v near 0.25 and 0.75)
  if (femurStripe) {
    for (const v of [0.245, 0.755]) {
      x.fillStyle = pale(0.75);
      x.fillRect(W * 0.075, v * H - H * 0.035, W * 0.272, H * 0.07);
    }
  }
  // White knee spot (distal femur, dorsolateral)
  if (kneeSpot) {
    for (const v of [0.16, 0.84]) {
      x.beginPath();
      x.ellipse(W * 0.322, v * H, W * 0.020, H * 0.062, 0, 0, 7);
      x.fillStyle = pale(0.9); x.fill();
    }
  }
  // Tibial inter-articular rings
  for (let i = 0; i < tibialBands; i++) {
    const m = 0.395 + i * 0.135;
    x.fillStyle = pale(0.72); x.fillRect(m * W, 0, W * 0.008, H);
  }
  // Tarsal bands (S14: fore tarsus pale-banded, 5th tarsomere strongly so)
  for (let i = 0; i < bands; i++) {
    const m = 0.760 + i * 0.038;
    x.fillStyle = pale(0.55 + i * 0.1);
    x.fillRect(m * W, 0, W * 0.010, H);
  }
  // Setae: fine dark spines along the whole leg, denser on tibia/tarsus
  const rnd = mulberry(seed * 31 + 7);
  x.strokeStyle = 'rgba(8,6,4,0.9)';
  for (let i = 0; i < 2600; i++) {
    const px = rnd() * W, py = rnd() * H, len = 3 + rnd() * 11;
    const a = (py < H * 0.5 ? 0 : Math.PI) + (rnd() - 0.5) * 0.5;
    x.lineWidth = 0.6 + rnd() * 0.8;
    x.beginPath(); x.moveTo(px, py);
    x.lineTo(px + Math.cos(a) * len * 0.3, py + Math.sin(a) * len);
    x.stroke();
  }
  // Long tibial spurs
  for (let i = 0; i < 40; i++) {
    const px = (0.38 + rnd() * 0.33) * W, py = rnd() * H, len = 14 + rnd() * 22;
    x.strokeStyle = 'rgba(30,22,14,0.95)'; x.lineWidth = 1.4;
    x.beginPath(); x.moveTo(px, py); x.lineTo(px, py + (py < H / 2 ? -len : len)); x.stroke();
  }
  return toTexture(c, { aniso: 16 });
}

/* -------------------------------------------------------------- wings */

/**
 * Wing membrane map. U along span, V across chord (0 = leading edge).
 * Contains: microtrichia field, lanceolate scale rows that thicken toward
 * the costa and the wing base, the S14 pale costal patch, dark scale spots,
 * and a marginal fringe band.
 */
export function makeWingMaps(size = 1024) {
  const W = size, H = size >> 1;
  const { c, x } = canvas(W, H);
  // Membrane: warm translucent grey with vein shadowing
  x.fillStyle = '#b9b2a4'; x.fillRect(0, 0, W, H);
  const field = fbmField(W, H, 4, 41, 0.5);
  const img = x.getImageData(0, 0, W, H); const d = img.data;
  for (let i = 0; i < W * H; i++) {
    const n = (field[i] - 0.5) * 34, k = i * 4;
    d[k] = THREE.MathUtils.clamp(d[k] + n, 0, 255);
    d[k + 1] = THREE.MathUtils.clamp(d[k + 1] + n * 1.0, 0, 255);
    d[k + 2] = THREE.MathUtils.clamp(d[k + 2] + n * 1.05, 0, 255);
  }
  x.putImageData(img, 0, 0);

  const rnd = mulberry(1907);
  // Lanceolate scales: denser and darker toward the costa and the base.
  // They are drawn as tiny overlapping leaf shapes aligned with the span.
  for (let i = 0; i < 26000; i++) {
    const u = Math.pow(rnd(), 0.72);          // bias toward the base
    const v = Math.pow(rnd(), 1.9);           // bias toward the leading edge
    const px = u * W, py = v * H;
    const prox = 1 - v * 0.72 - (1 - u) * 0.18;  // scale density factor
    if (rnd() > prox * 0.92) continue;
    const sl = 3.0 + rnd() * 6.5, sw = 1.0 + rnd() * 1.5;
    const dark = rnd() < 0.30;
    x.fillStyle = dark
      ? `rgba(30,26,20,${0.55 + rnd() * 0.4})`
      : `rgba(${88 + rnd() * 40 | 0},${80 + rnd() * 34 | 0},${70 + rnd() * 30 | 0},${0.35 + rnd() * 0.45})`;
    x.save(); x.translate(px, py); x.rotate((rnd() - 0.5) * 0.5);
    x.beginPath(); x.ellipse(0, 0, sw, sl, 0, 0, 7); x.fill(); x.restore();
  }
  // Microtrichia — the tiny hair-like microtrichia of the membrane (S15)
  x.strokeStyle = 'rgba(60,56,48,0.5)';
  for (let i = 0; i < 16000; i++) {
    const px = rnd() * W, py = rnd() * H, len = 1.5 + rnd() * 4;
    x.lineWidth = 0.5;
    x.beginPath(); x.moveTo(px, py);
    x.lineTo(px + (rnd() - 0.5) * 3, py + len * (0.4 + rnd())); x.stroke();
  }
  // S14: pale patch at the base of the costa
  x.fillStyle = 'rgba(232,231,219,0.90)';
  x.beginPath(); x.ellipse(W * 0.055, H * 0.10, W * 0.048, H * 0.115, 0, 0, 7); x.fill();
  // Dark scale spots
  for (const s of [[0.30, 0.30, 0.028, 0.6], [0.52, 0.52, 0.024, 0.55], [0.63, 0.20, 0.018, 0.45]]) {
    x.fillStyle = `rgba(24,20,16,${s[3]})`;
    x.beginPath(); x.ellipse(s[0] * W, s[1] * H, s[2] * W, s[2] * W * 1.5, 0, 0, 7); x.fill();
  }
  // Marginal fringe: long scales at the trailing edge (v -> 1)
  for (let i = 0; i < 5200; i++) {
    const px = rnd() * W, py = H - Math.pow(rnd(), 1.6) * H * 0.30;
    const sl = 4 + rnd() * 9;
    x.strokeStyle = `rgba(${60 + rnd() * 40 | 0},${56 + rnd() * 34 | 0},${48 + rnd() * 28 | 0},0.75)`;
    x.lineWidth = 0.9 + rnd();
    x.beginPath(); x.moveTo(px, py); x.lineTo(px + (rnd() - 0.5) * 2, py + sl); x.stroke();
  }

  // Alpha map: the membrane is thinner (more transparent) between veins,
  // opaque where scales overlap, fully opaque at the fringe.
  const ac = canvas(W, H);
  ac.x.fillStyle = '#4c4c4c'; ac.x.fillRect(0, 0, W, H);
  const af = fbmField(W, H, 4, 63, 0.55);
  const aimg = ac.x.getImageData(0, 0, W, H); const ad = aimg.data;
  for (let i = 0; i < W * H; i++) {
    const k = i * 4, n = af[i];
    ad[k] = ad[k + 1] = ad[k + 2] = THREE.MathUtils.clamp(40 + n * 130, 0, 255);
    ad[k + 3] = 255;
  }
  ac.x.putImageData(aimg, 0, 0);
  ac.x.fillStyle = 'rgba(255,255,255,0.85)';
  for (let i = 0; i < 3000; i++) { ac.x.fillRect(rnd() * W, H - Math.pow(rnd(), 1.5) * H * 0.35, 1.5, 3 + rnd() * 5); }

  return { map: toTexture(c, { aniso: 16 }), alphaMap: toTexture(ac.c, { srgb: false, aniso: 8 }) };
}

/* ---------------------------------------------------------- abdomen */

/** Abdomen tergite map: pale basal bands (S14) + dark ground scales. */
export function makeAbdomenMap(w = 1024, h = 512) {
  const { c, x } = canvas(w, h);
  x.fillStyle = '#14100c'; x.fillRect(0, 0, w, h);
  const field = fbmField(w, h, 4, 311, 0.55);
  const img = x.getImageData(0, 0, w, h); const d = img.data;
  for (let i = 0; i < w * h; i++) {
    const n = (field[i] - 0.5) * 34, k = i * 4;
    d[k] = THREE.MathUtils.clamp(d[k] + n, 0, 255);
    d[k + 1] = THREE.MathUtils.clamp(d[k + 1] + n * 0.85, 0, 255);
    d[k + 2] = THREE.MathUtils.clamp(d[k + 2] + n * 0.7, 0, 255);
  }
  x.putImageData(img, 0, 0);

  // Pale basal bands. U runs head→tip; the tergites are laid out linearly.
  const segs = 8, first = 1, last = 6;
  for (let i = first; i <= last; i++) {
    const u0 = i / segs;
    const x0 = u0 * w, bw = w * (0.020 + (i % 2) * 0.004);
    // The band wraps the tergite, with a slight dorsal dip (the classic
    // "triangle" of pale scales on Culicine tergites).
    const g = x.createLinearGradient(0, 0, 0, h);
    g.addColorStop(0.00, 'rgba(214,216,203,0.20)');
    g.addColorStop(0.18, 'rgba(226,228,215,0.95)');
    g.addColorStop(0.50, 'rgba(232,234,220,1.00)');
    g.addColorStop(0.82, 'rgba(226,228,215,0.95)');
    g.addColorStop(1.00, 'rgba(214,216,203,0.20)');
    x.fillStyle = g; x.fillRect(x0, 0, bw, h);
    // Rounded leading edge of the band
    x.beginPath(); x.ellipse(x0 + bw * 0.5, 0, bw * 0.5, h * 0.5, 0, 0, 7); x.fill();
  }
  // Terminal tergite pale crown
  x.fillStyle = 'rgba(214,216,205,0.55)';
  x.fillRect(w * 0.955, 0, w * 0.045, h);

  // Segment sutures
  x.fillStyle = 'rgba(0,0,0,0.65)';
  for (let i = 1; i < segs; i++) x.fillRect((i / segs) * w - 1.5, 0, 3, h);

  // Scale speckle to break up the flat bands
  const rnd = mulberry(555);
  x.globalCompositeOperation = 'destination-out';
  for (let i = 0; i < 9000; i++) {
    x.fillStyle = `rgba(0,0,0,${0.2 + rnd() * 0.5})`;
    x.beginPath(); x.ellipse(rnd() * w, rnd() * h, 1 + rnd() * 2.5, 2 + rnd() * 3, 0, 0, 7); x.fill();
  }
  x.globalCompositeOperation = 'source-over';
  return toTexture(c, { aniso: 16 });
}

/* ------------------------------------------------------------ world */

/** Terrain albedo: dry cracked earth + gravel + moss in the crevices. */
export function makeTerrainMap(size = 1024) {
  const { c, x } = canvas(size, size);
  x.fillStyle = '#4a4136'; x.fillRect(0, 0, size, size);
  const f1 = fbmField(size, size, 6, 88, 0.55);
  const f2 = fbmField(size, size, 4, 132, 0.5);
  const img = x.getImageData(0, 0, size, size); const d = img.data;
  for (let i = 0; i < size * size; i++) {
    const a = f1[i], b = f2[i], k = i * 4;
    const moss = Math.max(0, (b - 0.58)) * 2.4;
    let r = 96 + a * 78, g = 84 + a * 68, bl = 68 + a * 52;
    r = r * (1 - moss * 0.6) + 54 * moss * 0.6;
    g = g * (1 - moss * 0.6) + 76 * moss * 0.6;
    bl = bl * (1 - moss * 0.6) + 40 * moss * 0.6;
    d[k] = r; d[k + 1] = g; d[k + 2] = bl; d[k + 3] = 255;
  }
  x.putImageData(img, 0, 0);
  // Gravel
  const rnd = mulberry(1010);
  for (let i = 0; i < 24000; i++) {
    const px = rnd() * size, py = rnd() * size, r = 0.6 + rnd() * 3.4;
    const l = 60 + rnd() * 90;
    x.fillStyle = `rgba(${l | 0},${(l * 0.94) | 0},${(l * 0.86) | 0},${0.25 + rnd() * 0.5})`;
    x.beginPath(); x.ellipse(px, py, r, r * (0.6 + rnd() * 0.6), rnd() * 3, 0, 7); x.fill();
  }
  // Cracks
  x.strokeStyle = 'rgba(26,22,18,0.55)';
  for (let i = 0; i < 220; i++) {
    let px = rnd() * size, py = rnd() * size, a = rnd() * Math.PI * 2;
    x.lineWidth = 0.5 + rnd() * 1.6;
    x.beginPath(); x.moveTo(px, py);
    for (let k = 0; k < 14; k++) {
      a += (rnd() - 0.5) * 1.1; px += Math.cos(a) * 8; py += Math.sin(a) * 8;
      x.lineTo(px, py);
    }
    x.stroke();
  }
  return toTexture(c, { aniso: 16, repeat: [1, 1] });
}

/** Painted steel: the fuel tank. Base coat + chipping + weld seams. */
export function makeTankMap(size = 1024) {
  const { c, x } = canvas(size, size);
  x.fillStyle = '#4d5a4a'; x.fillRect(0, 0, size, size);
  const f = fbmField(size, size, 5, 404, 0.6);
  const img = x.getImageData(0, 0, size, size); const d = img.data;
  for (let i = 0; i < size * size; i++) {
    const n = (f[i] - 0.5) * 60, k = i * 4;
    d[k] = THREE.MathUtils.clamp(d[k] + n, 0, 255);
    d[k + 1] = THREE.MathUtils.clamp(d[k + 1] + n, 0, 255);
    d[k + 2] = THREE.MathUtils.clamp(d[k + 2] + n * 0.8, 0, 255);
  }
  x.putImageData(img, 0, 0);
  const rnd = mulberry(707);
  // Paint chipping revealing primer / bare metal
  for (let i = 0; i < 1400; i++) {
    const px = rnd() * size, py = rnd() * size, r = 1 + rnd() * 7;
    x.fillStyle = rnd() < 0.6 ? `rgba(96,72,48,${0.3 + rnd() * 0.5})` : `rgba(120,120,124,${0.3 + rnd() * 0.45})`;
    x.beginPath(); x.ellipse(px, py, r, r * (0.4 + rnd()), rnd() * 3, 0, 7); x.fill();
  }
  // Vertical weld seams
  for (const u of [0.16, 0.5, 0.84]) {
    const g = x.createLinearGradient(u * size - 6, 0, u * size + 6, 0);
    g.addColorStop(0, 'rgba(0,0,0,0)');
    g.addColorStop(0.45, 'rgba(22,22,20,0.5)');
    g.addColorStop(0.5, 'rgba(160,158,150,0.42)');
    g.addColorStop(0.55, 'rgba(22,22,20,0.5)');
    g.addColorStop(1, 'rgba(0,0,0,0)');
    x.fillStyle = g; x.fillRect(u * size - 6, 0, 12, size);
  }
  // Dirt streaks
  for (let i = 0; i < 300; i++) {
    const px = rnd() * size, py = rnd() * size, h = 30 + rnd() * 220;
    const g = x.createLinearGradient(0, py, 0, py + h);
    g.addColorStop(0, 'rgba(30,26,18,0.0)');
    g.addColorStop(0.3, `rgba(34,28,20,${0.10 + rnd() * 0.22})`);
    g.addColorStop(1, 'rgba(30,26,18,0)');
    x.fillStyle = g; x.fillRect(px, py, 2 + rnd() * 9, h);
  }
  return toTexture(c, { aniso: 16 });
}

/** Stencilled unit markings for the tank (alpha decal sheet). */
export function makeStencilAtlas(size = 512) {
  const { c, x } = canvas(size, size);
  x.clearRect(0, 0, size, size);
  const cell = size / 2;
  const draw = (cx, cy, txt, sub, colour) => {
    x.save(); x.translate(cx, cy);
    x.fillStyle = colour; x.strokeStyle = colour;
    x.font = `bold ${cell * 0.30}px "Arial Black", Impact, sans-serif`;
    x.textAlign = 'center'; x.textBaseline = 'middle';
    x.fillText(txt, 0, -cell * 0.06);
    x.font = `bold ${cell * 0.11}px Arial, sans-serif`;
    x.fillText(sub, 0, cell * 0.16);
    // Stencil bridges
    x.globalCompositeOperation = 'destination-out';
    x.fillRect(-cell, -cell * 0.02, cell * 2, cell * 0.022);
    x.globalCompositeOperation = 'source-over';
    x.restore();
  };
  draw(cell * 0.5, cell * 0.5, 'JP-8', 'JET A-1  ·  NO SMOKE', 'rgba(226,228,214,0.94)');
  draw(cell * 1.5, cell * 0.5, 'ARMY', 'FUEL CELL 04-7731', 'rgba(226,228,214,0.94)');
  return toTexture(c, { aniso: 8 });
}

/** Soft radial alpha blob used for the blood bolus, saliva droplets, shadows. */
export function makeRadialAlpha(size = 128, power = 2.0) {
  const { c, x } = canvas(size, size);
  const img = x.createImageData(size, size);
  for (let y = 0; y < size; y++) for (let X = 0; X < size; X++) {
    const dx = (X / size - 0.5) * 2, dy = (y / size - 0.5) * 2;
    const r = Math.min(1, Math.sqrt(dx * dx + dy * dy));
    const a = Math.pow(1 - r, power) * 255;
    const k = (y * size + X) * 4;
    img.data[k] = img.data[k + 1] = img.data[k + 2] = 255;
    img.data[k + 3] = a;
  }
  x.putImageData(img, 0, 0);
  const t = toTexture(c, { srgb: false }); t.wrapS = t.wrapT = THREE.ClampToEdgeWrapping;
  return t;
}

export { fbmField, mulberry };

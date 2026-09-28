import * as THREE from 'three';

// Procedural feather textures. A feather vane is mapped with
//   u ∈ [0,1]  : 0 = outer-vane edge, 0.5 = rachis, 1 = inner-vane edge (normalised to local vane width)
//   v ∈ [0,1]  : 0 = base of vane (calamus end), 1 = tip
// so the frayed barb ends in the texture always coincide with the geometric outline.
//
// Channels:  albedo map  R,G,B = luminance modulation (rachis, barbs, barbules), A = coverage
//            normal map  derived from a barb height field (ridges running out from the rachis at ~35°)

function rng(seed) { let s = seed >>> 0; return () => ((s = (s * 1664525 + 1013904223) >>> 0) / 4294967296); }

function makeCanvas(w, h) { const c = document.createElement('canvas'); c.width = w; c.height = h; return c; }

// barb field: returns {lum, alpha, height} arrays
function featherField(W, H, opts) {
  const { barbs = 150, angle = 0.62, rachis = 0.022, fray = 0.1, downBase = 0.0, gap = 0.0, seed = 1, contour = false } = opts;
  const R = rng(seed);
  const lum = new Float32Array(W * H), alpha = new Float32Array(W * H), height = new Float32Array(W * H);
  // per-barb random: brightness, fray length, and occasional split ("zip" separations) in the vane
  const nb = barbs + 8;
  const bLum = new Float32Array(nb * 2), bFray = new Float32Array(nb * 2), bSplit = new Float32Array(nb * 2);
  for (let i = 0; i < nb * 2; i++) { bLum[i] = 0.96 + R() * 0.08; bFray[i] = R(); bSplit[i] = R() < gap ? 1 : 0; }
  const tanA = Math.tan(angle);
  for (let y = 0; y < H; y++) {
    const v = (y + 0.5) / H;
    for (let x = 0; x < W; x++) {
      const u = (x + 0.5) / W;
      const side = u < 0.5 ? 0 : 1;
      const d = Math.abs(u - 0.5) * 2; // 0 at rachis, 1 at edge
      // barbs leave the rachis angled toward the tip: phase increases with v and with distance d
      const ph = (v - d * tanA * 0.09) * barbs;
      const bi = Math.floor(ph) + 8;
      const f = ph - Math.floor(ph);
      const k = (bi % nb) * 2 + side;
      // barb ridge profile (rounded ridge + dark groove between barbs)
      const ridge = Math.sin(Math.PI * f);
      let l = 0.92 + 0.08 * ridge;
      l *= bLum[k];
      // barbule sheen darker near rachis, lighter at margins
      l *= 0.95 + 0.07 * d;
      let a = 1;
      // frayed edge: each barb ends at a slightly different distance
      const edge = 1 - fray * (0.35 + 0.65 * bFray[k]);
      if (d > edge) a = Math.max(0, 1 - (d - edge) / (fray * 0.35 + 1e-3)) * (ridge > 0.35 ? 1 : 0);
      // zipper splits in the vane (natural separations between barbs)
      if (bSplit[k] && d > 0.25 && f < 0.28) { a *= 0.0; }
      // downy base (afterfeather/plumulaceous barbs) — fluffy, semi-transparent
      if (v < downBase) {
        const q = v / downBase;
        const n = R();
        a *= q * q * (0.5 + 0.5 * n) + (n > 0.55 ? 0.35 : 0);
        l *= 0.9 + 0.2 * n;
      }
      if (contour) { // contour feathers: rounded outline inside the card, fluffier tips
        const dy = (v - 0.55) / 0.45;
        const r2 = d * d + Math.max(0, dy) ** 2 * 1.0;
        if (r2 > 1.0 - fray * 0.6 * bFray[k]) a = 0;
      }
      // rachis
      const rw = rachis * (1.25 - 0.9 * v);
      if (Math.abs(u - 0.5) < rw) { l = 0.98 + 0.12 * (1 - v); a = v < 0.995 ? 1 : 0; }
      const i = y * W + x;
      lum[i] = l; alpha[i] = a; height[i] = ridge * 0.35 + (Math.abs(u - 0.5) < rw ? 0.8 : 0);
    }
  }
  return { lum, alpha, height };
}

function toTextures(W, H, field, normalStrength = 2.2) {
  const c = makeCanvas(W, H), g = c.getContext('2d');
  const img = g.createImageData(W, H);
  const n = makeCanvas(W, H), gn = n.getContext('2d');
  const nimg = gn.createImageData(W, H);
  for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
      const i = y * W + x;
      // canvas row 0 is the top of the image; three flips Y so v=1 is at the top → write v up
      const o = ((H - 1 - y) * W + x) * 4;
      const L = Math.min(255, Math.round(field.lum[i] * 215));
      img.data[o] = L; img.data[o + 1] = L; img.data[o + 2] = L; img.data[o + 3] = Math.round(field.alpha[i] * 255);
      const hx = field.height[y * W + Math.min(W - 1, x + 1)] - field.height[y * W + Math.max(0, x - 1)];
      const hy = field.height[Math.min(H - 1, y + 1) * W + x] - field.height[Math.max(0, y - 1) * W + x];
      let nx = -hx * normalStrength, ny = -hy * normalStrength, nz = 1;
      const len = Math.hypot(nx, ny, nz); nx /= len; ny /= len; nz /= len;
      nimg.data[o] = Math.round((nx * 0.5 + 0.5) * 255);
      nimg.data[o + 1] = Math.round((ny * 0.5 + 0.5) * 255);
      nimg.data[o + 2] = Math.round((nz * 0.5 + 0.5) * 255);
      nimg.data[o + 3] = 255;
    }
  }
  g.putImageData(img, 0, 0); gn.putImageData(nimg, 0, 0);
  const map = new THREE.CanvasTexture(c);
  map.colorSpace = THREE.SRGBColorSpace;
  const normalMap = new THREE.CanvasTexture(n);
  for (const t of [map, normalMap]) {
    t.anisotropy = 8; t.generateMipmaps = true; t.minFilter = THREE.LinearMipmapLinearFilter; t.magFilter = THREE.LinearFilter;
    t.wrapS = t.wrapT = THREE.ClampToEdgeWrapping;
  }
  return { map, normalMap };
}

let cache = null;
export function featherTextures() {
  if (cache) return cache;
  cache = {
    // flight feathers (primaries/secondaries/rectrices): stiff pennaceous vanes, fine barbs
    flight: toTextures(256, 1024, featherField(256, 1024, { barbs: 190, angle: 0.55, rachis: 0.018, fray: 0.05, gap: 0.035, seed: 7 })),
    // coverts: softer, shorter, slightly frayed tips
    covert: toTextures(256, 512, featherField(256, 512, { barbs: 90, angle: 0.7, rachis: 0.02, fray: 0.12, gap: 0.05, downBase: 0.12, seed: 11 })),
    // body contour feathers: rounded tips, downy bases
    contour: toTextures(128, 256, featherField(128, 256, { barbs: 44, angle: 0.8, rachis: 0.03, fray: 0.28, gap: 0.08, downBase: 0.3, seed: 23, contour: true }), 1.6),
  };
  return cache;
}

// Scaled skin of the tarsus / toes: hexagonal reticulate scales on the tarsus, transverse scutes on toes
export function scaleTextures() {
  const W = 512, H = 512;
  const c = makeCanvas(W, H), g = c.getContext('2d');
  const n = makeCanvas(W, H), gn = n.getContext('2d');
  const hf = new Float32Array(W * H);
  const R = rng(99);
  // jittered hex cells (Voronoi) for the reticulate scales
  const pts = [];
  const cols = 22, rows = 26;
  for (let j = 0; j < rows; j++) for (let i = 0; i < cols; i++) {
    pts.push([(i + (j % 2) * 0.5 + (R() - 0.5) * 0.5) / cols * W, (j + (R() - 0.5) * 0.5) / rows * H]);
  }
  const lumA = new Float32Array(W * H);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    let d1 = 1e9, d2 = 1e9, id = 0;
    const cj = Math.round(y / H * rows), ci = Math.round(x / W * cols);
    for (let jj = cj - 2; jj <= cj + 2; jj++) for (let ii = ci - 2; ii <= ci + 2; ii++) {
      const k = (((jj % rows) + rows) % rows) * cols + (((ii % cols) + cols) % cols);
      let dx = Math.abs(x - pts[k][0]); dx = Math.min(dx, W - dx);
      let dy = Math.abs(y - pts[k][1]); dy = Math.min(dy, H - dy);
      const d = dx * dx + dy * dy;
      if (d < d1) { d2 = d1; d1 = d; id = k; } else if (d < d2) d2 = d;
    }
    const e = Math.sqrt(d2) - Math.sqrt(d1); // distance to the cell border
    const hgt = Math.min(1, e / 6);
    hf[y * W + x] = Math.sqrt(hgt);
    lumA[y * W + x] = (0.78 + 0.22 * Math.sqrt(hgt)) * (0.92 + 0.16 * ((id * 7919) % 13) / 13);
  }
  const img = g.createImageData(W, H), nimg = gn.createImageData(W, H);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const i = y * W + x, o = i * 4;
    const L = Math.round(lumA[i] * 235);
    img.data[o] = L; img.data[o + 1] = L; img.data[o + 2] = L; img.data[o + 3] = 255;
    const hx = hf[y * W + ((x + 1) % W)] - hf[y * W + ((x - 1 + W) % W)];
    const hy = hf[((y + 1) % H) * W + x] - hf[((y - 1 + H) % H) * W + x];
    let nx = -hx * 3, ny = hy * 3, nz = 1; const len = Math.hypot(nx, ny, nz);
    nimg.data[o] = (nx / len * 0.5 + 0.5) * 255; nimg.data[o + 1] = (ny / len * 0.5 + 0.5) * 255; nimg.data[o + 2] = (nz / len * 0.5 + 0.5) * 255; nimg.data[o + 3] = 255;
  }
  g.putImageData(img, 0, 0); gn.putImageData(nimg, 0, 0);
  const map = new THREE.CanvasTexture(c); map.colorSpace = THREE.SRGBColorSpace;
  const normalMap = new THREE.CanvasTexture(n);
  for (const t of [map, normalMap]) { t.wrapS = t.wrapT = THREE.RepeatWrapping; t.anisotropy = 8; }
  return { map, normalMap };
}

// Fine keratin grain for the beak (bill has fine longitudinal striations and a smooth glossy surface)
export function noiseTexture(size = 256, seed = 5, scale = 1) {
  const c = makeCanvas(size, size), g = c.getContext('2d');
  const img = g.createImageData(size, size);
  const R = rng(seed);
  const base = new Float32Array(size * size);
  for (let i = 0; i < base.length; i++) base[i] = R();
  // two octaves of box-blurred value noise
  const blur = (src, r) => { const out = new Float32Array(src.length); for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) { let s = 0, n = 0; for (let k = -r; k <= r; k++) { s += src[y * size + ((x + k + size) % size)]; n++; } out[y * size + x] = s / n; } const o2 = new Float32Array(src.length); for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) { let s = 0, n = 0; for (let k = -r; k <= r; k++) { s += out[((y + k + size) % size) * size + x]; n++; } o2[y * size + x] = s / n; } return o2; };
  const a = blur(base, 2 * scale), b = blur(base, 7 * scale);
  for (let i = 0; i < base.length; i++) { const v = (a[i] - 0.5) * 1.6 + (b[i] - 0.5) * 3 + 0.5; const L = Math.max(0, Math.min(255, v * 255)); img.data[i * 4] = img.data[i * 4 + 1] = img.data[i * 4 + 2] = L; img.data[i * 4 + 3] = 255; }
  g.putImageData(img, 0, 0);
  const t = new THREE.CanvasTexture(c); t.wrapS = t.wrapT = THREE.RepeatWrapping;
  return t;
}

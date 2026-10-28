//============================================================================================================================================
//                                                              TEXTURES.JS
//============================================================================================================================================
// Every surface map in the editor is drawn procedurally into a <canvas> at runtime: no image files, nothing to fetch,
// so the whole thing still opens straight off a raw file host.
//
// Each generator produces a colour map plus a derived normal map. The normal map is Sobel-filtered from a height
// canvas drawn alongside the colour pass, which is what gives the joints between pavers real relief under the studio
// sun rather than a painted-on grid.
//
// Tiles are authored in METRES. The mesh generators write UVs in metres too (u = distance along the corridor,
// v = metres across the section), so a texture is tiled simply by setting repeat = 1 / tileMetres — paving stays the
// same physical size whatever the corridor width, and never stretches around a curve.

const cache = new Map();

// ── paving catalogue ──────────────────────────────────────────────────────────────────────────────────────────────

export const PAVING_PATTERNS = {
  concrete: { label: 'Cast concrete', tile: 1.8, kind: 'slab', unit: [0.9, 0.9], joint: 0.035, base: [168, 166, 160], spread: 10 },
  flagstone: { label: 'Stone flags', tile: 2.4, kind: 'stagger', unit: [1.2, 0.8], joint: 0.045, base: [150, 147, 138], spread: 16 },
  brick: { label: 'Running-bond brick', tile: 1.6, kind: 'stagger', unit: [0.4, 0.2], joint: 0.018, base: [148, 108, 92], spread: 18 },
  basket: { label: 'Basket-weave brick', tile: 1.6, kind: 'basket', unit: [0.4, 0.2], joint: 0.018, base: [141, 104, 90], spread: 18 },
  herringbone: { label: 'Herringbone pavers', tile: 1.8, kind: 'herringbone', unit: [0.45, 0.225], joint: 0.016, base: [136, 112, 98], spread: 16 },
  hex: { label: 'Hexagonal pavers', tile: 1.8, kind: 'hex', unit: [0.3, 0.3], joint: 0.02, base: [158, 156, 150], spread: 12 },
  cobble: { label: 'Cobblestone', tile: 1.4, kind: 'cobble', unit: [0.18, 0.18], joint: 0.03, base: [124, 123, 119], spread: 22 },
  granite: { label: 'Granite sett', tile: 1.2, kind: 'slab', unit: [0.2, 0.2], joint: 0.022, base: [104, 104, 108], spread: 20 },
  asphaltWalk: { label: 'Asphalt footway', tile: 3.0, kind: 'noise', base: [86, 86, 88], spread: 8 },
  gravelShoulder: { label: 'Gravel shoulder', tile: 2.0, kind: 'gravel', base: [138, 130, 115], spread: 26 },
  dirtShoulder: { label: 'Dirt verge', tile: 2.6, kind: 'dirt', base: [112, 98, 76], spread: 20 },
};

export const DEFAULT_PAVING = 'concrete';

// ── public API ────────────────────────────────────────────────────────────────────────────────────────────────────

// Returns { map, normalMap, tile } for a paving pattern, or null when no canvas is available (Node tooling).
export function pavingTexture(THREE, key, scale = 1) {
  const spec = PAVING_PATTERNS[key] || PAVING_PATTERNS[DEFAULT_PAVING];
  return build(THREE, `paving:${key}:${scale}`, spec.tile * scale, (ctx, h, px) => drawPaving(ctx, h, px, spec));
}

// Surfaces that are not user-selectable: carriageway asphalt, curb and deck concrete, steel, earth.
export function surfaceTexture(THREE, name) {
  const presets = {
    road: { tile: 6, draw: (c, h, px) => drawAsphalt(c, h, px, [56, 58, 62], 10) },
    'road:asphalt': { tile: 6, draw: (c, h, px) => drawAsphalt(c, h, px, [56, 58, 62], 10) },
    'road:chipseal': { tile: 4, draw: (c, h, px) => drawAsphalt(c, h, px, [84, 82, 78], 20) },
    'road:concrete': { tile: 5, draw: (c, h, px) => drawSlabRoad(c, h, px, [138, 138, 136]) },
    'road:setts': { tile: 1.4, draw: (c, h, px) => drawPaving(c, h, px, PAVING_PATTERNS.granite) },
    'road:gravel': { tile: 2.6, draw: (c, h, px) => drawGravel(c, h, px, [122, 115, 102], 28) },
    'road:dirt': { tile: 3.2, draw: (c, h, px) => drawDirt(c, h, px, [104, 90, 70], false) },
    'road:track': { tile: 4.0, draw: (c, h, px) => drawDirt(c, h, px, [100, 88, 70], true) },
    curb: { tile: 2.4, draw: (c, h, px) => drawConcrete(c, h, px, [164, 167, 172]) },
    deck: { tile: 4, draw: (c, h, px) => drawConcrete(c, h, px, [150, 152, 156]) },
    piers: { tile: 3, draw: (c, h, px) => drawConcrete(c, h, px, [138, 140, 144]) },
    roadbed: { tile: 3, draw: (c, h, px) => drawBoardform(c, h, px, [140, 142, 146]) },
    earth: { tile: 5, draw: (c, h, px) => drawEarth(c, h, px) },
    structure: { tile: 3, draw: (c, h, px) => drawConcrete(c, h, px, [128, 134, 142]) },
    barrier: { tile: 2, draw: (c, h, px) => drawConcrete(c, h, px, [156, 158, 162]) },
    driveway: { tile: 4.0, draw: (c, h, px) => drawSlabRoad(c, h, px, [150, 149, 145]) },
    gravelShoulder: { tile: 2.0, draw: (c, h, px) => drawGravel(c, h, px, [138, 130, 115], 26) },
    dirtShoulder: { tile: 2.6, draw: (c, h, px) => drawDirt(c, h, px, [112, 98, 76], false) },
  };
  const preset = presets[name];
  if (!preset) return null;
  return build(THREE, `surface:${name}`, preset.tile, preset.draw);
}

// Red octagon / yellow triangle sign faces. One texture holds every face we draw, laid out on a 2×2 atlas.
export function signTexture(THREE) {
  return build(THREE, 'signs', 1, drawSignAtlas, { size: 512, normals: false });
}

export function disposeTextures() {
  for (const entry of cache.values()) {
    entry.map?.dispose?.();
    entry.normalMap?.dispose?.();
  }
  cache.clear();
}

// ── canvas plumbing ───────────────────────────────────────────────────────────────────────────────────────────────

function build(THREE, key, tileMetres, draw, { size = 512, normals = true } = {}) {
  if (cache.has(key)) return cache.get(key);
  const canvas = makeCanvas(size, size);
  if (!canvas) return null;
  const ctx = canvas.getContext('2d');
  const height = makeCanvas(size, size);
  const hctx = height.getContext('2d');
  hctx.fillStyle = '#000';
  hctx.fillRect(0, 0, size, size);

  const pxPerMetre = size / tileMetres;
  draw(ctx, hctx, pxPerMetre);

  const map = new THREE.CanvasTexture(canvas);
  map.wrapS = map.wrapT = THREE.RepeatWrapping;
  map.colorSpace = THREE.SRGBColorSpace;
  map.anisotropy = 8;
  map.repeat.set(1 / tileMetres, 1 / tileMetres);

  let normalMap = null;
  if (normals) {
    normalMap = new THREE.CanvasTexture(normalFromHeight(hctx, size, 2.2));
    normalMap.wrapS = normalMap.wrapT = THREE.RepeatWrapping;
    normalMap.anisotropy = 8;
    normalMap.repeat.set(1 / tileMetres, 1 / tileMetres);
  }

  const entry = { map, normalMap, tile: tileMetres };
  cache.set(key, entry);
  return entry;
}

function makeCanvas(w, h) {
  if (typeof document !== 'undefined' && document.createElement) {
    const c = document.createElement('canvas');
    c.width = w;
    c.height = h;
    if (c.getContext && c.getContext('2d')) return c;
  }
  if (typeof OffscreenCanvas !== 'undefined') return new OffscreenCanvas(w, h);
  return null;
}

// Sobel the height canvas into a tangent-space normal map. Wrapping lookups keep the tile seamless.
function normalFromHeight(hctx, size, strength) {
  const src = hctx.getImageData(0, 0, size, size).data;
  const out = hctx.createImageData(size, size);
  const at = (x, y) => src[(((y + size) % size) * size + ((x + size) % size)) * 4] / 255;
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const dx =
        at(x - 1, y - 1) + 2 * at(x - 1, y) + at(x - 1, y + 1) -
        (at(x + 1, y - 1) + 2 * at(x + 1, y) + at(x + 1, y + 1));
      const dy =
        at(x - 1, y - 1) + 2 * at(x, y - 1) + at(x + 1, y - 1) -
        (at(x - 1, y + 1) + 2 * at(x, y + 1) + at(x + 1, y + 1));
      let nx = dx * strength;
      let ny = dy * strength;
      const nz = 1;
      const len = Math.hypot(nx, ny, nz) || 1;
      nx /= len;
      ny /= len;
      const i = (y * size + x) * 4;
      out.data[i] = (nx * 0.5 + 0.5) * 255;
      out.data[i + 1] = (ny * 0.5 + 0.5) * 255;
      out.data[i + 2] = ((1 / len) * 0.5 + 0.5) * 255;
      out.data[i + 3] = 255;
    }
  }
  const canvas = makeCanvas(size, size);
  canvas.getContext('2d').putImageData(out, 0, 0);
  return canvas;
}

// Deterministic per-tile jitter so a rebuild never reshuffles the paving.
function hash(x, y, seed = 0) {
  let h = Math.imul(x | 0, 374761393) ^ Math.imul(y | 0, 668265263) ^ Math.imul(seed, 2147483647);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}

const shade = (base, delta) =>
  `rgb(${clamp8(base[0] + delta)},${clamp8(base[1] + delta)},${clamp8(base[2] + delta)})`;
const clamp8 = (v) => Math.max(0, Math.min(255, Math.round(v)));

// Draws a polygon nine times (offset by ±tile) so shapes crossing the edge wrap instead of being clipped.
function wrapped(ctx, size, poly, paint) {
  for (let ox = -1; ox <= 1; ox++) {
    for (let oy = -1; oy <= 1; oy++) {
      ctx.save();
      ctx.translate(ox * size, oy * size);
      ctx.beginPath();
      ctx.moveTo(poly[0][0], poly[0][1]);
      for (let i = 1; i < poly.length; i++) ctx.lineTo(poly[i][0], poly[i][1]);
      ctx.closePath();
      paint(ctx);
      ctx.restore();
    }
  }
}

const rect = (x, y, w, h) => [[x, y], [x + w, y], [x + w, y + h], [x, y + h]];

// ── pattern painters ──────────────────────────────────────────────────────────────────────────────────────────────

function drawPaving(ctx, hctx, px, spec) {
  // Loose surfaces have no pavers to lay out — hand them to the aggregate generators instead.
  if (spec.kind === 'gravel') return drawGravel(ctx, hctx, px, spec.base, spec.spread);
  if (spec.kind === 'dirt') return drawDirt(ctx, hctx, px, spec.base, false);
  const size = ctx.canvas.width;
  // joint colour / zero height underneath every paver
  ctx.fillStyle = shade(spec.base, -46);
  ctx.fillRect(0, 0, size, size);
  speckle(ctx, size, 0.05);

  const pieces = layout(spec, px, size);
  pieces.forEach((piece, index) => {
    const tone = (hash(piece.key[0], piece.key[1], index & 7) - 0.5) * 2 * spec.spread;
    wrapped(ctx, size, piece.poly, (c) => {
      c.fillStyle = shade(spec.base, tone);
      c.fill();
    });
    wrapped(hctx, size, piece.poly, (c) => {
      c.fillStyle = `rgb(${clamp8(200 + tone * 0.6)},${clamp8(200 + tone * 0.6)},${clamp8(200 + tone * 0.6)})`;
      c.fill();
    });
  });
  speckle(ctx, size, 0.045);
  blotches(ctx, size, spec.base, 14);
}

// Returns the paver polygons for a pattern in pixel space, inset by the joint width.
function layout(spec, px, size) {
  const out = [];
  const joint = Math.max(1.2, (spec.joint || 0.02) * px);
  const push = (poly, key) => out.push({ poly, key });

  if (spec.kind === 'noise') return out;

  if (spec.kind === 'hex') {
    const r = (spec.unit[0] * px) / 2;
    // Pointy-top hexes: round the grid to whole cells so the tile stays seamless.
    const cols = Math.max(2, Math.round(size / (Math.sqrt(3) * r)));
    const rows = Math.max(2, Math.round(size / (1.5 * r)));
    const sx = size / cols;
    const sy = size / rows;
    for (let j = -1; j <= rows; j++) {
      for (let i = -1; i <= cols; i++) {
        const cx = i * sx + (j % 2 ? sx / 2 : 0);
        const cy = j * sy;
        const poly = [];
        for (let k = 0; k < 6; k++) {
          const a = (Math.PI / 180) * (60 * k - 30);
          poly.push([cx + Math.cos(a) * (sx / 2 - joint / 2) * 1.08, cy + Math.sin(a) * (sy / 1.5 - joint / 2) * 0.78]);
        }
        push(poly, [i, j]);
      }
    }
    return out;
  }

  if (spec.kind === 'cobble') {
    const u = spec.unit[0] * px;
    const n = Math.max(3, Math.round(size / u));
    const s = size / n;
    for (let j = 0; j < n; j++) {
      for (let i = 0; i < n; i++) {
        const jx = (hash(i, j, 1) - 0.5) * s * 0.22;
        const jy = (hash(i, j, 2) - 0.5) * s * 0.22;
        const cx = (i + 0.5 + (j % 2 ? 0.5 : 0)) * s + jx;
        const cy = (j + 0.5) * s + jy;
        const poly = [];
        const sides = 7;
        for (let k = 0; k < sides; k++) {
          const a = (Math.PI * 2 * k) / sides + hash(i, j, k) * 0.4;
          const rr = (s / 2 - joint / 2) * (0.82 + hash(i, j, k + 9) * 0.3);
          poly.push([cx + Math.cos(a) * rr, cy + Math.sin(a) * rr]);
        }
        push(poly, [i, j]);
      }
    }
    return out;
  }

  if (spec.kind === 'herringbone') {
    // A herringbone field tiles on a square whose side is a whole number of brick lengths.
    const len = spec.unit[0] * px;
    const wid = spec.unit[1] * px;
    const n = Math.max(2, Math.round(size / (len * 2)));
    const L = size / (n * 2);
    const W = L / 2;
    void wid;
    for (let j = -2; j <= n * 4; j++) {
      for (let i = -2; i <= n * 4; i++) {
        const ox = (i - j) * L;
        const oy = (i + j) * W;
        push(rect(ox + joint / 2, oy + joint / 2, L - joint, W - joint), [i, j]);
        push(rect(ox + L + joint / 2, oy + joint / 2, W - joint, L - joint), [i + 51, j]);
      }
    }
    return out;
  }

  if (spec.kind === 'basket') {
    const u = spec.unit[0] * px;
    const n = Math.max(2, Math.round(size / (u * 2)));
    const cell = size / n;
    const half = cell / 2;
    for (let j = 0; j < n; j++) {
      for (let i = 0; i < n; i++) {
        const x = i * cell;
        const y = j * cell;
        if ((i + j) % 2 === 0) {
          push(rect(x + joint / 2, y + joint / 2, cell - joint, half - joint), [i, j]);
          push(rect(x + joint / 2, y + half + joint / 2, cell - joint, half - joint), [i, j + 37]);
        } else {
          push(rect(x + joint / 2, y + joint / 2, half - joint, cell - joint), [i + 37, j]);
          push(rect(x + half + joint / 2, y + joint / 2, half - joint, cell - joint), [i + 73, j]);
        }
      }
    }
    return out;
  }

  // slab / stagger
  const cols = Math.max(1, Math.round(size / (spec.unit[0] * px)));
  const rows = Math.max(1, Math.round(size / (spec.unit[1] * px)));
  const w = size / cols;
  const h = size / rows;
  for (let j = 0; j < rows; j++) {
    const offset = spec.kind === 'stagger' && j % 2 ? w / 2 : 0;
    for (let i = -1; i <= cols; i++) {
      push(rect(i * w + offset + joint / 2, j * h + joint / 2, w - joint, h - joint), [i, j]);
    }
  }
  return out;
}

function drawAsphalt(ctx, hctx, px, base, spread) {
  const size = ctx.canvas.width;
  ctx.fillStyle = `rgb(${base[0]},${base[1]},${base[2]})`;
  ctx.fillRect(0, 0, size, size);
  hctx.fillStyle = '#6e6e6e';
  hctx.fillRect(0, 0, size, size);
  // aggregate
  for (let i = 0; i < 9000; i++) {
    const x = hash(i, 11, 3) * size;
    const y = hash(i, 23, 4) * size;
    const r = 0.6 + hash(i, 31, 5) * 1.9;
    const d = (hash(i, 41, 6) - 0.45) * spread * 2.4;
    ctx.fillStyle = shade(base, d);
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI * 2);
    ctx.fill();
    hctx.fillStyle = `rgb(${clamp8(110 + d * 3)},${clamp8(110 + d * 3)},${clamp8(110 + d * 3)})`;
    hctx.beginPath();
    hctx.arc(x, y, r, 0, Math.PI * 2);
    hctx.fill();
  }
  blotches(ctx, size, base, 10);
  void px;
}

function drawConcrete(ctx, hctx, px, base) {
  const size = ctx.canvas.width;
  ctx.fillStyle = `rgb(${base[0]},${base[1]},${base[2]})`;
  ctx.fillRect(0, 0, size, size);
  hctx.fillStyle = '#808080';
  hctx.fillRect(0, 0, size, size);
  blotches(ctx, size, base, 18);
  speckle(ctx, size, 0.05);
  void px;
}

// Board-marked concrete for retaining walls — horizontal shuttering lines read well on tall faces.
function drawBoardform(ctx, hctx, px, base) {
  drawConcrete(ctx, hctx, px, base);
  const size = ctx.canvas.width;
  const boards = Math.max(3, Math.round(size / (0.3 * px)));
  const h = size / boards;
  for (let i = 0; i <= boards; i++) {
    const y = i * h;
    ctx.fillStyle = shade(base, -22);
    ctx.fillRect(0, y - 1, size, 2);
    hctx.fillStyle = '#4a4a4a';
    hctx.fillRect(0, y - 1, size, 2);
    ctx.fillStyle = shade(base, (hash(i, 7, 8) - 0.5) * 14);
    ctx.fillRect(0, y + 1, size, h - 2);
  }
  speckle(ctx, size, 0.04);
}

// Loose aggregate: a dense scatter of stones over a dusty base, with the larger ones pushed into the height map so
// the sun catches them. This is what sells an unsealed road more than its colour does.
function drawGravel(ctx, hctx, px, base, spread) {
  const size = ctx.canvas.width;
  ctx.fillStyle = `rgb(${base[0]},${base[1]},${base[2]})`;
  ctx.fillRect(0, 0, size, size);
  hctx.fillStyle = '#767676';
  hctx.fillRect(0, 0, size, size);
  blotches(ctx, size, base, 12);
  const stones = 14000;
  for (let i = 0; i < stones; i++) {
    const x = hash(i, 13, 21) * size;
    const y = hash(i, 29, 22) * size;
    const r = 0.7 + Math.pow(hash(i, 37, 23), 2.2) * 3.4;
    const d = (hash(i, 43, 24) - 0.42) * spread * 2;
    ctx.fillStyle = shade(base, d);
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI * 2);
    ctx.fill();
    const lift = clamp8(118 + d * 2.4 + r * 6);
    hctx.fillStyle = `rgb(${lift},${lift},${lift})`;
    hctx.beginPath();
    hctx.arc(x, y, r, 0, Math.PI * 2);
    hctx.fill();
  }
  speckle(ctx, size, 0.06);
  void px;
}

// Graded dirt. `ruts` adds the pair of wheel tracks that make a farm or forestry road read as a two-track: packed,
// darker and slightly sunken, with a grassy crown between them.
function drawDirt(ctx, hctx, px, base, ruts) {
  const size = ctx.canvas.width;
  ctx.fillStyle = `rgb(${base[0]},${base[1]},${base[2]})`;
  ctx.fillRect(0, 0, size, size);
  hctx.fillStyle = '#7a7a7a';
  hctx.fillRect(0, 0, size, size);
  blotches(ctx, size, base, 16);
  for (let i = 0; i < 5200; i++) {
    const x = hash(i, 7, 31) * size;
    const y = hash(i, 17, 32) * size;
    const r = 0.5 + hash(i, 23, 33) * 1.6;
    ctx.fillStyle = shade(base, (hash(i, 47, 34) - 0.5) * 26);
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI * 2);
    ctx.fill();
  }
  if (ruts) {
    // v runs across the section, so the wheel tracks are horizontal bands in texture space.
    for (const centre of [0.26, 0.74]) {
      const y0 = centre * size;
      const half = size * 0.1;
      const grad = ctx.createLinearGradient(0, y0 - half, 0, y0 + half);
      grad.addColorStop(0, `rgba(${base[0]},${base[1]},${base[2]},0)`);
      grad.addColorStop(0.5, `rgba(${Math.round(base[0] * 0.72)},${Math.round(base[1] * 0.7)},${Math.round(base[2] * 0.66)},0.95)`);
      grad.addColorStop(1, `rgba(${base[0]},${base[1]},${base[2]},0)`);
      ctx.fillStyle = grad;
      ctx.fillRect(0, y0 - half, size, half * 2);
      hctx.fillStyle = 'rgba(70,70,70,0.75)';
      hctx.fillRect(0, y0 - half * 0.6, size, half * 1.2);
    }
    // vegetation down the crown and along the edges
    for (const centre of [0.5, 0.02, 0.98]) {
      for (let i = 0; i < 2600; i++) {
        const x = hash(i, 11, 35) * size;
        const y = (centre + (hash(i, 19, 36) - 0.5) * 0.14) * size;
        ctx.fillStyle = `rgba(${86 + hash(i, 5, 37) * 36 | 0},${96 + hash(i, 9, 38) * 34 | 0},56,0.75)`;
        ctx.fillRect(x, y, 1.4, 1.4);
      }
    }
  }
  speckle(ctx, size, 0.05);
  void px;
}

// Jointed concrete carriageway: transverse construction joints every ~4.5 m.
function drawSlabRoad(ctx, hctx, px, base) {
  drawConcrete(ctx, hctx, px, base);
  const size = ctx.canvas.width;
  const spacing = Math.max(24, 4.5 * px);
  for (let x = 0; x < size; x += spacing) {
    ctx.fillStyle = shade(base, -26);
    ctx.fillRect(x, 0, 2, size);
    hctx.fillStyle = '#4e4e4e';
    hctx.fillRect(x, 0, 2, size);
  }
}

function drawEarth(ctx, hctx, px) {
  const size = ctx.canvas.width;
  const base = [92, 84, 66];
  ctx.fillStyle = `rgb(${base[0]},${base[1]},${base[2]})`;
  ctx.fillRect(0, 0, size, size);
  hctx.fillStyle = '#707070';
  hctx.fillRect(0, 0, size, size);
  for (let i = 0; i < 2600; i++) {
    const x = hash(i, 3, 12) * size;
    const y = hash(i, 5, 13) * size;
    const r = 2 + hash(i, 7, 14) * 9;
    const g = hash(i, 9, 15);
    ctx.fillStyle = g > 0.55 ? shade([86, 96, 66], (g - 0.5) * 40) : shade(base, (g - 0.5) * 36);
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI * 2);
    ctx.fill();
    hctx.fillStyle = `rgb(${clamp8(112 + (g - 0.5) * 60)},0,0)`;
    hctx.beginPath();
    hctx.arc(x, y, r, 0, Math.PI * 2);
    hctx.fill();
  }
  speckle(ctx, size, 0.07);
  void px;
}

function speckle(ctx, size, amount) {
  const img = ctx.getImageData(0, 0, size, size);
  const d = img.data;
  for (let i = 0; i < d.length; i += 4) {
    const n = (hash(i, i >> 7, 21) - 0.5) * 255 * amount;
    d[i] = clamp8(d[i] + n);
    d[i + 1] = clamp8(d[i + 1] + n);
    d[i + 2] = clamp8(d[i + 2] + n);
  }
  ctx.putImageData(img, 0, 0);
}

function blotches(ctx, size, base, count) {
  for (let i = 0; i < count; i++) {
    const x = hash(i, 2, 31) * size;
    const y = hash(i, 4, 32) * size;
    const r = size * (0.06 + hash(i, 6, 33) * 0.18);
    const g = ctx.createRadialGradient(x, y, 0, x, y, r);
    const d = (hash(i, 8, 34) - 0.5) * 26;
    g.addColorStop(0, `rgba(${clamp8(base[0] + d)},${clamp8(base[1] + d)},${clamp8(base[2] + d)},0.5)`);
    g.addColorStop(1, 'rgba(0,0,0,0)');
    ctx.fillStyle = g;
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI * 2);
    ctx.fill();
  }
}

// ── signage atlas ─────────────────────────────────────────────────────────────────────────────────────────────────
// 2×2 cells: (0,0) STOP, (1,0) YIELD, (0,1) sign back, (1,1) spare / blank plate.

export const SIGN_UV = {
  stop: { u0: 0, v0: 0.5, u1: 0.5, v1: 1 },
  yield: { u0: 0.5, v0: 0.5, u1: 1, v1: 1 },
  back: { u0: 0, v0: 0, u1: 0.5, v1: 0.5 },
  blank: { u0: 0.5, v0: 0, u1: 1, v1: 0.5 },
};

function drawSignAtlas(ctx) {
  const size = ctx.canvas.width;
  const half = size / 2;
  ctx.fillStyle = '#9aa0a6';
  ctx.fillRect(0, 0, size, size);

  // STOP — top-left cell
  ctx.save();
  ctx.translate(0, 0);
  cell(ctx, half, '#b8322e', () => {
    ctx.strokeStyle = '#f2f2f0';
    ctx.lineWidth = half * 0.045;
    octagon(ctx, half / 2, half / 2, half * 0.40);
    ctx.stroke();
    ctx.fillStyle = '#f6f5f2';
    ctx.font = `700 ${half * 0.26}px 'DM Sans', Helvetica, Arial, sans-serif`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('STOP', half / 2, half / 2 + half * 0.01);
  }, () => {
    ctx.fillStyle = '#b8322e';
    octagon(ctx, half / 2, half / 2, half * 0.46);
    ctx.fill();
  });
  ctx.restore();

  // YIELD — top-right cell
  ctx.save();
  ctx.translate(half, 0);
  cell(ctx, half, '#d8d4cc', () => {
    ctx.fillStyle = '#f4f2ee';
    triangle(ctx, half / 2, half * 0.52, half * 0.40);
    ctx.fill();
    ctx.fillStyle = '#b8322e';
    triangle(ctx, half / 2, half * 0.52, half * 0.40);
    ctx.fill();
    ctx.fillStyle = '#f4f2ee';
    triangle(ctx, half / 2, half * 0.52, half * 0.29);
    ctx.fill();
    ctx.fillStyle = '#2b2b2b';
    ctx.font = `700 ${half * 0.14}px 'DM Sans', Helvetica, Arial, sans-serif`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('YIELD', half / 2, half * 0.40);
  });
  ctx.restore();

  // plate backs — bottom row
  ctx.fillStyle = '#8f949b';
  ctx.fillRect(0, half, size, half);
  ctx.fillStyle = '#82878e';
  ctx.fillRect(half, half, half, half);
  for (let i = 0; i < 220; i++) {
    const x = hash(i, 1, 44) * size;
    const y = half + hash(i, 2, 45) * half;
    ctx.fillStyle = `rgba(255,255,255,${hash(i, 3, 46) * 0.06})`;
    ctx.fillRect(x, y, 2, 2);
  }
}

function cell(ctx, s, bg, paint, shapePaint) {
  ctx.fillStyle = bg;
  ctx.fillRect(0, 0, s, s);
  shapePaint?.();
  paint();
}

function octagon(ctx, cx, cy, r) {
  ctx.beginPath();
  for (let i = 0; i < 8; i++) {
    const a = (Math.PI / 4) * i + Math.PI / 8;
    const x = cx + Math.cos(a) * r;
    const y = cy + Math.sin(a) * r;
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.closePath();
}

function triangle(ctx, cx, cy, r) {
  ctx.beginPath();
  for (let i = 0; i < 3; i++) {
    const a = (Math.PI * 2 * i) / 3 - Math.PI / 2 + Math.PI;
    const x = cx + Math.cos(a) * r;
    const y = cy + Math.sin(a) * r;
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.closePath();
}

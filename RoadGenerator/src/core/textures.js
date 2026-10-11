// Procedural, tileable PBR materials built from editable *layer stacks*. Each material is a stack of
// layers (base, aggregate, weathering, cracks, patches, …) composited into height/albedo/roughness maps;
// normal + AO are derived from height. Everything is seamless (periodic noise lattices).

const clamp01 = (v) => (v < 0 ? 0 : v > 1 ? 1 : v);
const lerp = (a, b, t) => a + (b - a) * t;
const smooth = (t) => t * t * (3 - 2 * t);

function hashInt(x, y, seed) {
  let h = (x * 374761393 + y * 668265263 + seed * 1274126177) | 0;
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  h ^= h >>> 16;
  return (h >>> 0) / 4294967296;
}
/** Periodic value noise: coordinates in [0,1), `freq` cells across (integer → tileable). */
function pnoise(u, v, freq, seed) {
  const x = u * freq, y = v * freq;
  const xi = Math.floor(x), yi = Math.floor(y);
  const fx = smooth(x - xi), fy = smooth(y - yi);
  const x0 = ((xi % freq) + freq) % freq, y0 = ((yi % freq) + freq) % freq, x1 = (x0 + 1) % freq, y1 = (y0 + 1) % freq;
  const a = hashInt(x0, y0, seed), b = hashInt(x1, y0, seed), c = hashInt(x0, y1, seed), d = hashInt(x1, y1, seed);
  return lerp(lerp(a, b, fx), lerp(c, d, fx), fy);
}
function fbm(u, v, freq, seed, oct = 4, gain = 0.5) {
  let s = 0, a = 1, f = freq, t = 0;
  for (let i = 0; i < oct; i++) { s += a * pnoise(u, v, f, seed + i * 17); t += a; a *= gain; f *= 2; }
  return s / t;
}
/** Periodic Voronoi: returns {d1, d2, id, cx, cy} for the nearest cell feature points. */
function voronoi(u, v, freq, seed, jitter = 1) {
  const x = u * freq, y = v * freq, xi = Math.floor(x), yi = Math.floor(y);
  let d1 = 9, d2 = 9, id = 0, cx = 0, cy = 0;
  for (let j = -1; j <= 1; j++) for (let i = -1; i <= 1; i++) {
    const gx = xi + i, gy = yi + j;
    const wx = ((gx % freq) + freq) % freq, wy = ((gy % freq) + freq) % freq;
    const px = gx + 0.5 + (hashInt(wx, wy, seed) - 0.5) * jitter, py = gy + 0.5 + (hashInt(wx, wy, seed + 99) - 0.5) * jitter;
    const d = Math.hypot(px - x, py - y);
    if (d < d1) { d2 = d1; d1 = d; id = hashInt(wx, wy, seed + 7); cx = px; cy = py; } else if (d < d2) d2 = d;
  }
  return { d1, d2, id, cx, cy };
}

// ---------------------------------------------------------------------------------------------
// Layer stack definitions. Every material kind exposes editable layers in the UI.
// ---------------------------------------------------------------------------------------------
export const PAVING_PATTERNS = ["herringbone", "running", "basket", "stack", "hex", "flagstone", "cobble", "large-slab", "tactile-blister"];

export function defaultStacks() {
  return {
    asphalt: { kind: "asphalt", uvScale: 6, layers: [
      { id: "base", name: "Bitumen base", on: true, color: [0.17, 0.17, 0.175], grain: 0.6, seed: 1 },
      { id: "aggregate", name: "Aggregate stones", on: true, amount: 0.55, size: 0.5, seed: 2 },
      { id: "weather", name: "Weathering / fading", on: true, amount: 0.45, scale: 1.0, seed: 3 },
      { id: "cracks", name: "Crack network", on: true, amount: 0.35, width: 0.5, seed: 4 },
      { id: "patches", name: "Repair patches", on: true, amount: 0.25, seed: 5 },
      { id: "sheen", name: "Bitumen sheen", on: true, roughness: 0.82 }] },
    asphaltBike: { kind: "asphalt", uvScale: 6, layers: [
      { id: "base", name: "Coloured surfacing", on: true, color: [0.45, 0.17, 0.14], grain: 0.5, seed: 11 },
      { id: "aggregate", name: "Aggregate stones", on: true, amount: 0.4, size: 0.5, seed: 12 },
      { id: "weather", name: "Weathering", on: true, amount: 0.5, scale: 1.0, seed: 13 },
      { id: "cracks", name: "Cracks", on: false, amount: 0.2, width: 0.5, seed: 14 },
      { id: "patches", name: "Patches", on: false, amount: 0.1, seed: 15 },
      { id: "sheen", name: "Sheen", on: true, roughness: 0.85 }] },
    concrete: { kind: "concrete", uvScale: 3, layers: [
      { id: "base", name: "Cement base", on: true, color: [0.58, 0.57, 0.54], grain: 0.5, seed: 21 },
      { id: "pores", name: "Air pores", on: true, amount: 0.5, seed: 22 },
      { id: "joints", name: "Expansion joints", on: true, spacing: 1, width: 0.6 },
      { id: "stains", name: "Water stains", on: true, amount: 0.4, seed: 23 },
      { id: "sheen", name: "Finish", on: true, roughness: 0.78 }] },
    curb: { kind: "concrete", uvScale: 1.5, layers: [
      { id: "base", name: "Precast concrete", on: true, color: [0.62, 0.61, 0.58], grain: 0.4, seed: 31 },
      { id: "pores", name: "Pores", on: true, amount: 0.35, seed: 32 },
      { id: "joints", name: "Kerb unit joints", on: true, spacing: 1, width: 0.5 },
      { id: "stains", name: "Tyre scuffs & grime", on: true, amount: 0.55, seed: 33 },
      { id: "sheen", name: "Finish", on: true, roughness: 0.72 }] },
    paving: { kind: "paving", uvScale: 2, layers: [
      { id: "pattern", name: "Paving pattern", on: true, pattern: "herringbone", unit: 0.2, ratio: 2, jointWidth: 0.012, seed: 41 },
      { id: "stone", name: "Stone colour", on: true, color: [0.55, 0.5, 0.44], variation: 0.35, seed: 42 },
      { id: "joint", name: "Joint sand", on: true, color: [0.42, 0.38, 0.32], depth: 0.6 },
      { id: "chips", name: "Chips & wear", on: true, amount: 0.3, seed: 43 },
      { id: "moss", name: "Moss / dirt in joints", on: true, amount: 0.35, seed: 44 },
      { id: "sheen", name: "Finish", on: true, roughness: 0.7 }] },
    grass: { kind: "grass", uvScale: 2.5, layers: [
      { id: "base", name: "Turf", on: true, color: [0.2, 0.3, 0.1], seed: 51 },
      { id: "patches", name: "Dry patches", on: true, amount: 0.4, seed: 52 },
      { id: "sheen", name: "Finish", on: true, roughness: 0.95 }] },
    soil: { kind: "soil", uvScale: 2, layers: [
      { id: "base", name: "Topsoil", on: true, color: [0.27, 0.2, 0.14], seed: 61 },
      { id: "mulch", name: "Mulch clumps", on: true, amount: 0.6, seed: 62 },
      { id: "sheen", name: "Finish", on: true, roughness: 0.95 }] },
    gravel: { kind: "gravel", uvScale: 1.5, layers: [
      { id: "stones", name: "Crushed stone", on: true, color: [0.5, 0.48, 0.45], size: 0.5, seed: 71 },
      { id: "sheen", name: "Finish", on: true, roughness: 0.9 }] },
    paint: { kind: "paint", uvScale: 1, layers: [{ id: "base", name: "Thermoplastic white", on: true, color: [0.92, 0.92, 0.88], seed: 81 }, { id: "sheen", name: "Finish", on: true, roughness: 0.5 }] },
    paintYellow: { kind: "paint", uvScale: 1, layers: [{ id: "base", name: "Thermoplastic yellow", on: true, color: [0.95, 0.75, 0.15], seed: 82 }, { id: "sheen", name: "Finish", on: true, roughness: 0.5 }] },
    metal: { kind: "metal", uvScale: 1, layers: [{ id: "base", name: "Galvanised steel", on: true, color: [0.62, 0.64, 0.66], seed: 91, metal: 1 }, { id: "sheen", name: "Finish", on: true, roughness: 0.42 }] },
    metalPainted: { kind: "metal", uvScale: 1, layers: [{ id: "base", name: "Painted steel", on: true, color: [0.12, 0.13, 0.14], seed: 92, metal: 0.3 }, { id: "sheen", name: "Finish", on: true, roughness: 0.5 }] },
    steelGalv: { kind: "metal", uvScale: 1, layers: [{ id: "base", name: "Hot-dip galvanised", on: true, color: [0.56, 0.58, 0.6], seed: 93, metal: 1, spangle: 1 }, { id: "sheen", name: "Finish", on: true, roughness: 0.48 }] },
    rubber: { kind: "paint", uvScale: 1, layers: [{ id: "base", name: "Rubber", on: true, color: [0.05, 0.05, 0.05], seed: 94 }, { id: "sheen", name: "Finish", on: true, roughness: 0.85 }] },
    tactile: { kind: "paving", uvScale: 0.4, layers: [
      { id: "pattern", name: "Blister paving", on: true, pattern: "tactile-blister", unit: 0.4, ratio: 1, jointWidth: 0.006, seed: 101 },
      { id: "stone", name: "Colour", on: true, color: [0.7, 0.35, 0.25], variation: 0.1, seed: 102 },
      { id: "joint", name: "Joint", on: true, color: [0.5, 0.3, 0.22], depth: 0.4 },
      { id: "chips", name: "Wear", on: true, amount: 0.2, seed: 103 }, { id: "moss", name: "Dirt", on: true, amount: 0.2, seed: 104 },
      { id: "sheen", name: "Finish", on: true, roughness: 0.7 }] },
    wood: { kind: "wood", uvScale: 1, layers: [{ id: "base", name: "Treated timber", on: true, color: [0.3, 0.22, 0.15], seed: 111 }, { id: "sheen", name: "Finish", on: true, roughness: 0.85 }] },
    ballast: { kind: "gravel", uvScale: 1.5, layers: [{ id: "stones", name: "Track ballast", on: true, color: [0.42, 0.4, 0.38], size: 0.65, seed: 121 }, { id: "sheen", name: "Finish", on: true, roughness: 0.92 }] },
    tiles: { kind: "paving", uvScale: 1, layers: [
      { id: "pattern", name: "Wall tiles", on: true, pattern: "stack", unit: 0.15, ratio: 1, jointWidth: 0.005, seed: 131 },
      { id: "stone", name: "Glaze", on: true, color: [0.85, 0.85, 0.8], variation: 0.08, seed: 132 },
      { id: "joint", name: "Grout", on: true, color: [0.5, 0.5, 0.48], depth: 0.3 },
      { id: "chips", name: "Wear", on: true, amount: 0.15, seed: 133 }, { id: "moss", name: "Grime", on: true, amount: 0.4, seed: 134 },
      { id: "sheen", name: "Glaze finish", on: true, roughness: 0.25 }] },
    glass: { kind: "paint", uvScale: 1, layers: [{ id: "base", name: "Tinted glass", on: true, color: [0.1, 0.12, 0.14], seed: 141, metal: 0.6 }, { id: "sheen", name: "Finish", on: true, roughness: 0.08 }] },
    carPaint: { kind: "paint", uvScale: 1, layers: [{ id: "base", name: "Car paint", on: true, color: [0.8, 0.8, 0.8], seed: 151, metal: 0.4 }, { id: "sheen", name: "Clear coat", on: true, roughness: 0.22 }] },
    billboard: { kind: "paint", uvScale: 1, layers: [{ id: "base", name: "Vinyl print", on: true, color: [0.9, 0.9, 0.9], seed: 161 }, { id: "sheen", name: "Finish", on: true, roughness: 0.6 }] },
    sign: { kind: "paint", uvScale: 1, layers: [{ id: "base", name: "Retroreflective sheeting", on: true, color: [0.1, 0.35, 0.2], seed: 171 }, { id: "sheen", name: "Finish", on: true, roughness: 0.3 }] },
    plastic: { kind: "paint", uvScale: 1, layers: [{ id: "base", name: "Traffic plastic", on: true, color: [0.9, 0.35, 0.05], seed: 181 }, { id: "sheen", name: "Finish", on: true, roughness: 0.6 }] },
    lamp: { kind: "paint", emissive: [1.0, 0.86, 0.6], uvScale: 1, layers: [{ id: "base", name: "Warm LED", on: true, color: [1, 0.95, 0.85], seed: 191 }, { id: "sheen", name: "Finish", on: true, roughness: 0.3 }] },
    lampOff: { kind: "paint", uvScale: 1, layers: [{ id: "base", name: "Unlit lens", on: true, color: [0.12, 0.12, 0.12], seed: 192 }, { id: "sheen", name: "Finish", on: true, roughness: 0.25 }] },
    lampRed: { kind: "paint", emissive: [1.0, 0.1, 0.05], uvScale: 1, layers: [{ id: "base", name: "Red lens", on: true, color: [0.9, 0.1, 0.05], seed: 193 }, { id: "sheen", name: "Finish", on: true, roughness: 0.25 }] },
    lampGreen: { kind: "paint", emissive: [0.1, 1.0, 0.35], uvScale: 1, layers: [{ id: "base", name: "Green lens", on: true, color: [0.1, 0.8, 0.35], seed: 194 }, { id: "sheen", name: "Finish", on: true, roughness: 0.25 }] },
    lampAmber: { kind: "paint", emissive: [1.0, 0.6, 0.05], uvScale: 1, layers: [{ id: "base", name: "Amber lens", on: true, color: [0.95, 0.6, 0.08], seed: 195 }, { id: "sheen", name: "Finish", on: true, roughness: 0.25 }] },
  };
}

// ---------------------------------------------------------------------------------------------
// Pattern functions for paving: return { height (0..1 across stone), inJoint, stoneId }
// ---------------------------------------------------------------------------------------------
function pavingPattern(u, v, layer) {
  const pat = layer.pattern || "herringbone", seed = layer.seed || 1;
  const jw = layer.jointWidth ?? 0.08;
  const tiles = Math.max(1, Math.round((layer.span || 1) / (layer.unit || 0.2))); // paving units across the texture (span metres)
  const ratio = layer.ratio || 2;
  const edge = (fx, fy, w, h) => Math.min(fx, w - fx, fy, h - fy); // distance to nearest edge in unit coords
  let fx, fy, w = 1, h = 1, id;
  if (pat === "herringbone") {
    // 45° herringbone: dominoes (2×1 cells) on a lattice rotated 45°; m = (a−b) mod 4 selects the domino.
    const x = u * tiles, y = v * tiles;
    const rx = (x + y), ry = (y - x);
    const a = Math.floor(rx), b = Math.floor(ry);
    const m = (((a - b) % 4) + 4) % 4;
    let ox = a, oy = b;
    if (m === 0) { w = 2; h = 1; } else if (m === 1) { w = 2; h = 1; ox = a - 1; } else if (m === 2) { w = 1; h = 2; oy = b - 1; } else { w = 1; h = 2; }
    fx = rx - ox; fy = ry - oy; id = hashInt(ox & 4095, oy & 4095, seed);
    const d = edge(fx, fy, w, h) * Math.SQRT1_2; // rotated metric → cell units
    return finish(d, jw * tiles * 0.5, id, layer);
  }
  if (pat === "running" || pat === "stack" || pat === "large-slab") {
    const tx = pat === "large-slab" ? tiles / 3 : tiles;
    const x = u * tx, yy = v * tx * (pat === "running" ? ratio : 1);
    const row = Math.floor(yy);
    const off = pat === "running" ? (row % 2) * 0.5 : 0;
    const col = Math.floor(x + off);
    fx = x + off - col; fy = yy - row; w = 1; h = 1; id = hashInt(col & 4095, row & 4095, seed);
    const d = Math.min(fx, 1 - fx, (fy) / (pat === "running" ? ratio : 1), (1 - fy) / (pat === "running" ? ratio : 1));
    return finish(d, jw * tx * 0.5, id, layer);
  }
  if (pat === "basket") {
    const x = u * tiles / 2, y = v * tiles / 2;
    const cx = Math.floor(x), cy = Math.floor(y);
    const horiz = ((cx + cy) % 2 + 2) % 2 === 0;
    const lx = x - cx, ly = y - cy;
    const sub = horiz ? Math.floor(ly * 2) : Math.floor(lx * 2);
    const sx = horiz ? lx : lx * 2 - sub, sy = horiz ? ly * 2 - sub : ly;
    const d = Math.min(sx, 1 - sx, sy, 1 - sy) * (horiz ? 1 : 1);
    id = hashInt(cx * 2 + sub, cy * 3 + (horiz ? 1 : 0), seed);
    return finish(d * 0.5, jw * tiles * 0.5, id, layer);
  }
  if (pat === "hex") {
    const f = tiles * 0.8;
    const x = u * f, y = v * f * 1.1547;
    // hex lattice via two offset square lattices
    let best = 9, bid = 0;
    for (let j = -1; j <= 1; j++) for (let i = -1; i <= 1; i++) {
      const cxI = Math.floor(x) + i, cyI = Math.floor(y) + j;
      const px = cxI + (cyI & 1 ? 0.5 : 0), py = cyI;
      const d = Math.hypot(x - px, (y - py) * 0.866);
      if (d < best) { best = d; bid = hashInt(cxI & 4095, cyI & 4095, seed); }
    }
    const dEdge = 0.5 - best;
    return finish(Math.max(0, dEdge), jw * f * 0.6, bid, layer);
  }
  if (pat === "flagstone" || pat === "cobble") {
    const f = pat === "cobble" ? tiles * 1.5 : tiles * 0.6;
    const vr = voronoi(u, v, Math.max(2, Math.round(f)), seed, pat === "cobble" ? 0.9 : 0.75);
    const dEdge = (vr.d2 - vr.d1) * 0.5;
    const r = finish(dEdge, jw * f * 0.6, vr.id, layer);
    if (pat === "cobble") { r.height = r.inJoint ? 0 : clamp01(1 - vr.d1 * 1.6) ** 0.7; }
    return r;
  }
  if (pat === "tactile-blister") {
    const tx = tiles, x = u * tx, y = v * tx;
    const col = Math.floor(x), row = Math.floor(y);
    fx = x - col; fy = y - row; id = hashInt(col, row, seed);
    const dTile = Math.min(fx, 1 - fx, fy, 1 - fy);
    const r = finish(dTile, jw * tx * 0.5, id, layer);
    // 6×6 blisters per 400mm tile, 25mm diameter, 5mm high
    const bx = fx * 6, by = fy * 6;
    const dx = bx - Math.floor(bx) - 0.5, dy = by - Math.floor(by) - 0.5;
    const d = Math.hypot(dx, dy) / 0.33;
    if (!r.inJoint) { r.height = 0.45 + 0.1 * r.bevel; if (d < 1) r.height += Math.sqrt(1 - d * d) * 0.5; }
    return r;
  }
  return finish(0.5, 0, 0, layer);
}
function finish(dEdge, halfJoint, id, layer) {
  const inJoint = dEdge < halfJoint;
  const bevel = clamp01((dEdge - halfJoint) / Math.max(halfJoint * 1.5, 0.02));
  const height = inJoint ? 0 : 0.6 + 0.4 * bevel;
  return { height, inJoint, id, bevel };
}

// ---------------------------------------------------------------------------------------------
// Material baking
// ---------------------------------------------------------------------------------------------
export function bakeMaterial(stack, size = 512) {
  const N = size, n2 = N * N;
  const height = new Float32Array(n2), rough = new Float32Array(n2), metal = new Float32Array(n2);
  const col = new Float32Array(n2 * 3);
  const L = Object.fromEntries(stack.layers.map((l) => [l.id, l]));
  const on = (id) => L[id] && L[id].on;
  const kind = stack.kind;
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const u = (x + 0.5) / N, v = (y + 0.5) / N, i = y * N + x;
    let h = 0.5, r = 0.8, m = 0, c = [0.5, 0.5, 0.5];
    if (kind === "asphalt") {
      const b = L.base; c = [...b.color];
      const grain = fbm(u, v, 64, b.seed, 3) - 0.5;
      h = 0.5 + grain * 0.35 * (b.grain ?? 0.6);
      c = c.map((k) => k * (1 + grain * 0.5));
      if (on("aggregate")) {
        const a = L.aggregate; const vr = voronoi(u, v, Math.round(40 + 60 * (1 - (a.size ?? 0.5))), a.seed, 1);
        const stone = clamp01(1 - vr.d1 * 2.2);
        if (vr.id < (a.amount ?? 0.5)) { h += stone * 0.25; const lum = 0.6 + vr.id * 0.8; c = c.map((k) => lerp(k, k * lum + 0.08, stone)); r -= stone * 0.1; }
      }
      if (on("weather")) {
        const wl = L.weather; const wv = fbm(u, v, 4 * (wl.scale || 1), wl.seed, 4);
        const amt = (wl.amount ?? 0.45) * smooth(clamp01((wv - 0.35) * 2.5));
        c = c.map((k) => lerp(k, k * 1.9 + 0.1, amt)); r += amt * 0.15;
      }
      if (on("cracks")) {
        const cl = L.cracks; const vr = voronoi(u, v, 6, cl.seed, 1);
        const e = vr.d2 - vr.d1; const w = 0.012 * (cl.width ?? 0.5) + 0.004;
        const gate = fbm(u, v, 3, cl.seed + 5, 2) < (cl.amount ?? 0.35) + 0.15;
        if (gate && e < w) { const t = 1 - e / w; h -= t * 0.5; c = c.map((k) => k * (1 - 0.6 * t)); }
      }
      if (on("patches")) {
        const pl = L.patches; const vr = voronoi(u, v, 3, pl.seed, 0.8);
        const nx = Math.abs(((u * 3) % 1) - 0.5), nz = Math.abs(((v * 3) % 1) - 0.5);
        if (vr.id < (pl.amount ?? 0.25) * 0.9 && nx < 0.3 && nz < 0.38) { const edge = Math.min(0.3 - nx, 0.38 - nz) * 30; const t = clamp01(edge); h += 0.04 * t; c = c.map((k) => lerp(k, k * 0.75, t)); r -= 0.08 * t; }
      }
      if (on("sheen")) r = r * 0.5 + (L.sheen.roughness ?? 0.82) * 0.5; else r = clamp01(r);
    } else if (kind === "concrete") {
      const b = L.base; c = [...b.color];
      const grain = fbm(u, v, 48, b.seed, 4) - 0.5;
      h = 0.5 + grain * 0.2 * (b.grain ?? 0.5);
      c = c.map((k) => k * (1 + grain * 0.3));
      if (on("pores")) { const p = L.pores; const vr = voronoi(u, v, 90, p.seed); if (vr.id < (p.amount ?? 0.4) * 0.5 && vr.d1 < 0.35) { const t = 1 - vr.d1 / 0.35; h -= t * 0.35; c = c.map((k) => k * (1 - 0.45 * t)); } }
      if (on("joints")) { const j = L.joints; const sp = j.spacing || 1; const fu = ((u * sp) % 1), fv = ((v * sp) % 1); const d = Math.min(fu, 1 - fu, fv, 1 - fv); const w = 0.006 * (j.width ?? 0.6) + 0.002; if (d < w) { const t = 1 - d / w; h -= 0.5 * t; c = c.map((k) => k * (1 - 0.5 * t)); } }
      if (on("stains")) { const s = L.stains; const sv = fbm(u, v, 3, s.seed, 4); const amt = (s.amount ?? 0.4) * smooth(clamp01((sv - 0.45) * 3)); c = c.map((k) => lerp(k, k * 0.6, amt)); r += amt * 0.1; }
      r = on("sheen") ? (L.sheen.roughness ?? 0.78) + (fbm(u, v, 20, 999, 2) - 0.5) * 0.15 : 0.8;
    } else if (kind === "paving") {
      const pat = pavingPattern(u, v, { ...L.pattern, span: stack.uvScale });
      const st = L.stone; const base = [...st.color];
      const varr = (pat.id - 0.5) * 2 * (st.variation ?? 0.3);
      const micro = fbm(u, v, 80, st.seed, 3) - 0.5;
      c = base.map((k, kk) => k * (1 + varr * (kk === 2 ? 0.6 : 1)) * (1 + micro * 0.25));
      h = pat.height;
      if (pat.inJoint) { const j = L.joint; c = [...(j?.color || [0.4, 0.37, 0.32])].map((k) => k * (1 + micro * 0.4)); h = 1 - (j?.depth ?? 0.6); r = 0.95; }
      else {
        if (on("chips")) { const ch = L.chips; const vr = voronoi(u, v, 30, ch.seed); if (vr.id < (ch.amount ?? 0.3) * 0.3 && vr.d1 < 0.3) { const t = 1 - vr.d1 / 0.3; h -= 0.15 * t; c = c.map((k) => k * (1 + 0.3 * t)); } }
        r = on("sheen") ? (L.sheen.roughness ?? 0.7) + micro * 0.3 : 0.75;
      }
      if (on("moss")) { const mo = L.moss; const mv = fbm(u, v, 5, mo.seed, 3); const near = pat.inJoint ? 1 : 1 - pat.bevel; const amt = (mo.amount ?? 0.35) * smooth(clamp01((mv - 0.4) * 3)) * (0.4 + 0.6 * near); c = [lerp(c[0], 0.2, amt), lerp(c[1], 0.26, amt), lerp(c[2], 0.12, amt)]; r = lerp(r, 0.95, amt); }
    } else if (kind === "grass") {
      const b = L.base; const n1 = fbm(u, v, 40, b.seed, 4), n2 = fbm(u, v, 8, b.seed + 3, 3);
      c = [b.color[0] * (0.6 + n1 * 0.9), b.color[1] * (0.6 + n1 * 0.8 + n2 * 0.2), b.color[2] * (0.6 + n1 * 0.7)];
      h = 0.5 + (n1 - 0.5) * 0.6;
      if (on("patches")) { const p = L.patches; const pv = fbm(u, v, 3, p.seed, 3); const amt = (p.amount ?? 0.4) * smooth(clamp01((pv - 0.5) * 4)); c = [lerp(c[0], 0.42, amt), lerp(c[1], 0.36, amt), lerp(c[2], 0.2, amt)]; }
      r = 0.95;
    } else if (kind === "soil") {
      const b = L.base; const n1 = fbm(u, v, 30, b.seed, 4);
      c = b.color.map((k) => k * (0.7 + n1 * 0.7)); h = 0.5 + (n1 - 0.5) * 0.5;
      if (on("mulch")) { const ml = L.mulch; const vr = voronoi(u, v, 24, ml.seed); if (vr.id < (ml.amount ?? 0.6)) { const t = clamp01(1 - vr.d1 * 2); h += t * 0.3; c = c.map((k) => k * (0.8 + vr.id * 0.5)); } }
      r = 0.95;
    } else if (kind === "gravel") {
      const g = L.stones; const vr = voronoi(u, v, Math.round(18 + 20 * (1 - (g.size ?? 0.5))), g.seed, 1);
      const stone = clamp01(1 - vr.d1 * 1.8) ** 0.6;
      h = stone * 0.9; c = g.color.map((k) => k * (0.6 + vr.id * 0.8) * (0.6 + stone * 0.5)); r = 0.85 + (1 - stone) * 0.1;
    } else if (kind === "metal") {
      const b = L.base; const brushed = fbm(u * 0.05 + 0.3, v, 96, b.seed, 2);
      c = b.color.map((k) => k * (0.9 + brushed * 0.2)); h = 0.5 + (brushed - 0.5) * 0.05; m = b.metal ?? 1;
      if (b.spangle) { const vr = voronoi(u, v, 10, b.seed + 1); c = c.map((k) => k * (0.85 + vr.id * 0.3)); }
      r = (L.sheen?.roughness ?? 0.45) + (brushed - 0.5) * 0.1;
    } else if (kind === "wood") {
      const b = L.base; const ring = Math.sin((v * 14 + fbm(u, v, 3, b.seed, 2) * 3) * Math.PI * 2) * 0.5 + 0.5; const g = fbm(u * 0.1, v, 64, b.seed, 3);
      c = b.color.map((k) => k * (0.75 + ring * 0.35) * (0.8 + g * 0.3)); h = 0.5 + (ring - 0.5) * 0.15 + (g - 0.5) * 0.1; r = 0.85;
    } else { // paint / flat
      const b = L.base; const g = fbm(u, v, 32, b.seed, 2);
      c = b.color.map((k) => k * (0.95 + g * 0.1)); h = 0.5 + (g - 0.5) * 0.04; m = b.metal ?? 0; r = L.sheen?.roughness ?? 0.5;
    }
    height[i] = clamp01(h); rough[i] = clamp01(r); metal[i] = clamp01(m);
    col[i * 3] = clamp01(c[0]); col[i * 3 + 1] = clamp01(c[1]); col[i * 3 + 2] = clamp01(c[2]);
  }
  // derive normal + AO (periodic Sobel)
  const albedo = new Uint8ClampedArray(n2 * 4), normal = new Uint8ClampedArray(n2 * 4), orm = new Uint8ClampedArray(n2 * 4);
  const strength = { asphalt: 2.2, concrete: 1.6, paving: 3.5, grass: 1.5, soil: 2.5, gravel: 3.5, metal: 0.6, wood: 1.2, paint: 0.5 }[kind] || 1.5;
  const H = (x, y) => height[((y + N) % N) * N + ((x + N) % N)];
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const i = y * N + x;
    const dx = (H(x + 1, y - 1) + 2 * H(x + 1, y) + H(x + 1, y + 1) - H(x - 1, y - 1) - 2 * H(x - 1, y) - H(x - 1, y + 1)) / 8;
    const dy = (H(x - 1, y + 1) + 2 * H(x, y + 1) + H(x + 1, y + 1) - H(x - 1, y - 1) - 2 * H(x, y - 1) - H(x + 1, y - 1)) / 8;
    let nx = -dx * strength * N * 0.02, ny = -dy * strength * N * 0.02, nz = 1;
    const l = Math.hypot(nx, ny, nz); nx /= l; ny /= l; nz /= l;
    normal[i * 4] = (nx * 0.5 + 0.5) * 255; normal[i * 4 + 1] = (ny * 0.5 + 0.5) * 255; normal[i * 4 + 2] = (nz * 0.5 + 0.5) * 255; normal[i * 4 + 3] = 255;
    // cheap AO: local height relative to blurred neighbourhood
    let avg = 0; for (let j = -3; j <= 3; j += 3) for (let k = -3; k <= 3; k += 3) avg += H(x + k, y + j); avg /= 9;
    const ao = clamp01(0.75 + (height[i] - avg) * 1.6);
    albedo[i * 4] = col[i * 3] * 255; albedo[i * 4 + 1] = col[i * 3 + 1] * 255; albedo[i * 4 + 2] = col[i * 3 + 2] * 255; albedo[i * 4 + 3] = 255;
    orm[i * 4] = ao * 255; orm[i * 4 + 1] = rough[i] * 255; orm[i * 4 + 2] = metal[i] * 255; orm[i * 4 + 3] = height[i] * 255;
  }
  return { albedo, normal, orm, size: N };
}

/** Renderer-side parameters for a stack (uv scale, metalness, wear tint). */
export function materialParams(st) {
  const baseLayer = st.layers.find((l) => l.id === "base" || l.id === "stone" || l.id === "stones");
  return { uvScale: st.uvScale, roughness: 1, metal: baseLayer?.metal ? 1 : 0, normalStrength: (st.normalStrength ?? (st.kind === "paint" ? 0.3 : 1)), tint: st.tint || [1, 1, 1], wearTint: st.kind === "asphalt" ? [0.55, 0.55, 0.57, 0.85] : [0.8, 0.8, 0.8, 0.3] };
}
/** Bake every stack → renderer material set. */
export function bakeMaterialSet(stacks, size = 512, onProgress = null) {
  const names = Object.keys(stacks);
  const set = { names, size, albedo: [], normal: [], orm: [], params: [] };
  names.forEach((n, i) => {
    const st = stacks[n];
    const b = bakeMaterial(st, size);
    set.albedo.push(b.albedo); set.normal.push(b.normal); set.orm.push(b.orm);
    set.params.push(materialParams(st));
    onProgress?.(i + 1, names.length, n);
  });
  return set;
}

// ---------------------------------------------------------------------------------------------
// Decal atlas: potholes, cracks, patches, oil, manhole, grate, tyre marks, gore hatch
// ---------------------------------------------------------------------------------------------
export const DECALS = ["pothole", "crackweb", "patch", "oil", "manhole", "grate", "tyre", "puddle", "tarseam", "sewerLid"];
export function bakeDecalAtlas(size = 256) {
  const names = DECALS, albedo = [], normal = [];
  for (const name of names) {
    const N = size, n2 = N * N, h = new Float32Array(n2), col = new Float32Array(n2 * 4);
    for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
      const u = (x + 0.5) / N - 0.5, v = (y + 0.5) / N - 0.5, i = y * N + x, r = Math.hypot(u, v) * 2;
      let a = 0, c = [0.1, 0.1, 0.1], hh = 0;
      const ang = Math.atan2(v, u);
      if (name === "pothole") {
        const edge = 0.72 + 0.2 * (fbm(ang / 6.283 + 0.5, 0.3, 6, 3, 2) - 0.5) * 2;
        const t = r / edge;
        if (t < 1) { a = 1; hh = -(1 - t * t) * 0.9 * (0.7 + 0.3 * fbm(u + 0.5, v + 0.5, 12, 4, 3)); const rim = clamp01((t - 0.75) / 0.25); c = [0.12 + rim * 0.2 + fbm(u + 0.5, v + 0.5, 20, 5, 2) * 0.1, 0.11 + rim * 0.15, 0.1 + rim * 0.1]; if (t > 0.9) a = 1 - (t - 0.9) / 0.1; }
        if (t < 0.55) { c = [0.22, 0.19, 0.16].map((k) => k * (0.7 + fbm(u + 0.5, v + 0.5, 30, 6, 2) * 0.7)); }
      } else if (name === "crackweb") {
        const vr = voronoi(u + 0.5, v + 0.5, 5, 21, 1); const e = vr.d2 - vr.d1;
        const w = 0.04 + fbm(u + 0.5, v + 0.5, 8, 22, 2) * 0.03;
        if (e < w) { const t = 1 - e / w; a = t * (1 - smooth(clamp01((r - 0.7) / 0.3))); hh = -t * 0.5; c = [0.07, 0.07, 0.07]; }
      } else if (name === "patch") {
        const bx = Math.abs(u) * 2, by = Math.abs(v) * 2; const d = Math.max(bx, by) + (fbm(u + 0.5, v + 0.5, 6, 31, 2) - 0.5) * 0.08;
        if (d < 0.92) { a = 1; hh = 0.06 * clamp01((0.92 - d) / 0.05); c = [0.1, 0.1, 0.105].map((k) => k * (0.8 + fbm(u + 0.5, v + 0.5, 40, 32, 2) * 0.5)); if (d > 0.84) { c = c.map((k) => k * 0.6); } }
      } else if (name === "oil") {
        const blob = fbm(u + 0.5, v + 0.5, 4, 41, 3); const t = clamp01((0.55 - r + (blob - 0.5) * 0.4) / 0.25);
        a = t * 0.85; c = [0.04, 0.04, 0.05]; hh = 0;
      } else if (name === "manhole" || name === "sewerLid") {
        if (r < 0.98) { a = 1; const rim = r > 0.9 ? 1 : 0; const pattern = name === "manhole" ? (Math.sin(u * 60) * Math.sin(v * 60) > 0.3 ? 1 : 0) : (Math.sin(ang * 12) > 0.6 && r > 0.25 && r < 0.85 ? 1 : 0); hh = rim ? -0.4 : pattern * 0.25; c = rim ? [0.08, 0.08, 0.08] : [0.2, 0.19, 0.17].map((k) => k * (0.8 + pattern * 0.3 + fbm(u + 0.5, v + 0.5, 30, 51, 2) * 0.3)); if (r > 0.95) hh = -0.9; }
      } else if (name === "grate") {
        const bx = Math.abs(u) * 2, by = Math.abs(v) * 2;
        if (bx < 0.95 && by < 0.95) { a = 1; const frame = bx > 0.85 || by > 0.85; const slot = Math.sin(v * 44) > 0.2 && bx < 0.8; hh = frame ? 0 : slot ? -1 : -0.1; c = frame ? [0.15, 0.15, 0.14] : slot ? [0.02, 0.02, 0.02] : [0.18, 0.17, 0.16]; }
      } else if (name === "tyre") {
        const band = Math.abs(Math.abs(u) - 0.22) < 0.06; const t = band ? 1 - smooth(clamp01((Math.abs(v) - 0.3) / 0.2)) : 0;
        a = t * (0.35 + fbm(u + 0.5, v + 0.5, 20, 61, 2) * 0.4); c = [0.05, 0.05, 0.05]; hh = 0;
      } else if (name === "puddle") {
        const blob = fbm(u + 0.5, v + 0.5, 3, 71, 3); const t = clamp01((0.6 - r + (blob - 0.5) * 0.5) / 0.08); a = t * 0.6; c = [0.08, 0.09, 0.1]; hh = -0.02;
      } else if (name === "tarseam") {
        const wobble = (fbm(u + 0.5, v + 0.5, 5, 81, 2) - 0.5) * 0.08; if (Math.abs(u + wobble) < 0.08) { a = 0.9; c = [0.03, 0.03, 0.03]; hh = 0.08; }
      }
      h[i] = hh; col[i * 4] = c[0]; col[i * 4 + 1] = c[1]; col[i * 4 + 2] = c[2]; col[i * 4 + 3] = a;
    }
    const A = new Uint8ClampedArray(n2 * 4), Nm = new Uint8ClampedArray(n2 * 4);
    const H = (x, y) => h[Math.min(N - 1, Math.max(0, y)) * N + Math.min(N - 1, Math.max(0, x))];
    for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
      const i = y * N + x;
      const dx = (H(x + 1, y) - H(x - 1, y)) * 0.5, dy = (H(x, y + 1) - H(x, y - 1)) * 0.5;
      let nx = -dx * N * 0.08, ny = -dy * N * 0.08, nz = 1; const l = Math.hypot(nx, ny, nz); nx /= l; ny /= l; nz /= l;
      Nm[i * 4] = (nx * 0.5 + 0.5) * 255; Nm[i * 4 + 1] = (ny * 0.5 + 0.5) * 255; Nm[i * 4 + 2] = (nz * 0.5 + 0.5) * 255; Nm[i * 4 + 3] = 255;
      A[i * 4] = col[i * 4] * 255; A[i * 4 + 1] = col[i * 4 + 1] * 255; A[i * 4 + 2] = col[i * 4 + 2] * 255; A[i * 4 + 3] = col[i * 4 + 3] * 255;
    }
    albedo.push(A); normal.push(Nm);
  }
  return { names, size, albedo, normal };
}

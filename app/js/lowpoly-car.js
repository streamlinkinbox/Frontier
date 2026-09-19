/* ============================================================
   FRONTIER · procedural low-poly vehicle generator
   ------------------------------------------------------------
   Everything you see is generated from a spec object at runtime:
   no imported meshes, no textures, no baked models.

   Frame of reference:  metres, +X = forwards, +Y = up, +Z = left.
   Origin = centre of the wheelbase, on the ground plane.

   The body is built as two "lofts" (a closed cross-section swept
   along a list of X stations, facet by facet) plus a kit of hinged
   panels that share exactly the same profile math, so every panel
   lines up with the shell it closes:

     hull        sills, arches, fenders, cabin tub, bays, deck
     greenhouse  pillars, roof, windshield, rear glass
     bonnet      hinged at the cowl
     boot/tail   hinged at the deck (or the roof rail on a hatch)
     doors       hinged at their leading edge, with a glass pocket
                 so the windows can roll down *into* the door
   ============================================================ */
import * as THREE from 'three';

/* ------------------------------------------------------------
   0 · small math helpers
   ------------------------------------------------------------ */
export const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
export const lerp = (a, b, t) => a + (b - a) * t;
export const smoothstep = t => t * t * (3 - 2 * t);
export const V3 = (x, y, z) => new THREE.Vector3(x, y, z);

/** stable string/number hash -> uint32 */
export function hashSeed(seed) {
  const s = String(seed);
  let h = 2166136261 >>> 0;
  for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619); }
  return h >>> 0;
}

/** mulberry32 — tiny deterministic PRNG */
export function rng(seed) {
  let a = hashSeed(seed);
  return function () {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

/**
 * Piecewise-linear ramp through [x, value] keyframes (ascending x).
 * Values are clamped at both ends, so keys never need padding.
 */
function ramp(keys) {
  const k = keys.slice().sort((a, b) => a[0] - b[0]);
  return x => {
    if (x <= k[0][0]) return k[0][1];
    for (let i = 1; i < k.length; i++) {
      if (x <= k[i][0]) {
        const [x0, v0] = k[i - 1], [x1, v1] = k[i];
        const d = x1 - x0;
        return d === 0 ? v1 : lerp(v0, v1, (x - x0) / d);
      }
    }
    return k[k.length - 1][1];
  };
}

const f2 = n => Math.round(n * 1000) / 1000;

/* ------------------------------------------------------------
   1 · geometry sink  (non-indexed, flat shaded, bucketed by material)
   ------------------------------------------------------------ */
const EPS = 1e-9;

class GeoBuf {
  constructor() { this.b = new Map(); }
  _b(m) { let a = this.b.get(m); if (!a) { a = []; this.b.set(m, a); } return a; }
  tri(m, a, b, c) {
    // skip slivers so flat normals never blow up
    const ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    const vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    const cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
    if (cx * cx + cy * cy + cz * cz < EPS) return;
    const A = this._b(m);
    A.push(a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z);
  }
  quad(m, a, b, c, d) { this.tri(m, a, b, c); this.tri(m, a, c, d); }
  /** flat polygon (fan from centroid). pts = Vector3[] */
  poly(m, pts, flip) {
    if (pts.length < 3) return;
    const c = new THREE.Vector3();
    pts.forEach(p => c.add(p)); c.multiplyScalar(1 / pts.length);
    for (let i = 0; i < pts.length; i++) {
      const a = pts[i], b = pts[(i + 1) % pts.length];
      if (flip) this.tri(m, c, b, a); else this.tri(m, c, a, b);
    }
  }
  /** axis-aligned-ish box between two points with a rectangular section */
  strut(m, a, b, w, h, up = UP) {
    const dir = b.clone().sub(a);
    const len = dir.length(); if (len < EPS) return;
    dir.multiplyScalar(1 / len);
    let u = up.clone();
    if (Math.abs(u.dot(dir)) > 0.985) u.set(dir.z, dir.x, dir.y).normalize();
    const side = new THREE.Vector3().crossVectors(u, dir).normalize();
    const nrm = new THREE.Vector3().crossVectors(dir, side).normalize();
    const sw = side.clone().multiplyScalar(w / 2), nh = nrm.clone().multiplyScalar(h / 2);
    const A = a.clone().add(sw).add(nh), B = a.clone().sub(sw).add(nh);
    const C = a.clone().sub(sw).sub(nh), D = a.clone().add(sw).sub(nh);
    const E = b.clone().add(sw).add(nh), F = b.clone().sub(sw).add(nh);
    const G = b.clone().sub(sw).sub(nh), H = b.clone().add(sw).sub(nh);
    this.quad(m, A, E, H, D); this.quad(m, D, H, G, C);
    this.quad(m, C, G, F, B); this.quad(m, B, F, E, A);
    this.quad(m, A, B, C, D); this.quad(m, E, F, G, H);
  }
  box(m, cx, cy, cz, sx, sy, sz, rx = 0) {
    const g = new THREE.BoxGeometry(sx, sy, sz);
    if (rx) g.rotateZ(rx);
    this.geo(m, g, cx, cy, cz);
    g.dispose();
  }
  /** append any THREE.BufferGeometry (indexed or not) as raw triangles */
  geo(m, g, ox = 0, oy = 0, oz = 0) {
    const p = g.attributes.position.array;
    const idx = g.index ? g.index.array : null;
    const A = this._b(m);
    const push = i => A.push(p[i * 3] + ox, p[i * 3 + 1] + oy, p[i * 3 + 2] + oz);
    if (idx) { for (let i = 0; i < idx.length; i++) push(idx[i]); }
    else { for (let i = 0; i < p.length / 3; i++) push(i); }
    return this;
  }
  /** merge another sink in, transformed */
  add(other, mtx) {
    for (const [m, arr] of other.b) {
      const A = this._b(m);
      const v = new THREE.Vector3();
      for (let i = 0; i < arr.length; i += 3) {
        v.set(arr[i], arr[i + 1], arr[i + 2]);
        if (mtx) v.applyMatrix4(mtx);
        A.push(v.x, v.y, v.z);
      }
    }
  }
  get tris() { let n = 0; for (const a of this.b.values()) n += a.length / 9; return n; }
  /** -> Map(materialKey -> BufferGeometry) */
  build() {
    const out = new Map();
    for (const [m, arr] of this.b) {
      if (!arr.length) continue;
      const g = new THREE.BufferGeometry();
      g.setAttribute('position', new THREE.Float32BufferAttribute(arr, 3));
      g.computeVertexNormals();
      g.computeBoundingSphere();
      out.set(m, g);
    }
    return out;
  }
}
const UP = new THREE.Vector3(0, 1, 0);

/** turn a GeoBuf into a Group of meshes */
function meshes(gb, mats, castShadow = true) {
  const g = new THREE.Group();
  for (const [key, geo] of gb.build()) {
    const mat = mats[key] || mats.paint;
    const mesh = new THREE.Mesh(geo, mat);
    mesh.castShadow = castShadow;
    mesh.receiveShadow = true;
    g.add(mesh);
  }
  return g;
}

/* ------------------------------------------------------------
   2 · material keys + palette
   ------------------------------------------------------------ */
export const MAT = {
  paint: 'paint', roof: 'roof', cabin: 'cabin', trim: 'trim', clad: 'clad', liner: 'liner',
  inner: 'inner', seat: 'seat', glass: 'glass', chrome: 'chrome',
  head: 'head', headGlass: 'headGlass', tail: 'tail', tyre: 'tyre',
  rim: 'rim', brake: 'brake', caliper: 'caliper', accent: 'accent', bay: 'bay',
  livery: 'livery'
};

export const PAINTS = [
  { id: 'frontier', name: 'Frontier Blue', body: '#1f5eff', roof: '#0d0d0f' },
  { id: 'ink', name: 'Ink Black', body: '#131316', roof: '#0a0a0b' },
  { id: 'glacier', name: 'Glacier', body: '#e8ecf2', roof: '#111214' },
  { id: 'graphite', name: 'Graphite', body: '#3a3d43', roof: '#15161a' },
  { id: 'signal', name: 'Signal Red', body: '#d32029', roof: '#101012' },
  { id: 'acid', name: 'Acid', body: '#c8f02a', roof: '#14150f' },
  { id: 'sand', name: 'Dune', body: '#c2a878', roof: '#1a1712' },
  { id: 'vapor', name: 'Vapour', body: '#7f8ea3', roof: '#101318' }
];

function makeMaterials(spec) {
  const flat = true, two = THREE.DoubleSide;
  const p = spec.paint;
  const paint = new THREE.MeshStandardMaterial({
    color: new THREE.Color(p.body), roughness: 0.34, metalness: 0.42,
    flatShading: flat, side: two, envMapIntensity: 1.15
  });
  const roof = new THREE.MeshStandardMaterial({
    color: new THREE.Color(p.roof || p.body), roughness: 0.42, metalness: 0.3,
    flatShading: flat, side: two, envMapIntensity: 1.0
  });
  const mk = (color, roughness, metalness, extra = {}) => new THREE.MeshStandardMaterial(
    Object.assign({ color: new THREE.Color(color), roughness, metalness, flatShading: flat, side: two, envMapIntensity: 0.9 }, extra));
  return {
    paint, roof,
    trim: mk('#1b1c20', 0.62, 0.15),
    clad: mk(spec.cladding ? '#15161a' : p.body, 0.78, 0.05),
    liner: mk('#0e0f12', 0.95, 0.0),
    inner: mk('#191a1e', 0.9, 0.02),
    seat: mk(spec.interior || '#22242a', 0.92, 0.0),
    cabin: (() => { const m = mk(spec.cabinColor || '#f4efe6', 0.55, 0.05); m.side = THREE.DoubleSide; return m; })(),
    bay: mk('#101114', 0.85, 0.08),
    glass: new THREE.MeshPhysicalMaterial({
      color: new THREE.Color(spec.glassTint || '#101a26'), roughness: 0.06, metalness: 0.0,
      transparent: true, opacity: spec.glassOpacity ?? 0.44, side: two, depthWrite: false,
      clearcoat: 1, clearcoatRoughness: 0.04, envMapIntensity: 1.9, flatShading: flat
    }),
    chrome: mk('#c9d2dc', 0.18, 1.0),
    headGlass: new THREE.MeshPhysicalMaterial({
      color: new THREE.Color('#dfe8f5'), roughness: 0.08, metalness: 0.0, transparent: true,
      opacity: 0.55, side: two, depthWrite: false, flatShading: flat, envMapIntensity: 2
    }),
    head: mk('#f2f6ff', 0.3, 0.1, { emissive: new THREE.Color('#cfe0ff'), emissiveIntensity: 1.5 }),
    tail: mk('#ff2a35', 0.35, 0.1, { emissive: new THREE.Color('#ff1f2d'), emissiveIntensity: 1.7 }),
    accent: mk(spec.accent || '#1f5eff', 0.35, 0.2, { emissive: new THREE.Color(spec.accent || '#1f5eff'), emissiveIntensity: 0.35 }),
    tyre: mk('#141519', 0.96, 0.0),
    rim: mk(spec.rim.color, spec.rim.roughness ?? 0.32, spec.rim.metalness ?? 0.85),
    brake: mk('#767c86', 0.45, 0.8),
    caliper: mk('#1f5eff', 0.4, 0.25),
    livery: mk(spec.livery.color, 0.4, 0.2)
  };
}

/* ------------------------------------------------------------
   3 · style presets
   ------------------------------------------------------------ */
export const STYLES = {
  proto: {
    id: 'proto', name: 'Proto', length: 4.32, wheelbase: 2.56, halfWidth: 0.93,
    ground: 0.17, belt: 0.84, roof: 1.26, floor: 0.42, wheelR: 0.345, tyreW: 0.315,
    track: 0.80, cowlBack: 0.62, wsRun: 0.52, roofLen: 1.02, rrRun: 0.50,
    doors: 1, tailgate: false, shelf: 0, bayDepth: 0.20, archGap: 0.035,
    noseTop: 0.76, tailTop: 0.80, deckRear: 0.82, hoodFront: 0.68, tailDeck: 0.80,
    roofHalf: 0.60, glassHalf: 0.80, crown: 0.17, flare: 0.0, wing: 0, splitter: 0,
    quarterGlass: false, framedDoors: false, cladding: false, toy: true, ride: 0
  },
  coupe: {
    id: 'coupe', name: 'Coupe', length: 4.44, wheelbase: 2.62, halfWidth: 0.94,
    ground: 0.135, belt: 0.95, roof: 1.275, floor: 0.37, wheelR: 0.345, tyreW: 0.245,
    track: 0.775, cowlBack: 0.44, wsRun: 0.74, roofLen: 0.96, rrRun: 0.58,
    doors: 1, tailgate: false, shelf: 0.30, bayDepth: 0.30, archGap: 0.055,
    noseTop: 0.63, tailTop: 0.90, deckRear: 0.98, hoodFront: 0.40, tailDeck: 0.44,
    roofHalf: 0.70, glassHalf: 0.80, crown: 0.035, flare: 0.012, wing: 0, splitter: 0,
    quarterGlass: true, framedDoors: false, cladding: false, ride: 0
  },
  sedan: {
    id: 'sedan', name: 'Sedan', length: 4.80, wheelbase: 2.83, halfWidth: 0.92,
    ground: 0.155, belt: 0.985, roof: 1.445, floor: 0.38, wheelR: 0.345, tyreW: 0.225,
    track: 0.775, cowlBack: 0.36, wsRun: 0.62, roofLen: 1.24, rrRun: 0.56,
    doors: 2, tailgate: false, shelf: 0.34, bayDepth: 0.34, archGap: 0.06,
    noseTop: 0.68, tailTop: 0.95, deckRear: 1.00, hoodFront: 0.44, tailDeck: 0.46,
    roofHalf: 0.735, glassHalf: 0.815, crown: 0.03, flare: 0.006, wing: 0, splitter: 0,
    quarterGlass: true, framedDoors: true, cladding: false, ride: 0
  },
  hatch: {
    id: 'hatch', name: 'Hatch', length: 4.26, wheelbase: 2.66, halfWidth: 0.90,
    ground: 0.175, belt: 1.00, roof: 1.475, floor: 0.40, wheelR: 0.352, tyreW: 0.225,
    track: 0.765, cowlBack: 0.16, wsRun: 0.58, roofLen: 1.36, rrRun: 0.98,
    doors: 2, tailgate: true, shelf: 0.0, bayDepth: 0.16, archGap: 0.06,
    noseTop: 0.70, tailTop: 1.06, deckRear: 1.02, hoodFront: 0.40, tailDeck: 0.26,
    roofHalf: 0.735, glassHalf: 0.815, crown: 0.03, flare: 0.010, wing: 0.35, splitter: 0,
    quarterGlass: true, framedDoors: true, cladding: true, ride: 0
  },
  gt: {
    id: 'gt', name: 'GT', length: 4.58, wheelbase: 2.72, halfWidth: 1.00,
    ground: 0.085, belt: 0.935, roof: 1.185, floor: 0.34, wheelR: 0.362, tyreW: 0.285,
    track: 0.835, cowlBack: 0.52, wsRun: 0.70, roofLen: 0.88, rrRun: 0.62,
    doors: 1, tailgate: false, shelf: 0.26, bayDepth: 0.30, archGap: 0.05,
    noseTop: 0.55, tailTop: 0.88, deckRear: 0.94, hoodFront: 0.42, tailDeck: 0.46,
    roofHalf: 0.665, glassHalf: 0.775, crown: 0.03, flare: 0.035, wing: 1, splitter: 1,
    quarterGlass: false, framedDoors: false, cladding: false, ride: 0
  },
  ute: {
    id: 'ute', name: 'Ute', length: 5.18, wheelbase: 3.16, halfWidth: 0.97,
    ground: 0.255, belt: 1.06, roof: 1.80, floor: 0.44, wheelR: 0.425, tyreW: 0.265,
    track: 0.80, cowlBack: 0.38, wsRun: 0.56, roofLen: 1.06, rrRun: 0.34,
    doors: 2, tailgate: false, shelf: 0.0, bayDepth: 0.0, archGap: 0.075,
    noseTop: 0.86, tailTop: 1.00, deckRear: 1.02, hoodFront: 0.46, tailDeck: 0.22,
    roofHalf: 0.745, glassHalf: 0.83, crown: 0.03, flare: 0.030, wing: 0, splitter: 0,
    quarterGlass: false, framedDoors: true, cladding: true, ride: 0,
    bed: { len: 1.68, floor: 0.90, rail: 1.02, dropTail: true }
  }
};

export const STYLE_IDS = Object.keys(STYLES);

/* ------------------------------------------------------------
   4 · spec  (style preset + seed variation + user overrides)
   ------------------------------------------------------------ */
const RIMS = ['gunmetal', 'silver', 'black', 'machined', 'white'];
const RIM_COLORS = {
  gunmetal: ['#4a4e55', 0.34, 0.85], silver: ['#c3c9d1', 0.26, 0.9],
  black: ['#181a1d', 0.45, 0.5], machined: ['#8f959c', 0.2, 0.95], white: ['#e6e8ea', 0.3, 0.35]
};

export function makeSpec(o = {}) {
  const styleId = STYLES[o.style] ? o.style : 'coupe';
  const st = STYLES[styleId];
  const seed = o.seed == null ? 'FR-0001' : o.seed;
  const r = rng(seed + '|' + styleId);
  const j = (a, b) => a + r() * (b - a);
  const pick = arr => arr[Math.floor(r() * arr.length) % arr.length];

  // ---- user overrides (sliders) -------------------------------------
  const length = o.length ?? st.length * j(0.985, 1.015);
  const wheelbase = clamp(o.wheelbase ?? st.wheelbase * j(0.99, 1.01), length * 0.48, length * 0.68);
  const widthScale = (o.width ?? st.halfWidth * 2 * j(0.985, 1.02)) / (st.halfWidth * 2);
  const roof = o.roof ?? st.roof * j(0.985, 1.02);
  const ground = clamp(o.ride ?? st.ground + st.ride + j(-0.012, 0.02), 0.055, 0.42);
  const wheelR = clamp(o.wheel ?? st.wheelR * j(0.97, 1.05), 0.24, 0.52);
  const facets = (o.facets ?? j(0.85, 1.2)) * (st.toy ? 0.5 : 1);

  // ---- seed-driven character ----------------------------------------
  const rimStyle = pick(['spoke', 'split', 'dish', 'turbine', 'mesh']);
  const rimName = pick(RIMS);
  const paint = o.paint
    || (r() < 0.34 ? PAINTS[1 + Math.floor(r() * (PAINTS.length - 1))] : PAINTS[Math.floor(r() * PAINTS.length)]);
  const roofPaint = o.paint ? (paint.roof || paint.body)
    : (r() < 0.55 ? (paint.roof || paint.body) : paint.body);                // two-tone roof
  const liveryOn = o.livery != null ? o.livery : (styleId === 'gt' ? true : r() < 0.3);

  const spec = {
    seed, style: styleId, styleName: st.name,
    length, wheelbase, halfWidth: st.halfWidth * widthScale,
    ground, belt: st.belt * (o.belt != null ? o.belt / st.belt : 1), roof, floor: st.floor,
    wheelR, tyreW: st.tyreW * (wheelR / st.wheelR) * j(0.95, 1.06),
    track: st.track * widthScale, wheelbaseScale: 1,
    cowlBack: st.cowlBack, wsRun: st.wsRun * j(0.94, 1.06), roofLen: st.roofLen,
    rrRun: st.rrRun, doors: o.doors ?? st.doors, tailgate: st.tailgate,
    shelf: st.shelf, bayDepth: st.bayDepth, archGap: st.archGap,
    noseTop: st.noseTop, tailTop: st.tailTop, deckRear: st.deckRear,
    hoodFront: st.hoodFront, tailDeck: st.tailDeck,
    roofHalf: st.roofHalf * widthScale, glassHalf: st.glassHalf * widthScale,
    crown: st.crown, flare: st.flare * j(0.6, 1.7), wing: st.wing, splitter: st.splitter,
    quarterGlass: st.quarterGlass, framedDoors: st.framedDoors, cladding: st.cladding,
    bed: st.bed, facets,
    rhd: r() < 0.28,
    mirrors: pick(['flag', 'arm', 'cap']),
    lampStyle: pick(['bar', 'pod', 'slit']),
    rim: { style: rimStyle, name: rimName, color: RIM_COLORS[rimName][0], roughness: RIM_COLORS[rimName][1], metalness: RIM_COLORS[rimName][2], spokes: 5 + Math.floor(r() * 4) },
    paint: { body: paint.body, roof: roofPaint, name: paint.name, id: paint.id },
    livery: { on: liveryOn, color: r() < 0.5 ? '#f2f4f8' : (paint.id === 'frontier' ? '#f2f4f8' : '#1f5eff'), width: j(0.055, 0.10), gap: j(0.02, 0.05) },
    accent: '#1f5eff', interior: pick(['#22242a', '#1d1f24', '#2a2320', '#1b2130']),
    glassTint: pick(['#0f1a26', '#141416', '#101820']), glassOpacity: j(0.38, 0.52),
    wipers: !st.toy && r() < 0.8, badges: !st.toy, spare: !st.toy && r() < 0.5, towHook: !st.toy && r() < 0.3,
    exhaust: pick(['single', 'twin', 'quad', 'centre']),
    archLip: !st.toy && r() < 0.6, roofRails: !st.toy && (styleId === 'ute' || r() < 0.15),
    lightbar: !st.toy && styleId === 'ute' && r() < 0.5,
    toy: !!st.toy,
    cabinColor: st.toy ? (r() < 0.62 ? pick(['#f4efe6', '#ffffff', '#e9e3d7']) : paint.body) : (paint.roof || paint.body),
    whitewall: st.toy && r() < 0.45, roundel: st.toy && r() < 0.6,
  };
  spec.belt = clamp(spec.belt, ground + 0.34, roof - 0.16);
  spec.floor = clamp(spec.floor, ground + 0.10, spec.belt - 0.30);
  return spec;
}

/* ------------------------------------------------------------
   5 · layout — every key X station + the door bays
   ------------------------------------------------------------ */
export function layout(spec) {
  const L = spec.length, wb = spec.wheelbase;
  const xF = L / 2, xR = -L / 2, xFA = wb / 2, xRA = -wb / 2;

  const xHoodF = xF - spec.hoodFront;
  const xTrunkR = xR + spec.tailDeck;
  const archSpanF = spec.wheelR + 0.11 + spec.flare;
  const archSpanR = spec.wheelR + 0.12 + spec.flare;

  // door bays ------------------------------------------------------
  const bayFront = xFA - archSpanF - 0.055;              // leading edge, behind the front arch
  const bayRear = Math.max(xRA + archSpanR + 0.06, xR + spec.tailDeck + spec.wheelR * 0.5);
  const doors = clamp(spec.doors, 1, 2);
  const bays = [];
  if (doors === 1) {
    bays.push({ id: 'doorL', side: 1, x0: bayRear, x1: bayFront, front: true });
    bays.push({ id: 'doorR', side: -1, x0: bayRear, x1: bayFront, front: true });
  } else {
    const span = bayFront - bayRear;
    const bPillar = 0.115;
    const split = bayFront - span * (spec.style === 'ute' ? 0.52 : 0.545);
    bays.push({ id: 'doorFL', side: 1, x0: split + bPillar / 2, x1: bayFront, front: true });
    bays.push({ id: 'doorFR', side: -1, x0: split + bPillar / 2, x1: bayFront, front: true });
    bays.push({ id: 'doorRL', side: 1, x0: bayRear, x1: split - bPillar / 2, front: false });
    bays.push({ id: 'doorRR', side: -1, x0: bayRear, x1: split - bPillar / 2, front: false });
  }
  bays.forEach(b => {
    b.len = b.x1 - b.x0;
    b.glassX0 = b.x0 + 0.045;                  // window opening, rear edge
    b.glassX1 = b.x1 - 0.045;                  // window opening, leading edge
  });
  // the raked A-pillar wedge starts at the leading door glass edge
  const pillarFront = Math.min(...bays.filter(b => b.front).map(b => b.glassX1));

  // cabin / deck key points ---------------------------------------
  let xCowl = Math.max(xFA - spec.cowlBack, bayFront + 0.05);
  const xRoofF = xCowl - spec.wsRun;
  const xRoofR = xRoofF - spec.roofLen;
  let xCabR = xRoofR - spec.rrRun;
  xCabR = Math.min(xCabR, xRoofR - 0.12);          // always behind the roof break
  if (spec.tailgate) xCabR = Math.min(xCabR, xR + spec.tailDeck + 0.02);
  else xCabR = Math.max(xCabR, xTrunkR + 0.26);     // leave room for a boot lid

  // ute bed ---------------------------------------------------------
  let bed = null;
  if (spec.bed) {
    const xBedR = xR + spec.tailDeck;
    const xBedF = xCabR + 0.02;
    bed = { x0: xBedF, x1: xBedR, floor: spec.bed.floor, rail: spec.bed.rail, dropTail: spec.bed.dropTail };
  }

  const sp = Object.assign({}, spec, {
    xF, xR, xFA, xRA, xCowl, xRoofF, xRoofR, xCabR, xHoodF, xTrunkR,
    archSpanF, archSpanR, bays, doors, bed, pillarFront,
    bayFront, bayRear,
    // ---- inner/outer widths
    hwOut: spec.halfWidth,
    hwInner: spec.halfWidth - (spec.doors > 0 ? 0.115 : 0.09),
    doorT: 0.10,
    // ---- longitudinal regions
    inBay(x) { return bays.some(b => x > b.x0 + 1e-4 && x < b.x1 - 1e-4); },
    bayAt(x) { return bays.find(b => x > b.x0 + 1e-4 && x < b.x1 - 1e-4) || null; },
    inCabin(x) { return x > xCabR - 1e-4 && x < xCowl - 0.03; },
    inBed(x) { return !!bed && x > bed.x0 && x < bed.x1; },
    inHoodBay(x) { return x > xHoodF && x < xCowl - 0.03; },
    inTrunkBay(x) { return !spec.tailgate && !bed && x > xCabR + 0.02 && x < xTrunkR; },
    inBayWell(x) { return sp.inHoodBay(x) || sp.inTrunkBay(x); }
  });

  // ---- arch notch (faceted trapezoid) -----------------------------
  const archShape = t => { const a = Math.abs(t); return a < 0.46 ? 1 : a < 0.68 ? 0.80 : a < 0.85 ? 0.48 : a < 1 ? 0.16 : 0; };
  sp.archLift = x => {
    if (spec.toy) return 0;                       // clay prototypes: no arch cut-outs
    const lift = spec.wheelR * 2 + spec.archGap - spec.ground;
    const f = archShape((x - xFA) / archSpanF) * (x > (xFA + xRA) / 2 ? 1 : 0);
    const r = archShape((x - xRA) / archSpanR) * (x <= (xFA + xRA) / 2 ? 1 : 0);
    return Math.max(f, r) * Math.max(0, lift);
  };
  sp.archN = x => Math.max(
    archShape((x - xFA) / archSpanF) * (x > 0 ? 1 : 0),
    archShape((x - xRA) / archSpanR) * (x <= 0 ? 1 : 0));

  // ---- profile curves ---------------------------------------------
  const hw = spec.halfWidth;
  const taper = [
    [xR, 0.845], [xR + 0.18, 0.905], [xTrunkR, 0.965], [xRA, 1.0],
    [xCabR, 1.0], [xCowl, 1.0], [xFA, 1.0], [xHoodF, 0.985],
    [xF - 0.16, 0.93], [xF, 0.795]
  ].map(([x, k]) => [x, k]);
  const widthAt = ramp(taper);
  sp.hwBotF = x => hw * widthAt(x) * 0.955;
  sp.hwWaistF = x => hw * widthAt(x) * 0.995;
  sp.hwUpF = x => hw * widthAt(x) * (1.0 + spec.flare * 1.4 * sp.archN(x));
  sp.hwBotFlared = x => sp.hwBotF(x) * (1 + spec.flare * sp.archN(x));

  // deck (outer top surface) height
  const deck = ramp([
    [xR, spec.tailTop * (spec.toy ? 0.99 : 0.94)], [xTrunkR, spec.deckRear],
    [xCabR + 0.02, spec.deckRear + 0.005], [xCowl - 0.02, spec.belt + 0.02],
    [xCowl + 0.05, spec.belt - 0.02], [xHoodF + 0.15, spec.noseTop + (spec.belt - spec.noseTop) * 0.55],
    [xHoodF, spec.noseTop + (spec.belt - spec.noseTop) * 0.42],
    [xF - 0.10, spec.noseTop * (spec.toy ? 1.0 : 0.97)], [xF, spec.noseTop * (spec.toy ? 0.98 : 0.90)]
  ]);
  sp.deckF = deck;

  // greenhouse roof line (belt -> roof -> belt)
  const roofLine = ramp([
    [xCowl, spec.belt + 0.015], [xCowl + spec.wsRun * 0.5, lerp(spec.belt, spec.roof, 0.55)],
    [xRoofF, spec.roof], [xRoofF + spec.roofLen * 0.5, spec.roof + 0.008],
    [xRoofR, spec.roof],
    spec.tailgate
      ? [xCabR, Math.max(spec.belt + 0.30, spec.roof - (spec.roof - spec.belt) * 0.30)]
      : [xCabR, spec.belt + 0.02]
  ]);
  sp.roofF = roofLine;
  sp.hwRoofF = ramp([[xCowl, spec.glassHalf * 0.995], [xRoofF, spec.roofHalf], [xRoofR, spec.roofHalf], [xCabR, spec.roofHalf * (spec.tailgate ? 1.02 : 0.94)]]);
  sp.hwGlassF = ramp([[xCowl, spec.glassHalf], [xRoofF, spec.glassHalf * 0.995], [xRoofR, spec.glassHalf * 0.99], [xCabR, spec.glassHalf * (spec.tailgate ? 1.03 : 0.93)]]);

  // bottom of the body
  sp.yBotF = ramp([
    [xR, spec.ground + 0.10], [xR + 0.22, spec.ground + 0.035],
    [xTrunkR, spec.ground + 0.01], [xCabR, spec.ground + (spec.cladding ? 0.03 : 0.0)],
    [xCowl, spec.ground + 0.005], [xHoodF, spec.ground + 0.0],
    [xF - 0.30, spec.ground + 0.02], [xF - 0.08, spec.ground + 0.09], [xF, spec.ground + 0.14]
  ]);
  if (spec.toy) {                                 // straight slab underside, wheels overlap it
    const yb = spec.ground * 0.44;
    sp.yBotF = ramp([[xR, yb + 0.05], [xR + 0.20, yb], [xF - 0.20, yb], [xF, yb + 0.05]]);
  }
  sp.chamF = ramp([
    [xR, 0.075], [xTrunkR, 0.06], [xCabR, spec.cladding ? 0.10 : 0.075],
    [xCowl, 0.06], [xF - 0.2, 0.055], [xF, 0.085]
  ]);
  sp.waistF = ramp([
    [xR, spec.belt - 0.30], [xTrunkR, spec.belt - 0.30], [xCabR, spec.belt - 0.33],
    [xCowl, spec.belt - 0.30], [xHoodF, spec.belt - 0.26], [xF, spec.noseTop - 0.17]
  ]);

  // ---- station list ------------------------------------------------
  const must = new Set([xR, xR + 0.06, xTrunkR, xCabR, xCowl, xHoodF, xRoofF, xRoofR, xFA, xRA, xF, xF - 0.06, 0]);
  if (spec.toy) [xR + 0.06, xF - 0.06, 0].forEach(x => must.delete(x));
  if (bed) [bed.x0, bed.x1, (bed.x0 + bed.x1) / 2].forEach(x => must.add(x));
  (spec.toy ? [] : [[xFA, archSpanF], [xRA, archSpanR]]).forEach(([ax, s]) => {
    (spec.toy ? [0.62, 1.0] : [0.46, 0.68, 0.85, 1.0]).forEach(k => { must.add(ax - s * k - 0.008); must.add(ax - s * k + 0.008); must.add(ax + s * k - 0.008); must.add(ax + s * k + 0.008); });
    if (!spec.toy) must.add(ax);
  });
  bays.forEach(b => { must.add(b.x0 - 0.014); must.add(b.x0 + 0.014); must.add(b.x1 - 0.014); must.add(b.x1 + 0.014); must.add((b.x0 + b.x1) / 2); });
  if (spec.shelf > 0) must.add(xCabR - spec.shelf);

  const sorted = [...must].filter(x => x >= xR - 1e-6 && x <= xF + 1e-6).sort((a, b) => a - b);
  const seg = (spec.toy ? 0.62 : 0.235) * clamp(spec.facets, 0.5, 2);
  const xs = [sorted[0]];
  for (let i = 1; i < sorted.length; i++) {
    const a = xs[xs.length - 1], b = sorted[i];
    const d = b - a;
    if (d < 0.028) continue;
    const n = Math.max(1, Math.round(d / seg));
    for (let k = 1; k <= n; k++) xs.push(f2(a + (d * k) / n));
  }
  sp.stations = xs.map(x => f2(x));
  return sp;
}

/* ------------------------------------------------------------
   6 · cross-sections
   ------------------------------------------------------------ */
/**
 * Hull cross-section: 8 [y,z] pairs from the bottom centre, up the
 * left side, to the top centre. Mirrored later to close the loop.
 */
export function hullSection(sp, x) {
  const open = sp.inBay(x);
  const cabin = sp.inCabin(x), inBed = sp.inBed(x);
  const lift = sp.archLift(x);
  const arch = sp.archN(x);

  const yBot = sp.yBotF(x) + lift;
  const cham = sp.toy ? 0.03 : sp.chamF(x) * (1 - arch * 0.35);
  const sillTop = yBot + cham;

  // belt / shoulder line -----------------------------------------
  let yUp, hwU, hwT, yTop, hwInner = sp.hwInner;
  if (inBed) {
    yUp = sp.bed.rail; hwU = sp.hwUpF(x); yTop = sp.bed.floor; hwT = hwInner = hwU - 0.085;
  } else if (cabin) {
    yUp = sp.belt; hwU = sp.hwUpF(x);
    const shelfX = sp.xCabR - (sp.shelf || 0);
    const onShelf = sp.shelf > 0 && x > shelfX;
    if (onShelf) {                                   // parcel shelf / rear bulkhead
      const t = clamp((x - shelfX) / Math.max(0.001, sp.shelf), 0, 1);
      yTop = lerp(sp.floor, sp.belt - 0.02, smoothstep(t));
      if (sp.style === 'ute') yTop = lerp(sp.floor, sp.belt + 0.02, smoothstep(t));
    } else yTop = sp.floor;
    hwT = hwInner;
  } else {
    yUp = sp.deckF(x) - sp.crown * (1 - arch * 0.6);
    hwU = sp.hwUpF(x);
    yTop = sp.deckF(x);
    hwT = hwU - (sp.inBayWell(x) ? (sp.toy ? 0.06 : 0.105) : (sp.toy ? 0.02 : 0.055));
    if (sp.inBayWell(x)) yTop = sp.deckF(x) - sp.bayDepth;
  }

  const hwB = sp.hwBotFlared(x);
  const hwW = sp.hwWaistF(x) * (1 + sp.flare * 0.6 * arch);
  let waistY = Math.max(sillTop + 0.035, sp.waistF(x), yUp - (yUp - sillTop) * 0.42);
  if (sp.toy) { waistY = yUp - 0.09; }                    // flat slab side

  const P = [];
  P.push([yBot, 0]);
  P.push([yBot, hwB * 0.80]);
  if (open) {
    P.push([sillTop, hwU]);                    // aperture bottom, outer
    P.push([sillTop + 0.012, hwInner]);        // sill plate (ledge you step over)
  } else {
    P.push([sillTop, hwB + cham * 0.22]);
    P.push([Math.min(waistY, yUp - 0.05), hwW]);
  }
  P.push([yUp, hwU]);                          // belt / rail outer edge
  P.push([yUp + 0.014, (open || cabin || inBed) ? hwInner : hwT + 0.012]);
  P.push([yTop, hwT]);
  P.push([yTop, 0]);
  return { P, open, cabin, inBed, arch, sillTop, yUp, hwU, hwInner, yBot, lift };
}

/** column roles of the hull loop (14 columns) */
const HULL_COLS = 14;
function hullMirror(i) { return HULL_COLS - 1 - i; }   // 0..6 left -> 13..7 right

/** greenhouse cross-section: outer surface up + inner surface back down */
function ghSection(sp, x) {
  const yb = sp.belt - 0.006;
  const yr = sp.roofF(x);
  let hg = sp.hwGlassF(x), hr = sp.hwRoofF(x);
  if (sp.toy) hr = hg - 0.10;                       // blocky trapezoid cabin
  const t = sp.toy ? 0.07 : 0.055;
  const bev = sp.toy ? 0.02 : 0.055;
  const G = [
    [yb, hg], [yb + (yr - yb) * 0.30, hg], [yr - bev, hr], [yr, hr * 0.55], [yr, 0],
    [yr - 0.042, 0], [yr - bev - 0.03, hr - t], [yb + (yr - yb) * 0.30 - 0.03, hg - t], [yb, hg - t]
  ];
  return G;
}
/* greenhouse columns:
   0 G0-G1 side outer (lower)   1 G1-G2 side outer (upper)
   2 G2-G3 roof outer (l)       3 G3-G4 roof crown
   4 G4-G5 roof outer (r)  == inner roof when mirrored? no: 4 is roof inner (l)
   … see emission table below                                            */

/* ------------------------------------------------------------
   7 · loft emitter
   ------------------------------------------------------------ */
/**
 * Sweep a closed section along X stations.
 *  sections(x) -> array of [y,z]
 *  keep(i, x0, x1) -> material key | null   (null = emit nothing)
 */
export function loft(gb, sp, sections, stations, keep, opts = {}) {
  const zSign = 1;
  const secs = stations.map(x => sections(x));
  const full = secs.map(P => {
    const left = P.map(([y, z]) => V3(0, y, z * zSign));
    const right = P.slice(1, P.length - 1).reverse().map(([y, z]) => V3(0, y, -z));
    return left.concat(right);
  });
  const n = full[0].length;
  for (let s = 0; s < stations.length - 1; s++) {
    const x0 = stations[s], x1 = stations[s + 1];
    const A = full[s], B = full[s + 1];
    for (let k = 0; k < n; k++) {
      const k2 = (k + 1) % n;
      const mat = keep(k, x0, x1, s);
      if (!mat) continue;
      const a = V3(x0, A[k].y, A[k].z), b = V3(x0, A[k2].y, A[k2].z);
      const c = V3(x1, B[k2].y, B[k2].z), d = V3(x1, B[k].y, B[k].z);
      const flip = A[k].z >= 0 && A[k2].z >= A[k].z ? false : undefined;
      gb.quad(mat, a, d, c, b);
      if (flip === false && opts.double) gb.quad(mat, b, c, d, a);
    }
  }
  // end caps
  if (opts.capFront !== false) gb.poly(opts.capFrontMat || MAT.trim, full[full.length - 1].map(p => V3(stations[stations.length - 1], p.y, p.z)), false);
  if (opts.capRear !== false) gb.poly(opts.capRearMat || MAT.trim, full[0].map(p => V3(stations[0], p.y, p.z)), true);
  return full;
}

/* ------------------------------------------------------------
   8 · hull
   ------------------------------------------------------------ */
function buildHull(sp, gb) {
  const st = sp.stations;
  const secs = new Map();
  st.forEach(x => secs.set(x, hullSection(sp, x)));

  const keep = (k, x0, x1) => {
    const x = (x0 + x1) / 2;
    const s = secs.get(x1) || hullSection(sp, x1);
    const left = k <= 6, idx = left ? k : HULL_COLS - 1 - k;
    const open = s.open, arch = s.arch > 0.5;
    const cabin = sp.inCabin(x) || sp.inBed(x);
    const well = sp.inBayWell(x);
    switch (idx) {
      case 0: return MAT.liner;                              // underside
      case 1: return arch ? MAT.liner : (sp.cladding ? MAT.clad : MAT.paint);
      case 2: return open ? MAT.inner : MAT.paint;
      case 3: return open ? null : MAT.paint;                        // <-- door aperture
      case 4: return cabin ? MAT.inner : MAT.paint;          // shoulder / belt rail top
      case 5: return open ? null : (cabin || well ? MAT.inner : MAT.paint);
      case 6: return (cabin || well) ? MAT.bay : MAT.paint;  // deck / floor
      default: return null;
    }
  };

  loft(gb, sp, x => secs.get(x).P, st, keep, { capFrontMat: MAT.paint, capRearMat: MAT.paint });

  // --- cladding band: a chunky protective strip along the sills/arches
  if (sp.cladding && !sp.toy) {
    [1, -1].forEach(side => {
      let prev = null;
      for (const x of st) {
        const s = hullSection(sp, x);
        const p = V3(x, s.yBot + 0.015, side * (sp.hwBotFlared(x) + 0.014));
        if (prev) {
          gb.quad(MAT.clad, prev, p, V3(p.x, p.y + 0.105, p.z), V3(prev.x, prev.y + 0.105, prev.z));
          gb.quad(MAT.clad, V3(prev.x, prev.y + 0.105, prev.z), V3(p.x, p.y + 0.105, p.z),
            V3(p.x, p.y + 0.105, p.z - side * 0.02), V3(prev.x, prev.y + 0.105, prev.z - side * 0.02));
        }
        prev = p;
      }
    });
  }

  // --- arch lips ---------------------------------------------------
  if (sp.archLip && !sp.toy) {
    [[sp.xFA, sp.archSpanF], [sp.xRA, sp.archSpanR]].forEach(([ax, span]) => {
      const N = 7;
      [1, -1].forEach(side => {
        let prev = null;
        for (let i = 0; i <= N; i++) {
          const t = -1 + (2 * i) / N;
          const x = ax + t * span * 0.985;
          const s = hullSection(sp, clamp(x, sp.xR + 0.02, sp.xF - 0.02));
          const y = s.yBot + s.arch * 0.0;
          const z = side * (sp.hwBotFlared(x) + 0.006);
          const p = V3(x, y, z);
          if (prev) gb.quad(MAT.trim, prev.p, p, V3(p.x, p.y + 0.035, p.z - side * 0.022), V3(prev.p.x, prev.p.y + 0.035, prev.p.z - side * 0.022));
          prev = { p };
        }
      });
    });
  }
}

/* ------------------------------------------------------------
   9 · greenhouse (pillars, roof, fixed glass)
   ------------------------------------------------------------ */
function ghSideMat(sp, x) {
  if (x > sp.xCowl || x < sp.xCabR) return null;
  // everything ahead of the leading door glass is the raked A-pillar wedge
  if (x >= sp.pillarFront) return MAT.paint;
  if (sp.toy && (x > sp.xRoofF + 0.02 || x < sp.xRoofR - 0.02)) return MAT.cabin;  // solid A/C wedge
  const bay = sp.bayAt(x);
  if (bay) {
    const g0 = bay.x0 + 0.045, g1 = bay.x1 - 0.045;
    if (x > g0 && x < g1) return 'open';                 // side window opening
    return MAT.trim;                                     // door frame / seal edge
  }
  // between the last bay and the cabin rear: C-pillar (+ quarter glass)
  const lastBay = sp.bays[sp.bays.length - 1];
  if (sp.quarterGlass && x > lastBay.x1 && x < sp.xCabR - 0.06 && !sp.tailgate) return MAT.glass;
  return sp.toy ? MAT.cabin : MAT.paint;
}
function ghTopMat(sp, x) {
  if (sp.toy) return sp.tailgate && x <= sp.xRoofR ? null : MAT.cabin;    // solid prototype block
  if (x >= sp.xRoofF) return MAT.glass;                                   // windshield (+X = forwards)
  if (x <= sp.xRoofR) return sp.tailgate ? null : MAT.glass;              // rear glass
  return MAT.roof;                                                        // roof panel
}

export function buildGreenhouse(sp, gb) {
  if (sp.roofLen <= 0) return;
  const seg = 0.19 * clamp(sp.facets, 0.5, 2);
  const xs = [];
  const keys = [sp.xCowl, sp.xRoofF, sp.xRoofR, sp.xCabR];
  sp.bays.forEach(b => { keys.push(b.x0 + 0.045, b.x1 - 0.045, b.x0, b.x1); });
  const lo = Math.min(sp.xCowl, sp.xCabR), hi = Math.max(sp.xCowl, sp.xCabR);
  const uniq = [...new Set(keys.map(f2))].filter(x => x >= lo - 1e-6 && x <= hi + 1e-6).sort((a, b) => a - b);
  xs.push(uniq[0]);
  for (let i = 1; i < uniq.length; i++) {
    const a = xs[xs.length - 1], b = uniq[i], d = b - a;
    if (d < 0.03) continue;
    const n = Math.max(1, Math.round(d / seg));
    for (let k = 1; k <= n; k++) xs.push(f2(a + (d * k) / n));
  }
  const stations = xs;

  // loop = 9-point half mirrored -> 16 columns
  const SIDE_OUT = new Set([0, 1, 14, 15]);
  const ROOF_OUT = new Set([2, 3, 12, 13]);
  const ROOF_IN = new Set([4, 5, 10, 11]);
  const SIDE_IN = new Set([6, 7, 8, 9]);
  const keep = (k, x0, x1) => {
    const x = (x0 + x1) / 2;
    const top = ghTopMat(sp, x);
    const side = ghSideMat(sp, x);
    if (ROOF_OUT.has(k)) return top;                                     // windshield / roof / rear glass
    if (ROOF_IN.has(k)) return top === MAT.glass ? null : MAT.inner;     // headliner
    if (SIDE_OUT.has(k)) return side === 'open' ? null : side;           // pillars / seals
    if (SIDE_IN.has(k)) return side === 'open' ? null : MAT.inner;       // pillar trim
    return null;
  };
  loft(gb, sp, x => ghSection(sp, x), stations, keep, sp.toy
    ? { capFront: true, capRear: true, capFrontMat: MAT.cabin, capRearMat: MAT.cabin }
    : { capFront: false, capRear: false });

  // windshield/rear-glass reveals at the outer edge (thin trim)
  const reveal = (x, mat) => {
    const G = ghSection(sp, x);
    for (let k = 0; k <= 1; k++) {
      const a = V3(x, G[k][0], G[k][1]), b = V3(x, G[k + 1][0], G[k + 1][1]);
      const c = V3(x, G[8 - k - 1][0], G[8 - k - 1][1]), d = V3(x, G[8 - k][0], G[8 - k][1]);
      gb.quad(mat, a, b, V3(x, G[k + 1][0], G[k + 1][1] - 0.055), V3(x, G[k][0], G[k][1] - 0.055));
      gb.quad(mat, V3(x, G[8 - k - 1][0], G[8 - k - 1][1] + 0.055), V3(x, G[8 - k][0], G[8 - k][1] + 0.055), d, c);
    }
  };
  // roof rail / drip moulding over each opening --------------------
  if (!sp.toy) sp.bays.forEach(b => {
    const x0 = b.x0 + 0.02, x1 = b.x1 - 0.02;
    [1, -1].forEach(side => {
      let prev = null;
      const N = 6;
      for (let i = 0; i <= N; i++) {
        const x = lerp(x0, x1, i / N);
        const yr = sp.roofF(x), yb = sp.belt - 0.006;
        const y = yr - 0.030;
        const t = clamp((y - yb) / Math.max(0.01, yr - yb), 0, 1);
        const z = lerp(sp.hwGlassF(x), sp.hwRoofF(x), smoothstep(t)) + 0.006;
        const p = V3(x, y, side * z);
        if (prev) gb.strut(MAT.trim, prev, p, 0.026, 0.052);
        prev = p;
      }
    });
  });
  // roof crown crease + antenna/shark fin --------------------------
  if (!sp.toy) {
    const finX = sp.xRoofR + (sp.xCabR - sp.xRoofR) * 0.35;
    gb.strut(MAT.trim, V3(finX, sp.roofF(finX) - 0.01, 0), V3(finX - 0.11, sp.roofF(finX - 0.11) + 0.055, 0), 0.02, 0.05);
    reveal(sp.xCabR - 0.001, MAT.trim);
  }
}

/* ------------------------------------------------------------
   10 · hinged panels: bonnet, boot lid, tailgate, drop tailgate
   ------------------------------------------------------------ */
/** thin shell following the deck surface over [xa, xb] */
function deckPanel(sp, gb, xa, xb, lift, thick, mat, edgeMat) {
  const N = Math.max(3, Math.round((xb - xa) / (0.24 * clamp(sp.facets, 0.5, 2))));
  const rows = [];
  for (let i = 0; i <= N; i++) {
    const x = lerp(xa, xb, i / N);
    const y = sp.deckF(x) + lift;
    const hwL = sp.hwUpF(x) - 0.028;
    rows.push({
      x,
      top: [[y, 0], [y - 0.004, hwL * 0.42], [y - 0.012, hwL * 0.80], [y - 0.028, hwL]],
      bot: [[y - thick, hwL * 0.94]]
    });
  }
  // outer skin
  for (let i = 0; i < rows.length - 1; i++) {
    const A = rows[i], B = rows[i + 1];
    for (let k = 0; k < 3; k++) {
      const a = V3(A.x, A.top[k][0], A.top[k][1]), b = V3(A.x, A.top[k + 1][0], A.top[k + 1][1]);
      const c = V3(B.x, B.top[k + 1][0], B.top[k + 1][1]), d = V3(B.x, B.top[k][0], B.top[k][1]);
      gb.quad(mat, d, c, b, a);
      gb.quad(mat, V3(A.x, A.top[k][0], -A.top[k][1]), V3(A.x, A.top[k + 1][0], -A.top[k + 1][1]),
        V3(B.x, B.top[k + 1][0], -B.top[k + 1][1]), V3(B.x, B.top[k][0], -B.top[k][1]));
    }
    // skirt (outer down-turn)
    const sa = V3(A.x, A.top[3][0], A.top[3][1]), sb = V3(A.x, A.bot[0][0], A.bot[0][1]);
    const ta = V3(B.x, B.top[3][0], B.top[3][1]), tb = V3(B.x, B.bot[0][0], B.bot[0][1]);
    gb.quad(edgeMat, sa, sb, tb, ta);
    gb.quad(edgeMat, V3(A.x, A.top[3][0], -A.top[3][1]), V3(B.x, B.top[3][0], -B.top[3][1]),
      V3(B.x, B.bot[0][0], -B.bot[0][1]), V3(A.x, A.bot[0][0], -A.bot[0][1]));
    // underside
    gb.quad(edgeMat, sb, V3(A.x, A.bot[0][0], -A.bot[0][1]), V3(B.x, B.bot[0][0], -B.bot[0][1]), tb);
  }
  // end caps
  const cap = (r, flip) => {
    const pts = r.top.map(([y, z]) => V3(r.x, y, z)).concat(r.bot.map(([y, z]) => V3(r.x, y, z)));
    const pts2 = r.top.map(([y, z]) => V3(r.x, y, -z)).reverse().concat(r.bot.map(([y, z]) => V3(r.x, y, -z)).reverse());
    gb.poly(edgeMat, pts.concat([V3(r.x, r.top[0][0], 0)]), flip);
    gb.poly(edgeMat, pts2.concat([V3(r.x, r.top[0][0], 0)]), !flip);
  };
  cap(rows[0], true); cap(rows[rows.length - 1], false);
}

function buildBonnet(sp, mats) {
  const gb = new GeoBuf();
  const xa = sp.xHoodF, xb = sp.xCowl - 0.012;
  deckPanel(sp, gb, xa, xb, 0.012, 0.05, MAT.paint, MAT.inner);
  // shut-line groove + vents from the seed
  const r = rng(sp.seed + 'bonnet');
  if (r() < 0.55 && !sp.toy) {
    const vx = lerp(xa, xb, 0.30 + r() * 0.25);
    [-1, 1].forEach(s => {
      for (let i = 0; i < 3; i++) {
        const z = s * (0.20 + i * 0.075);
        gb.strut(MAT.trim, V3(vx, sp.deckF(vx) + 0.014, z), V3(vx - 0.17, sp.deckF(vx - 0.17) + 0.014, z), 0.035, 0.014);
      }
    });
  }
  const pivot = V3(xb, sp.deckF(xb) + 0.012, 0);
  const g = meshes(gb, mats);
  g.children.forEach(c => c.geometry.translate(-pivot.x, -pivot.y, -pivot.z));
  g.position.copy(pivot);
  return { group: g, pivot, axis: 'z', dir: 1, open: 0.92, label: 'Bonnet', id: 'bonnet' };
}

function buildBoot(sp, mats) {
  if (sp.tailgate || sp.bed) return null;
  const gb = new GeoBuf();
  const xa = sp.xTrunkR, xb = sp.xCabR + 0.012;
  deckPanel(sp, gb, xa, xb, 0.014, 0.055, MAT.paint, MAT.inner);
  const pivot = V3(xb, sp.deckF(xb) + 0.014, 0);
  const g = meshes(gb, mats);
  g.children.forEach(c => c.geometry.translate(-pivot.x, -pivot.y, -pivot.z));
  g.position.copy(pivot);
  return { group: g, pivot, axis: 'z', dir: -1, open: 0.85, label: 'Boot', id: 'boot' };
}

/** hatch tailgate: hinged at the roof rear edge, glass + painted frame */
function buildTailgate(sp, mats) {
  if (!sp.tailgate) return null;
  const gb = new GeoBuf();
  const x0 = sp.xCabR, x1 = sp.xRoofR + 0.02;
  const N = 6;
  const rows = [];
  for (let i = 0; i <= N; i++) {
    const x = lerp(x0, x1, i / N);
    const y = sp.roofF(x);
    const h = sp.hwGlassF(x);
    rows.push({ x, y, h });
  }
  for (let i = 0; i < rows.length - 1; i++) {
    const A = rows[i], B = rows[i + 1];
    // glass band (upper 78%) + painted lower band
    const glassY = a => a.y - (a.y - sp.belt) * 0.24;
    [1, -1].forEach(s => {
      const zo = s * A.h, zi = s * (A.h - 0.055);
      const zo2 = s * B.h, zi2 = s * (B.h - 0.055);
      // outer painted frame edge
      gb.quad(MAT.paint, V3(A.x, A.y, zo), V3(A.x, glassY(A), zo), V3(B.x, glassY(B), zo2), V3(B.x, B.y, zo2));
      // glass
      gb.quad(sp.toy ? MAT.cabin : MAT.glass, V3(A.x, glassY(A), zo * 0.995), V3(A.x, A.y - 0.03, zo * 0.995), V3(B.x, B.y - 0.03, zo2 * 0.995), V3(B.x, glassY(B), zo2 * 0.995));
      // inner trim
      gb.quad(MAT.inner, V3(A.x, glassY(A), zi), V3(A.x, A.y - 0.02, zi), V3(B.x, B.y - 0.02, zi2), V3(B.x, glassY(B), zi2));
    });
    // top surface strip
    gb.quad(MAT.paint, V3(A.x, A.y, A.h), V3(B.x, B.y, B.h), V3(B.x, B.y, -B.h), V3(A.x, A.y, -A.h));
  }
  // bottom edge + number plate recess
  const A = rows[0];
  gb.quad(MAT.paint, V3(A.x, A.y, A.h), V3(A.x, A.y, -A.h), V3(A.x - 0.06, A.y - 0.02, -A.h * 0.98), V3(A.x - 0.06, A.y - 0.02, A.h * 0.98));
  gb.box(MAT.trim, A.x - 0.012, (A.y + sp.belt) / 2, 0, 0.02, (A.y - sp.belt) * 0.5, A.h * 1.4);
  const pivot = V3(x1, sp.roofF(x1) - 0.03, 0);
  const g = meshes(gb, mats);
  g.children.forEach(c => c.geometry.translate(-pivot.x, -pivot.y, -pivot.z));
  g.position.copy(pivot);
  return { group: g, pivot, axis: 'z', dir: -1, open: 1.35, label: 'Tailgate', id: 'boot' };
}

/** ute: drop tailgate at the back of the bed */
function buildDropTail(sp, mats) {
  if (!sp.bed || !sp.bed.dropTail) return null;
  const gb = new GeoBuf();
  const x = sp.bed.x1, y0 = sp.bed.floor + 0.02, y1 = sp.bed.rail;
  const h = sp.hwUpF(x) - 0.01;
  gb.box(MAT.paint, x, (y0 + y1) / 2, 0, 0.055, y1 - y0, h * 2 - 0.02);
  gb.box(MAT.inner, x - 0.033, (y0 + y1) / 2, 0, 0.012, y1 - y0 - 0.05, h * 2 - 0.10);
  gb.strut(MAT.chrome, V3(x + 0.03, y1 - 0.09, h * 0.42), V3(x + 0.03, y1 - 0.09, -h * 0.42), 0.02, 0.02);
  const pivot = V3(x, y0, 0);
  const g = meshes(gb, mats);
  g.children.forEach(c => c.geometry.translate(-pivot.x, -pivot.y, -pivot.z));
  g.position.copy(pivot);
  return { group: g, pivot, axis: 'z', dir: 1, open: 1.55, label: 'Tailgate', id: 'boot' };
}

/* ------------------------------------------------------------
   11 · doors (metal shell + glass pocket + window + frame)
   ------------------------------------------------------------ */
function doorSection(sp, x, side) {
  const s = hullSection(sp, x);
  const out = s.hwU, inn = s.hwU - sp.doorT;
  const y0 = s.sillTop + 0.004;
  const y1 = s.yUp;
  const yw = lerp(y0, y1, 0.46);
  const D = [
    [y0, out], [yw, out + 0.004], [y1, out], [y1 + 0.016, out - 0.012],   // outer skin + belt lip
    [y1 + 0.006, inn + 0.028], [y1 - 0.03, inn], [yw - 0.02, inn], [y0 + 0.018, inn + 0.006],
    [y0, inn + 0.012]
  ];
  return D.map(([y, z]) => [y, side * z]);
}

function buildDoor(sp, bay, mats) {
  const side = bay.side;
  const gb = new GeoBuf();
  const x0 = bay.x0 + 0.006, x1 = bay.x1 - 0.006;
  const N = Math.max(4, Math.round((x1 - x0) / (0.2 * clamp(sp.facets, 0.5, 2))));
  const xs = Array.from({ length: N + 1 }, (_, i) => lerp(x0, x1, i / N));
  const secs = xs.map(x => doorSection(sp, x, side));

  const matFor = k => {
    if (k <= 2) return MAT.paint;           // outer skin
    if (k === 3) return MAT.trim;           // belt lip / glass channel
    if (k === 8) return MAT.inner;          // bottom edge
    return MAT.inner;                       // door card
  };
  for (let i = 0; i < secs.length - 1; i++) {
    const A = secs[i], B = secs[i + 1];
    for (let k = 0; k < A.length; k++) {
      const k2 = (k + 1) % A.length;
      const m = matFor(k);
      gb.quad(m, V3(A[k][0] * 0 + xs[i], A[k][0], A[k][1]), V3(xs[i], A[k2][0], A[k2][1]),
        V3(xs[i + 1], B[k2][0], B[k2][1]), V3(xs[i + 1], B[k][0], B[k][1]));
    }
  }
  gb.poly(MAT.inner, secs[0].map(p => V3(xs[0], p[0], p[1])), true);
  gb.poly(MAT.paint, secs[secs.length - 1].map(p => V3(xs[xs.length - 1], p[0], p[1])), false);

  // --- character crease + shut-line shadow strip --------------------
  if (!sp.toy) {
    const yC = lerp(sp.floor, sp.belt, 0.72);
    gb.strut(MAT.paint, V3(x0 + 0.02, yC, side * (sp.hwUpF(x0) + 0.008)), V3(x1 - 0.02, yC, side * (sp.hwUpF(x1) + 0.008)), 0.014, 0.012);

    // --- handle -----------------------------------------------------
    const hx = lerp(x1, x0, 0.20), hy = sp.belt - 0.085;
    gb.strut(MAT.chrome, V3(hx, hy, side * (sp.hwUpF(hx) + 0.012)), V3(hx - 0.155, hy, side * (sp.hwUpF(hx - 0.155) + 0.012)), 0.028, 0.026);
  } else if (sp.roundel) {
    // race roundel on the door slab
    const rx = lerp(x0, x1, 0.5), ry = lerp(sp.floor, sp.belt, 0.52);
    const g = new THREE.CylinderGeometry(0.16, 0.16, 0.012, 12);
    g.rotateX(Math.PI / 2);
    gb.geo(MAT.livery, g, rx, ry, side * (sp.hwUpF(rx) + 0.004));
    g.dispose();
  }

  // --- window glass (rolls down into the pocket) --------------------
  const gx0 = Math.max(bay.glassX0, x0 + 0.02), gx1 = Math.min(bay.glassX1, x1 - 0.02);
  const glassGB = new GeoBuf();
  const NG = Math.max(3, Math.round((gx1 - gx0) / 0.22));
  const gxs = Array.from({ length: NG + 1 }, (_, i) => lerp(gx0, gx1, i / NG));
  const top = x => sp.roofF(clamp(x, sp.xCabR, sp.xCowl)) - 0.012;
  for (let i = 0; i < gxs.length - 1; i++) {
    const xa = gxs[i], xb = gxs[i + 1];
    const inset = sp.toy ? 0.028 : 0.030;
    const za = side * (sp.hwGlassF(clamp(xa, sp.xCabR, sp.xCowl)) - inset);
    const zb = side * (sp.hwGlassF(clamp(xb, sp.xCabR, sp.xCowl)) - inset);
    const ya = sp.belt - 0.10, yb = sp.belt - 0.10;
    gb2quad(glassGB, sp.toy ? MAT.cabin : MAT.glass, V3(xa, ya, za), V3(xa, top(xa), za), V3(xb, top(xb), zb), V3(xb, yb, zb));
  }
  // glass edge (a bright polished edge reads well when half down)
  for (let i = 0; i < gxs.length - 1; i++) {
    const xa = gxs[i], xb = gxs[i + 1];
    const za = side * (sp.hwGlassF(clamp(xa, sp.xCabR, sp.xCowl)) - (sp.toy ? 0.028 : 0.030));
    const zb = side * (sp.hwGlassF(clamp(xb, sp.xCabR, sp.xCowl)) - (sp.toy ? 0.028 : 0.030));
    gb2quad(glassGB, sp.toy ? MAT.trim : MAT.chrome, V3(xa, top(xa), za), V3(xa, top(xa) + 0.006, za), V3(xb, top(xb) + 0.006, zb), V3(xb, top(xb), zb));
  }
  const glass = meshes(glassGB, mats, false);
  glass.renderOrder = sp.toy ? 1 : 3;

  // --- framed door: window surround ---------------------------------
  if (sp.framedDoors && !sp.toy) {
    const fr = new GeoBuf();
    const zf = side * (sp.hwGlassF(clamp(gx0, sp.xCabR, sp.xCowl)) - 0.012);
    const zr = side * (sp.hwGlassF(clamp(gx1, sp.xCabR, sp.xCowl)) - 0.012);
    const yb = sp.belt + 0.02;
    fr.strut(MAT.paint, V3(gx0, yb, zf), V3(gx0, top(gx0) + 0.02, zf), 0.052, 0.034);
    fr.strut(MAT.paint, V3(gx1, yb, zr), V3(gx1, top(gx1) + 0.02, zr), 0.052, 0.034);
    const N2 = 5;
    let prev = null;
    for (let i = 0; i <= N2; i++) {
      const x = lerp(gx0, gx1, i / N2);
      const z = side * (sp.hwGlassF(clamp(x, sp.xCabR, sp.xCowl)) - 0.012);
      const p = V3(x, top(x) + 0.02, z);
      if (prev) fr.strut(MAT.paint, prev, p, 0.05, 0.034);
      prev = p;
    }
    gb.add(fr);
  }

  // --- mirror (rides on the door) ------------------------------------
  if (sp.toy) {
    const pivot0 = V3(x1, 0, side * (sp.hwUpF(x1) + 0.004));
    const g0 = meshes(gb, mats);
    g0.children.forEach(c => c.geometry.translate(-pivot0.x, -pivot0.y, -pivot0.z));
    g0.position.copy(pivot0);
    glass.children.forEach(c => c.geometry.translate(-pivot0.x, -pivot0.y, -pivot0.z));
    glass.position.copy(pivot0);
    return {
      group: g0, pivot: pivot0, axis: 'y', dir: side > 0 ? 1 : -1, open: 1.14,
      label: (bay.side > 0 ? 'Door · left' : 'Door · right') + (bay.front ? ' front' : ' rear'),
      id: bay.id, glass, glassTravel: (sp.belt - 0.10) - (sp.floor + 0.06), bay
    };
  }
  const mir = new GeoBuf();
  const mx = x1 - 0.10, mz = side * (sp.hwUpF(mx) + 0.02), my = sp.belt + 0.045;
  mir.strut(MAT.trim, V3(mx, my - 0.03, side * (sp.hwUpF(mx))), V3(mx, my, mz + side * 0.06), 0.028, 0.026);
  if (sp.mirrors === 'cap') {
    mir.box(MAT.paint, mx - 0.03, my + 0.005, mz + side * 0.10, 0.075, 0.055, 0.085);
    mir.box(MAT.chrome, mx - 0.03, my + 0.005, mz + side * 0.145, 0.06, 0.042, 0.012);
  } else if (sp.mirrors === 'flag') {
    mir.box(MAT.trim, mx - 0.02, my + 0.02, mz + side * 0.10, 0.03, 0.10, 0.075);
    mir.box(MAT.chrome, mx - 0.02, my + 0.02, mz + side * 0.14, 0.022, 0.082, 0.012);
  } else {
    mir.box(MAT.paint, mx - 0.05, my + 0.01, mz + side * 0.105, 0.10, 0.05, 0.075);
    mir.box(MAT.chrome, mx - 0.05, my + 0.01, mz + side * 0.145, 0.085, 0.038, 0.012);
  }
  gb.add(mir);

  // --- assemble on a hinge at the leading edge ----------------------
  const pivot = V3(x1, 0, side * (sp.hwUpF(x1) + 0.004));
  const g = meshes(gb, mats);
  g.children.forEach(c => c.geometry.translate(-pivot.x, -pivot.y, -pivot.z));
  g.position.copy(pivot);
  glass.children.forEach(c => c.geometry.translate(-pivot.x, -pivot.y, -pivot.z));
  glass.position.copy(pivot);

  const label = (bay.side > 0 ? 'Door · left' : 'Door · right') + (bay.front ? ' front' : ' rear');
  return {
    group: g, pivot, axis: 'y', dir: side > 0 ? 1 : -1, open: 1.14, label,
    id: bay.id, glass, glassTravel: (sp.belt - 0.10) - (sp.floor + 0.06), bay
  };
}
function gb2quad(gb, m, a, b, c, d) { gb.quad(m, a, b, c, d); }

/* ------------------------------------------------------------
   12 · interior, bays, glass house-keeping
   ------------------------------------------------------------ */
function buildInterior(sp, gb) {
  if (sp.toy) {                                     // prototype: clean empty wells
    const bx0 = sp.xHoodF - 0.06, bx1 = sp.xCowl - 0.06;
    const by = sp.deckF((bx0 + bx1) / 2) - sp.bayDepth;
    gb.box(MAT.bay, (bx0 + bx1) / 2, by - 0.02, 0, (bx1 - bx0) * 0.94, 0.04, sp.hwUpF((bx0 + bx1) / 2) * 1.5);
    if (!sp.tailgate && !sp.bed) {
      const tx = (sp.xTrunkR + sp.xCabR) / 2, ty = sp.deckF(tx) - sp.bayDepth;
      gb.box(MAT.bay, tx, ty - 0.02, 0, (sp.xCabR - sp.xTrunkR) * 0.9, 0.04, sp.hwUpF(tx) * 1.5);
    }
    return;
  }
  const zS = sp.track * 0.52;                       // seat centres
  const yF = sp.floor;
  const dashX = sp.xCowl - 0.10;
  const seatX = dashX - 0.46;
  const dir = sp.rhd ? -1 : 1;

  // dashboard -------------------------------------------------------
  gb.box(MAT.inner, dashX - 0.06, yF + 0.20, 0, 0.26, 0.30, sp.hwInner * 1.7);
  gb.box(MAT.trim, dashX - 0.16, yF + 0.36, 0, 0.10, 0.06, sp.hwInner * 1.6);
  // binnacle + wheel
  gb.box(MAT.trim, dashX - 0.10, yF + 0.34, dir * zS, 0.14, 0.10, 0.30);
  const wx = dashX - 0.24, wy = yF + 0.36;
  const ring = new THREE.TorusGeometry(0.165, 0.026, 4, 9);
  ring.rotateY(Math.PI / 2); ring.rotateZ(0.28 * dir);
  gb.geo(MAT.trim, ring, wx, wy, dir * zS);
  ring.dispose();
  gb.strut(MAT.trim, V3(wx, wy, dir * zS), V3(wx + 0.03, wy - 0.15, dir * zS), 0.03, 0.03);
  gb.strut(MAT.trim, V3(wx, wy, dir * zS), V3(wx + 0.03, wy + 0.02, dir * zS + 0.14), 0.028, 0.028);
  gb.strut(MAT.trim, V3(wx, wy, dir * zS), V3(wx + 0.03, wy + 0.02, dir * zS - 0.14), 0.028, 0.028);
  // centre console
  gb.box(MAT.inner, seatX + 0.22, yF + 0.14, 0, 0.62, 0.20, 0.17);
  gb.box(MAT.accent, seatX + 0.30, yF + 0.245, 0, 0.10, 0.012, 0.10);

  // seats ------------------------------------------------------------
  const seat = (x, z, recline) => {
    gb.box(MAT.seat, x, yF + 0.16, z, 0.50, 0.14, 0.44);
    gb.box(MAT.seat, x - 0.20 * Math.cos(recline), yF + 0.44, z, 0.14, 0.52, 0.42);
    gb.box(MAT.seat, x - 0.30, yF + 0.72, z, 0.11, 0.17, 0.26);
    gb.strut(MAT.accent, V3(x - 0.13, yF + 0.24, z - 0.20), V3(x - 0.20, yF + 0.62, z - 0.19), 0.035, 0.012);
  };
  seat(seatX, dir * zS * -1 * -1, 0.22);
  seat(seatX, -dir * zS, 0.22);
  // rear bench
  const rearX = seatX - 0.62;
  if (!sp.bed && rearX > sp.xCabR + 0.25) {
    gb.box(MAT.seat, rearX, yF + 0.17, 0, 0.48, 0.15, sp.hwInner * 1.5);
    gb.box(MAT.seat, rearX - 0.17, yF + 0.45, 0, 0.14, 0.48, sp.hwInner * 1.5);
    [-1, 0, 1].forEach(k => gb.box(MAT.seat, rearX - 0.24, yF + 0.72, k * 0.30, 0.10, 0.16, 0.24));
  }
  // footwells / pedal box
  gb.box(MAT.trim, dashX - 0.30, yF + 0.03, dir * zS, 0.26, 0.05, 0.34);
  gb.box(MAT.chrome, dashX - 0.34, yF + 0.10, dir * zS - 0.04, 0.02, 0.09, 0.07);
  gb.box(MAT.chrome, dashX - 0.32, yF + 0.09, dir * zS + 0.07, 0.02, 0.07, 0.06);

  // engine bay -------------------------------------------------------
  const bx0 = sp.xHoodF - 0.06, bx1 = sp.xCowl - 0.06;
  const by = sp.deckF((bx0 + bx1) / 2) - sp.bayDepth;
  const bm = (bx0 + bx1) / 2;
  const D = sp.bayDepth;                                            // usable well depth
  gb.box(MAT.bay, bm, by - 0.03, 0, (bx1 - bx0) * 0.94, 0.05, sp.hwUpF(bm) * 1.5);   // bay floor
  gb.box(MAT.trim, bm - 0.05, by + D * 0.34, 0, (bx1 - bx0) * 0.72, D * 0.52, 0.52); // block
  gb.box(MAT.bay, bm - 0.05, by + D * 0.66, 0, (bx1 - bx0) * 0.58, D * 0.12, 0.44);  // valve cover
  [-1, 1].forEach(s => gb.box(MAT.chrome, bm - 0.05, by + D * 0.74, s * 0.13, (bx1 - bx0) * 0.4, 0.04, 0.05));
  gb.box(MAT.accent, bm - 0.05, by + D * 0.72, 0, 0.10, 0.03, 0.16);
  [-1, 1].forEach(s => {                                                                // strut towers
    const cx = bx1 - 0.12, cz = s * (sp.hwUpF(cx) - 0.16);
    const g = new THREE.CylinderGeometry(0.075, 0.10, D * 0.55, 6);
    gb.geo(MAT.bay, g, cx, by + D * 0.30, cz);
    g.dispose();
  });
  gb.strut(MAT.chrome, V3(bx1 - 0.12, by + D * 0.62, -(sp.hwUpF(bx1) - 0.16)), V3(bx1 - 0.12, by + D * 0.62, (sp.hwUpF(bx1) - 0.16)), 0.05, 0.035);
  gb.box(MAT.trim, bx0 + 0.08, by + D * 0.36, 0, 0.09, D * 0.6, sp.hwUpF(bx0) * 1.4);   // radiator
  gb.box(MAT.bay, bx1 - 0.06, by + D * 0.24, -(sp.hwUpF(bx1) - 0.30), 0.22, D * 0.4, 0.16);  // battery
  gb.box(MAT.accent, bx1 - 0.06, by + D * 0.46, -(sp.hwUpF(bx1) - 0.30), 0.06, 0.02, 0.06);

  // luggage / spare in the boot well --------------------------------
  if (!sp.tailgate && !sp.bed) {
    const tx = (sp.xTrunkR + sp.xCabR) / 2, ty = sp.deckF(tx) - sp.bayDepth, TD = sp.bayDepth;
    gb.box(MAT.bay, tx, ty - 0.02, 0, (sp.xCabR - sp.xTrunkR) * 0.9, 0.04, sp.hwUpF(tx) * 1.5);
    if (sp.spare) {
      const rr = Math.min(0.26, TD * 0.42);
      const g = new THREE.CylinderGeometry(rr, rr, 0.075, 11);
      g.rotateX(Math.PI / 2);
      gb.geo(MAT.tyre, g, tx, ty + rr * 0.55, 0);
      g.dispose();
      gb.box(MAT.rim, tx, ty + rr * 0.55, 0, 0.09, rr * 1.1, rr * 1.1);
    } else {
      gb.box(MAT.inner, tx, ty + TD * 0.34, 0, (sp.xCabR - sp.xTrunkR) * 0.62, TD * 0.6, 0.62);
      gb.strut(MAT.accent, V3(tx - 0.2, ty + TD * 0.66, 0), V3(tx + 0.2, ty + TD * 0.66, 0), 0.04, 0.02);
    }
  }
  // ute bed floor + wheel arches in the bed
  if (sp.bed) {
    const bx = (sp.bed.x0 + sp.bed.x1) / 2;
    gb.box(MAT.bay, bx, sp.bed.floor - 0.02, 0, (sp.bed.x1 - sp.bed.x0) * 0.98, 0.05, sp.hwUpF(bx) * 1.6);
    [-1, 1].forEach(s => {
      const ax = sp.xRA;
      gb.box(MAT.liner, ax, sp.bed.floor + 0.16, s * (sp.hwUpF(ax) - 0.10), 0.72, 0.32, 0.10);
    });
    [0.28, 0.62].forEach(t => gb.strut(MAT.trim, V3(lerp(sp.bed.x0, sp.bed.x1, t), sp.bed.floor + 0.01, -sp.hwUpF(bx) + 0.09),
      V3(lerp(sp.bed.x0, sp.bed.x1, t), sp.bed.floor + 0.01, sp.hwUpF(bx) - 0.09), 0.05, 0.02));
  }
}

/* ------------------------------------------------------------
   13 · wheels, brakes, lamps, trim, livery
   ------------------------------------------------------------ */
function lathe(gb, mat, profile, segs, cx, cy, cz, axisZ = true) {
  const g = new THREE.LatheGeometry(profile.map(([r, y]) => new THREE.Vector2(Math.max(1e-4, r), y)), segs);
  if (axisZ) g.rotateX(Math.PI / 2);
  const p = g.attributes.position.array;
  const idx = g.index ? g.index.array : null;
  const push = (a, b, c) => gb.tri(mat, V3(p[a * 3] + cx, p[a * 3 + 1] + cy, p[a * 3 + 2] + cz),
    V3(p[b * 3] + cx, p[b * 3 + 1] + cy, p[b * 3 + 2] + cz), V3(p[c * 3] + cx, p[c * 3 + 1] + cy, p[c * 3 + 2] + cz));
  if (idx) { for (let i = 0; i < idx.length; i += 3) push(idx[i], idx[i + 1], idx[i + 2]); }
  else { for (let i = 0; i < p.length / 3; i += 3) push(i, i + 1, i + 2); }
  g.dispose();
}

function buildToyWheel(sp, gb) {
  const R = sp.wheelR, W = sp.tyreW;
  const segs = 18;
  lathe(gb, MAT.tyre, [
    [R * 0.55, -W / 2], [R * 0.86, -W / 2], [R * 0.97, -W / 2 + 0.05], [R, -W * 0.18],
    [R, W * 0.18], [R * 0.97, W / 2 - 0.05], [R * 0.86, W / 2], [R * 0.55, W / 2]
  ], segs, 0, 0, 0);
  if (sp.whitewall) {
    lathe(gb, MAT.cabin, [[R * 0.60, W / 2 + 0.002], [R * 0.84, W / 2 + 0.002], [R * 0.84, W / 2 - 0.05], [R * 0.60, W / 2 - 0.05]], segs, 0, 0, 0);
  }
  // dished face + chrome dome hub
  lathe(gb, MAT.tyre, [[R * 0.55, W * 0.30], [R * 0.34, W * 0.34], [R * 0.16, W * 0.36]], segs, 0, 0, 0);
  lathe(gb, MAT.chrome, [[0.001, W * 0.42], [R * 0.10, W * 0.40], [R * 0.14, W * 0.34], [0.001, W * 0.30]], segs, 0, 0, 0);
}

function buildWheel(sp, gb, rim) {
  if (sp.toy) return buildToyWheel(sp, gb);
  const R = sp.wheelR, W = sp.tyreW, rimR = R * 0.60;
  const segs = Math.round(clamp(11 * (sp.facets > 1.15 ? 1.25 : 1), 8, 18));
  // tyre -------------------------------------------------------------
  lathe(gb, MAT.tyre, [
    [rimR * 0.92, -W / 2], [R * 0.80, -W / 2], [R * 0.94, -W / 2 + 0.022], [R * 0.995, -W / 2 + 0.055],
    [R, -W * 0.16], [R, W * 0.16], [R * 0.995, W / 2 - 0.055], [R * 0.94, W / 2 - 0.022],
    [R * 0.80, W / 2], [rimR * 0.92, W / 2]
  ], segs, 0, 0, 0);
  // tread blocks (facets read as an off-road/low-poly tread)
  if (sp.style === 'ute' || sp.style === 'gt') {
    for (let i = 0; i < segs; i++) {
      const a = (i / segs) * Math.PI * 2;
      const c = Math.cos(a), s = Math.sin(a);
      gb.box(MAT.tyre, c * (R + 0.004), s * (R + 0.004), 0, 0.055, 0.055, W * (i % 2 ? 0.5 : 0.8));
    }
  }
  // rim barrel --------------------------------------------------------
  const dish = rim.style === 'dish' ? 0.055 : 0.0;
  lathe(gb, MAT.rim, [
    [rimR * 0.34, -W * 0.30 + dish], [rimR * 0.62, -W * 0.34 + dish], [rimR * 0.86, -W * 0.40],
    [rimR, -W * 0.42], [rimR * 1.005, W * 0.34], [rimR * 0.90, W * 0.40], [rimR * 0.60, W * 0.42],
    [rimR * 0.40, W * 0.36 - dish * 0.4]
  ], segs, 0, 0, 0);
  // spokes -------------------------------------------------------------
  const n = rim.style === 'split' ? rim.spokes * 2 : rim.spokes;
  const zo = W * 0.36 - dish * 0.5, zi = -W * 0.20 + dish;
  for (let i = 0; i < n; i++) {
    const a = (i / n) * Math.PI * 2 + (rim.style === 'turbine' ? 0.35 : 0);
    const c = Math.cos(a), s = Math.sin(a);
    if (rim.style === 'mesh') {
      const a2 = a + Math.PI / n;
      gb.strut(MAT.rim, V3(c * rimR * 0.34, s * rimR * 0.34, zi), V3(Math.cos(a2) * rimR * 0.92, Math.sin(a2) * rimR * 0.92, zo), 0.032, 0.026);
      gb.strut(MAT.rim, V3(Math.cos(a2) * rimR * 0.34, Math.sin(a2) * rimR * 0.34, zi), V3(c * rimR * 0.92, s * rimR * 0.92, zo), 0.032, 0.026);
    } else if (rim.style === 'turbine') {
      gb.strut(MAT.rim, V3(c * rimR * 0.30, s * rimR * 0.30, zi), V3(Math.cos(a + 0.5) * rimR * 0.95, Math.sin(a + 0.5) * rimR * 0.95, zo), 0.055, 0.022);
    } else {
      const w = rim.style === 'split' ? 0.022 : 0.055;
      gb.strut(MAT.rim, V3(c * rimR * 0.32, s * rimR * 0.32, zi), V3(c * rimR * 0.94, s * rimR * 0.94, zo), w, rim.style === 'dish' ? 0.035 : 0.028);
    }
  }
  // hub + lugs
  lathe(gb, MAT.rim, [[0.001, zi - 0.02], [rimR * 0.34, zi - 0.015], [rimR * 0.30, zo * 0.4], [0.001, zo * 0.45]], segs, 0, 0, 0);
  for (let i = 0; i < 5; i++) {
    const a = (i / 5) * Math.PI * 2;
    gb.box(MAT.chrome, Math.cos(a) * 0.055, Math.sin(a) * 0.055, zo * 0.5, 0.026, 0.026, 0.02);
  }
  // brakes
  lathe(gb, MAT.brake, [[rimR * 0.30, -W * 0.16], [R * 0.60, -W * 0.16], [R * 0.60, -W * 0.10], [rimR * 0.30, -W * 0.10]], 10, 0, 0, 0);
  gb.box(MAT.caliper, 0, R * 0.50, -W * 0.16, 0.13, 0.17, 0.055);
}

function buildWheels(sp, mats, root) {
  const wheels = [];
  const pos = [
    { x: sp.xFA, z: sp.track, front: true, id: 'wfl' }, { x: sp.xFA, z: -sp.track, front: true, id: 'wfr' },
    { x: sp.xRA, z: sp.track, front: false, id: 'wrl' }, { x: sp.xRA, z: -sp.track, front: false, id: 'wrr' }
  ];
  pos.forEach(p => {
    const steer = new THREE.Group();
    steer.position.set(p.x, sp.wheelR, p.z);
    const gb = new GeoBuf();
    buildWheel(sp, gb, sp.rim);
    const g = meshes(gb, mats);
    g.rotation.y = p.z > 0 ? 0 : Math.PI;
    steer.add(g);
    steer.userData.wheel = p;
    root.add(steer);
    wheels.push(steer);
  });
  return wheels;
}

function buildLamps(sp, gb) {
  const hf = sp.hwUpF(sp.xF - 0.02), nf = sp.hwUpF(sp.xHoodF);
  const yNose = sp.deckF(sp.xF);
  if (sp.toy) {
    // flat dark nose band + red tail wedges, exactly like a clay prototype
    gb.box(MAT.trim, sp.xF - 0.02, yNose * 0.62, 0, 0.10, yNose * 0.34, hf * 1.72);
    const hR = sp.hwUpF(sp.xR + 0.02), yT = sp.deckF(sp.xR);
    [-1, 1].forEach(s => gb.box(MAT.tail, sp.xR + 0.015, yT * 0.72, s * hR * 0.86, 0.07, yT * 0.30, 0.10));
    return;
  }
  // headlamps ------------------------------------------------------------
  [-1, 1].forEach(s => {
    const z = s * (hf * (sp.lampStyle === 'bar' ? 0.55 : 0.62));
    const y = yNose * (sp.lampStyle === 'slit' ? 0.90 : 0.82);
    if (sp.lampStyle === 'bar') {
      gb.box(MAT.headGlass, sp.xF - 0.015, y, z, 0.11, 0.085, hf * 0.62);
      gb.box(MAT.head, sp.xF - 0.06, y, z, 0.03, 0.05, hf * 0.55);
      gb.strut(MAT.accent, V3(sp.xF + 0.015, y - 0.055, z - s * hf * 0.30), V3(sp.xF + 0.015, y - 0.055, z + s * hf * 0.30), 0.018, 0.018);
    } else if (sp.lampStyle === 'slit') {
      gb.box(MAT.headGlass, sp.xF - 0.01, y, z, 0.10, 0.045, hf * 0.42);
      gb.box(MAT.head, sp.xF - 0.05, y, z, 0.025, 0.028, hf * 0.36);
    } else {
      gb.box(MAT.headGlass, sp.xF - 0.02, y, z, 0.12, 0.135, 0.30);
      gb.box(MAT.head, sp.xF - 0.06, y + 0.01, z, 0.03, 0.075, 0.20);
      gb.box(MAT.chrome, sp.xF - 0.06, y - 0.045, z, 0.03, 0.025, 0.22);
    }
  });
  if (sp.lampStyle === 'bar') {  // full-width light bar
    gb.box(MAT.headGlass, sp.xF - 0.01, yNose * 0.86, 0, 0.09, 0.035, hf * 0.9);
    gb.strut(MAT.accent, V3(sp.xF + 0.02, yNose * 0.86, -hf * 0.4), V3(sp.xF + 0.02, yNose * 0.86, hf * 0.4), 0.014, 0.014);
  }
  // grille + intakes ------------------------------------------------------
  gb.box(MAT.trim, sp.xF - 0.03, yNose * 0.52, 0, 0.06, yNose * 0.34, hf * (sp.lampStyle === 'pod' ? 0.72 : 1.1));
  for (let i = 0; i < 4; i++) {
    const y = yNose * (0.40 + i * 0.075);
    gb.strut(MAT.chrome, V3(sp.xF - 0.005, y, -hf * 0.5), V3(sp.xF - 0.005, y, hf * 0.5), 0.012, 0.012);
  }
  gb.box(MAT.liner, sp.xF - 0.02, sp.ground + 0.10, 0, 0.05, 0.09, hf * 1.45);
  [-1, 1].forEach(s => gb.box(MAT.liner, sp.xF - 0.03, sp.ground + 0.13, s * hf * 0.72, 0.06, 0.10, 0.20));
  // badge
  if (sp.badges) gb.box(MAT.accent, sp.xF - 0.004, yNose * 0.72, 0, 0.012, 0.075, 0.075);
  // splitter
  if (sp.splitter) {
    gb.box(MAT.trim, sp.xF - 0.10, sp.ground + 0.028, 0, 0.30, 0.022, hf * 2.02);
    [-1, 1].forEach(s => gb.box(MAT.trim, sp.xF - 0.16, sp.ground + 0.05, s * hf * 1.0, 0.20, 0.075, 0.03));
  }
  // tow hook
  if (sp.towHook) gb.box(MAT.accent, sp.xF - 0.012, sp.ground + 0.16, sp.hwUpF(sp.xF - 0.2) * 0.62, 0.03, 0.03, 0.06);

  // tail lamps -------------------------------------------------------------
  const yT = sp.deckF(sp.xR), hR = sp.hwUpF(sp.xR + 0.02);
  const barTail = sp.lampStyle !== 'pod' || sp.tailgate;
  [-1, 1].forEach(s => {
    const z = s * hR * 0.60;
    gb.box(MAT.tail, sp.xR + 0.012, yT * 0.86, z, 0.06, barTail ? 0.075 : 0.15, hR * (barTail ? 0.62 : 0.42));
    gb.box(MAT.trim, sp.xR + 0.04, yT * 0.86, z, 0.05, barTail ? 0.10 : 0.19, hR * (barTail ? 0.68 : 0.48));
  });
  if (barTail) gb.strut(MAT.tail, V3(sp.xR + 0.005, yT * 0.87, -hR * 0.5), V3(sp.xR + 0.005, yT * 0.87, hR * 0.5), 0.028, 0.02);
  // rear bumper + diffuser + exhaust
  gb.box(MAT.trim, sp.xR + 0.03, sp.ground + 0.12, 0, 0.07, 0.16, hR * 1.9);
  gb.box(MAT.liner, sp.xR + 0.06, sp.ground + 0.045, 0, 0.13, 0.06, hR * 1.5);
  const ex = sp.exhaust;
  const tips = ex === 'single' ? [0.4] : ex === 'twin' ? [0.42, -0.42] : ex === 'centre' ? [0.12, -0.12] : [0.62, 0.34, -0.34, -0.62];
  tips.forEach(k => {
    const g = new THREE.CylinderGeometry(0.042, 0.046, 0.09, 7);
    g.rotateZ(Math.PI / 2);
    gb.geo(MAT.chrome, g, sp.xR + 0.03, sp.ground + 0.14, k * hR);
    g.dispose();
  });
  // number plate
  gb.box(MAT.trim, sp.xR + 0.012, sp.ground + 0.30, 0, 0.014, 0.10, 0.34);
  gb.box(MAT.chrome, sp.xR + 0.006, sp.ground + 0.30, 0, 0.006, 0.075, 0.30);

  // wipers
  if (sp.wipers) {
    [-1, 1].forEach((s, i) => {
      const z = s * sp.hwGlassF(sp.xCowl) * (i ? 0.26 : 0.66);
      const x0 = sp.xCowl - 0.01, x1 = sp.xCowl - 0.34;
      gb.strut(MAT.trim, V3(x0, sp.roofF(x0) + 0.018, z), V3(x1, sp.roofF(x1) + 0.018, z - s * 0.06), 0.016, 0.012);
    });
  }
  // roof: rails / shark fin / wing
  if (sp.roofRails) [-1, 1].forEach(s => {
    let prev = null;
    for (let i = 0; i <= 4; i++) {
      const x = lerp(sp.xRoofF + 0.06, sp.xRoofR - 0.06, i / 4);
      const p = V3(x, sp.roofF(x) + 0.028, s * (sp.hwRoofF(x) * 0.78));
      if (prev) gb.strut(MAT.trim, prev, p, 0.035, 0.03);
      prev = p;
    }
  });
  if (sp.lightbar) {
    const x = sp.xRoofF + 0.06;
    gb.box(MAT.trim, x, sp.roofF(x) + 0.075, 0, 0.09, 0.075, sp.hwRoofF(x) * 1.3);
    gb.box(MAT.head, x + 0.045, sp.roofF(x) + 0.075, 0, 0.012, 0.05, sp.hwRoofF(x) * 1.24);
  }
  if (sp.wing) {
    const x = sp.xRoofR + (sp.xCabR - sp.xRoofR) * (sp.tailgate ? 0.12 : 0.25);
    const y = sp.roofF(x);
    if (sp.tailgate) {
      gb.box(MAT.paint, x - 0.02, y + 0.035, 0, 0.20, 0.028, sp.hwRoofF(x) * 1.7);
      [-1, 1].forEach(s => gb.strut(MAT.trim, V3(x - 0.02, y, s * sp.hwRoofF(x) * 0.7), V3(x - 0.02, y + 0.03, s * sp.hwRoofF(x) * 0.7), 0.05, 0.03));
    } else {
      const wx = sp.xR + 0.22, wy = sp.deckF(wx) + 0.30;
      gb.box(MAT.paint, wx, wy, 0, 0.30, 0.026, sp.hwUpF(wx) * 1.86);
      gb.box(MAT.trim, wx - 0.13, wy + 0.05, 0, 0.03, 0.10, sp.hwUpF(wx) * 1.8);
      [-1, 1].forEach(s => {
        gb.strut(MAT.trim, V3(wx + 0.02, sp.deckF(wx) + 0.02, s * sp.hwUpF(wx) * 0.72), V3(wx + 0.02, wy, s * sp.hwUpF(wx) * 0.72), 0.045, 0.03);
        gb.strut(MAT.trim, V3(wx - 0.10, sp.deckF(wx) + 0.02, s * sp.hwUpF(wx) * 0.72), V3(wx - 0.10, wy, s * sp.hwUpF(wx) * 0.72), 0.045, 0.03);
      });
    }
  }
}

/** painted racing stripes that follow the deck/roof surfaces */
function buildLivery(sp, gb) {
  if (!sp.livery.on) return;
  const w = sp.livery.width, gap = sp.livery.gap;
  const stripe = (xa, xb, yAt, hwAt, lift) => {
    const N = Math.max(4, Math.round((xb - xa) / 0.28));
    [-1, 1].forEach(s => {
      let prev = null;
      for (let i = 0; i <= N; i++) {
        const x = lerp(xa, xb, i / N);
        const y = yAt(x) + lift;
        const p = { a: V3(x, y, s * gap), b: V3(x, y, s * (gap + w)) };
        if (prev) gb.quad(MAT.livery, prev.a, prev.b, p.b, p.a);
        prev = p;
      }
    });
  };
  // bonnet + trunk / tail deck
  if (sp.bed) stripe(sp.xR + 0.08, sp.xCowl - 0.03, sp.deckF, 0, 0.008);
  else {
    stripe(sp.xHoodF + 0.03, sp.xCowl - 0.03, sp.deckF, 0, 0.016);
    stripe(sp.xTrunkR + 0.02, sp.xCabR - 0.02, sp.deckF, 0, 0.018);
  }
  // roof
  if (!sp.bed) stripe(sp.xRoofF + 0.04, sp.xRoofR - 0.04, sp.roofF, 0, 0.010);
}

/* ------------------------------------------------------------
   14 · top level
   ------------------------------------------------------------ */
export function buildCar(specIn) {
  const spec = specIn.spec ? specIn.spec : specIn;
  const sp = layout(spec);
  const mats = makeMaterials(spec);
  const root = new THREE.Group();
  root.name = 'car';
  const parts = [];
  let tris = 0;

  const addGB = (gb, parent = root, shadow = true) => {
    const g = meshes(gb, mats, shadow);
    g.traverse(o => { if (o.isMesh) tris += o.geometry.attributes.position.count / 3; });
    parent.add(g);
    return g;
  };

  // shell -------------------------------------------------------------
  const shellGB = new GeoBuf();
  buildHull(sp, shellGB);
  buildGreenhouse(sp, shellGB);
  buildInterior(sp, shellGB);
  buildLamps(sp, shellGB);
  buildLivery(sp, shellGB);
  const shell = addGB(shellGB);
  shell.name = 'shell';
  shell.traverse(o => { if (o.isMesh) o.userData.part = 'body'; });

  // articulated panels -------------------------------------------------
  const mk = p => {
    if (!p) return null;
    p.group.traverse(o => { if (o.isMesh) { o.userData.part = p.id; o.castShadow = true; } });
    p.t = 0; p.from = 0; p.to = 0; p.start = -1;
    root.add(p.group);
    parts.push(p);
    return p;
  };
  mk(buildBonnet(sp, mats));
  const boot = mk(sp.tailgate ? buildTailgate(sp, mats) : (sp.bed ? buildDropTail(sp, mats) : buildBoot(sp, mats)));
  if (boot) boot.label = sp.bed ? 'Tailgate' : (sp.tailgate ? 'Tailgate' : 'Boot');
  const doors = sp.bays.map(b => mk(buildDoor(sp, b, mats))).filter(Boolean);

  // windows: a door's glass is a child of the door group ----------------
  root.traverse(o => { if (o.isMesh && (o.material === mats.glass || o.material === mats.headGlass)) o.renderOrder = 4; });
  doors.forEach(d => {
    d.glass.children.forEach(o => { o.userData.part = d.id + ':glass'; o.renderOrder = 5; });
    d.glass.position.set(0, 0, 0);        // geometry is already pivot-relative
    d.group.add(d.glass);
    d.drop = 0; d.dropFrom = 0; d.dropTo = 0; d.dropStart = -1;
  });

  // wheels -------------------------------------------------------------
  const wheels = buildWheels(sp, mats, root);
  wheels.forEach(w => w.traverse(o => { if (o.isMesh) o.userData.part = 'wheel'; }));

  root.userData = { sp, spec, mats };
  const bbox = new THREE.Box3().setFromObject(root);
  const size = bbox.getSize(new THREE.Vector3());

  const dispose = () => {
    root.traverse(o => { if (o.isMesh) { o.geometry.dispose(); } });
    Object.values(mats).forEach(m => m.dispose && m.dispose());
  };

  return {
    root, parts, doors, wheels, mats, sp, spec,
    stats: { tris: Math.round(tris), meshes: root.children.length, L: size.x, W: size.z, H: size.y },
    setPaint(hexBody, hexRoof) {
      mats.paint.color.set(hexBody);
      mats.roof.color.set(hexRoof || hexBody);
    },
    dispose
  };
}

/* ------------------------------------------------------------
   15 · articulation helpers (shared with the UI)
   ------------------------------------------------------------ */
export const easeInOut = t => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2);

/** advance every hinge/window tween; call once per frame */
export function updateArticulation(car, time) {
  const DUR = { hinge: 0.62, glass: 0.42 };
  car.parts.forEach(p => {
    if (p.start >= 0) {
      const dur = DUR.hinge;
      const k = clamp((time - p.start) / dur, 0, 1);
      p.t = lerp(p.from, p.to, easeInOut(k));
      if (k >= 1) p.start = -1;
      p.group.rotation[p.axis] = p.dir * p.t * p.open;
    }
  });
  car.doors.forEach(d => {
    if (d.dropStart >= 0) {
      const k = clamp((time - d.dropStart) / DUR.glass, 0, 1);
      d.drop = lerp(d.dropFrom, d.dropTo, easeInOut(k));
      if (k >= 1) d.dropStart = -1;
      d.glass.position.y = -d.drop * d.glassTravel;
    }
  });
}

export function setPart(car, id, open, time) {
  const p = car.parts.find(x => x.id === id);
  if (!p) return false;
  p.from = p.t; p.to = open ? 1 : 0; p.start = time;
  return true;
}
export function setWindow(car, id, drop, time) {
  const d = car.doors.find(x => x.id === id);
  if (!d) return false;
  d.dropFrom = d.drop; d.dropTo = clamp(drop, 0, 1); d.dropStart = time;
  return true;
}
export function setSteer(car, deg) {
  const a = THREE.MathUtils.degToRad(clamp(deg, -32, 32));
  car.wheels.forEach(w => { if (w.userData.wheel.front) w.rotation.y = w.userData.wheel.z > 0 ? a : a; });
}

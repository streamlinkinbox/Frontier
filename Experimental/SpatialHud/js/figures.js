// The retained figure graph — Engine/SpatialInterface/InterfaceStructure.h and the composition from
// InterfaceLayoutCodec.cpp, in the browser.
//
// Retained, not immediate: a host constructs figures once, attaches them into a descent chain, then
// writes values frame to frame. A needle sweeping to the redline is one float write on an existing
// figure — nothing is re-tessellated, and that is the property the whole design exists for.
//
// Local plane convention, from the engine: +X right, +Y UP, origin at the figure centre, metres.

export const Category = {
  Surface: 0,       // rounded rectangle — housings, cards, buttons, bar troughs, toggle beds
  Arc: 1,           // annular sector; ScalarAlpha = fill fraction of the sweep
  TickRing: 2,      // repeated radial marks; ScalarBeta = mark count
  Needle: 3,        // tapered pointer; ScalarAlpha = angle fraction across the sweep
  SegmentCell: 4,   // one seven-segment digit; ScalarAlpha = digit 0..9 (10 blank, 11 minus)
  Lamp: 5,          // filled disc; ScalarAlpha = luminance
  Glyph: 6,         // one stroke-font character; ScalarAlpha = ASCII, ScalarBeta = stroke half width
  // 🔴 The one category with no native counterpart yet. It is numbered after Glyph so every ordinal
  //    the engine already ships keeps its value — adding a category must never renumber the others.
  StreakField: 7,
};

export const Slot = {
  Housing: 0, Surface: 1, SurfaceSunk: 2, Stroke: 3, Marking: 4,
  MarkingMute: 5, Accent: 6, Caution: 7, Warning: 8, Confirm: 9,
};

// PaletteConfiguration.cpp's instrument-dark default, linear, unchanged.
export const InstrumentDark = [
  [0.0090, 0.0100, 0.0120, 1.0],   // Housing
  [0.0200, 0.0220, 0.0260, 1.0],   // Surface
  [0.0110, 0.0125, 0.0150, 1.0],   // SurfaceSunk
  [0.0600, 0.0650, 0.0750, 1.0],   // Stroke
  [0.8000, 0.8300, 0.8800, 1.0],   // Marking
  [0.1800, 0.1950, 0.2200, 1.0],   // MarkingMute
  [0.0500, 0.4200, 0.9000, 1.0],   // Accent
  [0.9000, 0.4400, 0.0300, 1.0],   // Caution
  [0.8600, 0.0700, 0.0900, 1.0],   // Warning
  [0.0800, 0.6600, 0.2600, 1.0],   // Confirm
];

export const Detached = 0xffffffff;

// One drawable. Defaults match InterfaceFigure's, so a figure constructed with {} here and one
// default-constructed there describe the same thing.
export function Figure(fields = {}) {
  return Object.assign({
    category: Category.Surface,
    origin: [0, 0, 0],            // [m] relative to the ancestor's plane
    rotationX: 0, rotationY: 0, rotationZ: 0,   // [rad]
    scale: 1,
    halfWidth: 0.05, halfHeight: 0.05,          // [m]
    cornerRadius: 0,                            // [m]
    palette: Slot.Surface,
    tint: null,                   // null → the palette slot's colour
    baseColour: null,             // null → the tint, which is what the engine does for an unlit surface
    opacity: 1,
    scalarAlpha: 0, scalarBeta: 0,
    emissiveWeight: 1,            // 0 = albedo, lit by the room; 1 = pure emitter
    overlay: false,               // true = a widget: drawn bright, contributes nothing to room light
    orderingRank: 0,
    // Streak field only. Named rather than smuggled into scalarAlpha/Beta because six parameters do
    //    not fit in two floats, and the native slot will have to grow the same way.
    streak: null,
  }, fields);
}

export class Structure {
  constructor() {
    this.figures = [];
    this.ancestors = [];
    this.palette = InstrumentDark.map((One) => One.slice());
    this.tokenRadius = 0.006;     // [m] the corner used when a figure asks for the token radius
  }

  construct(figure) {
    this.figures.push(figure);
    this.ancestors.push(Detached);
    return this.figures.length - 1;
  }

  // Refuses cycles, as InterfaceStructure::Attach does — a cycle would spin the resolver forever.
  attach(child, ancestor) {
    if (child === ancestor) return false;
    for (let walk = ancestor; walk !== Detached; walk = this.ancestors[walk]) {
      if (walk === child) return false;
    }
    this.ancestors[child] = ancestor;
    return true;
  }

  query(index) { return this.figures[index]; }
  get count() { return this.figures.length; }
}

// ── composition ──────────────────────────────────────────────────────────────────────────────────
// InterfaceLayoutCodec.cpp ComposePlacement, term for term. R = Ry · Rx · Rz — roll in the plane
// first, then pitch toward the eye, then yaw into place.

export function composePlacement(f) {
  const Cz = Math.cos(f.rotationZ), Sz = Math.sin(f.rotationZ);
  const Cx = Math.cos(f.rotationX), Sx = Math.sin(f.rotationX);
  const Cy = Math.cos(f.rotationY), Sy = Math.sin(f.rotationY);
  const K = f.scale;

  const R00 = Cy * Cz + Sy * Sx * Sz;
  const R01 = -Cy * Sz + Sy * Sx * Cz;
  const R02 = Sy * Cx;
  const R10 = Cx * Sz;
  const R11 = Cx * Cz;
  const R12 = -Sx;
  const R20 = -Sy * Cz + Cy * Sx * Sz;
  const R21 = Sy * Sz + Cy * Sx * Cz;
  const R22 = Cy * Cx;

  return [
    [R00 * K, R01 * K, R02 * K, f.origin[0]],
    [R10 * K, R11 * K, R12 * K, f.origin[1]],
    [R20 * K, R21 * K, R22 * K, f.origin[2]],
  ];
}

export function combinePlacement(A, L) {
  const out = [[0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]];
  for (let r = 0; r < 3; r++) {
    for (let c = 0; c < 3; c++) {
      out[r][c] = A[r][0] * L[0][c] + A[r][1] * L[1][c] + A[r][2] * L[2][c];
    }
    out[r][3] = A[r][0] * L[0][3] + A[r][1] * L[1][3] + A[r][2] * L[2][3] + A[r][3];
  }
  return out;
}

// The two-sweep resolver from InterfaceSequence::Advance. An ancestor may be constructed after its
// descendant, so the pass repeats until nothing changes, bounded by the engine's DescentLimit.
export const DescentLimit = 64;

export function resolve(structure) {
  const total = structure.count;
  const placements = new Array(total).fill(null);
  let sweeps = 0, progress = true;

  while (progress && sweeps < DescentLimit) {
    progress = false;
    sweeps++;
    for (let at = 0; at < total; at++) {
      if (placements[at] !== null) continue;
      const ancestor = structure.ancestors[at];
      if (ancestor !== Detached && placements[ancestor] === null) continue;
      const local = composePlacement(structure.query(at));
      placements[at] = ancestor === Detached ? local : combinePlacement(placements[ancestor], local);
      progress = true;
    }
  }
  return { placements, sweeps };
}

// ── the sort key ─────────────────────────────────────────────────────────────────────────────────
// InterfaceSpecification.h: transparency in the top bit so the opaque group always submits first,
// then the caller's ordering rank, then view depth. Returned as a BigInt because 64 bits do not fit
// in a double and a silently truncated key sorts wrongly rather than visibly failing.

export const OpaqueThreshold = 0.999;

export function composeSortKey(transparent, orderingRank, viewDepth) {
  const depthBits = new DataView(new ArrayBuffer(4));
  depthBits.setFloat32(0, viewDepth, true);
  let depth = depthBits.getUint32(0, true);
  if (!transparent) depth = (~depth) >>> 0;     // opaque sorts front to back, so early-Z can work
  return (BigInt(transparent ? 1 : 0) << 63n)
       | (BigInt(orderingRank & 0x7fffff) << 40n)
       | (BigInt(depth) << 8n);
}

// ── packing ──────────────────────────────────────────────────────────────────────────────────────
// Ten vec4s per figure. The native slot is 112 packed bytes with RGBA8 tints; this one keeps the
// colours as floats and adds the two streak vectors, because the browser pays nothing for the space
// and an unpacked reference is one less thing to get wrong while reading it.

export const FloatsPerFigure = 40;

export function pack(structure, placements, eye) {
  const order = [];
  for (let at = 0; at < structure.count; at++) {
    const f = structure.query(at);
    const p = placements[at];
    if (!p) continue;
    const dx = p[0][3] - eye[0], dy = p[1][3] - eye[1], dz = p[2][3] - eye[2];
    const depth = Math.sqrt(dx * dx + dy * dy + dz * dz);
    order.push({
      at,
      rank: f.orderingRank,
      depth,
      // The engine's key is still computed, so a check can hold the two sides level and so the
      // finding below is about something real rather than about an approximation made here.
      key: composeSortKey(f.opacity < OpaqueThreshold, f.orderingRank, depth),
    });
  }

  // 🔴 SUBMISSION IS BY ORDERING RANK HERE, NOT BY THE ENGINE'S SORT KEY, AND THE DIFFERENCE IS A
  //    FINDING RATHER THAN A SHORTCUT.
  //
  //    ComposeInterfaceSortKey puts transparency in the TOP bit, so every transparent figure submits
  //    after every opaque one whatever its rank. That is correct when a depth buffer is doing the
  //    occlusion and the two groups only need to be ordered among themselves. It is wrong the moment
  //    a large TRANSPARENT figure has to sit UNDER opaque ones — which is exactly what the streak
  //    field is: rank 2, opacity 0.85, beneath an opaque needle at rank 7 and opaque digits at rank
  //    9. Under the engine's key the backdrop would submit last and paint over its own instrument.
  //
  //    The browser has no depth buffer here (every figure in this composition is coplanar to within
  //    three millimetres, so a depth test would resolve nothing and z-fight instead), and rank is
  //    the ordering the layout actually means. Back-to-front by depth breaks ties.
  //
  //    The native side will have to answer this when the streak field crosses over: either the
  //    backdrop becomes opaque, or the key stops treating transparency as the most significant bit.
  order.sort((A, B) => (A.rank - B.rank) || (B.depth - A.depth));

  const out = new Float32Array(order.length * FloatsPerFigure);
  order.forEach(({ at }, slot) => {
    const f = structure.query(at);
    const p = placements[at];
    const tint = f.tint || structure.palette[f.palette];
    const base = f.baseColour || tint;
    const radius = f.cornerRadius < 0 ? structure.tokenRadius : f.cornerRadius;
    const s = f.streak || [0, 0, 0, 0, 0, 0, 0, 0];
    const o = slot * FloatsPerFigure;

    out.set(p[0], o + 0);
    out.set(p[1], o + 4);
    out.set(p[2], o + 8);
    out.set([f.halfWidth, f.halfHeight, radius, f.opacity], o + 12);
    // Clip wide open, as P0 writes it. P2 animates this for masked wipes under arbitrary transforms.
    out.set([-1e6, -1e6, 1e6, 1e6], o + 16);
    out.set([f.category, f.scalarAlpha, f.scalarBeta, f.emissiveWeight], o + 20);
    out.set(tint, o + 24);
    out.set([base[0], base[1], base[2], f.overlay ? 1 : 0], o + 28);
    out.set(s.slice(0, 4), o + 32);
    out.set(s.slice(4, 8), o + 36);
  });
  return { data: out, count: order.length, order: order.map((One) => One.at) };
}

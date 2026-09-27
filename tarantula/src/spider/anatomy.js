// Anatomical reference data for an adult female Brachypelma hamorii (Mexican red-knee tarantula).
// Units: centimetres. Body frame: +Z forward (anterior), +Y dorsal, +X animal's LEFT.
//
// Sources used for proportions/colour (see README):
//  - Mendoza & Francke 2017 (via Wikipedia): female body length 52-54 mm excl. chelicerae/spinnerets,
//    leg IV longest (67 mm female). Legs/palps black to reddish-black with three rings: deep orange on
//    the proximal patella grading to pale orange-yellow, pale orange-yellow on the distal tibia,
//    yellowish-white at the distal metatarsus. Carapace black with a brownish-pink/orange border.
//  - Theraphosid leg formula for Brachypelma: IV > I > II > III.
//  - Seven podomeres per leg: coxa, trochanter, femur, patella, tibia, metatarsus, tarsus.
//  - Pedipalp: six podomeres (no metatarsus).

export const BODY = {
  carapace: { length: 2.35, width: 2.12, height: 0.62, frontZ: 1.2, backZ: -1.15 },
  fovea: { z: -0.28 },
  ocular: { z: 0.86, y: 0.58 },
  bodyHeight: 0.78, // prosoma centre above the substrate in a relaxed stance
  abdomen: { length: 3.0, width: 2.25, height: 1.95, centerZ: -2.78, centerY: 0.12 },
  pedicel: { z: -1.16, y: -0.02 },
};

// Leg definitions: socket on the pleural membrane between carapace and sternum (left side, mirror for right).
// yaw0 = resting azimuth of the leg plane measured from +Z toward the animal's side.
// seg = [coxa, trochanter, femur, patella, tibia, metatarsus, tarsus] lengths; rad = matching mean radii.
export const LEGS = [
  { id: 1, socket: [0.66, -0.12, 0.62], yaw0: 30, seg: [0.72, 0.42, 1.58, 0.96, 1.22, 1.12, 0.82], rad: [0.302, 0.258, 0.274, 0.263, 0.230, 0.174, 0.168], reach: 0.86 },
  { id: 2, socket: [0.80, -0.13, 0.22], yaw0: 66, seg: [0.68, 0.40, 1.42, 0.86, 1.06, 1.06, 0.76], rad: [0.291, 0.246, 0.263, 0.252, 0.218, 0.168, 0.162], reach: 0.86 },
  { id: 3, socket: [0.82, -0.13, -0.22], yaw0: 108, seg: [0.64, 0.38, 1.26, 0.76, 0.92, 1.22, 0.72], rad: [0.291, 0.246, 0.269, 0.252, 0.218, 0.166, 0.157], reach: 0.86 },
  { id: 4, socket: [0.70, -0.12, -0.64], yaw0: 150, seg: [0.72, 0.44, 1.66, 0.86, 1.36, 1.82, 0.80], rad: [0.302, 0.258, 0.291, 0.269, 0.230, 0.168, 0.157], reach: 0.86 },
];

// Pedipalp: coxa (bears the maxilla), trochanter, femur, patella, tibia, tarsus.
export const PALP = { socket: [0.36, -0.2, 0.98], yaw0: 12, seg: [0.52, 0.34, 1.06, 0.64, 0.76, 0.72], rad: [0.224, 0.213, 0.224, 0.224, 0.202, 0.179] };

// Tetrapod gait: {L1, R2, L3, R4} vs {R1, L2, R3, L4}; within a tetrapod, the hind pair (3,4)
// leads the fore pair (1,2) by ~10 % of the cycle (Spagna & Peattie 2012; Biancardi et al. 2011).
export function gaitOffset(legIndex /*0..3*/, side /*+1 left, -1 right*/) {
  const tetrapodA = (legIndex % 2 === 0) === (side > 0); // L1,L3,R2,R4
  const base = tetrapodA ? 0 : 0.5;
  const withinGroup = legIndex <= 1 ? 0.1 : 0.0;
  return (base + withinGroup) % 1;
}

// ---------------------------------------------------------------------------------------------
// Colour tables (sRGB hex, converted to linear at geometry build time)
// ---------------------------------------------------------------------------------------------
export const COL = {
  black: 0x0b0a09,
  blackWarm: 0x120d0a,
  velvet: 0x0e0c0b,
  coxa: 0x17110d,
  orangeDeep: 0xb4380e,
  orangePale: 0xc98652,
  ringTibia: 0xb8916a,
  ringMeta: 0xc9b597,
  scopula: 0x2b2621,
  carapaceRim: 0xb86a38,
  carapaceRimPale: 0xcf965e,
  abdomenPile: 0x0d0a09,
  abdomenHair: 0x7a3a1c,
  abdomenHairTip: 0xb46a36,
  guardPale: 0xbf9a66,
  guardOrange: 0xe0a060,
  chelBand: 0xa36f5d,
  cuticle: 0x0a0807,
};

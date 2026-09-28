// Bald Eagle (Haliaeetus leucocephalus) — adult female, mid-range of published measurements.
// All values are metres / radians. Sources are listed in ANATOMY.md; the key ones are noted inline.
//
// Coordinate convention (rig space):  +Z = forward (beak), +Y = up (dorsal), +X = the bird's LEFT.
// The right side is built as a mirror image (scale −1 on X) of the left.

const D = Math.PI / 180;
export const deg = D;

export const SIZE = {
  totalLength: 0.90,   // bill tip → tail tip, 70–102 cm (Britannica, BOW)
  wingspan: 2.05,      // 1.8–2.3 m (BOW); female ≈ 25 % heavier than male
  mass: 5.2,           // kg, female 4.5–6.3
  wingChord: 0.60,     // flattened wing (wrist → tip of longest primary) 51.5–69 cm
  tail: 0.31,          // 23–37 cm, slightly wedge-shaped (central rectrices longest)
  tarsus: 0.095,       // 8–11 cm, unfeathered on the lower half, yellow
  culmen: 0.058,       // exposed culmen 5–7.5 cm (female)
  gape: 0.085,         // gape to bill tip 7–9 cm
  billDepth: 0.035,    // at the cere
};

// ------------------------------------------------------------------ skeleton (lengths joint-to-joint)
// Humerus / ulna / carpometacarpus proportions follow accipitrid wing skeletons (≈ 1 : 1.12 : 0.53);
// sized so the fully spread wingspan (incl. primaries) reaches SIZE.wingspan.
export const BONES = {
  humerus: 0.205,
  ulna: 0.232,
  hand: 0.108,          // carpometacarpus
  digit: 0.072,         // phalanges of the major digit (p9–p10 attach here)
  alula: 0.038,         // digit I (alula / bastard wing)
  femur: 0.112,
  tibiotarsus: 0.160,
  tarsometatarsus: 0.095,
  neckVertebrae: 14,    // accipitrids have 13–14 free cervical vertebrae (very flexible neck)
  neckLength: 0.215,    // along the curve, base of neck (C14) → atlas
};

// Trunk landmarks in trunk-bone space (origin ≈ centre of mass, z along the spine).
export const TRUNK = {
  shoulder: [0.052, 0.040, 0.112],  // glenoid (left)
  neckBase: [0, 0.052, 0.140],
  hip: [0.046, 0.004, -0.030],      // acetabulum (left)
  pygostyle: [0, 0.030, -0.150],    // tail base
  length: 0.33,                     // chest front → vent (feathered body ≈ 0.36)
  width: 0.19,                      // across the shoulders, feathered body ≈ 0.23
  depth: 0.17,                      // back → keel
};

// ------------------------------------------------------------------ flight feathers
// Primaries p1 (innermost) … p10 (outermost). Eagle primaries are long, pointed and the outer ones
// are emarginated (outer vane) and notched (inner vane) forming the "fingers" (Trail 2014 feather ID
// guide). Bald eagle: 7 visible fingers in soaring silhouette (p4–p10 emarginated), p7 ≈ p8 longest.
export const PRIMARIES = [
  //  length, emarg.(outer vane step, fraction from tip), notch (inner vane, fraction from tip), base width
  { len: 0.305, emarg: 0.00, notch: 0.00, w: 0.062 }, // p1
  { len: 0.318, emarg: 0.00, notch: 0.00, w: 0.064 }, // p2
  { len: 0.333, emarg: 0.00, notch: 0.00, w: 0.065 }, // p3
  { len: 0.352, emarg: 0.20, notch: 0.00, w: 0.066 }, // p4
  { len: 0.378, emarg: 0.30, notch: 0.24, w: 0.068 }, // p5
  { len: 0.415, emarg: 0.38, notch: 0.33, w: 0.069 }, // p6
  { len: 0.452, emarg: 0.44, notch: 0.40, w: 0.070 }, // p7
  { len: 0.458, emarg: 0.47, notch: 0.44, w: 0.070 }, // p8
  { len: 0.430, emarg: 0.50, notch: 0.47, w: 0.068 }, // p9
  { len: 0.365, emarg: 0.54, notch: 0.50, w: 0.062 }, // p10
];
// Secondaries s1 (at the wrist) … s15 (at the elbow): rounded, broad, nearly symmetric.
export const N_SECONDARIES = 15;
export const secondaryLength = (i) => 0.300 - 0.020 * (i / 14) ** 1.4; // s1 0.300 → s15 0.280
export const N_TERTIALS = 3;
export const tertialLength = (i) => [0.255, 0.235, 0.205][i];

// Rectrices r1 (central) … r6 (outer) per side; the tail is slightly wedge-shaped.
export const RECTRICES = [0.310, 0.306, 0.300, 0.293, 0.285, 0.276];

// ------------------------------------------------------------------ head
export const HEAD = {
  crownToCere: 0.100,     // back of skull → cere
  width: 0.072,           // across the supraorbital ridges
  eyeRadius: 0.0135,      // exposed eye (cornea + iris) ~2.7 cm; iris pale yellow in adults
  eyeForward: 0.018,      // eyes face ~ 30–35° forward of lateral (binocular field ≈ 35–50°)
};

// ------------------------------------------------------------------ colours (linear-ish sRGB hex)
export const COLORS = {
  bodyBrown: 0x2e2118,     // dark chocolate brown
  bodyBrownLight: 0x4a3524,// pale feather edges / wear
  white: 0xf1eee6,         // head, neck, tail, tail coverts
  whiteShadow: 0xd8d2c6,
  beak: 0xeab43e,          // chrome-yellow bill and cere (keratin slightly translucent, paler at the tip)
  beakTip: 0xdcb070,
  feet: 0xe9b62a,          // yellow feet and bare tarsus
  talon: 0x0c0b0a,         // glossy black talons
  iris: 0xf2e9a6,          // pale lemon iris (adult)
};

export const FLAP_HZ = 2.8; // golden-eagle wingbeat frequency (Harel et al. / PNAS 2021)

/**
 * ANATOMY — researched morphometry of a female Aedes aegypti.
 *
 * Scene unit = 1 millimetre. Every number here is taken from literature and is
 * annotated with the source id used in docs/ANATOMY.md. Nothing in this file is
 * "art scale" — the mesh builder, the IK rig and the animation controllers all
 * consume these numbers directly, so changing one changes the insect everywhere.
 *
 * Body frame:  +X anterior (head)   +Y dorsal (up)   +Z left
 * Origin:      centre of the prothorax/mesothorax boundary, i.e. the shoulder girdle.
 */

import { Vector3 } from 'three';

const TAU = Math.PI * 2;
const D2R = Math.PI / 180;

/* ------------------------------------------------------------------ body -- */

export const BODY = {
  head: {
    width: 0.70,
    height: 0.62,
    depth: 0.58,
    centre: [0.98, 0.02, 0],
    // Compound eyes bulge far enough laterally that the head reads as
    // "two eyes with a small head", which is the correct silhouette.
    eyeRadius: 0.235,
    eyeCentre: [1.02, 0.045, 0.255],
    eyeBulge: 0.055,
    ocelli: [
      [0.70, 0.285, 0.075],
      [0.70, 0.285, -0.075],
      [0.615, 0.275, 0.0],
    ],
    // Culicine female: palps are short, ~37 % of proboscis length.
    palp: { length: 0.86, radius: 0.048, segments: 4 },
    // Female antennae: 12 flagellomeres, sparse short hairs (contrast: male plumose).
    antenna: { scape: 0.22, pedicel: 0.075, flagellum: 1.63, flagellomeres: 12, radius: 0.026 },
    clypeusDots: true, // S14: female Ae. aegypti has two silvery clypeal dots
  },

  thorax: {
    scutum: { length: 1.10, width: 0.66, height: 0.30, centre: [0.55, 0.30, 0] },
    scutellum: { length: 0.24, width: 0.40, height: 0.075, centre: [-0.03, 0.415, 0] },
    metanotum: { length: 0.42, width: 0.30, height: 0.14, centre: [-0.28, 0.26, 0] },
    // Pleurosternal walls, visible from the side — without these the thorax
    // reads as a smooth bean instead of a sclerotised box.
    pleuron: { length: 0.92, height: 0.40, y: 0.02 },
    // Three leg-bearing sclerites, with the real attachment offsets.
    coxa: {
      pro: [0.62, -0.20, 0.30],
      mes: [0.22, -0.20, 0.30],
      meta: [-0.18, -0.20, 0.30],
      radius: 0.115,
    },
    haltere: { length: 0.34, radius: 0.045, centre: [-0.50, 0.19, 0.20] },
    wingRoot: { pos: [0.30, 0.20, 0.20], pitch: 12 * D2R },
  },

  abdomen: {
    length: 2.35,
    // Tergite girth: swells to T-IV then tapers to the point (S: "Aedes has a
    // pointy abdomen", ECDC key).
    profile: [
      [0.000, 0.185], [0.100, 0.255], [0.240, 0.275], [0.400, 0.272],
      [0.560, 0.255], [0.720, 0.225], [0.880, 0.185], [1.000, 0.135],
      [1.000, 0.040],
    ],
    start: [-0.60, 0.16, 0],
    segments: 8,
    bandStart: 1, // tergite index where pale basal bands begin (S14: T-II..T-VI)
    bandEnd: 6,
    cerci: { length: 0.11, radius: 0.022 },
  },

  proboscis: {
    // S2: 2.32 mm, ⌀ ~0.08 mm for the whole proboscis, labium carries the bulk.
    length: 2.32,
    sheathRadius: 0.052,
    fascicleRadius: 0.014, // S2: stylets ~50 µm
    baseAngle: 34 * D2R, // points anteriorly and downward from the head
    labella: { length: 0.10, radius: 0.038, spread: 16 * D2R },
    // 6 stylets, all independently articulable in principle; we animate the
    // two mandibular + two maxillary saws and the labrum food canal.
    stylets: [
      { id: 'labrum', r: 0.0, phase: 0 },
      { id: 'hypopharynx', r: 0.0, phase: Math.PI },
      { id: 'mandible.L', r: 0.5, phase: 0 },
      { id: 'mandible.R', r: 0.5, phase: Math.PI },
      { id: 'maxilla.L', r: 0.5, phase: 0.35 },
      { id: 'maxilla.R', r: 0.5, phase: Math.PI + 0.35 },
    ],
    sawHz: 12, // S2: maxillary microsaw 10-15 Hz
    fascicleHz: 30, // S2: whole fascicle vibrates ~30 Hz
  },
};

/* ------------------------------------------------------------------ legs -- */

/**
 * Segment lengths in mm, per leg pair. S15: fore < mid < hind.
 * `pitch` is the rest angle of the whole leg away from vertical, in the sagittal
 * plane; `abduct` is the outward splay in the transverse plane. These two
 * numbers are what keep the six feet from ever crossing the midline.
 */
export const LEGS = {
  L1: {
    key: 'L1', name: 'fore-left', side: 1, index: 0,
    scale: 1.0,
    coxa: 0.30, trochanter: 0.09, femur: 1.28, tibia: 1.38,
    tarsus: [0.135, 0.115, 0.105, 0.098, 0.082],
    claw: 0.14,
    femurRadius: [0.072, 0.050], tibiaRadius: [0.050, 0.036],
    restPitch: 34 * D2R, restAbduct: 26 * D2R,
    minLateral: 0.11,   // hard floor on |z| — the anti-crossing constraint
    // S15: fore femur carries a longitudinal pale line; knee spot present.
    markings: { femurStripe: true, kneeSpot: true, tarsalBands: 2, tibialBands: 1 },
    reach: 3.10,
  },
  L2: {
    key: 'L2', name: 'mid-left', side: 1, index: 1,
    scale: 1.0,
    coxa: 0.32, trochanter: 0.09, femur: 1.52, tibia: 1.62,
    tarsus: [0.145, 0.125, 0.115, 0.105, 0.090],
    claw: 0.15,
    femurRadius: [0.078, 0.052], tibiaRadius: [0.052, 0.037],
    restPitch: 2 * D2R, restAbduct: 62 * D2R,
    minLateral: 0.11,
    markings: { femurStripe: true, kneeSpot: true, tarsalBands: 3, tibialBands: 2 },
    reach: 3.60,
  },
  L3: {
    key: 'L3', name: 'hind-left', side: 1, index: 2,
    scale: 1.0,
    coxa: 0.34, trochanter: 0.10, femur: 1.78, tibia: 1.92,
    tarsus: [0.160, 0.138, 0.126, 0.115, 0.098],
    claw: 0.17,
    femurRadius: [0.088, 0.058], tibiaRadius: [0.058, 0.040],
    restPitch: -34 * D2R, restAbduct: 30 * D2R,
    minLateral: 0.11,
    markings: { femurStripe: false, kneeSpot: true, tarsalBands: 4, tibialBands: 2 },
    reach: 4.10,
  },
};
LEGS.R1 = mirrored(LEGS.L1, 'R1', 'fore-right', -1, 0);
LEGS.R2 = mirrored(LEGS.L2, 'R2', 'mid-right', -1, 1);
LEGS.R3 = mirrored(LEGS.L3, 'R3', 'hind-right', -1, 2);

function mirrored(src, key, name, side, index) {
  const c = JSON.parse(JSON.stringify(src));
  c.key = key; c.name = name; c.side = side; c.index = index;
  c.coxaKey = ['pro', 'mes', 'meta'][index];
  return c;
}
for (const k of ['L1', 'L2', 'L3']) LEGS[k].coxaKey = ['pro', 'mes', 'meta'][LEGS[k].index];

/**
 * Nominal stance position of one tarsus, in thorax-local millimetres.
 *
 * The spread is not a free parameter: it is the radius at which this leg's own
 * femur + tibia put the femur–tibia joint at its neutral ~90° posture, the
 * value measured in walking insects. Anything shorter tucks the tarsus under
 * the body and folds the knee past its anatomical limit; anything longer
 * over-extends it. The fore–aft rake and the side-to-side splay still come from
 * the leg's rest pitch and abduction, so the six feet keep their fan.
 *
 * `rootLocal` is the coxa attachment on the thorax; `clearance` is how far the
 * thorax rides above the substrate.
 */
export function nominalStance(spec, rootLocal, clearance = GAIT.bodyClearance) {
  const hip = rootLocal.clone().addScaledVector(coxaRestDir(spec), spec.coxa);
  const rise = spec.tarsus.reduce((x, y) => x + y, 0) * 0.96;
  const soleY = -clearance;
  // Vertical component of the hip→ankle chord (the tarsus hangs below the
  // ankle by roughly its own length).
  const dy = (soleY + rise) - hip.y;
  const target = GAIT.stanceExtension * (spec.trochanter + spec.femur + spec.tibia);
  // Rake fore/aft by restPitch, swing outboard by restAbduct.
  const dir = new Vector3(
    Math.sin(spec.restPitch) * Math.cos(spec.restAbduct), 0,
    spec.side * Math.sin(spec.restAbduct));
  dir.divideScalar(dir.length() || 1);
  const h = Math.max(Math.sqrt(Math.max(0, target * target - dy * dy)), (spec.minLateral ?? 0.1) + 0.22);
  return new Vector3(hip.x + dir.x * h, soleY, hip.z + dir.z * h);
}

/**
 * Rest direction of the coxa link, in thorax-local space. Shared by the mesh
 * builder (which uses it for the static rest pose) and the gait planner (which
 * needs the same hip position to place a tarsus anatomically).
 */
export function coxaRestDir(spec) {
  const d = new Vector3(
    Math.cos(spec.restPitch) * 0.35 * Math.cos(spec.restAbduct) + 0.12,
    -Math.sin(spec.restPitch) * 0.55 - 0.72,
    spec.side * Math.sin(spec.restAbduct) * 0.95);
  return d.normalize();
}

/**
 * Tripod membership — S10/S11. Tripod A = {L1, R2, L3} (fore-left, mid-right,
 * hind-left); tripod B is its mirror image {R1, L2, R3}. The front leg of a
 * tripod leads the middle, which leads the hind (the *M-tripod* of Kim 2021).
 */
export const TRIPOD_A = ['L1', 'R2', 'L3'];
export const TRIPOD_B = ['R1', 'L2', 'R3'];
/** Intra-tripod lead: front → mid → hind, 0.035 cycle per rank (S11). */
export const TRIPOD_LEAD = { 0: 0.0, 1: 0.035, 2: 0.070 };

/* ----------------------------------------------------------------- wings -- */

export const WING = {
  length: 2.72, // S16: alular notch → apex of R3
  chordRoot: 0.40,
  chordMax: 0.62,
  aspectRatio: 4.2, // S6
  // Planform control points: [spanFraction, chord] sampled along the wing.
  planform: [
    [0.00, 0.185], [0.06, 0.395], [0.14, 0.585], [0.26, 0.620],
    [0.40, 0.585], [0.55, 0.500], [0.70, 0.390], [0.84, 0.255],
    [0.93, 0.150], [1.00, 0.052],
  ],
  // Dipteran venation: costa (C), subcosta (Sc), radius R1-R5, media M1-M4,
  // cubitus Cu, anal A, cross-veins r-m, m-cu. Positions are fractions of
  // (span, chord) so the venation survives rescaling.
  veins: [
    // Leading edge
    { name: 'C', from: [0.02, 0.06], to: [0.98, 0.04], w: 0.030, costal: true },
    { name: 'Sc', from: [0.05, 0.30], to: [0.55, 0.22], w: 0.018 },
    { name: 'R1', from: [0.10, 0.46], to: [0.99, 0.47], w: 0.024 },
    { name: 'R2+3', from: [0.20, 0.50], to: [0.93, 0.62], w: 0.020 },
    { name: 'R4+5', from: [0.20, 0.52], to: [0.80, 0.78], w: 0.018 },
    // Longitudinal system
    { name: 'M1', from: [0.28, 0.60], to: [0.55, 0.93], w: 0.014 },
    { name: 'M2', from: [0.33, 0.68], to: [0.62, 0.96], w: 0.013 },
    { name: 'M3', from: [0.38, 0.74], to: [0.70, 0.98], w: 0.012 },
    { name: 'M4', from: [0.40, 0.86], to: [0.56, 0.99], w: 0.011 },
    { name: 'Cu1', from: [0.44, 0.90], to: [0.66, 0.90], w: 0.011 },
    { name: 'Cu2', from: [0.48, 0.94], to: [0.74, 0.80], w: 0.010 },
    { name: 'A', from: [0.42, 0.97], to: [0.62, 0.72], w: 0.009 },
    // Cross veins
    { name: 'r-m', from: [0.34, 0.54], to: [0.30, 0.72], w: 0.008 },
    { name: 'm-cu', from: [0.40, 0.72], to: [0.44, 0.90], w: 0.007 },
    { name: 'Cu-A', from: [0.46, 0.92], to: [0.50, 0.95], w: 0.006 },
  ],
  // Scaled patches — S14: pale patch at the base of the costa.
  spots: [
    { u: 0.05, v: 0.10, r: 0.055, pale: true },
    { u: 0.30, v: 0.30, r: 0.030, pale: false },
    { u: 0.52, v: 0.52, r: 0.026, pale: false },
  ],
  fringe: { rows: 2, length: 0.055 },
  thickness: 0.006,
};

export const FLIGHT = {
  // S7/S8: Ae. aegypti female 498 Hz, Ae. albopictus female 499 Hz.
  frequencyHover: 498,
  frequencyCruise: 522,
  frequencyMax: 806, // S5/S9: >800 Hz free flight
  frequencyMin: 208,
  // S5/S6: total angular sweep ≈ 40-45°. We use the midpoint, 44°.
  sweepAmplitude: 22 * D2R,
  strokePlaneTilt: 28 * D2R,
  // S5: wing pitch is the dominant lift mechanism; it flips at stroke reversal.
  angleOfAttack: 38 * D2R,
  reversalFraction: 0.06, // lag ≈ 6 % of cycle (S6)
  // S6: flexible wing — the tip trails the root in phase.
  tipLag: 0.16,
  tipLagPhase: 0.30,
  dvdtLag: 0.22,
  dvdtGain: 0.55,
  // S9: free-flight body attitude.
  bodyPitchHover: 12 * D2R,
  bodyBounceAmp: 0.018,
  // Wedge/knockdown climb at high advance speed.
  maxAdvanceRatio: 1.6,
  cruiseSpeed: 340, // mm/s  (0.34 m/s — S9 max observed 0.4 m/s)
  maxSpeed: 620,
};

/* -------------------------------------------------------------- gaits -- */

export const GAIT = {
  dutyFactor: 0.68, // S11
  stepLength: 0.55,
  strideFrequency: 14,
  strideFrequencyMax: 22,
  bodyClearance: 0.62,
  // Walking insects hold the tibia near 90° to the femur — the neutral
  // posture of the femur–tibia joint (Bässler 1983; JEB tibia-moment-arm
  // geometry). The stance radius is the one that produces that angle from
  // each leg's own segment lengths, which is what splays the tarsi out
  // instead of tucking them under the body.
  stanceExtension: 0.68,
  bodyBob: 0.18,
  // Tarsal timing (S13): heel strike flexion, then release flexion.
  tarsusStrike: 32 * D2R,
  tarsusRelease: 40 * D2R,
  // Swing clearance — the foot must clear terrain, and real insects lift with a
  // long shallow arc, not a snappy snap.
  swingLift: 0.42,
  swingLiftProfile: 1.35, // >1 = flatter apex, longer glide
  speedToTripod: 0.55, // m/s at which the pure tripod is fully established
  waveLag: 1 / 6,
  maxJointAngle: 132 * D2R,
  minJointAngle: 6 * D2R,
};

export const FEED = {
  // S2
  sawHz: 12,
  fascicleHz: 30,
  sheathRetract: 0.92, // fraction of the proboscis that loops back over the head
  penetration: 0.62, // mm of fascicle actually inside the host
  engorgeScale: 1.9,
  salivationDrops: 5,
};

/* ---------------------------------------------------------- utilities -- */

export const smoothstep = (a, b, x) => {
  const t = Math.min(1, Math.max(0, (x - a) / (b - a)));
  return t * t * (3 - 2 * t);
};
export const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
export const lerp = (a, b, t) => a + (b - a) * t;
export const mix3 = (a, b, t) => [lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t)];
export const dirToRadians = D2R;
export { TAU, D2R };

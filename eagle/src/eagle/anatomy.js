// Bald eagle (Haliaeetus leucocephalus), adult - reference measurements (metres, radians, seconds).
//
// Sources (see README.md for full list):
//  - Wikipedia / Birds of the World species account: length 70-102 cm, wingspan 1.8-2.3 m, wing chord
//    51.5-69 cm, tail 23-37 cm (moderately long, slightly wedge-shaped), tarsus 8-11 cm, culmen 3-7.5 cm,
//    gape-to-tip 7-9 cm; plumage evenly dark brown with white head and tail, yellow bill, feet and eyes.
//  - PA Game Commission: tarsi NOT feathered (unlike the "booted" golden eagle); soars on flat wings held
//    at right angles to the body ("flying plank"); head protrudes more than half the tail length.
//  - Carolina Bird Club raptor primer: leading & trailing edges nearly parallel, board-flat soaring wing;
//    ends a flapping sequence with a downstroke into the glide (no upward adjustment).
//  - Flight feather anatomy: 10 functional primaries on the manus (6 on the carpometacarpus, 4 on the
//    phalanges); secondaries on the ulna; outer primaries emarginated -> slotted "fingers".
//  - Wingbeat: ~2.8 Hz measured on a free-flying eagle (Laurent et al. 2021, PNAS).
//  - Upstroke: wing partially flexed at elbow & wrist, hand-wing swept back, primaries separate and
//    rotate (slots); arm-wing stays partially extended. Downstroke: extended, moves down & forward with
//    the leading edge lowered (pronation). Elbow & wrist flex/extend together (coupled skeleton).
//  - Walking (Birds of the World): "awkward, rocking gait of alternating steps, rarely hops"; golden eagle
//    account: great strides needing a rolling motion of the body and shuffling of the wings for balance.
//  - Head: raptors change gaze with fast head saccades separated by fixations (Kane et al. 2017, Auk);
//    birds stabilise head orientation during flight; peak saccade speeds ~1000 deg/s and more.
//  - Peal call: 3-5 gull-like notes followed by 6-7 rapid notes ("kwit-kwit-kwit-kee-kee-kee-ker"),
//    delivered with the head thrown far back, bill pointing skyward.
//  - Perching reflex / tendon-locking mechanism: flexing ankle & knee pulls the toes closed.

export const DIM = {
  length: 0.92,          // bill tip to tail tip
  wingspan: 2.1,
  wingChord: 0.61,       // wrist to tip of longest primary
  tailLength: 0.32,
  tarsus: 0.10,
  culmen: 0.058,
  gape: 0.082,
};

// Skeleton (metres). Body axis along +z (forward), +y dorsal, +x = bird's LEFT.
export const SKEL = {
  // trunk
  pelvisToThorax: 0.13,
  thoraxToNeck: 0.1,
  // neck: 14 cervical vertebrae modelled as 8 segments
  neckSegs: [0.03, 0.03, 0.028, 0.026, 0.025, 0.024, 0.022, 0.02],
  // wing: humerus, ulna/radius, carpometacarpus, major digit
  shoulder: [0.075, 0.045, 0.105],   // left shoulder joint relative to thorax bone origin
  humerus: 0.205,
  ulna: 0.235,
  hand: 0.115,
  digit: 0.075,
  // leg
  hip: [0.055, -0.02, 0.0],          // left acetabulum relative to pelvis
  femur: 0.105,
  tibiotarsus: 0.165,
  tarsometatarsus: 0.1,
  // toes (phalanx lengths, talon length). dir = yaw for the LEFT foot (+ = outward, toward +x); the right
  // foot mirrors it. Digit I (hallux) points back.
  toes: [
    { name: 'I', phal: [0.028, 0.022], talon: 0.048, dir: Math.PI + 0.18, spread: 0.0 },   // hallux (back, slightly inward)
    { name: 'II', phal: [0.026, 0.02, 0.018], talon: 0.042, dir: -0.45, spread: 0 },       // inner (toward the body)
    { name: 'III', phal: [0.024, 0.02, 0.018, 0.015], talon: 0.033, dir: 0.0, spread: 0 }, // middle
    { name: 'IV', phal: [0.016, 0.013, 0.012, 0.012, 0.012], talon: 0.03, dir: 0.55, spread: 0 },  // outer
  ],
  tailToPelvis: -0.12,
};

// Plumage (linear sRGB-ish base colours, tinted per feather for variation)
export const COLOR = {
  brown: 0x3a2618,       // body & wing contour feathers
  brownDark: 0x22160f,   // flight feathers
  brownCovert: 0x46301f,
  white: 0xf2efe6,
  whiteShade: 0xe0dbcf,
  bill: 0xf0b21c,
  cere: 0xf2c23a,
  feet: 0xf0c13a,
  talon: 0x0c0b0a,
  iris: 0xf3e28a,
};

// Flight
export const FLIGHT = {
  flapHz: 2.8,
  downstrokeFrac: 0.56,
  glideSpeed: 13,      // m/s
  flapSpeed: 11,
};

/**
 * Reference-informed, high-target prey-capture strike (not a deimatic display).
 *
 * Angles are sagittal elevations in degrees: +Z forward, +Y up. `gape` is
 * the INCLUDED femur–tibia angle; 0° is fully folded, 180° is straight.
 * This convention prevents the tibia taking an arbitrary quaternion shortest
 * arc through the back of the femur. Each femur/tibia has one hinge axis.
 *
 * The approach mostly elevates the coxa with the coxa–femur angle held;
 * the sweep rapidly opens that joint while the tibia closes around the prey
 * space. Closure overlaps forward movement, and the closed grippers retract
 * towards the mouth. No hands clapping together, forward punch, or wrist flick.
 *
 * Phase order: Oufiero et al. 2016, JEB 219:2733–2742, Fig. 1;
 * Rossoni & Niven 2020, Biology Letters 16:20200098, Fig. 1 / high-speed stills;
 * Roles of muscle activities in foreleg movements during predatory strike of
 * the mantis, Journal of Insect Physiology (online 2022).
 * Angles/times below are authored for this rig, NOT measured M. religiosa data.
 */
export const STRIKE = Object.freeze({
  revision: 'hinged-capture-v2',
  duration: 2.4,
  sampleRate: 240,
  setEnd: .280,
  approachEnd: .500,
  sweepStart: .500,
  sweepEnd: .530,
  closeStart: .510,
  closeEnd: .544,
  retractStart: .570,
  retractEnd: .820,
  holdEnd: 1.200,
  resetEnd: 1.860,
});

const clamp = (x, a, b) => Math.min(b, Math.max(a, x));
const ease = (t) => { t = clamp(t, 0, 1); return t * t * (3 - 2 * t); };
const ramp = (a, b, t) => ease((t - a) / (b - a));
function curve(t, keys) {
  if (t <= keys[0][0]) return keys[0][1];
  for (let i = 1; i < keys.length; i++) {
    const [at, value] = keys[i], [before, previous] = keys[i - 1];
    if (t <= at) return previous + (value - previous) * ramp(before, at, t);
  }
  return keys.at(-1)[1];
}

export function strikePhase(t) {
  if (t < STRIKE.setEnd) return 'SET & AIM';
  if (t < STRIKE.sweepStart) return 'APPROACH';
  if (t < STRIKE.closeStart) return 'SWEEP';
  if (t < STRIKE.closeEnd) return 'SWEEP + CLAMP';
  if (t < STRIKE.retractStart) return 'CAPTURE';
  if (t < STRIKE.retractEnd) return 'PULL TO MOUTH';
  if (t < STRIKE.holdEnd) return 'HOLD';
  if (t < STRIKE.resetEnd) return 'RECOVER';
  return 'READY';
}

/** `rest` is derived from the actual authored bone endpoints, not guessed. */
export function sampleStrike(time, rest) {
  const t = clamp(time, 0, STRIKE.duration);
  const coxa = curve(t, [
    [0, rest.coxa], [.045, rest.coxa],
    [STRIKE.setEnd, -90], [STRIKE.approachEnd, -24],
    [STRIKE.sweepEnd, -13], [STRIKE.retractStart, -13],
    [STRIKE.retractEnd, -77], [STRIKE.holdEnd, -77],
    [STRIKE.resetEnd, rest.coxa], [STRIKE.duration, rest.coxa],
  ]);
  const femur = curve(t, [
    [0, rest.femur], [.045, rest.femur],
    [STRIKE.setEnd, 40], [STRIKE.approachEnd, 106],
    [STRIKE.sweepEnd, 50], [STRIKE.retractStart, 50],
    [STRIKE.retractEnd, 73], [STRIKE.holdEnd, 73],
    [STRIKE.resetEnd, rest.femur], [STRIKE.duration, rest.femur],
  ]);
  const gape = curve(t, [
    [0, rest.gape], [STRIKE.setEnd, 16],
    [STRIKE.approachEnd, 110], [STRIKE.closeStart, 126],
    [STRIKE.closeEnd, 20], [STRIKE.holdEnd, 20],
    [STRIKE.resetEnd, rest.gape], [STRIKE.duration, rest.gape],
  ]);
  // The tarsus is a walking appendage; fold it back, not towards imaginary prey.
  const tarsusFold = ramp(.12, STRIKE.setEnd, t) * (1 - ramp(STRIKE.holdEnd, STRIKE.resetEnd, t));
  const lunge = ramp(STRIKE.sweepStart - .010, STRIKE.sweepEnd, t)
    * (1 - ramp(STRIKE.retractStart, STRIKE.retractEnd, t));
  const attention = ramp(.045, STRIKE.setEnd, t) * (1 - ramp(STRIKE.holdEnd, STRIKE.resetEnd, t));
  const feeding = ramp(STRIKE.retractStart, STRIKE.retractEnd, t)
    * (1 - ramp(STRIKE.holdEnd, STRIKE.resetEnd, t));
  return {
    coxa, femur, gape, tarsusFold,
    // A small leg-supported translation. Feet are solved back to their anchors.
    shift: [0, -.025 * lunge, .16 * lunge],
    headPitch: -.045 * attention + .12 * feeding,
    abdomenPitch: .018 * lunge,
    sensingGain: 1 - .8 * attention,
    phase: strikePhase(t),
  };
}

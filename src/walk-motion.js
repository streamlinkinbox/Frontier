/**
 * Slow, four-legged wave walk. One cautious gait, not every form of mantis
 * locomotion. Middle/hind pairs walk; the raptorial pair is carried folded.
 *
 * The old loop advanced middle-before-hind on each side and prescribed a foot
 * speed inconsistent with the declared controller velocity. Here the order is
 * HL → ML → HR → MR. An 80% support duty factor leaves at least three feet down.
 *
 * Model-construction positions are centimetres. Controller speed is in m/s.
 * A planted foot moves at -controller speed in the in-place clip. Adding the
 * declared forward translation therefore leaves the contact fixed in the world.
 * Swing trajectories match that velocity at lift-off AND touchdown (C2 in Z).
 *
 * Ref: JEB 215:4255, doi:10.1242/jeb.073643 (mantis comparison in Discussion).
 * This is an authored, reference-informed slow gait, not measured motion capture.
 */
export const WALK = Object.freeze({
  revision: 'slow-wave-v2',
  duration: 2.4,
  sampleRate: 120,
  swingFraction: .20,
  speedMetresPerSecond: .0025,
  liftCm: .12,
  phaseOffsets: Object.freeze({ Hind_L: 0, Middle_L: .75, Hind_R: .5, Middle_R: .25 }),
  order: Object.freeze(['Hind_L', 'Middle_L', 'Hind_R', 'Middle_R']),
});
const TAU = Math.PI * 2;
const quintic = u => u * u * u * (10 + u * (-15 + 6 * u));
const wrap = n => ((n % 1) + 1) % 1;
export function sampleWalkingFoot(time, name) {
  if (!(name in WALK.phaseOffsets)) throw new Error(`Unknown walking leg: ${name}`);
  const phase = wrap(time / WALK.duration + WALK.phaseOffsets[name]);
  const speedCm = WALK.speedMetresPerSecond * 100;
  const fullAdvanceCm = speedCm * WALK.duration;
  const strokeCm = fullAdvanceCm * (1 - WALK.swingFraction);
  const swing = phase < WALK.swingFraction;
  if (!swing) return {
    phase, swing, liftCm: 0, toeLift: 0,
    offsetZCm: strokeCm / 2 - fullAdvanceCm * (phase - WALK.swingFraction),
  };
  const u = phase / WALK.swingFraction;
  // The -v*t component cancels root travel. Ground-space toe velocity is zero
  // at either end, rather than the old stop/start slip at each plant.
  const offsetZCm = -strokeCm / 2 + fullAdvanceCm * quintic(u) - fullAdvanceCm * WALK.swingFraction * u;
  const hump = 64 * u ** 3 * (1 - u) ** 3;
  return { phase, swing, offsetZCm, liftCm: WALK.liftCm * hump, toeLift: .10 * hump };
}
export function walkingBodyShift(time) {
  const p = time / WALK.duration;
  return [.018 * Math.sin(TAU * (p + .065)), .003 * Math.sin(TAU * p * 2), .005 * Math.sin(TAU * p * 2)];
}
export function walkContactLabel(time) {
  const swing = WALK.order.find(name => sampleWalkingFoot(time, name).liftCm > .002);
  const names = { Hind_L: 'LEFT HIND', Middle_L: 'LEFT MIDDLE', Hind_R: 'RIGHT HIND', Middle_R: 'RIGHT MIDDLE' };
  return swing ? `${names[swing]} STEP` : 'ALL FEET PLANTED';
}

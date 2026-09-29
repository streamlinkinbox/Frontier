/**
 * Held foreleg / wing threat display, not a prey-capture strike or human karate.
 * The clip is already in the display at frame 0 and loops seamlessly. Blend into
 * it over ~0.65 seconds (as the viewer does); it does not repeatedly drop the arms.
 *
 * Mantis religiosa exposes its inner coxal markings and raises/spreads its wings
 * during a deimatic display. Authored pose from species references, not mocap.
 */
export const STANCE = Object.freeze({
  revision: 'deimatic-hold-v1',
  duration: 6,
  sampleRate: 60,
  blendSeconds: .65,
  loop: true,
});
const TAU = Math.PI * 2;
const rad = n => n * Math.PI / 180;
export function sampleStance(time, side = 1) {
  const p = time / STANCE.duration, breath = Math.sin(TAU * p);
  return {
    shift: [.012 * breath, .020 + .004 * Math.sin(TAU * p * 2), 0],
    prothoraxPitch: rad(-26 + .7 * breath),
    prothoraxRoll: .012 * breath,
    headPitch: rad(18) - .012 * breath,
    headYaw: .025 * Math.sin(TAU * p),
    coxa: 8 + side * 3 + 1.5 * Math.sin(TAU * p + side * .3),
    femur: 79 + side * 4 + 1.2 * Math.sin(TAU * p + side * .4),
    gape: 60 + side * 5 + 1.5 * Math.sin(TAU * p + side * .2),
    splay: rad(side * (65 + 1.2 * breath)),
    tarsusFold: .08,
    forewingPitch: .93 + .012 * breath,
    forewingYaw: -side * (.39 + .008 * breath),
    hindwingPitch: .55 + .010 * breath,
    hindwingYaw: -side * .14,
    membraneUnfold: .98 + .02 * Math.sin(TAU * p) ** 2,
    abdomenPitch: .025 + .008 * breath,
  };
}

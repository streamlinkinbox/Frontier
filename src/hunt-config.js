/** Viewer behavior parameters, NOT measured species-specific biological timings. */
export const HUNT = Object.freeze({
  radiusMetres: .0023,
  forelegOriginZ: .0208,
  limits: Object.freeze({ angle: [-30, 30], distance: [12, 36], height: [18, 43] }),
  initial: Object.freeze({ angle: 0, distance: 20, height: 32 }),
  lockSeconds: .38,
  leanSeconds: 1.15,
  sweepSeconds: .030,
  clampStart: .010,
  captureSeconds: .054,
  retractSeconds: .36,
  recoverySeconds: .48,
  fixedStep: 1 / 240,
  targetMotionTolerance: .0008,
  skinPaddingMetres: .0009,
  contactToleranceMetres: .00065,
});
export const clamp = (n, a, b) => Math.min(b, Math.max(a, n));
export const smooth = (a, b, t) => { const u = clamp((t-a)/(b-a),0,1); return u*u*(3-2*u); };

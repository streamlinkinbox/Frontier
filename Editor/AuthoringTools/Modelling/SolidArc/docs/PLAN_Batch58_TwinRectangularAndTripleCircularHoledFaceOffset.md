# Batch 58 plan — exact twin-rectangular-hole and triple-circular-hole prism face offsets

## Scope

Promote one consolidated, distinct multi-loop face-offset batch:

1. An origin-centred, axis-aligned rectangular prism with exactly two rectangular through-holes
   (`V24/E36/C72/L18/F14`, genus 2).
2. An origin-centred, axis-aligned rectangular prism with exactly three circular through-holes
   (`V14/E21/C42/L15/F9`, genus 3).

Both domains support only a finite positive extension of the upper planar cap along +Z. The
reconstruction must retain closed topology and use analytic planar, linear, circular, and
extrusion supports. It must not mutate the source or invoke healing, Boolean approximation,
or tessellation.

## Recognition contract

The strict readers require:

- one closed solid and one hull with the exact topology counts above;
- a planar +Z upper cap with three loops for the rectangular-hole case or four loops for the
  triple-circular case;
- origin-centred, axis-aligned outer bounds, lower Z at zero, and only planar caps plus analytic
  extrusion walls;
- two disjoint inner rectangles for the genus-two route, or three equal-radius, disjoint,
  collinear circular loops for the genus-three route;
- reciprocal edge incidence, analytic curve classifications, and no malformed supports.

The readers refuse translated, tilted, oblique, arbitrary, mixed/freeform, unsupported-loop,
healing-dependent, lower-face, side-face, invalid-distance, and cross-domain inputs.

## Verification and proof

Use one verifier, `TwinRectangularAndTripleCircularHoledFaceOffsetVerification.cpp`, with:

- distances 0.5, 1.5, and 3.0;
- exact topology and closed-form volume checks;
- source immutability and dispatcher coverage;
- lower/side, invalid, translated, malformed, wrong-hole-count, and cross-domain refusal;
- one durable four-body proof: `Proofs/Batch58_TwinRectangularAndTripleCircularHoledFaceOffset.png`.

Register the verifier and proof in the standalone CMake target, the dependency-free full gate,
the capability roadmap, and the proof audit.

## Explicit non-goals

No arbitrary number of holes, rectangular holes in the circular route, circular holes in the
rectangular route, non-collinear circles, unequal circular radii, translated or tilted profiles,
oblique/freeform/mixed supports, inward offsets, lower/side offsets, general multi-loop editing,
or healing is promoted by this batch.

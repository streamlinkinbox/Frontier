# Batch 59 plan — exact twin-elliptical-hole and mixed elliptical/circular-hole prism face offsets

## Scope

Promote one consolidated genus-two multi-loop batch with two distinct analytic support domains:

1. An origin-centred, axis-aligned rectangular prism with exactly two canonical elliptical
   through-holes.
2. An origin-centred, axis-aligned rectangular prism with one canonical elliptical and one
   canonical circular through-hole.

Both sources have `V12/E18/C36/L12/F8` closed genus-two topology and support only a finite
positive +Z extension of the upper three-loop planar cap.

## Recognition contract

The strict reader requires the exact canonical outer rectangle `[-10,-6]` to `[10,6]`,
lower/upper levels `Z=0` and `Z=6`, reciprocal analytic edge incidence, two inner loops,
and only planar caps plus analytic extrusion walls.

The twin-ellipse route requires ellipse centres `(-4,0)` and `(4,0)` with major/minor radii
`2.2/1.4`. The mixed route requires the left ellipse with those dimensions and a radius `1.6`
circle centred at `(4,0)`. Arbitrary dimensions, placements, loop counts, circles-only,
rectangles, translated/tilted/oblique/freeform/mixed unsupported profiles, malformed topology,
lower/side selections, and invalid distances refuse transactionally.

## Verification and proof

Use one verifier, `TwinEllipticalAndEllipticalCircularHoledFaceOffsetVerification.cpp`, with:

- distances 0.5, 1.5, and 3.0;
- exact topology, analytic support, ellipse/circle area volume identities, and source immutability;
- dispatcher coverage and explicit refusal boundaries;
- one durable four-body proof:
  `Proofs/Batch59_TwinEllipticalAndEllipticalCircularHoledFaceOffset.png`.

Register the verifier and proof in the standalone CMake target, dependency-free full gate,
capability roadmap, and proof audit.

## Explicit non-goals

No arbitrary hole counts, unequal/noncanonical ellipses, arbitrary mixed curves, rectangular or
circle-only profiles through these APIs, translated/tilted/oblique/freeform supports, inward or
lower/side offsets, healing, or general multi-loop face editing is promoted by this batch.

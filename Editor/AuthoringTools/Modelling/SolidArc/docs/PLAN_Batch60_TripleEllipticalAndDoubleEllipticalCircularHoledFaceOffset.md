# Batch 60 plan — exact triple-elliptical-hole and double-elliptical/circular-hole prism face offsets

## Scope

Promote one consolidated genus-three multi-loop batch with two distinct analytic support domains:

1. An origin-centred, axis-aligned rectangular prism with exactly three canonical elliptical
   through-holes.
2. An origin-centred, axis-aligned rectangular prism with exactly two canonical elliptical and
   one canonical circular through-hole.

Both sources have `V14/E21/C42/L15/F9` closed genus-three topology and support only a finite
positive +Z extension of the upper four-loop planar cap.

## Recognition contract

The strict reader requires the exact canonical outer rectangle `[-12,-7]` to `[12,7]`,
lower/upper levels `Z=0` and `Z=6`, reciprocal analytic edge incidence, three inner loops,
and only planar caps plus analytic extrusion walls.

The triple-ellipse route requires ellipse centres `(-6,0)`, `(0,0)`, and `(6,0)` with
major/minor radii `1.8/1.2`. The mixed route requires the first two canonical ellipses and
a radius `1.4` circle centred at `(6,0)`. Arbitrary dimensions, placements, loop counts,
translated/tilted/oblique/freeform profiles, malformed topology, lower/side selections, and
invalid distances refuse transactionally.

## Verification and proof

Use one verifier, `TripleEllipticalAndDoubleEllipticalCircularHoledFaceOffsetVerification.cpp`,
with distances 0.5, 1.5, and 3.0; exact topology; ellipse/circle area volume identities;
source immutability; dispatcher coverage; refusal boundaries; and one durable four-body proof:

`Proofs/Batch60_TripleEllipticalAndDoubleEllipticalCircularHoledFaceOffset.png`

Register the verifier and proof in the standalone CMake target, dependency-free full gate,
capability roadmap, and proof audit.

## Explicit non-goals

No arbitrary ellipse dimensions, hole counts, rectangular/circle-only or arbitrary mixed loops,
translated/tilted/oblique/freeform supports, inward or lower/side offsets, healing, or general
multi-loop face editing is promoted by this batch.

# Plan — Phase 48 bounded orthogonal concave-prism shell

## Capability slice

Extend the shell/thicken family into a non-convex planar-profile domain: a closed axis-aligned
straight prism whose upper cap is one six-edge orthogonal L-shaped concave profile. A positive shell
thickness removes the selected upper cap, offsets the retained profile inward, and reconstructs an
open-top concave shell with an inset floor.

This is a distinct modelling operation from Phase 47's concave face offset and a distinct profile
from Phase 40's convex hexagonal shell. It does not widen shelling to arbitrary concave polygons,
Boolean healing, or freeform B-reps.

The focused fixture uses the counter-clockwise L profile
`(-6,-4) -> (6,-4) -> (6,-1) -> (-1,-1) -> (-1,4) -> (-6,4)`,
height `6`, and shell thickness `0.75`. The source is `V12/E18/C36/L8/F8`; the closed shell result
is `V24/E42/C84/L20/F20`, genus zero.

## Source and reconstruction contract

Add `FaceEditSolver::ShellExtrudedConcavePrism`. It accepts only a closed one-hull genus-zero
axis-aligned orthogonal six-edge concave prism with two Z levels, vertical generators, and its upper
planar cap selected. It must reject convex six-sided profiles, other-sided profiles, non-orthogonal
concave profiles, and curved or oblique supports.

The route offsets each cap edge inward by the positive wall thickness, intersects adjacent offset
lines, builds six outer walls, six inset walls, six top-rim ruled faces, and lets the validated sew
produce the outer bottom and inset floor. It validates closed topology, analytic supports, and the
outer-prism-minus-inner-cavity volume identity without healing.

## Refusal boundary

Refuse boxes through the new API, convex or other-sided profiles, non-orthogonal or infeasible
concave profiles, lower/side faces, cylinders, tilted/oblique/freeform/mixed supports, invalid or
over-thick thicknesses, malformed topology, and healing-dependent fallback. The public `Shell`
dispatcher reaches this route only after the existing canonical-box and convex-prism shell routes
decline.

## Verification and proof

`ConcavePrismShellVerification` checks source/result topology, closed-manifold validity, the inset
concave floor, planar/extrusion/ruled support counts, the analytic shell volume, source immutability,
public shell dispatch, and all refusal boundaries. The durable proof will be
`Proofs/Phase48_ConcavePrismShell.png`.

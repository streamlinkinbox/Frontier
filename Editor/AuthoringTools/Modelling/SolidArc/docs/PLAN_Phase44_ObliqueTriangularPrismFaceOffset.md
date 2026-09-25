# Plan — Phase 44 bounded oblique triangular-prism face offset

## Capability slice

Add the next distinct non-cone modelling domain: a non-axis-aligned oblique extrusion. The source
is a triangular prism whose three generators are parallel to one common slanted axis rather than
world +Z. A positive offset of its selected planar cap extends the extrusion along that cap normal,
while preserving the triangular profile and oblique generators.

This is not another radius, sweep, set-back, fillet, chamfer, or circular/elliptical profile
variant. It specifically advances bounded support handling beyond the axis-aligned routes proved
in Phases 40–43.

The focused fixture uses a triangular profile in the XY plane, normalized generator direction
`(0.35, 0.20, 1.0)`, source generator length `7.0`, and offset distance `1.4`. Its exact topology
is `V6/E9/C18/L5/F5`.

## Source and reconstruction contract

Add `FaceEditSolver::OffsetObliqueTriangularPrism`. It accepts only a closed genus-zero triangular
straight prism with one selected planar cap, one common non-vertical generator axis, line edges,
two projection levels along that axis, and no inner loops. The cap profile is translated backward
along the detected axis and rebuilt with the same axis at source length plus the offset.

The verifier checks the oblique triangular-prism volume identity: profile area times the axis's
normal component to the XY profile plane times the extended generator length.

## Refusal boundary

The route refuses axis-aligned/vertical prisms, boxes, pentagonal/hexagonal profiles, cylinders,
elliptical profiles, holed/multi-loop profiles, lower or side faces, non-parallel generators,
non-planar/freeform supports, zero/negative/non-finite offsets, malformed topology, and
healing-dependent fallback. The public `OffsetFace` dispatcher may delegate this exact oblique
triangular case only after the existing box, pentagonal, holed, and elliptical routes decline.

## Verification and proof

`ObliqueTriangularPrismFaceOffsetVerification` independently constructs the oblique triangular
prism and checks exact topology, non-vertical axis extraction, cap selection, generator parallelism,
volume, profile preservation, source immutability, public dispatch, deterministic refusal boundaries,
and malformed transactional behavior. The durable proof will be
`Proofs/Phase44_ObliqueTriangularPrismFaceOffset.png`.

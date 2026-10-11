# Plan — Phase 47 bounded orthogonal concave-prism face offset

## Capability slice

Advance the non-box face-offset family into a non-convex planar-profile domain: a closed axis-aligned
straight prism whose upper cap is one six-edge orthogonal L-shaped concave profile. A positive offset
of the selected upper cap extends the prism without convexifying, healing, or approximating the
reflex profile.

This is a distinct profile topology from the convex pentagonal/hexagonal routes and from the
multi-loop genus-two Phase 46 route. It does not widen face offset to arbitrary concave polygons,
Boolean healing, or freeform B-reps.

The focused fixture uses the counter-clockwise L profile
`(-6,-4) -> (6,-4) -> (6,-1) -> (-1,-1) -> (-1,4) -> (-6,4)`,
height `6`, and positive upper-cap offset `1.5`. Its exact source/result topology is
`V12/E18/C36/L8/F8`, genus zero.

## Source and reconstruction contract

Add `FaceEditSolver::OffsetExtrudedConcavePrism`. It accepts only a closed one-hull genus-zero
axis-aligned straight prism with six line edges on its upper planar cap, two Z levels, vertical
line generators, and exactly one orthogonal reflex vertex in the cap profile. It must reject convex
six-sided profiles, non-orthogonal or arbitrary concave profiles, multi-loop caps, and curved or
oblique supports.

The route reconstructs the identical six-edge L profile at the source lower Z and extrudes it to
`source height + offset`. It validates the exact solid result and never mutates the source.

## Refusal boundary

Refuse convex profiles, triangular/pentagonal/other-sided profiles, boxes through the new API,
self-intersecting or degenerate profiles, non-orthogonal concave profiles, holes, lower/side faces,
oblique/tilted/freeform/mixed supports, cylinders, invalid offsets, malformed topology, and
healing-dependent fallback.

## Verification and proof

`ConcavePrismFaceOffsetVerification` independently constructs the L-prism and checks the exact
source/result topology, one reflex upper profile, axis-aligned generators, planar/extrusion support,
L-profile area-times-height volume identity, source immutability, public `OffsetFace` dispatch,
and all refusal boundaries. The durable proof will be
`Proofs/Phase47_ConcavePrismFaceOffset.png`.

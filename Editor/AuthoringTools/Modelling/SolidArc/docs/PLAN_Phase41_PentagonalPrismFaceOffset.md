# Plan — Phase 41 bounded pentagonal-prism face offset

## Capability slice

Add the next non-cone topology route as a face-edit operation rather than another fillet or shell.
The source is a capped convex pentagonal prism with one selected upper planar cap. A positive
normal offset extends that cap and its five straight side faces while preserving the exact polygon
profile and source immutability.

The distinct fixture is a regular pentagonal prism with circumradius 4.5 and height 7.0. Its exact
source topology is `V10/E15/C30/L7/F7`; the offset distance is 1.25 and the result retains that
same non-box topology with increased analytic prism volume.

## Source and reconstruction contract

Add a separate `FaceEditSolver::OffsetExtrudedConvexPrism` route. It accepts only a closed convex
five-sided straight prism, its upper planar cap, axis-aligned vertical generators, and one finite
positive offset distance. It derives the ordered cap polygon and the lower/upper bounds, rebuilds
an exact extrusion from the unchanged polygon at the extended height, and validates the resulting
`V10/E15/C30/L7/F7` solid.

The existing axis-aligned box offset route remains separate. This phase deliberately proves a
non-box, non-cone topology and does not broaden face offset to arbitrary trimmed or curved B-reps.

## Refusal boundary

The route refuses boxes through this distinct API, six-sided prisms, triangular or concave profiles,
non-planar/curved supports, lower-cap selections, zero/negative/non-finite offsets, malformed
sources, and healing-dependent fallback. The public offset dispatcher may delegate only the exact
pentagonal-prism case after the legacy box route declines.

## Verification and proof

`PentagonalPrismFaceOffsetVerification` constructs the regular pentagonal prism independently and
checks exact source/result topology, cap selection, analytic volume increase, profile preservation,
source immutability, public dispatch, and failure-oriented box/hex/triangle/concave/curved/invalid/
malformed refusals. The durable proof will be
`Proofs/Phase41_PentagonalPrismFaceOffset.png`.
